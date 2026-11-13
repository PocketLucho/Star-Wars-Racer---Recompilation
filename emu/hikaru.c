/* Sega Hikaru machine model: memory map after MAME hikaru.cpp / 315-6154.cpp,
 * SH-4 on-chip peripherals, and logged stubs for the unknown devices. */
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "hikaru.h"
#include "gpu.h"
#include "mie.h"
#include "crypt5881.h"
#include "blockrt.h"
#include "romzip.h"
#include "aica.h"

static int dbg_env_aicawlog(void) { static int v = -1; if (v < 0) v = getenv("AICAWLOG") != 0; return v; }
static int dbg_env_aicakeylog(void) { static int v = -1; if (v < 0) v = getenv("AICAKEYLOG") != 0; return v; }

Machine *g_m;
void sh4_exception(Cpu *c, u32 evt, u32 vecoff, u32 retpc);

#define LOG(...) do { if (g_m->log) { fprintf(g_m->log, __VA_ARGS__); } } while (0)

static u8 *xalloc(size_t n) { u8 *p = calloc(1, n); if (!p) { perror("calloc"); exit(1); } return p; }

static int loadfile(const char *dir, const char *name, u8 *dst, size_t sz) { return (int)rom_read(dir, name, dst, sz); }

/* ROM_LOAD32_WORD: 16-bit words of file a at even word slots, file b at odd */
static void load32w(const char *dir, const char *a, const char *b, u8 *dst, size_t each) {
    u8 *A = xalloc(each), *B = xalloc(each);
    loadfile(dir, a, A, each); loadfile(dir, b, B, each);
    for (size_t i = 0; i < each; i += 2) {
        dst[i * 2 + 0] = A[i]; dst[i * 2 + 1] = A[i + 1];
        dst[i * 2 + 2] = B[i]; dst[i * 2 + 3] = B[i + 1];
    }
    free(A); free(B);
}

static u16 prot_ram_rd(u32 addr) {             /* hikaru.cpp prot_ram_r: 16-bit word, byte-swapped */
    u32 o = (addr & 0x7fff) * 2; u16 v = (u16)(g_m->protram[o] | (g_m->protram[o + 1] << 8));
    return (u16)((v >> 8) | (v << 8));
}

void hikaru_init(Machine *m, const char *romdir) {
    memset(m, 0, sizeof *m);
    g_m = m;
    if (rom_open(romdir)) exit(1);
    m->bios = xalloc(0x200000);
    loadfile(romdir, "epr-23400a.ic94", m->bios, 0x200000);
    m->user1 = xalloc(0x4000000);
    load32w(romdir, "epr-23174.ic29", "epr-23175.ic30", m->user1 + 0x0000000, 0x400000);
    load32w(romdir, "epr-23176.ic31", "epr-23177.ic32", m->user1 + 0x1000000, 0x400000);
    m->user2 = xalloc(0x10000000);
    for (int i = 0; i < 16; i++) {
        static const char *ic[32] = {
            "mpr-23086.ic37","mpr-23087.ic38","mpr-23088.ic39","mpr-23089.ic40","mpr-23090.ic41","mpr-23091.ic42",
            "mpr-23092.ic43","mpr-23093.ic44","mpr-23094.ic45","mpr-23095.ic46","mpr-23096.ic47","mpr-23097.ic48",
            "mpr-23098.ic49","mpr-23099.ic50","mpr-23100.ic51","mpr-23101.ic52","mpr-23102.ic53s","mpr-23103.ic54s",
            "mpr-23104.ic55s","mpr-23105.ic56s","mpr-23106.ic57s","mpr-23107.ic58s","mpr-23108.ic59s","mpr-23109.ic60s",
            "mpr-23110.ic61s","mpr-23111.ic62s","mpr-23112.ic63s","mpr-23113.ic64s","mpr-23114.ic65s","mpr-23115.ic66s",
            "mpr-23116.ic67s","mpr-23117.ic68s" };
        load32w(romdir, ic[i * 2], ic[i * 2 + 1], m->user2 + (size_t)i * 0x1000000, 0x800000);
    }
    m->ram[0] = xalloc(0x2000000); m->ram[1] = xalloc(0x2000000);
    m->shared = xalloc(0x400000);
    m->aica_ram[0] = xalloc(0x800000); m->aica_ram[1] = xalloc(0x800000);
    for (int i = 0; i < 2; i++) { m->aica[i] = calloc(1, sizeof(Aica)); aica_init(m->aica[i], i, m->aica_ram[i], 0x800000); m->aica_arm_reset[i] = 1; }
    memset(m->eep.data, 0xff, sizeof m->eep.data);
    memset(m->mie_eep.data, 0xff, sizeof m->mie_eep.data); m->mie_eep.dout = 1;
    x76_init(&m->x76);
    gpu_init();
    m->br[0].mode = 0; m->br[1].mode = 1;
    for (int i = 0; i < 2; i++) { m->cpu[i].id = i; m->cpu[i].machine = m; }
    memset(m->mie_port, 0xff, sizeof m->mie_port);
    mie_init(m);
    crypt5881_init(0x2903dad5, prot_ram_rd);
    for (int i = 0; i < 8; i++) g_input.analog[i] = 0x8000;
}

