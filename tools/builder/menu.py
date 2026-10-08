# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's interactive menu (Textual). Start it with tools/menuconfig.

Keys: space / enter toggle (a choice: next value) | / search | p profiles | s save as my profile
      | u publish this profile or not (CI builds the published ones; one of mine is shared first)
      | d delete a profile (asks first; never user-default)
      | w write a .config file | l load | b build | e build and run it in the emulator | q or ctrl+c quit
      x expand all | c collapse all
Everything it does goes through configure.py (the plain module the tests use)."""
import subprocess
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
import room as RM  # noqa: E402

BAR_W = 34


OVER_W = 20     # cells for the overflow, past the capacity mark


def over_unit(over):
    """bytes per overflow cell: 1 KiB, or coarser so that the overflow fits OVER_W cells"""
    unit = 1024
    while over > unit * OVER_W:
        unit *= 2
    return unit


def bar(region, used, cap, over):
    t = Text(f"{region.upper():8s}", style="bold")
    if over > 0:     # full up to the capacity mark, then one red cell per unit over it
        unit = over_unit(over)
        cells = max(1, -(-over // unit))
        t.append(" " * BAR_W, style="black on yellow")
        t.append("┃", style="bold white")
        t.append("█" * cells, style="bold red")
        t.append(" " * (OVER_W - cells))
        t.append(f" OVER by {over:,} B", style="bold red")
        t.append(f"  ({used:,} / {cap:,}; one red cell = {unit // 1024} KB)", style="dim")
        return t
    frac = used / cap if cap else 0
    fill = min(BAR_W, int(round(frac * BAR_W)))
    t.append(" " * fill, style="black on yellow" if frac > 0.97 else "black on green")
    t.append("·" * (BAR_W - fill), style="dim")
    t.append("┃", style="dim")
    t.append(" " * OVER_W)
    t.append(f" {used:,} / {cap:,} B  {-over:,} free")
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
    #bars { height: 7; padding: 0 1; border: round $accent; }
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
        Binding("p", "profiles", "profiles"), Binding("s", "save", "save profile"),
        Binding("u", "publish", "publish"), Binding("d", "delete", "delete profile"), Binding("w", "write", "write .config", show=False),
        Binding("l", "load", "load"),
        Binding("b", "build", "build"), Binding("e", "build_emu", "build + emu"),
        Binding("x", "expand", "expand all"), Binding("c", "collapse", "collapse"),
        Binding("escape", "clear_search", "clear search", show=False), Binding("q", "quit", "quit"),
        Binding("ctrl+c", "quit", "quit", show=False, priority=True),
    ]

    def __init__(self, cfg=None, name="default", path=None, profile=None):
        super().__init__()
        self.cfg = cfg or C.defaults()
        self.cfg_name = name
        self.profile = profile                           # the profile file's name (u publishes it), else None
        self.path = path
        self.costs = C.load_costs()
        self.filter = ""
        self.build_out = ""
        self.over, self.savings = {}, {}
        self.conflicts = {}
        self.room = RM.NONE                              # what BLE removed to make room (room.py)
        self.update_budget()

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
        deltas = self.delta_of(key)
        for r, n in deltas:
            hot = self.over.get(r, 0) > 0 and n > 0     # it holds part of an overflowing region
            t.append(f"  {r} {n / 1024:+.1f}K", style="bold red" if hot else "cyan")
        took = dict(deltas)
        if self.over and all(took.get(r, 0) >= o for r, o in self.over.items()):  # alone, it fits all
            t.append("  ◀ off = fits", style="bold red")
        if key in C.kit_keys() and live:                 # (their cost is shared: see C.kit_shared)
            shared = (C.kit_shared(self.costs) or {}).get("flash") if self.costs else None
            size = f" ({shared / 1000:.0f} KB)" if shared else ""
            t.append(f"  shares PERC samples{size} with the other sampled kits", style="dim")
            if C.is_last_kit(self.cfg, key):
                t.append("  last kit: off drops the samples", style="bold")
        errs = self.conflicts.get(key)
        if errs:                                         # an error of validate() names this item: live, in red
            t.append(f"  ✗ {errs[0]}" + (f" (+{len(errs) - 1})" if len(errs) > 1 else ""), style="bold red")
        return t

    def delta_of(self, key):
        """what the item costs as set now (vs off / its smallest choice) per region, from costs.json:
        [(region, bytes)], the regions it moves by 64 B or more, and every overflowing one it takes"""
        if not self.costs:
            return []
        s = (self.savings or {}).get(key)
        if not s:
            return []
        return [(r, s[r]) for r in C.REGIONS
                if abs(s[r]) >= 64 or (self.over.get(r, 0) > 0 and s[r] > 0)]

    def update_budget(self):
        """the estimate, the overflow per region, every item's savings and the items validate() errors name, once
        per change"""
        self.conflicts = C.conflicts(self.cfg)
        b = C.budget(self.cfg, self.costs) if self.costs else None
        self.over = {r: o for r, (_, _, o) in C.fits(b["total"]).items() if o > 0} if b else {}
        self.savings = C.savings_of(self.cfg, self.costs) if self.costs else {}

    def matches(self, key):
        if not self.filter:
            return True
        it = R.ITEMS[key]
        hay = f"{key} {it.label} {it.group} {it.desc}".lower()
        return self.filter.lower() in hay

    def rebuild(self):
        tree = self.query_one("#tree")
        collapsed = {n.data for n in self.walk(tree.root) if n.children and not n.is_expanded and n.data}
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
                               expand=bool(self.filter) or it.key not in collapsed)
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
        self.update_budget()
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
        how = ("exact: the last real build of this selection" if b.get("exact")
               else "estimate from measured deltas; b = exact build")
        lines = Text(f"{self.cfg_name}  ({how})\n", style="italic")
        for r, (used, cap, over) in C.fits(b["total"]).items():
            lines.append(bar(r, used, cap, over))
            lines.append("\n")
        w.update(lines)

    def refresh_msgs(self):
        err, warn, note = C.validate(self.cfg)
        t = Text()
        b = C.budget(self.cfg, self.costs) if self.costs else None
        if b:
            over, sav = self.over, self.savings
            if over:
                t.append("Red sizes in the list hold the overflow; ◀ marks an item whose removal alone fits.\n",
                         style="red")
                for r, o in over.items():
                    t.append(f"{r.upper()} overflows by {o:,} B. ", style="bold red")
                    best = sorted(((s[r], k) for k, s in sav.items() if s[r] > 0), reverse=True)[:6]
                    t.append("Biggest " + r + " items: " + ", ".join(f"{R.ITEMS[k].label} {n:,}" for n, k in best)
                             + "\n", style="red")
            if b["unmeasured"]:
                t.append(f"not measured: {', '.join(b['unmeasured'])}\n", style="yellow")
        if self.room.note:                               # BLE replaced samples: what went, and what else could
            t.append(f"BLE    {self.room.note}\n", style="bold magenta")
        for e in err:
            t.append(f"ERROR  {e}\n", style="bold red")
        for w in warn:
            t.append(f"warn   {w}\n", style="yellow")
        for n in note:
            t.append(f"NOTICE {n}\n", style="bold yellow")
        if self.build_out:                               # the last build's result first: it stays until the next b
            bad = self.build_out.startswith(("BUILD FAILED", "CANNOT BUILD"))
            t = Text(self.build_out + "\n\n", style="bold red" if bad else "green") + t
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
        for e in self.conflicts.get(key, []):
            t.append(f"ERROR  {e}\n", style="bold red")
        if self.costs:
            for v in ([c[0] for c in it.choices] if it.is_choice else [0, 1]):
                d = C.item_delta_alone(self.costs, key, v) if v != it.default else {r: 0 for r in C.REGIONS}
                if d is not None:
                    lab = dict(it.choices).get(v, "on" if v else "off")
                    t.append(f"  {lab:24s} " + "  ".join(f"{r} {d[r]:+,}" for r in C.REGIONS) + "\n", style="cyan")
            for name, d in self.costs.get("pairs", {}).items():   # (a cost that depends on another item's value)
                conds = C.pair_conds(name)
                if any(k == key for k, _ in conds):
                    t.append(f"  {' + '.join(f'{k}={v}' for k, v in conds)} adds " +
                             "  ".join(f"{r} {d.get(r, 0):+,}" for r in C.REGIONS) + "\n", style="cyan")
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
        before = dict(self.cfg)
        if it.is_choice:
            vals = [c[0] for c in it.choices]
            self.cfg[key] = vals[(vals.index(self.cfg[key]) + 1) % len(vals)] if self.cfg[key] in vals else vals[0]
        else:
            self.cfg[key] = 0 if self.cfg[key] else 1
        if it.notice and self.cfg[key]:
            self.notify(it.notice, title=it.label, severity="warning", timeout=8)
        old = self.room
        self.cfg, self.room = RM.after_toggle(before, self.cfg, key, old)      # (BLE replaces samples)
        if self.room.note and self.room.note != old.note:
            self.notify(self.room.note, title="BLE", severity="warning", timeout=12)
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
        mine = C.my_profile_names()
        pub = set(C.published_profiles())
        names = ([n + PUBLISHED * (n in pub) for n in C.profile_names()] + [f"mine: {n}" for n in mine] +
                 ["(registry defaults)"])

        def go(n):
            n = n.removesuffix(PUBLISHED)
            self.room = RM.NONE
            if n.startswith("("):
                self.cfg, self.cfg_name, self.profile = C.defaults(), "default", None
                try:
                    LAST.unlink()
                except OSError:
                    pass
            else:
                self.profile = n.removeprefix("mine: ")
                self.cfg, self.cfg_name = C.load_profile(self.profile)
                remember("profile", self.profile)
            self.rebuild()
            self.refresh_all()
            self.notify(f"profile {self.cfg_name}")
        self.push_screen(Pick("profile", names, go))

    def action_save(self):
        """save as one of my profiles (config/my-profiles): it then shows in p, and --profile NAME builds it"""
        def go(name):
            name = name.strip()
            try:
                p = C.save_my_profile(self.cfg, name)
            except (OSError, C.ConfigError) as e:
                self.notify(str(e), severity="error")
                return
            self.cfg_name, self.path, self.profile = name, str(p), name
            remember("profile", name)
            self.refresh_all()
            self.notify(f"saved profile {name}")
        default = "" if self.cfg_name in C.profile_names() + ["default"] else self.cfg_name
        self.push_screen(Ask("save as my profile (name)", default, go))

    def action_publish(self):
        """u: a shipped profile in or out of CI's builds; one of mine is shared first (config/profiles: commit it)"""
        n = self.profile
        try:
            if n is None:
                raise C.ConfigError("not a profile: save it as one first (s), then u")
            shared = n in C.my_profile_names() and n not in C.profile_names()
            if shared:
                C.share_profile(n)
            on = shared or n not in C.published_profiles()
            C.set_published(n, on)
        except (OSError, C.ConfigError) as e:
            self.notify(str(e), severity="error")
            return
        self.notify(f"{n}: " + ("published (CI builds it)" if on else "not published") +
                    (f"; shared to config/profiles/{n}.config" if shared else "") + " - commit it", timeout=8)

    def action_delete(self):
        """d: pick a profile (mine, then shipped; user-default is never offered), type yes, it is gone"""
        mine = [n for n in C.my_profile_names() if n not in C.profile_names()]
        names = [f"mine: {n}" for n in mine] + [n for n in C.profile_names() if n != C.DEFAULT_PROFILE]
        if not names:
            self.notify("no profile to delete (user-default stays)", severity="warning")
            return

        def confirm(n):
            n = n.removeprefix("mine: ")
            shipped = n not in mine
            note = " (shipped: git has it back until you commit the deletion)" if shipped else ""
            self.push_screen(Ask(f"delete profile {n}{note}? type yes", "", lambda ans: self.delete_profile(n, ans)))
        self.push_screen(Pick("delete which profile", names, confirm))

    def delete_profile(self, n, answer):
        if answer.strip().lower() != "yes":
            self.notify(f"{n} kept")
            return
        try:
            p = C.delete_profile(n)
        except (OSError, C.ConfigError) as e:
            self.notify(str(e), severity="error")
            return
        if self.profile == n:                            # the menu keeps its values, unnamed: s saves them again
            self.profile, self.path = None, None
            try:
                LAST.unlink()
            except OSError:
                pass
        self.notify(f"deleted {p}")

    def action_write(self):
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
                self.room = RM.NONE
                self.cfg_name = nm or Path(p).stem
                self.path = p
                self.profile = None
                remember("config", p)
            except (OSError, C.ConfigError) as e:
                self.notify(str(e), severity="error")
                return
            self.rebuild()
            self.refresh_all()
        self.push_screen(Ask("load .config", str(self.path or C.ROOT / "config" / "user.config"), go))

    def action_build(self, then_emu=False):
        err, _, _ = C.validate(self.cfg)
        if err:                                          # kept in the panel until the next b
            self.build_out = "CANNOT BUILD\n" + "\n".join(f"  {e}" for e in err)
            self.refresh_msgs()
            self.notify("; ".join(err), title="cannot build", severity="error", timeout=10)
            return
        self.build_out = "building... (about 10 s)" + (", then the emulator" if then_emu else "")
        self.refresh_msgs()
        self.run_build(dict(self.cfg), self.cfg_name, then_emu)

    def action_build_emu(self):
        """E: build, and when the firmware fits, run it in the emulator (tools/optimist.py emu, in the background)"""
        self.action_build(then_emu=True)

    @work(thread=True, exclusive=True)
    def run_build(self, cfg, name, then_emu=False):
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
        keep = ("over", "FAIL", "package", "error", "Error", "ld:", "overflowed", "undefined reference")
        tail = [ln for ln in out.splitlines() if ln.strip().startswith(keep) or any(k in ln for k in keep[3:])]
        tail = tail[-12:] if ok else tail[-20:]          # a failure keeps the compiler's own lines
        res = ("BUILD OK" if ok else "BUILD FAILED") + (" (measurement build: does not fit)" if ok and not fit else "")
        text = res + "\n" + "\n".join(lines + tail)
        if ok and fit and pkg.exists():
            text += f"\n  package: {pkg}"
            if then_emu:
                text += "\n" + self.launch_emu()
        elif then_emu:
            text += "\n  emulator: not started (no package that fits)"
        self.call_from_thread(self.show_build, text, sizes)

    def launch_emu(self):
        """the newest named package of this build in the emulator, in the background -> one line for the panel"""
        named = sorted((C.ROOT / "build").glob("optimist-*.fwsc"), key=lambda f: f.stat().st_mtime)
        fw = named[-1] if named else C.ROOT / "build" / "felucca.fwsc"
        cmd = [sys.executable, str(C.ROOT / "tools" / "optimist.py"), "emu", str(fw), "--bg"]
        try:
            r = subprocess.run(cmd, cwd=C.ROOT, capture_output=True, text=True, timeout=900)
        except (OSError, subprocess.TimeoutExpired) as e:
            return f"  emulator: could not start ({e})"
        lines = [ln for ln in (r.stdout + r.stderr).splitlines() if ln.strip()]
        if r.returncode:
            return "  emulator FAILED: " + (lines[-1] if lines else f"exit {r.returncode}")
        return "  emulator: " + (lines[-1] if lines else f"started with {fw.name}")

    def show_build(self, text, sizes):
        self.build_out = text                            # stays in the panel until the next b
        self.refresh_msgs()
        failed = not text.startswith("BUILD OK")
        self.notify(text.splitlines()[0], severity="error" if failed else "information", timeout=10 if failed else 5)


