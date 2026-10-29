/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, CLK IN / TEMPO's SYNC: the knob steps AUTO, the sources in AUTO's own order (clock_sync.c sy_rank:
 * LINK, TRS, USB, BLE), INT last, whatever the stored numbers (core.h SYNC_*: projects, settings, the editor protocol) are. */
#if FELUCCA_MIDI_CLOCK
static void syncorder_tests(void)
{
#if defined(FELUCCA_BLE) && FELUCCA_BLE && FELUCCA_LINK
    static const int16_t WANT[] = {SYNC_AUTO, SYNC_LINK, SYNC_TRS, SYNC_USB, SYNC_BLE, SYNC_INT};
#elif defined(FELUCCA_BLE) && FELUCCA_BLE
    static const int16_t WANT[] = {SYNC_AUTO, SYNC_TRS, SYNC_USB, SYNC_BLE, SYNC_INT};
#else
    static const int16_t WANT[] = {SYNC_AUTO, SYNC_TRS, SYNC_USB, SYNC_INT};
#endif
    const uint32_t n = sizeof WANT / sizeof WANT[0];
    const param_desc_t *d = &GP[G_SYNC];
    uint32_t i, up = 1, down = 1, sets = 1, pop = 1;
    int16_t save = song.g[G_SYNC];
    reset_ui();
    song.g[G_SYNC] = SYNC_AUTO;
    for (i = 0; i < n; i++) {
        up &= song.g[G_SYNC] == WANT[i];
        tp_turn(TPR_TEMPO, 3, 1, 1);                    /* (the SYNC card, one detent) */
    }
    up &= song.g[G_SYNC] == SYNC_INT;                   /* (turned past the end: stays, no wrap) */
    check(up, "CLK IN / SYNC: turned up from AUTO it steps AUTO, the sources in AUTO's own order (LINK TRS USB BLE as built), INT last; at INT it stays");
    for (i = n; i-- > 0;) {
        down &= song.g[G_SYNC] == WANT[i];
        tp_turn(TPR_TEMPO, 3, -1, 1);
    }
    down &= song.g[G_SYNC] == SYNC_AUTO;                /* (below AUTO: stays) */
    check(down, "CLK IN / SYNC: turned down from INT it walks the same list back to AUTO; at AUTO it stays");
    for (i = 0; i < n; i++) {                           /* the list (popup) and the knob agree; the stored numbers are the core.h ones */
        pop &= param_step(d, enum_at(d, (int32_t)i), 1) == enum_at(d, (int32_t)i + 1) || i + 1 == n;
        pop &= enum_at(d, (int32_t)i) == WANT[i] && enum_rank(d, WANT[i]) == (int32_t)i;
    }
    check(pop, "SYNC: the popup list's places and the knob's agree (enum_at / enum_rank), AUTO first, INT last");
    check(SYNC_INT == 0 && SYNC_USB == 1 && SYNC_TRS == 2 && SYNC_AUTO == 3 && SYNC_BLE == 4 && SYNC_LINK == 5,
          "SYNC: the stored values are unchanged (INT 0, USB 1, TRS 2, AUTO 3, BLE 4, LINK 5)");
    for (i = 0; i < n; i++) {                           /* the settings record (the HOME menu's SYNC is device-wide) keeps every value as it was */
        uint32_t w;
        song.g[G_SYNC] = WANT[i];
        w = bp23_word();
        song.g[G_SYNC] = SYNC_INT;
        bp23_from_word(w);
        sets &= song.g[G_SYNC] == WANT[i];
    }
    check(sets, "SYNC: saved with each value, the settings record loads the same value (LINK too: bit 31; the stored numbers are unchanged)");
    song.g[G_SYNC] = save;
}

/* MIDI OUT's list (OFF KEYS SEQ BOTH over the stored KEYS 0, BOTH 1, SEQ 2, OFF 3), LINK's (OFF ON ON+S/S), and the record's extension (CLK OUT's
 * mode, LINK, LINK OFS; a record from before reads BOTH / ON+S/S / CLK OFS) */
