/* SPDX-License-Identifier: GPL-3.0-only */
/* The preset list's filter and the shifts on PRESETS (the user, 2026-10-31: "Left shift plus preset should go to the next/prev
 * engine. Right shift should cycle thru (filter) types of preset. Factory, user, favorites/init etc. With some visual feedback
 * so that the user understands (keep the filter at the top of the list, and make it taller). Also when scrolling the top of
 * the list flash"), included by ui_optimist_test.c after the favorites' helpers:
 *   SHIFT (OCT-) + PRESETS    a synth track: the next / previous engine, its first sound, whatever the filter; DR: its kits
 *   SHIFT2 (OCT+) + PRESETS   a synth track: the filter, one a turn, round: ALL FACTORY USER (FAVORITES) INIT; the sound stays;
 *                             the drum track: nothing
 *   the popup list            its header names the filter, taller, over the whole cards band; lit (a white frame, the name
 *                             white) PLF_HL_MS after a change, the header's ground black (no text on a fill)
 *   no flash                  PRESETS scrolled with the popup up: no pixel under the header gets two colours in one frame
 *                             (the cards were drawn under the box's top at each detent, then the box over them) */
static void pf_turn(uint32_t b, int32_t s)              /* OCT- / OCT+ held + PRESETS */
{
    fm1_in.buttons |= BT(b);
    frame();
    turn(EN_PRESET, s);
    fm1_in.buttons &= ~BT(b);
    frames(2);
}
static uint32_t pf_sound(void) { return (uint32_t)TSEL->eng_req << 16 | (TSEL->preset & 0xFFFFu) | (uint32_t)user_of(TSEL) << 24; }
static int pf_list_is(uint32_t f)                       /* the list as browsed holds only filter f's sounds (some) */
{
    uint32_t i, k, e, total;
    (void)preset_pos(&total);
    for (i = 0; i < total; i++) {
        e = preset_at(i, &k);
        if (pl_source(e, k) != f)
            return 0;
    }
    return 1;
}
static int pf_lit(void) { return px_in(12, POP_Y + 4, 216, 2, C_WHITE); }   /* the header's white frame */
static uint32_t pf_scroll_flash(uint32_t n)             /* n PRESETS detents with the popup up: the pixels that flashed */
{
    uint32_t d, bad = 0;
    for (d = 0; d < n; d++) {
        fl_bad = 0, fl_ylo = 240u, fl_yhi = 0;
        fl_on = 1;
        turn(EN_PRESET, d & 1u ? -1 : 1);
        frames(2);
        fl_on = 0;
        bad += fl_bad;
    }
    return bad;
}
static void prefilter_tests(void)
{
    uint32_t i, all, total, e0, here, ok, seen = 0, bad;
    char b[120];

    reset_ui();
    song.sel = 0;
    frame();
    /* SHIFT2 + PRESETS: the filter, round, the sound stays */
    here = pf_sound();
    (void)preset_pos(&all);
    check(pl_filt == PLF_ALL, "the preset list's filter: ALL at the start");
    for (i = 0, ok = 1; i < PLF_NORDER; i++) {
        pf_turn(B_OCTUP, 1);
        ok &= pl_filt == PLF_ORDER[(i + 1u) % PLF_NORDER] && pf_sound() == here;
        ok &= op_overlay() == 5u && pop_kind == 1u;
        seen |= 1u << pl_filt;
    }
    check(ok && pl_filt == PLF_ALL, "SHIFT2 + PRESETS right: ALL FACTORY USER (FAVORITES) INIT and round to ALL, the sound kept, the popup up");
    check(seen == ((1u << PLF_ALL) | (1u << PLF_FACTORY) | (1u << PLF_USER) | (1u << PLF_INIT) | (FELUCCA_FAVORITES ? 1u << PLF_FAV : 0u)),
          "... every filter this build has, FAVORITES only with FELUCCA_FAVORITES");
    pf_turn(B_OCTUP, -1);
    check(pl_filt == PLF_ORDER[PLF_NORDER - 1u] && pf_sound() == here, "SHIFT2 + PRESETS left from ALL: the last, INIT (round)");
    check(song.octave == 0, "... OCT+ let go after: no octave");
    /* each filter's list */
    pl_filt = PLF_FACTORY;
    (void)preset_pos(&total);
    check(total > 0u && total < all && pf_list_is(PLF_FACTORY), "FACTORY: the factory sounds only, of every engine");
    pl_filt = PLF_INIT;
    (void)preset_pos(&total);
    {
        uint8_t l[NENGINES];
        check(total == pl_init_list(l) && total >= NLIST - NBANK && total > 1u && pf_list_is(PLF_INIT), "INIT: each engine's INIT, one an engine (every engine, not only those with no factory sound)");
    }
    pl_filt = PLF_USER;
    (void)preset_pos(&total);
    check(total >= (uint32_t)up_count() && pf_list_is(PLF_USER), "USER: the user presets (and the collections' slots the player fills) only");
    {
        uint32_t ufill = total;
    pl_filt = PLF_FACTORY;
    op_preset_go(0);
    pl_filt = PLF_INIT;
    turn(EN_PRESET, 1);
    {
        uint32_t k, cur = preset_pos(&total);
        snprintf(b, sizeof b, "INIT, a factory sound playing (not in it): PRESETS goes to its first, an INIT (%u of %u, engine %u%s)", (unsigned)cur,
                 (unsigned)total, (unsigned)TSEL->eng_req, op_armed() ? ", asked" : "");
        check(cur == 0u && pl_on_init(), b);
        (void)k;
    }
    pl_filt = PLF_ALL;
    /* the header: the filter's name, lit a moment after a change; no text on a fill */
    op_enter(SCR_SOUND);
    frames(2);
    pf_turn(B_OCTUP, 1);
    check(op_overlay() == 5u && pl_filt == PLF_FACTORY && pf_lit(), "the popup's header lit (a white frame) at once after the filter changed");
    check(px_in(16, POP_Y + 2, 150, POP_HH - 4, C_WHITE) && px_at(150, POP_Y + 14, C_BLACK) && px_at(150, POP_Y + 2 + POP_HH - 12, C_BLACK),
          "... the name drawn white on the box's black (the header not filled)");
    ppm("opt-prefilter-lit");
    frames(PLF_HL_MS / 16u + 2u);
    check(op_overlay() == 5u && !pf_lit() && px_in(16, POP_Y + 2, 150, POP_HH - 4, C_HI), "... the light gone after ~0.6 s, the popup still up, the name in the bright colour");
    ppm("opt-prefilter-factory");
    for (i = 0; i < PLF_NORDER; i++) {
        snprintf(b, sizeof b, "opt-prefilter-%u", (unsigned)i);
        if (pl_filt != PLF_ALL)
            ppm(b);
        pf_turn(B_OCTUP, 1);
        frames(PLF_HL_MS / 16u + 2u);
    }
    /* a filter that holds nothing: the popup, its header, NONE */
    if (!ufill) {
        pl_filt = PLF_INIT;
        pf_turn(B_OCTUP, -1);
        check(pl_filt == PLF_USER || pl_filt == PLF_FAV, "... (stepped back to an empty filter)");
        if (pl_filt == PLF_FAV)
            pf_turn(B_OCTUP, -1);
        (void)preset_pos(&total);
        check(pl_filt == PLF_USER && !total && op_overlay() == 5u && !strcmp(ui.msg, "NO USER PRESETS"),
              "USER with no user preset: the popup up all the same, \"NO USER PRESETS\"");
        ppm("opt-prefilter-empty");
        here = pf_sound();
        turn(EN_PRESET, 1);
        check(pf_sound() == here, "... PRESETS there: the sound stays");
    }
    }
    pl_filt = PLF_ALL;
    frames(OP_TOAST_FRAMES + 5u);

    /* SHIFT + PRESETS: the engines, whatever the filter */
    reset_ui();
    song.sel = 0;
    frame();
    e0 = TSEL->eng_req;
    pl_filt = PLF_USER;
    pf_turn(B_OCTDN, 1);
    if (TSEL->eng_req == e0)
        pf_turn(B_OCTDN, -1);
    check(TSEL->eng_req != e0 && pl_filt == PLF_USER, "SHIFT + PRESETS: another engine (USER filter on: not narrowed by it), the filter kept");
    check(op_overlay() == 5u && pop_kind == 4u, "... the engines' popup");
    ppm("opt-prefilter-engine");
    {
        uint32_t k, at = preset_all_pos(&total), e = preset_all_at(at, &k);
        check(e == TSEL->eng_req && (at == 0u || preset_all_at(at - 1u, &k) != e), "... its first sound loaded (the first of its group in the whole list)");
    }
    pl_filt = PLF_ALL;
    set_engine_of(TSEL, e0);
    snd_mark(song.sel);
    check(song.octave == 0, "... OCT- let go after: no octave");

    /* the drum track: SHIFT + PRESETS its kits, SHIFT2 + PRESETS nothing */
    reset_ui();
    for (i = 0; i < NTRK && !is_drum(&trk[i]); i++)
        ;
    if (i < NTRK) {
        uint32_t kit;
        song.sel = (uint8_t)i;
        ui.lane = 0;
        frame();
        kit = drum_kit_pos();
        pf_turn(B_OCTDN, 1);
        if (drum_kit_pos() == kit)
            pf_turn(B_OCTDN, -1);
        check(drum_kit_pos() != kit, "DR: SHIFT + PRESETS, the kits");
        kit = drum_kit_pos();
        pf_turn(B_OCTUP, 1);
        check(drum_kit_pos() == kit && pl_filt == PLF_ALL, "DR: SHIFT2 + PRESETS does nothing (no filter on the kits)");
        TDRUM->p[P_E0] = 0;
        snd_mark(i);
    }

    /* no flash when scrolling: EDIT (its cards change with the sound) and the mixer */
    reset_ui();
    song.sel = 0;
    track_select(0);
    tap(B_EDIT);
    frames(2);
    turn(EN_PRESET, 1);
    frames(2);
    check(op_overlay() == 5u, "EDIT: PRESETS, the popup up");
    bad = pf_scroll_flash(8);
    snprintf(b, sizeof b, "EDIT: PRESETS scrolled with the popup up: no pixel flashed (%u; rows %u..%u)", (unsigned)bad, (unsigned)fl_ylo,
             (unsigned)fl_yhi);
    check(bad == 0u, b);
    fl_on = 1, fl_bad = 0;
    pf_turn(B_OCTUP, 1);
    frames(PLF_HL_MS / 16u + 2u);                       /* (the light going: the header again) */
    fl_on = 0;
    check(fl_bad == 0u, "EDIT: SHIFT2 + PRESETS, the header lit then not: no pixel flashed");
    pl_filt = PLF_ALL;
    frames(OP_TOAST_FRAMES + 5u);
    reset_ui();
    song.sel = 0;
    frame();
    turn(EN_PRESET, 1);
    frames(2);
    bad = pf_scroll_flash(8);
    snprintf(b, sizeof b, "the mixer: PRESETS scrolled with the popup up: no pixel flashed (%u)", (unsigned)bad);
    check(bad == 0u, b);
    frames(OP_TOAST_FRAMES + 5u);
    check(op_overlay() == 0u && px_in(0, OY_CARD, 240, 40, C_BLACK), "... the popup gone: the panel again");
    reset_ui();
}
