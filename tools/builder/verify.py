# SPDX-License-Identifier: GPL-3.0-only
"""Verify the builder: every profile and N random configurations build, link and fit; nothing of an item left
out stays in the ELF (registry symbols); the host goldens of the items present are bit-identical
(tests/regress.c with the configuration's header); optionally the emulator boots each image silent.

  python3 tools/builder/verify.py [--random N] [--seed S] [--emu] [--out DIR]
Writes DIR/report.json and, per configuration, DIR/<name>/ (felucca.fwsc when it fits, sizes.json, build log)."""
import argparse
import json
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import configure as C  # noqa: E402
import registry as R  # noqa: E402

ROOT = C.ROOT
EMU = Path.home() / "GitHub" / "fm1-emulator-boot" / "rust-emulator"


def symbols(elf):
    out = subprocess.run(["nm", "-S", str(elf)], capture_output=True, text=True).stdout
    syms = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 4:
            syms[p[3]] = int(p[1], 16)
    return syms


def symbol_check(cfg, syms):
    """-> [problems]: a symbol of an item left out still in the image (> 4 B)"""
    bad = []
    for k, it in R.ITEMS.items():
        if C.built(cfg, k) or not it.symbols:
            continue
        if it.parent and not C.built(cfg, it.parent):
            continue                                    # (the parent's own check covers it)
        for s in it.symbols:
            if syms.get(s, 0) > 4:
                bad.append(f"{k} off but {s} ({syms[s]} B) is in the image")
    return bad


SOURCES = [k for k, it in R.ITEMS.items() if (it.group == "Synth engines" and not it.parent) or
           k in ("DRUM_SYNTH", "DRUM_SAMPLED") or k.startswith(("KIT_", "SET_"))]


def regress_bin(cfg, tag):
    """tests/regress.c built with this configuration's header -> its path (or None, the compiler's output)"""
    hdr = ROOT / "build" / "host" / f"cfg_{tag}.h"
    exe = ROOT / "build" / "host" / f"regress_{tag}"
    hdr.parent.mkdir(parents=True, exist_ok=True)
    hdr.write_text(C.header(cfg, tag))
    p = subprocess.run(["cc", "-O2", "-w", "-include", str(hdr), "-Ibuild/gen", "-Ifirmware/src", "-o", str(exe),
                        "tests/regress.c", "-lm"], cwd=ROOT, capture_output=True, text=True)
    return (exe, "") if not p.returncode else (None, p.stderr[-800:])


def regress_summary(out):
    m = re.search(r"(\d+) golden renders \((\d+) changed, (\d+) gone\), (\d+) health failures, (\d+) voice", out)
    return tuple(map(int, m.groups())) if m else None


def goldens(cfg):
    """Invariance: leaving a sound source out (an engine, a kit, a sample set) must not change any other sound.
    The reference is this configuration with every sound source back (its FX and features as they are: a
    shorter delay or FX left out change the sends' renders on purpose); its renders become a golden file,
    and this configuration's renders of the sounds it has must equal them bit for bit"""
    ref = dict(cfg)
    for k in SOURCES:
        ref[k] = R.ITEMS[k].default if k != "ENG_SLICE" else cfg[k]
    rexe, err = regress_bin(ref, "ref")
    if not rexe:
        return False, "regress (reference) does not compile: " + err
    gold = ROOT / "build" / "host" / "golden_ref.txt"
    shutil.copy(ROOT / "tests" / "golden.txt", gold)
    env = dict(__import__("os").environ, GOLDEN_UPDATE="1")
    subprocess.run([str(rexe), str(gold), "/dev/null"], cwd=ROOT, capture_output=True, text=True, env=env)
    cexe, err = regress_bin(cfg, "cfg")
    if not cexe:
        return False, "regress does not compile: " + err
    p = subprocess.run([str(cexe), str(gold), "/dev/null"], cwd=ROOT, capture_output=True, text=True)
    s = regress_summary(p.stdout)
    if not s:
        return False, "regress: no summary\n" + (p.stdout + p.stderr)[-800:]
    n, changed, gone, health, voice = s
    ok = changed == 0 and health == 0 and voice == 0 and n > 0
    msg = f"{n} renders = the reference, {changed} changed, {gone} gone (left out), {health} health, {voice} voice"
    if not ok:
        bad = [ln for ln in p.stdout.splitlines() if "CHANGED" in ln or "FAIL" in ln][:6]
        msg += "\n      " + "\n      ".join(bad)
    return ok, msg


