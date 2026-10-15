/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX record: what a project keeps of the generic FX slots (fx_slots.c), outside project_t (FUNB is full), built
 * as SLOOP 2.4's step extras were (stepx_proj.c, now the automation store's auto_proj.c): a store per project buffer,
 * named by the project's sum, each holding the record's stored form. Included by project.c beside auto_proj.c; in
 * flash: fx_rec_log.c.
 *   proj_capture  the working FX state, encoded, into the store of the buffer it captured into
 *   proj_apply    decoded from the store of the buffer it applies (none, or another project's: the defaults, as a
 *                 project from before plays). The slot layout is the project's (a load, `all`), as REVERB > TYPE.
 *
 *   stored form (version 1)  u8 version, u8 flags, u8 slot[4] (type ids: fx_slots.c FXT_*), then TLVs: u8 type,
 *                            u8 length, the bytes. A reader skips a TLV it does not know (new types need no version
 *                            bump); a newer version is ignored as a whole (the defaults play). TLV DLM_TLV (0x40):
 *                            the drum lanes' PAN, MUTE, SOLO (drums/drum_mix.c), not an FX type. TLV FXRET_TLV
 *                            (0x41): the FX returns CHO DLY REV (below). TLV SMUTE_TLV (0x43): the sequencer mutes
 *                            (seq/seq_mute.c).
 * Nothing to keep (the default layout, no TLV): no record, so most projects write nothing new. */
#define FXR_VER 1u
#if GEN_ON
#define FXR_GEN_N (2u + GEN_TLV_MAX)                   /* (seq/gen.c's TLV at its longest: every slot not at its defaults) */
#else
#define FXR_GEN_N 0u
#endif
#define FXR_HEAD (2u + FX_NSLOT)
#define FXR_MAX (352u + FXR_GEN_N)                     /* the stored form at its longest (FXR_HEAD + the TLVs; 120 -> 156
                                                        * 2026-10-13: + the further inserts' TLVs, 6 x (2 + 4), fx_slots.c
                                                        * fxs_tlv, in every build: a record from any build reads whole;
                                                        * 156 -> 168 2026-10-13: CHRS ECHO, 2 x (2 + 4); 168 -> 186
                                                        * 2026-10-14: GATER, 2 + 4 + its PATTERN RATE SHAPE 12; 186 -> 214 2026-10-14 fx-rework: DUST's 2 + 4, the
                                                        * tracks' DYN GAIN CEIL 8, the master's slots MST_TLV 2 + 12;
                                                        * 214 -> 232: the lanes' slots LIN_TLV 2 + 16; 232 -> 288: room
                                                        * for the TLVs coming (the voice counts' VCNT_TLV 2 + NPART, the Juno chorus) and ~40 B
                                                        * spare: the editor's backup packs them in two objects since,
                                                        * ed_backup.c FXR1 FXR2; 288 -> 352 2026-10-15 fx-pool: the returns'
                                                        * RET_TLV 2 + 9, EQ3's TLV 2 + 50, ~28 spare; + the note generators'
                                                        * GEN_TLV 2026-10-15, seq/gen.c: 2 + 4 a slot, 20 slots) */
/* The track insert slots (2026-10-12, fx_slots.c ins_slot): the header's slot[4] holds the master SENDS, its insert
 * places filled, as far as they go, with the inserts the tracks' slots hold (an older build then hears them on the
 * tracks with an amount: best effort); flags bit 0 (FXR_F_INS) says the record is of this model (the drum sounds' DST CMP
 * always heard: D7). TLV INS_TLV (0x42): T1 T2 T3 DR's S1 S2 (8 type ids), each track's COMP RATIO ATK REL (12; 0 in a
 * build without COMP), the drum track's send levels REV DLY CHO (3, drum_sends.c dsend_kit): written only when the slots
 * are not what ins_map derives from the header (as the record is read back), a track's COMP settings differ from T1's
 * (T1's: the COMP TLV's) or a send level is not 100 %. A load's (`all`): a song section keeps the slots playing.
 * No TLV: ins_map, the old project's layout on the tracks, the same sound (fx_slots.c, the design §4.2) */
