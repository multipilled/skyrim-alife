# Linux headless build

Builds and runs every headless suite and the harness on Linux with g++ (14.2 tested), from a clean checkout.

```
bash tests/linux/build.sh                                   # every tests/build_*.bat -> build/linux/<name>
ALIFE_WORLD_DIR=/path/to/data bash tests/linux/run_all.sh   # suites + harness modes, one line each
```

- Source lists come from `tests/build_*.bat` (the `cl` line through `/Fe:`), so a suite added for Windows builds here
  without editing the script. Flags: `-std=c++20 -O2 -Wall -Iinclude -Itests/linux/shim`. The build fails on any
  warning from the shims.
- `run_all.sh` runs every `build_*_test.bat` suite as `<suite> config <world_skyrim.tsv>` and the harness with
  default, `--mutants`, `--m5g`, `--solstheim`, `--m5d`, `--m5f`, `--dragons`, `--diff-sweep`,
  `--longrun --days 120 --seed 1`. Exit 0 only if every suite has 0 failures and every run exits 0.
- `ALIFE_WORLD_DIR` must hold `world_skyrim.tsv` and `world_solstheim.tsv` (game-derived data, never in the repo).
- `ALIFE_BUILD_DIR` (default `build/linux`, git-ignored) and `ALIFE_LOG_DIR` (default `$ALIFE_BUILD_DIR/logs`)
  move the binaries and logs; `CXX` picks the compiler.
- Shims (`shim/windows.h`, `shim/psapi.h`): `QueryThreadCycleTime` returns this thread's CPU time in nanoseconds
  (CLOCK_THREAD_CPUTIME_ID), not cycles, so "Mcycles" figures are CPU-milliseconds; `GetTickCount64` is
  CLOCK_MONOTONIC; the process memory stat fails, so the harness's working set reads 0.0 MB.

## Harness reference runs (drift detection)

```
ALIFE_WORLD_DIR=/path/to/data bash tests/linux/run_all.sh --update-golden  # write the references from this checkout
ALIFE_WORLD_DIR=/path/to/data bash tests/linux/run_all.sh --compare        # 21 runs vs the references, one line each
```

- **Local only, never committed.** The references are harness reports made from the world data in `ALIFE_WORLD_DIR`,
  which is game-derived (the news lines name Skyrim locations by editor ID). So they live next to that data, in
  `$ALIFE_WORLD_DIR/golden/<mode>_seed<N>.json`, outside the repo, and `tests/golden/linux/*.json` is git-ignored.
  An early version of ticket G13 committed them (commit 8d22ccf, removed again since): don't add them back.
- The runs: the harness `--json` report (ticket G8) on Linux for the default, `--mutants`, `--m5g`, `--solstheim`,
  `--m5d`, `--m5f` and `--dragons` modes at `--seed 1`, `2` and `3`, with the repo's `config/`. `--diff-sweep` and
  `--longrun` are left out: slow, and the longrun checks are CPU-timed.
- `--update-golden` creates `$ALIFE_WORLD_DIR/golden/` if missing and writes the 21 references; it does not write a
  reference for a run that exits non-zero. `--compare` runs the same 21 (no suites), compares each report with
  `python3 tools/compare_harness.py <reference> <fresh>`, and prints `identical` per run, or `differs: N field(s)` and
  the first 20 changed fields with their values. Exit 0 only if every run writes a report, exits 0 and is identical;
  1 on any difference; 2 if `ALIFE_WORLD_DIR` is unset or has no `world_skyrim.tsv`, or `$ALIFE_WORLD_DIR/golden/`
  doesn't exist. The fresh reports and logs are in `$ALIFE_LOG_DIR/golden/`.
- Stored form: the `--config` / `--world` values in `args` are written as `<config>` / `<world>` (they are machine
  paths), and every `cpu` object (CPU and wall milliseconds) is dropped. Everything else must match exactly. The harness
  is deterministic: two runs of each mode and seed on the same box differ only in `cpu` fields.
- Needs: `bash tests/linux/build.sh` first (the binaries in `$ALIFE_BUILD_DIR`), and g++ on x86-64 Linux. Another
  compiler, flags or CPU can change float results; the references are for this Linux build and this world data only.
- How to use it for a merge: build master and run `--update-golden` (the baseline), then build the branch and run
  `--compare`. A `differs` on a branch with no intended behaviour change (C++ under `src/` or `include/`,
  `config/*.ltx`, the harness, the world data) is a regression to find. For an intended change, check that the listed
  fields are the ones the change should move. After master moves, run `--update-golden` again on the new master.

## Python tool checks

```
bash tools/run_checks.sh                    # every tools/tests/test_*.py, check_config, CONFIG.md drift
CONFIG=/tmp/cfg bash tools/run_checks.sh    # same, with check_config and the CONFIG.md diff on another config dir
```

- Steps, in order: each `tools/tests/test_*.py` through `python3 -m unittest -v`, then
  `python3 tools/check_config.py "$CONFIG"`, then `python3 tools/gen_config_docs.py "$CONFIG" | diff - docs/CONFIG.md`.
  One `PASS`/`FAIL`/`SKIP` line per step with its count (tests and skips, files and checks, diff lines); a failing
  step's output is printed under its line. Exit 0 only if no step fails; 1 otherwise.
- A test file that needs Windows or game data marks itself in its first 30 lines with `# run_checks: needs windows`
  or `# run_checks: needs game-data`; it is skipped with the reason unless it runs under MSYS/MinGW/Cygwin or
  `ALIFE_WORLD_DIR` names an existing directory. No file under `tools/tests/` needs either today.
- `PYTHON` picks the interpreter (default `python3`); `VERBOSE=1` prints every step's full output. Needs no build
  (`test_ltxlib.py` compiles `src/core/Ltx.cpp` itself with `g++` or `clang++`, and skips that class without one).
