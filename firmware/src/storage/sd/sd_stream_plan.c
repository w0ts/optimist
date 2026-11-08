/* SPDX-License-Identifier: GPL-3.0-only */
/* The streams' memory per project (docs/SD-CARD-DESIGN.md 7, the user's rulings of 2026-10-26; the arena:
 * docs/SD-MODE-DESIGN.md 6b): the samples the project plays from the card (each part's CARD sample, by its hash), their
 * HEADS (the preload: one ring's bytes of each, sd_stream.c SST_HEAD_RING; a shorter sample whole) and the streams'
 * rings, in ONE block of the arena (its client SST_ARN_K, engines.c), placed when the project's samples change and held
 * only while the project plays card samples. One setting, the builder's SD_RING (the user, 2026-11-06 evening: "each
 * sampler should allocate its ring. basta"): every streaming voice owns a ring of that size, always (sst_nring: the
 * voices), and the plan places them all or refuses; it never places fewer or shorter rings, nor shorter heads. Main loop
 * only (sst_plan_poll every pass); included by sdcard.c after sd_stream.c and sd_index.c (FELUCCA_SD_STREAM).
 *
 *   sst_part_set(part, hash)   part's CARD sample (0: none): the project's load and the sample picker call it; slot
 *                              SST_NPART + l: drum lane l's card sample (sd_mode.c, every pass: the lanes on CARD)
 *   sst_remount()              a mount (sdcard.c): every stream stopped, everything looked up and placed again (the
 *                              runs are sector numbers: another card's would be garbage)
 *   sst_plan_poll()            places again when something changed: the CARD voices are let go first (their sources
 *                              not ready: they fade out, eng_sample.c), the old block given back two audio halves later,
 *                              each hash looked up in the sample index (sd_index.c), each file's need summed (the rings:
 *                              as many as the CARD parts can sound voices at once, SST_PART_RINGS a part, a card lane one, SST_N at most; its
 *                              runs, its head; a
 *                              part's voice count changed is placed again too), the block reserved and taken (the interrupts off), the rings set, the
 *                              sources loaded, each part pointed at its source
 *   sstp                       the plan's state for the UI: SSTP_PLACED, or SSTP_REFUSED (no room): need (bytes), drop
 *                              (the parts any one of whose memory, given back, would make room: a mask, as model.c
 *                              mem_offer proposes one drop at a time; 0 none alone), drop_all (1: the three parts'
 *                              together would), said (the UI said MEMORY FULL: CARD SOUNDS OFF once);
 *                              missing (a mask of slots whose sample is not on the card: MISSING, never another sound);
 *                              bmiss (a mask of the parts whose card set is not on the card: sd_mode.c says it once)
 * A part on a CARD SET (SST_PART_BANK(p): the set's number + 1, SD card mode's built-in sets, gen_samples.py
 * SMP_CARD_SETS) plays Optimist/Samples/<Set>/z0.wav .. z<k>.wav (SST_BANK_NAME: the set's name, capitalised; one source a zone, in
 * key order, each zone's keys in its "inst"): found by path (no index needed), the parts on one set sharing its
 * sources, their rings counted as a CARD part's; sst_bank_src / sst_bank_n (sd_stream.h) point the part at them. Z0
 * not there: the part MISSING (bmiss), silent, never another set.
 * The DRUMS' card sounds (SD card mode: registry.h KITS_CARD, X909_CARD): the sampled kits' sounds the lanes play
 * (SST_KIT_WANT(): a mask of drums/kit_card.c KC_FILE, Optimist/Samples/Kits/Acoustic/<nn> - <name>.wav) and the 909's
 * hh, crash, ride (SST_X9_WANT(), Samples/X0X/909/), found by path as a set's zones, their heads one ring's as every
 * stream's (a hit starts at once from it, the ring follows), SST_KIT_RINGS rings for the kit's hits and SST_X9_RINGS for
 * the 909's; sst_kit_src / sst_x9_src (sd_stream.h) point the drums at them. One not on the card: kmiss / x9miss (sd_mode.c
 * says MISSING once), that sound silent.
 * A part on GRAIN or STRETCH with SRC CARD (SST_PART_FREE(p); the user, 2026-11-05): its own WAV, by its hash as a CARD
 * sample, read in any order as a card set of one zone (sst_bank_src / sst_bank_n[p], sst_bank_free), no ring. WHOLE in the
 * block when it fits (sstp.whole, SST_PRE_WHOLE, at most SST_WHOLE_MAX; no bus at all, a cache of SST_RC_WHOLE_KB for the
 * engines' pins), else through the part's cache: any length (the cache needs only a grain's or a window's sectors), a
 * stereo file's STRETCH cache twice the KB (its window's bytes: SST_RK at most). sstp.bad[p]: part p's file cannot be
 * played, SSTB_RUNS in more runs than the fast-seek map holds, SSTB_FORMAT not a PCM WAV of 8 or 16 bits, SSTB_LONG 2^30
 * frames or more (sd_mode.c says it, once).
 * A refusal keeps the project's samples silent (their parts' CARD voices play nothing), the project unchanged, and is
 * tried again every SST_RETRY_MS (a part's change, an effect let go: room): placed by itself once it fits, as the
 * arena's other buffers are (core/arena_bufs.c). */
