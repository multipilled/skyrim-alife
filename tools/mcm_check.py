"""MCM consistency check (M7): every native the MCM script declares is registered by the DLL and back; every option on every page
has its event handlers (select / menu / slider, default, info text); the defaults the MCM restores, the shipped ALife.ini and the
difficulty sliders match the plugin's own defaults (include/ALife/Settings.h, difficulty.ltx).

  python -I tools/mcm_check.py [--ini <staged ALife.ini>] [--report out.txt]      (run from the mod checkout; exit 1 on a mismatch)
"""
import argparse
import os
import re
import sys

MOD = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
read = lambda *p: open(os.path.join(MOD, *p), encoding="utf-8").read()

# MCM setter -> (Settings::Data field, how the MCM value maps to the field)
SETTERS = {
    "ALifeSetEnabled": ("enabled", None), "ALifeSetNews": ("news", None), "ALifeSetDynamicRelations": ("dynamicRelations", None),
    "ALifeSetLoot": ("loot", None), "ALifeSetOfflineCombat": ("ocsMode", None), "ALifeSetTalkInitiative": ("talkInitiative", None),
    "ALifeSetCompatBudget": ("compatBudget", None), "ALifeSetFollowGame": ("diff.followGame", None), "ALifeSetDiffScale": ("diff.slider", None),
}
# menu option -> (field, index offset: the menu index the field's default stands for)
MENUS = {"OID_Combat": ("ocsMode", 0), "OID_Talk": ("talkInitiative", 0), "OID_Compat": ("compatBudget", 1)}
# ALife.ini [General] key -> field
INI = {"Enabled": "enabled", "FollowGameDifficulty": "diff.followGame", "OfflineCombat": "ocsMode", "News": "news",
       "DynamicRelations": "dynamicRelations", "TalkInitiative": "talkInitiative", "Loot": "loot", "iCompatBudget": "compatBudget",
       "bCompatHandover": "compatHandover", "PrepareUninstall": "prepareUninstall"}
SCALE_KEYS = ["PopulationScale", "MonsterScale", "StrengthScale", "LethalityScale", "EconomyScale", "MercyScale", "TaskScale", "DragonScale"]


def norm(v):
    v = v.strip().lower()
    return {"true": "1", "false": "0"}.get(v, v)


