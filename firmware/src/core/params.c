/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
/* RATE below 0 (voice.c lfo_inc): one cycle per note value of the tempo, longest first (core.h LSYNC_N, LRATE_OF_SLOOP) */
static const char *const N_LSYNC[LSYNC_N] = {"4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const char *const N_L2TRIG[] = {"NOTE", "FREE"};   /* LFO 2 (SLOOP 2.6's LFO 2 TRIG, POL) */
static const char *const N_L2POL[] = {"BI", "UNI"};
/* a modulation slot's DEST (core.h LT_*: LFO's, LFO 2's, ENV's, ENV2's) */
static const char *const N_LT[LT_N] = {"OFF", "PIT", "FLT", "SHP", "AMP", "RES", "PAN", "LVL", "ATK", "DEC",
                                       "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "CHO", "DLY", "REV", "INS1",
                                       "INS2", "L1 RT", "L1 AMT", "SUS", "REL", "ATK2", "DEC2", "SUS2", "REL2", "E1 AMT",
                                       "E2 AMT", "OSC2", "SDTN", "L2 RT", "L2 AMT"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
#if FELUCCA_DIV_LONG                 /* SLOOP 2.4 (8d3823f): appended, so the old values keep their index (core.h div_units) */
static const char *const N_SDIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1BAR", "2BAR"};
#else
#define N_SDIV N_DIV
#endif
#if FELUCCA_DLY_DOT                  /* SLOOP 2.4: 1/8 and 1/16 dotted, appended (core.h dly_units) */
static const char *const N_DLY[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/8D", "1/16D"};
#else
#define N_DLY N_DIV
#endif
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
#if FELUCCA_QNT_SEQ
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "ALL", "SEQ"};   /* .. Q_SEQ (qnt_seq.c) */
#define Q_LAST Q_SEQ
#else
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "ALL"};   /* Q_OFF .. Q_ALL (seq.c scale_map) */
#define Q_LAST Q_ALL
#endif
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON", "AUTO"};   /* G_CLOCK is the metronome (seq.c click_tick; AUTO: while nothing else plays) */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SYNC[] = {"INT", "USB", "TRS", "AUTO"};   /* SYNC_* (core.h); AUTO: TRS, USB, INT */
static const char *const N_CLKSRC[] = {"INT", "USB", "TRS"};         /* G_MIDI: the clock followed (MSRC_*) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
#if INS_FX || FELUCCA_USB_AUDIO                      /* (fx_slots.c UT_DESC, the USB input slots, names them too) */
static const char *const N_TJMODE[] = {"I", "II", "I+II"};   /* JUNO CHORUS's MODE (juno_cho.h) */
static const char *const N_TGPAT[] = {"16TH", "8TH", "4TH", "HALF", "OFF8", "OFF16", "DOT8", "DOT4",   /* GATER's */
                                      "TRNC1", "TRNC2", "TRNC3", "TRNC4", "GALOP", "RVGAL", "CLAVE", "TRESL"};   /* IGT_PAT */
#endif
static const char *const N_CHORD[] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};   /* seq.c CHORD_DEG */
static const char *const N_FXOFF[] = {"ON", "OFF"};                  /* P_FXOFF: 0 = the effects heard */
static const char *const N_VIEW[] = {"PAGE", "ALL"};                  /* G_VIEW */
static const char *const N_ROLL[] = {"1/8", "1/16", "1/32", "32T", "1/64"};   /* seq.c ROLL_DEN */
/* the master COMP / LIMIT (master_comp.c MC_RATIO, MC_ATK, MC_REL, MC_CEIL: the same order) */
static const char *const N_CRAT[] = {"1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "20:1", "INF"};
static const char *const N_CATK[] = {"0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms"};
static const char *const N_CREL[] = {"50ms", "100ms", "200ms", "300ms", "600ms", "1.2s", "AUTO"};
/* a track's DYN GAIN (fx_slots.c TDYN_GC, the track value P_TDGAIN) and EQ3's ranges and value tags (fx_slots.c fxs_fmt
 * formats a value by its tag: dB with KILL, the crossovers' Hz) */
static const char *const N_TDGAIN[] = {"AUTO", "0dB", "1dB", "2dB", "3dB", "4dB", "5dB", "6dB", "7dB", "8dB", "9dB",
                                       "10dB", "11dB", "12dB", "13dB", "14dB", "15dB"};
#define EQ_KILL (-31)
#define EQ_FL_DEF 32                                    /* 250 Hz */
#define EQ_FH_DEF 28                                    /* 2.5 kHz */
#define EQ_FMAX 60
static const char EQ_TAG_DB[] = "dB", EQ_TAG_FL[] = "Hz", EQ_TAG_FH[] = "Hz";
static const char *const N_CCEIL[] = {"OFF", "-0.1", "-0.3", "-0.5", "-1.0", "-2.0", "-3.0", "-6.0"};
#if FELUCCA_ANALOG2
static const char *const N_A2WAVE[] = {"=1", "SAW", "SQR", "TRI", "SIN", "PWM"};   /* eng_analog2.c: =1 osc 1's */
static const char *const N_A2FTYP[] = {"LP12", "LP24", "BP", "HP"};
static const char *const N_A2EREL[] = {"=DEC"};   /* REL2 0: the release takes DEC2's time (param_format) */
#endif
static const char *const N_ENGNAME[] = {"ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
#if !FELUCCA_ANALOG2
                                             "SUPER",
#endif
#if FELUCCA_SLICE
                                             "SLICE",
#endif
};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),    /* ENV 1 MOD's FLT and SHP depths (env1_cards.h; voice.c: env x d >> 7) */
    [P_ED_PIT] = PD("DROP", F_BIPCT, -64, 63, 0),   /* the pitch drop: a fast fall from each note's start (voice.c penv) */
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("TRIM", F_INT, -64, 63, 0),   /* the sound's level trim, 1/2 dB (presets set it) */
    [P_LRATE] = PD("RATE", F_LFOHZ, -LSYNC_N, 127, 60),   /* (< 0: synced, N_LSYNC) */
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_L1DA] = {"DST A", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},   /* LFO DEST: two slots (core.h LT_*) */
    [P_L1AA] = PD("AMT A", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),   /* (2 = a depth of 1 of before; AMP: 1) */
    [P_L1DB] = {"DST B", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},
    [P_L1AB] = PD("AMT B", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_SDIV, 2),
    [P_SSWING] = PD("SWG", F_SWING, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_CHORD] = PE("CHORD", N_CHORD, 0),
    [P_FXOFF] = PE("FX", N_FXOFF, 0),
#if FELUCCA_ANALOG2
    [P_A2WAVE] = PE("WAVE2", N_A2WAVE, 0),
    [P_A2SEMI] = PD("SEMI", F_SEMI, -24, 24, 0),
    [P_A2SYNC] = PE("SYNC", N_ONOFF, 0),
    [P_A2DRFT] = PD("DRFT", F_PCT, 0, 127, 0),
    [P_A2FTYP] = PE("FTYP", N_A2FTYP, 0),
    [P_A2FATK] = PD("ATK2", F_TIME, 0, 127, 0),     /* ENV2 (was the filter envelope's FATK FDEC FENV) */
    [P_A2FDEC] = PD("DEC2", F_TIME, 0, 127, 64),
    [P_A2FENV] = PD("FLT", F_BIPCT, -64, 63, 0),     /* ENV2 DEST: its amount on the cutoff (was AMT2) */
    [P_A2SWRM] = PD("SWARM", F_INT, 0, 6, 0),       /* copies of osc 1 (eng_analog2.c a2_copies) */
    [P_A2SDTN] = PD("SDTN", F_PCT, 0, 127, 34),      /* their spread (SUPER's SDTN) */
    [P_A2ESUS] = PD("SUS2", F_PCT, 0, 127, 0),
    [P_A2EREL] = {"REL2", F_TIME, 0, 127, 0, N_A2EREL, 0},
    [P_A2EPIT] = PD("PIT", F_BIPCT, -64, 63, 0),     /* ENV2 DEST: both oscillators and the swarm, +-31.5 st */
    [P_A2ESHP] = PD("SHP", F_BIPCT, -64, 63, 0),     /*   PW, the sync sweep (as ENV DEST SHP) */
    [P_A2EOS2] = PD("OSC2", F_BIPCT, -64, 63, 0),    /*   osc 2's pitch alone, +-31.5 st */
    [P_A2ESDT] = PD("ENV2", F_BIPCT, -64, 63, 0),    /*   the swarm's spread, +-63 (on the SWARM page) */
