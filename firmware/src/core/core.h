/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca core types: tracks, voices, engines, parameters.
 * Four tracks: tracks 1..3 are synth parts (each its own engine, preset, parameters,
 * voices and 64-step pattern), track 4 is the GM drum part (drums.c; its own voices,
 * pattern and the pattern parameters of its track_t). The parts share one budget of
 * NVOICE sounding voices (voice.c). */
#include <stdint.h>
#include "backports.h"       /* the backported features' build switches (FELUCCA_CHANCE ...) */
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPOLY 8
#define NPART 3                  /* synth parts: tracks 1..3 */
#define NTRK 4                   /* + the drum track */
#define TRK_DRUM 3
#if FELUCCA_USB_AUDIO
/* USB audio capture (usb_audio_stream.c): interleaved mono stems, cleared by mix_block;
 * post insert / level / mute, pre pan / sends / FX buses / master. */
static int32_t track_capture[CTL * NTRK];
static uint8_t track_capture_on = 1;     /* the stems are taken this block (audio.c: the host's capture, at the
                                          * block start); 0 (FELUCCA_SKIP): mix_block need not clear them */
#endif
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
enum { Q_OFF, Q_SNAP, Q_WHITE, Q_ALL, Q_SEQ };   /* P_QUANT (SCL › KEYS): seq.c scale_map; SNAP = the old ON;
                                                * SEQ: FELUCCA_QNT_SEQ only (qnt_seq.c) */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#ifndef FELUCCA_SLICE
#ifdef FELUCCA_ENG_SLICE
#define FELUCCA_SLICE FELUCCA_ENG_SLICE   /* (the builder's name for it, registry.h) */
#else
#define FELUCCA_SLICE 0          /* the SLICE engine (eng_slice.c): kept in the tree, not built by default */
#endif
#endif
/* The drum lanes' own sounds (drum_edit.c, ui_drums.c, drum_kits.c), each a build switch on its own (the
 * project keeps their data in every build: a build without one keeps it and plays the kit as it is) */
#ifndef FELUCCA_DRUM_EDIT
#define FELUCCA_DRUM_EDIT 1      /* the drum sound editor: EDIT on the drum track, 8 offsets per lane */
#endif
#ifndef FELUCCA_DRUM_USR
#define FELUCCA_DRUM_USR 1       /* user samples (USR1..USR3) on any drum lane */
#endif
/* FELUCCA_SKIP 1: the audio path leaves out work that cannot change a sample (a bus with nothing to add, a part
 * with no voice, a gain ramp that stands still, ...); 0 computes it all, as before (tests/skip_test.c builds both
 * and compares them sample for sample: the same output, the same state) */
#ifndef FELUCCA_SKIP
#define FELUCCA_SKIP 1
#endif
#ifndef FELUCCA_DRUM_KITS
#define FELUCCA_DRUM_KITS 1      /* user drum kits: a bank of 16 in the data flash, after the factory kits */
#endif
#if defined(FELUCCA_DRUM_SENDS) && !FELUCCA_DRUM_SENDS
#error "FELUCCA_DRUM_SENDS is retired: each drum lane's REV / DLY / CHO (drum_sends.c) are the drums' only sends"
#endif
#ifndef FELUCCA_ANALOG2
#define FELUCCA_ANALOG2 1        /* ANALOG 2 (eng_analog2.c): osc 2 wave / interval / sync, a filter envelope,
                                  * filter modes, drift, SUPER's swarm (SUPER is no engine of its own then: FM6
                                  * is 9); 0 = the original ANALOG and SUPER (and project format 5) */
#endif
#include "registry.h"     /* NENGINES, the engines' UIDs (FUN7 numbers: ANALOG 0 .. FM6 9, SLICE 10) and switches */
#include "../system/cpuguard.h"     /* the CPU guard's level, read by the render (FELUCCA_CPU_GUARD; else the constant 0) */
#define UP_SLOTS 32u             /* user presets (upreset.c) */

/* HOT: the audio path, executed from RAM (app.ld .ram_hot, copied at boot by main.c) instead of XIP
 * flash, so the UI's code and font reads between two halves cannot evict it from the cache. RAM and
 * XIP are farther apart than a direct call reaches (the linker refuses one): HOT code calls only HOT
 * code directly, and calls between the two go through a pointer (FAR, or an engine's function
 * pointer). build.py checks both directions. Nothing HOT runs while the flash is busy (the flash
 * driver runs with interrupts off). The host build (tests) has one address space: no section. */
/* HOT2: the same, for what no longer fits RAMTEXT: linked at the start of RAM, before .data (app.ld
 * .ram_hot2; same rules: it calls HOT / HOT2 code directly, XIP code through a pointer). Empty in the
 * integrated build: main RAM went to FM6's tables (built at boot, ~11.8 KB), so RAMTEXT holds the mix,
 * ANALOG 2, FORMANT, DIGITAL, TRIO, PHASE, WHEEL, SAMPLE and LOFI; FM6's code and GRAIN run from XIP
 * (docs/MEMORY-BUDGET.md: the sizes and the other choices). TAB_RAM
 * (felucca_tables.h) puts a constant table that the sample loops read into .data: copied to RAM at
 * boot, so the UI's font reads cannot evict it from the data cache either (no extra flash). */
#if defined(__PI32V2__) && !(defined(FELUCCA_ASM_CHECK) && FELUCCA_ASM_CHECK)
/* (an ASM_CHECK verification build runs every asm kernel next to its C: too big for RAMTEXT, so it
 * keeps the whole audio path in XIP; the same code, placed elsewhere) */
#define HOT __attribute__((section(".ram_hot")))
#define HOT2 __attribute__((section(".ram_hot2")))
#else
#define HOT
#define HOT2
#endif
/* This compiler inlines a function into a caller in another section only when it is always_inline:
 * AINL marks the small helpers that the HOT code and the rest both use (inlined everywhere). */
/* IN_POOL: a zeroed buffer of the main loop (no audio loop reads it) in the pool instead of main RAM (app.ld .pool,
 * the same SRAM past main RAM's 96 KiB; main RAM is the scarcer: docs/MEMORY-MAP.md). The host: one address space */
#ifdef __PI32V2__
#define IN_POOL __attribute__((section(".pool")))
#else
#define IN_POOL
#endif
/* IN_POOL_AUDIO: a zeroed buffer the audio path reads (per sample or per block): in the pool only with the builder's
 * EXPERIMENTAL TAB_IN_POOL (as the TAB_RAM tables), else main RAM as before; unverified on the FM-1 (the console's
 * tabbench measures the two) */
