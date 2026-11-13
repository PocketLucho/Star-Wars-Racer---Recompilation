#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "aica.h"

static int dbg_env_aicawlog(void) { static int v = -1; if (v < 0) v = getenv("AICAWLOG") != 0; return v; }
static int dbg_env_aicakeylog(void) { static int v = -1; if (v < 0) v = getenv("AICAKEYLOG") != 0; return v; }

/* ---- register helpers (16-bit registers, little endian) ---- */
static inline uint16_t R16(Aica *a, uint32_t o) { return (uint16_t)(a->regs[o & 0x7ffe] | (a->regs[(o & 0x7ffe) + 1] << 8)); }
static inline void W16(Aica *a, uint32_t o, uint16_t v) { a->regs[o & 0x7ffe] = (uint8_t)v; a->regs[(o & 0x7ffe) + 1] = (uint8_t)(v >> 8); }
#define CR(c, off) R16(a, (c) * 0x80 + (off))

enum { SCIEB = 0x289c, SCIPD = 0x28a0, SCIRE = 0x28a4, SCILV0 = 0x28a8, SCILV1 = 0x28ac, SCILV2 = 0x28b0,
       MCIEB = 0x28b4, MCIPD = 0x28b8, MCIRE = 0x28bc, REG_L = 0x2d00, REG_M = 0x2d04 };

/* ---- tables ---- */
static const double attack_ms[64] = {
    -1,-1,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
    1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
    76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
    2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0 };
static const double dsr_ms[64] = {
    -1,-1,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
    14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
    920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
    28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1 };
static int32_t att_step[64], dsr_step[64];            /* AEG units (<<16) per sample */
static int32_t gain_lut[1024];                        /* attenuation unit (0.375 dB) -> gain << 15 */
static const int send_level[16] = { 255, 14 << 3, 13 << 3, 12 << 3, 11 << 3, 10 << 3, 9 << 3, 8 << 3, 7 << 3, 6 << 3, 5 << 3, 4 << 3, 3 << 3, 2 << 3, 1 << 3, 0 };
static const int32_t adpcm_qs[8] = { 0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266 };
static int tables_done;
uint64_t g_aica_keyons[2], g_aica_sh4_w[2], g_aica_arm_w[2];

static void init_tables(void) {
    if (tables_done) return;
    tables_done = 1;
    const double full = 1023.0 * 65536.0;
    for (int i = 0; i < 64; i++) {
        att_step[i] = attack_ms[i] < 0 ? 0 : attack_ms[i] == 0 ? (int32_t)full : (int32_t)(full / (44.1 * attack_ms[i]));
        dsr_step[i] = dsr_ms[i] < 0 ? 0 : (int32_t)(full / (44.1 * dsr_ms[i]));
    }
    for (int i = 0; i < 1024; i++) gain_lut[i] = i >= 255 * 4 ? 0 : (int32_t)(32768.0 / pow(2.0, i / 16.0));
}

static int eff_rate(Aica *a, int c, int rate) {
    int e = rate * 2;
    int krs = (CR(c, 0x14) >> 10) & 15, oct = (CR(c, 0x18) >> 11) & 15, fns = CR(c, 0x18) & 0x3ff;
    if (krs < 15) { e += (fns >> 9) & 1; int k = (krs + (oct ^ 8) - 8) * 2; if (k > 0) e += k; }
    return e > 63 ? 63 : e;
}

/* ---- interrupts ---- */
static void update_arm_irq(Aica *a) {
    uint32_t p = R16(a, SCIEB) & R16(a, SCIPD) & 0x7ff;
    if (p && !a->fiq_out) {
        int bit = 0; while (!(p & (1u << bit))) bit++;
        if (bit > 7) bit = 7;
        uint32_t L = ((R16(a, SCILV0) >> bit) & 1) | (((R16(a, SCILV1) >> bit) & 1) << 1) | (((R16(a, SCILV2) >> bit) & 1) << 2);
        a->fiq_out = 1; a->fiq_L = L;
    }
    arm7_set_fiq(&a->arm, a->fiq_out);
}
static void update_sh4_irq(Aica *a) { a->sh4_irq = (R16(a, MCIEB) & R16(a, MCIPD) & 0x7ff) != 0; }
static void raise_both(Aica *a, uint16_t bits) {
    W16(a, SCIPD, R16(a, SCIPD) | bits); W16(a, MCIPD, R16(a, MCIPD) | bits);
    update_arm_irq(a); update_sh4_irq(a);
}

