/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's note tap (FELUCCA_LAUNCHPAD): what the sequencer, the arp and the rolls play, as MIDI OUT = SEQ would
 * send it, queued for the pads' lights (io/launchpad/lp_unit.c lu_played). Included by seq_midi.c: the tap
 * sits at seq_out_on, the one place every played note passes, whatever MIDI OUT is set to.
 *
 * One writer (the audio interrupt: lv_tap) and one reader (the main loop: lv_tap_get), no interrupt masked: the head is
 * written only here, the tail only by the reader. Nothing is queued while the push is off (lv_tap_on 0): then the tap is
 * one load and one test a note. The ring holds the track, its playing step and the note as sent (a drum hit: the lane's
 * MIDI note; the reader turns it into the lane). */
#ifndef LIVE_TAP_H
#define LIVE_TAP_H
#ifndef FELUCCA_LAUNCHPAD
#define FELUCCA_LAUNCHPAD 0
#endif
#if FELUCCA_LAUNCHPAD
#define LV_RING 32u                          /* notes queued between two main-loop passes (a power of two) */
#define LV_NO_STEP 255u                      /* a note played with the transport stopped (the arp, a roll) */
typedef struct {
    uint8_t trk, step, note, vel;
} lv_ev_t;
static struct {
    lv_ev_t ev[LV_RING];
    volatile uint32_t head, tail;            /* head: the interrupt's; tail: the main loop's */
    volatile uint32_t made, lost;            /* notes queued, notes lost (the ring was full) */
    volatile uint8_t on;                     /* the push is on: queue the notes */
} lv_tap_q;
static inline __attribute__((always_inline)) void lv_tap(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t h;
    if (!lv_tap_q.on)
        return;
    h = lv_tap_q.head;
    if (h - lv_tap_q.tail >= LV_RING) {
        lv_tap_q.lost++;
        return;
    }
    lv_tap_q.ev[h & (LV_RING - 1u)] = (lv_ev_t){(uint8_t)trk_index(t), song.playing ? (uint8_t)t->seq_idx : (uint8_t)LV_NO_STEP,
                                               (uint8_t)(note & 127u), (uint8_t)(vel ? vel & 127u : 1u)};
    lv_tap_q.made++;
    lv_tap_q.head = h + 1u;                  /* (written last: the reader sees a whole event) */
}
static int lv_tap_get(lv_ev_t *e)            /* main loop: the oldest note queued; 0 when none */
{
    uint32_t tl = lv_tap_q.tail;
    if (tl == lv_tap_q.head)
        return 0;
    *e = lv_tap_q.ev[tl & (LV_RING - 1u)];
    lv_tap_q.tail = tl + 1u;
    return 1;
}
static uint32_t lv_tap_drop(void)            /* main loop: every note queued dropped (the push turned off); how many */
{
    uint32_t n = lv_tap_q.head - lv_tap_q.tail;
    lv_tap_q.tail = lv_tap_q.head;
    return n;
}
#define LV_TAP(t, n, v) lv_tap((t), (n), (v))
#else
#define LV_TAP(t, n, v) ((void)0)
#endif
#endif
