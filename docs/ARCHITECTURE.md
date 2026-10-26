# AeroDSL Compiler Architecture

How the compiler is built, stage by stage. Read `SPEC.md` first for what the
language is; this document is about how it is translated.

> Academic prototype. Not for real aircraft, live ATC infrastructure, or any
> safety-critical use.

---

## 1. Shape of the thing

```
  scenario.aero
        │
        ▼
  ┌───────────────┐   tokens      ┌───────────────┐   AST      ┌───────────────┐
  │    lexer      │ ────────────▶ │    parser     │ ─────────▶ │     sema      │
  │   lexer.c     │               │   parser.c    │            │    sema.c     │
  └───────────────┘               └───────┬───────┘            └───────┬───────┘
                                          │ names                      │ types
                                          ▼                            │
                                  ┌───────────────┐                    │
                                  │  symbol table │◀───────────────────┘
                                  │   symtab.c    │
                                  └───────────────┘
        ┌─────────────────────────────────────────────────────────────┐
        │                                                             │
        ▼                                                             │
  ┌───────────────┐  three-address  ┌───────────────┐   blocks   ┌────┴──────────┐
  │  ir generation│ ──────────────▶ │     CFG       │ ─────────▶ │   optimiser   │
  │     ir.c      │      code       │    cfg.c      │            │    opt.c      │
  └───────────────┘                 └───────┬───────┘            └───────┬───────┘
                                            │                            │
                                            ▼                            ▼
                                     ┌─────────────┐            ┌────────────────┐
                                     │     SSA     │            │ domain analysis│
                                     │   ssa.c     │            │   analysis.c   │
                                     └─────────────┘            └───────┬────────┘
                                                                        │
        ┌───────────────────────────────────────────────────────────────┘
        ▼
  ┌───────────────┐   bytecode    ┌───────────────┐   trace    ┌───────────────┐
  │ code generator│ ────────────▶ │    AeroVM     │ ─────────▶ │ scenario file │
  │  codegen.c    │               │     vm.c      │            │    scn.c      │
  └───────────────┘               └───────────────┘            └───────────────┘
```

`compile.c` is the driver. It runs as far as it can and stops at the first
phase that reports an error, so a file with type errors still has its tokens
and AST available for inspection. `main.c` is the command line, `server.c`
and `json.c` serve the demonstration UI, and `util.c` and `diag.c` are shared
infrastructure.

### Directory layout

```
AeroDSL/
├── Makefile
├── README.md
├── src/
│   ├── aerodsl.h      every shared type and every phase entry point
│   ├── util.c         allocation, string views, int lists, bit sets
│   ├── diag.c         located, phase-tagged, severity-tagged diagnostics
│   ├── lexer.c        §2
│   ├── parser.c       §4
│   ├── ast.c          AST construction, the domain tables, the tree printer
│   ├── symtab.c       §3
│   ├── sema.c         §5
│   ├── ir.c           §6
│   ├── cfg.c          §7
│   ├── ssa.c          §8
│   ├── opt.c          §9
│   ├── analysis.c     §10
│   ├── codegen.c      §11
│   ├── vm.c           §12
│   ├── scn.c          §13
│   ├── compile.c      the driver
│   ├── json.c         the phase dump the UI consumes
│   ├── main.c         the command line
│   └── server.c       the demonstration server
├── ui/index.html      the demonstration UI
├── examples/          seventeen scenarios, seven of which are meant to fail
├── tests/
│   ├── test_all.c     336 checks
│   └── run_examples.sh
└── docs/              SPEC, ARCHITECTURE, SYLLABUS, VIVA, DEMO, STATUS
```

### Shared infrastructure

**`Str`** is a pointer and a length into the source buffer. No name is ever
copied; the source outlives the compilation. This is why `unit_compile` says
in its header comment that the caller must keep the source alive.

