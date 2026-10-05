/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock (firmware/src/clock_sync.c) on the host, with the audio timing of the firmware: a half
 * buffer of 256 samples is rendered when the DMA starts the other one, block b of it (32 samples) leaves
 * the DMA (256 + 32 b) samples later (audio.c sync_out_t); USB packets are seen by the 2 kHz poll and
 * timestamped half a poll back (usb.c). An external clock (F8 at 24 PPQN, FA / FB / FC, F2) drives a drum
 * hit on every 16th; each hit's time is the time its block leaves the DMA.
 *   phase   hit time - arrival time of the F8 of its step (the clock as it arrives), and - the F8's
 *           ideal (unjittered) time; the first hit after a start is reported apart (output latency)
 *   tempo   the estimate (sy.bpm) - the true tempo
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define TPS (24e6 / 44100.0)                      /* TIMER4 ticks a sample */
#define MS 24000.0
#define POLL 12000.0                              /* 2 kHz */
static double T;                                  /* host time of the half interrupt (ticks) */
typedef struct { double t; uint32_t pkt; } ev_t;
static ev_t ev[20000];
static uint32_t nev, evr;
typedef struct { double t; uint32_t idx; } hit_t;
static hit_t hit[20000];
static uint32_t nhit;
static double bpm_err_sum, bpm_err_max;
static uint32_t bpm_n;
static double (*true_bpm)(double t);

static void push(double t, uint32_t pkt)
{
    ev[nev].t = t;
    ev[nev++].pkt = pkt;
}

static void run_half(void)
{
    uint32_t b;
    int32_t o[2 * CTL];
    while (evr < nev) {                           /* what the polls before this interrupt have seen */
        double poll = ceil(ev[evr].t / POLL) * POLL;
        if (poll > T)
            break;
        usb_midi_rx_packet(ev[evr].pkt, (uint32_t)(int64_t)(poll - POLL / 2));
        evr++;
    }
    host_now = (uint32_t)(int64_t)T;
    for (b = 0; b < 8u; b++) {
        uint32_t a = drums.age, k;
        double out = T + (256.0 + 32.0 * b) * TPS;
        sync_out_t = (uint32_t)(int64_t)out;
        mix_block(o, CTL);
        if (drums.age != a)
            for (k = 0; k < NDRUM; k++)
                if (drums.v[k].age > a && nhit < 20000u) {
                    hit[nhit].t = out;
                    hit[nhit++].idx = TDRUM->seq_idx;
                }
    }
    if (true_bpm && sy.have == 2u && song.playing) {
        double e = fabs(sy.bpm / 65536.0 - true_bpm(T));
        bpm_err_sum += e;
        bpm_err_max = e > bpm_err_max ? e : bpm_err_max;
        bpm_n++;
    }
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
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 6, LV_NORM, 0);   /* a closed hat on every 16th */
    song.g[G_DRREV] = 0;
    song.playing = 0;
    transport_req = 0;
    mi_r = mi_w = 0;
    usb.config = 1;
    song.g[G_SYNC] = (int16_t)sync;
    sync_reset();
    T = 1e6;
    nev = evr = nhit = 0;
    bpm_err_sum = bpm_err_max = 0;
    bpm_n = 0;
    true_bpm = 0;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("sync: %-74s %s\n", what, ok ? "ok" : "FAIL");
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

/* the clock: ideal tick times from a tempo ramp over ticks (0 ticks: a jump) */
static double tick_t[20000], tick_a[20000], tick_bpm[20000];   /* ideal, arrival (jittered), tempo */
static uint32_t nticks;
static double tempo_at(double t)                  /* the tempo of the tick interval holding t */
{
    uint32_t lo = 0, hi = nticks - 1u;
    while (lo + 1u < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (tick_t[m] <= t) lo = m; else hi = m;
    }
    return tick_bpm[lo];
}

typedef struct { double mean, mabs, max, min; uint32_t n; } st_t;
static st_t stats(const double *v, uint32_t n)
{
    st_t s = {0, 0, -1e9, 1e9, n};
    uint32_t i;
    for (i = 0; i < n; i++) {
        s.mean += v[i];
        s.mabs += fabs(v[i]);
        s.max = v[i] > s.max ? v[i] : s.max;
        s.min = v[i] < s.min ? v[i] : s.min;
    }
    s.mean /= n ? n : 1;
    s.mabs /= n ? n : 1;
    return s;
}

