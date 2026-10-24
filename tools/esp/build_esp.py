#!/usr/bin/env python3
"""Reproducible builder for ALife.esp (A-Life for Skyrim v1.7.6, slice 1).

Reads vanilla data from Skyrim.esm (targeted GRUP scans only, see fastscan.py) and writes a
valid ESL-flagged plugin whose FormIDs match include/ALife/Forms.h exactly:

  0x900 CELL ALifeHoldingCell          0x910-0x913 REFR slot/holding markers (persistent)
  0x920-0x923 PACK travel/sandbox/follow/flee (template packages + GetFactionRank conditions)
  0x924-0x925 PACK camp life hold / camp (sandbox template, radius 150 / 450; docs/CAMPLIFE.md, M5h)
  0x980-0x98E REFR camp life job markers (persistent, holding cell)
  0x930-0x938 FACT                     0x940 QUST MCM (ALife_MCM + SKI_PlayerLoadGameAlias)
  0x945 MESG uninstall yes/no          0x950-0x952 FLST perks/spells/artifacts
  0x960-0x963 KYWD                     0x970 LVLI raid haul
  0xA00-0xAA5 NPC_ pool bases (faces cloned from vanilla chargen presets)
  0xB00-0xBA5 ACHR pool refs (persistent, initially disabled, in the holding cell)

Usage: build_esp.py <Skyrim.esm> <out ALife.esp> [manifest.json] [--author NAME]

Skyrim.esm is the player's own copy; nothing from it is stored in this repo. --author sets the
plugin header's author field (TES4 CNAM, default "A-Life"); it is not part of any record.
"""
import json, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fastscan

MASTERS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm"]  # Spec line 64; Dragonborn only if a record needs it
OWN = len(MASTERS) << 24          # own records live at index == master count
FORM_VERSION = 44                 # Skyrim SE record version

def own(x): return OWN | x

# ------------------------------------------------------------------ low-level writers
def sub(tag, data):
    assert len(tag) == 4
    if len(data) > 0xFFFF:
        return b"XXXX" + struct.pack("<HI", 4, len(data)) + tag.encode() + struct.pack("<H", 0) + data
    return tag.encode() + struct.pack("<H", len(data)) + data

def z(s): return s.encode("cp1252") + b"\0"
def u32(x): return struct.pack("<I", x)
def i32(x): return struct.pack("<i", x)
def f32(x): return struct.pack("<f", x)

class Rec:
    def __init__(self, typ, fid, subs, flags=0):
        self.typ, self.fid, self.subs, self.flags = typ, fid, subs, flags
    def bytes(self):
        data = b"".join(self.subs)
        return self.typ.encode() + struct.pack("<IIIHHHH", len(data), self.flags, self.fid, 0, 0, FORM_VERSION, 0) + data

class Grp:
    def __init__(self, label, gtype, items):
        self.label, self.gtype, self.items = label, gtype, items
    def bytes(self):
        body = b"".join(i.bytes() for i in self.items)
        lab = self.label.encode() if isinstance(self.label, str) else struct.pack("<I", self.label)
        return b"GRUP" + struct.pack("<I", 24 + len(body)) + lab + struct.pack("<iHHI", self.gtype, 0, 0, 0) + body
    def count(self):
        return 1 + sum(i.count() if isinstance(i, Grp) else 1 for i in self.items)

# ------------------------------------------------------------------ vanilla constants (verified)
RACE = {"Nord": 0x13746, "Imperial": 0x13744, "Breton": 0x13741, "Redguard": 0x13748, "HighElf": 0x13743,
        "WoodElf": 0x13749, "DarkElf": 0x13742, "Orc": 0x13747, "Argonian": 0x13740, "Khajiit": 0x13745}
