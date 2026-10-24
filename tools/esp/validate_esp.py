#!/usr/bin/env python3
"""Structural + reference validator for the A-Life plugins (ALife.esp, ALifeFactions.esp, ALifeTalk.esp; independent of the writers).

usage: python validate_esp.py <plugin.esp> <masters dir or file> [<masters dir or file> ...] [--manifest esp-manifest.json]

--manifest (ALife.esp): the plugin must match the manifest build_esp.py writes (tools/esp/esp-manifest.json is the committed one):
records = the HEDR record count, nextObjectId = the HEDR next object ID, and every pool entry's NPC_ / ACHR FormIDs, EditorID,
race and sex as in the plugin (NPC_ count = ACHR count = manifest entries).

Every master in the plugin's TES4 header (MAST) is looked up by name in the given folders (a file argument means its folder),
then in the plugin's own folder (a generated master such as ALifeFactions.esp sits next to ALifeTalk.esp). References are checked
against the master the FormID's high byte names. A master that cannot be found is skipped with a message, and the result is then
INCOMPLETE (exit 2), never PASS. No game file is read from anywhere but the paths given."""
import struct, sys, os, collections, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fastscan

def walk(d, o, end, depth, out, errs, path):
    while o < end:
        if d[o:o+4] == b"GRUP":
            size, = struct.unpack_from("<I", d, o + 4)
            lab = d[o+8:o+12]; gtype, = struct.unpack_from("<i", d, o + 12)
            if size < 24 or o + size > end: errs.append("bad GRUP size at %x" % o); return
            out.append(("GRUP", gtype, lab, depth, path))
            walk(d, o + 24, o + size, depth + 1, out, errs, path + [(gtype, lab)])
            o += size
        else:
            typ = d[o:o+4].decode("latin1"); size, flags, fid = struct.unpack_from("<III", d, o + 4)
            ver, = struct.unpack_from("<H", d, o + 20)
            data = d[o+24:o+24+size]
            if o + 24 + size > end: errs.append("record overruns group at %x" % o); return
            subs = fastscan.subs(data)
            if sum(6 + len(v) for t, v in subs) != size and not any(t == "XXXX" for t, v in subs):
                errs.append("%s %08X subrecord sizes do not add up" % (typ, fid))
            out.append((typ, fid, flags, subs, path, ver))
            o += 24 + size

def master_list(path):
    d = fastscan.load(path); hsz, = struct.unpack_from("<I", d, 4)
    return [v.rstrip(b"\0").decode("latin1") for t, v in fastscan.subs(d[24:24+hsz]) if t == "MAST"]

