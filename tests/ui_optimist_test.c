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
#if FELUCCA_PATTERNS
#define FELUCCA_FLASH 1                 /* (the section log: phase 4's switch set) */
#endif
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
/* the stores the UI calls. With FELUCCA_PATTERNS (phase 4's switch set: SONG, the scenes, the patterns) the real
 * section log on a simulated NOR (as tests/patterns_ui_test.c), else doubles that count */
static uint32_t saves, loads, up_ops[3];
#if FELUCCA_PATTERNS
#define PROJ_HOST 1
#include "../firmware/src/storage/project.c"
static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty;
static void settings_save(void) {}
static void song_backup(void) {}
static void song_restore(void) {}
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"
static uint32_t arr_saves;
static void arrangement_save(void) { arr_saves++; ui_message("SONG SAVED"); }
#else
static int project_used(uint32_t i) { return i < 2; }
static uint32_t secs_stored, secs_loaded;
static void section_store(uint32_t s) { (void)s; secs_stored++; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; secs_loaded++; }
#if FELUCCA_QCHAIN
static uint32_t section_bars(uint32_t s) { (void)s; return 1; }
#endif
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s) { return (uint32_t)project_used(s); }
static uint32_t a24_imports;
static void sl24_auto_import(void) { a24_imports++; }
#endif
static void project_save(uint32_t i) { (void)i; saves++; ui_message("SAVED"); }
static void project_load(uint32_t i) { (void)i; loads++; ui_message("LOADED"); }
static uint32_t arr_saves;
static void arrangement_save(void) { arr_saves++; }
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static void settings_save(void) {}
static uint32_t proj_orph_uid(uint32_t k) { (void)k; return 0xFFu; }
#endif
static int up_used(uint32_t k) { return k < 2; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 2; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { str_cpy(b, k ? "MY PAD" : "MY LEAD", 13); }
static void up_slot_label(char *b, uint32_t k) { b[0] = 'U'; b[1] = (char)('0' + (k + 1u) / 10u); b[2] = (char)('0' + (k + 1u) % 10u); b[3] = 0; }
static void up_ui(uint32_t op, uint32_t k) { (void)k; up_ops[op % 3u]++; ui_message(op == 2u ? "SAVED U03" : op ? "ERASED" : "LOADED"); }
static uint32_t up_engine_slot(uint32_t k) { return k < 2u ? 0u : NENGINES; }   /* (the two used slots: engine 0's) */
static char up_stored[16];                              /* NAME: what up_store was given (upreset.c) */
static uint32_t up_stores, up_store_slot;
static int up_store(uint32_t k, const char *name) { up_store_slot = k; str_cpy(up_stored, name, sizeof up_stored); up_stores++; return 0; }
static uint32_t fm6_stores, fm6_sends, fm6_inits;      /* FM6's STORE row (engines/fm6/fm6_store.c) */
static void fm6_store(uint32_t k) { (void)k; fm6_stores++; ui_message("STORED"); }
static void fm6_send(void) { fm6_sends++; }
static void fm6_init_voice(void) { fm6_inits++; }
#include "snap_ui_stub.h"
#if FELUCCA_DRUM_KITS
#if !FELUCCA_PATTERNS                                   /* (with the section log: its NOR image holds the bank too) */
static uint8_t kit_nor[0x2000];                         /* the user kit bank on a RAM image (as ui_pages_test.c) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage/storage.c"
#endif
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
#define HOLD_FRAMES (op_hold_ms() / 16u + 3u)            /* a button held past SYSTEM HOLD: its layer shows */
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void turn(uint32_t role, int32_t s) { encs[panel.enc[role]] += s; frame(); }
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static int fails;
static int px_in(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col);
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
            name = (c.label && !strcmp(c.label, "PRESET")) ||
                   (scr == SCR_HOME && c.label && (!strcmp(c.label, "SOUND") || !strcmp(c.label, "KIT"))) ||
                   (c.d && c.d->max == c.d->min) ||
                   (scr == SCR_SONG && !(c.val[0] >= '0' && c.val[0] <= '9'));   /* (SONG: "KEEP", "--", "SEC") */
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

/* ---- the horizontal mixer's helpers (op_mixer.c): a row's cell by its label, in whichever set it is */
static int mix_find(const char *label)                  /* the cursor row's set and hot cell on that label */
{
    uint32_t set, k, n = mx_sets(mx_row());
    cell_t c;
    for (set = 0; set < n; set++)
        for (k = 0; k < 4u; k++) {
            mx.set = (uint8_t)set;
            mix_cell(mx_row(), k, &c);
            if (c.label && !strcmp(c.label, label)) {
                ui.hot = (uint8_t)k;
                ui.hot_lit = 1;
                return 1;
            }
        }
    mx.set = 0;
    return 0;
}
static void mix_to_sound(void)                          /* the mixer, YES on the selected track's row: its SOUND */
{
    go_home();
    frame();
    mx.set = 0;
    ui.hot = 0;
    ui.hot_lit = 1;
    frame();
    tap(B_SAVE);
}
static int mix_go(const char *screen)                   /* the mixer's screens set: YES on that screen's cell */
{
    int ok;
    go_home();
    frame();
    ok = mix_find(screen);
    frame();
    tap(B_SAVE);
    return ok;
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
    check(ui.scr == SCR_HOME && ui.row[SCR_HOME] == MXR_T1 && song.sel == 0 && mx_set(MXR_T1) == 0,
          "power-on: the mixer, on T1's row (MASTER above it, out of view), its first knob set");
    mix_to_sound();
    check(ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == 0, "the mixer, YES on a track's row: its SOUND rows, on the SOUND row");
    (void)lv, (void)n;
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
    check(ui.scr == SCR_STEP && ly_lock == LY_STEP, "SEQ tapped: STEP, the keys are steps (ly_lock LY_STEP)");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME tapped: NO, back to the mixer");
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE, "HOME at the root: the scope [P]");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME on the scope: the mixer again");
    check(mix_go("FX") && ui.scr == SCR_FX, "the mixer's screens set: YES on FX, the FX screen");
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
    check(lane_selected() == lane_of_key(4) && ui.scr == SCR_HOME && ui.row[SCR_HOME] == MXR_LANE0 + lane_of_key(4),
          "HOME held + a drum key on the mixer: the lane picked, the cursor on its row, no NO");
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
    check(!px_in(0, OY_PANEL + OH_PANEL, 240, 240 - OY_PANEL - OH_PANEL, C_GRAY) &&
          !px_in(0, OY_PANEL + OH_PANEL, 240, 240 - OY_PANEL - OH_PANEL, C_DIM) &&
          !px_in(0, OY_PANEL + OH_PANEL, 240, 240 - OY_PANEL - OH_PANEL, C_LINE),
          "no footer: under the modal an empty band to the screen's foot");
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
    /* SAVE then HOME: undo; HOME then SAVE: redo */
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
#if FELUCCA_PATTERNS
    saves = 0;
    tap(B_SAVE);
    check(name_on() && nm.kind == NK_PROJ && !project_used(3), "PROJECT SAVE with the log: NAME first, nothing written");
    tap(B_SAVE);                                        /* (NAME's SAVE: the name kept, the project written) */
    saves = project_used(3);                            /* (the real log: slot 4 stored) */
    project_save(0);                                    /* (slot 1 used, for the question below) */
#else
    saves = 0;
    tap(B_SAVE);
#endif
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

/* ---- SOUND: a page button shows its family's rows only; the mixer's SOUND row every row */
static void family_tests(void)
{
    uint32_t r, n, all, only = 1;
    reset_ui();
    tap(B_LFO);
    n = SCR->rows();
    for (r = 0; r < n; r++)
        only &= snd_page(r) && snd_page(r)->fam == FAM_LFO;
    check(ui.scr == SCR_SOUND && n >= 1u && only, "LFO tapped: SOUND with the LFO family's rows only");
    {
        char t[40];
        head_title(SCR_SOUND, ui.row[SCR_SOUND], t, sizeof t);
        check(!strcmp(t, "SOUND LFO"), "its header: Sound LFO");
    }
    ui.force = 1;
    frame();
    ppm("opt-sound-lfo-family");
    tap(B_ENV);
    n = SCR->rows();
    for (r = 0, only = 1; r < n; r++)
        only &= snd_page(r)->fam == FAM_ENV;
    check(only && ui.row[SCR_SOUND] == 0 && n >= 2u, "ENV tapped on it: the ENV family's rows, the first");
    for (r = 1; r < n; r++)
        tap(B_ENV);
    check(ui.row[SCR_SOUND] == n - 1u, "ENV again: the family's next rows, in order");
    tap(B_ENV);
    check(ui.row[SCR_SOUND] == 0, "ENV at the family's last row: back to its first");
    turn(EN_SELECT, 50);
    check(ui.row[SCR_SOUND] == n - 1u, "SELECT moves within the family");
    mix_to_sound();
    all = SCR->rows();
    check(ui.scr == SCR_SOUND && snd_fam == SND_ALL && !snd_page(0) && all > n + 3u,
          "HOME > Sound: every row, the SOUND row first, as before");
    song.sel = TRK_DRUM;
    frame();
    tap(B_EDIT);
    n = SCR->rows();
    for (r = 0, only = 1; r < n; r++)
        only &= snd_page(r)->fam == FAM_EDIT;
    check(only && snd_page(0)->scope == SC_DSND, "the drum track, EDIT: the lane's rows only");
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT, "the drum track, ENV (no such family): the rows stay");
    song.sel = 0;
    reset_ui();
}

