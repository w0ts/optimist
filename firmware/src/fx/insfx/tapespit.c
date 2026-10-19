/* SPDX-License-Identifier: GPL-3.0-only */
/* TAPE and SPIT, two insert types of the shapers' range (fx_slots.c FXT_TAPE FXT_SPIT, 2026-10-19; included by insfx.c),
 * after Melodee 1.3.0's inserts (keremimo/melodee v1.4.4, firmware/src/fx.c fx_tape_design / fx_tape and fx_design's
 * FX_SPIT with fx_up's drive), by Kerem Kilic, GPL-3.0-only. Rewritten in fixed point at the full rate through the
 * SVF of dsp.c (tsvf_tick), its places on the CUTOFF_HZ scale, no divide in RAM code:
 *   TAPE  three low-passes: one at ~200 Hz and one at ~1.4 kHz around a random place (a new one every ~2 s), their
 *         difference (the mids) added back inverted when the place says so (amount above 32), then a low-pass from
 *         6.5 kHz (at 1) down to 1.7 kHz (at 127). The amount is Melodee's CHARACTER: how far the places wander and
 *         how dark it gets; the inversion glides over ~15 ms (no click)
 *   SPIT  one band-pass at a random place from 180 Hz to 5.9 kHz (a new one ~2.6 times a second), Q 1 .. 20 along
 *         the amount (its peak gain 1), into a soft clip at a drive of 1 .. 16 relative to the input's peak (as DIST:
 *         Melodee's clip has a fixed ceiling, which made a soft part 15 dB louder), mixed over the dry at 50 % .. 100 %
 * Melodee's separate RATE and MIX are fixed here (one amount a type, D4). Each fades in and out over 2.9 ms; at 0 and
 * faded nothing runs; after IFX_QUIET blocks of silence in it rests (its states 0) */
#if FELUCCA_INS_TAPE || FELUCCA_INS_SPIT
typedef struct {
    int32_t w;                                          /* the wet share (Q15) */
    int32_t z[3][2];                                    /* the SVFs' states (TAPE: low, mid, the tone; SPIT: z[0]) */
    int32_t c[3];                                       /* their places now, CUTOFF_HZ index << 8 (gliding to the target) */
    int32_t bl, tb;                                     /* TAPE's inversion now and its target, Q12: 0 .. -8192 (-2) */
    int32_t env, g, kq;                                 /* SPIT: the input's peak, 2^31 / it, the band's 1 / Q (Q12) */
    uint32_t ph;                                        /* the random place's clock */
    int32_t rnd, r;                                     /* the noise's state, the random place (0 .. 127) */
    uint8_t a, q;                                       /* the last amount (a fade-out runs at it), blocks of silence in */
} ifx_ts_t;
#if FELUCCA_INS_TAPE
static ifx_ts_t ifx_tape[IFX_NI];
#endif
#if FELUCCA_INS_SPIT
static ifx_ts_t ifx_spit[IFX_NI];
#endif
#define IFX_TP_RATE 36                                  /* a new TAPE place every ~2 s (LFO_INC: 0.5 Hz at 44) */
#define IFX_SP_RATE 75                                  /* SPIT's ~2.6 times a second */
#define IFX_TS_K 8192                                   /* TAPE's low-passes: damping 2 (Q 0.5, Melodee's two one-poles) */

/* the block's coefficients into c (flash, FAR: tsvf_coef_k divides): the clock, a new random place on its wrap, the
 * places' glide (1/8 a block, ~6 ms), TAPE's inversion target. fresh: from rest (the places at their targets) */
