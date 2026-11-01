/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David (hdavid), Optimist (github.com/w0ts/optimist) */
/* SYSTEM > CREDITS as end titles (firmware/src/ui/optimist/op_credits.c; the user, 2026-10-29): the list rises by itself;
 * KNOB 1 moves it by hand both ways and the crawl waits CR_PAUSE_MS; the page loops; every credit comes by in one loop;
 * nothing is drawn, ticked or hashed while the page is not shown. Included by ui_optimist_test.c before
 * ui_optimist_glstyle.h, which calls cred_checks() after its own Credits check. */
#define CT_BAND_TOP GL_TOP
static void cred_go(void)                               /* the Credits page, shown from the top */
{
    uint32_t f = gl_pfirst(1, gl_npg(1) - 1u);
    cr.on = 0;                                          /* (shown anew: from the top) */
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = (uint8_t)f;
    ui.force = 1;
    frame();
}
static int32_t cred_dist(int32_t from, int32_t to)      /* the signed distance from -> to inside one loop (shorter way round) */
{
    int32_t len = cr_len(), d = (to - from) % len;
    if (d > len / 2)
        d -= len;
    if (d < -len / 2)
        d += len;
    return d;
}
static int cred_lit_in(int32_t y0, int32_t y1, int32_t x0, int32_t x1, uint32_t *outside)   /* lit pixels in rows y0..y1 inside x0..x1; outside: lit pixels there but not between */
{
    int32_t x, y, n = 0;
    *outside = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240; x++)
            if (screen[y * 240 + x]) {
                if (x >= x0 && x <= x1)
                    n++;
                else
                    (*outside)++;
            }
    return n;
}
static int cred_sees(uint32_t i, int32_t k, int32_t line)   /* line -1: the name; 0 ..: the large line: it is drawn where it should be, whole in the band, centred */
{
    int32_t len = cr_len(), off = 0, y, h, x0, x1, w;
    uint32_t j, out;
    const char *s;
    const felucca_font_t *fb = font_big(), *f;
    for (j = 0; j < i; j++)
        off += cr_h(j);
    y = CT_BAND_TOP + GL_BAND - cr.px + off + k * len;
    if (line < 0) {
        s = CRED[i].name;
        f = &FONT_S;
    } else {
        s = CRED[i].who[line];
        f = text_w(fb, s) <= 232 ? fb : &FONT_S;
        y += (int32_t)FONT_S.h + CR_NAME_GAP + line * (int32_t)fb->h + (f == fb ? 0 : ((int32_t)fb->h - (int32_t)FONT_S.h) / 2);
    }
    h = (int32_t)f->h;
    if (y < CT_BAND_TOP || y + h > CT_BAND_TOP + GL_BAND)
        return -1;                                      /* (not whole in the band now) */
    w = text_w(f, s);
    x0 = 120 - w / 2 - 1;
    x1 = 120 + w / 2 + 1;
    return cred_lit_in(y, y + h, x0, x1, &out) > 0 && out == 0u;
}
static void cred_at(uint32_t i, int32_t top)            /* the list moved so that credit i's top is at band row top */
{
    int32_t off = 0;
    uint32_t j;
    for (j = 0; j < i; j++)
        off += cr_h(j);
    cr.pos = (GL_BAND - top + off) * 1000;
    cr_wrap();
    cr.hold = fm1_ms + 60000u;                          /* (kept: no crawl while the shot is taken) */
    cr.t = fm1_ms;
    frame();
}
static void cred_checks(void)
{
    int ok;
    int32_t p0, d, len;
    uint32_t n0, i, seen = 0, want = 0, t0, frames_n, ok_all = 1, k;
    static uint8_t got[CRED_N];
    cred_go();
    len = cr_len();
    check(cr.px >= 0 && cr.px < len && cr.px == (GL_BAND - 4) / CR_STEP * CR_STEP, "Credits: shown from the top, the first credit just under the band's edge");
    p0 = cr.px;
    n0 = cr.draws;
    frames(63);                                         /* about a second */
    d = cred_dist(p0, cr.px);
    printf("  credits: 63 frames: %d px, %u paints (loop %d px, %u credits)\n", d, (unsigned)(cr.draws - n0), len, (unsigned)CRED_N);
    check(d >= CR_SPEED - 4 && d <= CR_SPEED + 4, "Credits: the list rises by itself, about CR_SPEED pixels a second");
    check(cr.draws - n0 >= 8u && cr.draws - n0 <= 14u, "Credits: a paint for each step of CR_STEP pixels, 8 .. 14 a second, not one a frame");
    p0 = cr.px;
    turn(EN_K1, 1);
    d = cred_dist(p0, cr.px);
    printf("  credits: one detent: %d px\n", d);
    check(d >= CR_TURN - 2 && d <= CR_TURN + 2, "Credits: KNOB 1 one detent up moves the list CR_TURN pixels forward");
    p0 = cr.px;
    turn(EN_K1, -2);
    d = cred_dist(p0, cr.px);
    check(d <= -2 * CR_TURN + 2 && d >= -2 * CR_TURN - 2, "Credits: KNOB 1 turned back moves it back, both ways");
    p0 = cr.px;
    n0 = cr.draws;
    frames(150);                                        /* 2.4 s: inside the pause */
    check(cr.px == p0 && cr.draws == n0, "Credits: after a turn the crawl waits and nothing is painted at rest");
    frames(100);                                        /* past the pause */
    d = cred_dist(p0, cr.px);
    check(d > 0 && cr.draws > n0, "Credits: the crawl resumes a few seconds after the last turn");
    cred_at(0, 4);                                      /* (the start of the loop, held) */
    for (k = 0; k < 40u; k++)
        turn(EN_K1, -1);                                /* a long way back through the start: it wraps to the end */
    check(cr.pos >= 0 && cr.pos < len * 1000, "Credits: turned back through the start it wraps into the loop's end");
    /* every credit comes by in one loop: the crawl runs a whole loop, and each credit's name and its first line are found
     * on the screen, whole in the band and centred, at some frame */
    cred_go();
    cr_wrap();
    frames_n = (uint32_t)((int64_t)len * 1000 / (CR_SPEED * 16) + 40);
    for (t0 = 0; t0 < frames_n; t0++) {
        frame();
        for (i = 0; i < CRED_N; i++) {
            int32_t kk;
            if (got[i] == 3u)
                continue;
            for (kk = -1; kk <= 1; kk++) {
                if (!(got[i] & 1u) && cred_sees(i, kk, -1) == 1)
                    got[i] |= 1u;
                if (!(got[i] & 2u) && cred_sees(i, kk, 0) == 1)
                    got[i] |= 2u;
            }
        }
    }
    for (i = 0; i < CRED_N; i++) {
        seen += got[i] == 3u;
        want++;
        if (got[i] != 3u) {
            ok_all = 0;
            printf("  credits: '%s' (%s) not seen in one loop (%u)\n", CRED[i].name, CRED[i].who[0], got[i]);
        }
    }
    check(ok_all && seen == want, "Credits: every credit's name and its large line come by, whole in the band and centred, in one loop");
    check(t0 * 16u * CR_SPEED / 1000u < (uint32_t)len + 3u * CR_SPEED, "Credits: the loop is one pass of the list and its gap");
    /* the cost: another page, another screen */
    cred_go();
    frames(20);
    n0 = cr.draws;
    {
        uint32_t tt = cr.t, f = gl_pfirst(1, gl_npg(1) - 2u);
        ui.row[SCR_SYSTEM] = (uint8_t)f;                /* ABOUT: another SYSTEM page */
        frames(120);
        ok = cr.draws == n0 && cr.on == 0u && cr.t == tt;
        check(ok, "Credits: on another SYSTEM page nothing of the crawl is ticked or painted");
        cred_go();
        frames(10);
        n0 = cr.draws;
        tt = cr.t;
        op_enter(SCR_HOME);
        frames(160);
        ok = cr.draws == n0 && cr.t == tt;
        check(ok, "Credits: on another screen nothing of the crawl is ticked or painted");
        cred_go();
        check(cr.px == (GL_BAND - 4) / CR_STEP * CR_STEP, "Credits: shown again after another screen, it starts from the top");
    }
    cred_go();
    ok = 1;
    cr.hold = fm1_ms + 600000u;                         /* held: the same offset, the same signature: no paint */
    n0 = cr.draws;
    frames(200);
    ok &= cr.draws == n0;
    check(ok, "Credits: a held list is not painted again (the redraw signature)");
    /* the shots: worktrees/credits-shots when the run's output directory is that */
    cred_go();
    cred_at(0, 4);
    ppm("opt-credits-1-start");
    cred_at(4, 60);
    ppm("opt-credits-2-x0x");
    for (i = 0; i < CRED_N; i++) {
        if (!strcmp(CRED[i].name, "FDN8")) {
            cred_at(i, 40);
            ppm("opt-credits-3-two-lines");
        }
        if (!strcmp(CRED[i].name, "SLOOP 8 TRACKS")) {
            cred_at(i, 70);
            ppm("opt-credits-4-long-name");
        }
        if (!strcmp(CRED[i].name, "SAMPLES")) {
            cred_at(i, 30);
            ppm("opt-credits-5-three-lines");
        }
    }
    cred_at(CRED_N - 1u, 20);
    ppm("opt-credits-6-the-rest");
    cr.pos = (cr_len() - 80) * 1000;                    /* the seam: the end of the list over the start coming round */
    cr.hold = fm1_ms + 60000u;
    frame();
    ppm("opt-credits-7-seam");
    reset_ui();
}
