#!/usr/bin/env python3
"""Record-by-record comparison of two ALife.esp builds (for example a fresh build_esp.py output against a released one).

Usage: compare_esp.py <reference.esp> <candidate.esp>
Works for ALife.esp, ALifeFactions.esp and ALifeTalk.esp. Checks: TES4 header flags (ESL), masters and their order, the other
TES4 header subrecords (HEDR count and next object ID; the author, CNAM, is reported but allowed to differ), record counts by
type, the FormID set, and per FormID the record type, EDID, flags, group placement and record data. Lists the NPC_ FormIDs
(FaceGen files are named after them) and reports any shift. Ends with byte-identical yes/no.
Own records are keyed by local ID (shown as FFxxxxxx), so a plugin that gained a master still compares record for record; an
EditorID that moved to another ID, or vanished, is a mismatch (saves and FaceGen files refer to the IDs).
Exit status 0 when every record and every header field but CNAM matches."""
import collections, hashlib, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fastscan

def load(path):
    d = open(path, "rb").read()
    assert d[:4] == b"TES4", path
    hsz, hflags = struct.unpack_from("<II", d, 4)
    head = fastscan.subs(d[24:24 + hsz])
    own = sum(1 for t, _ in head if t == "MAST")   # own records carry the master count as their high byte
    recs, groups = collections.OrderedDict(), []
    def walk(o, end, gpath):
        while o < end:
            size, = struct.unpack_from("<I", d, o + 4)
            if d[o:o + 4] == b"GRUP":
                key = (struct.unpack_from("<i", d, o + 12)[0], d[o + 8:o + 12].hex())
                groups.append(gpath + (key,))
                walk(o + 24, o + size, gpath + (key,)); o += size
            else:
                typ = d[o:o + 4].decode("latin1"); flags, fid = struct.unpack_from("<II", d, o + 8)
                if fid >> 24 == own: fid = 0xFF000000 | (fid & 0xFFFFFF)   # FFxxxxxx = this plugin's own record, local ID xxxxxx
                data = d[o + 24:o + 24 + size]
                if fid in recs: raise SystemExit("%s: duplicate FormID %08X" % (path, fid))
                recs[fid] = dict(typ=typ, flags=flags, hdr=d[o + 16:o + 24], data=data, path=gpath,
                                 edid=fastscan.edid(fastscan.subs(data)))
                o += 24 + size
    walk(24 + hsz, len(d), ())
    return dict(raw=d, flags=hflags, head=head, recs=recs, groups=groups,
                masters=[v.rstrip(b"\0").decode() for t, v in head if t == "MAST"])

