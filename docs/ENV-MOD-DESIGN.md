# ENV 1 as the engine's own envelope: design (audit + plan, no firmware changed)

Branch `design/env-mod` from LOCAL optimist 9be3754a. Status: design only. The user ruled on 2026-10-16 that ENV 1 is
**not parametric**: it has no DEST/AMT pickers. ENV 1 is the engine's own envelope. Its destinations are fixed and
made in advance, one plain depth knob each, chosen per engine. ENV 2, LFO 1 and LFO 2 keep their free DEST A/B + AMT A/B.

Evidence: batch 141 = `062e3f19` "ENV DEST and ENV2 DEST: two free slots each" (plus `6a810504`, which stores the slots
in the modulation record and as UP_VER 3, and `f4bf244d`, which has JUNO read `ENV1_FLT_AMT`). Before that commit
(`062e3f19~1`, voice.c 1080-1121), ENV worked like this: the amplitude was **hard-wired** (`m.envq15 = env`), plus
`m.cutoff += envq15*P_ED_FLT>>7` and `m.shape += envq15*P_ED_SHP>>7`. ENV DEST PIT (`P_ED_PIT`) was **not ENV**: it is a
fixed ~40 ms pitch drop (`v->penv`), now called DROP. ANALOG's ENV2 had five fixed depths (FLT PIT SHP OSC2 SDTN).

## 1. Per engine

"Reads" lists which outputs of the voice.c mod path the engine actually uses (grep of `m->cutoff`, `m->shape`,
`envq15`, `amp_at`/`amp1`, pitch). A dead path means an ENV slot on it does nothing today.

| Engine | Envelopes it has (besides ENV 2, which runs on every engine) | Reads | ENV 1 slots today (default A=AMP 127, B=FLT or SHP from the preset) | Before 141 | **Proposed ENV 1 page: 4 fixed depth knobs** (amp always wired, see §1a) |
|---|---|---|---|---|---|
| ANALOG (and SUPER, its swarm alias) | track ADSR | cut, shape=PW, pitch, amp | all work | amp, FLT, SHP | **FLT, PIT, SHP(PW), RES** |
| DIGITAL | ADSR + MDEC (the modulator index EG, internal) | cut+shape both drive the FM index | FLT = SHP = index | amp, FLT, SHP (same effect) | **IDX** (the FLT path), **PIT**, **FB** (E6), **MDEC** (E5) |
| PHASE | ADSR; E3 "ENV" = ADSR to DCW amount | cut+shape both drive DCW depth, envq15 | FLT = SHP = DCW | amp, FLT, SHP | **DCW** (alias of E3 ENV), **PIT**, **DTN** (E4), **SUB** (E6) |
| LOFI | ADSR, or the STEP chip's 4-bit env (lofi_amp) when CHIP=STEP; SWP = pitch sweep | cut (positive only), pitch | FLT only | amp, FLT | **TONE** (FLT path), **PIT**, **DUTY** (E2), **SWP** (E4) |
| SAMPLE (the PIANO/VIBES/... sets) | ADSR | cut, pitch | FLT, PIT | amp, FLT | **CUT** (FLT path), **PIT**, **BITS** (E2), **DRV** (E6) |
| VOICE (FORMANT) | ADSR + TALK (vowel glide) | cut=vowel, shape=BUZZ | FLT, SHP | amp, FLT, SHP | **VOWL** (FLT path), **PIT**, **BUZZ** (SHP path), **Q** (E6) |
| TRIO | ADSR | cut, shape=PW | all | amp, FLT, SHP | **FLT, PIT, PW** (SHP path), **RES** (E6) |
| WHEEL (DRAWBAR) | ADSR (organ gate) + PERC decay + CLICK | pitch, amp only | FLT/SHP dead | amp only in effect | **PIT, DRV** (E6), **CLICK** (E5), **TOP** (E3) |
| GRAIN | ADSR | cut=TONE, shape=POS | all | amp, FLT, SHP | **TONE** (FLT path), **PIT**, **POS** (SHP path), **DENS** (E3) |
| FM6 | **6 DX7 operator EGs + pitch EG** shape the sound; ADSR opens at once, ~10 s (fm6_amp) | shape=MOD, pitch (plog), amp | SHP=MOD, FLT dead | amp, SHP | **special:** **M.TIM, C.TIM** (aliases of E2/E3: the op EG rates), **MOD** (SHP path), **PIT** |
| SLICE | ADSR + DCAY | cut, pitch | FLT, PIT | amp, FLT | **TONE** (FLT path), **PIT**, **DCAY** (E6), **START** (E2) |
| PHYS | ADSR + the model's own decay (DAMP) | cut=BRIT, pitch | FLT | amp, FLT | **BRIT** (FLT path), **PIT**, **DAMP** (E3), **EXC** (E7) |
| ACID | **303 MEG/VEG** (its own); the ADSR only gates | **nothing** (`(void)m`: ENV, LFO, pitch never reach it) | all dead, AMP too | none | **special:** aliases of the 303's own: **ENV** (E2), **DEC** (E3), **ACC** (E4), **CUT** (E0) |
| CZ | **DCO/DCW/DCA 8-step EGs, 2 lines**; ADSR ignored (cz_amp gives 32767) | pitch, amp (a constant) | FLT/SHP dead, AMP a no-op | none | **special:** aliases: **DCW** (E1), **W.TIM** (E2), **A.TIM** (E4), plus **PIT** (works) |
| JUNO | ADSR = the Juno ENV (VCA curve juno_amp; VCF ENV depth through ENV1_FLT_AMT) | cut (own scaling), shape=PW, envq15 | FLT special, SHP | amp, FLT, SHP | **VCF ENV** (FLT, juno_dac as today), **PIT**, **PW** (SHP path), **RES** (E5) |
| NOISE | ADSR | cut=FREQ, shape=COLR | all | amp, FLT, SHP | **FREQ** (FLT path), **PIT**, **COLR** (SHP path), **RES** (E3) |
| Drum track / drum sources (808, 909, drum synth, samples) | each lane's own: CLICK DECAY BEND LEVEL (DSND ENV page) | n/a | no ENV slots (drum track has no P_N1*) | the same | **no change**: the drum ENV page already works this way (fixed and made per source) |

