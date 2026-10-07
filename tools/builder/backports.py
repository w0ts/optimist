# SPDX-License-Identifier: GPL-3.0-only
"""Backported features (tools/backports.json, kept by the backport branches) as registry items.

Each entry of backports.json is one FELUCCA_* switch with its source (repo, commit, author, licence), a
notice and "warning" (X0X-derived or experimental). Here each becomes an Item with that provenance; an item
from X0X (charlesvestal/fm1-x0x) always carries a notice the menu shows as a NOTICE when it is selected.
Bits are stable: a switch keeps the bit listed in BITS (append new ones; never reuse)."""
import json
from pathlib import Path

import registry as R

SRC = Path(__file__).resolve().parents[2] / "tools" / "backports.json"
GROUP = {"sequencer": "Sequencer", "ui": "UI", "fx": "FX", "drums": "Drums", "midi": "MIDI & USB",
         "system": "System", "engines": "Synth engines", "engine": "Synth engines", "experimental": "Experimental"}
LABEL = {  # short menu labels (the title stays in the details)
    "FELUCCA_CHANCE": "per-step chance (STEP 2 PROB)", "FELUCCA_KEYLIT": "keys light the notes played",
    "FELUCCA_QNT_SEQ": "SCL > QNT SEQ (sequenced notes snap)", "FELUCCA_SPRING": "reverb: SPRING",
    "FELUCCA_BASSPLUS": "BASS+ speaker mode", "FELUCCA_BRIGHT": "screen brightness (MENU > BRIGHT)",
    "FELUCCA_DLY_HALVE": "long delay times halve to stay on the beat", "FELUCCA_MOTION": "motion recording (knobs per step)",
    "FELUCCA_ENG_PHYS": "PHYS (physical models)", "FELUCCA_ENG_ACID": "ACID (TB-303 bass + line generator)",
    "FELUCCA_ENG_CZ": "CZ (CZ-1 style tones)",
    "FELUCCA_KNOB_ONEREST": "knobs: one rest state a detent (no double clicks)",
    "FELUCCA_GLIDE": "mixer glides ~10 ms (no zipper)",
    "FELUCCA_LIGHTS": "menu LIGHTS / KEYS / NOTES (play in the dark)",
    "FELUCCA_REC_MODES": "REC screen dials: mode, length, count-in",
    "FELUCCA_KEYS_FAST": "keys ~1 ms sooner (debounce per column)",
    "FELUCCA_SHED_FADE": "overload: fade a voice, keep bass and lead",
    "FELUCCA_USB_FLOW": "USB MIDI in: flow control, malformed ignored",
    "FELUCCA_ST_STRICT": "stricter checks of saved data when read back",
    "FELUCCA_BK_CHECK": "restore: an object refused unless it would load",
    "FELUCCA_TRS_NOISE": "TRS MIDI in: line noise no longer deafens the jack",
    "FELUCCA_MONO_RELEASE": "no stuck note after a VOICE change",
    "FELUCCA_LAYER_QUIET": "knobs quiet as a layer button is let go",
    "FELUCCA_BPM_LOCK": "BPM LOCK: SELECT is the tempo only with GLO",
    "FELUCCA_DIV_ORDER": "divisions in length order (1/8 8T 1/16 ...)",
    "FELUCCA_PUNCH_LATCH": "punch LATCH: FX + key latches its effect",
    "FELUCCA_MOTION_MARK": "mark the parameters motion recording moves",
    "FELUCCA_UP_FM6": "FM6 user presets keep their voice",
    "FELUCCA_SL24_SAFE": "SLOOP 2.4's data kept safe (never erased)",
    "FELUCCA_SL24_XSTEP": "step extras storage (nudge, locks, fills; 2.4)",
    "FELUCCA_SL24_IMPORT": "import SLOOP 2.4 projects (LOAD twice)",
}
DESC = {  # what each switch does for the user (plain words; sizes from tools/builder/costs.json, details in backports.json)
    "FELUCCA_CHANCE": "Gives each synth step a chance to play (SEQ > STEP 2, KNOB 2: 0 to 100 % in 5 % steps); a step "
                      "that fails plays as a rest. About 0.5 KB of flash; nothing changes until a step's chance is turned "
                      "down. Synth tracks only; the web editor does not show it yet. Off: every step plays (chances "
                      "stay saved).",
    "FELUCCA_KEYLIT": "Lights the keys of the notes the selected track plays (its steps, the arpeggiator, held notes), "
                      "so you see the pattern on the keyboard. About 0.2 KB of flash, LEDs only; with LIGHTS, MENU > "
                      "NOTES turns it on and off at run time.",
    "FELUCCA_QNT_SEQ": "SCL > QNT SEQ: the keys and the sequenced notes snap to the scale as they play, so a pattern "
                       "follows a change of ROOT or SCALE; the steps keep their notes as written. About 0.3 KB of "
                       "flash; not on GM KIT or SLICE parts. A build without it plays a project's QNT SEQ as ALL.",
    "FELUCCA_SPRING": "A spring-tank reverb, one of the reverb's algorithms: the chirp and drip of a guitar amp's "
                      "spring (SPRNG on FX > REVERB > TYPE when two or more are ticked). Its output is mono, and it "
                      "is a little lighter on CPU than ROOM (emulator). Beside ROOM: about 1.9 KB of flash and "
                      "2.3 KB of RAM; ticked alone it is the only reverb.",
    "FELUCCA_BASSPLUS": "A third MENU > LOWCUT setting (OFF / LOWCUT / BASS+) for the FM-1's small speaker: it adds "
                        "harmonics of the bass below ~150 Hz, which the speaker can play, and raises the low cut to "
                        "~220 Hz. About 0.5 KB of flash and 0.35 KB of RAM; CPU only while BASS+ is on.",
    "FELUCCA_BRIGHT": "MENU > BRIGHT: the screen backlight in 8 levels (software PWM, saved with the settings). "
                      "About 36 B of flash; X0X reports it working on a real FM-1 but it is not tried here on "
                      "hardware. Off: always full brightness.",
    "FELUCCA_DLY_HALVE": "A delay time longer than the delay line is halved (so it stays on the beat) instead of being "
                         "cut off; it matters with the shorter delay lengths (with 0.74 s, 1/4 below 81 BPM plays as "
                         "1/8). Costs 64 B of flash; off: the time is cut at the line's length.",
    "FELUCCA_MOTION": "Motion recording: while recording, knob turns are stored per step and replayed on every pass "
                      "(SEQ > MOTION: play on / off per track, clear). 64 events for the four tracks together; not "
                      "editable from the web editor and edits are not undoable. About 3.4 KB of flash, 0.5 KB of "
                      "RAM and 1.4 KB of pool.",
    "FELUCCA_ENG_PHYS": "Physical-modelling engine (engine 11): modal, string, membrane and sympathetic-string models, "
                        "3 voices per part. Heavy: about 9.8 KB of flash and 38.7 KB of pool, and 3 voices of its "
                        "heaviest preset take about 17 % of the audio budget at 312 MHz (at 96 MHz that was too much "
                        "and voices were shed).",
    "FELUCCA_ENG_ACID": "X0X's TB-303-style bass voice (one monophonic voice per part, engine 12) with the TB-3PO line "
                        "generator. Experimental: floating-point DSP (about 14.4 KB of flash, 1.7 KB of RAM) that ran on "
                        "a real FM-1 in X0X but has not been tried here; no pitch bend or TUNE on the 303; 4 presets.",
    "FELUCCA_ENG_CZ": "Casio CZ-1 style engine from Melodee 0.11 (engine 13): two lines with 8-step envelopes, 8 "
                      "built-in tones (TONE) changed by the EDIT values; no envelope editing, banks or SysEx yet. About "
                      "7 KB of flash and 3.1 KB of RAM; similar CPU to FM6 (emulator).",
    "FELUCCA_MONO_RELEASE": "Fixes a stuck note when you let a key go just after a VOICE change (MONO stack). No "
                            "cost.",
    "FELUCCA_ST_STRICT": "Stricter checks of what is read back from flash (settings, projects, presets, kits) and a "
                         "compare after each save, so damaged data is refused instead of loaded. About 0.1 KB of "
                         "flash.",
    "FELUCCA_USB_FLOW": "USB MIDI in: asks the computer to wait when the device is busy instead of dropping messages, "
                        "and ignores malformed ones. About 0.3 KB of flash; the TRS jack cannot be held back.",
    "FELUCCA_SHED_FADE": "Under overload, fades out one voice at a time and never the bass or the lead, instead of "
                         "cutting voices; the sound changes only under overload. About 0.1 KB of flash.",
    "FELUCCA_KEYS_FAST": "The keys respond about 1 ms sooner (each key is debounced as its column is read); measured "
                         "on the host: mean press latency 3.3 to 1.7 ms. About 32 B of flash.",
    "FELUCCA_REC_MODES": "REC screen dials: MODE (free / tempo), LENGTH (1 / 2 / 4 bars) and START (note / 4-3-2-1 "
                         "count-in). The count-in runs on the internal clock only and in 4/4. About 1.2 KB of flash; "
                         "off: free recording started by a note.",
    "FELUCCA_LIGHTS": "Lights for playing in the dark: MENU LIGHTS (every button glows OFF / LOW / MID / HIGH), KEYS (C "
                      "or white keys glow) and NOTES (the note lights of KEYLIT on / off at run time). About 1.2 KB of "
                      "flash.",
    "FELUCCA_GLIDE": "Part level, pan and sends, the master volume and the drum track's level, pan and sends glide over "
                     "~10 ms instead of jumping, which removes the zipper noise of a moving knob. About 1.3 KB of "
                     "flash, 0.3 KB of RAM and 0.7 KB of fast RAM code; the sound changes only while a gain moves.",
    "FELUCCA_KNOB_ONEREST": "Counts a knob click as one full encoder cycle, so a pause in the middle of a click no "
                            "longer doubles the clicks after it. About 0.3 KB of flash; assumes the FM-1's full-cycle "
                            "detents.",
    "FELUCCA_TRS_NOISE": "TRS MIDI in: a noise byte (FD) arriving at the wrong moment no longer blocks the jack until "
                         "the next restart. About 16 B of flash.",
    "FELUCCA_BK_CHECK": "Restoring a backup writes each stored object only if the firmware would load it (otherwise "
                        "it is refused), so a bad backup cannot leave unloadable data. About 0.4 KB of flash.",
    "FELUCCA_LAYER_QUIET": "Knob turns while a layer button is being let go (and for 250 ms after a used layer "
                           "closes) are ignored, so releasing a layer does not nudge a parameter. About 8 B of flash.",
    "FELUCCA_BPM_LOCK": "SELECT changes the tempo only while GLO is held, so the tempo cannot slip live; GLO > GLOBAL's "
                        "BPM knob and tap tempo still work. No menu item: the build switch is the choice. About 64 B "
                        "of flash.",
    "FELUCCA_DIV_ORDER": "Lists note divisions in length order (1/4 1/8 8T 1/16 16T 1/32) on ARP RATE, SEQ DIV, DELAY "
                         "TIME and SLICER RATE; stored values are unchanged. About 0.2 KB of flash.",
    "FELUCCA_PUNCH_LATCH": "PUNCH: FX + a key latches its effect so you can let go; the same key or FX + OCT- turns it "
                           "off, another key switches to its effect, and FX stays lit while one plays. About 80 B of "
                           "flash.",
    "FELUCCA_MOTION_MARK": "With motion recording: a small square in the track colour marks the parameter cards that "
                           "the track's motion moves (page and VIEW ALL). About 0.3 KB of flash.",
    "FELUCCA_UP_FM6": "FM6 user presets keep their whole voice (operator edits included) instead of only the VOICE "
                      "number. About 0.7 KB of flash plus a 3.6 KB store in flash; a preset written from the web "
                      "editor drops its kept voice.",
}
DESC["FELUCCA_SL24_SAFE"] = (
    "Coming from SLOOP 2.4: Optimist never erases or writes over what 2.4 left that it cannot read: the four project "
    "slots and the autosave (shown as SLOOP 2.4 on the PROJECT page and in the editor, not EMPTY), 2.4's FM6 bank and "
    "a user sample longer than ours (USR3, USR4); 2.4's settings word and user presets are read right. Off, the first "
    "start erases 2.4's projects. About 0.7 KB of flash.")
