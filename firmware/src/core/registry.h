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
#define FELUCCA_ENG_ACID 0                 /* ACID (from X0X: eng_acid.c, backports.h) */
#endif
#ifndef FELUCCA_ENG_CZ
#define FELUCCA_ENG_CZ 0                   /* CZ (from Melodee 0.11: eng_cz.c, cz_native.c, backports.h) */
#endif
#ifndef FELUCCA_ENG_JUNO
#define FELUCCA_ENG_JUNO 0                 /* JUNO (the Juno-106 voice of fm-106 / KR-106: eng_juno.c, backports.h) */
#endif                                     /* (UID 14 is JUNO's; a build without its engine files leaves the slot empty) */
#ifndef FELUCCA_ENG_NOISE
#define FELUCCA_ENG_NOISE 0                /* NOISE (Felucca-Engines eng_noise.c, MIT: engines/noise/eng_noise.c, backports.h) */
#endif
#ifndef FELUCCA_ENG_DRONE
#define FELUCCA_ENG_DRONE 0                /* DRONE (drones, ambient, binaural beats: engines/drone/eng_drone.c) */
#endif
#ifndef FELUCCA_ENG_PLAITS
#define FELUCCA_ENG_PLAITS 0               /* PLAITS (Mutable Instruments Plaits' models, MIT: engines/plaits/eng_plaits.c) */
#endif

/* X(uid, NAME, fallback uid, the ENG list name): the UID is the engine's number in FUN7 projects and UPB2 user
 * presets. Fallbacks: FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE -> ANALOG, SLICE -> SAMPLE, CZ -> PHASE, the rest (JUNO, NOISE, DRONE, PLAITS too) -> ANALOG;
 * with ANALOG absent too, the first engine built. Order = ENGINES[] order (the slots). */
#if FELUCCA_ANALOG2
#define ENGINE_LIST(X) X(0, ANALOG, 0, "ANALOG") X(1, DIGITAL, 0, "DIGITAL") X(2, PHASE, 0, "PHASE") \
    X(3, LOFI, 0, "LOFI") X(4, SAMPLE, 0, "SAMPLE") X(5, FORMANT, 0, "VOICE") X(6, TRIO, 0, "TRIO")      \
    X(7, DRAWBAR, 0, "WHEEL") X(8, GRAIN, 4, "GRAIN") X(9, FM6, 1, "FM6") X(10, SLICE, 4, "SLICE")    \
    X(11, PHYS, 0, "PHYS") X(12, ACID, 0, "ACID") X(13, CZ, 2, "CZ") X(14, JUNO, 0, "JUNO") \
    X(15, NOISE, 0, "NOISE") X(16, DRONE, 0, "DRONE") X(17, PLAITS, 0, "PLAITS")
#define ENG_UID_N 18u
#define ENG_UID_PHYS 11u
#define ENG_UID_ACID 12u
#define ENG_UID_CZ 13u                     /* (fallback: PHASE) */
#define ENG_UID_JUNO 14u                   /* (fallback: ANALOG) */
#define ENG_UID_NOISE 15u                  /* (fallback: ANALOG) */
#define ENG_UID_DRONE 16u                  /* (fallback: ANALOG) */
#define ENG_UID_PLAITS 17u                 /* (fallback: ANALOG) */
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
/* every engine of the default build with a factory preset in ui.c BANK is built (SLICE, ACID, CZ, JUNO, NOISE, DRONE, PLAITS: optional) */
#define ENG_BANK_ALL (NENGINES - FELUCCA_ENG_SLICE - FELUCCA_ENG_ACID - FELUCCA_ENG_CZ - FELUCCA_ENG_JUNO - FELUCCA_ENG_NOISE - \
                      FELUCCA_ENG_DRONE - FELUCCA_ENG_PLAITS == (int)ENG_UID_N - 7)

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
 * four treatments), 5.. the synthesised kits in tools/gen_drumkits.py order (append only). The sources, each a
 * switch: the drum synth's 32 kits (5..36 together, drum_synth.c), its KAWA and PLAITS kits (47, 48: below, each
 * on its own) and the sampled kits (one switch per kit; with none, the PERC set is left out of the build). A kit
 * not built plays the other source's first kit (drums.c DRUM_OTHER).
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
/* Kit UIDs 37 and 38: X0X's circuit-modelled TR-909 and TR-808 (drum_x0x.c), each a switch of its own, off by
 * default. Every build knows their UIDs and names: a project, a user kit or a lane naming one keeps it, and a
 * build without it plays a stand-in (the synthesised 909 / 808 kit, else the first sampled kit, else the other X0X
 * kit). An X0X kit alone is a drum source (the user, 2026-10-20): the sounds it lacks are silent then. UIDs 39..46: their
 * style kits (drum_x0x.c X0X_STYLE: the same voices with their own SOUND settings), there with their machine; the
 * same stand-ins. Synthesised kits added after these take UIDs from 47. */
