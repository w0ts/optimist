/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, live effective values on the cards (ui/optimist/op_effval.c; the user's decisions,
 * 2026-10-12: one big number, the value as entered; two bars, the entered one and the actual value):
 *   equal to the audio  a drum lane's offsets a hit now plays (op_effval.c effv_lane_ofs) are dl_play_hit's for the patch,
 *                       a hold, a lock over it, the lane's LFO playing and stopped; the absolute value is the hit's
 *                       de_synth; a synth part's cutoff as ANALOG 2 renders it (track_render, v->s[6]: the LFO, the
 *                       envelope, an accent), block after block
 *   the bars            DRIVE -5 on a synthesised kit: the big number -5, the actual bar at the kit's drive - 10; a value
 *                       nothing moves: one bar; a hold in force: the second bar at it; a synth CUT with the LFO: the bar
 *                       moves between frames as a 2 px strip, the number does not; an LFO amount: the destination's
 *                       value on its scale; the lane LFO's AMT: its destination's; a held step's cards: none; a bar
 *                       that comes back to the entered value lingers, then goes
 * Included after the helpers. */
static int effv_white_at(uint32_t x, uint32_t y) { return screen[y * 240u + x] == swap16(C_WHITE); }
/* card k's actual bar on the screen (2x2): lit to av's place, dark past it (GK_BAR) */
static int effv_drawn(uint32_t k, const cell_t *c)
{
    uint32_t x = (k & 1u ? 121u : 3u) + 5u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u) + EFFV_Y2, w = BIG_W - 10u,
             lit = (uint32_t)((c->av - c->amin) * (int32_t)w / (c->amax - c->amin));
    return (lit < 2u || effv_white_at(x + lit / 2u, y)) && (lit + 2u >= w || !effv_white_at(x + lit + 2u, y));
}
#if FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH && (DLA_ON || DLFO_ON)
/* lane l's offsets: op_effval.c's against the hit's (dl_play_hit, then what dl_ofs_play gives the voice) */
static int effv_lane_same(uint32_t l)
{
    int8_t o[DE_N], h[DE_N];
    const int8_t *p;
    effv_lane_ofs(l, o);
    dl_play_hit(l);
    p = dl_ofs_play(l);
    if (p)
        memcpy(h, p, DE_N);
    else
        memset(h, 0, DE_N);
    {   /* the absolute values too: de_synth on the hit's offsets, as dl_ds_on gives the voice */
        int32_t v, mn, mx, semi;
        const dsnd_t *d = DS_SND(&DS_KITS[drum_kit() - DRUM_SAMPLED], ds_lane(LANE_NOTE[l], &semi));
        dsnd_t s;
        de_synth(&s, d, h);
        if (effv_lane(l, DE_DRIVE, &v, &mn, &mx) != 2u || v != s.drive || effv_lane(l, DE_DECAY, &v, &mn, &mx) != 2u ||
            v != s.decay)
            return 0;
    }
    return !memcmp(o, h, DE_N);
}
static void effv_drum_equal_tests(void)
{
    uint32_t l = 2, ok = 1, i;
    reset_ui();
    memset(&dl, 0, sizeof dl);
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    dl.ofs[l][DE_DRIVE] = -5;
    dl.ofs[l][DE_TUNE] = 3;
    ok &= effv_lane_same(l);
#if DLA_ON
    fm1_irq_off();
    dla.hv[l][DE_DECAY] = -20, dla.hm[l] |= 1u << DE_DECAY, dla.any |= 1u;   /* a hold in force (motion) */
    fm1_irq_on();
    ok &= effv_lane_same(l);
    dla.lv[l][DE_DECAY] = 9, dla.lm[l] |= 1u << DE_DECAY, dla.any |= 2u;     /* a lock over it */
    dla.lv[l][DE_DRIVE] = -30, dla.lm[l] |= 1u << DE_DRIVE;
    ok &= effv_lane_same(l);
#endif
#if DLFO_ON
    {
        dlfo_t c = DLFO_DEF;
        c.rate = 90, c.s[0].dest = 1u + DE_DRIVE, c.s[0].amt = 50, c.s[1].dest = 1u + DE_DECAY, c.s[1].amt = -40;
        dlfo_set(l, &c);
        song.playing = 1;
        for (i = 0; i < 64u && ok; i++) {               /* playing: the LFO from the blocks since PLAY */
            song.tick = i * 37u;
            ok &= effv_lane_same(l);
        }
        song.playing = 0;
        for (i = 0; i < 16u && ok; i++) {               /* stopped: from the blocks counted since */
            dlfo_free = i * 101u;
            ok &= effv_lane_same(l);
        }
        c.rate = -3;                                    /* synced (1BAR) */
        dlfo_set(l, &c);
        song.playing = 1;
        for (i = 0; i < 32u && ok; i++) {
            clk_beat = i / 4u, clk_pos = (i % 4u) * (BEAT_U / 4u);
            ok &= effv_lane_same(l);
        }
        song.playing = 0;
        dlfo_set(l, &DLFO_DEF);
    }
#endif
    check(ok, "live values: a lane's offsets and absolute values = the hit's (patch, hold, lock, LFO playing / stopped / synced)");
#if DLA_ON
    dla_holds_off();
    dla_locks_off();
#endif
    memset(&dl, 0, sizeof dl);
}
#else
static void effv_drum_equal_tests(void) {}
#endif
#if FELUCCA_ANALOG2 && FELUCCA_ENG_ANALOG
/* part 0 on ANALOG 2, its cutoff as rendered (v->s[6], keytrack and ENV2 off) against op_effval.c's, blocks after a note:
 * which 1 the LFO alone (effv_synth before the block: the LFO it will use), 2 the envelope alone (after: the env it used),
 * 3 both with an accent (effv_mods with the block's LFO and the voice after it) */
