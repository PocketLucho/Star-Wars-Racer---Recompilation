/* Game module interface. The recompiled blocks of a game are NOT part of the program: each user builds them
 * from their own ROM (builder/) into a shared library that exports swr_module_init(). The blocks reach the
 * machine through this small table of host functions. */
#pragma once
#include "sh4.h"
#include "blockrt.h"
#define SWR_MOD_ABI 1
typedef struct {
    u8  (*bus_r8)(Cpu *, u32);  u16 (*bus_r16)(Cpu *, u32);  u32 (*bus_r32)(Cpu *, u32);
    void (*bus_w8)(Cpu *, u32, u8);  void (*bus_w16)(Cpu *, u32, u16);  void (*bus_w32)(Cpu *, u32, u32);
    void (*sh4_illegal)(Cpu *, u32, u16);
    void (*sh4_on_sr_change)(Cpu *);
    void (*sh4_pref)(Cpu *, u32);
} SwrHost;
typedef struct {
    int abi; unsigned cpu_size;      /* SWR_MOD_ABI, sizeof(Cpu): must match the program */
    const char *machine;             /* "hikaru" or "dreamcast" */
    const BlkEntry *table; int count;
} SwrModule;
#ifdef _WIN32
#define SWR_EXPORT __declspec(dllexport)
#else
#define SWR_EXPORT __attribute__((visibility("default")))
#endif
