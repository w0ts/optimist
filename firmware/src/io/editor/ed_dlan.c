/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the drum lanes' place in the mix, their LFOs and the sequencer mutes (included by editor.c before
 * ed_fxp.c; web/EDITOR_PROTOCOL.md "v13: drum lanes' mix"). INFO tag 0x58 = this command is here.
 * What the device UI edits per lane that no earlier command reached: the lane's PAN, MUTE and SOLO (drums/drum_mix.c), its
 * own LFO (RATE WAVE PHS FADE and two DEST / AMT slots: drums/lane_play.c), the sequencer mutes of the lanes, the ACCENT
 * lane and the tracks (seq/seq_mute.c). Described like FXP's: the device's own descriptors.
 *   95 DLAN   op group lane [...] -> op group lane [...]
 *       group 0  a lane's mix: PAN MUTE SOLO SEQ MUTE (lane 0..15); lane 16 the ACCENT lane: SEQ MUTE, ACCENT (the amount)
 *       group 1  a lane's LFO: RATE WAVE PHS FADE DEST AMT DEST2 AMT2 (lane 0..15)
 *       group 2  a track's sequencer mute: SEQ MUTE (lane = track 0..3)
 *       op 0  group, lane            -> 0, group, lane, n (the parameters of it)
 *       op 2  group, lane, i         -> 2, group, lane, i, DESC's body (nothing after i: no such parameter)
 *       op 3  group, lane            -> 3, group, lane, n, n x v14
 *       op 4  group, lane, i, v14    the value set (clamped) -> 4, group, lane, i, v14
 * A bad group, lane or op: no reply. */
enum { ED_DLAN = 95 };
enum { DLG_MIX, DLG_LFO, DLG_TRK, DLG_N };
static const param_desc_t DLN_PAN = PD("PAN", F_BIPCT, -64, 63, 0);
static const param_desc_t DLN_MUTE = PE("MUTE", N_ONOFF, 0);
static const param_desc_t DLN_SOLO = PE("SOLO", N_ONOFF, 0);
static const param_desc_t DLN_SQ = PE("SEQ MUTE", N_ONOFF, 0);
#define DLN_NLANES (DRUM_LANES + (FELUCCA_DRUM_ACCENT ? 1u : 0u))   /* (the ACCENT lane: lane 16) */

static int dln_ok(uint32_t g, uint32_t l)
{
    return g == DLG_MIX ? l < DLN_NLANES : g == DLG_LFO ? l < DRUM_LANES : g == DLG_TRK && l < NTRK;
}
static uint32_t dln_n(uint32_t g, uint32_t l)
{
    return g == DLG_MIX ? (l == DRUM_ACC ? 2u : 4u) : g == DLG_LFO ? (uint32_t)DLF_N : 1u;
}
static const param_desc_t *dln_d(uint32_t g, uint32_t l, uint32_t i)
{
    if (!dln_ok(g, l) || i >= dln_n(g, l))
        return 0;
    if (g == DLG_LFO)
        return &DLFO_DESC[i];
    if (g == DLG_TRK)
        return &DLN_SQ;
    if (l == DRUM_ACC)
        return i ? &DRUM_ACC_DESC : &DLN_SQ;
    return i == 0u ? &DLN_PAN : i == 1u ? &DLN_MUTE : i == 2u ? &DLN_SOLO : &DLN_SQ;
}
static int32_t dln_get(uint32_t g, uint32_t l, uint32_t i)
{
    if (g == DLG_LFO)
        return dlfo_field(l, i);
    if (g == DLG_TRK)
        return smute_trk_on(l);
    if (l == DRUM_ACC)
        return i ? TDRUM->p[P_DACC] : smute_lane_on(DRUM_ACC);
    return i == 0u ? dlm_pan[l] : i == 1u ? dlm_muted(l) : i == 2u ? dlm_soloed(l) : smute_lane_on(l);
}
static void dln_put(uint32_t g, uint32_t l, uint32_t i, int32_t v)
{
    const param_desc_t *d = dln_d(g, l, i);
    if (!d)
        return;
    v = clamp(v, d->min, d->max);
    if (g == DLG_LFO)
        dlfo_field_set(l, i, v);
    else if (g == DLG_TRK)
        smute_set_trk(l, v != 0);
    else if (l == DRUM_ACC) {
        if (i)
            TDRUM->p[P_DACC] = (int16_t)v;
        else
            smute_set_lane(DRUM_ACC, v != 0);
    } else if (i == 0u)
        dlm_set_pan(l, v);
    else if (i == 1u)
        dlm_set_mute(l, v != 0);
    else if (i == 2u)
        dlm_set_solo(l, v != 0);
    else
        smute_set_lane(l, v != 0);
}
/* INFO tag 0x58: version 1, the groups (3) */
static void ed_dlan_info(void)
{
    ed_b(0x58);
    ed_b(2);
    ed_b(1);
    ed_b(DLG_N);
}
/* what the lanes' mix, LFOs and the mutes keep that no other push covers (the FX push's signature) */
static uint32_t ed_dlan_sig(uint32_t h)
{
    uint32_t g, l, i;
#define DLN_H(x) (h = (h ^ (uint32_t)(uint16_t)(x)) * 16777619u)
    for (g = 0; g < DLG_N; g++)
        for (l = 0; dln_ok(g, l); l++)
            for (i = 0; i < dln_n(g, l); i++)
                DLN_H(dln_get(g, l, i));
#undef DLN_H
    return h;
}
/* 95 DLAN: 1 = handled (replied), 0 = not this command or bad arguments (no reply) */
static int ed_dlan(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, g, l, i, n;
    const param_desc_t *d;
    if (cmd != ED_DLAN || na < 3u || op > 4u || op == 1u || !dln_ok(g = a[1], l = a[2]))
        return 0;
    n = dln_n(g, l);
    if (op == 0u) {
        ed_b(0), ed_b(g), ed_b(l), ed_b(n);
        return 1;
    }
    if (op == 3u) {
        ed_b(3), ed_b(g), ed_b(l), ed_b(n);
        for (i = 0; i < n; i++)
            ed_v(dln_get(g, l, i));
        return 1;
    }
    if (na < 4u)
        return 0;
    i = a[3];
    d = dln_d(g, l, i);
    if (op == 2u) {
        ed_b(2), ed_b(g), ed_b(l), ed_b(i);
        if (!d)
            return 1;
        ed_b(d->fmt);
        ed_v(d->min);
        ed_v(d->max);
        ed_v(d->def);
        ed_str(d->label, 8);
        ed_str(d->unit, 8);
        if (d->fmt == F_ENUM && d->names)
            for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)
                ed_str(d->names[i], 8);
        return 1;
    }
    if (!d || na < 6u)
        return 0;
    dln_put(g, l, i, ed_rv(a + 4));
    ui.force = 1;
    ed_b(4), ed_b(g), ed_b(l), ed_b(i);
    ed_v(dln_get(g, l, i));
    return 1;
}
