const pptxgen = require("pptxgenjs");
const L = require("./lib.js");
const { A, W, M, CW, BODY_TOP } = L;

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE";
pres.theme = { headFontFace: L.THEME.headFontFace, bodyFontFace: L.THEME.bodyFontFace };
pres.author = "AeroDSL project";
pres.title = "AeroDSL: A Domain-Specific Language and Compiler for Simulated ATC Training Scenarios";
pres.subject = "Compiler Design course project review";

const C = pres.SchemeColor;

// ---- layouts -------------------------------------------------------------

pres.defineSlideMaster({
  title: "TITLE_DARK",
  background: { color: A.dk2 },
  objects: [],
});

pres.defineSlideMaster({
  title: "CONTENT",
  background: { color: A.dk2 },
  objects: [
    { placeholder: { options: {
        name: "title", type: "title", x: M, y: 0.40, w: CW, h: 0.62,
        fontSize: 29, bold: true, color: A.lt1, align: "left", valign: "middle", margin: 0,
      }, text: " " } },
    { placeholder: { options: {
        name: "kicker", type: "body", x: M, y: 1.02, w: CW, h: 0.42,
        fontSize: 15, color: A.lt2, align: "left", valign: "top", margin: 0,
      }, text: " " } },
    { text: { text: "AeroDSL · academic prototype · not for operational air traffic control",
        options: { x: M, y: 7.02, w: 9.4, h: 0.3, fontSize: 9, color: "4A5B70",
                   isTextBox: true, margin: 0, valign: "middle" } } },
  ],
  slideNumber: { x: 12.55, y: 7.02, w: 0.4, h: 0.3, fontSize: 9, color: "4A5B70", align: "right" },
});

pres.defineSlideMaster({
  title: "APPENDIX",
  background: { color: "08111D" },
  objects: [
    { placeholder: { options: {
        name: "title", type: "title", x: M, y: 0.40, w: CW, h: 0.62,
        fontSize: 29, bold: true, color: A.lt1, align: "left", valign: "middle", margin: 0,
      }, text: " " } },
    { placeholder: { options: {
        name: "kicker", type: "body", x: M, y: 1.02, w: CW, h: 0.42,
        fontSize: 15, color: A.lt2, align: "left", valign: "top", margin: 0,
      }, text: " " } },
    { text: { text: "Appendix", options: { x: M, y: 7.02, w: 4, h: 0.3, fontSize: 9,
        color: "4A5B70", isTextBox: true, margin: 0, valign: "middle" } } },
  ],
  slideNumber: { x: 12.55, y: 7.02, w: 0.4, h: 0.3, fontSize: 9, color: "4A5B70", align: "right" },
});

// Records which shapes animate, in click order, per slide.
const ANIM = [];
function anim(slideIdx, steps, transition) {
  ANIM.push({ slide: slideIdx, steps, transition: transition || "fade" });
}
let N = 0;                       // 1-based slide counter
const next = () => ++N;

// =========================================================================
// 1 — Title
// =========================================================================
{
  const s = pres.addSlide({ masterName: "TITLE_DARK" });
  const i = next();

  // Motif: the two converging tracks from the project's own plan view.
  // Positive w/h with flip flags: negative extents are invalid OOXML and
  // make PowerPoint refuse to open the file.
  s.addShape("line", { x: 1.1, y: 4.25, w: 5.3, h: 2.05, flipV: true,
    line: { color: A.accent1, width: 2 }, objectName: "trackA" });
  s.addShape("line", { x: 6.6, y: 4.25, w: 5.3, h: 2.05, flipV: true, flipH: true,
    line: { color: A.accent2, width: 2 }, objectName: "trackB" });
  s.addShape("ellipse", { x: 6.42, y: 4.10, w: 0.3, h: 0.3,
    fill: { type: "solid", color: A.dk2 }, line: { color: A.accent3, width: 1.75 },
    objectName: "conflict" });
  s.addText("predicted loss of separation", { x: 6.95, y: 4.10, w: 4, h: 0.3,
    isTextBox: true, margin: 0, fontSize: 10, color: A.accent3, valign: "middle",
    objectName: "conflictLabel" });

  s.addText("AeroDSL", { x: M, y: 1.72, w: CW, h: 1.0, isTextBox: true, margin: 0,
    fontSize: 60, bold: true, color: A.lt1, charSpacing: -1 });
  s.addText("A domain-specific language and compiler for simulated ATC training scenarios",
    { x: M, y: 2.72, w: 10.6, h: 0.5, isTextBox: true, margin: 0,
      fontSize: 19, color: A.accent1 });
  s.addText("Compiler Design (BCSE307L) · project review", {
    x: M, y: 3.32, w: 10.6, h: 0.35, isTextBox: true, margin: 0,
    fontSize: 13.5, color: A.accent5 });

  s.addShape("roundRect", { x: M, y: 6.52, w: 12.09, h: 0.44, rectRadius: 0.08,
    fill: { color: "16233A" }, line: { color: "2A3B52", width: 1 } });
  s.addText("Educational prototype for simulated training scenarios. Not connected to, and not for use with, real aircraft or live air traffic control.",
    { x: M + 0.2, y: 6.52, w: 11.7, h: 0.44, isTextBox: true, margin: 0,
      fontSize: 11, color: A.accent2, valign: "middle" });

  anim(i, [["trackA"], ["trackB"], ["conflict", "conflictLabel"]], "fade");
}

// =========================================================================
// 2 — The problem in plain English
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("The problem, in plain English", { placeholder: "title" });
  s.addText("A scenario is a text file of timestamped commands. Nothing checks it before it runs.",
    { placeholder: "kicker" });

  const cw = (CW - 0.6) / 3;
  L.noteCard(s, { name: "who", x: M, y: BODY_TOP, w: cw, h: 1.82,
    head: "Who", body:
      "Instructors training controllers. Researchers running simulator experiments.\n\nDomain experts — not programmers." });
  L.noteCard(s, { name: "what", x: M + cw + 0.3, y: BODY_TOP, w: cw, h: 1.82,
    head: "What they build", body:
      "Aircraft, starting positions, and a timeline of clearances.\n\nBuilt to put one situation in front of a trainee." });
  L.noteCard(s, { name: "how", x: M + 2 * (cw + 0.3), y: BODY_TOP, w: cw, h: 1.82,
    head: "How, today", body:
      "By hand. One timestamped command per line.\n\nThe simulator replays it. Nothing validates it." });

  L.codeCard(s, { name: "scn", x: M, y: 3.90, w: CW, h: 1.78, mode: "plain",
    label: "A REAL BlueSky .scn FILE — THE DOCUMENTED FORMAT",
    fontSize: 12,
    highlights: ["CRE", "ADDWPT", "0:00:00.00>", "0:00:05.00>"],
    lines: [
      "# SCN file to create a KL001 aircraft flying over checkpoint woody",
      "0:00:00.00>CRE KL001, B737, 51.890958,5.177844, 300, FL100, 170",
      "0:00:00.00>ADDWPT KL001 WOODY",
      "0:00:05.00>FF",
    ] });

  s.addText([
    { text: "Source: ", options: { color: A.accent5 } },
    { text: "BlueSky (TU Delft) wiki, “Scenario files”. Quoted, not invented.",
      options: { color: A.accent5 } },
  ], { x: M, y: 5.82, w: CW, h: 0.5, isTextBox: true, margin: 0, fontSize: 11, italic: true });

  anim(i, [["who"], ["what"], ["how"], ["scn", "scn_label", "scn_text"]], "fade");
}

