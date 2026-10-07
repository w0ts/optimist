/* SPDX-License-Identifier: GPL-3.0-only */
/* The real UI (ui.c, ui_draw.c, ui_studio.c, ui_layers.c, ui_song.c, ui_menu.c, ui_input.c) on a
 * framebuffer with panel / flash / storage doubles, the audio (mix_block) running between frames as on
 * the device. Renders every screen to PPM for review (DIR/page-*.ppm, live-*.ppm, layer-*.ppm) and
 * drives the panel:
 *   taps    a layer button tapped opens its pages; held + a key / knob it does not
 *   FX      held + a white key: punch-in; knobs: filter, dust, duck
 *   SEQ     held: the steps on the white keys (drums: the sound played last), OCT: pages, a step key
 *           held + KNOB 2 / 3: level / ratchet
 *   EDIT    held + a key: erase; OCT- / OCT+: undo / redo; KNOB 1 shift, 2 length x2
 *   ARP     held + a key: a roll; KNOB 1 the rate
 *   SCL     held + a key: the key of the song
 *   GLO     held + keys: mute, solo, tap tempo; knobs: levels
 *   REC     press: arm / record at once; held: the clear ring, to the end: cleared (undo brings it back)
 *   SAVE    tapped: the song page; held: the SONG layer (sections A..D: play / store, SONG REC)
 *   DRUMS   the grid: sound / step / hit / level knobs, GRID <-> KIT
 *   FM6     ENV on an FM6 track: the operator editor (black keys OP1..OP6 PIT GLO MONO POLY, pages, knobs,
 *           STORE / INIT)
 * then 20000 frames of random use: every draw stays on the screen. */
