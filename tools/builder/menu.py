# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's interactive menu (Textual). Start it with tools/menuconfig.

Keys: space / enter toggle (a choice: next value) | / search | p profiles | s save | l load | b build
      e expand all | c collapse all | d details | q quit
Everything it does goes through configure.py (the plain module the tests use)."""
import sys
from pathlib import Path

from rich.text import Text
from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Footer, Header, Input, Label, OptionList, Static, Tree

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import configure as C  # noqa: E402
import registry as R  # noqa: E402

BAR_W = 34


def bar(region, used, cap, over):
    frac = used / cap if cap else 0
    fill = min(BAR_W, int(round(frac * BAR_W)))
    style = "bold white on red" if over > 0 else ("black on yellow" if frac > 0.97 else "black on green")
    t = Text(f"{region.upper():8s}", style="bold")
    t.append(" " * fill, style=style)
    t.append("·" * (BAR_W - fill), style="dim")
    tail = f" {used:,} / {cap:,} B  " + (f"OVER by {over:,}" if over > 0 else f"{-over:,} free")
    t.append(tail, style="bold red" if over > 0 else "")
    return t


class Pick(ModalScreen):
    """a list to pick from (profiles)"""
    BINDINGS = [Binding("escape", "app.pop_screen", "cancel")]

    def __init__(self, title, options, cb):
        super().__init__()
        self.title_, self.options, self.cb = title, options, cb

    def compose(self) -> ComposeResult:
        with Vertical(id="modal"):
            yield Label(self.title_)
            yield OptionList(*self.options, id="pick")

    def on_option_list_option_selected(self, ev):
        self.app.pop_screen()
        self.cb(self.options[ev.option_index])


class Ask(ModalScreen):
    """one line of text (a file name)"""
    BINDINGS = [Binding("escape", "app.pop_screen", "cancel")]

    def __init__(self, title, value, cb):
        super().__init__()
        self.title_, self.value, self.cb = title, value, cb

    def compose(self) -> ComposeResult:
        with Vertical(id="modal"):
            yield Label(self.title_)
            yield Input(value=self.value, id="ask")

    def on_input_submitted(self, ev):
        self.app.pop_screen()
        self.cb(ev.value)


class Builder(App):
    TITLE = "Firmware builder"
    CSS = """
    #bars { height: 6; padding: 0 1; border: round $accent; }
    #main { height: 1fr; }
    #tree { width: 3fr; border: round $primary; }
    #side { width: 2fr; }
    #info { border: round $primary; height: 1fr; padding: 0 1; }
    #msgs { border: round $warning; height: 1fr; padding: 0 1; }
    #search { display: none; }
    #modal { width: 70; height: auto; max-height: 24; border: thick $accent; background: $surface; padding: 1; }
    Pick, Ask { align: center middle; }
    """
    BINDINGS = [
        Binding("space", "toggle", "toggle", priority=True), Binding("slash", "search", "search"),
        Binding("p", "profiles", "profiles"), Binding("s", "save", "save"), Binding("l", "load", "load"),
        Binding("b", "build", "build"), Binding("e", "expand", "expand all"), Binding("c", "collapse", "collapse"),
        Binding("escape", "clear_search", "clear search", show=False), Binding("q", "quit", "quit"),
    ]

    def __init__(self, cfg=None, name="default", path=None):
        super().__init__()
        self.cfg = cfg or C.defaults()
        self.cfg_name = name
        self.path = path
        self.costs = C.load_costs()
        self.filter = ""
        self.build_out = ""

    # ---- layout
    def compose(self) -> ComposeResult:
        yield Header()
        yield Static(id="bars")
        yield Input(placeholder="search (label, key, group): enter to keep, escape to clear", id="search")
        with Horizontal(id="main"):
            yield Tree("build", id="tree")
            with Vertical(id="side"):
                yield VerticalScroll(Static(id="info_t"), id="info")
                yield VerticalScroll(Static(id="msgs_t"), id="msgs")
        yield Footer()

    def on_mount(self):
        tree = self.query_one("#tree")
        tree.show_root = False
        self.rebuild()
        self.refresh_all()
        tree.focus()

    # ---- the tree
    def node_label(self, key):
        it = R.ITEMS[key]
        v = self.cfg[key]
        live = not it.parent or C.built(self.cfg, it.parent)
        if it.is_choice:
            lab = dict(it.choices).get(v, str(v))
            box = f"<{lab}>"
        else:
            box = "[x]" if v else "[ ]"
        t = Text(f"{box} {it.label}", style="" if live else "dim")
        if it.experimental:
            t.append("  EXPERIMENTAL", style="bold magenta")
        if it.notice and v:
            t.append("  NOTICE", style="bold yellow")
        d = self.delta_of(key)
        if d:
            t.append(f"   {d}", style="cyan")
        return t

    def delta_of(self, key):
        """what the item costs as set now (vs off / its smallest choice), flash and pool, from costs.json"""
        if not self.costs:
            return ""
        s = C.savings_of(self.cfg, self.costs).get(key)
        if not s:
            return ""
        parts = []
        for r, n in (("flash", s["flash"]), ("pool", s["pool"]), ("ram", s["ram"])):
            if abs(n) >= 64:
                parts.append(f"{r} {n / 1024:+.1f}K")
        return " ".join(parts)

    def matches(self, key):
        if not self.filter:
            return True
        it = R.ITEMS[key]
        hay = f"{key} {it.label} {it.group} {it.desc}".lower()
        return self.filter.lower() in hay

    def rebuild(self):
        tree = self.query_one("#tree")
        expanded = {n.data for n in self.walk(tree.root) if n.is_expanded and n.data}
        tree.clear()
        self.nodes = {}
        for g in R.GROUPS:
            items = [it for it in R.top_level(g) if self.matches(it.key) or any(self.matches(c) for c in it.children)]
            if not items:
                continue
            gn = tree.root.add(Text(g, style="bold"), data=("group", g), expand=True)
            for it in items:
                if it.children:
                    n = gn.add(self.node_label(it.key), data=it.key,
                               expand=bool(self.filter) or it.key in expanded)
                    for c in it.children:
                        if self.matches(c) or self.matches(it.key):
                            self.nodes[c] = n.add_leaf(self.node_label(c), data=c)
                else:
                    n = gn.add_leaf(self.node_label(it.key), data=it.key)
                self.nodes[it.key] = n

    def walk(self, node):
        yield node
        for c in node.children:
            yield from self.walk(c)

    def relabel(self):
        for k, n in self.nodes.items():
            n.set_label(self.node_label(k))

    # ---- the panels
    def refresh_all(self):
        self.relabel()
        self.refresh_bars()
        self.refresh_msgs()
        self.refresh_info()

    def refresh_bars(self):
        w = self.query_one("#bars")
        b = C.budget(self.cfg, self.costs) if self.costs else None
        if not b:
            w.update(Text("no tools/builder/costs.json: run tools/builder/measure_costs.py (or b: a real build)",
                          style="yellow"))
            return
        lines = Text(f"{self.cfg_name}  (estimate from measured deltas; b = exact build)\n", style="italic")
        for r, (used, cap, over) in C.fits(b["total"]).items():
            lines.append(bar(r, used, cap, over))
            lines.append("\n")
        w.update(lines)

    def refresh_msgs(self):
        err, warn, note = C.validate(self.cfg)
        t = Text()
        b = C.budget(self.cfg, self.costs) if self.costs else None
        if b:
            over = {r: o for r, (_, _, o) in C.fits(b["total"]).items() if o > 0}
            if over:
                sav = C.savings_of(self.cfg, self.costs)
                for r, o in over.items():
                    t.append(f"{r.upper()} overflows by {o:,} B. ", style="bold red")
                    best = sorted(((s[r], k) for k, s in sav.items() if s[r] > 0), reverse=True)[:6]
                    t.append("Biggest " + r + " items: " + ", ".join(f"{R.ITEMS[k].label} {n:,}" for n, k in best)
                             + "\n", style="red")
            if b["unmeasured"]:
                t.append(f"not measured: {', '.join(b['unmeasured'])}\n", style="yellow")
        for e in err:
            t.append(f"ERROR  {e}\n", style="bold red")
        for w in warn:
            t.append(f"warn   {w}\n", style="yellow")
        for n in note:
            t.append(f"NOTICE {n}\n", style="bold yellow")
        if self.build_out:
            t.append("\n" + self.build_out)
        self.query_one("#msgs_t").update(t if t.plain else Text("no warnings", style="green"))

    def refresh_info(self):
        tree = self.query_one("#tree")
        key = tree.cursor_node.data if tree.cursor_node else None
        if not isinstance(key, str):
            self.query_one("#info_t").update(Text("space: toggle, /: search, b: build", style="dim"))
            return
        it = R.ITEMS[key]
        t = Text(it.label + "\n", style="bold")
        t.append(f"{key}" + (f"  ({it.flag})" if it.flag else "") + f"  bit {it.bit}\n", style="dim")
        if it.parent:
            t.append(f"option of {R.ITEMS[it.parent].label}\n", style="dim")
        if it.desc:
            t.append(it.desc + "\n")
        if self.costs:
            for v in ([c[0] for c in it.choices] if it.is_choice else [0, 1]):
                d = C.item_delta(self.costs, key, v) if v != it.default else {r: 0 for r in C.REGIONS}
                if d is not None:
                    lab = dict(it.choices).get(v, "on" if v else "off")
                    t.append(f"  {lab:24s} " + "  ".join(f"{r} {d[r]:+,}" for r in C.REGIONS) + "\n", style="cyan")
        if it.provenance:
            t.append("from " + it.provenance.line() + "\n", style="green")
            if it.provenance.url:
                t.append(it.provenance.url + "\n", style="dim")
        if it.notice:
            t.append(it.notice + "\n", style="bold yellow")
        if it.off_warning:
            t.append("off: " + it.off_warning + "\n", style="yellow")
        self.query_one("#info_t").update(t)

    def on_tree_node_highlighted(self, ev):
        self.refresh_info()

    def on_tree_node_selected(self, ev):
        if isinstance(ev.node.data, str) and not R.ITEMS[ev.node.data].children:
            self.action_toggle()

    # ---- actions
    def action_toggle(self):
        if isinstance(self.focused, Input):              # (a space typed in the search box)
            self.focused.insert_text_at_cursor(" ")
            return
        node = self.query_one("#tree").cursor_node
        key = node.data if node else None
        if not isinstance(key, str):
            return
        it = R.ITEMS[key]
        if it.is_choice:
            vals = [c[0] for c in it.choices]
            self.cfg[key] = vals[(vals.index(self.cfg[key]) + 1) % len(vals)] if self.cfg[key] in vals else vals[0]
        else:
            self.cfg[key] = 0 if self.cfg[key] else 1
        if it.notice and self.cfg[key]:
            self.notify(it.notice, title=it.label, severity="warning", timeout=8)
        self.refresh_all()

    def action_expand(self):
        for n in self.walk(self.query_one("#tree").root):
            n.expand()

    def action_collapse(self):
        for n in self.walk(self.query_one("#tree").root):
            if isinstance(n.data, str):
                n.collapse()

    def action_search(self):
        s = self.query_one("#search")
        s.display = True
        s.focus()

    def action_clear_search(self):
        s = self.query_one("#search")
        s.value = ""
        s.display = False
        self.filter = ""
        self.rebuild()
        self.refresh_all()
        self.query_one("#tree").focus()

    def on_input_changed(self, ev):
        if ev.input.id == "search":
            self.filter = ev.value.strip()
            self.rebuild()
            self.relabel()

    def on_input_submitted(self, ev):
        if ev.input.id == "search":
            self.query_one("#tree").focus()

    def action_profiles(self):
        names = C.profile_names() + ["(registry defaults)"]

        def go(n):
            if n.startswith("("):
                self.cfg, self.cfg_name = C.defaults(), "default"
            else:
                self.cfg, self.cfg_name = C.load_profile(n)
            self.rebuild()
            self.refresh_all()
            self.notify(f"profile {self.cfg_name}")
        self.push_screen(Pick("profile", names, go))

    def action_save(self):
        def go(p):
            Path(p).parent.mkdir(parents=True, exist_ok=True)
            Path(p).write_text(C.dump(self.cfg, self.cfg_name))
            self.path = p
            self.notify(f"saved {p}")
        self.push_screen(Ask("save .config as", str(self.path or C.ROOT / "config" / "user.config"), go))

    def action_load(self):
        def go(p):
            try:
                self.cfg, nm = C.load(p)
                self.cfg_name = nm or Path(p).stem
                self.path = p
            except (OSError, C.ConfigError) as e:
                self.notify(str(e), severity="error")
                return
            self.rebuild()
            self.refresh_all()
        self.push_screen(Ask("load .config", str(self.path or C.ROOT / "config" / "user.config"), go))

    def action_build(self):
        err, _, _ = C.validate(self.cfg)
        if err:
            self.notify("; ".join(err), title="cannot build", severity="error")
            return
        self.build_out = "building... (about 10 s)"
        self.refresh_msgs()
        self.run_build(dict(self.cfg), self.cfg_name)

    @work(thread=True, exclusive=True)
    def run_build(self, cfg, name):
        b = C.budget(cfg, self.costs) if self.costs else None
        fit = b and all(o <= 0 for _, _, o in C.fits(b["total"]).values())
        ok, sizes, out = C.build(cfg, name, measure=not fit)
        if not ok and fit:                               # (the estimate fitted, the exact build did not)
            ok, sizes, out = C.build(cfg, name, measure=True)
        lines = []
        if sizes:
            for r, (used, cap, over) in C.fits({k: sizes[k] for k in C.REGIONS}).items():
                lines.append(f"  exact {r:8s} {used:9,d} / {cap:9,d}  " +
                             (f"OVER by {over:,}" if over > 0 else f"{-over:,} free"))
        pkg = C.ROOT / "build" / "felucca.fwsc"
        tail = [ln for ln in out.splitlines() if ln.strip().startswith(("over", "FAIL", "package"))][-6:]
        res = ("BUILD OK" if ok else "BUILD FAILED") + (" (measurement build: does not fit)" if ok and not fit else "")
        text = res + "\n" + "\n".join(lines + tail)
        if ok and fit and pkg.exists():
            text += f"\n  package: {pkg}"
        self.call_from_thread(self.show_build, text, sizes)

    def show_build(self, text, sizes):
        self.build_out = text
        self.refresh_msgs()
        self.notify(text.splitlines()[0])


def main(argv):
    cfg, name, path = None, "default", None
    if len(argv) >= 2 and argv[0] == "--config":
        path = argv[1]
        cfg, name = C.load(path)
        name = name or Path(path).stem
    elif len(argv) >= 2 and argv[0] == "--profile":
        cfg, name = C.load_profile(argv[1])
    Builder(cfg, name, path).run()


if __name__ == "__main__":
    main(sys.argv[1:])
