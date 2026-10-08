/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The reverb tanks' shared parameter maths (XIP: run only when SIZE or DAMP changes, never per sample), for the
 * ROOM's long tails (fx.c room_long) and PLATE / FDN8 (reverb_alt.c rv_params). Included by fx.c. Was reverb_alt.c's. */

/* 2^(-y / 2^24), Q15 (a cubic for the fraction, 1e-4) */
static int32_t rv_exp2n(uint32_t y)
{
    uint32_t n = y >> 24;
    int32_t x = (int32_t)((y >> 8) & 0xFFFFu), p;
    p = 32765 - ((x * (22645 - ((x * (7556 - ((x * 1295) >> 16))) >> 16))) >> 16);
    return n > 15u ? 0 : p >> n;
}
