# SPDX-License-Identifier: GPL-3.0-only
"""What the emulator checks know about the firmware under test: its configuration (build/gen/felucca_config.h of the
tree that built it), its per-track parameter ids (the P_* enum of firmware/src/core/core.h, evaluated with that
configuration) and the panel's key ids.

A package in <tree>/build/ is read with <tree>'s sources (an old commit built in its own worktree is checked with
its own core.h); any other package with this repository's."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# the matrix key ids (fm1-ui's KEYMAP): the 14 buttons, then the 27 note keys from the lowest
BTN = dict(OCTDN=0, OCTUP=1, FX=2, SEL=3, ENV=4, LFO=5, EDIT=6, GLO=7, HOME=8, SAVE=9, ARP=10, SEQ=11, PLAY=12, REC=13)
_WHITE = [14 + k for k in (0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26)]
_BLACK = [14 + k for k in (1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25)]


def white(n):
    """white key n (1..16, from the left)"""
    return _WHITE[n - 1]


def black(n):
    """black key n (1..11: F#3 G#3 A#3 C#4 D#4 F#4 G#4 A#4 C#5 D#5 F#5)"""
    return _BLACK[n - 1]


def source_tree(fw):
    """the tree that built the package: <tree>/build/x.fwsc with <tree>/firmware/src/core/core.h, else ROOT"""
    p = Path(fw).resolve().parent
    if p.name == "build" and (p.parent / "firmware" / "src" / "core" / "core.h").exists():
        return p.parent
    return ROOT


def _defines(text):
    """the #define NAME VALUE lines of a header -> {NAME: expression text} (comments dropped)"""
    out = {}
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    for m in re.finditer(r"^\s*#\s*define\s+(\w+)\s+([^\n]+)$", text, re.M):
        out.setdefault(m.group(1), m.group(2).strip())
    return out


def _eval(expr, env, depth=0):
    """a C preprocessor condition with the names in env (unknown names: 0)"""
    if depth > 20:
        return 0
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda m: "1" if m.group(1) in env else "0", expr)
    e = e.replace("||", " or ").replace("&&", " and ")
    e = re.sub(r"!(?!=)", " not ", e)
    e = re.sub(r"\b(\d+)[uUlL]+\b", r"\1", e)

    def name(m):
        n = m.group(0)
        if n in ("or", "and", "not"):
            return n
        return str(_eval(env[n], env, depth + 1)) if n in env else "0"
    e = re.sub(r"\b[A-Za-z_]\w*\b", name, e)
    try:
        return int(eval(e, {"__builtins__": {}}))           # (a header of the tree under test: arithmetic only)
    except Exception:
        return 0


class Facts:
    def __init__(self, fw):
        self.fw = Path(fw).resolve()
        self.tree = source_tree(fw)
        cfg_h = self.fw.parent / "gen" / "felucca_config.h"
        core_h = self.tree / "firmware" / "src" / "core" / "core.h"
        self.cfg_found = cfg_h.exists()
        self.env = _defines(cfg_h.read_text()) if self.cfg_found else {}
        core = core_h.read_text()
        for k, v in _defines(core).items():
            self.env.setdefault(k, v)
        self.P = self._params(core)

    def define(self, rel, name):
        """a number #defined in a source file of the tree that built the package (rel: from firmware/src), with
        this build's configuration"""
        env = dict(self.env)
        for k, v in _defines((self.tree / "firmware" / "src" / rel).read_text()).items():
            env.setdefault(k, v)
        if name not in env:
            raise RuntimeError(f"{rel}: no #define {name}")
        return _eval(name, env)

    def on(self, name):
        """a configuration switch's value (FELUCCA_ prefix optional)"""
        n = name if name.startswith("FELUCCA_") or name in self.env else "FELUCCA_" + name
        return _eval(n, self.env)

    def _params(self, core):
        m = re.search(r"enum\s*\{[^\n]*per-track parameters[^\n]*\n(.*?)\n\s*P_COUNT", core, re.S)
        if not m:
            raise RuntimeError("core.h: no per-track parameter enum")
        body = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
        ids, n, stack = {}, 0, []
        for line in body.splitlines():
            s = line.strip()
            if s.startswith("#if"):
                stack.append(_eval(s.split(None, 1)[1], self.env) if s.startswith("#if ") else 1)
                continue
            if s.startswith("#endif"):
                stack.pop()
                continue
            if s.startswith("#else"):
                stack[-1] = not stack[-1]
                continue
            if not all(stack):
                continue
            for tok in re.findall(r"\b(P_\w+)\b", s):
                ids[tok] = n
                n += 1
        ids["P_COUNT"] = n
        return ids

    @property
    def ui(self):
        """0 SLOOP's UI, 1 the Optimist UI (the configuration; without it the ELF: the Optimist UI's op_* code)"""
        if self.cfg_found and "FELUCCA_UI" in self.env:
            return self.on("FELUCCA_UI")
        return int(b"\0op_mixer" in self.fw.with_suffix(".elf").read_bytes())