// =========================================================================
// 3 — What goes wrong with that workflow
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("What goes wrong with that file", { placeholder: "title" });
  s.addText("Easy for a simulator to read. That is exactly why it is hard for a person to write.",
    { placeholder: "kicker" });

  const cw = (CW - 0.9) / 4;
  const items = [
    ["name1", "No names", "A level used ten times is typed ten times."],
    ["name2", "No conditions", "“Turn at FL300” must become a hand-computed timestamp."],
    ["name3", "No checking", "Heading 450°. An aircraft never created. You find out by watching."],
    ["name4", "No intent", "Why the scenario exists is written nowhere in it."],
  ];
  items.forEach(([n, head, body], k) => {
    L.noteCard(s, { name: n, x: M + k * (cw + 0.3), y: BODY_TOP, w: cw, h: 1.52,
      head, body, headColor: A.accent3, fontSize: 12.5 });
  });

  L.codeCard(s, { name: "edit", x: M, y: 3.66, w: CW, h: 1.78, mode: "plain",
    label: "ONE EDIT, BROKEN SILENTLY",
    fontSize: 12,
    highlights: ["FL100", "FL200", "0:03:20.00>", "?"],
    lines: [
      "0:00:00.00>CRE KL001, B737, 51.890958,5.177844, 300, FL100, 170   <- start level edited to FL200",
      "0:03:20.00>HDG KL001, 270                                          <- timestamp computed from FL100",
      "",
      "The turn now fires at the wrong moment. The file is still valid. Nothing reports it.",
    ] });

  s.addShape("roundRect", { x: M, y: 5.74, w: CW, h: 0.86, rectRadius: 0.07,
    fill: { color: "2A1620" }, line: { color: A.accent3, width: 1.25 },
    objectName: "take3" });
  s.addText("Every one of these is a property of the format, not a mistake by the author. That is what makes it worth fixing with a language.",
    { x: M + 0.26, y: 5.74, w: CW - 0.52, h: 0.86, isTextBox: true, margin: 0,
      fontSize: 14, color: A.lt1, valign: "middle", objectName: "take3_t" });

  anim(i, [["name1"], ["name2"], ["name3"], ["name4"], ["edit", "edit_label", "edit_text"],
           ["take3", "take3_t"]], "fade");
}

// =========================================================================
// 4 — Why it is worth solving, and what already exists
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("Is this a real problem? What is already out there?", { placeholder: "title" });
  s.addText("Three evidence tiers, kept apart on purpose: verified, published, assumed.",
    { placeholder: "kicker" });

  const rows = [
    ["VERIFIED", A.accent1,
      "The format has no variables, no conditionals, and no validation.",
      "Read from the BlueSky documentation and my own output."],
    ["LITERATURE", A.accent4,
      "ATC scenario development has no standardised practice.",
      "Jafer, Updegrove & Chhaya, 2017 — they propose a scenario language too."],
    ["LITERATURE", A.accent4,
      "Manual scenario design is “a demanding and time-consuming bottleneck”.",
      "Gould et al., AirTrafficGen, arXiv:2508.02269, 2025."],
    ["ASSUMPTION", A.accent2,
      "That a compiler-checked language is the right answer for an instructor.",
      "I interviewed nobody. A hypothesis, not a finding."],
  ];
  let y = BODY_TOP;
  rows.forEach(([tag, col, claim, src], k) => {
    L.pill(s, { name: "p" + k, x: M, y: y + 0.07, w: 1.28, text: tag, fill: col, color: A.dk1 });
    s.addText(claim, { x: M + 1.5, y, w: 6.5, h: 0.52, isTextBox: true, margin: 0,
      fontSize: 12.5, color: A.lt1, valign: "middle", objectName: "c" + k });
    s.addText(src, { x: M + 8.15, y, w: 3.94, h: 0.52, isTextBox: true, margin: 0,
      fontSize: 10.5, color: A.accent5, valign: "middle", objectName: "s" + k });
    y += 0.74;
  });

  L.noteCard(s, { name: "priorart", x: M, y: 4.72, w: CW, h: 1.68,
    head: "AeroDSL is not the first language proposed for this, and this deck never says it is",
    headColor: A.accent2, fontSize: 13,
    body: "ASDL and the ATC Scenario Language came first. AirTrafficGen attacks the same problem with an LLM.\n\nThe contribution is not the idea. It is a complete, small, readable compiler for it — built from scratch, with domain-aware static checking the cited work does not describe." });

  anim(i, [["p0", "p0_t", "c0", "s0"], ["p1", "p1_t", "c1", "s1"], ["p2", "p2_t", "c2", "s2"],
           ["p3", "p3_t", "c3", "s3"], ["priorart", "priorart_h", "priorart_b"]], "fade");
}

// =========================================================================
// 5 — The same scenario, written as a program
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("The same scenario, written as a program", { placeholder: "title" });
  s.addText("AeroDSL does not replace the flat file. It generates it.", { placeholder: "kicker" });

  const half = (CW - 0.5) / 2;
  L.codeCard(s, { name: "src", x: M, y: BODY_TOP, w: half, h: 3.5, mode: "aero",
    label: "WHAT THE AUTHOR WRITES   ·   scenario.aero", labelColor: A.accent1, fontSize: 11.5,
    lines: [
      'scenario "Climb to cruise"',
      "",
      "fix VOMAN 12.95 77.67",
      "flight AI202 type A320 enter VOMAN",
      "       alt FL240 hdg 090 spd 280KT",
      "",
      "let cruise = FL320",
      "clear AI202 climb cruise - 2000",
      "advance 120",
    ] });

  L.codeCard(s, { name: "out", x: M + half + 0.5, y: BODY_TOP, w: half, h: 3.5, mode: "plain",
    label: "WHAT THE COMPILER PRODUCES   ·   scenario.scn", labelColor: A.accent2, fontSize: 11.5,
    highlights: ["DEFWPT", "CRE", "ALT", "FL300"],
    lines: [
      "# Scenario generated by the AeroDSL compiler",
      '# Source scenario: "Climb to cruise"',
      "# 120 s of simulated time, 1 aircraft,",
      "# 1 clearance, 0 losses of separation.",
      "",
      "00:00:00.00>DEFWPT VOMAN,12.950000,77.670000",
      "",
      "00:00:00.00>CRE AI202,A320,12.950000,",
      "           77.670000,90,FL240,280",
      "00:00:00.00>ALT AI202,FL300",
    ] });

  L.arrowRight(s, M + half + 0.08, BODY_TOP + 1.6, 0.34, "arrow1", A.accent1);

  L.noteCard(s, { name: "gain", x: M, y: 5.38, w: CW, h: 1.14,
    head: "FL320 appears once, and FL300 was computed by the compiler",
    body: "The author wrote the intent — cruise, less two thousand feet. Constant folding worked out 30000 ft, and the simulation worked out the timestamp. Neither was typed by hand, so neither can go stale when the scenario is edited.",
    fontSize: 12 });

  anim(i, [["src", "src_label", "src_text"], ["arrow1", "out", "out_label", "out_text"],
           ["gain", "gain_h", "gain_b"]], "fade");
}

// =========================================================================
// 6 — The language in one slide
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("The language in one slide", { placeholder: "title" });
  s.addText("examples/02_converging.aero — a controller-training exercise, complete and runnable",
    { placeholder: "kicker" });

  L.codeCard(s, { name: "lang", x: M, y: BODY_TOP, w: 7.5, h: 4.95, mode: "aero", fontSize: 11,
    lines: [
      'scenario "Converging pair at FL320"',
      "",
      "fix VOMAN 12.95 77.67",
      "fix AKTIV 13.20 77.90",
      "",
      "let level = FL320",
      "",
      "flight AI202 type A320 enter VOMAN",
      "       alt level hdg 045 spd 280KT",
      "flight BA117 type B777 enter AKTIV",
      "       alt level hdg 225 spd 300KT",
      "",
      "let closest = 500",
      "let elapsed = 0",
      "while elapsed < 600 do",
      "  advance 10",
      "  closest = min(closest, hsep(AI202, BA117))",
      "  elapsed = elapsed + 10",
      "end",
      "",
      'assert closest < 5, "the pair must converge"',
    ] });

  const notes = [
    ["n1", "Declarations", "Waypoints and aircraft are first-class. An aircraft enters where its declaration appears."],
    ["n2", "Abstraction", "FL320 is written once, as level, and used by both aircraft."],
    ["n3", "Simulated state", "hsep is the separation between two aircraft right now. advance moves time forward."],
    ["n4", "Intent, made checkable", "The last line says why the scenario exists — and the compiler checks it."],
  ];
  let y = BODY_TOP;
  notes.forEach(([n, head, body], k) => {
    const hh = [1.10, 1.10, 1.10, 1.10][k];
    L.noteCard(s, { name: n, x: M + 7.8, y, w: CW - 7.8, h: hh,
      head, body, fontSize: 11.5,
      headColor: k === 3 ? A.accent2 : A.accent1 });
    y += hh + 0.14;
  });

  anim(i, [["lang", "lang_text"], ["n1", "n1_h", "n1_b"], ["n2", "n2_h", "n2_b"],
           ["n3", "n3_h", "n3_b"], ["n4", "n4_h", "n4_b"]], "fade");
}

