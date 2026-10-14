/* SPDX-License-Identifier: GPL-3.0-only */
/* ALGORITHM's walk and a drum lane's own sound (docs/UI-OPTIMIST-DESIGN.md section 16; the user, 2026-10-12: "the
 * algo button should work the same on all pages, master (if it makes sense), t1, t2,t3,t4 and then individual drum
 * lanes (if it makes sense)", "really the algo knob should select the track and drum sub track every where", "i want
 * uniformity"; "the preset on a drum lane should change the drum lane preset, not the kit").
 *   the walk   one walk, the same on every page and layer, the mixer's rows: MASTER, T1 T2 T3, DR, then the 16 lanes and the ACCENT lane,
 *              stopping at both ends. MASTER: on the mixer its row; in a layer ui.master (the master FX's cards); on
 *              any other page the master FX screen (SCR_FX), which turned right goes back to the page it was left
 *              from (SCOPE is the exception: MASTER is its source, ui.master, and it stays). A lane: the page's view of it (SOUND its pages, STEP its steps; a family the drum track has none
 *              of: every row, op_screens.c snd_family). A held step (op_step.c), NAME (op_name.c) and a free take eat it
 *   selected   the drum track's selection is DR itself or one of its lanes (ui.lane; lane_picked: op_mixer.c); HOME + a key
 *              still picks a lane straight away
 *   PRESETS    on DR the kit (op_screens.c op_preset_step). On a lane by its SOURCE (the user, 2026-10-12: "SLOT: kit,
 *              or specific instrument. if Slot=kit. preset knob changes the preset kit, if Slot=something else preset
 *              changes the preset for the drum lane"): SRC KIT, the kit as on DR (HOME + PRESETS the kits too); any
 *              other source, that lane's sound only: the next / previous source of the list the drum page's SOURCE row
 *              steps (dsnd_desc.c DSD SRC: USR1..3, each kit's sound for this lane, the X0X voices; KIT left out, so
 *              it stays the lane's own), one a detent, round, the sources this build has; HOME held + PRESETS: the
 *              first sound of the next / previous kind, round (the colour language's: your samples, a sampled kit's,
 *              a synthesised kit's, the X0X machines'; sloop/ui_colors.c lane_col) */

/* ---- the walk */
static void op_enter(uint32_t scr);                     /* op_input.c */
static void op_row_pick(uint32_t r);
static int snd_has_fam(uint32_t fam);
static void op_jump_sound(uint32_t fam);
/* SOUND and the FX screen share the bands: the step between them is drawn in place like a track change (op_state.c
 * op_repaint), not through op_enter's clear to black */
static void walk_inplace(void)
{
    ui.force = 0;
    op_repaint();
}
static uint8_t fx_by_walk;                              /* the master FX screen was reached by the walk (op_input.c algo_has_track) */
static void walk_master_in(void)                        /* a page to MASTER: the FX screen; the page, family, row kept */
{
    uint8_t scr = ui.scr == SCR_TEMPO ? tp.prev : ui.scr, fam = snd_fam, row = ui.row[scr % SCR_N];   /* (the TEMPO page:
                                                         * the screen under it; MASTER closes the page) */
    if (scr == SCR_TEMPO || scr == SCR_FX)
        scr = SCR_SOUND;
    op_enter(SCR_FX);
    fx_by_walk = 1;
    ui.m_scr = scr;
    ui.m_fam = fam;
    ui.m_row = row;
    walk_inplace();
}
static void walk_master_out(void)                       /* MASTER to T1: the page where it was left */
{
    uint8_t scr = ui.m_scr, fam = ui.m_fam, row = ui.m_row;
    op_enter(scr);
    track_select(0);
    if (scr == SCR_SOUND) {
        if (fam != SND_ALL && (fam == SND_FM6 ? fm6_sel() : snd_has_fam(fam)))
            op_jump_sound(fam);
        if (row < snd_rows())
            op_row_pick(row);
    } else {
        ui.row[scr] = row;                              /* (op_rows_fix keeps it within the page's rows) */
        op_rows_fix();
    }
    ui.m_scr = SCR_SOUND;
    ui.m_fam = SND_ALL;
    walk_inplace();
}
static uint32_t walk_pos(int lay)                       /* where the walk is: a mixer row number */
{
    if (ui.scr == SCR_HOME)
        return mx_row();
    if (lay || ui.scr == SCR_SCOPE ? ui.master : ui.scr == SCR_FX)
        return MXR_MASTER;
    return mx_sel_row();
}
static void walk_sel(uint32_t p)                        /* the track, DR or a lane of row p selected */
{
    ui.lane = (uint8_t)(p >= MXR_LANE0);
    track_select(mx_trk_of(p));
    if (p >= MXR_LANE0)
        lane_pick(p - MXR_LANE0);                       /* (STEP: a change previews it, stopped; its LANE page shows it) */
    op_repaint();
}
static void walk_to(uint32_t p, int lay)
{
    if (ui.scr == SCR_SCOPE)
        lay = 1;                                        /* SCOPE: MASTER is its source (ui.master), the page stays (op_scope.c) */
    if (lay && ui.master != (p == MXR_MASTER)) {        /* a layer: MASTER a UI state, its cards the master FX */
        ui.master = (uint8_t)(p == MXR_MASTER);
        ui.force = 1;
    }
    if (ui.scr == SCR_HOME) {                           /* the mixer (a layer over it too): its row */
        mx_apply(p);
        return;
    }
    if (p == MXR_MASTER) {
        if (!lay)
            walk_master_in();
        return;
    }
    if (ui.scr == SCR_FX)
        walk_master_out();                              /* (a layer over it too: the page left, else MASTER again) */
    walk_sel(p);
}
/* ALGORITHM turned s detents (lay: in a performance layer): MASTER .. the 16th lane, the same on every page */
static void algo_walk(int32_t s, int lay)
{
    uint32_t p, n;
    if (!algo_has_track())
        return;                                         /* (a page with no track: nothing, op_input.c) */
    p = walk_pos(lay);
    n = (uint32_t)clamp((int32_t)p + s, MXR_MASTER, MXR_N - 1);
    if (n != p)
        walk_to(n, lay);
}