#define FXR_F_INS 1u
#define VCNT_TLV 0x47u                                 /* (below) */
/* TLV UT_TLV (0x48, fx-rework stage 3, 2026-10-15): the USB input track (fx_slots.c ut): LEVEL PAN MUTE FX-ON REV DLY CHO
 * (a byte each, PAN signed), its slots' types (2) and amounts (2, signed: FILT's); only when not every one is the default
 * (unity, centre, no slot), in builds with USB audio (others skip it). A load's (`all`) */
#define UT_TLV 0x48u
/* TLV RET_TLV (0x4A, fx-pool 2026-10-15): the returns R1 R2 R3 (fx_slots.c rt_tlv: each slot's type, line and amount), only
 * when one holds a type that is not a send; the header keeps the sends as before. A load's (`all`) */
#define RET_TLV 0x4Au
#define UT_TLV_N 11u
#define INS_TLV 0x42u
#define INS_TLV_N0 (NTRK * INS_N + 3u * NTRK + 3u)    /* (its form before fx-rework: an older reader takes these) */
#define INS_TLV_N (INS_TLV_N0 + 2u * NTRK)           /* + each track's DYN GAIN CEIL (fx_slots.c ins_cset[k][3, 4]) */
_Static_assert(INS_TLV_N0 == 23u && INS_TLV_N == 31u, "the track slots' TLV: 8 + 12 + 3, then 8");
/* TLV MST_TLV (0x44, fx-rework 2026-10-14): the master's insert slots S1 S2 (2 type ids), then the master's amount of DIST
 * and of each further type FOLD .. GATER (mst_amt: the types with no G_* of their own; FILT DUST DYN are G_FILT G_DUST
 * G_CTHR, in project_t),
 * written only when the slots are not what mst_map derives from the project's globals or an amount is set. A load's (`all`).
 * None: mst_map, the master effects an old project used in their old order DUST FILT DYN, the same sound but PUNCH, now
 * before them (it sat between DUST and the DJ filter) */
#define MST_TLV 0x44u
/* TLV LIN_TLV (0x46, fx-rework 2026-10-14): the drum lanes' insert slots (fx_slots.c lin_slot), a byte a lane: S1's index in
 * LIN_T, S2's in the high nibble; only when not every lane is the default S1 DIST, S2 DYN. A load's (`all`). The amounts:
 * DIST's TLV (S1's) and COMP's (S2's), as before */
#define LIN_TLV 0x46u
#define LIN_TLV_N DRUM_LANES
_Static_assert(LIN_NT <= 16u, "LIN_TLV: a type index a nibble");
#define MST_TLV_N (INS_N + 1u + (FXT_NEWN - FXT_NEW0))
AINL uint32_t mst_tlv_t(uint32_t i) { return i ? FXT_NEW0 + i - 1u : (uint32_t)FXT_DIST; }   /* the i-th amount's type */
/* TLV FXRET_TLV (0x41, 2026-10-12): the FX returns, CHO DLY REV (core.h G_CRET G_DRET G_RRET, 0..127), only when one is
 * not at unity (127); none: every return at unity, as every project from before. A load's (`all`), as the layout: a
 * song section keeps the returns playing */
