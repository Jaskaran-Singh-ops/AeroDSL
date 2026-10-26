# Viva questions and answers

Forty questions with answers that are true of the code in this repository.
The uncomfortable ones are here too, because those are the ones that get
asked. Where the honest answer is "we did not do that", it says so.

---

## The project and its premise

### 1. What problem does this solve, in one sentence?

Writing air-traffic-control training scenarios by hand as flat timestamped
command lists is unmaintainable and unchecked, so AeroDSL lets you write the
scenario as a program with variables, conditionals and procedures, checks it,
simulates it, verifies the claims you make about it, and generates the flat
file the simulator wants.

### 2. Who actually has that problem? How do you know?

People who author scenarios for ATC simulators — instructors running training
exercises, and researchers using simulators such as BlueSky. What I can defend
is a claim about the *format*: that a flat timestamped command list has no
variables, no conditionals, no procedures and no validation is evident from the
format itself. What I have **not** done is survey scenario authors, so I cannot
tell you how much time this costs them in practice. `SPEC.md` §1.2 states that
as an unvalidated assumption rather than hiding it.

### 3. Why does this need a new language? Why not a Python library that emits the file?

That is a real alternative and for part of the problem it would be enough:
Python already has variables, conditionals and functions. Three things a
library does not give you:

- **Static checking against the domain.** `clear AI202 heading 450` is rejected
  before anything runs, at the line and column where it was written, because
  `heading` is a language construct with a declared operand range. In a library
  it is an ordinary function call whose argument is checked, at best, when it
  executes.
- **Analysis across statements.** The compiler detects that two clearances
  contradict each other, or that one is overwritten before it can take effect,
  by analysing the generated code. That is a dataflow question about the
  program, which a compiler is built to answer and a library has no access to.
- **A closed surface.** An AeroDSL scenario cannot open a file, make a network
  call, or loop forever unnoticed. A scenario that is a Python script can do
  anything.

It is a trade-off, not a knock-out argument, and `SPEC.md` §1.4 presents it as
one.

### 4. Is this safe? Could it be used for real air traffic control?

No, and it is built so that the question cannot arise by accident. It has no
connection to anything: it reads a text file and writes a text file. The flight
model is kinematic with fixed rates — no mass, no wind, no performance
envelope, no pilot response delay. The separation standard is two hard-coded
constants. The waypoints are whatever the scenario declares. Nothing has been
reviewed by anyone with ATC expertise. Every document, the compiler's `--help`,
and the first lines of the generated file all say this.

### 5. What is the single most original thing here?

`assert`. A training scenario exists to produce a specific situation — a
converging pair the trainee has to resolve — and in a flat file that intent is
written nowhere and checked by a human watching a replay. In AeroDSL the
scenario states its own purpose, `assert closest < 5, "..."`, and the compiler's
simulator verifies it. An edit that quietly removes the conflict becomes a
compiler error. `examples/02_converging.aero` asserts the conflict happens;
`examples/12_separation.aero` is the same geometry resolved, asserting it does
not.

I have not verified that nobody has done this before, so I claim it as the
design choice this project made, not as a first.

---

## Lexical analysis

### 6. Why is case significant, and why that way round?

Lower case is the language — keywords, variables, procedures. UPPER CASE is the
airspace — callsigns and waypoints. It mirrors how aviation writes these things,
and it means the lexer can tell a variable from an aircraft without consulting
the symbol table. It also turns a common typo into a diagnosable one: `Cruise`
is a lexical error with a message explaining the convention, rather than a
silently undeclared name.

### 7. `FL320` matches both the flight-level pattern and the callsign pattern. How is that resolved?

By priority: the flight level is tried first, so it wins, and an aircraft may
not be called `FL` plus exactly three digits. `FL32`, `FL3200` and `FLXYZ` are
ordinary callsigns. `ARCHITECTURE.md` §2 derives the DFA by subset construction
and shows the one accepting state where the two patterns compete.

### 8. Is `FLXYZ` a lexical error?

No, and deliberately not. It is a well-formed callsign; whether anything of that
name was declared is a question for the symbol table, not the scanner. The
lexer's job is classification. If it does not resolve, the semantic analyser
reports it, and because the name begins with `FL` the message adds that a flight
level is written as `FL` and exactly three digits — which is usually what the
author meant.

