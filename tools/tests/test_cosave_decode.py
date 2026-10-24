"""Tests for tools/cosave_decode.py (ticket G6). Python 3 standard library only.

    python3 -m unittest tools/tests/test_cosave_decode.py -v

Every fixture is synthetic: the builders below pack each record by hand with struct.pack, field by field in the order
docs/COSAVE.md gives (independently of the decoder's layout functions), and write the .skse files to
tools/tests/fixtures/cosave/. No real save and no game data is involved.
"""
import os
import re
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "cosave_decode.py")
DOC = os.path.join(ROOT, "docs", "COSAVE.md")
SAVE_CPP = os.path.join(ROOT, "src", "Save.cpp")
FIXTURES = os.path.join(HERE, "fixtures", "cosave")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import cosave_decode as cd  # noqa: E402


# ------------------------------------------------------------------------------------------------ hand packers
class P:
    """Little-endian packer; one method per C++ type of docs/COSAVE.md."""
    def __init__(self):
        self.b = bytearray()

    def u8(self, v): self.b += struct.pack("<B", v); return self
    def i8(self, v): self.b += struct.pack("<b", v); return self
    def bool(self, v): self.b += b"\x01" if v else b"\x00"; return self
    def u32(self, v): self.b += struct.pack("<I", v); return self
    def i32(self, v): self.b += struct.pack("<i", v); return self
    def u64(self, v): self.b += struct.pack("<Q", v); return self
    def f32(self, v): self.b += struct.pack("<f", v); return self
    def f64(self, v): self.b += struct.pack("<d", v); return self
    form = u32

    def str(self, s):
        raw = s.encode("utf-8")
        self.u32(len(raw))
        self.b += raw
        return self


def sample_member(i):
    return {"id": 9000 + i, "name": "Hroki %d" % i, "title": "the Bold", "faction": 2, "role": 5, "race": 6, "sex": 1,
            "classId": 0x00013176, "level": 12, "levelBias": -2, "gearLevel": 10,
            "skills": [float(15 + k) for k in range(18)],
            "health": 100.5, "magicka": 50.0, "stamina": 75.25, "healthMax": 120.0, "magickaMax": 60.0, "staminaMax": 80.0,
            "gold": 37, "inventory": [{"form": 0x00012EB7, "count": 1}, {"form": 0x0001397E, "count": 12}],
            "equipped": [0x00012EB7], "perks": [0x000BABE4, 0x00079342], "spells": [],
            "hunger": 0.5, "cold": 0.0, "fatigue": 0.25, "sticky": True, "veteran": False, "waitlisted": True,
            "poolLocal": -1, "growthDay": 41, "gainToday": [k % 3 for k in range(18)],
            "fightBonusToday": 1.5, "rankXp": 33.0, "kills": 4, "arrows": 20}


def pack_member(p, m):
    p.u64(m["id"]).str(m["name"]).str(m["title"]).u8(m["faction"]).u8(m["role"]).u8(m["race"]).u8(m["sex"])
    p.form(m["classId"]).i32(m["level"]).i32(m["levelBias"]).i32(m["gearLevel"])
    for x in m["skills"]:
        p.f32(x)
    for k in ("health", "magicka", "stamina", "healthMax", "magickaMax", "staminaMax"):
        p.f32(m[k])
    p.i32(m["gold"]).u32(len(m["inventory"]))
    for it in m["inventory"]:
        p.form(it["form"]).i32(it["count"])
    for k in ("equipped", "perks", "spells"):
        p.u32(len(m[k]))
        for f in m[k]:
            p.form(f)
    p.f32(m["hunger"]).f32(m["cold"]).f32(m["fatigue"]).bool(m["sticky"]).bool(m["veteran"]).bool(m["waitlisted"])
    p.i32(m["poolLocal"]).u32(m["growthDay"])
    for x in m["gainToday"]:
        p.u8(x)
    p.f32(m["fightBonusToday"]).f32(m["rankXp"]).i32(m["kills"]).i32(m["arrows"])


def sample_squad(i, members):
    return {"id": 500 + i, "faction": 19 if i else 0, "tier": 1, "section": "bandit_advanced", "x": 1024.5, "y": -2048.0,
            "z": 16.0, "node": 0x00018A56, "prevNode": 0, "activity": 2, "actStep": 1, "actHours": 2.5, "action": 2,
            "assignedTarget": 0x00018A56, "currentTarget": 0x00018A57, "smartAssigned": 0, "stayStart": 1234.5,
            "idleHours": 4.0, "locked": True, "lockedUntil": 1250.0, "respawnSmart": 0x00018A56, "respawnEntry": "bandits",
            "retreatUntilHours": 1251.5, "avoidNode": 0x00018A58, "commonSquad": True, "standing": -150, "letGoCount": 1,
            "foughtPlayer": True, "attackedByPlayer": False, "lastMetHours": 1200.0, "trust": 1, "helpedByPlayer": False,
            "startStrength": 640.0, "startMembers": len(members), "memory": -25.5, "tollUntil": 0.0, "members": members}