#define FXRET_TLV 0x41u
#define FXRET_N 3u
_Static_assert(FXR_HEAD + 2u * (FXT_N - 1u) + FXT_TLV_SUM + 2u + DLM_TLV_N + 2u + FXRET_N + 2u + INS_TLV_N + 2u + SMUTE_TLV_ACC + 2u + MST_TLV_N + 2u + LIN_TLV_N + 2u + NPART + 2u + UT_TLV_N + 2u + RT_TLV_N + FXR_GEN_N <= FXR_MAX, "the FX record: every TLV fits");
_Static_assert(UT_TLV > VCNT_TLV && RET_TLV > UT_TLV + 1u, "the USB input's TLV, the returns': ids no other takes (0x49: EQ3's)");
#if GEN_ON
_Static_assert(GEN_TLV == RET_TLV + 1u && GEN_TLV_MAX <= 255u, "the note generators' TLV (seq/gen.c): 0x4B, past fx-pool's 0x49 0x4A");
#endif
_Static_assert(MST_TLV > SMUTE_TLV && LIN_TLV > MST_TLV && VCNT_TLV > LIN_TLV, "the master's and the lanes' slots' TLVs: ids no FX type nor other TLV takes");
_Static_assert(DLM_TLV >= FXT_N && FXRET_TLV > DLM_TLV && INS_TLV > FXRET_TLV && SMUTE_TLV > INS_TLV, "the drum lanes' mix TLV (drum_mix.c), the returns', the slots', the sequencer mutes': ids no FX type takes");
_Static_assert(G_DRET == G_CRET + 1 && G_RRET == G_CRET + 2, "the returns' TLV: CHO DLY REV in a row");
static uint32_t fxret_tlv(uint8_t *o)                  /* -> o, its length; 0: every return at unity (nothing to keep) */
{
    uint32_t k, any = 0;
    for (k = 0; k < FXRET_N; k++)
        any |= (uint32_t)((o[k] = (uint8_t)song.g[G_CRET + k]) != 127u);
    return any ? FXRET_N : 0u;
}
static void fxret_untlv(const uint8_t *a, uint32_t n)  /* (n 0: none, every return at unity; a shorter one: as none) */
{
    uint32_t k;
    for (k = 0; k < FXRET_N; k++)
        song.g[G_CRET + k] = (int16_t)(n >= FXRET_N && a[k] < 127u ? a[k] : 127u);
}
/* TLV VCNT_TLV (0x47, 2026-10-14): the parts' VOICES (core.h P_VOICES, 0..8, 0 = the engine's max), a byte a part, only when
 * one is not 0; none: every part at the engine's max, as every project from before. A load's (`all`): a song section
 * keeps the voices playing */
static uint32_t vcnt_tlv(uint8_t *o)                   /* -> o, its length; 0: every part at the engine's max */
{
    uint32_t k, any = 0;
    for (k = 0; k < NPART; k++)
        any |= (uint32_t)((o[k] = (uint8_t)clamp(trk[k].p[P_VOICES], 0, NPOLY)) != 0u);
    return any ? NPART : 0u;
}
static void vcnt_untlv(const uint8_t *a, uint32_t n)   /* (n 0: none, every part at the max; a shorter one: the rest at the max) */
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        trk[k].p[P_VOICES] = (int16_t)(k < n && a[k] <= NPOLY ? a[k] : 0u);
}
/* ---- an old project's layout on the tracks (the design §4.2: the same sound). hdr: the master layout it played (an
 * old record's header; no record: FXS_OLD, its FILTER as fxs_auto gave it a slot, now always: norec). Per track:
 *   1. the inserts the layout heard, in the track's old order (a part DIST FILTER COMP, the drum bus FILTER DIST COMP)
 *   2. kept when the track uses it: its amount set, or an automation event (a lock, motion) on it
 *   3. DIST kept too while it was heard (even at 0: a macro or a later turn of DST is heard as before)
 *   4. into S1, then S2; a third (DIST FILTER COMP all heard and used): dropped, TOOLS > MISS says "3RD INS" (ins_drop)
 *   5. an empty slot then takes the FILTER, when that is silent (built, its amount 0, no event on it): at 0 it is
 *      bypassed, so FX + KNOB 4 works on every old project
 * -> o (the slots), the tracks whose third insert went (a bit each) */
