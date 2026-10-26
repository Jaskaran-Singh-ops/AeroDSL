"""Run a scenario file in BlueSky, headless, and print what happened.

    pip install bluesky-simulator==1.1.1
    python bluesky/run_bluesky.py bluesky/05_loop.scn 300 10

Arguments: the .scn file, how many simulated seconds to run, and how often to
print each aircraft's state (default 60 s). sel_alt is the altitude the
aircraft is cleared to. A command BlueSky cannot apply, such as one naming
AI220, is reported as an error printed at the moment that line runs:
"Aircraft with callsign AI220 not found"."""
import os, sys, tempfile
import bluesky as bs
from bluesky.simulation import ScreenIO

msgs = []


class Screen(ScreenIO):
    def echo(self, text="", flags=0):
        msgs.append(text)

    def update(self):
        pass


scn, tend = sys.argv[1], float(sys.argv[2])
every = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
bs.init(mode="sim", detached=True, workdir=tempfile.mkdtemp(prefix="bluesky_"))
bs.scr = Screen()
bs.stack.stack(f"IC {os.path.abspath(scn)}")
bs.stack.stack("OP")
bs.sim.step()
nxt = 0.0
while bs.sim.simt < tend:
    bs.sim.step()
    if bs.sim.simt >= nxt:
        rows = []
        for i, acid in enumerate(bs.traf.id):
            rows.append(f"{acid} alt={bs.traf.alt[i] / 0.3048:7.0f}ft hdg={bs.traf.hdg[i]:5.1f} "
                        f"cas={bs.traf.cas[i] / 0.514444:5.1f}kt sel_alt={bs.traf.selalt[i] / 0.3048:7.0f}")
        print(f"t={bs.sim.simt:7.1f}  " + " | ".join(rows))
        nxt += every
if msgs:
    print("--- messages from BlueSky ---")
    for m in msgs:
        print(m)
