/* Debug software rasterizer for Hikaru CP output (flat/textured, z-buffered). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include "miniz.h"       /* zlib-compatible compress/crc32 */
#else
#include <zlib.h>
#endif
#include "hikaru_cp.h"
#include "swrender.h"
#include "hikaru_gfx.h"
static float sr_fog_a, sr_fog_c[3];

#define W 640
#define H 480
static uint8_t fb[H][W][3]; static float zb[H][W];
int sr_proj_mode = 2; float sr_scale_k = 1.0f;
uint8_t *sr_texram;       /* 2 banks x 4 MB */
int sr_no_tex;

/* texel fetch: TEXRAM bank as 2048x1024 16-bit sheet, linear 16-bit texels */
static uint16_t tex16(int bank, int x, int y) {
    x &= 2047; y &= 1023;
    uint32_t o = (uint32_t)y * 4096 + x * 2;
    const uint8_t *b = sr_texram + bank * 0x400000;
    return b[o] | (b[o + 1] << 8);
}
static int wrapc(int c, int n, int wrap, int mirror) {
    if (!wrap) return c < 0 ? 0 : c >= n ? n - 1 : c;
    if (mirror) { int p = c / n; c %= n; if (c < 0) { c += n; p--; } return (p & 1) ? n - 1 - c : c; }
    c %= n; return c < 0 ? c + n : c;
}
/* returns 0xAARRGGBB */
static uint32_t sample(const CpTexhead *t, float u, float v) {
    if (t->w2c1 & 0x4000) { u *= 16; v *= 16; }   /* texcoords in whole texels (see hikaru_gfx.c) */
    int w = 16 << t->logw, h = 16 << t->logh;
    int x = wrapc((int)floorf(u), w, (t->wrap >> 1) & 1, (t->repeat >> 1) & 1);
    int y = wrapc((int)floorf(v), h, t->wrap & 1, t->repeat & 1);
    int bx = (t->slotx - 0x80) * 16, by = (t->sloty - 0xc0) * 16;
    if (t->slotx < 0x80 || t->sloty < 0xc0) { bx = by = 0; }
    uint32_t r, g, b, a;
    switch (t->fmt) {
    case 0: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = (p & 31) << 3; g = ((p >> 5) & 31) << 3; b = ((p >> 10) & 31) << 3; a = (p >> 15) ? 255 : 0; break; }
    case 1: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = (p & 15) * 17; g = ((p >> 4) & 15) * 17; b = ((p >> 8) & 15) * 17; a = (p >> 12) * 17; break; }
    case 4: { uint16_t p = tex16(t->bank, bx + x, by + y);
        r = g = b = p & 255; a = p >> 8; break; }
    case 2: default: { /* ABGR1111: one 16-bit word per 2x2 block of 4-bit texels (bit3 R, bit2 G, bit1 B, bit0 A);
                          logical size 2w x 2h, words start at column bx/2 (same as the Vulkan shader) */
        int qx = wrapc((int)floorf(u), 2 * w, (t->wrap >> 1) & 1, (t->repeat >> 1) & 1), qy = wrapc((int)floorf(v), 2 * h, t->wrap & 1, t->repeat & 1);
        uint16_t p = tex16(t->bank, bx / 2 + qx / 2, by + qy / 2);
        uint32_t n = (p >> (12 - 4 * ((qx & 1) + 2 * (qy & 1)))) & 15;
        r = (n & 8) ? 255 : 0; g = (n & 4) ? 255 : 0; b = (n & 2) ? 255 : 0; a = (n & 1) ? 255 : 0; break; }
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void sr_png(const char *path) {
    size_t rl = W * 3 + 1; uint8_t *raw = malloc(rl * H);
    for (int y = 0; y < H; y++) { raw[y * rl] = 0; memcpy(raw + y * rl + 1, fb[y], W * 3); }
    uLongf cl = compressBound(rl * H); uint8_t *cz = malloc(cl); compress(cz, &cl, raw, rl * H);
    FILE *f = fopen(path, "wb");
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    #define CH(t, d, n) do { uint8_t hd[8]; uint32_t L = (n); hd[0]=L>>24;hd[1]=L>>16;hd[2]=L>>8;hd[3]=L; memcpy(hd+4,t,4); fwrite(hd,1,8,f); fwrite(d,1,L,f); \
        uint32_t c = crc32(0, (const Bytef*)t, 4); c = crc32(c, (const Bytef*)(d), L); uint8_t cc[4]={c>>24,c>>16,c>>8,c}; fwrite(cc,1,4,f);} while(0)
    uint8_t ih[13] = { 0,0,W>>8,W&255, 0,0,H>>8,H&255, 8, 2, 0, 0, 0 };
    CH("IHDR", ih, 13); CH("IDAT", cz, cl); CH("IEND", "", 0);
    fclose(f); free(raw); free(cz);
}

