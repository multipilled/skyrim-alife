#!/usr/bin/env bash
# Linux build of every headless suite. The source list of each binary is read out of tests/build_<name>.bat (the
# Windows build is the one source of truth), so a suite added there builds here without editing this script.
# Usage: bash tests/linux/build.sh            (output: $ALIFE_BUILD_DIR, default <repo>/build/linux)
# Flags: g++ -std=c++20 -O2 -Wall, include/ plus tests/linux/shim (windows.h / psapi.h stand-ins).
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=${ALIFE_BUILD_DIR:-$repo/build/linux}
cxx=${CXX:-g++}
mkdir -p "$out"
cd "$repo"
echo "compiler: $($cxx --version | head -1)"
echo "output:   $out"
rc=0; n=0
for bat in tests/build_*.bat; do
	name=$(basename "$bat" .bat); name=${name#build_}
	# the cl command runs from its "cl " line to the "/Fe:" line; keep its .cpp arguments, %SRC%\ = repo root
	srcs=$(tr -d '\r' < "$bat" | sed -n '/^cl /,/\/Fe:/p' | tr ' ^' '\n\n' | grep -E '\.cpp$' | sed 's#^%SRC%\\##; s#\\#/#g')
	count=$(echo $srcs | wc -w)
	log="$out/$name.build.log"
	n=$((n + 1))
	if [ "$count" -eq 0 ]; then echo "== $name: no .cpp found in $bat"; rc=1; continue; fi
	missing=0
	for s in $srcs; do [ -f "$s" ] || { echo "== $name: $bat names $s, not in the checkout"; missing=1; }; done
	[ $missing -eq 0 ] || { rc=1; continue; }
	if $cxx -std=c++20 -O2 -Wall -Iinclude -Itests/linux/shim $srcs -o "$out/$name" > "$log" 2>&1; then
		warn=$(grep -c 'warning:' "$log")
		shimwarn=$(grep 'warning:' "$log" | grep -c 'tests/linux/shim/')
		echo "== $name: $count sources, build OK, $warn warning(s) (log $log)"
		grep 'warning:' "$log" | sed 's/^/   /'
		[ "$shimwarn" -eq 0 ] || { echo "   ERROR: $shimwarn warning(s) from tests/linux/shim"; rc=1; }
	else
		echo "== $name: $count sources, BUILD FAILED (log $log)"
		head -30 "$log" | sed 's/^/   /'
		rc=1
	fi
done
echo "built $n binaries from tests/build_*.bat, exit $rc"
exit $rc
