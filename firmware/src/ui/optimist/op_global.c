/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM, GLO's settings (docs/UI-OPTIMIST-DESIGN.md sections 4.7, 4.11, 20; the user, 2026-10-12: "the system
 * menu would be better and more readable as vertical list of options", "select scroll by page", "algo one by one",
 * "consistency is key in our ui", "when we scroll with algo, we scroll page by page (the selection on the page moves line
 * by line; 2026-10-13: "ALGORITHM moves track - NO. only the track!": no line-by-line encoder any more)", "glo still pages thru pages of glo and then system, it should switch between the two"). Two vertical lists,
 * one setting a line: its name on the left, its value on the right, the lines under small section headers:
 *   GLOBAL   Tempo: BPM NUDGE SWING   Rec: MODE LENGTH START CLICK QUANT   MIDI: CLK IN CLK OUT MIDI IN MIDI OUT (one
 *            page; 3 lines without CLK OUT)   Channels: T1 T2 T3 DR
 *   SYSTEM   CPU: CLOCK LOAD (read-outs)   Screen   Lights   Audio   USB   Bluetooth   Calibrate: PANEL HOLD   About
 * The lines are the cells of the rows below, called through their own functions (TEMPO's tp_*, SYSTEM's sys_*), so each
 * value lives in one place and keeps its format; a row's empty cells are no line (what this build lacks is not shown).
 *   pages      fixed blocks of 4 lines, numbered within the screen (the header: "Tempo 2/5"); the 4 lines on screen are
 *              marked 1..4 at their left: KNOB k edits line k. A knob turned makes its line the cursor
 *   SELECT     a page a detent within the screen, stopping at its ends; the cursor on the page's first line
 *   KNOB k     line k of the page: edits it and makes it the cursor (the help line shows it)
 *   ALGORITHM  the track walk, as on every screen (op_walk.c); never the cursor
 *   GLO        tapped: GLOBAL from any other screen; on GLOBAL, SYSTEM; on SYSTEM, GLOBAL. Each keeps its cursor line
 *   PRESETS    the cursor line's value (HOME held: back to its default); an action line (PANEL, drawn RUN): runs it, as
 *              YES; a read-out: nothing. Never the track or its sound here
 *   SAVE       YES on the cursor line: an action (Calibrate PANEL asks first, About VERSION says it, Bluetooth BLE: the
 *              devices), an on / off value toggled
 * On a page with Tempo's lines OCT- / OCT+ held nudge the clock (op_tempo.c tempo_nudging). The BLE devices list
 * (op_project.c dev_*) keeps its own rows and cards. GLO held is the performance layer, unchanged (op_layers.c).
 * After it, the screen table. Drawn by op_gldraw.c. */
#define GL_SYS0 ((uint32_t)TPR_N)                       /* GLOBAL's rows: TEMPO's three, then SYSTEM's global rows */
#define GL_NSYS ((uint32_t)NSYS_GLO)
static uint32_t glo_rows(void) { return GL_SYS0 + GL_NSYS; }
static uint32_t glo_sys(uint32_t r) { return NSYS_SETUP + (r - GL_SYS0); }   /* its SYS row */
static void glo_cell(uint32_t r, uint32_t k, cell_t *c)
{
    if (r < GL_SYS0)
        tp_cell(r, k, c);
    else
        sys_cell(glo_sys(r), k, c);
}
static void glo_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (r < GL_SYS0)
        tp_turn(r, k, s, fine);
    else
        sys_turn(glo_sys(r), k, s, fine);
}
static int glo_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    return r < GL_SYS0 ? tp_yes(r, k, ok) : sys_yes(glo_sys(r), k, ok);
}

/* ---- the list: line i of screen s (0 GLOBAL, 1 SYSTEM) is cell k of row u, kept as u << 2 | k (built once: what a row
 * shows depends on the build only) */
