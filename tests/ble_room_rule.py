# SPDX-License-Identifier: GPL-3.0-only
"""The rule "BLE replaces samples" as docs/BUILDER.md states it, computed from the budget (costs.json) alone and not
from tools/builder/room.py, for tests/builder_test.py and tests/builder_menu_test.py: where the build with BLE
overflows, ONE item goes, the smallest single item that frees enough of every region that overflows, the sample sets
first and then the other items (FLUTE, the default choice, when it alone is enough). Nothing is pinned: the figures
are costs.json's as they are."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "builder"))
import configure as C  # noqa: E402
import registry as R  # noqa: E402

DEFAULT = "SET_FLUTE"


def off_value(key):
    it = R.ITEMS[key]
    return min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30) if it.is_choice else 0


def expected_room(cfg, costs):
    """cfg has BLE on -> (over, order, chosen): by how much it overflows (margin included, as --fit), the items whose
    removal alone covers it in the rule's order, and the one BLE takes (None: nothing overflows, or no single item)"""
    over = C.over_any(cfg, costs)
    sav = C.savings_of(cfg, costs)
    order = [k for k, v in sav.items() if k != "BLE" and over and all(v[r] >= n for r, n in over.items()) and
             not C.validate(dict(cfg, **{k: off_value(k)}))[0]]
    order.sort(key=lambda k: (not k.startswith("SET_"), sav[k]["flash"], k))
    chosen = (DEFAULT if DEFAULT in order else order[0]) if order else None
    return over, order, chosen


def with_ble(base, diag):
    return dict(base, BLE=1, **({"BLE_DIAG": 1} if diag else {}))
