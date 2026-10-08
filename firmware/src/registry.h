/* SPDX-License-Identifier: GPL-3.0-only */
/* What this build contains, and the stable IDs of what a build may leave out (docs/BUILDER.md).
 *
 * Every engine, drum kit and sample set has a permanent ID (UID), never reused: today's numbers (FUN7's engine
 * numbers, the kit numbers, the set numbers). Projects, user presets and the drum lanes store UIDs; the runtime
 * keeps slots (ENGINES[] index; the editor's wire still speaks slots, INFO v6 adds the UID of each slot).
 * A build without an item still loads what names it: it plays a fallback (the chain below) and keeps the UID and
 * the item's values, written back on save while the user leaves that track's sound alone (project.c).
 *
 * The switches are literal 0 / 1 flags (tools/configure.py writes build/gen/felucca_config.h from a .config;
 * without one every default below holds: today's build). Leaving an item out of the lists below drops its code,
 * tables, RAM and pool: SLOOP is one translation unit, and every test of an item is a constant (ENG_IS) that
 * folds. With every item present, each mapping below folds to the identity: the build is the one from before. */

#define FCAT_(a, b) a##b
#define FCAT(a, b) FCAT_(a, b)
#define FIF_0(...)
#define FIF_1(...) __VA_ARGS__
#define FIF(f) FCAT(FIF_, f)               /* FIF(flag)(x): x when the flag is 1 (flags are literal 0 / 1) */
#define FNOT_0 1
#define FNOT_1 0
#define FNOT(f) FCAT(FNOT_, f)

/* ---------------------------------------------------------------- engines --- */
#ifndef FELUCCA_ENG_ANALOG
#define FELUCCA_ENG_ANALOG 1               /* ANALOG 2 (eng_analog.c, eng_analog2.c) */
#endif
#ifndef FELUCCA_ENG_DIGITAL
#define FELUCCA_ENG_DIGITAL 1
#endif
#ifndef FELUCCA_ENG_PHASE
#define FELUCCA_ENG_PHASE 1
#endif
#ifndef FELUCCA_ENG_LOFI
#define FELUCCA_ENG_LOFI 1
#endif
#ifndef FELUCCA_ENG_SAMPLE
#define FELUCCA_ENG_SAMPLE 1
#endif
#ifndef FELUCCA_ENG_FORMANT
#define FELUCCA_ENG_FORMANT 1              /* "VOICE" */
#endif
#ifndef FELUCCA_ENG_TRIO
#define FELUCCA_ENG_TRIO 1
#endif
#ifndef FELUCCA_ENG_DRAWBAR
#define FELUCCA_ENG_DRAWBAR 1              /* "WHEEL" */
#endif
#ifndef FELUCCA_ENG_GRAIN
#define FELUCCA_ENG_GRAIN 1
#endif
#ifndef FELUCCA_ENG_FM6
#define FELUCCA_ENG_FM6 1
#endif
#ifndef FELUCCA_ENG_SLICE
#define FELUCCA_ENG_SLICE FELUCCA_SLICE    /* (FELUCCA_SLICE: its first name, core.h) */
#endif
#ifndef FELUCCA_ENG_PHYS
#define FELUCCA_ENG_PHYS 0                 /* PHYS (from Felucca 1.0: eng_phys.c, backports.h) */
#endif
#ifndef FELUCCA_ENG_ACID
#define FELUCCA_ENG_ACID 0                 /* ACID (from X0X: eng_acid.c, backports.h), EXPERIMENTAL */
#endif
#ifndef FELUCCA_ENG_CZ
#define FELUCCA_ENG_CZ 0                   /* CZ (from Melodee 0.11: eng_cz.c, cz_native.c, backports.h) */
#endif

/* X(uid, NAME, fallback uid, the ENG list name): the UID is the engine's number in FUN7 projects and UPB2 user
 * presets. Fallbacks: FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE -> ANALOG, SLICE -> SAMPLE, CZ -> PHASE, the rest -> ANALOG;
 * with ANALOG absent too, the first engine built. Order = ENGINES[] order (the slots). */
