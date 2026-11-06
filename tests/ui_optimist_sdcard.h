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
/* a page comes in the main loop's slices (sd_mode.c sdm_read_pump): the checks read what the page will be, the read
 * ended here; sdu_loading's raw (turn) and (frame) are the slices */
#define turn(r, s) (turn(r, s), sdm_wait())
#define tap(b) (tap(b), sdm_wait())
#define frame() (frame(), sdm_wait())
#define kdown(k) (kdown(k), sdm_wait())
#define kup(k) (kup(k), sdm_wait())
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
    sdm_wait();
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], k, &c);
    return c.val;
}
static const char *sdu_label(uint32_t k)
{
    static cell_t c;
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], k, &c);
    return c.label ? c.label : "";
}
static uint32_t sdu_band(uint32_t hot)                  /* the cards' band drawn with that knob last turned, no card lit */
{
    uint32_t i, h = 5381u;
    ui.hot = (uint8_t)hot;
    ui.hot_lit = 0;
    ui.force = 1;
    frame();
    for (i = OY_CARD * 240u; i < OY_PANEL * 240u; i++)
        h = h * 33u + screen[i];
    return h;
}
static uint32_t sdu_used(void);
static void sdu_pairs(void)                             /* a list's 2x1 cards: what shows follows the knob, every knob works */
{
    uint32_t a = sdu_band(0), b = sdu_band(2);
    check(a != b && sdu_band(1) == a && sdu_band(3) == b, "a list's cards: K1/K2 a pair, K3/K4 the other");
    check(card_edge(3u + BIG_W - 1u, OY_CARD) && card_edge(121u + BIG_W - 1u, OY_CARD), "a list's cards: two big cards side by side");
    sdu_band(0);
    uint32_t it = sdc.item;
    turn(EN_K3, 1);
    frame();
    check(ui.hot == 2u && !strcmp(sdu_val(2), "SAVE"), "K1/K2 showing, K3 turned: ACTION moved, its pair shows");
    int32_t kd = 1;
    turn(EN_K2, kd);
    if (sdc.item == it)                                 /* (K2 stops at the last item, no wrap: the other way) */
        kd = -1, turn(EN_K2, kd);
    frame();
    check(ui.hot == 1u && (sdc.item != it || sdm_count() <= 1u || sdu_used() <= 1u), "K3/K4 showing, K2 turned: ITEM moved, its pair shows");
    turn(EN_K2, -kd);
    turn(EN_K3, -1);
    frame();
    check(!strcmp(sdu_val(2), "LOAD") && sdc.item == it, "... and back");
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
        turn(EN_K3, -1);
    for (i = 0; i < op; i++)
        turn(EN_K3, 1);
}
static int sdu_has(const char *path) { FILINFO fi; return f_stat(path, &fi) == FR_OK; }
static int sdu_msg(const char *m) { return strstr(ui.msg, m) != 0; }
static uint64_t sdu_stage_hash(void)
{
    uint64_t h = 0;
    (void)sdp_stage_hash(sdm_stage_len(), sd_blk, &h);
    return h;
}
static int sdu_proj_ok(const char *rel, uint32_t number, uint64_t hash)   /* the card's project ("A/01 - my song"):
                                                                            * whole, that save */
{
    sdp_head h;
    char p[FF_LFN_BUF + 1];
    const char *base = strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel;
    snprintf(p, sizeof p, "Optimist/Projects/%s/%s.opp", rel, base);
    return sdp_verify_path(p, &h, sd_blk) == FR_OK && (!number || h.number == number) && (!hash || h.hash == hash);
}
static uint32_t sdu_used(void)                          /* the slots the page shows something on (a bank's free ones not) */
{
    uint32_t k, n = 0;
    for (k = 0; k < SDC_KEYS; k++)
        n += sdc_used(k) != 0;
    return n;
}
static void sdu_black(uint32_t b)                       /* black key b (0 .. 10: F# G# A# C# D# twice, the top F#) */
{
    static const uint8_t K[5] = {1, 3, 5, 8, 10};
    uint32_t k = b / 5u * 12u + K[b % 5u];
    kdown(k);
    kup(k);
    sdm_poll();
}
static void sdu_card(void)                              /* a fresh card in, the boot's mount and pass */
{
    card(sdu_img, sdu_sectors, 2048, 1, 1, 0);
    (void)sd_mount();
    sdm_st = SDM_OFF;                                   /* (the boot: the device record read again) */
    sdu_frames(3);
}
static void sdu_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void sdu_le32(uint8_t *p, uint32_t v) { sdu_le16(p, v); sdu_le16(p + 2, v >> 16); }
#if FELUCCA_SD_STREAM && FELUCCA_SLICE
/* USE on a part on SLICE (sd_mode.c): it stays on SLICE, SRC CARD; the plan reads the file whole into its block
 * (sd_slice.c) and SLICE's table is the file's: four 100 ms slices, a 25 ms tone at the start of each */
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
    check(f_open(&f, "Optimist/Samples/loop.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_write(&f, h, 44, &k) == FR_OK,
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
    for (idx = 0, n = sdm_count(); idx < n && strcmp(sdc_name(idx), "LOOP.WAV"); idx++)
        ;
    check(idx < n, "SLICE USE: loop.wav listed");
    set_engine_of(&trk[2], ENG_SLOT_SLICE);
    sdu_frames(1);
    check(trk[2].engine == (uint8_t)ENG_SLOT_SLICE, "SLICE USE: T3 on SLICE");
    song.sel = 2;
    sdu_key(idx);                                       /* (a sampler: no question) */
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

/* a file or folder of dir named "<prefix>..." (a kit's name goes on with its instruments) */
static int sdu_in_bank(const char *dir, const char *prefix)
{
    DIR d;
    FILINFO fi;
    int got = 0;
    if (f_opendir(&d, dir) != FR_OK)
        return 0;
    while (!got && f_readdir(&d, &fi) == FR_OK && fi.fname[0])
        got = !strncmp(fi.fname, prefix, strlen(prefix));
    f_closedir(&d);
    return got;
}
/* SAVE on a slot saves on that slot (the user, 2026-11-05, on the FM-1: "it always saves on slot 1, whatever the white
 * note button I click or the card item says": the work's own name, the name page's prefill, took the work's number),
 * in bank J of each list: the key, the ITEM, the name the bank already has, a used slot past its question */
static void sdu_slots_of(uint32_t kind, const char *dir, const char *what)
{
    char m[120];
    uint32_t i;
    sdu_row(kind);
    sdu_black(9);
    check(!strcmp(sdu_val(0), "J") && !sdu_used(), "slots: bank J, empty");
    sdu_op(SDO_SAVE);
    sdu_key(4);
    check(name_on(), "slots: key 5 on a free slot: NAME");
    sdu_name("SLOT FIVE");
    sdu_frames(1);
    snprintf(m, sizeof m, "%s: key 5 in an empty bank: 05 - slot five, no other", what);
    check(sdu_in_bank(dir, "05 - slot five") && sdu_used() == 1u && !strcmp(sdc_name(4), "SLOT FIVE"), m);
    sdu_op(SDO_SAVE);
    sdu_key(5);
    sdu_name("SLOT FIVE");
    sdu_frames(1);
    snprintf(m, sizeof m, "%s: key 6, the same name: 06 - slot five, 05 kept", what);
    check(sdu_in_bank(dir, "06 - slot five") && sdu_in_bank(dir, "05 - slot five") && sdu_used() == 2u, m);
    sdu_op(SDO_SAVE);
    for (i = 0; i < 16u && sdc.item != 7u; i++)
        turn(EN_K2, sdc.item < 7u ? 1 : -1);            /* (the ITEM card, no key) */
    tap(B_SAVE);
    check(name_on() && sdc.item == 7u, "slots: the ITEM on free slot 8, SAVE: NAME");
    sdu_name("BY ITEM");
    sdu_frames(1);
    snprintf(m, sizeof m, "%s: the ITEM on slot 8: 08 - by item", what);
    check(sdu_in_bank(dir, "08 - by item"), m);
    sdu_op(SDO_SAVE);
    sdu_key(4);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "SAVE SLOT FIVE?"), "slots: key 5, used: its question, red");
    tap(B_SAVE);
    check(name_on() && !strcmp(nm.s, "SLOT FIVE"), "slots: YES: NAME, prefilled");
    sdu_name("NEW FIVE");
    sdu_frames(1);
    snprintf(m, sizeof m, "%s: over slot 5 under another name: 05 - new five, the old one gone", what);
    check(sdu_in_bank(dir, "05 - new five") && !sdu_in_bank(dir, "05 - slot five") && sdu_used() == 3u, m);
}
static void sdu_slots_tests(void)
{
    char dir[96];
    song.sel = 0;
    sdu_slots_of(PR_PROJECT, "Optimist/Projects/J", "PROJECT");
    check(sdu_proj_ok("J/05 - new five", 0, 0), "PROJECT: over slot 5: whole");
    sdu_black(1);                                       /* (bank B again, as the tests after it found it) */
    snprintf(dir, sizeof dir, "Optimist/Presets/User/%s/J", sdn_engine(eng_uid(trk[0].eng_req)));
    sdu_slots_of(PR_CPRE, dir, "PRESETS");
    sdu_black(0);
#if FELUCCA_DRUM_KITS
    sdu_slots_of(PR_CKIT, "Optimist/Kits/User/J", "KITS");
    sdu_black(0);
#endif
    sdu_row(PR_PROJECT);
}

/* the card's lists' knobs, ALGORITHM and the PRESETS filter (the user, 2026-11-05) */
static void sdu_lists_cards(uint32_t kind, const char *what)
{
    char m[100];
    uint32_t was;
    sdu_row(kind);
    snprintf(m, sizeof m, "%s: K1 BANK, K2 ITEM, K3 ACTION, K4 VIEW", what);
    check(!strcmp(sdu_label(0), "BANK") && !strcmp(sdu_label(1), "ITEM") && !strcmp(sdu_label(2), kind == PR_CSMP ? "" : "ACTION") &&
          !strcmp(sdu_label(3), "VIEW"), m);
    was = sdc.item;
    turn(EN_K2, was ? -1 : 1);
    snprintf(m, sizeof m, "%s: K2 moves the ITEM", what);
    check(sdc.item != was || sdm_count() < 2u, m);
}
static void sdu_browse_tests(void)
{
    char d[96], b[20];
    uint32_t e0, e1, i, was, t2 = trk[1].eng_req;
    sdu_lists_cards(PR_PROJECT, "PROJECTS");
    sdu_lists_cards(PR_CPRE, "PRESETS");
#if FELUCCA_DRUM_KITS
    sdu_lists_cards(PR_CKIT, "KITS");
#endif
    sdu_lists_cards(PR_CSMP, "SAMPLES");
    check(!strcmp(sdu_val(0), "A") && !strcmp(sdc.unit, "/1"), "SAMPLES, a folder: K1 its pages of 16 as banks, A /1");
    tap(B_OCTUP);                                       /* (one page: nothing) */
    /* ALGORITHM */
    song.sel = 0;
    e0 = trk[0].eng_req;
    e1 = (e0 + 1u) % NENGINES;
    was = trk[1].eng_req;
    trk[1].eng_req = trk[1].engine = (uint8_t)e1;
    sdu_row(PR_CPRE);
    sdu_frames(2);
    snprintf(d, sizeof d, "Optimist/Presets/User/%s/", sdn_engine(eng_uid(e0)));
    check(head_has_track() && !strncmp(sdm.b.root, d, strlen(d)), "PRESETS: the header has the track, the list its engine");
    turn(EN_ALGO, 1);
    sdu_frames(2);
    snprintf(d, sizeof d, "Optimist/Presets/User/%s/", sdn_engine(eng_uid(e1)));
    check(song.sel == 1u && ui.scr == SCR_PROJECT && !strncmp(sdm.b.root, d, strlen(d)), "PRESETS: ALGO: T2, the list its engine's");
    turn(EN_ALGO, -1);
    turn(EN_ALGO, -1);
    sdu_frames(2);
    check(song.sel == 0u && ui.scr == SCR_PROJECT, "PRESETS: ALGO stops at T1 (no MASTER, no returns here)");
    sdu_row(PR_CSMP);
    turn(EN_ALGO, 1);
    check(song.sel == 1u && head_has_track(), "SAMPLES: ALGO selects the track");
    turn(EN_ALGO, -1);
    sdu_row(PR_PROJECT);
    turn(EN_ALGO, 1);
    check(song.sel == 0u && !head_has_track(), "PROJECTS: ALGO does nothing, as before");
#if FELUCCA_DRUM_KITS
    sdu_row(PR_CKIT);
    turn(EN_ALGO, 1);
    check(song.sel == 0u, "KITS: ALGO does nothing, as before");
#endif
    /* the PRESETS filter: ALL or an engine with a folder on the card; the track's by default; until the row is left */
    snprintf(d, sizeof d, "Optimist/Presets/Factory/%s", sdn_engine(eng_uid(e1)));
    f_mkdir(d);
    str_app(d, "/B", sizeof d);
    f_mkdir(d);
    sdu_row(PR_CPRE);
    sdu_frames(2);
    check(!sdc.flt, "PRESETS: the filter: the track's engine");
    for (i = 0; i < 40u && sdc.flt != 1u; i++)
        turn(EN_PRESET, -1);
    {
        uint32_t l[34], n, at, none = NENGINES;
        for (i = 0; i < NENGINES; i++)
            if (i != e0 && i != e1 && !(sdc.fmask >> i & 1u))
                none = i;
        pop_engines(l, &n, &at);
        check(sdc.flt == 1u && pop_kind == 4u && l[0] >= NENGINES && at == 0u && n >= 2u, "PRESETS knob: ALL first, the standard popup");
        check(sdc.fmask >> e0 & 1u && (none < NENGINES || NENGINES < 4u), "... the engines with a folder alone");
    }
    sdu_frames(1);
    sdm_folder(b, sizeof b);
    check(sdm_all_total() >= 1u && !strcmp(sdu_label(0), "BANK") && !strcmp(sdu_val(0), b) && strchr(sdu_val(0), ' '),
          "ALL: K1 names the engine and its bank");
    was = sdm_all_at();
    if (sdm_all_total() > 1u) {
        turn(EN_K1, 1);
        check(sdm_all_at() == was + 1u, "ALL: K1 the next bank of all the engines'");
    }
    turn(EN_PRESET, 1);
    sdu_frames(1);
    check(sdc.flt >= 2u, "PRESETS knob: an engine");
    turn(EN_ALGO, 1);
    sdu_frames(2);
    check(song.sel == 1u && sdc.flt >= 2u && !strstr(sdm.b.root, sdn_engine(eng_uid(e1))) == (sdc.flt - 2u != e1),
          "a choice stays over ALGO: the target moves, the list does not");
    for (i = 0; i < 40u && sdc.flt != 2u + e0; i++)
        turn(EN_PRESET, sdc.flt - 2u < e0 ? 1 : -1);
    sdu_frames(1);
    {   /* T2 on e1, the list e0's: a preset of another engine loads as any (the record names its engine: upreset.c up_load) */
        uint32_t g0 = upc_gives;
        check(sdc.flt == 2u + e0 && sdu_used() >= 1u, "PRESETS knob: T1's engine, with T2 selected");
        for (i = 0; i < SDC_KEYS && !sdc_used(i); i++)
            ;
        sdu_op(SDO_LOAD);
        sdu_key(i);
        tap(B_SAVE);
        sdu_frames(1);
        check(upc_gives == g0 + 1u, "a preset of another engine: loaded on the track selected");
    }
    turn(EN_ALGO, -1);
    sdu_row(PR_CSMP);
    sdu_row(PR_CPRE);
    sdu_frames(2);
    snprintf(d, sizeof d, "Optimist/Presets/User/%s/", sdn_engine(eng_uid(trk[song.sel].eng_req)));
    check(!sdc.flt && !strncmp(sdm.b.root, d, strlen(d)), "the row left: the track's engine again");
    trk[1].eng_req = trk[1].engine = (uint8_t)t2;
}

