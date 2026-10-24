#!/usr/bin/env bash
# Runs every headless suite built by tests/linux/build.sh, then the harness modes. Prints one line per suite / mode.
# Usage: ALIFE_WORLD_DIR=<dir with world_skyrim.tsv, world_solstheim.tsv> bash tests/linux/run_all.sh
# Config: the repo's config/. Binaries: $ALIFE_BUILD_DIR (default <repo>/build/linux). Per-run logs: $ALIFE_LOG_DIR
# (default $ALIFE_BUILD_DIR/logs). World data is never in the repo.
# Each harness mode also writes its JSON report (--json, ticket G8) next to its log: $ALIFE_LOG_DIR/harness_<mode>.json;
# compare two with python3 tools/compare_harness.py a.json b.json.
# Reference runs (ticket G13, tests/linux/README.md "Harness reference runs"):
#   run_all.sh --compare          only the reference runs: 7 modes x seeds 1-3, each compared against
#                                 $ALIFE_WORLD_DIR/golden/<mode>_seed<N>.json (cpu fields ignored); exit 1 on any
#                                 difference, exit 2 if ALIFE_WORLD_DIR or its golden/ folder is missing
#   run_all.sh --update-golden    the same runs, written over $ALIFE_WORLD_DIR/golden/ (created if missing)
# The references are made from the world data (game-derived: location editor IDs in the news lines), so they live next
# to it, outside the repo, and are never committed (tests/golden/linux/*.json is git-ignored).
set -u
action=all
case "${1:-}" in
	"") ;;
	--compare) action=compare ;;
	--update-golden) action=update ;;
	*) echo "usage: run_all.sh [--compare | --update-golden]"; exit 2 ;;
esac
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
bin=${ALIFE_BUILD_DIR:-$repo/build/linux}
logs=${ALIFE_LOG_DIR:-$bin/logs}
cfg="$repo/config"
if [ -z "${ALIFE_WORLD_DIR:-}" ] || [ ! -f "$ALIFE_WORLD_DIR/world_skyrim.tsv" ]; then
	echo "set ALIFE_WORLD_DIR to the folder holding world_skyrim.tsv (and world_solstheim.tsv)"; exit 2
