#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every drum kit puts the same kind of sound on the same lane (so a pattern sounds right whichever kit is loaded).

The 10 lanes play instruments (drums.c LANE_DEF_INST: BD SD CP CH OH LT HT RS CR RD); a kit answers a lane's GM note.
  synthesised kits  drum_synth.c DS_MAP: note -> the kit's lane of gen_drumkits.py LANES, so one table orders all 34 kits;
                    each kit's sound for that lane must be of the lane's kind (a kick a low tone, a hat above a ...)
  X0X kits          drum_x0x.c X9_NOTE / X8_NOTE: note -> the machine's voice (BD SD LT MT HT RS CP CH OH CR RD / ...)
  sampled kits      tools/gen_samples.py GM_KIT: note -> role
Exit status: the number of failed checks."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_drumkits as g        

REF = [("BD", 36, "KICK"), ("SD", 38, "SNARE"), ("CP", 39, "CLAP"), ("CH", 42, "CHH"), ("OH", 46, "OHH"),
       ("LT", 43, "TOMLO"), ("HT", 48, "TOMHI"), ("RS", 37, "RIM"), ("CR", 49, "CRASH"), ("RD", 51, "RIDE")]
X9_NAMES = ["BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CR", "RD"]
X8_NAMES = ["BD", "SD", "LT", "MT", "HT", "LC", "MC", "HC", "RS", "CL", "MA", "CP", "CB", "CH", "OH", "CY"]
X8_OK = {"CR": ("CY",), "RD": ("CY",)}           # the 808 has one cymbal for both
fails = 0


def check(ok, what):
    global fails
    print(f"kit_lane_align: {what:<100} {'ok' if ok else 'FAIL'}")
    fails += not ok


src_x0x = (ROOT / "firmware/src/drums/x0x/drum_x0x.c").read_text()
src_syn = (ROOT / "firmware/src/drums/synth/drum_synth.c").read_text()
src_drm = (ROOT / "firmware/src/drums/drums.c").read_text()

# 1. the reference: drums.c's default lanes are the reference instruments, in order
defs = re.search(r"LANE_DEF_INST\[DRUM_LANES\] = \{(.*?)\};", src_drm, re.S).group(1)
names = [s.strip() for s in defs.replace("\n", " ").split(",")]
want = ["INST_KICK", "INST_SNARE", "INST_CLAP", "INST_CH", "INST_OH", "INST_LT", "INST_HT", "INST_RIM", "INST_CRASH", "INST_RIDE"]
check(names == want, "drums.c LANE_DEF_INST is BD SD CP CH OH LT HT RS CR RD")
notes = [int(x) for x in re.search(r"INST_NOTE\[INST_N\] = \{(.*?)\}", src_drm).group(1).split(",")]
inst = ["INST_KICK", "INST_KICK2", "INST_SNARE", "INST_CLAP", "INST_CH", "INST_OH", "INST_PH", "INST_RIM", "INST_SNARE2",
        "INST_LT", "INST_HT", "INST_CRASH", "INST_RIDE"]
check(all(notes[inst.index(n)] == REF[i][1] for i, n in enumerate(want)), "the reference instruments play GM notes 36 38 39 42 46 43 48 37 49 51")

# 2. synthesised kits: DS_MAP sends each reference note to the same-kind lane of every kit
dm = [(int(a), int(b)) for a, b in re.findall(r"\{(\d+), (-?\d+)\}", re.search(r"DS_MAP\[.*?\] = \{(.*?)\n\};", src_syn, re.S).group(1))]
for code, note, lane in REF:
    check(g.LANES[dm[note - 35][0]] == lane, f"DS_MAP: {code} (note {note}) -> kit lane {lane}")

# 3. each kit's sound on a lane is of the lane's kind
KITS = [(n, k) for n, _, _, k in g.KITS] + [(x[0], x[3]) for x in g.XKITS]


def kind_ok(lane, s, kit):
    tonal = s["wave"] not in (None,) or s["src"] == "chip"
    noisy = s["src"] in ("white", "clap", "chip", "metal", "cym") or s["wave"] == "psnare"
    if lane in ("KICK", "KICK2"):
        return s["hz"] <= 75 and s["wave"] not in (None, "bell") and not s["src"] in ("clap", "metal", "cym")
    if lane in ("SNARE", "SNARE2"):
        return noisy and s["hz"] >= 100 and s["src"] not in ("metal", "cym", "clap")
    if lane == "CLAP":
        return s["wave"] is None and s["src"] in ("clap", "white", "chip")
    if lane in ("CHH", "OHH", "CRASH", "RIDE"):
        return s["src"] in ("metal", "cym", "white", "chip") or (s["wave"] == "fm" and s["hz"] >= 900)
    if lane in ("TOMLO", "TOMHI"):
        return s["wave"] in ("sine", "square", "fm") and 70 <= s["hz"] <= 320
    return True


for kit, k in KITS:
    bad = [lane for lane in g.LANES if not kind_ok(lane, k[lane], kit)]
    check(not bad, f"{kit:<8} every lane is its lane's kind{'' if not bad else ': ' + ' '.join(bad)}")
    check(k["CHH"]["ndec"] < k["OHH"]["ndec"], f"{kit:<8} CHH is shorter than OHH")
    check(k["TOMLO"]["hz"] < k["TOMHI"]["hz"], f"{kit:<8} TOMLO is lower than TOMHI")
    check(k["KICK"]["hz"] < k["TOMLO"]["hz"] <= 320, f"{kit:<8} KICK is below the toms")
    check(k["CRASH"]["ndec"] > k["OHH"]["ndec"], f"{kit:<8} CRASH rings longer than OHH")

# 4. X0X: the reference notes land on the same-kind voice of the 909 and the 808
for tab, nm, ok in (("X9_NOTE", X9_NAMES, None), ("X8_NOTE", X8_NAMES, X8_OK)):
    body = re.search(tab + r"\[.*?\] = \{(.*?)\};", src_x0x, re.S).group(1)
    ent = re.findall(r"XN\((\d+), \d+\)|XN_NONE", body)
    ent = [int(e) if e != "" else None for e in ent]
    for code, note, _ in REF:
        v = ent[note - 35]
        got = nm[v] if v is not None else None
        good = (ok or {}).get(code, (code,))
        check(got in good, f"{tab}: {code} (note {note}) -> {got}")

# 5. sampled kits: the reference notes fall in the GM role of their kind
gm = (ROOT / "tools/gen_samples.py").read_text()
roles = {"BD": "kick", "SD": "snare", "CP": "clap", "CH": "chh", "OH": "ohh", "LT": "tomlo", "HT": "tomhi", "RS": "rim",
         "CR": "crash", "RD": "ride"}
blk = re.search(r"GM_KIT = \{(.*?)\n\}", gm, re.S).group(1)
for code, note, _ in REF:
    r = roles[code]
    m = re.search(r'"' + r + r'": \[(.*?)\]', blk, re.S)
    rng = [(int(a), int(b)) for a, b, _ in re.findall(r"\((\d+), (\d+), (\d+)\)", m.group(1))]
    check(any(lo <= note <= hi for lo, hi in rng), f"GM_KIT: {code} (note {note}) is in role {r}")

print("kit_lane_align:", "all passed" if not fails else f"{fails} failed")
sys.exit(fails)
