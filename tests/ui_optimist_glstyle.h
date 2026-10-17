/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the look of GLOBAL's and SYSTEM's lists (feat/settings-look, 2026-10-14; the user, from a
 * screenshot of "Global channels 5/5"). Checked in every variant:
 *   header      the GLOBAL / SYSTEM badge, the page's title and n/N ("Channels 5/5", a short form when the room is short, never
 *               cut), bar.beat and the tempo (2026-10-14: the title is back in the top bar)
 *   title       no title line under the header: the lines have its room
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
/* the header of every page: the GLOBAL / SYSTEM badge, then the page's title and n/N (a short form where the room is short,
 * never cut), bar.beat and the tempo at the right (the user, 2026-10-14: "move back the title in the top bar") */
static int gls_header(void)
{
    uint32_t s, ok = 1, p, hashes = 0, h0 = 0;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL, np = gl_npg(s);
        for (p = 0; p < np; p++) {
            head_t h;
            char nm[24], pos[12], full[32];
            uint32_t f = gl_pfirst(s, p);
            op_enter(scr);
            ui.row[scr] = (uint8_t)f;
            frame();
            head_make(&h);
            gl_ptitle(s, f, nm, sizeof nm);
            snprintf(pos, sizeof pos, " %u/%u", p + 1u, np);
            snprintf(full, sizeof full, "%s", h.t);        /* (head_make's t holds the n/N already) */
            if (getenv("GLSTYLE_DUMP"))
                printf("  glstyle: header '%s' + '%s' (name '%s'), %d of %d px, bar.beat '%s'\n", h.t, h.pos, nm, (int)strlen(full) * 8, h.room, h.r);
            ok &= !strcmp(h.eng, s ? "SYSTEM" : "GLOBAL") && h.t[0] >= 'A' && h.t[0] <= 'Z' && !h.cut;
            ok &= np < 2u ? !h.pos[0] : !strcmp(h.pos, pos);
            ok &= (int32_t)strlen(full) * 8 <= h.room && h.r[0] && h.bpm[0];   /* (whole in its room, bar.beat and the tempo kept) */
            ok &= !strncmp(h.t, nm, strlen(nm)) || nm_find(NM_SHORT, nm, 0, full, sizeof full);   /* (the name, or its table's short form) */
            ok &= !strstr(h.t, "Global") && !strstr(h.t, "System");
            if (!hashes++)
                h0 = gls_header_hash();
            if (np > 1u && p == 1u)
                ok &= gls_header_hash() != h0;          /* (the title is drawn: another page, another header) */
            if (!ok) {
                printf("  glstyle: header '%s' + '%s' (name '%s') screen %u page %u: room %d, bar.beat '%s'\n", h.t, h.pos, nm, s, p, h.room, h.r);
                return 0;
            }
        }
    }
    return 1;
}
/* the big title line is gone: the band's rows above the first line's text are dark on every page (the cursor has no fill) */
static int gls_no_title(void)
{
    uint32_t s, p, ok = 1;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        for (p = 0; p < gl_npg(s); p++) {
            int32_t y, a, b, top;
            cell_t c;
            gl_lrow_t lr;
            op_enter(scr);
            ui.row[scr] = (uint8_t)gl_pfirst(s, p);
            frame();
            if (gl_textpage())
                continue;                               /* (INFO, CREDITS are read-outs from the top of the band, no 4-line pitch) */
            gl_cell(s, gl_pfirst(s, p), &c);
            gl_lrow(&c, gl_y0(), &lr);                  /* (the first line's text: its name, or its value in the large face) */
            top = lr.fy < lr.ty ? lr.fy : lr.ty;
            for (y = OH_HEAD; y < OH_HEAD + top; y++)
                if (gls_rowpx(y, 0, 240, &a, &b)) {
                    ok = 0;
                    if (getenv("GLSTYLE_DUMP"))
                        printf("  glstyle: screen %u page %u: lit row %d (%d..%d), band from %d, first text %d\n", s, p, y, a, b, OH_HEAD, OH_HEAD + top);
                }
        }
    }
    if (!ok)
        printf("  glstyle: lit pixels above the first line (a title line?)\n");
    return (int)ok;
}
static int gls_titles(void)
{
    uint32_t s, p, ok = 1;
    char t[24], want[24];
    for (s = 0; s < 2u; s++)
        for (p = 0; p < gl_npg(s); p++) {
            gl_ptitle(s, gl_pfirst(s, p), t, sizeof t);
            ok &= t[0] >= 'A' && t[0] <= 'Z' && !strstr(t, "Global") && !strstr(t, "System");
            if (!ok) {
                printf("  glstyle: title '%s' screen %u page %u\n", t, s, p);
                return 0;
            }
        }
    /* the sample: GLOBAL's last page is Channels n/N */
    {
        head_t h;
        op_enter(SCR_GLOBAL);
        ui.row[SCR_GLOBAL] = (uint8_t)gl_pfirst(0, gl_npg(0) - 1u);
        frame();
        head_make(&h);
        snprintf(want, sizeof want, "Channels %u/%u", gl_npg(0), gl_npg(0));
        ok &= !strcmp(h.t, want);
        if (!ok)
            printf("  glstyle: GLOBAL's last title '%s' '%s'\n", h.t, h.pos);
    }
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
                int32_t ly = gl_y0() + (int32_t)(i - f) * gl_pitch(), y = OH_HEAD + ly, by = OH_HEAD + gl_by(ly), ty0 = gl_ty(ly, GL_BH);
                uint32_t cur = i == f && e - f > 1u ? f + 1u : f, tog, bar, r, lit = 0;
                if (i == cur || (s && (sys_item(gl_ent(1, f) >> 2, gl_ent(1, f) & 3u) == SI_INFO || sys_item(gl_ent(1, f) >> 2, gl_ent(1, f) & 3u) == SI_CREDITS)))
                    continue;
                ui.row[scr] = (uint8_t)cur;
                frame();
                gl_cell(s, i, &c);
                {
                    gl_lrow_t lr;                       /* (the bar's row: under the value, in whichever face it is) */
                    gl_lrow(&c, ly, &lr);
                    by = OH_HEAD + lr.by, ty0 = lr.ty;
                }
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
                    int32_t mx = 236 - GL_CK, my = OH_HEAD + ty0 + ((int32_t)FONT_S.h - GL_CK) / 2;
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
    int32_t foot = OP_FOOT_Y, bottom = OH_HEAD + gl_y0() + 4 * gl_pitch();   /* (the help band: op_graph.c OP_FOOT) */
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
static int gls_ble_enter(void)                          /* YES on DEVICES opens the devices list */
{
    uint32_t i = line_named(SCR_SYSTEM, "DEVICES"), ok;
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
    uint32_t ok = 1, i, f = gl_pfirst(1, gl_npg(1) - 2u);   /* (the page before the last, CREDITS) */
    cell_t c, v, b, rp;
    char nm[24];
    gl_cell(1, f, &v);
    gl_cell(1, f + 1u, &b);
    gl_cell(1, f + 2u, &rp);
    gl_ptitle(1, f, nm, sizeof nm);
    ok &= !strcmp(nm, "About") && gl_end(1, f) - f == 3u && !strcmp(v.label, "VERSION") && !strcmp(b.label, "BUILD");
    ok &= !strcmp(b.val, __DATE__) && !strcmp(b.unit, __TIME__) && strlen(b.val) >= 10u && strlen(b.unit) == 8u;   /* (the date and time) */
    ok &= rp.kind == CK_RO && !strcmp(rp.unit, "github.com/w0ts/optimist");
    ok &= v.kind == CK_RO && b.kind == CK_RO;
    for (i = 0; i < 3u; i++) {                          /* the date, the time and the link fit their line */
        gl_cell(1, f + i, &c);
        if (getenv("GLSTYLE_DUMP"))
            printf("  glstyle: About line %u: label %d value %d unit %d\n", i, text_w(&FONT_S, c.label), text_w(&FONT_S, c.val), text_w(&FONT_S, c.unit));
        ok &= text_w(&FONT_S, c.val) + text_w(&FONT_S, c.unit) + text_w(&FONT_S, c.label) + 24 + 8 <= 236;   /* (the label, a gap, the value) */
    }
    if (!ok)
        printf("  glstyle: About '%s' '%s' '%s'\n", b.val, b.unit, rp.unit);
    return (int)ok;
}
static int gls_credits(void)                            /* the last page: CREDITS, text lines that fit, LICENSING.md the last */
{
    uint32_t ok = 1, i, f = gl_pfirst(1, gl_npg(1) - 1u);
    char nm[24];
    gl_ptitle(1, f, nm, sizeof nm);
    ok &= !strcmp(nm, "Credits") && gl_end(1, f) - f == 1u && sys_item(gl_ent(1, f) >> 2, gl_ent(1, f) & 3u) == SI_CREDITS;
    ok &= CRED_N >= 10u && !strcmp(CRED[CRED_N - 1u][1], "LICENSING.md") && !strcmp(CRED[0][1], FELUCCA_VERSION);
    for (i = 0; i < CRED_N; i++) {                      /* (the name, a gap, the who: inside 6 .. 236) */
        char b[16];
        ok &= 6 + text_w(&FONT_S, nm_card(CRED[i][0], 0, b, sizeof b)) + 8 + text_w(&FONT_S, CRED[i][1]) <= 236;
    }
    ok &= (GL_END - gl_y0()) / (int32_t)CRED_N >= (int32_t)FONT_S.h;   /* (a line's pitch holds the font) */
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = (uint8_t)f;
    ui.force = 1;
    frame();
    ok &= gl_textpage() != 0u;
    ppm("opt-system-credits");
    return (int)ok;
}
static int gls_info_page(void)
{
    uint32_t i, ok = 1;
    cell_t c;
    int32_t ph = (GL_END - gl_y0()) / (int32_t)INFO_N < GL_INFO_PH ? (GL_END - gl_y0()) / (int32_t)INFO_N : GL_INFO_PH;
    ok &= sys_item(gl_ent(1, 0) >> 2, gl_ent(1, 0) & 3u) == SI_INFO && gl_end(1, 0) == 1u;   /* (page 1 is INFO, alone) */
    ok &= ph >= (int32_t)FONT_S.h + 6;                  /* (every read-out fits the page) */
    song.batt_raw = 0;                                  /* the battery line: no reading says so, a reading n/3 and the raw count (no invented percentage) */
    sys_info_cell(INFO_BASE - 1u, &c);
    ok &= c.kind == CK_RO && !strcmp(c.label, "BATTERY") && !strcmp(c.val, "NO READING");
    song.batt_raw = 612;
    sys_info_cell(INFO_BASE - 1u, &c);
    ok &= !strcmp(c.val, "3/3 612") && 1;
    song.batt_raw = 540;
    sys_info_cell(INFO_BASE - 1u, &c);
    ok &= !strcmp(c.val, "1/3 540") ;
    info_mode = OVL_NOW;                                /* (the CPU line's measure: NOW; a fuzz run's KNOB 2 may have left another) */
    {   /* the CPU0 load number's colour: green under 60 %, amber 60..85, red over 85 (the guard's ceiling), text only */
        static const int32_t pc[7] = {0, 59, 60, 85, 86, 100, 150};
        static const uint16_t want[7] = {C_OK, C_OK, C_WARN, C_WARN, C_ERR, C_ERR, C_ERR};
        for (i = 0; i < 7u; i++) {
            char num[8];
            song.cpu_q8 = (uint32_t)(pc[i] * 256 + 255) / 100u;
            sys_info_cell(1, &c);
            snprintf(num, sizeof num, "%d", (int)pc[i]);
            ok &= !strcmp(c.val, num) && c.col == want[i];
            if (c.col != want[i] || strcmp(c.val, num))
                printf("  glstyle: CPU load %d %%: '%s' colour %04x, wanted %04x\n", (int)pc[i], c.val, c.col, want[i]);
        }
        song.cpu_q8 = 0;
    }
    for (i = 0; i < INFO_N; i++) {                      /* no line is the memory's any more */
        sys_info_cell(i, &c);
        ok &= strcmp(c.label, "FLASH") && strcmp(c.label, "RAM") && strcmp(c.label, "POOL") && strcmp(c.label, "RAM CODE");
    }
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    song.cpu_q8 = 70u * 256u / 100u;
    frame();
    ppm("opt-info-cpu70");
    song.cpu_q8 = 0;
    frame();
    for (i = 0; i < INFO_BASE; i++) {                   /* the page draws text only, no bar under any line */
        int32_t a, b, y = OH_HEAD + gl_y0() + (int32_t)i * ph, ty = y + (ph - (int32_t)FONT_S.h) / 2, by = ty + (int32_t)FONT_S.h + 2;
        sys_info_cell(i, &c);
        ok &= gls_rowpx(by + 2, 0, 240, &a, &b) == 0u;   /* (no bar under the text: the row below the glyphs is empty) */
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
    check(gls_header(), "GLOBAL, SYSTEM: the header is the badge, the page's title and n/N (not cut, a short form if the room is short), bar.beat and the tempo");
    check(gls_titles(), "GLOBAL, SYSTEM: each page's title is its name in sentence case (GLOBAL's last page \"Channels\" n/N)");
    check(gls_no_title(), "GLOBAL, SYSTEM: no big title line under the header");
    check(gls_fit(), "GLOBAL, SYSTEM: the 4 lines fill the room between the header and the help line");
    {
        int fo = gls_forms(&bars, &toggles);
        if (getenv("GLSTYLE_DUMP") || !fo || bars <= 8u || toggles == 0u)
            printf("  glstyle: forms %d, %u bars, %u toggles\n", fo, bars, toggles);
        bars = fo ? bars : 0u;
    }
    check(bars > 8u && toggles > 0u,
          "GLOBAL, SYSTEM: a ranged value's bar spans the screen's width, an on / off value has the square mark and no bar");
    check(gls_joined(), "SYSTEM: Screen & audio and USB + Bluetooth (Connections) share a page when they fit 4 lines together, else they stay apart");
#if FELUCCA_BLE
    check(gls_ble_enter(), "SYSTEM: YES on BLE from the joined USB + Bluetooth (Connections) page opens the devices");
#endif
#if FELUCCA_SYS_DEBUG
    check(gls_info_page(), "SYSTEM: INFO, the first page: the memory used / total as text read-outs, no bars");
#endif
    check(gls_about_info(), "SYSTEM: About: version, build date and time, the chip, the repo link, on one page and fitting");
    check(gls_credits(), "SYSTEM: Credits, after About: the version, the upstreams by name, LICENSING.md for the rest, each line fitting");
    reset_ui();
}
