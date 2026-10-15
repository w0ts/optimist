/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v10: the FX slots (fx/fx_slots.c; included by editor.c; web/EDITOR_PROTOCOL.md "v10: FX slots").
 * INFO tag 0x56 lists the effect types built; a type's amounts stay where they are: a track's (and the drum bus's) are its
 * P_* values (DESC / TRACK_PARAM / the v9 PARAMS push), a drum sound's REV DLY CHO its lane's sends (DRUM_LANE v2). What
 * this command adds: the master slots' layout (the sends), the tracks' insert slots, each track's COMP settings, each
 * drum sound's DIST and COMP amounts, the drum track's send levels.
 *   86 FX       op [args] -> op, then the state (ed_fx_out): master slot x 4 (a type id FXT_*, 0 empty), RATIO ATK REL
 *               (T1's; 127 x 3: no COMP in this build), 16 x the drum sounds' DIST, 16 x their COMP (0..127); then (the
 *               track insert slots, 2026-10-12: an older editor reads the first 39 bytes) T1 T2 T3 DR's S1 S2 (type ids),
 *               T1 T2 T3 DR's RATIO ATK REL (127 x 12 without COMP), the drum track's send levels REV DLY CHO (0..127)
 *       op 0  get
 *       op 1  the master slots: 4 type ids (sends only: an insert id, a type twice (its later slot), an id past the
 *             table: empty; fxs_set). The editor swaps as the device does (fxs_load) and sends the result
 *       op 2  the COMP settings of every track: RATIO ATK REL (each clamped to the master COMP's list)
 *       op 3  a drum sound's insert: lane 0..15, kind (0 DIST, 1 COMP), amount 0..127 (a kind not built: unchanged)
 *       op 4  a track's insert slots: track 0..3, S1, S2 (type ids: an insert, or 0; the same type twice: S2 empty;
 *             the editor swaps as the device does, ins_load)
 *       op 5  a track's COMP settings: track 0..3, RATIO ATK REL
 *       op 6  the drum track's send levels: REV DLY CHO (0..127)
 *   87 FX_PUSH  (v9 push, WATCH bit 2) op 0 and the same state, when it changed on the device (ed_sync9.c ed9_fx) */
enum { ED_FX = 86, ED_FX_PUSH = 87 };
#define ED_FX_N (4u + 3u + 2u * DRUM_LANES + NTRK * INS_N + 3u * NTRK + 3u)   /* the state's bytes after the op */

static uint32_t ed_cset(uint32_t k, uint32_t i)        /* track k's COMP setting i (127: no COMP built) */
{
#if FELUCCA_MASTER_COMP
    return (uint32_t)ins_cset[k % NTRK][i % 3u];
#else
    (void)k, (void)i;
    return 127u;
#endif
}
static void ed_fx_out(uint32_t op)
{
    uint32_t k, l;
    ed_b(op);
    for (k = 0; k < FX_NSLOT; k++)
        ed_b(fxs_slot[k]);
    for (k = 0; k < 3u; k++)
        ed_b(ed_cset(0, k));
    for (k = 0; k < 2u; k++)
        for (l = 0; l < DRUM_LANES; l++)
            ed_b(dins_amt[k][l]);
    for (k = 0; k < NTRK * INS_N; k++)
        ed_b(ins_slot[k / INS_N][k % INS_N]);
    for (k = 0; k < 3u * NTRK; k++)
        ed_b(ed_cset(k / 3u, k % 3u));
    for (k = 0; k < 3u; k++)
        ed_b((uint32_t)dsend_kit[k] & 127u);
}
/* what ed_fx_out sends, as a signature (the v9 push compares it) */
static uint32_t ed_fx_sig(void)
{
    uint32_t h = 0x811C9DC5u, k;
    const uint8_t *p = &dins_amt[0][0];
    for (k = 0; k < FX_NSLOT; k++)
        h = (h ^ fxs_slot[k]) * 16777619u;
    for (k = 0; k < NTRK * INS_N; k++)
        h = (h ^ ins_slot[k / INS_N][k % INS_N]) * 16777619u;
    for (k = 0; k < 3u * NTRK; k++)
        h = (h ^ ed_cset(k / 3u, k % 3u)) * 16777619u;
    for (k = 0; k < 3u; k++)
        h = (h ^ (uint32_t)(uint16_t)dsend_kit[k]) * 16777619u;
    for (k = 0; k < sizeof dins_amt; k++)
        h = (h ^ p[k]) * 16777619u;
    return h;
}

static const uint8_t ED_FX_LID[FXT_N] = {127, 19, 18, 17, 16, 20, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};   /* (fx_slots.c
                                                        * fxs_lane_id's table) */
/* one entry per FX type: a new type (the fx-pool branch adds 18, EQ3) trips this until its entry is added here and in
 * fx_slots.c fxs_lane_id (127: no SOUND 3 value) */
_Static_assert(sizeof ED_FX_LID == FXT_N, "LID: one entry per FX type");

/* INFO tag 0x56: version 1, the types built (n), then per type: id, kind (0 a send bus, 1 an insert), the amount's P_* id
 * (a synth track's and the drum bus's; 127 none), SOUND 3's value id of a drum sound's amount (16 REV, 17 DLY, 18 CHO,
 * 19 DST, 20 CMP; 127 none: FILT) */
static void ed_fx_info(void)
{
    uint32_t t, n = 0;
    for (t = 1; t < FXT_N; t++)
        n += FXT_BUILT >> t & 1u;
    ed_b(0x56);
    ed_b(2u + 4u * n);
    ed_b(1);
    ed_b(n);
    for (t = 1; t < FXT_N; t++)
        if (FXT_BUILT >> t & 1u) {
            ed_b(t);
            ed_b(FXT_INSERT >> t & 1u);
            ed_b(FXT_AMT[t] == 0xFFu ? 127u : FXT_AMT[t]);
            ed_b(t == FXT_DIST && !FELUCCA_FX_DIST ? 127u : ED_FX_LID[t]);
        }
}

#if FELUCCA_MASTER_COMP
static void ed_cset_set(uint32_t k, const uint8_t *a)  /* track k's RATIO ATK REL from a (clamped) */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        ins_cset[k % NTRK][i] = (int16_t)clamp(a[i], GP[G_CRAT + i].min, GP[G_CRAT + i].max);
}
#endif
/* 86 FX: 1 = handled (replied; editor.c redraws the screen), 0 = not this command or bad arguments (no reply) */
static int ed_fxs(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t k, op = na ? a[0] : 127u;
    if (cmd != ED_FX || op > 6u)
        return 0;
    if (op == 1u) {
        uint8_t s[FX_NSLOT];
        if (na < 1u + FX_NSLOT)
            return 0;
        for (k = 0; k < FX_NSLOT; k++)
            s[k] = a[1u + k] < FXT_N ? a[1u + k] : (uint8_t)FXT_NONE;
        fxs_set(s);
    } else if (op == 2u) {
        if (na < 4u)
            return 0;
#if FELUCCA_MASTER_COMP
        for (k = 0; k < NTRK; k++)
            ed_cset_set(k, a + 1);
#endif
    } else if (op == 3u) {
        if (na < 4u || a[1] >= DRUM_LANES || a[2] > 1u)
            return 0;
        if (a[2] == 0u ? FELUCCA_FX_DIST : FELUCCA_MASTER_COMP)
            dins_amt[a[2]][a[1]] = a[3];
    } else if (op == 4u) {
        if (na < 4u || a[1] >= NTRK)
            return 0;
        ins_put(a[1], a + 2);
    } else if (op == 5u) {
        if (na < 5u || a[1] >= NTRK)
            return 0;
#if FELUCCA_MASTER_COMP
        ed_cset_set(a[1], a + 2);
#endif
    } else if (op == 6u) {
        if (na < 4u)
            return 0;
        for (k = 0; k < 3u; k++)
            dsend_kit[k] = (int16_t)(a[1u + k] > DKS_DEF ? DKS_DEF : a[1u + k]);
    }
    ed_fx_out(op);
    return 1;
}
