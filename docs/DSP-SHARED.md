# Shared DSP building blocks

Optimist is assembled from ports (SLOOP / Felucca; Melodee for FM6, PHASE, CZ; X0X for the 303 and the 909 / 808 kits;
fm-106 for JUNO; Felucca-Engines for NOISE), and each brought its own copies of the basics; the copies were merged into
the blocks below. Every shared block is `always_inline`, so a merge moves source, not machine code, and frees no RAM code
(each copy was already inlined into its caller). New DSP code should call these blocks.

| File | Holds |
|---|---|
| `firmware/src/dsp/dsp_common.h` | table-free integer primitives: `xorshift32`, `lfsr15`, `clamp`, `crush_tz`, `mul_tz`, `lowcut_ef` |
| `firmware/src/dsp/dsp.c` | blocks that read the generated tables: `soft_knee(x, k)`, `tsvf_tick`, `tsvf_coef_k`, `fine_inc`, `det_inc`, `neg_cos16`, `ima_nibble`, `lerp16`, `smp_lp_k`, `pan_gains`, `decay_q16`, `decay_to0` |
| `firmware/src/dsp/dsp_float.h`, `x0x_param.h` | X0X's float maths (one `fastmath.h` instead of two, bit-identical; ACID keeps `FM_TANH_SHORT 0`) and `fm_tdf3`, `fm_blep_after / before`, `fm_fold3`, `fm_clip_sym`, `fm_clampf`, `fm_lin_pot`, `fm_discharge` |
| `drums/synth/drum_synth.c` | `dl_cut_k`, `dl_lvl_g` (drum note-on dB and CUT-, used by `drum_edit.c` and `drums/x0x/drum_x0x.c`) |

Users include fx.c, `fx/insfx/` (`fxi_kern.c`, `tapespit.c`), `fx/reverb/reverb_alt.c`, BASS+, SPRING (`mul_tz` only), the
analog / trio / super / phase / cz / sample / grain / slice / formant engines, the drum lanes and the X0X units.

## How a merge is proven

1. **Device code**: build each profile (`config/profiles/`) and an all-ports build before and after;
   `tools/dis_diff.py A.dis B.dis` compares every function of the two `felucca.dis` listings with addresses removed.
   Identical = same instructions = same samples and cycles on the FM-1. (Accepted: same-size reorders in TRIO, CZ
   detune, GRAIN; `drum_on` 2 B smaller.)
2. **Host renders**: `tests/dsp_ab.sh [BASE_REV]` builds the golden renders (`tests/regress.c`), skip scenarios and every
   X0X note from BASE_REV and from this tree in eight configurations (default; X0X kits; backported engines and
   effects; X0X + ACID; SLICE; REV_HALF + SPRING; `ANALOG2=0`; USB audio) and compares hashes.
3. **Block against copies**: `tests/dsp_shared_test.c` keeps every replaced copy verbatim and runs it against the shared
   block, exhaustively or over 2^24 inputs. 4. `tests/golden.txt` and `golden_cz.txt` unchanged. 5. The asm kernels
   (`hal/fm1_dsp_asm.h`) are untouched; their C twins are not merged where the asm mirrors them.

A scan of every function and DSP table of the profile images found no identical functions and no duplicated table.

## Left separate, and why

| What | Why |
|---|---|
| soft knee in FORMANT (K 24000), PHYS, original ANALOG | the call costs `formant_render` 4 B of RAM code; PHYS clamps first (int64); original ANALOG 8-12 % slower on the host |
| SVF coefficients in TRIO, `ds_filter` | `tsvf_coef_k` costs `trio_render` 18 B of RAM code; `ds_filter` moves `drums_mix` by -40..+58 B by profile |
| SPRING's low cut | `lowcut_ef` there stops clang splitting `sp` into scalars: +184 B flash |
| the 808 hi-hat discharge, TRIO `trio_tri16`, ANALOG 2 osc-2 drift | +2 B of code, a cheaper masked form, a bit-identical subtraction |
| ENV attack / release (voice.c, ANALOG 2's ENV 2) | the decay differs: `mulq16` floors a negative product one lower (1 LSB on some steps) |
| one-pole `mulq15` updates, Q8 delay reads, ramps, naive saw, slews | one expression each; a helper adds names, not sharing |
| segment envelopes (FM6, CZ, LOFI, 909, 808, 303), biquad forms, exp / exp2 / tanh approximations, Hermite (PHYS vs USB), 2^x tables (`SEMI_Q16`, `CZ_POW16`, `QDB`), 909 vs 808 drive stages, taus88 | different curves, structures or rounding; each exact to its original (Dexed, uPD933, 9W9, 8W8, Open303, DaisySP) |
| SUPER vs ANALOG 2 swarm | never in one build (`FELUCCA_ANALOG2`) |
| asm twins (`asm_a2_out`, ...) | mirror their C kernels; `ASM_CHECK` compares them |
| JUNO (`engines/juno/juno_dsp.h`) and JUNO CHORUS (`fx/junocho/juno_cho.h`) | fm-106 ports (GPL-3.0-only), self-contained (stdint only) so the harness builds them beside fm-106's headers |
| NOISE (`engines/noise/eng_noise.c`) | MIT, from Felucca-Engines; only the glue to `engine_t` and `dsp.c` changed |
| `reverb_alt.c` | already calls the shared blocks; its allpass rounds differently from the ROOM diffuser |

## Would change samples (not done; measured 2026-10-07 unless said)

- FM6's `FM6_SIN` (Dexed's Q24 table, `engines/fm6/eng_fm6.c`) to the shared `SINE`: 4,100 B RAM, needs the asm operator
  kernels to read `SINE`. Not re-measured since FM6 became the Dexed-exact core only (2026-10-18).
- GRAIN's `GR_HANN` to `(1 - cos) / 2` from `SINE`: 514 B RAM + 514 B flash, -69 to -72 dB re signal.
- The 909's `x0x_tanh_tab` to `fm_tanhf`: 4,104 B flash, -61 to -90 dB, but +25 % host CPU on the 909 groove.
- `drum909_render` / `drum808_render` are kept but x0x_drums.c renders the lanes itself: dropping the 909's saves 1,440 B in
  X0X builds (909 -1.8 %, 808 +0.4 % ISR instructions); dropping both costs the 808 +3.3 %.

## Licences

`dsp_common.h` and `dsp.c` are GPL-3.0-only (Felucca, Leo Kuroshita). `engines/phys/phys_dsp.c` (MIT, Electrosmith / Emilie
Gillet) and `engines/cz/cz_native.c` (BSD-3-Clause, Devin Acker / Kerem Kilic) keep their notices while calling GPL blocks;
the firmware as a whole is GPL-3.0. `dsp_float.h` and `x0x_param.h` keep X0X's GPL-3.0-only and Charles Vestal's line
(`LICENSING.md`, `tools/backports.json`). No code moved out of an MIT or BSD file into a GPL one.
