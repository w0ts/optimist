/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (fm1-x0x, charlesvestal/fm1-x0x b637df3, tests/host/encoder_test.c) */
/* hal/fm1_input.h's quadrature decoder (fm1__frame) on simulated fast turns: encoder 0, a full
 * quadrature cycle per detent. A turn must count every detent whether the matrix scan sees each
 * state for 4 scans, 2 or 1, or skips every other state (a fast flick); a one-scan glitch at rest
 * must count nothing. (The scan runs every 1.1 ms: one scan a state is ~225 detents a second.)
 * Ported from X0X; added for SLOOP-plus: the same at one scan a state on an encoder whose detents
 * are half cycles (the learned 00/11 pair of Melodee's first-click fix), and every encoder. */
#include <stdio.h>
#include <string.h>
#include "../firmware/hal/fm1_input.h"

static int fails;
static unsigned enc;                                 /* the encoder under test */
static void frame(uint32_t st)
{
    const uint8_t *m = FM1_ENC[enc];
    memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
    fm1_in.raw[m[0]] |= (uint8_t)(((st >> 1) & 1u) << m[1]);
    fm1_in.raw[m[2]] |= (uint8_t)((st & 1u) << m[3]);
    fm1__frame();
}
static const uint32_t CW[4] = {1, 3, 2, 0};          /* 0 -> 1 -> 3 -> 2 -> 0: one detent clockwise */
static const uint32_t CCW[4] = {2, 3, 1, 0};

static void rest(uint32_t st, int n)
{
    while (n-- > 0)
        frame(st);
}

static void reset(void)
{
    int i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (i = 0; i < (int)FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
    rest(0, 60);                                     /* at rest on state 0: learned as the detent */
}

static int take(void)
{
    int s = fm1_in.enc_steps[enc];
    fm1_in.enc_steps[enc] = 0;
    return s;
}

/* `det` detents, `per` scans on each state */
static int turn(int det, int per, int dir)
{
    int d, k;
    take();
    for (d = 0; d < det; d++)
        for (k = 0; k < 4; k++)
            rest(dir > 0 ? CW[k] : CCW[k], per);
    rest(0, 60);
    return take();
}

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL enc %u: ", enc); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(void)
{
    int per, got, k, d;
    for (enc = 0; enc < FM1_NENC; enc++) {
        for (per = 4; per >= 1; per /= 2) {
            reset();
            got = turn(10, per, 1);
            CHECK(got == 10, "10 detents clockwise, %d scans a state: %d", per, got);
        }
        /* a flick: it starts like any turn (one detent seen whole), then skips every other state;
         * from a standstill straight into skipping, the direction is unknowable and nothing counts */
        reset();
        for (k = 0; k < 4; k++)
            frame(CW[k]);
        for (d = 0; d < 9; d++)
            for (k = 1; k < 4; k += 2)
                frame(CW[k]);
        rest(0, 60);
        got = take();
        CHECK(got == 10, "a flick (1 detent whole, then 9 with every other state skipped): %d", got);
        reset();
        got = turn(10, 1, -1);
        CHECK(got == -10, "10 detents counter-clockwise, one scan a state: %d", got);
        /* a bounce at rest: one scan of state 1, back to 0 */
        reset();
        frame(1);
        rest(0, 60);
        got = take();
        CHECK(got == 0, "a one-scan glitch at rest counted %d", got);
#if !FELUCCA_KNOB_ONEREST                   /* (one rest state: a detent is a full cycle, by design) */
        /* half-cycle detents (00 and 11 both rest states), learned by resting on each, then a
         * fast turn at one scan a state: one step a half cycle */
        reset();
        frame(1);
        rest(3, 60);
        take();
        for (d = 0; d < 5; d++) {
            frame(2);
            frame(0);
            frame(1);
            frame(3);
        }
        rest(3, 60);
        got = take();
        CHECK(got == 10, "10 half-cycle detents clockwise, one scan a state: %d", got);
#endif
    }
    printf(fails ? "encoder (fast turns): %d FAILED\n" : "encoder (fast turns): ok\n", fails);
    return fails != 0;
}
