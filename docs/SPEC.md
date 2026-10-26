# AeroDSL Language Specification

**Version 2.0**

> **Scope statement.** AeroDSL is an academic prototype built for a Compiler
> Design course project. It generates and checks *training scenarios for
> air-traffic-control simulators*. It is not connected to, and must not be
> connected to, real aircraft, live ATC infrastructure, or any
> safety-critical system. Its flight model, its separation standard and its
> waypoint data are deliberate simplifications chosen for teaching, not
> operational values. Nothing in this document is aviation guidance.

---

## 1. Purpose and motivation

### 1.1 What the language is for

An ATC training scenario is a scripted traffic situation: a set of aircraft
with starting positions, and a timeline of clearances issued to them. Simulators
used for controller training read these scenarios from files. The widely used
open-source research simulator BlueSky, for example, reads `.scn` files that are
flat lists of timestamped commands, of the general form:

```
00:00:00.00>CRE AI202,A320,12.95,77.67,90,FL240,280
00:01:30.00>ALT AI202,FL320
00:02:00.00>HDG AI202,270
```

Each line is a time, a command and some arguments. The format is simple and that
is its virtue: a simulator can read it without any interpretation.

### 1.2 The problem

That same flatness is the problem for the person *writing* the scenario.

1. **No abstraction.** There are no variables, so a value used in ten places —
   a transition altitude, a cruise level, a standard arrival speed — is typed ten
   times. Changing it means finding all ten. There are no procedures, so a
   standard arrival pattern applied to twelve aircraft is written out twelve
   times.

2. **No conditions or repetition.** Every line is unconditional and
   absolutely timed. A scenario author who wants "when this aircraft reaches
   FL300, turn it onto 270" must work out *when* that happens — by hand, from
   climb rates — and write an absolute timestamp. If the aircraft's starting
   altitude is edited later, every dependent timestamp silently becomes wrong.

3. **Nothing is checked.** A `.scn` file is not validated before it runs. A
   heading of 450 degrees, a flight level above the aircraft's ceiling, a
   command naming an aircraft that was never created, two clearances that
   contradict each other — none of these are reported. They are discovered by
   running the simulator and watching.

4. **The author's intent is not expressed anywhere.** Training scenarios
   usually exist to create a *specific* situation: two aircraft converging
   with a planned loss of separation, so that the trainee controller has to
   resolve it. Whether the scenario actually produces that situation is not
   written in the file and is not checked. It is verified by a human watching
   the simulation.

Who experiences this: people who author scenarios for ATC simulators —
instructors, and researchers running experiments on simulators such as BlueSky.
The claims above are about the *format*: that a flat timestamped command list
has no variables, no conditionals, no procedures and no validation is evident
from the format itself. We have **not** surveyed how scenario authors work in
practice, and we do not claim to know how much time this costs them. That is an
assumption this project does not attempt to validate.

### 1.3 What AeroDSL does about it

AeroDSL is a small imperative language in which a scenario is a *program*:

- **Variables, expressions and procedures** give the abstraction the flat format
  lacks. One `let cruise = FL320` replaces ten literals.
- **Conditionals and loops over simulated state** (`if AI202.alt >= FL300 then
  ...`, `while ... do advance 10 end`) let the author say *when* something should
  happen in terms of the traffic situation, instead of computing a timestamp.
- **A compiler** checks the scenario before it runs: types, arity, operand
  ranges, undeclared aircraft and waypoints, contradictory clearances,
  unreachable code.
- **`assert`** lets the author state the scenario's intent — "these two aircraft
  must come within 3 NM of each other" — and the compiler's simulator verifies
  it.
- **The compiler's output is the flat scenario file.** AeroDSL does not replace
  the `.scn` format; it generates it. The flat file remains what the simulator
  consumes.

So the relationship is the ordinary one between a source language and a target:
you write the maintainable, checked thing, and the compiler produces the flat
thing the machine wants. The intended advantage is *for the author*, before the
simulation ever runs.

### 1.4 Why this needs a compiler and not a library

A legitimate objection: why not a Python library that emits `.scn` files?
That is a real alternative and for some of the above it would be enough —
Python already has variables, conditionals and functions.

The parts that a library does not give you, and that motivate a language with a
compiler, are:

- **Static checking against the domain.** `clear AI202 heading 450` is rejected
  before anything runs, at the line and column where it was written, because
  `heading` is a language construct with a declared operand range. In a library,
  `clear(ai202, "heading", 450)` is an ordinary function call whose argument is
  checked — at best — when it executes.
- **Analysis across statements.** The compiler detects that two clearances to
  the same aircraft contradict each other, or that one is overwritten before it
  can take effect, by analysing the generated code. This is a dataflow question
  about the program, which is exactly what a compiler is built to answer and
  what a library has no access to.
- **A closed, checkable surface.** A scenario in AeroDSL cannot open a file,
  make a network call or loop forever unnoticed — the language has no
  constructs for the first two, and the simulator bounds the third. A scenario
  that is a Python script can do anything.

This is a genuine design trade-off rather than a decisive argument, and nothing
in this document treats it as settled.

---

## 2. Design philosophy

1. **Small enough to implement completely, large enough to be a real
   language.** Expressions with precedence, control flow, procedures with
   parameters and a static type system — but no arrays, no records, no closures,
   no recursion requirement, no modules.
2. **Domain constructs are first-class, not library calls.** `clear`,
   `advance`, `assert` and flight state access are syntax, so the compiler can
   reason about them.
3. **Every error is attributed.** Phase, severity, line and column, always.
4. **The pipeline is observable.** Every intermediate form — tokens, AST,
   symbol table, three-address code, control-flow graph, bytecode, trace — can
   be printed. The compiler is meant to be looked inside.
5. **Honest limits.** Where the model is a simplification, the specification
   says so rather than implying realism.

**Source file extension:** `.aero`

---

## 3. Lexical structure

### 3.1 Character set and layout

Source is ASCII. Any byte outside the ranges used below is a lexical error.
Spaces, tabs, carriage returns and newlines separate tokens and are otherwise
insignificant; a statement may span lines and several statements may share a
line. There is no statement terminator.

A `#` begins a comment that runs to the end of the line.

### 3.2 Case convention

Case is significant and carries meaning, which mirrors aviation writing
practice:

- **Lower case** is the language: keywords, variables, parameters, procedures.
- **UPPER CASE** is the airspace: aircraft callsigns and waypoint names.

So `cruise` is a variable and `AI202` is an aircraft, and the lexer can tell
them apart without consulting the symbol table. `Cruise` and `aI202` are
lexical errors, which makes a common typo a diagnosable one.

### 3.3 Tokens

| Token | Pattern | Attribute | Example |
|---|---|---|---|
| `NUMBER` | `[0-9]+ ( "." [0-9]+ )?` | value | `270`, `12.95` |
| `FLEVEL` | `FL [0-9]{3}` | value × 100 ft | `FL320` → `32000` |
| `SPEED` | `[0-9]{1,3} KT` | value in knots | `280KT` → `280` |
| `IDENT` | `[a-z] [a-z0-9_]*` | — | `cruise`, `sep_min` |
| `CALLSIGN` | `[A-Z] [A-Z0-9]*` | — | `AI202`, `VOMAN` |
| `STRING` | `"` any but `"` or newline `"` | text | `"converging pair"` |

