/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock (firmware/src/clock_sync.c) on the host, with the audio timing of the firmware: a half buffer
 * of 256 samples is rendered when the DMA starts the other one, sample k of it leaves the DMA (256 + k)
 * samples later (audio.c sync_out_t). USB packets are seen by the next 10 kHz TIMER5 tick and timestamped
 * half a tick back (usb.c usb_rx_peek); TRS bytes land in the ring 320 us after they start (31250 baud) and
 * the 10 kHz peek takes them (midi_uart.c). Ablations (environment): USB2K / TRS2K the 2 kHz poll only,
 * EXACT exact USB times; -DSY_EXACT=0 / -DSY_PREDICT=0 / -DSY_FILTER=0 (clock_sync.c); STEPS=1 every step,
 * ONLY=i one scenario (the jitter differs: one random sequence runs through all). An external clock (F8 at 24 PPQN, FA / FB / FC, F2) drives a hat on every beat; an
 * onset is the first output sample above silence (|x| >= 16, Q15) after 5 ms below 4.
 *   phase   onset time - the F8 of its beat: its ideal (unjittered) time, and its arrival (USB: when the
 *           host delivers it; TRS: the end of its byte); the first hit after a start apart
 *   tempo   the estimate (sy.bpm) - the true tempo, once a half, while playing after the first beat
 *   source  SYNC AUTO: TRS before USB before the internal tempo, never switched while playing
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
static uint64_t instr_now(void)                   /* CPU=1: host instructions (regress.c), 0 elsewhere */
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static uint64_t cpu_instr, cpu_samples;

#define TPS (24e6 / 44100.0)                      /* TIMER4 ticks a sample */
#define MS 24000.0
#define POLL 12000.0                              /* 2 kHz */
#define BYTE_T 7680.0                             /* a TRS byte, TIMER4 ticks */
static double T;                                  /* host time of the half interrupt (ticks) */
typedef struct { double t; uint32_t pkt; uint8_t src; } mev_t;   /* t: USB delivery / TRS first byte start */
static mev_t mev[40000];
static uint32_t nmev, mevr;
static double on_t[40000];                        /* onsets */
static uint32_t non, quiet;
static double terr[40000];                        /* tempo errors */
static uint32_t nterr;
static double (*true_bpm)(double t);
static double trs_free;                           /* the TRS line is busy until then */
static double tq[64];                             /* TRS bytes in flight: landing times */
static uint8_t tb[64];
static uint32_t tq_w, tq_r;

static void push(double t, uint32_t pkt, uint32_t src)   /* in time order (USB: delivered; TRS: its byte starts) */
{
    uint32_t i = nmev++;
    while (i > mevr && mev[i - 1].t > t) {
        mev[i] = mev[i - 1];
        i--;
    }
    mev[i].t = t;
    mev[i].pkt = pkt;
    mev[i].src = (uint8_t)src;
}
static void trs_byte(double start, uint32_t b) { push(start, b, MSRC_TRS); }

static double next_poll;
static uint32_t ring_w;
static void polls_until(double until)
{
    if (next_poll < until - 40 * MS)
        next_poll = ceil((until - 40 * MS) / POLL) * POLL;
    for (; next_poll <= until; next_poll += POLL / 5) {   /* the TIMER5 tick, 10 kHz */
        double next = next_poll;
        int usb_poll = fmod(next, POLL) < 1.0;
        while (mevr < nmev && mev[mevr].t <= next) {
            if (mev[mevr].src == MSRC_USB && !usb_poll && getenv("USB2K"))
                break;                              /* (ablation: USB waits for the 2 kHz poll, as before) */
            if (mev[mevr].src == MSRC_USB) {        /* USB: in the endpoint by this tick (usb_rx_peek) */
                usb_midi_rx_packet(mev[mevr].pkt, (uint32_t)(int64_t)(getenv("EXACT") ? mev[mevr].t : next - (getenv("USB2K") ? POLL / 2 : POLL / 10)));
            } else {                                /* TRS: on the line after what is on it */
                double st = mev[mevr].t > trs_free ? mev[mevr].t : trs_free;
                trs_free = st + BYTE_T;
                tq[tq_w % 64u] = trs_free;
                tb[tq_w++ % 64u] = (uint8_t)mev[mevr].pkt;
            }
            mevr++;
        }
        if (getenv("TRS2K") && !usb_poll)
            continue;                               /* (ablation: TRS polled at 2 kHz, as before) */
        while (tq_r != tq_w && tq[tq_r % 64u] <= next) {   /* landed in the ring by this poll */
            um_ring[ring_w % UM_RING] = tb[tq_r % 64u];
            ring_w++;
            tq_r++;
        }
        if (um_ring[um.rd] != UM_EMPTY)
            um_drain((uint32_t)(int64_t)(next - (getenv("TRS2K") ? POLL / 2 : 1200.0)));   /* (uart_midi_peek) */
    }
}

