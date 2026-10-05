/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SUPER: a superwave per voice, so a chord plays as a chord (ANALOG's SUPERSAW preset stacks
 * the eight voices of UNISON on one note). Ported from Jangada's ANALOG superwave (eng_analog.c
 * analog_render_x), which shares Felucca's ancestry with SLOOP:
 *
 *   a band-limited saw and up to 6 copies of it (SUPR), copy k sitting COPY_AT[k] steps of the
 *   spread (SDTN) above or below, so one accumulator of the spread phase drives them all; MIX the
 *   level of the copies against the centre saw (the JP-8000's MIX); DRFT a slow random wander of
 *   the pitch per voice; SUB a square an octave below; CUT / RES / FTYP: the trapezoidal SVF as
 *   LP12 (ANALOG's), LP24, BP or HP, with ANALOG's default key tracking and soft knee.
 *
 * The CPU: all parts share NVOICE voices; with more than 4 of them sounding the superwave keeps
 * fewer copies (super_block counts them once a block). */
static const char *const N_SUPER_FTYP[] = {"LP12", "LP24", "BP", "HP"};

/* Jangada's dsp.c: the trapezoidal SVF step of tsvf_lp with the band-pass too (v1); high-pass =
 * in - k * bp - lp, k the damping of tsvf_coef (super_k) */
static inline int32_t tsvf_lpbp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *bp)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    *bp = v1;
    return v2;
}
static inline int32_t super_k(int32_t reso) { return 8192 - reso * 7600 / 127; }   /* Q12, as tsvf_coef */

/* voice state: ph[0] centre saw, ph[2] spread phase; s[0..1] filter, s[4..5] its second stage (LP24),
 * s[6] sub phase (voice.c voice_start keeps these on a retrigger), s[2] noise state (drift), s[3] drift */
static void super_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {
        v->ph[0] = 0;                                 /* a fresh note: the same attack every time */
        v->ph[2] = 0;
    }
    v->s[0] = v->s[1] = 0;
    v->s[4] = v->s[5] = 0;
    v->s[6] = 0;
    v->s[3] = 0;
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;
}

static uint32_t voices_busy(void);                    /* voice.c */
static uint8_t super_nv;                              /* voices sounding, all parts (super_block) */
static void super_block(track_t *t)
{
    (void)t;
    super_nv = (uint8_t)voices_busy();
}

/* the copies the CPU allows: 6 up to 4 voices, 4 up to 6, 2 above (Jangada: 8 voices of 7 saws
 * measured 73 % on the FM-1 and lost voices to the shedder; capped like this, 55 %) */
static uint32_t super_copies(uint32_t want)
{
    if (want > 4u && super_nv > 4u)
        want = 4;
    if (want > 2u && super_nv > 6u)
        want = 2;
    return want;
}

/* DRFT: a random walk of the pitch per block, cents x 256, up to +-30 ct; returns 1/4096 units */
static int32_t super_drift(int32_t *dp, int32_t *nst, int32_t drift)
{
    int32_t d = *dp, lim = drift * 30 * 256 / 127;
    d += ((int32_t)(noise32(nst) >> 24) - 128) * drift / 8;
    d -= d >> 7;                                      /* drawn back to the pitch */
    d = clamp(d, -lim, lim);
    *dp = d;
    return (d >> 8) * 2367 / 1000;
}

