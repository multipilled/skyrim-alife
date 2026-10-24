"""Pre-release lint of an A-Life package (a staged folder or the zip). Project rule: no Anomaly files, nothing from GAMMA,
no game files, no keys. Read only: the reference trees are only stat-ed and hashed.

  python -I tools/publish_lint.py <folder or .zip> [--report out.txt] [--game <Skyrim Data>] [--ref <dir>]...

FAIL (exit 1):
  - a shipped file byte-identical to a file in the game's Data folder, the local Anomaly reference (ref/anomaly) or a GAMMA install
    (indexed by size, hashed only where a size matches);
  - a file type outside the release allow list; a .env file; anything that looks like an API key or token;
  - an ALife.ini with bDebug=1, bForceGate=1 or a debug start / in-world queue;
  - a plugin whose masters are not the base game / DLC masters or A-Life's own plugins;
  - a script source (.psc) or config shipped without its compiled / runtime counterpart, and the reverse (.pex without .psc).
WARN: absolute user paths in shipped text, decompiler fingerprints (FUN_/DAT_/sub_ names), no README, Anomaly script
paths or line citations in shipped docs (design notes: allowed, but they are citations, not files), files over 8 MB.
"""
import argparse
import hashlib
import io
import os
import re
import struct
import sys
import zipfile