static void run_half(void)
{
    uint32_t b, i;
    int32_t o[2 * CTL];
    polls_until(T);
    host_now = (uint32_t)(int64_t)T;
    for (b = 0; b < 8u; b++) {
        double out = T + (256.0 + 32.0 * b) * TPS;
        sync_out_t = (uint32_t)(int64_t)out;
        if (getenv("CPU")) {
            uint64_t i0 = instr_now();
            mix_block(o, CTL);
            cpu_instr += instr_now() - i0;
            cpu_samples += CTL;
        } else {
            mix_block(o, CTL);
        }
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2u * i] < 0 ? -o[2u * i] : o[2u * i];
            if (getenv("TRACE") && x)
                printf("%.3f %d\n", (out + i * TPS) / MS, x);
            if (x >= 16) {
                if (quiet >= 220u && non < 40000u)
                    on_t[non++] = out + i * TPS;
                quiet = 0;
            } else if (x < 4) {
                quiet++;
            }
        }
    }
    if (getenv("DBG"))
        printf("T %.2f play %u beat %u pos %u have %u nm %u n %u n0 %u p %.3f m %d bias %d sus %u arm %u\n", T / MS,
               song.playing, clk_beat, clk_pos, sy.have, sy.nm, sy.n, sy.n0, sy.p / 256.0 / MS, sy.m, sy.bias,
               sy.suspect, sy.arm);
    if (true_bpm && sy.have == 2u && song.playing && clk_beat >= 1u && nterr < 40000u)
        terr[nterr++] = sy.bpm / 65536.0 - true_bpm(T);
    T += 256.0 * TPS;
}

static void reset(uint32_t sync)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    for (i = 0; i < 16u; i += 4u)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);   /* a closed hat on every beat (it rings ~0.3 s) */
    song.g[G_DRREV] = 0;
    song.playing = 0;
    transport_req = 0;
    mi_r = mi_w = 0;
    usb.config = 1;
    song.g[G_SYNC] = (int16_t)sync;
    memset(&sy, 0, sizeof sy);
    memset(sy_pr, 0, sizeof sy_pr);
    sy.mode = (uint8_t)sync;
    T = 1e6;
    nmev = mevr = non = nterr = 0;
    cpu_instr = cpu_samples = 0;
    quiet = 1000;
    true_bpm = 0;
    trs_free = 0;
    tq_r = tq_w;
    next_poll = 0;
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = UM_EMPTY;
    memset(&um, 0, sizeof um);
    ring_w = 0;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("sync: %-76s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint64_t rnd = 88172645463325252ull;
static double jit(double ms)                      /* uniform -ms .. +ms */
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 7;
    rnd ^= rnd << 17;
    return ((double)(rnd % 2000001u) / 1000000.0 - 1.0) * ms * MS;
}

static double tick_t[40000], tick_a[40000], tick_bpm[40000];   /* ideal, arrival, tempo of the interval */
static uint32_t nticks;
static double tempo_at(double t)
{
    uint32_t lo = 0, hi = nticks - 1u;
    while (lo + 1u < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (tick_t[m] <= t) lo = m; else hi = m;
    }
    return tick_bpm[lo];
}

