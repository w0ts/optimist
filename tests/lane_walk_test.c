/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of core/lane_walk.h: the stop-at-edge stepping of the 16 drum lanes. */
#include <stdio.h>
#include "../firmware/src/core/lane_walk.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fails++; printf("FAIL: %s\n", msg); } } while (0)

int main(void)
{
    lane_walk_t r;
    uint32_t last = 0;
    int32_t lane;
    uint32_t i;

    r = lane_walk(5, 0, 1, 1, 1);
    CHECK(r.what == LW_STAY && r.lane == 5, "no step: stays");
    r = lane_walk(5, 1, 0, 1, 1);
    CHECK(r.what == LW_MOVED && r.lane == 6, "one detent up");
    r = lane_walk(5, -2, 0, 1, 1);
    CHECK(r.what == LW_MOVED && r.lane == 3, "two detents down");
    r = lane_walk(1, -1, 0, 1, 1);
    CHECK(r.what == LW_MOVED && r.lane == 0, "reaching the kick is a plain move");

    /* walking up from the lanes: the turn reaches the kick and stops there, however far it goes */
    r = lane_walk(3, -9, 1, 1, 1);
    CHECK(r.what == LW_EDGE && r.lane == 0, "a fast turn past the kick stops at the kick");
    r = lane_walk(0, -1, 0, 1, 1);
    CHECK(r.what == LW_EDGE && r.lane == 0, "the same turn going on: absorbed, no leave");
    r = lane_walk(0, -1, 1, 1, 1);
    CHECK(r.what == LW_LEAVE_LO && r.lane == 0, "a fresh turn after the stop: leaves to the track above");
    r = lane_walk(0, -1, 1, 0, 1);
    CHECK(r.what == LW_EDGE && r.lane == 0, "no neighbour above: stays");
    r = lane_walk(0, 1, 1, 1, 1);
    CHECK(r.what == LW_MOVED && r.lane == 1, "a fresh turn the other way just walks");

    /* the last lane */
    r = lane_walk(14, 5, 1, 1, 1);
    CHECK(r.what == LW_EDGE && r.lane == 15, "past the last lane: stops");
    r = lane_walk(15, 1, 1, 1, 0);
    CHECK(r.what == LW_EDGE && r.lane == 15, "last lane, nothing below (mixer): stays");
    r = lane_walk(15, 1, 1, 1, 1);
    CHECK(r.what == LW_LEAVE_HI && r.lane == 15, "last lane, a neighbour below, fresh turn: leaves");

    /* out-of-range input is clamped */
    r = lane_walk(40, -1, 0, 1, 1);
    CHECK(r.lane == 14, "a lane past 15 is clamped first");
    r = lane_walk(-3, 1, 0, 1, 1);
    CHECK(r.lane == 1, "a lane below 0 is clamped first");

    /* lw_fresh: the first detent is fresh, a continuing turn is not, a pause makes the next fresh */
    CHECK(lw_fresh(&last, 1000, LW_GAP_MS) == 1, "first detent is fresh");
    CHECK(lw_fresh(&last, 1100, LW_GAP_MS) == 0, "100 ms later: same turn");
    CHECK(lw_fresh(&last, 1400, LW_GAP_MS) == 0, "300 ms later: same turn");
    CHECK(lw_fresh(&last, 1400 + LW_GAP_MS + 1u, LW_GAP_MS) == 1, "after a pause: fresh");
    (void)lw_fresh(&last, 0, LW_GAP_MS);
    CHECK(last != 0, "time 0 is kept non-zero (0 means: no detent yet)");

    /* a whole gesture: a long turn up from lane 4, a pause, another turn */
    last = 0;
    lane = 4;
    for (i = 0; i < 8u; i++) {                       /* eight detents, 80 ms apart */
        uint32_t f = lw_fresh(&last, 5000u + i * 80u, LW_GAP_MS);
        r = lane_walk(lane, -1, f, 1, 1);
        lane = r.lane;
        CHECK(r.what != LW_LEAVE_LO, "one continuous turn never leaves");
    }
    CHECK(lane == 0, "the long turn ends on the kick");
    {
        uint32_t f = lw_fresh(&last, 5000u + 8u * 80u + 500u, LW_GAP_MS);
        r = lane_walk(lane, -1, f, 1, 1);
        CHECK(r.what == LW_LEAVE_LO, "the next turn, after a pause, leaves");
    }
    if (fails)
        return 1;
    printf("lane walk: all checks passed\n");
    return 0;
}
