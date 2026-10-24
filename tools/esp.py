"""Minimal Skyrim SE plugin (TES4) reader: walks GRUPs/records/subrecords. Read-only; used for recon of ALife.esp and vanilla data."""
import struct, zlib, sys, io

class Rec:
    __slots__ = ("sig", "flags", "fid", "ver", "data", "off", "group_path")
    def subs(self):
        d = self.data
        if self.flags & 0x40000:  # compressed
            size = struct.unpack_from("<I", d, 0)[0]
            d = zlib.decompress(d[4:])
        o = 0
        xxxx = None
        out = []
        while o + 6 <= len(d):
            sig = d[o:o+4].decode("latin1"); sz = struct.unpack_from("<H", d, o+4)[0]; o += 6
            if sig == "XXXX":
                xxxx = struct.unpack_from("<I", d, o)[0]; o += 4; continue
            if xxxx is not None: sz = xxxx; xxxx = None
            out.append((sig, d[o:o+sz])); o += sz
        return out

def walk(buf, start=0, end=None, path=()):
    o = start; end = end if end is not None else len(buf)
    while o < end:
        sig = buf[o:o+4].decode("latin1")
        size = struct.unpack_from("<I", buf, o+4)[0]
        if sig == "GRUP":
            label = buf[o+8:o+12]
            gtype = struct.unpack_from("<i", buf, o+12)[0]
            lbl = label.decode("latin1") if gtype == 0 else struct.unpack("<I", label)[0]
            yield from walk(buf, o+24, o+size, path + ((gtype, lbl),))
            o += size
        else:
            r = Rec(); r.sig = sig; r.flags = struct.unpack_from("<I", buf, o+8)[0]; r.fid = struct.unpack_from("<I", buf, o+12)[0]
            r.ver = struct.unpack_from("<H", buf, o+20)[0]
            r.data = buf[o+24:o+24+size]; r.off = o; r.group_path = path
            yield r
            o += 24 + size

def hedr_count(buf, start=0, end=None):
    """Records plus GRUPs below the TES4 header: the HEDR numRecords value the Creation Kit and the DLC masters write."""
    o = start; end = len(buf) if end is None else end; n = 0
    while o < end:
        size = struct.unpack_from("<I", buf, o + 4)[0]; n += 1
        if buf[o:o+4] == b"GRUP": n += hedr_count(buf, o + 24, o + size)
        o += size if buf[o:o+4] == b"GRUP" else 24 + size
    return n

def load(path):
    with open(path, "rb") as f: return f.read()

def zstr(b): return b.split(b"\0", 1)[0].decode("latin1")

if __name__ == "__main__":
    buf = load(sys.argv[1])
    from collections import Counter
    c = Counter(); first = {}
    for r in walk(buf):
        c[r.sig] += 1; first.setdefault(r.sig, r)
    for k, v in sorted(c.items()): print(k, v)