typedef struct { double mean, mabs, max, min, p95; uint32_t n; } st_t;
static int dcmp(const void *a, const void *b)
{
    double x = fabs(*(const double *)a), y = fabs(*(const double *)b);
    return x < y ? -1 : x > y;
}
static st_t stats(const double *v, uint32_t n)
{
    static double s[40000];
    st_t r = {0, 0, -1e9, 1e9, 0, n};
    uint32_t i;
    for (i = 0; i < n; i++) {
        r.mean += v[i];
        r.mabs += fabs(v[i]);
        r.max = v[i] > r.max ? v[i] : r.max;
        r.min = v[i] < r.min ? v[i] : r.min;
        s[i] = v[i];
    }
    qsort(s, n, sizeof s[0], dcmp);
    r.p95 = n ? fabs(s[(n * 95u) / 100u]) : 0;
    r.mean /= n ? n : 1;
    r.mabs /= n ? n : 1;
    return r;
}

/* a clock scenario. src MSRC_USB / MSRC_TRS; tempo b0 -> b1 over ramp_beats (0: a jump at the downbeat)
 * from the downbeat; jitter +-jit_ms; fa_lead: FA this many ms before the downbeat F8 (hosts: a tick);
 * drop: F8 k (> 0) is lost; spike: F8 k arrives spike_ms late */
typedef struct {
    const char *name;
    uint32_t src;
    double b0, b1, ramp_beats, jit_ms;
    uint32_t beats;
    double fa_lead;
    uint32_t drop, spike;
    double spike_ms;
    double lim_mean, lim_max, lim_first;          /* checks: |mean|, worst after the first bar, the first hit */
    double at;                                    /* the tempo change starts this many beats after the downbeat */
} scen_t;

