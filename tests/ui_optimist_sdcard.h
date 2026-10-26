/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SD card mode (UI_SDCARD=1: FELUCCA_SD_UI on the card model; ui/optimist/op_sdcard.c,
 * storage/sd/sd_mode.c; the user, 2026-10-26: projects saved and loaded by name, every list as banks of 16 on the white
 * keys). Driven by the keys and the knobs, the card checked through FatFs:
 *   the boot: the card's folders made, a pending save finished, the samples indexed; no card: the flash row as before
 *   PROJECT: SAVE by name (NAME), the list, a SAVE over a project (its version), SAVES listed and loaded, LOAD over a
 *            changed work (SAVE <work>? first, HOME: the LOAD asked), DELETE; 20 projects: two pages, the knob, OCT
 *   PRESETS, KITS: SAVE / LOAD by name, a name of another file refused; SAMPLES: folders, into and up, USE by hash, RECENT
 *   a full card: the save kept in flash, finished at the next mount; a power cut at every block write of a save made
 *   from the UI: the next boot finishes it, the card's project the staged one; the card pulled: said, the flash row */
static uint8_t *sdu_img;
static uint32_t sdu_sectors;
static void sdu_frames(uint32_t n)                      /* frames, the main loop's card pass after each (main.c sdm_poll) */
{
    while (n--) {
        frame();
        sdm_poll();
    }
}
static uint32_t sdu_row(uint32_t kind)                  /* PROJECT's row of that kind, the cursor on it */
{
    uint32_t r;
    op_enter(SCR_PROJECT);
    for (r = 0; r < NPRJ && prj_kind(r) != kind; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)(r % NPRJ);
    sdu_frames(2);
    return r;
}
static const char *sdu_val(uint32_t k)
{
    static cell_t c;
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], k, &c);
    return c.val;
}
static const char *sdu_label(uint32_t k)
{
    static cell_t c;
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], k, &c);
    return c.label ? c.label : "";
}
static void sdu_key(uint32_t w) { kdown(WK(w)); kup(WK(w)); sdm_poll(); }
static void sdu_name(const char *s)                     /* NAME open: the name typed (NAME's typing: its own tests), SAVE */
{
    str_cpy(nm.s, s, sizeof nm.s);
    nm.len = nm.cur = (uint8_t)str_len(nm.s);
    nm.key = 0;
    tap(B_SAVE);
    sdm_poll();
}
static void sdu_op(uint32_t op)                         /* ACTION: LOAD first, the knob a detent a step to op */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        turn(EN_K2, -1);
    for (i = 0; i < op; i++)
        turn(EN_K2, 1);
}
static int sdu_has(const char *path) { FILINFO fi; return f_stat(path, &fi) == FR_OK; }
static int sdu_msg(const char *m) { return strstr(ui.msg, m) != 0; }
static uint64_t sdu_stage_hash(void)
{
    uint64_t h = 0;
    (void)sdp_stage_hash(sdm_stage_len(), sd_blk, &h);
    return h;
}
static int sdu_proj_ok(const char *name, uint32_t number, uint64_t hash)   /* the card's project: whole, that save */
{
    sdp_head h;
    char p[FF_LFN_BUF + 1];
    snprintf(p, sizeof p, "OPTIMIST/PROJECTS/%s/%s.OPP", name, name);
    return sdp_verify_path(p, &h, sd_blk) == FR_OK && (!number || h.number == number) && (!hash || h.hash == hash);
}
static void sdu_card(void)                              /* a fresh card in, the boot's mount and pass */
{
    card(sdu_img, sdu_sectors, 2048, 1, 1, 0);
    (void)sd_mount();
    sdm_st = SDM_OFF;                                   /* (the boot: the device record read again) */
    sdu_frames(3);
}
#if FELUCCA_SD_STREAM && FELUCCA_SLICE
/* USE on a part on SLICE (sd_mode.c): it stays on SLICE, SRC CARD; the plan reads the file whole into its block
 * (sd_slice.c) and SLICE's table is the file's: four 100 ms slices, a 25 ms tone at the start of each */
