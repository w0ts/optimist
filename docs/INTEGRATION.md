# Integration: how branches land on `optimist`

Feature work happens on branches, each in its own git worktree (BUILDING.md, Worktrees). A branch is not merged
by the one who wrote it: it is reported ready (`READY <branch> <hash>`), and **one integrator** takes the ready
branches together, merges them on an integration branch, runs the gate once, fixes what it finds and moves
`optimist` forward. This page is the integrator's routine.

## The routine

1. **An integration branch from `optimist`**, in a worktree of its own:
   `git worktree add ../optimist-work/wt/integ -b integrationNN optimist`.
2. **Merge each ready branch** (`git merge --no-ff <branch>`), in the order given; a conflict is resolved in the
   merge, not by editing the branch.
3. **The gate**: `make gate` (below). Any FAIL: fix it on the integration branch (a commit of its own, saying
   which branch it fixes), or hand it back to the branch's author, then run the gate again. Changes that need it
   (a builder item, a config): add `CONFIG=file` or `SET="KEY=V .."` so those builds are checked too.
4. **The emulator checks**: `make emu-check` on the gate's final build (user-default), or `make gate EMU=1` for
   both in one go. They matter most for a branch that touches storage, patterns, the FX slots or the sequencer.
5. **Move `optimist`**: `git checkout optimist && git merge --ff-only integrationNN`. Then push (below).
6. **Costs**: when a merged branch added builder items, `make costs` measures them (the gate's costs step fails
   until `tools/builder/costs.json` has them); commit `costs.json` on the integration branch before step 5.

## The gate

`make gate` (`python tools/optimist.py gate`, `tools/gate.py --help`) runs, in this order, and logs each step to
`build/gate/NN-step.log`:

| step | what | about |
|---|---|---|
| costs | every builder item has a measured cost (`measure_costs.py --missing --check`, as CI) | 1 s |
| profile P | every published profile builds, as CI's profiles job (`tools/gate.py --no-profiles`: user-default only, a quick run) | 10 s each |
| config F / set | the `CONFIG=` file and the `SET=` variant of user-default, when given | 10 s each |
| test | the host tests (`optimist.py test`: a BLE build, user-default, a measurement build of the defaults, `tests/run_tests.sh` with the web tests) | 14 min |
| final | user-default built again, last: `build/` holds its package and ELF for `make emu-check` and `make flash` | 10 s |
| emu | `EMU=1`: the emulator checks on that build (BUILDING.md, The emulator checks) | 7.5 min |

Every step runs even after one failed, so one run lists everything that is wrong; the exit status is 1 on any
FAIL. The gate tests the tree as it is: it says so when there are uncommitted changes, and a gate result belongs
to the commit it names (`gate: PASSED on <hash>`).

The BLE build's radio start-up tables are committed (`firmware/hal/ble_rf_tables_v15.h`, docs/BLE-STACK.md, section
12): a new worktree needs nothing. `FM1_STOCK_FWSC` makes a fresh capture from your stock V15 instead (same content).

## Pushing

- Only the integrator pushes, and only `optimist` (and the integration branch, when someone else must see it),
  after the gate passed on the commit being pushed. Never a force push of `optimist`: every worktree and the
  public repository build on it (its history was rewritten once, 2026-10-08; `rewrite-commit-map.txt` maps the old
  hashes).
- Commits carry the author and committer `566554+hdavid@users.noreply.github.com` (the repository is public: no
  other address), and no `Claude-Session:` trailer. `Co-Authored-By:` lines are fine.
- Never `git stash`: the stash is shared by every worktree of the repository, so one worktree's stash can be
  popped in another. Commit a `wip:` instead.
- A firmware that was not run on a real FM-1 says so in its commit or notes: the emulator checks are not a
  hardware test.

## Where things go

- **Nothing in `/tmp`** (or the system's temp folder): a reboot empties it and has already lost a whole test
  harness. Tools and checks go in the repository (`tools/`, `tests/`); notes, rosters, logs and extra worktrees in
  a folder of their own next to the checkout (here `~/GitHub/optimist-work/`), scratch files named after their
  owner (`integ-*`, `harness-*`).
- Kill only the processes you started (by their PID), never by a name pattern: other sessions build and run the
  emulator at the same time. Run at most four builds at once on one machine.
- Do not touch a USB device (an FM-1 on the cable) unless the work is about that device and its owner said so.
