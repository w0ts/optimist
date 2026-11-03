/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock: follow an external 24 PPQN clock as the transport (GLO > SYSTEM SYNC).
 * The input side (timestamped clock, start / continue / stop queued with the notes, the INT / USB / TRS
 * sources, the lost-clock stop) follows Melodee (Kerem Kilic, github.com/keremimo/melodee); the source
 * choice, the tempo filter, the latency compensation, the sample-accurate steps and song position are
 * SLOOP's.
 *
 * Source. SYNC AUTO follows TRS if it brings a clock, else USB, else BLE, else the internal tempo; USB / TRS / BLE
 * follow only that input; INT never follows. BLE's F8s carry the sender's own time (io/midi/midi_ble.c ble_in_time):
 * the connection interval's bunching taken out, a millisecond's grain left for the filter. A source is present after 4 F8 a steady period apart (each within
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
 * SYNC_DAC_T: the codec's own delay (DAC filter, analog path): 0, as it is not known (no FM-1 measured).
 * Offset (GLOBAL > MIDI, settings of the FM-1): the clock's source's, CLK OFS for a wired clock (USB, TRS), BT OFS for a
 * Bluetooth (BLE) one; while the audio streams to Bluetooth, what our side of the stream adds on top (bt/bt_acl.c
 * bta_lat_t): ms the steps go earlier (< 0 later) than the clock heard. Keys played by hand are not moved: on Bluetooth
 * they are as late as the stream. */
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
#if FELUCCA_MASTER_COMP                                   /* LIMIT > CEIL (master_comp.c, on by default since
                                                          * 2026-10-11): the mix leaves MLIM_N samples (1.45 ms) later */
#define SYNC_LIM_T (mlim.on ? (uint32_t)((MLIM_N * SY_TPS_Q8) >> 8) : 0u)
#else
#define SYNC_LIM_T 0u
#endif
#define SY_PMIN (8u * 1000u * FM1_TICKS_PER_US)            /* periods accepted: 8 .. 130 ms (312 .. 19 BPM) */
#define SY_PMAX (130u * 1000u * FM1_TICKS_PER_US)
#define SY_LOST (500u * 1000u * FM1_TICKS_PER_US)          /* no F8 for that long: gone (Melodee) */
#define SY_US (FM1_TICKS_PER_US)
/* G_SYNC: SYNC_INT .. SYNC_BLE (core.h) */
#define SY_OFS_MIN (-100)                                  /* CLK OFS / BT OFS, ms (op_project.c's lines) */
#define SY_OFS_MAX 250
#ifndef FELUCCA_LINK
#define FELUCCA_LINK 0                                     /* (params.c: LINK offered only in builds with it) */
#endif
static int16_t sync_ofs[3];                                /* [0] CLK OFS, [1] BT OFS, [2] LINK OFS (Link builds), ms (the settings record) */
#if FELUCCA_LINK || (defined(FELUCCA_WIFI) && FELUCCA_WIFI)
/* Ableton Link over Wi-Fi (wifi/wifi_link.c): LINK_OFF no session, LINK_ON tempo and beat shared, LINK_ONSS also play / stop (the default,
 * what Link did before the setting; stored as 0 in the record's ext bytes: persist_rec.c link_sel). The list reads OFF, ON, ON+S/S */
enum { LINK_OFF, LINK_ON, LINK_ONSS };
static uint8_t link_mode = LINK_ONSS;
static volatile uint8_t link_hop;                          /* AUTO found Link while playing: the Link side sends a START on the
                                                            * session's next bar (wl_clock), sync_msg takes it although playing:
                                                            * the steps hop there (the user, 2026-11-03: "it should just join the
                                                            * link session and hop", "on the next bar") */
static volatile uint8_t link_tempo_req;                    /* the user turned the tempo while Link drives it (op_tempo.c, tap tempo): sent
                                                            * to the session once by the Link side (wl_push_own), which then
                                                            * brings it back to every peer (the user, 2026-11-03: "in link mode we
                                                            * should be able to change the tempo") */
