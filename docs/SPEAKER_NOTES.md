# AeroDSL — Speaker Notes

Companion to the Review 1 deck, `slides/AeroDSL_Review1.pptx` (20 slides: 16 core + 4
appendix, 104 click-triggered build steps, presenter notes on every slide).

Everything quoted below as program output was produced by running the project in
this repository. Where a figure could not be checked, it is marked
**unverified** rather than rounded off.

**Say this once, early, and mean it:** AeroDSL is an educational prototype for
*simulated* training scenarios. It is not certified, not deployed, not connected
to any real aircraft or live air traffic control system, and it has not been
evaluated by anyone who writes scenarios professionally.

---

## 1. The five-minute script

> Use slides 1, 2+3 (merged in the telling), 5, 7, 9, 10, 16.
> Target: six to eight sentences per beat, no pauses for questions.

**(1) Open — slide 1.**
"AeroDSL is a small programming language I designed and a compiler for it,
written from scratch in C — about 8,400 lines, no parser generator and no
libraries. The domain is air-traffic-control training scenarios: the scripted
traffic situations instructors put in front of trainee controllers on a
simulator. Before anything else: this is an educational prototype for simulated
scenarios, not a system that touches real aircraft."

**(2) The problem — slides 2 and 3.**
"Scenario files today are flat lists of timestamped commands — this is the real
documented format from BlueSky, an open-source ATC simulator from TU Delft.
There are no variables, so a cruise level used ten times is typed ten times.
There are no conditions, so 'turn when it reaches FL300' has to be
hand-converted into a timestamp. And nothing checks the file: a heading of 450
degrees, an aircraft that was never created, two clearances that cancel each
other out — you find all of those by watching the replay."

**(3) What the language does — slide 5.**
"On the left is the same scenario written in AeroDSL. On the right is what my
compiler emits: the flat file. I am not replacing the simulator format, I am
generating it. The author writes `cruise - 2000` and the compiler works out
FL300 by constant folding. Nothing is typed twice, so nothing can go stale."

**(4) The compiler — slide 7.**
"Every stage of a textbook compiler is here and every one of them can be printed
from the command line: tokens, AST, symbol table, three-address code, control
flow graph, the optimised code, bytecode, and the run itself."

**(5) One program, all the way through — slide 9.**
"This is the same eight-line program as three-address code, then optimised, then
as bytecode. Three instructions became one: constant propagation replaced
`cruise`, folding evaluated the subtraction, dead-code elimination removed the
leftovers. Every box on that slide is real output from `./aerodsl`."

**(6) Why the optimiser earns its place — slide 10.**
"The interesting part is not that the code got smaller. My operand range check
runs *after* constant propagation. So `let h = 450` followed by
`clear AI202 heading h` is a compile-time error, because by then the compiler
knows what `h` holds. That is an optimisation pass justified by the diagnostic
it makes possible, not by the code it produces."

**(7) Close — slide 16.**
"What I am claiming is narrow: a small language with a complete compiler turns a
class of scenario-authoring mistake into a compiler error, and the compiler is
finished enough to show you every stage of it. The flight model is crude, units
are a convention rather than a type, and I have never loaded the output into a
real simulator. Those are the next three things I would do."

---

## 2. The ten-minute script

Add these four beats to the five-minute script, in this order.

**After beat (2), insert slide 4 — honesty about prior art.**
"Is this a real problem, and has it been solved? I want to separate three things.
Verified: I read the BlueSky scenario-file documentation and the format genuinely
has no variables, conditionals or validation. From the literature: Jafer,
Updegrove and Chhaya in 2017 describe ATC scenario development as lacking a
standard practice, and they propose an ATC Scenario Language on top of an
existing Aviation Scenario Definition Language — so AeroDSL is *not* the first
language proposed for this, and I do not claim it is. A 2025 paper, AirTrafficGen,
calls manual scenario design 'a demanding and time-consuming bottleneck' and
attacks it with an LLM instead. And then an assumption, clearly labelled: that a
compiler-checked DSL is the right answer for a working instructor. I have not
interviewed one. That is a design hypothesis, not a finding."

**After beat (3), insert slide 6 — the language itself.**
"Here is a complete runnable program — a converging pair. Waypoints and aircraft
are declarations. `level` is a variable used by both aircraft. `hsep` reads the
horizontal separation between two aircraft *right now*, during the simulation.
And the last line is an assertion: the scenario states its own purpose, and the
compiler runs it and checks it."

