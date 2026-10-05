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
 *   DX7     ENV on a DX7 track: the operator editor (black keys, OCT+ + the last one = OP8, pages, knobs)
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
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
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
static void settings_save(void) {}
#include "../firmware/src/ui_song.c"
#include "../firmware/src/ui_studio.c"
#include "../firmware/src/ui_dx7.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_overview.c"
#include "../firmware/src/ui_layers.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/splash.c"
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

/* the DX7 operator editor (ui_dx7.c): ENV on a DX7 track. The black keys (F#3 = key 1 ..): OP1..OP6 at
 * 1 3 5 8 10 13, PIT 15, GLO 17, MONO 20, POLY 22, the last one (25) OP7; OCT+ held + it: OP8 */
static int dx7_sounding(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return 1;
    return 0;
}
static void dx7_editor_tests(void)
{
    enum { K_OP1 = 1, K_OP2 = 3, K_OP6 = 13, K_PIT = 15, K_GLO = 17, K_MONO = 20, K_POLY = 22, K_OP7 = 25 };
    dx7_part_t *dp = dx7_of(&trk[0]);
    uint32_t a, pg;
    song.sel = 0; song.octave = 0; go_home(); frame();
    set_engine_of(&trk[0], ENG_IX_DX7); frames(3);
    check(trk[0].engine == ENG_IX_DX7 && dp->loaded1 != 0, "DX7 track: its voice loaded");
    tap(B_ENV);
    check(on_dx7_page() && dx7ui.target == 0 && ui.layer == LY_PLAY, "DX7: ENV tapped: the operator editor, OP1");
    ui.force = 1; frame(); ppm("dx7-op1-freq");
    tap(B_ENV); check(dx7ui.sub[0] == 1, "DX7: ENV tapped again: its next page (level)");
    tap(B_ENV); tap(B_ENV); tap(B_ENV); tap(B_ENV);
    check(dx7ui.sub[0] == 0, "DX7: five pages for an operator, then round");
    press(B_ENV); frames(12);
    check(ui.layer == LY_OPS && on_dx7_page() && ly_ops_on, "DX7: ENV held: the ops layer, the editor shows");
    check(keys_lit() & 1u << K_OP1, "DX7: ENV held: OP1's black key lit");
    key(K_OP2); check(dx7ui.target == 1, "DX7: ENV + the OP2 key: operator 2");
    a = dp->v[dx7_op_base(1) + DX7_COARSE];
    encs[panel.enc[EN_K2]] = 1; frames(2);
    check(dp->v[dx7_op_base(1) + DX7_COARSE] == a + 1u && dp->edited && dp->eff_gen == dp->gen,
          "DX7: ENV + KNOB 2: OP2 coarse +1, edited, the notes' voice rebuilt");
    check(dp->eff[dx7_op_base(1) + DX7_COARSE] == a + 1u, "DX7: the edit reaches what the notes play");
    ui.force = 1; frame(); ppm("dx7-op2-held");
    fm1_in.notes = 1u << 7; frames(4);
    check(dx7_sounding(&trk[0]), "DX7: ENV + a white key: it plays (audition while editing)");
    fm1_in.notes = 0; frames(2);
    key(K_OP6); check(dx7ui.target == 5, "DX7: ENV + the OP6 key: operator 6");
    fm1_in.buttons |= BT(B_OCTUP); edges_btn |= BT(B_OCTUP); frame();
    pg = dx7ui.sub[0];
    key(K_OP7); check(dx7ui.target == DX7T_OP8, "DX7: ENV + OCT+ held + the last black key: OP8");
    fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(dx7ui.sub[0] == pg, "DX7: OCT+ let go after it made OP8: no page turn");
    ui.force = 1; frame(); ppm("dx7-op8");
    key(K_OP7); check(dx7ui.target == DX7T_OP7, "DX7: the last black key alone: OP7");
    edges_btn |= BT(B_OCTUP); fm1_in.buttons |= BT(B_OCTUP); frame(); fm1_in.buttons &= ~BT(B_OCTUP); frame();
    check(dx7ui.sub[0] == (pg + 1u) % 5u, "DX7: ENV + OCT+ tapped alone: the next page");
    edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
    check(dx7ui.sub[0] == pg && song.octave == 0, "DX7: ENV + OCT-: the page back, the octave stays");
    key(K_PIT); check(dx7ui.target == DX7T_PIT, "DX7: ENV + PIT: the pitch envelope");
    ui.force = 1; frame(); ppm("dx7-pitch");
    key(K_MONO); check(trk[0].p[P_VOICE] == V_MONO && (keys_lit() >> K_MONO & 1u), "DX7: ENV + MONO: mono, its key lit");
    key(K_POLY); check(trk[0].p[P_VOICE] == V_POLY, "DX7: ENV + POLY: poly");
    key(K_GLO); check(dx7ui.target == DX7T_GLO, "DX7: ENV + GLO: algorithm, LFO, OP 7/8");
    release(B_ENV);
    check(ui.layer == LY_PLAY && on_dx7_page() && dx7ui.sub[2] == 0, "DX7: ENV let go after use: the editor stays, same page");
    a = dp->v[DX7_ALG];
    encs[panel.enc[EN_K1]] = 3; frames(2);
    check(dp->v[DX7_ALG] == (a + 3u > 31u ? 31u : a + 3u) && dp->eff[DX7_ALG] == dp->v[DX7_ALG],
          "DX7: KNOB 1 on the editor (ENV up): the algorithm");
    ui.force = 1; frame(); ppm("dx7-global-algo");
    dx7ui.sub[2] = 3;                                   /* op 7/8: route OP8 > OP7 > OP2 */
    encs[panel.enc[EN_K1]] = 3; frames(2);
    encs[panel.enc[EN_K2]] = 1; frames(2);
    check(dp->v[DX7_EXT] == DX7_EXT_STACKMOD && dp->v[DX7_EXTT] == 1, "DX7: OP 7/8 page: 8>7>op, target OP2");
    ui.force = 1; frame(); ppm("dx7-global-op78");
    fm1_in.notes = 1u << 3; frames(3);
    check(dx7_sounding(&trk[0]), "DX7: ENV up: a black key plays a note again");
    fm1_in.notes = 0; frames(2);
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(dp->edited && trk[0].eng_req == ENG_IX_DX7, "DX7: PRESETS on the editor: no stray preset over the edits");
    dx7ui.target = 0; dx7ui.sub[0] = 2; ui.force = 1; frame(); ppm("dx7-op1-egrate");
    dx7ui.sub[0] = 4; ui.force = 1; frame(); ppm("dx7-op1-keyscale");
    dx7ui.sub[0] = 0;
    {   /* every algorithm, with and without OP7 / OP8, on the screen */
        uint32_t al, ex;
        for (ex = 0; ex < DX7_EXT_COUNT; ex++)
            for (al = 0; al < 32u; al++) {
                dp->v[DX7_ALG] = (uint8_t)al;
                dp->v[DX7_EXT] = (uint8_t)ex;
                dp->v[DX7_EXTT] = (uint8_t)(al % 6u);
                ui.force = 1; frame();
            }
        dp->v[DX7_ALG] = 0; dp->v[DX7_EXT] = 0;
        dp->gen++;
    }
    track_select(1); frames(2);
    check(trk[1].eng_req != ENG_IX_DX7 && !on_dx7_page() && cur_fam() == FAM_ENV,
          "DX7: another track (not DX7): back to its ENV pages");
    tap(B_ENV); check(!on_dx7_page() && cur_fam() == FAM_ENV, "not DX7: ENV tapped: the ENV pages as before");
    press(B_ENV); frames(12);
    check(ui.layer == LY_PLAY && !ly_ops_on, "not DX7: ENV held: no layer");
    release(B_ENV);
    track_select(0); go_home(); frames(2);
    fuzz(6000, 4242);                                   /* random use with track 1 on DX7 */
    check(1, "DX7: 6000 frames of random use on a DX7 track");
    set_engine_of(&trk[0], TRK_DEF[0][0]); apply_preset_to(&trk[0], TRK_DEF[0][1]);
    trk[0].p[P_VOICE] = V_POLY; go_home(); frames(3);
}

