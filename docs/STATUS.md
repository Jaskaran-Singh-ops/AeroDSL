# Status

What is finished, what is partial, what was not attempted, what is known to be
wrong, and what comes next. The brief this project was built to asked for
honesty about which phases are complete, and this is that answer.

Last verified by a clean build and full test run on the commit this document
ships with: `make clean && make && make test` → **336 checks, 0 failures** and
**all example checks passed**.

---

## 1. Complete and tested

Every item here is implemented, exercised by the test suite, and demonstrable
from the command line.

### Front end

- **Lexer.** Hand-written, single pass, no backtracking. Typed tokens with
  lexeme, numeric attribute, line and column. Every lexical error reported, not
  just the first. Case convention enforced with an explanatory message.
- **Parser.** Recursive descent, one function per grammar production,
  precedence structural rather than table-driven. Error recovery at the start
  of the next line beginning a statement; a missing operand is reported at the
  keyword that needs it.
- **Symbol table.** Scope stack with shadowing, declaration sites recorded, and
  a barrier that stops a procedure body seeing the main body's variables.
- **Semantic analysis.** Full type checking over four types with no implicit
  conversions; arity checking for procedures and built-ins; the nine
  code-quality warnings of `SPEC.md` §7.5 (all nine are listed there); error suppression so one mistake
  gives one message.

### Middle end

- **Three-address code.** Quadruples, unbounded virtual registers, named
  variables and temporaries treated uniformly.
- **Backpatching.** Short-circuit boolean translation with truelists and
  falselists; `and`, `or` and `not` generate control flow rather than values in
  a condition, and a boolean used as a value is materialised separately.
- **Control-flow graph.** Basic blocks, edges, reachability, dominators,
  immediate dominators, back edges, natural loops, nesting depth.
- **Liveness.** Backward dataflow to a fixed point, with upward-exposed uses.
- **SSA.** Dominance frontiers, pruned phi placement, renaming over the
  dominator tree. Built and printed — see §3.
- **Optimiser.** Seven passes to a fixed point: constant propagation, constant
  folding, DAG-based CSE with copy propagation and algebraic identities,
  liveness-driven dead-code elimination, loop-invariant code motion, jump
  threading with unreachable-block removal, and compaction.
- **The state barrier.** Reads of the simulated world are never moved across
  `clear`, `advance`, `enter` or `call`. This is the rule the whole optimiser
  depends on for correctness in this domain, and it has its own test.
- **Domain analysis.** Operand range checking after constant propagation
  (which is also what catches a divisor that only became zero after
  propagation), and clearance consistency within a basic block. Clearances
  whose direction could not be decided statically are checked by the VM
  instead, and the compiler marks the ones it settled so neither reports
  twice.

### Back end and runtime

- **Compare-and-branch fusion** on the three-address code, where liveness is
  available to prove the comparison's result is dead.
- **Next-use information**, one backward scan per block.
- **Register allocation** over sixteen registers with spilling by furthest next
  use and dirty tracking; home frame slots for every virtual register.
- **Bytecode** for AeroVM, 22 instructions.
- **Peephole**: branch inversion, redundant move, store-load pair, jump to
  next, jump to jump, with target remapping on compaction.
- **AeroVM.** Register machine with activation records, recursion, bounded
  execution, runtime range checks, non-finite arithmetic rejected.
- **Flight model and separation monitor.** One-second integration, pairwise
  separation after every step, loss-of-separation events, closest approach.
- **Verification.** `assert` evaluated against simulated state, every assertion
  checked, non-zero exit on failure.
- **Scenario-file output.** `DEFWPT`, `CRE`, `ALT`, `HDG`, `SPD`, `ADDWPT`,
  timestamped from the run.

### Tooling

- Command line with twelve phase-output flags, reads standard input, exit codes
  0 / 1 / 2.
- A single-page demonstration UI showing every phase, styled to match the
  Review 1 slides: a syntax-coloured editor that marks error lines, a pipeline
  showing how far compilation got, and a plan view drawn from recorded
  positions with a time slider, playback and a highlighted aircraft. It opens
  on the scenarios used in the slides.
