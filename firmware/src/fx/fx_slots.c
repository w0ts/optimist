/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (docs: fm1-firmware FX-SLOTS-INSERTS-DESIGN.md, and the per-track insert slots' design,
 * 2026-10-12, its design D1..D9 in docs/UI-OPTIMIST-DESIGN.md "Insert slots"; included by params.c after rev_type.c).
 *   - Effect TYPES: DIST, FILTER, COMP are inserts; CHO, DLY, REV send buses. A type's amount belongs to the type, not
 *     to a slot (D4): a track's P_DIST P_TFLT P_TCOMP, its sends P_CHOR P_DLY P_REV, so presets, locks, motion, MACRO
 *     and SLOOP 2.4 keep their meaning whatever the slots. A type in no slot is not heard and keeps its amounts.
 *   - The four master slots hold the SENDS only (D1): FX > SLOTS, the record's header. One instance per type: a type
 *     held elsewhere swaps the two slots. Default ---- CHO DLY REV.
 *   - Each track (T1 T2 T3 DR) has two INSERT slots, S1 then S2, run in that order after the SLICER (D2): fx.c
 *     ins_part, dbus_run. One type at most once on a track: picking the other slot's type swaps them (D3). A new
 *     track's: S1 DIST, S2 FILTER (D5), both at 0 (bypassed: a compare a block). Old projects: fx_rec.c ins_map.
 *   - Each track's COMP has its own RATIO ATK REL (D6, ins_cset); the drum sounds' COMP takes the drum track's.
 *   - The drum sounds' own DST and CMP (SOUND 3) are always heard, 0 off (D7).
 * Type ids are stored (the FX record): never reuse one. */
enum { FXT_NONE, FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV, FXT_COMP, FXT_FILT,
       FXT_FOLD, FXT_FUZZ, FXT_PHSR, FXT_FLNG, FXT_FRMT, FXT_WIDE,   /* (2026-10-13: fx/insfx/insfx.c, a builder item each) */
       FXT_CHRS, FXT_ECHO,   /* (2026-10-13: insfx.c, the chorus's and a short delay's insert copies: a builder count, fxi.c) */
       FXT_N };
#define FXT_NEW0 FXT_FOLD                              /* the further insert types FXT_NEW0 .. FXT_N - 1 (insfx.c) */
#define FXT_LIM0 FXT_CHRS                              /* the limited ones FXT_LIM0 .. FXT_N - 1 (fxi.c: an instance each) */
#define FX_NSLOT 4u
#define FXT_BIT(x) (1u << (x))
#define FXT_INSERT (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_COMP) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_FOLD) | FXT_BIT(FXT_FUZZ) | \
                    FXT_BIT(FXT_PHSR) | FXT_BIT(FXT_FLNG) | FXT_BIT(FXT_FRMT) | FXT_BIT(FXT_WIDE) | FXT_BIT(FXT_CHRS) | \
                    FXT_BIT(FXT_ECHO))                 /* the others: sends */
#define FXT_BUILT ((FELUCCA_FX_DIST ? FXT_BIT(FXT_DIST) : 0u) | (FELUCCA_FX_CHORUS ? FXT_BIT(FXT_CHO) : 0u) | \
                   (FELUCCA_FX_DELAY ? FXT_BIT(FXT_DLY) : 0u) | (FELUCCA_FX_REVERB ? FXT_BIT(FXT_REV) : 0u) |  \
                   (FELUCCA_TRK_FILT ? FXT_BIT(FXT_FILT) : 0u) | (FELUCCA_MASTER_COMP ? FXT_BIT(FXT_COMP) : 0u) |   \
                   (FELUCCA_INS_FOLD ? FXT_BIT(FXT_FOLD) : 0u) | (FELUCCA_INS_FUZZ ? FXT_BIT(FXT_FUZZ) : 0u) |      \
                   (FELUCCA_INS_PHASER ? FXT_BIT(FXT_PHSR) : 0u) | (FELUCCA_INS_FLANGER ? FXT_BIT(FXT_FLNG) : 0u) | \
                   (FELUCCA_INS_FORMANT ? FXT_BIT(FXT_FRMT) : 0u) | (FELUCCA_INS_WIDTH ? FXT_BIT(FXT_WIDE) : 0u) |  \
                   (FELUCCA_INS_CHORUS > 0 ? FXT_BIT(FXT_CHRS) : 0u) | (FELUCCA_INS_DELAY > 0 ? FXT_BIT(FXT_ECHO) : 0u))
