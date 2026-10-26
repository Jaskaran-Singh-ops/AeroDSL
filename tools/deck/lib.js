// Shared drawing helpers for the AeroDSL deck.
const { THEME } = require("./theme.js");
const A = THEME.colors;

const W = 13.333, H = 7.5;
const M = 0.62;                 // page margin
const CW = W - 2 * M;           // content width
const BODY_TOP = 1.72;          // first y below the title block

// ---- AeroDSL syntax colouring -------------------------------------------

const KEYWORDS = new Set(["scenario","fix","flight","type","enter","alt","hdg","spd",
  "lat","lon","proc","let","clear","climb","descend","heading","speed","direct",
  "advance","if","then","else","while","do","end","assert","call","print","return",
  "and","or","not","true","false","num","bool"]);
const BUILTINS = new Set(["vsep","hsep","dist","bearing","abs","min","max","now"]);

// Splits one line of AeroDSL into coloured runs.
function aeroRuns(line) {
  const out = [];
  const hash = line.indexOf("#");
  let code = line, comment = null;
  if (hash >= 0) { code = line.slice(0, hash); comment = line.slice(hash); }

  const re = /("[^"]*")|([A-Z][A-Z0-9]*)|([a-z_][a-z0-9_]*)|([0-9]+(?:\.[0-9]+)?)|(\s+)|(.)/g;
  let m;
  while ((m = re.exec(code)) !== null) {
    const t = m[0];
    let color = A.lt2;
    if (m[1]) color = A.accent4;                       // string
    else if (m[2]) color = /^FL[0-9]{3}$/.test(t) ? A.lt1 : A.accent2;  // callsign / flight level
    else if (m[3]) color = KEYWORDS.has(t) ? A.accent1 : (BUILTINS.has(t) ? A.accent1 : A.lt2);
    else if (m[4]) color = A.lt1;                      // number
    else if (m[5]) color = A.lt2;
    else color = A.accent5;                            // punctuation
    const bold = !!(m[3] && KEYWORDS.has(t));
    out.push({ text: t, options: { color, bold } });
  }
  if (comment !== null) out.push({ text: comment, options: { color: A.accent5, italic: true } });
  return out;
}

// Generic colouring for compiler output: a leading index dim, the rest plain,
// with a few substrings highlighted.
function plainRuns(line, highlights = [], hiColor = A.accent2) {
  if (!highlights.length) return [{ text: line, options: { color: A.lt2 } }];
  let parts = [{ text: line, hi: false }];
  for (const h of highlights) {
    const next = [];
    for (const p of parts) {
      if (p.hi) { next.push(p); continue; }
      let s = p.text, idx;
      while ((idx = s.indexOf(h)) >= 0) {
        if (idx > 0) next.push({ text: s.slice(0, idx), hi: false });
        next.push({ text: h, hi: true });
        s = s.slice(idx + h.length);
      }
      if (s) next.push({ text: s, hi: false });
    }
    parts = next;
  }
  return parts.map(p => ({ text: p.text, options: { color: p.hi ? hiColor : A.lt2, bold: p.hi } }));
}

// ---- building blocks -----------------------------------------------------

// A dark code card. `mode`: "aero" | "plain". `highlights` applies in plain mode.
function codeCard(slide, o) {
  const name = o.name || ("card" + Math.random().toString(36).slice(2, 7));
  slide.addShape("roundRect", {
    x: o.x, y: o.y, w: o.w, h: o.h, rectRadius: 0.06,
    fill: { color: o.fill || A.accent6 },
    line: { color: o.border || "22344F", width: 1 },
    objectName: name,
  });
  let top = o.y + 0.13;
  if (o.label) {
    slide.addText(o.label, {
      x: o.x + 0.18, y: top, w: o.w - 0.36, h: 0.26, isTextBox: true, margin: 0,
      fontSize: 10.5, bold: true, color: o.labelColor || A.accent5,
      charSpacing: 1.2, objectName: name + "_label",
    });
    top += 0.34;
  }
  const fs = o.fontSize || 11.5;
  const runs = [];
  o.lines.forEach((ln, i) => {
    const r = o.mode === "aero" ? aeroRuns(ln) : plainRuns(ln, o.highlights || []);
    if (!r.length) r.push({ text: " ", options: { color: A.lt2 } });
    r[r.length - 1].options = Object.assign({}, r[r.length - 1].options,
      i < o.lines.length - 1 ? { breakLine: true } : {});
    runs.push(...r);
  });
  const needH = o.lines.length * (fs * 1.32) / 72 + 0.10;
  const haveH = o.y + o.h - top - 0.12;
  if (needH > haveH + 0.005)
    console.error(`  FIT codeCard ${name}: needs ${needH.toFixed(2)}" has ${haveH.toFixed(2)}" (${o.lines.length} lines @ ${fs}pt)`);
  slide.addText(runs, {
    x: o.x + 0.18, y: top, w: o.w - 0.36, h: o.y + o.h - top - 0.12,
    isTextBox: true, margin: 0, fontFace: "Courier New", fontSize: fs,
    lineSpacing: fs * 1.32, valign: "top", objectName: name + "_text",
  });
  return name;
}

