/* MIE (Maple/JVS Interface Engine): Z80 + GPIO + JVS UART, with a Sega 837-13551 JVS I/O board.
 * After MAME mie.cpp / jvshost.cpp / jvsdev.cpp / jvs13551.cpp. The 2 KB internal boot ROM
 * (315-6146) is not in the set; it is replaced by starting the uploaded firmware at 0x8010. */
#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t  test, service;      /* 1 = pressed (MIE GPIO G, active low on the wire) */
    uint8_t  tilt;               /* JVS system byte */
    uint16_t p[2];               /* JVS switch words, MAME bit layout (0x8000 start ... ) */
    uint16_t analog[8];          /* JVS analog channels, 16-bit, 0x8000 = centre */
    uint16_t coins[2];
    uint8_t  outputs;            /* last JVS general-purpose output byte (lamps etc.) */
} HikaruInput;

extern HikaruInput g_input;

struct Machine;
void mie_init(struct Machine *m);
void mie_run(struct Machine *m, int z80_cycles);         /* advance the Z80 */
uint8_t mie_port_r(struct Machine *m, uint8_t port, int from_host);
void mie_port_w(struct Machine *m, uint8_t port, uint8_t v, int from_host);
void mie_busrq_w(struct Machine *m, int state);
void mie_stats(FILE *f);
