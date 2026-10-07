# Web editor design brief (user decisions, 2026-10-06)

The web editor (`web/editor.html`, one file, no build step, Chrome/Edge) gets a design pass after the kit-editor
work (`feat/web-kits`) merges.

## Goals
- **More user friendly, easier to understand:** clear sections, plain-language labels, short hints and a tooltip per
  control; values in real units (ms, Hz, %, st, dB) instead of raw numbers; consistent knobs (drag / wheel,
  Shift = fine, double-click = reset, arrow keys, aria labels).
- **Less boring than plain black and white.**

## Colour system
- **General theme:** match the FM-1 colour editions, the same palettes as the emulator's themes
  (Classic, Black, Lilac, Orange, Mint, Cream, Blue), chosen in the editor and remembered per browser.
- **A colour per engine, used everywhere** the engine appears: preset lists, the track header, the Sound tab,
  the Mix tab, the sequencer, the kit editor's source tags. Drum source kinds get their own colours too
  (drum synth / sampled / X0X engine / your sample / your kit).
- **Status colours with meaning:** red = error or clipping/too-high value (e.g. a level or drive that distorts),
  amber = notice / warning (EXPERIMENTAL, X0X notice, MISSING), green = OK / saved / in sync.
  A value control turns red when its value distorts, where the firmware or editor can tell.
- No decorative per-track colours: a track shows its engine colour.
- Light and dark variants of each theme; keep contrast readable (WCAG AA for text).

## Same colours on the device (user, 2026-10-06)
- The engine colours (and the drum-kind colours) are used in the **device UI too**: the PRESETS browser, the
  track tiles / headers, the kit and SOURCE pages, VIEW ALL. One source of truth: a colour table (e.g.
  `tools/colors.json`) generated into a firmware header (RGB565) and into the editor, so device and editor always
  match. Measure the flash cost (a few bytes per engine).
- **Drop SLOOP's per-track colours** (user: "not used anywhere else than on the main screen, a perfect example of
  useless UX noise"). A track takes the colour of the engine it plays (the drum track: its kit's kind colour), on
  the device and in the editor. Colour must always carry meaning (engine, kind, status), never decoration.
- **Align the two UIs:** the same names, groupings, colours and order of things on the device and in the editor,
  so learning one teaches the other.
- The device's status colours follow the same meaning: red = clipping / error, amber = notice (MISSING, EXPERIMENTAL),
  green = OK / saved.

## Colour language (device + editor, 2026-10-07)
Colour carries meaning, never decoration. The same rules on the FM-1's screen and in the web editor:
- **Engine colour** wherever a track, an engine or a preset is shown: the track tiles and headers (HOME / TRACKS,
  the editor's mixer strips), the PRESETS browser (a mark per row), the curves, gauges and dials of a track's pages,
  the lit row of VIEW ALL, the footer's engine name, the editor's preset lists and Sound tab.
- **Drum-kind colour** for the drum track and its sounds: the drum track takes its kit's kind (synthesised,
  sampled, X0X, your kit); a sound's pages (SOUND / SOURCE) and the editor's lane tiles take its source's kind
  (your sample: `usr`).
- **Status colours:** red = error, clipping, recording or erasing (SAVE ERROR, MEM FULL, the REC light); amber = a
  notice (STOP FIRST, EMPTY SLOT, MISSING, AGAIN to confirm, RAM only, EXPERIMENTAL, a MOTION mark); green = done or
  running (SAVED, LOADED, STORED, playing). The device picks a message's colour from its words (ui.c `msg_status`).
- **Neutral for everything else:** the device's palette steps (HOME-hold COLOR: GREEN, AMBER, CYAN, RED, MONO) and
  the studio screens' greys; the editor's theme (FM-1 editions). Icons, labels, values and the knobs' own dials are
  neutral; **white** is what you touch (the knob being turned, the cursor, the selected item).
- **No colour per track number and none per knob:** SLOOP's blue / green / yellow / orange for tracks 1..4 and
  KNOB 1..4 are gone. Song sections A..D are neutral too (green while one plays).
- **One table:** `tools/colors.json` (engines, drum kinds, status, other). The firmware gets it as RGB565 at build
  time (`tools/gen_colors.py` -> `build/gen/felucca_colors.h`: `COL_ENG_<NAME>`, `COL_KIND_<KIND>`, `COL_ST_*`;
  `firmware/src/ui_colors.c` maps a track to its colour); the editor holds the same table (`COLORS`, checked equal by
  `web/test_web.mjs`). A new engine gets its colour in one place.

## Nice touches (if the protocol allows)
- A live level meter per track / master.
- The sequencer grid lights the playing step.

## Constraints
- Still one file, no framework, no external network fetches; keep it fast.
- Every engine's colour and every kind comes from one table, so new engines get a colour in one place.
- Web tests (`web/test_web.mjs`) keep passing; add tests for the theme switch and the colour table.
