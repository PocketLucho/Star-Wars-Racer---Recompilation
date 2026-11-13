"""SH-4 instruction decoder (little-endian, 16-bit opcodes).

decode(op) -> Insn with mnemonic, operand fields and flow info.
Used by the disassembler, the function catalogue and the SH-4 -> C lifter.
"""
from dataclasses import dataclass, field

@dataclass
class Insn:
    op: int
    mn: str            # canonical mnemonic key, e.g. "mov.l @(disp,pc),rn"
    n: int = 0
    m: int = 0
    imm: int = 0       # immediate / displacement (already scaled where noted)
    text: str = ""     # printable operands template
    flow: str = ""     # '', 'br','bra','bsr','jmp','jsr','rts','rte','bt','bf','bts','bfs','braf','bsrf','trap','sleep'
    delay: bool = False
    illegal: bool = False

def s8(x):  return x - 0x100 if x & 0x80 else x
def s12(x): return x - 0x1000 if x & 0x800 else x

# table: (mask, value, mnemonic, fmt)
# fmt letters: n=Rn(11:8) m=Rm(7:4) i=imm8 d=disp
T = []
def add(pat, mn, flow="", delay=False):
    # pat: 16-char string of 0/1/n/m/i/d
    mask = val = 0
    for c in pat:
        mask <<= 1; val <<= 1
        if c in "01":
            mask |= 1
            if c == "1": val |= 1
    T.append((mask, val, mn, pat, flow, delay))

