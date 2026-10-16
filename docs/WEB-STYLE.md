# The web style

The installer, the editor and the builder share one look: `web/style/optimist.css`, tokens and components only.
`web/make_site.py` puts it into each page where the page says `/*OPTIMIST-CSS*/` (inside a `<style>`), and the shared
header where it says `<!--OPTIMIST-HEADER-->`, so every page stays one standalone file that works offline. A page's own
layout stays in the page. `web/style/gallery.html` shows every component (open it from a checkout; it is not on the
site).

This is the web editor's look (the SLOOP 2.4 look: the FM-1's screen on a big display), taken from the editor's own CSS:
black in the dark scheme and warm grey in the light one, square corners, no shadows, no rounded cards, the device's
Terminus pixel font at 16 px, buttons in lower case on a flat fill, the four track colours, status colours only for
status. Light and dark follow the system (`prefers-color-scheme`); there is no switch.

![The component gallery, dark](../assets/screens/web-style-dark.webp)
![The component gallery, light](../assets/screens/web-style-light.webp)

## Tokens

The editor's own names come first; the `o-` components use the shared names, which point at them.

| Token | Use | Dark | Light |
| --- | --- | --- | --- |
| `--bg` (`--surface-0`) | the page, fields | `#000000` | `#f4f4f2` |
| `--s1` (`--surface-1`) | a card, the hover of a tab | 6 % of `--fg` into `--bg` | the same |
| `--s2` (`--surface-2`) | a control, a card's head | 14 % | 10 % |
| `--s3` (`--surface-3`, `--line-strong`) | hover, pressed, an empty bar, a field's border | 30 % | 38 % |
| `--panel` | a popover | `#0b0b0e` | `#ffffff` |
| `--line` | separators | `#26262c` | `#c3c3c9` |
| `--fg`, `--fg2` (`--text`), `--dim` (`--text-dim`), `--mute` | text, body text, secondary, the quietest | `#f2f2f2`, 82 %, `#8a8a92`, `--s3` | `#14141a`, 82 %, `#5e5e66`, `--dim` |
| `--trk` (`--accent`), `--on` (`--on-accent`) | the current track colour: the primary action, the current thing; text on a fill | `--t1`, `#000` | `--t1`, `#fff` |
| `--ok`, `--warn`, `--err` | done; experimental and on, nearly full; stops a build, over budget | `#1ecc70` `#ffc618` `#ff4a50` | `#11682b` `#825000` `#a82229` |
| `--t1` ... `--t4` | the four tracks (the editor's and the FM-1's): blue, green, yellow, orange | `#287cff` `#1ecc70` `#ffc618` `#ff621a` | `#1a52b8` `#11682b` `#825000` `#ab3207` |

The scale: every text size is 16 px (Terminus is drawn at 16 and 32 px; `--fs-6` is 32), spacing `--sp-1` ... `--sp-6`
(4, 8, 12, 16, 24, 32 px), `--r` 0, `--ctl-h` 30 px controls (36 px for a finger), `--head-h` 41 px (the header).
Fonts: `--font-ui` and `--font-mono` are both `"SLOOP Terminus"` (inlined by `make_site.py` from `web/style/terminus.ttf`,
SIL OFL 1.1, `TERMINUS-LICENSE.txt`, credited to SLOOP 2.4 by isod89) with the system's monospace as the fallback.

## Components

- **Header** `.o-head`: the editor's menu bar. The dinghy and OPTIMIST (to the site's root), then the three apps as
  tabs (`.o-nav`, a thin `.o-sep` between them, the current one `aria-current="page"` shown by its background, like the
  editor's open tab). No section titles and no coloured line under it. `make_site.py shared_header(app)` writes it.
- **Page** `.o-page`: one column, `--page-w` wide at most, 16 px gutters.
- **Buttons** `.o-btn` (or any `button` inside `<body class="o-ui">`): default, `.primary` (one per view), `.quiet`,
  `.sm`, `.big`. **Fields** `.o-select`, `.o-input`, `.o-check`, `.o-field` (a label over its control).
- **Info** `.o-info`: the round (i) that opens a popover. **Popover** `.o-pop` on a `[popover]` element.
  **Tooltip** `data-tip="..."`: one line, on hover and keyboard focus.
- **Tabs** `.o-tabs` (`aria-selected="true"` or `.on`), **tags** `.o-tag` (`.outline .accent .ok .warn .err`),
  **chips** `.o-chip` (`.accent .err`).
- **Card** `.o-panel` (the editor's `.group`: a raised surface and a thin line) with `.o-panel-h` (the control fill with
  a 3 px bar of `--c`, the track colour, under it; the editor's strip head) and `.o-panel-b`.
- **Message** `.o-msg` (`.err .warn .ok`, plain): one line, a short head in `<b>`, details in `.sub`.
  **Disclosure** `.o-details`.
- **Gauge** `.o-gauge` (`data-state="ok | tight | over"`): `.gl` label, `.gn` used / limit with `.go` the overflow,
  `.gb` the bar with `.gf` the fill, `.gx` the overflow and `.gc` the limit's mark. **Progress** `.o-progress`.
- **Toast** `.o-toast` inside one `.o-toasts`: says what just happened, then goes.

## Dos and don'ts

- Red is for what stops a build or an install (an error, an overflow); amber for what is experimental and on, or
  nearly full. Everything else is grey: credits, provenance, "tested in the emulator only" go in an (i) popover or a
  closed "Notes (n)", never in a coloured bar, and the same text is said once.
- One primary button per view. The accent marks the current thing and what is fine; it is not decoration.
- The track colours mean the tracks. Do not use them for states. A hidden control stays hidden (`[hidden]` wins over a button's display inside `.o-ui`).
- Dense, not cramped: 16 px Terminus, 4 px steps; numbers in `font-variant-numeric: tabular-nums`. Do not round a corner or add a shadow.
- Tokens, never raw colours, in a page's own CSS. A new colour starts as a token here, with its light value.
- No theme switch and no `data-theme`: the system decides.
- Phones: no sideways scroll, 16 px gutters, 16 px text everywhere (no zoom on focus), larger hit areas under
  `pointer: coarse`.

## A mounted module's layout

A module that can be mounted in another page (the installer, `web/installer/installer.css`, scoped to `.oi`) keeps its
layout in its own file and takes every colour, size and font from the tokens above, never a value of its own, so it has
the look of whatever page holds it (the standalone installer, or a tab of the editor). `web/test_web.mjs` checks that
the installer's CSS has no colour literal.