/* a run: pre-roll of 2 beats of clock, FA, the downbeat F8 and `beats` beats; hits vs ticks */
static void follow(const char *name, double b0, double b1, double ramp_beats, double jit_ms, uint32_t beats,
                   double max_mean, double max_abs)
{
    static double e_arr[4000], e_id[4000];
    uint32_t k, nt = 48u + beats * 24u, ne = 0, down = 48u, steps_ok = 1;
    double t = 2e6, first;
    char line[160];
    reset(SYNC_USB);
    for (k = 0; k < nt; k++) {
        double f = k < down ? 0.0 : ramp_beats <= 0 ? 1.0 : (k - down) / (ramp_beats * 24.0);
        tick_bpm[k] = b0 + (b1 - b0) * (f > 1.0 ? 1.0 : f);
        tick_t[k] = t;
        tick_a[k] = t + jit(jit_ms);
        if (k == down)
            push(tick_a[k] - 0.05 * MS, 0xFA0Fu);
        push(tick_a[k], 0xF80Fu);
        t += 60.0 / (24.0 * tick_bpm[k]) * 24e6;
    }
    nticks = nt;
    true_bpm = tempo_at;
    while (T < tick_t[nt - 1] - 30 * MS)
        run_half();
    first = (hit[0].t - tick_a[down]) / MS;
    for (k = 1; k < nhit && down + 6u * k < nt; k++) {
        steps_ok &= hit[k].idx == k % 16u;
        e_arr[ne] = (hit[k].t - tick_a[down + 6u * k]) / MS;
        e_id[ne++] = (hit[k].t - tick_t[down + 6u * k]) / MS;
    }
    {
        st_t a = stats(e_arr, ne), d = stats(e_id, ne);
        double bm = bpm_err_sum / (bpm_n ? bpm_n : 1);
        snprintf(line, sizeof line, "%s: %u hits, the first %+.2f ms", name, nhit, first);
        check(nhit >= beats * 4u - 1u && steps_ok && hit[0].idx == 0u, line);
        printf("sync:   phase vs arrival  mean %+.3f  |mean| %.3f  min %+.3f  max %+.3f ms\n", a.mean, a.mabs, a.min, a.max);
        printf("sync:   phase vs ideal    mean %+.3f  |mean| %.3f  min %+.3f  max %+.3f ms\n", d.mean, d.mabs, d.min, d.max);
        printf("sync:   tempo error       mean %.3f  max %.3f BPM\n", bm, bpm_err_max);
        snprintf(line, sizeof line, "%s: |mean phase| <= %.1f ms, |max| <= %.1f ms", name, max_mean, max_abs);
        check(fabs(d.mean) <= max_mean && d.max <= max_abs && -d.min <= max_abs, line);
    }
}

static void t_transport(void)
{
    uint32_t k, n = 0, idx_after = 0xFFu, ok;
    double t = 2e6, p = 60.0 / (24.0 * 120.0) * 24e6, t_fb = 0, t_fc = 0, h_after = 0;
    reset(SYNC_USB);
    for (k = 0; k < 400u; k++, t += p) {
        if (k == 24u)
            push(t - 0.05 * MS, 0xFA0Fu);
        if (k == 24u + 96u) {
            push(t - 0.05 * MS, 0xFC0Fu);         /* stop after a bar */
            t_fc = t;
        }
        if (k == 24u + 96u + 48u)
            push(t - 0.1 * MS, 0x0108F203u);      /* song position: 16th 8 + 128 (d2 = 1) */
        if (k == 24u + 96u + 49u) {
            push(t - 0.05 * MS, 0xFB0Fu);         /* continue */
            t_fb = t;
        }
        push(t, 0xF80Fu);
    }
    while (T < t_fc - 100 * MS)
        run_half();
    ok = song.playing;
    while (T < t_fc + 50 * MS)
        run_half();
    ok &= !song.playing;
    n = nhit;
    while (T < t_fb + 400 * MS)
        run_half();
    if (nhit > n) {
        idx_after = hit[n].idx;
        h_after = hit[n + 1].t - (t_fb + 6 * p);   /* the second hit after CONTINUE vs its F8 */
    }
    check(ok, "START plays, STOP stops (F8 still running)");
    printf("sync:   continue at 16th 136: first step %u, the next %+.3f ms from its F8\n", idx_after,
           h_after / MS);
    check(idx_after == 136u % 16u && fabs(h_after / MS) < 1.0,
          "SPP 136 + CONTINUE: the pattern resumes at step 8 (136 mod 16), in time");
    while (T < t + 200 * MS)
        run_half();
    n = nhit;
    while (T < t + 700 * MS)
        run_half();
    check(!song.playing && nhit == n, "the clock stops: the transport stops after 500 ms");
}

int main(void)
{
    follow("steady 120 BPM", 120, 120, 0, 0, 64, 0.5, 1.0);
    follow("120 BPM, +-1 ms jitter", 120, 120, 0, 1.0, 64, 0.5, 2.0);
    follow("steady 60 BPM", 60, 60, 0, 0, 32, 0.5, 1.0);
    follow("steady 200 BPM, +-0.5 ms jitter", 200, 200, 0, 0.5, 96, 0.5, 1.5);
    follow("ramp 100 -> 140 BPM over 16 beats", 100, 140, 16, 0, 48, 1.0, 2.5);
    follow("jump 100 -> 140 BPM at the downbeat", 100, 140, 0, 0, 48, 1.0, 2.5);
    follow("ramp 140 -> 90 BPM over 8 beats, +-1 ms", 140, 90, 8, 1.0, 48, 1.0, 3.5);
    t_transport();
    printf("sync: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
