/* Game-module side (compiled by the builder together with the generated blocks; -DSWR_DREAMCAST for the Dreamcast game):
 * forwards the bus calls of the blocks to the host program. */
#include "swrmod.h"
#ifndef SWR_MACHINE
#ifdef SWR_DREAMCAST
#define SWR_MACHINE "dreamcast"
#else
#define SWR_MACHINE "hikaru"
#endif
#endif
static const SwrHost *H;
u8 bus_r8(Cpu *c, u32 a) { return H->bus_r8(c, a); }
u16 bus_r16(Cpu *c, u32 a) { return H->bus_r16(c, a); }
u32 bus_r32(Cpu *c, u32 a) { return H->bus_r32(c, a); }
void bus_w8(Cpu *c, u32 a, u8 v) { H->bus_w8(c, a, v); }
void bus_w16(Cpu *c, u32 a, u16 v) { H->bus_w16(c, a, v); }
void bus_w32(Cpu *c, u32 a, u32 v) { H->bus_w32(c, a, v); }
void sh4_illegal(Cpu *c, u32 pc, u16 op) { H->sh4_illegal(c, pc, op); }
void sh4_on_sr_change(Cpu *c) { H->sh4_on_sr_change(c); }
void sh4_pref(Cpu *c, u32 a) { H->sh4_pref(c, a); }
SWR_EXPORT const SwrModule *swr_module_init(const SwrHost *h) {
    static SwrModule m;
    H = h; m.abi = SWR_MOD_ABI; m.cpu_size = sizeof(Cpu); m.machine = SWR_MACHINE; m.table = blk_table; m.count = blk_count;
    return &m;
}
