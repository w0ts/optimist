/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's step extras (firmware/src/stepx.h): the layout of 2.4's FUN5 track tail, the helpers (fill bits, locks,
 * a step cleared), the stored form round trip (empty, random, full) and its refusals. Run by tests/run_tests.sh. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/seq/stepx.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t rnd = 12345u;
static uint32_t rn(uint32_t m) { rnd = rnd * 1103515245u + 12345u; return (rnd >> 16) % m; }

static void random_x(stepx_t *x, uint32_t dens)
{
    uint32_t i;
    stepx_clear(x);
    for (i = 0; i < SX_NSTEP; i++) {
        if (rn(100) < dens)
            x->micro[i] = (int8_t)((int)rn(64) - 32);
        if (rn(100) < dens)
            stepx_fill_set(x, i, rn(3));
    }
    for (i = 0; i < NLOCK; i++)
        if (rn(100) < dens)
            (void)stepx_lock_set(x, rn(64), rn(80), (int16_t)((int)rn(65536) - 32768));
}

int main(void)
{
    static stepx_t x[4], y[4];
    static uint8_t buf[4 * STEPX_ENC_TRK_MAX];
    uint32_t k, n, t, ok;
    const uint8_t *a;
    /* 2.4's FUN5 track (proj_trk_t, 940 B): p[61], engine, preset, step[64] (10 B), then this tail at +764 */
    check("layout: 176 B, micro +0, lock +64 (4 B each), fill +160 (2.4's FUN5 tail +764 / +828 / +924)",
          sizeof(stepx_t) == 176u && 764u + __builtin_offsetof(stepx_t, lock) == 828u &&
              764u + __builtin_offsetof(stepx_t, fill) == 924u && 764u + sizeof(stepx_t) == 940u);
    stepx_clear(&x[0]);
    ok = stepx_is_empty(&x[0]) && x[0].lock[23].step == LOCK_FREE;
    stepx_fill_set(&x[0], 5, FC_NOFILL);
    ok &= x[0].fill[1] == (FC_NOFILL << 2) && stepx_fill(&x[0], 5) == FC_NOFILL && !stepx_is_empty(&x[0]);
    x[0].fill[2] = 3u << 6;                                     /* step 11 = 3: reads as NORM (2.4) */
    ok &= stepx_fill(&x[0], 11) == FC_NORM;
    check("fill: step i in byte i / 4, bits 2 (i % 4); 3 reads as NORM", ok);
    stepx_clear(&x[0]);
    for (k = 0, ok = 1; k < NLOCK; k++)
        ok &= stepx_lock_set(&x[0], k % 3u, k, (int16_t)k) == (int)k;
    ok &= stepx_lock_set(&x[0], 9, 99, 1) == -1 && stepx_lock_set(&x[0], 1, 4, -7) == 4 && x[0].lock[4].val == -7;
    x[0].micro[1] = -5;
    stepx_fill_set(&x[0], 1, FC_FILL);
    stepx_step_clear(&x[0], 1);
    for (k = 0; k < NLOCK; k++)
        ok &= x[0].lock[k].step == (k % 3u == 1u ? LOCK_FREE : k % 3u);
    ok &= !x[0].micro[1] && stepx_fill(&x[0], 1) == FC_NORM;
    check("locks: 24 a track, one per (step, param), full refuses; a step cleared (OCT-): nudge, locks, fill", ok);
    for (t = 0; t < 4u; t++)
        stepx_clear(&x[t]);
    for (t = 0, n = 0; t < 4u; t++)
        n += stepx_encode_trk(&x[t], buf + n);
    check("stored form: four empty tracks are 12 bytes (3 zero counts each)", n == 12u);
    for (k = 0, ok = 1; k < 300u; k++) {
        uint32_t dens = k % 3u == 0 ? 100u : k % 3u == 1 ? 10u : 50u;
        for (t = 0; t < 4u; t++)
            random_x(&x[t], dens);
        for (t = 0, n = 0; t < 4u; t++)
            n += stepx_encode_trk(&x[t], buf + n);
        ok &= n <= 4u * STEPX_ENC_TRK_MAX;
        a = buf;
        for (t = 0; t < 4u; t++) {
            stepx_t w = x[t];
            uint32_t j, i;
            ok &= stepx_decode_trk(&y[t], &a, buf + n);
            /* the same, but locks may come back in other slots (packed): compare as sets */
            ok &= !memcmp(w.micro, y[t].micro, sizeof w.micro) && !memcmp(w.fill, y[t].fill, sizeof w.fill);
            for (i = 0; i < NLOCK; i++)
                if (stepx_lock_used(&w.lock[i])) {
                    j = (uint32_t)stepx_lock_find(&y[t], w.lock[i].step, w.lock[i].param);
                    ok &= j < NLOCK && y[t].lock[j].val == w.lock[i].val;
                }
        }
        ok &= a == buf + n;
    }
    check("stored form: 300 random sets of four tracks (10 %, 50 %, full) round trip exactly", ok);
    a = buf;
    ok = !stepx_decode_trk(&y[0], &a, buf + 1);                 /* cut short */
    buf[0] = 1, buf[1] = 70, buf[2] = 1, buf[3] = 0, buf[4] = 0;    /* a step past 63 */
    a = buf;
    ok &= !stepx_decode_trk(&y[0], &a, buf + 5) && stepx_is_empty(&y[0]);
    buf[1] = 3, buf[2] = 40;                                    /* a nudge past MICRO_MAX */
    a = buf;
    ok &= !stepx_decode_trk(&y[0], &a, buf + 5);
    check("stored form refused (the track left empty): cut short, a step past 63, a nudge past 31", ok);
    printf("stepx test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