#if defined(__PI32V2__) && defined(FELUCCA_TAB_IN_POOL) && FELUCCA_TAB_IN_POOL
#define IN_POOL_AUDIO __attribute__((section(".pool")))
#else
#define IN_POOL_AUDIO
#endif
#define AINL static inline __attribute__((always_inline))
/* the interrupts off and back as they were, for code that runs in the main loop and in the audio interrupt alike
 * (fx_slots.c's slot puts: a song mode STOP applies the loop put aside from the ISR, arranger_scene.c song_restore ->
 * proj_apply -> fx_rec.c fxr_decode). A bare fm1_irq_on() there is an sti inside the interrupt, which outlives its
 * return on the FM-1 (core/audio.c ua_block: the USB audio builds that hung, 2026-10-12): IRQ_RESTORE switches them on
 * only if they were on at IRQ_SAVE (icfg bit 9, the global enable). The host: its own hook (tests/audio_hang_stress.c),
 * else on. */
#ifdef __PI32V2__
#define IRQ_IE_NOW() (fm1_icfg() & 0x200u)
#elif defined(HOST_IRQ_HOOKS)
#define IRQ_IE_NOW() host_irq_ie()
#else
#define IRQ_IE_NOW() 1u
#endif
#define IRQ_SAVE(ie) uint32_t ie = IRQ_IE_NOW(); fm1_irq_off()
#define IRQ_RESTORE(ie) do { if (ie) fm1_irq_on(); } while (0)
#ifndef HOST_DUAL
#define HOST_DUAL 0
#endif
/* two cores render parts at once (system/dual.c mix_block_dual; HOST_DUAL: the host's thread, tests/audio_hang_dual.h) */
#define DUAL_RENDER (FELUCCA_DUAL >= 2 || HOST_DUAL)
AINL void *far_ptr(void *p) { void *volatile q = p; return q; }   /* not folded back into a call */
#define FAR(fn) ((__typeof__(&fn))far_ptr((void *)&fn))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS,
    F_SWING,                    /* 0..100: MPC swing, 50 % (straight) .. 75 % */
    F_FILT                      /* -64..63: the DJ filter, LP <- OFF -> HP */
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

/* the FILTER insert type (fx_slots.c FXT_FILT; fx.c tflt_*): SLOOP 2.4's track FILTER (TRK_FILT) and the master DJ
 * filter (FX_DJF) are one effect since 2026-10-14 (fx-rework: "convert the special insert (like dj filter etc) as normal
 * ones we can plug everywhere"), built with either item, on any insert slot */
#define TFLT_ON (FELUCCA_TRK_FILT || FELUCCA_FX_DJF)
#define SL24_TP (TFLT_ON || FELUCCA_CHORDPLUS)   /* SLOOP 2.4's track values P_TFLT P_STRUM P_VLEAD exist */
/* any further insert type (fx/insfx/insfx.c); one line: tests/emu/fwfacts.py reads the #defines line by line */
#define INS_FX (FELUCCA_INS_FOLD || FELUCCA_INS_FUZZ || FELUCCA_INS_PHASER || FELUCCA_INS_FLANGER || FELUCCA_INS_FORMANT || FELUCCA_INS_WIDTH || FELUCCA_INS_CHORUS || FELUCCA_INS_DELAY || FELUCCA_INS_GATER || FELUCCA_INS_JCHORUS || FELUCCA_INS_EQ3)
/* the insert types with a limited number of instances (fx/insfx/fxi.c: CHRS ECHO JUNO FLNG, a builder count each) */
#define INS_LIM (FELUCCA_INS_CHORUS || FELUCCA_INS_DELAY || FELUCCA_INS_JCHORUS || FELUCCA_INS_FLANGER)
enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: the sound's level trim (1/2 dB), set by the presets so
                                                * every factory sound comes out as loud as the others (fx.c) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_L1DA, P_L1AA, P_L1DB, P_L1AB,            /* LFO DEST: two slots, DEST A + AMT A, DEST B + AMT B (LT_*, voice.c
                                                * lfo_slots; params.c lfo_*). These four ids held the depths PIT FLT SHP
                                                * AMP until 2026-10 (P_LEG_*: their stored form, lfo1_load) */
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c); new common parameters go just
                                                * before P_E0 (user presets and projects map by count) */
    P_CHORD,                                   /* chord mode: one key plays a chord of the scale (seq.c) */
    P_FXOFF,                                   /* FX bypass: 1 = the track plays dry (no DIST, SLICER, sends;
                                                * their values are kept: fx_on) */
#if FELUCCA_ANALOG2
    /* ANALOG 2's pages OSC 2, SWARM, FLT 2 (eng_analog2.c; shown on an ANALOG track only, params.c page_shown)
     * and ENV 2, ENV2 DEST (the ENV family, ANALOG tracks): osc 2's wave (0 = as osc 1), its interval, hard
     * sync; drift (also a free-running phase); the filter's mode; ENV2, an ADSR of its own: attack, decay
     * (FATK, FDEC: the AD envelope of the cutoff it was), sustain, release (0: the decay's time); its amount
     * on each destination: FLT (FENV, the cutoff), PIT (both oscillators and the swarm), SHP (as ENV DEST
     * SHP), OSC2 (osc 2's pitch), SDTN (the swarm's spread; the SWARM page); the swarm (copies of osc 1,
     * SUPER's superwave: 0 = none) and its spread. Their defaults leave the sound as before. SUS2 .. the
     * SDTN amount (A2X_N, from P_A2ESUS) are kept outside a track's stored values (project.c pj_x, upreset.c:
     * a2x_pack, a byte each). Until project format 11 one destination DST2 (A2E_*) took the one amount FENV */
    P_A2WAVE, P_A2SEMI, P_A2SYNC, P_A2DRFT,
    P_A2FTYP, P_A2FATK, P_A2FDEC, P_A2FENV,
    P_A2SWRM, P_A2SDTN,
    P_A2ESUS, P_A2EREL, P_A2EPIT, P_A2ESHP, P_A2EOS2, P_A2ESDT,
