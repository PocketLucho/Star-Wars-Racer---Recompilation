#include <time.h>
/* Emulation core shared by the console runner (main_cli.c) and the Windows frontend (main_win32.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hikaru.h"
#include "gpu.h"
#include "mie.h"
#include "blockrt.h"
#include "emu_core.h"

void sh4_init_tables(void);
void sh4_reset(Cpu *c);
int sh4_take_irq(Cpu *c);

static Machine M;
static EmuConfig C;
static u64 t_slice, next_vbl = 3333333, n_blocks, audio_t;
/* audio: stereo int16 ring, filled at 44.1 kHz */
#define AUD_N 65536
long long g_audio_ns;
int16_t g_aud[AUD_N * 2]; volatile uint32_t g_aud_w; static int32_t mixbuf[512 * 2];
static int booted, exec_reset_done;
static u32 *prof;
static u8 execmap[2][0x2000000 / 16];
static u8 visited[2][1 << 17];
u64 g_idle_skipped[2];

Machine *emu_machine(void) { return &M; }

/* ---- non-volatile memory, saved to a file: backup SRAM (0x00C00000, 64 KB) + the 93C46 EEPROM.
 *   The MIE 93C46 holds the BIOS settings (CABINET, ADVERTISE SOUND, country) and the game area; the BIOS reads
 *   it at boot from the SH-4 through the MIE ports and saves it through the Z80 (command 7). SWRNV002 = SRAM +
 *   mainboard 93C46 (port A, unused by this game) + MIE 93C46; SWRNV001 (build 8) had no MIE slot. */
static u8 nv_last[0x10000 + 256];
static void nv_snap(void) { memcpy(nv_last, M.nvram, 0x10000); memcpy(nv_last + 0x10000, M.eep.data, 128); memcpy(nv_last + 0x10080, M.mie_eep.data, 128); }
int emu_nv_read(FILE *f) {
    u8 hdr[8], a[128], b[128]; if (fread(hdr, 1, 8, f) != 8) return -1;
    int v2 = !memcmp(hdr, "SWRNV002", 8);
    if (!v2 && memcmp(hdr, "SWRNV001", 8)) return -1;
    int ok = fread(M.nvram, 1, sizeof M.nvram, f) == sizeof M.nvram && fread(a, 1, 128, f) == 128;
    if (ok && v2) ok = fread(b, 1, 128, f) == 128;
    if (!ok) return -1;
    memcpy(M.eep.data, a, 128);
    if (v2) memcpy(M.mie_eep.data, b, 128);
    nv_snap();
    return 0;
}
int emu_nv_write(FILE *f) {
    int ok = fwrite("SWRNV002", 1, 8, f) == 8 && fwrite(M.nvram, 1, sizeof M.nvram, f) == sizeof M.nvram
        && fwrite(M.eep.data, 1, 128, f) == 128 && fwrite(M.mie_eep.data, 1, 128, f) == 128;
    if (ok) nv_snap();
    return ok ? 0 : -1;
}
/* ---- unlocks (Ben Quadinaros / Gasgano) ----
 * The game keeps them in the "game area" of the MIE 93C46: 32 bytes at 0x1E of each 64-byte copy
 * {u32 sum of bytes 4..31, u32 0x00050806, ..., byte 11 = flags}: bit 0x20 = Gasgano (set by the game after
 * 700 plays, @0C140EA4), bit 0x40 = Ben Quadinaros (Expert course finished 50 times, @0C140EB4). Each copy
 * ends with the BIOS CRC (@0C010F48: CCITT 0x1021, register seeded 0x01A18104) of its first 0x3E bytes.
 * The running game holds the area at 0x0CEB5750 (and the BIOS at 0x0C00099E), patched too. */