static void sdu_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void sdu_le32(uint8_t *p, uint32_t v) { sdu_le16(p, v); sdu_le16(p + 2, v >> 16); }
static void sdu_slice_use(void)
{
    static const uint32_t hz[4] = {220, 441, 882, 1764};
    uint8_t h[44], d[2];
    uint32_t i, n, at, idx;
    FIL f;
    UINT k;
    memcpy(h, "RIFF", 4); sdu_le32(h + 4, 36u + 4u * 4410u * 2u); memcpy(h + 8, "WAVEfmt ", 8); sdu_le32(h + 16, 16);
    sdu_le16(h + 20, 1); sdu_le16(h + 22, 1); sdu_le32(h + 24, 44100); sdu_le32(h + 28, 88200); sdu_le16(h + 32, 2); sdu_le16(h + 34, 16);
    memcpy(h + 36, "data", 4); sdu_le32(h + 40, 4u * 4410u * 2u);
    check(f_open(&f, "OPTIMIST/SAMPLES/loop.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_write(&f, h, 44, &k) == FR_OK,
          "SLICE USE: a WAV of four tones on the card");
    for (i = 0; i < 4u * 4410u; i++) {
        double x = i % 4410u < 1100u ? 12000.0 * sin(2.0 * M_PI * hz[i / 4410u] * (i % 4410u) / 44100.0) : 0.0;
        sdu_le16(d, (uint16_t)(int16_t)x);              /* (25 ms of tone, then silence: four hits) */
        f_write(&f, d, 2, &k);
    }
    f_close(&f);
    sdm_again();
    sdu_frames(3);
    sdu_row(PR_CSMP);
    turn(EN_K4, -1);                                    /* (VIEW: from RECENT back to the folders) */
    turn(EN_K4, -1);
    for (idx = 0, n = sdm_count(); idx < n && strcmp(sdc_name(idx), "loop.wav"); idx++)
        ;
    check(idx < n, "SLICE USE: loop.wav listed");
    set_engine_of(&trk[2], ENG_SLOT_SLICE);
    sdu_frames(1);
    check(trk[2].engine == (uint8_t)ENG_SLOT_SLICE, "SLICE USE: T3 on SLICE");
    song.sel = 2;
    sdu_key(idx);
    sdu_key(idx);                                       /* (the second press uses it) */
    check(trk[2].eng_req == (uint8_t)ENG_SLOT_SLICE && trk[2].p[P_E0] == (int16_t)SLC_SRC_CARD && sst_part_hash[2] &&
          sst_part_hash[2] == sdm_smp[2].hash, "USE on a SLICE part: it stays on SLICE, SRC CARD, the sample to the plan");
    for (i = 0; i < 6u && !slc_crd[2]; i++) {           /* (the gate's two halves, then placed) */
        clk += 30u * SD_TICKS_MS;
        sst_plan_poll();
    }
    check(sstp.state == SSTP_PLACED && slc_crd[2] && slc_crd[2]->len == 4u * 4410u && sst_part_src[2] == 0xFFu,
          "... placed whole: SLICE's table the file's 17640 samples, no stream for it");
    trk[2].p[P_E1] = 0;                                 /* DIV 4 */
    trk[2].p[P_ROOT] = 0;
    trk[2].p[P_E2] = 0;
    for (i = 0; i < 4u && slc_crd[2]; i++) {
        uint32_t a, b, st;
        slc_bounds(slc_crd[2], 0, i, &a, &b, &st);
        check(a == i * 4410u && b == (i + 1u) * 4410u, "... DIV 4: each slice one tone");
    }
    at = slc_crd[2] ? slc_count(slc_crd[2], SLC_DIV_AUTO) : 0;
    check(at == 4u, "... AUTO finds the four tones' starts");
    set_engine_of(&trk[2], ENG_SLOT_SAMPLE);           /* (back as the tests after it expect) */
    sdu_frames(1);
    for (i = 0; i < 6u && slc_crd[2]; i++) {
        clk += 30u * SD_TICKS_MS;
        sst_plan_poll();
    }
    check(!slc_crd[2], "T3 off SLICE: its whole copy let go");
    sdu_row(PR_CSMP);
    turn(EN_K4, 1);                                     /* (VIEW RECENT again, as the tests after it found it) */
    turn(EN_K4, 1);
    song.sel = 1;
}
#endif

static void sdcard_tests(void)
{
    uint32_t i, r;
    char nm2[64];
    FIL f;
    UINT k;
    uint64_t h1;
    reset_ui();
    /* ---- the boot: the folders, the index */
    card(sdu_img, sdu_sectors, 2048, 1, 1, 0);
    check(sd_mount() == SD_READY, "SD: the card mounts (the model, FAT32)");
    {   /* samples a computer put there before: indexed at the boot's pass */
        f_mkdir("OPTIMIST");
        f_mkdir("OPTIMIST/SAMPLES");
        f_mkdir("OPTIMIST/SAMPLES/drums");
        if (f_open(&f, "OPTIMIST/SAMPLES/drums/kick.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFkick-data-one", 17, &k), f_close(&f);
        if (f_open(&f, "OPTIMIST/SAMPLES/snare.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFsnare-data-two", 18, &k), f_close(&f);
    }
    sdm_st = SDM_OFF;
    sdu_frames(4);
    check(sdm_ready() && sdu_has("OPTIMIST/PROJECTS") && sdu_has("OPTIMIST/PRESETS") && sdu_has("OPTIMIST/KITS") &&
          sdu_has("OPTIMIST/SYSTEM/INDEX.BIN"), "SD boot: the card ready, its folders made, the sample index written");
    check(sdx_open() == FR_OK && sdx.count == 2u, "SD boot: the two samples indexed");
    /* ---- PROJECT: the card's list */
    r = sdu_row(PR_PROJECT);
    check(sdc_listing() && !cards_2x2(), "PROJECT, a card: the card's list, four small cards");
    check(!strcmp(sdu_label(0), "PAGE") && !strcmp(sdu_val(0), "1") && !strcmp(sdu_label(1), "ACTION") &&
          !strcmp(sdu_val(1), "LOAD") && !strcmp(sdu_label(2), "ITEM") && !strcmp(sdu_label(3), "VIEW") &&
          !strcmp(sdu_val(3), "NAME"), "PROJECT, a card: PAGE 1, ACTION LOAD, ITEM, VIEW NAME");
    check(sdm_count() == 0u, "PROJECT, a fresh card: no project");
    sdu_key(0);
    check(!op_armed() && !name_on() && sdu_msg("EMPTY"), "LOAD on an empty slot: EMPTY, no question");
    /* SAVE by name */
    trk[0].p[P_DEC] = 11;
    sdu_op(SDO_SAVE);
    check(!strcmp(sdu_val(1), "SAVE"), "ACTION knob: SAVE");
    sdu_key(0);
    check(name_on() && nm.kind == NK_CARD, "SAVE on an empty slot: NAME at once");
    sdu_name("MY SONG");
    h1 = sdu_stage_hash();
    check(!name_on() && sdu_msg("SAVED TO CARD"), "NAME done: SAVED TO CARD");
    check(sdu_proj_ok("MY SONG", 1, h1), "the card's MY SONG/MY SONG.OPP: whole, save 1, the flash copy's bytes");
    {
        char b[16];
        sec_name(0, b);
        check(!strcmp(b, "MY SONG"), "the flash project is named as the card's");
    }
    sdu_frames(2);
    check(sdm_count() == 1u && !strcmp(sdc_name(0), "MY SONG"), "the list: MY SONG on slot 1");
    ppm("opt-sd-projects");
    {
        uint32_t fl = 0, dm = 0;
        sdc_keys_lit(&fl, &dm, 1);
        check(fl == 1u << white_key(0) && !dm, "the keys: slot 1 lit");
    }
    check(!strcmp(sdu_val(1), "LOAD"), "after the save: LOAD again");
    /* SAVE over it: asked (red), NAME prefilled, a version kept */
    trk[0].p[P_DEC] = 22;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "SAVE MY SONG?"), "SAVE over MY SONG: asked, red");
    tap(B_SAVE);
    check(name_on() && !strcmp(nm.s, "MY SONG"), "YES: NAME, prefilled MY SONG");
    sdu_name("MY SONG");
    check(sdu_proj_ok("MY SONG", 2, sdu_stage_hash()) && sdu_has("OPTIMIST/PROJECTS/MY SONG/VERSIONS/MY SONG-0001.OPP"),
          "saved again: save 2, save 1 in VERSIONS");
    /* SAVES: the versions, loaded */
    sdu_frames(1);
    turn(EN_K4, 1);
    check(!strcmp(sdu_val(3), "SAVES") && sdm_count() == 1u && !strcmp(sdc_name(0), "VERSION 1"),
          "VIEW SAVES: MY SONG's version 1");
    {
        char t[16];
        prj_name(ui.row[SCR_PROJECT], t);
        check(!strcmp(t, "MY SONG"), "SAVES: the title is the project's name");
    }
    trk[0].p[P_DEC] = 33;
    sdu_key(0);
    check(op_armed() && !strcmp(ui.arm_verb, "SAVE?"), "LOAD over a changed work: SAVE MY SONG? first");
    tap(B_HOME);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "LOAD VERSION 1?"), "HOME: the LOAD asked, red");
    tap(B_SAVE);
    sdm_poll();
    check(trk[0].p[P_DEC] == 11 && sdu_msg("LOADED"), "YES: version 1 loaded (the work as saved first)");
    tap(B_HOME);
    check(ui.scr == SCR_PROJECT && !strcmp(sdu_val(3), "NAME"), "HOME on SAVES: back to the projects");
    /* LOAD the project, "SAVE <work>?" YES: saved, then loaded */
    trk[0].p[P_DEC] = 44;
    sdu_key(0);
    check(op_armed() && !strcmp(ui.arm_verb, "SAVE?"), "LOAD MY SONG, the work changed: SAVE MY SONG? first");
    tap(B_SAVE);
    sdm_poll();
    check(trk[0].p[P_DEC] == 44 && sdu_proj_ok("MY SONG", 3, 0) &&
          sdu_has("OPTIMIST/PROJECTS/MY SONG/VERSIONS/MY SONG-0002.OPP"), "YES: saved (save 3), then loaded (the same work)");
    /* DELETE */
    sdu_op(SDO_DELETE);
    sdu_key(0);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "DELETE MY SONG?"), "DELETE: asked, red");
    tap(B_SAVE);
    sdu_frames(1);
    check(!sdu_has("OPTIMIST/PROJECTS/MY SONG") && sdm_count() == 0u, "YES: the project's folder gone, the list empty");
    /* 20 projects: two pages */
    for (i = 0; i < 20u; i++) {
        snprintf(nm2, sizeof nm2, "P%02u", (unsigned)i);
        (void)sdm_save(SDC_PROJ, nm2, "");
    }
    sdu_row(PR_PROJECT);
    check(sdm_count() == 16u && !strcmp(sdu_val(0), "1") && !strcmp(sdc.unit, "/2") && !strcmp(sdc_name(0), "P00"),
          "20 projects: page 1 of 2, P00 first");
    turn(EN_K1, 1);
    check(sdm_page() == 1u && sdm_count() == 4u && !strcmp(sdc_name(0), "P16"), "PAGE knob: page 2, P16..P19");
    tap(B_OCTDN);
    check(sdm_page() == 0u, "OCT- tapped: page 1");
    sdc.item = 15;
    turn(EN_K3, 1);
    check(sdm_page() == 1u && sdc.item == 0u, "ITEM knob past slot 16: the next page's first");
    tap(B_OCTDN);
    ppm("opt-sd-page1");
    tap(B_OCTUP);
    sdu_key(2);
    check(op_armed() && !strcmp(ui.arm_q, "LOAD P18?"), "key 3 on page 2: LOAD P18?");
    tap(B_SAVE);
    sdm_poll();
    {
        char b[16];
        sec_name(0, b);
        check(!strcmp(b, "P18") && sdu_msg("LOADED"), "YES: P18 loaded, the flash project named P18");
    }
    /* ---- PRESETS: SAVE / LOAD by name */
    sdu_row(PR_CPRE);
    check(sdc_listing() && sdm_count() == 0u && !strcmp(sdu_val(3), "FOLDER"), "PRESETS: the card's, VIEW FOLDER, empty");
    song.sel = 0;
    trk[0].p[P_DEC] = 5;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    check(name_on(), "PRESETS SAVE: NAME");
    sdu_name("LEAD");
    check(upc_takes == 1u && sdu_has("OPTIMIST/PRESETS/LEAD.OPS") && sdu_msg("SAVED TO CARD"), "PRESETS: LEAD.OPS written");
    sdu_frames(1);
    check(sdm_count() == 1u && !strcmp(sdc_name(0), "LEAD"), "the list: LEAD (no extension)");
    sdu_op(SDO_LOAD);
    sdu_key(0);
    tap(B_SAVE);
    check(upc_gives == 1u && upc_rec[0] == (uint8_t)5u && !strcmp((char *)upc_rec + 5, "LEAD"), "LOAD LEAD: its record given");
    sdu_key(0);
    check(op_armed() && strstr(ui.arm_q, "LEAD") && upc_gives == 1u, "a key: LOAD LEAD? asked, nothing loaded yet (one press shows it)");
    sdu_key(0);
    check(!op_armed() && upc_gives == 2u, "the same key again: loaded (a second press loads)");
    sdu_key(0);
    sdu_key(5);
    check(!op_armed() && upc_gives == 2u && sdu_msg("EMPTY"), "another key: about that one instead (an empty slot: EMPTY), LEAD not loaded");
    sdu_op(SDO_SAVE);
    sdu_key(1);
    sdu_name("LEAD");
    check(name_on() && sdu_msg("NAME TAKEN"), "SAVE a new preset as LEAD: NAME TAKEN, NAME stays");
    sdu_name("BAD/NAME");
    check(name_on() && sdu_msg("NAME NOT USABLE"), "a / in a name: NAME NOT USABLE");
    name_close();
    /* ---- KITS */
