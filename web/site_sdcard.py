# SPDX-License-Identifier: GPL-3.0-only
"""The SD card page (webapp/sd-card/): order the SD board from JLCPCB, print its tray, fit it in the FM-1.

A page of the site with its own tab (SD card) and a work-in-progress warning at the top: make_site.py writes it with the
shared header and style. The
text says only what hardware/README.md and the board's files say; the JLCPCB screenshots (assets/sd-card/jlc-*.webp)
were taken on jlcpcb.com's quote page with the board's Gerber zip, up to its sign-in. A photo still to come is a
placeholder (PHOTO_TODO), listed by todo_photos() so the build can say what is missing."""

GH = "https://github.com/w0ts/optimist"
RAW = GH + "/raw/main/hardware/"
HW = GH + "/blob/main/hardware/"
IMG = "../../assets/sd-card/"                      # (the page is webapp/sd-card/index.html; the pictures at the site's root)
BUILDER = "../builder/"
FFC = "https://www.aliexpress.com/item/4000022050815.html"
MBOOT = "https://github.com/jvitkauskas/m-bootloader"
MCARD = "https://github.com/jvitkauskas/m-card"

# the pictures the page shows (assets/sd-card/), with their alt texts: copied to the site by make_site.py
PICTURES = {
    "board-top.webp": "The SD board from above: the microSD slot at the bottom, the connector J3 for the LCD cable at the top",
    "board-bottom.webp": "The SD board from below: the connector J2 for the new cable",
    "tray-with-board.webp": "The printed tray with the SD board in it, seen from above",
    "tray-right-side.webp": "The printed tray from its right side, with its rails and posts",
    "jlc-1-upload.webp": "JLCPCB's quote page: the Add gerber file button",
    "jlc-2-detected.webp": "JLCPCB after the upload: both sides of the board, and the line Detected 2 layer board of 78.2x25.36mm",
    "jlc-3-layers.webp": "JLCPCB's form: Layers 2 selected, Dimensions 25.36 by 78.2",
    "jlc-4-thickness-finish.webp": "JLCPCB's form: PCB Thickness 1.6mm and Surface Finish HASL (with lead) selected, the rest at the defaults",
    "jlc-5-assembly.webp": "JLCPCB's PCB Assembly section switched on: both sides ticked, PCBA Type Standard, Assembly Side Both Sides, and the note that the board grows to 71.36 by 78.2 mm with edge rails",
    "jlc-6-next.webp": "JLCPCB's price panel with the NEXT button",
}
PHOTO_TODO = "PHOTO-TODO"                          # (a placeholder's class: the photo the User will provide)


def picture(name, width=None):
    """one of PICTURES, lazy, scaled to the column"""
    w = f' width="{width}"' if width else ""
    frame = "border:1px solid #d8d8d8;" if name.startswith("jlc-") else ""     # (a screenshot reads as one)
    return (f'<img src="{IMG}{name}" alt="{PICTURES[name]}"{w} loading="lazy" '
            f'style="max-width:100%;height:auto;border-radius:6px;display:block;margin:.6em 0;{frame}">')


def photo_todo(what):
    """a photo still to come: a marked box with what it will show"""
    return (f'<div class="msg {PHOTO_TODO}" style="border:2px dashed currentColor;padding:1.2em;margin:.6em 0;opacity:.7">'
            f'Photo to come: {what}</div>')


def step(title, body):
    return f"<li><b>{title}</b>{body}</li>\n"


WIP = ("<b>Work in progress.</b> This board has not been built and tested yet, and this page is not finished: its parts, "
       "values and steps may still change. Order it only if you are ready to debug it yourself.")

WHAT = ("A small board that adds a microSD slot to the M-VAVE FM-1. <b>No soldering on the FM-1:</b> it plugs in between the "
        "LCD cable and the main board, and sits in a printed tray right of the battery.")

BRINGS = f"""<ul>
<li>Projects, presets, kits and samples on the card.</li>
<li>Samples streamed from the card: 8 voices at once, measured on an FM-1.</li>
<li>Browse the card's samples on the FM-1.</li>
<li>More room for code: your data leaves the FM-1's flash.</li>
<li>Install firmware from the card with <a href="{MBOOT}">m-bootloader</a>.</li>
</ul>"""

NEED = f"""<ul>
<li><b>The board</b>, made and assembled by JLCPCB (below): 5 boards is their smallest order.</li>
<li><b>A 10-pin, 0.5 mm FFC cable</b>, type A, about 80 mm: <a href="{FFC}">this one</a>.</li>
<li><b>A microSD card.</b></li>
<li><b>The tray</b>, 3D printed (below).</li>
<li>A small knife or file, to cut a slot in the case.</li>
</ul>"""

FILES = f"""<ul>
<li><a href="{RAW}sd-board/fab/sd-board-gerbers.zip"><code>sd-board-gerbers.zip</code></a>: the board (upload it as it is, do not unzip).</li>
<li><a href="{RAW}sd-board/fab/bom.csv"><code>bom.csv</code></a>: the parts list (BOM).</li>
<li><a href="{RAW}sd-board/fab/cpl.csv"><code>cpl.csv</code></a>: where each part goes (pick and place, CPL).</li>
<li><a href="{RAW}tray/sd-tray.stl"><code>sd-tray.stl</code></a>: the tray.</li>
</ul>
<p>The KiCad board, the schematic and the tray's source are in <a href="{HW}">hardware/</a>.</p>"""


