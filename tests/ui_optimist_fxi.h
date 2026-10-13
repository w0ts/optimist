/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited insert types on the Optimist UI (2026-10-13, feat/fx-instances-ui; fxi.c: CHRS ECHO, a builder count of
 * instances each): on SOUND's INSERTS row and the mixer's insert set, a slot's TYPE cell shows
 *   the free count   "CHRS" with 2/4 beside it (free of available), a type on every track nothing
 *   WAIT             the type picked, the main loop has not handed an instance out yet (fxi_poll)
 *   IN USE           the same pass with none free (fxi_can 0): dim
 *   FULL             none free after the pass: bypassed, dim, "S1 CHRS" as the label
 *   hidden           a type with no instance built (count 0) is not in the list
 * and, with the help line, a line of its own (kind op, INSERTS). Built with the counts 0, 1 and 4 (run_tests.sh).
 * Included by ui_optimist_test.c. */
#define FXU_NC FELUCCA_INS_CHORUS
#define FXU_NE FELUCCA_INS_DELAY
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
    char want[8], why[80];
    uint32_t k;
    fxu_fresh();
    ins_load(0, 0, t);
    fxu_cell(0, 0, &c);
    check(fxi_state(0, 0) == FXI_WAIT && !strcmp(c.val, "WAIT") && !c.dim, "picked, before the main loop's pass: WAIT");
    fxi_poll();
    fxu_cell(0, 0, &c);
    snprintf(want, sizeof want, "%u/%u", n - 1u, n);
    snprintf(why, sizeof why, "%s: the free count %s beside the name (was '%s' '%s')", nm, want, c.val, c.unit);
    check(fxi_state(0, 0) == FXI_OK && !strcmp(c.val, nm) && !strcmp(c.unit, want) && !c.dim && !strcmp(c.label, "S1"), why);
    for (k = 1; k < NTRK && k < n; k++) {
        ins_load(k, 0, t);
        fxi_poll();
    }
    fxu_cell(0, 0, &c);
    snprintf(want, sizeof want, "%u/%u", n < NTRK ? 0u : n - NTRK, n);
    snprintf(why, sizeof why, "%s: every instance out: %s", nm, want);
    check(!strcmp(c.unit, want) && fxi_free(t) == (n < NTRK ? 0u : n - NTRK), why);
    if (n < NTRK) {
        ins_load(n, 1, t);                              /* (one more track than instances) */
        check(!fxi_can(n, t), "none free: fxi_can 0");
        fxu_cell(n, 1, &c);
        check(fxi_state(n, 1) == FXI_WAIT && !strcmp(c.val, "IN USE") && c.dim,
              "picked with none free, before the pass: IN USE, dim");
        fxi_poll();
        fxu_cell(n, 1, &c);
        snprintf(why, sizeof why, "%s: none free after the pass: FULL, dim, 'S2 %s' (was '%s' '%s' dim %u)", nm, nm, c.label, c.val, c.dim);
        snprintf(want, sizeof want, "S2 %s", nm);
        check(fxi_state(n, 1) == FXI_FULL && !strcmp(c.val, "FULL") && c.dim && !strcmp(c.label, want), why);
        fxu_cell(0, 0, &c);
        check(fxi_state(0, 0) == FXI_OK && !c.dim && !strcmp(c.val, nm), "... the track holding one is untouched");
        {   /* the mixer's insert set shows it too */
            uint32_t r = MXR_T1 + n, sset, kk, seen = 0;
            mitem_t m;
            song.sel = (uint8_t)n;
            for (sset = 0; sset < mx_sets(r); sset++)
                for (kk = 0; kk < 4u; kk++) {
                    mx.set = (uint8_t)sset;
                    if (mx_item(r, kk, &m) && m.kind == MI_INST && m.id == 1u) {
                        mix_cell(r, kk, &c);
                        seen = !strcmp(c.val, "FULL") && c.dim;
                    }
                }
            mx.set = 0;
            check(seen, "the mixer's insert set: the slot shows FULL, dim");
        }
    }
}
#endif
static void fxi_ui_tests(void)
{
    cell_t c;
    fxu_fresh();
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
