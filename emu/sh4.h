/* SH-4 CPU state and instruction semantics shared by the interpreter and
 * the statically recompiled code. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

#define SR_T   0x00000001u
#define SR_S   0x00000002u
#define SR_IMASK 0x000000f0u
#define SR_Q   0x00000100u
#define SR_M   0x00000200u
#define SR_FD  0x00008000u
#define SR_BL  0x10000000u
#define SR_RB  0x20000000u
#define SR_MD  0x40000000u

#define FPSCR_PR 0x00080000u
#define FPSCR_SZ 0x00100000u
#define FPSCR_FR 0x00200000u

typedef struct Cpu Cpu;

/* bus interface, implemented by the machine (hikaru.c) */
u8  bus_r8 (Cpu *c, u32 a);
u16 bus_r16(Cpu *c, u32 a);
u32 bus_r32(Cpu *c, u32 a);
void bus_w8 (Cpu *c, u32 a, u8 v);
void bus_w16(Cpu *c, u32 a, u16 v);
void bus_w32(Cpu *c, u32 a, u32 v);
void sh4_pref(Cpu *c, u32 a);          /* store-queue flush */
void sh4_on_sr_change(Cpu *c);
void sh4_on_sleep(Cpu *c);
void sh4_trap(Cpu *c, u32 imm, u32 pc);
void sh4_illegal(Cpu *c, u32 pc, u16 op);

struct Cpu {
    u32 r[16];
    u32 rbank[8];          /* the inactive bank of r0-r7 */
    u32 sr, gbr, vbr, ssr, spc, sgr, dbr, mach, macl, pr, pc;
    u32 fpscr, fpul;
    union { u32 u[2][16]; float f[2][16]; } fpr;  /* [bank][reg]; bank = FR bit */
    int fb;                 /* current float bank index (fpscr.FR) */
    /* machine side */
    int id;                 /* 0 = master, 1 = slave */
    u64 cycles;
    int sleeping;
    u32 irl;                /* external interrupt level 0..15 (IRL encoded) */
    u32 intevt_pending;     /* internal (on-chip) interrupt pending event code, 0 = none */
    u32 intevt_level;
    u32 sq[2][8];           /* store queues */
    u8  ocram[0x4000];
    void *machine;          /* owning machine */
    /* on-chip register file (P4 area, 0xFF000000..), sparse */
    u32 onchip[0x10000];
    /* TMU */
    u32 tcnt[3], tcor[3], tcr[3]; u8 tstr;
    /* DMAC */
    u32 sar[4], dar[4], dmatcr[4], chcr[4], dmaor;
    /* trace */
    int trace;
};

/* ---------------- register helpers ---------------- */
#define FR(c,n)   ((c)->fpr.f[(c)->fb][n])
#define FRU(c,n)  ((c)->fpr.u[(c)->fb][n])
#define XFU(c,n)  ((c)->fpr.u[(c)->fb ^ 1][n])

static inline double sh4_getdr(Cpu *c, int n) {          /* n even */
    u64 v = ((u64)FRU(c, n) << 32) | FRU(c, n + 1);
    double d; memcpy(&d, &v, 8); return d;
}
static inline void sh4_setdr(Cpu *c, int n, double d) {
    u64 v; memcpy(&v, &d, 8); FRU(c, n) = (u32)(v >> 32); FRU(c, n + 1) = (u32)v;
}

static inline void sh4_set_fpscr(Cpu *c, u32 v) {
    c->fpscr = v & 0x003fffff;
    c->fb = (v & FPSCR_FR) ? 1 : 0;
}

static inline void sh4_bank_swap(Cpu *c) {
    for (int i = 0; i < 8; i++) { u32 t = c->r[i]; c->r[i] = c->rbank[i]; c->rbank[i] = t; }
}

static inline void sh4_set_sr(Cpu *c, u32 v) {
    v &= 0x700083f3;
    u32 old = c->sr;
    int oa = (old & SR_MD) && (old & SR_RB);
    int na = (v & SR_MD) && (v & SR_RB);
    if (oa != na) sh4_bank_swap(c);
    c->sr = v;
    sh4_on_sr_change(c);
}

#define T_GET(c)    ((c)->sr & SR_T)
#define T_SET(c,x)  ((c)->sr = ((c)->sr & ~SR_T) | ((x) ? SR_T : 0))

/* bank register access for stc/ldc Rm_BANK (always the *inactive* bank when RB=..) */
static inline u32 *sh4_bankreg(Cpu *c, int m) { return &c->rbank[m]; }