/* eye -> screen. Hypotheses selectable with -proj */
static int project(const CpTri *t, const float *p, float *sx, float *sy, float *sz) {
    const CpViewport *v = &t->view;
    float x = p[0], y = p[1], z = p[2];
    float foc = v->f * sr_scale_k;
    if (sr_proj_mode == 0) { if (z <= 0.001f) return 0; *sx = v->off_x + x * foc / z; *sy = v->off_y - y * foc / z; *sz = z; }
    else if (sr_proj_mode == 1) { if (z <= 0.001f) return 0; *sx = v->off_x + x * foc / z; *sy = v->off_y + y * foc / z; *sz = z; }
    else { if (-z <= 0.001f) return 0; *sx = v->off_x + x * foc / -z; *sy = v->off_y - y * foc / -z; *sz = -z; }
    return 1;
}

static void raster(const CpTri *t, const float *sx, const float *sy, const float *sz, const uint8_t c[3], int textured) {
    float x0 = sx[0], y0 = sy[0], x1 = sx[1], y1 = sy[1], x2 = sx[2], y2 = sy[2];
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-6f) return;
    int minx = (int)fmaxf(0, floorf(fminf(x0, fminf(x1, x2)))), maxx = (int)fminf(W - 1, ceilf(fmaxf(x0, fmaxf(x1, x2))));
    int miny = (int)fmaxf(0, floorf(fminf(y0, fminf(y1, y2)))), maxy = (int)fminf(H - 1, ceilf(fmaxf(y0, fmaxf(y1, y2))));
    if (maxx - minx > 4000 || maxy - miny > 4000) return;
    float iz[3] = { 1 / sz[0], 1 / sz[1], 1 / sz[2] };
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
        float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
        float w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float zi = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
        float z = 1 / zi;
        if (z >= zb[y][x]) continue;
        uint8_t r = c[0], g = c[1], b = c[2];
        if (textured) {
            float u = (w0 * t->v[0].uv[0] * iz[0] + w1 * t->v[1].uv[0] * iz[1] + w2 * t->v[2].uv[0] * iz[2]) * z;
            float v = (w0 * t->v[0].uv[1] * iz[0] + w1 * t->v[1].uv[1] * iz[1] + w2 * t->v[2].uv[1] * iz[2]) * z;
            uint32_t tx = sample(&t->tex, u, v);
            if ((tx >> 24) < 128 && t->polytype != 3) continue;     /* punch-through / transparent texel */
            r = (uint8_t)(((tx >> 16) & 255) * c[0] / 255); g = (uint8_t)(((tx >> 8) & 255) * c[1] / 255); b = (uint8_t)((tx & 255) * c[2] / 255);
        }
        if (sr_fog_a > 0) { r = (uint8_t)(r + (sr_fog_c[0] - r) * sr_fog_a); g = (uint8_t)(g + (sr_fog_c[1] - g) * sr_fog_a); b = (uint8_t)(b + (sr_fog_c[2] - b) * sr_fog_a); }
        zb[y][x] = z; fb[y][x][0] = r; fb[y][x][1] = g; fb[y][x][2] = b;
    }
}