VAMP = {"Nord": 0x88794, "Imperial": 0x88844, "Breton": 0x8883C, "DarkElf": 0x8883D}
VOICE = {  # (male, female); beast races get beast voices
    "Nord": (0x13AE6, 0x13AE7), "Imperial": (0x13AD2, 0x13ADD), "Breton": (0x13AD2, 0x13ADC),
    "Redguard": (0x13AD5, 0x13AE0), "HighElf": (0x13AF0, 0x13AF1), "WoodElf": (0x13AD1, 0x13ADC),
    "DarkElf": (0x13AF2, 0x13AF3), "Orc": (0x13AEA, 0x13AEB), "Argonian": (0x13AEE, 0x13AEF),
    "Khajiit": (0x13AEC, 0x13AED)}
# Spec v1.7.6 pool (166): 6 per sex for each playable race, +6 per sex Breton and Altmer (24 each),
# +3 per sex Khajiit (18), plus 16 vampire-race actors (2 per sex x Nord/Imperial/Breton/DarkElf vampire races).
COUNTS = [("Nord", 6), ("Imperial", 6), ("Breton", 12), ("Redguard", 6), ("HighElf", 12), ("WoodElf", 6),
          ("DarkElf", 6), ("Orc", 6), ("Argonian", 6), ("Khajiit", 9)]
VAMPIRES = [(r, x) for r in ("Nord", "Imperial", "Breton", "DarkElf") for x in (0, 1) for _ in range(2)]
KW_ACTORTYPE_NPC = 0x13794
CLASS_WARRIOR1H = 0x13176
PLAYER_FACTION = 0xDB1
BANDIT_FACTION = 0x1BCC0
XMARKER, XMARKER_HEADING = 0x3B, 0x34
GOLD = 0xF

PERKS = [0xBABE4, 0xBABE8, 0xBABED, 0xBE123, 0xBCD2A, 0xBCCAE, 0x58F61, 0x58F62, 0x105F19, 0x52D50, 0x52D51,
         0x581F9, 0x58F6F, 0x51B17, 0x51B1B, 0x58F6D, 0xD8C33, 0x58F69, 0x58F68, 0xF3933, 0xF3F0E]
SPELLS = [0x12FCD, 0x2DD2A, 0x2B96B, 0x12FD0, 0x2B96C, 0x2DD29, 0x1C789, 0x45F9D, 0x45F9C, 0x10F7ED, 0x10F7EE,
          0x10F7EC, 0x7A82B, 0x7E8E5, 0x12FCC, 0x2F3B8, 0xB62EF, 0x12FD2, 0x5AD5C, 0x5AD5D, 0x51B16, 0x640B6, 0x204C3]
ARTIFACTS = [0xEA29C, 0x4E4EE, 0x35066, 0x2ACD2, 0x2AC6F, 0x240D2, 0x233E3, 0x45F96, 0x2C37B, 0x2AC61, 0x2AC60,
             0xD2846, 0x52794, 0x1CB36, 0x63B27, 0x63B29]
RAID_HAUL = [  # (level, form, count)
    (1, 0x9AF0A, 1), (1, 0xF961D, 1), (1, 0xE3E99, 1), (1, 0x98E5F, 1), (1, GOLD, 25), (1, 0x9AF11, 1),
    (1, 0xA297A, 1), (5, 0xA3F66, 1), (10, 0x4B58C, 1), (10, GOLD, 50)]

PACK_SRC = {0x920: 0xECEED, 0x921: 0xDD837, 0x922: 0xB2848, 0x923: 0x101D7D,   # travel, sandbox, follow, flee
            0x924: 0xDD837, 0x925: 0xDD837}                                     # camp life (M5h): hold a spot, sit / sleep at a fire or bed
PACK_EDID = {0x920: "ALife_PackTravel", 0x921: "ALife_PackSandbox", 0x922: "ALife_PackFollowLeader", 0x923: "ALife_PackFleeFrom",
             0x924: "ALife_PackCampHold", 0x925: "ALife_PackCampSit"}
PACK_RANK = {0x920: 1, 0x921: 2, 0x922: 3, 0x923: 4, 0x924: 5, 0x925: 6}
PACK_RADIUS = {0x920: 256, 0x921: 1200, 0x924: 150, 0x925: 450}   # PLDT linked-ref radius (follow / flee keep the template's)
JOB_MARKERS = 15                                                  # 0x980.. one per on-screen actor (kMaxOnlineActors)
FUNC_GETFACTIONRANK = 73