// =========================================================================
// 7 — The compiler pipeline
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("The compiler pipeline", { placeholder: "title" });
  s.addText("Every stage is implemented and can be printed from the command line",
    { placeholder: "kicker" });

  const bw = 2.18, bh = 0.98, gap = 0.3;
  const rowA = [
    ["p_lex", "Lexer", "--tokens"],
    ["p_par", "Parser", "--ast"],
    ["p_sem", "Semantic", "--symbols"],
    ["p_ir", "Three-address", "--ir"],
    ["p_cfg", "CFG", "--cfg"],
  ];
  const rowB = [
    ["p_opt", "Optimiser", "--opt-ir"],
    ["p_an", "Domain checks", "(diagnostics)"],
    ["p_cg", "Code gen", "--bc"],
    ["p_vm", "AeroVM", "--trace"],
    ["p_scn", "Scenario file", "--scn"],
  ];
  const yA = BODY_TOP + 0.22, yB = yA + 2.0;

  rowA.forEach(([n, t, sub], k) => {
    const x = M + k * (bw + gap);
    L.chip(s, { name: n, x, y: yA, w: bw, h: bh, title: t, sub });
    if (k < 4) L.arrowRight(s, x + bw + 0.055, yA + bh / 2 - 0.085, gap - 0.11, n + "_a");
  });
  rowB.forEach(([n, t, sub], k) => {
    const x = M + k * (bw + gap);
    L.chip(s, { name: n, x, y: yB, w: bw, h: bh, title: t, sub,
      accent: k === 1 ? A.accent2 : A.accent1 });
    if (k < 4) L.arrowRight(s, x + bw + 0.055, yB + bh / 2 - 0.085, gap - 0.11, n + "_a",
      k === 0 ? A.accent2 : A.accent1);
  });

  // Elbow from the end of row A down to the start of row B.
  s.addShape("line", { x: M + 4 * (bw + gap) + bw / 2, y: yA + bh, w: 0, h: 0.52,
    line: { color: A.accent1, width: 1.6 }, objectName: "elbow1" });
  s.addShape("line", { x: M + 0.9, y: yA + bh + 0.52, w: 4 * (bw + gap) + bw / 2 - 0.9, h: 0,
    line: { color: A.accent1, width: 1.6 }, objectName: "elbow2" });
  s.addShape("line", { x: M + 0.9, y: yA + bh + 0.52, w: 0, h: yB - (yA + bh + 0.52),
    line: { color: A.accent1, width: 1.6, endArrowType: "triangle" }, objectName: "elbow3" });

  L.noteCard(s, { name: "depth", x: M, y: yB + bh + 0.38, w: CW, h: 1.16,
    head: "Also implemented, and available if you want the depth",
    headColor: A.accent4, fontSize: 11.5,
    body: "Dominators and natural loops · liveness and constant-propagation dataflow · static single assignment with pruned phi placement (--ssa) · next-use register allocation over 16 registers with spilling. None of these is needed to tell the story; all of them are real, tested and printable." });

  anim(i, [
    ["p_lex", "p_lex_t", "p_lex_s"],
    ["p_lex_a", "p_par", "p_par_t", "p_par_s"],
    ["p_par_a", "p_sem", "p_sem_t", "p_sem_s"],
    ["p_sem_a", "p_ir", "p_ir_t", "p_ir_s"],
    ["p_ir_a", "p_cfg", "p_cfg_t", "p_cfg_s", "elbow1", "elbow2", "elbow3"],
    ["p_opt", "p_opt_t", "p_opt_s"],
    ["p_opt_a", "p_an", "p_an_t", "p_an_s"],
    ["p_an_a", "p_cg", "p_cg_t", "p_cg_s"],
    ["p_cg_a", "p_vm", "p_vm_t", "p_vm_s"],
    ["p_vm_a", "p_scn", "p_scn_t", "p_scn_s"],
    ["depth", "depth_h", "depth_b"],
  ], "fade");
}

// =========================================================================
// 8 — Walkthrough A: source -> tokens -> AST
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("One program, all the way through — part 1", { placeholder: "title" });
  s.addText("Every box on this slide and the next is real output from ./aerodsl, not an illustration",
    { placeholder: "kicker" });

  const w3 = (CW - 0.7) / 3;
  L.codeCard(s, { name: "w_src", x: M, y: BODY_TOP, w: w3, h: 4.3, mode: "aero", fontSize: 11,
    label: "1 · SOURCE   examples/13_walkthrough.aero", labelColor: A.accent1,
    lines: [
      'scenario "Climb to cruise"',
      "",
      "fix VOMAN 12.95 77.67",
      "flight AI202 type A320",
      "  enter VOMAN alt FL240",
      "  hdg 090 spd 280KT",
      "",
      "let cruise = FL320",
      "clear AI202 climb",
      "  cruise - 2000",
      "advance 120",
      "",
    ] });

  L.codeCard(s, { name: "w_tok", x: M + w3 + 0.35, y: BODY_TOP, w: w3, h: 4.3, mode: "plain",
    fontSize: 10.5, label: "2 · TOKENS   --tokens   (11 OF 31)", labelColor: A.accent1,
    highlights: ["FLEVEL", "SPEED", "CALLSIGN", "IDENT", "24000", "32000", "280"],
    lines: [
      "3:5   CALLSIGN  VOMAN",
      "3:11  NUMBER    12.95   value 12.95",
      "4:1   flight    flight",
      "4:8   CALLSIGN  AI202",
      "4:19  CALLSIGN  A320",
      "4:40  FLEVEL    FL240   value 24000",
      "4:50  NUMBER    090     value 90",
      "4:58  SPEED     280KT   value 280",
      "6:5   IDENT     cruise",
      "6:14  FLEVEL    FL320   value 32000",
      "7:13  climb     climb",
      "",
      "FL240 is one token worth 24000 ft.",
    ] });

  L.codeCard(s, { name: "w_ast", x: M + 2 * (w3 + 0.35), y: BODY_TOP, w: w3, h: 4.3,
    mode: "plain", fontSize: 11, label: "3 · AST   --ast   (TAIL)", labelColor: A.accent1,
    highlights: ["num", "flight", "Binary '-'", "Clear climb"],
    lines: [
      "  Main",
      "    Enter AI202 at VOMAN",
      "      alt:  Num 24000 : num",
      "      hdg:  Num 90 : num",
      "      spd:  Num 280 : num",
      "    Let cruise",
      "      Num 32000 : num",
      "    Clear climb",
      "      Name AI202 : flight",
      "      Binary '-' : num",
      "        Name cruise : num",
      "        Num 2000 : num",
    ] });

  s.addText("Lexer — recognising the words.", { x: M, y: 6.14, w: w3, h: 0.34,
    isTextBox: true, margin: 0, fontSize: 11.5, color: A.accent5, objectName: "cap1" });
  s.addText("Parser — checking the grammar, building the tree.", {
    x: M + w3 + 0.35, y: 6.14, w: w3, h: 0.34, isTextBox: true, margin: 0,
    fontSize: 11.5, color: A.accent5, objectName: "cap2" });
  s.addText("Semantic analysis — every node now carries a type.", {
    x: M + 2 * (w3 + 0.35), y: 6.14, w: w3, h: 0.34, isTextBox: true, margin: 0,
    fontSize: 11.5, color: A.accent5, objectName: "cap3" });

  anim(i, [["w_src", "w_src_label", "w_src_text", "cap1"],
           ["w_tok", "w_tok_label", "w_tok_text", "cap2"],
           ["w_ast", "w_ast_label", "w_ast_text", "cap3"]], "fade");
}