static int effv_cut_same(uint32_t which, uint32_t vel)
{
    track_t *t = &trk[0];
    int32_t o[CTL], val, mn, mx;
    uint32_t b, ok = 1;
    const voice_t *v;
    memset(t->v, 0, sizeof t->v);
    t->p[P_E4] = 50, t->p[P_E7] = 0, t->p[P_A2FENV] = 0;
    t->p[P_L1DA] = LT_FLT, t->p[P_L1AA] = which & 1u ? 90 : 0, t->p[P_ED_FLT] = which & 2u ? -38 : 0;   /* (LFO slot A: FLT) */
    t->p[P_L1DB] = t->p[P_L1AB] = t->p[P_ED_SHP] = 0;
    t->p[P_LRATE] = 100, t->p[P_LWAVE] = 0, t->p[P_LFADE] = 0;
    t->p[P_ATK] = 30, t->p[P_DEC] = 50, t->p[P_SUS] = 40;
    trk_note_on(t, 60, vel);
    for (b = 0; b < 200u && ok; b++) {
        int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
        uint32_t r = which == 1u ? effv_synth(0, P_E4, &val, &mn, &mx) : 1u;
        track_render(t, o, CTL);
        v = effv_voice(t);
        if (!v || v->s[6] == A2_NOCUT)
            continue;
        if (which == 2u)
            r = effv_synth(0, P_E4, &val, &mn, &mx);
        if (which == 3u) {
            effv_mod_t m;
            effv_mods(0, v, lfo, &m);
            val = clamp((t->p[P_E4] << 8) + m.cut, 0, 127 << 8);
            ok &= val == v->s[6];
            continue;
        }
        ok &= r == 1u && val == v->s[6] >> 8;
    }
    trk_all_off(t);
    memset(t->v, 0, sizeof t->v);
    return (int)ok;
}
static void effv_synth_equal_tests(void)
{
    track_t *t = &trk[0], keep = *t;
    reset_ui();
    song.sel = 0;
    set_engine_of(t, 0);                                /* (slot 0: ANALOG) */
    t->engine = t->eng_req, t->xf_on = 0, t->xf = 0;
    check(ENG_IS(ENGINES[t->engine], ANALOG) && effv_cut_same(1, 100) && effv_cut_same(2, 100) && effv_cut_same(3, 100) &&
              effv_cut_same(3, 120), "live values: ANALOG 2's cutoff as rendered (the LFO, the envelope, both, an accent)");
    *t = keep;
}
#else
static void effv_synth_equal_tests(void) {}
#endif

/* a slot's AMT on a value moved for the block (PAN LVL ATK DEC RES E2, an LFO's two slots, LFO 2's on the same value too):
 * op_effval.c's value now equals what voice.c lfo_over_in gave the render (the LFOs at their values of the block before) */
