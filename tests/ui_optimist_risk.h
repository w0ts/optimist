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
    h = glw_mix(h, &dl, sizeof dl);
    h = glw_mix(h, &trk[0].preset, 1);
    h = glw_mix(h, &live_sec, sizeof live_sec);
#if FELUCCA_ARRANGER
    h = glw_mix(h, &srec, sizeof srec);
#endif
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
static void rk_edit(void) { TSEL->p[P_ATK] = (int16_t)(TSEL->p[P_ATK] > 60 ? 1 : TSEL->p[P_ATK] + 1); }   /* an edit (sound, work) */
/* a turn that replaces an edited sound: "Discard T1 edits?", HOME keeps the edit; then YES: the change, and the next
 * detent (no edits now) asks nothing */
static void rk_discard(uint32_t role, int home, const char *what)
{
    uint32_t h0, id0;
    char q[24];
    rk_edit();
    h0 = rk_state();
    id0 = snd_id(song.sel);
    if (home)
        press(B_HOME);
    turn(role, 1);
    if (home)
        release(B_HOME);
    str_cpy(q, "DISCARD ", sizeof q);
    str_cpy(q + 8, trk_tag(song.sel), sizeof q - 8u);
    str_cpy(q + 10, " EDITS?", sizeof q - 10u);
    if (!op_armed() || strcmp(ui.arm_q, q)) {
        printf("  risk: %s: no \"%s\" (%s)\n", what, q, op_armed() ? ui.arm_q : "none");
        rk_ok = 0;
    }
    rk_after(what, h0);                                 /* (HOME: the edited sound kept) */
    if (home)
        press(B_HOME);
    turn(role, 1);
    if (home)
        release(B_HOME);
    tap(B_SAVE);                                        /* YES: the change */
    if (op_armed() || snd_id(song.sel) == id0 && snd_sig(song.sel) == ed.sig[song.sel] && rk_state() == h0) {
        printf("  risk: %s: YES did not change the sound\n", what);
        rk_ok = 0;
    }
    if (home)
        press(B_HOME);
    turn(role, 1);                                      /* unedited now: no question */
    if (home)
        release(B_HOME);
    if (op_armed()) {
        printf("  risk: %s: asked again over an unedited sound\n", what);
        rk_ok = 0;
        tap(B_HOME);
    }
}
static void risk_edits_tests(void)                      /* the user, 2026-10-13: "always ask if the sound/preset has edits" */
{
    uint32_t pre, h0;
    reset_ui();
    song.sel = 0;
    rk_n = 0;
    rk_ok = 1;
    op_enter(SCR_SOUND);
    frame();
    pre = TSEL->preset;
    turn(EN_PRESET, 1);
    check(!op_armed() && TSEL->preset != pre, "an unedited sound: PRESETS browses, nothing asked");
    rk_discard(EN_PRESET, 0, "PRESETS over edits");
    rk_discard(EN_PRESET, 1, "HOME + PRESETS (the engine) over edits");
    ui.row[SCR_SOUND] = 0;                              /* the SOUND row: PRESET, ENGINE */
    frame();
    rk_discard(EN_K1, 0, "the SOUND row's PRESET over edits");
    rk_discard(EN_K2, 0, "the SOUND row's ENGINE over edits");
    rk_at(SCR_SOUND, "SOUND", "INIT");
    ui.msg[0] = 0;
    tap(B_SAVE);
    check(!op_armed() && !strcmp(ui.msg, "SOUND INIT"), "INIT over an unedited sound: done, nothing asked");
    song.sel = TRK_DRUM;                                /* the drum track: the kit over edited lanes */
    ui.lane = 0;
    op_enter(SCR_SOUND);
    frame();
    dl.ofs[0][0] = (int8_t)(dl.ofs[0][0] ^ 1);
    {
        uint32_t k0 = (uint16_t)TDRUM->p[P_E0];
        turn(EN_PRESET, 1);
        check(op_armed() && !strcmp(ui.arm_q, "DISCARD DR EDITS?") && (uint16_t)TDRUM->p[P_E0] == k0,
              "DR: PRESETS (the kit) over an edited lane: \"Discard DR edits?\"");
        tap(B_HOME);
        check(!op_armed() && (uint16_t)TDRUM->p[P_E0] == k0 && dl.ofs[0][0], "... HOME: the kit and the lane's edit kept");
        turn(EN_PRESET, 1);
        tap(B_SAVE);
        check((uint16_t)TDRUM->p[P_E0] != k0, "... YES: the next kit");
    }
#if DL_UI
    {                                                   /* a lane on its own sound: PRESETS its sound, HOME + PRESETS its kind */
        uint32_t l;
        for (l = 0; l < DRUM_LANES && ls_count(l) < 2u; l++)
            ;
        if (l < DRUM_LANES) {
            lane_pick(l);
            dl.src[l] = (uint8_t)dsnd_idx_src(1u);
            snd_mark(TRK_DRUM);
            frame();
            if (lane_presets()) {
                rk_discard(EN_PRESET, 0, "a lane's own sound over edits");
                if (lane_presets())
                    rk_discard(EN_PRESET, 1, "a lane's sound kind over edits");
            }
        }
    }
#endif
    memset(&dl, 0, sizeof dl);                          /* (the lanes as the kit has them again) */
    song.sel = 0;
    ui.lane = 0;
    check(rk_ok, "every change over an edited sound asks \"Discard T1 edits?\", HOME keeps it, YES does it, then no question");
    reset_ui();
}
static void risk_tests(void)
{
    reset_ui();
    song.sel = 0;
    rk_n = 0;
    rk_ok = 1;
    song.g[G_SLOT] = 1;
    rk_edit();                                          /* (the work changed, the sound edited: what LOAD, INIT ask over) */
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
    rk_edit();
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
    rk_edit();
    rk_combo(B_ENV, "HOME + ENV: INIT");
    rk_combo(B_SEQ, "HOME + SEQ: the locks");
    rk_combo(B_REC, "HOME + REC: the track");
#if FELUCCA_ARRANGER
    {                                                   /* SONG REC over a stored song of parts */
        uint32_t h0;
        op_enter(SCR_HOME);
        frame();
        h0 = rk_state();
        press(B_SAVE);
        tap(B_REC);
        release(B_SAVE);
        if (!op_armed() || strcmp(ui.arm_q, "RECORD OVER SONG?")) {
            printf("  risk: SONG REC: no \"Record over song?\" (%s)\n", op_armed() ? ui.arm_q : "none");
            rk_ok = 0;
        }
        rk_after("SONG REC", h0);
    }
#endif
    if ((arrangement_ready() >> 1) & 1u) {              /* a scene loaded stopped over a changed work */
        uint32_t h0;
        op_enter(SCR_SONG);
        ui.row[SCR_SONG] = 1;                           /* (scene B's row: YES loads it, stopped) */
        frame();
        rk_edit();
        h0 = rk_state();
        tap(B_SAVE);
        if (!op_armed() || strcmp(ui.arm_q, "LOAD SCENE B?")) {
            printf("  risk: scene load: no \"Load scene B?\" (%s)\n", op_armed() ? ui.arm_q : "none");
            rk_ok = 0;
        }
        rk_after("a scene loaded stopped over a changed work", h0);
        work_mark();                                    /* (as stored: loaded at once, nothing asked) */
        tap(B_SAVE);
        if (op_armed() || live_sec != 1) {
            printf("  risk: scene load over an unchanged work: %s\n", op_armed() ? "asked" : "not loaded");
            rk_ok = 0;
        }
    }
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