def block(src, event):
    m = re.search(rf"(?ms)^Event {event}\(.*?^EndEvent", src)
    return m.group(0) if m else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ini")
    ap.add_argument("--report")
    a = ap.parse_args()
    out, bad = [], []
    psc = read("papyrus", "ALife_MCM.psc")
    cpp = read("src", "Papyrus.cpp")
    settings_h = read("include", "ALife", "Settings.h")

    # 1. natives
    declared = set(re.findall(r"(?m)^\s*\w*\s*Function\s+(ALife\w+)\(.*\)\s+Global\s+Native", psc))
    registered = set(re.findall(r'RegisterFunction\("(ALife\w+)"sv', cpp))
    for n in sorted(declared - registered): bad.append(f"native {n} declared in ALife_MCM.psc but not registered in src/Papyrus.cpp")
    for n in sorted(registered - declared): bad.append(f"native {n} registered in src/Papyrus.cpp but not declared in ALife_MCM.psc")
    out.append(f"natives: {len(declared)} declared, {len(registered)} registered")

    # 2. pages and options
    pages = re.findall(r'Pages\[\d+\]\s*=\s*"([^"]+)"', psc)
    out.append(f"pages: {', '.join(pages)}")
    for p in pages[1:]:
        if f'asPage == "{p}"' not in psc: bad.append(f"page {p} has no branch in OnPageReset")
    ev = {e: block(psc, e) for e in ("OnOptionSelect", "OnOptionMenuOpen", "OnOptionMenuAccept", "OnOptionSliderOpen", "OnOptionSliderAccept",
                                     "OnOptionDefault", "OnOptionHighlight")}
    opts = re.findall(r"(OID_\w+)(\[i\])?\s*=\s*Add(Toggle|Slider|Menu|Text)Option\(([^\n]*)\)", psc)
    need = {"Toggle": ["OnOptionSelect"], "Menu": ["OnOptionMenuOpen", "OnOptionMenuAccept"], "Slider": ["OnOptionSliderOpen", "OnOptionSliderAccept"],
            "Text": ["OnOptionSelect"]}
    n_opts = 0
    for oid, arr, kind, args in opts:
        if "OPTION_FLAG_DISABLED" in args: continue   # read-only lines (status, current difficulty)
        n_opts += 1
        key = "DiffIndex(aiOption)" if arr else oid
        for e in need[kind] + ["OnOptionHighlight"] + ([] if kind == "Text" else ["OnOptionDefault"]):
            if key not in ev[e]: bad.append(f"{oid}{arr} ({kind}) has no case in {e}")
    out.append(f"interactive options: {n_opts} ({', '.join(o + a for o, a, k, x in opts if 'OPTION_FLAG_DISABLED' not in x)})")

    # 3. defaults the MCM restores vs Settings.h
    fields = dict(re.findall(r"(?m)^\s*(?:bool|int|float)\s+(\w+)\{\s*([^}]*?)\s*\}", settings_h))
    fields["diff.followGame"] = "true"   # DifficultyChoice defaults (include/ALife/Difficulty.h)
    fields["diff.slider"] = "1.0"
    for setter, args in re.findall(r"(ALifeSet\w+)\(([^)]*)\)", ev["OnOptionDefault"]):
        if setter not in SETTERS: bad.append(f"OnOptionDefault calls {setter}: no known Settings field"); continue
        field = SETTERS[setter][0]
        val = args.split(",")[-1]
        want = fields.get(field)
        ok = want is not None and (norm(val) == norm(want) or (field == "diff.slider" and float(val) == 1.0))
        out.append(f"default {setter}({args.strip()}) -> {field} = {want}: {'ok' if ok else 'MISMATCH'}")
        if not ok: bad.append(f"MCM default {setter}({args}) != Settings.h {field}{{{want}}}")
    mo = ev["OnOptionMenuOpen"]
    for oid, (field, off) in MENUS.items():
        m = re.search(rf"aiOption == {oid}.*?SetMenuDialogDefaultIndex\((-?\d+)\)", mo, re.S)
        if not m: bad.append(f"{oid}: no SetMenuDialogDefaultIndex"); continue
        ok = int(m.group(1)) == int(fields[field]) + off
        out.append(f"menu default {oid} index {m.group(1)} -> {field} = {fields[field]}: {'ok' if ok else 'MISMATCH'}")
        if not ok: bad.append(f"{oid} default index {m.group(1)} != {field} {fields[field]} (+{off})")
    if "SetSliderDialogDefaultValue(1.0)" not in ev["OnOptionSliderOpen"]: bad.append("difficulty sliders: default value is not 1.0")

    # 4. difficulty.ltx: slider ranges hold 1.0, the Adept row is all 1.0 (default play unchanged)
    ltx = read("config", "difficulty.ltx")
    sec = lambda s: re.search(rf"(?ms)^\[{s}\]\s*\n(.*?)(?=^\[|\Z)", ltx).group(1)
    for k, lo, hi in re.findall(r"(?m)^(\w+)\s*=\s*([\d.]+)\s*,\s*([\d.]+)", sec("sliders")):
        if not (float(lo) <= 1.0 <= float(hi)): bad.append(f"difficulty.ltx [sliders] {k} = {lo}, {hi} does not hold the default 1.0")
    for k, v in re.findall(r"(?m)^(\w+)\s*=\s*([\d.]+)", sec("adept")):
        if float(v) != 1.0: bad.append(f"difficulty.ltx [adept] {k} = {v}: Adept must be 1.0")
    out.append("difficulty.ltx: slider ranges and the Adept row checked")

    # 5. the shipped ini
    if a.ini:
        t = open(a.ini, encoding="utf-8").read()
        gen = re.search(r"(?ms)^\[General\](.*?)(?=^\[|\Z)", t).group(1)
        vals = {k: v.split(";")[0].strip() for k, v in re.findall(r"(?m)^(\w+)=(.*)$", gen)}
        for key, field in INI.items():
            if key not in vals: bad.append(f"ini [General] lacks {key}"); continue
            ok = norm(vals[key]) == norm(fields[field])
            if not ok: bad.append(f"ini {key}={vals[key]} != Settings.h {field}{{{fields[field]}}}")
        for key in SCALE_KEYS:
            if key not in vals: bad.append(f"ini [General] lacks {key}")
            elif float(vals[key]) != 1.0: bad.append(f"ini {key}={vals[key]} (default 1.0)")
        save_keys = set(re.findall(r'out << "(\w+)="', read("src", "Settings.cpp"))) | set(SCALE_KEYS)
        for key in vals:
            if key not in save_keys: bad.append(f"ini [General] {key}: the plugin neither reads nor writes it")
        out.append(f"ini {a.ini}: [General] {len(vals)} keys checked")

    out.append(f"\nMISMATCHES: {len(bad)}")
    out += ["  " + b for b in bad]
    out.append("RESULT " + ("FAIL" if bad else "PASS"))
    text = "\n".join(out) + "\n"
    sys.stdout.write(text)
    if a.report: open(a.report, "w", encoding="utf-8", newline="\n").write(text)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