static uint32_t ins_row(const char *title);              /* (ui_optimist_inslots.h) */
static int effv_over_same(const int32_t *dest, uint32_t n, int32_t v1, int32_t v2, uint32_t both)
{
    track_t *t = &trk[0];
    uint32_t i, ok = 1;
    for (i = 0; i < n; i++) {
        int32_t val = 0, mn = 0, mx = 0, vb = 0, mb = 0, xb = 0;
        uint32_t q = P_PAN, r, r2 = 2;
        int32_t mv;
        const param_desc_t *ds = lo_dest(t, dest[i], &q);
        t->p[P_L1DA] = (int16_t)dest[i], t->p[P_L1AA] = 90, t->p[P_L1DB] = (int16_t)(both ? dest[i] : LT_OFF), t->p[P_L1AB] = both ? -50 : 0;
#if FELUCCA_LFO2
        t->p[P_L2DA] = (int16_t)(both == 2u ? dest[i] : LT_OFF), t->p[P_L2AA] = both == 2u ? 70 : 0, t->p[P_L2DB] = t->p[P_L2AB] = 0;
        lfo2[0].v = v2;
#else
        (void)v2;
#endif
        t->lfo_val = (int16_t)v1, t->lfo_fade = 32767;
        lfo_over_in();
        mv = t->p[q];                                   /* (what the render got) */
        lfo_over_out();
        r = effv_synth(0, P_L1AA, &val, &mn, &mx);      /* (the authored values: as the UI reads them) */
        if (both)
            r2 = effv_synth(0, P_L1AB, &vb, &mb, &xb);
        ok &= ds && r == 2u && r2 == 2u && val == mv && mn == ds->min && mx == ds->max && (!both || vb == val);
        ok &= mv != t->p[q] || ds->max <= ds->min || v1 == 0;   /* (it moved: the authored value is back) */
    }
    return (int)ok;
}
static void effv_over_tests(void)
{
    static const int32_t D[] = {LT_PAN, LT_LVL, LT_ATK, LT_DEC, LT_RES, LT_E2, LT_E1};
    track_t *t = &trk[0], keep = *t;
    cell_t c;
    uint32_t r;
    reset_ui();
    song.sel = 0;
    set_engine_of(t, 0);                                /* (slot 0: ANALOG) */
    t->engine = t->eng_req, t->xf_on = 0, t->xf = 0, t->pold = 0;
    t->p[P_PAN] = 5, t->p[P_LEVEL] = 80, t->p[P_ATK] = 40, t->p[P_DEC] = 50, t->p[P_E0] = 2, t->p[P_E1] = 60, t->p[P_E3] = 20;
    for (r = 0; r < 8u; r++)                            /* (every EDIT value mid-range: room to move both ways) */
        t->p[P_E0 + r] = (int16_t)((ENGINES[t->engine]->edit[r].min + ENGINES[t->engine]->edit[r].max) / 2);
    check(effv_over_same(D, 7, 32767, 0, 0) && effv_over_same(D, 7, -20000, 0, 0) && effv_over_same(D, 7, 12345, 0, 1),
          "live values: PAN LVL ATK DEC RES E1 E2 on slot A (and B on the same) as the render moved them");
#if FELUCCA_LFO2
    check(effv_over_same(D, 7, 9000, -30000, 2) && effv_over_same(D, 7, 32767, 32767, 2),
          "... with LFO 2's slot on the same value too (the two LFOs in turn)");
#endif
    t->p[P_L1DA] = LT_PAN, t->p[P_L1AA] = 60, t->p[P_L1DB] = LT_OFF, t->p[P_L1AB] = 0;
    t->lfo_val = 20000, t->lfo_fade = 32767;
    r = ins_row("LFO DEST");
    ui.row[SCR_SOUND] = (uint8_t)r;
    ui.force = 1;
    frame();
    SCR->cell(r, 1, &c);
    check(effv_cell(&c) != 0u && c.ak == GK_BIP && c.amin == TP[P_PAN].min && c.amax == TP[P_PAN].max,
          "the AMT card on PAN: a live bar on PAN's own scale (bipolar)");
    t->p[P_L1AA] = 0;
    SCR->cell(r, 1, &c);
    check(effv_cell(&c) == 0u && c.ak == GK_NONE, "AMT 0: no live bar");
    t->p[P_L1DA] = LT_E1, t->p[P_L1DB] = LT_XRATE;
    SCR->cell(r, 0, &c);
    check(!strcmp(c.val, ENGINES[t->eng_req]->edit[0].label), "the DEST A card on E1: the engine's own name of its first EDIT value");
#if FELUCCA_LFO2
    SCR->cell(r, 2, &c);
    check(!strcmp(c.val, "L2 RT"), "the DEST B card on XRATE: \"L2 RT\"");
#endif
    *t = keep;
    reset_ui();
}
static void effv_bar_tests(void)
{
    cell_t c;
#if FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
    {
        int32_t semi;
        const dsnd_t *d;
        uint32_t l = 1, r;
        reset_ui();
        memset(&dl, 0, sizeof dl);
        song.sel = TRK_DRUM;
        TDRUM->p[P_E0] = DRUM_SAMPLED;
        {   /* a kit lane whose sound has a drive near 67: DRIVE -5 plays about 45 % (57 of 127) */
            uint32_t k, bk = 0, bl = 0, best = 999;
            for (k = 0; k < DS_NKITS; k++)
                for (l = 0; l < DRUM_LANES; l++) {
                    uint32_t dv = DS_SND(&DS_KITS[k], ds_lane(LANE_NOTE[l], &semi))->drive, e = dv > 67u ? dv - 67u : 67u - dv;
                    if (drum_kit_built(DRUM_SAMPLED + k) && e < best)
                        best = e, bk = k, bl = l;
                }
            TDRUM->p[P_E0] = (int16_t)(DRUM_SAMPLED + bk);
            l = bl;
        }
        d = DS_SND(&DS_KITS[drum_kit() - DRUM_SAMPLED], ds_lane(LANE_NOTE[l], &semi));
        lane_select(l);
        ui.lane = 1;
        op_enter(SCR_SOUND);
        for (r = 0; r < SCR->rows() && !(snd_page(r) && snd_page(r)->scope == SC_DSND && snd_page(r)->id[0] == 4u); r++)
            ;
        ui.row[SCR_SOUND] = (uint8_t)r;                 /* SOUND 2: BEND CUT DRIVE LEVEL */
        frame();
        dl.ofs[l][DE_DRIVE] = -5;                       /* (shown -7 %: F_BIPCT, -5 of 64) */
        ui.hot_lit = 0;
        ui.force = 1;
        frames(2);
        SCR->cell(r, 2, &c);
        effv_take(2, &c);
        check(!strcmp(c.val, "-7") && c.ak == GK_BAR && c.av == clamp(d->drive - 10, 0, 127) && effv_drawn(2, &c),
              "DRIVE -5: the number as entered (-7 %), the actual bar at the kit's drive - 10 (absolute)");
        printf("  (kit %u lane %u: the kit's DRIVE %u, the actual %d of 127)\n", drum_kit(), l, d->drive, c.av);
        ppm("opt-effval-drive");
        SCR->cell(r, 3, &c);                            /* LEVEL: nothing moves it */
        effv_take(3, &c);
        check(c.ak == GK_NONE, "LEVEL, nothing moving it: one bar, as before");
#if DLA_ON
        fm1_irq_off();
        dla.hv[l][DE_LEVEL] = -6, dla.hm[l] |= 1u << DE_LEVEL, dla.any |= 1u;
        fm1_irq_on();
        SCR->cell(r, 3, &c);
        effv_take(3, &c);
        check(c.ak == GK_BIP && c.av == -6 && c.gv == 0, "... a hold in force: the second bar at it, the number still 0");
        dla_holds_off();
        frames(30);                                     /* (past the linger) */
        SCR->cell(r, 3, &c);
        effv_take(3, &c);
        check(c.ak == GK_NONE, "... let go: one bar again (after the linger)");
#endif
        memset(&dl, 0, sizeof dl);
    }
#endif
#if LANE_LFO_UI
    {
        dlfo_t f = DLFO_DEF;
        reset_ui();
        song.sel = TRK_DRUM;
        TDRUM->p[P_E0] = DRUM_SAMPLED;
        lane_select(3);
        ui.lane = 1;
        f.s[0].dest = 1u + DE_DECAY, f.s[0].amt = 40;
        dlfo_set(3, &f);
        op_enter(SCR_SOUND);
        tap(B_LFO);
        tap(B_LFO);                                     /* LFO DEST */
        SCR->cell(ui.row[SCR_SOUND], 1, &c);
        effv_take(1, &c);
        check(c.ak == GK_BAR && c.amax == 127, "the lane LFO's AMT: its destination's actual value (DECAY, absolute 0..127)");
        SCR->cell(ui.row[SCR_SOUND], 3, &c);
        effv_take(3, &c);
        check(c.ak == GK_NONE, "... AMT2, no destination: one bar");
        dlfo_set(3, &DLFO_DEF);
    }
#endif
    {   /* a synth CUT with the LFO moving: the bar moves (a 2 px strip), the number does not */
        track_t *t = &trk[0], keep = *t;
        uint32_t r, x0, y, i, n = 0, moved = 0, num0;
        int16_t av0;
        reset_ui();
        song.sel = 0;
        set_engine_of(t, 0);
        t->engine = t->eng_req, t->xf_on = 0, t->xf = 0;
        t->p[P_E4] = 64, t->p[P_L1DA] = LT_FLT, t->p[P_L1AA] = 100, t->p[P_LRATE] = 110, t->p[P_LWAVE] = 0, t->p[P_LFADE] = 0;
        op_enter(SCR_SOUND);
        for (r = 0; r < SCR->rows() && !(snd_page(r) && snd_page(r)->scope == SC_ENGINE && snd_page(r)->id[0] == P_E4); r++)
            ;
        if (r >= SCR->rows())                           /* (the cutoff on EDIT's first page: P_E4 its first card or not) */
            for (r = 0; r < SCR->rows(); r++) {
                const page_t *pg = snd_page(r);
                if (pg && pg->scope == SC_ENGINE && (pg->id[0] == P_E4 || pg->id[1] == P_E4 || pg->id[2] == P_E4 || pg->id[3] == P_E4))
                    break;
            }
        ui.row[SCR_SOUND] = (uint8_t)r;
        for (i = 0; i < 4u && snd_page(r) && snd_page(r)->id[i] != P_E4; i++)
            ;
        ui.force = 1;
        frames(3);
        SCR->cell(r, i, &c);
        effv_take(i, &c);
        av0 = c.av;
        num0 = (uint32_t)c.gv;
        x0 = (i & 1u ? 121u : 3u) + 5u, y = OY_CARD + (i & 2u ? BIG_H + 1u : 0u) + EFFV_Y2;
        {
            uint16_t row0[110];
            memcpy(row0, &screen[y * 240u + x0], sizeof row0);
            ppm("opt-effval-cut-a");
            frames(9);
            moved = memcmp(row0, &screen[y * 240u + x0], sizeof row0) != 0;
        }
        ppm("opt-effval-cut-b");
        SCR->cell(r, i, &c);
        effv_take(i, &c);
        n = c.gv == (int32_t)num0 && c.av != av0;
        {
            cell_t dc = c;                              /* (the bar as last drawn: the LFO moved since) */
            dc.av = ev.av[i];
            n &= effv_drawn(i, &dc);
        }
        check(c.ak && n && moved,
              "a synth CUT with the LFO: the actual bar moves (its strip redrawn), the number stays the entered 64");
        op_enter(SCR_SOUND);
        tap(B_LFO);
        tap(B_LFO);                                     /* LFO DEST: PIT FLT SHP AMP */
        SCR->cell(ui.row[SCR_SOUND], 1, &c);
        effv_take(1, &c);
        {
            int32_t v, mn, mx;
            (void)effv_synth(0, P_E4, &v, &mn, &mx);
            check(c.ak == GK_BAR && c.amin == mn && c.amax == mx && c.av == v,
                  "the LFO amount FLT: the bar is the cutoff's actual value, on the cutoff's scale");
        }
        ui.force = 1;
        frame();
        ppm("opt-effval-lfo-amt");
        SCR->cell(ui.row[SCR_SOUND], 2, &c);            /* SHP 0: nothing */
        effv_take(2, &c);
        check(c.ak == GK_NONE, "the LFO amount SHP at 0: one bar");
        *t = keep;
    }
    reset_ui();
}
static void effval_tests(void)
{
    effv_drum_equal_tests();
    effv_synth_equal_tests();
    effv_over_tests();
    effv_bar_tests();
}
