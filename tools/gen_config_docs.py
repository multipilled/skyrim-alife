#!/usr/bin/env python3
"""Generate docs/CONFIG.md, the config key reference (ticket G4). Python 3 standard library only.

    python3 tools/gen_config_docs.py config > docs/CONFIG.md

The page lists, per config/*.ltx file and section, every key with its type, its value in config/ (the default the mod
ships), the range or allowed values the loader accepts, the difficulty channel tag from the ltx comment (T21) and a
one-line meaning (the ltx comment, else the loader's comment). Each row cites the C++ line that reads the key.

How the loaders are found (no hand-kept list of keys): every .cpp under src/ (src/core first) is scanned for reads on an
`Ltx` object (`Get`, `GetF`, `GetI`, `GetB`, `List`, `Has`, `Section`), on small helpers that take an `Ltx` (Pair, Five,
Ids, PerFaction, FillFloats, ReadSlots) and on lambdas that wrap one (`f(sec, key, v)`, `glob(key)`). The `Ltx` object is
traced to its file through `<var>.LoadFile(... "x.ltx")` or, for a loader parameter, through its callers. The config files
are read by tools/ltxlib.py (ticket G7), the LTX reader shared with tools/check_config.py; this file only adds which
comment belongs to which key or section. Output is deterministic: the same tree gives the same page byte for byte.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ------------------------------------------------------------------------------------------------ LTX, as src/core/Ltx.cpp


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ltxlib  # noqa: E402


class LtxFile(ltxlib.Ltx):
    """One config file: sections in file order, own keys only (inheritance is listed, not resolved; #include lines are
    listed in `includes`, not loaded: every config/*.ltx gets its own part of the page). Parsing is tools/ltxlib.py;
    the on_line hook below attaches the ltx comments: a section's `above` block, its `header_comment`, each key's
    `comment` (an indented comment line continues the key above) and the file's `intro`."""

    def __init__(self, path, text=None):
        self.name = os.path.basename(path)
        self.intro, self._cur, self._block, self._last, self._seen = [], None, [], None, False
        if text is None:
            with open(path, "rb") as f:
                text = f.read()
        ltxlib.Ltx.__init__(self, path, text=text, follow_includes=False)
        while self.intro and self.intro[-1] == "":
            self.intro.pop()
        blocks = list(self.intro)
        for name in self.order:
            blocks += self.sections[name].above
        self.gloss = glossary(blocks)

    def on_line(self, path, no, raw, kind, com, item):
        if kind == "blank":
            if com is not None and com != "":
                if self._last is not None and raw[:1] in (" ", "\t") and not self._block:
                    self._last.comment = (self._last.comment + " " + com).strip()   # an indented comment line continues the key above
                else:
                    self._block.append(com)
                    self._last = None
            elif com is None:   # blank line ends a comment block
                if not self._seen and self._block:
                    self.intro.extend(self._block + [""])
                self._block, self._last = [], None
        elif kind == "header":
            sec, reopened = item
            self._seen = True
            if not reopened:
                sec.above, sec.header_comment, sec.keys = self._block, "", sec.lines
            if com:
                sec.header_comment = com
            self._block, self._last = [], None
        elif kind == "key":
            self._last = item
            self._block = []
        else:   # directive, bad header, key outside a section
            self._last = None


# ------------------------------------------------------------------------------------------------ C++ scanning


def strip_cpp_comments(text):
    """Code with // and /* */ comments blanked (newlines kept, strings intact) and a {line: trailing comment} map."""
    out, comments, i, n, line = [], {}, 0, len(text), 1
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments[line] = text[i + 2:j].strip()
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            seg = text[i:j]
            out.append(re.sub(r"[^\n]", " ", seg))
            line += seg.count("\n")
            i = j
        else:
            if c == "\n":
                line += 1
            out.append(c)
            i += 1
    return "".join(out), comments


def split_args(text, start):
    """Top-level arguments of the call whose '(' is at text[start - 1]; returns (args, index after ')')."""
    depth, args, cur, i = 0, [], [], start
    angle = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append("".join(cur).strip())
                return [a for a in args if a != ""] if args != [""] else [], i + 1
            depth -= 1
        elif c == "<" and depth == 0 and re.match(r"[\w:]+\s*$", "".join(cur)) and re.match(r"<[\w\s:,]*>", text[i:]):
            angle += 1
        elif c == ">" and angle:
            angle -= 1
        elif c == "," and depth == 0 and not angle:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return args, i


def literal(arg):
    m = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', arg.strip()) if arg else None
    return m.group(1) if m else None


class Src:
    def __init__(self, rel):
        self.rel = rel
        with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
            self.raw = f.read()
        self.code, self.comments = strip_cpp_comments(self.raw)
        self.lines = self.code.split("\n")
        self.nl = [m.start() for m in re.finditer("\n", self.code)]
        self.arrays = {}
        for m in re.finditer(r"(\w+)\s*\[[^\]]*\]\s*=\s*\{([^{}]*)\}", self.code):
            vals = re.findall(r'"([^"]*)"', m.group(2))
            if vals:
                self.arrays[m.group(1)] = vals
        for m in re.finditer(r"(\w+)\[\]\s*=\s*\{((?:[^{}]|\{[^{}]*\})*)\}", self.code):   # struct tables: first string of each row
            rows = re.findall(r'\{\s*"([^"]*)"', m.group(2))
            if rows and m.group(1) not in self.arrays:
                self.arrays[m.group(1)] = rows

    def lineno(self, pos):
        lo, hi = 0, len(self.nl)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.nl[mid] < pos:
                lo = mid + 1
            else:
                hi = mid
        return lo + 1

    def find_line(self, pattern, near=None):
        hits = [i + 1 for i, ln in enumerate(self.raw.split("\n")) if re.search(pattern, ln)]
        if not hits:
            return None
        if near is None:
            return hits[0]
        return min(hits, key=lambda h: (abs(h - near), h))


def source_files():
    out = []
    for d in ("src/core", "src"):
        full = os.path.join(ROOT, d)
        if os.path.isdir(full):
            out += [os.path.join(d, f) for f in sorted(os.listdir(full)) if f.endswith(".cpp")]
    return [s for s in out if s != "src/core/Ltx.cpp"]


TYPE_OF = {"GetF": "number", "GetI": "integer", "GetB": "bool", "Get": "string", "List": "list", "Has": "presence (value not read)",
           "Section": "section"}
# helper wrappers whose meaning is not the first Ltx call in their body
HELPER_TYPE = {"Pair": "pair `min, max` (swapped when max < min)", "Five": "list of 5 numbers", "Ids": "list of hex FormIDs"}
LTX_CALL = re.compile(r"(?<![\w.>])([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*(?:\.|->)\s*(GetF|GetI|GetB|Get|List|Has|Section|Sections)\s*\(")


class Read:
    def __init__(self, **kw):
        self.ltx = None
        self.__dict__.update(kw)

    @property
    def cite(self):
        return "%s:%d" % (self.src, self.line)


class Scanner:
    def __init__(self, files=None):
        self.srcs = {rel: Src(rel) for rel in (files or source_files())}
        self.decl = {}          # (rel, var) -> [(line, kind, data)]   kind: load | param | alias
        self.funcs = {}         # name -> [(rel, line, [param names], {index: ltx param}, end line)]
        self.helpers = {}       # (rel, name) -> spec
        self.lambdas = {}       # (rel, name) -> [(line, spec)]
        self.reads = []
        self.loads = {}         # ltx file -> [cite]
        for s in self.srcs.values():
            self.scan_decls(s)
        self.resolve_params()
        for s in self.srcs.values():
            self.scan_helpers(s)
        for s in self.srcs.values():
            self.scan_reads(s)
        for r in self.reads:
            r.ltx = self.ltx_of(r.src, r.obj, r.line)

    # ---- which Ltx object is which file
    def add_decl(self, rel, var, line, kind, data):
        self.decl.setdefault((rel, var), []).append((line, kind, data))

    def scan_decls(self, s):
        for m in re.finditer(r"([\w.]+)\s*\.\s*LoadFile\s*\(([^;]*?)\"/?([\w.]+\.ltx)\"", s.code):
            var = m.group(1).split(".")[-1]
            ln = s.lineno(m.start())
            self.add_decl(s.rel, var, ln, "load", m.group(3))
            self.loads.setdefault(m.group(3), []).append("%s:%d" % (s.rel, ln))
        for m in re.finditer(r"const\s+Ltx\s*&\s*(\w+)\s*=\s*([\w.]+)\s*;", s.code):
            self.add_decl(s.rel, m.group(1), s.lineno(m.start()), "alias", m.group(2).split(".")[-1])
        for m in re.finditer(r"\b(\w+)\s*\(([^()]*\bconst\s+Ltx\s*[&*][^()]*)\)\s*(?:const\s*)?\{?", s.code):
            name, params = m.group(1), m.group(2)
            if name in ("if", "for", "while", "switch", "return"):
                continue
            plist, _ = split_args(params + ")", 0)
            names, ltxi = [], {}
            for i, p in enumerate(plist):
                pm = re.search(r"(\w+)\s*(?:=[^,]*)?$", p)
                names.append(pm.group(1) if pm else "")
                if re.search(r"\bLtx\s*[&*]", p):
                    ltxi[i] = names[-1]
            ln = s.lineno(m.start())
            if not re.search(r"\{\s*$", s.lines[ln - 1]) and not (ln < len(s.lines) and s.lines[ln].strip() == "{"):
                continue   # a declaration, not a definition
            self.funcs.setdefault(name, []).append((s.rel, ln, names, ltxi, self.body_end(s, ln)))
            for i, var in ltxi.items():
                self.add_decl(s.rel, var, ln, "param", (name, i))

    @staticmethod
    def body_end(s, ln):
        ind = len(s.lines[ln - 1]) - len(s.lines[ln - 1].lstrip())
        for j in range(ln, len(s.lines)):
            t = s.lines[j]
            if t.strip() == "}" and len(t) - len(t.lstrip()) == ind:
                return j + 1
        return len(s.lines)

    def resolve_params(self):
        self.param_ltx = {}   # (func, index) -> ltx file
        for _ in range(6):
            changed = False
            for name, defs in self.funcs.items():
                for s in self.srcs.values():
                    for m in re.finditer(r"(?<![\w.:])" + re.escape(name) + r"\s*(?:<[^<>;()]*>)?\s*\(", s.code):
                        ln = s.lineno(m.start())
                        if any(d[0] == s.rel and d[1] == ln for d in defs):
                            continue
                        args, _ = split_args(s.code, m.end())
                        local = [d for d in defs if d[0] == s.rel] or defs
                        for d in local[:1]:
                            for i in d[3]:
                                if i < len(args):
                                    am = re.findall(r"&?\s*([A-Za-z_][\w.]*)", args[i])
                                    for cand in am:
                                        f = self.ltx_of(s.rel, cand, ln, partial=True)
                                        if f and (name, i) not in self.param_ltx:
                                            self.param_ltx[(name, i)] = f
                                            changed = True
                                            break
            if not changed:
                break

    def ltx_of(self, rel, obj, line, partial=False):
        var = obj.split(".")[-1]
        cands = [d for d in self.decl.get((rel, var), []) if d[0] <= line + 0] or self.decl.get((rel, var), [])
        if not cands and "." in obj:
            for (r2, v2), ds in self.decl.items():   # a member such as c.props / cfg.props: its LoadFile anywhere
                if v2 == var:
                    cands += [d for d in ds if d[1] == "load"]
        if not cands:
            return None
        best = max(cands, key=lambda d: d[0])
        # a load that follows the declaration in the same scope (Ltx x; ... x.LoadFile) counts for reads after it
        loads = [d for d in cands if d[1] == "load" and d[0] <= line]
        params = [d for d in cands if d[1] == "param" and d[0] <= line]
        if params and (not loads or max(params)[0] > max(loads)[0]):
            best = max(params)
        elif loads:
            best = max(loads)
        if best[1] == "load":
            return best[2]
        if best[1] == "alias":
            return self.ltx_of(rel, best[2], line) or self.ltx_of(rel, "x." + best[2], line)
        return self.param_ltx.get(best[2])

    # ---- helpers and lambdas that wrap an Ltx read
    def scan_helpers(self, s):
        chained = []
        for name, defs in self.funcs.items():
            for (rel, ln, names, ltxi, end) in defs:
                if rel != s.rel or name.startswith("Load"):
                    continue
                body = "\n".join(s.lines[ln:end])
                m = LTX_CALL.search(body)
                if not m or m.group(1) not in ltxi.values() or m.group(2) == "Sections":
                    chained.append((name, rel, ln, names, ltxi, body))
                    continue
                args, _ = split_args(body, m.end())
                spec = {"ltx_index": [i for i, v in ltxi.items() if v == m.group(1)][0], "fn": m.group(2),
                        "sec": self.param_or_lit(args, 0, names), "key": self.param_or_lit(args, 1, names),
                        "line": ln, "kind": "helper"}
                self.helpers[(s.rel, name)] = spec
        for _ in range(3):   # a helper that only forwards to another helper (FillBools -> FillFloats)
            for (name, rel, ln, names, ltxi, body) in chained:
                if (rel, name) in self.helpers or name.startswith("Load"):
                    continue
                for (r2, other), spec in list(self.helpers.items()):
                    if r2 != rel:
                        continue
                    cm = re.search(r"(?<![\w.:])" + re.escape(other) + r"\s*\(", body)
                    if not cm:
                        continue
                    args, _ = split_args(body, cm.end())
                    if spec["ltx_index"] >= len(args) or args[spec["ltx_index"]] not in ltxi.values():
                        continue
                    def through(slot):
                        if slot[0] == "param" and slot[1] < len(args):
                            return self.param_or_lit(args, slot[1], names)
                        return slot
                    self.helpers[(rel, name)] = {"ltx_index": [i for i, v in ltxi.items() if v == args[spec["ltx_index"]]][0],
                                                 "fn": spec["fn"], "sec": through(spec["sec"]), "key": through(spec["key"]),
                                                 "line": ln, "kind": "helper"}
                    break
        for m in re.finditer(r"auto\s+(\w+)\s*=\s*\[[^\]]*\]\s*\(([^)]*)\)", s.code):
            ln = s.lineno(m.start())
            plist, _ = split_args(m.group(2) + ")", 0)
            names = [re.search(r"(\w+)\s*$", p).group(1) if re.search(r"(\w+)\s*$", p) else "" for p in plist]
            tail = s.code[m.end():m.end() + 400]
            cm = LTX_CALL.search(tail)
            if not cm or cm.group(2) == "Sections":
                continue
            args, _ = split_args(tail, cm.end())
            spec = {"obj": cm.group(1), "fn": cm.group(2), "sec": self.param_or_lit(args, 0, names),
                    "key": self.param_or_lit(args, 1, names), "line": ln, "kind": "lambda"}
            if spec["sec"][0] == "param" or spec["key"][0] == "param":
                self.lambdas.setdefault((s.rel, m.group(1)), []).append((ln, spec))

    @staticmethod
    def param_or_lit(args, i, names):
        if i >= len(args):
            return ("none", None)
        a = args[i]
        if a in names:
            return ("param", names.index(a))
        return ("expr", a)

    # ---- reads
    def sec_value(self, s, expr, line):
        """A section argument -> (literal or None, [patterns], expr)."""
        lit = literal(expr)
        if lit is not None:
            return lit, [lit], expr
        e = re.sub(r"\.c_str\(\)$", "", expr.strip())
        if re.fullmatch(r"\w+", e):
            best = None
            for j in range(line, 0, -1):   # const char* S = "stashes"; the nearest one above the read
                m = re.search(r"const\s+char\s*\*\s*" + e + r"\s*=\s*\"([^\"]*)\"", s.lines[j - 1])
                if m:
                    return m.group(1), [m.group(1)], expr
            for j in range(line - 1, max(0, line - 40), -1):
                t = s.lines[j - 1]
                m = re.search(r"(?:std::string|auto)\s+" + e + r"\s*=\s*(.+);", t)
                if m:
                    best = self.expr_patterns(m.group(1))
                    for k in range(j, line):   # later plain reassignments: section = "pos_dlc2";
                        for am in re.finditer(r"\b" + e + r'\s*=\s*"([^"]*)"\s*;', s.lines[k]):
                            if am.group(1) not in best:
                                best.append(am.group(1))
                    break
                m = re.search(r"for\s*\(\s*(?:const\s+)?auto\s*&?\s*(\[[^\]]*\]|\w+)\s*:\s*([^)]*)\)", t)
                if m and re.search(r"\b" + e + r"\b", m.group(1)):
                    best = ["*"]
                    nxt = " ".join(s.lines[j:j + 3])
                    pm = re.search(e + r'\.rfind\("([^"]+)",\s*0\)', nxt)
                    if pm:
                        best = [pm.group(1) + "*"]
                    break
                if re.search(r"\b(?:const\s+)?(?:char|std::string)\b[^;]*\b" + e + r"\s*[\[,)]", t) and j != line:
                    best = ["*"]
                    break
            return None, best or ["*"], expr
        return None, ["*"], expr

    @staticmethod
    def expr_patterns(expr):
        """Section names a string expression can take: "a" -> a, "a" + x -> a*, a ? "b" : "c" + x -> b, c*; else *."""
        e = re.sub(r'std::string\(\s*("(?:[^"\\]|\\.)*")\s*\)', r"\1", expr.strip())
        pats = []
        for branch in re.split(r"\s[?:]\s", e):
            b = branch.strip()
            m = re.match(r'"((?:[^"\\]|\\.)*)"\s*(\+)?', b)
            if not m:
                if re.search(r"[=<>!]=|<|>", b) and branch is not re.split(r"\s[?:]\s", e)[-1]:
                    continue   # the condition of a ternary
                return ["*"]
            pats.append(m.group(1) + ("*" if m.group(2) else ""))
        return pats or ["*"]

    def key_value(self, s, expr):
        lit = literal(expr)
        if lit is not None:
            return lit, None
        e = expr.strip()
        m = re.fullmatch(r"(\w+)\[[^\]]*\]", e)
        if m:
            for src in [s] + list(self.srcs.values()):
                if m.group(1) in src.arrays:
                    return None, list(src.arrays[m.group(1)])
        if re.fullmatch(r"\w+", e) and re.search(r"\bchar\s+" + e + r"\s*\[", s.code) and re.search(r"snprintf\(\s*" + e + r"\b", s.code):
            return None, ["*"]   # a key printed into a buffer (a FormID in hex): any entry of the section
        if re.fullmatch(r"FactionId\(.*\)", e):
            return None, self.array("src/core/Core.cpp", "kFac")
        if re.fullmatch(r"KindName\(.*\)", e):
            return None, self.array("src/core/Core.cpp", "kKindNames")
        m = re.fullmatch(r'"(\w+)"\s*\+\s*std::to_string\(\s*\w+\s*\+\s*1\s*\)', e)
        if m:
            return None, [m.group(1) + str(i) for i in range(1, 33)]
        return None, None

    def array(self, rel, name):
        s = self.srcs.get(rel)
        return list(s.arrays.get(name, [])) if s else []

    def scan_reads(self, s):
        for m in LTX_CALL.finditer(s.code):
            obj, fn = m.group(1), m.group(2)
            if fn == "Sections":
                continue
            ln = s.lineno(m.start())
            if not self.is_ltx(s.rel, obj, ln):
                continue
            if self.in_helper(s.rel, ln):
                continue
            args, end = split_args(s.code, m.end())
            self.add_read(s, ln, obj, fn, args, m.start(), end, None)
        for (rel, name), spec in self.helpers.items():
            if rel != s.rel and any(r == s.rel for (r, n) in self.helpers if n == name):
                continue
            for m in re.finditer(r"(?<![\w.:])" + re.escape(name) + r"\s*(?:<[^<>;()]*>)?\s*\(", s.code):
                ln = s.lineno(m.start())
                if ln == spec["line"]:
                    continue
                args, end = split_args(s.code, m.end())
                i = spec["ltx_index"]
                if i >= len(args) or not self.is_ltx(s.rel, args[i], ln):
                    continue
                sec = args[spec["sec"][1]] if spec["sec"][0] == "param" and spec["sec"][1] < len(args) else spec["sec"][1]
                key = args[spec["key"][1]] if spec["key"][0] == "param" and spec["key"][1] < len(args) else spec["key"][1]
                rargs = [a for a in (sec, key) if a is not None]
                self.add_read(s, ln, args[i], spec["fn"], rargs, m.start(), end, name)
        for (rel, name), specs in self.lambdas.items():
            if rel != s.rel:
                continue
            for m in re.finditer(r"(?<![\w.:])" + re.escape(name) + r"\s*\(", s.code):
                ln = s.lineno(m.start())
                prior = [sp for (l0, sp) in specs if l0 <= ln]
                if not prior or ln == max(l0 for (l0, _) in specs if l0 <= ln):
                    continue
                spec = max(((l0, sp) for (l0, sp) in specs if l0 <= ln), key=lambda x: x[0])[1]
                args, end = split_args(s.code, m.end())
                sec = args[spec["sec"][1]] if spec["sec"][0] == "param" and spec["sec"][1] < len(args) else spec["sec"][1]
                key = args[spec["key"][1]] if spec["key"][0] == "param" and spec["key"][1] < len(args) else spec["key"][1]
                rargs = [a for a in (sec, key) if a is not None]
                self.add_read(s, ln, spec["obj"], spec["fn"], rargs, m.start(), end, name)

    def is_ltx(self, rel, obj, line):
        var = obj.split(".")[-1]
        if (rel, var) in self.decl:
            return True
        return "." in obj and any(v == var and any(d[1] == "load" for d in ds) for (r, v), ds in self.decl.items())

    def in_helper(self, rel, ln):
        for (r, name), spec in self.helpers.items():
            if r == rel:
                for d in self.funcs.get(name, []):
                    if d[0] == rel and d[1] <= ln <= d[4]:
                        return True
        for (r, name), specs in self.lambdas.items():
            if r == rel and any(l0 == ln for (l0, _) in specs):
                return True
        return False

    def add_read(self, s, ln, obj, fn, args, start, end, via):
        if not args:
            return
        if fn == "Has" and len(args) == 1:
            return   # a section presence check, not a key
        sec_lit, sec_pats, sec_expr = self.sec_value(s, args[0], ln)
        key_lit, keyset, key_expr = None, None, None
        if fn != "Section" and len(args) >= 2:
            key_expr = args[1]
            key_lit, keyset = self.key_value(s, args[1])
        default = args[2] if fn in ("Get", "GetF", "GetI", "GetB") and len(args) >= 3 else None
        lo = s.code.rfind("\n", 0, start) + 1
        hi = s.code.find("\n", end)
        stmt = s.code[lo:hi if hi >= 0 else len(s.code)]
        self.reads.append(Read(src=s.rel, line=ln, obj=obj, fn=fn, via=via, sec=sec_lit, sec_pats=sec_pats,
                               sec_expr=sec_expr, key=key_lit, keyset=keyset, key_expr=key_expr, default=default,
                               stmt=stmt, call=s.code[start:end], comment=s.comments.get(ln, ""),
                               type=HELPER_TYPE.get(via, TYPE_OF.get(fn, fn))))


# ------------------------------------------------------------------------------------------------ ranges


def constants():
    out = {}
    for d in ("include/ALife", "src/core"):
        full = os.path.join(ROOT, d)
        if not os.path.isdir(full):
            continue
        for f in sorted(os.listdir(full)):
            with open(os.path.join(full, f), encoding="utf-8", errors="replace") as fh:
                for m in re.finditer(r"constexpr\s+(?:int|float|double|unsigned|std::\w+)\s+(k\w+)\s*=\s*([-\d.]+)f?\s*;", fh.read()):
                    out.setdefault(m.group(1), m.group(2))
    return out


CONST = {}


LOCAL = {}


def num(expr):
    e = expr.strip()
    e = re.sub(r"\b(\d+(?:\.\d*)?)f\b", r"\1", e)
    e = re.sub(r"^(?:float|int|double)\((.*)\)$", r"\1", e)
    e = re.sub(r"\b(k\w+)\b", lambda m: LOCAL.get(m.group(1), CONST.get(m.group(1), m.group(1))), e)
    if re.fullmatch(r"-?\d+\.0*", e):
        e = e.split(".")[0]
    elif re.fullmatch(r"-?\d+\.\d*0", e):
        e = e.rstrip("0")
    m = re.fullmatch(r"(-?\d+)\s*([-+*/])\s*(-?\d+)", e)
    if m:
        a, op, b = int(m.group(1)), m.group(2), int(m.group(3))
        v = {"+": a + b, "-": a - b, "*": a * b, "/": a // b if b and a % b == 0 else None}[op]
        if v is not None:
            e = str(v)
    m = re.fullmatch(r"[A-Za-z_]\w*\.([A-Za-z_]\w*(?:\[\w+\])?)", e)
    if m:
        e = m.group(1)
    return e


def enclosing_clamps(stmt, call):
    """Bounds from std::clamp / std::max / std::min calls that wrap `call` inside one statement."""
    out, pos = [], stmt.find(call)
    if pos < 0:
        return out
    for m in re.finditer(r"std::(clamp|max|min)\s*\(", stmt):
        if m.start() > pos:
            break
        args, end = split_args(stmt, m.end())
        if end <= pos + len(call) - 1:
            continue
        others = [a for a in args if call not in a]
        if len(others) != len(args) - 1:
            continue
        if m.group(1) == "clamp" and len(args) == 3 and call in args[0]:
            out.append("[%s, %s]" % (num(args[1]), num(args[2])))
        elif m.group(1) == "max" and len(others) == 1:
            out.append(">= %s" % num(others[0]))
        elif m.group(1) == "min" and len(others) == 1:
            out.append("<= %s" % num(others[0]))
    return out


def targets(read):
    """The fields a read fills: the left side of `x = ...read...` and reference arguments of a helper."""
    t = []
    m = re.match(r"\s*(?:if\s*\(.*\)\s*)?([\w.\[\]]+)\s*=[^=]", read.stmt)
    if m:
        t.append(m.group(1))
    if read.via:
        t += re.findall(r"\b(?:c|d|k|m)\.\w+", read.call)
    else:
        mm = re.search(r"\bf\s*\(", read.stmt)
        if mm:
            t += re.findall(r"\b(?:c|s)\.\w+", read.stmt)
    if read.via:   # locals a helper fills, copied into fields right after: c.helperRankMin = int(rlo);
        args, _ = split_args(read.call, read.call.find("(") + 1)
        src = SCAN.srcs[read.src]
        for a in args:
            if re.fullmatch(r"[a-z]\w*", a) and a not in ("t", "x", "l", "err", "ok"):
                for ln in src.lines[read.line:read.line + 5]:
                    mm = re.match(r"\s*((?:c|d|k|m|s)\.\w+)\s*=\s*\w*\(?\s*" + a + r"\s*\)?\s*;", ln)
                    if mm:
                        t.append(mm.group(1))
    seen = []
    for x in t:
        if x not in seen and re.fullmatch(r"(?:c|d|k|m|s)\.\w+", x):
            seen.append(x)
    return seen


def post_clamps(scan, read):
    """Clamps applied after the read: `field = std::max(lo, field)` and the Clamp*Config helpers frac / pos / range."""
    s = scan.srcs[read.src]
    out = []
    for fld in targets(read):
        name = fld.split(".")[1]
        pat_field = r"\b" + re.escape(fld) + r"\b"
        for i, ln in enumerate(s.lines, 1):
            if i == read.line or not re.search(pat_field, ln) or LTX_CALL.search(ln):
                continue
            t = ln.strip()
            m = re.match(re.escape(fld) + r"\s*=\s*(.*);$", t)
            if m and re.search(pat_field, m.group(1)) and "std::" in m.group(1) and "*" not in m.group(1):
                rng = enclosing_clamps(m.group(1), re.search(pat_field, m.group(1)).group(0))
                if rng:
                    out.append((" and ".join(rng), "%s:%d" % (read.src, i)))
                continue
            m = re.search(r"\bfrac\(\s*" + re.escape(fld) + r"\s*\)", t)
            if m:
                out.append(("[0, 1]", "%s:%d" % (read.src, i)))
                continue
            m = re.search(r"\bpos\(\s*" + re.escape(fld) + r"\s*(?:,\s*([^)]+))?\)", t)
            if m:
                out.append((">= %s" % num(m.group(1) or "0"), "%s:%d" % (read.src, i)))
                continue
            m = re.search(r"\brange\(\s*([\w.]+)\s*,\s*([\w.]+)\s*,\s*([^)]+)\)", t)
            if m and re.search(pat_field, m.group(1) + " " + m.group(2)):
                out.append((">= %s, swapped if max < min" % num(m.group(3)), "%s:%d" % (read.src, i)))
                continue
            m = re.search(r"for\s*\(auto\s*&\s*(\w+)\s*:\s*" + re.escape(fld) + r"\)\s*\1\s*=\s*(std::.*);", t)
            if m:
                rng = enclosing_clamps(m.group(2), m.group(1))
                if rng:
                    out.append((" and ".join(rng), "%s:%d" % (read.src, i)))
                continue
            m = re.search(r"for\s*\(auto\s*\*\s*(\w+)\s*:\s*\{([^}]*)\}\s*\)", t)
            if m and re.search(pat_field, m.group(2)):
                nxt = s.lines[i] if i < len(s.lines) else ""
                cm = re.search(r"std::clamp\(\s*\*\w+\s*,\s*([^,]+),\s*([^)]+)\)", nxt)
                if cm:
                    out.append(("[%s, %s]" % (num(cm.group(1)), num(cm.group(2))), "%s:%d" % (read.src, i + 1)))
    return out


def entry_clamps(scan, read):
    """A section read entry by entry: std::clamp(std::stof(kv.second), lo, hi) in the loop (or in the helper it calls)."""
    s = scan.srcs[read.src]
    spans = [(read.line, read.line + 12)]
    if read.via:
        for d in scan.funcs.get(read.via, []):
            if d[0] == read.src:
                spans.append((d[1], d[4]))
    out = []
    for a, b in spans:
        for i in range(a, min(b, len(s.lines)) + 1):
            m = re.search(r"std::clamp\(\s*std::sto[fi]\(\s*\w+\.second\s*\)\s*,\s*([^,]+),\s*([^)]+)\)", s.lines[i - 1])
            if m:
                out.append(("[%s, %s]" % (num(m.group(1)), num(m.group(2))), "%s:%d" % (read.src, i)))
    return out


def item_clamps(scan, read):
    """A list read into a local, then each item clamped: auto lim = x.List(...); ... std::max(0.f, ...lim[0]...)."""
    m = re.match(r"\s*(?:const\s+)?auto\s+(\w+)\s*=\s*$", read.stmt[:read.stmt.find(read.call)]) if read.call in read.stmt else None
    if not m:
        return []
    var, s, out = m.group(1), scan.srcs[read.src], []
    for i in range(read.line, min(read.line + 6, len(s.lines))):
        ln = s.lines[i]
        for im in re.finditer(r"\b" + var + r"\[(\d+)\]", ln):
            for b in enclosing_clamps(ln, im.group(0)):
                txt = "item %d %s" % (int(im.group(1)) + 1, b)
                if txt not in [o[0] for o in out]:
                    out.append((txt, "%s:%d" % (read.src, i + 1)))
    return out


def allowed_values(scan, read):
    """String keys compared with literals right after the read: `m == "off" ? ... `."""
    s = scan.srcs[read.src]
    m = re.search(r"(?:auto|std::string)\s+(\w+)\s*=\s*(?:Lower\()?[^;]*" + re.escape(read.call), read.stmt)
    vals = []
    if m:
        var = m.group(1)
        for ln in s.lines[read.line - 1:read.line + 4]:
            vals += re.findall(r"\b" + var + r'\s*==\s*"([^"]*)"', ln)
    i = read.stmt.find(read.call)
    tail = read.stmt[i + len(read.call):]
    vals += re.findall(r'^\)*\s*==\s*"([^"]*)"', tail)
    out = []
    for v in vals:
        if v not in out:
            out.append(v)
    return out


# ------------------------------------------------------------------------------------------------ matching


def matches(pats, name):
    for p in pats:
        if p == "*" or p == name or (p.endswith("*") and name.startswith(p[:-1])):
            return True
    return False


def pick(reads, sec, key):
    tiers = ([], [], [], [], [])
    for r in reads:
        if r.fn == "Section":
            if r.sec == sec:
                tiers[3].append(r)
            elif r.sec is None and matches(r.sec_pats, sec) and r.sec_pats != ["*"]:
                tiers[4].append(r)
            continue
        lit_sec = r.sec == sec
        dyn_sec = r.sec is None and matches(r.sec_pats, sec)
        if r.key == key:
            if lit_sec:
                tiers[0].append(r)
            elif dyn_sec:
                tiers[2].append(r)
        elif r.key is None and r.keyset and (key in r.keyset or r.keyset == ["*"]):
            if lit_sec:
                tiers[1].append(r)
            elif dyn_sec:
                tiers[2].append(r)
    for i, t in enumerate(tiers):
        if t:
            return i, t
    return None, []


# ------------------------------------------------------------------------------------------------ page

CHANNEL = re.compile(r"\{([a-z]+[+-])\}")          # wounded.ltx / stashes.ltx: {lethality-} anywhere in the comment
LEAD_TAG = re.compile(r"^\[([a-z]+|-)\]\s*")       # tasks.ltx / dragons.ltx: [economy] or [-] at the start of the comment


def channel_of(comment):
    """(tag shown in the Channel column or None, comment without the tag)."""
    c = comment or ""
    m = LEAD_TAG.match(c)
    if m:
        tag = "`[-]` not scaled" if m.group(1) == "-" else "`[%s]`" % m.group(1)
        return tag, c[m.end():].strip()
    t = CHANNEL.findall(c)
    if t:
        return "`{%s}`" % t[0], CHANNEL.sub("", c).strip()
    return None, c


GLOSS = re.compile(r"^([A-Za-z_<][\w<> /|=.,-]*?)\s+:\s+(\S.*)$")


def glossary(lines):
    """Key notes from a comment block written as `key / other_key : what it does` (props.ltx, compat.ltx headers)."""
    out = {}
    for line in lines:
        m = GLOSS.match(line.strip())
        if not m:
            continue
        for part in m.group(1).split("/"):
            part = part.strip()
            km = re.match(r"([A-Za-z_]\w*)", part)
            if km and not part.startswith("<"):
                out.setdefault(km.group(1), m.group(2).strip())
    return out
FORMID = re.compile(r"^[0-9A-Fa-f]{3,8}$")


def esc(s):
    return s.replace("|", "\\|")


def code(s, limit=70):
    s = s.replace("`", "'")
    if len(s) > limit:
        s = s[:limit - 3].rstrip(", ") + "..."
    return "`%s`" % esc(s) if s != "" else "(empty)"


def one_line(s, limit=220):
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > limit:
        s = s[:limit - 3].rstrip() + "..."
    return esc(s)


def section_channel(sec):
    tags = []
    for line in sec.above + ([sec.header_comment] if sec.header_comment else []):
        tags += CHANNEL.findall(line)
    return tags[0] if len(set(tags)) == 1 else None


def cites(reads, limit=4):
    seen = []
    for r in reads:
        if r.cite not in seen:
            seen.append(r.cite)
    txt = ", ".join("`%s`" % c for c in seen[:limit])
    if len(seen) > limit:
        txt += " +%d more" % (len(seen) - limit)
    return txt


class Row:
    def __init__(self, key):
        self.key = key
        self.values, self.comments, self.lines, self.sections = [], [], [], []


def describe(scan, reads, tier, values, comment, sec_tag, inferred, gloss=""):
    """(type, range, channel, meaning, loader) for one key."""
    uniq = {}
    for r in reads:
        uniq.setdefault(r.cite + r.fn, r)
    reads = list(uniq.values())
    named = [r for r in reads if r.fn != "Section"]
    if any(r.fn != "Has" for r in named):
        named_t = [r for r in named if r.fn != "Has"]
    else:
        named_t = named
    rng, cite_extra = [], []
    LOCAL.clear()
    if reads:
        for m in re.finditer(r"constexpr\s+(?:int|float|double)\s+(k\w+)\s*=\s*([-\d.]+)f?\s*;", scan.srcs[reads[0].src].code):
            LOCAL.setdefault(m.group(1), m.group(2))
    if named:
        types = []
        for r in named_t:
            if r.type not in types:
                types.append(r.type)
        typ = " / ".join(types)
        for r in named:
            for b in enclosing_clamps(r.stmt, r.call):
                if b not in rng:
                    rng.append(b)
            for b, c in post_clamps(scan, r) + item_clamps(scan, r):
                if b not in rng:
                    rng.append(b)
                    cite_extra.append(c)
            av = allowed_values(scan, r)
            if av:
                txt = "compared with " + ", ".join("`%s`" % v for v in av) + (" (loader default `%s`)" % literal(r.default) if r.default and literal(r.default) is not None else "")
                if txt not in rng:
                    rng.append(txt)
        if not rng:
            if any(r.fn == "GetB" for r in named):
                rng.append("`true` `on` `yes` `1` = on, anything else off")
            elif any(r.fn == "GetI" for r in named):
                rng.append("any integer (read as int(stof))")
            elif any(r.fn == "GetF" for r in named):
                rng.append("any number")
            else:
                rng.append("-")
        loader = cites(named)
        if cite_extra:
            loader += "; clamp " + ", ".join("`%s`" % c for c in dict.fromkeys(cite_extra))
        if tier == 2 and all(r.sec is None for r in named):
            loader += " (section from a variable)"
    else:
        typ = inferred + " (entry)"
        for r in reads:
            for b, c in entry_clamps(scan, r):
                if b not in rng:
                    rng.append(b)
                    cite_extra.append(c)
        if not rng:
            rng.append("-")
        loader = (cites(reads) + " (reads every entry of the section)") if reads else "**not read by src/**"
        if cite_extra:
            loader += "; clamp " + ", ".join("`%s`" % c for c in dict.fromkeys(cite_extra))
    ch, text = channel_of(comment)
    ch = ch or (("`{%s}`" % sec_tag) if sec_tag else None)
    meaning = text or next((r.comment for r in named if r.comment), "") or gloss or "-"
    return typ, "; ".join(rng), ch or "-", meaning, loader


def infer(values):
    kinds = set()
    for v in values:
        if v.lower() in ("true", "false", "on", "off", "yes", "no"):
            kinds.add("bool")
        elif re.fullmatch(r"-?\d+", v):
            kinds.add("integer")
        elif re.fullmatch(r"-?\d*\.\d+|-?\d+\.\d*", v):
            kinds.add("number")
        elif "," in v:
            kinds.add("list")
        else:
            kinds.add("string")
    if kinds <= {"integer", "number"} and kinds:
        return "number" if "number" in kinds else "integer"
    return "/".join(sorted(kinds)) or "string"


def values_text(values, total):
    distinct = []
    for v in values:
        if v not in distinct:
            distinct.append(v)
    if len(distinct) == 1:
        return code(distinct[0]) + ("" if total == 1 else " (all %d)" % len(values) if len(values) == total else " (%d of %d)" % (len(values), total))
    head = ", ".join(code(v, 30) for v in distinct[:3])
    return "%d values: %s%s" % (len(distinct), head, ", ..." if len(distinct) > 3 else "")


GROUP = [   # sections the loaders read by pattern: one table per pattern instead of one per section
    ("monsters.ltx", r"creature_.+", "creature_<id>"),
    ("squads.ltx", r".+", "<squad id>"),
    ("props.ltx", r"spawn_.+", "spawn_<respawn entry>"),
    ("props.ltx", r"kw_.+", "kw_<keyword overlay>"),
    ("props.ltx", r"p_.+", "p_<preset>"),
    ("props.ltx", r"loc_.+", "loc_<location FormID>"),
    ("standing.ltx", r"rule\..+", "rule.<name>"),
    ("compat.ltx", r"compat_(?!factions$).+", "compat_<mod id>"),
    ("stashes.ltx", r"kind\..+", "kind.<loot kind>"),
    ("tasks.ltx", r"fetch_(?!lists$).+", "fetch_<list>"),
    ("camp.ltx", r"slots_.+", "slots_<place kind>"),
    ("camp.ltx", r"stories_.+", "stories_<faction>"),
]
COLLAPSE_AT = 20   # an id table (every key read as an entry, none by name) longer than this is summarised in one row

HEAD = "| Key | Type | Default (config/) | Range / allowed | Channel | Meaning | Loader |\n|---|---|---|---|---|---|---|"
GHEAD = "| Key | Type | Values in config/ | Range / allowed | Channel | Meaning | Loader |\n|---|---|---|---|---|---|---|"


SCAN = None


def render(cfg_dir, files=None):
    global CONST, SCAN
    CONST = constants()
    scan = Scanner(files)
    SCAN = scan
    names = sorted(f for f in os.listdir(cfg_dir) if f.endswith(".ltx"))
    ltx = {n: LtxFile(os.path.join(cfg_dir, n)) for n in names}
    out = []
    w = out.append
    w("# Config key reference")
    w("")
    w("Generated by `python3 tools/gen_config_docs.py config > docs/CONFIG.md` (ticket G4) from `config/*.ltx` and the")
    w("loaders in `src/`. Do not edit by hand: change the ltx comment or the loader and regenerate;")
    w("`tools/tests/test_gen_config_docs.py` fails when this page is out of date.")
    w("")
    w("Columns: **Default** is the value `config/` ships (the loader's compiled-in default is used only when the key is")
    w("missing). **Range / allowed** is what the loader accepts: a clamp it applies (`[lo, hi]`, `>= lo`), the literals it")
    w("compares a string with, or the parse rule. **Channel** is the T21 difficulty tag of the ltx comment (`{lethality+}`:")
    w("value x M, `{economy-}`: value / M, see `docs/DIFFICULTY.md`). **Loader** is the `src/` line that reads the key.")
    w("Grouped sections (one table for many sections of the same shape) show the values found across them.")
    w("")
    w("Files: " + ", ".join("[%s](#%s)" % (n, re.sub(r"[^a-z0-9_-]", "", n.lower().replace(" ", "-"))) for n in names))
    w("")
    stats = {"files": 0, "sections": 0, "keys": 0, "unread": [], "loader_only": []}
    for n in names:
        f = ltx[n]
        reads = [r for r in scan.reads if r.ltx == n]
        stats["files"] += 1
        w("## %s" % n)
        w("")
        loads = scan.loads.get(n, [])
        w("Loaded by " + (", ".join("`%s`" % c for c in loads) if loads else "**no LoadFile in src/**") + ".")
        w("")
        if f.intro:
            for line in f.intro:
                w(("> " + esc(line)).rstrip())
            w("")
        grouped, done = {}, set()
        for sname in f.order:
            for (gf, pat, label) in GROUP:
                if gf == n and re.fullmatch(pat, sname):
                    grouped.setdefault(label, []).append(sname)
                    done.add(sname)
                    break
        emitted_groups = set()
        for sname in f.order:
            if sname in done:
                label = next(lb for lb, ss in grouped.items() if sname in ss)
                if label in emitted_groups:
                    continue
                emitted_groups.add(label)
                render_group(w, scan, f, n, label, grouped[label], reads, stats)
            else:
                render_section(w, scan, f, n, f.sections[sname], reads, stats)
        # keys the loader reads that config/ does not set
        present = set()
        for s in f.sections.values():
            for k in s.keys:
                present.add((s.name, k.name))
        only = {}
        for r in reads:
            if r.fn == "Section" or r.key is None:
                continue
            if r.sec is not None:
                hit = (r.sec, r.key) in present
            else:
                hit = any(matches(r.sec_pats, s) and k == r.key for (s, k) in present)
            if not hit:
                label = r.sec if r.sec is not None else ("any (`%s`)" % r.sec_expr if "*" in r.sec_pats else ", ".join(r.sec_pats))
                only.setdefault((label, r.key), []).append(r)
        if only:
            w("### Read by the loader, not set in config/")
            w("")
            w("The compiled-in default applies.")
            w("")
            w("| Section | Key | Type | Loader default | Range / allowed | Loader |\n|---|---|---|---|---|---|")
            for (sec, key), rs in sorted(only.items()):
                typ, rng, _, meaning, loader = describe(scan, rs, 0, [], "", None, "string")
                d = next((r.default for r in rs if r.default), None)
                w("| %s | `%s` | %s | %s | %s | %s |" % (sec if sec.startswith("any") else "`%s`" % esc(sec), key, typ, code(num(d) if d else "-") if d else "-", rng, loader))
                stats["loader_only"].append("%s [%s] %s" % (n, sec, key))
            w("")
    unmapped = [r for r in scan.reads if r.ltx is None]
    if unmapped:
        w("## Reads the generator could not trace to a file")
        w("")
        w("| Object | Section | Key | Loader |\n|---|---|---|---|")
        for r in unmapped:
            w("| `%s` | %s | %s | `%s` |" % (r.obj, code(r.sec_expr or "-"), code(r.key_expr or "-"), r.cite))
        w("")
    w("## Summary")
    w("")
    w("- %d files, %d sections, %d keys in `config/`; %d key reads on `Ltx` objects in `src/`." % (
        stats["files"], stats["sections"], stats["keys"], len(scan.reads)))
    w("- Keys in `config/` that no line in `src/` reads (by name or as a section entry): %d%s" % (
        len(stats["unread"]), "." if not stats["unread"] else ":"))
    for u in stats["unread"]:
        w("  - " + esc(u))
    w("- Keys the loaders read that `config/` does not set (compiled-in default): %d." % len(stats["loader_only"]))
    w("")
    return "\n".join(out)


def render_section(w, scan, f, n, sec, reads, stats):
    stats["sections"] += 1
    head = "### [%s]" % sec.name
    if sec.parents:
        head += " : " + ", ".join(sec.parents)
    w(head)
    w("")
    note = " ".join(x for x in sec.above if x) + ((" " + sec.header_comment) if sec.header_comment else "")
    if note.strip():
        w(one_line(note, 600))
        w("")
    if not sec.keys:
        rs = [r for r in reads if r.fn == "Section" and r.sec == sec.name]
        txt = "(no keys"
        if sec.parents:
            txt += ": inherits every value from %s" % ", ".join("[%s]" % p for p in sec.parents)
        if rs:
            txt += "; the loader reads every entry here: %s" % cites(rs)
        w(txt + ")")
        w("")
        return
    tag = section_channel(sec)
    picks = [(k,) + pick(reads, sec.name, k.name) for k in sec.keys]
    named = [p for p in picks if p[1] is not None and p[1] <= 2 and not all(r.keyset == ["*"] for r in p[2])]
    if len(sec.keys) > COLLAPSE_AT and not named and all(FORMID.match(k.name) for k in sec.keys):
        stats["keys"] += len(sec.keys)
        rs = [r for p in picks for r in p[2]]
        vals = [k.value for k in sec.keys]
        allhex = all(FORMID.match(k.name) for k in sec.keys)
        typ, rng, ch, meaning, loader = describe(scan, rs, picks[0][1], vals, sec.keys[0].comment if False else "", tag, infer(vals))
        label = "<FormID hex>" if allhex else "<entry>"
        w("%d entries, keys %s ... %s." % (len(sec.keys), code(sec.keys[0].name), code(sec.keys[-1].name)))
        w("")
        w(GHEAD)
        w("| `%s` | %s | %s | %s | %s | %s | %s |" % (label, typ, values_text(vals, len(vals)), rng, ch,
                                                   one_line(note or "-") if note else "-", loader))
        w("")
        if not rs:
            stats["unread"].append("%s [%s]: all %d entries" % (n, sec.name, len(sec.keys)))
        return
    w(HEAD)
    for k, tier, rs in picks:
        stats["keys"] += 1
        typ, rng, ch, meaning, loader = describe(scan, rs, tier, [k.value], k.comment, tag, infer([k.value]),
                                                 f.gloss.get(k.name, ""))
        if not rs:
            stats["unread"].append("%s [%s] %s (line %d)" % (n, sec.name, k.name, k.line))
        w("| `%s` | %s | %s | %s | %s | %s | %s |" % (esc(k.name), typ, code(k.value), rng, ch, one_line(meaning), loader))
    w("")


def render_group(w, scan, f, n, label, snames, reads, stats):
    stats["sections"] += len(snames)
    w("### [%s] (%d sections)" % (label, len(snames)))
    w("")
    w("Sections: " + ", ".join("`%s`" % s for s in snames) + ".")
    w("")
    first = f.sections[snames[0]]
    note = " ".join(x for x in first.above if x)
    if note.strip():
        w(one_line(note, 600))
        w("")
    rows = {}
    order = []
    for sname in snames:
        sec = f.sections[sname]
        for k in sec.keys:
            stats["keys"] += 1
            if k.name not in rows:
                rows[k.name] = Row(k.name)
                order.append(k.name)
            r = rows[k.name]
            r.values.append(k.value)
            r.sections.append(sname)
            r.lines.append(k.line)
            if k.comment:
                r.comments.append(k.comment)
    if not order:
        w("(no keys of their own: every section inherits its values)")
        w("")
        return
    w(GHEAD)
    tag = section_channel(first)
    for key in order:
        r = rows[key]
        tier, rs = pick(reads, r.sections[0], key)
        typ, rng, ch, meaning, loader = describe(scan, rs, tier, r.values, r.comments[0] if r.comments else "", tag,
                                                 infer(r.values), f.gloss.get(key, ""))
        if not rs:
            stats["unread"].append("%s [%s] %s (%d sections, first line %d)" % (n, label, key, len(r.sections), r.lines[0]))
        w("| `%s` | %s | %s | %s | %s | %s | %s |" % (esc(key), typ, values_text(r.values, len(snames)), rng, ch,
                                                    one_line(meaning), loader))
    w("")


def main(argv):
    if len(argv) != 2 or not os.path.isdir(argv[1]):
        sys.stderr.write("usage: python3 tools/gen_config_docs.py <config dir>\n")
        return 2
    sys.stdout.write(render(argv[1]) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
