# Syllabus mapping

Every topic in BCSE307L Compiler Design, and what this project does about it.

The four statuses mean exactly what they say:

| | |
|---|---|
| **implemented** | working code in this repository, exercised by the test suite |
| **partial** | some of the topic is implemented; the entry says which part is not |
| **on paper** | worked through in the documentation, but not implemented |
| **not covered** | neither implemented nor worked through |

A project that claimed to cover everything would be lying. Ten topics below are
*not covered*, and the viva answers in `VIVA.md` are prepared to say so.

---

## Module 1 — Introduction to compilation and lexical analysis

| Topic | Status | Where |
|---|---|---|
| Structure and phases of a compiler | implemented | the whole pipeline; `ARCHITECTURE.md` §1 |
| Design issues | implemented | every phase documents its trade-offs; `SPEC.md` §1.4, §4.5, §11.1 |
| Patterns, lexemes, tokens, attributes | implemented | `src/lexer.c`; `Token` carries type, lexeme, numeric attribute, line, column |
| Specification of tokens | implemented | `SPEC.md` §3.3, as regular definitions |
| Extended regular expressions | implemented | the token patterns, including `FL[0-9]{3}` and `[0-9]{1,3}KT` |
| Regular expression to DFA, direct method | **on paper** | `ARCHITECTURE.md` §2 derives the DFA that separates `FLEVEL` from `CALLSIGN` by subset construction. The scanner does not build a table; it scans a word and classifies it, which is equivalent here because no two patterns share a prefix needing backtracking. |
| Lex, a lexical analyser generator | **not covered** | The lexer is hand-written, deliberately: a generator would have made the scanner a black box in exactly the phase a reader most wants to trace. |
| Introduction to LLVM | **not covered** | No LLVM anywhere. The back end targets a VM written for this project. |

**Run it:** `./aerodsl --tokens examples/02_converging.aero`

---

## Module 2 — Syntax analysis

| Topic | Status | Where |
|---|---|---|
| Role of the parser | implemented | `src/parser.c` |
| Parse tree / abstract syntax tree | implemented | `src/ast.c`; `--ast` prints it with types |
| Elimination of ambiguity | implemented | `SPEC.md` §4.4: no dangling else (`end` closes every block), one production per precedence level, non-associative comparison so `a < b < c` is rejected |
| Top-down parsing | implemented | `src/parser.c` |
| Recursive-descent parsing | implemented | one function per production |
| LL(1) grammars | implemented | the grammar is LL(1) by construction; `SPEC.md` §4.4 shows the three places it could have failed and why it does not |
| Shift-reduce parsers | **on paper** | `ARCHITECTURE.md` §4 |
| SLR parser table construction | **on paper** | `ARCHITECTURE.md` §4 works the full construction — FIRST, FOLLOW, the twelve LR(0) item sets and the SLR table — on the arithmetic core of AeroDSL's expression grammar, and shows why states 2 and 9 have no shift-reduce conflict |
| Operator-precedence parsing | **not covered** | Precedence is structural in the recursive-descent parser rather than table-driven |
| CLR and LALR construction | **not covered** | The grammar is LALR(1), but the canonical and merged item sets are not worked out |

**Run it:** `./aerodsl --ast examples/02_converging.aero`

---

## Module 3 — Semantic analysis

| Topic | Status | Where |
|---|---|---|
| Syntax-directed definitions | implemented | `src/sema.c` is an SDD over the AST: `Expr.type` is a synthesised attribute, and `check_expr` is the collected semantic rules |
| Evaluation order | implemented | one post-order walk, which is all a purely synthesised SDD needs |
| Applications of syntax-directed translation | implemented | type checking, symbol-table construction during the parse, and the translation scheme in `src/ir.c` |
| Syntax-directed translation schemes | implemented | `src/ir.c`; the rules are given below |
| L-attributed definitions | implemented | the backpatching scheme in `src/ir.c` is L-attributed: `truelist` and `falselist` are synthesised, and the label a statement's `nextlist` is patched to is inherited from the enclosing construct. Because every attribute depends only on the parent and on siblings to its left, the whole translation happens in one left-to-right pass with no attribute-dependency graph to build. |

