# SPDX-License-Identifier: GPL-3.0-only
"""Making room for BLE, headless only: `--ble-drop ITEM` (docs/BUILDER.md, "Making room for BLE"). The menus (the CLI menu,
the web page) no longer do it: ticking BLE removes nothing, the meters show an overflow like any other tick (2026-10-21).

BLE (about +18 KB of flash with Bluetooth Classic Audio built, +29 KB without, and +6 KB of RAM, costs.json) overflows the
app slot of the fuller profiles. `--ble-drop ITEM` removes that one item, which must free enough. It uses what the builder
has: the budget (costs.json), savings_of (what each built item frees), validate (a candidate that makes the configuration
invalid is not offered) and over_any (the same margin as --fit). Nothing here touches the firmware: a build without BLE is
exactly what it was.

Pure functions on plain dicts: a configuration in, a new configuration out (nothing mutated), plus a Room (what was
removed, and the message to show). Only make_room does anything now: with BLE on and the build overflowing, `drop` (the
item named, or SET_FLUTE) goes when that frees enough, else the smallest item that does; the message offers the others.
after_toggle (the menus' hook) removes nothing.
"""
from dataclasses import dataclass

import configure as C
import registry as R

TRIGGER = "BLE"
DEFAULT_DROP = "SET_FLUTE"       # the sample set BLE replaces unless the user picks another
OFFERS = 6                       # how many other items the message lists


@dataclass(frozen=True)
class Room:
    """what making room did: dropped ((key, value before), ...) are the items this module switched off (or down)
    and restores; note is the message for the user ("" when nothing happened)"""
    dropped: tuple = ()
    note: str = ""

    def keys(self):
        return tuple(k for k, _ in self.dropped)


NONE = Room()


def key_of(name):
    """"FLUTE", "flute" or "SET_FLUTE" -> "SET_FLUTE"; any other registry key as given (upper-cased)"""
    n = name.strip().upper()
    if n in R.ITEMS:
        return n
    if "SET_" + n in R.ITEMS:
        return "SET_" + n
    raise C.ConfigError(f"{name}: not an item (a sample set such as FLUTE or SET_PIANO, or a registry key)")


def short(key):
    """a name for messages: FLUTE samples, else the item's label up to its first parenthesis"""
    if key.startswith("SET_"):
        return f"{key[4:]} samples"
    return R.ITEMS[key].label.split(" (")[0]


def kb(n):
    return f"{n / 1000:.0f} KB"


def _off_value(key):
    it = R.ITEMS[key]
    return 0 if not it.is_choice else min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30)


def _need(cfg, costs):
    """{region: bytes over, estimate margin included} for cfg (as C.over_any)"""
    return C.over_any(cfg, costs)


def candidates(cfg, costs, over):
    """-> [(key, {region: freed})] items built in cfg whose removal alone covers the overflow `over`, sample sets
    first (smallest first), then the other items, smallest first; BLE itself, its own options (BLE_DIAG, BLE_BOND,
    BLE_CENTRAL: what was asked for with it, 2026-10-19: x0x-drums + BLE_DIAG had BLE_DIAG removed to make room for BLE)
    and anything that would make the configuration invalid are left out"""
    sav = C.savings_of(cfg, costs)
    out = []
    for k, s in sav.items():
        if k == TRIGGER or R.ITEMS[k].parent == TRIGGER or any(s[r] < o for r, o in over.items()):
            continue
        if C.validate(dict(cfg, **{k: _off_value(k)}))[0]:
            continue
        out.append((k, s))
    return sorted(out, key=lambda e: (not e[0].startswith("SET_"), e[1]["flash"], e[0]))


def _over_text(over, base):
    return " and ".join(f"~{kb(max(base.get(r, 0), 1))} of {r}" for r in over)


def _offers(cands, chosen):
    other = [(k, s) for k, s in cands if k != chosen][:OFFERS]
    return ", ".join(f"{short(k)} {kb(s['flash'])}" for k, s in other)


def make_room(cfg, costs=None, drop=None, how="untick it"):
    """BLE is on in cfg: -> (cfg', Room). Nothing is removed when the build fits as it is. Otherwise `drop` (an item
    key; default SET_FLUTE) goes; when the default alone does not free enough, the smallest candidate that does goes
    instead; an explicit `drop` that does not free enough is a ConfigError (the user named it). `how` tells the user how
    to pick another (the menu: untick it; the command line: --ble-drop ITEM)."""
    costs = costs or C.load_costs()
    if not C.built(cfg, TRIGGER):
        return dict(cfg), NONE
    over = _need(cfg, costs) if costs else {}
    if costs and not over:
        return dict(cfg), NONE
    cands = candidates(cfg, costs, over) if costs else []
    free = {k for k, _ in cands}
    chosen = drop or DEFAULT_DROP
    if drop is not None and drop not in R.ITEMS:
        raise C.ConfigError(f"{drop}: not an item")
    if costs and chosen not in free:
        if drop is not None:
            have = (C.savings_of(cfg, costs).get(drop) or {}).get("flash", 0)
            raise C.ConfigError(
                f"{short(drop)} frees {kb(have)}, not enough for BLE (it needs {_over_text(over, over)} more); "
                f"one of: " + (_offers(cands, "") or "none frees enough"))
        if not cands:
            return dict(cfg), Room(note=f"BLE does not fit and no single item frees enough ({_over_text(over, over)} "
                                        "over): remove several to make room")
        chosen = cands[0][0]
    if cfg.get(chosen) == _off_value(chosen):
        return dict(cfg), NONE
    prev = cfg[chosen]
    new = dict(cfg, **{chosen: _off_value(chosen)})
    sav = (C.savings_of(cfg, costs).get(chosen) or {}).get("flash", 0) if costs else 0
    need = _over_text(over, {r: (C.item_delta(costs, TRIGGER, 1) or {}).get(r, 0) for r in over}) if costs else "room"
    offers = _offers(cands, chosen)
    note = (f"BLE needs {need}: {short(chosen)} removed to make room" + (f" ({kb(sav)})" if sav else "") +
            (f"; pick another to remove instead ({how}): " + offers if offers else ""))
    return new, Room(((chosen, prev),), note)


def restore(cfg, room):
    """put back what the room removed, except an item whose value is no longer the one this module set"""
    out = dict(cfg)
    for k, prev in room.dropped:
        if out.get(k) == _off_value(k):
            out[k] = prev
    return out


def after_toggle(before, after, key, room, costs=None):
    """the user toggled `key` (before -> after configurations): -> (cfg, Room). Ticking BLE removes nothing, as any other
    tick (the user, 2026-10-21: "like ANY other tick: the meters go over and it tells you it does not fit"; the rule
    that unticked an item behind the user's back made BLE ON show more free flash than OFF). make_room stays for
    the headless --ble-drop, which names the item explicitly"""
    return after, NONE