fi
world="$ALIFE_WORLD_DIR/world_skyrim.tsv"
mkdir -p "$logs"
cd "$repo" || exit 2
echo "config: $cfg"
echo "world:  $world"
echo "logs:   $logs"
rc=0
if [ "$action" != all ]; then
	# reference runs: the fast deterministic modes (not --diff-sweep / --longrun: slow and CPU-timed) at seeds 1-3
	golden="$ALIFE_WORLD_DIR/golden"
	glogs="$logs/golden"
	if [ "$action" = compare ] && [ ! -d "$golden" ]; then
		echo "no references: $golden is missing (local only, never in the repo); make them with --update-golden"; exit 2
	fi
	mkdir -p "$glogs" "$golden"
	if [ ! -x "$bin/harness" ]; then echo "harness          NOT BUILT"; exit 1; fi
	# stored and compared form of a report: the --config / --world values in "args" are this machine's paths, written as
	# <config> / <world>; every "cpu" object (CPU / wall ms, different on every run) is dropped, so the references hold
	# only simulation results and a regenerated reference diffs only where behaviour changed
	normalize() {
		python3 - "$1" "$2" <<'PY'
import json, sys
src, dst = sys.argv[1], sys.argv[2]
with open(src, encoding="utf-8") as f:
    d = json.load(f)
def strip_cpu(o):
    if isinstance(o, dict):
        o.pop("cpu", None)
        for v in o.values():
            strip_cpu(v)
    elif isinstance(o, list):
        for v in o:
            strip_cpu(v)
strip_cpu(d)
a = d.get("args", [])
for i in range(len(a) - 1):
    if a[i] in ("--config", "--world"):
        a[i + 1] = "<%s>" % a[i][2:]
with open(dst, "w", encoding="utf-8", newline="\n") as f:
    json.dump(d, f, indent=1, ensure_ascii=False)
    f.write("\n")
PY
	}
	gmodes=("default|" "mutants|--mutants" "m5g|--m5g" "solstheim|--solstheim" "m5d|--m5d" "m5f|--m5f" "dragons|--dragons")
	n=0; nsame=0
	for m in "${gmodes[@]}"; do
		tag=${m%%|*}; args=${m#*|}
		for seed in 1 2 3; do
			name="${tag}_seed$seed"
			log="$glogs/$name.log"; raw="$glogs/$name.raw.json"; json="$glogs/$name.json"; ref="$golden/$name.json"
			rm -f "$raw" "$json"
			n=$((n + 1))
			# shellcheck disable=SC2086
			"$bin/harness" --config "$cfg" --world "$world" $args --seed "$seed" --json "$raw" > "$log" 2>&1; ec=$?
			if [ ! -s "$raw" ]; then printf '%-18s FAIL exit %s, no JSON report (log %s)\n' "$name" "$ec" "$log"; rc=1; continue; fi
			if ! normalize "$raw" "$json"; then printf '%-18s FAIL cannot read %s\n' "$name" "$raw"; rc=1; continue; fi
			[ "$ec" -eq 0 ] || { printf '%-18s harness exit %s (log %s)\n' "$name" "$ec" "$log"; rc=1; }
			if [ "$action" = update ]; then
				[ "$ec" -eq 0 ] || { printf '%-18s not written: the run failed\n' "$name"; continue; }
				cp "$json" "$ref"; printf '%-18s written %s\n' "$name" "$ref"; continue
			fi
			if [ ! -f "$ref" ]; then printf '%-18s FAIL no reference %s\n' "$name" "$ref"; rc=1; continue; fi
			out=$(python3 tools/compare_harness.py --max-lines 20 "$ref" "$json"); cec=$?
			printf '%-18s %s\n' "$name" "$(echo "$out" | head -1)"
			if [ "$cec" -eq 0 ]; then nsame=$((nsame + 1)); else
				echo "$out" | tail -n +2
				echo "   (a = $ref, b = $json; all fields: python3 tools/compare_harness.py --max-lines 0 a b)"
				rc=1
			fi
		done
	done
	if [ "$action" = update ]; then echo "golden: $n references written to $golden"
	else echo "golden: $nsame/$n match the references in $golden"; fi
	echo "run_all: exit $rc"
	exit $rc
fi
# suites: every tests/build_*_test.bat; each takes <config dir> [<world tsv>] (the ones that don't need a world ignore it)
for bat in tests/build_*_test.bat; do
	name=$(basename "$bat" .bat); name=${name#build_}
	log="$logs/$name.run.log"
	if [ ! -x "$bin/$name" ]; then printf '%-16s NOT BUILT\n' "$name"; rc=1; continue; fi
	"$bin/$name" "$cfg" "$world" > "$log" 2>&1; ec=$?
	line=$(grep -E '[0-9]+ passed, [0-9]+ failed' "$log" | tail -1)
	pass=$(echo "$line" | sed -nE 's/.* ([0-9]+) passed, ([0-9]+) failed.*/\1/p')
	fail=$(echo "$line" | sed -nE 's/.* ([0-9]+) passed, ([0-9]+) failed.*/\2/p')
	if [ -n "$pass" ] && [ "$ec" -eq 0 ] && [ "$fail" -eq 0 ]; then
		printf '%-16s PASS %s/%s  exit %s\n' "$name" "$pass" "$((pass + fail))" "$ec"
	else
		printf '%-16s FAIL %s/%s  exit %s  (log %s)\n' "$name" "${pass:-?}" "$(( ${pass:-0} + ${fail:-0} ))" "$ec" "$log"
		grep '^FAIL' "$log" | head -20 | sed 's/^/   /'
		rc=1
	fi
done
# harness modes (docs/TESTLOG.md); each must exit 0
if [ ! -x "$bin/harness" ]; then echo "harness          NOT BUILT"; exit 1; fi
modes=("default|" "mutants|--mutants" "m5g|--m5g" "solstheim|--solstheim" "m5d|--m5d" "m5f|--m5f" "dragons|--dragons"
	"diff-sweep|--diff-sweep" "longrun|--longrun --days 120 --seed 1")
njson=0
for m in "${modes[@]}"; do
	tag=${m%%|*}; args=${m#*|}
	log="$logs/harness_$tag.log"
	json="$logs/harness_$tag.json"
	rm -f "$json"
	start=$(date +%s)
	# shellcheck disable=SC2086
	"$bin/harness" --config "$cfg" --world "$world" $args --json "$json" > "$log" 2>&1; ec=$?
	secs=$(( $(date +%s) - start ))
	last=$(grep -iE 'passed|failed|PASS|FAIL' "$log" | tail -1)
	if [ "$ec" -eq 0 ]; then st=PASS; else st=FAIL; rc=1; fi
	printf 'harness %-36s %s exit %s  %ss  %s\n' "${args:-(default)}" "$st" "$ec" "$secs" "$last"
	if [ -s "$json" ]; then njson=$((njson + 1)); else echo "   no JSON report written: $json"; rc=1; fi
done
echo "harness json: $njson/${#modes[@]} reports in $logs (harness_<mode>.json)"
echo "run_all: exit $rc"
exit $rc
