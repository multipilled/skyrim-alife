"""Checks a plugin's header: every record the plugin defines itself must have a local ID below the next object ID in its
TES4 HEDR, or the Creation Kit hands out IDs that are already taken. Reads the value from the header; nothing is hard-coded.

usage: python check_next_id.py plugin.esp [...]      exit 1 when a plugin fails"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import esp


def check(buf):
    """(ok, next_id, max_own_local, offenders) for a plugin's bytes."""
    recs = esp.walk(buf)
    tes4 = next(recs)
    assert tes4.sig == "TES4", "not a plugin"
    subs = tes4.subs()
    next_id = next(struct.unpack_from("<fII", d)[2] for s, d in subs if s == "HEDR")
    own = sum(1 for s, _ in subs if s == "MAST")   # records of this plugin carry its master count as the high byte
    locals_ = [r.fid & 0xFFFFFF for r in recs if (r.fid >> 24) == own]
    bad = sorted(l for l in locals_ if l >= next_id)
    return not bad, next_id, max(locals_, default=0), bad


def main(paths):
    failed = False
    for p in paths:
        ok, nid, top, bad = check(open(p, "rb").read())
        if ok:
            print(f"{p}: OK  next object ID 0x{nid:X}, highest own local ID 0x{top:X}")
        else:
            failed = True
            print(f"{p}: FAIL next object ID 0x{nid:X}, {len(bad)} own records at or above it (0x{bad[0]:X}..0x{bad[-1]:X})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) if len(sys.argv) > 1 else (print(__doc__), 2)[1])
