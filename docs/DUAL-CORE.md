# Second core (CPU1) for audio rendering: design and prototype

Status: **EXPERIMENTAL. Validated in the emulator only. Nothing here has run on an FM-1.**
Build flag `FELUCCA_DUAL` (default 0 = off, the normal single-core firmware).
Branch `feat/dual-core` (from `feat/sloop-plus`). Emulator changes: `fm1-emulator`, branch `feat/dual-core-emu`.

Tags: **[V]** verified this session (code, disassembly, emulator run). **[SDK]** read in the JieLi AC79
SDK copy (`~/GitHub/ac79-sdk`). **[I]** inference, not measured. **[HW?]** unknowable without hardware.

## 1. Audio architecture today (what runs where)

- **One compilation unit** (`felucca.c` includes everything). All code and constants run from XIP flash,
  except `.ram_text` (the flash driver, ~1 KB) [V].
- **Audio interrupt (ALNK0, priority 3)**, `audio.c fm1_alnk0_irq`: renders one I2S half buffer of
  `HALF_FRAMES` = 256 frames (5.8 ms) in 8 blocks of `CTL` = 32 samples, each by `mix_block` (`fx.c`) [V]:
  1. clear the accumulators (`send_c/d/r`, `mix_l/r`);
  2. `events_block` (`seq.c`): the transport clock, sequencer steps, arpeggiator, the USB / UART MIDI
     queues, key events from the UI, engine switches (`engine_block`), note-ons / note-offs and voice
     allocation (the shared budget of 8 voices, `voice.c`);
  3. `duck_block`;
  4. `mix_part` for each of the 3 synth parts: `track_render` (LFO, envelopes, glide, the engine's
     `block` hook and `render` per voice) -> DIST insert -> SLICER -> level / pan / mute-solo fade ->
     sends, **added** into the shared accumulators;
  5. drums (`slicer_drums` -> `drums_render`), added into the same accumulators;
  6. `fx_buses` (chorus, tempo delay, FDN reverb), DUST, PUNCH, DJ filter, master volume, limiter.
  Then the 24-bit scaling. A half that takes > 85 % of its time sheds a voice before the next half
  (`shed_voice`). `song.cpu_q8` / `felucca_dbg` hold the load (TIMER4 ticks).
- **TIMER5 interrupt** (10 kHz, outranks ALNK0, so it nests inside the render): input scan, USB poll
  (2 kHz). USB / UART MIDI only fill queues; `events_block` consumes them [V].
- **Main loop** (`main.c`): UI input and drawing, editor SysEx, OTA, autosave / section / settings
  saves. The UI changes track parameters (`trk[].p[]`) directly; engine / preset switches with IRQs off
  (`set_engine_of`), notes through queues. Because the main loop only runs when the audio interrupt
  does not, **the render always sees a whole block's parameters** (a write lands between blocks).
- **Flash writes** (`fm1_flash.h`, `felucca.c st_read / st_erase / st_prog`, `editor.c`, `ota.c`):
  all go through `irq_save()` + a `.ram_text` routine (`FL_FAR`), with IRQs off on CPU0, in the main
  loop. The SFC (XIP) is off while they run: any XIP fetch then would read garbage. An erase also
  silences the DMA buffer (`fl_erase4k_quiet`).

## 2. Evidence on CPU1

### 2.1 Stock M-VAVE firmware [V: vendor objdump of `FM-1.fwsc`, emulator run]
- Start (stock code at XIP `0x02059a2e`):
  ```
  [0x01C7FFF8] = 0x020001B8          ; CPU1 entry, in XIP
  save [0x10008]; [0x10008] |= 8     ; a clock/system register; restored after the ack (meaning unknown)
  b[0x01C22328] = 0                  ; ack byte
  [0x1EEE004] |= 8 ; &= ~2           ; C1_CON: run, out of reset
  wait b[0x01C22328] != 0; restore [0x10008]
  ```