def pack_squad(p, g):
    p.u64(g["id"]).u8(g["faction"]).u8(g["tier"]).str(g["section"]).f32(g["x"]).f32(g["y"]).f32(g["z"])
    p.form(g["node"]).form(g["prevNode"]).u8(g["activity"]).i32(g["actStep"]).f32(g["actHours"])
    p.u8(g["action"]).form(g["assignedTarget"]).form(g["currentTarget"]).form(g["smartAssigned"])
    p.f64(g["stayStart"]).f64(g["idleHours"]).bool(g["locked"]).f64(g["lockedUntil"])
    p.form(g["respawnSmart"]).str(g["respawnEntry"])
    p.f64(g["retreatUntilHours"]).form(g["avoidNode"]).bool(g["commonSquad"])
    p.i32(g["standing"]).i32(g["letGoCount"]).bool(g["foughtPlayer"]).bool(g["attackedByPlayer"]).f64(g["lastMetHours"])
    p.u8(g["trust"]).bool(g["helpedByPlayer"]).f32(g["startStrength"]).i32(g["startMembers"]).f32(g["memory"]).f64(g["tollUntil"])
    p.u32(len(g["members"]))
    for m in g["members"]:
        pack_member(p, m)


def sample_sim2(ver):
    nf = {208: 32, 207: 28, 206: 21, 205: 15}[ver]
    rec = {"now": 1250.75, "gateOpened": True, "nextId": 123456, "worldSeed": 0xDEADBEEF12345678,
           "goodwill": [float(f * 10 - 50) for f in range(nf)], "notoriety": 12.5,
           "rumors": [{"faction": 2, "delta": -5.0, "dueHours": 1260.0}],
           "waitlist": [9001, 9002], "queuedKills": [{"cell": 0x0001A26F, "kills": 2}],
           "smarts": [{"location": 0x00018A56, "lastRespawn": 1100.0, "faction": 255, "raidedUntil": 0.0,
                       "respawn": [{"name": "bandits", "spawned": 1}, {"name": "wolves", "spawned": 0}]},
                      {"location": 0x00018A57, "lastRespawn": -1.0, "faction": 0, "raidedUntil": 1300.0, "respawn": []}],
           "merchants": [{"chest": 0x000A6E3E, "baselineGold": 750, "itemsBought": 3, "goldStolen": 0, "cycleStart": 1200.0}],
           "squads": [sample_squad(0, [sample_member(0), sample_member(1)]), sample_squad(1, [])]}
    if ver >= 206:
        rec["creatures"] = [0xFF000801, 0xFF000802]
    return rec


def pack_sim2(ver, r):
    p = P().f64(r["now"]).bool(r["gateOpened"]).u64(r["nextId"]).u64(r["worldSeed"])
    if ver >= 207:
        p.u8(len(r["goodwill"]))
    for x in r["goodwill"]:
        p.f32(x)
    p.f32(r["notoriety"]).u32(len(r["rumors"]))
    for ru in r["rumors"]:
        p.u8(ru["faction"]).f32(ru["delta"]).f64(ru["dueHours"])
    p.u32(len(r["waitlist"]))
    for x in r["waitlist"]:
        p.u64(x)
    p.u32(len(r["queuedKills"]))
    for q in r["queuedKills"]:
        p.form(q["cell"]).i32(q["kills"])
    p.u32(len(r["smarts"]))
    for s in r["smarts"]:
        p.form(s["location"]).f64(s["lastRespawn"]).u8(s["faction"]).f64(s["raidedUntil"]).u32(len(s["respawn"]))
        for e in s["respawn"]:
            p.str(e["name"]).i32(e["spawned"])
    p.u32(len(r["merchants"]))
    for m in r["merchants"]:
        p.form(m["chest"]).i32(m["baselineGold"]).i32(m["itemsBought"]).i32(m["goldStolen"]).f64(m["cycleStart"])
    p.u32(len(r["squads"]))
    for g in r["squads"]:
        pack_squad(p, g)
    if ver >= 206:
        p.u32(len(r["creatures"]))
        for f in r["creatures"]:
            p.form(f)
    return bytes(p.b)


