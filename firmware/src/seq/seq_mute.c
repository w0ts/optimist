/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer mutes (the user, 2026-10-13: "for midi, not audio"): a track, or a drum lane, whose steps stop
 * triggering. Not the mixer's MUTE card (P_MUTE for a track, drum_mix.c dlm_mute for a lane: those silence the sound,
 * the MIDI out goes on). Here the sequencer plays nothing of the track or the lane: no note for the internal sound and
 * none for MIDI out (seq.c seq_tick / drum_step), no lock or motion of the lane (auto.c); the notes already sounding
 * end by their own gates, the keys played live still sound.
 * Runtime state, saved with the project in the FX record (fx_rec.c: the TLV SMUTE_TLV, 3 bytes: the tracks' bits, the
 * lanes' mask, little endian; none when nothing is muted, so a project from before reads as nothing muted). The ACCENT
 * lane (core.h DRUM_ACC, lane 10) is bit DRUM_ACC of the lanes' mask: muted, its steps raise no hit (seq.c DACC_VEL);
 * stored as a 4th byte (bit 0), written only when it is muted, so a project without it reads as the ACCENT lane playing.
 * HOME held + a key on the mixer toggles them (ui/optimist/op_input.c); smute_key maps the key. */
#define SMUTE_TLV 0x43u                         /* (past DLM 0x40, FXRET 0x41, INS 0x42 of feat/insert-slots: no FX type's id) */
#define SMUTE_TLV_N 3u
#define SMUTE_TLV_ACC 4u                        /* (the form with the ACCENT lane's byte) */
#define SMUTE_ACC 24                            /* smute_toggle's id of the ACCENT lane (no key: the UI's mixer row) */
#define SMUTE_T0 16                             /* smute_toggle's ids: 0..DRUM_LANES - 1 a lane, SMUTE_T0 + k track k */
static uint8_t smute_trk;                       /* bit k: track k's sequencer muted */
static uint32_t smute_lane;                     /* bit l: drum lane l's (bit DRUM_ACC: the ACCENT lane) */

/* the key (0 = F3 .. 26) -> what it mutes: a lane, SMUTE_T0 + k track k (T1 T2 T3 DR), SMUTE_ACC, -1 none. The keys
 * that play the lanes mute them (drums.c lane_of_key: the Optimist UI's black keys, F#5 the ACCENT lane; SLOOP's white
 * keys); the tracks: the Optimist UI's first four white keys (F G A B), SLOOP's black keys F# G# A# C# */
static int32_t smute_key(uint32_t k)
{
#if FELUCCA_UI == 1
    static const int8_t T[12] = {0, -1, 1, -1, 2, -1, 3, -1, -1, -1, -1, -1};   /* from F: F G A B */
#else
    static const int8_t T[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};   /* from F: F# G# A# C# */
#endif
    uint32_t l;
    if (k >= 27u)
        return -1;
#if FELUCCA_UI == 1 && FELUCCA_DRUM_ACCENT && DACC_KEY_ON
    if (k == DACC_KEY)
        return SMUTE_ACC;                          /* (the last upper key: the ACCENT lane's sequencer mute) */
#endif
#if FELUCCA_UI == 1
    if ((l = lane_of_key(k)) < DRUM_LANES)
        return (int32_t)l;
#else
    if (punch_key(k) >= 0)                         /* (SLOOP's: a white key's lane; a black key no lane's) */
        return (l = (uint32_t)punch_key(k)) < DRUM_LANES ? (int32_t)l : -1;
#endif
    return k < 12u && T[k] >= 0 ? SMUTE_T0 + T[k] : -1;
}
static int smute_trk_on(uint32_t k) { return (smute_trk >> (k & 3u)) & 1u; }
static uint32_t smute_lbit(uint32_t l) { return l <= DRUM_ACC ? 1u << l : 0u; }   /* (lane DRUM_ACC: ACCENT) */
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
    if (id >= SMUTE_T0 && id < SMUTE_T0 + 4) {
        smute_set_trk((uint32_t)(id - SMUTE_T0), !smute_trk_on((uint32_t)(id - SMUTE_T0)));
        return smute_trk_on((uint32_t)(id - SMUTE_T0));
    }
    if (id >= 0 && id < (int32_t)DRUM_LANES) {
        smute_set_lane((uint32_t)id, !smute_lane_on((uint32_t)id));
        return smute_lane_on((uint32_t)id);
    }
    if (id == SMUTE_ACC) {
        smute_set_lane(DRUM_ACC, !smute_lane_on(DRUM_ACC));
        return smute_lane_on(DRUM_ACC);
    }
    return 0;
}
/* the keys' mask of what is muted now: a bit per key (the lanes' keys and the 4 track keys) */
static uint32_t smute_keys_lit(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        int32_t id = smute_key(k);
        if (id == SMUTE_ACC ? smute_lane_on(DRUM_ACC) : id >= SMUTE_T0 ? smute_trk_on((uint32_t)(id - SMUTE_T0)) : id >= 0 && smute_lane_on((uint32_t)id))
            m |= 1u << k;
    }
    return m;
}

/* ---- the FX record's TLV (fx_rec.c) */
static uint32_t smute_tlv(uint8_t *o)           /* -> o, its length; 0: nothing muted (nothing to keep) */
{
    uint32_t m = smute_lane & DRUM_LMASK;
    o[0] = smute_trk, o[1] = (uint8_t)m, o[2] = (uint8_t)(m >> 8), o[3] = (uint8_t)(smute_lane >> DRUM_ACC & 1u);
    return o[3] ? SMUTE_TLV_ACC : smute_trk || m ? SMUTE_TLV_N : 0u;
}
static void smute_untlv(const uint8_t *a, uint32_t n)   /* (a shorter one, or none: nothing muted) */
{
    smute_trk = n >= SMUTE_TLV_N ? (uint8_t)(a[0] & 15u) : 0u;
    smute_lane = n >= SMUTE_TLV_N ? (uint32_t)(a[1] | a[2] << 8) & DRUM_LMASK : 0u;
    if (n >= SMUTE_TLV_ACC && (a[3] & 1u))
        smute_lane |= 1u << DRUM_ACC;
}
