# SLOOP plus: speed ideas (CPU time on the real FM-1)

Date: 2026-10-05. Scope: `~/GitHub/sloop-merged` (branch `feat/sloop-plus`), read-only. The emulator is a bonus topic.
Another agent is hand-writing inline ASM for the 2–3 hottest loops (hardware `rep` loops, 64-bit MAC) in
`~/GitHub/sloop-perf`. That work is left out here, apart from where an idea below touches the same code.

Tags: **[V]** checked in code, a build artefact or a measurement this session. **[D]** from the AC7911B8
datasheet (V1.1, 2022). It is **not verified** that the FM-1 uses exactly this part; the 8 Mbit flash matches
the firmware's flash map. **[I]** inference or estimate, unmeasured.

## 0. What the audio path costs today

- **Audio scheduling [V].**
  - The I2S half-buffer is 256 frames, 5.8 ms (`core.h:16`). The ALNK0 ISR renders it in 8 blocks of `CTL` = 32 (`audio.c`, `dsp.c:110`).
  - If a half takes more than 85 % of its time, one voice is shed (`audio.c` `shed_voice`).
  - The USB CDC console reports `cpu_pct`, `audio_max_us` and `voices_shed` (`console.c:177-179`). This is the meter on hardware.
- **Host cost per sample [V].** These are instructions per sample on the Mac (x86-64 `cc -O2`), from `tests/regress.c` built in a scratch copy.
  - They are a *proxy* for relative cost only. They are not pi32v2 cycles, and they include neither cache misses nor flash wait states.

  | Workload (heaviest preset, 8 notes, including the idle mix) | instructions / sample |
  |---|---|
  | idle mix: FX buses, master and events, nothing playing | **358** |
  | idle + drum pattern | 678 |
  | 3 synth parts + drums | 1984 |
  | SUPER HOOVER SAW | 1880 |
  | DX7 DX 8OP BAS | 2140 (130 ns/sample on the host, the slowest engine by far) |
  | ANALOG FUNK BASS / TRIO HOOVER / PHASE CZ STRING | 1574 / 1410 / 1428 |

- **Experiment [V].** In the scratch copy, `fx_buses` was stubbed to return silence.
  - idle fell from 369 to **112** instructions per sample.
  - 3 parts + drums fell from 1995 to 1740.
  - So **the three FX buses cost about 255 instructions per sample all the time**: about 70 % of the idle floor and about 13 % of a busy mix.
  - Stubbing `master_out` saved only about 35–55.
- **Stale CPU checks [V].**
  - `tests/cpu_baseline.txt` still has the Felucca preset names, so almost every SLOOP preset prints "no baseline" and is not checked.
  - DX7 has no entries at all.
  - The three mix entries are over budget: idle 358 vs 286, idle+drums 678 vs 495, 3 parts 1984 vs 1566.
  - `tests/target_budget.py` does not list `dx7_render`, `dx7_block` or `dx7_op*`.
- **Memory map [V].** Source: `firmware/app.ld` and the build at 14:05.
  - All code and constants execute in place from XIP flash.
  - `.ram_text` uses **2,888 B of the 24 KB RAMTEXT** region at 0x01C00000.
  - 0x01C06000–0x01C08000 (8 KB) is unused ("RAM-run debug").
  - RAM 96 KB: `.data` + `.bss` = 83,224 B, so about 15 KB is free.
  - POOL: 322,288 of 344,064 B used, about 21 KB free; the build requires 8 KB to stay spare.
  - Flash: the image is 574,792 B against a 581,564 B slot, so **about 6.7 KB is free**.
- **Caches [D].**
  - 32 KB I-cache, 8-way, and 24 KB D-cache, 6-way.
  - The SDK linker script talks about up to 7 free D-cache ways of 4 KB (`sdk_ld_sfc.c:25-32`). That does not match the datasheet's 6-way, so treat way counts as **unverified**.
  - The emulator does not model cache timing or flash wait states. **Cache and placement ideas can only be measured on hardware.**