/* ---- channels ---- */
static void key_on(Aica *a, int c) {
    AicaChan *h = &a->ch[c];
    if (h->active && h->eg_state != 3) return;
    g_aica_keyons[a->id]++;
    h->active = 1; h->pos = 0; h->frac = 0; h->looped = 0; h->eg_state = 0; h->att = 0x3ff << 16;
    h->sa = (((uint32_t)CR(c, 0x00) & 0x7f) << 16) | CR(c, 0x04);
    h->prev = h->cur = 0; h->adpcm_q = 0x7f; h->adpcm_last = 0; h->adpcm_loop_saved = 0;
    if (att_step[eff_rate(a, c, CR(c, 0x10) & 31)] >= 0x3ff * 65536) h->att = 0;   /* instant attack */
}
static void key_off(Aica *a, int c) { if (a->ch[c].active) a->ch[c].eg_state = 3; }

static void keyonex(Aica *a) {
    for (int c = 0; c < 64; c++) {
        uint16_t r = CR(c, 0x00);
        if (r & 0x4000) key_on(a, c); else key_off(a, c);
    }
}

static int16_t fetch(Aica *a, int c, AicaChan *h, uint32_t idx) {
    int pcms = (CR(c, 0x00) >> 7) & 3;
    if (pcms == 0) { uint32_t o = (h->sa + idx * 2) & a->ram_mask; return (int16_t)(a->ram[o] | (a->ram[(o + 1) & a->ram_mask] << 8)); }
    if (pcms == 1) { return (int16_t)((int8_t)a->ram[(h->sa + idx) & a->ram_mask] << 8); }
    /* Yamaha ADPCM: decoded sequentially (idx advances by one each call) */
    uint8_t b = a->ram[(h->sa + (idx >> 1)) & a->ram_mask];
    int d = (idx & 1) ? (b >> 4) & 15 : b & 15;
    int32_t delta = (h->adpcm_q * (((d & 7) << 1) | 1)) >> 3;
    if (d & 8) delta = -delta;
    int32_t v = h->adpcm_last + delta;
    if (v > 32767) v = 32767; if (v < -32768) v = -32768;
    h->adpcm_last = v;
    h->adpcm_q = (h->adpcm_q * adpcm_qs[d & 7]) >> 8;
    if (h->adpcm_q < 0x7f) h->adpcm_q = 0x7f; if (h->adpcm_q > 0x6000) h->adpcm_q = 0x6000;
    return (int16_t)v;
}

