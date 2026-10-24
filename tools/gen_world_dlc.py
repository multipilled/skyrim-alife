"""data/world_solstheim.tsv: the Dragonborn (Solstheim) locations for the headless harness (tests/harness.cpp --solstheim), in the
format of world_skyrim.tsv plus an 11th column, the worldspace index (1). Positions come from config/positions.ltx [pos_dlc2]
(tools/gen_positions_dlc.py); parents, holds and keywords from the user's own Dragonborn.esm. Ids are written as the game sees them
with Dragonborn.esm at load index 4 (Skyrim, Update, Dawnguard, HearthFires, Dragonborn), so [loc_dlc2_<local id>] sections in
props.ltx apply exactly as in the game. Derived data from the user's install: not for redistribution.

usage: python gen_world_dlc.py"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import esp

D = os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Data/"
HERE = os.path.dirname(os.path.abspath(__file__))
OWN, RUNTIME = 2, 4                       # Dragonborn's own index in its FormIDs / its usual load index
HOLD_KW = 0x16771                          # LocTypeHold (Skyrim.esm)


def rt(fid):   # a Dragonborn.esm FormID as the game sees it
    return (RUNTIME << 24) | (fid & 0xFFFFFF) if (fid >> 24) == OWN else fid


pos = {}
sec = None
for raw in open(os.path.join(HERE, "..", "config", "positions.ltx"), encoding="utf-8"):
    line = raw.split(";")[0].strip()
    if line.startswith("["): sec = line[1:line.index("]")]; continue
    if sec == "pos_dlc2" and "=" in line:
        k, v = [x.strip() for x in line.split("=", 1)]
        x, y = [float(t) for t in v.split(",")[:2]]
        pos[int(k, 16)] = (x, y)

lctn = {}
for r in esp.walk(esp.load(D + "Dragonborn.esm")):
    if r.sig != "LCTN": continue
    L = dict(edid="", parent=0, kw=[], radius=0.0)
    for s, d in r.subs():
        if s == "EDID": L["edid"] = d.rstrip(b"\0").decode("latin1")
        elif s == "PNAM": L["parent"] = struct.unpack("<I", d)[0]
        elif s == "KWDA": L["kw"] = list(struct.unpack("<%dI" % (len(d) // 4), d))
        elif s == "RNAM" and len(d) == 4: L["radius"] = struct.unpack("<f", d)[0]
    lctn[r.fid] = L


def hold_of(fid):
    for _ in range(16):
        L = lctn.get(fid)
        if not L: return 0
        if HOLD_KW in L["kw"]: return fid
        fid = L["parent"]
    return 0


rows = []
for fid, L in lctn.items():
    local = fid & 0xFFFFFF
    if (fid >> 24) != OWN or local not in pos: continue
    x, y = pos[local]
    name = L["edid"].replace("DLC2", "").replace("Location", "") or "Place %06X" % local
    kws = ",".join(str(rt(k)) for k in L["kw"])
    rows.append("\t".join([str(rt(fid)), str(rt(L["parent"])), str(rt(hold_of(fid))), "0", repr(x), repr(y), "0.0", repr(L["radius"]), name, kws, "1"]))
out = os.path.join(HERE, "..", "..", "data", "world_solstheim.tsv")
if not os.path.isdir(os.path.dirname(out)): out = "../data/world_solstheim.tsv"
open(out, "w", newline="\n").write("\n".join(rows) + "\n")
print(f"wrote {out}: {len(rows)} Solstheim locations")