int main(int argc, char **argv)
{
    uint32_t i;
    outdir = argc > 1 ? argv[1] : "build/host";
    {   /* the preset list by kind (ui.c BANK): every factory preset of every engine once, every name found */
        uint32_t e, k, n, hits;
        bank_resolve();
        for (n = 0; n < NBANK; n++)
            if (bank_pi[n] == 0xFF) {
                printf("ui: BANK %s: no such preset in engine %u\n", BANK[n].name, BANK[n].e);
                fails++;
            }
        for (e = 0; e < NENGINES; e++)
            for (k = 0; k < ENGINES[e]->npresets; k++) {
                for (hits = 0, n = 0; n < NBANK; n++)
                    hits += BANK[n].e == e && bank_pi[n] == k;
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
    for (i = 0; i < NPART; i++) { set_engine_of(&trk[i], TRK_DEF[i][0]); apply_preset_to(&trk[i], TRK_DEF[i][1]); trk[i].engine = trk[i].eng_req; }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    sloop_splash(); ppm("page-splash");
    ui.menu = 2; ui.force = 1; frame(); ppm("page-about"); ui.menu = 0;
    go_home(); ui.force = 1; frame(); ppm("page-tracks");
    open_family(FAM_ENV); ui.force = 1; ui.hot_col = 1; ui.hot_t = 30; frame(); ppm("page-env");
    open_family(FAM_EDIT); ui.force = 1; frame(); ppm("page-edit");
    open_family(FAM_FX); ui.force = 1; frame(); ppm("page-fx");
    open_family(FAM_SEQ); ui.force = 1; frame(); ppm("page-step");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-global");
    open_family(FAM_GLO); ui.force = 1; frame(); ppm("page-master");
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
    fm1_in.notes = 0; frame(); check(punch.req == -1, "key up: the mix comes back");
    encs[panel.enc[EN_K2]] = 10; frame(); check(song.g[G_DUST] > 0, "FX + KNOB 2: DUST");
    encs[panel.enc[EN_K1]] = -10; frame(); check(song.g[G_FILT] < 0, "FX + KNOB 1: the filter (low-pass)");
    release(B_FX); check(cur_page()->scope == SC_TRK && ui.layer == LY_PLAY, "FX used then let go: no FX page");
    song.g[G_DUST] = 0; song.g[G_FILT] = 0;

    /* ---- a layer locked open: held + HOME tapped; any other button (not PLAY, REC, OCT) lets it go */
    go_home(); frame();
    press(B_FX); frames(3); tap(B_HOME); release(B_FX); frames(3);
    check(ly_lock == LY_FX && ui.layer == LY_FX && punch.hold, "FX held + HOME: locked open, FX let go");
    check(cur_page()->scope == SC_TRK, "FX + HOME: no FX page, no HOME jump");
    fm1_in.notes = 1u << 4; frame(); check(punch.req == 2, "locked FX + the 3rd white key: punch effect 3 (no hands on FX)");
    fm1_in.notes = 0; frame(); check(punch.req == -1, "locked FX, key up: the mix comes back");
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
    check(cur_page()->id[0] == P_VOICE && ov_on(), "EDIT twice more: VOICE lit");
    ui.force = 1; frame(); ppm("overview-edit");
    tap(B_LFO); frames(2); ui.force = 1; frame(); ppm("overview-lfo");
    tap(B_ENV); frames(2); ui.force = 1; frame(); ppm("overview-env");
    {
        uint8_t idx[OV_ROWS];
        uint32_t act, n = ov_pages(idx, &act), k, dx = 0;
        for (k = 0; k < n; k++)
            dx |= PAGES[idx[k]].scope == SC_DX7;
        check(ov_on() && n == 2u && !dx && !on_dx7_page(), "ENV overview (not DX7): ENV, ENV DEST, no DX7 editor row");
    }
    /* a DX7 track: ENV opens the operator editor (its own screen, not the overview); back on track 1: ENV pages */
    {
        uint32_t e1 = trk[1].eng_req, p1 = trk[1].preset;
        set_engine_of(&trk[1], ENG_IX_DX7); song.sel = 1; frames(3);
        tap(B_ENV); frames(3);
        check(on_dx7_page() && !ov_on(), "DX7 track, ENV tapped: the DX7 editor, no overview");
        ui.force = 1; frame(); ppm("overview-dx7-env");
        song.sel = 0; frames(3);
        check(!on_dx7_page() && cur_page()->fam == FAM_ENV && ov_on(), "back on a non-DX7 track: the ENV overview");
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
        check(ov_on() && n == 3u && act == 0u && PAGES[idx[0]].graph == GR_SLCR,
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

    dx7_editor_tests();
    song.sel = 0; go_home(); ui.force = 1;
    fuzz(20000, 777);
    printf("ui: %s\n", fails ? "FAILED" : "pages, layers (punch, steps, erase, roll, key, mix), layer lock, song layer, REC hold, drums, REC, DX7 editor, 20000-frame fuzz PASS");
    return fails;
}