**After beat (5), insert slide 11 — the state barrier.**
"This is my favourite slide because it is where the domain constrains the
compiler. A loop that waits for `AI202.alt < FL300` — by every textbook test,
`AI202.alt` is loop-invariant: same operands, nothing in the loop assigns to it.
Hoist it and the loop never terminates. So I have one rule: `clear`, `advance`,
`enter` and `call` change the simulated world and are barriers. A read of that
world never moves across one. That is `ir_is_barrier` in `src/cfg.c`, nine
lines. And it is not asserted, it is tested: the suite runs eighteen scenarios
twice, with the optimiser and without, and requires the diagnostics, the whole
event trace and the final state of every aircraft to match exactly."

**After beat (6), insert slide 12 — the three layers of error.**
"Three layers, and they are genuinely different phases. Lexical and syntax:
malformed tokens and grammar violations, reported with recovery so you get all
of them in one run. Semantic: types, arity, scope. And then the domain layer,
which is the one a general-purpose compiler could not have — operand ranges after
constant propagation, and clearance consistency over the basic block. Both
messages on that slide are copied from a run, not written for the slide."

**If you have slack:** slide 14 (the evidence — 336 checks, zero ASan/UBSan
findings, 3,000 fuzz inputs with no crash) and slide 15 (the syllabus table).

---

## 3. Explaining the problem, if you are asked to expand

Work through these in order. Each answers one question an examiner may ask.

**Who is the user?** Instructors who train air traffic controllers on a
simulator, and researchers running experiments on one. They are domain experts;
they are not necessarily programmers.

**What are they making?** A scenario: a set of aircraft with starting positions,
and a timeline of clearances issued to them over simulated time. A scenario
usually exists to put *one* specific situation in front of a trainee — a
converging pair, a late descent, a go-around.

**How do they do it today?** They hand-write a flat text file of timestamped
commands, one per line, which the simulator replays. The BlueSky format on
slide 2 is the documented example from the project's own wiki.

**What is hard about that?** Four things, and they are all consequences of the
format being designed for a machine to read rather than a person to write.
*No names:* a level used ten times is typed ten times. *No conditions:* "turn
when it reaches FL300" cannot be expressed, so the author computes a timestamp
by hand. *No checking:* the file is replayed, not validated. *No intent:* the
reason the scenario exists is written nowhere in it.

**A concrete example.** The author edits a start level from FL100 to FL200. Six
lines further down there is a `HDG` command at `0:03:20.00`, a timestamp that was
computed from FL100. The turn now happens at the wrong moment in the climb. The
file is still perfectly valid. Nothing reports anything. The author finds out by
watching the replay — if they notice at all.

**Why not just write a Python script?** You can, and people do. Three things you
lose. You get Python's errors, not the domain's: a script will happily pass 450
as a heading. You get no single artefact that *is* the scenario — the scenario
becomes a program that prints a scenario, which is a different thing to review,
diff or hand to a colleague. And the intent still is not checkable: a script can
emit the commands for a converging pair, but it cannot tell you whether the pair
actually converged, because it does not simulate.

**How AeroDSL changes it.** The author writes the scenario as a program with
named values, expressions, conditionals, loops and procedures. The compiler
type-checks it, optimises it, checks the domain constraints, runs it on a small
simulator, and emits the flat file the simulator wants. The numbers that used to
be typed by hand are computed.

**The error class it prevents.** Out-of-range operands and contradictory
clearances become compile-time errors instead of silent wrong behaviour at
replay time — including when the bad value is held in a variable, because the
range check runs after constant propagation.

**What would count as success?** An instructor writing a scenario in AeroDSL,
loading the generated `.scn` into BlueSky, and getting the traffic situation
they intended on the first run. **That experiment has not been done.** Nothing in
this project is evidence that it would succeed.

**Limitations, stated plainly.** The flight model is kinematic and crude. Units
are a naming convention, not part of the type system. The output has never been
loaded into a real simulator. No scenario author has used, seen or commented on
the language.

---

## 4. The complete compiler walkthrough

One program, every phase. This is slides 8 and 9, and every figure here is real
output — reproduce any of it with the command shown.

**The program** (`examples/13_walkthrough.aero`):

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

**Lexical analysis** — `./aerodsl --tokens examples/13_walkthrough.aero` — 37 tokens. The scanner is
hand-written, one pass, no backtracking. Its one interesting decision is the
classification order: keyword first, then the two domain literal shapes
`FL[0-9]{3}` and `[0-9]{1,3}KT`, then numbers, then lowercase identifiers, then
uppercase callsigns. That order is what lets `FL240` be one token worth 24,000
feet and `280KT` be one token worth 280 knots, while `A320` is still a callsign.
The unit conversion happens here, in the lexer, which is why no later phase has
to know what a flight level is.