#if FELUCCA_DRUM_KITS
    sdu_row(PR_CKIT);
    dl.ofs[0][0] = 7;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    sdu_name("ROOM KIT");
    check(sdu_has("OPTIMIST/KITS/ROOM KIT.OPK"), "KITS: ROOM KIT.OPK written");
    dl.ofs[0][0] = 0;
    sdu_frames(1);
    sdu_op(SDO_LOAD);
    sdu_key(0);
    tap(B_SAVE);
    check(dl.ofs[0][0] == 7 && sdu_msg("LOADED"), "LOAD ROOM KIT: the lanes back");
#endif
    /* ---- SAMPLES: folders, USE by hash, RECENT */
    sdu_row(PR_CSMP);
    check(sdm_count() == 2u && sdc_is_dir(0) && !strcmp(sdc_name(0), "drums") && !strcmp(sdc_name(1), "snare.wav"),
          "SAMPLES: the folder first, then the files");
    check(!strcmp(sdu_val(1), "USE"), "SAMPLES: ACTION USE");
    ppm("opt-sd-samples");
    sdu_key(0);
    {
        char t[16];
        prj_name(ui.row[SCR_PROJECT], t);
        check(!op_armed() && sdm_count() == 1u && !strcmp(sdc_name(0), "kick.wav") && !strcmp(t, "drums"),
              "a folder's key: into it, no question, the title its name");
    }
    tap(B_HOME);
    check(ui.scr == SCR_PROJECT && sdm_count() == 2u, "HOME: up a folder");
    song.sel = 1;
    sdu_key(1);
    check(op_armed() && !ui.arm_danger && !strcmp(ui.arm_q, "USE snare.wav?") && !sdm_smp[1].hash,
          "a sample's key: USE snare.wav? asked, nothing used yet (one press shows it)");
    sdu_key(1);
    check(!op_armed(), "the same key again: YES"); printf("DBG msg=[%s] hash=%llx op=%u\n", ui.msg, (unsigned long long)sdm_smp[1].hash, prj_arm_op);
    {
        sdx_rec rec;
        int32_t no = (sdx_open(), sdx_find_path("SAMPLES/snare.wav", &rec));
        uint64_t hh;
        const char *hint;
        check(no >= 0 && sdm_sample_of(1, &hh, &hint) && hh == rec.hash && !strcmp(hint, "snare.wav") && slg_has(SEC_ID_SDSMP),
              "USE snare.wav on T2: its hash and hint in the project");
        check(sdm_sample_path(1, nm2, sizeof nm2) && !strcmp(nm2, "OPTIMIST/SAMPLES/snare.wav"), "... found again by its hash");
    }