_Static_assert(FXT_N <= 16, "the type masks (ins_live): 16 bits");
#define FXT_SEND_OK(t) ((t) < FXT_N && !(FXT_INSERT >> (t) & 1u) && (t) != FXT_NONE)
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
#define FXT_AMT_NEW(on, id) ((on) ? (uint8_t)(id) : 0xFFu)   /* (the ids exist when any further type is built: core.h) */
#if INS_FX
    FXT_AMT_NEW(FELUCCA_INS_FOLD, P_TFOLD), FXT_AMT_NEW(FELUCCA_INS_FUZZ, P_TFUZZ), FXT_AMT_NEW(FELUCCA_INS_PHASER, P_TPHSR),
    FXT_AMT_NEW(FELUCCA_INS_FLANGER, P_TFLNG), FXT_AMT_NEW(FELUCCA_INS_FORMANT, P_TFRMT), FXT_AMT_NEW(FELUCCA_INS_WIDTH, P_TWIDE),
    FXT_AMT_NEW(FELUCCA_INS_CHORUS > 0, P_TCHRS), FXT_AMT_NEW(FELUCCA_INS_DELAY > 0, P_TECHO),
#else
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
#endif
};
/* the master layout before the track slots, and of a project with no FX record: DIST CHO DLY REV (fx_rec.c ins_map
 * reads an old layout's inserts; a record's header is written in it as far as it can: fx_rec.c fxr_head) */
static const uint8_t FXS_OLD[FX_NSLOT] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV};
static const uint8_t FXS_DEF[FX_NSLOT] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV};
static uint8_t fxs_slot[FX_NSLOT] = {FXT_NONE, FXT_CHO, FXT_DLY, FXT_REV};   /* (an id this build lacks: kept, unheard) */
/* what the audio reads (fxs_set writes them): the sends heard (built, in a slot), and the drum lanes' send level
 * masks REV DLY CHO (drum_sends.c: 31, or 0 while the type is in no slot: level 0 sends nothing) */
#define FXS_LIVE_DEF (FXT_BUILT & (FXT_BIT(FXT_CHO) | FXT_BIT(FXT_DLY) | FXT_BIT(FXT_REV)))
static uint8_t fxs_live = FXS_LIVE_DEF;
static uint8_t fxs_lm[3] = {31, 31, 31};
#define FXS_ON(x) (fxs_live >> (x) & 1u)

/* a layout s (the record, the SLOTS page, the editor) into the master slots: sends only (an insert id: empty, its
 * place is the tracks' now: fx_rec.c ins_map); a type a second time leaves its slot empty */