/* ---------------- arithmetic helpers ---------------- */
static inline void op_div1(Cpu *c, int n, int m) {
    u32 tmp0, tmp2; u8 old_q, tmp1;
    old_q = (c->sr & SR_Q) ? 1 : 0;
    u32 Q = (c->r[n] & 0x80000000) ? 1 : 0;
    tmp2 = c->r[m];
    c->r[n] = (c->r[n] << 1) | T_GET(c);
    u32 M = (c->sr & SR_M) ? 1 : 0;
    if (!old_q) {
        if (!M) { tmp0 = c->r[n]; c->r[n] -= tmp2; tmp1 = c->r[n] > tmp0;
            Q = Q ? (tmp1 == 0) : tmp1; }
        else    { tmp0 = c->r[n]; c->r[n] += tmp2; tmp1 = c->r[n] < tmp0;
            Q = Q ? tmp1 : (tmp1 == 0); }
    } else {
        if (!M) { tmp0 = c->r[n]; c->r[n] += tmp2; tmp1 = c->r[n] < tmp0;
            Q = Q ? (tmp1 == 0) : tmp1; }
        else    { tmp0 = c->r[n]; c->r[n] -= tmp2; tmp1 = c->r[n] > tmp0;
            Q = Q ? tmp1 : (tmp1 == 0); }
    }
    c->sr = (c->sr & ~SR_Q) | (Q ? SR_Q : 0);
    T_SET(c, Q == M);
}

static inline void op_macl(Cpu *c, int n, int m) {
    s32 a = (s32)bus_r32(c, c->r[n]); c->r[n] += 4;
    s32 b = (s32)bus_r32(c, c->r[m]); c->r[m] += 4;
    s64 mul = (s64)a * (s64)b;
    s64 acc = (s64)(((u64)c->mach << 32) | c->macl);
    acc += mul;
    if (c->sr & SR_S) {
        if (acc > 0x00007fffffffffffLL) acc = 0x00007fffffffffffLL;
        if (acc < -0x0000800000000000LL) acc = -0x0000800000000000LL;
    }
    c->mach = (u32)((u64)acc >> 32); c->macl = (u32)acc;
}
static inline void op_macw(Cpu *c, int n, int m) {
    s32 a = (s16)bus_r16(c, c->r[n]); c->r[n] += 2;
    s32 b = (s16)bus_r16(c, c->r[m]); c->r[m] += 2;
    s32 mul = a * b;
    if (c->sr & SR_S) {
        s64 r = (s64)(s32)c->macl + mul;
        if (r > 0x7fffffffLL) { r = 0x7fffffff; c->mach |= 1; }
        else if (r < -0x80000000LL) { r = -0x80000000LL; c->mach |= 1; }
        c->macl = (u32)r;
    } else {
        s64 acc = (s64)(((u64)c->mach << 32) | c->macl);
        acc += mul;
        c->mach = (u32)((u64)acc >> 32); c->macl = (u32)acc;
    }
}

static inline u32 ftrc_sat_f(float f) {
    if (isnan(f)) return 0x80000000u;
    if (f >= 2147483648.0f) return 0x7fffffffu;
    if (f < -2147483648.0f) return 0x80000000u;
    return (u32)(s32)f;
}
static inline u32 ftrc_sat_d(double f) {
    if (isnan(f)) return 0x80000000u;
    if (f >= 2147483648.0) return 0x7fffffffu;
    if (f < -2147483648.0) return 0x80000000u;
    return (u32)(s32)f;
}

static inline void op_fsca(Cpu *c, int n) {
    double ang = (double)(c->fpul & 0xffff) * (2.0 * M_PI / 65536.0);
    FR(c, n * 2) = (float)sin(ang);
    FR(c, n * 2 + 1) = (float)cos(ang);
}
static inline void op_fipr(Cpu *c, int n, int m) {
    int vn = n * 4, vm = m * 4;
    float s = FR(c, vn) * FR(c, vm) + FR(c, vn + 1) * FR(c, vm + 1)
            + FR(c, vn + 2) * FR(c, vm + 2) + FR(c, vn + 3) * FR(c, vm + 3);
    FR(c, vn + 3) = s;
}
static inline void op_ftrv(Cpu *c, int n) {
    int v = n * 4; float x[4]; int ob = c->fb ^ 1;
    for (int i = 0; i < 4; i++) x[i] = FR(c, v + i);
    for (int i = 0; i < 4; i++)
        FR(c, v + i) = c->fpr.f[ob][i] * x[0] + c->fpr.f[ob][4 + i] * x[1]
                     + c->fpr.f[ob][8 + i] * x[2] + c->fpr.f[ob][12 + i] * x[3];
}

/* fmov with SZ=1 register pair helpers: n even -> DRn (current bank), n odd -> XDn&~1 (other bank) */
static inline u32 *fpair(Cpu *c, int n) {
    return (n & 1) ? &c->fpr.u[c->fb ^ 1][n & ~1] : &c->fpr.u[c->fb][n];
}