Keywords (all lower case, reserved, cannot be used as identifiers):

```
advance  alt      and      assert   bool     call     clear    climb
descend  direct   do       else     end      enter    false    fix
flight   hdg      heading  if       lat      let      lon      not
num      or       print    proc     return   scenario spd      speed
then     true     type     while
```

The type names `num`, `bool`, `flight` and `fix` are keywords. `fix` and
`flight` are each both a declaration keyword and a type name; the parser
distinguishes them by position, and section 4.4 explains why that keeps the
grammar LL(1). `alt`, `hdg`, `spd`, `lat` and `lon` are keywords because they
name both a flight declaration's fields and the readable state fields of
section 5.2, which makes `AI202.alt` purely syntactic.

The eight built-in function names of section 5.3 --- `vsep`, `hsep`, `dist`,
`bearing`, `abs`, `min`, `max` and `now` --- are lexed as identifiers but are
reserved: declaring a variable, parameter or procedure with one of those names
is an error.

Operators and punctuation:

```
+  -  *  /  %  <  <=  >  >=  ==  !=  =  (  )  ,  .
```

### 3.4 Classification order

The lexer reads a maximal run of `[A-Za-z0-9_]` (a *word*) and classifies the
**whole word** by the first rule that matches it:

1. a keyword,
2. `FL` followed by exactly three digits → `FLEVEL`,
3. digits followed by `KT` → `SPEED`,
4. digits, optionally with one decimal point → `NUMBER`,
5. all lower case or digits or underscore, starting with a lower-case letter →
   `IDENT`,
6. all upper case or digits, starting with an upper-case letter → `CALLSIGN`,
7. otherwise a lexical error.

Order resolves the two real overlaps:

- **`FLEVEL` before `CALLSIGN`.** `FL320` matches both patterns. The flight
  level wins, so an aircraft may not be called `FL` plus exactly three digits.
  `FL32`, `FL3200` and `FLXYZ` are ordinary callsigns, which is the right
  division of labour: the lexer classifies, and whether a name of that shape
  was ever declared is a question for the symbol table. A `FL`-shaped name
  that does not resolve gets a diagnostic saying so.
- **Keywords before `IDENT`.** A variable cannot be named `climb`.

Words that lex as the *wrong* category are left for later phases, which is the
correct division of labour. `clear AI202 climb VOMAN` lexes cleanly and is
caught by the type checker as a `fix` used where a `num` was required;
`clear AI202 climb FLXYZ` also lexes cleanly, as a callsign, and is caught by
name resolution as an undeclared name with a note about how a flight level is
written.

### 3.5 Regular definitions and the DFA

Each token pattern is a regular expression; section 3.3 gives them. The
implementation recognises words by a hand-written classifier rather than a
generated DFA table, because the token set has no overlapping prefixes that
require backtracking: a word is scanned once and then classified. The
equivalent DFA for the interesting case, `FLEVEL` versus `CALLSIGN`, is derived
in `docs/ARCHITECTURE.md` section 2 to show the construction.

### 3.6 Lexical errors

Each is reported at the offending position, and all of them are reported — the
lexer does not stop at the first. If any lexical error occurs, compilation stops
before parsing, because a token stream with holes produces cascades of
meaningless syntax errors.

| Input | Error |
|---|---|
| `Cruise`, `aI202` | mixed case; AeroDSL uses lower case for names and UPPER CASE for callsigns |
| `1000KT` | a speed has at most three digits |
| `ABKT` | a speed is digits followed by `KT` |
| `12.` | a decimal point must be followed by a digit |
| `12.5KT` | a unit suffix cannot follow a decimal point |
| `_hidden` | a name must start with a lower-case letter |
| `@` | unexpected character |
| `!` | only valid as part of `!=` |
| `"abc` | unterminated string |
| a byte above 127 | unexpected non-ASCII character |
| a control character | unexpected control character, with its value |

`FLXYZ` is **not** in this table: it is a well-formed callsign, and is reported
by the semantic analyser as an undeclared name if nothing of that name was
declared.

---

## 4. Grammar

The grammar is LL(1) and is parsed by recursive descent, with precedence
climbing for expressions. Section 4.5 justifies the choice and shows the
grammar is unambiguous.

### 4.1 Program and declarations

```ebnf
program      ::= "scenario" STRING  item*

item         ::= fix_decl | flight_decl | proc_decl | stmt

fix_decl     ::= "fix" CALLSIGN coord coord
coord        ::= [ "-" ] NUMBER

flight_decl  ::= "flight" CALLSIGN [ "type" CALLSIGN ] "enter" CALLSIGN
                 "alt" expr "hdg" expr "spd" expr

proc_decl    ::= "proc" IDENT "(" [ param { "," param } ] ")" block "end"
param        ::= type IDENT
type         ::= "num" | "bool" | "flight" | "fix"
```

A `fix_decl` names a waypoint and gives its latitude and longitude in degrees.

A `flight_decl` names an aircraft, optionally its type, the waypoint it enters
the airspace at, and its initial altitude, heading and speed. The type is not
used by the language or the simulation; it is carried through to the generated
scenario file, which needs one. When it is omitted the output says `A320`.

A `flight_decl` is **also a statement**: the aircraft enters the airspace at the
point in the program where the declaration appears. That is what lets a scenario
introduce traffic part way through, which is how real traffic arrives. Its
initial state may therefore be any expression, including one that reads the
state of aircraft already airborne.

Declarations and statements may be interleaved; a name must be declared before
it is used. Statements outside any procedure form the *main body*, which is the
scenario's timeline.

### 4.2 Statements

```ebnf
block        ::= stmt*

stmt         ::= let_stmt | assign_stmt | clear_stmt | advance_stmt
               | if_stmt  | while_stmt  | assert_stmt
               | call_stmt | print_stmt | return_stmt

let_stmt     ::= "let" IDENT "=" expr
assign_stmt  ::= IDENT "=" expr
clear_stmt   ::= "clear" expr clear_op expr
clear_op     ::= "climb" | "descend" | "heading" | "speed" | "direct"
advance_stmt ::= "advance" expr
if_stmt      ::= "if" expr "then" block [ "else" block ] "end"
while_stmt   ::= "while" expr "do" block "end"
assert_stmt  ::= "assert" expr [ "," STRING ]
call_stmt    ::= "call" IDENT "(" [ expr { "," expr } ] ")"
print_stmt   ::= "print" print_arg { "," print_arg }
print_arg    ::= expr | STRING
return_stmt  ::= "return"
```

### 4.3 Expressions

```ebnf
expr      ::= or_expr
or_expr   ::= and_expr { "or" and_expr }
and_expr  ::= not_expr { "and" not_expr }
not_expr  ::= "not" not_expr | rel_expr
rel_expr  ::= add_expr [ rel_op add_expr ]
rel_op    ::= "<" | "<=" | ">" | ">=" | "==" | "!="
add_expr  ::= mul_expr { ("+" | "-") mul_expr }
mul_expr  ::= unary { ("*" | "/" | "%") unary }
unary     ::= "-" unary | postfix
postfix   ::= primary { "." field }
field     ::= "alt" | "hdg" | "spd" | "lat" | "lon"
primary   ::= NUMBER | FLEVEL | SPEED | "true" | "false"
            | IDENT | CALLSIGN
            | builtin "(" [ expr { "," expr } ] ")"
            | "(" expr ")"
builtin   ::= "vsep" | "hsep" | "dist" | "bearing" | "abs" | "min" | "max" | "now"
```

