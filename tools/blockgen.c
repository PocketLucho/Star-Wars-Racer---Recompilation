/* SH-4 block generator (AOT): C port of blockgen.py, so the game builder only needs a C compiler.
 * One C function per basic block, each tagged with the FNV-1a hash of its guest bytes.
 *
 * usage: blockgen -o DIR -n UNITS --image CPU:FILE:HEXBASE [...] --seeds FILE [...] [--no-explore]
 *   images: RAM snapshots (cpu, file, base address in the 0x0C000000 view)
 *   seeds:  text lines "cpu hexpc [hexbytes]" (entry points; optional bytes = a mini image of that block)
 * Block boundaries must match emu/blockrt.c (interp_block): a block ends after a branch (+ delay slot) or
 * bt/bf, or after 256 instructions; rte / trapa / sleep / illegal never go in a block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "blockgen_ops.h"

typedef uint32_t u32; typedef uint8_t u8;
enum { K_OTHER, K_BT, K_BF, K_BTS, K_BFS, K_BRA, K_BSR, K_BRAF, K_BSRF, K_JMP, K_JSR, K_RTS, K_INTERP };
static short opid[65536]; static u8 opkind[65536];
static int kind_of(const char *n) {
    static const struct { const char *n; int k; } t[] = { {"OP_BT_LABEL", K_BT}, {"OP_BF_LABEL", K_BF}, {"OP_BT_S_LABEL", K_BTS},
        {"OP_BF_S_LABEL", K_BFS}, {"OP_BRA_LABEL", K_BRA}, {"OP_BSR_LABEL", K_BSR}, {"OP_BRAF_RN", K_BRAF}, {"OP_BSRF_RN", K_BSRF},
        {"OP_JMP_AT_RN", K_JMP}, {"OP_JSR_AT_RN", K_JSR}, {"OP_RTS", K_RTS}, {"OP_RTE", K_INTERP}, {"OP_TRAPA_IMM_IMM", K_INTERP},
        {"OP_SLEEP", K_INTERP} };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(n, t[i].n)) return t[i].k;
    return K_OTHER;
}
static const char *sem(u32 op) { static char b[80]; int i = opid[op]; if (i < 0 || !OPS[i].has_sem) return "SEM_OP_ILLEGAL"; snprintf(b, sizeof b, "SEM_%s", OPS[i].name); return b; }
static int s8(u32 v) { return v & 0x80 ? (int)v - 256 : (int)v; }
static int s12(u32 v) { return v & 0x800 ? (int)v - 0x1000 : (int)v; }

typedef struct { int cpu; u32 base, len; u8 *d; } Image;
static Image *img; static int nimg, capimg;
static void add_image(int cpu, u32 base, u8 *d, u32 len) {
    if (nimg == capimg) { capimg = capimg ? capimg * 2 : 64; img = realloc(img, capimg * sizeof *img); }
    img[nimg++] = (Image){ cpu, base, len, d };
}
typedef struct { int cpu; u32 pc; } Seed;
static Seed *seed; static int nseed, capseed;

typedef struct { u32 pc, h, size; size_t off; } Blk;
static Blk *blk; static int nblk, capblk;
static char *text; static size_t ntext, captext;
static int *htab; static u32 hmask;
static void emit(const char *fmt, ...);
#include <stdarg.h>
static void emit(const char *fmt, ...) {
    if (captext - ntext < 1024) { captext = captext ? captext * 2 : 1 << 20; text = realloc(text, captext); }
    va_list ap; va_start(ap, fmt); ntext += vsnprintf(text + ntext, captext - ntext, fmt, ap); va_end(ap);
}
static int find(u32 pc, u32 h) {
    u32 k = (pc * 2654435761u ^ h) & hmask;
    while (htab[k] >= 0) { if (blk[htab[k]].pc == pc && blk[htab[k]].h == h) return 1; k = (k + 1) & hmask; }
    return 0;
}
static void rehash(void) {
    u32 sz = 1024; while (sz < (u32)capblk * 4) sz <<= 1;
    free(htab); htab = malloc(sz * sizeof *htab); hmask = sz - 1; memset(htab, 0xff, sz * sizeof *htab);
    for (int i = 0; i < nblk; i++) { u32 k = (blk[i].pc * 2654435761u ^ blk[i].h) & hmask; while (htab[k] >= 0) k = (k + 1) & hmask; htab[k] = i; }
}
static void insert(u32 pc, u32 h, u32 size, size_t off) {
    if (nblk == capblk) { capblk = capblk ? capblk * 2 : 4096; blk = realloc(blk, capblk * sizeof *blk); blk[nblk] = (Blk){ pc, h, size, off }; nblk++; rehash(); return; }
    blk[nblk] = (Blk){ pc, h, size, off };
    u32 k = (pc * 2654435761u ^ h) & hmask; while (htab[k] >= 0) k = (k + 1) & hmask; htab[k] = nblk++;
}
static int cmp_blk(const void *a, const void *b) { const Blk *x = a, *y = b; return x->pc != y->pc ? (x->pc < y->pc ? -1 : 1) : x->h != y->h ? (x->h < y->h ? -1 : 1) : 0; }
static int cmp_u32(const void *a, const void *b) { u32 x = *(const u32 *)a, y = *(const u32 *)b; return x < y ? -1 : x > y; }

static int explore = 1; static long counts[2];
static void process(const Image *im, const u32 *seeds, int ns) {
    u32 base = im->base, end = base + im->len;
    #define R16(a) ((a) >= base && (a) < end - 1 ? (int)(im->d[(a) - base] | im->d[(a) - base + 1] << 8) : -1)
    u8 *done = calloc(im->len / 16 + 2, 1);
    u32 *work = 0; int nw = 0, capw = 0;
    #define PUSH(v) do { if (nw == capw) { capw = capw ? capw * 2 : 1024; work = realloc(work, capw * sizeof *work); } work[nw++] = (v); } while (0)
    for (int i = 0; i < ns; i++) { u32 p = seeds[i]; if (p >= base && p < end && p >= 0x0c000000u && p < 0x0e000000u && !(p & 1)) PUSH(p); }
    qsort(work, nw, sizeof *work, cmp_u32);
    static u32 ia[260], io[260];
    while (nw) {
        u32 pc = work[--nw];
        u32 di = (pc - base) >> 1; if (done[di >> 3] & (1 << (di & 7))) continue; done[di >> 3] |= 1 << (di & 7);
        int n = 0, term = 0 /* 0 none, 1 stop, 2 branch */, bad = 0; u32 ta = 0, top = 0, succ[3]; int nsucc = 0, tk = K_OTHER;
        u32 a = pc;
        for (int k = 0; k < 256; k++) {
            int op = R16(a); if (op < 0) { bad = 1; break; }
            int kd = opid[op] < 0 ? K_INTERP : opkind[op];
            if (kd == K_INTERP) { if (k == 0) { bad = 1; break; } succ[nsucc++] = a; term = 1; ta = a; break; }
            ia[n] = a; io[n] = op; n++;
            if (kd != K_OTHER) {
                if (kd == K_BT || kd == K_BF) { succ[nsucc++] = a + 4 + s8(op & 0xff) * 2; succ[nsucc++] = a + 2; }
                else if (kd == K_BTS || kd == K_BFS) { succ[nsucc++] = a + 4 + s8(op & 0xff) * 2; succ[nsucc++] = a + 4; }
                else if (kd == K_BRA) succ[nsucc++] = a + 4 + s12(op & 0xfff) * 2;
                else if (kd == K_BSR) { succ[nsucc++] = a + 4 + s12(op & 0xfff) * 2; succ[nsucc++] = a + 4; }
                else if (kd == K_JSR || kd == K_BSRF) succ[nsucc++] = a + 4;
                if (kd != K_BT && kd != K_BF) { int sop = R16(a + 2); if (sop < 0) { bad = 1; break; } ia[n] = a + 2; io[n] = sop; n++; }
                term = 2; ta = a; top = op; tk = kd; break;
            }
            a += 2;
        }
        if (bad) continue;
        if (!term) { succ[nsucc++] = a; term = 1; ta = a; }
        u32 size = ia[n - 1] + 2 - pc, h = 2166136261u;
        for (u32 k = 0; k < size; k++) h = (h ^ im->d[pc - base + k]) * 16777619u;
        if (explore) for (int i = 0; i < nsucc; i++) { u32 s = succ[i]; if (s >= base && s < end && s >= 0x0c000000u && s < 0x0e000000u) { u32 d2 = (s - base) >> 1; if (!(done[d2 >> 3] & (1 << (d2 & 7)))) PUSH(s); } }
        if (find(pc, h)) continue;
        size_t off = ntext; int cyc = 0;
        emit("  const u32 seg = c->pc & 0xe0000000u;\n");
        int nbody = term == 1 ? n : (tk == K_BT || tk == K_BF) ? n - 1 : n - 2;
        for (int i = 0; i < nbody; i++) { emit("  %s(c, 0x%04x, seg | 0x%08xu);\n", sem(io[i]), io[i], ia[i]); cyc++; }
        if (term == 1) emit("  c->pc = seg | 0x%08xu;\n", ta);
        else {
            u32 op = top; a = ta; int rn = (op >> 8) & 15; char SL[200] = "";
            u32 tg8 = a + 4 + s8(op & 0xff) * 2, tg12 = a + 4 + s12(op & 0xfff) * 2;
            if (tk != K_BT && tk != K_BF) snprintf(SL, sizeof SL, "c->pc = seg | 0x%08xu; %s(c, 0x%04x, seg | 0x%08xu);", a + 2, sem(io[n - 1]), io[n - 1], ia[n - 1]);
            switch (tk) {
            case K_BT: emit("  c->pc = (c->sr & SR_T) ? (seg | 0x%08xu) : (seg | 0x%08xu);\n", tg8, a + 2); cyc += 1; break;
            case K_BF: emit("  c->pc = (c->sr & SR_T) ? (seg | 0x%08xu) : (seg | 0x%08xu);\n", a + 2, tg8); cyc += 1; break;
            case K_BTS: emit("  { u32 tg = (c->sr & SR_T) ? (seg | 0x%08xu) : (seg | 0x%08xu); %s c->pc = tg; }\n", tg8, a + 4, SL); cyc += 2; break;
            case K_BFS: emit("  { u32 tg = (c->sr & SR_T) ? (seg | 0x%08xu) : (seg | 0x%08xu); %s c->pc = tg; }\n", a + 4, tg8, SL); cyc += 2; break;
            case K_BRA: emit("  { %s c->pc = (seg | 0x%08xu); }\n", SL, tg12); cyc += 2; break;
            case K_BSR: emit("  { c->pr = (seg | 0x%08xu); %s c->pc = (seg | 0x%08xu); }\n", a + 4, SL, tg12); cyc += 2; break;
            case K_BRAF: emit("  { u32 tg = (seg | 0x%08xu) + c->r[%d]; %s c->pc = tg; }\n", a + 4, rn, SL); cyc += 2; break;
            case K_BSRF: emit("  { u32 tg = (seg | 0x%08xu) + c->r[%d]; c->pr = (seg | 0x%08xu); %s c->pc = tg; }\n", a + 4, rn, a + 4, SL); cyc += 2; break;
            case K_JMP: emit("  { u32 tg = c->r[%d]; %s c->pc = tg; }\n", rn, SL); cyc += 2; break;
            case K_JSR: emit("  { u32 tg = c->r[%d]; c->pr = (seg | 0x%08xu); %s c->pc = tg; }\n", rn, a + 4, SL); cyc += 2; break;
            case K_RTS: emit("  { u32 tg = c->pr; %s c->pc = tg; }\n", SL); cyc += 2; break;
            }
        }
        emit("  c->cycles += %d;\n", cyc); text[ntext++] = 0;
        insert(pc, h, size, off); counts[im->cpu & 1]++;
    }
    free(done); free(work);
}

