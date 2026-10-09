/* SPDX-License-Identifier: GPL-3.0-only */
/* The stop-at-edge walk (firmware/src/ui/sloop/edge_walk.c): the mixer's ALGORITHM over T1 T2 T3 DR | 16 lanes.
 * A turn stops at the edge, a fresh turn crosses it, the ends stop, a slow walk crosses where it pauses.
 * Run by tests/run_tests.sh. */
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/ui/sloop/edge_walk.c"

#define N 20u                                   /* 4 tracks, 16 lanes */
#define EDGE 4u                                 /* the first lane */
static int bad;
static void check(const char *what, int ok)
{
    printf("edge walk: %-80s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* one turn of k detents dt ms apart from row r, starting at time *t (a pause before it: fresh) -> the row */
static uint32_t turn(uint32_t r, int32_t dir, uint32_t k, uint32_t dt, uint32_t *t, uint32_t *last)
{
    uint32_t i;
    *t += 1000u;
    for (i = 0; i < k; i++, *t += dt)
        r = edge_walk(r, dir, N, EDGE, edge_fresh(last, *t));
    return r;
}
int main(void)
{
    uint32_t t = 5000u, last = 0, r;
    check("one detent down: T1 -> T2", edge_walk(0, 1, N, EDGE, 1) == 1u);
    check("the top stops: T1 up stays T1", edge_walk(0, -1, N, EDGE, 1) == 0u);
    check("the bottom stops: lane 16 down stays", edge_walk(N - 1u, 1, N, EDGE, 1) == N - 1u);
    check("a fresh detent crosses: DR -> lane 1", edge_walk(EDGE - 1u, 1, N, EDGE, 1) == EDGE);
    check("... and back: lane 1 -> DR", edge_walk(EDGE, -1, N, EDGE, 1) == EDGE - 1u);
    check("the same turn stops: DR stays DR going down", edge_walk(EDGE - 1u, 1, N, EDGE, 0) == EDGE - 1u);
    check("... lane 1 stays lane 1 going up", edge_walk(EDGE, -1, N, EDGE, 0) == EDGE);
    check("away from the edge the same turn walks on: lane 1 -> lane 2", edge_walk(EDGE, 1, N, EDGE, 0) == EDGE + 1u);
    check("no edge (0): T3 -> DR -> lane 1 in one turn", edge_walk(edge_walk(2, 1, N, 0, 0), 1, N, 0, 0) == EDGE);
    r = turn(12u, -1, 12, 40u, &t, &last);
    check("a fast spin up from lane 9: stops on lane 1 (kick)", r == EDGE);
    r = turn(r, -1, 1, 40u, &t, &last);
    check("... a fresh turn after the stop: crosses to DR", r == EDGE - 1u);
    r = turn(0u, 1, 10, 50u, &t, &last);
    check("a fast spin down from T1: stops on DR", r == EDGE - 1u);
    r = turn(r, 1, 3, 50u, &t, &last);
    check("... a fresh turn: into the lanes, lane 1 then on (lane 3)", r == EDGE + 2u);
    r = turn(EDGE + 1u, -1, 1, 0u, &t, &last);
    r = turn(r, -1, 1, 0u, &t, &last);
    check("a slow walk (a pause before each detent): lane 2 -> lane 1 -> DR", r == EDGE - 1u);
    last = 1000u;
    check("edge_fresh: a detent 349 ms after the last is the same turn", !edge_fresh(&last, 1349u));
    check("... 350 ms after it, a new one", edge_fresh(&last, 1699u));
    check("list: a row in view keeps the top", list_top(0, 3, N, 4) == 0u && list_top(5, 7, N, 4) == 5u);
    check("list: down off the bottom slides one row (DR -> lane 1: top T2)", list_top(0, 4, N, 4) == 1u);
    check("list: up off the top slides one row", list_top(5, 4, N, 4) == 4u);
    check("list: a jump far away (the track picked elsewhere): that row at the top", list_top(12, 0, N, 4) == 0u);
    check("list: never past the end (the last four)", list_top(19, 19, N, 4) == 16u);
    printf(bad ? "edge walk: %d FAILED\n" : "edge walk: all ok\n", bad);
    return bad != 0;
}