static uint32_t ins_used(uint32_t k, uint32_t id)
{
    if (id >= P_COUNT)
        return 0;
    if (trk[k % NTRK].p[id])
        return 1;
#if FELUCCA_AUTO
    {
        const auto_list_t *l = AUTO_L(k);
        uint32_t i;
        for (i = 0; i < l->n && i < AUTO_MAX; i++)
            if (!AUTO_PSEUDO(l->ev[i].param) && !AUTO_IS_LANE(l->ev[i].param) && mot_id(l->ev[i].param) == id)
                return 1;
    }
#endif
    return 0;
}
static uint32_t ins_map(const uint8_t *hdr, int norec, uint8_t (*o)[INS_N])
{
    static const uint8_t ORD[2][3] = {{FXT_DIST, FXT_FILT, FXT_COMP}, {FXT_FILT, FXT_DIST, FXT_COMP}};
    uint32_t live = norec ? FXT_BIT(FXT_FILT) : 0u, k, i, drop = 0;
    for (i = 0; i < FX_NSLOT; i++)
        live |= hdr[i] < FXT_N ? FXT_BIT(hdr[i]) : 0u;
    live &= FXT_BUILT & FXT_INSERT;
    for (k = 0; k < NTRK; k++) {
        const uint8_t *ord = ORD[k == TRK_DRUM];
        uint32_t n = 0, keep = 0;
        o[k][0] = o[k][1] = FXT_NONE;
        for (i = 0; i < 3u; i++)                       /* (rules 1, 2: heard and in use) */
            if ((live >> ord[i] & 1u) && ins_used(k, FXT_AMT[ord[i]]))
                keep |= FXT_BIT(ord[i]), n++;
        if (n < INS_N && (live >> FXT_DIST & 1u))
            keep |= FXT_BIT(FXT_DIST);                 /* (rule 3: while there is room) */
        for (i = 0, n = 0; i < 3u; i++)
            if (keep >> ord[i] & 1u) {
                if (n < INS_N)
                    o[k][n++] = ord[i];
                else
                    drop |= 1u << k;                   /* (rule 4: a third in use) */
            }
        if ((FXT_BUILT >> FXT_FILT & 1u) && o[k][0] != FXT_FILT && o[k][1] != FXT_FILT && !ins_used(k, FXT_AMT[FXT_FILT])) {
            if (o[k][1] == FXT_NONE)
                o[k][1] = FXT_FILT;
            else if (o[k][0] == FXT_NONE)
                o[k][0] = FXT_FILT;
        }
    }
    return drop;
}
/* the header written: the master sends, each empty place an insert some track's slot holds (DIST FILTER COMP) */
static void fxr_head(uint8_t *h)
{
    static const uint8_t T[3] = {FXT_DIST, FXT_FILT, FXT_COMP};
    uint32_t k, i, j;
    for (k = 0; k < FX_NSLOT; k++)                     /* (a return's other type: RET_TLV, not the header) */
        h[k] = FXT_SEND_OK(fxs_slot[k]) ? fxs_slot[k] : (uint8_t)FXT_NONE;
    for (i = 0, k = 0; i < 3u; i++) {
        uint32_t held = 0;
        for (j = 0; j < NTRK; j++)
            held |= ins_slot[j][0] == T[i] || ins_slot[j][1] == T[i];
        while (held && k < FX_NSLOT && h[k] != FXT_NONE)
            k++;
        if (held && k < FX_NSLOT)
            h[k] = T[i];
    }
}
/* the TLV's payload -> o (INS_TLV_N); 0: nothing to keep (the slots as m, every COMP as T1's, every send level 100 %) */
static uint32_t ins_tlv(uint8_t (*m)[INS_N], uint8_t *o)
{
    uint32_t k, any = 0;
    for (k = 0; k < NTRK * INS_N; k++)
        any |= (uint32_t)((o[k] = ins_slot[k / INS_N][k % INS_N]) != m[k / INS_N][k % INS_N]);
    for (k = 0; k < 3u * NTRK; k++)
#if FELUCCA_MASTER_COMP
        any |= (uint32_t)((o[NTRK * INS_N + k] = (uint8_t)ins_cset[k / 3u][k % 3u]) != (uint8_t)ins_cset[0][k % 3u]);
#else
        o[NTRK * INS_N + k] = 0;
#endif
    for (k = 0; k < 3u; k++)
        any |= (uint32_t)((o[NTRK * INS_N + 3u * NTRK + k] = (uint8_t)dsend_kit[k]) != DKS_DEF);
    for (k = 0; k < 2u * NTRK; k++)
#if FELUCCA_MASTER_COMP
        any |= (uint32_t)((o[INS_TLV_N0 + k] = (uint8_t)ins_cset[k / 2u][3u + k % 2u]) != 0u);
#else
        o[INS_TLV_N0 + k] = 0;
#endif
    return any ? INS_TLV_N : 0u;
}
/* the TLV back (n bytes at a; n 0: none): the slots from it, else from m; the COMP settings and send levels (none: as
 * they are, the COMP TLV's, and 100 %) */
static void ins_untlv(const uint8_t *a, uint32_t n, uint8_t (*m)[INS_N])
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        ins_put(k, n >= NTRK * INS_N ? a + k * INS_N : m[k]);
#if FELUCCA_MASTER_COMP
    for (k = 0; n >= NTRK * INS_N + 3u * NTRK && k < 3u * NTRK; k++)
        ins_cset[k / 3u][k % 3u] = (int16_t)clamp(a[NTRK * INS_N + k], GP[G_CRAT + k % 3u].min, GP[G_CRAT + k % 3u].max);