// =========================================================================
// 9 — Walkthrough B: TAC -> optimised -> bytecode
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("One program, all the way through — part 2", { placeholder: "title" });
  s.addText("The same eight lines, now as three-address code, optimised, and as bytecode",
    { placeholder: "kicker" });

  const w3 = (CW - 0.7) / 3;
  L.codeCard(s, { name: "x_ir", x: M, y: BODY_TOP, w: w3, h: 3.1, mode: "plain", fontSize: 11,
    label: "4 · THREE-ADDRESS CODE   --ir", labelColor: A.accent1,
    highlights: ["cruise = 32000", "t1 = cruise - 2000", "clear AI202, climb, t1"],
    lines: [
      "0   param 24000",
      "1   param 90",
      "2   param 280",
      "3   enter AI202",
      "4   cruise = 32000",
      "5   t1 = cruise - 2000",
      "6   clear AI202, climb, t1",
      "7   advance 120",
      "8   return",
    ] });

  L.codeCard(s, { name: "x_opt", x: M + w3 + 0.35, y: BODY_TOP, w: w3, h: 3.1, mode: "plain",
    fontSize: 11, label: "5 · OPTIMISED   --opt-ir", labelColor: A.accent2,
    highlights: ["clear AI202, climb, 30000"],
    lines: [
      "0   param 24000",
      "1   param 90",
      "2   param 280",
      "3   enter AI202",
      "4   clear AI202, climb, 30000",
      "5   advance 120",
      "6   return",
      "",
      "Three instructions became one.",
    ] });

  L.codeCard(s, { name: "x_bc", x: M + 2 * (w3 + 0.35), y: BODY_TOP, w: w3, h: 3.1,
    mode: "plain", fontSize: 11, label: "6 · BYTECODE   --bc   (7 OF 13 SHOWN)", labelColor: A.accent1,
    highlights: ["clear  r0, climb, r1", "loadk  r1, 30000"],
    lines: [
      " 6  enter  AI202",
      " 7  loadk  r0, 0",
      " 8  loadk  r1, 30000",
      " 9  clear  r0, climb, r1",
      "10  loadk  r0, 120",
      "11  adv    r0",
      "12  halt",
      "",
      "13 instructions, 2 frame slots, 16 registers.",
    ] });

  L.codeCard(s, { name: "x_out", x: M, y: 5.06, w: CW, h: 1.6, mode: "plain", fontSize: 11.5,
    label: "7 · RUN IT   --trace   then the scenario file", labelColor: A.accent2,
    highlights: ["AI202 cleared climb 30000 ft", "00:00:00.00>ALT AI202,FL300", "alt  27600 ft"],
    lines: [
      "00:00:00  AI202 enters the airspace          final state",
      "00:00:00  AI202 cleared climb 30000 ft       AI202   alt  27600 ft  hdg  90  spd 280 kt",
      "",
      "00:00:00.00>CRE AI202,A320,12.950000,77.670000,90,FL240,280        00:00:00.00>ALT AI202,FL300",
    ] });

  anim(i, [["x_ir", "x_ir_label", "x_ir_text"],
           ["x_opt", "x_opt_label", "x_opt_text"],
           ["x_bc", "x_bc_label", "x_bc_text"],
           ["x_out", "x_out_label", "x_out_text"]], "fade");
}
// =========================================================================
// 10 — Optimisation in detail
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("Optimisation, and why it is more than tidying", { placeholder: "title" });
  s.addText("Seven passes, repeated to a fixed point, because each one creates work for the others",
    { placeholder: "kicker" });

  L.codeCard(s, { name: "o_b", x: M, y: BODY_TOP, w: 5.0, h: 1.72, mode: "plain", fontSize: 11.5,
    label: "BEFORE", labelColor: A.accent5,
    highlights: ["cruise = 32000", "t1 = cruise - 2000"],
    lines: ["cruise = 32000", "t1 = cruise - 2000", "clear AI202, climb, t1"] });

  L.arrowRight(s, M + 5.12, BODY_TOP + 0.78, 0.4, "o_arrow", A.accent2);

  L.codeCard(s, { name: "o_a", x: M + 5.66, y: BODY_TOP, w: 5.0, h: 1.72, mode: "plain",
    fontSize: 11.5, label: "AFTER", labelColor: A.accent2,
    highlights: ["clear AI202, climb, 30000"],
    lines: ["clear AI202, climb, 30000", "", "propagate · fold · eliminate the dead copy"] });

  const counters = [
    ["constants folded", "5"], ["operands replaced by propagated constants", "11"],
    ["common subexpressions eliminated", "2"], ["copies propagated", "6"],
    ["algebraic simplifications", "1"], ["dead instructions removed", "15"],
    ["instructions hoisted out of loops", "3"], ["jumps threaded or removed", "6"],
    ["unreachable blocks removed", "2"], ["peephole rewrites", "4"],
  ];
  s.addShape("roundRect", { x: M, y: 3.78, w: 6.6, h: 2.72, rectRadius: 0.07,
    fill: { color: A.accent6 }, line: { color: "22344F", width: 1 }, objectName: "cbox" });
  s.addText("ON examples/06_optimise.aero — EVERY PASS FIRES", {
    x: M + 0.2, y: 3.92, w: 6.2, h: 0.26, isTextBox: true, margin: 0,
    fontSize: 10.5, bold: true, color: A.accent5, charSpacing: 1.2, objectName: "cbox_h" });
  counters.forEach(([k, v], idx) => {
    const col = idx % 2, row = Math.floor(idx / 2);
    s.addText(k, { x: M + 0.2 + col * 3.2, y: 4.28 + row * 0.38, w: 2.55, h: 0.3,
      isTextBox: true, margin: 0, fontSize: 10, color: A.lt2, valign: "middle",
      objectName: "ck" + idx });
    s.addText(v, { x: M + 2.72 + col * 3.2, y: 4.28 + row * 0.38, w: 0.4, h: 0.3,
      isTextBox: true, margin: 0, fontSize: 11.5, bold: true, color: A.accent2,
      align: "right", valign: "middle", fontFace: "Courier New", objectName: "cv" + idx });
  });
  s.addText("61 three-address instructions in, 37 out", {
    x: M + 0.2, y: 6.14, w: 6.2, h: 0.26, isTextBox: true, margin: 0,
    fontSize: 11, italic: true, color: A.accent1, objectName: "cbox_f" });

  L.noteCard(s, { name: "enable", x: M + 6.9, y: 3.60, w: CW - 6.9, h: 3.16,
    head: "The pass that earns its place by producing an error message",
    headColor: A.accent2, fontSize: 12,
    body: "Range checking runs after constant propagation. So this —\n\n     let h = 450\n     clear AI202 heading h\n\n— is a compile-time error, because by then the compiler knows what h holds.\n\nAn optimisation is usually justified by the code it produces. This one is justified by the error message it makes possible." });

  anim(i, [["o_b", "o_b_label", "o_b_text"], ["o_arrow", "o_a", "o_a_label", "o_a_text"],
           ["cbox", "cbox_h", "cbox_f",
            ...counters.map((_, k) => "ck" + k), ...counters.map((_, k) => "cv" + k)],
           ["enable", "enable_h", "enable_b"]], "fade");
}

