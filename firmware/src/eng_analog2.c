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
 * are worked out every 16 samples, from the last block's cutoff to this one's, and stepped linearly per
 * sample in between (a2_filter; ANALOG: once a block, no steps).
 *
 * Kernels (the asm porting guide). analog_render works a block (n = CTL = 32 samples) through one buffer
 * b[] of the voice, half scale (the filter's input level: ANALOG's s >> 1), kernel by kernel; each is a
 * plain static function with one simple loop, no branch in it but the polyBLEP's / the saturation's
 * (rare), its state loaded into locals before the loop and stored after; the path (wave, filter mode,
 * swarm, osc 2, sync, a still or a moving cutoff) is chosen per block. n is CTL or A2_SEG (CTL / 2, the
 * cutoff's segments), but an oscillator kernel also gets the segments of a2_sync (any n >= 1). The C is
 * the reference; with FELUCCA_ASM (the target's default) the kernels marked [asm] run as the pi32v2 asm
 * of hal/fm1_dsp_asm.h, bit-identical (FELUCCA_ASM_CHECK=1 compares them with the C at run time). The
 * products fit 32 bits: no 64-bit MAC.
 *   [asm] a2_saw (the saws: osc 1, osc 2, an odd swarm copy) 7 instructions a sample (C 14); a2_saw2 two
 *         swarm copies of SAW at once, 13 a sample (the slot of a 2 x 16-bit SIMD version).
 *   osc  A2_OSC[w](b, ph, inc, pw, g, n)  b[i] += mulq15(wave(ph + i inc), g), w: SAW SQR TRI SIN PWM
 *        (SQR: pw 0x80000000). No state (phases are the caller's). g 32768: exactly the wave. Bound: |b|
 *        stays below 65536 up to the filter (osc 1 + 6 copies at most 29300, osc 2 16384, noise 6350;
 *        drive never raises it), so the filter's first stage needs no clamp of its input.
 *   a2_sync(b, w2, ph0, inc1, ph1, inc2, pw, g, n) -> ph1 after n: osc 2 hard-synced to osc 1, as
 *        segments of A2_OSC[w2] between osc 1's wraps, a correction on the two samples around each.
 *   a2_noise(b, &nst, nz, n)  b[i] += mulq15(noise - 16384, nz); nst the xorshift state (v->s[2]).
 *   a2_drive(b, drive, dw, n) b[i] += mulq15(softclip(b[i] drive / 2^14) / 2 - b[i], dw) (ANALOG's DRV).
 *   a2_lp / a2_bp / a2_hp / a2_lp2(b, st, c, kd, n)  the SVF in place: b[i] = low / band / high-pass
 *        of b[i]; the states saturate (A2_SAT: one compare inline, a2_knee called past it: band-pass
 *        32768, low-pass 65536); st[0..1] its states (v->s[0..1]; LP24's second stage a2_lp2, v->s[4..5],
 *        its input clamped to +-65536: the first stage's low-pass reaches 128736); kd the damping (HP).
 *        Live in the loop: b, n, ic1, ic2, a1..a3 and the temporaries (pi32: no spill).
 *   ..._i(b, st, c, d, kd, n)  the same with a moving cutoff: c the coefficients of sample 0, d added
 *        to them after each sample (+3 live values, 3 adds a sample; pi32: no spill).
 *   a2_filter(b, s, c0, cut, ftyp, kd)  picks them: still (c0 == cut) one a2_coef and the plain kernel;
 *        moving, a2_coef at the two segment ends, d rounded to the nearest (the segment ends within
 *        A2_SEG / 2 units of the exact coefficient, the next starts on it).
 *   a2_out(out, b, amp0, amp1, n)  out[i] += the soft knee of b[i] (16000), x 2 x the amplitude ramp
 *        amp0 -> amp1 (Q15) x VOICE_FS.
 * Voice state: ph[0] / ph[1] the oscillators, ph[2] the swarm's spread phase; s[0..1] the filter, s[4..5]
 * its second stage, s[2] the noise generator (and the drift's), s[3] the drift, s[6] the last block's
 * cutoff (A2_NOCUT: a fresh note), s[7] the filter envelope (Q24, | A2_ATK while it rises). voice.c
 * voice_start keeps s[0..1], s[4..6] on a retrigger. */
#include "../hal/fm1_dsp_asm.h"                    /* FELUCCA_ASM: kernels in pi32v2 asm */
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
/* FELUCCA_ASM: the hottest kernels run as the pi32v2 asm of hal/fm1_dsp_asm.h; the C below stays the
 * reference (the host build) and, with FELUCCA_ASM_CHECK=1 (a verification build, not for release),
 * runs next to the asm on a copy: a2_asm_check counts the calls and the blocks that differ. */
#if FELUCCA_ASM
#define A2_REF(name) name##_c
#else
#define A2_REF(name) name
#endif
#if FELUCCA_ASM_CHECK
struct { uint32_t calls, bad; } a2_asm_check;     /* read by the emulator (play_check peek:a2_asm_check:2) */
static void a2_asm_cmp(const int32_t *a, const int32_t *b, uint32_t n)
{
    uint32_t i, bad = 0;
    for (i = 0; i < n; i++)
        bad |= (uint32_t)(a[i] != b[i]);
    a2_asm_check.calls++;
    a2_asm_check.bad += bad;
}
#define A2_CHECK_PRE(b, n)                                                                              \
    int32_t ref_[CTL];                                                                                  \
    uint32_t k_;                                                                                        \
    for (k_ = 0; k_ < (n); k_++)                                                                        \
        ref_[k_] = (b)[k_];
#define A2_CHECK_POST(b, n) a2_asm_cmp(b, ref_, n);
#else
#define A2_CHECK_PRE(b, n)
#define A2_CHECK_POST(b, n)
#endif

typedef void (*a2_osc_fn)(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n);
static void A2_REF(a2_saw)(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    uint32_t i;
    (void)pw;
    for (i = 0; i < n; i++, ph += inc)
        b[i] += mulq15(osc_saw(ph, inc), g);
}
#if FELUCCA_ASM
static void a2_saw(int32_t *b, uint32_t ph, uint32_t inc, uint32_t pw, int32_t g, uint32_t n)
{
    if (!n)
        return;
    {
        A2_CHECK_PRE(b, n)
#if FELUCCA_ASM_CHECK
        a2_saw_c(ref_, ph, inc, pw, g, n);
#endif
        asm_saw_acc(b, ph, inc, g, n);
        A2_CHECK_POST(b, n)
    }
}
/* two saws of one gain (two copies of the swarm), sample by sample: the sums of two a2_saw */
static __attribute__((noinline)) void a2_saw2(int32_t *b, uint32_t ph1, uint32_t inc1, uint32_t ph2, uint32_t inc2,
                                              int32_t g, uint32_t n)
{
    A2_CHECK_PRE(b, n)
#if FELUCCA_ASM_CHECK
    a2_saw_c(ref_, ph1, inc1, 0, g, n);
    a2_saw_c(ref_, ph2, inc2, 0, g, n);
#endif
    asm_saw2_acc(b, ph1, inc1, ph2, inc2, g, n);
    A2_CHECK_POST(b, n)
}
#endif
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
/* two twins per mode: name (still: c the coefficients) and name##_i (moving: c at sample 0, d their
 * step per sample, one add each) */
#define A2_FLT_LOOP(IN, OUT, STEP)                                                                    \
    {                                                                                                 \
        int32_t ic1 = st[0], ic2 = st[1], a1 = c->a1, a2 = c->a2, a3 = c->a3, x, v1, v2;             \
        uint32_t i;                                                                                   \
        (void)kd;                                                                                     \
        for (i = 0; i < n; i++) {                                                                     \
            x = IN;                                                                                   \
            A2_SVF(x, v1, v2);                                                                        \
            b[i] = OUT;                                                                               \
            STEP                                                                                      \
        }                                                                                             \
        st[0] = ic1;                                                                                  \
        st[1] = ic2;                                                                                  \
    }
#define A2_FLT_KERNEL(name, IN, OUT)                                                                  \
    static void name(int32_t *b, int32_t *st, const tsvf_t *c, int32_t kd, uint32_t n)              \
    A2_FLT_LOOP(IN, OUT, )                                                                            \
    static void name##_i(int32_t *b, int32_t *st, const tsvf_t *c, const tsvf_t *d, int32_t kd, uint32_t n) \
    {                                                                                                 \
        const int32_t d1 = d->a1, d2 = d->a2, d3 = d->a3;                                             \
        A2_FLT_LOOP(IN, OUT, a1 += d1; a2 += d2; a3 += d3;)                                           \
    }
A2_FLT_KERNEL(a2_lp, b[i], v2)                       /* (the input: |b| < 65536, the osc contract) */
A2_FLT_KERNEL(a2_bp, b[i], v1)
A2_FLT_KERNEL(a2_hp, b[i], x - ((kd * v1) >> 12) - v2)     /* HP = in - k bp - lp */
A2_FLT_KERNEL(a2_lp2, clamp(b[i], -65536, 65536), v2)      /* LP24's second stage: its input (the first
                                                            * stage's low-pass, up to 128736) clamped */
typedef void (*a2_flt_fn)(int32_t *b, int32_t *st, const tsvf_t *c, int32_t kd, uint32_t n);
typedef void (*a2_flt_i_fn)(int32_t *b, int32_t *st, const tsvf_t *c, const tsvf_t *d, int32_t kd, uint32_t n);
static const a2_flt_fn A2_FLT[5] = {a2_lp, a2_lp, a2_bp, a2_hp, a2_lp2};   /* LP12 LP24 BP HP, LP24's 2nd */
static const a2_flt_i_fn A2_FLT_I[5] = {a2_lp_i, a2_lp_i, a2_bp_i, a2_hp_i, a2_lp2_i};

/* the filter over a block (CTL samples), the cutoff going from c0 (the last block's) to cut: still, one
 * set of coefficients; moving, they are worked out at c0, halfway and cut and stepped linearly per sample
 * over each half (A2_SEG samples: one add per coefficient per sample, no divide in the loop; a segment
 * ends at most A2_SEG - 1 units short of the next one's exact start). LP24: stage 2 (Q 0.7) after stage 1,
 * over the whole block, its states s[4..5] */
#define A2_SEG_LOG2 (CTL_LOG2 - 1)
#define A2_SEG (1u << A2_SEG_LOG2)
static void a2_filter(int32_t *b, int32_t *s, int32_t c0, int32_t cut, uint32_t ftyp, int32_t kd)
{
    uint32_t st, j;
    for (st = 0; st <= (ftyp == 1u); st++) {
        uint32_t m = st ? 4u : ftyp;
        int32_t k = st ? 5793 : kd;
        tsvf_t c, e, d;
        a2_coef(&c, c0, k);
        if (c0 == cut) {
            A2_FLT[m](b, s + 4 * st, &c, kd, CTL);
            continue;
        }
        for (j = 0; j < 2u; j++) {
            a2_coef(&e, c0 + (((cut - c0) * (int32_t)(j + 1u)) >> 1), k);
            d.a1 = (e.a1 - c.a1 + (int32_t)(A2_SEG / 2u)) >> A2_SEG_LOG2;   /* (rounded: no drift) */
            d.a2 = (e.a2 - c.a2 + (int32_t)(A2_SEG / 2u)) >> A2_SEG_LOG2;
            d.a3 = (e.a3 - c.a3 + (int32_t)(A2_SEG / 2u)) >> A2_SEG_LOG2;
            c.a1 += d.a1;                             /* sample i at (i + 1) / A2_SEG of the way: the */
            c.a2 += d.a2;                             /* last one at the segment's end, as the cutoff */
            c.a3 += d.a3;
            A2_FLT_I[m](b + j * A2_SEG, s + 4 * st, &c, &d, kd, A2_SEG);
            c = e;
        }
    }
}

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
#if FELUCCA_ASM_CHECK
/* the edges the presets may not reach (phases at the wraps, increments up to 2^31, odd n): every asm
 * kernel against its C on random buffers, once at the first render; counted in a2_asm_check */
static uint32_t a2_rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}
static void a2_asm_selftest(void)
{
    static const uint32_t INC[] = {0, 1, 32767, 32768, 65535, 65536, 1000000, 60000000, 0x3FFFFFFFu,
                                   0x40000000u, 0x50000000u, 0x7FFFFFF0u};
    static const int32_t G[] = {32767, 16384, -5000, 1, 32768};
    static const uint32_t N[] = {1, 2, 7, 16, 32};
    uint32_t s = 12345, i, j, k, r;
    int32_t b[CTL];
    for (i = 0; i < sizeof INC / sizeof INC[0]; i++)
        for (j = 0; j < 8u; j++)
            for (k = 0; k < sizeof N / sizeof N[0]; k++) {
                uint32_t inc = INC[i], ph = j == 0 ? 0 : j == 1 ? 0u - inc : j == 2 ? inc - 1u : j == 3 ? 0xFFFFFFFFu
                                                     : a2_rnd(&s);
                int32_t g = G[(j + k) % (sizeof G / sizeof G[0])];
                for (r = 0; r < CTL; r++)
                    b[r] = (int32_t)(a2_rnd(&s) >> 15) - 65536;
                a2_saw(b, ph, inc, 0, g, N[k]);
                a2_saw2(b, ph, inc, j < 4u ? 0u - ph : a2_rnd(&s), INC[(i + j) % (sizeof INC / sizeof INC[0])], g,
                        N[k]);
            }
}
#endif

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
#if FELUCCA_ASM_CHECK
    static uint8_t tested;
    if (!tested) {
        tested = 1;
        a2_asm_selftest();
    }
#endif
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
        k = 0;
#if FELUCCA_ASM
        if (w1 == 0u)                                 /* saws: two copies a pass (the SIMD slot) */
            for (; k + 1u < ncopy; k += 2u)
                a2_saw2(b, ph0 + (uint32_t)(int32_t)COPY_AT[k] * spr + COPY_PH[k],
                        inc1 + (uint32_t)(int32_t)COPY_AT[k] * dinc,
                        ph0 + (uint32_t)(int32_t)COPY_AT[k + 1u] * spr + COPY_PH[k + 1u],
                        inc1 + (uint32_t)(int32_t)COPY_AT[k + 1u] * dinc, gc, n);
#endif
        for (; k < ncopy; k++)
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
    a2_filter(b, v->s, c0, cut, ftyp, kd);
    a2_out(out, b, m->amp0, m->amp1, n);
}
