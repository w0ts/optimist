/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_pages_test.c, the SLOOP UI: the drum track's ENV (the user, 2026-10-11: "most of the drum sounds have env
 * parameters decay, attack. put them on the env page"). ENV on the drum track opens the lane's ENV page (params.c,
 * SC_DSND in the ENV family): CLICK DECAY BEND LEVEL of the sound played last, the SOUND pages' own values; an edit
 * changes that lane's sound; a sound with LEVEL only (the X0X 909's rim shot) says so. Included after the helpers. */
#if DL_UI && FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
static const param_desc_t *denv_cell(uint32_t k)       /* the page's value k (0: none on this sound) */
{
    int16_t *vp;
    return page_desc(cur_page(), k, &vp);
}
static int16_t denv_val(uint32_t k)
{
    int16_t *vp = 0;
    return page_desc(cur_page(), k, &vp) && vp ? *vp : (int16_t)-999;
}
static void drum_env_tests(void)
{
    song.sel = TRK_DRUM; song.playing = 0; transport_req = 0; go_home(); frame();
    memset(&dl, 0, sizeof dl);
    TDRUM->p[P_E0] = DRUM_SAMPLED; frames(2);                   /* the 808: synthesised */
    key(key_of_white(2));                                       /* the snare played: the sound to edit */
    open_family(FAM_ENV); frames(2);
    check(cur_page()->scope == SC_DSND && cur_page()->fam == FAM_ENV && page_shown(cur_page()),
          "drum track, ENV tapped: the lane's ENV page");
    check(denv_cell(0) == &DSD[DE_CLICK] && denv_cell(1) == &DSD[DE_DECAY] && denv_cell(2) == &DSD[DE_BEND] &&
          denv_cell(3) == &DSD[DE_LEVEL], "drum ENV, the 808's snare: CLICK DECAY BEND LEVEL");
    {
        uint32_t i, synth_env = 0;
        for (i = 0; i < NPAGES; i++)
            synth_env |= PAGES[i].fam == FAM_ENV && PAGES[i].scope == SC_TRACK && page_shown(&PAGES[i]);
        check(!synth_env, "drum track: the synth's ENV pages not shown (no \"DRUM TRACK\" page on ENV)");
    }
    view_set(0); ui.force = 1; frame(); ppm("page-drum-env");
    tap(B_ENV); frames(2);
    check(cur_page()->scope == SC_DSND && cur_page()->fam == FAM_ENV, "ENV again: the same page (one ENV page here)");
    encs[panel.enc[EN_K2]] -= 10; frames(2);
    check(dl.ofs[2][DE_DECAY] < -5 && !dl.ofs[0][DE_DECAY], "drum ENV: KNOB 2 DECAY shorter, on the snare only");
    encs[panel.enc[EN_K1]] += 6; frames(2);
    check(dl.ofs[2][DE_CLICK] > 0, "drum ENV: KNOB 1 CLICK up on the snare");
    {   /* the sound the next hit plays (drum_edit.c de_synth): shorter, more click */
        int32_t semi;
        const dsnd_t *d = DS_SND(&DS_KITS[0], ds_lane(LANE_NOTE_OF(2), &semi));
        dsnd_t o;
        de_synth(&o, d, dl_ofs(2));
        check(o.decay < d->decay && o.click > d->click, "drum ENV: the snare's sound changed (decay shorter, click up)");
    }
    ui.force = 1; frame(); ppm("page-drum-env-edited");
    view_set(1); ui.force = 1; frame(); ppm("overview-drum-env");
    view_set(0);
    open_family(FAM_EDIT); frames(2);
    {
        uint32_t i;
        for (i = 0; i < 8u && cur_page()->id[0] != 0; i++)
            tap(B_EDIT);                                        /* (EDIT keeps the page last open: SOUND first) */
    }
    check(cur_page()->id[0] == 0 && denv_val(1) == dl.ofs[2][DE_DECAY], "SOUND: the same DECAY (the same storage)");
    key(key_of_white(0));                                       /* the kick played: ENV follows it */
    open_family(FAM_ENV); frames(2);
    check(dsnd_lane() == 0u && denv_val(1) == 0, "drum ENV follows the sound played (the kick: DECAY 0)");
#if FELUCCA_DRUM_SAMPLED
    TDRUM->p[P_E0] = DRUM_SFIRST; frames(2);                    /* a sampled kit: DECAY and LEVEL only */
    check(!denv_cell(0) && denv_cell(1) && !denv_cell(2) && denv_cell(3),
          "drum ENV, a sampled kit: DECAY LEVEL (no CLICK, no BEND)");
    ui.force = 1; frame(); ppm("page-drum-env-sampled");
    TDRUM->p[P_E0] = DRUM_SAMPLED; frames(2);
#endif
#if DRUM_X0X && FELUCCA_DRUM_KITS && FELUCCA_DRUM_X909
    dl.src[7] = (uint8_t)(DL_X909 + 5u);                        /* the 909's rim shot on lane 8: LEVEL only */
    pen_lane = 7; frames(2);
    check(!dsnd_has_env(7) && !denv_cell(0) && !denv_cell(1) && !denv_cell(2) && denv_cell(3),
          "drum ENV, the X0X 909's rim shot: no envelope, LEVEL only");
    ui.force = 1; frame(); ppm("page-drum-env-none");
    dl.src[7] = DL_KIT;
#endif
    song.sel = 0; frames(2);                                    /* a synth track: its own ENV again */
    check(cur_page()->scope == SC_TRACK && cur_page()->id[0] == P_ATK, "another track: its ENV (ATK DEC SUS REL)");
    memset(&dl, 0, sizeof dl);
    go_home(); frames(2);
    view_set(1);
}
#else
static void drum_env_tests(void) {}
#endif
