#!/bin/sh
# Checks that every example behaves as its header says it does: the valid
# ones compile, run and exit 0; the ones written to fail exit 1.
set -u
fails=0
pass() { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s: %s\n' "$1" "$2"; fails=$((fails+1)); }

for f in examples/01_minimal.aero examples/02_converging.aero \
         examples/03_procedures.aero examples/04_conditional.aero \
         examples/05_loop.aero examples/06_optimise.aero \
         examples/13_walkthrough.aero examples/15_longer_hold.aero \
         examples/11_boundaries.aero examples/12_separation.aero; do
  if ./aerodsl "$f" >/dev/null 2>/dev/null; then pass "$f"; else bad "$f" "should compile and run"; fi
done

for f in examples/07_lexical_errors.aero examples/08_syntax_errors.aero \
         examples/09_type_errors.aero examples/10_domain_errors.aero \
         examples/14_review_errors.aero examples/16_four_mistakes.aero \
         examples/17_scope.aero; do
  if ./aerodsl "$f" >/dev/null 2>/dev/null; then bad "$f" "should have been rejected"; else pass "$f"; fi
done

# Every phase flag must work on a real scenario without crashing.
for flag in --tokens --ast --symbols --ir --opt-ir --cfg --ssa --nextuse --bc \
            --trace --scn --stats --phases --json --no-opt --no-sim; do
  if ./aerodsl "$flag" examples/02_converging.aero >/dev/null 2>/dev/null; then
    pass "$flag"
  else
    bad "$flag" "failed on examples/02_converging.aero"
  fi
done

# The compiler must read standard input too.
if ./aerodsl --no-sim < examples/01_minimal.aero >/dev/null 2>/dev/null; then
  pass "reads standard input"
else
  bad "stdin" "could not compile from standard input"
fi

if [ "$fails" -eq 0 ]; then
  printf 'all example checks passed\n'
else
  printf '%d example check(s) failed\n' "$fails"
  exit 1
fi