#define FELUCCA_ARRANGER 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint16_t screen[240*240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ uint32_t i,j; assert(x+w<=240 && y+h<=240); for(j=0;j<h;j++) for(i=0;i<w;i++) screen[(y+j)*240+x+i]=p[j*w+i]; }
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c)
{ uint32_t i,j; assert(x+w<=240&&y+h<=240); for(j=0;j<h;j++)for(i=0;i<w;i++)screen[(y+j)*240+x+i]=swap16(c); }
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
static uint8_t fm1_led[16], fm1_led_dim[16];
#if FELUCCA_LIGHTS
static uint8_t fm1_led_bg[16];                 /* (hal/fm1_input.h: the backlight layer) */
static uint16_t fm1_led_bg_ns;
#endif
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t last_kit;                  /* the last factory kit of this build (the kit list) */
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_drums.c"   /* the drum track's SOUND pages, the kit list */
static uint32_t saves, loads;
static int project_used(uint32_t i) { return i < 2; }
static void project_save(uint32_t i) { (void)i; saves++; ui_message("SAVED"); }
static void project_load(uint32_t i) { (void)i; loads++; }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 3; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void song_backup(void) {}
static void song_restore(void) {}
static uint32_t sec_stores, sec_loads;
static void section_store(uint32_t s) { sec_stores++; live_sec = (int8_t)s; }
static void section_load(uint32_t s) { sec_loads++; live_sec = (int8_t)s; }
static int up_used(uint32_t k) { return k < 2; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 2; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { str_cpy(b, k ? "MY PAD" : "MY LEAD", 13); }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
#include "snap_ui_stub.h"
static void settings_save(void) {}
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_fm6.c"
#include "../firmware/src/icons.c"
static uint32_t proj_orph_uid(uint32_t k) { (void)k; return 0xFFu; }   /* (project.c is not in this test) */
#if FELUCCA_MISSING_WARN
#include "../firmware/src/miss.c"   /* (tests/missing_test.c tests it) */
#endif
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_overview.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#if FELUCCA_MACROS
#include "../firmware/src/macro_ui.c"
#endif
#include "../firmware/src/ui_input.c"
#include "../firmware/src/fm6_store.c"   /* (no flash on the host: STORE is refused) */
#if FELUCCA_DRUM_KITS
/* the user kit bank (drum_kits.c) on a RAM image of its two sectors, through storage.c */
static uint8_t kit_nor[0x2000];
static int st_read(uint32_t off, void *dst, uint32_t n)
{ if (off < 0xDA000u || off + n > 0xDC000u) return -1; memcpy(dst, kit_nor + off - 0xDA000u, n); return 0; }
static int st_erase(uint32_t off) { if (off < 0xDA000u || off >= 0xDC000u) return -1; memset(kit_nor + off - 0xDA000u, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{ uint32_t i; if (off < 0xDA000u || off + n > 0xDC000u) return -1; for (i = 0; i < n; i++) kit_nor[off - 0xDA000u + i] &= ((const uint8_t *)src)[i]; return 0; }
#include "../firmware/src/storage.c"
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drum_kits.c"
#endif
#include "../firmware/src/splash.c"
/* the editor's reply builder, as editor.c has it: the drum source commands (ed_dsrc.c) read the SOUND pages */
static uint8_t ed_out[600];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static void ed_str(const char *s, uint32_t max)
{ uint32_t i; for (i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu); ed_b(0); }
#include "../firmware/src/ed_dsrc.c"
#include "../firmware/src/ed_pages.c"
#include "../firmware/src/ed_status.c"
static const char *outdir;
static void ppm(const char *name) {
    char path[512]; snprintf(path,sizeof path,"%s/%s.ppm",outdir,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;i++) { uint16_t p=swap16(screen[i]); uint8_t rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31}; fwrite(rgb,1,3,f); }
    fclose(f);
}
/* one UI frame (~16 ms): the audio between (as the ISR does), then input, LEDs, draw */
static void frame(void)
{
    uint32_t q;
    static int32_t o[CTL * 2];
    for (q = 0; q < 22u; q++) mix_block(o, CTL);
    ui_input(); ui_leds(); ui_draw(); fm1_ms += 16;
}
static void frames(uint32_t n) { while (n--) frame(); }
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void key(uint32_t k) { fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static int fails;
static void check(int ok, const char *what) { printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
#include "backports_ui.c"         /* the backported features' UI (each with its switch) */
#include "bp23_ui.c"              /* the SLOOP 2.3 / X0X 0.10.1 backports' UI (each with its switch) */
#include "fel102_ui.c"            /* the Felucca 1.0.2 / 1.0.3 small options' UI (each with its switch) */
#include "param_help_ui.c"        /* the knobs' help lines (FELUCCA_PARAM_HELP) */

/* fuzz: n frames of random buttons (held or tapped), knobs and keys, with the audio running between
 * frames; every draw stays on the screen (lcd_blit / lcd_fill assert it) */
static void fuzz(uint32_t n, uint32_t seed)
{
    uint32_t f, held = 0;
    #define R(n) ((seed = seed * 1664525u + 1013904223u) >> 8) % (n)
    for (f = 0; f < n; f++) {
        if (R(6) == 0) {
            uint32_t bt = R(NB);
            edges_btn |= 1u << panel.btn[bt];
            if (bt != B_HOME && R(3) == 0) held ^= 1u << panel.btn[bt];
        }
        if (R(30) == 0) held = 0;
        if (R(700) == 0) ly_lock = (uint8_t)R(LY_COUNT);              /* (a layer locked open, at random) */
        fm1_in.buttons = held & ~(1u << panel.btn[B_HOME]);
        if (R(4) == 0) encs[R(7)] += (int32_t)R(5) - 2;
        fm1_in.notes = R(10) == 0 ? (1u << R(27)) : (R(3) ? fm1_in.notes : 0);
        frame();
        if (ui.menu) { ui.menu = 0; ui.force = 1; }               /* (the menu is tested above) */
    }
    fm1_in.buttons = 0; fm1_in.notes = 0; frames(4);
    #undef R
}

#if DL_UI
/* the drum track's SOUND pages (ui_drums.c): EDIT on the drum track, the sound last played or picked with
 * EDIT + its key, the four pages, a user sample on a lane, RESET; the kit list with the user kits */
static void view_set(uint32_t v) { song.g[G_VIEW] = (int16_t)v; settings.view = v; ui.force = 1; frame(); }
static void drum_sound_tests(void)
{
    uint32_t age, i, mask;
    song.sel = TRK_DRUM; song.playing = 0; transport_req = 0; go_home(); frame();
    memset(&dl, 0, sizeof dl);
    TDRUM->p[P_E0] = DRUM_SAMPLED; frames(2);                 /* the 808 */
    key(key_of_white(2));
    check(pen_lane == 2, "SOUND: a drum key played: the sound to edit (snare)");
    tap(B_EDIT); frames(2);
    check(cur_page()->scope == SC_DSND && cur_page()->id[0] == 0, "drum track, EDIT tapped: the SOUND page");
    view_set(0); ppm("page-sound");
    encs[panel.enc[EN_K1]] = 3; frames(2);
    check(dl.ofs[2][DE_TUNE] == 3, "SOUND: KNOB 1 TUNE +3 on the snare");
    encs[panel.enc[EN_K2]] = -10; frames(2);
    check(dl.ofs[2][DE_DECAY] < -5, "SOUND: KNOB 2 DECAY shorter");
    ui.force = 1; frame(); ppm("page-sound-edited");
    tap(B_EDIT); frames(2);
    check(cur_page()->scope == SC_DSND && cur_page()->id[0] == 4, "EDIT again: SOUND 2 (BEND CUT DRIVE LEVEL)");
    encs[panel.enc[EN_K4]] = -3; frames(2);
    check(dl.ofs[2][DE_LEVEL] == -3, "SOUND 2: KNOB 4 LEVEL -3 dB");
    ui.force = 1; frame(); ppm("page-sound2");
    tap(B_EDIT); frames(2);
    check(cur_page()->id[0] == 16 || !FELUCCA_DRUM_SENDS, "EDIT again: SOUND 3 (REV DLY CHO)");
#if FELUCCA_DRUM_SENDS
    encs[panel.enc[EN_K1]] = 10; encs[panel.enc[EN_K2]] = 5; frames(2);
    check(dsend_rev(dsend[2]) >= 8 && dsend_dly(dsend[2]) >= 4 && dsend_cho(dsend[2]) == 0,
          "SOUND 3: KNOB 1 REV up from TRK, KNOB 2 DLY, on the snare");
    ui.force = 1; frame(); ppm("page-sound3");
    tap(B_EDIT); frames(2);
#endif
    check(cur_page()->id[0] == 8 || !(FELUCCA_DRUM_USR || FELUCCA_DRUM_KITS), "EDIT again: SOURCE");
    ui.force = 1; frame(); ppm("page-source");
#if FELUCCA_DRUM_KITS
    {   /* Optimist: SRC after the kits: the X0X voices of this build (drums.c DRUM_SRC_NAMES), each to its source */
        uint32_t i, ok = 1;
        for (i = 0; i <= (uint32_t)DSD[8].max; i++)
            ok &= dsnd_src_idx(dsnd_idx_src(i)) == i;
        ok &= (uint32_t)DSD[8].max == 3u + DRUM_KITS + DS_SRC_XN;
#if DRUM_X0X
        ok &= !strcmp(DS_SRC_NAMES[DSD[8].max], FELUCCA_DRUM_X808 ? "X8 CY" : FELUCCA_X909_CYM ? "X9 RD" : "X9 OH");
        dsnd_set(8, (int32_t)(4u + DRUM_KITS), 1);       /* the first X0X voice on the snare */
        ok &= dl.src[2] == (FELUCCA_DRUM_X909 ? DL_X909 : DL_X808);
        ui.force = 1; frame(); ppm("page-source-x0x");
        dl.src[2] = DL_KIT;
#endif
        check(ok, "SOURCE: SRC lists the kits, then this build's X0X voices; a voice picked is the lane's source");
    }
#endif
    tap(B_EDIT); frames(2);
    check(cur_page()->id[0] == 12, "EDIT again: KIT");
    ui.force = 1; frame(); ppm("page-kit");
    tap(B_EDIT); frames(2);
    check(cur_page()->id[0] == 0, "EDIT again: SOUND (round)");
    /* EDIT held + a key: the sound picked, nothing played, nothing erased */
    dstep_set(&TDRUM->dstep[0], 5, LV_NORM, 0);
    age = drums.age; mask = dstep_mask(&TDRUM->dstep[0]);
    press(B_EDIT); frames(12);
    ui.force = 1; frame(); ppm("layer-sound-pick");
    key(key_of_white(5));
    check(pen_lane == 5 && drums.age == age && dstep_mask(&TDRUM->dstep[0]) == mask,
          "SOUND: EDIT + the open hat key: picked, not played, not erased");
    release(B_EDIT);
    check(cur_page()->scope == SC_DSND, "EDIT let go: the SOUND page stays");
    /* a sampled kit: TUNE DECAY / CUT LEVEL only */
    {
        int16_t *vp;
        TDRUM->p[P_E0] = 0; frames(2);
        check(page_desc(cur_page(), 0, &vp) && !page_desc(cur_page(), 2, &vp) && !page_desc(cur_page(), 3, &vp),
              "ACOUSTIC: SOUND shows TUNE DECAY, no SNAP CLICK");
        ui.force = 1; frame(); ppm("page-sound-sampled");
        TDRUM->p[P_E0] = DRUM_SAMPLED; frames(2);
    }
    /* VIEW ALL: a row per page */
    view_set(1);
    check(ov_on() && cur_page()->scope == SC_DSND, "VIEW ALL: the SOUND pages as rows");
    ppm("overview-sound");
    view_set(0);
#if FELUCCA_DRUM_USR
    /* SOURCE: USR1 on the open hat, HIT 2, START, LEN */
    open_family(FAM_EDIT); open_family(FAM_EDIT); frames(1);
    while (cur_page()->id[0] != 8) tap(B_EDIT);
    encs[panel.enc[EN_K1]] = 1; frames(2);
    check(dl.src[5] == DL_USR, "SOURCE: KNOB 1 -> USR1");
    encs[panel.enc[EN_K2]] = 1; encs[panel.enc[EN_K3]] = 64; encs[panel.enc[EN_K4]] = -63; frames(2);
    check(dl_hit(dl.ref[5]) == 1 && dl_start(dl.ref[5]) == 512 && dl_len(dl.ref[5]) == 520,
          "SOURCE: HIT 2, START half, LEN half");
    ui.force = 1; frame(); ppm("page-source-usr");
#endif
    /* KIT: RESET (twice) */
    while (cur_page()->id[0] != 12) tap(B_EDIT);
    encs[panel.enc[EN_K4]] = 1; frames(2);
    check(dl.src[5] != DL_KIT || dl.ofs[5][0] || 1, "KIT: RESET armed");
    encs[panel.enc[EN_K4]] = 1; frames(2);
    for (i = 0; i < DE_N; i++) mask |= (uint32_t)dl.ofs[5][i];
    check(dl.src[5] == DL_KIT && !dl.ofs[5][0], "KIT: RESET again: the open hat as the kit has it");
#if FELUCCA_DRUM_KITS
    /* SAVE into slot 3, the kit list, load from PRESETS */
    memset(kit_nor, 0xFF, sizeof kit_nor); uk_read = 0;
    pen_lane = 0; dl.ofs[0][DE_TUNE] = -5;                     /* (the kick a bit lower) */
    encs[panel.enc[EN_K1]] = 2; frames(2);
    check(dsnd_slot == 2, "KIT: KNOB 1 the slot (3)");
    encs[panel.enc[EN_K2]] = 1; frames(2); encs[panel.enc[EN_K2]] = 1; frames(2);
    check(ukit_used(2) && dl.ukit == 3u && ukit_count() == 1u, "KIT: SAVE twice: stored in slot 3, the project plays it");
    ui.force = 1; frame(); ppm("page-kit-saved");
    memset(&dl, 0, sizeof dl); dl_e0 = TDRUM->p[P_E0];
    { uint32_t lk = DRUM_KITS - 1u; while (!drum_kit_built(lk)) lk--; last_kit = lk; }   /* (the last kit built) */
    TDRUM->p[P_E0] = (int16_t)last_kit; dl_e0 = (int16_t)last_kit; go_home(); frames(1);
    encs[panel.enc[EN_PRESET]] = 1; frames(2);
    check(dl.ukit == 3u && dl.ofs[0][DE_TUNE] == -5 && TDRUM->p[P_E0] == DRUM_SAMPLED,
          "PRESETS past the last kit: the user kit (lanes, its kit)");
    studio_open(SC_DRUM); drum_page = 1; ui.force = 1; frame(); ppm("live-kit-user");
    encs[panel.enc[EN_PRESET]] = -1; frames(2);       /* (the DRUMS screen: PRESETS walks the kits too) */
    check(!dl.ukit && TDRUM->p[P_E0] == (int16_t)last_kit && !dl.ofs[0][DE_TUNE], "PRESETS back: the last factory kit, lanes as the kit");
    drum_page = 0;
    open_family(FAM_EDIT); while (cur_page()->id[0] != 12) tap(B_EDIT);
    encs[panel.enc[EN_K3]] = 1; frames(2); encs[panel.enc[EN_K3]] = 1; frames(2);
    check(!ukit_used(2) && ukit_count() == 0u, "KIT: ERASE twice: slot 3 empty");
#endif
    memset(&dl, 0, sizeof dl);
#if DL_ANY
    {   /* the editor's DRUM_SRCS (50): the SRC list page by page, with each source's kind; DRUM_SHOW (51): the values
           a lane's source has (the SOUND pages' own rule) */
        uint8_t a[1];
        uint32_t total = 0, seen = 0, kinds = 0, ok = 1, p, j, n;
        int ok_names = 1;
        do {
            a[0] = (uint8_t)seen; ed_n = 0;
            ok &= (uint32_t)ed_dsrc(ED_DRUM_SRCS, a, 1);
            total = ed_out[1]; n = ed_out[2]; p = 3;
            for (j = 0; j < n; j++) {
                uint32_t src = ed_out[p], kind = ed_out[p + 1], i = seen + j;
                p += 2;
                ok_names &= !strcmp((const char *)ed_out + p, DS_SRC_NAMES[i]) && (src == 127u || dsnd_idx_src(i) == src);
                if (src != 127u && src >= DL_KIT0 && src - DL_KIT0 < DRUM_KITS)
                    ok_names &= (kind & 7u) == ((src - DL_KIT0) < DRUM_SAMPLED ? 2u : (src - DL_KIT0) < DRUM_SYNTH_END ? 3u : 4u)
                                && !(kind & 8u) == !!drum_kit_built(src - DL_KIT0);
                kinds |= 1u << (kind & 7u);
                p += str_len((const char *)ed_out + p) + 1u;
            }
            seen += n;
        } while (n && seen < total);
        check(ok && total == (uint32_t)DSD[8].max + 1u && seen == total && ok_names && p <= sizeof ed_out &&
              (kinds & 1u) && (!FELUCCA_DRUM_KITS || (kinds & 8u)),
              "editor DRUM_SRCS: the SRC list in pages (src, kind, name as the SOURCE page)");
        dl.src[3] = DL_KIT0 + 0u;                          /* the clap from ACOUSTIC (sampled) */
        dl.src[2] = DL_KIT0 + DRUM_SAMPLED;                /* the snare from the first synthesised kit */
        a[0] = 3; ed_n = 0; ed_dsrc(ED_DRUM_SHOW, a, 1);
        n = ed_out[1] | (uint32_t)ed_out[2] << 7;
        check(ed_n == 5u + str_len(LANE_NAME[3]) && !strcmp((const char *)ed_out + 4, LANE_NAME[3]), "editor DRUM_SHOW: the lane's name after the flags");
        a[0] = 2; ed_n = 0; ed_dsrc(ED_DRUM_SHOW, a, 1);
        p = ed_out[1] | (uint32_t)ed_out[2] << 7;
        check(!FELUCCA_DRUM_EDIT || !FELUCCA_DRUM_KITS ||
              (n == ((1u << DE_TUNE) | (1u << DE_DECAY) | (1u << DE_CUT) | (1u << DE_LEVEL)) && p == 0xFFu && !(ed_out[3] & 2u)),
              "editor DRUM_SHOW: a sampled sound TUNE DECAY CUT LEVEL, a synthesised one all 8");
        a[0] = 16; ed_n = 0;
        check(!ed_dsrc(ED_DRUM_SHOW, a, 1) && !ed_dsrc(ED_DRUM_SRCS, a, 0), "editor DRUM_SHOW lane 16 / DRUM_SRCS without start: no reply");
        memset(&dl, 0, sizeof dl);
    }
#endif
    {   /* the editor's PAGES (52): params.c PAGES in pages of 24, each shown or not for the selected track */
        uint8_t a[1];
        uint32_t seen = 0, n, p, j, k, ok = 1, total = 0, shown_dsnd = 0, shown_env = 0;
        song.sel = TRK_DRUM;
        do {
            a[0] = (uint8_t)seen; ed_n = 0;
            ok &= (uint32_t)ed_pages(ED_PAGES, a, 1);
            total = ed_out[1]; n = ed_out[2]; p = 3;
            for (j = 0; j < n; j++) {
                const page_t *pg = &PAGES[seen + j];
                ok &= ed_out[p] == pg->fam && ed_out[p + 1] == pg->scope && ed_out[p + 2] == (uint8_t)!!page_shown(pg);
                for (k = 0; k < 4u; k++)
                    ok &= ed_out[p + 3 + k] == (pg->id[k] == 0xFFu ? 127u : pg->id[k]);
                ok &= !strcmp((const char *)ed_out + p + 7, pg->title);
                shown_dsnd |= pg->scope == SC_DSND && ed_out[p + 2];
                shown_env |= pg->scope == SC_TRACK && pg->id[0] == P_ATK && ed_out[p + 2];
                p += 7 + str_len(pg->title) + 1u;
            }
            seen += n;
        } while (n && seen < total);
        check(ok && total == NPAGES && seen == total && (shown_dsnd || !DL_ANY) && shown_env,
              "editor PAGES: every page (family, scope, shown for the drum track, ids, title)");
        song.sel = 0;
    }
    {   /* the editor's STATUS (53): PLAY / STOP as the button, the steps playing, the peak bytes 0 (no meters) */
        uint8_t a[1] = {1};
        uint32_t c, okp = 1, pk = 0;
        int was = song.playing;
        ed_n = 0;
        check(ed_status(ED_STATUS, a, 1) && transport_req == 1u, "editor STATUS 1: PLAY asked (transport_req)");
        frames(30);
        ed_n = 0;
        ed_status(ED_STATUS, a, 0);
        for (c = 0; c < NTRK; c++) {
            okp &= ed_out[4 + c * 3] < NSTEP;
            pk |= ed_out[5 + c * 3] | ed_out[6 + c * 3];
        }
        check((ed_out[0] & 1u) && song.playing && ed_out[1] == (uint8_t)((song.g[G_BPM] + 8192) & 127) && okp && ed_n == 4u + NTRK * 3u && !pk,
              "editor STATUS: playing, BPM, the step of every track, the peak bytes 0");
        a[0] = 2; ed_n = 0; ed_status(ED_STATUS, a, 1); frames(2);
        ed_n = 0; ed_status(ED_STATUS, a, 0);
        check(!song.playing && !(ed_out[0] & 1u) && ed_out[4] == 127u, "editor STATUS 2: STOP; stopped: no step");
        (void)was;
    }
    song.sel = 0; go_home(); frames(2);
    check(!on_dsnd_page(), "another track: no SOUND page");
    view_set(1);
}
#endif

/* the FM6 operator editor (ui_fm6.c): ENV on an FM6 track. The black keys (F#3 = key 1 ..): OP1..OP6 at
 * 1 3 5 8 10 13, PIT 15, GLO 17, MONO 20, POLY 22 */
static int fm6_sounding(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return 1;
    return 0;
}
static void fm6_editor_tests(void)
{
    enum { K_OP1 = 1, K_OP2 = 3, K_OP6 = 13, K_PIT = 15, K_GLO = 17, K_MONO = 20, K_POLY = 22, K_LAST = 25 };
    int16_t *ed = fm6_ed[0];
    uint32_t a, pg;
    song.sel = 0; song.octave = 0; go_home(); frame();
    set_engine_of(&trk[0], ENG_IX_FM6); frames(3);
    check(trk[0].engine == ENG_IX_FM6 && fm6_cur[0] != 0, "FM6 track: its voice loaded");
    tap(B_ENV);
    check(on_fm6k_page() && fm6ui.target == 0 && ui.layer == LY_PLAY, "FM6: ENV tapped: the operator editor, OP1");
    ui.force = 1; frame(); ppm("fm6-op1-freq");
    tap(B_ENV); check(fm6ui.sub[0] == 1, "FM6: ENV tapped again: its next page (level)");
    tap(B_ENV); tap(B_ENV); tap(B_ENV); tap(B_ENV); tap(B_ENV);
    check(fm6ui.sub[0] == 0, "FM6: six pages for an operator, then round");
    press(B_ENV); frames(12);
    check(ui.layer == LY_OPS && on_fm6k_page() && ly_ops_on, "FM6: ENV held: the ops layer, the editor shows");
    check(keys_lit() & 1u << K_OP1, "FM6: ENV held: OP1's black key lit");
    key(K_OP2); check(fm6ui.target == 1, "FM6: ENV + the OP2 key: operator 2");
    a = (uint32_t)ed[FM6_OPB(2) + FO_CRS];
    encs[panel.enc[EN_K2]] = 1; frames(2);
    check((uint32_t)ed[FM6_OPB(2) + FO_CRS] == (a < 31u ? a + 1u : 31u), "FM6: ENV + KNOB 2: OP2 coarse +1 (the buffer)");
    check(fm6_pt[0].hash != 0, "FM6: the edit reaches the part (its hash taken)");
    ui.force = 1; frame(); ppm("fm6-op2-held");
    fm1_in.notes = 1u << 7; frames(4);
    check(fm6_sounding(&trk[0]), "FM6: ENV + a white key: it plays (audition while editing)");
    fm1_in.notes = 0; frames(2);
    key(K_OP6); check(fm6ui.target == 5, "FM6: ENV + the OP6 key: operator 6");
    pg = fm6ui.sub[0];
    key(K_LAST); check(fm6ui.target == 5, "FM6: the last black key: nothing (no OP7 / OP8)");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(fm6ui.sub[0] == (pg + 1u) % 6u, "FM6: ENV + OCT+: the next page");
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(fm6ui.sub[0] == pg && song.octave == 0, "FM6: ENV + OCT-: the page back, the octave stays");
    fm6ui.sub[0] = 1;                                   /* level page: KNOB 4 switches OP6 off */
    encs[panel.enc[EN_K4]] = -1; frames(2);
    check(ed[FV_ON + 5] == 0, "FM6: OP6 level page, KNOB 4: the operator switched off");
    encs[panel.enc[EN_K4]] = 1; frames(2);
    check(ed[FV_ON + 5] == 1, "FM6: ... and on again");
    key(K_PIT); check(fm6ui.target == FMT_PIT, "FM6: ENV + PIT: the pitch envelope");
    ui.force = 1; frame(); ppm("fm6-pitch");
    key(K_MONO); check(trk[0].p[P_VOICE] == V_MONO && (keys_lit() >> K_MONO & 1u), "FM6: ENV + MONO: mono, its key lit");
    key(K_POLY); check(trk[0].p[P_VOICE] == V_POLY, "FM6: ENV + POLY: poly");
    key(K_GLO); check(fm6ui.target == FMT_GLO, "FM6: ENV + GLO: algorithm, LFO, porta, store");
    release(B_ENV);
    check(ui.layer == LY_PLAY && on_fm6k_page() && fm6ui.sub[2] == 0, "FM6: ENV let go after use: the editor stays, same page");
    frames(16);                                   /* (#39: KNOB 1..4 are quiet for 250 ms after a used layer closes) */
    a = (uint32_t)ed[FV_ALG];
    encs[panel.enc[EN_K1]] = 3; frames(2);
    check((uint32_t)ed[FV_ALG] == (a + 3u > 31u ? 31u : a + 3u), "FM6: KNOB 1 on the editor (ENV up): the algorithm");
    ui.force = 1; frame(); ppm("fm6-global-algo");
    fm6ui.sub[2] = 3;                                   /* porta: always, time */
    encs[panel.enc[EN_K1]] = 1; frames(2);
    encs[panel.enc[EN_K2]] = 5; frames(2);
    check(ed[FN_PMODE] == 1 && ed[FN_PTIME] > 0, "FM6: PORTA page: on, a time");
    ui.force = 1; frame(); ppm("fm6-global-porta");
    fm6ui.sub[2] = 4; fm6k_page_entered();              /* store: slot, STORE / SEND / INIT armed twice */
    encs[panel.enc[EN_K1]] = 2; frames(2);
    check(fm6ui.slot == 2, "FM6: STORE page: KNOB 1 the user slot");
    ui.force = 1; frame(); ppm("fm6-global-store");
    encs[panel.enc[EN_K4]] = 1; frames(2);
    check(ui.arm == 0xF2u, "FM6: INIT: one detent arms");
    encs[panel.enc[EN_K4]] = 1; frames(4);
    check(ui.arm == 0 && ed[FV_ALG] == 0 && ed[FM6_OPB(2) + FO_OL] == 0, "FM6: INIT: a second one, the init voice");
    encs[panel.enc[EN_K2]] = 1; frames(2); encs[panel.enc[EN_K2]] = 1; frames(2);
    check(trk[0].p[P_E0] != (int16_t)(FM6_NROM + 2u), "FM6: STORE without flash (the host): refused, VOICE stays");
    fm1_in.notes = 1u << 3; frames(3);
    check(fm6_sounding(&trk[0]), "FM6: ENV up: a black key plays a note again");
    fm1_in.notes = 0; frames(2);
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(trk[0].eng_req == ENG_IX_FM6, "FM6: PRESETS on the editor: no stray preset over the edits");
    fm6ui.target = 0; fm6ui.sub[0] = 2; ui.force = 1; frame(); ppm("fm6-op1-egrate");
    fm6ui.sub[0] = 4; ui.force = 1; frame(); ppm("fm6-op1-keyscale");
    fm6ui.sub[0] = 0;
    {   /* every algorithm on the screen */
        uint32_t al;
        for (al = 0; al < 32u; al++) {
            ed[FV_ALG] = (int16_t)al;
            ui.force = 1; frame();
        }
        ed[FV_ALG] = 0;
    }
    track_select(1); frames(2);
    check(trk[1].eng_req != ENG_IX_FM6 && !on_fm6k_page() && cur_fam() == FAM_ENV,
          "FM6: another track (not FM6): back to its ENV pages");
    tap(B_ENV); check(!on_fm6k_page() && cur_fam() == FAM_ENV, "not FM6: ENV tapped: the ENV pages as before");
    press(B_ENV); frames(12);
    check(ui.layer == LY_PLAY && !ly_ops_on, "not FM6: ENV held: no layer");
    release(B_ENV);
    track_select(0); go_home(); frames(2);
    fuzz(6000, 4242);                                   /* random use with track 1 on FM6 */
    check(1, "FM6: 6000 frames of random use on an FM6 track");
    set_engine_of(&trk[0], trk_def_engine(0)); apply_preset_to(&trk[0], trk_def_preset(0));
    trk[0].p[P_VOICE] = V_POLY; go_home(); frames(3);
}

/* a SAMPLE preset of a set this build leaves out (its set has no zones): gen_samples.py SET_PRESETS */
static int smp_preset_gone(const char *name)
{
    static const char *const P[][2] = {{"GRAND PNO", "PIANO"}, {"DUSTY PNO", "PIANO"}, {"LOFI KEYS", "PIANO"},
        {"UP BASS", "BASS"}, {"DEEP BASS", "BASS"}, {"VIBES", "VIBES"}, {"HORN STAB", "HORNS"},
        {"STRING STB", "STRGS"}, {"LOFI FLUTE", "FLUTE"}, {"SCRATCH", "SCRCH"}, {"GM KIT", "PERC"}};
    uint32_t i, k;
    for (i = 0; i < sizeof P / sizeof P[0]; i++)
        if (str_eq(P[i][0], name))
            for (k = 0; k < SMP_NSETS; k++)
                if (str_eq(SMP_SETS[k].name, P[i][1]))
                    return SMP_SETS[k].nz == 0;
    return 0;
}

/* the FM6 editor with VIEW ALL (an operator's pages as rows of 4 x 4 PAGEs, PIT, GLO) and the long ENV
 * press (the algorithm full screen) */
/* FM6's ENGINE (EDIT 2) lists the modes this build has (eng_fm6.c fm6_desc): run_tests.sh builds this test with
 * all three, with MODERN left out and with MARK I only. The stored value never moves by itself: a part saved
 * with a mode left out keeps it (a build with it plays it as saved) and shows the mode it plays */
static uint32_t edit2_page(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES && strcmp(PAGES[i].title, "EDIT 2"); i++)
        ;
    return i;
}
static void engine_shown(char *val)
{
    int16_t *vp;
    const char *u;
    const param_desc_t *d = page_desc(&PAGES[edit2_page()], 0, &vp);
    val[0] = 0;
    if (d && d->label[0] != '-')
        param_format(d, *vp, val, &u);
}
static void fm6_engine_tests(void)
{
    track_t *t = &trk[0];
    uint32_t i, e2 = edit2_page(), n;
    char v[8];
    song.sel = 0; song.playing = 0; go_home(); frames(2);
    set_engine_of(t, ENG_IX_FM6); frames(3);
    view_set(0);
    for (n = 0, i = 0; i < 8u; i++) {                   /* EDIT tapped round its pages: is EDIT 2 among them? */
        tap(B_EDIT); frames(1);
        n += ui.page == e2;
    }
    t->p[P_E4] = 1; ui.page = (uint8_t)e2; ui.force = 1; frame(); ppm("fm6-engine-edit2");
    if (FM6_NMODES == 1) {
        check(!n && ui.page != e2 && !page_shown(&PAGES[e2]), "FM6, one ENGINE mode: EDIT 2 (nothing on it) not shown, PATCH is");
        t->p[P_E4] = 0; ui.page = (uint8_t)page_first(FAM_EDIT); frames(2);
        for (i = 0; i < 4u; i++) { encs[panel.enc[EN_K1 + i]] = 1; frames(2); }
        check(t->p[P_E4] == 0 && fm6_mode(t->p[P_E4]) == (FELUCCA_FM6_MARK1 ? 1u : FELUCCA_FM6_MODERN ? 0u : 2u),
              "FM6, one mode: a part saved with another keeps it, plays the one built");
        return;
    }
    check(n && ui.page == e2 && page_shown(&PAGES[e2]), "FM6: EDIT 2 shows ENGINE");
    t->p[P_E4] = FM6_PLAYS(1);                          /* (a mode built: MARK I, else the first) */
    encs[panel.enc[EN_K1]] = -5; frames(2);
    engine_shown(v);
    check(t->p[P_E4] == (FELUCCA_FM6_MODERN ? 0 : FELUCCA_FM6_MARK1 ? 1 : 2) && v[0],
          "FM6: KNOB 1 down to the first mode built");
    for (n = 1, i = 0; i < 4u; i++) {                   /* one detent at a time: the modes built, in order */
        int16_t was = t->p[P_E4];
        encs[panel.enc[EN_K1]] = 1; frames(2);
        n += t->p[P_E4] != was;
        check(t->p[P_E4] == was || FM6_BUILT(t->p[P_E4]), "FM6: a detent lands on a mode built");
    }
    check(n == FM6_NMODES, "FM6: KNOB 1 steps through the modes built only");
    if (FM6_NMODES == 2 && !FELUCCA_FM6_MODERN) {
        char mk[8];
        const char *u;
        param_format(&ENG_FM6.edit[4], 1, mk, &u);      /* (MARK I as the column shows it) */
        t->p[P_E4] = 0; ui.force = 1; frame(); ppm("fm6-engine-stored-modern");
        engine_shown(v);
        check(!strcmp(v, mk) && t->p[P_E4] == 0, "FM6 without MODERN: a part saved MODERN shows MARK I (plays it), keeps 0");
        encs[panel.enc[EN_K1]] = -1; frames(2);
        check(t->p[P_E4] == 0, "... KNOB 1 down: nothing below");
        encs[panel.enc[EN_K1]] = 1; frames(2);
        engine_shown(v);
        check(t->p[P_E4] == 2 && !strcmp(v, "OPL"), "... KNOB 1 up: OPL (a detent shows another mode)");
    }
}

static void fm6_view_tests(void)
{
    enum { K_OP2 = 3, K_PIT = 15, K_GLO = 17 };
    int16_t *ed = fm6_ed[0];
    uint32_t first, act, page, npages, n, sub, a;
    song.sel = 0; song.playing = 0; go_home(); frames(2);
    set_engine_of(&trk[0], ENG_IX_FM6); frames(3);
    view_set(1);
    tap(B_ENV); frames(2);
    fm6ui.target = 0; fm6ui.sub[0] = 0; ui.force = 1; frame();
    check(on_fm6k_page() && fm6ui.mode == FMV_ALL, "FM6 VIEW ALL: the operator's pages as rows");
    ppm("fm6-all-op1-page1");
    n = ov_grid(6, fm6ui.sub[0], &first, &act, &page, &npages);
    check(n == 4u && page == 0u && npages == 2u, "FM6 VIEW ALL: FREQ .. EG LEVEL on PAGE 1/2");
    tap(B_ENV); tap(B_ENV); tap(B_ENV); frames(2);
    check(fm6ui.sub[0] == 3u && fm6ui.mode == FMV_ALL && !fm6ui.algo, "FM6 VIEW ALL: ENV tapped three times: EG LEVEL lit (quick taps: no diagram)");
    tap(B_ENV); frames(2);
    n = ov_grid(6, fm6ui.sub[0], &first, &act, &page, &npages);
    check(fm6ui.sub[0] == 4u && n == 2u && act == 0u && page == 1u, "FM6 VIEW ALL: past row 4: PAGE 2/2, KEY SCALE lit");
    ui.force = 1; frame(); ppm("fm6-all-op1-page2");
    a = (uint32_t)ed[FM6_OPB(1) + FO_LD];
    encs[panel.enc[EN_K2]] = 2; frames(2);
    check((uint32_t)ed[FM6_OPB(1) + FO_LD] == (a + 2u > 99u ? 99u : a + 2u), "FM6 VIEW ALL: KNOB 2 edits the lit row (L DEPTH)");
    tap(B_ENV); tap(B_ENV); frames(2);
    check(fm6ui.sub[0] == 0u, "FM6 VIEW ALL: past the last row: PAGE 1, FREQ");
    press(B_ENV); key(K_PIT); release(B_ENV); frames(2);
    check(fm6ui.target == FMT_PIT && fm6ui.mode == FMV_ALL, "FM6 VIEW ALL: PIT, its two pages as rows");
    ui.force = 1; frame(); ppm("fm6-all-pit");
    press(B_ENV); key(K_GLO); release(B_ENV); frames(2);
    fm6ui.sub[2] = 0; ui.force = 1; frame(); ppm("fm6-all-glo-page1");
    fm6ui.sub[2] = 4; fm6k_page_entered(); ui.force = 1; frame(); ppm("fm6-all-glo-page2");
    n = ov_grid(5, fm6ui.sub[2], &first, &act, &page, &npages);
    check(n == 1u && page == 1u && npages == 2u, "FM6 VIEW ALL: GLO: STORE alone on PAGE 2/2");
    press(B_ENV); key(1); release(B_ENV); frames(2);      /* OP1 */
    /* the long press */
    fm6ui.sub[0] = 2; sub = fm6ui.sub[0];
    press(B_ENV); frames(10);
    check(!fm6ui.algo && fm6ui.mode == FMV_ALL, "ENV held 0.18 s: the page (no diagram yet)");
    frames(22);
    check(fm6ui.algo && fm6ui.mode == FMV_ALGO, "ENV held 0.5 s: the algorithm full screen");
    ui.force = 1; frame(); ppm("fm6-algo-long-press");
    key(K_OP2);
    check(fm6ui.target == 1u && fm6ui.algo, "ENV + OP2 on the diagram: OP2 picked, lit there");
    ui.force = 1; frame(); ppm("fm6-algo-op2");
    release(B_ENV); frames(2);
    check(!fm6ui.algo && fm6ui.mode == FMV_ALL && fm6ui.sub[0] == sub, "ENV let go: the page back, the page unturned (no tap)");
    press(B_ENV); frames(32);
    check(fm6ui.algo, "ENV held again: the diagram");
    a = (uint32_t)ed[FM6_OPB(2) + FO_R1];
    encs[panel.enc[EN_K1]] = 1; frames(2);
    check(!fm6ui.algo && fm6ui.mode == FMV_ALL && (uint32_t)ed[FM6_OPB(2) + FO_R1] != a,
          "a knob with ENV held: the page back at once, the edit made (EG RATE R1)");
    frames(10);
    check(!fm6ui.algo, "... and the diagram stays away until ENV comes up");
    release(B_ENV); frames(2);
    a = (uint32_t)ed[FV_ALG];
    ed[FV_ALG] = 31; ed[FV_FB] = 6;                     /* algorithm 32: six carriers, OP6's loop */
    press(B_ENV); frames(32); ui.force = 1; frame(); ppm("fm6-algo-32");
    release(B_ENV);
    ed[FV_ALG] = 4; press(B_ENV); frames(32); ui.force = 1; frame(); ppm("fm6-algo-5");
    release(B_ENV);
    ed[FV_ALG] = (int16_t)a;
    view_set(0); frames(2);
    check(fm6ui.mode == FMV_PAGE, "FM6 VIEW PAGE: the one-page editor as before");
    ui.force = 1; frame(); ppm("fm6-page-op2");
    press(B_ENV); frames(32);
    check(fm6ui.algo && fm6ui.mode == FMV_ALGO, "FM6 VIEW PAGE: the long press shows the diagram too");
    release(B_ENV); frames(2);
    check(fm6ui.mode == FMV_PAGE, "... let go: the one page back");
    view_set(1);
    set_engine_of(&trk[0], trk_def_engine(0)); apply_preset_to(&trk[0], trk_def_preset(0));
    trk[0].p[P_VOICE] = V_POLY; go_home(); frames(3);
}

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    {   /* the preset list by kind (ui.c BANK): every factory preset of every engine once, every name found */
        uint32_t e, k, n, hits;
        bank_resolve();
        for (n = 0; n < NBANK_ALL; n++)       /* every entry of an engine built names a preset of it (a set left
                                               * out of a reduced build takes its presets along: SMP_SETS nz 0) */
            if (bank_pi[n] == 0xFF && eng_built(BANK[n].e) &&
                !(BANK[n].e == 4u && smp_preset_gone(BANK[n].name))) {
                printf("ui: BANK %s: no such preset in engine %u\n", BANK[n].name, BANK[n].e);
                fails++;
            }
        for (e = 0; e < NENGINES; e++)
            for (k = 0; k < ENGINES[e]->npresets; k++) {
                if (!preset_playable(ENGINES[e], k))   /* (its sample set is not in this build) */
                    continue;
                for (hits = 0, n = 0; n < NBANK; n++)
                    hits += eng_slot_built(BANK[bank_ix[n]].e) == e && bank_pi[bank_ix[n]] == k;
                if (hits != 1) {
                    printf("ui: preset %s (engine %u) is %u times in BANK\n", ENGINES[e]->presets[k].name, e, hits);
                    fails++;
                }
            }
    }
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], trk_def_engine(i)); apply_preset_to(&trk[i], trk_def_preset(i)); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
#ifdef PHELP_ONLY                                  /* tests/param_help_test.c: the help lines' coverage of this build */
    param_help_tests();
    printf("ui: help %s\n", fails ? "FAILED" : "PASS");
    return fails;
#endif
    boot_splash(); ppm("page-splash");
    ui.menu = 2; ui.force = 1; frame(); ppm("page-about"); ui.menu = 0;
    go_home(); ui.force = 1; frame(); ppm("page-tracks");
    open_family(FAM_ENV); ui.force = 1; ui.hot_col = 1; ui.hot_t = 30; frame(); ppm("page-env");
    open_family(FAM_EDIT); ui.force = 1; frame(); ppm("page-edit");
    open_family(FAM_FX); ui.force = 1; frame(); ppm("page-fx");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("page-step");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-global");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-master");