#endif
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
#if SL24_TP
    /* SLOOP 2.4's track values (isod89/sloop-fm1 v2.4, 8d3823f: its P_TFLT 50, P_STRUM 51, P_VLEAD 52), after the
     * engine's eight so that no id before moves (motion events, locks, the editor, the stored values keep their
     * numbers): the track's FILTER (fx.c, FELUCCA_TRK_FILT), the chords' STRUM and voice leading (seq.c,
     * FELUCCA_CHORDPLUS). All three exist when either switch is on (the same ids in every such build). Not part of
     * a sound (param_kept) nor of a user preset; a project keeps them packed in the drum track (project.c px_pack) */
    P_TFLT, P_STRUM, P_VLEAD,
#endif
#if FELUCCA_MASTER_COMP
    /* the COMP insert's amount (fx_slots.c, an FX slot type): 0 off .. 127 (threshold -1 .. -30 dB, automatic
     * make-up). Not part of a sound nor of a user preset (decision D8); the FX record keeps it (fx_rec.c) */
    P_TCOMP,
#endif
#if INS_FX
    /* the further insert types' amounts (fx/insfx/insfx.c; fx_slots.c FXT_FOLD ..): 0 off .. 127. All eight exist when
     * any of them is built (the same ids in every such build). Not part of a sound nor of a user preset (as P_TCOMP);
     * the FX record keeps them (fx_slots.c fxs_tlv, a TLV a type). P_TCHRS P_TECHO (2026-10-13): the insert copies of
     * the chorus and a short delay (fxi.c) */
    P_TFOLD, P_TFUZZ, P_TPHSR, P_TFLNG, P_TFRMT, P_TWIDE, P_TCHRS, P_TECHO,
    /* GATER (2026-10-14, fx/insfx/gater.c): its amount (DEPTH) after ECHO's, then its PATTERN, RATE and SHAPE (a
     * track's own, kept in GATER's TLV after the amounts) */
    P_TGATE, P_TGPAT, P_TGRATE, P_TGSHP,
#endif
#if FELUCCA_FX_DUST
    /* the DUST insert's amount (2026-10-14, fx-rework: the master's DUST is an insert type, fx_slots.c FXT_DUST, on any
     * slot; the master's own amount stays G_DUST). Before LFO 2's (the last values of a track: project.c l2_pack); not part of a sound nor of a user preset;
     * the FX record keeps it (fx_slots.c fxs_tlv, DUST's TLV) */
    P_TDUST,
#endif
#if FELUCCA_INS_JCHORUS
    /* JUNO CHORUS (2026-10-14, fx/junocho/juno_cho.h, fx_slots.c FXT_JUNO): a track's MIX (its amount) and MODE (I II
     * I+II). Not part of a sound nor of a user preset; the FX record keeps them (fx_slots.c fxs_tlv, JUNO's TLV) */
    P_TJCHO, P_TJMODE,
#endif
#if FELUCCA_INS_EQ3
    /* EQ3 (2026-10-15, fx/insfx/eq3.c, fx_slots.c FXT_EQ3): a track's ON (its amount: 1 by default, so the type is heard
     * as soon as a band moves); the bands and frequencies are the owner's (eq3_v). Not part of a sound nor of a user
     * preset; the FX record keeps them (fx_slots.c fxs_tlv, EQ3's TLV) */
    P_TEQ,
#endif
#if FELUCCA_LFO2
    /* LFO 2 (builder item LFO2; voice.c lfo2_tick): a second LFO a part, after every other value so that no id before
     * moves. RATE as LFO's (0..127 free, -1..-10 synced: LSYNC_N), WAVE as LFO's, TRIG (NOTE: a fresh phrase restarts
     * it at phase 0; FREE: never), POL (BI -1..+1, UNI 0..+1: SLOOP 2.6's P_LTRIG P_LPOL), and two slots as LFO's
     * (DEST A + AMT A, DEST B + AMT B: LT_*). Off (every slot OFF or at 0): nothing computed. The parts' only (the
     * drum track has its lanes' LFOs). Not part of a sound nor of a user preset (param_kept, as the track FILTER); a
     * project keeps them packed in the drum track's LFO slots (project.c l2_pack), no format change */
    P_L2RATE, P_L2WAVE, P_L2TRIG, P_L2POL, P_L2DA, P_L2AA, P_L2DB, P_L2AB,
#endif
    /* ENV DEST and ENV2 DEST (2026-10-14): each envelope's two slots, as LFO DEST's (DEST A + AMT A, DEST B + AMT B, the
     * shared list LT_*; voice.c env_plan). Nothing is wired: ENV's slot A is AMP 127 by default (a new sound, INIT, a
     * project or a preset of before), so every sound plays as it did. Part of a sound (model.c param_kept); stored in
     * the words of the depths they replaced, ENV DEST FLT SHP and ENV2's amounts (env_store.h) */
    P_N1DA, P_N1AA, P_N1DB, P_N1AB,
#if FELUCCA_ANALOG2
    P_N2DA, P_N2AA, P_N2DB, P_N2AB,              /* ENV2 (its ADSR: P_A2FATK P_A2FDEC P_A2ESUS P_A2EREL), every engine */
#endif
    /* VOICES (voice.c trk_nvoice): the part's voice count in POLY and UNISON, 0 = the engine's max (JUNO 6, ACID 1 ...; the card shows it as that number, params.c page_desc),
     * else 1..8 and at most the engine's (MONO stays a mode of P_VOICE). Every build. Not part of a sound nor of a user
     * preset (param_kept), not lockable; stored in a project in the FX record (fx_rec.c VCNT_TLV) */
    P_VOICES,
    P_COUNT
};
#if FELUCCA_ANALOG2
#define P_NLAST P_N2AB          /* the envelopes' slots: P_N1DA .. P_NLAST */
#else
#define P_NLAST P_N1AB
#endif
/* The modulation destinations, one list for the four modulators (LFO DEST, LFO2 DEST, ENV DEST, ENV2 DEST; params.c
 * N_LT, voice.c LT_TAB says how each acts):
 *   PIT FLT SHP AMP  every voice of the part (an LFO: each block, as its four depths did, AMT 2 = depth 1; AMP: AMT =
 *                    depth, AMT < 0 the dip from the bottom. An envelope: each voice's own, ENV DEST's depths of before
 *                    at AMT 2 = depth 1; AMP 127 is the envelope itself, below 127 less of it, < 0 upside down)
 *   OSC2 SDTN        the voice's ANALOG osc 2 pitch and its swarm's spread (ENV2 DEST's of before; nothing elsewhere)
 *   RES              the engine's resonance (its EDIT value named RES / RESO / Q), PAN, LVL (the part's level), ATK
 *                    DEC SUS REL (ENV's), ATK2 DEC2 SUS2 REL2 (ENV2's), E1..E8 (the engine's own EDIT values): the
 *                    value moved (voice.c lfo_over: an LFO's for the block, the part's; an envelope's for each voice
 *                    as it renders, env_vmove), +- half its range at full (an envelope: 0 .. its range)
 *   CHO DLY REV      the part's sends, INS1 INS2 the amount of the insert in its slot S1 / S2 (fx_slots.c ins_amt), as
 *                    the values above (an envelope: the part's newest voice's)
 *   L1RT L1AMT       LFO's RATE (its free rates only) and its two amounts (x 0..2); L2RT L2AMT LFO 2's (with LFO2);
 *   EN1AMT EN2AMT    ENV's, ENV2's two amounts (x 0..2)
 * Mod of mod: every move starts from the authored value each block (or voice), from the modulators' values of the block
 * before, so nothing feeds back into a runaway, whatever the order (voice.c: the order). New destinations: one line in
 * LT_TAB and a name. The numbers are stored (projects, presets): append, never move. L1RT L1AMT were XRATE XAMT (the
 * other LFO's) until 2026-10-14: LFO's slots of before on them are L2RT L2AMT now (lfo1_load) */