static u16 bios_crc(const u8 *p, int n) {
    u32 r0 = 0x01a18104u, r1 = p[0]; int i = 1;
    for (int k = 0; k < n; k++) {
        r0 |= r1; r1 = i < n ? p[i] : 0; i++;
        for (int b = 0; b < 8; b++) { u32 top = r0 & 0x80000000u; r0 <<= 1; if (top) r0 ^= 0x10210000u; }
    }
    for (int b = 0; b < 8; b++) { u32 top = r0 & 0x80000000u; r0 <<= 1; if (top) r0 ^= 0x10210000u; }
    return (u16)(r0 >> 16);
}
static int game_area_set(u8 *g, u8 bit) {          /* g = 32-byte area; 1 if it is a valid area */
    if (!(g[4] == 0x06 && g[5] == 0x08 && g[6] == 0x05 && g[7] == 0x00)) return 0;
    g[11] |= bit;
    u32 s = 0; for (int i = 4; i < 32; i++) s += g[i];
    g[0] = (u8)s; g[1] = (u8)(s >> 8); g[2] = (u8)(s >> 16); g[3] = (u8)(s >> 24);
    return 1;
}
int emu_unlock(int which) {
    u8 bit = which == 0 ? 0x40 : 0x20;   /* 0 = Ben Quadinaros, 1 = Gasgano */
    int n = 0;
    for (int c = 0; c < 128; c += 64) {
        u8 *blk = M.mie_eep.data + c;
        if (game_area_set(blk + 0x1e, bit)) { u16 crc = bios_crc(blk, 0x3e); blk[0x3e] = (u8)crc; blk[0x3f] = (u8)(crc >> 8); n++; }
    }
    if (M.ram[0]) { game_area_set(M.ram[0] + 0xeb5750, bit); game_area_set(M.ram[0] + 0x99e, bit); }
    return n ? 0 : -1;    /* -1: no game area yet (EEPROM blank: let the game boot once) */
}
/* country (MIE EEPROM byte 5 of each copy; written at the factory, not reachable from the BIOS menus;
 * a blank chip = Japan): 0 JPN, 1 USA, 2 EXP, 3/4 (see LEEME). Blank chips get the factory layout. */
static const u8 eep_factory[64] = {
    0x42,0x42,0x50,0x2d,0x02,0x00,0x01,0x00,0x18,0x00,0x04,0x01,0x01,0x01,0x01,0x00,
    0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x1b,0x17,
    0x00,0x00,0x06,0x08,0x05,0x00,0xff,0xff,0xff,0x1f,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00 };
int emu_region(void) { return M.mie_eep.data[0] == 0x42 && M.mie_eep.data[1] == 0x42 ? M.mie_eep.data[5] : 0; }
void emu_set_region(int r) {
    for (int c = 0; c < 128; c += 64) {
        u8 *blk = M.mie_eep.data + c;
        if (!(blk[0] == 0x42 && blk[1] == 0x42 && blk[2] == 0x50 && blk[3] == 0x2d)) memcpy(blk, eep_factory, 64);
        blk[5] = (u8)r;
        u16 crc = bios_crc(blk, 0x3e); blk[0x3e] = (u8)crc; blk[0x3f] = (u8)(crc >> 8);
    }
}
int emu_unlocked(int which) {
    u8 bit = which == 0 ? 0x40 : 0x20;
    return (M.mie_eep.data[0x1e + 11] & bit) != 0;
}
int emu_nv_load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int r = emu_nv_read(f); fclose(f); return r;
}
int emu_nv_save(const char *path) {
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    int ok = emu_nv_write(f) == 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return -1; }
    remove(path);
    return rename(tmp, path) != 0 ? -1 : 0;
}
int emu_nv_dirty(void) {
    return memcmp(nv_last, M.nvram, 0x10000) || memcmp(nv_last + 0x10000, M.eep.data, 128) || memcmp(nv_last + 0x10080, M.mie_eep.data, 128);
}
u64 emu_cycles(void) { return M.cpu[0].cycles; }

void emu_dump_regs(Cpu *c) {
    fprintf(stderr, "CPU%d pc=%08x pr=%08x sr=%08x vbr=%08x gbr=%08x spc=%08x ssr=%08x sleeping=%d\n", c->id, c->pc, c->pr, c->sr, c->vbr, c->gbr, c->spc, c->ssr, c->sleeping);
    for (int i = 0; i < 16; i++) fprintf(stderr, " r%-2d=%08x%s", i, c->r[i], (i & 3) == 3 ? "\n" : "");
}

int emu_init(const EmuConfig *cfg) {
    C = *cfg;
    sh4_init_tables();
    hikaru_init(&M, C.rom);
    M.log = C.logpath ? fopen(C.logpath, "w") : 0;
    for (int i = 0; i < 2; i++) sh4_reset(&M.cpu[i]);
    g_blk_ram[0] = M.ram[0]; g_blk_ram[1] = M.ram[1]; g_blk_ram_mask = 0x01ffffff; g_blk_log = M.log;
    if (C.module && C.aot) blk_module_load(C.module, "hikaru");
    blk_init(C.aot, C.verify, C.misses, C.seen);
    return 0;
}