/* ------------------------------------------------------------------ */
/* 93C46 (8-bit org: 7-bit address, 8-bit data) */
void eep_update(Eeprom93 *e, int cs, int sk, int di) {
    if (!cs) { e->state = 0; e->bits = 0; e->shreg = 0; e->cs = 0; e->dout = 1; e->sk = sk; return; }
    if (!e->cs) { e->state = 0; e->bits = 0; e->shreg = 0; }
    e->cs = 1;
    if (sk && !e->sk) {                     /* rising edge */
        if (e->state == 0) {               /* wait for start bit */
            if (di) { e->state = 1; e->bits = 0; e->shreg = 0; }
        } else if (e->state == 1) {        /* opcode(2)+addr(7) */
            e->shreg = (e->shreg << 1) | di; e->bits++;
            if (e->bits == 9) {
                int opc = (e->shreg >> 7) & 3, a = e->shreg & 0x7f;
                e->addr = a;
                if (opc == 2) { e->state = 2; e->rdcnt = 0; e->dout = 0; }      /* READ: dummy 0 then data */
                else if (opc == 1) { e->state = 3; e->bits = 0; e->shreg = 0; } /* WRITE */
                else if (opc == 3) { if (e->wen) e->data[a] = 0xff; e->state = 9; e->dout = 1; }
                else {
                    int sub = (a >> 5) & 3;
                    if (sub == 3) e->wen = 1; else if (sub == 0) e->wen = 0;
                    else if (sub == 2 && e->wen) { memset(e->data, 0xff, 128); }
                    else if (sub == 1) { e->state = 4; e->bits = 0; e->shreg = 0; }
                    if (e->state == 1) e->state = 9;
                    e->dout = 1;
                }
            }
        } else if (e->state == 2) {        /* reading */
            int bit = 7 - (e->rdcnt & 7);
            e->dout = (e->data[e->addr] >> bit) & 1;
            e->rdcnt++;
            if ((e->rdcnt & 7) == 0) e->addr = (e->addr + 1) & 0x7f;
        } else if (e->state == 3 || e->state == 4) {
            e->shreg = (e->shreg << 1) | di; e->bits++;
            if (e->bits == 8) {
                if (e->wen) {
                    if (e->state == 3) e->data[e->addr] = (u8)e->shreg;
                    else { memset(e->data, (u8)e->shreg, 128); }
                }
                e->state = 9; e->dout = 1;
            }
        }
    }
    e->sk = sk; e->di = di;
}

