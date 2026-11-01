/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the level meters' fixed -40 dBFS floor (core/vufloor.h, op_draw.c meter_h, op_mixdraw.c; the user, 2026-10-31:
 * "we are a groovebox, not a DAW"): a peak under it draws nothing, 0 dBFS is the full scale, the MASTER scale's ticks run from 0 down to
 * -40. Screen opt-vufloor-master-40.ppm. */
static void vumeter_master_view(double amp, const char *shot)   /* MASTER's tall meter and returns, a sine of the given amplitude in the ring (the master reads its peak there; 1036: about -30 dBFS) */
{
    uint32_t i;
    reset_ui();
    ui.row[SCR_HOME] = MXR_MASTER;
    for (i = 0; i < VIS_RING; i++)
        vis_pcm[0][i] = vis_pcm[1][i] = (int32_t)(sin((double)i * 2.0 * 3.14159265 / 100.0) * amp);
    ui.force = 1;
    frames(3);
    ppm(shot);
}
/* the audio meters' peak-hold marker (op_mixdraw.c mx_peak; the user, 2026-10-29: "VU meters just get normal audio peaks"): it jumps to a
 * new peak, holds MX_PH_HOLD frames, falls 20 dB/s, catches up with a louder one, is red at 0 dBFS, and is never drawn under the -40 floor.
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
    reset_ui();
    ui.row[SCR_HOME] = MXR_MASTER;
    peak_ring(8000.0);                                  /* (about -12 dBFS) */
    ui.force = 1;
    frames(3);
    ph0 = MX_PHL(MXR_MASTER);
    check(ph0 > 100u && ph0 >= mxd.pk[MXR_MASTER], "peak hold: the marker jumps to the peak (it stands at or over where the bar ends)");
    peak_ring(0.0);                                     /* (silence) */
    frames(MX_PH_HOLD / 2u);
    q0 = mxd.ph[MXR_MASTER];
    check(mxd.pk[MXR_MASTER] < ph0 && MX_PHL(MXR_MASTER) == ph0 && px_in(peak_x(ph0) - 2u, PEAK_Y, 2, 1, mx_vu_colat((int32_t)peak_x(ph0) - (int32_t)MXM_X - 1, (int32_t)MXM_W, 0u)),
          "peak hold: the bar has fallen, the marker is held where it was, drawn there in the meter's own colour");
    ppm("opt-peakhold-master");
    for (i = MX_PH_HOLD / 2u; i < MX_PH_HOLD + 10u && mxd.ph[MXR_MASTER] == q0; i++)
        frames(1);                                      /* (the loud frames' own hold ran a frame or two down: it ends at 89 .. 91 quiet frames) */
    check(i >= MX_PH_HOLD - 2u && i <= MX_PH_HOLD + 1u && mxd.ph[MXR_MASTER] < q0, "peak hold: held MX_PH_HOLD (~1.5 s) frames, then it starts to fall");
    q0 = mxd.ph[MXR_MASTER];
    frames(20);                                         /* (20 frames: a build with a sounding master may stop it at that level sooner) */
    ok = q0 - mxd.ph[MXR_MASTER] == 20u * MX_PH_FALL;
    check(ok && MX_PHL(MXR_MASTER) < ph0, "peak hold: then it falls steadily, 473/256 of a unit a frame (20 dB/s on the -40 scale)");
    peak_ring(8000.0);
    frames(1);
    check(MX_PHL(MXR_MASTER) >= ph0 - 15u && MX_PHL(MXR_MASTER) <= ph0 + 15u && mxd.phd[MXR_MASTER] == MX_PH_HOLD, "peak hold: a level at or over the marker takes it back up and holds again");
    peak_ring(400000.0);                                 /* (over 0 dBFS: the full scale) */
    frames(3);
    check(MX_PHL(MXR_MASTER) >= MX_LV - 2u && px_in((uint32_t)MXM_X + MXM_W - 2u, PEAK_Y, 2, 1, C_ERR), "peak hold: at 0 dBFS the marker is red");
    peak_ring(0.0);
    frames(MX_PH_HOLD + 2u);
    reset_ui();                                         /* (-50 dBFS is under the floor) */
    ui.row[SCR_HOME] = MXR_MASTER;
    song.g[G_FILT] = 1;                                 /* (the master's DJ filter a hair into high-pass, as an earlier test's random use can leave it: its rounding left a DC of ~726 on a silent master before fx.c tflt_tick_e, read here as a peak) */
    peak_ring(104.0);
    ui.force = 1;
    frames(5);
    check(mxd.ph[MXR_MASTER] == 0u && !px_in((uint32_t)MXM_X, PEAK_Y, MXM_W, 1, C_OK) && !px_in((uint32_t)MXM_X, PEAK_Y, MXM_W, 1, C_ERR),
          "peak hold: a peak under the floor gets no marker, the bar none either");
    peak_ring(0.0);
    reset_ui();
}
static void vumeter_tests(void)
{
    int32_t pk, db = VU_FLOOR_DB, d = -db + 6, prev = 0, lowest = 0, at6;
    uint32_t m1 = 1, edge, ok;
    for (pk = 0; pk <= 40000; pk++) {
        int32_t h = (int32_t)meter_h(pk);
        m1 &= h >= prev && h <= METER_H;
        if (h && !lowest)
            lowest = pk;
        prev = h;
    }
    /* the lowest peak drawn is the floor's: its dB within 0.7 dB of the floor, the peak under it within 0.7 dB above */
    edge = lowest > 0 && 20.0 * log10((double)lowest / 32767.0) > (double)db - 0.7 && 20.0 * log10((double)(lowest - 1) / 32767.0 + 1e-9) < (double)db + 0.7;
    at6 = (int32_t)meter_h(8192);                       /* -12 dBFS: the scale is the floor + 6 dB wide */
    ok = m1 && edge && meter_h(32767) == METER_H && meter_h(0) == 0u && meter_h(-3) == 0u &&
         (int32_t)meter_h(65535) == METER_H && at6 >= METER_H * (d - 12) / d - 2 && at6 <= METER_H * (d - 12) / d + 2;
    check(ok, "VU floor -40: heights rise with the peak, nothing under the floor, the first peak drawn is the floor's, 0 dBFS the full scale, -12 dB on the scale");
    peakhold_tests();                                   /* (its frames move the master compressor: SCRS golden GR -5 -> -4) */
    vumeter_master_view(1036.0, "opt-vufloor-master-40");
    reset_ui();
}
