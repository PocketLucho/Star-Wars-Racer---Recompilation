/* ARM7 interpreter (ARMv4 ARM-state subset used by the Sega AICA sound drivers). Our own code. */
#include <string.h>
#include "arm7.h"

#define N_FLAG 0x80000000u
#define Z_FLAG 0x40000000u
#define C_FLAG 0x20000000u
#define V_FLAG 0x10000000u
#define I_BIT 0x80u
#define F_BIT 0x40u
#define MODE(a) ((a)->cpsr & 0x1f)

static int bank_index(uint32_t mode) {
    switch (mode) { case 0x11: return 1; case 0x12: return 2; case 0x13: return 3; case 0x17: return 4; case 0x1b: return 5; default: return 0; }
}

/* switch register banks from the current mode to `nmode` */
static void switch_mode(Arm7 *a, uint32_t nmode) {
    uint32_t omode = MODE(a);
    if (omode == nmode) return;
    int ob = bank_index(omode), nb = bank_index(nmode);
    if ((omode == 0x11) != (nmode == 0x11)) {
        int of = omode == 0x11, nf = nmode == 0x11;
        memcpy(a->bank_r8_12[of], &a->r[8], 5 * 4);
        memcpy(&a->r[8], a->bank_r8_12[nf], 5 * 4);
    }
    a->bank_r13_14[ob][0] = a->r[13]; a->bank_r13_14[ob][1] = a->r[14];
    a->r[13] = a->bank_r13_14[nb][0]; a->r[14] = a->bank_r13_14[nb][1];
    if (ob) a->bank_spsr[ob] = a->spsr;
    a->spsr = nb ? a->bank_spsr[nb] : a->cpsr;
    a->cpsr = (a->cpsr & ~0x1fu) | nmode;
}

static void set_cpsr(Arm7 *a, uint32_t v) {
    switch_mode(a, v & 0x1f);
    a->cpsr = v;
}

void arm7_reset(Arm7 *a) {
    void *ctx = a->ctx;
    uint32_t (*r32)(void *, uint32_t) = a->read32; uint16_t (*r16)(void *, uint32_t) = a->read16; uint8_t (*r8)(void *, uint32_t) = a->read8;
    void (*w32)(void *, uint32_t, uint32_t) = a->write32; void (*w16)(void *, uint32_t, uint16_t) = a->write16; void (*w8)(void *, uint32_t, uint8_t) = a->write8;
    void (*ack)(void *) = a->fiq_ack;
    memset(a, 0, sizeof *a);
    a->ctx = ctx; a->read32 = r32; a->read16 = r16; a->read8 = r8; a->write32 = w32; a->write16 = w16; a->write8 = w8; a->fiq_ack = ack;
    a->cpsr = 0x13 | I_BIT | F_BIT;    /* SVC, interrupts off */
    a->r[15] = 0;
}

void arm7_set_fiq(Arm7 *a, int level) { a->fiq_line = level; }

static void exception(Arm7 *a, uint32_t vector, uint32_t mode, uint32_t lr, int disable_fiq) {
    uint32_t old = a->cpsr;
    switch_mode(a, mode);
    a->spsr = old;
    a->r[14] = lr;
    a->cpsr |= I_BIT; if (disable_fiq) a->cpsr |= F_BIT;
    a->r[15] = vector;
}

static int cond_ok(uint32_t c, uint32_t f) {
    int n = !!(f & N_FLAG), z = !!(f & Z_FLAG), cy = !!(f & C_FLAG), v = !!(f & V_FLAG);
    switch (c) {
    case 0: return z; case 1: return !z; case 2: return cy; case 3: return !cy;
    case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
    case 8: return cy && !z; case 9: return !cy || z; case 10: return n == v; case 11: return n != v;
    case 12: return !z && n == v; case 13: return z || n != v; case 14: return 1;
    }
    return 0;
}

/* register read where r15 reads as instruction address + 8 (or + 12 for register-specified shifts) */
static inline uint32_t R(Arm7 *a, int n, uint32_t pc, int extra) { return n == 15 ? pc + 8 + extra : a->r[n]; }

