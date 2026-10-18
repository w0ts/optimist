# Cloud builds

Anyone with a GitHub account can build a firmware from the web builder (docs/BUILDER-WEB.md) without installing
anything: **Build in the cloud** sends the configuration to this repository's GitHub Actions, which build it with the
same toolchain as CI and answer with the download.

## How a user does it

1. Open the builder (<https://w0ts.github.io/optimist/webapp/builder/>, or the editor's Build tab) and make a configuration.
2. **Build in the cloud**. If the same configuration (the same cloud key: the site's commit and the canonical form) was
   built before, the page shows its download at once. Otherwise it opens a GitHub issue, filled in: press
   **Submit new issue**.
3. A workflow answers in the issue: "Building …" with a link to the log, then, a few minutes later, the **build id**
   (the key's first 16 hex digits), an **Install it** link (the installer opened on that build), a link to its
   configuration in the builder, and the downloads `optimist-<name>-<key16>.fwsc` and its `-ui.zip`; it closes the issue. A refused configuration (an unknown key, a
   rule that is not met) is answered at once and closed; a failed build (it does not fit, most often: the page's size
   is an estimate) is answered with the log and stays open.
4. Install it: the issue's **Install it** link, or the web installer's *Choose* > *A cloud build*: the list of the
   builds kept (name, build id, date, commit, flash size), searched by a pasted build id or a name, each with
   *copy id* and *configuration* (the builder opened on it: `webapp/builder/?cloud=<stem>`). Or download the `.fwsc`
   and `python3 tools/optimist.py flash FILE`.

## The parts

| Part | What it does |
|---|---|
| `web/builder/builder.js`, `logic.js` | the button: the cached build (the public releases API, no login), else `cloudIssueUrl` (the form, with `commit` and `config` filled in) |
| `web/make_site.py` | `build-info.json`: `commit` (the site's, for the key) and `repo` (`$GITHUB_REPOSITORY`, so a fork's site asks its own repository) |
| `.github/ISSUE_TEMPLATE/cloud-build.yml` | the form: *Commit* (empty = the default branch's latest) and *Configuration*; label `cloud-build` |
| `.github/workflows/cloud-build.yml` | `request` → `build` → `publish`, `failed` on a failure |
| `tools/cloud_build.py` | reads the body, checks the commit, the verdict, the replies (tests: `tests/cloud_build_test.py`) |
| the `cloud-builds` pre-release | the builds, one `.fwsc` + `-ui.zip` per cloud key |
| `tools/cloud_shelf.sh` | the `cloud-builds` **branch**: the same files, `<stem>.config` and `index.json`; one commit, replaced (tests: `tests/cloud_build_test.py`, a bare repository) |
| `web/installer/installer.js` | *A cloud build*: `loadCloud` (the branch's commit from the API, then `index.json` and the `.fwsc` from raw.githubusercontent.com by that commit), `matchCloud`, the SHA-256 checked before install |

## The workflow

- **request** (read the repository, write issues): refuses an author's request past `MAX_PER_DAY` (5) in 24 hours, then runs `tools/cloud_build.py` from the default branch, so a request
  cannot change how it is read. The issue body reaches it through an environment variable only, never a shell line. The
  commit must be a full SHA on a branch of this repository (only maintainers push branches, so only published code is
  built). That commit's own builder checks the configuration (`optimist.py cloud-key --strict --json`: an unknown key or
  a broken rule refuses it) and gives the key; the asset stem is `optimist-<name slug>-<first 16 hex of the key>`. If
  the release already has that `.fwsc`, the build is skipped.
- **build** (read only, no token in the checkout): checks out that commit, downloads the toolchain (it only restores
  the cache `build.yml` saves, never saves it: `release.yml` and `pages.yml` read that key), `optimist.py
  package --config`, and uploads the two files as an artifact.
- **publish** (write the release and issues): attaches them to `cloud-builds` (created as a pre-release on first use,
  never marked latest; a copy already there from an identical request is kept, never overwritten), answers with the
  links and the package's SHA-256 and closes the issue.
  Then `tools/cloud_shelf.sh add` puts the build (or, answered from the cache, the release's copy, its date renewed)
  onto the `cloud-builds` branch. Why a branch: a release download ends on a host that sends no CORS header, so a web
  page cannot fetch it; raw.githubusercontent.com sends `Access-Control-Allow-Origin: *` (both measured 2026-10-18).
- **prune** (daily at 03:17 UTC, or Actions > cloud-build > Run workflow): `tools/cloud_shelf.sh prune`.
- **failed**: on a failed build or publish, answers with the run's link and leaves the issue open for a maintainer.

A configuration is only integers for registry keys (`core.parse` refuses anything else), so a request can switch
features on and off and nothing more. The checker's own message is quoted in a code block, stripped to plain
characters, so a request's text never becomes a link or a mention in the bot's answer. The token is only in the
steps that call `gh`, never next to code of the requested commit; every action is pinned to a commit SHA. Each issue has its own concurrency group: reopening an issue builds it again.

## Set up once (maintainer)

1. Settings > General > Features: **Issues** on (the form lives in the issue tracker).
2. Create the label: `gh label create cloud-build --description "a web builder cloud build" --color 0e8a16`.
3. Push the default branch with the workflow and the form, and let `pages.yml` deploy the site (the builder must come
   from a commit on GitHub, or its requests name a commit the workflow refuses).
4. Actions need no secret: the workflow uses `GITHUB_TOKEN` with the permissions listed per job.

## Housekeeping

Automatic: a build not asked for again in `RETAIN_DAYS` (30, the workflow's env) leaves the branch and the release
(the daily **prune**, and each **publish**). The branch is always one commit, replaced (force-pushed with a lease; a
run that loses the race starts again), so its history does not grow, and it is deleted when its last build expires.
The daily prune also deletes the release assets of no build on the branch (a day old or more: a build being
published uploads first) and, once the release holds nothing, the release and its tag. The jobs' hand-over
artifacts (`request`, `firmware`) are kept 1 day. So nothing is left behind but the toolchain cache the builds share.
A later request of an expired configuration simply builds it again. By hand: `gh release delete-asset cloud-builds NAME`
(the next prune leaves the branch's copy until it expires). A user can open as many requests as they like; if that
is abused, limit who can open issues (Settings > Moderation) or turn the workflow off (Actions > cloud-build > Disable).