static uint8_t link_jam;                                   /* MODE JAM (the Wi-Fi page) is on: Link runs whatever LINK says (set by ui/opt_wifi.h every pass; never stored) */
#define link_eff() ((uint8_t)(link_jam && link_mode == LINK_OFF ? LINK_ON : link_mode))   /* the LINK in force: OFF is ON while JAM; the stored link_mode is untouched */
#endif
static int32_t sy_lead;                                    /* this block's offset, TIMER4 ticks (sync_follow) */
#if FELUCCA_BT_AUDIO
static int32_t bta_lat_t(void);
#endif
#if FELUCCA_BLE
#define SY_BT_OFS 1                                        /* BT OFS: a Bluetooth (BLE) clock can be followed */
#else
#define SY_BT_OFS 0
#endif
static const param_desc_t SY_OFS_D[3] = {PU("CLK OFS", F_INT, SY_OFS_MIN, SY_OFS_MAX, 0, "ms"),
                                         PU("BT OFS", F_INT, SY_OFS_MIN, SY_OFS_MAX, 0, "ms"),
                                         PU("LINK OFS", F_INT, SY_OFS_MIN, SY_OFS_MAX, 0, "ms")};

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
    uint8_t src;           /* the source followed: MSRC_USB / MSRC_TRS / MSRC_BLE, 0 = none (internal tempo) */
} sy;
/* the offset of the clock followed (the user, 2026-10-25: "one for usb, one for BLE ... like the name implies"): BT OFS
 * for a Bluetooth (BLE) clock, LINK OFS for Link, CLK OFS for a wired one (USB, TRS); TEMPO's nudge sets it (op_tempo.c) */
static uint32_t sync_ofs_ix(void) { return sy.src == MSRC_BLE ? 1u : FELUCCA_LINK && sy.src == MSRC_LINK ? 2u : 0u; }   /* (LINK OFS: its own since 2026-10-29) */
static int32_t sync_lead_t(void)                           /* the offset now: the source's, and the Bluetooth audio's own
                                                          * delay on top while it streams (bt/bt_acl.c bta_lat_t) */
{
    int32_t t = sync_ofs[FAST_CALL(seqev, sync_ofs_ix)()] * 1000 * (int32_t)FM1_TICKS_PER_US;
#if FELUCCA_BT_AUDIO
    int32_t bt = FAST_CALL(seqev, bta_lat_t)();
    if (bt >= 0)
        t += bt;
#endif
    return t;
}
#define SY_STEADY 4u                                       /* F8s a steady period apart: a source is present */
#ifndef FELUCCA_USB_HOST
#define FELUCCA_USB_HOST 0                                 /* the USB MIDI host (the release: fm1-base's; MSRC_USBH) */
#endif
#if FELUCCA_USB_HOST
#define SY_SRC_LAST MSRC_USBH                              /* the last source sync_select looks at */
#elif FELUCCA_LINK
#define SY_SRC_LAST MSRC_LINK
#else
#define SY_SRC_LAST MSRC_BLE
#endif
/* a USB host's device (MSRC_USBH) is the USB port's clock: SYNC USB follows it, AUTO ranks it as USB, CLK shows USB */
#define SY_USB(s) ((s) == MSRC_USB || (FELUCCA_USB_HOST && (s) == MSRC_USBH))
#define SY_SHOWN(s) (FELUCCA_USB_HOST && (s) == MSRC_USBH ? (uint32_t)MSRC_USB : (uint32_t)(s))
static struct { uint32_t last, dt; uint8_t cnt; } sy_pr[SY_SRC_LAST + 1];   /* per source (MSRC_*): presence */

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

/* the sources SYNC allows */
static uint32_t sy_allowed(uint32_t s)
{
    uint32_t m = (uint32_t)song.g[G_SYNC];
    return m == SYNC_AUTO || (m == SYNC_USB && SY_USB(s)) || (m == SYNC_TRS && s == MSRC_TRS) ||
#if FELUCCA_LINK
           (m == SYNC_LINK && s == MSRC_LINK) ||
#endif
           (m == SYNC_BLE && s == MSRC_BLE);
}

#if FELUCCA_LINK
/* AUTO's preference: LINK 5 (a session the user joined on purpose wins over any cable clock, 2026-10-27), TRS 4,
 * USB 3, BLE 1 (the most jitter last), none 0 */
static uint32_t sy_rank(uint32_t s)
{
    if (s == MSRC_LINK) return 5u;
    if (s == MSRC_TRS) return 4u;
    if (SY_USB(s)) return 3u;
    if (s == MSRC_BLE) return 1u;
    return 0u;
}
#else
/* AUTO's preference: TRS 3, USB 2, BLE 1 (the most jitter last), none 0 */
static uint32_t sy_rank(uint32_t s) { return s == MSRC_BLE ? 1u : SY_USB(s) ? (uint32_t)MSRC_USB : s; }
#endif

static void sy_follow_src(uint32_t s)                     /* a new source: its clock from scratch */
{
    sy.src = (uint8_t)s;
    sy.have = 0;
    sy.arm = 0;
    sy.stop = 0;
}

