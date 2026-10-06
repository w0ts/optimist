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
    "FELUCCA_QNT_SEQ": "SCL > QNT SEQ (sequenced notes snap)", "FELUCCA_SPRING": "spring reverb (REVERB TYPE)",
    "FELUCCA_BASSPLUS": "BASS+ speaker mode", "FELUCCA_BRIGHT": "screen brightness (MENU > BRIGHT)",
    "FELUCCA_DLY_HALVE": "delay longer than the line halves", "FELUCCA_MOTION": "motion recording (knobs per step)",
    "FELUCCA_ENG_PHYS": "PHYS (physical models)", "FELUCCA_ENG_ACID": "ACID (303 voice + generator)",
    "FELUCCA_REC_MODES": "REC screen dials: mode, length, count-in",
    "FELUCCA_KEYS_FAST": "keys ~1 ms sooner (debounce per column)",
    "FELUCCA_SHED_FADE": "overload: fade a voice, keep bass and lead",
    "FELUCCA_USB_FLOW": "USB MIDI in: flow control, malformed ignored",
    "FELUCCA_ST_STRICT": "stricter flash read-back checks",
    "FELUCCA_MONO_RELEASE": "no stuck note after a VOICE change",
}
PARENT = {"FELUCCA_SPRING": "FX_REVERB", "FELUCCA_DLY_HALVE": "FX_DELAY"}   # options of a registry item
BITS = {  # switch -> stable BUILD bit (append only)
    "FELUCCA_CHANCE": 64, "FELUCCA_KEYLIT": 65, "FELUCCA_QNT_SEQ": 66, "FELUCCA_KNOB_ACCEL": 67,
    "FELUCCA_LCD_DIRTY": 68, "FELUCCA_UNDO_HISTORY": 69, "FELUCCA_SIZE": 70, "FELUCCA_UA_RESAMPLE": 71,
    "FELUCCA_SPRING": 72, "FELUCCA_BASSPLUS": 73, "FELUCCA_BRIGHT": 74, "FELUCCA_DLY_HALVE": 75,
    "FELUCCA_MOTION": 76, "FELUCCA_ENG_PHYS": 77, "FELUCCA_ENG_ACID": 78,
    "FELUCCA_REC_MODES": 105,
    "FELUCCA_KEYS_FAST": 104,
    "FELUCCA_SHED_FADE": 103,
    "FELUCCA_USB_FLOW": 102,
    "FELUCCA_ST_STRICT": 101,
    "FELUCCA_MONO_RELEASE": 100,
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
        desc = e.get("title", "")
        if cost.get("cpu"):
            desc += f" (CPU: {cost['cpu']})"
        if e.get("limits"):
            desc += f". Limits: {e['limits']}"
        parent = PARENT.get(sw)
        if parent:
            group = R.ITEMS[parent].group
        it = R.Item(key, sw, LABEL.get(sw, e.get("title", key)[:48]), group, bit, default=int(e.get("default", 0)),
                    desc=desc, provenance=prov, notice=notice, parent=parent,
                    experimental=bool(e.get("experimental", False) or (x0x and e.get("warning") and not e.get("default"))))
        R.add_item(it)
        items.append(it)
    return items


ITEMS = load()
