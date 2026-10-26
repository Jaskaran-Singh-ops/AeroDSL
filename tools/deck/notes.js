// Presenter notes, one entry per slide, in order.
// Each: what to say · the concept · why it matters · analogy · likely question
// and answer · transition.
module.exports = [
// 1 Title
`SAY: "This is AeroDSL — a small programming language I designed, and a compiler for it, written from scratch in C. The domain is air traffic control training scenarios: the scripted traffic situations instructors use to train controllers on a simulator."

CONCEPT: framing. A new language plus a working compiler, for a real authoring problem.

WHY IT MATTERS: the first fifteen seconds decide whether the examiner thinks this is a toy or a real compiler. Say "compiler written from scratch in C, no parser generator" early.

SAY NEXT: "Before anything else — this is an educational prototype for simulated scenarios. It is not connected to anything real, and I will be clear about that throughout."

THE PICTURE: those two lines are the actual plan view from my demo — two aircraft converging, and the circle is where my compiler's simulator predicts they lose separation.

LIKELY Q: "Did you write the whole compiler yourself?"
A: "Yes — about 8,400 lines of C11, no dependencies. No Lex, no Yacc, no LLVM. The lexer and the recursive-descent parser are hand-written, which I did on purpose so I can trace any part of them for you."

TRANSITION: "Let me start with the problem, because the language only makes sense once you have seen what it replaces."`,

// 2 Problem
`SAY: "Who actually writes these? Instructors training controllers, and researchers running experiments on ATC simulators. They are domain experts — not necessarily programmers."

"What they build is a scenario: a set of aircraft with starting positions, and a timeline of clearances — climb to this level, turn to that heading."

"And today they write it as a flat text file. One command per line, each line stamped with the time it should fire. That file at the bottom is the real documented format from BlueSky, which is an open-source ATC simulator from TU Delft."

CONCEPT: establishing the user, the artefact and the current workflow before proposing anything.

ANALOGY: "It is a bit like writing a film by listing every camera instruction with a timecode, instead of writing a script."

WHY IT MATTERS: a professor who has never seen this project needs the workflow in their head before the language will mean anything.

LIKELY Q: "Is BlueSky's format really like that?"
A: "Yes — that example is quoted from their wiki page on scenario files. The timestamp-arrow line format, the hash comment and the CRE argument order are all theirs, not mine."

TRANSITION: "The format is fine for the simulator. The trouble starts when a person has to maintain it."`,

// 3 What goes wrong
`SAY: "Four things go wrong, and they all come from the same cause — the file has no structure above the level of a single command."

"No names: a level used ten times is typed ten times. No conditions: if you want a turn when the aircraft reaches a level, you have to work out when that happens and write an absolute timestamp. Nothing is checked. And the reason the scenario exists at all is written nowhere."

"The box at the bottom is the one that actually bites. Someone edits the starting altitude from FL100 to FL200. The timestamp for the turn was computed from FL100. Now the turn fires at the wrong moment, nothing reports it, and you find out by watching the replay."

CONCEPT: the failure mode is silent, which is what makes it worth a compiler.

WHY IT MATTERS: this is the single most important slide for justifying the project. If the examiner accepts this, everything else follows.

LIKELY Q: "Couldn't you just write a Python script to generate the file?"
A: "You could, and for the first two problems that would be enough — Python has variables and conditionals. What it does not give you is checking against the domain: in a library, clear(f, 'heading', 450) is an ordinary function call. In my language heading is a construct with a declared range, so 450 is rejected at the line and column where it was written, before anything runs. And a library cannot analyse across statements to notice that two clearances contradict each other."

TRANSITION: "Before I show you my answer, let me be honest about whether this problem is real and whether anyone has already solved it."`,

// 4 Evidence
`SAY: "I want to separate three different kinds of claim, because it would be easy to overstate this."

"Verified: the format genuinely has no variables, conditionals or validation. I read that off the documentation and off my own output."

"From the literature: Jafer and colleagues in 2017 say ATC scenario development lacks standardised practice and scenarios are hard to reuse — and they propose an ATC Scenario Language for exactly that. A 2025 paper on AirTrafficGen calls manual scenario design, in their words, a demanding and time-consuming bottleneck."

"And an assumption: that a compiler-checked language is the right answer for a working instructor. I have not interviewed anyone. That is a design hypothesis and I am labelling it as one."

"The bottom box is the one I most want to say out loud. AeroDSL is not the first language proposed for this. ASDL and the ATC Scenario Language came first. What I am claiming is narrower: a complete, small, readable compiler for the idea, with domain-aware static checking that those papers do not describe."

CONCEPT: intellectual honesty as a feature, not an apology.

WHY IT MATTERS: examiners probe novelty claims. Conceding priority before being asked turns a weakness into evidence of judgement.

LIKELY Q: "So what is actually new here?"
A: "As an idea, nothing — and I say so. As a project, the contribution is the compiler itself: the static checking that uses the domain, and specifically the barrier rule I will show you on slide eleven, which is what makes optimising a language with simulated state correct at all."

TRANSITION: "So — here is the same scenario, written as a program."`,

// 5 Side by side
`SAY: "On the left, what the author writes. On the right, what my compiler produces — and the right-hand side is the file the simulator actually consumes. AeroDSL does not replace the flat format. It generates it."

"Two things to notice. FL320 appears once, as cruise. And look at the clearance: the author wrote cruise minus two thousand feet. The compiler folded that to thirty thousand, and the output says FL300. Nobody typed FL300."

CONCEPT: source language versus target. The ordinary compiler relationship.

ANALOGY: "Exactly like writing C instead of assembly. The assembly still exists — you just stop writing it by hand."

WHY IT MATTERS: it reframes the project from "a new thing that competes with the simulator" to "a compiler, with a well-defined target".

LIKELY Q: "Does that generated file actually load in BlueSky?"
A: "I have not tested that, and I am careful not to claim it. What I can show you is that the line format and the CRE argument order match their published documentation exactly. Loading it into BlueSky is the first thing on my future-work list."

TRANSITION: "Let me show you the language properly."`,

// 6 Language
`SAY: "This is a complete, runnable example from the repository — a controller-training exercise with two aircraft converging at the same level."

"Four things I would point at. Declarations: waypoints and aircraft are first-class, and an aircraft enters the airspace where its declaration appears, so traffic can arrive part-way through a scenario."

"Abstraction: FL320 written once, used by both aircraft."

"Simulated state: hsep is the horizontal separation between two aircraft right now. advance runs the simulation forward. So that loop is measuring the closest the pair ever comes — in the language itself, not by reading it off a replay."

"And the last line. assert says why this scenario exists: the pair must come within five miles, or there is no exercise. The compiler simulates the scenario and checks it."

CONCEPT: domain constructs as syntax, and executable intent.

WHY IT MATTERS: this is the slide where the language stops looking like a config format.

LIKELY Q: "Why is assert interesting? Every language has assertions."
A: "Because of what it is asserting about. It is checked against a simulation the compiler ran itself, so the claim being verified is about the traffic situation, not about a variable. If someone edits the geometry and the aircraft stop converging, the compile fails."

TRANSITION: "So how does that become a scenario file? Here is the pipeline."`,

// 7 Pipeline
`SAY: "Ten stages, all implemented, and every one of them can be printed from the command line — the flag is written under each box."

"Front end across the top: lexer, parser, semantic analysis, then three-address code and the control-flow graph. Back end along the bottom: the optimiser, the domain checks, code generation, the virtual machine, and finally the scenario file."

"The amber box is the one that is specific to this project — that is where operand ranges and clearance consistency are checked."

"And the bar at the bottom: dominators, liveness, SSA, register allocation with spilling. All real and all tested. I am not going to walk through them unless you want me to, because they are not needed to understand the project."

CONCEPT: the standard phase structure, with the domain-specific pass identified.

ANALOGY for the stages: "Lexer recognises the words. Parser checks the grammar. Semantic analysis asks whether the sentence makes sense. The rest is translation."

WHY IT MATTERS: it shows completeness without drowning the audience.

LIKELY Q: "Which phases are only partially done?"
A: "Register allocation and common-subexpression elimination are both local to a basic block, not global. The SSA form is built and printed but no pass runs on it. Those three are in my limitations slide and in the project's STATUS document."

TRANSITION: "Let me take one tiny program and follow it all the way through."`,

// 8 Walkthrough A
`SAY: "Eight lines of source. Everything you see on this slide and the next is real output from the compiler — I ran it to make these slides."

"Tokens first. Notice FL240 is a single token carrying the value twenty-four thousand feet, and 280KT is a single token worth two hundred and eighty knots. The units are in the lexer."

"Then the AST — the tree. And every node already has a type on it: num, flight. That is the semantic analyser's output, annotated onto the tree the parser built."

CONCEPT: lexical then syntactic then semantic, each adding information.

ANALOGY: "Tokens are the words. The tree is the sentence diagram. The types are knowing that 'climb' needs a height and not a place."

WHY IT MATTERS: this is the heart of a compiler review. Use real output and the examiner can trust the rest.

LIKELY Q: "Why is FL320 one token and not three?"
A: "Because in this domain it is one thing — a flight level. The lexer matches FL followed by exactly three digits and attaches the numeric value. It is tried before the callsign rule, which is why an aircraft cannot be named FL plus three digits. I derived that little DFA in the architecture document."

TRANSITION: "Same program, now in the middle of the compiler."`,

// 9 Walkthrough B
`SAY: "Three-address code: at most one operation per instruction, which is the form the optimiser works on. You can see the let became an assignment, the subtraction became its own instruction into a temporary, and then the clearance uses that temporary."

"Optimised: those three instructions collapsed into one. Constant propagation knew what cruise held, folding did the subtraction, and the copy that was left became dead."

"Then bytecode for my virtual machine — sixteen registers, spill slots in the frame."

"And at the bottom, running it. Two events, and then the scenario file: a CRE line and an ALT line, at the times the simulation produced them."

CONCEPT: lowering, optimisation, code generation, execution.

ANALOGY: "Three-address code is the compiler's own simplified language — small enough that it can reason about it."

WHY IT MATTERS: it closes the loop from source to the artefact the user actually wanted.

LIKELY Q: "Why generate bytecode and a VM rather than C?"
A: "Because the scenario has to run to produce its own timeline. A conditional that depends on where the aircraft is can only be resolved by simulating. And a VM can be instrumented — every clearance and every separation measurement is observable from inside the instruction loop, which is what the assertion checking needs."

TRANSITION: "Let me say a bit more about the optimiser, because it does something I did not expect when I started."`,

// 10 Optimisation
`SAY: "Seven passes, repeated until nothing changes, because each one creates work for the others."

"The small example at the top is the one from the walkthrough: propagate, fold, drop the dead copy. Three instructions to one."

"The box on the left is every counter from a file I wrote specifically so that every pass fires. Sixty-one three-address instructions in, thirty-seven out."

"But the right-hand box is the thing I would actually like to be asked about. Range checking runs after constant propagation. So if you write let h = 450 and then clear with heading h, that is a compile-time error — because by that point the compiler knows what h holds. An optimisation is normally justified by the code it produces. Here one is justified by the error message it makes possible."

CONCEPT: optimisation as an enabler of analysis, not just of speed.

WHY IT MATTERS: it is a genuine insight and it is specific to this project.

LIKELY Q: "Does the optimiser change what the program does?"
A: "It must not, and I test that rather than assert it. The suite runs eighteen scenarios twice — once with the optimiser, once without — and requires the diagnostics, the whole event trace and the final state of every aircraft to be identical."

TRANSITION: "Which brings me to the one rule that makes all of that safe."`,

// 11 Barrier
`SAY: "Here is a perfectly ordinary loop: wait until the aircraft passes FL300."

"A textbook optimiser would break it. That altitude read looks loop-invariant by every usual test — the operands are the same, nothing in the loop assigns to them. So hoist it out, evaluate the condition once, and the loop never terminates."

"The fix is the rule on the right. clear, advance, enter and call can change the simulated world, so they are barriers. A read of that world is never moved across one — not by common-subexpression elimination, not by code motion. That is six lines of C and it is what makes every other optimisation in the project correct."

"And the bottom box: it is checked by that differential test, not just asserted."

CONCEPT: a domain-specific dependence rule constraining classical optimisation.

ANALOGY: "It is the same reason a C compiler cannot cache a read through a volatile pointer. Something outside the code can change it."

WHY IT MATTERS: this is the intellectual high point of the project. If you only get one deep question, you want it to be this one.

LIKELY Q: "How do you know you caught every case?"
A: "I do not know it by proof — I know it by construction and by test. The rule is applied in one place, ir_is_barrier, and every pass that moves or reuses an instruction consults it. The differential test is what would catch a pass that forgot."

TRANSITION: "Let me show you what the compiler catches."`,

// 12 Errors
`SAY: "Three layers, and every message on this slide is copied from an actual run."

"Lexical and syntax first — and the parser reports all of them, not just the first, because it resynchronises at the start of the next statement line."

"Then semantic: types, arity, scope. One decision I will defend — there is no implicit test against zero. if AI202.alt then is rejected. In a language about altitudes and headings, 'non-zero means true' would be a trap: a heading condition would be false only when the aircraft points exactly north."

"Then the domain layer. The heading error here came from a variable, not a literal — caught because range checking runs after propagation. And the warning underneath is the one a general-purpose compiler could not produce: two clearances that contradict each other, with no advance between them to let the first take effect."

CONCEPT: the three error classes, kept distinct, plus a fourth domain layer.

WHY IT MATTERS: examiners specifically test whether a student confuses lexical, syntax and semantic errors.

LIKELY Q: "What is the difference between the error and the warning here?"
A: "The error is a value that cannot be legal — no heading is 450 degrees — so compilation stops. The warning is a scenario that is legal but probably not what was meant; an author might have a reason, so it reports and carries on."

TRANSITION: "Last piece — what happens when it runs."`,

// 13 Execution
`SAY: "Three things in the VM. A register machine with proper activation records, so procedures and recursion work. A flight model that integrates in one-second steps — and it is deliberately crude: fixed rates, no wind, no mass."

"And a separation monitor that measures every pair of aircraft after every simulated second."

"The trace on the left is the converging example. At one minute thirty-five the monitor records the loss of separation. At ten minutes, the assertion holds — the conflict this scenario exists to create does happen."

"The box on the right is what I would defend hardest. There is a companion file with the same geometry and a resolution applied, and it asserts the opposite — that separation is maintained. Both hold. Edit either one so the geometry changes, and the compiler tells you."

CONCEPT: runtime organisation, plus verification of intent.

WHY IT MATTERS: it closes the argument that started on slide three — the intent that was written nowhere is now checkable.

LIKELY Q: "How realistic is the flight model?"
A: "Not very, and I would not defend it as realistic. It is kinematic with fixed rates. It is good enough to decide when two aircraft come close on the scale of a training scenario and good for nothing else. It is the weakest part of the project and it is first on my limitations slide."

TRANSITION: "Let me show you what is actually in the repository, and what I did to convince myself it works."`,

// 14 Implementation
`SAY: "Ten source files, about eighty-four hundred lines of C11, no dependencies at all — no parser generator, no libraries. It builds with make and a C compiler."

"On the right is the evidence. Three hundred and thirty-six checks pass. Eighteen scenarios are run twice, with and without the optimiser, and compared — that is the differential test. The whole suite is clean under AddressSanitizer and the undefined-behaviour sanitizer. And three thousand fuzz inputs — random tokens, random bytes, mutated valid programs — produce no crash."

"The fuzz harness is in the repository as tests/fuzz.py, so you can run it yourself rather than take my word for it."

CONCEPT: verification, and making claims reproducible.

WHY IT MATTERS: numbers that can be checked carry far more weight than numbers that cannot.

LIKELY Q: "Which test would catch the most serious kind of bug?"
A: "The differential one. An unsound optimisation is the worst thing a compiler can do, because the program still runs and quietly does the wrong thing. That test compares the whole observable behaviour of eighteen scenarios with the optimiser on and off."

TRANSITION: "Against the syllabus, then."`,

// 15 Syllabus
`SAY: "Forty-five topics implemented, one partial, three worked out on paper, and ten not covered."

"Module one through six are essentially complete. Module two I would highlight: the parser is recursive descent, but because the syllabus also covers bottom-up parsing, I worked the full SLR table — the item sets, FIRST and FOLLOW, and the proof there is no shift-reduce conflict — by hand in the architecture document."

"Module seven is where I am weakest. Domain-specific languages and SSA are there; automatic parallelisation, vectorisation, cache locality and instruction scheduling are not. I have named all ten uncovered topics in the box at the bottom rather than leaving you to find them."

CONCEPT: honest coverage mapping.

WHY IT MATTERS: a table claiming every topic would not be believable. Naming the gaps makes the rest credible.

LIKELY Q: "Why no switch statement, when module four lists it?"
A: "Because the language has nothing to switch on. Adding a construct purely to tick a syllabus box would be padding, and I would rather say that than pretend."

TRANSITION: "Finally — what this is not."`,

// 16 Limitations
`SAY: "Four limitations I would rather raise myself."

"The flight model is the weakest part. Everything above it is sound in its own terms, but a scenario that depends on precise timing is only as good as that model."

"Units are a convention, not types. climb 280 means two hundred and eighty feet and is accepted. That is the one place the language looks safer than it is, and making units part of the type system is the single most valuable change I could make."

"SSA is built and printed but no pass uses it."

"And I have not loaded the output into a real simulator. The format matches the published documentation; whether it loads, I do not know, and I have not claimed otherwise anywhere in this deck."

"Next, in order: use the SSA form, add unit types, and test against BlueSky."

CONCEPT: closing on scope and judgement.

WHY IT MATTERS: finishing with limitations rather than a claim leaves the impression of someone who understands their own work.

LIKELY Q: "If you had another month, what would you do?"
A: "Unit-aware types. It is the smallest change with the biggest effect on the thing the language is for — it turns the one remaining silent failure into a compile error, and it is mostly a wider type enum plus a table of which units combine."

CLOSING LINE: "The claim is not that this solves air traffic control. It is that a small language with a complete compiler turns a class of authoring mistake into a compiler error — and that the compiler is finished enough that I can show you every stage of it. Thank you — happy to go into any phase in detail."`,

// A1 Grammar
`APPENDIX — only if asked about the grammar.

SAY: "The grammar is LL(1) by construction, which is what lets me have one function per production in the parser."

"Three places it could have been ambiguous. end closes every block, so there is no dangling-else problem at all. Comparisons do not chain — a < b < c is a syntax error, which removes a whole class of bug by construction rather than by checking. And fix and flight are each both a keyword and a type name, but they only ever appear in different positions, so one token of lookahead is enough."

LIKELY Q: "Show me there is no shift-reduce conflict."
A: "The architecture document has the full SLR construction on the expression core — the twelve LR(0) item sets and the table. The two states that could conflict are the ones with E to T dot and E to E plus T dot, where a reduce competes with shifting a star. Star is not in FOLLOW of E, so no reduce entry is placed on it and the shift stands unopposed. That is precedence falling out of the table, and it is the same fact my recursive-descent parser expresses by having parse_add call parse_mul."`,

// A2 Scope
`APPENDIX — use if asked "isn't this too big for a student project?"

SAY: "I thought about this carefully. The honest answer is that the student-sized version of this project is a smaller story, not a smaller codebase."

"The must-have row is the spine — remove any one of those and either the pipeline stops or the domain claim stops being true."

"The nice-to-have row is each a syllabus topic in its own right and cheap to explain on top of what is already there."

"The held-in-reserve row is real and tested and genuinely not needed to understand the project. SSA is the clearest example: it answers 'is there more?' rather than 'what is this?'."

"And I deliberately left things out — arrays, modules, procedures returning values, a global register allocator. Each was a decision, not an oversight."

LIKELY Q: "Would the project be better if it were smaller?"
A: "The presentation would be — which is why the core deck is sixteen slides and leads with seven stages. The code would not be: deleting working, tested, documented code to look simpler would lose information, not complexity."`,

// A3 Animations
`APPENDIX — technical note on the deck itself, not the project.

SAY (if asked): "The build steps are real PowerPoint entrance animations, written into the slide XML directly — the library I used to generate the deck has no animation API, so I post-processed the package to add the timing tree."

"I did not use morph, because it needs PowerPoint 2019 or later and degrades badly where it is unsupported. Everything is fade, which is universally supported."

"And every slide is laid out so it still reads with all the build steps visible at once, because animations do not survive a PDF export."`,

// A4 Sources
`APPENDIX — the sources.

SAY (if asked): "Four external sources and two textbook ones."

"The BlueSky wiki for the scenario file format — that is where the line format and the CRE argument order on slides two and five come from."

"Jafer, Updegrove and Chhaya from 2017 for the claim that ATC scenario development lacks standard practice — and that is also the prior art for the idea of an ATC scenario language."

"AirTrafficGen from 2025 for the description of manual scenario design as a bottleneck."

"And Aho, Lam, Sethi and Ullman for the compiler technique throughout — backpatching, the DAG, next-use, the register allocator — plus Cytron for SSA construction."

IMPORTANT: "There is no industry statistic, interview, incident record or user study anywhere in this deck, because I did not conduct one."`,
];