#ifndef SST_PART_VOICES
#define SST_PART_VOICES(p) SST_N                     /* (sdcard.c: the voices part p can sound at once) */
#endif
/* a part's rings at most: its card notes at once (a 5th is silent and counted, sst_nostream). Measured 2026-10-26 on the
 * default project (the emulator, SD card build): the effects' lines held 68 KB of the 96 KB arena, and a SAMPLE part's 8
 * rings of 4 KB plus its head (37,192 B) never fit beside them (MEMORY FULL: CARD SOUNDS OFF at the first USE); 4 rings, about 21 KB, do */
#ifndef SST_PART_RINGS
#define SST_PART_RINGS 4u
#endif
/* the card lanes (slots SST_NPART + lane, FELUCCA_SD_LANES): a ring each (a lane's hit cuts its own hit before:
 * drums/lane_card.c), sharing the SST_N rings with the parts */
/* SLICE on CARD (sd_slice.c, SST_PART_SLICE): that part's sample goes into the block whole (ADPCM), no ring or head for
 * it; a part going on or off it places again */
#ifndef SST_BANK_Z
#define SST_BANK_Z 8u                                /* a card set's zones at most (gen_samples.py CARD_ZONES) */
#endif
#define SST_REC_BANK 0x7FFFFFFF                      /* (also a drum sound's: found by its path) */
#define SST_ZB_KIT 0xF0u                             /* (sstp.zb of a sampled kit's sound, zz its file; of the 909's:) */
#define SST_ZB_X9 0xF1u
#ifndef SST_KIT_WANT
#define SST_KIT_WANT() 0u                            /* (drums/kit_card.c: the kits' sounds the lanes play, a mask) */
#endif
#ifndef SST_X9_WANT
#define SST_X9_WANT() 0u                             /* (drums/x0x/x0x_card.c: the 909's files its sounds play, a mask) */
#endif
#ifndef SST_KIT_RINGS
#define SST_KIT_RINGS 10u                            /* the kit's hits at once: a ring a drum voice (drums.c NDRUM, a lane each since
                                                      * 2026-11-05), never fewer: no room for them, refused (the user, 2026-11-06
                                                      * evening; measured that day: five rings at a 200 ms pre-buffer, a sixth hit cut
                                                      * the oldest tail). More hits: the oldest tail's ring (drums/lane_card.c
                                                      * dlc_ring_free) */
#endif
#ifndef SST_X9_RINGS
#define SST_X9_RINGS 2u                              /* the 909's hat and a cymbal at once */
#endif                      /* (sstp.rec of a card set's zone: found by its path) */
#define SST_RETRY_MS 200u
#define SST_GATE_MS 20u                              /* the audio's two halves (11.6 ms) and some: no voice reads it */
enum { SSTP_IDLE = 0, SSTP_GATED, SSTP_PLACED, SSTP_REFUSED };
enum { SSTB_RUNS = 1, SSTB_FORMAT, SSTB_LONG, SSTB_GONE };
static uint64_t sst_part_hash[SST_NSLOT] IN_POOL;    /* each part's CARD sample, then each card lane's; 0 none */
static uint8_t sst_part_bank[SST_NPART];             /* each part's card set + 1 (SST_PART_BANK), 0 none */
/* GRAIN and STRETCH on a card set (SST_PART_RAND, sdcard.c): the part reads its zones in any order, through a CACHE of
 * its own in the block (sd_stream_rc.h, SST_RC_BYTES) and no ring; a zone only they play has no head (a head is a
 * stream's start: theirs come from the cache) */
