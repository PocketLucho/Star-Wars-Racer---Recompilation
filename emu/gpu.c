/* Hikaru GPU register stubs: Antarctic (3D command processor, PCI 0x00000000..)
 * and Eurasia (2D/display, PCI 0xF2000000..), plus frame timing.
 * Every access is counted so we can learn the register map from the game. */
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "hikaru.h"
#include "gpu.h"
#include "hikaru_cp.h"
#include "swrender.h"
#include "hikaru_gfx.h"
#include "gfx_out.h"

Gpu g_gpu;
GfxOutFn g_gfx_out;

typedef struct { u32 addr; u32 rd, wr; u32 last; } Acc;
static Acc acc[4096]; static int nacc;
static void count(u32 a, int w, u32 v) {
    for (int i = 0; i < nacc; i++) if (acc[i].addr == a) { if (w) { acc[i].wr++; acc[i].last = v; } else acc[i].rd++; return; }
    if (nacc < 4096) { acc[nacc].addr = a; acc[nacc].rd = !w; acc[nacc].wr = w; acc[nacc].last = v; nacc++; }
}
static int cmpacc(const void *a, const void *b) { u32 x = ((Acc*)a)->addr, y = ((Acc*)b)->addr; return x < y ? -1 : x > y; }
void gpu_dump_stats(FILE *f) {
    fprintf(f, "IDMA: entries=%llu bytes=%llu\n", (unsigned long long)g_gpu.idma_entries, (unsigned long long)g_gpu.idma_bytes);
    fprintf(f, "CP: steps=%llu triangles=%llu\n", (unsigned long long)g_gpu.cp_steps, (unsigned long long)g_gpu.cp_tris);
    qsort(acc, nacc, sizeof acc[0], cmpacc);
    for (int i = 0; i < nacc; i++) fprintf(f, "GPUREG %08x rd=%u wr=%u last=%08x\n", acc[i].addr, acc[i].rd, acc[i].wr, acc[i].last);
}

void gpu_init(void) {
    memset(&g_gpu, 0, sizeof g_gpu);
    g_gpu.ant_space = calloc(1, 0x1000000);
    g_gpu.eur_vram = calloc(1, 0x800000);
    g_gpu.c0 = calloc(1, 0x2000000);
    g_gpu.texram[0] = calloc(1, 0x800000); g_gpu.texram[1] = g_gpu.texram[0] + 0x400000;   /* contiguous: renderer indexes both banks */
}

static void update_irq(Machine *m) {
    u32 *ant = (u32 *)g_gpu.ant_space;
    if (g_gpu.eur[0x18 / 4]) ant[0x88 / 4] |= 0x80; else ant[0x88 / 4] &= ~0x80u;
    int line = (ant[0x88 / 4] & 0xff) != 0;
    g_gpu.ant_int = line;
    hikaru_set_ant_int(m, line);
}

int gpu_handles(u32 a) {
    return a < 0x00400000 || (a >= 0xf2000000 && a < 0xf2100000) || (a >= 0xf3000000 && a < 0xf3800000) ||
           (a >= 0xc0000000 && a < 0xc2000000);
}

/* GPU indirect DMA: table of 16-byte entries {src bus addr, size, (fmt|logh|logw|sloty|slotx), bank}
 * copies texel data (with its mipmap chain) into TEXRAM. Done synchronously when started. */