def sample_task(ver, i):
    t = {"id": 7 + i, "type": 9 if ver >= 3 else 1, "stage": 0, "slot": i, "giver": 500, "giverFaction": 2,
         "giverName": "Legate Rikke", "target": 501, "targetMember": 9001, "targetFaction": 19 if ver >= 2 else 16,
         "targetName": "Draugr Wight", "pinned": [502, 503], "node": 14, "place": "Bleak Falls Barrow",
         "item": 0x0003AD5B, "itemName": "Sealed Parcel", "count": 3, "gold": 250, "offeredAt": 1000.0, "acceptedAt": 1001.5,
         "parcelMissingSince": -1.0}
    if ver >= 3:
        t.update({"dest": 22, "marks": [{"squad": 501, "member": 9001}, {"squad": 504, "member": 9010}], "step": 1,
                  "extra": 0x1122334455667788, "corpse": 0xFF000C01, "deadline": 1300.25, "squadDesc": "draugr_guard"})
    return t


def pack_task(p, ver, t):
    p.u32(t["id"]).u8(t["type"]).u8(t["stage"]).i32(t["slot"]).u64(t["giver"]).u8(t["giverFaction"]).str(t["giverName"])
    p.u64(t["target"]).u64(t["targetMember"]).u8(t["targetFaction"]).str(t["targetName"]).u32(len(t["pinned"]))
    for x in t["pinned"]:
        p.u64(x)
    p.u32(t["node"]).str(t["place"]).form(t["item"]).str(t["itemName"]).i32(t["count"]).i32(t["gold"])
    p.f64(t["offeredAt"]).f64(t["acceptedAt"]).f64(t["parcelMissingSince"])
    if ver >= 3:
        p.u32(t["dest"]).u32(len(t["marks"]))
        for m in t["marks"]:
            p.u64(m["squad"]).u64(m["member"])
        p.i32(t["step"]).u64(t["extra"]).form(t["corpse"]).f64(t["deadline"]).str(t["squadDesc"])


def sample_taskrec(ver):
    o = sample_task(ver, -1)
    o["slot"] = -1
    offer = {"giver": 600, "until": 1100.0}
    offer.update(o)
    return {"nextId": 12, "active": [sample_task(ver, 0), sample_task(ver, 1)], "offers": [offer],
            "cooldown": [{"giver": 601, "until": 1080.0}], "markers": [0xFF000900 + k for k in range(8)]}


def pack_taskrec(ver, r):
    p = P().u32(r["nextId"]).u32(len(r["active"]))
    for t in r["active"]:
        pack_task(p, ver, t)
    p.u32(len(r["offers"]))
    for o in r["offers"]:
        p.u64(o["giver"]).f64(o["until"])
        pack_task(p, ver, o)
    p.u32(len(r["cooldown"]))
    for c in r["cooldown"]:
        p.u64(c["giver"]).f64(c["until"])
    for f in r["markers"]:
        p.form(f)
    return bytes(p.b)


def sample_comp(ver):
    return {"companions": [{"squad": 500, "name": "Hroki 0", "paid": True, "joinedAt": 1000.0, "until": 1168.0, "warned": False,
                            "order": 1, "holdFire": False, "loot": True, "cell": 0, "ws": 0x0000003C,
                            "wx": 100.0, "wy": 200.0, "wz": -8.5}],
            "rolls": [{"squad": 500, "willing": True, "factor": 1.25}, {"squad": 501, "willing": False, "factor": 1.0}]}


def pack_comp(ver, r):
    p = P().u32(len(r["companions"]))
    for c in r["companions"]:
        p.u64(c["squad"]).str(c["name"]).bool(c["paid"]).f64(c["joinedAt"]).f64(c["until"]).bool(c["warned"])
        p.u8(c["order"]).bool(c["holdFire"]).bool(c["loot"]).form(c["cell"]).form(c["ws"]).f32(c["wx"]).f32(c["wy"]).f32(c["wz"])
    p.u32(len(r["rolls"]))
    for x in r["rolls"]:
        p.u64(x["squad"]).bool(x["willing"]).f32(x["factor"])
    return bytes(p.b)


def sample_wond(ver):
    return {"down": [{"member": 9001, "squad": 500, "since": 1240.0, "upAt": 0.0, "healthBefore": 35.5, "flags": 3}],
            "fights": [{"squad": 500, "rolled": True, "yielded": False}]}


def pack_wond(ver, r):
    p = P().u32(len(r["down"]))
    for d in r["down"]:
        p.u64(d["member"]).u64(d["squad"]).f64(d["since"]).f64(d["upAt"]).f32(d["healthBefore"]).u8(d["flags"])
    p.u32(len(r["fights"]))
    for f in r["fights"]:
        p.u64(f["squad"]).bool(f["rolled"]).bool(f["yielded"])
    return bytes(p.b)


