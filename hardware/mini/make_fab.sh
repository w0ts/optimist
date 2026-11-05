#!/bin/sh
# The short SD board's fabrication files (DESIGN.md): the board from make_board.py, its design-rule check, the
# Gerbers and drill files zipped for JLCPCB, the BOM and placement (make_board.py writes them), a top and bottom render.
#   sh make_fab.sh        (from hardware/mini; needs KiCad 10: /Applications/KiCad)
set -eu
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9
"$KP" make_board.py 2>&1 | grep -v "wxApp\|stdpbase" || true
"$KP" make_schematic.py
kicad-cli pcb drc --severity-error --exit-code-violations -o fab/drc.rpt sd-board.kicad_pcb   # (errors only: the silk notes stay)
kicad-cli sch erc --severity-error --exit-code-violations -o fab/erc.rpt sd-board.kicad_sch >/dev/null 2>&1 || {
    grep -E "^\[" fab/erc.rpt | grep -v "lib_symbol_issues\|footprint_link_issues" && exit 1; }   # (a fresh KiCad
                                                                    # without its global library tables: only those notes)
kicad-cli sch export netlist --format kicadxml -o fab/sd-board.net.xml sd-board.kicad_sch >/dev/null 2>&1
"$KP" check_nets.py fab/sd-board.net.xml sd-board.kicad_pcb 2>&1 | grep -v "wxApp\|stdpbase"
kicad-cli sch export pdf -o sd-board-schematic.pdf sd-board.kicad_sch >/dev/null 2>&1
rm -rf fab/gerbers && mkdir -p fab/gerbers
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,Edge.Cuts \
    --no-protel-ext --use-drill-file-origin -o fab/gerbers/ sd-board.kicad_pcb >/dev/null
kicad-cli pcb export drill --format excellon --excellon-separate-th --drill-origin plot -o fab/gerbers/ sd-board.kicad_pcb >/dev/null
(cd fab/gerbers && zip -q -r ../sd-board-gerbers.zip .)
kicad-cli pcb render --side top --width 1400 --height 1300 --quality high --zoom 0.8 -o render-top.png sd-board.kicad_pcb >/dev/null
kicad-cli pcb render --side bottom --width 1400 --height 1300 --zoom 0.8 -o render-bottom.png sd-board.kicad_pcb >/dev/null
# a solid mockup to 3D-print and a 1:1 paper template, to check the fit in the FM-1
"$KP" make_mockup.py 2>&1 | grep -v "wxApp\|stdpbase"
kicad-cli pcb export pdf --layers Edge.Cuts,F.Fab,F.SilkS --mode-single --include-border-title -o template-1to1.pdf \
    sd-board.kicad_pcb >/dev/null
echo "fab/sd-board-gerbers.zip fab/bom.csv fab/cpl.csv render-top.png render-bottom.png mockup-printable.stl template-1to1.pdf"
