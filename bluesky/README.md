# BlueSky files used in the review

These are the files behind the BlueSky comparison slides. Every result on
those slides came from replaying them in BlueSky 1.1.1, headless, with
`run_bluesky.py`.

| File | What it is |
|---|---|
| `03_procedures.scn` | what the compiler writes for `examples/03_procedures.aero`. It is also exactly what an author would type by hand. |
| `15_longer_hold.scn` | what the compiler writes for `examples/15_longer_hold.aero`. Diff it with the file above: ten of the twelve timed lines move. |
| `05_loop.scn` | what the compiler writes for `examples/05_loop.aero`. The turn is at 00:03:20. |
| `05_loop_atalt.scn` | example 5 with the turn written as BlueSky's own trigger, `ATALT`. |
| `four_mistakes.scn` | hand-written, with the four mistakes in `examples/16_four_mistakes.aero`. |

The three generated files are reproducible:

```sh
./aerodsl examples/03_procedures.aero  > bluesky/03_procedures.scn
./aerodsl examples/15_longer_hold.aero > bluesky/15_longer_hold.scn
./aerodsl examples/05_loop.aero        > bluesky/05_loop.scn
```

## Replaying them in BlueSky

```sh
pip install bluesky-simulator==1.1.1
python bluesky/run_bluesky.py bluesky/05_loop.scn 300 10
```

The arguments are the file, how many simulated seconds to run, and how often
to print each aircraft's state.

BlueSky's first lines, about loading the navigation database and its cache
file, are BlueSky setting itself up; they are not errors.

What you should see:

- `05_loop.scn` runs with no messages. AI202 is at about 28,939 ft at
  t = 200 s, which is 3:20, and starts turning then. It is still about
  1,060 ft below FL300, because BlueSky's A320 climbs at about 1,500 ft/min
  and our flight model assumes 1,800 ft/min.
- `05_loop_atalt.scn` turns when AI202 reaches FL300, at about 4:02.
- `four_mistakes.scn`:
  - heading 450 is accepted silently and flown as 090;
  - speed 40 is accepted, and the A320 slows to about 130 kt and stays there;
  - `AI220` is reported only when its line runs, at 00:00:20, as an error
    printed during the run: "Aircraft with callsign AI220 not found";
  - the two ALT commands at the same time: the second silently wins
    (`sel_alt` becomes 28000).