Precedence, lowest first — each level is a separate production, so the
precedence is structural rather than imposed by a table:

| Level | Operators | Associativity |
|---|---|---|
| 1 | `or` | left |
| 2 | `and` | left |
| 3 | `not` | prefix |
| 4 | `<  <=  >  >=  ==  !=` | **non-associative** |
| 5 | `+  -` | left |
| 6 | `*  /  %` | left |
| 7 | unary `-` | prefix |
| 8 | `.field`, call | postfix |

`rel_expr` allows **at most one** comparison, so `a < b < c` is a syntax error
rather than the nonsense it means in C. This removes a class of bug by
construction.

Built-ins are part of the grammar rather than library names, so `min` cannot be
shadowed and an arity error is reported at the call site.

### 4.4 Ambiguity

Three places could be ambiguous, and each is resolved:

1. **Dangling `else`.** Not possible: `if` is terminated by `end`, so
   `if a then if b then x else y end end` brackets explicitly.
2. **`fix` and `flight` as both keyword and type name.** A `fix_decl` or
   `flight_decl` only appears at item level and a `type` only appears inside a
   parameter list, so position alone distinguishes them. The grammar remains
   LL(1).
3. **Expression statements.** There are none, which is what keeps the grammar
   LL(1) with no lookahead beyond the first token: every statement begins with
   a distinct keyword except an assignment, and an assignment begins with an
   identifier, which no other statement does. The parser therefore decides
   which production to take from the first token alone.

Classical expression ambiguity (`1 - 2 - 3`, `1 + 2 * 3`) does not arise,
because each precedence level is its own left-recursive-free production with an
explicit iteration.

### 4.4.1 Limits the grammar does not express

The repetitions written `{ "," ... }` above are unbounded in the grammar and
bounded in the implementation, which reports anything longer as a syntax error:

| Construct | Limit |
|---|---|
| procedure parameters | 8 |
| arguments in a `call` | 8 |
| items in a `print` | 8 |
| arguments to a built-in | 2, and each built-in's own arity (section 5.3) |

These are implementation limits rather than language design, chosen so that the
AST node for a call is a fixed-size array. Nothing in the language needs more.

### 4.5 Why recursive descent

The grammar was designed to be LL(1) so that it could be parsed by recursive
descent, for three reasons:

- **The parse is readable.** Each production is one function, so the parser can
  be read next to the grammar and the mapping checked by eye. In a viva the
  parser can be traced by hand.
- **Diagnostics are better.** A hand-written parser knows what it was trying to
  parse, so it reports "`climb` requires a flight level, found waypoint
  `VOMAN`" rather than "unexpected token".
- **Error recovery is directed.** On an error, the parser skips to the next
  token that can begin a statement or declaration and resumes, so one mistake
  does not hide the rest.

The language could be parsed by an LALR(1) generator, and the grammar above is
also LALR(1). The trade-off is explicit: a generator would guarantee the tables
are right, at the cost of worse messages and a build dependency. Since the
course covers both top-down and bottom-up parsing, `docs/ARCHITECTURE.md`
section 4 works the expression grammar through an SLR(1) item-set construction
as well, so the bottom-up material is exercised on paper against the same
grammar the parser implements.

---

## 5. Types

Four value types and one non-value:

| Type | Values | Written as |
|---|---|---|
| `num` | a double-precision number | `270`, `12.95`, `FL320`, `280KT` |
| `bool` | `true`, `false` | comparisons, `and`/`or`/`not` |
| `flight` | a reference to a declared aircraft | a callsign declared by `flight` |
| `fix` | a reference to a declared waypoint | a callsign declared by `fix` |
| `void` | — | a procedure has no value |

There are **no implicit conversions**, including none between `num` and `bool`.
`if AI202.alt then` is a type error, not an implicit test against zero.

Units are documented and checked by convention, not by the type system: `num`
means feet for altitudes, degrees for headings and latitude/longitude, knots for
speeds, seconds for time, nautical miles for horizontal distance. `FL320` and
`280KT` are notations for numbers in the expected unit, and exist so the source
reads like the domain. Section 14 lists unit checking as a known limitation.

### 5.1 Operator typing

| Operator | Operands | Result |
|---|---|---|
| `+ - * / %` | `num`, `num` | `num` |
| unary `-` | `num` | `num` |
| `< <= > >=` | `num`, `num` | `bool` |
| `== !=` | two of the same type | `bool` |
| `and or` | `bool`, `bool` | `bool` |
| `not` | `bool` | `bool` |

`%` is the floating-point remainder, and is useful for wrapping headings:
`(h + 180) % 360`.

### 5.2 Flight state fields

`f.field` where `f` is a `flight` reads the aircraft's current simulated state.
The result is `num`.

| Field | Unit | Meaning |
|---|---|---|
| `alt` | feet | current altitude |
| `hdg` | degrees | current true heading, 0 ≤ hdg < 360 |
| `spd` | knots | current ground speed |
| `lat` | degrees | current latitude |
| `lon` | degrees | current longitude |

Fields are read-only. State changes only through `clear`, and takes effect over
time as `advance` runs the simulation. These reads are *state-dependent*, which
constrains the optimiser: see section 10.6.

### 5.3 Built-in functions

| Call | Signature | Unit | Meaning |
|---|---|---|---|
| `vsep(a, b)` | `(flight, flight) -> num` | feet | vertical separation, always ≥ 0 |
| `hsep(a, b)` | `(flight, flight) -> num` | NM | horizontal separation, always ≥ 0 |
| `dist(f, x)` | `(flight, fix) -> num` | NM | distance from aircraft to waypoint |
| `bearing(f, x)` | `(flight, fix) -> num` | degrees | initial bearing to waypoint |
| `abs(x)` | `(num) -> num` | — | absolute value |
| `min(a, b)` | `(num, num) -> num` | — | smaller |
| `max(a, b)` | `(num, num) -> num` | — | larger |
| `now()` | `() -> num` | seconds | elapsed simulation time |

`vsep`, `hsep`, `dist`, `bearing` and `now` read simulation state and are
therefore state-dependent in the same way as field access. `abs`, `min` and
`max` are pure and are constant-folded when their arguments are constant.

---

## 6. Declarations, scope and the symbol table

### 6.1 Scope rules

- `fix`, `flight` and `proc` declarations are **global**, regardless of where
  they appear in the file, and must be unique across the whole program.
- `let` introduces a variable in the **current** scope.
- A procedure body opens a new scope containing its parameters.
- `if`, `while` and procedure bodies each open a block scope, so a `let` inside
  one is not visible after its `end`.
- An inner `let` may shadow an outer variable. Redeclaring a name in the *same*
  scope is an error.
- A name must be declared before the statement that uses it. There is no
  hoisting, and a procedure cannot call a procedure declared later.