#ifndef FELUCCA_DRUM_X909
#define FELUCCA_DRUM_X909 0                /* X0X's TR-909 (9W9 / ER-99 models, sampled hats and cymbals) */
#endif
#ifndef FELUCCA_X909_HAT
#define FELUCCA_X909_HAT 1                 /* the 909's hi-hat samples: 1 8-bit, 2 6-bit (x0x/drum909.c) */
#endif
#ifndef FELUCCA_X0X_TANH
#define FELUCCA_X0X_TANH 1                 /* the 909's tanh table: 1 1024 points, 2 256 points (x0x/drum909_dsp.h) */
#endif
#ifndef FELUCCA_X909_CYM
#define FELUCCA_X909_CYM 1                 /* ... with its ride and crash samples: 1 8-bit, 2 6-bit, 0 none (the stand-in's) */
#endif
#ifndef FELUCCA_DRUM_X808
#define FELUCCA_DRUM_X808 0                /* X0X's TR-808 (8W8 models, 16 sounds) */
#endif
#define DRUM_X0X (FELUCCA_DRUM_X909 || FELUCCA_DRUM_X808)
#ifndef FELUCCA_DS_KAWA
#define FELUCCA_DS_KAWA 1                  /* kit KAWA (UID 47) and the drum synth's KAWA options (drum_synth.c; Kawa Tracker) */
#endif
#ifndef FELUCCA_DS_PLAITS
#define FELUCCA_DS_PLAITS 1                /* kit PLAITS (UID 48): Plaits' synthetic kick and snare (drum_plaits.c; Hortator) */
#endif
/* Three drum synths, each a switch of its own (the user, 2026-10-22): the 32 kits (FELUCCA_DRUM_SYNTH), KAWA, PLAITS.
 * The synth's voice they share (drum_synth.c, the sound table DS_SNDS) is built with any of them */
#define DRUM_DS (FELUCCA_DRUM_SYNTH || FELUCCA_DS_KAWA || FELUCCA_DS_PLAITS)
#if !DRUM_DS && !DRUM_SMASK && !DRUM_X0X
#error "the drum track needs a drum source: a drum synth (the 32 synthesised kits, KAWA, PLAITS, X0X 909, X0X 808) or a sampled kit"
#endif
#define DRUM_UID_X909 37u
#define DRUM_UID_X808 38u
#define DRUM_UID_XSTYLE 39u                /* 39..42: X9 TECH, X9 HOUSE, X9 UKG, X9 ACID; 43..46: X8 TRAP, X8 BOOM, */
#define DRUM_NXSTYLE 8u                    /* X8 ELEC, X8 MIAMI */
/* Kit UIDs 47..: synthesised kits added after the X0X ones (tools/gen_drumkits.py XKITS: 47 KAWA, 48 PLAITS), each a builder
 * item of its own (FELUCCA_DS_KAWA, FELUCCA_DS_PLAITS above: drum synths, independent of the 32 kits); a build without
 * one plays its stand-in (the 808 for KAWA, the 909 for PLAITS: gen_drumkits.py; without the 32 kits, the first kit built) */
