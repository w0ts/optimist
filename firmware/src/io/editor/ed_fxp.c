/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: every effect slot of the firmware, and its parameters, by owner (fx/fx_slots.c; included by editor.c
 * before ed_fxs.c; web/EDITOR_PROTOCOL.md "v13: FX owners"). INFO tag 0x57 = this command is here.
 * An OWNER is where a slot lives: 0..3 the tracks T1 T2 T3 DR (two insert slots S1 S2 each), 4 the master (S1 S2), 5 the
 * USB input (S1 S2, and slot 2: its own values VOLUME PAN MUTE FX ON REV DLY CHO), 6..8 the returns A B C (slot 0: a send
 * type CHO DLY REV, or any other type the return may hold; slot 1, 2026-10-16: its sends to the returns to its right, named
 * "B" "C": A two, B one, C none; fx_slots.c rs_amt), 9..24 the drum sounds (lanes 0..15, S1 S2: the
 * types a voice can hold). A slot holds a TYPE id (FXT_*: 0 empty) and the type has parameters (a count and a descriptor
 * each, the device's own: DYN THRS RATIO ATK REL GAIN CEIL, EQ3 LO MID HI ON FREQ L FREQ H, GATER DEPTH PAT RATE SHAPE,
 * JUNO MIX MODE; every other insert one amount), so an editor draws any slot from what the device says and names no type.
 *   94 FXP   op owner [args] -> op owner [state]
 *       op 0  owner                      -> slots n, then per slot: type, parameter count; the types the owner may hold
 *                                           (5 x 7 bits, bit t = type id t; the empty slot always); a return: then its
 *                                           send line (0 CHO 1 DLY 2 REV: P_CHOR P_DLY P_REV; 127 none)
 *       op 1  owner, slot, type          the slot loads the type as the device's (a type the other slot holds swaps
 *                                           them); replies as op 0
 *       op 2  owner, slot, i             -> owner, slot, i, then DESC's body (fmt, min, max, def, label, unit, names);
 *                                           no such parameter: nothing after i
 *       op 3  owner, slot                -> owner, slot, n, n x v14: every value of the slot
 *       op 4  owner, slot, i, v14        the value set (clamped) -> owner, slot, i, v14
 * A bad owner, slot or op: no reply. The returns' slot 0 first parameter is the line's RET level (G_CRET + line). */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
#include "ed_desc.h"          /* (DESC's body) */
enum { ED_FXP = 94 };
#if FELUCCA_ED_FXP                                       /* (the builder item ED_FXP; off: no command 94, INFO has no tag 0x57) */
#define FXP_MST NTRK
#define FXP_USB (NTRK + 1u)
#define FXP_RET0 (NTRK + 2u)
#define FXP_LANE0 (NTRK + 5u)
#define FXP_NOWN (FXP_LANE0 + DRUM_LANES)
_Static_assert(NTRK == 4, "the owners: T1 T2 T3 DR, then the master, the USB input, three returns, the lanes");
#if FELUCCA_USB_AUDIO
#define FXP_USB_ON 1u
#else
#define FXP_USB_ON 0u
#endif

static uint32_t fxp_slots(uint32_t ow) { return ow == FXP_USB ? 3u : 2u; }   /* (a return: its type, its sends) */
#define FXP_IS_RET(ow) ((ow) >= FXP_RET0 && (ow) < FXP_LANE0)
static int fxp_owner(uint32_t ow) { return ow < FXP_NOWN && (ow != FXP_USB || FXP_USB_ON); }

static uint32_t fxp_type(uint32_t ow, uint32_t sl)
{
    if (ow < NTRK)
        return ins_slot[ow][sl & 1u];
    if (ow == FXP_MST)
        return mst_slot[sl & 1u];
    if (ow == FXP_USB) {
#if FELUCCA_USB_AUDIO
        return sl < 2u ? ut.slot[sl] : (uint32_t)FXT_NONE;
#else
        return FXT_NONE;
#endif
    }
    if (ow < FXP_LANE0)
        return sl ? (uint32_t)FXT_NONE : fxs_slot[1u + (ow - FXP_RET0) % 3u];   /* (slot 1: its sends) */
    return lin_slot[(ow - FXP_LANE0) % DRUM_LANES][sl & 1u];
}
/* the types an owner may hold (the built ones; FXT_NONE always) */
static uint32_t fxp_ok(uint32_t ow)
{
    if (ow < NTRK)
        return FXT_BUILT & FXT_INSERT;
    if (ow == FXP_MST)
        return MST_OK;
    if (ow == FXP_USB) {
#if FELUCCA_USB_AUDIO
        return UT_OK;
#else
        return 0;
#endif
    }
    if (ow < FXP_LANE0)
        return (FXT_BUILT & (FXT_BIT(FXT_CHO) | FXT_BIT(FXT_DLY) | FXT_BIT(FXT_REV))) | RT_OK;
    return LIN_OK;
}
#if FELUCCA_INS_EQ3
static const param_desc_t FXP_EQON = PE("ON", N_ONOFF, 1);
#endif
/* the descriptor and the value's place of (owner, slot, parameter i); 0: none. A lane's amount: *vp is a copy (lin_desc),
 * written by lin_set (fxp_put). The label of the one descriptor copy may be renamed (the device's page names) */