/* ------------------------------------------------------------------ */
/* PCI memory space (behind the 315-6154 bridges) */
void mie_irq_update(Machine *m);
static u32 rd32le(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static void wr32m(u8 *p, u32 v, u32 mask) { u32 o; memcpy(&o, p, 4); o = (o & ~mask) | (v & mask); memcpy(p, &o, 4); }

/* AICA registers on the 32-bit local bus: one 16-bit register per 32-bit word (low half). 0x2c00 = ARM reset. */
/* AICA RTC (both chips): 0x710000 seconds since 1950 high 16, 0x710004 low 16, 0x710008 write enable.
 * The game probes AICA #2 by reading these three words: if they are all equal it assumes the second
 * AICA (music) is missing. Time = host clock at power-on + emulated time (deterministic within a run). */
static u32 aica_rtc_r(Machine *m, u32 a) {
    if (!m->rtc_base) m->rtc_base = (u32)time(NULL) + 189388800u;   /* 1970 -> 1964 epoch (measured in the BIOS CLOCK SETTING screen; 1950 showed 2040) */
    u32 t = m->rtc_base + (u32)(m->aica[0]->samples / 44100);
    switch (a & 0xc) { case 0: return t >> 16; case 4: return t & 0xffff; default: return 0; }
}
static u32 aica_reg_r(Machine *m, int i, u32 off) {
    off &= 0x7ffc;
    if (off == 0x2c00) return m->aica_arm_reset[i];
    return aica_reg_read(m->aica[i], off, 4);
}
static void aica_reg_w(Machine *m, int i, u32 off, u32 v, u32 mask) {
    if (m->log) fprintf(m->log, "AICA%d SH4 regw %04x=%08x mask %08x pc=%08x\n", i, off, v, mask, m->cpu[0].pc);
    off &= 0x7ffc;
    if (off == 0x2c00) {
        if (mask & 0xff) { m->aica_arm_reset[i] = v & 1; aica_set_arm_reset(m->aica[i], v & 1); if (m->log) fprintf(m->log, "AICA%d ARM %s\n", i, (v & 1) ? "reset" : "run"); }
        return;
    }
    if ((mask & 0xffff) == 0xffff) aica_reg_write(m->aica[i], off, v & 0xffff, 2);
    else { if (mask & 0xff) aica_reg_write(m->aica[i], off, v & 0xff, 1); if (mask & 0xff00) aica_reg_write(m->aica[i], off + 1, (v >> 8) & 0xff, 1); }
}

u32 pci_r32(Machine *m, u32 a, u32 mask) {
    a &= ~3u;
    if (gpu_handles(a)) return gpu_r32(m, a);
    if (a >= 0x00400000 && a < 0x00800000) return 0;
    if (a >= 0x00800000 && a < 0x00820000) {          /* MIE ports, umask16 0x00ff */
        u32 p = (a - 0x00800000) >> 1; u32 v = 0;
        for (int k = 0; k < 2; k++) {
            if (!(mask & (0xffu << (k * 16)))) continue;
            u32 port = (p + k) & 0xff, val = mie_port_r(m, (u8)port, 1);
            /* 8-bit port on a 16-bit lane: the undriven high byte reads as 1s. The BIOS input reader does
               mov.w (sign-extending) on port 0 and ANDs in ~(MIE c005 << 16) to get TEST; with 0s there the
               system menu never saw TEST and no option could be confirmed. */
            v |= (0xff00u | (val & 0xff)) << (k * 16);
        }
        return v;
    }
    if (a >= 0x00820000 && a < 0x00840000) {
        u32 p = (a - 0x00820000) >> 1;
        return m->mie_mem[p & 0xffff] | (m->mie_mem[(p + 1) & 0xffff] << 16);
    }
    if (a >= 0x00c00000 && a < 0x00c10000) return rd32le(&m->nvram[a & 0xffff]);
    if (a >= 0x02000000 && a < 0x03000000) return ((!m->mie_busrq) << 8) | 0x600;
    if (a >= 0x0a000000 && a < 0x0a010000) return rd32le(&m->protram[a & 0xffff]);
    if (a >= 0x0a010000 && a < 0x0a010010) {      /* 315-5881 iomap_le: 0 ready, e decrypt */
        u32 v = 0, o = a & 0xc;
        if (mask & 0x0000ffff) { if (o == 0x0) v |= crypt5881_ready_r(); }
        if (mask & 0xffff0000) { if (o == 0xc) v |= (u32)crypt5881_decrypt_le_r() << 16; }
        return v;
    }
    if (a >= 0x0c700000 && a < 0x0c708000) return aica_reg_r(m, 0, a - 0x0c700000);
    if (a >= 0x0c710000 && a < 0x0c710010) return aica_rtc_r(m, a);
    if (a >= 0x0c800000 && a < 0x0d000000) { if (dbg_env_aicawlog() && m->log) { static long n; if (n++ < 50000000) fprintf(m->log, "AR0 %06x %08x pc=%08x\n", a - 0x0c800000, rd32le(&m->aica_ram[0][a - 0x0c800000]), m->cpu[0].pc); } return rd32le(&m->aica_ram[0][a - 0x0c800000]); }
    if (a >= 0x0d700000 && a < 0x0d708000) return aica_reg_r(m, 1, a - 0x0d700000);
    if (a >= 0x0d710000 && a < 0x0d710010) return aica_rtc_r(m, a);
    if (a >= 0x0d800000 && a < 0x0e000000) return rd32le(&m->aica_ram[1][a - 0x0d800000]);
    if (a >= 0x10000000 && a < 0x14000000) return rd32le(&m->user1[a - 0x10000000]);
    if (a >= 0x14000000 && a < 0x15000000) return x76_read(&m->x76);
    if (a >= 0x18000000 && a < 0x20000000) return 0;
    if (a >= 0x20000000 && a < 0x30000000) return rd32le(&m->user2[a - 0x20000000]);
    if (a >= 0x40000000 && a < 0x42000000) return rd32le(&m->ram[1][a - 0x40000000]);
    if (a >= 0x48000000 && a < 0x48400000) return rd32le(&m->shared[a - 0x48000000]);
    if (a >= 0x70000000 && a < 0x72000000) return rd32le(&m->ram[0][a - 0x70000000]);
    LOG("PCI rd %08x (mask %08x) UNMAPPED\n", a, mask);
    return 0;
}

void pci_w32(Machine *m, u32 a, u32 v, u32 mask) {
    a &= ~3u;
    if (gpu_handles(a)) { gpu_w32(m, a, v, mask); return; }
    if (a >= 0x00400000 && a < 0x00800000) return;
    if (a >= 0x00800000 && a < 0x00820000) {
        u32 p = (a - 0x00800000) >> 1;
        for (int k = 0; k < 2; k++) if (mask & (0xffu << (k * 16))) mie_port_w(m, (u8)((p + k) & 0xff), (u8)(v >> (k * 16)), 1);
        return;
    }
    if (a >= 0x00820000 && a < 0x00840000) {
        u32 p = (a - 0x00820000) >> 1;
        for (int k = 0; k < 2; k++) if (mask & (0xffu << (k * 16))) {
            u32 q = (p + k) & 0xffff;
            if (q >= 0x8000 && q < 0x9000 && m->mie_mem[q] != (u8)(v >> (k * 16))) { static u64 n; if (!(n++ & 1023)) LOG("MIE code write #%llu @%04x (busrq=%d)\n", (unsigned long long)n, q, m->mie_busrq); }
            m->mie_mem[q] = (u8)(v >> (k * 16));
        }
        return;
    }
    if (a >= 0x00c00000 && a < 0x00c10000) { wr32m(&m->nvram[a & 0xffff], v, mask); return; }
    if (a >= 0x02000000 && a < 0x03000000) { mie_busrq_w(m, (v >> 8) & 1); return; }
    if (a >= 0x0a000000 && a < 0x0a010000) { wr32m(&m->protram[a & 0xffff], v, mask); return; }
    if (a >= 0x0a010000 && a < 0x0a010010) {      /* 8 addr lo, a addr hi, c subkey */
        u32 o = a & 0xc;
        LOG("5881 wr %08x <- %08x m%08x pc=%08x pr=%08x\n", a, v, mask, m->cpu[0].pc, m->cpu[0].pr);
        if (o == 0x8) { if (mask & 0xffff) crypt5881_addrlo_w((u16)v); if (mask & 0xffff0000) crypt5881_addrhi_w((u16)(v >> 16)); }
        if (o == 0xc && (mask & 0xffff)) crypt5881_subkey_le_w((u16)v);
        return;
    }
    if (a >= 0x0c700000 && a < 0x0c708000) { aica_reg_w(m, 0, a - 0x0c700000, v, mask); return; }
    if (a >= 0x0c710000 && a < 0x0c710010) return;
    if (a >= 0x0c800000 && a < 0x0d000000) { wr32m(&m->aica_ram[0][a - 0x0c800000], v, mask);
        if (dbg_env_aicawlog() && m->log) { static long n; if (n++ < 40000000) fprintf(m->log, "AW0 %06x %08x %08x\n", a - 0x0c800000, v, mask); } return; }
    if (a >= 0x0d700000 && a < 0x0d708000) { aica_reg_w(m, 1, a - 0x0d700000, v, mask); return; }
    if (a >= 0x0d710000 && a < 0x0d710010) return;
    if (a >= 0x0d800000 && a < 0x0e000000) { wr32m(&m->aica_ram[1][a - 0x0d800000], v, mask);
        if (dbg_env_aicawlog() && m->log) { static long n; if (n++ < 400000) fprintf(m->log, "AW1 %06x %08x %08x\n", a - 0x0d800000, v, mask); } return; }
    if (a >= 0x14000000 && a < 0x15000000) { x76_write(&m->x76, v); return; }
    if (a >= 0x40000000 && a < 0x42000000) { wr32m(&m->ram[1][a - 0x40000000], v, mask); blk_ram_written(1, a - 0x40000000, 4); return; }
    if (a >= 0x48000000 && a < 0x48400000) { wr32m(&m->shared[a - 0x48000000], v, mask); return; }
    if (a >= 0x70000000 && a < 0x72000000) { wr32m(&m->ram[0][a - 0x70000000], v, mask); blk_ram_written(0, a - 0x70000000, 4); return; }
    LOG("PCI wr %08x <- %08x (mask %08x) UNMAPPED\n", a, v, mask);
}

void mie_irq_update(Machine *m) {
    u32 mask = m->mie_port[2];
    if (m->mie_irq_pending & mask) m->irl_pins[0] |= 4; else m->irl_pins[0] &= ~4u;   /* IRL2 */
    cpu_check_irq(&m->cpu[0]);
}
void hikaru_set_ant_int(Machine *m, int line) {
    if (line) m->mie_irq_pending |= 1; else m->mie_irq_pending &= ~1u;
    mie_irq_update(m);
}

/* PCI configuration space: no devices modelled yet */
static u32 pcicfg_r(Machine *m, int br, u32 off) {
    LOG("PCICFG%d rd %06x\n", br, off);
    return 0xffffffff;
}
static void pcicfg_w(Machine *m, int br, u32 off, u32 v) {
    LOG("PCICFG%d wr %06x <- %08x\n", br, off, v);
}

/* ------------------------------------------------------------------ */
/* 315-6154 bridge */
static void parse_bases(u32 reg, u32 *b) {
    for (int i = 0; i < 4; i++) { b[i] = ((reg >> (i * 8)) & 0xff) << 24; if (b[i] < 0xc0000000) b[i] &= 0x7fffffff; }
}
static u32 bridge_r(Machine *m, int id, u32 off) {
    Bridge *b = &m->br[id];
    if (off == 0) return b->mode;
    return b->regs[(off >> 2) & 0x3f];
}
static void bridge_irq_update(Machine *m, int id) {   /* 315-6154 irq1 -> SH-4 IRL1, level */
    Bridge *b = &m->br[id];
    if ((b->regs[1] & 0xffff) & (b->regs[1] >> 16)) m->irl_pins[id] |= 2; else m->irl_pins[id] &= ~2u;
    cpu_check_irq(&m->cpu[id]);
}
static void bridge_w(Machine *m, int id, u32 off, u32 v, u32 mask) {
    Bridge *b = &m->br[id]; int r = (off >> 2) & 0x3f;
    if (r == 1) { b->regs[1] &= ~(v & mask & 0xffff); mask &= 0xffff0000; b->regs[1] = (b->regs[1] & ~mask) | (v & mask); bridge_irq_update(m, id); return; }
    b->regs[r] = (b->regs[r] & ~mask) | (v & mask);
    if (r == 4) parse_bases(b->regs[4], b->bases);
    else if (r == 5) parse_bases(b->regs[5], b->bases + 4);
    else if (r == 6) parse_bases(b->regs[6], b->bases + 8);
    else if (r == 7) { b->cfg14 = b->regs[7] & 1; b->cfg18 = (b->regs[7] >> 1) & 1; }
    else if (r == 14 && (mask & 0xff000000) && (b->regs[14] & (1 << 24))) {
        u32 s = b->regs[12], d = b->regs[13], l = b->regs[14] & 0xffffff;
        LOG("BR%d DMA %08x -> %08x len %x words (pc=%08x pr=%08x)\n", id, s, d, l, m->cpu[id].pc, m->cpu[id].pr);
        if (s < 0xc0000000) s &= 0x7fffffff;
        if (d < 0xc0000000) d &= 0x7fffffff;
        while (l--) { pci_w32(m, d, pci_r32(m, s, 0xffffffff), 0xffffffff); s += 4; d += 4; }
        b->regs[14] &= ~(1u << 24);
        b->regs[1] |= 1 << 12;
        /* irq1 to the owning CPU when masked in */
        bridge_irq_update(m, id);
    }
}
static u32 aperture_r(Machine *m, int id, int ap, u32 off, u32 mask) {
    Bridge *b = &m->br[id];
    u32 dst = (off >> 24) & 3, o = off & 0xffffff;
    if (ap == 0 && dst == 0 && b->cfg14) return pcicfg_r(m, id, o);
    if (ap == 1 && dst == 0 && b->cfg18) return pcicfg_r(m, id, o);
    return pci_r32(m, b->bases[ap * 4 + dst] + o, mask);
}
static void aperture_w(Machine *m, int id, int ap, u32 off, u32 v, u32 mask) {
    Bridge *b = &m->br[id];
    u32 dst = (off >> 24) & 3, o = off & 0xffffff;
    if (ap == 0 && dst == 0 && b->cfg14) { pcicfg_w(m, id, o, v); return; }
    if (ap == 1 && dst == 0 && b->cfg18) { pcicfg_w(m, id, o, v); return; }
    pci_w32(m, b->bases[ap * 4 + dst] + o, v, mask);
}

/* ------------------------------------------------------------------ */
/* SH-4 on-chip (P4) registers */
static u32 *oc(Cpu *c, u32 a) { return &c->onchip[((a >> 8) & 0xff00) | (a & 0xff)]; } /* coarse sparse map */

static u32 porta_read(Machine *m, int id) {
    if (id == 0) {
        u32 v = (m->eep.dout ? 1u : 0u) << 5;
        v |= 0xf << 6;   /* irq lines inactive (active low) */
        if (m->mie_irq_pending & 1) v &= ~(1u << 6);    /* ANT_INT */
        return v | (m->porta_out[0] & 0x1f);
    }
    return (0xf << 6) | (m->porta_out[1] & 0x3f);   /* DIPSW all off */
}
static void porta_write(Machine *m, int id, u32 v) {
    m->porta_out[id] = v;
    if (getenv("PORTALOG")) LOG("CPU%d PDTRA <- %04x pc=%08x\n", id, v, m->cpu[id].pc);
    /* master bit0 pattern 1110001111111 -> slave NMI (Stefano Teso's notes in MAME) */
    m->nmi_hist[id] = ((m->nmi_hist[id] << 1) | (v & 1)) & 0x1fff;
    if (m->nmi_hist[id] == 0x1c7f) { m->nmi_pending[id ^ 1] = 1; m->cpu[id ^ 1].sleeping = 0; LOG("CPU%d -> NMI to CPU%d\n", id, id ^ 1); }
    int other = id ^ 1;
    if (v & 1) m->irl_pins[other] &= ~8; else m->irl_pins[other] |= 8;   /* bit0 -> other CPU /IRL3 */
    cpu_check_irq(&m->cpu[other]);
    if (id == 0) eep_update(&m->eep, (v >> 4) & 1, (v >> 3) & 1, (v >> 2) & 1);
}

static u32 onchip_r(Cpu *c, u32 a, int sz) {
    Machine *m = c->machine;
    switch (a & 0xfffffffc) {
    case 0xff80002c: return m->porta_ctrl[c->id];
    case 0xff800030: return porta_read(m, c->id);
    case 0xffd80004: return c->tstr;
    case 0xffd80008: return c->tcor[0]; case 0xffd8000c: return c->tcnt[0]; case 0xffd80010: return c->tcr[0];
    case 0xffd80014: return c->tcor[1]; case 0xffd80018: return c->tcnt[1]; case 0xffd8001c: return c->tcr[1];
    case 0xffd80020: return c->tcor[2]; case 0xffd80024: return c->tcnt[2]; case 0xffd80028: return c->tcr[2];
    case 0xffa00000: case 0xffa00010: case 0xffa00020: case 0xffa00030: return c->sar[(a >> 4) & 3];
    case 0xffa00004: case 0xffa00014: case 0xffa00024: case 0xffa00034: return c->dar[(a >> 4) & 3];
    case 0xffa00008: case 0xffa00018: case 0xffa00028: case 0xffa00038: return c->dmatcr[(a >> 4) & 3];
    case 0xffa0000c: case 0xffa0001c: case 0xffa0002c: case 0xffa0003c: return c->chcr[(a >> 4) & 3];
    case 0xffa00040: return c->dmaor;
    case 0xffc00008: return *oc(c, a) & 0xff;          /* WTCNT */
    }
    u32 v = *oc(c, a);
    if (sz == 1) v = (v >> ((a & 3) * 8)) & 0xff;
    else if (sz == 2) v = (v >> ((a & 2) * 8)) & 0xffff;
    return v;
}

static void dmac_run(Cpu *c, int ch) {
    u32 chcr = c->chcr[ch];
    if (!(chcr & 1) || !(c->dmaor & 1)) return;
    int ts = (chcr >> 4) & 7; static const int tsz[8] = { 8, 1, 2, 4, 32, 0, 0, 0 };
    int unit = tsz[ts]; if (!unit) return;
    int sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;
    u32 s = c->sar[ch], d = c->dar[ch], n = c->dmatcr[ch] & 0xffffff;
    if (((chcr >> 8) & 0xf) != 4) { LOG("CPU%d DMAC ch%d non-auto request chcr=%08x\n", c->id, ch, chcr); }
    LOG("CPU%d DMAC ch%d %08x->%08x n=%x unit=%d\n", c->id, ch, s, d, n, unit);
    while (n--) {
        for (int i = 0; i < unit; i += 4) bus_w32(c, d + i, bus_r32(c, s + i));
        if (sm == 1) s += unit; else if (sm == 2) s -= unit;
        if (dm == 1) d += unit; else if (dm == 2) d -= unit;
    }
    c->sar[ch] = s; c->dar[ch] = d; c->dmatcr[ch] = 0;
    c->chcr[ch] |= 2;               /* TE */
    if (chcr & 4) { c->intevt_pending |= 1u << (8 + ch); cpu_check_irq(c); }
}

static void onchip_w(Cpu *c, u32 a, u32 v, int sz) {
    Machine *m = c->machine;
    switch (a & 0xfffffffc) {
    case 0xff80002c: m->porta_ctrl[c->id] = v; return;
    case 0xff800030: porta_write(m, c->id, v); return;
    case 0xffd80004: c->tstr = (u8)v; return;
    case 0xffd80008: c->tcor[0] = v; return; case 0xffd8000c: c->tcnt[0] = v; return; case 0xffd80010: c->tcr[0] = v & 0x3ff; return;
    case 0xffd80014: c->tcor[1] = v; return; case 0xffd80018: c->tcnt[1] = v; return; case 0xffd8001c: c->tcr[1] = v & 0x3ff; return;
    case 0xffd80020: c->tcor[2] = v; return; case 0xffd80024: c->tcnt[2] = v; return; case 0xffd80028: c->tcr[2] = v & 0x3ff; return;
    case 0xffa00000: case 0xffa00010: case 0xffa00020: case 0xffa00030: c->sar[(a >> 4) & 3] = v; return;
    case 0xffa00004: case 0xffa00014: case 0xffa00024: case 0xffa00034: c->dar[(a >> 4) & 3] = v; return;
    case 0xffa00008: case 0xffa00018: case 0xffa00028: case 0xffa00038: c->dmatcr[(a >> 4) & 3] = v; return;
    case 0xffa0000c: case 0xffa0001c: case 0xffa0002c: case 0xffa0003c: c->chcr[(a >> 4) & 3] = v; dmac_run(c, (a >> 4) & 3); return;
    case 0xffa00040: c->dmaor = v; for (int i = 0; i < 4; i++) dmac_run(c, i); return;
    }
    u32 *p = oc(c, a);
    if (sz == 1) { int s = (a & 3) * 8; *p = (*p & ~(0xffu << s)) | ((v & 0xff) << s); }
    else if (sz == 2) { int s = (a & 2) * 8; *p = (*p & ~(0xffffu << s)) | ((v & 0xffff) << s); }
    else *p = v;
}

/* TMU: approximate Pphi = CPU/4, one CPU cycle per instruction */
void onchip_tick(Cpu *c, int cycles) {
    static const int div[8] = { 4, 16, 64, 256, 1024, 1024, 1024, 1024 };
    for (int ch = 0; ch < 3; ch++) {
        if (!(c->tstr & (1 << ch))) continue;
        static u32 acc[2][3];
        int d = div[c->tcr[ch] & 7] * 4;
        acc[c->id][ch] += cycles;
        if (acc[c->id][ch] < (u32)d) continue;
        u64 ticks = acc[c->id][ch] / (u32)d;            /* closed form: idle skipping can advance millions of cycles */
        acc[c->id][ch] -= (u32)(ticks * (u32)d);
        u64 cnt = c->tcnt[ch];
        if (ticks <= cnt) c->tcnt[ch] = (u32)(cnt - ticks);
        else {                                           /* one or more underflows: reload from TCOR (period tcor+1) */
            u64 rest = ticks - cnt - 1, period = (u64)c->tcor[ch] + 1;
            c->tcnt[ch] = (u32)(c->tcor[ch] - (rest % period));
            c->tcr[ch] |= 0x100;     /* UNF */
            if (c->tcr[ch] & 0x20) { c->intevt_pending |= 1u << ch; }
        }
    }
    if (c->intevt_pending) cpu_check_irq(c);
}

/* interrupt priority evaluation */
static int ipr_level(Cpu *c, int src) {
    u32 ipra = *oc(c, 0xffd00004) & 0xffff, iprc = *oc(c, 0xffd0000c) & 0xffff;
    switch (src) {
    case 0: return (ipra >> 12) & 15;
    case 1: return (ipra >> 8) & 15;
    case 2: return (ipra >> 4) & 15;
    default: return (iprc >> 8) & 15;  /* DMAC */
    }
}
void cpu_check_irq(Cpu *c) {
    Machine *m = c->machine;
    int best = -1; u32 evt = 0; int bsrc = -1;
    u32 pins = m->irl_pins[c->id];
    static const int irl_lvl[4] = { 13, 10, 7, 4 };
    static const u32 irl_evt[4] = { 0x240, 0x2a0, 0x300, 0x360 };
    for (int i = 0; i < 4; i++) if (pins & (1 << i)) { if (irl_lvl[i] > best) { best = irl_lvl[i]; evt = irl_evt[i]; bsrc = 100 + i; } }
    for (int i = 0; i < 3; i++) if (c->intevt_pending & (1u << i)) { int l = ipr_level(c, i); if (l > best) { best = l; evt = 0x400 + i * 0x20; bsrc = i; } }
    for (int i = 0; i < 4; i++) if (c->intevt_pending & (1u << (8 + i))) { int l = ipr_level(c, 3); if (l > best) { best = l; evt = 0x640 + i * 0x20; bsrc = 8 + i; } }
    c->intevt_level = best > 0 ? best : 0;
    c->irl = evt;
    if (best > 0 && c->sleeping && (best > (int)((c->sr >> 4) & 15))) c->sleeping = 0;
    (void)bsrc;
}

/* called between instructions */
u32 g_irqcnt[2][64];
int sh4_take_irq(Cpu *c) {
    Machine *m = c->machine;
    if (m->nmi_pending[c->id] && !(c->sr & SR_BL)) {
        m->nmi_pending[c->id] = 0;
        LOG("CPU%d takes NMI at pc=%08x sr=%08x\n", c->id, c->pc, c->sr);
        *oc(c, 0xff000028) = 0x1c0;
        sh4_exception(c, 0x1c0, 0x600, c->pc);
        return 1;
    }
    if (!c->intevt_level || (c->sr & SR_BL)) return 0;
    if ((int)c->intevt_level <= (int)((c->sr >> 4) & 15)) return 0;
    u32 evt = c->irl;
    /* internal sources are edge-latched: clear on acceptance */
    if (evt >= 0x400 && evt < 0x460) c->intevt_pending &= ~(1u << ((evt - 0x400) / 0x20));
    if (evt >= 0x640 && evt < 0x6c0) c->intevt_pending &= ~(1u << (8 + (evt - 0x640) / 0x20));
    *oc(c, 0xff000028) = evt;
    { int k = (evt >> 5) & 63; u32 n = ++g_irqcnt[c->id][k]; if ((n & (n - 1)) == 0) LOG("CPU%d IRQ %03x #%u pc=%08x\n", c->id, evt, n, c->pc); }
    sh4_exception(c, evt, 0x600, c->pc);
    c->sleeping = 0;
    cpu_check_irq(c);
    return 1;
}

void sh4_on_sr_change(Cpu *c) { (void)c; }
void sh4_on_sleep(Cpu *c) { c->sleeping = 1; }
void sh4_trap(Cpu *c, u32 imm, u32 pc) {
    LOG("CPU%d TRAPA #%02x at %08x pr=%08x r4=%08x r5=%08x\n", c->id, imm, pc, c->pr, c->r[4], c->r[5]);
    *oc(c, 0xff000020) = imm << 2; *oc(c, 0xff000024) = 0x160;
    sh4_exception(c, 0x160, 0x100, pc + 2);
}
void sh4_illegal(Cpu *c, u32 pc, u16 op) {
    LOG("CPU%d ILLEGAL %04x at %08x\n", c->id, op, pc);
    fprintf(stderr, "CPU%d ILLEGAL %04x at %08x\n", c->id, op, pc);
    *oc(c, 0xff000024) = 0x180;
    sh4_exception(c, 0x180, 0x100, pc);
}

void sh4_pref(Cpu *c, u32 a) {
    if ((a & 0xfc000000) != 0xe0000000) return;
    int q = (a >> 5) & 1;
    u32 qacr = *oc(c, q ? 0xff00003c : 0xff000038);
    u32 ext = ((qacr & 0x1c) << 24) | (a & 0x03ffffe0);
    for (int i = 0; i < 8; i++) bus_w32(c, ext + i * 4, c->sq[q][i]);
}

/* ------------------------------------------------------------------ */
/* CPU bus: returns 32-bit with mask semantics for devices */
static u32 dev_r32(Cpu *c, u32 a, u32 mask, int sz) {
    Machine *m = c->machine;
    if (a >= 0xe0000000) {
        if (a >= 0xff000000) return onchip_r(c, a, sz);
        if (a < 0xe4000000) return c->sq[(a >> 5) & 1][(a >> 2) & 7];
        LOG("CPU%d rd P4 %08x\n", c->id, a); return 0;
    }
    if (a >= 0x7c000000 && a < 0x80000000) return rd32le(&c->ocram[a & 0x3ffc]);
    u32 p = a & 0x1fffffff;
    if (p < 0x00200000) return rd32le(&m->bios[p & ~3u]);
    if (p < 0x04000000) return aperture_r(m, c->id, 2, p & 0x3fffffc, mask);
    if (p < 0x04000100) return bridge_r(m, c->id, p & 0xfc);
    if (p >= 0x0c000000 && p < 0x0e000000) return rd32le(&m->ram[c->id][p & 0x1fffffc]);
    if (c->id == 1 && p >= 0x10000000 && p < 0x10400000) return rd32le(&m->shared[p & 0x3ffffc]);
    if (p >= 0x14000000 && p < 0x18000000) return aperture_r(m, c->id, 0, p & 0x3fffffc, mask);
    if (p >= 0x18000000 && p < 0x1c000000) return aperture_r(m, c->id, 1, p & 0x3fffffc, mask);
    LOG("CPU%d rd UNMAPPED %08x pc=%08x\n", c->id, a, c->pc);
    return 0;
}
static void dev_w32(Cpu *c, u32 a, u32 v, u32 mask, int sz) {
    Machine *m = c->machine;
    if (a >= 0xe0000000) {
        if (a >= 0xff000000) { onchip_w(c, a, v, sz); return; }
        if (a < 0xe4000000) { u32 *q = &c->sq[(a >> 5) & 1][(a >> 2) & 7]; *q = (*q & ~mask) | (v & mask); return; }
        LOG("CPU%d wr P4 %08x <- %08x\n", c->id, a, v); return;
    }
    if (a >= 0x7c000000 && a < 0x80000000) { wr32m(&c->ocram[a & 0x3ffc], v, mask); return; }
    u32 p = a & 0x1fffffff;
    if (p < 0x00200000) { LOG("CPU%d wr BIOS %08x <- %08x\n", c->id, a, v); return; }
    if (p < 0x04000000) { aperture_w(m, c->id, 2, p & 0x3fffffc, v, mask); return; }
    if (p < 0x04000100) { bridge_w(m, c->id, p & 0xfc, v, mask); return; }
    if (p >= 0x0c000000 && p < 0x0e000000) { wr32m(&m->ram[c->id][p & 0x1fffffc], v, mask); blk_ram_written(c->id, p & 0x1fffffc, 4); return; }
    if (c->id == 1 && p >= 0x10000000 && p < 0x10400000) { wr32m(&m->shared[p & 0x3ffffc], v, mask); return; }
    if (p >= 0x14000000 && p < 0x18000000) { aperture_w(m, c->id, 0, p & 0x3fffffc, v, mask); return; }
    if (p >= 0x18000000 && p < 0x1c000000) { aperture_w(m, c->id, 1, p & 0x3fffffc, v, mask); return; }
    LOG("CPU%d wr UNMAPPED %08x <- %08x pc=%08x\n", c->id, a, v, c->pc);
}

/* fast paths for RAM */

#define RAMPTR(c, a) ({ u32 _p = (a) & 0x1fffffff; \
    ((a) < 0xe0000000 && _p >= 0x0c000000 && _p < 0x0e000000) ? &((Machine*)(c)->machine)->ram[(c)->id][_p & 0x1ffffff] : \
    ((a) < 0xe0000000 && _p < 0x00200000) ? &((Machine*)(c)->machine)->bios[_p] : (u8*)0; })

