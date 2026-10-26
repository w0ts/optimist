/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI (firmware/src/ui/optimist, docs/UI-OPTIMIST-DESIGN.md) on a framebuffer with
 * panel / flash / storage doubles, the audio (mix_block) running between frames as on the device, in the style of
 * tests/ui_pages_test.c (SLOOP's UI). It checks:
 *   rows     every screen's rows resolve, on every engine and on the drum track: names fit the panel, labels the
 *            cards, a value cell has its descriptor and its value
 *   keys     SELECT the row, ALGORITHM the track, PRESETS the hot cell one unit (the preset only on the SOUND row),
 *            KNOB 1..4 the cells, SHIFT held + a knob the default, YES enters / toggles / does, NO goes back, the
 *            page buttons jump to their rows, SHIFT held + a drum key picks the lane, PLAY / REC transport
 *   confirm  SHIFT + REC asks "CLEAR T1? YES"; YES does it, NO and 3 s let it go; PROJECT's NEW asks; a SAVE over
 *            an empty slot does not
 *   undo     SAVE then HOME undoes, HOME then SAVE redoes, neither is a YES or a NO; EDIT + OCT- / OCT+ too
 *   messages every message and every question fits the header (232 px)
 * then random use: every draw stays on the screen. Renders the screens to PPM (DIR/opt-*.ppm) for review. */
#ifndef FELUCCA_PAGE_LINES
#define FELUCCA_PAGE_LINES 0            /* (the firmware always builds the lines, registry.h; these tests keep the 2x2 cards they were written for, the lines builds pass 1) */
#endif
#define FELUCCA_ARRANGER 1
#ifdef MIRROR_CHECK
#define FELUCCA_ED_MIRROR 1             /* (the mixer's meters to the client at most every 100 ms: op_mixdraw.c mx_vu_at) */
#endif
#define STP_PAT 0u                      /* STEP's first row, PATTERN (no LANE page after it since 2026-10-14) */
#define STP_DR_TL (STP_TL + (STP_TL && FELUCCA_GEN_GRIDS ? 1u : 0u))   /* DR itself (no lane picked): GRIDS and DENSITY in GEN's place */
#if FELUCCA_PATTERNS
#define FELUCCA_FLASH 1                 /* (the section log: phase 4's switch set) */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#if FELUCCA_KNOB_ACCEL
#include "../firmware/src/ui/knob_accel.h"     /* (felucca.c includes it: the knobs' acceleration, a build with FELUCCA_KNOB_ACCEL=1 such as the user's) */
#endif
#include <assert.h>
static uint16_t screen[240 * 240];
static void lcd_sync(void) {}
static uint32_t band_y = 240u, band_hits;               /* the help band's first row (op_graph.c OP_FOOT_Y, set in main), and
                                                         * the blits that reached it besides the line's own (ui_optimist_geometry.h) */
static uint32_t probe_hits;                             /* the blits that cover the probe pixel (120, 180): inside PRESETS' popup, a flash when more than one a frame */
static uint32_t blit_px;                                /* the pixels sent (ui_optimist_slowframe.h: a step press sends its column) */
#ifdef MIRROR_CHECK
/* MIRROR_CHECK: every write also through the mirror's encoder (io/editor/ed_mirror_enc.h) and the client's decoder (mirror_dec.h):
 * at the end the decoded screen is this one (tests/run_tests.sh, ED_MIRROR) */
#include "mirror_dec.h"
static uint8_t mc_f[512];
static uint32_t mc_n, mc_frames, mc_bytes, mc_bad;
static int mir_wire(uint32_t b)
{
    if (mc_n < sizeof mc_f)
        mc_f[mc_n++] = (uint8_t)b;
    return 1;
}
static void mir_wire_end(void)
{
    mc_bad += !md_frame(mc_f, mc_n);
    mc_frames++, mc_bytes += mc_n;
    mc_n = 0;
}
#include "../firmware/src/io/editor/ed_mirror_enc.h"
static uint8_t mir_mute;                                /* (display/lcd.c's: a meter not due) */
static uint32_t mc_meter_ms, mc_meter_sends, mc_meter_now;
static uint32_t mir_meters_due(void)                    /* (ed_mirror.c's, the client always there; mc_meter_now: a window now) */
{
    if (fm1_ms - mc_meter_ms < 100u && !mc_meter_now)
        return 0;
    mc_meter_ms = fm1_ms;
    mc_meter_sends++;
    return 1;
}
static uint32_t mir_meters_off(void) { return 0; }    /* (USB here: Bluetooth's empty meters, mirror_test.c) */
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p) { mir_enc_blit(x, y, w, h, p); }   /* (gfx.c cv_mirror) */
#endif
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
#ifdef MIRROR_CHECK
    if (!mir_mute)
        mir_enc_blit(x, y, w, h, p);
#endif
    blit_px += w * h;
    if (x <= 120u && 120u < x + w && y <= 180u && 180u < y + h)
        probe_hits++;
    assert(x + w <= 240 && y + h <= 240);
    if (y + h > band_y && !(x == 0u && y == band_y && w == 240u && h == 240u - band_y))
        band_hits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = p[j * w + i];
}
#include "../firmware/src/display/gfx.c"
static uint32_t full_fills;                             /* the whole screen cleared (ui_optimist_master.h: ALGORITHM in place) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
#ifdef MIRROR_CHECK
    mir_enc_fill(x, y, w, h, c);
#endif
    full_fills += w == 240u && h == 240u;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = swap16(c);
}
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_half[16];
#if FELUCCA_LIGHTS
static uint8_t fm1_led_bg[16];
static uint16_t fm1_led_bg_ns;
#endif
#define FM1_NCOL 16u
static int8_t FM1_KEYMAP[5][16];                      /* (filled in main: every button and key one LED place) */
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_btn_t[2][14];                      /* each button's last press / release (timer ticks), as hal/fm1_input.h stamps them */
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t notes_tap;                              /* keys pressed and let go between two frames (the ISR's edge, no key down now) */
static uint32_t fm1_input_note_edges(void) { uint32_t e = (fm1_in.notes & ~notes_seen) | notes_tap; notes_seen = fm1_in.notes; notes_tap = 0; return e; }
static void fm1_wdt_feed(void) { fm1_ms++; }              /* (time passes while the firmware waits: SYSTEM > CALIBRATE's idle timeout, met by fuzz) */
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
#include "ble_ui_stubs.h"                      /* the radio: midi_ble.c state, ble_devices.c over a stand-in link layer */
#include "bta_ui_stubs.h"                      /* with FELUCCA_BT_AUDIO: Bluetooth audio's state and calls, recorded */
#if DUAL_LOAD && !FELUCCA_DUAL                 /* (the readout without the second core: system/dual.c's state the UI reads) */
#include "../firmware/src/system/dual_mode.h"
static uint8_t dual_mode;                      /* the 2ND CORE setting */
static struct { uint8_t up, why, boot_mode; } dual;   /* up, DUAL_* why it is down, the setting this boot started with */
#endif
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
#if FELUCCA_MOTION
static void motion_flash_read(uint32_t obj, const project_t *p) { (void)obj; (void)p; }   /* (sections.c's migration reads the motion store; the real one is project.c's, not linked here) */
#endif
static void sections_write(void);
static void persist_flush_now(void) { sections_write(); }   /* (project.c's, not linked here: the arena written before a project opens) */
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
static uint32_t up_missing;                             /* bit k: user preset k's engine not built (ui_optimist_premiss.h) */
static uint32_t up_engine_slot(uint32_t k) { return k < 2u && !((up_missing >> k) & 1u) ? 0u : NENGINES; }   /* (the two used slots: engine 0's) */
static char up_stored[16];                              /* NAME: what up_store was given (upreset.c) */
static uint32_t up_stores, up_store_slot;
static int up_store(uint32_t k, const char *name) { up_store_slot = k; str_cpy(up_stored, name, sizeof up_stored); up_stores++; return 0; }
#if FELUCCA_NB_LIST                                /* the FM6 / CZ collections in PRESETS (storage/nbank.c): no flash here, none stored */
static uint32_t nb_kind(const track_t *t) { (void)t; return 0; }
static uint32_t nb_list(uint32_t *rank, uint32_t n, uint32_t *at) { (void)rank; (void)n; (void)at; return 0; }
static uint32_t nb_loaded_kind, nb_loaded_slot;           /* (the last nb_load: the slot, and the track's engine then) */
static void nb_name(uint32_t kind, uint32_t k, char *nm) { nm[0] = 'N'; nm[1] = (char)('0' + kind); nm[2] = (char)('0' + k); nm[3] = 0; }
static void nb_load(uint32_t k) { nb_loaded_slot = k; nb_loaded_kind = TSEL->eng_req; }
static uint32_t nb_list_of(uint32_t kind, uint32_t *rank, uint32_t n, uint32_t *at)   /* two collections: kind 1 of 3 slots (engine 0's), kind 2 of 2 (engine 1's) */
{
    uint32_t c = kind == 1u ? 3u : 2u;
    *rank = c;
    if (n < c)
        *at = n;
    return c;
}
static uint32_t nb_kind_at(uint32_t i) { return i + 1u; }
static uint32_t nb_engine_slot(uint32_t kind) { return kind == 1u ? 0u : 1u; }
#endif
#if !FELUCCA_PATTERNS                                    /* (project.c is in the TU with the section log only: NEW PROJECT's blank load of the FX and the automation) */
static int fxr_decode(const uint8_t *a, uint32_t n, int all) { (void)a; (void)n; (void)all; return 0; }
#if FELUCCA_AUTO
static void auto_init(void) {}
#endif
#endif
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

#if UI_SDCARD
#include "ui_optimist_sdcard_env.h"                     /* SD card mode: the card model under the card's modules and sd_mode.c */
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
/* FRAME_HASH=1: one FNV-1a hash of every frame drawn, printed at the end (a refactor of the drawing keeps every pixel:
 * the same hash before and after) */