static void idma_run(Machine *m) {
    u32 *ant = (u32 *)g_gpu.ant_space;
    while ((ant[0x14 / 4] & 1) && ant[0x10 / 4]) {
        u32 t = ant[0x0c / 4];
        if ((t >> 24) != 0x48) break;
        u32 *e = (u32 *)(m->shared + (t & 0x3ffff0));
        if (e[1]) {
            u32 src = e[0], size = e[1], bank = e[3] & 1;
            u32 slotx = e[2] & 0xff, sloty = (e[2] >> 8) & 0xff, lw = 16u << ((e[2] >> 16) & 7), lh = 16u << ((e[2] >> 19) & 7);
            const u8 *sb = 0; u32 smask = 0;
            if ((src >> 24) == 0x48) { sb = m->shared; smask = 0x3fffff; }
            else if ((src >> 25) == (0x40000000u >> 25)) { sb = m->ram[1]; smask = 0x1ffffff; }
            if (sb && slotx >= 0x80 && sloty >= 0xc0) {
                u32 x0 = (slotx - 0x80) * 16, y0 = (sloty - 0xc0) * 16, done = 0, so = src & smask;
                while (done < size && lw && lh && x0 + lw <= 2048 && y0 + lh <= 1024) {
                    u8 *tr = g_gpu.texram[bank];
                    for (u32 y = 0; y < lh; y++) for (u32 x = 0; x < lw; x++) {
                        u32 d = ((y0 + y) * 4096 + (x0 + x) * 2) ^ 2;
                        u32 o = (so + done + (y * lw + x) * 2) & smask;
                        tr[d] = sb[o]; tr[d + 1] = sb[o + 1];
                    }
                    done += lw * lh * 2;
                    x0 += (2048 - x0) / 2; y0 += (1024 - y0) / 2; bank ^= 1; lw >>= 1; lh >>= 1;
                }
                g_gpu.idma_bytes += done; g_gpu.texram_dirty = 1;
            }
            g_gpu.idma_entries++;
        }
        ant[0x0c / 4] += 0x10;
        ant[0x10 / 4]--;
    }
    ant[0x14 / 4] &= ~1u;
    ant[0x88 / 4] |= 1;            /* GPU 15 IRQ bit 0: IDMA done */
    update_irq(m);
}

u32 gpu_r32(Machine *m, u32 a) {
    if (a < 0x00400000) {
        count(a < 0x100 ? a : (a & 0xfffff000), 0, 0);
        return *(u32 *)(g_gpu.ant_space + a);
    }
    if (a >= 0xf2000000 && a < 0xf2100000) {
        count(a, 0, 0);
        u32 o = a - 0xf2000000;
        if (o < 0x1000) return g_gpu.eur[o / 4];
        return 0;
    }
    if (a >= 0xf3000000 && a < 0xf3800000) return *(u32 *)(g_gpu.eur_vram + (a - 0xf3000000));
    if (a >= 0xc0000000 && a < 0xc2000000) { count(a & 0xffff0000, 0, 0); return *(u32 *)(g_gpu.c0 + (a - 0xc0000000)); }
    return 0;
}