#define DRUM_UID_XKIT 47u
#define DRUM_UID_XMACH(k) ((k) < DRUM_UID_XSTYLE ? (k) : (k) < DRUM_UID_XSTYLE + 4u ? DRUM_UID_X909 : \
                           (k) < DRUM_UID_XKIT ? DRUM_UID_X808 : (k))   /* (47..: no machine's) */
#define DRUM_IS_X0X(k) ((k) >= DRUM_UID_X909 && (k) < DRUM_UID_XKIT)

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
/* more insert types for the tracks' insert slots (fx/insfx/insfx.c; fx_slots.c S1 S2), each a builder item: FOLD FUZZ
 * PHSR FLNG after Felucca 1.5.6's track INSERT, FRMT WIDE after Melodee 1.3.1's inserts */
#ifndef FELUCCA_INS_FOLD
#define FELUCCA_INS_FOLD 0                 /* FOLD: a wavefolder, drive relative to the track's peak */
#endif
#ifndef FELUCCA_INS_FUZZ
#define FELUCCA_INS_FUZZ 0                 /* FUZZ: a hard top, a soft halved bottom */
#endif
#ifndef FELUCCA_INS_PHASER
#define FELUCCA_INS_PHASER 0               /* PHSR: four swept all-pass stages with feedback */
#endif
#ifndef FELUCCA_INS_FLANGER
#define FELUCCA_INS_FLANGER 0              /* FLNG: a swept 0.25 .. 5 ms line with feedback (5 x 528 B of pool: parts, bus L R) */
#endif
#ifndef FELUCCA_INS_FORMANT
#define FELUCCA_INS_FORMANT 0              /* FRMT: three vowel formants, A E I O U along the amount */
#endif
#ifndef FELUCCA_INS_WIDTH
#define FELUCCA_INS_WIDTH 0                /* WIDE: one side through swept all-passes (a mono part becomes stereo) */
#endif
/* the shapers (2026-10-18, fx/insfx/insfx.c ifx_drive, ifx_mod: FOLD's state and drive, a builder item each) */
#ifndef FELUCCA_INS_SFOLD
#define FELUCCA_INS_SFOLD 0                /* SFLD: a sine wavefolder (round folds, Felucca's PHASE FOLD shape) */
#endif
#ifndef FELUCCA_INS_RECT
#define FELUCCA_INS_RECT 0                 /* RECT: full-wave rectified, an octave up (an Octavia fuzz) */
#endif
#ifndef FELUCCA_INS_HARM
#define FELUCCA_INS_HARM 0                 /* HARM: v + v^2/2 + v^3/4 after a soft clip: the octave and the twelfth */
#endif
#ifndef FELUCCA_INS_RING
#define FELUCCA_INS_RING 0                 /* RING: a ring modulator, a sine of 26 Hz .. 2.6 kHz along the amount */
#endif
#ifndef FELUCCA_INS_SUBOCT
#define FELUCCA_INS_SUBOCT 0               /* SUB: an octave down (a flip-flop on the zero crossings), monophonic lines */
#endif
#ifndef FELUCCA_INS_SLEW
#define FELUCCA_INS_SLEW 0                 /* SLEW: a slew-rate limit relative to the peak (a slow op-amp's crunch) */
#endif
/* TAPE, SPIT (2026-10-19, fx/insfx/tapespit.c, after Melodee 1.3.0's FX_TAPE FX_SPIT: a builder item each) */
#ifndef FELUCCA_INS_TAPE
#define FELUCCA_INS_TAPE 0                 /* TAPE: a wandering tape tone, its mids moved and inverted, the highs rolled off */
#endif
#ifndef FELUCCA_INS_SPIT
#define FELUCCA_INS_SPIT 0                 /* SPIT: a resonant band jumping at random, driven into a soft clip */
#endif
/* the insert copies of the "once" effects (fx/insfx/insfx.c, fxi.c: 2026-10-13), each a builder count: how many tracks
 * may hold the type at once (0: not built, up to NTRK). Their lines are in the pool, one per instance; fxi.c hands an
 * instance to a track holding the type (the main loop), a track left without one is bypassed */