#if FELUCCA_SNAPSHOTS
    {   /* SAVE > SNAPSHOT (tests/snap_ui_stub.h): the slots, KNOB 1 to B, SAVE armed then done, the message green */
        uint32_t i, sp = NPAGES;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_SNAP)
                sp = i;
        check(sp < NPAGES && PAGES[sp].fam == FAM_SAVE && sp > 0 && PAGES[sp - 1].graph == GR_SLOTS, "SNAPSHOT: in the SAVE family, after PROJECT");
        ui.page = (uint8_t)sp; ui.sslot = 0; ui.force = 1; frames(2); ppm("page-snapshot");
        encs[panel.enc[EN_K1]] = 20; frames(2);
        check(ui.sslot == FELUCCA_SNAPSHOTS, "SNAPSHOT: KNOB 1 to the last row: B (BEFORE LOAD)");
        ui.force = 1; frame(); ppm("page-snapshot-b");
        encs[panel.enc[EN_K1]] = -20; frames(2);
        encs[panel.enc[EN_K4]] = 1; frames(2);
        check(!sn_ops[2] && !strcmp(ui.msg, "AGAIN: SAVE"), "SNAPSHOT: one SAVE detent arms (AGAIN: SAVE)");
        encs[panel.enc[EN_K4]] = 1; frames(2);
        check(sn_ops[2] == 1u && ui.msg_st == 1u, "SNAPSHOT: a second one saves, the message in green");
        ui.force = 1; frame(); ppm("page-snapshot-saved");
        encs[panel.enc[EN_K2]] = 1; frames(2); encs[panel.enc[EN_K2]] = 1; frames(2);
        check(sn_ops[0] == 1u, "SNAPSHOT: LOAD armed then done");
    }
