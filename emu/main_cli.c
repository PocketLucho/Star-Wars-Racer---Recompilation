/* Console runner (tests, coverage passes, lockstep, frame dumps). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "emu_core.h"
#ifdef HAVE_VULKAN
#include "vkrender.h"
int gfx_vk_start(const VkrConfig *cfg);
#endif

#include "mie.h"
#include "gpu.h"
#include "cheats.h"
#include "emu_core.h"
/* scripted inputs: -coin F, -press F <p1 mask hex> <frames>, -analog F <channel> <value hex> */
static struct { long frame; int kind; unsigned mask; long len; } ev[256]; static int nev;
static void apply_inputs(void) {
    long f = (long)g_gpu.frame; unsigned p1 = 0;
    for (int i = 0; i < nev; i++) {
        if (ev[i].kind == 0 && f == ev[i].frame) g_input.coins[0]++;
        if (ev[i].kind == 1 && f >= ev[i].frame && f < ev[i].frame + ev[i].len) p1 |= ev[i].mask;
        if (ev[i].kind == 2 && f == ev[i].frame) g_input.analog[ev[i].mask & 7] = (unsigned short)ev[i].len;
        if (ev[i].kind == 3 && f == ev[i].frame) fprintf(stderr, "unlock %u: %s\n", ev[i].mask, emu_unlock((int)ev[i].mask) == 0 ? "ok" : "no game area");
    }
    g_input.p[0] = (unsigned short)p1;
}

int main(int argc, char **argv) {
    EmuConfig c = { 0 }; const char *builder_dump = 0;
    c.rom = "../rom"; c.logpath = "hikaru.log"; c.pagelog = 1; c.stop_cpu = -1; c.prof_cpu = -1; c.idle_skip = 1;
    u64 maxcyc = 200000000ULL; const char *wavpath = 0; FILE *wav = 0; uint32_t aud_r = 0, wav_n = 0; int use_vk = 0, vk_scale = 1, vk_wide = 0, vk_msaa = 1, dumps = 1; const char *nvpath = 0; int region = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-rom")) c.rom = argv[++i];
        else if (!strcmp(argv[i], "-n")) maxcyc = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-noslave")) c.no_slave = 1;
        else if (!strcmp(argv[i], "-hleboot")) c.hleboot = 1;
        else if (!strcmp(argv[i], "-noidle")) c.idle_skip = 0;
        else if (!strcmp(argv[i], "-nodumps")) dumps = 0;
        else if (!strcmp(argv[i], "-prof")) c.prof_cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-aot")) c.aot = 1;
        else if (!strcmp(argv[i], "-vk")) use_vk = 1;
        else if (!strcmp(argv[i], "-vkscale")) vk_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-vkwide")) vk_wide = 1;
        else if (!strcmp(argv[i], "-vkmsaa")) vk_msaa = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-nv")) nvpath = argv[++i];
        else if (!strcmp(argv[i], "-verify")) c.verify = 1;
        else if (!strcmp(argv[i], "-misses")) c.misses = argv[++i];
        else if (!strcmp(argv[i], "-seen")) c.seen = argv[++i];
        else if (!strcmp(argv[i], "-module")) c.module = argv[++i];
        else if (!strcmp(argv[i], "-builder-dump")) builder_dump = argv[++i];
        else if (!strcmp(argv[i], "-execreset")) c.exec_reset_at = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-stopafter")) c.stop_after = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-stop")) { c.stop_cpu = atoi(argv[++i]); c.stop_pc = strtoul(argv[++i], 0, 16); }
        else if (!strcmp(argv[i], "-test")) { extern long g_test_frames[32]; extern int g_ntest; if (g_ntest < 32) g_test_frames[g_ntest++] = atol(argv[++i]); else i++; }
        else if (!strcmp(argv[i], "-service")) { extern long g_svc_frames[64]; extern int g_nsvc; if (g_nsvc < 64) g_svc_frames[g_nsvc++] = atol(argv[++i]); else i++; }
        else if (!strcmp(argv[i], "-watch")) { extern u32 g_watch; g_watch = strtoul(argv[++i], 0, 16); }
        else if (!strcmp(argv[i], "-watchval")) { extern u32 g_watchval; g_watchval = strtoul(argv[++i], 0, 16); }
        else if (!strcmp(argv[i], "-wav")) wavpath = argv[++i];
        else if (!strcmp(argv[i], "-region")) region = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cheat")) { ev[nev].frame = atol(argv[++i]); ev[nev].kind = 3; ev[nev].mask = atoi(argv[++i]); nev++; }
        else if (!strcmp(argv[i], "-coin")) { ev[nev].frame = atol(argv[++i]); ev[nev].kind = 0; nev++; }
        else if (!strcmp(argv[i], "-press")) { ev[nev].frame = atol(argv[++i]); ev[nev].kind = 1; ev[nev].mask = strtoul(argv[++i], 0, 16); ev[nev].len = atoi(argv[++i]); nev++; }
        else if (!strcmp(argv[i], "-analog")) { ev[nev].frame = atol(argv[++i]); ev[nev].kind = 2; ev[nev].mask = atoi(argv[++i]); ev[nev].len = strtoul(argv[++i], 0, 16); nev++; }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
    }
    emu_init(&c);
    if (nvpath) fprintf(stderr, "nvram %s: %s\n", nvpath, emu_nv_load(nvpath) == 0 ? "loaded" : "not found (fresh)");
    if (region >= 0) emu_set_region(region);
#ifdef HAVE_VULKAN
    if (use_vk) { VkrConfig vc = { 0 }; vc.headless = 1; vc.scale = vk_scale; vc.wide = vk_wide; vc.msaa = vk_msaa; if (gfx_vk_start(&vc)) fprintf(stderr, "Vulkan unavailable, continuing without it\n"); }
#else
    if (use_vk) fprintf(stderr, "this build has no Vulkan renderer\n");
#endif
    if (wavpath) { wav = fopen(wavpath, "wb"); static const char hdr[44] = { 0 }; fwrite(hdr, 1, 44, wav); }
    while (emu_cycles() < maxcyc) {
        apply_inputs(); if (emu_run_frame() < 0) break;
        if (wav) { while (aud_r < g_aud_w) { fwrite(&g_aud[(aud_r % AUD_N) * 2], 4, 1, wav); aud_r++; wav_n++; } }
    }
    if (wav) {   /* RIFF header: 44.1 kHz stereo 16-bit */
        uint32_t data = wav_n * 4; uint8_t h[44]; memcpy(h, "RIFF", 4); uint32_t v = 36 + data; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4); uint16_t s16 = 1; memcpy(h + 20, &s16, 2); s16 = 2; memcpy(h + 22, &s16, 2); v = 44100; memcpy(h + 24, &v, 4);
        v = 44100 * 4; memcpy(h + 28, &v, 4); s16 = 4; memcpy(h + 32, &s16, 2); s16 = 16; memcpy(h + 34, &s16, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
    if (nvpath) emu_nv_save(nvpath);
    if (builder_dump) { int r = emu_builder_dump(builder_dump); fprintf(stderr, "builder dump: %s\n", r ? "FAILED" : "ok"); return r ? 1 : 0; }
    emu_finish(dumps);
    return 0;
}
