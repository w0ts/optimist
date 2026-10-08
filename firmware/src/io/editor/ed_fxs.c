/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v10: the FX slots (fx/fx_slots.c; included by editor.c; web/EDITOR_PROTOCOL.md "v10: FX slots").
 * INFO tag 0x56 lists the effect types built; a type's amounts stay where they are: a track's (and the drum bus's) are its
 * P_* values (DESC / TRACK_PARAM / the v9 PARAMS push), a drum sound's REV DLY CHO its lane's sends (DRUM_LANE v2). What
 * this command adds: the slots' layout, the COMP inserts' shared settings and each drum sound's DIST and COMP amounts.
 *   86 FX       op [args] -> op, then the state (ed_fx_out): slot x 4 (a type id FXT_*, 0 empty), RATIO ATK REL (127 x 3:
 *               no COMP in this build), 16 x the drum sounds' DIST, 16 x their COMP (0..127)
 *       op 0  get
 *       op 1  the slots: 4 type ids (as FX > SLOTS stores them: a type twice leaves its later slot empty, fxs_set; an id
 *             past the table: empty). The editor swaps as the device does (fxs_load) and sends the result
 *       op 2  the COMP settings: RATIO ATK REL (each clamped to the master COMP's list)
 *       op 3  a drum sound's insert: lane 0..15, kind (0 DIST, 1 COMP), amount 0..127 (a kind not built: unchanged)
 *   87 FX_PUSH  (v9 push, WATCH bit 2) op 0 and the same state, when it changed on the device (ed_sync9.c ed9_fx) */
enum { ED_FX = 86, ED_FX_PUSH = 87 };
#define ED_FX_N (4u + 3u + 2u * DRUM_LANES)                    /* the state's bytes after the op */

static void ed_fx_out(uint32_t op)
{
    uint32_t k, l;
    ed_b(op);
    for (k = 0; k < FX_NSLOT; k++)
        ed_b(fxs_slot[k]);
    for (k = 0; k < 3u; k++)
#if FELUCCA_MASTER_COMP
        ed_b((uint32_t)fxs_cset[k]);
#else
        ed_b(127);
#endif
    for (k = 0; k < 2u; k++)
        for (l = 0; l < DRUM_LANES; l++)
            ed_b(dins_amt[k][l]);
}
/* what ed_fx_out sends, as a signature (the v9 push compares it) */
static uint32_t ed_fx_sig(void)
{
    uint32_t h = 0x811C9DC5u, k;
    const uint8_t *p = &dins_amt[0][0];
    for (k = 0; k < FX_NSLOT; k++)
        h = (h ^ fxs_slot[k]) * 16777619u;
#if FELUCCA_MASTER_COMP
    for (k = 0; k < 3u; k++)
        h = (h ^ (uint32_t)(uint16_t)fxs_cset[k]) * 16777619u;
#endif
    for (k = 0; k < sizeof dins_amt; k++)
        h = (h ^ p[k]) * 16777619u;
    return h;
}

/* INFO tag 0x56: version 1, the types built (n), then per type: id, kind (0 a send bus, 1 an insert), the amount's P_* id
 * (a synth track's and the drum bus's; 127 none), SOUND 3's value id of a drum sound's amount (16 REV, 17 DLY, 18 CHO,
 * 19 DST, 20 CMP; 127 none: FILT) */
static void ed_fx_info(void)
{
    static const uint8_t LID[FXT_N] = {127, 19, 18, 17, 16, 20, 127};   /* (fx_slots.c fxs_lane_id's table) */
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
            ed_b(t == FXT_DIST && !FELUCCA_FX_DIST ? 127u : LID[t]);
        }
}

/* 86 FX: 1 = handled (replied; editor.c redraws the screen), 0 = not this command or bad arguments (no reply) */
static int ed_fxs(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t k, op = na ? a[0] : 127u;
    if (cmd != ED_FX || op > 3u)
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
        for (k = 0; k < 3u; k++)
            fxs_cset[k] = (int16_t)clamp(a[1u + k], GP[G_CRAT + k].min, GP[G_CRAT + k].max);
#endif
    } else if (op == 3u) {
        if (na < 4u || a[1] >= DRUM_LANES || a[2] > 1u)
            return 0;
        if (a[2] == 0u ? FELUCCA_FX_DIST : FELUCCA_MASTER_COMP)
            dins_amt[a[2]][a[1]] = a[3];
    }
    ed_fx_out(op);
    return 1;
}