static uint32_t frame_hash = 2166136261u, frame_n;
static void frame_hash_print(void) { printf("frame hash %08x over %u frames\n", frame_hash, frame_n); }
static void frame(void)                                 /* one UI frame (~16 ms): the audio, input, LEDs, draw */
{
    uint32_t q;
    static int32_t o[CTL * 2];
    static int hashing = -1;
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    ui_input();
    msg_seen();
    ui_leds();
    ui_draw();
    fm1_ms += 16;
    if (hashing < 0 && (hashing = getenv("FRAME_HASH") != NULL))
        atexit(frame_hash_print);
    if (hashing) {
        const uint8_t *p = (const uint8_t *)screen;
        for (q = 0; q < sizeof screen; q++)
            frame_hash = (frame_hash ^ p[q]) * 16777619u;
        frame_n++;
    }
}
static void frames(uint32_t n) { while (n--) frame(); }
#define HOLD_FRAMES (op_hold_ms() / 16u + 3u)            /* a button held past SYSTEM HOLD: its layer shows */
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { fm1_btn_t[0][panel.btn[b]] = fm1_ticks(); edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_btn_t[1][panel.btn[b]] = fm1_ticks(); fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
/* a turn of s detents: slow (80 ms a detent) when the knobs accelerate (FELUCCA_KNOB_ACCEL, a build such as the user's):
 * the tests count detents, the acceleration has its own (knob_accel_test.c). The time since the knob's last detent is set,
 * the clock does not move. */
static void turn(uint32_t role, int32_t s)
{
#if FELUCCA_KNOB_ACCEL
    ui.enc_t[role] = fm1_ticks() - (uint32_t)(s < 0 ? -s : s) * 80u * 1000u * 24u;
#endif
    encs[panel.enc[role]] += s;
    frame();
}
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static void cards_up(void) { if (ui.scr == SCR_STEP && !ui.cards_t && !st.held) tap(B_SEQ); }   /* STEP: SEQ brings the
                                                         * cards back (the page kept), the knobs edit only under them */
static int fails;
static uint32_t key_led(uint32_t k);                     /* ui_optimist_pages.h */
static int px_in(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col);
static void check(int ok, const char *what) { printf("optimist ui: %-72s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
#define RING_FRAMES ((RH_CLEAR_MS + 40u) / 16u + 2u)    /* (a delete's ring held to its end: frames of 16 ms) */
static void reset_ui(void)
{
#if FELUCCA_FAVORITES
    fav_list = 0;                                       /* (LIST ALL: a favorites test leaves the list filtered otherwise) */
#endif
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    op_disarm();
    ui.msg_t = 0;
    st.pk = PK_NONE;                                    /* (no note picked: its selection colour would show on the next roll) */
    smute_trk = 0;                                      /* (nothing muted: SHIFT + a key mutes since 2026-10-16) */
    smute_lane = 0;
    memset(snd_mem, 0, sizeof snd_mem);                 /* (a power-on's page memory: SOUND's, the held step's, STEP's row) */
    memset(hp_mem, 0, sizeof hp_mem);
    ui.row[SCR_STEP] = 0;
    go_home();
    frames(2);
    snd_mark_all();                                     /* (the sounds as loaded: browsing asks nothing, op_edits.c) */
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
            if (c.label && text_w(&FONT_S, c.label) > (gl_scr(scr) ? 12 * 8 : CARD_W))   /* (GLOBAL, SYSTEM: a line's name, 12 characters) */
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
static uint32_t sys_item_of(uint32_t r, uint32_t k)    /* SYSTEM's cell k of the page of line r: its SI_* item */
{
    uint32_t i = gl_line(1, r, k), e;
    if (GL_DEV || i >= gl_n(1))
        return SI_NONE;
    e = gl_ent(1, i);
    return sys_item(e >> 2, e & 3u);
}
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
                   (scr == SCR_SYSTEM && c.kind == CK_RO && c.label && (!strcmp(c.label, "VERSION") || !strcmp(c.label, "INFO") || !strcmp(c.label, "INTERNALS") || !strcmp(c.label, "USB STATS") || !strcmp(c.label, "CREDITS") ||
                                                                 !strcmp(c.label, "BUILD") || !strcmp(c.label, "STATE") ||
                                                                 !strcmp(c.label, "DEVICE") || !strcmp(c.label, "ACTIVITY") ||
                                                                 !strcmp(c.label, "FORMAT") ||
                                                                 !strcmp(c.label, " "))) ||   /* (text: no range) */
                   (scr == SCR_SYSTEM && c.kind == CK_VAL && sys_item_of(r, k) >= SI_UAS0) ||   /* (USB AUDIO's channels: a list of
                                                         * names, text only) */
                   (scr == SCR_SYSTEM && (sys_item_of(r, k) == SI_BLE || sys_item_of(r, k) == SI_BTA)) ||   /* (BT MIDI's and BT
                                                         * AUDIO's MODE: OFF, MAN, AUTO, words) */
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
#if FELUCCA_INS_PHASER
/* fx-pool (2026-10-15): a return holding another type (PHSR on A: its RET AMT row, the SENDS / RETURNS cards named PHSR) and
 * EQ3 (a return's RET EQ3 rows, a track's EQ3 and EQ3 FREQ rows): the rows exist, their cards are drawn (opt-fxpool-*.ppm) */
static int fx_row_of(const char *t)                   /* the master FX screen's row titled t, -1 none */
{
    uint32_t r, n = fxs_rows();
    for (r = 0; r < n; r++)
        if (!strcmp(fx_pg(r)->title, t))
            return (int)r;
    return -1;
}
static int snd_row_of(const char *t)
{
    uint32_t r, n = snd_rows();
    for (r = 0; r < n; r++)
        if (snd_page(r) && !strcmp(snd_page(r)->title, t))
            return (int)r;
    return -1;
}
static void fxpool_tests(void)
{
    int r;
    const uint8_t s[2] = {FXT_EQ3, FXT_NONE};
    cell_t c;
    reset_ui();
    fxs_load(3, FXT_PHSR);                              /* (C: CHO's line 0) */
    fxs_load(1, FXT_EQ3);
    EQ3_V(NTRK + 2u + 2u)[EQV_LO] = EQ_KILL;            /* (A: REV's line 2) */
    go_home();
    ui.row[SCR_HOME] = MXR_MASTER;
    tap(B_FX);
    check(ui.scr == SCR_FX && fx_row_of("SLOTS") >= 0 && fx_row_of("RET AMT") >= 0 && fx_row_of("RET EQ3") >= 0 &&
          fx_row_of("RET EQ3 F") >= 0, "fx-pool: EQ3 on A, PHSR on C: the master FX screen has SLOTS, RET AMT, RET EQ3, RET EQ3 F");
    ui.row[SCR_FX] = (uint8_t)fx_row_of("SLOTS"), ui.force = 1, frame(), ppm("opt-fxpool-slots");
    ui.row[SCR_FX] = (uint8_t)fx_row_of("RET AMT"), ui.force = 1, frame(), ppm("opt-fxpool-retamt");
    ui.row[SCR_FX] = (uint8_t)fx_row_of("RET EQ3"), ui.force = 1, frame(), ppm("opt-fxpool-reteq3");
    r = fx_row_of("RETURNS");
    ui.row[SCR_FX] = (uint8_t)(r < 0 ? 0 : r), ui.force = 1, frame(), ppm("opt-fxpool-returns");
    fxs_cell((uint32_t)ui.row[SCR_FX], 2, &c);
    check(r >= 0 && c.label && !strcmp(c.label, "PHSR RT"), "... RETURNS' card 3 (C, line 0) named PHSR RT (the return's type)");
    ins_put(0, s);
    song.sel = 0;
    go_home();
    tap(B_FX);
    check(ui.scr == SCR_SOUND && snd_row_of("EQ3") >= 0 && snd_row_of("EQ3 FREQ") >= 0, "... T1 holding EQ3: its EQ3 and EQ3 FREQ rows");
    EQ3_V(0)[EQV_LO] = EQ_KILL, EQ3_V(0)[EQV_HI] = 4;
    ui.row[SCR_SOUND] = (uint8_t)snd_row_of("EQ3"), ui.force = 1, frame(), ppm("opt-fxpool-eq3");
    ui.row[SCR_SOUND] = (uint8_t)snd_row_of("EQ3 FREQ"), ui.force = 1, frame(), ppm("opt-fxpool-eq3freq");
    ins_defaults();                                     /* (every slot the default again, the screens' rows the first) */
    eq3_defaults();
    fxs_set(FXS_DEF);
    ui.row[SCR_FX] = 0, ui.row[SCR_SOUND] = 0;
    reset_ui();
    go_home();
}
#endif
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
static void sound_all(void)                             /* SOUND, every row, the SOUND row first (the whole list, as
                                                         * ALGORITHM back from MASTER leaves it; the mixer has no YES, D6) */
{
    go_home();
    frame();
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    frame();
}
/* MAIN and the SOUND row (2026-10-18): EDIT opens on MAIN, the engine's four (engine_t macro); SELECT back: the SOUND
 * row, ENGINE PRESET INIT SAVE AS; KNOB 2 the sound, KNOB 1 the engine; an engine without its four (FM6): no MAIN */
static void main_page_tests(void)
{
    static track_t keep_all[NTRK];
    uint32_t sel = song.sel;
    track_t keep = *TSEL;
    cell_t c;
    uint32_t e, ok = 1, pre, k;
    memcpy(keep_all, trk, sizeof keep_all);             /* (every track as it was after: the later tests' state) */
    for (e = 0; e < NENGINES; e++) {                    /* MAIN's cards: the engine's macro, the row there exactly when it has one */
        const engine_t *en = ENGINES[e];
        uint32_t any = (en->macro[0] | en->macro[1] | en->macro[2] | en->macro[3]) != 0u;
        if (eng_free(e))
            continue;
        set_engine_of(TSEL, e);
        go_home();
        tap(B_EDIT);
        ok &= ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == (any ? SND_MAIN_ROW : 0u);
        for (k = 0; any && k < 4u; k++)
            ok &= main_pg.id[k] == (en->macro[k] ? en->macro[k] : 0xFFu);
        memset(snd_mem, 0, sizeof snd_mem);
    }
    *TSEL = keep;
    check(ok, "EDIT opens on MAIN, the engine's four (engine_t macro); an engine without them (FM6) on the SOUND row");
    go_home();
    tap(B_EDIT);
    SCREENS[SCR_SOUND].cell(ui.row[SCR_SOUND], 0, &c);
    ok = ui.row[SCR_SOUND] == SND_MAIN_ROW && c.d && str_eq(c.label, track_desc(TSEL, main_pg.id[0])->label);
    turn(EN_SELECT, -1);
    ok &= ui.row[SCR_SOUND] == 0u;
    SCREENS[SCR_SOUND].cell(0, 0, &c);
    ok &= str_eq(c.label, "ENGINE");
    SCREENS[SCR_SOUND].cell(0, 1, &c);
    ok &= str_eq(c.label, "PRESET");
    SCREENS[SCR_SOUND].cell(0, 2, &c);
    ok &= str_eq(c.val, "INIT") || str_eq(c.label, "INIT") || c.kind == CK_ACT;
    check(ok, "MAIN shows the engine's first value; SELECT back from it: the SOUND row ENGINE PRESET INIT SAVE AS");
    pre = TSEL->preset;
    turn(EN_K2, 1);
    ok = TSEL->preset != pre;
    e = TSEL->eng_req;
    turn(EN_K1, 1);
    ok &= TSEL->eng_req != e;
    *TSEL = keep;
    check(ok, "the SOUND row: KNOB 2 the next sound, KNOB 1 the next engine");
#if FELUCCA_PARAM_HELP
    {   /* where an automated value's change comes from (hl_src): LFO A on the MAIN page's first card (ANALOG: CUT, P_E4) */
        char b[64];
        uint32_t m;
        memset(snd_mem, 0, sizeof snd_mem);             /* (EDIT on MAIN again: the SOUND row was the last shown) */
        go_home();
        tap(B_EDIT);
        hl.k = 0;
        m = hl_src(b);
        check(!m, "an untouched value: no source on the help line");
        TSEL->p[P_L1DA] = (int16_t)(LT_E1 + (main_pg.id[0] - P_E0));
        TSEL->p[P_L1AA] = 30;
        m = hl_src(b);
        check(main_pg.id[0] >= P_E0 && main_pg.id[0] <= P_E7 && m && strstr(b, "LFO-A"),
              "LFO A aimed at MAIN's first value: the help line says LFO-A");
        TSEL->p[P_L1AA] = 0;
        check(!hl_src(b), "... its AMT at 0: no source");
    }
#endif
    memset(snd_mem, 0, sizeof snd_mem);
    go_home();
    memcpy(trk, keep_all, sizeof keep_all);
    song.sel = (uint8_t)sel;
}
static uint32_t row_named(uint32_t scr, const char *name);
/* the help line on a DEST naming an engine value (op_help.c hl_dest, 2026-10-18): the engine, the value's name, its line */
static void dest_help_tests(void)
{
#if FELUCCA_PARAM_HELP
    char b[64], want[64];
    int16_t keep = TSEL->p[P_L1DA];
    uint32_t r, code;
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    go_home();
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    r = row_named(SCR_SOUND, "LFO DEST");
    ui.row[SCR_SOUND] = (uint8_t)r;
    hl.k = 0;
    TSEL->p[P_L1DA] = LT_E5;
    code = hl_dest(b);
    str_cpy(want, e->name, sizeof want);
    str_cpy(want + str_len(want), " ", 2);
    str_cpy(want + str_len(want), track_desc(TSEL, P_E4)->label, sizeof want - str_len(want));
    check(r < SCR->rows() && code && !strncmp(b, want, str_len(want)), "a DEST on an engine value: the help line names the engine and the value (\"ANALOG CUT - ...\")");
    TSEL->p[P_L1DA] = LT_PIT;
    check(hl_dest(b) && !strcmp(b + str_len(trk_tag(song.sel % NTRK)), " PIT - Pitch of every voice"),
          "... a DEST on PIT: the track and \"PIT - Pitch of every voice\" (2026-10-19: no more \"What LFO moves\")");
    TSEL->p[P_L1DA] = LT_PAN;
    check(hl_dest(b) && strstr(b, "Stereo position") != 0, "... on PAN: SENDS' line (the names left out: too wide for the band)");
    TSEL->p[P_L1DA] = LT_ATK;
    check(hl_dest(b) && strstr(b, " ATK - Attack") != 0, "... on ATK: ENV's line");
    TSEL->p[P_L1DA] = LT_INS1;
    check(hl_dest(b) && b[0] && !strstr(b, "What LFO"), "... on INS1: the slot and the type it holds (or empty), or the type's line");
    TSEL->p[P_L1DA] = LT_RES;
    code = hl_dest(b);
    check(lo_res(e) >= 8u || (code && !strncmp(b, e->name, str_len(e->name))), "... on RES: the engine's own resonance value");
    TSEL->p[P_L1DA] = LT_OFF;
    check(!hl_dest(b), "... OFF: the table's own line");
    TSEL->p[P_L1DA] = keep;
    go_home();
#endif
}
static uint32_t row_named(uint32_t scr, const char *name)   /* the row of screen scr named name, or its row count */
{
    uint32_t r, n = SCREENS[scr].rows();
    char nm[12];
    for (r = 0; r < n; r++) {
        SCREENS[scr].name(r, nm);
        if (!strcmp(nm, name))
            break;
    }
    return r;
}
/* GLOBAL and SYSTEM's list (op_global.c): screen scr entered, the cursor on the first line labelled lab (its knob:
 * LINE_KNOB); the line, or the screen's line count when there is none */
static uint32_t gl_kn(uint32_t scr, uint32_t i)         /* line i's knob (0..3) on its page: its place on the page (op_global.c pages) */
{
    return i - gl_first(scr == SCR_SYSTEM, i);
}
static uint32_t gl_pfirst(uint32_t s, uint32_t p)       /* the first line of page p of screen s (the line count past the last page) */
{
    uint32_t i;
    for (i = 0; i < gl_n(s); i++)
        if (gl_pg(s, i) == p)
            break;
    return i;
}
static uint32_t line_named(uint32_t scr, const char *lab)
{
    uint32_t i, n;
    cell_t c;
    if (ui.scr != scr)
        op_enter(scr);
    n = SCREENS[scr].rows();
    for (i = 0; i < n; i++) {
        SCREENS[scr].cell(i, gl_kn(scr, i), &c);
        if (c.label && !strcmp(c.label, lab))
            break;
    }
    if (i < n) {
        ui.row[scr] = (uint8_t)i;
        ui.hot = (uint8_t)gl_kn(scr, i);
    }
    return i;
}
static void line_at(uint32_t scr, uint32_t i)            /* the cursor on line i of scr (ALGORITHM no longer moves it: the knobs pick, SELECT pages) */
{
    if (ui.scr != scr)
        op_enter(scr);
    ui.row[scr] = (uint8_t)i;
    ui.hot = (uint8_t)gl_kn(scr, i);
}
#define LINE_KNOB(i) (EN_K1 + gl_kn(ui.scr, (i)))     /* the knob of line i: its place on its page */
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
    sound_all();
    check(ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == 0 && !snd_page(0) && snd_page(SND_MAIN_ROW) == &main_pg,
          "SOUND, every row: the SOUND row first (ENGINE PRESET INIT SAVE AS, 2026-10-18), then MAIN");
    main_page_tests();
    dest_help_tests();
    (void)lv, (void)n;
    ui.force = 1;
    frame();
    ppm("opt-sound");
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_PRESET, 1);
    check(TSEL->preset != pre || TSEL->eng_req != eng, "PRESETS: the next sound");
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND])->fam == FAM_ENV && snd_page(ui.row[SCR_SOUND])->id[0] == P_ATK,
          "ENV tapped: SOUND's ENV row");
    tap(B_ENV);
    check(snd_page(ui.row[SCR_SOUND])->fam == FAM_ENV && snd_page(ui.row[SCR_SOUND])->id[0] != P_ATK, "ENV again: the family's next row");
    {
        int16_t a = TSEL->p[P_ATK];
        uint32_t p2 = TSEL->preset;
        while (snd_page(ui.row[SCR_SOUND])->id[0] != P_ATK)
            turn(EN_SELECT, -1);                        /* (back by SELECT: the button stops at the family's last row) */
        ui.hot = 0;
        turn(EN_PRESET, 2);
        check(TSEL->p[P_ATK] == a && TSEL->preset != p2, "ENV row: PRESETS the sound (the next preset), ATK stays (2026-10-10)");
        fm1_in.buttons |= BT(B_OCTDN);
        turn(EN_K1, 1);
        fm1_in.buttons &= ~BT(B_OCTDN);
        frame();
        check(TSEL->p[P_ATK] == TP[P_ATK].def && ui.scr == SCR_SOUND, "SHIFT held + KNOB 1: ATK to its default, still on SOUND");
    }
    tap(B_SEQ);
    check(ui.scr == SCR_STEP && ly_lock == LY_STEP, "SEQ tapped: STEP, the keys are steps (ly_lock LY_STEP)");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME tapped: NO, back to the mixer");
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE, "HOME at the root: the scope [P]");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "HOME on the scope: the mixer again");
    ui.row[SCR_HOME] = MXR_MASTER;
    ui.hot_lit = 1;
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT, "MASTER's row, a cell picked, SAVE: PROJECT (the mixer has no YES, D6)");
    go_home();
    ui.row[SCR_HOME] = MXR_MASTER;
    tap(B_FX);
    check(ui.scr == SCR_FX, "MASTER's row, FX: the master FX screen");
    ui.force = 1;
    frame();
    ppm("opt-fx");
    {   /* FX tapped, then ALGORITHM left past T1: the global FX screen, and back */
        char h[40];
        uint32_t n, r;
        reset_ui();
        song.sel = 0;
        frame();
        tap(B_FX);
        check(ui.scr == SCR_SOUND && snd_fam == FAM_FX, "FX tapped: the track's FX pages");
        turn(EN_ALGO, 1);
        check(song.sel == 1 && ui.scr == SCR_SOUND && snd_fam == FAM_FX, "ALGORITHM right: T2's FX pages");
        turn(EN_ALGO, -1);
        check(song.sel == 0 && ui.scr == SCR_SOUND, "... and back to T1");
        turn(EN_ALGO, -1);
        check(ui.scr == SCR_FX, "ALGORITHM left past T1: the global FX screen");
        head_title(SCR_FX, ui.row[SCR_FX], h, sizeof h);
        check(!strcmp(h, "Insert"), "its header names it (Insert under the MASTER badge, the first page: the inserts)");
        n = SCR->rows();
        for (r = 0; r < n && ui.row[SCR_FX] != n - 1u; r++)
            tap(B_FX);
        check(ui.scr == SCR_FX && ui.row[SCR_FX] == n - 1u, "FX tapped again on it: the next page, up to the last");
        tap(B_FX);
        check(ui.scr == SCR_FX && ui.row[SCR_FX] == 0u, "... past the last: the first again (a button again wraps)");
        turn(EN_ALGO, -1);
        check(ui.scr == SCR_FX, "ALGORITHM left on it: stays (nothing wraps)");
        turn(EN_ALGO, 1);
        check(ui.scr == SCR_SOUND && snd_fam == FAM_FX && song.sel == 0, "ALGORITHM right: T1's FX pages again");
    }
    reset_ui();
    song.sel = TRK_DRUM;
    frame();
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    key(key_of_lane(2));                                /* (the third lane's black key, A#3) */
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    check(lane_selected() != 2u && ui.scr == SCR_HOME && smute_lane_on(2),
          "SHIFT held + a drum key on the mixer: the lane's sequencer muted instead of picked (2026-10-13), no NO");
    smute_lane = 0;
    tap(B_EDIT);
    check(ui.scr == SCR_SOUND && ui.row[SCR_SOUND] == SND_MAIN_ROW && !snd_page(0) && snd_page(1)->scope == SC_DSND &&
              str_eq(snd_page(1)->title, "MAIN") && snd_page(2)->scope == SC_DSND,
          "drum track, EDIT: on MAIN (the lane's TUNE DECAY CUT LEVEL), the SOUND row (SRC ACCENT INSTR KIT) before it");
    {
        int8_t t0 = dl.ofs[lane_selected()][DE_TUNE];
        while (!snd_page(ui.row[SCR_SOUND]) || snd_page(ui.row[SCR_SOUND])->id[0] != 0)
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
    for (j = y; j < y + h && j < 240u; j++)                 /* (the box clipped to the screen: a read past it is undefined) */
        for (i = x; i < x + w && i < 240u; i++)
            if (screen[j * 240u + i] == swap16(col))
                return 1;
    return 0;
}
static int px_at(uint32_t x, uint32_t y, uint16_t col) { return x < 240u && y < 240u && screen[y * 240u + x] == swap16(col); }   /* that pixel (off the screen: none) */
/* ---- the confirm idiom */
static void confirm_tests(void)
{
    uint32_t r;
    reset_ui();
    trk[0].step[0].time = ST_NOTE;
    trk[0].step[0].n = 1;
    trk[0].step[0].note[0] = 60;
    trk[0].p[P_ATK] = (int16_t)(trk[0].p[P_ATK] > 60 ? 1 : trk[0].p[P_ATK] + 1);   /* (an edited sound: INIT asks) */
    press(B_OCTDN);                                      /* (SHIFT + REC no longer clears the track: SHIFT + ENV is the modal's trigger) */
    press(B_ENV);
    release(B_ENV);
    release(B_OCTDN);
    check(ui.arm_scr == ARM_OP && !strcmp(ui.arm_q, "INIT T1?") && rec_wait == 0 && ui.scr == SCR_HOME,
          "SHIFT + ENV: asks INIT T1? (no ENV, no NO)");
    check(op_overlay() == 1u && ui.overlay == 1u && !strcmp(ui.arm_verb, "INIT?") && !strcmp(ui.arm_arg, "T1") &&
          ui.arm_danger && target_col(ui.arm_arg) == trk_col(0), "the question is the modal: INIT? T1 (its colour), red");
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
    check(ui.arm_scr == ARM_NONE && !strcmp(ui.msg, "SOUND INIT"), "YES: the sound initialised, said");
    check(ui.toast_t > 0 && ui.msg_t == 0 && ui.overlay == 2u, "the result of a confirmed action: a toast, not the header");
    ui.force = 1;
    frame();
    ppm("opt-toast");
    check(screen[(OY_PANEL + 60u) * 240u + 236u] == swap16(OP_SURF) &&
          screen[(OY_PANEL + 44u) * 240u + 120u] == swap16(msg_status(ui.msg) ? C_STATUS[msg_status(ui.msg) & 3u] : C_HI),
          "the toast: a green box in the middle, the mixer drawn around it (not the modal)");
    frames(OP_TOAST_FRAMES + 2u);
    check(ui.toast_t == 0 && ui.overlay == 0u, "the toast goes (1.5 s), the panel comes back");
    /* SAVE then HOME: undo; HOME then SAVE: redo (over a track cleared, the REC ring's way) */
    op_clear_track(0);
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
    check(trk[0].step[0].n == 0 && ui.scr == SCR_HOME && ui.arm_scr == ARM_NONE && !strncmp(ui.msg, "REDO", 4),
          "HOME then SAVE: redo, no YES, no NO");
    press(B_OCTDN);
    press(B_SAVE);
    release(B_SAVE);
    release(B_OCTDN);
    check(trk[0].step[0].n == 0 && strncmp(ui.msg, "UNDO", 4), "SHIFT then SAVE: no redo, no undo any more (2026-10-16)");
    press(B_EDIT);
    press(B_OCTDN);
    release(B_OCTDN);
    release(B_EDIT);
    check(trk[0].step[0].n == 0, "EDIT + OCT-: no undo any more (SAVE then HOME is the one)");
    song.octave = 0;
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(trk[0].step[0].n == 1, "... SAVE then HOME: the step back");
    press(B_OCTDN);
    press(B_REC);
    frames(2);
    check(rh.ring && rh.motion && !song.rec && !op_armed(), "SHIFT + REC: the motion ring at once, nothing asked, no recording");
    release(B_REC);
    release(B_OCTDN);
    frames(2);
    check(!rh.ring && trk[0].step[0].n == 1, "... let go before it closed: nothing cleared");
    check(!strcmp(PRJ_NAME[PR_A24], "SLOOP"), "PROJECT: SLOOP 2.4 .. 2.6's autosave row named SLOOP");
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
    turn(EN_K2, 1);                                     /* (OPERATION: SAVE) */
    saves = 0;
    tap(B_SAVE);
    check(ui.arm_scr == SCR_PROJECT && !strcmp(ui.arm_q, "SAVE PROJECT 4?") && !ui.arm_danger && saves == 0 && !name_on(),
          "PROJECT SAVE into an empty slot: asks first too (amber), nothing written");
#if FELUCCA_PATTERNS
    tap(B_SAVE);
    check(name_on() && nm.kind == NK_PROJ && !prj_slot_used(PR_PROJECT, 3), "PROJECT SAVE with the log: NAME first, nothing written");
    song.playing = 1;                                   /* (an external MIDI clock playing: no STOP here) */
    tap(B_SAVE);                                        /* (the transport stops, NAME's save follows, PLAY again) */
    check(!name_on() && prj_slot_used(PR_PROJECT, 3) && !song.playing && transport_req == 1,
          "NAME while playing: SAVE stops the transport, writes, starts it again");
    transport_req = 0;
    saves = prj_slot_used(PR_PROJECT, 3);               /* (the real log: slot 4 stored) */
    (void)prj_save_go(0);                               /* (slot 1 used, for the question below; FELUCCA_PATTERNS: project 1's state) */
#else
    tap(B_SAVE);
#endif
    check(saves == 1 && ui.arm_scr == ARM_NONE, "PROJECT SAVE into an empty slot: asked, then saved");
    song.g[G_SLOT] = 1;
    check(prj_op() == PO_LOAD, "the operation is LOAD again after an action");
    turn(EN_K2, 1);
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
    turn(EN_K2, 1);
    turn(EN_K2, 1);                                     /* (LOAD, SAVE, CLEAR) */
    tap(B_SAVE);
    tap(B_SAVE);
    check(up_ops[1] == 1, "USER CLEAR: asks, then erases");
    {
        uint32_t kc = knob_colors, w = line_named(SCR_SYSTEM, "KNOB COLORS");   /* (the first lines are CPU's, read-outs: D2;
                                                         * COLOR, which this checked, is gone: 2026-10-18) */
        knob_colors = 0;
        ui.force = 1;
        frame();
        ppm("opt-system");
        turn(LINE_KNOB(w), 1);
        check(knob_colors, "SYSTEM Screen: the KNOB COLORS line's knob sets it");
        tap(B_SAVE);
        check(!knob_colors, "SYSTEM: YES on the cursor line steps a setting round");
        knob_colors = kc;
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
        check(!strcmp(nm_card("ATK", 0, b, sizeof b), "ATK") && !strcmp(nm_card("FILT", 0, b, sizeof b), "FILTER") &&
              !strcmp(nm_card("ENGINE", 0, b, sizeof b), "ENGINE") && !strcmp(nm_card("DTN", 0, b, sizeof b), "DETUNE") &&
              !strcmp(nm_card("R2", "DIGITAL", b, sizeof b), "RAT2") && !strcmp(nm_card("R2", "FM6", b, sizeof b), "R2"),
              "card labels: the names table's, in capitals whatever the length (FILTER, ENGINE, DETUNE; DIGITAL's RAT2)");
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
        check(!strcmp(t, "LFO"), "its header: LFO (the badge names the track)");
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
    check(ui.row[SCR_SOUND] == 0u, "ENV at the family's last row: its first again (a button again wraps)");
    turn(EN_SELECT, 50);
    check(ui.row[SCR_SOUND] == n - 1u, "SELECT moves within the family");
    sound_all();
    all = SCR->rows();
    check(ui.scr == SCR_SOUND && snd_fam == SND_ALL && !snd_page(0) && snd_page(SND_MAIN_ROW) && all > n + 3u,
          "HOME > Sound: every row: the SOUND row, MAIN, the pages");
    song.sel = TRK_DRUM;
    frame();
    tap(B_EDIT);
    n = SCR->rows();
    for (r = 1, only = !snd_page(0); r < n; r++)
        only &= snd_page(r)->fam == FAM_EDIT;
    check(only && snd_page(1)->scope == SC_DSND, "the drum track, EDIT: the SOUND row (KIT LANE), then the lane's rows only");
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT, "the drum track, no lane picked: ENV does nothing (the lane's envelope needs a lane)");
    lane_select(4);
    ui.lane = 1;
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == (DSND_ENV ? FAM_ENV : FAM_EDIT),
          "the drum track, a lane picked: ENV the lane's ENV row (no drum editor built: the rows stay)");
#if DL_UI
    tap(B_FX);
    n = SCR->rows();
    for (r = 0, only = 0; r < n; r++)
        only |= snd_page(r) == &LANE_FX_PG[0];
    check(ui.scr == SCR_SOUND && snd_fam == FAM_FX && only, "the drum track, a lane picked: FX shows the lane's INSERTS row");
#endif
    ui.lane = 0;
    lane_select(0);
    song.sel = 0;
    reset_ui();
}

