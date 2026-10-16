/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the Optimist UI's STEP TOOLS and EUCLID pages (ui/optimist/op_tools.c; included by editor.c; web/EDITOR_PROTOCOL.md
 * "v13: sequence tools"; builds with the Optimist UI and FELUCCA_SEQTOOLS). REVERSE, RANDOM, COOK and LENGTH (x2, 1/2) act on the selected
 * track's pattern (a drum track: the whole kit, or one lane), each one undo level, through the device's own function (tl_run: the UI's YES
 * calls the same one), so the steps and their events move exactly as on the device. EUCLID is the note generator (HITS spread over STEPS,
 * turned by ROT, repeated through LEN): ON writes it over the pattern, OFF keeps it or gives the pattern back.
 *   98 TOOLS  op [args] -> op, rc, then the state
 *       state: track, lane (the one EUCLID writes: 0..15), len, hits, steps, rot, on (the generator is on here), then op 1's message: a, b
 *       op 0  -                                 get the state
 *       op 1  verb (0 REVERSE 1 RANDOM 2 COOK 3 LENGTH), lane (127: the whole kit / a synth track), half (LENGTH: 1 = 1/2, 0 = x2)
 *       op 2  k (0 HITS, 1 STEPS, 2 ROT), value            EUCLID's value, written at once while it is on
 *       op 3  lane (127: keep the lane)                   EUCLID ON: the pattern is written (rc 1: another generator is on)
 *       op 4  -                                           EUCLID OFF, the generated steps kept
 *       op 5  -                                           EUCLID OFF, the pattern as it was given back
 * rc 0 done. A bad op: no reply; the selected track is the device's (TRACK, 27). INFO tag 0x5B: version 1. */
enum { ED_TOOLS = 98 };
#if FELUCCA_UI == 1 && FELUCCA_SEQTOOLS && FELUCCA_ED_TOOLS
static void ed_tools_state(uint32_t op, uint32_t rc, const char *sa, const char *sb)
{
    track_t *t = TSEL;
    gen_t g = *ng_cur();                                /* (the slot's settings; EUCLID's three values, ON only for EUCLID) */
    GEN_TYPES[g.type % GT_N].fit(&g, trk_len(t));
    ed_b(op), ed_b(rc), ed_b(song.sel % NTRK), ed_b(lane_selected());
    ed_b(trk_len(t) & 0x7Fu), ed_b(g.p[0]), ed_b(g.p[1]), ed_b(g.p[2]), ed_b(g.on && g.type == GT_EUCLID);
    ed_str(sa, 24);
    ed_str(sb, 24);
}
static int ed_tools(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, rc = 0;
    track_t *t = TSEL;
    gen_t g;
    char b[24];
    const char *sa = "", *sb = "";
    if (cmd != ED_TOOLS || op > 5u)
        return 0;
    if (op == 1u) {
        int32_t lane;
        if (na < 4u || a[1] > TL_LEN)
            return 0;
        lane = a[2] < DRUM_LANES && is_drum(t) ? (int32_t)a[2] : -1;
        if (ng_cur()->on)
            rc = 2u;                                    /* (the generator is on: the pattern is its, turn it off first) */
        else
            (void)tl_run(t, a[1], lane, a[3] != 0u, &sa, &sb, b);
    } else if (op == 2u) {
        if (na < 3u || a[1] > 2u)
            return 0;
        fm1_irq_off();                                  /* (read, changed, written: one section, as the UI's ng_turn) */
        g = *ng_cur();
        if (g.type != GT_EUCLID)
            gen_defaults(&g, GT_EUCLID);
        g.p[a[1]] = (uint8_t)clamp((int32_t)a[2], GEN_TYPES[GT_EUCLID].par[a[1]].min, ng_max(&GEN_TYPES[GT_EUCLID].par[a[1]], trk_len(t)));
        GEN_TYPES[GT_EUCLID].fit(&g, trk_len(t));
        ng_put(&g);
        fm1_irq_on();
    } else if (op == 3u) {                              /* EUCLID ON here (the generator layer: the pattern untouched) */
        if (na >= 2u && a[1] < DRUM_LANES && is_drum(t))
            lane_select(a[1]);
        fm1_irq_off();
        g = *ng_cur();
        if (g.type != GT_EUCLID)
            gen_defaults(&g, GT_EUCLID);
        g.on = 1;
        GEN_TYPES[GT_EUCLID].fit(&g, trk_len(t));
        ng_put(&g);
        fm1_irq_on();
    } else if (op == 4u || op == 5u) {                  /* OFF: 4 writes what plays into the pattern first (one undo level) */
        g = *ng_cur();
        if (g.on && op == 4u && gen_write_prep(t, ng_slot())) {
            tl_mark(t);
            fm1_irq_off();
            (void)gen_write_commit(t);
            t->seq_active = 1;
            fm1_irq_on();
            sync_reload = 1;
        }
        fm1_irq_off();
        g = *ng_cur();
        g.on = 0;
        ng_put(&g);
        fm1_irq_on();
    }
    ui.force = 1;
    ed_tools_state(op, rc, sa, sb);
    return 1;
}
static void ed_tools_info(void)
{
    ed_b(0x5B);
    ed_b(1);
    ed_b(1);
}
#else
static int ed_tools(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_tools_info(void) {}
#endif