static void follow(const scen_t *c)
{
    static double e_id[8000], e_arr[8000];
    uint32_t k, nt = 48u + c->beats * 24u, down = 48u, ne = 0, every = c->b0 > 145 || c->b1 > 145 ? 2u : 1u, steps = c->beats / every;
    double t = 2e6, first, late_max = -1e9;
    char line[200];
    reset(SYNC_AUTO);
    if (every == 2u)                               /* (fast: a hat every other beat, so it falls quiet) */
        for (k = 4; k < 16u; k += 8u)
            memset(&TDRUM->dstep[k], 0, sizeof TDRUM->dstep[k]);
    for (k = 0; k < nt; k++) {
        uint32_t k0 = down + (uint32_t)(c->at * 24.0);
        double f = k < k0 ? 0.0 : c->ramp_beats <= 0 ? 1.0 : (k - k0) / (c->ramp_beats * 24.0);
        tick_bpm[k] = c->b0 + (c->b1 - c->b0) * (f > 1.0 ? 1.0 : f);
        tick_t[k] = t;
        tick_a[k] = t + jit(c->jit_ms) + (k && k == c->spike ? c->spike_ms * MS : 0.0);
        t += 60.0 / (24.0 * tick_bpm[k]) * 24e6;
    }
    nticks = nt;
    for (k = 0; k < nt; k++) {
        if (c->src == MSRC_USB) {
            if (k == down)                     /* (a tick ahead: just after the F8 before) */
                push(c->fa_lead > 1.0 ? tick_a[k - 1] + 0.1 * MS : tick_a[k] - c->fa_lead * MS, 0xFA0Fu, MSRC_USB);
            if (k != c->drop || !k)
                push(tick_a[k], 0xF80Fu, MSRC_USB);
        } else {                                   /* (arrival: the end of its byte) */
            if (k == down)
                trs_byte(c->fa_lead > 1.0 ? tick_a[k - 1] + 0.1 * MS : tick_a[k] - c->fa_lead * MS - BYTE_T, 0xFA);
            if (k != c->drop || !k)
                trs_byte(tick_a[k] - BYTE_T, 0xF8);
        }
    }
    true_bpm = tempo_at;
    while (T < tick_t[nt - 1] - 40 * MS)
        run_half();
    if (!non) {
        check(0, c->name);
        return;
    }
    first = (on_t[0] - tick_t[down]) / MS;
    for (k = 1; k < non && k < steps && down + 24u * every * k < nt; k++) {
        double id = (on_t[k] - tick_t[down + 24u * every * k]) / MS;
        e_id[ne] = id;
        if (getenv("STEPS"))
            printf("sync:     step %u  %+.3f ms  (%.2f BPM)\n", k, id, tick_bpm[down + 24u * every * k]);
        e_arr[ne++] = (on_t[k] - tick_a[down + 24u * every * k]) / MS;
        if (k >= 4u)
            late_max = id > late_max ? id : late_max;
    }
    {
        st_t d = stats(e_id, ne), a = stats(e_arr, ne), b = stats(terr, nterr);
        printf("sync: %s\n", c->name);
        printf("sync:   onsets %u of %u steps; the first %+.3f ms from its F8\n", non, steps, first);
        printf("sync:   phase vs ideal    mean %+.3f  |mean| %.3f  p95 %.3f  min %+.3f  max %+.3f ms\n", d.mean, d.mabs,
               d.p95, d.min, d.max);
        printf("sync:   phase vs arrival  mean %+.3f  |mean| %.3f  p95 %.3f  min %+.3f  max %+.3f ms\n", a.mean, a.mabs,
               a.p95, a.min, a.max);
        printf("sync:   tempo error       mean %+.4f  |mean| %.4f  p95 %.4f  max |%.4f| BPM\n", b.mean, b.mabs, b.p95,
               fabs(b.max) > fabs(b.min) ? fabs(b.max) : fabs(b.min));
        snprintf(line, sizeof line, "%s: |mean| <= %.2f, worst after bar 1 <= %.2f, first <= %.1f ms", c->name,
                 c->lim_mean, c->lim_max, c->lim_first);
        {
            double w = 0, wc = 0;
            for (k = 3; k < ne; k++) {
                double b = (k + 1.0) * every;             /* (a change mid-song: its first 2 beats apart) */
                if (c->at > 0 && b >= c->at && b < c->at + 2.0)
                    wc = fabs(e_id[k]) > wc ? fabs(e_id[k]) : wc;
                else
                    w = fabs(e_id[k]) > w ? fabs(e_id[k]) : w;
            }
            if (c->at > 0)
                printf("sync:   the 2 beats from the change at beat %.0f: worst %.3f ms (not predictable)\n", c->at, wc);
            printf("sync:   worst after bar 1 %.3f ms, the latest %+.3f ms\n", w, late_max);
            if (cpu_samples)
                printf("sync:   cost: %.0f host instructions / sample (mix_block)\n", (double)cpu_instr / (double)cpu_samples);
            check(non >= steps - 1u && fabs(d.mean) <= c->lim_mean && w <= c->lim_max && fabs(first) <= c->lim_first,
                  line);
        }
    }
}