**Syntax analysis** — `--ast`. Recursive descent, LL(1) by construction, one C
function per production, with one function per precedence level in the
expression grammar. On an error the parser reports and then resynchronises at
the start of the next statement *line*, so a file with ten mistakes gives ten
messages rather than one followed by noise.

**Semantic analysis** — `--symbols`. A scoped symbol table (a flat array of all
symbols for ownership, plus a stack of the ones currently visible), four types
(`num`, `bool`, `flight`, `fix`), arity and type checking on every operator,
builtin and procedure call, and nine warnings. There is deliberately no implicit
conversion to bool: in a language about altitudes, "non-zero is true" is a trap,
so `if AI202.alt then` is rejected.

**Intermediate code** — `--ir`. Quadruples. Boolean operators and comparisons are
translated by **backpatching** — `makelist`, `merge`, `backpatch` — so `a and b`
short-circuits without the generator ever needing to know where the target label
will land. For this program:

```
0  cruise = 32000
6  t1 = cruise - 2000
7  clear AI202, climb, t1
```

**Control flow** — `--cfg`. Basic blocks, a dominator tree by iterative
intersection, immediate dominators, back edges, natural loops, and live-variable
dataflow. `--ssa` additionally builds static single assignment form by Cytron's
method: dominance frontiers, pruned phi placement, renaming over the dominator
tree. (Be honest here — see weaknesses: **SSA is built and printable but no
optimisation pass consumes it.**)

**Optimisation** — `--opt-ir`. Seven passes repeated to a fixed point, because
each creates work for the others: constant propagation over a three-point
lattice, constant folding, DAG-based local common-subexpression elimination with
copy propagation and algebraic identities, dead-code elimination, loop-invariant
code motion, jump threading, and unreachable-block removal. On this program the
three instructions above collapse to one:

```
5  clear AI202, climb, 30000
```

**Domain analysis.** Runs on the *optimised* IR, which is the point. Operand
range checks see propagated constants, so a bad value held in a variable is a
compile-time error. Clearance consistency is a scan of one basic block —
deliberately, so it never reports a contradiction that is not one.

**Code generation** — `--bc`. Compare-and-branch fusion, next-use information,
register allocation over 16 registers with spilling by furthest next use, then a
peephole pass. 18 bytecode instructions, 2 frame slots.

**Execution** — `--trace`. A register virtual machine with activation records,
a kinematic flight model integrated one simulated second at a time, and a
separation monitor that checks every pair of aircraft every second.

**Output** — `--scn`. The flat scenario file, which is where we came in.

---

## 5. Twenty-five viva questions, with accurate answers

**1. Why a new language instead of a library or a script?**
Because the thing being authored is a document, not a program that prints a
document — it gets reviewed, diffed and shared. And because a library gets the
host language's error messages. A heading of 450 is a type error in my language
and an ordinary integer in Python.

**2. Is AeroDSL the first DSL for ATC scenarios?**
No, and I am careful not to claim it. ASDL — the Aviation Scenario Definition
Language — and Jafer, Updegrove and Chhaya's 2017 ATC Scenario Language came
first, and AirTrafficGen (2025) attacks the same authoring problem with an LLM.
What I contribute is not the idea; it is a complete small compiler for that idea,
built from scratch, with domain-aware static checking those papers do not
describe.

**3. Which parser technique, and why that one?**
Recursive descent, LL(1) by construction. I chose it because I wanted to write
the parser rather than generate it, and because a hand-written parser gives far
better error recovery — I resynchronise at statement-line boundaries, which a
generated parser would not do for free. The grammar is in Appendix A; I worked an
SLR table by hand in the docs to show I can do it the other way.

**4. Is the grammar really LL(1)? What were the ambiguities?**
Three places it could have been ambiguous and is not. `end` closes every block,
so there is no dangling-else problem. Comparison is non-associative, so `a < b < c`
is a syntax error rather than nonsense. And `fix` and `flight` are both keywords
and type names, but they only ever occur in distinguishable positions.

**5. What exactly does backpatching do here?**
When I generate code for `a and b`, I do not yet know the address to jump to if
`a` is false. So I emit the jump with an empty target and record its index in a
*falselist*. When the enclosing construct finally knows where false goes, it
backpatches every index in that list. `merge` joins two such lists. It is how I
get short-circuit evaluation in one pass over the tree.