#if FELUCCA_ANALOG2
#define ENGINE_LIST(X) X(0, ANALOG, 0, "ANALOG") X(1, DIGITAL, 0, "DIGITAL") X(2, PHASE, 0, "PHASE") \
    X(3, LOFI, 0, "LOFI") X(4, SAMPLE, 0, "SAMPLE") X(5, FORMANT, 0, "VOICE") X(6, TRIO, 0, "TRIO")      \
    X(7, DRAWBAR, 0, "WHEEL") X(8, GRAIN, 4, "GRAIN") X(9, FM6, 1, "FM6") X(10, SLICE, 4, "SLICE")    \
    X(11, PHYS, 0, "PHYS") X(12, ACID, 0, "ACID") X(13, CZ, 2, "CZ")
#define ENG_UID_N 14u
#define ENG_UID_PHYS 11u
#define ENG_UID_ACID 12u
#define ENG_UID_CZ 13u                     /* (fallback: PHASE) */
#define ENG_UID_FM6 9u                     /* (the importers: DX7 and FM6 parts of older projects play FM6) */
#define ENG_UID_SLICE 10u
#define ENG_UID_SUPER 0u                   /* SUPER's presets and parts: ANALOG's swarm */
#else                                      /* (the original ANALOG and SUPER: measurements only, tests/analog2_test.c) */
#define FELUCCA_ENG_SUPER 1
#define ENGINE_LIST(X) X(0, ANALOG, 0, "ANALOG") X(1, DIGITAL, 0, "DIGITAL") X(2, PHASE, 0, "PHASE") \
    X(3, LOFI, 0, "LOFI") X(4, SAMPLE, 0, "SAMPLE") X(5, FORMANT, 0, "VOICE") X(6, TRIO, 0, "TRIO")      \
    X(7, DRAWBAR, 0, "WHEEL") X(8, GRAIN, 4, "GRAIN") X(9, SUPER, 0, "SUPER") X(10, FM6, 1, "FM6")         \
    X(11, SLICE, 4, "SLICE")
#define ENG_UID_N 12u
#define ENG_UID_FM6 10u
#define ENG_UID_SLICE 11u
#define ENG_UID_SUPER 9u
#endif

#define ENG_CNT_(u, N, fb, s) +FELUCCA_ENG_##N
#define NENGINES (0 ENGINE_LIST(ENG_CNT_)) /* engines built */
#if NENGINES < 1
#error "a build needs at least one synth engine"
#endif
#define ENG_HAS(N) (FELUCCA_ENG_##N)
#define ENG_IS(e, N) (FELUCCA_ENG_##N && (e) == &ENG_##N)   /* folds to 0 when the engine is not built */

/* ENG_SLOT_<NAME>: the engine's slot (its ENGINES[] index), 0xFF = not built */
#define ENG_SLOTE_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(ENG_SLOT_##N,)
#define ENG_GONEE_(u, N, fb, s) FIF(FNOT(FELUCCA_ENG_##N))(ENG_SLOT_##N = 0xFF,)
enum { ENGINE_LIST(ENG_SLOTE_) ENG_SLOT_END_ };
enum { ENGINE_LIST(ENG_GONEE_) ENG_GONE_END_ };
#define ENG_ALL (NENGINES == ENG_UID_N)    /* every engine built: every mapping is the identity */
/* the engines built are UIDs 0..NENGINES-1 (those left out come last: slot = UID) */
#define ENG_DENSEE_(u, N, fb, s) &&(ENG_SLOT_##N == (FELUCCA_ENG_##N ? (u) : 0xFF))
#define ENG_DENSE (1 ENGINE_LIST(ENG_DENSEE_))
/* every engine of the default build with a factory preset in ui.c BANK is built (SLICE, ACID and CZ: optional) */
#define ENG_BANK_ALL (NENGINES - FELUCCA_ENG_SLICE - FELUCCA_ENG_ACID - FELUCCA_ENG_CZ == (int)ENG_UID_N - 3)