static void channel_sample(Aica *a, int c, int32_t *L, int32_t *Rr) {
    AicaChan *h = &a->ch[c];
    if (!h->active) return;
    uint16_t r0 = CR(c, 0x00);
    int lpctl = (r0 >> 9) & 1, pcms = (r0 >> 7) & 3;
    uint32_t lsa = CR(c, 0x08), lea = CR(c, 0x0c);
    /* envelope */
    int ar = CR(c, 0x10) & 31, d1r = (CR(c, 0x10) >> 6) & 31, d2r = (CR(c, 0x10) >> 11) & 31, rr = CR(c, 0x14) & 31;
    int dl = (CR(c, 0x14) >> 5) & 31;
    switch (h->eg_state) {
    case 0: h->att -= att_step[eff_rate(a, c, ar)]; if (h->att <= 0) { h->att = 0; h->eg_state = 1; } break;
    case 1: h->att += dsr_step[eff_rate(a, c, d1r)]; if ((h->att >> 16) >= (dl << 5)) h->eg_state = 2; break;
    case 2: h->att += dsr_step[eff_rate(a, c, d2r)]; break;
    case 3: h->att += dsr_step[eff_rate(a, c, rr)]; break;
    }
    if (h->att >= 0x3ff << 16) { h->att = 0x3ff << 16; if (h->eg_state == 3) { h->active = 0; return; } }
    /* sample (linear interpolation between the last two decoded samples) */
    int32_t s = h->prev + (((int32_t)h->cur - h->prev) * (int32_t)h->frac >> 10);
    /* advance */
    h->frac += h->step;
    while (h->frac >= 1024) {
        h->frac -= 1024;
        h->pos++;
        if (h->pos == lsa && pcms >= 2 && !h->adpcm_loop_saved) { h->adpcm_q_loop = h->adpcm_q; h->adpcm_last_loop = h->adpcm_last; h->adpcm_loop_saved = 1; }
        if (h->pos >= lea && lea) {
            h->looped = 1;
            if (lpctl) { h->pos = lsa; if (pcms >= 2 && h->adpcm_loop_saved) { h->adpcm_q = h->adpcm_q_loop; h->adpcm_last = h->adpcm_last_loop; } }
            else { h->active = 0; return; }
        }
        h->prev = h->cur;
        h->cur = fetch(a, c, h, h->pos);
    }
    /* volume: TL + AEG + direct send level + pan */
    uint16_t r28 = CR(c, 0x28), r24 = CR(c, 0x24);
    int tl = (r28 & 0x40) ? 0 : r28 >> 8;
    int disdl = (r24 >> 8) & 15, dipan = r24 & 31;
    if (!disdl) return;
    int full = tl + send_level[disdl] + (h->att >> 18);
    int pan = full + send_level[(~dipan) & 15];
    int la = (dipan & 0x10) ? full : pan, ra = (dipan & 0x10) ? pan : full;
    if (la > 1023) la = 1023; if (ra > 1023) ra = 1023;
    *L += (s * gain_lut[la]) >> 15;
    *Rr += (s * gain_lut[ra]) >> 15;
}

static void update_step(Aica *a, int c) {
    uint16_t r = CR(c, 0x18);
    uint32_t oct = (r >> 11) & 15, rate = 1024 | (r & 0x3ff);
    if (oct & 8) rate >>= 16 - oct; else rate <<= oct;
    a->ch[c].step = rate;
}

/* ---- common register side effects ---- */
static void dma(Aica *a) {
    uint16_t r8c = R16(a, 0x288c), r88 = R16(a, 0x2888), r80 = R16(a, 0x2880), r84 = R16(a, 0x2884);
    if (!(r8c & 1)) return;
    uint32_t dlg = (r8c >> 2) & 0x1fff, ddir = (r8c >> 15) & 1, dgate = (r88 >> 15) & 1;
    uint32_t mem = ((((uint32_t)r80 >> 9) & 0x7f) << 16 | (r84 & 0xfffc)) & a->ram_mask, reg = (r88 & 0x7ffc);
    for (uint32_t i = 0; i < dlg; i++, mem = (mem + 4) & a->ram_mask, reg += 4) {
        if (ddir) { uint32_t v = dgate ? 0 : R16(a, reg); memcpy(a->ram + mem, &v, 4); }
        else W16(a, reg, dgate ? 0 : (uint16_t)(a->ram[mem] | (a->ram[mem + 1] << 8)));
    }
    W16(a, 0x288c, r8c & ~1u);
    raise_both(a, 1 << 4);
}

static void reg_write16(Aica *a, uint32_t o, uint16_t v, int from_arm) {
    o &= 0x7ffe;
    if (from_arm) g_aica_arm_w[a->id]++; else g_aica_sh4_w[a->id]++;
    if (from_arm) a->wcount[o >> 1]++;
    if (o < 0x2000 && (o & 0x7f) == 0 && dbg_env_aicakeylog()) { static int n; if (n++ < 300) fprintf(stderr, "AICA%d %s ch%02d r0=%04x smp=%llu\n", a->id, from_arm ? "arm" : "sh4", o >> 7, v, (unsigned long long)a->samples); }
    switch (o) {
    case SCIEB: W16(a, o, v & 0x7ff); update_arm_irq(a); return;
    case SCIPD: if (v & 0x20) { W16(a, o, R16(a, o) | 0x20); update_arm_irq(a); } return;
    case SCIRE: W16(a, SCIPD, R16(a, SCIPD) & ~v); update_arm_irq(a); return;
    case MCIEB: W16(a, o, v & 0x7ff); update_sh4_irq(a); return;
    case MCIPD: if (v & 0x20) { W16(a, o, R16(a, o) | 0x20); update_sh4_irq(a); } return;
    case MCIRE: W16(a, MCIPD, R16(a, MCIPD) & ~v); update_sh4_irq(a); return;
    case REG_M: if (from_arm && (v & 1)) { a->fiq_out = 0; update_arm_irq(a); } return;
    case REG_L: if (from_arm) return; break;
    }
    W16(a, o, v);
    if (o < 0x2000) {
        int c = o >> 7, r = o & 0x7f;
        if (r == 0x00 && (v & 0x8000)) { keyonex(a); W16(a, o, v & 0x7fff); }
        if (r == 0x18) update_step(a, c);
    } else if (o == 0x288c) dma(a);
}

