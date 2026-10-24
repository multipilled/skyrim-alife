"""Tests for tools/compare_harness.py (ticket G8). Python 3 standard library only.

    python3 -m unittest tools/tests/test_compare_harness.py -v

The reports here are small synthetic ones in the shape `harness --json` writes (format alife-harness-report, version 1).
"""
import copy
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "compare_harness.py")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import compare_harness as ch  # noqa: E402

REPORT = {
    "format": "alife-harness-report", "version": 1, "mode": "m5d", "args": ["--m5d"], "seed": 1, "seeds": 1, "days": 30,
    "step_hours": 0.1667, "failures": 0, "exit_code": 0,
    "checks": {"m5d: bounded": {"pass": True, "runs": 1, "failed": 0},
               "m5d: survivors get up in time": {"pass": True, "runs": 1, "failed": 0}},
    "runs": [{
        "seed": 1, "days": 30, "steps": 4319, "end_day": 33.37,
        "squads": {"total": 339, "by_faction": {"bandit": {"squads": 41, "members": 128, "killed": 139},
                                                 "imperial": {"squads": 29, "members": 93, "killed": 7}},
                   "by_kind": {"people": {"squads": 192, "members": 556}, "dragons": {"squads": 0, "members": 0}}},
        "online": {"eligible_at_end": 266, "eligible_by_faction": {"bandit": 41}},
        "kills": {"matrix": {"bandit": {"imperial": 8}}, "total": 8},
        "downs": {"downed": 184, "offline": 184, "finished": 127},
        "news": {"lines": 429, "sample": ["SOS from the Bandits near BrokenOarGrotto: under attack by Wild beasts!"]},
        "cpu": {"steps": 4319, "wall_ms_total": 2969.5, "step_ms": {"p50": 0.377, "p99": 0.636, "max": 0.794, "mean": 0.39}},
    }],
    "probes": [],
}


def run(a, b, *extra):
    out, err = io.StringIO(), io.StringIO()
    with tempfile.TemporaryDirectory() as d:
        pa, pb = os.path.join(d, "a.json"), os.path.join(d, "b.json")
        for p, obj in ((pa, a), (pb, b)):
            with open(p, "w", encoding="utf-8") as f:
                f.write(obj if isinstance(obj, str) else json.dumps(obj))
        with redirect_stdout(out), redirect_stderr(err):
            rc = ch.main([pa, pb] + list(extra))
    return rc, out.getvalue(), err.getvalue()


def changed(**edits):
    """A copy of REPORT with dotted-path edits, e.g. changed(**{"runs.0.news.lines": 430})."""
    r = copy.deepcopy(REPORT)
    for path, v in edits.items():
        cur = r
        segs = path.split(".")
        for s in segs[:-1]:
            cur = cur[int(s)] if isinstance(cur, list) else cur[s]
        last = segs[-1]
        if v is ch.MISSING:
            del cur[last]
        elif isinstance(cur, list):
            cur[int(last)] = v
        else:
            cur[last] = v
    return r


class Identical(unittest.TestCase):
    def test_same_report_is_identical(self):
        rc, out, _ = run(REPORT, copy.deepcopy(REPORT))
        self.assertEqual((rc, out), (0, "identical\n"))

    def test_cpu_fields_ignored_by_default(self):
        b = changed(**{"runs.0.cpu.wall_ms_total": 9999.0, "runs.0.cpu.step_ms": {"p50": 3, "p99": 9, "max": 40, "mean": 4}})
        rc, out, _ = run(REPORT, b)
        self.assertEqual((rc, out), (0, "identical\n"))

    def test_include_cpu_reports_them(self):
        b = changed(**{"runs.0.cpu.wall_ms_total": 9999.0})
        rc, out, _ = run(REPORT, b, "--include-cpu")
        self.assertEqual(rc, 1)
        self.assertIn("differs: 1 field(s)", out)
        self.assertIn("runs.0.cpu.wall_ms_total: 2969.5 -> 9999.0", out)

    def test_include_cpu_with_a_tolerance(self):
        b = changed(**{"runs.0.cpu.step_ms.p99": 0.7})
        rc, out, _ = run(REPORT, b, "--include-cpu", "--tol", "**.cpu.**=50%")
        self.assertEqual((rc, out), (0, "equivalent: 1 field(s) differ within tolerance\n"))

    def test_int_and_float_of_same_value_are_equal(self):
        rc, out, _ = run(REPORT, changed(**{"days": 30.0}))
        self.assertEqual((rc, out), (0, "identical\n"))


