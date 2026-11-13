"""Rebuild on-screen text from the 8x8 font blits in hikaru.log (glyph = (src-0xf0000)/8, dst = y<<11 | x)."""
import sys, re, collections
scr = collections.OrderedDict()
frames = []
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"BLIT ([0-9a-f]+) -> ([0-9a-f]+) 8x8", line)
    if not m: continue
    s, d = int(m.group(1), 16), int(m.group(2), 16)
    ch = (s - 0xf0000) // 8
    y, x = d >> 11, d & 0x7ff
    scr[(y, x)] = chr(ch) if 32 <= ch < 127 else "?"
rows = collections.defaultdict(dict)
for (y, x), c in scr.items(): rows[y][x] = c
for y in sorted(rows):
    xs = sorted(rows[y]); x0 = xs[0]
    line = [" "] * ((xs[-1] - x0) // 8 + 1)
    for x in xs: line[(x - x0) // 8] = rows[y][x]
    print(f"y={y:4d} x={x0:4d}: {''.join(line)}")
