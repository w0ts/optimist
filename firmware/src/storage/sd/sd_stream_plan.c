/* SPDX-License-Identifier: GPL-3.0-only */
/* The streams' memory per project (docs/SD-CARD-DESIGN.md 7, the user's rulings of 2026-10-26; the arena:
 * docs/SD-MODE-DESIGN.md 6b): the samples the project plays from the card (each part's CARD sample, by its hash), their
 * HEADS (the pre-buffer: the first sst_pre_ms of each, the one global setting) and the streams' rings, in ONE block of
 * the arena (its client SST_ARN_K, engines.c), placed when the project's samples or the setting change and held only
 * while the project plays card samples. Main loop only (sst_plan_poll every pass); included by sdcard.c after
 * sd_stream.c and sd_index.c (FELUCCA_SD_STREAM).
 *
 *   sst_part_set(part, hash)   part's CARD sample (0: none): the project's load and the sample picker call it; slot
 *                              SST_NPART + l: drum lane l's card sample (sd_mode.c, every pass: the lanes on CARD)
 *   sst_pre_set(ms)            the pre-buffer, ms (SST_PRE_MS: 25 50 100 200; default 50)
 *   sst_pre_kb(ms)             what the heads would take at that setting, KB (the setting's page shows it beside it)
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
 *                              together would), fit_ms (the longest pre-buffer that fits as it is, 0 none), said (the UI
 *                              said MEM FULL CARD once);
 *                              missing (a mask of slots whose sample is not on the card: MISSING, never another sound)
 * A refusal keeps the project's samples silent (their parts' CARD voices play nothing), the project unchanged, and is
 * tried again every SST_RETRY_MS (a part's change, an effect let go: room): placed by itself once it fits, as the
 * arena's other buffers are (core/arena_bufs.c). */
#ifndef SST_PART_VOICES
#define SST_PART_VOICES(p) SST_N                     /* (sdcard.c: the voices part p can sound at once) */
#endif
/* a part's rings at most: its card notes at once (a 5th is silent and counted, sst_nostream). Measured 2026-10-26 on the
 * default project (the emulator, SD card build): the effects' lines held 68 KB of the 96 KB arena, and a SAMPLE part's 8
 * rings of 4 KB plus its head (37,192 B) never fit beside them (MEM FULL CARD at the first USE); 4 rings, about 21 KB, do */
#ifndef SST_PART_RINGS
#define SST_PART_RINGS 4u
#endif
/* the card lanes (slots SST_NPART + lane, FELUCCA_SD_LANES): a ring each (a lane's hit cuts its own hit before:
 * drums/lane_card.c), sharing the SST_N rings with the parts */
/* SLICE on CARD (sd_slice.c, SST_PART_SLICE): that part's sample goes into the block whole (ADPCM), no ring or head for
 * it; a part going on or off it places again */
#define SST_RETRY_MS 200u
#define SST_GATE_MS 20u                              /* the audio's two halves (11.6 ms) and some: no voice reads it */
enum { SSTP_IDLE = 0, SSTP_GATED, SSTP_PLACED, SSTP_REFUSED };
static const uint16_t SST_PRE_MS[4] = {25, 50, 100, 200};
static uint16_t sst_pre_ms = 50;                     /* (the setting) */
static uint64_t sst_part_hash[SST_NSLOT] IN_POOL;    /* each part's CARD sample, then each card lane's; 0 none */

static struct {
    uint8_t dirty, state, said;
    uint8_t drop, drop_all;
    uint16_t missing;
    uint16_t fit_ms;
    uint32_t need, t0;
    int32_t rec[SST_NSRC];                           /* each source's index record (sdx_read), -1 none */
    uint64_t hash[SST_NSRC];
    uint8_t use[SST_NSRC];                           /* SSTU_STREAM: a slot streams it, SSTU_SLICE: a SLICE part's */
    uint8_t nsrc, nring, slc;                        /* slc: the slots on SLICE CARD when placed (sst_slicers) */
} sstp IN_POOL;
enum { SSTU_STREAM = 1, SSTU_SLICE = 2 };

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

static uint32_t sst_nring(void)                      /* the rings the CARD parts (SST_PART_RINGS each at most) and the card
                                                      * lanes can use at once */
{
    uint32_t p, n = 0;
    uint32_t sl = sst_slicers();
    for (p = 0; p < SST_NSLOT; p++)
        if (sst_part_hash[p] && !((sl >> p) & 1u))
            n += p < SST_NPART ? (SST_PART_VOICES(p) < SST_PART_RINGS ? SST_PART_VOICES(p) : SST_PART_RINGS) : 1u;
    return n < SST_N ? n : SST_N;
}

static void sst_part_set(uint32_t part, uint64_t hash)   /* (part: a slot, SST_NPART + lane a card lane) */
{
    if (part < SST_NSLOT && sst_part_hash[part] != hash) {
        sst_part_hash[part] = hash;
        sstp.dirty = 1;
    }
}

static void sst_pre_set(uint32_t ms)
{
    uint32_t i, m = SST_PRE_MS[1];
    for (i = 0; i < 4u; i++)
        if (ms == SST_PRE_MS[i])
            m = ms;
    if (m != sst_pre_ms) {
        sst_pre_ms = (uint16_t)m;
        sstp.dirty = 1;
    }
}

static FRESULT sst_rec_path(int32_t no, char *path, uint32_t cap)   /* the source's file, from the card's root */
{
    FRESULT r = sdx_read((uint32_t)no, &sdx.rec);
    if (r != FR_OK)
        return r;
    if (!sdl_path(path, cap, SDL_N, sdx.rec.path))  /* ("OPTIMIST/" + the record's path) */
        return FR_INVALID_NAME;
    return FR_OK;
}