/* UID <-> slot (the tables fold away with every engine built) */
#define ENG_UIDV_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(u,)
#define ENG_SLOTV_(u, N, fb, s) [u] = ENG_SLOT_##N,
#define ENG_FBV_(u, N, fb, s) [u] = fb,
static const uint8_t ENG_UID[NENGINES] = {ENGINE_LIST(ENG_UIDV_)};
static const uint8_t ENG_SLOT_OF[ENG_UID_N] = {ENGINE_LIST(ENG_SLOTV_)};
static const uint8_t ENG_FALLBACK[ENG_UID_N] = {ENGINE_LIST(ENG_FBV_)};
#define ENG_NAMEV_(u, N, fb, s) [u] = s,
static const char *const ENG_UID_NAME[ENG_UID_N] = {ENGINE_LIST(ENG_NAMEV_)};   /* (the UI names an absent one) */

/* slot -> UID (what projects, user presets and the drum lanes store) */
static uint32_t eng_uid(uint32_t slot) { return ENG_DENSE ? slot : ENG_UID[slot % NENGINES]; }
/* the engine of this UID is built */
static int eng_built(uint32_t uid) { return ENG_DENSE ? uid < NENGINES : uid < ENG_UID_N && ENG_SLOT_OF[uid] != 0xFFu; }
/* UID -> the slot that plays it: its own, else its fallback's (registry.h), else slot 0. With every engine
 * built, as before: any byte modulo NENGINES */
static uint32_t eng_slot(uint32_t uid)
{
    uint32_t n;
    if (ENG_ALL)
        return uid % NENGINES;
    if (ENG_DENSE && uid < NENGINES)
        return uid;
    for (n = 0; n < 4u && uid < ENG_UID_N; n++) {
        if (ENG_SLOT_OF[uid] != 0xFFu)
            return ENG_SLOT_OF[uid];
        uid = ENG_FALLBACK[uid];
    }
    return 0;
}
/* the slot of a UID known to be built (BANK entries, the defaults) */
static uint32_t eng_slot_built(uint32_t uid) { return ENG_DENSE ? uid : ENG_SLOT_OF[uid % ENG_UID_N]; }

/* ------------------------------------------------------------- drum kits --- */
/* The drum track's kit UID is its number (drums.c): 0..4 the sampled kits (the PERC sample set: ACOUSTIC and its
 * four treatments), 5.. the synthesised kits in tools/gen_drumkits.py order (append only). Two sources, each a
 * switch: the drum synth (every synthesised kit together, drum_synth.c) and the sampled kits (one switch per
 * kit; with none, the PERC set is left out of the build). A kit not built plays the other source's first kit.
 * 37 and 38: the X0X kits (below). */
#ifndef FELUCCA_DRUM_SYNTH
#define FELUCCA_DRUM_SYNTH 1               /* the synthesised kits (808, 909, ... 32 of them) */
#endif
#ifndef FELUCCA_KIT_ACOUSTIC
#define FELUCCA_KIT_ACOUSTIC 1
#endif
#ifndef FELUCCA_KIT_DEEP
#define FELUCCA_KIT_DEEP 1
#endif
#ifndef FELUCCA_KIT_TIGHT
#define FELUCCA_KIT_TIGHT 1
#endif
#ifndef FELUCCA_KIT_BRIGHT
#define FELUCCA_KIT_BRIGHT 1
#endif
#ifndef FELUCCA_KIT_DUST
#define FELUCCA_KIT_DUST 1
#endif
#define DRUM_SMASK (FELUCCA_KIT_ACOUSTIC | FELUCCA_KIT_DEEP << 1 | FELUCCA_KIT_TIGHT << 2 | FELUCCA_KIT_BRIGHT << 3 | \
                    FELUCCA_KIT_DUST << 4)  /* the sampled kits built, bit = kit UID */
