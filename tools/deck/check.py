#!/usr/bin/env python3
"""Schema-validates every slide, layout, master and notes part of a .pptx."""
import io
import sys
import zipfile
from lxml import etree

SCH = "/mnt/skills/public/pptx/scripts/office/schemas/ISO-IEC29500-4_2016/pml.xsd"
schema = etree.XMLSchema(etree.parse(SCH))
z = zipfile.ZipFile(sys.argv[1])
names = [n for n in z.namelist()
         if n.endswith(".xml") and ("/slides/" in n or "/slideLayouts/" in n
                                    or "/slideMasters/" in n or "/notesSlides/" in n)]
bad = 0
for n in sorted(names):
    doc = etree.parse(io.BytesIO(z.read(n)))
    if not schema.validate(doc):
        bad += 1
        print(f"INVALID {n}")
        for e in schema.error_log[:2]:
            print("   ", e.message[:200])
nl = z.namelist()
print(f"checked {len(names)} parts, {bad} invalid")
print(f"[Content_Types].xml is entry {nl.index('[Content_Types].xml')} of {len(nl)}")
print(f"directory entries: {sum(1 for n in nl if n.endswith('/'))}")
sys.exit(1 if bad else 0)