#endif
#if SL24_TP
    /* SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f), its ranges and defaults (params.c) */
    [P_TFLT] = PD("FILT", F_FILT, -64, 63, 0),     /* the track's filter: < 0 low-pass, > 0 high-pass (fx.c) */
    [P_STRUM] = {"STRUM", F_INT, -60, 60, 0, 0, "ms"},   /* ms a note: > 0 low to high, < 0 high to low (voice.c) */
    [P_VLEAD] = PE("VLEAD", N_ONOFF, 0),           /* each chord voiced nearest the last one (seq.c) */
#endif
#if FELUCCA_MASTER_COMP
    [P_TCOMP] = PD("DYN", F_PCT, 0, 127, 0),       /* the DYN insert (fx_slots.c): 0 off, threshold up to -30 dB */
    [P_TDRAT] = PE("RATIO", N_CRAT, 1),            /* its settings (2026-10-18: track values): as the master COMP's */
    [P_TDATK] = PE("ATK", N_CATK, 4),
    [P_TDREL] = PE("REL", N_CREL, 6),
    [P_TDGAIN] = PE("GAIN", N_TDGAIN, 0),          /*   AUTO: the make-up from the threshold */
    [P_TDCEIL] = PE("CEIL", N_CCEIL, 0),           /*   OFF: no limiter */
#endif
#if INS_FX
    /* the further insert types' amounts (fx/insfx/insfx.c): each 0 off .. 127; the label names the type's slot */
    [P_TFOLD] = PD("FOLD", F_PCT, 0, 127, 0),      /* the drive into the folder */
    [P_TFUZZ] = PD("FUZZ", F_PCT, 0, 127, 0),      /* the drive into the fuzz */
    [P_TPHSR] = PD("PHSR", F_PCT, 0, 127, 0),      /* the phaser: its mix to 64, then its feedback */
    [P_TFLNG] = PD("FLNG", F_PCT, 0, 127, 0),      /* the flanger: its mix to 64, then its feedback */
    [P_TFRMT] = PD("FRMT", F_PCT, 0, 127, 0),      /* the vowel: A E I O U from 1 to 127 */
    [P_TWIDE] = PD("WIDE", F_PCT, 0, 127, 0),      /* the width: the swept side's share */
    [P_TCHRS] = PD("CHRS", F_PCT, 0, 127, 0),      /* the chorus insert (fxi.c): its mix to 64, then its depth */
    [P_TECHO] = PD("ECHO", F_PCT, 0, 127, 0),      /* the slapback insert (fxi.c): its level to 64, then its feedback */
    [P_TGATE] = PD("GATER", F_PCT, 0, 127, 0),     /* GATER (insfx/gater.c): its DEPTH, 127 a closed step silent */
    [P_TGPAT] = PE("PAT", N_TGPAT, 8),             /*   its pattern (gater.c IGT_PAT), TRNC1 */
    [P_TGRATE] = PE("RATE", N_SLDIV, 1),           /*   a step: 1/16 */
    [P_TGSHP] = PD("SHAPE", F_PCT, 0, 127, 0),     /*   hard .. smooth */
    /* the shapers (2026-10-18, insfx.c ifx_drive ifx_mod) */
    [P_TSFLD] = PD("SFLD", F_PCT, 0, 127, 0),      /* the drive into the sine folder */
    [P_TRECT] = PD("RECT", F_PCT, 0, 127, 0),      /* the rectified share, then its drive */
    [P_THARM] = PD("HARM", F_PCT, 0, 127, 0),      /* the drive into v + v^2/2 + v^3/4 */
    [P_TRING] = PD("RING", F_PCT, 0, 127, 0),      /* the ring's sine: 26 Hz .. 2.6 kHz */
    [P_TSUBO] = PD("SUB", F_PCT, 0, 127, 0),       /* the octave down's level */
    [P_TSLEW] = PD("SLEW", F_PCT, 0, 127, 0),      /* the slew limit: light .. heavy */
#endif
#if FELUCCA_FX_DUST
    [P_TDUST] = PD("DUST", F_PCT, 0, 127, 0),      /* the DUST insert (fx.c dust_run): a track's lo-fi / vinyl */
#endif
#if FELUCCA_INS_JCHORUS
    [P_TJCHO] = PD("JUNO", F_PCT, 0, 127, 0),      /* JUNO CHORUS (fxi_kern.c): its MIX, 127 the Juno's dry / wet */
    [P_TJMODE] = PE("MODE", N_TJMODE, 0),          /*   its MODE: I, II, I+II */
#endif
#if FELUCCA_INS_EQ3
    [P_TEQ] = PE("EQ3", N_ONOFF, 1),               /* EQ3 (insfx/eq3.c): on (its bands: the EQ3 pages) */
#endif
    [P_L2RATE] = PD("RATE", F_LFOHZ, -LSYNC_N, 127, 60),   /* LFO 2 (voice.c lfo2_tick): as LFO's RATE (< 0 synced) */
    [P_L2WAVE] = PE("WAVE", N_LWAVE, 0),
    [P_L2TRIG] = PE("TRIG", N_L2TRIG, 0),
    [P_L2POL] = PE("POL", N_L2POL, 0),
    [P_L2DA] = {"DST A", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},   /* its two slots, as LFO's */
    [P_L2AA] = PD("AMT A", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),
    [P_L2DB] = {"DST B", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},
    [P_L2AB] = PD("AMT B", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),
    [P_E1PIT] = PD("PIT", F_BIPCT, -64, 63, 0),     /* ENV 1 MOD (env1_cards.h): its PIT depth, +-31.5 st at full */
    [P_E1X] = PD("MOVE1", F_BIPCT, -64, 63, 0),     /*   its moves of an EDIT value (named per engine: E1_LBL) */
    [P_E1Y] = PD("MOVE2", F_BIPCT, -64, 63, 0),
    [P_E1Z] = PD("MOVE3", F_BIPCT, -64, 63, 0),
#if FELUCCA_ANALOG2
    [P_N2DA] = {"DST A", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},   /* ENV2 DEST: two slots (a new sound, INIT, a preset: A on the engine's FLT at 0, env2_dest_def) */
    [P_N2AA] = PD("AMT A", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),
    [P_N2DB] = {"DST B", F_ENUM, 0, LT_NB - 1, 0, N_LT, 0},
    [P_N2AB] = PD("AMT B", F_BIPCT, LFO_AMT_LO, LFO_AMT_HI, 0),
#endif
    [P_VOICES] = PD("VOICES", F_INT, 0, NPOLY, 0),         /* POLY / UNISON voice count (voice.c trk_nvoice); 0 = the engine's max (track_desc: 1..max shown) */
};
#include "lfo_store.h"                                /* LFO DEST's slots <-> the depths of before (stored form) */
#include "env_store.h"                                /* ENV2 DEST's slots <-> the depths of before; ENV DEST's fold;
                                                       * ENV 1 MOD's cards (env1_cards.h) */
/* ENV 1 MOD's cards (env1_cards.h) as the page shows them, the selected part's engine's: an alias the engine's EDIT
 * value itself; a depth -64..63 named as the EDIT value it moves or FLT PIT SHP (in the pool, made when asked) */