def ctda_rank(rank):
    # op 0x00 (==, AND), value, function, param1 = ALifeModeFaction, param2, run on subject, ref 0, param3 -1
    return sub("CTDA", struct.pack("<B3xfHHIIIIi", 0x00, float(rank), FUNC_GETFACTIONRANK, 0, own(0x934), 0, 0, 0, -1))

def build_packs(esm):
    res = fastscan.scan(esm, {"PACK"})
    by = {fid: s for fid, fl, s in res["PACK"]}
    out = []
    for local, src in PACK_SRC.items():
        s = by[src]
        subs = [sub("EDID", z(PACK_EDID[local]))]
        inserted = False
        for t, v in s:
            if t in ("EDID", "CTDA", "QNAM") or (t == "CNAM" and not inserted):
                continue
            if t == "PKCU" and not inserted:
                subs.append(ctda_rank(PACK_RANK[local]))
                inserted = True
            if t == "PLDT" and struct.unpack("<i", v[:4])[0] == 6:
                kw = own(0x961)
                radius = PACK_RADIUS.get(local, 1200)
                v = struct.pack("<iIi", 6, kw, radius)
            if t == "PTDA":
                ttype = struct.unpack("<i", v[:4])[0]
                if local == 0x922 and ttype == 3:
                    v = struct.pack("<iIi", 3, own(0x962), 0)       # follow linked ref (leader)
                elif local == 0x923 and ttype == 0:
                    v = struct.pack("<iIi", 3, own(0x963), 0)       # flee from linked ref
            if t == "PKDT" and local == 0x923:
                pass
            subs.append(sub(t, v))
        assert inserted, local
        out.append(Rec("PACK", own(local), subs))
    return out