#if FELUCCA_SD_STREAM
    check(sst_part_hash[1] == sdm_smp[1].hash && sdm_smp[1].hash, "USE (streaming built): the part's CARD sample given to the plan");
    sst_part_set(1, 0);
    sdm_smp_key = 0;                                    /* (the project's record read again: a load, a boot) */
    sdm_smp_sync();
    check(sst_part_hash[1] == sdm_smp[1].hash, "... and again from the project's record");
#endif
    turn(EN_K4, 1);
    turn(EN_K4, 1);
    check(!strcmp(sdu_val(3), "RECENT") && sdm_count() == 1u && !strcmp(sdc_name(0), "snare.wav"), "VIEW RECENT: the one used");
    f_rename("OPTIMIST/SAMPLES/snare.wav", "OPTIMIST/SAMPLES/drums/snare 2.wav");
    sdm_again();
    sdu_frames(3);
    check(sdm_sample_path(1, nm2, sizeof nm2) && !strcmp(nm2, "OPTIMIST/SAMPLES/drums/snare 2.wav"),
          "renamed and moved on the card: found by its hash");
#if FELUCCA_SD_STREAM && FELUCCA_SLICE
    sdu_slice_use();
#endif
    song.sel = 0;
    /* ---- SYSTEM > SD CARD: VERSIONS, SLACK, kept in the log's device record */
    op_enter(SCR_SYSTEM);
    for (r = 0; r < sys_rows(); r++) {
        char t[16];
        sys_name(r, t);
        if (!strcmp(t, "SD CARD"))
            break;
    }
    check(r < sys_rows(), "SYSTEM: an SD CARD page");
    if (r < sys_rows()) {
        cell_t c;
        ui.row[SCR_SYSTEM] = (uint8_t)r;
        sdu_frames(2);
        sys_cell(r, 0, &c);
        check(!strcmp(c.label, "CARD") && !strcmp(c.val, "READY"), "SD CARD: CARD READY");
        sys_turn(r, 1, 1, 0);
        sys_turn(r, 2, 1, 0);
        check(sdp_keep == 9u && sda_scale == 125u, "VERSIONS +1: 9 kept; SLACK +1: 125 %");
        op_enter(SCR_HOME);
        check(sdm_dirty == 1u, "SYSTEM left: the settings due (written once quiet)");
        sdu_frames(1);
        memset(&sdm_rec, 0, sizeof sdm_rec);
        sdm_rec_read();
        check(sdm_keep() == 9u && sdm_scale() == 125u, "SYSTEM left: the settings in the log (read again)");
        sdm_set(0, 0);
        sdm_set(1, 0);
        sdm_leave();
        sdu_frames(1);
        check(sdp_keep == 8u && sda_scale == 100u, "SHIFT + the knob: 8 versions, 100 %");
#if FELUCCA_SD_STREAM
        sys_cell(r + 1u, 0, &c);
        check(!strcmp(c.label, "PRE-BUFFER") && !strcmp(c.val, "50") && strstr(c.unit, "KB"), "SD CARD page 2: PRE-BUFFER 50 ms, its KB");
        sys_turn(r + 1u, 0, 1, 0);
        sys_turn(r + 1u, 0, 1, 0);
        sys_turn(r + 1u, 0, 1, 0);
        check(sst_pre_ms == 200u, "PRE-BUFFER knob: 100, 200, stops at 200");
        sdm_leave();                                    /* (SYSTEM left) */
        sdu_frames(1);
        memset(&sdm_rec, 0, sizeof sdm_rec);
        sst_pre_set(50);
        sdm_rec_read();
        check(sst_pre_ms == 200u, "... kept in the log's device record");
        sdm_set(2, 0);
        sdm_leave();
        sdu_frames(1);
        check(sst_pre_ms == 50u, "SHIFT + the knob: 50 ms");
        /* the streams' memory short (sd_stream_plan.c refused): MEM FULL CARD, a part's drop asked, then the pre-buffer that fits */
        sstp.state = SSTP_REFUSED;
        sstp.said = 0;
        sstp.drop = 1u << 2;
        sstp.fit_ms = 25;
        sdu_frames(1);
        check(op_armed() && ui.arm_danger && !strncmp(ui.arm_q, "DROP T3", 7), "MEM FULL CARD: DROP T3 asked, red");
        tap(B_HOME);
        sdu_frames(1);
        check(!op_armed() && sdu_msg("25 MS FITS"), "NO: no other part frees enough: the pre-buffer that fits said");
        sdu_frames(3);
        check(!op_armed(), "... said once");
        sstp.state = SSTP_IDLE;
        sdu_frames(1);
#endif
    }
    /* ---- a full card: the save kept in flash, finished at the next mount */
    sdu_row(PR_PROJECT);
    {
        FIL big;
        FATFS *fs;
        DWORD fr = 0;
        FRESULT rr = f_getfree("", &fr, &fs);
        rr = rr == FR_OK ? f_open(&big, "FILLER.BIN", FA_CREATE_ALWAYS | FA_WRITE) : rr;
        if (rr == FR_OK) {                              /* (the free room all taken but a cluster, in whatever runs) */
            rr = f_lseek(&big, (FSIZE_t)(fr - 1u) * fs->csize * 512u);
            f_close(&big);
        }
        trk[0].p[P_DEC] = 55;
        (void)sdm_save(SDC_PROJ, "FULL ONE", "");
        check(rr == FR_OK && sdu_msg("CARD FULL") && sdm_rec.pend == 1u, "a full card: CARD FULL, the save pending (in flash)");
        f_unlink("FILLER.BIN");
        sdm_again();
        sdu_frames(3);
        check(!sdm_rec.pend && sdu_proj_ok("FULL ONE", 1, sdu_stage_hash()) && sdu_msg("CARD SAVE FINISHED"),
              "room again, the next mount: the pending save finished");
    }
    /* a pending save of another project than the flash one now (NEW, a LOAD since): dropped, never finished with it */
    sdm_rec.pend = 1;
    str_cpy(sdm_rec.p.name, "SOMEONE ELSE", sizeof sdm_rec.p.name);
    sdm_again();
    sdu_frames(3);
    check(!sdm_rec.pend && sdu_msg("CARD SAVE DROPPED") && !sdu_has("OPTIMIST/PROJECTS/SOMEONE ELSE"),
          "a pending save of another project: dropped, nothing written");
    /* ---- the card pulled: said, the flash row back, the card's rows say so */
    C.present = 0;
    (void)sdm_open(SDC_PROJ, SDV_FOLDER, "");        /* (a read: the card gone) */
    sd_state = SD_NOCARD;
    sdu_frames(1);
    check(sdu_msg("SD CARD GONE") && !sdc_listing() && cards_2x2(), "the card pulled: SD CARD GONE, PROJECT the flash row");
    sdu_row(PR_CPRE);
    check(!strcmp(sdu_label(0), "CARD") && !strcmp(sdu_val(0), "NONE"), "PRESETS without a card: CARD NONE");
    sdu_key(0);
    check(!op_armed() && !name_on(), "... its keys do nothing");
    /* ---- no card at the boot: said once, the flash store */
    sd_state = SD_NOCARD;
    sdm_st = SDM_OFF;
    sdu_frames(1);
    check(sdu_msg("NO SD CARD") && sdm_st == SDM_NONE, "no card at the boot: NO SD CARD");
    C.present = 1;
    card_power_on();
    sdu_row(PR_PROJECT);
    sdu_frames(3);
    check(sdm_ready() && sdc_listing(), "the card back: entering PROJECT mounts it, the card's list");
}
/* a power cut at every block write of a SAVE made from the UI: the next boot (the device record read again, the card
 * mounted) finishes it from the flash copy; the card's project then the staged one, the record cleared */