### 9. Why hand-write the lexer instead of using Lex?

Because the scanner is the phase a reader most wants to trace, and a generator
turns it into a table. The token set has no overlapping prefixes requiring
backtracking, so the scanning and classification logic is about 180 lines and
can be read straight through. The cost is that I had to get the priority order
right myself, which `ARCHITECTURE.md` §2 documents and the tests check.

### 10. How do you handle `12.95` when `.` is also the field-access operator?

The scanner branches on the first character. A word — a run of
`[A-Za-z0-9_]` — is scanned and classified whole. A number is scanned
separately: digits, then an optional fractional part only if a digit follows
the point, then any trailing letters (which make it a speed, or an error). So
`12.95` is one token and `AI202.alt` is three, because `AI202` starts with a
letter and takes the word path.

---

## Syntax analysis

### 11. Why recursive descent rather than an LALR generator?

Three reasons, and one admission.

Readability: one function per production, so the parser can be read next to the
grammar. Diagnostics: a hand-written parser knows what it was trying to parse,
so it says *"'climb' requires a value in ft, found 'clear'"* rather than
*"syntax error"*. Recovery: it can resynchronise where it chooses.

The admission is that a generator would guarantee the tables are right, and I
have to argue that by hand instead. `ARCHITECTURE.md` §4 works the full SLR(1)
construction on the expression core to show the grammar really is unambiguous.

### 12. Show me that the grammar has no shift-reduce conflict.

`ARCHITECTURE.md` §4 has the twelve LR(0) item sets and the SLR table for

```
E → E + T | T      T → T * F | F      F → ( E ) | num
```

The two states that could conflict are state 2 (`E → T·`, `T → T· * F`) and
state 9 (`E → E + T·`, `T → T· * F`). In both, the competing action is shifting
`*`. Because `FOLLOW(E) = { $, +, ) }` does not contain `*`, no reduce entry is
placed on `*`, so the shift stands unopposed. That is precedence falling out of
the table, and it is the same fact the recursive-descent parser expresses by
having `parse_add` call `parse_mul`.

### 13. How is the dangling-else ambiguity avoided?

It cannot arise: `if` is terminated by `end`, so `if a then if b then x else y
end end` brackets explicitly. There is no production in which an `else` could
attach to either of two `if`s.

### 14. Why is `a < b < c` a syntax error rather than meaning what C means?

Because what C means is almost never what was intended. The comparison
production allows at most one comparison operator, so the chain is caught by
the grammar and the message says to write `a < b and b < c`. It removes a class
of bug by construction, which is the kind of thing a small language can afford
to do.

### 15. How does error recovery work, and why that way?

On an error the parser reports it and skips forward to the first token that can
begin a statement **and** is the first token on its line. AeroDSL has no
statement terminator, but statements are written one per line, so the start of a
line is the most reliable place to pick the parse back up.
`examples/08_syntax_errors.aero` has six mistakes and the compiler reports all
six, one per mistake.

Two bugs in the first version are worth mentioning because both were silent.
Resynchronising after *every* statement, not only after a failed one, swallowed
the stray `2` in `advance 1 2`. And unconditionally skipping one token before
searching swallowed the whole statement after an error, because a failed
statement has usually already consumed its own tokens. Both are now in the
tests.

### 16. Is the grammar LL(1)? What about `fix`, which is both a keyword and a type name?

Yes. `fix` and `flight` each serve as a declaration keyword at item level and as
a type name inside a parameter list, and those are different positions, so one
token decides. Every statement begins with a distinct keyword except an
assignment, and an assignment begins with an identifier, which no other
statement does — so the parser chooses its production from the first token
alone, with no lookahead beyond it.

---

## Semantic analysis

### 17. What does your symbol table store, and how does shadowing work?

Two lists. One owns every symbol ever declared, so the AST can keep pointing at
one after its scope closes and so the table can be dumped in full. The other is
the scope stack: entering a scope pushes a mark, leaving one truncates back to
it. Lookup walks the stack backwards, innermost first, which is what gives
shadowing. Each entry carries kind, type, scope depth, declaration position,
whether it has been read, whether it has been assigned, and an index — a virtual
register for variables, a table index for aircraft and waypoints.

### 18. Why can a procedure not see the main body's variables?