# --- 0000 group
add("0000000000001000", "clrt")
add("0000000000101000", "clrmac")
add("0000000001001000", "clrs")
add("0000000000011000", "sett")
add("0000000001011000", "sets")
add("0000000000001001", "nop")
add("0000000000001011", "rts", "rts", True)
add("0000000000101011", "rte", "rte", True)
add("0000000000011011", "sleep", "sleep")
add("0000000000011001", "div0u")
add("0000000000111000", "ldtlb")
add("0000nnnn00100011", "braf rn", "braf", True)
add("0000nnnn00000011", "bsrf rn", "bsrf", True)
add("0000nnnn00101001", "movt rn")
add("0000nnnn10000011", "pref @rn")
add("0000nnnn10010011", "ocbi @rn")
add("0000nnnn10100011", "ocbp @rn")
add("0000nnnn10110011", "ocbwb @rn")
add("0000nnnn11000011", "movca.l r0,@rn")
add("0000nnnn00000010", "stc sr,rn")
add("0000nnnn00010010", "stc gbr,rn")
add("0000nnnn00100010", "stc vbr,rn")
add("0000nnnn00110010", "stc ssr,rn")
add("0000nnnn01000010", "stc spc,rn")
add("0000nnnn00111010", "stc sgr,rn")
add("0000nnnn11111010", "stc dbr,rn")
add("0000nnnn1mmm0010", "stc rm_bank,rn")
add("0000nnnn00001010", "sts mach,rn")
add("0000nnnn00011010", "sts macl,rn")
add("0000nnnn00101010", "sts pr,rn")
add("0000nnnn01011010", "sts fpul,rn")
add("0000nnnn01101010", "sts fpscr,rn")
add("0000nnnnmmmm0100", "mov.b rm,@(r0,rn)")
add("0000nnnnmmmm0101", "mov.w rm,@(r0,rn)")
add("0000nnnnmmmm0110", "mov.l rm,@(r0,rn)")
add("0000nnnnmmmm0111", "mul.l rm,rn")
add("0000nnnnmmmm1100", "mov.b @(r0,rm),rn")
add("0000nnnnmmmm1101", "mov.w @(r0,rm),rn")
add("0000nnnnmmmm1110", "mov.l @(r0,rm),rn")
add("0000nnnnmmmm1111", "mac.l @rm+,@rn+")
# --- 0001
add("0001nnnnmmmmdddd", "mov.l rm,@(disp,rn)")
# --- 0010
add("0010nnnnmmmm0000", "mov.b rm,@rn")
add("0010nnnnmmmm0001", "mov.w rm,@rn")
add("0010nnnnmmmm0010", "mov.l rm,@rn")
add("0010nnnnmmmm0100", "mov.b rm,@-rn")
add("0010nnnnmmmm0101", "mov.w rm,@-rn")
add("0010nnnnmmmm0110", "mov.l rm,@-rn")
add("0010nnnnmmmm0111", "div0s rm,rn")
add("0010nnnnmmmm1000", "tst rm,rn")
add("0010nnnnmmmm1001", "and rm,rn")
add("0010nnnnmmmm1010", "xor rm,rn")
add("0010nnnnmmmm1011", "or rm,rn")
add("0010nnnnmmmm1100", "cmp/str rm,rn")
add("0010nnnnmmmm1101", "xtrct rm,rn")
add("0010nnnnmmmm1110", "mulu.w rm,rn")
add("0010nnnnmmmm1111", "muls.w rm,rn")
# --- 0011
add("0011nnnnmmmm0000", "cmp/eq rm,rn")
add("0011nnnnmmmm0010", "cmp/hs rm,rn")
add("0011nnnnmmmm0011", "cmp/ge rm,rn")
add("0011nnnnmmmm0100", "div1 rm,rn")
add("0011nnnnmmmm0101", "dmulu.l rm,rn")
add("0011nnnnmmmm0110", "cmp/hi rm,rn")
add("0011nnnnmmmm0111", "cmp/gt rm,rn")
add("0011nnnnmmmm1000", "sub rm,rn")
add("0011nnnnmmmm1010", "subc rm,rn")
add("0011nnnnmmmm1011", "subv rm,rn")
add("0011nnnnmmmm1100", "add rm,rn")
add("0011nnnnmmmm1101", "dmuls.l rm,rn")
add("0011nnnnmmmm1110", "addc rm,rn")
add("0011nnnnmmmm1111", "addv rm,rn")
# --- 0100
add("0100nnnn00000000", "shll rn")
add("0100nnnn00000001", "shlr rn")
add("0100nnnn00000100", "rotl rn")
add("0100nnnn00000101", "rotr rn")
add("0100nnnn00001000", "shll2 rn")
add("0100nnnn00001001", "shlr2 rn")
add("0100nnnn00011000", "shll8 rn")
add("0100nnnn00011001", "shlr8 rn")
add("0100nnnn00101000", "shll16 rn")
add("0100nnnn00101001", "shlr16 rn")
add("0100nnnn00010000", "dt rn")
add("0100nnnn00010001", "cmp/pz rn")
add("0100nnnn00010101", "cmp/pl rn")
add("0100nnnn00100000", "shal rn")
add("0100nnnn00100001", "shar rn")
add("0100nnnn00100100", "rotcl rn")
add("0100nnnn00100101", "rotcr rn")
add("0100nnnn00011011", "tas.b @rn")
add("0100nnnn00001011", "jsr @rn", "jsr", True)
add("0100nnnn00101011", "jmp @rn", "jmp", True)
add("0100nnnnmmmm1100", "shad rm,rn")
add("0100nnnnmmmm1101", "shld rm,rn")
add("0100nnnnmmmm1111", "mac.w @rm+,@rn+")
add("0100nnnn00001110", "ldc rn,sr")
add("0100nnnn00011110", "ldc rn,gbr")
add("0100nnnn00101110", "ldc rn,vbr")
add("0100nnnn00111110", "ldc rn,ssr")
add("0100nnnn01001110", "ldc rn,spc")
add("0100nnnn11111010", "ldc rn,dbr")
add("0100nnnn1mmm1110", "ldc rn,rm_bank")
add("0100nnnn00000111", "ldc.l @rn+,sr")
add("0100nnnn00010111", "ldc.l @rn+,gbr")
add("0100nnnn00100111", "ldc.l @rn+,vbr")
add("0100nnnn00110111", "ldc.l @rn+,ssr")
add("0100nnnn01000111", "ldc.l @rn+,spc")
add("0100nnnn11110110", "ldc.l @rn+,dbr")
add("0100nnnn1mmm0111", "ldc.l @rn+,rm_bank")
add("0100nnnn00000011", "stc.l sr,@-rn")
add("0100nnnn00010011", "stc.l gbr,@-rn")
add("0100nnnn00100011", "stc.l vbr,@-rn")
add("0100nnnn00110011", "stc.l ssr,@-rn")
add("0100nnnn01000011", "stc.l spc,@-rn")
add("0100nnnn00110010", "stc.l sgr,@-rn")
add("0100nnnn11110010", "stc.l dbr,@-rn")
add("0100nnnn1mmm0011", "stc.l rm_bank,@-rn")
add("0100nnnn00001010", "lds rn,mach")
add("0100nnnn00011010", "lds rn,macl")
add("0100nnnn00101010", "lds rn,pr")
add("0100nnnn01011010", "lds rn,fpul")
add("0100nnnn01101010", "lds rn,fpscr")
add("0100nnnn00000110", "lds.l @rn+,mach")
add("0100nnnn00010110", "lds.l @rn+,macl")
add("0100nnnn00100110", "lds.l @rn+,pr")
add("0100nnnn01010110", "lds.l @rn+,fpul")
add("0100nnnn01100110", "lds.l @rn+,fpscr")
add("0100nnnn00000010", "sts.l mach,@-rn")
add("0100nnnn00010010", "sts.l macl,@-rn")
add("0100nnnn00100010", "sts.l pr,@-rn")
add("0100nnnn01010010", "sts.l fpul,@-rn")
add("0100nnnn01100010", "sts.l fpscr,@-rn")
# --- 0101
add("0101nnnnmmmmdddd", "mov.l @(disp,rm),rn")
# --- 0110
add("0110nnnnmmmm0000", "mov.b @rm,rn")
add("0110nnnnmmmm0001", "mov.w @rm,rn")
add("0110nnnnmmmm0010", "mov.l @rm,rn")
add("0110nnnnmmmm0011", "mov rm,rn")
add("0110nnnnmmmm0100", "mov.b @rm+,rn")
add("0110nnnnmmmm0101", "mov.w @rm+,rn")
add("0110nnnnmmmm0110", "mov.l @rm+,rn")
add("0110nnnnmmmm0111", "not rm,rn")
add("0110nnnnmmmm1000", "swap.b rm,rn")
add("0110nnnnmmmm1001", "swap.w rm,rn")
add("0110nnnnmmmm1010", "negc rm,rn")
add("0110nnnnmmmm1011", "neg rm,rn")
add("0110nnnnmmmm1100", "extu.b rm,rn")
add("0110nnnnmmmm1101", "extu.w rm,rn")
add("0110nnnnmmmm1110", "exts.b rm,rn")
add("0110nnnnmmmm1111", "exts.w rm,rn")
# --- 0111
add("0111nnnniiiiiiii", "add #imm,rn")
# --- 1000
add("10000000nnnndddd", "mov.b r0,@(disp,rn)")
add("10000001nnnndddd", "mov.w r0,@(disp,rn)")
add("10000100mmmmdddd", "mov.b @(disp,rm),r0")
add("10000101mmmmdddd", "mov.w @(disp,rm),r0")
add("10001000iiiiiiii", "cmp/eq #imm,r0")
add("10001001dddddddd", "bt label", "bt")
add("10001011dddddddd", "bf label", "bf")
add("10001101dddddddd", "bt/s label", "bts", True)
add("10001111dddddddd", "bf/s label", "bfs", True)
# --- 1001
add("1001nnnndddddddd", "mov.w @(disp,pc),rn")
# --- 1010 / 1011
add("1010dddddddddddd", "bra label", "bra", True)
add("1011dddddddddddd", "bsr label", "bsr", True)
# --- 1100
add("11000000dddddddd", "mov.b r0,@(disp,gbr)")
add("11000001dddddddd", "mov.w r0,@(disp,gbr)")
add("11000010dddddddd", "mov.l r0,@(disp,gbr)")
add("11000011iiiiiiii", "trapa #imm", "trap")
add("11000100dddddddd", "mov.b @(disp,gbr),r0")
add("11000101dddddddd", "mov.w @(disp,gbr),r0")
add("11000110dddddddd", "mov.l @(disp,gbr),r0")
add("11000111dddddddd", "mova @(disp,pc),r0")
add("11001000iiiiiiii", "tst #imm,r0")
add("11001001iiiiiiii", "and #imm,r0")
add("11001010iiiiiiii", "xor #imm,r0")
add("11001011iiiiiiii", "or #imm,r0")
add("11001100iiiiiiii", "tst.b #imm,@(r0,gbr)")
add("11001101iiiiiiii", "and.b #imm,@(r0,gbr)")
add("11001110iiiiiiii", "xor.b #imm,@(r0,gbr)")
add("11001111iiiiiiii", "or.b #imm,@(r0,gbr)")
# --- 1101 / 1110
add("1101nnnndddddddd", "mov.l @(disp,pc),rn")
add("1110nnnniiiiiiii", "mov #imm,rn")
# --- 1111 FPU
add("1111nnnnmmmm0000", "fadd frm,frn")
add("1111nnnnmmmm0001", "fsub frm,frn")
add("1111nnnnmmmm0010", "fmul frm,frn")
add("1111nnnnmmmm0011", "fdiv frm,frn")
add("1111nnnnmmmm0100", "fcmp/eq frm,frn")
add("1111nnnnmmmm0101", "fcmp/gt frm,frn")
add("1111nnnnmmmm0110", "fmov.s @(r0,rm),frn")
add("1111nnnnmmmm0111", "fmov.s frm,@(r0,rn)")
add("1111nnnnmmmm1000", "fmov.s @rm,frn")
add("1111nnnnmmmm1001", "fmov.s @rm+,frn")
add("1111nnnnmmmm1010", "fmov.s frm,@rn")
add("1111nnnnmmmm1011", "fmov.s frm,@-rn")
add("1111nnnnmmmm1100", "fmov frm,frn")
add("1111nnnnmmmm1110", "fmac fr0,frm,frn")
add("1111nnnn00001101", "fsts fpul,frn")
add("1111nnnn00011101", "flds frn,fpul")
add("1111nnnn00101101", "float fpul,frn")
add("1111nnnn00111101", "ftrc frn,fpul")
add("1111nnnn01001101", "fneg frn")
add("1111nnnn01011101", "fabs frn")
add("1111nnnn01101101", "fsqrt frn")
add("1111nnnn01111101", "fsrra frn")
add("1111nnnn10001101", "fldi0 frn")
add("1111nnnn10011101", "fldi1 frn")
add("1111nnn010111101", "fcnvsd fpul,drn")
add("1111nnn011111101", "fsca fpul,drn")
add("1111mmm010111101", "fcnvsd fpul,drn")
add("1111nnn011101101", "fipr fvm,fvn")   # nnmm encoded in 11:8 -> handled in decode
add("1111nn0111111101", "ftrv xmtrx,fvn")
add("1111001111111101", "fschg")
add("1111101111111101", "frchg")
add("1111nnnn10111101", "fcnvds drn,fpul")

