# Demonstration scripts

Two scripts: five minutes of talking, and ten minutes at the keyboard. Every
command here has been run and every number quoted is what it actually prints.

**Before you start:** `make && make test` from the project root. The test run
ends with `336 checks, 0 failures` and `all example checks passed`, which is a
good thing to have on screen when you begin.

---

# Part 1 — The five-minute talk

Timings are a guide. The point of the structure is that the problem comes
before the solution and the limitations come before anyone has to ask.

### 0:00 — The problem (60 seconds)

> An air-traffic-control training scenario is a scripted traffic situation:
> aircraft with starting positions, and a timeline of clearances. Simulators
> read these from files that look like this.

*Show a few lines of a `.scn` file — `examples/` generates one, or use the
block in `README.md`.*

> That format is easy for a simulator to read and unpleasant for a person to
> write. No variables, so a value used ten times is typed ten times. No
> conditionals, so "turn this aircraft when it reaches FL300" has to be
> converted by hand into an absolute timestamp — and silently becomes wrong
> when someone edits the starting altitude. Nothing is checked: a heading of
> 450 degrees, a command naming an aircraft that was never created, two
> clearances that contradict each other, all discovered by running the
> simulator and watching.
>
> And the thing the scenario exists for — that these two aircraft *must*
> converge, so the trainee has something to resolve — is written nowhere.

### 1:00 — The language (60 seconds)

*Show `examples/02_converging.aero` on screen.*

> AeroDSL makes the scenario a program. Variables, expressions, conditionals,
> loops, procedures, a static type system with four types. Three things in this
> file are the point.
>
> `let level = FL320` is written once and used twice.
>
> The loop measures the closest approach *in the language*, with `hsep` and
> `min`, instead of leaving it to be read off a replay.
>
> And the last line states why the scenario exists: these two must come within
> five miles. That is a claim the compiler checks.

### 2:00 — The compiler (90 seconds)

> It is about 8,300 lines of C — 6,500 excluding comments and blank lines —
> with no dependencies at all: no parser generator, no libraries.
>
> A hand-written lexer. A recursive-descent parser, one function per grammar
> production. A scoped symbol table. Full type checking. Three-address code,
> with short-circuit boolean translation by backpatching. A control-flow graph
> with dominators, natural loops and liveness. SSA construction with pruned phi
> placement. Seven optimisation passes to a fixed point. Register allocation
> over sixteen registers using next-use information. Bytecode for a virtual
> machine that simulates the traffic and monitors separation. And then the flat
> scenario file.
>
> Two parts are specific to the domain and are the ones I would point at.

**First: the optimiser makes a diagnostic possible.**

> Range checking runs *after* constant propagation. So this —
>
> ```
> let h = 450
> clear AI202 heading h
> ```
>
> is a compile-time error, not a runtime one, because by then the compiler
> knows what `h` holds. An optimisation is normally justified by the code it
> produces. Here one is justified by the error message it enables.

**Second: the domain constrains the optimiser.**

> `clear` and `advance` can change the simulated world, so a read of that world
> must never move across them. Without that rule this loop breaks:
>
> ```
> while AI202.alt < FL300 do advance 10 end
> ```
>
> The altitude read is loop-invariant by every ordinary test, so it would be
> hoisted out and the loop would never terminate. That one rule is what makes
> every other optimisation safe here, and there is a test for it.

### 3:30 — Evidence (45 seconds)

> 336 checks. The one that matters most runs eighteen scenarios twice, with and
> without the optimiser, and requires the diagnostics, the whole event trace and
> the final state of every aircraft to be identical. An unsound optimisation
> fails it immediately.
>
> There is also a register-pressure test — twenty live values through sixteen
> registers, with the arithmetic checked against a number computed by hand — so
> the spill path is genuinely exercised. `make debug` builds the whole thing
> under AddressSanitizer and UndefinedBehaviorSanitizer, and `make fuzz` throws
> three thousand random and mutated inputs at that build. Both are clean.

### 4:15 — Limits (45 seconds)

> What this is not. The flight model is kinematic with fixed rates — no mass,
> no wind, no performance envelope. The separation standard is two hard-coded
> constants, not real minima. Register allocation and common-subexpression
> elimination are both local to a basic block. SSA is built and printed but no
> pass uses it. Nothing here has been reviewed by anyone with ATC expertise, and
> The generated files load and run in BlueSky 1.1.1, but their times come
> from my simplified flight model, so a turn can come early there.
>
> The specification lists twenty-two limitations and the syllabus mapping marks
> ten topics as not covered. I would rather hand you that list than have you
> find it.