// =========================================================================
// 11 — The state barrier
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("The one rule that makes all of it safe", { placeholder: "title" });
  s.addText("Here the domain constrains the compiler, not the other way round.",
    { placeholder: "kicker" });

  L.codeCard(s, { name: "b_loop", x: M, y: BODY_TOP, w: 5.6, h: 1.5, mode: "aero", fontSize: 13,
    label: "A PERFECTLY ORDINARY LOOP", labelColor: A.accent5,
    lines: ["while AI202.alt < FL300 do", "  advance 10", "end"] });

  L.noteCard(s, { name: "b_bad", x: M, y: 3.34, w: 5.6, h: 1.40,
    head: "What a textbook optimiser would do", headColor: A.accent3, fontSize: 12.5,
    body: "AI202.alt looks loop-invariant: same operands, nothing in the loop assigns to it. Hoist it, evaluate once — and the loop never terminates." });

  L.noteCard(s, { name: "b_rule", x: M + 5.9, y: BODY_TOP, w: CW - 5.9, h: 1.44,
    head: "The rule", headColor: A.accent1, fontSize: 13,
    body: "clear, advance, enter and call change the simulated world. They are barriers, and a read of that world never moves across one." });

  L.codeCard(s, { name: "b_code", x: M + 5.9, y: 3.30, w: CW - 5.9, h: 1.96, mode: "plain",
    fontSize: 11, label: "src/cfg.c", labelColor: A.accent5,
    highlights: ["IR_ADVANCE", "IR_CLEAR", "IR_ENTER", "IR_CALL"],
    lines: [
      "int ir_is_barrier(const Ir *ir) {",
      "    switch (ir->op) {",
      "    case IR_ADVANCE: case IR_CLEAR:",
      "    case IR_ENTER:   case IR_CALL:  return 1;",
      "    default: return 0;",
      "    } }",
    ] });

  L.noteCard(s, { name: "b_proof", x: M, y: 5.46, w: CW, h: 1.16,
    head: "And it is checked, not just asserted", headColor: A.accent2, fontSize: 12.5,
    body: "Eighteen scenarios run twice — with the optimiser and without. Diagnostics, the whole event trace and every aircraft's final state must match exactly." });

  anim(i, [["b_loop", "b_loop_label", "b_loop_text"], ["b_bad", "b_bad_h", "b_bad_b"],
           ["b_rule", "b_rule_h", "b_rule_b"], ["b_code", "b_code_label", "b_code_text"],
           ["b_proof", "b_proof_h", "b_proof_b"]], "fade");
}

// =========================================================================
// 12 — Error detection
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("What the compiler catches, and when", { placeholder: "title" });
  s.addText("Three layers. Each message below is copied from a run, not written for the slide.",
    { placeholder: "kicker" });

  L.codeCard(s, { name: "e_src", x: M, y: BODY_TOP, w: 5.9, h: 2.92, mode: "aero", fontSize: 11.5,
    label: "examples/14_review_errors.aero   (LINES 8-18, HEADER OMITTED)",
    labelColor: A.accent3,
    lines: [
      ' 8  scenario "Errors"',
      "10  fix VOMAN 12.95 77.67",
      "11  flight AI202 type A320 enter VOMAN",
      "          alt FL240 hdg 090 spd 280KT",
      "",
      "13  let turn = 450",
      "14  clear AI202 heading turn",
      "",
      "16  clear AI202 climb FL320",
      "17  clear AI202 descend FL280",
    ] });

  L.codeCard(s, { name: "e_out", x: M + 6.2, y: BODY_TOP, w: CW - 6.2, h: 2.52, mode: "plain",
    fontSize: 10.5, label: "./aerodsl examples/14_review_errors.aero", labelColor: A.accent3,
    highlights: ["semantic error", "semantic warning", "450"],
    lines: [
      "14:1: semantic error: the operand of 'heading'",
      "      must be between 0 and 360 deg, found 450",
      "",
      "17:1: semantic warning: AI202 was cleared 'climb'",
      "      at line 16 and 'descend' here with no 'advance'",
      "      between them: the two contradict each other",
      "",
      "exit status 1",
    ] });

  const layers = [
    ["l1", "Lexical and syntax", A.accent4,
      "Malformed tokens and broken grammar. Every one is reported — the parser recovers at the next statement."],
    ["l2", "Semantic", A.accent1,
      "Types, arity, scope. No implicit bool: if AI202.alt then is rejected, because “non-zero is true” is a trap here."],
    ["l3", "Domain", A.accent2,
      "Operand ranges and clearance consistency. The layer a general-purpose compiler could not have."],
  ];
  const cw3 = (CW - 0.6) / 3;
  layers.forEach(([n, head, col, body], k) => {
    L.noteCard(s, { name: n, x: M + k * (cw3 + 0.3), y: 4.78, w: cw3, h: 1.80,
      head, headColor: col, body, fontSize: 12 });
  });

  anim(i, [["e_src", "e_src_label", "e_src_text"], ["e_out", "e_out_label", "e_out_text"],
           ["l1", "l1_h", "l1_b"], ["l2", "l2_h", "l2_b"], ["l3", "l3_h", "l3_b"]], "fade");
}

