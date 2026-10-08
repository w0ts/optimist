/* SPDX-License-Identifier: GPL-3.0-only */
/* firmware/src/knob_accel.h: knob acceleration by turn speed (after X0X 61654ba, Charles Vestal).
 * A slow turn is one step a detent; quicker detents 2 / 3 / 5 / 8 steps; ranges under 100 stop at
 * 3; ranges over 150 (the tempo) double when quick; small ranges (<= 24) and lists never
 * accelerate; several detents in one read are timed per detent. */
#include <stdio.h>
#include <stdint.h>
#include "../firmware/src/ui/knob_accel.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    static const struct { uint32_t dt; int32_t s, range, want; } T[] = {
        {500, 1, 127, 1},     /* slow: exact */
        {100, 1, 127, 1},
        {79, 1, 127, 2},
        {44, 1, 127, 3},
        {24, 1, 127, 5},
        {11, 1, 127, 8},
        {11, -1, 127, -8},    /* counter-clockwise the same */
        {11, 1, 99, 3},       /* range < 100: at most 3 */
        {11, 1, 200, 16},     /* the tempo: doubled when quick */
        {79, 1, 200, 4},
        {500, 1, 200, 1},     /* slow tempo: still one BPM a detent */
        {11, 1, 24, 1},       /* small ranges never accelerate */
        {11, 1, 0, 1},        /* a list (range 0): exact */
        {11, 3, 0, 3},
        {60, 3, 127, 15},     /* 3 detents in one read 60 ms after the last: 20 ms each -> 5 a detent */
        {600, 3, 127, 3},     /* 3 detents in 600 ms: 200 ms each, exact */
        {0, 0, 127, 0},       /* no detent: nothing */
        {0, 2, 127, 16},      /* a burst read at once (dt 0): the fastest */
    };
    unsigned i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        int32_t got = knob_accel(T[i].dt, T[i].s, T[i].range);
        CHECK(got == T[i].want, "dt %u ms, %d detents, range %d: %d, expected %d", (unsigned)T[i].dt, (int)T[i].s,
              (int)T[i].range, (int)got, (int)T[i].want);
    }
    printf(fails ? "knob acceleration: %d FAILED\n" : "knob acceleration: ok\n", fails);
    return fails != 0;
}
