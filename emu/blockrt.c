#include <stdlib.h>
#include <string.h>
#include "blockrt.h"
#include "optable.h"

extern u8 sh4_optab[65536];
void sh4_step(Cpu *c);
u16 sh4_fetch_raw(Cpu *c, u32 pc);

/* ---------------- block boundary rule ---------------- */
int sh4_is_block_end(u16 op) {
    switch (sh4_optab[op]) {
    case OP_BT_LABEL: case OP_BF_LABEL: case OP_BT_S_LABEL: case OP_BF_S_LABEL:
    case OP_BRA_LABEL: case OP_BSR_LABEL: case OP_BRAF_RN: case OP_BSRF_RN:
    case OP_JMP_AT_RN: case OP_JSR_AT_RN: case OP_RTS:
        return 1;
    }
    return 0;
}
int sh4_is_interp_only(u16 op) {
    int id = sh4_optab[op];
    return id == OP_RTE || id == OP_TRAPA_IMM_IMM || id == OP_SLEEP || id == OP_ILLEGAL;
}

/* ---------------- machine binding ----------------
 * the owning machine sets these: RAM of each CPU (seen from 0x0C000000), its size mask and a log file.
 * Hikaru: 32 MB per CPU (mask 0x01FFFFFF). Dreamcast: 16 MB, mirrored (mask 0x00FFFFFF). */
u8 *g_blk_ram[2];
u32 g_blk_ram_mask = 0x01ffffff;
FILE *g_blk_log;

/* ---------------- block table ----------------
 * T_/TN_: the table in use. It comes from the game module (swr_game.dll / .so, built on the user's machine
 * from their own ROM, see builder/) when one was loaded with blk_module_load(); otherwise from the table
 * linked into the program (blk_stub.c = empty: interpreter only; or gen/blk_table.c in developer builds). */
extern const BlkEntry blk_table[]; extern const int blk_count;
static const BlkEntry *T_; static int TN_;
static const BlkEntry *mod_table; static int mod_count = -1;
void blk_use_table(const BlkEntry *t, int n) { mod_table = t; mod_count = n; }
int blk_table_size(void) { return TN_; }

/* ---------------- state ---------------- */
static int aot_on, verify_on;
static FILE *miss_f, *seen_f;
#define RAM_PAGES (0x2000000 >> 12)
static u32 pgen[2][RAM_PAGES];
static u8 codepage[2][RAM_PAGES];
static u32 *vgen[2];              /* per table entry: validated generation + 1 (per CPU) */
static int *hmap; static u32 hmask;  /* pc -> first table index */
static u8 *missed[2];             /* 1 bit per halfword of RAM: already reported */
static u8 *seen[2];
static u64 st_aot[2], st_interp[2], st_hashchk, st_hashfail, st_verify, st_vfail;

static u32 hpc(u32 pc) { return (pc * 2654435761u) >> 8; }

void blk_init(int aot_enabled, int verify, const char *miss_path, const char *seen_path) {
    if (mod_count >= 0) { T_ = mod_table; TN_ = mod_count; } else { T_ = blk_table; TN_ = blk_count; }
    aot_on = aot_enabled && TN_ > 0; verify_on = verify;
    for (int i = 0; i < 2; i++) {
        vgen[i] = calloc(TN_ ? TN_ : 1, sizeof(u32));
        missed[i] = calloc(0x2000000 / 16, 1); seen[i] = calloc(0x2000000 / 16, 1);
    }
    u32 sz = 1; while (sz < (u32)TN_ * 2 + 16) sz <<= 1;
    hmask = sz - 1; hmap = malloc(sz * sizeof(int)); for (u32 i = 0; i < sz; i++) hmap[i] = -1;
    for (int i = 0; i < TN_; i++) {
        if (i && T_[i - 1].pc == T_[i].pc) continue;
        u32 h = hpc(T_[i].pc) & hmask;
        while (hmap[h] >= 0) h = (h + 1) & hmask;
        hmap[h] = i;
    }
    if (miss_path) miss_f = fopen(miss_path, "w");
    if (seen_path) seen_f = fopen(seen_path, "w");
}

static int find_first(u32 p) {
    u32 h = hpc(p) & hmask;
    while (hmap[h] >= 0) { if (T_[hmap[h]].pc == p) return hmap[h]; h = (h + 1) & hmask; }
    return -1;
}

void blk_ram_written(int cpu, u32 off, u32 len) {
    u32 a = off >> 12, b = (off + (len ? len - 1 : 0)) >> 12;
    for (u32 p = a; p <= b && p < RAM_PAGES; p++)
        if (codepage[cpu][p]) { pgen[cpu][p]++; codepage[cpu][p] = 0; }
}

static u32 fnv1a(const u8 *p, u32 n) { u32 h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }

