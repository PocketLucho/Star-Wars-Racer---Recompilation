/* Converts the CP output (eye-space triangles + state) into GPU-ready vertices, shared by the
 * renderers. Projection (matches SWRA): screen = offset + eye.xy * f / -eye.z, depth reversed. */
#pragma once
#include <stdint.h>
#include "hikaru_cp.h"

typedef struct {
    float clip[4];        /* clip-space position (w = -eye.z) */
    float uv[2];          /* texel units */
    uint8_t color[4];     /* RGBA modulation */
    uint32_t tex[4];      /* x: fmt | bank<<4 | wrapU<<8 | wrapV<<9 | mirU<<10 | mirV<<11 | textured<<16 | punch<<17 | translucent<<18 | pattern(501)<<20
                             y: baseX | baseY<<16    z: logW | logH<<8    w: unused */
    uint8_t fog[4];       /* fog colour RGB + factor A (0 = no fog), applied after texturing */
} GfxVertex;

/* draw ranges, in submission order; kind selects the pipeline state */
enum { GFX_OPAQUE = 0, GFX_BLEND = 1, GFX_ADD = 2, GFX_TOP_BLEND = 3, GFX_TOP_ADD = 4 };
/* a run of triangles: pipeline kind, clip rectangle of its viewport (221, 640x480 pixels, y down) and whether the
   depth buffer is cleared before it (each 421 viewport number F is drawn after the lower ones with its own depth) */
typedef struct { int first, count, kind; int16_t clip[4]; uint8_t clear_depth; } GfxRange;

typedef struct {
    GfxRange *r; int nr, rcap;
    GfxVertex *v; int nv, cap;    /* opaque + punch-through first, then translucent */
    int n_opaque;                 /* vertices [0, n_opaque) opaque, [n_opaque, n_depth) translucent (in order), */
    int n_depth;                  /* [n_depth, nv) "always on top" (depth function 2/6), blended, no depth test */
    float clear[4];
    int blend_rate;               /* framebuffer blender (181): 0 = off, else the share of the frame before, 1..255 */
} GfxFrame;

#define GFX_NEAR 0.01f
extern float g_gfx_xscale;
void gfx_build(GfxFrame *f, const CpState *s);
/* per-vertex lighting + fog of vertex k (shared with the software renderer) */
void gfx_vertex_shade(const CpState *s, const CpTri *t, int k, const float face_n[3], uint8_t col[4], uint8_t fog[4]);
