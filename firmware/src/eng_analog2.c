/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2 (FELUCCA_ANALOG2; eng_analog.c includes it in place of the original note-on and render).
 * The same engine: its eight EDIT values mean what they did, and ten more sit on three pages of its own
 * (core.h P_A2WAVE..P_A2SDTN; params.c EDIT > OSC 2, SWARM, FLT 2). At their defaults the sound is ANALOG's.
 *   OSC 2  WAVE2 osc 2's own wave (=1: osc 1's). SEMI its interval, -24..+24 st (DTN: the fine detune on
 *          top). SYNC: osc 2 restarts with osc 1 (hard sync, band-limited: a polyBLEP of osc 2's jump on
 *          both sides of the restart); with SYNC on, the SHP modulation (ENV DEST SHP, LFO DEST SHP)
 *          sweeps osc 2's pitch (+-32 st), the classic sync sweep.
 *   SWARM  SWARM 1..6 copies of osc 1 around it, SDTN their spread: SUPER's superwave (its engine is gone:
 *          its presets are ANALOG's now). DRFT: SUPER's slow random wander of the pitch, osc 2 against
 *          osc 1; DRFT > 0 also leaves the phases free (no restart at a note: pads, strings), DRFT 0
 *          restarts them (an 808 starts the same every time).
 *   FLT 2  FTYP LP12 (ANALOG's), LP24 (a second stage, unresonant), BP, HP. FATK / FDEC / FENV: an AD
 *          envelope of the cutoff of its own, restarted by each note (ENV DEST FLT still adds the ADSR).
 * The filter is ANALOG's trapezoidal SVF, but its two states saturate softly in the loop (the band-pass
 * state linear up to 32768, the low-pass one up to 65536, a tanh knee above, bounded so the products fit
 * 32 bits) instead of the hard clamp at +-150000,
 * and RES goes on past ANALOG's top: up to 100 the damping is ANALOG's, then it falls faster and below
 * zero at about 126, where the filter rings at the cutoff by itself (self-oscillation; the saturation
 * sets its level, KTR 64 plays it in tune). The cutoff moves smoothly: when it moves, the coefficients
 * are worked out every 8 samples, from the last block's cutoff to this one's (ANALOG: once a block).
 *
 * Kernels (the asm porting guide). analog_render works a block (n = CTL = 32 samples) through one buffer
 * b[] of the voice, half scale (the filter's input level: ANALOG's s >> 1), kernel by kernel; each is a
 * plain static function with one simple loop, no branch in it but the polyBLEP's / the saturation's
 * (rare), its state loaded into locals before the loop and stored after; the path (wave, filter mode,
 * swarm, osc 2, sync) is chosen per block. n is CTL or CTL / 4 (the cutoff steps), but an oscillator
 * kernel also gets the segments of a2_sync (any n >= 1). All in C; no pi32v2 builtin (the products fit
 * 32 bits: no 64-bit MAC needed so far).
 *   osc  A2_OSC[w](b, ph, inc, pw, g, n)  b[i] += mulq15(wave(ph + i inc), g), w: SAW SQR TRI SIN PWM
 *        (SQR: pw 0x80000000). No state (phases are the caller's). g 32768: exactly the wave. |b| stays
 *        below 2^17 (osc 1 + 6 copies + osc 2, all at their gains).
 *   a2_sync(b, w2, ph0, inc1, ph1, inc2, pw, g, n) -> ph1 after n: osc 2 hard-synced to osc 1, as
 *        segments of A2_OSC[w2] between osc 1's wraps, a correction on the two samples around each.
 *   a2_noise(b, &nst, nz, n)  b[i] += mulq15(noise - 16384, nz); nst the xorshift state (v->s[2]).
 *   a2_drive(b, drive, dw, n) b[i] += mulq15(softclip(b[i] drive / 2^14) / 2 - b[i], dw) (ANALOG's DRV).
 *   a2_lp / a2_bp / a2_hp(b, st, c, kd, n)  the SVF in place: b[i] = low / band / high-pass of b[i];
 *        the states saturate (A2_SAT: one compare inline, a2_knee called past it: band-pass 32768,
 *        low-pass 65536);
 *        st[0..1] its states (v->s[0..1]; LP24's second stage v->s[4..5], fed by the first a2_lp);
 *        the input is clamped to +-65536 (only LP24's second stage gets near it); kd the damping (HP).
 *   a2_out(out, b, amp0, amp1, n)  out[i] += the soft knee of b[i] (16000), x 2 x the amplitude ramp
 *        amp0 -> amp1 (Q15) x VOICE_FS.
 * Voice state: ph[0] / ph[1] the oscillators, ph[2] the swarm's spread phase; s[0..1] the filter, s[4..5]
 * its second stage, s[2] the noise generator (and the drift's), s[3] the drift, s[6] the last block's
 * cutoff (A2_NOCUT: a fresh note), s[7] the filter envelope (Q24, | A2_ATK while it rises). voice.c
 * voice_start keeps s[0..1], s[4..6] on a retrigger. */
#define A2_NOCUT INT32_MIN
#define A2_ATK (1 << 30)
#define A2_SWARM_GC (80 * 258)                        /* the copies' level against osc 1: SUPER's MIX 80 */

static void analog_note_on(track_t *t, voice_t *v)
{
    if (!t->p[P_A2DRFT]) {                            /* DRFT 0: the phases restart */
        if (!v->env && !v->env_out)
            v->ph[0] = v->ph[2] = 0;                  /* a fresh note: from phase 0 (an 808 starts the same every time) */
        v->ph[1] = v->ph[0] + 0x40000000u;
    }
    v->s[0] = v->s[1] = v->s[4] = v->s[5] = 0;        /* filter */
    v->s[6] = A2_NOCUT;
    v->s[7] = A2_ATK;                                 /* the filter envelope from 0 */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

/* damping (Q12) of RES: ANALOG's (2.0 .. 0.144) up to 100, then faster: 0 near 126, -0.03 at 127 */
static int32_t a2_k(int32_t res)
{
    int32_t k = 8192 - res * 7600 / 127, d = res - 100;
    return d > 0 ? k - ((d * d * 125) >> 7) : k;
}

/* tsvf_coef with the damping given (k may be a little below 0: den stays near 4096 or above) */
static void a2_coef(tsvf_t *c, int32_t cut, int32_t k)
{
    int32_t i = cut >> 8, g = SVF_G[i], den;
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 65536 + ((g * (g + k)) >> 8);              /* (Q16: a damping just below 0 still counts at low g) */
    c->a1 = (int32_t)((1u << 29) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}

/* ------------------------------------------------------------ kernels --- */
typedef void (*a2_osc_fn)(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n);
static void a2_saw(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    uint32_t i;
    (void)pw;
    for (i = 0; i < n; i++, ph += inc)
        b[i] += mulq15(osc_saw(ph, inc), g);
}
static void a2_pulse(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++, ph += inc)
        b[i] += mulq15(osc_pulse(ph, inc, pw), g);
}
static void a2_tri(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    uint32_t i;
    (void)pw;
    for (i = 0; i < n; i++, ph += inc)
        b[i] += mulq15(osc_tri(ph), g);
}
static void a2_sin(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    uint32_t i;
    (void)pw;
    for (i = 0; i < n; i++, ph += inc)
        b[i] += mulq15(osc_sine(ph), g);
}
static const a2_osc_fn A2_OSC[5] = {a2_saw, a2_pulse, a2_tri, a2_sin, a2_pulse};   /* N_ANALOG_WAVE order */

/* wave w at phase ph, no BLEP (the jumps of the sync) */
static int32_t a2_naive(uint32_t w, uint32_t ph, uint32_t pw)
{
    int32_t x = 0;
    A2_OSC[w](&x, ph, 0, w == 1u ? 0x80000000u : pw, 32768, 1);
    return x;
}

/* osc 2 (wave w2) restarts where osc 1 wraps: rendered in segments between the wraps; at a restart t
 * (of a sample) before sample i, osc 2 jumps by h: the polyBLEP (h / 2) t^2 on sample i - 1 (or on the
 * last of the block, when the wrap is the next block's first sample), -(h / 2) (1 - t)^2 on sample i,
 * less what osc 2's own BLEP counts there (the restart looks like a wrap of its phase: hn) */
static uint32_t a2_sync(int32_t *b, uint32_t w2, uint32_t ph0, uint32_t inc1, uint32_t ph1, uint32_t inc2,
                        uint32_t pw, int32_t g, uint32_t n)
{
    a2_osc_fn f = A2_OSC[w2];
    uint32_t i = 0, d1 = inc1 >> 15, d2 = inc2 >> 15, t, m;
    int32_t hn = w2 == 0u ? -65536 : w2 == 1u || w2 == 4u ? -32768 : 0, h, u, post;
    if (w2 == 1u)
        pw = 0x80000000u;
    for (;;) {
        post = 0;
        if (d1 && ph0 < inc1) {                       /* osc 1 wrapped t before sample i */
            t = ph0 / d1;
            h = a2_naive(w2, 0, pw) - a2_naive(w2, ph1 - t * d2, pw);
            if (i)
                b[i - 1] += mulq15(((h >> 2) * (int32_t)((t * t) >> 15)) >> 14, g);
            if (i == n)
                break;
            ph1 = t * d2;
            u = 32768 - (int32_t)t;
            post = -((((h - hn) >> 2) * ((u * u) >> 15)) >> 14);
        } else if (i == n) {
            break;
        }
        m = (~ph0) / inc1 + 1;                        /* samples to osc 1's next wrap */
        if (m > n - i)
            m = n - i;
        f(b + i, ph1, inc2, pw, g, m);
        b[i] += mulq15(post, g);
        ph0 += inc1 * m;
        ph1 += inc2 * m;
        i += m;
    }
    return ph1;
}

static void a2_noise(int32_t *b, int32_t *st, int32_t nz, uint32_t n)
{
    uint32_t i;
    int32_t s = *st;
    for (i = 0; i < n; i++)
        b[i] += mulq15((int32_t)(noise32(&s) >> 17) - 16384, nz);
    *st = s;
}

static void a2_drive(int32_t *b, int32_t drive, int32_t dw, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {                         /* (half scale: ANALOG's ((s >> 2) (drive >> 2)) >> 11) */
        int32_t s = b[i];
        b[i] = s + mulq15((softclip(((s >> 1) * (drive >> 2)) >> 11) >> 1) - s, dw);
    }
}

/* the states of the filter: linear up to K, then a tanh knee; the band-pass state (the resonance) from
 * 32768 (at most 64368), the low-pass one from 65536 (at most 128736: the products fit 32 bits). The
 * knee is a call (rare: a resonant peak), the test inline */
static __attribute__((noinline)) int32_t a2_knee(int32_t x, int32_t k, uint32_t sh)
{
    int32_t a = x < 0 ? -x : x;
    a = k + (softclip((a - k) >> sh) << sh);
    return x < 0 ? -a : a;
}
#define A2_SAT(x, K, SH)                                                       \
    do {                                                                      \
        if ((uint32_t)((x) + (K)) > 2u * (K))         /* (one compare: the usual case) */ \
            x = a2_knee(x, K, SH);                                            \
    } while (0)

/* the SVF step of tsvf_lp, the states saturating; *v1 the band-pass, returns the low-pass */
#define A2_SVF(in, v1, v2)                                                    \
    do {                                                                      \
        int32_t v3_ = (in) - ic2;                                             \
        v1 = (a1 * ic1 + a2 * v3_) >> 13;                                     \
        v2 = ic2 + ((a2 * ic1 + a3 * v3_) >> 13);                             \
        ic1 = 2 * v1 - ic1;                                                   \
        A2_SAT(ic1, 32768, 0);                                                \
        ic2 = 2 * v2 - ic2;                                                   \
        A2_SAT(ic2, 65536, 1);                                                \
    } while (0)
#define A2_FLT_KERNEL(name, OUT)                                                              \
    static void name(int32_t *b, int32_t *st, const tsvf_t *c, int32_t kd, uint32_t n)      \
    {                                                                                         \
        int32_t ic1 = st[0], ic2 = st[1], a1 = c->a1, a2 = c->a2, a3 = c->a3, x, v1, v2;     \
        uint32_t i;                                                                           \
        (void)kd;                                                                             \
        for (i = 0; i < n; i++) {                                                             \
            x = clamp(b[i], -65536, 65536);                                                   \
            A2_SVF(x, v1, v2);                                                                \
            b[i] = OUT;                                                                       \
        }                                                                                     \
        st[0] = ic1;                                                                          \
        st[1] = ic2;                                                                          \
    }
A2_FLT_KERNEL(a2_lp, v2)
A2_FLT_KERNEL(a2_bp, v1)
A2_FLT_KERNEL(a2_hp, x - ((kd * v1) >> 12) - v2)     /* HP = in - k bp - lp */
typedef void (*a2_flt_fn)(int32_t *b, int32_t *st, const tsvf_t *c, int32_t kd, uint32_t n);
static const a2_flt_fn A2_FLT[4] = {a2_lp, a2_lp, a2_bp, a2_hp};   /* LP12 LP24 BP HP */

static void a2_out(int32_t *out, const int32_t *b, int32_t amp0, int32_t amp1, uint32_t n)
{
    uint32_t i;
    int32_t acc = amp0 << 5, d = amp1 - amp0;         /* the amplitude, x 32 (n == CTL) */
    for (i = 0; i < n; i++, acc += d) {
        int32_t y = b[i], a = y < 0 ? -y : y;
        if (a > 16000) {                              /* ANALOG's soft knee after the filter */
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, acc >> 5), VOICE_FS) << 1;
    }
}

/* ------------------------------------------------------------- swarm --- */
static uint32_t voices_busy(void);                    /* voice.c */
static uint8_t super_nv;                              /* voices sounding, all parts (super_block) */
static void super_block(track_t *t)
{
    if (t->p[P_A2SWRM] > 2)                           /* (only a swarm asks: the idle part costs nothing) */
        super_nv = (uint8_t)voices_busy();
}

/* the copies the CPU allows: 6 up to 4 voices, 4 up to 6, 2 above (Jangada: 8 voices of 7 saws
 * measured 73 % on the FM-1 and lost voices to the shedder; capped like this, 55 %) */
static uint32_t super_copies(uint32_t want)
{
    if (want > 6u)
        want = 6;                                     /* a bad value (editor, old data) cannot overrun */
    if (want > 4u && super_nv > 4u)
        want = 4;
    if (want > 2u && super_nv > 6u)
        want = 2;
    return want;
}

/* ------------------------------------------------------------ render --- */
static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint32_t COPY_PH[6] = {0x2B7E1516u, 0x9E3779B9u, 0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au};
    static const int8_t COPY_AT[6] = {1, -1, 2, -2, 3, -3};   /* spread steps of copy k */
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] % 5u, w2 = p[P_A2WAVE] ? (uint32_t)(p[P_A2WAVE] - 1) % 5u : w1;
    uint32_t ftyp = (uint32_t)p[P_A2FTYP] & 3u, ncopy = super_copies((uint32_t)p[P_A2SWRM]), j, k;
    int32_t det = p[P_E1], m2 = p[P_E2] * 258, nz = p[P_E3] * 100, drv = p[P_E6], kd = a2_k(p[P_E5]);
    int32_t off = p[P_A2SEMI] * 16 + (p[P_A2SYNC] ? (m->shape - (64 << 8)) >> 5 : 0);   /* osc 2, 1/16 st */
    uint32_t inc1 = m->inc, inc2 = inc1, ph0 = v->ph[0], spr = v->ph[2], dinc = 0;
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15), pw1 = w1 == 1u ? 0x80000000u : pw;
    int32_t g1 = m2 ? (32767 - m2) >> 1 : 16384;     /* osc 1's gain: MIX, half scale */
    int32_t fe = v->s[7], fl = fe & ((1 << 25) - 1), cut, c0, b[CTL];
    tsvf_t f1, f2;
    if (det || off) {   /* DTN in cents: whole 1/16 semitones from the table, the rest as a fine factor */
        int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;        /* rem: 1/1600 semitone */
        inc2 = PITCH_INC[clamp(m->pitch16 + off + d16, 0, 2047)];
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    }
    if (p[P_A2DRFT]) {                                /* osc 1 one way, osc 2 the other */
        int32_t d = super_drift(&v->s[3], &v->s[2], p[P_A2DRFT]);
        inc1 += (uint32_t)((int32_t)(inc1 >> 12) * d);
        inc2 -= (uint32_t)((int32_t)(inc2 >> 12) * d);
    }
    for (j = 0; j < n; j++)
        b[j] = 0;
    if (ncopy) {                                      /* the swarm: SUPER's spread, levels and phases */
        int32_t cg = (32767 * 1024) / (1024 + (((int32_t)ncopy * A2_SWARM_GC) >> 5));
        int32_t gc = mulq15(mulq15(A2_SWARM_GC, cg), g1 << 1);
        dinc = (inc1 >> 16) * (uint32_t)(clamp(p[P_A2SDTN], 0, 127) * 60 * 2367 * 16 / (127 * 3 * 1000));
        for (k = 0; k < ncopy; k++)
            A2_OSC[w1](b, ph0 + (uint32_t)(int32_t)COPY_AT[k] * spr + COPY_PH[k],
                       inc1 + (uint32_t)(int32_t)COPY_AT[k] * dinc, pw1, gc, n);
        g1 = mulq15(g1, cg);
    }
    A2_OSC[w1](b, ph0, inc1, pw1, g1, n);
    if (m2) {                                         /* osc 2 (MIX 0, the 808s and subs: one oscillator) */
        if (p[P_A2SYNC])
            v->ph[1] = a2_sync(b, w2, ph0, inc1, v->ph[1], inc2, pw, m2 >> 1, n);
        else {
            A2_OSC[w2](b, v->ph[1], inc2, w2 == 1u ? 0x80000000u : pw, m2 >> 1, n);
            v->ph[1] += inc2 * n;
        }
    } else {
        v->ph[1] += inc2 * n;
    }
    v->ph[0] = ph0 + inc1 * n;
    v->ph[2] = spr + dinc * n;
    if (nz)
        a2_noise(b, &v->s[2], nz, n);
    if (drv)                                          /* DRV: dry -> driven (1x .. 4x; a clean range low) */
        a2_drive(b, 32768 + drv * 768, drv * 258, n);
    if (fe & A2_ATK) {                                /* the filter envelope: AD, once a block */
        fl += (int32_t)ENV_LIN[p[P_A2FATK] & 127];
        if (fl >= (1 << 24))
            fl = fe = 1 << 24;                        /* the top: decaying from the next block */
        else
            fe = fl | A2_ATK;
    } else {
        fl -= mulq16(fl, ENV_EXP[p[P_A2FDEC] & 127]);
        fe = fl;
    }
    v->s[7] = fe;
    cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4) + (((fl >> 9) * p[P_A2FENV]) >> 7);
    cut = clamp(cut, 0, 127 << 8);
    c0 = v->s[6] == A2_NOCUT ? cut : v->s[6];
    v->s[6] = cut;
    for (j = 0, k = c0 == cut ? 1u : 4u; j < k; j++) {   /* the cutoff in 4 steps a block when it moves */
        uint32_t a = j * n / k, e = (j + 1u) * n / k;
        int32_t c = c0 + (((cut - c0) * (int32_t)(j + 1u)) >> 2);
        a2_coef(&f1, c, kd);
        A2_FLT[ftyp](b + a, &v->s[0], &f1, kd, e - a);
        if (ftyp == 1u) {                             /* LP24: the second stage at Q 0.7 */
            a2_coef(&f2, c, 5793);
            a2_lp(b + a, &v->s[4], &f2, 0, e - a);
        }
    }
    a2_out(out, b, m->amp0, m->amp1, n);
}