- **Size of the audio code [V]** (from `build/felucca.dis`).
  - `mix_block` is 13.7 KB, with events, sequencer and FX inlined into it.
  - Engine renders: analog 1.8, digital 1.4, formant 1.9, trio 2.6, grain 3.5, super 1.4 and dx7 (render + block + kernels) 3.9 KB.
  - `drums_mix` 2.8 KB, `slicer_track` 0.9 KB, ISR 0.5 KB.
  - **A half with 3 engines + drums runs about 22–26 KB of code.** That fits the 32 KB I-cache by itself.
  - But the UI runs between halves, so its code and data are cached in between: `ui_draw` alone is 26 KB, plus 46 KB of font tables and the UI code.

---

## 1. CPU clock

### 1.1 Measure the real clock first (S)
- **What.**
  - Clock setup:
    - The firmware never programs the PLL; it runs at whatever the SPL hands over [V: no PLL code in `firmware/hal` or `crt0.S`].
    - The update loader never rewrites the flash head, so the FM-1 keeps M-VAVE's SPL and its `isd_config` [V: `tools/fm1pkg_make.py` docstring].
    - The SDK default is `SYS_CLK=320MHz`, `HSB_DIV=1` (`ac79-sdk/cpu/wl82/tools/isd_config_rule.c:136-150`) [V]. M-VAVE's setting is **unknown**.
    - The clock registers were captured on hardware once but never decoded (`fm1-emulator-boot/rust-emulator/ATOMIC.md`, "SPL clock handoff": PLL 0x119a0.. = 45400203 3f503026 0940022b 0750310c, 0x10008 = 00010200) [V].
  - Proposed measurement:
    - Add a console command that times a fixed loop of known length against TIMER4 (24 MHz crystal, independent of the PLL; `hal/fm1_time.h`). Run the loop from RAM to remove flash effects.
    - Loop cycles / elapsed time = CPU MHz.
- **Gain.** None by itself. It tells you whether §1.2 is worth anything.
- **Risk.** None.
- **Where.** `console.c` (new key), plus a RAM-placed loop.

### 1.2 Raise the clock to 320 MHz if it is lower (M)
- **What.** If §1.1 shows less than 320 MHz (the datasheet maximum [D]), program the PLL and bus dividers at boot.
- **Gain.** Proportional to the ratio. At 240 → 320 MHz it is about +33 % headroom [I]. Flash-miss stalls do not scale, because the SFC clock is separate.
- **Risk: high.**
  - The SDK clock driver (`clk_set`) is in `cpu.a` (closed), so the register sequence must be worked out from the vendor disassembly.
  - LSB-derived clocks would move with it: LCD SPI1 (`lcd.c` `LCD_BAUD`), UART baud, the USB PLL48M path.
  - Power use and heat go up.
  - Do **not** go past 320 MHz. `CONFIG_OVERCLOCKING_ENABLE` in the SDK offers 396 MHz, but that is above the datasheet rating.
- **Where.** `crt0.S` or `fm1_sys.h`, before `audio_init`.
- **Measure.** §1.1, then `cpu_pct` / `audio_max_us` on a fixed heavy pattern.

---

## 2. Memory placement (XIP flash vs RAM)

### 2.1 Put the audio hot path in RAM: a new `.ram_hot` section (M) — likely the biggest single win
- **What.**
  - Move these into RAMTEXT:
    - `fm1_alnk0_irq`, `mix_block` (with `fx_buses`, `mix_part`, `master_out`, `dust`/`djf`/`punch`), `track_render`, `drums_mix` + `ds_render`, `slicer_track`;
    - the render functions of the heaviest engines: DX7 kernels (`dx7_op*`, `dx7_note_compute`, `dx7_block`, `dx7_render`), `super_render`, `analog_render`, `trio_*`.
  - Space: about 21.7 KB is free in RAMTEXT, plus the unused 8 KB at 0x01C06000. About **29 KB** in total [V], which is more than the 22–26 KB hot set above.
  - Build changes:
    - Use a **separate** section. The existing `.ram_text` is checked to contain no calls, because it runs with XIP off.
    - Hot code calls XIP code, so calls in both directions need the far-call form (see `FL_FAR` in `hal/fm1_flash.h:21`).
    - Alternatively, keep every callee inlined. Most DSP helpers already are.
    - `build.py check()` needs a rule for the new section.
