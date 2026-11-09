/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's report of 2026-10-29: "when I change some effects, the page count changes (expected) but
 * then the current page changes to another one, super confusing. This happens in different places." The cursor is kept on its
 * page by what the page is (op_keep.c): a change that adds or removes pages before it leaves the same page on the screen (its
 * title and its four cards), at its new place, the hot card kept; a page that went is replaced by its owner (the slots of the
 * effect it showed), else by the nearest page before it. Every list: SOUND's FX pages (an insert's type), MASTER's (a return's
 * type on SLOTS: the knob itself, the user's case; the master inserts' effects), the mixer's knob sets (MASTER's INS DYN before
 * RETURNS), SOUND's EDIT pages (another engine: PRESETS on any page), a lane's SOURCE page (a user sample). Included by
 * ui_optimist_test.c after inslots and fxi (ins_list, ins_load, fxs_set). */
static void pgk_snap(char *b, uint32_t n)               /* what the screen shows of its page: the header's title, the four cards */
{
    cell_t c;
    char t[40];
    uint32_t k, at;
    head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
    at = (uint32_t)snprintf(b, n, "%s", t);
    for (k = 0; k < 4u && at < n; k++) {
        SCR->cell(ui.row[ui.scr], k, &c);
        at += (uint32_t)snprintf(b + at, n - at, " | %s=%s", c.label ? c.label : "", c.val);
    }
}
static uint32_t pgk_row(const char *title)              /* the row of the screen shown whose page is titled so (its count: none) */
{
    uint32_t r, n = SCR->rows();
    for (r = 0; r < n; r++)
        if (ui.scr == SCR_SOUND ? snd_page(r) && !strcmp(snd_page(r)->title, title) : ui.scr == SCR_FX && !strcmp(fx_pg(r)->title, title))
            return r;
    return n;
}
static int pgk_title_is(const char *title)
{
    return ui.scr == SCR_SOUND ? snd_page(ui.row[SCR_SOUND]) && !strcmp(snd_page(ui.row[SCR_SOUND])->title, title)
                               : ui.scr == SCR_FX && !strcmp(fx_pg(ui.row[SCR_FX])->title, title);
}
static void pgk_on(uint32_t r)                           /* the cursor on row r, the way SELECT leaves it, a frame drawn */
{
    op_row_pick(r);
    frames(2);
}

static int32_t pgk_at(const uint16_t *k, uint32_t n, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (k[i] == key)
            return (int32_t)i;
    return -1;
}
/* SOUND's FX family on T1: the cursor on SENDS (past the inserts' pages); slot 1's type, set from outside the page (the editor,
 * the mixer's INSERT set, a layer), adds pages before it, then takes them away; then the cursor on one of those pages when its
 * type goes: INSERTS (their owner) */
static void pgk_sound_fx_tests(void)
{
    char a[200], b[200];
    char t[16], t2[16], t3[16];
    uint16_t k0[64];
    uint32_t i, keep, big = FXT_NONE, n0, nb = 0, r0, r, x, gone;
    reset_ui();
    ins_defaults();
    song.sel = 0;
    track_select(0);
    tap(B_FX);
    frames(2);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_FX, "page kept, SOUND FX: FX opens T1's FX pages");
    keep = ins_slot[0][0];
    n0 = SCR->rows();
    for (i = 0; i < n0 && i < 64u; i++)
        k0[i] = (uint16_t)pgk_snd_key(i);               /* (the default slots' pages) */
    for (i = 0; i < INS_NLIST; i++) {                   /* (the type with the most pages of its own in this build) */
        ins_load(0, 0, ins_list[i]);
        if (SCR->rows() > nb)
            nb = SCR->rows(), big = ins_list[i];
    }
    ins_load(0, 0, keep);
    r0 = pgk_row("SENDS");
    check(r0 < SCR->rows(), "... SENDS: a row");
    if (nb <= n0) {                                     /* (the builds with GATER, JUNO, EQ3, XDLY or the DYN run it: run_tests.sh) */
        check(1, "... (no insert type with pages of its own in this build: SOUND's FX case not here)");
        reset_ui();
        return;
    }
    pgk_on(r0);
    ui.hot = 2;                                         /* (KNOB 3's card hot: the cursor within the page) */
    pgk_snap(a, sizeof a);
    ins_load(0, 0, big);
    frame();
    pgk_snap(b, sizeof b);
    check(SCR->rows() > n0, "... slot 1 takes the type with pages of its own: the page count grows");
    check(ui.row[SCR_SOUND] == pgk_row("SENDS") && ui.row[SCR_SOUND] > r0, "... the cursor follows SENDS to its new place (was: the same number, another page)");
    check(!strcmp(a, b), "... the screen shows the same page: its title and its four cards");
    check(ui.hot == 2u, "... the hot card kept (KNOB 3's)");
    ins_load(0, 0, keep);
    frame();
    pgk_snap(b, sizeof b);
    check(ui.row[SCR_SOUND] == r0 && !strcmp(a, b) && ui.hot == 2u, "... the type back: the pages go, SENDS still, at its old place");
    /* on INSERTS itself (the knob's own page: the pages come after it) */
    r = pgk_row("INSERTS");
    pgk_on(r);
    ins_load(0, 0, big);
    frame();
    check(pgk_title_is("INSERTS") && ui.row[SCR_SOUND] == r, "... on INSERTS: still INSERTS, the new pages after it");
    /* the removal: the cursor on the first of the type's own pages (one the default slots lack), the type gone: INSERTS, the
     * page they belong to (a page the other slot's type keeps: that page still) */
    ins_load(0, 0, keep);
    frames(2);
    for (i = 0, n0 = SCR->rows(); i < n0 && i < 64u; i++)
        k0[i] = (uint16_t)pgk_snd_key(i);               /* (the default slots' pages, as the effects' pass leaves them) */
    ins_load(0, 0, big);
    frames(2);
    for (x = r + 1u; x < SCR->rows() && pgk_at(k0, n0, pgk_snd_key(x)) >= 0; x++)
        ;
    check(x < pgk_row("SENDS"), "... the type's own pages: between INSERTS and SENDS");
    snd_title_row(x, t);
    pgk_on(x);
    ui.hot = 3;
    ins_load(0, 0, keep);
    frame();
    snd_title_row(ui.row[SCR_SOUND], t2);
    for (i = 0, gone = 1; i < SCR->rows(); i++) {
        snd_title_row(i, t3);
        gone &= strcmp(t, t3) != 0;
    }
    check(gone, "... that page gone with the type");
    check(pgk_title_is("INSERTS"), "... the cursor on it, the type gone: INSERTS (the slots it was an effect of)");
    check(ui.hot == 0u, "... the hot card: the first again (another page)");
    ins_defaults();
    reset_ui();
}

