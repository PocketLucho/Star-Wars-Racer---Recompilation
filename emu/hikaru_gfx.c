#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hikaru_gfx.h"
#include "gpu.h"

float g_gfx_xscale = 1.0f;   /* 0.75 in widescreen: the 4:3 image covers the central 3/4, more world at the sides */

static void reserve(GfxFrame *f, int n) {
    if (f->nv + n > f->cap) { f->cap = (f->nv + n) * 2 + 1024; f->v = realloc(f->v, f->cap * sizeof(GfxVertex)); }
}

static void make_vertex(GfxVertex *o, const CpTri *t, const CpVert *v, const uint8_t col[4], const uint8_t fog[4], uint32_t texw) {
    const CpViewport *vp = &t->view;
    float w = -v->pos[2];
    float sx = vp->off_x + v->pos[0] * vp->f / (w != 0 ? w : 1e-6f);
    float sy = vp->off_y - v->pos[1] * vp->f / (w != 0 ? w : 1e-6f);
    float xn = (sx / 320.0f - 1.0f) * g_gfx_xscale, yn = sy / 240.0f - 1.0f;
    /* widescreen: flat untextured screen quads that overflow the 4:3 edges (letterbox bars of RETIRED / FINISHED)
       reach the edges of the 16:9 picture */
    if (g_gfx_xscale < 1.0f && !((t->mat.flags881 >> 3) & 1) && t->v[0].pos[2] == t->v[1].pos[2] && t->v[1].pos[2] == t->v[2].pos[2]) {
        if (sx <= 0.0f) xn = -1.0f; else if (sx >= 640.0f) xn = 1.0f;
    }
    o->clip[0] = xn * w; o->clip[1] = yn * w; o->clip[2] = GFX_NEAR; o->clip[3] = w;
    {   /* 2C1 bit 14: texcoords in whole texels instead of 1/16 (Tatooine ground; verified against the arcade video) */
        float sc = (t->tex.w2c1 & 0x4000) ? 16.0f : 1.0f;
        o->uv[0] = v->uv[0] * sc; o->uv[1] = v->uv[1] * sc; }
    memcpy(o->color, col, 4); memcpy(o->fog, fog, 4);
    const CpTexhead *th = &t->tex;
    int bx = 0, by = 0;
    if (th->slotx >= 0x80 && th->sloty >= 0xc0) { bx = (th->slotx - 0x80) * 16; by = (th->sloty - 0xc0) * 16; }
    o->tex[0] = texw;
    o->tex[1] = (uint32_t)bx | ((uint32_t)by << 16);
    o->tex[2] = (uint32_t)th->logw | ((uint32_t)th->logh << 8);
    o->tex[3] = 0;
}

/* Hikaru lighting (model from Valkyrie's renderer): lit materials (881 shading mode != 0 with a lightset that
 * is not fully masked): colour = viewport ambient (011) * material ambient + sum over the enabled lights of
 * att * max(N.L, 0) * material diffuse * light diffuse; lights are given in eye space. Unlit: material ambient.
 * Fog (621, per viewport) on materials with the 881 depth-blend bit: linear from start to end (see below). */
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
static inline float rsqrt_fast(float x) {   /* ~23-bit accurate: rsqrtss + one Newton step */
    float y = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return y * (1.5f - 0.5f * x * y * y);
}
#else
static inline float rsqrt_fast(float x) { return 1.0f / sqrtf(x); }
#endif
static void norm3(float v[3]) { float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; if (l2 > 1e-24f) { float r = rsqrt_fast(l2); v[0] *= r; v[1] *= r; v[2] *= r; } }

/* per-frame lightset cache: light vectors normalised / classified once per frame instead of per vertex */
typedef struct { float L[3], pos[3], col[3], att0, att1; uint8_t point, infinite, att_type; } GfxLight;
typedef struct { int n; GfxLight l[4]; } GfxLs;
static GfxLs *ls_cache; static int ls_cap; static const CpState *ls_owner; static uint32_t ls_gen; static int ls_n = -1;
static void prepare_lights(const CpState *s) {
    if (ls_owner == s && ls_gen == s->gen && ls_n == s->nls) return;
    if (s->nls > ls_cap) { ls_cap = s->nls * 2 + 64; ls_cache = realloc(ls_cache, ls_cap * sizeof(GfxLs)); }
    for (int i = 0; i < s->nls; i++) {
        const CpLightset *ls = &s->ls_frame[i]; GfxLs *o = &ls_cache[i]; o->n = 0;
        if (ls->mask == 0xf) continue;
        for (int li = 0; li < 4; li++) {
            const CpLight *l = &ls->l[li];
            if ((ls->mask & (1 << li)) || !(l->has_pos || l->has_dir)) continue;
            GfxLight *g = &o->l[o->n++];
            g->point = l->has_pos; memcpy(g->pos, l->pos, sizeof g->pos); memcpy(g->L, l->dir, sizeof g->L); norm3(g->L);
            g->infinite = l->att_type == 0 && l->att[0] == 1.0f && l->att[1] == 1.0f;
            g->att_type = l->att_type; g->att0 = l->att[0]; g->att1 = l->att[1];
            for (int k = 0; k < 3; k++) g->col[k] = l->diffuse[k] / 255.0f;
        }
    }
    ls_owner = s; ls_gen = s->gen; ls_n = s->nls;
}