static param_desc_t e1_rd[4] IN_POOL;
static __attribute__((noinline)) const param_desc_t *e1_card_desc(uint32_t e, uint32_t k)
{
    const param_desc_t *ed = ENGINES[e]->edit;
    param_desc_t *d = &e1_rd[k & 3u];
    uint32_t l = ENV1_CARDS[e].l[k & 3u], c = ENV1_CARDS[e].c[k & 3u];
    if (E1K_KIND(c) == 0x50u)
        return &ed[E1K_ARG(c)];
    d->fmt = F_BIPCT, d->min = -64, d->max = 63;
    d->label = l < 8u ? ed[l].label : l == E1L_FLT ? "FLT" : l == E1L_PIT ? "PIT" : "SHP";
    return d;
}
/* An automation event (a motion, a lock) on a depth of before: LFO DEST's PIT FLT SHP AMP (P_LEG_PIT .. P_LEG_AMP, stored
 * before LFO DEST had slots, or SLOOP 2.4's), ENV2's FLT PIT SHP OSC2 SDTN (P_A2FENV, P_A2EPIT ..; before 2026-10-14).
 * ENV DEST's FLT SHP (P_ED_FLT P_ED_SHP) are ENV 1 MOD's depths again (2026-10-16): an event on them is their own.
 * leg_first: the slots it went to (P_L1DA, P_N2DA; P_COUNT: not such an id), *d its DEST */
static uint32_t leg_first(uint32_t id, uint32_t *d)
{
    if (id >= P_LEG_PIT && id <= P_LEG_AMP)
        return *d = LT_PIT + (id - P_LEG_PIT), P_L1DA;
#if FELUCCA_ANALOG2
    if (id == P_A2FENV)
        return *d = LT_FLT, P_N2DA;
    if (id >= P_A2EPIT && id <= P_A2ESDT)
        return *d = ENV2_LEG_DEST[1u + id - P_A2EPIT], P_N2DA;
#endif
    return P_COUNT;
}
#define LEG_ID(id) ({ uint32_t d_; leg_first((id), &d_) < P_COUNT; })
/* such an event of part t at *v -> the AMT of t's slot on that destination (of the modulator it went to), *v in AMT units
 * (2 x; LFO's AMP as it is). No slot on it: a slot that does nothing (OFF or at 0; not one in *taken, the slots this
 * step's events took: bits 0 1 LFO's, 2 3 ENV's, 4 5 ENV2's) is put on that destination for the event (*did: its DEST id,
 * P_COUNT none), as the depth of before acted there; none: P_COUNT (not played). *dest: the destination */
static uint32_t lfo_leg_ev(const track_t *t, uint32_t id, int32_t *v, uint32_t *taken, uint32_t *did, uint32_t *dest)
{
    uint32_t d, k, f = leg_first(id, &d), sh = f == P_L1DA ? 0u : 4u;
    *did = P_COUNT;
    *dest = d;
    if (f >= P_COUNT)
        return P_COUNT;
    *v = f == P_L1DA && d == LT_AMP ? clamp(*v, 0, 127) : 2 * clamp(*v, -64, 63);
    for (k = 0; k < 2u; k++)
        if ((uint32_t)t->p[f + 2u * k] == d) {
            *taken |= 1u << (sh + k);
            return f + 2u * k + 1u;
        }
    for (k = 0; k < 2u; k++)
        if (!((*taken >> (sh + k)) & 1u) && (t->p[f + 2u * k] == LT_OFF || !t->p[f + 2u * k + 1u])) {
            *taken |= 1u << (sh + k);
            *did = f + 2u * k;
            return f + 2u * k + 1u;
        }
    return P_COUNT;
}
/* a preset's extra parameters (preset_t.x) into p, each clamped to its range; the depths of before (P_LEG_*: the
 * engines' tables) into LFO DEST's slots */
static void preset_extras(int16_t *p, const preset_t *pr)
{
    int16_t d[4] = {0, 0, 0, 0};
    uint32_t i, leg = 0;
    for (i = 0; i + 1u < 8u; i += 2u) {
        int32_t id = pr->x[i] - 1;
        if (id >= P_LEG_PIT && id <= P_LEG_AMP)
            d[id - P_LEG_PIT] = pr->x[i + 1u], leg = 1;
        else if (id >= 0 && id < P_E0)
            p[id] = (int16_t)clamp(pr->x[i + 1u], TP[id].min, TP[id].max);
    }
    if (leg)
        (void)lfo_from_depths(d, &p[P_L1DA]);
}
#if FELUCCA_ANALOG2
/* ANALOG 2's values of ANALOG preset pi (eng_analog.c A2_PX) into p, clamped; after preset_extras */
static void analog2_extras(int16_t *p, const engine_t *e, uint32_t pi)
{
    uint32_t i;
    if (ENG_IS(e, ANALOG))
        for (i = 0; i < sizeof A2_PX / sizeof A2_PX[0]; i++)
            if ((uint32_t)A2_PX[i][0] == pi)
                p[A2_PX[i][1]] = (int16_t)clamp(A2_PX[i][2], TP[A2_PX[i][1]].min, TP[A2_PX[i][1]].max);
}
/* SUPER's presets 0..4 are ANALOG's 16..20 now, in the same order (eng_analog.c; tests/project_test.c) */
#define A2_SUPER0 16u
/* a sound that played SUPER (p: today's layout, SUPER's eight values in P_E0..P_E7; spi: its SUPER preset)
 * -> ANALOG on the swarm: the ANALOG version of that preset (engine values, ANALOG 2's own), then the SUPER
 * values that have a home there: SUPR -> SWARM, SDTN, DRFT, CUT, RES, FTYP (MIX and SUB stay the preset's:
 * osc 2 plays SUPER's sub). Returns the ANALOG preset. Projects (project.c) and user presets (upreset.c) */
static uint32_t analog2_from_super(int16_t *p, uint32_t spi)
{
    int16_t sv[8];
    uint32_t k, pi = A2_SUPER0 + (spi < 5u ? spi : 0u);
    const preset_t *pr;
    if (!ENG_HAS(ANALOG))                       /* (ANALOG not built: the part keeps SUPER's values, ANALOG's UID) */
        return pi;
    pr = &ENG_ANALOG.presets[pi];
    memcpy(sv, &p[P_E0], sizeof sv);
    for (k = 0; k < 8u; k++)
        p[P_E0 + k] = pr->e[k];
    analog2_extras(p, &ENG_ANALOG, pi);
    p[P_A2SWRM] = sv[0];
    p[P_A2SDTN] = sv[1];
    p[P_A2DRFT] = sv[3];
    p[P_E4] = sv[5];
    p[P_E5] = sv[6];
    p[P_A2FTYP] = sv[7];
    return pi;
}
#else
#define analog2_extras(p, e, pi) ((void)0)
#endif


static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 90),
    [G_SWING] = PD("SWING", F_SWING, 0, 100, 0),
    [G_CLOCK] = PE("CLICK", N_CLICK, 0),            /* (the old CLK slot: projects keep their format) */
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DLY, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("CLK", N_CLKSRC, 0),           /* read-only: the clock followed (the device shows USB status) */
    [G_SYNC] = PE("SYNC", N_SYNC, SYNC_AUTO),     /* MIDI clock to follow (clock_sync.c); old projects: 0 = INT */
    [G_VIEW] = PE("VIEW", N_VIEW, 1),            /* (the old ROUT slot) PAGE: one page, ALL: the family (ui_overview.c) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, FELUCCA_SECTIONS, 1),   /* (A..D, A..H or A..P: registry.h) */
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 0, 16, 10),            /* GM drum part MIDI channel, 0 = off */
    [G_DRLVL] = PD("LVL", F_INT, 0, 127, 100),
    [G_DRREV] = PD("REV", F_INT, 0, 127, 16),        /* retired (no page): a project's DRREV_MOVED, older ones the
                                                       * TRK lanes' REV (drum_sends.c); the default 16 decodes old
                                                       * song sections (sec_codec.c) */
    [G_DUST] = PD("DUST", F_PCT, 0, 127, 0),
    [G_DUCK] = PD("DUCK", F_PCT, 0, 127, 0),
    [G_FILT] = PD("FILT", F_FILT, -64, 63, 0),
    [G_ROLL] = PE("ROLL", N_ROLL, 1),
    [G_NEWPRJ] = PE("NEW", N_GO, 0),
    [G_CTHR] = {"THRS", F_INT, -30, 0, 0, 0, "dB"},   /* 0: OFF (param_format); the master bus' dB re full scale */
    [G_CRAT] = PE("RATIO", N_CRAT, 1),
    [G_CATK] = PE("ATK", N_CATK, 4),
    [G_CREL] = PE("REL", N_CREL, 6),
    [G_CGAIN] = {"GAIN", F_INT, 0, 15, 0, 0, "dB"},  /* make-up, after the compressor */
    [G_CCEIL] = PE("CEIL", N_CCEIL, FELUCCA_MASTER_COMP ? 6 : 0),   /* dB below the output's full scale; OFF: the old
                                                       * limiter. The default -3.0 dB (2026-10-11, the user's ruling: our
                                                       * CEIL as the default limiter; project.c mc_pack keeps OFF as its 0).
                                                       * A build without the limiter: OFF (miss.c says a set CEIL is not
                                                       * heard; a new project has none) */
    [G_RECQ] = PE("QUANT", N_ONOFF, 1),              /* REC QUANTIZE: ON snaps the recorded notes (seq.c rec_exact) */
    [G_CRET] = PD("CRET", F_PCT, 0, 127, 127),       /* the FX returns (core.h): CHO DLY REV, 127 unity, 0 off */
    [G_DRET] = PD("DRET", F_PCT, 0, 127, 127),
    [G_RRET] = PD("RRET", F_PCT, 0, 127, 127),
};

