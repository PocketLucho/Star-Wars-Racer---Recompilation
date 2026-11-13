#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hikaru.h"
#include "mie.h"
#include "z80.h"

HikaruInput g_input;

static z80 Z;
static int z80_started;
static uint8_t jvs_control, jvs_lcr, jvs_dest;
static uint8_t irq_enable, irq_pending, maple_irqlevel = 0xff;
static uint8_t gpiodir = 0xff;
static uint8_t send_buf[1024], recv_buf[1024];
static uint32_t send_size, recv_size, jvs_rpos;
static uint8_t jvs_address = 0xff, jvs_reset_counter;
static uint64_t stat_cmds, stat_jvs_msgs, stat_steps;

#define LOG(...) do { if (g_m && g_m->log) fprintf(g_m->log, __VA_ARGS__); } while (0)

/* ---------------- JVS: 837-13551 I/O board ---------------- */
static const char *jvs_id = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551";

static int jvs_handle(const uint8_t *s, uint32_t n, uint8_t **dp) {
    uint8_t *d = *dp;
    int old_reset = jvs_reset_counter; jvs_reset_counter = 0;
    int r;
    switch (s[0]) {
    case 0xf0:
        if (n < 2 || s[1] != 0xd9) return 0;
        jvs_reset_counter = old_reset + 1;
        if (jvs_reset_counter == 2) { jvs_address = 0xff; jvs_reset_counter = 0; }
        r = 2; break;
    case 0x10: { int l = (int)strlen(jvs_id) + 1; *d++ = 1; memcpy(d, jvs_id, l); d += l; r = 1; break; }
    case 0x11: *d++ = 1; *d++ = 0x11; r = 1; break;
    case 0x12: *d++ = 1; *d++ = 0x20; r = 1; break;
    case 0x13: *d++ = 1; *d++ = 0x10; r = 1; break;
    case 0x14:
        *d++ = 1;
        *d++ = 0x01; *d++ = 2; *d++ = 13; *d++ = 0;   /* switches: 2 players, 13 bits */
        *d++ = 0x02; *d++ = 2; *d++ = 0;  *d++ = 0;   /* coins: 2 slots */
        *d++ = 0x03; *d++ = 8; *d++ = 16; *d++ = 0;   /* analog: 8 ch, 16 bit */
        *d++ = 0x12; *d++ = 6; *d++ = 0;  *d++ = 0;   /* driver outputs: 6 */
        *d++ = 0x00; r = 1; break;
    case 0x20:
        if (n < 3 || s[1] > 2 || s[2] > 2) return 0;
        *d++ = 1; *d++ = g_input.tilt;
        for (int i = 0; i < s[1]; i++) for (int j = 0; j < s[2]; j++) *d++ = (uint8_t)(g_input.p[i] >> ((1 - j) * 8));
        r = 3; break;
    case 0x21:
        if (n < 2 || s[1] > 2) return 0;
        *d++ = 1;
        for (int i = 0; i < s[1]; i++) { *d++ = g_input.coins[i] >> 8; *d++ = (uint8_t)g_input.coins[i]; }
        r = 2; break;
    case 0x22:
        if (n < 2 || s[1] > 8) return 0;
        *d++ = 1;
        for (int i = 0; i < s[1]; i++) { *d++ = g_input.analog[i] >> 8; *d++ = (uint8_t)g_input.analog[i]; }
        r = 2; break;
    case 0x30: case 0x31:
        if (n < 4 || s[1] < 1 || s[1] > 2) return 0;
        *d++ = 1;
        { int v = (s[2] << 8) | s[3]; g_input.coins[s[1] - 1] += (s[0] == 0x31) ? v : -v; }
        r = 4; break;
    case 0x32:
        if (n < 2 || n < 2u + s[1] || s[1] > 1) return 0;
        *d++ = 1;
        if (s[1]) { if (g_input.outputs != (s[2] & 0xfc)) LOG("JVS outputs %02x\n", s[2] & 0xfc); g_input.outputs = s[2] & 0xfc; }
        r = 2 + s[1]; break;
    case 0x38:
        if (n < 3 || s[1] > 6) return 0;
        *d++ = 1;
        { uint8_t m = 1 << s[1]; if (s[2] == 0) g_input.outputs &= ~m; else if (s[2] == 1) g_input.outputs |= m; else g_input.outputs ^= m; }
        r = 3; break;
    default:
        LOG("JVS: unhandled command %02x\n", s[0]);
        return 0;
    }
    *dp = d;
    return r;
}

