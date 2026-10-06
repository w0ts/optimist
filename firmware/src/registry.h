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

/* X(uid, NAME, fallback uid, the ENG list name): the UID is the engine's number in FUN7 projects and UPB2 user
 * presets. Fallbacks: FM6 -> DIGITAL -> ANALOG, GRAIN -> SAMPLE -> ANALOG, SLICE -> SAMPLE, the rest -> ANALOG;
 * with ANALOG absent too, the first engine built. Order = ENGINES[] order (the slots). */
#if FELUCCA_ANALOG2
#define ENGINE_LIST(X) X(0, ANALOG, 0, "ANALOG") X(1, DIGITAL, 0, "DIGITAL") X(2, PHASE, 0, "PHASE") \
    X(3, LOFI, 0, "LOFI") X(4, SAMPLE, 0, "SAMPLE") X(5, FORMANT, 0, "VOICE") X(6, TRIO, 0, "TRIO")      \
    X(7, DRAWBAR, 0, "WHEEL") X(8, GRAIN, 4, "GRAIN") X(9, FM6, 1, "FM6") X(10, SLICE, 4, "SLICE")    \
    X(11, PHYS, 0, "PHYS") X(12, ACID, 0, "ACID")
#define ENG_UID_N 13u
#define ENG_UID_PHYS 11u
#define ENG_UID_ACID 12u
#define ENG_UID_FM6 9u                     /* (the importers: DX7 and FM6 parts of older projects play FM6) */
#define ENG_UID_SUPER 0u                   /* SUPER's presets and parts: ANALOG's swarm */
#else                                      /* (the original ANALOG and SUPER: measurements only, tests/analog2_test.c) */
#define FELUCCA_ENG_SUPER 1
#define ENGINE_LIST(X) X(0, ANALOG, 0, "ANALOG") X(1, DIGITAL, 0, "DIGITAL") X(2, PHASE, 0, "PHASE") \
    X(3, LOFI, 0, "LOFI") X(4, SAMPLE, 0, "SAMPLE") X(5, FORMANT, 0, "VOICE") X(6, TRIO, 0, "TRIO")      \
    X(7, DRAWBAR, 0, "WHEEL") X(8, GRAIN, 4, "GRAIN") X(9, SUPER, 0, "SUPER") X(10, FM6, 1, "FM6")         \
    X(11, SLICE, 4, "SLICE")
#define ENG_UID_N 12u
#define ENG_UID_FM6 10u
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
/* every engine with a factory preset in ui.c BANK is built (BANK names no SLICE preset) */
#define ENG_BANK_ALL (NENGINES - FELUCCA_ENG_SLICE - FELUCCA_ENG_ACID == (int)ENG_UID_N - 2)

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
 * kit; with none, the PERC set is left out of the build). A kit not built plays the other source's first kit. */
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

/* -------------------------------------------------------------- features --- */
#ifndef FELUCCA_MIDI_CLOCK
#define FELUCCA_MIDI_CLOCK 1               /* follow a MIDI clock (GLO > SYSTEM SYNC: AUTO TRS > USB > INT) */
#endif
#ifndef FELUCCA_MIDI_EXPR
#define FELUCCA_MIDI_EXPR 1                /* MIDI bend, mod wheel, breath, foot, aftertouch, sustain, RPN 0
                                            * (0: notes and the panics CC 120..123 only) */
#endif
#ifndef FELUCCA_OV_ARP
#define FELUCCA_OV_ARP 1                   /* the ARP graph and the ARP family in VIEW ALL */
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
#ifndef FELUCCA_BACKUP
#define FELUCCA_BACKUP 1                   /* the web editor's backup / restore of everything stored (ed_backup.c) */
#endif
#ifndef FELUCCA_OVERVIEW
#define FELUCCA_OVERVIEW 1                 /* VIEW ALL: a page family at once (ui_overview.c, GLO > SYSTEM VIEW) */
#endif
#ifndef FELUCCA_MISSING_WARN
#define FELUCCA_MISSING_WARN 1             /* "MISSING: PHYS T2, KIT 909" when a load uses what this build leaves
                                            * out; SAVE > TOOLS > MISS lists it again (miss.c) */
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