- **Why it should help [I].**
  - Each half, the audio code is evicted by `ui_draw` (26 KB), the LCD/gfx code and font reads.
  - It then refills from quad-SPI flash. A 32-byte line is about 80+ SPI clocks, roughly 1 µs or a few hundred CPU cycles at 160–320 MHz.
  - Refilling about 20 KB is about 640 lines, roughly 0.3–0.7 ms of a 5.8 ms half: **5–12 % of the audio budget**, every half. The worst case is during busy UI redraws.
  - RAM code also stops the audio from slowing down while the UI is busy.
- **Effort.** M. Section attributes on the functions, the linker script, the `build.py` check, and far calls.
- **Risk.**
  - Bit-exact: no change to the output.
  - Flash writes: they already run with IRQs off, so RAM code adds nothing there.
  - Fit: RAMTEXT space is shared with the flash driver (2.9 KB), so leave it room.
  - The emulator copies `.ram_text` from the image; check that the new section is copied as well.
- **Measure.**
  - On hardware, `audio_max_us` and `cpu_pct` on the same pattern with the UI idle vs scrolling a page, before and after.
  - The emulator cannot show this.

### 2.2 Hot rodata tables into RAM (S)
- **What.** Copy the tables the audio path reads every sample into `.data` or a RAMTEXT data area at boot:
  - `SINE` 2 KB (`osc_sine`), `TANH_Q15` 0.5 KB (`softclip`, used in the knee, DIST and filter knee);
  - `DX7_EXP2` 4 KB, `SVF_G`, `ENV_EXP`/`ENV_LIN`, `LFO_INC`, `LEVEL_Q12`, `GR_HANN`.
  - That is about 9 KB [V sizes from `felucca.dis`].
  - `dx7_sintab` is already in RAM (`dx7_core.c:54`). `PITCH_INC` (8 KB) is read once per voice per block, so it is not worth moving.
- **Why [I].**
  - Every UI frame streams 46 KB of fonts (`FONT_S_DATA` 21.5 KB, `FONT_L_DATA` 24.6 KB) through the 24 KB D-cache. That evicts the tables.
  - This assumes XIP data reads go through the D-cache. The SDK keeps 1 D-cache way for flash data in its SFC-only build, which suggests so. **Unverified.**
- **Gain.** A few % of a busy half [I].
- **Effort.** S: `static const` → `static` plus an init copy, or a `.data` placement.
- **Risk.** It needs about 9 KB of the ~15 KB of free `.bss`. Another option is the POOL headroom, as long as 8 KB stays spare.

### 2.3 Use spare D-cache ways as RAM (L, speculative)
- **What.**
  - The SDK gives up to 7 D-cache ways (4 KB each) back as "cache_ram" at 0x1F20000+ when no SDRAM is used (`sdk_ld_sfc.c:25-40`). The emulator models a 64 KB cache-RAM window at 0x1F20000 (`src/cache.rs`).
  - That would be extra fast RAM for §2.1 and §2.2.
- **Risk: high.**
  - The way count conflicts with the datasheet's 6-way.
  - Fewer ways means more D-cache misses on flash rodata.
  - The register sequence is in `cpu.a`.
- **Note.** Only worth doing if §2.1 runs out of room.

### 2.4 Keep big UI work away from the audio's cache (S, partial)
- **What.**
  - Redraw only what changed. Avoid full-screen font blits while playing.
  - Copy canvas memory with `dma_copy` (SDK `asm/dma.h`) instead of CPU loops.
- **Gain.** Less cache churn and less UI time. Most of the effect overlaps with §2.1.
- **Where.** `gfx.c`, `ui_draw.c`, `lcd.c`. The LCD itself already uses DMA [V `hal/fm1_lcd_hw.h:9`].

---

## 3. Compiler

### 3.1 Build with the SDK's CPU flags: `-mcpu=r3` (S to try, M to validate)
- **What.**
  - SLOOP compiles with only `-target pi32v2 -Os -ffunction-sections -fno-builtin` (`tools/build.py:40`) [V].
  - The JieLi SDK builds all its code with `-mcpu=r3 -mfprev1` and `-mllvm -pi32v2-large-program=true` [V `apps/demo/*/board/wl82/Makefile`].
  - It links with `-pi32v2-enable-simd=true` and `-pi32v2-enable-rep-memop`. All of these options exist in the bundled clang [V, `strings`].
  - So SLOOP is built for the default pi32v2 core, which may lack instructions the WL82 has.
