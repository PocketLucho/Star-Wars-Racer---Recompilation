"""Static memory views of the Hikaru ROMs for analysis (SH-4 address space)."""
import struct, os

ROM = os.path.join(os.path.dirname(__file__), "..", "rom")

class Region:
    def __init__(self, base, data, name):
        self.base, self.data, self.name = base, data, name
        self.end = base + len(data)

class Mem:
    def __init__(self):
        self.regions = []
    def add(self, base, data, name):
        self.regions.append(Region(base, data, name))
    def find(self, a):
        a &= 0x1fffffff                 # strip P1/P2 segment bits
        for r in self.regions:
            if r.base <= a < r.end:
                return r, a - r.base
        return None, None
    def r8(self, a):
        r, o = self.find(a)
        return None if r is None else r.data[o]
    def r16(self, a):
        r, o = self.find(a)
        if r is None or o + 2 > len(r.data): return None
        return struct.unpack_from("<H", r.data, o)[0]
    def r32(self, a):
        r, o = self.find(a)
        if r is None or o + 4 > len(r.data): return None
        return struct.unpack_from("<I", r.data, o)[0]
    def bytes(self, a, n):
        r, o = self.find(a)
        return None if r is None else r.data[o:o + n]

def load_bios(name="epr-23400a.ic94"):
    return open(os.path.join(ROM, name), "rb").read()

def load_user1():
    return open(os.path.join(ROM, "user1.bin"), "rb").read()

def bios_mem(name="epr-23400a.ic94"):
    m = Mem(); m.add(0, load_bios(name), "bios"); return m