#ifndef SST_PART_RAND
#define SST_PART_RAND(p) 0u
#endif
#ifndef SST_PART_FREE
#define SST_PART_FREE(p) 0u                          /* (sdcard.c: the part on GRAIN or STRETCH with SRC CARD) */
#endif
#ifndef SST_WHOLE_MAX
#define SST_WHOLE_MAX (128u * 1024u)                 /* a file read whole into the block at most: the arena's other users */
#endif
#define SST_RC_WHOLE_KB 1u                           /* (a whole file's part: no sector ever cached, the pins' home) */
static uint8_t sst_part_rnd;                         /* the parts read through a cache, a mask (SST_PART_RAND) */
static uint8_t sst_part_free;                        /* those on their own WAV, a mask (SST_PART_FREE) */
static uint8_t sst_rc_kb[SST_NPART];                 /* the KB placed (a whole file's, a stereo STRETCH's: not sst_part_kb) */
static uint8_t sst_part_kb[SST_NPART];               /* their caches' KB (SST_PART_RAND: GRAIN's, STRETCH's) */
static uint16_t sst_kit_want;                        /* the drums' card sounds wanted (SST_KIT_WANT, SST_X9_WANT) */
static uint8_t sst_x9_want;

static struct {
    uint8_t dirty, state, said;
    uint8_t drop, drop_all, bmiss, x9miss;
    uint8_t bad[SST_NPART];                          /* a part's own file not playable (SSTB_*), 0 fine */
    uint32_t whole;                                  /* the sources read whole into the block, a mask */
    uint16_t kmiss;                                  /* the kits' sounds wanted and not on the card (x9miss: the 909's) */
    uint16_t missing;
    uint32_t need, t0;
    int32_t rec[SST_NSRC];                           /* each source's index record (sdx_read), -1 none */
    uint64_t hash[SST_NSRC];
    uint8_t use[SST_NSRC];                           /* SSTU_STREAM: a slot streams it, SSTU_SLICE: a SLICE part's */
    uint8_t zb[SST_NSRC], zz[SST_NSRC];              /* a card set's zone: its set + 1 and zone (zb 0: by hash) */
    uint8_t nsrc, nring, nring_want, slc;                        /* slc: the slots on SLICE CARD when placed (sst_slicers) */
    uint8_t rc;                                      /* the parts given a cache when placed (sst_rcs_of), a mask */
} sstp IN_POOL;
enum { SSTU_STREAM = 1, SSTU_SLICE = 2, SSTU_RAND = 4, SSTU_KIT = 8 };   /* (RAND: a cache's part reads it, no head for
                                                                         * it; KIT: a drum sound's, streamed as any) */

static uint32_t sst_rcs_of(void)                     /* the parts that take a cache: on a card set, read in any order */
{
    uint32_t p, m = 0;
    for (p = 0; p < SST_NPART; p++)
        if ((sst_part_bank[p] || (sst_part_free >> p & 1u && sst_part_hash[p])) && (sst_part_rnd >> p & 1u))
            m |= 1u << p;
    return m;
}

static uint32_t sst_cache_bytes(uint32_t m)         /* the caches of mask m in the block */
{
    uint32_t p, n = 0;
    for (p = 0; p < SST_NPART; p++)
        if (m >> p & 1u)
            n += SST_RC_BYTES(sst_rc_kb[p]);
    return n;
}

AINL uint32_t sst_src_head(uint32_t i)               /* source i's head, bytes: whole, one ring's (a stream's start), none (a cache's) */
{
    return sstp.whole >> i & 1u ? SST_PRE_WHOLE : sstp.use[i] & (SSTU_STREAM | SSTU_KIT) ? SST_HEAD_RING : 0u;
}
#define SSTU_HEAD (SSTU_STREAM | SSTU_RAND | SSTU_KIT)   /* (a source loaded with its runs: streamed or cached) */

static uint32_t sst_slicers(void)                    /* the parts on SLICE with SRC CARD and a card sample, a mask */
{
    uint32_t m = 0;
#ifdef SST_PART_SLICE
    uint32_t p;
    for (p = 0; p < SST_NPART; p++)
        if (sst_part_hash[p] && SST_PART_SLICE(p))
            m |= 1u << p;
#endif
    return m;
}

static uint32_t sst_nring(void)                      /* the rings: the voices that stream at once, a ring each, always: the CARD
                                                      * parts' (SST_PART_RINGS each at most), a card lane's one, the kit's
                                                      * SST_KIT_RINGS, the 909's SST_X9_RINGS; SST_N at most */
{
    uint32_t p, n = 0;
    uint32_t sl = sst_slicers();
    for (p = 0; p < SST_NSLOT; p++)
        if (p < SST_NPART && (sst_part_free >> p & 1u))
            continue;                                /* (read through its cache, no ring) */
        else if ((sst_part_hash[p] || (p < SST_NPART && sst_part_bank[p] && !(sst_part_rnd >> p & 1u))) && !((sl >> p) & 1u))
            n += p < SST_NPART ? (SST_PART_VOICES(p) < SST_PART_RINGS ? SST_PART_VOICES(p) : SST_PART_RINGS) : 1u;
    n += sst_kit_want ? SST_KIT_RINGS : 0u;
    n += sst_x9_want ? SST_X9_RINGS : 0u;
    return n < SST_N ? n : SST_N;
}

