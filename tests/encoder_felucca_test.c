/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The encoder cases of Felucca 1.6's tests/input_test.c (hugelton/Felucca v1.6 1727537; #63, #126 from v1.1.5
 * 276f72a), unchanged but for the harness: hal/fm1_input.h's knob decoder (fm1__frame) fed by a model of the
 * TIMER5 scan (one column every 100 us, a frame every 11) and of turned, bouncing contacts. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_input.h"

#define TICK_US 100u
static int fails;
static int check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}
static uint32_t rng = 0x12345u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}
static void reset(void)
{
    uint32_t i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
}

/* #126 a turned encoder 0: the toggle times of its A (0) and B (1) contacts; a turn of n detents (dir +1 clockwise:
 * B A B A from the detent state 0, -1: A B A B), per us a detent, its 4 transitions bunched over w (1 = even) of each,
 * each edge bouncing (runs of 30..bmax us) for up to bspan us (at most half the way to the next edge) */
#define ENE 8192
static struct { uint32_t t[ENE], n, i, s; } enl[2];
static void en_clear(void) { memset(enl, 0, sizeof enl); }
static void en_add(uint32_t l, uint32_t t) { if (enl[l].n < ENE && (!enl[l].n || t > enl[l].t[enl[l].n - 1u])) enl[l].t[enl[l].n++] = t; }
static uint32_t en_turn(uint32_t t0, int32_t n, int32_t dir, double per, double w, uint32_t bspan, uint32_t bmax)
{
    int32_t i, j;
    uint32_t t = t0;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++) {
            double f = 0.5 - w / 2 + (j + 0.5) * w / 4, nf = j < 3 ? 0.5 - w / 2 + (j + 1.5) * w / 4 : 1.0 + 0.5 - w / 2 + 0.5 * w / 4;
            uint32_t gap = (uint32_t)(per * (nf - f)), l = (uint32_t)((j & 1) ^ (dir > 0)), end;
            t = t0 + (uint32_t)(per * (i + f));
            end = t + (bspan < gap / 2u ? bspan : gap / 2u);
            en_add(l, t);
            while (bspan && bmax) {          /* bounce: back and forth in pairs, the new level last */
                uint32_t a = t + 30u + rnd(bmax), b = a + 30u + rnd(bmax);
                if (b >= end)
                    break;
                en_add(l, a);
                en_add(l, b);
                t = b;
            }
        }
    return t0 + (uint32_t)(per * n);
}
static uint32_t en_at(uint32_t l, uint32_t us)     /* (us only rises between en_clear and the next) */
{
    while (enl[l].i < enl[l].n && enl[l].t[enl[l].i] <= us) {
        enl[l].i++;
        enl[l].s ^= 1u;
    }
    return enl[l].s;
}
/* the TIMER5 scan of encoder 0 (A on its column, B on its own) up to `until` us; the steps it counted */
static int32_t en_run(uint32_t until)
{
    const uint8_t *m = FM1_ENC[0];
    uint32_t us, col = 0;
    for (us = 0; us < until; us += TICK_US) {
        fm1_in.raw[col] = 0;
        if (col == m[0])
            fm1_in.raw[col] |= (uint8_t)(en_at(0, us) << m[1]);
        if (col == m[2])
            fm1_in.raw[col] |= (uint8_t)(en_at(1, us) << m[3]);
        col = col + 1u == FM1_NCOL ? 0u : col + 1u;
        if (!col)
            fm1__frame();
    }
    return fm1_in.enc_steps[0];
}

