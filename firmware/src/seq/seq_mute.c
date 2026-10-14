/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer mutes (the user, 2026-10-13: "for midi, not audio"): a track, or a drum lane, whose steps stop
 * triggering. Not the mixer's MUTE card (P_MUTE for a track, drum_mix.c dlm_mute for a lane: those silence the sound,
 * the MIDI out goes on). Here the sequencer plays nothing of the track or the lane: no note for the internal sound and
 * none for MIDI out (seq.c seq_tick / drum_step), no lock or motion of the lane (auto.c); the notes already sounding
 * end by their own gates, the keys played live still sound.
 * Runtime state, saved with the project in the FX record (fx_rec.c: the TLV SMUTE_TLV, 3 bytes: the tracks' bits, the
 * lanes' mask, little endian; none when nothing is muted, so a project from before reads as nothing muted). The ACCENT
 * lane (core.h DRUM_ACC, lane 16) is bit 16 of the lanes' mask: muted, its steps raise no hit (seq.c DACC_VEL); stored as
 * a 4th byte (bit 0), written only when it is muted, so an older build reads the 3 bytes it knows and a project from
 * before reads as the ACCENT lane playing.
 * HOME held + a key on the mixer toggles them (ui/optimist/op_input.c); smute_key maps the key. */
#define SMUTE_TLV 0x43u                         /* (past DLM 0x40, FXRET 0x41, INS 0x42 of feat/insert-slots: no FX type's id) */
#define SMUTE_TLV_N 3u
#define SMUTE_TLV_ACC 4u                        /* (the form with the ACCENT lane's byte) */
#define SMUTE_ACC 24                            /* smute_toggle's id of the ACCENT lane (no key: the UI's mixer row) */
#define SMUTE_KEYS 20u                          /* the 16 drum lanes, then T1 T2 T3 DR */
static uint8_t smute_trk;                       /* bit k: track k's sequencer muted */
static uint32_t smute_lane;                     /* bit l: drum lane l's (bit 16: the ACCENT lane) */

/* the key (0 = F3 .. 26) -> what it mutes: 0..15 the lane of that white key, 16..19 the track of the black key F# G# A# C#
 * (T1 T2 T3 DR), -1 none (the other black keys). The keys the drum lanes play are the 16 white keys (drums.c) */
static int32_t smute_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* the lane in the octave, from F */
    static const int8_t B[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    int32_t i;
    if (k >= 27u)
        return -1;
#if FELUCCA_UI == 1 && FELUCCA_DRUM_ACCENT && DACC_KEY_ON
    if (k == DACC_KEY)
        return SMUTE_ACC;                          /* (the last upper key: the ACCENT lane's sequencer mute) */
#endif
    i = W[k % 12u];
    if (i >= 0)
        return (int32_t)((k / 12u) * 7u) + i;      /* (k 26: lane 15) */
    return k < 12u && B[k] >= 0 ? 16 + B[k] : -1;
}
static int smute_trk_on(uint32_t k) { return (smute_trk >> (k & 3u)) & 1u; }
static uint32_t smute_lbit(uint32_t l) { return 1u << (l == DRUM_ACC ? DRUM_ACC : l & 15u); }   /* (lane 16: ACCENT) */
static int smute_lane_on(uint32_t l) { return (smute_lane & smute_lbit(l)) != 0u; }
static void smute_set_trk(uint32_t k, int on)
{
    smute_trk = (uint8_t)(on ? smute_trk | 1u << (k & 3u) : smute_trk & ~(1u << (k & 3u)));
}
static void smute_set_lane(uint32_t l, int on)
{
    smute_lane = on ? smute_lane | smute_lbit(l) : smute_lane & ~smute_lbit(l);
}
/* toggle what a smute_key value names; -> its state now (0 for a key that mutes nothing) */
static int smute_toggle(int32_t id)
{
    if (id >= 16 && id < 20) {
        smute_set_trk((uint32_t)id - 16u, !smute_trk_on((uint32_t)id - 16u));
        return smute_trk_on((uint32_t)id - 16u);
    }
    if (id >= 0 && id < 16) {
        smute_set_lane((uint32_t)id, !smute_lane_on((uint32_t)id));
        return smute_lane_on((uint32_t)id);
    }
    if (id == SMUTE_ACC) {
        smute_set_lane(DRUM_ACC, !smute_lane_on(DRUM_ACC));
        return smute_lane_on(DRUM_ACC);
    }
    return 0;
}
/* the keys' mask of what is muted now: a bit per key (the 16 white lane keys and the 4 black track keys) */
static uint32_t smute_keys_lit(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        int32_t id = smute_key(k);
        if (id == SMUTE_ACC ? smute_lane_on(DRUM_ACC) : id >= 16 ? smute_trk_on((uint32_t)id - 16u) : id >= 0 && smute_lane_on((uint32_t)id))
            m |= 1u << k;
    }
    return m;
}

/* ---- the FX record's TLV (fx_rec.c) */
static uint32_t smute_tlv(uint8_t *o)           /* -> o, its length; 0: nothing muted (nothing to keep) */
{
    o[0] = smute_trk, o[1] = (uint8_t)smute_lane, o[2] = (uint8_t)(smute_lane >> 8), o[3] = (uint8_t)(smute_lane >> 16 & 1u);
    return o[3] ? SMUTE_TLV_ACC : smute_trk || smute_lane ? SMUTE_TLV_N : 0u;
}
static void smute_untlv(const uint8_t *a, uint32_t n)   /* (a shorter one, or none: nothing muted) */
{
    smute_trk = n >= SMUTE_TLV_N ? (uint8_t)(a[0] & 15u) : 0u;
    smute_lane = n >= SMUTE_TLV_N ? (uint32_t)(a[1] | a[2] << 8) : 0u;
    if (n >= SMUTE_TLV_ACC && (a[3] & 1u))
        smute_lane |= 1u << DRUM_ACC;
}