- 336 checks, including a differential test of optimiser soundness, a
  register-pressure test that forces spilling, and a recursion test.
- `make debug` for a sanitiser build and `make fuzz` for 3000 random and
  mutated inputs against it, both clean at the time of writing.
- Runs on Windows, macOS and Linux. Prebuilt programs are in `bin/`
  (`make release`). The Windows and Linux x64 builds produce output identical
  to the reference build on every example and phase option (255 runs, `tools/check_release.sh`), and
  the test suite passes on Windows under Wine. The macOS and Linux arm64
  builds have not been run.

---

## 2. Partial

| Item | What works | What does not |
|---|---|---|
| **Register allocation** | On-demand allocation with spilling inside a basic block | Allocation does not cross blocks: every value live at a block boundary is written back and reloaded. A global allocator over an interference graph would keep more in registers. |
| **Common-subexpression elimination** | Local, over each block's DAG | No global available-expressions analysis, so a computation repeated in two blocks is computed twice. |
| **Clearance consistency** | Within a basic block, including tracking the commanded level | Does not cross blocks. Sound but incomplete: it never reports a contradiction that is not one, and misses contradictions that span a branch. |
| **Security checking of VM code** | Execution is bounded (20M instructions, 24h simulated, 256 frames) and every clearance operand is range-checked before being applied | There is no verifier pass over the bytecode itself proving jump targets are in range and registers are defined before use. The generator only produces well-formed bytecode and a test checks that, but that tests the generator, not arbitrary input. |
| **Loop-invariant code motion** | Fires correctly, with the dead-on-exit and cannot-fail conditions | Only hoists to a position immediately before the loop header, and declines any loop whose header is reached by a jump from outside rather than by falling into it. |

---

## 3. Built but unused

**Static single assignment.** `src/ssa.c` is a complete and correct
construction — dominance frontiers, pruned phi placement, renaming over the
dominator tree — and `--ssa` prints it. **No optimisation pass operates on
it.** The optimiser works on the ordinary three-address code.

This is stated here, in `SPEC.md` §14, in `SYLLABUS.md` Module 7, in
`ARCHITECTURE.md` §8 and in `VIVA.md` Q34, because it is the single easiest
thing in this project to overclaim.

---

## 4. Not attempted

Each of these is also marked in `SYLLABUS.md` with the module it belongs to.

| | Why not |
|---|---|
| LLVM | The back end targets a VM written for this project. LLVM would have replaced the part of the project that was worth building. |
| Lex / Flex | The lexer is hand-written deliberately; a generator makes the scanner a black box in the phase a reader most wants to trace. |
| Operator-precedence parsing | Precedence is structural in the recursive-descent parser. |
| CLR and LALR table construction | The grammar is LALR(1); SLR is worked out on paper in `ARCHITECTURE.md` §4, the canonical and merged item sets are not. |
| `switch` statements | The language has no use for one, and adding a construct to tick a box is padding. |
| Automatic parallelisation | — |
| Cache-locality optimisation | — |
| Vectorisation | — |
| Instruction scheduling | — |
| Software pipelining | — |
| Unit-aware types | The single most valuable extension; see §6. |
| Arrays, records, modules | Out of scope for a language this size. |

---

## 5. Known limitations and defects

None of these is a crash or a wrong answer; they are places where the
implementation is weaker than it looks.

1. **The flight model is the weakest part of the project.** Kinematic, fixed
   rates, no mass, no wind, no performance envelope, no pilot response delay.
   Everything above it is sound in its own terms, but a scenario that depends
   on precise timing is only as good as this model, and the generated file
   inherits the same limitation.
2. **Units are a documentation convention, not types.** `clear AI202 climb 280`
   means 280 **feet** and is accepted. This is the one place the language looks
   safer than it is.
3. **The separation standard is two constants.** 5 NM and 1000 ft, applied
   everywhere. Real minima vary by airspace, altitude band, equipment and
   procedure.
4. **Flat-earth position.** One degree of latitude is 60 NM and longitude is
   scaled by cosine. Wrong near the poles and across the date line.