static void fxs_set(const uint8_t *s)
{
    uint32_t k, j, live = 0;
    for (k = 0; k < FX_NSLOT; k++) {
        uint32_t t = s[k] < FXT_N && (FXT_INSERT >> s[k] & 1u) ? (uint32_t)FXT_NONE : s[k];
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
/* the i-th send heard, in the master slots' order (SENDS, the SLOOP FX page's K2..K4): its type, FXT_NONE past them */
static uint32_t fxs_send(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < FX_NSLOT; k++)
        if (fxs_slot[k] < FXT_N && FXS_ON(fxs_slot[k]) && !i--)
            return fxs_slot[k];
    return FXT_NONE;
}
#define fxs_send_amt(i) (fxs_send(i) == FXT_NONE ? 0xFFu : (uint32_t)FXT_AMT[fxs_send(i)])

/* SOUND 3's cell k (the drum sound picked: drum_sends.c dsend_desc's value id + 16): its DST, then its sends REV DLY
 * CHO in the master slots' order, then its CMP while there is room (DST and CMP always heard: D7); 0xFF none. The
 * default: DST CHO DLY REV, SLOOP's page as it was */
static uint32_t fxs_lane_id(uint32_t k)
{
    static const uint8_t LID[FXT_N] = {0xFF, 19, 18, 17, 16, 20, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   /* (by type: NONE DIST
                                                        * CHO DLY REV COMP FILT, the further inserts: none) */
    uint32_t i, n = 0;
    if (FELUCCA_FX_DIST && n++ == (k & 3u))
        return 19u;
    for (i = 0; i < 3u && fxs_send(i) != FXT_NONE; i++)
        if (n++ == (k & 3u))
            return LID[fxs_send(i)];
    if (FELUCCA_MASTER_COMP && n == (k & 3u))
        return 20u;
    return 0xFFu;
}

/* type t into master slot k (FX > SLOTS, the editor): a send another slot holds swaps places with this slot's; an
 * insert: refused (the tracks' slots) */
static void fxs_load(uint32_t k, uint32_t t)
{
    uint8_t s[FX_NSLOT];
    uint32_t j;
    if (t != FXT_NONE && !FXT_SEND_OK(t))
        return;
    memcpy(s, fxs_slot, sizeof s);
    for (j = 0; j < FX_NSLOT; j++)
        if (s[j] == t && j != (k & 3u) && t != FXT_NONE)
            s[j] = s[k & 3u];
    s[k & 3u] = (uint8_t)t;
    fxs_set(s);
}

/* ---- each track's two insert slots (S1, S2; T1 T2 T3 DR). The audio reads ins_slot once a block (fx.c ins_part,
 * dbus_run); the main loop writes them with the interrupts off (a swap is two bytes) */
#define INS_N 2u
#define INS_DEF0 (FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE)
#define INS_DEF1 (FELUCCA_TRK_FILT ? FXT_FILT : FXT_NONE)
_Static_assert(NTRK == 4, "ins_slot: T1 T2 T3 DR");
static uint8_t ins_slot[NTRK][INS_N] = {{INS_DEF0, INS_DEF1}, {INS_DEF0, INS_DEF1}, {INS_DEF0, INS_DEF1},
                                        {INS_DEF0, INS_DEF1}};
/* the types each track's slots hold and this build has (what is heard): the guard, the pages, the 2.4 export */
#define INS_LIVE_DEF ((FXT_BIT(INS_DEF0) | FXT_BIT(INS_DEF1)) & FXT_BUILT & FXT_INSERT)
static uint16_t ins_live[NTRK] = {INS_LIVE_DEF, INS_LIVE_DEF, INS_LIVE_DEF, INS_LIVE_DEF};
#define INS_ON(k, x) (ins_live[(k) % NTRK] >> (x) & 1u)
static uint8_t ins_drop;                               /* the tracks (a bit each) whose third insert the last load's
                                                        * mapping dropped (fx_rec.c ins_map; TOOLS > MISS: miss.c) */
#if INS_LIM
/* the limited types' instances (fx/insfx/fxi.c) follow the slots in the main loop: a change of slots asks for it (1), a
 * project load for every instance handed out again in track order (2: deterministic). Written anywhere (a load may run
 * in the ISR), read and cleared by fxi_poll only */
static volatile uint8_t fxi_dirty;
#define FXI_ASK(v) (fxi_dirty |= (uint8_t)(v))
#else
#define FXI_ASK(v) ((void)0)
#endif

/* track k's slots from s (the record, the editor, ins_load): an insert id (kept even when this build lacks it,
 * unheard), else empty; the same type a second time: empty */
static void ins_put(uint32_t k, const uint8_t *s)
{
    uint32_t a = s[0] < FXT_N && (FXT_INSERT >> s[0] & 1u) ? s[0] : (uint32_t)FXT_NONE;
    uint32_t b = s[1] < FXT_N && (FXT_INSERT >> s[1] & 1u) && s[1] != a ? s[1] : (uint32_t)FXT_NONE;
    k %= NTRK;
    fm1_irq_off();
    ins_slot[k][0] = (uint8_t)a, ins_slot[k][1] = (uint8_t)b;
    ins_live[k] = (uint16_t)((FXT_BIT(a) | FXT_BIT(b)) & FXT_BUILT & FXT_INSERT);
    FXI_ASK(1u);                                       /* (a limited type's instance: fxi.c, the main loop) */
    fm1_irq_on();
}
/* track k's slot holding type t: 1 S1, 2 S2, 0 none (or not built) */
static uint32_t ins_has(uint32_t k, uint32_t t)
{
    k %= NTRK;
    return !(t < FXT_N && INS_ON(k, t)) ? 0u : ins_slot[k][0] == t ? 1u : 2u;
}
/* type t into track k's slot s (INSERTS, the mixer, the editor): the type the other slot holds swaps them (D3) */
static void ins_load(uint32_t k, uint32_t s, uint32_t t)
{
    uint8_t v[INS_N];
    k %= NTRK, s &= 1u;
    v[0] = ins_slot[k][0], v[1] = ins_slot[k][1];
    if (t != FXT_NONE && v[s ^ 1u] == t)
        v[s ^ 1u] = v[s];
    v[s] = (uint8_t)t;
    ins_put(k, v);
}
/* track k's slot s's amount (a P_* id: the type's), 0xFF: empty or not built */
static uint32_t ins_amt(uint32_t k, uint32_t s)
{
    uint32_t t = ins_slot[k % NTRK][s & 1u];
    return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
}
/* every track's slots the default (a new project, the host tests) */
static void ins_defaults(void)
{
    static const uint8_t D[INS_N] = {INS_DEF0, INS_DEF1};
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        ins_put(k, D);
}

/* ---- what the FX record keeps beside the layout: per type, a TLV (fx_rec.c). The drum track's amounts (the drum bus:
 * fx.c dbus_run, design phase 4) are its P_DIST P_CHOR P_DLY P_REV P_TCOMP, kept here only (project_t's drum slots are
 * stored 0: project.c proj_capture; a project from before reads 0 there whatever its slots held)
 *   COMP   the drum bus's amount, the parts', a RATIO ATK REL (before the per-track settings, every track's: now T1's,
 *          for an older reader; the tracks' own are in the track slots' TLV, fx_rec.c), then the 16 drum sounds'
 *   DIST   the drum bus's amount (the parts' are P_DIST in project_t), then the 16 drum sounds' (as COMP's)
 *   CHO DLY REV   the drum bus's send */
#if FELUCCA_MASTER_COMP
_Static_assert(TRK_DRUM == NPART && NTRK == NPART + 1, "the COMP TLV: the drum bus, then the parts 0 .. NPART - 1");
/* each track's COMP RATIO ATK REL (D6; GP's defaults: params.c G_CRAT G_CATK G_CREL), the drum sounds' the drum track's */
static int16_t ins_cset[NTRK][3] = {{1, 4, 6}, {1, 4, 6}, {1, 4, 6}, {1, 4, 6}};
#endif
/* the drum bus's amount of type t: its id in the drum track's values (0xFF: none) */
static const uint8_t FXT_BUS[FXT_N] = {0xFF, P_DIST, P_CHOR, P_DLY, P_REV,
#if FELUCCA_MASTER_COMP
                                       P_TCOMP,
#else
                                       0xFF,
#endif
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   /* (FILT; the further inserts: their
                                                        * own TLV, every track's amount: fxs_tlv) */
/* type t's TLV payload -> o, its length; 0: nothing to keep. FXT_TLV_SUM: every type's longest, summed (COMP: the
 * drum bus, 3 parts, 3 settings, 16 drum sounds; DIST: the bus, 16 sounds; CHO DLY REV: the bus). The drum sounds'
 * amounts (phase 5: drum_sends.c dins_amt) come last and only when one is set: a reader of before takes the rest */
#define FXT_TLV_SUM (23u + 17u + 3u + (FXT_N - FXT_NEW0) * NTRK)   /* (+ the further inserts': 4 each) */
#define FXT_TLV_COMP (NTRK + 3u)                       /* (COMP's TLV before the drum sounds' amounts) */
static uint32_t fxs_lanes(const uint8_t *a, uint8_t *o)   /* the 16 drum sounds' amounts -> o; any set */
{
    uint32_t k, any = 0;
    for (k = 0; k < DRUM_LANES; k++)
        any |= (o[k] = a[k]);
    return any;
}
static uint32_t fxs_tlv(uint32_t t, uint8_t *o)
{
    uint32_t k, any = 0;
#if INS_FX
    if (t >= FXT_NEW0 && t < FXT_N) {                  /* a further insert (insfx.c): T1 T2 T3 DR's amounts, any set */
        if (FXT_AMT[t] == 0xFFu)
            return 0;                                  /* (not built: its amounts are not here to keep) */
        for (k = 0; k < NTRK; k++)
            any |= (o[k] = (uint8_t)trk[k].p[FXT_AMT[t]]);
        return any ? NTRK : 0u;
    }
#endif
    if (t >= FXT_N || FXT_BUS[t] == 0xFFu)
        return 0;
    any = (uint32_t)(o[0] = (uint8_t)TDRUM->p[FXT_BUS[t]]);   /* (the drum bus first: every type that has one) */
    if (t == FXT_DIST)
        return fxs_lanes(dins_amt[0], o + 1) ? 1u + DRUM_LANES : any ? 1u : 0u;
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP) {
        for (k = 1; k < NTRK; k++)
            any |= (uint32_t)(o[k] = (uint8_t)trk[k - 1u].p[P_TCOMP]);
        for (k = 0; k < 3u; k++)
            any |= (uint32_t)((o[NTRK + k] = (uint8_t)ins_cset[0][k]) != (uint8_t)GP[G_CRAT + k].def);
        return fxs_lanes(dins_amt[1], o + FXT_TLV_COMP) ? FXT_TLV_COMP + DRUM_LANES : any ? FXT_TLV_COMP : 0u;
    }
#endif
    (void)k;
    return any ? 1u : 0u;
}
/* the values the TLVs keep, as a project without them has them (all: the settings too) */
static void fxs_untlv_none(int all)
{
    uint32_t k;
    for (k = 0; k < FXT_N; k++)
        if (FXT_BUS[k] != 0xFFu)
            TDRUM->p[FXT_BUS[k]] = 0;
    memset(dins_amt, 0, sizeof dins_amt);
#if INS_FX
    for (k = 0; k < NTRK * (FXT_N - FXT_NEW0); k++)
        trk[k % NTRK].p[P_TFOLD + k / NTRK] = 0;      /* (the further inserts' amounts: built or not) */
#endif
#if FELUCCA_MASTER_COMP
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TCOMP] = 0;
    for (k = 0; all && k < 3u * NTRK; k++)
        ins_cset[k / 3u][k % 3u] = GP[G_CRAT + k % 3u].def;
#endif
    (void)all;
}
/* type t's TLV (n bytes at a) back; a type unknown here, or a shorter TLV's missing part: as none */
static void fxs_untlv(uint32_t t, const uint8_t *a, uint32_t n, int all)
{
    uint32_t k;
#if INS_FX
    if (t >= FXT_NEW0 && t < FXT_N && FXT_AMT[t] != 0xFFu)
        for (k = 0; k < NTRK; k++)                     /* (a shorter TLV: the rest 0) */
            trk[k].p[FXT_AMT[t]] = (int16_t)(k < n ? (a[k] > 127u ? 127u : a[k]) : 0u);
#endif
    if (t < FXT_N && FXT_BUS[t] != 0xFFu && n >= 1u)
        TDRUM->p[FXT_BUS[t]] = (int16_t)(a[0] > 127u ? 127u : a[0]);
    if (t == FXT_DIST && n >= 1u + DRUM_LANES)
        for (k = 0; k < DRUM_LANES; k++)
            dins_amt[0][k] = (uint8_t)(a[1u + k] > 127u ? 127u : a[1u + k]);
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP && n >= FXT_TLV_COMP) {
        for (k = 1; k < NTRK; k++)
            trk[k - 1u].p[P_TCOMP] = (int16_t)(a[k] > 127u ? 127u : a[k]);
        for (k = 0; all && k < 3u * NTRK; k++)       /* (every track's: the track slots' TLV may follow) */
            ins_cset[k / 3u][k % 3u] = (int16_t)clamp(a[NTRK + k % 3u], GP[G_CRAT + k % 3u].min, GP[G_CRAT + k % 3u].max);
        for (k = 0; n >= FXT_TLV_COMP + DRUM_LANES && k < DRUM_LANES; k++)
            dins_amt[1][k] = (uint8_t)(a[FXT_TLV_COMP + k] > 127u ? 127u : a[FXT_TLV_COMP + k]);
    }
#endif
    (void)t, (void)a, (void)n, (void)all, (void)k;
}

