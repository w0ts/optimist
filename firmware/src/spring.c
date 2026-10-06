/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: the SPRING reverb,
 * hugelton/Felucca 727f272, fx.c rev_spring / rev_clear / the model change in fx_buses) */
/* SPRING (FELUCCA_SPRING, backports.h): FX > REVERB > TYPE picks the reverb bus' model, ROOM (fx.c's feedback
 * delay network) or SPRING. Included by fx.c, after rev_step.
 *
 * Felucca's model, as it wrote it: one spring of a spring tank, mono like the other buses there: the input and
 * the loop's return -> a low cut (~110 Hz: a spring carries little bass) -> SP_N stretched first-order
 * allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below fs / 8 = 5.5 kHz the group
 * delay rises with frequency, the chirp; each pass round the loop adds more of it: the "boing", the drips) ->
 * the loop's delay line (SP_LEN) -> back through a one-pole low-pass (DAMP) and the decay gain (SIZE). The
 * output: the spring's far end, half way along the loop (the first sound 15 .. 30 ms after the send: the
 * tank's own pre-delay; a slow wobble of a sample or two on it), plus a second, quieter pickup at three
 * quarters (a shorter spring beside it: denser). SIZE sets the loop's length (30 .. 60 ms) and its decay; DAMP
 * the loop's low-pass. Changing the model fades the old one's block out and clears both models' buffers.
 *
 * Here: the loop's line is the ROOM's rev_line (no RAM of its own but the allpasses' 176 B); the same output
 * goes to both sides (a spring is mono); the bus is skipped when idle as the ROOM is (fx.c: exactly 0, its
 * zero-write count, its filters at 0), so a tail rings out to the last LSB and resumes bit-identically; the
 * loop runs from RAM (HOT2: RAMTEXT is full), the rare model change from XIP. SP_GAIN: the output level, matched to the ROOM's
 * (tests/backports_test.c: the two models' wet RMS on the same send within 3 dB). */
#define SP_LEN 4096u                     /* the loop's line in rev_line (8684 samples; REV_HALF 4346) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
#define SP_GAIN 512                      /* the output, Q8 (see above): x2, Felucca's level -5.9 dB under our ROOM */
_Static_assert(sizeof rev_line / 2u >= SP_LEN, "SPRING in the ROOM's line");
static int32_t sp_ap[4u * (SP_N + 1u)];  /* the chain: stage k's output 4 samples ago, per phase (wp & 3) */
static struct {
    uint32_t ph;                         /* the output tap's wobble */
    uint32_t q;                          /* samples since it last wrote a non-zero value (fx.c fx_q) */
    int32_t lp, hp, he, size;            /* the loop's low-pass, low cut (and its remainder), the loop length
                                          * (Q8, glides) */
    uint16_t w;                          /* the loop's write index (SP_MASK) */
    uint8_t type;                        /* the model running: 0 ROOM, 1 SPRING */
} sp = {.q = FX_Q_MAX};

/* one block of SPRING, added to both sides */
static HOT2 __attribute__((noinline)) void spring_run(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w, wv = 0;
    int16_t *ln = rev_line;
    if (!sp.size)
        sp.size = len;
    sp.size += clamp(len - sp.size, -256, 256);         /* SIZE glides (a sample a block at most) */
    L = sp.size >> 8;
    sp.ph += 2u * LFO_INC[24];                          /* the wobble: a slow sine, 1.5 samples deep */
    w = (sp.size >> 1) + ((osc_sine(sp.ph) * 3) >> 8);  /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    if (sp.q >= SP_LEN && !(sp.lp | sp.hp) && !fx_any(rev_in, n)) {
        sp.w = (uint16_t)(sp.w + n);                    /* idle: every cell 0, the output 0 (sp.he alone */
        return;                                         /* moves nothing while the rest is 0) */
    }
    for (i = 0; i < n; i++) {
        uint32_t wp = sp.w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o, y;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        sp.lp += mulq15(r - sp.lp, kl);
        o = sp.lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - sp.hp + sp.he;                          /* the low cut, its step's remainder kept (as */
        sp.he = o & 63;                                 /* dc_block): no dead band to hold an offset in the loop */
        sp.hp += o >> 6;
        x -= sp.hp;
        p = sp_ap[j];                                   /* the chain: sp_ap[j + k], stage k's output 4 samples ago */
        sp_ap[j] = x;
        wv |= x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - sp_ap[j + k]) * SP_A;      /* towards 0, as the loop's gain: floors would feed */
            o = sp_ap[j + k];                           /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            sp_ap[j + k] = x;
            wv |= x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        sp.w = (uint16_t)(wp + 1u);
        y = ((t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2) * SP_GAIN >> 8;
        wl[i] += y;
        wr[i] += y;
    }
    sp.q = fx_q(sp.q, wv, n);
}