/* once a block, stopped: the source to follow (TRS, USB, LINK, BLE, or none); never while playing */
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
    if (song.g[G_MIDI] != (int16_t)SY_SHOWN(sy.src))
        song.g[G_MIDI] = (int16_t)SY_SHOWN(sy.src);        /* (read-only: the source shown, the editor) */
#if FELUCCA_LINK
    if (song.playing && !sy.arm && sy.src != MSRC_LINK && FAST_CALL(seqev, sy_allowed)(MSRC_LINK) &&
        FAST_CALL(seqev, sy_present)(MSRC_LINK, now) && FAST_CALL(seqev, sy_rank)(MSRC_LINK) > FAST_CALL(seqev, sy_rank)(sy.src))
        link_hop = 1;                                      /* (playing, Link there and preferred: its next bar's START, sync_msg) */
#endif
    if (song.playing || sy.arm)
        return;
    for (s = MSRC_USB; s <= SY_SRC_LAST; s++)
        if (FAST_CALL(seqev, sy_allowed)(s) && FAST_CALL(seqev, sy_present)(s, now) && FAST_CALL(seqev, sy_rank)(s) > FAST_CALL(seqev, sy_rank)(best))
            best = s;
    if (best != sy.src)
        sy_follow_src(best);
    if (song.g[G_MIDI] != (int16_t)SY_SHOWN(sy.src))
        song.g[G_MIDI] = (int16_t)SY_SHOWN(sy.src);        /* (read-only: the source shown, the editor) */
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
            FAST_CALL(seqev, sy_restart)(t, dt << 8);
        } else {
            sy.have = 1;
        }
        return;
    }
    sy.tq += sy.p;                                         /* the prediction: one period on */
    sy.tq += (uint64_t)(sy.acc >> 17);                     /* (and the ramp's) */
    FAST_CALL(seqev, sy_period)((uint32_t)((int32_t)sy.p + (int32_t)(sy.acc >> 16)));
    pred = (uint32_t)(sy.tq >> 8);
    e = (int32_t)(t - pred);
    ae = e < 0 ? -e : e;