static void sst_part_set(uint32_t part, uint64_t hash)   /* (part: a slot, SST_NPART + lane a card lane) */
{
    if (part < SST_NSLOT && sst_part_hash[part] != hash) {
        sst_part_hash[part] = hash;
        sstp.dirty = 1;
    }
}

static uint32_t sst_bank_sync(void)                  /* the parts' card sets (SST_PART_BANK) again: 1 changed */
{
    uint32_t p, b, ch = 0, r = 0, fr = 0;
    if (SST_KIT_WANT() != sst_kit_want || SST_X9_WANT() != sst_x9_want) {   /* (the drums' card sounds) */
        sst_kit_want = (uint16_t)SST_KIT_WANT();
        sst_x9_want = (uint8_t)SST_X9_WANT();
        ch = 1;
    }
#ifdef SST_PART_BANK
    for (p = 0; p < SST_NPART; p++) {
        b = SST_PART_BANK(p);
        if (b != sst_part_bank[p]) {
            sst_part_bank[p] = (uint8_t)b;
            ch = 1;
        }
        if (SST_PART_FREE(p))
            fr |= 1u << p;
        if ((b || SST_PART_FREE(p)) && SST_PART_RAND(p)) {
            r |= 1u << p;
            if (sst_part_kb[p] != SST_PART_RAND(p)) {
                sst_part_kb[p] = (uint8_t)SST_PART_RAND(p);
                ch = 1;
            }
        }
    }
    if (r != sst_part_rnd || fr != sst_part_free) {  /* (a part went from SAMPLE to GRAIN or STRETCH on its set, or back) */
        sst_part_rnd = (uint8_t)r;
        sst_part_free = (uint8_t)fr;
        ch = 1;
    }
#else
    (void)p;
    (void)b;
    (void)r;
    (void)fr;
#endif
    return ch;
}

static FRESULT sst_rec_path(int32_t no, char *path, uint32_t cap)   /* the source's file, from the card's root */
{
    FRESULT r = sdx_read((uint32_t)no, &sdx.rec);
    if (r != FR_OK)
        return r;
    if (!sdl_path(path, cap, SDL_N, sdx.rec.path))  /* ("Optimist/" + the record's path) */
        return FR_INVALID_NAME;
    return FR_OK;
}

/* source i's file from the card's root: a card set's zone by its path, else its index record's */
static FRESULT sst_src_path(uint32_t i, char *path, uint32_t cap)
{
    if (sstp.zb[i] == SST_ZB_KIT || sstp.zb[i] == SST_ZB_X9) {   /* a drum sound: its file by name */
#if defined SST_KIT_FILE || defined SST_X9_FILE
        static const char K[] = SDL_ROOT "/Samples/Kits/Acoustic/", X[] = SDL_ROOT "/Samples/X0X/909/";
        const char *d = sstp.zb[i] == SST_ZB_KIT ? K : X, *nm = 0;
        uint32_t n = 0, k;
#ifdef SST_KIT_FILE
        if (sstp.zb[i] == SST_ZB_KIT)
            nm = SST_KIT_FILE(sstp.zz[i]);
#endif
#ifdef SST_X9_FILE
        if (sstp.zb[i] == SST_ZB_X9)
            nm = SST_X9_FILE(sstp.zz[i]);
#endif
        if (!nm)
            return FR_INVALID_NAME;
        for (k = 0; d[k] && n + 1u < cap; k++)
            path[n++] = d[k];
        for (k = 0; nm[k] && n + 1u < cap; k++)
            path[n++] = nm[k];
        path[n] = 0;
        return nm[k] ? FR_INVALID_NAME : FR_OK;
#else
        return FR_INVALID_NAME;
#endif
    }
#ifdef SST_PART_BANK
    if (sstp.zb[i]) {
        static const char D[] = SDL_ROOT "/Samples/", F[] = "/z0.wav";   /* (Optimist/Samples/Piano/z0.wav) */
        const char *nm = SST_BANK_NAME(sstp.zb[i] - 1u);
        uint32_t n = 0, k;
        for (k = 0; D[k] && n + 1u < cap; k++)
            path[n++] = D[k];
        for (k = 0; nm[k] && n + 1u < cap; k++)      /* the set's name capitalised: PIANO's folder Piano */
            path[n++] = k ? (char)sdl_lc((uint8_t)nm[k]) : nm[k];
        for (k = 0; F[k] && n + 1u < cap; k++)
            path[n++] = k == 2u ? (char)('0' + sstp.zz[i]) : F[k];
        path[n] = 0;
        return k == sizeof F - 1u ? FR_OK : FR_INVALID_NAME;
    }
#endif
    return sst_rec_path(sstp.rec[i], path, cap);
}

