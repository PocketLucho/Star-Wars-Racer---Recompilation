"""Render the Eurasia layers from a VRAMDUMP file: layers.py vram_NNNNN.bin out.png
Layout (unit 0 regs 0x180..0x1bc): coords lo/hi = x(9 bits, 8-byte units) | y<<9. Format bit (0x1b0): 0 = ABGR1555, 1 = 32bpp."""
import sys, numpy as np
from PIL import Image
d = open(sys.argv[1], 'rb').read()
v16 = np.frombuffer(d[:0x800000], dtype='<u2').reshape(2048, 2048)
regs = np.frombuffer(d[0x800000:0x801000], dtype='<u4')
R = lambda o: int(regs[o // 4])
def c1555(a):
    return np.stack([(a & 31) << 3, ((a >> 5) & 31) << 3, ((a >> 10) & 31) << 3, np.where(a & 0x8000, 255, 0)], -1).astype(np.uint8)
def c32(a32):
    return np.stack([a32 & 255, (a32 >> 8) & 255, (a32 >> 16) & 255, (a32 >> 24) & 255], -1).astype(np.uint8)
tiles = []
fmt, ena, direct = R(0x1b0), R(0x1b4), R(0x1b8)
for i, o in enumerate((0x180, 0x188, 0x190, 0x198)):
    lo, hi = R(o), R(o + 4)
    x0, y0, x1, y1 = (lo & 0x1ff) * 4, lo >> 9, ((hi & 0x1ff) + 1) * 4, (hi >> 9) + 1   # in 16-bit columns
    bit = (fmt >> (0 if i < 2 else i - 1)) & 1
    reg = v16[y0:y1, x0:x1]
    img = c32(reg.view('<u4').reshape(reg.shape[0], -1).astype(np.uint32)) if bit else c1555(reg)
    print(f"bank{i}: x{x0}-{x1} y{y0}-{y1} fmt{bit} {img.shape[1]}x{img.shape[0]} nonzero={np.count_nonzero(reg)}")
    tiles.append(Image.fromarray(img, 'RGBA').convert('RGB').resize((320, 240)))
print(f"fmt={fmt:x} ena={ena:x} direct={direct:x} 1bc={R(0x1bc):x} 100={R(0x100):x}")
out = Image.new('RGB', (640, 480)); [out.paste(t, ((k % 2) * 320, (k // 2) * 240)) for k, t in enumerate(tiles)]
out.save(sys.argv[2])
