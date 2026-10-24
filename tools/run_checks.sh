#!/usr/bin/env bash
# One entry point for the Python tool checks (Linux box or any POSIX shell with python3).
#
#   bash tools/run_checks.sh                 # every tools/tests/test_*.py, check_config, CONFIG.md drift
#   CONFIG=/tmp/cfg bash tools/run_checks.sh # check another config dir (steps 2 and 3)
#
# Steps, in order:
#   1. every tools/tests/test_*.py through `python3 -m unittest -v` (one step per file)
#   2. python3 tools/check_config.py "$CONFIG"
#   3. python3 tools/gen_config_docs.py "$CONFIG" | diff - docs/CONFIG.md
# One PASS/FAIL/SKIP line per step with its count; a failing step's output is printed under its line.
# Exit 0 only if no step fails (a SKIP is not a failure). Exit 1 otherwise.
#
# A test file that needs Windows or game data says so in its first 30 lines and is skipped, with the reason, when
# the need is not met (the file is never deleted):
#   # run_checks: needs windows      -> runs only under MSYS/MinGW/Cygwin
#   # run_checks: needs game-data    -> runs only when ALIFE_WORLD_DIR names an existing directory
#
# Env: CONFIG (default <repo>/config), PYTHON (default python3), VERBOSE=1 prints every step's full output.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONFIG=${CONFIG:-$ROOT/config}
PYTHON=${PYTHON:-python3}
VERBOSE=${VERBOSE:-0}

steps=0 passed=0 failed=0 skipped=0
TMP=$(mktemp -d "${TMPDIR:-/tmp}/run_checks.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT

show() {   # show <file>: indented step output
    sed 's/^/    /' "$1"
}

result() { # result PASS|FAIL|SKIP <step> <detail> [<output file>]
    steps=$((steps + 1))
    case $1 in
        PASS) passed=$((passed + 1)) ;;
        FAIL) failed=$((failed + 1)) ;;
        SKIP) skipped=$((skipped + 1)) ;;
    esac
    printf '%-4s  %s: %s\n' "$1" "$2" "$3"
    if [ -n "${4:-}" ] && { [ "$1" = FAIL ] || [ "$VERBOSE" = 1 ]; }; then
        show "$4"
    fi
}

needs_unmet() { # needs_unmet <test file>: prints the skip reason, or nothing
    need=$(head -n 30 "$1" | sed -n 's/^#[[:space:]]*run_checks:[[:space:]]*needs[[:space:]]*\([A-Za-z-]*\).*/\1/p' | head -n 1)
    case $need in
        "") ;;
        windows)
            case $(uname -s) in
                MINGW*|MSYS*|CYGWIN*) ;;
                *) echo "needs Windows (this is $(uname -s))" ;;
            esac ;;
        game-data)
            if [ -z "${ALIFE_WORLD_DIR:-}" ] || [ ! -d "$ALIFE_WORLD_DIR" ]; then
                echo "needs game data (set ALIFE_WORLD_DIR to the extracted world data)"
            fi ;;
        *) echo "unknown need '$need' in its run_checks marker" ;;
    esac
}

cd "$ROOT" || exit 1
echo "run_checks: repo $ROOT"
echo "run_checks: config $CONFIG"

# 1. unit tests, one step per file
found=0
for t in tools/tests/test_*.py; do
    [ -f "$t" ] || continue
    found=$((found + 1))
    echo "run_checks: found $t"
done
if [ "$found" -eq 0 ]; then
    result FAIL "unittest" "no tools/tests/test_*.py found"
fi
for t in tools/tests/test_*.py; do
    [ -f "$t" ] || continue
    why=$(needs_unmet "$t")
    if [ -n "$why" ]; then
        result SKIP "unittest $t" "$why"
        continue
    fi
    out="$TMP/unittest.out"
    "$PYTHON" -m unittest -v "$t" >"$out" 2>&1
    rc=$?
    ran=$(sed -n 's/^Ran \([0-9][0-9]*\) tests\{0,1\} in .*/\1/p' "$out" | tail -n 1)
    status=$(grep -E '^(OK|FAILED|NO TESTS RAN)' "$out" | tail -n 1)
    skips=$(printf '%s\n' "$status" | sed -n 's/.*skipped=\([0-9][0-9]*\).*/\1/p')
    detail="${ran:-0} tests, ${skips:-0} skipped${status:+ ($status)}"
    if [ "$rc" -eq 0 ] && [ "${ran:-0}" -gt 0 ]; then
        result PASS "unittest $t" "$detail" "$out"
    else
        [ "${ran:-0}" -gt 0 ] || [ -n "$status" ] || detail="$detail, no test summary"
        result FAIL "unittest $t" "$detail, exit $rc" "$out"
    fi
done

# 2. config validator
out="$TMP/check_config.out"
"$PYTHON" tools/check_config.py "$CONFIG" >"$out" 2>&1
rc=$?
last=$(grep -E '^(OK|FAIL):|^error:|^usage:' "$out" | tail -n 1)
if [ "$rc" -eq 0 ]; then
    result PASS "check_config $CONFIG" "${last#OK: }" "$out"
else
    result FAIL "check_config $CONFIG" "${last:-no summary line}, exit $rc" "$out"
fi

# 3. CONFIG.md drift
gen="$TMP/CONFIG.md" out="$TMP/gen_config_docs.diff"
if "$PYTHON" tools/gen_config_docs.py "$CONFIG" >"$gen" 2>"$out"; then
    diff "$gen" docs/CONFIG.md >"$out" 2>&1
    rc=$?
    lines=$(wc -l <docs/CONFIG.md | tr -d ' ')
    if [ "$rc" -eq 0 ]; then
        result PASS "gen_config_docs $CONFIG | diff - docs/CONFIG.md" "identical, $lines lines" "$out"
    else
        n=$(grep -c '^[<>]' "$out")
        result FAIL "gen_config_docs $CONFIG | diff - docs/CONFIG.md" "$n lines differ (regenerate docs/CONFIG.md)" "$out"
    fi
else
    result FAIL "gen_config_docs $CONFIG | diff - docs/CONFIG.md" "generator failed" "$out"
fi

echo "run_checks: $steps steps, $passed passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ] || exit 1
exit 0