- CPU1 entry `0x020001B8`: `usp = 0x01C182D0; sp = 0x01C192D0; reti = 0x01C02428; rti`.
- `0x01C02428` (RAM): writes the ack byte, `cli`, `r0 = cnum`, a per-core IRQ-nesting counter, two
  calls into XIP init code, `sti`, then **a mailbox loop in RAM** polling a state byte at `0x01C192E0`
  (5 -> run a function, write 6; 2 -> run another). Stock CPU1 idles in RAM.
- Stock pauses CPU1 around flash operations: Cx_CON bit 2 (pause request), wait bit 4 (stopped),
  bit 3 (resume) — **inferred by the emulator author from the stock code** (`rust-emulator/SMP.md`).

### 2.2 JieLi AC79 SDK [SDK]
- `CPU_CORE_NUM 2` (`asm/cpu.h:43`); a custom dual-core FreeRTOS port (closed `port.c`).
- Core id: `asm volatile("%0 = cnum")` (`cpu.h:79-84`); CNUM is SR6, also `q32DSP(n)->CNUM`.
- `startup.S.o` (cpu.a): `cpu1_start` = `usp = _cpu1_ustack; sp = _cpu1_sstack; reti = cpu1_main; rti` —
  the same shape as the stock entry. `cpu1_main` (`apps/common/system/init.c:99-123`): sets
  `cpu1_run_flag`, IRQs off, `interrupt_init(); debug_init(); os_start()`.
- Register block `corex2(0)` at 0x1EEE000: `C0_CON` 0x1EEE000, **`C1_CON` 0x1EEE004**, `CACHE_CON`
  0x1EEE008 (bit 14 idle). Halting the other core (`cpu/wl82/debug.c:464-471`):
  `Cx_CON &= ~BIT(3); Cx_CON |= BIT(1)` -> bit 3 = run, bit 1 = reset/halt.
- **One cache controller** for both cores (`corex2(0)` only; `dcache_way_use_select(cpu_way, prp_way)`
  splits ways between CPU and peripherals, not per core). The uncached alias exists only for SDRAM
  (0x4000000 -> 0x8000000, `cpu.h:45-53`); **internal SRAM (0x01C00000..) has no alias** and the SDK
  uses plain variables for cross-core flags (`cpu1_run_flag`, spinlocks). The SDK's cross-core
  primitives are `csync` + `testset` spinlocks (`cpu.h:189-190`) and `lockset/lockclr` (a global bus lock).
  I found no D-cache flush around cross-core data in the SDK [SDK, I: SRAM is coherent or uncached].
- Soft IRQs 120..127; core 1 takes SOFT4 (124) as its IPI; `request_irq(index, prio, handler, cpu_id)`.
  Vector table at 0x01C7FE00 (`ISR_ENTRY`); **0x01C7FFF8 = slot 126 (SOFT6)**.
- Flash: closed `vm_sfc.c.o` (`norflash_set_write_cpu_hold`, `cpu_enter_sfc_critical`,
  `another_cpu_irq`). Comment `init.c:276`: with `cpu1_run_flag = 0`, flash writes do not suspend
  CPU1, **but then everything CPU1 runs must be in internal RAM**.
- Faults: `debug.c` reports `DBG_MSG` bits per core (bit 0 WDT, 6/8 c1 MMU, 10/11 c1 PC/write limit,
  16-18 c1 bus invalid) and ends in `__cpu_reset()`. One watchdog for the chip.

### 2.3 Emulator model [V: `rust-emulator/src/cpu.rs`, `cache.rs`]
- C1_CON bit 3 set + bit 1 clear starts CPU1 at `[0x01C7FFF8]`, supervisor context, CNUM = 1.
  Bit 1 destroys it (reset). Bit 2 pause -> bit 4 stopped; bit 3 resume.
- Cores alternate per instruction on one shared bus and memory; no cache model (functional only);
  LOCKSET serializes the bus; per-core IRQ configs, shared soft-IRQ latch.