/* ---- STEP: the keys are steps (docs/UI-OPTIMIST-DESIGN.md 4.2 STEP) */
static uint32_t WK(uint32_t w) { return white_key(w); }   /* white key w's key index */
static uint32_t BK(uint32_t l) { return key_of_lane(l); }   /* drum lane l's black key (2026-10-16: it selects and plays the lane) */
static void kdown(uint32_t k) { fm1_in.notes |= 1u << k; frame(); }
static void kup(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
#include "ui_optimist_mixer.h"                     /* the horizontal mixer, SCOPE, no footer */
#include "ui_optimist_lanefx.h"                    /* SHIFT + a key held: its mute back on release; LANE FX */
#include "ui_optimist_usbfx.h"                     /* the USB input's insert slots: every type, its sets, FULL */
static void seq_long(void)                              /* SEQ held alone past HOLD, let go */
{
    press(B_SEQ);
    frames(HOLD_FRAMES);
    release(B_SEQ);
}
static void step_keys_tests(void)
{
    track_t *t;
    reset_ui();
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    lane_select(2);
    tap(B_SEQ);
    {
        char nm[12];
        step_name(0, nm);
        check(ui.scr == SCR_STEP && ly_lock == LY_STEP && ui.row[SCR_STEP] == 0 && !strcmp(nm, "PATTERN"),
              "drums: SEQ, STEP, on its first page PATTERN (LEN DIV SWING GATE)");
    }
    pen_lane = 9;
    key(WK(0));
    check(dstep_has(&t->dstep[0], 2) && dstep_mask(&t->dstep[0]) == 4u && pen_lane == 9,
          "drums: an empty step tapped: set with the lane (no sound played)");
    ui.force = 1;
    frame();
    ppm("opt-step-drums");
    key(WK(0));
    check(!dstep_mask(&t->dstep[0]), "drums: a set step tapped: off (no level cycle; ghost / hard are SHIFT / SHIFT2 + a tap)");
    fm1_in.notes |= 1u << BK(3);                        /* lane 3's black key held + a step: that lane's level cycles, said */
    frame();
    key(WK(3));
    check(lane_selected() == 3 && dstep_has(&t->dstep[3], 3) && dstep_lvl(&t->dstep[3], 3) == LV_NORM && strstr(ui.msg, " NORM"),
          "drums: a black key held + an empty step: its lane's hit, NORM said");
    key(WK(3));
    check(dstep_lvl(&t->dstep[3], 3) == LV_HARD && strstr(ui.msg, " HARD"), "... the step again: HARD said");
    key(WK(3));
    check(dstep_lvl(&t->dstep[3], 3) == LV_GHOST && strstr(ui.msg, " GHOST"), "... again: GHOST said");
    key(WK(3));
    check(dstep_has(&t->dstep[3], 3) && dstep_lvl(&t->dstep[3], 3) == LV_NORM && !st.held && ui.toast_t > 0u,
          "... again: NORM (never off; no step held), in a toast");
    ui.scr = SCR_HOME;                                  /* (the mixer: no grid, the toast is the only sign) */
    ui.toast_t = 0;
    key(WK(3));
    check(ui.scr == SCR_HOME && dstep_lvl(&t->dstep[3], 3) == LV_HARD && ui.toast_t > 0u && strstr(ui.msg, " HARD"),
          "drums, the mixer: a black key held + a step: HARD in a toast");
    ui.scr = SCR_STEP;
    fm1_in.notes &= ~(1u << BK(3));
    frame();
    key(WK(3));                                         /* (HARD -> off: a plain tap) */
    key(WK(3));                                         /* (back on, NORM: the next check clears it again) */
    key(WK(3));
    check(!dstep_has(&t->dstep[3], 3), "drums: the black key let go, the step tapped: off");
    lane_select(2);
    press(B_OCTUP);
    key(WK(1));
    release(B_OCTUP);
    check(dstep_lvl(&t->dstep[1], 2) == LV_HARD, "drums: OCT+ held + a step: a hard hit (OCT- is SHIFT now: the lane pick)");
    dstep_set(&t->dstep[5], 2, LV_NORM, 0);
    key(BK(5));
    check(lane_selected() == 5 && !st.play && ui.scr == SCR_STEP && ly_lock == LY_STEP && dstep_has(&t->dstep[5], 2),
          "drums: a black key: its lane picked, no step cleared, the white keys still steps");
    kdown(WK(5));
    tap(B_HOME);
    kup(WK(5));
    check(lane_selected() == 5 && !dstep_mask(&t->dstep[5]) && ui.scr == SCR_STEP && ly_lock == LY_STEP && !st.play,
          "drums: a step held + HOME: the step cleared, no lane picked, STEP stays");
    {
        uint32_t lanes_before = lane_selected();
        dstep_set(&t->dstep[6], 1, LV_NORM, 0);
        lane_select(2);
        kdown(WK(6));
        tap(B_HOME);
        kup(WK(6));
        check(lane_selected() == 2u && !dstep_lanes(&t->dstep[6]) && lanes_before == 5u,
              "drums: a step held + HOME clears the whole step (any lane's hit) and picks no lane");
        lane_select(5);
    }
    dstep_set(&t->dstep[7], 1, LV_NORM, 0);
    press(B_SEQ);
    key(WK(7));
    release(B_SEQ);
    check(dstep_has(&t->dstep[7], 1), "drums: SEQ + a step clears nothing any more (2026-10-16: step held + HOME)");
    dstep_clr(&t->dstep[7], 1);
    dstep_clr(&t->dstep[7], 5);                         /* (the tap set the picked lane's hit) */
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
#if DLA_ON && FELUCCA_PLOCK                               /* (the user, 2026-10-12, D3: the lane's TUNE and DECAY locks) */
        check(SCR->rows() == STP_PAT + 1u + STP_TL && c.label && !strcmp(c.label, "TUNE") && c.kind == CK_VAL,
              "drums: the held step's cards LEVEL RATCHET TUNE DECAY");
#else
        check(SCR->rows() == STP_PAT + 1u + STP_TL && !c.label && c.kind == CK_NONE, "drums: the held step's cards LEVEL RATCHET - -");
#endif
        ui.force = 1;
        frame();
        ppm("opt-step-drum-held");
    }
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    fm1_in.buttons &= ~BT(B_OCTDN);
    kup(WK(4));
    frame();
    check(!dstep_has(&t->dstep[4], 5) && ui.scr == SCR_STEP, "a step held (a hard hit), let go as a tap: off, STEP stays");
    key(WK(4));
    kdown(WK(4));
    tap(B_HOME);
    kup(WK(4));
    frame();
    check(!dstep_mask(&t->dstep[4]) && ui.scr == SCR_STEP && ui.row[SCR_STEP] == 0, "drums: a step held + HOME: cleared (no NO, no page turned)");
    key(WK(4));
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    key(WK(4));
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    check(dstep_mask(&t->dstep[4]) && ui.scr == SCR_STEP, "drums: SHIFT held + a key: the step stays (the key picks), no clear");
    lane_select(5);
    kdown(WK(4));
    tap(B_HOME);
    kup(WK(4));                                         /* (cleared again, as the following tests expect) */
    check(!dstep_mask(&t->dstep[4]), "drums: ... a step held + HOME clears it");
    seq_long();
    check(st.play && ly_lock == LY_PLAY && ui.scr == SCR_STEP, "SEQ held alone on STEP (past HOLD): the keys play, STEP stays");
    key(BK(7));
    check(!dstep_mask(&t->dstep[7]) && lane_selected() == 7, "the keys playing: a black key, no step set, the lane is the last hit");
    seq_long();
    check(!st.play && ly_lock == LY_STEP, "SEQ held again: the keys are steps");
    turn(EN_SELECT, 9);
    turn(EN_SELECT, -(int32_t)STP_TL);                 /* (past LANE: TOOLS, EUCLID, op_tools.c) */
    check(lane_selected() == 7 && ui.row[SCR_STEP] == STP_PAT, "drums, no step held: SELECT walks PATTERN, then the tools (the lane stays 8)");
    {
        cell_t c;
        step_cell(ui.row[SCR_STEP], 0, &c);
        check(c.d == &TP[P_SLEN], "drums, no step held: PATTERN first, no LANE page (LEVEL TUNE DECAY REV)");
    }
    /* the window */
    t->p[P_SLEN] = 40;
    frame();
    {
        char h[40];
        int8_t oct = song.octave;
        turn(EN_PRESET, 1);
        head_title(SCR_STEP, 0, h, sizeof h);
        check(st.page == 1 && !st.follow && !strcmp(h, "Pattern") && ui.scr == SCR_STEP && song.octave == oct,
              "PRESETS: the window 17-32 (the strip lights it; the header the page's name, #17), FOLLOW off, no NO, the octave kept");
        ui.force = 1;
        frame();
        ppm("opt-step-window");
        key(WK(0));
        check(dstep_has(&t->dstep[16], lane_selected()), "the window 17-32: the first key is step 17");
        turn(EN_PRESET, 1);
        head_title(SCR_STEP, 0, h, sizeof h);
        check(st.page == 2 && !strcmp(h, "Pattern"), "PRESETS again: 33-40 (LEN 40)");
        key(WK(10));
        check(!dstep_mask(&t->dstep[42]), "a key past LEN: nothing");
        turn(EN_PRESET, 1);
        check(st.page == 2, "the window stops at LEN");
        press(B_OCTDN);
        turn(EN_PRESET, -1);
        release(B_OCTDN);
        check(!st.follow && ui.scr == SCR_STEP && st.page == 2, "OCT- (SHIFT) + PRESETS: nothing (the window not scrolled)");
        press(B_OCTUP);
        turn(EN_PRESET, 1);
        release(B_OCTUP);
        check(st.follow && ui.scr == SCR_STEP && st.page == 2, "OCT+ (SHIFT2) + PRESETS: FOLLOW again (the window not scrolled)");
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
    op_arm(ARM_OP, OA_INIT, song.sel, "INIT", trk_tag(song.sel), 1);
    key(WK(2));
    check(op_armed() && !dstep_mask(&t->dstep[2]), "a question asked: the step keys do nothing");
    tap(B_HOME);
    song.sel = 0;
    reset_ui();
}

/* SLOOP #33 / #107: the SLOOP UI keeps SLOOP's SEQ convention (SONG is SEQ's last page, tests/sloop_bp_ui.c); this UI
 * keeps its own: SEQ tapped is STEP, from anywhere, the SONG screen visited or not, and never SONG */
static void seq_never_song_tests(void)
{
    uint32_t i, song_seen = 0;
    reset_ui();
    op_enter(SCR_PROJECT);
    press(B_SAVE);
    frames(HOLD_FRAMES);
    release(B_SAVE);
    check(ui.scr == SCR_SONG, "SEQ vs SONG: the SONG screen visited (SAVE held on PROJECT)");
    reset_ui();
    tap(B_SEQ);
    check(ui.scr == SCR_STEP, "SEQ tapped after SONG was visited: STEP, not SONG (SLOOP #33 is the SLOOP UI's)");
    for (i = 0; i < 8u; i++) {
        tap(B_SEQ);
        song_seen |= ui.scr == SCR_SONG;
    }
    check(!song_seen, "SEQ tapped again and again: never the SONG screen");
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
    {   /* SLOOP #41 (2.5 fa9ce57) is the SLOOP UI's: here the pick is the chord as it sounds (seq.c key_down's pen) */
        uint8_t c[4];
        uint32_t m, j, same;
        t->p[P_CHORD] = 1;                              /* TRIAD */
        press(B_SEQ);
        fm1_in.notes |= 1u << WK(2);                    /* one key */
        frame();
        fm1_in.notes = 0;
        frame();
        release(B_SEQ);
        m = chord_notes(t, kb_map(t, WK(2)), c);
        same = pen_n == m && m == 3u;
        for (j = 0; same && j < m; j++)
            same = pen_note[j] == c[j];
        check(same, "synth, chord mode: SEQ held + one key: the pick is the whole chord");
        memset(&t->step[7], 0, sizeof t->step[7]);
        key(WK(7));
        same = t->step[7].n == 3u;
        for (j = 0; same && j < 3u; j++)
            same = t->step[7].note[j] == pen_note[j];
        check(same, "synth, chord mode: the tapped step takes the whole chord, not only its root (SLOOP #41)");
        memset(&t->step[7], 0, sizeof t->step[7]);
        t->p[P_CHORD] = 0;
    }
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
#if FELUCCA_PLOCK
    {   /* a live-recorded length (seq.c rec_len_end: a GATE lock on the last step): LENGTH shows it; KNOB 4 whole steps */
        cell_t c;
        int32_t v;
        (void)lock_set(t, 6, P_SGATE, 39);           /* 3 NOTE, 4 .. 6 TIE: 6 ends 0.5 + 39 / 128 in, 3.8 steps */
        kdown(WK(3));
        step_cell(0, 3, &c);
        check(!strcmp(c.val, "3.8"), "synth: LENGTH of a recorded note: the fraction (3.8)");
        turn(EN_K4, -1);
        step_cell(0, 3, &c);
        check(!strcmp(c.val, "3") && !lock_get(t, 6, P_SGATE, &v) && t->step[6].time != ST_TIE,
              "synth: KNOB 4 on it: whole steps, the fraction goes");
        turn(EN_K4, 1);
        kup(WK(3));
    }
#endif
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
    turn(EN_SELECT, 1);                                 /* (SELECT pages the held step's cards: page 2, the extras) */
    turn(EN_K1 + (FELUCCA_CHANCE ? 1u : 0u), 3);        /* (CHANCE first when built, then NUDGE) */
    check(step_micro(t, 3) == 3 && ui.scr == SCR_STEP, "a step held + SELECT to the extras, NUDGE +3");
    turn(EN_SELECT, -1);
#endif
#if FELUCCA_FILLS
    tap(B_SAVE);
    check(step_fill(t, 3) == FC_FILL && ui.scr == SCR_STEP, "a step held + SAVE: its fill condition (FILL ONLY)");
#endif
    ui.force = 1;
    frame();
    ppm("opt-step-extras");
    kup(WK(3));
#if FELUCCA_MICRO || FELUCCA_FILLS
    check(step_on(&t->step[3]), "edited: kept when let go");
#endif
#endif
#if FELUCCA_PLOCK
    kdown(WK(3));
    press(B_ENV);
    check(st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].fam == FAM_ENV, "a step held + ENV: the ENV page's cells as its locks");
    turn(EN_K1, 5);
    {
        int32_t v = 0;
        cell_t c;
        step_cell(0, 0, &c);
        check(lock_get(t, 3, PAGES[st.lock_pg].id[0], &v) && v == t->p[PAGES[st.lock_pg].id[0]] + 5 && c.mark == 1u,
              "a turn writes a lock on the step, its card marked");
        ui.force = 1;
        frame();
        ppm("opt-step-lock");
    }
    fm1_in.buttons |= BT(B_OCTDN);
    turn(EN_K1, 1);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    {
        int32_t v;
        check(!lock_get(t, 3, PAGES[st.lock_pg].id[0], &v) && ui.scr == SCR_STEP, "SHIFT + the knob: the lock cleared");
    }
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
    check(SG_TOP + SG_H + 2 <= SG_MK_Y && SG_PH_Y + 4 <= SP_H && SG_INFO_Y + 34 <= SG_MK_Y && SG_X + 16 * SG_CW <= 240 && RL_X + 16 * RL_CW <= 240,
          "the grid, its marks and playhead to the screen's foot, the held step's two lines over its foot, its width");
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

/* ---- phase 4: the held performance layers, the lock, the TEMPO page, SAVE / SHIFT + a button, SONG */
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
    check(px_at(CARD_X(0), OY_PANEL + 12u, C_LINE) && 2 + 4 * TILE_H <= LAY_SUB_Y && LAY_SUB_Y + 16 <= OH_BODY,
          "the 16 tiles drawn within the panel's band (124 rows)");
    kdown(WK(1));
    check(punch.req == 1, "FX + key 2: punch-in effect 2 (seq.c, the ISR)");
    kup(WK(1));
    {
        int16_t f = song.g[G_FILT], d = song.g[G_DUST];
        turn(EN_K1, -3);
        turn(EN_K2, 4);
        check(song.g[G_FILT] < f && (FELUCCA_FX_DUST ? song.g[G_DUST] > d : song.g[G_DUST] == d),
              "FX held: KNOB 1 FILTER, KNOB 2 DUST (without DUST built: its frame alone, nothing turns)");
        d = song.g[G_DUST];
        turn(EN_PRESET, 1);
        check(song.g[G_DUST] == d, "FX held: PRESETS no longer edits the hot knob (DUST)");
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
    /* GLO: mute (the sequencer's), FX or fills, tap tempo, the levels; no solo */
    press(B_GLO);
    key(WK(0));
    check(smute_trk_on(0) && !trk[0].p[P_MUTE], "GLO + key 1: T1 muted (the sequencer mute, the one mute)");
    key(WK(0));
    key(WK(5));
    check(!smute_trk_on(0) && song.solo == 0u && !smute_trk, "GLO + key 1 again: heard; key 6: nothing (no solo)");
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
    /* EDIT: SHIFT LENGTH TRANSPOSE, erase on the keys (undo: SAVE then HOME) */
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
    hold2(B_SAVE, B_HOME);
    check(t->p[P_SLEN] == 16 && t->step[0].note[0] == 60 && !step_on(&t->step[1]), "SAVE then HOME: the EDIT hold's edits undone (one level)");
    song.octave = 0;
    track_defaults_steps(t);
    /* lock a layer: the layer held then SHIFT2, or SHIFT2 held then the layer's button (the shifts' language: on) */
    hold2(B_OCTDN, B_FX);
    frames(2);
    check(lay.lock == LY_PLAY, "SHIFT held, FX pressed: no lock (SHIFT is off, SHIFT2 locks)");
    hold2(B_OCTUP, B_FX);
    frames(2);
    check(lay.lock == LY_FX && lay.shown == LY_FX && ui.scr == SCR_HOME && ly_lock == LY_FX,
          "SHIFT2 held, FX pressed: FX locked open (no NO), the ISR's layer too");
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
    hold2(B_GLO, B_OCTUP);
    frames(2);
    check(lay.lock == LY_MIX && lay.shown == LY_MIX && ui.scr == SCR_HOME, "GLO held, SHIFT2 pressed: the mix layer locked");
    key(WK(1));
    check(smute_trk_on(1), "locked GLO: key 2 mutes T2");
    key(WK(1));
    tap(B_ENV);
    check(lay.lock == LY_PLAY && ui.scr == SCR_HOME, "ENV lets it go (no SOUND rows)");
#if FELUCCA_PATTERNS
    /* LFO: the patterns (the built black-key modifiers) */
    press(B_LFO);
    frames(HOLD_FRAMES);
    check(lpk.on && ui.scr == SCR_SONG && song_on_pat_row(), "LFO held: SONG's PATTERNS row (the patterns screen)");
    {
        uint32_t sel0 = song.sel;
        turn(EN_ALGO, 1);
        check(song.sel == sel0 && lpk.on && !ui.master && ui.scr == SCR_SONG,
              "LFO held (PATTERNS): ALGORITHM does nothing (a global screen)");
        turn(EN_ALGO, -8);
        check(song.sel == sel0 && lpk.on && !ui.master && ui.scr != SCR_FX,
              "... left or right: the same (no MASTER, the screen kept)");
        song.sel = 0;
    }
    for (k = 0; k < 8u; k++)
        t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(48 + k);
    fm1_in.notes |= 1u << 13;                           /* black key 6 was the store modifier: nothing now */
    frame();
    key(WK(1));
    fm1_in.notes &= ~(1u << 13);
    frame();
    check(!pat_has(0, 1), "LFO + black 6 + white 2: no store any more (the upper-key modifiers are gone)");
    for (k = 0; k < 8u; k++)                            /* (the key above launched the empty slot 2: the working copy again) */
        t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(48 + k);
    press(B_SAVE);                                      /* LFO held, then SAVE + n: store into slot n */
    key(WK(1));
    check(lpk.on, "LFO held + SAVE: the patterns screen stays");
    release(B_SAVE);
    check(pat_has(0, 1), "LFO held + SAVE + key 2: the working copy stored into T1 2");
    {   /* a brisk strum on the device: the key's press and SAVE's release land in one (slow) UI frame (the user, 2026-10-23:
         * SAVE + key opened PROJECT): still the chord, stored, no tap */
        uint32_t scr = ui.scr;
        press(B_SAVE);
        fm1_in.notes |= 1u << WK(2);
        fm1_btn_t[1][panel.btn[B_SAVE]] = fm1_ticks();
        fm1_in.buttons &= ~BT(B_SAVE);
        frame();
        fm1_in.notes &= ~(1u << WK(2));
        frame();
        check(pat_has(0, 2) && ui.scr == scr && !op_armed(), "LFO held, SAVE + key 3 with SAVE let go in the key's frame: stored, the screen stays (no SAVE tap)");
    }
    press(B_HOME);                                       /* LFO held, then HOME + n: clear slot n (asked) */
    key(WK(1));
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 2?") && lay.lock == LY_PLAY, "LFO held + HOME + key 2: clear slot 2, asked");
    release(B_HOME);
    frames(2);
    check(lay.lock == LY_PLAY, "... HOME let go after a clear: the layer is not locked");
    tap(B_SAVE);
    check(!pat_has(0, 1), "... YES: cleared");
    press(B_OCTDN);                                     /* SHIFT tapped with LFO held and no key: no lock (SHIFT2 locks) */
    release(B_OCTDN);
    frames(2);
    check(lay.lock == LY_PLAY, "LFO held + SHIFT tapped: no lock (2026-10-16: SHIFT2 locks a layer)");
    release(B_LFO);
    frames(2);
    hold2(B_OCTUP, B_LFO);                              /* SHIFT2 held, then LFO: locked (LFO's OCT+ is launch now, so SHIFT2 first) */
    frames(2);
    check(lay.lock == LY_PAT, "SHIFT2 held + LFO: the patterns layer locked open");
    press(B_SAVE);                                      /* the pattern page locked: SAVE + key stores, the lock and the page stay (the user, */
    key(WK(1));                                         /* 2026-10-23: SAVE there left the page for the one under it) */
    release(B_SAVE);
    check(lay.lock == LY_PAT && pat_has(0, 1) && ui.scr == SCR_SONG, "... locked: SAVE + key 2 stores into T1 2, the page stays");
    press(B_HOME);
    key(WK(1));
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 2?") && lay.lock == LY_PAT, "... locked: HOME + key 2 asks to clear");
    release(B_HOME);
    tap(B_HOME);
    check(!op_armed() && lay.lock == LY_PAT && pat_has(0, 1), "... HOME: no, still locked");
    press(B_LFO);                                       /* the page locked: LFO is the store's button too (the user, 2026-10-23) */
    key(WK(3));                                         /* (slot 4: free; slot 3 holds the brisk strum's store above) */
    release(B_LFO);
    check(lay.lock == LY_PAT && pat_has(0, 3) && ui.scr == SCR_SONG && !op_armed(), "... locked: LFO + key 4 stores into T1 4, the page stays");
    tap(B_LFO);
    frames(2);
    check(lay.lock == LY_PAT && ui.scr == SCR_SONG && !op_armed(), "... locked: LFO alone does nothing (no LFO pages)");
    tap(B_HOME);
    frames(2);
    check(lay.lock == LY_PLAY && ui.scr != SCR_SONG, "... HOME alone: the page let go, back to the screen under it");
    hold2(B_OCTUP, B_LFO);
    frames(2);
    tap(B_ENV);
    frames(2);
    check(lay.lock == LY_PLAY, "... any other button lets it go");
    press(B_LFO);
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    ppm("opt-layer-lfo");
    {   /* LFO held long enough: the patterns screen, its knobs are the patterns' (the track selected, a cue); a knob turned
         * at once (before the hold) is SHIFT and keeps the screen under it (ui_optimist_len.h: LEN by one) */
        check(lpk.on && ui.scr == SCR_SONG, "LFO held: the patterns screen (SONG's PATTERNS row)");
        turn(EN_K2, 1);
        check(song.sel == 1u && lay.shown == LY_PLAY, "LFO held + KNOB 2 on the patterns screen: T2 selected");
        turn(EN_K1, 1);
        song.sel = 0;
    }
    release(B_LFO);
    track_defaults_steps(t);
    {   /* PRESETS / the engine step: a popup list of the sounds over the panel (op_preset.c draw_pop) */
        reset_ui();
        song.sel = 0;
        op_enter(SCR_SOUND);
        ui.row[SCR_SOUND] = 0;
        ui.force = 1;
        frames(2);
        turn(EN_PRESET, 1);
        frames(3);
        check(op_overlay() == 5u && pop_kind == 1u, "PRESETS turned: the popup list of the sounds");
        {   /* the engines' own collections are in the whole list from any track (the user, 2026-10-21: FM6's packs "sometimes" missing) */
            uint32_t all0, all1, i, k, e, f1 = 0, f2 = 0, own = 0;
            set_engine_of(TSEL, 0u);
            snd_mark(song.sel);
            (void)preset_all_pos(&all0);
            set_engine_of(TSEL, 1u);
            snd_mark(song.sel);
            (void)preset_all_pos(&all1);
            for (i = 0; i < all1; i++) {
                e = preset_all_at(i, &k);
                f1 += e == NB_LIST && (k >> 8) == 1u;
                f2 += e == NB_LIST && (k >> 8) == 2u;
            }
            check(all0 == all1 && f1 == 3u && f2 == 2u, "the whole list holds both collections (3 + 2 entries) whichever engine the track is on");
            pl_whole = 0;
            (void)preset_pos(&own);
            for (i = 0, f1 = 0, f2 = 0; i < own; i++) {
                e = preset_at(i, &k);
                f1 += e == NB_LIST && (k >> 8) == 1u;
                f2 += e == NB_LIST && (k >> 8) == 2u;
            }
            check(f1 == 0u && f2 == 2u, "... EDIT's PRESET card's list: only the track's engine's own collection");
            pl_whole = 1;
            for (i = 0; i < all1; i++)
                if (preset_all_at(i, &k) == NB_LIST && (k >> 8) == 1u && (k & 255u) == 2u)
                    break;
            op_preset_go(i);
            check(TSEL->eng_req == 0u && nb_loaded_slot == 2u, "picking another engine's collection entry: the track takes that engine, the slot loaded");
            set_engine_of(TSEL, 1u);
            snd_mark(song.sel);
            pl_whole = 1;
        }
        {   /* PRESETS: one big list of every engine's sounds (it crosses engines); EDIT's PRESET card keeps to the engine's own */
            uint32_t e0 = tsel()->eng_req, r, left = 0, total, all;
            for (r = 0; r < 150u; r++) {
                turn(EN_PRESET, 1);
                frame();
                left |= tsel()->eng_req != e0;
            }
            check(left, "PRESETS: 150 detents on cross into other engines (one big list of everything)");
            (void)preset_pos(&total);
            (void)preset_all_pos(&all);
            check(total == all, "... the browsing list is the whole list");
            set_engine_of(TSEL, e0);
            snd_mark(song.sel);
            pl_whole = 0;                               /* (EDIT's card: the engine's own list) */
            op_preset_go(0);
            left = 0;
            for (r = 0; r < 150u; r++) {
                turn(EN_K2, 1);                         /* (EDIT's PRESET card) */
                frame();
                left |= tsel()->eng_req != e0;
            }
            check(!left, "EDIT's PRESET card: 150 detents on, the engine never changes");
            (void)preset_pos(&total);
            check(total > 0u && total < all, "... its list is the engine's part of the whole list");
            press(B_OCTUP);
            turn(EN_PRESET, 1);
            release(B_OCTUP);
            frames(2);
            check(tsel()->eng_req != e0, "SHIFT2 + PRESETS: the engine step still changes the engine");
            op_enter(SCR_SOUND);
            ui.row[SCR_SOUND] = 0;
            ui.force = 1;
            frames(2);
        }
        ppm("opt-preset-popup");
        frames(200);                                    /* (the toast, and the popup with it, gone) */
        check(op_overlay() != 5u, "... the popup is gone with its toast");
        {   /* the popup covers the top cards too: they are drawn again when it goes (the mixer showed it left on them) */
            uint32_t i, before = 0, after = 0;
            reset_ui();
            song.sel = 0;
            op_enter(SCR_HOME);
            ui.force = 1;
            frames(5);
            for (i = 24u * 240u; i < 130u * 240u; i++)
                before = before * 31u + screen[i];
            turn(EN_PRESET, 1);
            frames(3);
            check(op_overlay() == 5u, "the mixer, PRESETS turned: the popup list");
            frames(200);
            turn(EN_PRESET, -1);
            frames(200);
            for (i = 24u * 240u; i < 130u * 240u; i++)
                after = after * 31u + screen[i];
            check(op_overlay() != 5u && before == after, "... gone with its toast: the cards and rows under it drawn again, as before");
            reset_ui();
            song.sel = 0;
            op_enter(SCR_SOUND);
            ui.row[SCR_SOUND] = 0;
            ui.force = 1;
            frames(2);
        }
        turn(EN_K1, 1);                                 /* EDIT page 1: KNOB 1 the sound (the user, 2026-10-21: "see the popup") */
        frames(3);
        check(op_overlay() == 5u && pop_kind == 4u, "EDIT's SOUND row, KNOB 1 (ENGINE): the popup lists the engines only");
        ppm("opt-engine-popup");
        frames(200);
        turn(EN_K2, 1);                                 /* KNOB 2: the PRESET, the engine's own sounds */
        frames(3);
        check(op_overlay() == 5u && pop_kind == 5u, "EDIT's SOUND row, KNOB 2 (PRESET): the popup list of the engine's own sounds");
        frames(200);
        ppm("opt-edit-after-popup");
        {   /* any card with a list value (an insert's type, a scale, a mode): the same popup, its names, the value in the middle */
            static const uint8_t SCRS[3] = {SCR_SOUND, SCR_FX, SCR_STEP};
            uint32_t si, r, k, hit = 0, n3 = 0;
            for (si = 0; si < 3u && !hit; si++) {
                op_enter(SCRS[si]);
                for (r = 0; r < SCR->rows() && !hit; r++)
                    for (k = 0; k < 4u && !hit; k++) {
                        cell_t cc;
                        frames(200);
                        ui.row[ui.scr] = (uint8_t)r;
                        frame();
                        SCR->cell(r, k, &cc);
                        if (cc.kind != CK_VAL || !cc.d || cc.d->fmt != F_ENUM || cc.d->max - cc.d->min < 2 || is_go(cc.d) || ui.scr == SCR_STEP)
                            continue;
                        turn(EN_K1 + k, 1);
                        frames(3);
                        n3++;
                        if (op_overlay() == 5u && pop_kind == 3u) {
                            hit = 1;
                            ppm("opt-enum-popup");
                        }
                    }
            }
            (void)n3;
#if FELUCCA_DIV_ORDER
            {   /* a division list shows in the order the knob walks (by length), not the stored index order */
                const param_desc_t *dd = &TP[P_SDIV];
                int32_t p, ok = 1;
                for (p = dd->min; p < dd->max; p++)
                    ok &= param_step(dd, enum_at(dd, p), 1) == enum_at(dd, p + 1) && enum_rank(dd, enum_at(dd, p)) == p;
                check(ok, "the popup lists a division card's values in the order the knob steps through them (DIV_ORDER)");
            }
#endif
            check(hit, "a card with a list value (not a sound): KNOB turned: the popup list of its values (pop_kind 3)");
            if (hit) {   /* at the list's end: nothing drawn below it (the first items were shown there) */
                for (r = 0; r < 70u; r++)
                    turn(EN_K1 + (k - 1u), 1);
                frames(3);
                check(*pen.vp == pen.d->max && op_overlay() == 5u, "... turned to the end of the list: the last value, the popup up");
                {
                    uint32_t y, any = 0;
                    for (y = 168u; y < 190u; y++)
                        any |= px_in(14, y, 200, 1, C_GRAY) || px_in(14, y, 200, 1, C_WHITE);
                    check(!any, "... nothing below the last item (no wrap to the top of the list)");
                }
            }
            reset_ui();
            song.sel = 0;
            op_enter(SCR_SOUND);
            ui.row[SCR_SOUND] = 0;
            ui.force = 1;
            frames(2);
        }
        {   /* scrolling with the popup up: its box is drawn once a detent, the panel under it not at all (it flashed) */
            uint32_t d, worst = 0;
            for (d = 0; d < 8u; d++) {
                probe_hits = 0;
                turn(EN_PRESET, 1);
                frame();
                if (probe_hits > worst)
                    worst = probe_hits;
            }
            check(worst <= 1u, "PRESETS scrolled with the popup up: one blit over its box a detent (no flash from the panel under it)");
        }
        press(B_OCTUP);
        turn(EN_PRESET, 1);
        release(B_OCTUP);
        frames(3);
        check(op_overlay() == 5u, "SHIFT2 + PRESETS (the engine step): the popup list too");
        ppm("opt-engine-popup");
        frames(OP_TOAST_FRAMES + 5u);
        check(op_overlay() == 0u, "... gone with the toast, ~1.5 s after the last turn");
        song.sel = TRK_DRUM;
        ui.lane = 0;
        ui.force = 1;
        frames(2);
        turn(EN_PRESET, 1);
        frames(3);
        ppm("opt-kit-popup");
        song.sel = 0;
        reset_ui();
    }
    {   /* SAVE tapped on the patterns screen (LFO held): the track's pattern is stored, the screen stays */
        reset_ui();
        op_enter(SCR_HOME);
        frame();
        press(B_LFO);
        frames(HOLD_FRAMES);
        tap(B_SAVE);
        frames(2);
        check(lpk.on && ui.scr == SCR_SONG && (op_armed() || pat_has(0, 0) || pat_cur[0] < PAT_N || !strncmp(ui.msg, "NO FREE", 7)),
              "LFO held + SAVE tapped: the patterns screen stays, the track's pattern is stored (asked over a used slot)");
        if (op_armed())
            tap(B_HOME);
        {
            int32_t oc = song.octave;
            tap(B_OCTUP);
            tap(B_OCTDN);
            frames(2);
            check(song.octave == oc && strncmp(ui.msg, "OCTAVE", 6) != 0 && lpk.on, "... OCT+ / OCT- on the patterns screen: nothing (no octave change)");
        }
        release(B_LFO);
        frames(2);
        check(ui.scr == SCR_HOME && !lpk.on, "... LFO let go: back to the mixer");
        reset_ui();
        op_enter(SCR_HOME);
        frame();
        press(B_LFO);
        frames(HOLD_FRAMES);
        tap(B_ENV);                                     /* a page button during the hold: you go there and stay */
        frames(2);
        release(B_LFO);
        frames(3);
        check(ui.scr == SCR_SOUND && !lpk.on, "LFO held + ENV tapped, LFO let go: SOUND stays (no jump back to the screen it came from)");
        reset_ui();
        op_enter(SCR_HOME);
        frame();
        press(B_LFO);
        frames(HOLD_FRAMES);
        tap(B_SEQ);                                     /* SEQ tapped: SONG's views in turn, still the peek */
        frames(2);
        release(B_LFO);
        frames(3);
        check(ui.scr == SCR_HOME && !lpk.on, "LFO held + SEQ tapped, LFO let go: back to the mixer");
        reset_ui();
    }
#endif
    {   /* every layer's title and footer lines fit; a scene's and a note's letter keep their capital */
        static const uint8_t L[] = {LY_FX, LY_ERASE, LY_ROLL, LY_SCALE, LY_MIX FIF(FELUCCA_PATTERNS)(, LY_PAT, LY_PAT)};
        uint32_t i, bad = 0;
        char h[40], kk[40], b[32];
        for (i = 0; i < sizeof L; i++) {
            lay.shown = L[i];
#if FELUCCA_PATTERNS
            lay.pmod = (uint8_t)(i + 1u == sizeof L ? 10u : 0u);   /* (the last: the LFO layer's scenes) */
#endif
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
#if FELUCCA_PATTERNS
        lay.pmod = 0;
#endif
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
    check(lay.shown == LY_PLAY, "SAVE held: no layer any more (2026-10-16: the scenes are the LFO layer's)");
    release(B_SAVE);
    frames(2);
    reset_ui();
#if FELUCCA_PATTERNS
    press(B_LFO);
    frames(HOLD_FRAMES);
    key(BK(9));                                         /* black 10: the white keys are the scenes */
    check(lpk.on && lay.pmod == 10u && !strcmp(ui.msg, "SCENES"), "LFO + black 10: the scenes");
    ui.force = 1;
    frame();
    ppm("opt-layer-scenes");
    work_mark();                                        /* (the work as stored: no question, op_edits.c) */
    key(WK(1));
    check(live_sec == 1 && !strcmp(ui.msg, "LOADED B"), "the scenes + key 2, stopped: scene B loaded");
    key(WK(2));
    check(!strcmp(ui.msg, "EMPTY C"), "the scenes + key 3: C is empty, said");
    press(B_SAVE);
    key(WK(1));
    release(B_SAVE);
    check(op_armed() && ui.arm_scr == ARM_OP && !strcmp(ui.arm_q, "STORE SCENE B?") && srec == 0,
          "the scenes, SAVE + key 2: store the loop into B, asked (used); no SONG REC");
    tap(B_SAVE);
    check(!op_armed() && (saves || project_used(1)), "... YES: stored");
    press(B_HOME);
    key(WK(0));
    release(B_HOME);
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE A?") && strncmp(ui.msg, "UNDO", 4) && strncmp(ui.msg, "NOTHING", 7),
          "the scenes, HOME + key 1: clear A, asked; no undo");
    tap(B_HOME);
    key(BK(9));
    check(lay.pmod == 0u && !strcmp(ui.msg, "PATTERNS"), "black 10 again: the patterns");
    key(BK(9));
    release(B_LFO);
    frames(2);
    check(lay.pmod == 0u, "LFO let go: the patterns again next time");
#endif
    press(B_SEQ);
    tap(B_REC);
    check(srec == 1, "SEQ + REC tapped: SONG REC armed");
    tap(B_REC);
    release(B_SEQ);
    check(srec == 0, "... again: off");
    arrangement.count = 2;                              /* (a song of A and B: both stored) */
    arrangement.entry[0].scene = 0, arrangement.entry[1].scene = 1, arrangement.entry[0].bars = arrangement.entry[1].bars = 4;
    hold2(B_SEQ, B_PLAY);
    frames(3);
    check(arrangement_enabled && (song.playing || transport_req), "SEQ + PLAY: the song from its start");
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
    turn(EN_SELECT, 1);
    {   /* REC 2: QUANT (REC QUANTIZE), ON by default; BPM and NUDGE stay together on the TEMPO row. Without REC MODES QUANT is
         * REC's first card (MODE's, empty there) and REC 2 is no page (the audit's #21: no one-card page) */
        cell_t c, b, n;
        const uint32_t qr = FELUCCA_REC_MODES ? 2u : 1u;
        SCREENS[SCR_TEMPO].cell(qr, 0, &c);
        tp_cell(0, 0, &b);
        tp_cell(0, 1, &n);
        check(ui.row[SCR_TEMPO] == qr && SCREENS[SCR_TEMPO].rows() == qr + 1u && c.label && !strcmp(c.label, "QUANT") &&
              song.g[G_RECQ] == 1 && !strcmp(b.label, "BPM") && !strcmp(n.label, "NUDGE"),
              "TEMPO: SELECT again, REC 2: QUANT, ON (BPM NUDGE on TEMPO; no REC MODES: QUANT on REC, two pages)");
        turn(EN_K1, -1);
        check(song.g[G_RECQ] == 0, "TEMPO REC 2: KNOB 1 QUANT OFF");
        ui.force = 1;
        frame();
        ppm("opt-tempo-rec2");
        turn(EN_K1, 1);
        check(song.g[G_RECQ] == 1, "... and ON");
    }
    turn(EN_SELECT, -3);
    key(WK(0));
    frames(29);
    key(WK(0));
    check(song.g[G_BPM] >= 115 && song.g[G_BPM] <= 125 && ui.scr == SCR_TEMPO, "TEMPO: a white key taps the tempo");
    frames(130);                                        /* (over 2 s: a new count) */
    song.g[G_BPM] = 60;
    tap(B_HOME);
    frames(29);
    tap(B_HOME);
    check(song.g[G_BPM] >= 115 && song.g[G_BPM] <= 125 && ui.scr == SCR_TEMPO,
          "TEMPO: HOME taps the tempo too (PLAY held; HOME let go is no back)");
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
    hold2(B_OCTDN, B_SEQ);
    check(!op_armed(), "SHIFT + SEQ: no question any more (2026-10-16: SHIFT + REC)");
    op_enter(SCR_HOME);
#if FELUCCA_MICRO
    step_micro_set(&trk[0], 5, 7);
#endif
#if FELUCCA_PLOCK
    fm1_irq_off();
    (void)lock_set(&trk[0], 3, P_SGATE, 100);           /* (a recorded note's length: seq.c rec_gate_set) */
    (void)lock_set(&trk[0], 4, P_ATK, 50);
    fm1_irq_on();
#endif
    press(B_OCTDN);
    press(B_REC);
    frames(RH_CLEAR_MS / 16u + 3u);
    release(B_REC);
    release(B_OCTDN);
    frames(2);
    check(!op_armed() && !song.rec && !strcmp(ui.msg, "T1 LOCKS CLEARED"),
          "SHIFT + REC held, the ring closed: the pattern's locks, nudges, fills, motion cleared, the notes stay");
#if FELUCCA_MICRO
    check(step_micro(&trk[0], 5) == 0, "... the nudge gone");
#endif
#if FELUCCA_PLOCK
    {
        int32_t g;
        check(lock_get(&trk[0], 3, P_SGATE, &g) && g == 100 && !lock_get(&trk[0], 4, P_ATK, &g),
              "... the locks gone, the recorded note length (its GATE lock) kept");
    }
    {                                                   /* a knob's parameter (REC held + the knob): the whole track */
        cell_t c;
        int32_t g;
        fm1_irq_off();
        (void)lock_set(&trk[0], 2, P_ATK, 40);
        (void)lock_set(&trk[0], 6, P_ATK, 41);
        (void)lock_set(&trk[0], 6, P_DEC, 42);
        fm1_irq_on();
        cell_param(&c, &TP[P_ATK], &trk[0].p[P_ATK]);
        ui.scr = SCR_SOUND;
        check(knob_auto_clear(0, 0, &c) && !lock_get(&trk[0], 2, P_ATK, &g) && !lock_get(&trk[0], 6, P_ATK, &g) &&
              lock_get(&trk[0], 6, P_DEC, &g) && lock_get(&trk[0], 3, P_SGATE, &g) && strstr(ui.msg, "CLEARED"),
              "a knob's clear: that parameter's locks on every step go, the others and the note lengths stay");
        check(knob_auto_clear(0, 0, &c) && strstr(ui.msg, "NO LOCKS") && lock_get(&trk[0], 6, P_DEC, &g),
              "... none left: said so, nothing changes");
        {                                               /* REC held + a knob: the same clear; REC's press undone, no ring */
            uint32_t b0 = fm1_in.buttons, r0 = song.rec;
            fm1_irq_off();
            (void)lock_set(&trk[0], 3, P_ATK, 43);
            fm1_irq_on();
            check(!rec_knob(0, 0, &c), "REC up: a knob is a knob");
            fm1_in.buttons = b0 | 1u << panel.btn[B_REC];
            memset(&rh, 0, sizeof rh);
            rh.on = 1, rh.prev_rec = 0, rh.prev_wait = 0, rh.t0 = fm1_ms;
            song.rec = 1;                               /* (the press armed it) */
            check(rec_knob(0, 0, &c) && !lock_get(&trk[0], 3, P_ATK, &g) && strstr(ui.msg, "CLEARED") && song.rec == 0 &&
                      !rh.on && !rh.ring && rh.knob,
                  "REC held + a knob: that parameter's locks go, REC's press is undone");
            check(rec_knob(0, 0, &c) && !strstr(ui.msg, "NO LOCKS"), "... once a knob a hold");
            rec_held(fm1_in.buttons);
            check(!rh.ring, "... and the ring never starts, so the track is not cleared");
            fm1_in.buttons = b0;
            rec_held(fm1_in.buttons);
            check(!rh.knob && !rh.kdone, "... REC up: the next hold is a new one");
            song.rec = r0;
        }
        ui.scr = SCR_HOME;
    }
#if DLA_ON
    {                                                   /* SHIFT + REC's ring with a drum lane picked: that lane's only */
        int32_t x;
        fm1_irq_off();
        (void)auto_put(AL(TDRUM), 4u | AUTO_ONLY, AUTO_LANE(2, 1), 10);
        (void)auto_put(AL(TDRUM), 5u | AUTO_ONLY, AUTO_LANE(3, 1), 11);
        fm1_irq_on();
        op_clear_motion(TRK_DRUM, 2);
        check(!auto_get(AL(TDRUM), 4u | AUTO_ONLY, AUTO_LANE(2, 1), &x) && auto_get(AL(TDRUM), 5u | AUTO_ONLY, AUTO_LANE(3, 1), &x) &&
              !strcmp(ui.msg, "CLAP LOCKS CLEARED"), "SHIFT + REC's ring, a lane picked: its locks go, the other lanes' stay");
        op_clear_motion(TRK_DRUM, -1);
        check(!auto_get(AL(TDRUM), 5u | AUTO_ONLY, AUTO_LANE(3, 1), &x), "... no lane picked: the whole drum track's");
    }
#endif
#endif
    trk[0].p[P_ATK] = (int16_t)(trk[0].p[P_ATK] + 1);   /* (an edit: INIT asks over edits only, op_edits.c) */
    hold2(B_OCTDN, B_ENV);
    check(op_armed() && !strcmp(ui.arm_q, "INIT T1?"), "SHIFT + ENV over an edited sound: INIT, asked");
    tap(B_HOME);
    song.octave = 2;
    hold2(B_OCTDN, B_OCTUP);
    check(song.octave == 2 && master_sel(), "both OCT off STEP: MASTER, the octave kept");
    hold2(B_OCTDN, B_OCTUP);
    check(song.octave == 2 && !master_sel() && ui.scr == SCR_HOME, "both OCT again: back on the track, no NO");
    song.octave = 0;
    {                                                   /* FOLD: both OCT tapped on a synth track's STEP */
        track_t *ft = &trk[0];
        int32_t n, gaps = 0;
        int16_t root0 = trk[0].p[P_ROOT];
        uint32_t i;
        song.sel = 0;
        for (i = 0; i < NSTEP; i++)
            ft->step[i].time = ST_REST, ft->step[i].n = 0;
        ft->step[0].time = ST_NOTE, ft->step[0].n = 1, ft->step[0].note[0] = 60;
        ft->step[5].time = ST_NOTE, ft->step[5].n = 1, ft->step[5].note[0] = 67;
        trk[0].p[P_SCALE] = 0;
        op_enter(SCR_STEP);
        frame();
        hold2(B_OCTDN, B_OCTUP);
        check(roll_fold && ui.scr == SCR_STEP && !master_sel() && !strcmp(ui.msg, "FOLD: NOTES"),
              "both OCT tapped on STEP (a synth track): FOLD on, no MASTER");
        ui.force = 1;
        frame();
        ppm("opt-step-fold");
        roll_range(ft);
        for (n = 61; n < 67; n++)
            gaps += RV(n);
        check(RV(60) && RV(67) && !gaps, "... the roll: the pattern's notes' rows only (C4, G4; not the pick's)");
        st.nsel = 0;
        held_pitch_one(ft, 0, 1);
        check(ft->step[0].note[0] == 67, "... ALGORITHM on a held note, folded: the next row up (C4 to G4, not C#4)");
        held_pitch_one(ft, 0, 1);
        check(ft->step[0].note[0] == 68, "... past the top row: one semitone");
        held_pitch_one(ft, 0, -2);
        check(ft->step[0].note[0] == 66, "... two detents down: G4's row, then a semitone (C4's row went with the only C4)");
        ft->step[0].note[0] = 60;
        trk[0].p[P_SCALE] = 1, trk[0].p[P_ROOT] = 0;      /* (MAJ, root C) */
        ui.force = 1;
        frame();
        ppm("opt-step-fold-scale");
        roll_range(ft);
        check(RV(62) && RV(64) && RV(65) && !RV(61) && !RV(63),
              "... a scale set: the scale's rows over the pattern's span");
        trk[0].p[P_SCALE] = 0, trk[0].p[P_ROOT] = root0;
        press(B_OCTDN);
        press(B_OCTUP);
        frames(FOLD_TAP_MS / 16u + 2u);
        release(B_OCTUP);
        release(B_OCTDN);
        check(roll_fold && !master_sel() && ui.scr == SCR_STEP, "... held too long: no toggle (and no MASTER on STEP)");
        hold2(B_OCTDN, B_OCTUP);
        roll_range(ft);
        check(!roll_fold && !strcmp(ui.msg, "FOLD OFF") && RV(61) && RV(66), "... tapped again: FOLD off, every row");
        ft->step[0].time = ft->step[5].time = ST_REST;
        op_enter(SCR_HOME);
        frame();
    }
    tap(B_PLAY);
    frames(2);
    hold2(B_OCTDN, B_PLAY);
    frames(3);
    check(!song.playing && !strcmp(ui.msg, "ALL SOUND OFF") && ui.scr == SCR_HOME, "SHIFT + PLAY: stop, every voice off");
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
static void drumkit_popup_tests(void)
{   /* the kit popup lists what PRESETS steps through (factory kits as built, then the user kits), in that order, the playing one lit */
    uint32_t i, last, visited = 1, kits[64], tot, cur, ok = 1;
    reset_ui();
    song.sel = TRK_DRUM;
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    frames(2);
    for (i = 0; i < 200u; i++) {                        /* (down to the first kit) */
        last = drum_kit_pos();
        op_preset_step(-1);
        if (drum_kit_pos() == last)
            break;
    }
    for (i = 0; i < 200u; i++) {                        /* (up to the last: every position, the popup's list in step) */
        pop_drum_kits(kits, &tot, &cur);
        ok &= cur < tot && kits[cur] == drum_kit_pos();
        last = drum_kit_pos();
        op_preset_step(1);
        if (drum_kit_pos() == last)
            break;
        visited++;
    }
    pop_drum_kits(kits, &tot, &cur);
    ok &= tot == visited && kits[tot - 1u] == drum_kit_pos();
    check(ok, "the drum kit popup: its list is every kit PRESETS steps through, the playing one at its place each step, user kits last");
    reset_ui();
}
static void newproj_tests(void)
{   /* NEW PROJECT: blank like a project with nothing stored: the mix, the sends, the inserts, the drum lanes, the automation */
    reset_ui();
    song.sel = 0;
    trk[0].p[P_LEVEL] = 11;
    trk[0].step[0].time = ST_NOTE, trk[0].step[0].n = 1, trk[0].step[0].note[0] = 60;
    ins_slot[0][0] = 2;
    fxs_slot[0] = 2;
    dl.ofs[0][0] = 5;
    dl.src[0] = 1;
    dsend[0] = 9;
    song.g[G_SWING] = 20;
    op_global_go(G_NEWPRJ);
    check(trk[0].p[P_LEVEL] == TP[P_LEVEL].def && trk[0].step[0].time == ST_REST && song.g[G_SWING] == GP[G_SWING].def,
          "NEW PROJECT: the track's level, its steps and the globals at their defaults");
#if FELUCCA_PATTERNS
    check(ins_slot[0][0] != 2 && fxs_slot[0] != 2, "... the inserts and the FX slots empty again");
#endif
    check(dl.ofs[0][0] == 0 && dl.src[0] == 0 && dsend[0] == dsend_word(0, 0, 0), "... the drum lanes' tuning and sound, their sends: none, no reverb either");
    check(song.g[G_RSIZE] == RSIZE_NEW && (NPART < 1u || trk[0].p[P_REV] == 0), "... the reverb SIZE 80 %, no reverb send on a part (the user, 2026-10-23)");
#if FELUCCA_DRUM_ACCENT
    {   /* the drum track: no hit, its ACCENT lane four on the floor (the user, 2026-10-22); the project still counts as empty */
        uint32_t k, ok = 1;
        for (k = 0; k < NSTEP; k++) {
            const dstep_t *s = &TDRUM->dstep[k];
            ok &= (uint32_t)(dstep_mask(s) == 0u && dstep_has(s, DRUM_ACC) == (int)(k % 4u == 0u));
            if (k % 4u == 0u)
                ok &= (uint32_t)(dstep_lvl(s, DRUM_ACC) == LV_NORM && dstep_rat(s, DRUM_ACC) == 0u);
        }
        check(ok, "... the drum track: no hit, the ACCENT lane on every 4th step (NORM, no ratchet), nowhere else");
        check(project_empty(), "... the project still empty (the first take sets the tempo, the click plays): the accents do not count");
    }
#endif
    reset_ui();
}
static void pick_quiet_tests(void)
{   /* the drum track's lane keys while playing: they pick the lane and sound nothing, on every page; stopped or
     * recording they sound (the user, 2026-10-22) */
    uint32_t k = key_of_lane(2);
    reset_ui();
    song.sel = TRK_DRUM;
    ui.scr = SCR_HOME;
    song.playing = 1;
    frame();
    kdown(k);
    check(kb_kind[k] != KS_DRUM, "drum lane key, playing, off STEP: silent");
    kup(k);
    song.rec |= 1u << TRK_DRUM;
    frame();
    kdown(k);
    check(kb_kind[k] == KS_DRUM, "... recording the drum track: it sounds");
    kup(k);
    song.rec &= ~(1u << TRK_DRUM);
    song.playing = 0;
    frame();
    kdown(k);
    check(kb_kind[k] == KS_DRUM, "... stopped: it sounds");
    kup(k);
    track_defaults_steps(TDRUM);                        /* (the hit the recording pass entered: the drum pattern blank again for the tests after) */
    reset_ui();
}
static void wipe_tests(void)
{   /* SHIFT2 + REC held while playing: the notes the sequencer enters are erased, one undo for the hold */
    track_t *t = &trk[0];
    uint32_t k, left;
    reset_ui();
    song.sel = 0;
    for (k = 0; k < 16u; k++)
        t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(48 + k);
    song.playing = 1;
    t->seq_abs = 0;
    t->seq_idx = 4;
    press(B_OCTUP);
    press(B_REC);
    frames(2);
    check(!strncmp(ui.msg, "WIPING", 6) && t->step[4].time == ST_REST, "SHIFT2 + REC held, playing: the step under the playhead is wiped, said");
    check(t->step[3].time == ST_NOTE && t->step[5].time == ST_NOTE, "... the others stay until the playhead enters them");
    t->seq_idx = 5;
    erase_step(t, 5);                                   /* (the sequencer entering the next step: seq.c) */
    check(t->step[5].time == ST_REST, "... a step the sequencer enters while held: gone as it passes");
    release(B_REC);
    release(B_OCTUP);
    frames(2);
    t->seq_idx = 6;
    erase_step(t, 6);
    check(t->step[6].time == ST_NOTE && !strncmp(ui.msg, "WIPE DONE", 9), "released: the wipe ends, the next step is left alone");
    press(B_SAVE);                                      /* the undo chord: SAVE, then HOME */
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    frames(2);
    for (k = 0, left = 0; k < 16u; k++)
        left += t->step[k].time == ST_NOTE;
    check(left == 16u, "SAVE + HOME: one undo brings every wiped step back");
    song.playing = 0;
    t->seq_abs = SEQ_NONE;
    song.playing = 0;
    press(B_OCTUP);
    press(B_REC);
    frames(2);
    check(t->step[0].time == ST_NOTE && !strncmp(ui.msg, "PLAY FIRST", 10), "stopped: nothing is wiped (it would clear the whole pattern), said");
    release(B_REC);
    release(B_OCTUP);
    reset_ui();
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
    go_home();
    frame();
    op_enter(SCR_PROJECT);
    press(B_SAVE);
    frames(HOLD_FRAMES);
    release(B_SAVE);
    check(ui.scr == SCR_SONG, "SAVE held on PROJECT: the SONG screen");
    op_enter(SCR_HOME);
    frame();
    press(B_SEQ);
    frames(HOLD_FRAMES);
    release(B_SEQ);
    check(ui.scr == SCR_SONG, "SEQ held alone off STEP: the SONG screen (2026-10-20)");
    op_enter(SCR_HOME);
    frame();
    tap(B_SEQ);
    check(ui.scr == SCR_STEP, "SEQ tapped off STEP: still STEP");
    op_enter(SCR_SONG);
    frame();
    {   /* the keys on SONG (2026-10-20): white = scenes, black = the parts window, F#5 = PATTERNS / MODE */
        uint32_t cnt = arrangement.count;
        arrangement.count = 25;
        ui.row[SCR_SONG] = 0;
        key(WK(2));
        check(ui.row[SCR_SONG] == 2u && ui.scr == SCR_SONG, "SONG: white key 3: the cursor on scene C (and it is launched: loaded, asked, or said empty)");
        if (op_armed())
            tap(B_HOME);
        press(B_OCTUP);
        key(WK(3));
        release(B_OCTUP);
        if (op_armed())
            tap(B_HOME);
        check(ui.row[SCR_SONG] == 3u && ui.scr == SCR_SONG, "SONG: SHIFT2 + white key 4: the cursor on scene D (launched as soon as possible)");
        ui.row[SCR_SONG] = 2;
        tap(B_SEQ);
        check(ui.scr == SCR_SONG && ui.row[SCR_SONG] >= song_row_of_part(0), "SONG: SEQ tapped on a scene: the next view, the PARTS timeline");
        tap(B_SEQ);
        check(ui.scr == SCR_SONG && ui.row[SCR_SONG] == song_base_mode(), "... again: SETTINGS (MODE)");
        tap(B_SEQ);
        check(ui.scr == SCR_SONG && ui.row[SCR_SONG] == 2u, "... again: SCENES, on the row it was left on (C)");
        ui.row[SCR_SONG] = (uint8_t)song_row_of_part(3);
        tap(B_SEQ);
        tap(B_SEQ);
        tap(B_SEQ);
        check(ui.row[SCR_SONG] == song_row_of_part(3), "... and PARTS again on the part it was left on");
        op_enter(SCR_HOME);
        frame();
        press(B_OCTUP);
        press(B_SEQ);
        release(B_SEQ);
        release(B_OCTUP);
        frames(2);
        check(ui.scr == SCR_SONG, "SHIFT2 held + SEQ: the SONG screen (from the mixer)");
        op_enter(SCR_STEP);
        frame();
        press(B_OCTUP);
        press(B_SEQ);
        release(B_SEQ);
        release(B_OCTUP);
        frames(2);
        check(ui.scr == SCR_SONG, "SHIFT2 held + SEQ on STEP: the SONG screen too (no octave, no STEP tap)");
        arrangement_clock.running = 0;
        arrangement.entry[2].scene = 0;
        fm1_in.notes |= 1u << BK(2);
        frame();
        key(WK(1));
        fm1_in.notes &= ~(1u << BK(2));
        frame();
        if (op_armed())
            tap(B_HOME);
        check(arrangement.entry[2].scene == 1u && arrangement.count == 25u && ui.row[SCR_SONG] == song_row_of_part(2),
              "SONG: a part key held + a scene key: that scene is the part's (part 3 = B)");
        arrangement.count = 3;
        arrangement.entry[2].bars = 8;
        fm1_in.notes |= 1u << BK(5);
        frame();
        key(WK(2));
        fm1_in.notes &= ~(1u << BK(5));
        frame();
        if (op_armed())
            tap(B_HOME);
        check(arrangement.count == 6u && arrangement.entry[5].scene == 2u && arrangement.entry[3].scene == 2u &&
                  arrangement.entry[4].bars == 0u && ui.row[SCR_SONG] == song_row_of_part(5),
              "SONG: a part key past the chain held + a scene key: the part is made (and those between), this scene");
        arrangement.count = 25;
        {   /* a key held with nothing else pressed: a scene key asks to clear it, a part key to delete it and what follows */
            uint32_t r;
            ui.row[SCR_SONG] = 0;
            fm1_in.notes |= 1u << BK(4);
            frames(HOLD_FRAMES);
            check(op_armed() && !strcmp(ui.arm_q, "DELETE PART 5 + AFTER?"), "SONG: a part key held: delete that part and everything after, asked");
            tap(B_SAVE);
            fm1_in.notes &= ~(1u << BK(4));
            frames(2);
            check(arrangement.count == 4u, "... YES: the chain ends before part 5");
            arrangement.count = 25;
            fm1_in.notes |= 1u << BK(0);
            frames(HOLD_FRAMES);
            check(!op_armed() && !strcmp(ui.msg, "ONE PART LEFT"), "SONG: part 1 held: it cannot go (one part left)");
            fm1_in.notes &= ~(1u << BK(0));
            frames(2);
            press(B_OCTDN);
            key(BK(4));
            release(B_OCTDN);
            check(op_armed() && !strcmp(ui.arm_q, "DELETE PART 5 + AFTER?"), "SONG: SHIFT + a part key: delete that part and what follows, asked");
            tap(B_HOME);
            (void)r;
        }
        key(BK(0));
        check(ui.row[SCR_SONG] == song_row_of_part(0), "SONG: black key 1 = part 1");
        key(BK(7));
        check(ui.row[SCR_SONG] == song_row_of_part(7), "SONG: black key 8 = part 8 (the window holds 8)");
        key(BK(8));
        check(ui.row[SCR_SONG] == song_row_of_part(7), "SONG: black key 9 does nothing (it is no part key)");
        arrangement_clock.running = 1, arrangement_clock.index = 0, arrangement_clock.bar = 0;
        arrangement_clock.jump = arrangement_clock.jump_now = 0;
        arrangement.entry[4].scene = 1;                 /* (scene B: stored by the scene tests above) */
        key(BK(4));
        check(arrangement_clock.jump == 5u && arrangement_clock.jump_now == 0u && ui.row[SCR_SONG] == song_row_of_part(4),
              "SONG: black key 5: the song jumps to part 5 at the next bar (the cursor there)");
        arrangement_clock.jump = arrangement_clock.jump_now = 0;
        press(B_OCTUP);
        key(BK(4));
        release(B_OCTUP);
        check(arrangement_clock.jump == 5u && arrangement_clock.jump_now == 1u, "SONG: SHIFT2 + black key 5: the jump at once (as soon as possible)");
        arrangement_clock.jump = arrangement_clock.jump_now = 0;
        notes_tap = 1u << BK(4);                        /* (pressed and let go before the next frame: a slow one) */
        fm1_ms += 400;
        frames(2);
        check(arrangement_clock.jump == 5u, "SONG: black key 5 pressed and let go within one slow frame: still the jump (not lost)");
        arrangement_clock.jump = arrangement_clock.jump_now = 0;
        arrangement_clock.running = 0;
        tap(B_OCTUP);
        frames(2);
        check(ui.row[SCR_SONG] == song_row_of_part(12), "SONG: OCT+: the window by eight, the cursor at the same place (part 5 -> 13)");
        key(BK(2));
        check(ui.row[SCR_SONG] == song_row_of_part(10), "SONG: page 2, black key 3 = part 11");
        tap(B_OCTUP);
        tap(B_OCTUP);
        key(BK(7));
        check(sg_page == 3u && ui.row[SCR_SONG] != song_row_of_part(31 % 25), "SONG: page 4 holds part 25: black key 8 past the chain does nothing");
        tap(B_OCTDN);
        tap(B_OCTDN);
        tap(B_OCTDN);
        key(BK(0));
        check(ui.row[SCR_SONG] == song_row_of_part(0), "SONG: OCT- back to page 1");
        key(25);
        check(ui.row[SCR_SONG] == song_base_mode(), "SONG: F#5 from a part: SETTINGS (the MODE row)");
        key(25);
        check(ui.row[SCR_SONG] < song_nscn(), "SONG: F#5 on SETTINGS: back to the scenes");
        arrangement.count = (uint8_t)cnt;
    }
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
            turn(EN_K1, -1);                            /* (down to "--": none; KNOB 1 = T1) */
        {
            cell_t cc;
            song_cell(c, 0, &cc);
            check(sg.ed_s == 2 && cc.col == C_WARN, "KNOB 1 on its T1 cell: the reference edited (amber until written)");
        }
        frames(60);
        check(sg.ed_s == 0xFF && pat_scene_refs(2, refs) && refs[0] == PAT_NONE, "... written once it rests: T1 none");
        turn(EN_K1, 1);
        turn(EN_ALGO, 1);                               /* (another lane, a cell before the first rested: the first stays) */
        turn(EN_K1, 1);
        turn(EN_ALGO, -1);
        frames(60);
        check(pat_scene_refs(2, refs) && refs[0] == PAT_KEEP && refs[1] < PAT_N && refs[2] == PAT_NONE && refs[3] == PAT_NONE,
              "SONG scene C: KNOB 1 on T1 then on T2 (the lane by ALGORITHM) at once: both references kept (one edit never undoes the other)");
        {   /* playing: a scene's reference and the chain edit too (the arena, the RAM chain), saved when quiet */
            uint32_t was = refs[0], cnt0 = arrangement.count;
            song.playing = 1;
            ui.row[SCR_SONG] = (uint8_t)c;
            turn(EN_K1, 1);
            frames(60);
            check(sg.ed_s == 0xFF && pat_scene_refs(2, refs) && refs[0] != was && sec_pend_has(2),
                  "SONG, playing: KNOB 1 on a scene's T1 reference changes it (the pending arena, no STOP FIRST)");
            ui.row[SCR_SONG] = (uint8_t)song_row_of_part(0);
            frame();
            turn(EN_K2, 1);
            check(arrangement.entry[0].bars > 0u && strcmp(ui.msg, "STOP FIRST") != 0, "SONG, playing: a part's bars turn too");
            song_part_insert(0);
            check(arrangement.count == cnt0 + 1u && strcmp(ui.msg, "STOP FIRST") != 0, "SONG, playing: INSERT a part works");
            song_part_cut(1);
            song_part_assign(cnt0, 1);
            check(arrangement.count == cnt0 + 1u && strcmp(ui.msg, "STOP FIRST") != 0, "SONG, playing: a part is made (part key + scene key)");
            song_part_cut(cnt0);
            check(arrangement.count == cnt0, "SONG, playing: the part cut works");
            song.playing = 0;
        }
        {   /* a part's SCENE card: a scene with T1..DR all "-" is grey and struck, a scene with a pattern is not */
            cell_t pc;
            uint32_t k;
            arrangement.entry[0].scene = 2;
            song_refresh(1);
            song_cell(song_row_of(SG_PART, 0), 0, &pc);
            check(!pc.strike && !pc.dim, "SONG part: its scene has a pattern: the letter plain");
            for (k = 0; k < NTRK; k++)
                pat_scene_ref_set(2, k, PAT_NONE);
            song_refresh(1);
            song_cell(song_row_of(SG_PART, 0), 0, &pc);
            check(pc.strike && pc.dim, "SONG part: its scene has T1..DR all \"-\": the letter grey and struck through");
            pat_scene_ref_set(2, 0, PAT_KEEP);
            song_refresh(1);
        }
        ui.row[SCR_SONG] = (uint8_t)c;
        fm1_in.notes |= 1u << WK(2);
        frames(HOLD_FRAMES);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE C?"), "SONG: a scene key held: clear that scene, asked");
        tap(B_HOME);
        fm1_in.notes &= ~(1u << WK(2));
        frames(2);
        check(project_used(2), "... NO: the scene is kept");
        ui.row[SCR_SONG] = (uint8_t)c;
        fm1_in.notes |= 1u << WK(2);
        frames(HOLD_FRAMES);
        check(op_armed() && ui.arm_ring, "SONG: a scene key held: the ring (the hold confirms)");
        fm1_in.notes &= ~(1u << WK(2));
        frames(2);
        check(!op_armed() && project_used(2), "... let go before the end: cancelled, the scene kept");
        ui.row[SCR_SONG] = (uint8_t)c;
        press(B_SAVE);
        frames(HOLD_FRAMES);
        check(op_armed() && ui.arm_ring && !strcmp(ui.arm_q, "CLEAR SCENE C?") && ui.scr == SCR_SONG, "SONG: SAVE held on a scene row: clear that scene, the ring fills (not PROJECT)");
        frames(40);
        ppm("opt-delete-ring");
        release(B_SAVE);
        frames(2);
        check(!op_armed() && project_used(2) && ui.scr == SCR_SONG, "... let go before the end: cancelled, the scene is kept, no PROJECT");
        press(B_SAVE);
        frames(HOLD_FRAMES + RING_FRAMES);
        check(!op_armed() && !project_used(2), "... held to the end: the scene is cleared");
        release(B_SAVE);
        frames(2);
        scene_store_now(2);                             /* (stored again: the tests after it use the scene) */
        ui.row[SCR_SONG] = 0;
        press(B_HOME);
        key(WK(2));
        release(B_HOME);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE C?") && ui.row[SCR_SONG] == (uint8_t)c, "SONG: HOME + a scene key: clear that scene, asked");
        tap(B_HOME);
        press(B_SAVE);
        key(WK(2));
        release(B_SAVE);
        check(op_armed() && !strcmp(ui.arm_q, "STORE SCENE C?"), "SONG: SAVE + a scene key: store the loop into it, asked over a used one");
        tap(B_HOME);
        check(project_used(2) && !op_armed(), "... NO: kept, nothing asked");
        press(B_OCTDN);
        key(WK(2));
        release(B_OCTDN);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE C?"), "SONG: SHIFT + a scene key: clear that scene, asked");
        tap(B_HOME);
        ui.force = 1;
        frame();
        ppm("opt-song-scenes");
        tap(B_SAVE);
        check(live_sec == 2, "YES on scene C, stopped: loaded");
        /* the PATTERNS row: the session grid, the keys launch, SAVE + key stores, SHIFT + key clears */
        ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_PAT, 0);
        frames(2);
        check(ly_lock == LY_PAT && song_on_pat_row(), "PATTERNS row: the keys launch the selected track's patterns");
        song.sel = 0;
        turn(EN_ALGO, 2);
        check(song.sel == 0u && ui.scr == SCR_SONG && !ui.master, "PATTERNS row: ALGORITHM does nothing (a global screen)");
        turn(EN_K3, 1);
        check(song.sel == 2u, "PATTERNS row: KNOB 3 selects T3 (the keys then launch its patterns)");
        turn(EN_K1, 1);
        check(song.sel == 0u, "... KNOB 1 selects T1");
        press(B_SEQ);
        press(B_SAVE);
        key(WK(3));
        release(B_SAVE);
        release(B_SEQ);
        frames(2);
        check((project_used(3) || op_armed()) && ui.scr == SCR_SONG, "PATTERNS row: SEQ + SAVE + key 4: the four patterns stored as scene D (asked when used)");
        if (op_armed())
            tap(B_HOME);
        press(B_SEQ);
        key(WK(3));
        release(B_SEQ);
        frames(2);
        check(op_armed() || project_used(3), "PATTERNS row: SEQ + key 4 alone: stored as scene D (asked over a used one)");
        if (op_armed())
            tap(B_HOME);
        press(B_SEQ);
        press(B_HOME);
        key(WK(3));
        release(B_HOME);
        release(B_SEQ);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE D?") && ui.scr == SCR_SONG, "PATTERNS row: SEQ + HOME + key 4: clear scene D, asked");
        tap(B_HOME);
        frames(2);
        t->step[3].time = ST_NOTE, t->step[3].n = 1, t->step[3].note[0] = 67;   /* (C's T1 is none: a note) */
        press(B_SAVE);
        key(WK(5));
        release(B_SAVE);
        check(pat_has(0, 5), "SAVE held + key 6: the working copy into slot 6");
        ui.force = 1;
        frames(2);
        check(key_led(white_key(5)) >= 1u && key_led(white_key(7)) == 0u, "PATTERNS row: a stored pattern's key is lit, an empty slot's dark");
        press(B_SEQ);
        frames(HOLD_FRAMES);                            /* (held past HOLD: no view cycle on its release) */
        check(key_led(white_key(2)) >= 1u || !project_used(2), "SEQ held (scenes): a filled scene's key is lit");
        check(key_led(white_key(14)) == 0u, "... an empty scene's key is dark");
        release(B_SEQ);
        frames(2);
        key(WK(5));
        check(pat_cur[0] == 5 && !strcmp(ui.msg, "LOADED T1 6"), "key 6: T1's pattern 6 (stopped: loaded)");
        frames(2);
        check(key_led(white_key(5)) == 2u, "... the playing pattern's key is full");
        ui.force = 1;
        frame();
        ppm("opt-song-patterns");
        fm1_in.notes |= 1u << WK(5);
        frames(HOLD_FRAMES);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 6?") && ui.scr == SCR_SONG, "PATTERNS row: a pattern key held: clear that slot, asked");
        tap(B_HOME);
        fm1_in.notes &= ~(1u << WK(5));
        frames(2);
        press(B_HOME);
        key(WK(5));
        release(B_HOME);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR T1 6?") && ui.scr == SCR_SONG, "HOME held + key 6: clear slot 6, asked");
        tap(B_SAVE);
        check(!pat_has(0, 5), "... YES: cleared");
        {   /* clearing while playing: it reads as none at once, the tombstone is written when quiet */
            uint8_t sv = t->step[0].time;
            t->step[0].time = ST_NOTE, t->step[0].n = 1, t->step[0].note[0] = 60;
            check(!pat_store_slot(0, 7) && pat_has(0, 7), "(a pattern stored in slot 8, stopped: in the log)");
            song.playing = 1;
            check(!pat_write(0, 7, 0) && !pat_has(0, 7) && slg_tombs_any(), "PATTERNS, playing: a clear works (no STOP FIRST), pending in RAM");
            song.playing = 0;
            sections_write();
            check(!slg_tombs_any() && !pat_has(0, 7), "... quiet: the tombstone is written, the slot stays empty");
            t->step[0].time = sv;
            scene_store_now(9);
            check(project_used(9), "(scene J stored, stopped)");
            song.playing = 1;
            check(!sec_scene_clear(9) && !project_used(9) && slg_tombs_any(), "SONG, playing: a scene clear works (no STOP FIRST), pending in RAM");
            song.playing = 0;
            sections_write();
            check(!slg_tombs_any() && !project_used(9), "... quiet: its tombstones are written, the scene stays empty");
        }
        tap(B_REC);
        check(strncmp(ui.msg, "DUPLICATE T1", 12), "PATTERNS row, REC: no duplicate any more (2026-10-16: the LFO layer's keys)");
        key(key_of_lane(8));                            /* (black 9: DUPLICATE, as in the LFO layer) */
        check(!strncmp(ui.msg, "DUPLICATE T1", 12), "PATTERNS row, black 9: the working copy into the first free slot");
        ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, 2);
        frame();
        {
        int8_t ls0 = live_sec;
        song.playing = 1;                               /* a scene key playing: the next bar; with SHIFT2: as soon as possible */
        live_req = -1;
        live_sec = -1;
        key(WK(2));
        frames(2);
        check(live_req == 2 && live_now == 0 && live_sec != 2, "SONG, playing: a scene key: queued for the next bar");
        live_req = -1;
        live_now = 0;
        live_sec = 1;                                   /* the scene playing is B with BARS 4: a launch waits for the end of a pass of 4 bars */
        scene_bars[1] = 4;
        key(WK(2));
        frames(2);
        check(live_len == 4u && live_req == 2, "SONG, playing scene B (BARS 4): a scene key queued, waiting for 4 bars");
        scene_bars[1] = 0;
        live_req = -1;
        live_now = 0;
        press(B_OCTUP);
        key(WK(2));
        release(B_OCTUP);
        frames(2);
        check(live_sec == 2 && live_req == -1 && live_now == 0 && !strcmp(ui.msg, "NOW: C"),
              "SONG, playing: SHIFT2 + a scene key: applied as soon as possible (\"NOW: C\"), not waiting for the bar");
        live_req = -1;
        live_now = 0;
        song.playing = 0;
        live_sec = ls0;
        }
        press(B_OCTDN);
        press(B_REC);
        release(B_REC);
        release(B_OCTDN);
        check(op_armed() && !strcmp(ui.arm_q, "CLEAR SCENE C?"), "SHIFT + REC on scene C: clear it, asked");
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
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, 2);
    frame();
    {   /* the SCENES table (op_songtbl.c): KNOB 1..4 the values of T1..DR, PRESETS the BARS, SELECT the cursor, ALGORITHM the lane */
        scene_bars[2] = 0;
        ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, 2);
        frame();
        check(song_tbl_on(), "SONG, a scene row: the SCENES table");
        turn(EN_ALGO, 9);
        check(stb_lane == 4u, "SCENES: ALGORITHM down the lanes to BARS (the last)");
        turn(EN_PRESET, 3);
        check(scene_bars[2] == 3u && !strcmp(ui.msg, "SCENE C: 3 BARS") && sg.dirty, "... PRESETS: the scene's BARS 3, said, the song to be saved");
        turn(EN_PRESET, -9);
        check(scene_bars[2] == 0u && !strcmp(ui.msg, "SCENE C: AUTO"), "... down: AUTO again, no lower");
        turn(EN_PRESET, 99);
        check(scene_bars[2] == 64u, "... up: 64 at most");
        {
            cell_t sc5;
            song_cell(song_row_of(SG_SCENE, 2), 4, &sc5);
            check(!strcmp(sc5.label, "BARS") && !strcmp(sc5.val, "64") && sc5.col == 0xB31Fu && sc5.kind == CK_RO, "SCENE C: its BARS cell, purple, the value 64");
            scene_bars[2] = 0;
            song_cell(song_row_of(SG_SCENE, 2), 4, &sc5);
            check(!strcmp(sc5.val, "AUTO"), "... AUTO when 0");
            song_cell(song_row_of(SG_PART, 0), 4, &sc5);
            check(sc5.label == 0, "... no 5th cell on the other rows");
        }
        turn(EN_SELECT, 1);
        check(ui.row[SCR_SONG] == song_row_of(SG_SCENE, 3) && stb_lane == 4u, "SCENES: SELECT the cursor to the next scene, the lane kept");
        turn(EN_SELECT, -1);
        turn(EN_ALGO, -9);
        check(stb_lane == 0u, "... ALGORITHM up the lanes");
        turn(EN_K2, 1);
        check(stb_lane == 0u, "... KNOB 2 edits the next scene's value in that lane (the lane stays)");
        {   /* KNOB 2: the scene after the cursor's, the same lane: its BARS lane too */
            uint32_t b3 = scene_bars[3];
            turn(EN_ALGO, 9);
            turn(EN_K2, 2);
            check(scene_bars[3] == b3 + 2u && scene_bars[2] == 0u, "... on the BARS lane KNOB 2 sets the next scene's BARS (the current stays)");
            scene_bars[3] = b3;
            turn(EN_ALGO, -9);
        }
        ui.force = 1;
        frames(2);
        ppm("opt-song-scenes-table");
        scene_bars[2] = 0;
    }
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_PART, 0);
    frame();
    {   /* the PARTS timeline: two lanes (the scene, the bars: S the scene's, A the longest pattern, a number) */
        uint32_t bars = arrangement.entry[0].bars, cnt = arrangement.count;
        cell_t bc;
        stb_lane = 0;
        arrangement.entry[0].bars = 0;
        song_cell(song_row_of(SG_PART, 0), 1, &bc);
        check(song_tbl_on() && !strcmp(bc.val, "SCENE"), "PARTS table: BARS 0 is S (it follows its scene's BARS)");
        turn(EN_ALGO, 1);
        check(stb_lane == 0u, "PARTS: ALGORITHM does nothing (the scene lane only)");
        turn(EN_PRESET, 2);
        check(arrangement.entry[0].bars == 2u, "PARTS: PRESETS the bars (from S up: 1, 2)");
        turn(EN_PRESET, -5);
        check(arrangement.entry[0].bars == 0u, "... down: S again, no lower");
        turn(EN_PRESET, 99);
        check(arrangement.entry[0].bars == ARR_BARS_A, "... up through 64 to A (the longest pattern)");
        song_cell(song_row_of(SG_PART, 0), 1, &bc);
        check(!strcmp(bc.val, "AUTO"), "... A reads AUTO in the cell");
        turn(EN_PRESET, -1);
        check(arrangement.entry[0].bars == 64u, "... A down: 64");
        arrangement.entry[0].bars = 0;
        turn(EN_SELECT, 1);
        check(ui.row[SCR_SONG] == song_row_of(SG_PART, 1) || arrangement.count < 2u, "PARTS: SELECT the cursor to the next part");
        turn(EN_SELECT, -9);
        check(ui.row[SCR_SONG] == song_row_of(SG_PART, 0), "... and back to the first");
        if (arrangement.count >= 2u) {
            uint8_t s0 = arrangement.entry[0].scene, s1 = arrangement.entry[1].scene;
            turn(EN_K1, 1);
            check(arrangement.entry[0].scene != s0 && arrangement.entry[1].scene == s1, "PARTS: KNOB 1 the scene of the part under the cursor");
            turn(EN_K2, 1);
            check(arrangement.entry[1].scene != s1 && ui.row[SCR_SONG] == song_row_of(SG_PART, 0), "... KNOB 2 the next part's scene (the cursor stays)");
            arrangement.entry[0].scene = s0, arrangement.entry[1].scene = s1;
        }
        {
            uint32_t n0 = arrangement.count;
            tap(B_SAVE);
            check(op_armed() && arrangement.count == n0 && !strcmp(ui.arm_q, "INSERT PART 1?"), "PARTS: SAVE asks \"INSERT PART 1?\", nothing inserted");
            tap(B_HOME);
            check(!op_armed() && arrangement.count == n0, "INSERT: HOME cancels, nothing changed");
            tap(B_SAVE);
            tap(B_SAVE);
            check(arrangement.count == n0 + 1u && ui.row[SCR_SONG] == song_row_of(SG_PART, 1), "INSERT: a copy after it");
            arrangement.count = (uint8_t)n0;
            ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_PART, 0);
        }
        ui.force = 1;
        frames(2);
        ppm("opt-song-parts-table");
        arrangement.entry[0].bars = (uint8_t)bars;
        (void)cnt;
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

