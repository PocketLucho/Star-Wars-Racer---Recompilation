/* Program side of the game module: loads swr_game.dll / .so and installs its block table. */
#include <stdio.h>
#include <string.h>
#include "swrmod.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
u8 bus_r8(Cpu *, u32); u16 bus_r16(Cpu *, u32); u32 bus_r32(Cpu *, u32);
void bus_w8(Cpu *, u32, u8); void bus_w16(Cpu *, u32, u16); void bus_w32(Cpu *, u32, u32);
void sh4_illegal(Cpu *, u32, u16); void sh4_on_sr_change(Cpu *); void sh4_pref(Cpu *, u32);

int blk_module_load(const char *path, const char *machine) {
    static const SwrHost host = { bus_r8, bus_r16, bus_r32, bus_w8, bus_w16, bus_w32, sh4_illegal, sh4_on_sr_change, sh4_pref };
    typedef const SwrModule *(*InitFn)(const SwrHost *);
    InitFn init = 0;
#ifdef _WIN32
    wchar_t w[1024]; if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return -1;
    HMODULE m = LoadLibraryW(w); if (!m) return -1;
    init = (InitFn)(void *)GetProcAddress(m, "swr_module_init");
#else
    void *m = dlopen(path, RTLD_NOW | RTLD_LOCAL); if (!m) return -1;
    init = (InitFn)dlsym(m, "swr_module_init");
#endif
    if (!init) return -1;
    const SwrModule *g = init(&host);
    if (!g || g->abi != SWR_MOD_ABI || g->cpu_size != sizeof(Cpu) || !g->machine || strcmp(g->machine, machine)) {
        fprintf(stderr, "game module %s: built for another version of the program; run the builder again\n", path);
        return -1;
    }
    blk_use_table(g->table, g->count);
    return g->count;
}