/* MASTER: none of the returns' pages since 2026-10-30 (the user: "the return track stuff move out of the master track"); a
 * return's own EDIT pages instead (op_mixer.c rt_rows): the cursor on its FX row while its TYPE turns (its effect's pages after
 * it come and go): FX still; on RET DLY F when DLY leaves that return (the editor): FX, its one page left */
static uint32_t pgk_rt_row(const char *nm)              /* the row of the FX screen shown whose name is nm (its count: none) */
{
    uint32_t r, n = SCR->rows();
    char t[16];
    for (r = 0; r < n; r++) {
        SCR->name(r, t);
        if (!strcmp(t, nm))
            return r;
    }
    return n;
}
static int pgk_rt_is(const char *nm)
{
    char t[16];
    SCR->name(ui.row[SCR_FX], t);
    return ui.scr == SCR_FX && !strcmp(t, nm);
}
static void pgk_master_tests(void)
{
    static const uint8_t NODLY[FX_NSLOT] = {FXT_NONE, FXT_CHO, FXT_NONE, FXT_REV};
    static const char *const GONE[] = {"SLOTS", "RETURNS", "RET REV", "RET TYPE", "RET DELAY", "RET DLY F", "RET AMT", "RET EQ3"};
    char a[200], b[200];
    uint32_t r0, i, same = 1, n0, t0, moved = 0;
    reset_ui();
    fxs_set(FXS_DEF);
    op_enter(SCR_FX);
    frames(2);
    for (i = 0, r0 = 1; i < sizeof GONE / sizeof GONE[0]; i++)
        r0 &= pgk_rt_row(GONE[i]) == SCR->rows() && pgk_row(GONE[i]) == SCR->rows();
    check(ui.scr == SCR_FX && !ui.fxo && r0, "MASTER's pages: no SLOTS, RETURNS, RET REV, RET TYPE, RET DELAY, RET DLY F, RET AMT, RET EQ3");
    op_enter(SCR_HOME);                                 /* RET B (DLY), its EDIT pages */
    mx_apply(MXR_RET0 + 1u);
    tap(B_EDIT);
    frames(2);
    n0 = SCR->rows();
    check(ui.scr == SCR_FX && ui.fxo == 2u && pgk_rt_is("FX") && pgk_rt_row("RET DELAY") < n0 && pgk_rt_row("RET DLY F") < n0,
          "RET B's EDIT pages: FX (its effect's TYPE; no LEVEL, 2026-10-31), RET DELAY, RET DLY F (the master's of before)");
    t0 = fxs_slot[2];
    for (i = 0; i < 6u; i++) {                          /* its TYPE (FX's first card) turned: other effects, other pages after FX */
        turn(EN_K1, 1);
        frame();
        same &= pgk_rt_is("FX") && ui.row[SCR_FX] == 0u && ui.fxo == 2u;
        moved |= SCR->rows() != n0;
    }
    check(fxs_slot[2] != t0 && moved && same, "... its TYPE turned on FX: its effect's pages after FX come and go, FX stays");
    fxs_set(FXS_DEF);
    frame();
    pgk_on(pgk_rt_row("RET DLY F"));
    pgk_snap(a, sizeof a);
    fxs_set(NODLY);
    frame();
    pgk_snap(b, sizeof b);
    check(pgk_rt_is("FX") && SCR->rows() < n0 && strcmp(a, b), "... on RET DLY F, DLY out of RET B (the editor): FX, its page left");
    fxs_set(FXS_DEF);
    reset_ui();
    op_enter(SCR_FX);
    frames(2);
#if FELUCCA_MASTER_COMP
    /* the master inserts: INS DYN 1 / 2 come with DYN in a master slot; the cursor on DUCK (after them) stays on DUCK */
    {
        uint32_t keep0 = mst_slot[0], keep1 = mst_slot[1];
        fxs_set(FXS_DEF);
        mst_load(0, FXT_NONE);
        mst_load(1, FXT_NONE);
        frame();
        r0 = pgk_row("MASTER");
        if (r0 < SCR->rows()) {
            pgk_on(r0);
            pgk_snap(a, sizeof a);
            mst_load(0, FXT_DYN);
            frame();
            pgk_snap(b, sizeof b);
            check(ui.row[SCR_FX] > r0 && pgk_title_is("MASTER") && !strcmp(a, b), "... DUCK, DYN into a master slot: INS DYN 1 2 before it, DUCK still");
            pgk_on(pgk_row("LIMIT"));
            mst_load(0, FXT_NONE);
            frame();
            check(pgk_title_is("MST INS"), "... on INS DYN 2, DYN out of the master's slots: INSERTS (the master's slots)");
        }
        mst_load(0, keep0);
        mst_load(1, keep1);
    }
#endif
    fxs_set(FXS_DEF);
    reset_ui();
}