enum { LT_OFF, LT_PIT, LT_FLT, LT_SHP, LT_AMP, LT_RES, LT_PAN, LT_LVL, LT_ATK, LT_DEC,
       LT_E1, LT_E2, LT_E3, LT_E4, LT_E5, LT_E6, LT_E7, LT_E8, LT_CHO, LT_DLY, LT_REV, LT_INS1, LT_INS2,
       LT_L1RT, LT_L1AMT, LT_SUS, LT_REL, LT_ATK2, LT_DEC2, LT_SUS2, LT_REL2, LT_N1AMT, LT_N2AMT, LT_OSC2, LT_SDTN,
       LT_L2RT, LT_L2AMT, LT_N };
#define LT_NB (FELUCCA_LFO2 ? LT_N : LT_L2RT)  /* the destinations of this build (L2RT L2AMT: with LFO 2) */
/* ENV's summed AMT on FLT (its slots; ENV DEST FLT d of before is AMT 2 d): for an engine that models the envelope's
 * cutoff depth itself (eng_juno.c) */
#define ENV1_FLT_AMT(p) (((p)[P_N1DA] == LT_FLT ? (p)[P_N1AA] : 0) + ((p)[P_N1DB] == LT_FLT ? (p)[P_N1AB] : 0))
#define LT_XRATE_V1 LT_L1RT             /* (the stored numbers of XRATE XAMT, LFO's slots until 2026-10-14) */
#define LT_XAMT_V1 LT_L1AMT
#define LT_CLASSIC(d) ((uint32_t)(d) - LT_PIT < 4u)   /* PIT FLT SHP AMP: the four depths of before */
#define LFO_AMT_LO (-128)               /* a slot's AMT */
#define LFO_AMT_HI 127
/* the ids that held the depths PIT FLT SHP AMP (a project's, a user preset's, SLOOP's, a preset's x[]): their stored form */
#define P_LEG_PIT P_L1DA
#define P_LEG_FLT P_L1AA
#define P_LEG_SHP P_L1DB
#define P_LEG_AMP P_L1AB
#define P_ENG_END (P_E7 + 1)    /* the values a track stores in a project or a user preset: P_LEVEL .. P_E7 */
/* RATE below 0 (voice.c lfo_inc): one cycle per note value of the tempo, longest first, as SLOOP 2.6's LFO 2 SYNC
 * (after Felucca 1.4 by Leo Kuroshita): its P_LSYNC n (0 OFF, 1..10) is our RATE -n; with SYNC on its RATE is not
 * heard. LRATE_OF_SLOOP: its SYNC and RATE -> our RATE; SLOOP_LSYNC_OF: our RATE -> its SYNC (its RATE: ours, 0..127) */
#define LSYNC_N 10
#define LRATE_OF_SLOOP(lsync, rate) ((lsync) > 0 && (lsync) <= LSYNC_N ? -(int32_t)(lsync) : (int32_t)(rate))
#define SLOOP_LSYNC_OF(rate) ((rate) < 0 ? -(int32_t)(rate) : 0)
#define P_TAIL 1                /* a track has values after P_E7 (P_ENG_END .. P_COUNT - 1): none stored with it, none part
                                 * of a sound but the envelopes' slots (param_kept) */

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_VIEW, G_INFO,   /* G_VIEW: the old ROUT slot (projects keep their format); kept in the
                                 * settings, not in a project (ui_overview.c) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page (the ENGINE page is gone); a SET of G_ENGSEL switches the engine (editor) */
    G_CLRSEQ, G_INITSND,
    G_DRCH, G_DRLVL, G_DRREV,
    G_DUST, G_DUCK, G_FILT,     /* the master bus: lo-fi / vinyl, the kick ducking the parts, the DJ filter (fx.c) */
    G_ROLL,                     /* note repeat rate (ARP + key, seq.c) */
    G_NEWPRJ,                   /* TOOLS > NEW: a new project (GO) */
    /* the master COMP and LIMIT (master_comp.c), GLO > COMP / LIMIT. They come after the globals a project's g[] holds
     * (PJ_NG: the project format does not grow); a project keeps them packed in its reserved bytes (project.c
     * mc_pack). Projects from before read 0 there: THRS OFF, CEIL OFF, the master as before */
    G_CTHR, G_CRAT, G_CATK, G_CREL,   /* THRS (0 OFF, 1..30: -1..-30 dB), RATIO, ATK, REL (enums) */
    G_CGAIN, G_CCEIL,           /* GAIN (make-up, 0..15 dB), CEIL (the limiter's ceiling: 0 OFF; -3.0 dB in a new
                                 * project since 2026-10-11, a saved one keeps its own) */
    /* REC QUANTIZE (the TEMPO screen's second page; seq.c rec_exact): ON (1, the default) a recorded note snaps to the
     * grid, its start and its length (whole steps); OFF keeps the start as a nudge (MICRO) and the length as a GATE
     * lock (PLOCK). Per project, in its G_INFO slot as !QUANT (project.c RECQ_PACK: 0 = ON in every older one) */
    G_RECQ,
    /* the FX returns (2026-10-12; fx.c fx_buses): each send effect's wet level into the master, CHO DLY REV, whatever
     * slot holds it (fx_slots.c). 0..127, 127 unity (the default: the mix of before, sample for sample), 64 one half;
     * 0: silent, that bus not run (its lines cleared). The delay's MIX (G_DMIX) stays its own. The mixer's MASTER row,
     * Returns (op_mixer.c). A project keeps them in its FX record (fx_rec.c FXRET_TLV: none = unity), not in g[] */
    G_CRET, G_DRET, G_RRET,
    G_COUNT
};
#define PJ_NG ((uint32_t)G_CTHR)   /* the globals a project's g[] holds (its format: 32 since format 1's 27 grew) */
#define SL24_AUTO (FELUCCA_SL24_SAFE && FELUCCA_SL24_IMPORT)   /* PROJECT > A24 (sl24_guard.c) */
#define G_A24 (G_COUNT + 2)     /* PROJECT > A24: import SLOOP 2.4's autosave (GO, twice); no stored value */
#define G_CGR (G_COUNT + 1)     /* LIMIT > GR: the master's gain reduction now, whole dB (master_comp.c); no stored value */
#if FELUCCA_MASTER_COMP
static int16_t mc_gr_view;      /* (its value: meters.c, the main loop) */
#endif
#if FELUCCA_MISSING_WARN
#define G_MISS G_COUNT          /* TOOLS > MISS: what the loads use and this build lacks (miss.c); no stored value */
static volatile uint8_t miss_gen;   /* + 1 per load (proj_apply, also in the audio ISR; a user preset / kit) */
#define MISS_BUMP() (miss_gen++)
#else
#define MISS_BUMP() ((void)0)
#endif
#if !FELUCCA_LFO2
static uint8_t l2_miss;         /* the parts whose LFO 2 acts in the project last applied (project.c l2_live): not played
                                 * in a build without LFO2 (miss.c) */
