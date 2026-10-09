"""Minimal minidump reader: exception record, faulting thread registers, modules, and the module+offset of every return-address-like
value on the faulting thread's stack. No dependencies. usage: python mdump.py <file.dmp> [depth]"""
import struct, sys

d = open(sys.argv[1], "rb").read()
depth = int(sys.argv[2]) if len(sys.argv) > 2 else 120
assert d[:4] == b"MDMP", "not a minidump"
nstreams, rva = struct.unpack_from("<II", d, 8)[0:2]
streams = {}
for i in range(nstreams):
    t, size, off = struct.unpack_from("<III", d, rva + i * 12)
    streams[t] = (size, off)

def ustr(off):
    n = struct.unpack_from("<I", d, off)[0]
    return d[off + 4: off + 4 + n].decode("utf-16le")

# modules (stream 4)
mods = []
size, off = streams[4]
cnt = struct.unpack_from("<I", d, off)[0]
for i in range(cnt):
    base, msize, _, _, name_rva = struct.unpack_from("<QIIII", d, off + 4 + i * 108)
    mods.append((base, msize, ustr(name_rva).split("\\")[-1]))

def where(addr):
    for b, s, n in mods:
        if b <= addr < b + s: return f"{n}+0x{addr - b:X}"
    return None

# exception (stream 6)
size, off = streams[6]
tid, _ = struct.unpack_from("<II", d, off)
code, flags, rec, eaddr, nparams = struct.unpack_from("<IIQQI", d, off + 8)
params = struct.unpack_from("<15Q", d, off + 8 + 4 + 4 + 8 + 8 + 4 + 4)
ctx_size, ctx_rva = struct.unpack_from("<II", d, off + 8 + 152)
print(f"exception thread {tid}: code 0x{code:08X} address 0x{eaddr:X} -> {where(eaddr)}  params {[hex(p) for p in params[:nparams]]}")
# CONTEXT x64: RSP at 0x98, RIP at 0xF8
rsp = struct.unpack_from("<Q", d, ctx_rva + 0x98)[0]
rip = struct.unpack_from("<Q", d, ctx_rva + 0xF8)[0]
regs = {n: struct.unpack_from("<Q", d, ctx_rva + o)[0] for n, o in (("rax", 0x78), ("rcx", 0x80), ("rdx", 0x88), ("rbx", 0x90), ("rbp", 0xA0), ("rsi", 0xA8), ("rdi", 0xB0), ("r8", 0xB8), ("r9", 0xC0))}
print(f"RIP {rip:X} ({where(rip)})  RSP {rsp:X}  " + "  ".join(f"{k}={v:X}" for k, v in regs.items()))

# memory ranges: Memory64List (stream 9) or MemoryList (stream 5)
ranges = []
if 9 in streams:
    size, off = streams[9]
    n, base_rva = struct.unpack_from("<QQ", d, off)
    cur = base_rva
    for i in range(n):
        a, l = struct.unpack_from("<QQ", d, off + 16 + i * 16)
        ranges.append((a, l, cur)); cur += l
elif 5 in streams:
    size, off = streams[5]
    n = struct.unpack_from("<I", d, off)[0]
    for i in range(n):
        a, l, r = struct.unpack_from("<QII", d, off + 4 + i * 16)
        ranges.append((a, l, r))

# thread stacks live in the thread list (stream 3): MINIDUMP_THREAD = id,susp,prio,prioclass,teb(8),stack(start8,size4,rva4),context(size4,rva4)
if 3 in streams:
    size, off = streams[3]
    n = struct.unpack_from("<I", d, off)[0]
    for i in range(n):
        t_id, _, _, _, teb, st_start, st_size, st_rva = struct.unpack_from("<IIIIQQII", d, off + 4 + i * 48)
        ranges.append((st_start, st_size, st_rva))

def read(addr, n):
    for a, l, r in ranges:
        if a <= addr < a + l:
            m = min(n, a + l - addr)
            return d[r + (addr - a): r + (addr - a) + m]
    return b""

stack = read(rsp, 8 * 2048)
print(f"stack bytes available: {len(stack)}")
shown = 0
for i in range(0, len(stack) - 7, 8):
    v = struct.unpack_from("<Q", stack, i)[0]
    w = where(v)
    if w and not w.startswith("ntdll") and shown < depth:
        print(f"  [rsp+{i:#05x}] {v:X} {w}")
        shown += 1
print("modules of interest:", [(n, hex(b)) for b, s, n in mods if n.lower() in ("alife.dll", "skyrimse.exe", "skyui_se.dll")] )