def emu_boot(fwsc, outdir):
    """boot the image in the emulator with no input: the audio must stay silent (rms 0)"""
    exe = EMU / "target" / "release" / "examples" / "play_check"
    if not exe.exists():
        return None, f"no emulator play_check at {exe}"
    p = subprocess.run([str(exe), str(fwsc), "run:2", "level:1"], cwd=EMU, capture_output=True, text=True,
                       timeout=900, env={"FM1_CPU_MHZ": "96", "PATH": "/usr/bin:/bin"})
    (outdir / "emu.log").write_text(p.stdout + p.stderr)
    m = re.search(r"audio: (\d+) frames, rms ([0-9.]+)", p.stdout)
    if p.returncode or not m:
        return False, f"emulator: exit {p.returncode}, no level (emu.log)"
    if int(m.group(1)) == 0:
        return False, "emulator: no audio frames (the audio did not start)"
    m = re.search(r"rms ([0-9.]+)", p.stdout)
    return float(m.group(1)) == 0.0, f"emulator boot: rms {m.group(1)}"


def random_config(rng):
    cfg = C.defaults()
    for k, it in R.ITEMS.items():
        if it.experimental or k in ("OTA",):
            continue
        if it.is_choice:
            cfg[k] = rng.choice([c[0] for c in it.choices if not (k == "USB_MODE" and c[0] == 2)])
        else:
            cfg[k] = 1 if rng.random() < 0.7 else 0
    if not any(C.built(cfg, k) for k in R.ITEMS if k.startswith("ENG_") and not R.ITEMS[k].parent):
        cfg["ENG_ANALOG"] = 1
    if not any(cfg[k] for k in ("FM6_MARK1", "FM6_MODERN", "FM6_OPL")):
        cfg["FM6_MARK1"] = 1
    if not cfg["DRUM_SYNTH"] and not C.built(cfg, "DRUM_SAMPLED"):
        cfg["DRUM_SYNTH"] = 1
    if cfg.get("MOTION") and cfg["SECTIONS"] != 4:       # (motion keeps its data beside the four slots)
        cfg["SECTIONS"] = 4
    return cfg


def run_one(name, cfg, out, emu, costs):
    d = out / re.sub(r"[^A-Za-z0-9._-]", "_", name)
    d.mkdir(parents=True, exist_ok=True)
    est = C.budget(cfg, costs)["total"] if costs else None
    ok, sizes, log = C.build(cfg, name, measure=False, log=d / "build.log")
    rec = {"name": name, "config": C.dump(cfg, name), "estimate": est, "exact": sizes and
           {k: sizes[k] for k in C.REGIONS}, "built": ok, "problems": []}
    if not ok:
        rec["problems"].append("build failed (does not fit or does not compile): see build.log")
        okm, sizes, _ = C.build(cfg, name, measure=True, log=d / "measure.log")
        rec["exact"] = sizes and {k: sizes[k] for k in C.REGIONS}
        rec["measure_links"] = okm
    else:
        shutil.copy(ROOT / "build" / "felucca.fwsc", d / "felucca.fwsc")
    if sizes:
        (d / "sizes.json").write_text(json.dumps(sizes, indent=1))
    rec["problems"] += symbol_check(cfg, symbols(ROOT / "build" / "felucca.elf"))
    g_ok, g_msg = goldens(cfg)
    rec["goldens"] = g_msg
    if not g_ok:
        rec["problems"].append("goldens: " + g_msg)
    if emu and ok:
        e_ok, e_msg = emu_boot(d / "felucca.fwsc", d)
        rec["emulator"] = e_msg
        if e_ok is False:
            rec["problems"].append(e_msg)
    print(f"{name:28s} {'OK ' if not rec['problems'] else 'BAD'} built={ok} exact={rec['exact']} "
          f"est={est} | {g_msg}" + "".join(f"\n    {p}" for p in rec["problems"]), flush=True)
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--random", type=int, default=6)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--emu", action="store_true")
    ap.add_argument("--only", nargs="*", help="profiles to run (default: all)")
    ap.add_argument("--out", default=str(ROOT / "build" / "verify"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    costs = C.load_costs()
    recs = []
    for p in C.profile_names():
        if a.only is not None and p not in a.only:
            continue
        cfg, name = C.load_profile(p)
        recs.append(run_one(p, cfg, out, a.emu, costs))
    rng = random.Random(a.seed)
    for i in range(a.random):
        cfg = random_config(rng)
        fitted, _ = C.fit(cfg, costs)
        recs.append(run_one(f"random-{a.seed}-{i}", fitted or cfg, out, a.emu, costs))
    (out / "report.json").write_text(json.dumps(recs, indent=1))
    bad = [r["name"] for r in recs if r["problems"]]
    print(f"verify: {len(recs) - len(bad)} / {len(recs)} clean" + (f"; problems: {', '.join(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
