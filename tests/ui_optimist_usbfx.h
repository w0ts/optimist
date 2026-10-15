/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the USB input's insert slots on the mixer (op_mixer.c, fx_slots.c ut). Included by
 * ui_optimist_test.c after ui_optimist_mixer.h. */
#if FELUCCA_USB_AUDIO && FELUCCA_INS_EQ3 && FELUCCA_INS_GATER && FELUCCA_INS_CHORUS
/* the USB input's insert slots take every insert type (2026-10-15, the user: "any FX insert on the USB IN track, like any
 * other track"): its TYPE cards list them all; EQ3 in a slot: the row's USB EQ3 and USB EQ3 F sets (the input's own bands),
 * GATER: USB GATER (its own PAT RATE SHAPE), JUNO CHORUS: USB JUNO (its MODE); a pooled type with none free: the card's FULL,
 * dim, as a track's; each card its help line (opt-usbfx-*.ppm) */
static int ut_set_of(const char *name)                  /* the USB row's set named name, -1 none */
{
    uint32_t set, n = mx_sets(MXR_USB), keep = mx.set;
    char b[16];
    int at = -1;
    for (set = 0; set < n && at < 0; set++) {
        mx.set = (uint8_t)set;
        mix_name(MXR_USB, b);
        if (!strcmp(b, name))
            at = (int)set;
    }
    mx.set = (uint8_t)keep;
    return at;
}
static void usbfx_tests(void)
{
    uint32_t t, n = 0, ok = 1, k;
    int16_t *vp;
    const param_desc_t *d;
    cell_t c;
    int s;
    reset_ui();
    ut_defaults();
    go_home();
    ui.row[SCR_HOME] = MXR_USB;
    for (t = 1; t < FXT_N; t++)
        n += (FXT_BUILT & FXT_INSERT) >> t & 1u;
    d = ut_desc(UT_ID_TY, &vp);
    for (t = 0; d && t <= (uint32_t)d->max; t++)
        ok &= ut_list[t] == FXT_NONE || ((FXT_BUILT & FXT_INSERT) >> ut_list[t] & 1u);
    check(d && (uint32_t)d->max == n && ok, "USB input: its S1 TYPE lists ---- and every insert type a track's does");
    ut_load(0, FXT_EQ3);
    ut_load(1, FXT_GATE);
    ut.amt[1] = 100;
    s = ut_set_of("USB EQ3");
    ok = s >= 0 && mx_labels(MXR_USB, (uint32_t)s, "LO", "MID", "HI", 0) && ut_set_of("USB EQ3 F") == s + 1 &&
         mx_labels(MXR_USB, (uint32_t)s + 1u, "FREQ L", "FREQ H", 0, 0) && ut_set_of("USB GATER") == s + 2 &&
         mx_labels(MXR_USB, (uint32_t)s + 2u, "PAT", "RATE", "SHAPE", 0);
    check(ok, "... EQ3 on S1, GATER on S2: the sets USB EQ3 (LO MID HI), USB EQ3 F (FREQ L FREQ H), USB GATER (PAT RATE SHAPE)");
    mx.set = (uint8_t)(s < 0 ? 0 : s);
    mix_turn(MXR_USB, 0, -4, 1);
    mix_turn(MXR_USB, 2, 3, 1);
    check(eq3_v[NTRK + 1u][EQV_LO] == -4 && eq3_v[NTRK + 1u][EQV_HI] == 3 && eq3_v[0][EQV_LO] == 0 && eq3_v[NTRK][EQV_LO] == 0,
          "... USB EQ3's LO and HI knobs: the input's own bands (not T1's, not the master's)");
    ui.force = 1, frame(), ppm("opt-usbfx-eq3");
    mx.set = (uint8_t)(s < 0 ? 0 : s + 2);
    mix_turn(MXR_USB, 0, 2, 1);
    mix_turn(MXR_USB, 1, 1, 1);
    check(ut.v[UTV_GPAT] == 10 && ut.v[UTV_GRATE] == 2 && trk[0].p[P_TGPAT] == TP[P_TGPAT].def,
          "... USB GATER's PAT and RATE: the input's own (TRNC1 + 2, 1/32), T1's untouched");
    ui.force = 1, frame(), ppm("opt-usbfx-gater");
#if FELUCCA_PARAM_HELP
    for (k = 0, ok = 1; k < 3u; k++) {
        mix_cell(MXR_USB, k, &c);
        ok &= hl_find(SCR_HOME, MXR_USB, &c) != 0u;
    }
    mx.set = (uint8_t)(s < 0 ? 0 : s);
    for (k = 0; k < 3u; k++) {
        mix_cell(MXR_USB, k, &c);
        ok &= hl_find(SCR_HOME, MXR_USB, &c) != 0u;
    }
    check(ok, "... the help line on USB EQ3's and USB GATER's cards");
#endif
#if FELUCCA_INS_JCHORUS
    ut_load(1, FXT_JUNO);
    s = ut_set_of("USB JUNO");
    check(s >= 0 && mx_labels(MXR_USB, (uint32_t)s, "MODE", 0, 0, 0) && ut_set_of("USB GATER") < 0,
          "... JUNO CHORUS on S2 (GATER out): the set USB JUNO (MODE), no USB GATER");
#endif
    {   /* the pool: CHRS on every instance's track, then on the input's S1: FULL (dim), as a track's card */
        static const uint8_t C2[2] = {FXT_CHRS, FXT_NONE};
        for (k = 0; k < FELUCCA_INS_CHORUS; k++)
            ins_put(k, C2);
        fxi_poll(), frame();                            /* (the main loop's pass: fxi.c) */
        ut_load(0, FXT_CHRS);
        ut.amt[0] = 100;
        fxi_poll(), fxi_poll(), frame();
        s = ut_set_of("INSERT");
        mx.set = (uint8_t)(s < 0 ? 0 : s);
        mix_cell(MXR_USB, 0, &c);
        check(s >= 0 && c.label && !strcmp(c.label, "S1 CHRS") && !strcmp(c.val, "FULL") && c.dim,
              "... CHRS on S1 with every instance taken: the card S1 CHRS over FULL, dim (as a track's)");
        ui.force = 1, frame(), ppm("opt-usbfx-full");
        ins_defaults();
        for (k = 0; k < 30u; k++)
            fxi_poll(), frame();                        /* (the tracks' fade out, then theirs back, the input's handed out) */
        mix_cell(MXR_USB, 0, &c);
        check(strcmp(c.val, "FULL") != 0 && !c.dim, "... a track's CHRS gone: the input's card heard again");
    }
    ut_defaults();
    eq3_defaults();
    mx.set = 0;
    ui.row[SCR_HOME] = MXR_T1;
    reset_ui();
    go_home();
}
#endif