u32 g_watch = 0;   /* physical address (29-bit) to watch for writes, 0 = off */
u32 g_watchval = 0; /* value to watch for in 32-bit writes, 0 = off */
#define WATCH(c, a, v, n) do { if ((g_watch && (((a) & 0x1ffffffc) == (g_watch & 0x1ffffffc))) || (g_watchval && n == 32 && (u32)(v) == g_watchval)) \
    LOG("WATCH CPU%d wr%d %08x <- %08x pc=%08x pr=%08x\n", (c)->id, n, a, (u32)(v), (c)->pc, (c)->pr); } while (0)
static u8 bus_r8_impl(Cpu *c, u32 a) {
    u8 *p = RAMPTR(c, a); if (p) return *p;
    if (a >= 0xff000000) return (u8)dev_r32(c, a, 0xff, 1);
    return (u8)(dev_r32(c, a & ~3u, 0xffu << ((a & 3) * 8), 1) >> ((a & 3) * 8));
}
static u16 bus_r16_impl(Cpu *c, u32 a) {
    u8 *p = RAMPTR(c, a); if (p) { u16 v; memcpy(&v, p, 2); return v; }
    if (a >= 0xff000000) return (u16)dev_r32(c, a, 0xffff, 2);
    return (u16)(dev_r32(c, a & ~3u, 0xffffu << ((a & 2) * 8), 2) >> ((a & 2) * 8));
}
static u32 bus_r32_impl(Cpu *c, u32 a) {
    u8 *p = RAMPTR(c, a); if (p) { u32 v; memcpy(&v, p, 4); return v; }
    return dev_r32(c, a, 0xffffffff, 4);
}
static void bus_w8_impl(Cpu *c, u32 a, u8 v) {
    WATCH(c, a, v, 8);
    u8 *p = RAMPTR(c, a); if (p && (a & 0x1fffffff) >= 0x00200000) { *p = v; blk_ram_written(c->id, a & 0x1ffffff, 1); return; }
    if (a >= 0xff000000) { dev_w32(c, a, v, 0xff, 1); return; }
    dev_w32(c, a & ~3u, (u32)v << ((a & 3) * 8), 0xffu << ((a & 3) * 8), 1);
}
static void bus_w16_impl(Cpu *c, u32 a, u16 v) {
    WATCH(c, a, v, 16);
    u8 *p = RAMPTR(c, a); if (p && (a & 0x1fffffff) >= 0x00200000) { memcpy(p, &v, 2); blk_ram_written(c->id, a & 0x1ffffff, 2); return; }
    if (a >= 0xff000000) { dev_w32(c, a, v, 0xffff, 2); return; }
    dev_w32(c, a & ~3u, (u32)v << ((a & 2) * 8), 0xffffu << ((a & 2) * 8), 2);
}
static void bus_w32_impl(Cpu *c, u32 a, u32 v) {
    WATCH(c, a, v, 32);
    u8 *p = RAMPTR(c, a); if (p && (a & 0x1fffffff) >= 0x00200000) { memcpy(p, &v, 4); blk_ram_written(c->id, a & 0x1ffffff, 4); return; }
    dev_w32(c, a, v, 0xffffffff, 4);
}
u16 sh4_fetch(Cpu *c, u32 pc) { return bus_r16_impl(c, pc); }       /* instruction fetch: never journaled */
u16 sh4_fetch_raw(Cpu *c, u32 pc) { return bus_r16_impl(c, pc); }

