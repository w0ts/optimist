# The fast USB-MIDI install

An install over USB-MIDI has two steps (docs/INSTALL-PATHS.md): the running firmware checks the package and stages the
package's own update loader (step 1, a few dozen reads), then that loader writes the app area (step 2). Step 2 is where
the time goes, and it is run by the loader **of the package being installed**, so a package with this loader is fast on
its own first install when the host is new too.

## Today's protocol (every host and loader before this)

The device asks, the host answers: `cmd 0x30` read requests of at most 512 bytes, stop and wait, with a 10 ms pause in
every host before each answer. The loader reads the whole `flash.bin` twice (once to check its CRC16 before anything is
erased, once to write it) and polls USB every 0.5 ms, one 64-byte packet per poll.

## The fast install (both loaders: `firmware/loader`, `fm1base/bsp/loader`)

1. Before step 2's upgrade command the host sends the word `cmd 0x31 [1, 4096 LE]` (framed as every host command:
   `F0 pack7(00 59 31 03 00 00 01 00 10 chk) F7`, chk = ~sum of the body). A loader that can answers the same frame with
   its read size (4096). No answer within 0.5 s: today's protocol, unchanged. The word holds for the next session only.
2. The loader reads the table at `0xD0000000 + flash.bin's offset`, `6 x sectors` bytes: per 4 KiB sector of flash.bin
   its CRC16 (XMODEM, the UFW's) and CRC-32 (zlib), little-endian. The CRC16s combined must give the UFW entry's CRC16 of
   the whole flash.bin, so a damaged package or table is refused before anything is erased.
3. Per app sector: the flash already has both CRCs (48 bits): kept, nothing sent. Else the sector is erased while it
   comes in (one 4 KiB read), checked against both CRCs (three tries, never written otherwise), programmed and verified
   byte for byte. A cut-off install resumes the same way: what was written is skipped.
4. `0xF0000000` "success", as before.

Hosts: `tools/fm1_install.py` (`fast_hello`, `sector_table`, `answer`), `web/fm1ota_optimist.js` (`OptimistUpdater.write`,
`serveFast`), the app's `FlashProtocol` / `FlashUpdater`. No pause before an answer in the fast install; 50 ms instead of
2 s after the upgrade command. While it waits for an answer the loader empties EP1 as soon as a packet is there.

Compatibility: a new host and an older loader: the word is ignored, today's protocol. An older host and a new loader: no
word, today's protocol (with the faster EP1 polling). Step 1 is unchanged everywhere.

## The bar while the loader checks

A resumed or repeated fast install spends its first second or so in the loader comparing the sectors already on the FM-1
with the table, with no read request; the hosts' bar used to sit at 0 % and jump at the first read. Now (`CheckBar` in
`tools/fm1_install.py`, `OptCheckBar` in `web/fm1ota_optimist.js`, `FlashCheckBar` in the app), from the upgrade command:

- "checking what is already on the FM-1" (CLI) / "Checking what is already on the FM-1…" (web, app).
- The loader may say how far it is: `cmd 0x32 [checked LE16, total LE16]`, framed as every loader frame
  (`F0 pack7(00 59 32 04 00 00 c c t t chk) F7`), at most every 8 sectors or ~100 ms during the fast path's check. No opt-in:
  only a host that sent the fast hello gets the fast path, and every such host skips frames that are no read request. The
  fast hello stays the 10 bytes above: both loaders take it only when the decoded frame is exactly 10 bytes
  (`ota_service() == 10u`), so a longer hello would silently give today's protocol. The bar is checked / total.
- No 0x32 frame within 0.5 s (an older loader): the bar moves by time at 12 ms a sector. The check is a bitwise CRC16 and a
  bitwise CRC-32 over each 4 KiB read from flash; about 7 ms a sector on the FM-1 (a resumed install took ~1 s for ~147
  sectors, 2026-11-01; not measured per sector). 12 ms is slower on purpose, so the estimate stays behind the loader.
- At the first read request at A the bar shows A / the image at once (the sectors before it are right), then the write's
  addresses. It never goes back.

The loaders' own bar (the FM-1's screen) already counts kept sectors: `firmware/loader/ldr_core.c` and fm1-base's
`bsp/loader/ldr_core.c` call `ldr_progress` for every sector of the fast pass, kept or written.

## Cable dropped

When the FM-1 disappears mid-install (the port goes: a cable pulled or loose), the installer says so and goes on **by
itself** as soon as the FM-1 answers again (the user, 2026-11-01: "the flasher should automatically resume"); nothing to
press. It looks for the FM-1 every 0.5 s, with no time limit, until it is back or the install is cancelled:

- CLI: "Connection dropped. Check the cable: the install continues by itself when the FM-1 is back. Do not power off the
  FM-1. (q to cancel)" (q, quit or cancel, then Enter, cancels). With `--yes`, or without a terminal, the same sentence
  without "(q to cancel)"; it waits up to 10 minutes, then exits 4 with "the FM-1 did not come back within 10 minutes.
  Connection dropped. Check the cable, then run the same command again to resume.", and the same command resumes.