static const param_desc_t DRUM_KIT_DESC = {"KIT", F_ENUM, 0, (int16_t)(DRUM_KITS - 1u), 0, DRUM_KIT_NAMES, 0};   /* (drums.c) */
/* the drum track's ACCENT amount (core.h P_DACC: its P_E1), velocity added to an accented hit (drums.c dacc_vel) */
static const param_desc_t DRUM_ACC_DESC = {"ACCENT", F_INT, 0, DACC_MAX, DACC_DEF, 0, 0};
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (is_drum(t) && id == P_DACC)
        return &DRUM_ACC_DESC;
    if (id == P_VOICES) {                             /* 1 .. the engine's voices (the engine asked for), never 0 shown */
        static const param_desc_t VC[NPOLY] = {
            PD("VOICES", F_INT, 1, 1, 1), PD("VOICES", F_INT, 1, 2, 2), PD("VOICES", F_INT, 1, 3, 3), PD("VOICES", F_INT, 1, 4, 4),
            PD("VOICES", F_INT, 1, 5, 5), PD("VOICES", F_INT, 1, 6, 6), PD("VOICES", F_INT, 1, 7, 7), PD("VOICES", F_INT, 1, 8, 8)};
        return &VC[eng_vmax(ENGINES[t->eng_req % NENGINES]) - 1u];
    }
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* a knob's steps on value v: clamped to the range; "-" (a value not shown) does not move. (With FM6 built: its EDIT 2
 * has "-" values only, so FM6's "-" values are hidden; page_shown leaves that page out. Other engines' "-" cells stay
 * as they were: dim) */
#if FELUCCA_ENG_FM6
static int param_hidden(const param_desc_t *d)          /* one of FM6's EDIT values that is "-" (a range the page does not show) */
{
    uint32_t e;
    if (!d->label)
        return 1;
    if (d->label[0] != '-')
        return 0;
    for (e = 0; e < NENGINES; e++)
        if (ENG_IS(ENGINES[e], FM6) && d >= ENGINES[e]->edit && d < ENGINES[e]->edit + 8)
            return 1;
    return 0;
}
#define PARAM_HIDDEN(d) param_hidden(d)
#define param_step0(d, v, steps) (PARAM_HIDDEN(d) ? (v) : clamp((v) + (steps), (d)->min, (d)->max))
#else
#define PARAM_HIDDEN(d) 0
#define param_step0(d, v, steps) clamp((v) + (steps), (d)->min, (d)->max)
#endif

#define is_lrate(d) ((d) == &TP[P_LRATE] || (d) == &TP[P_L2RATE])   /* an LFO's RATE (LFO 2's too) */
/* the LFO's RATE turned (steps detents, accelerated): a fast turn down stops at 0 first, the slowest free rate; below 0
 * (the synced note values, N_LSYNC) one value a detent, whatever the speed; above 0 as every value */
static int32_t lrate_step(const param_desc_t *d, int32_t v, int32_t steps)
{
    if (!steps || (v >= 0 && v + steps >= 0))
        return clamp(v + steps, d->min, d->max);
    return v > 0 ? 0 : clamp(v + (steps < 0 ? -1 : 1), d->min, d->max);
}

#if FELUCCA_DIV_ORDER
/* #48 (after Felucca 1.0.2, hugelton/Felucca db70550, params.c DIV_ORDER / enum_rank / param_turn, by Leo
 * Kuroshita, GPL-3.0-only): the note divisions in the order of their length, longest first (the triplets between
 * their neighbours), on the knobs and the gauges; the stored values (N_DIV, N_SLDIV indices: projects, presets,
 * the editor protocol) stay. N_ROLL is in that order already. Index: the place shown, entry: the value */