static void sdcard_cut_tests(void)
{
    uint8_t *snap = malloc((size_t)sdu_sectors * 512u);
    long total, w0, kk;
    int unfinished = 0, wrong = 0, said = 0;
    uint64_t want;
    sdu_card();
    sdu_row(PR_PROJECT);
    trk[0].p[P_DEC] = 61;
    (void)sdm_save(SDC_PROJ, "CUT ME", "");
    trk[0].p[P_DEC] = 62;
    memcpy(snap, sdu_img, (size_t)sdu_sectors * 512u);
    w0 = C.writes;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    tap(B_SAVE);
    sdu_name("CUT ME");
    total = C.writes - w0;
    want = sdu_stage_hash();
    check(total > 10 && sdu_proj_ok("CUT ME", 2, want), "a save from the UI (uncut): save 2 on the card");
    for (kk = 0; kk < total; kk++) {
        memcpy(sdu_img, snap, (size_t)sdu_sectors * 512u);
        card_power_on();
        sdu_card();
        sdu_row(PR_PROJECT);
        C.cut_at = C.writes + kk;
        sdu_op(SDO_SAVE);
        sdu_key(0);
        tap(B_SAVE);
        sdu_name("CUT ME");
        said += sdu_msg("CARD SAVE FAILED") || sdu_msg("SD CARD GONE");   /* (the cut card: gone, its next pass says) */
        if (!sdm_rec.pend)
            unfinished++;                               /* (cut, the record already cleared: the boot would not finish it) */
        card_power_on();                                /* the power back: a boot */
        memset(&sdm_rec, 0, sizeof sdm_rec);
        (void)sd_mount();
        sdm_st = SDM_OFF;
        sdu_frames(3);
        if (sdm_rec.pend || !sdu_proj_ok("CUT ME", 2, want))
            wrong++;
    }
    check(unfinished == 0 && said == (int)total, "a cut at each of the save's block writes: said, the save left pending");
    check(wrong == 0, "... the next boot finishes it: the card's project the staged one, the record cleared");
    memcpy(sdu_img, snap, (size_t)sdu_sectors * 512u);
    card_power_on();
    free(snap);
}

