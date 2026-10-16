#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cloud builds (tools/cloud_build.py, .github/workflows/cloud-build.yml): the issue form's body is read and refused as
it should be, a real profile goes request -> cloud-key --strict --json -> verdict as the workflow runs it, the asset
names are the web builder's (web/builder/logic.js assetStem), and the issue URL the builder opens stays short enough.

  python3 tests/cloud_build_test.py                     (run by tests/run_tests.sh)
"""
import json
import os
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
