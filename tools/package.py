"""Assembles an installable A-Life release (Vortex / MO2: everything relative to Data) and its zip, then lints it.

  python -I tools/package.py --version 2.1.0 [--out-dir releases/ALife-2.1.0-rc] [--dll ...] [--pex-dir ...] [--bdebug]

Output (all under --out-dir, default releases/ALife-<version>): ALife-<version>/ (the staged tree), ALife-<version>.zip, LINT.txt
(tools/publish_lint.py on the zip), MCM.txt (tools/mcm_check.py with the shipped ini) and MANIFEST.txt (every file, size, sha256).
Exit 1 when the ESP validation, the lint or the MCM check fails.

Built here, from this checkout and the player's own Skyrim.esm (nothing from the game is copied):
  ALife.esp          tools/esp/build_esp.py (pool actors, packages incl. M5h camp life, job markers), validated by validate_esp.py
  ALifeFactions.esp  tools/build_factions_esp.py from config/factions.ltx
  ALifeTalk.esp      tools/build_talk_esp.py (config/talk.ltx alongside)
From the build: SKSE/Plugins/ALife.dll (--dll), Scripts/*.pex (--pex-dir: ALife_MCM, ALife_Tasks, ALife_Companions) with their
sources in Source/Scripts, every config/*.ltx, a default ALife.ini written from the plugin's defaults (include/ALife/Settings.h).
From the previous release zip (--base-zip): only the FaceGen (meshes/, textures/: the mod's own Creation Kit export, keyed by the
pool NPC_ FormIDs, which build_esp.py keeps) and docs/ALife/FACEGEN.md.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.environ.get("ALIFE_ROOT", os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = ["ALife_MCM", "ALife_Tasks", "ALife_Companions"]
DOCS = ["README.md", "CHANGELOG.md", "STANDING.md", "CONVERSATIONS.md", "TASKS.md", "COMPANIONS.md", "WOUNDED.md", "DIFFICULTY.md",
        "CAMPLIFE.md", "MUTANTS.md", "CIVILWAR.md", "SOLSTHEIM.md", "COMPAT.md"]   # player-facing notes; PARITY / TESTLOG / GROK_AUDIT stay in the repo

ap = argparse.ArgumentParser()
ap.add_argument("--version", default="2.1.0")
ap.add_argument("--out-dir", help="default releases/ALife-<version>")
ap.add_argument("--bdebug", action="store_true", help="ship ALife.ini with bDebug=1 (test builds; the lint then fails on purpose)")
ap.add_argument("--base-zip", default=os.path.join(ROOT, "releases", "ALife-2.0.0-anomaly.zip"), help="previous release: FaceGen only")
ap.add_argument("--mod", default=os.path.normpath(os.path.join(HERE, "..")), help="checkout to package (default: this one)")
ap.add_argument("--dll", default=os.path.join(ROOT, "build", "Release", "ALife.dll"))
ap.add_argument("--pex-dir", default=os.path.join(ROOT, "build", "papyrus"), help="folder of the compiled scripts")
ap.add_argument("--esm", default=r"C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data\Skyrim.esm", help="the player's Skyrim.esm")
ap.add_argument("--esp-author", default="multipilled", help="ALife.esp header author")
ap.add_argument("--no-lint", action="store_true")
args = ap.parse_args()
MOD = args.mod
PY = sys.executable
name = f"ALife-{args.version}"
outdir = args.out_dir or os.path.join(ROOT, "releases", name)
out = os.path.join(outdir, name)
if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(out)
failed = []


def put(src, dst_rel):
    dst = os.path.join(out, dst_rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    tail = (r.stdout + r.stderr).strip().splitlines()[-3:]
    print(f"{what}: exit {r.returncode}" + ("".join("\n  " + t for t in tail)))
    if r.returncode:
        failed.append(what)
    return r


# 1) plugins, built from this checkout
run([PY, "-B", os.path.join(MOD, "tools", "esp", "build_esp.py"), args.esm, os.path.join(out, "ALife.esp"), os.path.join(outdir, "esp-manifest.json"),
     "--author", args.esp_author], "ALife.esp (build_esp.py)")
run([PY, "-B", os.path.join(MOD, "tools", "esp", "validate_esp.py"), os.path.join(out, "ALife.esp"), os.path.dirname(args.esm),
     "--manifest", os.path.join(MOD, "tools", "esp", "esp-manifest.json")], "ALife.esp validate (+ committed esp-manifest.json)")
run([PY, "-B", os.path.join(MOD, "tools", "build_factions_esp.py"), os.path.join(MOD, "config", "factions.ltx"), os.path.join(out, "ALifeFactions.esp")],
    "ALifeFactions.esp")
run([PY, "-B", os.path.join(MOD, "tools", "build_talk_esp.py"), os.path.join(out, "ALifeTalk.esp"), os.path.join(MOD, "config", "talk.ltx")], "ALifeTalk.esp")
for esp in ("ALifeFactions.esp", "ALifeTalk.esp"):
    run([PY, "-B", os.path.join(MOD, "tools", "esp", "validate_esp.py"), os.path.join(out, esp), os.path.dirname(args.esm), out], f"{esp} validate")

# 2) the SKSE plugin, scripts and configs
put(args.dll, "SKSE/Plugins/ALife.dll")
for s in SCRIPTS:
    pex = os.path.join(args.pex_dir, s + ".pex")
    if not os.path.isfile(pex):
        failed.append(f"{s}.pex missing in {args.pex_dir}")
        continue
    put(pex, f"Scripts/{s}.pex")
    put(os.path.join(MOD, "papyrus", s + ".psc"), f"Source/Scripts/{s}.psc")
for f in sorted(os.listdir(os.path.join(MOD, "config"))):
    if f.endswith(".ltx"):
        put(os.path.join(MOD, "config", f), f"SKSE/Plugins/ALife/{f}")

# 3) FaceGen from the previous release (the mod's own export; its file names are the unchanged pool NPC_ FormIDs)
with zipfile.ZipFile(args.base_zip) as z:
    for n in z.namelist():
        if n.startswith(("meshes/", "textures/")) or n == "docs/ALife/FACEGEN.md":
            z.extract(n, out)

# 4) default ini: the keys and defaults Settings::Save writes (include/ALife/Settings.h); tools/mcm_check.py checks them
dbg = 1 if args.bdebug else 0
ini = f"""; A-Life for Skyrim {args.version} (Anomaly edition). The MCM writes [General]; edit [Debug] by hand only.
[General]
Enabled=1
FollowGameDifficulty=1   ; Skyrim's difficulty picks a difficulty.ltx row, times the sliders below (docs/ALife/DIFFICULTY.md)
PopulationScale=1.0
MonsterScale=1.0
StrengthScale=1.0
LethalityScale=1.0
EconomyScale=1.0
MercyScale=1.0
TaskScale=1.0
DragonScale=1.0
OfflineCombat=1   ; 0 off, 1 only at smart terrains (default), 2 full
News=1
DynamicRelations=0
TalkInitiative=2   ; squads stopping you to talk: 0 never, 1 rarely, 2 normally (default), 3 often
Loot=1   ; squads strip the bodies they pass
iCompatBudget=-1   ; patrol mods (Immersive Patrols, Skyrim at War): -1 auto, 0 off (full 3 groups / 15 actors), 1-3 online groups
bCompatHandover=1   ; forts a patrol mod garrisons get no A-Life respawn or outlaw squads
PrepareUninstall=0

