/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (docs: fm1-firmware FX-SLOTS-INSERTS-DESIGN.md; included by params.c after rev_type.c).
 *   - Four slots, the FX page's four knobs. A slot is empty or holds one effect TYPE and is named after it (its
 *     amount's label: DST CHO DLY REV FILT ...). Default, and every project from before: DIST CHO DLY REV.
 *   - One instance per type: a type sits in one slot at most (loading one held elsewhere swaps the two slots).
 *   - The amounts belong to the type, not the slot (decision D3): a synth track's are its P_DIST P_CHOR P_DLY P_REV
 *     (P_TFLT: the FILTER, decision D6), so presets, locks, motion, MACRO SPACE and SLOOP 2.4 keep their meaning
 *     whatever the layout. A type in no slot is not heard and keeps its amounts.
 *   - The inserts run in a fixed order whatever the slots (decision D4): DIST, SLICER, FILTER, COMP, pre-fader.
 * Type ids are stored (the FX record): never reuse one. */
enum { FXT_NONE, FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV, FXT_COMP, FXT_FILT, FXT_N };
#define FX_NSLOT 4u
#define FXT_BIT(x) (1u << (x))
#define FXT_INSERT (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_COMP) | FXT_BIT(FXT_FILT))   /* the others: send buses */
#define FXT_BUILT ((FELUCCA_FX_DIST ? FXT_BIT(FXT_DIST) : 0u) | (FELUCCA_FX_CHORUS ? FXT_BIT(FXT_CHO) : 0u) | \
                   (FELUCCA_FX_DELAY ? FXT_BIT(FXT_DLY) : 0u) | (FELUCCA_FX_REVERB ? FXT_BIT(FXT_REV) : 0u) |  \
                   (FELUCCA_TRK_FILT ? FXT_BIT(FXT_FILT) : 0u) | (FELUCCA_MASTER_COMP ? FXT_BIT(FXT_COMP) : 0u))
/* each type's per-track amount (a P_* id; its label names the slot), 0xFF: none in this build */
static const uint8_t FXT_AMT[FXT_N] = {
    0xFF, P_DIST, P_CHOR, P_DLY, P_REV,
#if FELUCCA_MASTER_COMP
    P_TCOMP,
#else
    0xFF,
#endif
#if FELUCCA_TRK_FILT
    P_TFLT,
#else
    0xFF,
#endif
};
static const uint8_t FXS_DEF[FX_NSLOT] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV};
static uint8_t fxs_slot[FX_NSLOT] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV};   /* (an id this build lacks: kept, unheard) */
/* what the audio reads (fxs_set writes them): the types heard (built, in a slot), and the drum lanes' send level
 * masks REV DLY CHO (drum_sends.c: 31, or 0 while the type is in no slot: level 0 sends nothing) */
#define FXS_LIVE_DEF (FXT_BUILT & (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_CHO) | FXT_BIT(FXT_DLY) | FXT_BIT(FXT_REV)))
static uint8_t fxs_live = FXS_LIVE_DEF;
static uint8_t fxs_lm[3] = {31, 31, 31};
#define FXS_ON(x) (fxs_live >> (x) & 1u)

/* a layout s (the record, the SLOTS page, the editor) into the slots: a type a second time leaves its slot empty */
static void fxs_set(const uint8_t *s)
{
    uint32_t k, j, live = 0;
    for (k = 0; k < FX_NSLOT; k++) {
        uint32_t t = s[k];
        for (j = 0; j < k; j++)
            if (fxs_slot[j] == t)
                t = FXT_NONE;
        fxs_slot[k] = (uint8_t)t;
        if (t < FXT_N)
            live |= FXT_BIT(t) & FXT_BUILT;
    }
    fxs_live = (uint8_t)live;
    fxs_lm[0] = FXS_ON(FXT_REV) ? 31u : 0u;
    fxs_lm[1] = FXS_ON(FXT_DLY) ? 31u : 0u;
    fxs_lm[2] = FXS_ON(FXT_CHO) ? 31u : 0u;
}