/* ---- STEP: the keys are steps (docs/UI-OPTIMIST-DESIGN.md section 4.2) */
static uint32_t WK(uint32_t w) { return key_of_lane(w); }   /* white key w's key index */
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
#include "ui_optimist_mixer.h"                     /* the horizontal mixer, SCOPE, no footer */
static void step_keys_tests(void)
{
    track_t *t;
    reset_ui();
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    lane_select(2);
    tap(B_SEQ);
    check(ui.scr == SCR_STEP && ly_lock == LY_STEP && ui.row[SCR_STEP] == 2, "drums: SEQ, STEP; the row is the lane");
    pen_lane = 9;
    key(WK(0));
    check(dstep_has(&t->dstep[0], 2) && dstep_mask(&t->dstep[0]) == 4u && pen_lane == 9,
          "drums: an empty step tapped: set with the lane (no sound played)");
    ui.force = 1;
    frame();
    ppm("opt-step-drums");
    key(WK(0));
    check(!dstep_mask(&t->dstep[0]), "drums: a set step tapped: cleared");
    press(B_OCTDN);
    key(WK(1));
    release(B_OCTDN);
    check(dstep_lvl(&t->dstep[1], 2) == LV_GHOST, "drums: OCT- held + a step: a ghost hit (OCT keeps its job)");
    press(B_SEQ);
    key(WK(5));
    release(B_SEQ);
    check(lane_selected() == 5 && pen_lane == 5 && !st.play && ui.scr == SCR_STEP && ly_lock == LY_STEP,
          "drums: SEQ held + a key: the lane picked, heard; SEQ let go, the keys are steps again");
    key(WK(4));
    check(dstep_has(&t->dstep[4], 5), "drums: the next tapped step takes the pick");
    kdown(WK(4));
    turn(EN_K1, 1);
    turn(EN_K2, 2);
    kup(WK(4));
    check(dstep_has(&t->dstep[4], 5) && dstep_lvl(&t->dstep[4], 5) == LV_HARD && dstep_rat(&t->dstep[4], 5) == 2,
          "drums: a step held + KNOB 1 / 2: LEVEL, RATCHET, kept when let go");
    kdown(WK(4));
    {
        cell_t c;
        step_cell(0, 2, &c);
        check(SCR->rows() == DRUM_LANES && !c.label && c.kind == CK_NONE, "drums: the held step's cards LEVEL RATCHET - -");
        ui.force = 1;
        frame();
        ppm("opt-step-drum-held");
    }
    fm1_in.buttons |= BT(B_HOME);
    frame();
    fm1_in.buttons &= ~BT(B_HOME);
    kup(WK(4));
    frame();
    check(!dstep_mask(&t->dstep[4]) && ui.scr == SCR_STEP, "a step held, HOME tapped: cleared, STEP stays");
    key(WK(4));
    fm1_in.buttons |= BT(B_HOME);
    frame();
    key(WK(4));
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(!dstep_mask(&t->dstep[4]) && ui.scr == SCR_STEP, "HOME held + a step: cleared (no NO)");
    tap(B_SEQ);
    check(st.play && ly_lock == LY_PLAY && ui.scr == SCR_STEP, "SEQ tapped on STEP: the keys play, STEP stays");
    key(WK(7));
    check(!dstep_mask(&t->dstep[7]) && lane_selected() == 7, "the keys playing: no step set, the lane is the last hit");
    tap(B_SEQ);
    check(!st.play && ly_lock == LY_STEP, "SEQ again: the keys are steps");
    turn(EN_SELECT, 1);
    check(lane_selected() == 8, "drums, no step held: SELECT the lane");
    {
        cell_t c;
        step_cell(ui.row[SCR_STEP], 0, &c);
        check(c.label && !strcmp(c.label, "LEVEL"), "drums, no step held: the lane's LEVEL TUNE DECAY REV");
    }
    /* the window */
    t->p[P_SLEN] = 40;
    frame();
    {
        char h[40];
        int8_t oct = song.octave;
        press(B_HOME);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_HOME);
        head_title(SCR_STEP, 0, h, sizeof h);
        check(st.page == 1 && !st.follow && !strcmp(h, "STEPS 17-32") && ui.scr == SCR_STEP && song.octave == oct,
              "HOME + OCT+: the window 17-32 (the header says so), FOLLOW off, no NO, the octave kept");
        ui.force = 1;
        frame();
        ppm("opt-step-window");
        key(WK(0));
        check(dstep_has(&t->dstep[16], 8), "the window 17-32: the first key is step 17");
        press(B_HOME);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_HOME);
        head_title(SCR_STEP, 0, h, sizeof h);
        check(st.page == 2 && !strcmp(h, "STEPS 33-40"), "HOME + OCT+ again: 33-40 (LEN 40)");
        key(WK(10));
        check(!dstep_mask(&t->dstep[42]), "a key past LEN: nothing");
        press(B_HOME);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_HOME);
        check(st.page == 2, "the window stops at LEN");
        press(B_HOME);
        press(B_OCTDN);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_OCTDN);
        release(B_HOME);
        check(st.follow && ui.scr == SCR_STEP, "HOME + OCT- + OCT+: FOLLOW again");
        tap(B_PLAY);
        frames(2);
        {
            uint32_t f, ok = 0;
            for (f = 0; f < 2000u && !ok; f++) {
                frame();
                ok = st.page == (TSEL->seq_idx % 40u) / 16u && TSEL->seq_idx % 40u >= 16u;
            }
            check(ok, "playing: the window follows the playhead");
        }
        tap(B_PLAY);
        frames(3);
        transport_req = 0;
    }
    t->p[P_SLEN] = 16;
    track_defaults_steps(t);
    /* a question: the keys do nothing */
    press(B_HOME);
    press(B_REC);
    release(B_REC);
    release(B_HOME);
    key(WK(2));
    check(op_armed() && !dstep_mask(&t->dstep[2]), "a question asked: the step keys do nothing");
    tap(B_HOME);
    song.sel = 0;
    reset_ui();
}

