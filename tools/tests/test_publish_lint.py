"""Tests for tools/publish_lint.py, the release gate (ticket G12). Python 3 standard library only.

    python3 -m unittest tools/tests/test_publish_lint.py -v

Fixtures (all synthetic, see tools/tests/fixtures/publish/README.txt): clean/ is a fake release tree; the tests add its
two binary placeholders (Scripts/ALife_MCM.pex, SKSE/Plugins/ALife.dll: *.pex and *.dll are git-ignored), and the
result passes with 0 FAIL and 0 WARN; ref/ holds fake --game / --ref trees. Each test builds that tree in a temp dir,
plants one problem and
runs the tool as a subprocess (python -I, as tools/package.py does), then checks the exit code, RESULT line and that
the planted path is reported under FAIL (or WARN) with that rule's message. Key-shaped strings are assembled at run
time from placeholders so no key-like literal is committed.

Rule kinds publish_lint enforces (read from the tool; RULES below maps each to the test that plants it):
  FAIL: byte-identical to a game / Anomaly / GAMMA reference file, file type not on the allow list (incl. game
        archives and .esm masters), .env file, key or token pattern (each SECRET alternative), ALife.ini test-build
        switches (bDebug, bForceGate, bBackground, sDebugStartup, sDebugInWorld, sDebugAuto), plugin that is not TES4,
        plugin with an unexpected master, .pex without Source/Scripts .psc, .psc without Scripts .pex.
  WARN: absolute local path, decompiler fingerprint, Anomaly script citations in .md, no README.md, file over 8 MB.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "publish_lint.py")
FIX = os.path.join(HERE, "fixtures", "publish")
CLEAN = os.path.join(FIX, "clean")
REF_GAME = os.path.join(FIX, "ref", "game")
REF_ANOMALY = os.path.join(FIX, "ref", "anomaly")
REF_GAMMA = os.path.join(FIX, "ref", "gamma")
PH = "EXAMPLENOTAKEY"   # placeholder body for key-shaped strings: EXAMPLE-NOT-A-KEY without the dashes the patterns reject

# files of the clean tree that git ignores (*.pex, *.dll), written by build_clean()
GENERATED = {"Scripts/ALife_MCM.pex": "SYNTHETIC PEX PLACEHOLDER for tools/tests/test_publish_lint.py, not a compiled script.\n",
             "SKSE/Plugins/ALife.dll": "SYNTHETIC DLL PLACEHOLDER for tools/tests/test_publish_lint.py, not a plugin binary.\n"}

# rule kind -> test method that plants it (test_rule_inventory checks that every entry exists)
RULES = {
    "FAIL byte-identical to a game Data file": "test_fail_identical_to_game_file",
    "FAIL byte-identical to an Anomaly reference file": "test_fail_identical_to_anomaly_file",
    "FAIL byte-identical to a GAMMA file": "test_fail_identical_to_gamma_file",
    "FAIL file type not on the allow list": "test_fail_type_not_allowed",
    "FAIL game archive (.bsa) / master (.esm)": "test_fail_game_archive_and_master",
    "FAIL .env file": "test_fail_env_file",
    "FAIL key or token pattern": "test_fail_secret_patterns",
    "FAIL ALife.ini debug switch": "test_fail_debug_ini",
    "FAIL plugin that is not TES4": "test_fail_plugin_not_tes4",
    "FAIL plugin with unexpected masters": "test_fail_plugin_bad_master",
    "FAIL .pex without its .psc": "test_fail_pex_without_source",
    "FAIL .psc without its .pex": "test_fail_psc_without_pex",
    "WARN absolute local path": "test_warn_user_path",
    "WARN decompiler fingerprint": "test_warn_decompiler_fingerprint",
    "WARN Anomaly script citations in .md": "test_warn_anomaly_citations",
    "WARN no README.md": "test_warn_no_readme",
    "WARN file over 8 MB": "test_warn_big_file",
}


def readb(*parts):
    with open(os.path.join(*parts), "rb") as f:
        return f.read()


def plugin(masters=()):
    """A minimal synthetic TES4 record: HEDR plus one MAST/DATA pair per master."""
    body = b"HEDR" + struct.pack("<H", 12) + struct.pack("<fII", 1.7, 0, 0x800)
    for m in masters:
        name = m.encode("latin1") + b"\0"
        body += b"MAST" + struct.pack("<H", len(name)) + name + b"DATA" + struct.pack("<H", 8) + b"\0" * 8
    return b"TES4" + struct.pack("<I", len(body)) + b"\0" * 16 + body


def build_clean(dst):
    """the clean package tree at <dst>: fixtures/publish/clean plus the git-ignored placeholders"""
    shutil.copytree(CLEAN, dst)
    for rel, text in GENERATED.items():
        p = os.path.join(dst, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


def run_lint(src, game=REF_GAME, refs=(REF_ANOMALY, REF_GAMMA)):
    cmd = [sys.executable, "-I", TOOL, src, "--game", game]
    for r in refs:
        cmd += ["--ref", r]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, cwd=ROOT)
    return p.returncode, p.stdout


def sections(out):
    """(fails, warns, result) parsed from the tool's report."""
    fails, warns, cur, result = [], [], None, None
    for line in out.splitlines():
        if line.startswith("FAIL: "):
            cur = fails
        elif line.startswith("WARN: "):
            cur = warns
        elif line.startswith("RESULT "):
            result, cur = line.split()[1], None
        elif cur is not None and line.startswith("  "):
            cur.append(line.strip())
    return fails, warns, result


class PublishLintTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="g12-publish-")
        self.pkg = os.path.join(self.tmp, "pkg")
        self.fresh()

    def fresh(self):
        """a new copy of the clean tree (each subTest plants into its own copy)"""
        shutil.rmtree(self.pkg, ignore_errors=True)
        build_clean(self.pkg)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def put(self, rel, data):
        p = os.path.join(self.pkg, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode("utf-8"))

    def remove(self, rel):
        os.remove(os.path.join(self.pkg, *rel.split("/")))

    def lint(self, **kw):
        rc, out = run_lint(self.pkg, **kw)
        fails, warns, result = sections(out)
        return rc, out, fails, warns, result

    def assertFails(self, rel, needle, **kw):
        """The tool exits 1, says RESULT FAIL and reports <rel>: ...<needle>... under FAIL."""
        rc, out, fails, warns, result = self.lint(**kw)
        self.assertEqual(rc, 1, out)
        self.assertEqual(result, "FAIL", out)
        hit = [f for f in fails if f.startswith(rel + ":") and needle in f]
        self.assertTrue(hit, f"no FAIL '{rel}: ...{needle}...' in\n{out}")
        return fails

    def assertWarnsOnly(self, needle, **kw):
        """The tool still passes (exit 0, no FAIL) but reports <needle> under WARN."""
        rc, out, fails, warns, result = self.lint(**kw)
        self.assertEqual((rc, result, fails), (0, "PASS", []), out)
        self.assertTrue([w for w in warns if needle in w], f"no WARN '...{needle}...' in\n{out}")
        return warns

    # --- inventory and the clean tree -------------------------------------------------------------------------------
    def test_rule_inventory(self):
        """every rule kind listed in RULES has a planted-failure test"""
        missing = [k for k, t in RULES.items() if not callable(getattr(self, t, None))]
        self.assertEqual(missing, [])
        sys.stderr.write("\n  publish_lint rule kinds -> test:\n" + "".join(f"    {k} -> {t}\n" for k, t in RULES.items()))

    def test_clean_tree_passes(self):
        """clean fixture tree: exit 0, RESULT PASS, 0 FAIL, 0 WARN, references present"""
        rc, out = run_lint(self.pkg)
        fails, warns, result = sections(out)
        self.assertEqual((rc, result, fails, warns), (0, "PASS", [], []), out)
        self.assertIn("(7 files,", out)
        self.assertIn("3 files indexed by size", out)
        self.assertNotIn("MISSING", out)

    def test_clean_tree_with_good_plugins_passes(self):
        """clean tree plus plugins mastered only on base game / DLC / A-Life plugins passes"""
        self.put("ALife.esp", plugin(["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]))
        self.put("ALifeTalk.esp", plugin(["Skyrim.esm", "ALife.esp", "ALifeFactions.esp"]))
        self.put("ALifeLight.esl", plugin(["Skyrim.esm", "ALifeTalk.esp"]))
        rc, out, fails, warns, result = self.lint()
        self.assertEqual((rc, result, fails, warns), (0, "PASS", [], []), out)
        self.assertIn("plugin ALife.esp: masters Skyrim.esm, Update.esm, Dawnguard.esm, HearthFires.esm, Dragonborn.esm", out)

    def test_clean_zip_passes_and_planted_zip_fails(self):
        """the zip form of the package is read too: clean zip passes, a planted debug ini in the zip fails"""
        z = os.path.join(self.tmp, "clean.zip")
        with zipfile.ZipFile(z, "w") as zf:
            for dp, dn, fn in os.walk(self.pkg):
                for f in fn:
                    full = os.path.join(dp, f)
                    zf.write(full, os.path.relpath(full, self.pkg).replace(os.sep, "/"))
        rc, out = run_lint(z)
        self.assertEqual((rc,) + sections(out), (0, [], [], "PASS"), out)
        bad = os.path.join(self.tmp, "bad.zip")
        with zipfile.ZipFile(z) as src, zipfile.ZipFile(bad, "w") as dst:
            for n in src.namelist():
                data = src.read(n)
                if n == "SKSE/Plugins/ALife.ini":
                    data = data.replace(b"bDebug=0", b"bDebug=1")
                dst.writestr(n, data)
        rc, out = run_lint(bad)
        self.assertEqual(rc, 1, out)
        self.assertIn("SKSE/Plugins/ALife.ini: bDebug=1 (test build ini)", sections(out)[0])

    def test_missing_reference_trees_reported(self):
        """reference trees that do not exist are reported as MISSING (and the clean tree still passes)"""
        nowhere = os.path.join(self.tmp, "nowhere")
        rc, out = run_lint(self.pkg, game=nowhere, refs=(nowhere + "2",))
        self.assertEqual(rc, 0, out)
        self.assertIn(f"{nowhere} (MISSING)", out)
        self.assertIn("0 files indexed by size", out)

    # --- FAIL rules ---------------------------------------------------------------------------------------------------
    def test_fail_identical_to_game_file(self):
        """rule identical-to-reference (game Data): a copied game file fails"""
        data = readb(REF_GAME, "Interface", "fake_game_strings.txt")
        self.put("Interface/strings_copy.txt", data)
        fails = self.assertFails("Interface/strings_copy.txt", "byte-identical to ")
        self.assertIn("fake_game_strings.txt", " ".join(fails))

    def test_fail_identical_to_anomaly_file(self):
        """rule identical-to-reference (Anomaly ref): a copied Anomaly file fails, even renamed"""
        data = readb(REF_ANOMALY, "configs", "fake_anomaly.ltx")
        self.put("SKSE/Plugins/ALife/renamed.ltx", data)
        fails = self.assertFails("SKSE/Plugins/ALife/renamed.ltx", "byte-identical to ")
        self.assertIn("fake_anomaly.ltx", " ".join(fails))

    def test_fail_identical_to_gamma_file(self):
        """rule identical-to-reference (GAMMA): a copied GAMMA file fails"""
        data = readb(REF_GAMMA, "mods", "fake_gamma.ini")
        self.put("SKSE/Plugins/gamma_copy.ini", data)
        fails = self.assertFails("SKSE/Plugins/gamma_copy.ini", "byte-identical to ")
        self.assertIn("fake_gamma.ini", " ".join(fails))

    def test_same_size_different_bytes_is_not_identical(self):
        """a same-size file with different bytes is hashed and not flagged (the size index is only a prefilter)"""
        data = bytearray(readb(REF_GAMMA, "mods", "fake_gamma.ini"))
        data[-2:-1] = b"2"
        self.put("SKSE/Plugins/own.ini", bytes(data))
        rc, out, fails, warns, result = self.lint()
        self.assertEqual((rc, fails), (0, []), out)

    def test_fail_type_not_allowed(self):
        """rule allow-list: file types outside the release allow list fail, including files with no extension"""
        for rel, ext in (("SKSE/Plugins/helper.exe", ".exe"), ("gamedata/scripts/xr_fake.script", ".script"),
                         ("tools/build.py", ".py"), ("LICENSE", "(none)")):
            with self.subTest(rel=rel):
                self.fresh()
                self.put(rel, "synthetic\n")
                self.assertFails(rel, f"file type {ext} is not on the release allow list")

    def test_fail_game_archive_and_master(self):
        """rule allow-list (game archives / masters): a .bsa archive and a shipped Skyrim.esm fail"""
        self.put("ALife - Textures.bsa", b"BSA\0" + b"\0" * 80)
        self.assertFails("ALife - Textures.bsa", "file type .bsa is not on the release allow list")
        self.fresh()
        game = os.path.join(self.tmp, "game")
        shutil.copytree(REF_GAME, game)
        esm = plugin([]) + b"\0" * 64
        with open(os.path.join(game, "Skyrim.esm"), "wb") as f:
            f.write(esm)
        self.put("Skyrim.esm", esm)
        fails = self.assertFails("Skyrim.esm", "file type .esm is not on the release allow list", game=game)
        self.assertIn("Skyrim.esm: byte-identical to " + os.path.join(game, "Skyrim.esm"), fails)

    def test_fail_env_file(self):
        """rule .env: a .env file (and a name ending in .env) fails"""
        for rel in (".env", "SKSE/Plugins/local.env"):
            with self.subTest(rel=rel):
                self.fresh()
                self.put(rel, "NAME=placeholder\n")
                self.assertFails(rel, ".env file")

    def test_fail_secret_patterns(self):
        """rule key-or-token: one planted placeholder per SECRET alternative fails, in an allowed text file"""
        cases = {
            "anthropic": "sk-" + "ant-" + PH + "000000",
            "openai": "sk-" + PH * 3,
            "github classic": "gh" + "p_" + PH * 3,
            "github fine-grained": "github" + "_pat_" + PH * 3,
            "aws": "AK" + "IA" + "EXAMPLENOTAKEY00",
            "slack": "xo" + "xb-" + "EXAMPLE-NOT-A-KEY",
            "google": "AI" + "za" + (PH * 3)[:35],
            "private key": "-----BEGIN " + "PRIVATE KEY-----",
            "rsa private key": "-----BEGIN RSA " + "PRIVATE KEY-----",
            "api_key assignment": "api_key = '" + "EXAMPLE-NOT-A-KEY" + "'",
            "token assignment": 'token: "' + "EXAMPLE-NOT-A-KEY" + '"',
            "password assignment": "password='" + "EXAMPLE-NOT-A-KEY" + "'",
        }
        for name, s in cases.items():
            with self.subTest(pattern=name):
                self.fresh()
                self.put("docs/ALife/SETUP.md", f"# Setup\n\n{s}\n")
                self.assertFails("docs/ALife/SETUP.md", "looks like it holds a key or token")

    def test_short_placeholder_assignment_is_not_a_secret(self):
        """a key assignment with a value under 12 characters is not flagged (the pattern's lower bound)"""
        self.put("docs/ALife/SETUP.md", "# Setup\n\napi_key = 'EXAMPLE'\n")
        rc, out, fails, warns, result = self.lint()
        self.assertEqual((rc, fails), (0, []), out)

    def test_fail_debug_ini(self):
        """rule test-build ini: each debug switch in ALife.ini fails with its key"""
        ini = os.path.join(CLEAN, "SKSE", "Plugins", "ALife.ini")
        base = readb(ini).decode("utf-8")
        cases = {
            "bDebug": (base.replace("bDebug=0", "bDebug=1"), "bDebug=1 (test build ini)"),
            "bDebug spaced": (base.replace("bDebug=0", "bDebug = 1"), "bDebug=1 (test build ini)"),
            "bForceGate": (base.replace("bForceGate=0", "bForceGate=1"), "bForceGate=1 (test build ini)"),
            "bBackground": (base + "bBackground=1\n", "bBackground=1 (test build ini)"),
            "sDebugStartup": (base.replace("sDebugStartup=", "sDebugStartup=scenario"), "sDebugStartup is set (test build ini)"),
            "sDebugInWorld": (base.replace("sDebugInWorld=", "sDebugInWorld=queue"), "sDebugInWorld is set (test build ini)"),
            "sDebugAuto": (base.replace("sDebugAuto=", "sDebugAuto=1"), "sDebugAuto is set (test build ini)"),
        }
        for name, (text, msg) in cases.items():
            with self.subTest(switch=name):
                self.assertNotEqual(text, base)
                self.fresh()
                self.put("SKSE/Plugins/ALife.ini", text)
                fails = self.assertFails("SKSE/Plugins/ALife.ini", msg)
                self.assertEqual(len(fails), 1, fails)

    def test_debug_ini_empty_values_and_comments_pass(self):
        """empty debug strings, commented switches and debug values on other ini files do not fail"""
        ini = readb(CLEAN, "SKSE", "Plugins", "ALife.ini").decode("utf-8")
        self.put("SKSE/Plugins/ALife.ini", ini + "; bDebug=1\n;sDebugStartup=x\nsDebugAuto=   ; empty\n")
        self.put("SKSE/Plugins/Other.ini", "[Debug]\nbDebug=1\n")
        rc, out, fails, warns, result = self.lint()
        self.assertEqual((rc, fails), (0, []), out)

    def test_fail_plugin_not_tes4(self):
        """rule plugin-not-TES4: an .esp that does not start with TES4 fails"""
        self.put("ALife.esp", b"NOTAPLUGIN" + b"\0" * 80)
        self.assertFails("ALife.esp", "not a TES4 plugin")

    def test_fail_plugin_bad_master(self):
        """rule unexpected-masters: a plugin mastered on anything outside base game / DLC / A-Life fails"""
        self.put("ALife.esp", plugin(["Skyrim.esm", "SomeOtherMod.esp"]))
        self.assertFails("ALife.esp", "unexpected masters ['SomeOtherMod.esp']")
        self.fresh()
        self.put("ALifeLight.esl", plugin(["Skyrim.esm", "Unofficial Skyrim Special Edition Patch.esp"]))
        self.assertFails("ALifeLight.esl", "unexpected masters ['Unofficial Skyrim Special Edition Patch.esp']")

    def test_fail_pex_without_source(self):
        """rule pex-needs-psc: a compiled script without Source/Scripts/<name>.psc fails"""
        self.remove("Source/Scripts/ALife_MCM.psc")
        self.assertFails("Scripts/ALife_MCM.pex", "compiled script without its source in Source/Scripts")
        self.fresh()
        self.put("Scripts/ALife_Extra.pex", "SYNTHETIC PEX PLACEHOLDER\n")
        self.assertFails("Scripts/ALife_Extra.pex", "compiled script without its source in Source/Scripts")

    def test_fail_psc_without_pex(self):
        """rule psc-needs-pex: a script source without Scripts/<name>.pex fails"""
        self.remove("Scripts/ALife_MCM.pex")
        self.assertFails("Source/Scripts/ALife_MCM.psc", "script source without Scripts/alife_mcm.pex")

    # --- WARN rules (reported, but the package still passes) ----------------------------------------------------------
    def test_warn_user_path(self):
        """warn absolute-path: user and dev-machine paths in shipped text"""
        for p in ("C:\\Users\\someone\\Documents", "/home/someone/", "D:\\GAMMA\\mods"):
            with self.subTest(path=p):
                self.fresh()
                self.put("docs/ALife/PATHS.md", f"# Paths\n\nSee {p} for details.\n")
                self.assertWarnsOnly("docs/ALife/PATHS.md: absolute local path")

    def test_warn_decompiler_fingerprint(self):
        """warn decompiler-fingerprint: FUN_/DAT_/sub_ names in shipped text"""
        for s in ("FUN_1400a2b3c", "DAT_14012345", "sub_140ABCDEF", "Decompiled with"):
            with self.subTest(name=s):
                self.fresh()
                self.put("SKSE/Plugins/ALife/notes.txt", f"see {s} here\n")
                self.assertWarnsOnly("SKSE/Plugins/ALife/notes.txt: decompiler fingerprint")

    def test_warn_anomaly_citations(self):
        """warn anomaly-citations: Anomaly script names cited in a shipped .md"""
        self.put("docs/ALife/DESIGN.md", "# Design\n\nAs in sim_board.script:120 and smart_terrain.script.\n")
        warns = self.assertWarnsOnly("docs/ALife/DESIGN.md: 2 citations of Anomaly script names")
        self.assertEqual(len(warns), 1, warns)

    def test_warn_no_readme(self):
        """warn no-readme: a package without README.md"""
        self.remove("README.md")
        self.assertWarnsOnly("no README.md in the package")

    def test_warn_big_file(self):
        """warn size: a file over 8 MB"""
        self.put("textures/ALife/big.dds", b"\0" * (8 * 1024 * 1024 + 1))
        self.assertWarnsOnly("textures/ALife/big.dds: 8 MB")


if __name__ == "__main__":
    unittest.main()