/* slot k's amount id (0xFF: empty, or a type this build lacks) */
static uint32_t fxs_amt(uint32_t k)
{
    uint32_t t = fxs_slot[k & 3u];
    return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
}

/* type t into slot k (FX > SLOTS, the editor): a type another slot holds swaps places with this slot's */
static void fxs_load(uint32_t k, uint32_t t)
{
    uint8_t s[FX_NSLOT];
    uint32_t j;
    memcpy(s, fxs_slot, sizeof s);
    for (j = 0; j < FX_NSLOT; j++)
        if (s[j] == t && j != (k & 3u) && t != FXT_NONE)
            s[j] = s[k & 3u];
    s[k & 3u] = (uint8_t)t;
    fxs_set(s);
}

/* the layout of a project without an FX record (one from before, a SLOOP 2.4 import: proj_apply): the default. A
 * track FILTER in use (P_TFLT, decision D6) takes the slot of the type it silences least: the lowest amounts summed
 * over the parts (and the drum lanes' sends), CHO first on a tie, then DLY, DIST, REV. The project then plays as it
 * did, sample for sample, whenever one of the four is unused */
static void fxs_auto(void)
{
    uint8_t s[FX_NSLOT];
    memcpy(s, FXS_DEF, sizeof s);
#if FELUCCA_TRK_FILT
    {
        static const uint8_t ORD[FX_NSLOT] = {1, 2, 0, 3};   /* (slots of the default: CHO DLY DIST REV) */
        uint32_t i, k, best = 1, bv = 0xFFFFFFFFu, any = 0;
        for (k = 0; k < NTRK; k++)
            any |= (uint32_t)(trk[k].p[P_TFLT] != 0);
        for (i = 0; any && i < FX_NSLOT; i++) {
            uint32_t sl = ORD[i], a = FXT_AMT[FXS_DEF[sl]], v = 0;
            for (k = 0; k < NPART; k++)
                v += (uint32_t)(trk[k].p[a] < 0 ? -trk[k].p[a] : trk[k].p[a]);
            for (k = 0; k < DRUM_LANES; k++)
                v += a == P_REV ? dsend_rev(dsend[k]) : a == P_DLY ? dsend_dly(dsend[k]) : a == P_CHOR ? dsend_cho(dsend[k]) : 0u;
            if (v < bv)
                bv = v, best = sl;
        }
        if (any)
            s[best] = FXT_FILT;
    }
#endif
    fxs_set(s);
}

/* ---- what the FX record keeps beside the layout: per type, a TLV (fx_rec.c). The drum track's amounts (the drum bus:
 * fx.c dbus_run, design phase 4) are its P_DIST P_CHOR P_DLY P_REV P_TCOMP, kept here only (project_t's drum slots are
 * stored 0: project.c proj_capture; a project from before reads 0 there whatever its slots held)
 *   COMP   the drum bus's amount, the parts', its RATIO ATK REL (P_TCOMP is past P_E7: not in project_t)
 *   DIST   the drum bus's amount (the parts' are P_DIST in project_t)
 *   CHO DLY REV   the drum bus's send */
#if FELUCCA_MASTER_COMP
_Static_assert(TRK_DRUM == NPART && NTRK == NPART + 1, "the COMP TLV: the drum bus, then the parts 0 .. NPART - 1");
static int16_t fxs_cset[3] = {1, 4, 6};                /* RATIO ATK REL (GP's defaults: params.c) */
#endif
/* the drum bus's amount of type t: its id in the drum track's values (0xFF: none) */
static const uint8_t FXT_BUS[FXT_N] = {0xFF, P_DIST, P_CHOR, P_DLY, P_REV,
#if FELUCCA_MASTER_COMP
                                       P_TCOMP,
#else
                                       0xFF,
#endif
                                       0xFF};
/* type t's TLV payload -> o, its length; 0: nothing to keep. FXT_TLV_SUM: every type's longest, summed (COMP: the
 * drum bus, 3 parts, 3 settings, 16 drum sounds; DIST: the bus, 16 sounds; CHO DLY REV: the bus) */
