/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, CARDS 2x2 by default and the picture alone (the user, 2026-10-12: "drop the row list";
 * docs/UI-OPTIMIST-DESIGN.md sections 3 and 11.6): in 2x2 a screen with a picture (SOUND's graphs, TEMPO, SCOPE) shows
 * it on the whole panel with no rows under it, the header naming the row and its n/N; the screens whose list is the
 * content (PROJECT, SYSTEM, SONG, a SOUND row with no picture) keep their rows under the big values; the mixer keeps
 * its 1x4 cards; 1x4 is drawn as before, the rows under the picture. Included by ui_optimist_test.c after
 * ui_optimist_cards.h. */
static uint8_t cards_boot = 0xFF;                       /* op_cards as a fresh unit starts (main: before the 1x4 tests) */

static void cp_draw(uint32_t mode)                      /* the screen drawn whole in the mode */
{
    op_cards = (uint8_t)mode;
    ui.force = 1;
    frame();
    frame();
}
static int cp_pos(char *p)                              /* the header's n/N for the screen shown: 1 when there is one */
{
    head_pos(ui.scr, ui.row[ui.scr], p);
    return p[0] == ' ' && strchr(p, '/') != 0;
}
/* the picture alone on the whole panel, in the track's colour down to the panel's foot (y0 .. 240: what a row there was) */
static int cp_pic_alone(uint32_t pic, uint32_t y0)
{
    char p[12];
    return lst.pic == pic && lst.shown == 0u && gr_h == OH_BODY && OP_PY == OY_PANEL_2X2 && cards_band_is(CARDS_2X2) &&
           px_in(0, y0, 240, 240 - y0, trk_col(song.sel)) && cp_pos(p);
}
static int cp_list(void)                                /* rows under the big values, the cursor's bar among them */
{
    return lst.shown > 0u && cards_band_is(CARDS_2X2) && px_in(0, (uint32_t)OP_PY, 236, OP_PH, trk_col(song.sel));
}
static void cards_pic_tests(void)
{
    char p[12];
    uint32_t w;
    check(cards_boot == CARDS_2X2, "CARDS: a fresh unit starts in 2x2 (the default, 2026-10-12)");
    w = bp23_word();
    bp23_from_word(0);
    check(op_cards == CARDS_2X2 && ((bp23_word() >> 23) & 1u) == 0u,
          "CARDS: an empty settings word (every record from before) reads 2x2 and writes bit 23 as 0");
    bp23_from_word(w);
    op_cards = CARDS_LINE;

    /* SOUND ENV: the envelope alone, 117 rows; 1x4: its rows under it, as before */
    reset_ui();
    song.sel = 0;
    tap(B_ENV);
    cp_draw(CARDS_2X2);
    ppm("opt-cardspic-sound-env");
    check(ui.scr == SCR_SOUND && cp_pic_alone(1u, 222u), "CARDS 2x2: SOUND ENV, the envelope alone on the panel (its attack at the foot), the header's n/N");
    cp_draw(CARDS_LINE);
    check(lst.pic == 1u && lst.shown > 0u && gr_h < OH_BODY && OP_PY == OY_PANEL && cards_band_is(CARDS_LINE),
          "CARDS 1x4: SOUND ENV keeps its rows under the envelope");
    /* SOUND LFO, the FX row's needles */
    tap(B_LFO);
    cp_draw(CARDS_2X2);
    ppm("opt-cardspic-sound-lfo");
    check(cp_pic_alone(1u, 200u), "CARDS 2x2: SOUND LFO, the wave alone on the panel");
    tap(B_FX);
    cp_draw(CARDS_2X2);
    check(ui.scr == SCR_SOUND && lst.pic == 1u && lst.shown == 0u && gr_h == OH_BODY && cp_pos(p),
          "CARDS 2x2: SOUND FX, the slots' needles alone on the panel");
    /* SOUND on a row with no picture: the rows stay (the list is all there is) */
    mix_to_sound();
    {
        uint32_t r, n = SCR->rows(), row = 0xFF;
        for (r = 1; r < n && row == 0xFF; r++)
            if (snd_page(r) && !snd_graph_page(r) && (snd_fam != SND_FM6))
                row = r;
        ui.row[SCR_SOUND] = (uint8_t)(row == 0xFF ? 0 : row);
        cp_draw(CARDS_2X2);
        check(row != 0xFF && lst.pic == 0u && cp_list(), "CARDS 2x2: SOUND on a row with no picture keeps its rows");
        ui.row[SCR_SOUND] = 0;
    }
    /* every SOUND row of every track: a picture alone, else the rows */
    {
        uint32_t t, r, n, ok = 1, pics = 0;
        char nm[48], rn[12];
        for (t = 0; t < NTRK && ok; t++) {
            reset_ui();
            song.sel = (uint8_t)t;
            mix_to_sound();
            n = SCR->rows();
            for (r = 0; r < n && ok; r++) {
                ui.row[SCR_SOUND] = (uint8_t)r;
                cp_draw(CARDS_2X2);
                ok &= (uint32_t)(lst.pic ? lst.shown == 0u && gr_h == OH_BODY && cp_pos(rn) : cp_list());
                pics += lst.pic != 0u;
                SCR->name(r, rn);
                snprintf(nm, sizeof nm, "opt-cardspic-t%u-%02u-%s", t, r, rn);
                for (w = 0; nm[w]; w++)
                    nm[w] = nm[w] == ' ' || nm[w] == '/' ? '-' : nm[w];
                ppm(nm);
                if (!ok)
                    printf("  cardspic: track %u SOUND row %u (%s): pic %u shown %u\n", t, r, rn, lst.pic, lst.shown);
            }
        }
        check(ok && pics > 8u, "CARDS 2x2: every SOUND row of every track: a picture alone on the panel, or its rows");
        song.sel = 0;
        op_cards = CARDS_LINE;
    }
    /* TEMPO: the tempo and the beat alone; the header says which of its rows (1x4: no n/N, the rows say it) */
    reset_ui();
    op_enter(SCR_TEMPO);
    cp_draw(CARDS_2X2);
    ppm("opt-cardspic-tempo");
    check(lst.pic == 3u && lst.shown == 0u && gr_h == OH_BODY && cp_pos(p) && !strcmp(p, " 1/3") &&
          px_in(8, (uint32_t)OP_PY + 20u, 100, 50, C_WHITE),
          "CARDS 2x2: TEMPO, the tempo big and the beat alone on the panel, the header 1/3");
    turn(EN_SELECT, 1);
    frames(2);
    check(cp_pos(p) && !strcmp(p, " 2/3") && lst.shown == 0u, "... SELECT: the REC row, the header 2/3");
    cp_draw(CARDS_LINE);
    check(lst.shown == 3u && !cp_pos(p), "CARDS 1x4: TEMPO keeps its three rows and no n/N");
    /* SCOPE: the trace to the foot, no tabs; the header names the source and its n/N */
    reset_ui();
    op_enter(SCR_SCOPE);
    cp_draw(CARDS_2X2);
    ppm("opt-cardspic-scope");
    check(SC_H == OH_BODY && cp_pos(p) && !px_in(1, 240u - 20u, 46, 20, scope_col(ui.row[SCR_SCOPE] % (1u + NTRK))),
          "CARDS 2x2: SCOPE, the trace to the foot, no tabs, the header's n/N");
    cp_draw(CARDS_LINE);
    check(SC_H == OH_BODY - 23 && !cp_pos(p) && px_in(1, (uint32_t)SC_TAB_Y, 46, (uint32_t)SC_TAB_H, scope_col(ui.row[SCR_SCOPE] % (1u + NTRK))),
          "CARDS 1x4: SCOPE keeps its tabs");
    /* the list pages keep their rows under the big values */
    {
        static const uint8_t S[] = {SCR_PROJECT, SCR_SYSTEM, SCR_SONG, SCR_FX};
        uint32_t i, ok = 1;
        for (i = 0; i < sizeof S; i++) {
            reset_ui();
            op_enter(S[i]);
            cp_draw(CARDS_2X2);
            if (S[i] == SCR_PROJECT)
                ppm("opt-cardspic-project");
            ok &= (uint32_t)cp_list();
            if (!ok) {
                printf("  cardspic: list screen %u\n", S[i]);
                break;
            }
        }
        check(ok, "CARDS 2x2: PROJECT, SYSTEM, SONG and FX keep their rows under the big values");
    }
    /* the mixer: 1x4 whatever the setting */
    reset_ui();
    cp_draw(CARDS_2X2);
    ppm("opt-cardspic-mixer");
    check(ui.scr == SCR_HOME && !cards_2x2() && OP_PY == OY_PANEL && cards_band_is(CARDS_LINE),
          "CARDS 2x2: the mixer keeps its four cards in a line");
    press(B_FX);                                        /* a layer held over the mixer: the layer's screen, 2x2 */
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    ppm("opt-cardspic-layer-fx");
    check(lay.shown == LY_FX && cards_2x2() && cards_band_is(CARDS_2X2) && OP_PY == OY_PANEL_2X2,
          "CARDS 2x2: a layer held over the mixer takes the big values (its own screen)");
    frames(HOLD_FRAMES);                                /* (held past the tap's grace: no tap on the release) */
    release(B_FX);
    frames(3);
    check(ui.scr == SCR_HOME && lay.shown == LY_PLAY && !cards_2x2() && cards_band_is(CARDS_LINE),
          "... let go: the mixer's 1x4 cards again");
    /* STEP: the grid on the whole panel in both modes; a held step's lines under the big values */
    check(SP_Y + SG_INFO_Y >= OY_CARD + OH_CARD_2X2 && SP_Y + SG_INFO_Y + SG_INFO_H <= SP_Y + SG_MK_Y,
          "CARDS 2x2: STEP, a held step's two lines sit under the big values, over the grid's foot");
    op_cards = CARDS_2X2;                               /* random use in the default: every draw on the screen */
    fuzz(3000, 4242);
    check(1, "3000 frames of random use in CARDS 2x2 with the picture alone");
    op_cards = CARDS_LINE;
    reset_ui();
}