#endif
    open_family(FAM_SCL); ui.force = 1; frame(); ppm("page-scale");

    {   /* GLO > SYSTEM, the MIDI column: RX for 250 ms after USB-MIDI came in (after Melodee 670193c) */
        char v[12];
        const char *u, *l;
        frame();
        l = midi_status(v, &u);
        check(!strcmp(l, "USB") && !u[0], "MIDI column: USB, no RX before data");
        usb.rx_pkts++;
        frame();
        l = midi_status(v, &u);
        check(!strcmp(l, "USB") && !strcmp(u, "RX"), "MIDI column: RX once a packet came in");
        frames(20);
        midi_status(v, &u);
        check(!u[0], "MIDI column: RX gone 250 ms later");
    }

    /* ---- taps open pages, holds are layers */
    go_home(); ui.force = 1; frame();
    { uint8_t was = song.sel; song.sel = 0; check(keys_guide() == 0u, "synth track, no layer: no landmarks (a piano)"); song.sel = was; }
    tap(B_SEQ); check(cur_fam() == FAM_SEQ, "SEQ tapped: the SEQ pages");
    go_home(); frame();
    tap(B_FX); check(cur_fam() == FAM_FX, "FX tapped: the FX pages");
    go_home(); frame();
    press(B_FX); frames(12); check(ui.layer == LY_FX && punch.hold, "FX held: the punch layer shows");
    check(keys_guide() == (1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12)),
          "FX held: keys 1, 5, 9, 13 lit dim (the rows of the grid)");
    ppm("layer-punch");
    fm1_in.notes = 1u << 4; frame(); check(punch.req == 2, "FX + the 3rd white key: punch effect 3");
    ppm("layer-punch-on");
