/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock out (firmware/src/io/midi/clock_out.c, usb.c's timed queue) on the host, with the audio timing of the
 * firmware (as tests/clock_sync_test.c: a half of 256 samples rendered when the DMA starts the other, sample k of it
 * leaving (256 + k) samples later, audio.c sync_out_t). The USB side is usb.c's own: rt_out_ready decides what is
 * sent, at the 10 kHz TIMER5 tick (usb_rx_peek); a packet is "sent" at the first tick at or past its due time.
 * A hat on every beat; an onset is the first output sample above silence (|x| >= 16, Q15) after 5 ms below 4.
 * Checked: 24 F8 a beat at several tempos, stopped and playing; the F8s even (no jitter of their own); each beat's
 * F8 with the sound of its beat; FA before the downbeat, FC at a stop, F2 + FB at a continue; nothing when OFF;
 * a tempo change mid-bar; swing does not move the clock; following a USB clock nothing goes back to USB, following
 * TRS the clock goes on to USB at the followed tempo.
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define TPS (24e6 / 44100.0)                      /* TIMER4 ticks a sample */
#define MS 24000.0
#define TICK (24e6 / 10000.0)                     /* the TIMER5 tick, 10 kHz */
static double T;                                  /* host time of the half interrupt (ticks) */
static double on_t[20000];                        /* onsets */
static uint32_t non, quiet;
typedef struct { double t, due; uint32_t pkt; double half; } out_t;   /* sent at t (due: as queued) */
static out_t out[40000];
static uint32_t nout;
static double next_tick;
static int fails;

static void check(int ok, const char *what)
{
    printf("clkout: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the TIMER5 ticks up to time until: what usb.c sends (rt_out_ready, as usb_rx_peek / ep1_tx take it) */
static void sends_until(double until)
{
    if (next_tick < until - 100 * MS)
        next_tick = ceil((until - 100 * MS) / TICK) * TICK;
    for (; next_tick <= until; next_tick += TICK) {
        while (rt_out_ready((uint32_t)(int64_t)next_tick)) {
            if (nout < 40000u) {
                out[nout].t = next_tick;
                out[nout].due = (double)(int32_t)(rt_out_due[rt_r % RTQ] - (uint32_t)(int64_t)next_tick) + next_tick;
                out[nout].pkt = rt_out_pkt[rt_r % RTQ];
                out[nout++].half = T;
            }
            rt_r++;
        }
    }
}

static void run_half(void)
{
    uint32_t b, i;
    int32_t o[2 * CTL];
    sends_until(T);
    host_now = (uint32_t)(int64_t)T;
    for (b = 0; b < 8u; b++) {
        double t0 = T + (256.0 + 32.0 * b) * TPS;
        sync_out_t = (uint32_t)(int64_t)t0;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = o[2u * i] < 0 ? -o[2u * i] : o[2u * i];
            if (x >= 16) {
                if (quiet >= 220u && non < 20000u)
                    on_t[non++] = t0 + i * TPS;
                quiet = 0;
            } else if (x < 4) {
                quiet++;
            }
        }
    }
    T += 256.0 * TPS;
}
static void run_ms(double ms)
{
    double end = T + ms * MS;
    while (T < end)
        run_half();
}

static void reset(uint32_t on, uint32_t sync, int32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
#if FELUCCA_MASTER_COMP
    mlim_reset();                                   /* LIMIT's lookahead (CEIL is on by default, 1.45 ms of latency the F8s */
    mlim.on = song.g[G_CCEIL] > 0 && song.g[G_CCEIL] <= 7;   /* add: SYNC_LIM_T): empty and already on, as after boot */
#endif
    for (i = 0; i < 16u; i += 4u)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);   /* a closed hat on every beat */
    host_drum_rev(0);
    song.playing = 0;
    transport_req = 0;
    mi_r = mi_w = 0;
    usb.config = 1;
    rt_r = rt_w = 0;
    song.g[G_SYNC] = (int16_t)sync;
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = 0;
    memset(&sy, 0, sizeof sy);
    memset(sy_pr, 0, sizeof sy_pr);
    sy.mode = (uint8_t)sync;
    memset(&co, 0, sizeof co);
    clko_on = (uint8_t)on;
    clko_started = 0;
    T = 1e6;
    next_tick = 0;
    nout = non = 0;
    quiet = 1000;
}

static uint32_t st_of(uint32_t k) { return (out[k].pkt >> 8) & 0xFFu; }
static uint32_t count(uint32_t b, uint32_t from, uint32_t to)
{
    uint32_t k, n = 0;
    for (k = from; k < to; k++)
        n += st_of(k) == b;
    return n;
}
static uint32_t find(uint32_t b, uint32_t from)
{
    for (; from < nout; from++)
        if (st_of(from) == b)
            return from;
    return nout;
}
/* the F8 intervals in [from, to): their mean and the largest |interval - p| (ms) */
static void intervals(uint32_t from, uint32_t to, double p, double *mean, double *dev)
{
    uint32_t k, n = 0;
    double last = -1, sum = 0;
    *dev = 0;
    for (k = from; k < to; k++) {
        if (st_of(k) != 0xF8u)
            continue;
        if (last >= 0) {
            double d = (out[k].due - last) / MS;
            sum += d;
            n++;
            *dev = fabs(d - p) > *dev ? fabs(d - p) : *dev;
        }
        last = out[k].due;
    }
    *mean = n ? sum / n : 0;
}

/* stopped then playing at bpm: the count, the evenness, the beats against the sound, FA first, FC at the stop */
static void t_tempo(int32_t bpm)
{
    char line[160];
    double p = 60000.0 / (24.0 * bpm), mean, dev, worst = 0, sum = 0;
    uint32_t k, fa, n_stop, beats = 16, f8_play, i0, n_beat = 0, fc, every = bpm > 145 ? 2u : 1u;
    reset(1, SYNC_INT, bpm);
    if (every == 2u)                               /* (fast: a hat every other beat, so it falls quiet between) */
        for (k = 4; k < 16u; k += 8u)
            memset(&TDRUM->dstep[k], 0, sizeof TDRUM->dstep[k]);
    run_ms(2000);
    n_stop = count(0xF8u, 0, nout);
    intervals(0, nout, p, &mean, &dev);
    snprintf(line, sizeof line, "%d BPM stopped: F8 every %.3f ms (%u in 2 s), even within 0.01 ms", bpm, p, n_stop);
    check(n_stop >= (uint32_t)(1950.0 / p) && n_stop <= (uint32_t)(2050.0 / p) + 1u && fabs(mean - p) < 0.002 && dev < 0.01,
          line);
    i0 = nout;
    transport_req = 1;
    run_ms(beats * 60000.0 / bpm + 20.0);
    fa = find(0xFAu, i0);
    sends_until(T + 20 * MS);
    f8_play = 0;
    for (k = fa; k < nout; k++)
        if (st_of(k) == 0xF8u && out[k].due < on_t[0] + beats * 60000.0 / bpm * MS - p * MS / 2)
            f8_play++;
    intervals(fa, nout, p, &mean, &dev);
    for (k = fa + 1u, i0 = 0; k < nout && n_beat < non; k++) {
        if (st_of(k) != 0xF8u)
            continue;
        if (i0++ % (24u * every) == 0u) {          /* the beat's F8 against the hat */
            double e = (on_t[n_beat++] - out[k].t) / MS;
            sum += e;
            worst = fabs(e) > worst ? fabs(e) : worst;
        }
    }
    printf("clkout: %d BPM: %u onsets; F8 vs the beat's onset: mean %+.3f ms, worst %.3f ms; F8 interval %.4f ms (dev %.4f)\n",
           bpm, non, n_beat ? sum / n_beat : 0, worst, mean, dev);
    snprintf(line, sizeof line, "%d BPM playing: FA first, 24 F8 a beat (%u in %u beats), even, each beat's F8 with its sound (0.4 ms)",
             bpm, f8_play, beats);
    check(fa < nout && fa > 0 && st_of(fa + 1u) == 0xF8u && f8_play == beats * 24u && dev < 0.01 && n_beat >= beats / every - 1u &&
              worst < 0.45 && fabs(sum / n_beat) < 0.3, line);
    i0 = nout;
    transport_req = 2;
    run_ms(500);
    sends_until(T);
    fc = find(0xFCu, i0);
    snprintf(line, sizeof line, "%d BPM: FC at the stop, the clock runs on stopped", bpm);
    check(fc < nout && count(0xF8u, fc, nout) >= (uint32_t)(400.0 / p) && count(0xFAu, i0, nout) == 0u, line);
}

static void t_off(void)
{
    reset(0, SYNC_INT, 120);
    run_ms(500);
    transport_req = 1;
    run_ms(1000);
    transport_req = 2;
    run_ms(200);
    sends_until(T + 20 * MS);
    check(nout == 0u && rt_w == 0u, "CLOCK OUT OFF: nothing queued, nothing sent (stopped, playing, stopping)");
    clko_on = 1;                                   /* switched ON while stopped: the clock, no start */
    run_ms(200);
    sends_until(T);
    check(count(0xF8u, 0, nout) >= 8u && count(0xFAu, 0, nout) == 0u && count(0xFCu, 0, nout) == 0u,
          "switched ON: F8 from then on, no FA / FC of its own");
}

/* the tempo from 120 to 97 BPM mid-beat while playing: every F8 interval one of the two periods (or the one that
 * spans the change), no tick lost or doubled: the count is the clock's own */
static void t_change(void)
{
    double p0 = 60000.0 / (24.0 * 120), p1 = 60000.0 / (24.0 * 97), last = -1;
    uint32_t k, bad = 0, n = 0, mid = 0, fa;
    uint64_t units;
    reset(1, SYNC_INT, 120);
    transport_req = 1;
    run_ms(1000 + 37.0);                           /* (2 beats and a bit: not on a beat) */
    song.g[G_BPM] = 97;
    run_ms(2000);
    units = (uint64_t)clk_abs();                   /* (the next block stops before it moves: the clock's last place) */
    transport_req = 2;
    run_half();
    sends_until(T + 20 * MS);
    fa = find(0xFAu, 0);
    for (k = fa; k < nout; k++) {
        if (st_of(k) == 0xFCu)
            break;
        if (st_of(k) != 0xF8u)
            continue;
        n++;
        if (last >= 0) {
            double d = (out[k].due - last) / MS;
            if (fabs(d - p0) > 0.01 && fabs(d - p1) > 0.01) {
                if (d > p0 - 0.01 && d < p1 + 0.01)
                    mid++;                         /* (the one interval with the change in it) */
                else
                    bad++;
            }
        }
        last = out[k].due;
    }
    printf("clkout: tempo 120 -> 97 mid-beat: %u F8 to the stop, the clock at %.3f ticks; %u intervals between, %u off\n",
           n, (double)units / SY_TICK_U, mid, bad);
    check(bad == 0u && mid <= 1u && n == (uint32_t)((units + SY_TICK_U - 1u) / SY_TICK_U),
          "tempo change mid-beat: every interval one of the two periods, the count the clock's ticks");
}

static void t_swing(void)
{
    double p = 60000.0 / (24.0 * 110), mean, dev;
    uint32_t fa;
    reset(1, SYNC_INT, 110);
    song.g[G_SWING] = 100;                         /* (and every track's) */
    trk[0].p[P_SSWING] = 50;
    transport_req = 1;
    run_ms(3000);
    sends_until(T);
    fa = find(0xFAu, 0);
    intervals(fa, nout, p, &mean, &dev);
    check(fa < nout && dev < 0.01 && fabs(mean - p) < 0.002, "SWING 100: the clock does not swing (every F8 interval the same)");
}

/* a restart while playing (PLAY from the top): FA again, the F8 that follows on the new downbeat */
static void t_restart(void)
{
    uint32_t i0, fa;
    reset(1, SYNC_INT, 120);
    transport_req = 1;
    run_ms(700);
    sends_until(T + 20 * MS);
    i0 = nout;
    seq_start();                                   /* (as the UI's restart: playing, from the top) */
    run_ms(300);
    sends_until(T);
    fa = find(0xFAu, i0);
    check(fa < nout && fa + 1u < nout && st_of(fa + 1u) == 0xF8u && count(0xFCu, 0, nout) == 0u,
          "a restart while playing: FA again, then the downbeat's F8");
}

/* an external clock on src (USB / TRS) at bpm: F8s timestamped as they arrive (queued with the input, as usb.c and
 * midi_uart.c do), FA after 2 beats; or F2 + FB (continue at 16th 4) */
static double present_t;                          /* when the source became the one followed */
static void ext_feed(uint32_t src, double bpm, double ms, uint32_t cont)
{
    double p = 60.0 / (24.0 * bpm) * 24e6, t = T + p, end = T + ms * MS;
    uint32_t k = 0;
    present_t = 0;
    while (T < end) {
        if (!present_t && sy.src == src)
            present_t = T;
        while (t <= T) {
            if (k == 48u) {
                if (cont) {
                    midi_in_enqueue(0x03u | 0xF2u << 8 | 4u << 16, src, (uint32_t)(int64_t)(t - 0.2 * MS));
                    midi_in_enqueue(0xFB0Fu, src, (uint32_t)(int64_t)(t - 0.1 * MS));
                } else {
                    midi_in_enqueue(0xFA0Fu, src, (uint32_t)(int64_t)(t - 0.1 * MS));
                }
            }
            midi_in_enqueue(0xF80Fu, src, (uint32_t)(int64_t)t);
            t += p;
            k++;
        }
        run_half();
    }
}

static void t_ext(void)
{
    double p = 60000.0 / (24.0 * 132), mean, dev;
    uint32_t fb, f2;
    uint32_t k, late = 0;
    reset(1, SYNC_AUTO, 120);
    ext_feed(MSRC_USB, 132, 3000, 0);
    sends_until(T);
    for (k = 0; k < nout; k++)
        late += out[k].t > present_t + 15 * MS;    /* (queued before: may still leave) */
    printf("clkout: USB clock followed from %.1f ms: %u sent before (stopped, not yet followed), %u after\n",
           (present_t - 1e6) / MS, nout, late);
    check(present_t > 0 && sy.src == MSRC_USB && song.playing && late == 0u && find(0xFAu, 0) == nout && clko_blocked(),
          "following a USB clock: nothing sent back to USB once it is followed (the menu: amber)");
    reset(1, SYNC_AUTO, 120);
    ext_feed(MSRC_TRS, 132, 4000, 0);
    sends_until(T);
    intervals(find(0xFAu, 0) + 2u, nout, p, &mean, &dev);   /* (the downbeat's F8 late by the output latency: FA
                                                    * came with its F8, as the steps; from the next one) */
    printf("clkout: following TRS at 132 BPM: %u sent, F8 interval mean %.4f ms (%.4f), the largest step %.4f ms\n", nout, mean,
           p, dev);
    check(sy.src == MSRC_TRS && song.playing && find(0xFAu, 0) < nout && fabs(mean - p) < 0.01 && dev < 0.05 && !clko_blocked(),
          "following TRS: FA, then F8 on USB at the followed tempo (smooth: the filtered clock)");
    {   /* each beat's F8 with the sound of its beat (the steps at their sample: no CTL / 2 here) */
        uint32_t fa = find(0xFAu, 0), i = 0, nb = 0;
        double w = 0;
        for (k = fa + 1u; k < nout && nb < non; k++)
            if (st_of(k) == 0xF8u && i++ % 24u == 0u) {
                double e = (on_t[nb++] - out[k].t) / MS;
                if (nb > 1u)
                    w = fabs(e) > w ? fabs(e) : w;
            }
        printf("clkout: following TRS: %u beats, F8 vs the beat's onset worst %.3f ms (after the downbeat)\n", nb, w);
        check(nb >= 6u && w < 0.15, "following TRS: each beat's F8 leaves with its sound (0.15 ms)");
    }
    reset(1, SYNC_AUTO, 120);
    ext_feed(MSRC_TRS, 120, 2000, 1);
    sends_until(T);
    fb = find(0xFBu, 0);
    f2 = find(0xF2u, 0);
    check(song.playing && f2 < nout && fb == f2 + 1u && ((out[f2].pkt >> 16) & 0x7Fu) == 4u && find(0xFAu, 0) == nout,
          "following TRS, CONTINUE at song position 4: F2 4 + FB go on to USB (no FA)");
}

/* following TRS: START, STOP after 50 ticks, CONTINUE (no song position: where the clock stopped, not on a 16th): F2 at
 * the next 16th + FB, and no F8 before that 16th: the first is the one of the external tick at that place */
static void t_cont_mid(void)
{
    double p = 60.0 / (24.0 * 120.0) * 24e6, t = T + p, tk[400];
    uint32_t k = 0, fb, f2, q, pos = 0, j, first;
    char line[160];
    reset(1, SYNC_AUTO, 120);
    while (k < 300u) {
        while (t <= T && k < 300u) {
            if (k == 48u)
                midi_in_enqueue(0xFA0Fu, MSRC_TRS, (uint32_t)(int64_t)(t - 0.1 * MS));
            if (k == 98u)
                midi_in_enqueue(0xFC0Fu, MSRC_TRS, (uint32_t)(int64_t)(t - 0.1 * MS));
            if (k == 130u) {
                midi_in_enqueue(0xFB0Fu, MSRC_TRS, (uint32_t)(int64_t)(t - 0.1 * MS));
                pos = sy.pos;                      /* (where the clock stopped, ticks: the F8 after FB is there) */
            }
            midi_in_enqueue(0xF80Fu, MSRC_TRS, (uint32_t)(int64_t)t);
            tk[k] = t;
            t += p;
            k++;
        }
        run_half();
    }
    run_ms(100);
    sends_until(T);
    f2 = find(0xF2u, 0);
    fb = find(0xFBu, 0);
    q = f2 < nout ? ((out[f2].pkt >> 16) & 0x7Fu) | ((out[f2].pkt >> 24) & 0x7Fu) << 7 : 0u;
    first = fb + 1u;
    while (first < nout && st_of(first) != 0xF8u)
        first++;
    j = 130u + (q * 6u - pos);                     /* the external F8 at the 16th's place */
    printf("clkout: CONTINUE where the clock stopped (tick %u): F2 %u; first F8 %+.3f ms from the external one at the 16th\n",
           pos, q, first < nout && j < 300u ? (out[first].due - tk[j]) / MS : 99.0);
    snprintf(line, sizeof line, "following TRS, CONTINUE off the 16ths (tick %u): F2 %u + FB, the first F8 at that 16th", pos);
    check(pos % 6u != 0u && q == (pos + 5u) / 6u && fb == f2 + 1u && first < nout && j < 300u &&
              fabs((out[first].due - tk[j]) / MS) < 0.2, line);
}

int main(void)
{
    t_tempo(120);
    t_tempo(97);
    t_tempo(173);
    t_tempo(40);
    t_tempo(240);
    t_off();
    t_change();
    t_swing();
    t_restart();
    t_ext();
    t_cont_mid();
    printf("clkout: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