def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    bad = []
    print("reference %s  md5 %s  %d bytes" % (sys.argv[1], hashlib.md5(a["raw"]).hexdigest(), len(a["raw"])))
    print("candidate %s  md5 %s  %d bytes" % (sys.argv[2], hashlib.md5(b["raw"]).hexdigest(), len(b["raw"])))
    for x, n in ((a, "reference"), (b, "candidate")):
        print("%-9s ESL=%s masters=%s records=%d groups=%d" % (n, bool(x["flags"] & 0x200), x["masters"], len(x["recs"]), len(x["groups"])))
    if (a["flags"] & 0x200) != (b["flags"] & 0x200): bad.append("ESL flag differs")
    if a["masters"] != b["masters"]: bad.append("masters differ")
    if a["groups"] != b["groups"]: bad.append("group layout differs")
    ca = collections.Counter(r["typ"] for r in a["recs"].values()); cb = collections.Counter(r["typ"] for r in b["recs"].values())
    print("type      ref  cand")
    for t in sorted(set(ca) | set(cb)):
        print("%-8s %4d  %4d%s" % (t, ca[t], cb[t], "" if ca[t] == cb[t] else "  MISMATCH"))
    print("total    %4d  %4d" % (sum(ca.values()), sum(cb.values())))
    if ca != cb: bad.append("record counts by type differ")
    fa, fb = set(a["recs"]), set(b["recs"])
    for f in sorted(fa - fb): bad.append("FormID %08X only in reference" % f)
    for f in sorted(fb - fa): bad.append("FormID %08X only in candidate" % f)
    if list(a["recs"]) != list(b["recs"]) and fa == fb: bad.append("record order differs")
    fields = collections.Counter()
    for f in sorted(fa & fb):
        ra, rb = a["recs"][f], b["recs"][f]
        for k in ("typ", "edid", "flags", "path", "hdr", "data"):
            if ra[k] != rb[k]:
                fields[k] += 1
                if fields[k] <= 5: bad.append("%08X %s %s differs" % (f, ra["typ"], k))
    npc_a = sorted(f for f, r in a["recs"].items() if r["typ"] == "NPC_")
    npc_b = sorted(f for f, r in b["recs"].items() if r["typ"] == "NPC_")
    span = lambda l: "%d (%08X..%08X)" % (len(l), l[0], l[-1]) if l else "0"   # the factions and talk plugins have no NPC_
    print("NPC_ FormIDs: ref %s  cand %s  shifted/missing: %d" % (span(npc_a), span(npc_b), len(set(npc_a) ^ set(npc_b))))
    if npc_a != npc_b: bad.append("NPC_ FormIDs differ (FaceGen file names would not match)")
    ea = {r["edid"]: f for f, r in a["recs"].items() if r["edid"]}; eb = {r["edid"]: f for f, r in b["recs"].items() if r["edid"]}
    moved = sorted(e for e in set(ea) & set(eb) if ea[e] != eb[e])
    print("EDIDs: %d in both (%d at the same ID, %d moved), %d only in reference, %d only in candidate" % (
        len(set(ea) & set(eb)), len(set(ea) & set(eb)) - len(moved), len(moved), len(set(ea) - set(eb)), len(set(eb) - set(ea))))
    for e in moved: bad.append("EDID %s moved %08X -> %08X" % (e, ea[e], eb[e]))
    for e in sorted(set(ea) - set(eb)): bad.append("EDID %s (%08X) only in reference" % (e, ea[e]))
    same_edid = sum(1 for f in fa & fb if a["recs"][f]["edid"] == b["recs"][f]["edid"])
    same_data = sum(1 for f in fa & fb if a["recs"][f]["data"] == b["recs"][f]["data"])
    print("records with same type+EDID+flags+placement+data: %d/%d (EDID equal %d, data equal %d)" % (
        sum(1 for f in fa & fb if all(a["recs"][f][k] == b["recs"][f][k] for k in ("typ", "edid", "flags", "path", "hdr", "data"))),
        len(fa | fb), same_edid, same_data))
    ha, hb = a["head"], b["head"]
    hdiff = [t for t in sorted({t for t, _ in ha} | {t for t, _ in hb})
             if [v for k, v in ha if k == t] != [v for k, v in hb if k == t]]
    for t in hdiff:
        if t not in ("CNAM", "MAST", "DATA"):   # the author may differ; masters are checked above (DATA = each master's size, 0)
            bad.append("TES4 %s differs: %r -> %r" % (t, [v for k, v in ha if k == t], [v for k, v in hb if k == t]))
    ident = a["raw"] == b["raw"]
    print("byte-identical:", "yes" if ident else "no")
    if not ident:
        print("TES4 header subrecords that differ:", ", ".join("%s (%r -> %r)" % (t, [v for k, v in ha if k == t], [v for k, v in hb if k == t]) for t in hdiff) or "none")
        if not bad and len(a["raw"]) != len(b["raw"]):
            print("file size differs by %+d bytes, all in the TES4 author (CNAM); every record and group is identical" % (len(b["raw"]) - len(a["raw"])))
    for e in bad: print("MISMATCH", e)
    print("RESULT", "MATCH" if not bad else "FAIL", "record for record" if not bad else "(%d problems)" % len(bad))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