#if FELUCCA_PUNCH_LATCH
    fm1_in.notes = 0; frame(); check(punch.req == 2, "key up (PUNCH LATCH): the effect stays");
    fm1_in.notes = 1u << 4; frame(); fm1_in.notes = 0; frame(); check(punch.req == -1, "the same key again: the mix comes back");
#else
    fm1_in.notes = 0; frame(); check(punch.req == -1, "key up: the mix comes back");
#endif
    encs[panel.enc[EN_K2]] = 10; frame(); check(song.g[G_DUST] > 0, "FX + KNOB 2: DUST");
    encs[panel.enc[EN_K1]] = -10; frame(); check(song.g[G_FILT] < 0, "FX + KNOB 1: the filter (low-pass)");
    release(B_FX); check(cur_page()->scope == SC_TRK && ui.layer == LY_PLAY, "FX used then let go: no FX page");
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- knob turns as a layer is let go (Felucca 1.0.2 #39): they must not reach the page underneath */
#if FELUCCA_LAYER_QUIET
    {
        int16_t mode0, p0[P_COUNT], g0[G_COUNT];
        uint32_t k, moved;
        go_home(); frames(2);
        mode0 = TSEL->p[P_AMODE];
        press(B_ARP); frames(2);
        encs[panel.enc[EN_K1]] = 1;                      /* a detent in the frame ARP is let go */
        release(B_ARP); frames(3);
        printf("ui: #39 ARP let go with KNOB 1 in that frame: page %s, ARP MODE %d -> %d\n",
               cur_fam() == FAM_ARP ? "ARP" : "other", mode0, TSEL->p[P_AMODE]);
        check(TSEL->p[P_AMODE] == mode0 && cur_fam() != FAM_ARP,
              "#39: ARP let go while KNOB 1 turns: no tap, the arpeggiator stays off");
        TSEL->p[P_AMODE] = mode0;
        go_home(); frames(2);
        memcpy(p0, TSEL->p, sizeof p0); memcpy(g0, song.g, sizeof g0);
        press(B_FX); frames(12);
        encs[panel.enc[EN_K2]] = 4; frame();            /* FX + KNOB 2: DUST (the layer's) */
        release(B_FX);
        encs[panel.enc[EN_K1]] = 2; frame();            /* the hand still turning just after (16 ms) */
        encs[panel.enc[EN_K1]] = 1; frame();
        for (moved = 0, k = 0; k < P_COUNT; k++) moved += TSEL->p[k] != p0[k];
        for (k = 0; k < G_COUNT; k++) moved += k != G_DUST && song.g[k] != g0[k];
        printf("ui: #39 FX used, let go, KNOB 1 turned 16 / 32 ms later: %u page values moved\n", moved);
        check(moved == 0, "#39: knob turns just after a layer closes do not edit the page (quiet window)");
        memcpy(TSEL->p, p0, sizeof p0); memcpy(song.g, g0, sizeof g0);
        frames(20);
        encs[panel.enc[EN_K1]] = 1; frame();
        for (moved = 0, k = 0; k < P_COUNT; k++) moved += TSEL->p[k] != p0[k];
        for (k = 0; k < G_COUNT; k++) moved += song.g[k] != g0[k];
        check(moved == 1, "#39: a knob 320 ms after the layer closed edits the page again");
        memcpy(TSEL->p, p0, sizeof p0); memcpy(song.g, g0, sizeof g0);
    }
#endif

    /* ---- a layer locked open: held + HOME tapped; any other button (not PLAY, REC, OCT) lets it go */
    go_home(); frame();
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX && ui.layer == LY_FX && punch.hold, "FX held + HOME: locked open, FX let go");
    check(cur_page()->scope == SC_TRK, "FX + HOME: no FX page, no HOME jump");
    fm1_in.notes = 1u << 4; frame(); check(punch.req == 2, "locked FX + the 3rd white key: punch effect 3 (no hands on FX)");
#if FELUCCA_PUNCH_LATCH
    fm1_in.notes = 0; frame(); fm1_in.notes = 1u << 4; frame(); fm1_in.notes = 0; frame();
#else
    fm1_in.notes = 0; frame();