DESC["FELUCCA_SL24_XSTEP"] = (
    "Keeps SLOOP 2.4's per-step extras with every project: each step's nudge (micro timing), up to 24 parameter locks "
    "a track and each step's fill condition, in 2.4's own layout, so 2.4 projects carry them over. Storage only: the "
    "sequencer features that play them come with their own switches. A section with none costs nothing. About 1.6 KB of flash, "
    "0.9 KB of RAM and 4.2 KB of pool.")
DESC["FELUCCA_SL24_IMPORT"] = (
    "A SLOOP 2.4 project left in a slot shows as SLOOP 2.4 on the PROJECT page; LOAD it twice and it becomes the "
    "working project (values, engines, drum kits, steps; nudges, locks and fills with the step extras storage), "
    "then SAVE puts it in a section. 2.4's original stays in flash. Lost: the track filter, strum and voice-leading "
    "values, FM6 patches (the closest factory voice instead) and the USR kits.")
PARENT = {"FELUCCA_SPRING": "FX_REVERB", "FELUCCA_DLY_HALVE": "FX_DELAY", "FELUCCA_PUNCH_LATCH": "FX_PUNCH",
          "FELUCCA_MOTION_MARK": "MOTION", "FELUCCA_UP_FM6": "ENG_FM6"}   # options of a registry item