static uint16_t reg_read16(Aica *a, uint32_t o, int from_arm) {
    o &= 0x7ffe;
    if (from_arm && o == REG_L) return (uint16_t)a->fiq_L;
    if (o == 0x2810 || o == 0x2814) {                  /* monitor of channel MSLC */
        int c = (R16(a, 0x280c) >> 8) & 63; AicaChan *h = &a->ch[c];
        if (o == 0x2814) return (uint16_t)h->pos;
        uint16_t v = (uint16_t)((h->att >> 16) & 0x1fff) | (uint16_t)((h->active ? h->eg_state : 3) << 13) | (h->looped ? 0x8000 : 0);
        h->looped = 0;
        return v;
    }
    if (o == 0x2808) return (R16(a, o) & 0x00ff) | 0x0900;   /* MIDI in empty, out empty */
    return R16(a, o);
}

/* ---- SH-4 side ---- */
uint32_t aica_reg_read(Aica *a, uint32_t addr, int size) {
    uint32_t v = reg_read16(a, addr & ~1u, 0);
    if (size == 1) return (addr & 1) ? v >> 8 : v & 0xff;
    return v;
}
void aica_reg_write(Aica *a, uint32_t addr, uint32_t v, int size) {
    if (size == 1) {
        uint16_t old = R16(a, addr & ~1u);
        uint16_t nv = (addr & 1) ? (uint16_t)((old & 0x00ff) | ((v & 0xff) << 8)) : (uint16_t)((old & 0xff00) | (v & 0xff));
        if ((addr & 0x7ffe) < 0x2000 && (addr & 0x7f) == 0x01 && !(v & 0x80)) nv &= 0x7fff;   /* byte write without KYONEX */
        reg_write16(a, addr & ~1u, nv, 0);
    } else reg_write16(a, addr, (uint16_t)v, 0);
}