#ifndef FELUCCA_INS_CHORUS
#define FELUCCA_INS_CHORUS 0               /* CHRS: a two-tap chorus, 2 KB of pool an instance */
#endif
#ifndef FELUCCA_INS_DELAY
#define FELUCCA_INS_DELAY 0                /* ECHO: a 75 ms slapback with feedback, 8 KB of pool an instance */
#endif
#ifndef FELUCCA_INS_JCHORUS
#define FELUCCA_INS_JCHORUS 0              /* JUNO CHORUS: the Juno-106's chorus (fx/junocho), 2 KB of pool an instance */
#endif
#ifndef FELUCCA_INS_XDLY
#define FELUCCA_INS_XDLY 0                 /* XDLY: two delay lines modulating each other's time (after the LYRA-8's), 2 lines of
                                            * FELUCCA_XDLY_LEN samples of pool an instance (fx/insfx/xdly.c) */
#endif
#ifndef FELUCCA_XDLY_LEN
#define FELUCCA_XDLY_LEN 16384             /* XDLY's line, samples (a power of two): 16384 0.37 s (64 KB an instance), 8192 */
#endif
#ifndef FELUCCA_DLY_ARENA
#define FELUCCA_DLY_ARENA 0                /* the delay arena (fxi_kern.c FXA_*): KB of pool for every delay line, the delay
                                            * send's included, a multiple of 8; 0: the send's line and room for every ECHO
                                            * and XDLY instance of the counts (each its own, as before the arena) */
#endif
#ifndef FELUCCA_RET_FB
#define FELUCCA_RET_FB 1                   /* the returns' feedback sends B -> A, C -> A, C -> B (fx_slots.c rs_amt, fx.c rs_fbv) */
#endif
#ifndef FELUCCA_INS_GATER
#define FELUCCA_INS_GATER 0                /* GATER: a tempo-synced 16-step volume gate (fx/insfx/gater.c), any number */
#endif
#ifndef FELUCCA_CHORDPLUS
#define FELUCCA_CHORDPLUS 0               /* SLOOP 2.4's CHORD+: in chord mode the black keys change the chord; SCL 2
                                            * STRUM and VLEAD (seq.c chord_play_notes, voice.c strum_*) */
#endif

/* -------------------------------------------------------------- features --- */
#ifndef FELUCCA_MIDI_CLOCK
#define FELUCCA_MIDI_CLOCK 1               /* MIDI clock in + out (the builder's one item): follow a MIDI clock (GLO > SYSTEM SYNC: AUTO TRS > USB > INT) */
#endif
#ifndef FELUCCA_MIDI_CLKOUT
#define FELUCCA_MIDI_CLKOUT FELUCCA_MIDI_CLOCK  /* send MIDI clock (HOME menu CLOCK OUT: F8 24 PPQN, FA / FB + F2 / FC, on USB,
                                            * timed as the audio leaves; io/midi/clock_out.c). Not a builder item of its own:
                                            * the one item MIDI_CLOCK is clock in + out; tests may still set it alone */
#endif
#ifndef FELUCCA_TFLT_RESO
#define FELUCCA_TFLT_RESO 1                /* HOME > AUDIO > FILT RESO (SLOOP 2.8): the track FILTER's resonance, 0 clean .. 10 (fx.c
                                            * tflt_reso; core.h G_TFRES). Only with the track FILTER (TRK_FILT) */
#endif
#ifndef FELUCCA_DS_NTUNE
#define FELUCCA_DS_NTUNE 1                 /* a synthesised drum with no tone (hats, shaker, clap): its TUNE (drum EDIT) moves its
                                            * filter and noise high-pass too (SLOOP 2.8; drums/drum_edit.c de_synth) */