#endif
/* G_SYNC (clock_sync.c): the values of the first clock builds kept (0 INT = projects from before the
 * clock; USB / TRS = usb.c MSRC_*); AUTO, the default, follows TRS, then USB, then the internal tempo */
enum { SYNC_INT, SYNC_USB, SYNC_TRS, SYNC_AUTO };

/* ----------------------------------------------------------- voices --- */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release, 4 fading out (given up) */
    uint8_t kill;                /* stage 4: blocks of the fade left */
    uint8_t pitch_frac;          /* the glide between 1/16 semitones: pitch_cur + pitch_frac / 256 */
    uint8_t ofs;                 /* a new voice: the sample of its first block it starts at (core.h ev_at) */
    int16_t penv;                /* the pitch envelope (ENV > PIT): Q15, a fast fall from the note's start */
    int32_t env;                 /* Q24 */
    int32_t env2;                /* ENV2 (voice.c env2_tick): Q24, | E2_ATK while it rises */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide: 1/4096 semitone per control tick, 0 = RATE (GLIDE: the time an octave takes) */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
} voice_t;
#define E2_ATK (1 << 30)         /* voice_t.env2: still rising (voice.c env2_tick) */

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t plog;                /* the voice's own pitch offset (glide, LFO and ENV pitch, unison detune; not
                                  * TUNE, not the MIDI bend / wheel), Q24 octaves: for engines that figure
                                  * their pitch (FM6: its controllers come through fm6_midi_expr) */
    int32_t fine;                /* the pitch below pitch16, 1/4096 of the increment (fine_inc), for the
                                  * engines that make increments from pitch16; 0 unless a MIDI bend or the
                                  * mod wheel moves the part (then glide and LFO fractions too) */
    int32_t os2, sdt;            /* ANALOG: osc 2's pitch (1/16 semitone) and the swarm's spread moved (OSC2 SDTN) */
    int32_t fine_all;            /* all inc adds to pitch_inc(pitch16), 1/4096 (fine_inc): the unison detune, the
                                  * fine TUNE, the glide / LFO / bend fraction. For a second oscillator made from
                                  * pitch16 (det_inc: ANALOG, PHASE), so it moves with the first (after isod89/
                                  * sloop-fm1 PR #45, Erick Buendia Barrientos) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t arp[4];              /* MODE, RATE, OCT, GATE */
    uint8_t pat;                 /* sequence pattern (PATTERNS[pat - 1]), loaded only into an empty sequencer */
    int8_t x[8];                 /* more of the patch: up to 4 (track parameter id + 1, value) pairs, 0 = none
                                  * (glide, pitch / LFO modulation, voice mode...: preset_extras) */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define ARP(m, rt, o, g) .arp = {(m) + 1, (rt) + 1, (o) + 1, (g) + 1}
#define PAT(n) .pat = (n)
#define XP(...) .x = {__VA_ARGS__}  /* XP(P_GLIDE + 1, 30, P_ED_PIT + 1, 8): parameter id + 1, value */

struct track;
typedef struct {
    const char *name;            /* "VA" */
    const char *page_title[2];
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    int8_t fil_page;             /* EDIT page that holds the filter, -1 = none */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    uint16_t color;              /* accent colour of the engine (RGB565) */
    uint8_t macro[4];            /* unused: SLOOP's HOME screen's KNOB 1..4 (gone: it was unreachable) */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    uint8_t vel_own;             /* 1 = velocity is the engine's (FM6: per operator); else it scales the voice */
    /* optional: the POLY voice (0..cap-1) for a new note, the engine's own choice (FM6: Dexed's) */
    uint32_t (*alloc)(struct track *t, uint32_t note);
    /* optional: MONO / LEGATO / UNISON moved a sounding voice to a new note without a new attack */
    void (*legato)(struct track *t, voice_t *v);
    /* optional: a key went down in MONO / LEGATO / UNISON, whether or not it takes the voice */
    void (*mono_key)(struct track *t, uint32_t note);
    /* optional: the part's block after its voices (FM6: Dexed's DC filter); nr: voices rendered */
    void (*post)(struct track *t, int32_t *out, uint32_t n, uint32_t nr);
} engine_t;

/* ------------------------------------------------------------ track --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
/* (bits 2..6: chance.c) a NOTE step whose notes already sounding go on, not struck again; the others sounding end at
 * its start (POLY: notes recorded over each other, seq.c rec_carry) */
#define SF_CARRY 0x80u
/* the dynamics of a note or a drum hit (2 bits each in the steps): played without velocity
 * (OCT- / OCT+ held on the drum track, the step page) or from a MIDI velocity */
