"""Decode Address Library for SKSE (format 5 header, v2 delta encoding) and map engine offsets to function IDs.
usage: python addrlib.py <versionlib.bin> 0x6711D4 0x6BC59A ...   -> nearest function start <= offset, its Address Library ID"""
import struct, sys, bisect

d = open(sys.argv[1], "rb").read()
fmt = struct.unpack_from("<I", d, 0)[0]
ver = struct.unpack_from("<4I", d, 4)
name_end = 20 + 64
ptr = struct.unpack_from("<I", d, name_end)[0]
# find the count: try the next u32 / u64 candidates and validate by decoding
def decode(start, count):
    o = start
    ids, offs = [], []
    pid, poff = 0, 0
    for _ in range(count):
        t = d[o]; o += 1
        lo, hi = t & 0xF, t >> 4
        def rd(n):
            nonlocal o
            v = int.from_bytes(d[o:o + n], "little"); o += n; return v
        if lo == 0: i = rd(8)
        elif lo == 1: i = pid + 1
        elif lo == 2: i = pid + rd(1)
        elif lo == 3: i = pid - rd(1)
        elif lo == 4: i = pid + rd(2)
        elif lo == 5: i = pid - rd(2)
        elif lo == 6: i = rd(2)
        elif lo == 7: i = rd(4)
        else: raise ValueError("bad id type")
        tmp = (poff // ptr) if (hi & 8) else poff
        k = hi & 7
        if k == 0: off = rd(8)
        elif k == 1: off = tmp + 1
        elif k == 2: off = tmp + rd(1)
        elif k == 3: off = tmp - rd(1)
        elif k == 4: off = tmp + rd(2)
        elif k == 5: off = tmp - rd(2)
        elif k == 6: off = rd(2)
        elif k == 7: off = rd(4)
        if hi & 8: off *= ptr
        ids.append(i); offs.append(off)
        pid, poff = i, off
    return ids, offs, o

result = None
for cpos in (name_end + 4, name_end + 8, name_end + 12):
    for width in (4, 8):
        count = int.from_bytes(d[cpos:cpos + width], "little")
        if not (100000 < count < 2000000): continue
        try:
            ids, offs, end = decode(cpos + width, count)
        except Exception:
            continue
        if end <= len(d) and end > len(d) - 16:
            result = (ids, offs)
            print(f"format {fmt} version {ver} ptr {ptr}: {count} entries (count at {cpos}, width {width})")
            break
    if result: break
if not result:
    raise SystemExit("could not decode the address library")
ids, offs = result
pairs = sorted(zip(offs, ids))
keys = [p[0] for p in pairs]
byid = dict(zip(ids, offs))
for a in sys.argv[2:]:
    off = int(a, 16)
    i = bisect.bisect_right(keys, off) - 1
    if i >= 0: print(f"{a}: nearest function start 0x{pairs[i][0]:X} (id {pairs[i][1]}), +0x{off - pairs[i][0]:X} into it")
for chk in (52269, 51420):
    print(f"sanity: id {chk} -> 0x{byid.get(chk, 0):X}")