#endif
#ifndef FELUCCA_PHASE_FOLD
#define FELUCCA_PHASE_FOLD 1               /* PHASE's E7 FOLD: a sine wavefolder (Felucca-Engines 04ffe90, MIT; engines/phase/eng_phase.c) */
#endif
#ifndef FELUCCA_VOICE_OWN
#define FELUCCA_VOICE_OWN 1                /* voices: at the shared budget, a part restarts its own released voice before
                                            * fading another part's (JIANT 0.9.9 cpu3; core/voice.c voice_alloc) */
#endif
#ifndef FELUCCA_REC_PLAIN
#define FELUCCA_REC_PLAIN 0                /* seq.c rec_poly: recording keeps every note played whatever the voice mode (MONO, LEGATO,
                                            * UNISON no longer cut a step to one note); an ACID part records as before (0: as before) */
#endif
#ifndef FELUCCA_MIDI_EXPR
#define FELUCCA_MIDI_EXPR 1                /* MIDI bend, mod wheel, breath, foot, aftertouch, sustain, RPN 0
                                            * (0: notes and the panics CC 120..123 only) */
#endif
/* (retired 2026-10, always built: a MIDI channel for each track and MIDI IN = CLOCK are no build choices any more; the
 *  user's on / off is in the HOME menu, SYSTEM 1/3 and 2/3. Their #if guards stay true.) */
#if defined(FELUCCA_MIDI_CH) && !FELUCCA_MIDI_CH
#error "FELUCCA_MIDI_CH is retired: each track's MIDI channel (seq_midi.c, HOME menu) is always built"
#endif
#undef FELUCCA_MIDI_CH
#define FELUCCA_MIDI_CH 1                  /* a MIDI channel for each track, 1..16 or OFF, in and out, saved in the
                                            * project (SLOOP 2.4 phase 3: seq_midi.c; HOME menu > SYSTEM) */
#ifndef FELUCCA_MIDI_OUT
#define FELUCCA_MIDI_OUT 1                 /* HOME menu > MIDI OUT = SEQ: the sequencer, the arp and the rolls go to
                                            * MIDI OUT too (SLOOP 2.4; seq_midi.c). A builder item again (MIDI_NOTEOUT,
                                            * 2026-10-11; bit 196 stays retired): 0 = the menu row goes, the panel's keys
                                            * still go out */
#endif
/* (retired 2026-10, always built: the CPU sleeps (fm1_idle) between main-loop polls; no build choice any more) */
#if defined(FELUCCA_IDLE) && !FELUCCA_IDLE
#error "FELUCCA_IDLE is retired: the main loop always waits for an interrupt (system/main.c) between UI frames"
#endif
#undef FELUCCA_IDLE
#define FELUCCA_IDLE 1                     /* main loop: wait for an interrupt (idle) between UI frames */
#if defined(FELUCCA_MIDI_INCLK) && !FELUCCA_MIDI_INCLK
#error "FELUCCA_MIDI_INCLK is retired: MIDI IN = CLOCK (HOME menu > MIDI IN) is always built"
#endif
#undef FELUCCA_MIDI_INCLK
#define FELUCCA_MIDI_INCLK 1               /* HOME menu > MIDI IN = CLOCK: MIDI in takes the clock only, no notes
                                            * (SLOOP 2.4) */
#ifndef FELUCCA_SECTIONS
#define FELUCCA_SECTIONS 16                /* song sections: 4 (A..D, the project slots in RAM and flash, as before), 8
                                            * or 16 (A..P in banks of 4, compressed in one shared log: sec_log.c) */
#endif
#if FELUCCA_SECTIONS != 4 && FELUCCA_SECTIONS != 8 && FELUCCA_SECTIONS != 16
#error "FELUCCA_SECTIONS: 4, 8 or 16"
#endif
#define SEC_LOGGED (FELUCCA_SECTIONS > 4)  /* the sections live in the log, not in RAM slots */
#ifndef FELUCCA_PATTERNS
#define FELUCCA_PATTERNS 0                 /* per-track patterns, 16 a track, and scenes (pat.c, docs/PATTERNS-DESIGN.md);
                                            * off: sections as before (a scene a PATTERNS build stored plays flattened) */