enum { LV_NORM, LV_GHOST, LV_SOFT, LV_HARD };
/* a step of a synth part as it is stored (10 bytes: projects, sections, patterns, snapshots, SLOOP 2.4): up to 4 notes
 * (POLY), time, accent, slide */
#define STEP_STORED                                                                                                        uint8_t note[4];                                                                                                       uint8_t n;                   /* notes in use, 0 = empty */                                                             uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */                                                          uint8_t flags;               /* SF_ACCENT | SF_SLIDE */                                                                uint8_t vel;                                                                                                           uint8_t lvl;                 /* 2 bits per note (note k: bits 2k..2k+1): LV_* */                                       uint8_t rat;                 /* 2 bits per note: the hits it plays in its step - 1 (ratchet x1..x4) */
typedef struct { STEP_STORED } pstep_t;
/* a step in RAM (the tracks, undo): the stored step and each note's own end (POLY notes recorded over each other, REC
 * QUANTIZE OFF): note k ends ngate[k] / 128 of the step after its start, 0 = at the step's GATE (as every note did).
 * Not in the stored step: a stored copy keeps them as events of the track's automation list (seq/ngate.c) */
typedef struct {
    STEP_STORED
    uint8_t ngate[4];
} step_t;
#define STEP_SN ((uint32_t)sizeof(pstep_t))   /* a stored step's bytes */
#define DRUM_LANES 16            /* the drum track: 16 sounds, one per white key (drums.c LANE_*) */
#if FELUCCA_USB_AUDIO
#include "../io/usb/ua_tap.h"     /* the capture channels' sources and their taps (SYSTEM > USB AUDIO) */
#endif
/* the drum track's 17th step lane, the ACCENT lane (FELUCCA_DRUM_ACCENT, drums.c dacc_vel): a lane as the others
 * (steps, level, ratchet, length, the edits, the tools, patterns, sections, snapshots), no sound of its own; its
 * steps raise every hit of the step by the drum track's ACCENT amount (P_DACC), its ratchet hits too, scaled by the accent
 * step's own level (drums.c dacc_vel); its own ratchet is kept and edited, and changes nothing. The step's chance, condition
 * and fill are the step's: an accent plays exactly when its step does. DRUM_SLANES: the step lanes */
#define DRUM_ACC 16u
#define DRUM_SLANES 17u
/* the ACCENT amount: the drum track's P_E1 (its EDIT values after the kit are free; stored with the track as amount -
 * DACC_DEF, so a project from before loads the default); velocity added to an accented hit, 0..DACC_MAX */
#define P_DACC P_E1
#define DACC_MAX 127
#define DACC_DEF 27              /* a NORM hit (velocity 100) accented plays 127: the X0X kits' own accent */
#if FELUCCA_DRUM_ACCENT && !FELUCCA_AUTO
#undef FELUCCA_DRUM_ACCENT
#define FELUCCA_DRUM_ACCENT 0    /* (the lane is stored as events of the automation store: without it, none) */
#endif
/* the Optimist UI shows the ACCENT lane (FELUCCA_UI 1 with the lane built): DRUM_UILANES lanes in its grids, its mixer, its
 * walk; the LAST upper key (key 25, the lone F# at the right end) is the lane's key (seq.c kb_drum_lane, drums.c key_of_lane, op_input.c) */
#ifndef DACC_KEY_ON
#define DACC_KEY_ON 1
#endif
#define DACC_KEY 25u
#define DRUM_UILANES (DRUM_LANES + (uint32_t)(FELUCCA_DRUM_ACCENT != 0 && DACC_KEY_ON != 0))
typedef struct {                 /* a step of the drum track (in RAM; stored: its first 10 bytes, a stored step's size) */
    uint8_t on[2];               /* bit per lane */
    uint8_t lvl[4];              /* 2 bits per lane (lane k: byte k / 4, bits 2 (k % 4)..): LV_* */
    uint8_t rat[4];              /* 2 bits per lane: ratchet */
    uint8_t acc;                 /* the ACCENT lane: bit 0 on, bits 1..2 its level, bits 3..4 its ratchet. Not in the
                                  * stored step: every stored copy keeps it as an event (seq/ngate.h, auto.h AUTO_ACC) */
} dstep_t;
#define DSTEP_SN 10u             /* a drum step's stored bytes (on, lvl, rat) */
typedef struct { uint8_t on[2], lvl[4], rat[4]; } dstep_sn_t;   /* a drum step as stored (dstep_t without its acc) */
_Static_assert(sizeof(pstep_t) == 10 && sizeof(dstep_t) == DSTEP_SN + 1u && sizeof(dstep_sn_t) == DSTEP_SN && sizeof(step_t) == 14 &&
               __builtin_offsetof(dstep_t, acc) == DSTEP_SN && NSTEP * sizeof(dstep_t) <= NSTEP * sizeof(step_t) &&
               __builtin_offsetof(step_t, ngate) == sizeof(pstep_t), "a stored step is 10 bytes on every track");

