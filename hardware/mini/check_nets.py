# SPDX-License-Identifier: GPL-3.0-only
"""The schematic and the board say the same: every (part, pin) on the same net in both. The schematic's netlist comes from
kicad-cli (make_fab.sh), the board's from its pads (pcbnew). Exit 1 on any difference.

  python3.9 check_nets.py NETLIST.xml sd-board.kicad_pcb
"""
import sys
import xml.etree.ElementTree as ET

import pcbnew

sch = {}
for n in ET.parse(sys.argv[1]).getroot().iter("net"):
    name = n.get("name").lstrip("/")
    if name.startswith(("unconnected-", "Net-(")):
        continue
    for node in n.iter("node"):
        if not node.get("ref").startswith("#"):
            sch[(node.get("ref"), node.get("pin"))] = name

pcb = {}
for fp in pcbnew.LoadBoard(sys.argv[2]).GetFootprints():
    for p in fp.Pads():
        if p.GetNetname() and p.GetNumber():
            pcb[(fp.GetReference(), p.GetNumber())] = p.GetNetname()

shell = {k: v for k, v in pcb.items() if k[0] == "J1" and k[1] in ("10", "11", "12", "13")}   # (the board's shell pads:
for k in shell:                                                                                # the schematic's SH)
    del pcb[k]
if set(shell.values()) - {sch.get(("J1", "SH"))}:
    print("the shell pads", shell, "vs the schematic's SH", sch.get(("J1", "SH")))
    sys.exit(1)
sch.pop(("J1", "SH"), None)
for ref in ("J2", "J3"):                                                  # (the FPC sockets' tabs: the schematic's MP)
    tabs = {k: pcb.pop(k) for k in [(ref, "11"), (ref, "12")] if k in pcb}
    if tabs and set(tabs.values()) != {sch.pop((ref, "MP"), None)}:
        print("the FPC tabs", tabs, "vs the schematic's MP")
        sys.exit(1)
bad = sorted(k for k in set(sch) | set(pcb) if sch.get(k) != pcb.get(k))
for k in bad:
    print(f"{k[0]}.{k[1]}: schematic {sch.get(k)}, board {pcb.get(k)}")
print(f"nets: {len(pcb)} pins on the board, {len(sch)} in the schematic, {len(bad)} differ")
sys.exit(1 if bad else 0)
