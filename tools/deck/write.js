const path = require("path");
const fs = require("fs");
const { THEME } = require("./theme.js");
const { pres, ANIM, count } = require("./deck.js");
const notes = require("./notes.js");

const SKILL = "/root/.claude/skills/synced/e675df5f-2038-4d2d-b934-3a044c73085e_0372397a-2dfb-4874-b709-2db6b6d6b678/pptx";
const { applyTheme } = require(path.join(SKILL, "scripts/apply_theme.js"));

const OUT = process.argv[2] || "/tmp/deck/AeroDSL_Compiler_Design_Review.pptx";

(async () => {
  const n = count();
  if (notes.length !== n) {
    console.error(`notes ${notes.length} != slides ${n}`);
    process.exit(1);
  }
  for (let i = 0; i < n; i++) pres.slides[i].addNotes(notes[i]);

  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  fs.writeFileSync("/tmp/deck/anim.json", JSON.stringify(ANIM, null, 1));
  console.log(`wrote ${OUT} | slides ${n} | notes ${notes.length} | animated ${ANIM.length}`);
})().catch((e) => { console.error(e); process.exit(1); });
