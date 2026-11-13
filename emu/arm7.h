/* ARM7 (ARMv4, ARM state) interpreter, one instance per AICA sound CPU. */
#pragma once
#include <stdint.h>

typedef struct Arm7 {
    uint32_t r[16];
    uint32_t cpsr, spsr;
    uint32_t bank_r8_12[2][5];       /* [0] = non-FIQ, [1] = FIQ */
    uint32_t bank_r13_14[6][2];      /* usr/sys, fiq, irq, svc, abt, und */
    uint32_t bank_spsr[6];
    int fiq_line, irq_line;
    int64_t cycles;
    void *ctx;
    uint32_t (*read32)(void *ctx, uint32_t a);
    uint16_t (*read16)(void *ctx, uint32_t a);
    uint8_t  (*read8)(void *ctx, uint32_t a);
    void (*write32)(void *ctx, uint32_t a, uint32_t v);
    void (*write16)(void *ctx, uint32_t a, uint16_t v);
    void (*write8)(void *ctx, uint32_t a, uint8_t v);
    void (*fiq_ack)(void *ctx);      /* optional */
    uint64_t insns, undefined;
    uint32_t yield_pc; int yielded;               /* if != 0: arm7_run ends its slice when it reaches this pc (main-loop head) */
} Arm7;

void arm7_reset(Arm7 *a);
void arm7_set_fiq(Arm7 *a, int level);
void arm7_run(Arm7 *a, int cycles);
