"""Minimal Z80 disassembler (x/y/z/p/q decoding). usage: z80dis.py <bin> <file_off_hex> <load_addr_hex> <len_hex>"""
import sys

R = ["b", "c", "d", "e", "h", "l", "(hl)", "a"]
RP = ["bc", "de", "hl", "sp"]
RP2 = ["bc", "de", "hl", "af"]
CC = ["nz", "z", "nc", "c", "po", "pe", "p", "m"]
ALU = ["add a,", "adc a,", "sub ", "sbc a,", "and ", "xor ", "or ", "cp "]
ROT = ["rlc", "rrc", "rl", "rr", "sla", "sra", "sll", "srl"]


def s8(v): return v - 256 if v > 127 else v


def dis(mem, base, pc):
    """returns (text, length)"""
    def b(i): return mem[pc - base + i]
    i = 0
    ix = None
    op = b(0)
    if op in (0xDD, 0xFD):
        ix = "ix" if op == 0xDD else "iy"
        i = 1
        op = b(1)
        if op in (0xDD, 0xFD, 0xED): return ("db 0x%02x" % b(0), 1)
    def rr(n):
        if ix:
            if n == 4: return ix + "h"
            if n == 5: return ix + "l"
        return R[n]
    def mem_hl(disp_idx):
        return "(%s%+d)" % (ix, s8(b(disp_idx))) if ix else "(hl)"
    def w(k): return b(k) | (b(k + 1) << 8)
    hlname = ix if ix else "hl"
    if op == 0xCB:
        if ix:
            d = s8(b(i + 1)); o = b(i + 2)
            x, y, z = o >> 6, (o >> 3) & 7, o & 7
            t = "(%s%+d)" % (ix, d)
            if x == 0: return ("%s %s" % (ROT[y], t), i + 3)
            return ("%s %d,%s" % (["", "bit", "res", "set"][x], y, t), i + 3)
        o = b(1); x, y, z = o >> 6, (o >> 3) & 7, o & 7
        if x == 0: return ("%s %s" % (ROT[y], R[z]), 2)
        return ("%s %d,%s" % (["", "bit", "res", "set"][x], y, R[z]), 2)
    if op == 0xED:
        o = b(1); x, y, z = o >> 6, (o >> 3) & 7, o & 7; p, q = y >> 1, y & 1
        if x == 1:
            if z == 0: return ("in %s,(c)" % ("f" if y == 6 else R[y]), 2)
            if z == 1: return ("out (c),%s" % ("0" if y == 6 else R[y]), 2)
            if z == 2: return ("%s hl,%s" % ("sbc" if q == 0 else "adc", RP[p]), 2)
            if z == 3:
                return (("ld (0x%04x),%s" % (w(2), RP[p])) if q == 0 else ("ld %s,(0x%04x)" % (RP[p], w(2))), 4)
            if z == 4: return ("neg", 2)
            if z == 5: return ("reti" if y == 1 else "retn", 2)
            if z == 6: return ("im %s" % ["0", "0/1", "1", "2"][y & 3], 2)
            return (["ld i,a", "ld r,a", "ld a,i", "ld a,r", "rrd", "rld", "nop", "nop"][y], 2)
        if x == 2 and z <= 3 and y >= 4:
            return ([["ldi", "cpi", "ini", "outi"], ["ldd", "cpd", "ind", "outd"],
                     ["ldir", "cpir", "inir", "otir"], ["lddr", "cpdr", "indr", "otdr"]][y - 4][z], 2)
        return ("db 0xed,0x%02x" % o, 2)
    x, y, z = op >> 6, (op >> 3) & 7, op & 7; p, q = y >> 1, y & 1
    n = i + 1  # index of next byte
    # displacement handling for (hl) operands with ix/iy
    def opnd(k):
        nonlocal n
        if k == 6:
            if ix:
                t = "(%s%+d)" % (ix, s8(b(n))); n += 1; return t
            return "(hl)"
        return rr(k) if not ix or True else R[k]
    if x == 0:
        if z == 0:
            if y == 0: return ("nop", n)
            if y == 1: return ("ex af,af'", n)
            tgt = (pc + n + 1 + s8(b(n))) & 0xffff
            if y == 2: return ("djnz 0x%04x" % tgt, n + 1)
            if y == 3: return ("jr 0x%04x" % tgt, n + 1)
            return ("jr %s,0x%04x" % (CC[y - 4], tgt), n + 1)
        if z == 1:
            if q == 0: return ("ld %s,0x%04x" % (hlname if p == 2 else RP[p], w(n)), n + 2)
            return ("add %s,%s" % (hlname, hlname if p == 2 else RP[p]), n)
        if z == 2:
            t = [("ld (bc),a", 0), ("ld a,(bc)", 0), ("ld (de),a", 0), ("ld a,(de)", 0),
                 ("ld (0x%04x),%s", 1), ("ld %s,(0x%04x)", 3), ("ld (0x%04x),a", 2), ("ld a,(0x%04x)", 2)][y]
            if t[1] == 0: return (t[0], n)
            if t[1] == 1: return (t[0] % (w(n), hlname), n + 2)
            if t[1] == 2: return (t[0] % w(n), n + 2)
            return (t[0] % (hlname, w(n)), n + 2)
        if z == 3: return (("inc " if q == 0 else "dec ") + (hlname if p == 2 else RP[p]), n)
        if z in (4, 5):
            t = opnd(y); return (("inc " if z == 4 else "dec ") + t, n)
        if z == 6:
            t = opnd(y); v = b(n); return ("ld %s,0x%02x" % (t, v), n + 1)
        return (["rlca", "rrca", "rla", "rra", "daa", "cpl", "scf", "ccf"][y], n)
    if x == 1:
        if y == 6 and z == 6: return ("halt", n)
        if y == 6 or z == 6:
            # with ix: the non-(hl) operand uses plain h/l
            if y == 6:
                t = opnd(6); return ("ld %s,%s" % (t, R[z]), n)
            t = opnd(6); return ("ld %s,%s" % (R[y], t), n)
        return ("ld %s,%s" % (rr(y), rr(z)), n)
    if x == 2:
        t = opnd(z); return (ALU[y] + t, n)
    # x == 3
    if z == 0: return ("ret " + CC[y], n)
    if z == 1:
        if q == 0: return ("pop " + (hlname if p == 2 else RP2[p]), n)
        return (["ret", "exx", "jp (%s)" % hlname, "ld sp,%s" % hlname][p], n)
    if z == 2: return ("jp %s,0x%04x" % (CC[y], w(n)), n + 2)
    if z == 3:
        if y == 0: return ("jp 0x%04x" % w(n), n + 2)
        if y == 2: return ("out (0x%02x),a" % b(n), n + 1)
        if y == 3: return ("in a,(0x%02x)" % b(n), n + 1)
        return (["", "", "", "", "ex (sp),%s" % hlname, "ex de,hl", "di", "ei"][y], n)
    if z == 4: return ("call %s,0x%04x" % (CC[y], w(n)), n + 2)
    if z == 5:
        if q == 0: return ("push " + (hlname if p == 2 else RP2[p]), n)
        if p == 0: return ("call 0x%04x" % w(n), n + 2)
        return ("db 0x%02x" % op, n)
    if z == 6: return (ALU[y] + "0x%02x" % b(n), n + 1)
    return ("rst 0x%02x" % (y * 8), n)


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    off, la, ln = int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4], 16)
    mem = data[off:off + ln] + b"\0\0\0\0"
    pc = la
    while pc < la + ln:
        t, l = dis(mem, la, pc)
        print("%04x: %-12s %s" % (pc, mem[pc - la:pc - la + l].hex(), t))
        pc += l
