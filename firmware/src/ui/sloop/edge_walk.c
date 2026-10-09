/* SPDX-License-Identifier: GPL-3.0-only */
/* The stop-at-edge walk (the user's ruling for the mixer and the step screen): ALGORITHM walks a list of rows, one a
 * detent, that has an edge inside it (the mixer: T1 T2 T3 DR | the 16 drum lanes). A turn that reaches the row next
 * to the edge stops there; only a fresh turn (the knob at rest EW_GAP_MS or more before the detent) crosses it. So a
 * fast spin up through the lanes stops on the kick, and a fast spin down from T1 stops on DR. The ends of the list
 * stop too (no wrap). Pure: tests/edge_walk_test.c. */
#define EW_GAP_MS 350u                          /* a pause this long ends a turn (the user: 350 ms, as the drum grid) */

/* the row after one detent: rows 0 .. n - 1, the edge between rows edge - 1 and edge (0: none); fresh: this detent
 * starts a new turn */
static uint32_t edge_walk(uint32_t row, int32_t dir, uint32_t n, uint32_t edge, int fresh)
{
    uint32_t to = row;
    if (dir > 0 && row + 1u < n)
        to = row + 1u;
    else if (dir < 0 && row > 0u)
        to = row - 1u;
    if (!fresh && edge && ((row == edge - 1u && to == edge) || (row == edge && to == edge - 1u)))
        return row;                             /* the same turn: it stops at the edge */
    return to;
}
/* a detent at now (ms): does it start a new turn? (*last: the time of the detent before, updated) */
static int edge_fresh(uint32_t *last, uint32_t now)
{
    int fresh = now - *last >= EW_GAP_MS;
    *last = now;
    return fresh;
}
/* a list of n rows showing vis at a time from top: the top that keeps row sel in view, sliding one row at a time
 * (down off the bottom: the next row comes in; up off the top: the one before) */
static uint32_t list_top(uint32_t top, uint32_t sel, uint32_t n, uint32_t vis)
{
    if (sel < top)
        top = sel;
    else if (sel >= top + vis)
        top = sel + 1u - vis;
    if (n >= vis && top > n - vis)
        top = n - vis;
    return top;
}