/* START / STOP / SPP + CONTINUE on USB with a running clock; then the clock stops */
static void t_transport(void)
{
    uint32_t k, n_cont, ok;
    double t = 2e6, p = 60.0 / (24.0 * 120.0) * 24e6, t_fb = 0, t_fc = 0, t_end;
    reset(SYNC_AUTO);
    for (k = 0; k < 400u; k++, t += p) {
        if (k == 24u)
            push(t - p + 0.1 * MS, 0xFA0Fu, MSRC_USB);   /* a tick ahead */
        if (k == 24u + 96u) {
            push(t - 0.05 * MS, 0xFC0Fu, MSRC_USB);    /* stop after a bar */
            t_fc = t;
        }
        if (k == 24u + 96u + 48u)
            push(t - p + 0.1 * MS, 0x0108F203u, MSRC_USB);   /* song position: 16th 136 */
        if (k == 24u + 96u + 49u) {
            push(t - p + 0.2 * MS, 0xFB0Fu, MSRC_USB); /* continue, a tick ahead */
            t_fb = t;
        }
        push(t, 0xF80Fu, MSRC_USB);
    }
    t_end = t;
    while (T < t_fc - 100 * MS)
        run_half();
    ok = song.playing && sy.src == MSRC_USB;
    printf("sync:   START: the first hit %+.3f ms from the downbeat F8\n", (on_t[0] - (2e6 + 24 * p)) / MS);
    check(ok && fabs((on_t[0] - (2e6 + 24 * p)) / MS) < 0.2, "START a tick ahead (running clock): the downbeat on time");
    while (T < t_fc + 50 * MS)
        run_half();
    check(!song.playing, "STOP stops (F8 still running)");
    n_cont = non;
    while (T < t_fb + 600 * MS)
        run_half();
    printf("sync:   continue at 16th 136: step %u; its hit %+.3f ms from its F8, the next %+.3f\n", TDRUM->seq_idx,
           non > n_cont ? (on_t[n_cont] - t_fb) / MS : 99.0, non > n_cont + 1 ? (on_t[n_cont + 1] - t_fb - 24 * p) / MS : 99.0);
    check(non > n_cont + 1 && fabs((on_t[n_cont] - t_fb) / MS) < 0.2 && fabs((on_t[n_cont + 1] - t_fb - 24 * p) / MS) < 0.2,
          "SPP 136 + CONTINUE a tick ahead: resumes on time");
    while (T < t_end + 100 * MS)
        run_half();
    k = non;
    while (T < t_end + 700 * MS)
        run_half();
    check(!song.playing && non == k, "the clock stops: the transport stops after 500 ms");
}

/* a clock of one source from t0 for n ticks at bpm (no transport) */
static double clock_run(uint32_t src, double t0, uint32_t n, double bpm)
{
    uint32_t k;
    double p = 60.0 / (24.0 * bpm) * 24e6, t = t0;
    for (k = 0; k < n; k++, t += p) {
        if (src == MSRC_USB)
            push(t, 0xF80Fu, MSRC_USB);
        else
            trs_byte(t - BYTE_T, 0xF8);
    }
    return t;
}
static void run_to(double t)
{
    while (T < t)
        run_half();
}

