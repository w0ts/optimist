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

## Nice touches (if the protocol allows)
- A live level meter per track / master.
- The sequencer grid lights the playing step.

## Constraints
- Still one file, no framework, no external network fetches; keep it fast.
- Every engine's colour and every kind comes from one table, so new engines get a colour in one place.
- Web tests (`web/test_web.mjs`) keep passing; add tests for the theme switch and the colour table.
