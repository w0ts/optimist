# The web style

All Optimist web pages (installer, builder, simulator and the editor's Install, Build and Simulator tabs) use the one
style of the three sites (Optimist, jank and DN2 wishes): [common.css](https://w0ts.github.io/site-style/common.css),
kept in https://github.com/w0ts/site-style. A site sets only its accent; Optimist's is in `web/style/optimist.css`:

    :root { --accent: #da92cc; --accent-text-l: #b13c9a; --accent-text-d: #da92cc; }

- Light and dark follow the system; `data-theme="light"|"dark"` forces one. `--accent` is the bright fill colour (dark `--on`
  text on it), `--accent-text` the small-text shade.
- Components are neutral names: `.card` (`.card-head`), `.msg` (`.ok`, `.warn`, `.err`), `.tag`, `.gauge`, `.tabs`, `.list`,
  `button` (accent fill; `.quiet` outline, `.sm`, `.big`, `.icon`, `.link`), `details.sec`, `dl.kv`, `.titlebar`, `header.site` +
  `nav.site`, `.pop`, `.toast`, `[data-tip]`. Dense UI: class `dense` on a container.
- The page header is the shared one row, `<header class="site"><div class="titlebar"><h1>title</h1></div><nav class="site">tabs</nav></header>`
  directly in `<body>` (`make_site.shared_header`): the same 1100 px width as `main`, so the title's left edge and the tabs' right
  edge line up with the content. Modules take their width from the host (no `max-width` of their own).
- The App Store badges are Apple's vector badges cropped to the logo and words and tinted with `currentColor`
  (`make_site.BADGE_TINT`: `"grey"` or `"accent"`).
- The pages load common.css by URL (`web/make_site.py` links it and inlines `optimist.css`). Two exceptions that must work
  offline use the vendored copy `web/style/common.css`: the Mirror app's builder page (inlined) and the editor, whose Install /
  Build / Simulator panels load it scoped to `.o-host` with CSS nesting (`appStyle()` in `web/editor.html`). Refresh the copy
  from the site-style repo when common.css changes.
- Layout of one module stays in its own CSS (`web/installer/installer.css`, `web/builder/builder.css`, `web/sim/sim.css`),
  using only the shared tokens (`--bg --panel --ink --dim --line --off --accent --accent-text --err --warn --on`).