// =========================================================================
// 13 — Execution and output
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("Running it: the VM, the monitor, the assertion", { placeholder: "title" });
  s.addText("The target is a virtual machine because the scenario must run to produce its own timeline.",
    { placeholder: "kicker" });

  L.noteCard(s, { name: "v1", x: M, y: BODY_TOP, w: 3.84, h: 1.40,
    head: "A register machine", fontSize: 12,
    body: "16 registers per activation record, frame slots for spills. Recursion works, capped at depth 256." });
  L.noteCard(s, { name: "v2", x: M + 4.14, y: BODY_TOP, w: 3.84, h: 1.40,
    head: "A flight model", fontSize: 12,
    body: "advance integrates one simulated second at a time: 1800 ft/min, 3°/s, 1 kt/s. Kinematic and crude." });
  L.noteCard(s, { name: "v3", x: M + 8.28, y: BODY_TOP, w: CW - 8.28, h: 1.40,
    head: "A separation monitor", headColor: A.accent2, fontSize: 12,
    body: "Every simulated second, every pair. A loss is anything inside 5 NM and 1000 ft." });

  L.codeCard(s, { name: "v_tr", x: M, y: 3.30, w: 7.4, h: 2.26, mode: "plain", fontSize: 11,
    label: "examples/02_converging.aero  ·  --trace", labelColor: A.accent1,
    highlights: ["LOSS OF SEPARATION", "assertion held", "4.93 NM"],
    lines: [
      "00:00:00  AI202 enters the airspace",
      "00:00:00  BA117 enters the airspace",
      "00:01:35  LOSS OF SEPARATION: AI202 and BA117:",
      "          4.93 NM apart, 0 ft vertically",
      "00:10:00  print: closest approach: 1.34877 NM",
      "00:10:00  assertion held: the pair must come",
      "          within 5 NM, or there is no exercise",
    ] });

  L.noteCard(s, { name: "v_pt", x: M + 7.7, y: 3.30, w: CW - 7.7, h: 2.34,
    head: "The part I would defend hardest", headColor: A.accent2, fontSize: 12.5,
    body: "The scenario states its own purpose, and the compiler checks it.\n\nexamples/12 is the same geometry with a resolution applied and the opposite assertion. Both hold. Change the geometry and the compiler tells you." });

  s.addShape("roundRect", { x: M, y: 5.86, w: CW, h: 0.80, rectRadius: 0.07,
    fill: { color: "0F2B27" }, line: { color: A.accent1, width: 1.25 },
    objectName: "take13" });
  s.addText("No other phase can answer “did the scenario do what it was written to do?”. That is why the compiler ends in a simulator.",
    { x: M + 0.26, y: 5.86, w: CW - 0.52, h: 0.80, isTextBox: true, margin: 0,
      fontSize: 14, color: A.lt1, valign: "middle", objectName: "take13_t" });

  anim(i, [["v1", "v1_h", "v1_b"], ["v2", "v2_h", "v2_b"], ["v3", "v3_h", "v3_b"],
           ["v_tr", "v_tr_label", "v_tr_text"], ["v_pt", "v_pt_h", "v_pt_b"],
           ["take13", "take13_t"]], "fade");
}
// =========================================================================
// 14 — Implementation and testing
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("Implementation and evidence", { placeholder: "title" });
  s.addText("About 8,400 lines of C11 with no dependencies — no parser generator, no libraries",
    { placeholder: "kicker" });

  const files = [
    ["lexer.c", "296", "scanner, typed tokens, positions"],
    ["parser.c", "925", "recursive descent, one function per production"],
    ["sema.c", "482", "types, arity, scope, nine warnings"],
    ["ir.c", "639", "three-address code, backpatching"],
    ["cfg.c", "490", "blocks, dominators, loops, liveness"],
    ["ssa.c", "435", "dominance frontiers, pruned phis"],
    ["opt.c", "885", "seven passes to a fixed point"],
    ["analysis.c", "286", "ranges and clearance consistency"],
    ["codegen.c", "739", "next-use, allocation, bytecode, peephole"],
    ["vm.c", "826", "machine, flight model, separation monitor"],
  ];
  s.addShape("roundRect", { x: M, y: BODY_TOP, w: 6.9, h: 4.6, rectRadius: 0.07,
    fill: { color: A.accent6 }, line: { color: "22344F", width: 1 }, objectName: "fbox" });
  s.addText("SOURCE", { x: M + 0.22, y: BODY_TOP + 0.14, w: 6.4, h: 0.26, isTextBox: true,
    margin: 0, fontSize: 10.5, bold: true, color: A.accent5, charSpacing: 1.2,
    objectName: "fbox_h" });
  files.forEach(([f, n, d], k) => {
    const y = BODY_TOP + 0.52 + k * 0.4;
    s.addText(f, { x: M + 0.22, y, w: 1.3, h: 0.32, isTextBox: true, margin: 0,
      fontSize: 11, color: A.accent1, fontFace: "Courier New", valign: "middle",
      objectName: "ff" + k });
    s.addText(n, { x: M + 1.52, y, w: 0.5, h: 0.32, isTextBox: true, margin: 0,
      fontSize: 10.5, color: A.accent5, align: "right", fontFace: "Courier New",
      valign: "middle", objectName: "fn" + k });
    s.addText(d, { x: M + 2.2, y, w: 4.5, h: 0.32, isTextBox: true, margin: 0,
      fontSize: 11, color: A.lt2, valign: "middle", objectName: "fd" + k });
  });

  const stats = [
    ["336", "checks pass", A.accent1],
    ["18", "scenarios run twice, with and without the optimiser, and compared", A.accent2],
    ["0", "findings under AddressSanitizer and UBSan", A.accent1],
    ["3000", "fuzz inputs, no crash", A.accent1],
  ];
  let y = BODY_TOP;
  stats.forEach(([big, lab, col], k) => {
    s.addShape("roundRect", { x: M + 7.2, y, w: CW - 7.2, h: 1.02, rectRadius: 0.07,
      fill: { color: A.accent6 }, line: { color: "22344F", width: 1 }, objectName: "sb" + k });
    s.addText(big, { x: M + 7.4, y: y + 0.06, w: 1.5, h: 0.9, isTextBox: true, margin: 0,
      fontSize: 34, bold: true, color: col, valign: "middle", objectName: "sbig" + k });
    s.addText(lab, { x: M + 8.95, y: y + 0.06, w: CW - 9.15, h: 0.9, isTextBox: true, margin: 0,
      fontSize: 11.5, color: A.lt2, valign: "middle", objectName: "slab" + k });
    y += 1.2;
  });

  s.addText("make  ·  make test  ·  make fuzz  —  all three run clean from a fresh unzip of the submitted archive",
    { x: M, y: 6.46, w: CW, h: 0.32, isTextBox: true, margin: 0, fontSize: 11.5,
      italic: true, color: A.accent1, objectName: "runline" });

  anim(i, [
    ["fbox", "fbox_h", ...files.map((_, k) => "ff" + k), ...files.map((_, k) => "fn" + k),
     ...files.map((_, k) => "fd" + k)],
    ["sb0", "sbig0", "slab0"], ["sb1", "sbig1", "slab1"],
    ["sb2", "sbig2", "slab2"], ["sb3", "sbig3", "slab3"], ["runline"],
  ], "fade");
}

// =========================================================================
// 15 — Syllabus mapping
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("Against the Compiler Design syllabus", { placeholder: "title" });
  s.addText("45 topics implemented, 1 partial, 3 worked on paper, 10 not covered — and the deck names all ten",
    { placeholder: "kicker" });

  const mods = [
    ["1", "Lexical analysis", "Hand-written scanner, token specification, the FL320 / AI202 DFA", "5", "0", "1", "2"],
    ["2", "Syntax analysis", "LL(1) recursive descent; SLR table worked by hand in the docs", "6", "0", "2", "2"],
    ["3", "Semantic analysis", "SDD over the AST; the L-attributed backpatching scheme", "5", "0", "0", "0"],
    ["4", "Intermediate code", "Quadruples, short-circuit backpatching, procedures", "9", "0", "0", "1"],
    ["5", "Optimisation", "Blocks, DAG, dataflow, loops, peephole", "10", "1", "0", "0"],
    ["6", "Code generation", "Next-use, register allocation, activation records", "6", "0", "0", "0"],
    ["7", "Parallelism", "Domain-specific languages, SSA", "4", "0", "0", "5"],
  ];

  const cols = [0.4, 1.95, 4.75, 0.6, 0.6, 0.6, 0.6];
  const heads = ["", "Module", "Where it is in AeroDSL", "impl.", "part.", "paper", "not"];
  let x = M + 0.16;
  heads.forEach((h, k) => {
    if (h) s.addText(h, { x: x + (k >= 3 ? 0 : 0), y: BODY_TOP, w: cols[k], h: 0.3,
      isTextBox: true, margin: 0, fontSize: 9.5, bold: true, color: A.accent5,
      charSpacing: 1, align: k >= 3 ? "center" : "left", objectName: "th" + k });
    x += cols[k] + 0.18;
  });

  let y = BODY_TOP + 0.32;
  mods.forEach(([num, name, where, impl, part, paper, not], k) => {
    s.addShape("roundRect", { x: M, y: y - 0.03, w: CW, h: 0.50, rectRadius: 0.05,
      fill: { color: k % 2 ? "101C2E" : A.accent6 }, line: { width: 0 },
      objectName: "row" + k });
    let cx = M + 0.16;
    s.addText(num, { x: cx, y, w: cols[0], h: 0.44, isTextBox: true, margin: 0,
      fontSize: 13, bold: true, color: A.accent1, valign: "middle", objectName: "m" + k });
    cx += cols[0] + 0.18;
    s.addText(name, { x: cx, y, w: cols[1], h: 0.44, isTextBox: true, margin: 0,
      fontSize: 11, bold: true, color: A.lt1, valign: "middle", objectName: "mn" + k });
    cx += cols[1] + 0.18;
    s.addText(where, { x: cx, y, w: cols[2], h: 0.44, isTextBox: true, margin: 0,
      fontSize: 10, color: A.lt2, valign: "middle", objectName: "mw" + k });
    cx += cols[2] + 0.18;
    [[impl, A.accent1], [part, part === "0" ? A.accent5 : A.accent2],
     [paper, paper === "0" ? A.accent5 : A.accent4],
     [not, not === "0" ? A.accent5 : A.accent3]]
      .forEach(([v, col], j) => {
        s.addText(v, { x: cx, y, w: cols[3 + j], h: 0.44, isTextBox: true, margin: 0,
          fontSize: 12.5, bold: true, color: col, align: "center", valign: "middle",
          fontFace: "Courier New", objectName: "mc" + k + "_" + j });
        cx += cols[3 + j] + 0.18;
      });
    y += 0.52;
  });

  L.noteCard(s, { name: "syl", x: M, y: y + 0.06, w: CW, h: 1.08,
    head: "The ten not covered, named", headColor: A.accent3, fontSize: 10.5,
    body: "LLVM · Lex · operator-precedence parsing · CLR and LALR construction · switch statements · automatic parallelisation · cache-locality optimisation · vectorisation · instruction scheduling · software pipelining. Fifty-nine topics in all; a project claiming every one of them would not be believable." });

  anim(i, [
    ...mods.map((_, k) => ["row" + k, "m" + k, "mn" + k, "mw" + k, "mc" + k + "_0",
                           "mc" + k + "_1", "mc" + k + "_2", "mc" + k + "_3"]),
    ["syl", "syl_h", "syl_b"],
  ], "fade");
}