/* ---- ARM side memory map: 0x000000-0x7fffff RAM, 0x800000-0x807fff registers ---- */
static uint32_t arm_r32(void *ctx, uint32_t ad) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad >= 0x60 && ad < 0x510 && a->rcount) { a->rcount[ad]++; if (ad >= 0x88 && ad < 0x9c) fprintf(stderr, "ARM rd %02x pc=%08x lr=%08x smp=%llu\n", ad, a->arm.r[15], a->arm.r[14], (unsigned long long)a->samples); }
    if (ad < 0x800000) { uint32_t o = ad & a->ram_mask & ~3u; uint32_t v; memcpy(&v, a->ram + o, 4); return v; }
    return reg_read16(a, ad, 1);
}
static uint16_t arm_r16(void *ctx, uint32_t ad) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad >= 0x60 && ad < 0x510 && a->rcount) { a->rcount[ad]++; if (ad >= 0x88 && ad < 0x9c) fprintf(stderr, "ARM rd %02x pc=%08x lr=%08x smp=%llu\n", ad, a->arm.r[15], a->arm.r[14], (unsigned long long)a->samples); }
    if (ad < 0x800000) { uint32_t o = ad & a->ram_mask & ~1u; return (uint16_t)(a->ram[o] | (a->ram[o + 1] << 8)); }
    return reg_read16(a, ad, 1);
}
static uint8_t arm_r8(void *ctx, uint32_t ad) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad >= 0x60 && ad < 0x510 && a->rcount) { a->rcount[ad]++; if (ad >= 0x88 && ad < 0x9c) fprintf(stderr, "ARM rd %02x pc=%08x lr=%08x smp=%llu\n", ad, a->arm.r[15], a->arm.r[14], (unsigned long long)a->samples); }
    if (ad < 0x800000) return a->ram[ad & a->ram_mask];
    uint16_t v = reg_read16(a, ad & ~1u, 1); return (ad & 1) ? v >> 8 : v & 0xff;
}
static void arm_w32(void *ctx, uint32_t ad, uint32_t v) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad >= 0x50 && ad < 0xa0 && dbg_env_aicawlog()) fprintf(stderr, "ARM%d w32 %02x=%08x pc=%08x smp=%llu\n", a->id, ad, v, a->arm.r[15], (unsigned long long)a->samples);
    if (ad < 0x800000) { memcpy(a->ram + (ad & a->ram_mask & ~3u), &v, 4); return; }
    reg_write16(a, ad, (uint16_t)v, 1);
}
static void arm_w16(void *ctx, uint32_t ad, uint16_t v) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad < 0x800000) { uint32_t o = ad & a->ram_mask & ~1u; a->ram[o] = (uint8_t)v; a->ram[o + 1] = (uint8_t)(v >> 8); return; }
    reg_write16(a, ad, v, 1);
}
static void arm_w8(void *ctx, uint32_t ad, uint8_t v) {
    Aica *a = ctx; ad &= 0xffffff;
    if (ad < 0x800000) { a->ram[ad & a->ram_mask] = v; return; }
    uint16_t old = R16(a, ad & ~1u);
    uint16_t nv = (ad & 1) ? (uint16_t)((old & 0xff) | (v << 8)) : (uint16_t)((old & 0xff00) | v);
    if ((ad & 0x7ffe) < 0x2000 && (ad & 0x7f) == 0x01 && !(v & 0x80)) nv &= 0x7fff;
    reg_write16(a, ad & ~1u, nv, 1);
}

void aica_init(Aica *a, int id, uint8_t *ram, uint32_t ram_size) {
    init_tables();
    memset(a, 0, sizeof *a);
    a->id = id; a->ram = ram; a->ram_mask = ram_size - 1;
    a->arm.ctx = a; a->arm.read32 = arm_r32; a->arm.read16 = arm_r16; a->arm.read8 = arm_r8;
    a->arm.write32 = arm_w32; a->arm.write16 = arm_w16; a->arm.write8 = arm_w8;
    arm7_reset(&a->arm);
    a->arm_running = 0;
    a->arm_cycles_per_sample = 512;     /* ARM7 at 22.58 MHz (AICA core clock / 2) per 44.1 kHz sample */
    W16(a, 0x2800, 0x0010);             /* VER = 1 */
    if (getenv("AICASTATS")) { a->rcount = calloc(0x510, sizeof(uint32_t)); a->pchist = calloc(0x4000, sizeof(uint32_t)); }
}

void aica_set_arm_reset(Aica *a, int asserted) {
    if (asserted) a->arm_running = 0;
    else if (!a->arm_running) { arm7_reset(&a->arm); a->arm_running = 1; a->fiq_out = 0; a->arm.yield_pc = 0; a->samples_since_reset = 0; }
}