- **Fixed on `feat/dual-core-emu`:**
  1. Guest time. Before, CPU1's instructions also advanced guest time, so with CPU1 running every
     timer (and the firmware's own CPU meter) ran twice as fast per CPU0 instruction. Now a pair of
     instructions (one per core) takes one step of guest time; `cpu.core_steps[0/1]` count each core.
     Test `both_cores_share_one_step_of_guest_time`. (Stock FM-1 / Baud Girl, which start CPU1, now
     get twice the CPU0 work per unit of guest time: their 200M-step counters changed, they boot to
     their home screens; Felucca / Jangada / SLOOP 2.2 / SLOOP plus counters unchanged.)
  2. **A CPU bug that is not dual-core specific:** the 32-bit add-immediate forms (`rX = rY + imm`,
     0xe100..0xe130 / 0xe0e0) did not set the carry flag, but the compiler pairs them with `addc` for
     64-bit sums (`r12 = r10 + 32; r13 = r11 + r0 + c`, in `dx7_tables_init`). A stale carry made
     `dx7_sintab[0]` = 2^26 instead of 0 in one build and not in another (which build depended on
     register allocation). This is what first made the dual build sound different from the default
     build in the emulator. Test `add_immediate_sets_the_carry_for_a_following_addc`. Real hardware
     is assumed to set the carry here (the vendor compiler relies on it).
  3. `play_check` gained `cores` and `words:ADDR:N`; `diagnose` prints both instruction counts.

### 2.4 What is not known [HW?]
- Whether the boot ROM on CPU1 really jumps through `0x01C7FFF8` on this chip revision (stock does it,
  so very likely), and what `[0x10008]` bit 3 does.
- Whether the CPU0 guards (`fm1_guard.h`: PC limit, bus-invalid enables in `DBG_EN`, write limits)
  also apply to CPU1 or its ROM start. **A hypothesis for the earlier probe that reset the device**:
  a guard (PC limit, bus invalid) or an exception taken by CPU1 with the vectors of the probe. Not
  checked. The prototype starts CPU1 with only the stack and write guards on (they are CPU0-only).
- Memory ordering between the cores (store buffers, whether `csync` orders stores for the other core),
  cache coherence of SRAM, the cost of the shared bus when both cores run (contention), the real
  clock. The emulator cannot show any of these.

## 3. Design

### 3.1 Work split: fork / join per block, bit-identical
CPU1 renders **whole synth parts** (`mix_part`: engine, DIST, SLICER, level, pan, sends) for parts in
`DUAL_PARTS` (default parts 2 and 3) into **its own accumulators**; CPU0 renders the other part and the
drums, then waits for CPU1, adds its accumulators and runs the buses and the master (`mix_block_dual`,
`src/system/dual.c`). All the sums are 32-bit integer additions, so the order does not matter: **the output is
bit-identical to one core rendering everything**, as long as no part's render touches state of another
part. The alternative (CPU1 runs the FX buses one block behind) would change the sound (latency) and
was not taken.

State audited for cross-part sharing in the render path (`static` globals of the engines, voice.c,
slicer.c, fx.c), and fixed under `FELUCCA_DUAL >= 2`:
| Shared state | Problem | Fix |
|---|---|---|
| `part_buf`, `send_*`, `mix_*` (fx.c) | one scratch / accumulator set | `mixacc_t` per core; `mix_part(t, n, A)` (macro `MX`, the default build is unchanged in source) |
| `dx7_core.c` `bus/ext/tmp`, `eng_dx7.c` `buf` | function-static scratch blocks | one set per core, indexed by `fm1_cnum()` |
| `eng_formant.c formant_nz` | one noise generator for all parts | one per part (so the dual build's FORMANT breath noise / random vowels are a different, equally random sequence than the default build's; the same with and without CPU1) |
| `eng_super.c super_nv = voices_busy()` | reads every part's voices, which the other core is ending | a snapshot (`dual_vbusy`) at the block start, after the events (differs from the default build only when a voice ends inside the block on an earlier part *and* SUPER's copy cap changes at that moment) |
| `dx7_cart_check` (in DX7's `block` hook) | a cartridge lookup writes globals | done by CPU0 before the fork |
| `dx7_tables_init` | first-use init from either core | at boot |
Read-only during the render: `song`, `duck`, the tables, `clk_*`. Per part already: `dx7_part`,
`drw_t/v`, `gr_p`, `sl[]`, DX7 slots (each touched by its own part only in the render).

### 3.2 Handoff (mailbox `fm1_dual_mb`, internal SRAM, `hal/fm1_dual.h`)
- Every field has one writer. CPU0: `arg` (part mask), then `csync`, then `req = req + 1`. CPU1 (RAM
  loop) sees `req != done`, `csync`, runs the job from XIP, returns to the RAM loop, `csync`,
  `done = req`. CPU0 waits for `done == req`, `csync`, adds CPU1's accumulators.
- No cache maintenance: internal SRAM, as the SDK's own cross-core flags [I, HW?]. If hardware shows
  stale data, the fallback is the SDK's `flush_dcache`/`flushinv_dcache` on `dual_acc[1]` and the
  track state (its cost would eat part of the gain).
- CPU1 has IRQs off for its whole life and polls (no IPI). Polling costs bus bandwidth [HW?]; SOFT4
  (as the SDK) is the alternative if contention shows.

### 3.3 Flash writes
The SDK rule (`init.c:276`) is kept by construction: **CPU1 executes XIP code only between `req` and
`done`, and both happen inside CPU0's audio interrupt**; outside, it spins in `fm1_dual_idle_ram`
(`.ram_text`, no calls, no constants; the build's "no calls in .ram_text" check holds). Flash
operations run in the main loop with IRQs off, so they can never overlap a job. `irq_save()` (every
flash operation) calls `dual_flash_enter()`, which checks that CPU1 is idle and halts it if not (it
cannot happen unless the protocol is broken). The stock hardware pause (bit 2 / bit 4) is not used: its
semantics are inferred; it stays a fallback option.

### 3.4 Failure behaviour (never brick, never hang)
- Boot: no hello within 50 ms -> CPU1 back into reset (C1_CON bit 1), single core for this boot.
- A job not done within one half buffer (5.8 ms), or a CPU1 fault reported (`fm1_fault_c` on CNUM 1
  parks CPU1 in RAM and sets `fault`) -> CPU1 into reset for good, CPU0 renders those parts itself
  from then on (a click once: a part may be half-advanced). The UI says "CPU1 OFF <why>" once; the
  console shows `cpu1_up/why/jobs/wait_max_us`.
- Watchdog: one chip watchdog fed by CPU0's main loop (unchanged); CPU1 never feeds it. A CPU0 hang
  still resets the chip; a CPU1 hang cannot hang CPU0 (timeouts everywhere CPU0 waits).
- Resets (UBOOT entry, OTA, reboot) are chip resets (P33): CPU1 resets with them.
- The bus and PC guards are enabled after CPU1's start (unknown whether they cover CPU1's ROM path).

Also fixed on the way (default build too): DX7 phase sums (`phase += freq`, `phase + in[i]`) relied on
signed overflow, undefined in C (UBSan on the host regress shows it); they now wrap in `uint32_t`
(`dx7_wrap`). Same samples where the compiler already wrapped.

## 4. Prototype

| File | What |
|---|---|
| `firmware/hal/fm1_dual.h` | C1_CON / SPL vector / [0x10008] registers, `fm1_cnum()`, the mailbox, the RAM idle loop, the CPU1 entry (asm), start with timeout, halt |
| `firmware/src/system/dual.c` | CPU1's main, boot, `mix_block_dual` (fork / join), timeouts, the flash check, the stage-1 counter, the UI notice |
| `firmware/src/fx/fx.c` | `mix_part(t, n, A)` per-core accumulators (`MX`), `mix_finish` shared by both mixers |
| `firmware/src/system/bench.c` | emulator scenarios `FELUCCA_BENCH` 1..3 (+ `FELUCCA_BENCH_SAVE`), per-block signatures |
| `firmware/hal/fm1_irq.h`, `fm1_flash.h` | CPU1 fault hook; `FM1_FLASH_ENTER` in `irq_save` |
| `tools/build.py` | `FELUCCA_DUAL` 0/1/2, `FELUCCA_BENCH` 0..3, `FELUCCA_BENCH_SAVE`, `DUAL_PARTS` (part mask), `DUAL_FAILTEST` 0..3 |

Build: `FELUCCA_DUAL=2 JIELI_TOOLCHAIN=~/.jieli/toolchain-docker sh build.sh`. Cost: +1.5 KB flash
(576 KB of the 581.5 KB slot), +2 KB RAM (CPU1's accumulators), +6 KB pool (CPU1's stacks), +23 insns
of `.ram_text`. The default build (`FELUCCA_DUAL=0`) keeps its source paths; host tests unchanged
(131 golden renders, 0 changed; the 3 DX7 health failures and 3 CPU-over-budget entries were there before).

## 5. Emulator results [V]

Emulator `feat/dual-core-emu`, `play_check`, 1 s boot then 3 s recorded. Scenarios (`bench.c`): 8 voices
in 3 synth parts (3 + 3 + 2 notes) with a drum groove and the presets' FX sends:
B1 DX STRINGS / DX 8OP KEY / DX PAD (all DX7), B2 SUPER PAD x3, B3 DX 8OP KEY / SUPER PAD / DX STRINGS.
`single` = default build, `dual0` = dual build with `DUAL_PARTS=0` (CPU1 running, no work),
`dual` = parts 2 and 3 on CPU1. Load = the firmware's own meter (`song.cpu_q8`, TIMER4 time inside the
audio interrupt per half, averaged), `max_us` = the longest half (budget 5805 us).