# fipr is 1111nnmm11101101 : fix pattern
T = [t for t in T if t[2] != "fipr fvm,fvn"]
add("1111nnmm11101101", "fipr fvm,fvn")
# fcnvsd 1111nnn010101101, fcnvds 1111mmm010111101 (correct encodings)
T = [t for t in T if not t[2].startswith("fcnv")]
add("1111nnn010101101", "fcnvsd fpul,drn")
add("1111mmm010111101", "fcnvds drm,fpul")
# fsca correct: 1111nnn011111101 ; ftrv: 1111nn0111111101 ; fschg 1111001111111101 ; frchg 1111101111111101
# order matters: most specific (more fixed bits) first
T.sort(key=lambda t: -bin(t[0]).count("1"))

_cache = {}

def decode(op):
    r = _cache.get(op)
    if r is not None:
        return r
    for mask, val, mn, pat, flow, delay in T:
        if op & mask == val:
            n = (op >> 8) & 15
            m = (op >> 4) & 15
            ins = Insn(op, mn, n, m, 0, "", flow, delay)
            low8 = op & 0xff
            low4 = op & 0xf
            if mn.startswith("ldc rn,rm_bank") or mn.startswith("ldc.l @rn+,rm_bank") \
               or mn.startswith("stc rm_bank") or mn.startswith("stc.l rm_bank"):
                ins.m = (op >> 4) & 7
            if mn in ("fipr fvm,fvn",):
                ins.n = (op >> 10) & 3; ins.m = (op >> 8) & 3
            if mn in ("ftrv xmtrx,fvn",):
                ins.n = (op >> 10) & 3
            if mn in ("fcnvsd fpul,drn", "fsca fpul,drn"):
                ins.n = (op >> 9) & 7
            if mn == "fcnvds drm,fpul":
                ins.m = (op >> 9) & 7
            # immediates/displacements
            if "iiiiiiii" in pat:
                if mn in ("add #imm,rn", "mov #imm,rn", "cmp/eq #imm,r0"):
                    ins.imm = s8(low8)
                else:
                    ins.imm = low8
            if mn == "mov.l rm,@(disp,rn)" or mn == "mov.l @(disp,rm),rn":
                ins.imm = low4 * 4
            elif mn in ("mov.b r0,@(disp,rn)", "mov.b @(disp,rm),r0"):
                ins.imm = low4; ins.n = ins.n if mn.startswith("mov.b r0") else ins.n
                if mn == "mov.b r0,@(disp,rn)": ins.n = (op >> 4) & 15
                else: ins.m = (op >> 4) & 15
            elif mn in ("mov.w r0,@(disp,rn)", "mov.w @(disp,rm),r0"):
                ins.imm = low4 * 2
                if mn == "mov.w r0,@(disp,rn)": ins.n = (op >> 4) & 15
                else: ins.m = (op >> 4) & 15
            elif mn in ("bt label", "bf label", "bt/s label", "bf/s label"):
                ins.imm = s8(low8) * 2 + 4          # relative to insn addr
            elif mn in ("bra label", "bsr label"):
                ins.imm = s12(op & 0xfff) * 2 + 4
            elif mn == "mov.w @(disp,pc),rn":
                ins.imm = low8 * 2 + 4              # target = pc+4+disp*2
            elif mn == "mov.l @(disp,pc),rn":
                ins.imm = low8 * 4                  # target = (pc&~3)+4+disp*4
            elif mn == "mova @(disp,pc),r0":
                ins.imm = low8 * 4
            elif mn.endswith("@(disp,gbr)") or mn.startswith("mov.b @(disp,gbr)") \
                 or mn.startswith("mov.w @(disp,gbr)") or mn.startswith("mov.l @(disp,gbr)"):
                sc = {"b": 1, "w": 2, "l": 4}[mn[4]]
                ins.imm = low8 * sc
            _cache[op] = ins
            return ins
    ins = Insn(op, "illegal", illegal=True)
    _cache[op] = ins
    return ins

