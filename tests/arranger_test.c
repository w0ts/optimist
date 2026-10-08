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
    c.entry[0].bars = 0; assert(!arr_valid(&c, 15));
    c.entry[0].bars = 65; assert(!arr_valid(&c, 15));
    c.entry[0].bars = 1; c.entry[0].scene = 4; assert(!arr_valid(&c, 15));
    c.entry[0].scene = 0; c.count = 17; assert(!arr_valid(&c, 15));
    puts("arranger: order, repeats, stop, loop, tempo change, fractional timing and invalid scenes PASS");
    return 0;
}