/* a page read in slices (the user, 2026-11-05): SAMPLES' NAME view over a folder of 80, a pass of 80 entries a page; (turn)
 * and (frame) as the main loop's UI pass, sdm_poll its card pass */
static int sdu_toast(const char *w) { return ui.toast_t && !strcmp(ui.msg, w); }
static void sdu_loading(void)
{
    char was[SDC_NAME], t[48];
    uint32_t i, polls = 0, most = 0, c0, n;
    int quiet = 1;
    FIL f;
    f_mkdir("Optimist/Samples/many");
    for (i = 0; i < 80u; i++) {
        snprintf(t, sizeof t, "Optimist/Samples/many/take %02u.wav", i);
        check(f_open(&f, t, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_close(&f) == FR_OK, "slices: a file of the 80");
    }
    sdu_row(PR_CSMP);
    for (i = 0; i < 4u && sdc.view[SDC_SMP] != SDV_NAME; i++)   /* (left on RECENT) */
        turn(EN_K4, -1);
    sdu_frames(2);
    check(sdm_pages() >= 6u && sdm_page() == 0u && sdm_count() == 16u, "slices: the NAME view, 16 a page");
    str_cpy(was, sdc_name(0), sizeof was);
    /* ---- slow: what shows until it is read */
    (turn)(EN_K1, 1);
    check(sdm_reading() && sdm_page() == 1u && !strcmp(sdc_name(0), was), "a page asked: being read, K1 on its way, the names as they were");
    for (i = 0; i < 200u && sdm_read_ms() + 16u < 200u && sdm_reading(); i++) {
        (frame)();
        quiet &= !sdu_toast("LOADING");
    }
    check(quiet, "... no popup under 200 ms");
    (frame)();
    (frame)();
    check(sdu_toast("LOADING") && pop_kind == 0u && !strcmp(sdc_name(0), was), "... past 200 ms: the toast LOADING, the old page under it");
    ppm("opt-sd-loading");
    (turn)(EN_K1, 1);
    check(sdm_page() == 2u, "a turn on a read going: it adds up");
    do {
        c0 = clk;
        sdm_poll();
        n = clk - c0;
        most = n > most ? n : most;
        polls++;
    } while (sdm_reading() && polls < 1000u);
    check(polls >= 4u && most < 16000u, "the read in slices of the main loop's, each under 16 ms of the bus");
    check(sdm_page() == 2u && sdm.b.page == 2u && strcmp(sdc_name(0), was) != 0, "... the page there, whole");
    (frame)();
    check(!ui.toast_t && !sdc.loading, "... the popup gone with the read");
    /* ---- fast: read within the first frames */
    (turn)(EN_K1, -1);
    quiet = 1;
    for (i = 0; i < 40u; i++) {
        sdm_poll();
        (frame)();
        quiet &= !sdu_toast("LOADING");
    }
    check(quiet && !sdm_reading() && sdm_page() == 1u, "a read that makes it: no popup");
    /* ---- a key on a read going, the card pulled in one */
    (turn)(EN_K1, 1);
    for (i = 0; i < 16u; i++)
        (frame)();
    check(sdu_toast("LOADING"), "LOADING again");
    i = sdc.item;
    kdown(WK(3u)), kup(WK(3u));
    check(sdc.item == i && !op_armed(), "a key under LOADING: nothing asked of a page not here");
    sdm_wait();
    (frame)();
    check(!ui.toast_t, "... the read ended, the popup gone");
    (turn)(EN_K1, 1);
    sd_state = SD_NOCARD;
    sdm_poll();
    check(!sdm_reading() && !sdm_count(), "the card pulled in a read: the read dropped");
    sdu_card();
}
/* SAMPLES' black keys and PREVIEW (the user, 2026-11-05) */
static uint32_t sdu_pv_key(void) { return 1u << (2u * 12u + 1u); }
#if FELUCCA_SD_STREAM
static void sdu_pv_wav(void)                            /* Samples/aa pv.wav: 0.5 s, 44.1 kHz mono: the NAME view's first */
{
    static uint8_t b[512];
    uint32_t i, left = 44100u;
    UINT k;
    FIL f;
    memset(b, 0, sizeof b);
    memcpy(b, "RIFF", 4), sdu_le32(b + 4, 36u + left), memcpy(b + 8, "WAVEfmt ", 8), sdu_le32(b + 16, 16u);
    sdu_le16(b + 20, 1u), sdu_le16(b + 22, 1u), sdu_le32(b + 24, 44100u), sdu_le32(b + 28, 88200u), sdu_le16(b + 32, 2u);
    sdu_le16(b + 34, 16u), memcpy(b + 36, "data", 4), sdu_le32(b + 40, left);
    check(f_open(&f, "Optimist/Samples/aa pv.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK, "preview: its file");
    for (i = 44u; i < 512u; i += 2u)
        sdu_le16(b + i, 12000u);
    f_write(&f, b, 512, &k);
    left -= 468u;
    for (i = 0; i < 512u; i += 2u)
        sdu_le16(b + i, 12000u);
    while (left) {
        f_write(&f, b, left < 512u ? left : 512u, &k);
        left -= k;
    }
    f_close(&f);
}
static uint32_t sdu_pv_run(uint32_t n)                  /* n main loop passes with the audio: the largest output sample */
{
    static int32_t o[CTL * 2];
    uint32_t i, top = 0;
    while (n--) {
        sdm_poll();
        for (i = 0; i < 8u; i++)
            (void)sst_service();
        fm1_ms += 6;
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++)
            top = (uint32_t)(o[i] < 0 ? -o[i] : o[i]) > top ? (uint32_t)(o[i] < 0 ? -o[i] : o[i]) : top;
    }
    return top;
}
static uint32_t sdu_pv_out(void)                        /* the voice alone, a block: its largest sample (half the file's 12000) */
{
    uint32_t i, top = 0;
    for (i = 0; i < CTL; i++)
        mix_l[i] = mix_r[i] = 0;
    sst_pv_mix(CTL);
    for (i = 0; i < CTL; i++)
        top = (uint32_t)(mix_l[i] < 0 ? -mix_l[i] : mix_l[i]) > top ? (uint32_t)(mix_l[i] < 0 ? -mix_l[i] : mix_l[i]) : top;
    return top;
}
#endif
static void sdu_smp_keys(void)
{
    uint32_t f = 0, d = 0, was;
#if FELUCCA_SD_STREAM
    uint32_t eng = trk[0].eng_req;
    int32_t p0 = trk[0].p[P_E0];
    sdu_pv_wav();
#endif
    sdu_row(PR_CSMP);
    while (sdc.view[SDC_SMP] != SDV_NAME)
        turn(EN_K4, sdc.view[SDC_SMP] < SDV_NAME ? 1 : -1);
    sdu_frames(2);
    check(sdm_pages() >= 3u, "samples' keys: the NAME view, its pages");
    was = ui.hot;
    turn(EN_K3, 1);
    check(ui.hot == was && !sdu_label(2)[0], "K3 on SAMPLES: nothing, its card blank");
    sdu_black(2);
    check(sdm_page() == 2u && !sdc.item && !strcmp(sdu_val(0), "C"), "a black key on SAMPLES: its page, K1's letter");
    sdc_keys_lit(&f, &d, 0);
    check(f >> (0u * 12u + 5u) & 1u, "... lit as a bank's, A# for page C");
    sdu_black(9);
    check(sdm_page() == 2u, "a page past the last: nothing");
    sdu_black(0);
    check(sdm_page() == 0u, "... F#: page A");
    turn(EN_K4, 1);
    sdu_frames(2);
    sdu_black(0);
    check(sdc.view[SDC_SMP] == SDV_RECENT && sdm_page() == 0u, "the keys in RECENT too (one page: A)");
    turn(EN_K4, -1);
    sdu_frames(2);
    /* ---- PREVIEW */
    sdu_black(10);
    check(sdc.pv && ui.toast_t && !strcmp(ui.msg, "PREVIEW ON") && sdm_page() == 0u, "the top F#: PREVIEW ON, no page");
    f = d = 0;
    sdc_keys_lit(&f, &d, 0);
    check((f & sdu_pv_key()) != 0u, "... its key lit");
#if FELUCCA_SD_STREAM
    check(!strcmp(sdc_name(0), "AA PV.WAV") || !strcmp(sdc_name(0), "aa pv.wav"), "preview: the sample on slot 1");
    sdu_key(0);
    check(!op_armed() && !sdc_pick_on() && sdc.item == 0u && (sdu_pv_run(40), sst_pv.act) && sdu_pv_out() > 5000u,
          "a white key, PREVIEW on: it plays at once, no question");
    check(trk[0].eng_req == eng && trk[0].p[P_E0] == p0, "... the track's engine and sound as they were");
    check(sst[SST_PV].on && sst[SST_PV].src == SST_PVS, "... streamed: its own stream");
    sdu_key(0);
    (void)sdu_pv_run(4);
    check(!sst_pv.act && !sst[SST_PV].on && !sdu_pv_out(), "the same key again: it stops");
    sdu_key(0);
    (void)sdu_pv_run(20);
    check(sst_pv.act, "... again: it plays");
    sdu_row(PR_CKIT);
    frame();
    (void)sdu_pv_run(4);
    check(!sst_pv.act, "the page left: silence");
    sdu_row(PR_CSMP);
    sdu_frames(2);
    check(sdc.pv, "... PREVIEW still on");
    sdc.item = 1;
    turn(EN_K2, -1);
    frame();
    (void)sdu_pv_run(20);
    check(sst_pv.act && sdc.item == 0u, "K2 onto a sample: it plays");
    sdc.pv_k = 0xFFu;
    sdu_black(10);
    (void)sdu_pv_run(4);
    check(!sdc.pv && !strcmp(ui.msg, "PREVIEW OFF") && !sst_pv.act, "PREVIEW OFF: silence");
    turn(EN_K2, 1);
    turn(EN_K2, -1);
    frame();
    (void)sdu_pv_run(20);
    check(!sst_pv.act, "PREVIEW off: nothing plays on select");
    {
        int ok;
        char q[8];
        (void)q;
        tap(B_SAVE);
        ok = sdc_pick_on() || !op_armed();
        check(ok, "YES on the item: USE, as ever");
        if (sdc_pick_on())
            tap(B_HOME);
    }
#else
    sdu_black(10);
    check(!sdc.pv, "PREVIEW off again");
#endif
    sdu_card();
}
/* the PRESETS knob, the panel's own turn (the user, on the FM-1, 2026-11-05): an engine only in Factory, the list on User;
 * the NAME view; ALL first */
static void sdu_filter_tree(void)
{
    uint32_t e2 = NENGINES, i, l[34], n, at, up = trk[0].preset;
    char d[96], f[120];
    FIL fp;
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, TSEL->eng_req % NENGINES, 0);
    sdu_frames(2);
    turn(EN_PRESET, 1);
    for (i = 0; i < NENGINES; i++)
        if (i != TSEL->eng_req && i != e2 && !(sdc.fmask >> i & 1u))
            e2 = i;
    if (e2 >= NENGINES) {
        check(1, "(no engine free for the tree test)");
        return;
    }
    snprintf(d, sizeof d, "Optimist/Presets/Factory/%s", sdn_engine(eng_uid(e2)));
    f_mkdir("Optimist/Presets/Factory");
    f_mkdir(d);
    strncat(d, "/A", sizeof d - strlen(d) - 1u);
    f_mkdir(d);
    snprintf(f, sizeof f, "%s/01 - factory one.ops", d);
    check(f_open(&fp, f, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_close(&fp) == FR_OK, "the tree test's file");
    sdu_row(PR_CKIT);
    sdu_frames(2);
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, TSEL->eng_req % NENGINES, 0);
    sdu_frames(2);
    check(sdm_pre_tree() == 1, "the list on User");
    for (i = 0; i < 40u && sdc.flt != 2u + e2; i++)
        turn(EN_PRESET, sdc.flt >= 2u && sdc.flt - 2u > e2 ? -1 : 1);
    sdu_frames(2);
    pop_engines(l, &n, &at);
    check(sdc.flt == 2u + e2 && sdm_pre_tree() == 0 && sdu_used() == 1u, "PRESETS knob: an engine only in Factory: Factory's bank shown, not Empty");
    check(l[0] >= NENGINES, "... ALL first");
    while (sdc.view[SDC_PRE] == SDV_FOLDER)
        turn(EN_K4, 1);
    sdu_frames(2);
    turn(EN_PRESET, -1);
    sdu_frames(2);
    check(sdc.view[SDC_PRE] == SDV_NAME && sdc.flt && pop_kind == 4u && trk[0].preset == up, "the knob in NAME: the filter changed, the view still NAME, no preset stepped");
    sdc.view[SDC_PRE] = SDV_FOLDER;                     /* (the banks' tests go on) */
    sdc.stale = 1;
    sdu_row(PR_CKIT);
    sdu_frames(1);
}
/* a preset on trial on the selected track (the user, 2026-11-05) */
typedef struct {
    int16_t p[P_COUNT];
    uint8_t eng_req, preset, user;
    sdm_ref_t ref;
    uint32_t work;
#if FELUCCA_ENG_FM6
    int16_t fm6[FM6_NP];
#endif
} sdu_snd_t;
static void sdu_snd(sdu_snd_t *s, uint32_t part)
{
    memset(s, 0, sizeof *s);
    memcpy(s->p, trk[part].p, sizeof s->p);
    s->eng_req = trk[part].eng_req, s->preset = trk[part].preset, s->user = trk[part].user;
    s->ref = sdm_ref[part];
    s->work = work_sig();
#if FELUCCA_ENG_FM6
    memcpy(s->fm6, fm6_ed[part], sizeof s->fm6);
#endif
}
static int sdu_snd_same(const sdu_snd_t *a, uint32_t part)
{
    sdu_snd_t b;
    sdu_snd(&b, part);
    return !memcmp(a, &b, sizeof b);
}
/* HOME held on the card's lists, deep in a folder and with a preview on (the user, 2026-11-04: "in project pages, long home should go to
 * home"): the mixer on the hold; the short HOME still goes up a folder */
static void sdu_hold_tests(void)
{
    uint32_t i, kinds[] = {PR_PROJECT, PR_CPRE, PR_CSMP
#if FELUCCA_DRUM_KITS
                           , PR_CKIT
#endif
    };
    sdu_row(PR_CSMP);                                   /* a view the user chose holds while the session runs, leaving the page too */
    while (sdc.view[SDC_SMP] != SDV_NAME)
        turn(EN_K4, sdc.view[SDC_SMP] < SDV_NAME ? 1 : -1);
    sdu_row(PR_CKIT);
    op_enter(SCR_SYSTEM);
    sdu_row(PR_CSMP);
    check(sdc.view[SDC_SMP] == SDV_NAME, "SAMPLES: the view the user chose (NAME) is still there after the page and the screen were left");
    while (sdc.view[SDC_SMP] != SDV_FOLDER)
        turn(EN_K4, sdc.view[SDC_SMP] > SDV_FOLDER ? -1 : 1);
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        sdu_row(kinds[i]);
        sdu_frames(2);
        press(B_HOME);
        hh_wait(250);
        check(ui.scr == SCR_PROJECT, "card list: HOME held, not yet at the hold time");
        hh_wait(200);
        check(ui.scr == SCR_HOME, "card list: HOME held: the mixer on the hold");
        release(B_HOME);
        frames(2);
        check(ui.scr == SCR_HOME, "... the release: no second HOME");
    }
    sdu_card();
    {   /* a folder with a sample, and one outside, put there by a computer */
        FIL f;
        UINT k;
        f_mkdir("Optimist");
        f_mkdir("Optimist/Samples");
        f_mkdir("Optimist/Samples/drums");
        if (f_open(&f, "Optimist/Samples/drums/kick.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFkick-data-one", 17, &k), f_close(&f);
        if (f_open(&f, "Optimist/Samples/snare.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFsnare-data-two", 18, &k), f_close(&f);
        sdm_st = SDM_OFF;
        sdu_frames(4);
    }
    sdu_row(PR_CSMP);
    while (sdc.view[SDC_SMP] != SDV_FOLDER)
        turn(EN_K4, sdc.view[SDC_SMP] > SDV_FOLDER ? -1 : 1);
    sdu_frames(2);
    check(sdm_count() >= 1u && sdc_is_dir(0), "SAMPLES: a folder first");
    sdu_key(0);
    check(!sdm_at_top(), "SAMPLES: into the folder");
    press(B_HOME);
    hh_wait(500);
    check(ui.scr == SCR_HOME, "deep in a folder: HOME held goes to the mixer, not up a folder");
    release(B_HOME);
    frames(2);
    sdu_row(PR_CSMP);
    sdu_key(0);
    tap(B_HOME);
    check(ui.scr == SCR_PROJECT && sdm_at_top(), "deep in a folder: a short HOME is still up a folder");
    press(B_HOME);
    press(B_SAVE);
    hh_wait(500);
    check(ui.scr == SCR_PROJECT, "card list: HOME then SAVE held: a chord, not HOME");
    release(B_SAVE);
    release(B_HOME);
    sdu_frames(2);
    op_disarm();
    if (name_on())
        tap(B_HOME);
#if FELUCCA_SD_STREAM
    sdu_pv_wav();
    sdu_row(PR_CSMP);
    while (sdc.view[SDC_SMP] != SDV_NAME)
        turn(EN_K4, sdc.view[SDC_SMP] < SDV_NAME ? 1 : -1);
    sdu_frames(2);
    if (!sdc.pv)
        sdu_black(10);
    sdc.pv_k = 0xFFu;
    sdu_key(0);
    (void)sdu_pv_run(40);
    check(sst_pv.act, "a sample previewing for the held HOME");
    press(B_HOME);
    hh_wait(500);
    (void)sdu_pv_run(4);
    check(ui.scr == SCR_HOME && !sst_pv.act, "HOME held with a sample playing: the mixer, silence");
    release(B_HOME);
    frames(2);
#endif
    sdu_card();
}
/* K2 (ITEM) on the card's lists (the user, 2026-11-04): the next filled item, the free ones and an empty bank passed, the next bank on the last
 * item, a stop at the ends (no wrap); under SAVE the free slots count; inside an engine's filter only that engine's banks */
static void sdu_put(const char *path)
{
    FIL f;
    UINT k;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
        f_write(&f, "x", 1, &k), f_close(&f);
}
static void sdu_rm_tree(const char *path)               /* a folder and all in it gone */
{
    DIR d;
    FILINFO fi;
    char sub[160];
    if (f_opendir(&d, path) == FR_OK) {
        while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
            snprintf(sub, sizeof sub, "%s/%s", path, fi.fname);
            if (fi.fattrib & AM_DIR)
                sdu_rm_tree(sub);
            else
                f_unlink(sub);
        }
        f_closedir(&d);
    }
    f_unlink(path);
}
static char sdu_item_e[2][96];                          /* the engine folders the K2 tests made */
static void sdu_item_clean(void)                        /* the boot's card again: nothing the K2 tests made is left */
{
    sdu_rm_tree("Optimist/Projects/A");
    sdu_rm_tree("Optimist/Projects/C");
    sdu_rm_tree(sdu_item_e[0]);
    sdu_rm_tree(sdu_item_e[1]);
    sdu_item_e[0][0] = sdu_item_e[1][0] = 0;
    sdc.flt = 0;                                        /* (the filter, the engine shown, the item: the list as the boot has it) */
    sdc.fmask = 0;
    sdc.eng = 0xFFu;
    sdc.item = 0;
    sdc.stale = 1;
    sdu_row(PR_PROJECT);
    sdm_st = SDM_OFF;
    sdu_frames(4);
}
static void sdu_item_tests(void)
{
    uint32_t e0 = trk[0].eng_req, e1 = (e0 + 1u) % NENGINES, i;
    char dir[96], path[128];
    f_mkdir("Optimist");
    f_mkdir("Optimist/Projects");
    f_mkdir("Optimist/Projects/A");
    f_mkdir("Optimist/Projects/C");
    f_mkdir("Optimist/Projects/A/01 - one");
    f_mkdir("Optimist/Projects/A/05 - five");
    f_mkdir("Optimist/Projects/C/03 - three");
    sdm_st = SDM_OFF;
    sdu_frames(4);
    sdu_row(PR_PROJECT);
    sdu_frames(2);
    (void)sdm_bank_go(0);
    sdm_wait();
    sdc.item = 0;
    check(sdm_bank_shown() == 0 && sdc_used(0) && !sdc_used(1) && sdc_used(4), "K2: bank A with slots 1 and 5 filled");
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 0 && sdc.item == 4u, "K2 forward: the free slots passed, slot 5");
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 2 && sdc.item == 2u, "K2 forward from the page's last filled item: the next page with something (bank B, empty, passed): C, slot 3");
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 2 && sdc.item == 2u, "K2 forward on the last page: it stops on the last item");
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 4u, "K2 back from the first filled item of C: A's last filled, slot 5");
    turn(EN_K2, -1);
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 0u, "K2 back to the first item");
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 0u, "K2 back on the first page: it stops on the first item");
    sdu_op(SDO_SAVE);
    sdc.item = 4;
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 0 && sdc.item == 5u && !sdc_used(5), "K2 under SAVE: the next slot, free or not");
    sdc.item = 15;
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 1 && sdc.item == 0u, "K2 under SAVE from the last slot: the next bank's first, though it is empty");
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 15u, "... and back to A's last");
    /* ---- the default name of a save: <bank>-<item> from the slot asked (the user, 2026-11-04) */
    sec_name_set(0, "");
    sdu_key(1);
    check(name_on() && nm.kind == NK_CARD && !strcmp(nm.s, "A-02") && !strcmp(nm.ph, "A-02"), "SAVE on A's free slot 2: NAME offers A-02, not PROJECT");
    name_close();
    (void)sdm_bank_go(3);
    sdm_wait();
    sdu_key(11);
    check(name_on() && !strcmp(nm.s, "D-12"), "... bank D, key 12: D-12 (the slot asked, not slot 1)");
    name_close();
    sec_name_set(0, "A-02");
    sdu_key(11);
    check(name_on() && !strcmp(nm.s, "D-12"), "the work still named by an earlier default (A-02), saved on D-12: D-12");
    name_close();
    sec_name_set(0, "MY SONG");
    sdu_key(11);
    check(name_on() && !strcmp(nm.s, "MY SONG") && !strcmp(nm.ph, "D-12"), "the work with a name of its own, on another slot: its name kept (an empty one would be D-12)");
    name_close();
    sec_name_set(0, "PROJECT");
    sdu_key(11);
    check(name_on() && !strcmp(nm.s, "D-12"), "the placeholder name PROJECT counts as unnamed too");
    name_close();
    sec_name_set(0, "");
    (void)sdm_bank_go(0);
    sdm_wait();
    sdu_op(SDO_LOAD);
    /* the engine filter: only that engine's banks count */
    sdu_row(PR_CPRE);
    sdu_frames(2);
    for (i = 0; i < NENGINES && (i == e0 || !eng_built(eng_uid(i))); i++)
        ;
    if (i >= NENGINES) {
        sdu_item_clean();
        return;
    }
    e1 = i;
    f_mkdir("Optimist/Presets");
    f_mkdir("Optimist/Presets/User");
    snprintf(dir, sizeof dir, "Optimist/Presets/User/%s", sdn_engine(eng_uid(e1)));
    str_cpy(sdu_item_e[0], dir, sizeof sdu_item_e[0]);
    f_mkdir(dir);
    for (i = 0; i < 2u; i++) {
        snprintf(path, sizeof path, "%s/%c", dir, 'A' + (char)i);
        f_mkdir(path);
    }
    snprintf(path, sizeof path, "%s/A/02 - first.ops", dir);
    sdu_put(path);
    snprintf(path, sizeof path, "%s/B/04 - second.ops", dir);
    sdu_put(path);
    snprintf(dir, sizeof dir, "Optimist/Presets/User/%s", sdn_engine(eng_uid(e0)));
    str_cpy(sdu_item_e[1], dir, sizeof sdu_item_e[1]);
    f_mkdir(dir);
    snprintf(path, sizeof path, "%s/C", dir);
    f_mkdir(path);
    snprintf(path, sizeof path, "%s/C/07 - other engine.ops", dir);
    sdu_put(path);
    sdm_st = SDM_OFF;
    sdu_frames(4);
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, e1, 0);
    sdc.flt = (uint8_t)(2u + e1);
    sdu_frames(2);
    sdc.item = 1;
    check(sdc_used(1) && sdm_bank_shown() == 0, "K2, a filter: the engine's bank A, slot 2 filled");
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 1 && sdc.item == 3u, "K2 forward in the filter: its bank B, slot 4");
    turn(EN_K2, 1);
    check(sdm_bank_shown() == 1 && sdc.item == 3u, "... the other engine's bank C is no page: it stops");
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 1u, "K2 back in the filter: bank A, slot 2");
    turn(EN_K2, -1);
    check(sdm_bank_shown() == 0 && sdc.item == 1u, "... and stops at the first");
    sdu_item_clean();
}
/* PRESETS and KITS open on the flat NAME list, filtered by engine (the user, 2026-11-04: "flat name list, filtered by engine"); PROJECTS (their FOLDER is
 * shown as NAME) and SAMPLES keep FOLDER. The banks' tests then run with FOLDER chosen, as a user can */