#ifdef MIRROR_CHECK
/* the mirror catching up (ed_mirror.c, a link that fell behind): black to the client only, every band drawn again in place
 * (op_repaint: the panel is not cleared, no flash), two frames; the client's screen is the panel's again. Never under PRESETS'
 * popup list (overlay 5: the panel under it is not drawn again, ed_mirror.c waits for it to close). Its cost in bytes */
static uint32_t mc_resyncs, mc_resync_bad, mc_resync_max, mc_resync_sum;
static void mc_resync(void)
{
    uint32_t i, b0 = mc_bytes, n, diff = 0;
    mir_enc_fill(0, 0, 240, 240, C_BLACK);
    op_repaint();
    frames(7);                                          /* (SCOPE draws its trace every other frame) */
    mc_meter_now = 1;                                   /* (the meters lag the panel by up to 100 ms: compared in a window, nothing */
    frame();                                            /* left stale) */
    mc_meter_now = 0;
    n = mc_bytes - b0;
    for (i = 0; i < 240u * 240u; i++)
        diff += md_fb[i] != swap16(screen[i]);
    mc_resyncs++, mc_resync_sum += n, mc_resync_bad += diff != 0u;
    if (diff && getenv("MIRROR_WHERE")) {
        uint32_t x0 = 240, y0 = 240, x1 = 0, y1 = 0;
        for (i = 0; i < 240u * 240u; i++)
            if (md_fb[i] != swap16(screen[i])) {
                uint32_t x = i % 240u, y = i / 240u;
                x0 = x < x0 ? x : x0, y0 = y < y0 ? y : y0, x1 = x > x1 ? x : x1, y1 = y > y1 ? y : y1;
            }
        printf("  catch-up off: %u px in x %u..%u y %u..%u; scr %u layer %u overlay %u panel had %04X, client %04X\n", diff, x0, x1, y0, y1,
               ui.scr, lay.shown, ui.overlay, swap16(screen[y0 * 240u + x0]), md_fb[y0 * 240u + x0]);
    }
    mc_resync_max = n > mc_resync_max ? n : mc_resync_max;
    for (i = 0; diff && i < 240u * 240u; i++)
        md_fb[i] = swap16(screen[i]);              /* (in step again for the next one) */
}
#endif
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
#ifdef MIRROR_CHECK
        if (f % 50u == 49u && ui.overlay != 5u)
            mc_resync();