def build_npcs(esm):
    res = fastscan.scan(esm, {"NPC_"})
    presets = {}
    for fid, fl, s in res["NPC_"]:
        acbs = fastscan.get(s, "ACBS")
        if not acbs or not (struct.unpack("<I", acbs[:4])[0] & 4):
            continue
        race = struct.unpack("<I", fastscan.get(s, "RNAM"))[0]
        fem = struct.unpack("<I", acbs[:4])[0] & 1
        presets.setdefault((race, fem), []).append((fastscan.edid(s), s))
    for k in presets:
        presets[k].sort()
    roster = []
    for sex in (0, 1):
        for race, n in COUNTS:
            for i in range(n):
                roster.append((race, sex, i, False))
    for race, sex in VAMPIRES:
        roster.append((race, sex, 2 + sum(1 for r in roster if r[0] == race and r[1] == sex and r[3]), True))
    assert len(roster) == 166, len(roster)
    npcs, manifest = [], []
    for idx, (race, sex, i, vamp) in enumerate(roster):
        plist = presets[(RACE[race], sex)]
        edid_p, ps = plist[i % len(plist)]
        variant = i // len(plist) + (1 if vamp else 0)   # faces past the preset count, and vampires, get perturbed morphs
        local = 0xA00 + idx
        race_id = VAMP[race] if vamp else RACE[race]
        tag = ("V" if vamp else "") + race + ("F" if sex else "M")
        edid = "ALife_Pool_%s%02d" % (tag, i + 1)
        get = lambda t: fastscan.get(ps, t)
        subs = [sub("EDID", z(edid)), sub("OBND", get("OBND"))]
        # ACBS: female flag only (no preset, no auto-calc, no PC level mult); DLL sets level/AVs at runtime
        subs.append(sub("ACBS", struct.pack("<IhhHHHHhHhH", sex, 50, 50, 1, 1, 81, 100, 35, 0, 50, 0)))
        for fac in (0x930, 0x934):
            subs.append(sub("SNAM", struct.pack("<IB3x", own(fac), 0)))
        subs.append(sub("VTCK", u32(VOICE[race][sex])))
        subs.append(sub("RNAM", u32(race_id)))
        subs.append(sub("ATKR", u32(race_id)))
        # AIDT: aggression 0 (set by DLL), confidence average, energy 50, morality no crime, assistance helps allies
        subs.append(sub("AIDT", struct.pack("<BBBBBBBBIII", 0, 2, 50, 3, 0, 1, 0, 0, 0, 0, 0)))
        for p in (0x923, 0x922, 0x921, 0x920, 0x924, 0x925):   # evaluated top-down: flee, follow, sandbox, travel, camp hold, camp sit (rank-exclusive)
            subs.append(sub("PKID", u32(own(p))))
        subs.append(sub("KSIZ", u32(2)))
        subs.append(sub("KWDA", u32(KW_ACTORTYPE_NPC) + u32(own(0x960))))
        subs.append(sub("CNAM", u32(CLASS_WARRIOR1H)))
        subs.append(sub("FULL", z("Traveler")))
        subs.append(sub("DATA", b""))
        subs.append(sub("DNAM", get("DNAM")))
        for t, v in ps:
            if t == "PNAM":
                subs.append(sub("PNAM", v))
        for t in ("HCLF",):
            if get(t): subs.append(sub(t, get(t)))
        subs.append(sub("NAM5", b"\xff\x00"))
        subs.append(sub("NAM6", f32(1.0)))
        w = struct.unpack("<f", get("NAM7"))[0] if get("NAM7") else 50.0
        nam9 = list(struct.unpack("<19f", get("NAM9"))) if get("NAM9") else [0.0] * 19
        if variant:
            import random
            rnd = random.Random(local)
            nam9 = [max(-1.0, min(1.0, x + rnd.uniform(-0.25, 0.25))) if k < 18 else x for k, x in enumerate(nam9)]
            w = max(0.0, min(100.0, w + rnd.uniform(-30, 30)))
        subs.append(sub("NAM7", f32(w)))
        subs.append(sub("NAM8", get("NAM8") or u32(1)))
        if get("FTST"): subs.append(sub("FTST", get("FTST")))
        if get("QNAM"): subs.append(sub("QNAM", get("QNAM")))
        subs.append(sub("NAM9", struct.pack("<19f", *nam9)))
        if get("NAMA"): subs.append(sub("NAMA", get("NAMA")))
        for t, v in ps:
            if t in ("TINI", "TINC", "TINV", "TIAS"):
                subs.append(sub(t, v))
        npcs.append(Rec("NPC_", own(local), subs))
        manifest.append({"local": "0x%03X" % local, "ref": "0x%03X" % (0xB00 + idx), "edid": edid, "race": "0x%X" % race_id,
                         "sex": "F" if sex else "M", "face": edid_p, "variant": variant})
    return npcs, manifest

def build_factions():
    def fact(local, edid, rel, hidden=True):
        subs = [sub("EDID", z(edid))]
        for other, reaction in rel:     # reaction: 0 neutral, 1 enemy, 2 ally, 3 friend
            subs.append(sub("XNAM", struct.pack("<IiI", other, 0, reaction)))
        subs.append(sub("DATA", u32(0x1 if hidden else 0)))
        return Rec("FACT", own(local), subs)
    return [
        fact(0x930, "ALifeFaction", [(own(0x930), 3)]),
        # No Friend/Ally to vanilla BanditFaction: CK requires reciprocal Friend/Ally (Faction Reaction Error), and
        # overriding the vanilla faction would hurt compatibility. Default (neutral) keeps the two from fighting.
        fact(0x931, "ALifeBanditFaction", [(PLAYER_FACTION, 0), (own(0x931), 2)]),  # Neutral to Player; Enemy only via ALifePlayerEnemyFaction
        fact(0x932, "ALifeHunterFaction", [(own(0x932), 2)]),
        fact(0x933, "ALifeAdventurerFaction", [(BANDIT_FACTION, 1), (own(0x933), 2)]),
        fact(0x934, "ALifeModeFaction", []),
        fact(0x935, "ALifeSlot0Faction", [(own(0x935), 2)]),
        fact(0x936, "ALifeSlot1Faction", [(own(0x936), 2)]),
        fact(0x937, "ALifeSlot2Faction", [(own(0x937), 2)]),
        fact(0x938, "ALifePlayerEnemyFaction", [(PLAYER_FACTION, 1)]),
    ]

