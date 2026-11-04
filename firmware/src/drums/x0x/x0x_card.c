/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X 909's hats, crash and ride on the SD card (SD card mode, registry.h X909_CARD; the user, 2026-11-04: "the
 * xox samples etc should also be on the sd card", docs/SD-CARD-DESIGN.md 9): its four sample voices (drum909.c d9_smp_t:
 * CH OH CR RD) read the card's Optimist/Samples/X0X/909/hh.wav, crash.wav, ride.wav (tools/sd_samples.py: the sources'
 * own 44.1 kHz samples, the 24-bit hat rounded to 16 bits) through a stream each (storage/sd/sd_stream.h), no sample in
 * the flash. The float side (x0x_drums.c, a unit of its own) calls these with integers only. Included by drum_x0x.c.
 *   x9c_want()            the files the lanes' 909 sounds play now, a mask (sd_stream_plan.c SST_X9_WANT: the main loop's)
 *   x9c_start(v, pitchq)  a hit of voice v at rate pitchq (Q16 of the file's own): its stream from the head, its frames;
 *                         0 not on the card (sd_mode.c says MISSING once) or no ring free (counted): the voice silent
 *   x9c_at(v, p)          frame p (int16 scale), the last two kept (the interpolator reads p + 1, then p); a stream
 *                         that runs dry (counted, sst_dry) fades its last sample out over X9C_FADE, then 0
 *   x9c_stop(v)           the voice is over (its decay, its end, a choke's): the stream back */
#define X9C_FADE 64u                               /* 1.5 ms */
static const char *const X9C_FILE[SST_NX9] = {"hh.wav", "crash.wav", "ride.wav"};
static const uint8_t X9C_OF[4] = {0, 0, 1, 2};       /* CH OH CR RD -> the file */
static struct {
    volatile uint8_t on[4];                          /* (each stream's owner: 0, the stream is free) */
    uint8_t k[4], si[4], fade[4];
    uint32_t f0[4], f1[4];                           /* the frames read last, and before */
    int32_t x0[4], x1[4];
} x9c;
#define SST_X9_WANT() x9c_want()
#define SST_X9_FILE(k) X9C_FILE[(k) % SST_NX9]

static uint32_t x9c_want(void)
{
    uint32_t l, m = 0, kit = drum_kit(), k, c;
    for (l = 0; l < DRUM_LANES; l++) {
        if (dl.src[l] == DL_CARD || dl_usr_of(l))
            continue;
        k = dl_kit_of(l, kit);
        if (!DRUM_IS_X0X(k) || DRUM_UID_XMACH(k) != DRUM_UID_X909)
            continue;
        c = x0x_code(k, l, LANE_NOTE_OF(l));
        if (c != XN_NONE && (c & 31u) >= 7u && (c & 31u) <= 10u)
            m |= 1u << X9C_OF[(c & 31u) - 7u];
    }
    return m;
}

void x9c_stop(uint32_t v)
{
    v &= 3u;
    if (x9c.on[v] && x9c.k[v] < SST_N && sst[x9c.k[v]].owner == &x9c.on[v])
        sst_stop(x9c.k[v]);
    x9c.on[v] = 0;
    x9c.fade[v] = 0;
}

uint32_t x9c_start(uint32_t v, uint32_t pitchq)
{
    uint32_t si;
    int32_t k;
    v &= 3u;
    x9c_stop(v);
    si = sst_x9_src[X9C_OF[v]];
    if (si >= SST_NSRC || !sst_src[si].ready)
        return 0;                                    /* (not on the card, or not placed yet: silent) */
    k = sst_start(si, 0, 0, 0, sst_rate(&sst_src[si], pitchq ? pitchq : 65536u), &x9c.on[v]);
    if (k < 0)
        return 0;                                    /* (no ring free: counted, sst_nostream) */
    x9c.k[v] = (uint8_t)k;
    x9c.si[v] = (uint8_t)si;
    x9c.f0[v] = x9c.f1[v] = 0xFFFFFFFFu;
    x9c.x0[v] = x9c.x1[v] = 0;
    x9c.on[v] = 1;                                   /* (last: the stream is the voice's from here) */
    return sst_src[si].frames;
}

HOT int32_t x9c_at(uint32_t v, uint32_t p)
{
    sst_t *st;
    int32_t x;
    v &= 3u;
    if (p == x9c.f1[v])
        return x9c.x1[v];
    if (p == x9c.f0[v])
        return x9c.x0[v];
    if (!x9c.on[v]) {                                /* (ended or dry: the last sample faded out, then 0) */
        if (!x9c.fade[v])
            return 0;
        x9c.fade[v]--;
        return x9c.x1[v] * (int32_t)x9c.fade[v] / (int32_t)X9C_FADE;
    }
    st = &sst[x9c.k[v] < SST_N ? x9c.k[v] : 0u];
    st->beat = st->beat + 1u;                        /* (rendered: the main loop keeps it) */
    if (!st->on || !sst_src[x9c.si[v]].ready || !sst_frame(st, &sst_src[x9c.si[v]], p, &x)) {
        if (st->on)
            sst_ran_dry(x9c.k[v]);                   /* (the card late: counted) */
        x9c.on[v] = 0;
        x9c.fade[v] = X9C_FADE;
        return x9c.x1[v];
    }
    x9c.f0[v] = x9c.f1[v];
    x9c.x0[v] = x9c.x1[v];
    x9c.f1[v] = p;
    x9c.x1[v] = x;
    return x;
}
