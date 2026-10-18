#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cloud builds (tools/cloud_build.py, .github/workflows/cloud-build.yml): the issue form's body is read and refused as
it should be, a real profile goes request -> cloud-key --strict --json -> verdict as the workflow runs it, the asset
names are the web builder's (web/builder/logic.js assetStem), and the issue URL the builder opens stays short enough.

  python3 tests/cloud_build_test.py                     (run by tests/run_tests.sh)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from builder_path import ROOT
import cloud_build as CB
import core as K

fails = 0
SHA = "0123456789abcdef0123456789abcdef01234567"
KEY = "ab" * 32


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def refused(body):
    try:
        CB.parse_request(body)
    except CB.Refused as e:
        return str(e)
    return None


def form(commit, config):
    """the body GitHub writes for the form (.github/ISSUE_TEMPLATE/cloud-build.yml: an input, a textarea render: text)"""
    return f"### Commit\n\n{commit}\n\n### Configuration\n\n```text\n{config}\n```\n"


# ---- reading the form
r = CB.parse_request(form(SHA, "# name: my synth\nENG_FM6=1\nBLE=0"))
check("the form: commit and the fenced configuration", r == {"commit": SHA, "config": "# name: my synth\nENG_FM6=1\nBLE=0\n"})
check("an empty commit (GitHub's _No response_) is the latest", CB.parse_request(form("_No response_", "BLE=0"))["commit"] is None)
check("CRLF bodies read the same", CB.parse_request(form(SHA, "BLE=0").replace("\n", "\r\n"))["config"] == "BLE=0\n")
check("refused: an empty issue", refused("") is not None and refused("   ") is not None)
check("refused: no configuration", refused(form(SHA, "")) is not None and refused("### Commit\n\nabc\n") is not None)
check("refused: a short or odd commit", refused(form("abc123", "BLE=0")) is not None and refused(form("main; rm -rf /", "BLE=0")) is not None)
check("refused: a body over the size limit", refused(form(SHA, "A=1\n" * 6000)) is not None)
check("a heading in the configuration does not cut it", CB.parse_request(form(SHA, "# Synth engines\nENG_FM6=1"))["config"].startswith("# Synth"))

# ---- names
check("asset stem: the slug and 16 digits of the key", CB.asset_stem("My Synth!", KEY) == f"optimist-my-synth-{KEY[:16]}")
check("asset stem: no name, an odd name", CB.asset_stem("", KEY).startswith("optimist-custom-") and CB.asset_stem("../..", KEY).startswith("optimist-custom-"))
check("asset stem: long names are cut to 32", len(CB.slug("x" * 80)) == 32)
try:
    CB.asset_stem("a", "nothex")
    check("asset stem: a bad key is an error", False)
except ValueError:
    check("asset stem: a bad key is an error", True)
check("a name in a comment: no Markdown, no mention", CB.safe_name("@all [x](http://e) <b>") == "all xhttpe b")

names = ["My Synth!", "", "../..", "x" * 80, "ACID--303 ", "ümlaut drums", "-a-"]
js = ("import * as L from './web/builder/logic.js'; const n = JSON.parse(process.argv[1]); "
      f"console.log(JSON.stringify(n.map((x) => L.assetStem(x, '{KEY}'))));")
try:
    out = subprocess.run(["node", "--input-type=module", "-e", js, json.dumps(names)], cwd=ROOT, capture_output=True,
                         text=True, check=True).stdout
    check("asset stem: the web builder's (logic.js) is the same", json.loads(out) == [CB.asset_stem(n, KEY) for n in names])
except (OSError, subprocess.CalledProcessError) as e:
    check(f"asset stem: the web builder's (node: {e})", False)

# ---- the verdict on cloud-key's result
v = CB.verdict({"errors": [], "key": KEY, "name": "Pad"})
check("verdict: the key, the stem, the name", v == {"key": KEY, "name": "Pad", "stem": CB.asset_stem("Pad", KEY)})
for what, ev, err in (("a rule not met", {"errors": [{"text": "PATTERNS needs SECTIONS"}], "key": KEY}, ""),
                      ("an unknown key (cloud-key's error)", None, "error: line 2: NOPE is not an item of this builder"),
                      ("no key", {"errors": [], "key": None}, "")):
    try:
        CB.verdict(ev, err)
        check(f"verdict: refused, {what}", False)
    except CB.Refused as e:
        check(f"verdict: refused, {what}", "refused" in str(e) or "no cloud key" in str(e))

