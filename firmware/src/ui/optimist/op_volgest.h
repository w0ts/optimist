/* SPDX-License-Identifier: GPL-3.0-only */
/* SHIFT + the volume knob: the output switch (the user, 2026-10-20: "shift plus move a bit the knob, like 15% to
 * toggle" between Bluetooth audio and the FM-1's own output). The volume knob is a pot read by the ADC (system/main.c
 * master_pot): its place is the volume, so while SHIFT is held the volume stays where it was, a move of 15 % of the
 * travel either way switches once, and after SHIFT is let go (a switch made) the knob takes the volume back only once it comes back
 * to where it was (a soft takeover: no jump). Pure (no hardware, no UI): op_bt.c calls it, tests/vol_gesture_test.c
 * tests it. */
#ifndef OP_VOLGEST_H
#define OP_VOLGEST_H
#include <stdint.h>

#define VG_FULL (1023 * 16)                             /* the knob's travel as master_pot keeps it (the pot x16) */
#define VG_STEP (VG_FULL * 15 / 100)                    /* the move that switches: 15 % */
#define VG_NEAR (VG_FULL / 100)                         /* back within 1 % of where it was: the volume follows again */
struct vgest {
    uint8_t on, done, wait, above;                      /* SHIFT held; switched this press; the takeover; the knob above k0 */
    int32_t k0;                                         /* the knob when SHIFT went down: the volume held */
};
/* one pot reading (knob as master_pot keeps it), SHIFT down or not -> 1 the volume holds (does not follow the knob);
 * *toggle 1 when this reading switches the output */
static int vgest_step(struct vgest *g, int shift, int32_t knob, int *toggle)
{
    *toggle = 0;
    if (shift) {
        if (!g->on) {
            g->on = 1, g->done = 0, g->wait = 0, g->k0 = knob;
        } else if (!g->done && (knob - g->k0 >= VG_STEP || g->k0 - knob >= VG_STEP)) {
            g->done = 1;
            *toggle = 1;
        }
        return 1;
    }
    if (g->on) {                                        /* SHIFT let go: a moved knob waits to come back, after a switch only
                                                         * (a plain SHIFT, an octave, left the knob dead until wiggled back:
                                                         * the user, 2026-10-21) */
        g->on = 0;
        g->wait = g->done && (knob - g->k0 >= VG_NEAR || g->k0 - knob >= VG_NEAR);
        g->above = knob > g->k0;
    }
    if (g->wait) {
        int32_t d = knob - g->k0;
        int crossed = g->above ? d <= 0 : d >= 0;       /* (back past where it was) */
        if (!crossed && (d >= VG_NEAR || -d >= VG_NEAR))
            return 1;                                   /* (still more than 1 % off, on the side it was left) */
        g->wait = 0;
    }
    return 0;
}
#endif
