/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
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
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};   /* G_CLOCK is the metronome (seq.c click_tick) */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SYNC[] = {"INT", "USB", "TRS", "AUTO"};   /* SYNC_* (core.h); AUTO: TRS, USB, INT */
static const char *const N_CLKSRC[] = {"INT", "USB", "TRS"};         /* G_MIDI: the clock followed (MSRC_*) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_CHORD[] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};   /* seq.c CHORD_DEG */
static const char *const N_FXOFF[] = {"ON", "OFF"};                  /* P_FXOFF: 0 = the effects heard */
static const char *const N_VIEW[] = {"PAGE", "ALL"};                  /* G_VIEW */
static const char *const N_ROLL[] = {"1/8", "1/16", "1/32", "32T", "1/64"};   /* seq.c ROLL_DEN */
/* the master COMP / LIMIT (master_comp.c MC_RATIO, MC_ATK, MC_REL, MC_CEIL: the same order) */
static const char *const N_CRAT[] = {"1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "20:1", "INF"};
static const char *const N_CATK[] = {"0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms"};
static const char *const N_CREL[] = {"50ms", "100ms", "200ms", "300ms", "600ms", "1.2s", "AUTO"};
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
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("TRIM", F_INT, -64, 63, 0),   /* the sound's level trim, 1/2 dB (presets set it) */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
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
};
/* a preset's extra parameters (preset_t.x) into p, each clamped to its range */
static void preset_extras(int16_t *p, const preset_t *pr)
{
    uint32_t i;
    for (i = 0; i + 1u < 8u; i += 2u) {
        int32_t id = pr->x[i] - 1;
        if (id >= 0 && id < P_E0)
            p[id] = (int16_t)clamp(pr->x[i + 1u], TP[id].min, TP[id].max);
    }
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
    [G_CCEIL] = PE("CEIL", N_CCEIL, 0),              /* dB below the output's full scale; OFF: the old limiter */
};

static const param_desc_t DRUM_KIT_DESC = {"KIT", F_ENUM, 0, (int16_t)(DRUM_KITS - 1u), 0, DRUM_KIT_NAMES, 0};   /* (drums.c) */
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* a knob's steps on value v: clamped to the range; "-" (a value not shown) does not move; a list (F_ENUM) steps
 * one entry a detent past an entry with no name (FM6's ENGINE: a mode this build leaves out), and stays where it
 * is when no named entry is left that way. (Only FM6 with fewer than three modes has such a list or such a
 * value: other builds clamp, as before) */
#if FELUCCA_ENG_FM6 && FM6_NMODES < 3
#define PARAM_HIDDEN(d) (!(d)->label || (d)->label[0] == '-')   /* (a value with a range the page does not show) */
#else
#define PARAM_HIDDEN(d) 0
#endif
#if !(FELUCCA_ENG_FM6 && FM6_NMODES < 3)
#define param_step0(d, v, steps) clamp((v) + (steps), (d)->min, (d)->max)
#else
static int32_t param_step0(const param_desc_t *d, int32_t v, int32_t steps)
{
    int32_t u, dir = steps < 0 ? -1 : 1;
    if (PARAM_HIDDEN(d))
        return v;
    if (d->fmt != F_ENUM || !d->names)
        return clamp(v + steps, d->min, d->max);
    v = clamp(v, d->min, d->max);
    for (; steps; steps -= dir) {
        for (u = v + dir; u >= d->min && u <= d->max && !d->names[u]; u += dir)
            ;
        if (u < d->min || u > d->max)
            break;
        v = u;
    }
    return v;
}
#endif

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
    return param_step0(d, v, steps);
}
#else
#define enum_rank(d, v) (v)
#define param_step(d, v, steps) param_step0(d, v, steps)
#endif

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
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
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
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
static const uint8_t MAC_ID[4] = {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_LD_FLT};   /* (values the drum track never reads) */
#endif
#define STEP_ID_CHANCE 4u        /* SC_STEP columns: 0 STEP, 1 NOTE, 2 TIME, 3 FLAG; 4 CHANCE (FELUCCA_CHANCE) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_DSND, GR_ENV2, GR_SNAP };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* (P_ED_FX: the level trim, no page) */
#if FELUCCA_ANALOG2
    {"ENV2", FAM_ENV, SC_TRACK, GR_ENV2, {P_A2FATK, P_A2FDEC, P_A2ESUS, P_A2EREL}},   /* ANALOG only: page_shown */
    {"ENV2 DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_A2FENV, P_A2EPIT, P_A2ESHP, P_A2EOS2}},   /* (SDTN: SWARM) */
#endif
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
#if FELUCCA_TRK_FILT
    {"FILTER", FAM_FX, SC_TRACK, GR_NONE, {P_TFLT, 0xFF, 0xFF, 0xFF}},   /* the track's filter, the drum track's too (2.4) */
#endif
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
#if REV_MULTI
    {"REVERB", FAM_FX, SC_BPSET, GR_NONE, {BPS_RTYPE, 0xFF, 0xFF, 0xFF}},   /* TYPE: the algorithms built (rev_type.c) */
#endif
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
    {"SWARM", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2SWRM, P_A2SDTN, P_A2DRFT, P_A2ESDT}},   /* ENV2: on the spread */
    {"FLT 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2FTYP, 0xFF, 0xFF, 0xFF}},
#endif
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
#if DL_ANY
    /* the drum track's EDIT family: the sound picked (ui_drums.c; ids are its values, not P_*) */
    {"SOUND", FAM_EDIT, SC_DSND, GR_DSND, {0, 1, 2, 3}},
    {"SOUND 2", FAM_EDIT, SC_DSND, GR_DSND, {4, 5, 6, 7}},
    {"SOUND 3", FAM_EDIT, SC_DSND, GR_DSND, {16, 17, 18, 0xFF}},   /* the sends (drum_sends.c) */
    {"SOURCE", FAM_EDIT, SC_DSND, GR_DSND, {8, 9, 10, 11}},
    {"KIT", FAM_EDIT, SC_DSND, GR_DSND, {12, 13, 14, 15}},
#endif
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
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
/* a page cell's id: the FX page's four are the slots' amounts (fx_slots.c) */
static uint32_t page_id(const page_t *pg, uint32_t k) { return pg->graph == GR_FX ? fxs_amt(k) : pg->id[k]; }

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), PATTERN, SLICER and TRACKS; every other page (STEP too)
 * shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_STEP)                       /* the synth steps' roll and cards: the drum track has dstep[] there */
        return 0;                                   /* (its grid is the DRUMS page; SLOOP 2.4) */
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR
#if FELUCCA_TRK_FILT
                                      || pg->id[0] == P_TFLT   /* (FX > FILTER: the drum bus and its sends) */