void gfx_vertex_shade(const CpState *s, const CpTri *t, int k, const float face_n[3], uint8_t col[4], uint8_t fog[4]) {
    const CpMaterial *m = &t->mat; const CpViewport *vp = &t->view; const CpVert *v = &t->v[k];
    int textured = (m->flags881 >> 3) & 1, mode = m->flags881 & 3, translucent = t->polytype == 0xd;
    prepare_lights(s);
    const GfxLs *ls = (t->ls >= 0 && t->ls < s->nls) ? &ls_cache[t->ls] : 0;
    float md[3], ma[3], c[3];
    /* 091/291 roles: the game sets 091=0, 291=FF on the Tatooine sand (lit by the sun, not by ambient), so 091 is
     * taken as the ambient coefficient and 291 as the diffuse one (Valkyrie has them the other way round).
     * Viewport ambient (011, 16-bit) scaled /512: matches the arcade video in the hangar and the race.
     * Both are visual fits; SWR_MATSWAP=0 / SWR_AMBDIV=<n> override them. */
    static int swp = -1; if (swp < 0) swp = getenv("SWR_MATSWAP") ? atoi(getenv("SWR_MATSWAP")) != 0 : 1;
    const uint8_t *D = swp ? m->ambient : m->diffuse, *A = swp ? m->diffuse : m->ambient;
    for (int i = 0; i < 3; i++) { md[i] = D[i] * (1.0f / 255.0f); ma[i] = A[i] * (1.0f / 255.0f); }
    int has_a = A[0] | A[1] | A[2], has_d = D[0] | D[1] | D[2];
    /* both colours 0: a textured mesh shows its texels (Odin: texture mode 0 takes the texel as the base colour);
       an untextured one is really black (the letterbox bars of the RETIRED / FINISHED screens; 0.8 grey before) */
    if (!has_a) { if (has_d) memcpy(ma, md, sizeof ma); else ma[0] = ma[1] = ma[2] = textured ? 1.0f : 0.0f; }
    if (mode && ls && ls->n) {
        float n[3];
        if (mode == 2 || (v->nrm[0] == 0 && v->nrm[1] == 0 && v->nrm[2] == 0)) memcpy(n, face_n, sizeof n);   /* flat / no normal */
        else memcpy(n, v->nrm, sizeof n);
        norm3(n);
        static float amb_div = -1; if (amb_div < 0) amb_div = getenv("SWR_AMBDIV") ? (float)atof(getenv("SWR_AMBDIV")) : 512.0f;
        for (int i = 0; i < 3; i++) c[i] = clamp01(vp->ambient[i] / amb_div) * ma[i];
        if (has_d && (c[0] < 1 || c[1] < 1 || c[2] < 1))
        for (int li = 0; li < ls->n; li++) {
            const GfxLight *l = &ls->l[li];
            float L[3], d = 0.001f, att = 1;
            if (l->point) { for (int i = 0; i < 3; i++) L[i] = l->pos[i] - v->pos[i]; float l2 = L[0] * L[0] + L[1] * L[1] + L[2] * L[2];
                            if (l2 > 1e-24f) { float id = rsqrt_fast(l2); d = l2 * id; L[0] *= id; L[1] *= id; L[2] *= id; } }
            else memcpy(L, l->L, sizeof L);
            float ndl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
            if (ndl <= 0) continue;
            if (!l->infinite) {
                float dd = l->att_type == 1 ? d * d : l->att_type == 2 ? 1.0f / d : l->att_type == 3 ? 1.0f / (d * d) : d;
                att = clamp01(l->att0 * (l->att1 + dd));
            }
            float k2 = att * ndl;
            for (int i = 0; i < 3; i++) c[i] += k2 * md[i] * l->col[i];
        }
    } else memcpy(c, ma, sizeof c);
    for (int i = 0; i < 3; i++) col[i] = (uint8_t)(clamp01(c[i]) * 255.0f + 0.5f);
    /* translucent alpha = poly alpha (103) x vertex alpha (push header bits 24-31), as in Valkyrie */
    col[3] = translucent ? (uint8_t)(clamp01(t->polyalpha * (v->alpha / 255.0f)) * 255) : 255;
    fog[0] = fog[1] = fog[2] = fog[3] = 0;
    static int nofog = -1; if (nofog < 0) nofog = getenv("SWR_NOFOG") != 0;
    if (!nofog && vp->fog_on && ((m->flags881 >> 2) & 1)) {
        /* 621 params as built by the game (@0C014A96): P = 1/(end-start), Q = near/end  ->  f = 1 - P*(end - z) */
        float end = vp->fog_bias > 0 ? vp->n / vp->fog_bias : 1e30f;
        float f = clamp01(1.0f - vp->fog_density * (end - (-v->pos[2])));
        fog[0] = vp->fog_color[0]; fog[1] = vp->fog_color[1]; fog[2] = vp->fog_color[2]; fog[3] = (uint8_t)(f * 255.0f + 0.5f);
    }
}

