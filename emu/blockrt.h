/* Block-level AOT runtime (same scheme as the Virtua NBA / Virtua Tennis / HOTD2 recomps):
 *  - unit = SH-4 basic block (ends after a branch + delay slot, bt/bf, or 256 instructions;
 *    rte / trapa / sleep always run in the interpreter as single-instruction blocks)
 *  - every generated block carries the FNV-1a hash of its guest bytes; the runtime picks the
 *    version that matches the CPU's RAM (code that is copied / rewritten at run time)
 *  - verified 4 KB pages are watched: any RAM write to them (CPU, DMA, bridge) bumps the page
 *    generation and forces re-verification
 *  - no block: the interpreter runs the block with identical boundaries and cycle cost
 *  - lockstep (-verify): interpreter with a memory-access journal, then the AOT block replaying it,
 *    all architectural registers compared */
#pragma once
#include "sh4.h"
#include <stdio.h>

typedef void (*BlkFn)(Cpu *c);
typedef struct { u32 pc; u32 hash; u32 size; BlkFn fn; } BlkEntry;
extern const BlkEntry blk_table[];   /* linked-in table (blk_stub.c or gen/blk_table.c) */
extern const int blk_count;
void blk_use_table(const BlkEntry *t, int n);     /* before blk_init: use this table (game module) */
int  blk_table_size(void);
/* game module (swrmod.h): returns the number of blocks, or -1 if there is no usable module at path */
int  blk_module_load(const char *path, const char *machine);

#define BLK_MAX_INSNS 256

extern u8 *g_blk_ram[2];        /* set by the machine before blk_init */
extern u32 g_blk_ram_mask;
extern FILE *g_blk_log;
void blk_init(int aot_enabled, int verify, const char *miss_path, const char *seen_path);
void blk_step(Cpu *c);                       /* run one block on c */
void blk_ram_written(int cpu, u32 off, u32 len);   /* off = byte offset into that CPU's 32 MB RAM */
void blk_stats(FILE *f);
void blk_finish(void);

/* memory-access journal used by lockstep verification (hooked into bus_r / bus_w) */
extern int g_jmode;      /* 0 off, 1 record, 2 replay */
u32 jr_replay_read(u32 a, int sz);
void jr_record(u32 a, int sz, u32 v, int wr);
void jr_replay_write(u32 a, int sz, u32 v);

/* block-boundary rule shared by the interpreter path and tools/blockgen.py */
int sh4_is_block_end(u16 op);       /* branch (incl. delay slot), bt/bf */
int sh4_is_interp_only(u16 op);     /* rte, trapa, sleep, illegal */