**6. What is in your symbol table, and how is scope handled?**
Name, kind (variable, flight, fix, procedure, parameter), type, declaration site,
and whether it has been read. Scope is two arrays: `all` owns every symbol ever
declared; `stack` holds the ones currently visible. Entering a scope records the
stack height; leaving it truncates the stack back. I had a bug here early on —
popping only decremented a depth counter, so symbols stayed visible after their
scope ended. Splitting ownership from visibility fixed it.

**7. Why three-address code and not direct bytecode?**
Because every analysis I want is easier on it. Quadruples give me a uniform
instruction shape for the DAG, for dataflow, for next-use, and for SSA. Going
straight to a stack machine would have meant doing all of that on a representation
that hides the operands.

**8. Why a VM and not C or assembly?**
Because the scenario has to *run* in order to produce its own timeline. The output
is not a program the user executes later; it is a trace and a flat file produced
by executing the program now. A VM I control also lets me put the separation
monitor inside the execution loop.

**9. Walk me through one optimisation that actually changed something.**
`let cruise = FL320; clear AI202 climb cruise - 2000`. Constant propagation
replaces `cruise` with 32000. Folding evaluates `32000 - 2000` to 30000.
Dead-code elimination removes the now-unused assignment and temporary. Three
instructions become one, and you can see it with `--ir` and `--opt-ir`.

**10. How do you know the optimiser is correct?**
A differential test. The suite runs eighteen scenarios twice — with the optimiser
and without — and requires the diagnostics, the whole event trace and the final
state of every aircraft to match exactly. If a pass ever changes observable
behaviour, that test fails immediately.

**11. Your language has simulated state. Doesn't that break every standard
optimisation?**
It would, which is the most interesting problem in the project. `AI202.alt` looks
loop-invariant by every textbook test, and hoisting it out of
`while AI202.alt < FL300 do advance 10 end` makes the loop never terminate. My
rule is that `clear`, `advance`, `enter` and `call` are barriers — they can change
the simulated world — and a read of that world is never moved across one. That is
`ir_is_barrier` in `src/cfg.c`.

**12. Is that rule sound, or just careful?**
Sound for the moves I make, and conservative: it forbids some motions that would
in fact be safe. I would rather miss an optimisation than change what a scenario
does. The differential test is the evidence.

**13. Why does range checking happen after optimisation rather than in the
semantic analyser?**
So that it sees through variables. The semantic analyser can only reject a literal
450. After constant propagation, `let h = 450; clear AI202 heading h` is also
rejected. Putting it there also stops the same mistake being reported twice.

**14. What is the difference between your three error layers?**
Lexical errors are malformed tokens — a bad speed literal, mixed case. Syntax
errors are grammar violations — a chained comparison, a missing `end`. Semantic
errors are about meaning with correct grammar — wrong type, wrong arity, undeclared
name. The domain layer is semantic too, but it depends on values the optimiser
computed, so it runs later.

**15. How does your error recovery work?**
On an error the parser reports it, then skips tokens until the start of the next
statement line, then resumes. Two details cost me bugs: I only resynchronise when
the current statement actually produced an error, and the sync routine must not
unconditionally consume a token, or it eats the statement it was supposed to
resume at.

**16. What is SSA for, in your project?**
Honestly: it is built, it is correct, and nothing consumes it. `--ssa` prints
dominance frontiers, pruned phi placement and the renamed code, constructed by
Cytron's algorithm. My optimiser works on the ordinary three-address code. Using
SSA form in the optimiser is the first item on my future-work list.

**17. How does register allocation work?**
Sixteen registers. I compute next-use information per basic block; when I need a
register and none is free, I spill the one whose next use is furthest away, and I
only write it back if it is dirty. Spill slots live in the activation record.

**18. What does the peephole pass do?**
Two things: it fuses a comparison followed by a branch into one instruction, and it
inverts branches to remove an unconditional jump that immediately follows.

**19. How many syllabus topics do you actually cover?**
Of 59 in the syllabus: 45 implemented, 1 partial, 3 worked by hand in the
documentation rather than in code, and 10 not covered. The ten are named on
slide 15 — LLVM, Lex, operator-precedence parsing, CLR and LALR construction,
switch statements, and the five parallelism topics. I would not believe a project
that claimed all 59.

**20. How was this tested?**
336 assertions in a hand-written suite, every example compiled and checked, the
differential test above, a clean run under AddressSanitizer and UndefinedBehaviour
Sanitizer, and a fuzz harness — 3,000 generated inputs, no crash. `make`,
`make test` and `make fuzz` all run from a fresh unzip.

