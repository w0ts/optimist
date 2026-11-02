/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG and the pattern page (the user, 2026-11-02), included by ui_optimist_test.c after ui_optimist_patnew.h:
 *   the LEDs    SONG's scenes (and its other rows): SEQ flashes, HOME is no longer lit full (it is half lit: it goes HOME, as everywhere);
 *               the pattern page (SHIFT2 + LFO): LFO flashes, SEQ and HOME not full
 *   SHIFT2 + a page button   works from the pattern page (locked open) and from SONG's scenes as from any page: the page is left and
 *               the button does its job (a layer locked open, SEQ: SONG's scenes, ENV: its tap, the SOUND page) */
#if FELUCCA_PATTERNS
static void sn_reset(void)
{
    reset_ui();
    lay_unlock();
    lpk.on = lpk.fine = 0;
    song.sel = 0;
    song.playing = 0;
    frames(2);
}
static void sn_leds(uint32_t b, uint32_t *on, uint32_t *off, uint32_t *dim)   /* frames of a flash period: the LED of b full / dark / dim */
{
    uint32_t i, v;
    *on = *off = *dim = 0;
    for (i = 0; i < 60u; i++) {
        frames(1);
        v = led_lvl(b);
        if (v == 2u)
            (*on)++;
        else if (v == 1u)
            (*dim)++;
        else
            (*off)++;
    }
}
static void sn_open_pat(void)                           /* the pattern page locked open, SHIFT2 let go */
{
    sn_reset();
    hold2(B_OCTUP, B_LFO);
    frames(3);
}
static void songnav_ui_tests(void)
{
    static const uint32_t BT_[] = {B_GLO, B_EDIT, B_FX, B_SCL, B_ENV, B_ARP, B_LFO, B_SEQ};
    static const char *const NM[] = {"GLO", "EDIT", "FX", "SCL", "ENV", "ARP", "LFO", "SEQ"};
    uint32_t i, on, off, dim, pg;
    char w[160];
    /* the LEDs */
    sn_reset();
    op_enter(SCR_SONG);
    ui.row[SCR_SONG] = 0;
    sn_leds(B_SEQ, &on, &off, &dim);
    check(on > 5u && off + dim > 5u, "songnav: SONG's scenes: the SEQ LED flashes");
    sn_leds(B_HOME, &on, &off, &dim);
    check(on == 0u && dim == 60u, "... HOME is only half lit (not full)");
    sn_open_pat();
    check(lay.lock == LY_PAT && ui.scr == SCR_SONG, "songnav: SHIFT2 + LFO: the pattern page locked open");
    sn_leds(B_LFO, &on, &off, &dim);
    check(on > 5u && off + dim > 5u, "... LFO flashes (as before)");
    sn_leds(B_SEQ, &on, &off, &dim);
    check(on == 0u, "... SEQ is not lit full there, nor HOME");
    sn_leds(B_HOME, &on, &off, &dim);
    check(on == 0u, "... (HOME)");
    /* SHIFT2 + a button, the pattern page locked open, SHIFT2 still held */
    for (pg = 0; pg < 3u; pg++)
        for (i = 0; i < sizeof BT_ / sizeof BT_[0]; i++) {
            uint32_t b = BT_[i], l = lay_btn_layer(b);
            if (pg == 0u)
                sn_open_pat();                          /* the pattern page, locked */
            else if (pg == 1u) {
                sn_reset();                             /* SONG's scenes */
                op_enter(SCR_SONG);
                ui.row[SCR_SONG] = 0;
                frames(3);
            } else {
                sn_reset();                             /* SONG's scenes with the FX layer locked open */
                op_enter(SCR_SONG);
                ui.row[SCR_SONG] = 0;
                hold2(B_OCTUP, B_FX);
                frames(3);
            }
            press(B_OCTUP);
            press(b);
            frames(2);
            snprintf(w, sizeof w, "songnav: %s, SHIFT2 + %s%s", pg == 0u ? "the pattern page" : pg == 1u ? "SONG's scenes" : "SONG, FX locked",
                     NM[i], b == B_LFO ? ": the pattern page" : b == B_SEQ ? ": SONG's scenes" : l != LY_PLAY ? ": its layer locked open" : ": its page");
            if (b == B_LFO)
                check(lay.lock == LY_PAT || pg == 1u, w);
            else if (b == B_SEQ)
                check(ui.scr == SCR_SONG && lay.lock == LY_PLAY && !song_on_pat_row() && !lpk.on, w);
            else if (l != LY_PLAY)
                check(lay.lock == l && lay.shown == l && !lpk.on, w);
            else {
                release(b);
                release(B_OCTUP);
                frames(3);
                check(lay.lock == LY_PLAY && ui.scr == SCR_SOUND, w);   /* (ENV off FM6: its tap, the SOUND page, a locked page let go) */
                continue;
            }
            release(b);
            release(B_OCTUP);
            frames(2);
        }
    /* what was there is kept: HOME on the locked pattern page, SHIFT2 held, still leaves it as before; SAVE + SHIFT2 stays the store */
    sn_open_pat();
    press(B_OCTUP);
    press(B_HOME);
    release(B_HOME);
    release(B_OCTUP);
    frames(3);
    check(lay.lock == LY_PLAY, "songnav: SHIFT2 + HOME on the pattern page: the page let go (as any HOME)");
    sn_open_pat();
    tap(B_GLO);
    check(lay.lock == LY_PLAY, "songnav: GLO alone on the pattern page: the page let go, as before");
}
#endif
