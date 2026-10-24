"""Tests for tools/check_config.py (ticket G2). Python 3 standard library only.

    python3 -m unittest tools/tests/test_check_config.py -v

Fixtures: tools/tests/fixtures/config_bad/<case>__<file>.ltx replaces <file>.ltx in a copy of the repo's config/. Every line
marked "; EXPECT <rule>" must be reported as "<path>:<line>: [<rule>]", and nothing else may be reported.
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "check_config.py")
CONFIG = os.path.join(ROOT, "config")
FIXTURES = os.path.join(HERE, "fixtures", "config_bad")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_config as cc  # noqa: E402

ERR_RE = re.compile(r"^(.*\.ltx):(\d+): \[([\w.]+)\] ")
# the six kinds the ticket asks for, each must have a failing fixture
TICKET_KINDS = {"prob_1_5__wounded.ltx": "num.prob", "missing_section__compat.ltx": "ref.section",
                "negative_weight__races.ltx": "num.weight", "duplicate_key__companions.ltx": "dup.key",
                "unknown_channel__difficulty.ltx": "ref.channel", "unknown_race__races.ltx": "ref.race"}


def run_tool(cfg_dir):
    p = subprocess.run([sys.executable, TOOL, cfg_dir], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, cwd=ROOT)
    return p.returncode, p.stdout


def expected(path):
    out = set()
    with open(path, encoding="utf-8") as f:
        for no, line in enumerate(f, 1):
            m = re.search(r"EXPECT ([\w.]+)", line)
            if m:
                out.add((no, m.group(1)))
    return out


class MasterConfig(unittest.TestCase):
    def test_master_config_ok(self):
        code, out = run_tool(CONFIG)
        self.assertEqual(code, 0, out)
        self.assertRegex(out.strip().splitlines()[-1], r"^OK: \d+ files, \d+ checks$")
        self.assertNotIn("WARN", out)


class Fixtures(unittest.TestCase):
    def test_every_ticket_kind_has_a_fixture(self):
        for name, rule in TICKET_KINDS.items():
            self.assertIn((rule), {r for _, r in expected(os.path.join(FIXTURES, name))}, name)

    def test_fixture_count(self):
        self.assertGreaterEqual(len(FIXTURE_NAMES), len(TICKET_KINDS))

    def check_fixture(self, name):
        target = name.split("__", 1)[1]
        tmp = tempfile.mkdtemp(prefix="g2-")
        try:
            cfg = os.path.join(tmp, "config")
            shutil.copytree(CONFIG, cfg)
            shutil.copy(os.path.join(FIXTURES, name), os.path.join(cfg, target))
            code, out = run_tool(cfg)
            self.assertEqual(code, 1, out)
            want = expected(os.path.join(FIXTURES, name))
            self.assertTrue(want, "fixture has no EXPECT marker")
            got = set()
            for line in out.splitlines():
                m = ERR_RE.match(line)
                if m:
                    self.assertEqual(m.group(1), os.path.join(cfg, target), line)
                    got.add((int(m.group(2)), m.group(3)))
            self.assertEqual(got, want, out)
            self.assertRegex(out.strip().splitlines()[-1], r"^FAIL: \d+ errors in \d+ files, \d+ checks$")
        finally:
            shutil.rmtree(tmp)


FIXTURE_NAMES = sorted(n for n in os.listdir(FIXTURES) if n.endswith(".ltx"))
for _n in FIXTURE_NAMES:   # one test per fixture: test_fixture_<case>__<file>
    setattr(Fixtures, "test_fixture_" + _n[:-4], lambda self, n=_n: self.check_fixture(n))


class LtxParser(unittest.TestCase):
    """The parser follows src/core/Ltx.cpp."""

    def load(self, text, extra=None):
        tmp = tempfile.mkdtemp(prefix="g2-ltx-")
        self.addCleanup(shutil.rmtree, tmp)
        for n, t in dict(extra or {}, **{"a.ltx": text}).items():
            with open(os.path.join(tmp, n), "wb") as f:
                f.write(t.encode("utf-8") if isinstance(t, str) else t)
        rep = cc.Report()
        return cc.Ltx(rep, os.path.join(tmp, "a.ltx")), rep

    def test_comments_quotes_bom(self):
        x, rep = self.load(b"\xef\xbb\xbf[s]\na = 1 ; c\nb = \"x;y\"\nc = 2 // c\nd\n")
        self.assertEqual([(l.key, l.val) for l in x.sec("s")], [("a", "1"), ("b", "x;y"), ("c", "2"), ("d", "")])
        self.assertEqual(rep.errors, [])

    def test_inheritance_override_in_place(self):
        x, rep = self.load("[p]\na = 1\nb = 2\n[c]:p\nb = 3\nd = 4\n")
        self.assertEqual([(l.key, l.val) for l in x.sec("c")], [("a", "1"), ("b", "3"), ("d", "4")])
        self.assertEqual(rep.errors, [])

    def test_include(self):
        x, rep = self.load('#include "b.ltx"\n[s]\na = 1\n', {"b.ltx": "[t]\nb = 2\n"})
        self.assertEqual(x.get("t", "b").val, "2")
        self.assertEqual(len(rep.files), 2)

    def test_cycle_and_missing_parent(self):
        x, rep = self.load("[a]:b\n[b]:a\n[c]:zz\n")
        self.assertEqual(sorted(e[2] for e in rep.errors), ["ltx.cycle", "ltx.cycle", "ref.parent"])
        self.assertEqual(x.sec("a"), [])


class Citations(unittest.TestCase):
    """Every file:line the checker cites exists; every SPEC row's line names its key (or loops over the id table)."""

    def test_cited_lines_exist(self):
        with open(TOOL, encoding="utf-8") as f:
            text = f.read()
        cites = re.findall(r"((?:src|include)/[\w/]+\.(?:cpp|h)):(\d+)(?:-(\d+))?", text)
        self.assertGreater(len(cites), 100)
        for path, a, b in cites:
            with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                n = len(f.read().split("\n"))
            for v in (a, b):
                if v:
                    self.assertLessEqual(int(v), n, "%s:%s" % (path, v))

    def test_spec_rows_name_their_key(self):
        loops = ("kJobIds[j]", "tiers[i]", "FactionId(Faction(f))", "\"activity_factions\", key", "std::to_string(ti + 1)")
        for fname, secs in cc.SPEC.items():
            for sec, rows in secs.items():
                for key, _, _, cite in rows:
                    path, line = cite.rsplit(":", 1)
                    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                        src = f.read().split("\n")[int(line) - 1]
                    with self.subTest(file=fname, section=sec, key=key):
                        self.assertTrue('"%s"' % key in src or any(l in src for l in loops), "%s: %s" % (cite, src.strip()))