class Differs(unittest.TestCase):
    def test_another_seed_lists_each_differing_field(self):
        b = changed(**{"seed": 2, "runs.0.seed": 2, "runs.0.squads.by_faction.bandit.squads": 44, "runs.0.news.lines": 431})
        rc, out, _ = run(REPORT, b)
        self.assertEqual(rc, 1)
        lines = out.splitlines()
        self.assertEqual(lines[0], "differs: 4 field(s)")
        self.assertIn("  seed: 1 -> 2  (delta +1, tolerance 0)", lines)
        self.assertIn("  runs.0.squads.by_faction.bandit.squads: 41 -> 44  (delta +3, tolerance 0)", lines)
        self.assertIn("  runs.0.news.lines: 429 -> 431  (delta +2, tolerance 0)", lines)

    def test_string_and_bool_fields(self):
        b = changed(**{"runs.0.news.sample": ["something else"], "mode": "m5f"})
        b["checks"]["m5d: bounded"]["pass"] = False
        rc, out, _ = run(REPORT, b)
        self.assertEqual(rc, 1)
        self.assertIn('  mode: "m5d" -> "m5f"  (tolerance 0)', out)
        self.assertIn('  checks["m5d: bounded"].pass: true -> false  (tolerance 0)', out)
        self.assertIn('runs.0.news.sample.0: "SOS from the Bandits', out)

    def test_bool_is_not_a_number(self):
        b = changed(**{"failures": False})   # 0 vs false: different types
        rc, out, _ = run(REPORT, b, "--tol", "failures=5")
        self.assertEqual(rc, 1)
        self.assertIn("failures: 0 -> false", out)

    def test_missing_fields_on_either_side(self):
        b = changed(**{"runs.0.downs": ch.MISSING})
        b["runs"][0]["extra"] = 1
        rc, out, _ = run(REPORT, b)
        self.assertEqual(rc, 1)
        self.assertIn("runs.0.downs.downed: only in a (184)", out)
        self.assertIn("runs.0.extra: only in b (1)", out)

    def test_null_vs_object(self):
        rc, out, _ = run(REPORT, changed(**{"runs.0.downs": None}))
        self.assertEqual(rc, 1)
        self.assertIn("runs.0.downs: only in b (null)", out)

    def test_list_lengths(self):
        b = copy.deepcopy(REPORT)
        b["runs"].append(copy.deepcopy(REPORT["runs"][0]))
        rc, out, _ = run(REPORT, b)
        self.assertEqual(rc, 1)
        self.assertIn("runs.1.seed: only in b (1)", out)

    def test_max_lines(self):
        b = copy.deepcopy(REPORT)
        for f in b["runs"][0]["squads"]["by_faction"].values():
            f["squads"] += 1
            f["members"] += 1
        rc, out, _ = run(REPORT, b, "--max-lines", "2")
        self.assertEqual(rc, 1)
        self.assertIn("differs: 4 field(s)", out)
        self.assertIn("... 2 more", out)


