#!/usr/bin/env python3
"""One reader for the .ltx config dialect, shared by the tools (ticket G7). Python 3 standard library only.

It follows src/core/Ltx.cpp (and the Trim / SplitList helpers of include/ALife/Util.h) line by line; every rule cites the
C++ line it copies. tools/check_config.py (G2) and tools/gen_config_docs.py (G4) import it, so a parser fix lands once.

    x = ltxlib.Ltx("config/alife.ltx")      # file plus its #include files, like Ltx::LoadFile
    x.Get("sim", "online_actors")           # resolved with inheritance, like Ltx::Get

What it copies from Ltx.cpp:
  - UTF-8 BOM: three bytes cut from the first line when it starts with 0xEF (Ltx.cpp:48)
  - comments: ';' or '//' outside double quotes, to the end of the line (Ltx.cpp:10-20)
  - lines are trimmed (space, tab, CR, LF), empty ones skipped (Ltx.cpp:50-51)
  - '#include "file"' loads file relative to the including file's folder, into the same object; other '#' lines are
    ignored, a missing file is only an error message (Ltx.cpp:52-62, 22-26)
  - '[name]' opens a section; a repeated [name] reopens the first and appends to it, keeping its place in the order;
    '[name]:a,b' sets (replaces) the parents, a header without ':' keeps them (Ltx.cpp:63-77)
  - a header without ']' is skipped and its keys land in the previous section (Ltx.cpp:64-68)
  - keys before the first section, or under an empty '[]' header, are dropped (Ltx.cpp:79)
  - 'key = value', 'key' alone is an empty value, a value wrapped in double quotes loses them (Ltx.cpp:80-84)
  - resolution: parents first in listed order, then own lines; a key already present is replaced in place, a new key
    is appended; a section already on the stack (cycle) or missing is skipped silently (Ltx.cpp:90-104)

Two things the C++ does not do, both only for the tools' sake: an #include cycle is reported and cut (the C++ would
recurse until its stack overflows), and `follow_includes=False` records #include lines without loading them (the
docs generator documents each file's own text).

Subclasses get the line events through the on_* hooks (no-ops here, except that errors the C++ would write to its
error string are collected in `self.errors`). The hooks never change what is stored.
"""
import os

# ------------------------------------------------------------------------------------------------ include/ALife/Util.h


def trim(s):
    return s.strip(" \t\r\n")   # include/ALife/Util.h:46-52 Trim: space, tab, CR, LF only


def split_list(s, sep=","):
    return [x for x in (trim(p) for p in s.split(sep)) if x]   # include/ALife/Util.h:54-65: items trimmed, empty dropped


# ------------------------------------------------------------------------------------------------ src/core/Ltx.cpp helpers


def split_comment(line):
    """(code, comment) the way StripComment cuts a line: comment is the text after ';' or '//', None when there is none."""
    q = False
    for i, c in enumerate(line):
        if c == '"':
            q = not q                                                       # src/core/Ltx.cpp:15
        elif c == ";" and not q:
            return line[:i], line[i + 1:]                                   # src/core/Ltx.cpp:16
        elif c == "/" and not q and line[i + 1:i + 2] == "/":
            return line[:i], line[i + 2:]                                   # src/core/Ltx.cpp:17
    return line, None                                                       # src/core/Ltx.cpp:19


def strip_comment(line):
    return split_comment(line)[0]                                           # src/core/Ltx.cpp:10-20


def dir_of(path):
    p = max(path.rfind("/"), path.rfind("\\"))                              # src/core/Ltx.cpp:24
    return "" if p < 0 else path[:p + 1]                                    # src/core/Ltx.cpp:25


# ------------------------------------------------------------------------------------------------ the model


class Line:
    """One key line: key, value, where it is (path, line number) and its trimmed comment ("" when none)."""
    __slots__ = ("key", "val", "path", "line", "comment")

    def __init__(self, key, val, path, line, comment=""):
        self.key, self.val, self.path, self.line, self.comment = key, val, path, line, comment

    name = property(lambda self: self.key)      # the generator's names for the same fields
    value = property(lambda self: self.val)

    def __repr__(self):
        return "Line(%r, %r, %s:%d)" % (self.key, self.val, self.path, self.line)


class Section:
    """Ltx.cpp's Raw (include/ALife/Ltx.h:31-35): parents plus own lines in file order, and where it was first opened."""

    def __init__(self, name, path, line):
        self.name, self.path, self.line = name, path, line
        self.parents, self.parents_at = [], (path, line)   # parents_at: the header that last set the parents
        self.lines = []                                    # own lines only, file order, repeats kept (include/ALife/Ltx.h:34)
        self.first = {}                                    # key -> (path, line) of its first own line