class TablesMatchSource(unittest.TestCase):
    """The id tables copied into the checker are the C++ ones."""

    def ids(self, path, start, stop=";"):
        with open(os.path.join(ROOT, path), encoding="utf-8") as f:
            text = f.read()
        i = text.index(start)
        return re.findall(r'"([^"]+)"', text[i:text.index(stop, i)])

    def test_tables(self):
        self.assertEqual(cc.FACTIONS, self.ids("src/core/Core.cpp", "kFac[] = {", "};")[0::2])
        self.assertEqual(cc.RACES, self.ids("src/core/Core.cpp", "kRaceIds[] ="))
        self.assertEqual(cc.KINDS, self.ids("src/core/Core.cpp", "kKindNames[] ="))
        self.assertEqual(cc.CHANNELS, self.ids("src/core/Difficulty.cpp", "kIds[kDiffChannels] ="))
        self.assertEqual(cc.DIFF_ROWS, self.ids("src/core/Difficulty.cpp", "kRows[kGameDifficulties] ="))
        self.assertEqual(cc.TASK_TYPES, self.ids("src/core/Tasks.cpp", "kTypeIds[kTaskTypes] =")[1:])
        self.assertEqual(cc.CAMP_JOBS, self.ids("src/core/Camp.cpp", "kJobIds[kCampJobs] =")[1:])
        self.assertEqual(cc.ROLES, self.ids("src/core/Config.cpp", "roleNames[] ="))
        self.assertEqual(cc.PLAYER_RACES, self.ids("src/core/Standing.cpp", "kNames[] ="))
        self.assertEqual(cc.PLAYER_FLAGS, self.ids("src/core/Standing.cpp", "kTable[] = {", "};"))
        self.assertEqual(cc.WORLDSPACES, self.ids("include/ALife/Core.h", "kWorldspaceIds[kWorldspaces] ="))
        self.assertEqual(len(cc.HUMAN), 18)


if __name__ == "__main__":
    unittest.main()
