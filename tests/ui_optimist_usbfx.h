/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the USB input's insert slots on the mixer (op_mixer.c, fx_slots.c ut). Included by
 * ui_optimist_test.c after ui_optimist_mixer.h. */
#if FELUCCA_USB_AUDIO && FELUCCA_INS_GATER && FELUCCA_INS_CHORUS
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
    check(EQ3_V(NTRK + 1u)[EQV_LO] == -4 && EQ3_V(NTRK + 1u)[EQV_HI] == 3 && EQ3_V(0)[EQV_LO] == 0 && EQ3_V(NTRK)[EQV_LO] == 0,
          "... USB EQ3's LO and HI knobs: the input's own bands (not T1's, not the master's)");
    ui.force = 1, frame(), ppm("opt-usbfx-eq3");
    mx.set = (uint8_t)(s < 0 ? 0 : s + 2);
    mix_turn(MXR_USB, 0, 2, 1);
    mix_turn(MXR_USB, 1, 3, 1);
    check(ut.v[UTV_GPAT] == 10 && ut.v[UTV_GRATE] == DUR_DEF_16 + 3 && trk[0].p[P_TGPAT] == TP[P_TGPAT].def,
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
    {   /* FX on the USB row (2026-10-19, the user: "the usb channel should have it's inserts on FX key, like other tracks"): its
         * insert slots, then each insert set in turn, round; FX lit; no track's FX layer, the track selected untouched */
        mitem_t it[MX_MAXI];
        uint32_t i, ns = mx_items(MXR_USB, it) / 4u, want[8], nw = 0, sel = song.sel;
        for (i = 0; i < ns && nw < 8u; i++)
            if (mx_ut_ins(it[i * 4u].grp))
                want[nw++] = i;
        mx.set = 0;
        for (i = 0, ok = nw >= 3u; i <= nw + 1u && ok; i++) {   /* (slots, USB EQ3, USB EQ3 F, USB GATER, out to VOLUME .., the slots) */
            tap(B_FX), frames(2);
            ok &= mx.set == (i < nw ? want[i] : i == nw ? 0u : want[0]) && ui.scr == SCR_HOME && ui.row[SCR_HOME] == MXR_USB;
        }
        check(ok && want[0] == 1u && song.sel == sel, "... FX on its row: its insert slots, then USB EQ3, USB EQ3 F, USB GATER in turn, then out to VOLUME PAN FX ON, again (the track selected untouched)");
        press(B_FX), frames(30);
        check(lay.shown != LY_FX && (op_btn_avail() >> B_FX & 1u), "... FX lit there; held, no track's FX layer");
        release(B_FX), frames(2);
    }
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
        check(s >= 0 && c.label && !strcmp(c.label, "S1 CHRS") && !strcmp(c.val, "CHRS") && c.strike && c.dim,
              "... CHRS on S1 with every instance taken: the card S1 CHRS, the name struck through, dim (as a track's)");
        ui.force = 1, frame(), ppm("opt-usbfx-full");
        ins_defaults();
        for (k = 0; k < 30u; k++)
            fxi_poll(), frame();                        /* (the tracks' fade out, then theirs back, the input's handed out) */
        mix_cell(MXR_USB, 0, &c);
        check(!c.strike && !c.dim, "... a track's CHRS gone: the input's card heard again (not struck)");
    }
    ut_defaults();
    eq3_defaults();
    mx.set = 0;
    ui.row[SCR_HOME] = MXR_T1;
    reset_ui();
    go_home();
}
#endif