static uint32_t shift_op(Arm7 *a, uint32_t ins, uint32_t pc, int *carry) {
    int type = (ins >> 5) & 3, amt;
    uint32_t v;
    int c = !!(a->cpsr & C_FLAG);
    if (ins & 0x10) {                          /* shift by register */
        v = R(a, ins & 15, pc, 4);
        amt = a->r[(ins >> 8) & 15] & 0xff;
        if (amt == 0) { *carry = c; return v; }
        switch (type) {
        case 0: if (amt < 32) { *carry = (v >> (32 - amt)) & 1; return v << amt; } *carry = amt == 32 ? v & 1 : 0; return 0;
        case 1: if (amt < 32) { *carry = (v >> (amt - 1)) & 1; return v >> amt; } *carry = amt == 32 ? v >> 31 : 0; return 0;
        case 2: if (amt < 32) { *carry = (v >> (amt - 1)) & 1; return (uint32_t)((int32_t)v >> amt); } *carry = v >> 31; return (uint32_t)((int32_t)v >> 31);
        default: amt &= 31; if (!amt) { *carry = v >> 31; return v; } *carry = (v >> (amt - 1)) & 1; return (v >> amt) | (v << (32 - amt));
        }
    }
    v = R(a, ins & 15, pc, 0);
    amt = (ins >> 7) & 31;
    switch (type) {
    case 0: if (!amt) { *carry = c; return v; } *carry = (v >> (32 - amt)) & 1; return v << amt;
    case 1: if (!amt) { *carry = v >> 31; return 0; } *carry = (v >> (amt - 1)) & 1; return v >> amt;
    case 2: if (!amt) { *carry = v >> 31; return (uint32_t)((int32_t)v >> 31); } *carry = (v >> (amt - 1)) & 1; return (uint32_t)((int32_t)v >> amt);
    default: if (!amt) { *carry = v & 1; return (v >> 1) | ((uint32_t)c << 31); }   /* RRX */
             *carry = (v >> (amt - 1)) & 1; return (v >> amt) | (v << (32 - amt));
    }
}

static void set_nz(Arm7 *a, uint32_t r) { a->cpsr = (a->cpsr & ~(N_FLAG | Z_FLAG)) | (r & N_FLAG) | (r ? 0 : Z_FLAG); }