#endif
    check(punch.req == -1, "locked FX, key up (PUNCH LATCH: pressed again): the mix comes back");
    encs[panel.enc[EN_K2]] = 6; frame(); check(song.g[G_DUST] > 0, "locked FX + KNOB 2: DUST");
    ui.force = 1; frame(); ppm("layer-locked");
    { uint8_t was = song.playing; tap(B_PLAY); frames(2);
      check(ly_lock == LY_FX && song.playing != was, "locked: PLAY plays and keeps the lock"); tap(B_PLAY); frames(2); }
    tap(B_ENV); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && !punch.hold && cur_page()->scope == SC_TRK,
          "locked, ENV pressed: unlocked, and only that (no ENV page)");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_HOME); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_page()->scope == SC_TRK, "locked, HOME tapped: unlocked");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    tap(B_FX); frames(2);
    check(ly_lock == LY_PLAY && ui.layer == LY_PLAY && cur_fam() != FAM_FX, "locked, FX tapped: unlocked, no FX page");
    go_home(); frame();
    press(B_SEQ); frames(3); tap(B_HOME); release(B_SEQ); frames(3);
    press(B_FX); frames(12);
    check(ly_lock == LY_PLAY && ui.layer == LY_FX, "locked SEQ, FX held: unlocked, the FX layer");
    release(B_FX); frames(2); check(ui.layer == LY_PLAY, "and FX let go: back to playing");
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    press(B_HOME); frames(50); release(B_HOME); frames(2);
    check(ui.menu && ly_lock == LY_PLAY, "locked, HOME held: the menu, unlocked");
    ui.menu = 0; ui.force = 1; frames(2);
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- SEQ layer: drum steps on the white keys */
    song.sel = TRK_DRUM; go_home(); frame();
    key(4);                                          /* A3: the snare, played: the layer's sound */
    check(pen_lane == 2, "a drum key played: the SEQ layer's sound (snare)");
    press(B_SEQ); frames(10);
    key(0); key(7); key(14);                          /* steps 1, 5, 9 */
    check(dstep_has(&TDRUM->dstep[0], 2) && dstep_has(&TDRUM->dstep[4], 2) && dstep_has(&TDRUM->dstep[8], 2) &&
          !dstep_has(&TDRUM->dstep[1], 2), "SEQ + white keys 1, 5, 9: snare steps");
    ppm("layer-steps");
    fm1_in.notes = 1u << 7; frame();                  /* step 5 held + KNOB 2 / 3: level, ratchet */
    encs[panel.enc[EN_K2]] = 1; frame();
    encs[panel.enc[EN_K3]] = 2; frame();
    fm1_in.notes = 0; frame();
    check(dstep_lvl(&TDRUM->dstep[4], 2) == LV_HARD && dstep_rat(&TDRUM->dstep[4], 2) == 2u,
          "step 5 held + KNOB 2 / 3: hard, x3");
    key(0); check(!dstep_has(&TDRUM->dstep[0], 2), "step 1 again: off");
    release(B_SEQ); check(ui.layer == LY_PLAY && !on_drum_page(), "SEQ used then let go: no page change");

    /* ---- EDIT: undo / redo, erase, length */
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(!dstep_has(&TDRUM->dstep[4], 2) && !dstep_has(&TDRUM->dstep[8], 2), "EDIT + OCT-: undo (the SEQ hold's steps gone)");
#if FELUCCA_UNDO_HISTORY
    {   /* the history's message: levels applied of all, the track (undo.c) */
        uint32_t n, m, tk;
        char want[32];
        undo_status(&n, &m, &tk);
        snprintf(want, sizeof want, "UNDO %u/%u DRUMS", n, m);
        check(!strcmp(ui.msg, want) && m >= 1u && n < m, "EDIT + OCT-: \"UNDO n/m DRUMS\"");
    }
#else
    check(!strcmp(ui.msg, "UNDO"), "EDIT + OCT-: \"UNDO\"");
#endif
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(!dstep_has(&TDRUM->dstep[0], 2) && dstep_lvl(&TDRUM->dstep[4], 2) == LV_HARD, "EDIT + OCT+: redo (back, as left)");
    ppm("layer-erase");
    key(4);                                           /* stopped: every snare goes */
    check(!dstep_has(&TDRUM->dstep[4], 2) && !dstep_has(&TDRUM->dstep[8], 2), "EDIT + snare (stopped): every snare erased");
    encs[panel.enc[EN_K2]] = 1; frame();
    check(TDRUM->p[P_SLEN] == 32, "EDIT + KNOB 2: length x2");
    release(B_EDIT);
    edges_btn |= BT(B_EDIT); fm1_in.buttons |= BT(B_EDIT); frame();      /* undo the length too */
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(TDRUM->p[P_SLEN] == 16, "EDIT + OCT-: the length back to 16");

    /* ---- SEQ layer, a synth step held + KNOB 4: its length as TIE steps (after Melodee 0dbe626) */
    song.sel = 0; go_home(); frame();
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    press(B_SEQ); frames(10);
    key(0); key(14);                                  /* steps 1 and 9 */
    check(step_on(&trk[0].step[0]) && step_on(&trk[0].step[8]), "SEQ + keys 1, 9: two synth steps");
    fm1_in.notes = 1u << 0; frame();                  /* step 1 held */
    encs[panel.enc[EN_K4]] = 3; frame();
    check(trk[0].step[1].time == ST_TIE && trk[0].step[3].time == ST_TIE && trk[0].step[4].time == ST_REST,
          "step 1 held + KNOB 4 +3: four steps long (3 ties)");
    ppm("layer-steps-length");
    encs[panel.enc[EN_K4]] = 10; frame();
    check(trk[0].step[7].time == ST_TIE && step_on(&trk[0].step[8]), "KNOB 4 +10: up to the next note, not over it");
    encs[panel.enc[EN_K4]] = -6; frame();
    check(trk[0].step[1].time == ST_TIE && trk[0].step[2].time == ST_REST && trk[0].step[7].time == ST_REST,
          "KNOB 4 -6: two steps, its own ties cleared");
    fm1_in.notes = 0; frame();
    check(step_on(&trk[0].step[0]), "step 1 let go after a length edit: kept");
    release(B_SEQ);
    steps_clear(&trk[0]);

    /* ---- ARP layer: a roll, rate knob */
    press(B_ARP); frames(10);
    encs[panel.enc[EN_K1]] = 1; frame();
    check(song.g[G_ROLL] == 2, "ARP + KNOB 1: the roll rate (1/32)");
    fm1_in.notes = 1u << 7; frames(3); check(roll[0].on, "ARP + a key: it rolls");
    ppm("layer-roll");
    fm1_in.notes = 0; frame(); check(!roll[0].on, "key up: the roll ends");
    release(B_ARP);

    /* ---- SCL: the key of the song; GLO: mute, solo, tap */
    song.sel = 0; go_home(); frame();
    press(B_SCL); frames(10);
    key(9);                                           /* D4 */
    check(trk[0].p[P_ROOT] == 2 && trk[1].p[P_ROOT] == 2 && trk[2].p[P_ROOT] == 2, "SCL + D: every part in D");
    encs[panel.enc[EN_K1]] = 2; frame();
    check(trk[0].p[P_CHORD] == 2, "SCL + KNOB 1: chords (7TH) on the track");
    ppm("layer-key");
    release(B_SCL);
    press(B_GLO); frames(10);
    key(0); check(trk[0].p[P_MUTE] == 1, "GLO + key 1: track 1 muted");
    key(9); check(song.solo == 2u, "GLO + key 6: track 2 soloed");
    ppm("layer-mix");
    for (i = 0; i < 4u; i++) { fm1_in.notes = 1u << 26; frame(); fm1_in.notes = 0; frames(36); }   /* ~0.6 s apart */
    check(song.g[G_BPM] >= 95 && song.g[G_BPM] <= 105, "GLO + the last key, tapped at ~0.6 s: ~100 BPM");
    key(0); key(9);
    check(!trk[0].p[P_MUTE] && !song.solo, "again: unmuted, no solo");
    key(key_of_white(9));
    check(trk[1].p[P_FXOFF] == 1 && fx_on(&trk[0]) && !fx_on(&trk[1]), "GLO + key 10: track 2 FX off (bypass)");
    check((keys_lit() >> key_of_white(9) & 1u) == 0u && (keys_lit() >> key_of_white(8) & 1u), "GLO: keys 9..12 lit = FX on");
    ppm("layer-mix-fx");
    key(key_of_white(9));
    check(trk[1].p[P_FXOFF] == 0, "again: FX on");
    release(B_GLO);
    trk[0].p[P_CHORD] = 0;

    /* ---- VIEW ALL (the default): a family at once; the knobs edit the lit page; VIEW PAGE: one page */
    song.sel = 0; go_home(); frame();
    check(settings.view == 1u && GP[G_VIEW].def == 1, "VIEW: ALL by default");
    tap(B_EDIT); frames(2);
    check(ov_on() && cur_page()->fam == FAM_EDIT, "EDIT: the overview");
    {
        int16_t e0 = trk[0].p[P_E1];
        encs[panel.enc[EN_K2]] = 1; frames(2);
        check(trk[0].p[P_E1] == e0 + 1, "overview: KNOB 2 edits the lit page (EDIT 1)");
    }
    tap(B_EDIT); tap(B_EDIT); frames(2);
