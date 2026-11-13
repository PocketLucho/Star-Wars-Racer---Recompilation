#pragma once
#include "sh4.h"
typedef struct {
    int cs, rst, scl, sdaw, sdar, state, bit, byte, accepted;
    u32 shift; u8 command;
    u8 rtr[4], wbuf[8], data[112];
} X76;
void x76_init(X76 *x);
void x76_write(X76 *x, u32 v);
int x76_read(X76 *x);
