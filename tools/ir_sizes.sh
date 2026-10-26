#!/bin/sh
# Three-address instructions per example, before and after the optimiser:
# the numbers in the review deck's "How we know it works" chart.
#
#   sh tools/ir_sizes.sh
cd "$(dirname "$0")/.."
count() { ./aerodsl "$1" "$2" 2>/dev/null | grep -cE '^ +[0-9]+ '; }
printf '%-26s %6s %6s\n' example before after
for f in 01_minimal 02_converging 03_procedures 04_conditional 05_loop \
         06_optimise 11_boundaries 12_separation 13_walkthrough; do
  printf '%-26s %6s %6s\n' "$f" "$(count --ir examples/$f.aero)" "$(count --opt-ir examples/$f.aero)"
done