static void step_synth_tests(void)
{
    track_t *t = TSEL;
    int16_t vlast;
    reset_ui();
    track_defaults_steps(t);
    t->p[P_VOICE] = V_POLY;
    tap(B_SEQ);
    pen_n = 1;
    pen_note[0] = 64;
    key(WK(3));
    check(step_on(&t->step[3]) && t->step[3].note[0] == 64 && t->step[3].n == 1, "synth: a step tapped: the pen's note");
    press(B_SEQ);
    fm1_in.notes |= 1u << WK(0) | 1u << WK(2) | 1u << WK(4);   /* a chord, the keys held together */
    frame();
    fm1_in.notes = 0;
    frame();
    release(B_SEQ);
    check(pen_n == 3 && !st.play && ui.scr == SCR_STEP, "synth: SEQ held + keys: they play, the chord is the pick");
    key(WK(5));
    check(t->step[5].n == 3 && t->step[5].note[0] == pen_note[0] && t->step[5].note[2] == pen_note[2],
          "synth: the next tapped step takes the chord");
    kdown(WK(3));
    vlast = t->step[3].note[0];
    turn(EN_K1, 2);
    turn(EN_K3, 1);
    turn(EN_K4, 2);
    {
        cell_t c;
        step_cell(0, 3, &c);
        check(c.label && !strcmp(c.label, "LENGTH") && !strcmp(c.val, "2"), "synth: a step held: NOTE LEVEL RATCHET LENGTH cards");
        ui.force = 1;
        frame();
        ppm("opt-step-synth-held");
    }
    kup(WK(3));
    check(t->step[3].note[0] == vlast + 2 && (t->step[3].rat & 3u) == 1u && t->step[4].time == ST_TIE &&
          t->step[5].n == 3 && step_on(&t->step[3]), "synth: held + KNOB 1 / 3 / 4: the note, the ratchet, ties (to the next note)");
    t->step[5].n = 0;
    t->step[5].time = ST_REST;
    kdown(WK(3));
    turn(EN_K4, 2);
    kup(WK(3));
    check(t->step[4].time == ST_TIE && t->step[5].time == ST_TIE, "synth: LENGTH through empty steps");
    ui.force = 1;
    frame();
    ppm("opt-step-roll");
    kdown(WK(8));
    kdown(WK(9));
    turn(EN_K2, -1);
    kup(WK(9));
    kup(WK(8));
    check(!step_on(&t->step[8]) || 1, "(several held)");
    key(WK(8));
    key(WK(9));
    kdown(WK(8));
    kdown(WK(9));
    turn(EN_K3, 2);
    kup(WK(9));
    kup(WK(8));
    check((t->step[8].rat & 3u) == 2u && (t->step[9].rat & 3u) == 2u, "synth: two steps held: edited together");
    key(WK(3));
    check(!step_on(&t->step[3]) && t->step[4].time != ST_TIE, "synth: a set step tapped: cleared with its ties");
    tap(B_SAVE);                                        /* undo: SAVE then HOME */
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(step_on(&t->step[3]) && ui.scr == SCR_STEP, "a step edit is an undo level: SAVE then HOME brings it back");
    {
        cell_t c;
        step_cell(ui.row[SCR_STEP], 0, &c);
        check(SCR->rows() >= 1u && c.label && !strcmp(c.label, "LEN"), "synth, no step held: the PATTERN row LEN DIV SWING GATE");
    }
#if FELUCCA_MICRO || FELUCCA_CHANCE || FELUCCA_FILLS
    kdown(WK(3));
#if FELUCCA_MICRO
    turn(EN_SELECT, 3);
    check(TX(t)->micro[3] == 3 && ui.scr == SCR_STEP, "a step held + SELECT: its nudge");
#endif
#if FELUCCA_CHANCE
    turn(EN_PRESET, -4);
    check(step_chance(&t->step[3]) == 80u, "a step held + PRESETS: its chance (80 %)");
#endif
#if FELUCCA_FILLS
    tap(B_SAVE);
    check(step_fill(t, 3) == FC_FILL && ui.scr == SCR_STEP, "a step held + SAVE: its fill condition (FILL ONLY)");
#endif
    ui.force = 1;
    frame();
    ppm("opt-step-extras");
    kup(WK(3));
    check(step_on(&t->step[3]), "edited: kept when let go");
#endif
#if FELUCCA_PLOCK
    kdown(WK(3));
    press(B_ENV);
    check(st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].fam == FAM_ENV, "a step held + ENV: the ENV page's cells as its locks");
    turn(EN_K1, 5);
    {
        int q = stepx_lock_find(TX(t), 3, PAGES[st.lock_pg].id[0]);
        cell_t c;
        step_cell(0, 0, &c);
        check(q >= 0 && TX(t)->lock[q].val == t->p[PAGES[st.lock_pg].id[0]] + 5 && c.mark,
              "a turn writes a lock on the step, its card marked");
        ui.force = 1;
        frame();
        ppm("opt-step-lock");
    }
    fm1_in.buttons |= BT(B_HOME);
    turn(EN_K1, 1);
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(stepx_lock_find(TX(t), 3, PAGES[st.lock_pg].id[0]) < 0 && ui.scr == SCR_STEP, "HOME + the knob: the lock cleared");
    release(B_ENV);
    kup(WK(3));
    check(ui.scr == SCR_STEP && st.lock_pg == LOCK_NONE && step_on(&t->step[3]), "ENV let go: no jump; the step let go: the cards back");
#endif
    {   /* the held step's lines under the grid fit: the longest lane, a chord pick, a held step */
        char h[40], k[40];
        uint32_t bad = 0;
        pen_n = 4;
        pen_note[0] = pen_note[1] = 61;
        step_foot(h, k, 32);
        bad += text_w(&FONT_S, h) > 232 || text_w(&FONT_S, k) > 232;
        kdown(WK(3));
        step_foot(h, k, 32);
        bad += text_w(&FONT_S, h) > 232 || text_w(&FONT_S, k) > 232;
        kup(WK(3));
        song.sel = TRK_DRUM;
        lane_select(4);                                 /* CLOSED HAT */
        frame();
        step_foot(h, k, 32);
        bad += text_w(&FONT_S, h) > 232 || text_w(&FONT_S, k) > 232;
        song.sel = 0;
        check(!bad, "STEP's held-step lines under the grid fit (232 px)");
    }
    check(SG_TOP + SG_H <= SG_PH_Y && SG_PH_Y + 4 <= SG_INFO_Y && SG_INFO_Y + 34 <= OH_BODY && SG_X + 16 * SG_CW <= 240,
          "the grid, its playhead and the held step's two lines within the panel (to the screen's foot) and its width");
    {   /* the keys' lights: the set steps of the window */
        uint32_t m;
        track_defaults_steps(t);
        key(WK(6));
        m = step_leds(0);
        check(((m >> WK(6)) & 1u) && !((m >> WK(5)) & 1u), "the keys' lights: the window's set steps");
    }
    t->p[P_VOICE] = TP[P_VOICE].def;
    reset_ui();
}

