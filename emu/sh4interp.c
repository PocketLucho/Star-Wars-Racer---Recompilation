/* SH-4 reference interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include "sh4.h"
#include "sh4exec.h"

u8 sh4_optab[65536];
#define optab sh4_optab

void sh4_init_tables(void) {
    int np = sizeof(sh4_patterns) / sizeof(sh4_patterns[0]);
    for (int op = 0; op < 65536; op++) {
        optab[op] = OP_ILLEGAL;
        for (int i = 0; i < np; i++)
            if ((op & sh4_patterns[i].mask) == sh4_patterns[i].val) { optab[op] = (u8)sh4_patterns[i].id; break; }
    }
}
int sh4_opid(u16 op) { return optab[op]; }
const char *sh4_opname(u16 op) { return sh4_opnames[optab[op]]; }

void sh4_reset(Cpu *c) {
    memset(c->r, 0, sizeof c->r);
    c->sr = SR_MD | SR_RB | SR_BL | SR_IMASK;
    c->vbr = 0; c->pc = 0xa0000000; c->fpscr = 0x00040001; c->fb = 0;
    c->sleeping = 0;
}

u16 sh4_fetch(Cpu *c, u32 pc);

/* execute one instruction (plus delay slot). */
void sh4_step(Cpu *c) {
    u32 pc = c->pc;
    u16 op = sh4_fetch(c, pc);
    u32 tgt; int d;
    switch (optab[op]) {
    case OP_BT_LABEL: c->pc = T_GET(c) ? pc + 4 + SI8 * 2 : pc + 2; c->cycles++; return;
    case OP_BF_LABEL: c->pc = !T_GET(c) ? pc + 4 + SI8 * 2 : pc + 2; c->cycles++; return;
    case OP_BT_S_LABEL: tgt = T_GET(c) ? pc + 4 + SI8 * 2 : pc + 4; goto delay;
    case OP_BF_S_LABEL: tgt = !T_GET(c) ? pc + 4 + SI8 * 2 : pc + 4; goto delay;
    case OP_BRA_LABEL: d = op & 0xfff; if (d & 0x800) d -= 0x1000; tgt = pc + 4 + d * 2; goto delay;
    case OP_BSR_LABEL: d = op & 0xfff; if (d & 0x800) d -= 0x1000; tgt = pc + 4 + d * 2; c->pr = pc + 4; goto delay;
    case OP_BRAF_RN: tgt = pc + 4 + c->r[N]; goto delay;
    case OP_BSRF_RN: tgt = pc + 4 + c->r[N]; c->pr = pc + 4; goto delay;
    case OP_JMP_AT_RN: tgt = c->r[N]; goto delay;
    case OP_JSR_AT_RN: tgt = c->r[N]; c->pr = pc + 4; goto delay;
    case OP_RTS: tgt = c->pr; goto delay;
    case OP_RTE: {
        tgt = c->spc;
        u32 nsr = c->ssr;
        /* delay slot executes with the restored SR per manual */
        sh4_set_sr(c, nsr);
        u16 sop = sh4_fetch(c, pc + 2);
        exec_plain(c, sop, pc + 2);
        c->pc = tgt; c->cycles += 2; return; }
    default:
        exec_plain(c, op, pc);
        if (c->pc == pc) c->pc = pc + 2;     /* exceptions may have changed pc */
        c->cycles++;
        return;
    }
delay: {
        u16 sop = sh4_fetch(c, pc + 2);
        c->pc = pc + 2;
        exec_plain(c, sop, pc + 2);
        c->pc = tgt; c->cycles += 2;
    }
}

/* raise exception/interrupt: code = EXPEVT/INTEVT, vector offset */
void sh4_exception(Cpu *c, u32 evt, u32 vecoff, u32 retpc) {
    c->spc = retpc;
    c->ssr = c->sr;
    c->sgr = c->r[15];
    sh4_set_sr(c, c->sr | SR_MD | SR_RB | SR_BL);
    c->pc = c->vbr + vecoff;
    (void)evt;
}