- Web installer and the Mirror app: "Connection dropped. Check the cable: the install continues by itself when the FM-1 is
  back. Do not power off the FM-1." with a Cancel button.
- Every drop says it again, no limit.

**Do not power off** (the user: "when flashing is paused, we should still have the DO NOT POWER OFF until the flashing is
finished"): from step 2 on until done, every host message says so. CLI: "switching to update mode: do not power off the
FM-1 until it says done", and the checking / writing lines end "(do not power off the FM-1)" (`tools/progress.py` reads
them; the builder's label "writing, do not unplug or power off"). Web and app: every step-2 status ends "Do not power off
the FM-1.". A cancel during step 2: "Cancelled. Nothing more was sent. The FM-1 stays in update mode: keep it on and press
Install again to finish." (web, app), "cancelled: the FM-1 stays in update mode: keep it on and run the install again to
finish" (CLI). In step 1: "Cancelled. Nothing was written." / "cancelled: nothing was written".

What the FM-1 is after a drop, and what resume does (`resume_plan` / `resumePlan` / `FlashResume.plan`, the same table):

- **Step 2 (the loader writing).** The update record stays (also over a power cycle): the FM-1 comes back in its loader.
  Two cases, both handled:
  - The loader **kept its session** (the FM-1, 2026-11-01: the cable back within its read tries, 3 x 5 s): it is still in
    `ota_read`, which takes only the identity (0x11) and answers (0x30), so the new fast hello and upgrade command go
    unheard and it asks its next 4 KiB read again. A host that took the silence for an older loader refused that read
    ("the device asked for 0x18400+4096, outside the package"; nothing wrong was written). Now, with no answer to the hello,
    the hosts serve today's protocol until a 4 KiB or table read comes, then the fast install's way from there ("the
    loader goes on with its fast install where it was"). An older loader only ever asks 512 bytes, so it is not mistaken.
  - The loader **started afresh** (its tries ran out, or a power cycle): it answers the hello, runs the fast path again
    and skips every 4 KiB sector already right, so only the rest is sent.
  If it comes back running the package, the loader had finished: done, nothing sent.
- **Step 1 (the running firmware staging the loader).** Nothing was committed: after its read timeouts (3 x 5 s) the
  firmware erases the staging area and carries on (`firmware/src/system/ota.c`). Back running the same identity as before
  step 1: step 1 again. Back in our loader (the drop came after the commit): step 2.
- **Anything else** (another firmware's loader, another identity or model, the old firmware after step 2): it stops with
  nothing more sent (`foreign` / `unexpected`, exit 6). Never `--force`; the upgrade command goes to a running firmware
  only when it is the one that ran before step 1.

With two FM-1s on the cable, resume asks only the chosen one: a port of the other units' keys is never asked, nor a port
with another FM1-XXXX than the chosen one's; a port without a token that is none of the others is asked (a loader may
come back under another name). Only a dropped port counts: a loader that stops answering (`stopped`), a refusal or a
missing loader after step 1 (`noloader`, the -5 fallback's code) end the install as before.

**After step 1** the hosts look for the loader at once, every 0.2 s, within the same 33 s as before (it was a blind 3 s
wait, then a look every second): the running firmware that still answers before it restarts is not the loader. Kept
waits: 0.3 s after opening a port in the CLI (CoreMIDI may drop a message sent just after opening, `MidoLink`) and the fast
hello's 0.5 s (an older loader never answers it: that silence is how it is told apart).

Tests: `tests/install_test.py` (`dropped`, `progress`, `loader_poll`), `web/test_web.mjs` (`dropped`, `checkBar`,
`loaderPoll`), `web/e2e_installer.mjs`, `FlashTests.swift` (`testDrop*`, `testCheckFrameAndBar`,
`testBarMovesWhileTheLoaderChecks`, `testTheLoaderIsLookedForAtOnce`), `tests/install_replay_test.py` against the real loader
code: dropped twice and resumed by itself, a loader that kept its fast session (the FM-1's case), cancelled.

Tests: `tests/ldr_test.c` and `fm1base/tests/ldr_test.c` built with `-DOTA_FAST_CHUNK` / `-DLDR_FAST_CHUNK` (the table,
changed sectors only, resume, damaged package, the vectors), `tests/install_replay_test.py` (fm1_install.py against the
real loader code on a pipe: old loader, old host, fast, cut off and resumed), `tests/install_test.py`, `web/test_web.mjs`,
`FlashTests.swift`. One vector is shared by all of them: the hello `f0003245190000400000203807f7`, and the table of
`((i * 31) ^ (i >> 8)) & 0xFF` over 8 KiB, `6204a25f925f5cc59b283583`.