/* idle loops: polling of a value that only an interrupt or the other CPU can change. Skipped at the
 * loop head, and only while the loop condition still says "keep waiting". */
static int is_idle(Cpu *c, u32 p) {
    if (!C.idle_skip) return 0;
    if (c->id == 0) {
        if (p == 0x0c0118d6 || p == 0x0c0069fc) return (s8)bus_r8(c, c->gbr) > 0;      /* vblank counter wait (game / BIOS) */
        if (p == 0x0c001e8e) return bus_r32(c, c->r[3]) != 0;                           /* BIOS: wait for the slave's mailbox reply */
        return 0;
    }
    if (p == 0x0c002030) return bus_r8(c, c->gbr) == 0 && !((Machine *)c->machine)->nmi_pending[1];  /* slave: BIOS mailbox poll */
    return 0;
}

static void hle_boot(Cpu *c) {
    /* game header descriptor @0x390: src 0x10000400 -> dst 0x0c010000, len 0x1ff0000, entry 0x0c010000 */
    memcpy(M.ram[0] + 0x10000, M.user1 + 0x400, 0x1ff0000);
    blk_ram_written(0, 0x10000, 0x1ff0000);
    c->pc = 0xac010000; c->sr |= 0xf0; booted = 1;
    /* like the BIOS: reset the slave and let it reach its mailbox poll loop */
    sh4_reset(&M.cpu[1]);
    u64 k = 0; Cpu *s1 = &M.cpu[1]; u64 sc = s1->cycles;
    while ((s1->pc & 0x1fffffff) != 0x0c00bf12 && k < 50000000) { sh4_take_irq(s1); blk_step(s1); k++; if (s1->cycles - sc >= 64) { onchip_tick(s1, (int)(s1->cycles - sc)); sc = s1->cycles; } }
    s1->cycles = c->cycles;
    if (M.log) fprintf(M.log, "[%llu] HLE boot: program loaded, jump to 0c010000 (slave settled after %llu blocks)\n", (unsigned long long)c->cycles, (unsigned long long)k);
}

/* returns 1 when a vblank happened in this slice, -1 on stop */
static int run_slice(void) {
    const u64 SLICE = 256;
    t_slice += SLICE;
    for (int ci = 0; ci < (C.no_slave ? 1 : 2); ci++) {
        Cpu *c = &M.cpu[ci];
        u64 c0 = c->cycles;
        while (c->cycles < t_slice) {
            if (c->sleeping) { sh4_take_irq(c); if (c->sleeping) { c->cycles = t_slice; break; } }
            sh4_take_irq(c);
            u32 pc = c->pc, p = pc & 0x1fffffff;
            if (C.pagelog && M.log) {
                u32 pg = (p >> 12) & 0x1ffff;
                if (!visited[ci][pg]) { visited[ci][pg] = 1;
                    fprintf(M.log, "[%llu] CPU%d enters page %08x (pc=%08x pr=%08x)\n", (unsigned long long)c->cycles, ci, pc & ~0xfffu, pc, c->pr); }
            }
            if (C.hleboot && !booted && ci == 0 && p == 0x0c0069fc) { hle_boot(c); continue; }
            if (is_idle(c, p)) { g_idle_skipped[ci] += t_slice - c->cycles; c->cycles = t_slice; break; }
            if (C.exec_reset_at && c->cycles >= C.exec_reset_at && !exec_reset_done) { exec_reset_done = 1; memset(execmap, 0, sizeof execmap); }
            if (ci == C.stop_cpu && p == (C.stop_pc & 0x1fffffff) && c->cycles >= C.stop_after) { fprintf(stderr, "stop hit\n"); emu_dump_regs(c); return -1; }
            if (p >= 0x0c000000 && p < 0x0e000000) { u32 o = (p - 0x0c000000) >> 1; execmap[ci][o >> 3] |= 1 << (o & 7); }
            if (C.prof_cpu == ci && ((++n_blocks) & 63) == 0) { if (!prof) prof = calloc(1 << 22, 4); prof[(p >> 1) & 0x3fffff]++; }
            blk_step(c);
        }
        onchip_tick(c, (int)(c->cycles - c0));
        if (ci == 0) mie_run(&M, (int)((c->cycles - c0) * 6 / 64));
    }
    if (C.no_slave) M.cpu[1].cycles = t_slice;
    /* audio at 44.1 kHz: 200 MHz / 44100 = 4535.147 SH-4 cycles per sample */
    {
        u64 due = (M.cpu[0].cycles * 44100ULL) / 200000000ULL;
        int n = (int)(due - audio_t);
        while (n > 0) {
            int k = n > 512 ? 512 : n;
            memset(mixbuf, 0, k * 8);
#ifndef _WIN32
            { struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
              hikaru_audio_run(&M, k, mixbuf);
              clock_gettime(CLOCK_MONOTONIC, &t1); g_audio_ns += (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec); }
#else
            hikaru_audio_run(&M, k, mixbuf);
#endif
            for (int i = 0; i < k * 2; i++) { int32_t v = mixbuf[i] >> 1; if (v > 32767) v = 32767; if (v < -32768) v = -32768; g_aud[(g_aud_w * 2 + i) % (AUD_N * 2)] = (int16_t)v; }
            g_aud_w += k; audio_t += k; n -= k;
        }
    }
    if (M.cpu[0].cycles >= next_vbl) { next_vbl += 3333333; gpu_vblank(&M); return 1; }
    return 0;
}