/* FX > SLOTS: S1..S4, each an enum of the sends built (their amounts' labels) and ---- (empty) */
#define FXS_NLIST (1 + FELUCCA_FX_CHORUS + FELUCCA_FX_DELAY + FELUCCA_FX_REVERB)
static const char *fxs_names[FXS_NLIST];
static uint8_t fxs_list[FXS_NLIST];
static int16_t fxs_v[FX_NSLOT];
static const param_desc_t FXS_DESC[FX_NSLOT] = {
    {"S1", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}, {"S2", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0},
    {"S3", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}, {"S4", F_ENUM, 0, FXS_NLIST - 1, 0, fxs_names, 0}};
/* INSERTS' S1 and S2 TYPE (the selected track's): an enum of the inserts built and ---- (empty) */
#define INS_NLIST (1 + FELUCCA_FX_DIST + FELUCCA_TRK_FILT + FELUCCA_MASTER_COMP + FELUCCA_INS_FOLD + FELUCCA_INS_FUZZ + \
                   FELUCCA_INS_PHASER + FELUCCA_INS_FLANGER + FELUCCA_INS_FORMANT + FELUCCA_INS_WIDTH + \
                   (FELUCCA_INS_CHORUS > 0) + (FELUCCA_INS_DELAY > 0))
static const char *ins_names[INS_NLIST];
static uint8_t ins_list[INS_NLIST];
static int16_t ins_v[INS_N];
static const param_desc_t INS_DESC[INS_N] = {{"S1", F_ENUM, 0, INS_NLIST - 1, 0, ins_names, 0},
                                             {"S2", F_ENUM, 0, INS_NLIST - 1, 0, ins_names, 0}};