**Diagnostics** are one flat list for the whole compilation. Each carries a
phase (`lexical`, `syntax`, `semantic`, `runtime`, `verify`), a severity, a
line, a column and a message. They are sorted by position before display, so
the output reads in source order even though the phases produced them in
pipeline order.

**Bit sets** (`BitSet`) back every dataflow analysis. **Int lists**
(`IntList`) back the backpatch lists, the CFG edge lists and the SSA renaming
stacks.

---

## 2. Lexical analysis

**In:** the source text. **Out:** a `TokenList` ending in `T_EOF`, plus any
lexical diagnostics.

The scanner reads left to right with no backtracking. On seeing a letter it
takes a maximal run of `[A-Za-z0-9_]` — a *word* — and then classifies the
whole word by the priority list in `SPEC.md` §3.4. Numbers are scanned
separately, because `.` is not a word character and `12.95` has to be one
token while `AI202.alt` has to be three.

Each token carries its type, its lexeme as a `Str`, a numeric attribute for
the three token types that have one, and its line and column.

### The one interesting classification

`FL320` matches both the flight-level pattern and the callsign pattern. The
flight level wins because it is tried first. Here is the DFA that decides it,
derived from the two regular definitions

```
FLEVEL   →  'F' 'L' D D D
CALLSIGN →  U ( U | D )*            U = [A-Z]   D = [0-9]
```

by subset construction:

```
         F            L            D            D            D
  S0 ───────▶ S1 ───────▶ S2 ───────▶ S3 ───────▶ S4 ───────▶ S5
   │           │           │           │           │           │
   │ other U   │ U, D      │ U         │ U         │ U         │ U, D
   ▼           ▼           ▼           ▼           ▼           ▼
  SC ◀───────────────────────────────────────────────────────────
   │
   │ U, D
   └──▶ SC

  accepting:  S1 S2 S3 S4 SC → CALLSIGN        S5 → FLEVEL
```

`S1` through `S4` accept, because `F`, `FL`, `FL3` and `FL32` are all perfectly
good callsigns. Only `S5` — `FL` followed by exactly three digits — accepts as a
flight level, and a sixth character takes it to `SC`, so `FL3200` is a callsign
again. The implementation does not build this table; it scans the word and then
applies the rules in order, which is equivalent because no two token patterns
share a prefix in a way that would need backtracking.

### Error handling

Every lexical error is reported, not just the first, and each one is attributed
to its exact column. If any occurred, the driver stops before parsing: a token
stream with holes in it produces cascades of meaningless syntax errors.

---

## 3. The symbol table

**Data structure:** two lists. `all` owns every symbol ever declared, so the
AST can keep pointing at one after its scope closes and so the table can be
dumped in full. `stack` holds only what is currently in scope, with
`starts[d]` marking where scope depth `d` begins.

Entering a scope pushes a mark; leaving one truncates the stack back to it,
which is what makes a name stop resolving. Lookup walks the stack backwards,
innermost first — that is what gives shadowing — and the walk is linear
because scenarios are small and a linear walk can be traced by hand in a viva.

Each entry records its kind, type, scope depth, declaration line and column,
whether it has been read, whether it has been assigned, and an index whose
meaning depends on the kind: a virtual register for variables and parameters, a
table index for aircraft and waypoints, a procedure index for procedures.
Because the declaration site is recorded, a redeclaration error can name both
the new and the original declaration.

### The barrier

`barrier` is a stack index below which only *global* kinds — aircraft,
waypoints, procedures — resolve. It is raised while parsing a procedure body
and restored afterwards. This is what implements "a procedure cannot see the
main body's variables", and the reason is the runtime rather than taste: a
main-body variable lives in main's activation record, so a procedure reading
one would be reading another frame.

---

## 4. Syntax analysis

**In:** the token list. **Out:** a `Program`, plus syntax diagnostics. Names
are resolved during the parse, because the grammar is declare-before-use.