typedef struct { float pos[3], nrm[3], fn[3], fogp[3], alpha; uint32_t vamb[3]; int ls; uint32_t flags; uint8_t mat[6], fog_on, flat; } VCacheKey;
static VCacheKey vc_key[4]; static uint8_t vc_col[4][4], vc_fog[4][4]; static int vc_valid[4], vc_next;

static int pending_clear;
static void add_range(GfxFrame *f, int kind, int first, int count, const CpViewport *v) {
    if (!count) return;
    int16_t c[4] = { (int16_t)(v->l < 0 ? 0 : v->l > 640 ? 640 : v->l), (int16_t)(v->b < 0 ? 0 : v->b > 480 ? 480 : v->b),
                     (int16_t)(v->r < 0 ? 0 : v->r > 640 ? 640 : v->r), (int16_t)(v->t < 0 ? 0 : v->t > 480 ? 480 : v->t) };
    if (c[2] <= c[0] || c[3] <= c[1]) { c[0] = 0; c[1] = 0; c[2] = 640; c[3] = 480; }   /* no 221 clip: whole screen */
    GfxRange *p = f->nr ? &f->r[f->nr - 1] : 0;
    if (p && !pending_clear && p->kind == kind && p->first + p->count == first && !memcmp(p->clip, c, sizeof c)) { p->count += count; return; }
    if (f->nr >= f->rcap) { f->rcap = f->rcap ? f->rcap * 2 : 64; f->r = realloc(f->r, f->rcap * sizeof(GfxRange)); }
    GfxRange *g = &f->r[f->nr++];
    g->first = first; g->count = count; g->kind = kind; memcpy(g->clip, c, sizeof c); g->clear_depth = (uint8_t)pending_clear;
    pending_clear = 0;
}