- **Gain.** Unknown until tried [I].
  - The current listing has no `rep` hardware loops and no packed SIMD [V: 0 `rep` in `felucca.dis`].
  - It does have 64-bit products (`r5_r4 = r4 * r14 (s)`).
  - Anywhere from 0 to 15 % on the DSP loops.
  - It also overlaps the other agent's hand-written `rep`/MAC work: if the compiler emits them, less hand ASM is needed.
- **Risk.**
  - Bit-exact: should be identical for integer code. Check the host goldens plus the emulator audio CRCs (`HANDOFF-fm1.md` regression baselines).
  - Emulator: it must decode every new instruction the compiler emits. Unknown encodings fault visibly. Use `scripts/pi32-objdump.sh` as the reference.
  - Code size changes, and only about 6.7 KB is free.
- **Measure.**
  - `tests/target_budget.py` (static).
  - The emulator's instruction count per half for the same pattern.
  - `cpu_pct` on hardware.

### 3.2 Inline the per-sample calls left at `-Os` (S)
- **What.** At `-Os` this clang (4.0.x [V `pi32v2/lib/clang/4.0.1`]) leaves three helpers out of line inside per-sample loops [V `felucca.dis`]:
  - `pd_wave` in `phase_render` (2 call sites);
  - `sample_next` in `sample_render`, every input sample;
  - `trio_naive` in `trio_render`. This one runs only on hard-sync restarts (`eng_trio.c:227`), so it is a low priority.

  (`ds_filter` and `gr_seek` are per block, so leave them.)
- **Fix.** Mark `pd_wave` and `sample_next` with `__attribute__((always_inline))`. `sample_next` is declared `static inline`, but `-Os` ignored the hint.
- **Gain.** One call/return plus register spills per sample per voice. Perhaps 5–15 % of PHASE and SAMPLE [I].
- **Effort.** S.
- **Risk.** Bit-exact. Costs a few hundred bytes of flash.
- **Measure.** `target_budget.py` should show "0 calls there".

### 3.3 `-Os` → speed for the DSP only (M)
- **What.** `-O2` for the whole unity build would not fit the 6.7 KB of free flash [I].
  - Clang 4 has no `optimize("O2")` attribute and no `#pragma clang attribute` (that arrived in clang 6).
  - Option A: build at `-O2` and mark the UI, editor, USB and project functions `__attribute__((minsize))`. This works in clang 4, but it means tagging a lot of functions.
  - Option B: split the unity build into two translation units, `dsp.o` at `-O2` and the rest at `-Os`. Statics shared across files would need to become non-static.
  - Option C (cheapest): `#pragma clang loop unroll_count(2|4)` on the 5–6 hottest sample loops, plus `always_inline` on the kernels.
- **Gain.** 5–20 % on the DSP loops [I]. Clang's `-Os` is close to `-O2` apart from unrolling and inlining thresholds.
- **Risk.** Flash. Check what each step costs in bytes.

### 3.4 Unity build / LTO
- Already one translation unit (`firmware/src/felucca.c`), so the compiler sees everything. LTO would add nothing [V].
- The SDK's `-enable-ipra` (inter-procedural register allocation) is a link-time option in their LTO flow. **Unverified** whether it applies to a single non-LTO object.

---

## 4. Algorithmic (bit-exactness changes are noted)

### 4.1 Skip idle FX buses (S/M) — best measured win
- **What.**
  - Today, `fx_buses` (`fx.c:133`) runs chorus, delay and the 4-line FDN reverb for every sample, even when every send is 0 and the lines are silent.
  - Change: keep a per-bus "input silent for N samples" counter and an "output below threshold" flag. Run the chorus only when it has input or its 2048-sample line is not yet flushed.
  - Run the delay while input or feedback energy stays above a floor. Run the reverb until its RT60-based tail is gone.
- **Gain [V measured on host].** Up to about 255 of 358 instructions per sample at idle (about 70 %), and up to about 13 % of a 3-part mix that uses no sends. With partial use, it saves the buses that are not in use [I].
- **Risk.**
  - Bit-exact while a bus runs. Goldens change only where a tail is cut below the threshold.
  - Use a threshold below 1 LSB of the 16-bit DAC [D: the DAC is 16-bit].