One function per production, so the parser can be read next to the grammar in
`SPEC.md` §4 and the correspondence checked by eye. Expressions use one
function per precedence level — `parse_expr` for `or`, then `parse_and`,
`parse_not`, `parse_rel`, `parse_add`, `parse_mul`, `parse_unary`,
`parse_postfix` and `parse_primary` — which makes the precedence structural
rather than table-driven.

### Why recursive descent, and the bottom-up alternative

`SPEC.md` §4.5 gives the reasons: a readable parse, better diagnostics, and
directed error recovery. The course also covers bottom-up parsing, and the
grammar is LALR(1) as well, so the table construction is worked here on the
arithmetic core — a slice of AeroDSL's expression grammar, with the same
shape as the real thing.

```
(0)  E' → E
(1)  E  → E + T
(2)  E  → T
(3)  T  → T * F
(4)  T  → F
(5)  F  → ( E )
(6)  F  → num
```

**FIRST and FOLLOW**

| | FIRST | FOLLOW |
|---|---|---|
| `E` | `(`, `num` | `$`, `+`, `)` |
| `T` | `(`, `num` | `$`, `+`, `)`, `*` |
| `F` | `(`, `num` | `$`, `+`, `)`, `*` |

**The LR(0) item sets**

```
I0 : E' → ·E      I1 : E' → E·        I2 : E → T·        I3 : T → F·
     E  → ·E + T       E  → E· + T         T → T· * F
     E  → ·T
     T  → ·T * F  I4 : F → (·E )      I5 : F → num·     I6 : E → E + ·T
     T  → ·F           E → ·E + T                            T → ·T * F
     F  → ·( E )       E → ·T                                T → ·F
     F  → ·num         T → ·T * F                            F → ·( E )
                       T → ·F                                F → ·num
                       F → ·( E )
                       F → ·num

I7 : T → T * ·F   I8 : F → ( E· )     I9 : E → E + T·   I10 : T → T * F·
     F → ·( E )        E → E· + T          T → T· * F
     F → ·num
                                      I11 : F → ( E )·
```

**The SLR(1) table** (`sN` shift to state N, `rN` reduce by production N)

| state | `num` | `+` | `*` | `(` | `)` | `$` | `E` | `T` | `F` |
|---|---|---|---|---|---|---|---|---|---|
| 0 | s5 | | | s4 | | | 1 | 2 | 3 |
| 1 | | s6 | | | | acc | | | |
| 2 | | r2 | s7 | | r2 | r2 | | | |
| 3 | | r4 | r4 | | r4 | r4 | | | |
| 4 | s5 | | | s4 | | | 8 | 2 | 3 |
| 5 | | r6 | r6 | | r6 | r6 | | | |
| 6 | s5 | | | s4 | | | | 9 | 3 |
| 7 | s5 | | | s4 | | | | | 10 |
| 8 | | s6 | | | s11 | | | | |
| 9 | | r1 | s7 | | r1 | r1 | | | |
| 10 | | r3 | r3 | | r3 | r3 | | | |
| 11 | | r5 | r5 | | r5 | r5 | | | |

There is no conflict anywhere. The two states that could have one are 2 and 9,
where a reduction competes with shifting `*`: `*` is not in `FOLLOW(E)`, so the
reduce entry is not placed on `*` and the shift stands unopposed. That is
precisely how precedence falls out of an SLR table, and it is the same fact
that the recursive-descent parser expresses by having `parse_add` call
`parse_mul`.

The trade-off is explicit: a generator would guarantee the tables are right, at
the cost of worse diagnostics and a build dependency. This project chose
diagnostics.

### Error recovery

On an error the parser reports it and then skips forward to the first token
that can begin a statement *and* is the first token on its line. AeroDSL has no
statement terminator, but statements are written one per line, so the start of a
line is the most reliable place to pick the parse back up; it stops one bad
statement from being re-parsed in pieces.

Two details took debugging and are worth recording:

- Resynchronisation runs **only after an error**. Skipping tokens after a
  successful statement silently swallowed the stray `2` in `advance 1 2`.