The scheme for the boolean operators, in the textbook's notation:

```
B → B₁ or M B₂   { backpatch(B₁.falselist, M.instr);
                   B.truelist  = merge(B₁.truelist, B₂.truelist);
                   B.falselist = B₂.falselist }

B → B₁ and M B₂  { backpatch(B₁.truelist, M.instr);
                   B.truelist  = B₂.truelist;
                   B.falselist = merge(B₁.falselist, B₂.falselist) }

B → not B₁       { B.truelist  = B₁.falselist;
                   B.falselist = B₁.truelist }

M → ε            { M.instr = nextinstr }
```

`M` is the marker non-terminal. In a recursive-descent parser it is not a
non-terminal at all — it is the point in `gen_cond` where a label is placed
before the second operand is generated, which is the same thing expressed
differently.

**Run it:** `./aerodsl --symbols --ast examples/02_converging.aero`

---

## Module 4 — Intermediate code generation

| Topic | Status | Where |
|---|---|---|
| Variants of syntax trees | implemented | the AST, and the per-block DAG in `src/opt.c` |
| Three-address code | implemented | `src/ir.c`, as quadruples; `--ir` prints it |
| Types | implemented | `num`, `bool`, `flight`, `fix`; `SPEC.md` §5 |
| Declarations | implemented | `fix`, `flight`, `proc`, `let`; `src/symtab.c` assigns each a virtual register or table index |
| Procedures | implemented | `param` / `call` / `return`; `SPEC.md` §8.4 |
| Assignment statements | implemented | `src/ir.c`, `gen_stmt` |
| Translation of expressions | implemented | `src/ir.c`, `gen_expr`; `ARCHITECTURE.md` §6 |
| Control flow | implemented | `if`, `if/else`, `while`, with jumping code |
| Backpatching | implemented | `src/ir.c`: `makelist`, `merge`, `backpatch`, truelists and falselists |
| Switch-case statements | **not covered** | The language has no `switch`. A domain language for clearances had no use for one, and adding a construct purely to tick a box would have been padding. |

**Run it:** `./aerodsl --ir examples/02_converging.aero`

---

## Module 5 — Code optimisation

| Topic | Status | Where |
|---|---|---|
| Basic blocks | implemented | `src/cfg.c`, `cfg_build` |
| Loops in flow graphs | implemented | dominators, back edges, natural loops, nesting depth |
| Introduction to data-flow analysis | implemented | liveness (backward) and constant propagation (forward), both to a fixed point |
| Optimisation of basic blocks | implemented | the DAG pass: CSE, copy propagation, algebraic identities |
| DAG representation of basic blocks | implemented | `src/opt.c`, `pass_dag`, by value numbering |
| Principal sources of optimisation | implemented | constant folding and propagation, CSE, copy propagation, dead code, code motion, strength reduction (`x*2 → x+x`) |
| Loop optimisations | implemented | loop-invariant code motion, with the two soundness conditions in `ARCHITECTURE.md` §9 |
| Peephole optimisation | implemented | `src/codegen.c`: branch inversion, redundant move, store-load pair, jump to next, jump to jump |
| Machine-independent optimisation | implemented | every pass above runs on the three-address code, before any register is chosen |
| A naïve code generator for a virtual machine | implemented | `src/codegen.c` and `src/vm.c` |
| Security checking of virtual machine code | **partial** | The VM bounds execution — 20 million instructions, 24 hours of simulated time, 256 nested calls — and every clearance operand is range-checked before it is applied, so a scenario cannot run away or drive the model out of its domain. What is *not* done is verification of the bytecode itself before execution: there is no separate pass proving that jump targets are in range and that registers are defined before use. The code generator produces only well-formed bytecode, and a test checks that every jump target is in range, but that is a test of the generator rather than a verifier of arbitrary input. |

