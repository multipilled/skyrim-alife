"""Tests for tools/mcm_check.py, the MCM release check (ticket G12). Python 3 standard library only.

    python3 -m unittest tools/tests/test_mcm_check.py -v

mcm_check reads the mod checkout under its module-level MOD (papyrus/ALife_MCM.psc, src/Papyrus.cpp,
include/ALife/Settings.h, src/Settings.cpp, config/difficulty.ltx). Each test copies the synthetic matching set in
tools/tests/fixtures/mcm/good/ to a temp dir, plants one mismatch, loads the unchanged tool from tools/mcm_check.py,
points its MOD at the copy and runs main(); it checks the exit code, RESULT line and the planted mismatch line.

Rule kinds mcm_check enforces (read from the tool; RULES below maps each to the test that plants it):
  1 natives: declared in the .psc but not registered (missing native); registered but not declared (extra native).
  2 pages / options: a page with no OnPageReset branch; an interactive option without its case in a needed event
    (Toggle: Select, Default, Highlight; Menu: MenuOpen, MenuAccept, Default, Highlight; Slider: SliderOpen,
    SliderAccept, Default, Highlight; Text: Select, Highlight). OPTION_FLAG_DISABLED options are exempt.
  3 defaults: OnOptionDefault calls an unknown setter; an OnOptionDefault value differs from Settings.h; a menu has no
    SetMenuDialogDefaultIndex or its index differs from the Settings.h default (+ offset); the slider default is not 1.0.
  4 difficulty.ltx: a [sliders] range that does not hold 1.0; an [adept] value that is not 1.0.
  5 --ini: a [General] key missing, a value differing from Settings.h, a scale key missing or not 1.0, a key the
    plugin neither reads nor writes.
"""
import contextlib
import importlib.util
import io
import os
import re
import shutil
import sys
import tempfile
import unittest
import warnings

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, "tools", "mcm_check.py")
GOOD = os.path.join(HERE, "fixtures", "mcm", "good")
PSC = ("papyrus", "ALife_MCM.psc")
CPP = ("src", "Papyrus.cpp")
SETTINGS_H = ("include", "ALife", "Settings.h")
SETTINGS_CPP = ("src", "Settings.cpp")
LTX = ("config", "difficulty.ltx")
INI = ("ALife.ini",)

RULES = {
    "missing native (declared, not registered)": "test_fail_missing_native",
    "extra native (registered, not declared)": "test_fail_extra_native",
    "page without an OnPageReset branch": "test_fail_page_without_branch",
    "handler-less option (new option, no cases)": "test_fail_handlerless_option",
    "option missing its case in one needed event": "test_fail_option_missing_one_handler",
    "OnOptionDefault calls an unknown setter": "test_fail_default_unknown_setter",
    "default mismatch: OnOptionDefault vs Settings.h": "test_fail_default_mismatch",
    "menu without SetMenuDialogDefaultIndex": "test_fail_menu_without_default_index",
    "default mismatch: menu default index": "test_fail_menu_default_index_mismatch",
    "default mismatch: slider default not 1.0": "test_fail_slider_default_not_one",
    "difficulty.ltx slider range without 1.0": "test_fail_slider_range",
    "difficulty.ltx Adept row not 1.0": "test_fail_adept_row",
    "ini [General] key missing": "test_fail_ini_missing_key",
    "default mismatch: ini value vs Settings.h": "test_fail_ini_value_mismatch",
    "ini scale key missing or not 1.0": "test_fail_ini_scale",
    "ini key the plugin neither reads nor writes": "test_fail_ini_unknown_key",
}


def call_main(tool, argv):
    """tool.main() with sys.argv = argv; returns (exit code, stdout). The tool reads and writes files without closing
    them (open(...).read()), so its ResourceWarnings are silenced here to keep the -v output readable."""
    buf, old = io.StringIO(), sys.argv
    try:
        sys.argv = argv
        with contextlib.redirect_stdout(buf), warnings.catch_warnings():
            warnings.simplefilter("ignore", ResourceWarning)
            rc = tool.main()
    finally:
        sys.argv = old
    return rc, buf.getvalue()


