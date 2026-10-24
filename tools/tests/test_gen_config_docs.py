#!/usr/bin/env python3
"""Tests for tools/gen_config_docs.py (ticket G4). Standard library only.

    python3 -m unittest tools/tests/test_gen_config_docs.py -v
"""
import collections
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen_config_docs as g  # noqa: E402

CONFIG = os.path.join(ROOT, "config")
PAGE = os.path.join(ROOT, "docs", "CONFIG.md")

# Key names read by Get(/Num(-style calls in src/core/*.cpp, found by grep (independent of the generator's scanner):
#   Ltx API         x.Get / GetF / GetI / GetB / List / Has (sec, "key" ...)   -> key is the 2nd argument
#   Num-style       any Num...( call with a "key" 2nd argument                  -> (none on master: ParseNum takes a value)
#   wrappers        f(sec, "key", v) lambdas in Wounds.cpp / Standing.cpp,
#                   fill(out, "key", {...}) in Config.cpp ([activity_factions]) -> 2nd argument
#                   Pair(t, sec, "key", ...), Ids(x, sec, "key", ...)           -> 3rd argument
#                   Five(t, "key", ...)                                         -> 2nd argument ([loot])
GREP = [
    ("Get/GetF/GetI/GetB/List/Has", re.compile(r'(?:\.|->)\s*(?:GetF|GetI|GetB|Get|List|Has)\s*\(\s*[^,()]+?\s*,\s*"([A-Za-z0-9_.]+)"\s*[,)]')),
    ("Num*", re.compile(r'\b\w*Num\w*\s*\(\s*[^,()]+?\s*,\s*"([A-Za-z0-9_.]+)"\s*[,)]')),
    ("f/fill lambdas", re.compile(r'(?<![\w.])(?:f|fill)\s*\(\s*[^,()]+?\s*,\s*"([A-Za-z0-9_.]+)"\s*[,)]')),
    ("Pair/Ids", re.compile(r'\b(?:Pair|Ids)\s*\(\s*\w+\s*,\s*[^,]+?\s*,\s*"([A-Za-z0-9_.]+)"\s*[,)]')),
    ("Five", re.compile(r'\bFive\s*(?:<[^>]*>)?\s*\(\s*\w+\s*,\s*"([A-Za-z0-9_.]+)"\s*[,)]')),
]


def core_key_reads():
    sites = collections.OrderedDict()
    per_kind = collections.Counter()
    core = os.path.join(ROOT, "src", "core")
    for name in sorted(os.listdir(core)):
        if not name.endswith(".cpp") or name == "Ltx.cpp":
            continue
        with open(os.path.join(core, name), encoding="utf-8", errors="replace") as fh:
            code, _ = g.strip_cpp_comments(fh.read())
        for kind, rx in GREP:
            for m in rx.finditer(code):
                line = code.count("\n", 0, m.start()) + 1
                sites.setdefault(m.group(1), []).append("src/core/%s:%d" % (name, line))
                per_kind[kind] += 1
    return sites, per_kind


class PageIsCurrent(unittest.TestCase):
    def test_committed_page_matches_generator(self):
        with open(PAGE, encoding="utf-8") as fh:
            committed = fh.read()
        self.assertEqual(g.render(CONFIG) + "\n", committed,
                         "docs/CONFIG.md is stale: python3 tools/gen_config_docs.py config > docs/CONFIG.md")

    def test_every_src_core_key_is_documented(self):
        with open(PAGE, encoding="utf-8") as fh:
            page = fh.read()
        documented = set(re.findall(r"^\| `([^`]+)` \|", page, re.M)) | set(re.findall(r"^\| [^|]+ \| `([^`]+)` \|", page, re.M))
        sites, per_kind = core_key_reads()
        missing = {k: v for k, v in sites.items() if k not in documented}
        n_sites = sum(len(v) for v in sites.values())
        print("\n  src/core/*.cpp key reads (grep): %d distinct keys at %d call sites (%s); %d missing from docs/CONFIG.md"
              % (len(sites), n_sites, ", ".join("%s %d" % (k, per_kind[k]) for k, _ in GREP), len(missing)))
        self.assertGreater(len(sites), 300)
        self.assertEqual(missing, {}, "keys read in src/core but not in a docs/CONFIG.md Key column")

    def test_cited_lines_exist(self):
        with open(PAGE, encoding="utf-8") as fh:
            page = fh.read()
        cites = set(re.findall(r"`(src/[\w/]+\.cpp):(\d+)`", page))
        self.assertGreater(len(cites), 300)
        for path, line in sorted(cites):
            with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
                n = fh.read().count("\n") + 1
            self.assertLessEqual(int(line), n, "%s:%s past the end of the file" % (path, line))