void gpu_w32(Machine *m, u32 a, u32 v, u32 mask) {
    if (a < 0x00400000) {
        count(a < 0x100 ? a : (a & 0xfffff000), 1, v);
        { static int lg = -1; if (lg < 0) lg = getenv("GPULOG") ? atoi(getenv("GPULOG")) : 0;
          if (lg && a < 0x100 && (long)g_gpu.frame >= lg && m->log) fprintf(m->log, "ANT wr %02x <- %08x frame=%llu pc=%08x\n", a, v, (unsigned long long)g_gpu.frame, m->cpu[0].pc); }
        u32 *p = (u32 *)(g_gpu.ant_space + a);
        if (a == 0x88) { *p &= (v | ~mask); update_irq(m); return; }   /* write 0 to clear */
        if (a == 0x14) { *p = (*p & ~mask) | (v & mask); if (*p & 1) idma_run(m); return; }
        if (a == 0x58 && (v & 3) == 3) {   /* CP start: run the command list now (state persists across frames) */
            static CpState *cp; static long every = -1;
            if (!cp) { cp = calloc(1, sizeof *cp); cp_init(cp); }
            if (every < 0) every = getenv("FRAMEDUMP") ? atol(getenv("FRAMEDUMP")) : 0;
            cp->cmdram = m->shared; cp->slave_ram = m->ram[1];
            {   static long lf = -2; if (lf == -2) lf = getenv("CPLOG") ? atol(getenv("CPLOG")) : -1;
                cp->log = (lf >= 0 && (long)g_gpu.frame == lf) ? fopen("frames/cplog.txt", "w") : 0; }
            cp_frame(cp, 0x48000100, 2000000);
            if (cp->log) { fprintf(cp->log, "ntris=%d\n", cp->ntris); fclose(cp->log); cp->log = 0; }
            g_gpu.cp_tris += cp->ntris; g_gpu.cp_steps += cp->steps;
            {   static long tf = -2; if (tf == -2) tf = getenv("TRIDUMP") ? atol(getenv("TRIDUMP")) : -1;
                if (tf >= 0 && (long)g_gpu.frame == tf) {
                    FILE *f = fopen("frames/tris.txt", "w");
                    for (int i = 0; f && i < cp->ntris; i++) { const CpTri *t = &cp->tris[i]; float zmin = 1e30f, zmax = -1e30f, sx0 = 1e9f, sx1 = -1e9f, sy0 = 1e9f, sy1 = -1e9f;
                        for (int k = 0; k < 3; k++) { float w = -t->v[k].pos[2]; if (w < zmin) zmin = w; if (w > zmax) zmax = w;
                            float sx = t->view.off_x + t->v[k].pos[0] * t->view.f / (w ? w : 1e-6f), sy = t->view.off_y - t->v[k].pos[1] * t->view.f / (w ? w : 1e-6f);
                            if (sx < sx0) sx0 = sx; if (sx > sx1) sx1 = sx; if (sy < sy0) sy0 = sy; if (sy > sy1) sy1 = sy; }
                        fprintf(f, "%5d w=%9.1f..%9.1f scr=(%6.0f,%6.0f)-(%6.0f,%6.0f) dif=%02x%02x%02x amb=%02x%02x%02x f881=%08x tex=fmt%d %dx%d slot=%02x,%02x b%d pt=%x a=%.2f info=%08x vp=%d pc=%08x vamb=%x,%x,%x ls=%d mask=%x nl=%d fog=%d/%g uv=%.1f,%.1f;%.1f,%.1f;%.1f,%.1f w0c1=%08x w2c1=%08x df=%d dmin=%g dmax=%g blend=%d atest=%d\n", i, zmin, zmax, sx0, sy0, sx1, sy1,
                            t->mat.diffuse[0], t->mat.diffuse[1], t->mat.diffuse[2], t->mat.ambient[0], t->mat.ambient[1], t->mat.ambient[2], t->mat.flags881,
                            t->tex.fmt, 16 << t->tex.logw, 16 << t->tex.logh, t->tex.slotx, t->tex.sloty, t->tex.bank, t->polytype, t->polyalpha, t->info, t->vp, t->src_pc, t->view.ambient[0], t->view.ambient[1], t->view.ambient[2], t->ls, t->ls >= 0 ? cp->ls_frame[t->ls].mask : -1, cp->nls, t->view.fog_on, t->view.fog_density, t->v[0].uv[0], t->v[0].uv[1], t->v[1].uv[0], t->v[1].uv[1], t->v[2].uv[0], t->v[2].uv[1], t->tex.w0c1, t->tex.w2c1, t->view.dfunc, t->view.dmin, t->view.dmax, t->mat.blend, t->mat.alpha_test); }
                    if (f) fclose(f);
                } }
            gfx_out_frame(cp, g_gpu.texram[0], &g_gpu.texram_dirty, g_gpu.frame);
#ifndef _WIN32
            {   /* GFXBENCH=from,to : time gfx_build alone (no renderer) */
                static long b0 = -2, b1; if (b0 == -2) { b0 = -1; if (getenv("GFXBENCH")) sscanf(getenv("GFXBENCH"), "%ld,%ld", &b0, &b1); }
                if (b0 >= 0 && (long)g_gpu.frame >= b0 && (long)g_gpu.frame < b1) {
                    static GfxFrame bf; static double acc; static long n; struct timespec t0, t1;
                    clock_gettime(CLOCK_MONOTONIC, &t0); gfx_build(&bf, cp); clock_gettime(CLOCK_MONOTONIC, &t1);
                    acc += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6; n++;
                    if ((long)g_gpu.frame == b1 - 1) fprintf(stderr, "gfx_build bench: %.2f ms avg over %ld frames (%d verts)\n", acc / n, n, bf.nv);
                } }
#endif
            static long from = -1; if (from < 0) from = getenv("FRAMEDUMP_FROM") ? atol(getenv("FRAMEDUMP_FROM")) : 0;
            if (every > 0 && (long)g_gpu.frame >= from && g_gpu.frame % every == 0) {
                char fn[64]; snprintf(fn, sizeof fn, "frames/f_%05llu.png", (unsigned long long)g_gpu.frame);
                sr_texram = g_gpu.texram[0]; sr_render(cp);
                { static u16 *lay; if (!lay) lay = malloc(640 * 960 * 2); int ena = gpu_layers(lay); sr_overlay(lay, ena, gpu_layers_direct()); }
                sr_png(fn);
                if (getenv("TEXDUMP")) { snprintf(fn, sizeof fn, "frames/t_%05llu.bin", (unsigned long long)g_gpu.frame);
                    FILE *tf = fopen(fn, "wb"); if (tf) { fwrite(g_gpu.texram[0], 1, 0x800000, tf); fclose(tf); } }
            }
        }
        if (a == 0x58) {   /* GPU kick: snapshot command RAM (PCI 0x48000000, 4 MB) for offline decoding */
            static u32 kicks; kicks++;
            const char *every = getenv("CMDDUMP");
            if (every && (kicks % (u32)atoi(every)) == 0) {
                char fn[64]; snprintf(fn, sizeof fn, "cmd/kick_%05u.bin", kicks);
                FILE *f = fopen(fn, "wb"); if (f) { fwrite(m->shared, 1, 0x400000, f); fclose(f); }
                snprintf(fn, sizeof fn, "cmd/tex_%05u.bin", kicks);
                f = fopen(fn, "wb"); if (f) { fwrite(g_gpu.texram[0], 1, 0x400000, f); fwrite(g_gpu.texram[1], 1, 0x400000, f); fclose(f); }
                if (getenv("CMDDUMP_SLAVE")) {      /* the CP also runs code from slave RAM (PCI 0x40000000) */
                    snprintf(fn, sizeof fn, "cmd/slave_%05u.bin", kicks);
                    f = fopen(fn, "wb"); if (f) { fwrite(m->ram[1], 1, 0x2000000, f); fclose(f); }
                }
                if (m->log) fprintf(m->log, "GPU kick #%u (v=%08x) frame=%llu -> %s\n", kicks, v, (unsigned long long)g_gpu.frame, fn);
            }
        }
        *p = (*p & ~mask) | (v & mask);
        return;
    }
    if (a >= 0xf2000000 && a < 0xf2100000) {
        count(a, 1, v);
        u32 o = a - 0xf2000000;
        if (o >= 0x08 && o <= 0x14) {   /* per-source ack: write 1 to 08+4k clears status 18 bit k */
            if (v & 1) g_gpu.eur[0x18 / 4] &= ~(1u << ((o - 8) / 4));
            update_irq(m); return; }
        if (o < 0x1000) g_gpu.eur[o / 4] = (g_gpu.eur[o / 4] & ~mask) | (v & mask);
        if (o >= 0x40000 && o < 0x40010) {
            g_gpu.blt[(o - 0x40000) / 4] = v;
            if (o == 0x4000c && (v & 1)) {   /* GPU DMA: rect copy inside the 2048x2048x16bpp sheet (coords x:11 bits, y above) */
                u32 sx = g_gpu.blt[0] & 0x7ff, sy = g_gpu.blt[0] >> 11, dx = g_gpu.blt[1] & 0x7ff, dy = g_gpu.blt[1] >> 11;
                u32 w = g_gpu.blt[2] & 0xffff, h = g_gpu.blt[2] >> 16;
                u16 *vr = (u16 *)g_gpu.eur_vram;
                /* pixel x of the sheet lives in halfword x^1 (CPU writes 32-bit words, first pixel in the high half) */
                for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
                    u32 s2 = ((sy + y) & 2047) * 2048 + (((sx + x) & 2047) ^ 1), d2 = ((dy + y) & 2047) * 2048 + (((dx + x) & 2047) ^ 1);
                    vr[d2] = vr[s2];
                }
                g_gpu.eur[0x24 / 4] |= 1;
                u32 src = g_gpu.blt[0], dst = g_gpu.blt[1];
                if (m->log) fprintf(m->log, "BLIT %08x -> %08x %ux%u\n", src, dst, w, h);
            }
        }
        return;
    }
    if (a >= 0xf3000000 && a < 0xf3800000) { u32 *p = (u32 *)(g_gpu.eur_vram + (a - 0xf3000000)); *p = (*p & ~mask) | (v & mask); return; }
    if (a >= 0xc0000000 && a < 0xc2000000) { count(a & 0xffff0000, 1, v); u32 *p = (u32 *)(g_gpu.c0 + (a - 0xc0000000)); *p = (*p & ~mask) | (v & mask); return; }
}