#define GL_PAGE 4u                                      /* the lines on screen, the knobs' */
#define GL_MAXL 24u                                     /* the lines of a screen, at most (19 at most, DUAL, BLE, CDC, every option) */
#if FELUCCA_BLE
#define GL_DEV (bdv.open != 0u)                         /* SYSTEM shows the BLE devices list (op_project.c) */
#else
#define GL_DEV 0
#endif
#define GL_MAXP 12u                                     /* the pages of a screen, at most (a page holds a row: 8 rows at most) */
static struct {
    uint8_t ok, n[2], np[2];
    uint16_t cont[2];                                   /* bit p: page p goes on with the section page p - 1 ended with */
    uint8_t at[2][GL_MAXL], pg[2][GL_MAXL];             /* line i: its cell (row << 2 | cell), its page */
    uint8_t pf[2][GL_MAXP + 1u];                        /* page p: its first line; [np] the line count */
} gll;
static int gl_has(uint32_t s, uint32_t u, uint32_t k)   /* row u's cell k is a line */
{
    cell_t c;
    if (s || u >= GL_SYS0)
        return sys_item(s ? u : glo_sys(u), k) != SI_NONE;
    if (u == TPR_TEMPO && k == 3u)                      /* (SYNC: PLAY's TEMPO page has it; here it is CLK IN, on the MIDI page) */
        return 0;
    tp_cell(u, k, &c);
    return c.kind != CK_NONE;
}
static void gl_secname(uint32_t s, uint32_t e, char *b) /* the section of entry e (row << 2 | cell): its row's name (REC 2's lines are REC's) */
{
    uint32_t u = e >> 2;
    if (s || u >= GL_SYS0)
        str_cpy(b, SYS[(s ? u : glo_sys(u)) % NSYS].name, 12);
    else
        tp_name(u == TPR_REC2 ? TPR_REC : u, b);
}
/* ---- the pages (the user, 2026-10-13: "the global and system screen should be grouped in logical pages, dont cut a page
 * in the middle if possible", "i prefer two pages with 2 items than messy pages", "the midi channels for the tracks should
 * be on one page"). A page is one section (gl_sid: REC 2's lines are REC's), or a joined pair (GL_JOIN: Screen & audio, USB + Bluetooth (Connections)), sections are otherwise never combined to fill a page,
 * a short one leaves its page's last lines empty and their knobs do nothing. Only a section of more than 4 lines is split,
 * between its rows (REC: MODE LENGTH START CLICK, then QUANT: "REC 1/2" over each page). The pages are made from the lines
 * this build has (gl_build): a section the build lacks moves the rest up, no page number is fixed */
static uint32_t gl_sid(uint32_t s, uint32_t e)          /* entry e's section: one number each (REC 2's lines are REC's) */
{
    uint32_t u = e >> 2;
    return s || u >= GL_SYS0 ? 8u + (s ? u : glo_sys(u)) : u == TPR_REC2 ? (uint32_t)TPR_REC : u;
}
/* joined pages (the user, 2026-10-14: "Screen and Audio could be on one page", "USB and Bluetooth go together"): two
 * neighbouring sections of SYSTEM that fit one page together (4 lines at most) share it, the page titled by GL_JOIN's
 * name (written as drawn); when they do not fit (a build with more lines) each keeps its own pages. One table: a pair is one entry */
static const struct { const char *a, *b, *title; } GL_JOIN[] = {
    {"SCREEN", "AUDIO", "Screen & audio"}, {"USB", "BLUETOOTH", "Connections"}};
#define GL_NJOIN (sizeof GL_JOIN / sizeof GL_JOIN[0])
static const char *gl_rowname(uint32_t s, uint32_t e) { return SYS[(s ? e >> 2 : glo_sys(e >> 2)) % NSYS].name; }
static uint32_t gl_grp(uint32_t s, uint32_t e)          /* entry e's page group: its section, or its joined pair's */
{
    uint32_t j;
    if (s)
        for (j = 0; j < GL_NJOIN; j++)
            if (str_eq(gl_rowname(s, e), GL_JOIN[j].a) || str_eq(gl_rowname(s, e), GL_JOIN[j].b))
                return 64u + j;
    return gl_sid(s, e);
}
static void gl_build(void)
{
    uint32_t s, u, k, n, n0, np, cnt, i, same, grp;
    if (gll.ok)
        return;
    for (s = 0; s < 2u; s++) {
        for (u = n = np = cnt = 0; u < (s ? NSYS_SETUP : glo_rows()); u++) {
            n0 = n;                                     /* (row u's lines, kept together on a page) */
            for (k = 0; k < 4u; k++)
                if (n < GL_MAXL && gl_has(s, u, k))
                    gll.at[s][n++] = (uint8_t)(u << 2 | k);
            if (n == n0)
                continue;
            same = n0 && gl_sid(s, gll.at[s][n0]) == gl_sid(s, gll.at[s][n0 - 1u]);
            grp = n0 && gl_grp(s, gll.at[s][n0]) == gl_grp(s, gll.at[s][n0 - 1u]);
            if (!grp || cnt + n - n0 > GL_PAGE) {       /* (the next page: another group, or this one is full) */
                if (same)
                    gll.cont[s] |= (uint16_t)(1u << np);   /* (one section over two pages: REC) */
                gll.pf[s][np++] = (uint8_t)n0;
                cnt = 0;
            }
            cnt += n - n0;
            for (i = n0; i < n; i++)
                gll.pg[s][i] = (uint8_t)(np - 1u);
        }
        gll.n[s] = (uint8_t)n;
        gll.np[s] = (uint8_t)np;
        gll.pf[s][np] = (uint8_t)n;
    }
    gll.ok = 1;
}
/* the accessors below, but gl_n and gl_npg, take a list that gl_build has made: gl_scr, gl_n, gl_npg and the screens'
 * rows() are the way in (every use is after one of them) */
