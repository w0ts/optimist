# Shared DSP building blocks

Optimist is assembled from ports of SLOOP / Felucca, Melodee (FM6, PHASE, CZ) and X0X (the 303 voice, the 909 and
808 kits), and each port brought its own copies of the basics. This page is the inventory of those copies, what was
merged into one shared block, the proof that no caller's output changed, and what stays separate and why.

Shared modules, by kind:

| File | What | Who includes it |
|---|---|---|
| `firmware/src/dsp/dsp_common.h` | integer primitives with no table: random, clamps, rounding, error-feedback filters | `libc.c`, `dsp.c`, the float units (`x0x/x0x_drums.c`, `acid/acid_dsp.c`) |
| `firmware/src/dsp/dsp.c` | the integer blocks that read the generated tables (`felucca_tables.h`): sine, soft clip, SVF, ramps | the unity build (`engines.c`) |
| `firmware/src/dsp/dsp_float.h` | the X0X single-precision maths (was `acid/fastmath.h` and `x0x/fastmath.h`) and float blocks | `acid/bass303.c`, `x0x/drum909_dsp.h`, `x0x/drum808.c` |

Every shared block is `always_inline` (or `static inline` where the copies were), so a merge moves source, not
machine code: the measure of a merge is that each caller compiles to the same instructions (or fewer).

## How a merge is proven

1. **Device code:** each profile is built before and after; `tools/dis_diff.py`
   compares every function of the two `felucca.dis` listings with the addresses taken out. "identical" = the same
   instructions in every function of that build, so the same samples and the same cycles on the FM-1. The five
   profiles (user-default, fm-va-studio, drum-machine, everything-that-fits, x0x-drums) and **all-ports**
   (x0x-drums + ACID, PHYS, CZ, SPRING, BASS+, SLICE: every port built at once) are compared.
2. **Host renders:** `tests/dsp_ab.sh [BASE_REV]` builds the golden renders (`tests/regress.c`), the skip scenarios
   (`tests/skip_test.c`) and every X0X note (`tests/x0x_drums_test.c`) from BASE_REV and from this tree, in nine
   configurations (default; X0X kits; every backported engine and effect; X0X + ACID + GLIDE; the SLOOP 2.3
   switches; SLICE; REV_HALF + SPRING; ANALOG2=0, the original ANALOG / SUPER; USB audio), and compares hashes and
   WAVs. It also prints each render's host CPU (instructions per sample) A and B.
3. **Block against copies:** `tests/dsp_shared_test.c` keeps every replaced copy verbatim as a reference and runs it
   against the shared block, exhaustively or over 2^24 inputs (`run_tests.sh` runs it).
4. **Goldens:** `tests/golden.txt` and `tests/golden_cz.txt` unchanged.
5. **asm:** no asm kernel (`hal/fm1_dsp_asm.h`) was touched; their C twins are not merged where the asm mirrors them.

## Inventory

Location: **RAM** = the copy is inlined in a `HOT` function (RAM code, `.ram_hot`), **RAM2** = `HOT2` (`.ram_hot2`,
counted in RAM), **XIP** = flash code. Sizes: an inlined copy has no size of its own (it is part of its caller);
tables are listed with their bytes. "Identical": whether one shared block can give every copy's exact output.
Builds: which switches put the copy in (engine switches only drop the engine from the table; its code goes with it).

### Random

| # | Copies (file:line before the merge) | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| R1 | xorshift32 13/17/5: `dsp.c:121` noise32, `libc.c:79` rng, `phys_dsp.c:74` px_rand, `eng_acid.c:68` tb_rng (zero -> 1), `eng_formant.c:95` (inline, int32), `cz_native.c:197` (inline), `x0x/drum909_dsp.h:304` d9_noise (float out) | RAM (CZ, analog, trio...), XIP | always; PHYS; ACID; CZ; X909 | yes | **merged** (`xorshift32`) |
| R2 | 15-bit LFSR `(l ^ l >> 1) & 1` into bit 14: `eng_lofi.c:171`, `drum_synth.c:249` | RAM | LOFI; DRUM_SYNTH | yes | see Merged |
| R3 | LCG 1664525 / 1013904223: `voice.c:350` (unison seed), `eng_analog2.c:477` a2_rnd | XIP | always; ASM_CHECK only | yes | left (a2_rnd exists only in the asm check build) |
| R4 | taus88 `drum808.c:225` rng_frand2 | RAM-equiv. (x0x.o) | X808 | different algorithm | separate |

