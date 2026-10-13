/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM, GLO's settings (docs/UI-OPTIMIST-DESIGN.md sections 4.7, 4.11, 20; the user, 2026-10-12: "the system
 * menu would be better and more readable as vertical list of options", "select scroll by page", "algo one by one",
 * "consistency is key in our ui", "when we scroll with algo, we scroll page by page (the selection on the page moves line
 * by line)", "glo still pages thru pages of glo and then system, it should switch between the two"). Two vertical lists,
 * one setting a line: its name on the left, its value on the right, the lines under small section headers:
 *   GLOBAL   Tempo: BPM NUDGE SWING SYNC   Rec: MODE LENGTH START CLICK QUANT   MIDI: OUT IN SYNC CLK
 *            Clock out: CLK OUT   Channels: T1 T2 T3 DR
 *   SYSTEM   CPU: CLOCK LOAD (read-outs)   Screen   Lights   Audio   USB   Bluetooth   Calibrate: PANEL HOLD   About
 * The lines are the cells of the rows below, called through their own functions (TEMPO's tp_*, SYSTEM's sys_*), so each
 * value lives in one place and keeps its format; a row's empty cells are no line (what this build lacks is not shown).
 *   pages      fixed blocks of 4 lines, numbered within the screen (the header: "Tempo 2/5"); the 4 lines on screen are
 *              marked 1..4 at their left: KNOB k edits line k. A knob turned makes its line the cursor
 *   SELECT     a page a detent within the screen, stopping at its ends; the cursor on the page's first line
 *   ALGORITHM  the cursor a line a detent within the screen, stopping at its ends; past the page's last line the next
 *              page shows (and back)
 *   GLO        tapped: GLOBAL from any other screen; on GLOBAL, SYSTEM; on SYSTEM, GLOBAL. Each keeps its cursor line
 *   PRESETS    the cursor line's value (HOME held: back to its default); never the track or its sound here
 *   SAVE       YES on the cursor line: an action (Calibrate PANEL asks first, About VERSION, Bluetooth BLE: the devices),
 *              an on / off value toggled
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
#define GL_MAXL 40u
#if FELUCCA_BLE
#define GL_DEV (bdv.open != 0u)                         /* SYSTEM shows the BLE devices list (op_project.c) */
#else
#define GL_DEV 0
#endif
static struct {
    uint8_t ok, n[2];
    uint8_t at[2][GL_MAXL];
} gll;
static int gl_has(uint32_t s, uint32_t u, uint32_t k)   /* row u's cell k is a line */
{
    cell_t c;
    if (s || u >= GL_SYS0)
        return sys_item(s ? u : glo_sys(u), k) != SI_NONE;
    tp_cell(u, k, &c);
    return c.kind != CK_NONE;
}
static void gl_build(void)
{
    uint32_t s, u, k, n;
    if (gll.ok)
        return;
    for (s = 0; s < 2u; s++) {
        for (u = 0, n = 0; u < (s ? NSYS_SETUP : glo_rows()); u++)
            for (k = 0; k < 4u; k++)
                if (n < GL_MAXL && gl_has(s, u, k))
                    gll.at[s][n++] = (uint8_t)(u << 2 | k);
        gll.n[s] = (uint8_t)n;
    }
    gll.ok = 1;
}
static uint32_t gl_n(uint32_t s)
{
    gl_build();
    return gll.n[s & 1u];
}
static uint32_t gl_ent(uint32_t s, uint32_t i)
{
    gl_build();
    return gll.at[s & 1u][i % GL_MAXL];
}
static uint32_t gl_first(uint32_t i) { return i - i % GL_PAGE; }   /* the first line of line i's page */
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
    uint32_t u = gl_ent(s, i) >> 2;
    if (s || u >= GL_SYS0)
        str_cpy(b, SYS[(s ? u : glo_sys(u)) % NSYS].name, 12);
    else
        tp_name(u == TPR_REC2 ? TPR_REC : u, b);
}