static uint32_t gl_n(uint32_t s)
{
    gl_build();
    return gll.n[s & 1u];
}
static uint32_t gl_npg(uint32_t s)                      /* the pages of screen s */
{
    gl_build();
    return gll.np[s & 1u];
}
static uint32_t gl_ent(uint32_t s, uint32_t i) { return gll.at[s & 1u][i % GL_MAXL]; }
static uint32_t gl_pg(uint32_t s, uint32_t i) { return gll.pg[s & 1u][i % GL_MAXL]; }   /* the page of line i */
static uint32_t gl_first(uint32_t s, uint32_t i) { return gll.pf[s & 1u][gl_pg(s, i)]; }   /* the first line of line i's page */
static uint32_t gl_end(uint32_t s, uint32_t i) { return gll.pf[s & 1u][gl_pg(s, i) + 1u]; }   /* one past its last */
static int gl_scr(uint32_t scr) { return (scr == SCR_GLOBAL || scr == SCR_SYSTEM) && !(scr == SCR_SYSTEM && GL_DEV); }
static void gl_cell(uint32_t s, uint32_t i, cell_t *c)
{
    uint32_t e = gl_ent(s, i);
    if (i >= gl_n(s)) {
        cell_clear(c);                                  /* (past the last line: none) */
        return;
    }
    if (s)
        sys_cell(e >> 2, e & 3u, c);
    else
        glo_cell(e >> 2, e & 3u, c);
}
static void gl_turn(uint32_t s, uint32_t i, int32_t st, int fine)
{
    uint32_t e = gl_ent(s, i);
    if (i >= gl_n(s))
        return;
    if (s)
        sys_turn(e >> 2, e & 3u, st, fine);
    else
        glo_turn(e >> 2, e & 3u, st, fine);
}
static int gl_yes(uint32_t s, uint32_t i, uint32_t ok)  /* YES on line i; a question asked comes back to line i */
{
    uint32_t e = gl_ent(s, i);
    int r;
    if (i >= gl_n(s))
        return 0;
    r = s ? sys_yes(e >> 2, e & 3u, ok) : glo_yes(e >> 2, e & 3u, ok);
    if (!ok && op_armed() && ui.arm_scr == (s ? SCR_SYSTEM : SCR_GLOBAL))
        ui.arm_row = (uint8_t)i, ui.arm_k = 0;
    return r;
}
static void gl_sect(uint32_t s, uint32_t i, char *b)    /* line i's section: its row's name (REC 2's lines are REC's) */
{
    gl_secname(s, gl_ent(s, i), b);
}
/* the title of the page of line i, n bytes into b, cased: its section's name, or a joined pair's when the page holds both
 * ("Screen & audio") */
static void gl_ptitle(uint32_t s, uint32_t i, char *b, uint32_t n)
{
    char t[24];
    uint32_t f = gl_first(s, i), z = gl_end(s, i) - 1u, j;
    gl_sect(s, f, t);
    op_case(b, t, n);
    if (s && gl_sid(s, gl_ent(s, f)) != gl_sid(s, gl_ent(s, z)))
        for (j = 0; j < GL_NJOIN; j++)
            if (str_eq(gl_rowname(s, gl_ent(s, f)), GL_JOIN[j].a))
                str_cpy(b, GL_JOIN[j].title, n);
}

/* the page's title as the list draws it (the header says only Global or System): "Channels 5/5", the n/N dropped on a
 * screen of one page */
static void gl_title(uint32_t s, uint32_t i, char *b, uint32_t n)
{
    gl_ptitle(s, i, b, n);
    if (gl_npg(s) > 1u) {
        uint32_t k = str_len(b);
        b[k++] = ' ';
        fmt_int(b + k, (int32_t)gl_pg(s, i) + 1);
        k = str_len(b);
        b[k++] = '/';
        fmt_int(b + k, (int32_t)gl_npg(s));
    }
}