#ifndef FELUCCA_DRUM_SAMPLED
#define FELUCCA_DRUM_SAMPLED (DRUM_SMASK != 0)
#endif
#if !FELUCCA_DRUM_SAMPLED
#undef DRUM_SMASK
#define DRUM_SMASK 0
#endif
#if !FELUCCA_DRUM_SYNTH && !DRUM_SMASK
#error "the drum track needs a drum source: the drum synth or a sampled kit"
#endif
/* Kit UIDs 37 and 38: X0X's circuit-modelled TR-909 and TR-808 (drum_x0x.c), each a switch of its own, off by
 * default. Every build knows their UIDs and names: a project, a user kit or a lane naming one keeps it, and a
 * build without it plays a stand-in (the synthesised 909 / 808 kit, else the first sampled kit). UIDs 39..46: their
 * style kits (drum_x0x.c X0X_STYLE: the same voices with their own SOUND settings), there with their machine; the
 * same stand-ins. Synthesised kits added after these take UIDs from 47. */
#ifndef FELUCCA_DRUM_X909
#define FELUCCA_DRUM_X909 0                /* X0X's TR-909 (9W9 / ER-99 models, sampled hats and cymbals) */
#endif
#ifndef FELUCCA_X909_CYM
#define FELUCCA_X909_CYM 1                 /* ... with its ride and crash samples: 1 8-bit, 2 6-bit, 0 none (the stand-in's) */
#endif
#ifndef FELUCCA_DRUM_X808
#define FELUCCA_DRUM_X808 0                /* X0X's TR-808 (8W8 models, 16 sounds) */
#endif
#define DRUM_X0X (FELUCCA_DRUM_X909 || FELUCCA_DRUM_X808)
#define DRUM_UID_X909 37u
#define DRUM_UID_X808 38u
#define DRUM_UID_XSTYLE 39u                /* 39..42: X9 TECH, X9 HOUSE, X9 UKG, X9 ACID; 43..46: X8 TRAP, X8 BOOM, */
#define DRUM_NXSTYLE 8u                    /* X8 ELEC, X8 MIAMI */
#define DRUM_UID_XMACH(k) ((k) < DRUM_UID_XSTYLE ? (k) : (k) < DRUM_UID_XSTYLE + 4u ? DRUM_UID_X909 : DRUM_UID_X808)

/* --------------------------------------------------------- build report --- */
/* tools/configure.py defines these in build/gen/felucca_config.h (editor.c BUILD, 43): the profile, the .config's
 * hash and one bit per registry item built (tools/builder/registry.py "bit") */
#ifndef FELUCCA_CFG_NAME
#define FELUCCA_CFG_NAME "default"
#endif
#ifndef FELUCCA_CFG_HASH
#define FELUCCA_CFG_HASH 0u
#endif
#ifndef FELUCCA_CFG_BITS
#define FELUCCA_CFG_BITS {0}
#endif

/* -------------------------------------------------------------------- FX --- */
/* Each effect drops its code, buffers and pages (params.c page_shown) when off; the parameters stay in every
 * project (stored formats do not depend on the build). Sized: FELUCCA_DLY_LEN (fx.c), PUNCH_N, SL_LEN. */
#ifndef FELUCCA_FX_DIST
#define FELUCCA_FX_DIST 1                  /* per-track DIST insert */
#endif
#ifndef FELUCCA_FX_CHORUS
#define FELUCCA_FX_CHORUS 1                /* the chorus send bus */
#endif
#ifndef FELUCCA_FX_DELAY
#define FELUCCA_FX_DELAY 1                 /* the tempo delay send bus */
#endif
#ifndef FELUCCA_FX_REVERB
#define FELUCCA_FX_REVERB 1                /* the reverb send bus (also the drums' REV) */
#endif
#ifndef FELUCCA_REV_POOL
#define FELUCCA_REV_POOL 0                 /* the reverb's four lines (17 KB) in the pool instead of main RAM */
#endif
#ifndef FELUCCA_REV_HALF
#define FELUCCA_REV_HALF 0                 /* the reverb's tank at 22.05 kHz: half its lines' RAM (fx.c) */
#endif
/* the reverb's algorithms (fx.c, reverb_alt.c; SPRING: backports.h): each one built is on FX > REVERB > TYPE (rev_type.c),
 * picked at run time; one built: no TYPE. At least one with the bus (rev_type.c says so). They share one line buffer */