def pcrel_target(ins, pc):
    """Absolute address referenced by PC-relative operand (branch target or literal)."""
    mn = ins.mn
    if mn in ("bt label", "bf label", "bt/s label", "bf/s label", "bra label", "bsr label",
              "mov.w @(disp,pc),rn"):
        return (pc + ins.imm) & 0xffffffff
    if mn in ("mov.l @(disp,pc),rn", "mova @(disp,pc),r0"):
        return ((pc & ~3) + 4 + ins.imm) & 0xffffffff
    return None

def fmt(ins, pc, rd32=None, rd16=None, names=None):
    """Pretty-print an instruction at pc. rd32/rd16 read memory for literal annotation."""
    if ins.illegal:
        return f".word 0x{ins.op:04x}"
    mn = ins.mn
    parts = mn.split(" ", 1)
    opn = parts[0]; ops = parts[1] if len(parts) > 1 else ""
    tgt = pcrel_target(ins, pc)
    comment = ""
    def nm(a):
        if names and a in names: return names[a]
        return f"0x{a:08x}"
    if tgt is not None:
        if "label" in ops:
            ops = ops.replace("label", nm(tgt))
        elif "@(disp,pc)" in ops:
            ops = ops.replace("@(disp,pc)", f"@(0x{tgt:08x})")
            if opn == "mov.l" and rd32:
                v = rd32(tgt)
                if v is not None: comment = f"  ; =0x{v:08x}" + (f" {names[v]}" if names and v in names else "")
            elif opn == "mov.w" and rd16:
                v = rd16(tgt)
                if v is not None: comment = f"  ; =0x{v:04x}"
    ops = ops.replace("#imm", f"#{ins.imm}" if ins.imm < 0 or ins.imm < 10 else f"#0x{ins.imm:x}")
    ops = ops.replace("disp", f"{ins.imm}")
    ops = ops.replace("frn", f"fr{ins.n}").replace("frm", f"fr{ins.m}")
    ops = ops.replace("drn", f"dr{ins.n*2}").replace("drm", f"dr{ins.m*2}")
    ops = ops.replace("fvn", f"fv{ins.n*4}").replace("fvm", f"fv{ins.m*4}")
    ops = ops.replace("rm_bank", f"r{ins.m}_bank")
    ops = ops.replace("rn", f"r{ins.n}").replace("rm", f"r{ins.m}")
    return (f"{opn:8s}{ops}" + comment).rstrip()