/* the screens' functions: a row is a line (the cursor's), a cell the 4 lines of its page */
static uint32_t gls_rows(void) { return gl_n(0); }
static void gls_name(uint32_t r, char *b) { gl_sect(0, r, b); }
static void gls_cell(uint32_t r, uint32_t k, cell_t *c) { gl_cell(0, gl_first(r) + k, c); }
static void gls_turn(uint32_t r, uint32_t k, int32_t s, int fine) { gl_turn(0, gl_first(r) + k, s, fine); }
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
        gl_cell(1, gl_first(r) + k, c);
}
static void sls_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (GL_DEV)
        sys_turn(r, k, s, fine);
    else
        gl_turn(1, gl_first(r) + k, s, fine);
}
static int sls_yes(uint32_t r, uint32_t k, uint32_t ok) { return GL_DEV ? sys_yes(r, k, ok) : gl_yes(1, r, ok); }

/* ---- moving */
static void op_row_pick(uint32_t r);                    /* op_input.c */
static void gl_at(uint32_t i)                           /* the cursor on line i of the screen shown */
{
    if (i == ui.row[ui.scr])
        return;
    ui.row[ui.scr] = (uint8_t)i;
    ui.hot = (uint8_t)(i % GL_PAGE);
    op_disarm();                                        /* (a question was about the other line) */
}
static int glo_cycle(void) { return gl_scr(ui.scr); }  /* GLOBAL or SYSTEM shown as the list (not the BLE devices) */
/* SELECT: s pages on within the screen, stopping at its ends; the cursor on the page's first line */
static void glo_step(int32_t s)
{
    int32_t n = ((int32_t)gl_n(ui.scr == SCR_SYSTEM) + 3) / 4, was = (int32_t)(ui.row[ui.scr] / GL_PAGE);
    int32_t at = clamp(was + s, 0, n - 1);
    if (at != was)
        gl_at((uint32_t)at * GL_PAGE);
}
static void gl_algo(int32_t s)                          /* ALGORITHM: the cursor s lines on within the screen, stopping at its ends */
{
    if (!glo_cycle()) {                                 /* (the BLE devices list: its cursor, as SELECT) */
        op_row_pick((uint32_t)clamp((int32_t)ui.row[ui.scr] + s, 0, (int32_t)sls_rows() - 1));
        return;
    }
    gl_at((uint32_t)clamp((int32_t)ui.row[ui.scr] + s, 0, (int32_t)gl_n(ui.scr == SCR_SYSTEM) - 1));
}
static void gl_presets(int32_t s, uint32_t home)        /* PRESETS: the cursor line's value (HOME held: its default) */
{
    uint32_t sc = ui.scr == SCR_SYSTEM, r = ui.row[ui.scr];
    cell_t c;
    if (!glo_cycle())
        return;                                         /* (the BLE devices list: nothing) */
    gl_cell(sc, r, &c);
    if (c.kind == CK_VAL)
        gl_turn(sc, r, home ? OP_RESET : s, 0);
}
static void gl_pick(uint32_t r, uint32_t k)             /* KNOB k turned (op_input.c op_knobs): its line the cursor */
{
    if (glo_cycle() && gl_first(r) + k != ui.row[ui.scr]) {
        ui.row[ui.scr] = (uint8_t)(gl_first(r) + k);
        op_disarm();
    }
}
static int gl_on_tempo(void)                            /* GLOBAL's page shows Tempo's lines: OCT nudges (op_tempo.c) */
{
    uint32_t i, f;
    if (ui.scr != SCR_GLOBAL)
        return 0;
    for (i = f = gl_first(ui.row[SCR_GLOBAL]); i < f + GL_PAGE && i < gl_n(0); i++)
        if (gl_ent(0, i) >> 2 == TPR_TEMPO)
            return 1;
    return 0;
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
    {tp_rows, tp_name, tp_cell, tp_turn, tp_yes},
    {scope_rows, scope_name, scope_cell, scope_turn, scope_yes},
    {gls_rows, gls_name, gls_cell, gls_turn, gls_yes},
};
#define SCR (&SCREENS[ui.scr % SCR_N])