- The skip does **not** unconditionally consume a token first. A failed
  statement has usually already consumed its own tokens, leaving the parser
  exactly where the next statement begins, and skipping one more swallowed that
  statement whole. Forward progress is guaranteed by the caller instead, which
  advances only if the position did not move at all.

A missing operand is reported at the keyword that needs it rather than at
whatever follows, so `clear AI202 climb` says *"'climb' requires a value in
ft"* at the `climb`, not *"expected a value"* on the next line.

---

## 5. Semantic analysis

**In:** the AST and the symbol table. **Out:** every expression annotated with
a type, plus semantic diagnostics.

One walk. `check_expr` returns a type and stores it on the node;
`check_stmt` checks the statement's own rules and recurses.

**Error suppression** is the detail that keeps the output readable: a
subexpression that already failed has type `TY_ERROR`, and an operator with a
`TY_ERROR` operand reports nothing and yields `TY_ERROR` in turn. One mistake
therefore produces one message.

What it checks is listed in `SPEC.md` §7.1 and §7.5. What it deliberately does
**not** check is operand *ranges*: those run later, on the optimised code,
where constant propagation has already replaced variables by their values. §10
below explains why.

Two of the warnings are worth a note:

- **"this loop cannot make progress"** fires when a `while` body neither
  advances the simulation nor assigns any variable the condition reads, and
  calls no procedure. That is a sufficient condition for non-termination, not a
  decision procedure — the halting problem forbids one — so loops it cannot
  prove anything about are bounded by the runtime limits instead.
- **"nothing refers to it again"** for an aircraft uses the symbol's `used`
  flag rather than tracking clearances, because an aircraft that simply flies
  through a scenario without a clearance is perfectly legitimate.

---

## 6. Intermediate code generation

**In:** the AST. **Out:** one `Func` per procedure plus `Func` 0 for the main
body, each holding a list of quadruples.

Operands are virtual registers, constants, labels, or small indices
(procedure, string, aircraft, waypoint, field, clearance kind, built-in).
Virtual registers are unbounded and numbered per procedure, and **both named
variables and compiler temporaries are virtual registers**, which is what lets
the optimiser treat them uniformly.

Constants appear as immediate operands rather than being loaded into a register
first. `IR_CONST` therefore appears only as the *result* of constant folding,
which makes the before-and-after dump in the demonstration read clearly:
`t2 = t0 - t1` becomes `t2 = 30000`.

### Short-circuit boolean translation and backpatching

This is the part worth reading. A boolean expression in a condition is not
evaluated into a value; it is translated into *jumping code*, and because a
jump has to be emitted before its target is known, each boolean carries a
**truelist** and a **falselist** of instruction indices whose target is still
blank. The enclosing statement fills them in when it places the label. That is
backpatching, and it is why the generator needs only one forward pass.

For `B1 and B2`: generate `B1`; place a label and patch `B1`'s truelist to it;
generate `B2`. The result's truelist is `B2`'s, and its falselist is the two
merged. For `or`, the same with the lists exchanged. For `not`, exchange the
lists and emit nothing at all.

So `if AI202.alt > 30000 and hsep(AI202, BA117) < 5 then ... end` compiles to
this, which is `--ir` output rather than a reconstruction:

```
    t0 = AI202.alt
    t1 = t0 > 30000          <- the comparison, as a value
    if t1 goto L0            <- truelist, patched when L0 was placed
    goto L2                  <- falselist, patched to the exit
L0:
    t2 = hsep(AI202, BA117)  <- only reached when the first test held
    t3 = t2 < 5
    if t3 goto L1
    goto L2
L1:
    <the body>
L2:
```

and the `hsep` call — which in the VM measures every aircraft pair — is simply
not executed when the altitude test fails.

A boolean used as a *value* rather than as a condition (`let converging =
hsep(a,b) < 5`) is materialised instead, by generating the jumping code and then
assigning 1 or 0 on the two paths. Both routines exist; which one runs depends on
the context the expression appears in.