/* the mixer's MASTER row: its knob sets INSERT, INS DYN (DYN in a master slot), RETURNS, ...; the cursor on RETURNS while DYN
 * comes and goes: RETURNS still */
static void pgk_mixer_tests(void)
{
#if FELUCCA_MASTER_COMP
    char a[200], b[200], nm[16];
    uint32_t keep0 = mst_slot[0], keep1 = mst_slot[1], s, n, n0;
    reset_ui();
    fxs_set(FXS_DEF);
    mst_load(0, FXT_NONE);
    mst_load(1, FXT_NONE);
    ui.row[SCR_HOME] = MXR_MASTER;
    frames(2);
    n0 = mx_sets(MXR_MASTER);
    for (s = 0; s < n0; s++) {
        mx.set = (uint8_t)s;
        mix_name(MXR_MASTER, nm);
        if (!strcmp(nm, "DUCK"))
            break;
    }
    frames(2);
    check(ui.scr == SCR_HOME && s < n0, "page kept, the mixer: MASTER's DUCK set (its returns: their own rows since 2026-10-30)");
    pgk_snap(a, sizeof a);
    mst_load(0, FXT_DYN);
    frame();
    n = mx_sets(MXR_MASTER);
    mix_name(MXR_MASTER, nm);
    pgk_snap(b, sizeof b);
    check(n > n0 && mx.set > s && !strcmp(nm, "DUCK") && !strcmp(a, b), "... DYN into a master slot (INS DYN before DUCK): DUCK still");
    for (s = 0; s < n; s++) {
        mx.set = (uint8_t)s;
        mix_name(MXR_MASTER, nm);
        if (!strcmp(nm, "INS DYN"))
            break;
    }
    frames(2);
    mst_load(0, FXT_NONE);
    frame();
    mix_name(MXR_MASTER, nm);
    check(s < n && !strcmp(nm, "INSERT"), "... on INS DYN, DYN out: INSERT (the master's slots)");
    mst_load(0, keep0);
    mst_load(1, keep1);
    reset_ui();
#endif
}

/* SOUND on T1, every row: another engine (PRESETS on any page loads one) moves the pages after its own; a page both engines
 * have stays on the screen, a page only the first has gives the nearest page before it the second has */
