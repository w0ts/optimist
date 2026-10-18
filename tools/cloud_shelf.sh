#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The cloud-builds branch, where the web installer lists and fetches the cloud builds (docs/CLOUD-BUILD.md): run by
# .github/workflows/cloud-build.yml from the default branch's checkout, with GH_TOKEN (contents: write) and REPO.
#
#   bash tools/cloud_shelf.sh add      out/<STEM>.fwsc, -ui.zip and work/request.config onto the branch (env as for
#                                      tools/cloud_build.py shelf add), then the expired builds off it
#   bash tools/cloud_shelf.sh prune    the expired builds off the branch (the daily run)
#
# The branch is one commit, replaced each time (its history does not grow) and deleted when its last build expires;
# the assets of the expired builds leave the cloud-builds release too. A run that loses the race to another one (the
# lease) starts again from the branch as that one left it. SHELF_URL and GH stand in for GitHub in
# tests/cloud_build_test.py (a bare repository, a stub gh).
set -euo pipefail
action="${1:?add or prune}"
url="${SHELF_URL:-https://github.com/$REPO}"
gh="${GH:-gh}"
if [ -z "${SHELF_URL:-}" ]; then
  "$gh" auth setup-git
  git config --global user.name "github-actions[bot]"
  git config --global user.email "41898282+github-actions[bot]@users.noreply.github.com"
fi

done=0
for try in 1 2 3 4 5; do
  rm -rf shelf
  old="$(git ls-remote --heads "$url" cloud-builds | cut -f1)"
  if [ -n "$old" ]; then
    git clone -q --depth 1 --branch cloud-builds "$url" shelf
  else
    git init -q -b cloud-builds shelf
    git -C shelf remote add origin "$url"
  fi
  if [ "$action" = add ]; then
    python3 tools/cloud_build.py shelf add shelf --files out --config work/request.config > dropped.txt
  else
    python3 tools/cloud_build.py shelf prune shelf > dropped.txt
    if [ ! -s dropped.txt ]; then echo "nothing expired"; done=1; break; fi
  fi
  n="$(python3 -c 'import json, sys; print(len(json.load(open(sys.argv[1]))["builds"]))' shelf/index.json)"
  if [ "$n" = 0 ]; then
    if [ -z "$old" ] || git -C shelf push -q --force-with-lease="cloud-builds:$old" origin :cloud-builds; then
      echo "no build left: the branch is gone"; done=1; break
    fi
  else
    git -C shelf checkout -q --orphan next
    git -C shelf add -A
    git -C shelf commit -q -m "Cloud builds: $n (tools/cloud_shelf.sh)"
    if git -C shelf push -q --force-with-lease="cloud-builds:$old" origin next:cloud-builds; then
      echo "cloud-builds: $n builds"; done=1; break
    fi
  fi
  echo "the branch moved meanwhile (try $try): again"
  sleep $((try * 5))
done
[ "$done" = 1 ] || { echo "could not update the cloud-builds branch" >&2; exit 1; }

while read -r stem; do
  [ -n "$stem" ] || continue
  for f in "$stem.fwsc" "$stem-ui.zip"; do
    "$gh" release delete-asset cloud-builds "$f" -y || echo "no asset $f in the release (already gone)"
  done
  echo "expired: $stem"
done < dropped.txt

# the daily run also leaves nothing else behind: release assets of no build on the branch, then the release and its
# tag once it holds no asset
if [ "$action" = prune ]; then
  if "$gh" release view cloud-builds --json assets > assets.json; then
    python3 tools/cloud_build.py shelf orphans shelf < assets.json > orphans.txt
    while read -r f; do
      [ -n "$f" ] || continue
      "$gh" release delete-asset cloud-builds "$f" -y || echo "no asset $f in the release (already gone)"
      echo "orphan: $f"
    done < orphans.txt
    left="$("$gh" release view cloud-builds --json assets --jq '.assets | length')"
    if [ "$left" = 0 ]; then
      "$gh" release delete cloud-builds --cleanup-tag -y
      echo "the release held nothing: deleted, with its tag"
    fi
  else
    echo "no cloud-builds release"
  fi
fi
