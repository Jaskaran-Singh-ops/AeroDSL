#!/usr/bin/env python3
"""
Injects native PowerPoint animation into the generated deck.

pptxgenjs has no animation API, so the animations are written straight into
the OOXML after the file is built. Each click step becomes one <p:par> under
the main sequence; the first shape of a step is a clickEffect, the rest are
withEffect so they appear together. Every effect is presetID 10 (Fade),
presetClass "entr", which is the same object PowerPoint writes when you pick
Animations > Fade, so the result is editable in the Animation Pane rather
than being a picture of an animation.

Shapes are addressed by the objectName pptxgenjs wrote into
<p:cNvPr name="...">, which is resolved here to the numeric shape id the
timing tree needs.
"""
import json
import os
import re
import shutil
import sys
import zipfile

PPTX = sys.argv[1]
ANIM = sys.argv[2]
WORK = sys.argv[3]

TRANSITIONS = {
    "fade": "<p:fade/>",
    "wipe-left": '<p:wipe dir="r"/>',
    "wipe-up": '<p:wipe dir="u"/>',
    "morph": "<p:fade/>",  # morph is not expressible here; documented as a substitution
}


def effect(ids, first, cid):
    """One <p:par> holding the shapes of a single click step."""
    out = []
    for n, spid in enumerate(ids):
        node = "clickEffect" if (first and n == 0) else "withEffect"
        delay = 0
        out.append(
            f'<p:par><p:cTn id="{cid}" presetID="10" presetClass="entr" presetSubtype="0"'
            f' fill="hold" grpId="0" nodeType="{node}">'
            f'<p:stCondLst><p:cond delay="{delay}"/></p:stCondLst><p:childTnLst>'
            f'<p:set><p:cBhvr><p:cTn id="{cid+1}" dur="1" fill="hold">'
            f'<p:stCondLst><p:cond delay="0"/></p:stCondLst></p:cTn>'
            f'<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
            f"<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst>"
            f'</p:cBhvr><p:to><p:strVal val="visible"/></p:to></p:set>'
            f'<p:animEffect transition="in" filter="fade"><p:cBhvr>'
            f'<p:cTn id="{cid+2}" dur="500"/>'
            f'<p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
            f"</p:cBhvr></p:animEffect>"
            f"</p:childTnLst></p:cTn></p:par>"
        )
        cid += 3
    return "".join(out), cid


def timing(steps):
    cid = 3
    pars = []
    for step in steps:
        inner, cid2 = effect(step, True, cid + 2)
        pars.append(
            f'<p:par><p:cTn id="{cid}" fill="hold">'
            f'<p:stCondLst><p:cond delay="indefinite"/></p:stCondLst><p:childTnLst>'
            f'<p:par><p:cTn id="{cid+1}" fill="hold">'
            f'<p:stCondLst><p:cond delay="0"/></p:stCondLst><p:childTnLst>'
            f"{inner}"
            f"</p:childTnLst></p:cTn></p:par>"
            f"</p:childTnLst></p:cTn></p:par>"
        )
        cid = cid2
    builds = "".join(f'<p:bldP spid="{s}" grpId="0"/>' for step in steps for s in step)
    return (
        "<p:timing><p:tnLst>"
        '<p:par><p:cTn id="1" dur="indefinite" restart="never" nodeType="tmRoot">'
        "<p:childTnLst>"
        '<p:seq concurrent="1" nextAc="seek">'
        '<p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst>'
        + "".join(pars)
        + "</p:childTnLst></p:cTn>"
        '<p:prevCondLst><p:cond evt="onPrev" delay="0">'
        "<p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>"
        '<p:nextCondLst><p:cond evt="onNext" delay="0">'
        "<p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst>"
        "</p:seq>"
        "</p:childTnLst></p:cTn></p:par>"
        "</p:tnLst>"
        f"<p:bldLst>{builds}</p:bldLst>"
        "</p:timing>"
    )


def main():
    if os.path.isdir(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    with zipfile.ZipFile(PPTX) as z:
        all_names = [n for n in z.namelist() if not n.endswith("/")]
        z.extractall(WORK, all_names)

    anim = json.load(open(ANIM))
    report = []
    for entry in anim:
        n = entry["slide"]
        p = os.path.join(WORK, "ppt", "slides", f"slide{n}.xml")
        xml = open(p, encoding="utf-8").read()
        names = {
            m.group(2): m.group(1)
            for m in re.finditer(r'<p:cNvPr id="(\d+)" name="([^"]*)"', xml)
        }
        steps, missing = [], []
        for step in entry["steps"]:
            ids = []
            for nm in step:
                if nm in names:
                    ids.append(names[nm])
                else:
                    missing.append(nm)
            if ids:
                steps.append(ids)
        if missing:
            print(f"  slide {n}: unresolved objectNames {missing}", file=sys.stderr)

        tr = TRANSITIONS.get(entry.get("transition", "fade"), "<p:fade/>")
        block = f'<p:transition spd="med">{tr}</p:transition>' + timing(steps)
        assert "<p:timing>" not in xml and "<p:transition" not in xml, f"slide {n} already timed"
        xml = xml.replace("</p:sld>", block + "</p:sld>")
        open(p, "w", encoding="utf-8").write(xml)
        report.append(
            {
                "slide": n,
                "clickSteps": len(steps),
                "shapes": sum(len(s) for s in steps),
                "transition": entry.get("transition", "fade"),
                "unresolved": missing,
            }
        )

    # Repack the way the OPC specification wants it: [Content_Types].xml
    # first, no directory entries. `zip -r` puts it wherever readdir found it,
    # which PowerPoint can reject.
    out = os.path.abspath(PPTX)
    names = [n for n in all_names if not n.endswith("/")]
    ordered = ["[Content_Types].xml"] + [n for n in names if n != "[Content_Types].xml"]
    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for n in ordered:
            z.write(os.path.join(WORK, n), n)
    os.replace(tmp, out)
    json.dump(report, open("/tmp/deck/anim_report.json", "w"), indent=1)
    tot_steps = sum(r["clickSteps"] for r in report)
    tot_shapes = sum(r["shapes"] for r in report)
    unres = sum(len(r["unresolved"]) for r in report)
    print(f"animated {len(report)} slides | {tot_steps} click steps | {tot_shapes} shapes | unresolved {unres}")


main()
