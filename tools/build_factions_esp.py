"""Builds ALifeFactions.esp (ESL-flagged) from config/factions.ltx.

One hidden FACT record per A-Life faction (FormID 0x800 + index in enum Faction), relations mirrored from [relations]
(ally = combat reaction 2, enemy = 1), plus ties to the vanilla analogue faction so vanilla and A-Life actors of the same
kind do not fight each other. Masters: Skyrim.esm, Update.esm, Dawnguard.esm (same as ALife.esp).

usage: python build_factions_esp.py [factions.ltx] [out.esp]
"""
import struct, sys, os

FACTIONS = ["bandit", "forsworn", "imperial", "stormcloak", "thalmor", "wanderer", "hunter", "mercenary", "mage",
            "necromancer", "vampire", "dawnguard", "silverhand", "vigilant", "caravan"]   # must match enum Faction order in Core.h
MASTERS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm"]
OWN = 0x03000000          # load-order index of this plugin (after 3 masters)
BASE_ID = 0x800

# vanilla analogue factions (allies of the A-Life faction)  [FormID with master index in the high byte]
VANILLA_ALLY = {
    "bandit":      [0x0001BCC0, 0x00044EF6],      # BanditFaction, BanditFriendFaction
    "forsworn":    [0x00043599],                  # ForswornFaction
    "necromancer": [0x00034B74, 0x00026724],      # NecromancerFaction, WarlockFaction
    "vampire":     [0x00027242, 0x02003376],      # VampireFaction, DLC1VampireFaction
    "thalmor":     [0x00039F26],                  # ThalmorFaction
    "silverhand":  [0x000AA0A4],                  # SilverHandFaction
    "vigilant":    [0x000B3292],                  # VigilantOfStendarrFaction
    "hunter":      [0x0004E509],                  # HunterFaction
    "mage":        [0x0001F259],                  # CollegeofWinterholdFaction
    "dawnguard":   [0x02014217],                  # DLC1DawnguardFaction
}
# factions that are hostile to town guards
VANILLA_ENEMY = {f: [0x00086EEE] for f in ("bandit", "forsworn", "necromancer", "vampire", "silverhand")}   # IsGuardFaction


def parse_relations(path):
    ally, enemy = {f: set() for f in FACTIONS}, {f: set() for f in FACTIONS}
    sec = None
    for raw in open(path, encoding="utf-8"):
        line = raw.split(";")[0].strip()
        if not line: continue
        if line.startswith("["): sec = line[1:line.index("]")]; continue
        if sec != "relations" or "=" not in line: continue
        k, v = [x.strip() for x in line.split("=", 1)]
        if k not in FACTIONS: continue
        for part in v.split("|"):
            kind, _, lst = part.partition(":")
            kind = kind.strip().lower()
            for o in [x.strip() for x in lst.split(",") if x.strip()]:
                if o not in FACTIONS: continue
                if kind == "ally": ally[k].add(o)
                elif kind == "enemy": enemy[k].add(o)   # friend / rival only shape reputation, not engine combat reactions
    for a in FACTIONS:   # symmetric
        for b in list(ally[a]): ally[b].add(a)
        for b in list(enemy[a]): enemy[b].add(a)
    return ally, enemy


def sub(sig, data): return sig.encode() + struct.pack("<H", len(data)) + data
def zstr(s): return s.encode("latin1") + b"\0"


def record(sig, fid, subs):
    body = b"".join(subs)
    return sig.encode() + struct.pack("<IIIIHH", len(body), 0, fid, 0, 44, 0) + body


def build(ltx, out):
    ally, enemy = parse_relations(ltx)
    recs = []
    fid = lambda f: OWN | (BASE_ID + FACTIONS.index(f))
    for f in FACTIONS:
        subs = [sub("EDID", zstr("ALifeF_" + f))]
        xn = []
        xn.append((fid(f), 0, 3))   # itself: friend
        for o in FACTIONS:
            if o == f: continue
            if o in ally[f]: xn.append((fid(o), 0, 2))
            elif o in enemy[f]: xn.append((fid(o), 0, 1))
        for v in VANILLA_ALLY.get(f, []): xn.append((v, 0, 2))
        for v in VANILLA_ENEMY.get(f, []): xn.append((v, 0, 1))
        for (a, m, r) in xn: subs.append(sub("XNAM", struct.pack("<IiI", a, m, r)))
        subs.append(sub("DATA", struct.pack("<I", 1)))   # hidden from the player
        recs.append(record("FACT", fid(f), subs))
    grup_body = b"".join(recs)
    grup = b"GRUP" + struct.pack("<I", 24 + len(grup_body)) + b"FACT" + struct.pack("<iHHHH", 0, 0, 0, 0, 0) + grup_body
    hedr = sub("HEDR", struct.pack("<fII", 1.71, len(recs), BASE_ID + len(FACTIONS)))
    tes4_subs = [hedr, sub("CNAM", zstr("A-Life")), sub("SNAM", zstr("A-Life factions (generated from factions.ltx)"))]
    for m in MASTERS:
        tes4_subs += [sub("MAST", zstr(m)), sub("DATA", struct.pack("<Q", 0))]
    tes4_subs.append(sub("INTV", struct.pack("<I", 0)))
    tes4 = record("TES4", 0, tes4_subs)
    # TES4 flags: 0x200 = ESL (light master); fix the flags field in the record header
    tes4 = tes4[:8] + struct.pack("<I", 0x200) + tes4[12:]
    open(out, "wb").write(tes4 + grup)
    print(f"wrote {out}: {len(recs)} factions, {len(tes4) + len(grup)} bytes")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    ltx = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "config", "factions.ltx")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "package", "ALifeFactions.esp")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    build(ltx, out)
