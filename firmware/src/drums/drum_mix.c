/* SPDX-License-Identifier: GPL-3.0-only */
/* Each drum lane's place in the mix: PAN, MUTE and SOLO of the DRUM_LANES lanes (included by drums.c after drum_sends.c).
 *   PAN    -64..63 as a track's P_PAN (dsp.c pan_gains), on top of the drum track's own: the two gains multiplied
 *          (Q12; a lane at the centre is 4096: the track's pan sample for sample). drums_mix takes it once a voice a
 *          block, never per sample. The SLICER's mono path (slicer.c) pans the drum track as one: no lane pan there.
 *   MUTE   a muted lane is not heard: its hits are not started (drum_on) and a voice of it still sounding stops,
 *          declicked (drums_mix: the voice's last value fades with the tail). An X0X channel already ringing rings out.
 *   SOLO   any lane soloed: only the soloed lanes are heard (a soloed lane also muted stays muted). The drum track stays heard
 *          and every other track and the USB input is silent (song.solo SOLO_LANE: one solo set, core.h trk_silent).
 * The click's wood block (notes 76 / 77) has no lane: index DRUM_LANES, always heard, at the centre.
 * Kept in the FX record (fx_rec.c: a TLV of its own, DLM_TLV, like drum_sends.c dins_amt): project_t is full and the
 * drum record (dlrec_t) is keyed by its exact bytes. A project from before has no TLV: every lane at the centre,
 * heard. Per lane also a peak meter (the mixer's lane rows: meters.c meter_lane_take): the largest |output| of the
 * lane's voices, kept per voice and block (no cost in the sample loops). */
static int8_t dlm_pan[DRUM_LANES + 1];          /* (+ the click: always 0) */
static uint16_t dlm_mute, dlm_solo;
static uint32_t dlm_heard = DRUM_LMASK | 1u << DRUM_LANES;   /* the lanes heard (bit DRUM_LANES: the click), what the ISR reads */
static int32_t dlm_pk[DRUM_LANES];              /* each lane's largest |output| since the meters took it (ISR) */

/* the lanes heard of a mute and a solo mask (bit DRUM_LANES: the click, always) */
static uint32_t dlm_heard_mask(uint32_t mute, uint32_t solo)
{
    uint32_t m = (solo & DRUM_LMASK) ? solo & DRUM_LMASK : DRUM_LMASK;
    return (m & ~mute & DRUM_LMASK) | 1u << DRUM_LANES;
}
static void dlm_update(void) { dlm_heard = dlm_heard_mask(dlm_mute, dlm_solo); }
static __attribute__((noinline)) void dlm_solo_sync(void)   /* song.solo's SOLO_LANE: the drum track counts as soloed while a lane is (core.h trk_silent) */
{
    song.solo = (uint8_t)((song.solo & ~SOLO_LANE) | (dlm_solo ? SOLO_LANE : 0u));
}
static void dlm_solo_clear(void)                /* a new project: no lane soloed */
{
    dlm_solo = 0;
    dlm_update();
    dlm_solo_sync();
}
static void dlm_set_mute(uint32_t l, int on)
{
    dlm_mute = (uint16_t)(on ? dlm_mute | 1u << (l % DRUM_LANES) : dlm_mute & ~(1u << (l % DRUM_LANES)));
    dlm_update();
}
static void dlm_set_solo(uint32_t l, int on)
{
    dlm_solo = (uint16_t)(on ? dlm_solo | 1u << (l % DRUM_LANES) : dlm_solo & ~(1u << (l % DRUM_LANES)));
    dlm_update();
    dlm_solo_sync();
}
static void dlm_set_pan(uint32_t l, int32_t p) { dlm_pan[l % DRUM_LANES] = (int8_t)clamp(p, -64, 63); }
static int dlm_muted(uint32_t l) { return (dlm_mute >> (l % DRUM_LANES)) & 1u; }
static int dlm_soloed(uint32_t l) { return (dlm_solo >> (l % DRUM_LANES)) & 1u; }
static int dlm_lane_heard(uint32_t l) { return (dlm_heard >> (l % DRUM_LANES)) & 1u; }

/* the lane of a drum note (the click: DRUM_LANES) */
AINL uint32_t dlm_lane(uint32_t note) { return dnote_lane(note); }   /* a voice's note -> its lane (drums.c) */
AINL int dlm_heard_note(uint32_t note) { return (dlm_heard >> dlm_lane(note)) & 1u; }
/* the track's gains (and their change over the block) times lane l's pan (no change at the centre) */
AINL void dlm_gains(uint32_t l, int32_t *gl, int32_t *dl, int32_t *gr, int32_t *dr)
{
    int32_t pl, pr;
    if (!dlm_pan[l])
        return;
    pan_gains(dlm_pan[l], &pl, &pr);
    *gl = (*gl * pl) >> 12, *dl = (*dl * pl) >> 12;
    *gr = (*gr * pr) >> 12, *dr = (*dr * pr) >> 12;
}
AINL void dlm_peak(uint32_t l, int32_t pk)
{
    if (l < DRUM_LANES && pk > dlm_pk[l])
        dlm_pk[l] = pk;
}

/* ---- the FX record's TLV (fx_rec.c): the lanes' pans, then the mute and solo masks (little endian) */
#define DLM_TLV 0x40u                           /* (an id no FX type takes: fx_slots.c FXT_* stay below it) */
#define DLM_TLV_N (DRUM_LANES + 4u)
static uint32_t dlm_tlv(uint8_t *o)             /* -> o, its length; 0: every lane as before (nothing to keep) */
{
    uint32_t l, any = dlm_mute | dlm_solo;
    for (l = 0; l < DRUM_LANES; l++)
        any |= (uint32_t)(o[l] = (uint8_t)dlm_pan[l]);
    o[DRUM_LANES] = (uint8_t)dlm_mute, o[DRUM_LANES + 1u] = (uint8_t)(dlm_mute >> 8);
    o[DRUM_LANES + 2u] = (uint8_t)dlm_solo, o[DRUM_LANES + 3u] = (uint8_t)(dlm_solo >> 8);
    return any ? DLM_TLV_N : 0u;
}
static void dlm_none(void)                      /* a project without the TLV: centre, heard */
{
    memset(dlm_pan, 0, sizeof dlm_pan);
    dlm_mute = dlm_solo = 0;
    dlm_update();
    dlm_solo_sync();
}
static void dlm_untlv(const uint8_t *a, uint32_t n)   /* (a shorter one: as none) */
{
    uint32_t l;
    dlm_none();
    if (n < DLM_TLV_N)
        return;
    for (l = 0; l < DRUM_LANES; l++)
        dlm_set_pan(l, (int8_t)a[l]);
    dlm_mute = (uint16_t)((a[DRUM_LANES] | a[DRUM_LANES + 1u] << 8) & DRUM_LMASK);
    dlm_solo = (uint16_t)((a[DRUM_LANES + 2u] | a[DRUM_LANES + 3u] << 8) & DRUM_LMASK);
    dlm_update();
    dlm_solo_sync();
}