---

# Part 2 — The ten-minute live demonstration

Run everything from the project root. Five acts; each has a point to land.

---

## Act 1 — It works, end to end (1 minute)

```sh
./aerodsl examples/02_converging.aero
```

> One command: source in, scenario file out. `DEFWPT` for the waypoints, `CRE`
> for each aircraft, then the clearances at the times they were actually
> issued — and those times came from simulating the scenario, not from me
> working them out.

```sh
./aerodsl --trace examples/02_converging.aero
```

> Here is what happened. Both aircraft enter. At one minute thirty-five the
> monitor records a loss of separation at 4.93 miles. The closest approach was
> 1.10 miles. The assertion held — the conflict the scenario exists to create
> does happen.

**Then the companion file:**

```sh
./aerodsl --trace examples/12_separation.aero | tail -12
```

> Same geometry, one extra clearance putting 2000 feet between them. Closest
> approach is still 1.10 miles horizontally, but now with 2000 feet vertically,
> so no loss of separation, and this file's assertion — that separation is
> *maintained* — also holds.
>
> Two files, opposite claims, both verified by the compiler. That is the idea.

---

## Act 2 — Every phase is visible (2 minutes)

```sh
./aerodsl --phases examples/05_loop.aero | less
```

Walk through, pausing on three:

**The AST**, to show types are attached:

```sh
./aerodsl --ast examples/05_loop.aero | head -30
```

**The three-address code**, and specifically the short-circuit translation:

```sh
./aerodsl --ir examples/02_converging.aero
```

> Look at the loop condition. The comparison produces a value, then `if t goto
> L1` and `goto L2`. Those two jumps had no target when they were emitted —
> they were on a truelist and a falselist, and the statement patched them when
> it placed its labels. That is backpatching, and it is why this is a one-pass
> translation.

**The control-flow graph:**

```sh
./aerodsl --cfg examples/05_loop.aero
```

> Blocks, successors, predecessors, dominators, and liveness. B1 is marked a
> loop header because the edge from B3 back to it is a back edge — B1 dominates
> B3.

---

## Act 3 — The optimiser (2 minutes 30)

```sh
./aerodsl --ir --opt-ir --stats examples/06_optimise.aero
```

> This file is written so every pass fires. Sixty-two instructions down to
> thirty-eight.

Read the counters off the screen:

```
constants folded                             5
operands replaced by propagated constants   11
common subexpressions eliminated             2
copies propagated                            5
algebraic simplifications                    1
dead instructions removed                   15
instructions hoisted out of loops            1
jumps threaded or removed                    6
unreachable blocks removed                   2
peephole rewrites                            4
```

Then point at three specific things in the side-by-side output:

> `cruise`, `margin` and `target` — three declarations — have collapsed into the
> single constant 30000 in the clearance.
>
> `AI202.alt + 500` appears twice in the source and once in the optimised code:
> that is common-subexpression elimination over the block's DAG.
>
> And the `else` arm is gone entirely, with a warning saying so, because
> propagation proved the condition constant.

**Now the one that matters:**

```sh
./aerodsl --opt-ir examples/06_optimise.aero | grep -A12 "function climb_in_steps"
```

> Inside the procedure, `limit * 2` has moved out of the loop — it is invariant,
> so it is computed once. But `f.alt` has **not** moved, even though it looks
> just as invariant, because the loop contains an `advance` and an `advance`
> changes the altitude. That is the domain rule, and without it this loop would
> never terminate.

---

## Act 4 — The back end (1 minute 30)

```sh
./aerodsl --nextuse --bc examples/05_loop.aero | head -40
```

> Next-use information: for each instruction, when each live value is needed
> again. The allocator uses it to decide what to spill — the value needed
> furthest in the future, which is Belady's rule restricted to a block.

```sh
./aerodsl --bc examples/02_converging.aero | sed -n '18,24p'
```

> And in the bytecode, `jrel r0 >= r1` — the comparison and the branch have been
> fused into one instruction, and the condition has been *inverted* so the
> peephole pass could delete the jump that followed it.

**SSA:**

