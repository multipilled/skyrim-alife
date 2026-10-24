#!/usr/bin/env python3
"""Compare two harness JSON reports (`harness --json <path>`, ticket G8). Python 3 standard library only.

    python3 tools/compare_harness.py a.json b.json                  -> "identical", exit 0
                                                                       or one line per differing field, exit 1
    python3 tools/compare_harness.py a.json b.json --tol 'runs.*.news.lines=5%' --tol 'runs.*.squads.**=2'
    python3 tools/compare_harness.py a.json b.json --include-cpu --tol '**.cpu.**=50%'

Every leaf value (number, string, bool, null) of the two reports is compared by its path. A path is the list of keys and
list indices from the top, written with dots: `runs.0.squads.by_faction.bandit.squads`. Keys that are not plain words
are shown quoted: runs.0.checks["m5d: bounded"] (checks live at the top: checks["m5d: bounded"].failed).

Tolerances (per field, the last matching rule wins; built-in rules first, then --tol-file, then --tol in order):
  PATTERN=SPEC    PATTERN: dot-separated segments; each is a shell glob for one key or index (fnmatch: * ? [..]),
                  and ** stands for any number of segments. SPEC: 0 (exact, the default), N (|a - b| <= N),
                  N% (|a - b| <= N percent of max(|a|, |b|)), or ignore. Strings, bools and nulls compare exactly.
  --tol-file F    a JSON object {"PATTERN": "SPEC", ...}
Built in: every field under a `cpu` object (CPU / wall time per step, total wall ms) is ignored unless --include-cpu.

Output: "identical" when every compared field is equal. "equivalent: N field(s) differ within tolerance" (exit 0) when
some differ but all within their tolerance (listed with --verbose). Otherwise "differs: N field(s)" and one line each:
    <path>: <a> -> <b>  (delta <d>, tolerance <spec>)       or     <path>: only in a / only in b
exit 1. Bad arguments or unreadable files: exit 2.
"""
import argparse
import fnmatch
import json
import math
import re
import sys

CPU_RULES = [("**.cpu.**", "ignore"), ("cpu.**", "ignore")]
_WORD = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def flatten(obj, path=(), out=None):
    """{path tuple: leaf}; an empty dict / list is a leaf of its own so that {} vs [] vs missing still shows."""
    if out is None:
        out = {}
    if isinstance(obj, dict) and obj:
        for k, v in obj.items():
            flatten(v, path + (str(k),), out)
    elif isinstance(obj, list) and obj:
        for i, v in enumerate(obj):
            flatten(v, path + (i,), out)
    else:
        out[path] = obj
    return out


def show_path(path):
    s = ""
    for seg in path:
        if isinstance(seg, int):
            s += ("." if s else "") + str(seg)
        elif _WORD.match(seg):
            s += ("." if s else "") + seg
        else:
            s += "[" + json.dumps(seg) + "]"
    return s or "(top)"


def match(pattern_segs, path):
    """pattern_segs: list of glob segments, '**' = any number of segments."""
    if not pattern_segs:
        return not path
    head = pattern_segs[0]
    if head == "**":
        return any(match(pattern_segs[1:], path[i:]) for i in range(len(path) + 1))
    if not path:
        return False
    return fnmatch.fnmatchcase(str(path[0]), head) and match(pattern_segs[1:], path[1:])


def parse_spec(spec):
    """('exact'|'abs'|'pct'|'ignore', value); ValueError on a bad spec."""
    s = str(spec).strip()
    if s == "ignore":
        return ("ignore", 0.0)
    if s.endswith("%"):
        v = float(s[:-1])
        kind = "pct"
    else:
        v = float(s)
        kind = "abs"
    if not math.isfinite(v) or v < 0:
        raise ValueError("tolerance must be a finite number >= 0: %r" % spec)
    if v == 0:
        return ("exact", 0.0)
    return (kind, v)


def parse_rule(text):
    if "=" not in text:
        raise ValueError("--tol wants PATTERN=SPEC, got %r" % text)
    pat, spec = text.rsplit("=", 1)
    pat = pat.strip()
    if not pat:
        raise ValueError("empty pattern in %r" % text)
    return (pat, spec.strip())