/* 2D layers (Eurasia, unit 0): regs 0x190/0x194 (layer 1) and 0x198/0x19c (layer 2) give the rectangle in the
 * 2048x2048x16-bit sheet: x in 8-byte units (9 bits), y above; 0x1b0 bits 1/2 = 32bpp format (not seen yet),
 * 0x1b4 bits 0/1 = enable. Pixels are ABGR1555 with bit 15 = opaque. out = 640x960 (layer 1 rows 0-479,
 * layer 2 rows 480-959); returns the enable mask. */
int gpu_layers_direct(void) { return g_gpu.eur[0x1b8 / 4] & 3; }   /* 1 = over the 3D, 0 = behind it */
static void lay_dump(const u16 *out) {
    static long df = -2; if (df == -2) df = getenv("LAYDUMP") ? atol(getenv("LAYDUMP")) : -1;
    if (df >= 0 && (long)g_gpu.frame == df) { FILE *f = fopen("lay.bin", "wb"); if (f) { fwrite(out, 2, 640 * 960, f); fclose(f); }
        f = fopen("eurvram.bin", "wb"); if (f) { fwrite(g_gpu.eur_vram, 1, 2048 * 2048 * 2, f); fclose(f); } }
}
int gpu_layers(u16 *out) {
    const u16 *v = (const u16 *)g_gpu.eur_vram; int ena = g_gpu.eur[0x1b4 / 4] & 3;
    { static long lf = -2; if (lf == -2) lf = getenv("LAYLOG") ? atol(getenv("LAYLOG")) : -1;
      if (lf >= 0 && (long)g_gpu.frame % 50 == 0 && (long)g_gpu.frame >= lf) fprintf(stderr, "LAY f%llu 190=%08x 194=%08x 198=%08x 19c=%08x 1b0=%x 1b4=%x 1b8=%x 1bc=%x\n", (unsigned long long)g_gpu.frame,
          g_gpu.eur[0x190/4], g_gpu.eur[0x194/4], g_gpu.eur[0x198/4], g_gpu.eur[0x19c/4], g_gpu.eur[0x1b0/4], g_gpu.eur[0x1b4/4], g_gpu.eur[0x1b8/4], g_gpu.eur[0x1bc/4]); }
    for (int l = 0; l < 2; l++) {
        u16 *o = out + l * 640 * 480;
        u32 lo = g_gpu.eur[(0x190 + l * 8) / 4], hi = g_gpu.eur[(0x194 + l * 8) / 4];
        u32 x0 = (lo & 0x1ff) * 4, y0 = (lo >> 9) & 0x7ff, x1 = ((hi & 0x1ff) + 1) * 4, y1 = ((hi >> 9) & 0x7ff) + 1;
        if (!(ena & (1 << l)) || x1 <= x0 || y1 <= y0 || ((g_gpu.eur[0x1b0 / 4] >> (l + 1)) & 1)) { memset(o, 0, 640 * 480 * 2); ena &= ~(1 << l); continue; }
        u32 w = x1 - x0 > 640 ? 640 : x1 - x0, h = y1 - y0 > 480 ? 480 : y1 - y0;
        for (u32 y = 0; y < 480; y++) {
            if (y >= h) { memset(o + y * 640, 0, 640 * 2); continue; }
            const u16 *row = v + ((y0 + y) & 2047) * 2048;
            for (u32 x = 0; x < w; x++) o[y * 640 + x] = row[((x0 + x) & 2047) ^ 1];   /* pixel x = halfword x^1 */
            if (w < 640) memset(o + y * 640 + w, 0, (640 - w) * 2);
        }
    }
    /* 0x1bc bit 2: layer order. Set (race demo, in-game: 6) = layer 1 (unit 0) over layer 2, so the
       CREDIT(S) / INSERT COIN(S) / (C)SEGA line of unit 0 is not hidden by the opaque black of unit 1.
       Clear (URL screen 2, title 3) = layer 2 on top, as before. */
    if (g_gpu.eur[0x1bc / 4] & 4) ena |= 4;
    lay_dump(out); return ena;
}