### 1a. The card kinds and the amplitude

* **Amplitude: always wired, as it was before 141.** `amp = env` (after `e->amp`, the engine's own curve). No AMP
  knob. Engines whose own envelope ends the voice (CZ, FM6, ACID, LOFI STEP) keep ending it as they do today.
* Each of the 4 cards is one of three kinds, held in a flash table per engine (`env1[4] = {label, kind, arg}`, about
  16 engines x 4 x 3 B = ~200 B of flash):
  1. **Path depth.** FLT, SHP or PIT on the voice paths, with the arithmetic from before 141 (`envq15*d>>7`; PIT is
     new and works like today's LT_PIT, +-31.5 st at full).
  2. **Value depth (E-move).** ENV moves one EDIT value for each voice, using today's `env_vmove` (XIP flash, as LC_VAL
     works now).
  3. **Alias.** The card is the engine's own EDIT value that already controls its envelope (ACID, CZ, FM6, PHASE DCW).
     This needs no new storage and no DSP change: it is the same parameter shown a second time on the ENV 1 page.

## 2. ENV 2, LFO 1, LFO 2: kept as built

Their pages, ids (P_N2DA..P_N2AB, P_L1DA.., P_L2DA..), the shared LT_* list and storage stay exactly as built. Three
small changes:

* `LT_N1AMT` ("E1 AMT") keeps its number. It now scales ENV 1's depth knobs (not AMP).
* `LT_ATK`..`LT_REL` stay (ENV 1's times as destinations).
* `LT_AMP` on ENV 2 or an LFO multiplies on top of the wired ENV 1 amplitude, as it does today.

## 3. Migration (formats may change; Optimist's own old files must load sensibly)

**Ids.** `P_ED_FLT` and `P_ED_SHP` become live again as ENV 1's FLT and SHP depths (-64..63). Every format already
stores these words: project track words, UP_VER 3 words, SLOOP export, the editor. Since 141 they are written as 0. The
four ENV DEST ids are repurposed so that no id moves and P_COUNT stays the same: `P_N1DA` becomes `P_E1PIT` (ENV 1 PIT
depth), `P_N1AA` becomes `P_E1X` (card 4: the E-move depth), and `P_N1DB`/`P_N1AB` are reserved (hidden, def 0). An
alias card has no id of its own.

| Source | How it loads |
|---|---|
| Project from before 141 (no modulation record, or tag-less) | The words already hold FLT/SHP depths: **as is**, same sound. DROP is unchanged. |
| Project 141..now (modulation record `MREC_TAG 0xD2`, parts' ENV DEST bytes) | Fold the slots: AMP (any AMT) is dropped because amp is wired. FLT a goes to `P_ED_FLT += a/2`, SHP a to `P_ED_SHP += a/2`, PIT a to `P_E1PIT = a`. Anything else (RES, E1..E8, ATK..REL, PAN/LVL/sends/INS, L1/L2, E2 AMT, OSC2, SDTN): **moved to ENV 2** if ENV 2's slots are all OFF (copy ATK DEC SUS REL into ATK2..REL2, then put the slot in ENV 2 A/B). Otherwise it is **dropped and counted** in the TOOLS > MISS style "lost" note. |
| New saves | `MREC_TAG 0xD3`. The parts' block keeps 20 bytes; ENV 1's 4 bytes become E1PIT, E1X, 0, 0. FLT/SHP go back in their words. |
| User presets UPB3 / UP_VER 3 | Read with the same fold. Written as **UP_VER 4**: the same 216-byte record, with slot bytes 0..3 reinterpreted. UPB2/UPB1 read as today (depth words, so as is). |
| Factory presets (`preset_t.fenv`, XP `P_ED_SHP`) | Straight into the depths. `env_load` is no longer needed for ENV 1. |
| Sections (SEC records), motion and locks on P_N1DA..P_N1AB | Convert like params.c's "event on a depth of before": AMT on a FLT slot becomes a P_ED_FLT event (/2), and so on. Events on DEST ids, or on dropped targets, are dropped. |
| SLOOP bank export (sl24_export) | Simpler: FLT/SHP words direct. `env_store`'s ENV 1 half is deleted. |
| Web editor files (`DST A#1`.. label keys, editor.html 3344-3387) | Map ENV DEST's DST A/B and AMT A/B into FLT/SHP/PIT using the same fold. ENV2's `DST A#2` is unchanged. |

**Lost on load:** only post-141 projects that put ENV 1 on a non-path destination while ENV 2 was already in use, plus
odd AMTs (one LSB). ENV 1 AMP below 127 or negative also cannot be kept, because amp is wired: the sound changes.

## 4. UI impact

* PAGES (params.c 687) changes to `{"ENV DEST" -> "ENV 1 MOD"?, {P_ED_FLT, P_E1PIT, P_ED_SHP, P_E1X}}`. Cards are
  labelled and redirected per engine through the `env1[]` table (in `page_desc`/`track_desc`, the same way E1..E8 get
  names). Alias cards show the EDIT value. SLOOP's UI uses the same PAGES, so it changes too. ENV, ENV2 and ENV2 DEST
  are unchanged.
* Optimist UI: `op_effval.c` 127-140 (ENV slots in the effective-value display) reads the depths. `op_graph.c` and
  `op_names.c` need relabels. `ui_draw`, `ui_layers`, `ui_menu` and `ui_overview` (SLOOP) need a re-check.
* `tools/param_help.json`: ENV DEST becomes per-engine card help (about 16 x 4 short lines; `gen_param_help.py`).
* Web editor: the Sound cards are descriptor-driven, so labels come from the device. Regenerate the mock tables
  (`tests/scrs_table.json`, `tools/gen_fxp_table.py`) and update `web/test_web.mjs`, `web/EDITOR_PROTOCOL.md` (same ids,
  new meaning: bump INFO's version so an old editor does not send DST values) and editor.html's file conversion.
* `docs/guide/sound.md` §"ENV and ENV 2" / §"Destinations": ENV 1 gets fixed depths and a per-engine table. The
  "AMP 127 default" rule goes away. `docs/UI-OPTIMIST-DESIGN.md` needs the ENV page update.

## 5. Audio engine, cost, risk

* **voice.c.** `env_plan`: the k=0 slot loop is replaced by `am[0][EA_FLT]=2*P_ED_FLT`, `am[0][EA_SHP]=2*P_ED_SHP`,
  `am[0][EA_PIT]=P_E1PIT`, and `E1X` as one `mv_*` entry (an E-move). `env_plain` becomes "ENV 2 off, E1PIT=0, E1X=0".
  The plain RAM path keeps today's FLT/SHP arithmetic and drops the AMP-gain branch, so RAMTEXT is the same or a few
  bytes smaller. PIT and E1X run only on the slow path (XIP `env_post`/`env_vmove`), so **0 B of new RAMTEXT**.
  **0 B of new RAM**: the values sit in existing `t->p` words and `env_plan_t` is unchanged. The engine tables are
  flash. The env_store.h ENV 1 half and its `env1_from_depths` shrink to a load-only fold. This fits the 276 B RAM
  margin (the brief's figure, not re-measured here).
* **Cost per voice.** The default (depths only) costs the same as today's plain path. E1PIT adds one multiply per voice
  per block. An E-move costs what an ENV slot on E-n costs today. The CPU guard (`cpuguard_costs.h`) has no modulation
  term today, so nothing changes there. Measure `env_dest_test`'s slow path in the batch costs.json run.
* **JUNO.** `ENV1_FLT_AMT(p)` becomes `2*p[P_ED_FLT]` (the same numbers).
* **Risks.**
  * Post-141 user sounds that used ENV 1 freely: some are re-routed or lost (see §3).
  * ENV 1 AMP below 127 can no longer be expressed.
  * ACID, CZ and FM6 ENV 1 cards are aliases, so turning them changes the sound on the EDIT page too (by design).
  * E-move cards on engines where ENV 1 is a gate (WHEEL) follow the ADSR.
  * Factory presets: no change, because the fold of A=AMP127 + B=FLT/SHP gives back the exact depths.

## 6. Plan (slices) and tests

| Slice | Content | Reserve |
|---|---|---|
| E1 | core: ids repurposed (P_E1PIT, P_E1X, 2 reserved), env_plan/env_plain, the `env1[]` per-engine table in engine_t, JUNO macro | no builder item (core behaviour; ENV 2 stays under ANALOG2) |
| E2 | storage: MREC_TAG 0xD3 + the 0xD2 fold, UP_VER 4, sections/locks conversion, sl24_export, ed_backup | MREC tag 0xD3, UP_VER 4 |
| E3 | UI: PAGES, page_desc labels/aliases, op_effval/op_graph, SLOOP pages, param_help | none |
| E4 | editor: INFO bump, file conversion, mock tables, test_web | INFO version +1 |
| E5 | docs: sound.md, UI-OPTIMIST-DESIGN.md, DOC-COVERAGE | none |

Tests:
* **Golden renders** (`tests/golden.txt`, regress.c; golden_cz/juno/noise) **should not change**, because every preset
  folds back to its depths. Any golden diff is a bug. They change only where the user picks a different card meaning
  for an engine whose default moves away from 0.
* `env_dest_test.c` is rewritten as `env1_test.c`. Per engine, for every card: at 0 the render is bit-equal to a
  zero-depth render. For path cards, +-63 moves the expected path (cutoff, shape, pitch Hz). E-moves change the named
  value per voice and restore it after render. Alias cards equal the EDIT value.
* Migration tests in project_test, upreset_test, sl25_upreset_test and motion_sections_test: fixtures from 141..now with
  ENV 1 on AMP/FLT/SHP/PIT/RES/E3/PAN, and with ENV 2 free and busy. Check the lost count and the copied ADSR.
* ui_pages_test and ui_optimist_test: labels per engine. test_web: mock table.

## 7. Open questions (short, per engine; recommended first)

1. ANALOG/TRIO/JUNO/NOISE card 4: **RES** / DRV / NOIS(ANALOG) / none?
2. DIGITAL: **IDX PIT FB MDEC** / IDX PIT only (2 cards blank)?
3. FM6: **M.TIM C.TIM MOD PIT** (aliases + paths) / a new "PEG depth" card in place of PIT / MOD PIT only?
4. ACID: **aliases ENV DEC ACC CUT** / leave ENV 1 blank on ACID (303 untouched)?
5. CZ: **aliases DCW W.TIM A.TIM + PIT** / PIT only?
6. WHEEL: **PIT DRV CLICK TOP** / PIT only?
7. PHASE: **DCW (alias E3) PIT DTN SUB** / FLT-path DCW depth as a new knob?
8. Post-141 ENV 1 slots on other targets: **move to ENV 2 when it is free (copy the ADSR)** / always drop with a note?
9. Page name: **ENV 1 MOD** / ENV DEST (kept) / DEPTH?
