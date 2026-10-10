/* SPDX-License-Identifier: GPL-3.0-only */
/* firmware/src/ui/sloop/knob_gate.h: how far a knob must move while a layer button is held before it counts.
 * One detent of jitter is nothing (no layer used, no tap lost); two net detents from the press, either way, are a turn;
 * +1 then -1 is none. Exit status: the number of failed checks. */
#include <stdio.h>
#include <stdint.h>
#include "../firmware/src/ui/sloop/knob_gate.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    int32_t pos = 0, r;
    int i;
    /* one detent either way: held back */
    r = knob_gate(&pos, 1);
    CHECK(r == 0 && pos == 1, "+1: held back (pos %d)", pos);
    pos = 0;
    r = knob_gate(&pos, -1);
    CHECK(r == 0 && pos == -1, "-1: held back (pos %d)", pos);
    /* jitter: +1 -1 +1 -1 ... never reaches the gate */
    pos = 0;
    for (i = 0; i < 40; i++)
        CHECK(knob_gate(&pos, i & 1 ? -1 : 1) == 0, "jitter %d passed", i);
    CHECK(pos == 0, "jitter: back at the press point (pos %d)", pos);
    /* two the same way: through, with the movement, and the position clear */
    pos = 0;
    knob_gate(&pos, 1);
    r = knob_gate(&pos, 1);
    CHECK(r == 2 && pos == 0, "+1 +1: passes as 2 (r %d pos %d)", r, pos);
    pos = 0;
    knob_gate(&pos, -1);
    r = knob_gate(&pos, -1);
    CHECK(r == -2 && pos == 0, "-1 -1: passes as -2 (r %d pos %d)", r, pos);
    /* two in one read */
    pos = 0;
    r = knob_gate(&pos, 2);
    CHECK(r == 2, "+2 at once: passes (r %d)", r);
    r = knob_gate(&pos, -3);
    CHECK(r == -3, "-3 at once: passes (r %d)", r);
    /* movement counts from the press point, not detent by detent: +1 +1 -1 is one net */
    pos = 0;
    knob_gate(&pos, 1);
    r = knob_gate(&pos, -1);
    CHECK(r == 0 && pos == 0, "+1 -1: nothing (pos %d)", pos);
    knob_gate(&pos, -1);
    r = knob_gate(&pos, -1);
    CHECK(r == -2, "then -1 -1: two net the other way (r %d)", r);
    /* a drift of one, then a second the same way later: still a turn (net from the press) */
    pos = 0;
    knob_gate(&pos, 1);
    knob_gate(&pos, 0);
    r = knob_gate(&pos, 1);
    CHECK(r == 2, "+1, a pause, +1: a turn (r %d)", r);
    /* a runaway read stays bounded */
    pos = 0;
    r = knob_gate(&pos, 100000);
    CHECK(r == 1000, "a runaway read is bounded (r %d)", r);
    /* the release path asks without taking */
    CHECK(!knob_gate_met(0, 1) && !knob_gate_met(0, -1), "met: one detent no");
    CHECK(knob_gate_met(0, 2) && knob_gate_met(0, -2), "met: two detents yes, either way");
    CHECK(knob_gate_met(1, 1) && knob_gate_met(-1, -1), "met: one held back + one now is two");
    CHECK(!knob_gate_met(1, -1) && !knob_gate_met(-1, 1), "met: +1 then -1 no");
    CHECK(KNOB_GATE_DETENTS == 2, "the gate is two detents");
    printf("knob_gate: %d failed\n", fails);
    return fails;
}