#if FELUCCA_SD_REC
/* the card recorder on REC (op_input.c crec_*; the user, 2026-10-26: "short plus long click on rec"): a tap, then REC
 * pressed again at once and held, starts a take (the tap's arm put back), the same again stops it; a quick second tap
 * is a plain REC press; REC flashes while the card records */
static void sdu_rec_take(void)                          /* tap, press, held past CR_HOLD_MS, let go */
{
    tap(B_REC);
    press(B_REC);
    frames(CR_HOLD_MS / 16u + 2u);
    release(B_REC);
}
static void sdu_rec_service(void)                       /* the main loop's passes: the recorder's blocks */
{
    uint32_t i;
    for (i = 0; i < 64u && sdr_service(); i++)
        ;
}
static void sdu_rec_tests(void)
{
    uint8_t rec0;
    sdu_card();
    op_enter(SCR_HOME);
    frames(2);
    rec0 = song.rec;
    sdu_rec_take();
    check(sdr_busy() && sdu_msg("CARD REC") && song.rec == rec0, "REC tap + held: a card take, the tap's arm put back");
    check(!rh.ring, "... the held press starts no clear ring");
    sdu_rec_service();
    frames(4);
    sdu_rec_take();
    sdu_rec_service();
    check(sdr_state == RC_DONE && sdu_msg("CARD STOP") && song.rec == rec0, "the same again: the take stopped, closed");
    check(sdu_has("OPTIMIST/RECORD/REC-0001.WAV"), "... its file: OPTIMIST/RECORD/REC-0001.WAV");
    frames(30);                                          /* (past the gap: a fresh gesture) */
    tap(B_REC);
    tap(B_REC);
    frames(2);
    check(!sdr_busy() && song.rec == rec0, "two quick taps: two plain REC presses (armed, then not), no take");
    sdu_rec_take();
    check(sdr_busy() && sdu_msg("REC-0002"), "a second take: REC-0002");
    sdu_rec_take();
    sdu_rec_service();
    check(sdr_state == RC_DONE && sdu_has("OPTIMIST/RECORD/REC-0002.WAV"), "... stopped, its file");
}
#endif