#endif
#if FELUCCA_PATTERNS && !SEC_LOGGED
#error "FELUCCA_PATTERNS needs FELUCCA_SECTIONS 8 or 16 (the log, the stage)"
#endif
#ifndef FELUCCA_SNAPSHOTS
#define FELUCCA_SNAPSHOTS 4                /* whole-state snapshot slots (the work, every section, the song): 0, 2, 4 or
                                            * 8, plus BEFORE LOAD (snapshots.c, docs/SNAPSHOTS.md) */
#endif
#if FELUCCA_SNAPSHOTS != 0 && FELUCCA_SNAPSHOTS != 2 && FELUCCA_SNAPSHOTS != 4 && FELUCCA_SNAPSHOTS != 8
#error "FELUCCA_SNAPSHOTS: 0, 2, 4 or 8"
#endif
/* their flash: slots + 4 sectors of 4 KiB at USR3's end, below the banks (0xD8000): USR3 holds that much less */
#define SN_SECTORS (FELUCCA_SNAPSHOTS ? (uint32_t)FELUCCA_SNAPSHOTS + 4u : 0u)
#ifndef FELUCCA_NATIVE_BANKS
#define FELUCCA_NATIVE_BANKS 0             /* the CZ native tone collection in PRESETS: CZ U01..U26 (cz_bank.c; after
                                            * Melodee 0.12, keremimo/melodee) */
#endif
/* the CZ collection: 26 native tones of 144 B in one A/B storage object, 2 sectors of 4 KiB at USR3's end below the
 * snapshot area: USR3 holds 8 KiB less (Melodee 0.12 has 128; one object's 3840 B take 26) */
#define CZ_NUSER (FELUCCA_NATIVE_BANKS && FELUCCA_ENG_CZ ? 26u : 0u)
#define NB_SECTORS (CZ_NUSER ? 2u : 0u)
#ifndef FELUCCA_BACKUP
#define FELUCCA_BACKUP 1                   /* the web editor's backup / restore of everything stored (ed_backup.c) */
#endif
/* the live editor's features (builder items ED_MIX, ED_SEQ, ED_SCREENS, 2026-10-19: "group them in logical features");
 * each command's own switch below follows its feature (a host test may still set one alone) */