static void sdu_default_views(void)
{
    check(sdc.view[SDC_PRE] == SDV_NAME && sdc.view[SDC_KIT] == SDV_NAME && sdc.view[SDC_PROJ] == SDV_FOLDER && sdc.view[SDC_SMP] == SDV_FOLDER,
          "the lists' default views: PRESETS and KITS NAME, PROJECTS and SAMPLES FOLDER");
    sdc.view[SDC_PRE] = SDV_FOLDER;
    sdc.view[SDC_KIT] = SDV_FOLDER;
    sdc.stale = 1;
}
static uint32_t sdu_name_has(const char *w)             /* the page shown holds a slot whose name holds w */
{
    uint32_t i;
    for (i = 0; i < sdm_count(); i++)
        if (sdl_has(sdc_name(i), w))
            return 1;
    return 0;
}
static uint32_t sdu_name_at(const char *w)
{
    uint32_t i;
    for (i = 0; i < sdm_count(); i++)
        if (sdl_has(sdc_name(i), w))
            return i;
    return 99;
}
static void sdu_name_files(const char *eng, const char *bank, const char *file)
{
    char p[160];
    f_mkdir("Optimist");
    f_mkdir("Optimist/Presets");
    f_mkdir("Optimist/Presets/User");
    snprintf(p, sizeof p, "Optimist/Presets/User/%s", eng);
    f_mkdir(p);
    str_app(p, "/", sizeof p);
    str_app(p, bank, sizeof p);
    f_mkdir(p);
    str_app(p, "/", sizeof p);
    str_app(p, file, sizeof p);
    sdu_put(p);
}
static void sdu_name_tests(void)
{
    uint32_t ea = trk[0].eng_req % NENGINES, eb = NENGINES, ec = NENGINES, i, n = 0;
    char nm[32];
    for (i = 0; i < NENGINES; i++)
        if (i != ea && eng_built(eng_uid(i))) {
            if (eb == NENGINES)
                eb = i;
            else if (ec == NENGINES)
                ec = i;
        }
    if (ec == NENGINES)
        return;                                         /* (fewer than three engines built) */
    song.sel = 0;
    sdu_card();                                         /* (a card in, mounted: the tests before it left it cut or full) */
    sdu_name_files(sdn_engine(eng_uid(ea)), "A", "01 - zzz first.ops");
    sdu_name_files(sdn_engine(eng_uid(ea)), "A", "02 - aaa second.ops");
    sdu_name_files(sdn_engine(eng_uid(eb)), "A", "01 - bbb other.ops");
    for (i = 0; i < 20u; i++) {                         /* twenty of a third engine's: two pages */
        snprintf(nm, sizeof nm, "%02u - qq%02u.ops", (unsigned)(i % 16u) + 1u, (unsigned)i);
        sdu_name_files(sdn_engine(eng_uid(ec)), i < 16u ? "A" : "B", nm);
    }
    sdm_st = SDM_OFF;
    sdu_frames(4);
    sdc.view[SDC_PRE] = SDV_NAME;
    sdc.flt = 0;
    sdc.eng = 0xFFu;
    sdc.stale = 1;
    sdc.fmask = 0;
    sdu_row(PR_CPRE);
    sdu_pairs();
    sdu_frames(6);
    sdu_pairs();
    check(sdc_here() == SDC_PRE && sdc.view[SDC_PRE] == SDV_NAME && sdm_bank_shown() < 0 && !strcmp(sdu_val(3), "NAME"), "PRESETS, NAME: the flat list, no bank");
    check(sdu_name_has("ZZZ") && sdu_name_has("AAA") && !sdu_name_has("BBB") && !sdu_name_has("QQ"),
          "... the track's engine only: its presets, not the others'");
    check(sdu_name_at("AAA") < sdu_name_at("ZZZ"), "... sorted by name (AAA before ZZZ, not by slot)");
    /* the PRESETS knob: the filter changes, NAME stays */
    for (i = 0; i < 60u && sdc.flt != 2u + eb; i++)
        turn(EN_PRESET, sdc.flt >= 2u && sdc.flt - 2u > eb ? -1 : 1), sdu_frames(1);
    sdu_frames(6);
    check(sdc.flt == 2u + eb && sdc.view[SDC_PRE] == SDV_NAME && sdu_name_has("BBB") && !sdu_name_has("ZZZ"), "PRESETS knob: another engine's presets, still NAME");
    for (i = 0; i < 60u && sdc.flt != 1u; i++)
        turn(EN_PRESET, -1), sdu_frames(1);
    sdu_frames(6);
    check(sdc.flt == 1u && sdu_name_has("BBB") && sdu_name_has("AAA"), "... ALL: every engine's");
    /* K2 and the black keys on a flat list of two pages */
    for (i = 0; i < 60u && sdc.flt != 2u + ec; i++)
        turn(EN_PRESET, 1), sdu_frames(1);
    sdu_frames(8);
    n = sdm_pages();
    check(sdc.flt == 2u + ec && n == 2u && sdm_page() == 0u && sdm_count() == 16u, "an engine with twenty presets: two pages, the first of sixteen");
    sdc.item = 3;
    ui.toast_t = 0;                                     /* (the engine popup of the knob away: the list shows) */
    pop_kind = 0;
    ui.force = 1;
    sdu_frames(3);
    ppm("opt-sd-list16");
    sdc.item = 0;
    for (i = 0; i < 15u; i++)
        turn(EN_K2, 1);
    check(sdc.item == 15u && sdm_page() == 0u, "K2 forward through the page: its last item");
    turn(EN_K2, 1);
    sdu_frames(8);
    check(sdm_page() == 1u && sdc.item == 0u, "... on: the next page, its first item");
    for (i = 0; i < 40u; i++)
        turn(EN_K2, 1);
    sdu_frames(2);
    check(sdm_page() + 1u == sdm_pages() && sdc.item + 1u == sdm_count(), "... to the end: the last item of the last page, no wrap");
    n = sdm_count();
    for (i = 0; i + 1u < n; i++)
        turn(EN_K2, -1);
    check(sdc.item == 0u && sdm_page() + 1u == sdm_pages(), "K2 back through the last page: its first item");
    turn(EN_K2, -1);
    sdu_frames(8);
    check(sdm_page() + 2u == sdm_pages() && sdc.item == 15u, "K2 back from the first item: the previous page, its last item");
    for (i = 0; i < 40u; i++)
        turn(EN_K2, -1);
    sdu_frames(2);
    check(sdm_page() == 0u && sdc.item == 0u, "... to the start: the first item, no wrap");
    /* black keys: pages; the top F#: PREVIEW */
    sdu_black(1);
    sdu_frames(8);
    check(sdm_page() == 1u && sdc.item == 0u, "a black key on the flat list: that page");
    sdu_black(5);
    sdu_frames(2);
    check(sdm_page() == 1u, "... a key past the last page: nothing");
    sdu_black(10);
    check(sdc.pvp && !strcmp(ui.msg, "PREVIEW ON"), "the top F# is still PREVIEW");
    sdu_black(10);
    sdu_black(0);
    sdu_frames(8);
    check(sdm_page() == 0u, "... F#: page A");
    /* FOLDER: today's banks, the other view */
    while (sdc.view[SDC_PRE] != SDV_FOLDER)
        turn(EN_K4, -1), sdu_frames(1);
    sdu_frames(8);
    check(sdm_bank_shown() >= 0 && !strcmp(sdu_val(3), "FOLDER"), "VIEW to FOLDER: the banks, as ever");
    while (sdc.view[SDC_PRE] != SDV_NAME)
        turn(EN_K4, 1), sdu_frames(1);
    sdu_frames(8);
    check(sdm_bank_shown() < 0, "... and back to NAME: the flat list");
    sdu_row(PR_CKIT);
    sdu_frames(4);
    sdu_row(PR_CPRE);
    sdu_frames(4);
    check(sdc.view[SDC_PRE] == SDV_NAME, "NAME stays chosen after the page was left");
}
static void sdu_pvw_tests(void)
{
    uint32_t e0 = trk[0].eng_req, e1 = (e0 + 1u) % NENGINES, n, f = 0, d = 0, i;
    char dir[80], path[120];
    sdu_snd_t was;
    sdu_row(PR_CKIT);
    frame();
    song.sel = 0;
    for (i = 0; i < NENGINES && (i == e0 || !eng_built(eng_uid(i))); i++)
        ;
    if (i >= NENGINES)
        return;
    e1 = i;
    set_engine_of(&trk[0], e1);
    sdu_frames(2);
    upc_eng = (int32_t)e1;
    n = up_card_take(sd_blk, "TRIAL");
    check(n != 0u, "preview: a preset of another engine made");
    snprintf(dir, sizeof dir, "Optimist/Presets/User/%s", sdn_engine(eng_uid(e1)));
    f_mkdir("Optimist/Presets/User");
    f_mkdir(dir);
    str_app(dir, "/A", sizeof dir);
    f_mkdir(dir);
    snprintf(path, sizeof path, "%s/01 - trial.ops", dir);
    check(sdm_small_put_id(path, "OPPR", sd_blk, n, 77u) == FR_OK, "preview: its file");
    snprintf(path, sizeof path, "%s/02 - trial two.ops", dir);
    check(sdm_small_put_id(path, "OPPR", sd_blk, n, 78u) == FR_OK, "preview: another");
    set_engine_of(&trk[0], e0);
    sdu_frames(3);
#if FELUCCA_ENG_FM6
    fm6_ed[0][1] ^= 1;                                  /* (a voice of its own, to come back) */
#endif
    frame();
    work_mark();
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, e1, 0);
    sdu_frames(2);
    sdc.flt = (uint8_t)(2u + e1);
    check(sdu_used() == 2u, "preview: the list on the two presets");
    sdu_snd(&was, 0);
    /* ---- the key, the toast, the LED; ten banks */
    sdu_black(10);
    check(sdc.pvp && !strcmp(ui.msg, "PREVIEW ON"), "PRESETS' top F#: PREVIEW ON, no bank");
    sdc_keys_lit(&f, &d, 0);
    check((f & sdu_pv_key()) != 0u, "... its key lit");
    /* ---- no room */
    upc_refuse = 1;
    sdu_key(0);
    sdu_frames(4);
    check(!sdm_pvw_on() && sdu_msg("MEM FULL") && !op_armed() && sdu_snd_same(&was, 0), "no room for its engine: MEM FULL, no question, the track as it was");
    upc_refuse = 0;
    /* ---- on trial */
    sdu_key(0);
    sdu_frames(8);
    check(sdm_pvw_on() && trk[0].eng_req == e1 && trk[0].engine == e1 && !op_armed(), "a white key, PREVIEW on: the engine on the track at once, no question");
    check(!memcmp(&sdm_ref[0], &was.ref, sizeof was.ref), "... no card reference");
    check(ui.menu, "... autosave waits");
    sdu_black(10);
    sdu_frames(8);
    check(!sdc.pvp && !sdm_pvw_on() && trk[0].engine == e0, "PREVIEW off: the track's engine back");
    check(sdu_snd_same(&was, 0) && !work_changed() && !ui.menu, "... its sound bit for bit, nothing marked");
    /* ---- K2 after the settle, a swap, YES */
    sdu_black(10);
    sdu_key(0);
    frame();
    sdc.item = 0;
    turn(EN_K2, 1);
    check(sdc.pv_k == 0u, "K2 moved: nothing before the settle");
    fm1_ms += 100u;
    sdu_frames(2);
    check(sdm_pvw_on() && sdc.pv_k == 1u && sdc.item == 1u, "... after it: the next one on the track");
    sdu_key(1);
    sdu_frames(4);
    check(!sdm_pvw_on() && sdu_snd_same(&was, 0), "the same key again: off, the sound back");
    sdu_key(0);
    frame();
    tap(B_HOME);
    sdu_frames(4);
    check(!sdm_pvw_on() && sdu_snd_same(&was, 0), "HOME: the sound back");
    /* ---- HOME held: the trial reverts and the mixer shows (the user, 2026-11-04) */
    sdu_key(0);
    frame();
    check(sdm_pvw_on(), "on trial for the held HOME");
    press(B_HOME);
    hh_wait(500);
    sdu_frames(4);
    check(ui.scr == SCR_HOME && !sdm_pvw_on() && sdu_snd_same(&was, 0), "HOME held on a preset on trial: the sound back, the mixer");
    release(B_HOME);
    frames(2);
    check(ui.scr == SCR_HOME, "... the release: no second HOME");
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, e1, 0);
    sdc.flt = (uint8_t)(2u + e1);
    sdu_frames(2);
    /* ---- ALGO */
    sdu_key(0);
    frame();
    turn(EN_ALGO, 1);
    frames(2);
    check(song.sel == 1u && !sdm_pvw_on() && sdu_snd_same(&was, 0), "ALGO: that track's sound back first");
    turn(EN_ALGO, -1);
    sdu_frames(2);
    /* ---- leaving, a capture, a project over it */
    sdu_key(0);
    frame();
    sdu_row(PR_CKIT);
    frames(2);
    check(!sdm_pvw_on() && sdu_snd_same(&was, 0), "the page left: the sound back");
    sdu_row(PR_CPRE);
    sdu_frames(2);
    (void)sdm_pre_show(1, e1, 0);
    sdc.flt = (uint8_t)(2u + e1);
    sdu_frames(2);
    sdu_key(0);
    frame();
    check(sdm_pvw_on(), "on trial again");
    {
        static project_t pj;
        static dlrec_t dl1;
        proj_capture(&pj, &dl1);
        check(!sdm_pvw_on() && sdu_snd_same(&was, 0) && pj.t[0].engine == was.eng_req, "a save's capture: the real sound, the trial over");
    }
    /* ---- YES keeps it */
    sdu_key(0);
    frame();
    tap(B_SAVE);
    sdu_frames(4);
    check(!sdm_pvw_on() && trk[0].eng_req == e1 && trk[0].engine == e1 && sdm_ref[0].id == 77u && sdu_msg("LOADED") && !ui.menu,
          "YES: it stays, the card's reference set, LOADED");
    check(!snd_edited(0) && work_changed(), "... the project changed, as a LOAD");
    upc_eng = -1;
    sdu_black(10);
    sdu_row(PR_CKIT);
    frame();
    sdu_card();
}
/* the PRESETS row on the drum track: the kits (the user, 2026-11-05: "while on the drum track (main) the preset should be kits") */
static void sdu_kit_row(void)
{
    uint32_t n, up = 0;
    uint64_t kit_ref = 55u;
    uint32_t w0, f = 0, d = 0;
    int16_t was;
    static uint8_t snd[256 + sizeof sdm_smp];
    static uint8_t b[UK_CARD_N];
    sdu_row(PR_CKIT);
    frame();
    song.sel = 2;
    f_mkdir("Optimist/Kits/User");
    f_mkdir("Optimist/Kits/User/A");
    was = dl.ofs[0][0];
    dl.ofs[0][0] = 9;
    n = ukit_card_take(b);
    dl.ofs[0][0] = was;
    check(n && sdm_small_put_id("Optimist/Kits/User/A/01 - trial kit.opk", "OPKT", b, n, kit_ref) == FR_OK, "kits row: a kit on the card");
    sdu_row(PR_CPRE);
    sdu_frames(2);
    check(sdc_here() == SDC_PRE, "PRESETS, T3: the presets");
    turn(EN_ALGO, 1);
    sdu_frames(2);
    check(is_drum(TSEL) && sdc_pre_row() && sdc_here() == SDC_KIT && sdc.open == SDC_KIT, "ALGO onto DR: the row is the kits' list");
    (void)sdm_bank_go(0);                               /* (the bank the kit is in: K2 earlier may have gone on to another with kits) */
    sdm_wait();
    sdu_frames(2);
    check(sdu_used() >= 1u && !strcmp(sdu_label(0), "BANK"), "... in banks, Kits/User");
    up = (uint32_t)trk[TRK_DRUM].p[P_E0];
    turn(EN_PRESET, 1);
    frame();
    check(pop_kind != 4u, "... the PRESETS knob: no filter");
    check((uint32_t)trk[TRK_DRUM].p[P_E0] == up, "... no kit stepped");
    /* ---- on trial */
    w0 = work_sig();
    memcpy(snd, &sdm_ref[NPART], sizeof sdm_ref[0]);
    sdu_black(10);
    sdc_keys_lit(&f, &d, 0);
    check(sdc.pvp && !strcmp(ui.msg, "PREVIEW ON") && (f & sdu_pv_key()) != 0u, "on DR the top F#: PREVIEW too");
    sdu_key(0);
    frame();
    check(sdm_pvw_on() && dl.ofs[0][0] == 9 && !op_armed() && !sdm_smp_dirty, "a key: the kit on the drum track, no question, nothing to store");
    {
        uint32_t st = sdm_dirty;
        sdm_poll();
        check(sdm_dirty == st && ui.menu, "... nothing stored, autosave waits");
    }
    sdu_black(10);
    frame();
    check(!sdm_pvw_on() && dl.ofs[0][0] == was && work_sig() == w0 && !memcmp(snd, &sdm_ref[NPART], sizeof sdm_ref[0]), "PREVIEW off: the lanes back, bit for bit");
    sdu_black(10);
    sdu_key(0);
    frame();
    turn(EN_ALGO, -1);
    frames(2);
    check(song.sel == 2u && !sdm_pvw_on() && work_sig() == w0 && sdc_here() == SDC_PRE, "ALGO off DR: the lanes back, the presets' list");
    turn(EN_ALGO, 1);
    sdu_frames(2);
    sdu_key(0);
    frame();
    check(sdm_pvw_on(), "on trial again");
    tap(B_SAVE);
    sdu_frames(1);
    check(!sdm_pvw_on() && dl.ofs[0][0] == 9 && sdm_ref[NPART].id == kit_ref && sdu_msg("LOADED"), "YES: the kit stays, its reference set");
    dl.ofs[0][0] = was;
    /* ---- a lane: as ever */
    sdu_black(10);
    sdc.pvp = 0;
    turn(EN_ALGO, 1);
    sdu_frames(2);
    check(lane_picked() && sdc_here() == SDC_PRE, "a lane picked: the presets' list, as before");
    while (is_drum(TSEL))
        turn(EN_ALGO, -1);
    sdu_frames(2);
    /* ---- no user kit: Factory's */
    sdu_row(PR_CKIT);
    frame();
    check(f_rename("Optimist/Kits/User", "Optimist/Kits/Userx") == FR_OK, "kits row: User away");
    song.sel = 0;
    sdu_row(PR_CPRE);
    sdu_frames(2);
    while (!is_drum(TSEL))
        turn(EN_ALGO, 1);
    sdu_frames(2);
    check(sdc_here() == SDC_KIT && sdl_has(sdm.b.root, "/Factory"), "no kit of the user's: DR opens on Factory's");
    sdu_row(PR_CKIT);
    frame();
    (void)f_rename("Optimist/Kits/Userx", "Optimist/Kits/User");
    song.sel = 0;
    sdu_card();
}
/* a boot with its index pass in slices (the user, 2026-11-05: "on boot the screen was flashing"): the LCD's clears counted,
 * on HOME and on SYSTEM's page that shows the pass */