PUBLISHED = "  (published)"                  # the profile list's mark
LAST = C.ROOT / "config" / "last-used.txt"     # what the menu opened with last time (git-ignored)


def remember(kind, value):
    """kind: "profile" (a shipped or my profile name) or "config" (a .config path)"""
    try:
        LAST.parent.mkdir(parents=True, exist_ok=True)
        LAST.write_text(f"{kind}\n{value}\n")
    except OSError:
        pass


def recall():
    """-> (cfg, name, path, profile) of the last profile or .config the menu used, else the registry defaults"""
    try:
        kind, value = LAST.read_text().splitlines()[:2]
        if kind == "profile":
            cfg, name = C.load_profile(value)
            return cfg, name, None, value
        if kind == "config":
            cfg, name = C.load(value)
            return cfg, name or Path(value).stem, value, None
    except (OSError, ValueError, C.ConfigError):
        pass
    return None, "default", None, None


def main(argv):
    cfg, name, path, profile = None, "default", None, None
    if len(argv) >= 2 and argv[0] == "--config":
        path = argv[1]
        cfg, name = C.load(path)
        name = name or Path(path).stem
        remember("config", path)
    elif len(argv) >= 2 and argv[0] == "--profile":
        cfg, name = C.load_profile(argv[1])
        profile = argv[1]
        remember("profile", argv[1])
    else:
        cfg, name, path, profile = recall()        # no argument: where you left off
    Builder(cfg, name, path, profile).run()


if __name__ == "__main__":
    main(sys.argv[1:])