- **Where.** `fx.c` `fx_buses` and `mix_block`. The send sums in `mix_part` already know when `c`/`d`/`r` are 0.

### 4.2 Reverb at half rate (M)
- **What.** Run the diffusers and FDN on every second sample (22.05 kHz) and interpolate the output. The loop is already damped (`lpk`).
- **Gain.** About half the reverb's share of the ~255 [I]. The split between the three buses was not measured.
- **Risk.**
  - The output is not bit-exact; it sounds slightly darker.
  - The goldens change.

### 4.3 Release ends: free DX7 voices that are inaudible (S/M)
- **What.**
  - `regress` reports "voices not free 12 s after the note-offs" for DX7 13_TUB_BELLS and 26_DX_TUBULAR [V].
  - `dx7_note_playing` (`dx7_core.c:520`) keeps a voice while any carrier has `levels[3] > 0`.
  - A held-down tail renders all 6–8 operators for seconds and blocks the voice budget.
  - Fix: end the note when every carrier's gain stays below `DX7_THRESH` for a few blocks.
  - The generic envelope already ends at -72 dB (`voice.c` `env_tick`, `1 << 12`).
- **Gain.** For bell or pad patches played repeatedly, removes up to 8 × (6–8 ops) of dead work, the heaviest engine [I].
- **Risk.** Very quiet tails get cut. Goldens for those presets change.
- **Where.** `dx7_core.c`, `eng_dx7.c`.

### 4.4 Hoist loop-invariant branches out of sample loops (S/M)
- **What.** Several per-sample loops test values that are constant for the whole block:
  - `analog_render`: `switch (wave)`, `if (m2)`, `if (nz)`, `if (drv)` (`eng_analog.c:40-80`);
  - `ds_render`: `switch (wave)`, `src`, `fmode`, `d->t2`, `d->drive`, `s->crush`, `c` (`drum_synth.c:205-300`);
  - `mix_part`: `if (c)`, `if (d)`, `if (r)`, `g < 32767` (`fx.c:262`);
  - `drums_mix`: the `mono` and `send` tests and the kit checks.
- **Fix.** Make one specialised loop per common case (for example saw+saw no-drive, sine), or use a macro template. Clang `-Os` does not unswitch loops.
- **Gain.** Depends on branch cost on pi32v2, which is **unknown**: no branch predictor is documented. Perhaps 5–15 % of those functions [I].
- **Risk.** Bit-exact. Costs flash (each copy is a few hundred bytes), and only about 6.7 KB is free.

### 4.5 Keep drum-voice state in locals (S)
- **What.**
  - `ds_render` updates `s->ph`, `s->ph2`, `s->f1`, `s->f2`, `s->hp1`, `s->hp2`, `s->rnd`, `s->holdn`, `s->held`, ... through the pointer every sample.
  - `out` is `int32_t *`, so the compiler must assume aliasing and reload and store every field.
  - Fix: load them into locals before the loop and store them after, as `analog_render` already does ("state in locals: out[] may alias", `eng_analog.c:35`).
  - The same applies to `drums_mix`'s sampled voices (`v->s[2]`, `v->s[3]`, `drums.filter[k]`, `drums.env[k]`) and to `fx` in `fx_buses` (`fx.cho_w`, `fx.dly_lp`, `fx.line_lp[]`, `fx.line_i[]`, `fx.ap_i[]`).
- **Gain.** Removes several load/store pairs per sample. `drums_mix` has the largest static cost in `target_budget.txt` (6153). Perhaps 10–25 % of drums and FX [I].
- **Risk.** Bit-exact.

### 4.6 Remove divides from sample loops (S)
- **`analog_render`.** 20 divides and `super_render` 8 [V `target_budget.py`]. These are polyBLEP's `ph / d` (`dsp.c:26-40`), and they only run in the wrap window, about 2 samples per period [V]. That is cheap for low notes and grows with pitch and with SUPER's 7 saws.
  - Fix: a per-block reciprocal (`rcp = 2^32 / d` per voice, a multiply in the sample).
  - Gain: small for bass, measurable for high SUPER chords [I].
  - Risk: not bit-exact (rounding), but inaudible.
- **`master_out`.** It divides on every limited sample (`fx.c:117`: `(LIM_T << 15) / lim_env`).
  - Fix: a reciprocal table indexed by `lim_env >> k`, or one divide per 4 samples. It only matters while limiting.