int main(int argc, char **argv) {
    const char *out = "gen"; int units = 16;
    for (int op = 0; op < 65536; op++) { opid[op] = -1; for (unsigned i = 0; i < sizeof OPS / sizeof *OPS; i++) if ((op & OPS[i].mask) == OPS[i].val) { opid[op] = (short)i; opkind[op] = (u8)kind_of(OPS[i].name); if (!strcmp(OPS[i].name, "OP_ILLEGAL")) opid[op] = -1; break; } }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) units = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-explore")) explore = 0;
        else if (!strcmp(argv[i], "--image") && i + 1 < argc) {
            char *s = argv[++i]; char *c1 = strchr(s, ':'), *c2 = strrchr(s, ':');
            if (!c1 || c1 == c2) { fprintf(stderr, "bad --image %s\n", s); return 1; }
            *c1 = 0; *c2 = 0;
            FILE *f = fopen(c1 + 1, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", c1 + 1); return 1; }
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); u8 *d = malloc(n + 4); if (fread(d, 1, n, f) != (size_t)n) return 1; fclose(f);
            add_image(atoi(s), (u32)strtoul(c2 + 1, 0, 16), d, (u32)n);
        } else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) {
            FILE *f = fopen(argv[++i], "r"); if (!f) { fprintf(stderr, "cannot open %s\n", argv[i]); return 1; }
            static char line[8192];
            while (fgets(line, sizeof line, f)) {
                int cpu; unsigned pc; int n = 0; if (line[0] == '#' || sscanf(line, "%d %x%n", &cpu, &pc, &n) < 2) continue;
                if (nseed == capseed) { capseed = capseed ? capseed * 2 : 4096; seed = realloc(seed, capseed * sizeof *seed); }
                seed[nseed++] = (Seed){ cpu, pc };
                char *p = line + n; while (*p == ' ') p++;
                size_t hl = strspn(p, "0123456789abcdefABCDEF");
                if (hl >= 4) { u8 *d = malloc(hl / 2 + 4); for (size_t k = 0; k < hl / 2; k++) { unsigned v; sscanf(p + 2 * k, "%2x", &v); d[k] = (u8)v; } add_image(cpu, pc, d, (u32)(hl / 2)); }
            }
            fclose(f);
        } else { fprintf(stderr, "usage: blockgen -o DIR -n UNITS --image CPU:FILE:HEXBASE ... --seeds FILE ... [--no-explore]\n"); return 1; }
    }
    capblk = 4096; blk = malloc(capblk * sizeof *blk); rehash();
    for (int cpu = 0; cpu < 2; cpu++) {
        u32 *s = malloc((nseed + 1) * sizeof *s); int ns = 0;
        for (int i = 0; i < nseed; i++) if (seed[i].cpu == cpu) s[ns++] = seed[i].pc;
        for (int i = 0; i < nimg; i++) if (img[i].cpu == cpu) process(&img[i], s, ns);
        free(s);
    }
    qsort(blk, nblk, sizeof *blk, cmp_blk);
    const char *hdr = "/* generated by tools/blockgen - do not edit */\n#include \"sh4sem.h\"\n#include \"blockrt.h\"\n";
    int per = (nblk + units - 1) / (units > 0 ? units : 1); char path[1024];
    for (int u = 0; u < units; u++) {
        if (u * per >= nblk) break;
        snprintf(path, sizeof path, "%s/blk_%02d.c", out, u); FILE *f = fopen(path, "w"); if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        fputs(hdr, f);
        for (int i = u * per; i < (u + 1) * per && i < nblk; i++) fprintf(f, "void B_%08x_%08x(Cpu *c) {\n%s}\n", blk[i].pc, blk[i].h, text + blk[i].off);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/blk_table.c", out); FILE *f = fopen(path, "w"); if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    fputs(hdr, f);
    for (int i = 0; i < nblk; i++) fprintf(f, "void B_%08x_%08x(Cpu *c);\n", blk[i].pc, blk[i].h);
    fputs("const BlkEntry blk_table[] = {\n", f);
    for (int i = 0; i < nblk; i++) fprintf(f, "  {0x%08xu, 0x%08xu, %uu, B_%08x_%08x},\n", blk[i].pc, blk[i].h, blk[i].size, blk[i].pc, blk[i].h);
    fprintf(f, "};\nconst int blk_count = %d;\n", nblk);
    fclose(f);
    printf("blocks: %d (cpu0: %ld, cpu1: %ld) -> %s/\n", nblk, counts[0], counts[1], out);
    return 0;
}