static const uint8_t DIV_ORDER[6] = {0, 1, 4, 2, 5, 3};     /* 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t SLDIV_ORDER[6] = {0, 3, 1, 4, 2, 5};   /* 1/8 8T 1/16 16T 1/32 32T */
#if FELUCCA_DIV_LONG
static const uint8_t SDIV_ORDER[9] = {8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 2BAR 1BAR 1/2 1/4 1/8 8T 1/16 16T 1/32 */
#endif
#if FELUCCA_DLY_DOT
static const uint8_t DLY_ORDER[8] = {0, 6, 1, 7, 4, 2, 5, 3};       /* 1/4 1/8D 1/8 1/16D 8T 1/16 16T 1/32 */
#endif
static const uint8_t *enum_order(const param_desc_t *d)
{
#if FELUCCA_DIV_LONG
    if (d->names == N_SDIV)
        return SDIV_ORDER;
#endif
#if FELUCCA_DLY_DOT
    if (d->names == N_DLY)
        return DLY_ORDER;
#endif
    return d->names == N_DIV ? DIV_ORDER : d->names == N_SLDIV ? SLDIV_ORDER : 0;
}
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the order shown (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
static int32_t param_step(const param_desc_t *d, int32_t v, int32_t steps)   /* (a division: over the order) */
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    return is_lrate(d) ? lrate_step(d, v, steps) : param_step0(d, v, steps);
}
#else
#define enum_rank(d, v) (v)
#define param_step(d, v, steps) (is_lrate(d) ? lrate_step(d, v, steps) : param_step0(d, v, steps))
#endif

static uint32_t ins_amt(uint32_t k, uint32_t s);     /* (fx_slots.c, included below) */
/* a slot's DEST v (LT_E1 ..: not a plain name) as val (<= 6 chars): INS1 INS2 the type the selected track's slot S1 / S2
 * holds, named as INSERTS does ("FILT", "DST"; empty: "S1 --"); E1 .. E8 the selected part's engine's own name for
 * that EDIT value ("E1" when it names none); the others their names (N_LT) */
static void lt_text(const param_desc_t *d, int32_t v, char *val)
{
    const char *s = N_LT[v];
    (void)d;
    if (v == LT_INS1 || v == LT_INS2) {               /* the insert the selected track's slot holds, as INSERTS names it */
        uint32_t id = ins_amt((uint32_t)(TSEL - trk), (uint32_t)(v - LT_INS1));
        if (id < P_COUNT) {
            str_cpy(val, TP[id].label, 7);
            return;
        }
        str_cpy(val, v == LT_INS1 ? "S1 --" : "S2 --", 7);   /* (empty: INSERTS' ---- with the slot) */
        return;
    }
    if (v >= LT_E1 && v <= LT_E8) {
        const param_desc_t *e = track_desc(TSEL, P_E0 + (uint32_t)(v - LT_E1));
        if (e->label && e->label[0] && e->label[0] != '-')
            s = e->label;
    }
    str_cpy(val, s, 7);
}
/* value string (<= 5 chars) and unit for a parameter value */
#if FELUCCA_INS_EQ3
static int fxs_fmt(const param_desc_t *d, int32_t v, char *val, const char **unit);   /* (fx_slots.c: EQ3's values) */
#endif
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
#if FELUCCA_INS_EQ3
    if (fxs_fmt(d, v, val, unit))
        return;
#endif
    if (d == &GP[G_CTHR] && v == 0) {                 /* COMP > THRS at 0: the compressor is off */
        str_cpy(val, "OFF", 6);
        return;
    }
    if (d == &GP[G_SYNC] && v == SYNC_AUTO) {         /* AUTO and the clock it follows now (G_MIDI): "A:TRS" */
        static const char *const A[3] = {"A:INT", "A:USB", "A:TRS"};   /* (a column fits 5 characters, */
        str_cpy(val, A[(uint32_t)song.g[G_MIDI] % 3u], 6);              /* "AUTO:TRS" is the editor's) */
        return;
    }
    switch (d->fmt) {
    case F_PCT:                                       /* of the range: 0 .. 100 % */
        fmt_int(val, d->max > 0 ? (v * 100 + d->max / 2) / d->max : v);
        *unit = "%";
        break;
    case F_SWING:                                     /* MPC style: the share of a step pair the first one gets */
        fmt_int(val, 50 + (clamp(v, 0, 100) + 2) / 4);
        *unit = "%";
        break;
    case F_FILT:                                      /* LP 1..100 % closed, HP 1..100 % */
        if (!v) {
            str_cpy(val, "OFF", 6);
        } else {
            str_cpy(val, v < 0 ? "LP" : "HP", 6);
            fmt_int(val + 2, v < 0 ? (-v * 100 + 32) / 64 : (v * 100 + 31) / 63);
            *unit = "%";
        }
        break;
    case F_BIPCT:                                     /* (-64..63 or an LFO slot's -128..127: +-100 %) */
        fmt_int(val, (d->max > 63 ? v * 100 / 128 : v * 100 / 64));
        if (v > 0) {
            char t[8];
            fmt_int(t, (d->max > 63 ? v * 100 / 128 : v * 100 / 64));
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (d->names && v == 0) {                     /* (REL2 0: "=DEC") */
            str_cpy(val, d->names[0], 6);
            break;
        }
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (v < 0) {                                  /* (RATE: a note value of the tempo) */
            str_cpy(val, N_LSYNC[(uint32_t)(-v - 1) < LSYNC_N ? (uint32_t)(-v - 1) : 0u], 6);
            break;
        }
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else if (h < 10000u) {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        } else {
            fmt_int(val, (int32_t)((h + 500u) / 1000u));   /* "12 kHz": "12.5" leaves no room for the unit (SLOOP 2.4) */
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        if (d->names == N_LT && v >= LT_E1 && v < LT_NB) {
            lt_text(d, v, val);
            break;
        }
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK, SC_SONG, SC_DRUM, SC_FM6K, SC_DSND };
#include "../fx/reverb/rev_type.c"            /* the reverb's algorithms built, TYPE's list, the project byte */
#include "../fx/fx_slots.c"         /* the generic FX slots: the types, the four slots */
#if BP_SET_ANY
#define SC_BPSET (SC_DSND + 1)   /* the backported features' settings (bp_set.c) */
#include "bp_set.c"
#endif
#if FELUCCA_MOTION
#define SC_MOTION (SC_DSND + 2)  /* SEQ > MOTION (motion.c): PLAY, the events, CLEAR */
#endif
#if FELUCCA_MACROS
#define SC_MACRO (SC_DSND + 3)   /* GLO > MACRO (macro.c): COLOR MOTION SPACE ENERGY, kept in the drum track's MAC_ID */
static const uint8_t MAC_ID[4] = {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LEG_FLT};   /* (values the drum track never reads) */
#endif
#define SC_FXSLOT (SC_DSND + 8)  /* FX > SLOTS (fx_slots.c): S1..S4, the type each slot holds */
/* a track page's cells that are not P_* values (page_id, page_desc): INSERTS' S1 / S2 TYPE (fx_slots.c ins_desc), the
 * drum track's send levels REV DLY CHO on SENDS (drum_sends.c dsend_kit) */
#define PID_INST 0xF0u
#define PID_DKS 0xF4u
_Static_assert(P_COUNT < PID_INST, "the page ids past the track values");
#define STEP_ID_CHANCE 4u        /* SC_STEP columns: 0 STEP, 1 NOTE, 2 TIME, 3 FLAG; 4 CHANCE (FELUCCA_CHANCE) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_DSND, GR_ENV2, GR_SNAP, GR_INS,
       GR_LFO2,                  /* (LFO 2's wave: the Optimist UI draws it, SLOOP's has no graph there) */
       GR_GATE };                /* GATER's 16 steps and the gate's shape (insfx/gater.c igt_profile) */

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    /* ENV 1 MOD (2026-10-16, env1_cards.h): ENV 1's four depth knobs, made per engine (page_id: the engine's cards; an
     * alias card the engine's own EDIT value); the amplitude always wired. ENV DEST (two free slots) until then */
    {"ENV 1 MOD", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_E1PIT, P_ED_SHP, P_E1X}},
#if FELUCCA_ANALOG2
    {"ENV2", FAM_ENV, SC_TRACK, GR_ENV2, {P_A2FATK, P_A2FDEC, P_A2ESUS, P_A2EREL}},   /* every engine since 2026-10-14 */
    {"ENV2 DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_N2DA, P_N2AA, P_N2DB, P_N2AB}},
#endif
#if DL_ANY
    /* the drum track's ENV: the picked lane's envelope values, the SOUND pages' own (drum_edit.c DE_*, the same storage):
     * CLICK the attack, DECAY, BEND the pitch drop, LEVEL its peak; a sound without one shows "-" (page_shown: the
     * drum track's only ENV page, in place of the synth's; the user, 2026-10-11) */
    {"ENV", FAM_ENV, SC_DSND, GR_DSND, {DE_CLICK, DE_DECAY, DE_BEND, DE_LEVEL}},
#endif
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_L1DA, P_L1AA, P_L1DB, P_L1AB}},   /* DEST A, AMT A, DEST B, AMT B */
    /* LFO 2 (voice.c lfo2_tick; the synth tracks' only: page_for_drum). DEST AMT leave two cards for a second pair */
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO2, {P_L2RATE, P_L2WAVE, P_L2TRIG, P_L2POL}},
    {"LFO2 DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_L2DA, P_L2AA, P_L2DB, P_L2AB}},
#if FELUCCA_UI != 1
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},   /* S1's amount, the master sends (page_id) */
#endif
    /* the track's two insert slots: S1 TYPE, its amount, S2 TYPE, its amount (fx_slots.c; the ids: page_id) */
    {"INSERTS", FAM_FX, SC_TRACK, GR_INS, {PID_INST, 0xFF, PID_INST + 1, 0xFF}},
#if FELUCCA_MASTER_COMP
    /* the track DYN's (fx-rework 2026-10-14: the COMP insert with its make-up and a limiter): its amount (THRS), RATIO, GAIN,
     * CEIL, then ATK REL (its own: fx_slots.c ins_cset; the ids: fxs_desc) */
    {"DYN", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_TAMT, 4, 7, 8}},
    {"DYN 2", FAM_FX, SC_FXSLOT, GR_NONE, {5, 6, 0xFF, 0xFF}},
#endif
#if FELUCCA_INS_GATER
    /* GATER (insfx/gater.c): DEPTH (page_desc names P_TGATE so; INSERTS' card keeps GATER), PAT, RATE, SHAPE; page_shown:
     * only while one of the track's slots holds it; the drum track's too (it gates the drum bus) */
    {"GATER", FAM_FX, SC_TRACK, GR_GATE, {P_TGATE, P_TGPAT, P_TGRATE, P_TGSHP}},
#endif
#if FELUCCA_INS_JCHORUS
    {"JUNO CHO", FAM_FX, SC_TRACK, GR_NONE, {P_TJCHO, P_TJMODE, 0xFF, 0xFF}},   /* while a slot of the track holds it */
