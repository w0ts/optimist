/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock: follow an external 24 PPQN clock (GLO > SYSTEM SYNC = USB / TRS), or send one (SYNC = OUT).
 * The input side (timestamped clock, start / continue / stop queued with the notes, the INT / USB / TRS
 * choice, the lost-clock stop) follows Melodee (Kerem Kilic, github.com/keremimo/melodee); the tempo
 * filter, the latency compensation, song position and the clock out are SLOOP's.
 *
 * Latency. The audio ISR renders a half buffer (HALF_FRAMES = 256 samples) when the DMA starts playing
 * the other one: sample k of the half being rendered leaves the I2S DMA (HALF_FRAMES + k) samples after
 * that interrupt (5.8 .. 11.6 ms), whatever the CPU clock. audio.c gives each block the TIMER4 time its
 * first sample leaves (sync_out_t, from the interrupt's entry time, filtered: sync_anchor). A step of the
 * sequencer sounds at the start of the first block whose clock (at the block start) has reached it, so up
 * to a block (CTL = 32 samples, 0.73 ms) after its exact time.
 *
 * Follow (USB / TRS). Each F8 is timestamped where it arrives (the 2 kHz poll: half a poll period is taken
 * off, the mean wait; TRS bytes found together are spread back by a byte time each: midi_uart.c). A
 * second-order delay-locked loop (Adriaensen, "Using a DLL to filter time", 2005) smooths the times and
 * estimates the period: t' = t' + P + b e, P = P + c e, e = arrival - (t' + P), b = 9/32, c = 11/256
 * (in TIMER4 Q8: P += 11 e; a loop bandwidth of ~0.2 tick^-1): a +-1 ms jitter is filtered to ~0.3 ms,
 * a tempo ramp is followed with a lag of (period change per tick) / c. Every block, the transport clock
 * (clk_beat / clk_pos) is set to where the external clock will be when the middle of this block leaves
 * the DMA: position = ticks counted + (sync_out_t + CTL / 2 + SYNC_DAC_T - t') / P. So the audio of a
 * step leaves when the clock that marks it arrives (the half block centres the block quantisation: the
 * step sounds within +-CTL / 2 of it). The clock never moves back (if the estimate was ahead, it waits)
 * and never runs more than 3 periods past the last F8 (a stalled clock stops the steps); 500 ms without
 * F8 stops the transport (Melodee). The tempo (G_BPM, shown, the delay and the LFOs) follows the estimate.
 * Start: the first F8 after FA is the downbeat (MIDI 1.0); the transport starts in the block that sees
 * it, already at the position the clock will have when that block is heard: the first step sounds late
 * by the output latency (it cannot sound before it is known), every later one in time. Continue (FB)
 * resumes at the song position (F2, in 16ths) or where the clock stopped. PLAY on the device while
 * following waits for the next F8 (a start at the clock's tick, not at its bar); STOP stops.
 *
 * Out (SYNC = OUT): F8 at 24 PPQN, also while stopped (a slave can lock before the start), FA before the
 * first F8 of a start, FC at a stop, on USB. Each F8 is timed when the audio at its position leaves the
 * DMA: block time + its offset in the block + CTL / 2 (the mean lateness of a step, above), so the
 * clock and the sound of the steps leave together; sent by usb_poll (2 kHz) when its time has come.
 * SYNC_DAC_T: the codec's own delay (DAC filter, analog path), added to both: 0, as it is not known
 * (no FM-1 measured). The FM-1 has no MIDI OUT jack known to the firmware (UART1 is RX only): no TRS out. */
#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif
#ifndef SYNC_DAC_US
#define SYNC_DAC_US 0u                                   /* the codec's delay after the I2S DMA (unknown) */
#endif
#define SYNC_DAC_T (SYNC_DAC_US * FM1_TICKS_PER_US)
#define SY_TICK_U (BEAT_U / 24u)                           /* units of a MIDI clock tick */
#define SY_TPS_Q8 ((uint32_t)((uint64_t)FM1_TICKS_PER_US * 256000000u / FS))   /* TIMER4 ticks a sample, Q8 */
#define SY_PMIN (8u * 1000u * FM1_TICKS_PER_US)            /* periods accepted: 8 .. 130 ms (312 .. 19 BPM) */
#define SY_PMAX (130u * 1000u * FM1_TICKS_PER_US)
#define SY_LOST (500u * 1000u * FM1_TICKS_PER_US)          /* no F8 for that long: stop (Melodee) */
enum { SYNC_INT, SYNC_OUT, SYNC_USB, SYNC_TRS };          /* G_SYNC */

static volatile uint32_t sync_out_t;   /* TIMER4 time the block being rendered starts to leave the DMA (audio.c) */
static struct {
    uint32_t t, tf;        /* the filtered time of the last F8 (TIMER4), + its fraction (Q8) */
    uint32_t p;            /* the period, TIMER4 ticks Q8 */
    uint32_t r;            /* units (1 sample at 1 BPM) per TIMER4 tick, Q24: SY_TICK_U / period */
    uint32_t last;         /* arrival of the last F8 */
    uint32_t n, n0;        /* F8s counted; n - n0 = the position (ticks) of the last one */
    uint32_t pos;          /* the position (ticks) the next start / continue begins at */
    uint32_t bpm;          /* the tempo, Q16 */
    uint8_t have;          /* F8s seen: 0, 1, 2 = locked (a period) */
    uint8_t pend;          /* START / CONTINUE received: the next F8 starts */
    uint8_t go, stop;      /* for events_block: start / stop now */
    uint8_t mode;          /* G_SYNC as last seen */
} sy;
static struct { uint32_t ph; uint8_t play; } co;          /* clock out: units into the current tick */

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

static void sync_reset(void)
{
    memset(&sy, 0, sizeof sy);
    sy.mode = (uint8_t)song.g[G_SYNC];
}

/* a clock / transport message of the source followed (events_block, in queue order) */
static void sync_in(uint32_t st, uint32_t d1, uint32_t d2, uint32_t t)
{
    if (st == 0xF8u) {
        uint32_t dt = t - sy.last;
        if (sy.have < 2u) {
            if (sy.have && dt >= SY_PMIN && dt < SY_PMAX) {
                sy_period(dt << 8);
                sy.have = 2;
            } else {
                sy.have = 1;
            }
            sy.t = t;
            sy.tf = 0;
        } else {
            int32_t e;
            sy.tf += sy.p & 255u;                          /* the prediction: one period on */
            sy.t += (sy.p >> 8) + (sy.tf >> 8);
            sy.tf &= 255u;
            e = (int32_t)(t - sy.t);
            if (e > (int32_t)(sy.p >> 9) || -e > (int32_t)(sy.p >> 9)) {   /* off by half a tick: relock */
                if (dt >= SY_PMIN && dt < SY_PMAX)
                    sy_period(dt << 8);
                sy.t = t;
                sy.tf = 0;
            } else {
                sy.t += (uint32_t)((e * 9) >> 5);
                sy_period((uint32_t)((int32_t)sy.p + e * 11));
            }
        }
        sy.last = t;
        sy.n++;
        if (sy.pend) {
            sy.pend = 0;
            sy.n0 = sy.n - sy.pos;                         /* this F8 is at sy.pos */
            sy.go = 1;
        }
    } else if (st == 0xFAu || st == 0xFBu) {
        if (st == 0xFAu)
            sy.pos = 0;
        sy.pend = 1;
        sy.stop = 0;
    } else if (st == 0xFCu) {
        if (song.playing && !sy.pend)
            sy.pos = sy.n - sy.n0 + 1u;                    /* CONTINUE: from the next tick */
        sy.pend = 0;
        sy.go = 0;
        sy.stop = 1;
    } else if (st == 0xF2u && !song.playing) {
        sy.pos = (d1 | d2 << 7) * 6u;                      /* song position: 16ths */
    }
}

/* where the external clock will be when the middle of the block being rendered leaves the DMA */
static void sy_target(uint32_t *beat, int32_t *pos)
{
    uint32_t x = sync_out_t + ((CTL / 2u * SY_TPS_Q8) >> 8) + SYNC_DAC_T;
    uint32_t r = sy.have == 2u ? sy.r : (uint32_t)song.g[G_BPM] * (uint32_t)((1ull << 32) / SY_TPS_Q8);
    uint32_t pt = sy.n - sy.n0, lim = sy.have == 2u ? 3u * (sy.p >> 8) : 3u * SY_PMAX;
    int32_t d = (int32_t)(x - sy.t);
    if (d > (int32_t)lim)
        d = (int32_t)lim;                                  /* a stalled clock: the steps stop */
    *beat = pt / 24u;
    *pos = (int32_t)((pt % 24u) * SY_TICK_U) + (int32_t)(((int64_t)d * r) >> 24);
    while (*pos < 0 && *beat) {
        *pos += (int32_t)BEAT_U;
        --*beat;
    }
    if (*pos < 0)
        *pos = 0;
    while (*pos >= (int32_t)BEAT_U) {
        *pos -= (int32_t)BEAT_U;
        ++*beat;
    }
}

/* the transport under the external clock, once a block after the MIDI input: start / stop, then the
 * clock to its target; returns the units it moved (the block's advance) */
static uint32_t sync_follow(void)
{
    uint32_t beat, moved = 0;
    int32_t pos, db;
    if (sy.stop) {
        sy.stop = 0;
        if (song.playing)
            seq_stop();
    }
    if (sy.go) {
        sy.go = 0;
        seq_start();
        if (song.playing) {
            sy_target(&beat, &pos);
            clk_beat = beat;
            clk_pos = (uint32_t)pos;
        }
        return 0;
    }
    if (!song.playing)
        return 0;
    if ((uint32_t)(SYNC_NOW() - sy.last) > SY_LOST || !sy.have) {
        seq_stop();                                        /* the clock is gone */
        sy.have = 0;
        return 0;
    }
    sy_target(&beat, &pos);
    db = (int32_t)(beat - clk_beat);
    if (db > 1 || db < -1) {                               /* far off (a song position): there at once */
        if (db > 0) {
            clk_beat = beat;
            clk_pos = (uint32_t)pos;
        }
        return 0;
    }
    pos += db * (int32_t)BEAT_U - (int32_t)clk_pos;        /* how far ahead the target is */
    if (pos > 0) {
        moved = (uint32_t)pos;
        clk_pos += moved;
        while (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
    }
    return moved;
}

/* UI PLAY / STOP while following: PLAY starts at the next F8, STOP stops */
static void sync_local(uint32_t req)
{
    if (req == 1u) {
        if (!song.playing) {
            sy.pos = 0;
            sy.pend = 1;
        }
    } else {
        sy.pend = 0;
        sy.go = 0;
        sy.stop = 1;
    }
}

/* clock out (SYNC = OUT), at the end of a block that advances the clock by adv units (n samples at the
 * song tempo): every tick crossed, timed at its audio; FA before a start's first F8, FC at a stop */
static void sync_send(uint32_t adv, uint32_t n)
{
    uint32_t bpm = adv / n, k, base = sync_out_t + ((CTL / 2u * SY_TPS_Q8) >> 8) + SYNC_DAC_T;
    if (song.playing != co.play) {
        co.play = song.playing;
        if (co.play)
            rt_out_push(base - 1u, 0xFAu);
        else
            rt_out_push(base, 0xFCu);
    }
    if (co.play)
        co.ph = clk_pos % SY_TICK_U;                   /* (playing: the ticks of the transport clock) */
    if (!bpm)
        return;
    for (k = co.ph ? SY_TICK_U - co.ph : 0u; k < adv; k += SY_TICK_U)
        rt_out_push(base + ((k * SY_TPS_Q8 / bpm) >> 8), 0xF8u);
    co.ph = (co.ph + adv) % SY_TICK_U;
}
