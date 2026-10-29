/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > METERS > VU FLOOR (core/vufloor.h, op_draw.c meter_h, op_mixdraw.c): the dB at every level meter's
 * bottom, -80 .. -20 in 10 dB steps, -60 by default and then exactly the meter of before the setting; a lower floor shows quieter
 * peaks, a higher one fewer, 0 dBFS is always the full scale, the MASTER scale's ticks run from 0 down to the floor. Screens
 * opt-vufloor-master-80 / -60 / -20.ppm (VUF_SET: the baseline build of the same test is made without the setting, sed on this line). */
#ifdef VUF_BASELINE                                     /* the build of before the setting: its -60 screen, to compare */
#define VUF_SET(i) ((void)(i))
#else
#define VUF_SET(i) (vu_floor_i = (uint8_t)(i))
#endif
static uint32_t meter_h_before(int32_t pk)             /* the function as it was before the setting (-60 only) */
{
    static const uint8_t T[9] = {0, 11, 21, 29, 37, 45, 52, 58, 64};
    uint32_t a = pk > 0 ? (uint32_t)pk : 0u, lg, rem, idx, r, sh, f;
    if (a < 32u)
        return 0u;
    lg = 31u - (uint32_t)__builtin_clz(a);
    rem = a - (1u << lg);
    sh = lg - 3u;
    idx = rem >> sh;
    r = rem & ((1u << sh) - 1u);
    f = T[idx] + (((uint32_t)T[idx + 1u] - T[idx]) * r + (1u << (sh - 1u))) / (1u << sh);
    return (uint32_t)ui_clamp((int32_t)((lg - 5u) * 64u + 64u + f), 0, METER_H);
}
static void vufloor_master_view(uint32_t fl, double amp, const char *shot)   /* MASTER's tall meter and returns at a floor, a sine of the given amplitude in the ring (the master reads its peak there; 1036: about -30 dBFS) */
{
    uint32_t i;
    VUF_SET(fl);
    reset_ui();
    VUF_SET(fl);
    ui.row[SCR_HOME] = MXR_MASTER;
    for (i = 0; i < VIS_RING; i++)
        vis_pcm[0][i] = vis_pcm[1][i] = (int32_t)(sin((double)i * 2.0 * 3.14159265 / 100.0) * amp);
    ui.force = 1;
    frames(3);
    ppm(shot);
}
#ifndef VUF_BASELINE
/* the audio meters' peak-hold marker (op_mixdraw.c mx_peak; the user, 2026-10-29: "VU meters just get normal audio peaks"): it jumps to a
 * new peak, holds MX_PH_HOLD frames, falls 20 dB/s, catches up with a louder one, is red at 0 dBFS, and is never drawn under the VU FLOOR.
 * Screens opt-peakhold-master.ppm (the marker stands over the fallen bar). */
