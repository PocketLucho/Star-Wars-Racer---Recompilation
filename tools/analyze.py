"""Static + dynamic function catalogue for an SH-4 image.

usage: analyze.py <image.bin> <base_hex> <out_prefix> [--exec execmap.bin --calls callmap.bin --ram-base 0c000000]
                  [--seed addr ...] [--range lo hi]

Produces <out_prefix>.json (functions, blocks, xrefs, string hints),
<out_prefix>.csv and <out_prefix>_ghidra.py (label/function import script).
"""
import sys, os, struct, json, re, argparse, collections
sys.path.insert(0, os.path.dirname(__file__))
from sh4dec import decode, pcrel_target, fmt

ap = argparse.ArgumentParser()
ap.add_argument("image"); ap.add_argument("base"); ap.add_argument("out")
ap.add_argument("--exec"); ap.add_argument("--calls"); ap.add_argument("--ram-base", default="0c000000")
ap.add_argument("--seed", nargs="*", default=[])
ap.add_argument("--range", nargs=2, default=None, help="code range lo hi (hex) for prologue scan")
ap.add_argument("--name", default="game")
A = ap.parse_args()

img = open(A.image, "rb").read()
BASE = int(A.base, 16)
END = BASE + len(img)
def phys(a): return a & 0x1fffffff
def inimg(a): return BASE <= phys(a) < END - 1
def r16(a):
    a = phys(a); return struct.unpack_from("<H", img, a - BASE)[0] if BASE <= a < END - 1 else None
def r32(a):
    a = phys(a); return struct.unpack_from("<I", img, a - BASE)[0] if BASE <= a < END - 3 else None

# ---- strings
strings = {}
for m in re.finditer(rb"[\x20-\x7e]{5,}\x00", img):
    s = m.group()[:-1].decode("ascii")
    if sum(c.isalpha() for c in s) >= 3:
        strings[BASE + m.start()] = s

# ---- dynamic info
def bits(path, rbase):
    out = set()
    if not path: return out
    d = open(path, "rb").read()
    for i, b in enumerate(d):
        if b:
            for k in range(8):
                if b >> k & 1: out.add(rbase + ((i * 8 + k) << 1))
    return out
RB = int(A.ram_base, 16)
dyn_exec = {a for a in bits(A.exec, RB) if BASE <= a < END}
dyn_calls = {a for a in bits(A.calls, RB) if BASE <= a < END}

# ---- recursive descent
seeds = set(int(s, 16) & 0x1fffffff for s in A.seed) | dyn_calls
lo, hi = (int(A.range[0], 16), int(A.range[1], 16)) if A.range else (BASE, END)

# prologue scan: "sts.l pr,@-r15" (4f22) or "mov.l r14,@-r15" (2fe6) at a 2-aligned address preceded by rts;nop / data
def is_prologue(a):
    op = r16(a)
    return op in (0x4f22, 0x2fe6, 0x2fd6, 0x2fc6, 0x2fb6, 0x2fa6, 0x2f96, 0x2f86)
for a in range(lo, hi, 2):
    if is_prologue(a):
        p1, p2 = r16(a - 2), r16(a - 4)
        if p2 == 0x000b or p1 == 0x000b or p1 == 0x0009 and r16(a - 4) in (0x0009, 0x000b) or p1 == 0:
            seeds.add(a)

# pointer seeds: any aligned 32-bit word in the image whose value points at a prologue in the code range
ptr_seeds = set()
for off in range(0, len(img) - 3, 4):
    v = struct.unpack_from("<I", img, off)[0]
    p = v & 0x1fffffff
    if (v >> 29) in (0, 4, 5) and lo <= p < hi and not (p & 1) and is_prologue(p):
        ptr_seeds.add(p)
seeds |= ptr_seeds

funcs = {}            # entry -> dict
insn_owner = {}       # addr -> entry
calls = collections.defaultdict(set)   # caller -> callees
callers = collections.defaultdict(set)
lit_refs = collections.defaultdict(set)  # target -> referencing insn addrs
work = list(sorted(seeds))
known_entries = set(work)

def explore(entry):
    blocks = set(); insns = []
    q = [entry]; seen = set()
    regconst = {}
    bad = False
    while q:
        a = q.pop()
        if a in seen or not inimg(a): continue
        seen.add(a); blocks.add(a)
        regconst = {}
        while True:
            op = r16(a)
            if op is None: break
            ins = decode(op)
            if ins.illegal: bad = True; break
            insns.append(a)
            t = pcrel_target(ins, a)
            if ins.mn == "mov.l @(disp,pc),rn":
                v = r32(t); regconst[ins.n] = v; lit_refs[t].add(a)
                if v is not None: lit_refs[("val", v)].add(a)
            elif ins.mn == "mova @(disp,pc),r0":
                regconst[0] = t
            elif ins.n in regconst and not ins.mn.startswith(("mov.l rm,", "mov.b rm,", "mov.w rm,", "cmp", "tst", "jsr", "jmp", "fmov", "lds", "ldc")):
                regconst.pop(ins.n, None)
            f = ins.flow
            if f in ("bt", "bf", "bts", "bfs"):
                q.append(t)
                if f in ("bts", "bfs"):
                    insns.append(a + 2); a += 4; continue
                a += 2; continue
            if f == "bra":
                insns.append(a + 2)
                if t in known_entries and t != entry: calls[entry].add(t)   # tail call
                else: q.append(t)
                break
            if f == "bsr":
                calls[entry].add(t); insns.append(a + 2); a += 4; continue
            if f == "jsr":
                v = regconst.get(ins.n)
                if v is not None and inimg(v): calls[entry].add(phys(v))
                insns.append(a + 2); a += 4; continue
            if f in ("bsrf",):
                insns.append(a + 2); a += 4; continue
            if f == "jmp":
                v = regconst.get(ins.n)
                if v is not None and inimg(v) and phys(v) != entry: calls[entry].add(phys(v))
                insns.append(a + 2); break
            if f in ("rts", "rte", "braf"):
                insns.append(a + 2); break
            if f == "sleep" or f == "trap":
                a += 2; continue
            a += 2
    return blocks, insns, bad

