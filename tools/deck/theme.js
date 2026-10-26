// Radar-console palette: phosphor teal and amber on deep navy. Chosen for
// the domain (an ATC display) rather than a default corporate blue.
const THEME = {
  name: "AeroDSL Radar",
  headFontFace: "Arial",
  bodyFontFace: "Calibri",
  colors: {
    dk1: "0A121E",   // near-black navy, text on light
    lt1: "FFFFFF",
    dk2: "0C1626",   // slide background
    lt2: "DCE6F2",   // body text on dark
    accent1: "35C6A8", // phosphor teal  - primary
    accent2: "E8A33D", // amber          - secondary / caution
    accent3: "E85D75", // rose           - errors
    accent4: "7FA3DB", // slate blue     - supporting
    accent5: "8D9CAF", // muted grey     - captions
    accent6: "16233A", // card fill
    hlink: "35C6A8",
    folHlink: "8D9CAF",
  },
};
module.exports = { THEME };