/* draw all triangles produced by the CP into the framebuffer */
void sr_render(const CpState *s) {
    uint8_t cr = 0, cg = 0, cb = 0;
    if (s->has_clear) { cr = s->clear_color & 255; cg = (s->clear_color >> 8) & 255; cb = (s->clear_color >> 16) & 255; }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { zb[y][x] = 1e30f; fb[y][x][0] = cr; fb[y][x][1] = cg; fb[y][x][2] = cb; }
    for (int i = 0; i < s->ntris; i++) {
        const CpTri *t = &s->tris[i]; float sx[3], sy[3], sz[3]; int ok = 1;
        for (int k = 0; k < 3; k++) ok &= project(t, t->v[k].pos, &sx[k], &sy[k], &sz[k]);
        if (!ok) continue;
        float a[3], b[3], n[3];
        for (int k = 0; k < 3; k++) { a[k] = t->v[1].pos[k] - t->v[0].pos[k]; b[k] = t->v[2].pos[k] - t->v[0].pos[k]; }
        n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0];
        float ln = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-9f;
        (void)ln; if (n[2] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        uint8_t col[3]; int textured = sr_texram && !sr_no_tex && ((t->mat.flags881 >> 3) & 1);
        {   /* flat: average of the per-vertex lighting (shared with the Vulkan path), fog folded in */
            uint8_t vc[3][4], vf[3][4]; float acc[3] = { 0, 0, 0 }, fa = 0, fc[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; k++) { gfx_vertex_shade(s, t, k, n, vc[k], vf[k]); for (int i = 0; i < 3; i++) { acc[i] += vc[k][i]; fc[i] = vf[k][i]; } fa += vf[k][3]; }
            fa /= 3 * 255.0f;
            for (int i = 0; i < 3; i++) col[i] = (uint8_t)(acc[i] / 3);
            sr_fog_a = fa; for (int i = 0; i < 3; i++) sr_fog_c[i] = fc[i];
        }
        raster(t, sx, sy, sz, col, textured);
    }
}

/* write an RGBA8 buffer as PNG (RGB) */
void sr_png_rgba_fp(FILE *f, const uint8_t *rgba, int w, int h);
void sr_png_rgba(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    sr_png_rgba_fp(f, rgba, w, h);
}
void sr_png_rgba_fp(FILE *f, const uint8_t *rgba, int w, int h) {
    size_t rl = (size_t)w * 3 + 1; uint8_t *raw = malloc(rl * h);
    for (int y = 0; y < h; y++) { raw[y * rl] = 0; for (int x = 0; x < w; x++) memcpy(raw + y * rl + 1 + x * 3, rgba + ((size_t)y * w + x) * 4, 3); }
    uLongf cl = compressBound(rl * h); uint8_t *cz = malloc(cl); compress(cz, &cl, raw, rl * h);
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
    #define CH2(t, d, n) do { uint8_t hd[8]; uint32_t L = (n); hd[0]=L>>24;hd[1]=L>>16;hd[2]=L>>8;hd[3]=L; memcpy(hd+4,t,4); fwrite(hd,1,8,f); fwrite(d,1,L,f); \
        uint32_t c = crc32(0, (const Bytef*)t, 4); c = crc32(c, (const Bytef*)(d), L); uint8_t cc[4]={c>>24,c>>16,c>>8,c}; fwrite(cc,1,4,f);} while(0)
    uint8_t ih[13] = { w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0 };
    CH2("IHDR", ih, 13); CH2("IDAT", cz, cl); CH2("IEND", "", 0);
    fclose(f); free(raw); free(cz);
}

/* 2D layers over the 3D image: layer 2 over layer 1, ABGR1555 with bit 15 = opaque */
void sr_overlay(const uint16_t *lay, int ena, int direct) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        for (int k = 0; k < 2; k++) {
            int l = (ena & 4) ? k : 1 - k;          /* bit 2: layer 1 on top (Eurasia 0x1bc bit 2) */
            if (!(ena & (1 << l))) continue;
            if (!(direct & (1 << l)) && zb[y][x] < 1e30f) continue;   /* behind the 3D: only where nothing was drawn */
            uint16_t p = lay[l * 640 * 480 + y * 640 + x];
            if (!(p & 0x8000)) continue;
            fb[y][x][0] = (uint8_t)((p & 31) << 3); fb[y][x][1] = (uint8_t)(((p >> 5) & 31) << 3); fb[y][x][2] = (uint8_t)(((p >> 10) & 31) << 3);
            break;
        }
    }
}