static void data_proc(Arm7 *a, uint32_t ins, uint32_t pc) {
    int op = (ins >> 21) & 15, s = (ins >> 20) & 1, rd = (ins >> 12) & 15, rn = (ins >> 16) & 15;
    int carry = !!(a->cpsr & C_FLAG);
    uint32_t op2;
    if (ins & 0x02000000) {
        uint32_t imm = ins & 0xff, rot = ((ins >> 8) & 15) * 2;
        op2 = rot ? (imm >> rot) | (imm << (32 - rot)) : imm;
        if (rot) carry = op2 >> 31;
    } else op2 = shift_op(a, ins, pc, &carry);
    uint32_t op1 = R(a, rn, pc, (!(ins & 0x02000000) && (ins & 0x10)) ? 4 : 0);
    uint32_t res = 0; int logical = 0; uint64_t wide; int write = 1;
    int c = carry, v = !!(a->cpsr & V_FLAG);
    switch (op) {
    case 0x0: res = op1 & op2; logical = 1; break;                                  /* AND */
    case 0x1: res = op1 ^ op2; logical = 1; break;                                  /* EOR */
    case 0x2: res = op1 - op2; c = op1 >= op2; v = ((op1 ^ op2) & (op1 ^ res)) >> 31; break;   /* SUB */
    case 0x3: res = op2 - op1; c = op2 >= op1; v = ((op2 ^ op1) & (op2 ^ res)) >> 31; break;   /* RSB */
    case 0x4: wide = (uint64_t)op1 + op2; res = (uint32_t)wide; c = wide >> 32; v = (~(op1 ^ op2) & (op1 ^ res)) >> 31; break;  /* ADD */
    case 0x5: wide = (uint64_t)op1 + op2 + !!(a->cpsr & C_FLAG); res = (uint32_t)wide; c = wide >> 32; v = (~(op1 ^ op2) & (op1 ^ res)) >> 31; break; /* ADC */
    case 0x6: { uint32_t ci = !!(a->cpsr & C_FLAG); res = op1 - op2 - !ci; c = (uint64_t)op1 >= (uint64_t)op2 + !ci; v = ((op1 ^ op2) & (op1 ^ res)) >> 31; break; } /* SBC */
    case 0x7: { uint32_t ci = !!(a->cpsr & C_FLAG); res = op2 - op1 - !ci; c = (uint64_t)op2 >= (uint64_t)op1 + !ci; v = ((op2 ^ op1) & (op2 ^ res)) >> 31; break; } /* RSC */
    case 0x8: res = op1 & op2; logical = 1; write = 0; break;                       /* TST */
    case 0x9: res = op1 ^ op2; logical = 1; write = 0; break;                       /* TEQ */
    case 0xa: res = op1 - op2; c = op1 >= op2; v = ((op1 ^ op2) & (op1 ^ res)) >> 31; write = 0; break;  /* CMP */
    case 0xb: wide = (uint64_t)op1 + op2; res = (uint32_t)wide; c = wide >> 32; v = (~(op1 ^ op2) & (op1 ^ res)) >> 31; write = 0; break; /* CMN */
    case 0xc: res = op1 | op2; logical = 1; break;                                  /* ORR */
    case 0xd: res = op2; logical = 1; break;                                        /* MOV */
    case 0xe: res = op1 & ~op2; logical = 1; break;                                 /* BIC */
    case 0xf: res = ~op2; logical = 1; break;                                       /* MVN */
    }
    if (s) {
        if (rd == 15 && write) {            /* restore CPSR from SPSR */
            if (bank_index(MODE(a))) set_cpsr(a, a->spsr);
        } else {
            set_nz(a, res);
            a->cpsr = (a->cpsr & ~C_FLAG) | (c ? C_FLAG : 0);
            if (!logical) a->cpsr = (a->cpsr & ~V_FLAG) | (v ? V_FLAG : 0);
        }
    }
    if (write) { a->r[rd] = res; if (rd == 15) a->r[15] &= ~3u; }
}

static void block_transfer(Arm7 *a, uint32_t ins, uint32_t pc) {
    int p = (ins >> 24) & 1, u = (ins >> 23) & 1, s = (ins >> 22) & 1, w = (ins >> 21) & 1, l = (ins >> 20) & 1;
    int rn = (ins >> 16) & 15; uint32_t list = ins & 0xffff;
    int n = 0; for (int i = 0; i < 16; i++) n += (list >> i) & 1;
    uint32_t base = a->r[rn], start;
    if (u) start = p ? base + 4 : base; else start = p ? base - n * 4 : base - n * 4 + 4;
    uint32_t nb = u ? base + n * 4 : base - n * 4;
    int user_bank = s && !(l && (list & 0x8000));
    uint32_t saved_mode = MODE(a);
    if (user_bank) switch_mode(a, 0x10);
    uint32_t addr = start;
    if (l) {
        if (w) a->r[rn] = nb;
        for (int i = 0; i < 16; i++) if (list & (1 << i)) { a->r[i] = a->read32(a->ctx, addr & ~3u); addr += 4; }
        if (list & 0x8000) { a->r[15] &= ~3u; if (s && bank_index(saved_mode)) set_cpsr(a, a->spsr); }
    } else {
        for (int i = 0; i < 16; i++) if (list & (1 << i)) { a->write32(a->ctx, addr & ~3u, i == 15 ? pc + 12 : a->r[i]); addr += 4; }
        if (w) a->r[rn] = nb;
    }
    if (user_bank) switch_mode(a, saved_mode);
    a->cycles += n + 1;
}

