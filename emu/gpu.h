#pragma once
#include "sh4.h"
#include <stdio.h>
struct Machine;
typedef struct {
    u8 *ant_space;      /* PCI 0x00000000-0x00ffffff */
    u32 eur[0x400];     /* PCI 0xf2000000-0xf2000fff */
    u8 *eur_vram;       /* PCI 0xf3000000-0xf37fffff */
    u8 *c0;             /* PCI 0xc0000000-0xc1ffffff (unknown, via master ap1) */
    u32 blt[4];
    u8 *texram[2];      /* 2 banks x 2048x1024 16-bit texels (4 MB each) */
    u64 idma_entries, idma_bytes, cp_steps, cp_tris;
    int texram_dirty;
    int ant_int;
    u64 frame;
} Gpu;
extern Gpu g_gpu;
void gpu_init(void);
int gpu_handles(u32 a);
u32 gpu_r32(struct Machine *m, u32 a);
void gpu_w32(struct Machine *m, u32 a, u32 v, u32 mask);
void gpu_vblank(struct Machine *m);
int gpu_layers(u16 *out640x960);
int gpu_layers_direct(void);       /* 0x1b8: bit n set = layer n+1 drawn over the 3D, clear = behind it */   /* 2D layers 1/2 as ABGR1555 (bit15 = opaque), returns enable mask */
void gpu_dump_stats(FILE *f);
void hikaru_set_ant_int(struct Machine *m, int line);