#endif
                                      );
}

#if FELUCCA_ANALOG2 || DL_ANY || (FELUCCA_ENG_FM6 && FM6_NMODES == 1)
#if FELUCCA_ENG_FM6 && FM6_NMODES == 1
/* an engine's EDIT page shows a value (not only "-" columns: FM6's EDIT 2 with one ENGINE mode built) */
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

/* ANALOG 2's own pages (OSC 2, SWARM, FLT 2; ENV2, ENV2 DEST) are there on an ANALOG track only: the family buttons step
 * past them, the overview and the page count leave them out, and they show no values elsewhere. The drum
 * track's EDIT family is its SOUND pages (those of this build), only there. An engine's page with nothing on
 * it is not shown either */
static int page_shown(const page_t *pg)
{
    if ((pg->graph == GR_SLCR && !FELUCCA_FX_SLICER) || (pg->id[0] == G_DTIME && pg->scope == SC_GLOBAL && !FELUCCA_FX_DELAY) ||
        ((pg->id[0] == G_CTHR || pg->id[0] == G_CGAIN) && pg->scope == SC_GLOBAL && !FELUCCA_MASTER_COMP) ||
        (pg->id[0] == G_RSIZE && pg->scope == SC_GLOBAL && !FELUCCA_FX_REVERB && !FELUCCA_FX_CHORUS))
        return 0;                                     /* the pages of an FX this build leaves out (registry.h) */
#if FELUCCA_ENG_ACID
    if (pg->scope == SC_BPSET && pg->id[0] == BPS_GDENS)
        return !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], ACID);   /* ACID GEN: ACID tracks only */
#endif
#if FELUCCA_CHANCE
    if (pg->scope == SC_STEP && pg->id[1] == STEP_ID_CHANCE && is_drum(TSEL))
        return 0;                                     /* STEP 2 (chance): synth tracks only */
#endif
#if DL_ANY
    if (pg->fam == FAM_EDIT && (pg->scope == SC_DSND) != is_drum(TSEL))
        return 0;
    if (pg->scope == SC_DSND)                         /* SOUND 1, 2: the editor; SOURCE: samples or kits; KIT */
        return pg->id[0] >= 16u ? 1 : pg->id[0] < 8u ? FELUCCA_DRUM_EDIT :
               pg->id[0] < 12u ? FELUCCA_DRUM_USR || FELUCCA_DRUM_KITS : 1;
#endif
#if FELUCCA_ENG_FM6 && FM6_NMODES == 1
    if (pg->scope == SC_ENGINE && !is_drum(TSEL) && ENG_IS(ENGINES[TSEL->eng_req % NENGINES], FM6) &&
        !engine_page_used(pg))
        return 0;                                     /* (FM6 with one ENGINE mode: EDIT 2, PATCH stays) */
#endif
#if FELUCCA_ANALOG2
    return pg->scope != SC_TRACK || pg->id[0] < P_A2WAVE || pg->id[0] >= P_E0 ||
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
               id == G_RSIZE || id == G_RDAMP ? FELUCCA_FX_REVERB :
               id == G_CRATE || id == G_CDEPTH ? FELUCCA_FX_CHORUS : id == G_SYNC ? FELUCCA_MIDI_CLOCK : id == G_VIEW ? FELUCCA_OVERVIEW : id == G_A24 ? SL24_AUTO : 1;
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
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