- **A procedure body cannot see the main body's variables.** It sees its own
  parameters, its own `let` declarations, and the global declarations
  (aircraft, waypoints, procedures). AeroDSL has no global variables. The
  reason is the runtime: a main-body variable lives in main's activation
  record, and a procedure reading one would be reading another frame. Anything
  a procedure needs is passed to it as a parameter.

### 6.2 What the symbol table stores

One entry per declared name, carrying: kind (variable, parameter, aircraft,
waypoint, procedure), type, the scope depth it was declared at, its declaration
line and column, whether it has been read, whether it has been assigned, and an
index whose meaning depends on the kind — a virtual register for variables and
parameters, a table index for aircraft and waypoints, a procedure index for
procedures.

The table is a stack of scopes. Lookup walks from the innermost scope outward
and returns the first match, which is what gives shadowing. Because the
declaration site is recorded, a redeclaration error can point at both the new
and the original declaration.

### 6.3 Mutability

Variables introduced by `let` and parameters are mutable and may be assigned.
Aircraft, waypoints and procedures are not values that can be reassigned, so
`AI202 = 3` is an error. Assigning to a parameter changes the local copy only:
all parameters are passed **by value**, including `flight` and `fix` references
(copying a reference, not the aircraft).

---

## 7. Static semantics

Semantic analysis walks the AST, annotates every expression with a type, and
reports the following. Errors stop the pipeline before code generation;
warnings do not.

### 7.1 Name and type errors

| # | Rule | Severity |
|---|---|---|
| 1 | Use of an undeclared name | error |
| 2 | Redeclaration in the same scope | error |
| 3 | Operand types do not match the operator (section 5.1) | error |
| 4 | Condition of `if` / `while` / `assert` is not `bool` | error |
| 5 | Clearance operand type is wrong for its kind (section 7.3) | error |
| 6 | First operand of `clear` is not a `flight` | error |
| 7 | `.field` applied to something that is not a `flight` | error |
| 8 | *(an unknown field name is a **syntax** error: the five field names are terminals of the grammar, section 4.3)* | — |
| 9 | Assignment changes the type of a variable | error |
| 10 | Assignment to an aircraft, waypoint or procedure | error |
| 11 | Wrong number of arguments to a procedure or built-in | error |
| 12 | Argument type does not match the parameter type | error |
| 13 | `call` names something that is not a procedure | error |
| 14 | `flight ... enter X` where `X` is not a declared waypoint | error |
| 15 | Division or remainder by a constant zero | error |
| 16 | Procedure declared twice, aircraft declared twice, waypoint declared twice | error |

### 7.2 Domain range checks

Range checking does **not** happen in the semantic analyser. It runs in a
separate static-analysis pass over the optimised three-address code
(section 10.8), for one reason worth stating plainly: by that point constant
propagation has replaced variables by their values, so

```
let h = 450
clear AI202 heading h
```

is caught at compile time exactly as a literal `450` would be. Keeping the check
in one place also stops the same mistake being reported twice.

A value known only at run time is checked by the VM, against the same bounds and
reported at the same source position. The three layers — type checking, static
range checking on the optimised code, and the runtime — therefore never disagree
about what is legal.

The one exception is a waypoint's latitude and longitude. Those are static data
that never reach the three-address code at all, so they are checked in the
semantic analyser, which is the only phase that sees them. The bounds are:

| Quantity | Valid range | Rationale for the bound |
|---|---|---|
| altitude (`climb`, `descend`, `alt`) | 0 to 60000 ft | above any civil cruising level; a prototype bound |
| heading (`heading`, `hdg`) | 0 to 360 degrees | compass; 0 and 360 both mean north |
| speed (`speed`, `spd`) | 60 to 600 kt | below any airliner stall, above any airliner cruise |
| `advance` interval | greater than 0, at most 86400 s | a scenario is at most a day |
| latitude | −90 to 90 | |
| longitude | −180 to 180 | |

These bounds are prototype choices for a simulator, not aircraft performance
data, and no aircraft type model is consulted.

### 7.3 Clearance operand types

| Clearance | Operand type | Unit | Range |
|---|---|---|---|
| `climb` | `num` | feet | 0 to 60000 |
| `descend` | `num` | feet | 0 to 60000 |
| `heading` | `num` | degrees | 0 to 360 |
| `speed` | `num` | knots | 60 to 600 |
| `direct` | `fix` | — | must be declared |

### 7.4 Clearance consistency

These are the checks that need analysis across statements rather than within
one, and they run on the generated three-address code where the control flow is
explicit (section 10.8). All are warnings: each describes a scenario that is
suspicious but legal, and a scenario author may have a reason.

| # | Situation | Reported as |
|---|---|---|
| 1 | `climb` and `descend` issued to the same aircraft with no `advance` between them | contradictory vertical clearances |
| 2 | `heading` and `direct` issued to the same aircraft with no `advance` between them | both assign the lateral path |
| 3 | Two clearances of the same kind to the same aircraft with no `advance` between them | the first has no effect; names both lines |
| 4 | `climb` to a level strictly below the aircraft's current commanded level, or `descend` to one strictly above it | the clearance does not do what its name says |

Check 4 compares strictly, so re-issuing the level an aircraft is already
cleared to — which a loop naturally does — says nothing. That is a clearance
with no effect rather than one pointing the wrong way, and check 3 already
covers the case where it is genuinely redundant.

It is possible at all because the compiler tracks each aircraft's *commanded*
level through the code when it is statically known. Where it is not — the level
is read from the simulation, or the aircraft was reached through a procedure
parameter — the VM makes the same comparison against the level actually in force
and reports it as a runtime warning. The compiler marks each clearance it was
able to decide, so the two never report the same one twice.

### 7.5 Code quality warnings

| # | Situation |
|---|---|
| 1 | A variable is declared and never read |
| 2 | A parameter is never read |
| 3 | A procedure is declared and never called |
| 4 | A waypoint is declared and never used |
| 5 | An aircraft enters the airspace and nothing in the scenario refers to it again |
| 6 | A statement is unreachable (follows `return` in the same block) |
| 7 | The scenario declares no aircraft |
| 8 | The scenario contains no `advance` anywhere, so no simulated time ever passes |
| 9 | A `while` loop that cannot make progress |

Warning 5 is about *references*, not clearances: an aircraft that flies straight
through a scenario without being given a clearance is perfectly legitimate, so
the check is that nothing mentions it at all.

Warning 8 looks for an `advance` anywhere in the program, including inside a
procedure that is never called, so it is conservative: it never fires on a
scenario in which time does pass. It is suppressed entirely when the scenario
declares no aircraft, since warning 7 has already covered that case.

Warning 9 fires when a loop body contains no `advance`, assigns no variable the
condition reads, **and** calls no procedure — a call could do either, so a loop
containing one is left alone. That is a sufficient condition for a loop that
cannot make progress, not a decision procedure; the halting problem forbids the
general case. Loops the check cannot prove anything about are bounded at runtime
instead (section 12.5).

---

## 8. Intermediate representation

### 8.1 Form