passno = 0
while work:
    e = work.pop()
    if e in funcs or not inimg(e): continue
    blocks, insns, bad = explore(e)
    funcs[e] = {"entry": e, "blocks": sorted(blocks), "ninsn": len(set(insns)), "bad": bad,
                "end": max(insns) + 2 if insns else e}
    for a in insns: insn_owner.setdefault(a, e)
    for c in calls[e]:
        callers[c].add(e)
        if c not in funcs and c not in known_entries:
            known_entries.add(c); work.append(c)

# reject prologue-only seeds that decode to garbage and have no callers / dynamic evidence
def evidence(e):
    return (e in dyn_calls) or bool(callers.get(e)) or (e in [int(s, 16) & 0x1fffffff for s in A.seed])
for e in list(funcs):
    if funcs[e]["bad"] and not evidence(e):
        del funcs[e]

# ---- naming hints from referenced strings
def str_hints(e):
    hs = []
    f = funcs[e]
    for a in range(e, f["end"], 2):
        if insn_owner.get(a) != e: continue
        op = r16(a); ins = decode(op)
        if ins.mn == "mov.l @(disp,pc),rn":
            v = r32(pcrel_target(ins, a))
            if v is not None and phys(v) in strings: hs.append(strings[phys(v)])
    return hs[:6]

def sanitize(s):
    s = re.sub(r"[^A-Za-z0-9_]+", "_", s).strip("_")
    return s[:40]

out = []
for e in sorted(funcs):
    f = funcs[e]
    hints = str_hints(e)
    name = f"sub_{e:08x}"
    for h in hints:
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]{3,}$", h.strip("<> ")):
            name = f"{sanitize(h.strip('<> '))}_{e & 0xfffff:05x}"; break
    out.append({
        "entry": f"{e:08x}", "name": name, "size_insn": f["ninsn"], "end": f"{f['end']:08x}",
        "callers": len(callers.get(e, ())), "callees": sorted(f"{c:08x}" for c in calls.get(e, ())),
        "executed": e in dyn_exec, "dyn_called": e in dyn_calls, "strings": hints, "suspect": f["bad"],
    })

json.dump({"image": A.image, "base": f"{BASE:08x}", "functions": out,
           "strings": {f"{k:08x}": v for k, v in strings.items()}}, open(A.out + ".json", "w"), indent=1)
with open(A.out + ".csv", "w") as fh:
    fh.write("entry,name,insns,callers,callees,executed,strings\n")
    for f in out:
        fh.write(f'{f["entry"]},{f["name"]},{f["size_insn"]},{f["callers"]},{len(f["callees"])},{int(f["executed"])},"{" | ".join(f["strings"]).replace(chr(34), "")}"\n')

# Ghidra script (Jython / PyGhidra compatible)
with open(A.out + "_ghidra.py", "w") as g:
    g.write("# Import Star Wars Racer Arcade function catalogue into Ghidra.\n")
    g.write("# Load the image as raw SH-4 (SuperH4:LE:32:default) at base 0x%08x, then run this script.\n" % BASE)
    g.write("from ghidra.program.model.symbol import SourceType\n")
    g.write("af = currentProgram.getAddressFactory()\nst = currentProgram.getSymbolTable()\nfm = currentProgram.getFunctionManager()\nlst = currentProgram.getListing()\n")
    g.write("def A(x): return af.getDefaultAddressSpace().getAddress(x)\n")
    g.write("F = [\n")
    for f in out:
        cm = ("strings: " + " | ".join(f["strings"])) if f["strings"] else ""
        g.write("  (0x%s, %r, %r),\n" % (f["entry"], f["name"], cm))
    g.write("]\n")
    g.write("for a, n, c in F:\n    ad = A(a)\n    disassemble(ad)\n    fn = fm.getFunctionAt(ad) or createFunction(ad, n)\n")
    g.write("    if fn: fn.setName(n, SourceType.USER_DEFINED)\n    if c: setPlateComment(ad, c)\n")
    g.write("print('imported %d functions' % len(F))\n")

ex = sum(1 for f in out if f["executed"])
print(f"functions: {len(out)}  executed: {ex}  dyn-called: {len(dyn_calls)}  strings: {len(strings)}  suspect: {sum(f['suspect'] for f in out)}")
