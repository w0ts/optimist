/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "../firmware/src/seq/arranger.h"

int main(void)
{
    arr_config_t c;
    arr_clock_t r;
    unsigned long samples;
    unsigned transitions = 0;
    int event;
    arr_defaults(&c);
    assert(!arr_valid(&c, 7));                 /* missing drum/scene slot cannot silently play */
    assert(arr_begin(&r, &c, 7) == ARR_INVALID && !r.running);
    c.count = 3;
    c.entry[0] = (arr_entry_t){0, 1};
    c.entry[1] = (arr_entry_t){2, 2};
    c.entry[2] = (arr_entry_t){0, 1};          /* repeated scenes are valid */
    assert(arr_begin(&r, &c, 5) == 0);
    /* 137 BPM has a fractional samples-per-bar: transitions must never drift
     * by more than one 32-sample audio block, even over multiple sections. */
    for (samples = 0; r.running; samples += 32) {
        event = arr_next(&r, &c, 44118);
        if (event != ARR_NONE) {
            unsigned bars = transitions == 0 ? 1 : transitions == 1 ? 3 : 4;
            double expected = 44118.0 * 240.0 * bars / 137.0;
            assert(samples >= expected && samples - expected < 32.0);
            assert(event == (transitions == 0 ? 2 : transitions == 1 ? 0 : ARR_DONE));
            transitions++;
        }
        arr_elapse(&r, 32, 137);
    }
    assert(transitions == 3);
    c.count = 1; c.entry[0].bars = 1; c.loop = 1;
    assert(arr_begin(&r, &c, 1) == 0);
    arr_elapse(&r, 44118, 120);                /* half bar at 120 BPM */
    assert(arr_next(&r, &c, 44118) == ARR_NONE);
    arr_elapse(&r, 88236, 60);                 /* tempo change preserves musical position */
    assert(arr_next(&r, &c, 44118) == 0 && r.running);
    c.entry[0].bars = 0; assert(arr_valid(&c, 15));       /* (0: AUTO, the scene's longest pattern) */
    c.entry[0].bars = 65; assert(!arr_valid(&c, 15));
    c.entry[0].bars = 1; c.entry[0].scene = 4; assert(!arr_valid(&c, 15));
    c.entry[0].scene = 0; c.count = 17; assert(!arr_valid(&c, 15));
    /* AUTO: the part lasts the bars its scene reports (r.auto_bars, set when the scene is applied) */
    c.count = 2; c.loop = 0;
    c.entry[0] = (arr_entry_t){0, 0};
    c.entry[1] = (arr_entry_t){1, 3};
    assert(arr_begin(&r, &c, 3) == 0);
    r.auto_bars = 2;
    arr_elapse(&r, 44118, 240);                /* one bar at 240 BPM per 44118 samples x 240 / 240 */
    assert(arr_next(&r, &c, 44118) == ARR_NONE);
    arr_elapse(&r, 44118, 240);
    assert(arr_next(&r, &c, 44118) == 1);      /* after 2 bars: the second part */
    r.auto_bars = 0;
    c.entry[0].bars = 0;
    assert(arr_begin(&r, &c, 3) == 0 && r.auto_bars == 0);
    /* a jump: to part 2 at the next bar line, then to part 0 at once */
    c.count = 3; c.loop = 1;
    c.entry[0] = (arr_entry_t){0, 4}; c.entry[1] = (arr_entry_t){1, 4}; c.entry[2] = (arr_entry_t){2, 4};
    assert(arr_begin(&r, &c, 7) == 0);
    r.jump = 3; r.jump_now = 0;
    arr_elapse(&r, 44118, 240);
    assert(arr_next(&r, &c, 44118) == 2 && r.index == 2 && r.bar == 0 && r.jump == 0);   /* (at the first bar line) */
    r.jump = 1; r.jump_now = 1;
    assert(arr_next(&r, &c, 44118) == 0 && r.index == 0 && r.bar == 0 && r.phase == 0 && r.jump == 0 && r.jump_now == 0);   /* (at once) */
    r.jump = 9; r.jump_now = 1;                /* (past the chain: ignored) */
    assert(arr_next(&r, &c, 44118) == ARR_NONE && r.index == 0);
    /* S, A and a number: the part of entry e plays arr_eff_bars: its scene's BARS (auto_bars), the longest pattern (pat_bars), a number */
    c.count = 1; c.loop = 1;
    c.entry[0] = (arr_entry_t){0, 0};
    assert(arr_begin(&r, &c, 1) == 0);
    r.auto_bars = 3; r.pat_bars = 5;
    assert(arr_eff_bars(&c.entry[0], &r) == 3);                 /* S: the scene's own BARS */
    c.entry[0].bars = ARR_BARS_A;
    assert(arr_eff_bars(&c.entry[0], &r) == 5 && arr_valid(&c, 1));   /* A: the longest pattern, whatever the scene says */
    c.entry[0].bars = 7;
    assert(arr_eff_bars(&c.entry[0], &r) == 7);
    c.entry[0].bars = 65;
    assert(!arr_valid(&c, 1));
    c.entry[0].bars = 0; r.auto_bars = r.pat_bars = 0;
    assert(arr_eff_bars(&c.entry[0], &r) == 4);                 /* (not known: 4) */
    puts("arranger: order, repeats, stop, loop, tempo change, fractional timing and invalid scenes PASS");
    return 0;
}
