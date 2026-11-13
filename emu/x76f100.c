/* X76F100 secure serial flash (ROM board), C port of MAME's device.
 * The real dump is not in the swracer set, so passwords are accepted
 * unconditionally (permissive mode) and data defaults to zero. */
#include <string.h>
#include <stdio.h>
#include "x76f100.h"

enum { ST_STOP, ST_RTR, ST_CMD, ST_PWD, ST_VPWD, ST_READ, ST_WRITE };

void x76_init(X76 *x) {
    memset(x, 0, sizeof *x);
    x->rtr[0] = 0x19; x->rtr[1] = 0x00; x->rtr[2] = 0xaa; x->rtr[3] = 0x55;
    x->state = ST_STOP;
}
static int data_offset(X76 *x) {
    int off = ((x->command >> 1) & 0x0f) * 8 + x->byte;
    return off >= 112 ? -1 : off;
}
static void pwd_ok(X76 *x) {
    if ((x->command & 0x81) == 0x81) x->state = ST_READ;
    else if ((x->command & 0x81) == 0x80) x->state = ST_WRITE;
}
static void w_cs(X76 *x, int s) {
    if (x->cs && !s) x->state = ST_STOP;
    if (!x->cs && s) { x->state = ST_STOP; x->sdar = 0; }
    x->cs = s;
}
static void w_rst(X76 *x, int s) {
    if (!x->rst && s && !x->cs) { x->state = ST_RTR; x->bit = 0; x->byte = 0; }
    x->rst = s;
}
static void w_scl(X76 *x, int s) {
    if (!x->cs) {
        switch (x->state) {
        case ST_RTR:
            if (x->scl && !s) {
                if (x->bit == 0) x->shift = x->rtr[x->byte];
                x->sdar = x->shift & 1; x->shift >>= 1; x->bit++;
                if (x->bit == 8) { x->bit = 0; if (++x->byte == 4) x->byte = 0; }
            }
            break;
        case ST_CMD: case ST_PWD: case ST_VPWD: case ST_WRITE:
            if (!x->scl && s) {
                if (x->bit < 8) { x->shift = (x->shift << 1) | (x->sdaw ? 1 : 0); x->bit++; }
                else {
                    x->sdar = 0;
                    switch (x->state) {
                    case ST_CMD: x->command = (u8)x->shift; x->state = ST_PWD; break;
                    case ST_PWD:
                        x->wbuf[x->byte++] = (u8)x->shift;
                        if (x->byte == 8) { x->state = ST_VPWD; x->accepted = 1; }
                        break;
                    case ST_VPWD:
                        if ((x->shift & 0xff) == 0x55) { if (x->accepted) pwd_ok(x); else x->sdar = 1; }
                        break;
                    case ST_WRITE:
                        x->wbuf[x->byte++] = (u8)x->shift;
                        if (x->byte == 8) {
                            if (x->command != 0xfc && x->command != 0xfe)
                                for (x->byte = 0; x->byte < 8; x->byte++) { int o = data_offset(x); if (o < 0) break; x->data[o] = x->wbuf[x->byte]; }
                            x->byte = 0;
                        }
                        break;
                    }
                    x->bit = 0; x->shift = 0;
                }
            }
            break;
        case ST_READ:
            if (!x->scl && s) {
                if (x->bit < 8) {
                    if (x->bit == 0) { int o = data_offset(x); x->shift = o < 0 ? 0 : x->data[o]; }
                    x->sdar = (x->shift >> 7) & 1; x->shift <<= 1; x->bit++;
                } else { x->bit = 0; x->sdar = 0; if (!x->sdaw) x->byte++; }
            }
            break;
        }
    }
    x->scl = s;
}
static void w_sda(X76 *x, int s) {
    if (!x->cs && x->scl) {
        if (!x->sdaw && s) { x->state = ST_STOP; x->sdar = 0; }
        if (x->sdaw && !s) {
            if (x->state == ST_STOP) x->state = ST_CMD;
            x->bit = 0; x->byte = 0; x->shift = 0; x->sdar = 0;
        }
    }
    x->sdaw = s;
}
/* MAME hikaru: cs=!bit2, rst=!bit3, scl=bit1, sda=bit0 */
void x76_write(X76 *x, u32 v) {
    w_cs(x, !((v >> 2) & 1));
    w_rst(x, !((v >> 3) & 1));
    w_scl(x, (v >> 1) & 1);
    w_sda(x, v & 1);
}
int x76_read(X76 *x) { return x->cs ? 1 : x->sdar; }