#if FELUCCA_ANALOG2
    {   /* an ANALOG track: ANALOG 2's OSC 2, SWARM, FLT 2 after EDIT 2; the overview's window follows the
         * lit page; another engine's track steps past them */
        uint8_t idx[OV_ROWS];
        uint32_t act, n;
        check(cur_page()->id[0] == P_A2WAVE && ov_on(), "ANALOG 2: EDIT twice more: OSC 2 lit");
        ui.force = 1; frame(); ppm("overview-edit-osc2");
        tap(B_EDIT); frames(2);
        check(cur_page()->id[0] == P_A2SWRM && cur_page()->id[3] == P_A2ESDT, "ANALOG 2: SWARM: SWARM SDTN DRFT, ENV2's amount on the spread");
        trk[0].p[P_A2SWRM] = 3; trk[0].p[P_A2ESDT] = -20; ui.force = 1; frame(); ppm("overview-edit-swarm");
        view_set(0); ui.force = 1; frame(); ppm("page-swarm"); view_set(1);
        trk[0].p[P_A2SWRM] = 0; trk[0].p[P_A2ESDT] = 0;
        tap(B_EDIT); frames(2);
        check(cur_page()->id[0] == P_A2FTYP && cur_page()->id[1] == 0xFFu, "ANALOG 2: EDIT once more: FLT 2 (FTYP) lit");
        ui.force = 1; frame(); ppm("overview-edit-flt2");
        {   /* PAGEs of 4 x 4: the rows EDIT 1, EDIT 2, OSC 2, SWARM | FLT 2, VOICE, VOICE 2 (ENV2: the ENV family) */
            uint32_t pg, npg;
            char fl[24];
            n = ov_rows(idx, &act, &pg, &npg);
            check(n == 3u && act == 0u && pg == 1u && npg == 2u && PAGES[idx[0]].id[0] == P_A2FTYP &&
                  PAGES[idx[1]].id[0] == P_VOICE, "VIEW ALL: FLT 2 (row 5): PAGE 2/2 at once, FLT 2 its first row, lit");
            ov_foot_label(fl);
            check(str_eq(fl, "PAGE 2/2"), "VIEW ALL: the footer says PAGE 2/2");
            ppm("overview-edit-page2");
            view_set(0); ui.force = 1; frame(); ppm("page-flt2"); view_set(1);
            {   /* ENV on an ANALOG track: ENV1, ENV1 DEST, ENV2, ENV2 DEST: one PAGE of 4 x 4 */
                int16_t s0 = trk[0].p[P_A2ESUS], r0 = trk[0].p[P_A2EREL];
                uint32_t k;
                open_family(FAM_ENV); frames(2);
                while (cur_page()->id[0] != P_ATK)
                    tap(B_ENV);
                frames(2);
                n = ov_rows(idx, &act, &pg, &npg);
                check(n == 4u && npg == 1u && PAGES[idx[0]].id[0] == P_ATK && PAGES[idx[1]].id[0] == P_ED_FLT &&
                      PAGES[idx[2]].graph == GR_ENV2 && PAGES[idx[3]].id[0] == P_A2FENV,
                      "ANALOG track, ENV: ENV1, ENV1 DEST, ENV2, ENV2 DEST (one PAGE)");
                ov_foot_label(fl);
                check(str_eq(fl, "PAGE 1/1"), "ENV, VIEW ALL: PAGE 1/1");
                tap(B_ENV); tap(B_ENV); frames(2);
                check(cur_page()->graph == GR_ENV2 && cur_page()->id[2] == P_A2ESUS, "ENV twice more: ENV2 (ATK2 DEC2 SUS2 REL2)");
                encs[panel.enc[EN_K3]] = 40; encs[panel.enc[EN_K4]] = 0; frames(2);
                check(trk[0].p[P_A2ESUS] > s0, "ENV2 lit: KNOB 3 SUS2");
                trk[0].p[P_A2FATK] = 20; trk[0].p[P_A2FDEC] = 70; trk[0].p[P_A2EREL] = 90;
                tap(B_ENV); frames(2);
                check(cur_page()->id[0] == P_A2FENV && cur_page()->id[1] == P_A2EPIT && cur_page()->id[2] == P_A2ESHP &&
                      cur_page()->id[3] == P_A2EOS2, "ENV once more: ENV2 DEST (FLT PIT SHP OSC2)");
                for (k = 0; k < 4u; k++) {            /* each knob its own amount, the others still */
                    int16_t was[4];
                    uint32_t j;
                    for (j = 0; j < 4u; j++)
                        was[j] = trk[0].p[cur_page()->id[j]];
                    encs[panel.enc[EN_K1 + k]] = (int8_t)(k & 1u ? -3 : 5); frames(2);
                    for (j = 0; j < 4u; j++)
                        check(trk[0].p[cur_page()->id[j]] == (j == k ? was[j] + (k & 1u ? -3 : 5) : was[j]),
                              "ENV2 DEST lit: a knob moves its own amount only");
                }
                trk[0].p[P_A2FENV] = 40; trk[0].p[P_A2EPIT] = -12; trk[0].p[P_A2ESHP] = 25; trk[0].p[P_A2EOS2] = 63;
                ui.force = 1; frame(); ppm("overview-env-a2");
                view_set(0); ui.force = 1; frame(); ppm("page-env2dest");
                check(!ov_on() && cur_page()->id[0] == P_A2FENV, "VIEW PAGE: ENV2 DEST on one page");
                tap(B_ENV); tap(B_ENV); tap(B_ENV); frames(2);
                check(cur_page()->graph == GR_ENV2, "VIEW PAGE: round to ENV2");
                ui.force = 1; frame(); ppm("page-env2");
                trk[0].p[P_A2EREL] = 0; ui.force = 1; frame(); ppm("page-env2-reldec");
                tap(B_ENV); tap(B_ENV); frames(2);
                check(cur_page()->id[0] == P_ATK, "ANALOG track, ENV: round to ENV1");
                ui.force = 1; frame(); ppm("page-env1");
                tap(B_ENV); tap(B_ENV); frames(2);
                song.sel = 1; frames(3);                /* on ENV2 and the track is no ANALOG: its ENV */
                check(cur_page()->fam == FAM_ENV && cur_page()->id[0] == P_ATK, "ENV2 and a DIGITAL track: ENV (its family stays)");
                n = ov_rows(idx, &act, &pg, &npg);
                check(n == 2u, "DIGITAL track: ENV, ENV DEST only");
                song.sel = 0; frames(2);
                view_set(1); ui.force = 1; frame(); ppm("overview-env1");
                trk[0].p[P_A2FENV] = trk[0].p[P_A2EPIT] = trk[0].p[P_A2ESHP] = trk[0].p[P_A2EOS2] = 0;
                trk[0].p[P_A2ESUS] = s0; trk[0].p[P_A2EREL] = r0; trk[0].p[P_A2FATK] = 0; trk[0].p[P_A2FDEC] = 64;
                open_family(FAM_EDIT); frames(2);
                while (cur_page()->id[0] != P_VOICE)
                    tap(B_EDIT);
                frames(2);
            }
            n = ov_rows(idx, &act, &pg, &npg);
            check(n == 3u && act == 1u && pg == 1u && PAGES[idx[act]].id[0] == P_VOICE,
                  "ANALOG 2: VOICE the second row of PAGE 2 (FLT 2, VOICE, VOICE 2)");
            tap(B_EDIT); tap(B_EDIT); frames(2);
            n = ov_rows(idx, &act, &pg, &npg);
            check(n == 4u && act == 0u && pg == 0u && PAGES[idx[0]].id[0] == P_E0,
                  "past the last row: PAGE 1/2 again, EDIT 1 lit");
            ui.force = 1; frame(); ppm("overview-edit-page1");
            while (cur_page()->id[0] != P_VOICE)
                tap(B_EDIT);
            frames(2);
        }
        song.sel = 1; ui.page = (uint8_t)page_first(FAM_EDIT); frames(2);   /* DIGITAL: EDIT 1, 2, VOICE, VOICE 2 */
        tap(B_EDIT); tap(B_EDIT); frames(2);
        check(cur_page()->id[0] == P_VOICE, "another engine: EDIT 2 -> VOICE (no ANALOG 2 pages)");
        tap(B_EDIT); tap(B_EDIT); frames(2);
        n = ov_pages(idx, &act);
        check(cur_page()->id[0] == P_E0 && n == 4u, "another engine: four EDIT pages, round to EDIT 1");
        song.sel = 0; frames(2);
        while (cur_page()->id[0] != P_VOICE)
            tap(B_EDIT);
        frames(2);
    }
#endif
    check(cur_page()->id[0] == P_VOICE && ov_on(), "EDIT twice more: VOICE lit");
    ui.force = 1; frame(); ppm("overview-edit");
    tap(B_LFO); frames(2); ui.force = 1; frame(); ppm("overview-lfo");
    tap(B_ENV); frames(2); ui.force = 1; frame(); ppm("overview-env");
    {
        uint8_t idx[OV_ROWS];
        uint32_t act, n = ov_pages(idx, &act), k, dx = 0;
        for (k = 0; k < n; k++)
            dx |= PAGES[idx[k]].scope == SC_FM6K;
        check(ov_on() && n == (FELUCCA_ANALOG2 ? 4u : 2u) && !dx && !on_fm6k_page(),
              "ENV overview (not FM6): ENV, ENV DEST (ANALOG: ENV2, ENV2 DEST), no FM6 editor row");
    }
    {   /* ARP: two rows over the arp's bar; the family button steps the rows, round */
        uint8_t idx[OV_ROWS];
        uint32_t act, n, a0 = trk[0].p[P_AMODE], a1 = trk[0].p[P_AOCT], a2 = trk[0].p[P_ASWING], a3 = trk[0].p[P_APROB];
        open_family(FAM_ARP); frames(2);
        n = ov_pages(idx, &act);
        check(ov_on() && n == 2u && act == 0u && PAGES[idx[0]].graph == GR_ARP, "ARP: the overview, ARP and ARP 2, ARP lit");
        ui.force = 1; frame(); ppm("overview-arp-off");
        trk[0].p[P_AMODE] = 3; trk[0].p[P_AOCT] = 2; trk[0].p[P_ASWING] = 50; trk[0].p[P_APROB] = 90;
        frames(2); ui.force = 1; frame(); ppm("overview-arp");
        {
            uint32_t px, lit = 0, c = swap16(TE_COL[0]);
            for (px = 112u * 240u; px < 196u * 240u; px++)
                lit += screen[px] == c;
            check(lit > 100u, "ARP: the arp's bar under the rows (track colour)");
        }
        open_family(FAM_ARP); frames(2);
        n = ov_pages(idx, &act);
        check(n == 2u && act == 1u && cur_page()->id[0] == P_ASWING, "ARP tapped again: ARP 2 lit");
        ui.force = 1; frame(); ppm("overview-arp2");
        encs[panel.enc[EN_K1]] = 5; frames(2);
        check(trk[0].p[P_ASWING] == 55, "ARP 2 lit: KNOB 1 edits SWG");
        view_set(0); open_family(FAM_ARP); frames(2);
        ui.force = 1; frame(); ppm("page-arp");
        check(!ov_on() && cur_page()->graph == GR_ARP, "VIEW PAGE: ARP on one page, with its graph");
        view_set(1);
        trk[0].p[P_AMODE] = (int16_t)a0; trk[0].p[P_AOCT] = (int16_t)a1; trk[0].p[P_ASWING] = (int16_t)a2;
        trk[0].p[P_APROB] = (int16_t)a3;
        tap(B_ENV); frames(2);
    }
    /* an FM6 track: ENV opens the operator editor (its own screen, not the overview); back on track 1: ENV pages */
    {
        uint32_t e1 = trk[1].eng_req, p1 = trk[1].preset;
        set_engine_of(&trk[1], ENG_IX_FM6); song.sel = 1; frames(3);
        tap(B_ENV); frames(3);
        check(on_fm6k_page() && !ov_on(), "FM6 track, ENV tapped: the FM6 editor, no overview");
        ui.force = 1; frame(); ppm("overview-fm6-env");
        song.sel = 0; frames(3);
        check(!on_fm6k_page() && cur_page()->fam == FAM_ENV && ov_on(), "back on a non-FM6 track: the ENV overview");
        set_engine_of(&trk[1], e1); apply_preset_to(&trk[1], p1); frames(2);
    }
    tap(B_FX); frames(2);
    trk[0].p[P_FXOFF] = 1; ui.force = 1; frame(); ppm("overview-fx-off");
    trk[0].p[P_FXOFF] = 0;
    song.sel = TRK_DRUM; frame();
    check(cur_page()->graph == GR_FX && !ov_on(), "drum track on FX (not its page): the one-page DRUM TRACK");
    tap(B_FX); frames(2);
    {
        uint8_t idx[OV_ROWS];
        uint32_t act, n = ov_pages(idx, &act);
        check(ov_on() && n == 3u + FELUCCA_SPRING && act == 0u && PAGES[idx[0]].graph == GR_SLCR,   /* (+ REVERB) */
              "drum track, FX tapped: SLICER lit in the first row (no empty FX row)");
    }
    ui.force = 1; frame(); ppm("overview-fx-drum");
    song.sel = 0; frame();
    tap(B_GLO); frames(2);
    for (i = 0; i < 4u && cur_page()->id[2] != G_VIEW; i++) { tap(B_GLO); frames(2); }
    check(cur_page()->id[2] == G_VIEW && cur_page()->fam == FAM_GLO, "GLO tapped to SYSTEM: VIEW on KNOB 3");
    ui.force = 1; frame(); ppm("overview-glo");
    encs[panel.enc[EN_K3]] = -1; frames(2);
    check(song.g[G_VIEW] == 0 && settings.view == 0u && !ov_on(), "KNOB 3 left: VIEW PAGE (the setting follows)");
    ppm("page-glo");
    encs[panel.enc[EN_K3]] = 1; frames(2);
    check(settings.view == 1u && ov_on(), "KNOB 3 right: ALL again");
    go_home(); frame();

    /* ---- REC: press arms; held: the ring; to the end: cleared */
    go_home(); frame();
    song.playing = 0; song.rec = 0; rec_wait = 0;
    tap(B_REC); check(rec_wait == 1, "REC tapped (stopped): armed");
    ui.force = 1; frame(); ppm("live-rec-ready");
    tap(B_REC); check(rec_wait == 0, "REC again: cancelled");
    trk[0].step[3].n = 1, trk[0].step[3].note[0] = 60, trk[0].step[3].time = ST_NOTE;
    press(B_REC); frames(50);                         /* 0.8 s: the ring */
    check(ui.hold_kind == 1u && rec_wait == 0, "REC held: the press undone, the clear ring");
    ppm("hold-clear");
    frames(90);                                       /* to the end */
    check(!trk[0].step[3].n && ui.hold_kind == 0, "REC held to the end: track 1 cleared");
    release(B_REC);
    press(B_EDIT); frames(10);
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    release(B_EDIT);
    check(trk[0].step[3].n == 1, "EDIT + OCT-: the cleared track back");
    press(B_REC); frames(60); release(B_REC);         /* let go before the end: nothing */
    check(trk[0].step[3].n == 1 && !rec_wait, "REC let go before the end: nothing cleared");