#define FXS_ID_CMP FX_NSLOT                            /* fxs_desc ids: 0..3 the master slots, 4..6 CMP RATIO ATK REL */
#define FXS_ID_INS 8u                                  /* 8, 9: the selected track's S1, S2 TYPE (INSERTS, the mixer) */
/* track k's slot s's TYPE as a list entry (*vp: its index in ins_list) */
static const param_desc_t *ins_desc(uint32_t k, uint32_t s, int16_t **vp)
{
    uint32_t t, n = 0;
    ins_v[s & 1u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (t = 0; t < FXT_N; t++)
        if (t == FXT_NONE || (FXT_BUILT & FXT_INSERT) >> t & 1u) {
            ins_names[n] = t == FXT_NONE ? "----" : TP[FXT_AMT[t]].label;
            if (ins_slot[k % NTRK][s & 1u] == t)
                ins_v[s & 1u] = (int16_t)n;
            ins_list[n++] = (uint8_t)t;
        }
    *vp = &ins_v[s & 1u];
    return &INS_DESC[s & 1u];
}
static const param_desc_t *fxs_desc(uint32_t k, int16_t **vp)
{
    uint32_t t, n = 0;
    if (k >= FXS_ID_INS)
        return ins_desc(song.sel, k - FXS_ID_INS, vp);
#if FELUCCA_MASTER_COMP
    if (k >= FX_NSLOT) {                               /* CMP's RATIO ATK REL, the selected track's: the master COMP's lists */
        *vp = &ins_cset[song.sel % NTRK][(k - FX_NSLOT) % 3u];
        return &GP[G_CRAT + (k - FX_NSLOT) % 3u];
    }
#endif
    fxs_v[k & 3u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (t = 0; t < FXT_N; t++)                        /* (the list from the table: each type's amount names it) */
        if (t == FXT_NONE || ((FXT_BUILT >> t & 1u) && FXT_SEND_OK(t))) {
            fxs_names[n] = t == FXT_NONE ? "----" : TP[FXT_AMT[t]].label;
            if (fxs_slot[k & 3u] == t)
                fxs_v[k & 3u] = (int16_t)n;
            fxs_list[n++] = (uint8_t)t;
        }
    *vp = &fxs_v[k & 3u];
    return &FXS_DESC[k & 3u];
}