static void jvs_message(uint8_t dest, const uint8_t *s, uint32_t n, uint8_t *d0, uint32_t *rsz) {
    *rsz = 0;
    if (n == 2 && s[0] == 0xf1) { jvs_address = s[1]; d0[0] = 1; d0[1] = 1; *rsz = 2; return; }
    if (dest == 0xff || dest == jvs_address) {
        const uint8_t *p = s; uint8_t *d = d0;
        *d++ = 0x01;
        while (p < s + n) {
            int len = jvs_handle(p, (uint32_t)(n - (p - s)), &d);
            if (len == 0) { *d++ = 0x02; break; }
            p += len;
        }
        *rsz = (uint32_t)(d - d0);
    }
}

static void jvs_decode(uint8_t *b, uint32_t *size) {
    uint32_t pos = 0, n = *size;
    if (!n) return;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t t = b[i];
        if (!i && t == 0xe0) continue;
        if (t == 0xd0) { i++; t = b[i] + 1; }
        b[pos++] = t;
    }
    *size = pos ? pos - 1 : 0;
}

static void jvs_encode(uint8_t *b, uint32_t *size) {
    uint32_t n = *size, add = 1; uint8_t sum = 0;
    if (!n) return;
    for (uint32_t i = 0; i < n; i++) sum += b[i];
    b[n++] = sum;
    for (uint32_t i = 0; i < n; i++) if (b[i] == 0xd0 || b[i] == 0xe0) add++;
    for (uint32_t i = n, j = add; i; i--) {
        uint8_t t = b[i - 1];
        if (t == 0xd0 || t == 0xe0) { b[i + j - 1] = t - 1; b[i + j - 2] = 0xd0; j--; }
        else b[i + j - 1] = t;
    }
    b[0] = 0xe0;
    *size = n + add;
}

static void jvs_commit(void) {
    recv_size = 0;
    if (!send_size) return;
    jvs_decode(send_buf, &send_size);
    if (send_size < 3 || send_buf[0] == 0 || send_buf[1] != send_size - 1) {
        recv_buf[0] = 0; recv_buf[1] = 2; recv_buf[2] = 3; recv_size = 3;
    } else {
        uint32_t r = 0;
        jvs_message(send_buf[0], send_buf + 2, send_size - 2, recv_buf + 2, &r);
        if (r) { recv_buf[0] = 0; recv_buf[1] = r + 1; recv_size = r + 2; }
    }
    stat_jvs_msgs++;
    if (g_m && g_m->log) {
        LOG("JVS > dst=%02x", send_buf[0]); for (uint32_t i = 2; i < send_size; i++) LOG(" %02x", send_buf[i]);
        LOG("  < "); for (uint32_t i = 0; i < recv_size && i < 24; i++) LOG(" %02x", recv_buf[i]); LOG("\n");
    }
    jvs_encode(recv_buf, &recv_size);
    send_size = 0;
}

/* ---------------- MIE ports (shared by host and Z80) ---------------- */
uint8_t mie_port_r(Machine *m, uint8_t port, int from_host) {
    switch (port) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
        if (port == 1) return (uint8_t)m->mie_irq_pending;
        if (port == 6) {                                            /* G */
            uint8_t v = 0xff;
            /* the MIE firmware reports bit 3 as TEST and bit 2 as SERVICE (MIE c005 bits 7/6, which the BIOS
               menu reads as select / move); they were swapped before, so TEST could not confirm menu options */
            if (g_input.test) v &= ~0x08;
            if (g_input.service) v &= ~0x04;
            v &= ~0x10;                                             /* fan sensor ok */
            if (!m->mie_eep.dout) v &= ~0x01;                       /* bit 0: MIE 93C46 DO (Z80 fw: in a,(6); rra) */
            return v;
        }
        return m->mie_port[port];
    case 0x08: return gpiodir;
    case 0x0f: return 0;
    case 0x10:
        if (jvs_lcr & 0x80) return 0;
        return jvs_rpos < recv_size ? recv_buf[jvs_rpos++] : 0;
    case 0x15: return 0x60 | (jvs_rpos < recv_size ? 1 : 0);
    case 0x30: return irq_enable;
    case 0x50: return maple_irqlevel;
    case 0x70: return irq_pending;
    case 0x91: return 0x8c | (jvs_address != 0xff ? 2 : 0);          /* device present, address-set line */
    default: return m->mie_port[port];
    }
}

