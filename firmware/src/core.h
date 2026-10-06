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
#ifndef FELUCCA_DRUM_KITS
#define FELUCCA_DRUM_KITS 1      /* user drum kits: a bank of 16 in the data flash, after the factory kits */
#endif
#ifndef FELUCCA_DRUM_SENDS
#define FELUCCA_DRUM_SENDS 1     /* each drum lane's own REV / DLY / CHO sends (drum_sends.c, SOUND 3) */
#endif
#ifndef FELUCCA_ANALOG2
#define FELUCCA_ANALOG2 1        /* ANALOG 2 (eng_analog2.c): osc 2 wave / interval / sync, a filter envelope,
                                  * filter modes, drift, SUPER's swarm (SUPER is no engine of its own then: FM6
                                  * is 9); 0 = the original ANALOG and SUPER (and project format 5) */
#endif
#include "registry.h"     /* NENGINES, the engines' UIDs (FUN7 numbers: ANALOG 0 .. FM6 9, SLICE 10) and switches */
#include "cpuguard.h"     /* the CPU guard's level, read by the render (FELUCCA_CPU_GUARD; else the constant 0) */
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
#define AINL static inline __attribute__((always_inline))
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

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: the sound's level trim (1/2 dB), set by the presets so
                                                * every factory sound comes out as loud as the others (fx.c) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
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
    /* ANALOG 2's pages OSC 2, SWARM, FLT 2 and ENV2 (eng_analog2.c; shown on an ANALOG track only, params.c
     * page_shown): osc 2's wave (0 = as osc 1), its interval, hard sync; drift (also a free-running phase);
     * the filter's mode; ENV2, an ADSR of its own: attack, decay, its amount (FATK, FDEC, FENV: the AD
     * envelope of the cutoff it was), sustain, release (0: the decay's time) and its destination (0: the
     * cutoff); the swarm (copies of osc 1, SUPER's superwave: 0 = none) and its spread. Their defaults
     * leave the sound as before */
    P_A2WAVE, P_A2SEMI, P_A2SYNC, P_A2DRFT,
    P_A2FTYP, P_A2FATK, P_A2FDEC, P_A2FENV,
    P_A2SWRM, P_A2SDTN,
    P_A2ESUS, P_A2EREL, P_A2EDST,
#endif
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

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
    G_COUNT
};
#if FELUCCA_MISSING_WARN
#define G_MISS G_COUNT          /* TOOLS > MISS: what the loads use and this build lacks (miss.c); no stored value */
static volatile uint8_t miss_gen;   /* + 1 per load (proj_apply, also in the audio ISR; a user preset / kit) */
#define MISS_BUMP() (miss_gen++)
#else
#define MISS_BUMP() ((void)0)
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
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide: 1/4096 semitone per control tick, 0 = RATE (GLIDE: the time an octave takes) */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
} voice_t;

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
    uint8_t macro[4];            /* HOME: the four parameters on KNOB 1..4 */
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
/* the dynamics of a note or a drum hit (2 bits each in the steps): played without velocity
 * (OCT- / OCT+ held on the drum track, the step page) or from a MIDI velocity */
enum { LV_NORM, LV_GHOST, LV_SOFT, LV_HARD };
typedef struct {                 /* a step of a synth part (10 bytes): up to 4 notes (POLY), time, accent, slide */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
    uint8_t lvl;                 /* 2 bits per note (note k: bits 2k..2k+1): LV_* */
    uint8_t rat;                 /* 2 bits per note: the hits it plays in its step - 1 (ratchet x1..x4) */
} step_t;
#define DRUM_LANES 16            /* the drum track: 16 sounds, one per white key (drums.c LANE_*) */
typedef struct {                 /* a step of the drum track (10 bytes, the size of a step_t) */
    uint8_t on[2];               /* bit per lane */
    uint8_t lvl[4];              /* 2 bits per lane (lane k: byte k / 4, bits 2 (k % 4)..): LV_* */
    uint8_t rat[4];              /* 2 bits per lane: ratchet */
} dstep_t;
_Static_assert(sizeof(step_t) == 10 && sizeof(dstep_t) == 10, "a step is 10 bytes on every track");

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
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
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
    uint32_t rh_last_abs;        /* (its step of the transport grid) */
    step_t rh_bak;
    uint32_t pass;               /* loops played since PLAY (a recording pass: one undo) */
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t lvl;                 /* the LEVEL gain (Q12) of the last block: a change is ramped (fx.c) */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    int32_t att;                 /* mute / solo fade: attenuation, Q15 (0 = heard; fx.c mix_part, drums_mix) */
    uint8_t dist_on;             /* DIST was on in the last block (its states restart when it comes on) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
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
static volatile uint32_t clk_beat, clk_pos;
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* N_DIV: beats = 1 / DEN */
static uint32_t div_units(uint32_t div) { return BEAT_U / DIV_DEN[div % 6u]; }
/* length of one division (N_DIV order) in samples at the song tempo (rounded down) */
static uint32_t div_samples(uint32_t div)
{
    return div_units(div) / (uint32_t)song.g[G_BPM];
}
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
/* silent: MUTE, or another track is soloed */
AINL int trk_silent(const track_t *t)
{
    uint32_t i = (uint32_t)(t - trk);
    return t->p[P_MUTE] || (song.solo && !((song.solo >> i) & 1u));
}
/* the track's effects are heard (P_FXOFF: the bypass keeps DIST, the SLICER and the sends set, unheard) */
AINL int fx_on(const track_t *t) { return t->p[P_FXOFF] == 0; }
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
static uint32_t cpu_khz;          /* the CPU clock measured at boot (main.c, hal/fm1_clock.h), 0 = none */
/* Two early failed boots -> USB recovery; recovery reset -> mask-ROM UBOOT. */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));