static uint32_t pgk_keys(uint32_t e, uint16_t *k)       /* engine e's SOUND rows' keys (op_keep.c pgk_snd_key) */
{
    uint32_t r, n;
    set_engine(e);
    n = SCR->rows();
    for (r = 0; r < n && r < 64u; r++)
        k[r] = (uint16_t)pgk_snd_key(r);
    return n < 64u ? n : 64u;
}
static void pgk_engine_tests(void)
{
    uint16_t k1[64], k2[64];
    uint32_t e1, e2, n1, n2, i, keep, shift = 0, gone = 0;
    int32_t j, want;
    char a[200], b[200], t[16];
    reset_ui();
    song.sel = 0;
    track_select(0);
    keep = TSEL->eng_req;
    op_enter(SCR_SOUND);
    frames(2);
    for (e1 = 0; e1 < NENGINES && !(shift && gone); e1++)
        for (e2 = 0; e2 < NENGINES && !(shift && gone); e2++) {
            if (e1 == e2 || eng_free(e1) || eng_free(e2))
                continue;
            n2 = pgk_keys(e2, k2);
            n1 = pgk_keys(e1, k1);
            frames(2);                                  /* (the cursor where the keeper leaves it on e1) */
            for (i = 1; i < n1; i++) {
                j = pgk_at(k2, n2, k1[i]);
                if (!shift && j >= 0 && (uint32_t)j != i) {   /* a page both have, at another place */
                    pgk_on(i);
                    pgk_snap(a, sizeof a);
                    snd_title_row(i, t);
                    set_engine(e2);
                    frame();
                    snd_title_row(ui.row[SCR_SOUND], b);
                    check(ui.row[SCR_SOUND] == (uint32_t)j && !strcmp(t, b),
                          "page kept, another engine: a page both engines have stays on the screen, at its new place");
                    shift = 1;
                    set_engine(e1);
                    frame();
                    check(ui.row[SCR_SOUND] == i, "... the first engine again: the same page at its first place");
                } else if (j < 0 && k1[i] == (0x200u | SND_MAIN_ROW)) {   /* MAIN only e1 has (2026-11-09): e2's first page */
                    static int said;
                    if (said++)
                        continue;
                    pgk_on(i);
                    set_engine(e2);
                    frame();
                    check(ui.row[SCR_SOUND] == (n2 > 1u ? 1u : 0u), "... MAIN the other engine lacks: its first page past the SOUND row");
                    set_engine(e1);
                    frame();
                } else if (!gone && j < 0) {          /* a page only e1 has: the nearest before it e2 has */
                    for (want = -1, j = (int32_t)i - 1; j >= 0 && want < 0 && (int32_t)i - j <= (int32_t)PGK_BACK; j--)
                        want = pgk_at(k2, n2, k1[j]);
                    if (want < 0)
                        continue;
                    pgk_on(i);
                    set_engine(e2);
                    frame();
                    check((int32_t)ui.row[SCR_SOUND] == want, "... a page the other engine lacks: the nearest page before it that it has");
                    gone = 1;
                    set_engine(e1);
                    frame();
                }
            }
        }
    check(shift, "... (the case exists in this build: two engines with a page at different places)");
    set_engine(keep);
    reset_ui();
}

/* the drum track, lane 1 picked, every SOUND row: a user sample on the lane brings its SOURCE page (HIT START LEN) before KIT;
 * the cursor on KIT stays on KIT, the cursor on SOURCE when the lane plays the kit again: the page before it */
static void pgk_drum_tests(void)
{
#if FELUCCA_DRUMS && FELUCCA_DRUM_USR && DL_ANY
    char a[200], b[200], t[16], before[16];
    uint32_t keep, r, n0;
    reset_ui();
    song.sel = TRK_DRUM;
    track_select(TRK_DRUM);
    lane_select(0);
    ui.lane = 1;                                        /* (the lane picked, not DR itself) */
    keep = dl.src[0];
    dl.src[0] = DL_KIT;
    op_enter(SCR_SOUND);
    frames(2);
    n0 = SCR->rows();
    r = pgk_row("KIT");
    check(ui.scr == SCR_SOUND && lane_picked() && r < n0, "page kept, a drum lane: its KIT page");
    pgk_on(r);
    pgk_snap(a, sizeof a);
    dl.src[0] = DL_USR;                                 /* (USR1: the lane's SOURCE page shown) */
    frame();
    pgk_snap(b, sizeof b);
    check(SCR->rows() == n0 + 1u && ui.row[SCR_SOUND] == r + 1u && !strcmp(a, b), "... a user sample on the lane: SOURCE before KIT, KIT still on the screen");
    pgk_on(pgk_row("SOURCE"));
    snd_title_row(ui.row[SCR_SOUND] - 1u, before);
    dl.src[0] = DL_KIT;
    frame();
    snd_title_row(ui.row[SCR_SOUND], t);
    check(SCR->rows() == n0 && !strcmp(t, before), "... on SOURCE, the lane on the kit again: the page before it");
    dl.src[0] = (uint8_t)keep;
    ui.lane = 0;
    track_select(0);
    reset_ui();
#endif
}

static void pagekeep_tests(void)
{
    pgk_drum_tests();
    pgk_sound_fx_tests();
    pgk_master_tests();
    pgk_mixer_tests();
    pgk_engine_tests();
}
