#pragma once
#include <stdint.h>
#include <stdio.h>
#include "hikaru.h"
typedef struct {
    const char *rom;            /* directory of ROM files or a MAME zip */
    const char *logpath;        /* hikaru.log or NULL */
    int hleboot, aot, verify, no_slave, idle_skip, pagelog;
    const char *misses, *seen;
    const char *module;         /* game module (swr_game.dll / .so) with the recompiled blocks, or NULL */
    u64 exec_reset_at; int stop_cpu; u32 stop_pc; u64 stop_after; int prof_cpu;
} EmuConfig;
int emu_init(const EmuConfig *cfg);
int emu_run_frame(void);          /* runs until the next vblank; -1 = stop requested */
u64 emu_cycles(void);
Machine *emu_machine(void);
void emu_finish(int dumps);
/* non-volatile memory (backup SRAM + EEPROM): call emu_nv_load after emu_init */
int emu_nv_load(const char *path);
int emu_nv_save(const char *path);
int emu_nv_dirty(void);
/* unlock a hidden pilot in the game's EEPROM area (0 = Ben Quadinaros, 1 = Gasgano); 0 = ok */
int emu_unlock(int which);
int emu_unlocked(int which);
/* market (MIE EEPROM): 0 Japan, 1 USA, 2 Export, ...; call after emu_nv_load, before running */
int emu_region(void);
void emu_set_region(int r);
int emu_nv_read(FILE *f);    /* same format, caller-opened (wide paths on Windows) */
int emu_nv_write(FILE *f);
#define AUD_N 65536
extern int16_t g_aud[AUD_N * 2];          /* stereo ring buffer, 44.1 kHz */
extern volatile uint32_t g_aud_w;          /* total samples written */

/* builder support: writes the RAM of both CPUs (ram_master.bin, ram_slave.bin) into dir; 0 = ok */
int emu_builder_dump(const char *dir);
int emu_block_count(void);