static void sdu_boot_clears(void)
{
    uint32_t i, s, c0, steps = 0, was, ix_bar = 0, ix_all = 0;
    for (s = 0; s < 2u; s++) {
        if (s)
            op_enter(SCR_SYSTEM);
        else
            op_enter(SCR_HOME);
        frame();
        c0 = full_fills;
        sdu_ix_ms = 1;                                  /* (a few units a pass, as a slice of the real clock) */
        sdm_ix_force = 1;
        card(sdu_img, sdu_sectors, 2048, 1, 1, 0);
        (void)sd_mount();
        sdm_st = SDM_OFF;
        was = 0;
        steps = 0;
        for (i = 0; i < 20000u && (i < 4u || sdm_st == SDM_INDEX || sdm_st == SDM_MOUNTED); i++) {
            frame();
            sdm_poll();
            steps += sdm_ix_pct != was;
            was = sdm_ix_pct;
            if (sdm_st == SDM_INDEX) {                  /* (the header's progress bar in the clock icon's place) */
                uint32_t st = 0, k = head_clock(&st);
                ix_bar += k == HC_INDEX && st == (uint32_t)sdm_ix_pct * HI_W / 100u;
                ix_all++;
            }
        }
        {
            uint32_t st = 0;
            check(sdm_st != SDM_INDEX && head_clock(&st) != HC_INDEX, "index done: the clock icon back in its place");
        }
        check(ix_all && ix_bar == ix_all, "a boot's index pass: the header's icon is its progress throughout");
        sdu_ix_ms = 0xFFFFFFFFu;
        printf("SD boot on %s: %u passes, %u steps of the %%, %u LCD clears\n", s ? "SYSTEM" : "HOME", i, steps, full_fills - c0);
        check(steps >= 10u && full_fills - c0 <= 2u, "a boot's index pass: at most two LCD clears");
    }
    op_enter(SCR_HOME);
    frame();
}
/* NEW on the card's PROJECT list (the user, 2026-11-05: "we miss a function to init a project") */
static void sdu_new_tests(void)
{
    uint32_t used, r;
    char cur[SDC_PNAME];
    r = sdu_row(PR_PROJECT);
    (void)r;
    used = sdu_used();
    sdu_op(3);
    check(!strcmp(sdu_val(2), "NEW"), "PROJECTS: ACTION's fourth, NEW");
    sdu_key(0);
    sec_name(0, cur);
    if (op_armed() && ui.arm_q[0] == 'S') {             /* (the work changed and named: its save first) */
        tap(B_HOME);
    }
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "NEW PROJECT?"), "NEW: asked first, red");
    tap(B_HOME);
    check(!op_armed() && sdu_used() == used, "... HOME: nothing happens");
    sdu_op(3);
    trk[1].p[P_E0]++;
    frame();
    sdu_key(0);
    if (cur[0]) {
        snprintf(ui.msg, sizeof ui.msg, "SAVE %s?", cur);
        check(op_armed() && !strcmp(ui.arm_q, ui.msg), "NEW, the work changed: SAVE <its name>? first, as LOAD's");
        tap(B_HOME);
    }
    check(op_armed() && !strcmp(ui.arm_q, "NEW PROJECT?"), "... HOME: NEW asked next");
    tap(B_SAVE);
    sdu_frames(1);
    check(!op_armed() && sdu_msg("NEW PROJECT") && !work_changed() && sdu_used() == used && sdc.op == 0u,
          "... YES: the work blank, no card file touched, ACTION back on LOAD");
    sdu_op(SDO_SAVE);
    sdu_key(15);
    if (op_armed())
        tap(B_SAVE);
    sdu_name("BEFORE NEW");
    sdu_frames(1);
    sdu_row(PR_PROJECT);
    used = sdu_used();
    sec_name(0, cur);
    sdu_op(3);
    trk[1].p[P_E0]++;
    frame();
    sdu_key(0);
    if (cur[0]) {
        check(op_armed() && ui.arm_q[0] == 'S', "NEW, the work changed, named: SAVE first");
        tap(B_SAVE);
        sdu_frames(1);
        check(!op_armed() && sdu_msg("NEW PROJECT") && !work_changed() && sdu_used() == used, "... YES: saved over its slot, then blank");
    } else {
        tap(B_HOME);
    }
    sdu_card();
}
static void sdcard_tests(void)
{
    uint32_t i, r;
    char nm2[64];
    FIL f;
    UINT k;
    uint64_t h1;
    reset_ui();
    {   /* no favorites in SD card mode (the user, 2026-11-04: the banks are the organisation) */
        uint32_t i, fav = 0;
        for (i = 0; i < PLF_NORDER; i++)
            fav |= PLF_ORDER[i] == PLF_FAV;
        check(!FELUCCA_FAVORITES && !fav && PLF_NORDER == 4u, "SD card mode: no favorites (the preset list's filters ALL "
              "FACTORY USER INIT, no FAVORITES, no marks)");
    }
    /* ---- the boot: the folders, the index */
    card(sdu_img, sdu_sectors, 2048, 1, 1, 0);
    check(sd_mount() == SD_READY, "SD: the card mounts (the model, FAT32)");
    {   /* samples a computer put there before: indexed at the boot's pass */
        f_mkdir("Optimist");
        f_mkdir("Optimist/Samples");
        f_mkdir("Optimist/Samples/drums");
        if (f_open(&f, "Optimist/Samples/drums/kick.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFkick-data-one", 17, &k), f_close(&f);
        if (f_open(&f, "Optimist/Samples/snare.wav", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
            f_write(&f, "RIFFsnare-data-two", 18, &k), f_close(&f);
    }
    sdm_st = SDM_OFF;
    sdu_frames(4);
    check(sdm_ready() && sdu_has("Optimist/Projects") && sdu_has("Optimist/Presets") && sdu_has("Optimist/Kits") &&
          sdu_has("Optimist/System/index.bin"), "SD boot: the card ready, its folders made, the sample index written");
    check(sdx_open() == FR_OK && sdx.count == 2u, "SD boot: the two samples indexed");
    sdu_item_tests();                                   /* (on the boot's empty card; it takes away what it made) */
    /* ---- PROJECT: the card's list */
    r = sdu_row(PR_PROJECT);
    check(sdc_listing() && !cards_2x2() && cards_2x1() && OP_PY == OY_PANEL, "PROJECT, a card: the card's list, two big cards, the panel at 76");
    sdu_pairs();
    check(!strcmp(sdu_label(0), "BANK") && !strcmp(sdu_val(0), "A") && !strcmp(sdu_label(2), "ACTION") &&
          !strcmp(sdu_val(2), "LOAD") && !strcmp(sdu_label(1), "ITEM") && !strcmp(sdu_label(3), "VIEW") &&
          !strcmp(sdu_val(3), "NAME"), "PROJECT, a card: BANK A, ACTION LOAD, ITEM, VIEW NAME");
    check(sdm_count() == 16u && !sdu_used(), "PROJECT, a fresh card: bank A, 16 free slots");
    sdu_key(0);
    check(!op_armed() && !name_on() && sdu_msg("EMPTY"), "LOAD on an empty slot: EMPTY, no question");
    /* SAVE by name */
    trk[0].p[P_DEC] = 11;
    sdu_op(SDO_SAVE);
    check(!strcmp(sdu_val(2), "SAVE"), "ACTION knob: SAVE");
    sdu_key(0);
    check(name_on() && nm.kind == NK_CARD, "SAVE on an empty slot: NAME at once");
    sdu_name("MY SONG");
    h1 = sdu_stage_hash();
    check(!name_on() && sdu_msg("SAVED TO CARD"), "NAME done: SAVED TO CARD");
    check(sdu_proj_ok("A/01 - my song", 1, h1),
          "the card's Projects/A/01 - my song/01 - my song.opp (slot 1, the name lowercase): whole, save 1, the flash copy's bytes");
    {
        char b[16];
        sec_name(0, b);
        check(!strcmp(b, "MY SONG"), "the flash project is named as the card's");
    }
    sdu_frames(2);
    check(sdu_used() == 1u && !strcmp(sdc_name(0), "MY SONG"), "the list: MY SONG on slot 1 (shown upper case)");
    ppm("opt-sd-projects");
    {
        uint32_t fl = 0, dm = 0;
        sdc_keys_lit(&fl, &dm, 1);
        check(fl == (1u << white_key(0) | 1u << 1) && !dm, "the keys: slot 1 lit, bank A's black key (F#)");
    }
    check(!strcmp(sdu_val(2), "LOAD"), "after the save: LOAD again");
    /* SAVE over it: asked (red), NAME prefilled, a version kept */
    trk[0].p[P_DEC] = 22;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "SAVE MY SONG?"), "SAVE over MY SONG: asked, red");
    tap(B_SAVE);
    check(name_on() && !strcmp(nm.s, "MY SONG"), "YES: NAME, prefilled MY SONG");
    sdu_name("MY SONG");
    check(sdu_proj_ok("A/01 - my song", 2, sdu_stage_hash()) &&
          sdu_has("Optimist/Projects/A/01 - my song/Versions/01 - my song-0001.opp"),
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
    check(trk[0].p[P_DEC] == 44 && sdu_proj_ok("A/01 - my song", 3, 0) &&
          sdu_has("Optimist/Projects/A/01 - my song/Versions/01 - my song-0002.opp"),
          "YES: saved (save 3, its own slot), then loaded (the same work)");
    /* DELETE */
    sdu_op(SDO_DELETE);
    sdu_key(0);
    check(op_armed() && ui.arm_danger && !strcmp(ui.arm_q, "DELETE MY SONG?"), "DELETE: asked, red");
    tap(B_SAVE);
    sdu_frames(1);
    check(!sdu_has("Optimist/Projects/A/01 - my song") && !sdu_used(), "YES: the project's folder gone, the bank empty");
    /* banks of 16 (the user, 2026-11-04): 16 projects fill A, a 17th is BANK FULL; the black keys and OCT pick banks */
    sdm_save_at(0xFFu);
    for (i = 0; i < 16u; i++) {
        snprintf(nm2, sizeof nm2, "P%02u", (unsigned)i);
        (void)sdm_save(SDC_PROJ, nm2, "");
    }
    check(sdu_proj_ok("A/01 - p00", 1, 0) && sdu_proj_ok("A/16 - p15", 1, 0), "16 projects: A/01 - p00 .. A/16 - p15");
    check(sdm_save(SDC_PROJ, "P16", "") == 1 && sdu_msg("BANK FULL") && !sdu_has("Optimist/Projects/B"),
          "a 17th in bank A: BANK FULL, NAME stays, nothing written");
    sdu_row(PR_PROJECT);
    check(sdu_used() == 16u && !strcmp(sdu_val(0), "A") && !strcmp(sdc_name(0), "P00") && !strcmp(sdc_name(15), "P15"),
          "bank A: P00 on slot 1 .. P15 on slot 16");
    sdu_black(1);
    check(!strcmp(sdu_val(0), "B") && !sdu_used() && sdu_msg("BANK B"), "black key 2 (G#): bank B, empty, said");
    for (i = 16; i < 20u; i++) {
        snprintf(nm2, sizeof nm2, "P%02u", (unsigned)i);
        (void)sdm_save(SDC_PROJ, nm2, "");
    }
    sdu_row(PR_PROJECT);                                /* (the list read again: the bank shown, B) */
    check(sdu_proj_ok("B/01 - p16", 1, 0) && sdu_used() == 4u && !strcmp(sdc_name(0), "P16"),
          "saved while bank B is shown: B/01 - p16 .. B/04 - p19");
    {
        uint32_t fl = 0, dm = 0;
        sdc_keys_lit(&fl, &dm, 1);
        check(fl & 1u << 3, "the keys: bank B's black key (G#) lit");
    }
    tap(B_OCTDN);
    check(!strcmp(sdu_val(0), "A") && !strcmp(sdc_name(0), "P00"), "OCT- tapped: bank A");
    turn(EN_K1, 1);
    check(!strcmp(sdu_val(0), "B"), "BANK knob: bank B");
    tap(B_OCTUP);
    check(!strcmp(sdu_val(0), "C") && !sdu_used(), "OCT+ tapped: bank C (OCT goes on past K, to Z)");
    sdu_black(10);
    check(!strcmp(sdu_val(0), "K"), "the top black key (F#): bank K");
    sdu_black(1);
    tap(B_HOME);
    check(sdm_count() >= 2u && sdc_is_dir(0) && !strcmp(sdc_name(0), "A") && !strcmp(sdc_name(1), "B"),
          "HOME in a bank: the projects' banks as folders");
    sdu_key(1);
    check(!strcmp(sdu_val(0), "B") && !strcmp(sdc_name(0), "P16"), "a bank's folder: into it, as a bank");
    sdu_key(2);
    check(op_armed() && !strcmp(ui.arm_q, "LOAD P18?"), "key 3 in bank B: LOAD P18?");
    tap(B_SAVE);
    sdm_poll();
    {
        char b[16];
        sec_name(0, b);
        check(!strcmp(b, "P18") && sdu_msg("LOADED") && !strcmp(sdm_work, "B/03 - p18"),
              "YES: P18 loaded, the flash project named P18, its place B/03");
    }
    /* ---- PRESETS: SAVE / LOAD by name */
    sdu_row(PR_CPRE);
    check(sdc_listing() && !sdu_used() && !strcmp(sdu_val(3), "FOLDER") && !strcmp(sdu_val(0), "A"),
          "PRESETS: the card's, VIEW FOLDER, the part's engine's User bank A, empty");
    song.sel = 0;
    trk[0].p[P_DEC] = 5;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    check(name_on(), "PRESETS SAVE: NAME");
    sdu_name("LEAD");
    snprintf(nm2, sizeof nm2, "Optimist/Presets/User/%s/A/01 - lead.ops", sdn_engine(eng_uid(trk[0].eng_req)));
    check(upc_takes == 1u && sdu_has(nm2) && sdu_msg("SAVED TO CARD"), "PRESETS: User/<Engine>/A/01 - lead.ops written");
    sdu_frames(1);
    check(sdu_used() == 1u && !strcmp(sdc_name(0), "LEAD"), "the list: LEAD on slot 1 (no number, no extension)");
    sdu_op(SDO_LOAD);
    sdu_key(0);
    tap(B_SAVE);
    check(upc_gives == 1u && upc_rec[0] == (uint8_t)5u && !strcmp((char *)upc_rec + 5, "LEAD"), "LOAD LEAD: its record given");
    {
        char b[16];
        uint32_t was = trk[song.sel].preset;
        snd_name(song.sel, b);
        check(!strcmp(b, "LEAD"), "LOAD LEAD: the part named LEAD on the mixer (the file's name, no extension)");
        trk[song.sel].preset = (uint8_t)(was + 1u);
        snd_name(song.sel, b);
        check(strcmp(b, "LEAD") != 0, "... until its preset changes");
        trk[song.sel].preset = (uint8_t)was;
        str_cpy(sdm.path, "Optimist/Presets/Factory/Analog/A/03 - sub bass.ops", sizeof sdm.path);
        trk[song.sel].preset = 0;
        sdm_named(song.sel, "03 - sub bass.ops");
        check(trk[song.sel].preset == 2u && !sdm_part_name(song.sel),
              "a factory file (Factory/<Engine>/A/03 - ...): the part's preset number 2, its name the engine's list's");
        str_cpy(sdm.path, "Optimist/Presets/Factory/Analog/B/02 - x.ops", sizeof sdm.path);
        sdm_named(song.sel, "02 - x.ops");
        check(trk[song.sel].preset == 17u || ENGINES[trk[song.sel].eng_req % NENGINES]->npresets <= 17u,
              "... bank B's 02: the table's preset 17");
        trk[song.sel].preset = (uint8_t)was;
    }
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
    snprintf(nm2, sizeof nm2, "Optimist/Presets/User/%s/A/02 - lead.ops", sdn_engine(eng_uid(trk[0].eng_req)));
    check(!name_on() && sdu_has(nm2), "SAVE on slot 2 (free) as LEAD, its bank has one: the slot's number, a copy");
    f_unlink(nm2);
    sdu_frames(1);
    sdc.stale = 1;
    sdu_frames(1);
    sdu_op(SDO_SAVE);
    sdu_key(1);
    sdu_name("BASS");
    snprintf(nm2, sizeof nm2, "Optimist/Presets/User/%s/A/02 - bass.ops", sdn_engine(eng_uid(trk[0].eng_req)));
    check(!name_on() && sdu_has(nm2), "SAVE on slot 2 (free): 02 - bass.ops");
    sdu_op(SDO_SAVE);
    sdu_key(5);
    sdu_name("PAD");
    snprintf(nm2, sizeof nm2, "Optimist/Presets/User/%s/A/06 - pad.ops", sdn_engine(eng_uid(trk[0].eng_req)));
    check(!name_on() && sdu_has(nm2), "SAVE on slot 6 (free): the slot's number, 06 - pad.ops");
    snprintf(nm2, sizeof nm2, "Optimist/Presets/User/%s/A/02 - bass.ops", sdn_engine(eng_uid(trk[0].eng_req)));
    {
        uint64_t bass_id = sdm_small_id(nm2);
        char pad[160];
        snprintf(pad, sizeof pad, "Optimist/Presets/User/%s/A/06 - pad.ops", sdn_engine(eng_uid(trk[0].eng_req)));
        check((bass_id & SDI_USER) && sdm_small_id(pad) && sdm_small_id(pad) != bass_id,
              "ids: each preset saved its own user id (BASS's, PAD's: a save as another name, a new one)");
        sdu_op(SDO_SAVE);
        sdu_key(1);
        tap(B_SAVE);
        sdu_name("BASS");
        check(!name_on() && sdu_has(nm2) && sdu_used() == 3u, "SAVE over BASS by its name: its own file again, no new one");
        check(sdm_small_id(nm2) == bass_id, "ids: saved over by its name, BASS keeps its id");
    }
    sdu_op(SDO_SAVE);
    sdu_key(3);
    sdu_name("BAD/NAME");
    check(name_on() && sdu_msg("NAME NOT USABLE"), "a / in a name: NAME NOT USABLE");
    name_close();
    /* ---- KITS */
#if FELUCCA_DRUM_KITS
    sdu_row(PR_CKIT);
    check(sdc_listing() && !sdu_used() && !strcmp(sdu_label(0), "BANK") && !strcmp(sdu_val(0), "A"),
          "KITS: in banks of 16, Kits/User bank A, empty");
    dl.ofs[0][0] = 7;
    sdu_op(SDO_SAVE);
    sdu_key(0);
    sdu_name("ROOM KIT");
    check(TDRUM->p[P_E0] == 0 && sdu_has("Optimist/Kits/User/A/01 - room kit (sampled).opk"), "KITS: Kits/User/A/01 - "
          "room kit (sampled).opk written (the name lowercase, its lanes' instruments: ACOUSTIC's, sampled)");
    dl.ofs[0][0] = 0;
    sdu_frames(1);
    check(sdu_used() == 1u && !strcmp(sdc_name(0), "ROOM KIT"), "KITS: ROOM KIT on slot 1");
    sdu_op(SDO_LOAD);
    sdu_key(0);
    tap(B_SAVE);
    check(dl.ofs[0][0] == 7 && sdu_msg("LOADED"), "LOAD ROOM KIT: the lanes back");
    TDRUM->p[P_E0] = 5;                              /* (the lanes on the kit, the synthesised 808: other instruments) */
    memset(dl.src, 0, sizeof dl.src);
    sdu_op(SDO_SAVE);
    sdu_key(0);
    tap(B_SAVE);                                     /* (over a used slot: asked first) */
    sdu_name("ROOM KIT");
    check(sdu_has("Optimist/Kits/User/A/01 - room kit (synth).opk") &&
              !sdu_has("Optimist/Kits/User/A/01 - room kit (sampled).opk"),
          "KITS: saved over ROOM KIT on other instruments: 01 - room kit (synth).opk, the (sampled) one gone");
    sdu_frames(1);
    check(sdu_used() == 1u && !strcmp(sdc_name(0), "ROOM KIT"), "KITS: still ROOM KIT on slot 1, one file");
    TDRUM->p[P_E0] = 0;
    {   /* a factory kit: Kits/Factory/<L>/<nn> - <name>.opk (the zip's), browsed and loaded as a user kit is */
        uint8_t kit[UK_CARD_N];
        uint32_t i;
        dl.ofs[1][0] = 5;
        TDRUM->p[P_E0] = 6;
        (void)ukit_card_take(kit);
        dl.ofs[1][0] = 0;
        f_mkdir("Optimist/Kits/Factory/A");
        check(sdm_small_put_id("Optimist/Kits/Factory/A/07 - 909 (renamed, 808).opk", "OPKT", kit, sizeof kit,
                               sdi_factory_kit(6)) == FR_OK,
              "KITS: a factory kit file, renamed on a computer (its instruments' part not what its lanes are)");
        for (i = 0; i < 3u && !sdm_at_top(); i++) {
            tap(B_HOME);                                /* (up from Kits/User/A to Kits/) */
            sdu_frames(40);
        }
        for (i = 0; i < sdm_count() && strcmp(sdc_name(i), "FACTORY"); i++)
            ;
        sdu_key(i);
        sdu_frames(1);
        sdu_key(0);
        sdu_frames(1);
        check(!strcmp(sdu_val(0), "A") && sdu_used() == 1u && !strcmp(sdc_name(6), "909"),
              "KITS: Factory, bank A: 909 on slot 7 (the name before \" (\")");
        sdu_black(1);
        check(!strcmp(sdu_val(0), "B") && !sdu_used(), "KITS: black key 2: Factory's bank B");
        sdu_black(0);
        TDRUM->p[P_E0] = 0;
        sdu_op(SDO_LOAD);
        sdu_key(6);
        tap(B_SAVE);
        sdu_frames(1);
        check(TDRUM->p[P_E0] == 6 && dl.ofs[1][0] == 5 && sdu_msg("LOADED 909"), "LOAD 909: the kit and its lanes");
        dl.ofs[1][0] = 0;
    }
    {   /* ids (the user, 2026-11-04): the project's preset (BASS, a user id) and kit (909, its factory id) found again when
         * moved to another bank, renamed, moved between Factory and User; a copied file: the first found, said; gone:
         * MISSING */
        char from[160], to[160], dir[120];
        const char *eng = sdn_engine(eng_uid(trk[song.sel].eng_req));
        uint64_t lid = sdm_ref[song.sel].id, kid = sdm_ref[NPART].id;
        static uint8_t cp[600];
        FIL a, b;
        UINT k1, k2;
#ifdef DEVNAME_UID
        check(dev_uid_ok == 0u || (lid >> 32 & 0x7FFFFFFFu) != 0u, "ids: a user id carries the FM-1's flash ID's hash");
#endif
        check((lid & SDI_USER) && strstr(sdm_ref[song.sel].hint, "/A/02 - bass.ops") &&
                  kid == sdi_factory_kit(6) && strstr(sdm_ref[NPART].hint, "Kits/Factory/A/07 - 909"),
              "ids: the part's preset BASS (saved last: a user id) and the kit 909 (its factory id) kept with their "
              "paths");
        snprintf(from, sizeof from, "Optimist/%s", sdm_ref[song.sel].hint);
        snprintf(dir, sizeof dir, "Optimist/Presets/User/%s/C", eng);
        f_mkdir(dir);
        snprintf(to, sizeof to, "%s/09 - renamed bass.ops", dir);
        f_mkdir("Optimist/Kits/User/C");
        check(f_rename(from, to) == FR_OK && f_rename("Optimist/Kits/Factory/A/07 - 909 (renamed, 808).opk",
                                                      "Optimist/Kits/User/C/05 - my 909 (909).opk") == FR_OK,
              "ids: BASS moved to bank C and renamed, 909 moved from Factory to User and renamed (a computer's moves)");
        check(sdx_update(sd_blk, sizeof sd_blk) == FR_OK, "ids: the index's pass (a mount's)");
        ui.msg[0] = 0;
        sdm_ref_check = 1;
        sdm_poll();
        check(strstr(sdm_ref[song.sel].hint, "/C/09 - renamed bass.ops") && sdm_ref[song.sel].id == lid &&
                  !strcmp(sdm_ref[NPART].hint, "Kits/User/C/05 - my 909 (909).opk") && !sdu_msg("MISSING"),
              "ids: both found by their ids, the project's paths updated");
        snprintf(from, sizeof from, "Optimist/Presets/User/%s/D", eng);
        f_mkdir(from);
        str_app(from, "/01 - copy of bass.ops", sizeof from);
        check(f_open(&a, to, FA_READ) == FR_OK && f_read(&a, cp, sizeof cp, &k1) == FR_OK && f_close(&a) == FR_OK &&
                  f_open(&b, from, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_write(&b, cp, k1, &k2) == FR_OK &&
                  k2 == k1 && f_close(&b) == FR_OK, "ids: BASS copied on a computer (the same id twice)");
        str_cpy(sdm_ref[song.sel].hint, "Presets/gone.ops", sizeof sdm_ref[song.sel].hint);
        sdx_update(sd_blk, sizeof sd_blk);
        ui.msg[0] = 0;
        sdm_ref_check = 1;
        sdm_poll();
        check(sdm_ref[song.sel].hint[0] && (strstr(sdm_ref[song.sel].hint, "renamed bass") ||
                                            strstr(sdm_ref[song.sel].hint, "copy of bass")) && sdu_msg("SAME ID TWICE"),
              "ids: a copied file: the first found, SAME ID TWICE said");
        f_unlink(to);
        f_unlink(from);
        sdx_update(sd_blk, sizeof sd_blk);
        ui.msg[0] = 0;
        sdm_ref_check = 1;
        sdm_poll();
        check(sdu_msg("PRESET MISSING"), "ids: BASS gone from the card: PRESET MISSING (the part's, P1..P3)");
        memset(&sdm_ref[song.sel], 0, sizeof sdm_ref[song.sel]);
    }
#endif
    sdu_slots_tests();
    /* ---- SAMPLES: folders, USE by hash, RECENT */
    sdu_row(PR_CSMP);
    check(sdm_count() == 2u && sdc_is_dir(0) && !strcmp(sdc_name(0), "DRUMS") && !strcmp(sdc_name(1), "SNARE.WAV"),
          "SAMPLES: the folder first, then the files (upper case)");
    check(!sdu_label(2)[0], "SAMPLES: K3 empty (USE is the keys' and YES's)");
    ppm("opt-sd-samples");
    sdu_key(0);
    {
        char t[16];
        prj_name(ui.row[SCR_PROJECT], t);
        check(!op_armed() && sdm_count() == 1u && !strcmp(sdc_name(0), "KICK.WAV") && !strcmp(t, "DRUMS"),
              "a folder's key: into it, no question, the title its name");
    }
    tap(B_HOME);
    check(ui.scr == SCR_PROJECT && sdm_count() == 2u, "HOME: up a folder");
    song.sel = 1;
#if FELUCCA_SD_STREAM && FELUCCA_SLICE
    {   /* a part on no sampler, two built: the engine popup, the samplers alone; HOME cancels; a pick (the user, 2026-11-05) */
        uint32_t was = trk[1].eng_req, l[34], n, at;
        check(was != (uint8_t)ENG_SLOT_SAMPLE && was != (uint8_t)ENG_SLOT_SLICE && was != (uint8_t)ENG_SLOT_GRAIN &&
              was != (uint8_t)ENG_SLOT_STRETCH, "USE: T2 on no sampler");
        sdu_key(1);
        pop_engines(l, &n, &at);
        check(sdc_pick_on() && pop_kind == 4u && ui.toast_t && !op_armed() && n == 2u + FELUCCA_ENG_GRAIN + FELUCCA_ENG_STRETCH &&
              l[0] == (uint32_t)ENG_SLOT_SAMPLE && l[1] == (uint32_t)ENG_SLOT_SLICE &&
              (!FELUCCA_ENG_GRAIN || l[2] == (uint32_t)ENG_SLOT_GRAIN) &&
              (!FELUCCA_ENG_STRETCH || l[n - 1u] == (uint32_t)ENG_SLOT_STRETCH),
              "USE on a non-sampler: the engine popup: every sampler built, SAMPLE SLICE GRAIN STRETCH");
        frames(200);
        check(sdc_pick_on() && ui.toast_t && pop_kind == 4u, "... it waits (no toast's time-out)");
        turn(EN_PRESET, 1);
        pop_engines(l, &n, &at);
        check(at == 1u, "... a knob moves it");
        tap(B_HOME);
        check(!sdc_pick_on() && !pop_kind && trk[1].eng_req == was && !sdm_smp[1].hash && sdm_count() == 2u && !strcmp(sdc_name(1), "SNARE.WAV"),
              "HOME: cancelled, nothing changed, the list as it was");
        sdu_key(1);
        turn(EN_PRESET, 1);
        tap(B_SAVE);
        sdu_frames(1);
        check(!sdc_pick_on() && trk[1].eng_req == (uint8_t)ENG_SLOT_SLICE && trk[1].p[P_E0] == (int16_t)SLC_SRC_CARD && sdm_smp[1].hash,
              "a pick, SLICE: the track on it, its SRC on CARD, the sample in");
        set_engine_of(&trk[1], was);
        sdu_frames(1);
        memset(&sdm_smp[1], 0, sizeof sdm_smp[1]);
        sdm_smp_dirty = 1;
        sdu_frames(1);
        sst_part_set(1, 0);
        sdu_key(1);
        sdu_key(1);                                     /* (the sample's key again: as YES, the first: SAMPLE) */
        sdu_frames(1);
        check(!sdc_pick_on() && trk[1].eng_req == (uint8_t)ENG_SLOT_SAMPLE && trk[1].p[P_E0] == (int16_t)SMP_CARD,
              "a pick, the same key: SAMPLE, its SET on CARD");
        sdu_key(1);
        check(!sdc_pick_on() && !op_armed(), "on a sampler: into it, no question, no popup");
#if FELUCCA_ENG_GRAIN
        {   /* GRAIN takes it (the user, 2026-11-05): a pick, its SRC on CARD, the file's name on SRC; on GRAIN: no question */
            cell_t c;
            int16_t *vp = &trk[1].p[P_E0];
            set_engine_of(&trk[1], was);
            sdu_frames(1);
            sdu_key(1);
            turn(EN_PRESET, 1);
            turn(EN_PRESET, 1);
            tap(B_SAVE);
            sdu_frames(1);
            check(!sdc_pick_on() && trk[1].eng_req == (uint8_t)ENG_SLOT_GRAIN && *vp == (int16_t)SMP_CARD && sdm_smp[1].hash,
                  "a pick, GRAIN: the track on it, its SRC on CARD, the sample in");
            sdu_key(1);
            check(!sdc_pick_on() && !op_armed() && *vp == (int16_t)SMP_CARD, "on GRAIN: into it, no question");
            memset(&c, 0, sizeof c);
            song.sel = 1;
            cell_param(&c, &ENGINES[trk[1].eng_req]->edit[0], vp);
            check(!strcmp(c.val, "SNARE"), "GRAIN's SRC on CARD shows the file's name");
            sdu_frames(30);
            cell_param(&c, &ENGINES[trk[1].eng_req]->edit[0], vp);
            sdu_frames(1);
            sstp.missing |= 2u;
            cell_param(&c, &ENGINES[trk[1].eng_req]->edit[0], vp);
            check(!strcmp(c.val, "MISSING"), "... MISSING when the card has it not");
            sstp.missing &= (uint16_t)~2u;
            set_engine_of(&trk[1], (uint32_t)ENG_SLOT_SAMPLE);
            *vp = (int16_t)SMP_CARD;
            sdu_frames(1);
        }
#endif
    }
#else
    sdu_key(1);                                         /* (one sampler at most: no popup) */
    if (sdc_pick_on())                                  /* (GRAIN or STRETCH built beside SAMPLE: the first) */
        tap(B_SAVE), sdu_frames(1);
    check(!op_armed() && !sdc_pick_on(), "USE with one sampler built, or the first picked: no question left");
#if FELUCCA_SD_STREAM
    check(trk[1].eng_req == (uint8_t)ENG_SLOT_SAMPLE && trk[1].p[P_E0] == (int16_t)SMP_CARD, "... the track on the one sampler");
#endif
#endif
    {
        sdx_rec rec;
        int32_t no = (sdx_open(), sdx_find_path("Samples/snare.wav", &rec));
        uint64_t hh;
        const char *hint;
        check(no >= 0 && sdm_sample_of(1, &hh, &hint) && hh == rec.hash && !strcmp(hint, "snare.wav") && slg_has(SEC_ID_SDSMP),
              "USE snare.wav on T2: its hash and hint in the project");
        check(sdm_sample_path(1, nm2, sizeof nm2) && !strcmp(nm2, "Optimist/Samples/snare.wav"), "... found again by its hash");
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
    check(!strcmp(sdu_val(3), "RECENT") && sdm_count() == 1u && !strcmp(sdc_name(0), "SNARE.WAV"), "VIEW RECENT: the one used");
    f_rename("Optimist/Samples/snare.wav", "Optimist/Samples/drums/snare 2.wav");
    sdm_again();
    sdu_frames(3);
    check(sdm_sample_path(1, nm2, sizeof nm2) && !strcmp(nm2, "Optimist/Samples/drums/snare 2.wav"),
          "renamed and moved on the card: found by its hash");
#if FELUCCA_SD_STREAM && FELUCCA_SLICE
    sdu_slice_use();
#endif
    song.sel = 0;
    sdu_browse_tests();
    sdu_loading();
    sdu_smp_keys();
    sdu_new_tests();
    sdu_filter_tree();
    sdu_pvw_tests();
    sdu_hold_tests();
#if FELUCCA_DRUM_KITS
    sdu_kit_row();
#endif
    sdu_boot_clears();
    /* ---- SYSTEM > SD CARD: the card and the index; 25 versions, no setting (the user, 2026-11-06) */
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
        for (uint32_t k = 1; k < 4u; k++) {
            sys_cell(r, k, &c);
            check(strcmp(c.label, "SLACK") && strcmp(c.label, "VERSIONS"), "SD CARD: no SLACK, no VERSIONS");
        }
        check(sdp_keep == 25u, "25 versions kept");
        {   /* SD CARD: CLOCK, read-only, the step the mount's detection kept (sd_mmc.c sd_clock_probe) */
            const uint32_t k = FELUCCA_SD_STREAM ? 3u : 2u;
            sys_cell(r, k, &c);
            check(!strcmp(c.label, "CLOCK") && !strcmp(c.val, "6") && c.unit && !strcmp(c.unit, "MHZ") && c.kind == CK_RO,
                  "SD CARD: CLOCK 6 MHZ, read-only (the floor: no probe in this test)");
            sd_step = 2;
            sys_cell(r, k, &c);
            check(!strcmp(c.val, "15"), "CLOCK: 15 MHZ at step 2");
            sd_step = 5;
            sys_cell(r, k, &c);
            check(!strcmp(c.val, "8.6"), "CLOCK: 8.6 MHZ at step 5 (8.57)");
            sd_step = SD_FLOOR;
        }
#if FELUCCA_SD_STREAM
        sys_cell(r, 2, &c);
        check(!strcmp(c.label, "PRE-BUFFER") && !strcmp(c.val, "50") && strstr(c.unit, "KB"), "SD CARD: PRE-BUFFER 50 ms, its KB");
        sys_turn(r, 2, 1, 0);
        sys_turn(r, 2, 1, 0);
        sys_turn(r, 2, 1, 0);
        check(sst_pre_ms == 200u, "PRE-BUFFER knob: 100, 200, stops at 200");
        sdm_leave();                                    /* (SYSTEM left) */
        sdu_frames(1);
        memset(&sdm_rec, 0, sizeof sdm_rec);
        sst_pre_set(50);
        sdm_rec_read();
        check(sst_pre_ms == 200u, "... kept in the log's device record");
        sdm_set(0);
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
        {   /* a GRAIN part's cache missing its reads (sd_stream_rc.h, the card too slow): "T3 CARD SLOW" once */
            static uint8_t rcm[SST_RC_BYTES(16)] __attribute__((aligned(4)));
            sst_rc_place(2, rcm, 16);
            fm1_ms += SDM_SLOW_MS + 1u;
            sdu_frames(1);
            sst_rc[2].hit = 30;
            sst_rc[2].miss = 2;
            fm1_ms += SDM_SLOW_MS + 1u;
            sdu_frames(1);
            check(!sdu_msg("CARD SLOW"), "a few misses: nothing said");
            sst_rc[2].hit = 40;
            sst_rc[2].miss = 8;
            fm1_ms += SDM_SLOW_MS + 1u;
            sdu_frames(1);
            check(sdu_msg("T3 CARD SLOW"), "misses an eighth of the reads: T3 CARD SLOW");
            ui.msg[0] = 0;
            sst_rc[2].miss = 20;
            fm1_ms += SDM_SLOW_MS + 1u;
            sdu_frames(1);
            check(!sdu_msg("CARD SLOW"), "... said once");
            sst_rc_place(2, 0, 0);
            sdu_frames(1);
        }
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
        check(!sdm_rec.pend && sdu_proj_ok(sdm_rec.p.name, 1, sdu_stage_hash()) && !strncmp(sdm_rec.p.name, "B/", 2) &&
              strstr(sdm_rec.p.name, " - full one") && sdu_msg("CARD SAVE FINISHED"),
              "room again, the next mount: the pending save finished");
    }
    /* a pending save of another project than the flash one now (NEW, a LOAD since): dropped, never finished with it */
    sdm_rec.pend = 1;
    str_cpy(sdm_rec.p.name, "SOMEONE ELSE", sizeof sdm_rec.p.name);
    sdm_again();
    sdu_frames(3);
    check(!sdm_rec.pend && sdu_msg("CARD SAVE DROPPED") && !sdu_has("Optimist/Projects/SOMEONE ELSE"),
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
/* MIDI tracks' phase 0 (docs/MIDI-TRACKS-DESIGN.md 3): a card project with records of ids this build does not know (a
 * later firmware's, past SLG_IDS) loads: each is skipped, the rest loaded (before phase 0 the whole file was refused);
 * a record of the MIDI tracks' reserved ids (142..225) is loaded as it is and saved to the card again */
static uint8_t sdu_strm[64 * 1024];
static uint32_t sdu_strm_n;
static int sdu_strm_read(uint32_t at, void *p, uint32_t n)
{
    if (at + n > sdu_strm_n)
        return -1;
    memcpy(p, sdu_strm + at, n);
    return 0;
}
static void sdu_strm_rec(uint32_t id, uint32_t n, uint32_t tag)   /* a record (id, n bytes) appended to the stream */
{
    uint32_t i;
    sdu_strm[sdu_strm_n] = (uint8_t)id, sdu_strm[sdu_strm_n + 1u] = (uint8_t)n, sdu_strm[sdu_strm_n + 2u] = (uint8_t)(n >> 8);
    for (i = 0; i < n; i++)
        sdu_strm[sdu_strm_n + 3u + i] = (uint8_t)(tag + i * 3u);
    sdu_strm_n += 3u + n;
}
static void sdu_unknown_tests(void)
{
    static uint8_t org[64 * 1024], b[SEC_REC_MAX];
    int (*rd)(uint32_t, void *, uint32_t) = sdp_hooks.read;
    uint32_t n, first, i, ok = 1;
    FRESULT r;
    sdu_card();
    sdu_row(PR_PROJECT);
    trk[0].p[P_DEC] = 71;
    check(sdm_proj_save("FUTURE") == FR_OK, "a later firmware's card file: this build's project saved to the card first");
    n = sdm_stage_len();
    ok = n > 0u && n < sizeof org && sdm_stage_read(0, org, n) == 0;
    first = 3u + ((uint32_t)org[1] | (uint32_t)org[2] << 8);   /* (the first record's end) */
    sdu_strm_n = 0;
    sdu_strm_rec(SLG_IDS + 3u, 40u, 5u);                /* (a later firmware's: before the first) */
    memcpy(sdu_strm + sdu_strm_n, org, first), sdu_strm_n += first;
    sdu_strm_rec(250u, 700u, 9u);                       /* (one between) */
    sdu_strm_rec(SLG_ID_MPAT0 + 17u, 90u, 11u);         /* (a MIDI pattern: reserved here, kept) */
    sdu_strm_rec(SLG_ID_MWORK0 + 3u, 120u, 13u);        /* (a MIDI track's working copy) */
    memcpy(sdu_strm + sdu_strm_n, org + first, n - first), sdu_strm_n += n - first;
    sdu_strm_rec(0xFFu, 1u, 17u);                       /* (the last id, one byte, at the end) */
    sdp_hooks.read = sdu_strm_read;
    r = sdp_save("FUTURE", 9, sdu_strm_n, sd_blk);
    sdp_hooks.read = rd;
    ok &= r == FR_OK && sdu_proj_ok("FUTURE", 9, 0);
    trk[0].p[P_DEC] = 72;
    sdm_proj_load_file("Optimist/Projects/FUTURE/FUTURE.OPP", "FUTURE");
    check(ok && sdu_msg("LOADED") && trk[0].p[P_DEC] == 71,
          "... its file with four records of unknown ids (first, between, last) and two MIDI ones: LOADED, the project as saved");
    for (i = SLG_IDS, ok = 1; i < 256u; i++)
        ok &= !slg_has(i);
    n = slg_get(SLG_ID_MPAT0 + 17u, b);
    ok &= n == 90 && b[0] == 11u && b[89] == (uint8_t)(11u + 89u * 3u);
    n = slg_get(SLG_ID_MWORK0 + 3u, b);
    ok &= n == 120 && b[119] == (uint8_t)(13u + 119u * 3u);
    check(ok, "... the unknown ids skipped (none in the log), the MIDI tracks' records loaded as they are");
    n = sdm_stage_len();
    ok = n == sdu_strm_n - (3u + 40u) - (3u + 700u) - (3u + 1u);
    check(ok, "... the next card SAVE streams the MIDI records too (the stream as loaded, less the unknown ids)");
    sdu_strm_n = 0;                                     /* (a broken one: an unknown record's length past the payload) */
    memcpy(sdu_strm, org, first), sdu_strm_n = first;
    sdu_strm_rec(SLG_IDS + 1u, 50u, 3u);
    sdu_strm[sdu_strm_n - 50u - 2u] = 0xFF, sdu_strm[sdu_strm_n - 50u - 1u] = 0x7F;   /* (32,767 bytes: past the end) */
    sdp_hooks.read = sdu_strm_read;
    r = sdp_save("BROKEN", 1, sdu_strm_n, sd_blk);
    sdp_hooks.read = rd;
    trk[0].p[P_DEC] = 73;
    sdm_proj_load_file("Optimist/Projects/BROKEN/BROKEN.OPP", "BROKEN");
    check(r == FR_OK && !sdu_msg("LOADED") && sdu_msg("LOAD ERROR") && trk[0].p[P_DEC] == 73,
          "... an unknown record running past the payload: LOAD ERROR, the work as it was");
}
static void sdcard_cut_tests(void)
{
    uint8_t *snap = malloc((size_t)sdu_sectors * 512u);
    long total, w0, kk;
    int unfinished = 0, wrong = 0, said = 0;
    uint64_t want;
    char cut_rel[SDL_NAME_MAX + 1u];
    sdu_card();
    sdu_row(PR_PROJECT);
    trk[0].p[P_DEC] = 61;
    (void)sdm_save(SDC_PROJ, "CUT ME", "");
    str_cpy(cut_rel, sdm_work, sizeof cut_rel);         /* (its place: the bank shown, the first free number) */
    sdc.stale = 1;                                      /* (the list read again: it shows the save) */
    sdu_frames(2);
    trk[0].p[P_DEC] = 62;
    memcpy(snap, sdu_img, (size_t)sdu_sectors * 512u);
    w0 = C.writes;
    sdu_op(SDO_SAVE);
    sdu_key(sdb_bank_no(sdb_base(cut_rel)) - 1u);       /* (its own slot) */
    tap(B_SAVE);
    sdu_name("CUT ME");
    total = C.writes - w0;
    want = sdu_stage_hash();
    check(total > 10 && sdu_proj_ok(cut_rel, 2, want), "a save from the UI (uncut): save 2 on the card");
    for (kk = 0; kk < total; kk++) {
        memcpy(sdu_img, snap, (size_t)sdu_sectors * 512u);
        card_power_on();
        sdu_card();
        sdu_row(PR_PROJECT);
        C.cut_at = C.writes + kk;
        sdu_op(SDO_SAVE);
        sdu_key(sdb_bank_no(sdb_base(cut_rel)) - 1u);
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
        if (sdm_rec.pend || !sdu_proj_ok(cut_rel, 2, want))
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
    check(sdu_has("Optimist/Record/rec-0001.wav"), "... its file: Optimist/Record/rec-0001.wav");
    frames(30);                                          /* (past the gap: a fresh gesture) */
    tap(B_REC);
    tap(B_REC);
    frames(2);
    check(!sdr_busy() && song.rec == rec0, "two quick taps: two plain REC presses (armed, then not), no take");
    sdu_rec_take();
    check(sdr_busy() && sdu_msg("REC-0002"), "a second take: REC-0002");
    sdu_rec_take();
    sdu_rec_service();
    check(sdr_state == RC_DONE && sdu_has("Optimist/Record/rec-0002.wav"), "... stopped, its file");
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
    f_mkdir("Optimist/Presets/DX7");
    check(f_open(&fp, "Optimist/Presets/DX7/BANK1.syx", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK &&
          f_write(&fp, f, sizeof f, &w) == FR_OK && w == sizeof f && f_close(&fp) == FR_OK, "DX7: a bank written by a computer");
    sdu_row(PR_CPRE);
    for (i = 0; i < 4u && !sdm_at_top(); i++) {
        tap(B_HOME);                                /* (from the part's User bank up to Presets/) */
        sdu_frames(40);                                 /* (apart: not a double HOME) */
    }
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
    /* the pages after the card's are there (a line cap that left the SD lines out cut SYSTEM at SETTINGS, the user 2026-11-05) */
    check(glo_find(SCR_SYSTEM, "SETTINGS") < SCREENS[SCR_SYSTEM].rows() && glo_find(SCR_SYSTEM, "GENERAL") < SCREENS[SCR_SYSTEM].rows() &&
              glo_find(SCR_SYSTEM, "METERS") < SCREENS[SCR_SYSTEM].rows() && glo_find(SCR_SYSTEM, "ABOUT") < SCREENS[SCR_SYSTEM].rows(),
          "SYSTEM in SD card mode: GENERAL, METERS and ABOUT after the card's pages");
    check(gl_npg(1) <= GL_MAXP, "SYSTEM in SD card mode: its pages fit the page table");
    persist_t mine;
    sdu_card();
    memset(&persist_saved, 0, sizeof persist_saved);
    persist_saved.magic = PERSIST_MAGIC4;
    persist_saved.lowcut = 3;
    memset(persist_saved.ble_addr, 0x11, sizeof persist_saved.ble_addr);
    memset(persist_saved.ble_rf, 0x22, sizeof persist_saved.ble_rf);
    memset(persist_saved.ble_bond, 0x33, sizeof persist_saved.ble_bond);
    sdm_backup();
    check(sdu_has("Optimist/Backup/settings.opd") && sdu_msg("SETTINGS SAVED TO CARD"), "SETTINGS: BACKUP written to the card");
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
    f_mkdir("Optimist");
    f_mkdir("Optimist/Presets");
    f_mkdir("Optimist/Presets/Factory");
    snprintf(dir, sizeof dir, "Optimist/Presets/Factory/%s", sdn_engine(eng_uid(e)));   /* (sd_names.h: "Analog") */
    f_mkdir(dir);
    str_app(dir, "/A", sizeof dir);                     /* (bank A: the presets 0 .. 15, numbered 01 ..) */
    f_mkdir(dir);
    sdu_factory_put(dir, "01 - test one.ops", 0x11);
    sdu_factory_put(dir, "02 - two.ops", 0x22);
    dir[strlen(dir) - 1u] = 'B';                        /* (bank B empty, bank C: beyond the table, as FM6's pack */
    f_mkdir(dir);                                       /* voices are; the banks read up to the first not there) */
    dir[strlen(dir) - 1u] = 'C';
    f_mkdir(dir);
    sdu_factory_put(dir, "02 - beyond.ops", 0x33);
    sdm_st = SDM_OFF;                                   /* (a boot: the mount, the index, the names) */
    sdu_frames(3);
    check(!strcmp(preset_name(e, 0), "TEST ONE") && !strcmp(preset_name(e, 1), "TWO") && !strcmp(preset_name(e, 2), "?"),
          "factory: the names from the card (A/01 - test one.ops: TEST ONE), a missing one ?");
    check(ENG_NPRE(e) == 34u && !strcmp(preset_name(e, 33), "BEYOND"),
          "factory: the card's count, past the table's (C/02 is preset 33: 34 presets), its name on the list");
    {
        uint32_t i, at = 0xFFFFu;
        if (!bank_ready)
            bank_resolve();
        for (i = 0; i < bank_n; i++)
            if (bank_e[i] == e && bank_pi[i] == 33u)
                at = i;
        check(at != 0xFFFFu, "factory: ... the PRESETS list has it (made again after the card's scan)");
    }
    g0 = upc_gives;
    trk[0].eng_req = trk[0].engine = (uint8_t)e;
    apply_preset_to(&trk[0], 33);
    sdu_frames(1);
    check(upc_gives == g0 + 1u && upc_rec[0] == 0x33 && trk[0].preset == 33u,
          "factory: preset 33 applied: C/02's file read on the next pass, as any preset");
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
    f_mkdir("Optimist/Presets/CZ");
    check(f_open(&fp, "Optimist/Presets/CZ/BRASS1.SYX", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK &&
          f_write(&fp, f, sizeof f, &w) == FR_OK && w == sizeof f && f_close(&fp) == FR_OK, "CZ: a tone written by a computer");
    sdu_row(PR_CPRE);
    for (i = 0; i < 4u && !sdm_at_top(); i++) {
        tap(B_HOME);                                    /* (from the part's User bank up to Presets/) */
        sdu_frames(40);                                 /* (apart: not a double HOME) */
    }
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
#undef turn
#undef tap
#undef frame
#undef kdown
#undef kup
