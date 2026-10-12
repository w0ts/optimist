/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, under the 2x2 cards: the picture when the screen has one, else the row list (the user,
 * 2026-10-12: "Keep the graph", "we can keep the list below when there is nothing to display besides the 4 cards",
 * "arp is the same as lfo/env. no list"; docs/UI-OPTIMIST-DESIGN.md sections 3 and 11.6). A screen with a picture
 * (SOUND's graphs: ENV, LFO, the FX needles, ARP, SCL; TEMPO, SCOPE) shows it on the whole panel with no rows; a screen
 * with none (EDIT, VOICE, PROJECT, SYSTEM, SONG, the master FX) shows its rows under the big values, the cursor's bar
 * among them; SONG's PATTERNS row keeps its rows under the session grid; the header names the row and its n/N on every
 * one; the mixer keeps its 1x4 cards. Included by ui_optimist_test.c after ui_optimist_cards.h. */
static void cp_draw(void)                               /* the screen drawn whole */
{
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
    return lst.pic == pic && lst.shown == 0u && gr_h == OH_BODY && OP_PY == OY_PANEL_2X2 && cards_band_is(BAND_2X2) &&
           px_in(0, y0, 240, 240 - y0, trk_col(song.sel)) && cp_pos(p);
}
/* no picture: the rows under the 2x2 cards, the cursor's bar among them, the header's n/N (one row: none) */
static int cp_list(void)
{
    char p[12];
    return lst.pic == 0u && lst.shown > 0u && OP_PY == OY_PANEL_2X2 && cards_band_is(BAND_2X2) &&
           px_in(0, (uint32_t)OP_PY, 236, OP_PH, trk_col(song.sel)) && (cp_pos(p) || SCR->rows() < 2u);
}
static void cards_pic_tests(void)
{
    char p[12];
    uint32_t w;
    /* SOUND ENV: the envelope alone, 117 rows */
    reset_ui();
    song.sel = 0;
    tap(B_ENV);
    cp_draw();
    ppm("opt-cardspic-sound-env");
    check(ui.scr == SCR_SOUND && cp_pic_alone(1u, 222u), "2x2: SOUND ENV, the envelope alone on the panel (its attack at the foot), the header's n/N");
    /* SOUND LFO, the FX row's needles, ARP, SCL */
    tap(B_LFO);
    cp_draw();
    ppm("opt-cardspic-sound-lfo");
    check(cp_pic_alone(1u, 200u), "2x2: SOUND LFO, the wave alone on the panel");
    tap(B_FX);
    cp_draw();
    check(ui.scr == SCR_SOUND && lst.pic == 1u && lst.shown == 0u && gr_h == OH_BODY && cp_pos(p),
          "2x2: SOUND FX, the slots' needles alone on the panel");
    tap(B_SCL);
    cp_draw();
    check(ui.scr == SCR_SOUND && lst.pic == 1u && lst.shown == 0u && gr_h == OH_BODY && cp_pos(p), "2x2: SOUND SCL, the scale alone");
    {   /* ARP: every one of its rows the ARP picture, no list (the user: "arp is the same as lfo/env. no list") */
        uint32_t r, n, ok = 1;
        tap(B_ARP);
        n = SCR->rows();
        for (r = 0; r < n; r++) {
            ui.row[SCR_SOUND] = (uint8_t)r;
            cp_draw();
            ok &= (uint32_t)(snd_page(r) && snd_page(r)->fam == FAM_ARP && lst.pic == 1u && lst.shown == 0u &&
                             gr_h == OH_BODY && snd_graph_page(r) && snd_graph_page(r)->graph == GR_ARP && cp_pos(p));
        }
        ppm("opt-cardspic-arp2");
        check(n >= 2u && ok, "2x2: every ARP row (ARP, ARP 2): the ARP picture alone, no rows, the n/N");
    }
    /* SOUND on a row with no picture: the rows under the big values */
    mix_to_sound();
    {
        uint32_t r, n = SCR->rows(), row = 0xFF;
        for (r = 1; r < n && row == 0xFF; r++)
            if (snd_page(r) && !snd_graph_page(r) && (snd_fam != SND_FM6))
                row = r;
        ui.row[SCR_SOUND] = (uint8_t)(row == 0xFF ? 0 : row);
        cp_draw();
        check(row != 0xFF && cp_list(), "2x2: SOUND on a row with no picture: its rows under the big values, the n/N");
        ui.row[SCR_SOUND] = 0;
    }
    /* every SOUND row of every track: a picture alone, else the rows */
    {
        uint32_t t, r, n, ok = 1, pics = 0, lists = 0;
        char nm[48], rn[12];
        for (t = 0; t < NTRK && ok; t++) {
            reset_ui();
            song.sel = (uint8_t)t;
            mix_to_sound();
            n = SCR->rows();
            for (r = 0; r < n && ok; r++) {
                ui.row[SCR_SOUND] = (uint8_t)r;
                cp_draw();
                ok &= (uint32_t)(lst.pic ? lst.shown == 0u && gr_h == OH_BODY && cp_pos(rn) : cp_list());
                pics += lst.pic != 0u;
                lists += lst.pic == 0u;
                SCR->name(r, rn);
                snprintf(nm, sizeof nm, "opt-cardspic-t%u-%02u-%s", t, r, rn);
                for (w = 0; nm[w]; w++)
                    nm[w] = nm[w] == ' ' || nm[w] == '/' ? '-' : nm[w];
                ppm(nm);
                if (!ok)
                    printf("  cardspic: track %u SOUND row %u (%s): pic %u shown %u\n", t, r, rn, lst.pic, lst.shown);
            }
        }
        check(ok && pics > 8u && lists > 4u, "2x2: every SOUND row of every track: a picture alone on the panel, or its rows");
        song.sel = 0;
    }
    /* TEMPO: the tempo and the beat alone; the header says which of its rows */
    reset_ui();
    op_enter(SCR_TEMPO);
    cp_draw();
    ppm("opt-cardspic-tempo");
    check(lst.pic == 3u && lst.shown == 0u && gr_h == OH_BODY && cp_pos(p) && !strcmp(p, " 1/3") &&
          px_in(8, (uint32_t)OP_PY + 20u, 100, 50, C_WHITE),
          "2x2: TEMPO, the tempo big and the beat alone on the panel, the header 1/3");
    turn(EN_SELECT, 1);
    frames(2);
    check(cp_pos(p) && !strcmp(p, " 2/3") && lst.shown == 0u, "... SELECT: the REC row, the header 2/3");
    /* SCOPE: the trace to the foot, no tabs; the header names the source and its n/N */
    reset_ui();
    op_enter(SCR_SCOPE);
    cp_draw();
    ppm("opt-cardspic-scope");
    check(SC_H == OH_BODY && cp_pos(p) && !px_in(1, 240u - 20u, 46, 20, scope_col(ui.row[SCR_SCOPE] % (1u + NTRK))),
          "2x2: SCOPE, the trace to the foot, no tabs, the header's n/N");
    /* the list screens: their rows under the big values, on every row */
    {
        static const uint8_t S[] = {SCR_PROJECT, SCR_SYSTEM, SCR_SONG, SCR_FX};
        static const char *const NM[] = {"project", "system", "song", "fx"};
        uint32_t i, r, ok = 1;
        char nm[40];
        for (i = 0; i < sizeof S; i++) {
            reset_ui();
            op_enter(S[i]);
            for (r = 0; r < SCR->rows() && ok; r++) {
                ui.row[S[i]] = (uint8_t)r;
                cp_draw();
                ok &= (uint32_t)(lst.pic ? S[i] == SCR_SONG && lst.shown > 0u : cp_list());   /* (SONG's PATTERNS: the grid and the rows) */
                if (!ok)
                    printf("  cardspic: screen %u row %u: pic %u shown %u\n", S[i], r, lst.pic, lst.shown);
            }
            ui.row[S[i]] = 0;
            cp_draw();
            snprintf(nm, sizeof nm, "opt-cardspic-%s", NM[i]);
            ppm(nm);
        }
        check(ok, "2x2: PROJECT, SYSTEM, SONG and the master FX, every row: the big values, the rows under them, the n/N");
    }
    /* EDIT 1 and VOICE: their rows under the big values */
    reset_ui();
    song.sel = 0;
    tap(B_EDIT);
    cp_draw();
    ppm("opt-cardspic-edit1");
    check(ui.scr == SCR_SOUND && cp_list(), "2x2: EDIT 1, the big values, its rows under them, the header's n/N");
    {
        uint32_t r, n = SCR->rows(), v = 0xFF;
        char rn[12];
        for (r = 0; r < n && v == 0xFF; r++) {
            SCR->name(r, rn);
            if (!strcmp(rn, "VOICE"))
                v = r;
        }
        ui.row[SCR_SOUND] = (uint8_t)(v == 0xFF ? 0 : v);
        cp_draw();
        ppm("opt-cardspic-voice");
        check(v != 0xFF && cp_list() && lst.cur == v, "2x2: VOICE, the big values, the rows under them, VOICE's bar");
        ui.row[SCR_SOUND] = 0;
    }
    /* the mixer: 1x4 */
    reset_ui();
    cp_draw();
    ppm("opt-cardspic-mixer");
    check(ui.scr == SCR_HOME && !cards_2x2() && OP_PY == OY_PANEL && cards_band_is(BAND_1X4),
          "the mixer keeps its four cards in a line");
    press(B_FX);                                        /* a layer held over the mixer: the layer's screen, 2x2 */
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    ppm("opt-cardspic-layer-fx");
    check(lay.shown == LY_FX && cards_2x2() && cards_band_is(BAND_2X2) && OP_PY == OY_PANEL_2X2,
          "a layer held over the mixer takes the big values (its own screen)");
    frames(HOLD_FRAMES);                                /* (held past the tap's grace: no tap on the release) */
    release(B_FX);
    frames(3);
    check(ui.scr == SCR_HOME && lay.shown == LY_PLAY && !cards_2x2() && cards_band_is(BAND_1X4),
          "... let go: the mixer's 1x4 cards again");
    /* STEP: the grid on the whole panel; a held step's lines under the big values */
    check(SP_Y + SG_INFO_Y >= OY_CARD + OH_CARD_2X2 && SP_Y + SG_INFO_Y + SG_INFO_H <= SP_Y + SG_MK_Y,
          "2x2: STEP, a held step's two lines sit under the big values, over the grid's foot");
    fuzz(3000, 4242);                                   /* random use: every draw on the screen */
    check(1, "3000 frames of random use with the picture alone or the rows");
    tracks_default();                                   /* (what the random run changed goes) */
    reset_ui();
}