#ifdef FELUCCA_REVERB                      /* (the old one-tank choice, 0 ROOM, 1 PLATE, 2 FDN8, 3 AIRWIN: that tank alone) */
#ifndef FELUCCA_REV_ROOM
#define FELUCCA_REV_ROOM (FELUCCA_REVERB == 0)
#endif
#ifndef FELUCCA_REV_PLATE
#define FELUCCA_REV_PLATE (FELUCCA_REVERB == 1)
#endif
#ifndef FELUCCA_REV_FDN8
#define FELUCCA_REV_FDN8 (FELUCCA_REVERB == 2)
#endif
#ifndef FELUCCA_REV_AIRWIN
#define FELUCCA_REV_AIRWIN (FELUCCA_REVERB == 3)
#endif
#endif
#ifndef FELUCCA_REV_ROOM
#define FELUCCA_REV_ROOM 1                 /* ROOM: four lines at 44.1 kHz (fx.c) */
#endif
#ifndef FELUCCA_REV_PLATE
#define FELUCCA_REV_PLATE 0                /* PLATE: Dattorro's plate at 22.05 kHz (reverb_alt.c) */
#endif
#ifndef FELUCCA_REV_FDN8
#define FELUCCA_REV_FDN8 0                 /* FDN8: eight modulated lines at 22.05 kHz (reverb_alt.c) */
#endif
#ifndef FELUCCA_REV_AIRWIN
#define FELUCCA_REV_AIRWIN 0               /* AIRWIN: Airwindows' VerbTiny at 22.05 kHz (reverb_airwin.c) */
#endif
#ifndef FELUCCA_FX_SLICER
#define FELUCCA_FX_SLICER 1                /* per-track stutter / gate insert */
#endif
#ifndef FELUCCA_FX_PUNCH
#define FELUCCA_FX_PUNCH 1                 /* the 16 punch-in FX on the mix (FX + a white key) */
#endif
#ifndef FELUCCA_FX_DJF
#define FELUCCA_FX_DJF 1                   /* MASTER > FILT: the DJ filter */
#endif
#ifndef FELUCCA_FX_DUST
#define FELUCCA_FX_DUST 1                  /* MASTER > DUST: vinyl / lo-fi */
#endif
#ifndef FELUCCA_FX_DUCK
#define FELUCCA_FX_DUCK 1                  /* MASTER > DUCK: the kick ducks the parts */
#endif
#ifndef FELUCCA_MASTER_COMP
#define FELUCCA_MASTER_COMP 1              /* GLO > COMP / LIMIT: the master bus compressor and brickwall limiter */
#endif
#ifndef FELUCCA_TRK_FILT
#define FELUCCA_TRK_FILT 0                 /* SLOOP 2.4's track FILTER (P_TFLT): FX > FILTER, FX + KNOB 4; LP <- off -> HP
                                            * on each part and on the drum bus with all its sends (fx.c tflt_*) */
#endif
#ifndef FELUCCA_CHORDPLUS
#define FELUCCA_CHORDPLUS 0                /* SLOOP 2.4's CHORD+: in chord mode the black keys change the chord; SCL 2
                                            * STRUM and VLEAD (seq.c chord_play_notes, voice.c strum_*) */
#endif

/* -------------------------------------------------------------- features --- */
#ifndef FELUCCA_MIDI_CLOCK
#define FELUCCA_MIDI_CLOCK 1               /* follow a MIDI clock (GLO > SYSTEM SYNC: AUTO TRS > USB > INT) */
#endif
#ifndef FELUCCA_MIDI_EXPR
#define FELUCCA_MIDI_EXPR 1                /* MIDI bend, mod wheel, breath, foot, aftertouch, sustain, RPN 0
                                            * (0: notes and the panics CC 120..123 only) */
#endif
/* (retired 2026-10, always built: a MIDI channel for each track, MIDI OUT = SEQ and MIDI IN = CLOCK are no build
 *  choices any more; the user's on / off is in the HOME menu, SYSTEM 1/3 and 2/3. Their #if guards stay true.) */
