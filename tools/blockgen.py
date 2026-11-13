"""SH-4 block generator (AOT), same scheme as the NAOMI recomps: one C function per basic block,
each tagged with the FNV-1a hash of its guest bytes.

usage: blockgen.py -o gen -n 16
           --image 0:../rom/game_ram.bin:0c010000 --image 0:ram_master.bin:0c000000 --image 1:ram_slave.bin:0c000000
           [--catalog ../out/master.json] [--seen seen.txt ...] [--misses misses.txt ...]

Block boundaries must match emu/blockrt.c (interp_block):
  - a block ends after a branch (+ delay slot) or bt/bf, or after 256 instructions
  - rte / trapa / sleep / illegal never go in a block: the block stops before them
Images are (cpu, file, base) RAM snapshots; seeds are explored in every image of their CPU and
each (pc, hash) is emitted once, so code that is copied or rewritten at run time gets one
version per distinct content.
"""
import sys, os, re, struct, json, argparse, collections

HERE = os.path.dirname(os.path.abspath(__file__))
EMU = os.path.join(HERE, "..", "emu")
ap = argparse.ArgumentParser()
ap.add_argument("-o", "--out", default="gen")
ap.add_argument("-n", "--units", type=int, default=16)
ap.add_argument("--image", action="append", default=[])
ap.add_argument("--catalog")
ap.add_argument("--seen", action="append", default=[])
ap.add_argument("--misses", action="append", default=[])
ap.add_argument("--no-explore", action="store_true", help="only the seeds themselves, no successor walk")
A = ap.parse_args()

# ---- opcode table (same patterns as the interpreter)
opt = open(os.path.join(EMU, "optable.h")).read()
pats = [(int(m, 16), int(v, 16), i) for m, v, i in re.findall(r"\{0x([0-9a-f]+), 0x([0-9a-f]+), (OP_[A-Z0-9_]+)\}", opt)]
OPID = [None] * 65536
for op in range(65536):
    for m, v, i in pats:
        if op & m == v: OPID[op] = i; break
    else: OPID[op] = "OP_ILLEGAL"
SEM = set(re.findall(r"static inline void SEM_(OP_[A-Z0-9_]+)\(", open(os.path.join(EMU, "sh4sem.h")).read()))
BLOCK_END = {"OP_BT_LABEL", "OP_BF_LABEL", "OP_BT_S_LABEL", "OP_BF_S_LABEL", "OP_BRA_LABEL", "OP_BSR_LABEL",
             "OP_BRAF_RN", "OP_BSRF_RN", "OP_JMP_AT_RN", "OP_JSR_AT_RN", "OP_RTS"}
INTERP_ONLY = {"OP_RTE", "OP_TRAPA_IMM_IMM", "OP_SLEEP", "OP_ILLEGAL"}
MAXI = 256

def sem(op):
    i = OPID[op]
    return f"SEM_{i}" if i in SEM else "SEM_OP_ILLEGAL"
def s8(v): return v - 256 if v & 0x80 else v
def s12(v): return v - 0x1000 if v & 0x800 else v

# ---- images
images = collections.defaultdict(list)     # cpu -> [(base, bytes)]
for spec in A.image:
    cpu, path, base = spec.split(":")
    images[int(cpu)].append((int(base, 16), open(path, "rb").read()))

# ---- seeds
seeds = collections.defaultdict(set)
if A.catalog:
    for f in json.load(open(A.catalog))["functions"]:
        if not f["suspect"]: seeds[0].add(int(f["entry"], 16))
for path in A.seen + A.misses:
    for line in open(path):
        f = line.split()
        c, p = int(f[0]), int(f[1], 16)
        seeds[c].add(p)
        if len(f) > 2:      # miss with the guest bytes captured at run time: its own mini image
            images[c].append((p, bytes.fromhex(f[2])))

def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xffffffff
    return h

def extract(img, base, pc):
    """returns (insn list [(addr, op)], kind, successors) or None"""
    end = base + len(img)
    def r16(a): return struct.unpack_from("<H", img, a - base)[0] if base <= a < end - 1 else None
    out = []; succ = []
    a = pc
    for k in range(MAXI):
        op = r16(a)
        if op is None: return None
        i = OPID[op]
        if i in INTERP_ONLY:
            if k == 0: return None
            succ.append(a)
            return out, ("stop", a), succ
        out.append((a, op))
        if i in BLOCK_END:
            if i in ("OP_BT_LABEL", "OP_BF_LABEL"):
                succ += [a + 4 + s8(op & 0xff) * 2, a + 2]
            elif i in ("OP_BT_S_LABEL", "OP_BF_S_LABEL"):
                succ += [a + 4 + s8(op & 0xff) * 2, a + 4]
            elif i == "OP_BRA_LABEL":
                succ += [a + 4 + s12(op & 0xfff) * 2]
            elif i == "OP_BSR_LABEL":
                succ += [a + 4 + s12(op & 0xfff) * 2, a + 4]
            elif i in ("OP_JSR_AT_RN", "OP_BSRF_RN"):
                succ += [a + 4]
            if i not in ("OP_BT_LABEL", "OP_BF_LABEL"):
                sop = r16(a + 2)
                if sop is None: return None
                out.append((a + 2, sop))
            return out, ("br", a, op), succ
        a += 2
    succ.append(a)
    return out, ("stop", a), succ

