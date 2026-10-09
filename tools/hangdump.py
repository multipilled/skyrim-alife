"""Thread stacks of a full minidump of a hung process (memory-mapped, so multi-GB dumps are fine): for every thread, the module+offset of
return-address-like values near the top of its stack. usage: python hangdump.py <file.dmp> [module-substring ...]"""
import mmap, struct, sys
f = open(sys.argv[1], "rb")
d = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
want = [m.lower() for m in sys.argv[2:]] or ["alife"]
nstreams, rva = struct.unpack_from("<II", d, 8)
streams = {}
for i in range(nstreams):
    t, size, off = struct.unpack_from("<III", d, rva + i * 12)
    streams[t] = (size, off)
def ustr(off):
    n = struct.unpack_from("<I", d, off)[0]
    return d[off + 4: off + 4 + n].decode("utf-16le")
mods = []
size, off = streams[4]
cnt = struct.unpack_from("<I", d, off)[0]
for i in range(cnt):
    base, msize, _, _, name_rva = struct.unpack_from("<QIIII", d, off + 4 + i * 108)
    mods.append((base, msize, ustr(name_rva).split("\\")[-1]))
def where(a):
    for b, s, n in mods:
        if b <= a < b + s: return f"{n}+0x{a - b:X}"
ranges = []
if 9 in streams:
    size, off = streams[9]
    n, base_rva = struct.unpack_from("<QQ", d, off)
    cur = base_rva
    for i in range(n):
        a, l = struct.unpack_from("<QQ", d, off + 16 + i * 16)
        ranges.append((a, l, cur)); cur += l
def read(addr, n):
    for a, l, r in ranges:
        if a <= addr < a + l:
            m = min(n, a + l - addr); return d[r + (addr - a): r + (addr - a) + m]
    return b""
size, off = streams[3]
n = struct.unpack_from("<I", d, off)[0]
for i in range(n):
    tid, _, _, _, teb, st_start, st_size, st_rva, ctx_size, ctx_rva = struct.unpack_from("<IIIIQQIIII", d, off + 4 + i * 48)
    rsp = struct.unpack_from("<Q", d, ctx_rva + 0x98)[0]
    rip = struct.unpack_from("<Q", d, ctx_rva + 0xF8)[0]
    stack = read(rsp, 0x4000)
    frames = []
    for k in range(0, len(stack) - 7, 8):
        v = struct.unpack_from("<Q", stack, k)[0]
        w = where(v)
        if w and not w.lower().startswith(("ntdll", "kernel", "win32u", "user32")): frames.append(w)
    hit = any(any(m in fr.lower() for m in want) for fr in frames)
    if hit or i == 0:
        print(f"thread {tid} rip {where(rip)} frames:")
        for fr in frames[:40]: print("   ", fr)