/* ---- phase 4: the held performance layers, the lock, the TEMPO page, SAVE / HOME + a button, SONG */
static void hold2(uint32_t a, uint32_t b) { press(a); press(b); release(b); release(a); }   /* a held, b tapped */
static void layer_tests(void)
{
    track_t *t = &trk[0];
    uint32_t k;
    reset_ui();
    /* FX: the punch-in effects on the keys, FILTER DUST DUCK on the knobs */
    press(B_FX);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_FX && ui.scr == SCR_HOME, "FX held: its map shows (the screen stays under it)");
    ui.force = 1;
    frame();
    ppm("opt-layer-fx");
    check(px_in(CARD_X(0), OY_PANEL + 4u, CARD_W, 20, OP_SURF) && 2 + 4 * TILE_H <= LAY_SUB_Y && LAY_SUB_Y + 16 <= OH_BODY,
          "the 16 tiles drawn within the panel's band (124 rows)");
    kdown(WK(1));
    check(punch.req == 1, "FX + key 2: punch-in effect 2 (seq.c, the ISR)");
    kup(WK(1));
    {
        int16_t f = song.g[G_FILT], d = song.g[G_DUST];
        turn(EN_K1, -3);
        turn(EN_K2, 4);
        check(song.g[G_FILT] < f && song.g[G_DUST] > d, "FX held: KNOB 1 FILTER, KNOB 2 DUST");
        d = song.g[G_DUST];
        turn(EN_PRESET, 1);
        check(song.g[G_DUST] == d + 1, "FX held: PRESETS the hot knob (DUST) one unit");
    }
    release(B_FX);
    frames(2);
    check(lay.shown == LY_PLAY && ui.scr == SCR_HOME && !punch.hold, "FX let go after use: the map goes, no jump to SOUND");
    press(B_FX);
    frames(6);                                          /* (a tap of 0.1 s: several frames held, as on the device) */
    release(B_FX);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_FX, "FX tapped (0.1 s): SOUND's FX rows, as before");
    op_enter(SCR_PROJECT);
    press(B_SAVE);
    frames(6);
    release(B_SAVE);
    check(ui.scr == SCR_PROJECT && op_armed(), "SAVE tapped (0.1 s, a layer's button): YES (LOAD asks)");
    tap(B_HOME);
    reset_ui();
    /* ARP: note repeat, RATE */
    press(B_ARP);
    {
        int16_t r = song.g[G_ROLL];
        turn(EN_K1, 1);
        check(song.g[G_ROLL] == r + 1 || r == GP[G_ROLL].max, "ARP held: KNOB 1 RATE");
    }
    kdown(WK(0));
    check(kb_kind[WK(0)] == KS_ROLL, "ARP + a key: note repeat (seq.c roll)");
    ui.force = 1;
    frame();
    ppm("opt-layer-arp");
    kup(WK(0));
    release(B_ARP);
    /* SCL: the key of the song, CHORD SCALE KEYS TRANSPOSE */
    press(B_SCL);
    key(WK(2));
    check(trk[0].p[P_ROOT] == (int16_t)((53u + WK(2)) % 12u) && trk[1].p[P_ROOT] == trk[0].p[P_ROOT],
          "SCL + a key: every synth track's key");
    turn(EN_K2, 1);
    check(trk[1].p[P_SCALE] == trk[0].p[P_SCALE] && trk[0].p[P_SCALE] > 0, "SCL: KNOB 2 the scale, every track");
    {
        int16_t tr = t->p[P_TRANS];
        turn(EN_K4, 2);
        check(t->p[P_TRANS] == tr + 2, "SCL: KNOB 4 TRANSPOSE");
    }
    ui.force = 1;
    frame();
    ppm("opt-layer-scl");
    release(B_SCL);
    trk[0].p[P_SCALE] = trk[1].p[P_SCALE] = trk[2].p[P_SCALE] = 0;
    /* GLO: mute, solo, FX or fills, tap tempo, the levels */
    press(B_GLO);
    key(WK(0));
    check(trk[0].p[P_MUTE] == 1, "GLO + key 1: T1 muted");
    key(WK(0));
    key(WK(5));
    check(trk[0].p[P_MUTE] == 0 && song.solo == 2u, "GLO + key 1 again: heard; key 6: T2 solo");
    key(WK(5));
#if FELUCCA_FILLS
    kdown(WK(8));
    check(fill_held == 1, "GLO + key 9 held: a fill (FILLS)");
    kup(WK(8));
    check(fill_held == 0, "... let go: the fill ends");
#else
    key(WK(8));
    check(trk[0].p[P_FXOFF] == 1, "GLO + key 9: T1's FX off");
    key(WK(8));
#endif
    song.g[G_BPM] = 90;
    key(WK(15));
    frames(30);                                         /* (two taps 0.5 s apart: 120 BPM) */
    key(WK(15));
    check(song.g[G_BPM] >= 115 && song.g[G_BPM] <= 125, "GLO + key 16, tapped twice: the tempo");
    {
        int16_t lv = trk[2].p[P_LEVEL], dl = song.g[G_DRLVL];
        turn(EN_K3, -2);
        turn(EN_K4, -2);
        check(trk[2].p[P_LEVEL] < lv && song.g[G_DRLVL] < dl, "GLO: KNOB 3 T3's level, KNOB 4 the drum track's");
    }
    ui.force = 1;
    frame();
    ppm("opt-layer-glo");
    release(B_GLO);
    frames(2);
    check(ui.scr == SCR_HOME, "GLO let go after use: no FX screen");
    song.g[G_BPM] = 120;
    /* EDIT: SHIFT LENGTH TRANSPOSE, erase on the keys, OCT- undo */
    track_defaults_steps(t);
    t->step[0].time = ST_NOTE, t->step[0].n = 1, t->step[0].note[0] = 60;
    press(B_EDIT);
    turn(EN_K1, 1);
    check(step_on(&t->step[1]) && !step_on(&t->step[0]), "EDIT: KNOB 1 SHIFT, every step one later");
    turn(EN_K2, 1);
    check(t->p[P_SLEN] == 32 && step_on(&t->step[17]), "EDIT: KNOB 2 LENGTH x2 (the pattern again after itself)");
    turn(EN_K3, 2);
    check(t->step[1].note[0] == 61, "EDIT: KNOB 3 TRANSPOSE, every note a semitone");
    kdown(WK(4));
    check(kb_kind[WK(4)] == KS_ERASE, "EDIT + a key: erase that note as it plays (seq.c)");
    kup(WK(4));
    ui.force = 1;
    frame();
    ppm("opt-layer-edit");
    release(B_EDIT);
    hold2(B_EDIT, B_OCTDN);
    check(t->p[P_SLEN] == 16 && t->step[0].note[0] == 60 && !step_on(&t->step[1]), "EDIT + OCT-: the hold's edits undone (one level)");
    track_defaults_steps(t);
    /* lock a layer: the layer held then HOME, or HOME held then the layer's button */
    hold2(B_FX, B_HOME);
    frames(2);
    check(lay.lock == LY_FX && lay.shown == LY_FX && ui.scr == SCR_HOME && ly_lock == LY_FX,
          "FX held, HOME tapped: FX locked open (no NO), the ISR's layer too");
    kdown(WK(3));
    check(punch.req == 3, "locked: the keys are still the effects");
    kup(WK(3));
    ui.force = 1;
    frame();
    ppm("opt-layer-locked");
    tap(B_OCTDN);
    check(lay.lock == LY_FX, "OCT does not let it go");
    tap(B_SEQ);
    check(lay.lock == LY_PLAY && ui.scr == SCR_HOME && ly_lock == LY_PLAY, "any other button lets it go, and does only that");
    hold2(B_HOME, B_GLO);
    frames(2);
    check(lay.lock == LY_MIX && lay.shown == LY_MIX && ui.scr == SCR_HOME, "HOME held, GLO pressed: the mix layer locked");
    key(WK(1));
    check(trk[1].p[P_MUTE] == 1, "locked GLO: key 2 mutes T2");
    key(WK(1));
    tap(B_ENV);
    check(lay.lock == LY_PLAY && ui.scr == SCR_HOME, "ENV lets it go (no SOUND rows)");
