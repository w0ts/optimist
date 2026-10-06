/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (fm1-x0x, charlesvestal/fm1-x0x 61654ba, accel() in ui.c) */
/* Knob acceleration by turn speed, ported from X0X: a slow turn is one step a detent (fine
 * detail), quicker detents 2 / 3 / 5 / 8 steps, so a fast half turn sweeps 0..127. Ranges under
 * 100 stop at 3; ranges over 150 (the tempo) double when quick (16 a detent). Small ranges
 * (<= 24) and lists (callers pass range 0) are never accelerated. Several detents in one read are
 * timed per detent. dt_ms: milliseconds since the previous read of this knob. Pure (host test:
 * tests/knob_accel_test.c). */
#pragma once
#include <stdint.h>

static int32_t knob_accel(uint32_t dt_ms, int32_t s, int32_t range)
{
    uint32_t a = (uint32_t)(s < 0 ? -s : s), m;
    if (range <= 24 || !a)
        return s;
    if (a > 1u)
        dt_ms /= a;                                   /* per detent (a >= 2 here: never zero) */
    m = dt_ms < 12u ? 8u : dt_ms < 25u ? 5u : dt_ms < 45u ? 3u : dt_ms < 80u ? 2u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    if (range > 150 && m > 1u)
        m *= 2u;
    return s * (int32_t)m;
}
