#!/usr/bin/env python3
"""
Repairs two classes of OOXML defect that make PowerPoint refuse a deck, and
repackages the file the way the OPC specification wants it.

1. pptxgenjs writes one <a:pPr> per *run* instead of one per *paragraph*. Any
   paragraph with more than one run therefore gets an <a:pPr> after a run,
   which is invalid: <a:pPr> must be the first child of <a:p>. Every
   syntax-coloured code block in this deck is a multi-run paragraph, so this
   affected eleven slides. The repair keeps the first <a:pPr> of each paragraph
   and deletes the rest -- they are duplicates of it.

2. A shape given a negative width or height is written with a negative
   <a:ext cx=".." cy=".."/>, which the schema forbids. The repair makes the
   extent positive and sets the matching flip flag on the <a:xfrm>, which is
   how PowerPoint itself represents a shape drawn backwards.

Then the package is rebuilt with [Content_Types].xml as the first entry and no
directory entries, which is what the OPC specification asks for.
"""
import os
import re
import shutil
import sys
import zipfile

SRC = sys.argv[1]
WORK = sys.argv[2]

PARA = re.compile(r"<a:p>.*?</a:p>", re.S)
PPR = re.compile(r"<a:pPr(?:\s[^>]*)?/>|<a:pPr(?:\s[^>]*)?>.*?</a:pPr>", re.S)
XFRM = re.compile(r"<a:xfrm(?P<attrs>[^>]*)>(?P<body>.*?)</a:xfrm>", re.S)
EXT = re.compile(r'<a:ext cx="(-?\d+)" cy="(-?\d+)"\s*/>')

stats = {"pPr": 0, "ext": 0, "files": 0}


def fix_paragraphs(xml):
    def one(m):
        p = m.group(0)
        hits = list(PPR.finditer(p))
        if len(hits) <= 1:
            return p
        # Keep the first; drop the rest, last-first so offsets stay valid.
        for h in reversed(hits[1:]):
            p = p[: h.start()] + p[h.end():]
            stats["pPr"] += 1
        return p

    return PARA.sub(one, xml)


def fix_extents(xml):
    def one(m):
        attrs, body = m.group("attrs"), m.group("body")
        e = EXT.search(body)
        if not e:
            return m.group(0)
        cx, cy = int(e.group(1)), int(e.group(2))
        if cx >= 0 and cy >= 0:
            return m.group(0)
        if cx < 0 and ' flipH="1"' not in attrs:
            attrs += ' flipH="1"'
        if cy < 0 and ' flipV="1"' not in attrs:
            attrs += ' flipV="1"'
        body = body[: e.start()] + f'<a:ext cx="{abs(cx)}" cy="{abs(cy)}"/>' + body[e.end():]
        stats["ext"] += 1
        return f"<a:xfrm{attrs}>{body}</a:xfrm>"

    return XFRM.sub(one, xml)


def main():
    if os.path.isdir(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    with zipfile.ZipFile(SRC) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        z.extractall(WORK, names)

    for n in names:
        if not n.endswith(".xml"):
            continue
        if not (n.startswith("ppt/slides/") or n.startswith("ppt/slideLayouts/")
                or n.startswith("ppt/slideMasters/") or n.startswith("ppt/notesSlides/")
                or n.startswith("ppt/notesMasters/")):
            continue
        p = os.path.join(WORK, n)
        xml = open(p, encoding="utf-8").read()
        out = fix_extents(fix_paragraphs(xml))
        if out != xml:
            open(p, "w", encoding="utf-8").write(out)
            stats["files"] += 1

    # [Content_Types].xml first, no directory entries.
    ordered = ["[Content_Types].xml"] + [n for n in names if n != "[Content_Types].xml"]
    tmp = SRC + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for n in ordered:
            z.write(os.path.join(WORK, n), n)
    os.replace(tmp, SRC)
    print(f"repaired: {stats['pPr']} stray <a:pPr> removed, "
          f"{stats['ext']} negative extents flipped, {stats['files']} parts rewritten")


main()