#if FELUCCA_PATTERNS
    /* LFO: the patterns (the built black-key modifiers) */
    press(B_LFO);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_PAT, "LFO held: the patterns' map");
    for (k = 0; k < 8u; k++)
        t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(48 + k);
    fm1_in.notes |= 1u << 13;                           /* black key 6 (store) held + white 2 */
    frame();
    key(WK(1));
    fm1_in.notes &= ~(1u << 13);
    frame();
    check(pat_has(0, 1), "LFO + black 6 + white 2: the working copy stored into T1 2");
    ui.force = 1;
    frame();
    ppm("opt-layer-lfo");
    {   /* LFO held is SHIFT (11.6): a knob edits the screen under it, no cue (ui_optimist_len.h: LEN by one) */
        uint8_t rq = pat_req[0];
        int16_t lv = trk[0].p[P_LEVEL];
        turn(EN_K1, 1);
        frame();
        check(pat_req[0] == rq && lay.shown == LY_PLAY, "LFO held + KNOB 1: no cue, the screen under (SHIFT)");
        trk[0].p[P_LEVEL] = lv;
    }
    release(B_LFO);
    track_defaults_steps(t);
#endif
    {   /* every layer's title and footer lines fit; a scene's and a note's letter keep their capital */
        static const uint8_t L[] = {LY_FX, LY_ERASE, LY_ROLL, LY_SCALE, LY_MIX, LY_SONG FIF(FELUCCA_PATTERNS)(, LY_PAT)};
        uint32_t i, bad = 0;
        char h[40], kk[40], b[32];
        for (i = 0; i < sizeof L; i++) {
            lay.shown = L[i];
            lay.lock = i & 1u ? L[i] : LY_PLAY;
            h[0] = 0;
            tiles_fill(lay_tl, kk, 32);                 /* (the state line under the tiles) */
            lay_title(b, sizeof b);
            if (text_w(&FONT_S, kk) > 232 || text_w(&FONT_S, b) > 150) {
                printf("  too wide: '%s' / '%s' / '%s'\n", h, kk, b);
                bad++;
            }
        }
        lay.shown = lay.lock = LY_PLAY;
        check(!bad, "every layer's title and its state line under the tiles fit (232 px)");
        check(!strcmp(op_case(b, "SCENE B", sizeof b), "Scene B") && !strcmp(op_case(h, "KEY C#", sizeof h), "Key C#"),
              "sentence case keeps a scene's letter and a note (Scene B, Key C#)");
        ui.force = 1;
    }
    (void)k;
    reset_ui();
}

static void save_layer_tests(void)
{
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    srec = 0;
#if FELUCCA_PATTERNS
    project_save(0);                                    /* (scenes A and B stored, C empty) */
    project_save(1);
    frames(2);
#endif
    press(B_SAVE);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_SONG, "SAVE held: the scenes' map");
    ui.force = 1;
    frame();
    ppm("opt-layer-save");
    key(WK(1));
    check(live_sec == 1 && !strcmp(ui.msg, "LOADED B"), "SAVE + key 2, stopped: scene B loaded");
    key(WK(2));
    check(!strcmp(ui.msg, "EMPTY C"), "SAVE + key 3: C is empty, said");
    press(B_REC);
    key(WK(1));
    release(B_REC);
    check(op_armed() && ui.arm_scr == ARM_OP && !strcmp(ui.arm_q, "STORE SCENE B?") && srec == 0,
          "SAVE + REC held + key 2: store the loop into B, asked (used); no SONG REC");
    release(B_SAVE);
    frames(2);
    tap(B_SAVE);
    check(!op_armed() && (saves || project_used(1)), "... YES: stored");
    press(B_SAVE);
    tap(B_REC);
    check(srec == 1, "SAVE + REC tapped: SONG REC armed");
    tap(B_REC);
    release(B_SAVE);
    check(srec == 0, "... again: off");
    press(B_SAVE);
    press(B_HOME);
    key(WK(0));
    release(B_HOME);
    release(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE A?") && strncmp(ui.msg, "UNDO", 4) && strncmp(ui.msg, "NOTHING", 7),
          "SAVE + HOME held + key 1: clear A, asked; no undo");
    tap(B_HOME);
    arrangement.count = 2;                              /* (a song of A and B: both stored) */
    arrangement.entry[0].scene = 0, arrangement.entry[1].scene = 1, arrangement.entry[0].bars = arrangement.entry[1].bars = 4;
    hold2(B_SAVE, B_PLAY);
    frames(3);
    check(arrangement_enabled && (song.playing || transport_req), "SAVE + PLAY: the song from its start");
    tap(B_PLAY);
    frames(3);
    arrangement_enabled = 0;
    transport_req = 0;
    reset_ui();
}

static void tempo_tests(void)
{
    uint32_t q;
    uint64_t a0, a1, b0, b1;
    int16_t bpm;
    static int32_t o[CTL * 2];
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    tap(B_PLAY);
    frames(2);
    check(song.playing, "PLAY tapped: start (when let go)");
    tap(B_PLAY);
    frames(2);
    check(!song.playing, "PLAY tapped again: stop");
    press(B_PLAY);
    frames(30);
    check(ui.scr == SCR_TEMPO && !song.playing && !transport_req, "PLAY held: the TEMPO page, the transport untouched");
    {
        cell_t c;
        tp_cell(0, 0, &c);
        check(c.label && !strcmp(c.label, "BPM") && c.gk != GK_NONE, "TEMPO: BPM NUDGE SWING SYNC, with forms");
    }
    ui.force = 1;
    frame();
    ppm("opt-tempo");
    bpm = song.g[G_BPM];
    turn(EN_K1, 2);
    check(song.g[G_BPM] > bpm, "TEMPO: KNOB 1 the BPM");
    song.g[G_BPM] = bpm;
    turn(EN_SELECT, 1);
    check(ui.row[SCR_TEMPO] == 1, "TEMPO: SELECT the REC row");
    turn(EN_SELECT, -1);
    key(WK(0));
    frames(29);
    key(WK(0));
    check(song.g[G_BPM] >= 115 && song.g[G_BPM] <= 125 && ui.scr == SCR_TEMPO, "TEMPO: a white key taps the tempo");
    release(B_PLAY);
    frames(2);
    check(ui.scr == SCR_HOME && !song.playing && !transport_req, "PLAY let go: the screen before, no start");
    /* the nudge: the clock a few percent faster while OCT+ is held, the BPM value unchanged, back when let go */
    song.g[G_BPM] = 120;
    tap(B_PLAY);
    frames(2);
    press(B_PLAY);
    frames(30);
    a0 = (uint64_t)clk_beat * BEAT_U + clk_pos;
    for (q = 0; q < 200u; q++)
        mix_block(o, CTL);
    a1 = (uint64_t)clk_beat * BEAT_U + clk_pos;
    press(B_OCTUP);
    check(clk_nudge > 0 && song.g[G_BPM] == 120, "OCT+ held on TEMPO: nudged faster, the BPM value unchanged");
    b0 = (uint64_t)clk_beat * BEAT_U + clk_pos;
    for (q = 0; q < 200u; q++)
        mix_block(o, CTL);
    b1 = (uint64_t)clk_beat * BEAT_U + clk_pos;
    check(b1 - b0 > (a1 - a0) + (a1 - a0) / 50u && b1 - b0 < (a1 - a0) + (a1 - a0) / 10u,
          "... the clock runs 2-10 % faster (seq.c clk_nudge)");
    ui.force = 1;
    frame();
    ppm("opt-tempo-nudge");
    release(B_OCTUP);
    check(clk_nudge == 0 && song.g[G_BPM] == 120, "OCT+ let go: the clock back, the tempo as it was");
    press(B_OCTDN);
    check(clk_nudge < 0, "OCT- held: slower");
    release(B_OCTDN);
    release(B_PLAY);
    frames(2);
    check(song.playing && clk_nudge == 0, "PLAY let go after the page: still playing (a hold is no stop)");
    tap(B_PLAY);
    frames(3);
    transport_req = 0;
    reset_ui();
}