// =========================================================================
// 16 — Limitations and conclusion
// =========================================================================
{
  const s = pres.addSlide({ masterName: "CONTENT" });
  const i = next();
  s.addText("What it is not, and what comes next", { placeholder: "title" });
  s.addText("The four limitations I would raise before you had to ask me.", { placeholder: "kicker" });

  const lims = [
    ["w1", "The flight model is the weakest part", A.accent3,
      "Fixed rates, no mass, no wind, no performance envelope. A scenario that depends on precise timing is only as good as this."],
    ["w2", "Units are a convention, not types", A.accent3,
      "clear AI202 climb 280 means 280 feet, and is accepted. The one place the language looks safer than it is."],
    ["w3", "SSA is built but unused", A.accent2,
      "The construction is complete and printable, but no optimisation pass consumes it yet."],
    ["w4", "Unverified against a real simulator", A.accent2,
      "The format matches the published BlueSky documentation. I have never loaded the file into BlueSky."],
  ];
  const cw2 = (CW - 0.3) / 2;
  lims.forEach(([n, head, col, body], k) => {
    L.noteCard(s, { name: n, x: M + (k % 2) * (cw2 + 0.3), y: BODY_TOP + Math.floor(k / 2) * 1.62,
      w: cw2, h: 1.44, head, headColor: col, body, fontSize: 12 });
  });

  s.addShape("roundRect", { x: M, y: 5.12, w: CW, h: 1.32, rectRadius: 0.07,
    fill: { color: "0F2B27" }, line: { color: A.accent1, width: 1.5 }, objectName: "concl" });
  s.addText("Next, in order", { x: M + 0.24, y: 5.24, w: 3, h: 0.3, isTextBox: true,
    margin: 0, fontSize: 12.5, bold: true, color: A.accent1, objectName: "concl_h" });
  s.addText("1 · use the SSA form in the optimiser     2 · make units part of the type system     3 · load the output into BlueSky",
    { x: M + 0.24, y: 5.56, w: CW - 0.48, h: 0.34, isTextBox: true, margin: 0,
      fontSize: 12.5, color: A.lt2, objectName: "concl_a" });
  s.addText("The claim is narrow: a small language with a complete compiler turns a class of authoring mistake into a compiler error — and the compiler is finished enough to show you every stage of it.",
    { x: M + 0.24, y: 5.94, w: CW - 0.48, h: 0.4, isTextBox: true, margin: 0,
      fontSize: 12, italic: true, color: A.accent5, objectName: "concl_b" });

  anim(i, [["w1", "w1_h", "w1_b"], ["w2", "w2_h", "w2_b"], ["w3", "w3_h", "w3_b"],
           ["w4", "w4_h", "w4_b"], ["concl", "concl_h", "concl_a", "concl_b"]], "fade");
}
// =========================================================================
// A1 — Grammar
// =========================================================================
{
  const s = pres.addSlide({ masterName: "APPENDIX" });
  const i = next();
  s.addText("Appendix A · the grammar", { placeholder: "title" });
  s.addText("LL(1) by construction, which is what lets one function per production parse it",
    { placeholder: "kicker" });

  L.codeCard(s, { name: "g1", x: M, y: BODY_TOP, w: 6.1, h: 5.25, mode: "plain", fontSize: 9.6,
    label: "DECLARATIONS AND STATEMENTS", labelColor: A.accent1,
    highlights: ['"clear"', '"advance"', '"assert"', '"flight"', '"fix"', '"proc"'],
    lines: [
      'program      ::= "scenario" STRING item*',
      "item         ::= fix_decl | flight_decl",
      "               | proc_decl | stmt",
      "",
      'fix_decl     ::= "fix" CALLSIGN coord coord',
      'coord        ::= [ "-" ] NUMBER',
      'flight_decl  ::= "flight" CALLSIGN',
      '                 [ "type" CALLSIGN ] "enter" CALLSIGN',
      '                 "alt" expr "hdg" expr "spd" expr',
      'proc_decl    ::= "proc" IDENT "(" [ params ] ")"',
      '                 block "end"',
      "",
      'stmt         ::= "let" IDENT "=" expr',
      '               | IDENT "=" expr',
      '               | "clear" expr clear_op expr',
      '               | "advance" expr',
      '               | "if" expr "then" block',
      '                 [ "else" block ] "end"',
      '               | "while" expr "do" block "end"',
      '               | "assert" expr [ "," STRING ]',
      '               | "call" IDENT "(" [ args ] ")"',
      '               | "print" print_arg { "," print_arg }',
      '               | "return"',
      'clear_op     ::= "climb" | "descend" | "heading"',
      '               | "speed" | "direct"',
    ] });

  L.codeCard(s, { name: "g2", x: M + 6.4, y: BODY_TOP, w: CW - 6.4, h: 3.18, mode: "plain",
    fontSize: 10, label: "EXPRESSIONS — ONE PRODUCTION PER PRECEDENCE LEVEL",
    labelColor: A.accent1,
    highlights: ["rel_op", "non-associative"],
    lines: [
      'expr      ::= or_expr',
      'or_expr   ::= and_expr { "or" and_expr }',
      'and_expr  ::= not_expr { "and" not_expr }',
      'not_expr  ::= "not" not_expr | rel_expr',
      'rel_expr  ::= add_expr [ rel_op add_expr ]    non-associative',
      'add_expr  ::= mul_expr { ("+" | "-") mul_expr }',
      'mul_expr  ::= unary { ("*" | "/" | "%") unary }',
      'unary     ::= "-" unary | postfix',
      'postfix   ::= primary { "." field }',
      'field     ::= "alt" | "hdg" | "spd" | "lat" | "lon"',
      'primary   ::= NUMBER | FLEVEL | SPEED | "true" | "false"',
      '            | IDENT | CALLSIGN | builtin "(" args ")"',
      '            | "(" expr ")"',
    ] });

  L.noteCard(s, { name: "g3", x: M + 6.4, y: BODY_TOP + 3.38, w: CW - 6.4, h: 1.52,
    head: "Three places it could have been ambiguous, and is not", fontSize: 11,
    body: "end closes every block, so there is no dangling else. A comparison cannot chain, so a < b < c is a syntax error rather than nonsense. fix and flight are both keywords and type names, but only ever in different positions." });

  anim(i, [["g1", "g1_label", "g1_text"], ["g2", "g2_label", "g2_text"], ["g3", "g3_h", "g3_b"]],
    "fade");
}