; Test switch. Never shown in the MCM. Hotkeys are DirectX scan codes.
; Numpad1=79 status dump, Numpad2=80 fast-forward one game day, Numpad3=81 place a hostile pair near you.
[Debug]
bDebug={dbg}
bForceGate={dbg}
sDebugStartup={"coc AbandonedShackExterior" if args.bdebug else ""}
sDebugInWorld={"tgm" if args.bdebug else ""}
sDebugAuto=
iDebugSeed=1
iDebugKeyStatus=79
iDebugKeyFight=80
iDebugKeyFillSticky=81
iDebugKeyGoto=75
iDebugKeyScenario=76
iDebugKeyKill=77
iDebugKeyTalk=71
"""
ini_path = os.path.join(out, "SKSE", "Plugins", "ALife.ini")
open(ini_path, "w", newline="\n").write(ini)

# 5) player docs
for f in DOCS:
    src = os.path.join(MOD, "docs", f)
    if os.path.isfile(src):
        put(src, f"docs/ALife/{f}")
    else:
        failed.append(f"docs/{f} missing")

# 6) zip (forward slashes so Vortex installs the full tree) and manifest
zpath = os.path.join(outdir, name + ".zip")
if os.path.exists(zpath):
    os.remove(zpath)
rows = []
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, dn, fn in os.walk(out):
        dn.sort()
        for f in sorted(fn):
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, out).replace(os.sep, "/")
            z.write(full, rel)
            rows.append(f"{hashlib.sha256(open(full, 'rb').read()).hexdigest()}  {os.path.getsize(full):>9}  {rel}")
open(os.path.join(outdir, "MANIFEST.txt"), "w", newline="\n").write("\n".join(rows) + "\n")
print(f"{name}: {len(rows)} files -> {zpath} ({os.path.getsize(zpath) // 1024} KB)")

# 7) checks
if not args.no_lint:
    run([PY, "-I", os.path.join(MOD, "tools", "publish_lint.py"), zpath, "--report", os.path.join(outdir, "LINT.txt"),
         "--game", os.path.dirname(args.esm)], "publish lint")
    run([PY, "-I", os.path.join(MOD, "tools", "mcm_check.py"), "--ini", ini_path, "--report", os.path.join(outdir, "MCM.txt")], "MCM check")
print("PACKAGE " + ("FAILED: " + "; ".join(failed) if failed else "OK"))
sys.exit(1 if failed else 0)
