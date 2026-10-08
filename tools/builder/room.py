# SPDX-License-Identifier: GPL-3.0-only
"""Making room for an item that does not fit: "BLE replaces samples" (docs/BUILDER.md, "Making room for BLE").

BLE (Experimental, +13 KB of flash, +5.5 KB of RAM) overflows the app slot of the user-default build. Ticking it
removes one item to make room, FLUTE (the sample set) by default; the user can have another set or big item
removed instead. The same rules serve the menu (menu.py: toggling) and the headless path (configure.py /
optimist.py --ble-drop KEY), and use what the builder has: the budget (costs.json), savings_of (what each built
item frees), validate (a candidate that makes the configuration invalid is not offered) and over_any (the same
margin as --fit). Nothing here touches the firmware: a build without BLE is exactly what it was.

Pure functions on plain dicts: a configuration in, a new configuration out (nothing mutated), plus a Room (what was
removed, and the message to show). The menu keeps the Room between toggles; the rules, in after_toggle():

  - BLE ticked and the build then overflows: the default item (SET_FLUTE) goes, when removing it is enough; the
    message names it and offers the other sets and big items that would free enough, with their sizes;
  - BLE ticked and the build fits as it is (a smaller profile): nothing is removed;
  - another item removed while one is auto-removed: the auto-removed one comes back when the build then fits
    with it, so the user's pick replaces the default;
  - BLE unticked: what was removed comes back, except what the user ticked or unticked by hand since.
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
    first (smallest first), then the other items, smallest first; BLE itself and anything that would make the
    configuration invalid are left out"""
    sav = C.savings_of(cfg, costs)
    out = []
    for k, s in sav.items():
        if k == TRIGGER or any(s[r] < o for r, o in over.items()):
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
    """the user toggled `key` (before -> after configurations): -> (cfg, Room), the cfg with room made or given
    back as the rules in the module docstring say"""
    costs = costs or C.load_costs()
    was, now = C.built(before, TRIGGER), C.built(after, TRIGGER)
    if key == TRIGGER:
        if now and not was:
            return make_room(after, costs)
        if was and not now:
            back = restore(after, room)
            names = [short(k) for k, _ in room.dropped if back[k] != after[k]]
            return back, Room(note=("BLE off: " + ", ".join(names) + " restored") if names else "")
        return after, room
    held = tuple((k, p) for k, p in room.dropped if k != key)      # (the user's hand on it: no longer ours)
    if held != room.dropped:
        return after, Room(held, room.note if held else "")
    if not room.dropped or not now or after[key] >= before[key]:
        return after, room
    back = restore(after, room)                                     # (the user removed something else)
    if costs and not _need(back, costs):
        names = ", ".join(short(k) for k in room.keys())
        return back, Room(note=f"{short(key)} removed instead: {names} restored")
    return after, room
