/* SPDX-License-Identifier: GPL-3.0-only */
/* LANE WALK: a knob walks the 16 drum lanes with the stop-at-edge trick. Turning towards the first lane (kick) stops AT
 * the kick, however many detents the turn has left; only a FRESH turn after that stop (a pause since the last detent)
 * leaves the lanes for the track above. Pure, no state of its own beyond the caller's two ints: both the drum step screen
 * (ui_drumstep.c) and the mixer walk can use it. Included by the UI and by tests/lane_walk_test.c.
 *
 *   lane_walk(lane, step, fresh, ...)  step > 0 goes to higher lanes (towards 15), step < 0 towards the kick.
 *   lw_fresh(&last_ms, now, gap)       1 when this detent starts a new turn (nothing for more than gap ms), and the
 *                                      time is kept, so detents of one turn never count as fresh. */
#ifndef FELUCCA_LANE_WALK_H
#define FELUCCA_LANE_WALK_H
#include <stdint.h>

#define LW_NLANES 16
#define LW_GAP_MS 350u                 /* a pause longer than this between detents: the next turn is a fresh one */

enum { LW_STAY = 0, LW_MOVED = 1, LW_EDGE = 2, LW_LEAVE_LO = 3, LW_LEAVE_HI = 4 };

typedef struct {
    int32_t lane;                      /* the lane after the step, 0..15 */
    uint8_t what;                      /* LW_STAY (no step), LW_MOVED, LW_EDGE (reached or stuck at an edge: stopped),
                                        * LW_LEAVE_LO / LW_LEAVE_HI (a fresh turn pushed past an edge that may be left) */
} lane_walk_t;

/* lane: the lane now; step: detents of this frame (signed); fresh: this turn began after a pause; leave_lo / leave_hi:
 * may the walk cross the first / last lane to the neighbour (mixer: the last lane has none, so leave_hi = 0) */
static inline lane_walk_t lane_walk(int32_t lane, int32_t step, uint32_t fresh, uint32_t leave_lo, uint32_t leave_hi)
{
    lane_walk_t r;
    int32_t n;
    if (lane < 0)
        lane = 0;
    if (lane > LW_NLANES - 1)
        lane = LW_NLANES - 1;
    r.lane = lane;
    r.what = LW_STAY;
    if (step == 0)
        return r;
    n = lane + step;
    if (n < 0 || n > LW_NLANES - 1) {
        uint32_t lo = n < 0, can = lo ? leave_lo : leave_hi;
        r.lane = lo ? 0 : LW_NLANES - 1;
        if (lane == r.lane && fresh && can) {      /* already stopped at the edge, and this is a new turn: cross */
            r.what = lo ? LW_LEAVE_LO : LW_LEAVE_HI;
            return r;
        }
        r.what = LW_EDGE;                          /* reached it this turn (or no neighbour): stop */
        return r;
    }
    r.lane = n;
    r.what = LW_MOVED;
    return r;
}

/* 1 when the detent at `now` starts a new turn; *last is updated on EVERY detent, so a turn that keeps going is never
 * fresh, however far it runs past the edge. The very first detent (*last = 0) is fresh. */
static inline uint32_t lw_fresh(uint32_t *last, uint32_t now, uint32_t gap)
{
    uint32_t f = *last == 0u || now - *last > gap;
    *last = now ? now : 1u;
    return f;
}
#endif