/* ---- a lane's own sound */
enum { LK_KIT, LK_USR, LK_SAMPLED, LK_SYNTH, LK_X0X, LK_N };
static uint32_t ls_count(uint32_t l)                    /* the SOURCE list's length (0: none in this build) */
{
    int16_t *vp;
    const param_desc_t *d = dsnd_desc_lane(l, 8u, &vp);
    return d ? (uint32_t)d->max + 1u : 0u;
}
static uint32_t ls_kind(uint32_t i)                     /* SOURCE entry i's kind */
{
    if (i == 0u)
        return LK_KIT;
    if (i < 4u)
        return LK_USR;
    i -= 4u;                                            /* (a kit UID, then the X0X voices) */
    return i < DRUM_SAMPLED ? LK_SAMPLED : i < DRUM_SYNTH_END ? LK_SYNTH : LK_X0X;
}
static int ls_ok(uint32_t i)                            /* entry i plays in this build (KIT: not a lane's own sound) */
{
    if (i == 0u)
        return 0;
    if (i >= 1u && i <= 3u)
        return FELUCCA_DRUM_USR;
    if (i >= 4u && i < 4u + DRUM_KITS)
        return drum_kit_built(i - 4u);
    return 1;
}
static uint32_t ls_cur(uint32_t l, uint32_t n)
{
    uint32_t i = dsnd_src_idx(dl.src[l]);
    return i < n ? i : 0u;
}
static void ls_set(uint32_t l, uint32_t i)
{
    uint32_t was = ui.force;
    dl.src[l] = (uint8_t)dsnd_idx_src(i);               /* (as the SOURCE row: op_cells.c op_dsnd_set) */
    op_preset_shown(was);                               /* (its pages may change: sampled / synthesised values) */
}
/* PRESETS acts on the lane's own sound: a lane selected whose source is not KIT (KIT: the kit, as on DR) */
static int lane_presets(void)
{
    return lane_picked() && lane_snd() && dl.src[lane_selected()] != DL_KIT;   /* (the ACCENT lane has no sound: PRESETS the kit) */
}
static void lane_sound_step(int32_t s)                  /* PRESETS on a lane: the next / previous source, round */
{
    uint32_t l = lane_selected(), n = ls_count(l), i, j, k;
    if (n < 2u || ed_ask(EK_LSOUND, s))
        return;
    i = ls_cur(l, n);
    for (k = (uint32_t)(s < 0 ? -s : s); k > 0u; k--)
        for (j = 0; j < n; j++) {
            i = s > 0 ? (i + 1u < n ? i + 1u : 0u) : (i ? i - 1u : n - 1u);
            if (ls_ok(i))
                break;
        }
    ls_set(l, i);
    ed_done();
}
static void lane_kind_step(int32_t s)                   /* HOME + PRESETS on a lane: the next / previous kind's first */
{
    uint32_t l = lane_selected(), n = ls_count(l), k0, t, i;
    if (n < 2u || ed_ask(EK_LKIND, s))
        return;
    k0 = ls_kind(ls_cur(l, n));
    for (t = 1; t < LK_N; t++) {                        /* (LK_KIT: no entry is ok, skipped) */
        uint32_t kt = (k0 + (s > 0 ? t : LK_N - t)) % LK_N;
        for (i = 0; i < n; i++)
            if (ls_ok(i) && ls_kind(i) == kt) {
                ls_set(l, i);
                ed_done();
                return;
            }
    }
}
/* lane l's sound by name: the kit's name while it plays the project's kit, else its source's ("X9 SD", "USR2") */
static const char *lane_sound_name(uint32_t l)
{
    uint32_t i = dsnd_src_idx(dl.src[l & 15u]);
    return i ? DS_SRC_NAMES[i] : drum_kit_name();
}