blocks = {}   # (pc, hash) -> C body
counts = collections.Counter()
for cpu, seedset in seeds.items():
    for base, img in images.get(cpu, []):
        end = base + len(img)
        work = sorted(p for p in seedset if base <= p < end and p >= 0x0c000000 and p < 0x0e000000 and not p & 1)
        done = set()
        while work:
            pc = work.pop()
            if pc in done: continue
            done.add(pc)
            r = extract(img, base, pc)
            if r is None: continue
            insns, term, succ = r
            size = insns[-1][0] + 2 - pc
            h = fnv(img[pc - base:pc - base + size])
            key = (pc, h)
            if not A.no_explore:
                for s in succ:
                    if base <= s < end and s not in done and 0x0c000000 <= s < 0x0e000000: work.append(s)
            if key in blocks: continue
            # ---- emit C
            L = ["  const u32 seg = c->pc & 0xe0000000u;"]; cyc = 0
            body = insns if term[0] == "stop" else insns[:-2] if OPID[term[2]] not in ("OP_BT_LABEL", "OP_BF_LABEL") else insns[:-1]
            for (a, op) in body:
                L.append(f"  {sem(op)}(c, 0x{op:04x}, seg | 0x{a:08x}u);"); cyc += 1
            if term[0] == "stop":
                L.append(f"  c->pc = seg | 0x{term[1]:08x}u;")
            else:
                a, op = term[1], term[2]; i = OPID[op]; n = (op >> 8) & 15
                slot = (a + 2, img[a + 2 - base] | (img[a + 3 - base] << 8)) if i not in ("OP_BT_LABEL", "OP_BF_LABEL") else None
                tg8 = (a + 4 + s8(op & 0xff) * 2) & 0xffffffff
                tg12 = (a + 4 + s12(op & 0xfff) * 2) & 0xffffffff
                SL = f"c->pc = seg | 0x{a + 2:08x}u; {sem(slot[1])}(c, 0x{slot[1]:04x}, seg | 0x{slot[0]:08x}u);" if slot else ""
                if i == "OP_BT_LABEL": L.append(f"  c->pc = (c->sr & SR_T) ? (seg | 0x{tg8:08x}u) : (seg | 0x{a + 2:08x}u);"); cyc += 1
                elif i == "OP_BF_LABEL": L.append(f"  c->pc = (c->sr & SR_T) ? (seg | 0x{a + 2:08x}u) : (seg | 0x{tg8:08x}u);"); cyc += 1
                elif i == "OP_BT_S_LABEL": L.append(f"  {{ u32 tg = (c->sr & SR_T) ? (seg | 0x{tg8:08x}u) : (seg | 0x{a + 4:08x}u); {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_BF_S_LABEL": L.append(f"  {{ u32 tg = (c->sr & SR_T) ? (seg | 0x{a + 4:08x}u) : (seg | 0x{tg8:08x}u); {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_BRA_LABEL": L.append(f"  {{ {SL} c->pc = (seg | 0x{tg12:08x}u); }}"); cyc += 2
                elif i == "OP_BSR_LABEL": L.append(f"  {{ c->pr = (seg | 0x{a + 4:08x}u); {SL} c->pc = (seg | 0x{tg12:08x}u); }}"); cyc += 2
                elif i == "OP_BRAF_RN": L.append(f"  {{ u32 tg = (seg | 0x{a + 4:08x}u) + c->r[{n}]; {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_BSRF_RN": L.append(f"  {{ u32 tg = (seg | 0x{a + 4:08x}u) + c->r[{n}]; c->pr = (seg | 0x{a + 4:08x}u); {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_JMP_AT_RN": L.append(f"  {{ u32 tg = c->r[{n}]; {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_JSR_AT_RN": L.append(f"  {{ u32 tg = c->r[{n}]; c->pr = (seg | 0x{a + 4:08x}u); {SL} c->pc = tg; }}"); cyc += 2
                elif i == "OP_RTS": L.append(f"  {{ u32 tg = c->pr; {SL} c->pc = tg; }}"); cyc += 2
            L.append(f"  c->cycles += {cyc};")
            blocks[key] = (size, "\n".join(L))
            counts[cpu] += 1

os.makedirs(A.out, exist_ok=True)
for f in os.listdir(A.out):
    if f.startswith("blk_") and f.endswith((".c", ".o")): os.remove(os.path.join(A.out, f))
keys = sorted(blocks)
per = (len(keys) + A.units - 1) // max(A.units, 1)
hdr = '/* generated by tools/blockgen.py - do not edit */\n#include "sh4sem.h"\n#include "blockrt.h"\n'
for u in range(A.units):
    ks = keys[u * per:(u + 1) * per]
    if not ks: continue
    with open(os.path.join(A.out, f"blk_{u:02d}.c"), "w") as fh:
        fh.write(hdr)
        for pc, h in ks:
            fh.write(f"void B_{pc:08x}_{h:08x}(Cpu *c) {{\n{blocks[(pc, h)][1]}\n}}\n")
with open(os.path.join(A.out, "blk_table.c"), "w") as fh:
    fh.write(hdr)
    for pc, h in keys: fh.write(f"void B_{pc:08x}_{h:08x}(Cpu *c);\n")
    fh.write("const BlkEntry blk_table[] = {\n")
    for pc, h in keys: fh.write(f"  {{0x{pc:08x}u, 0x{h:08x}u, {blocks[(pc, h)][0]}u, B_{pc:08x}_{h:08x}}},\n")
    fh.write("};\n")
    fh.write(f"const int blk_count = {len(keys)};\n")
print(f"blocks: {len(keys)} ({', '.join(f'cpu{c}: {n}' for c, n in sorted(counts.items()))}) -> {A.out}/")
