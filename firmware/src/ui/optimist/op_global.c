/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL, GLO tapped (docs/UI-OPTIMIST-DESIGN.md section 4.9; the user, 2026-10-12: "what is supposed to be the GLO page ?
 * i thought it would be the page where we have bpm, nudge, midi track settings etc"): the settings of the whole
 * instrument a user changes while playing, as SLOOP's GLO page, rows of 2x2 cards with the row list under them. Every
 * row is another screen's, called through its own functions, so each value lives in one place:
 *   TEMPO      BPM · NUDGE · SWING · SYNC          TEMPO's rows (op_tempo.c tp_*): what PLAY held shows
 *   REC        MODE · LENGTH · START · CLICK
 *   REC 2      QUANT
 *   MIDI       OUT · IN · SYNC · CLK               SYSTEM's global rows (op_project.c SYS, after NSYS_SETUP: no longer
 *   CLOCK OUT  CLK OUT                             on SYSTEM, which keeps the setup: CPU, SCREEN, AUDIO, BLUETOOTH ...)
 *   CHANNELS   T1 · T2 · T3 · DR
 * GLO is the settings button (the user, 2026-10-12, page organisation D1 as revised): one cycle, GLOBAL's rows then
 * SYSTEM's (CPU first: CLOCK, LOAD), then GLOBAL's first again; GLO tapped steps it a row, round, SELECT walks the same
 * sequence and stops at its ends (glo_step). Two screens still: the header's badge names GLOBAL or SYSTEM, the n/N
 * counts within the screen. On the TEMPO row OCT- / OCT+ held nudge the clock as on the TEMPO page (op_tempo.c
 * tempo_nudging). GLO held is the performance layer, unchanged (op_layers.c). After it, the screen table. */
#define GL_SYS0 ((uint32_t)TPR_N)                       /* the first of SYSTEM's global rows */
#define GL_NSYS ((uint32_t)NSYS_GLO)
static uint32_t glo_rows(void) { return GL_SYS0 + GL_NSYS; }
static uint32_t glo_sys(uint32_t r) { return NSYS_SETUP + (r - GL_SYS0); }   /* its SYS row */
static void glo_name(uint32_t r, char *b)
{
    if (r < GL_SYS0)
        tp_name(r, b);
    else
        sys_name(glo_sys(r), b);
}
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
static void op_row_pick(uint32_t r);                    /* op_input.c */
/* GLOBAL's and SYSTEM's rows as one sequence: s rows on (wrap: past the last, GLOBAL's first; else stopping at the
 * ends); crossing from one screen to the other enters it on the row reached */
static void glo_step(int32_t s, int wrap)
{
    int32_t ng = (int32_t)glo_rows(), n = ng + (int32_t)NSYS_SETUP;
    int32_t at = (ui.scr == SCR_SYSTEM ? ng : 0) + (int32_t)ui.row[ui.scr] + s;
    uint32_t scr;
    at = wrap ? (at >= n ? 0 : at < 0 ? n - 1 : at) : clamp(at, 0, n - 1);
    scr = at < ng ? SCR_GLOBAL : SCR_SYSTEM;
    if (scr != ui.scr) {
        ui.row[scr] = (uint8_t)(at < ng ? at : at - ng);
        op_enter(scr);
        return;
    }
    op_row_pick((uint32_t)(at < ng ? at : at - ng));
}
static int glo_cycle(void)                              /* GLOBAL or SYSTEM shown, as rows (not the BLE devices list) */
{
#if FELUCCA_BLE
    if (bdv.open)
        return 0;
#endif
    return ui.scr == SCR_GLOBAL || ui.scr == SCR_SYSTEM;
}
static void glo_tap(void)                               /* GLO tapped: GLOBAL; on the cycle, its next row, round (D1) */
{
#if FELUCCA_BLE
    if (ui.scr == SCR_SYSTEM && bdv.open) {
        dev_close();                                    /* (the devices list: back to its BLUETOOTH row) */
        return;
    }
#endif
    if (glo_cycle())
        glo_step(1, 1);
    else
        op_enter(SCR_GLOBAL);
}

static const screen_t SCREENS[SCR_N] = {
    {mix_rows, mix_name, mix_cell, mix_turn, mix_yes},
    {snd_rows, snd_name_row, snd_cell, snd_turn, snd_yes},
    {fxs_rows, fxs_name, fxs_cell, fxs_turn, fxs_yes},
    {prj_rows, prj_name, prj_cell, prj_turn, prj_yes},
    {sys_rows, sys_name, sys_cell, sys_turn, sys_yes},
    {step_rows, step_name, step_cell, step_turn, step_yes},
    {song_rows, song_name, song_cell, song_turn, song_yes},
    {tp_rows, tp_name, tp_cell, tp_turn, tp_yes},
    {scope_rows, scope_name, scope_cell, scope_turn, scope_yes},
    {glo_rows, glo_name, glo_cell, glo_turn, glo_yes},
};
#define SCR (&SCREENS[ui.scr % SCR_N])
