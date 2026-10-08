/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI (FELUCCA_UI=1, firmware/src/ui/optimist, docs/UI-OPTIMIST-DESIGN.md phase 1) on a framebuffer with
 * panel / flash / storage doubles, the audio (mix_block) running between frames as on the device, in the style of
 * tests/ui_pages_test.c (SLOOP's UI). It checks:
 *   rows     every screen's rows resolve, on every engine and on the drum track: names fit the panel, labels the
 *            cards, a value cell has its descriptor and its value
 *   keys     SELECT the row, ALGORITHM the track, PRESETS the hot cell one unit (the preset only on the SOUND row),
 *            KNOB 1..4 the cells, HOME held + a knob the default, YES enters / toggles / does, NO goes back, the
 *            page buttons jump to their rows, HOME held + a drum key picks the lane, PLAY / REC transport
 *   confirm  HOME + REC asks "CLEAR T1? YES"; YES does it, NO and 3 s let it go; PROJECT's NEW asks; a SAVE over
 *            an empty slot does not
 *   undo     SAVE then HOME undoes, HOME then SAVE redoes, neither is a YES or a NO; EDIT + OCT- / OCT+ too
 *   messages every message and every question fits the header (232 px)
 * then random use: every draw stays on the screen. Renders the screens to PPM (DIR/opt-*.ppm) for review. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_UI 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = p[j * w + i];
}
#include "../firmware/src/display/gfx.c"
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = swap16(c);
}
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16];
#if FELUCCA_LIGHTS
static uint8_t fm1_led_bg[16];
static uint16_t fm1_led_bg_ns;
#endif
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
#include "../firmware/src/ui/panel.c"
#include "../firmware/src/core/model.c"
#include "../firmware/src/drums/dsnd_desc.c"
#define OUT_SHIFT 7
#define HALF_WORDS (HALF_FRAMES * 2u)
static volatile uint32_t audio_halves;
static int32_t abuf[2u * HALF_WORDS];
static uint32_t fm1_audio_free_half(void) { return 0; }
#include "../firmware/src/ui/meters.c"
#include "../firmware/src/ui/optimist/optimist.c"
/* the stores the UI calls (storage/project.c, upreset.c, snapshots.c are not in this test): counted */
static uint32_t saves, loads, up_ops[3];
static int project_used(uint32_t i) { return i < 2; }
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s) { return (uint32_t)project_used(s); }
static uint32_t a24_imports;
static void sl24_auto_import(void) { a24_imports++; }
#endif
static void project_save(uint32_t i) { (void)i; saves++; ui_message("SAVED"); }
static void project_load(uint32_t i) { (void)i; loads++; ui_message("LOADED"); }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static int up_used(uint32_t k) { return k < 2; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 2; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { str_cpy(b, k ? "MY PAD" : "MY LEAD", 13); }
static void up_slot_label(char *b, uint32_t k) { b[0] = 'U'; b[1] = (char)('0' + (k + 1u) / 10u); b[2] = (char)('0' + (k + 1u) % 10u); b[3] = 0; }
static void up_ui(uint32_t op, uint32_t k) { (void)k; up_ops[op % 3u]++; ui_message(op == 2u ? "SAVED U03" : op ? "ERASED" : "LOADED"); }
static void settings_save(void) {}
static uint32_t proj_orph_uid(uint32_t k) { (void)k; return 0xFFu; }
#include "snap_ui_stub.h"
#if FELUCCA_DRUM_KITS
static uint8_t kit_nor[0x2000];                         /* the user kit bank on a RAM image (as ui_pages_test.c) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drums/drum_kits.c"
#endif

static const char *outdir;
static void ppm(const char *name)
{
    char path[512];
    unsigned i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t p = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static uint32_t msg_max, toast_max, modal_bad;         /* the widest header message and toast (px); questions over */
static int modal_fits(void)                             /* the modal's verb (FONT_L) and target in its box (208 px) */
{
    char v[32], a[32];                                  /* (as drawn: in sentence case, the large face with lower case) */
    op_case(v, ui.arm_verb, sizeof v);
    op_case(a, ui.arm_arg, sizeof a);
    return text_w(font_big(), v) <= 208 && (text_w(font_big(), a) <= 208 || text_w(&FONT_S, a) <= 208);
}
static void msg_seen(void)
{
    if (ui.msg_t && (uint32_t)text_w(&FONT_S, ui.msg) > msg_max)
        msg_max = (uint32_t)text_w(&FONT_S, ui.msg);
    if (ui.toast_t && (uint32_t)text_w(&FONT_S, ui.msg) > toast_max)
        toast_max = (uint32_t)text_w(&FONT_S, ui.msg);
    if (ui.arm_scr != ARM_NONE && !modal_fits())
        modal_bad++;
}
static void frame(void)                                 /* one UI frame (~16 ms): the audio, input, LEDs, draw */
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    ui_input();
    msg_seen();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
}
static void frames(uint32_t n) { while (n--) frame(); }
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void turn(uint32_t role, int32_t s) { encs[panel.enc[role]] += s; frame(); }
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static int fails;
static void check(int ok, const char *what) { printf("optimist ui: %-72s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static void reset_ui(void)
{
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    op_disarm();
    ui.msg_t = 0;
    go_home();
    frames(2);
}

/* ---- every screen's rows resolve */
static int rows_ok(uint32_t scr, char *why)
{
    uint32_t r, k, n;
    cell_t c;
    char nm[12];
    ui.scr = (uint8_t)scr;
    n = SCREENS[scr].rows();
    if (!n || n > OP_MAXROWS + 1u)
        return snprintf(why, 64, "%s: %u rows", SCR_NAME[scr], n), 0;
    for (r = 0; r < n; r++) {
        SCREENS[scr].name(r, nm);
        if (!nm[0] || str_len(nm) > 9u)
            return snprintf(why, 64, "%s row %u: name '%s'", SCR_NAME[scr], r, nm), 0;
        for (k = 0; k < 4u; k++) {
            SCREENS[scr].cell(r, k, &c);
            if (c.label && text_w(&FONT_S, c.label) > CARD_W)
                return snprintf(why, 64, "%s %s: label '%s' wider than a card", SCR_NAME[scr], nm, c.label), 0;
            if (c.kind == CK_VAL && c.d && !c.vp)
                return snprintf(why, 64, "%s %s cell %u: a value without its value", SCR_NAME[scr], nm, k), 0;
            if (c.kind > CK_ENTER || str_len(c.val) >= sizeof c.val)
                return snprintf(why, 64, "%s %s cell %u: kind / text", SCR_NAME[scr], nm, k), 0;
        }
    }
    return 1;
}
/* every value on a screen has a form (op_cells.c cell_gauge): only names, actions and read-outs with no range are text */
static uint32_t forms_bad, forms_seen;                /* forms_seen: a bit per GK_* kind met */
static void forms_of(uint32_t scr)
{
    uint32_t r, k, n;
    cell_t c;
    ui.scr = (uint8_t)scr;
    n = SCREENS[scr].rows();
    for (r = 0; r < n; r++)
        for (k = 0; k < 4u; k++) {
            int name;
            SCREENS[scr].cell(r, k, &c);
            name = (c.label && !strcmp(c.label, "PRESET")) || (scr == SCR_HOME && MIX[r % NMIX].kind == MK_SOUND) ||
                   (c.d && c.d->max == c.d->min);
            if ((c.kind == CK_VAL || c.kind == CK_RO) && !name && c.gk == GK_NONE) {
                printf("  no form: screen %u row %u cell %u '%s'\n", scr, r, k, c.label ? c.label : "");
                forms_bad++;
            }
            if (c.gk != GK_NONE && (c.gmax <= c.gmin || c.gv < c.gmin || c.gv > c.gmax))
                forms_bad++;
            forms_seen |= 1u << c.gk;
        }
}
/* every form kind resolves to a primitive that draws: a cell of each kind, at its low and high end, on a small canvas */
static int forms_draw(void)
{
    static const int16_t MIN[GK_PILL + 1] = {0, 0, -64, 0, 0, 0}, MAX[GK_PILL + 1] = {0, 127, 63, 5, 40, 1};
    uint32_t gk, end, i, lit;
    cell_t c;
    for (gk = GK_BAR; gk <= GK_PILL; gk++)
        for (end = 0; end < 2u; end++) {
            cell_clear(&c);
            cell_gauge(&c, gk >= GK_DOTS, MIN[gk], MAX[gk], end ? MAX[gk] : MIN[gk]);
            if (c.gk != gk)
                return 0;                               /* (cell_gauge picks the kind from the range alone) */
            cv_begin(60, 8, C_BLACK);
            draw_gauge(0, 0, 60, 8, &c, C_WHITE, C_LINE);
            cv_blit(0, 0);
            for (i = lit = 0; i < 8u * 240u; i++)
                lit += (i % 240u) < 60u && screen[i] == swap16(C_WHITE);
            if (!lit && !(gk == GK_BAR && !end))        /* (a bar at its minimum is empty: only its track) */
                return 0;
        }
    return 1;
}
static void rows_tests(void)
{
    char why[64] = "";
    uint32_t e, ok = 1, scr;
    track_t keep = *TSEL;
    for (scr = 0; scr < SCR_N && ok; scr++)
        ok = rows_ok(scr, why);
    for (e = 0; e < NENGINES && ok; e++) {              /* the SOUND rows of every engine */
        if (eng_free(e))
            continue;
        set_engine_of(TSEL, e);
        ok = rows_ok(SCR_SOUND, why);
    }
    *TSEL = keep;
    song.sel = TRK_DRUM;                                /* the drum track: its lanes' rows */
    ok = ok && rows_ok(SCR_SOUND, why) && rows_ok(SCR_HOME, why);
    song.sel = 0;
    check(ok, ok ? "every screen's rows resolve (every engine, the drum track)" : why);
    {
        uint32_t scr2;
        for (scr2 = 0; scr2 < SCR_N; scr2++)
            forms_of(scr2);
        song.sel = TRK_DRUM;
        forms_of(SCR_SOUND);
        forms_of(SCR_HOME);
        song.sel = 0;
        {   /* the header: the screen's name once ("Project", "Sound ENV", never "Project project") */
            uint32_t s, r, tr, rep = 0;
            char t[40], *w[8];
            for (tr = 0; tr < 2u; tr++) {
                song.sel = tr ? TRK_DRUM : 0;
                for (s = 0; s < SCR_N; s++)
                    for (r = 0; r < SCREENS[s].rows(); r++) {
                        uint32_t nw = 0, i, j;
                        char *p;
                        head_title(s, r, t, sizeof t);
                        for (p = strtok(t, " "); p && nw < 8u; p = strtok(0, " "))
                            w[nw++] = p;
                        for (i = 0; i < nw; i++)
                            for (j = i + 1u; j < nw; j++)
                                if (!strcmp(w[i], w[j])) {
                                    printf("  header repeats '%s': screen %u row %u\n", w[i], s, r);
                                    rep++;
                                }
                    }
            }
            song.sel = 0;
            check(!rep, "no header repeats a word (every screen, every row, a synth and the drum track)");
        }
        check(!forms_bad, "every value on every screen has a form (bar, centre bar, dots, tick, pill)");
        check((forms_seen & 0x3Eu) == 0x3Eu, "the screens use every form: bar, centre bar, dots, tick, pill");
        check(forms_draw(), "every form kind resolves to a primitive that draws (both ends of its range)");
        ui.force = 1;
    }
    ui.scr = SCR_HOME;
    reset_ui();
    {
        uint32_t i, any = 0, n = snd_rows();
        for (i = 1; i < n; i++)
            any |= snd_page(i)->fam == FAM_ENV ? 1u : snd_page(i)->fam == FAM_SEQ ? 2u : 0u;
        check(any == 3u && snd_rows() > 10u, "SOUND: the SOUND row, then the track's pages (ENV .. PATTERN)");
    }
}

/* ---- keys and knobs */
static void key_tests(void)
{
    int16_t lv;
    uint32_t pre, eng, n;
    reset_ui();
    ui.force = 1;
    frame();
    ppm("opt-home");
    check(ui.scr == SCR_HOME && ui.row[SCR_HOME] == 0 && MIX[0].kind == MK_MASTER, "power-on: the mixer, the cursor on MASTER");
    turn(EN_SELECT, 1);
    check(ui.row[SCR_HOME] == 1 && MIX[1].id == P_LEVEL, "SELECT: the next row (LEVEL)");
    turn(EN_SELECT, -5);
    check(ui.row[SCR_HOME] == 0, "SELECT: stops at the first row");
    turn(EN_SELECT, 1);
    lv = trk[1].p[P_LEVEL];
    turn(EN_K2, -3);
    check(trk[1].p[P_LEVEL] < lv && ui.hot == 1, "mixer LEVEL: KNOB 2 is track 2's level, its cell hot");
    lv = trk[1].p[P_LEVEL];
    turn(EN_PRESET, 1);
    check(trk[1].p[P_LEVEL] == lv + 1, "PRESETS: the hot cell, one unit a detent");
    fm1_in.buttons |= BT(B_HOME);
    turn(EN_K2, 1);
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(trk[1].p[P_LEVEL] == TP[P_LEVEL].def && ui.scr == SCR_HOME, "HOME held + KNOB 2: the cell to its default, no NO");
    turn(EN_ALGO, 1);
    check(song.sel == 1, "ALGORITHM: the track");
    turn(EN_ALGO, -4);
    check(song.sel == 0, "ALGORITHM: stops at T1");
    turn(EN_SELECT, 2);                                 /* FX */
    {
        int16_t fx = trk[2].p[P_FXOFF];
        turn(EN_K3, 0);
        ui.hot = 2;
        ui.hot_lit = 1;
        tap(B_SAVE);
        check(MIX[ui.row[0]].id == P_FXOFF && trk[2].p[P_FXOFF] != fx, "mixer FX: YES toggles the hot cell's track (T3 dry)");
        tap(B_SAVE);
        check(trk[2].p[P_FXOFF] == fx, "mixer FX: YES again, back");
    }
    for (n = 0; n < NMIX && MIX[n].kind != MK_SOUND; n++)
        ;
    ui.row[SCR_HOME] = (uint8_t)n;
    frame();
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_PRESET, 1);
    check(TSEL->preset != pre || TSEL->eng_req != eng, "mixer SOUND row: PRESETS browses the selected track's sounds");
    tap(B_SAVE);
    check(ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == 0, "mixer SOUND row, YES: the SOUND screen, on its SOUND row");
    ui.force = 1;
    frame();
    ppm("opt-sound");
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_K1, 1);
    check(TSEL->preset != pre || TSEL->eng_req != eng, "SOUND row: KNOB 1 the next sound");
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND])->fam == FAM_ENV && snd_page(ui.row[SCR_SOUND])->id[0] == P_ATK,
          "ENV tapped: SOUND's ENV row");
    tap(B_ENV);
    check(snd_page(ui.row[SCR_SOUND])->fam == FAM_ENV && snd_page(ui.row[SCR_SOUND])->id[0] != P_ATK, "ENV again: the family's next row");
    {
        int16_t a = TSEL->p[P_ATK];
        uint32_t p2 = TSEL->preset;
        tap(B_ENV);
        while (snd_page(ui.row[SCR_SOUND])->id[0] != P_ATK)
            tap(B_ENV);
        ui.hot = 0;
        turn(EN_PRESET, 2);
        check(TSEL->p[P_ATK] == a + 2 && TSEL->preset == p2, "ENV row: PRESETS the hot cell (ATK +2), the sound stays");
        fm1_in.buttons |= BT(B_HOME);
        turn(EN_K1, 1);
        fm1_in.buttons &= ~BT(B_HOME);
        frame();
        check(TSEL->p[P_ATK] == TP[P_ATK].def && ui.scr == SCR_SOUND, "HOME held + KNOB 1: ATK to its default, still on SOUND");
    }
    tap(B_SEQ);
    check(snd_page(ui.row[SCR_SOUND])->fam == FAM_SEQ, "SEQ tapped: the PATTERN row (STEP is phase 2)");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME tapped: NO, back to the mixer");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME at the root: nothing");
    tap(B_GLO);
    check(ui.scr == SCR_FX && PAGES[fx_ix[ui.row[SCR_FX]]].fam == FAM_GLO, "GLO tapped: the FX screen, its GLO rows");
    ui.force = 1;
    frame();
    ppm("opt-fx");
    reset_ui();
    song.sel = TRK_DRUM;
    frame();
    fm1_in.buttons |= BT(B_HOME);
    frame();
    key(4);                                             /* (the third white key: lane 2) */
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(lane_selected() == lane_of_key(4) && ui.scr == SCR_HOME, "HOME held + a drum key: the lane picked, no NO");
    tap(B_EDIT);
    check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND])->scope == SC_DSND, "drum track, EDIT: the lane's SOUND rows");
    {
        int8_t t0 = dl.ofs[lane_selected()][DE_TUNE];
        while (snd_page(ui.row[SCR_SOUND])->id[0] != 0)
            tap(B_EDIT);
        turn(EN_K1, 2);
        check(dl.ofs[lane_selected()][DE_TUNE] == t0 + 2, "drum SOUND row: KNOB 1 TUNE of the selected lane");
        dl.ofs[lane_selected()][DE_TUNE] = t0;
    }
    ui.force = 1;
    frame();
    ppm("opt-sound-drums");
    song.sel = 0;
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    rec_wait = 0;
    song.rec = 0;
    tap(B_REC);
    check(rec_wait == 1, "REC, stopped: armed (the first note starts it)");
    tap(B_REC);
    check(rec_wait == 0 && !strcmp(ui.msg, "REC OFF"), "REC again: off");
    tap(B_PLAY);
    check(song.playing || transport_req == 1, "PLAY: start");
    frames(2);
    tap(B_PLAY);
    frames(2);
    check(!song.playing, "PLAY again: stop");
    transport_req = 0;
}