// =========================================================================
// A2 — Scope decisions
// =========================================================================
{
  const s = pres.addSlide({ masterName: "APPENDIX" });
  const i = next();
  s.addText("Appendix B · what is core, what is depth", { placeholder: "title" });
  s.addText("Nothing was deleted. This is about which parts carry the story and which are held in reserve.",
    { placeholder: "kicker" });

  const groups = [
    ["MUST HAVE", A.accent1,
      "Lexer · parser · AST · symbol table · type checking · three-address code with backpatching · basic blocks · constant folding and propagation · dead code · code generation · the AeroVM · the scenario file · domain range and clearance checks · the state barrier",
      "These are the spine. Remove any one and either the pipeline stops or the domain claim stops being true."],
    ["NICE TO HAVE", A.accent2,
      "DAG common-subexpression elimination · loop-invariant code motion · dominators and natural loops · next-use register allocation with spilling · the peephole pass · the browser demonstration",
      "Each is a syllabus topic in its own right and each is cheap to explain on top of what is already there. Kept, and shown only if there is time."],
    ["HELD IN RESERVE", A.accent4,
      "SSA construction with dominance frontiers · the control-flow-graph dump · the next-use dump · the fuzz harness",
      "Real, tested, and genuinely not needed to understand the project. They answer “is there more?” rather than “what is this?”."],
    ["DELIBERATELY ABSENT", A.accent5,
      "Arrays and records · modules · procedures returning values · a global register allocator · global CSE · unit-aware types · any link to real aviation data or systems",
      "Each was considered and left out: the first four for scope, unit types as the single best future extension, the last as a hard line the project does not cross."],
  ];
  let y = BODY_TOP;
  groups.forEach(([tag, col, items, why], k) => {
    s.addShape("roundRect", { x: M, y, w: CW, h: 1.06, rectRadius: 0.07,
      fill: { color: k % 2 ? "101C2E" : A.accent6 }, line: { color: "22344F", width: 1 },
      objectName: "gg" + k });
    L.pill(s, { name: "gp" + k, x: M + 0.2, y: y + 0.14, w: 1.75, text: tag, fill: col,
      color: A.dk1 });
    s.addText(items, { x: M + 2.15, y: y + 0.10, w: CW - 2.4, h: 0.52, isTextBox: true,
      margin: 0, fontSize: 10.5, color: A.lt1, valign: "top", objectName: "gi" + k });
    s.addText(why, { x: M + 2.15, y: y + 0.62, w: CW - 2.4, h: 0.40, isTextBox: true,
      margin: 0, fontSize: 10.5, italic: true, color: A.accent5, valign: "top",
      objectName: "gw" + k });
    y += 1.18;
  });

  s.addText("The student-sized version of this project is a smaller story, not a smaller codebase. Working, tested, documented code is not worth deleting to look simpler.",
    { x: M, y: y + 0.12, w: CW, h: 0.4, isTextBox: true, margin: 0, fontSize: 12,
      italic: true, color: A.accent1, objectName: "gnote" });

  anim(i, [...groups.map((_, k) => ["gg" + k, "gp" + k, "gp" + k + "_t", "gi" + k, "gw" + k]),
           ["gnote"]], "fade");
}

// =========================================================================
// A3 — Animation inventory
// =========================================================================
{
  const s = pres.addSlide({ masterName: "APPENDIX" });
  const i = next();
  s.addText("Appendix C · how the animation in this deck works", { placeholder: "title" });
  s.addText("Recorded here so nothing in the deck is claimed to move when it does not",
    { placeholder: "kicker" });

  const rows = [
    ["NATIVE", A.accent1, "Entrance animations on every build step",
      "Real PowerPoint click-triggered fade entrances (presetID 10, presetClass entr), written as a <p:timing> tree directly into each slide's XML. pptxgenjs has no animation API, so this was post-processed into the package."],
    ["NATIVE", A.accent1, "Slide transitions",
      "A <p:transition> fade on every slide. Universally supported."],
    ["NOT USED", A.accent5, "Morph transition",
      "Needs the PowerPoint 2019+ p168 namespace and degrades badly where it is unsupported. Fade was chosen instead; the deck says so rather than implying morph."],
    ["NOT USED", A.accent5, "Motion paths, spins, bounces, sound",
      "Deliberately excluded. Every animation here exists to sequence an explanation, not to decorate it."],
    ["FALLBACK", A.accent2, "PDF export",
      "Animations do not survive PDF export, so every slide is laid out to be complete and readable with all build steps visible at once. Nothing is hidden behind an animation."],
  ];
  let y = BODY_TOP;
  rows.forEach(([tag, col, what, detail], k) => {
    s.addShape("roundRect", { x: M, y, w: CW, h: 0.86, rectRadius: 0.06,
      fill: { color: k % 2 ? "101C2E" : A.accent6 }, line: { width: 0 },
      objectName: "ar" + k });
    L.pill(s, { name: "ap" + k, x: M + 0.2, y: y + 0.14, w: 1.2, text: tag, fill: col,
      color: A.dk1 });
    s.addText(what, { x: M + 1.6, y: y + 0.1, w: 3.3, h: 0.74, isTextBox: true, margin: 0,
      fontSize: 11.5, bold: true, color: A.lt1, valign: "middle", objectName: "aw" + k });
    s.addText(detail, { x: M + 5.0, y: y + 0.1, w: CW - 5.2, h: 0.74, isTextBox: true,
      margin: 0, fontSize: 10.5, color: A.lt2, valign: "middle", objectName: "ad" + k });
    y += 0.95;
  });

  s.addText("To present: each build step advances on a click, exactly like a normal PowerPoint build. Pressing through without pausing shows the finished slide.",
    { x: M, y: y + 0.14, w: CW, h: 0.4, isTextBox: true, margin: 0, fontSize: 12,
      italic: true, color: A.accent1, objectName: "anote" });

  anim(i, [...rows.map((_, k) => ["ar" + k, "ap" + k, "ap" + k + "_t", "aw" + k, "ad" + k]),
           ["anote"]], "fade");
}

// =========================================================================
// A4 — Sources
// =========================================================================
{
  const s = pres.addSlide({ masterName: "APPENDIX" });
  const i = next();
  s.addText("Appendix D · sources", { placeholder: "title" });
  s.addText("Everything the deck attributes to someone else", { placeholder: "kicker" });

  const srcs = [
    ["BlueSky scenario-file format", "TU Delft CNS/ATM",
      "github.com/TUDelft-CNS-ATM/bluesky — wiki, “Scenario files”. The H:MM:SS.SS> line format, the # comment convention and the CRE argument order on slides 2 and 5 are taken from this page."],
    ["ATC scenario development lacks standard practice", "Jafer, Updegrove & Chhaya, 2017",
      "“Towards a Common Air Traffic Control Simulation Scenario Development”, SciTePress, pp. 52–70, DOI 10.5220/0007905000520070. Proposes an ATC Scenario Language extending ASDL."],
    ["Aviation Scenario Definition Language (ASDL)", "Jafer et al., Embry-Riddle",
      "A formal scenario definition language for aviation. The prior art AeroDSL's idea sits behind."],
    ["Manual scenario design is a bottleneck", "Gould, De Ath, Carvell & Pepper, 2025",
      "“AirTrafficGen: Configurable Air Traffic Scenario Generation with Large Language Models”, arXiv:2508.02269. Describes manual design as “a demanding and time-consuming bottleneck”."],
    ["Compiler technique throughout", "Aho, Lam, Sethi & Ullman",
      "Compilers: Principles, Techniques and Tools, 2nd ed. Backpatching, the DAG, next-use information and the register allocator follow the schemes in the text."],
    ["SSA construction", "Cytron et al.",
      "Dominance frontiers, phi placement and renaming over the dominator tree."],
  ];
  let y = BODY_TOP;
  srcs.forEach(([what, who, detail], k) => {
    s.addText(what, { x: M, y, w: 4.4, h: 0.3, isTextBox: true, margin: 0,
      fontSize: 11.5, bold: true, color: A.accent1, objectName: "sw" + k });
    s.addText(who, { x: M, y: y + 0.3, w: 4.4, h: 0.28, isTextBox: true, margin: 0,
      fontSize: 10.5, italic: true, color: A.accent5, objectName: "sh" + k });
    s.addText(detail, { x: M + 4.6, y, w: CW - 4.6, h: 0.72, isTextBox: true, margin: 0,
      fontSize: 10.5, color: A.lt2, valign: "top", objectName: "sd" + k });
    y += 0.80;
  });

  s.addText("No industry statistic, interview, incident record or user study is quoted anywhere in this deck, because none was conducted.",
    { x: M, y: y + 0.1, w: CW, h: 0.4, isTextBox: true, margin: 0, fontSize: 12,
      italic: true, color: A.accent2, objectName: "snote" });

  anim(i, [...srcs.map((_, k) => ["sw" + k, "sh" + k, "sd" + k]), ["snote"]], "fade");
}

// =========================================================================
module.exports = { pres, ANIM, count: () => N };