#endif
    }
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    frames(4);
#undef R
}

/* the synth tracks' default sounds (power-on), and the drum lanes as the kit has them: what a random run changed goes, so
 * the tests after one start where the suite began (2026-10-12: the walk reaching the lanes changed fuzz's path) */
static void tracks_default(void)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], trk_def_engine(i));
        apply_preset_to(&trk[i], trk_def_preset(i));
        trk[i].engine = trk[i].eng_req;
    }
    memset(&dl, 0, sizeof dl);
    lane_select(0);
    ui.lane = 0;
}

/* ---- the loose ends (docs/UI-OPTIMIST-DESIGN.md 4.5, 4.11, 4.12): the SONG shortcut, undo of the step extras, NAME, FM6's editor, the graphs on
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
    release(B_SAVE);
    frames(2);
    check(ui.scr != SCR_SONG, "SAVE held + SELECT: no SONG shortcut any more (2026-10-16)");
    op_enter(SCR_PROJECT);
    press(B_SAVE);
    frames(HOLD_FRAMES);
    armed = op_armed();
    release(B_SAVE);
    frames(2);
    check(ui.scr == SCR_SONG && !armed && !op_armed(), "SAVE held on PROJECT: the SONG screen (no operation run, nothing asked)");
    ui.row[SCR_SONG] = (uint8_t)song_row_of_part(0);        /* (a scene row: SAVE held clears the scene instead) */
    press(B_SAVE);
    frames(HOLD_FRAMES);
    release(B_SAVE);
    frames(2);
    check(ui.scr == SCR_SONG && !op_armed(), "... SAVE held on SONG: nothing (a store gesture held long must not leave the screen; the user, 2026-10-23)");
    op_enter(SCR_PROJECT);
    frame();
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT && op_armed(), "... SAVE tapped on PROJECT: its operation, asked (as before)");
    tap(B_HOME);
    reset_ui();
}
static void undo_extras_tests(void)
{
#if FELUCCA_MICRO || FELUCCA_FILLS || FELUCCA_PLOCK
    track_t *t = &trk[0];
    reset_ui();
    song.sel = 0;
    track_defaults_steps(t);
    AL(t)->n = 0;
    auto_touch(0);
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
        auto_list_t after = *AL(t);
        int had = AL(t)->n != 0;
        press(B_SAVE);
        press(B_HOME);
        release(B_HOME);
        release(B_SAVE);
        check(had && !AL(t)->n && step_on(&t->step[5]),
              "undo: the step's nudge, fill and lock go back, the step stays (its own level)");
        press(B_HOME);
        press(B_SAVE);
        release(B_SAVE);
        release(B_HOME);
        check(!memcmp(AL(t), &after, sizeof after), "redo (HOME then SAVE): the nudge, fill and lock again, byte for byte");
    }
    press(B_OCTDN);                                      /* SHIFT + REC held, the ring closed: the extras cleared; undone too */
    press(B_REC);
    frames(RH_CLEAR_MS / 16u + 3u);
    release(B_REC);
    release(B_OCTDN);
    frames(2);
    check(!AL(t)->n && !song.rec, "SHIFT + REC, the ring closed: the extras cleared");
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(AL(t)->n != 0, "... undone: they are back");
    track_defaults_steps(t);
    AL(t)->n = 0;
    auto_touch(0);
    reset_ui();
