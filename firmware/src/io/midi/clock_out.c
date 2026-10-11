/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI clock out (FELUCCA_MIDI_CLKOUT; the HOME menu's CLOCK OUT, OFF / ON, a setting of the FM-1: settings word bit
 * 26). Included by seq.c after clock_sync.c (sync_out_t, sy, sy_div); events_block calls clko_block once a
 * block of CTL samples, after the steps, before the transport clock moves on.
 *
 * What is sent: F8 at 24 a beat, always while ON (also stopped, so the other side can lock to the tempo first, as most
 * hardware sequencers do); FA at a start from the top, then the downbeat's F8; F2 (song position, 16ths) + FB at a
 * start from elsewhere (following an external clock's CONTINUE); FC at a stop. On USB (the TRS jack is MIDI IN only:
 * no TX on the FM-1's board), and to clko_ble_send, the one place a BLE sender plugs in (empty here).
 *
 * Time. The ticks are those of the transport clock (core.h: clk_beat, clk_pos; BEAT_U / 24 units a tick), so they
 * follow tempo changes, the TEMPO page's nudge and the arranger, and never swing (swing moves only the odd steps of a
 * grid, seq.c grid_at). Each F8 is due when the audio of its position leaves the I2S DMA: the block's time (audio.c
 * sync_out_t, + SYNC_DAC_T) + its offset in the block; usb.c sends it within 0.05 ms of that. On the internal tempo
 * a step starts at the first block whose start is at or past it, CTL / 2 late on average (0 .. 0.73 ms): the F8s get
 * that mean too, so a step and its F8 leave within +-0.36 ms, the F8s with no jitter of their own. Following an
 * external clock the steps start at their sample (ev_map): the F8s at the same sample, no CTL / 2.
 *
 * Following an external clock (clock_sync.c, sy.src): the F8s go on at the followed tempo, timed as the steps are
 * (a re-timed, de-jittered copy), never back out of the port being followed (no loop through a host that echoes):
 * following USB, nothing goes to USB; following TRS, USB gets it (a TRS -> USB clock bridge). */
#ifndef FELUCCA_MIDI_CLKOUT
#define FELUCCA_MIDI_CLKOUT 0
#endif
#if FELUCCA_MIDI_CLKOUT
#define CO_F8 (0x0Fu | 0xF8u << 8)                        /* USB-MIDI packets: CIN F, one byte */
#define CO_RT(b) (0x0Fu | (uint32_t)(b) << 8)
#define CO_16TH (6u * SY_TICK_U)                          /* a song-position step: 6 ticks */
static uint8_t clko_on;                                   /* HOME menu CLOCK OUT: 0 OFF (the default), 1 ON */
static struct {
    uint32_t ph;           /* units into the current tick at the last block's end (stopped: the free-running clock) */
    uint32_t skip;         /* units from the block's start before an F8 may go (a continue: to its 16th) */
    uint8_t play;          /* the transport as last seen */
    uint8_t live;          /* sending last block */
} co;

/* BLE MIDI clock: the one hook a BLE sender would fill (pkt as USB-MIDI, due a TIMER4 time). Out of scope here: the
 * BLE code (firmware/src/ble/, io/midi/midi_ble.c) has no timed send yet */
static void clko_ble_send(uint32_t due, uint32_t pkt) { (void)due, (void)pkt; }

/* the ports this goes to now: USB unless USB is the clock followed */
static int clko_usb(void)
{
    return !(FELUCCA_MIDI_CLOCK && sy.src == MSRC_USB);
}
/* ON but sending nowhere (following USB): the menu shows it amber */
static int clko_blocked(void) { return clko_on && !clko_usb(); }

static void clko_emit(uint32_t due, uint32_t pkt)
{
    if (clko_usb())
        rt_out_push(due, pkt);
    clko_ble_send(due, pkt);
}

/* a start at beat, pos (the transport's place at the block's start): FA from the top, else F2 + FB at the next 16th
 * (less than a tick past one, a late start: that one). due: when the block's audio leaves; per: the TIMER4 ticks of a
 * clock period (the stopped F8s queued closer than half of it to the downbeat are taken back). Returns where the
 * first F8 is, units from pos (<= 0: late, at the block's start) */