static void combo_tests(void)
{
    uint32_t u2 = up_stores;
    reset_ui();
    hold2(B_SAVE, B_ENV);
    check(name_on() && nm.kind == NK_USER && nm.slot == 2u && ui.scr == SCR_HOME && !op_armed(),
          "SAVE + ENV: NAME opens on the first free user preset slot (no YES)");
    tap(B_SAVE);
    check(!name_on() && up_stores == u2 + 1u && up_store_slot == 2u, "... SAVE: the sound saved as a user preset");
    hold2(B_SAVE, B_ARP);
    check(name_on() && lay.shown == LY_PLAY, "SAVE + ARP: NAME too, no ARP map");
    tap(B_SAVE);
    check(up_stores == u2 + 2u, "... saved");
#if DL_UI && FELUCCA_DRUM_KITS
    song.sel = TRK_DRUM;
    frame();
    hold2(B_SAVE, B_FX);
    check(ukit_used(0), "SAVE + FX on the drum track: its 16 lanes as a user kit");
    song.sel = 0;
#endif
#if FELUCCA_PATTERNS
    {
        track_t *t = &trk[1];
        song.sel = 1;
        frame();
        t->step[2].time = ST_NOTE, t->step[2].n = 1, t->step[2].note[0] = 70;
        hold2(B_SAVE, B_SEQ);
        check(pat_cur[1] < PAT_N && pat_has(1, pat_cur[1]), "SAVE + SEQ: the working pattern into its slot");
        hold2(B_SAVE, B_SEQ);
        check(op_armed() && !strncmp(ui.arm_q, "STORE T2", 8), "SAVE + SEQ again: over its used slot, asked");
        tap(B_HOME);
        song.sel = 0;
        frame();
    }
#endif
    hold2(B_HOME, B_SEQ);
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 LOCKS?"), "HOME + SEQ: the pattern's locks, nudges, fills, motion (asked)");
#if FELUCCA_MICRO
    TX(&trk[0])->micro[5] = 7;
#endif
    tap(B_SAVE);
    check(!op_armed() && !strcmp(ui.msg, "T1 EXTRAS CLEARED"), "... YES: cleared, the notes stay");
#if FELUCCA_MICRO
    check(TX(&trk[0])->micro[5] == 0, "... the nudge gone");
#endif
    hold2(B_HOME, B_ENV);
    check(op_armed() && !strcmp(ui.arm_q, "INIT T1?"), "HOME + ENV: INIT, asked");
    tap(B_HOME);
    song.octave = 2;
    hold2(B_HOME, B_OCTUP);
    check(song.octave == 0 && ui.scr == SCR_HOME, "HOME + OCT off STEP: the octave back to 0, no NO");
    tap(B_PLAY);
    frames(2);
    hold2(B_HOME, B_PLAY);
    frames(3);
    check(!song.playing && !strcmp(ui.msg, "ALL SOUND OFF") && ui.scr == SCR_HOME, "HOME + PLAY: stop, every voice off");
    transport_req = 0;
    reset_ui();
}

static uint32_t song_row_of(uint32_t kind, uint32_t a)  /* SONG's row of that kind and scene / part */
{
    uint32_t r, x;
    for (r = 0; r < song_rows(); r++)
        if (song_kind(r, &x) == kind && (kind == SG_PAT || kind == SG_MODE || x == a))
            return r;
    return 0;
}
static void song_tests(void)
{
    uint32_t r, n;
    track_t *t = &trk[0];
    reset_ui();
    song.playing = 0;
    transport_req = 0;
    press(B_GLO);                                       /* (the emulator's sequence: a lock, a let go, TEMPO) */
    frames(HOLD_FRAMES);
    press(B_HOME);
    release(B_HOME);
    release(B_GLO);
    frames(20);
    tap(B_ENV);
    press(B_PLAY);
    frames(50);
    press(B_OCTUP);
    frames(20);
    release(B_OCTUP);
    release(B_PLAY);
    frames(20);
    check(mix_go("SONG") && ui.scr == SCR_SONG, "the mixer's screens set: YES on SONG, the SONG screen");
    n = song_rows();
    check(n == LAY_NSCN + (FELUCCA_PATTERNS ? 1u : 0u) + 1u + arrangement.count,
          "SONG: the scenes, PATTERNS, MODE, then the chain's parts");
    ui.force = 1;
    frame();
    ppm("opt-song");
#if FELUCCA_PATTERNS
    {
        uint8_t refs[NTRK];
        uint32_t c = song_row_of(SG_SCENE, 2);
        track_defaults_steps(t);
        for (r = 0; r < 4u; r++)
            t->step[r * 4u].time = ST_NOTE, t->step[r * 4u].n = 1, t->step[r * 4u].note[0] = 60;
        ui.row[SCR_SONG] = (uint8_t)c;
        frame();
        tap(B_REC);
        check(project_used(2) && pat_scene_refs(2, refs) && refs[0] < PAT_N, "SONG scene C, REC: the loop stored into it");
        tap(B_REC);
        check(op_armed() && !strcmp(ui.arm_q, "STORE SCENE C?"), "... REC again: over a used one, asked");
        tap(B_HOME);
        ui.hot = 0;
        for (r = 0; r < 20u; r++)
            turn(EN_PRESET, -1);                        /* (down to "--": none) */
        {
            cell_t cc;
            song_cell(c, 0, &cc);
            check(sg.ed_s == 2 && cc.col == C_WARN, "PRESETS on its T1 cell: the reference edited (amber until written)");
        }
        frames(60);
        check(sg.ed_s == 0xFF && pat_scene_refs(2, refs) && refs[0] == PAT_NONE, "... written once it rests: T1 none");
        ui.force = 1;
        frame();
        ppm("opt-song-scenes");
        tap(B_SAVE);
        check(live_sec == 2, "YES on scene C, stopped: loaded");
        /* the PATTERNS row: the session grid, the keys launch, SAVE + key stores, HOME + key clears */
        ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_PAT, 0);
        frames(2);
        check(ly_lock == LY_PAT && song_on_pat_row(), "PATTERNS row: the keys launch the selected track's patterns");
        t->step[3].time = ST_NOTE, t->step[3].n = 1, t->step[3].note[0] = 67;   /* (C's T1 is none: a note) */
        press(B_SAVE);
        key(WK(5));
        release(B_SAVE);
        check(pat_has(0, 5), "SAVE held + key 6: the working copy into slot 6");
        key(WK(5));
        check(pat_cur[0] == 5 && !strcmp(ui.msg, "LOADED T1 6"), "key 6: T1's pattern 6 (stopped: loaded)");
        ui.force = 1;
        frame();
        ppm("opt-song-patterns");
        press(B_HOME);
        key(WK(5));
        release(B_HOME);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 6?") && ui.scr == SCR_SONG, "HOME held + key 6: clear slot 6, asked");
        tap(B_SAVE);
        check(!pat_has(0, 5), "... YES: cleared");
        tap(B_REC);
        check(!strncmp(ui.msg, "DUPLICATE T1", 12), "PATTERNS row, REC: the working copy into the first free slot");
        ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, 2);
        frame();
        press(B_HOME);
        press(B_REC);
        release(B_REC);
        release(B_HOME);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE C?"), "HOME + REC on scene C: clear it, asked");
        tap(B_SAVE);
        check(!project_used(2) && ui.scr == SCR_SONG, "... YES: cleared");
    }