class Rules:
    def __init__(self, rules):
        self.rules = [(pat.split("."), parse_spec(spec), spec) for pat, spec in rules]

    def lookup(self, path):
        found = (("exact", 0.0), "0")
        for segs, parsed, spec in self.rules:
            if match(segs, path):
                found = (parsed, spec)
        return found


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def within(a, b, tol):
    kind, v = tol
    if kind == "ignore":
        return True
    if is_num(a) and is_num(b):
        if a == b:
            return True
        if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
            return True
        d = abs(a - b)
        if kind == "abs":
            return d <= v
        if kind == "pct":
            return d <= v / 100.0 * max(abs(a), abs(b))
        return False
    return a == b and type(a) is type(b)


def fmt(v):
    return json.dumps(v)


def compare(a, b, rules):
    """(equal, inside, outside, ignored): lists of (path, a, b, spec); missing sides are the string marker."""
    fa, fb = flatten(a), flatten(b)
    equal, inside, outside, ignored = [], [], [], []
    keys = list(fa.keys()) + [k for k in fb.keys() if k not in fa]
    for path in keys:
        (parsed, spec) = rules.lookup(path)
        has_a, has_b = path in fa, path in fb
        va, vb = fa.get(path), fb.get(path)
        if parsed[0] == "ignore":
            ignored.append((path, va, vb, spec))
            continue
        if not has_a or not has_b:
            outside.append((path, va if has_a else MISSING, vb if has_b else MISSING, spec))
            continue
        if va == vb and type(va) is type(vb) or (is_num(va) and is_num(vb) and va == vb):
            equal.append((path, va, vb, spec))
        elif within(va, vb, parsed):
            inside.append((path, va, vb, spec))
        else:
            outside.append((path, va, vb, spec))
    return equal, inside, outside, ignored


class _Missing:
    def __repr__(self):
        return "MISSING"


MISSING = _Missing()


def describe(item):
    path, va, vb, spec = item
    p = show_path(path)
    if va is MISSING:
        return "%s: only in b (%s)" % (p, fmt(vb))
    if vb is MISSING:
        return "%s: only in a (%s)" % (p, fmt(va))
    if is_num(va) and is_num(vb):
        d = vb - va
        ds = ("%+d" % d) if isinstance(d, int) else ("%+.6g" % d)
        return "%s: %s -> %s  (delta %s, tolerance %s)" % (p, fmt(va), fmt(vb), ds, spec)
    return "%s: %s -> %s  (tolerance %s)" % (p, fmt(va), fmt(vb), spec)


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Diff two harness --json reports with per-field tolerances.")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tol", action="append", default=[], metavar="PATTERN=SPEC",
                    help="tolerance for matching fields: 0, N, N%%, or ignore (repeatable; last match wins)")
    ap.add_argument("--tol-file", metavar="FILE", help='JSON object {"PATTERN": "SPEC"}')
    ap.add_argument("--include-cpu", action="store_true", help="also compare the cpu fields (ignored by default)")
    ap.add_argument("--verbose", "-v", action="store_true", help="also list fields within tolerance and the ignored count")
    ap.add_argument("--max-lines", type=int, default=200, help="at most this many differing fields listed (0 = all)")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    try:
        rules = [] if args.include_cpu else list(CPU_RULES)
        if args.tol_file:
            tf = load(args.tol_file)
            if not isinstance(tf, dict):
                raise ValueError("%s: want a JSON object of PATTERN: SPEC" % args.tol_file)
            rules += [(str(k), str(v)) for k, v in tf.items()]
        rules += [parse_rule(t) for t in args.tol]
        r = Rules(rules)
    except (ValueError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    try:
        a, b = load(args.a), load(args.b)
    except (OSError, ValueError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    equal, inside, outside, ignored = compare(a, b, r)
    if outside:
        print("differs: %d field(s)" % len(outside))
        lim = len(outside) if args.max_lines <= 0 else args.max_lines
        for it in outside[:lim]:
            print("  " + describe(it))
        if len(outside) > lim:
            print("  ... %d more (--max-lines 0 lists all)" % (len(outside) - lim))
    elif inside:
        print("equivalent: %d field(s) differ within tolerance" % len(inside))
    else:
        print("identical")
    if args.verbose:
        for it in inside:
            print("  within: " + describe(it))
        print("  (%d equal, %d within tolerance, %d differing, %d ignored)" % (len(equal), len(inside), len(outside), len(ignored)))
    return 1 if outside else 0


if __name__ == "__main__":
    sys.exit(main())
