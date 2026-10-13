/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the risky actions (the user, 2026-10-13: "all action that are 'risk' we should have a popup
 * yes no (home/save)"; the audit: ~/GitHub/optimist-work/helpline/ACTIONS.md). Each action that destroys or replaces
 * what undo cannot bring back, or changes the device, asks first: YES (SAVE tapped) or a combination arms the modal,
 * NO (HOME tapped) lets it go, and nothing changed. Included after ui_optimist_help.h. */
static uint32_t rk_state(void)                          /* the work, the stores' counters, the panel table */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NTRK; i++) {                        /* (the stored parts: the voices and playheads move by themselves) */
        h = glw_mix(h, trk[i].p, sizeof trk[i].p);
        h = glw_mix(h, trk[i].step, sizeof trk[i].step);
        h = glw_mix(h, trk[i].dstep, sizeof trk[i].dstep);
        h = glw_mix(h, &trk[i].eng_req, 1);
    }
    h = glw_mix(h, song.g, sizeof song.g);
    h = glw_mix(h, &panel, sizeof panel);
    h = glw_mix(h, &loads, sizeof loads);
    h = glw_mix(h, &saves, sizeof saves);
    h = glw_mix(h, up_ops, sizeof up_ops);
#if FELUCCA_ARRANGER
    h = glw_mix(h, &arrangement.count, sizeof arrangement.count);
    h = glw_mix(h, arrangement.entry, sizeof arrangement.entry);
#endif
    return h;
}
/* the cell labelled lab on the row named row of screen scr: the cursor on it, hot. 0 when not in this build */
static int rk_at(uint32_t scr, const char *row, const char *lab)
{
    uint32_t r, k, n;
    cell_t c;
    char nm[16];
    op_enter(scr);
    frame();
    n = SCREENS[scr].rows();
    for (r = 0; r < n; r++) {
        SCREENS[scr].name(r, nm);
        if (row && strcmp(nm, row))
            continue;
        for (k = 0; k < 4u; k++) {
            SCREENS[scr].cell(r, k, &c);
            if (c.label && !strcmp(c.label, lab)) {
                ui.row[scr] = (uint8_t)r;
                ui.hot = (uint8_t)(glo_cycle() ? r % 4u : k);
                return 1;
            }
        }
    }
    return 0;
}
static uint32_t rk_n, rk_ok;
static void rk_after(const char *what, uint32_t h0)     /* armed now: HOME lets it go, nothing changed */
{
    int asked = op_armed();
    tap(B_HOME);
    frame();
    if (!asked || op_armed() || rk_state() != h0) {
        printf("  risk: %s: %s\n", what, !asked ? "no question" : op_armed() ? "still asked after HOME" : "changed on HOME");
        rk_ok = 0;
    }
    rk_n++;
}
static void rk_cell(uint32_t scr, const char *row, const char *lab)   /* YES on the cell */
{
    uint32_t h0;
    if (scr == SCR_GLOBAL || scr == SCR_SYSTEM) {       /* (the settings lists: a line) */
        if (line_named(scr, lab) >= SCREENS[scr].rows())
            return;
        frame();
    } else if (!rk_at(scr, row, lab))
        return;
    h0 = rk_state();
    tap(B_SAVE);
    rk_after(lab, h0);
}
static void rk_combo(uint32_t b, const char *what)      /* HOME held + b */
{
    uint32_t h0;
    op_enter(SCR_SOUND);
    frame();
    h0 = rk_state();
    press(B_HOME);
    press(b);
    release(b);
    release(B_HOME);
    rk_after(what, h0);
}
static void risk_tests(void)
{
    reset_ui();
    song.sel = 0;
    rk_n = 0;
    rk_ok = 1;
    song.g[G_SLOT] = 1;
    rk_cell(SCR_PROJECT, "PROJECT", "LOAD");            /* LOAD over the work */
    if (project_used(prj_slot()))
        rk_cell(SCR_PROJECT, "PROJECT", "SAVE");        /* over a used slot (an empty one: no question) */
    rk_cell(SCR_PROJECT, "PROJECT", "NEW");
    ui.user_slot = 0;                                   /* (U01: used in the test's store) */
    rk_cell(SCR_PROJECT, "USER", "LOAD");
    rk_cell(SCR_PROJECT, "USER", "ERASE");
    rk_cell(SCR_PROJECT, "USER", "SAVE");
#if FELUCCA_SNAPSHOTS
    rk_cell(SCR_PROJECT, "SNAPSHOT", "LOAD");
    rk_cell(SCR_PROJECT, "SNAPSHOT", "CLEAR");
#endif
#if SL24_AUTO
    rk_cell(SCR_PROJECT, "SLOOP", "IMPORT");
#endif
    rk_cell(SCR_PROJECT, "TOOLS", "CLRSQ");
    rk_cell(SCR_PROJECT, "TOOLS", "INIT");
    rk_cell(SCR_PROJECT, "TOOLS", "NEW");
    rk_cell(SCR_SOUND, "SOUND", "INIT");                /* INIT sound */
    rk_cell(SCR_SYSTEM, 0, "PANEL");                    /* CALIBRATE */
#if FELUCCA_ARRANGER
    if (arrangement.count < 2u) {                       /* (a second part, so that one can go) */
        arrangement.entry[1] = arrangement.entry[0];
        arrangement.count = 2;
    }
    rk_cell(SCR_SONG, "PART 1", "DELETE");              /* DELETE a part: no undo for the song's order */
#endif
    rk_combo(B_ENV, "HOME + ENV: INIT");
    rk_combo(B_SEQ, "HOME + SEQ: the locks");
    rk_combo(B_REC, "HOME + REC: the track");
    song.sel = TRK_DRUM;                                /* the drum lane's user kit: SAVE, ERASE, RESET name it */
    ui.lane = 1;
    rk_cell(SCR_SOUND, 0, "SAVE");
    rk_cell(SCR_SOUND, 0, "ERASE");
    rk_cell(SCR_SOUND, 0, "RESET");
    song.sel = 0;
    ui.lane = 0;
    check(rk_ok && rk_n >= 12u, "every risky action asks (the modal: SAVE yes, HOME no) and HOME changes nothing");
    reset_ui();
}