void mie_irq_update(Machine *m);
void mie_port_w(Machine *m, uint8_t port, uint8_t v, int from_host) {
    switch (port) {
    case 0x08: gpiodir = v; m->mie_port[port] = v; return;
    case 0x10: if (!(jvs_lcr & 0x80) && send_size < sizeof send_buf) send_buf[send_size++] = v; return;
    case 0x12: jvs_dest = v; return;
    case 0x13: jvs_lcr = v; return;
    case 0x30: irq_enable = v; return;
    case 0x50: maple_irqlevel = v; return;
    case 0x70: irq_pending = v; return;
    case 0x90:
        if ((jvs_control & 1) && !(v & 1)) { jvs_commit(); jvs_rpos = 0; }
        jvs_control = v; return;
    default:
        m->mie_port[port] = v;
        /* F: CS bit3, DI bit1, SK bit0 of the MIE 93C46 (BIOS system settings). The BIOS reads it directly at
           boot (host writes a080000a, reads DO on port G) and the Z80 firmware writes it on command 7. */
        /* port F: 93C46 CS bit3, DI bit1, SK bit0 (DO = port G bit 0). Driven by the Z80 firmware (saves, command 7)
           and directly by the SH-4 BIOS at boot (reads the settings into 0C000980) */
        if (port == 5) eep_update(&m->mie_eep, (v >> 3) & 1, v & 1, (v >> 1) & 1);
        if (from_host && port <= 7) mie_irq_update(m);
        return;
    }
}

/* ---------------- Z80 ---------------- */
static uint8_t zrd(void *u, uint16_t a) {
    Machine *m = u;
    if (a < 0x0800) return 0xff;                         /* internal ROM: absent */
    if (a < 0x7000) return 0xff;
    if (a < 0x7800) return m->mie_mem[0x7000 | (a & 0x3f)];  /* maple regs, unused on Hikaru */
    return m->mie_mem[a];
}
static void zwr(void *u, uint16_t a, uint8_t v) {
    Machine *m = u;
    if (a < 0x7000) return;
    if (a < 0x7800) { m->mie_mem[0x7000 | (a & 0x3f)] = v; return; }
    if (a == 0xc000 || a == 0xc001) LOG("MIE z80 wr %04x <- %02x (pc=%04x)\n", a, v, Z.pc);
    m->mie_mem[a] = v;
}
static uint8_t zin(z80 *z, uint8_t p) { return mie_port_r((Machine *)z->userdata, p, 0); }
static void zout(z80 *z, uint8_t p, uint8_t v) { mie_port_w((Machine *)z->userdata, p, v, 0); }

void mie_init(Machine *m) {
    z80_init(&Z);
    Z.read_byte = zrd; Z.write_byte = zwr; Z.port_in = zin; Z.port_out = zout; Z.userdata = m;
    z80_started = 0;
}

void mie_busrq_w(Machine *m, int state) {
    if (state != m->mie_busrq) LOG("MIE busrq %d (master pc=%08x) z80 pc=%04x\n", state, m->cpu[0].pc, Z.pc);
    if (m->mie_busrq && !state) {
        /* bus released: if firmware was uploaded and the Z80 is not running it yet, boot it
           (stands in for the 315-6146 internal ROM, which jumps to 0x8010 once RAM holds code) */
        if (!z80_started && m->mie_mem[0x8000] == 0x18) {
            z80_init(&Z);
            Z.read_byte = zrd; Z.write_byte = zwr; Z.port_in = zin; Z.port_out = zout; Z.userdata = m;
            Z.pc = 0x8010; Z.sp = 0;
            z80_started = 1;
            LOG("MIE: Z80 firmware started at 8010\n");
        }
    }
    m->mie_busrq = state;
}

void mie_run(Machine *m, int cycles) {
    if (!z80_started || m->mie_busrq) return;
    uint64_t target = Z.cyc + (uint64_t)cycles;
    while (Z.cyc < target) {
        uint16_t pc = Z.pc;
        if (pc >= 0x8046 && pc <= 0x804e && m->mie_mem[0xc000] == 0) { Z.cyc = target; break; }  /* idle loop */
        if (pc == 0x8760) {            /* firmware busy-wait (a * ~24 ms): skip it, the host only polls the mailbox */
            Z.pc = m->mie_mem[Z.sp] | (m->mie_mem[(uint16_t)(Z.sp + 1)] << 8); Z.sp += 2; Z.a = 0; continue;
        }
        z80_step(&Z); stat_steps++;
        if (pc == 0x8046 && m->mie_mem[0xc000]) stat_cmds++;
    }
}

void mie_stats(FILE *f) {
    fprintf(f, "MIE: started=%d pc=%04x z80 steps=%llu cmds~%llu jvs msgs=%llu jvs addr=%02x outputs=%02x\n",
            z80_started, Z.pc, (unsigned long long)stat_steps, (unsigned long long)stat_cmds,
            (unsigned long long)stat_jvs_msgs, jvs_address, g_input.outputs);
}