**Run it:** `./aerodsl --ir --opt-ir --stats examples/06_optimise.aero`

---

## Module 6 — Code generation

| Topic | Status | Where |
|---|---|---|
| Issues in the design of a code generator | implemented | `SPEC.md` §11.1 argues the choice of target; `ARCHITECTURE.md` §11 the rest |
| Target machine | implemented | AeroVM: 16 registers per frame, unbounded frame slots, 22 instructions |
| Next-use information | implemented | `src/codegen.c`, `compute_nextuse`; `--nextuse` prints it |
| Register allocation and assignment | implemented | on-demand allocation with spilling by furthest next use, and dirty tracking so a clean register costs no store |
| Runtime organisation | implemented | `src/vm.c`: a frame stack and a slot stack |
| Activation records | implemented | return address, caller's base, 16 registers, frame slots; arguments are written into the callee's first slots by the caller |

**Run it:** `./aerodsl --nextuse --bc examples/05_loop.aero`

---

## Module 7 — Parallelism

| Topic | Status | Where |
|---|---|---|
| Domain-specific languages | implemented | this is the project. `SPEC.md` §1 states the domain, the problem, who has it, and what a DSL buys over a library. |
| Compilation of a DSL | implemented | the whole pipeline, ending in a scenario file for an existing simulator |
| Static single assignment | implemented | `src/ssa.c`: dominance frontiers, pruned phi placement, renaming over the dominator tree. **Built and printed, not used by the optimiser** — `SPEC.md` §14 says so. |
| Impact of language design on the compiler | implemented | several places where a language decision changed the compiler, collected in `VIVA.md` Q27 — notably that making `clear` and `advance` syntax rather than library calls is what allows the barrier analysis and the clearance consistency checks to exist at all |
| Automatic parallelisation | **not covered** | — |
| Optimisation for cache locality | **not covered** | — |
| Vectorisation | **not covered** | — |
| Instruction scheduling | **not covered** | — |
| Software pipelining | **not covered** | — |

The state-barrier analysis in `src/opt.c` is a dependence argument of the same
family as the dependence testing that automatic parallelisation rests on: it
decides which instructions may be reordered relative to which. It is **not**
automatic parallelisation, and calling it that would be a stretch.

**Run it:** `./aerodsl --ssa examples/05_loop.aero`

---

## Module 8 — Contemporary issues

Not a fixed syllabus item. Two things in this project are the kind of thing
that belongs here:

- **Domain-specific languages as a check on correctness, not only as
  notation.** The `assert` construct lets a scenario state its own purpose and
  have the compiler verify it. That is the project's one genuinely unusual
  idea, and `SPEC.md` §12.4 and §1.3 set out what it does and does not
  establish.
- **Optimisation enabling diagnostics.** Range checking runs *after* constant
  propagation, so a violation written through a variable is caught at compile
  time. An optimisation is normally justified by the code it produces; here one
  is justified by the error message it makes possible.

---

## Summary

| Module | Implemented | Partial | On paper | Not covered |
|---|---|---|---|---|
| 1 Lexical analysis | 5 | 0 | 1 | 2 |
| 2 Syntax analysis | 6 | 0 | 2 | 2 |
| 3 Semantic analysis | 5 | 0 | 0 | 0 |
| 4 Intermediate code | 9 | 0 | 0 | 1 |
| 5 Optimisation | 10 | 1 | 0 | 0 |
| 6 Code generation | 6 | 0 | 0 | 0 |
| 7 Parallelism | 4 | 0 | 0 | 5 |
| **Total** | **45** | **1** | **3** | **10** |

The ten uncovered topics are LLVM, Lex, operator-precedence parsing, CLR and
LALR construction, switch statements, and the five parallelism topics. None of
them is hidden: each has a row above saying so, and `SPEC.md` §14 lists the
implementation's limitations separately.
