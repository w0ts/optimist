# ENV 1 as the engine's own envelope (ENV 1 MOD)

Ruling (2026-10-16): **ENV 1 is not parametric.** No DEST / AMT pickers: it is the engine's own envelope with four
fixed destinations per engine, each one depth knob (-64..63). **ENV 2, LFO 1 and LFO 2 keep two free slots** (DEST A + AMT A,
DEST B + AMT B, the shared `LT_*` list in `core/core.h`). Code: `firmware/src/core/env1_cards.h` (the card table),
`core/voice.c` (`env_plan`, `env_vmove`, `env_pre`, `env_post`), `core/env_store.h` (ENV 2 DEST, the fold of old files),
`core/params.c` (`e1_card_desc`), `tests/env1_test.c`.

## ENV 1

- **The amplitude is always wired** (`amp = env`, after the engine's own curve); there is no AMP knob. Engines whose own
  envelope ends the voice (CZ, FM6, ACID, LOFI STEP) keep ending it as they do.
- Each card is a **path** (FLT, SHP: `P_ED_FLT`, `P_ED_SHP`, `env x d >> 7` on the plain RAM path for every engine, card or
  not, so old sounds play as before; PIT: `P_E1PIT`, +-31.5 semitones, slow path), a **move** (ENV moves the engine's EDIT
  value n per voice, depth in `P_E1X` / `P_E1Y` / `P_E1Z`), a **part move** (a value read once a block for the part, moved
  from its newest voice: WHEEL's DRV CLICK TOP, FM6's LPF) or an **alias** (the engine's own EDIT value, shown a second
  time, nothing stored). The ids are the four of the old ENV DEST slots; no id moved. All are lockable and recordable as
  motion (tail ids `P_ENG_END + 15..18`, `seq/auto.c`).
- **E1 AMT** (`LT_N1AMT`): an LFO on it scales ENV 1's FLT and SHP depths for the block (x 0..2); ENV 2 on it scales FLT SHP PIT
  per voice; the moves are not scaled. Cost: no RAMTEXT, no RAM.

| Engine | Cards (the EDIT value's own names) | ENV 2 default DEST A |
|---|---|---|
| ANALOG, SUPER | FLT, PIT, SHP, RES (move) | FLT |
| DIGITAL | IDX (FLT path), PIT, FB, MDEC | FLT |
| PHASE | ENV (alias), PIT, DTN, SUB | FLT |
| LOFI / SAMPLE | TONE / CUT (FLT), PIT, DUTY / BITS, SWP / DRV | FLT |
| VOICE | VOWL (FLT), PIT, BUZZ (SHP), Q | FLT |
| TRIO, JUNO | FLT, PIT, PW (SHP), RES | FLT |
| WHEEL | PIT, DRV, CLICK, TOP (part moves) | TOP |
| GRAIN | TONE (FLT), PIT, POS (SHP), DENS | FLT |
| SLICE | TONE (FLT), PIT, DCAY, PTCH | FLT |
| FM6 | PIT, LPF (part move) | PIT |
| PHYS | BRIT (FLT), PIT, DAMP, EXC | FLT |
| ACID | ENV, DEC, ACC, SLD (aliases of the 303's own) | FLT (its CUT) |
| CZ | DCW, W.TIM, A.TIM (aliases), PIT | E2 (DCW) |
| NOISE | FREQ (FLT), PIT, COLR (SHP), RES | FLT |
| DRONE | TONE (FLT), PIT, COUPLE (SHP), CHAR | FLT |
| PLAITS | TONE (FLT), PIT, TIMB (SHP), MORPH | FLT |

Drum lanes have their own ENV page (CLICK DECAY BEND LEVEL). SLICE's fourth card is PTCH, not START (START is read once at
the note-on, so ENV 1 did nothing there).

## ENV 2 and the default filter

ENV 2 (`P_N2DA..P_N2AB`, ADSR `P_A2FATK..`) exists on every engine in builds with `ANALOG2` (the default). Its DEST A is
**FLT at amount 0** on a new sound, INIT and every factory preset that does not use ENV 2 (`env2_def_dest`); DEST B is
OFF. Engines without a filter (CZ, FM6, WHEEL, PHASE, DIGITAL) get their nearest control; no filter was added. ACID has its
own filter (FLT drives its CUT, RES its RESO) and, like CZ, its own amplitude envelope that the shared AMP path does not drive.

## Older files

Between 2026-10-14 and 16 ENV 1 had two free slots (modulation record tag 0xD2, user presets `UP_VER 3`). They are read and
**folded** (`env1_fold`): AMP is dropped (an AMT other than 127 counts as lost); FLT / SHP / PIT slots become the depths at
AMT / 2; any other target moves to ENV 2 when both its slots are unused (ENV's ATK DEC SUS REL copied into ATK2 DEC2 SUS2
REL2), else it is lost and shows in TOOLS > MISS as `ENV DEST T1`. Before 2026-10-14 the depths were already FLT / SHP
words and load unchanged. User presets are written as `UP_VER 4`.

## Limits

ENV 1 amplitude below full cannot be expressed. ACID / CZ / FM6 cards are aliases: turning them changes the EDIT value too.
Old DIGITAL / PHASE FLT and SHP depths keep playing as stored (SHP acts with no card on DIGITAL). Golden renders must not
change for factory presets: any golden diff is a bug.