def main():
    argv = list(sys.argv[1:])
    manifest = None
    if "--manifest" in argv:
        k = argv.index("--manifest"); manifest = argv[k + 1]; del argv[k:k + 2]
    if len(argv) < 2: print(__doc__); sys.exit(2)
    esp, mdirs = argv[0], argv[1:]
    d = open(esp, "rb").read()
    errs, items = [], []
    stats = collections.Counter()
    assert d[:4] == b"TES4"
    hsz, hflags = struct.unpack_from("<II", d, 4)
    head = fastscan.subs(d[24:24+hsz])
    hedr = dict(head)["HEDR"]
    ver, num, nxt = struct.unpack("<fII", hedr)
    masters = [v.rstrip(b"\0").decode() for t, v in head if t == "MAST"]
    walk(d, 24 + hsz, len(d), 0, items, errs, [])
    groups = [i for i in items if i[0] == "GRUP"]
    recs = [i for i in items if i[0] != "GRUP"]
    if num != len(items): errs.append("HEDR numRecords %d != %d" % (num, len(items)))
    own = len(masters) << 24
    ownids = {r[1] for r in recs}
    maxobj = max(r[1] & 0xFFFFFF for r in recs)
    if nxt <= maxobj: errs.append("nextObjectId %X <= max %X" % (nxt, maxobj))
    if not (hflags & 0x200): errs.append("ESL flag missing")
    if any((r[1] & 0xFFFFFF) > 0xFFF for r in recs): errs.append("FormID outside ESL range")
    if any((r[1] >> 24) != len(masters) for r in recs): errs.append("own record with wrong master index")
    # fixed subrecord sizes
    want = {("NPC_", "ACBS"): 24, ("NPC_", "AIDT"): 20, ("NPC_", "DNAM"): 52, ("NPC_", "OBND"): 12, ("NPC_", "NAM9"): 76,
            ("QUST", "DNAM"): 12, ("PACK", "PSDT"): 12, ("PACK", "PKDT"): 12, ("PACK", "PKCU"): 12, ("PACK", "CTDA"): 32,
            ("PACK", "PLDT"): 12, ("PACK", "PTDA"): 12, ("ACHR", "DATA"): 24, ("REFR", "DATA"): 24, ("CELL", "DATA"): 2,
            ("FACT", "XNAM"): 12, ("LVLI", "LVLO"): 12}
    for r in recs:
        if r[5] != 44: errs.append("%s %08X form version %d" % (r[0], r[1], r[5]))
        for t, v in r[3]:
            w = want.get((r[0], t))
            if w is not None and len(v) != w: errs.append("%s %08X %s size %d != %d" % (r[0], r[1], t, len(v), w))
    # placement: every ACHR/REFR must sit in CELL > block(2) > sub-block(3) > children(6) > persistent(8)
    for r in recs:
        if r[0] in ("ACHR", "REFR"):
            types = [g[0] for g in r[4]]
            if types != [0, 2, 3, 6, 8]: errs.append("%s %08X placed under %s" % (r[0], r[1], types))
            if not (r[2] & 0x400): errs.append("%s %08X not persistent" % (r[0], r[1]))
    # references into the masters must exist and have the right type, each looked up in the master its high byte names
    dirs = [a if os.path.isdir(a) else os.path.dirname(os.path.abspath(a)) for a in mdirs] + [os.path.dirname(os.path.abspath(esp))]
    types = {"RACE", "VTYP", "FACT", "KYWD", "CLAS", "HDPT", "CLFM", "TXST", "PERK", "SPEL", "ARMO", "WEAP", "SLGM", "LVLI", "MISC",
             "STAT", "PACK", "QUST", "GLOB", "DIAL", "DLBR", "ALCH", "INGR", "BOOK", "KEYM", "AMMO", "NPC_"}
    mtype, skipped, skipped_refs = {}, [], collections.Counter()
    for i, m in enumerate(masters):
        path = next((os.path.join(dd, m) for dd in dirs if os.path.isfile(os.path.join(dd, m))), None)
        if path is None:
            skipped.append(m); print("SKIP master %s: not found in %s; references into it are not checked" % (m, dirs)); continue
        mm = master_list(path)   # a master's own records carry its master count as the high byte
        mtype[i] = {fid & 0xFFFFFF: t for t, lst in fastscan.scan(path, types).items() for fid, fl, ss in lst if (fid >> 24) == len(mm)}
    ownt = {r[1]: r[0] for r in recs}
    def chk(rec, sub, fid, allowed):
        hi = fid >> 24
        if hi == len(masters): t = ownt.get(fid)
        elif hi > len(masters): t = None
        elif hi not in mtype: skipped_refs[masters[hi]] += 1; return
        else: t = mtype[hi].get(fid & 0xFFFFFF)
        if t not in allowed: errs.append("%s %08X %s -> %08X is %s, want %s" % (rec[0], rec[1], sub, fid, t, allowed))
        else: stats["ref->" + ("(own)" if hi == len(masters) else masters[hi])] += 1
    for r in recs:
        for t, v in r[3]:
            if r[0] == "NPC_":
                if t in ("RNAM", "ATKR"): chk(r, t, struct.unpack("<I", v)[0], {"RACE"})
                elif t == "VTCK": chk(r, t, struct.unpack("<I", v)[0], {"VTYP"})
                elif t == "PNAM": chk(r, t, struct.unpack("<I", v)[0], {"HDPT"})
                elif t == "HCLF": chk(r, t, struct.unpack("<I", v)[0], {"CLFM"})
                elif t == "FTST": chk(r, t, struct.unpack("<I", v)[0], {"TXST"})
                elif t == "CNAM": chk(r, t, struct.unpack("<I", v)[0], {"CLAS"})
                elif t == "SNAM": chk(r, t, struct.unpack("<I", v[:4])[0], {"FACT"})
                elif t == "PKID": chk(r, t, struct.unpack("<I", v)[0], {"PACK"})
                elif t == "KWDA":
                    for k in range(0, len(v), 4): chk(r, t, struct.unpack_from("<I", v, k)[0], {"KYWD"})
            elif r[0] == "ACHR" and t == "NAME": chk(r, t, struct.unpack("<I", v)[0], {"NPC_"}); stats["ACHR"] += 1
            elif r[0] == "REFR" and t == "NAME": chk(r, t, struct.unpack("<I", v)[0], {"STAT"})
            elif r[0] == "FLST" and t == "LNAM": chk(r, t, struct.unpack("<I", v)[0], {"PERK", "SPEL", "ARMO", "WEAP", "SLGM", "MISC"})
            elif r[0] == "LVLI" and t == "LVLO": chk(r, t, struct.unpack("<HHIHH", v)[2], {"LVLI", "MISC", "ARMO", "WEAP"})
            elif r[0] == "FACT" and t == "XNAM": chk(r, t, struct.unpack("<IiI", v)[0], {"FACT"})
            elif r[0] == "PACK" and t == "PKCU": chk(r, t, struct.unpack("<III", v)[1], {"PACK"})
            elif r[0] == "PACK" and t in ("PLDT", "PTDA"):
                ty, u, rad = struct.unpack("<iIi", v)
                if (r[0], t, ty) in (("PACK", "PLDT", 6), ("PACK", "PTDA", 3)): chk(r, t, u, {"KYWD"})
            elif r[0] in ("DIAL", "DLBR") and t == "QNAM": chk(r, t, struct.unpack("<I", v)[0], {"QUST"})
            elif r[0] == "DIAL" and t == "BNAM": chk(r, t, struct.unpack("<I", v)[0], {"DLBR"})
            elif r[0] == "DLBR" and t == "SNAM": chk(r, t, struct.unpack("<I", v)[0], {"DIAL"})
            elif r[0] == "INFO" and t == "TCLT": chk(r, t, struct.unpack("<I", v)[0], {"DIAL"})
            elif r[0] == "INFO" and t == "CTDA":
                op, = struct.unpack_from("<B", v, 0); fn, = struct.unpack_from("<H", v, 8); p1, = struct.unpack_from("<I", v, 12)
                if op & 0x04: chk(r, t + ".global", struct.unpack_from("<I", v, 4)[0], {"GLOB"})   # compared against a global
                if fn == 71: chk(r, t, p1, {"FACT"})        # GetInFaction
                elif fn == 74: chk(r, t, p1, {"GLOB"})      # GetGlobalValue
                elif fn == 47: chk(r, t, p1, {"MISC", "ALCH", "INGR", "WEAP", "ARMO", "BOOK", "KEYM", "SLGM", "AMMO"})   # GetItemCount
            elif r[0] == "PACK" and t == "CTDA":
                fn, = struct.unpack_from("<H", v, 8); p1, = struct.unpack_from("<I", v, 12)
                if fn == 73: chk(r, t, p1, {"FACT"})
    if manifest:   # the committed / freshly written manifest must describe this very plugin
        man = json.load(open(manifest))
        if man.get("records") != num: errs.append("manifest records %s != HEDR numRecords %d" % (man.get("records"), num))
        if int(str(man.get("nextObjectId", "0")), 16) != nxt: errs.append("manifest nextObjectId %s != HEDR %X" % (man.get("nextObjectId"), nxt))
        npc = {r[1] & 0xFFFFFF: r for r in recs if r[0] == "NPC_"}
        achr = {r[1] & 0xFFFFFF: r for r in recs if r[0] == "ACHR"}
        pool = man.get("pool", [])
        if not (len(pool) == len(npc) == len(achr)): errs.append("manifest pool %d / NPC_ %d / ACHR %d" % (len(pool), len(npc), len(achr)))
        for e in pool:
            n, a = npc.get(int(e["local"], 16)), achr.get(int(e["ref"], 16))
            if not n or not a: errs.append("manifest %s: NPC_ %s / ACHR %s missing" % (e["edid"], e["local"], e["ref"])); continue
            sd = dict(n[3])
            ed = sd.get("EDID", b"").rstrip(b"\0").decode("latin1")
            race = struct.unpack("<I", sd["RNAM"])[0] & 0xFFFFFF if "RNAM" in sd else None
            sex = "F" if "ACBS" in sd and struct.unpack_from("<I", sd["ACBS"])[0] & 1 else "M"
            base = struct.unpack("<I", dict(a[3]).get("NAME", bytes(4)))[0] & 0xFFFFFF
            if ed != e["edid"] or race != int(e["race"], 16) or sex != e["sex"] or base != int(e["local"], 16):
                errs.append("manifest %s differs: plugin EDID %s race %s sex %s ACHR base %X" % (e["edid"], ed, race and hex(race), sex, base))
        stats["manifest pool"] = len(pool)
    c = collections.Counter(r[0] for r in recs)
    print("masters", masters, "HEDR", round(ver, 2), num, hex(nxt), "groups", len(groups), "records", dict(c))
    print("references checked", dict(sorted(stats.items())))
    for e in errs[:50]: print("ERROR", e)
    for m in skipped: print("SKIPPED master %s: %d references into it NOT checked" % (m, skipped_refs[m]))
    if errs: print("RESULT", "FAIL", len(errs), "errors"); sys.exit(1)
    if skipped: print("RESULT", "INCOMPLETE", "0 errors in what was checked; masters not found:", ", ".join(skipped)); sys.exit(2)
    print("RESULT", "PASS", 0, "errors"); sys.exit(0)

if __name__ == "__main__":
    main()
