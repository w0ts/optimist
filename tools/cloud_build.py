#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The cloud build's steps that read a request (.github/workflows/cloud-build.yml, docs/CLOUD-BUILD.md).

The web builder opens a pre-filled issue (.github/ISSUE_TEMPLATE/cloud-build.yml): a commit and a .config. The
issue body is untrusted text: it is read here, never put into a shell line. The workflow runs this file from the
default branch, so a request cannot change how it is read.

  python3 tools/cloud_build.py request --out DIR      the issue body (env ISSUE_BODY) -> DIR/request.config and the
                                                      commit (a full SHA on a branch of this repository; none given:
                                                      the default branch's HEAD); refused -> DIR/reply.md, exit 1
  python3 tools/cloud_build.py verdict DIR            DIR/eval.json (+ eval.err), what `optimist.py cloud-key --strict
                                                      --json` at that commit said -> the key and the asset stem;
                                                      refused (an unknown key, a rule not met) -> DIR/reply.md, exit 1
  python3 tools/cloud_build.py reply KIND DIR         the comment for KIND (building, done, failed, error) -> DIR/reply.md
  python3 tools/cloud_build.py shelf add SHELF --files OUT --config FILE
                                                      the build (OUT/<STEM>.fwsc, -ui.zip; env KEY STEM NAME COMMIT ISSUE
                                                      SHA256 SUMMARY_FILE) onto the checkout SHELF of the cloud-builds
                                                      branch: its files, <STEM>.config and index.json; then the prune
  python3 tools/cloud_build.py shelf prune SHELF      the builds older than RETAIN_DAYS (env, default 30) off SHELF;
                                                      their stems -> stdout (one a line: the workflow drops their assets)

The cloud-builds branch is where the web installer finds the builds: GitHub's release downloads send no CORS header,
so a page cannot fetch them; raw.githubusercontent.com does. The branch is one commit, replaced at each change (its
history does not grow), and deleted when the last build on it expires.

Outputs for the workflow go to $GITHUB_OUTPUT (KEY=value lines) when it is set, and to stdout.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

RELEASE_TAG = "cloud-builds"            # the pre-release whose assets are the builds, named by asset_stem()
SHELF_BRANCH = "cloud-builds"           # the branch the web installer reads them from (index.json + the files)
RETAIN_DAYS = 30                        # a build not asked for again in this many days leaves the branch and the release
MAX_BODY = 20000                        # an issue body longer than this is not a request of the builder's
SHA = re.compile(r"[0-9a-f]{40}|[0-9a-f]{64}")
KEY = re.compile(r"[0-9a-f]{64}")
NO_RESPONSE = "_No response_"           # what GitHub writes for an empty form field


class Refused(Exception):
    """a request that is not built: the text is the reply (Markdown, plain words)"""


def sections(body):
    """an issue form's body ("### Heading" then the answer) -> {heading: answer}"""
    out, head, lines = {}, None, []
    for line in body.replace("\r\n", "\n").split("\n"):
        m = re.fullmatch(r"### (.+?)\s*", line)
        if m:
            if head is not None:
                out[head] = "\n".join(lines).strip()
            head, lines = m.group(1).strip(), []
        elif head is not None:
            lines.append(line)
    if head is not None:
        out[head] = "\n".join(lines).strip()
    return out


def unfence(text):
    """the content of a ```fenced``` answer (a form field rendered as code), else the text as it is"""
    m = re.fullmatch(r"```[A-Za-z]*\n(.*?)\n?```", text.strip(), re.S)
    return m.group(1) if m else text


def parse_request(body):
    """-> {"commit": SHA or None, "config": .config text}; Refused when it is not a builder request"""
    if not isinstance(body, str) or not body.strip():
        raise Refused("The issue is empty. Open the request from the web builder's **Build in the cloud** button.")
    if len(body) > MAX_BODY:
        raise Refused(f"The request is longer than {MAX_BODY:,} characters; a builder configuration is much shorter.")
    s = sections(body)
    config = unfence(s.get("Configuration", "")).strip()
    if not config or config == NO_RESPONSE:
        raise Refused("The request has no configuration. Open it from the web builder's **Build in the cloud** "
                      "button, or paste a `.config` into the *Configuration* field.")
    commit = s.get("Commit", "").strip()
    if commit in ("", NO_RESPONSE):
        commit = None
    elif not SHA.fullmatch(commit):
        raise Refused("The commit must be a full commit SHA (40 hexadecimal digits) or left empty for the latest.")
    return {"commit": commit, "config": config + "\n"}


def on_a_branch(commit, repo="."):
    """the commit is on a branch of this repository (remote-tracking branches of a full clone): only maintainers
    push branches, so the code built is code they published"""
    p = subprocess.run(["git", "-C", repo, "branch", "-r", "--contains", commit], capture_output=True, text=True)
    return p.returncode == 0 and p.stdout.strip() != ""


def slug(name):
    """the configuration's name in a file name: lower case, a-z 0-9 and dashes, at most 32 characters"""
    s = re.sub(r"[^a-z0-9]+", "-", str(name or "").lower()).strip("-")[:32].strip("-")
    return s or "custom"


def asset_stem(name, key):
    """the release assets of a build: <stem>.fwsc and <stem>-ui.zip; the key's first 16 digits name it (web/builder/
    logic.js assetStem says the same)"""
    if not KEY.fullmatch(key or ""):
        raise ValueError(f"a cloud key: 64 lower-case hex digits, got {key!r}")
    return f"optimist-{slug(name)}-{key[:16]}"


def verdict(ev, err=""):
    """cloud-key --json's result (or its error) -> {"key", "name", "stem"}; Refused on an error"""
    if not ev:
        last = err.strip().splitlines()[-1].removeprefix("error: ") if err.strip() else ""
        raise Refused("The configuration was refused: it could not be read. Fix it in the web builder and send it "
                      "again." + (f"\n\n    {quiet(last)}" if quiet(last) else ""))
    errors = [e["text"] for e in ev.get("errors", [])]
    if errors:
        raise Refused("The configuration was refused (fix it in the web builder and send it again):\n\n" +
                      "\n".join(f"- {e}" for e in errors))
    if not KEY.fullmatch(ev.get("key") or ""):
        raise Refused("The configuration has no cloud key (no commit was given to the check).")
    name = ev.get("name") or ""
    return {"key": ev["key"], "name": name, "stem": asset_stem(name, ev["key"])}


def quiet(text):
    """the checker's message for an indented code block: its own characters only (a request's text in it can be no
    link and no mention), 200 at most"""
    return re.sub(r"[^A-Za-z0-9 _=:,.()-]", "", str(text))[:200].strip()


def safe_name(name):
    """a name for a comment: the firmware's characters only (no Markdown, no mention, no link)"""
    return re.sub(r"[^A-Za-z0-9 +._-]", "", str(name or ""))[:32] or "custom"


def reply(kind, env):
    """the comment text; env: REPO, RUN_URL, COMMIT, KEY, STEM, NAME (strings from the workflow) and SUMMARY_FILE (the
    package's JSON summary) as needed"""
    repo, run = env.get("REPO", ""), env.get("RUN_URL", "")
    if kind == "building":
        return (f"Building **{safe_name(env.get('NAME'))}** from `{env.get('COMMIT', '')[:12]}` "
                f"(cloud key `{env.get('KEY', '')[:16]}`). It takes a few minutes: [follow the build]({run}). "
                "This issue is updated and closed when it is done.")
    if kind == "done":
        base = f"https://github.com/{repo}/releases/download/{RELEASE_TAG}/{env.get('STEM', '')}"
        owner, _, name = repo.partition("/")
        sizes = ""
        if env.get("SUMMARY_FILE") and Path(env["SUMMARY_FILE"]).is_file():
            s = json.loads(Path(env["SUMMARY_FILE"]).read_text()).get("sizes") or {}
            rows = [f"{quiet(r)} {v:,}" for r, v in s.items() if isinstance(v, int)] if isinstance(s, dict) else []
            sizes = "\n\nSizes: " + ", ".join(rows) if rows else ""
        cached = "This configuration was built before: here it is again." if env.get("CACHED") == "true" else "Done."
        site = f"https://{owner.lower()}.github.io/{name}/webapp"
        build_id = env.get("KEY", "")[:16]
        return (f"{cached} Build id `{build_id}`.\n\n"
                f"- **[Install it]({site}/installer/?build={build_id})** with the web installer (or paste the build id "
                "in its *A cloud build* list)\n"
                f"- [Its configuration in the web builder]({site}/builder/?cloud={env.get('STEM', '')})\n"
                f"- Firmware: [{env.get('STEM')}.fwsc]({base}.fwsc) (`python3 tools/optimist.py flash FILE.fwsc`)\n"
                f"- The web editor's sidecar: [{env.get('STEM')}-ui.zip]({base}-ui.zip)\n\n"
                f"Kept {RETAIN_DAYS} days after the last request for it. "
                f"Built from `{env.get('COMMIT', '')[:12]}`, cloud key `{env.get('KEY', '')}`.{sizes}" +
                (f"\n\nSHA-256 of the .fwsc: `{env['SHA256']}`" if re.fullmatch(r"[0-9a-f]{64}", env.get("SHA256", "")) else ""))
    if kind == "failed":
        return (f"The build failed: [the log]({run}). The estimate in the web builder is not exact, so a configuration "
                "near a limit can fail to fit; switch something off (or use **Make it fit** with some margin) and "
                "send it again. If it should have built, leave a comment here.")
    if kind == "error":
        return (f"The request could not be processed (a step of the workflow failed, not your configuration): "
                f"[the log]({run}). A maintainer will look; the issue stays open.")
    raise ValueError(f"reply: unknown kind {kind!r}")


def shelf_entry(env, now):
    """the index.json entry of a build from the workflow's env (KEY, STEM, NAME, COMMIT, ISSUE, SHA256, SUMMARY_FILE)"""
    key, stem = env.get("KEY", ""), env.get("STEM", "")
    if not KEY.fullmatch(key) or not re.fullmatch(r"optimist-[a-z0-9-]{1,32}-[0-9a-f]{16}", stem) or not stem.endswith(key[:16]):
        raise ValueError(f"shelf: a key and its stem, got {key!r} {stem!r}")
    sizes = {}
    if env.get("SUMMARY_FILE") and Path(env["SUMMARY_FILE"]).is_file():
        s = json.loads(Path(env["SUMMARY_FILE"]).read_text()).get("sizes") or {}
        sizes = {quiet(r): v for r, v in s.items() if isinstance(v, int)} if isinstance(s, dict) else {}
    issue = env.get("ISSUE", "")
    return {"id": key[:16], "key": key, "stem": stem, "name": safe_name(env.get("NAME")),
            "commit": env.get("COMMIT", "") if SHA.fullmatch(env.get("COMMIT", "")) else "",
            "date": now.strftime("%Y-%m-%dT%H:%M:%SZ"), "issue": int(issue) if issue.isdigit() else None,
            "sha256": env.get("SHA256", "") if re.fullmatch(r"[0-9a-f]{64}", env.get("SHA256", "")) else "",
            "sizes": sizes, "fwsc": f"{stem}.fwsc", "ui": f"{stem}-ui.zip", "config": f"{stem}.config"}


def shelf_add(builds, entry):
    """-> the builds with this one first (an earlier copy of the same key replaced: its date is the new one; its sizes
    kept when this one has none, a request answered from the cache having no build summary)"""
    old = next((b for b in builds if b.get("key") == entry["key"]), {})
    new = dict(entry, sizes=entry["sizes"] or old.get("sizes") or {})
    return [new] + [b for b in builds if b.get("key") != entry["key"]]


def shelf_prune(builds, now, days=RETAIN_DAYS):
    """-> (kept, dropped): the builds of the last `days` days, and the others"""
    edge = now - timedelta(days=days)
    def fresh(b):
        try:
            return datetime.strptime(b.get("date", ""), "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc) >= edge
        except ValueError:
            return False
    return [b for b in builds if fresh(b)], [b for b in builds if not fresh(b)]


def read_index(shelf):
    f = Path(shelf) / "index.json"
    return json.loads(f.read_text()).get("builds", []) if f.is_file() else []


def write_shelf(shelf, builds, dropped):
    """index.json of the builds kept; the files of the dropped ones removed (those a kept build names stay)"""
    shelf = Path(shelf)
    keep = {b[k] for b in builds for k in ("fwsc", "ui", "config")}
    for b in dropped:
        for k in ("fwsc", "ui", "config"):
            if b.get(k) and b[k] not in keep and "/" not in b[k]:
                (shelf / b[k]).unlink(missing_ok=True)
    (shelf / "index.json").write_text(json.dumps({"version": 1, "retain_days": RETAIN_DAYS, "builds": builds},
                                                 indent=1) + "\n")


def retain_days(env):
    v = env.get("RETAIN_DAYS", "")
    return int(v) if v.isdigit() and int(v) > 0 else RETAIN_DAYS


def cmd_shelf(a):
    shelf, now = Path(a.shelf), datetime.now(timezone.utc)
    shelf.mkdir(parents=True, exist_ok=True)
    builds = read_index(shelf)
    if a.action == "add":
        entry = shelf_entry(os.environ, now)
        for k in ("fwsc", "ui"):
            shutil.copyfile(Path(a.files) / entry[k], shelf / entry[k])
        shutil.copyfile(a.config, shelf / entry["config"])
        builds = shelf_add(builds, entry)
    kept, dropped = shelf_prune(builds, now, retain_days(os.environ))
    write_shelf(shelf, kept, dropped)
    for b in dropped:
        print(b["stem"])
    return 0


def output(**kv):
    lines = "".join(f"{k}={v}\n" for k, v in kv.items())
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
            f.write(lines)
    sys.stdout.write(lines)


def cmd_request(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    try:
        req = parse_request(os.environ.get("ISSUE_BODY", ""))
        commit = req["commit"] or subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True,
                                                 check=True).stdout.strip()
        if not on_a_branch(commit):
            raise Refused(f"`{commit[:12]}` is not a commit of a branch of this repository. Use the web builder of "
                          "the published site, or leave the commit empty for the latest.")
    except Refused as e:
        (out / "reply.md").write_text(str(e) + "\n")
        print(e, file=sys.stderr)
        return 1
    (out / "request.config").write_text(req["config"])
    output(commit=commit)
    return 0


def cmd_verdict(a):
    d = Path(a.dir)
    try:
        text = (d / "eval.json").read_text() if (d / "eval.json").is_file() else ""
        ev = json.loads(text) if text.strip() else None
        err = (d / "eval.err").read_text() if (d / "eval.err").is_file() else ""
        v = verdict(ev, err)
    except (Refused, json.JSONDecodeError) as e:
        msg = str(e) if isinstance(e, Refused) else "The configuration check gave no readable result."
        (d / "reply.md").write_text(msg + "\n")
        print(msg, file=sys.stderr)
        return 1
    output(key=v["key"], stem=v["stem"], name=safe_name(v["name"]))
    return 0


def cmd_reply(a):
    Path(a.dir).mkdir(parents=True, exist_ok=True)
    (Path(a.dir) / "reply.md").write_text(reply(a.kind, os.environ) + "\n")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("request", help="the issue body (env ISSUE_BODY) -> request.config and the commit")
    p.add_argument("--out", required=True)
    p.set_defaults(fn=cmd_request)
    p = sub.add_parser("verdict", help="DIR/eval.json -> the key and the asset stem, or the refusal")
    p.add_argument("dir")
    p.set_defaults(fn=cmd_verdict)
    p = sub.add_parser("reply", help="the comment for a step -> DIR/reply.md")
    p.add_argument("kind", choices=("building", "done", "failed", "error"))
    p.add_argument("dir")
    p.set_defaults(fn=cmd_reply)
    p = sub.add_parser("shelf", help="a build onto the cloud-builds branch's checkout, or the expired ones off it")
    p.add_argument("action", choices=("add", "prune"))
    p.add_argument("shelf")
    p.add_argument("--files", help="add: the directory with <STEM>.fwsc and <STEM>-ui.zip")
    p.add_argument("--config", help="add: the request's .config")
    p.set_defaults(fn=cmd_shelf)
    a = ap.parse_args(argv)
    if a.cmd == "shelf" and a.action == "add" and not (a.files and a.config):
        ap.error("shelf add needs --files and --config")
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
