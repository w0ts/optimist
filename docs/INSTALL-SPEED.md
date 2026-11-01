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

Tests: `tests/ldr_test.c` and `fm1base/tests/ldr_test.c` built with `-DOTA_FAST_CHUNK` / `-DLDR_FAST_CHUNK` (the table,
changed sectors only, resume, damaged package, the vectors), `tests/install_replay_test.py` (fm1_install.py against the
real loader code on a pipe: old loader, old host, fast, cut off and resumed), `tests/install_test.py`, `web/test_web.mjs`,
`FlashTests.swift`. One vector is shared by all of them: the hello `f0003245190000400000203807f7`, and the table of
`((i * 31) ^ (i >> 8)) & 0xFF` over 8 KiB, `6204a25f925f5cc59b283583`.
