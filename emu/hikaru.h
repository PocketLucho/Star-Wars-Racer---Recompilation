#pragma once
#include "sh4.h"
#include "x76f100.h"
#include <stdio.h>

typedef struct {
    u32 regs[0x40];
    u32 bases[12];
    int cfg14, cfg18;
    int mode;               /* 0 main, 1 slave */
} Bridge;

typedef struct {
    /* 93C46 serial EEPROM, 8-bit organisation */
    u8 data[128];
    int cs, sk, di, dout;
    u32 shreg; int bits; int state; int addr; int wen; int rdcnt;
} Eeprom93;
void eep_update(Eeprom93 *e, int cs, int sk, int di);

typedef struct Machine {
    Cpu cpu[2];
    u8 *bios;               /* 2 MB */
    u8 *user1;              /* 64 MB program ROM region */
    u8 *user2;              /* 256 MB mask ROM region */
    u8 *ram[2];             /* 32 MB local RAM per CPU */
    u8 *shared;             /* 4 MB slave bridge RAM (PCI 0x48000000) */
    u8 nvram[0x10000];
    u8 protram[0x10000];
    u8 *aica_ram[2];        /* 8 MB each */
    struct Aica *aica[2];
    int aica_arm_reset[2]; u32 rtc_base;
    u8 mie_mem[0x10000];
    u8 mie_port[0x100];
    int mie_busrq;
    Bridge br[2];
    Eeprom93 eep;          /* mainboard 93C46 on CPU0 port A */
    Eeprom93 mie_eep;      /* 93C46 on the MIE (port F: CS bit3, DI bit1, SK bit0; port G bit 0 = DO): BIOS system settings */
    X76 x76;
    u32 porta_out[2], porta_ctrl[2];
    u32 nmi_hist[2]; int nmi_pending[2];
    u32 irl_pins[2];
    u32 mie_irq_pending;        /* independent IRL pins asserted (bit0=IRL0..bit3=IRL3) */
    /* unknown-device logging */
    FILE *log;
    int log_unmapped;
    u64 frame;
} Machine;

extern Machine *g_m;
void hikaru_init(Machine *m, const char *romdir);
u32 pci_r32(Machine *m, u32 a, u32 mask);
void pci_w32(Machine *m, u32 a, u32 v, u32 mask);
void cpu_check_irq(Cpu *c);
void onchip_tick(Cpu *c, int cycles);
void hikaru_audio_run(Machine *m, int n, int32_t *mix);