#endif
#if FELUCCA_INS_EQ3
    /* EQ3 (fx/insfx/eq3.c): the track's LO MID HI and ON, then its crossovers (fx_slots.c eq3_desc; page_shown: while one of
     * its slots, or a lane's of the drum track, holds it) */
    {"EQ3", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_TEQ, FXS_ID_TEQ + 1, FXS_ID_TEQ + 2, FXS_ID_TEQ + 3}},
    {"EQ3 FREQ", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_TEQ + 4, FXS_ID_TEQ + 5, 0xFF, 0xFF}},
#endif
#if FELUCCA_UI == 1
    {"SENDS", FAM_FX, SC_TRACK, GR_FX, {P_REV, P_DLY, P_CHOR, 0xFF}},   /* the sends (page_id: A B C's); DR: its levels */
#endif
#if FELUCCA_TRK_FILT
    {"FILTER", FAM_FX, SC_TRACK, GR_NONE, {P_TFLT, 0xFF, 0xFF, 0xFF}},   /* the track's filter, the drum track's too (2.4) */
#endif
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
#if REV_MULTI
    {"REVERB", FAM_FX, SC_BPSET, GR_NONE, {BPS_RTYPE, 0xFF, 0xFF, 0xFF}},   /* TYPE: the algorithms built (rev_type.c) */
#endif
    {"SLOTS", FAM_FX, SC_FXSLOT, GR_NONE, {1, 2, 3, 0xFF}},   /* the type on each master return A B C (fx_slots.c; last of FX) */
    {"RET AMT", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_RAMT, FXS_ID_RAMT + 1, FXS_ID_RAMT + 2, 0xFF}},   /* (fx-pool) a return's
                                                     * other type's amount; page_shown: while one holds one */
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_CHORD}},
#if FELUCCA_CHORDPLUS
    {"SCL 2", FAM_SCL, SC_TRACK, GR_SCALE, {P_TRANS, P_STRUM, P_VLEAD, 0xFF}},   /* (SLOOP 2.4: the chords played) */
#else
    {"SCL 2", FAM_SCL, SC_TRACK, GR_SCALE, {P_TRANS, 0xFF, 0xFF, 0xFF}},
#endif
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
#if FELUCCA_ENG_ACID
    {"ACID GEN", FAM_EDIT, SC_BPSET, GR_NONE, {BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO}},   /* ACID tracks (eng_acid.c) */
#endif
#if FELUCCA_ANALOG2
    {"OSC 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2WAVE, P_A2SEMI, P_A2SYNC, 0xFF}},   /* ANALOG only: page_shown */
    {"SWARM", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2SWRM, P_A2SDTN, P_A2DRFT, 0xFF}},   /* (ENV2 on the spread: its DEST SDTN) */
    {"FLT 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2FTYP, 0xFF, 0xFF, 0xFF}},
#endif
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
#if FELUCCA_UI == 1
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_VOICES, P_ED_PIT}},   /* (the Optimist UI: PAN and MUTE are the mixer's, the user 2026-10-14; DROP on card 4: the pitch drop, ENV DEST PIT before 2026-10-14; card 3: feat/voice-count's VOICES) */
#else
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_ED_PIT, 0xFF, 0xFF, 0xFF}},   /* DROP: the pitch drop (ENV DEST PIT before) */
#endif
#if DL_ANY
    /* the drum track's EDIT family: the sound picked (ui_drums.c; ids are its values, not P_*) */
    {"SOUND", FAM_EDIT, SC_DSND, GR_DSND, {0, 1, 2, 3}},
    {"SOUND 2", FAM_EDIT, SC_DSND, GR_DSND, {4, 5, 6, 7}},
    {"SOUND 3", FAM_EDIT, SC_DSND, GR_DSND, {16, 17, 18, 0xFF}},   /* the sends and inserts, in the FX slots' order
                                                                     * (page_id: fx_slots.c fxs_lane_id) */
    {"SOURCE", FAM_EDIT, SC_DSND, GR_DSND, {8, 9, 10, 11}},
    {"KIT", FAM_EDIT, SC_DSND, GR_DSND, {12, 13, 14, 15}},
#endif
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
    /* the master's two insert slots (fx_slots.c mst_slot, fx-rework): S1 TYPE, its amount, S2 TYPE, its amount */
    {"MST INS", FAM_GLO, SC_FXSLOT, GR_NONE, {FXS_ID_MST, FXS_ID_MST + 2, FXS_ID_MST + 1, FXS_ID_MST + 3}},
#if FELUCCA_INS_JCHORUS
    {"MST JUNO", FAM_GLO, SC_FXSLOT, GR_NONE, {FXS_ID_JMST, FXS_ID_JMST + 1, 0xFF, 0xFF}},   /* the master's JUNO CHORUS */
#endif
#if FELUCCA_INS_EQ3
    {"MST EQ3", FAM_GLO, SC_FXSLOT, GR_NONE, {FXS_ID_MEQ, FXS_ID_MEQ + 1, FXS_ID_MEQ + 2, FXS_ID_MEQ + 3}},   /* the master's EQ3 */
    {"MST EQ3 F", FAM_GLO, SC_FXSLOT, GR_NONE, {FXS_ID_MEQ + 4, FXS_ID_MEQ + 5, 0xFF, 0xFF}},
    {"RET EQ3", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_REQ, FXS_ID_REQ + 1, FXS_ID_REQ + 2, FXS_ID_REQ + 3}},   /* a return's */
    {"RET EQ3 F", FAM_FX, SC_FXSLOT, GR_NONE, {FXS_ID_REQ + 4, FXS_ID_REQ + 5, 0xFF, 0xFF}},
#endif
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_DUCK, G_FILT, G_ROLL}},
    {"COMP", FAM_GLO, SC_GLOBAL, GR_NONE, {G_CTHR, G_CRAT, G_CATK, G_CREL}},   /* the master compressor (master_comp.c) */
    {"LIMIT", FAM_GLO, SC_GLOBAL, GR_NONE, {G_CGAIN, G_CCEIL, 0xFF, G_CGR}},   /* make-up, the limiter; GR: a readout */
#if FELUCCA_MACROS
    {"MACRO", FAM_GLO, SC_MACRO, GR_NONE, {0, 1, 2, 3}},   /* COLOR MOTN SPACE ENRGY (macro.c) */
#endif
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRLVL, 0xFF, 0xFF, 0xFF}},   /* (its MIDI channel: HOME menu > SYSTEM; REV: each sound's) */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
#if SL24_AUTO
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, G_A24, G_LOAD, G_SAVE}},   /* A24: SLOOP 2.4's autosave */
#else
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
#endif
#if FELUCCA_SNAPSHOTS
    {"SNAPSHOT", FAM_SAVE, SC_GLOBAL, GR_SNAP, {0xFF, 0xFF, 0xFF, 0xFF}},   /* whole-state slots: SLOT LOAD CLEAR SAVE */
#endif
#if FELUCCA_MISSING_WARN
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, G_MISS, G_NEWPRJ}},   /* MISS: miss.c */
#else
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, G_NEWPRJ}},
#endif
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
#if FELUCCA_CHANCE
    {"STEP 2", FAM_SEQ, SC_STEP, GR_ROLL, {0, STEP_ID_CHANCE, 0xFF, 0xFF}},   /* synth tracks: chance.c */
#endif
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
#if FELUCCA_MOTION
    {"MOTION", FAM_SEQ, SC_MOTION, GR_NONE, {0, 1, 2, 3}},   /* PLAY EVENTS FREE CLEAR (motion.c) */