def order_steps():
    """the JLCPCB order, one step per screen (the screenshots up to the sign-in; the rest needs an account)"""
    return "<ol>\n" + "".join((
        step("Open the quote page.", f' On <a href="https://cart.jlcpcb.com/quote">cart.jlcpcb.com/quote</a>, press <b>Add gerber file</b> and pick <code>sd-board-gerbers.zip</code>.'
             + picture("jlc-1-upload.webp")),
        step("Check what JLCPCB found.", " Both sides of the board appear, and the line under them reads <i>Detected 2 layer board of 78.2x25.36mm</i>."
             + picture("jlc-2-detected.webp")),
        step("Layers: 2.", " JLCPCB fills it in from the files, and the size too. Leave Base Material at FR-4 and the quantity at 5."
             + picture("jlc-3-layers.webp")),
        step("Thickness 1.6 mm, Surface Finish HASL (with lead).", " JLCPCB now picks OSP by default: change it. Leave the colour and everything else at the defaults."
             + picture("jlc-4-thickness-finish.webp")),
        step("Switch on PCB Assembly.", " Then:<ul><li>tick <b>Assemble bottom side</b> too (J2 sits underneath),</li>"
             "<li><b>PCBA Type: Standard</b> (Economic cannot assemble both sides),</li>"
             "<li><b>Assembly Side: Both Sides</b>,</li><li>leave Edge Rails at <i>Added by JLCPCB</i>.</li></ul>"
             "JLCPCB then says the board grows to 71.36 x 78.2 mm: Standard assembly needs both sides longer than 70 mm, so it adds "
             "edge rails. That is expected: remove the rails when the boards arrive."
             + picture("jlc-5-assembly.webp")),
        step("Press NEXT and sign in.", " The steps after this one need a JLCPCB account."
             + picture("jlc-6-next.webp", 320)),
        step("Upload the BOM and the CPL.", " <code>bom.csv</code> as the BOM, <code>cpl.csv</code> as the pick and place file, then <b>Process BOM &amp; CPL</b>."
             + photo_todo("JLCPCB's BOM and CPL upload")),
        step("Check the parts.", " Every line of the BOM should find its part."
             + photo_todo("JLCPCB's parts list, every part found")),
        step("Check the placement.", " In the preview, every part sits on its pads, and <b>J2 is on the bottom side</b>."
             + photo_todo("JLCPCB's placement preview, bottom side with J2")),
        step("Save to cart and order.", " Pay, and JLCPCB makes, assembles and ships the boards.")
    )) + "</ol>\n"


TRAY = f"""<p>Print <a href="{RAW}tray/sd-tray.stl"><code>sd-tray.stl</code></a>. It holds the board right of the battery.</p>
{picture("tray-with-board.webp", 520)}
{picture("tray-right-side.webp", 520)}
{photo_todo("the printed tray")}"""


def fit_steps():
    """fitting the board in the FM-1 (hardware/README.md's steps)"""
    return "<ol>\n" + "".join((
        step("Open the FM-1.", " Battery off first." + photo_todo("the FM-1 opened, the battery unplugged")),
        step("Cut the card slot.", " A 15 x 2.5 mm slot in the case's bottom wall, 81.7 mm from the left wall's inside: the microSD card goes in through it."
             + photo_todo("the slot cut in the case's bottom wall")),
        step("Make room for the cable.", " Cut the plastic under the audio jacks, so the ribbon cable can pass."
             + photo_todo("the plastic cut under the audio jacks")),
        step("Board into the tray, tray into the case.", " Right of the battery." + photo_todo("the tray with the board, in place right of the battery")),
        step("The cables.", " Unplug the LCD cable from the main board and plug it into <b>J3</b>, gold contacts up. Then the new FFC cable from the main board's <b>J12</b> into <b>J2</b>, gold contacts down."
             + photo_todo("the LCD cable in J3 and the new cable from J12 in J2")),
        step("Close it, card in.", " Battery on, close the case, and push the microSD card into its slot." + photo_todo("the FM-1 closed, the card in its slot")),
    )) + "</ol>\n"


FIRMWARE = f"""<p>In the <a href="{BUILDER}">Builder</a>, turn on <b>SD card mode</b>, build, and install your build. If the screen stays black with the
board in, build with <code>LCD_BAUD=1</code> (the screen at 30 MHz).</p>"""

CREDITS = (f'Based on <a href="{MCARD}">m-card</a> by Julius Vitkauskas (CC0). The board, its files and the tray: '
           f'<a href="{HW}">hardware/</a>.')


def todo_photos(html):
    """the photos still to come in a page's HTML (their texts), for the build's note"""
    import re
    return re.findall(rf'class="msg {PHOTO_TODO}"[^>]*>Photo to come: ([^<]*)<', html)


def sdcard_body():
    """the page's <main> content"""
    return "".join((
        f'<section>\n<p class="msg warn">{WIP}</p>\n<h2>The SD card board</h2>\n<p class="lead">{WHAT}</p>\n'
        f'<p style="display:flex;gap:1em;flex-wrap:wrap">{picture("board-top.webp", 260)}{picture("board-bottom.webp", 260)}</p>\n<h3>What it brings to Optimist</h3>\n{BRINGS}\n</section>\n',
        f'<section>\n<h2>What you need</h2>\n{NEED}\n<h3>The files</h3>\n{FILES}\n</section>\n',
        f'<section>\n<h2>Order it from JLCPCB</h2>\n<p>JLCPCB makes the board and solders every part on it.</p>\n{order_steps()}</section>\n',
        f'<section>\n<h2>Print the tray</h2>\n{TRAY}\n</section>\n',
        f'<section>\n<h2>Fit it in the FM-1</h2>\n{fit_steps()}</section>\n',
        f'<section>\n<h2>The firmware</h2>\n{FIRMWARE}\n</section>\n',
        f'<section>\n<h2>Credits</h2>\n<p>{CREDITS}</p>\n</section>\n'))