static void t_auto(void)
{
    double t;
    uint32_t ok;
    /* USB alone, then TRS too (stopped): TRS wins */
    reset(SYNC_AUTO);
    clock_run(MSRC_USB, 2e6, 400, 120);
    run_to(2e6 + 200 * MS);
    ok = sy.src == MSRC_USB && song.g[G_MIDI] == 1;
    clock_run(MSRC_TRS, 2e6 + 300 * MS, 300, 100);
    run_to(2e6 + 500 * MS);
    check(ok && sy.src == MSRC_TRS && song.g[G_MIDI] == 2 && song.g[G_BPM] == 100,
          "AUTO: USB clock followed; TRS clock arrives (stopped): TRS, its tempo");
    {   /* GLO > SYSTEM shows "A:TRS" (5 characters fit a column); both gone: "A:INT" */
        char val[12];
        const char *unit;
        int ok2;
        param_format(&GP[G_SYNC], SYNC_AUTO, val, &unit);
        ok2 = !strcmp(val, "A:TRS") && !*unit;
        run_to(2e6 + 9000 * MS);                     /* (USB ends at 8.3 s, TRS at 7.8 s) */
        param_format(&GP[G_SYNC], SYNC_AUTO, val, &unit);
        check(ok2 && sy.src == 0 && !strcmp(val, "A:INT") && GP[G_SYNC].def == SYNC_AUTO,
              "AUTO (default) shows the clock followed: A:TRS; both gone: A:INT");
    }
    /* an unsteady clock (periods 10 and 30 ms by turns) is not a source */
    reset(SYNC_AUTO);
    {
        uint32_t k;
        for (k = 0; k < 60u; k++)
            push(2e6 + (k / 2u) * 40.0 * MS + (k & 1u) * 10.0 * MS, 0xF80Fu, MSRC_USB);
    }
    run_to(2e6 + 1000 * MS);
    check(sy.src == 0 && song.g[G_BPM] == 120, "AUTO: an unsteady clock (10 / 30 ms by turns) is not followed");
    /* playing on USB; TRS appears: no switch until stopped */
    reset(SYNC_AUTO);
    t = clock_run(MSRC_USB, 2e6, 1200, 120);
    push(2e6 + 47.5 * 60.0 / (24 * 120.0) * 24e6, 0xFA0Fu, MSRC_USB);
    run_to(2e6 + 1000 * MS);
    ok = song.playing && sy.src == MSRC_USB;
    clock_run(MSRC_TRS, 2e6 + 1100 * MS, 600, 90);
    run_to(2e6 + 2000 * MS);
    ok &= song.playing && sy.src == MSRC_USB;
    push(2e6 + 2100 * MS, 0xFC0Fu, MSRC_USB);
    run_to(2e6 + 2300 * MS);
    check(ok && !song.playing && sy.src == MSRC_TRS, "AUTO: playing on USB, TRS appears: kept until STOP, then TRS");
    /* playing on TRS, TRS lost: stops after 500 ms, then USB (still there) */
    reset(SYNC_AUTO);
    clock_run(MSRC_USB, 2e6, 1200, 120);
    t = clock_run(MSRC_TRS, 2e6 + 0.3 * MS, 200, 120);
    trs_byte(2e6 + 47.5 * 60.0 / (24 * 120.0) * 24e6, 0xFA);
    run_to(t - 50 * MS);
    ok = song.playing && sy.src == MSRC_TRS;
    run_to(t + 450 * MS);
    ok &= song.playing;
    run_to(t + 650 * MS);
    check(ok && !song.playing && sy.src == MSRC_USB, "AUTO: TRS lost while playing: stops after 500 ms, then USB");
    /* nothing present, FA + clock from USB at once (a host that clocks only while playing): followed */
    reset(SYNC_AUTO);
    push(2e6 - 0.1 * MS, 0xFA0Fu, MSRC_USB);
    clock_run(MSRC_USB, 2e6, 200, 120);
    run_to(2e6 + 500 * MS);
    printf("sync:   clock only while playing: the first hit %+.3f ms from the downbeat F8\n", non ? (on_t[0] - 2e6) / MS : 99.0);
    check(song.playing && sy.src == MSRC_USB && non > 1u, "AUTO: START + clock from nothing (USB): followed");
    /* INT: clocks ignored; PLAY plays at its own tempo */
    reset(SYNC_INT);
    clock_run(MSRC_USB, 2e6, 200, 150);
    run_to(2e6 + 300 * MS);
    check(sy.src == 0 && song.g[G_BPM] == 120 && song.g[G_MIDI] == 0, "INT: a clock is ignored (tempo kept)");
    /* USB only: a TRS clock is ignored */
    reset(SYNC_USB);
    clock_run(MSRC_TRS, 2e6, 200, 150);
    clock_run(MSRC_USB, 2e6 + 0.5 * MS, 200, 100);
    run_to(2e6 + 300 * MS);
    check(sy.src == MSRC_USB && song.g[G_BPM] == 100, "USB: follows USB, not the TRS clock");
}