#ifndef FELUCCA_ED_MIX
#define FELUCCA_ED_MIX 1                     /* web editor: the mixer: every effect slot, the drum lanes' mix and LFOs, solo and mute */
#endif
#ifndef FELUCCA_ED_SEQ
#define FELUCCA_ED_SEQ 1                     /* web editor: the sequencer: a step in a drum lane's context, the pattern tools, the song chain */
#endif
#ifndef FELUCCA_ED_SCREENS
#define FELUCCA_ED_SCREENS 1                 /* web editor: the Optimist UI's screens, GLOBAL and SYSTEM, the scope (the Optimist UI) */
#endif
#ifndef FELUCCA_ED_FXS
#define FELUCCA_ED_FXS FELUCCA_ED_MIX        /* web editor protocol v10: the FX slots (cmds 86, 87, tag 0x56) (io/editor/ed_fxs.c) */
#endif
#ifndef FELUCCA_ED_FXP
#define FELUCCA_ED_FXP FELUCCA_ED_MIX        /* web editor protocol v13: every effect slot by owner and its parameters (cmd 94, INFO tag 0x57) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_DLAN
#define FELUCCA_ED_DLAN FELUCCA_ED_MIX       /* web editor protocol v13: the drum lanes' mix, LFOs and sequencer mutes (cmd 95, tag 0x58) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_SOLO
#define FELUCCA_ED_SOLO FELUCCA_ED_MIX       /* web editor protocol v14: track and lane solo and mute (cmd 104, tag 0x61) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_STEPLANE
#define FELUCCA_ED_STEPLANE FELUCCA_ED_SEQ   /* web editor protocol v14: a step in a drum lane's context, its lockable values (cmd 105, tag 0x62; the automation store) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_TOOLS
#define FELUCCA_ED_TOOLS FELUCCA_ED_SEQ      /* web editor protocol v13: the STEP TOOLS and the note generators (cmd 98, tag 0x5B; SEQTOOLS or NOTEGEN) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_SONG
#define FELUCCA_ED_SONG FELUCCA_ED_SEQ       /* web editor protocol v13: the song chain (cmd 97, tag 0x5A; the arranger) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_SCRS
#define FELUCCA_ED_SCRS FELUCCA_ED_SCREENS   /* web editor protocol v13: the Optimist UI's screens as rows of cards (cmd 99, tag 0x5C; the Optimist UI) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_SCRN
#define FELUCCA_ED_SCRN FELUCCA_ED_SCREENS   /* web editor protocol v13: GLOBAL and SYSTEM as the Optimist UI's lines (cmd 96, tag 0x59; the Optimist UI) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_SCOPE
#define FELUCCA_ED_SCOPE FELUCCA_ED_SCREENS  /* web editor protocol v13: the oscilloscope's trace (cmd 101, tag 0x5E; the Optimist UI) (io/editor/ed_*.c) */
#endif
/* (part of live editing, always: no builder item since 2026-10-19) */
#ifndef FELUCCA_ED_FAVS
#define FELUCCA_ED_FAVS 1                    /* web editor protocol v13: the favorites (cmd 100, tag 0x5D; FAVORITES) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_UNDO
#define FELUCCA_ED_UNDO 1                    /* web editor protocol v14: undo and redo (cmd 102, tag 0x5F) (io/editor/ed_*.c) */
#endif
#ifndef FELUCCA_ED_NOTE
#define FELUCCA_ED_NOTE 1                    /* web editor protocol v14: notes played from the editor (cmd 103, tag 0x60) (io/editor/ed_*.c, seq/ednote.c) */
#endif
#ifndef FELUCCA_CPU_GUARD
#define FELUCCA_CPU_GUARD 0                /* the predictive CPU guard: eases back quality, then UNISON, then notes
                                            * under overload (cpuguard.c, after Flowstate; docs/CPU-GUARD.md) */
#endif
#ifndef FELUCCA_MISSING_WARN
#define FELUCCA_MISSING_WARN 1             /* "MISSING: PHYS T2, KIT 909" when a load uses what this build leaves
                                            * out; SAVE > TOOLS > MISS lists it again (miss.c) */
#endif
#ifndef FELUCCA_FAVORITES
#define FELUCCA_FAVORITES 1                /* mark sounds as favorites and browse only them: SAVE > PRESETS KNOB 3 FAV, KNOB 4
                                            * LIST ALL / FAV (core/favorites.c, model.c; Felucca 1.6; SLOOP's UI) */
#endif
#ifndef FELUCCA_LIVE_SAVE
#define FELUCCA_LIVE_SAVE 0                /* saves while playing: no STOP BEFORE SAVE, the waiting writes (sections, song,
                                            * settings, the working project) go to flash with the transport running, one
                                            * at a time; off again until the next start if the flash part ever holds an
                                            * erase in one window (seq.c SAVE_WAITS_PLAY, felucca.c FL_ERASE_HELD) */
#endif
#ifndef FELUCCA_PARAM_HELP
#define FELUCCA_PARAM_HELP 0               /* the knob's value in plain words on the top bar ("Filter cutoff"), from
                                            * tools/param_help.json (param_help.c) */
#endif
#ifndef FELUCCA_ED_LIVE
#define FELUCCA_ED_LIVE 1                  /* the web editor's live editing (editor.c: GET / SET / DESC, presets, tracks,
                                            * steps, sync and the ED_* commands); 0: backup and restore only (ed_lite.c) */