### Control flow

`while c do S end` becomes

```
L_begin:                     ← the back edge at the end jumps here
    <code for c>
    if  → L_body             ← c's truelist, patched when L_body is placed
    goto → L_after           ← c's falselist, patched when L_after is placed
L_body:
    <code for S>
    goto L_begin
L_after:
```

The condition always emits both a conditional branch and an unconditional one,
because that is what `gen_cond` produces for any boolean. The pair looks
wasteful and is not: the bytecode peephole inverts the condition and deletes
the second jump, which is why `--bc` shows a single `jrel r0 >= r1`.

### Procedures and `enter`

A call evaluates its arguments, emits one `param` per argument, then `call p, n`.
The callee's parameters are virtual registers 0 to n−1.

An aircraft entering the airspace reuses the same mechanism: three `param`
instructions carrying altitude, heading and speed, then `enter F`. That avoided
inventing a four-address instruction in a three-address code.

---

## 7. Control-flow graph and dataflow

A block begins at the first instruction, at any label, and after any jump or
return. Edges follow the jump targets and the fall-through.

| Analysis | Algorithm | Used by |
|---|---|---|
| Reachability | forward traversal from the entry | unreachable-code removal |
| Dominators | iterative bit-vector: `DOM(n) = {n} ∪ ⋂ DOM(p)` | loops, SSA |
| Immediate dominators | the strict dominator with the largest dominator set | SSA |
| Back edges | an edge `n → h` where `h` dominates `n` | loop detection |
| Natural loops | blocks reaching `n` without passing `h`, plus `h` | LICM |
| Liveness | backward: `IN = USE ∪ (OUT − DEF)`, `OUT = ⋃ IN(succ)` | DCE, LICM, allocation, SSA |

All of them iterate to a fixed point in block order. Reverse postorder would
converge in fewer rounds; index order is easier to trace by hand and scenarios
are small.

`USE(B)` collects only *upward-exposed* uses — read in the block before any
assignment to the same register within it — which is what makes the liveness
equation correct.

---

## 8. Static single assignment

`ssa.c` builds and prints the SSA form. Cytron's algorithm:

1. **Dominance frontiers.** For every join block `b` and every predecessor `p`,
   walk up the dominator tree from `p` until `idom(b)`, adding `b` to each
   block's frontier on the way. A loop header turns out to be in its own
   frontier, which is exactly why a loop needs a phi for each value carried
   round it.
2. **Phi placement**, iterated to a fixed point because a phi is itself a
   definition. The form is **pruned**: a phi is placed only where the value is
   live on entry, using the same liveness everything else uses. Without the
   filter, a loop acquires a phi for every block-local temporary inside it.
3. **Renaming**, by a walk of the dominator tree carrying a stack of live
   versions per register, pushing on each definition and popping on the way
   out of a block.

For `examples/05_loop.aero` this produces exactly one phi:

```
B1:
      waited.2 = phi(waited.1 from B0, waited.3 from B3)
     6  L0:
     7  t1.1 = AI202.alt
     8  if t1.1 < 30000 goto L1
```

**This form is printed, not used.** The optimiser works on the ordinary
three-address code. Saying otherwise would misrepresent the project, and
`SPEC.md` §14 lists converting the optimiser to SSA as work that was not done.

One bug worth recording, because it is the kind that hides: `cfg_build` clears
the immediate-dominator field, and the code generator rebuilds the graph after
fusing compares. SSA built after that silently saw `idom = −1` everywhere, so
the dominator tree had no edges, renaming never recursed, and every version
printed as `undef` — while the dominance frontiers still *looked* plausible.
The fix was for `ssa_print` to recompute what it needs rather than trust what
the last phase left behind, and for the code generator to restore the analyses
it invalidates.

---

## 9. Optimisation

Seven passes, repeated until nothing changes, because each creates work for the
others.

