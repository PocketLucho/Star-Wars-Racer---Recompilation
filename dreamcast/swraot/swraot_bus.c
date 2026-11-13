/* Star Wars Racer recomp, Dreamcast: bus of our SH-4 core (interpreter + AOT blocks) on Flycast's memory map.
 * Main RAM (16 MB, area 3 and mirrors) is accessed directly; everything else goes through Flycast
 * (fc_* in swraot_executor.cpp). Store queues live in our Cpu and are flushed through Flycast's handler
 * (TA FIFO, RAM, ...). CPU writes to RAM invalidate the AOT blocks of the touched pages. */
#include <string.h>
#include <stdio.h>
#include "sh4.h"
#include "blockrt.h"

u8   fc_read8(u32 a);
u16  fc_read16(u32 a);
u32  fc_read32(u32 a);
void fc_write8(u32 a, u8 v);
void fc_write16(u32 a, u16 v);
void fc_write32(u32 a, u32 v);
void fc_sq_flush(Cpu *c, u32 a);          /* copy c->sq into Flycast's buffer and run its SQ handler */
void fc_reios_trap(Cpu *c, u32 pc);        /* HLE BIOS call (opcode 0x085B in the BIOS area) */

#define IS_RAM(a) ((((a) & 0x1c000000u) == 0x0c000000u) && (a) < 0xe0000000u)
#define RAMOFF(a) ((a) & 0x00ffffffu)
static inline u8 *ram(void) { return g_blk_ram[0]; }

u8 bus_r8(Cpu *c, u32 a) { (void)c; return IS_RAM(a) ? ram()[RAMOFF(a)] : fc_read8(a); }
u16 bus_r16(Cpu *c, u32 a) {
    (void)c; if (IS_RAM(a)) { u16 v; memcpy(&v, ram() + RAMOFF(a), 2); return v; }
    return fc_read16(a);
}
u32 bus_r32(Cpu *c, u32 a) {
    if (IS_RAM(a)) { u32 v; memcpy(&v, ram() + RAMOFF(a), 4); return v; }
    if ((a & 0xfc000000u) == 0xe0000000u) return c->sq[(a >> 5) & 1][(a >> 2) & 7];
    return fc_read32(a);
}
void bus_w8(Cpu *c, u32 a, u8 v) {
    (void)c; if (IS_RAM(a)) { ram()[RAMOFF(a)] = v; blk_ram_written(0, RAMOFF(a), 1); return; }
    fc_write8(a, v);
}
void bus_w16(Cpu *c, u32 a, u16 v) {
    (void)c; if (IS_RAM(a)) { memcpy(ram() + RAMOFF(a), &v, 2); blk_ram_written(0, RAMOFF(a), 2); return; }
    fc_write16(a, v);
}
u32 g_swr_watch = 0xffffffff; int g_swr_watch_n;
void bus_w32(Cpu *c, u32 a, u32 v) {
    if (RAMOFF(a) == g_swr_watch && IS_RAM(a) && g_swr_watch_n < 12) { g_swr_watch_n++; fprintf(stderr, "SWRAOT watch %08x <- %08x pc=%08x pr=%08x\n", a, v, c->pc, c->pr); }
    if (IS_RAM(a)) { memcpy(ram() + RAMOFF(a), &v, 4); blk_ram_written(0, RAMOFF(a), 4); return; }
    if ((a & 0xfc000000u) == 0xe0000000u) { c->sq[(a >> 5) & 1][(a >> 2) & 7] = v; return; }
    fc_write32(a, v);
}
u16 sh4_fetch(Cpu *c, u32 pc) { return bus_r16(c, pc); }
u16 sh4_fetch_raw(Cpu *c, u32 pc) { return bus_r16(c, pc); }

void sh4_pref(Cpu *c, u32 a) { if ((a & 0xfc000000u) == 0xe0000000u) fc_sq_flush(c, a); }
void sh4_on_sr_change(Cpu *c) { (void)c; }
void sh4_on_sleep(Cpu *c) { c->sleeping = 1; }

void sh4_exception(Cpu *c, u32 evt, u32 vecoff, u32 retpc);
void sh4_trap(Cpu *c, u32 imm, u32 pc) {
    fc_write32(0xff000020u, imm << 2);          /* TRA */
    fc_write32(0xff000024u, 0x160);             /* EXPEVT */
    sh4_exception(c, 0x160, 0x100, pc + 2);
}
void sh4_illegal(Cpu *c, u32 pc, u16 op) {
    if (op == 0x085b) { fc_reios_trap(c, pc); return; }     /* Flycast HLE BIOS hook */
    { static int n; if (n++ < 20) fprintf(stderr, "SWRAOT illegal %04x at %08x\n", op, pc); }
    fc_write32(0xff000024u, 0x180);
    sh4_exception(c, 0x180, 0x100, pc);
}
