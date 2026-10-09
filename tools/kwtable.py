import os
import sys, struct; sys.path.insert(0, "."); import esp
D = os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Data/"
buf = esp.load(D + "Skyrim.esm")
rows = []
for r in esp.walk(buf):
    if r.sig == "KYWD":
        for s, d in r.subs():
            if s == "EDID":
                e = esp.zstr(d)
                if e.startswith("LocType") or e.startswith("LocSet") or e.startswith("LocTypeHold"): rows.append((e, r.fid))
for e, f in sorted(rows): print(f"{e} = 0x{f:X}")