| # | Pass | What it does |
|---|---|---|
| 1 | constant propagation | forward dataflow over a three-point lattice |
| 2 | constant folding | an instruction with constant operands becomes a constant |
| 3 | DAG | local CSE, copy propagation, algebraic identities |
| 4 | dead code | liveness-driven removal of instructions nothing reads |
| 5 | LICM | moves loop-invariant instructions to before the loop |
| 6 | jumps | threading, constant branches, unreachable blocks |
| 7 | compaction | removes the nops and unreferenced labels the rest leave |

Propagation runs **before** folding, because it is what gives folding something
to do.

The DAG pass builds, per basic block, a graph in which each distinct computation
is a node identified by its operator and the *value numbers* of its operands. An
instruction whose computation already has a node is a common subexpression and
becomes a copy from the register already holding it.

### The state barrier

`clear`, `advance`, `enter` and `call` can change the simulated world. Reads of
that world — a flight field, or one of the state-dependent built-ins — must
never be moved across them. The optimiser treats those four as **barriers**: a
barrier invalidates every DAG node for a state-dependent computation, and an
instruction is not loop-invariant if it is state-dependent and the loop contains
a barrier.

This is the one domain-specific rule in the optimiser and without it everything
else would be wrong. `while AI202.alt < FL300 do advance 10 end` would have its
altitude read hoisted out of the loop and would never terminate. The test suite
checks exactly this case.

### Two soundness conditions worth stating

**Algebraic identities.** `x+0`, `x−0`, `x*1`, `x/1` become copies and `x*2`
becomes `x+x`. `x*0 → 0` and `x−x → 0` are deliberately *not* done: they are the
two identities that fail for a non-finite `x`. The ones that are done are exact
for finite values, and finiteness is guaranteed from the other end — a constant
zero divisor is a semantic error, a runtime zero divisor is a runtime error, and
the VM rejects any arithmetic result that is not finite.

**Loop-invariant code motion** additionally requires that the destination be
dead on every exit edge of the loop. Without it:

```
let x = 1
while c do  x = 5   advance 1  end
print x
```

`x = 5` passes every other test, and hoisting it prints 5 even when the loop body
never ran. Division is also excluded from hoisting, because a loop body that
never executes must not be able to raise an error the original program could not.

---

## 10. Domain-specific static analysis

`analysis.c` runs on the **optimised** three-address code, and that placement is
the point.

**Range checking.** Constant propagation has by now replaced variables by their
values, so

```
let h = 450
clear AI202 heading h
```

is caught at compile time exactly as a literal would be. This is the clearest
payoff from having an optimiser in a project that does not otherwise need fast
code: the optimisation makes a diagnostic possible that was not possible before.

**Clearance consistency.** Within a basic block the clearance sequence for each
aircraft is straight-line, so "two clearances with no `advance` between them" is
a scan of the block, and the commanded level can be tracked through it to decide
whether a `climb` actually climbs. Clearances in different blocks are not
compared, which makes the check sound but incomplete — it never reports a
contradiction that is not one, and misses contradictions that span a branch.
For a warning about something that is legal, that is the right way round.

---

## 11. Code generation

Three steps.

**Compare-and-branch fusion** happens on the three-address code, not the
bytecode, because the liveness that proves the comparison's result is not needed
elsewhere is computed there. `t = a < b` consumed immediately by a branch on `t`,
with `t` dead after, becomes `if a < b goto L`.

**Next-use information** is computed by one backward scan per block: at each
instruction, record what is pending; then kill the destination, because an
earlier instruction's view of that register refers to a different value; then
record the reads.

**Register allocation** is the textbook on-demand scheme over 16 registers.
Every virtual register also has a home frame slot, and the slot of register `v`
is `v`, which puts a procedure's parameters in slots 0..n−1 where the caller
writes them. `getreg` takes an empty register, then one whose value is already
dead, and only then spills the one whose value is needed **furthest in the
future** — Belady's rule, restricted to a block, which is what next-use
information is for. A register is written back only if it is *dirty*, so a value
loaded and never changed costs no store.