/* both models' buffers and states to silence (the model changed; XIP: rare) */
static void spring_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_line / 2u; i++)
        rev_line[i] = 0;
    for (i = 0; i < sizeof rev_ap / 2u; i++)
        rev_ap[i] = 0;
    for (i = 0; i < sizeof sp_ap / 4u; i++)
        sp_ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.line_lp[i] = 0;
    sp.lp = sp.hp = sp.he = 0;
#if FELUCCA_REV_HALF
    for (i = 0; i < 16u; i++)                           /* (REV_HALF: the ROOM's filters) */
        rev_half.e[i] = rev_half.l[i] = rev_half.r[i] = rev_half.o[i & 7u] = 0;
#endif
    sp.q = fx.rev_q = FX_Q_MAX;                         /* (both idle: every cell 0) */
}

/* the ROOM (fx.c's network, as fx_buses runs it without the switch): run = not idle (fx_buses: run_r), else the
 * indices move on. In a SPRING build every ROOM block comes through here (one call site of rev_step: the host
 * build, which ignores always_inline, keeps it inlined) */
static HOT2 __attribute__((noinline)) void room_run(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n,
                                                    int32_t ma, int32_t mb, int32_t g, int32_t lpk, int run,
                                                    int32_t *wv)
{
    uint32_t i;
#if FELUCCA_REV_HALF
    (void)i;
    if (run)
        rev_half_run(rev_in, wl, wr, n, ma, mb, g, lpk, wv);   /* (fx.c: the tank at half the rate) */
    else
        rev_half_skip(n);
#else
    if (run) {
        for (i = 0; i < n; i++) {
            int32_t rr;
            wl[i] += rev_step(rev_in[i], ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, lpk, &rr, wv);
            wr[i] += rr;
        }
    } else {
        fx.ap_i[0] = fx_wrap(fx.ap_i[0], n, REV_AP[0]);       /* (n <= CTL: shorter than every line) */
        fx.ap_i[1] = fx_wrap(fx.ap_i[1], n, REV_AP[1]);
        fx.line_i[0] = fx_wrap(fx.line_i[0], n, REV_L0);
        fx.line_i[1] = fx_wrap(fx.line_i[1], n, REV_LINE[1]);
        fx.line_i[2] = fx_wrap(fx.line_i[2], n, REV_LINE[2]);
        fx.line_i[3] = fx_wrap(fx.line_i[3], n, REV_LINE[3]);
    }
#endif
}

/* the model changed (XIP: rare; called through a pointer): the old one's block fades out, both are cleared, the
 * new one starts from silence next block */
static __attribute__((noinline)) void spring_switch(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n,
                                                    int32_t ma, int32_t mb, int32_t g, int32_t lpk, uint32_t want)
{
    int32_t tl[CTL], tr[CTL], gg = 65536, d = 65536 / (int32_t)n, wv = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        tl[i] = tr[i] = 0;
    if (sp.type)
        FAR(spring_run)(rev_in, tl, tr, n);
    else
        FAR(room_run)(rev_in, tl, tr, n, ma, mb, g, lpk, 1, &wv);
    for (i = 0; i < n; i++, gg -= d) {
        wl[i] += mulq16(tl[i], (uint32_t)gg);
        wr[i] += mulq16(tr[i], (uint32_t)gg);
    }
    spring_clear();
    sp.type = (uint8_t)want;
}

/* the reverb bus in a SPRING build (fx_buses): the model change, SPRING, or the ROOM. ma, mb: line 0's modulation
 * at both ends of the block; g, lpk: the ROOM's loop gain and damping; run: the ROOM is not idle; *wv: what the
 * ROOM wrote (its zero-write count, fx_buses) */
static HOT2 void spring_bus(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n, int32_t ma, int32_t mb,
                            int32_t g, int32_t lpk, int run, int32_t *wv)
{
    uint32_t want = bp_set[BPS_RTYPE] == 1;
    if (want != sp.type)
        FAR(spring_switch)(rev_in, wl, wr, n, ma, mb, g, lpk, want);
    else if (sp.type)
        spring_run(rev_in, wl, wr, n);
    else
        room_run(rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
}