**312 MHz (no voice shedding anywhere):**

| Scenario | single max_us / load | dual max_us / load | CPU0 reduction (load) | Audio single vs dual |
|---|---|---|---|---|
| B1 all DX7 | 2278 / 31.6 % | 1443 / 17.6 % | -44 % | bit-identical (132 298 frames) |
| B2 SUPER x3 | 1987 / 28.1 % | 1291 / 16.0 % | -43 % | bit-identical |
| B3 DX7 + SUPER + DX7 | 2206 / 31.6 % | 1476 / 19.1 % | -40 % | bit-identical |

`dual0` measured the same as `single` (within 0.5 %): the mailbox and CPU1 spinning cost CPU0 nothing
measurable in the emulator. (WAVs align at a 2-frame offset: CPU1's start shifts the audio start.)
CPU0 still does the events, part 1, the drums, the three FX buses and the master; the ideal split is
not 50 %. Moving the FX buses or a third part would need another design (latency) or a better balance
(`DUAL_PARTS` picks the parts; a per-block balance by voices is possible, not done).

**96 MHz (overload):** single-core B1 runs at 104 % (312 late halves of 517, voices shed), dual at 70 %
(1 late half, no shedding needed); B3 106 % vs 69 %; B2 66 % vs 61 % (the single build shed voices).
Outputs then differ, as the single build drops voices.

**Fallbacks (`DUAL_FAILTEST`, B3, 4 s, each verified):** (1) CPU1 never says hello -> 50 ms timeout, CPU1
held in reset, single core, audio and UI normal; (2) a job that never ends -> one late half (max 7042 us),
CPU1 into reset, CPU0 renders all parts from then on; (3) CPU1's fault handler -> CPU0 sees `fault`,
halts it, no late half. The exception itself cannot be raised on CPU1 in the emulator (its vector
routing on hardware is unknown).

**Flash save while playing (`FELUCCA_BENCH_SAVE=1`, B3, dual):** a project save (sector erase +
program, IRQs off, in the main loop) 1.5 s in: "SAVED" on screen, CPU1 stayed up (`up=1`, jobs went
on to 4864 / 4865 in flight), ping / pong in step, no fault, no late half.

**Plain builds:** stage 2 (`FELUCCA_DUAL=2`, no bench) at 48 MHz: boots, 3 held keys play, 5312 jobs,
load 44 %. Stage 1 (`FELUCCA_DUAL=1`): "CPU1 <count> OK" at the bottom of the screen, counting.
Regression set on the fixed emulator: Felucca LCD 4235962 / audio 318151, Jangada 4211962 / 318136,
SLOOP 2.2 284160 / 315581, SLOOP plus 284160 / 315570 (all unchanged); emulator unit tests and the
ignored firmware tests (Felucca package, USB-MIDI host, web bridge) pass.

What the emulator cannot tell: cycles (it counts instructions), cache misses and XIP wait states (two
cores fetching from one flash cache will slow each other), SRAM bus contention, memory ordering.
**The real gain on hardware is unknown**; it will be lower than the 40-44 % above.

## 6. Hardware test plan (nothing has run on a device)

Packages (only emulator-tested, **EXPERIMENTAL**), in `~/GitHub/fm1-firmware/`:
- `sloop-dual-EXPERIMENTAL-stage1-2026-10-05.fwsc` — `FELUCCA_DUAL=1`
- `sloop-dual-EXPERIMENTAL-stage2-2026-10-05.fwsc` — `FELUCCA_DUAL=2`

Before anything: check the way back works on this device, with a known-good package (SLOOP plus,
or the stock `FM-1.fwsc`): install it with M-UPGRADE / the web installer and see it boot. Keep the
stock package and M-UPGRADE at hand. Have a USB cable to the computer, the CDC console open if possible.

Recovery ladder (from mildest):
1. A CPU1 problem the firmware notices: it says "CPU1 OFF <why>" and runs single-core. Nothing to do.
2. The device resets during boot (as the earlier probe did): the boot guard counts warm resets; after
   two failed boots in a row the firmware starts its **USB recovery mode** (no CPU1 start there), where
   a package can be installed again. A third reset there falls back to the mask-ROM UBOOT.
3. Hold OCT- at power-on: recovery mode by hand. OCT- + OCT+ for 5 s (stopped): UBOOT.
4. The mask-ROM USB updater (UBOOT) takes M-UPGRADE / FM-1-transporter whatever the app does;
   the earlier probe that reset the device was recovered this way.

Stage 1 (CPU1 starts, counts, nothing else):
- Install stage 1. Watch the boot: the SLOOP logo, then the normal screen with "CPU1 <n> OK" at the
  bottom, the number growing twice a second.
- Outcomes: counting + OK = CPU1 runs our code from the SPL vector and SRAM writes are seen across
  cores. "CPU1 NO START" = no hello in 50 ms (CPU1 held in reset; SLOOP works single-core): the start
  sequence or the vector does not do on this chip what the emulator assumes. "LATE" = CPU1 started
  but stopped answering pings. Reset loop -> recovery mode (ladder 2): note it; the start crashes the
  chip (guards? exception routing? the [0x10008] bit?).
- Leave it running 10 minutes, play, save a project (flash with CPU1 alive), power-cycle.
- USB CDC console, command `status`: `cpu1_up`, `cpu1_why`, `cpu1_jobs`, `cpu1_wait_max_us`.

Stage 2 (parts 2 and 3 on CPU1), only after stage 1 passed:
- Load a heavy project (3 DX7 / SUPER parts, 8 voices, drums, sends). Compare by ear with SLOOP plus.
- Console: `cpu_pct`, `audio_max_us`, `voices_shed`, `cpu1_jobs`, `cpu1_wait_max_us`, `late` (felucca_dbg).
  Record them for the same project on SLOOP plus: that is the real gain.
- Listen for clicks / garbage on parts 2 and 3 only (stale data between cores = a coherence or ordering
  problem: then add D-cache flushes or the SDK's barriers, see 3.2).
- Save while playing; edit parameters while playing; switch engines on parts 2 / 3; USB MIDI in.
- If "CPU1 OFF TIMEOUT / FAULT" appears: note when; it fell back, the device should keep working.

What to watch overall: resets (and when), the screen message, audio glitches on parts 2-3 only, the
console counters, battery drain (CPU1 sleeps between jobs with `FELUCCA_DUAL_IDLE`, the default:
compare with `FELUCCA_DUAL_IDLE=0`, where it spins in RAM), heat. If CPU1 never wakes on the device
(soft interrupt 124 not reaching it), every block times out once and CPU1 goes down at the first job:
"CPU1 OFF TIMEOUT"; then test `FELUCCA_DUAL_IDLE=0`.

## 7. Risks and open points

- **Start on hardware [HW?]:** the vector, the [0x10008] bit and C1_CON order follow the stock app, but
  the one previous probe that started CPU1 reset the device; why is unknown. Our differences from stock:
  CPU1 runs our C code with IRQs off and never calls the SDK's `interrupt_init / debug_init`; CPU0's
  guards are on (stack, write limits) during the start; vectors are all fatal stubs (an exception on
  CPU1 lands in `fm1_fault_c`, which parks it if CNUM = 1, if the vector is used by CPU1 at all).
- **Coherence / ordering [HW?]:** assumed coherent internal SRAM (as the SDK's flags); `csync` around
  the flags. If wrong: stale part samples (audible on parts 2-3).
- **Flash:** CPU1 off XIP during flash work by construction, checked at every `irq_save`. The stock
  firmware pauses CPU1 by hardware instead; if hardware shows XIP trouble anyway (prefetch?), add that.
- **Bus / cache contention:** both cores fetch code through one cache and the XIP flash: the gain may
  shrink a lot. Moving the audio hot path to RAM (`.ram_hot`, SPEED-IDEAS 2.1) helps both.
- **Power:** with `FELUCCA_DUAL_IDLE` (default with `FELUCCA_DUAL=2`) CPU1 sleeps in `idle` between
  jobs: CPU0 posts a job (`req`), then sets soft interrupt 124 (SOFT4, the SDK's CPU1 IPI) in the
  shared latch; CPU1 has only that source enabled, in its own configuration bank (+0x200, as the
  emulator models the stock firmware), and its handler (`isr_c1_wake` -> `fm1_cpu1_wake`, RAM) clears
  the latch first, then runs every posted job and publishes `done`, so a job posted after its last look
  wakes it again (no lost wake). Emulator (integration, B1 at 312 MHz): CPU1 halted in ~84 % of its
  slots, audio bit-identical with the single-core build, fallbacks (`DUAL_FAILTEST` 2, 3) as before.
  Unverified on a device: the per-core bank address, the shared latch, `idle` waking on CPU1.
  `FELUCCA_DUAL_IDLE=0` keeps the RAM spin (`fm1_dual_idle_ram`).
- **Interrupt nesting:** TIMER5 nests in the audio interrupt (as before); it only fills queues, so CPU1
  never sees a half-changed track.
- **Sound vs the default build:** bit-identical in the emulator for the three scenarios; FORMANT's
  random sequence differs (per-part generator) and SUPER's copy cap can differ in a rare block.
- **Maintenance:** any new cross-part state in a render path (a shared static scratch, a global RNG)
  breaks the split silently: keep render state per part or per core (`DX7_CORE()` pattern), and re-run
  the single / dual0 / dual WAV comparison (`bench.c` signatures locate the first differing part/block).
