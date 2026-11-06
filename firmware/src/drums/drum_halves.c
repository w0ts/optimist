/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The drum track's voices in two halves, a render job each, so the two cores share them (2026-11-05, the user's FM-1
 * with only drums playing: CPU0 at 60-75 %, CPU1 at 0 %; "basically every task/job should spread"). Included by the
 * split's owners after fx.c and slicer.c: fm1-base's core/opt_dual.c (the release) and system/dual.c (Optimist's own).
 *
 *   before the fork (CPU0)  dh_pre: the SLICER on (or still fading)? then one job as before (DH_ONE: slicer_drums and
 *                           dbus_run, either core). Else the SLICER's clock, the values every voice reads (drums.c
 *                           drums_pre: the lanes' sends, the glides, the gains), and the lanes of each half (dh_lanes);
 *                           nothing sounding: the drum job runs the drum bus alone (DH_IDLE, either core)
 *   the two jobs            dh_job(0): half A's voices onto the buses themselves (as the drum job wrote them: CPU0's parts
 *                           go to their accumulators until the join), the X0X channels with them; dh_job(1): half B's
 *                           voices into its own buffers (dhb). Either core runs either half
 *   after the join (CPU0)   dh_join: half B added onto the buses, the declick tail of the voices cut before this block,
 *                           what both halves cut added to it, the peak, the times (drums_fold); then the caller runs the
 *                           drum bus (dbus_run: its inserts on the drums summed) before the parts' sums go on the buses
 *
 * The halves split by LANE, never by voice: two voices of one lane may overlap (a hit cutting the one before), and a
 * lane's meter and USB tap are written by its voices. Every voice is in one half; a voice's samples do not depend on
 * which half renders it (its state is its own: drums.v[k], dv, dins_st[k], filter[k], env[k]), and the buses are integer
 * sums, so the output is the same samples whatever the lanes' split and whichever core runs which half (the one-job
 * order too: tests/drum_halves_test.c, fm1-base's optimist/dualproof.py). Kept in half A: the click's voices (no lane),
 * the X0X channels (their shared sum is one product: not split) and the lanes that have one sounding, and with USB audio
 * the lanes a capture channel taps (two lanes may sum into one channel). The others go by their voices' load, the
 * heaviest first onto the lighter half (a voice with its lane's inserts on weighs more: on the user's kit the inserts are
 * 116 us of the voices' 212). */
#if FELUCCA_DRUMS
enum { DH_ONE, DH_IDLE, DH_TWO };               /* this block: one job (the SLICER); nothing sounds (the drum bus
                                                 * alone, the drum job); split in two halves */
typedef struct {                                /* half B's own buses, scratch and stem (CTL each) */
    int32_t ml[CTL], mr[CTL], sr[CTL], sd[CTL], sc[CTL], buf[CTL], dsb[CTL];
#if FELUCCA_USB_AUDIO
    int32_t cap[CTL];
#endif
} dhb_t;
static dhb_t dhb __attribute__((section(".pool")));   /* (the pool: main RAM is full) */
static dsink_t dh[2];                           /* [0] half A: the buses; [1] half B: dhb */
static uint32_t dh_lanes[2];                    /* each half's lanes (bit DRUM_LANES: the click's voices) */
static uint8_t dh_on;                           /* DH_*: this block's */

/* the lanes of half B this block, by the voices' load (half A keeps what must stay together: above); bias (Q8 of the
 * voices' load): how much more half B takes (the core that runs half A runs the drum bus after the join too) */
static HOT uint32_t dh_split(uint32_t bias)
{
    uint32_t w[DRUM_LANES], k, l, a = 0, b = 0, keep = 1u << DRUM_LANES, mb = 0, tot;
    for (l = 0; l < DRUM_LANES; l++)
        w[l] = 0;
    for (k = 0; k < NDRUM; k++)
        if (drums.v[k].active) {
            l = drums.lane[k];
            if (l < DRUM_LANES)
                w[l] += drums.synth[k] ? 3u : 2u;
            else
                a += 2u;
        }
#if DINS
    for (l = 0; l < DRUM_LANES; l++)              /* (its inserts: the larger share of a sampled voice's time) */
        if (w[l] && dblk.on && (lin_amt(l, 0) | lin_amt(l, 1)))
            w[l] += w[l] * 3u / 2u;
#endif
#if DRUM_X0X
    for (k = 0; k < X0X_NCH; k++)
        if ((xc.live >> k) & 1u) {
            keep |= 1u << xc.lane[k];             /* (xc.lane: DRUM_LANES for the click's) */
            a += 3u;
        }
#endif
#if FELUCCA_USB_AUDIO
    keep |= dlt_live;                           /* (the lanes a channel takes: its sum in one half) */
#endif
    for (l = 0, tot = a; l < DRUM_LANES; l++)
        tot += w[l];
    for (l = 0; l < DRUM_LANES; l++)
        if ((keep >> l) & 1u)
            a += w[l], w[l] = 0;
    a += (tot * (bias < 256u ? bias : 256u)) >> 8;   /* (as if A had it already) */
    for (;;) {                                  /* the heaviest left, onto the lighter half (ties: A) */
        uint32_t m = DRUM_LANES;
        for (l = 0; l < DRUM_LANES; l++)
            if (w[l] && (m == DRUM_LANES || w[l] > w[m]))
                m = l;
        if (m == DRUM_LANES)
            break;
        if (b < a)
            b += w[m], mb |= 1u << m;
        else
            a += w[m];
        w[m] = 0;
    }
    return mb;
}

/* half B's sink onto dhb */
static void dh_sink_b(dsink_t *H)
{
    H->ml = dhb.ml, H->mr = dhb.mr, H->rev = dhb.sr, H->mono = 0;
    H->sr = dhb.sr, H->sd = dhb.sd, H->sc = dhb.sc;
    H->buf = dhb.buf, H->dsb = dhb.dsb;
#if FELUCCA_USB_AUDIO
    H->cap = dhb.cap, H->cs = 1;
#else
    H->cap = 0, H->cs = 0;
#endif
}

/* CPU0 before the fork, after drums.a0 / a1: this block's DH_* (the jobs: DH_ONE the one job, DH_IDLE the drum job
 * runs the drum bus alone, DH_TWO both halves and the drum bus after the join); bias: dh_split's (who renders what:
 * never the samples) */
static HOT uint32_t dh_pre(uint32_t n, uint32_t bias)
{
    if (!slicer_drums_plain())
        return dh_on = DH_ONE;                  /* (the SLICER on the drums: one job, slicer_drums) */
    slicer_track(TDRUM, 0, n);                  /* (its clock runs, as slicer_drums runs it) */
    if (!drums_pre(0))
        return dh_on = DH_IDLE;
    dsink_bus(&dh[0], mix_l, mix_r, send_r, 0);
    dh_lanes[1] = dh_split(bias);
    dh_lanes[0] = ~dh_lanes[1];
    return dh_on = DH_TWO;
}

/* half h's voices (either core): half B clears its buffers first (the buses were cleared at the block's start) */
static HOT void dh_job(uint32_t h, uint32_t n)
{
    dsink_t *H = &dh[h & 1u];
    uint32_t i;
    if (h) {
        dh_sink_b(H);
        for (i = 0; i < n; i++) {
            dhb.ml[i] = dhb.mr[i] = dhb.sr[i] = dhb.sd[i] = dhb.sc[i] = 0;
#if FELUCCA_USB_AUDIO
            dhb.cap[i] = 0;
#endif
        }
    }
    H->tail = 0, H->pk = 0, H->prof[0] = H->prof[1] = H->prof[2] = 0;
    drums_mix(H, dh_lanes[h & 1u], h == 0u, n);
}

/* CPU0 after the join, before the drum bus and the parts' sums: half B onto the buses, the tail, the fold */
static HOT void dh_join(uint32_t n)
{
    uint32_t i;
    if (dh_on != DH_TWO)
        return;
    for (i = 0; i < n; i++) {
        mix_l[i] += dhb.ml[i];
        mix_r[i] += dhb.mr[i];
        send_r[i] += dhb.sr[i];
        send_d[i] += dhb.sd[i];
        send_c[i] += dhb.sc[i];
#if FELUCCA_USB_AUDIO
        track_capture[i * NTRK + TRK_DRUM] += dhb.cap[i];
#endif
    }
    drums_tail(&dh[0], n);                      /* (drums.tail as the block started: the halves' cuts not in it yet) */
    drums_fold(&dh[0], &dh[1]);
}
#endif