#endif
    for (k = 0; k < 3u; k++)
        dsend_kit[k] = (int16_t)(n >= INS_TLV_N0 && a[NTRK * INS_N + 3u * NTRK + k] < DKS_DEF ? a[NTRK * INS_N + 3u * NTRK + k] : DKS_DEF);
#if FELUCCA_MASTER_COMP
    for (k = 0; k < 2u * NTRK; k++)                    /* (DYN's GAIN CEIL; none: AUTO, OFF, as the COMP of before) */
        ins_cset[k / 2u][3u + k % 2u] = (int16_t)(n >= INS_TLV_N ? clamp(a[INS_TLV_N0 + k], 0, tdyn_d(3u + k % 2u)->max) : 0);
#endif
}

/* ---- the master's slots of a project with no MST_TLV (above), from its globals: the effects it used (DYN: THRS, GAIN or
 * CEIL set; DUST, FILT: an amount), DYN first, then DUST, FILT, then the unused FILT and DYN while there is room (FILT at
 * 0 is bypassed: the knob works on every old project), each in the old order DUST FILT DYN -> o; 1: a third used (all
 * three: FILT is left out, the limiter kept; ins_drop's master bit) */
static uint32_t mst_map(uint8_t *o)
{
    static const uint8_t PRI[5] = {FXT_DYN, FXT_DUST, FXT_FILT, FXT_FILT, FXT_DYN}, RANK[3] = {FXT_DUST, FXT_FILT, FXT_DYN};
    uint32_t used = 0, pick = 0, n = 0, i;
    if (MST_OK >> FXT_DUST & 1u && song.g[G_DUST])
        used |= FXT_BIT(FXT_DUST);
    if (MST_OK >> FXT_FILT & 1u && song.g[G_FILT])
        used |= FXT_BIT(FXT_FILT);
    if (MST_OK >> FXT_DYN & 1u && (song.g[G_CTHR] || song.g[G_CGAIN] || song.g[G_CCEIL]))
        used |= FXT_BIT(FXT_DYN);
    for (i = 0; i < 5u && n < INS_N; i++) {
        uint32_t t = PRI[i];
        if (!(pick >> t & 1u) && (i < 3u ? used >> t & 1u : MST_OK >> t & 1u))
            pick |= FXT_BIT(t), n++;
    }
    o[0] = o[1] = FXT_NONE;
    for (i = 0, n = 0; i < 3u; i++)
        if (pick >> RANK[i] & 1u)
            o[n++] = RANK[i];
    return (used & ~pick) != 0u;
}
static uint32_t lin_ix(uint32_t t)                    /* type t's index in LIN_T (none: 0) */
{
    uint32_t i;
    for (i = 1; i < LIN_NT; i++)
        if (LIN_T[i] == t)
            return i;
    return 0;
}
static uint32_t lin_tlv(uint8_t *o)                    /* -> o (LIN_TLV_N); 0: every lane the default */
{
    uint32_t l;
    if (lin_dflt())
        return 0;
    for (l = 0; l < DRUM_LANES; l++)
        o[l] = (uint8_t)(lin_ix(lin_slot[l][0]) | lin_ix(lin_slot[l][1]) << 4);
    return LIN_TLV_N;
}
static void lin_untlv(const uint8_t *a, uint32_t n)    /* (n 0: none, the defaults) */
{
    uint32_t l;
    lin_defaults();
    for (l = 0; n >= LIN_TLV_N && l < DRUM_LANES; l++) {
        uint8_t s[INS_N];
        s[0] = (a[l] & 15u) < LIN_NT ? LIN_T[a[l] & 15u] : (uint8_t)FXT_NONE;
        s[1] = (uint32_t)(a[l] >> 4) < LIN_NT ? LIN_T[a[l] >> 4] : (uint8_t)FXT_NONE;
        lin_put(l, s);
    }
}
#if FELUCCA_USB_AUDIO
static uint32_t ut_tlv(uint8_t *o)                     /* -> o (UT_TLV_N); 0: the defaults */
{
    uint32_t k;
    if (ut_dflt() && !ut.amt[0] && !ut.amt[1])
        return 0;
    for (k = 0; k < UTV_N; k++)
        o[k] = (uint8_t)(int8_t)ut.v[k];
    o[UTV_N] = ut.slot[0], o[UTV_N + 1u] = ut.slot[1];
    o[UTV_N + 2u] = (uint8_t)(int8_t)ut.amt[0], o[UTV_N + 3u] = (uint8_t)(int8_t)ut.amt[1];
    return UT_TLV_N;
}
static void ut_untlv(const uint8_t *a, uint32_t n)     /* (n short: the defaults) */
{
    uint32_t k;
    ut_defaults();
    if (n < UT_TLV_N)
        return;
    for (k = 0; k < UTV_N; k++)
        ut.v[k] = (int16_t)clamp((int8_t)a[k], UT_DESC[k].min, UT_DESC[k].max);
    for (k = 0; k < INS_N; k++) {
        uint32_t t = a[UTV_N + k];
        ut.slot[k] = (uint8_t)(t < FXT_N && (UT_OK >> t & 1u) && (!k || t != ut.slot[0] || t == FXT_NONE) ? t : FXT_NONE);
        ut.amt[k] = ut.slot[k] != FXT_NONE && FXT_AMT[ut.slot[k]] != 0xFFu ?
                    (int16_t)clamp((int8_t)a[UTV_N + 2u + k], TP[FXT_AMT[ut.slot[k]]].min, TP[FXT_AMT[ut.slot[k]]].max) : 0;
    }
}
#endif
static uint32_t mst_tlv(const uint8_t *m, uint8_t *o)   /* -> o (MST_TLV_N); 0: nothing to keep (the slots as m, no amount) */
{
    uint32_t k, any = 0;
    for (k = 0; k < INS_N; k++)
        any |= (uint32_t)((o[k] = mst_slot[k]) != m[k]);
    for (k = 0; k < MST_TLV_N - INS_N; k++)
        any |= (uint32_t)(o[INS_N + k] = (uint8_t)mst_amt[mst_tlv_t(k)]);
    return any ? MST_TLV_N : 0u;
}
static void mst_untlv(const uint8_t *a, uint32_t n)    /* (n 0: none, the slots from the globals, no amount) */
{
    uint8_t m[INS_N];
    uint32_t k, drop = mst_map(m);
    mst_put(n >= INS_N ? a : m);
    for (k = 0; k < MST_TLV_N - INS_N; k++)           /* (only MST_TLV's own: JUNO CHORUS's and EQ3's are their TLVs',
                                                        * read before) */
        mst_amt[mst_tlv_t(k)] = (int16_t)(n >= INS_N + k + 1u ? (a[INS_N + k] > 127u ? 127u : a[INS_N + k]) : 0u);
    ins_drop = (uint8_t)((ins_drop & ~(1u << NTRK)) | (n >= INS_N ? 0u : drop << NTRK));
}

