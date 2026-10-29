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
        if (WANT[i] == SYNC_LINK)
            continue;                                   /* (the settings word has no place for LINK: settings_word.c, not this change) */
        song.g[G_SYNC] = WANT[i];
        w = bp23_word();
        song.g[G_SYNC] = SYNC_INT;
        bp23_from_word(w);
        sets &= song.g[G_SYNC] == WANT[i];
    }
    check(sets, "SYNC: saved with each value, the settings record loads the same value (the stored numbers are unchanged)");
    song.g[G_SYNC] = save;
}
#endif