#if defined(FELUCCA_MIDI_CH) && !FELUCCA_MIDI_CH
#error "FELUCCA_MIDI_CH is retired: each track's MIDI channel (seq_midi.c, HOME menu) is always built"
#endif
#undef FELUCCA_MIDI_CH
#define FELUCCA_MIDI_CH 1                  /* a MIDI channel for each track, 1..16 or OFF, in and out, saved in the
                                            * project (SLOOP 2.4 phase 3: seq_midi.c; HOME menu > SYSTEM) */
#if defined(FELUCCA_MIDI_OUT) && !FELUCCA_MIDI_OUT
#error "FELUCCA_MIDI_OUT is retired: MIDI OUT = SEQ (seq_midi.c, HOME menu > MIDI OUT) is always built"
#endif
#undef FELUCCA_MIDI_OUT
#define FELUCCA_MIDI_OUT 1                 /* HOME menu > MIDI OUT = SEQ: the sequencer, the arp and the rolls go to
                                            * MIDI OUT too (SLOOP 2.4; seq_midi.c) */
#if defined(FELUCCA_MIDI_INCLK) && !FELUCCA_MIDI_INCLK
#error "FELUCCA_MIDI_INCLK is retired: MIDI IN = CLOCK (HOME menu > MIDI IN) is always built"
#endif
#undef FELUCCA_MIDI_INCLK
#define FELUCCA_MIDI_INCLK 1               /* HOME menu > MIDI IN = CLOCK: MIDI in takes the clock only, no notes
                                            * (SLOOP 2.4) */
#ifndef FELUCCA_OV_ARP
#define FELUCCA_OV_ARP 1                  /* the ARP graph and the ARP family in VIEW ALL */
#endif
#ifndef FELUCCA_FM6_ALL
#define FELUCCA_FM6_ALL 1                  /* FM6's operator editor in VIEW ALL: a group's pages as rows */
#endif
#ifndef FELUCCA_FM6_ALGO
#define FELUCCA_FM6_ALGO 1                 /* ENV held on an FM6 track: the algorithm full screen */
#endif
#ifndef FELUCCA_SECTIONS
#define FELUCCA_SECTIONS 16                /* song sections: 4 (A..D, the project slots in RAM and flash, as before), 8
                                            * or 16 (A..P in banks of 4, compressed in one shared log: sec_log.c) */
#endif
#if FELUCCA_SECTIONS != 4 && FELUCCA_SECTIONS != 8 && FELUCCA_SECTIONS != 16
#error "FELUCCA_SECTIONS: 4, 8 or 16"
#endif
#define SEC_LOGGED (FELUCCA_SECTIONS > 4)  /* the sections live in the log, not in RAM slots */
#ifndef FELUCCA_SNAPSHOTS
#define FELUCCA_SNAPSHOTS 4                /* whole-state snapshot slots (the work, every section, the song): 0, 2, 4 or
                                            * 8, plus BEFORE LOAD (snapshots.c, docs/SNAPSHOTS.md) */
#endif
#if FELUCCA_SNAPSHOTS != 0 && FELUCCA_SNAPSHOTS != 2 && FELUCCA_SNAPSHOTS != 4 && FELUCCA_SNAPSHOTS != 8
#error "FELUCCA_SNAPSHOTS: 0, 2, 4 or 8"
#endif
/* their flash: slots + 4 sectors of 4 KiB at USR3's end, below the banks (0xD8000): USR3 holds that much less */
#define SN_SECTORS (FELUCCA_SNAPSHOTS ? (uint32_t)FELUCCA_SNAPSHOTS + 4u : 0u)
#ifndef FELUCCA_BACKUP
#define FELUCCA_BACKUP 1                   /* the web editor's backup / restore of everything stored (ed_backup.c) */
#endif
#ifndef FELUCCA_OVERVIEW
#define FELUCCA_OVERVIEW 1                 /* VIEW ALL: a page family at once (ui_overview.c, GLO > SYSTEM VIEW) */
#endif
#ifndef FELUCCA_CPU_GUARD
#define FELUCCA_CPU_GUARD 0                /* the predictive CPU guard: eases back quality, then UNISON, then notes
                                            * under overload (cpuguard.c, after Flowstate; docs/CPU-GUARD.md) */