**21. What is the weakest part of the project?**
The flight model. It is kinematic with fixed rates — 1800 feet per minute, three
degrees per second, one knot per second — with no mass, no wind and no performance
envelope. Everything above it is sound in its own terms, but a scenario that
depends on precise timing is only as good as that model.

**22. Does the type system know about units?**
No, and that is the one place the language looks safer than it is. `FL320` and
`280KT` convert to feet and knots in the lexer, but after that they are just
numbers. `clear AI202 climb 280` means 280 feet and is accepted. Making units part
of the type system is the single best extension I could make.

**23. Have you tested the output against a real simulator?**
Yes, in BlueSky 1.1.1, headless (`bluesky/run_bluesky.py`). The files generated
for examples 3 and 5 load and run with no error messages, which also settles the
hour padding and the comma in `ADDWPT`. The test found a real limitation: BlueSky's
A320 climbs at about 1,500 ft/min and our model at 1,800, so the turn example 5
times at 3:20 comes about 1,060 ft below FL300. The fix is to compile altitude
waits into BlueSky's `ATALT` trigger. Only version 1.1.1 has been tried.

**24. Could this be used for real air traffic control?**
No, and it is not meant to be. It is an educational prototype for simulated
training scenarios. There is no certification, no validation against operational
procedure, no deployment, and no evaluation by anyone in the profession. The
footer of every slide says so.

**25. What would you do next?**
Three things in order: use the SSA form in the optimiser rather than just
printing it; make units part of the type system; and load the generated file into
BlueSky and find out what is wrong with it.

**26. What was the hardest bug?**
Loop-invariant code motion hoisting a store that was still live after the loop.
`let x = 1; while c do x = 5 ... end; print x` printed 5 even when the loop never
ran. The fix is a liveness condition — the value must be dead on every exit edge
of the loop before the store can move.

---

## 6. Claims to avoid, and mistakes to avoid

**Do not say any of these. They are not true of this project.**

- That AeroDSL is the first or only language for ATC scenarios. It is not; ASDL
  and the 2017 ATC Scenario Language came first.
- That it is used by, tested by, or validated with aviation professionals.
  No one outside this project has used it.
- That it is certified, safety-rated, or suitable for operational air traffic
  control. It is none of these.
- That it has been run against a real simulator. The format was checked against
  documentation; the file was never loaded.
- Any statistic about how long scenario authoring takes, how many scenarios are
  written, or how often they contain errors. No such measurement was made, and
  none appears in the deck.
- That the optimiser makes scenarios "faster" in any way the user would notice.
  The point of the optimiser here is the diagnostic it enables, not speed.
- That SSA is used by the optimiser. It is built and printed, and that is all.
- That every syllabus topic is implemented. Ten are not, and they are named.

**Presentation mistakes to avoid.**

- Do not call a syntax error a semantic error, or the reverse. The examiner is
  listening for exactly this. Grammar violation → syntax. Meaning with valid
  grammar → semantic.
- Do not say "the compiler optimises it away" about the `cruise - 2000` example
  without naming the passes: propagation, then folding, then dead-code
  elimination. Three different passes, in that order.
- Do not describe the state barrier as "a hack". It is a soundness condition, and
  it is tested.
- Do not read the slides. Every slide is a single message; say the message and
  point at the evidence.
- If you are asked something you did not implement, say so and say where it would
  go. "Not implemented — it would sit between X and Y" is a much better answer
  than a guess.

---

## 7. Demo commands, in the order you would run them

```bash
E=examples/13_walkthrough.aero

make                                 # builds in a few seconds, no dependencies
./aerodsl --tokens  $E   # 37 tokens, FL240 worth 24000
./aerodsl --ast     $E   # typed tree
./aerodsl --symbols $E   # scoped symbol table
./aerodsl --ir      $E   # 11 quadruples
./aerodsl --opt-ir  $E   # 9 — three instructions became one
./aerodsl --cfg     examples/05_loop.aero     # blocks, dominators, loops
./aerodsl --ssa     examples/05_loop.aero     # frontiers, phis, renaming
./aerodsl --bc      $E   # 18 bytecode instructions
./aerodsl --trace   $E   # the run
./aerodsl --scn     $E   # the flat scenario file
./aerodsl --stats   examples/06_optimise.aero # every pass fires
./aerodsl examples/14_review_errors.aero         # the error layers; exit status 1
make test                            # 336 checks
make fuzz                            # 3000 inputs
make ui                              # the browser demonstration
```