AFTER = {"FELUCCA_SPRING": "REV_FDN8"}   # an option's place among its parent's (SPRING beside the other reverb algorithms)
BITS = {  # switch -> stable BUILD bit (append only)
    "FELUCCA_CHANCE": 64, "FELUCCA_KEYLIT": 65, "FELUCCA_QNT_SEQ": 66, "FELUCCA_KNOB_ACCEL": 67,
    "FELUCCA_LCD_DIRTY": 68, "FELUCCA_UNDO_HISTORY": 69, "FELUCCA_SIZE": 70, "FELUCCA_UA_RESAMPLE": 71,
    "FELUCCA_SPRING": 72, "FELUCCA_BASSPLUS": 73, "FELUCCA_BRIGHT": 74, "FELUCCA_DLY_HALVE": 75,
    "FELUCCA_MOTION": 76, "FELUCCA_ENG_PHYS": 77, "FELUCCA_ENG_ACID": 78,
    "FELUCCA_BK_CHECK": 110,
    "FELUCCA_TRS_NOISE": 109,
    "FELUCCA_KNOB_ONEREST": 108,
    "FELUCCA_GLIDE": 107,
    "FELUCCA_LIGHTS": 106,
    "FELUCCA_REC_MODES": 105,
    "FELUCCA_KEYS_FAST": 104,
    "FELUCCA_SHED_FADE": 103,
    "FELUCCA_USB_FLOW": 102,
    "FELUCCA_ST_STRICT": 101,
    "FELUCCA_MONO_RELEASE": 100,
    "FELUCCA_LAYER_QUIET": 130,
    "FELUCCA_BPM_LOCK": 132,
    "FELUCCA_DIV_ORDER": 133,
    "FELUCCA_PUNCH_LATCH": 131,
    "FELUCCA_MOTION_MARK": 134,
    "FELUCCA_UP_FM6": 135,
    "FELUCCA_ENG_CZ": 136,
    "FELUCCA_SL24_SAFE": 161,
    "FELUCCA_SL24_XSTEP": 162,
    "FELUCCA_SL24_IMPORT": 163,                           # (SLOOP 2.4 phase 0: 161..164; 160 is feat/pr45-small's)
}
NEXT_FREE = 80                                          # (switches not in BITS yet: from here, by name)


