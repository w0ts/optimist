/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock: follow an external 24 PPQN clock as the transport (GLO > SYSTEM SYNC).
 * The input side (timestamped clock, start / continue / stop queued with the notes, the INT / USB / TRS
 * sources, the lost-clock stop) follows Melodee (Kerem Kilic, github.com/keremimo/melodee); the source
 * choice, the tempo filter, the latency compensation, the sample-accurate steps and song position are
 * SLOOP's.
 *
 * Source. SYNC AUTO follows TRS if it brings a clock, else USB, else the internal tempo; USB / TRS follow
 * only that input; INT never follows. A source is present after 4 F8 a steady period apart (each within
 * 1/4 + 2 ms of the last), and gone 500 ms after its last F8. The source changes only while stopped (never
 * mid-song, not even from the internal tempo); a START / CONTINUE while stopped takes its source at once if
 * it is preferred, or nothing is followed yet. Following a clock that stops while playing: the transport
 * stops after 500 ms (Melodee).
 *
 * Time. Each F8 is timestamped where it arrives, within 0.1 ms: the 10 kHz TIMER5 tick looks at the USB
 * endpoint and the TRS ring (usb.c usb_rx_peek, midi_uart.c uart_midi_peek; TIMER4 ticks, 24 MHz). An
 * expanding memory alpha-beta filter (Brookner, "Tracking and Kalman filtering made easy", ch. 1: the
 * least-squares line through the last N ticks, recursively) smooths the times and estimates the period:
 *   e = arrival - (t' + P),  t' += P + a e,  P += b e,  a = 2 (2N - 1) / (N (N + 1)),  b = 6 / (N (N + 1))
 * N grows by one each tick from 2 (exact through the first two ticks: a fast lock) to SY_NMAX (a narrow
 * loop: jitter filtered ~5x). A tempo change shows as residuals of one sign: their mean (1/4 EMA) above
 * SY_BTH / 4 = 1.5 x the noise (EMA of |e|) + 0.05 ms halves N (a wider loop until it has caught up) and
 * for SY_MAN ticks fits a quadratic instead (a steady acceleration: a tempo ramp). An arrival off by
 * more than 4 x the noise (at least 0.4 ms) is held as a suspect and not used: the next tick decides (back
 * on time: it was an outlier, a late USB frame; off again: the tempo jumped, the filter restarts from the
 * last two arrivals). One a period late: an F8 was lost (a dropped TRS byte), it is counted.
 *
 * Latency. The audio ISR renders a half buffer (256 samples) when the DMA starts the other one: sample k of
 * the half leaves the I2S DMA (256 + k) samples after that interrupt (5.8 .. 11.6 ms), at any CPU clock.
 * audio.c gives each block the TIMER4 time its first sample leaves (sync_out_t). Every block, the transport
 * clock goes to where the external clock will be when the block's last sample has left (+ SYNC_DAC_T):
 * position(t) = (ticks counted + (t - t') / P) x a tick. A step whose start falls inside the block starts
 * at its own sample (core.h ev_at: the voice renders from there), so it leaves when its F8 arrives. The
 * clock never moves back (an estimate that was ahead waits) and stops 3 periods past the last F8.
 * Start: the first F8 after FA is the downbeat (MIDI 1.0). With the clock running, the time of that F8 is
 * known (the next tick of the filter) and the downbeat is rendered to leave then, if it is still ahead by
 * the output latency (FA a tick before the F8, as hosts send it); else, or without a running clock, it
 * starts at once, late by the latency, every later step in time. Continue (FB) does the same at the song
 * position (F2, 16ths) or where the clock stopped. PLAY on the device while following arms a start at the
 * next F8; STOP stops.
 * SYNC_DAC_T: the codec's own delay (DAC filter, analog path): 0, as it is not known (no FM-1 measured). */
#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif
#ifndef SYNC_DAC_US
#define SYNC_DAC_US 0u                                   /* the codec's delay after the I2S DMA (unknown) */
#endif
#ifndef SY_NMAX
#define SY_NMAX 250u                                      /* the filter's longest memory, ticks */
#endif
#ifndef SY_BTH
#define SY_BTH 6                                          /* a tempo change: |mean residual| > SY_BTH / 4 x noise */
#endif
#ifndef SY_COOL
#define SY_COOL 2                                         /* .. then ticks before the next halving */
#endif
#ifndef SY_MAN
#define SY_MAN 96                                         /* .. and ticks of tracking it as a ramp */
#endif
/* measurement knobs (tests/clock_sync_test.c ablations; the firmware builds the defaults) */
#ifndef SY_EXACT
#define SY_EXACT 1                                        /* 0: steps at block starts, the clock at mid-block */
#endif
#ifndef SY_PREDICT
#define SY_PREDICT 1                                      /* 0: a start waits for its downbeat F8 */
#endif
#ifndef SY_FILTER
#define SY_FILTER 1                                       /* 0: a fixed-gain DLL (b 9/32, c 11/256) */
#endif
#define SYNC_DAC_T (SYNC_DAC_US * FM1_TICKS_PER_US)
#define SY_TICK_U (BEAT_U / 24u)                           /* units of a MIDI clock tick */
#define SY_TPS_Q8 ((uint32_t)((uint64_t)FM1_TICKS_PER_US * 256000000u / FS))   /* TIMER4 ticks a sample, Q8 */
#define SY_PMIN (8u * 1000u * FM1_TICKS_PER_US)            /* periods accepted: 8 .. 130 ms (312 .. 19 BPM) */
#define SY_PMAX (130u * 1000u * FM1_TICKS_PER_US)
#define SY_LOST (500u * 1000u * FM1_TICKS_PER_US)          /* no F8 for that long: gone (Melodee) */
#define SY_US (FM1_TICKS_PER_US)
/* G_SYNC: SYNC_INT .. SYNC_AUTO (core.h) */

static volatile uint32_t sync_out_t;   /* TIMER4 time the block being rendered starts to leave the DMA (audio.c) */
static struct {
    uint64_t tq;           /* the filtered time of the last F8 (TIMER4 ticks, Q8; its low 32 bits wrap with TIMER4) */
    uint32_t p;            /* the period, TIMER4 ticks Q8 */
    uint32_t r;            /* units (1 sample at 1 BPM) per TIMER4 tick, Q24: SY_TICK_U / period */
    uint32_t last, sus;    /* arrival of the last F8; of the suspect one */
    uint32_t n, n0;        /* F8s counted; n - n0 = the position (ticks) of the last one */
    uint32_t pos;          /* the position (ticks) the next start / continue begins at */
    uint32_t bpm;          /* the tempo, Q16 */
    int32_t m, bias;       /* noise: EMA of |e|; tempo change: EMA of e (TIMER4 ticks) */
    int64_t acc;           /* a ramp: the period's change a tick, TIMER4 ticks Q24 */
    uint8_t have;          /* F8s seen: 0, 1, 2 = locked (a period) */
    uint8_t nm, cool;      /* the filter's memory N; ticks before N may halve again */
    uint8_t man;           /* ticks left of tracking a tempo ramp */
    uint8_t suspect;       /* the last F8 was off: not used yet */
    uint8_t arm;           /* START / CONTINUE: start when the clock reaches pos (n0 set) */
    uint8_t stop;          /* for events_block: stop now */
    uint8_t mode;          /* G_SYNC as last seen */
    uint8_t src;           /* the source followed: MSRC_USB / MSRC_TRS, 0 = none (internal tempo) */
} sy;
#define SY_STEADY 4u                                       /* F8s a steady period apart: a source is present */
static struct { uint32_t last, dt; uint8_t cnt; } sy_pr[3];   /* per source (MSRC_*): presence */

static uint32_t sy_div(uint64_t n, uint32_t d)            /* n / d (no 64-bit division in the runtime) */
{
    uint64_t r = 0;
    uint32_t q = 0, i;
    for (i = 64; i--;) {
        r = r << 1 | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            if (i < 32u)
                q |= 1u << i;
        }
    }
    return q;
}

static void sy_period(uint32_t p)                         /* a new period estimate: rate, tempo, G_BPM */
{
    int32_t b;
    sy.p = (uint32_t)clamp((int32_t)p, (int32_t)SY_PMIN << 8, (int32_t)(SY_PMAX << 8) - 1);
    sy.r = sy_div((uint64_t)SY_TICK_U << 32, sy.p);
    sy.bpm = (uint32_t)(((uint64_t)sy.r * SY_TPS_Q8) >> 16);
    b = (int32_t)((sy.bpm + 32768u) >> 16);               /* shown with 0.6 BPM of hysteresis */
    if ((int32_t)sy.bpm - song.g[G_BPM] * 65536 > 39322 || song.g[G_BPM] * 65536 - (int32_t)sy.bpm > 39322)
        song.g[G_BPM] = (int16_t)clamp(b, GP[G_BPM].min, GP[G_BPM].max);
}

static void sy_restart(uint32_t t, uint32_t p)            /* the filter from an arrival and a period */
{
    sy_period(p);
    sy.tq = (uint64_t)t << 8;
    sy.acc = 0;
    sy.nm = 2;
    sy.bias = 0;
    sy.suspect = 0;
    sy.have = 2;
    sy.m = 1000 * SY_US;                                   /* (the noise unknown again: a wide gate) */
}

static int sy_present(uint32_t s, uint32_t now)
{
    return sy_pr[s].cnt >= SY_STEADY && now - sy_pr[s].last < SY_LOST;
}

/* the sources SYNC allows, TRS first */
static uint32_t sy_allowed(uint32_t s)
{
    uint32_t m = (uint32_t)song.g[G_SYNC];
    return m == SYNC_AUTO || (m == SYNC_USB && s == MSRC_USB) || (m == SYNC_TRS && s == MSRC_TRS);
}

static void sy_follow_src(uint32_t s)                     /* a new source: its clock from scratch */
{
    sy.src = (uint8_t)s;
    sy.have = 0;
    sy.arm = 0;
    sy.stop = 0;
}

/* once a block, stopped: the source to follow (TRS, USB, or none); never while playing */
static void sync_select(uint32_t now)
{
    uint32_t s, best = 0;
    if (song.g[G_SYNC] != sy.mode) {                      /* SYNC changed: a following transport stops */
        sy.mode = (uint8_t)song.g[G_SYNC];
        if (sy.src && song.playing)
            sy.stop = 1;
        else
            sy_follow_src(0);
    }
    if (song.g[G_MIDI] != sy.src)
        song.g[G_MIDI] = (int16_t)sy.src;                  /* (read-only: the source shown, the editor) */
    if (song.playing || sy.arm)
        return;
    for (s = MSRC_TRS; s >= MSRC_USB && !best; s--)
        if (sy_allowed(s) && sy_present(s, now))
            best = s;
    if (best != sy.src)
        sy_follow_src(best);
    if (song.g[G_MIDI] != sy.src)
        song.g[G_MIDI] = (int16_t)sy.src;                  /* (read-only: the source shown, the editor) */
}

/* an F8 of the source followed */
static void sy_tick(uint32_t t)
{
    uint32_t dt = t - sy.last, pred, gate;
    int32_t e, ae;
    sy.last = t;
    sy.n++;
    if (sy.have < 2u) {
        if (sy.have && dt >= SY_PMIN && dt < SY_PMAX) {
            sy_restart(t, dt << 8);
        } else {
            sy.have = 1;
        }
        return;
    }
    sy.tq += sy.p;                                         /* the prediction: one period on */
    sy.tq += (uint64_t)(sy.acc >> 17);                     /* (and the ramp's) */
    sy_period((uint32_t)((int32_t)sy.p + (int32_t)(sy.acc >> 16)));
    pred = (uint32_t)(sy.tq >> 8);
    e = (int32_t)(t - pred);
    ae = e < 0 ? -e : e;
#if !SY_FILTER
    if ((uint32_t)ae > (sy.p >> 9)) {
        sy_restart(t, dt << 8);
    } else {
        sy.tq += (uint64_t)(((int64_t)e * 9) << 3);
        sy_period((uint32_t)((int32_t)sy.p + e * 11));
    }
    return;
#endif
    gate = (uint32_t)(4 * sy.m) > 400u * SY_US ? (uint32_t)(4 * sy.m) : 400u * SY_US;
    if ((uint32_t)ae > gate && sy.nm >= 8u && (uint32_t)(e - (int32_t)(sy.p >> 8)) + gate <= 2u * gate) {
        sy.n++;                                            /* a period late: an F8 was lost */
        sy.tq += sy.p;
        e -= (int32_t)(sy.p >> 8);
        ae = e < 0 ? -e : e;
    }
    if ((uint32_t)ae > gate && sy.nm >= 8u) {
        if (!sy.suspect) {                                 /* held: the next F8 decides */
            sy.suspect = 1;
            sy.sus = t;
            return;
        }
        if (dt >= SY_PMIN && dt < SY_PMAX)                 /* off twice: the tempo jumped */
            sy_restart(t, dt << 8);
        else
            sy.tq = (uint64_t)t << 8;
        return;
    }
    sy.suspect = 0;
    if (sy.nm >= 3u && sy.man) {                           /* a ramp: the quadratic through the last N */
        uint32_t n = sy.nm, d = n * (n + 1u) * (n + 2u);
        int32_t g = (int32_t)sy_div((uint64_t)(3u * (3u * n * n - 3u * n + 2u)) << 16, d);
        int32_t h = (int32_t)sy_div((uint64_t)(18u * (2u * n - 1u)) << 16, d);
        int64_t k2 = (int64_t)sy_div((uint64_t)60u << 32, d);   /* Q32 */
        sy.tq += (uint64_t)(((int64_t)e * g) >> 8);
        sy_period((uint32_t)((int32_t)sy.p + (int32_t)(((int64_t)e * h) >> 8)));
        sy.acc += (e * k2) >> 8;                           /* Q24 */
        sy.man--;
    } else {
        sy.acc -= sy.acc >> 3;                             /* (no ramp: its estimate fades) */
        uint32_t n = sy.nm, d = n * (n + 1u);
        int32_t a = (int32_t)((2u * (2u * n - 1u) << 16) / d), b = (int32_t)((6u << 16) / d);
        sy.tq += (uint64_t)(((int64_t)e * a) >> 8);
        sy_period((uint32_t)((int32_t)sy.p + (int32_t)(((int64_t)e * b) >> 8)));
    }
    {
        int32_t dv = e - sy.bias;                          /* the noise: around the mean, not the lag */
        sy.m += ((dv < 0 ? -dv : dv) - sy.m) >> 4;
    }
    sy.bias += (e - sy.bias) >> 2;
    if (sy.cool)
        sy.cool--;
    if (sy.nm < SY_NMAX)
        sy.nm++;
    ae = sy.bias < 0 ? -sy.bias : sy.bias;
    if (sy.nm >= 8u && !sy.cool && ae > ((sy.m * SY_BTH) >> 2) + 50 * (int32_t)SY_US) {
        sy.nm = (uint8_t)(sy.nm / 2u);                     /* a tempo change: a wider loop */
        sy.cool = SY_COOL;
        sy.man = SY_MAN;
        sy.bias = 0;
    }
}

/* a clock / transport message (events_block, in queue order, any source) */
static void sync_msg(uint32_t st, uint32_t d1, uint32_t d2, uint32_t t, uint32_t s)
{
    if (st == 0xF8u) {
        uint32_t dt = t - sy_pr[s].last, d = dt - sy_pr[s].dt + sy_pr[s].dt / 4u + 2000u * SY_US;
        if (dt < SY_PMIN || dt >= SY_PMAX)
            sy_pr[s].cnt = 1;                              /* (the first, or after a gap) */
        else if (sy_pr[s].cnt >= 2u && d > sy_pr[s].dt / 2u + 4000u * SY_US)
            sy_pr[s].cnt = 2;                              /* not steady: a period, counting again */
        else if (sy_pr[s].cnt < SY_STEADY)
            sy_pr[s].cnt++;                                /* (steady: within 1/4 + 2 ms of the last period) */
        sy_pr[s].dt = dt;
        sy_pr[s].last = t;
    }
    if (s != sy.src) {                                     /* START / CONTINUE stopped: from a source to prefer */
        if ((st != 0xFAu && st != 0xFBu) || song.playing || sy.arm || !sy_allowed(s) ||
            (sy.src && sy.src > s && sy_present(sy.src, t)))
            return;
        sy_follow_src(s);
    }
    if (st == 0xF8u) {
        sy_tick(t);
    } else if (st == 0xFAu || st == 0xFBu) {
        if (st == 0xFAu)
            sy.pos = 0;
        sy.n0 = sy.n + 1u - sy.pos;                        /* the next F8 is at sy.pos */
        sy.arm = 1;
        sy.stop = 0;
    } else if (st == 0xFCu) {
        if (song.playing && !sy.arm)
            sy.pos = sy.n - sy.n0 + 1u;                    /* CONTINUE: from the next tick */
        sy.arm = 0;
        sy.stop = 1;
    } else if (st == 0xF2u && !song.playing) {
        sy.pos = (d1 | d2 << 7) * 6u;                      /* song position: 16ths */
    }
}

/* where the external clock will be at TIMER4 time x, in units from position 0 */
static int64_t sy_at(uint32_t x)
{
    uint32_t lim = 3u * (sy.p >> 8);
    int64_t dq = ((int64_t)(int32_t)(x - (uint32_t)(sy.tq >> 8)) << 8) - (int64_t)(sy.tq & 255u);
    if (dq > (int64_t)lim << 8)
        dq = (int64_t)lim << 8;                            /* a stalled clock: the steps stop */
    return (int64_t)(int32_t)(sy.n - sy.n0) * SY_TICK_U + ((dq * sy.r) >> 32);
}

static int64_t clk_abs(void) { return (int64_t)clk_beat * BEAT_U + clk_pos; }
static void clk_set(int64_t u)                            /* (u >= 0) */
{
    int64_t d = u - clk_abs();
    if (d >= 0 && d < (int64_t)BEAT_U) {                  /* (each block: on by less than a beat) */
        clk_pos += (uint32_t)d;
        if (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
        return;
    }
    clk_beat = sy_div((uint64_t)u, BEAT_U);
    clk_pos = (uint32_t)(u - (int64_t)clk_beat * BEAT_U);
}

/* the transport under the external clock, once a block of n samples after the MIDI input: start / stop,
 * then the clock to where the external one will be at the block's end (ev_map: where in the block each
 * position is, for the steps' samples); returns the units it moved */
static uint32_t sync_follow(uint32_t n)
{
    uint32_t t0 = sync_out_t + SYNC_DAC_T;
    int64_t p0, p1, c;
    if (sy.stop) {
        sy.stop = 0;
        if (song.playing)
            seq_stop();
    }
    if (sy.have < 2u) {                                    /* no period yet: the downbeat F8 starts it */
        if (song.playing && (uint32_t)(SYNC_NOW() - sy.last) > SY_LOST)
            seq_stop();
        else if (sy.arm && sy.have && (int32_t)(sy.n - sy.n0) >= (int32_t)sy.pos) {
            sy.arm = 0;
            seq_start();
            if (song.playing)
                clk_set((int64_t)sy.pos * SY_TICK_U);       /* (it holds there until the next F8) */
        }
        return 0;
    }
#if SY_EXACT
    p0 = sy_at(t0);
    p1 = sy_at(t0 + ((n * SY_TPS_Q8) >> 8));
#else
    p0 = p1 = sy_at(t0 + ((n / 2u * SY_TPS_Q8) >> 8));
#endif
    if (sy.arm) {                                          /* armed: start when the clock reaches pos */
        int64_t at = (int64_t)sy.pos * SY_TICK_U;
        if (p1 < at || (!SY_PREDICT && (int32_t)(sy.n - sy.n0) < (int32_t)sy.pos))
            return 0;
        sy.arm = 0;
        seq_start();
        if (!song.playing)
            return 0;
        clk_set(p0 > at ? p0 : at);                        /* late (no warning): from where it is now */
    } else if (!song.playing) {
        return 0;
    }
    if ((uint32_t)(SYNC_NOW() - sy.last) > SY_LOST) {
        seq_stop();                                        /* the clock is gone */
        sy.have = 0;
        return 0;
    }
    c = clk_abs();
    if (p1 - c > (int64_t)BEAT_U || c - p1 > (int64_t)BEAT_U) {   /* far off (a relock): there at once */
        if (p1 > c)
            clk_set(p1);
        return 0;
    }
    ev_map.on = 1;
    ev_map.n = (uint8_t)n;
    ev_map.adv = p1 > p0 ? (int32_t)(p1 - p0) : 0;
    ev_map.d = (int32_t)((p1 > c ? p1 : c) - p0);
    if (p1 <= c)
        return 0;                                          /* ahead: wait */
    clk_set(p1);
    return (uint32_t)(p1 - c);
}

/* UI PLAY / STOP while following: PLAY starts at the next F8, STOP stops */
static void sync_local(uint32_t req)
{
    if (req == 1u) {
        if (!song.playing && !sy.arm) {
            sy.pos = 0;
            sy.n0 = sy.n + 1u;
            sy.arm = 1;
        }
    } else {
        sy.arm = 0;
        sy.stop = 1;
    }
}