/* is entry i valid for this CPU's current RAM? */
static int validate(int cpu, int i) {
    const BlkEntry *e = &T_[i];
    u32 off = (e->pc - 0x0c000000) & g_blk_ram_mask, p0 = off >> 12, p1 = (off + e->size - 1) >> 12;
    u32 g = pgen[cpu][p0] + (pgen[cpu][p1] << 16) + 1;
    if (vgen[cpu][i] == g && codepage[cpu][p0] && codepage[cpu][p1]) return 1;
    st_hashchk++;
    if (fnv1a(g_blk_ram[cpu] + off, e->size) != e->hash) { st_hashfail++; vgen[cpu][i] = 0; return 0; }
    codepage[cpu][p0] = codepage[cpu][p1] = 1;
    vgen[cpu][i] = pgen[cpu][p0] + (pgen[cpu][p1] << 16) + 1;
    return 1;
}

static BlkFn lookup(Cpu *c, u32 p) {
    if (p < 0x0c000000 || p >= 0x0e000000) return 0;
    int i = find_first(p);
    if (i < 0) return 0;
    for (; i < TN_ && T_[i].pc == p; i++)
        if (validate(c->id, i)) return T_[i].fn;
    return 0;
}

/* interpreter with exactly the AOT block boundaries */
static void interp_block(Cpu *c) {
    for (int k = 0; k < BLK_MAX_INSNS; k++) {
        u32 pc = c->pc;
        u16 op = sh4_fetch_raw(c, pc);
        if (sh4_is_interp_only(op)) { if (k == 0) sh4_step(c); return; }
        sh4_step(c);
        if (sh4_is_block_end(op)) return;
        if (c->pc != pc + 2) return;        /* exception / sleep */
        if (c->sleeping) return;
    }
}

/* ---------------- lockstep ---------------- */
typedef struct { u32 a, v; u8 sz, wr; } JEnt;
static JEnt *jbuf; static int jcount, jpos, jcap, jerr;
int g_jmode;
void jr_record(u32 a, int sz, u32 v, int wr) {
    if (jcount == jcap) { jcap = jcap ? jcap * 2 : 4096; jbuf = realloc(jbuf, jcap * sizeof(JEnt)); }
    jbuf[jcount].a = a; jbuf[jcount].v = v; jbuf[jcount].sz = (u8)sz; jbuf[jcount].wr = (u8)wr; jcount++;
}
u32 jr_replay_read(u32 a, int sz) {
    if (jpos >= jcount || jbuf[jpos].wr || jbuf[jpos].a != a || jbuf[jpos].sz != sz) { jerr++; return 0; }
    return jbuf[jpos++].v;
}
void jr_replay_write(u32 a, int sz, u32 v) {
    if (jpos >= jcount || !jbuf[jpos].wr || jbuf[jpos].a != a || jbuf[jpos].sz != sz || jbuf[jpos].v != v) { jerr++; return; }
    jpos++;
}

typedef struct {
    u32 r[16], rbank[8], sr, gbr, vbr, ssr, spc, sgr, dbr, mach, macl, pr, pc, fpscr, fpul;
    u32 fpr[2][16]; int fb; u32 sq[2][8]; int sleeping; u64 cycles;
} Arch;
static void arch_get(Cpu *c, Arch *a) {
    memset(a, 0, sizeof *a);
    memcpy(a->r, c->r, sizeof a->r); memcpy(a->rbank, c->rbank, sizeof a->rbank);
    a->sr = c->sr; a->gbr = c->gbr; a->vbr = c->vbr; a->ssr = c->ssr; a->spc = c->spc; a->sgr = c->sgr; a->dbr = c->dbr;
    a->mach = c->mach; a->macl = c->macl; a->pr = c->pr; a->pc = c->pc; a->fpscr = c->fpscr; a->fpul = c->fpul;
    memcpy(a->fpr, c->fpr.u, sizeof a->fpr); a->fb = c->fb; memcpy(a->sq, c->sq, sizeof a->sq);
    a->sleeping = c->sleeping; a->cycles = c->cycles;
}
static void arch_set(Cpu *c, const Arch *a) {
    memcpy(c->r, a->r, sizeof a->r); memcpy(c->rbank, a->rbank, sizeof a->rbank);
    c->sr = a->sr; c->gbr = a->gbr; c->vbr = a->vbr; c->ssr = a->ssr; c->spc = a->spc; c->sgr = a->sgr; c->dbr = a->dbr;
    c->mach = a->mach; c->macl = a->macl; c->pr = a->pr; c->pc = a->pc; c->fpscr = a->fpscr; c->fpul = a->fpul;
    memcpy(c->fpr.u, a->fpr, sizeof a->fpr); c->fb = a->fb; memcpy(c->sq, a->sq, sizeof a->sq);
    c->sleeping = a->sleeping; c->cycles = a->cycles;
}
static void arch_diff(FILE *f, const Arch *x, const Arch *y) {
    for (int i = 0; i < 16; i++) if (x->r[i] != y->r[i]) fprintf(f, "  r%d interp=%08x aot=%08x\n", i, x->r[i], y->r[i]);
    for (int i = 0; i < 8; i++) if (x->rbank[i] != y->rbank[i]) fprintf(f, "  rbank%d %08x %08x\n", i, x->rbank[i], y->rbank[i]);
#define D(fl) if (x->fl != y->fl) fprintf(f, "  " #fl " interp=%08llx aot=%08llx\n", (unsigned long long)x->fl, (unsigned long long)y->fl);
    D(sr) D(gbr) D(vbr) D(ssr) D(spc) D(sgr) D(dbr) D(mach) D(macl) D(pr) D(pc) D(fpscr) D(fpul) D(fb) D(sleeping) D(cycles)
#undef D
    for (int b = 0; b < 2; b++) for (int i = 0; i < 16; i++) if (x->fpr[b][i] != y->fpr[b][i]) fprintf(f, "  fr%d.%d %08x %08x\n", b, i, x->fpr[b][i], y->fpr[b][i]);
    if (memcmp(x->sq, y->sq, sizeof x->sq)) fprintf(f, "  store queues differ\n");
}