Because of the runtime, not taste. A main-body variable lives in main's
activation record. A procedure reading one would be reading another frame, and
this VM has no static links or displays to make that meaningful. So the language
has no global variables: a procedure sees its parameters, its own `let`
declarations, and the global declarations. The symbol table implements it with a
*barrier* — a stack index below which only aircraft, waypoints and procedures
resolve — raised while parsing a procedure body.

### 19. Why does `if AI202.alt then` not work?

Because there is no implicit conversion from `num` to `bool`. The condition of
`if`, `while` and `assert` must have type `bool`, and the error message says so
and suggests writing a comparison. In a language about altitudes and headings,
"non-zero means true" would be a trap: `if AI202.hdg then` would be false only
when the aircraft is pointing exactly north.

### 20. How do you avoid a cascade of errors from one mistake?

An expression that has already failed is given the type `TY_ERROR`, and any
operator with a `TY_ERROR` operand reports nothing and yields `TY_ERROR` in
turn. One mistake produces one message.
`examples/09_type_errors.aero` has ten distinct mistakes and produces ten
errors, not thirty.

---

## Intermediate code

### 21. Explain backpatching, and show me where it is in your code.

A boolean expression in a condition is translated into jumping code, not into a
value. The jumps have to be emitted before their targets are known, so each
boolean carries a *truelist* and a *falselist* of instruction indices whose
target is still blank, and the enclosing statement fills them in when it places
the label.

In `src/ir.c`: `makelist`, `merge` and `backpatch`, used by `gen_cond`. For
`B1 and B2`, `gen_cond` generates `B1`, places a label, patches `B1`'s truelist
to it, and generates `B2`; the result's truelist is `B2`'s and its falselist is
the two merged. `not` exchanges the lists and emits nothing at all.

It is why the generator needs only one forward pass over the tree.

### 22. Prove your `and` really short-circuits.

Two ways. Structurally, `--ir` shows that the second operand's code sits after a
label that only the first operand's true-branch reaches. Observably, this is in
the test suite:

```
let d = 0
print 1 > 2 and 10 / d > 1      → prints false
```

If the right operand were evaluated, the division by zero would be a runtime
error. It is not, so it was not.

### 23. Why do constants appear as operands rather than being loaded into temporaries first?

Because it makes the optimisation visible and the IR shorter. `t = a - 2000`
rather than `t1 = 2000; t = a - t1`. The consequence is that `IR_CONST` only
ever appears as the *result* of constant folding, so the before-and-after dump
reads clearly: `t2 = t0 - t1` becomes `t2 = 30000`.

### 24. How does an aircraft entering the airspace work in three-address code?

An `enter` needs four things — the aircraft, an altitude, a heading and a speed
— and three-address code has room for two operands. Rather than inventing a
four-address instruction, it reuses the parameter mechanism: three `param`
instructions then `enter F`, exactly as a procedure call stages its arguments.
The VM reads them from the same argument buffer.

---

## Control flow and dataflow

### 25. How do you find loops?

Dominators first, by the iterative bit-vector algorithm: `DOM(n)` is the
intersection of `DOM(p)` over all predecessors plus `n` itself. Then a *back
edge* is an edge `n → h` where `h` dominates `n`, and each one marks `h` as a
loop header. The natural loop of a back edge is the set of blocks that reach `n`
without passing through `h`, plus `h`, found by a backward traversal.

### 26. Write the liveness equations and say what they are for.

```
LIVEout(B) = ⋃ LIVEin(S)  over successors S
LIVEin(B)  = USE(B) ∪ (LIVEout(B) − DEF(B))
```

solved by iterating to a fixed point. `USE(B)` must be the *upward-exposed*
uses — read in the block before any assignment to the same register within it —
or the equation is wrong. Liveness drives dead-code elimination, the
"dead on every loop exit" test in code motion, the register allocator's choice
of what to spill, and the pruning of phi functions in SSA.

---

## Optimisation

### 27. Name a place where a language-design decision changed the compiler.

Several, but the important one: `clear`, `advance` and `assert` are *syntax*,
not library calls. Because they are instructions in the IR, the optimiser can
see them, and two things follow that would otherwise be impossible — the barrier
analysis that keeps state reads from moving across an `advance`, and the
clearance consistency checks that compare two clearances within a block. Had
these been function calls, the compiler would have seen an opaque call and
could have concluded nothing.