def sample_stsh(ver):
    return {"spots": [{"id": 1, "node": 14, "ws": 0, "x": 10.0, "y": 20.0, "z": 30.0, "base": 0x000E8F4C, "state": 1,
                       "rolled": False, "slot": 0, "ref": 0xFF000A01, "markedAt": 1222.0,
                       "items": [{"form": 0x0000000F, "count": 120}]},
                      {"id": 2, "node": 15, "ws": 1, "x": -1.0, "y": -2.0, "z": 0.5, "base": 0x000E8F4C, "state": 0,
                       "rolled": False, "slot": -1, "ref": 0, "markedAt": 0.0, "items": []}],
            "found": 3, "notesRead": 5, "revealed": 2, "rewards": 1, "markers": [0xFF000B00 + k if k < 2 else 0 for k in range(32)]}


def pack_stsh(ver, r):
    p = P().u32(len(r["spots"]))
    for s in r["spots"]:
        p.u32(s["id"]).u32(s["node"]).u8(s["ws"]).f32(s["x"]).f32(s["y"]).f32(s["z"]).form(s["base"]).u8(s["state"])
        p.bool(s["rolled"]).i8(s["slot"]).form(s["ref"]).f64(s["markedAt"]).u32(len(s["items"]))
        for it in s["items"]:
            p.form(it["form"]).i32(it["count"])
    p.u64(r["found"]).u64(r["notesRead"]).u64(r["revealed"]).u64(r["rewards"])
    for f in r["markers"]:
        p.form(f)
    return bytes(p.b)


def sample_news(ver):
    nf = 32
    return {"lines": ["Day 3: the Legion took Fort Greymoor", "Day 4: bandits raided Riverwood"], "factions": nf,
            "relations": [(x * nf + y) % 2001 - 1000 for x in range(nf) for y in range(nf)]}


def pack_news(ver, r):
    p = P().u32(len(r["lines"]))
    for s in r["lines"]:
        p.str(s)
    p.u32(r["factions"])
    for v in r["relations"]:
        p.i32(v)
    return bytes(p.b)


def sample_drgn(ver):
    return {"dragonLast": 1180.5, "dragonKills": 2}


def pack_drgn(ver, r):
    return bytes(P().f64(r["dragonLast"]).u32(r["dragonKills"]).b)


def sample_diff(ver):
    return {"followGame": 1, "sliders": [1.0, 1.5, 0.75, 1.0, 2.0, 0.5, 1.25, 1.0]}


def pack_diff(ver, r):
    p = P().u8(r["followGame"]).u8(len(r["sliders"]))
    for x in r["sliders"]:
        p.f32(x)
    return bytes(p.b)


# every type and version docs/COSAVE.md lists
CASES = {
    "SIM2": ((205, 206, 207, 208), sample_sim2, pack_sim2),
    "TASK": ((1, 2, 3), sample_taskrec, pack_taskrec),
    "COMP": ((1,), sample_comp, pack_comp),
    "WOND": ((1,), sample_wond, pack_wond),
    "STSH": ((1,), sample_stsh, pack_stsh),
    "NEWS": ((1,), sample_news, pack_news),
    "DRGN": ((1,), sample_drgn, pack_drgn),
    "DIFF": ((1,), sample_diff, pack_diff),
}
WRITE_ORDER = ["SIM2", "TASK", "COMP", "WOND", "STSH", "NEWS", "DRGN", "DIFF"]   # src/Save.cpp:155-188


def chunk(code, ver, data):
    return (cd.fourcc(code), ver, data)


def write_fixture(name, data):
    os.makedirs(FIXTURES, exist_ok=True)
    path = os.path.join(FIXTURES, name)
    with open(path, "wb") as f:
        f.write(data)
    return path


def run_tool(*args):
    p = subprocess.run([sys.executable, TOOL] + list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, cwd=ROOT)
    return p.returncode, p.stdout


def doc_text():
    with open(DOC, encoding="utf-8") as f:
        return f.read()