static void single_transfer(Arm7 *a, uint32_t ins, uint32_t pc) {
    int i = (ins >> 25) & 1, p = (ins >> 24) & 1, u = (ins >> 23) & 1, b = (ins >> 22) & 1, w = (ins >> 21) & 1, l = (ins >> 20) & 1;
    int rn = (ins >> 16) & 15, rd = (ins >> 12) & 15;
    uint32_t off;
    if (i) { int c; off = shift_op(a, ins & ~0x10u, pc, &c); } else off = ins & 0xfff;
    uint32_t base = R(a, rn, pc, 0);
    uint32_t ea = u ? base + off : base - off;
    uint32_t addr = p ? ea : base;
    if (l) {
        uint32_t v;
        if (b) v = a->read8(a->ctx, addr);
        else { v = a->read32(a->ctx, addr & ~3u); int rot = (addr & 3) * 8; if (rot) v = (v >> rot) | (v << (32 - rot)); }
        if (!p || w) a->r[rn] = ea;
        a->r[rd] = v; if (rd == 15) a->r[15] &= ~3u;
    } else {
        uint32_t v = rd == 15 ? pc + 12 : a->r[rd];
        if (b) a->write8(a->ctx, addr, (uint8_t)v); else a->write32(a->ctx, addr & ~3u, v);
        if (!p || w) a->r[rn] = ea;
    }
    a->cycles += 2;
}

static void halfword_transfer(Arm7 *a, uint32_t ins, uint32_t pc) {
    int p = (ins >> 24) & 1, u = (ins >> 23) & 1, imm = (ins >> 22) & 1, w = (ins >> 21) & 1, l = (ins >> 20) & 1;
    int rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, sh = (ins >> 5) & 3;
    uint32_t off = imm ? (((ins >> 4) & 0xf0) | (ins & 15)) : a->r[ins & 15];
    uint32_t base = R(a, rn, pc, 0), ea = u ? base + off : base - off, addr = p ? ea : base;
    if (l) {
        uint32_t v = 0;
        if (sh == 1) v = a->read16(a->ctx, addr & ~1u);
        else if (sh == 2) v = (uint32_t)(int32_t)(int8_t)a->read8(a->ctx, addr);
        else if (sh == 3) v = (uint32_t)(int32_t)(int16_t)a->read16(a->ctx, addr & ~1u);
        if (!p || w) a->r[rn] = ea;
        a->r[rd] = v;
    } else {
        if (sh == 1) a->write16(a->ctx, addr & ~1u, (uint16_t)(rd == 15 ? pc + 12 : a->r[rd]));
        if (!p || w) a->r[rn] = ea;
    }
    a->cycles += 2;
}

