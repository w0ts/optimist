# The second core (CPU1)

Status 2026-10-19: **on by default.** The builder's `DUAL` item defaults to 2 (`tools/builder/registry.py`); a profile
may set `DUAL=0`. A raw compile without the builder still defaults `FELUCCA_DUAL` to 0 (`firmware/src/felucca.c`).
Verified on an FM-1: a soak of about 75 minutes with BLE on and USB MIDI, zero drops and zero underruns. Some units
misread RAM on CPU1; those fall back to one core by themselves (section 5).

Tags: **[HW?]** cannot be known without hardware; **[I]** inferred.

## 1. What it does

CPU0 runs the audio interrupt (ALNK0, priority 3): per 256-frame half buffer, in 8 blocks of 32 samples, `mix_block`
(`fx.c`) renders events, the 3 synth parts, the drums, the FX buses and the master. With CPU1 up, **jobs are forked
per block and joined**: CPU1 renders whole parts into its **own accumulators**, CPU0 renders the rest, waits, adds
CPU1's sums, then runs the buses and master (`mix_block_dual`, `firmware/src/system/dual.c`).

- All sums are 32-bit integer additions, so the order does not matter: **the output is bit-identical to one core
  rendering everything**, as long as no job touches another job's state.
- `dual_split` assigns jobs (the three parts, the drum track, the reverb's kept block) each block by their measured
  render time, longest first onto the core with less load. A build with `DUAL_PARTS` set keeps a fixed split (the
  emulator's references).
- **The reverb runs one block (0.73 ms) late** while CPU1 is up (`REV_LATE`, `fx.c`; `DUAL_REV=0` builds without it).
  `fx_buses` keeps the block's send and decisions; `rev_job` runs the tank during the next block. With CPU1 down the
  reverb runs inline; if CPU1 dies, the tail jumps one block once.
- Measured (emulator, instructions not cycles): CPU0 load down 40-44 % at 312 MHz with the same sound. Bench at 480 MHz:
  CPU0 20.7 %, CPU1 16.9 % with the late reverb. The real gain on hardware is lower and unmeasured (cache misses, XIP
  waits and bus contention are not modelled).
- Cost (`tools/builder/costs.json`, `DUAL`): about 3.4 KB of flash, 3.7 KB of RAM, 6.1 KB of pool, 660 B of RAM code.
  (The builder's own item text still says 1.8 KB / 1.9 KB.)

State shared by parts is made per core or per part: scratch and accumulators (`mixacc_t`, `mix_part(t, n, A)`), the DX7
scratch (`fm1_cnum()`), FORMANT's noise (one generator per part, so a different but equally random sequence), SUPER's
voice count (a snapshot at block start), the DX7 cartridge check and table init (CPU0 before the fork), the LFO S&H and PHYS
reset draws (CPU0 before the fork, in part order). A new shared static in a render path breaks the split silently:
`tests/emu_dual_check.py` compares single, `DUAL_PARTS=0` and dual takes sample for sample.

## 2. The mailbox and the protocol (`firmware/hal/fm1_dual.h`, `fm1_dual_guard.h`)

- Internal SRAM, every field has one writer, each core's fields on their own 64-byte lines. No cache maintenance
  (internal SRAM, as the SDK's own cross-core flags [I, HW?]).
- CPU0: write `arg` (job mask), `check = FM1_DUAL_CHECK(req, arg)`, `csync`, `req = req + 1`. CPU1 (a polling loop in
  RAM) runs a job only when `req == done + 1` (its own `done`), `arg` is allowed and `check` matches; it reads `arg`
  once, runs the job from XIP, `csync`, `done = req`. CPU0 waits for `done == req`.
- CPU1 polls once per pass with 32 nops (no bus traffic), beat and ping every 256 passes. The sleep variant
  (`FELUCCA_DUAL_IDLE=1`, wake by soft IRQ 124) is off by default: the wake never reached CPU1 on the FM-1, and it did
  not start under the stock order (TIMEOUT).
- A join is bounded to two blocks (`DUAL_JOIN_US`, 1.45 ms).

## 3. Start and stop on the FM-1

Start sequence, as the stock M-VAVE app does it (disassembled): `[0x01C7FFF8]` = CPU1 entry; `[0x10008] |= 8` until CPU1
answers; `C1_CON` (`0x1EEE004`) `|= 8`, `&= ~2`; wait for the ack. Stop: `C1_CON &= ~8, |= 2`. The entry sets
`cli`, `icfg = 0`, the stacks, and `rti`s into C. CPU1 has its own EMU bank (stack limit on, div0 and float traps off).

**Stock V15's start-up order is the normal order of a DUAL build** (reverse-engineered from V15, measured on the FM-1:
before it, every BLE + dual boot crashed within 2-60 s; after it, 3 x 180 s runs clean):

1. **The rails first** (`system/supply.c`, `hal/fm1_supply.h`, top of `fm1_main`, IRQs off): the builder item
   `STOCK_SUPPLY` (default on with `DUAL`) sets the rails as the stock app does (the 1.4 V rail onto the DC-DC, VDC14
   1.60 V, SYSVDD 1.38 V, never lowered, the PLL untouched, the 2.6 V detector after). The boot loader leaves 1.26 V /
   1.40 V on the LDO and CPU1 merely spinning crashed CPU0 there. A wrong read-back keeps CPU1 off (`2ND CORE OFF
   SUPPLY`). **Risk, at your own risk:** above the AC79 datasheet's table, absolute limits not public, unit variation,
   heat and battery not measured.
2. CPU1 starts at `dual_boot` and spins in its RAM loop (no job yet), as stock does.
3. The BLE radio starts before `audio_init`, IRQs off (`main.c`).
4. CPU1's jobs begin 400 ms after BLE is running (`dual_stock_after_ble`).
5. **Every flash write or erase parks CPU1** (`fm1_dual_mb.park`): CPU1 waits for the cache idle bit and spins in RAM;
   CPU0 waits up to 200 us, else pauses it in hardware (`C1_CON` bit 2, stopped bit 4), writes, resumes (bit 3). The
   SFC (XIP) is off during flash work, so CPU1 may never fetch from XIP then. By construction CPU1 runs XIP code only
   between `req` and `done`, both inside CPU0's audio interrupt; `irq_save()` also checks it.
6. The TIMER5 and ALNK0 entries run from RAM (`isr_timer5_ram`, `isr_alnk0_ram`).
7. BLUETOOTH ON at runtime (a radio started while audio runs, which stock never does) holds CPU1 for the radio's
   program and starts it again after (`dual_radio_hold` / `_release`; emulator-tested only).

**CPU1's stack** is `fm1_c1_stack`, 6 KiB in the pool, set by the entry: the spinning build runs its jobs on `usp`
(5 KiB), the sleeping one in the wake interrupt on `ssp`. `tools/build.py cpu1_stack` checks the static worst path
(ACID about 2.5 KB, PHYS 1.6 KB) against 3,840 B and fails over it.

**Boot-order check:** CPU1 may start only after the UBOOT hold and the boot guard, never on the recovery path.
`tools/build.py boot_order()` reads the call graph of `boot_hold`, `bootguard_begin`, `fm1_main`, `recovery_main`,
`dual_boot`, `fm1_dual_start` (kept out of line, +192 B).

## 4. Fallbacks (never brick, never hang)

| Message (SYSTEM > INFO "2ND CORE OFF ...") | Cause |
|---|---|
| NO START | no hello within 50 ms: CPU1 back into reset |
| TIMEOUT / FAULT | a job not done within the join bound, or a fault on CPU1 (`fm1_fault_c` parks it): CPU1 into reset for good, CPU0 renders its parts from then on (one click) |
| MISREAD / CANARY | the request check refused a request, or the boot canary (32 words read 64 times) read wrong: CPU1 held |
| SUPPLY | the rails' read-back was wrong (`STOCK_SUPPLY`) |
| AFTER CRASH | the boot before crashed within its first 30 s: this boot stays on one core |
| OVERLOAD | the main loop had no pass for 1.5 s (`DUAL_STARVE_MS`): CPU1 taken down |
| SETTING | the 2ND CORE setting is OFF (`dual_mode.h`, settings word bits 27-28: 0 AUTO, 1 OFF) |

With `DUAL_MODE_MENU 1` AUTO turns itself OFF after a misread, canary, timeout or fault and saves it; without that menu
(the Optimist UI today) the self-disable lasts the session. A single watchdog serves the chip and only CPU0 feeds it;
a CPU1 hang cannot hang CPU0. Resets are chip resets and reset CPU1. Recovery ladder if a boot loops: the boot guard
starts USB recovery mode after two failed boots; hold OCT- at power-on for recovery by hand; OCT- + OCT+ for 5 s
enters the mask-ROM UBOOT.

Misread RAM (some units, at the boot loader's supply, seen on X0X, Melodee and Jangada units): guard ported from X0X
(GPL-3.0). The load of each core shows in SYSTEM's CPU row (`dual_load.h`: CLOCK, CPU0 including the wait in `dual_join`,
CPU1).

## 5. Testing

- Console: `cpu1_mode`, `cpu1_bad`, `cpu1_canary`, `cpu1_up/why/jobs/wait_max_us`, `cpu1_supply_was`, `supply_*`.
- Emulator: fault injection `DUAL_FAILTEST` (1 no hello, 2 a job that never ends, 3 CPU1 fault, 4 a wrong check word,
  5 a wrong canary, 6 a lost wake, 7 a wake that never arrives). The emulator's timing model does not represent the
  chip's dual-core timing (a late half showed at 96 MHz in a burst of flash writes, none at 192 MHz or on the FM-1),
  so `tests/emu/run.py` builds a DUAL profile single-core (`OPTIMIST_DUAL_PARK=1`) unless `EMU_DUAL=1`.
  The second core is verified by the host tests, `dual_stockorder_emu_test`, `dual_ble_rule_emu_test`,
  `emu_boot_check` (48 / 96 / 192 MHz: no late half, CPU1 taking jobs), `emu_dual_check` and the hardware soak.
- Bench scenarios: `FELUCCA_BENCH` 1-3 (+ more), `bench.c`; the signature locates the first differing part and block.
- Test image: `OPTIMIST_DUALCFG=1` (console `dualcfg`, `reboot`) switches orders and probes; only in console test builds.

## 6. Open limits

- **Hardware unknowns [HW?]:** memory ordering and coherence of SRAM between cores (stale part samples on parts 2-3
  would be the symptom; fallback: SDK D-cache flushes), bus and cache contention (both cores fetch through one cache and
  one flash), whether CPU0's PC and bus guards cover CPU1.
- **Why some units misread RAM on CPU1** is not understood; the supply raise and the guard are mitigations.
- **The sleep variant** never ran on the FM-1; only the spin runs there.
- **Sound vs single core:** bit-identical in the emulator except FORMANT's noise sequence, SUPER's copy cap in a rare
  block, and PHYS's reset seed.
- **Maintenance:** keep render state per part or per core; re-run the single / dual0 / dual comparison.

Code map: `firmware/hal/fm1_dual.h` (registers, mailbox, RAM loop, entry), `fm1_dual_guard.h` (check, canary),
`firmware/src/system/dual.c` (CPU1 main, boot, fork/join, split, timeouts), `dual_mode.h` (setting, reasons),
`dual_load.h` (load), `supply.c`, `fx/fx.c` (`mix_part`, `rev_job`), `system/bench.c`, `tools/build.py` (flags,
`cpu1_stack`, `boot_order`).