#endif
#ifndef FELUCCA_LIBRARIAN
#define FELUCCA_LIBRARIAN 1                /* the web editor's librarian, with live editing or without (ed_lite.c): the user presets
                                            * (16..21) and the user kits (38..41) listed, loaded, stored, erased, read and written;
                                            * without live editing the sections and the patterns too (2026-10-19: the base is the
                                            * communication layer, the rest sub-items) */
#endif
#define ED_LIB FELUCCA_LIBRARIAN           /* (the user presets and kits answered: either editor, with the librarian) */
#define ED_UKITS (FELUCCA_DRUM_KITS && ED_LIB)   /* (the user kits' commands, 38..41) */
#ifndef FELUCCA_PAGE_LINES
#define FELUCCA_PAGE_LINES 1               /* the Optimist UI's screens of rows with no picture as GLOBAL's four lines (a row
                                            * a page, KNOB k line k), not 2x2 cards over a row list (op_gldraw.c pl_draw).
                                            * Always on in a firmware build (the builder item was retired 2026-10-21); the host
                                            * tests pass 0 to keep the cards they were written for (ui_optimist_test.c) */
#endif
#ifndef FELUCCA_SYS_DEBUG
#define FELUCCA_SYS_DEBUG 1                /* the Optimist UI's SYSTEM debug pages: INTERNALS, USB STATS and INFO's clock,
                                            * second core, drops, guard lines (op_project.c); off, INFO is CPU LOAD,
                                            * UNDERRUNS, BATTERY. The builder's default is 0; the host tests keep them */
#endif
#ifndef FELUCCA_DRUM_ACCENT
#define FELUCCA_DRUM_ACCENT 1              /* the drum track's ACCENT lane is heard: its steps raise every hit of the step
                                            * by the ACCENT amount (drums.c dacc_vel, seq.c drum_step); off: the lane is
                                            * kept, edited and stored as ever, not heard. Needs the automation store */
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
/* Options of FM6 (ignored when FELUCCA_ENG_FM6 is 0). FM6 plays one synthesis, Dexed's MSFA (the MARK I, MODERN and OPL
 * modes, the factory voices R01..R16 and the U01..U32 user bank were retired 2026-10-18: builder bits 10, 11, 12, 15, 16,
 * 80, 81, 89 are never reused). INIT VOICE is always there; the voice packs are builder items; DX7 SysEx is received only */
#ifndef FELUCCA_FM6_SYSEX
#define FELUCCA_FM6_SYSEX 1                /* DX7 SysEx in: a voice replaces the track's, a 32-voice bank is a pick list in PRESETS */
#endif
#ifndef FELUCCA_FM6_PK_KEYS
#define FELUCCA_FM6_PK_KEYS 0              /* pack 1: keys and e-pianos, 16 voices */
#endif
#ifndef FELUCCA_FM6_PK_BASS
#define FELUCCA_FM6_PK_BASS 0              /* pack 2: basses */
#endif
#ifndef FELUCCA_FM6_PK_BELLS
#define FELUCCA_FM6_PK_BELLS 0             /* pack 3: bells and mallets */
#endif
#ifndef FELUCCA_FM6_PK_PADS
#define FELUCCA_FM6_PK_PADS 0              /* pack 4: pads, leads, brass and FX */
#endif
#ifndef FELUCCA_FM6_PK_80S
#define FELUCCA_FM6_PK_80S 0               /* pack 5: 80s mega classics */
#endif
#define FM6_NPACKS (FELUCCA_FM6_PK_KEYS + FELUCCA_FM6_PK_BASS + FELUCCA_FM6_PK_BELLS + FELUCCA_FM6_PK_PADS + FELUCCA_FM6_PK_80S)
/* FM6's own collection after the user presets in PRESETS (nbank.c): the packs built, the bank received */
#define FELUCCA_FM6_LIST (FELUCCA_ENG_FM6 && (FM6_NPACKS || FELUCCA_FM6_SYSEX))
#define FELUCCA_NB_LIST (FELUCCA_NATIVE_BANKS || FELUCCA_FM6_LIST)   /* an engine's own collection in PRESETS (nbank.c) */