def vmad_quest():
    def wstr(s): return struct.pack("<H", len(s)) + s.encode()
    # script ALife_MCM with property UninstallMessage -> MESG 0x945
    prop = wstr("UninstallMessage") + struct.pack("<BB", 1, 1) + struct.pack("<HhI", 0, -1, own(0x945))
    scripts = struct.pack("<hhH", 5, 2, 1) + wstr("ALife_MCM") + struct.pack("<BH", 0, 1) + prop
    # quest fragments: version 2, no fragments, empty file name; one alias (id 0) with SKI_PlayerLoadGameAlias
    alias_obj = struct.pack("<HhI", 0, 0, own(0x940))
    alias = alias_obj + struct.pack("<hhH", 5, 2, 1) + wstr("SKI_PlayerLoadGameAlias") + struct.pack("<BH", 0, 0)
    frag = struct.pack("<BH", 2, 0) + wstr("") + struct.pack("<H", 1) + alias
    return scripts + frag

def build_quest():
    return Rec("QUST", own(0x940), [
        sub("EDID", z("ALife_MCMQuest")), sub("VMAD", vmad_quest()), sub("FULL", z("A-Life")),
        sub("DNAM", bytes.fromhex("110100ff0000000000000000")),   # start game enabled (same as SkyUI's own MCM quests)
        sub("NEXT", b""), sub("ANAM", u32(1)),
        sub("ALST", u32(0)), sub("ALID", z("PlayerAlias")), sub("FNAM", u32(0)), sub("ALFR", u32(0x14)),
        sub("VTCK", u32(0)), sub("ALED", b"")])

def build_mesg():
    return Rec("MESG", own(0x945), [
        sub("EDID", z("ALife_UninstallMessage")),
        sub("DESC", z("Prepare A-Life for uninstall?\n\nAll A-Life groups leave the world and the simulation stops. "
                      "Save your game afterwards, then remove the mod.")),
        sub("FULL", z("A-Life")), sub("INAM", u32(0)), sub("DNAM", u32(1)),
        sub("ITXT", z("Yes")), sub("ITXT", z("No"))])

def build_flst(local, edid, forms):
    return Rec("FLST", own(local), [sub("EDID", z(edid))] + [sub("LNAM", u32(f)) for f in forms])

def build_lvli():
    subs = [sub("EDID", z("ALife_RaidHaul")), sub("OBND", b"\0" * 12), sub("LVLD", b"\x00"), sub("LVLF", b"\x03"),
            sub("LLCT", bytes([len(RAID_HAUL)]))]
    for lvl, form, cnt in sorted(RAID_HAUL):
        subs.append(sub("LVLO", struct.pack("<HHIHH", lvl, 0, form, cnt, 0)))
    return Rec("LVLI", own(0x970), subs)

def build_keywords():
    names = {0x960: "ALifePoolActor", 0x961: "ALifeTravelTarget", 0x962: "ALifeLeader", 0x963: "ALifeFleeTarget"}
    return [Rec("KYWD", own(k), [sub("EDID", z(v))]) for k, v in names.items()]

def ref_data(x, y, z_, rz=0.0):
    return sub("DATA", struct.pack("<6f", x, y, z_, 0.0, 0.0, rz))

