# Reproducing the review slides

Every code sample, output, message and number in the Review 2 and Review 3
decks (`slides/AeroDSL_Review2_Design_Architecture.pptx`, 17 slides, and
`slides/AeroDSL_Review3_Implementation_Results.pptx`, 19 slides) comes from a
file in this repository. These tables give the command that reproduces each
slide. Run the commands from the repository root after `make`; on Windows use
`bin\windows-x64\aerodsl.exe` (or just `aerodsl`) in place of `./aerodsl`.

Slides show the programs without their header comments and blank lines.
Everything else, including every line:column position, is exactly what the
compiler prints.

## Review 2: design and architecture

| Slide | What it shows | Command |
|---|---|---|
| 1 | title | |
| 2 | index | |
| 3 | the problem statement | Review 1, `slides/AeroDSL_Review1.pptx` |
| 4 | the whole compiler, top to bottom | `./aerodsl --phases examples/13_walkthrough.aero` |
| 5 | example 5 | `./aerodsl --scn examples/05_loop.aero` |
| 6 | the grammar | `docs/SPEC.md` section 4 |
| 7 | the parse tree of line 14 | `./aerodsl --tokens --ast examples/13_walkthrough.aero` |
| 8 | tokens, tree and checks | `./aerodsl --tokens --ast --symbols examples/13_walkthrough.aero` |
| 9 | three-address code before and after | `./aerodsl --ir --opt-ir --stats examples/13_walkthrough.aero` |
| 10 | the scenario file | `./aerodsl --bc --trace --scn examples/13_walkthrough.aero` |
| 11 | the `Token` and `Ir` structs | `src/aerodsl.h` (the slide shortens the comments) |
| 12 | the symbol table and the scope error | `./aerodsl --symbols examples/17_scope.aero` |
| 13 | jumping code and a control-flow graph | `./aerodsl --ir examples/04_conditional.aero` and `./aerodsl --cfg --ssa examples/05_loop.aero` |
| 14 | the optimiser report | `./aerodsl --ir --opt-ir --stats examples/06_optimise.aero` |
| 15 | four decisions; a range error hidden in a variable | `./aerodsl examples/14_review_errors.aero` |
| 16 | every stage, its files and its size | `wc -l src/*.c src/*.h` for the line counts; `./aerodsl --help` for the flags |
| 17 | thank you | |

## Review 3: implementation and results

| Slide | What it shows | Command |
|---|---|---|
| 1 | title | |
| 2 | index | |
| 3 | the abstract | its numbers come from slides 8, 10, 12 and 13 |
| 4 | the problem statement | Review 1, `slides/AeroDSL_Review1.pptx` |
| 5 | the whole compiler | `./aerodsl --phases examples/13_walkthrough.aero` |
| 6 | example 5 | `./aerodsl --scn examples/05_loop.aero` |
| 7 | sample output | `./aerodsl --trace examples/05_loop.aero` and `./aerodsl examples/05_loop.aero` |
| 8 | the BlueSky file and example 3 | `./aerodsl examples/03_procedures.aero` prints the left side; it is saved as `bluesky/03_procedures.scn` |
| 9 | one hold made longer | `./aerodsl examples/15_longer_hold.aero`, then compare with `bluesky/03_procedures.scn` |
| 10 | four mistakes | BlueSky: `python bluesky/run_bluesky.py bluesky/four_mistakes.scn 600 120`. AeroDSL: `./aerodsl examples/16_four_mistakes.aero`, then change `AI220` to `AI202` on line 14 and compile again |
| 11 | the scorecard | `grep -cv '^#\|^$' bluesky/03_procedures.scn examples/03_procedures.aero` gives 20 and 24; the other rows come from slides 8 to 10 and 12 |
| 12 | BlueSky's climb against ours | `python bluesky/run_bluesky.py bluesky/05_loop.scn 300 10`, and `bluesky/05_loop_atalt.scn` for the ATALT version |
| 13 | the chart and the test numbers | `sh tools/ir_sizes.sh` for the chart; `make test` for the 336 checks, the 18-scenario differential test and the 34 example checks; `make fuzz` for the 3,000 inputs; `sh tools/check_release.sh` for the Windows and Linux comparison |
| 14 | the demo page | `./run-demo.sh`, or double-click `run-demo.bat`, then open `http://127.0.0.1:8080`. Every sample on the page is a file in `examples/`, named with the slides that use it |
| 15 | the GitHub repository | this repository |
| 16 | the steps to run it | `README.md` |
| 17 | limits and next steps | `docs/STATUS.md` |
| 18 | references | `docs/REVIEW.md`, Sources |
| 19 | thank you | |

BlueSky needs `pip install bluesky-simulator==1.1.1`; see `bluesky/README.md`
for what each replay should show.

If you change an example, run `python3 tools/sync_ui_examples.py` so the demo
page compiles the same file.