static void step(Arm7 *a) {
    if (a->fiq_line && !(a->cpsr & F_BIT)) {
        exception(a, 0x1c, 0x11, a->r[15] + 4, 1);
        if (a->fiq_ack) a->fiq_ack(a->ctx);
    }
    uint32_t pc = a->r[15];
    uint32_t ins = a->read32(a->ctx, pc);
    a->r[15] = pc + 4;
    a->insns++; a->cycles++;
    if (!cond_ok(ins >> 28, a->cpsr)) return;
    uint32_t t = (ins >> 25) & 7;
    switch (t) {
    case 0:
        if ((ins & 0x0fc000f0) == 0x00000090) {                     /* MUL / MLA */
            int rd = (ins >> 16) & 15, rn = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
            uint32_t r = a->r[rm] * a->r[rs]; if (ins & 0x00200000) r += a->r[rn];
            a->r[rd] = r; if (ins & 0x00100000) set_nz(a, r);
            a->cycles += 2; return;
        }
        if ((ins & 0x0f8000f0) == 0x00800090) {                     /* UMULL/UMLAL/SMULL/SMLAL */
            int hi = (ins >> 16) & 15, lo = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
            uint64_t r;
            if (ins & 0x00400000) r = (uint64_t)((int64_t)(int32_t)a->r[rm] * (int64_t)(int32_t)a->r[rs]);
            else r = (uint64_t)a->r[rm] * a->r[rs];
            if (ins & 0x00200000) r += ((uint64_t)a->r[hi] << 32) | a->r[lo];
            a->r[lo] = (uint32_t)r; a->r[hi] = (uint32_t)(r >> 32);
            if (ins & 0x00100000) a->cpsr = (a->cpsr & ~(N_FLAG | Z_FLAG)) | (a->r[hi] & N_FLAG) | (r ? 0 : Z_FLAG);
            a->cycles += 3; return;
        }
        if ((ins & 0x0fb00ff0) == 0x01000090) {                     /* SWP / SWPB */
            int rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rm = ins & 15;
            uint32_t addr = a->r[rn];
            if (ins & 0x00400000) { uint8_t o = a->read8(a->ctx, addr); a->write8(a->ctx, addr, (uint8_t)a->r[rm]); a->r[rd] = o; }
            else { uint32_t o = a->read32(a->ctx, addr & ~3u); a->write32(a->ctx, addr & ~3u, a->r[rm]); a->r[rd] = o; }
            a->cycles += 3; return;
        }
        if ((ins & 0x0e000090) == 0x00000090 && (ins & 0x60)) { halfword_transfer(a, ins, pc); return; }
        if ((ins & 0x0fbf0fff) == 0x010f0000) { a->r[(ins >> 12) & 15] = (ins & 0x00400000) ? a->spsr : a->cpsr; return; }  /* MRS */
        if ((ins & 0x0fb0fff0) == 0x0120f000 || (ins & 0x0fb0f000) == 0x0320f000) { goto msr; }
        data_proc(a, ins, pc); return;
    case 1:
        if ((ins & 0x0fb0f000) == 0x0320f000) goto msr;
        data_proc(a, ins, pc); return;
    case 2: case 3:
        if (t == 3 && (ins & 0x10)) { exception(a, 0x04, 0x1b, pc + 4, 0); a->undefined++; return; }
        single_transfer(a, ins, pc); return;
    case 4: block_transfer(a, ins, pc); return;
    case 5: {                                                       /* B / BL */
        int32_t off = (int32_t)(ins << 8) >> 6;
        if (ins & 0x01000000) a->r[14] = pc + 4;
        a->r[15] = pc + 8 + off; a->cycles += 2; return;
    }
    case 6: exception(a, 0x04, 0x1b, pc + 4, 0); a->undefined++; return;   /* coprocessor: undefined */
    case 7:
        if (ins & 0x01000000) { exception(a, 0x08, 0x13, pc + 4, 0); return; }   /* SWI */
        exception(a, 0x04, 0x1b, pc + 4, 0); a->undefined++; return;
    }
    return;
msr: {
        uint32_t v;
        if (ins & 0x02000000) { uint32_t imm = ins & 0xff, rot = ((ins >> 8) & 15) * 2; v = rot ? (imm >> rot) | (imm << (32 - rot)) : imm; }
        else v = a->r[ins & 15];
        uint32_t mask = 0;
        if (ins & 0x00080000) mask |= 0xff000000; if (ins & 0x00040000) mask |= 0x00ff0000;
        if (ins & 0x00020000) mask |= 0x0000ff00; if (ins & 0x00010000) mask |= 0x000000ff;
        if (ins & 0x00400000) { if (bank_index(MODE(a))) a->spsr = (a->spsr & ~mask) | (v & mask); }
        else {
            if (MODE(a) == 0x10) mask &= 0xff000000;         /* user mode: flags only */
            set_cpsr(a, (a->cpsr & ~mask) | (v & mask));
        }
    }
}

void arm7_run(Arm7 *a, int cycles) {
    int64_t end = a->cycles + cycles;
    if (!a->yield_pc) { while (a->cycles < end) step(a); return; }
    /* idle skip: once the driver's main loop comes back to its head, the rest of this slice is idle
     * (time-critical work runs in the FIQ handler, which still fires at the next sample) */
    int64_t start = a->cycles;
    while (a->cycles < end) {
        if (a->r[15] == a->yield_pc && a->cycles != start && !(a->fiq_line && !(a->cpsr & 0x40))) { a->cycles = end; a->yielded = 1; break; }
        step(a);
    }
}