def load_tool():
    spec = importlib.util.spec_from_file_location("mcm_check_under_test", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class McmCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="g12-mcm-")
        self.mod = os.path.join(self.tmp, "mod")
        self.fresh()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def fresh(self):
        """a new copy of the matching set (each subTest plants into its own copy)"""
        shutil.rmtree(self.mod, ignore_errors=True)
        shutil.copytree(GOOD, self.mod)

    def path(self, parts):
        return os.path.join(self.mod, *parts)

    def text(self, parts):
        with open(self.path(parts), encoding="utf-8") as f:
            return f.read()

    def write(self, parts, s):
        with open(self.path(parts), "w", encoding="utf-8", newline="\n") as f:
            f.write(s)

    def edit(self, parts, old, new, count=1):
        s = self.text(parts)
        self.assertEqual(s.count(old), count, f"fixture edit: {old!r} expected {count}x in {'/'.join(parts)}")
        self.write(parts, s.replace(old, new))

    def edit_event(self, event, old, new):
        """replace <old> only inside 'Event <event>(' ... 'EndEvent' of the .psc"""
        s = self.text(PSC)
        m = re.search(rf"(?ms)^Event {event}\(.*?^EndEvent", s)
        self.assertIsNotNone(m, event)
        blk = m.group(0)
        self.assertIn(old, blk, f"fixture edit: {old!r} not in {event}")
        self.write(PSC, s[:m.start()] + blk.replace(old, new) + s[m.end():])

    def run_tool(self, ini=False):
        tool = load_tool()
        tool.MOD = self.mod
        rc, out = call_main(tool, ["mcm_check.py"] + (["--ini", self.path(INI)] if ini else []))
        m = re.search(r"(?ms)^MISMATCHES: (\d+)\n(.*?)^RESULT (\w+)", out)
        self.assertIsNotNone(m, out)
        bad = [l.strip() for l in m.group(2).splitlines() if l.strip()]
        self.assertEqual(int(m.group(1)), len(bad), out)
        return rc, out, bad, m.group(3)

    def assertMismatch(self, expect, ini=False, only=False):
        """exit 1, RESULT FAIL and every line in <expect> reported (and nothing else when only=True)"""
        rc, out, bad, result = self.run_tool(ini)
        self.assertEqual((rc, result), (1, "FAIL"), out)
        expect = [expect] if isinstance(expect, str) else expect
        for e in expect:
            self.assertIn(e, bad, out)
        if only:
            self.assertEqual(sorted(bad), sorted(expect), out)
        return bad

    # --- inventory and the matching pair -----------------------------------------------------------------------------
    def test_rule_inventory(self):
        """every rule kind listed in RULES has a planted-failure test"""
        missing = [k for k, t in RULES.items() if not callable(getattr(self, t, None))]
        self.assertEqual(missing, [])
        sys.stderr.write("\n  mcm_check rule kinds -> test:\n" + "".join(f"    {k} -> {t}\n" for k, t in RULES.items()))

    def test_matching_pair_passes(self):
        """matching .psc / Papyrus.cpp / Settings.h / difficulty.ltx: exit 0, 0 mismatches"""
        rc, out, bad, result = self.run_tool()
        self.assertEqual((rc, result, bad), (0, "PASS", []), out)
        self.assertIn("natives: 13 declared, 13 registered", out)
        self.assertIn("pages: A-Life, Difficulty", out)
        self.assertIn("interactive options: 10 (", out)
        self.assertNotIn("OID_Status", out)   # OPTION_FLAG_DISABLED: not interactive
        self.assertEqual(out.count(": ok\n"), 12, out)   # 9 OnOptionDefault setters + 3 menus

    def test_matching_pair_with_ini_passes(self):
        """the matching set plus the shipped ini at the plugin's defaults passes"""
        rc, out, bad, result = self.run_tool(ini=True)
        self.assertEqual((rc, result, bad), (0, "PASS", []), out)
        self.assertIn("[General] 18 keys checked", out)

    def test_disabled_option_needs_no_handlers(self):
        """a read-only (OPTION_FLAG_DISABLED) option without any handler still passes"""
        self.edit(PSC, '\tOID_Status = AddTextOption(', '\tOID_ReadOnly = AddToggleOption("x", true, OPTION_FLAG_DISABLED)\n\tOID_Status = AddTextOption(')
        rc, out, bad, result = self.run_tool()
        self.assertEqual((rc, bad), (0, []), out)

    def test_report_file_written(self):
        """--report writes the same text as stdout"""
        rep = os.path.join(self.tmp, "MCM.txt")
        tool = load_tool()
        tool.MOD = self.mod
        rc, out = call_main(tool, ["mcm_check.py", "--report", rep])
        self.assertEqual(rc, 0, out)
        with open(rep, encoding="utf-8") as f:
            self.assertEqual(f.read(), out)

    # --- 1 natives ---------------------------------------------------------------------------------------------------
    def test_fail_missing_native(self):
        """rule missing-native: declared in the .psc, registration removed from Papyrus.cpp"""
        self.edit(CPP, '\tvm->RegisterFunction("ALifeSetLoot"sv, kScript, SetLoot);\n', "")
        self.assertMismatch("native ALifeSetLoot declared in ALife_MCM.psc but not registered in src/Papyrus.cpp", only=True)
        self.fresh()
        self.edit(PSC, "Function ALifePrepareUninstall() Global Native\n",
                  "Function ALifePrepareUninstall() Global Native\nInt Function ALifeNewThing(Int aiX) Global Native\n")
        self.assertMismatch("native ALifeNewThing declared in ALife_MCM.psc but not registered in src/Papyrus.cpp", only=True)

    def test_fail_extra_native(self):
        """rule extra-native: registered in Papyrus.cpp, not declared in the .psc"""
        self.edit(CPP, "\treturn true;", '\tvm->RegisterFunction("ALifeExtraNative"sv, kScript, Extra);\n\treturn true;')
        self.assertMismatch("native ALifeExtraNative registered in src/Papyrus.cpp but not declared in ALife_MCM.psc", only=True)
        self.fresh()
        self.edit(PSC, "String Function ALifeStatus() Global Native\n", "")
        self.assertMismatch("native ALifeStatus registered in src/Papyrus.cpp but not declared in ALife_MCM.psc", only=True)

    # --- 2 pages and options -----------------------------------------------------------------------------------------
    def test_fail_page_without_branch(self):
        """rule page-branch: a page in Pages[] with no asPage branch in OnPageReset"""
        self.edit(PSC, '\tPages[1] = "Difficulty"\n', '\tPages[1] = "Difficulty"\n\tPages[2] = "Standing"\n')
        self.assertMismatch("page Standing has no branch in OnPageReset", only=True)

    def test_fail_handlerless_option(self):
        """rule option-handlers: a new option of each kind with no event cases fails every needed event"""
        need = {"Toggle": ["OnOptionSelect", "OnOptionHighlight", "OnOptionDefault"],
                "Menu": ["OnOptionMenuOpen", "OnOptionMenuAccept", "OnOptionHighlight", "OnOptionDefault"],
                "Slider": ["OnOptionSliderOpen", "OnOptionSliderAccept", "OnOptionHighlight", "OnOptionDefault"],
                "Text": ["OnOptionSelect", "OnOptionHighlight"]}
        args = {"Toggle": '"New", true, 0', "Menu": '"New", "A", 0', "Slider": '"New", 1.0, "{1}x", 0', "Text": '"New", "Press", 0'}
        for kind, events in need.items():
            with self.subTest(kind=kind):
                self.fresh()
                self.edit(PSC, "\tOID_Status = AddTextOption(", f"\tOID_NewOpt = Add{kind}Option({args[kind]})\n\tOID_Status = AddTextOption(")
                self.assertMismatch([f"OID_NewOpt ({kind}) has no case in {e}" for e in events], only=True)

    def test_fail_option_missing_one_handler(self):
        """rule option-handlers: removing one existing option's case from one needed event fails that event"""
        cases = [("OID_News", "Toggle", "OnOptionSelect"), ("OID_News", "Toggle", "OnOptionHighlight"),
                 ("OID_News", "Toggle", "OnOptionDefault"),
                 ("OID_Talk", "Menu", "OnOptionMenuOpen"), ("OID_Talk", "Menu", "OnOptionMenuAccept"),
                 ("OID_Talk", "Menu", "OnOptionHighlight"), ("OID_Talk", "Menu", "OnOptionDefault"),
                 ("OID_Diff[i]", "Slider", "OnOptionSliderOpen"), ("OID_Diff[i]", "Slider", "OnOptionSliderAccept"),
                 ("OID_Diff[i]", "Slider", "OnOptionHighlight"), ("OID_Diff[i]", "Slider", "OnOptionDefault"),
                 ("OID_Uninstall", "Text", "OnOptionSelect"), ("OID_Uninstall", "Text", "OnOptionHighlight")]
        for oid, kind, event in cases:
            with self.subTest(option=oid, event=event):
                self.fresh()
                if oid == "OID_Diff[i]":
                    self.edit_event(event, "DiffIndex(aiOption)", "-1")
                else:
                    self.edit_event(event, f"aiOption == {oid}\n", "aiOption == OID_Removed\n")
                expect = [f"{oid} ({kind}) has no case in {event}"]
                if (oid, event) == ("OID_Talk", "OnOptionMenuOpen"):
                    expect.append("OID_Talk: no SetMenuDialogDefaultIndex")   # the menu default goes with its case
                self.assertMismatch(expect, only=True)

    # --- 3 defaults --------------------------------------------------------------------------------------------------
    def test_fail_default_unknown_setter(self):
        """rule default-setter: OnOptionDefault calls a setter mcm_check cannot map to a Settings field"""
        self.edit_event("OnOptionDefault", "\t\tALifeSetLoot(true)\n", "\t\tALifeSetLoot(true)\n\t\tALifeSetUnknown(true)\n")
        self.assertMismatch("OnOptionDefault calls ALifeSetUnknown: no known Settings field", only=True)

    def test_fail_default_mismatch(self):
        """rule default-value: an OnOptionDefault value that differs from Settings.h (either side changed)"""
        self.edit_event("OnOptionDefault", "ALifeSetNews(true)", "ALifeSetNews(false)")
        self.assertMismatch("MCM default ALifeSetNews(false) != Settings.h news{true}", only=True)
        self.fresh()
        self.edit(SETTINGS_H, "bool loot{ true };", "bool loot{ false };")
        self.assertMismatch("MCM default ALifeSetLoot(true) != Settings.h loot{false}", only=True)
        self.fresh()
        self.edit_event("OnOptionDefault", "ALifeSetOfflineCombat(1)", "ALifeSetOfflineCombat(2)")
        self.assertMismatch("MCM default ALifeSetOfflineCombat(2) != Settings.h ocsMode{1}", only=True)
        self.fresh()
        self.edit_event("OnOptionDefault", "ALifeSetDiffScale(d, 1.0)", "ALifeSetDiffScale(d, 1.5)")
        self.assertMismatch("MCM default ALifeSetDiffScale(d, 1.5) != Settings.h diff.slider{1.0}", only=True)
        self.fresh()
        self.edit_event("OnOptionDefault", "ALifeSetFollowGame(true)", "ALifeSetFollowGame(false)")
        self.assertMismatch("MCM default ALifeSetFollowGame(false) != Settings.h diff.followGame{true}", only=True)

    def test_fail_menu_without_default_index(self):
        """rule menu-default: a menu option whose OnOptionMenuOpen case sets no default index"""
        self.edit_event("OnOptionMenuOpen", "\t\tSetMenuDialogDefaultIndex(0)\n", "")
        self.assertMismatch("OID_Compat: no SetMenuDialogDefaultIndex", only=True)

    def test_fail_menu_default_index_mismatch(self):
        """rule menu-default: a menu default index that is not the Settings.h default plus the menu's offset"""
        self.edit_event("OnOptionMenuOpen", "SetMenuDialogDefaultIndex(2)", "SetMenuDialogDefaultIndex(3)")
        self.assertMismatch("OID_Talk default index 3 != talkInitiative 2 (+0)", only=True)
        self.fresh()
        self.edit_event("OnOptionMenuOpen", "SetMenuDialogDefaultIndex(0)", "SetMenuDialogDefaultIndex(-1)")
        self.assertMismatch("OID_Compat default index -1 != compatBudget -1 (+1)", only=True)
        self.fresh()
        self.edit(SETTINGS_H, "int ocsMode{ 1 };", "int ocsMode{ 2 };")
        self.assertMismatch(["OID_Combat default index 1 != ocsMode 2 (+0)",
                             "MCM default ALifeSetOfflineCombat(1) != Settings.h ocsMode{2}"], only=True)

    def test_fail_slider_default_not_one(self):
        """rule slider-default: the difficulty sliders' dialog default is not 1.0"""
        self.edit_event("OnOptionSliderOpen", "SetSliderDialogDefaultValue(1.0)", "SetSliderDialogDefaultValue(0.5)")
        self.assertMismatch("difficulty sliders: default value is not 1.0", only=True)

    # --- 4 difficulty.ltx --------------------------------------------------------------------------------------------
    def test_fail_slider_range(self):
        """rule ltx-sliders: a [sliders] range that does not hold 1.0"""
        self.edit(LTX, "strength   = 0.5, 2.0", "strength   = 1.1, 2.0")
        self.assertMismatch("difficulty.ltx [sliders] strength = 1.1, 2.0 does not hold the default 1.0", only=True)

    def test_fail_adept_row(self):
        """rule ltx-adept: an [adept] value that is not 1.0 (other rows may differ)"""
        self.edit(LTX, "[adept]\npopulation = 1.0", "[adept]\npopulation = 1.2")
        self.assertMismatch("difficulty.ltx [adept] population = 1.2: Adept must be 1.0", only=True)

    # --- 5 the shipped ini -------------------------------------------------------------------------------------------
    def test_fail_ini_missing_key(self):
        """rule ini-key: a [General] key the tool expects is missing"""
        self.edit(INI, "bCompatHandover=1\n", "")
        self.assertMismatch("ini [General] lacks bCompatHandover", ini=True, only=True)

    def test_fail_ini_value_mismatch(self):
        """rule ini-value: a shipped [General] value that differs from Settings.h"""
        self.edit(INI, "DynamicRelations=0", "DynamicRelations=1")
        self.assertMismatch("ini DynamicRelations=1 != Settings.h dynamicRelations{false}", ini=True, only=True)
        self.fresh()
        self.edit(INI, "OfflineCombat=1   ;", "OfflineCombat=2   ;")
        self.assertMismatch("ini OfflineCombat=2 != Settings.h ocsMode{1}", ini=True, only=True)

    def test_fail_ini_scale(self):
        """rule ini-scale: a scale key missing, or not 1.0"""
        self.edit(INI, "MercyScale=1.0\n", "")
        self.assertMismatch("ini [General] lacks MercyScale", ini=True, only=True)
        self.fresh()
        self.edit(INI, "DragonScale=1.0", "DragonScale=1.5")
        self.assertMismatch("ini DragonScale=1.5 (default 1.0)", ini=True, only=True)

    def test_fail_ini_unknown_key(self):
        """rule ini-unknown: a [General] key the plugin neither reads nor writes (not in Settings.cpp)"""
        self.edit(INI, "PrepareUninstall=0\n", "PrepareUninstall=0\nStaleKey=1\n")
        self.assertMismatch("ini [General] StaleKey: the plugin neither reads nor writes it", ini=True, only=True)


if __name__ == "__main__":
    unittest.main()
