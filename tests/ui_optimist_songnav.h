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
static uint32_t sn_cell_hash(void)                      /* the marker's box on the screen: a hash of its pixels */
{
    int32_t ph = (GL_END - 22 - 16) / 5;
    int32_t x = STB_X0 + (int32_t)stb.mk_i * STB_CW + (STB_CW - 2 - STB_MK) / 2;
    int32_t y = GL_TOP + 22 + 5 * ph + (GL_END - 22 - 5 * ph - STB_MK) / 2;
    uint32_t h = 2166136261u;
    int32_t i, j;
    for (j = 0; j < STB_MK; j++)
        for (i = 0; i < STB_MK; i++)
            h = (h ^ screen[(y + j) * 240 + x + i]) * 16777619u;
    return h;
}
static int32_t sn_cell_box_y(void)                      /* the marker box's top */
{
    int32_t ph = (GL_END - 22 - 16) / 5;
    return GL_TOP + 22 + 5 * ph + (GL_END - 22 - 5 * ph - STB_MK) / 2;
}
static uint32_t sn_cell_green(void)                     /* ... and its green pixels */
{
    int32_t ph = (GL_END - 22 - 16) / 5;
    int32_t x = STB_X0 + (int32_t)stb.mk_i * STB_CW + (STB_CW - 2 - STB_MK) / 2;
    int32_t y = GL_TOP + 22 + 5 * ph + (GL_END - 22 - 5 * ph - STB_MK) / 2;
    uint32_t n = 0;
    int32_t i, j;
    for (j = 0; j < STB_MK; j++)
        for (i = 0; i < STB_MK; i++)
            n += screen[(y + j) * 240 + x + i] == swap16(C_OK);
    return n;
}
static void songnav_scenes_tests(void)
{
    uint32_t i, h0, h1, nph, g;
    uint32_t seen[8];
    /* the keys launch, the encoder only selects, SAVE stores into the current scene */
    sn_reset();
    op_enter(SCR_SONG);
    ui.row[SCR_SONG] = 0;
    frames(3);
    song_refresh(1);
    key(WK(4));
    check(live_sec == 4, "songnav: scene key E, stopped: loaded");
    ui.row[SCR_SONG] = 0;
    frames(2);
    turn(EN_SELECT, 1);
    check(ui.row[SCR_SONG] == 1u && live_sec == 4 && !song.playing, "... SELECT: the cursor moves to B, nothing launched, E stays current");
    turn(EN_SELECT, 1);
    check(ui.row[SCR_SONG] == 2u && live_sec == 4, "... again: C");
    ui.row[SCR_SONG] = 1;
    frames(2);
    tap(B_SAVE);
    check(!strncmp(ui.msg, "STORED E", 8) && project_used(4), "songnav: SAVE with the cursor on B: the loop stored into E, the current scene (\"STORED E\")");
    check(live_sec == 4 && ui.row[SCR_SONG] == 1u, "... E stays current, the cursor stays on B");
    check(!op_armed(), "... nothing asked");
    ui.row[SCR_SONG] = 2;
    frames(2);
    {
        uint32_t used = (uint32_t)project_used(2);
        tap(B_REC);
        check(!op_armed() && !song.rec && !rec_wait && (uint32_t)project_used(2) == used,
              "songnav: REC tapped on a scene: nothing stored, nothing asked, not the transport's record");
    }
    /* the marker */
    stb.mk_on = 0;
    ui.force = 1;
    frames(3);
    check(stb.mk_on && stb.mk_i == 4u, "songnav: the current scene's column has the marker (a play triangle)");
    check(sn_cell_green() > 60u, "... drawn green in its box");
    check(stb.t[4][stb.mk_i][0] != 0 && sn_cell_box_y() >= GL_TOP + 22 + 5 * ((GL_END - 22 - 16) / 5),
          "... the BARS cell of the current scene shows its text; the marker sits on its own line under the table (below BARS)");
    h0 = sn_cell_hash();
    frames(40);
    h1 = sn_cell_hash();
    check(h0 == h1, "... stopped: it stands still");
    song.playing = 1;
    memset(seen, 0, sizeof seen);
    for (i = 0, nph = 0; i < 60u; i++) {
        uint32_t h, k, f = 0;
        frames(1);
        h = sn_cell_hash();
        for (k = 0; k < nph; k++)
            f |= seen[k] == h;
        if (!f && nph < 8u)
            seen[nph++] = h;
    }
    check(nph == 4u, "... transport running: it turns through four positions");
    {
        uint32_t px0 = blit_px;
        frames(60);
        check(blit_px - px0 < 60u * 4u * STB_MK * STB_MK / 6u + 2000u, "... a second of it sends the marker's box only (about six steps), not the table");
    }
    {   /* a queued scene: its column's border blinks (250 ms), only the border is sent */
        uint32_t on = 0, off = 0, px0;
        int32_t bx = STB_X0 + 6 * STB_CW - 1, by = GL_TOP + 21 + 3;
        song.playing = 0;
        live_req = 6;
        frames(3);
        check(stb.bd_on && stb.bd_i == 6u, "songnav: a scene queued: its column carries the border");
        px0 = blit_px;
        for (i = 0; i < 60u; i++) {
            frames(1);
            if (screen[by * 240 + bx] == swap16(C_WARN))
                on++;
            else
                off++;
        }
        check(on > 15u && off > 15u, "... the border blinks (amber / dark, about 250 ms each)");
        check(blit_px - px0 < 60u * 2000u / 4u, "... only the border's strips are sent while it blinks, not the table");
        live_req = -1;
        frames(3);
        check(!stb.bd_on, "... launched / unqueued: no border");
    }
    g = sn_cell_green();
    check(g > 5u, "... green still");
    song.playing = 0;
}
static void songnav_ui_tests(void)
{
    songnav_scenes_tests();
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
