/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited insert types on the Optimist UI (2026-10-13, feat/fx-instances-ui; fxi.c: CHRS ECHO, as many at once as the
 * effects memory holds, 2026-10-23): on SOUND's INSERTS row and the mixer's insert set, a slot's TYPE cell shows
 *   the free count   "CHRS" with 2/4 beside it (free of available), a type on every track nothing
 *   WAIT             the type picked, the main loop has not handed an instance out yet (fxi_poll)
 *   IN USE           the same pass with none free (fxi_can 0): the name struck through, dim
 *   FULL             none free after the pass: bypassed, the name struck through, dim, "S1 CHRS" as the label
 *   hidden           a type not built is not in the list
 * and, with the help line, a line of its own (kind op, INSERTS). Built with memories that hold 0, 1 and 4 of each
 * (run_tests.sh: FXU_NC FXU_NE).
 * Included by ui_optimist_test.c. */
/* how many CHRS, ECHO the effects memory's own room holds alone, the sends in their master slots (fxi_mem.h; run_tests.sh
 * sets it with DLY_ARENA); 0: not built */
#if INS_LIM
#define FXU_NC (FXI_B0 ? FXL_MIN(FXI_NK, FXA_I / FXA_Z0) : 0u)
#define FXU_NE (FXI_B1 ? FXL_MIN(FXI_NK, FXA_I / FXA_Z1) : 0u)
#else
#define FXU_NC 0u
#define FXU_NE 0u
#endif
static uint32_t fxu_listed(uint32_t t)                  /* the picker's list holds type t */
{
    int16_t *v;
    uint32_t i;
    ins_desc(0, 0, &v);
    for (i = 0; i < INS_NLIST; i++)
        if (ins_list[i] == t)
            return 1;
    return 0;
}
static void fxu_cell(uint32_t k, uint32_t s, cell_t *c) /* track k's slot s's TYPE cell on SOUND's INSERTS row */
{
    uint32_t r = ins_row("INSERTS");
    song.sel = (uint8_t)k;
    ui.row[SCR_SOUND] = (uint8_t)r;
    SCR->cell(r, s * 2u, c);
}
static void fxu_fresh(void)
{
    reset_ui();
    ins_defaults();
    fxs_set(FXS_DEF);
    fxi_init();
    fxi_poll();
    song.sel = 0;
}
#if INS_LIM
static void fxu_count(uint32_t t, uint32_t n, const char *nm)
{
    cell_t c;
    char want[8], why[80], full[24];
    uint32_t k;
    fxu_fresh();
    ins_load(0, 0, t);
    fxu_cell(0, 0, &c);
    check(fxi_state(0, 0) == FXI_WAIT && !strcmp(c.val, "WAIT") && !c.dim, "picked, before the main loop's pass: WAIT");
    fxi_poll();
    fxu_cell(0, 0, &c);
    snprintf(want, sizeof want, "%u/%u", n - 1u, n);
    snprintf(why, sizeof why, "%s: the free count %s before the name (was '%s' '%s')", nm, want, c.val, c.unit);
    snprintf(full, sizeof full, "%s %s", want, nm);
    check(fxi_state(0, 0) == FXI_OK && !strcmp(c.val, full) && !c.unit[0] && !c.dim && !strcmp(c.label, "S1"), why);
    for (k = 1; k < NTRK && k < n; k++) {
        ins_load(k, 0, t);
        fxi_poll();
    }
    fxu_cell(0, 0, &c);
    snprintf(want, sizeof want, "%u/%u", n < NTRK ? 0u : n - NTRK, n);
    snprintf(why, sizeof why, "%s: every instance out: %s", nm, want);
    snprintf(full, sizeof full, "%s %s", want, nm);
    check(!strcmp(c.val, full) && fxi_free(t) == (n < NTRK ? 0u : n - NTRK), why);
    if (n < NTRK) {
        ins_load(n, 1, t);                              /* (one more track than instances) */
        check(!fxi_can(n, t), "none free: fxi_can 0");
        fxu_cell(n, 1, &c);
        check(fxi_state(n, 1) == FXI_WAIT && !strcmp(c.val, nm) && c.strike && c.dim,
              "picked with none free, before the pass: the name struck, dim");
        fxi_poll();
        fxu_cell(n, 1, &c);
        snprintf(why, sizeof why, "%s: none free after the pass: name struck, dim, 'S2 %s' (was '%s' '%s' dim %u)", nm, nm, c.label, c.val, c.dim);
        snprintf(want, sizeof want, "S2 %s", nm);
        check(fxi_state(n, 1) == FXI_FULL && !strcmp(c.val, nm) && c.strike && c.dim && !strcmp(c.label, want), why);
        if (t == FXT_CHRS || t == FXT_ECHO) {                   /* (the struck name, on the screen: opt-fxi-full-<name>.ppm) */
            char shot[32];
            fxu_cell(n, 1, &c);
            ui.force = 1, frame();
            snprintf(shot, sizeof shot, "opt-fxi-full-%s", nm), ppm(shot);
        }
        fxu_cell(0, 0, &c);
        check(fxi_state(0, 0) == FXI_OK && !c.dim && strstr(c.val, nm), "... the track holding one is untouched");
        {   /* the TYPE popup on a track with none free: that entry is marked unavailable (struck, dim), ---- is not, so a
             * turn that skips it reads as such (the user, 2026-10-23: "cannot select the last FX, and it seems to move by 2") */
            int16_t *vp;
            uint32_t i, at = 0;
            song.sel = (uint8_t)n;
            (void)ins_desc(n, 0, &vp);                  /* (S1 of track n: slot 0 is free of t, slot 1 holds it) */
            for (i = 0; i < INS_NLIST; i++)
                if (ins_list[i] == t)
                    at = i;
            pen.d = &INS_DESC[0], pen.vp = vp, pop_kind = 3;
            check(at && !pen_avail((int32_t)at) && pen_avail(0), "the TYPE popup: the type with none free (and the other slot's) unavailable, struck and dim; ---- not");
            check(ins_skip(n, 0, (int32_t)at - 1, (int32_t)at) != (int32_t)at, "... and a turn onto it skips it, as before");
            pop_kind = 0;
            song.sel = 0;
        }
        {   /* the mixer's insert set shows it too */
            uint32_t r = MXR_T1 + n, sset, kk, seen = 0;
            mitem_t m;
            song.sel = (uint8_t)n;
            for (sset = 0; sset < mx_sets(r); sset++)
                for (kk = 0; kk < 4u; kk++) {
                    mx.set = (uint8_t)sset;
                    if (mx_item(r, kk, &m) && m.kind == MI_INST && m.id == 1u) {
                        mix_cell(r, kk, &c);
                        seen = c.strike && c.dim;
                    }
                }
            mx.set = 0;
            check(seen, "the mixer's insert set: the slot shows its name struck, dim");
        }
    }
}
#endif
/* a TYPE list (n names): ---- first, then A to Z, each once (fx_slots.c FXT_ALPHA; the user, 2026-10-20: "could the FX
 * be sorted alpha in the selector for the inserts ?") */
