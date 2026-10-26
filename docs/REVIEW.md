# AeroDSL — Project Review

A refinement review of the AeroDSL compiler project against the Compiler Design
(BCSE307L) syllabus, carried out before anything was changed, together with the
scope recommendation, problem statement, build evidence and remaining weaknesses
that follow from it.

Everything in §1, §5, §7 and §8 was checked by reading the source and running
the project. Where something could not be verified, it says so.

Contents:

1. [Audit of the existing project](#1-audit-of-the-existing-project)
2. [Recommended student scope](#2-recommended-student-scope)
3. [The problem, with its evidence](#3-the-problem-with-its-evidence)
4. [The language and the compiler, in summary](#4-the-language-and-the-compiler-in-summary)
5. [Syllabus mapping](#5-syllabus-mapping)
6. [Animation record](#6-animation-record)
7. [Build and test results](#7-build-and-test-results)
8. [Remaining technical weaknesses](#8-remaining-technical-weaknesses)
9. [What changed in this pass, and why](#9-what-changed-in-this-pass-and-why)

---

## 1. Audit of the existing project

The rule applied throughout: a feature counts as implemented only if it was
found in the source and observed working, not because a document claims it.

### 1.1 Phases, as found

| Phase | State | Evidence |
|---|---|---|
| Lexical analysis | **Implemented** | `src/lexer.c`, 296 lines. Hand-written single pass. Verified: `--tokens` emits 31 typed tokens for the walkthrough example, with `FL240 → 24000` and `280KT → 280`. |
| Syntax analysis | **Implemented** | `src/parser.c`, 925 lines. Recursive descent, one function per production, one function per precedence level. Verified: `--ast` prints a typed tree; error recovery resynchronises at statement-line starts and reports ten errors in `examples/09`. |
| Symbol table | **Implemented** | `src/symtab.c`. Flat `all` array for ownership plus a visibility `stack`. Verified: `--symbols`. Procedure bodies cannot see main-body variables (a deliberate barrier). |
| Semantic analysis | **Implemented** | `src/sema.c`, 482 lines. Four types, no implicit conversions, arity checking, nine warnings. Verified by running each of `examples/07`–`10`. |
| Intermediate code | **Implemented** | `src/ir.c`, 639 lines. Quadruples; short-circuit booleans by backpatching (`makelist`/`merge`/`backpatch`). Verified: `--ir`. |
| Control-flow analysis | **Implemented** | `src/cfg.c`, 490 lines. Basic blocks, iterative dominators, immediate dominators, back edges, natural loops, live-variable dataflow. Verified: `--cfg`. |
| SSA | **Implemented but unused** | `src/ssa.c`, 435 lines. Dominance frontiers, pruned phi placement, renaming over the dominator tree (Cytron). Verified: `--ssa` prints correct frontiers and renamed code. **No optimisation pass consumes it.** This is the single largest gap between what exists and what is used. |
| Optimisation | **Implemented** | `src/opt.c`, 885 lines. Seven passes iterated to a fixed point (24-round cap). Verified: `--stats` on `examples/06` shows all ten counters non-zero. |
| Domain analysis | **Implemented** | `src/analysis.c`, 286 lines. Operand range checks and clearance consistency, run on the *optimised* IR. Verified: `examples/14` is rejected with exit status 1. |
| Code generation | **Implemented** | `src/codegen.c`, 739 lines. Compare-branch fusion, next-use information, 16-register allocation with spilling by furthest next use, peephole. Verified: `--bc`, `--nextuse`. |
| Execution | **Implemented** | `src/vm.c`, 826 lines. Register VM with activation records, kinematic flight model, separation monitor. Verified: `--trace`. |
| Scenario output | **Implemented** | `src/scn.c`, 104 lines. Verified: `--scn`. Format checked against published documentation only — see §3.6. |
| Runtime verification | **Implemented** | `assert` statements evaluated during simulation. Verified: `examples/02` and `examples/12`. |
| Browser demonstration | **Implemented** | `ui/index.html`, 1,055 lines, 12 tabs, served by `make ui`. |

### 1.2 Real algorithms, named

These are textbook algorithms genuinely implemented rather than approximated:

- **Backpatching** with truelists and falselists for short-circuit boolean
  translation (`ir.c`).
- **Iterative dominator computation** by set intersection over the reverse
  postorder, then immediate dominators, back edges by dominance, and natural
  loops by backward reachability (`cfg.c`).
- **Live-variable dataflow** to a fixed point, backwards over the CFG (`cfg.c`).
- **Cytron's SSA construction**: dominance frontiers, pruned phi placement
  guided by liveness, and renaming by a walk of the dominator tree (`ssa.c`).
- **Constant propagation** over a three-point lattice (undefined / constant /
  unknown), with meet at join points (`opt.c`).
- **DAG-based local value numbering** for common-subexpression elimination,
  with copy propagation and algebraic identities on the same structure
  (`opt.c`).
- **Loop-invariant code motion** guarded by a liveness condition on loop exit
  edges (`opt.c`).
- **Next-use information** and register allocation that spills the register
  whose next use is furthest away, writing back only if dirty (`codegen.c`).

### 1.3 Documentation claims that were checked

Two documentation audits were run against the source — 33 findings, then 13 on
re-check. All were resolved. The ones worth recording, because they are the
kind of thing an examiner tests:

| Claim found in the docs | Reality | Resolution |
|---|---|---|
| `SPEC` §3.4: `FLXYZ` is a lexical error | It lexes as a CALLSIGN | Documentation corrected |
| `SPEC` §7.5 lists seven warnings | The code emits nine | Table rewritten |
| `SPEC` §7.1 rule 8: unknown field is a semantic error | It is a **syntax** error | Documentation corrected |
| `SPEC` §7.2: the semantic analyser does no range checking | Latitude and longitude are the exception | Documentation corrected |
| Division by a propagated zero is caught | It was not — only literal zeros were | **Code fixed**: `check_divisor` added to `analysis.c` |
| `SPEC` §7.4: the VM catches a "climb that does not climb" | No such check existed | **Code fixed**: VM check added; `analysis.c` sets `ir->bop` so the static and runtime checks never both fire |
| `SPEC` §10.6: three barrier instructions | There are four (`clear`, `advance`, `enter`, `call`) | Documentation corrected |
| `ARCHITECTURE` §6's worked IR example | It was invented, not generated | Replaced with real `--ir` output |
| A `parse_or` function | Does not exist; it is `parse_expr` | Documentation corrected |
| "Fuzzing" was claimed | No harness existed in the tree | **Code added**: `tests/fuzz.py` and a `make fuzz` target |
| `scn.c` labelled all emitted events "clearances" | Entries are not clearances | **Code fixed**: counted separately |
| Several counts ("eleven topics", "six lexical errors") | Off by one in four places | Corrected against actual output |

The lesson that generalises: of twelve specific claims spot-checked, four were
wrong in a way that required a code change, not a documentation change.

### 1.4 Defects found and fixed during the rebuild

Recorded because every one of them is a plausible viva question.

1. **Scope popping left symbols visible.** `symtab_pop` only decremented a
   depth counter. Fixed by separating ownership (`all`) from visibility
   (`stack`) and truncating the stack.
2. **Parser resynchronised after successful statements**, silently swallowing
   the stray `2` in `advance 1 2`. Fixed by syncing only when the statement
   actually produced a diagnostic.
3. **`sync_to` unconditionally consumed a token**, eating the statement it was
   meant to resume at. Fixed by relying on the caller's progress guard.
4. **LICM hoisted a store that was live after the loop.** `let x = 1; while c
   do x = 5 … end; print x` printed 5 when the loop never ran. Fixed with a
   dead-on-every-exit-edge condition.
5. **SSA renaming silently produced `.undef` everywhere**, because `cfg_build`
   clears `idom` and codegen rebuilds the graph. The frontiers still looked
   plausible, which is what made it dangerous. Fixed by having `ssa_print`
   recompute dominators and liveness itself.
6. **Copy and constant propagation counted no-op rewrites**, so the fixed point
   was never reached. Fixed with equality guards.
7. **Print output split across events** because register loads sit between
   print instructions. Fixed with an end-of-statement marker carried through
   the IR into the bytecode.
8. **Eleven parser memory leaks** on error paths, found by AddressSanitizer.
9. **Clearance direction check was too strict**, so a loop re-issuing the same
   level warned spuriously. Changed to strict comparison.

### 1.5 Unnecessary complexity, identified

Nothing was removed — see §2 — but these are the parts that cost the most and
return the least *for a review*:

- **SSA construction (435 lines).** Correct, printable, and consumed by
  nothing. Its only present value is as a syllabus artefact.
- **The `--nextuse` dump.** Useful once, when debugging the allocator.
- **The 12-tab browser UI (1,055 lines).** Excellent for a demonstration,
  irrelevant to the compiler argument.
- **The fuzz harness.** It is evidence, not function.

### 1.6 Essential features, identified

Remove any of these and either the pipeline stops or the project's claim stops
being true: lexer, parser, AST, symbol table, type checking, three-address code
with backpatching, basic blocks, constant folding and propagation, dead-code
elimination, code generation, the VM, the scenario-file writer, the domain range
and clearance checks, and the state-barrier rule.

---

## 2. Recommended student scope

**Recommendation: change nothing in the code; change what the presentation puts
in front of the examiner.** The project is not too large to defend — it is too
large to *present* in fifteen minutes. The right response is a presentation
hierarchy, not deletion.

> Explicitly: no working feature is removed, and none was removed in this pass.
> Working, tested, documented code is not worth deleting to look smaller.

| Feature | Tier | Reasoning |
|---|---|---|
| Lexer, parser, AST, symbol table | **Must have** | Without them there is no compiler. Both are hand-written, which is itself defensible. |
| Type checking, four types, no implicit bool | **Must have** | Carries Module 3. The no-implicit-bool decision is a domain argument, not a limitation. |
| Three-address code + backpatching | **Must have** | Carries Module 4 and is the most commonly examined single technique in it. |
| Basic blocks + CFG | **Must have** | Prerequisite for everything in Module 5 and for the domain analysis. |
| Constant propagation, folding, dead code | **Must have** | The smallest set of passes that makes the headline example work. |
| Code generation + bytecode | **Must have** | Carries Module 6. |
| The AeroVM | **Must have** | The scenario must execute to produce its own timeline. |
| Scenario-file output | **Must have** | This is what makes the project a tool rather than an exercise. |
| Domain range + clearance checks | **Must have** | The only part no general-purpose compiler could have. The project's actual contribution. |
| The state-barrier rule | **Must have** | The soundness condition. Without it the optimiser is wrong, not just conservative. |
| DAG-based CSE, copy propagation, algebraic identities | **Nice to have** | Real Module 5 topics, cheap to explain once blocks exist. Keep; show if asked. |
| Loop-invariant code motion | **Nice to have** | Strongest demonstration of the barrier rule. Keep; show if time. |
| Dominators and natural loops | **Nice to have** | Syllabus topics; prerequisites for LICM and SSA. |
| Next-use register allocation with spilling | **Nice to have** | A Module 6 topic in its own right. |
| Peephole optimisation | **Nice to have** | Two rewrites; one slide at most. |
| Browser demonstration | **Nice to have** | Excellent if the room has a projector and five spare minutes. |
| SSA construction | **Held in reserve** | Correct and complete, but unused. Answers "is there more?" rather than "what is this?". |
| CFG and next-use dumps | **Held in reserve** | Debugging aids. Show only on a direct question. |
| Fuzz harness | **Held in reserve** | Evidence, produced on request. |
| Arrays, records, modules | **Out of scope** | Scope. They would not strengthen any syllabus claim. |
| Procedures returning values | **Out of scope** | Scope. Procedures without return values already carry activation records and parameter passing. |
| Global register allocation, global CSE | **Out of scope** | Substantial work for no new syllabus coverage; local versions already demonstrate both. |
| Unit-aware types | **Out of scope, but the best next extension** | Would close the real soundness gap in §8.2. Deferred for time, not for value. |
| Any link to real aviation data or systems | **Out of scope, permanently** | A hard line. The project is an educational prototype and nothing in it should suggest otherwise. |

### 2.1 Roadmap, in order

1. **Use the SSA form in the optimiser.** Highest value: it turns 435 lines of
   reserve into working machinery, and sparse conditional constant propagation
   on SSA would strictly improve the pass that the domain analysis depends on.
2. **Make units part of the type system.** `clear AI202 climb 280` should not
   type-check. This is the one place the language looks safer than it is.
3. **Compile altitude waits into BlueSky's `ATALT`.** The generated files now
   load and run in BlueSky 1.1.1, which also showed a turn coming about
   1,060 ft early, because BlueSky's A320 climbs more slowly than our model.
4. **Replace the kinematic flight model** with something that has a
   performance envelope. Lowest priority: it does not change the compiler
   argument, only the realism of the output.

---

## 3. The problem, with its evidence

Every claim below carries a tier:
**[V]** verified from this repository or from primary documentation ·
**[L]** supported by published literature, cited ·
**[H]** a design hypothesis, not a finding ·
**[F]** future work, not done.

> **No industry statistic, interview, user study or incident record appears in
> this document or in the presentation, because none was conducted.** AeroDSL
> has not been used, seen or commented on by anyone who writes scenarios
> professionally.

### 3.1 Who, and what they are making

**[H]** The intended user is an instructor who trains air traffic controllers on
a simulator, or a researcher running experiments on one. They are domain experts
and not necessarily programmers. This is the design assumption the project is
built on; it was not established by interview.

**[L]** Jafer, Updegrove and Chhaya (2017) describe air-traffic-control scenario
development as lacking a standardised practice, with scenarios that are hard to
reuse and share, and propose an ATC Scenario Language on top of the existing
Aviation Scenario Definition Language (ASDL) in response.

**[L]** Gould, De Ath, Carvell and Pepper (2025) describe manual scenario design
as "a demanding and time-consuming bottleneck", and approach it with a large
language model rather than a language.

### 3.2 The current workflow

**[V]** The scenario is a flat text file of timestamped commands, one per line,
replayed by the simulator. The documented BlueSky example is:

```
# SCN file to create a KL001 aircraft flying over checkpoint woody
0:00:00.00>CRE KL001, B737, 51.890958,5.177844, 300, FL100, 170
0:00:00.00>ADDWPT KL001 WOODY
0:00:05.00>FF
```

This is read directly from the BlueSky (TU Delft) wiki page "Scenario files".

### 3.3 What is hard about it

All four are **[V]** — properties of the format, read from its documentation
and confirmed against the generator in `src/scn.c`.

- **No names.** There is no variable binding. A cruise level used ten times is
  typed ten times.
- **No conditions.** There is no conditional or loop construct. "Turn when it
  reaches FL300" must be hand-converted into an absolute timestamp.
- **No checking.** The file is replayed, not validated. A heading of 450°, an
  aircraft that was never created, and two clearances that cancel out are all
  syntactically fine.
- **No intent.** Why the scenario exists — the conflict it is meant to create —
  is recorded nowhere in it.

### 3.4 A concrete failure

**[V]** (constructed from the format's own properties, not an observed
incident): the author edits a start level from `FL100` to `FL200`. Six lines
below, a `HDG` command sits at `0:03:20.00`, a timestamp originally computed
from the FL100 climb profile. The turn now happens at the wrong point in the
climb. The file remains valid. Nothing reports anything. The error surfaces, if
at all, by watching the replay.

### 3.5 Why not a generic script

**[H]**, argued rather than measured. Three things are lost by generating the
file from Python or a shell script:

1. **The errors are the host language's.** A script will pass 450 as a heading
   without comment. Domain validity is not expressible in the host's type
   system without building, in effect, the checker this project builds.
2. **The scenario stops being an artefact.** It becomes a program that prints an
   artefact — a different thing to review, diff and hand to a colleague.
3. **Intent remains uncheckable.** A script can emit the commands for a
   converging pair; it cannot tell you whether the pair converged, because it
   does not simulate.

### 3.6 What AeroDSL does, and the error class it removes

**[V]** The author writes the scenario as a program: named values, expressions,
conditionals, loops and procedures. The compiler type-checks it, optimises it,
applies domain checks, executes it on a small simulator, and emits the flat
file. Observed on `examples/13_walkthrough.aero`:

```
let cruise = FL320                        →   clear AI202, climb, 30000
clear AI202 climb cruise - 2000
```

**[V]** The error class removed: out-of-range operands and contradictory
clearances become compile-time errors instead of silent wrong behaviour at
replay time — *including* when the bad value is held in a variable, because the
range check runs after constant propagation. `examples/14_review_errors.aero`
exits 1 with both diagnostics.

**[V]** The generated `.scn` line format and the `CRE` argument order were
checked against the published BlueSky documentation and match what `src/scn.c`
emits.

**[V]** Settled by running BlueSky 1.1.1: files with the `HH:` padded hour and
a comma in `ADDWPT` load and run with no error messages (`bluesky/README.md`).

### 3.7 Success criteria, and what has not been shown

**[F]** Success would be an instructor writing a scenario in AeroDSL, loading
the generated file into BlueSky, and getting the traffic situation they intended
on the first run. **This experiment has not been performed.** Nothing in the
project is evidence that it would succeed.

### 3.8 Prior art, and the novelty claim

**[L]** AeroDSL is **not** the first language proposed for authoring ATC
scenarios. ASDL and the 2017 ATC Scenario Language precede it, and AirTrafficGen
(2025) attacks the same authoring problem by a different route. The presentation
says this explicitly, on slide 4.

What the project contributes is not the idea. It is a complete, small, readable
compiler for that idea — built from scratch, with a domain-aware static checking
layer that the cited work does not describe — and a full demonstration of every
compiler phase on one example.

### 3.9 Statement of limits

AeroDSL is an educational prototype for simulated training scenarios. It is not
certified, not validated against operational procedure, not deployed, and not
connected to any real aircraft or live air-traffic-control system. The footer of
every presentation slide says so, and the title slide says it in full.

---

## 4. The language and the compiler, in summary

### 4.1 The language

Four types (`num`, `bool`, `flight`, `fix`); declarations for waypoints and
aircraft; `let` bindings; assignment; `clear` with six clearance kinds; `advance`
for simulated time; `if`/`while`/`proc`/`call`/`print`/`return`; `assert` for
stating the scenario's purpose; field reads (`AI202.alt`) and builtins (`hsep`,
`min`, …). Two domain literals: `FL320` (a flight level, 32,000 ft) and `280KT`
(a speed, 280 kt), both converted in the lexer.

Valid code, from `examples/13_walkthrough.aero` — **this runs**:

```
scenario "Climb to cruise"
fix VOMAN 12.95 77.67
let cruise = FL320

flight AI202 type A320 enter VOMAN
       alt FL240 hdg 090 spd 280KT

advance 120
clear AI202 climb cruise - 2000
advance 80
clear AI202 heading 120
```

Invalid syntax — **this is rejected**, from `examples/08_syntax_errors.aero`:
a chained comparison `a < b < c`, a missing `end`, a stray operand after a
complete statement.

A semantic error — **this is rejected**: `if AI202.alt then` (no implicit
conversion to bool), a call with the wrong arity, an undeclared callsign.

A domain error — **this is rejected**, from
`examples/14_review_errors.aero`:

```
14:1: semantic error: the operand of 'heading' must be between 0 and 360 deg, found 450
17:1: semantic warning: AI202 was cleared 'climb' at line 16 and 'descend' here
      with no 'advance' between them: the two contradict each other
```

The optimisation opportunity, the generated IR and the final output are in
§3.6 above and in the speaker notes' walkthrough, all from real runs.

### 4.2 The compiler

`source → lexer → parser → semantic analysis → three-address code → CFG →
optimiser (7 passes to a fixed point) → domain analysis → code generation →
AeroVM → scenario file`

Each arrow is a command-line flag: `--tokens --ast --symbols --ir --cfg --ssa
--opt-ir --nextuse --bc --trace --scn`.

**The design decision worth defending.** The language has simulated state, so
`AI202.alt` can change without any instruction assigning to it. Every standard
optimisation that moves or reuses a read is therefore unsound by default. The
rule is that `clear`, `advance`, `enter` and `call` are **barriers**, and a read
of simulated state never crosses one (`ir_is_barrier`, `src/cfg.c`). It is
conservative: it forbids some safe motions. It is verified by a differential
test, not by assertion.

**The second decision worth defending.** Domain range checking runs on the
*optimised* IR rather than on the AST, so it sees through variables. This makes
an optimisation pass a precondition for a diagnostic — an inversion of the usual
relationship, and the clearest single point in the project.

---

## 5. Syllabus mapping

Of **59** syllabus topics: **45 implemented**, **1 partial**, **3 worked by hand
in the documentation** rather than in code, **10 not covered**.

> This project does **not** cover every syllabus topic, and does not claim to.

| Module | Where it is in AeroDSL | Source | How to demonstrate | impl. | partial | paper | not |
|---|---|---|---|---|---|---|---|
| 1 Lexical analysis | Hand-written scanner, token specification, the `FL320`/`AI202` DFA | `src/lexer.c` | `./aerodsl --tokens examples/13_walkthrough.aero` | 5 | 0 | 1 | 2 |
| 2 Syntax analysis | LL(1) recursive descent; an SLR table worked by hand in `docs/` | `src/parser.c` | `--ast`; then `examples/08` for recovery | 6 | 0 | 2 | 2 |
| 3 Semantic analysis | Syntax-directed definitions over the AST; the L-attributed backpatching scheme | `src/sema.c` | `--symbols`; then `examples/09` | 5 | 0 | 0 | 0 |
| 4 Intermediate code | Quadruples, short-circuit backpatching, procedures and activation records | `src/ir.c` | `--ir` | 9 | 0 | 0 | 1 |
| 5 Optimisation | Basic blocks, the DAG, dataflow, dominators, loops, peephole | `src/opt.c`, `src/cfg.c` | `--opt-ir`, `--cfg`, `--stats examples/06_optimise.aero` | 10 | 1 | 0 | 0 |
| 6 Code generation | Next-use information, register allocation with spilling, activation records | `src/codegen.c` | `--bc`, `--nextuse` | 6 | 0 | 0 | 0 |
| 7 Parallelism and DSLs | Domain-specific language design; SSA construction | `src/ssa.c`, the whole project | `--ssa examples/05_loop.aero` | 4 | 0 | 0 | 5 |
| **Total** | | | | **45** | **1** | **3** | **10** |

**The partial one:** a bytecode verifier exists in outline only; the VM trusts
the code generator.

**The three on paper:** the SLR parsing table, the operator-precedence relations,
and the regular-expression-to-DFA construction for the token classes are worked
by hand in the documentation rather than implemented as code.

**The ten not covered, named:** LLVM, Lex, operator-precedence parsing, CLR
construction, LALR construction, switch statements, automatic parallelisation,
cache-locality optimisation, vectorisation, instruction scheduling and software
pipelining.

---

## 6. Animation record

`pptxgenjs` has no animation API. The animations were written into the OOXML by
`animate.py` after the deck was generated, as the same objects PowerPoint itself
writes, so they appear and can be edited in the Animation Pane.

**Measured from the built file:** 20 slides, 104 click steps, 405 animated
shapes, 0 unresolved targets. The deck validates against the official
ISO/IEC 29500 `pml.xsd` schema, before and after injection.

| Effect | Status | How it is done, or why not |
|---|---|---|
| Entrance animation on every build step | **Native** | Click-triggered fade: `presetID="10"`, `presetClass="entr"`, `nodeType="clickEffect"` (first shape of a step) or `"withEffect"` (the rest), inside a `<p:timing>` tree with a `mainSeq` and a `<p:bldLst>`. Targets resolved from each shape's `objectName` to its `<p:cNvPr id>`. |
| Slide transition | **Native** | `<p:transition spd="med"><p:fade/></p:transition>` on every slide, placed after `<p:clrMapOvr>`. |
| A — problem → solution reveal (slides 2, 3, 5) | **Native, as sequenced fades** | The "before" state appears first; the solution fades in on the next click. |
| B — pipeline animation (slide 7) | **Native, as sequenced fades** | Ten stage chips reveal left to right, one click each, with their arrows. Not a motion path: nothing travels along the pipeline. |
| C — step-by-step compilation (slides 8, 9) | **Native, as sequenced fades** | Each of the seven stages is one click step, each with its real output. |
| D — error detection (slide 12) | **Native, as sequenced fades** | Source first, then the diagnostics, then the three layers. |
| E — optimisation demo (slide 10) | **Native, as sequenced fades** | Before, arrow, after, then counters, then the point. |
| F — architecture reveal (slides 11, 13, 14) | **Native, as sequenced fades** | Components reveal one click at a time. |
| Morph transition | **Not used** | Needs the PowerPoint 2019+ `p168` namespace and degrades badly where unsupported. Fade was used instead and the deck says so rather than implying morph. |
| Motion paths, spins, bounces, sound | **Not used** | Deliberately excluded. Every animation exists to sequence an explanation. |
| PDF export | **Documented fallback** | Animations do not survive PDF export. Every slide is laid out to be complete and readable with all build steps visible at once; nothing is hidden behind an animation. Verified by rendering all 20 slides to PNG and inspecting each. |

**Nothing static is described as animated anywhere in the deck.** Slide 19 is
this table, in the deck, for exactly that reason.

---

## 7. Build and test results

Run from a clean tree on the submitted archive.

```
$ make clean && make
(builds; no warnings)

$ make test
336 checks, 0 failures
all tests passed
all example checks passed

$ make fuzz
3000 inputs, 0 crashes
```

| Evidence | Result | Command |
|---|---|---|
| Build | Clean, no warnings, no dependencies | `make` |
| Unit and integration suite | **336 checks, 0 failures** | `make test` |
| Example behaviour | All 17 examples behave as their headers say; the 10 valid ones exit 0, the 7 invalid ones exit 1 | `make test` (runs `tests/run_examples.sh`) |
| Phase flags | All 16 run without crashing on a real scenario | `make test` |
| Differential (optimiser preserves behaviour) | 18 scenarios — 10 hand-written + 8 examples — run with and without the optimiser; diagnostics, full event trace and final aircraft state must match exactly | `make test` |
| AddressSanitizer + UndefinedBehaviourSanitizer | **0 findings** | `make debug`, then the suite and the fuzzer |
| Fuzzing | **3,000 generated inputs, 0 crashes** | `make fuzz` |
| Source size | 8,390 lines of C11 across `src/` (header included); 1,372 lines of tests | `wc -l src/* tests/test_all.c` |
| Deck validation | All 45 slide, layout, master and notes parts valid against ISO/IEC 29500 `pml.xsd`; relationships and content types pass | `build/check.py`, `scripts/office/validate.py` |
| Deck packaging | `[Content_Types].xml` first, no directory entries | `build/check.py` |
| Deck visual check | All 20 slides rendered to PNG and inspected; overflow and overlap corrected | LibreOffice → PDF → `pdftoppm` |

**Not verified, and recorded as such:**

- The generated `.scn` file has never been loaded into BlueSky or any other
  simulator. Format conformance rests on documentation alone.
- The `H:`/`HH:` hour padding and the `ADDWPT` argument separator could not be
  settled from the documentation.
- No performance benchmark was taken, and none is quoted anywhere.
- No scenario author has evaluated the language.

---

## 8. Remaining technical weaknesses

In the order I would raise them.

**8.1 The flight model is the weakest part.** Kinematic with fixed rates — 1,800
ft/min, 3°/s, 1 kt/s — with no mass, no wind and no performance envelope.
Everything above it is sound in its own terms, but a scenario whose correctness
depends on precise timing is only as good as this model. It is also the thing
that most obviously separates the project from a usable tool.

**8.2 Units are a convention, not a type.** `FL320` and `280KT` convert to feet
and knots in the lexer, after which they are ordinary numbers. `clear AI202
climb 280` means 280 feet and is accepted. This is the one place the language
looks safer than it is, and it is the single best extension available.

**8.3 SSA is built and unused.** 435 lines of correct, tested construction that
no optimisation pass consumes. Either use it or move it behind a documented
"study artefact" label; leaving it as is invites the reasonable question of why
it exists.

**8.4 The clearance consistency check is intraprocedural and
intra-block.** It is sound — it never reports a contradiction that is not one —
but it misses every contradiction that spans a branch or a procedure boundary.
Correct for a *warning*; a limitation nonetheless.

**8.5 Register allocation is local.** Within a basic block, with spilling. No
global allocation, no live-range splitting, no coalescing.

**8.6 Common-subexpression elimination is local.** DAG-based within a block.
No global value numbering, no partial-redundancy elimination.

**8.7 Fixed capacities.** 256 tracked flights, 16 registers, 8 parameters, 8
arguments, 8 print items, recursion depth 256, 24 optimiser rounds. All are
enforced with diagnostics rather than silently overflowing, but they are limits.

**8.8 No separate compilation and no modules.** A scenario is one file.

**8.9 The bytecode is unverified.** The VM trusts the code generator. A
hand-written bytecode file could crash it. This is the "partial" entry in the
syllabus table.

**8.10 The format conformance claim rests on documentation alone.** Until the
output is loaded into BlueSky, §3.6 is a reading of a wiki page and not a test.

---

## 9. What changed in this pass, and why

No working feature was removed and no compiler code was rewritten. The changes
were:

| Change | Reason |
|---|---|
| **Fixed: the deck would not open in PowerPoint.** Two defects, both found by validating against the official OOXML schema rather than trusting a renderer. (a) Two line shapes on the title slide were drawn with a negative width and height, which pptxgenjs writes as a negative `<a:ext>`; the schema forbids it and PowerPoint refuses the file. They are now positive extents with `flipH`/`flipV`, which is how PowerPoint represents a backwards-drawn shape. (b) pptxgenjs emits one `<a:pPr>` per *run* instead of one per *paragraph*, so every paragraph with more than one run gets an `<a:pPr>` after a run — invalid, because `<a:pPr>` must be the first child of `<a:p>`. Every syntax-coloured code block in this deck is a multi-run paragraph, so this affected eleven slides and 393 paragraphs. A repair pass (`build/repair.py`) now keeps the first `<a:pPr>` of each paragraph and deletes the duplicates. | LibreOffice renders both defects happily, which is why the PDF looked correct while the `.pptx` did not open. The lesson: validate the package, do not just look at a render. |
| Also in packaging: the archive is now rebuilt with `[Content_Types].xml` as its first entry and no directory entries | `zip -r` stores entries in `readdir` order, which left the content-types part near the end of the archive. Correct per the OPC specification, and one less thing for a strict reader to reject. |
| Cut the prose on every content slide — slide 2 from 157 to 113 words, slide 11 from 183 to 139, slide 16 from 225 to 177 | The first draft averaged ~180 words a slide, which is a document on a wall rather than a presentation. |
| Raised the type floor: no prose below 11pt, no code below 10pt (the grammar appendix was 9pt) | Readability from the back of a room. |
| Added build-time fit checking to `codeCard` and `noteCard` | Text overflow was being caught by eye in rendered PNGs. It is now caught when the deck is generated, and every overflow it found has been fixed. |
| Added a takeaway strip to slides 3 and 13 | Both had an empty lower third and no single stated message. |
| Added `examples/13_walkthrough.aero` | The presentation walks one example through every phase; it has to be in the repository so every figure on slides 8 and 9 is reproducible with one command. |
| Added `examples/14_review_errors.aero` | Same, for the error slide. Registered in `tests/run_examples.sh` as a file that must be rejected, so it cannot silently start compiling. |
| Registered both in `tests/run_examples.sh` | So the test suite enforces the presentation's claims. |
| Built `AeroDSL_Compiler_Design_Review.pptx` (now `slides/AeroDSL_Review1.pptx`) | Deliverable. |
| Built `AeroDSL_Review_Speaker_Notes.md` | Deliverable. |
| Built this document | Deliverable. |

Three things were deliberately **not** changed:

- **The `.scn` hour padding and `ADDWPT` separator.** Changing working code to
  match a guess about undocumented behaviour would be worse than recording the
  uncertainty. Recorded in §3.6 and §7.
- **SSA.** Deleting 435 lines of correct code to make the project look smaller
  is the thing the brief rules out, and wiring it into the optimiser is new
  compiler work the brief also rules out. It is tiered as reserve instead.
- **The browser UI.** Same reasoning. Kept, tiered as nice-to-have.

---

## Sources

- BlueSky (TU Delft CNS/ATM), wiki, "Scenario files" —
  <https://github.com/TUDelft-CNS-ATM/bluesky/wiki/Scenario-files>.
  The line format, the comment convention and the `CRE` argument order on
  slides 2 and 5 are read from this page.
- S. Jafer, J. Updegrove, B. Chhaya, "Towards a Common Air Traffic Control
  Simulation Scenario Development", SciTePress, 2017, pp. 52–70,
  DOI 10.5220/0007905000520070. Proposes an ATC Scenario Language extending
  ASDL; the source for the "no standardised practice" claim and the prior-art
  statement.
- Aviation Scenario Definition Language (ASDL), Jafer et al., Embry-Riddle
  Aeronautical University. The formal scenario definition language AeroDSL's
  idea sits behind.
- D. S. W. Gould, G. De Ath, B. Carvell, N. Pepper, "AirTrafficGen: Configurable Air
  Traffic Scenario Generation with Large Language Models", arXiv:2508.02269,
  2025. The source for "a demanding and time-consuming bottleneck".
- A. V. Aho, M. S. Lam, R. Sethi, J. D. Ullman, *Compilers: Principles,
  Techniques and Tools*, 2nd ed. Backpatching, the DAG, next-use information and
  the register allocator follow the schemes in this text.
- R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, F. K. Zadeck, "Efficiently
  computing static single assignment form and the control dependence graph".
  Dominance frontiers, phi placement and renaming over the dominator tree.
