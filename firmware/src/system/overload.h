/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio overload monitor (the user, 2026-10-15: "we should have actually a MAX of the last few seconds and a DROP-OUT
 * DETECTOR"): SYSTEM > INFO's PEAK (the longest audio half of the last ~5 s, % of a half: over 100 % it ran late) and DROPS
 * (late halves since boot: the DMA moved on while the half rendered, a gap in the sound), and the header's red DROP mark for
 * ~1 s after one (op_draw.c), on every page.
 *
 * The audio ISR (core/audio.c) calls ovl_half once a half with what it measures already (its time, late or not): a compare
 * and a store or two; once a slot (1.024 s of fm1_ms) the new slot's maximum starts from 0. The UI only reads. The host tests
 * call ovl_half themselves. */
#ifndef OVERLOAD_H
#define OVERLOAD_H
#define OVL_SLOTS 8u                     /* the ring of per-slot maxima (a power of 2) */
#define OVL_WIN 5u                       /* PEAK: the slot being filled and the 4 before it (~5 s) */
#define OVL_MARK_MS 1000u                /* the header's DROP mark: this long after a late half */
#define OVL_HALF_US (HALF_FRAMES * 1000000u / FS)   /* one audio half, us (= audio.c's) */
typedef struct {
    uint16_t smax[OVL_SLOTS];            /* the longest half in each slot, us (65535 at most) */
    uint32_t slot;                       /* the slot being filled: fm1_ms >> 10 */
    uint32_t drops;                      /* late halves since boot */
    uint32_t drops0;                     /* DROPS' baseline: OCT- + YES on INFO sets it to drops (the display only) */
    uint32_t drop_ms;                    /* fm1_ms at the last late half */
} ovl_t;
static volatile ovl_t ovl;

/* the audio ISR, a half's end: it took us microseconds; late: the DMA moved on while it rendered; ms: fm1_ms */
static inline void ovl_half(uint32_t us, uint32_t late, uint32_t ms)
{
    uint32_t s = ms >> 10, k = s & (OVL_SLOTS - 1u);
    if (s != ovl.slot) {                 /* a new slot (once a second): it and any skipped since start from 0 */
        uint32_t j = ovl.slot, n = 0;
        while (j != s && n++ < OVL_SLOTS)
            ovl.smax[++j & (OVL_SLOTS - 1u)] = 0;
        ovl.slot = s;
    }
    if (us > ovl.smax[k])
        ovl.smax[k] = (uint16_t)(us < 0xFFFFu ? us : 0xFFFFu);
    if (late) {
        ovl.drops++;
        ovl.drop_ms = ms;
    }
}

/* the UI: PEAK, the longest half of the window in % of a half (over 100: late), as of fm1_ms ms (a window the ISR has not
 * reached yet, the audio stopped: its old slots do not count) */
static uint32_t ovl_peak_pct(uint32_t ms)
{
    uint32_t i, m = 0, s = ms >> 10;
    for (i = 0; i < OVL_WIN; i++) {
        uint32_t j = ovl.slot - i;
        if (s - j < OVL_WIN && ovl.smax[j & (OVL_SLOTS - 1u)] > m)
            m = ovl.smax[j & (OVL_SLOTS - 1u)];
    }
    return (m * 100u + OVL_HALF_US / 2u) / OVL_HALF_US;
}
static uint32_t ovl_drops(void) { return ovl.drops - ovl.drops0; }   /* DROPS as shown (since boot or the last clear) */
static void ovl_clear(void) { ovl.drops0 = ovl.drops; }
static int ovl_recent(uint32_t ms, uint32_t win) { return ovl.drops && ms - ovl.drop_ms < win; }   /* a late half within win ms */
#endif