static void super_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint32_t COPY_PH[6] = {0x2B7E1516u, 0x9E3779B9u, 0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au};
    static const int8_t COPY_AT[6] = {1, -1, 2, -2, 3, -3};   /* spread steps of copy k */
    const int16_t *p = t->p;
    uint32_t ncopy = super_copies((uint32_t)p[P_E0]), ftyp = (uint32_t)p[P_E7], i, k;
    int32_t sd = p[P_E1], gc = p[P_E2] * 258, drift = p[P_E3], sg = p[P_E4] * 200;
    int32_t cut = (p[P_E5] << 8) + m->cutoff + (64 * (v->pitch16 - 60 * 16) >> 4);   /* ANALOG's KTR 64 */
    int32_t kq = super_k(p[P_E6]), cg, gcg;
    uint32_t inc = m->inc, dinc, subinc, cph[6], cinc[6];
    uint32_t ph0 = v->ph[0], spr = v->ph[2], sph = (uint32_t)v->s[6];   /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], jc1 = v->s[4], jc2 = v->s[5], nst = v->s[2], dft = v->s[3];
    tsvf_t flt;
    if (drift)
        inc += (uint32_t)((int32_t)(inc >> 12) * super_drift(&dft, &nst, drift));
    /* SDTN: finer at the low end; 127 puts the outer copies (3 steps) 60 ct away (1 ct = 2.367 / 4096,
     * here in 1 / 65536: no dead zone at small spreads) */
    sd = (sd + sd * sd / 127) >> 1;
    dinc = (inc >> 16) * (uint32_t)(sd * 60 * 2367 * 16 / (127 * 3 * 1000));
    subinc = inc >> 1;
    /* level: centre at 1, copies at MIX; the sum scaled by 1 / (1 + ncopy * MIX), as Jangada's
     * 1 / (1 + 0.6 ncopy): the same peak whatever the copies */
    cg = (32767 * 1024) / (1024 + (((int32_t)ncopy * gc) >> 5));
    gcg = mulq15(gc, cg);
    for (k = 0; k < ncopy; k++) {
        cph[k] = ph0 + (uint32_t)(int32_t)COPY_AT[k] * spr + COPY_PH[k];
        cinc[k] = inc + (uint32_t)(int32_t)COPY_AT[k] * dinc;
    }
    tsvf_coef(&flt, cut, p[P_E6]);
    for (i = 0; i < n; i++) {
        int32_t s = mulq15(osc_saw(ph0, inc), cg), y, bp, ab;
        if (ncopy) {
            int32_t sum = 0;
            for (k = 0; k < ncopy; k++) {
                sum += osc_saw(cph[k], cinc[k]);
                cph[k] += cinc[k];
            }
            s += ((sum >> 2) * gcg) >> 13;            /* |sum| < 6 x 2^15: no overflow */
        }
        ph0 += inc;
        if (sg) {
            s += mulq15(osc_pulse(sph, subinc, 0x80000000u), sg);
            sph += subinc;
        }
        y = tsvf_lpbp(&flt, s >> 1, &ic1, &ic2, &bp);
        if (ftyp == 1)                                /* LP24: the low-pass again (input bounded: no overflow) */
            y = tsvf_lp(&flt, clamp(y, -100000, 100000), &jc1, &jc2);
        else if (ftyp == 2)
            y = bp;
        else if (ftyp == 3)
            y = (s >> 1) - ((kq * bp) >> 12) - y;     /* HP = in - k bp - lp */
        ab = y < 0 ? -y : y;                          /* ANALOG's soft knee: only resonance peaks saturate */
        if (ab > 16000) {
            ab = 16000 + (softclip((ab - 16000) * 2) >> 1);
            y = y < 0 ? -ab : ab;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[2] = spr + dinc * n;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = dft;
    v->s[4] = jc1;
    v->s[5] = jc2;
    v->s[6] = (int32_t)sph;
}

static const preset_t SUPER_PRESETS[] = {
    /* {SUPR, SDTN, MIX, DRFT, SUB, CUT, RES, FTYP}, {A D S R}, fenv, mono */
    {"SUPER LEAD", {6, 56, 80, 12, 0, 100, 18, 0}, {2, 80, 110, 50}, 12, 1, FX(0, 20, 40, 45), XP(P_GLIDE + 1, 20)},
    {"SUPER PAD", {6, 72, 90, 30, 0, 64, 12, 1}, {90, 90, 118, 100}, 8, 0, FX(0, 50, 25, 80),
     XP(P_LRATE + 1, 14, P_LD_FLT + 1, 12)},
    /* trance chords: four notes, each a superwave, a short filter envelope */
    {"SUPER CHRD", {6, 60, 85, 8, 0, 70, 22, 0}, {0, 72, 70, 40}, 30, 0, FX(0, 25, 45, 50)},
    {"SUPER PLCK", {4, 44, 70, 0, 0, 60, 30, 0}, {0, 82, 24, 50}, 40, 0, FX(0, 15, 50, 40)},
    /* the rave hoover: wide, a sub, a swoop up into each note (ENV -> PITCH) and a slow glide */
    {"HOOVER SAW", {6, 110, 100, 20, 70, 92, 20, 0}, {6, 70, 110, 40}, 0, 1, FX(20, 40, 20, 30),
     XP(P_GLIDE + 1, 60, P_GLMODE + 1, 1, P_ED_PIT + 1, -24)},
};

static const engine_t ENG_SUPER = {
    "SUPER", {"SAW", "TONE"},
    {
        {"SUPR", F_INT, 0, 6, 6, 0, 0},
        {"SDTN", F_PCT, 0, 127, 50, 0, 0},
        {"MIX", F_PCT, 0, 127, 80, 0, 0},
        {"DRFT", F_PCT, 0, 127, 10, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"FTYP", F_ENUM, 0, 3, 0, N_SUPER_FTYP, 0},
    },
    SUPER_PRESETS, sizeof(SUPER_PRESETS) / sizeof(SUPER_PRESETS[0]), 1, super_note_on, super_render,
    0xFD20, {P_E1, P_E5, P_E6, P_REL},
    .block = super_block,
};