static uint32_t sst_need(void)                       /* the block: the rings, the caches, then each source */
{
    static char path[SDX_PATH + 16];
    uint32_t i, b, n = sstp.nring * sst_k * 512u + sst_cache_bytes(sstp.rc);
    for (i = 0; i < sstp.nsrc; i++) {
        if (sstp.rec[i] < 0 || sst_src_path(i, path, sizeof path) != FR_OK)
            continue;
        if ((sstp.use[i] & SSTU_HEAD) && sst_src_need(path, sst_src_head(i), &b) == FR_OK)
            n += (b + 3u) & ~3u;
#ifdef SST_PART_SLICE
        if ((sstp.use[i] & SSTU_SLICE) && ssl_need(path, &b) == FR_OK)
            n += (b + 3u) & ~3u;
#endif
    }
    return n;
}

static void sst_gate(void)                           /* the CARD voices let go: they fade out (eng_sample.c) */
{
    uint32_t i;
    sst_bank_free = 0;
    for (i = 0; i < SST_NSLOT; i++)
        sst_part_src[i] = 0xFF;
#ifdef SST_PART_SLICE
    for (i = 0; i < SST_NPART; i++)
        ssl_set(i, 0);                               /* (SLICE's voices on it fall silent: eng_slice.c slc_get) */
#endif
    for (i = 0; i < SST_NPART; i++) {
        sst_bank_n[i] = 0;
        sst_rc[i].on = 0;                            /* (a cache's grains fade out: eng_grain.c, eng_stretch.c) */
    }
#if SST_NKIT
    for (i = 0; i < SST_NKIT; i++)
        sst_kit_src[i] = 0xFF;                       /* (the drums' card hits silent until placed again) */
#endif
#if SST_NX9
    for (i = 0; i < SST_NX9; i++)
        sst_x9_src[i] = 0xFF;
#endif
    sst_bank_gen++;
    for (i = 0; i < SST_NSRC; i++)
        sst_src[i].ready = 0;
}

#ifdef SST_PART_BANK
/* card set b + 1's zones as sources (once for the parts that play it, each part's use added: a stream's, a cache's):
 * Z0 .. on, up to the first not there; 0 none */
static uint32_t sst_bank_lookup(uint32_t b, uint32_t use)
{
    static char path[SDX_PATH + 16];
    static FILINFO fi;
    uint32_t i, z, found = 0;
    for (i = 0; i < sstp.nsrc; i++)
        if (sstp.zb[i] == b) {
            sstp.use[i] |= (uint8_t)use;
            found = 1;
        }
    if (found)
        return 1;
    for (z = 0; z < SST_BANK_Z && sstp.nsrc < SST_NSRC; z++) {
        i = sstp.nsrc;
        sstp.zb[i] = (uint8_t)b;
        sstp.zz[i] = (uint8_t)z;
        if (sst_src_path(i, path, sizeof path) != FR_OK || f_stat(path, &fi) != FR_OK)
            break;
        sstp.hash[i] = 0;
        sstp.rec[i] = SST_REC_BANK;
        sstp.use[i] = (uint8_t)use;
        sstp.nsrc++;
    }
    if (sstp.nsrc < SST_NSRC)
        sstp.zb[sstp.nsrc] = 0;                      /* (the slot tried and not taken) */
    return z > 0;
}
#endif

/* the drums' card sounds of mask m as sources (zb SST_ZB_KIT / SST_ZB_X9, zz the file): -> the mask of those not on the
 * card */
static uint32_t sst_drum_lookup(uint32_t zb, uint32_t m)
{
    static char path[SDX_PATH + 16];
    static FILINFO fi;
    uint32_t k, i, miss = 0;
    for (k = 0; k < 16u; k++) {
        if (!(m >> k & 1u))
            continue;
        i = sstp.nsrc;
        if (i >= SST_NSRC) {
            miss |= 1u << k;                         /* (no source left: silent, as one not on the card) */
            continue;
        }
        sstp.zb[i] = (uint8_t)zb;
        sstp.zz[i] = (uint8_t)k;
        if (sst_src_path(i, path, sizeof path) != FR_OK || f_stat(path, &fi) != FR_OK) {
            miss |= 1u << k;
            continue;
        }
        sstp.hash[i] = 0;
        sstp.rec[i] = SST_REC_BANK;
        sstp.use[i] = SSTU_KIT;
        sstp.nsrc++;
    }
    if (sstp.nsrc < SST_NSRC)
        sstp.zb[sstp.nsrc] = 0;
    return miss;
}