def build_cell(npc_count):
    cell = Rec("CELL", own(0x900), [
        sub("EDID", z("ALifeHoldingCell")), sub("FULL", z("A-Life Holding")),
        sub("DATA", struct.pack("<H", 0x0001)),                 # interior
        sub("LTMP", u32(0)),
    ])
    PERSIST, INIT_DISABLED = 0x400, 0x800
    refs = []
    for k in range(3):
        refs.append(Rec("REFR", own(0x910 + k), [sub("EDID", z("ALifeSlotMarker%d" % k)), sub("NAME", u32(XMARKER_HEADING)),
                                                 ref_data(0.0, 512.0 * (k + 1), 0.0)], PERSIST))
    refs.append(Rec("REFR", own(0x913), [sub("EDID", z("ALifeHoldingMarker")), sub("NAME", u32(XMARKER_HEADING)),
                                         ref_data(0.0, 0.0, 0.0)], PERSIST))
    for k in range(JOB_MARKERS):                                 # camp life job markers (M5h): guard / sniper / camper spots
        refs.append(Rec("REFR", own(0x980 + k), [sub("EDID", z("ALifeJobMarker%02d" % k)), sub("NAME", u32(XMARKER_HEADING)),
                                                 ref_data(256.0, 128.0 * k, 0.0)], PERSIST))
    for i in range(npc_count):
        x = (i % 16) * 128.0 - 1024.0
        y = (i // 16) * 128.0 - 2048.0
        refs.append(Rec("ACHR", own(0xB00 + i), [sub("NAME", u32(own(0xA00 + i))), ref_data(x, y, 0.0)], PERSIST | INIT_DISABLED))
    obj = 0x900
    block, subblock = obj % 10, (obj // 10) % 10
    persistent = Grp(own(0x900), 8, refs)
    children = Grp(own(0x900), 6, [persistent])
    return Grp("CELL", 0, [Grp(block, 2, [Grp(subblock, 3, [cell, children])])])

def main():
    args, author = list(sys.argv[1:]), "A-Life"
    if "--author" in args:
        k = args.index("--author"); author = args[k + 1]; del args[k:k + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    esm, out = args[0], args[1]
    man_path = args[2] if len(args) > 2 else None
    npcs, manifest = build_npcs(esm)
    groups = [
        Grp("KYWD", 0, build_keywords()),
        Grp("FACT", 0, build_factions()),
        Grp("NPC_", 0, npcs),
        Grp("LVLI", 0, [build_lvli()]),
        build_cell(len(npcs)),
        Grp("QUST", 0, [build_quest()]),
        Grp("PACK", 0, build_packs(esm)),
        Grp("FLST", 0, [build_flst(0x950, "ALife_WorkingPerks", PERKS), build_flst(0x951, "ALife_CastableSpells", SPELLS),
                        build_flst(0x952, "ALife_DaedricArtifacts", ARTIFACTS)]),
        Grp("MESG", 0, [build_mesg()]),
    ]
    count = sum(g.count() for g in groups)
    used = [0x900, 0x910, 0x911, 0x912, 0x913, 0x920, 0x921, 0x922, 0x923, 0x924, 0x925] + list(range(0x930, 0x939)) +            list(range(0x980, 0x980 + JOB_MARKERS)) + \
           [0x940, 0x945, 0x950, 0x951, 0x952, 0x960, 0x961, 0x962, 0x963, 0x970] + \
           list(range(0xA00, 0xA00 + len(npcs))) + list(range(0xB00, 0xB00 + len(npcs)))
    assert max(used) <= 0xFFF, "ESL range"
    hedr = sub("HEDR", struct.pack("<fII", 1.71, count, max(used) + 1))
    tes4 = [hedr, sub("CNAM", z(author)), sub("SNAM", z("A-Life for Skyrim v1.7.6 (slice 1: Whiterun Hold)"))]
    for m in MASTERS:
        tes4 += [sub("MAST", z(m)), sub("DATA", struct.pack("<Q", 0))]
    tes4.append(sub("INTV", u32(0)))
    header = Rec("TES4", 0, tes4, 0x200)        # ESL flag
    blob = header.bytes() + b"".join(g.bytes() for g in groups)
    with open(out, "wb") as f:
        f.write(blob)
    if man_path:
        with open(man_path, "w") as f:
            json.dump({"records": count, "nextObjectId": "0x%X" % (max(used) + 1), "pool": manifest}, f, indent=1)
    print("wrote", out, len(blob), "bytes; records+groups =", count, "; nextObjectId = 0x%X" % (max(used) + 1))

if __name__ == "__main__":
    main()
