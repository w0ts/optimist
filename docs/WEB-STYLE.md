# The web style

The installer, the editor and the builder share one look: `web/style/optimist.css`, tokens and components only.
`web/make_site.py` puts it into each page where the page says `/*OPTIMIST-CSS*/` (inside a `<style>`), and the shared
header where it says `<!--OPTIMIST-HEADER-->`, so every page stays one standalone file that works offline. A page's own
layout stays in the page. `web/style/gallery.html` shows every component (open it from a checkout; it is not on the
site).

The look is the web editor's (the SLOOP 2.4 look): black in the dark scheme and warm grey in the light one, square corners,
no shadows, buttons in lower case on a flat fill, the system's sans-serif at 13 to 24 px, and **one colour**: the blue
`--t1`. Light and dark follow the system (`prefers-color-scheme`); there is no switch.

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
| `--t1` = `--t2` = `--t3` = `--t4` = `--trk` (`--accent`) | the one colour: the primary action, the current thing, an OK (`--ok`) | `#287cff` | `#1a52b8` |
| `--on` (`--on-accent`) | text on a filled colour | `#000` | `#fff` |
| `--warn`, `--err` | experimental and on, nearly full; stops a build, over budget | `#ffc618` `#ff4a50` | `#825000` `#a82229` |

`--t2` to `--t4` and `--ok` stay as names because components and the editor use them, but they are the same blue
(`web/test_web.mjs` checks it).

The scale: text sizes `--fs-1` ... `--fs-6` are 13, 14, 14, 16, 18 and 24 px (body text is `--fs-3`), spacing `--sp-1` ...
`--sp-6` is 4, 8, 12, 16, 24 and 32 px, `--r` is 0, `--ctl-h` 30 px for controls (larger under `pointer: coarse`),
`--head-h` 41 px (the header). `--font-ui` is the system's sans-serif, `--font-mono` its monospace. The
`terminus.ttf` file in `web/style/` and the data-URL swap in `make_site.py shared_css` are left over: no rule uses the font
now (the editor's `fukiai.ttf` only draws its icon glyphs).

## Components

- **Header** `.o-head`: the dinghy and OPTIMIST (to the site's root), then the apps as tabs (`.o-nav`: builder, installer,
  simulator, editor; a thin `.o-sep` between them, the current one `aria-current="page"` shown by its background).
  `make_site.py shared_header(app)` writes it.
- **Page** `.o-page`: one column, `--page-w` wide at most (1100 px), 16 px gutters.
- **Buttons** `.o-btn` (or any `button` inside `<body class="o-ui">`): default, `.primary` (one per view), `.quiet`, `.sm`,
  `.big`. **Fields** `.o-select`, `.o-input`, `.o-check`, `.o-field` (a label over its control).
- **Info** `.o-info`: the round (i) that opens a popover. **Popover** `.o-pop` on a `[popover]` element. **Tooltip**
  `data-tip="..."`: one line, on hover and keyboard focus.
- **Tabs** `.o-tabs` (`aria-selected="true"` or `.on`), **tags** `.o-tag` (`.outline .accent .ok .warn .err`), **chips**
  `.o-chip` (`.accent .err`).
- **Card** `.o-panel` with `.o-panel-h` (the control fill with a 3 px bar of `--c` under it) and `.o-panel-b`.
- **Message** `.o-msg` (`.err .warn .ok`): one line, a short head in `<b>`, details in `.sub`. **Disclosure** `.o-details`.
- **Gauge** `.o-gauge` (`data-state="ok | tight | over"`): `.gl` label, `.gn` used / limit with `.go` the overflow, `.gb`
  the bar with `.gf` the fill, `.gx` the overflow and `.gc` the limit's mark. **Progress** `.o-progress`.
- **Toast** `.o-toast` inside one `.o-toasts`: says what just happened, then goes.

## Dos and don'ts

- Red is for what stops a build or an install (an error, an overflow); amber for what is experimental and on, or nearly
  full. Everything else is grey or the blue: credits, provenance, "tested in the emulator only" go in an (i) popover or a
  closed "Notes (n)", never in a coloured bar, and the same text is said once.
- One primary button per view. The blue marks the current thing and what is fine; it is not decoration.
- A hidden control stays hidden (`[hidden]` wins over a button's display inside `.o-ui`).
- Dense, not cramped: 4 px steps; numbers in `font-variant-numeric: tabular-nums`. Do not round a corner or add a shadow.
- Tokens, never raw colours, in a page's own CSS. A new colour starts as a token here, with its light value.
- No theme switch and no `data-theme`: the system decides.
- Phones: no sideways scroll, 16 px gutters, 16 px text in controls and larger hit areas under
  `pointer: coarse` (no zoom on focus).

## A mounted module's layout

A module that can be mounted in another page (the installer, `web/installer/installer.css`, scoped to `.oi`) keeps its
layout in its own file and takes every colour, size and font from the tokens above, never a value of its own, so it has the
look of whatever page holds it. `web/test_web.mjs` checks that the installer's CSS has no colour literal and no track
colour token.