static int32_t clko_start(uint32_t beat, uint32_t pos, uint32_t due, uint32_t per)
{
    uint32_t q = beat * 4u + pos / CO_16TH, r = pos % CO_16TH;
    int32_t k0 = -(int32_t)r;
    if (r >= SY_TICK_U) {
        q++;                                              /* (between 16ths: from the next one) */
        k0 += (int32_t)CO_16TH;
    }
    rt_out_drop_after(due - per / 2u, CO_F8);
    if (!q) {
        clko_emit(due, CO_RT(0xFAu));
    } else {
        q = q < 16383u ? q : 16383u;                      /* (the song position's 14 bits) */
        clko_emit(due, 0x03u | 0xF2u << 8 | (q & 127u) << 16 | (q >> 7) << 24);
        clko_emit(due, CO_RT(0xFBu));
    }
    return k0;
}

/* once a block of n samples: adv the units the transport moved (following: what sync_follow moved, the clock already
 * at the block's end; else the clock still at its start), ext following an external clock. The ticks are where the
 * transport's place is a whole number of ticks (BEAT_U is 24 of them): k0, k0 + a tick .. units from the block's start */
static void clko_block(uint32_t n, uint32_t adv, uint32_t ext)
{
    uint32_t base = sync_out_t + SYNC_DAC_T, span, lead_q8, beat = clk_beat, pos = clk_pos, ph;
    int32_t k, lead = 0;
    if (!clko_on || !n) {
        co.live = 0;
        co.play = song.playing;
        clko_started = 0;
        return;
    }
    if (!song.playing) {
        adv = n * (uint32_t)song.g[G_BPM];                /* stopped: a clock of its own at the tempo shown */
        ph = co.ph;                                       /* (just stopped: on from where the transport was) */
    } else {
        if (ext) {                                        /* following: the clock is at the block's end, back to its start */
            if (adv > pos) {
                beat--;
                pos += BEAT_U;                            /* (adv < BEAT_U: sync_follow moves on by less than a beat) */
            }
            pos -= adv;
            if (ev_map.on && ev_map.adv > 0)
                lead = ev_map.d - (int32_t)adv;           /* the block's first sample is lead units before pos */
        }
        ph = pos % SY_TICK_U;
    }
    span = ext && song.playing && ev_map.on && ev_map.adv > 0 ? (uint32_t)ev_map.adv : adv;
    if (!span)
        span = n * (uint32_t)song.g[G_BPM];               /* (following, the clock waiting: for the timing below) */
    lead_q8 = song.playing && ext ? 0u : (CTL / 2u) << 8; /* (the internal tempo's steps: CTL / 2 late on average) */
    k = ph ? (int32_t)(SY_TICK_U - ph) : 0;
    if (!co.live) {
        co.live = 1;
        co.play = song.playing;                           /* (switched ON: the clock from here, no start or stop) */
    }
    if (song.playing && (clko_started || !co.play)) {
        uint32_t per = (uint32_t)(((uint64_t)sy_div((uint64_t)SY_TICK_U * n << 8, span) * SY_TPS_Q8) >> 16);
        int32_t k0 = clko_start(beat, pos, base, per);
        if (k0 <= 0)
            k = k0;                                       /* (the first F8 just behind: at the block's start) */
        co.skip = k0 > 0 ? (uint32_t)k0 : 0u;             /* (ahead: none before it) */
    } else if (!song.playing && co.play) {
        clko_emit(base, CO_RT(0xFCu));
        co.skip = 0;
    }
    clko_started = 0;
    co.play = song.playing;
    for (; k < (int32_t)adv; k += (int32_t)SY_TICK_U) {
        int32_t d = k + lead;                             /* units after the block's first sample */
        uint32_t q8 = lead_q8 + (d > 0 ? sy_div((uint64_t)(uint32_t)d * n << 8, span) : 0u);
        if (!co.skip || k >= (int32_t)co.skip)
            clko_emit(base + (uint32_t)(((uint64_t)q8 * SY_TPS_Q8) >> 16), CO_F8);
    }
    co.skip = co.skip > adv ? co.skip - adv : 0u;
    co.ph = (ph + adv) % SY_TICK_U;
}
#else
#define clko_block(n, adv, ext) ((void)0)
#endif