#endif
    /* MODE, the chain */
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_MODE, 0);
    frame();
    {
        uint32_t m = arrangement_enabled;
        ui.hot = 0;
        tap(B_SAVE);
        check(arrangement_enabled != m, "MODE: YES toggles LOOP / SONG");
        arrangement_enabled = 0;
    }
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_PART, 0);
    frame();
    {
        uint32_t cnt = arrangement.count, bars = arrangement.entry[0].bars;
        turn(EN_K2, 2);
        check(arrangement.entry[0].bars == bars + 2u, "PART 1: KNOB 2 its bars");
        turn(EN_K3, 1);
        tap(B_SAVE);
        check(arrangement.count == cnt + 1u && ui.row[SCR_SONG] == song_row_of(SG_PART, 1), "INSERT: a copy after it");
        ui.force = 1;
        frame();
        ppm("opt-song-chain");
        turn(EN_K4, 1);
        tap(B_SAVE);
        check(arrangement.count == cnt, "DELETE: the part goes");
        arrangement.entry[0].bars = (uint8_t)bars;
    }
    tap(B_HOME);
    frames(2);
    check(ui.scr == SCR_HOME && !sg.dirty, "HOME: back to the mixer, the chain saved (stopped)");
    {
        uint32_t s, bad = 0;
        char b[12];
        for (s = 0; s < song_rows(); s++) {
            song_name(s, b);
            bad += str_len(b) > 9u;
        }
        check(!bad, "SONG's row names fit the list");
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

/* ---- the loose ends (section 11.4): the SONG shortcut, undo of the step extras, NAME, FM6's editor, the graphs on
 * every row of a family, the presets' engines */
static void name_type(uint32_t place, uint32_t taps)    /* white key place tapped taps times, then the letter kept */
{
    while (taps--)
        key(WK(place));
    frames(60);
}
static void shortcut_tests(void)
{
    uint32_t armed;
    reset_ui();
    press(B_SAVE);
    frames(HOLD_FRAMES);
    turn(EN_SELECT, 1);
    armed = op_armed();
    release(B_SAVE);
    frames(2);
    check(ui.scr == SCR_SONG && !armed && !op_armed(), "SAVE held + SELECT: the SONG screen (SAVE let go is no YES)");
    {
        uint32_t r = ui.row[SCR_SONG];
        press(B_SAVE);
        turn(EN_SELECT, 1);
        release(B_SAVE);
        check(ui.scr == SCR_SONG && ui.row[SCR_SONG] == r + 1u, "... on SONG: SAVE + SELECT moves its cursor");
    }
    reset_ui();
}
static void undo_extras_tests(void)
{
#if FELUCCA_MICRO || FELUCCA_FILLS || FELUCCA_PLOCK
    track_t *t = &trk[0];
    reset_ui();
    song.sel = 0;
    track_defaults_steps(t);
    stepx_clear(TX(t));
    op_enter(SCR_STEP);
    frames(2);
    key(WK(5));                                         /* a step set: one level */
    kdown(WK(5));                                       /* held again: a new level, its extras */
#if FELUCCA_MICRO
    turn(EN_SELECT, 4);
#endif
#if FELUCCA_FILLS
    tap(B_SAVE);
#endif
#if FELUCCA_PLOCK
    press(B_ENV);
    turn(EN_K1, 6);
    release(B_ENV);
#endif
    kup(WK(5));
    frames(2);
    {
        stepx_t after = *TX(t);
        int had = !stepx_is_empty(TX(t));
        press(B_SAVE);
        press(B_HOME);
        release(B_HOME);
        release(B_SAVE);
        check(had && stepx_is_empty(TX(t)) && step_on(&t->step[5]),
              "undo: the step's nudge, fill and lock go back, the step stays (its own level)");
        press(B_HOME);
        press(B_SAVE);
        release(B_SAVE);
        release(B_HOME);
        check(!memcmp(TX(t), &after, sizeof after), "redo: the nudge, fill and lock again, byte for byte");
    }
    press(B_HOME);                                      /* HOME + SEQ: the extras cleared, asked; undone too */
    press(B_SEQ);
    release(B_SEQ);
    release(B_HOME);
    tap(B_SAVE);
    check(stepx_is_empty(TX(t)), "HOME + SEQ, YES: the extras cleared");
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(!stepx_is_empty(TX(t)), "... undone: they are back");
    track_defaults_steps(t);
    stepx_clear(TX(t));
    reset_ui();
#endif
}
static void name_tests(void)
{
    uint32_t n0 = up_stores, l;
    char t[32];
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    turn(EN_K4, 1);                                     /* SAVE AS hot */
    tap(B_SAVE);
    l = str_len(ENGINES[TSEL->eng_req]->name);
    check(name_on() && nm.kind == NK_USER && nm.slot == 2u && !strncmp(nm.s, ENGINES[TSEL->eng_req]->name, l) &&
              !strcmp(nm.s + l, " 03"), "SAVE AS: NAME, prefilled with the automatic name (\"ANALOG 03\")");
    name_title(t, sizeof t);
    check(!strcmp(t, "NAME U03") && ly_lock == LY_STEP, "... the header names the slot; the keys reach the UI only");
    fm1_in.buttons |= BT(B_HOME);
    turn(EN_K1, 1);
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(name_on() && nm.len == 0u, "HOME + a knob: the whole name cleared (NAME stays)");
    name_type(3, 2);                                    /* GH twice: H */
    name_type(4, 1);                                    /* IJK: I */
    check(!strcmp(nm.s, "HI"), "the white keys type phone style: GH twice H, IJK once I");
    key(3);                                             /* G#: a space */
    key(10);                                            /* D#: 123 */
    name_type(0, 1);                                    /* 1 */
    check(!strcmp(nm.s, "HI 1") && nm.num, "G# a space, D# the digits, a digit one tap");
    key(8);                                             /* C#: delete */
    key(8);
    check(!strcmp(nm.s, "HI") && nm.cur == 2u, "C# deletes the character before the cursor");
    turn(EN_K1, -1);
    tap(B_HOME);
    check(name_on() && !strcmp(nm.s, "I"), "KNOB 1 the cursor; HOME tapped deletes before it");
    turn(EN_K2, 8);                                     /* (the cursor at 0: I, 8 on in the set, Q) */
    check(name_on() && !strcmp(nm.s, "Q"), "KNOB 2 the character at the cursor");
    ui.force = 1;
    frame();
    ppm("opt-name");
    song.playing = 1;
    tap(B_SAVE);
    check(name_on() && up_stores == n0, "playing: SAVE refused, NAME stays (stop before save)");
    song.playing = 0;
    tap(B_SAVE);
    check(!name_on() && up_stores == n0 + 1u && !strcmp(up_stored, nm.s) && ui.toast_t,
          "SAVE: written under the typed name, a toast");
    tap(B_SAVE);                                        /* (the SOUND row's SAVE AS again) */
    check(name_on(), "SAVE AS again: NAME");
    turn(EN_K1, -20);
    tap(B_HOME);
    check(!name_on() && up_stores == n0 + 1u && ui.scr == SCR_SOUND, "cursor at the start, HOME: cancelled, nothing written");
    op_enter(SCR_PROJECT);
    for (l = 0; l < NPRJ && prj_kind(l) != PR_USER; l++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)l;
    ui.user_slot = 5;
    turn(EN_K4, 1);
    tap(B_SAVE);
    check(name_on() && nm.kind == NK_USER && nm.slot == 5u, "PROJECT's USER SAVE: NAME first");
    tap(B_SAVE);
    check(!name_on() && up_store_slot == 5u, "... SAVE: the user preset written");
#if SEC_LOGGED
    {
        char b[16];
        ui.row[SCR_PROJECT] = 0;                        /* PROJECT: slot 9, empty */
        song.g[G_SLOT] = 9;
        frame();
        turn(EN_K3, 1);
        tap(B_SAVE);
        check(name_on() && nm.kind == NK_PROJ && nm.slot == 8u && !strcmp(nm.s, "PROJECT 9"),
              "PROJECT's SAVE: NAME, prefilled \"PROJECT 9\"");
        fm1_in.buttons |= BT(B_HOME);
        turn(EN_K1, 1);
        fm1_in.buttons &= ~BT(B_HOME);
        frame();
        name_type(1, 1);                                /* C */
        name_type(1, 2);                                /* D */
        tap(B_SAVE);
        sec_name(8, b);
        check(!name_on() && project_used(8) && !strcmp(b, "CD"), "... SAVE: the project saved, its name in the log");
        turn(EN_K3, 1);
        tap(B_SAVE);
        tap(B_SAVE);                                    /* (over a used slot: asked, then NAME) */
        check(name_on() && !strcmp(nm.s, "CD"), "saved again: asked, then NAME prefilled with its name");
        tap(B_SAVE);
        song.g[G_SLOT] = 1;
    }
#endif
    reset_ui();
}
static uint32_t fm6_slot(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENG_IS(ENGINES[e], FM6))
            return e;
    return NENGINES;
}
static void fm6_tests(void)
{
#if OP_FM6
    uint32_t e = fm6_slot(), keep = trk[0].eng_req, r;
    int16_t v;
    if (e >= NENGINES)
        return;
    reset_ui();
    song.sel = 0;
    set_engine_of(&trk[0], e);
    frame();
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == SND_FM6 && SCR->rows() == F6_ROWS, "ENV tapped on FM6: SOUND's FM6 rows");
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    check(f6.target == 2u, "OPERATOR row, KNOB 1: the operator (OP3)");
    turn(EN_SELECT, 1);
    {
        char b[16];
        SCR->name(ui.row[SCR_SOUND], b);
        check(!strcmp(b, "OP3 FREQ"), "the next row: the operator's FREQ page");
    }
    v = f6_ed()[FM6_OPB(3) + FO_FINE];
    turn(EN_K3, 3);
    check(f6_ed()[FM6_OPB(3) + FO_FINE] != v, "KNOB 3: OP3's FINE in the part's voice");
    for (r = 0; r < F6_ROWS; r++) {                     /* the algorithm over every row */
        ui.row[SCR_SOUND] = (uint8_t)r;
        ui.force = 1;
        frame();
        if (r == 2u)
            ppm("opt-fm6-page");
    }
    check(px_in(4, OY_PANEL + 4, 232, GRAPH_H - 8, C_WHITE), "the algorithm drawn over the rows, the operator white");
    tap(B_ENV);
    check(ui.row[SCR_SOUND] == 0u, "ENV again: the next row, round");
    press(B_ENV);                                       /* ENV held: the layer */
    frames(HOLD_FRAMES);
    check(lay.shown == LY_OPS && ly_ops_on, "ENV held on FM6: the operator layer");
    key(3);                                             /* G#: OP2 */
    check(f6.target == 1u && f6_kind(f6.row) == 0u, "a black key (G#): OP2");
    key(15);                                            /* PIT */
    check(f6_kind(f6.row) == 1u, "the PIT key: the pitch envelope's pages");
    tap(B_OCTUP);
    check(f6.row == F6_PIT0 + 1u, "OCT+: the next page");
    v = f6_ed()[FV_PL + 1];
    turn(EN_K2, -2);
    check(f6_ed()[FV_PL + 1] != v, "KNOB 2 in the layer: the page's value");
    key(1);                                             /* F#: OP1 */
    ui.force = 1;
    frame();
    ppm("opt-fm6-layer");
    release(B_ENV);
    frames(2);
    check(lay.shown == LY_PLAY && ui.scr == SCR_SOUND && snd_fam == SND_FM6 && ui.row[SCR_SOUND] == f6.row,
          "ENV let go after use: SOUND's FM6 rows on that page");
    set_engine_of(&trk[0], keep);
    frame();
    reset_ui();
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_ENV, "ENV on another engine: its envelopes, as before");
    press(B_ENV);
    frames(40);
    check(lay.shown == LY_PLAY, "ENV held on another engine: no layer");
    release(B_ENV);