Three-address code, as quadruples: an operation, a destination and up to two
sources. Operands are virtual registers, constants, labels, or small indices
(procedure, string, aircraft, waypoint, field, clearance kind, built-in).
Virtual registers are unbounded and numbered per procedure; both named
variables and compiler-generated temporaries are virtual registers, which is
what lets the optimiser treat them uniformly.

| Instruction | Meaning |
|---|---|
| `t = k` | load a constant |
| `t = a` | copy |
| `t = a op b` | arithmetic or comparison |
| `t = op a` | negation or logical not |
| `t = f.field` | read simulated state |
| `t = bi(a, b)` | call a built-in |
| `L:` | label |
| `goto L` | unconditional jump |
| `if a goto L` | branch if true |
| `ifFalse a goto L` | branch if false |
| `if a rel b goto L` | fused compare-and-branch, produced by the peephole pass |
| `enter f` | an aircraft enters the airspace, its state from three preceding `param`s |
| `clear f, kind, a` | issue a clearance |
| `advance a` | run the simulation forward |
| `assert a, msg` | check a condition |
| `print a` / `print msg` | output |
| `param a` | stage an argument |
| `call p, n` | call a procedure |
| `return` | return from a procedure |
| `nop` | removed instruction |

Each instruction keeps the line and column of the construct it came from, so a
diagnostic raised during optimisation or at runtime can point back into the
source.

### 8.2 Translation of expressions

Arithmetic is translated bottom-up: each node emits one instruction into a fresh
temporary. `clear AI202 climb cruise - 2000` becomes:

```
t0 = cruise
t1 = 2000
t2 = t0 - t1
clear AI202, climb, t2
```

which the optimiser then reduces, assuming `cruise` is the constant `FL320`:

```
clear AI202, climb, 30000
```

### 8.3 Translation of booleans and control flow

Boolean expressions in a condition are translated by **short-circuit jumping
code** with **backpatching**, following the scheme in the course text. Each
boolean expression carries a *truelist* and a *falselist* of jump instructions
whose targets are not yet known; the surrounding statement fills them in when
the target label is placed. `and` and `or` therefore generate no boolean value
at all when they appear in a condition — they generate control flow, so
`AI202.alt > 30000 and hsep(AI202, BA117) < 5` does not evaluate `hsep` when the
altitude test fails.

For `while c do S end`:

```
L_begin:                   <- the back edge jumps here
    <code for c>
    if  -> L_body          <- c.truelist, patched to L_body
    goto -> L_after        <- c.falselist, patched to L_after
L_body:
    <code for S>
    goto L_begin
L_after:
```

A boolean used as a *value* rather than a condition — `let converging = hsep(a,b)
< 5` — is instead materialised into a temporary, because it has to be stored.
Both paths are implemented; which one runs depends on the context the expression
appears in.

### 8.4 Procedures

A call evaluates the arguments left to right, emits one `param` per argument,
and then `call p, n`. The callee's parameters are virtual registers 0 to n−1, so
the caller's `param` instructions write into the callee's frame. `return` with
no value ends the procedure; falling off the end of the body does the same.

Recursion is permitted by the implementation and bounded by a call-depth limit
(section 12.5), but the language has no base-case analysis and no scenario needs
it.

---

## 9. Control-flow graph

The three-address code of each procedure is divided into basic blocks: a new
block begins at the first instruction, at any labelled instruction, and at any
instruction following a jump or `return`. Edges connect each block to the blocks
it can transfer control to.

On this graph the compiler computes:

- **Reachability** from the entry block, by forward traversal.
- **Dominators**, by the iterative bit-vector algorithm: `DOM(n)` is the
  intersection of `DOM(p)` over all predecessors `p`, plus `n` itself.
- **Immediate dominators**, as the closest strict dominator of each block.
- **Back edges**: an edge from `n` to `h` where `h` dominates `n`. Each marks
  `h` as a loop header.
- **Natural loops**: the blocks that can reach `n` without passing through `h`,
  plus `h`, which gives the loop body and its nesting depth.
- **Liveness**, by backward dataflow to a fixed point:
  `LIVEout(B) = union of LIVEin(S)` over successors `S`, and
  `LIVEin(B) = USE(B) ∪ (LIVEout(B) − DEF(B))`.

Liveness drives dead-code elimination and register allocation; dominators and
back edges drive loop-invariant code motion.

### 9.1 Static single assignment

The compiler also builds, and prints, the **static single assignment** form of
each function: the form in which every register is assigned exactly once, so
that a use names its definition unambiguously. Where control flow joins and a
register could hold either of two definitions, a phi function names both.

Construction follows Cytron's algorithm:

1. **Dominance frontiers.** The frontier of a block `n` is the set of blocks
   that `n` does not strictly dominate but whose predecessors it does --
   exactly the places where a definition in `n` stops being the only one that
   can reach, and so exactly where a phi function is needed. A loop header is
   in its own dominance frontier, which is why a loop needs a phi for each
   value carried round it.
2. **Phi placement**, iterated to a fixed point, since a phi is itself a
   definition and can force further phis on its own frontier. The form built
   here is **pruned**: a phi is placed only where the value is live on entry,
   using the same liveness the optimiser and the register allocator use. Without
   that filter the form is still correct but carries a phi for every
   block-local temporary that happens to sit inside a loop.
3. **Renaming**, by a walk of the dominator tree carrying a stack of live
   versions per register.

**This form is built for inspection and is not what the rest of the compiler
runs on.** The optimiser works on the ordinary three-address code. Rewriting the
optimiser to work on SSA is listed in section 14 as work this project did not
do, and saying otherwise would be a misrepresentation.

---

## 10. Optimisation

Passes run in the order below, and the whole sequence repeats until no pass
changes anything, because one pass creates opportunities for another: constant
propagation produces constant operands, folding turns those into constants, and
the resulting unused instructions become dead.

Two bounds keep a bug from turning into a hang rather than a wrong answer: the
outer sequence runs at most 24 times, and the dataflow iteration inside constant
propagation at most 1000. Neither has been observed to bind — every pass is
written to report a change only when it actually changed something, which is
what makes the fixed point reachable — but a compiler that loops forever is
worse than one that stops early.

### 10.1 Constant propagation

A forward dataflow analysis over the control-flow graph, and the first pass to
run, because it is what gives folding something to do. Each virtual register is
assigned a lattice value: *undefined*, *a specific constant*, or *not constant*.
At a join, two different constants give *not constant*. The result replaces
constant-valued register operands with their constants.

A *constant* here includes an aircraft or waypoint reference, not only a number.
That matters for section 10.8: it is what lets the analysis see which aircraft a
clearance issued through a procedure parameter actually names.

### 10.2 Constant folding

An instruction whose operands are all constants is replaced by a constant load.
Comparisons fold to `true` or `false`. The pure built-ins `abs`, `min` and `max`
fold; the ones that read the simulation do not.

A division or remainder by a constant zero is **not** folded. It can reach this
pass, because propagation may have just made the divisor constant — the semantic
analyser only sees a literal zero. Leaving it alone means the analysis pass of
section 10.8 finds it afterwards and reports it as an error, which is how
`let z = 0` followed by `1 / z` is caught at compile time rather than at run
time. A fold whose result would not be finite is declined for the same reason:
the runtime rejects it with a better message.

