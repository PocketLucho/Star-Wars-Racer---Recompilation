/* Instruction semantics (non-branch), shared by interpreter and recompiled code.
 * When called with a constant opcode the compiler folds the switch away. */
#pragma once
#include "sh4.h"
#include "optable.h"
extern unsigned char sh4_optab[65536];
static inline __attribute__((always_inline)) int sh4_opid_const(u16 op) {
    int np = sizeof(sh4_patterns) / sizeof(sh4_patterns[0]);
    for (int i = 0; i < np; i++) if ((op & sh4_patterns[i].mask) == sh4_patterns[i].val) return sh4_patterns[i].id;
    return OP_ILLEGAL;
}
#define exec_plain(c, op, pc) exec_plain_id((c), sh4_optab[(op)], (op), (pc))
#define sh4_opid_inline(op) (__builtin_constant_p(op) ? sh4_opid_const(op) : sh4_optab[op])
#define N   ((op >> 8) & 15)
#define M   ((op >> 4) & 15)
#define I8  (op & 0xff)
#define SI8 ((s32)(s8)(op & 0xff))
#define D4  (op & 0xf)
#define R   (c->r)
#define PR_ (c->fpscr & FPSCR_PR)
#define SZ_ (c->fpscr & FPSCR_SZ)

/* returns 0 normally; non-branch only. pc = address of this instruction */
/* id = decoded instruction id (OP_*). Recompiled code passes it as a constant, so only one case survives. */
static inline __attribute__((always_inline)) void exec_plain_id(Cpu *c, int id, u16 op, u32 pc) {
    u32 t;
    switch (id) {
    case OP_CLRT: c->sr &= ~SR_T; break;
    case OP_CLRMAC: c->mach = c->macl = 0; break;
    case OP_CLRS: c->sr &= ~SR_S; break;
    case OP_SETT: c->sr |= SR_T; break;
    case OP_SETS: c->sr |= SR_S; break;
    case OP_NOP: break;
    case OP_SLEEP: sh4_on_sleep(c); break;
    case OP_DIV0U: c->sr &= ~(SR_M | SR_Q | SR_T); break;
    case OP_LDTLB: break;
    case OP_FSCHG: sh4_set_fpscr(c, c->fpscr ^ FPSCR_SZ); break;
    case OP_FRCHG: sh4_set_fpscr(c, c->fpscr ^ FPSCR_FR); break;
    case OP_FTRV_XMTRX_FVN: op_ftrv(c, (op >> 10) & 3); break;
    case OP_FSCA_FPUL_DRN: op_fsca(c, (op >> 9) & 7); break;
    case OP_FCNVSD_FPUL_DRN: { float f; memcpy(&f, &c->fpul, 4); sh4_setdr(c, ((op >> 9) & 7) * 2, (double)f); } break;
    case OP_FCNVDS_DRM_FPUL: { float f = (float)sh4_getdr(c, ((op >> 9) & 7) * 2); memcpy(&c->fpul, &f, 4); } break;
    case OP_MOVT_RN: R[N] = T_GET(c); break;
    case OP_PREF_AT_RN: sh4_pref(c, R[N]); break;
    case OP_OCBI_AT_RN: case OP_OCBP_AT_RN: case OP_OCBWB_AT_RN: break;
    case OP_MOVCA_L_R0_AT_RN: bus_w32(c, R[N], R[0]); break;
    case OP_STC_SR_RN: R[N] = c->sr; break;
    case OP_STC_GBR_RN: R[N] = c->gbr; break;
    case OP_STC_VBR_RN: R[N] = c->vbr; break;
    case OP_STC_SSR_RN: R[N] = c->ssr; break;
    case OP_STC_SPC_RN: R[N] = c->spc; break;
    case OP_STC_SGR_RN: R[N] = c->sgr; break;
    case OP_STC_DBR_RN: R[N] = c->dbr; break;
    case OP_STS_MACH_RN: R[N] = c->mach; break;
    case OP_STS_MACL_RN: R[N] = c->macl; break;
    case OP_STS_PR_RN: R[N] = c->pr; break;
    case OP_STS_FPUL_RN: R[N] = c->fpul; break;
    case OP_STS_FPSCR_RN: R[N] = c->fpscr; break;
    case OP_SHLL_RN: T_SET(c, R[N] >> 31); R[N] <<= 1; break;
    case OP_SHLR_RN: T_SET(c, R[N] & 1); R[N] >>= 1; break;
    case OP_ROTL_RN: T_SET(c, R[N] >> 31); R[N] = (R[N] << 1) | (R[N] >> 31); break;
    case OP_ROTR_RN: T_SET(c, R[N] & 1); R[N] = (R[N] >> 1) | (R[N] << 31); break;
    case OP_SHLL2_RN: R[N] <<= 2; break;
    case OP_SHLR2_RN: R[N] >>= 2; break;
    case OP_SHLL8_RN: R[N] <<= 8; break;
    case OP_SHLR8_RN: R[N] >>= 8; break;
    case OP_SHLL16_RN: R[N] <<= 16; break;
    case OP_SHLR16_RN: R[N] >>= 16; break;
    case OP_DT_RN: R[N]--; T_SET(c, R[N] == 0); break;
    case OP_CMP_PZ_RN: T_SET(c, (s32)R[N] >= 0); break;
    case OP_CMP_PL_RN: T_SET(c, (s32)R[N] > 0); break;
    case OP_SHAL_RN: T_SET(c, R[N] >> 31); R[N] <<= 1; break;
    case OP_SHAR_RN: T_SET(c, R[N] & 1); R[N] = (u32)((s32)R[N] >> 1); break;
    case OP_ROTCL_RN: t = R[N] >> 31; R[N] = (R[N] << 1) | T_GET(c); T_SET(c, t); break;
    case OP_ROTCR_RN: t = R[N] & 1; R[N] = (R[N] >> 1) | (T_GET(c) << 31); T_SET(c, t); break;
    case OP_TAS_B_AT_RN: { u8 v = bus_r8(c, R[N]); T_SET(c, v == 0); bus_w8(c, R[N], v | 0x80); } break;
    case OP_LDC_RN_SR: sh4_set_sr(c, R[N]); break;
    case OP_LDC_RN_GBR: c->gbr = R[N]; break;
    case OP_LDC_RN_VBR: c->vbr = R[N]; break;
    case OP_LDC_RN_SSR: c->ssr = R[N]; break;
    case OP_LDC_RN_SPC: c->spc = R[N]; break;
    case OP_LDC_RN_DBR: c->dbr = R[N]; break;
    case OP_LDC_L_AT_RN_INC_SR: t = bus_r32(c, R[N]); R[N] += 4; sh4_set_sr(c, t); break;
    case OP_LDC_L_AT_RN_INC_GBR: c->gbr = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDC_L_AT_RN_INC_VBR: c->vbr = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDC_L_AT_RN_INC_SSR: c->ssr = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDC_L_AT_RN_INC_SPC: c->spc = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDC_L_AT_RN_INC_DBR: c->dbr = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_STC_L_SR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->sr); break;
    case OP_STC_L_GBR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->gbr); break;
    case OP_STC_L_VBR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->vbr); break;
    case OP_STC_L_SSR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->ssr); break;
    case OP_STC_L_SPC_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->spc); break;
    case OP_STC_L_SGR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->sgr); break;
    case OP_STC_L_DBR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->dbr); break;
    case OP_LDS_RN_MACH: c->mach = R[N]; break;
    case OP_LDS_RN_MACL: c->macl = R[N]; break;
    case OP_LDS_RN_PR: c->pr = R[N]; break;
    case OP_LDS_RN_FPUL: c->fpul = R[N]; break;
    case OP_LDS_RN_FPSCR: sh4_set_fpscr(c, R[N]); break;
    case OP_LDS_L_AT_RN_INC_MACH: c->mach = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDS_L_AT_RN_INC_MACL: c->macl = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDS_L_AT_RN_INC_PR: c->pr = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDS_L_AT_RN_INC_FPUL: c->fpul = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_LDS_L_AT_RN_INC_FPSCR: t = bus_r32(c, R[N]); R[N] += 4; sh4_set_fpscr(c, t); break;
    case OP_STS_L_MACH_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->mach); break;
    case OP_STS_L_MACL_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->macl); break;
    case OP_STS_L_PR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->pr); break;
    case OP_STS_L_FPUL_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->fpul); break;
    case OP_STS_L_FPSCR_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->fpscr); break;
    case OP_FSTS_FPUL_FRN: FRU(c, N) = c->fpul; break;
    case OP_FLDS_FRN_FPUL: c->fpul = FRU(c, N); break;
    case OP_FLOAT_FPUL_FRN:
        if (PR_) sh4_setdr(c, N & 14, (double)(s32)c->fpul); else FR(c, N) = (float)(s32)c->fpul; break;
    case OP_FTRC_FRN_FPUL:
        c->fpul = PR_ ? ftrc_sat_d(sh4_getdr(c, N & 14)) : ftrc_sat_f(FR(c, N)); break;
    case OP_FNEG_FRN: FRU(c, N) ^= 0x80000000u; break;   /* DR: sign in FRn (upper) */
    case OP_FABS_FRN: FRU(c, N) &= 0x7fffffffu; break;
    case OP_FSQRT_FRN:
        if (PR_) sh4_setdr(c, N & 14, sqrt(sh4_getdr(c, N & 14))); else FR(c, N) = sqrtf(FR(c, N)); break;
    case OP_FSRRA_FRN: FR(c, N) = 1.0f / sqrtf(FR(c, N)); break;
    case OP_FLDI0_FRN: FRU(c, N) = 0; break;
    case OP_FLDI1_FRN: FRU(c, N) = 0x3f800000u; break;
    case OP_FIPR_FVM_FVN: op_fipr(c, (op >> 10) & 3, (op >> 8) & 3); break;
    case OP_STC_RM_BANK_RN: R[N] = c->rbank[(op >> 4) & 7]; break;
    case OP_LDC_RN_RM_BANK: c->rbank[(op >> 4) & 7] = R[N]; break;
    case OP_LDC_L_AT_RN_INC_RM_BANK: c->rbank[(op >> 4) & 7] = bus_r32(c, R[N]); R[N] += 4; break;
    case OP_STC_L_RM_BANK_PREDEC_RN: R[N] -= 4; bus_w32(c, R[N], c->rbank[(op >> 4) & 7]); break;
    case OP_MOV_B_RM_AT_R0_RN: bus_w8(c, R[N] + R[0], (u8)R[M]); break;
    case OP_MOV_W_RM_AT_R0_RN: bus_w16(c, R[N] + R[0], (u16)R[M]); break;
    case OP_MOV_L_RM_AT_R0_RN: bus_w32(c, R[N] + R[0], R[M]); break;
    case OP_MUL_L_RM_RN: c->macl = R[N] * R[M]; break;
    case OP_MOV_B_AT_R0_RM_RN: R[N] = (u32)(s8)bus_r8(c, R[M] + R[0]); break;
    case OP_MOV_W_AT_R0_RM_RN: R[N] = (u32)(s16)bus_r16(c, R[M] + R[0]); break;
    case OP_MOV_L_AT_R0_RM_RN: R[N] = bus_r32(c, R[M] + R[0]); break;
    case OP_MAC_L_AT_RM_INC_AT_RN_INC: op_macl(c, N, M); break;
    case OP_MOV_B_RM_AT_RN: bus_w8(c, R[N], (u8)R[M]); break;
    case OP_MOV_W_RM_AT_RN: bus_w16(c, R[N], (u16)R[M]); break;
    case OP_MOV_L_RM_AT_RN: bus_w32(c, R[N], R[M]); break;
    case OP_MOV_B_RM_PREDEC_RN: t = R[M]; R[N] -= 1; bus_w8(c, R[N], (u8)t); break;
    case OP_MOV_W_RM_PREDEC_RN: t = R[M]; R[N] -= 2; bus_w16(c, R[N], (u16)t); break;
    case OP_MOV_L_RM_PREDEC_RN: t = R[M]; R[N] -= 4; bus_w32(c, R[N], t); break;
    case OP_DIV0S_RM_RN: {
        u32 q = R[N] >> 31, m = R[M] >> 31;
        c->sr = (c->sr & ~(SR_Q | SR_M)) | (q ? SR_Q : 0) | (m ? SR_M : 0); T_SET(c, q ^ m); } break;
    case OP_TST_RM_RN: T_SET(c, (R[N] & R[M]) == 0); break;
    case OP_AND_RM_RN: R[N] &= R[M]; break;
    case OP_XOR_RM_RN: R[N] ^= R[M]; break;
    case OP_OR_RM_RN: R[N] |= R[M]; break;
    case OP_CMP_STR_RM_RN: t = R[N] ^ R[M];
        T_SET(c, !(t & 0xff000000) || !(t & 0xff0000) || !(t & 0xff00) || !(t & 0xff)); break;
    case OP_XTRCT_RM_RN: R[N] = (R[N] >> 16) | (R[M] << 16); break;
    case OP_MULU_W_RM_RN: c->macl = (u32)(u16)R[N] * (u32)(u16)R[M]; break;
    case OP_MULS_W_RM_RN: c->macl = (u32)((s32)(s16)R[N] * (s32)(s16)R[M]); break;
    case OP_CMP_EQ_RM_RN: T_SET(c, R[N] == R[M]); break;
    case OP_CMP_HS_RM_RN: T_SET(c, R[N] >= R[M]); break;
    case OP_CMP_GE_RM_RN: T_SET(c, (s32)R[N] >= (s32)R[M]); break;
    case OP_DIV1_RM_RN: op_div1(c, N, M); break;
    case OP_DMULU_L_RM_RN: { u64 r = (u64)R[N] * R[M]; c->mach = (u32)(r >> 32); c->macl = (u32)r; } break;
    case OP_CMP_HI_RM_RN: T_SET(c, R[N] > R[M]); break;
    case OP_CMP_GT_RM_RN: T_SET(c, (s32)R[N] > (s32)R[M]); break;
    case OP_SUB_RM_RN: R[N] -= R[M]; break;
    case OP_SUBC_RM_RN: { u32 a = R[N], b = R[M], tt = T_GET(c); u32 r = a - b - tt;
        T_SET(c, ((u64)a < (u64)b + tt)); R[N] = r; } break;
    case OP_SUBV_RM_RN: { s32 a = R[N], b = R[M]; s64 r = (s64)a - b; R[N] = (u32)r; T_SET(c, r != (s32)r); } break;
    case OP_ADD_RM_RN: R[N] += R[M]; break;
    case OP_DMULS_L_RM_RN: { s64 r = (s64)(s32)R[N] * (s32)R[M]; c->mach = (u32)((u64)r >> 32); c->macl = (u32)r; } break;
    case OP_ADDC_RM_RN: { u64 r = (u64)R[N] + R[M] + T_GET(c); R[N] = (u32)r; T_SET(c, r >> 32); } break;
    case OP_ADDV_RM_RN: { s64 r = (s64)(s32)R[N] + (s32)R[M]; R[N] = (u32)r; T_SET(c, r != (s32)r); } break;
    case OP_SHAD_RM_RN: { s32 s = (s32)R[M];
        if (s >= 0) R[N] <<= (s & 31);
        else if ((s & 31) == 0) R[N] = ((s32)R[N] < 0) ? 0xffffffffu : 0;
        else R[N] = (u32)((s32)R[N] >> ((~s & 31) + 1)); } break;
    case OP_SHLD_RM_RN: { s32 s = (s32)R[M];
        if (s >= 0) R[N] <<= (s & 31);
        else if ((s & 31) == 0) R[N] = 0;
        else R[N] >>= ((~s & 31) + 1); } break;
    case OP_MAC_W_AT_RM_INC_AT_RN_INC: op_macw(c, N, M); break;
    case OP_MOV_B_AT_RM_RN: R[N] = (u32)(s8)bus_r8(c, R[M]); break;
    case OP_MOV_W_AT_RM_RN: R[N] = (u32)(s16)bus_r16(c, R[M]); break;
    case OP_MOV_L_AT_RM_RN: R[N] = bus_r32(c, R[M]); break;
    case OP_MOV_RM_RN: R[N] = R[M]; break;
    case OP_MOV_B_AT_RM_INC_RN: t = (u32)(s8)bus_r8(c, R[M]); if (N != M) R[M] += 1; R[N] = t; break;
    case OP_MOV_W_AT_RM_INC_RN: t = (u32)(s16)bus_r16(c, R[M]); if (N != M) R[M] += 2; R[N] = t; break;
    case OP_MOV_L_AT_RM_INC_RN: t = bus_r32(c, R[M]); if (N != M) R[M] += 4; R[N] = t; break;
    case OP_NOT_RM_RN: R[N] = ~R[M]; break;
    case OP_SWAP_B_RM_RN: R[N] = (R[M] & 0xffff0000) | ((R[M] >> 8) & 0xff) | ((R[M] & 0xff) << 8); break;
    case OP_SWAP_W_RM_RN: R[N] = (R[M] >> 16) | (R[M] << 16); break;
    case OP_NEGC_RM_RN: { u32 tt = T_GET(c), mm = R[M]; u32 r = 0 - mm - tt; T_SET(c, (mm | tt) != 0 && !(mm == 0 && tt == 0));
        T_SET(c, (u64)mm + tt > 0); R[N] = r; } break;
    case OP_NEG_RM_RN: R[N] = 0 - R[M]; break;
    case OP_EXTU_B_RM_RN: R[N] = R[M] & 0xff; break;
    case OP_EXTU_W_RM_RN: R[N] = R[M] & 0xffff; break;
    case OP_EXTS_B_RM_RN: R[N] = (u32)(s8)R[M]; break;
    case OP_EXTS_W_RM_RN: R[N] = (u32)(s16)R[M]; break;
    case OP_MOV_B_R0_AT_DISP_RN: bus_w8(c, R[M] + D4, (u8)R[0]); break;
    case OP_MOV_W_R0_AT_DISP_RN: bus_w16(c, R[M] + D4 * 2, (u16)R[0]); break;
    case OP_MOV_B_AT_DISP_RM_R0: R[0] = (u32)(s8)bus_r8(c, R[M] + D4); break;
    case OP_MOV_W_AT_DISP_RM_R0: R[0] = (u32)(s16)bus_r16(c, R[M] + D4 * 2); break;
    case OP_CMP_EQ_IMM_IMM_R0: T_SET(c, R[0] == (u32)SI8); break;
    case OP_MOV_B_R0_AT_DISP_GBR: bus_w8(c, c->gbr + I8, (u8)R[0]); break;
    case OP_MOV_W_R0_AT_DISP_GBR: bus_w16(c, c->gbr + I8 * 2, (u16)R[0]); break;
    case OP_MOV_L_R0_AT_DISP_GBR: bus_w32(c, c->gbr + I8 * 4, R[0]); break;
    case OP_TRAPA_IMM_IMM: sh4_trap(c, I8, pc); break;
    case OP_MOV_B_AT_DISP_GBR_R0: R[0] = (u32)(s8)bus_r8(c, c->gbr + I8); break;
    case OP_MOV_W_AT_DISP_GBR_R0: R[0] = (u32)(s16)bus_r16(c, c->gbr + I8 * 2); break;
    case OP_MOV_L_AT_DISP_GBR_R0: R[0] = bus_r32(c, c->gbr + I8 * 4); break;
    case OP_MOVA_AT_DISP_PC_R0: R[0] = (pc & ~3u) + 4 + I8 * 4; break;
    case OP_TST_IMM_IMM_R0: T_SET(c, (R[0] & I8) == 0); break;
    case OP_AND_IMM_IMM_R0: R[0] &= I8; break;
    case OP_XOR_IMM_IMM_R0: R[0] ^= I8; break;
    case OP_OR_IMM_IMM_R0: R[0] |= I8; break;
    case OP_TST_B_IMM_IMM_AT_R0_GBR: T_SET(c, (bus_r8(c, c->gbr + R[0]) & I8) == 0); break;
    case OP_AND_B_IMM_IMM_AT_R0_GBR: t = c->gbr + R[0]; bus_w8(c, t, bus_r8(c, t) & I8); break;
    case OP_XOR_B_IMM_IMM_AT_R0_GBR: t = c->gbr + R[0]; bus_w8(c, t, bus_r8(c, t) ^ I8); break;
    case OP_OR_B_IMM_IMM_AT_R0_GBR: t = c->gbr + R[0]; bus_w8(c, t, bus_r8(c, t) | I8); break;
    /* ---- FPU arithmetic ---- */
    case OP_FADD_FRM_FRN: if (PR_) sh4_setdr(c, N & 14, sh4_getdr(c, N & 14) + sh4_getdr(c, M & 14)); else FR(c, N) += FR(c, M); break;
    case OP_FSUB_FRM_FRN: if (PR_) sh4_setdr(c, N & 14, sh4_getdr(c, N & 14) - sh4_getdr(c, M & 14)); else FR(c, N) -= FR(c, M); break;
    case OP_FMUL_FRM_FRN: if (PR_) sh4_setdr(c, N & 14, sh4_getdr(c, N & 14) * sh4_getdr(c, M & 14)); else FR(c, N) *= FR(c, M); break;
    case OP_FDIV_FRM_FRN: if (PR_) sh4_setdr(c, N & 14, sh4_getdr(c, N & 14) / sh4_getdr(c, M & 14)); else FR(c, N) /= FR(c, M); break;
    case OP_FCMP_EQ_FRM_FRN: T_SET(c, PR_ ? sh4_getdr(c, N & 14) == sh4_getdr(c, M & 14) : FR(c, N) == FR(c, M)); break;
    case OP_FCMP_GT_FRM_FRN: T_SET(c, PR_ ? sh4_getdr(c, N & 14) > sh4_getdr(c, M & 14) : FR(c, N) > FR(c, M)); break;
    case OP_FMAC_FR0_FRM_FRN: FR(c, N) = FR(c, 0) * FR(c, M) + FR(c, N); break;
    /* ---- FPU moves ---- */
    case OP_FMOV_FRM_FRN:
        if (SZ_) { u32 *d = fpair(c, N), *s = fpair(c, M); d[0] = s[0]; d[1] = s[1]; }
        else FRU(c, N) = FRU(c, M); break;
    case OP_FMOV_S_AT_RM_FRN:
        if (SZ_) { u32 *d = fpair(c, N); d[0] = bus_r32(c, R[M]); d[1] = bus_r32(c, R[M] + 4); }
        else FRU(c, N) = bus_r32(c, R[M]); break;
    case OP_FMOV_S_AT_R0_RM_FRN:
        if (SZ_) { u32 *d = fpair(c, N); d[0] = bus_r32(c, R[M] + R[0]); d[1] = bus_r32(c, R[M] + R[0] + 4); }
        else FRU(c, N) = bus_r32(c, R[M] + R[0]); break;
    case OP_FMOV_S_AT_RM_INC_FRN:
        if (SZ_) { u32 *d = fpair(c, N); d[0] = bus_r32(c, R[M]); d[1] = bus_r32(c, R[M] + 4); R[M] += 8; }
        else { FRU(c, N) = bus_r32(c, R[M]); R[M] += 4; } break;
    case OP_FMOV_S_FRM_AT_RN:
        if (SZ_) { u32 *s = fpair(c, M); bus_w32(c, R[N], s[0]); bus_w32(c, R[N] + 4, s[1]); }
        else bus_w32(c, R[N], FRU(c, M)); break;
    case OP_FMOV_S_FRM_AT_R0_RN:
        if (SZ_) { u32 *s = fpair(c, M); bus_w32(c, R[N] + R[0], s[0]); bus_w32(c, R[N] + R[0] + 4, s[1]); }
        else bus_w32(c, R[N] + R[0], FRU(c, M)); break;
    case OP_FMOV_S_FRM_PREDEC_RN:
        if (SZ_) { u32 *s = fpair(c, M); R[N] -= 8; bus_w32(c, R[N], s[0]); bus_w32(c, R[N] + 4, s[1]); }
        else { R[N] -= 4; bus_w32(c, R[N], FRU(c, M)); } break;
    /* ---- data with PC/imm ---- */
    case OP_MOV_L_RM_AT_DISP_RN: bus_w32(c, R[N] + D4 * 4, R[M]); break;
    case OP_MOV_L_AT_DISP_RM_RN: R[N] = bus_r32(c, R[M] + D4 * 4); break;
    case OP_ADD_IMM_IMM_RN: R[N] += (u32)SI8; break;
    case OP_MOV_W_AT_DISP_PC_RN: R[N] = (u32)(s16)bus_r16(c, pc + 4 + I8 * 2); break;
    case OP_MOV_L_AT_DISP_PC_RN: R[N] = bus_r32(c, (pc & ~3u) + 4 + I8 * 4); break;
    case OP_MOV_IMM_IMM_RN: R[N] = (u32)SI8; break;
    default:
        sh4_illegal(c, pc, op);
    }
}