class RoundTrip(unittest.TestCase):
    def test_every_type_and_version(self):
        n = 0
        for code, (versions, sample, pack) in CASES.items():
            for ver in versions:
                with self.subTest(record=code, version=ver):
                    rec = sample(ver)
                    raw = pack(ver, rec)
                    got, warn = cd.decode_record(code, ver, raw)
                    self.assertIsNone(warn)
                    self.assertEqual(got, rec)
                    self.assertEqual(cd.encode_record(code, ver, got), raw)
                    n += 1
        print("\n  round-tripped %d record type/version pairs" % n)
        self.assertEqual(n, 13)

    def test_cases_cover_decoder_and_doc(self):
        self.assertEqual({c: set(v[0]) for c, v in CASES.items()}, {c: set(v[0]) for c, v in cd.RECORDS.items()})
        doc = doc_text()
        for code, (versions, _, _) in CASES.items():
            self.assertRegex(doc, r"### `%s`" % code, code)
            for ver in versions:
                self.assertRegex(doc, r"`%s` v%d\b" % (code, ver), "%s v%d not listed in docs/COSAVE.md" % (code, ver))

    def test_full_file_current_versions(self):
        chunks = [chunk(c, cd.CURRENT[c], CASES[c][2](cd.CURRENT[c], CASES[c][1](cd.CURRENT[c]))) for c in WRITE_ORDER]
        other = (cd.fourcc("PAPY"), [chunk("DATA", 1, b"\x01\x02\x03")])   # another plugin's data is skipped
        data = cd.build_cosave([other, (cd.ALIFE_UID, chunks)])
        self.assertEqual(data[:4], b"SKSE")
        self.assertEqual(data[cd.HEADER.size + 12 + 12 + 3:][:4], b"FILA")   # 'ALIF' as MSVC stores it
        path = write_fixture("all_current.skse", data)
        code, out = run_tool(path)
        self.assertEqual(code, 0, out)
        self.assertNotIn("WARNING", out)
        for c in WRITE_ORDER:
            self.assertIn("== %s v%d" % (c, cd.CURRENT[c]), out)
        self.assertIn("plugin PAPY (other plugin, skipped)", out)
        self.assertIn("faction = 19 (draugr)", out)
        self.assertIn("population=1", out)

    def test_old_versions_files(self):
        for ver in (205, 206, 207):
            data = cd.build_cosave([(cd.ALIFE_UID, [chunk("SIM2", ver, pack_sim2(ver, sample_sim2(ver)))])])
            path = write_fixture("sim2_v%d.skse" % ver, data)
            code, out = run_tool(path)
            self.assertEqual(code, 0, out)
            self.assertIn("== SIM2 v%d" % ver, out)
            self.assertIn("faction = 19 (spriggan, now 22)", out)   # pre-208: 19 was the spriggans (monsters from 15), now 22
        for ver in (1, 2):
            data = cd.build_cosave([(cd.ALIFE_UID, [chunk("TASK", ver, pack_taskrec(ver, sample_taskrec(ver)))])])
            path = write_fixture("task_v%d.skse" % ver, data)
            code, out = run_tool(path)
            self.assertEqual(code, 0, out)
            self.assertIn("== TASK v%d" % ver, out)
        self.assertIn("targetFaction = 19 (draugr)", out)   # v2: no shift