### 10.3 DAG construction, common-subexpression elimination and copy propagation

Within each basic block, a directed acyclic graph is built over the
instructions: every distinct computation becomes a node, identified by its
operator and the identity of its operand nodes. An instruction whose
computation already has a node is an available common subexpression and is
replaced by a copy from the register already holding it. Copies are then
propagated forward, so the copy itself usually becomes dead.

The DAG is also where **algebraic simplification** happens: `x + 0`, `x - 0`,
`x * 1` and `x / 1` become copies, and `x * 2` becomes `x + x`, which is strength
reduction. The commuted forms `0 + x`, `1 * x` and `2 * x` are recognised too.

`x * 0 -> 0` and `x - x -> 0` are deliberately **not** done. They are the two
identities that fail for a non-finite `x`, and rewriting them would bake in an
assumption about values at the point of rewriting rather than checking it at the
point of execution. The identities that are done are exact for finite values,
and finiteness is guaranteed from the other end: a constant zero divisor is a
semantic error, a runtime zero divisor is a runtime error, and the VM rejects
any arithmetic result that is not finite (section 12.5). So no non-finite value
can ever exist for them to be wrong about.

### 10.4 Dead-code elimination

After liveness, an instruction whose destination is not live after it, and which
has no side effect, is removed. The side-effecting instructions — `clear`,
`advance`, `assert`, `print`, `call`, `param`, control transfers — are never
removed by this pass, whatever their destination.

### 10.5 Loop-invariant code motion

For each natural loop, an instruction is invariant if all of the following hold,
iterated to a fixed point:

1. it has no side effect;
2. its destination is assigned nowhere else in the loop;
3. every operand is a constant, or is defined outside the loop, or is defined by
   an instruction already shown invariant;
4. it is not state-dependent, or the loop contains no barrier (section 10.6);
5. it cannot fail at run time, which excludes division and remainder, since a
   loop body that never executes must not be able to raise an error the original
   program could not;
6. **its destination is dead on every exit edge of the loop.** Without this the
   pass is wrong, and the counter-example is short:

   ```
   let x = 1
   while c do  x = 5   advance 1  end
   print x
   ```

   `x = 5` satisfies every other test, and hoisting it would print 5 even when
   the loop body never ran. Requiring the value to be dead on exit is the strict
   form of the usual condition that the definition dominate every loop exit.

Invariant instructions move to just before the loop header's label. That
placement is only correct if the loop is entered by falling into it, so the pass
first checks that the header has exactly one predecessor outside the loop and
that nothing jumps to it from outside; a loop that fails the check is left
alone.

### 10.6 The state barrier

`clear`, `advance`, `enter` and `call` can change the simulated world. Reads of that
world — field access, and the state-dependent built-ins listed in section 5.3 —
must therefore never be moved across them, eliminated as a common subexpression
across them, or hoisted out of a loop containing one.

The optimiser enforces this by treating those four instructions as *barriers*:
a barrier kills every DAG node for a state-dependent computation, and an
instruction is not loop-invariant if it is state-dependent and the loop contains
a barrier. This is the one domain-specific rule in the optimiser, and without it
every optimisation in this section would be unsound: a loop that waits for an
aircraft to climb would have its `alt` read hoisted out and would never
terminate.

### 10.7 Jump threading and unreachable-code removal

A jump to a label whose block contains only another jump is retargeted to the
final destination. A conditional branch whose condition is now a constant
becomes an unconditional jump or is removed. Blocks unreachable from the entry
block are deleted, and their deletion is reported as a warning naming the source
line, which is how section 7.5's unreachable-code warning is produced for code
made unreachable by optimisation rather than by `return`.

### 10.8 Domain-specific static analysis

Two checks run over the optimised code rather than over the AST, each for its
own reason.

**Range checking (section 7.2).** Constant propagation has by now replaced
variables by their values, so an operand written as a variable is as checkable
as a literal. This is the single clearest payoff from having an optimiser in a
project that does not otherwise need fast code: the optimisation makes a
diagnostic possible that was not possible before.

**Clearance consistency (section 7.4).** This is where the control flow is
explicit. Within a basic block the clearance sequence for each aircraft is
straight-line, so "with no `advance` between them" is a scan of the block, and
the commanded level can be tracked through it to decide whether a `climb`
actually climbs.

Clearances in different blocks are not compared, which makes the check sound but
incomplete: it never reports a contradiction that is not one, and it misses
contradictions that span a branch. That trade-off is deliberate, because these
are warnings about scenarios that are legal, and a false warning about something
the author wrote on purpose is worse than a missed one.

A clearance issued through a procedure parameter names no particular aircraft
unless propagation has resolved it, and where it has not, the analysis discards
what it knew rather than guessing.

---

## 11. Code generation

### 11.1 Target

The target is **AeroVM**, a register machine defined in section 12. It is the
right target for this project because the scenario has to *run* to produce the
timeline the scenario file needs, and because a VM can be instrumented — every
clearance, every separation measurement and every assertion is observable, which
is what the verification feature needs. Generating C or native code would make
the compiler's back end more conventional but would make the simulator and the
instrumentation someone else's problem.

### 11.2 Next-use information

For each basic block, a backward scan records for every virtual register, at
every instruction, the index of its next use within the block, or *none* if its
last use in the block has passed. A register whose next use is *none* and which
is not live out of the block is dead, and the register holding it can be reused
immediately.

### 11.3 Register allocation

AeroVM has 16 registers per frame plus unbounded frame slots. Allocation is the
textbook on-demand scheme, per basic block:

- An operand already in a register uses it.
- Otherwise a register is obtained: an empty one; or one holding a value that is
  dead at this point; or, failing both, the register whose value's next use is
  **furthest away**, whose value is spilled with a `store` to its frame slot.
- At a block boundary every register that is **dirty** and holds a value that is
  live out is written back, because the allocator is local and makes no
  assumption about which register a value is in on entry to a block. A register
  is dirty only if it has been written since it was loaded, so a value that was
  read and never changed costs no store.

Variables that are live across blocks therefore have a home frame slot and are
loaded and stored around their uses; temporaries that live and die inside one
block usually stay in registers. The next-use heuristic is what makes the
spilling choice non-arbitrary, and it is Belady's rule restricted to a block.

### 11.4 Peephole optimisation

Compare-and-branch fusion happens first, on the three-address code rather than
on the bytecode, because the liveness information that proves the comparison's
result is not needed elsewhere is computed there. `t = a < b` immediately
consumed by a branch on `t`, with `t` dead afterwards, becomes the single
instruction `if a < b goto L`.

The remaining patterns are applied to the emitted bytecode:

- A conditional branch that only skips an unconditional jump has its condition
  inverted and takes the jump's target, which removes the jump. This is the
  shape that jumping code for conditions naturally produces, so it fires on
  every `if` and every `while`.
- `move` to the register a value is already in is removed.
- A `store` immediately followed by a `load` of the same slot into the same
  register is removed.
- A jump to the instruction that follows it is removed.
- A jump to a jump is retargeted.

---

## 12. AeroVM: execution, simulation and verification

### 12.1 Machine

Registers are double-precision. A `flight` or `fix` value is its table index
held in a register; the type checker has already proved which registers hold
which, so values are not tagged at runtime.