#endif
    {"SONG", FAM_SEQ, SC_SONG, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"TRACKS", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* REC button; TRACK LEVEL LEN PAN */
    {"DRUMS", FAM_TRK, SC_DRUM, GR_NONE, {0xFF,0xFF,0xFF,0xFF}},
    /* the FM6 operator editor (ui_fm6.c): ENV on an FM6 track; last, so ENV's own pages never cycle into it */
    {"FM6", FAM_ENV, SC_FM6K, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
/* the drum track has an ENV page (its lanes' envelopes, the editor's values): ENV does something there (the LEDs) */
#define DSND_ENV (DL_ANY && FELUCCA_DRUM_EDIT)
/* a page cell's id: the FX page's four are the slots' amounts (fx_slots.c) */
/*   INSERTS    S1 TYPE, S1's amount (its type's P_*), S2 TYPE, S2's amount
 *   FX / SENDS SLOOP's FX: S1's amount first, then the master sends in their slots' order (fx_slots.c fxs_send; the
 *              default slots REV DLY CHO); the Optimist UI's SENDS: card k return R(k+1)'s send (fxs_card, blank for an
 *              empty return: one order on every screen); its drum track: its send levels (the lanes' multipliers: the
 *              drum bus has no send of its own, 2026-10-18) */
#define PG_E1(pg) ((pg)->scope == SC_TRACK && (pg)->id[1] == P_E1PIT)   /* the ENV 1 MOD page */
static uint32_t page_id(const page_t *pg, uint32_t k)
{
    k &= 3u;
    if (pg->graph == GR_INS)
        return k & 1u ? ins_amt(song.sel, k >> 1) : PID_INST + (k >> 1);
#if FELUCCA_UI == 1
    if (pg->graph == GR_FX) {   /* the returns' order (fx_slots.c fxs_card: card k is R(k+1)) */
        uint32_t t;
        if (k == 3u)                                    /* card 4: the track's PAN (the drum track's too) */
            return P_PAN;
        t = fxs_card(k);
        if (t == FXT_NONE)
            return 0xFFu;
        if (is_drum(TSEL))
            return PID_DKS + FXS_DSI(t);                /* (PID_DKS: REV DLY CHO) */
        return FXT_AMT[t];
    }
#else
    if (pg->graph == GR_FX) {
        uint32_t t = k ? fxs_send(k - 1u) : FXT_NONE;
        if (!k)
            return ins_amt(song.sel, 0);
        if (t == FXT_NONE)
            return 0xFFu;
        return is_drum(TSEL) ? PID_DKS + FXS_DSI(t) : (uint32_t)FXT_AMT[t];   /* (the drum track: its send levels, the lanes'
                                                                              * multipliers; its bus sends nothing) */
    }
#endif
    if (PG_E1(pg)) {                                    /* ENV 1 MOD: the engine's four cards (env1_cards.h) */
        return env1_id(TSEL->eng_req, k);
    }
    return pg->scope == SC_DSND && pg->id[0] == 16u ? fxs_lane_id(k) : pg->id[k];   /* (SOUND 3: in the slots' order) */
}

/* an FX page's (graph: GR_FX, GR_INS) cell k as its needle shows it: the amount's size 0..127 (a FILTER's
 * x 2), -1 none (an empty slot, a TYPE cell) */
static int32_t fx_needle(uint32_t graph, uint32_t k)
{
    page_t pg = {"", FAM_FX, SC_TRACK, 0, {0xFF, 0xFF, 0xFF, 0xFF}};
    uint32_t id;
    int32_t v;
    pg.graph = (uint8_t)graph;
    id = page_id(&pg, k);
    if (id >= PID_DKS && id < PID_DKS + 3u)
        return dsend_kit[id - PID_DKS];
    if (id >= P_COUNT)
        return -1;
    v = TSEL->p[id];
    v = v < 0 ? -2 * v : v;
    return v > 127 ? 127 : v;
}

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), PATTERN, FX (the drum bus's amounts), SLICER and TRACKS; every other page (STEP too)
 * shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_STEP)                       /* the synth steps' roll and cards: the drum track has dstep[] there */
#if FELUCCA_CHANCE
        return pg->id[1] == STEP_ID_CHANCE;         /* (its grid is the DRUMS page; SLOOP 2.4; STEP 2: its chance) */
#else
        return 0;
#endif
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR ||
                                      pg->graph == GR_FX || pg->graph == GR_INS
#if FELUCCA_TRK_FILT
                                      || pg->id[0] == P_TFLT   /* (FX > FILTER: the drum bus and its sends) */
#endif
#if FELUCCA_INS_GATER
                                      || pg->graph == GR_GATE   /* (FX > GATER: the drum bus's gate) */
#endif
#if FELUCCA_INS_JCHORUS
                                      || pg->id[0] == P_TJCHO   /* (FX > JUNO CHO: the drum bus's) */
#endif
                                      );
}

#if FELUCCA_ANALOG2 || DL_ANY || FELUCCA_ENG_FM6
#if FELUCCA_ENG_FM6
/* an engine's EDIT page shows a value (not only "-" columns: FM6's EDIT 2) */
static int engine_page_used(const page_t *pg)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        const param_desc_t *d = pg->id[k] == 0xFFu ? 0 : track_desc(TSEL, pg->id[k]);
        if (d && d->label && d->label[0] != '-')
            return 1;
    }
    return 0;
}
#endif

/* ANALOG 2's own pages (OSC 2, SWARM, FLT 2) are there on an ANALOG track only (ENV2 and ENV2 DEST: every engine): the family buttons step
 * past them, the overview and the page count leave them out, and they show no values elsewhere. The drum
 * track's EDIT family is its SOUND pages (those of this build), only there. An engine's page with nothing on
 * it is not shown either */
static int page_shown(const page_t *pg)
{
    if ((pg->graph == GR_SLCR && !FELUCCA_FX_SLICER) || (pg->id[0] == G_DTIME && pg->scope == SC_GLOBAL && !FXS_HAS(FXT_DLY)) ||
        ((pg->id[0] == G_CTHR || pg->id[0] == G_CGAIN) && pg->scope == SC_GLOBAL && !MST_ON(FXT_DYN)) ||   /* (COMP LIMIT:
                                                       * the master's DYN, while one of its slots holds it) */
        (pg->id[0] == G_RSIZE && pg->scope == SC_GLOBAL && !FXS_HAS(FXT_REV) && !FXS_HAS(FXT_CHO)))
        return 0;                                     /* the pages of an FX this build leaves out (registry.h), or in no
                                                       * FX slot (fx_slots.c) */
#if REV_MULTI
    if (pg->scope == SC_BPSET && pg->id[0] == BPS_RTYPE)
        return FXS_HAS(FXT_REV);                       /* REVERB > TYPE: while a slot holds the reverb */
#endif
#if FELUCCA_INS_JCHORUS
    if (pg->scope == SC_FXSLOT && pg->id[0] == FXS_ID_JMST)
        return MST_ON(FXT_JUNO);                      /* MST JUNO: while a master slot holds it */
#endif
    if (pg->scope == SC_FXSLOT && pg->id[0] == FXS_ID_RAMT)
        return rt_any();                              /* RET AMT: while a return holds a type that is not a send */
#if FELUCCA_INS_EQ3
    if (pg->scope == SC_FXSLOT && pg->id[0] >= FXS_ID_TEQ && pg->id[0] < FXS_ID_TEQ + 6u)
        return INS_ON(song.sel, FXT_EQ3) || (song.sel % NTRK == TRK_DRUM && lin_has(FXT_EQ3));   /* EQ3: the track's (a lane's: DR's) */
    if (pg->scope == SC_FXSLOT && pg->id[0] >= FXS_ID_MEQ && pg->id[0] < FXS_ID_MEQ + 6u)
        return MST_ON(FXT_EQ3);
    if (pg->scope == SC_FXSLOT && pg->id[0] >= FXS_ID_REQ && pg->id[0] < FXS_ID_REQ + 6u)
        return rt_eq3() < 3;
#endif
    if (pg->scope == SC_FXSLOT && pg->id[0] >= FXS_ID_MST)
        return MST_OK != 0u;                          /* MST INS: the master's insert slots */
    if (pg->scope == SC_FXSLOT && pg->id[0] >= FX_NSLOT)
        return INS_ON(song.sel, FXT_COMP);            /* DYN, DYN 2: while one of the track's slots holds the DYN (its own) */
#if FELUCCA_TRK_FILT
    if (pg->scope == SC_TRACK && pg->id[0] == P_TFLT)
        return INS_ON(song.sel, FXT_FILT);            /* FILTER: while one of the track's slots holds it */
#endif
#if FELUCCA_INS_GATER
    if (pg->graph == GR_GATE)
        return INS_ON(song.sel, FXT_GATE);            /* GATER: while one of the track's slots holds it */
#endif
#if FELUCCA_INS_JCHORUS
    if (pg->scope == SC_TRACK && pg->id[0] == P_TJCHO)
        return INS_ON(song.sel, FXT_JUNO);            /* JUNO CHO: while one of the track's slots holds it */
#endif
    if (pg->graph == GR_INS)
        return (FXT_BUILT & FXT_INSERT) != 0u;        /* INSERTS: any insert built */
#if FELUCCA_ENG_ACID
    if (pg->scope == SC_BPSET && pg->id[0] == BPS_GDENS)
        return !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], ACID);   /* ACID GEN: ACID tracks only */