void aica_run(Aica *a, int n, int32_t *mix) {
    for (int i = 0; i < n; i++) {
        if (a->arm_running && !a->arm.yield_pc && a->samples_since_reset++ == 4096) {
            /* find the AM2 driver main loop: "stmdb sp!,{pc}; mov r0,r0; b x" calls ending in "b <head>" */
            for (uint32_t h = 0x400; h < 0x2000 && !a->arm.yield_pc; h += 4) {
                uint32_t w0, w1; memcpy(&w0, a->ram + h, 4); memcpy(&w1, a->ram + h + 4, 4);
                if (w0 != 0xe92d8000 || w1 != 0xe1a00000) continue;
                for (uint32_t e = h + 12; e < h + 0x100; e += 4) {
                    uint32_t b; memcpy(&b, a->ram + e, 4);
                    if ((b & 0xff000000) == 0xea000000) { int32_t off = (int32_t)(b << 8) >> 6; if (e + 8 + off == h) { a->arm.yield_pc = h; break; } }
                    uint32_t c; memcpy(&c, a->ram + e, 4);
                    if (c != 0xe92d8000 && c != 0xe1a00000 && (c & 0xff000000) != 0xea000000) break;
                }
            }
            if (getenv("AICASTATS")) fprintf(stderr, "AICA%d: main loop head %08x\n", a->id, a->arm.yield_pc);
        }
        /* after a full main-loop pass the driver idles for ARM_MAIN_DIV samples, unless an FIQ is pending */
        int run_arm = a->arm_running && (!a->arm_sleep || (a->arm.fiq_line && !(a->arm.cpsr & 0x40)));
        if (a->arm_sleep) a->arm_sleep--;
        if (run_arm) {
            a->arm.yielded = 0;
            arm7_run(&a->arm, a->arm_cycles_per_sample);
            if (a->arm.yielded && !a->arm_sleep) a->arm_sleep = 7; if (a->pchist) a->pchist[(a->arm.r[15] & 0xfffc) >> 2]++;
        }
        /* timers A/B/C at 44.1 kHz with prescaler 2^md */
        for (int t = 0; t < 3; t++) {
            uint32_t ro = 0x2890 + t * 4; uint16_t r = R16(a, ro); int md = (r >> 8) & 7;
            if (++a->timer_cnt[t] >= (1 << md)) {
                a->timer_cnt[t] = 0;
                uint16_t cnt = (uint16_t)((r + 1) & 0xff);
                W16(a, ro, (uint16_t)((r & 0xff00) | cnt));
                if (cnt == 0) raise_both(a, (uint16_t)(1 << (6 + t)));
            }
        }
        W16(a, SCIPD, R16(a, SCIPD) | 0x400); W16(a, MCIPD, R16(a, MCIPD) | 0x400);   /* sample done */
        update_arm_irq(a); update_sh4_irq(a);
        int32_t L = 0, Rr = 0;
        for (int c = 0; c < 64; c++) if (a->ch[c].active) channel_sample(a, c, &L, &Rr);
        int mvol = R16(a, 0x2800) & 15;
        if (mix) { mix[i * 2] += L * mvol / 15; mix[i * 2 + 1] += Rr * mvol / 15; }
        a->samples++;
    }
}

#include <stdio.h>
#include <stdlib.h>

void aica_stats(Aica *a, FILE *f) {
    int act = 0; for (int c = 0; c < 64; c++) act += a->ch[c].active;
    fprintf(f, "AICA%d: arm %s pc=%08x cpsr=%08x insns=%llu undef=%llu  keyons=%llu active=%d  regw sh4=%llu arm=%llu  mvol=%d SCIEB=%04x SCIPD=%04x MCIEB=%04x MCIPD=%04x\n",
        a->id, a->arm_running ? "run" : "reset", a->arm.r[15], a->arm.cpsr, (unsigned long long)a->arm.insns, (unsigned long long)a->arm.undefined,
        (unsigned long long)g_aica_keyons[a->id], act, (unsigned long long)g_aica_sh4_w[a->id], (unsigned long long)g_aica_arm_w[a->id],
        R16(a, 0x2800) & 15, R16(a, SCIEB), R16(a, SCIPD), R16(a, MCIEB), R16(a, MCIPD));
    fprintf(f, "  regs:"); for (int i = 0; i < 16; i++) fprintf(f, " r%d=%08x", i, a->arm.r[i]); fprintf(f, "\n");
    if (a->pchist) for (int i = 0; i < 0x4000; i++) if (a->pchist[i] > 20000) fprintf(f, "  pc %05x: %u\n", i * 4, a->pchist[i]);
    if (getenv("AICASTATS") && a->rcount) for (int i = 0; i < 0x510; i++) if (a->rcount[i]) fprintf(f, "  armr %04x: %u (now %08x)\n", i, a->rcount[i], *(uint32_t *)(a->ram + (i & ~3)));
    if (getenv("AICASTATS")) for (int i = 0; i < 0x4000; i++) if (a->wcount[i]) fprintf(f, "  armw %04x: %u (now %04x)\n", i * 2, a->wcount[i], R16(a, i * 2));
}
