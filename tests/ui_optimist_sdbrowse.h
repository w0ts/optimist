/* SPDX-License-Identifier: GPL-3.0-only */
/* SD card mode's sample browser on the sound's own knob (ui/optimist/op_sdbrowse.c; the user, 2026-11-09: "when in sd
 * sample mode the k1 then scroll thru sample of the folder, opening the sample window (we click save to switch to other
 * source of sample). for both"): SAMPLE's SET and a drum lane's SRC showing SD. Included by ui_optimist_test.c after
 * ui_optimist_sdcard.h (its card: Samples/drums/kick.wav and "snare 2.wav", folders and a sample at Samples/) */
#if FELUCCA_SD_STREAM
static int sdb_find(const char *nm)                     /* the page's item named nm, -1 none */
{
    uint32_t k;
    for (k = 0; k < sdm_count(); k++)
        if (sdc_used(k) && !strcmp(sdc_name(k), nm))
            return (int)k;
    return -1;
}
static int sdb_hot(const char *label)                   /* the SOUND screen's card labelled label made hot: 1 found */
{
    uint32_t r, k;
    cell_t c;
    for (r = 0; r < SCREENS[SCR_SOUND].rows(); r++)
        for (k = 0; k < 4u; k++) {
            memset(&c, 0, sizeof c);
            SCREENS[SCR_SOUND].cell(r, k, &c);
            if (c.label && !strcmp(c.label, label)) {
                ui.row[SCR_SOUND] = (uint8_t)r;
                ui.hot = (uint8_t)k;
                return 1;
            }
        }
    return 0;
}
static const char *sdb_val(void)                        /* the hot card's value */
{
    static cell_t c;
    memset(&c, 0, sizeof c);
    SCREENS[SCR_SOUND].cell(ui.row[SCR_SOUND], ui.hot, &c);
    return c.val;
}
static void sdb_settle(void) { sdu_frames(12); }         /* (past SDC_LANE_SETTLE_MS: the item taken) */
static void sdbt_into(const char *dir)                   /* the popup up: into folder dir by its item and SAVE */
{
    int k = sdb_find(dir);
    if (k >= 0) {
        sdc.item = (uint8_t)k;
        tap(B_SAVE);
        sdu_frames(1);
    }
}
static void sdb_part_tests(void)
{
    uint32_t en;
    int k;
    song.sel = 1;
    set_engine_of(&trk[1], (uint32_t)ENG_SLOT_SAMPLE);
    trk[1].p[P_E0] = (int16_t)SMP_CARD;
    memset(&sdm_smp[1], 0, sizeof sdm_smp[1]);
    sdm_smp_dirty = 1;
    sdc_br.src = 0;
    sdu_frames(1);
    op_enter(SCR_SOUND);
    frame();
    check(sdb_hot("SET") && !strcmp(sdb_val(), "SD"), "browser: T2 on SAMPLE, SET on its last value SD (was CARD)");
    en = EN_K1 + ui.hot;
    turn(en, 1);
    check(sdc_br_on() && pop_kind == 6u && ui.scr == SCR_SOUND && trk[1].p[P_E0] == (int16_t)SMP_CARD,
          "SET's knob on SD: the samples' popup over the page, SET still SD");
    check(sdb_find("DRUMS") >= 0 && sdc_is_dir((uint32_t)sdb_find("DRUMS")), "... Samples: its folders and files");
    ppm("opt-sd-browser");
    k = sdb_find("AA PV.WAV");
    check(k > 0, "... AA PV.WAV (a real 0.5 s WAV) listed");
    if (k > 0) {                                        /* (the sample under the item heard: the preview voice's own output) */
        sdc.item = (uint8_t)(k - 1);
        turn(en, 1);
        sdb_settle();
        check(sdc_br_on() && sdc.item == (uint8_t)k && (sdu_pv_run(40), sst_pv.act) && sdu_pv_out() > 5000u,
              "stopped, the knob onto a sample: it plays from the card (the preview's voice), nothing written");
    }
    sdbt_into("DRUMS");
    {
        char t[24];
        sdm_folder(t, sizeof t);
        check(sdc_br_on() && !strcmp(t, "DRUMS") && sdc.item == 0u, "SAVE on a folder: into it, its first item");
    }
    ppm("opt-sd-browser-folder");
    turn(en, 1);
    sdb_settle();
    check(sdc_br_on() && sdc.item == 1u && !sdm_smp[1].hash && sdm_pv_busy() && !sdm_smp_dirty,
          "the knob: the next sample, settled: heard alone, nothing written yet (no flash write a sample)");
    turn(en, 1);
    turn(en, 1);
    sdb_settle();
    check(sdc.item == 1u && sdm_count() == 2u, "... past the last: it stays (no wrap)");
    turn(en, -1);
    sdb_settle();
    check(!sdm_smp[1].hash && sdc.item == 0u, "... back: KICK heard, still nothing taken");
    sdu_frames(200);
    check(!sdc_br_on() && !pop_kind && !strcmp(sdm_smp[1].hint, "drums/kick.wav") && trk[1].p[P_E0] == (int16_t)SMP_CARD &&
              trk[1].eng_req == (uint8_t)ENG_SLOT_SAMPLE,
          "left alone: the popup goes, the one heard last (KICK) T2's, SET still SD, SAMPLE kept");
    turn(en, 1);
    {
        char t[24];
        sdm_folder(t, sizeof t);
        check(sdc_br_on() && !strcmp(t, "DRUMS"), "the knob again: the popup on the sample's folder");
    }
    tap(B_SAVE);
    check(!sdc_br_on() && (sdc_br.src >> 1 & 1u) && sdu_msg("SOURCE"), "SAVE on a sample: the knob back to the source (SOURCE said)");
    turn(en, -1);
    check(!sdc_br_on() && trk[1].p[P_E0] != (int16_t)SMP_CARD, "... the knob now steps the source: SD left");
    turn(en, 1);
    check(!sdc_br_on() && trk[1].p[P_E0] == (int16_t)SMP_CARD && !(sdc_br.src >> 1 & 1u), "... back on SD: sample mode again");
    turn(en, 1);
    check(sdc_br_on(), "... the next turn: the samples");
    tap(B_HOME);
    sdu_frames(1);
    check(sdc_br_on() && sdb_find("DRUMS") >= 0, "HOME: up a folder");
    tap(B_HOME);
    sdu_frames(1);
    check(!sdc_br_on() && !pop_kind, "HOME at Samples: the popup gone");
    tap(B_SAVE);
    check(!sdc_br_on() && (sdc_br.src >> 1 & 1u), "SAVE on SET showing SD, popup down: source mode");
    tap(B_SAVE);
    check(sdc_br_on() && !(sdc_br.src >> 1 & 1u), "... SAVE again: the samples, the popup up");
    sdbt_into("DRUMS");
    k = sdb_find("SNARE 2.WAV");
    check(k >= 0, "... SNARE 2.WAV there");
    sdu_key((uint32_t)k);
    sdu_frames(1);
    check(sdc_br_on() && sdc.item == (uint8_t)k && sdm_pv_busy(), "a key: its item heard at once, the popup stays");
    tap(B_HOME);
    tap(B_HOME);
    sdu_frames(1);
    check(!sdc_br_on() && !strcmp(sdm_smp[1].hint, "drums/snare 2.wav"), "... HOME out: the one heard taken");
    trk[1].p[P_E0] = 0;
    sdc_br.src = 0;
    turn(en, 1);
    check(!sdc_br_on() && trk[1].p[P_E0] != 0, "SET on a set, not SD: the knob steps the sets");
    trk[1].p[P_E0] = (int16_t)SMP_CARD;
}
#if FELUCCA_SD_LANES
static void sdb_lane_tests(void)
{
    uint32_t l;
    cell_t c;
    while (!is_drum(TSEL))
        turn(EN_ALGO, 1);
    turn(EN_ALGO, 1);
    sdu_frames(1);
    check(lane_picked(), "browser: a drum lane picked");
    l = lane_selected() % DRUM_LANES;
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    ui.hot = 0;
    frame();
    lane_set_src(l, DL_KIT);
    memset(&sdm_smp[NPART + l], 0, sizeof sdm_smp[NPART + l]);
    sdc_br.src = 0;
    turn(EN_K1, 1);
    frame();
    check(dl.src[l] == DL_CARD && !sdc_br_on() && pop_kind == 3u && ui.toast_t, "the lane's SRC: KIT, then SD, in the standard list (no samples yet)");
    ppm("opt-sd-lane-src-list");
    memset(&c, 0, sizeof c);
    SCREENS[SCR_SOUND].cell(0, 0, &c);
    check(!strcmp(c.label, "SRC") && !strcmp(c.val, "SD"), "... its card reads SD");
    turn(EN_K1, 1);
    check(sdc_br_on() && pop_kind == 6u && dl.src[l] == DL_CARD, "on SD the knob: the samples' popup, SRC still SD");
    sdbt_into("DRUMS");
    sdb_settle();
    turn(EN_K1, 1);
    sdb_settle();
    check(dl.src[l] == DL_CARD && sdm_pv_busy(), "... the knob onto a sample, settled: heard");
    tap(B_SAVE);
    sdu_frames(1);
    check(!sdc_br_on() && (sdc_br.src >> (NPART + l) & 1u) && !strcmp(sdm_smp[NPART + l].hint, "drums/snare 2.wav"),
          "SAVE: the sample the lane's, the knob back to the source");
    memset(&c, 0, sizeof c);
    SCREENS[SCR_SOUND].cell(0, 0, &c);
    check(!strcmp(c.val, "SNARE 2"), "... SRC shows the sample's name");
    turn(EN_K1, -1);
    check(dl.src[l] == DL_KIT && pop_kind == 3u, "... the knob: KIT, in the standard list");
    while (is_drum(TSEL))
        turn(EN_ALGO, -1);
    song.sel = 0;
    sdu_frames(1);
}
#endif
static void sdb_tests(void)
{
    sdb_part_tests();
#if FELUCCA_SD_LANES
    sdb_lane_tests();
#endif
    sdc_br.src = 0;
    op_enter(SCR_HOME);
    frame();
}
#endif