/* called by the scheduler at each vblank */
#include "mie.h"
long g_press_test_frame = -1, g_press_service_frame = -1;
long g_test_frames[32]; int g_ntest; long g_svc_frames[64]; int g_nsvc;      /* extra scripted TEST presses (-test can be repeated) */
void gpu_vblank(Machine *m) {
    g_gpu.frame++;
    {   /* VRAMDUMP=f1,f2,... : dump the Eurasia sheet (+ layer regs) at those frames */
        static char *list = (char *)-1; if (list == (char *)-1) list = getenv("VRAMDUMP");
        if (list) { char key[24]; snprintf(key, sizeof key, ",%llu,", (unsigned long long)g_gpu.frame);
            char buf[512]; snprintf(buf, sizeof buf, ",%s,", list);
            if (strstr(buf, key)) { char fn[64]; snprintf(fn, sizeof fn, "frames/vram_%05llu.bin", (unsigned long long)g_gpu.frame);
                FILE *f = fopen(fn, "wb"); if (f) { fwrite(g_gpu.eur_vram, 1, 0x800000, f); fwrite(g_gpu.eur, 4, 0x400, f); fclose(f); } } }
    }
    /* scripted inputs: hold TEST / SERVICE for 10 frames starting at the given frame */
    g_input.test = g_press_test_frame >= 0 && (long)g_gpu.frame >= g_press_test_frame && (long)g_gpu.frame < g_press_test_frame + 10;
    for (int i = 0; i < g_ntest; i++) if ((long)g_gpu.frame >= g_test_frames[i] && (long)g_gpu.frame < g_test_frames[i] + 10) g_input.test = 1;
    g_input.tilt = g_input.test ? 0x80 : 0;   /* JVS system byte bit 7 = TEST (the game reads it to enter the menu) */
    { static int m = -1; if (m < 0) { const char *e = getenv("SWR_TESTSRC"); m = e ? atoi(e) : 0; }
      if (m == 1) g_input.test = 0; if (m == 2) g_input.tilt = 0;
      if (m == 3 && g_ntest && (long)g_gpu.frame >= g_test_frames[0] + 10) g_input.tilt = 0;
      if (m == 4 && g_ntest && (long)g_gpu.frame >= g_test_frames[0] + 10) g_input.test = 0; }
    g_input.service = g_press_service_frame >= 0 && (long)g_gpu.frame >= g_press_service_frame && (long)g_gpu.frame < g_press_service_frame + 10;
    for (int i = 0; i < g_nsvc; i++) if ((long)g_gpu.frame >= g_svc_frames[i] && (long)g_gpu.frame < g_svc_frames[i] + 6) g_input.service = 1;
    g_gpu.eur[0x18 / 4] |= 2;
    /* GPU modelled as finishing instantly: "done, needs feeding" on both units (Teso notes) */
    g_gpu.eur[0x18 / 4] |= 4;
    ((u32 *)g_gpu.ant_space)[0x88 / 4] |= 4;

    update_irq(m);
}
