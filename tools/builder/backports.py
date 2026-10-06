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
         "system": "System", "engines": "Synth engines", "experimental": "Experimental"}
BITS = {  # switch -> stable BUILD bit (append only)
    "FELUCCA_CHANCE": 64, "FELUCCA_KEYLIT": 65, "FELUCCA_QNT_SEQ": 66, "FELUCCA_KNOB_ACCEL": 67,
    "FELUCCA_LCD_DIRTY": 68, "FELUCCA_UNDO_HISTORY": 69, "FELUCCA_SIZE": 70,
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
        notice = e.get("notice", "")
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
        it = R.Item(key, sw, e.get("title", key)[:60], group, bit, default=int(e.get("default", 0)), desc=desc,
                    provenance=prov, notice=notice, experimental=bool(e.get("experimental", False)))
        R.add_item(it)
        items.append(it)
    return items


ITEMS = load()
