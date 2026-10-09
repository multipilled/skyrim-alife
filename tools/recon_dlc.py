"""Recon: what do the DLCs and Creation Club plugins add that A-Life cares about (worldspaces, locations, factions)?
Read-only walk over the installed plugins; writes a report to recon/dlc_recon.txt (not shipped)."""
import sys, struct, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import esp
D = os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Data/"
ORDER = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]
cc = [l.strip() for l in open(os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Skyrim.ccc") if l.strip()]
ORDER += [c for c in cc if os.path.exists(D + c)]

def masters(buf):
    out = []
    o = 24
    # TES4 record: header then subrecords
    size = struct.unpack_from("<I", buf, 4)[0]
    end = 24 + size
    while o + 6 <= end:
        sig = buf[o:o+4].decode("latin1"); sz = struct.unpack_from("<H", buf, o+4)[0]; o += 6
        if sig == "MAST": out.append(esp.zstr(buf[o:o+sz]))
        o += sz
    return out

class Plugin:
    def __init__(self, name):
        self.name = name
        self.buf = esp.load(D + name)
        self.masters = masters(self.buf)
        self.light = bool(struct.unpack_from("<I", self.buf, 8)[0] & 0x200)
    def resolve(self, fid):
        idx = fid >> 24
        local = fid & 0xFFFFFF
        if idx < len(self.masters): return (self.masters[idx].lower(), local)
        return (self.name.lower(), local & (0xFFF if self.light else 0xFFFFFF))

plugins = {}
for n in ORDER:
    try: plugins[n] = Plugin(n)
    except Exception as e: print("skip", n, e)

kyw = {}      # (file, local) -> edid
for p in plugins.values():
    for r in esp.walk(p.buf):
        if r.sig == "KYWD":
            for s, d in r.subs():
                if s == "EDID": kyw[p.resolve(r.fid)] = esp.zstr(d)

out = open("recon/dlc_recon.txt", "w", encoding="utf-8")
def P(*a): print(*a, file=out)

for n, p in plugins.items():
    wrlds, lctn, fact, npcs = [], [], [], 0
    for r in esp.walk(p.buf):
        if r.sig == "WRLD":
            e = [esp.zstr(d) for s, d in r.subs() if s == "EDID"]
            wrlds.append((f"{r.fid:08X}", e[0] if e else "?"))
        elif r.sig == "LCTN":
            subs = r.subs()
            e = [esp.zstr(d) for s, d in subs if s == "EDID"]
            kw = []
            for s, d in subs:
                if s == "KWDA":
                    for i in range(0, len(d), 4):
                        kw.append(kyw.get(p.resolve(struct.unpack_from("<I", d, i)[0]), "?"))
            lctn.append((f"{r.fid:08X}", e[0] if e else "?", kw))
        elif r.sig == "FACT":
            e = [esp.zstr(d) for s, d in r.subs() if s == "EDID"]
            fact.append((f"{r.fid:08X}", e[0] if e else "?"))
        elif r.sig == "NPC_": npcs += 1
    if not (wrlds or lctn or fact) and n not in ("Skyrim.esm",): 
        continue
    P(f"=== {n}  masters={len(p.masters)} light={p.light}  WRLD={len(wrlds)} LCTN={len(lctn)} FACT={len(fact)} NPC_={npcs}")
    for w in wrlds: P("  WRLD", *w)
    kinds = collections.Counter()
    for fid, e, kw in lctn:
        for k in kw:
            if k.startswith("LocType") or k.startswith("DLC") : kinds[k] += 1
    if lctn: P("  LCTN keyword counts:", dict(kinds.most_common(30)))
    if n != "Skyrim.esm":
        for fid, e, kw in lctn[:200]: P("  LCTN", fid, e, [k for k in kw if k.startswith("LocType") or k.startswith("DLC")][:4])
    for f in fact[:60]: P("  FACT", *f)
out.close()
print("wrote recon/dlc_recon.txt")
