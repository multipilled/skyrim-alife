"""Tests for tools/ltxlib.py, the LTX reader shared by the tools (ticket G7). Python 3 standard library only.

    python3 -m unittest tools/tests/test_ltxlib.py -v

Rules are checked one by one against small files, then (when a C++ compiler is on PATH) the real src/core/Ltx.cpp is
built into a tiny driver and both readers must give the same sections, resolved lines and error text on every
config/*.ltx and on a seeded batch of generated files.
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ltxlib  # noqa: E402
import check_config  # noqa: E402
import gen_config_docs  # noqa: E402


class Tmp(unittest.TestCase):
    def files(self, files):
        tmp = tempfile.mkdtemp(prefix="g7-ltx-")
        self.addCleanup(shutil.rmtree, tmp)
        for n, t in files.items():
            p = os.path.join(tmp, n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(t.encode("utf-8") if isinstance(t, str) else t)
        return tmp

    def load(self, text, extra=None, **kw):
        tmp = self.files(dict(extra or {}, **{"a.ltx": text}))
        return ltxlib.Ltx(os.path.join(tmp, "a.ltx"), **kw)

    @staticmethod
    def kv(x, sec):
        return [(l.key, l.val) for l in x.section(sec)]


class Lexing(Tmp):
    def test_bom(self):   # src/core/Ltx.cpp:48
        self.assertEqual(self.kv(self.load(b"\xef\xbb\xbf[s]\na = 1\n"), "s"), [("a", "1")])
        self.assertEqual(self.kv(ltxlib.Ltx(text="\ufeff[s]\na = 1\n"), "s"), [("a", "1")])
        x = self.load(b"[s]\n\xef\xbb\xbfa = 1\n")             # only the first line
        self.assertEqual(x.section("s")[0].key, "\ufeffa")
        x = self.load(b"\xef\n[s]\na = 1\n")                   # first line shorter than 3 bytes: nothing cut
        self.assertEqual((x.order, self.kv(x, "s")), (["s"], [("a", "1")]))

    def test_comments_outside_quotes(self):   # src/core/Ltx.cpp:10-20
        x = self.load('[s]\na = 1 ; c\nb = "x;y" ; c\nc = 2 // c\nd = "p//q"\ne = 3/4\nf = "u" ; "v;w"\n; k = 9\n// k = 9\n')
        self.assertEqual(self.kv(x, "s"), [("a", "1"), ("b", "x;y"), ("c", "2"), ("d", "p//q"), ("e", "3/4"), ("f", "u")])
        self.assertEqual(ltxlib.split_comment('k = "a;b" ; note'), ('k = "a;b" ', " note"))
        self.assertEqual(ltxlib.split_comment("k = 1 // note"), ("k = 1 ", " note"))
        self.assertEqual(ltxlib.split_comment("k = 1"), ("k = 1", None))
        self.assertEqual(ltxlib.split_comment('k = "1 ; 2'), ('k = "1 ; 2', None))   # unclosed quote hides the rest
        self.assertEqual([l.comment for l in x.section("s")][:3], ["c", "c", "c"])

    def test_quoted_values_and_bare_keys(self):   # src/core/Ltx.cpp:80-83
        x = self.load('[s]\na = "q"\nb = ""\nc = "open\nd = x"\ne\nf = g = h\n  i   =   j k  \r\n')
        self.assertEqual(self.kv(x, "s"), [("a", "q"), ("b", ""), ("c", '"open'), ("d", 'x"'), ("e", ""), ("f", "g = h"),
                                           ("i", "j k")])

    def test_trim_and_split_list(self):   # include/ALife/Util.h:46-65
        self.assertEqual(ltxlib.trim(" \t\r\nx y\n\r\t "), "x y")
        self.assertEqual(ltxlib.trim("\vx\f"), "\vx\f")           # only space, tab, CR, LF
        self.assertEqual(ltxlib.split_list(" a, ,b ,, c ,"), ["a", "b", "c"])
        self.assertEqual(ltxlib.split_list("a|b", "|"), ["a", "b"])

    def test_line_numbers_and_crlf(self):
        x = self.load("; c\r\n\r\n[s]\r\na = 1\r\n")
        self.assertEqual([(l.key, l.val, l.line) for l in x.own("s")], [("a", "1", 4)])
        self.assertEqual(x.sections["s"].line, 3)


class Sections(Tmp):
    def test_inheritance_override_in_place(self):   # src/core/Ltx.cpp:90-104
        x = self.load("[p]\na = 1\nb = 2\n[q]\nb = 20\nc = 30\n[c]:p, q\nd = 4\na = 9\n")
        self.assertEqual(self.kv(x, "c"), [("a", "9"), ("b", "20"), ("c", "30"), ("d", "4")])
        self.assertEqual(x.sections["c"].parents, ["p", "q"])

    def test_repeated_key_in_own_section_replaces_in_place(self):   # src/core/Ltx.cpp:84, 100
        x = self.load("[s]\na = 1\nb = 2\na = 3\n")
        self.assertEqual(self.kv(x, "s"), [("a", "3"), ("b", "2")])
        self.assertEqual(len(x.own("s")), 3)

    def test_cycles_and_missing_parents(self):   # src/core/Ltx.cpp:92-94
        x = self.load("[a]:b\nx = 1\n[b]:a\ny = 2\n[c]:zz, a\nz = 3\n[d]:d\nw = 4\n")
        self.assertEqual(self.kv(x, "a"), [("y", "2"), ("x", "1")])
        self.assertEqual(self.kv(x, "b"), [("x", "1"), ("y", "2")])
        self.assertEqual(self.kv(x, "c"), [("y", "2"), ("x", "1"), ("z", "3")])
        self.assertEqual(self.kv(x, "d"), [("w", "4")])
        self.assertEqual(x.cycle("a"), ["a", "b", "a"])
        self.assertEqual(x.cycle("d"), ["d", "d"])
        self.assertIsNone(x.cycle("zz"))
        self.assertIsNone(x.section("zz"))

    def test_repeated_sections(self):   # src/core/Ltx.cpp:69-76
        x = self.load("[p]\nk = p\n[q]\nk = q\n[s]:p\na = 1\n[t]\n[s]\nb = 2\n[s]:q\nc = 3\n")
        self.assertEqual(x.order, ["p", "q", "s", "t"])
        self.assertEqual(self.kv(x, "s"), [("k", "q"), ("a", "1"), ("b", "2"), ("c", "3")])
        self.assertEqual((x.sections["s"].line, x.sections["s"].parents_at[1]), (5, 10))
        x = self.load("[p]\nk = p\n[s]:p\na = 1\n[s]\nb = 2\n")   # a header without ':' keeps the parents
        self.assertEqual(self.kv(x, "s"), [("k", "p"), ("a", "1"), ("b", "2")])

    def test_bad_headers_orphans_and_empty_names(self):   # src/core/Ltx.cpp:64-68, 79
        x = self.load("k = 0\n[s]\na = 1\n[t\nb = 2\n[ u ] trailing\nc = 3\n[]\nd = 4\n[]:s\n")
        self.assertEqual(x.order, ["s", "u", ""])
        self.assertEqual(self.kv(x, "s"), [("a", "1"), ("b", "2")])
        self.assertEqual(self.kv(x, "u"), [("c", "3")])
        self.assertEqual(self.kv(x, ""), [("a", "1"), ("b", "2")])   # keys under [] are dropped, its parent [s] still counts
        self.assertEqual(x.errors, ["bad section header: [t"])

    def test_empty_key_is_kept(self):   # src/core/Ltx.cpp:81, 84
        x = self.load("[s]\n= 5\n")
        self.assertEqual(self.kv(x, "s"), [("", "5")])


class Includes(Tmp):
    def test_include_relative_to_including_file(self):   # src/core/Ltx.cpp:22-26, 52-62
        tmp = self.files({"a.ltx": '#include "sub/b.ltx"\n[s]:t\na = 1\n# other directive\n#includes nothing\n',
                          "sub/b.ltx": '#include "c.ltx"\n[t]\nb = 2\n', "sub/c.ltx": "[u]\nc = 3\n"})
        x = ltxlib.Ltx(os.path.join(tmp, "a.ltx"))
        self.assertEqual(x.order, ["u", "t", "s"])
        self.assertEqual(self.kv(x, "s"), [("b", "2"), ("a", "1")])
        self.assertEqual(x.errors, [])
        self.assertEqual(x.includes, [("sub/b.ltx", 1), ("c.ltx", 1)])

    def test_missing_include_keeps_loading(self):   # src/core/Ltx.cpp:58
        x = self.load('#include "nope.ltx"\n[s]\na = 1\n#include\n')
        self.assertEqual(self.kv(x, "s"), [("a", "1")])
        self.assertEqual(len(x.errors), 1)
        self.assertTrue(x.errors[0].startswith("cannot open ") and x.errors[0].endswith("nope.ltx"))

    def test_first_and_last_quote(self):   # src/core/Ltx.cpp:54-56
        x = self.load('#include "b.ltx" ; "c.ltx"\n#include "d.ltx" "e.ltx"\n', {"b.ltx": "[b]\n"}, follow_includes=False)
        self.assertEqual(x.includes, [("b.ltx", 1), ('d.ltx" "e.ltx', 2)])

    def test_include_cycle_is_cut(self):   # not in Ltx.cpp, which would recurse without end
        x = self.load('#include "b.ltx"\n[s]\na = 1\n', {"b.ltx": '#include "a.ltx"\n[t]\nb = 2\n'})
        self.assertEqual(x.order, ["t", "s"])
        self.assertEqual(len(x.errors), 1)
        self.assertIn("cycle", x.errors[0])

    def test_include_twice_reopens(self):
        x = self.load('#include "b.ltx"\n#include "b.ltx"\n', {"b.ltx": "[t]\nb = 2\n"})
        self.assertEqual((x.order, len(x.own("t")), self.kv(x, "t")), (["t"], 2, [("b", "2")]))

    def test_follow_includes_false(self):
        x = self.load('#include "b.ltx"\n[s]\n', {"b.ltx": "[t]\n"}, follow_includes=False)
        self.assertEqual((x.order, x.includes), (["s"], [("b.ltx", 1)]))


class Api(Tmp):
    def test_cpp_names(self):   # src/core/Ltx.cpp:117-157, include/ALife/Ltx.h:16-28
        x = self.load("[p]\nl = a, b ,,c\n[s]:p\nt = yes\nf = no\nq = \"on\"\ne =\n")
        self.assertEqual((x.Has("s"), x.Has("zz"), x.Has("s", "l"), x.Has("s", "zz"), x.Has("zz", "l")),
                         (True, False, True, False, False))
        self.assertEqual((x.Get("s", "l"), x.Get("s", "zz", "d"), x.Get("zz", "l", "d")), ("a, b ,,c", "d", "d"))
        self.assertEqual((x.GetB("s", "t"), x.GetB("s", "f", True), x.GetB("s", "q"), x.GetB("s", "e", True),
                          x.GetB("s", "zz", True)), (True, False, True, True, True))
        self.assertEqual(x.List("s", "l"), ["a", "b", "c"])
        self.assertEqual(x.Sections(), ["p", "s"])
        self.assertIs(x.Section("s"), x.section("s"))

    def test_cache_cleared_by_a_later_load(self):   # src/core/Ltx.cpp:86
        x = ltxlib.Ltx(text="[p]\na = 1\n[s]:p\n")
        self.assertEqual(x.Get("s", "a"), "1")
        x.load_string("[p]\na = 2\n")
        self.assertEqual(x.Get("s", "a"), "2")


class ToolsUseIt(unittest.TestCase):
    def test_both_tools_subclass_the_shared_reader(self):
        self.assertTrue(issubclass(check_config.Ltx, ltxlib.Ltx))
        self.assertTrue(issubclass(gen_config_docs.LtxFile, ltxlib.Ltx))
        self.assertIs(check_config.trim, ltxlib.trim)
        self.assertIs(check_config.split_list, ltxlib.split_list)
        for tool in ("check_config.py", "gen_config_docs.py"):
            with open(os.path.join(ROOT, "tools", tool), encoding="utf-8") as f:
                text = f.read()
            for own in ("def strip_comment", "def split_comment", "def trim", "def split_list", "def _resolve",
                        ".find(\"]\")", "startswith(\"#include\")"):
                self.assertNotIn(own, text, "%s still parses LTX itself (%s)" % (tool, own))

    def test_cited_lines(self):
        """Every file:line cited in ltxlib.py exists, and key rules point at the C++ line that does the same thing."""
        with open(os.path.join(ROOT, "tools", "ltxlib.py"), encoding="utf-8") as f:
            text = f.read()
        cites = re.findall(r"((?:src|include)/[\w/]+\.(?:cpp|h)):(\d+)(?:-(\d+))?", text)
        self.assertGreater(len(cites), 40)
        src = {}
        for path, a, b in cites:
            if path not in src:
                with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                    src[path] = f.read().split("\n")
            for v in (a, b):
                if v:
                    self.assertLessEqual(int(v), len(src[path]), "%s:%s" % (path, v))
        ltx = src["src/core/Ltx.cpp"]
        for line, token in ((15, "'\"'"), (16, "';'"), (17, "'/'"), (48, "0xEF"), (50, "StripComment"), (53, "#include"),
                            (58, "LoadFile"), (64, "']'"), (69, "cur ="), (70, "m_sections[cur]"), (73, "':'"),
                            (75, "SplitList"), (79, "cur.empty()"), (80, "'='"), (83, "'\"'"), (86, "m_resolved.clear"),
                            (92, "cycle"), (94, "return"), (96, "parents"), (100, "ex.second = kv.second"),
                            (101, "push_back"), (151, "\"true\"")):
            self.assertIn(token, ltx[line - 1], "src/core/Ltx.cpp:%d" % line)
        util = src["include/ALife/Util.h"]
        self.assertIn("Trim", util[45])
        self.assertIn("SplitList", util[53])
        hdr = src["include/ALife/Ltx.h"]
        self.assertIn("struct Raw", hdr[30])
        self.assertIn("lines;", hdr[33])


DRIVER = r"""
#include "ALife/Ltx.h"
#include <cstdio>
int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		ALife::Ltx x;
		std::string err;
		x.LoadFile(argv[i], &err);
		for (auto& s : x.Sections()) {
			std::printf("[%s]\n", s.c_str());
			if (auto* l = x.Section(s))
				for (auto& kv : *l) std::printf("%s=%s\n", kv.first.c_str(), kv.second.c_str());
		}
		std::printf("ERR %s\n@@\n", err.c_str());
	}
}
"""


def dump_py(path):
    x = ltxlib.Ltx(path)
    out = []
    for s in x.Sections():
        out.append("[%s]" % s)
        out += ["%s=%s" % (l.key, l.val) for l in x.Section(s)]
    out.append("ERR " + "".join(e + "\n" for e in x.errors))
    return "\n".join(out)


class AgainstCpp(Tmp):
    """Build src/core/Ltx.cpp and compare both readers (skipped without a C++ compiler)."""

    @classmethod
    def setUpClass(cls):
        cxx = shutil.which("g++") or shutil.which("clang++")
        if not cxx:
            raise unittest.SkipTest("no C++ compiler on PATH")
        cls.dir = tempfile.mkdtemp(prefix="g7-cpp-")
        with open(os.path.join(cls.dir, "drv.cpp"), "w") as f:
            f.write(DRIVER)
        cls.exe = os.path.join(cls.dir, "ltxdrv")
        subprocess.run([cxx, "-std=c++20", "-O1", "-I", os.path.join(ROOT, "include"), os.path.join(cls.dir, "drv.cpp"),
                        os.path.join(ROOT, "src", "core", "Ltx.cpp"), "-o", cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir)

    def compare(self, paths):
        out = subprocess.run([self.exe] + paths, capture_output=True, check=True).stdout.decode("utf-8")
        cpp = out.split("\n@@\n")[:-1]
        self.assertEqual(len(cpp), len(paths))
        for p, c in zip(paths, cpp):
            self.assertEqual(dump_py(p), c, p)
        return len(paths)

    def test_master_config(self):
        cfg = os.path.join(ROOT, "config")
        n = self.compare(sorted(os.path.join(cfg, f) for f in os.listdir(cfg) if f.endswith(".ltx")))
        print("\n  Ltx.cpp vs ltxlib: %d config/*.ltx files identical" % n, file=sys.stderr)

    PIECES = ["[a]", "[b]:a", "[c]:a, b", "[c]:zz", "[a]:c", "[]", "[ d ]", "[e", "[f] x", "k = 1", "k = 2", "m",
              "n = \"q\"", "n = \"x;y\" ; c", "o = 1 // c", "p = a//b", "= 3", "  q =  r s  ", "; note", "// note",
              "#include \"inc.ltx\"", "#include \"missing.ltx\"", "#include", "#x", "\"", "r = \"", "s = 1;2", "\t",
              "t = \"u\" ; \"v", "[g]:a,,b", "w=\"\"", "  ", "k=3\r"]

    def test_generated_files(self):
        rnd = random.Random(7)
        tmp = self.files({"inc.ltx": "[a]\nk = inc\ni = 1\n[h]:a\nh = 2\n"})
        paths = []
        for n in range(400):
            lines = [rnd.choice(self.PIECES) for _ in range(rnd.randint(0, 14))]
            data = "\n".join(lines).encode("utf-8")
            r = rnd.random()
            if r < 0.15:
                data = b"\xef\xbb\xbf" + data                     # BOM
            elif r < 0.2:
                data = rnd.choice([b"\xef\n", b"\xef[a]\n", b"\xef\xbb\n"]) + data   # 0xEF lead byte, short or not
            p = os.path.join(tmp, "f%03d.ltx" % n)
            with open(p, "wb") as f:
                f.write(data)
            paths.append(p)
        n = self.compare(paths)
        print("\n  Ltx.cpp vs ltxlib: %d generated files identical (seed 7)" % n, file=sys.stderr)


if __name__ == "__main__":
    unittest.main()