// A numbered pipeline chip.
function chip(slide, o) {
  const name = o.name;
  slide.addShape("roundRect", {
    x: o.x, y: o.y, w: o.w, h: o.h, rectRadius: 0.08,
    fill: { color: o.fill || A.accent6 },
    line: { color: o.accent || A.accent1, width: o.dim ? 1 : 1.75 },
    objectName: name,
  });
  slide.addText(o.title, {
    x: o.x + 0.1, y: o.y + 0.13, w: o.w - 0.2, h: 0.3, isTextBox: true, margin: 0,
    fontSize: 12.5, bold: true, color: o.dim ? A.accent5 : A.lt1, align: "center",
    objectName: name + "_t",
  });
  if (o.sub) {
    slide.addText(o.sub, {
      x: o.x + 0.08, y: o.y + 0.44, w: o.w - 0.16, h: 0.46, isTextBox: true, margin: 0,
      fontSize: 9.5, color: o.accent || A.accent1, align: "center",
      fontFace: "Courier New", objectName: name + "_s",
    });
  }
  return name;
}

function arrowRight(slide, x, y, w, name, color) {
  slide.addShape("rightArrow", {
    x, y, w, h: 0.17, fill: { color: color || A.accent1 }, line: { width: 0 },
    objectName: name,
  });
}

// A headed block of prose used in card grids.
function noteCard(slide, o) {
  const name = o.name;
  slide.addShape("roundRect", {
    x: o.x, y: o.y, w: o.w, h: o.h, rectRadius: 0.07,
    fill: { color: o.fill || A.accent6 }, line: { color: o.border || "22344F", width: 1 },
    objectName: name,
  });
  slide.addText(o.head, {
    x: o.x + 0.2, y: o.y + 0.16, w: o.w - 0.4, h: 0.3, isTextBox: true, margin: 0,
    fontSize: 13, bold: true, color: o.headColor || A.accent1, objectName: name + "_h",
  });
  // Rough wrap estimate so prose overflow is caught at build time rather than
  // in the rendered PDF. ~0.50 em average advance for Calibri at body sizes.
  {
    const fs = o.fontSize || 12;
    const cpl = Math.max(8, Math.floor(((o.w - 0.4) * 72) / (fs * 0.50)));
    const nl = String(o.body).split("\n")
      .reduce((a, ln) => a + Math.max(1, Math.ceil(ln.length / cpl)), 0);
    const need = nl * (fs * 1.33) / 72;
    const have = o.h - 0.68;
    if (need > have + 0.02)
      console.error(`  FIT noteCard ${name}: needs ${need.toFixed(2)}" has ${have.toFixed(2)}" (~${nl} lines @ ${fs}pt)`);
  }
  slide.addText(o.body, {
    x: o.x + 0.2, y: o.y + 0.52, w: o.w - 0.4, h: o.h - 0.68, isTextBox: true, margin: 0,
    fontSize: o.fontSize || 12, color: A.lt2, lineSpacing: (o.fontSize || 12) * 1.33,
    valign: "top", objectName: name + "_b",
  });
  return name;
}

// A small pill used for evidence tiers and statuses.
function pill(slide, o) {
  slide.addShape("roundRect", {
    x: o.x, y: o.y, w: o.w, h: 0.3, rectRadius: 0.15,
    fill: { color: o.fill }, line: { width: 0 }, objectName: o.name,
  });
  slide.addText(o.text, {
    x: o.x, y: o.y, w: o.w, h: 0.3, isTextBox: true, margin: 0,
    fontSize: 9.5, bold: true, color: o.color || A.dk1, align: "center",
    charSpacing: 0.8, objectName: o.name + "_t",
  });
}

module.exports = { THEME, A, W, H, M, CW, BODY_TOP, aeroRuns, plainRuns,
                   codeCard, chip, arrowRight, noteCard, pill };