static uint32_t sst_need(uint32_t pre_ms)           /* the block at that setting: the rings, then each source */
{
    static char path[SDX_PATH + 16];
    uint32_t i, b, n = sstp.nring * SST_K * 512u;
    for (i = 0; i < sstp.nsrc; i++) {
        if (sstp.rec[i] < 0 || sst_rec_path(sstp.rec[i], path, sizeof path) != FR_OK)
            continue;
        if ((sstp.use[i] & SSTU_STREAM) && sst_src_need(path, pre_ms, &b) == FR_OK)
            n += (b + 3u) & ~3u;
#ifdef SST_PART_SLICE
        if ((sstp.use[i] & SSTU_SLICE) && ssl_need(path, &b) == FR_OK)
            n += (b + 3u) & ~3u;
#endif
    }
    return n;
}

static uint32_t sst_slice_need(void)                 /* the SLICE parts' samples: no pre-buffer in them */
{
#ifdef SST_PART_SLICE
    static char path[SDX_PATH + 16];
    uint32_t i, b, n = 0;
    for (i = 0; i < sstp.nsrc; i++)
        if ((sstp.use[i] & SSTU_SLICE) && sstp.rec[i] >= 0 && sst_rec_path(sstp.rec[i], path, sizeof path) == FR_OK &&
            ssl_need(path, &b) == FR_OK)
            n += (b + 3u) & ~3u;
    return n;
#else
    return 0;
#endif
}

static uint32_t sst_pre_kb(uint32_t ms)
{
    return (sst_need(ms) - sstp.nring * SST_K * 512u - sst_slice_need() + 1023u) / 1024u;
}

static void sst_gate(void)                           /* the CARD voices let go: they fade out (eng_sample.c) */
{
    uint32_t i;
    for (i = 0; i < SST_NSLOT; i++)
        sst_part_src[i] = 0xFF;
#ifdef SST_PART_SLICE
    for (i = 0; i < SST_NPART; i++)
        ssl_set(i, 0);                               /* (SLICE's voices on it fall silent: eng_slice.c slc_get) */
#endif
    for (i = 0; i < SST_NSRC; i++)
        sst_src[i].ready = 0;
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
        for (i = 0; i < sstp.nsrc && sstp.hash[i] != sst_part_hash[p]; i++)
            ;
        if (i == sstp.nsrc && i < SST_NSRC) {
            sstp.hash[i] = sst_part_hash[p];
            sstp.rec[i] = sdx_find_hash(sst_part_hash[p], &sdx.rec);
            sstp.use[i] = 0;
            sstp.nsrc++;
        }
        if (i < sstp.nsrc)
            sstp.use[i] |= (uint8_t)(((sstp.slc >> p) & 1u) ? SSTU_SLICE : SSTU_STREAM);
        if (i >= sstp.nsrc || sstp.rec[i] < 0)
            sstp.missing |= (uint16_t)(1u << p);
    }
}

static void sst_load_all(uint8_t *mem)               /* the rings, then each source; each part to its source */
{
    static char path[SDX_PATH + 16];
    static const void *tab[SST_NSRC];
    uint32_t i, p, at = sstp.nring * SST_K * 512u, used;
    sst_rings(mem, sstp.nring);
    for (i = 0; i < sstp.nsrc; i++) {
        tab[i] = 0;
        if (sstp.rec[i] < 0 || sst_rec_path(sstp.rec[i], path, sizeof path) != FR_OK) {
            sstp.rec[i] = -1;
            continue;
        }
        if (sstp.use[i] & SSTU_STREAM) {
            if (sst_src_load(i, path, sst_pre_ms, mem + at, sstp.need - at, &used) != FR_OK) {
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
    for (p = 0; p < SST_NSLOT; p++)
        for (i = 0; sst_part_hash[p] && i < sstp.nsrc; i++)
            if (sstp.hash[i] == sst_part_hash[p]) {
                if (sstp.rec[i] < 0)
                    sstp.missing |= (uint16_t)(1u << p);
#ifdef SST_PART_SLICE
                else if ((sstp.slc >> p) & 1u)
                    ssl_set(p, tab[i]);
#endif
                else
                    sst_part_src[p] = (uint8_t)i;
            }
}

static int sst_place(void)                           /* 0 placed (or nothing to place), -1 refused */
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
        sstp.fit_ms = 0;
        for (r = 3; r >= 0 && !sstp.fit_ms; r--)
            if (SST_PRE_MS[r] < sst_pre_ms && arn_fits(SST_ARN_K, sst_need(SST_PRE_MS[r]), 0))
                sstp.fit_ms = SST_PRE_MS[r];
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
    if ((sstp.state == SSTP_PLACED || sstp.state == SSTP_REFUSED) && sst_slicers() != sstp.slc)
        sstp.dirty = 1;                              /* (a part went on or off SLICE CARD: whole, or streamed) */
    if (sstp.state == SSTP_PLACED && sst_nring() != sstp.nring)
        sstp.dirty = 1;                              /* (a CARD part's voices changed: its rings) */
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
        fm1_irq_off();
        arn_give(SST_ARN_K);                         /* (no voice reads it: gated two halves ago) */
        fm1_irq_on();
        sst_lookup();
        sstp.nring = (uint8_t)sst_nring();
        sstp.need = sst_need(sst_pre_ms);
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