static __attribute__((noinline)) void ifx_ts_block(ifx_ts_t *s, uint32_t spit, uint32_t n, int fresh, tsvf_t *c, int32_t pk)
{
    uint32_t old = s->ph, k;
    int32_t a = s->a, t[3];
    if (!s->rnd)
        s->rnd = (int32_t)(0x2545F491u ^ (uint32_t)(uintptr_t)s);
    s->ph += IFX_INC(spit ? IFX_SP_RATE : IFX_TP_RATE) * n;
    if (fresh || s->ph < old)
        s->r = (int32_t)(noise32(&s->rnd) >> 25);
    if (spit) {
        t[0] = 9277 + ((s->r * 18074) >> 7);            /* 180 Hz (36.2) .. 5.9 kHz (106.8) on CUTOFF_HZ */
        t[1] = t[2] = t[0];
    } else {
        int32_t d = (s->r - 64) * a;                    /* (+-8128) */
        t[0] = 9823 + ((d * 45) >> 8);                  /* ~200 Hz (38.4), +-32 % at 127 */
        t[1] = 19901 + ((d * 161) >> 8);                /* ~1.4 kHz (77.7), +-2.4 octaves at 127 */
        t[1] = t[1] > t[0] + 2048 ? t[1] : t[0] + 2048; /* (at least ~1 octave above the low one) */
        t[2] = 27853 - a * 55;                          /* 6.5 kHz (108.8) .. 1.7 kHz (81.5) */
        s->tb = a > 32 && (s->r & 64) ? -8192 : 0;
    }
    s->kq = 81920 / (20 + 3 * a);                       /* (SPIT: k = 1 / Q, Q = 1 + 0.15 a) */
    for (k = 0; k < 3u; k++) {
        s->c[k] = fresh ? t[k] : s->c[k] + ((t[k] - s->c[k]) >> 3);
        tsvf_coef_k(&c[k], s->c[k], spit ? s->kq : IFX_TS_K);
    }
    if (spit) {                                         /* the peak: instant up, ~23 ms down (1/16 a block) */
        s->env = fresh ? pk : s->env - (s->env >> 4);
        s->env = pk > s->env ? pk : s->env;
        s->g = (int32_t)(0x80000000u / (uint32_t)(s->env > 256 ? s->env : 256));
    }
    if (fresh)
        s->bl = s->tb;
}

/* TAPE (spit 0) or SPIT (1) at amount a on n samples of b; sh: b's headroom (a part's 3, the bus's 2) */
static HOT2 __attribute__((noinline)) void ifx_ts_run(ifx_ts_t *s, uint32_t spit, int32_t a, int32_t *b, uint32_t n,
                                                      uint32_t sh)
{
    tsvf_t c[3];
    uint32_t i;
    int32_t w = s->w, to = a ? 32768 : 0, bl, tb, drv, m, bp, pk;
    int fresh = !w;
    if (!a && !w)
        return;
    if (a)
        s->a = (uint8_t)a;
    if (!w || ifx_quiet(&s->q, b, n)) {
        ifx_zero(s->z, sizeof s->z);                    /* starting, or resting on silence: no stale filters */
        if (w) {
            s->w = to;
            return;
        }
    }
    pk = 0;
    for (i = 0; spit && i < n; i++) {                   /* (SPIT: the block's peak, at the SVF's scale) */
        int32_t v = clamp(b[i] >> sh, -37500, 37500);
        v = v < 0 ? -v : v;
        pk = v > pk ? v : pk;
    }
    FAR(ifx_ts_block)(s, spit, n, fresh, c, pk);
    bl = s->bl, tb = s->tb;
    drv = 1 + (s->a >> 3);                              /* SPIT: 1 .. 16 */
    m = 64 + (s->a >> 1);                               /* its wet over the dry, Q7: 50 % .. 100 % */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], xs = clamp(x >> sh, -37500, 37500), y;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        if (spit) {
            int32_t u;
            tsvf_tick(&c[0], xs >> 3, &s->z[0][0], &s->z[0][1], &bp);   /* (in at 1/8: bp's peak is Q x its input) */
            u = clamp(((bp * s->kq) >> 9), -100000, 100000) * drv;      /* (k bp: peak gain 1, back at xs's scale) */
            y = (int32_t)(((int64_t)softclip((int32_t)(((int64_t)u * s->g) >> 16)) * s->env) >> 15);   /* (the clip at
                                                                    * the input's peak: as loud as it came) */
            y = xs + (((y - xs) * m) >> 7);
        } else {
            int32_t lo = tsvf_lp(&c[0], xs, &s->z[0][0], &s->z[0][1]);
            int32_t up = tsvf_lp(&c[1], xs, &s->z[1][0], &s->z[1][1]);
            bl += clamp(tb - bl, -12, 12);              /* (~15 ms from 0 to -2) */
            y = tsvf_lp(&c[2], clamp(xs + (((up - lo) * bl) >> 12), -120000, 120000), &s->z[2][0], &s->z[2][1]);
        }
        b[i] = ifx_mix(x, y << sh, w);
    }
    s->w = w, s->bl = bl;
    if (!w)
        ifx_zero(s->z, sizeof s->z);                    /* (faded out: it restarts from rest) */
}
/* type ty's state on instance i (0: not built) */
AINL ifx_ts_t *ifx_ts_st(uint32_t ty, uint32_t i)
{
#if FELUCCA_INS_TAPE
    if (ty == FXT_TAPE)
        return &ifx_tape[i];
#endif
#if FELUCCA_INS_SPIT
    if (ty == FXT_SPIT)
        return &ifx_spit[i];
#endif
    (void)ty, (void)i;
    return 0;
}
#endif