# ---- the replies
env = {"REPO": "w0ts/optimist", "RUN_URL": "https://github.com/w0ts/optimist/actions/runs/1", "COMMIT": SHA, "KEY": KEY,
       "STEM": CB.asset_stem("pad", KEY), "NAME": "pad"}
done = CB.reply("done", env)
check("reply done: both downloads, the installer, the key",
      f"/releases/download/{CB.RELEASE_TAG}/{env['STEM']}.fwsc" in done and f"{env['STEM']}-ui.zip" in done
      and "w0ts.github.io/optimist/webapp/installer/" in done and KEY in done)
check("reply done, cached: says it was built before", "built before" in CB.reply("done", dict(env, CACHED="true")))
try:
    CB.verdict(None, "error: line 1: expected KEY=number, got '[get fw](https://evil.example/x) @w0ts'")
    check("verdict: the checker's message is quoted without a link or a mention", False)
except CB.Refused as e:
    check("verdict: the checker's message is quoted without a link or a mention", "](" not in str(e) and "@" not in str(e)
          and "//" not in str(e) and "\n\n    line 1:" in str(e))
check("reply done: the SHA-256 when given, sizes that are not numbers skipped",
      ("a" * 64) in CB.reply("done", dict(env, SHA256="a" * 64)) and "SHA-256" not in CB.reply("done", dict(env, SHA256="x")))
with tempfile.TemporaryDirectory() as t:
    (Path(t) / "s.json").write_text(json.dumps({"sizes": {"flash": None, "ram": 1234}}))
    check("reply done: a summary with a null size still answers", "ram 1,234" in CB.reply("done", dict(env, SUMMARY_FILE=str(Path(t) / "s.json"))))
check("reply error: the run link, the issue stays open", env["RUN_URL"] in CB.reply("error", env))
check("reply building / failed: the run link", env["RUN_URL"] in CB.reply("building", env) and env["RUN_URL"] in CB.reply("failed", env))

check("reply done: the installer opened on the build id, the builder on its configuration",
      f"/webapp/installer/?build={KEY[:16]}" in done and f"/webapp/builder/?cloud={env['STEM']}" in done)

# ---- the cloud-builds branch (the installer's list): add, replace, prune, the files
from datetime import datetime, timedelta, timezone
now = datetime(2026, 10, 18, 12, 0, tzinfo=timezone.utc)
KEY2 = "cd" * 32
stem1, stem2 = CB.asset_stem("synth", KEY), CB.asset_stem("minimal", KEY2)
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    (t / "s.json").write_text(json.dumps({"sizes": {"flash": 526224, "ram": None}}))
    e1 = CB.shelf_entry({"KEY": KEY, "STEM": stem1, "NAME": "Synth [x](y)", "COMMIT": SHA, "ISSUE": "7", "SHA256": "e" * 64,
                         "SUMMARY_FILE": str(t / "s.json")}, now)
    check("shelf entry: id = the key's 16 digits, the files by the stem, safe name, numeric sizes only",
          e1["id"] == KEY[:16] and e1["fwsc"] == f"{stem1}.fwsc" and e1["config"] == f"{stem1}.config"
          and "[" not in e1["name"] and e1["sizes"] == {"flash": 526224} and e1["issue"] == 7 and e1["date"] == "2026-10-18T12:00:00Z")
    try:
        CB.shelf_entry({"KEY": KEY, "STEM": "../../evil", "COMMIT": SHA}, now)
        check("shelf entry: a stem that is not the key's is refused", False)
    except ValueError:
        check("shelf entry: a stem that is not the key's is refused", True)
    e2 = CB.shelf_entry({"KEY": KEY2, "STEM": stem2, "NAME": "minimal", "COMMIT": SHA}, now - timedelta(days=31))
    builds = CB.shelf_add(CB.shelf_add([], e2), e1)
    again = CB.shelf_add(builds, dict(e1, date="2026-10-19T00:00:00Z"))
    check("shelf add: newest first; the same key again replaces it (its date renewed, its sizes kept from the cache)",
          [b["key"] for b in builds] == [KEY, KEY2] and len(again) == 2 and again[0]["date"] == "2026-10-19T00:00:00Z"
          and CB.shelf_add(builds, dict(e1, sizes={}))[0]["sizes"] == {"flash": 526224})
    kept, dropped = CB.shelf_prune(builds, now)
    check(f"shelf prune: a build older than {CB.RETAIN_DAYS} days goes, a fresh one stays; a bad date goes",
          [b["key"] for b in kept] == [KEY] and [b["key"] for b in dropped] == [KEY2]
          and CB.shelf_prune([dict(e1, date="?")], now)[0] == [])
    # the command, as the workflow runs it: add onto an empty checkout, then a prune that empties it
    shelf, out = t / "shelf", t / "out"
    out.mkdir()
    for n in (f"{stem1}.fwsc", f"{stem1}-ui.zip"):
        (out / n).write_bytes(b"x")
    (t / "request.config").write_text("ENG_FM6=0\n")
    shelf.mkdir()
    (shelf / f"{stem2}.fwsc").write_bytes(b"old")
    (shelf / "index.json").write_text(json.dumps({"builds": [e2]}))
    run_env = dict(os.environ, KEY=KEY, STEM=stem1, NAME="synth", COMMIT=SHA, ISSUE="7", SHA256="e" * 64)
    q = subprocess.run([sys.executable, str(ROOT / "tools" / "cloud_build.py"), "shelf", "add", str(shelf), "--files", str(out),
                        "--config", str(t / "request.config")], capture_output=True, text=True, env=run_env)
    idx = json.loads((shelf / "index.json").read_text())
    check("shelf add (command): the files and the .config on the branch, the index, the expired build dropped (printed)",
          q.returncode == 0 and (shelf / f"{stem1}.fwsc").is_file() and (shelf / f"{stem1}.config").read_text() == "ENG_FM6=0\n"
          and [b["id"] for b in idx["builds"]] == [KEY[:16]] and not (shelf / f"{stem2}.fwsc").exists()
          and q.stdout.split() == [stem2])
    q = subprocess.run([sys.executable, str(ROOT / "tools" / "cloud_build.py"), "shelf", "prune", str(shelf)], capture_output=True,
                       text=True, env=dict(os.environ, RETAIN_DAYS="0"))
    q2 = subprocess.run([sys.executable, str(ROOT / "tools" / "cloud_build.py"), "shelf", "prune", str(shelf)], capture_output=True,
                        text=True, env=dict(os.environ))
    check("shelf prune (command): RETAIN_DAYS=0 is not a setting (default kept); the fresh build stays",
          q.returncode == 0 and q.stdout == "" and q2.stdout == "" and (shelf / f"{stem1}.fwsc").is_file())

