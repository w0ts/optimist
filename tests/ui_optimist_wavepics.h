/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the ENV and LFO families always show their picture (the user, 2026-10-31: "the env and lfo
 * screens should always display the waveform below"): every SOUND row of ENV / ENV2 (the envelope) and of LFO / LFO 2
 * (the wave), the DEST, MOD and PHS rows too, a synth track's and a drum lane's, draws non-empty picture pixels in the
 * panel under the cards (the band OP_PY .. OP_FOOT_Y, the track's colour), and the picture follows PHS and WAVE.
 * Screens opt-wavepic-*. Included after ui_optimist_lfo2.h. */
static uint32_t wp_pixels(uint16_t col)                  /* pixels of colour col in the panel band under the cards */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = cards_2x2() ? OY_PANEL_2X2 : OY_PANEL; y < OP_FOOT_Y; y++)
        for (x = 0; x < 240; x++)
            n += swap16(screen[y * 240 + x]) == col;
    return n;
}
static uint32_t wp_hash(void)                            /* the panel band's pixels, hashed */
{
    uint32_t h = 2166136261u;
    int32_t i;
    for (i = (cards_2x2() ? OY_PANEL_2X2 : OY_PANEL) * 240; i < OP_FOOT_Y * 240; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static uint32_t wp_rows(uint32_t fam, uint32_t drum, uint32_t save)   /* every row of the family: how many lack the picture */
{
    uint32_t r, bad = 0, n, seen = 0;
    char nm[48];
    reset_ui();
    song.sel = drum ? 3u : 0u;
    ui.lane = drum ? 1u : 0u;                           /* (a lane picked, not DR itself: the lane's ENV and LFO) */
    tap(fam == FAM_ENV ? B_ENV : B_LFO);
    n = SCR->rows();
    for (r = 0; r < n; r++) {
        const page_t *p = snd_page(r);
        if (!p || p->fam != fam)
            continue;
        seen++;
        ui.row[SCR_SOUND] = (uint8_t)r;
        ui.force = 1;
        frames(2);
        if (!wp_pixels(trk_col(song.sel))) {
            bad++;
            printf("optimist ui: no picture on %s row %u (%s)\n", drum ? "drum" : "synth", r, p->title);
        }
        if (save) {
            snprintf(nm, sizeof nm, "opt-wavepic-%s-%s-%u", drum ? "drum" : "synth", fam == FAM_ENV ? "env" : "lfo", r);
            ppm(nm);
        }
    }
    printf("optimist ui: wavepics: %s %s rows seen %u\n", drum ? "drum" : "synth", fam == FAM_ENV ? "ENV" : "LFO", seen);
    return bad + !seen;
}
static void wavepics_tests(void)
{
    uint32_t h1, h2;
    uint32_t sv = getenv("WAVEPIC_SHOTS") != NULL;
    check(wp_rows(FAM_ENV, 0, sv) == 0, "ENV family (synth): every row draws the envelope");
    check(wp_rows(FAM_LFO, 0, sv) == 0, "LFO family (synth): every row draws the wave");
    check(wp_rows(FAM_ENV, 1, sv) == 0, "ENV family (drum lane): every row draws the envelope");
    check(wp_rows(FAM_LFO, 1, sv) == 0, "LFO family (drum lane): every row draws the wave");
    {   /* LFO 2's rows draw LFO 2's wave, LFO's rows LFO's: the picture follows the wave and phase on every row */
        uint32_t r, ok = 1;
        reset_ui();
        song.sel = 0;
        tap(B_LFO);
        TSEL->p[P_LWAVE] = 0, TSEL->p[P_L2WAVE] = 2, TSEL->p[P_LPHASE] = 0, TSEL->p[P_L2PHS] = 0;
        for (r = 0; r < SCR->rows(); r++) {
            const page_t *p = snd_page(r);
            if (!p || p->fam != FAM_LFO)
                continue;
            ui.row[SCR_SOUND] = (uint8_t)r;
            ui.force = 1;
            frames(2);
            h1 = wp_hash();
            TSEL->p[p->graph == GR_LFO2 || strstr(p->title, "2") ? P_L2PHS : P_LPHASE] = 32;
            ui.force = 1;
            frames(2);
            h2 = wp_hash();
            TSEL->p[P_L2PHS] = TSEL->p[P_LPHASE] = 0;
            ok &= h1 != h2;
        }
        check(ok, "LFO rows: the picture changes with the row's own LFO's PHS (LFO 2's rows draw LFO 2)");
    }
}
