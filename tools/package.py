"""Assembles releases/ALife-<version>/ (Vortex/MO2-installable, everything relative to Data) and the matching .zip.

  python package.py [--version 2.0.0] [--bdebug]

Inputs:  build/Release/ALife.dll, config/*.ltx, generated ALifeFactions.esp, build/papyrus/ALife_MCM.pex, papyrus/ALife_MCM.psc,
         docs/*.md and the previous release zip (ALife.esp + the 166 FaceGen faces; those are unchanged).
No game files are copied. The FaceGen data is the mod's own (generated with the Creation Kit from ALife.esp).
"""
import argparse, os, shutil, subprocess, sys, zipfile

ROOT = os.environ.get("ALIFE_ROOT", os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

ap = argparse.ArgumentParser()
ap.add_argument("--version", default="2.0.0")
ap.add_argument("--bdebug", action="store_true", help="ship ALife.ini with bDebug=1 (test builds)")
ap.add_argument("--base-zip", default=os.path.join(ROOT, "releases", "ALife-1.7.6h-slice1.zip"))
ap.add_argument("--mod", default=os.path.join(ROOT, "mod"), help="checkout to package (a worktree of mod/)")
ap.add_argument("--dll", default=os.path.join(ROOT, "build", "Release", "ALife.dll"), help="the ALife.dll to ship")
args = ap.parse_args()
MOD = args.mod

name = f"ALife-{args.version}-anomaly"
out = os.path.join(ROOT, "releases", name)
if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(out)

def put(src, dst_rel):
    dst = os.path.join(out, dst_rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)

# 0) the generated dialogue plugin (and config/talk.ltx, which maps its responses to actions; copied with the other configs below)
os.makedirs(out, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(MOD, "tools", "build_talk_esp.py"), os.path.join(out, "ALifeTalk.esp"),
                       os.path.join(MOD, "config", "talk.ltx")])

# 1) plugin + scripts + configs
put(args.dll, "SKSE/Plugins/ALife.dll")
for f in sorted(os.listdir(os.path.join(MOD, "config"))):
    if f.endswith(".ltx"):
        put(os.path.join(MOD, "config", f), f"SKSE/Plugins/ALife/{f}")
put(os.path.join(ROOT, "build", "papyrus", "ALife_MCM.pex"), "Scripts/ALife_MCM.pex")
put(os.path.join(MOD, "papyrus", "ALife_MCM.psc"), "Source/Scripts/ALife_MCM.psc")

# 2) the generated factions plugin
subprocess.check_call([sys.executable, os.path.join(MOD, "tools", "build_factions_esp.py"),
                       os.path.join(MOD, "config", "factions.ltx"), os.path.join(out, "ALifeFactions.esp")])

# 3) ALife.esp + FaceGen from the previous release (unchanged)
with zipfile.ZipFile(args.base_zip) as z:
    for n in z.namelist():
        if n == "ALife.esp" or n.startswith(("meshes/", "textures/")) or n == "docs/ALife/FACEGEN.md":
            z.extract(n, out)

# 4) default ini
ini = f"""; A-Life for Skyrim {args.version} (Anomaly edition). The MCM writes [General]; edit [Debug] by hand only.
[General]
Enabled=1
PopulationScale=1.0
OfflineCombat=1   ; 0 off, 1 only at smart terrains (default), 2 full
News=1
TalkInitiative=2   ; squads stopping you to talk: 0 never, 1 rarely, 2 normally, 3 often
Loot=1   ; squads strip the bodies they pass
PrepareUninstall=0

; Test switch. Never shown in the MCM. Hotkeys are DirectX scan codes.
; Numpad1=79 status dump, Numpad2=80 fast-forward one game day, Numpad3=81 place a hostile pair near you.
[Debug]
bDebug={1 if args.bdebug else 0}
bForceGate={1 if args.bdebug else 0}
sDebugStartup={"coc AbandonedShackExterior" if args.bdebug else ""}
sDebugInWorld={"tgm" if args.bdebug else ""}
sDebugAuto=
iDebugSeed=1
iDebugKeyStatus=79
iDebugKeyFight=80
iDebugKeyFillSticky=81
"""
os.makedirs(os.path.join(out, "SKSE", "Plugins"), exist_ok=True)
open(os.path.join(out, "SKSE", "Plugins", "ALife.ini"), "w", newline="\n").write(ini)

# 5) docs
os.makedirs(os.path.join(out, "docs", "ALife"), exist_ok=True)
for f in sorted(os.listdir(os.path.join(MOD, "docs"))):   # README, STANDING, CONVERSATIONS (FACEGEN.md comes with the base release)
    if f.endswith(".md") and f != "FACEGEN.md":
        shutil.copy2(os.path.join(MOD, "docs", f), os.path.join(out, "docs", "ALife", f))

# 6) zip (forward slashes so Vortex installs the full tree)
zpath = os.path.join(ROOT, "releases", name + ".zip")
if os.path.exists(zpath):
    os.remove(zpath)
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, dn, fn in os.walk(out):
        for f in fn:
            full = os.path.join(dp, f)
            z.write(full, os.path.relpath(full, out).replace(os.sep, "/"))
n = sum(len(fn) for _, _, fn in os.walk(out))
print(f"{name}: {n} files -> {zpath} ({os.path.getsize(zpath) // 1024} KB)")