#endif
#if DL_ANY
    if ((pg->fam == FAM_EDIT || pg->scope == SC_DSND) && (pg->scope == SC_DSND) != is_drum(TSEL))
        return 0;
    if (pg->fam == FAM_ENV && pg->scope == SC_TRACK && is_drum(TSEL) && DSND_ENV)
        return 0;                                     /* (the drum track's ENV: the lane's, not the synth's pages) */
    if (pg->scope == SC_DSND)                         /* SOUND 1, 2: the editor; SOURCE: samples or kits; KIT */
        return pg->id[0] >= 16u ? 1 : pg->id[0] < 8u ? FELUCCA_DRUM_EDIT :
               pg->id[0] < 12u ? FELUCCA_DRUM_USR || FELUCCA_DRUM_KITS : 1;
#endif
#if FELUCCA_ENG_FM6
    if (pg->scope == SC_ENGINE && !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], FM6) &&
        !engine_page_used(pg))
        return 0;                                     /* (FM6: EDIT 2, PATCH stays) */
#endif
#if FELUCCA_ANALOG2
    return pg->scope != SC_TRACK || pg->id[0] < P_A2WAVE || pg->id[0] >= P_E0 || pg->graph == GR_ENV2 ||   /* (ENV2: every engine) */
           (!is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], ANALOG));
#else
    return 1;
#endif
}
#define PAGE_SHOWN_FN 1
#else
#define page_shown(pg) 1
#endif
#if DL_ANY
/* the SOUND pages' values (ui_drums.c sets it: builds without the UI, the host tests, have none) */
static const param_desc_t *(*dsnd_desc_fn)(uint32_t id, int16_t **vp);
#endif

/* a cell of an FX this build leaves out shows nothing (its value stays in the project) */
static int cell_built(const page_t *pg, uint32_t id)
{
    if (pg->scope == SC_TRACK)
        return id == P_DIST ? FELUCCA_FX_DIST : id == P_CHOR ? FELUCCA_FX_CHORUS : id == P_DLY ? FELUCCA_FX_DELAY :
               id == P_REV ? FELUCCA_FX_REVERB : 1;
    if (pg->scope == SC_GLOBAL)
        return id == G_DUST ? FELUCCA_FX_DUST : id == G_DUCK ? FELUCCA_FX_DUCK : id == G_FILT ? FELUCCA_FX_DJF :
               (id >= G_CTHR && id <= G_CCEIL) || id == G_CGR ? FELUCCA_MASTER_COMP :
               id == G_RSIZE || id == G_RDAMP ? FXS_HAS(FXT_REV) :   /* (in a slot: fx_slots.c) */
               id == G_CRATE || id == G_CDEPTH ? FXS_HAS(FXT_CHO) : id == G_SYNC ? FELUCCA_MIDI_CLOCK : id == G_VIEW ? FELUCCA_OVERVIEW : id == G_A24 ? SL24_AUTO : 1;
    return 1;
}
#if FELUCCA_MISSING_WARN
static int16_t miss_n;                                /* TOOLS > MISS: what the project uses and this build lacks */
#endif
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = page_id(pg, slot);
    if (id == 0xFFu || !page_shown(pg) || !cell_built(pg, id) ||
        (is_drum(TSEL) && (!page_for_drum(pg) || (pg->scope == SC_GLOBAL && id == G_INITSND)))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (id >= PID_INST && id < PID_INST + INS_N)       /* INSERTS: S1 / S2 TYPE (fx_slots.c) */
        return fxs_desc(FXS_ID_INS + id - PID_INST, valp);
    if (id >= PID_DKS && id < PID_DKS + 3u) {          /* the drum track's send levels (drum_sends.c dsend_kit) */
        static const param_desc_t DKS_DESC[3] = {PD("REV", F_PCT, 0, 127, DKS_DEF), PD("DLY", F_PCT, 0, 127, DKS_DEF),
                                                 PD("CHO", F_PCT, 0, 127, DKS_DEF)};
        *valp = &dsend_kit[id - PID_DKS];
        return &DKS_DESC[id - PID_DKS];
    }
#if FELUCCA_MOTION
    if (pg->scope == SC_MOTION) {                     /* (drawn and edited by ui_draw.c / ui_input.c) */
        *valp = 0;
        return 0;
    }
#endif
#if FELUCCA_MACROS
    if (pg->scope == SC_MACRO) {                      /* the positions, in the drum track's MAC_ID values */
        static const param_desc_t MAC_DESC[4] = {PD("COLOR", F_BIPCT, -64, 63, 0), PD("MOTN", F_BIPCT, -64, 63, 0),
                                                 PD("SPACE", F_BIPCT, -64, 63, 0), PD("ENRGY", F_BIPCT, -64, 63, 0)};
        *valp = &TDRUM->p[MAC_ID[id & 3u]];
        return &MAC_DESC[id & 3u];
    }
#endif
#if DL_ANY
    if (pg->scope == SC_DSND) {
        *valp = 0;
        return dsnd_desc_fn ? dsnd_desc_fn(id, valp) : 0;
    }
#endif
#if BP_SET_ANY
    if (pg->scope == SC_BPSET)
        return bps_desc(id, valp);
#endif
    if (pg->scope == SC_FXSLOT)
        return fxs_desc(id, valp);
    if (pg->scope == SC_GLOBAL) {
#if FELUCCA_MISSING_WARN
        if (id == G_MISS) {                           /* TOOLS > MISS: a count, no stored value (miss.c) */
            static const param_desc_t MISS_DESC = PD("MISS", F_INT, 0, 99, 0);
            *valp = &miss_n;
            return &MISS_DESC;
        }
#endif
#if FELUCCA_MASTER_COMP
        if (id == G_CGR) {                            /* LIMIT > GR: the gain reduction now, read only (master_comp.c) */
            static const param_desc_t GR_DESC = {"GR", F_INT, 0, 0, 0, 0, "dB"};
            *valp = &mc_gr_view;
            return &GR_DESC;
        }
#endif
#if SL24_AUTO
        if (id == G_A24) {                            /* PROJECT > A24: a GO button, no stored value (sl24_guard.c) */
            static const param_desc_t A24_DESC = PE("A24", N_GO, 0);
            static int16_t a24_go;
            *valp = &a24_go;
            return &A24_DESC;
        }
#endif
        *valp = &song.g[id];
        return &GP[id];
    }
#if FELUCCA_INS_GATER
    if (pg->graph == GR_GATE && id == P_TGATE) {      /* GATER's amount, DEPTH on its own page (INSERTS: GATER, by type) */
        static const param_desc_t GT_DEPTH = PD("DEPTH", F_PCT, 0, 127, 0);
        *valp = &TSEL->p[id];
        return &GT_DEPTH;
    }
#endif
    *valp = &TSEL->p[id];
    if (id == P_VOICES) {                             /* the stored 0 (default, old projects) is the engine's max; a larger one is cut to it */
        uint32_t m = eng_vmax(ENGINES[TSEL->eng_req % NENGINES]);
        if (TSEL->p[id] <= 0 || (uint32_t)TSEL->p[id] > m)
            TSEL->p[id] = (int16_t)m;
    }
    if (id == P_CHOR || id == P_DLY || id == P_REV)    /* a send: named by its line's return (fx-pool: PHSR ...) */
        return fxs_send_d(id);
    if (PG_E1(pg))                                      /* ENV 1 MOD: the engine's card, by its name */
        return e1_card_desc(TSEL->eng_req, slot);
    return track_desc(TSEL, id);
}
