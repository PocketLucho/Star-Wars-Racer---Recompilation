"""Render Eurasia VRAM dump as image. usage: vram2png.py <vram.bin> <out.png> <stride_px> <y0> <h> [bpp=32]"""
import sys, zlib, struct
d = open(sys.argv[1], "rb").read()
stride, y0, h = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
bpp = int(sys.argv[6]) if len(sys.argv) > 6 else 32
rows = []
for y in range(y0, y0 + h):
    row = bytearray([0])
    for x in range(stride):
        if bpp == 32:
            o = (y * stride + x) * 4
            v = struct.unpack_from("<I", d, o)[0] if o + 4 <= len(d) else 0
            r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
        else:
            o = (y * stride + x) * 2
            v = struct.unpack_from("<H", d, o)[0] if o + 2 <= len(d) else 0
            r, g, b = ((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3
        row += bytes((r, g, b))
    rows.append(bytes(row))
raw = b"".join(rows)
def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", stride, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
