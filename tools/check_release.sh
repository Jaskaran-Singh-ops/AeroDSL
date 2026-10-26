#!/bin/sh
# Checks the prebuilt Windows and Linux x64 programs against a fresh build:
# every example with each of 15 phase options. Standard output, diagnostics
# (standard error) and exit status must all match. The Windows program runs
# under Wine. Output and diagnostics are compared separately, because the two
# streams can interleave differently when captured together.
#
#   make && sh tools/build_release.sh && sh tools/check_release.sh
cd "$(dirname "$0")/.."
opts="--tokens --ast --symbols --ir --opt-ir --cfg --ssa --nextuse --bc --trace --scn --stats --phases --json --no-opt"
tmp=$(mktemp -d)
n=0; bad=0
run() {  # name program args...
  name=$1; shift
  "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"; echo "exit $?" >>"$tmp/$name.out"
  tr -d '\r' <"$tmp/$name.out" >"$tmp/$name.o"; tr -d '\r' <"$tmp/$name.err" >"$tmp/$name.e"
}
for f in examples/*.aero; do
  for o in $opts; do
    run ref ./aerodsl $o "$f"
    run lin bin/linux-x64/aerodsl $o "$f"
    WINEDEBUG=-all run win wine bin/windows-x64/aerodsl.exe $o "$f"
    n=$((n+1))
    for p in lin win; do
      if ! cmp -s "$tmp/ref.o" "$tmp/$p.o" || ! cmp -s "$tmp/ref.e" "$tmp/$p.e"; then
        bad=$((bad+1)); echo "DIFF ($p) $o $f"
      fi
    done
  done
done
rm -rf "$tmp"
echo "runs: $n  mismatches: $bad"