static void sst_lookup(void)                         /* the slots' hashes -> sources (one a sample), their records */
{
    uint32_t p, i;
    sstp.nsrc = 0;
    sstp.missing = 0;
    sstp.slc = (uint8_t)sst_slicers();
    for (p = 0; p < SST_NSLOT; p++) {
        if (!sst_part_hash[p])
            continue;
        for (i = 0; i < sstp.nsrc && (sstp.zb[i] || sstp.hash[i] != sst_part_hash[p]); i++)
            ;
        if (i == sstp.nsrc && i < SST_NSRC) {
            sstp.zb[i] = 0;
            sstp.hash[i] = sst_part_hash[p];
            sstp.rec[i] = sdx_find_hash(sst_part_hash[p], &sdx.rec);
            sstp.use[i] = 0;
            sstp.nsrc++;
        }
        if (i < sstp.nsrc)
            sstp.use[i] |= (uint8_t)(((sstp.slc >> p) & 1u) ? SSTU_SLICE :
                                     p < SST_NPART && (sst_part_free >> p & 1u) ? SSTU_RAND : SSTU_STREAM);
        if (i >= sstp.nsrc || sstp.rec[i] < 0)
            sstp.missing |= (uint16_t)(1u << p);
    }
    sstp.bmiss = 0;
#ifdef SST_PART_BANK
    for (p = 0; p < SST_NPART; p++)
        if (sst_part_bank[p] && !sst_bank_lookup(sst_part_bank[p], sst_part_rnd >> p & 1u ? SSTU_RAND : SSTU_STREAM)) {
            sstp.bmiss |= (uint8_t)(1u << p);
            sstp.missing |= (uint16_t)(1u << p);
        }
#endif
    sstp.kmiss = (uint16_t)sst_drum_lookup(SST_ZB_KIT, sst_kit_want);
    sstp.x9miss = (uint8_t)sst_drum_lookup(SST_ZB_X9, sst_x9_want);
}

static void sst_load_all(uint8_t *mem)               /* the rings, then each source; each part to its source */
{
    static char path[SDX_PATH + 16];
    static const void *tab[SST_NSRC];
    uint32_t i, p, at = sstp.nring * sst_k * 512u, used;
    sst_rings(mem, sstp.nring);
    for (p = 0; p < SST_NPART; p++)                  /* the caches after the rings */
        if (sstp.rc >> p & 1u) {
            sst_rc_place(p, mem + at, sst_rc_kb[p]);
            at += SST_RC_BYTES(sst_rc_kb[p]);
        }
    for (i = 0; i < sstp.nsrc; i++) {
        tab[i] = 0;
        if (sstp.rec[i] < 0 || sst_src_path(i, path, sizeof path) != FR_OK) {
            sstp.rec[i] = -1;
            continue;
        }
        if (sstp.use[i] & SSTU_HEAD) {
            if (sst_src_load(i, path, sst_src_head(i), mem + at, sstp.need - at, &used) != FR_OK) {
                sstp.rec[i] = -1;                    /* (gone since the lookup, or no longer a WAV it plays) */
                continue;
            }
            sst_src[i].hash = sstp.hash[i];
            at += (used + 3u) & ~3u;
        }
#ifdef SST_PART_SLICE
        if (sstp.use[i] & SSTU_SLICE) {
            tab[i] = ssl_load(path, mem + at, sstp.need - at, &used);
            if (!tab[i]) {
                sstp.rec[i] = -1;
                continue;
            }
            at += (used + 3u) & ~3u;
        }
#endif
    }
    for (p = 0; p < SST_NPART; p++)                  /* a card set's parts: its zones' sources */
        for (i = 0; sst_part_bank[p] && i < sstp.nsrc; i++)
            if (sstp.zb[i] == sst_part_bank[p]) {
                for (used = i; used < sstp.nsrc && sstp.zb[used] == sst_part_bank[p]; used++)
                    ;
                sst_bank_src[p] = (uint8_t)i;
                sst_bank_n[p] = (uint8_t)(used - i);   /* (last: the audio reads it; a zone not loaded is not ready) */
                if (sstp.rec[i] < 0)
                    sstp.missing |= (uint16_t)(1u << p);
                break;
            }
    for (i = 0; i < sstp.nsrc; i++) {                /* the drums' card sounds (last: the audio reads them) */
#if SST_NKIT
        if (sstp.zb[i] == SST_ZB_KIT && sstp.zz[i] < SST_NKIT) {
            if (sstp.rec[i] >= 0)
                sst_kit_src[sstp.zz[i]] = (uint8_t)i;
            else
                sstp.kmiss |= (uint16_t)(1u << sstp.zz[i]);
        }
#endif
#if SST_NX9
        if (sstp.zb[i] == SST_ZB_X9 && sstp.zz[i] < SST_NX9) {
            if (sstp.rec[i] >= 0)
                sst_x9_src[sstp.zz[i]] = (uint8_t)i;
            else
                sstp.x9miss |= (uint8_t)(1u << sstp.zz[i]);
        }
#endif
    }
    sst_bank_gen++;                                  /* (the engines' card zones again: eng_sample.c smp_cz_sync) */
    for (p = 0; p < SST_NSLOT; p++)
        for (i = 0; sst_part_hash[p] && i < sstp.nsrc; i++)
            if (!sstp.zb[i] && sstp.hash[i] == sst_part_hash[p]) {
                if (sstp.rec[i] < 0)
                    sstp.missing |= (uint16_t)(1u << p);
#ifdef SST_PART_SLICE
                else if ((sstp.slc >> p) & 1u)
                    ssl_set(p, tab[i]);
#endif
                else if (p < SST_NPART && (sst_part_free >> p & 1u)) {
                    sst_bank_src[p] = (uint8_t)i;    /* (its own WAV: a card set of one zone) */
                    sst_bank_n[p] = 1;
                    sst_bank_free |= (uint8_t)(1u << p);
                } else {
                    sst_part_src[p] = (uint8_t)i;
                }
            }
    sst_bank_gen++;                                  /* (the zones again, the parts' own WAVs in) */
}

