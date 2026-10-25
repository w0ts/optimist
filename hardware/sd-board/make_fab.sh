#!/bin/sh
# The SD board's fabrication files (docs/SD-CARD-DESIGN.md): the board from make_board.py, its design-rule check, the
# Gerbers and drill files zipped for JLCPCB, the BOM and placement (make_board.py writes them), a top and bottom render.
#   sh make_fab.sh        (from hardware/sd-board; needs KiCad 10: /Applications/KiCad)
set -eu
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9
"$KP" make_board.py 2>&1 | grep -v "wxApp\|stdpbase" || true
kicad-cli pcb drc --severity-error --exit-code-violations -o fab/drc.rpt sd-board.kicad_pcb   # (errors only: the silk notes stay)
rm -rf fab/gerbers && mkdir -p fab/gerbers
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Paste,F.Silkscreen,B.Silkscreen,Edge.Cuts \
    --no-protel-ext --use-drill-file-origin -o fab/gerbers/ sd-board.kicad_pcb >/dev/null
kicad-cli pcb export drill --format excellon --excellon-separate-th --drill-origin plot -o fab/gerbers/ sd-board.kicad_pcb >/dev/null
(cd fab/gerbers && zip -q -r ../sd-board-gerbers.zip .)
kicad-cli pcb render --side top --width 1200 --height 1500 --quality high --zoom 0.9 -o render-top.png sd-board.kicad_pcb >/dev/null
kicad-cli pcb render --side bottom --width 1200 --height 1500 --zoom 0.9 -o render-bottom.png sd-board.kicad_pcb >/dev/null
echo "fab/sd-board-gerbers.zip fab/bom.csv fab/cpl.csv render-top.png render-bottom.png"