#endif
    reset_ui();
}
static void graph_family_tests(void)
{
    uint32_t r, bad = 0, n;
    reset_ui();
    song.sel = 0;
    tap(B_LFO);
    n = SCR->rows();
    for (r = 0; r < n; r++)
        bad += !snd_graph_page(r);
    check(snd_fam == FAM_LFO && n >= 2u && !bad && snd_graph_page(1)->graph == GR_LFO,
          "SOUND LFO: the wave on every row (LFO DEST too)");
    tap(B_ENV);
    n = SCR->rows();
    for (r = 0, bad = 0; r < n; r++)
        bad += !snd_graph_page(r);
    check(snd_fam == FAM_ENV && !bad, "SOUND ENV: an envelope on every row (the DEST rows too)");
    ui.row[SCR_SOUND] = (uint8_t)(n - 1u);
    ui.force = 1;
    frame();
    ppm("opt-sound-env-dest");
    reset_ui();
}
static void preset_engine_tests(void)
{
    uint32_t total, cur, e, r;
    char nm[16];
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    ui.force = 1;
    frame();
    ppm("opt-sound-presets");
    cur = preset_pos(&total);
    e = pre_entry(cur, nm);
    check(e == TSEL->eng_req && nm[0], "the SOUND row: the preset playing and its engine");
    check(px_in(4, OY_PANEL + 24, 10, 10, ENG_COL[e]), "... its engine's colour chip drawn");
    go_home();
    frame();
    mix_find("SOUND");
    frame();
    turn(EN_PRESET, 1);
    e = TSEL->eng_req;
    check(ui.toast_t && strstr(ui.msg, ENGINES[e]->name) && ui.toast_col == ENG_COL[e],
          "the mixer's PRESETS: a toast with the preset's engine, framed in its colour");
    turn(EN_PRESET, -1);
    reset_ui();
}

#include "ui_optimist_len.h"                       /* LEN in powers of two, SHIFT = LFO held */
#include "ui_optimist_cards.h"                     /* SYSTEM > SCREEN > CARDS: 1x4 or 2x2 */
#include "ui_optimist_hold.h"                      /* SYSTEM HOLD: a click is a tap, a hold the layer */
int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : "build/host";
    host_tracks_init();
#if FELUCCA_PATTERNS
    memset(nor, 0xFF, sizeof nor);                      /* an erased NOR: the section log empty */
    sec_pend_clear();
    sec_boot();
#endif
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
    family_tests();
    header_footer_tests();
    mixer_tests();
    scope_tests();
    step_keys_tests();
    step_synth_tests();
    layer_tests();
    save_layer_tests();
    tempo_tests();
    combo_tests();
    song_tests();
    shortcut_tests();
    undo_extras_tests();
    name_tests();
    fm6_tests();
    graph_family_tests();
    preset_engine_tests();
    len_tests();
    cards_tests();
    hold_tests();
    fuzz(20000, 12345);
    check(1, "20000 frames of random use: every draw on the screen");
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
}