#endif
#ifndef FELUCCA_MISSING_WARN
#define FELUCCA_MISSING_WARN 1             /* "MISSING: PHYS T2, KIT 909" when a load uses what this build leaves
                                            * out; SAVE > TOOLS > MISS lists it again (miss.c) */
#endif
#ifndef FELUCCA_CHORD_NAMES
#define FELUCCA_CHORD_NAMES 1              /* the STEP page names a step's chord: Am, Fmaj7, Bm7b5 (ui.c chord_name, from
                                            * isod89/sloop-fm1 PR #45) */
#endif
#ifndef FELUCCA_PARAM_HELP
#define FELUCCA_PARAM_HELP 0               /* the knob's value in plain words on the top bar ("Filter cutoff"), from
                                            * tools/param_help.json (param_help.c) */
#endif
#ifndef FELUCCA_DRUM_STEP
#define FELUCCA_DRUM_STEP 0                /* the drum track's SEQ layer as a TR step sequencer: one sound's 16 steps over
                                            * an overview of the 16 sounds, SEQ + EDIT picks the sound on the white keys,
                                            * the page follows the playhead (ui_drumstep.c; PR #45 of isod89/sloop-fm1) */
#endif
#ifndef FELUCCA_MACROS
#define FELUCCA_MACROS 0                   /* GLO > MACRO: COLOR MOTION SPACE ENERGY, after Flowstate (macro.c) */
#endif
#if !FELUCCA_MACROS
#undef FELUCCA_ENERGY
#define FELUCCA_ENERGY 0                   /* (an option of MACROS) */
#endif
#ifndef FELUCCA_ENERGY
#define FELUCCA_ENERGY 0                   /* ENERGY's bands thin / thicken the drum track's steps (macro.c) */
#endif

/* ------------------------------------------------------------ FM6 options --- */
/* Options of FM6 (ignored when FELUCCA_ENG_FM6 is 0). The modes: at least one; a voice asking for a mode left
 * out plays MARK I (else the first built) and keeps its ENGINE setting (eng_fm6.c fm6_mode) */
#ifndef FELUCCA_FM6_MARK1
#define FELUCCA_FM6_MARK1 1                /* ENGINE MARK I: the DX7's log-sine / exp resolution */
#endif
#ifndef FELUCCA_FM6_MODERN
#define FELUCCA_FM6_MODERN 1               /* ENGINE MODERN: MSFA 24-bit */
#endif
#ifndef FELUCCA_FM6_OPL
#define FELUCCA_FM6_OPL 1                  /* ENGINE OPL resolution */
#endif
#if !FELUCCA_FM6_MARK1 && !FELUCCA_FM6_MODERN && !FELUCCA_FM6_OPL
#error "FM6 needs at least one ENGINE mode (FELUCCA_FM6_MARK1, _MODERN, _OPL)"
#endif
#ifndef FELUCCA_FM6_MKI_FLASH
#define FELUCCA_FM6_MKI_FLASH 0            /* MARK I's log-sine and exponent tables as const data in flash (generated:
                                            * tools/gen_tables.py), not RAM tables built at boot: 4 KB of RAM less;
                                            * the emulator has no XIP cache nor flash wait states: the hardware's
                                            * cost is not measured */
#endif
#ifndef FELUCCA_FM6_SYSEX
#define FELUCCA_FM6_SYSEX 1                /* DX7 SysEx in (voice, bank, parameters) and SEND; the web editor's FM6
                                            * tab needs it */
#endif
#ifndef FELUCCA_FM6_VOICES
#define FELUCCA_FM6_VOICES 1               /* the 16 factory voices R01..R16 (0: they play INIT VOICE) */
#endif
#ifndef FELUCCA_FM6_STORE
#define FELUCCA_FM6_STORE 1                /* STORE into the user bank U01..U32 (a USR slot) */
#endif