int emu_run_frame(void) {
    for (;;) { int r = run_slice(); if (r) return r < 0 ? -1 : 0; }
}

void emu_finish(int dumps) {
    fprintf(stderr, "audio time: %.2f s\n", g_audio_ns / 1e9);
    for (int i = 0; i < 2; i++) emu_dump_regs(&M.cpu[i]);
    if (dumps) {
        FILE *f = fopen("ram_master.bin", "wb"); fwrite(M.ram[0], 1, 0x2000000, f); fclose(f);
        f = fopen("ram_slave.bin", "wb"); fwrite(M.ram[1], 1, 0x2000000, f); fclose(f);
        f = fopen("eur_vram.bin", "wb"); fwrite(g_gpu.eur_vram, 1, 0x800000, f); fclose(f);
        f = fopen("exec_master.bin", "wb"); fwrite(execmap[0], 1, sizeof execmap[0], f); fclose(f);
        f = fopen("exec_slave.bin", "wb"); fwrite(execmap[1], 1, sizeof execmap[1], f); fclose(f);
        f = fopen("mie_mem.bin", "wb"); fwrite(M.mie_mem, 1, 0x10000, f); fclose(f);
        f = fopen("shared.bin", "wb"); fwrite(M.shared, 1, 0x400000, f); fclose(f);
        f = fopen("aica0_ram.bin", "wb"); fwrite(M.aica_ram[0], 1, 0x800000, f); fclose(f);
    }
    if (M.log) gpu_dump_stats(M.log);
    mie_stats(stderr);
    { extern void aica_stats(struct Aica *, FILE *); aica_stats(M.aica[0], stderr); aica_stats(M.aica[1], stderr); }
    { extern u32 g_irqcnt[2][64]; for (int i = 0; i < 2; i++) for (int k = 0; k < 64; k++) if (g_irqcnt[i][k]) fprintf(stderr, "IRQ CPU%d evt %03x x%u\n", i, k << 5, g_irqcnt[i][k]); }
    if (prof) { for (int t = 0; t < 60; t++) { u32 best = 0, bi = 0; for (u32 i = 0; i < (1u << 22); i++) if (prof[i] > best) { best = prof[i]; bi = i; }
        if (!best) break; fprintf(stderr, "hot %08x x%u\n", bi << 1, best); prof[bi] = 0; } }
    fprintf(stderr, "frames=%llu cycles=%llu idle-skipped=%llu/%llu\n", (unsigned long long)g_gpu.frame, (unsigned long long)M.cpu[0].cycles,
            (unsigned long long)g_idle_skipped[0], (unsigned long long)g_idle_skipped[1]);
    blk_stats(stderr); blk_finish();
    if (M.log) fclose(M.log);
}

int emu_block_count(void) { return blk_table_size(); }
int emu_builder_dump(const char *dir) {
    char p[1100]; int ok = 1;
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, i ? "ram_slave.bin" : "ram_master.bin");
        FILE *f = fopen(p, "wb"); if (!f) return -1;
        if (fwrite(M.ram[i], 1, 0x2000000, f) != 0x2000000) ok = 0;
        if (fclose(f) != 0) ok = 0;
    }
    return ok ? 0 : -1;
}
