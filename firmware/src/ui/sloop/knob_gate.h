/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOB GATE: how far a knob must move while a layer button is held before it counts (ui_input.c layers_input,
 * ui_layers.c layer_knobs). One detent is jitter: a finger resting on a knob, a knob brushed on the way to a key. It
 * must neither mark the layer used (that shows the layer's map at once and takes the tap away) nor act. The
 * movement is the net distance from where the press began, in detents, either way: +1 then -1 is none, +2 is two.
 * Pure: the caller owns the position, one per knob, and clears it when the layer button goes down. Host test:
 * tests/knob_gate_test.c. Included by ui/sloop/ui_layers.c. */
#ifndef FELUCCA_KNOB_GATE_H
#define FELUCCA_KNOB_GATE_H
#include <stdint.h>
#define KNOB_GATE_DETENTS 2     /* net detents from the press that make a knob a deliberate turn */

/* one read of a knob: turn detents (signed) added to *pos (the net movement since the press). Returns the net
 * movement to act on once it has reached KNOB_GATE_DETENTS either way (and clears *pos: the next read passes
 * straight through, the layer is used by then), else 0 (held back: nothing used, nothing done). */
static inline int32_t knob_gate(int32_t *pos, int32_t turn)
{
    int32_t p = *pos + turn, m = p < 0 ? -p : p;
    if (m > 1000)
        p = p < 0 ? -1000 : 1000;                       /* (a runaway read: bounded) */
    if (m < KNOB_GATE_DETENTS) {
        *pos = p;
        return 0;
    }
    *pos = 0;
    return p;
}
/* the same question without taking the movement (the release path: the turns read in the frame the button went up are
 * dropped anyway, only "was it deliberate" is asked): 1 when pos + turn has reached the gate */
static inline int knob_gate_met(int32_t pos, int32_t turn)
{
    int32_t p = pos + turn;
    return (p < 0 ? -p : p) >= KNOB_GATE_DETENTS;
}
#endif