```sh
./aerodsl --ssa examples/05_loop.aero | head -24
```

> Dominance frontiers, and then exactly one phi function: `waited` at the loop
> header, taking version 1 from before the loop and version 3 from the back
> edge. One phi, not five, because the placement is pruned by liveness.
>
> I should say plainly: this form is built and printed, but no pass uses it. The
> optimiser works on the ordinary three-address code.

---

## Act 5 — What the compiler catches (2 minutes)

```sh
./aerodsl examples/09_type_errors.aero
```

> Ten distinct mistakes, ten errors — plus four warnings about the variables
> they left unread. Each at its own line and column.
> Not a cascade — a failed subexpression poisons its parents silently so one
> mistake produces one message.

```sh
./aerodsl examples/08_syntax_errors.aero
```

> Six syntax errors reported in one run. The parser recovers at the start of the
> next line that begins a statement.

**Then the distinctive ones:**

```sh
./aerodsl examples/10_domain_errors.aero
```

Point at four lines of output:

All seven of its diagnostics:

```
10:1: semantic error:   the operand of 'heading' must be between 0 and 360 deg, found 450
14:1: semantic error:   the operand of 'heading' must be between 0 and 360 deg, found 500
14:1: semantic warning: this 'heading' for AI202 replaces the one at line 10
                        before it can take effect; the first has no effect
17:1: semantic error:   the operand of 'speed' must be between 60 and 600 kt, found 40
21:1: semantic warning: BA117 was cleared 'climb' at line 20 and 'descend' here
                        with no 'advance' between them: the two contradict each other
25:1: semantic warning: BA117 was cleared 'heading' at line 24 and 'direct' here
                        with no 'advance' between them: both assign the lateral path
32:1: semantic error:   'advance' needs a positive number of seconds, found 0;
                        time cannot run backwards
```

Point at four of them:

> Line 10 is a literal 450. Line 14 is a variable holding 450 — the same
> message, because this check runs after constant propagation and the compiler
> knows what the variable holds.
>
> Lines 21 and 25 are the ones no general-purpose compiler could produce. Two
> clearances that contradict each other, and two that both assign the lateral
> path, found by scanning the basic block because there is no `advance` between
> them to let the first take effect.

**Finish live:**

```sh
make ui          # or ./run-demo.sh; on Windows double-click run-demo.bat
```

*Open `http://127.0.0.1:8080`. The page uses the same look as the Review 1
slides, and opens on "Climb to cruise · example 13 · Review 2: slides 7 to 10" with the **Scenario** phase
showing: the exact code and output from slides 7 to 10 of the Review 2 deck
(the program is the one on slide 5 of Review 1). Every sample is a file in `examples/`, named with
the slides that use it. Then pick "Converging pair",
click through the phases, and on the **Plan** phase press play or drag the time
slider.*

> Every phase, and the plan view drawn from positions the VM recorded every ten
> simulated seconds. Press play and you can watch the two tracks close — the
> red marker is where the monitor recorded the loss of separation. The buttons
> above the plan pick which aircraft is drawn in blue.
>
> Then switch the example to "Errors the compiler catches": the pipeline at the
> top stops at the stage that failed, the error line turns red in the editor,
> and clicking a message jumps to its line. Ctrl+Enter recompiles at once.

---

## Questions you should expect, and where the answer is

| Question | Answer |
|---|---|
| Why not a Python library? | `VIVA.md` Q3 |
| How do you know the optimiser is correct? | `VIVA.md` Q28 — the differential test |
| Is the SSA actually used? | No. `VIVA.md` Q34 |
| Did you verify the output loads in BlueSky? | No. `VIVA.md` Q40 |
| What is the weakest part? | The flight model. `VIVA.md` Q40 |
| Show me a shift-reduce conflict in your grammar | There isn't one; `ARCHITECTURE.md` §4 |

---

## If something goes wrong

- **`make ui` says it cannot find `ui/index.html`** — run it from the project
  root.
- **The UI says "offline"** — the server is not running; start it again with
  `run-demo.bat` (Windows), `./run-demo.sh` or `./aerodsl --serve 8080`.
- **A command prints nothing** — diagnostics go to standard error and the
  scenario file to standard output. Add `--trace` or drop the redirection.
- **You want to show a failure cleanly** — `./aerodsl examples/07_lexical_errors.aero`
  is the shortest one, with seven lexical errors.