int main(int argc, char **argv)
{
    static const scen_t S[] = {
        {"USB 120 BPM steady", MSRC_USB, 120, 120, 0, 0, 64, 20.8, 0, 0, 0, 0.1, 0.2, 0.2},
        {"TRS 120 BPM steady", MSRC_TRS, 120, 120, 0, 0, 64, 20.8, 0, 0, 0, 0.1, 0.2, 0.2},
        {"USB 174 BPM steady", MSRC_USB, 174, 174, 0, 0, 64, 14.3, 0, 0, 0, 0.1, 0.2, 0.2},
        {"USB 120 BPM, +-1 ms jitter", MSRC_USB, 120, 120, 0, 1.0, 96, 20.8, 0, 0, 0, 0.1, 0.5, 1.0},
        {"TRS 120 BPM, +-1 ms jitter", MSRC_TRS, 120, 120, 0, 1.0, 96, 20.8, 0, 0, 0, 0.1, 0.5, 1.0},
        {"USB 174 BPM, +-0.5 ms jitter", MSRC_USB, 174, 174, 0, 0.5, 96, 14.3, 0, 0, 0, 0.2, 0.4, 1.0},
        {"USB 60 BPM steady", MSRC_USB, 60, 60, 0, 0, 32, 41.6, 0, 0, 0, 0.1, 0.2, 0.2},
        {"USB 100 -> 140 BPM ramp over 16 beats", MSRC_USB, 100, 140, 16, 0, 48, 25.0, 0, 0, 0, 0.1, 1.0, 0.2},
        {"USB 140 -> 90 BPM ramp over 8 beats, +-1 ms", MSRC_USB, 140, 90, 8, 1.0, 48, 17.8, 0, 0, 0, 0.2, 2.0, 1.5},
        {"USB 100 -> 140 BPM jump at the downbeat", MSRC_USB, 100, 140, 0, 0, 48, 25.0, 0, 0, 0, 0.1, 0.2, 0.2},
        {"USB 120 BPM, FA 0.1 ms before the downbeat F8", MSRC_USB, 120, 120, 0, 0, 32, 0.1, 0, 0, 0, 0.1, 0.2, 14},
        {"TRS 120 BPM, one F8 lost (tick 200)", MSRC_TRS, 120, 120, 0, 0, 32, 20.8, 200, 0, 0, 0.1, 0.3, 0.2},
        {"USB 120 BPM, one F8 3 ms late (tick 200)", MSRC_USB, 120, 120, 0, 0, 32, 20.8, 0, 200, 3.0, 0.1, 0.3, 0.2},
        {"TRS 174 BPM steady", MSRC_TRS, 174, 174, 0, 0, 64, 14.3, 0, 0, 0, 0.1, 0.2, 0.2},
        {"TRS 174 BPM, +-0.5 ms jitter", MSRC_TRS, 174, 174, 0, 0.5, 96, 14.3, 0, 0, 0, 0.2, 0.4, 1.0},
        {"USB 120 -> 150 BPM jump at beat 16", MSRC_USB, 120, 150, 0, 0, 48, 20.8, 0, 0, 0, 0.2, 0.3, 0.2, 16},
        {"TRS 140 -> 100 BPM jump at beat 16, +-0.5 ms", MSRC_TRS, 140, 100, 0, 0.5, 48, 17.8, 0, 0, 0, 0.2, 0.5, 1.0, 16},
        {"USB 90 -> 130 BPM ramp over 32 beats from beat 8", MSRC_USB, 90, 130, 32, 0, 64, 27.7, 0, 0, 0, 0.1, 0.5, 0.2, 8},
    };
    uint32_t i;
    (void)argc;
    (void)argv;
    if (getenv("SEED"))
        rnd += (uint64_t)atoll(getenv("SEED")) * 0x9E3779B97F4A7C15ull;   /* (another jitter sequence) */
    for (i = 0; i < sizeof S / sizeof S[0]; i++)
        if (!getenv("ONLY") || atoi(getenv("ONLY")) == (int)i)
            follow(&S[i]);
    if (getenv("CPU")) {                          /* the same hats at the internal tempo: the cost to compare */
        reset(SYNC_INT);
        transport_req = 1;
        run_to(T + 10000 * MS);
        printf("sync: INT 120 BPM (internal) cost: %.0f host instructions / sample (mix_block)\n",
               (double)cpu_instr / (double)(cpu_samples ? cpu_samples : 1));
    }
    if (getenv("ONLY"))
        return fails;
    t_transport();
    t_auto();
    printf("sync: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