The destination register prefers, in order: the one the destination already
occupies; an operand register whose value dies at this instruction — safe
because the VM reads both sources before writing the destination; a fresh one.

Allocation is local to a block, so everything live is written back at a block
boundary and reloaded in the next. That is the price of not building an
interference graph, and `SPEC.md` §14 lists it.

**Peephole** on the bytecode: a conditional branch that only skips an
unconditional jump has its condition inverted and takes the jump's target (which
fires on every `if` and every `while`, since that is the shape jumping code
produces); a move to the register a value is already in; a store followed
immediately by a load of the same slot into the same register; a jump to the
next instruction; a jump to a jump. Removal is done by marking and then
compacting through a position map, so every jump target stays correct.

---

## 12. The AeroVM

A register machine with 16 registers per activation record. A frame holds the
return address, the caller's frame base, the registers and the frame slots. A
call allocates a frame, copies the staged arguments into the callee's first
slots and jumps; a return restores the caller's. That is the whole runtime
organisation: no displays, no static links, no nested procedures.

Values are plain doubles. An aircraft or waypoint reference is its table index
held in a register; the type checker has already proved which registers hold
which, so nothing is tagged at run time.

Woven into the instruction loop are the three things that make a VM the right
target for this language:

- **`advance`** integrates the flight model in one-second steps: altitude at
  1800 ft/min climbing and 2000 ft/min descending, heading at 3°/s, speed at
  1 kt/s, position on a flat-earth approximation. A `direct` clearance
  recomputes the heading toward the waypoint each step.
- **the separation monitor** runs after every step, measuring every pair, and
  records an event when a pair first comes inside 5 NM *and* 1000 ft.
- **`assert`** is checked against that state, which is how a scenario states and
  verifies its own intent.

Execution is bounded: 20 million instructions, 24 hours of simulated time, 256
nested calls. Exceeding any limit is a runtime error naming the construct
responsible, which is how the unbounded loops the static check cannot prove
anything about are contained.

Positions are sampled every ten simulated seconds into the result, which is what
the demonstration UI draws.

---

## 13. The scenario-file backend

The recorded timeline is written as a flat timestamped command list: `DEFWPT`
for each waypoint, `CRE` for each entry, and `ALT`, `HDG`, `SPD` or `ADDWPT`
for each clearance.

Because the output is produced from a run, conditionals and loops are already
resolved: what comes out is one concrete scenario, not a program. For scenario
generation that is the intent — the simulator wants a scenario — but it means
the output is only as faithful as this prototype's flight model, and it is not a
translation of the program's full semantics.

The format is BlueSky's `.scn` format. The files generated for examples 3 and
5 load and run in **BlueSky 1.1.1** (`bluesky/README.md`); other versions have
not been tried, and the times inherit this prototype's flight model.

---

## 14. Testing

`tests/test_all.c` holds 336 checks across every phase. Three carry most of the
weight:

- the **differential test** runs eighteen scenarios twice, with and without the
  optimiser, and requires the diagnostics, the event trace and the final flight
  state to be identical. An unsound optimisation fails it immediately.
- the **register-pressure test** puts twenty simultaneously live values through
  sixteen registers and checks the arithmetic against a figure computed by hand,
  so spilling and reloading are exercised rather than merely compiled.
- the **recursion test** keeps several activation records live at once, and a
  companion check confirms runaway recursion is reported rather than crashing.

`tests/run_examples.sh` checks that each example behaves as its header says and
that every command-line flag works.

`tests/fuzz.py`, run by `make fuzz`, feeds the compiler random token sequences,
random bytes, and valid examples with a few random byte edits, and fails on any
exit status that is not 0, 1 or 2. It runs against the sanitiser build, so a
latent memory error surfaces as a crash rather than as quiet corruption. At the
time of writing the whole suite runs clean under AddressSanitizer and
UndefinedBehaviorSanitizer and 3000 fuzz inputs produce no crash.
