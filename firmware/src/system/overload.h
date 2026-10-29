/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio overload monitor (the user, 2026-10-15: "we should have actually a MAX of the last few seconds and a DROP-OUT
 * DETECTOR", "the second encoder should switch between CURRENT, MAX OVER A FEW SECONDS, MAX FOR EVER. And we should also monitor
 * UNDERRUNS"; 2026-10-28: "progress bars with 1-minute and 10-second peaks"): SYSTEM > INFO's CPU lines (a bar of the load
 * now, a bright tick at the longest audio half of the last ~10 s, a dim one at the longest since boot; the number by KNOB 1: NOW,
 * MAX 10S, MAX since boot; % of a half, over 100 % it ran late), DROPS (late halves since boot: the DMA moved on while the half rendered, a gap in the sound),
 * UNDERRUNS (what else was lost: USB audio's under / overruns and missed frames, MIDI in that found its ring full), and the
 * header's mark for ~1 s after one (op_draw.c), on every page: red for a drop, amber for another underrun.
 *
 * The audio ISR (core/audio.c) calls ovl_half once a half with what it measures already (its time, its wait for the interrupt,
 * late or not): a compare and a store or two; once a slot (1.024 s of fm1_ms) the new slot's maximum starts from 0. CPU1's
 * time (dual.c dual_load_half) goes into the second ring the same way. The MIDI producers (TIMER5, the BLE interrupt) count what
 * they could not queue, one counter each. The UI only reads (and sets the display's baselines). The host tests call these
 * themselves. */
#ifndef OVERLOAD_H
#define OVERLOAD_H
#define OVL_SLOTS 16u                    /* a ring's per-slot maxima (a power of 2, more than the longest window: 10 s; was 64 for the 1 min one) */
#define OVL_WIN 5u                       /* the reset-kept record's ~5 s (audio.c felucca_dbg.pk_5s): the slot being filled, 4 before */
#define OVL_W10 10u                      /* MAX 10S, the bright tick: the slot being filled and the 9 before it (~10 s) */
#define OVL_MARK_MS 1000u                /* the header's mark: this long after a drop / an underrun */
#define OVL_HALF_US (HALF_FRAMES * 1000000u / FS)   /* one audio half, us (= audio.c's) */
enum { OVL_CPU0, OVL_CPU1, OVL_RAM, OVL_NRING };     /* (OVL_RAM: the arena's blocks in use, KB, sampled with the CPU's:
                                                         * audio.c; SYSTEM > INFO's RAM bar) */
enum { OVL_NOW, OVL_MAX10, OVL_MAXALL, OVL_NMODE };   /* the CPU lines' number (INFO's KNOB 1) */
enum { OVL_MIDI_USB, OVL_MIDI_TRS, OVL_MIDI_BLE, OVL_NMIDI };
typedef struct {
    uint16_t smax[OVL_SLOTS];            /* the longest half in each slot, us (65535 at most) */
    uint16_t amax;                       /* the longest since boot or the last clear, us */
    uint32_t slot;                       /* the slot being filled: fm1_ms >> 10 */
} ovl_ring_t;
typedef struct {
    ovl_ring_t r[OVL_NRING];             /* CPU0's audio halves (its wait for the interrupt in), CPU1's jobs, the arena's KB */
    uint32_t drops;                      /* late halves since boot */
    uint32_t drops0;                     /* DROPS' baseline: SHIFT + YES on INFO sets it to drops (the display only) */
    uint32_t drop_ms;                    /* fm1_ms at the last late half */
    uint32_t midi_lost[OVL_NMIDI];       /* MIDI in lost: its ring full (USB, TRS: TIMER5; BLE: its interrupt), since boot */
    uint32_t xrun0;                      /* UNDERRUNS' baseline (ovl_xruns at the last clear) */
    uint32_t xrun_seen, xrun_ms;         /* (the UI) the total last seen, and when it last grew */
} ovl_t;
static volatile ovl_t ovl;
#define OVL_LOST(k) (ovl.midi_lost[k]++)  /* MIDI in lost from source k (the producers: usb.c, midi_uart.c, midi_ble.c) */

/* a time of us into ring r at fm1_ms ms (an interrupt: a compare, a store; once a slot the new one cleared) */
static inline void ovl_ring_put(volatile ovl_ring_t *r, uint32_t us, uint32_t ms)
{
    uint32_t s = ms >> 10, k = s & (OVL_SLOTS - 1u);
    if (s != r->slot) {                  /* a new slot (once a second): it and any skipped since start from 0 */
        uint32_t j = r->slot, n = 0;
        while (j != s && n++ < OVL_SLOTS)
            r->smax[++j & (OVL_SLOTS - 1u)] = 0;
        r->slot = s;
    }
    us = us < 0xFFFFu ? us : 0xFFFFu;
    if (us > r->smax[k])
        r->smax[k] = (uint16_t)us;
    if (us > r->amax)
        r->amax = (uint16_t)us;
}
/* the audio ISR, a half's end: it took us microseconds after the DMA freed the half; late: the DMA moved on while it rendered */
static inline void ovl_half(uint32_t us, uint32_t late, uint32_t ms)
{
    ovl_ring_put(&ovl.r[OVL_CPU0], us, ms);
    if (late) {
        ovl.drops++;
        ovl.drop_ms = ms;
    }
}

/* the UI: ring r's longest half in % of a half (over 100: late), the last win slots (at most OVL_SLOTS) as of fm1_ms ms
 * (slots the ISR has not reached, the audio stopped, do not count) or since boot / the last clear */
static uint32_t ovl_pct(uint32_t us) { return (us * 100u + OVL_HALF_US / 2u) / OVL_HALF_US; }
/* ring r's largest value (as put: us, the RAM ring's KB) in the last win slots as of ms ms */
static uint32_t ovl_win_max(uint32_t ring, uint32_t ms, uint32_t win)
{
    const volatile ovl_ring_t *r = &ovl.r[ring % OVL_NRING];
    uint32_t i, m = 0, s = ms >> 10, top = r->slot;
    for (i = 0; i < win && i < OVL_SLOTS; i++) {
        uint32_t j = top - i, v = r->smax[j & (OVL_SLOTS - 1u)];
        if (s - j < win && v > m)
            m = v;
    }
    return m;
}
static uint32_t ovl_win_pct(uint32_t ring, uint32_t ms, uint32_t win) { return ovl_pct(ovl_win_max(ring, ms, win)); }
static uint32_t ovl_max5_pct(uint32_t ring, uint32_t ms) { return ovl_win_pct(ring, ms, OVL_WIN); }
static uint32_t ovl_maxall_pct(uint32_t ring) { return ovl_pct(ovl.r[ring % OVL_NRING].amax); }
static uint32_t ovl_drops(void) { return ovl.drops - ovl.drops0; }   /* DROPS as shown (since boot or the last clear) */
static int ovl_recent(uint32_t ms, uint32_t win) { return ovl.drops && ms - ovl.drop_ms < win; }   /* a late half within win ms */
static uint32_t ovl_midi_lost(void) { return ovl.midi_lost[0] + ovl.midi_lost[1] + ovl.midi_lost[2]; }
#endif