static void midi_lists_tests(void)
{
#if FELUCCA_MIDI_OUT
    {
        static const int16_t ORD[] = {MOUT_OFF, MOUT_KEYS, MOUT_SEQ, MOUT_BOTH};
        const param_desc_t *d = &BPS_DESC[BPS_MOUT];
        uint32_t i, ok = 1;
        int16_t v = MOUT_OFF;
        for (i = 0; i < 4u; i++) {
            ok &= v == ORD[i] && enum_at(d, (int32_t)i) == ORD[i] && enum_rank(d, ORD[i]) == (int32_t)i;
            v = (int16_t)param_step(d, v, 1);
        }
        ok &= v == MOUT_BOTH && MOUT_KEYS == 0 && MOUT_BOTH == 1 && MOUT_SEQ == 2 && MOUT_OFF == 3;
        for (i = 4; i-- > 0;) {
            ok &= v == ORD[i];
            v = (int16_t)param_step(d, v, -1);
        }
        check(ok && v == MOUT_OFF, "MIDI OUT: the knob steps OFF, KEYS, SEQ, BOTH (stored 3, 0, 2, 1), no wrap; the stored numbers unchanged");
    }
#endif
#if FELUCCA_LINK
    {   /* GLOBAL > MIDI: LINK beside CLK IN / CLK OUT (OFF, ON, ON+S/S; ON+S/S, what Link did before, the default), LINK OFS on the next page */
        uint32_t w, o, ok;
        reset_ui();
        link_mode = LINK_ONSS;
        tap(B_GLO);
        w = line_named(SCR_GLOBAL, "LINK");
        o = line_named(SCR_GLOBAL, "LINK OFS");
        check(w < SCR->rows() && o < SCR->rows(), "GLOBAL: a LINK line on the MIDI page and a LINK OFS line after it (Link builds)");
        turn(LINE_KNOB(w), 1);
        ok = link_mode == LINK_ONSS;                    /* (the knob stops at ON+S/S) */
        turn(LINE_KNOB(w), -1);
        ok &= link_mode == LINK_ON;
        turn(LINE_KNOB(w), -1);
        ok &= link_mode == LINK_OFF;
        turn(LINE_KNOB(w), -1);
        ok &= link_mode == LINK_OFF;
        turn(LINE_KNOB(w), 1);
        ok &= link_mode == LINK_ON;
        check(ok, "GLOBAL LINK: its knob steps OFF, ON, ON+S/S, stopping at both ends");
        sync_ofs[2] = 0;
        turn(LINE_KNOB(o), 1);
        check(o < SCR->rows() && sync_ofs[2] > 0 && sync_ofs[0] == 0 && sync_ofs[1] == 0, "GLOBAL LINK OFS: its own value, CLK OFS and BT OFS untouched");
        sync_ofs[2] = 0;
        link_mode = LINK_ONSS;
        reset_ui();
    }
#endif
#if FELUCCA_MIDI_OUT
    {
        uint32_t w, ok;
        reset_ui();
        bp_set[BPS_MOUT] = MOUT_KEYS;
        tap(B_GLO);
        w = line_named(SCR_GLOBAL, "MIDI OUT");
        turn(LINE_KNOB(w), -1);
        ok = bp_set[BPS_MOUT] == MOUT_OFF;
        turn(LINE_KNOB(w), 1);
        turn(LINE_KNOB(w), 1);
        ok &= bp_set[BPS_MOUT] == MOUT_SEQ;
        turn(LINE_KNOB(w), 1);
        ok &= bp_set[BPS_MOUT] == MOUT_BOTH;
        check(ok, "GLOBAL MIDI OUT: its knob left from KEYS is OFF, then right KEYS, SEQ, BOTH");
        bp_set[BPS_MOUT] = MOUT_KEYS;
        reset_ui();
    }
#endif
#if FELUCCA_FLASH
    {
        persist_t r;
        uint32_t ok = 1;
        memset(&r, 0, sizeof r);
        r.clk_ofs[0] = 17;
#if FELUCCA_MIDI_CLKOUT
        clko_on = CLKO_BOTH;                            /* (the word's bit 26 of a record from before) */
        persist_ext_load(&r);
        ok &= clko_on == (FELUCCA_BLE ? CLKO_BOTH : CLKO_USB);
#endif
#if FELUCCA_LINK
        link_mode = LINK_OFF;
        persist_ext_load(&r);
        ok &= link_mode == LINK_ONSS;                   /* (before the setting: Link shared play / stop) */
#endif
#if FELUCCA_MIDI_CLOCK
        sync_ofs[2] = 0;
        persist_ofs_load(&r);
        ok &= sync_ofs[2] == 17;                        /* (Link used CLK OFS) */
        r.ext_ver = 1;
        r.link_ofs = -42;
        persist_ofs_load(&r);
        ok &= sync_ofs[2] == -42 && sync_ofs[0] == 0;
        sync_ofs[0] = sync_ofs[2] = 0;
#endif
        check(ok, "the settings record: an older one reads CLK OUT BOTH, LINK ON+S/S, LINK OFS = CLK OFS; a newer one its own LINK OFS");
#if FELUCCA_MIDI_CLKOUT
        {
            static const uint8_t M[] = {CLKO_USB, CLKO_BLE, CLKO_BOTH};
            uint32_t i;
            ok = 1;
            for (i = 0; i < sizeof M && FELUCCA_BLE; i++) {
                persist_t q;
                memset(&q, 0, sizeof q);
                clko_on = M[i];
                persist_ext_save(&q);
                clko_on = CLKO_BOTH;                    /* (the word brings ON back; the record says which) */
                persist_ext_load(&q);
                ok &= clko_on == M[i];
            }
            check(ok, "the settings record: CLK OUT USB / BLE / BOTH each read back as saved");
        }
        clko_on = CLKO_OFF;
#endif
#if FELUCCA_LINK
        {
            uint32_t i, ok2 = 1;
            for (i = 0; i <= LINK_ONSS; i++) {
                persist_t q;
                memset(&q, 0, sizeof q);
                link_mode = (uint8_t)i;
                persist_ext_save(&q);
                link_mode = LINK_ONSS;
                persist_ext_load(&q);
                ok2 &= link_mode == i;
            }
            check(ok2, "the settings record: LINK OFF / ON / ON+S/S each read back as saved");
            link_mode = LINK_ONSS;
        }
#endif
    }
#endif
}
#endif