/* the parts on their own WAV: each file looked at (not playable: bad, MISSING; a stereo file's STRETCH cache twice the
 * KB, its window's bytes), then the files only caches read, each whole when the block still fits, in source order: a
 * part whose reads are all its whole file keeps a cache of SST_RC_WHOLE_KB */
static void sst_free_kb(uint64_t hash, uint32_t kb2, uint32_t whole)   /* the parts on that file: their caches' KB */
{
    uint32_t p;
    for (p = 0; p < SST_NPART; p++)
        if ((sst_part_free >> p & 1u) && sst_part_hash[p] == hash)
            sst_rc_kb[p] = (uint8_t)(whole ? SST_RC_WHOLE_KB : sst_part_kb[p] * (kb2 && sst_part_kb[p] <= SST_STRETCH_KB ? 2u : 1u));
}
static void sst_free_pick(void)
{
    static char path[SDX_PATH + 16];
    uint32_t p, i, b, cand = 0, wide = 0, bad;
    FRESULT r;
    sstp.whole = 0;
    for (p = 0; p < SST_NPART; p++) {
        sstp.bad[p] = 0;
        sst_rc_kb[p] = sst_part_kb[p];
    }
    for (i = 0; i < sstp.nsrc; i++) {
        if (sstp.zb[i] || sstp.rec[i] < 0 || !(sstp.use[i] & SSTU_RAND) || sst_src_path(i, path, sizeof path) != FR_OK)
            continue;
        r = sst_src_need(path, SST_PRE_WHOLE, &b);
        bad = r == FR_DENIED ? SSTB_RUNS : r == FR_INVALID_OBJECT ? SSTB_FORMAT : r != FR_OK ? SSTB_GONE :
              sst_probe.dlen >> sst_probe.fsh >= 1u << 30 ? SSTB_LONG : 0u;
        if (bad) {
            sstp.rec[i] = -1;
            for (p = 0; p < SST_NPART; p++)
                if ((sst_part_free >> p & 1u) && sst_part_hash[p] == sstp.hash[i]) {
                    sstp.bad[p] = (uint8_t)(bad == SSTB_GONE ? 0u : bad);
                    sstp.missing |= (uint16_t)(1u << p);
                }
            continue;
        }
        if (sst_probe.fsh > 1u) {
            wide |= 1u << i;
            sst_free_kb(sstp.hash[i], 1u, 0u);
        }
        if (sstp.use[i] == SSTU_RAND && sst_probe.dlen <= SST_WHOLE_MAX)
            cand |= 1u << i;
    }
    sstp.rc = (uint8_t)sst_rcs_of();
    for (i = 0; i < sstp.nsrc; i++) {
        if (!(cand >> i & 1u))
            continue;
        sstp.whole |= 1u << i;
        sst_free_kb(sstp.hash[i], 0u, 1u);
        if (!arn_fits(SST_ARN_K, sst_need(), 0)) {
            sstp.whole &= ~(1u << i);                /* (it does not fit: through the cache) */
            sst_free_kb(sstp.hash[i], wide >> i & 1u, 0u);
        }
    }
}