static void run_verified(Cpu *c, BlkFn fn) {
    Arch before, ref, got;
    arch_get(c, &before);
    jcount = 0; g_jmode = 1; interp_block(c); g_jmode = 0;
    arch_get(c, &ref);
    arch_set(c, &before);
    jpos = 0; jerr = 0; g_jmode = 2; fn(c); g_jmode = 0;
    arch_get(c, &got);
    st_verify++;
    if (memcmp(&ref, &got, sizeof ref) || jerr || jpos != jcount) {
        st_vfail++;
        if (g_blk_log && st_vfail < 200) {
            fprintf(g_blk_log, "VERIFY MISMATCH cpu%d block %08x (journal %d/%d, errors %d)\n", c->id, before.pc, jpos, jcount, jerr);
            arch_diff(g_blk_log, &ref, &got);
        }
    }
    arch_set(c, &ref);            /* continue on the interpreter's (reference) result */
}

void blk_step(Cpu *c) {
    u32 p = c->pc & 0x1fffffff;
    if (seen_f && p >= 0x0c000000 && p < 0x0e000000) {
        u32 o = (p - 0x0c000000) >> 1;
        if (!(seen[c->id][o >> 3] & (1 << (o & 7)))) { seen[c->id][o >> 3] |= 1 << (o & 7); fprintf(seen_f, "%d %08x\n", c->id, p); }
    }
    BlkFn fn = aot_on ? lookup(c, p) : 0;
    if (fn) {
        st_aot[c->id]++;
        if (verify_on) run_verified(c, fn); else fn(c);
        return;
    }
    st_interp[c->id]++;
    if (aot_on && miss_f && p >= 0x0c000000 && p < 0x0e000000 && !sh4_is_interp_only(sh4_fetch_raw(c, c->pc))) {
        u32 o = (p - 0x0c000000) >> 1;
        if (!(missed[c->id][o >> 3] & (1 << (o & 7)))) {
            missed[c->id][o >> 3] |= 1 << (o & 7);
            /* include the guest bytes as they are now, so the generator can compile this version */
            u32 off = p - 0x0c000000, n = 2 * BLK_MAX_INSNS + 4;
            if (off + n > 0x2000000) n = 0x2000000 - off;
            fprintf(miss_f, "%d %08x ", c->id, p);
            for (u32 k = 0; k < n; k++) fprintf(miss_f, "%02x", g_blk_ram[c->id][(off + k) & g_blk_ram_mask]);
            fprintf(miss_f, "\n");
        }
    }
    interp_block(c);
}

void blk_stats(FILE *f) {
    fprintf(f, "blocks: table=%d aot=%s verify=%s\n", TN_, aot_on ? "on" : "off", verify_on ? "on" : "off");
    for (int i = 0; i < 2; i++) fprintf(f, "  cpu%d: aot blocks run=%llu interpreted blocks=%llu\n", i, (unsigned long long)st_aot[i], (unsigned long long)st_interp[i]);
    fprintf(f, "  hash checks=%llu failed=%llu", (unsigned long long)st_hashchk, (unsigned long long)st_hashfail);
    if (verify_on) fprintf(f, "  verified=%llu mismatches=%llu", (unsigned long long)st_verify, (unsigned long long)st_vfail);
    fprintf(f, "\n");
}
void blk_finish(void) { if (miss_f) fclose(miss_f); if (seen_f) fclose(seen_f); }