5. **The scenario file is a trace, not a translation.** Conditionals and loops
   are resolved by simulating, so the output is one concrete scenario. For
   scenario generation that is the intent, but it is not a translation of the
   program's semantics.
6. **BlueSky: tested on one version, and the times drift.** The files
   generated for examples 3 and 5 load and run in BlueSky 1.1.1 with no error
   messages (`bluesky/README.md`). Their times come from our flight model:
   BlueSky's A320 climbs at about 1,500 ft/min against our 1,800, so the turn
   that example 5 times at 3:20 comes about 1,060 ft below FL300 in BlueSky.
   Other BlueSky versions have not been tried.
7. **Error recovery can still lose a statement.** Resynchronisation is at the
   start of the next line beginning a statement, so a mistake part way through
   a line can consume the rest of that line.
8. **A hoisted arithmetic operation that overflowed** would report an error even
   if the loop body never ran. Division, the one operation that can fail on
   ordinary values, is excluded from hoisting for exactly this reason, and the
   value ranges AeroDSL works with make overflow unreachable in practice.
9. **Procedures do not return values.** Expression-level abstraction is limited
   to the eight built-ins.
10. **No aircraft performance data**, so no check that a commanded level or
    speed is within a given aircraft's capability. The `type` in a declaration
    is carried to the output and otherwise unused.
11. **Nothing has been reviewed by anyone with ATC expertise.**

### Defects fixed during development, recorded because they are instructive

- Resynchronising after every statement rather than only after a failed one
  silently swallowed a stray token. Now tested.
- Unconditionally skipping one token before resynchronising swallowed the whole
  following statement. Now tested.
- Scope popping left symbols visible to a later scope at the same depth. Fixed
  by truncating the visible stack rather than only decrementing the depth.
- Loop-invariant code motion hoisted a store whose value was live after the
  loop, changing the result when the loop ran zero times. Now has the
  dead-on-exit condition, and the exact program is a test.
- `cfg_build` clears immediate dominators, and the code generator rebuilds the
  graph; SSA built afterwards saw an empty dominator tree and printed every
  version as undefined while the frontiers still looked plausible. Fixed by
  having the SSA printer recompute what it needs.
- Copy and constant propagation counted rewrites that changed nothing, so the
  fixed-point loop never converged and only the round limit stopped it.
- Print output was split across events because register loads sit between print
  instructions in the bytecode. Fixed with an explicit end-of-statement marker.
- The diagnostic sort key shifted `(long)1 << 40`, which is undefined where
  `long` is 32 bits, as on Windows. It went unnoticed on Linux, where `long`
  is 64 bits; the Windows cross-build's warning found it. Now `long long`.

---

## 6. What I would do next, in order

1. **Use the SSA form.** Convert the optimiser to work on SSA. This is the one
   change that unlocks the most: global value numbering and a global register
   allocator both want it, and both are listed in §2 as the current weak spots.
2. **Unit-aware types.** Make feet, knots, degrees and seconds distinct types
   so `clear AI202 climb 280` is rejected. This is the most valuable change to
   the *language* and the smallest in implementation terms: it is mostly a
   wider `Type` and a table of which operations combine which units.
3. **Compile altitude waits into BlueSky's `ATALT`.** Running the output in
   BlueSky 1.1.1 showed the timing drift in item 6 of section 5. A wait such as
   `while AI202.alt < FL300` should become `AI202 ATALT FL300 HDG AI202 270`,
   which turns at the level whatever the climb rate (`bluesky/05_loop_atalt.scn`).
4. **Global common-subexpression elimination**, via available-expressions
   analysis over the CFG.
5. **A global register allocator** over an interference graph.
6. **Cross-block clearance analysis**, so a contradiction that spans a branch is
   found. This needs care: it is a warning about legal code, so a false
   positive costs more than a miss.
7. **A bytecode verifier**, to close out the Module 5 item in §2.
8. **A better flight model** — at minimum, per-aircraft-type climb rates and
   speed envelopes, which would also give the `type` field something to do.