#if FELUCCA_UNDO_HISTORY
    {   /* a recording pass of several notes with the UI running: one level (undo.c; the UI ends its own sessions only) */
        uint32_t n0, m0, n1, m1, tk;
        song.sel = 0; go_home(); frame();
        steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
        undo_close(); undo_status(&n0, &m0, &tk);
        transport_req = 1; frame(); song.rec = 1u;
        while (trk[0].seq_idx != 2u) frame();
        key(7); frames(3); key(9); frames(3); key(11);
        while (trk[0].seq_idx != 14u) frame();
        song.rec = 0; transport_req = 2; frames(2);
        undo_close(); undo_status(&n1, &m1, &tk);
        if (m1 != n0 + 1u)
            printf("ui:   recording pass: levels %u/%u -> %u/%u\n", n0, m0, n1, m1);
        check(m1 == n0 + 1u && n1 == m1 && step_on(&trk[0].step[2]),
              "three notes in one recording pass, the UI running: one undo level (the redo before it gone)");
    }
#endif
    /* ---- SAVE: tap = the song page; held = the SONG layer (live sections, SONG REC) */
    go_home(); frame();
    song.playing = 0; live_sec = -1; live_req = -1; srec = 0; arrangement_enabled = 0;
    press(B_SAVE); frames(15); check(ui.layer == LY_SONG, "SAVE held: the song layer");
    key(key_of_white(4)); check(sec_stores == 0 && sec_armed == 1u, "store over a used A: asks again");
    key(key_of_white(4)); check(sec_stores == 1 && live_sec == 0, "again: the loop stored in A");
    key(key_of_white(6)); check(sec_stores == 2 && live_sec == 2, "an empty C: stored at once");
    key(key_of_white(1)); check(sec_loads == 1 && live_sec == 1, "stopped: B loaded as the loop");
    key(key_of_white(3)); check(sec_loads == 1, "an empty D: not played");
    song.playing = 1; clk_beat = 1; clk_pos = 0;
    key(key_of_white(0)); check(live_req == 0, "playing: A asked for the next bar");
    live_req = -1; song.playing = 0;
    key(key_of_white(13)); check(srec == 1u && !arrangement_enabled, "SONG REC armed (loop mode)");
    ui.force = 1; frame(); ppm("layer-song");
    key(key_of_white(13)); check(srec == 0u, "SONG REC again: off");
    key(key_of_white(12)); check(arrangement_enabled == 1u, "loop / song: song mode");
    key(key_of_white(12)); check(arrangement_enabled == 0u, "again: loop mode");
    {   /* the overwrite confirmed with SAVE let go and held again in between (or kept held: above) */
        uint32_t n = sec_stores;
        key(key_of_white(5)); check(sec_stores == n && sec_armed == 2u, "store over a used B: asks again");
        release(B_SAVE); frames(10); press(B_SAVE); frames(15);
        check(!on_song_page() && ui.layer == LY_SONG, "SAVE let go and held again: the song layer, no tap");
        key(key_of_white(5)); check(sec_stores == n + 1u && live_sec == 1, "B again (SAVE held anew): stored");
        key(key_of_white(5)); check(sec_stores == n + 1u && sec_armed == 2u, "B once more: asks again");
        frames(3000u / 16u + 2u);
        key(key_of_white(5)); check(sec_stores == n + 1u && sec_armed == 2u, "B after 3 s: asks again, no store");
        key(key_of_white(5)); check(sec_stores == n + 2u, "and again within 3 s: stored");
    }
    release(B_SAVE);
    check(!on_song_page() && saves == 0, "SAVE held and let go: no song page, no save");
    tap(B_SAVE); check(on_song_page(), "SAVE tapped on TRACKS: the song page");
    ui.force = 1; frame(); ppm("page-song");

    /* ---- the drum screen */
    song.sel = TRK_DRUM; studio_open(SC_DRUM); ui.force = 1; frame();
    check(on_drum_page(), "the drum screen");
    encs[panel.enc[EN_K3]] = 1; frame(); check(dstep_has(&TDRUM->dstep[0], 0), "DRUMS: KNOB 3 adds the kick on step 1");
    encs[panel.enc[EN_K4]] = -1; frame(); check(dstep_lvl(&TDRUM->dstep[0], 0) == LV_SOFT, "DRUMS: KNOB 4 softer");
    encs[panel.enc[EN_K1]] = 100; encs[panel.enc[EN_K2]] = 100; frame();
    check(drum_lane == 15 && drum_cursor == 15, "DRUMS: KNOB 1 / 2 bounded (16 sounds, 16 steps)");
    {   /* a scene to look at */
        static const uint8_t BEAT[16] = {0x11, 0x10, 0x10, 0x10, 0x14, 0x10, 0x10, 0x21, 0x11, 0x10, 0x01, 0x10, 0x1C, 0x10, 0x10, 0x20};
        uint32_t j;
        for (j = 0; j < 16u; j++) {
            memset(&TDRUM->dstep[j], 0, sizeof(dstep_t));
            if (BEAT[j] & 1u) dstep_set(&TDRUM->dstep[j], 0, LV_HARD, 0);
            if (BEAT[j] & 4u) dstep_set(&TDRUM->dstep[j], 2, LV_NORM, 0);
            if (BEAT[j] & 8u) dstep_set(&TDRUM->dstep[j], 3, LV_GHOST, 0);
            if (BEAT[j] & 0x10u) dstep_set(&TDRUM->dstep[j], 4, j % 4u ? LV_SOFT : LV_NORM, j == 14u ? 2u : 0u);
            if (BEAT[j] & 0x20u) dstep_set(&TDRUM->dstep[j], 5, LV_NORM, 0);
        }
        for (j = 0; j < 16u; j += 3u) { trk[0].step[j].n = 1; trk[0].step[j].note[0] = 36; trk[0].step[j].time = ST_NOTE; }
        for (j = 0; j < 16u; j += 4u) { trk[1].step[j].n = 3; trk[1].step[j].time = ST_NOTE; }
        drum_lane = 4; drum_cursor = 6; drum_page = 0; ui.force = 1; ui.msg_t = 0;
        drums.hits = 1u | 1u << 4; frame(); ppm("live-grid");
        drum_page = 1; ui.force = 1; drums.hits = 1u | 1u << 4; frame(); ppm("live-kit");
        drum_page = 0; song.sel = 1; go_home(); ui.force = 1;
        transport_req = 1; frames(30); song.rec = 2u; frame(); ui.force = 1; frame(); ppm("live-tracks");
        song.rec = 0; transport_req = 2; frames(2);
        song.sel = TRK_DRUM;
    }
    {   /* the free take screens */
        static track_t keep[NTRK];
        memcpy(keep, trk, sizeof keep);
        for (i = 0; i < NTRK; i++) steps_clear(&trk[i]);
        rec_wait = 1; ui.force = 1; frame(); ppm("live-rec-free");
        rec_wait = 0; ft_on = 1; ft_t = (uint32_t)(5.4 * FS / CTL); ui.force = 1; ui_draw(); ppm("live-free-take");
        ft_on = 0; ft_t = 0; memcpy(trk, keep, sizeof keep);
        rec_wait = 0; frame();
    }
    for (i = 0; i < DRUM_KITS; i++) { TDRUM->p[P_E0] = (int16_t)i; ui.force = 1; drum_page = 1; frame(); }
    drum_page = 0;

#if DL_UI
    drum_sound_tests();
#endif
    fm6_editor_tests();
    fm6_engine_tests();
    backport_ui_tests();
    bp23_ui_tests();
    fel102_ui_tests();
    fm6_view_tests();
    param_help_tests();
    song.sel = 0; go_home(); ui.force = 1;
    fuzz(20000, 777);
    printf("ui: %s\n", fails ? "FAILED" : "pages, layers (punch, steps, erase, roll, key, mix), layer lock, song layer, REC hold, drums, REC, FM6 editor, 20000-frame fuzz PASS");
    return fails;
}