class Scanner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        g.CONST = g.constants()
        cls.scan = g.Scanner()
        g.SCAN = cls.scan

    def test_literal_reads_point_at_their_key(self):
        n = 0
        for r in self.scan.reads:
            if r.key is None:
                continue
            with open(os.path.join(ROOT, r.src), encoding="utf-8", errors="replace") as fh:
                line = fh.read().split("\n")[r.line - 1]
            self.assertIn('"%s"' % r.key, line, r.cite)
            n += 1
        self.assertGreater(n, 300)

    def test_objects_traced_to_files(self):
        unmapped = [r.cite for r in self.scan.reads if r.ltx is None]
        self.assertEqual(unmapped, [])
        want = {"alife.ltx", "factions.ltx", "props.ltx", "squads.ltx", "standing.ltx", "monsters.ltx", "compat.ltx",
                "difficulty.ltx", "races.ltx", "dragons.ltx", "dragon_places.ltx", "wounded.ltx", "stashes.ltx",
                "tasks.ltx", "companions.ltx", "camp.ltx", "camp_spots.ltx", "talk.ltx", "positions.ltx", "water.ltx"}
        self.assertEqual({r.ltx for r in self.scan.reads}, want)

    def test_parameter_and_helper_tracing(self):
        by = {(r.ltx, r.sec, r.key): r for r in self.scan.reads if r.key}
        self.assertEqual(by[("wounded.ltx", "wounded", "hp")].via, "f")                 # lambda f(W, "hp", c.hp)
        self.assertEqual(by[("wounded.ltx", "wounded", "down_hp")].via, "Pair")         # Pair(t, W, "down_hp", ...)
        self.assertEqual(by[("stashes.ltx", "loot", "tier_levels")].via, "Five")        # Five(t, "tier_levels", ...)
        self.assertEqual(by[("standing.ltx", "deeds", "kill_base")].src, "src/core/Standing.cpp")
        self.assertIn(("talk.ltx", "globals", "comp_offer"), by)                        # glob("comp_offer") lambda
        self.assertIn(("companions.ltx", "companions", "max_members"), by)              # LoadPartyConfig(cl, ...) caller

    def test_ranges_from_clamps(self):
        page = g.render(CONFIG)
        for row in ("| `max_members` | integer | `6` | [1, 8] |",                      # std::clamp inline (Party.cpp)
                    "| `hp` | number | `0.20` | [0, 1] | `{lethality-}` |",           # frac() in ClampWoundConfig
                    "| `call_seconds` | number | `15` | >= 1 |",                        # pos(v, 1.f)
                    "| `max_squad_size` | integer | `7` | [1, 15] |",                   # Difficulty.cpp
                    "| `online_actors` | integer | 2 values: `12`, `9` | [5, 15] |",    # constants resolved
                    "| `fly_speed` | number | `120000` | >= 1000 | `[-]` not scaled |"):
            self.assertIn(row, page)


class LtxParser(unittest.TestCase):
    TEXT = ("\ufeff; file note\n; key_a / key_b : glossary text\n\n"
            "; above s1 {economy+}\n[s1]   ; header note\nkey_a = 1   ; {lethality-} meaning a\n"
            "key_b = \"x;y\"  // meaning b\n          ; continued b\nflag\n#include \"other.ltx\"\n"
            "[s2]:s1, s0\nkey_c = 2\n[s1]\nkey_d = [-] 4 ; [task] meaning d\n")

    def test_parse(self):
        f = g.LtxFile("/tmp/t.ltx", self.TEXT)
        self.assertEqual(f.order, ["s1", "s2"])                       # a repeated [s1] adds to the first one
        s1 = f.sections["s1"]
        self.assertEqual([k.name for k in s1.keys], ["key_a", "key_b", "flag", "key_d"])
        self.assertEqual(s1.keys[1].value, "x;y")                    # ';' inside quotes is not a comment, quotes stripped
        self.assertEqual(s1.keys[1].comment, "meaning b continued b")
        self.assertEqual(s1.keys[2].value, "")
        self.assertEqual(s1.above, ["above s1 {economy+}"])
        self.assertEqual(s1.header_comment, "header note")
        self.assertEqual(f.sections["s2"].parents, ["s1", "s0"])
        self.assertEqual(f.includes, [("other.ltx", 10)])
        self.assertEqual(f.intro, ["file note", "key_a / key_b : glossary text"])
        self.assertEqual(f.gloss, {"key_a": "glossary text", "key_b": "glossary text"})
        self.assertEqual(g.section_channel(s1), "economy+")

    def test_channel_tags(self):
        self.assertEqual(g.channel_of("{lethality-} a member goes down"), ("`{lethality-}`", "a member goes down"))
        self.assertEqual(g.channel_of("[economy] gold"), ("`[economy]`", "gold"))
        self.assertEqual(g.channel_of("[-] not scaled"), ("`[-]` not scaled", "not scaled"))
        self.assertEqual(g.channel_of("plain"), (None, "plain"))


class Findings(unittest.TestCase):
    def test_unread_and_missing_keys_are_reported(self):
        tmp = tempfile.mkdtemp()
        try:
            with open(os.path.join(CONFIG, "alife.ltx"), encoding="utf-8") as fh:
                text = fh.read()
            text = re.sub(r"\[news\][^\[]*", "", text) + "\n[sim]\nzzz_not_a_key = 1\n"
            with open(os.path.join(tmp, "alife.ltx"), "w", encoding="utf-8") as fh:
                fh.write(text)
            page = g.render(tmp)
        finally:
            shutil.rmtree(tmp)
        self.assertIn("| `zzz_not_a_key` | integer (entry) | `1` | - | - | - | **not read by src/** |", page)
        self.assertIn("alife.ltx [sim] zzz_not_a_key", page)
        self.assertIn("| `news` | `sos_gap_hours` | number |", page)   # read by Config.cpp, no longer in the file


if __name__ == "__main__":
    unittest.main()