typedef struct {                 /* one DIST insert's state (fx.c dist_run): a part's now, a voice's or an FX slot's later */
    int32_t lo, l1, env;         /* the low band (dry), the tone, the peak follower (0: off, the states restart) */
} dist_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* MIDI bend and mod wheel (midi_control.c): live, never saved; Q8 semitones, targets and smoothed */
    int16_t bend_target, bend_q8, wheel_target, wheel_q8;
    uint32_t wheel_phase;        /* the wheel's own 5 Hz vibrato */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime (clock units: samples x BPM, fx.c BEAT_U) */
    uint32_t arp_pos;            /* stopped: units into the current arp step */
    uint32_t arp_abs;            /* playing: the arp step of the transport grid last played */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_new;             /* a chord just started: its first note now */
    uint8_t arp_den;             /* the RATE it last played on (a RATE change waits for the next step) */
    uint32_t arp_off;            /* units to its note-off */
    /* sequencer: synth parts step[], the drum track dstep[] (16 lanes) */
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
    uint32_t seq_abs;            /* the step of the transport grid last played (seq.c trk_grid), SEQ_NONE */
    uint16_t seq_idx;            /* its index in the pattern */
    uint8_t seq_notes[4];        /* sounding seq notes */
    uint8_t seq_n;
    uint8_t seq_nm;              /* bit i: seq_notes[i] ends at its own time, seq_noff[i] (a step's ngate) */
    uint32_t seq_noff[4];        /* units to that note's note-off */
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t seq_den;             /* the DIV it last played on (a DIV change waits for the next step) */
    uint32_t seq_off;            /* units to the note-off of the step's notes */
    uint8_t seq_active;          /* any step programmed */
    uint8_t rat_done[4];         /* ratchet hits played in this step: per note (synth) */
    uint32_t rat_lanes;          /* .. per lane (drums): 2 bits each */
    uint32_t rskip_abs;          /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[4];   /* do not trigger them again there (they sound already) */
    uint16_t rskip_lanes;        /* (the drum track: lanes) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    uint8_t rh_own;              /* rh_start held no note before: its GATE lock is the recorded note's (PLOCK) */
    int8_t rh_nudge;             /* the nudge its start was kept as (REC QUANTIZE OFF, MICRO), 1/64 of a step */
    uint8_t rh_drop;             /* POLY: a held note is up, others not: the next step carries the rest (seq.c) */
    uint8_t rh_at[4];            /* the step each of rh_note[] started in (a chord's keys share their length) */
    /* POLY, REC QUANTIZE OFF: notes up while keys of their chord were still down, each with its own end (seq.c
     * rec_own_end): the chord's last key up within 1/8 of a step after one makes them end together */
    uint8_t rt_m, rt_note[4];    /* bit i: entry i in use; the note */
    uint8_t rt_at[4];            /* its chord: the step it started in */
    uint8_t rt_l[4];             /* the step its own end is in */
    uint32_t rt_pos[4];          /* when it was up: 1/128 steps of the grid */
    uint32_t rh_last_abs;        /* (its step of the transport grid) */
    uint32_t rh_start_abs;       /* rh_start's step of the transport grid (the length lock: seq.c rec_len_end) */
    step_t rh_bak;
    uint32_t pass;               /* loops played since PLAY (a recording pass: one undo) */
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t lvl;                 /* the LEVEL gain (Q12) of the last block: a change is ramped (fx.c) */
#if FELUCCA_GLIDE
    int32_t gl_pl, gl_pr, gl_c, gl_d, gl_rv, gl_v;   /* level, pan gains, sends where they glide now (fx.c mix_part) */
    uint8_t gl_on;               /* gl_* hold a glide (0: the next block starts at the targets) */
#endif
    int32_t peak;
    dist_t dist;                 /* the DIST insert (fx.c track_dist) */
    int32_t att;                 /* mute / solo fade: attenuation, Q15 (0 = heard; fx.c mix_part, drums_mix) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    uint8_t pold;                /* SLOOP 2.6: a sound loaded, the sounding voices fade out with p_old first (voice.c) */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
#if FELUCCA_PATTERNS
    uint32_t org;                /* the grid step its pattern started on: a launched pattern from its step 1 (pat.c) */
#endif
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    volatile uint8_t solo;       /* bit per track soloed (GLO + key), 0 = none: the others are silent */
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: three parts and the drum track */
static song_t song;

/* The transport clock (seq.c runs it). One unit = one sample at 1 BPM: a beat is BEAT_U units at any
 * tempo, and every division of it (1/4 .. 1/64, the triplets) is a whole number of units. The steps
 * of every track, the arp, the rolls, the click, the SLICER and the song arranger (which counts the
 * same units) all follow it: no rounding, no drift between them, a tempo change moves them together.
 * clk_beat: beats since PLAY, clk_pos: units into that beat, at the start of the block being rendered
 * (events_block advances it by CTL x BPM after the block's events). */
#ifndef FS
#define FS 44100                 /* (as build/gen/felucca_tables.h: tools/gen_tables.py) */
#endif
#define BEAT_U ((uint32_t)FS * 60u)
#include "../system/overload.h"   /* the overload monitor: the CPU lines' maxima, DROPS, UNDERRUNS, the header's mark */
#include "../system/dual_load.h"  /* CPU1's load beside song.cpu_q8 (FELUCCA_DUAL 2 only) */
static volatile uint32_t clk_beat, clk_pos;
/* the units the clock moves in the block being rendered (events_block: n x BPM, nudged, or what an external clock
 * moved while playing; stopped: n x BPM): a synced LFO's step (voice.c lfo_inc) */
static uint32_t clk_blk;
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* N_DIV: beats = 1 / DEN */
#define NDIV_SHORT 6u            /* the divisions inside a beat (N_DIV: the arp's RATE, the delay's TIME) */
#if FELUCCA_DIV_LONG
/* SLOOP 2.4 (isod89/sloop-fm1 8d3823f): SEQ DIV (N_SDIV) + 1/2 note, a bar, two bars (whole beats: DIV_BEATS) */
#define NDIV_STEP 9u
static const uint8_t DIV_BEATS[3] = {2, 4, 8};
AINL uint32_t div_units(uint32_t div)   /* (inlined: the ISR, in RAM code, calls it) */
{
    return div < NDIV_SHORT ? BEAT_U / DIV_DEN[div] : BEAT_U * DIV_BEATS[(div - NDIV_SHORT) % 3u];
}
#else
#define NDIV_STEP 6u
static uint32_t div_units(uint32_t div) { return BEAT_U / DIV_DEN[div % 6u]; }
#endif
/* length of one division (N_DIV / N_SDIV order) in samples at the song tempo (rounded down) */
static uint32_t div_samples(uint32_t div)
{
    return div_units(div) / (uint32_t)song.g[G_BPM];
}
#if FELUCCA_DLY_DOT
/* SLOOP 2.4: the delay's TIME (N_DLY) in clock units: the N_DIV values, then 1/8 dotted, 1/16 dotted */
AINL uint32_t dly_units(uint32_t d)
{
    return d < NDIV_SHORT ? BEAT_U / DIV_DEN[d] : d == NDIV_SHORT ? BEAT_U * 3u / 4u : BEAT_U * 3u / 8u;
}
AINL uint32_t dly_samples(uint32_t d) { return dly_units(d) / (uint32_t)song.g[G_BPM]; }
#else
#define dly_samples(d) div_samples(d)
#endif
/* the sample of the block a sequenced note starts at (clock_sync.c, following an external clock): the
 * clock is at the block's end, ev_map maps positions back into it. ev_at(ago): an event due ago units before
 * the clock -> ev_ofs, read by voice_start / drum_on (the voice renders from that sample). 0 otherwise. */
