#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export the firmware builder for the web builder page (web/builder/, docs/BUILDER-WEB.md): the page runs the same
Python rules (tools/builder/core.py) in the browser with Pyodide, so it needs their source and the data the CLI
reads from the disk.

  python3 tools/export_builder_web.py            write web/builder/data/ (commit it with a change of the builder)
  python3 tools/export_builder_web.py --check    exit 1 when web/builder/data/ is not what the builder has now
  python3 tools/export_builder_web.py --out DIR  write the files into DIR instead (web/make_site.py)

web/builder/data/:
  python.json    the Python files the page loads into Pyodide (core.py, registry.py, backports.py with
                 tools/backports.json, room.py, web_api.py) and needs_auto (configure._needs_auto, read from the
                 firmware headers: the browser has no firmware/)
  registry.json  web_api.registry_view(): the items as the CLI builder has them (the backports included), for the
                 page to list before Python has started, and the drift check
  costs.json     tools/builder/costs.json, as it is
  profiles.json  the shipped profiles (config/profiles/*.config of this checkout, as text) and the flavours
                 (web/flavours.json, without the package globs)
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILDER = ROOT / "tools" / "builder"
sys.path.insert(0, str(BUILDER))
import configure as C  # noqa: E402  (tools/builder: the registry with the backports, needs_auto from the headers)
import web_api  # noqa: E402

OUT = ROOT / "web" / "builder" / "data"
PY_FILES = ("tools/builder/core.py", "tools/builder/registry.py", "tools/builder/backports.py",
            "tools/builder/room.py", "tools/builder/web_api.py", "tools/backports.json")


def _json(obj):
    return json.dumps(obj, indent=1, ensure_ascii=False) + "\n"


def profiles():
    """-> [{"name", "text", "published"}] the profiles shipped in this checkout (not a worktree's shared ones, not
    config/my-profiles: the site publishes what the commit holds)"""
    out = []
    for p in sorted((ROOT / "config" / "profiles").glob("*.config")):
        out.append({"name": p.stem, "text": p.read_text(encoding="utf-8"), "published": C.is_published(p)})
    return out


def flavours():
    """web/flavours.json's entries for the page (a preset each: its profile; "default": true marks the one the page opens
    with, else the first), without the package glob"""
    data = json.loads((ROOT / "web" / "flavours.json").read_text(encoding="utf-8"))
    return [{k: f[k] for k in ("id", "title", "description", "profile", "default") if k in f} for f in data["flavours"]]


def files():
    """-> {name: text} what web/builder/data/ holds"""
    web_api.init(C.load_costs(), C.NEEDS_AUTO)
    py = {"needs_auto": list(C.NEEDS_AUTO),
          "files": {f: (ROOT / f).read_text(encoding="utf-8") for f in PY_FILES}}
    return {"python.json": _json(py), "registry.json": _json(web_api.registry_view()),
            "costs.json": (BUILDER / "costs.json").read_text(encoding="utf-8"),
            "profiles.json": _json({"profiles": profiles(), "flavours": flavours()})}


def write(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in files().items():
        (out / name).write_text(text, encoding="utf-8")
    return out


def check(out=OUT):
    """-> [names] of the files in out that differ from a fresh export (missing included)"""
    bad = []
    for name, text in files().items():
        p = Path(out) / name
        if not p.is_file() or p.read_text(encoding="utf-8") != text:
            bad.append(name)
    return bad


def main(argv):
    if argv[:1] == ["--check"]:
        bad = check()
        if bad:
            print(f"web/builder/data is stale ({', '.join(bad)}): run python3 tools/export_builder_web.py and commit it")
            return 1
        print("web/builder/data: up to date")
        return 0
    if argv[:1] == ["--out"] and len(argv) == 2:
        print(f"builder data: {write(argv[1])}")
        return 0
    if not argv:
        print(f"builder data: {write(OUT)}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
