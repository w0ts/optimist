/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, LEN in powers of two and SHIFT = LFO held (docs/UI-OPTIMIST-DESIGN.md section 11.6).
 * Included by ui_optimist_test.c after its helpers. */

static void len_set_turn(int32_t from, uint32_t role, int32_t s)   /* LEN at from, then a turn of s on role */
{
    TSEL->p[P_SLEN] = (int16_t)from;
    turn(role, s);
}
static void len_tests(void)
{
    track_t *t;
    int ok;
    reset_ui();
    song.sel = 0;
    t = TSEL;
    tap(B_SEQ);                                         /* STEP on T1: its first row is PATTERN (LEN DIV SWING GATE) */
    {
        cell_t c;
        uint32_t r, n = SCR->rows();
        for (r = 0; r < n; r++) {
            SCR->cell(r, 0, &c);
            if (c.d == &TP[P_SLEN])
                break;
        }
        ui.row[SCR_STEP] = (uint8_t)r;
        frame();
        check(ui.scr == SCR_STEP && r < n, "LEN: STEP's PATTERN row, KNOB 1");
    }
    len_set_turn(16, EN_K1, 1);
    ok = t->p[P_SLEN] == 32;
    turn(EN_K1, 1);
    ok &= t->p[P_SLEN] == 64;
    turn(EN_K1, 1);
    ok &= t->p[P_SLEN] == 64;                           /* (the last of the list: NSTEP) */
    check(ok, "LEN up: 16, 32, 64, stays at 64");
    len_set_turn(64, EN_K1, -1);
    ok = t->p[P_SLEN] == 32;
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    ok &= t->p[P_SLEN] == 2;
    turn(EN_K1, -1);
    ok &= t->p[P_SLEN] == 1;
    turn(EN_K1, -1);
    ok &= t->p[P_SLEN] == 1;
    check(ok, "LEN down: 64, 32 ... 2, 1, stays at 1");
    len_set_turn(12, EN_K1, 1);
    ok = t->p[P_SLEN] == 16;
    len_set_turn(12, EN_K1, -1);
    ok &= t->p[P_SLEN] == 8;
    len_set_turn(3, EN_K1, 1);
    ok &= t->p[P_SLEN] == 4;
    len_set_turn(33, EN_K1, -1);
    ok &= t->p[P_SLEN] == 32;
    check(ok, "LEN off the list: 12 up 16, down 8; 3 up 4; 33 down 32");
    len_set_turn(4, EN_K1, 2);
    check(t->p[P_SLEN] == 16, "LEN: two detents at once, two list steps (4 to 16)");
    fm1_in.buttons |= BT(B_LFO);                        /* SHIFT: LFO held */
    frame();
    len_set_turn(16, EN_K1, 1);
    ok = t->p[P_SLEN] == 17 && ui.shift;
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    ok &= t->p[P_SLEN] == 15;
    fm1_in.buttons &= ~BT(B_LFO);
    frame();
    check(ok && ui.scr == SCR_STEP, "LFO held + KNOB 1: LEN by one (16, 17, 15); LFO let go is no tap");
    check(!ui.shift, "LFO let go: SHIFT off");
    len_set_turn(16, EN_PRESET, 1);
    check(ui.hot == 0u && t->p[P_SLEN] == 17, "PRESETS on the hot LEN: by one, as on any cell");
    {   /* SHIFT is LEN's only: another cell turned with LFO held moves as ever (DIV: one step) */
        int16_t dv = t->p[P_SDIV];
        fm1_in.buttons |= BT(B_LFO);
        frame();
        turn(EN_K2, 1);
        fm1_in.buttons &= ~BT(B_LFO);
        frame();
        check(t->p[P_SDIV] != dv || dv == TP[P_SDIV].max, "LFO held + KNOB 2 (DIV): its turn as ever");
        t->p[P_SDIV] = dv;
    }
#if FELUCCA_PATTERNS
    {   /* the patterns layer keeps its keys while LFO is held, and loses the knobs' cue */
        uint8_t rq;
        uint32_t k;
        for (k = 0; k < 8u; k++)
            t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(48 + k);
        pat_store_slot(0, 1);
        pat_store_slot(0, 2);
        press(B_LFO);
        frames(HOLD_FRAMES);
        check(lay.shown == LY_PAT && ly_lock == LY_PLAY, "LFO held: the patterns' map");
        rq = pat_req[0];
        len_set_turn(16, EN_K1, 1);
        frame();
        check(pat_req[0] == rq && t->p[P_SLEN] == 17 && lay.shown == LY_PLAY,
              "LFO held + KNOB 1: SHIFT (LEN by one), no pattern cued, the screen under shown");
        key(WK(1));
        check(pat_req[0] == 1 || pat_cur[0] == 1, "... and LFO still held + white 2: T1's pattern 2 launches");
        release(B_LFO);
        check(ui.scr == SCR_STEP, "LFO let go after that: no tap (no LFO rows)");
        frames(4);
        track_defaults_steps(t);
    }
#endif
    t->p[P_SLEN] = 16;
    reset_ui();
    tap(B_LFO);
    check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND]) && snd_page(ui.row[SCR_SOUND])->fam == FAM_LFO,
          "LFO tapped alone: SOUND's LFO family, as before");
    reset_ui();
}