def is_x0x(src):
    return "fm1-x0x" in json.dumps(src).lower() or "charles vestal" in json.dumps(src).lower()


def load(path=SRC):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return []
    items = []
    extra = sorted(e["switch"] for e in data.get("items", []) if e["switch"] not in BITS)
    for e in data.get("items", []):
        sw = e["switch"]
        key = sw.replace("FELUCCA_", "")
        if key in R.ITEMS or key in R.FORBIDDEN or sw in [it.flag for it in R.ITEMS.values()]:
            continue
        src = e.get("source", {})
        x0x = is_x0x(src) or is_x0x(e.get("also", {}))
        prov = R.Provenance(project=src.get("repo", "").replace("https://github.com/", ""),
                            author=src.get("author", ""), licence=src.get("licence", ""),
                            commit=src.get("commit", ""), url=src.get("repo", ""))
        notice = e.get("notice", "") if (x0x or e.get("warning")) else ""   # (provenance shows for the rest)
        if x0x and not notice:
            notice = f"Ported from X0X by Charles Vestal (GPL-3.0). Experimental in this firmware."
        if (e.get("warning") or x0x) and notice and not notice.upper().startswith(("WARNING", "NOTICE")):
            notice = ("WARNING: " if e.get("warning") else "") + notice
        bit = BITS.get(sw, NEXT_FREE + extra.index(sw) if sw in extra else NEXT_FREE)
        group = GROUP.get(e.get("group", ""), "System")
        if group not in R.GROUPS:
            R.GROUPS.insert(R.GROUPS.index("UI"), group)
        cost = e.get("cost", {})
        desc = DESC.get(sw) or e.get("title", "")
        if sw not in DESC and cost.get("cpu"):
            desc += f" (CPU: {cost['cpu']})"
        if sw not in DESC and e.get("limits"):
            desc += f". Limits: {e['limits']}"
        parent = PARENT.get(sw)
        if parent:
            group = R.ITEMS[parent].group
        it = R.Item(key, sw, LABEL.get(sw, e.get("title", key)[:48]), group, bit, default=int(e.get("default", 0)),
                    desc=desc, provenance=prov, notice=notice, parent=parent,
                    experimental=bool(e.get("experimental", False) or (x0x and e.get("warning") and not e.get("default"))))
        R.add_item(it)
        if sw in AFTER and parent and AFTER[sw] in R.ITEMS[parent].children:
            ch = R.ITEMS[parent].children
            ch.remove(key)
            ch.insert(ch.index(AFTER[sw]) + 1, key)
        items.append(it)
    return items


ITEMS = load()