void gfx_build(GfxFrame *f, const CpState *s) {
    f->nr = 0;
    memset(vc_valid, 0, sizeof vc_valid);
    f->nv = 0; f->n_opaque = 0;
    f->blend_rate = (s->fb181 >> 24) & 1 ? (int)((s->fb181 >> 16) & 0xff) : 0;
    f->clear[0] = f->clear[1] = f->clear[2] = 0; f->clear[3] = 1;
    if (s->has_clear) {
        f->clear[0] = (s->clear_color & 255) / 255.0f;
        f->clear[1] = ((s->clear_color >> 8) & 255) / 255.0f;
        f->clear[2] = ((s->clear_color >> 16) & 255) / 255.0f;
    }
    /* pass 0: opaque, 1: translucent (both depth-tested), 2: viewports whose 421 depth function is 2 or 6
     * (menu titles at w~5, race gauges at the far plane): drawn last, in order, without depth test */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("BLENDSTATS") != 0;
      if (dbg) { int cnt[4][16] = {{0}}; for (int i = 0; i < s->ntris; i++) cnt[s->tris[i].mat.blend & 3][s->tris[i].polytype & 15]++;
        if (g_gpu.frame % 20 == 0) { fprintf(stderr, "F%llu", (unsigned long long)g_gpu.frame);
          for (int b = 0; b < 4; b++) for (int p = 0; p < 16; p++) if (cnt[b][p]) fprintf(stderr, " b%d/t%x:%d", b, p, cnt[b][p]); fprintf(stderr, "\n"); } } }
    { static long vf = -2; if (vf == -2) vf = getenv("VPSTATS") ? atol(getenv("VPSTATS")) : -1;
      if (vf >= 0 && ((long)g_gpu.frame == vf || (getenv("VPSTATS2") && (long)g_gpu.frame % 500 == 0))) {
        int last = -99, run = 0; float ll = 0, lr = 0, lb = 0, lt = 0;
        for (int i = 0; i <= s->ntris; i++) { const CpTri *t = i < s->ntris ? &s->tris[i] : 0;
          int same = t && t->view.dfunc == last && t->view.l == ll && t->view.r == lr && t->view.b == lb && t->view.t == lt;
          if (!same) { if (run) fprintf(stderr, "VP f%lld df=%d clip l=%g r=%g b=%g t=%g n=%d\n", (long long)g_gpu.frame, last, ll, lr, lb, lt, run);
            if (t) { last = t->view.dfunc; ll = t->view.l; lr = t->view.r; lb = t->view.b; lt = t->view.t; run = 0; } }
          run++; } } }
    /* 421 F = viewport number: lower numbers first, each over what is there with a depth buffer of its own (Odin);
     * inside one: opaque, then translucent in order. Each run is clipped to its viewport (221): the HOW TO CONTROL
     * screen draws its frame as F=3, the lever picture as F=4 and the race inside the frame box as F=7. */
    unsigned groups = 0; for (int i = 0; i < s->ntris; i++) groups |= 1u << (s->tris[i].view.dfunc & 7);
    int first_group = 1; pending_clear = 0;
    for (int grp = 0; grp < 8; grp++) {
      if (!(groups & (1u << grp))) continue;
      if (!first_group) pending_clear = 1;
      first_group = 0;
      for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s->ntris; i++) {
            const CpTri *t = &s->tris[i];
            int add = t->mat.blend == 2;                         /* A81 mode 2: additive (glows, flares) */
            int translucent = t->polytype == 0xd || add;
            if ((t->view.dfunc & 7) != grp || translucent != pass) continue;
            { static int sk = -2; if (sk == -2) { const char *e = getenv("SWR_SKIPPASS"); sk = e ? atoi(e) : -1; } if (sk == pass) continue; }
            { static float nc = -2; if (nc == -2) { const char *e = getenv("SWR_NEARCULL"); nc = e ? (float)atof(e) : -1; }
              if (nc >= 0 && (-t->v[0].pos[2] < nc || -t->v[1].pos[2] < nc || -t->v[2].pos[2] < nc)) continue; }
            add_range(f, pass == 0 ? GFX_OPAQUE : (add ? GFX_ADD : GFX_BLEND), f->nv, 3, &t->view);
            float a[3], b[3], n[3];
            for (int k = 0; k < 3; k++) { a[k] = t->v[1].pos[k] - t->v[0].pos[k]; b[k] = t->v[2].pos[k] - t->v[0].pos[k]; }
            n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0];
            if (n[2] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }    /* face normal towards the viewer (fallback only) */
            int textured = (t->mat.flags881 >> 3) & 1;
            uint8_t col[3][4], fog[3][4];
            for (int k = 0; k < 3; k++) {
                gfx_vertex_shade(s, t, k, n, col[k], fog[k]);
            }
            { static long hl = -2; if (hl == -2) { const char *e = getenv("SWR_HILITE"); hl = e ? strtol(e, 0, 16) : -1; }
              if (hl >= 0 && t->tex.w4c1 == (uint32_t)hl) for (int k = 0; k < 3; k++) { col[k][0] = 255; col[k][1] = col[k][2] = 0; col[k][3] = 255; fog[k][3] = 0; } }
            const CpTexhead *th = &t->tex;
            uint32_t texw = (uint32_t)th->fmt | ((uint32_t)th->bank << 4) |
                            ((uint32_t)((th->wrap >> 1) & 1) << 8) | ((uint32_t)(th->wrap & 1) << 9) |
                            ((uint32_t)((th->repeat >> 1) & 1) << 10) | ((uint32_t)(th->repeat & 1) << 11) |
                            ((uint32_t)textured << 16) | ((uint32_t)(t->polytype != 3) << 17) | ((uint32_t)translucent << 18) |
                            ((uint32_t)(t->pattern & 0x1f) << 20);
            reserve(f, 3);
            for (int k = 0; k < 3; k++) make_vertex(&f->v[f->nv++], t, &t->v[k], col[k], fog[k], texw);
        }
      }
    }
    f->n_opaque = f->n_depth = f->nv;
}