#define FXT_TLV_SUM (23u + 17u + 3u)
static uint32_t fxs_tlv(uint32_t t, uint8_t *o)
{
    uint32_t k, any = 0;
    if (t >= FXT_N || FXT_BUS[t] == 0xFFu)
        return 0;
    any = (uint32_t)(o[0] = (uint8_t)TDRUM->p[FXT_BUS[t]]);   /* (the drum bus first: every type that has one) */
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP) {
        for (k = 1; k < NTRK; k++)
            any |= (uint32_t)(o[k] = (uint8_t)trk[k - 1u].p[P_TCOMP]);
        for (k = 0; k < 3u; k++)
            any |= (uint32_t)((o[NTRK + k] = (uint8_t)fxs_cset[k]) != (uint8_t)GP[G_CRAT + k].def);
        return any ? NTRK + 3u : 0u;
    }
#endif
    (void)k;
    return any ? 1u : 0u;
}
/* the values the TLVs keep, as a project without them has them (all: the shared settings too) */
static void fxs_untlv_none(int all)
{
    uint32_t k;
    for (k = 0; k < FXT_N; k++)
        if (FXT_BUS[k] != 0xFFu)
            TDRUM->p[FXT_BUS[k]] = 0;
#if FELUCCA_MASTER_COMP
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TCOMP] = 0;
    for (k = 0; all && k < 3u; k++)
        fxs_cset[k] = GP[G_CRAT + k].def;
#endif
    (void)all;
}
/* type t's TLV (n bytes at a) back; a type unknown here, or a shorter TLV's missing part: as none */
static void fxs_untlv(uint32_t t, const uint8_t *a, uint32_t n, int all)
{
    uint32_t k;
    if (t < FXT_N && FXT_BUS[t] != 0xFFu && n >= 1u)
        TDRUM->p[FXT_BUS[t]] = (int16_t)(a[0] > 127u ? 127u : a[0]);
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP && n >= NTRK + 3u) {
        for (k = 1; k < NTRK; k++)
            trk[k - 1u].p[P_TCOMP] = (int16_t)(a[k] > 127u ? 127u : a[k]);
        for (k = 0; all && k < 3u; k++)
            fxs_cset[k] = (int16_t)clamp(a[NTRK + k], GP[G_CRAT + k].min, GP[G_CRAT + k].max);
    }
#endif
    (void)t, (void)a, (void)n, (void)all, (void)k;
}

/* FX > SLOTS: S1..S4, each an enum of the types built (their amounts' labels) and ---- (empty) */
#define FXS_NLIST (1 + FELUCCA_FX_DIST + FELUCCA_FX_CHORUS + FELUCCA_FX_DELAY + FELUCCA_FX_REVERB + FELUCCA_TRK_FILT + \
                   FELUCCA_MASTER_COMP)
static const char *fxs_names[FXS_NLIST];
static uint8_t fxs_list[FXS_NLIST];
static int16_t fxs_v[FX_NSLOT];
static const param_desc_t FXS_DESC[FX_NSLOT] = {
    {"S1", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}, {"S2", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0},
    {"S3", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}, {"S4", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}};
static const param_desc_t *fxs_desc(uint32_t k, int16_t **vp)
{
    uint32_t t, n = 0;
#if FELUCCA_MASTER_COMP
    if (k >= FX_NSLOT) {                               /* CMP's RATIO ATK REL: the master COMP's lists */
        *vp = &fxs_cset[(k - FX_NSLOT) % 3u];
        return &GP[G_CRAT + (k - FX_NSLOT) % 3u];
    }
#endif
    fxs_v[k & 3u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (t = 0; t < FXT_N; t++)                        /* (the list from the table: each type's amount names it) */
        if (t == FXT_NONE || (FXT_BUILT >> t & 1u)) {
            fxs_names[n] = t == FXT_NONE ? "----" : TP[FXT_AMT[t]].label;
            if (fxs_slot[k & 3u] == t)
                fxs_v[k & 3u] = (int16_t)n;
            fxs_list[n++] = (uint8_t)t;
        }
    *vp = &fxs_v[k & 3u];
    return &FXS_DESC[k & 3u];
}