static param_desc_t fxp_cp;
static const param_desc_t *fxp_named(const param_desc_t *d, const char *label)
{
    fxp_cp = *d;
    fxp_cp.label = label;
    return &fxp_cp;
}
#if FELUCCA_INS_EQ3
/* EQ3's i-th cell (LO MID HI ON FREQ L FREQ H) of the owner o in eq3_v; the ON cell is on_p / on_d */
static const param_desc_t *fxp_eq3(uint32_t o, uint32_t i, int16_t *on_p, const param_desc_t *on_d, int16_t **vp)
{
    if (i > 5u)
        return 0;
    if (i == 3u) {
        *vp = on_p;
        return on_d;
    }
    i = i > 3u ? i - 1u : i;
    *vp = &eq3_v[o][i];
    return &EQ_D[i];
}
#endif
static const param_desc_t *fxp_loc(uint32_t ow, uint32_t sl, uint32_t i, int16_t **vp)
{
    uint32_t t = fxp_type(ow, sl), id;
    *vp = 0;
    if (ow == FXP_USB && sl == 2u) {                    /* the input's own values: LEVEL PAN MUTE FX ON REV DLY CHO */
#if FELUCCA_USB_AUDIO
        if (i < UTV_N0) {
            *vp = &ut.v[i];
            return &UT_DESC[i];
        }
#endif
        return 0;
    }
    if (FXP_IS_RET(ow) && sl == 1u) {                   /* a return's sends to its right (A: B C, B: C), named by the return
                                                        * each goes to; listed whatever the returns hold (an empty one's: 0,
                                                        * unheard) */
        static const char *const RN[FX_NSLOT] = {"", "A", "B", "C"};
        uint32_t k = rs_of(1u + (ow - FXP_RET0), i);
        if (k >= RS_N)
            return 0;
        *vp = &rs_amt[k];
        return fxp_named(&TP[P_REV], RN[RS_DST[k] % FX_NSLOT]);
    }
    if (t == FXT_NONE || t >= FXT_N || !(FXT_BUILT >> t & 1u))
        return 0;
    if (ow >= FXP_RET0 && ow < FXP_LANE0) {            /* a return: its line's RET, then the type's own */
        uint32_t k = 1u + (ow - FXP_RET0), ln = fxs_line[k % FX_NSLOT];
        if (ln >= 3u)
            return 0;
        if (i == 0u) {
            *vp = &song.g[G_CRET + ln];
            return fxp_named(&GP[G_CRET + ln], "RET");
        }
        i--;
        if (!FXS_GEN(t))                               /* (a send: its own popup, FX > DLY / REV / CHO) */
            return 0;
#if FELUCCA_INS_EQ3
        if (t == FXT_EQ3)
            return fxp_eq3(NTRK + 2u + ln, i, &fxs_ramt[k], &FXP_EQON, vp);
#endif
        if (i || FXT_AMT[t] == 0xFFu)
            return 0;
        *vp = &fxs_ramt[k];
        return &TP[FXT_AMT[t]];
    }
    if (ow >= FXP_LANE0) {                             /* a drum sound: its slot's amount */
        const param_desc_t *d = i ? 0 : lin_desc(ow - FXP_LANE0, sl & 1u, vp);
        return d;
    }
    if (ow == FXP_USB) {
#if FELUCCA_USB_AUDIO
#if INS_FX
        if (t == FXT_GATE && i >= 1u && i < 4u) {      /* GATER's PAT RATE SHAPE are the input's (ut.v), its DEPTH the slot's amount */
            *vp = &ut.v[UTV_GPAT + i - 1u];
            return &UT_DESC[UTV_GPAT + i - 1u];
        }
#endif
#if FELUCCA_INS_JCHORUS
        if (t == FXT_JUNO && i == 1u) {
            *vp = &ut.v[UTV_JMODE];
            return &UT_DESC[UTV_JMODE];
        }
#endif
#if FELUCCA_INS_EQ3
        if (t == FXT_EQ3)
            return fxp_eq3(NTRK + 1u, i, &ut.amt[sl & 1u], &FXP_EQON, vp);
#endif
        if (i || FXT_AMT[t] == 0xFFu)
            return 0;
        *vp = &ut.amt[sl & 1u];
        return &TP[FXT_AMT[t]];
#else
        return 0;
#endif
    }
    if (ow == FXP_MST) {
        if (t == FXT_DYN) {                            /* the master's COMP + LIMIT: THRS RATIO ATK REL GAIN CEIL */
            if (i > G_CCEIL - G_CTHR)
                return 0;
            *vp = &song.g[G_CTHR + i];
            return &GP[G_CTHR + i];
        }
#if FELUCCA_INS_JCHORUS
        if (t == FXT_JUNO && i < 2u) {
            *vp = i ? &mst_jmode : &mst_amt[FXT_JUNO];
            return &TP[i ? P_TJMODE : P_TJCHO];
        }
#endif
#if FELUCCA_INS_EQ3
        if (t == FXT_EQ3)
            return fxp_eq3(NTRK, i, &mst_amt[FXT_EQ3], &FXP_EQON, vp);
#endif
        if (i || !mst_amt_d(t))
            return 0;
        *vp = mst_amt_p(t);
        return mst_amt_d(t);
    }
    /* a track's slot: the type's values in the track, its settings beside them */
    {
        track_t *tr = &trk[ow];
#if FELUCCA_MASTER_COMP
        if (t == FXT_DYN) {                            /* THRS (the amount), RATIO ATK REL GAIN CEIL (ins_cset) */
            if (i > TDYN_N)
                return 0;
            if (i == 0u) {
                *vp = &tr->p[P_TCOMP];
                return fxp_named(&TP[P_TCOMP], "THRS");
            }
            *vp = &ins_cset[ow][i - 1u];
            return tdyn_d(i - 1u);
        }
#endif
#if INS_FX
        if (t == FXT_GATE && i < 4u) {                 /* DEPTH PAT RATE SHAPE */
            *vp = &tr->p[P_TGATE + i];
            return i ? &TP[P_TGATE + i] : fxp_named(&TP[P_TGATE], "DEPTH");
        }
#endif
#if FELUCCA_INS_JCHORUS
        if (t == FXT_JUNO && i < 2u) {
            *vp = &tr->p[i ? P_TJMODE : P_TJCHO];
            return &TP[i ? P_TJMODE : P_TJCHO];
        }
#endif
#if FELUCCA_INS_EQ3
        if (t == FXT_EQ3)
            return fxp_eq3(ow, i, &tr->p[P_TEQ], i == 3u ? fxp_named(&TP[P_TEQ], "ON") : 0, vp);
#endif
        id = FXT_AMT[t];
        if (i || id == 0xFFu)
            return 0;
        *vp = &tr->p[id];
        return &TP[id];
    }
}
static uint32_t fxp_n(uint32_t ow, uint32_t sl)
{
    int16_t *vp;
    uint32_t n = 0;
    while (n < 16u && fxp_loc(ow, sl, n, &vp))
        n++;
    return n;
}
/* parameter i of the slot set to v (clamped to its range; a fixed one stays) */
static void fxp_put(uint32_t ow, uint32_t sl, uint32_t i, int32_t v)
{
    int16_t *vp;
    const param_desc_t *d = fxp_loc(ow, sl, i, &vp);
    if (!d || !vp || d->max <= d->min)
        return;
    if (ow >= FXP_LANE0)
        lin_set(ow - FXP_LANE0, sl & 1u, v);           /* (the lane's amount: dins_amt, by its type's range) */
    else
        *vp = (int16_t)clamp(v, d->min, d->max);
}
static void fxp_set_type(uint32_t ow, uint32_t sl, uint32_t t)
{
    if (t >= FXT_N || (t != FXT_NONE && !(fxp_ok(ow) >> t & 1u)) || sl >= fxp_slots(ow) || (ow == FXP_USB && sl == 2u) ||
        (FXP_IS_RET(ow) && sl == 1u))
        return;
    if (ow < NTRK)
        ins_load(ow, sl, t);
    else if (ow == FXP_MST)
        mst_load(sl, t);
#if FELUCCA_USB_AUDIO
    else if (ow == FXP_USB)
        ut_load(sl, t);
#endif
    else if (ow < FXP_LANE0)
        fxs_load(1u + (ow - FXP_RET0), t);
    else {                                             /* a lane: lin_set's list index of the type */
        uint32_t i, n = 0, ix = 0, l = ow - FXP_LANE0;
        for (i = 0; i < LIN_NT; i++)
            if (LIN_T[i] == FXT_NONE || LIN_OK >> LIN_T[i] & 1u) {
                ix = LIN_T[i] == t ? n : ix;
                n++;
            }
        lin_set(l, 2u + (sl & 1u), (int32_t)ix);
    }
}
static void fxp_layout(uint32_t op, uint32_t ow)
{
    uint32_t sl, k, ok = fxp_ok(ow);
    ed_b3(op, ow, fxp_slots(ow));
    for (sl = 0; sl < fxp_slots(ow); sl++) {
        ed_b2(fxp_type(ow, sl), fxp_n(ow, sl));
    }
    for (k = 0; k < 5u; k++)
        ed_b(ok >> (7u * k));
    if (ow >= FXP_RET0 && ow < FXP_LANE0)              /* a return: its send line (0 CHO's, 1 DLY's, 2 REV's; 127 none): the
                                                        * line the tracks' send knob of it is (P_CHOR, P_DLY, P_REV) */
        ed_b(fxs_line[1u + (ow - FXP_RET0)] < 3u && fxs_slot[1u + (ow - FXP_RET0)] != FXT_NONE ? fxs_line[1u + (ow - FXP_RET0)] : 127u);
}
/* INFO tag 0x57: version 1, the flags (bit 0 the USB input is here), the owners' count */
static void ed_fxp_info(void)
{
    ed_b(0x57);
    ed_b4(3, 1, FXP_USB_ON, FXP_NOWN);
}
/* what the slots keep that no other push covers (the FX push's signature): the owners' settings and slots */
static uint32_t ed_fxp_sig(uint32_t h)
{
    uint32_t k;
    const uint8_t *p;
#define FXP_H(x) (h = (h ^ (uint32_t)(uint16_t)(x)) * 16777619u)
    for (k = 0; k < NTRK * INS_N; k++)
        FXP_H(ins_slot[k / INS_N][k % INS_N]);
    for (k = 0; k < INS_N; k++)
        FXP_H(mst_slot[k]);
    for (k = 0; k < FXT_N; k++)
        FXP_H(mst_amt[k]);
    for (k = 1; k < FX_NSLOT; k++)
        FXP_H(fxs_ramt[k] + 256 * fxs_line[k]);
    for (k = 0; k < RS_N; k++)
        FXP_H(rs_amt[k]);
    p = &lin_slot[0][0];
    for (k = 0; k < DRUM_LANES * INS_N; k++)
        FXP_H(p[k]);
#if FELUCCA_INS_EQ3
    for (k = 0; k < EQ_NO * EQV_N; k++)
        FXP_H(eq3_v[k / EQV_N][k % EQV_N]);
#endif
#if FELUCCA_MASTER_COMP
    for (k = 0; k < NTRK * TDYN_N; k++)
        FXP_H(ins_cset[k / TDYN_N][k % TDYN_N]);
#endif
#if FELUCCA_INS_JCHORUS
    FXP_H(mst_jmode);
#endif
#if FELUCCA_USB_AUDIO
    for (k = 0; k < UTV_N; k++)
        FXP_H(ut.v[k]);
    for (k = 0; k < INS_N; k++)
        FXP_H(ut.slot[k] + 256 * ut.amt[k]);
#endif
#undef FXP_H
    return h;
}
/* 94 FXP: 1 = handled (replied), 0 = not this command or bad arguments (no reply) */
static int ed_fxp(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, ow, sl, i;
    int16_t *vp;
    const param_desc_t *d;
    if (cmd != ED_FXP || op > 4u || na < 2u || !fxp_owner(ow = a[1]))
        return 0;
    if (op == 0u) {
        fxp_layout(0, ow);
        return 1;
    }
    if (na < 3u || (sl = a[2]) >= fxp_slots(ow))
        return 0;
    if (op == 1u) {
        if (na < 4u)
            return 0;
        fxp_set_type(ow, sl, a[3]);
        fxp_layout(1, ow);
        ui.force = 1;
        return 1;
    }
    if (op == 3u) {
        uint32_t n = fxp_n(ow, sl);
        ed_b4(3, ow, sl, n);
        for (i = 0; i < n; i++)
            fxp_loc(ow, sl, i, &vp), ed_v(*vp);
        return 1;
    }
    if (na < 4u)
        return 0;
    i = a[3];
    d = fxp_loc(ow, sl, i, &vp);
    if (op == 2u) {
        ed_b4(2, ow, sl, i);
        if (d)
            ed_desc_body(d);
        return 1;
    }
    if (!d || na < 6u)
        return 0;
    fxp_put(ow, sl, i, ed_rv(a + 4));
    fxp_loc(ow, sl, i, &vp);
    ui.force = 1;
    ed_b4(4, ow, sl, i);
    ed_v(*vp);
    return 1;
}
#define ED_FXP_FN ed_fxp,                   /* (editor.c: its table of commands) */
#else
static int ed_fxp(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_FXP_FN
static void ed_fxp_info(void) {}
static uint32_t ed_fxp_sig(uint32_t h) { return h; }
#endif