An activation record holds the return address, the caller's frame base, the 16
registers and the frame slots. A call allocates a frame, copies the staged
arguments into the callee's first slots, and jumps; a return restores the
caller's frame. This is the runtime organisation the course covers, in its
simplest form: no displays, no static links, no nesting of procedures.

### 12.2 Flight model

Each aircraft holds a position, an altitude, a heading, a speed, the targets
most recently commanded, and the waypoint it is tracking. `advance n` integrates
the state forward in one-second steps:

| Quantity | Rate |
|---|---|
| climb | 1800 ft/min |
| descent | 2000 ft/min |
| turn | 3 degrees/second (standard rate) |
| acceleration | 1 kt/second |

A `direct` clearance makes the aircraft steer toward the waypoint, recomputing
its heading each step; the waypoint is considered reached within 0.5 NM, and
tracking then stops. Position integrates on a flat-earth approximation — one
degree of latitude is 60 NM, one degree of longitude is 60 NM times the cosine
of the latitude.

This is a *kinematic* model: the aircraft does exactly what it is told at fixed
rates, with no mass, no wind, no performance envelope and no pilot response
delay. It is adequate for deciding when aircraft come close to one another on
the scale of a training scenario, and inadequate for anything else.

### 12.3 Separation monitor

After each one-second step, and again whenever an aircraft enters the airspace
— since it can enter already in conflict with one that is airborne — the monitor
computes, for every pair of active aircraft, horizontal separation in nautical miles and vertical separation in
feet. A pair is in **loss of separation** when horizontal is below 5 NM *and*
vertical is below 1000 ft. Entering that state records an event with the time,
the pair and both values. The run also records the closest approach seen between
any pair.

5 NM and 1000 ft resemble separation standards used in parts of en-route
controlled airspace, and are used here as fixed prototype constants. Real
minima vary by airspace, altitude band, equipment and procedure; this monitor
models none of that.

### 12.4 Verification

`assert c, "message"` evaluates `c` at that point in the timeline. A failure is
reported as a `verify` diagnostic at the assertion's line and column with its
message, and the run continues so that every assertion is checked. The exit
status is non-zero if any assertion failed.

This is what lets a scenario state its own intent. A scenario built to train a
controller on a converging pair can assert that the pair does in fact converge:

```
assert hsep(AI202, BA117) < 5, "the conflict must actually occur"
```

and the compiler will tell the author if an edit elsewhere has quietly removed
the conflict the scenario exists to create.

### 12.5 Runtime limits and errors

Execution is bounded: at most 20 million bytecode instructions, at most 24 hours
of simulated time, and at most 256 nested calls. Exceeding a limit is a runtime
error naming the construct responsible, which is how the unbounded loops that
section 7.5's warning cannot prove anything about are contained.

Runtime errors are reported at the source position of the instruction and stop
execution:

- a clearance operand outside its range (section 7.3);
- an initial altitude, heading or speed outside its range;
- a non-positive `advance`, or one longer than the limit;
- a clearance to an aircraft that has not entered the airspace yet;
- division or remainder by zero;
- **any arithmetic result that is not finite.**

The last of those is not only hygiene. It is the guarantee that makes the
optimiser's algebraic identities exact (section 10.3): because no non-finite
value can survive, there is none for those identities to be wrong about.

An aircraft never leaves the airspace once it has entered: there is no construct
for removing one, and the simulation keeps flying it.

The VM additionally carries a handful of defensive checks — an aircraft index
out of range, a waypoint that does not exist, an aircraft entering twice, a call
to a procedure that does not exist. None of them should be reachable from a
program that passed the type checker; they exist so that a bug in an earlier
phase produces a located diagnostic instead of reading past the end of a table.

The VM also emits runtime *warnings*, which do not stop execution: a `climb`
that does not climb or a `descend` that does not descend, where the level was
not known at compile time (section 7.4).

---

## 13. Scenario-file output

After a successful run, the recorded timeline is written as a flat, timestamped
command list:

```
# Scenario generated by the AeroDSL compiler
# Source scenario: "Converging pair at FL320"
# ...
00:00:00.00>DEFWPT VOMAN,12.950000,77.670000
00:00:00.00>DEFWPT AKTIV,13.200000,77.900000

00:00:00.00>CRE AI202,A320,12.950000,77.670000,45,FL320,280
00:00:00.00>CRE BA117,B777,13.200000,77.900000,225,FL320,300
00:01:30.00>ALT AI202,FL320
00:03:00.00>HDG AI202,270
00:04:00.00>ADDWPT AI202,AKTIV
```

Waypoints are defined first, so the commands that name them resolve. A `climb`
or `descend` becomes `ALT`, a `heading` becomes `HDG`, a `speed` becomes `SPD`,
and a `direct` becomes `ADDWPT`. An altitude is written as a flight level when
it is at least 1000 ft and a round hundred, and in plain feet otherwise.

Every clearance that the run actually issued appears at the time it was issued.
Conditionals and loops are therefore resolved by the simulation: the output is
one concrete scenario, not a program. For scenario generation that is the
intent — the simulator wants a scenario — but it means the output of a scenario
whose behaviour depends on the simulation is only as good as this prototype's
flight model, and it is explicitly not a translation of the AeroDSL program's
full semantics.

The command names and the `HH:MM:SS.SS>` line format are BlueSky's. The files
generated for examples 3 and 5 load and run in **BlueSky 1.1.1** with no error
messages (`bluesky/README.md`); other versions have not been tried. Times in the
file come from this prototype's flight model, so a wait can resolve at a
different moment than BlueSky's own aircraft model would: in example 5 the turn
comes about 1,060 ft below FL300.

---

## 14. Known limitations

Stated plainly, because the project is graded on honesty about its own
boundaries as much as on what it does.

**Language**
1. Procedures do not return values; expression-level abstraction is limited to
   the built-ins.
2. No arrays, records or collections, so a scenario cannot iterate over "all
   aircraft". Each aircraft is named individually.
3. No `for` loop; counted repetition is written with `while`.
4. Units are a documentation convention, not types. `clear AI202 climb 280`
   means 280 **feet** and is accepted. A unit-aware type system would catch it
   and is the single most valuable extension to this language.
5. No modules or includes; a scenario is one file.
6. No string operations; strings appear only in `print` and `assert`.
7. No global variables. A procedure sees only its parameters and the global
   declarations, so everything it needs is passed in (section 6.1).
8. No `for` loop over aircraft, so a scenario that does the same thing to
   twelve aircraft calls a procedure twelve times.

**Compiler**
9. Register allocation is local to a basic block, so every value live across a
   block boundary is written back and reloaded. A global allocator over an
   interference graph would keep more of them in registers.
10. Common-subexpression elimination is local to a basic block; there is no
    global available-expressions analysis.
11. The clearance consistency checks do not cross basic blocks (section 10.8),
    so they are sound but incomplete.
12. Static single assignment is constructed and printed, but **no pass operates
    on it**. The optimiser works on the ordinary three-address code. Converting
    the optimiser to SSA is the obvious next piece of work and was not done.