static int fxu_sorted(const char *const *nm, uint32_t n)
{
    uint32_t i;
    if (!n || strcmp(nm[0], "----"))
        return 0;
    for (i = 2; i < n; i++)
        if (strcmp(nm[i - 1u], nm[i]) >= 0)
            return 0;
    return 1;
}
static void fxi_ui_tests(void)
{
    cell_t c;
    int16_t *v;
    fxu_fresh();
    (void)ins_desc(0, 0, &v);
    (void)mst_desc(0, &v);
    check(fxu_sorted(ins_names, INS_NLIST) && fxu_sorted(mst_names, (uint32_t)mst_d[0].max + 1u),
          "the TYPE lists (a track's slots, the master's): ----, then A to Z");
#if FELUCCA_USB_AUDIO
    (void)ut_desc(UT_ID_TY, &v);
    check(fxu_sorted(ut_names, (uint32_t)ut_td[0].max + 1u), "the USB input's TYPE list: ----, then A to Z");
#endif
    check(fxu_listed(FXT_CHRS) == (FXU_NC > 0), "CHRS is in the picker's list when built (count > 0) and hidden at 0");
    check(fxu_listed(FXT_ECHO) == (FXU_NE > 0), "ECHO is in the picker's list when built (count > 0) and hidden at 0");
    fxu_cell(0, 0, &c);
    check(!c.dim && !c.hlp && c.label && !strcmp(c.label, "S1"), "a type that is not limited (the default S1): no note, no dim");
#if INS_LIM
    if (FXU_NC > 0)
        fxu_count(FXT_CHRS, FXU_NC, "CHRS");
    if (FXU_NE > 0)
        fxu_count(FXT_ECHO, FXU_NE, "ECHO");
#if FELUCCA_PARAM_HELP
    if (FXU_NC > 0) {
        uint32_t r;
        char pb[PH_LINE];
        fxu_fresh();
        ins_load(0, 0, FXT_CHRS);
        fxi_poll();
        fxu_cell(0, 0, &c);
        r = ui.row[SCR_SOUND];
        check(hl_find(SCR_SOUND, r, &c) && !strcmp(ph_text(hl_find(SCR_SOUND, r, &c), pb), "Chorus, shared: free/avail"), "help: CHRS held: \"Chorus, shared: free/avail\"");
    }
    if (FXU_NC > 0 && FXU_NC < NTRK) {
        uint32_t k, r;
        char pb[PH_LINE];
        fxu_fresh();
        for (k = 0; k <= FXU_NC; k++) {
            ins_load(k, 0, FXT_CHRS);
            fxi_poll();
        }
        fxu_cell(FXU_NC, 0, &c);
        r = ui.row[SCR_SOUND];
        check(fxi_state(FXU_NC, 0) == FXI_FULL && hl_find(SCR_SOUND, r, &c) &&
                  !strcmp(ph_text(hl_find(SCR_SOUND, r, &c), pb), "No free chorus: bypassed"),
              "help: a FULL CHRS slot: \"No free chorus: bypassed\"");
    }
    if (FXU_NE > 0 && FXU_NE < NTRK) {
        uint32_t k, r;
        char pb[PH_LINE];
        fxu_fresh();
        for (k = 0; k <= FXU_NE; k++) {
            ins_load(k, 0, FXT_ECHO);
            fxi_poll();
        }
        fxu_cell(FXU_NE, 0, &c);
        r = ui.row[SCR_SOUND];
        check(hl_find(SCR_SOUND, r, &c) && !strcmp(ph_text(hl_find(SCR_SOUND, r, &c), pb), "No free echo: bypassed"),
              "help: a FULL ECHO slot: \"No free echo: bypassed\"");
    }
#endif
#endif
    fxu_fresh();
    reset_ui();
}