### 4.7 Fuse the passes over the block (S)
- **What.** The extra passes:
  - `mix_block` clears 5 buffers in their own loop.
  - `audio_block` (`audio.c:24`) walks the block again to shift Q15 → 24-bit and fill the scope.
  - `mix_l += wet_l` is another pass.
- **Fix.** Fold the clear into the first writer, and the shift and scope into the master loop.
- **Gain.** About 10–20 instructions per sample [I].
- **Note.** The DAC is 16-bit [D], so `OUT_SHIFT` 7 to 24-bit gives no extra resolution. Leave the format alone; this is about the pass only.
- **Risk.** Bit-exact.

### 4.8 Larger control block, CTL 32 → 64 (M)
- **What.** Halve per-block overhead: per voice `env_tick`, glide, `tsvf_coef` (2 divides), `PITCH_INC` lookup, the engine's per-block setup, `events_block`, `duck_block`, and the FX LFOs.
- **Gain.** A few % of a full mix [I]. The per-voice setup is roughly 100–200 instructions per 32 samples.
- **Risk.**
  - Envelope and modulation steps grow from 0.73 to 1.45 ms. That is audible on fast attacks and clicks, and it adds sequencer timing jitter.
  - `CTL_LOG2` assumptions are everywhere. All goldens change.
- **Verdict.** Low priority.

### 4.9 Make voice shedding budget-aware (S)
- **What.** The 85 % shed (`audio.c`) reacts only after a late half. Add a per-engine cost weight, from the host CPU table, to `voice_room`/stealing so DX7 or SUPER polyphony caps before overload.
- **Gain.** No average speed, but fewer audible voice cuts and less `late`.
- **Risk.** Behaviour change in polyphony.

---

## 5. Second core

### 5.1 Render part of the mix on CPU1 (L) — largest ceiling, highest risk
- **Facts.**
  - The datasheet says dual-core [D].
  - The stock M-VAVE app starts CPU1 [V `rust-emulator/SMP.md`]. It writes the entry to the SPL RAM vector 0x01C7FFF8, sets C1_CON bit 3 and clears reset bit 1, and waits on a mailbox byte.
  - It pauses CPU1 around flash operations (Cx_CON bit 2 → stopped bit 4 → resume bit 3).
  - SLOOP never starts CPU1, so it is idle [V: no C1_CON use in `firmware/`].
  - The emulator already models both cores functionally [V `SMP.md`].
  - One hardware probe that tried to start CPU1 reset the device; the USB updater recovered it [V `SMP.md`].
- **What.**
  - Per block, CPU1 renders one or two synth tracks, for example tracks 2–3 or the DX7 track, into its own buffer. CPU0 renders the rest plus drums.
  - CPU0 then waits on a per-block flag and mixes.
  - Simpler alternative: CPU1 runs `fx_buses` one block behind.
- **Gain.** Up to about 1.6–1.8× synth headroom [I], limited by the slowest part and the shared bus and cache.
- **Risk / effort.**
  - CPU1's code must be in RAM or paused during flash writes, as stock does.
  - Shared state (track params written by the UI) needs a block-boundary handoff.
  - Cache coherence between cores is **unknown**: there is one shared cache controller ("corex2") in the emulator model.
  - Plan: bring-up in the emulator first, then a careful hardware test with recovery ready.

---

## 6. Hardware blocks and libraries in the SDK

| Block | Evidence | Fit for SLOOP |
|---|---|---|
| Hardware EQ (IIR biquads, up to 20 sections, int/float, interrupt-driven) | `asm/eq.h`, `JL_EQ` at lsfr 0xE000 [V] | Only static filters: the master DC blocker / LOWCUT (`fx.c` `master_out`), a fixed speaker EQ. Not the time-varying voice filters. Driver is in `cpu.a` (closed), so the registers must be worked out. Gain is small (the master path is about 40 instructions per sample [V]), at M–L effort. Low priority. |
| Hardware SRC | `asm/src.h`, `JL_SRC` [V] | Could resample drum or SAMPLE voices, but per-voice pitch changes every block. Not worth it [I]. |
| `dma_copy` / `dma_copy_async` | `asm/dma.h` [V] | UI canvas or large clears; not per-block audio (32 samples is too small to set up DMA). |
| FFT library `libFFT_pi32v2_OnChip.a` | `cpu/wl82/liba` [V] | No current use (no spectral FX). |
| `math_fast_function.h` (sin/exp/tanh fix-point) | [V] | Table lookups are already cheaper. No. |
| FPU (single precision, `-mfprev1`) | [D] "RISC 32-bit CPU (Support FPU)"; emulator decodes a subset [V `decode.rs:376`] | The integer DSP is already lean. Float may help only where 64-bit products or divides are used: DX7 `(int64)sin*g >> 24`, limiter divide, tsvf_coef divide. Cycle counts are **unknown**. Low priority; measure one kernel before committing. |