Two others: making comparison non-associative removed a class of bug from the
grammar rather than from the checker; and having no global variables made the
activation record trivial, at the cost of every procedure needing explicit
parameters.

### 28. Your optimiser moves code around. How do you know it does not change what the program does?

Three answers, in increasing order of strength.

One, every pass is written against a stated condition, and the two that needed
the most care are documented: the algebraic identities and loop-invariant code
motion, both in `ARCHITECTURE.md` §9.

Two, side-effecting instructions — `clear`, `advance`, `enter`, `assert`,
`print`, `param`, `call`, and every control transfer — are never removed by
dead-code elimination, whatever their result.

Three, and this is the real answer: the test suite runs eighteen scenarios twice,
once with the optimiser and once without, and requires the diagnostics, the
whole event trace and the final state of every aircraft to be identical. An
unsound optimisation fails that test immediately. It is the check I would point
at if you asked me what makes the optimiser trustworthy.

### 29. What is the "state barrier" and why does the optimiser need it?

`clear`, `advance`, `enter` and `call` can change the simulated world. Reads of
that world — a flight field, or `hsep`, `vsep`, `dist`, `bearing`, `now` — must
never be moved across them. The optimiser treats those four as barriers: a
barrier invalidates every DAG node for a state-dependent computation, and an
instruction is not loop-invariant if it is state-dependent and the loop contains
a barrier.

Without it, this loop breaks:

```
while AI202.alt < FL300 do
  advance 10
end
```

The altitude read is loop-invariant by every ordinary test — same operands,
no assignment to them inside the loop — so it would be hoisted out, the
condition would be evaluated once, and the loop would never terminate. It is the
one domain-specific rule in the optimiser and everything else depends on it.
There is a test for exactly this.

### 30. Why do you not simplify `x * 0` to `0`?

Because it is false for a non-finite `x`, and rewriting it would bake an
assumption about values into the point of rewriting rather than checking it at
the point of execution. The identities that *are* done — `x+0`, `x−0`, `x*1`,
`x/1` — are exact for finite values, and finiteness is guaranteed from the other
end: a constant zero divisor is a semantic error, a runtime zero divisor is a
runtime error, and the VM rejects any arithmetic result that is not finite. So
no non-finite value can exist for those four to be wrong about.

### 31. Give me a case where loop-invariant code motion would be wrong.

```
let x = 1
while c do  x = 5   advance 1  end
print x
```

`x = 5` has no side effect, is assigned nowhere else in the loop, and has a
constant operand — invariant by every usual test. Hoisting it prints 5 even when
the loop body never runs. The pass therefore also requires the destination to be
**dead on every exit edge** of the loop, which is the strict form of the usual
condition that the definition dominate every loop exit. That exact program is in
the test suite.

Division is excluded from hoisting for a related reason: a loop body that never
executes must not be able to raise an error the original program could not.

### 32. Why does constant propagation run before constant folding?

Because propagation is what gives folding something to do. Before propagation,
`cruise - margin` has two register operands and there is nothing to fold; after
it, both are constants. Running folding first means it fires only on literal
arithmetic, which a programmer rarely writes. The passes then repeat to a fixed
point, so the order matters for how quickly it converges rather than for the
final answer — but it also matters for what the demonstration shows, and the
honest reason I noticed is that the folding counter read zero.

### 33. What is the DAG for, exactly?

Within a basic block, each distinct computation becomes a node identified by its
operator and the *value numbers* of its operands. An instruction whose
computation already has a node is a common subexpression, and becomes a copy
from the register already holding it. The same structure is where algebraic
simplification happens. It is local to a block — there is no global
available-expressions analysis — which `SPEC.md` §14 lists as a limitation.

---

## SSA

### 34. What is SSA, where are the phi functions needed, and did you implement it?

SSA is the form in which every register is assigned exactly once, so a use names
its definition unambiguously. Where control flow joins and a register could hold
either of two definitions, a phi function names both.

Phi functions go at **dominance frontiers**. The frontier of a block `n` is the
set of blocks `n` does not strictly dominate but whose predecessors it does —
exactly where a definition in `n` stops being the only one that can reach. A
loop header is in its own frontier, which is why a loop needs a phi for each
value carried round it.