#endif
}
/* the automation store on STEP (phase 3): YES toggles HOLD of the hot cell's event, the card's mark, the marks under the
 * steps (a dot, a hold's tail), SHIFT + a step clears its motion too, chance on a drum step (an event) */
static void auto_step_tests(void)
{
#if FELUCCA_AUTO
    track_t *t = &trk[0];
    int32_t v;
    reset_ui();
    song.sel = 0;
    track_defaults_steps(t);
    AL(t)->n = 0;
    auto_w.on = 0;
    auto_touch(0);
    op_enter(SCR_STEP);
    frames(2);
    key(WK(3));
    key(WK(9));
#if FELUCCA_PLOCK && FELUCCA_MOTION
    {
        uint32_t id;
        cell_t c;
        kdown(WK(3));
        press(B_ENV);
        id = PAGES[st.lock_pg].id[0];
        turn(EN_K1, 4);                                 /* a lock on step 4 */
        ui.hot = 0;
        tap(B_SAVE);                                    /* YES: HOLD */
        step_cell(0, 0, &c);
        check(hold_get(t, 3, id, &v) && !lock_get(t, 3, id, &v) && c.mark == 2u && (auto_w.on & 1u) && str_eq(ui.msg, "HOLD: UNTIL THE NEXT"),
              "a step held + ENV, YES: the hot cell's lock becomes a hold event, its card's mark an arrow, its motion plays");
        ui.force = 1;
        frame();
        ppm("opt-step-hold");
        tap(B_SAVE);
        step_cell(0, 0, &c);
        check(lock_get(t, 3, id, &v) && !hold_get(t, 3, id, &v) && c.mark == 1u, "... YES again: this step only (a padlock)");
        tap(B_SAVE);                                    /* (a hold again, for the marks) */
        release(B_ENV);
        kup(WK(3));
        ui.force = 1;
        frame();
        {
            uint32_t y = (uint32_t)SP_Y + (uint32_t)SG_MK_Y + 1u, x3 = RL_X + 3u * RL_CW + (RL_CW - 2u) / 2u,
                     xt = RL_X + 6u * RL_CW + 2u, x9 = RL_X + 9u * RL_CW + (RL_CW - 2u) / 2u;   /* (a synth track: the roll's columns) */
            check(screen[y * 240u + x3] == swap16(trk_col(0)) && screen[y * 240u + xt] == swap16(col_shade(trk_col(0), 5u)) &&
                  screen[y * 240u + x9] != swap16(trk_col(0)),
                  "the marks under the steps: a dot under step 4, its hold's tail on to LEN; step 10 (no event) none");
            ppm("opt-step-marks");
        }
        key(WK(3));                                     /* (tapped: the note goes, its step-only events with it) */
        check(hold_get(t, 3, id, &v), "a set step tapped: cleared, its hold event stays (the motion is the pattern's)");
        (void)lock_set(t, 3, P_PAN, 5);
        fm1_in.buttons |= BT(B_OCTDN);
        kdown(WK(3));
        kup(WK(3));
        fm1_in.buttons &= ~BT(B_OCTDN);
        frame();
        check(hold_get(t, 3, id, &v) && !lock_get(t, 3, P_PAN, &v), "SHIFT (OCT- held) + the step: its p-locks cleared, its motion stays");
        AL(t)->n = 0;                                   /* (the motion was this test's; the next tests start clean) */
    }
#endif
#if FELUCCA_CHANCE
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    AL(t)->n = 0;
    auto_touch(TRK_DRUM);
    op_enter(SCR_STEP);
    frames(2);
    key(WK(2));
    kdown(WK(2));
    turn(EN_PRESET, -6);
    {
        char h[40], k[40];
        step_foot(h, k, sizeof h);
        check(step_chance_ev(t, 2) == 100u && strstr(h, "Chance 100%") != 0, "drums: a step held + PRESETS: no chance edit any more, the chance stays 100 %");
    }
    kup(WK(2));
    track_defaults_steps(t);
    AL(t)->n = 0;
    auto_touch(TRK_DRUM);
#endif
    reset_ui();
#endif
}
static void name_tests(void)
{
    uint32_t n0 = up_stores, l;
    char t[32];
    reset_ui();
    song.sel = 0;
    op_enter(SCR_PROJECT);                              /* SAVE AS lives on PROJECT > USER (the SOUND row is gone, 2026-10-14) */
    for (l = 0; l < NPRJ && prj_kind(l) != PR_USER; l++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)l;
    ui.user_slot = 2;
    turn(EN_K2, 1);                                     /* OPERATION: SAVE */
    tap(B_SAVE);
    check(op_armed() && !name_on(), "SAVE AS: asks first, NAME not open");
    tap(B_HOME);
    check(!op_armed() && !name_on() && up_stores == n0, "SAVE AS: HOME cancels, nothing changed");
    tap(B_SAVE);
    tap(B_SAVE);
    l = str_len(ENGINES[TSEL->eng_req]->name);
    check(name_on() && nm.kind == NK_USER && nm.slot == 2u && !strncmp(nm.s, ENGINES[TSEL->eng_req]->name, l) &&
              !strcmp(nm.s + l, " 03"), "SAVE AS: NAME, prefilled with the automatic name (\"ANALOG 03\")");
    name_title(t, sizeof t);
    check(!strcmp(t, "NAME U03") && ly_lock == LY_STEP, "... the header names the slot; the keys reach the UI only");
    fm1_in.buttons |= BT(B_OCTDN);
    turn(EN_K1, 1);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    check(name_on() && nm.len == 0u, "SHIFT + a knob: the whole name cleared (NAME stays)");
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
    check(!name_on() && up_stores == n0 + 1u && !strcmp(up_stored, nm.s) && ui.toast_t && !song.playing && transport_req == 1,
          "playing: SAVE stops the transport, writes under the typed name, PLAY again");
    transport_req = 0;
    ui.user_slot = 3;
    turn(EN_K2, 1);                                     /* (SAVE AS again: asks, then NAME) */
    tap(B_SAVE);
    tap(B_SAVE);
    check(name_on(), "SAVE AS again: NAME");
    turn(EN_K1, -20);
    tap(B_HOME);
    check(!name_on() && up_stores == n0 + 1u && ui.scr == SCR_PROJECT, "cursor at the start, HOME: cancelled, nothing written");
    op_enter(SCR_PROJECT);
    for (l = 0; l < NPRJ && prj_kind(l) != PR_USER; l++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)l;
    ui.user_slot = 5;
    turn(EN_K2, 1);                                     /* (OPERATION: SAVE) */
    tap(B_SAVE);
    check(op_armed() && !name_on() && !strcmp(ui.arm_q, "SAVE USER U06?"), "PROJECT's USER SAVE: asks first");
    tap(B_SAVE);
    check(name_on() && nm.kind == NK_USER && nm.slot == 5u, "PROJECT's USER SAVE: then NAME");
    tap(B_SAVE);
    check(!name_on() && up_store_slot == 5u, "... SAVE: the user preset written");
#if SEC_LOGGED
    {
        char b[16];
        ui.row[SCR_PROJECT] = 0;                        /* PROJECT: slot 9, empty */
        song.g[G_SLOT] = 9;
        frame();
        turn(EN_K2, 1);
        tap(B_SAVE);
        tap(B_SAVE);
        check(name_on() && nm.kind == NK_PROJ && nm.slot == 8u && !strcmp(nm.s, "PROJECT 9"),
              "PROJECT's SAVE: NAME, prefilled \"PROJECT 9\"");
        fm1_in.buttons |= BT(B_OCTDN);
        turn(EN_K1, 1);
        fm1_in.buttons &= ~BT(B_OCTDN);
        frame();
        name_type(1, 1);                                /* C */
        name_type(1, 2);                                /* D */
        tap(B_SAVE);
        sec_name(8, b);
        check(!name_on() && prj_slot_used(PR_PROJECT, 8) && !strcmp(b, "CD"), "... SAVE: the project saved, its name in the log");
        turn(EN_K2, 1);
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
        check(!strcmp(b, "OP3 TUNE"), "the next row: the operator's TUNE page");
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
    check(ui.row[SCR_SOUND] == 0u, "ENV again on the last row: the first again (a button again wraps)");
    {   /* M-VAVE's FM editor on the device: the operator picked and switched off on the page itself (SHIFT2 / SHIFT + a black key), the EG drawn */
        uint32_t k, ok = 1, hits = drums.hits;
        int16_t on4 = f6_ed()[FV_ON + 4];
        ui.row[SCR_SOUND] = F6_OP0 + 1u;
        ui.force = 1;
        frame();
        press(B_OCTUP);                                 /* SHIFT2 held: the keys name what they pick */
        frames(HOLD_FRAMES);
        for (k = 0; k < 27u; k++) {
            uint8_t q = led_pos[14u + k];
            uint32_t lv = q == 0xFF ? 9u : (fm1_led[q >> 3] >> (q & 7u)) & 1u ? 2u : (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u ? 1u : 0u;
            uint32_t want = (fm6_keys_at(F6_OP0 + 1u) >> k) & 1u ? 2u : (FM6_KEYS_FN >> k) & 1u ? 1u : 0u;
            if (q != 0xFF && lv != want)
                ok = 0;
        }
        check(ok, "SHIFT2 held on SOUND's FM6 rows: the 10 black keys lit (the operator edited full), the others dark");
        kdown(10);                                      /* OP5 */
        kup(10);
        release(B_OCTUP);
        check(f6.target == 4u && snd_fam == SND_FM6 && lay.shown == LY_PLAY && f6_kind(ui.row[SCR_SOUND]) == 0u && ui.row[SCR_SOUND] == f6.row,
              "SHIFT2 + a black key on the FM6 rows: OP5 picked on the page, no layer");
        check(drums.hits == hits, "... silent");
        press(B_OCTDN);                                 /* SHIFT held: the same key switches the operator */
        kdown(10);
        kup(10);
        release(B_OCTDN);
        check(f6_ed()[FV_ON + 4] == !on4, "SHIFT + OP5's key: OP5 switched off / on");
        press(B_OCTDN);
        kdown(10);
        kup(10);
        release(B_OCTDN);
        check(f6_ed()[FV_ON + 4] == on4, "... and back");
        press(B_OCTUP);
        kdown(15);                                      /* PIT */
        kup(15);
        release(B_OCTUP);
        check(f6_kind(ui.row[SCR_SOUND]) == 1u && snd_fam == SND_FM6, "SHIFT2 + the PIT key: the pitch envelope's pages");
        ui.row[SCR_SOUND] = F6_OP0 + 1u;                /* OPn RATES: the envelope's shape, the algorithm gone */
        ui.force = 1;
        frame();
        ppm("opt-fm6-eg");
        check(f6_eg_row(f6_cur_row()) && px_in(4, 120, 232, 22, C_WHITE),
              "OPn RATES: the envelope drawn (the chips, a dot a stage), the operator white");
        ui.row[SCR_SOUND] = F6_PIT0;
        ui.force = 1;
        frame();
        ppm("opt-fm6-pit");
        check(f6_eg_row(f6_cur_row()), "PIT RATES: the pitch envelope drawn");
        ui.row[SCR_SOUND] = F6_OP0 + 4u;                /* SCALE: the algorithm again */
        ui.force = 1;
        frame();
        check(!f6_eg_row(f6_cur_row()) && px_in(4, 120, 232, 112, C_WHITE), "SCALE: the algorithm again");
        ui.row[SCR_SOUND] = 0u;
        ui.force = 1;
        frame();
    }
    press(B_ENV);                                       /* ENV held: the layer */
    frames(HOLD_FRAMES);
    check(lay.shown == LY_OPS && ly_ops_on, "ENV held on FM6: the operator layer");
    {   /* every key with a function lit (the selected full, the others dim), the others dark (the user, 2026-10-12) */
        uint32_t k, ok = 1, lit = fm6_keys_lit();
        frame();
        for (k = 0; k < 27u; k++) {
            uint8_t q = led_pos[14u + k];
            uint32_t lv = q == 0xFF ? 9u : (fm1_led[q >> 3] >> (q & 7u)) & 1u ? 2u : (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u ? 1u : 0u;
            uint32_t want = (lit >> k) & 1u ? 2u : (FM6_KEYS_FN >> k) & 1u ? 1u : 0u;
            if (q != 0xFF && lv != want)
                ok = 0;
        }
        check(ok, "ENV held on FM6: OP1..OP6 PIT GLO MONO POLY lit (the one edited and the mode full, the rest dim), the other keys dark");
    }
    key(3);                                             /* G#: OP2 */
    check(f6.target == 1u && f6_kind(f6.row) == 0u, "a black key (G#): OP2");
    key(15);                                            /* PIT */
    check(f6_kind(f6.row) == 1u, "the PIT key: the pitch envelope's pages");
    tap(B_OCTUP);
    check(f6.row == F6_PIT0 + 1u, "OCT+: the next page");
    tap(B_OCTUP);
    check(f6.row == F6_PIT0, "OCT+ on the kind's last page: its first again (a button again wraps)");
    tap(B_OCTUP);
    v = f6_ed()[FV_PL + 1];
    turn(EN_K2, -2);
    check(f6_ed()[FV_PL + 1] != v, "KNOB 2 in the layer: the page's value");
    turn(EN_SELECT, -1);
    check(f6.row == F6_PIT0, "SELECT in the layer: its pages too (back to PIT 1)");
    turn(EN_SELECT, -3);
    check(f6.row == F6_PIT0, "... stopping at the first (SELECT does not wrap)");
    ui.force = 1;
    frame();
    ppm("opt-fm6-layer-pit");                           /* ENV held on a PIT page: the pitch envelope, over the keys' map */
    check(f6_eg_row(f6.row) && f6_cur_row() == f6.row, "ENV held on a PIT page: the layer's page is the picture's (the envelope), not SOUND's row");
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
    check(px_in(4, (uint32_t)OP_PY, 10, (uint32_t)OP_PH, ENG_COL[e]), "... its engine's colour chip drawn");
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
    {   /* the list by instrument; SHIFT2 held + PRESETS: the next / previous instrument (the user, 2026-10-12; SHIFT2 since 2026-10-16) */
        uint32_t i, e0, e1, sorted = 1, ok = 1, k, ne = 0, back;
        for (i = 1; i < NBANK; i++)
            if (preset_at(i, &k) < preset_at(i - 1u, &k))
                sorted = 0;
        check(sorted, "the preset list: grouped by instrument (engine order)");
        song.sel = 0;
        frame();
        e0 = TSEL->eng_req;
        for (i = 0; i < NBANK; i++)                     /* (the instruments in the list: eng_built takes a UID, not the slot i) */
            if (!i || preset_at(i, &k) != preset_at(i - 1u, &k))
                ne++;
        press(B_OCTUP);
        turn(EN_PRESET, 1);
        e1 = TSEL->eng_req;
        cur = preset_pos(&total);
        check(e1 != e0 && (cur == 0u || preset_at(cur - 1u, &k) != e1),
              "SHIFT2 held + PRESETS: the next instrument, its first sound");
        turn(EN_PRESET, -1);
        back = TSEL->eng_req;
        release(B_OCTUP);
        check(back == e0 && ui.scr == SCR_HOME && !op_armed(), "... and back; OCT+ let go: nothing else (no octave, no SYSTEM)");
        press(B_OCTUP);
        for (i = 0; i < ne + 1u; i++)
            turn(EN_PRESET, -1);                        /* (down to the first instrument: the list stops there, no wrap) */
        for (i = 0, ok = 0; i < ne + 2u; i++) {
            uint32_t was = TSEL->eng_req;
            turn(EN_PRESET, 1);
            ok += TSEL->eng_req != was;
        }
        release(B_OCTUP);
        check(ne <= 1u || ok == ne - 1u, "... every turn a new instrument up to the last, which stays (no wrap round)");
        set_engine_of(TSEL, e0);                        /* (the track as it was) */
        snd_mark(song.sel);
        reset_ui();
    }
    {   /* the master FX slots name their role by type, not place (the user, 2026-10-12: "slot 1 is insert, right ?") */
        cell_t c;
        int16_t *vp;
        uint8_t keep[FX_NSLOT];
        uint32_t k, ok = 1;
        static const uint8_t TY[FX_NSLOT] = {FXT_REV, FXT_NONE, FXT_DIST, FXT_CHO};
        static const char *const ROLE[FX_NSLOT] = {"SEND", "", "INS", "SEND"};
        memcpy(keep, fxs_slot, sizeof keep);
        memcpy(fxs_slot, TY, sizeof TY);
        for (k = 0; k < FX_NSLOT; k++) {
            const param_desc_t *d = fxs_desc(k, &vp);
            cell_param(&c, d, vp);
            if (d != &FXS_DESC[k] || strcmp(c.unit, ROLE[k]))
                ok = 0;
        }
        memcpy(fxs_slot, keep, sizeof keep);
        check(ok, "the master FX slots: each card says INS or SEND by its type (REV SEND, empty none, DST INS, CHO SEND)");
    }
}

#include "ui_optimist_len.h"                      /* LEN in powers of two, SHIFT = LFO held */
#include "ui_optimist_cards.h"                     /* the cards 2x2 but on the mixer, no CARDS setting */
#include "ui_optimist_cardspic.h"                  /* under the cards: the picture alone, else the rows */
#include "ui_optimist_stepcards.h"                /* STEP: the cards as an overlay */
#include "ui_optimist_taplate.h"                  /* STEP: a tap judged by the key's own times */
#include "ui_optimist_slowframe.h"                /* a slow frame: the buttons' and keys' own times, the presses in order */
#include "ui_optimist_picker.h"                   /* STEP: the note picker (the cards down) */
#include "ui_optimist_master.h"                    /* PRESETS = the sound only; ALGORITHM to MASTER */
#include "ui_optimist_knobcol.h"                   /* SYSTEM > SCREEN > KNOB COLORS */
#include "ui_optimist_clkout.h"                    /* SYSTEM > CLOCK OUT */
#include "ui_optimist_uasel.h"                     /* SYSTEM > USB AUDIO */
#include "ui_optimist_core.h"                       /* SYSTEM > CPU > 2ND CORE (DUAL builds), the CPU1 cell's reason */
#include "ui_optimist_sysdbg.h"                     /* builder item SYS_DEBUG off: INFO is CPU LOAD, UNDERRUNS, BATTERY */
#include "ui_optimist_hold.h"                      /* SYSTEM HOLD: a click is a tap, a hold the layer */
#include "ui_optimist_lane.h"                      /* the lane: preview when stopped, the pick silent playing */
#include "ui_optimist_step35.h"                     /* STEP: entry follows OCT, a held step transposed */
#include "ui_optimist_rechold.h"                   /* REC held: the track cleared */
#include "ui_optimist_drenv.h"                     /* the drum track's ENV: the lane's envelope values */
#include "ui_optimist_ble.h"                        /* with FELUCCA_BLE: SYSTEM > BLUETOOTH, the settings word's bits */
#include "ui_optimist_stepmove.h"                  /* a held step moved: PRESETS in time, ALGORITHM in pitch / lane; REC held on a lane */
#include "ui_optimist_overlap.h"                   /* a synth note past another: the picked note seen, moved past (snap), LENGTH over */
#include "ui_optimist_stephold.h"                  /* STEP: a tap toggles the step, a hold opens its cards only */
#include "ui_optimist_lforate.h"                  /* LFO RATE: the free bar from 0, a synced value as a division */
#include "ui_optimist_mix35.h"                      /* batch 35: the mixer's three views, its knob sets, the title flash */
#include "ui_optimist_cards2.h"                     /* 2026-10-12: the MASTER page's FX returns, OCT on the roll */
#include "ui_optimist_glo.h"                        /* 2026-10-12: GLO tapped opens GLOBAL */
#include "ui_optimist_glwalk.h"                     /* 2026-10-13: every GLOBAL / SYSTEM line walked: knob, PRESETS, actions RUN */
#include "ui_optimist_glpages.h"                    /* 2026-10-13: GLOBAL / SYSTEM in logical pages (op_global.c GL_GROUPS) */
#include "ui_optimist_edscrn.h"                     /* the editor's SCRN (cmd 96): GLOBAL and SYSTEM as the UI's lines */
#include "ui_optimist_edscrs.h"                     /* the editor's SCRS (cmd 99): the other screens as rows of cards */
#include "ui_optimist_edv14.h"                      /* the editor's STEPLANE (cmd 105): a step in a lane's context, the device's lockable list */
#include "ui_optimist_glstyle.h"                    /* 2026-10-14: the look of the lists (title, full-width bars, toggles, joined pages, INFO, About) */
#include "ui_optimist_curline.h"                    /* 2026-10-14: the cursor row: the line's colour at full strength, white text */
#include "ui_optimist_help.h"                       /* 2026-10-13: the help line on the screen's foot (PARAM_HELP) */
#include "ui_optimist_risk.h"                       /* 2026-10-13: every risky action asks, HOME changes nothing */
#include "ui_optimist_actcards.h"                    /* 2026-10-13: action cards: the verb, always asked; PROJECT's keys pick the slot */
#include "ui_optimist_prjflow.h"                      /* 2026-10-14: PROJECT: BANK, OPERATION, ITEM, the keys run it */
#if UI_SDCARD
#include "ui_optimist_sdcard.h"                       /* SD card mode: the card's lists, banks of 16, saves by name */
#endif
#include "ui_optimist_algo.h"                       /* 2026-10-12: ALGORITHM's walk, a lane's PRESETS, the ARP picture */
#include "ui_optimist_mute.h"                       /* 2026-10-12: the mixer's MUTE card */
#include "ui_optimist_solo.h"                       /* 2026-10-16: no solo (the mixer, GLO, a project's lane solo) */
#include "ui_optimist_retmute.h"                   /* 2026-10-16: a return's mute on MASTER, the editor's SOLO op 7 and REC */
#include "ui_optimist_strike.h"                     /* 2026-10-15: a muted track struck through (mixer, header, volume card) */
#include "ui_optimist_smute.h"                      /* 2026-10-13: the sequencer mutes, SHIFT held + a key on the mixer */
#include "ui_optimist_algotrack.h"                  /* 2026-10-13: ALGORITHM is the track and nothing else, every screen and layer */
#include "ui_optimist_uniform.h"                   /* 2026-10-13: the uniform-UI rulings (docs/UI-OPTIMIST-DESIGN.md 2.3) */
#include "ui_optimist_pagemem.h"                    /* 2026-10-13: every screen remembers its page (SOUND per track, the held step) */
#include "ui_optimist_nav.h"                        /* 2026-10-24: the mirror's touch navigation (op_nav.c, 109 NAV) */
#include "ui_optimist_memlist.h"                    /* 2026-10-25: a load that does not fit: the list of what to drop */
/* ---- the buttons' half light: what has a function glows dim, the active button full, what has nothing is dark */
static uint32_t led_lvl(uint32_t b)                     /* 2 full, 1 dim, 0 dark: the LED of panel button b */
{
    uint8_t q = led_pos[panel.btn[b]];
    if (q == 0xFF)
        return 9;
    return (fm1_led[q >> 3] >> (q & 7u)) & 1u ? 2u : (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u ? 1u : 0u;
}
static void led_tests(void)
{
    uint32_t b;
    reset_ui();
    song.sel = 0;
    frames(2);
    check(led_lvl(B_LFO) == 1 && led_lvl(B_ENV) == 1 && led_lvl(B_FX) == 1, "LEDs, a synth track: ENV LFO FX are half lit");
    check(led_lvl(B_HOME) == 2 && led_lvl(B_SAVE) == 1 && led_lvl(B_PLAY) == 0 && led_lvl(B_REC) == 1,
          "LEDs: HOME (the screen) full, SAVE REC half lit, PLAY's red off (stopped: its green is dim, ui_optimist_lamp.h)");
    song.sel = TRK_DRUM;
    frames(2);
    check(led_lvl(B_LFO) == (KLFO_ON ? 1 : 0) && led_lvl(B_ENV) == 0,
          "LEDs, the drum track, no lane picked: no ENV (a lane's pages), LFO only with the kit's (GRIDS_LFO, half)");
    check(led_lvl(B_EDIT) == 1 && led_lvl(B_FX) == 1 && led_lvl(B_GLO) == 1 && led_lvl(B_SEQ) == 1,
          "LEDs, the drum track: EDIT FX GLO SEQ half lit");
    for (b = 0; b < NB; b++)
        if (led_lvl(b) == 9)
            break;
    check(b == NB, "LEDs: every button has an LED place in the test");
    tap(B_SEQ);
    frames(2);
    check(ui.scr == SCR_STEP && led_lvl(B_SEQ) == 2 && led_lvl(B_LFO) == (KLFO_ON ? 1 : 0),
          "LEDs: on STEP the SEQ button is full, LFO as before (dark, half with the kit's LFOs)");
    song.sel = 0;
    tap(B_LFO);
    frames(2);
    check(ui.scr == SCR_SOUND && led_lvl(B_LFO) == 2 && led_lvl(B_ENV) == 1, "LEDs: on the LFO page its button is full, ENV half");
    check(led_lvl(B_GLO) == 1, "LEDs: GLO half lit off GLOBAL");
    tap(B_GLO);
    frames(2);
    check(ui.scr == SCR_GLOBAL && led_lvl(B_GLO) == 2 && led_lvl(B_HOME) == 1, "LEDs: on GLOBAL the GLO button is full, HOME half");
    tap(B_HOME);
    frames(2);
    {
        uint8_t d0[16], d1[16];
        memcpy(d0, fm1_led_dim, sizeof d0);
        frames(5);
        memcpy(d1, fm1_led_dim, sizeof d1);
        check(!memcmp(d0, d1, sizeof d0), "LEDs: nothing changes while nothing changes");
    }
    song.sel = 0;
    reset_ui();
}
#include "ui_optimist_lamp.h"                       /* 2026-10-16: the lamp under PLAY, green playing, red recording */
#include "ui_optimist_btnled.h"                    /* 2026-10-13: every button's LED against what its tap and hold do, screens x track kinds */
#include "ui_optimist_pages.h"                      /* 2026-10-12: the page organisation (D1..D11) */
#include "ui_optimist_laneui.h"                     /* 2026-10-12: the drum lanes' gestures (REC, a held step's locks, the lane's LFO) */
#include "ui_optimist_lanesrc.h"                    /* 2026-10-13: a lane's SRC on page 1, knob 3 */
#include "ui_optimist_effval.h"                    /* 2026-10-12: live effective values, one number and two bars */
#include "ui_optimist_inslots.h"                    /* 2026-10-12: the track insert slots: INSERTS, CMP, SENDS, DR's levels */
#include "ui_optimist_noswap.h"                     /* 2026-10-18: a TYPE turn skips the other slot's type (master, lanes) */
#include "ui_optimist_fxi.h"                         /* 2026-10-13: the limited insert types: free count, FULL, WAIT, hidden */
#include "ui_optimist_gater.h"                      /* GATER (INS_GATER): its FX row, the cards, the picture and its playhead, the help lines */
#include "ui_optimist_lfo2.h"                      /* LFO 2: the LFO 2 and LFO2 DEST rows, RATE, SHIFT + a knob, the AMT bar */
#include "ui_optimist_plocks.h"
#include "ui_optimist_seqtools.h"                   /* Felucca 1.5.6: STEP's TOOLS and EUCLID, the COND card */
#include "ui_optimist_missline.h"                   /* the MISSING line stays until HOME */
#include "ui_optimist_premiss.h"                    /* a user preset of an engine not built: not in PRESETS */
#include "ui_optimist_notename.h"                   /* the note played on the keys, bottom left (Melodee's) */
#include "ui_optimist_stepdots.h"                   /* the drum steps as dots (Melodee's): STEP and the mixer */
#include "ui_optimist_accent.h"                     /* the drum track's ACCENT lane: key 1, STEP's 17th row, the amount, the walk */
#include "ui_optimist_lanevals.h"                  /* a picked lane shows the lane's values; the drum track's own tagged DR */
#include "ui_optimist_hdrnum.h"                     /* the track number in the header badge, the preset after the title */
#include "ui_optimist_names.h"                      /* the names table: titles never cut, card labels in capitals, list cells whole */
#include "ui_optimist_layout.h"                     /* batch D: a badge everywhere, one rule for nothing, no one-card page, no DUST unbuilt */
#include "ui_optimist_geometry.h"                   /* batch B: the help band, units in the label row, the list's columns, the needles */
#include "ui_optimist_stepauto.h"                    /* a step with automation: its own colour (STEP grid, roll, mixer strips) */
#include "ui_optimist_colours.h"                     /* batch C: colour means something (mute tiles, red, dim, REC, SUPER) */
#include "ui_optimist_plines.h"                      /* PAGE_LINES: the rows with no picture as GLOBAL's lines */
#include "ui_optimist_screens.h"                     /* batch E: per-screen oddities (titles, STEP, drums, names, pictures) */
#include "ui_optimist_favorites.h"                   /* FELUCCA_FAVORITES: the FAVORITES row under the SOUND row (docs/UI-OPTIMIST-DESIGN.md 13 Favorites) */

#ifndef UIO_NO_MAIN                                     /* (tests/audio_hang_stress.c: its own main) */
#include "ui_optimist_lockaudit.h"

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : "build/host";
    {
        uint32_t i;
        for (i = 0; i < 5u * 16u; i++)
            FM1_KEYMAP[i / 16u][i % 16u] = -1;
        static const int8_t REAL[4][11] = {                 /* (hal/fm1_input.h's table: the keyless LED sites are where they are: PLAY's green) */
            {5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1}, {34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12},
            {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33}, {0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22}};
        for (i = 0; i < 44u; i++)
            FM1_KEYMAP[1u + i / 11u][i % 11u] = REAL[i / 11u][i % 11u];
    }
    host_tracks_init();
#if FELUCCA_PATTERNS
    memset(nor, 0xFF, sizeof nor);                      /* an erased NOR: the section log empty */
    sec_pend_clear();
    sec_boot();
#endif
    tracks_default();
    band_y = (uint32_t)OP_FOOT_Y;                       /* (no band without the help line: 240) */
    panel = PANEL_DEFAULT;
    settings_init();
    layers_init();
    go_home();
    frames(3);
#if FELUCCA_GRIDS_EUCLID
    {   /* OUT EUCL: one click, one step of a length or one hit (op_tools.c gr_eu_turn), both ways, the ends held */
        uint32_t p, ok = 1, n0, h0;
        int16_t keep[GR_KIT_N];
        memcpy(keep, &TDRUM->p[P_GRX], sizeof keep);
        gr_out = GRO_EUCLID;
        for (p = 0; p < 3u; p++) {
            n0 = gr_eu_len(&TDRUM->p[P_GRX], p);
            gr_kturn(3, p, 1);
            ok &= gr_eu_len(&TDRUM->p[P_GRX], p) == (n0 < 32u ? n0 + 1u : 32u);
            gr_kturn(3, p, -1);
            ok &= gr_eu_len(&TDRUM->p[P_GRX], p) == n0;
            h0 = gr_eu_hits(&TDRUM->p[P_GRX], p);
            gr_kturn(4, p, 1);
            ok &= gr_eu_hits(&TDRUM->p[P_GRX], p) == (h0 < n0 ? h0 + 1u : n0);
            gr_kturn(4, p, -1);
            ok &= gr_eu_hits(&TDRUM->p[P_GRX], p) == h0;
            TDRUM->p[P_GRX + p] = 0;
            gr_kturn(3, p, -1);
            ok &= gr_eu_len(&TDRUM->p[P_GRX], p) == 1u;
        }
        check(ok, "GRIDS OUT EUCL: one click is one step of a part's length or one of its hits, both ways; the lengths stop at 1");
        memcpy(&TDRUM->p[P_GRX], keep, sizeof keep);
        gr_out = GRO_DRUMS;
    }
#endif
#if UI_SDCARD                                           /* (SD card mode's build, run_tests.sh: its tests alone, on the FAT32 image) */
    if (argc < 3) {
        printf("usage: ui_optimist_sdcard_test OUTDIR FAT32.img\n");
        return 2;
    }
    sdu_img = load(argv[2], &sdu_sectors);
    sdcard_tests();
#if FELUCCA_SD_REC
    sdu_rec_tests();
#endif
#if FELUCCA_ENG_FM6
    sdu_dx7_tests();
#endif
    sdu_settings_tests();
    sdcard_cut_tests();
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
#endif
#if UI_MEMLIST                                           /* (the arena's build, run_tests.sh: the list's tests alone) */
    memlist_tests();
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
#endif
#if FELUCCA_PAGE_LINES
    plines_tests();                                     /* (the lines build: the rest of the suite assumes the 2x2 cards) */
    lane_fx_lines_tests();                              /* (LANE FX: the 2x2 cards in the lines build too) */
    fuzz(5000, 4242);
    check(!band_hits, "PAGE_LINES: 5000 random frames, no blit in the help band but the help line's");
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
#endif
#if FELUCCA_PLOCK
    if (getenv("PLOCK_AUDIT"))                          /* (the p-lock / motion list, ui_optimist_lockaudit.h) */
        return (int)lock_audit();
#endif
    rows_tests();
#if FELUCCA_INS_PHASER
    fxpool_tests();
#endif
#if FELUCCA_USB_AUDIO && FELUCCA_INS_GATER && FELUCCA_INS_CHORUS
    usbfx_tests();
#endif
    key_tests();
    confirm_tests();
    message_tests();
    family_tests();
    header_footer_tests();
    header_track_tests();
    hold_mute_tests();
    step_peek_tests();
    lane_fx_ui_tests();
    header_tracknum_tests();
    names_tests();
    bd_layout_tests();
    geometry_tests();
    mixer_tests();
    stepdots_tests();
    scope_tests();
    step_keys_tests();
    step_synth_tests();
    seq_never_song_tests();
    layer_tests();
    save_layer_tests();
    tempo_tests();
    combo_tests();
    drumkit_popup_tests();
    wipe_tests();
    pick_quiet_tests();
    song_tests();
    shortcut_tests();
    undo_extras_tests();
    auto_step_tests();
    name_tests();
    fm6_tests();
    graph_family_tests();
    preset_engine_tests();
    len_tests();
    cards_tests();
    cards_pic_tests();
    step_cards_tests();
    taplate_tests();
    slowframe_tests();
    picker_tests();
    overlap_tests();
    step_cards_leak_tests();
    s35_entry_tests();
    s35_transpose_tests();
    s35_roll_tests();
    s35_drum_codes_tests();
    s35_pages_tests();
    master_tests();
    algo_inplace_tests();
    knobcol_tests();
    clkout_tests();
    uasel_tests();
    newproj_tests();
#if FELUCCA_SYS_DEBUG
    core_row_tests();                                   /* (INFO's every line, INTERNALS, USB STATS: the debug pages) */
    overload_tests();
#else
    sysdbg_off_tests();
#endif
    hold_tests();
    lane_preview_tests();
    step_pages_tests();
    rec_hold_tests();
    drum_env_tests();
    mix35_tests();
    c2_master_tests();
    c2_roll_tests();
    glo_tests();
    glwalk_tests();
    glpages_tests();
    edscrn_tests();
    edscrs_tests();
    edv14_tests();
    glstyle_tests();
    curline_tests();
    help_tests();
    risk_tests();
    risk_edits_tests();
    actcards_tests();
    prjflow_tests();
    colours_tests();
#if FELUCCA_AUTO
    step_auto_tests();
#endif
    algo_tests();
    mute_tests();
    solo_tests();
    retmute_tests();
    strike_tests();
    smute_tests();
    pagemem_tests();
#if FELUCCA_ED_MIRROR
    nav_tests();
#endif
    favorites_tests();
    algotrack_tests();
    uniform_tests();
    lforate_tests();
    step_hold_tests();
    step_move_tests();
    step_pitch_tests();
    led_tests();
    btnled_tests();
    lamp_tests();
    pages_nav_tests();
    pages_master_tests();
    pages_keys_tests();
    keys_global_tests();
    pages_head_tests();
    pages_select_tests();
    pages_hit_tests();
    lane_ui_tests();
    lane_src_tests();
    effval_tests();
    inslots_tests();
    noswap_tests();
    fxi_ui_tests();
#if FELUCCA_INS_GATER
    gater_ui_tests();
#endif
    lfo2ui_tests();
#if FELUCCA_PLOCK
    plock_audit_tests();
#endif
#if FELUCCA_SEQTOOLS || FELUCCA_GENS
    seqtools_tests();
#endif
#if FELUCCA_COND && FELUCCA_CHANCE
    cond_card_tests();
#endif
    accent_ui_tests();
    lanevals_tests();
    screens_tests();
#if FELUCCA_BLE
    ble_tests();
#endif
    premiss_tests();
    missline_tests();
    notename_tests();                                   /* (late: the note it plays moves the master's meters, SCRS's golden) */
    fuzz(20000, 12345);
    check(1, "20000 frames of random use: every draw on the screen");
    if (band_hits)
        printf("  geometry: %u blits reached the help band\n", band_hits);
    check(!band_hits, "#6 the whole run (every test, the random frames): no blit in the help band but the help line's");
#ifdef MIRROR_CHECK
    {
        uint32_t i, diff = 0;
        mc_meter_now = 1;                               /* (the meters' last change: a window) */
        frame();
        mc_meter_now = 0;
        for (i = 0; i < 240u * 240u; i++)
            diff += md_fb[i] != swap16(screen[i]);
        printf("  mirror: %u frames, %u bytes, the screen drawn %u px; %u bad frames, %u px differ\n", mc_frames, mc_bytes, blit_px, mc_bad, diff);
        printf("  mirror catch-up (black to the client, op_repaint): %u, %u not the panel after; %u bytes each on average, %u at most\n",
               mc_resyncs, mc_resync_bad, mc_resyncs ? mc_resync_sum / mc_resyncs : 0u, mc_resync_max);
        check(!mc_bad && !diff, "the screen mirrored: every write decoded, the same pixels");
        printf("  mirror meters: %u windows of 100 ms\n", mc_meter_sends);
        check(mc_meter_sends > 100u, "the screen mirrored: the mixer's meters sent in their windows");
        check(mc_resyncs > 100u && !mc_resync_bad, "the screen mirrored: every catch-up (black to the client, every band again in place) is the panel");
    }
#endif
    printf(fails ? "optimist ui test FAILED (%d)\n" : "optimist ui test passed\n", fails);
    return fails != 0;
}
#endif