13. No vectorisation, no parallelisation, no instruction scheduling and no
    software pipelining. Of the syllabus's Module 7, this project covers
    domain-specific languages and SSA; it does not cover the parallelism
    material, and the state-barrier analysis of section 10.6 is a dependence
    argument of the same family but is not automatic parallelisation.
14. Error recovery resynchronises at the start of the next line that begins a
    statement, so a mistake in the middle of a line can still consume the rest
    of that line.
15. A hoisted arithmetic operation that overflowed would report an error even
    if the loop body never executed. Division, the one operation that can fail
    on ordinary values, is excluded from hoisting for exactly this reason, and
    the value ranges AeroDSL works with make overflow unreachable in practice.

**Domain**
16. The flight model is kinematic and crude (section 12.2): fixed rates, no
    mass, no wind, no performance envelope, no pilot response delay.
17. The separation standard is two fixed constants (section 12.3).
18. There is no aircraft performance data, so there is no check that a commanded
    level or speed is within a given aircraft's capability. The type in a
    `flight` declaration is carried to the generated scenario file and is
    otherwise unused.
19. The waypoint database is whatever the scenario declares. No real navigation
    data is used, and the coordinates in the examples are approximations chosen
    to make the geometry work.
20. Position uses a flat-earth approximation, which is wrong near the poles and
    across the date line.
21. The generated scenario file is a trace of one run, not a translation of the
    program's semantics (section 13).
22. Nothing here has been validated by anyone with air traffic control
    expertise.

---

## 15. Example programs

The files in `examples/` are referenced here by name; each one is runnable.

| File | What it shows |
|---|---|
| `01_minimal.aero` | the smallest complete scenario |
| `02_converging.aero` | two aircraft, an intended loss of separation, verified by `assert` |
| `03_procedures.aero` | a reusable arrival procedure applied to three aircraft |
| `04_conditional.aero` | a clearance that depends on simulated state |
| `05_loop.aero` | `while` waiting for an aircraft to reach a level |
| `06_optimise.aero` | every optimisation pass firing; its two warnings are deliberate |
| `07_lexical_errors.aero` | every lexical error in section 3.6 |
| `08_syntax_errors.aero` | error recovery reporting several syntax errors at once |
| `09_type_errors.aero` | the type and name errors of section 7.1 |
| `10_domain_errors.aero` | range violations and contradictory clearances |
| `11_boundaries.aero` | values exactly on every range boundary, all valid |
| `12_separation.aero` | a resolution that keeps a pair separated, asserted |

### 15.1 A complete scenario

This is `examples/02_converging.aero`, and it is the example that shows what the
language is for. It is a controller-training exercise: two aircraft at the same
level converging head on, which the trainee has to notice and resolve.

```
scenario "Converging pair at FL320"

fix VOMAN 12.95 77.67
fix AKTIV 13.20 77.90

let level = FL320

flight AI202 type A320 enter VOMAN alt level hdg 045 spd 280KT
flight BA117 type B777 enter AKTIV alt level hdg 225 spd 300KT

# Track the closest the pair ever comes, in the language itself.
let closest = 500
let elapsed = 0
while elapsed < 600 do
    advance 10
    closest = min(closest, hsep(AI202, BA117))
    elapsed = elapsed + 10
end

print "closest approach: ", closest, " NM"

# The scenario's whole purpose, stated as a checkable claim.
assert closest < 5, "the pair must come within 5 NM, or there is no exercise"
```

Three things in it are the point of the language:

- `let level = FL320` is written once and used twice. In a flat command list it
  would be two literals, and an edit would have to find both.
- The loop measures the closest approach *in the scenario itself*, using `hsep`
  and `min`, rather than leaving it to be read off a replay.
- The `assert` states why the scenario exists. If someone later edits the
  geometry and the aircraft no longer converge, the compiler says so.

Its companion, `examples/12_separation.aero`, is the same geometry with a
resolution applied and the opposite assertion: that separation is kept. Between
them they show both halves of the claim the language makes.

### 15.2 Invalid programs and what is reported

These are the compiler's actual messages, copied from a run rather than
paraphrased.

```
clear AI202 heading 450
  semantic error: the operand of 'heading' must be between 0 and 360 deg,
  found 450

let h = 450
clear AI202 heading h
  semantic error: the operand of 'heading' must be between 0 and 360 deg,
  found 450          ← the same message, because propagation resolved h

clear AI202 climb VOMAN
  semantic error: 'climb' takes a number in ft, found fix

clear BA117 climb FL320
clear BA117 descend FL280
  semantic warning: BA117 was cleared 'climb' at line 12 and 'descend' here
  with no 'advance' between them: the two contradict each other

if AI202.alt then
  semantic error: the condition of an if must be bool, found num; AeroDSL has
  no implicit test against zero, so write a comparison such as '> 0'

let x = 1 < 2 < 3
  syntax error: comparisons do not chain in AeroDSL; write 'a < b and b < c'
  instead of 'a < b < c'

clear AI202 climb FLXYZ
  semantic error: 'FLXYZ' is not declared (a flight level is written FL and
  exactly three digits, as in FL320)
```

---

## 16. Grammar summary

The complete grammar, collected for reference.

```ebnf
program      ::= "scenario" STRING item*
item         ::= fix_decl | flight_decl | proc_decl | stmt

fix_decl     ::= "fix" CALLSIGN coord coord
coord        ::= [ "-" ] NUMBER
flight_decl  ::= "flight" CALLSIGN [ "type" CALLSIGN ] "enter" CALLSIGN
                 "alt" expr "hdg" expr "spd" expr
proc_decl    ::= "proc" IDENT "(" [ param { "," param } ] ")" block "end"
param        ::= type IDENT
type         ::= "num" | "bool" | "flight" | "fix"

block        ::= stmt*
stmt         ::= "let" IDENT "=" expr
               | IDENT "=" expr
               | "clear" expr clear_op expr
               | "advance" expr
               | "if" expr "then" block [ "else" block ] "end"
               | "while" expr "do" block "end"
               | "assert" expr [ "," STRING ]
               | "call" IDENT "(" [ expr { "," expr } ] ")"
               | "print" print_arg { "," print_arg }
               | "return"
clear_op     ::= "climb" | "descend" | "heading" | "speed" | "direct"
print_arg    ::= expr | STRING

expr         ::= or_expr
or_expr      ::= and_expr { "or" and_expr }
and_expr     ::= not_expr { "and" not_expr }
not_expr     ::= "not" not_expr | rel_expr
rel_expr     ::= add_expr [ rel_op add_expr ]
rel_op       ::= "<" | "<=" | ">" | ">=" | "==" | "!="
add_expr     ::= mul_expr { ("+" | "-") mul_expr }
mul_expr     ::= unary { ("*" | "/" | "%") unary }
unary        ::= "-" unary | postfix
postfix      ::= primary { "." field }
field        ::= "alt" | "hdg" | "spd" | "lat" | "lon"
primary      ::= NUMBER | FLEVEL | SPEED | "true" | "false"
               | IDENT | CALLSIGN
               | builtin "(" [ expr { "," expr } ] ")"
               | "(" expr ")"
builtin      ::= "vsep" | "hsep" | "dist" | "bearing"
               | "abs" | "min" | "max" | "now"
```
