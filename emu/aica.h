/* Yamaha AICA sound chip (x2 on Hikaru) + its ARM7 sound CPU. Our own implementation; register
 * semantics, envelope timings and level conversions follow the AICA documentation as used by Flycast. */
#pragma once
#include <stdint.h>
#include "arm7.h"

typedef struct {
    int active;            /* playing */
    uint32_t sa;           /* start address (bytes) */
    uint32_t pos;          /* current sample index relative to SA */
    uint32_t frac;         /* 10-bit fraction */
    uint32_t step;         /* 10-bit fixed point step */
    int looped;
    int eg_state;          /* 0 attack 1 decay1 2 decay2 3 release */
    int32_t att;           /* 10-bit attenuation << 16 */
    int16_t prev, cur;     /* for interpolation */
    int32_t adpcm_q, adpcm_last, adpcm_q_loop, adpcm_last_loop; int adpcm_loop_saved;
} AicaChan;

typedef struct Aica {
    int id;
    uint8_t *ram; uint32_t ram_mask;
    uint8_t regs[0x8000];
    AicaChan ch[64];
    Arm7 arm; int arm_running;
    int timer_cnt[3];      /* prescaler counters */
    int fiq_out; uint32_t fiq_L;
    int sh4_irq;           /* MCIEB & MCIPD */
    int arm_cycles_per_sample;
    uint32_t wcount[0x4000]; uint32_t *rcount, *pchist; uint32_t samples_since_reset; int arm_sleep;
    uint64_t samples;
} Aica;

void aica_init(Aica *a, int id, uint8_t *ram, uint32_t ram_size);
uint32_t aica_reg_read(Aica *a, uint32_t addr, int size);            /* SH-4 side */
void aica_reg_write(Aica *a, uint32_t addr, uint32_t v, int size);  /* SH-4 side */
void aica_set_arm_reset(Aica *a, int asserted);
/* run n samples: steps the ARM, timers and channels; mixes into out (stereo int16 pairs, added) */
void aica_run(Aica *a, int n, int32_t *mix);
#include <stdio.h>
void aica_stats(Aica *a, FILE *f);