#if !SY_FILTER
    if ((uint32_t)ae > (sy.p >> 9)) {
        FAST_CALL(seqev, sy_restart)(t, dt << 8);
    } else {
        sy.tq += (uint64_t)(((int64_t)e * 9) << 3);
        FAST_CALL(seqev, sy_period)((uint32_t)((int32_t)sy.p + e * 11));
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
            FAST_CALL(seqev, sy_restart)(t, dt << 8);
        else
            sy.tq = (uint64_t)t << 8;
        return;
    }
    sy.suspect = 0;
    if (sy.nm >= 3u && sy.man) {                           /* a ramp: the quadratic through the last N */
        uint32_t n = sy.nm, d = n * (n + 1u) * (n + 2u);
        int32_t g = (int32_t)FAST_CALL(seqev, sy_div)((uint64_t)(3u * (3u * n * n - 3u * n + 2u)) << 16, d);
        int32_t h = (int32_t)FAST_CALL(seqev, sy_div)((uint64_t)(18u * (2u * n - 1u)) << 16, d);
        int64_t k2 = (int64_t)FAST_CALL(seqev, sy_div)((uint64_t)60u << 32, d);   /* Q32 */
        sy.tq += (uint64_t)(((int64_t)e * g) >> 8);
        FAST_CALL(seqev, sy_period)((uint32_t)((int32_t)sy.p + (int32_t)(((int64_t)e * h) >> 8)));
        sy.acc += (e * k2) >> 8;                           /* Q24 */
        sy.man--;
    } else {
        sy.acc -= sy.acc >> 3;                             /* (no ramp: its estimate fades) */
        uint32_t n = sy.nm, d = n * (n + 1u);
        int32_t a = (int32_t)((2u * (2u * n - 1u) << 16) / d), b = (int32_t)((6u << 16) / d);
        sy.tq += (uint64_t)(((int64_t)e * a) >> 8);
        FAST_CALL(seqev, sy_period)((uint32_t)((int32_t)sy.p + (int32_t)(((int64_t)e * b) >> 8)));
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
#if FELUCCA_LINK
    if (s != sy.src && st == 0xFAu && s == MSRC_LINK && link_hop && song.playing && FAST_CALL(seqev, sy_allowed)(s)) {
        link_hop = 0;                                      /* (the hop: Link followed from its bar's START, while playing) */
        sy_follow_src(s);
        sy.last = t;                                       /* (its clock is here: not "gone" before its first F8) */
    } else
#endif
    if (s != sy.src) {                                     /* START / CONTINUE stopped: from a source to prefer */
        if ((st != 0xFAu && st != 0xFBu) || song.playing || sy.arm || !FAST_CALL(seqev, sy_allowed)(s) ||
            (sy.src && FAST_CALL(seqev, sy_rank)(sy.src) > FAST_CALL(seqev, sy_rank)(s) && FAST_CALL(seqev, sy_present)(sy.src, t)))
            return;
        sy_follow_src(s);
    }
    if (st == 0xF8u) {
        sy_tick(t);
    } else if (st == 0xFAu || st == 0xFBu) {
#if FELUCCA_LINK
        if (s == MSRC_LINK)
            link_hop = 0;                                  /* (a hop's START: done) */
#endif
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
    uint32_t lim = 3u * (sy.p >> 8) + (sy_lead > 0 ? (uint32_t)sy_lead : 0u);   /* (an offset: that far ahead too) */
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
    uint32_t t0;
    int64_t p0, p1, c;
    sy_lead = sync_lead_t();
    t0 = sync_out_t + SYNC_DAC_T + SYNC_LIM_T + (uint32_t)sy_lead;
    if (sy.stop) {
        sy.stop = 0;
        if (song.playing)
            FAST_CALL(seqev, seq_stop)();
    }
    if (sy.have < 2u) {                                    /* no period yet: the downbeat F8 starts it */
        if (song.playing && (uint32_t)(SYNC_NOW() - sy.last) > SY_LOST)
            FAST_CALL(seqev, seq_stop)();
        else if (sy.arm && sy.have && (int32_t)(sy.n - sy.n0) >= (int32_t)sy.pos) {
            sy.arm = 0;
            FAST_CALL(seqev, seq_start)();
            if (song.playing)
                FAST_CALL(seqev, clk_set)((int64_t)sy.pos * SY_TICK_U);       /* (it holds there until the next F8) */
        }
        return 0;
    }
#if SY_EXACT
    p0 = FAST_CALL(seqev, sy_at)(t0);
    p1 = FAST_CALL(seqev, sy_at)(t0 + ((n * SY_TPS_Q8) >> 8));
#else
    p0 = p1 = FAST_CALL(seqev, sy_at)(t0 + ((n / 2u * SY_TPS_Q8) >> 8));
#endif
    if (sy.arm) {                                          /* armed: start when the clock reaches pos */
        int64_t at = (int64_t)sy.pos * SY_TICK_U;
        if (p1 < at || (!SY_PREDICT && (int32_t)(sy.n - sy.n0) < (int32_t)sy.pos))
            return 0;
        sy.arm = 0;
        FAST_CALL(seqev, seq_start)();
        if (!song.playing)
            return 0;
        FAST_CALL(seqev, clk_set)(p0 > at ? p0 : at);                        /* late (no warning): from where it is now */
    } else if (!song.playing) {
        return 0;
    }
    if ((uint32_t)(SYNC_NOW() - sy.last) > SY_LOST) {
        FAST_CALL(seqev, seq_stop)();                                        /* the clock is gone */
        sy.have = 0;
        return 0;
    }
    c = FAST_CALL(seqev, clk_abs)();
    if (p1 - c > (int64_t)BEAT_U || c - p1 > (int64_t)BEAT_U) {   /* far off (a relock): there at once */
        if (p1 > c)
            FAST_CALL(seqev, clk_set)(p1);
        return 0;
    }
    ev_map.on = 1;
    ev_map.n = (uint8_t)n;
    ev_map.adv = p1 > p0 ? (int32_t)(p1 - p0) : 0;
    ev_map.d = (int32_t)((p1 > c ? p1 : c) - p0);
    if (p1 <= c)
        return 0;                                          /* ahead: wait */
    FAST_CALL(seqev, clk_set)(p1);
    return (uint32_t)(p1 - c);
}

/* UI PLAY / STOP while following: PLAY starts at the next F8, STOP stops */
static void sync_local(uint32_t req)
{
    if (req == 1u) {
#if FELUCCA_LINK
        if (sy.src == MSRC_LINK && !song.playing && !sy.arm) {
            link_hop = 1;                                  /* (PLAY following Link: on the session's next bar, its START) */
            return;
        }
#endif
        if (!song.playing && !sy.arm) {
            sy.pos = 0;
            sy.n0 = sy.n + 1u;
            sy.arm = 1;
        }
    } else {
#if FELUCCA_LINK
        link_hop = 0;                                      /* (STOP: no hop pending) */
#endif
        sy.arm = 0;
        sy.stop = 1;
    }
}