# ---- tools/cloud_shelf.sh against a bare repository (GitHub's part: SHELF_URL, a stub gh that logs its calls)
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    subprocess.run(["git", "init", "-q", "--bare", str(t / "remote.git")], check=True)
    (t / "gh").write_text(f'#!/bin/sh\necho "$@" >> {t / "gh.log"}\n')
    (t / "gh").chmod(0o755)
    run = t / "run"
    (run / "tools").mkdir(parents=True)
    for f in ("cloud_build.py", "cloud_shelf.sh"):
        (run / "tools" / f).write_bytes((ROOT / "tools" / f).read_bytes())
    (run / "work").mkdir()
    (run / "work" / "request.config").write_text("ENG_FM6=0\n")
    sh_env = dict(os.environ, SHELF_URL=f"file://{t / 'remote.git'}", GH=str(t / "gh"), COMMIT=SHA, ISSUE="7",
                  GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")

    def shelf_sh(action, **env):
        return subprocess.run(["bash", "tools/cloud_shelf.sh", action], cwd=run, capture_output=True, text=True,
                              env=dict(sh_env, **env))

    def remote(*args):
        return subprocess.run(["git", "--git-dir", str(t / "remote.git"), *args], capture_output=True, text=True).stdout

    results = []
    for key, stem in ((KEY, stem1), (KEY2, stem2)):
        shutil.rmtree(run / "out", ignore_errors=True)
        (run / "out").mkdir()
        for n in (f"{stem}.fwsc", f"{stem}-ui.zip"):
            (run / "out" / n).write_bytes(key.encode())
        results.append(shelf_sh("add", KEY=key, STEM=stem, NAME="x"))
    files = remote("ls-tree", "--name-only", "cloud-builds").split()
    idx = json.loads(remote("show", "cloud-builds:index.json") or "{}")
    check("cloud_shelf.sh add: a new branch, then a second build; one commit, both builds, the index newest first",
          all(r.returncode == 0 for r in results) and remote("rev-list", "--count", "cloud-builds").strip() == "1"
          and {f"{stem1}.fwsc", f"{stem2}-ui.zip", f"{stem2}.config", "index.json"} <= set(files)
          and [b["id"] for b in idx.get("builds", [])] == [KEY2[:16], KEY[:16]])
    r = shelf_sh("prune")
    check("cloud_shelf.sh prune: nothing expired, nothing pushed", r.returncode == 0 and "nothing expired" in r.stdout
          and not (t / "gh.log").exists())
    # (expire them: rewrite the dates on the branch as another run would have left them)
    work = t / "edit"
    subprocess.run(["git", "clone", "-q", f"file://{t / 'remote.git'}", "-b", "cloud-builds", str(work)], check=True)
    old = json.loads((work / "index.json").read_text())
    old["builds"] = [dict(b, date="2020-01-01T00:00:00Z") for b in old["builds"]]
    (work / "index.json").write_text(json.dumps(old))
    subprocess.run(["git", "-C", str(work), "commit", "-qam", "old"], check=True, env=sh_env)
    subprocess.run(["git", "-C", str(work), "push", "-q", "origin", "cloud-builds"], check=True)
    r = shelf_sh("prune")
    log = (t / "gh.log").read_text() if (t / "gh.log").exists() else ""
    check("cloud_shelf.sh prune: the last builds expired -> the branch deleted, their release assets deleted",
          r.returncode == 0 and remote("branch", "--list", "cloud-builds").strip() == ""
          and f"release delete-asset cloud-builds {stem1}.fwsc -y" in log and f"{stem2}-ui.zip" in log)

# ---- the workflow's steps on a real profile, at this checkout's HEAD
profile = (ROOT / "config" / "profiles" / "synth.config").read_text()
head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
with tempfile.TemporaryDirectory() as t:
    work = Path(t)
    req = CB.parse_request(form(head, profile.rstrip("\n")))
    (work / "request.config").write_text(req["config"])
    p = subprocess.run([sys.executable, str(ROOT / "tools" / "optimist.py"), "cloud-key", str(work / "request.config"),
                        "--commit", head, "--strict", "--json"], capture_output=True, text=True)
    (work / "eval.json").write_text(p.stdout)
    (work / "eval.err").write_text(p.stderr)
    env_out = dict(os.environ, GITHUB_OUTPUT=str(work / "out.txt"))
    q = subprocess.run([sys.executable, str(ROOT / "tools" / "cloud_build.py"), "verdict", str(work)], capture_output=True,
                       text=True, env=env_out)
    outs = dict(line.split("=", 1) for line in (work / "out.txt").read_text().splitlines()) if q.returncode == 0 else {}
    ref = json.loads(p.stdout)["key"] if p.stdout.strip() else None
    check("the workflow's steps: synth.config -> a key and a stem", q.returncode == 0 and outs.get("key") == ref
          and outs.get("stem", "").endswith(ref[:16]))
    (work / "request.config").write_text(req["config"] + "NOT_AN_ITEM=1\n")
    p = subprocess.run([sys.executable, str(ROOT / "tools" / "optimist.py"), "cloud-key", str(work / "request.config"),
                        "--commit", head, "--strict", "--json"], capture_output=True, text=True)
    (work / "eval.json").write_text(p.stdout)
    (work / "eval.err").write_text(p.stderr)
    q = subprocess.run([sys.executable, str(ROOT / "tools" / "cloud_build.py"), "verdict", str(work)], capture_output=True, text=True)
    check("the workflow's steps: an unknown key is refused, with a reply", q.returncode == 1 and (work / "reply.md").is_file()
          and "NOT_AN_ITEM" in (work / "reply.md").read_text())

# ---- the URL the builder opens: GitHub refuses very long ones (about 8 KB); a dump of every key stays under
cfg, _ = K.parse("")
text = K.dump(cfg, "a long configuration name", full=True)
url_js = ("import * as L from './web/builder/logic.js'; const [t] = JSON.parse(process.argv[1]); "
          f"console.log(L.cloudIssueUrl('w0ts/optimist', '{SHA}', t, 'a long configuration name'));")
try:
    url = subprocess.run(["node", "--input-type=module", "-e", url_js, json.dumps([text])], cwd=ROOT, capture_output=True,
                         text=True, check=True).stdout.strip()
    check(f"the issue URL of the everything-on configuration: {len(url):,} characters (< 8,000)", len(url) < 8000
          and "template=cloud-build.yml" in url and f"commit={SHA}" in url)
except (OSError, subprocess.CalledProcessError) as e:
    check(f"the issue URL (node: {e})", False)

print("cloud build: " + ("all ok" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