static int px_in(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col)   /* colour col drawn in the box */
{
    uint32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (screen[j * 240u + i] == swap16(col))
                return 1;
    return 0;
}
/* ---- the confirm idiom */
static void confirm_tests(void)
{
    uint32_t r;
    reset_ui();
    trk[0].step[0].time = ST_NOTE;
    trk[0].step[0].n = 1;
    trk[0].step[0].note[0] = 60;
    press(B_HOME);
    press(B_REC);
    release(B_REC);
    release(B_HOME);
    check(ui.arm_scr == ARM_TRACK && !strcmp(ui.arm_q, "CLEAR T1?") && rec_wait == 0 && ui.scr == SCR_HOME,
          "HOME + REC: asks CLEAR T1? (no REC, no NO)");
    check(op_overlay() == 1u && ui.overlay == 1u && !strcmp(ui.arm_verb, "CLEAR?") && !strcmp(ui.arm_arg, "T1") &&
          ui.arm_danger && target_col(ui.arm_arg) == trk_col(0), "the question is the modal: CLEAR? T1 (its colour), red");
    ui.force = 1;
    frame();
    ppm("opt-confirm");
    check(screen[(OY_PANEL + 1u) * 240u + 8u] == swap16(C_ERR) && screen[(OY_PANEL + 20u) * 240u + 4u] == swap16(C_BLACK) &&
          text_w(font_big(), ui.arm_q) <= MODAL_W && OH_PANEL <= 124 && font_big()->last >= 'z',
          "the modal is drawn: a red frame over the panel, the question on one line, one 124-row band");
    check(px_in(24, OY_PANEL + 96u, 80, 16, C_GRAY) && !px_in(24, OY_PANEL + 96u, 80, 16, C_WHITE) &&
          px_in(136, OY_PANEL + 96u, 80, 16, C_WHITE) && !px_in(136, OY_PANEL + 96u, 80, 16, C_GRAY),
          "the modal's hints as the panel's buttons: HOME no on the left, SAVE yes on the right");
    check(!px_in(0, OY_FOOT + 2u, 240, 17, C_WARN) && !px_in(0, OY_FOOT + 2u, 240, 17, C_GRAY) &&
          px_in(0, OY_FOOT + 20u, 180, 17, C_DIM), "while the modal asks, the footer has no yes / no (KEYS stays)");
    tap(B_SAVE);
    check(ui.arm_scr == ARM_NONE && trk[0].step[0].n == 0 && !strcmp(ui.msg, "T1 CLEARED"), "YES: the track cleared, said");
    check(ui.toast_t > 0 && ui.msg_t == 0 && ui.overlay == 2u, "the result of a confirmed action: a toast, not the header");
    ui.force = 1;
    frame();
    ppm("opt-toast");
    check(screen[(OY_PANEL + 60u) * 240u + 4u] == swap16(OP_SURF) &&
          screen[(OY_PANEL + 44u) * 240u + 120u] == swap16(C_OK),
          "the toast: a green box in the middle, the mixer drawn around it (not the modal)");
    frames(OP_TOAST_FRAMES + 2u);
    check(ui.toast_t == 0 && ui.overlay == 0u, "the toast goes (1.5 s), the panel comes back");
    tap(B_SAVE);                                        /* SAVE then HOME: undo; HOME then SAVE: redo */
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(trk[0].step[0].n == 1 && ui.scr == SCR_HOME && ui.arm_scr == ARM_NONE && !strncmp(ui.msg, "UNDO", 4),
          "SAVE then HOME: undo (the step back), no YES, no NO");
    press(B_HOME);
    press(B_SAVE);
    release(B_SAVE);
    release(B_HOME);
    check(trk[0].step[0].n == 0 && ui.scr == SCR_HOME && !strncmp(ui.msg, "REDO", 4), "HOME then SAVE: redo");
    press(B_EDIT);
    press(B_OCTDN);
    release(B_OCTDN);
    release(B_EDIT);
    check(trk[0].step[0].n == 1 && song.octave == 0, "EDIT + OCT-: undo too (the octave stays)");
    press(B_HOME);
    press(B_REC);
    release(B_REC);
    release(B_HOME);
    tap(B_HOME);
    check(ui.arm_scr == ARM_NONE && trk[0].step[0].n == 1, "asked, NO: let go, nothing cleared");
    press(B_HOME);
    press(B_REC);
    release(B_REC);
    release(B_HOME);
    frames(OP_ARM_MS / 16u + 2u);
    check(ui.arm_scr == ARM_NONE && trk[0].step[0].n == 1, "asked, 3 s: let go");
    op_enter(SCR_PROJECT);
    ui.row[SCR_PROJECT] = 0;
    frame();
    ui.force = 1;
    frame();
    ppm("opt-project");
    song.g[G_BPM] = 133;
    turn(EN_K4, 1);
    check(ui.hot == 3 && song.g[G_BPM] == 133, "PROJECT: turning NEW only picks it");
    tap(B_SAVE);
    check(ui.arm_scr == SCR_PROJECT && !strcmp(ui.arm_q, "NEW PROJECT?") && ui.arm_danger, "PROJECT NEW, YES: asks NEW? PROJECT, red");
    ui.force = 1;
    frame();
    ppm("opt-modal-new");
    tap(B_SAVE);
    check(song.g[G_BPM] == GP[G_BPM].def && !strcmp(ui.msg, "NEW PROJECT"), "YES again: a new project");
    song.g[G_SLOT] = 4;                                 /* (project_used: slots 1, 2) */
    turn(EN_K3, 1);
    saves = 0;
    tap(B_SAVE);
    check(saves == 1 && ui.arm_scr == ARM_NONE, "PROJECT SAVE into an empty slot: saved at once");
    song.g[G_SLOT] = 1;
    tap(B_SAVE);
    check(saves == 1 && !strcmp(ui.arm_q, "SAVE PROJECT 1?") && ui.arm_danger, "PROJECT SAVE over a used slot: asks, red");
    ui.force = 1;
    frame();
    ppm("opt-modal-save");
    turn(EN_SELECT, 1);
    check(ui.arm_scr == ARM_NONE, "another row: the question goes");
    for (r = 0; r < NPRJ && prj_kind(r) != PR_USER; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    ui.user_slot = 1;
    turn(EN_K3, 1);
    tap(B_SAVE);
    tap(B_SAVE);
    check(up_ops[1] == 1, "USER ERASE: asks, then erases");
    op_enter(SCR_SYSTEM);
    ui.force = 1;
    frame();
    ppm("opt-system");
    {
        uint32_t p = settings.palette;
        turn(EN_K1, 1);
        check(settings.palette == (p + 1u) % NPALETTES, "SYSTEM SCREEN: KNOB 1 the colours");
        tap(B_SAVE);
        check(settings.palette == (p + 2u) % NPALETTES, "SYSTEM: YES steps a setting round");
        settings.palette = p;
        palette_set(p);
    }
    reset_ui();
}

/* ---- every question and message fits the header */
static void message_tests(void)
{
    uint32_t scr, r, k, n;
    uint32_t widest = 0;
    for (scr = 0; scr < SCR_N; scr++) {                 /* every cell's YES that asks: its question */
        if (scr == SCR_SYSTEM)
            continue;                                   /* (CALIBRATE waits for the panel; nothing there asks) */
        op_enter(scr);
        n = SCR->rows();
        for (r = 0; r < n; r++)
            for (k = 0; k < 4u; k++) {
                cell_t c;
                SCREENS[scr].cell(r, k, &c);
                if (c.kind != CK_ACT || (scr == SCR_SOUND && r == 0 && k == 3u))
                    continue;
                ui.row[scr] = (uint8_t)r;
                SCREENS[scr].yes(r, k, 0);
                if (ui.arm_scr != ARM_NONE) {
                    widest++;                           /* (a question asked) */
                    modal_bad += !modal_fits();
                }
                op_disarm();
            }
    }
    msg_seen();
    check(widest > 0 && !modal_bad, "every question fits the modal (verb and target, 208 px)");
    check(msg_max <= 232u, "every message said in these tests fits the header (232 px)");
    check(toast_max <= 212u, "every toast fits its box (212 px)");
    {   /* sentence case for UI words, capitals for short labels, acronyms and track names (the user, 2026-10-08) */
        static const char *const IN[] = {"MIX MASTER", "SOUND ENV", "T1 CLEARED", "SAVE?", "PROJECT 1", "CLEAR DR?",
                                         "LOOP 2 BARS 120 BPM", "ENV DEST", "NEW PROJECT?", "1.3 OPTIMIST UI"};
        static const char *const OUT[] = {"Mix master", "Sound ENV", "T1 cleared", "Save?", "Project 1", "Clear DR?",
                                          "Loop 2 bars 120 BPM", "ENV dest", "New project?", "1.3 optimist UI"};
        char b[32];
        uint32_t i, bad = 0;
        for (i = 0; i < sizeof IN / sizeof IN[0]; i++)
            if (strcmp(op_case(b, IN[i], sizeof b), OUT[i])) {
                printf("  case: '%s' -> '%s'\n", IN[i], b);
                bad++;
            }
        check(!bad, "titles, messages, questions in sentence case (T1, DR, ENV, BPM, UI kept)");
        check(!strcmp(op_label(b, "ATK", sizeof b), "ATK") && !strcmp(op_label(b, "FILT", sizeof b), "FILT") &&
              !strcmp(op_label(b, "ENGINE", sizeof b), "Engine") && !strcmp(op_label(b, "SAVE AS", sizeof b), "Save as"),
              "card labels: 5 letters or fewer as printed (ATK, FILT), longer ones in sentence case");
        check(!strcmp(op_case(b, "ABCDEFGHIJ", 6), "Abcde"), "the case helper keeps to its buffer");
    }
    reset_ui();
}

static void fuzz(uint32_t nf, uint32_t seed)            /* random use: every draw stays on the screen */
{
    uint32_t f, held = 0;
#define R(n) ((seed = seed * 1664525u + 1013904223u) >> 8) % (n)
    for (f = 0; f < nf; f++) {
        if (R(6) == 0) {
            uint32_t bt = R(NB);
            if (bt == B_PLAY || bt == B_REC)
                continue;
            edges_btn |= 1u << panel.btn[bt];
            if (R(3) == 0)
                held ^= 1u << panel.btn[bt];
        }
        if (R(30) == 0)
            held = 0;
        fm1_in.buttons = held;
        if (R(4) == 0)
            { uint32_t e = R(7); encs[e] += (int32_t)R(5) - 2; }
        fm1_in.notes = R(10) == 0 ? (1u << R(27)) : (R(3) ? fm1_in.notes : 0);
        frame();
    }
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    frames(4);
#undef R
}

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : "build/host";
    host_tracks_init();
    {
        uint32_t i;
        for (i = 0; i < NPART; i++) {
            set_engine_of(&trk[i], trk_def_engine(i));
            apply_preset_to(&trk[i], trk_def_preset(i));
            trk[i].engine = trk[i].eng_req;
        }
    }
    panel = PANEL_DEFAULT;
    settings_init();
    layers_init();
    go_home();
    frames(3);
    rows_tests();
    key_tests();
    confirm_tests();
    message_tests();
    fuzz(20000, 12345);
    check(1, "20000 frames of random use: every draw on the screen");
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
}