---

## 7. Emulator speed (bonus, brief; the other agent may already cover `fm1-emulator-perf`)
- The CPU runs in bounded slices on the UI thread (`README.md:84`, `ui.rs:186-232`) [V].
  - Moving it to its own thread, with audio and LCD handed over through channels, would decouple guest speed from egui frame time [I, M].
- There is already a per-PC decode cache (`code_cache.rs`) [V].
  - Next steps: superblock dispatch (no per-instruction epoch and PC checks inside a straight run), and a fast path for SRAM loads and stores that skips device dispatch [I, M].
- Fast-forward the main-loop idle wait to the next timer or audio IRQ instead of interpreting the polling loop [I, S/M].
- These would also fix the editor's 300 ms timeout at slow emulation.
- None of this can show cache or flash effects (§2): the emulator counts instructions, not cycles.

---

## 8. Measurement plan (to do before and with the ideas)
1. **Repair the CPU checks (S).**
   - Run `BUDGET_UPDATE=1` on the regress baseline after review, so SLOOP and DX7 presets are tracked.
   - Add `dx7_render`, `dx7_block` and `dx7_op*` to `tests/target_budget.py`.
   - Today the host check guards nearly nothing [V].
2. **Hardware meter script.**
   - Fixed demo project; read `cpu_pct`, `audio_max_us`, `late` and `voices_shed` from the console.
   - Repeat with the UI idle and while scrolling pages. The difference is the cache and XIP effect (§2.1).
3. **Emulator.** Instructions per half, from `examples/diagnose`, on the same project. This checks the instruction-count ideas (§3, §4) without hardware.

---

## 9. Ranked top 10 (value for effort)

| # | Idea | § | Effort | Expected gain | Bit-exact? |
|---|---|---|---|---|---|
| 1 | Skip idle FX buses | 4.1 | S/M | up to 70 % of the idle floor, ~13 % of a send-less mix (**measured on host**) | yes while running |
| 2 | Audio hot path in RAM (`.ram_hot`, ~29 KB free) | 2.1 | M | 5–12 % of each half, more during UI redraws (estimate; hardware only) | yes |
| 3 | Measure the real CPU clock (TIMER4 loop) | 1.1 | S | decides whether #9 matters | – |
| 4 | Try `-mcpu=r3` (+ SDK simd/rep options) | 3.1 | S/M | 0–15 % (unknown); may give for free what is being hand-written in ASM | should be |
| 5 | `always_inline` on `pd_wave`, `sample_next` | 3.2 | S | 5–15 % of PHASE / SAMPLE | yes |
| 6 | Drum-voice and FX state in locals (aliasing) | 4.5 | S | 10–25 % of drums and FX | yes |
| 7 | Free inaudible DX7 tails | 4.3 | S/M | large on bell/pad patches (heaviest engine) | no (tails) |
| 8 | Hot tables into RAM (~9 KB) | 2.2 | S | a few % | yes |
| 9 | Raise the clock to 320 MHz if lower | 1.2 | M | proportional (e.g. +33 % from 240) | yes (risky bring-up) |
| 10 | Render one or two tracks on CPU1 | 5.1 | L | up to ~1.6–1.8× synth headroom | yes (risky bring-up) |

Close behind:
- hoisting invariant branches (4.4);
- per-block reciprocals for polyBLEP and the limiter (4.6);
- fusing block passes (4.7);
- half-rate reverb (4.2).

Before any of this, item 1 of §8 (repairing the stale CPU baselines) is a prerequisite for trusting the numbers.