class Tolerances(unittest.TestCase):
    def test_absolute(self):
        b = changed(**{"runs.0.news.lines": 431})
        self.assertEqual(run(REPORT, b, "--tol", "runs.*.news.lines=2")[0], 0)
        rc, out, _ = run(REPORT, b, "--tol", "runs.*.news.lines=1")
        self.assertEqual(rc, 1)
        self.assertIn("(delta +2, tolerance 1)", out)

    def test_percent_of_the_larger(self):
        b = changed(**{"runs.0.squads.total": 349})   # 10 of 349 = 2.9 %
        self.assertEqual(run(REPORT, b, "--tol", "runs.*.squads.total=3%")[0], 0)
        self.assertEqual(run(REPORT, b, "--tol", "runs.*.squads.total=2.5%")[0], 1)

    def test_double_star_spans_segments(self):
        b = changed(**{"runs.0.squads.by_faction.bandit.members": 130, "runs.0.squads.by_kind.people.members": 560})
        rc, out, _ = run(REPORT, b, "--tol", "runs.**.members=5")
        self.assertEqual((rc, out), (0, "equivalent: 2 field(s) differ within tolerance\n"))

    def test_ignore_and_last_rule_wins(self):
        b = changed(**{"runs.0.news.lines": 999})
        self.assertEqual(run(REPORT, b, "--tol", "runs.*.news.**=ignore")[1], "identical\n")
        self.assertEqual(run(REPORT, b, "--tol", "runs.*.news.**=ignore", "--tol", "runs.*.news.lines=0")[0], 1)

    def test_quoted_key_is_one_segment(self):
        b = copy.deepcopy(REPORT)
        b["checks"]["m5d: bounded"]["runs"] = 2
        self.assertEqual(run(REPORT, b, "--tol", "checks.m5d:*.runs=1")[0], 0)

    def test_tolerance_file(self):
        b = changed(**{"runs.0.news.lines": 431, "runs.0.online.eligible_at_end": 270})
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump({"runs.*.news.lines": "1%", "runs.*.online.eligible_at_end": "4"}, f)
        try:
            rc, out, _ = run(REPORT, b, "--tol-file", f.name, "--verbose")
        finally:
            os.unlink(f.name)
        self.assertEqual(rc, 0)
        self.assertTrue(out.startswith("equivalent: 2 field(s) differ within tolerance\n"))
        self.assertIn("within: runs.0.online.eligible_at_end: 266 -> 270  (delta +4, tolerance 4)", out)

    def test_verbose_counts_ignored_cpu_fields(self):
        rc, out, _ = run(REPORT, REPORT, "--verbose")
        self.assertEqual(rc, 0)
        self.assertEqual(out.splitlines()[0], "identical")
        self.assertIn(", 0 differing, 6 ignored)", out)

    def test_spec_parsing(self):
        self.assertEqual(ch.parse_spec("0"), ("exact", 0.0))
        self.assertEqual(ch.parse_spec("2.5"), ("abs", 2.5))
        self.assertEqual(ch.parse_spec("5%"), ("pct", 5.0))
        self.assertEqual(ch.parse_spec("ignore"), ("ignore", 0.0))
        for bad in ("-1", "x", "nan", "inf%"):
            with self.assertRaises(ValueError):
                ch.parse_spec(bad)


class Errors(unittest.TestCase):
    def test_bad_tolerance_exits_2(self):
        rc, _, err = run(REPORT, REPORT, "--tol", "runs.*.news.lines")
        self.assertEqual(rc, 2)
        self.assertIn("PATTERN=SPEC", err)
        self.assertEqual(run(REPORT, REPORT, "--tol", "x=abc")[0], 2)

    def test_unreadable_json_exits_2(self):
        rc, _, err = run(REPORT, "{not json")
        self.assertEqual(rc, 2)
        self.assertIn("error:", err)

    def test_missing_file_exits_2(self):
        with redirect_stderr(io.StringIO()):
            self.assertEqual(ch.main(["/nonexistent/a.json", "/nonexistent/b.json"]), 2)


class Command(unittest.TestCase):
    def test_runs_as_a_script(self):
        with tempfile.TemporaryDirectory() as d:
            pa, pb = os.path.join(d, "a.json"), os.path.join(d, "b.json")
            for path, obj in ((pa, REPORT), (pb, changed(**{"runs.0.kills.total": 9}))):
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(obj, f)
            p = subprocess.run([sys.executable, TOOL, pa, pa], stdout=subprocess.PIPE, universal_newlines=True)
            self.assertEqual((p.returncode, p.stdout), (0, "identical\n"))
            p = subprocess.run([sys.executable, TOOL, pa, pb], stdout=subprocess.PIPE, universal_newlines=True)
            self.assertEqual(p.returncode, 1)
            self.assertEqual(p.stdout, "differs: 1 field(s)\n  runs.0.kills.total: 8 -> 9  (delta +1, tolerance 0)\n")


if __name__ == "__main__":
    unittest.main()