DEFAULT_REFS = [r"..\ref\anomaly", r"..\GAMMA"]
DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data"
ALLOWED = {".esp", ".esl", ".dll", ".pex", ".psc", ".ltx", ".ini", ".md", ".txt", ".nif", ".dds", ".json"}
MASTERS_OK = {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm", "alife.esp", "alifefactions.esp", "alifetalk.esp"}
SECRET = re.compile(rb"(sk-ant-[A-Za-z0-9_-]{20,}|sk-[A-Za-z0-9]{32,}|ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[0-9A-Z]{16}|"
                    rb"xox[abpr]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35}|-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----|"
                    rb"(api[_-]?key|secret|token|password)\s*[=:]\s*['\"][^'\"\s]{12,})", re.I)
USERPATH = re.compile(rb"([A-Za-z]:\\\\?Users\\\\?[^\\\s]+|/home/[a-z]+/|[A-Za-z]:\\\\?(GAMMA|SteamLibrary))", re.I)
DECOMP = re.compile(rb"\b(FUN_[0-9a-fA-F]{6,}|DAT_[0-9a-fA-F]{6,}|sub_[0-9A-F]{6,}|Decompiled with)\b")
ANOMALY_CITE = re.compile(rb"\b[a-z_]+\.script(:\d+)?\b")
TEXT = {".ltx", ".ini", ".md", ".txt", ".psc", ".json"}


def items(src):
    """(relative path, bytes) of every file in a folder or zip."""
    if src.lower().endswith(".zip"):
        with zipfile.ZipFile(src) as z:
            for n in z.namelist():
                if not n.endswith("/"):
                    yield n.replace("\\", "/"), z.read(n)
    else:
        for dp, dn, fn in os.walk(src):
            for f in fn:
                full = os.path.join(dp, f)
                yield os.path.relpath(full, src).replace(os.sep, "/"), open(full, "rb").read()


def size_index(roots):
    idx = {}
    for r in roots:
        if not os.path.isdir(r):
            continue
        for dp, dn, fn in os.walk(r):
            for f in fn:
                p = os.path.join(dp, f)
                try:
                    idx.setdefault(os.path.getsize(p), []).append(p)
                except OSError:
                    pass
    return idx


def sha(b):
    return hashlib.sha256(b).hexdigest()


def masters(data):
    """MAST names of a TES4 plugin."""
    if data[:4] != b"TES4":
        return None
    size = struct.unpack_from("<I", data, 4)[0]
    body, o, out = data[24:24 + size], 0, []
    while o + 6 <= len(body):
        t = body[o:o + 4]
        n = struct.unpack_from("<H", body, o + 4)[0]
        if t == b"MAST":
            out.append(body[o + 6:o + 6 + n].rstrip(b"\0").decode("latin1"))
        o += 6 + n
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--report")
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--ref", action="append")
    a = ap.parse_args()
    refs = a.ref or DEFAULT_REFS
    fails, warns, lines = [], [], []
    files = list(items(a.src))
    lines.append(f"package: {a.src} ({len(files)} files, {sum(len(b) for _, b in files) // 1024} KB)")
    roots = [a.game] + refs
    idx = size_index(roots)
    lines.append("reference trees: " + ", ".join(f"{r} ({'present' if os.path.isdir(r) else 'MISSING'})" for r in roots) +
                 f"; {sum(len(v) for v in idx.values())} files indexed by size")
    hashed = {}
    names = {p.lower() for p, _ in files}
    for path, data in files:
        ext = os.path.splitext(path)[1].lower()
        base = os.path.basename(path).lower()
        if ext not in ALLOWED:
            fails.append(f"{path}: file type {ext or '(none)'} is not on the release allow list")
        if base == ".env" or base.endswith(".env"):
            fails.append(f"{path}: .env file")
        if len(data) > 64:
            h = sha(data)
            for cand in idx.get(len(data), []):
                if cand not in hashed:
                    try:
                        hashed[cand] = sha(open(cand, "rb").read())
                    except OSError:
                        hashed[cand] = None
                if hashed[cand] == h:
                    fails.append(f"{path}: byte-identical to {cand}")
                    break
        if SECRET.search(data):
            fails.append(f"{path}: looks like it holds a key or token ({SECRET.search(data).group(0)[:12]!r}...)")
        if ext in TEXT:
            if m := USERPATH.search(data):
                warns.append(f"{path}: absolute local path {m.group(0).decode('latin1', 'replace')!r}")
            if m := DECOMP.search(data):
                warns.append(f"{path}: decompiler fingerprint {m.group(0).decode()!r}")
            if ext == ".md" and (c := len(ANOMALY_CITE.findall(data))):
                warns.append(f"{path}: {c} citations of Anomaly script names (design references, not Anomaly files)")
        if base == "alife.ini":
            t = data.decode("latin1")
            for k, bad in (("bDebug", "1"), ("bForceGate", "1"), ("bBackground", "1")):
                if re.search(rf"(?m)^{k}[ \t]*=[ \t]*{bad}\b", t):
                    fails.append(f"{path}: {k}={bad} (test build ini)")
            for k in ("sDebugStartup", "sDebugInWorld", "sDebugAuto"):
                if re.search(rf"(?m)^{k}[ \t]*=[ \t]*[^\s;]", t):   # [ \t]: an empty value must not match the next line
                    fails.append(f"{path}: {k} is set (test build ini)")
        if ext in (".esp", ".esl", ".esm"):
            ms = masters(data)
            if ms is None:
                fails.append(f"{path}: not a TES4 plugin")
            else:
                bad = [m for m in ms if m.lower() not in MASTERS_OK]
                lines.append(f"plugin {path}: masters {', '.join(ms)}")
                if bad:
                    fails.append(f"{path}: unexpected masters {bad}")
        if len(data) > 8 * 1024 * 1024:
            warns.append(f"{path}: {len(data) // (1024 * 1024)} MB")
        if ext == ".pex" and not any(n.endswith("source/scripts/" + base.replace(".pex", ".psc")) for n in names):
            fails.append(f"{path}: compiled script without its source in Source/Scripts")
        if ext == ".psc" and ("scripts/" + base.replace(".psc", ".pex")) not in names:
            fails.append(f"{path}: script source without Scripts/{base.replace('.psc', '.pex')}")
    if not any(p.lower().endswith("readme.md") for p, _ in files):
        warns.append("no README.md in the package")
    lines.append(f"\nFAIL: {len(fails)}")
    lines += ["  " + f for f in fails]
    lines.append(f"WARN: {len(warns)}")
    lines += ["  " + w for w in warns]
    lines.append("RESULT " + ("FAIL" if fails else "PASS"))
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if a.report:
        open(a.report, "w", encoding="utf-8", newline="\n").write(text)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