Yes, it is implemented: `src/ssa.c` computes dominance frontiers, places phis to
a fixed point, and renames by a walk of the dominator tree. The form is
**pruned** — a phi is placed only where the value is live on entry, using the
liveness the rest of the compiler already computes — so a loop does not acquire
a phi for every block-local temporary inside it.

**But no pass operates on the SSA form.** The optimiser works on the ordinary
three-address code. The SSA is built and printed for inspection, and converting
the optimiser to use it is the obvious next piece of work. `SPEC.md` §14 says
this, and I would rather say it than be caught claiming otherwise.

---

## Code generation

### 35. How does register allocation work, and what is next-use information for?

AeroVM has 16 registers per frame, and every virtual register also has a home
frame slot — the slot of register `v` is `v`, which puts a procedure's
parameters where the caller writes them.

Allocation is the textbook on-demand scheme, per block. `getreg` takes an empty
register; failing that, one whose value is already dead; failing that, it spills
the register whose value is needed **furthest in the future**. That last rule is
what next-use information is for, and it is Belady's rule restricted to a block.
A register is written back only if it is *dirty*, so a value that was loaded and
never changed costs no store.

The limitation is that allocation is local: everything live is written back at a
block boundary and reloaded in the next, because the allocator makes no
assumption about which register holds what on entry. A global allocator over an
interference graph would do better.

### 36. How do you know the spilling code is correct? It would be easy for that never to run.

There is a test built for exactly that doubt. It puts twenty simultaneously live
values through sixteen registers, derived from the aircraft's altitude so
constant propagation cannot fold them away, sums them, and checks the result
against a figure computed by hand — 480210. It also asserts that the generated
bytecode actually contains stores and loads, so the test fails if a future
change makes the spill path unreachable rather than silently passing.

### 37. What does your peephole pass do?

Compare-and-branch fusion happens first, on the three-address code rather than
the bytecode, because the liveness that proves the comparison's result is not
needed elsewhere lives there. Then on the bytecode: a conditional branch that
only skips an unconditional jump has its condition inverted and takes the jump's
target — which fires on every `if` and every `while`, because that is the shape
jumping code produces; a move into the register a value already occupies; a
store followed immediately by a load of the same slot into the same register; a
jump to the next instruction; a jump to a jump.

Removal is done by marking and then compacting through a position map, so every
jump target stays correct.

### 38. Why target your own VM rather than generating C or machine code?

Because the scenario has to *run* to produce the output. The scenario file
records what the scenario actually did, and conditionals and loops over
simulated state can only be resolved by simulating. A VM can also be
instrumented: every clearance, every separation measurement and every assertion
is observable from inside the instruction loop, which is what the verification
feature needs. Generating C would make the back end more conventional and make
the simulator and the instrumentation someone else's problem.

---

## Runtime and the domain

### 39. Describe your activation record and what happens on a call.

A frame holds the return address, the caller's frame base, 16 registers and the
function's frame slots. On a call the caller has already staged its arguments
with `arg` instructions; the VM allocates a frame on the slot stack, copies the
staged arguments into the callee's first slots — which is where its parameters
live, since the slot of virtual register `v` is `v` — and jumps. A return
restores the caller's frame and resumes at the saved address. No displays, no
static links, no nested procedures.

Recursion works; the test suite has a countdown procedure that keeps several
frames live. Depth is capped at 256, and exceeding it is a runtime error naming
the problem rather than a crash.

### 40. What would you do next, and what is the weakest part of this project?

The weakest part is the flight model. Everything above it is sound in its own
terms, but the model is kinematic with fixed rates, and the separation
monitoring is only as meaningful as the positions it measures. A scenario that
depends on precise timing is only as good as that model, and the generated file
inherits the same limitation.

The weakest part *of the compiler* is that register allocation and
common-subexpression elimination are both local to a basic block, and that the
SSA form is built but unused. Those three are related: a global allocator over
an interference graph and a global value-numbering pass both want SSA, so
converting the optimiser to SSA is the one change that would unlock the most.

Three things I would do next, in order: use the SSA form for the optimiser; make
units part of the type system, so `clear AI202 climb 280` is rejected rather
than silently meaning 280 feet; and compile altitude waits into BlueSky's
`ATALT` trigger. We ran the generated files in BlueSky 1.1.1: they load and
run, but in example 5 the turn comes about 1,060 ft early, because BlueSky's
A320 climbs more slowly than our model.
