/* SPDX-License-Identifier: GPL-3.0-only */
/* hal/fm1_input.h's key debounce, built both ways (tests/run_tests.sh):
 *   FELUCCA_KEYS_FAST=0  integrating: a press after 3 frames closed, counted at the frame's end
 *   FELUCCA_KEYS_FAST=1  SLOOP 2.3 / Felucca 1.0: as each column is read, a press after 2 samples closed in a
 *                        row, a release after 8 open in a row
 * Time in TIMER5 ticks (0.1 ms): the scan reads column c at tick 11 k + c of frame k; the frame's end
 * (fm1__frame) is at tick 11 k + 11. Every key, contact closure at every phase of the scan: the press latency,
 * then a one-sample glitch (no press), bounce on the way down (one press), chatter on release (one release). */
#include <stdio.h>
#include <string.h>
#include "../firmware/hal/fm1_input.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("keys (%s): %-62s %s\n", FELUCCA_KEYS_FAST ? "fast" : "2.2 ", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int key_col(uint32_t id, uint32_t *row)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id) {
                *row = r;
                return (int)c;
            }
    return -1;
}

static uint32_t tick;                                /* 0.1 ms ticks since the start */
static uint8_t level[FM1_NKEY];                      /* the contact now (1 closed) */
static void scan_tick(void)                          /* one column read (as fm1_input_tick), the frame at the end */
{
    uint32_t c = tick % FM1_NCOL, r;
    fm1_in.raw[c] = 0;
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][c] >= 0 && level[FM1_KEYMAP[r][c]])
            fm1_in.raw[c] |= (uint8_t)(1u << r);
#if FELUCCA_KEYS_FAST
    fm1__keys(c);
#endif
    tick++;
    if (tick % FM1_NCOL == 0u)
        fm1__frame();
}
static uint32_t edges(uint32_t id)                   /* press edges of key id since the last call */
{
    uint32_t e = id >= 14u ? fm1_in.notes_pressed >> (id - 14u) & 1u : fm1_in.pressed >> id & 1u;
    fm1_in.notes_pressed = fm1_in.pressed = fm1_in.released = 0;
    return e;
}
static int down(uint32_t id) { return id >= 14u ? (fm1_in.notes >> (id - 14u)) & 1u : (fm1_in.buttons >> id) & 1u; }

int main(void)
{
    uint32_t id, ph, worst = 0, sum = 0, n = 0, glitch_ok = 1, bounce_ok = 1, chatter_ok = 1, row;
    for (id = 0; id < FM1_NKEY; id++) {
        if (key_col(id, &row) < 0)
            continue;
        for (ph = 0; ph < FM1_NCOL; ph++) {          /* the closure at every phase of the scan */
            uint32_t t0, k;
            memset((void *)&fm1_in, 0, sizeof fm1_in);
            memset(level, 0, sizeof level);
            tick = 0;
            for (k = 0; k < 3u * FM1_NCOL + ph; k++)
                scan_tick();
            edges(id);
            level[id] = 1;
            t0 = tick;
            for (k = 0; k < 10u * FM1_NCOL && !down(id); k++)
                scan_tick();
            if (tick - t0 > worst)
                worst = tick - t0;
            sum += tick - t0;
            n++;
            level[id] = 0;                           /* let go: the release, then a one-sample glitch */
            for (k = 0; k < 12u * FM1_NCOL; k++)
                scan_tick();
            edges(id);
            level[id] = 1;
            for (k = 0; k < FM1_NCOL; k++)
                scan_tick();
            level[id] = 0;
            for (k = 0; k < 5u * FM1_NCOL; k++)
                scan_tick();
            glitch_ok &= !edges(id) && !down(id);
            /* bounce on the way down: closed 1 frame, open 1, closed for good: one press */
            level[id] = 1;
            for (k = 0; k < FM1_NCOL; k++) scan_tick();
            level[id] = 0;
            for (k = 0; k < FM1_NCOL; k++) scan_tick();
            level[id] = 1;
            for (k = 0; k < 6u * FM1_NCOL; k++) scan_tick();
            bounce_ok &= edges(id) == 1u && down(id);
            /* chatter on the way up: open 2 frames, closed 1, open for good: it stays down until the end */
            level[id] = 0;
            for (k = 0; k < 2u * FM1_NCOL; k++) scan_tick();
            level[id] = 1;
            for (k = 0; k < FM1_NCOL; k++) scan_tick();
            level[id] = 0;
            for (k = 0; k < 4u * FM1_NCOL; k++) scan_tick();
            chatter_ok &= !edges(id);
            for (k = 0; k < 12u * FM1_NCOL; k++) scan_tick();
            chatter_ok &= !down(id) && !edges(id);
        }
    }
    printf("keys (%s): press latency from contact closure: mean %.2f ms, worst %.1f ms (%u cases)\n",
           FELUCCA_KEYS_FAST ? "fast" : "2.2 ", sum * 0.1 / n, worst * 0.1, n);
#if FELUCCA_KEYS_FAST
    check(worst <= 2u * FM1_NCOL + 1u, "a press within 2 frames (2.2 ms) of the closure, every key and phase");
#else
    check(worst <= 4u * FM1_NCOL, "a press within 4 frames of the closure, every key and phase");
#endif
    check(glitch_ok, "a closed sample alone plays nothing");
    check(bounce_ok, "a bounce on the way down: one press");
    check(chatter_ok, "chatter on the way up: one release, no second press");
    printf("%s\n", fails ? "KEYS TEST FAILED" : "keys test passed");
    return fails;
}
