"""Copy the example programs into the demo page, verbatim.

The demo page (ui/index.html) keeps its samples inline so that it needs nothing
but the compiler's /compile endpoint. This script rewrites that list from
examples/*.aero, so the page always compiles exactly the files in the
repository, with the same line numbers in every message.

    python3 tools/sync_ui_examples.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (file, label). The label names the example and the slides of the Review 2 and
# Review 3 decks (slides/) that show it.
SAMPLES = [
    ("13_walkthrough.aero", "Climb to cruise · example 13 · Review 2: slides 7 to 10"),
    ("03_procedures.aero", "Three arrivals · example 3 · Review 3: slides 8 and 11"),
    ("15_longer_hold.aero", "Longer hold · example 15 · Review 3: slide 9"),
    ("16_four_mistakes.aero", "Four mistakes · example 16 · Review 3: slide 10"),
    ("05_loop.aero", "Wait for a level · example 5 · Review 2: slides 5, 13 · Review 3: slides 6, 7, 12"),
    ("04_conditional.aero", "Conditional · example 4 · Review 2: slide 13"),
    ("17_scope.aero", "Scope error · example 17 · Review 2: slide 12"),
    ("06_optimise.aero", "Optimiser demo · example 6 · Review 2: slide 14 · Review 3: slide 13"),
    ("14_review_errors.aero", "Range error in a variable · example 14 · Review 2: slide 15"),
    ("02_converging.aero", "Converging pair · example 2"),
    ("12_separation.aero", "Same pair, resolved · example 12"),
    ("01_minimal.aero", "Minimal · example 1"),
    ("11_boundaries.aero", "Boundaries · example 11"),
    ("07_lexical_errors.aero", "Lexical errors · example 7"),
    ("08_syntax_errors.aero", "Syntax errors · example 8"),
    ("09_type_errors.aero", "Type errors · example 9"),
    ("10_domain_errors.aero", "Domain errors · example 10"),
]


def js_template(text):
    return "`" + text.replace("\\", "\\\\").replace("`", "\\`").replace("${", "\\${") + "`"


def main():
    listed = {f for f, _ in SAMPLES}
    missing = sorted(set(os.listdir(os.path.join(ROOT, "examples"))) - listed)
    if missing:
        raise SystemExit("not in the demo page list: " + ", ".join(missing))
    body = []
    for f, label in SAMPLES:
        with open(os.path.join(ROOT, "examples", f), encoding="utf-8") as fh:
            body.append(f"  {label!r}: {js_template(fh.read())}".replace("'", '"', 2) if '"' not in label else
                        f"  {label!r}: {js_template(fh.read())}")
    block = ("/* ------------------------------------------------------------------ *\n"
             " * Examples: every file in examples/, copied verbatim by\n"
             " * tools/sync_ui_examples.py, named with the review slides that use it.\n"
             " * ------------------------------------------------------------------ */\n\n"
             "const EXAMPLES = {\n" + ",\n\n".join(body) + "\n};")
    path = os.path.join(ROOT, "ui", "index.html")
    with open(path, encoding="utf-8") as fh:
        page = fh.read()
    pat = re.compile(r"/\* -+ \*\n \* Examples\..*?\n\};", re.S)
    if not pat.search(page):
        pat = re.compile(r"/\* -+ \*\n \* Examples: .*?\n\};", re.S)
    page, n = pat.subn(lambda m: block, page, count=1)
    if n != 1:
        raise SystemExit("could not find the EXAMPLES block in ui/index.html")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(page)
    print(f"wrote {len(SAMPLES)} examples into ui/index.html")


if __name__ == "__main__":
    main()
