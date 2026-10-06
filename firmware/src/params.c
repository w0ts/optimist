/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
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
#if FELUCCA_ANALOG2
static const char *const N_A2WAVE[] = {"=1", "SAW", "SQR", "TRI", "SIN", "PWM"};   /* eng_analog2.c: =1 osc 1's */
static const char *const N_A2FTYP[] = {"LP12", "LP24", "BP", "HP"};
static const char *const N_A2EDST[] = {"CUT", "PITCH", "SHAPE", "OSC2", "SDTN"};   /* ENV2's destination */
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
    [P_SDIV] = PE("DIV", N_DIV, 2),
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
    [P_A2FENV] = PD("AMT2", F_BIPCT, -64, 63, 0),
    [P_A2SWRM] = PD("SWARM", F_INT, 0, 6, 0),       /* copies of osc 1 (eng_analog2.c a2_copies) */
    [P_A2SDTN] = PD("SDTN", F_PCT, 0, 127, 34),      /* their spread (SUPER's SDTN) */
    [P_A2ESUS] = PD("SUS2", F_PCT, 0, 127, 0),
    [P_A2EREL] = {"REL2", F_TIME, 0, 127, 0, N_A2EREL, 0},
    [P_A2EDST] = PE("DST2", N_A2EDST, 0),
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
    [G_DTIME] = PE("TIME", N_DIV, 1),
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
    [G_DRREV] = PD("REV", F_INT, 0, 127, 16),
    [G_DUST] = PD("DUST", F_PCT, 0, 127, 0),
    [G_DUCK] = PD("DUCK", F_PCT, 0, 127, 0),
    [G_FILT] = PD("FILT", F_FILT, -64, 63, 0),
    [G_ROLL] = PE("ROLL", N_ROLL, 1),
    [G_NEWPRJ] = PE("NEW", N_GO, 0),
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
#define param_step(d, v, steps) clamp((v) + (steps), (d)->min, (d)->max)
#else
static int32_t param_step(const param_desc_t *d, int32_t v, int32_t steps)
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

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    if (dsend_fmt(d, v, val))                         /* a drum lane's REV at TRK (drum_sends.c) */
        return;
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
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
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
#if BP_SET_ANY
#define SC_BPSET (SC_DSND + 1)   /* the backported features' settings (bp_set.c) */
#include "bp_set.c"
#endif
#if FELUCCA_MOTION
#define SC_MOTION (SC_DSND + 2)  /* SEQ > MOTION (motion.c): PLAY, the events, CLEAR */
#endif
#define STEP_ID_CHANCE 4u        /* SC_STEP columns: 0 STEP, 1 NOTE, 2 TIME, 3 FLAG; 4 CHANCE (FELUCCA_CHANCE) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_DSND, GR_ENV2 };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* (P_ED_FX: the level trim, no page) */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
#if FELUCCA_SPRING
    {"REVERB", FAM_FX, SC_BPSET, GR_NONE, {BPS_RTYPE, 0xFF, 0xFF, 0xFF}},   /* TYPE: ROOM / SPRING (spring.c) */
#endif
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_CHORD}},
    {"SCL 2", FAM_SCL, SC_TRACK, GR_SCALE, {P_TRANS, 0xFF, 0xFF, 0xFF}},
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
#if FELUCCA_ENG_ACID
    {"ACID GEN", FAM_EDIT, SC_BPSET, GR_NONE, {BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO}},   /* ACID tracks (eng_acid.c) */
#endif
#if FELUCCA_ANALOG2
    {"OSC 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2WAVE, P_A2SEMI, P_A2SYNC, 0xFF}},   /* ANALOG only: page_shown */
    {"SWARM", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2SWRM, P_A2SDTN, P_A2DRFT, 0xFF}},
    {"FLT 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2FTYP, P_A2FENV, P_A2EDST, 0xFF}},   /* ENV2's amount, where */
    {"ENV2", FAM_EDIT, SC_TRACK, GR_ENV2, {P_A2FATK, P_A2FDEC, P_A2ESUS, P_A2EREL}},
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
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_VIEW, G_INFO}},
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRCH, G_DRLVL, G_DRREV, 0xFF}},   /* GM kit on MIDI ch 10 */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
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

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), STEP, PATTERN, SLICER and TRACKS; every other page
 * shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR);
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

/* ANALOG 2's own pages (OSC 2, SWARM, FLT 2) are there on an ANALOG track only: the family buttons step
 * past them, the overview and the page count leave them out, and they show no values elsewhere. The drum
 * track's EDIT family is its SOUND pages (those of this build), only there. An engine's page with nothing on
 * it is not shown either */
static int page_shown(const page_t *pg)
{
    if ((pg->graph == GR_SLCR && !FELUCCA_FX_SLICER) || (pg->id[0] == G_DTIME && pg->scope == SC_GLOBAL && !FELUCCA_FX_DELAY) ||
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
        return pg->id[0] >= 16u ? FELUCCA_DRUM_SENDS : pg->id[0] < 8u ? FELUCCA_DRUM_EDIT :
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
               id == G_RSIZE || id == G_RDAMP || id == G_DRREV ? FELUCCA_FX_REVERB :
               id == G_CRATE || id == G_CDEPTH ? FELUCCA_FX_CHORUS : id == G_SYNC ? FELUCCA_MIDI_CLOCK : id == G_VIEW ? FELUCCA_OVERVIEW : 1;
    return 1;
}
#if FELUCCA_MISSING_WARN
static int16_t miss_n;                                /* TOOLS > MISS: what the project uses and this build lacks */
#endif
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
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
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