/* the screens' functions: a row is a line (the cursor's), a cell the 4 lines of its page */
static uint32_t gls_rows(void) { return gl_n(0); }
static void gls_name(uint32_t r, char *b) { gl_sect(0, r, b); }
static uint32_t gl_line(uint32_t s, uint32_t r, uint32_t k)   /* knob k's line on the page of line r: past the page's last, GL_MAXL (none) */
{
    uint32_t i = gl_first(s, r) + k;
    return i < gl_end(s, r) ? i : GL_MAXL;
}
static void gls_cell(uint32_t r, uint32_t k, cell_t *c) { gl_cell(0, gl_line(0, r, k), c); }   /* (an empty line: no cell) */
static void gls_turn(uint32_t r, uint32_t k, int32_t s, int fine) { gl_turn(0, gl_line(0, r, k), s, fine); }
static int gls_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    (void)k;
    return gl_yes(0, r, ok);
}
static uint32_t sls_rows(void) { return GL_DEV ? sys_rows() : gl_n(1); }
static void sls_name(uint32_t r, char *b)
{
    if (GL_DEV)
        sys_name(r, b);
    else
        gl_sect(1, r, b);
}
static void sls_cell(uint32_t r, uint32_t k, cell_t *c)
{
    if (GL_DEV)
        sys_cell(r, k, c);
    else
        gl_cell(1, gl_line(1, r, k), c);
}
static void sls_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (GL_DEV)
        sys_turn(r, k, s, fine);
    else
        gl_turn(1, gl_line(1, r, k), s, fine);
}
static int sls_yes(uint32_t r, uint32_t k, uint32_t ok) { return GL_DEV ? sys_yes(r, k, ok) : gl_yes(1, r, ok); }

/* ---- moving */
static void op_row_pick(uint32_t r);                    /* op_input.c */
static void gl_at(uint32_t i)                           /* the cursor on line i of the screen shown */
{
    if (i == ui.row[ui.scr])
        return;
    ui.row[ui.scr] = (uint8_t)i;
    ui.hot = (uint8_t)(i - gl_first(ui.scr == SCR_SYSTEM, i));
    op_disarm();                                        /* (a question was about the other line) */
}
static int glo_cycle(void) { return gl_scr(ui.scr); }  /* GLOBAL or SYSTEM shown as the list (not the BLE devices) */
/* SELECT: s pages on within the screen, stopping at its ends; the cursor on the page's first line */
static void glo_step(int32_t s)
{
    uint32_t sc = ui.scr == SCR_SYSTEM, r = ui.row[ui.scr], was = r;
    for (; s > 0 && gl_end(sc, r) < gl_n(sc); s--)
        r = gl_end(sc, r);
    for (; s < 0 && gl_first(sc, r); s++)
        r = gl_first(sc, gl_first(sc, r) - 1u);
    if (r != was)
        gl_at(r);
}
static void gl_pick(uint32_t r, uint32_t k)             /* KNOB k turned (op_input.c op_knobs): its line the cursor */
{
    uint32_t i = gl_line(ui.scr == SCR_SYSTEM, r, k);
    if (glo_cycle() && i < GL_MAXL && i != ui.row[ui.scr]) {
        ui.row[ui.scr] = (uint8_t)i;
        op_disarm();
    }
}
static int gl_on_tempo(void)                            /* GLOBAL's page shows Tempo's lines: OCT nudges (op_tempo.c) */
{                                                       /* (Tempo is GLOBAL's first section: its lines are on page 0) */
    return ui.scr == SCR_GLOBAL && gl_n(0) && gl_pg(0, ui.row[SCR_GLOBAL]) == 0u;
}
/* GLO tapped: GLOBAL; on GLOBAL, SYSTEM; on SYSTEM (its BLE devices list too: closed), GLOBAL. Each keeps its line */
static void glo_tap(void) { op_enter(ui.scr == SCR_GLOBAL ? SCR_SYSTEM : SCR_GLOBAL); }

static const screen_t SCREENS[SCR_N] = {
    {mix_rows, mix_name, mix_cell, mix_turn, mix_yes},
    {snd_rows, snd_name_row, snd_cell, snd_turn, snd_yes},
    {fxs_rows, fxs_name, fxs_cell, fxs_turn, fxs_yes},
    {prj_rows, prj_name, prj_cell, prj_turn, prj_yes},
    {sls_rows, sls_name, sls_cell, sls_turn, sls_yes},
    {step_rows, step_name, step_cell, step_turn, step_yes},
    {song_rows, song_name, song_cell, song_turn, song_yes},
    {tps_rows, tp_name, tps_cell, tps_turn, tps_yes},
    {scope_rows, scope_name, scope_cell, scope_turn, scope_yes},
    {gls_rows, gls_name, gls_cell, gls_turn, gls_yes},
};
#define SCR (&SCREENS[ui.scr % SCR_N])