### Envelopes

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| E1 | ENV_LIN attack / ENV_EXP release: `voice.c:541` env_tick, `eng_analog2.c:566` ENV2 | RAM | always | attack, release yes; **decay no** (`env += mulq16(sus - env, k)` vs `fl -= mulq16(fl - sus, k)`: mulq16 floors, so ANALOG 2's decay is 1 LSB faster on some steps) | see Merged / Left |
| E2 | Q16 exponential decay `(uint32)x * k >> 16`: `drum_synth.c:183/194/196`, `drum_edit.c:198`, `eng_drawbar.c:220` | RAM | always | yes | see Merged |
| E3 | forced-to-zero decay `x -= (x >> 11) + 1`: `drums.c:469`, `bassplus.c:30` | RAM, RAM2 | always; BASSPLUS | yes | see Merged |
| E4 | segment envelopes: `fm6_eg_step` / `fm6_peg_step` (Dexed), `cz_env_tick` (uPD933), `lofi_amp` (NES steps), 909 `d9_env_*`, 808 `env_step` (SC EnvGen), 303 `env_y *= pw` | XIP / x0x.o / acid.o | | no: different curves, each exact to its original | separate |

### Saturation, clamps, rounding

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| S1 | soft knee `a > K ? K + (softclip((a - K) * 2) >> 1)`: K 16000 `eng_analog2.c:423` a2_out_c (+ asm twin), `eng_trio.c:325`, `eng_trio.c:340`, `eng_analog.c:80`, `eng_super.c:142`; K 16384 `fx.c:142` knee; K 24000 `eng_formant.c:199`; `eng_phys.c:204` (clamped) | RAM | always | yes for a given K (phys: its clamp only acts where the plain form would overflow) | see Merged |
| S2 | `clamp` copies: `phys_dsp.c:72` px_clamp, `usb_audio_stream.c:98` ua_clip (int16) | XIP | PHYS; USB audio | yes | see Merged |
| S3 | crush toward zero: `fx.c:664` crush_bits, `punch.c:183` (shift 11) | RAM | always | yes | see Merged |
| S4 | Q15 multiply rounded toward zero: `fx.c:208` mul_tz, `spring.c:65` | RAM, RAM2 | always; SPRING | yes | see Merged |
| S5 | float tanh: `fm_tanhf` (two `fastmath.h`), `d9_tanh` (909 table `x0x_tanh_tab`), `opamp_clip` (808, uses fm_tanhf) | x0x.o, acid.o | ACID; X909; X808 | the two `fm_tanhf` yes (tests/x0x_drums_test.c proves the short series for every float); the table no | see Merged |
| S6 | drive stages 909 `d9_shape*` vs 808 `shape`: shared sub-expressions only (`u / (1 + |u|)`, the fold loop) | x0x.o | X909, X808 | sub-expressions yes, the stages no (different k, gains, constants) | see Left |
| S7 | float output clip to +-2^20: `acid_dsp.c:83`, `x0x_drums.c:366` | acid.o, x0x.o | ACID; X909/X808 | yes | see Merged |

### Filters

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| F1 | Simper SVF coefficients from SVF_G: `dsp.c:146` tsvf_coef, `eng_trio.c:300` (k 7168), `drum_synth.c:104` ds_filter (k = 8192 - res 245) | RAM | always | yes given k (`eng_analog2.c` a2_coef: Q16 den, no) | see Merged |
| F2 | SVF tick: `dsp.c:160` tsvf_lp, `eng_super.c:19` tsvf_lpbp, `drum_synth.c:283` (inline, LP/BP/HP) | RAM | always / !ANALOG2 | yes (trio, ANALOG 2: different state clamps, no) | see Merged |
| F3 | one-pole `y += mulq15(x - y, k)`: fx dist / dust, sample, slice, drum_edit, drum_x0x, drum_synth, spring, bassplus (`* 692 >> 15`) | RAM | various | yes | see Merged |
| F4 | cutoff to one-pole k `4000 + (clamp(..) * 28767 >> 15)`: `eng_sample.c:175`, `eng_grain.c:415`, `eng_slice.c:351` | RAM / XIP | SAMPLE; GRAIN; SLICE | yes | see Merged |
| F5 | error-feedback one-pole high-pass, shift 6: `fx.c:132` lowcut1, `spring.c:67` (inline; `e & 63` = `e - (e >> 6 << 6)`); shift 5: `bassplus.c:44` lowcut5 | RAM, RAM2 | always; SPRING; BASSPLUS | yes | see Merged |
| F6 | 64-bit DC blocker with error feedback: `phys_dsp.c:678` (R 1073498345), `phys_symp.c:121` (R 1070617000, Q8 out) | XIP | PHYS | same form, different R and output | see Left |
| F7 | float 3rd-order TDF-II step: `bass303.c:579` (RAT), `drum808.c:345` bq3_run | acid.o, x0x.o | ACID; X808 | yes (same products and sums in the same order) | see Merged |
| F8 | float biquads: 909 DF-I, 303 TDF-II and DF-I, 808 "delta form"; one-pole HP / DC: 808 hp1, 303 dspguide HP, 303 dcb, 808 leaky | x0x.o, acid.o | | no: different structures and rounding | separate |
| F9 | resonators `fres_coef` (Klatt), `px_svf_coef`, `px_csvf_*`, the 303 ladder | | | unique | separate |

### Oscillators and phase

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| O1 | -cos from a 16-bit phase: `eng_phase.c:13` pd_cos, `eng_cz.c:47` cz_cos | RAM / XIP | PHASE; CZ | yes | see Merged |
| O2 | triangle: `dsp.c` osc_tri, `eng_trio.c:90` trio_tri16 (= osc_tri + 32768 for every phase) | RAM | always | yes | see Merged |
| O3 | naive saw `(int32)(ph >> 16) - 32768`: voice.c LFO, lofi, trio x3, osc_saw | RAM | | one expression | left (an expression, not a block) |
| O4 | polyBLEP: integer `dsp.c` blep vs `eng_trio.c` trio_blep (different form); float 303 `blep` vs 808 `blep_fix` (same polynomials, float vs uint32 phase) | | | integer no; float polynomials yes | see Merged (float polynomials) |
| O5 | SUPER vs ANALOG 2 swarm: `COPY_PH`/`COPY_AT` (24 + 6 B), super_copies, super_block | RAM | **never in one build** (FELUCCA_ANALOG2) | yes | left: not a duplicate in any image |
| O6 | sine tables: `SINE` int16 1024 (RAM, 2 KB), `SINE_PK` (RAM 4 KB, SIMD only, derived from SINE), `FM6_SIN` int32 1025 Q24 (RAM 4 KB, built by Dexed's rotation), `px_sin` (polynomial), 909/808 `fm_sinf` | RAM data | | no: FM6_SIN is Dexed's exact table (the rotation's rounding), SINE is rounded sin | see Ears |

### Pitch, exp, dB

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| P1 | `fine_inc` inline: voice.c:673, eng_analog.c:34, eng_analog2.c:603/607/608, eng_phase.c:141, eng_super.c:95, cz_native.c:187, eng_trio.c:251 | RAM | | yes | see Merged |
| P2 | DTN in cents (whole 1/16 st from PITCH_INC, the rest as fine): `eng_analog.c:32`, `eng_analog2.c:600`, `eng_phase.c:133` | RAM | | yes (trio, cz: one multiply, no) | see Merged |
| P3 | 2^x converters: `pow2_q16` (sample), `fmt_ratio` + `SEMI_Q16[13]` (52 B), `px_exp2`, `fm6_pow2`, `cz_scale` + `CZ_POW16[16]` (64 B), `QDB[24]` (48 B), float `fm_exp2f`, `d8_exp2_cr`, `d9_exp_small`, `d8_exp` | | | no: each rounds its own way | see Left |
| P4 | drum note-on dB and CUT-: `drum_edit.c:163`, `drum_x0x.c:219` | XIP | DRUM_EDIT; X0X | yes | see Merged |
| P5 | note -> Hz: 303 `tuning * exp2((n - 69) * (1/12))`, 808 `440 * exp2_cr((n - 69) / 12)` | | | no | separate |

### Interpolation, ramps, mixing

| # | Copies | Loc. | Builds | Identical | Status |
|---|---|---|---|---|---|
| I1 | sample lerp `a + ((b - a) * (frac >> 1) >> 15)`: `eng_sample.c:196`, `drums.c:457`, `eng_slice.c:390`, `eng_grain.c:309/324` | RAM / XIP | | yes | see Merged |
| I2 | IMA ADPCM nibble: `eng_sample.c:120` (index guarded), `eng_grain.c:116`, `eng_slice.c:92` | RAM / XIP | SAMPLE; GRAIN; SLICE | yes (the guard stays at its caller) | see Merged |
| I3 | Q8 fractional delay read: `fx.c:234/278`, `spring.c:84`, `reverb_alt.c` rv_tap (PLATE / FDN8) | RAM | | yes | see Left |
| I4 | Hermite: `phys_dsp.c:587` (Q16, floor), `usb_audio_stream.c:88` (Q15, rounded) | | | no | separate |
| I5 | per-block floor ramp `a + ((b - a) * i >> 5)` in fx.c, drums.c, drum_x0x.c, spring.c, a2_out_c; `amp_at` rounds toward zero | RAM | | floor copies yes, amp_at no | left (one expression each) |
| I6 | linear slew `x += clamp(t - x, -s, s)`: fx djf, spring, slicer x2, gain_next | RAM | | yes | see Merged |
| I7 | pan law `4096 -/+ pan * 64`: `fx.c:562`, `drums.c:327`, `slicer.c:201` | RAM | | yes | see Merged |
| I8 | float linear pot `lo + (hi - lo) * (pot / 127.0f)`: 909, 808 | x0x.o | | yes (303: `* (1 / 127.0f)`, no) | see Merged |
| I9 | 808 linear discharge (cymbal, hat) | x0x.o | X808 | yes | see Merged |

### Tables (generated and static)

| # | Tables | Bytes | Identical | Status |
|---|---|---|---|---|
| T1 | `felucca_tables.h` (gen_tables.py): SINE, PITCH_INC, ENV_*, LFO_INC, SVF_G, LEVEL_Q12, DECAY_K, TANH_Q15, FM6_* | | one copy each already; no table is copied elsewhere | nothing to merge |
| T2 | `x0x_drum_tables.h` (gen_x0x_drums.py): `x0x_tanh_tab[1026]` float, `x0x_pot_exp[18][128]` | 4,104; 9,216 | 909 only; the 808 computes the same pot curves at run time with another pow (last bits differ) | see Ears |
| T3 | FM6 RAM tables built from flash ones: `FM6_MKI_LOG` (2 KB RAM) from `FM6_MKI_LOGQ` (2 KB flash), `FM6_OPL_LOG` (1 KB) from `FM6_OPL_LOGQ` | | the RAM copy is the speed path (FELUCCA_FM6_MKI_FLASH reads flash instead) | left (a deliberate RAM/flash trade) |
| T4 | 2^(k/N): `SEMI_Q16[13]`, `CZ_POW16[16]`, `QDB[24]` vs PITCH_INC ratios | 52, 64, 48 | no (rounding) | see Left |
| T5 | `GR_HANN[257]` (RAM, sin^2 window) ~ `(32767 - SINE[...]) / 2` | 514 | no | see Ears |
| T6 | `COPY_PH`, `COPY_AT` (SUPER / ANALOG 2) | 30 | never in one build | left |

A scan of every function and every data object of the five profiles' images for identical bodies
(address-normalised) found **no** two identical functions and no duplicated DSP table: every duplicate is source
that the compiler inlined into its callers, or tables of different formats.

Checked after optimist f4d854b added `reverb_alt.c` (REVERB = 1 PLATE, 2 FDN8): it already calls the shared blocks
(`clamp` through rv_sat, `mul_tz`, fx.c's `fx_step`, `osc_sine`, `LFO_INC`). Its Schroeder allpass `rv_ap`
(g Q15, `mul_tz` both ways) is not the ROOM's diffuser in fx.c rev_step (g = 1/2 through `half_ap` and `v >> 1`:
other rounding), and `rv_tap` is one more Q8 delay read (I3). Nothing to merge.

## Merged

Sizes: flash / RAM / RAM code of the five profiles and all-ports, before -> after. Device code: tools/dis_diff.py over
the six builds. Host: tests/dsp_ab.sh (nine configurations) and tests/dsp_shared_test.c.

| Block (shared as) | Copies replaced | Sizes, six builds | Device code | Host renders, CPU |
|---|---|---|---|---|
| R1 `xorshift32` (dsp_common.h) | noise32, rng, px_rand, tb_rng, formant RAND, CZ ring noise, d9_noise | +0 B everywhere | every function identical | same; CPU within noise |
| R2 `lfsr15` (dsp_common.h) | LOFI NES noise, CHIP drum noise | +0 B | identical | same |
| S2 `clamp` (moved to dsp_common.h) | PHYS px_clamp, USB capture ua_clip | +0 B | identical except ua_audio (same 310 B, same instruction count: min/max order) | same |
| S3 `crush_tz` (dsp_common.h) | DUST crush_bits, PUNCH CRUSH | +0 B | identical | same |
| S4 `mul_tz` (dsp_common.h) | fx.c delay / reverb loops, SPRING loop gain | +0 B | identical | same |
| F5 `lowcut_ef(x, lc, err, shift)` (dsp_common.h) | LOWCUT lowcut1, BASS+ lowcut5 (SPRING keeps its copy, below) | +0 B | identical | same |
| F2 `tsvf_tick` (dsp.c; tsvf_lp and tsvf_coef now call tsvf_tick / tsvf_coef_k) | SUPER tsvf_lpbp, the synth drums' SVF step (inline) | +0 B | identical | same |
| P1 `fine_inc` call sites (dsp.c) | voice.c TUNE, ANALOG 2 / SUPER drift, CZ detune (inline copies) | +0 B | identical except cz_native_render (same 1,146 B, same instructions, two swapped) | same |
| P2 `det_inc(pitch16, det, fine)` (dsp.c) | DTN of ANALOG 2, PHASE, original ANALOG | +0 B | identical | same |
| O1 `neg_cos16` (dsp.c) | PHASE pd_cos, CZ cz_cos | +0 B | identical | same |
| I2 `ima_nibble` + IMA_STEP / IMA_IDX (moved to dsp.c) | IMA decode of SAMPLE, GRAIN, SLICE | +0 B | identical | same |
| I1 `lerp16` (dsp.c) | resampler interpolation in SAMPLE, the drum lanes, SLICE, GRAIN x2 | +0 B | identical except grain_render (same 2,450 B, same instructions: frac >> 1 arithmetic instead of logical, frac >= 0) | same |
| F4 `smp_lp_k` (dsp.c) | SAMPLE, GRAIN, SLICE low-pass coefficient | +0 B | identical | same |
| I7 `pan_gains` (dsp.c) | fx.c mix_part, drums.c, slicer.c | +0 B | identical | same |
| E2 `decay_q16` (dsp.c) | synth drums (3), drum_edit DECAY-, DRAWBAR percussion | +0 B | identical | same |
| E3 `decay_to0` (dsp.c) | drums.c 808-style envelope, BASS+ envelope | +0 B | identical | same |
| P4 `dl_cut_k`, `dl_lvl_g` (drum_synth.c, beside ds_onepole) | drum_edit.c and drum_x0x.c note-on edits | +0 B (drum_on -2 B, XIP) | identical but drum_on (2 B smaller, note-on) | same |
| S5 X0X maths: `acid/fastmath.h` + `x0x/fastmath.h` -> `dsp_float.h`; `acid/x0x_param.h` + `x0x/x0x_param.h` -> `x0x_param.h` | the two copies of each (they differed only in fm_tanhf's early stops, proven bit-identical; ACID's unit keeps them off: `FM_TANH_SHORT 0`) | +0 B (with the early stops in ACID too: +160 B flash in bass303_init, drive_soft, run_osc_sqr, so off there) | identical in all six builds (acid.o and x0x.o) | same |
| F7 `fm_tdf3` (dsp_float.h) | 303 RAT op-amp stage, 808 cymbal bq3_run | +0 B | identical | same |
| O4 `fm_blep_after`, `fm_blep_before` (dsp_float.h) | 303 blep, 808 blep_fix polynomials | +0 B | identical | same |
| S6 `fm_fold3` (dsp_float.h) | 909 and 808 drive type 5 fold loop | +0 B | identical | same |
| S7 `fm_clip_sym` (dsp_float.h); `fm_clampf` for the 808's clampf | ACID and X0X output clip; 808 clampf | +0 B (fm_clampf for the clip: +2 B in acid_render and x0x_render, so the order-keeping fm_clip_sym) | identical | same |
| I8 `fm_lin_pot` (dsp_float.h) | 909 d9_pot_value, 808 pot_value (linear) | +0 B | identical | same |
| I9 `fm_discharge` (dsp_float.h) | 808 cymbal discharge (the hi-hat keeps its copy, below) | +0 B | identical | same |
| S1 `soft_knee(x, k)` (dsp.c) | fx.c knee, ANALOG 2 a2_out_c, SUPER, TRIO x2 | +0 B everywhere | identical except trio_render: same size, same instruction count in its loops (target cost 341 = 341), registers swapped | same; CPU within noise |

Result: the six builds compile to the same machine code as optimist 568f906, and the five profiles again after merging optimist b4b5cc8 (ui-pass, cpu-items, patterns phase 0, div0 trap off, REVERB tanks, drum sends): same sizes (x0x-drums 16 B smaller: drum_on), every function identical, except the
same-size reorders named above and drum_on, 2 B smaller; flash, RAM and RAM code unchanged in every profile
(everything-that-fits keeps its 20 B of RAM code). The gain is in the source: 25 merges (the table above) replacing 72 copies, one place
each, each with a test against the copies it replaced. No merge freed RAM code: every copy was already inlined
into its caller, so one source still compiles into each caller (see "Trades" for the two measured ways to free
some).

## Needs your ears

Not merged: these would change samples. WAV pairs (A = as now, B = the shared block), the null-test numbers and a
README in `~/GitHub/fm1-firmware/screens/dsp-dedupe-2026-10-07/`.

| Candidate | Gain | Difference (null test) | CPU |
|---|---|---|---|
| FM6's own sine `FM6_SIN` (Dexed's Q24 table, RAM) -> the shared `SINE` | 4,100 B RAM in FM6 builds (needs FM6 and its asm operator kernels to read SINE: not written) | only ENGINE MODERN uses it: BRASS SECT -65 dB re signal (peak 64 steps); MARK I presets identical | host unchanged |
| GRAIN's window `GR_HANN` -> (1 - cos) / 2 from `SINE` | 514 B RAM + 514 B flash | -69 to -72 dB re signal (peak 17-18 steps) | host unchanged |
| the 909's tanh table `x0x_tanh_tab` -> `fm_tanhf` | 4,104 B flash in X909 builds | -61 to -90 dB re signal | **+25 %** host on the 909 groove: not worth it |

## Trades (bit-identical, measured, not done: a choice for you)

| Change | RAM code | Flash | CPU |
|---|---|---|---|
| drum_synth.c ds_filter through `tsvf_coef_k` (via a local tsvf_t) | everything-that-fits **-40 B** (20 -> 60 B left); user-default +8 B, drum-machine +4 B | same | drums_mix target cost 4081 -> 4084 (user-default), 4047 -> 4035 (everything-that-fits) |
| X0X's whole-kit renders `drum909_render` / `drum808_render` are linked but never called (x0x_drums.c renders the lanes itself) | 0 | x0x builds -1,440 B (909 render out; its voices then inline into x0x_render) | emulator bench 8: 909 -1.8 %, 808 **+0.4 %** ISR instructions; with the 909 voices kept out of line: -1,056 B flash, 808 the same, 909 +0.23 %. Dropping the 808 render too: -16 B, 808 +3.3 % |

## Left separate

| Block | Why |
|---|---|
| soft knee in FORMANT (24000) | the shared call costs formant_render 4 B of RAM code (register allocation) |
| soft knee in the original ANALOG (ANALOG2=0, no profile) | the host compiles the call 8-12 % slower |
| soft knee in PHYS | it clamps the excess first (its int64 input can overflow the plain form) |
| SVF coefficients in TRIO and ds_filter | `tsvf_coef_k` costs trio_render 18 B of RAM code; ds_filter: see Trades |
| SPRING's error-feedback low cut | `lowcut_ef(&sp.hp, &sp.he)` stops clang splitting `sp` into scalars: +184 B flash |
| the 808 hi-hat's linear discharge | `fm_discharge` costs hat_tick 2 B (the cymbal's uses it) |
| TRIO's trio_tri16 (= osc_tri + 32768) | it is ANDed with the saw; the masked form is the cheap one |
| ANALOG 2's osc-2 drift `inc2 -= ...` | the same bits as fine_inc(inc2, -d) but written as a subtraction; left as is |
| E1 env attack / release (voice.c, ANALOG 2 ENV2) | the decay steps differ (mulq16 floors a negative product one lower): only parts are the same |
| one-pole `y += mulq15(x - y, k)`, Q8 delay reads, per-block ramps, naive saw, slews | one expression each, already on the shared mulq15 / clamp; a helper per expression adds names, not sharing |
| segment envelopes (FM6, CZ, LOFI, 909, 808, 303), biquad forms (909 DF-I, 303 TDF-II, 808 delta form), exp / exp2 / tanh approximations (fm_*, d8_*, d9_*, px_*, fm6_*), Hermite (PHYS vs USB resampler), 2^x tables (SEMI_Q16, CZ_POW16, QDB) | different curves, structures or rounding: each exact to its original (Dexed, uPD933, 9W9, 8W8, Open303, DaisySP) |
| 909 vs 808 drive stages (types 0-4, 6) | different k, gains, constants (BFZ 0.18033 vs 0.22/1.22), `* (1/3)` vs `/ 3`; the fold (type 5) is shared |
| SUPER vs ANALOG 2 swarm (`COPY_PH`, `super_copies`, ...) | never in the same build (FELUCCA_ANALOG2) |
| FM6 RAM copies of flash tables (MARK I log / exp, OPL log) | the RAM copy is the speed path; FELUCCA_FM6_MKI_FLASH already offers the flash one |
| asm twins (`asm_a2_out`, `asm_a2_drive`, ...) | they mirror their C kernels instruction for instruction; ASM_CHECK compares them (untouched) |

## Licences

`dsp_common.h` and `dsp.c` are GPL-3.0-only (Felucca, Leo Kuroshita); every block names the files whose copies it
replaced. Files under other licences that now call a GPL block: `phys_dsp.c` (MIT, Electrosmith / Emilie Gillet:
px_rand, px_clamp) and `cz_native.c` (BSD-3-Clause, Devin Acker / Kerem Kilic: the ring noise, the detune); their
own notices are unchanged and the firmware as a whole is GPL-3.0. `dsp_float.h` and `x0x_param.h` keep X0X's
GPL-3.0-only and Charles Vestal's line (LICENSING.md, tools/backports.json name the new paths). No code moved out of
an MIT or BSD file into a GPL one.