class Robustness(unittest.TestCase):
    def test_truncated_record(self):
        raw = pack_sim2(208, sample_sim2(208))
        cut = raw[:len(raw) // 2]
        rec, warn = cd.decode_record("SIM2", 208, cut)
        self.assertIn("truncated", warn)
        self.assertEqual(rec["nextId"], 123456)                 # what came before the cut is still decoded
        drgn = pack_drgn(1, sample_drgn(1))
        data = cd.build_cosave([(cd.ALIFE_UID, [chunk("SIM2", 208, cut), chunk("DRGN", 1, drgn)])])
        path = write_fixture("truncated_record.skse", data)
        code, out = run_tool(path)
        self.assertEqual(code, 1, out)
        self.assertIn("WARNING: record SIM2 v208 truncated", out)
        self.assertIn("dragonKills = 2", out)                   # the next record still decodes
        self.assertNotIn("Traceback", out)

    def test_truncated_file(self):
        full = cd.build_cosave([(cd.ALIFE_UID, [chunk("DRGN", 1, pack_drgn(1, sample_drgn(1))),
                                                chunk("DIFF", 1, pack_diff(1, sample_diff(1)))])])
        for cut in range(len(full) - 1, cd.HEADER.size, -1):   # every cut point inside the plugin data
            data = full[:cut]
            try:
                cd.decode_file(data)
            except Exception as e:  # noqa: BLE001 - any exception is the failure this test looks for
                self.fail("cut at %d: %r" % (cut, e))
        path = write_fixture("truncated_file.skse", full[:-5])
        code, out = run_tool(path)
        self.assertEqual(code, 1, out)
        self.assertIn("truncated", out)
        self.assertNotIn("Traceback", out)

    def test_unknown_type_and_version(self):
        data = cd.build_cosave([(cd.ALIFE_UID, [chunk("ZZZZ", 1, b"\xff" * 7),
                                                chunk("SIM2", 204, b"\x00" * 40),
                                                chunk("DIFF", 2, b"\x00\x01\x00\x00\x80\x3f"),
                                                chunk("DRGN", 1, pack_drgn(1, sample_drgn(1)))])])
        path = write_fixture("unknown_type.skse", data)
        code, out = run_tool(path)
        self.assertEqual(code, 1, out)
        self.assertIn("WARNING: unknown record type ZZZZ (v1, 7 bytes): skipped", out)
        self.assertIn("WARNING: record SIM2 v204 is not a version this decoder knows", out)
        self.assertIn("WARNING: record DIFF v2 is not a version this decoder knows", out)
        self.assertIn("dragonKills = 2", out)
        self.assertNotIn("Traceback", out)

    def test_garbage_record_bodies_never_crash(self):
        import random
        rnd = random.Random(6)
        for code, (versions, _, _) in CASES.items():
            for ver in versions:
                for _ in range(200):
                    blob = bytes(rnd.randrange(256) for _ in range(rnd.randrange(0, 64)))
                    try:
                        cd.decode_record(code, ver, blob)
                    except Exception as e:  # noqa: BLE001
                        self.fail("%s v%d on %r: %r" % (code, ver, blob, e))

    def test_not_a_cosave(self):
        path = write_fixture("not_a_cosave.skse", b"TESV_SAVEGAME" + b"\x00" * 20)
        code, out = run_tool(path)
        self.assertEqual(code, 2, out)
        self.assertIn("not an SKSE co-save", out)


class SourceAgreement(unittest.TestCase):
    def test_record_codes_in_save_cpp_are_documented(self):
        with open(SAVE_CPP, encoding="utf-8") as f:
            src = f.read()
        codes = sorted(set(re.findall(r"'([A-Z0-9]{4})'", src)))
        doc = doc_text()
        missing = [c for c in codes if c not in doc]
        print("\n  record type constants in src/Save.cpp: %d (%s); in docs/COSAVE.md: %d; missing: %s"
              % (len(codes), " ".join(codes), len(codes) - len(missing), missing or "none"))
        self.assertEqual(missing, [])
        self.assertGreaterEqual(len(codes), 9)

    def test_every_written_record_is_decoded(self):
        """Every kRec / WriteRecord type anywhere in src/ is one the decoder (and so the doc) knows."""
        found = set()
        for dirpath, _, files in os.walk(os.path.join(ROOT, "src")):
            for fn in files:
                if fn.endswith(".cpp"):
                    with open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace") as f:
                        found |= set(re.findall(r"Rec\w*\s*=\s*'([A-Z0-9]{4})'", f.read()))
        self.assertEqual(found, set(cd.RECORDS), found ^ set(cd.RECORDS))

    def test_current_versions_match_source(self):
        def const(path, pattern):
            with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                return int(re.search(pattern, f.read()).group(1))
        want = {"SIM2": const("include/ALife/SaveFormat.h", r"kVer = (\d+);"),
                "TASK": const("include/ALife/Tasks.h", r"kSaveVersion = (\d+);"),
                "COMP": const("include/ALife/Party.h", r"kSaveVersion = (\d+);"),
                "WOND": const("include/ALife/Wounds.h", r"kSaveVersion = (\d+);"),
                "STSH": const("include/ALife/Stashes.h", r"kSaveVersion = (\d+);"),
                "NEWS": const("src/Save.cpp", r"kNewsVer = (\d+);"),
                "DRGN": const("src/Save.cpp", r"kDragonVer = (\d+);"),
                "DIFF": const("src/Save.cpp", r"kDiffVer = (\d+);")}
        self.assertEqual(want, cd.CURRENT)
        self.assertEqual(const("src/Jobs.cpp", r"kMaxSlots = (\d+);") * 2, cd.JOB_MARKERS)
        self.assertEqual(const("src/Stashes.cpp", r"kMaxSlots = (\d+);"), cd.STASH_MARKERS)

    def test_doc_citations_exist(self):
        """Every `path:line` cited in docs/COSAVE.md (a bare `:line` uses the last path named before it) is in CITATIONS,
        and the expected token is on that line, so line drift fails here. Ranges must also end inside the file."""
        cur, seen = None, set()
        for m in re.finditer(r"`((?:src|include|tools)/[\w/.]+\.(?:cpp|h|py))?:(\d+)(?:-(\d+))?`", doc_text()):
            cur = m.group(1) or cur
            self.assertIsNotNone(cur, m.group(0))
            key = "%s:%s" % (cur, m.group(2))
            with self.subTest(cite=key):
                with open(os.path.join(ROOT, cur), encoding="utf-8", errors="replace") as f:
                    lines = f.read().split("\n")
                self.assertLessEqual(int(m.group(3) or m.group(2)), len(lines), "cites past the end")
                self.assertTrue(key in CITATIONS, "new citation: add it with an expected token to CITATIONS")
                self.assertIn(CITATIONS[key], lines[int(m.group(2)) - 1], "line drift: the cited line lost its token")
            seen.add(key)
        self.assertEqual(sorted(set(CITATIONS) - seen), [], "CITATIONS entries no longer cited in docs/COSAVE.md")
        print("\n  docs/COSAVE.md citations checked against their expected token: %d" % len(seen))


# "path:line token": the token each line cited in docs/COSAVE.md must still carry (line numbers as of master d0fc955)
CITATIONS = dict(l.split(" ", 1) for l in """
src/Save.cpp:17 kUID = 'ALIF'
src/Save.cpp:359 SetUniqueID(kUID)
src/Save.cpp:91 BoardReady()) return
src/Save.cpp:32 void v(const T& x)
src/core/Tasks.cpp:106 struct Bytes
src/core/Party.cpp:231 struct Bytes
src/core/Wounds.cpp:569 struct Bytes
src/core/Stashes.cpp:375 struct Bytes
include/ALife/Core.h:23 enum class Faction : std::uint8_t
include/ALife/Core.h:42 enum class Tier : std::uint8_t
include/ALife/Core.h:189 enum class Action : std::uint8_t
include/ALife/Tasks.h:26 enum class TaskType : std::uint8_t
include/ALife/Party.h:26 enum class Order : std::uint8_t
src/Save.cpp:58 RE::FormID f()
src/Save.cpp:33 void s(const std::string& x)
src/Save.cpp:45 ok = false
src/core/Tasks.cpp:120 ok = false
src/core/Party.cpp:245 ok = false
src/core/Wounds.cpp:582 ok = false
src/core/Stashes.cpp:388 ok = false
include/ALife/Core.h:36 kNoFaction = 255
include/ALife/SaveFormat.h:25 FactionIndexOf(
src/Save.cpp:88 void SaveCb(
src/Save.cpp:155 WriteRecord(kRec, kVer
src/Save.cpp:244 SaveFormat::Loads(ver)
src/Jobs.cpp:498 WriteRecord(kRec, Tasks::kSaveVersion
src/Jobs.cpp:501 void LoadFrom(
src/Companions.cpp:530 WriteRecord(kRec, Party::kSaveVersion
src/Companions.cpp:534 void LoadFrom(
src/Wounded.cpp:549 WriteRecord(kRec, Wounds::kSaveVersion
src/Wounded.cpp:553 void LoadFrom(
src/Stashes.cpp:296 WriteRecord(kRec, Stashes::kSaveVersion
src/Stashes.cpp:300 void LoadFrom(
src/Save.cpp:172 WriteRecord(kNewsRec, kNewsVer
src/Save.cpp:229 type == kNewsRec
src/Save.cpp:179 WriteRecord(kDragonRec, kDragonVer
src/Save.cpp:217 type == kDragonRec
src/Save.cpp:187 WriteRecord(kDiffRec, kDiffVer
src/Save.cpp:199 type == kDiffRec
src/Save.cpp:191 void LoadCb(
src/Save.cpp:159 Jobs::SaveTo(a)
src/Save.cpp:339 if (!sawDragons)
src/Save.cpp:343 if (!diffLoaded)
src/Save.cpp:96 w.v(b.now)
include/ALife/SaveFormat.h:10 kVer = 208
src/Save.cpp:255 b.now = r.v<double>()
src/Save.cpp:99 w.v(std::uint8_t(kFactionCount))
include/ALife/SaveFormat.h:39 const int nf
src/Save.cpp:100 w.v(st.goodwill[f])
include/ALife/SaveFormat.h:40 f < nf
src/Save.cpp:101 w.v(st.notoriety)
include/ALife/SaveFormat.h:44 st.notoriety
src/Save.cpp:102 st.rumors.size()
include/ALife/SaveFormat.h:45 r.template v<std::uint32_t>()
src/Save.cpp:105 P.waitlist.size()
src/Save.cpp:257 P.waitlist.push_back
src/Save.cpp:107 P.queuedKills.size()
src/Save.cpp:258 P.queuedKills[c]
src/Save.cpp:111 w.v(std::uint32_t(b.smart.size()))
src/Save.cpp:260 auto ns
src/Save.cpp:123 Merchants::Snapshot()
src/Save.cpp:280 std::vector<Merchants::State> ms
src/Save.cpp:127 auto& gs = b.squads
src/Save.cpp:290 b.squads.clear()
src/Save.cpp:154 Creatures::Refs()
src/Save.cpp:326 ver >= kVerMutants
src/Save.cpp:13 Co-save: every piece of simulated state
src/Save.cpp:265 Graph::ByLocation(loc)
src/Save.cpp:130 w.v(g.id)
src/Save.cpp:292 Group g;
src/Save.cpp:75 void WNode(
src/Save.cpp:142 w.v(m.id)
src/Save.cpp:306 Member m;
include/ALife/Core.h:84 struct Member
include/ALife/Core.h:61 enum Skill
include/ALife/SaveFormat.h:18 bool Loads(
include/ALife/Core.h:30 Dragon,
src/Save.cpp:250 SaveFormat::FactionOf(ver, f)
include/ALife/SaveFormat.h:42 FactionIndexOf(a_ver, f)
include/ALife/SaveFormat.h:47 ru.faction = FactionOf
src/Save.cpp:27 204: a fifteenth faction
include/ALife/SaveFormat.h:17 208, 207, 206 and 205 load
src/Save.cpp:331 if (!r.ok) {
src/core/Tasks.cpp:1201 void Tasks::Save(
src/core/Tasks.cpp:1213 bool Tasks::Load(
src/core/Tasks.cpp:135 void SaveTask(
src/core/Tasks.cpp:150 Task LoadTask(
src/Jobs.cpp:493 void SaveTo(
include/ALife/Tasks.h:261 kSaveVersion = 3
src/Jobs.cpp:19 kMaxSlots = 4
src/Jobs.cpp:30 2 * kMaxSlots> gMarker
src/core/Tasks.cpp:143 if (a_version < 3) return;
include/ALife/Tasks.h:129 struct Task
src/core/Tasks.cpp:160 t.dest = r.v<std::uint32_t>()
include/ALife/Tasks.h:151 TASK v3 (M5e)
src/core/Tasks.cpp:171 if (a_version == 1)
src/core/Tasks.cpp:1219 k <= 16
src/core/Tasks.cpp:128 len > 4096
src/core/Tasks.cpp:156 n < 64
src/core/Tasks.cpp:162 if (n > 64) r.ok = false
src/core/Tasks.cpp:172 int(t.type) >= kTaskTypes
src/Jobs.cpp:507 k < gMarker.size()
src/Jobs.cpp:514 auto fix = [&](Task& t)
src/core/Party.cpp:261 void Party::Save(
src/core/Party.cpp:274 bool Party::Load(
src/Companions.cpp:526 void SaveTo(
include/ALife/Party.h:123 kSaveVersion = 1
src/core/Party.cpp:277 version != kSaveVersion
src/core/Party.cpp:264 list.size()
src/core/Party.cpp:270 rolls.size()
src/core/Party.cpp:279 k <= 16
src/core/Party.cpp:253 k > 4096
src/Companions.cpp:539 for (auto& c : gP.list)
src/core/Wounds.cpp:590 void Wounds::Save(
src/core/Wounds.cpp:601 bool Wounds::Load(
src/Wounded.cpp:545 void SaveTo(
include/ALife/Wounds.h:197 kSaveVersion = 1
src/core/Wounds.cpp:604 version != kSaveVersion
include/ALife/Wounds.h:78 kYielded = 1, kOffline = 2, kCalled = 4
src/core/Wounds.cpp:594 : down)
src/core/Wounds.cpp:597 fights.size()
src/core/Wounds.cpp:606 k <= 100000
src/core/Stashes.cpp:396 void Stashes::Save(
src/core/Stashes.cpp:410 bool Stashes::Load(
src/Stashes.cpp:18 kMaxSlots = 32
src/Stashes.cpp:24 kMaxSlots> gMarker
src/Stashes.cpp:295 for (auto id : gMarker)
include/ALife/Stashes.h:142 kSaveVersion = 1
src/core/Stashes.cpp:414 version != kSaveVersion
src/core/Stashes.cpp:399 spots.size()
src/core/Stashes.cpp:406 w.v(found)
src/Stashes.cpp:304 const std::size_t tail
src/core/Stashes.cpp:417 i < 100000
src/Stashes.cpp:305 len < tail
src/Save.cpp:163 record 'NEWS'
src/Save.cpp:22 kNewsVer = 1
src/Save.cpp:169 n.v(std::uint32_t(kFactionCount))
include/ALife/SaveFormat.h:53 live relation matrix of the NEWS record
src/Save.cpp:240 SaveFormat::RestoreRelations
src/Save.cpp:175 record 'DRGN'
src/Save.cpp:24 kDragonVer = 1
include/ALife/Board.h:134 double dragonLast
src/Save.cpp:181 record 'DIFF' v1
src/Save.cpp:26 kDiffVer = 1
include/ALife/Difficulty.h:12 enum class DiffChannel
src/Save.cpp:206 k < n; ++k
src/Jobs.cpp:17 kRec = 'TASK'
src/Companions.cpp:16 kRec = 'COMP'
src/Wounded.cpp:17 kRec = 'WOND'
src/Stashes.cpp:15 kRec = 'STSH'
""".strip().split("\n"))

if __name__ == "__main__":
    unittest.main()