int main(void)
{
    {   /* one clockwise detent cycle of encoder 0 = one step */
        static const uint8_t SEQ[] = {0, 1, 3, 2, 0};   /* quadrature states A<<1|B, from the rest */
        uint32_t i, f;
        const uint8_t *m = FM1_ENC[0];
        int32_t s;
        reset();
        for (i = 0; i < 5u; i++)
            for (f = 0; f < 4u; f++) {
                memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
                fm1_in.raw[m[0]] |= (uint8_t)(((SEQ[i] >> 1) & 1u) << m[1]);
                fm1_in.raw[m[2]] |= (uint8_t)((SEQ[i] & 1u) << m[3]);
                fm1__frame();
            }
        s = fm1_in.enc_steps[0];
        check("an encoder detent cycle is one step", s == 1 || s == -1);
    }
    {   /* #63: a knob left alone on its detent, one contact chattering (runs of 1..5 frames), or both contacts
         * lost together (the row they share disturbed, 1..5 frames): ~4 min each of every rest state, no step */
        uint32_t rest, mode, f, g, moved = 0;
        const uint8_t *m = FM1_ENC[0];
        for (mode = 0; mode < 3u; mode++)
            for (rest = 0; rest < 4u; rest++) {
                reset();
                for (f = 0, g = 0; f < 200000u; f++) {
                    uint32_t st = rest;
                    if (g) {
                        st ^= mode == 0u ? 2u : mode == 1u ? 1u : 3u;
                        g--;
                    } else if (rnd(300) == 0u) {
                        g = 1u + rnd(5);
                    }
                    if (f < 100u)
                        st = rest;                     /* (the power-on state is the detent) */
                    memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
                    fm1_in.raw[m[0]] |= (uint8_t)(((st >> 1) & 1u) << m[1]);
                    fm1_in.raw[m[2]] |= (uint8_t)((st & 1u) << m[3]);
                    fm1__frame();
                    moved |= fm1_in.enc_steps[0] != 0;
                }
            }
        check("#63 a still knob: one contact chattering or both lost together never steps", !moved);
    }
    {   /* #126 turns at speed through the scan (a frame ~1.1 ms): every detent counted, 12-detent turns over every phase
         * of the scan; slow turns with bouncy contacts; fast ones each way; a reversal */
        static const double EVEN[] = {50, 100, 150, 200, 250, 300}, BUNCH[] = {50, 100, 150};
        static const double SLOW[] = {3, 8, 20, 40};
        uint32_t r, ph, d, bad = 0, n = 0, worst = 0;
        char what[112];
        for (r = 0; r < 2u * 6u; r++)
            for (ph = 0; ph < 40u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1, s;
                    double rate = r < 6u ? EVEN[r] : BUNCH[(r - 6u) % 3u];
                    if (r >= 6u + 3u)
                        continue;
                    reset();
                    en_clear();
                    s = en_run(en_turn(5000u + ph * 53u, 12, dir, 1e6 / rate, r < 6u ? 1.0 : 0.5, 0u, 0u) + 20000u);
                    n++;
                    if (s != 12 * dir) {
                        bad++;
                        if ((uint32_t)(s * dir < 12 ? 12 - s * dir : s * dir - 12) > worst)
                            worst = (uint32_t)(s * dir < 12 ? 12 - s * dir : s * dir - 12);
                    }
                }
        printf("encoder at speed: %u turns of 12 detents, %u miscounted (worst by %u)\n", (unsigned)n, (unsigned)bad, (unsigned)worst);
        check("#126 12-detent turns up to 300 detents/s (even) / 150/s (bunched in half a click): every detent", !bad);
        for (bad = 0, r = 0; r < 4u; r++)
            for (ph = 0; ph < 20u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1;
                    reset();
                    en_clear();
                    bad += en_run(en_turn(5000u + ph * 71u, 6, dir, 1e6 / SLOW[r], ph & 1u ? 1.0 : 0.4, 1500u, 400u) + 20000u) != 6 * dir;
                }
        snprintf(what, sizeof what, "#126 slow turns (3..40 detents/s), contacts bouncing 1.5 ms: one step a click (%u off)", (unsigned)bad);
        check(what, !bad);
        for (bad = 0, ph = 0; ph < 40u; ph++) {
            uint32_t t;
            int32_t s1;
            reset();
            en_clear();
            t = en_turn(5000u + ph * 37u, 8, 1, 1e6 / 120.0, 0.7, 200u, 60u);   /* 8 right, 6 left at once */
            s1 = en_run(t + 2000u);
            t = en_turn(t + 2000u, 6, -1, 1e6 / 120.0, 0.7, 200u, 60u);
            reset();                                    /* (replayed from the start: the same scan) */
            enl[0].i = enl[1].i = enl[0].s = enl[1].s = 0;
            bad += s1 != 8 || en_run(t + 20000u) != 2;
        }
        check("#126 120 detents/s, bouncing 0.2 ms: 8 right then 6 left at once -> 8, then 2", !bad);
    }
    printf("%s\n", fails ? "ENCODER (Felucca 1.6 cases) FAILED" : "encoder (Felucca 1.6 cases) passed");
    return fails != 0;
}