/* 0 placed (or nothing to place), -1 refused: no room for every voice's ring and every preload, MEMORY FULL: CARD SOUNDS OFF with the
 * parts whose memory would make room (never fewer rings, shorter rings or shorter heads: the user, 2026-11-06 evening) */
static int sst_place(void)
{
    uint8_t *mem = 0;
    int r;
    if (!sstp.nsrc) {
        sstp.state = SSTP_IDLE;
        return 0;
    }
    fm1_irq_off();
    r = arn_reserve(SST_ARN_K, sstp.need);
    if (!r)
        mem = (uint8_t *)arn_take(SST_ARN_K);
    fm1_irq_on();
    if (r || !mem) {
        if (sstp.state != SSTP_REFUSED)
            sstp.said = 0;
        sstp.state = SSTP_REFUSED;
        sstp.drop = 0;
        for (r = 0; r < (int)SST_NPART; r++)        /* (the parts' blocks: one given back at a time) */
            if (arn_fits(SST_ARN_K, sstp.need, 1u << r))
                sstp.drop |= (uint8_t)(1u << r);
        sstp.drop_all = (uint8_t)arn_fits(SST_ARN_K, sstp.need, (1u << SST_NPART) - 1u);
        sstp.t0 = SD_NOW();
        return -1;
    }
    sst_load_all(mem);
    sstp.state = SSTP_PLACED;
    return 0;
}

static void sst_remount(void)                        /* a mount (sdcard.c): the samples' runs may be another card's */
{
    uint32_t i;
    sst_gate();
    for (i = 0; i < SST_N; i++)
        sst[i].on = 0;
    sst_stats_reset();
    sstp.dirty = 1;
}

static void sst_plan_poll(void)
{
    uint32_t i;
    if (sst_bank_sync())
        sstp.dirty = 1;                              /* (a part went on or off a card set, or to another one) */
    if ((sstp.state == SSTP_PLACED || sstp.state == SSTP_REFUSED) && sst_slicers() != sstp.slc)
        sstp.dirty = 1;                              /* (a part went on or off SLICE CARD: whole, or streamed) */
    if (sstp.state == SSTP_PLACED && sst_nring() != sstp.nring_want)
        sstp.dirty = 1;                              /* (a CARD part's voices changed: its rings) */
    if (sst_k != sd_ring_kb() * 2u && sstp.state != SSTP_GATED && !sst_pv.act && sst_pv.cmd != SPV_START)
        sstp.dirty = 1;                              /* (SYSTEM > SD CARD > RING changed: placed again at that size; the
                                                      * SAMPLES list's preview shares sst_k, so not while it plays) */
    if (sstp.dirty) {
        sstp.dirty = 0;
        sst_gate();
        sstp.state = SSTP_GATED;
        sstp.t0 = SD_NOW();
        return;
    }
    if (sstp.state == SSTP_GATED) {
        if ((uint32_t)(SD_NOW() - sstp.t0) < SST_GATE_MS * SD_TICKS_MS)
            return;
        for (i = 0; i < SST_N; i++)
            sst[i].on = 0;
        sst_rings(0, 0);
        for (i = 0; i < SST_NPART; i++)
            sst_rc_place(i, 0, 0);
        fm1_irq_off();
        arn_give(SST_ARN_K);                         /* (no voice reads it: gated two halves ago) */
        fm1_irq_on();
        if (!sst_pv.act && sst_pv.cmd != SPV_START)
            sst_k = sd_ring_kb() * 2u;               /* (no ring placed now: the setting's size; the heads follow it) */
        sst_lookup();
        sstp.nring = sstp.nring_want = (uint8_t)sst_nring();
        sstp.rc = (uint8_t)sst_rcs_of();
        sst_free_pick();
        sstp.need = sst_need();
        (void)sst_place();
        return;
    }
    if (sstp.state == SSTP_REFUSED && (uint32_t)(SD_NOW() - sstp.t0) >= SST_RETRY_MS * SD_TICKS_MS) {
        if (arn_fits(SST_ARN_K, sstp.need, 0))
            (void)sst_place();
        else
            sstp.t0 = SD_NOW();
    }
}