static void peak_ring(double amp)                       /* a sine of the given amplitude in the scope's ring: MASTER reads its peak there */
{
    uint32_t i;
    for (i = 0; i < VIS_RING; i++)
        vis_pcm[0][i] = vis_pcm[1][i] = (int32_t)(sin((double)i * 2.0 * 3.14159265 / 100.0) * amp);
}
#define PEAK_Y ((uint32_t)(OP_PY + MXM_VY + 5))   /* (OP_PY moves with the page: read when used) */
static uint32_t peak_x(uint32_t ph) { return (uint32_t)MXM_X + ph * (uint32_t)MXM_W / MX_LV; }   /* the marker's right end */
static void peakhold_tests(void)
{
    uint32_t ph0, q0, i, ok;
    VUF_SET(VUF_DEF);
    reset_ui();
    VUF_SET(VUF_DEF);
    ui.row[SCR_HOME] = MXR_MASTER;
    peak_ring(8000.0);                                  /* (about -12 dBFS) */
    ui.force = 1;
    frames(3);
    ph0 = MX_PHL(MXR_MASTER);
    check(ph0 > 100u && ph0 >= mxd.pk[MXR_MASTER], "peak hold: the marker jumps to the peak (it stands at or over where the bar ends)");
    peak_ring(0.0);                                     /* (silence) */
    frames(MX_PH_HOLD / 2u);
    q0 = mxd.ph[MXR_MASTER];
    check(mxd.pk[MXR_MASTER] < ph0 && MX_PHL(MXR_MASTER) == ph0 && px_in(peak_x(ph0) - 2u, PEAK_Y, 2, 1, mx_vu_colat((int32_t)peak_x(ph0) - (int32_t)MXM_X - 1, (int32_t)(mx_vu_lv(MX_KNEE_PK) * (uint32_t)MXM_W / MX_LV), (int32_t)MXM_W, C_OK)),
          "peak hold: the bar has fallen, the marker is held where it was, drawn there in the meter's own colour");
    ppm("opt-peakhold-master");
    for (i = MX_PH_HOLD / 2u; i < MX_PH_HOLD + 10u && mxd.ph[MXR_MASTER] == q0; i++)
        frames(1);                                      /* (the loud frames' own hold ran a frame or two down: it ends at 89 .. 91 quiet frames) */
    check(i >= MX_PH_HOLD - 2u && i <= MX_PH_HOLD + 1u && mxd.ph[MXR_MASTER] < q0, "peak hold: held MX_PH_HOLD (~1.5 s) frames, then it starts to fall");
    for (ok = 1, i = 0; i < 60u; i++) {
        uint32_t before = mxd.ph[MXR_MASTER];
        frames(1);
        ok &= before - mxd.ph[MXR_MASTER] == MX_PH_FALL[VUF_DEF];
    }
    check(ok && MX_PHL(MXR_MASTER) < ph0 - 60u, "peak hold: then it falls steadily, 330/256 of a unit a frame (20 dB/s on the -60 scale)");
    peak_ring(8000.0);
    frames(1);
    check(MX_PHL(MXR_MASTER) >= ph0 - 15u && MX_PHL(MXR_MASTER) <= ph0 + 15u && mxd.phd[MXR_MASTER] == MX_PH_HOLD, "peak hold: a level at or over the marker takes it back up and holds again");
    peak_ring(400000.0);                                 /* (over 0 dBFS: the full scale) */
    frames(3);
    check(MX_PHL(MXR_MASTER) >= MX_LV - 2u && px_in((uint32_t)MXM_X + MXM_W - 2u, PEAK_Y, 2, 1, C_ERR), "peak hold: at 0 dBFS the marker is red");
    peak_ring(0.0);
    frames(MX_PH_HOLD + 2u);
    VUF_SET(VUF_N - 1u);                                /* (the floor at -20: -30 dBFS is under it) */
    reset_ui();
    VUF_SET(VUF_N - 1u);
    ui.row[SCR_HOME] = MXR_MASTER;
    peak_ring(1036.0);
    ui.force = 1;
    frames(5);
    check(mxd.ph[MXR_MASTER] == 0u && !px_in((uint32_t)MXM_X, PEAK_Y, MXM_W, 1, C_OK) && !px_in((uint32_t)MXM_X, PEAK_Y, MXM_W, 1, C_ERR),
          "peak hold: a peak under the VU FLOOR gets no marker, the bar none either");
    VUF_SET(VUF_DEF);
    peak_ring(0.0);
    reset_ui();
}
static void vufloor_tests(void)
{
    uint32_t i, fl, ok = 1, mono = 1, k;
    int32_t pk;
    /* the default is the meter of before: every peak of 16 bits (and beyond), the same height */
    VUF_SET(VUF_DEF);
    for (pk = -5; pk <= 70000; pk++)
        ok &= meter_h(pk) == meter_h_before(pk);
    check(ok, "VU FLOOR -60 (the default): meter_h gives, for every peak 0 .. 70000, the height it gave before the setting");
    ok = 1;
    for (fl = 0; fl < VUF_N; fl++) {
        int32_t db = vuf_db(fl), d = -db + 6, prev = 0, lowest = 0, at6;
        uint32_t m1 = 1, edge = 1;
        VUF_SET(fl);
        for (pk = 0; pk <= 40000; pk++) {
            int32_t h = (int32_t)meter_h(pk);
            m1 &= h >= prev && h <= METER_H;
            if (h && !lowest)
                lowest = pk;
            prev = h;
        }
        /* the lowest peak drawn is the floor's: its dB within 0.7 dB of the floor, the peak under it within 0.7 dB above (a step of 6.02 dB read as 6, as the scale always did) */
        edge = lowest > 0 && 20.0 * log10((double)lowest / 32767.0) > (double)db - 0.7 && 20.0 * log10((double)(lowest - 1) / 32767.0 + 1e-9) < (double)db + 0.7;   /* (integers: at -80 the steps are 2 dB) */
        at6 = (int32_t)meter_h(8192);                   /* -12 dBFS: the scale is the floor + 6 dB wide */
        ok &= m1 && edge && meter_h(32767) == METER_H && meter_h(0) == 0u && meter_h(-3) == 0u &&
              (int32_t)meter_h(65535) == METER_H && at6 >= METER_H * (d - 12) / d - 2 && at6 <= METER_H * (d - 12) / d + 2;
        if (!(m1 && edge))
            printf("  vufloor: floor %d: monotonic %u, edge %u (lowest peak %d)\n", db, m1, edge, lowest);
        mono &= m1;
    }
    check(ok && mono, "VU FLOOR -80 .. -20: heights rise with the peak, nothing under the floor, the first peak drawn is the floor's, 0 dBFS the full scale, -12 dB on the scale");
    VUF_SET(0);
    check(meter_h(1) == 0u && meter_h(4) > 0u && meter_h(5) > meter_h(4) && meter_h(7) >= meter_h(5),
          "VU FLOOR -80: peaks of 1 .. 7 (fewer than 3 mantissa bits) are handled: 1 under the floor, 4 on, rising");
    VUF_SET(VUF_DEF);
    /* the setting's line: the knob steps it, stops at the ends, YES goes round, SHIFT + the knob its factory value */
    reset_ui();
    k = line_named(SCR_SYSTEM, "VU FLOOR");
    check(vu_floor_i == VUF_DEF, "SYSTEM > METERS: VU FLOOR starts at -60");
    turn(LINE_KNOB(k), 1);
    check(vu_floor_i == VUF_DEF + 1u && vuf_db(vu_floor_i) == -50, "VU FLOOR: the knob up: -50");
    for (i = 0; i < 9u; i++)
        turn(LINE_KNOB(k), -1);
    check(vu_floor_i == 0u && vuf_db(vu_floor_i) == -80, "VU FLOOR: the knob down to the end: -80");
    for (i = 0; i < 9u; i++)
        turn(LINE_KNOB(k), 1);
    check(vu_floor_i == VUF_N - 1u && vuf_db(vu_floor_i) == -20, "VU FLOOR: the knob up to the other end: -20");
    VUF_SET(VUF_DEF);
    vufloor_master_view(0, 1036.0, "opt-vufloor-master-80");
    vufloor_master_view(VUF_DEF, 1036.0, "opt-vufloor-master-60");
    vufloor_master_view(VUF_N - 1u, 8000.0, "opt-vufloor-master-20");
    VUF_SET(VUF_DEF);
    reset_ui();
    peakhold_tests();
}
#endif
