# Deck build

Regenerates the Review 1 deck, `slides/AeroDSL_Review1.pptx`, from source.

```sh
node  write.js   ../../slides/AeroDSL_Review1.pptx
python3 repair.py  ../../slides/AeroDSL_Review1.pptx /tmp/rep
python3 animate.py ../../slides/AeroDSL_Review1.pptx anim.json /tmp/anim
python3 check.py   ../../slides/AeroDSL_Review1.pptx
```

| File | What it does |
|---|---|
| `theme.js` | The palette |
| `lib.js` | Drawing helpers, with build-time text-fit checking |
| `deck.js` | The 20 slides; exports the click-step plan as `ANIM` |
| `notes.js` | Presenter notes, one entry per slide |
| `write.js` | Builds the .pptx and applies the theme |
| `repair.py` | Fixes two pptxgenjs defects that make PowerPoint refuse the file, and repackages per the OPC spec — see the file's own comment |
| `animate.py` | Injects native `<p:timing>` fade entrances and slide transitions |
| `check.py` | Validates every part against ISO/IEC 29500 `pml.xsd` |

`repair.py` must run before `animate.py`: the repair rewrites paragraph XML,
the animation appends to the end of each slide part.
