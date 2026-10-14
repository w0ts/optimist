/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the drum track's ENV (the user, 2026-10-11: "most of the drum sounds have env parameters
 * decay, attack. put them on the env page"): ENV on the drum track shows the selected lane's ENV row (params.c, SC_DSND
 * in the ENV family): CLICK DECAY BEND LEVEL, the SOUND rows' own values, with its envelope as the graph; an edit
 * changes that lane's sound; a sound with LEVEL only (the X0X 909's rim shot) says so. Included after the helpers. */
#if FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
static const char *denv_label(uint32_t k)              /* the ENV row's cell k label ("": none on this sound) */
{
    cell_t c;
    page_cell(snd_page(ui.row[SCR_SOUND]), k, &c);
    return c.label ? c.label : "";
}
static void drum_env_tests(void)
{
    uint32_t r, n, only;
    reset_ui();
    memset(&dl, 0, sizeof dl);
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;                      /* the 808: synthesised */
    lane_select(2);                                     /* the snare (HOME held + its key picks it) */
    ui.lane = 1;                                        /* (picked: the lane's ENV needs one, 2026-10-13) */
    frame();
    tap(B_ENV);
    n = SCR->rows();
    for (r = 0, only = 1; r < n; r++)
        only &= snd_page(r) && snd_page(r)->fam == FAM_ENV && snd_page(r)->scope == SC_DSND;
    check(ui.scr == SCR_SOUND && snd_fam == FAM_ENV && n == 1u && only, "the drum track, ENV: the lane's ENV row");
    check(!strcmp(denv_label(0), "CLICK") && !strcmp(denv_label(1), "DECAY") && !strcmp(denv_label(2), "BEND") &&
          !strcmp(denv_label(3), "LEVEL"), "drum ENV, the 808's snare: CLICK DECAY BEND LEVEL");
    check(snd_graph_page(0) == snd_page(0) && op_screen_btn() == B_ENV, "drum ENV: its own graph, the ENV button's screen");
    ui.force = 1;
    frame();
    ppm("opt-drum-env");
    turn(EN_K2, -10);
    check(dl.ofs[2][DE_DECAY] < -5 && !dl.ofs[0][DE_DECAY], "drum ENV: KNOB 2 DECAY shorter, on the snare only");
    turn(EN_K1, 6);
    check(dl.ofs[2][DE_CLICK] > 0, "drum ENV: KNOB 1 CLICK up on the snare");
    {   /* the sound the next hit plays (drum_edit.c de_synth): shorter, more click */
        int32_t semi;
        const dsnd_t *d = DS_SND(&DS_KITS[0], ds_lane(LANE_NOTE[2], &semi));
        dsnd_t o;
        de_synth(&o, d, dl_ofs(2));
        check(o.decay < d->decay && o.click > d->click, "drum ENV: the snare's sound changed (decay shorter, click up)");
    }
    ui.force = 1;
    frame();
    ppm("opt-drum-env-edited");
    lane_select(0);                                     /* another lane picked: its values */
    frame();
    {
        cell_t c;
        page_cell(snd_page(0), 1, &c);
        check(c.gv == 0 && dl.ofs[2][DE_DECAY] < -5, "drum ENV follows the lane picked (the kick: DECAY 0)");
    }
    tap(B_EDIT);
    for (r = 0, n = SCR->rows(), only = 0; r < n; r++)
        if (snd_page(r) && snd_page(r)->id[0] == 0u && snd_page(r)->scope == SC_DSND) {
            cell_t c;
            lane_select(2);
            page_cell(snd_page(r), 1, &c);
            only = c.gv == dl.ofs[2][DE_DECAY];
        }
    check(only, "SOUND: the same DECAY (the same storage)");
    tap(B_ENV);
#if FELUCCA_DRUM_SAMPLED
    TDRUM->p[P_E0] = DRUM_SFIRST;                       /* a sampled kit: DECAY and LEVEL only */
    frame();
    check(!denv_label(0)[0] && !strcmp(denv_label(1), "DECAY") && !denv_label(2)[0] && !strcmp(denv_label(3), "LEVEL"),
          "drum ENV, a sampled kit: DECAY LEVEL (no CLICK, no BEND)");
    ui.force = 1;
    frame();
    ppm("opt-drum-env-sampled");
    TDRUM->p[P_E0] = DRUM_SAMPLED;
#endif
#if DRUM_X0X && FELUCCA_DRUM_KITS && FELUCCA_DRUM_X909
    dl.src[7] = (uint8_t)(DL_X909 + 5u);                /* the 909's rim shot on lane 8: LEVEL only */
    lane_select(7);
    frame();
    check(!dsnd_has_env(7) && !denv_label(0)[0] && !denv_label(1)[0] && !denv_label(2)[0] &&
          !strcmp(denv_label(3), "LEVEL"), "drum ENV, the X0X 909's rim shot: no envelope, LEVEL only");
    ui.force = 1;
    frame();
    ppm("opt-drum-env-none");
    dl.src[7] = DL_KIT;
#endif
    song.sel = 0;                                       /* a synth track: its own envelopes */
    frame();
    tap(B_ENV);
    check(snd_fam == FAM_ENV && snd_page(0)->scope == SC_TRACK && snd_page(0)->id[0] == P_ATK,
          "a synth track, ENV: its envelopes, as before");
    memset(&dl, 0, sizeof dl);
    lane_select(0);
    reset_ui();
}
#else
static void drum_env_tests(void) {}
#endif
