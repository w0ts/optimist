#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The CLI builder's answers for the web builder's tests (web/test_builder.mjs, web/e2e_builder.mjs): the same
questions the page asks tools/builder/web_api.py, answered here by configure.py and room.py as the CLI and the menu
run them (the estimate only: no real build's sizes, as on the page), without web_api.

  python3 tests/builder_web_ref.py < request.json > answers.json

request: {"views": [{"values", "name", "commit"}], "toggles": [{"values", "key", "value", "room"}],
          "texts": [".config text"]}
answers: {"views": [{"eval", "savings", "conflicts", "over", "fit", "dump"}], "toggles": [{"values", "room"}],
          "texts": [{"values", "name", "warnings", "error"}], "registry": core.describe()}
"""
import json
import sys

from builder_path import ROOT  # noqa: F401  (tools/builder on sys.path)
import configure as C
import registry as R
import room as RM

C.exact_sizes = lambda cfg: None                 # (the estimate only: the page has no build/exact-sizes.json)
C.exact_builds = lambda: {}


def full(values):
    cfg = C.defaults()
    cfg.update({k: v for k, v in values.items() if k in cfg})
    return cfg


def view(q, costs):
    cfg = full(q["values"])
    ev = C.evaluate({"config": cfg, "costs": costs, "commit": q.get("commit")})
    b = C.budget(cfg, costs, use_exact=False)
    fitted, changes = C.fit(cfg, costs)
    return {"eval": ev, "savings": C.savings_of(cfg, costs),
            "conflicts": {k: [str(e) for e in v] for k, v in C.conflicts(cfg).items()},
            "over": {r: o for r, (_, _, o) in C.fits(b["total"], cfg).items() if o > 0},
            "fit": {"values": fitted, "changes": changes}, "dump": C.dump(cfg, q.get("name", ""))}


def toggle(q, costs):
    before = full(q["values"])
    after = dict(before, **{q["key"]: int(q["value"])})
    room = q.get("room") or {}
    cfg, new = RM.after_toggle(before, after, q["key"], RM.Room(tuple(tuple(x) for x in room.get("dropped", ())),
                                                                 room.get("note", "")), costs)
    return {"values": cfg, "room": {"dropped": [list(x) for x in new.dropped], "note": new.note}}


def text(t):
    said = []
    try:
        cfg, name = C.parse(t, warn=said.append)
    except C.ConfigError as e:
        return {"values": None, "name": "", "warnings": said, "error": str(e)}
    return {"values": cfg, "name": name, "warnings": said, "error": None}


def main():
    req = json.load(sys.stdin)
    costs = C.load_costs()
    out = {"views": [view(q, costs) for q in req.get("views", [])],
           "toggles": [toggle(q, costs) for q in req.get("toggles", [])],
           "texts": [text(t) for t in req.get("texts", [])],
           "registry": C.describe(), "items": len(R.ITEMS)}
    json.dump(out, sys.stdout)


if __name__ == "__main__":
    main()
