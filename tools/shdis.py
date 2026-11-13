"""Linear disassembly helper: dis.py <image> <load_base_hex> <start_hex> <count>"""
import sys, struct
sys.path.insert(0, __import__("os").path.dirname(__file__))
from sh4dec import decode, fmt
from mem import Mem

def main():
    img, base, start, cnt = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4])
    m = Mem(); m.add(base, open(img, "rb").read(), "img")
    pc = start
    for _ in range(cnt):
        op = m.r16(pc)
        if op is None: break
        ins = decode(op)
        print(f"{pc:08x}: {op:04x}  {fmt(ins, pc, m.r32, m.r16)}")
        pc += 2

if __name__ == "__main__":
    main()
