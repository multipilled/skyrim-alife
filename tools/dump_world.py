"""Extract the Skyrim location graph (every LCTN with a world marker) from the user's own Skyrim.esm for the headless sim harness.
Output: data/world_skyrim.json  (derived data from the user's install - not for redistribution)."""
import os
import sys, struct, json, time
sys.path.insert(0, "."); import esp
D = os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Data/"
buf = esp.load(D + "Skyrim.esm")
kyw = {}; lctn = {}; wrld = {}
for r in esp.walk(buf):
    if r.sig == "KYWD":
        for s, d in r.subs():
            if s == "EDID": kyw[r.fid] = esp.zstr(d)
    elif r.sig == "LCTN":
        L = dict(fid=r.fid, edid="", kw=[], parent=0, marker=0, radius=0.0)
        for s, d in r.subs():
            if s == "EDID": L["edid"] = esp.zstr(d)
            elif s == "KWDA": L["kw"] = list(struct.unpack("<%dI" % (len(d)//4), d))
            elif s == "PNAM": L["parent"] = struct.unpack("<I", d)[0]
            elif s == "MNAM": L["marker"] = struct.unpack("<I", d)[0]
            elif s == "RNAM": L["radius"] = struct.unpack("<f", d)[0]
        lctn[r.fid] = L
    elif r.sig == "WRLD":
        W = dict(fid=r.fid, edid="", parent=0)
        for s, d in r.subs():
            if s == "EDID": W["edid"] = esp.zstr(d)
            elif s == "WNAM": W["parent"] = struct.unpack("<I", d)[0]
        wrld[r.fid] = W
cellpos = {}   # location fid -> list of (x, y) exterior cell centres in Tamriel-rooted worlds
for r in esp.walk(buf):
    if r.sig != "CELL": continue
    world = 0
    for gt, lbl in r.group_path:
        if gt == 1: world = lbl
    if not world: continue
    xclc = None; xlcn = 0
    for sg, d in r.subs():
        if sg == "XCLC" and len(d) >= 8: xclc = struct.unpack_from("<ii", d, 0)
        elif sg == "XLCN" and len(d) >= 4: xlcn = struct.unpack_from("<I", d, 0)[0]
    if xclc and xlcn: cellpos.setdefault(xlcn, []).append((xclc[0] * 4096 + 2048, xclc[1] * 4096 + 2048, world))
want = {L["marker"]: f for f, L in lctn.items() if L["marker"]}
pos = {}
for r in esp.walk(buf):
    if r.sig == "REFR" and r.fid in want:
        world = 0
        for gt, lbl in r.group_path:
            if gt == 1: world = lbl
        p = None
        for s, d in r.subs():
            if s == "DATA" and len(d) >= 12: p = struct.unpack_from("<3f", d, 0)
        if p: pos[r.fid] = (p, world)
def kwnames(L): return [kyw.get(k, hex(k)) for k in L["kw"]]
def hold_of(fid):
    seen = 0
    while fid in lctn and seen < 20:
        if 0x16771 in lctn[fid]["kw"]: return fid       # LocTypeHold
        fid = lctn[fid]["parent"]; seen += 1
    return 0
TAMRIEL = 0x3C
def root_world(w):
    seen = 0
    while w in wrld and wrld[w]["parent"] and seen < 8: w = wrld[w]["parent"]; seen += 1
    return w
out = []
def descendants(fid):
    kids = [f for f, L in lctn.items() if L["parent"] == fid]
    res = [fid]
    for k in kids: res += descendants(k)
    return res
for f, L in lctn.items():
    m = L["marker"]
    src = "marker"
    if m in pos:
        (x, y, z), w = pos[m]
    else:
        pts = []
        for d in descendants(f): pts += cellpos.get(d, [])
        if not pts: continue
        x = sum(p[0] for p in pts) / len(pts); y = sum(p[1] for p in pts) / len(pts); z = 0.0
        w = pts[0][2]; src = "cells"
    out.append(dict(source_pos=src, formid=f, edid=L["edid"], keywords=L["kw"], kwnames=[k for k in kwnames(L) if k.startswith("Loc")],
                    parent=L["parent"], hold=hold_of(f), hold_edid=lctn.get(hold_of(f), {}).get("edid", ""), x=x, y=y, z=z, world=w, root_world=root_world(w), radius=L["radius"]))
json.dump(dict(source="Skyrim.esm", locations=out), open("../data/world_skyrim.json", "w"), indent=0)
from collections import Counter
print("locations with marker:", len(out), "of", len(lctn))
print("root worlds:", Counter(wrld.get(o["root_world"], {}).get("edid", hex(o["root_world"])) for o in out))
tam = [o for o in out if o["root_world"] == TAMRIEL]
print("Tamriel-rooted:", len(tam))
print("holds:", Counter(o["hold_edid"] for o in tam).most_common(15))
kc = Counter(k for o in tam for k in o["kwnames"])
for k, v in kc.most_common(40): print(f"  {k}: {v}")
xs = [o["x"] for o in tam]; ys = [o["y"] for o in tam]
print("bounds x", min(xs), max(xs), "y", min(ys), max(ys))