#define FXR_AUX 4u
typedef struct {
    uint32_t psum;                                     /* the project this belongs to (0: none) */
    uint16_t n;                                        /* the stored form's length (0: nothing to keep) */
    uint8_t b[FXR_MAX];
} fxr_store_t;
static fxr_store_t fxr_aux[FXR_AUX] __attribute__((section(".pool")));
static const project_t *fxr_aux_p[FXR_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static fxr_store_t *fxr_for(const project_t *p, int mk)
{
    uint32_t i;
    for (i = 0; i < FXR_AUX; i++)
        if (fxr_aux_p[i] == p)
            return &fxr_aux[i];
    for (i = 0; mk && i < FXR_AUX; i++)
        if (!fxr_aux_p[i]) {
            fxr_aux_p[i] = p;
            return &fxr_aux[i];
        }
    return 0;
}
/* the buffer's store if it belongs to p as it is now, else 0 */
static const fxr_store_t *fxr_of(const project_t *p)
{
    const fxr_store_t *m = fxr_for(p, 0);
    return m && m->psum == p->sum ? m : 0;
}

/* the working FX state -> o (FXR_MAX), its length; 0: nothing to keep */
static uint32_t fxr_encode(uint8_t *o)
{
    uint8_t map[NTRK][INS_N];
    uint32_t k, n = FXR_HEAD, any = 0;
    o[0] = FXR_VER;
    o[1] = FXR_F_INS;
    fxr_head(o + 2);                                   /* (the sends, then the inserts held: above) */
    for (k = 0; k < FX_NSLOT; k++)
        any |= (uint32_t)(o[2u + k] != FXS_OLD[k]);
    for (k = 1; k < FXT_N; k++) {                      /* each type's TLV (fx_slots.c fxs_tlv) */
        uint32_t m = fxs_tlv(k, o + n + 2u);
        if (m) {
            o[n] = (uint8_t)k, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the drum lanes' PAN MUTE SOLO (drum_mix.c): a TLV of its own, a reader of before skips it */
        uint32_t m = dlm_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)DLM_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the sequencer mutes (seq/seq_mute.c): a TLV of its own, none when nothing is muted */
        uint32_t m = smute_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)SMUTE_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
#if GEN_ON
    {   /* the note generators (seq/gen.c): a TLV of its own, none when every slot is off at its defaults */
        uint32_t m = gen_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)GEN_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
#endif
    {   /* the parts' VOICES (above) */
        uint32_t m = vcnt_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)VCNT_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
#if FELUCCA_USB_AUDIO
    {   /* the USB input track (above) */
        uint32_t m = ut_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)UT_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
#endif
    {   /* the FX returns (above), as the drum lanes' */
        uint32_t m = fxret_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)FXRET_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the returns holding another type (fx_slots.c rt_tlv) */
        uint32_t m = rt_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)RET_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the master's slots (above), as the tracks' */
        uint8_t mm[INS_N];
        uint32_t m;
        (void)mst_map(mm);
        m = mst_tlv(mm, o + n + 2u);
        if (m) {
            o[n] = (uint8_t)MST_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the drum lanes' slots (above) */
        uint32_t m = lin_tlv(o + n + 2u);
        if (m) {
            o[n] = (uint8_t)LIN_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    {   /* the track slots (above): only when the read-back's ins_map would not give them (no record so far: as one
         * with none is read) */
        uint32_t m;
        (void)ins_map(o + 2, !(any || n > FXR_HEAD), map);
        m = ins_tlv(map, o + n + 2u);
        if (m) {
            o[n] = (uint8_t)INS_TLV, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    return any || n > FXR_HEAD ? n : 0u;
}
/* n bytes at a (0: none) -> the working FX state; all: the slot layouts too, the master's and the tracks' (a load; a
 * song section keeps them). 0 refused (the defaults) */
static int fxr_decode(const uint8_t *a, uint32_t n, int all)
{
    uint32_t at = FXR_HEAD, in = 0, mn = 0, ln = 0, rn = 0;
    const uint8_t *ia = 0, *ma = 0, *la = 0, *ra = 0;
    int ok = n >= FXR_HEAD && n <= FXR_MAX && a[0] == FXR_VER;
    while (ok && at < n)                               /* (the TLVs: checked whole before any is taken) */
        if (at + 2u > n || at + 2u + a[at + 1u] > n)
            ok = 0;
        else
            at += 2u + a[at + 1u];
    fxs_untlv_none(all);
    dlm_none();                                        /* (none: every drum lane at the centre, heard) */
    smute_untlv(0, 0);                                 /* (none: no sequencer muted) */
#if GEN_ON
    gen_untlv(0, 0);                                   /* (none: every note generator off, at its defaults) */
#endif
    if (all) {
        fxret_untlv(0, 0);                             /* (none: every return at unity) */
        vcnt_untlv(0, 0);                              /* (none: every part's VOICES at the engine's max) */
#if FELUCCA_USB_AUDIO
        ut_untlv(0, 0);                                /* (none: the USB input at its defaults) */
#endif
    }
    for (at = FXR_HEAD; ok && at < n; at += 2u + a[at + 1u])
        if (a[at] == DLM_TLV)
            dlm_untlv(a + at + 2u, a[at + 1u]);
        else if (a[at] == SMUTE_TLV)
            smute_untlv(a + at + 2u, a[at + 1u]);
#if GEN_ON
        else if (a[at] == GEN_TLV)
            gen_untlv(a + at + 2u, a[at + 1u]);
#endif
        else if (a[at] == VCNT_TLV) {
            if (all)
                vcnt_untlv(a + at + 2u, a[at + 1u]);
        }
#if FELUCCA_USB_AUDIO
        else if (a[at] == UT_TLV) {
            if (all)
                ut_untlv(a + at + 2u, a[at + 1u]);
        }
#endif
        else if (a[at] == FXRET_TLV) {
            if (all)
                fxret_untlv(a + at + 2u, a[at + 1u]);
        } else if (a[at] == INS_TLV)
            ia = a + at + 2u, in = a[at + 1u];
        else if (a[at] == MST_TLV)
            ma = a + at + 2u, mn = a[at + 1u];
        else if (a[at] == LIN_TLV)
            la = a + at + 2u, ln = a[at + 1u];
        else if (a[at] == RET_TLV)
            ra = a + at + 2u, rn = a[at + 1u];
        else
            fxs_untlv(a[at], a + at + 2u, a[at + 1u], all);   /* (an unknown type: skipped) */
    if (ok && !(a[1] & FXR_F_INS)) {                   /* a record from before the track slots: a drum sound's DST /
                                                        * CMP was heard only with its type in a master slot (rule 8) */
        uint32_t k, live = 0;
        for (k = 0; k < FX_NSLOT; k++)
            live |= a[2u + k] < FXT_N ? FXT_BIT(a[2u + k]) : 0u;
        if (!(live >> FXT_DIST & 1u))
            memset(dins_amt[0], 0, sizeof dins_amt[0]);
        if (!(live >> FXT_COMP & 1u))
            memset(dins_amt[1], 0, sizeof dins_amt[1]);
    }
    if (all) {
        uint8_t m[NTRK][INS_N];
        uint32_t drop = ins_map(ok ? a + 2 : FXS_OLD, !ok, m);   /* (the tracks' values and automation are in) */
        ins_drop = (uint8_t)(ia && in >= NTRK * INS_N ? 0u : drop);
        ins_untlv(ia, ia ? in : 0u, m);
        mst_untlv(ma, ma ? mn : 0u);                   /* (after ins_drop: its master bit) */
        lin_untlv(la, la ? ln : 0u);
        fxs_set(ok ? a + 2 : FXS_OLD);                 /* (the sends; an insert's place: empty) */
        if (ra)
            rt_untlv(ra, rn);                          /* (the returns holding another type) */
        FXI_ASK(2u);                                   /* (a load: every limited instance handed out again in track
                                                        * order, fxi.c: the same project plays the same way) */
    }
    return ok;
}
/* proj_capture, after the sum: the working state into the buffer's store */
static void fxr_capture_store(const project_t *p)
{
    fxr_store_t *m = fxr_for(p, 1);
    if (!m)
        return;
    m->n = (uint16_t)fxr_encode(m->b);
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off) */
static void fxr_apply_store(const project_t *p, int all)
{
    const fxr_store_t *m = fxr_of(p);
    if (!m || !fxr_decode(m->b, m->n, all))
        (void)fxr_decode(0, 0, all);
}
/* the record of the project in buffer p (key first) -> o (4 + FXR_MAX), its length; 0: nothing to keep */
static uint32_t fxr_rec(uint32_t key, const project_t *p, uint8_t *o)
{
    const fxr_store_t *m = fxr_of(p);
    if (!m || !m->n)
        return 0;
    memcpy(o, &key, 4);
    memcpy(o + 4, m->b, m->n);
    return 4u + m->n;
}
/* a record (n bytes at r) into p's store when its key is key, else none */
static void fxr_from_rec(const project_t *p, const uint8_t *r, uint32_t n, uint32_t key)
{
    fxr_store_t *m = fxr_for(p, 1);
    uint32_t k = 0;
    if (!m)
        return;
    if (n >= 4u)
        memcpy(&k, r, 4);
    m->psum = p->sum;
    m->n = (uint16_t)(n > 4u && n <= 4u + FXR_MAX && k == key ? n - 4u : 0u);
    memcpy(m->b, r + 4, m->n);
}
/* the autosave's change test: a hash of the FX state captured with autosave_buf */
static uint32_t fxr_hash(const project_t *p)
{
    const fxr_store_t *m = fxr_of(p);
    return m && m->n ? proj_hash(m->b, m->n) : 0u;
}