class Ltx:
    """One Ltx object of the loader: a file plus the files it #includes, or a string (Ltx::LoadString)."""

    def __init__(self, path=None, text=None, follow_includes=True):
        self.path = path
        self.sections, self.order, self.includes, self.errors = {}, [], [], []
        self.follow_includes = follow_includes
        self._cache, self._open = {}, []
        if text is not None:
            self.load_string(text, dir_of(path or ""), path or "<string>")
        elif path is not None:
            self.load_file(path)

    # ---- loading (src/core/Ltx.cpp:29-88)
    def load_file(self, path, inc_at=None):
        key = os.path.normcase(os.path.abspath(path))
        if key in self._open:   # not in the C++: it would recurse without end
            self.on_include_cycle(path, inc_at)
            return False
        try:
            with open(path, "rb") as f:                                     # src/core/Ltx.cpp:31
                data = f.read()
        except OSError as e:
            self.on_open_error(path, inc_at, e)                             # src/core/Ltx.cpp:32-35
            return False
        self.on_file(path)
        self._open.append(key)
        try:
            return self.load_string(data, dir_of(path), path)               # src/core/Ltx.cpp:38
        finally:
            self._open.pop()

    def load_string(self, text, dir_="", path="<string>"):
        data = text.encode("utf-8") if isinstance(text, str) else text
        cur = ""                                                            # src/core/Ltx.cpp:43
        for no, braw in enumerate(data.split(b"\n"), 1):                    # std::getline: '\n' only (src/core/Ltx.cpp:47)
            if no == 1 and len(braw) >= 3 and braw[0] == 0xEF:
                braw = braw[3:]                                             # src/core/Ltx.cpp:48 UTF-8 BOM
            raw = braw.decode("utf-8", "replace")
            code, com = split_comment(raw)
            com = trim(com) if com is not None else None
            line = trim(code)                                               # src/core/Ltx.cpp:50
            if not line:                                                    # src/core/Ltx.cpp:51
                self.on_line(path, no, raw, "blank", com, None)
                continue
            if line[0] == "#":                                              # src/core/Ltx.cpp:52
                self.on_line(path, no, raw, "directive", com, None)
                if line.startswith("#include"):                             # src/core/Ltx.cpp:53
                    q1, q2 = line.find('"'), line.rfind('"')                # src/core/Ltx.cpp:54-55
                    if q1 != -1 and q2 > q1:                                # src/core/Ltx.cpp:56
                        name = line[q1 + 1:q2]
                        self.includes.append((name, no))
                        if self.follow_includes:
                            self.load_file(dir_ + name, (path, no))         # src/core/Ltx.cpp:58
                    else:
                        self.on_include_unquoted(path, no, line)
                continue                                                    # src/core/Ltx.cpp:61
            if line[0] == "[":                                              # src/core/Ltx.cpp:63
                close = line.find("]")                                      # src/core/Ltx.cpp:64
                if close == -1:                                             # src/core/Ltx.cpp:65-68
                    self.on_line(path, no, raw, "badheader", com, None)
                    self.on_bad_header(path, no, line)
                    continue
                cur = trim(line[1:close])                                   # src/core/Ltx.cpp:69
                reopened = cur in self.sections
                if not reopened:                                            # src/core/Ltx.cpp:70-71
                    self.sections[cur] = Section(cur, path, no)
                    self.order.append(cur)
                s = self.sections[cur]
                if cur:
                    self.on_section(s, path, no, reopened)
                else:   # src/core/Ltx.cpp:70 makes the section, src/core/Ltx.cpp:79 drops every key under it
                    self.on_empty_section(path, no)
                rest = trim(line[close + 1:])                               # src/core/Ltx.cpp:72
                if rest and rest[0] == ":":                                 # src/core/Ltx.cpp:73
                    s.parents, s.parents_at = split_list(rest[1:]), (path, no)   # src/core/Ltx.cpp:74-75
                elif rest and cur:
                    self.on_header_text(path, no, rest)                     # ignored by src/core/Ltx.cpp:73
                self.on_line(path, no, raw, "header", com, (s, reopened))
                continue                                                    # src/core/Ltx.cpp:77
            if not cur:                                                     # src/core/Ltx.cpp:79
                self.on_line(path, no, raw, "orphan", com, None)
                self.on_orphan(path, no, line)
                continue
            eq = line.find("=")                                             # src/core/Ltx.cpp:80
            key = line if eq == -1 else trim(line[:eq])                     # src/core/Ltx.cpp:81
            val = "" if eq == -1 else trim(line[eq + 1:])                   # src/core/Ltx.cpp:82
            if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
                val = val[1:-1]                                             # src/core/Ltx.cpp:83
            s = self.sections[cur]
            ln = Line(key, val, path, no, com or "")
            first = s.first.get(key)
            if key:
                self.on_key(s, ln, first)
            else:
                self.on_empty_key(s, ln)
            if first is None:
                s.first[key] = (path, no)
            s.lines.append(ln)                                              # src/core/Ltx.cpp:84
            self.on_line(path, no, raw, "key", com, ln)
        self._cache.clear()                                                 # src/core/Ltx.cpp:86
        return True                                                         # src/core/Ltx.cpp:87

    # ---- hooks (subclasses override; the defaults keep what Ltx.cpp writes to its error string)
    def on_file(self, path):
        pass

    def on_open_error(self, path, inc_at, err):
        self.errors.append("cannot open " + path)                          # src/core/Ltx.cpp:33, 58

    def on_include_cycle(self, path, inc_at):
        self.errors.append("#include cycle: %s" % path)

    def on_include_unquoted(self, path, no, line):
        pass                                                                # src/core/Ltx.cpp:56: silently ignored

    def on_bad_header(self, path, no, line):
        self.errors.append("bad section header: " + line)                  # src/core/Ltx.cpp:66

    def on_section(self, sec, path, no, reopened):
        pass

    def on_empty_section(self, path, no):
        pass

    def on_header_text(self, path, no, rest):
        pass

    def on_orphan(self, path, no, line):
        pass

    def on_key(self, sec, ln, first):
        pass

    def on_empty_key(self, sec, ln):
        pass

    def on_line(self, path, no, raw, kind, comment, item):
        """Every physical line: kind is blank / directive / badheader / header / orphan / key; comment is the trimmed
        comment text or None; item is the Line (key) or (Section, reopened) (header)."""

    # ---- resolution (src/core/Ltx.cpp:90-131)
    def _resolve(self, name, stack, out):
        if name in stack:                                                   # src/core/Ltx.cpp:92 inheritance cycle
            return
        s = self.sections.get(name)
        if s is None:                                                       # src/core/Ltx.cpp:93-94 missing parent
            return
        stack.append(name)                                                  # src/core/Ltx.cpp:95
        for p in s.parents:
            self._resolve(p, stack, out)                                    # src/core/Ltx.cpp:96
        for ln in s.lines:                                                  # src/core/Ltx.cpp:97-102
            for i, ex in enumerate(out):
                if ex.key == ln.key:
                    out[i] = ln                                             # src/core/Ltx.cpp:100 replaced in place
                    break
            else:
                out.append(ln)                                              # src/core/Ltx.cpp:101
        stack.pop()                                                         # src/core/Ltx.cpp:103

    def section(self, name):
        """Resolved Line list, or None when the section does not exist (Ltx::Section, src/core/Ltx.cpp:106-115)."""
        if name not in self.sections:                                       # src/core/Ltx.cpp:110
            return None
        if name not in self._cache:                                         # src/core/Ltx.cpp:108-109
            out = []
            self._resolve(name, [], out)                                    # src/core/Ltx.cpp:111-113
            self._cache[name] = out
        return self._cache[name]

    def line(self, sec, key):
        """The resolved Line of sec/key, or None (the line Ltx::Get would read, src/core/Ltx.cpp:127-130)."""
        for ln in self.section(sec) or []:
            if ln.key == key:
                return ln
        return None

    def own(self, name):
        s = self.sections.get(name)
        return s.lines if s else []

    def cycle(self, name, stack=()):
        """The inheritance cycle reached from name as a list of names ending where it closes, or None. Diagnostic only:
        resolution itself stops silently at src/core/Ltx.cpp:92."""
        stack = list(stack)
        if name in stack:
            return stack[stack.index(name):] + [name]
        s = self.sections.get(name)
        if not s:
            return None
        for p in s.parents:
            c = self.cycle(p, stack + [name])
            if c:
                return c
        return None

    # ---- the C++ API, same names (include/ALife/Ltx.h:16-28)
    def Has(self, sec, key=None):
        if key is None:
            return sec in self.sections                                     # include/ALife/Ltx.h:19
        return self.line(sec, key) is not None                              # src/core/Ltx.cpp:117-123

    def Section(self, sec):
        return self.section(sec)

    def Sections(self):
        return list(self.order)                                             # include/ALife/Ltx.h:22

    def Get(self, sec, key, default=""):
        ln = self.line(sec, key)                                            # src/core/Ltx.cpp:125-131
        return default if ln is None else ln.val

    def GetB(self, sec, key, default=False):
        v = self.Get(sec, key)                                              # src/core/Ltx.cpp:147-152
        if not v:
            return default
        return v in ("true", "on", "yes", "1")

    def List(self, sec, key):
        return split_list(self.Get(sec, key))                               # src/core/Ltx.cpp:154-157
