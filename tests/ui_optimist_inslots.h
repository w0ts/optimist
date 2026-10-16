/* SPDX-License-Identifier: GPL-3.0-only */
/* The track insert slots on the Optimist UI (2026-10-12: "two slot, configurable per track (filter, disto, comp etc)";
 * fx_slots.c ins_slot): SOUND's FX row INSERTS (S1 TYPE, its amount, S2 TYPE, its amount), CMP the track's own while a
 * slot holds the COMP, SENDS the master sends (the drum track: its send LEVELS, not ON / OFF), BUS SENDS the drum
 * bus's own; the cards' second bar (op_effval.c) on an insert amount a macro moves. Included by ui_optimist_test.c. */
static uint32_t ins_row(const char *title)              /* SOUND's row of the page titled so, SCR->rows() none */
{
    uint32_t r;
    op_enter(SCR_SOUND);
    for (r = 0; r < SCR->rows(); r++)
        if (snd_page(r) && !strcmp(snd_page(r)->title, title))
            return r;
    return r;
}
static void inslots_tests(void)
{
    cell_t c;
    uint32_t r;
    reset_ui();
    ins_defaults();
    fxs_set(FXS_DEF);
    dsend_kit[0] = dsend_kit[1] = dsend_kit[2] = DKS_DEF;   /* (the levels as a new project's: the fuzz turned them) */
    song.sel = 0;
    r = ins_row("INSERTS");
    check(r < SCR->rows(), "SOUND's FX row: INSERTS");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    SCR->cell(r, 0, &c);
    check(!strcmp(c.label, "S1") && !strcmp(c.val, FELUCCA_FX_DIST ? "DIST" : "EMPTY"), "INSERTS: KNOB 1 S1's TYPE (DIST by default)");
    SCR->cell(r, 1, &c);
    check(!FELUCCA_FX_DIST || (c.d == &TP[P_DIST] && c.vp == &trk[0].p[P_DIST]), "... KNOB 2 S1's amount: T1's DST");
    SCR->cell(r, 2, &c);
    check(!strcmp(c.label, "S2") && !strcmp(c.val, TFLT_ON ? "FILTER" : "EMPTY"), "... KNOB 3 S2's TYPE (FILTER when built)");
    SCR->cell(r, 3, &c);
    check(TFLT_ON ? c.d == &TP[FXT_AMT[FXT_FILT]] : !c.d, "... KNOB 4 S2's amount (empty: \"-\")");
    check(ins_row("CMP") >= SCR->rows(), "CMP: not a row while no slot of T1 holds the COMP");
#if FELUCCA_MASTER_COMP
    {
        int16_t *v;
        int32_t i, cmp = 0;
        r = ins_row("INSERTS");
        ui.row[SCR_SOUND] = (uint8_t)r;
        frame();
        ins_desc(0, 1, &v);
        for (i = 0; i < (int32_t)INS_NLIST; i++)
            cmp = ins_list[i] == FXT_COMP ? i : cmp;
        turn(EN_K3, cmp - *v);
        SCR->cell(r, 3, &c);
        check(ins_slot[0][1] == FXT_COMP && c.d == &TP[P_TCOMP] && ins_slot[1][1] == INS_DEF1,
              "KNOB 3 turned to CMP: T1's S2 holds the COMP (T2's kept), KNOB 4 its amount");
        r = ins_row("DYN");
        SCR->cell(r < SCR->rows() ? r : 0u, 1, &c);
        check(r < SCR->rows() && c.vp == &ins_cset[0][0], "DYN: a row now, T1's own THRS RATIO GAIN CEIL (DYN 2: ATK REL)");
        song.sel = 1;
        check(ins_row("DYN") >= SCR->rows(), "... T2 selected (no DYN in its slots): no DYN row");
        song.sel = 0;
        r = ins_row("INSERTS");
        ui.row[SCR_SOUND] = (uint8_t)r;
        frame();
        ins_desc(0, 0, &v);
        turn(EN_K1, cmp - *v);
        check(ins_slot[0][1] == FXT_COMP && ins_slot[0][0] != FXT_COMP, "KNOB 1 turned to CMP: S2's type, skipped (no more swapping the other slot's insert)");
        ins_defaults();
    }
#endif
    r = ins_row("SENDS");
    SCR->cell(r < SCR->rows() ? r : 0u, 0, &c);
    check(r < SCR->rows() && (!FELUCCA_FX_REVERB || c.d == &TP[P_REV]), "SENDS: the master sends REV DLY CHO (REV first, the one order)");
    check(ins_row("BUS SENDS") >= SCR->rows(), "BUS SENDS: not a synth track's row");
    song.sel = TRK_DRUM;
    r = ins_row("SENDS");
    SCR->cell(r < SCR->rows() ? r : 0u, 0, &c);
    check(r < SCR->rows() && (!FELUCCA_FX_REVERB || (c.vp == &dsend_kit[0] && !strcmp(c.val, "100") && c.d->fmt == F_PCT)),
          "the drum track's SENDS: its send LEVELS (REV at 100 %), not ON / OFF");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    turn(EN_K1, -10);
    check(!FELUCCA_FX_REVERB || (dsend_kit[0] < DKS_DEF && dsend_kit[2] == DKS_DEF), "... KNOB 1 turned: the REV level down, CHO's kept");
    dsend_kit[0] = dsend_kit[1] = dsend_kit[2] = DKS_DEF;
    r = ins_row("BUS SENDS");
    SCR->cell(r < SCR->rows() ? r : 0u, 0, &c);
    check(r < SCR->rows() && (!FELUCCA_FX_REVERB || c.vp == &TDRUM->p[P_REV]), "... BUS SENDS: the drum bus's own sends (REV first)");
    r = ins_row("INSERTS");
    SCR->cell(r < SCR->rows() ? r : 0u, 1, &c);
    check(r < SCR->rows() && (!FELUCCA_FX_DIST || c.vp == &TDRUM->p[P_DIST]), "... INSERTS: the drum bus's slots (S1's DST the bus's)");
#if FELUCCA_MACROS && FELUCCA_FX_DIST
    {   /* the cards' two bars (op_effval.c): ENERGY moves a part's DST, the INSERTS card shows the actual value */
        song.sel = 0;
        r = ins_row("INSERTS");
        ui.row[SCR_SOUND] = (uint8_t)r;
        trk[0].p[P_DIST] = 10;
        TDRUM->p[MAC_ID[3]] = 60;                       /* (ENERGY: macro.c MK_PART P_DIST) */
        frames(2);
        SCR->cell(r, 1, &c);
        effv_take(1, &c);
        check(c.d == &TP[P_DIST] && c.ak != GK_NONE, "INSERTS: S1's DST moved by ENERGY: its card's second bar (the actual value)");
        TDRUM->p[MAC_ID[3]] = 0, trk[0].p[P_DIST] = 0;
    }
#endif
    song.sel = 0;
    reset_ui();
}