static struct { uint8_t on, n; int32_t d, adv; } ev_map;     /* d: clock - position at the block's start */
static uint8_t ev_ofs;
static void ev_at(uint32_t ago)
{
    int32_t x = ev_map.d - (int32_t)ago;
    uint32_t j;
    ev_ofs = 0;
    if (!ev_map.on || ev_map.adv <= 0 || x <= 0)
        return;
    if (x > ev_map.adv)
        x = ev_map.adv;
    j = ((uint32_t)x * ev_map.n + (uint32_t)ev_map.adv - 1u) / (uint32_t)ev_map.adv;
    ev_ofs = (uint8_t)(j < ev_map.n ? j : ev_map.n - 1u);
}
#define TSEL (&trk[song.sel])    /* the selected track */
#define TDRUM (&trk[TRK_DRUM])
static int is_drum(const track_t *t) { return t == TDRUM; }
/* step i of track t as bytes (*n of them): the drum track's 16-lane step, a synth step with its own note ends */
static const uint8_t *trk_step_raw(const track_t *t, uint32_t i, uint32_t *n)
{
    *n = is_drum(t) ? (uint32_t)sizeof(dstep_t) : (uint32_t)sizeof(step_t);
    return is_drum(t) ? (const uint8_t *)&t->dstep[i % NSTEP] : (const uint8_t *)&t->step[i % NSTEP];
}
/* silent: MUTE, or another track is soloed */
AINL int trk_silent(const track_t *t)
{
    uint32_t i = (uint32_t)(t - trk);
    return t->p[P_MUTE] || (song.solo && !((song.solo >> i) & 1u));
}
/* the track's effects are heard (P_FXOFF: the bypass keeps DIST, the SLICER and the sends set, unheard) */
AINL int fx_on(const track_t *t) { return t->p[P_FXOFF] == 0; }
AINL uint32_t eng_vmax(const engine_t *e) { return e->poly && e->poly < NPOLY ? e->poly : NPOLY; }   /* the engine's voices */
uint32_t trk_nvoice(const track_t *t);   /* voice.c: the voices POLY and UNISON may use: min(VOICES, the engine's cap) */
#if FELUCCA_ANALOG2
/* ENV2's values outside a track's stored ones (P_A2ESUS .. P_A2ESDT, every one fits a signed byte): six values
 * <-> three 16-bit words, two bytes each (low byte first); all 0, the defaults, is 0 0 0 */
#define A2X_N 6u
#define A2X_W 3u
_Static_assert(P_A2ESUS + A2X_N == P_E0, "ENV2's extras: the last values before P_E0");
AINL void a2x_pack(int16_t *w, const int16_t *x)
{
    uint32_t i;
    for (i = 0; i < A2X_W; i++)
        w[i] = (int16_t)(uint16_t)((uint8_t)x[2u * i] | (uint32_t)(uint8_t)x[2u * i + 1u] << 8);
}
AINL void a2x_unpack(int16_t *x, const int16_t *w)
{
    uint32_t i;
    for (i = 0; i < A2X_W; i++) {
        x[2u * i] = (int8_t)(uint8_t)((uint16_t)w[i] & 0xFFu);
        x[2u * i + 1u] = (int8_t)(uint8_t)((uint16_t)w[i] >> 8);
    }
}
/* the one destination of ENV2 until project format 11 (DST2: FUNA's, user presets of UP_VER 1, section records
 * without SEC_V2): its amount AMT2 (P_A2FENV) went to it alone. DST2 d with amount *amt -> the amounts: CUT
 * keeps it in FENV (the FLT amount); PITCH, SHAPE, OSC2, SDTN get it (x: SUS2 REL2 PIT SHP OSC2 SDTN, the
 * other amounts 0) and FENV 0: the same sound. d out of range is clamped as a load clamped DST2 */
enum { A2E_CUT, A2E_PITCH, A2E_SHAPE, A2E_OSC2, A2E_SDTN };
AINL void a2x_from_dst(int16_t *x, int16_t *amt, int32_t d)
{
    x[2] = x[3] = x[4] = x[5] = 0;
    d = d < 0 ? 0 : d > A2E_SDTN ? A2E_SDTN : d;
    if (d != A2E_CUT) {
        x[1 + d] = *amt;                              /* (P_A2EPIT + d - 1) */
        *amt = 0;
    }
}
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* where the audio interrupt is (2026-10-15: two watchdog resets with CPU0 inside the audio render, the main loop and TIMER5
 * stopped: TIMER5 does not preempt the audio render on the FM-1): a plain store at each section (ASTAGE, the codes in
 * docs/DUAL-CORE.md / console `boot`), kept by the reset (main.c copies it to fm1_astage_prev). LOOP_GUARD: a loop whose
 * end depends on state other code writes stops after MAX passes and notes ID (fm1_aloop, the first one; main.c keeps
 * it as fm1_aloop_prev): a hang there turns into a bounded block and a record */
volatile uint32_t fm1_astage __attribute__((section(".noinit")));
volatile uint32_t fm1_aloop __attribute__((section(".noinit")));
static uint32_t fm1_astage_prev, fm1_aloop_prev;
#define ASTAGE(s) (fm1_astage = (s))
/* (code either core may run: only CPU0 writes the marker; CPU1 must not write .noinit, its write limits there) */
#if defined(FELUCCA_DUAL) && FELUCCA_DUAL >= 2 && defined(__PI32V2__) && !(defined(FELUCCA_DUALCFG) && FELUCCA_DUALCFG)
#define ASTAGE0(s) ((void)0)                       /* (the drum job's fine markers: the console test image only, their RAM
                                                    * text kept for the second core's code) */
#elif defined(FELUCCA_DUAL) && FELUCCA_DUAL >= 2 && defined(__PI32V2__)
#define ASTAGE0(s)                                                                                                     \
    do {                                                                                                               \
        if (!fm1_cnum())                                                                                               \
            fm1_astage = (s);                                                                                          \
    } while (0)
#else
#define ASTAGE0(s) ASTAGE(s)
#endif
#define LOOP_GUARD(id, cnt, max)                                                                                       \
    if (++(cnt) > (max)) {                                                                                             \
        if (!fm1_aloop)                                                                                                \
            fm1_aloop = (id);                                                                                          \
        break;                                                                                                         \
    }
static uint32_t cpu_khz;          /* the CPU clock measured at boot (main.c, hal/fm1_clock.h), 0 = none */
/* Two early failed boots -> USB recovery; recovery reset -> mask-ROM UBOOT. */
#include "../system/bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));
