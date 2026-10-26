/* SPDX-License-Identifier: GPL-3.0-only */
/* SLICE on a card sample (engines/slice/eng_slice.c, SRC CARD): the part's card sample read whole into the streams'
 * block (sd_stream_plan.c) as IMA ADPCM, mono, at its own rate, and scanned for its slices as a user slot is. A
 * slicer jumps between slices, so the streams' one head a sample cannot start it at once; a whole one-bar break in
 * ADPCM (2 s: 44 KB) is less than a pre-buffer per slice (32 x 50 ms of 16-bit: 141 KB). Main loop only; included by
 * sdcard.c after sd_stream.c, before sd_stream_plan.c.
 *   SST_PART_SLICE(p)               part p is on SLICE with SRC CARD: its sample goes whole (no ring, no head for it)
 *   ssl_need(path, &bytes)          the bytes of its block (FR_INVALID_OBJECT: not a WAV the streams play)
 *   ssl_load(path, mem, n, &used)   read, encode, scan into mem (n bytes); the table, or 0 (its error in ssl_err)
 *   ssl_set(part, tab)              the part's CARD material (0: none; sst_gate, before the block is given back) */
#if FELUCCA_SLICE
#define SST_PART_SLICE(p) (trk[p].engine == (uint8_t)ENG_SLOT_SLICE && trk[p].p[P_E0] == (int16_t)SLC_SRC_CARD)
#define SSL_SLACK 8u                                 /* (the block's place 4-byte aligned, the table's 8) */

static FRESULT ssl_err;

static FRESULT ssl_need(const char *path, uint32_t *bytes)
{
    static FIL f IN_POOL;
    static uint8_t hd[64] __attribute__((aligned(4)));
    sst_src_t s;
    FRESULT r;
    *bytes = 0;
    memset(&s, 0, sizeof s);
    if ((r = f_open(&f, path, FA_READ)) != FR_OK)
        return r;
    r = sst_wav(&f, &s, hd);
    (void)f_close(&f);
    if (r == FR_OK)
        *bytes = slc_enc_bytes(s.frames) + SSL_SLACK;
    return r;
}

static const slc_src_t *ssl_load(const char *path, uint8_t *mem, uint32_t n, uint32_t *used)
{
    static FIL f IN_POOL;
    static uint8_t buf[512] __attribute__((aligned(4)));
    sst_src_t s;
    slc_enc_t e;
    uint8_t *at = (uint8_t *)(((uintptr_t)mem + 7u) & ~(uintptr_t)7u);
    uint32_t left, i, fb;
    UINT got;
    FRESULT r;
    const slc_src_t *tab = 0;
    *used = 0;
    memset(&s, 0, sizeof s);
    if ((r = f_open(&f, path, FA_READ)) != FR_OK)
        goto out;
    if ((r = sst_wav(&f, &s, buf)) != FR_OK)
        goto close;
    *used = slc_enc_bytes(s.frames) + SSL_SLACK;
    if (*used > n) {
        r = FR_NOT_ENOUGH_CORE;
        goto close;
    }
    if ((r = f_lseek(&f, s.dofs)) != FR_OK)
        goto close;
    slc_enc_begin(&e, at, s.frames, s.rate);
    fb = 1u << s.fsh;
    for (left = s.dlen; left;) {                     /* (512 B: whole frames, a frame is 1, 2 or 4 bytes) */
        uint32_t want = left < sizeof buf ? left : (uint32_t)sizeof buf;
        if ((r = f_read(&f, buf, want, &got)) != FR_OK)
            goto close;
        if (got != want) {
            r = FR_INT_ERR;
            goto close;
        }
        for (i = 0; i + fb <= got; i += fb) {
            const uint8_t *p = buf + i;
            int32_t a = s.bits == SST_F16 ? (int32_t)(int16_t)(p[0] | p[1] << 8) : ((int32_t)p[0] - 128) << 8;
            if (s.chans == 2) {
                const uint8_t *q = p + (fb >> 1);
                a = (a + (s.bits == SST_F16 ? (int32_t)(int16_t)(q[0] | q[1] << 8) : ((int32_t)q[0] - 128) << 8)) >> 1;
            }
            slc_enc_put(&e, a);
        }
        left -= got;
    }
    tab = slc_enc_end(&e);
    if (!tab)
        r = FR_INVALID_OBJECT;
close:
    (void)f_close(&f);
out:
    ssl_err = r;
    return r == FR_OK ? tab : 0;
}

static void ssl_set(uint32_t part, const void *tab)
{
    if (part < SST_NPART)
        slc_crd[part] = (const slc_src_t *)tab;
}
#endif
