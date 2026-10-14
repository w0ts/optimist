/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the look of GLOBAL's and SYSTEM's lists (feat/settings-look, 2026-10-14; the user, from a
 * screenshot of "Global channels 5/5"). Checked in every variant:
 *   header      says Global or System and nothing of the page: the same pixels on every page
 *   title       the page's own title in the large face, "Channels 5/5": name + n/N, fitting the screen
 *   bars        a ranged value's bar spans the screen's width (GL_BX .. GL_BX + GL_BW), 4 px thick
 *   toggles     an on / off value has the square mark and no bar
 *   pitch       the 4 lines fill the room between the title and the help line
 *   joined      Screen & audio and USB + Bluetooth (Connections) share a page when they fit 4 lines together, else they stay apart; YES
 *               on BLE still opens the devices from the joined page
 *   info        SYSTEM's first page lists the memory used / total; About: version, build date + time, chip, the repo link */
static uint32_t gls_rowpx(int32_t y, int32_t x0, int32_t x1, int32_t *lo, int32_t *hi)   /* the lit pixels of screen rows y.. in x0..x1 */
{
    int32_t x;
    uint32_t n = 0;
    *lo = 1000;
    *hi = -1;
    for (x = x0; x < x1; x++)
        if (screen[y * 240 + x]) {
            n++;
            *lo = x < *lo ? x : *lo;
            *hi = x > *hi ? x : *hi;
        }
    return n;
}
static uint32_t gls_header_hash(void)
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < (uint32_t)OH_HEAD * 240u; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static int gls_header(void)
{
    uint32_t s, ok = 1, h0, p;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL, np = gl_npg(s);
        op_enter(scr);
        ui.row[scr] = 0;
        frame();
        h0 = gls_header_hash();
        for (p = 1; p < np; p++) {
            ui.row[scr] = (uint8_t)gl_pfirst(s, p);
            frame();
            ok &= gls_header_hash() == h0;              /* (the header never shows the page) */
        }
    }
    op_enter(SCR_TEMPO);
    frame();
    ok &= gls_header_hash() != h0;                      /* (the check sees the header at all) */
    if (!ok)
        printf("  glstyle: header changes with the page\n");
    return (int)ok;
}
static int gls_titles(void)
{
    uint32_t s, p, ok = 1;
    char t[24], want[24];
    for (s = 0; s < 2u; s++)
        for (p = 0; p < gl_npg(s); p++) {
            uint32_t f = gl_pfirst(s, p);
            char nm[24], num[12];
            gl_title(s, f, t, sizeof t);
            gl_ptitle(s, f, nm, sizeof nm);
            snprintf(num, sizeof num, " %u/%u", p + 1u, gl_npg(s));
            snprintf(want, sizeof want, "%s%s", nm, gl_npg(s) > 1u ? num : "");
            ok &= !strcmp(t, want) && nm[0] >= 'A' && nm[0] <= 'Z' && !strstr(t, "Global") && !strstr(t, "System");
            if (getenv("GLSTYLE_DUMP"))
                printf("  glstyle: '%s' %d px, name alone %d, n/N %d\n", t, text_w(font_big(), t), text_w(font_big(), nm), text_w(font_big(), num));
            ok &= text_w(font_big(), t) <= GL_TW || ((strchr(t, '&') || strstr(t, "Connections")) && text_w(&FONT_S, t) <= GL_TW);   /* (the large face; a joined pair's long name the small) */
            if (!ok) {
                printf("  glstyle: title '%s' (%d px) screen %u page %u\n", t, text_w(font_big(), t), s, p);
                return 0;
            }
        }
    /* the sample: GLOBAL's last page is Channels n/N */
    gl_title(0, gl_pfirst(0, gl_npg(0) - 1u), t, sizeof t);
    snprintf(want, sizeof want, "Channels %u/%u", gl_npg(0), gl_npg(0));
    ok &= !strcmp(t, want);
    if (!ok)
        printf("  glstyle: GLOBAL's last title '%s'\n", t);
    return (int)ok;
}
/* every line of every page but the cursor's: ranged values with a full-width bar, on / off values a mark and no bar */
static int gls_forms(uint32_t *bars, uint32_t *toggles)
{
    uint32_t s, p, i, ok = 1;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        op_enter(scr);
        for (p = 0; p < gl_npg(s); p++) {
            uint32_t f = gl_pfirst(s, p), e = gl_pfirst(s, p + 1u);
            for (i = f; i < e; i++) {
                cell_t c;
                int32_t ly = gl_y0() + (int32_t)(i - f) * gl_pitch(), y = OH_HEAD + ly, by = OH_HEAD + gl_by(ly);
                uint32_t cur = i == f && e - f > 1u ? f + 1u : f, tog, bar, r, lit = 0;
                if (i == cur || (s && sys_item(gl_ent(1, f) >> 2, gl_ent(1, f) & 3u) == SI_INFO))
                    continue;
                ui.row[scr] = (uint8_t)cur;
                frame();
                gl_cell(s, i, &c);
                tog = gl_tog(&c);
                bar = c.kind != CK_ACT && c.gk != GK_NONE && !tog;
                for (r = 0; r < (uint32_t)GL_BH; r++) {
                    int32_t a, b;
                    lit += gls_rowpx(by + (int32_t)r, 0, 240, &a, &b);
                }
                if (bar) {                              /* the bar: the screen's width, the row under it dark */
                    int32_t a, b, lo = 1000, hi = -1;
                    uint32_t n = 0;
                    lo = 1000;
                    hi = -1;
                    for (r = 0; r < (uint32_t)GL_BH; r++)
                        if (gls_rowpx(by + (int32_t)r, 0, 240, &a, &b)) {
                            lo = a < lo ? a : lo;
                            hi = b > hi ? b : hi;
                            n++;
                        }
                    ok &= n && lo <= GL_BX + 1 && hi >= GL_BX + GL_BW - 3 && hi < GL_BX + GL_BW + 1;
                    (*bars)++;
                } else {
                    ok &= lit == 0u;                    /* (no bar) */
                }
                if (tog) {                              /* the mark: a square at the right, outlined off, filled on */
                    int32_t mx = 236 - GL_CK, my = OH_HEAD + gl_ty(ly, GL_BH) + ((int32_t)FONT_S.h - GL_CK) / 2;
                    uint32_t on = c.gv > c.gmin;
                    ok &= screen[my * 240 + mx] != 0 && screen[(my + GL_CK - 1) * 240 + mx + GL_CK - 1] != 0;
                    ok &= (screen[(my + GL_CK / 2) * 240 + mx + GL_CK / 2] != 0) == (on != 0u);
                    (*toggles)++;
                }
                if (!ok) {
                    printf("  glstyle: screen %u line %u '%s': bar %u toggle %u lit %u\n", s, i, c.label ? c.label : "", bar, tog, lit);
                    return 0;
                }
            }
        }
    }
    return 1;
}
static int gls_fit(void)
{
    int32_t foot = 240 - GL_FOOT, bottom = OH_HEAD + gl_y0() + 4 * gl_pitch();
    int ok = gl_pitch() >= (int32_t)FONT_S.h + 3 + GL_BH + 4 && bottom <= foot && foot - bottom <= 12;
    if (!ok)
        printf("  glstyle: pitch %d, 4 lines end at %d, the foot %d\n", gl_pitch(), bottom, foot);
    return ok;
}
static uint32_t gls_lines(const char *row)              /* the lines of SYSTEM's section row, 0: not in this build */
{
    uint32_t u, k, n = 0;
    for (u = 0; u < NSYS_SETUP; u++)
        if (!strcmp(SYS[u].name, row))
            for (k = 0; k < 4u; k++)
                n += SYS[u].it[k] != SI_NONE;
    return n;
}
static int gls_joined(void)
{
    static const struct { const char *a, *b, *title; } J[2] = {{"SCREEN", "AUDIO", "Screen & audio"}, {"USB", "BLUETOOTH", "Connections"}};
    uint32_t j, ok = 1;
    for (j = 0; j < 2u; j++) {
        uint32_t na = gls_lines(J[j].a), nb = gls_lines(J[j].b), a, b;
        char t[24];
        if (!na || !nb)
            continue;                                   /* (a build with only one of them: it stands alone) */
        for (a = 0; a < gl_n(1) && strcmp(SYS[gl_ent(1, a) >> 2].name, J[j].a); a++)
            ;
        for (b = 0; b < gl_n(1) && strcmp(SYS[gl_ent(1, b) >> 2].name, J[j].b); b++)
            ;
        if (na + nb <= GL_PAGE) {
            gl_ptitle(1, a, t, sizeof t);
            ok &= gl_pg(1, a) == gl_pg(1, b) && !strcmp(t, J[j].title) && b == a + na;
        } else {
            ok &= gl_pg(1, a) != gl_pg(1, b);
        }
        if (!ok)
            printf("  glstyle: joined %s: %u + %u lines, pages %u %u\n", J[j].title, na, nb, gl_pg(1, a), gl_pg(1, b));
    }
    return (int)ok;
}
#if FELUCCA_BLE
static int gls_ble_enter(void)                          /* YES on BLE, from the joined page, opens the devices */
{
    uint32_t i = line_named(SCR_SYSTEM, "BLE"), ok;
    line_at(SCR_SYSTEM, i);
    frame();
    tap(B_SAVE);
    ok = bdv.open != 0u;
    tap(B_HOME);                                        /* (back to the list) */
    ok &= bdv.open == 0u;
    return (int)ok;
}
#endif
static int gls_about_info(void)
{
    uint32_t ok = 1, i, f = gl_pfirst(1, gl_npg(1) - 1u);
    cell_t c, v, b, ch, rp;
    char nm[24];
    gl_cell(1, f, &v);
    gl_cell(1, f + 1u, &b);
    gl_cell(1, f + 2u, &ch);
    gl_cell(1, f + 3u, &rp);
    gl_ptitle(1, f, nm, sizeof nm);
    ok &= !strcmp(nm, "About") && gl_end(1, f) - f == 4u && !strcmp(v.label, "VERSION") && !strcmp(b.label, "BUILD");
    ok &= !strcmp(b.val, __DATE__) && !strcmp(b.unit, __TIME__) && strlen(b.val) >= 10u && strlen(b.unit) == 8u;   /* (the date and time) */
    ok &= !strcmp(ch.label, "CHIP") && !strcmp(ch.val, "AC791N") && ch.unit[0];
    ok &= rp.kind == CK_RO && !strcmp(rp.unit, "github.com/w0ts/optimist");
    ok &= v.kind == CK_RO && b.kind == CK_RO && ch.kind == CK_RO;
    for (i = 0; i < 4u; i++) {                          /* the date, the time and the link fit their line */
        gl_cell(1, f + i, &c);
        if (getenv("GLSTYLE_DUMP"))
            printf("  glstyle: About line %u: label %d value %d unit %d\n", i, text_w(&FONT_S, c.label), text_w(&FONT_S, c.val), text_w(&FONT_S, c.unit));
        ok &= text_w(&FONT_S, c.val) + text_w(&FONT_S, c.unit) + text_w(&FONT_S, c.label) + 24 + 8 <= 236;   /* (the label, a gap, the value) */
    }
    if (!ok)
        printf("  glstyle: About '%s' '%s' '%s' '%s'\n", b.val, b.unit, ch.val, rp.unit);
    return (int)ok;
}
static int gls_info_page(void)
{
    uint32_t i, ok = 1, tot;
    cell_t c;
    char ln[16];
    int32_t ph = (GL_END - gl_y0()) / (int32_t)INFO_N;
    ok &= sys_item(gl_ent(1, 0) >> 2, gl_ent(1, 0) & 3u) == SI_INFO && gl_end(1, 0) == 1u;   /* (page 1 is INFO, alone) */
    ok &= ph >= (int32_t)FONT_S.h + 8;                  /* (every read-out and its bar fits the page) */
    for (i = 0; i < 4u; i++) {
        uint32_t used = sys_mem(i, &tot);
        sys_info_cell(INFO_N - 4u + i, &c);
        snprintf(ln, sizeof ln, "%u/%u", used >> 10, tot >> 10);
        ok &= c.kind == CK_RO && !strcmp(c.val, ln) && !strcmp(c.unit, "KB") && used > 0u && used < tot;   /* (read-outs: in use, below the region) */
        ok &= c.gk == GK_BAR && c.gv >= 0 && c.gv <= 100 && (uint32_t)c.gv == used * 100u / tot;
    }
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    frame();
    for (i = 0; i < INFO_N; i++) {                      /* the page draws every line's bar full width */
        int32_t a, b, y = OH_HEAD + gl_y0() + (int32_t)i * ph, ty = y + (ph - (int32_t)FONT_S.h - 6) / 2, by = ty + (int32_t)FONT_S.h + 3;
        sys_info_cell(i, &c);
        ok &= gls_rowpx(by + 2, 0, 240, &a, &b) > 0u && a <= GL_BX + 1 && b >= GL_BX + GL_BW - 3;   /* (the trough, whatever the value) */
    }
    if (!ok)
        printf("  glstyle: info page wrong (pitch %d)\n", ph);
    return (int)ok;
}
static void glstyle_tests(void)
{
    uint32_t bars = 0, toggles = 0;
    reset_ui();
    song.sel = 0;
    check(gls_header(), "GLOBAL, SYSTEM: the header says only Global / System (the same on every page)");
    check(gls_titles(), "GLOBAL, SYSTEM: each page's title is its name and n/N in the large face (\"Channels 5/5\"), whole on the screen");
    check(gls_fit(), "GLOBAL, SYSTEM: the 4 lines fill the room between the title and the help line");
    check(gls_forms(&bars, &toggles) && bars > 8u && toggles > 3u,
          "GLOBAL, SYSTEM: a ranged value's bar spans the screen's width, an on / off value has the square mark and no bar");
    check(gls_joined(), "SYSTEM: Screen & audio and USB + Bluetooth (Connections) share a page when they fit 4 lines together, else they stay apart");
#if FELUCCA_BLE
    check(gls_ble_enter(), "SYSTEM: YES on BLE from the joined USB + Bluetooth (Connections) page opens the devices");
#endif
    check(gls_info_page(), "SYSTEM: INFO, the first page: the memory used / total as read-outs with bars, every line a bar full width");
    check(gls_about_info(), "SYSTEM: About: version, build date and time, the chip, the repo link, on one page and fitting");
    reset_ui();
}