#if FELUCCA_ENG_FM6
/* DX7 banks on the card (sd_mode.c sdm_dx7_*; the user, 2026-10-26: the FM6 collections): a 32-voice .SYX under
 * PRESETS is a folder of its voices, two pages of 16; a voice's LOAD puts it into the part on FM6 */
static void sdu_dx7_tests(void)
{
    static uint8_t f[4104];
    FIL fp;
    UINT w;
    uint32_t i, j, ok;
    uint8_t pk[128];
    sdu_card();
    f[0] = 0xF0, f[1] = 0x43, f[2] = 0x00, f[3] = 0x09, f[4] = 0x20, f[5] = 0x00, f[4103] = 0xF7;
    for (i = 0; i < 32u; i++) {                          /* (real voices: part 1's, each operator's level its own) */
        int16_t ed[sizeof fm6_ed[0] / sizeof fm6_ed[0][0]];
        uint8_t *v = f + 6u + i * 128u;
        memcpy(ed, fm6_ed[0], sizeof ed);
        for (j = 0; j < 6u; j++)
            ed[j * FO_N + FO_OL] = (int16_t)((i * 3u + j * 11u) % 100u);
        ed[FV_ALG] = (int16_t)(i % 32u);
        fm6_pack(v, ed);
        memcpy(v + 118, "VOICE     ", 10);
        v[124] = (uint8_t)('0' + i / 10u), v[125] = (uint8_t)('0' + i % 10u);
    }
    f_mkdir("OPTIMIST/PRESETS/DX7");
    check(f_open(&fp, "OPTIMIST/PRESETS/DX7/BANK1.syx", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK &&
          f_write(&fp, f, sizeof f, &w) == FR_OK && w == sizeof f && f_close(&fp) == FR_OK, "DX7: a bank written by a computer");
    sdu_row(PR_CPRE);
    sdu_op(SDO_LOAD);
    for (i = 0; i < sdm_count() && strcmp(sdc_name(i), "DX7"); i++)
        ;
    sdu_key(i);
    check(sdm_count() == 1u && !strcmp(sdc_name(0), "BANK1") && sdc_is_dir(0), "DX7: the folder; the bank shown as a folder");
    sdu_key(0);
    check(sdm_count() == 16u && !strcmp(sdc_name(0), "VOICE 00") && !strcmp(sdc_name(15), "VOICE 15") && sdm_pages() == 2u,
          "DX7: into the bank: its voices, 16 a page");
    tap(B_OCTUP);
    check(sdm_page() == 1u && !strcmp(sdc_name(4), "VOICE 20"), "DX7: OCT+: the page of voices 16..31");
    song.sel = 1;
    trk[1].eng_req = trk[1].engine = 0;                 /* (another engine: the load puts the part on FM6) */
    sdu_op(SDO_LOAD);
    sdu_key(4);
    tap(B_SAVE);
    sdu_frames(4);
    fm6_pack(pk, fm6_ed[1]);
    for (ok = 1, j = 0; j < 118u; j++)
        ok &= pk[j] == f[6u + 20u * 128u + j];
    check(trk[1].eng_req == (uint8_t)ENG_SLOT_FM6, "DX7: voice 20's LOAD puts T2 on FM6");
    check(ok, "... its voice as the file's");
    check(sdu_msg("LOADED"), "... LOADED said");
    check(sdm_count() == 16u && sdm_page() == 1u, "... the bank still shown");
    tap(B_HOME);
    check(sdm_count() == 1u && !strcmp(sdc_name(0), "BANK1"), "DX7: HOME: back to the folder");
}
#endif

/* SYSTEM > SETTINGS (sd_mode.c sdm_backup / sdm_restore; the user, 2026-10-26): BACKUP writes the settings record to the
 * card, RESTORE puts it back into flash and restarts; this FM-1's address and radio copy stay, its pairings too when the
 * file is another unit's */
static void sdu_settings_tests(void)
{
    persist_t mine;
    sdu_card();
    memset(&persist_saved, 0, sizeof persist_saved);
    persist_saved.magic = PERSIST_MAGIC4;
    persist_saved.lowcut = 3;
    memset(persist_saved.ble_addr, 0x11, sizeof persist_saved.ble_addr);
    memset(persist_saved.ble_rf, 0x22, sizeof persist_saved.ble_rf);
    memset(persist_saved.ble_bond, 0x33, sizeof persist_saved.ble_bond);
    sdm_backup();
    check(sdu_has("OPTIMIST/BACKUP/SETTINGS.OPD") && sdu_msg("SETTINGS SAVED TO CARD"), "SETTINGS: BACKUP written to the card");
    persist_saved.lowcut = 7;                            /* (changed since) */
    sdm_restore();
    check(persist_saved.lowcut == 3u && sdu_msg("RESTARTING"), "SETTINGS: RESTORE: the record back (LOWCUT 3 again)");
    sdu_frames(70);
    check(sdu_reboots == 1, "... and a restart, a moment after its message");
    mine = persist_saved;                                /* another FM-1: its own address, radio copy and pairings */
    memset(persist_saved.ble_addr, 0x44, sizeof persist_saved.ble_addr);
    memset(persist_saved.ble_rf, 0x55, sizeof persist_saved.ble_rf);
    memset(persist_saved.ble_bond, 0x66, sizeof persist_saved.ble_bond);
    persist_saved.lowcut = 1;
    sdm_restore();
    check(persist_saved.lowcut == 3u && persist_saved.ble_addr[0] == 0x44 && persist_saved.ble_rf[0] == 0x55 &&
          persist_saved.ble_bond[0] == 0x66, "another FM-1's backup: its settings, this unit's address, radio and pairings kept");
    (void)mine;
}

#if PRESETS_CARD
/* SD card mode's factory presets on the card (sd_mode.c sdm_factory_*; core.h PRESETS_CARD: no tables in the build):
 * the names read at the mount, a preset's file loaded on the main loop's next pass (never where the interrupts may be
 * off), a project or a user preset over it dropping the ask, a missing file and no card said */
static void sdu_factory_put(const char *dir, const char *file, uint8_t fill)
{
    char path[160];
    uint8_t rec[UPC_N];
    memset(rec, fill, sizeof rec);
    snprintf(path, sizeof path, "%s/%s", dir, file);
    check(sdm_small_put(path, "OPPR", rec, sizeof rec) == FR_OK, "factory: a preset file written");
}
static void sdu_factory_tests(void)
{
    char dir[96];
    uint32_t e, g0;
    reset_ui();
    sdu_card();
    for (e = 0; e < NENGINES && ENGINES[e]->npresets < 3u; e++)
        ;
    check(e < NENGINES, "factory: an engine with 3 presets or more");
    f_mkdir("OPTIMIST");
    f_mkdir("OPTIMIST/PRESETS");
    f_mkdir("OPTIMIST/PRESETS/FACTORY");
    snprintf(dir, sizeof dir, "OPTIMIST/PRESETS/FACTORY/%s", ENGINES[e]->name);
    f_mkdir(dir);
    sdu_factory_put(dir, "000 TEST ONE.OPS", 0x11);
    sdu_factory_put(dir, "001 TWO.OPS", 0x22);
    sdm_st = SDM_OFF;                                   /* (a boot: the mount, the index, the names) */
    sdu_frames(3);
    check(!strcmp(preset_name(e, 0), "TEST ONE") && !strcmp(preset_name(e, 1), "TWO") && !strcmp(preset_name(e, 2), "?"),
          "factory: the names from the card, a missing one ?");
    g0 = upc_gives;
    song.sel = 0;
    trk[0].eng_req = trk[0].engine = (uint8_t)e;
    apply_preset_to(&trk[0], 1);
    check(upc_gives == g0 && trk[0].preset == 1u, "factory: a preset applied: the engine's defaults now, the file not read yet");
    sdu_frames(1);
    check(upc_gives == g0 + 1u && upc_rec[0] == 0x22 && trk[0].preset == 1u, "... read on the next pass: the file's record, its place kept");
    apply_preset_to(&trk[0], 0);
    sdm_factory_drop(0);                                /* (a user preset or a project over it) */
    sdu_frames(1);
    check(upc_gives == g0 + 1u, "factory: an ask dropped by a sound loaded over it: not read");
    apply_preset_to(&trk[0], 2);
    sdu_frames(1);
    check(upc_gives == g0 + 1u && sdu_msg("PRESET NOT ON THE CARD"), "factory: a missing file: said, the defaults kept");
    C.present = 0;
    sd_state = SD_NOCARD;
    sdm_st = SDM_NONE;
    apply_preset_to(&trk[0], 0);
    sdu_frames(1);
    check(upc_gives == g0 + 1u && sdu_msg("NO SD CARD"), "factory: no card: the defaults, said");
    C.present = 1;
    card_power_on();
}
#endif

#if FELUCCA_ENG_CZ
/* a Casio CZ single tone on the card (sd_mode.c sdm_cz_load; the user, 2026-10-26: the CZ collections): its key pressed
 * in PRESETS puts its 128 synthesis bytes into the part's tone, the part on CZ first */
static void sdu_cz_tests(void)
{
    static uint8_t f[264];
    FIL fp;
    UINT w;
    uint32_t i, ok = 1;
    sdu_card();
    f[0] = 0xF0, f[1] = 0x44, f[2] = 0, f[3] = 0, f[4] = 0x70, f[5] = 0x20, f[6] = 0x60, f[263] = 0xF7;
    for (i = 0; i < 128u; i++) {
        uint8_t b = (uint8_t)(i * 37u + 5u);
        f[7u + 2u * i] = b & 15u, f[8u + 2u * i] = b >> 4;
    }
    f_mkdir("OPTIMIST/PRESETS/CZ");
    check(f_open(&fp, "OPTIMIST/PRESETS/CZ/BRASS1.SYX", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK &&
          f_write(&fp, f, sizeof f, &w) == FR_OK && w == sizeof f && f_close(&fp) == FR_OK, "CZ: a tone written by a computer");
    sdu_row(PR_CPRE);
    sdu_op(SDO_LOAD);
    for (i = 0; i < sdm_count() && strcmp(sdc_name(i), "CZ"); i++)
        ;
    sdu_key(i);
    song.sel = 0;
    trk[0].eng_req = trk[0].engine = 0;                 /* (another engine: the load puts the part on CZ) */
    sdu_key(0);
    sdu_frames(2);
    for (i = 0; i < 128u; i++)
        ok &= cz_tone(0)[i] == (uint8_t)(i * 37u + 5u);
    check(trk[0].eng_req == (uint8_t)ENG_SLOT_CZ && trk[0].engine == (uint8_t)ENG_SLOT_CZ, "CZ: the tone's key puts T1 on CZ");
    check(ok, "... its 128 bytes in the part's tone, as the file's");
    check(sdu_msg("LOADED"), "... LOADED said");
}
#endif