/* public accessors: memory-access journal for lockstep verification (nested accesses, e.g. DMA
   started by a register write, are not journaled: replay skips the outer write entirely) */
static int jdepth;
#define JREAD(T, sz, call) \
    if (g_jmode == 2 && !jdepth) return (T)jr_replay_read(a, sz); \
    if (g_jmode == 1 && !jdepth) { jdepth++; T v_ = call; jdepth--; jr_record(a, sz, v_, 0); return v_; } \
    return call;
#define JWRITE(sz, call) \
    if (g_jmode == 2 && !jdepth) { jr_replay_write(a, sz, v); if ((a >> 26) == (0xe0000000u >> 26)) { call; } return; } /* store queues = CPU state */ \
    if (g_jmode == 1 && !jdepth) { jdepth++; call; jdepth--; jr_record(a, sz, v, 1); return; } \
    call;
u8 bus_r8(Cpu *c, u32 a) { JREAD(u8, 1, bus_r8_impl(c, a)) }
u16 bus_r16(Cpu *c, u32 a) { JREAD(u16, 2, bus_r16_impl(c, a)) }
u32 bus_r32(Cpu *c, u32 a) { JREAD(u32, 4, bus_r32_impl(c, a)) }
void bus_w8(Cpu *c, u32 a, u8 v) { JWRITE(1, bus_w8_impl(c, a, v)) }
void bus_w16(Cpu *c, u32 a, u16 v) { JWRITE(2, bus_w16_impl(c, a, v)) }
void bus_w32(Cpu *c, u32 a, u32 v) { JWRITE(4, bus_w32_impl(c, a, v)) }

/* advance both AICAs by n samples (44.1 kHz); SZK interrupt = either AICA's SH-4 interrupt */
void hikaru_audio_run(Machine *m, int n, int32_t *mix) {
    aica_run(m->aica[0], n, mix);
    aica_run(m->aica[1], n, mix);
    int line = m->aica[0]->sh4_irq || m->aica[1]->sh4_irq;
    int old = (m->mie_irq_pending >> 4) & 1;
    if (line != old) { if (line) m->mie_irq_pending |= 0x10; else m->mie_irq_pending &= ~0x10u; mie_irq_update(m); }
}
