/* Hikaru GPU command processor (CP): executes a command list and produces triangles.
 * Our own implementation; instruction encodings from the documentation in Valkyrie
 * (Stefano Teso, hikaru-gpu-cp.c) and MAME notes. Renderer-agnostic: the output is a list of
 * triangles in eye space with their state, consumed by a rasterizer (debug software one now,
 * Vulkan later). */
#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct {
    float off_x, off_y;            /* 221: screen-space centre */
    float l, r, b, t;              /* 221: clip rectangle (pixels) */
    float f, f2, n;                /* 021: "far" (projection scale?), alt, near */
    float dmin, dmax; int dfunc;   /* 421 */
    uint8_t fog_on, fog_color[3];  /* 621: depth queue (fog) */
    float fog_density, fog_bias;
    uint32_t clear;                /* 191 */
    uint32_t ambient[3];           /* 011 */
} CpViewport;

/* lights (Valkyrie: 051/451 colours, 061 attenuation, 961/B61 direction/position, 104 commit) */
typedef struct {
    float dir[3], pos[3], att[2];
    uint8_t att_type, has_dir, has_pos, has_spec;
    uint16_t diffuse[3];           /* 10-bit, 255 = 1.0 */
    uint8_t specular[3];
} CpLight;
typedef struct { CpLight l[4]; uint8_t mask; } CpLightset;   /* mask bit i = light i disabled */

typedef struct {
    uint8_t diffuse[3], ambient[3], specular[4];
    uint32_t flags881;             /* shading mode, textured, ... */
    uint8_t blend, alpha_test;
} CpMaterial;

typedef struct {
    uint32_t w2c1, w4c1, w0c1;     /* raw format/size, slot, bias words */
    int fmt, logw, logh, slotx, sloty, bank, wrap, repeat;
} CpTexhead;

typedef struct {
    float pos[3];                  /* eye space */
    float nrm[3];
    float uv[2];
    uint8_t alpha;
    uint8_t mtx;                   /* push header bit 12: which of the two loaded matrices (skinning) */
} CpVert;

typedef struct {
    CpVert v[3];
    int vp;                        /* viewport table index active (scratch copy stored separately) */
    CpViewport view;
    CpMaterial mat;
    CpTexhead tex;
    int polytype; float polyalpha;
    uint8_t pattern;
    uint32_t info, src_pc;
    int ls;                        /* index into CpState.ls_frame (lightset active), -1 = none */
} CpTri;

typedef struct {
    /* memory */
    const uint8_t *cmdram;         /* 4 MB, PCI 0x48000000 */
    const uint8_t *slave_ram;      /* 32 MB, PCI 0x40000000 (may be NULL) */
    /* execution */
    uint32_t pc, stack[64]; int sp; uint8_t pat_stack[64];
    uint8_t pattern;            /* 501: 4x4 screen-door pattern for the following polygons (0 = all pixels) */
    int running, zero_run; long steps;
    /* object tables */
    CpViewport vp_scratch, vp_table[8], vp_stack[32]; int vp_depth;
    float mv[32][4][4]; int mv_depth, mv_total, last_tri_count;
    CpMaterial mat_scratch, *mat_table; uint32_t mat_base;
    CpTexhead tex_scratch, *tex_table; uint32_t tex_base;
    float precision; int polytype; float polyalpha;
    CpLight lit_scratch, *lit_table;              /* 1024 lights */
    CpLightset ls_scratch, *ls_table; int ls_base; /* 256 lightsets */
    CpLightset *ls_frame; int nls, capls, cur_ls; /* lightsets recalled this frame (referenced by CpTri.ls) */
    uint32_t gen;                                 /* incremented every cp_frame (cache key for renderers) */
    /* vertex assembly */
    CpVert tmp[3]; uint32_t tmp_info; int nverts;
    /* output */
    CpTri *tris; int ntris, cap;
    /* stats / debug */
    uint32_t clear_color; int has_clear;      /* 191: -------a gggggggg bbbbbbbb rrrrrrrr */
    FILE *log; long unknown_ops; uint32_t opcount[0x200];
    uint32_t fb181, fb781;                    /* framebuffer configuration: 181 (blender: bit 24 on, bits 16-23 rate), 781 (3D buffers) */
} CpState;

void cp_init(CpState *s);
void cp_frame(CpState *s, uint32_t start_pc, long max_steps);   /* runs until kill/end, fills s->tris */
