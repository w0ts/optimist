/* SPDX-License-Identifier: GPL-3.0-only */
/* Samples streamed from the SD card: the main loop's side (docs/SD-CARD-DESIGN.md 7, "Streaming"; the shared types and
 * the audio's reads: sd_stream.h). Full FatFs builds with the builder's SD_RING (FELUCCA_SD_STREAM); included by
 * sdcard.c after sd_diskio.c, and by tests/sd_stream_test.c on the card model. Main loop only, never in an interrupt.
 *
 *   sst_src_load(si, path, pre_ms, mem, n, &used)   source si from a WAV on the card (path from the card's root): its
 *                         format, its data's place, its runs on the card (FatFs' fast-seek map, once), its head (the
 *                         first pre_ms of its data) into mem (n bytes; mem 0: only the bytes it needs, in *used).
 *                         FR_OK; FR_INVALID_OBJECT a file the streams cannot play (not a PCM WAV of 8 or 16 bits, mono
 *                         or stereo, up to 4 x 44.1 kHz); FR_DENIED more runs than SST_MAP_N / 2 - 1 (a computer's
 *                         file on a card formatted with small clusters); FR_NOT_ENOUGH_CORE n too small
 *   sst_src_need(path, pre_ms, &bytes)   the same, the bytes only
 *   sst_rings(mem, n)     the first n streams' rings: n x SST_K x 512 B from mem (4-byte aligned); the others have
 *                         none (mem 0: every stream off)
 *   sst_service()         one run for the stream with the least time left that has a free slot: what the main loop's
 *                         idle wait calls (main.c); 1 a run was made (the bus was the card's meanwhile)
 *   sst_strip()           the same between two of the screen's strips (lcd.c lcd_blit), only for a stream with less
 *                         than SST_URGENT_MS left: the screen draws on otherwise
 *   sst_stats / sst_stats_reset()   the console's sd stream
 *
 * A RUN is one CMD18 of at most SST_RUN blocks into the stream's free slots, in play order and inside one of the file's
 * contiguous runs (a loop's jump or a run's end ends it): the screen cannot draw while it lasts (deselecting the card
 * would abort it), so it is short (8 blocks, 4 KB, about 2.2 ms at 20 MHz) and the screen's strips go between runs. A
 * card that does not send a block's token within SST_WAIT_US (busy: 100 ms and more now and then) is let go (CMD12, the
 * bus back to the screen) and asked again SST_BACKOFF_MS later: a stall never freezes the screen, the rings and the
 * heads carry the voices through it, or a voice runs dry and fades out (counted). A block whose CRC16 fails is read
 * again (sd_crc_retry: a second failure at 30 MHz drops to 20); a card that fails SST_DEAD_ERRS runs in a row is gone
 * (sd_gone, as a disk read's failure): the voices run dry and fade, nothing waits on it. A stream whose voice is no
 * longer rendered (its beat still for SST_DEAD_MS: a release's end, a steal) is taken back. */
#include "sd_stream.h"

#ifndef SST_RUN
#define SST_RUN 8u                                   /* blocks a run at most: the screen's longest wait (a parameter) */
#endif
#ifndef SST_WAIT_US
#define SST_WAIT_US 3000u                            /* a block's token not come in this: the run let go */
#endif
#ifndef SST_BACKOFF_MS
#define SST_BACKOFF_MS 2u                            /* then the card left alone this long (the screen draws) */
#endif
#ifndef SST_URGENT_MS
#define SST_URGENT_MS 40u                            /* between strips: a run only for a stream with less left */
#endif
#define SST_DEAD_MS 50u                              /* a voice not rendered this long: its stream taken back */
#define SST_DEAD_ERRS 3u                             /* runs failing in a row: the card is gone */
#define SST_NONE 0xFFFFFFFFu
#ifndef SST_IRQ_OFF
#define SST_IRQ_OFF() ((void)0)                      /* (sdcard.c: the interrupts; a host test: none) */
#define SST_IRQ_ON() ((void)0)
#endif
#ifndef SST_MAP_N
#define SST_MAP_N 64u                                /* the fast-seek map's words while a source loads: 31 runs */
#endif

static struct {
    uint32_t runs, blocks, stalls, crc, errors, reaped, aborted;
    uint32_t run_max_us;                             /* the longest run: the screen's longest wait for the card */
    uint32_t lead_min_ms;                            /* the least time a stream had left when a run began */
    uint32_t t0;                                     /* (SD_NOW at the reset: KB/s) */
    uint32_t backoff_t0;
    uint8_t backoff, errs, in;
} sst_st IN_POOL;
static DWORD sst_map[SST_MAP_N] IN_POOL;

static uint32_t sst_us(uint32_t ticks) { return (uint32_t)((uint64_t)ticks * 1000u / SD_TICKS_MS); }

static void sst_stats_reset(void)
{
    sst_dry = sst_nostream = 0;
    sst_st.runs = sst_st.blocks = sst_st.stalls = sst_st.crc = sst_st.errors = sst_st.reaped = sst_st.aborted = 0;
    sst_st.run_max_us = 0;
    sst_st.lead_min_ms = SST_NONE;
    sst_st.t0 = SD_NOW();
}

static void sst_rings(uint8_t *mem, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < SST_N; i++) {
        sst[i].on = 0;
        sst[i].ring = mem && i < n ? mem + i * SST_K * 512u : 0;
    }
}

/* ---- play order: the file's sectors a stream reads, after its head ---- */
static uint32_t sst_first(const sst_src_t *s, const sst_t *st)
{
    if (s->hlen >= s->dlen || (st->lpend && st->lpend <= s->hlen))
        return SST_NONE;                             /* (all of it in the head: nothing from the card) */
    return (s->dofs + s->hlen) >> 9;
}

static uint32_t sst_after(const sst_src_t *s, const sst_t *st, uint32_t sec)
{
    uint32_t b;
    if (st->lpend && sec == (s->dofs + st->lpend - 1u) >> 9) {   /* the loop's end: back to its start (past the head) */
        b = st->lpstart > s->hlen ? st->lpstart : s->hlen;
        return (s->dofs + b) >> 9;
    }
    return sec < (s->dofs + s->dlen - 1u) >> 9 ? sec + 1u : SST_NONE;
}

static uint32_t sst_lba(const sst_src_t *s, uint32_t sec, uint32_t *left)   /* the card's sector, and the run's rest */
{
    uint32_t i;
    for (i = 0; i < s->nrun; i++)
        if (sec - s->run[i].fsec < s->run[i].n) {
            *left = s->run[i].n - (sec - s->run[i].fsec);
            return s->run[i].lba + (sec - s->run[i].fsec);
        }
    *left = 0;
    return 0;
}

/* the main loop's view of stream i: reset for a new note, taken back when its voice is gone; its lead (ms of data
 * ahead of the voice) when it has a free slot and more to read, else SST_NONE */
static uint32_t sst_lead(uint32_t i, uint32_t now)
{
    sst_t *st = &sst[i];
    const sst_src_t *s;
    uint32_t ahead, used;
    if (!st->on)
        return SST_NONE;
    s = &sst_src[st->src];
    if (st->pgen != st->gen) {                       /* a new note: from the head's end */
        st->pgen = st->gen;
        st->wr = 0;
        st->fails = 0;
        st->next = sst_first(s, st);
        st->done = st->next == SST_NONE;
        st->beat_seen = st->beat;
        st->beat_t = now;
    }
    if (st->owner && !*st->owner)                    /* (its voice is over: the audio takes it for a new note; never
                                                      * cleared here, that note may be starting right now) */
        return SST_NONE;
    if (st->beat != st->beat_seen) {
        st->beat_seen = st->beat;
        st->beat_t = now;
    } else if (now - st->beat_t > SST_DEAD_MS * SD_TICKS_MS) {   /* (the voice is no longer rendered) */
        SST_IRQ_OFF();                               /* (not a note the audio starts on it meanwhile) */
        if (st->gen == st->pgen && st->beat == st->beat_seen && !(st->owner && !*st->owner)) {
            st->on = 0;
            sst_st.reaped++;
        }
        SST_IRQ_ON();
        return SST_NONE;
    }
    used = st->wr - st->rd;
    if (st->done || st->dry || used >= SST_K)
        return SST_NONE;
    ahead = used * 512u + (!st->in_ring && st->pos < s->hlen ? s->hlen - st->pos : 0u);
    return ahead / (st->bpms ? st->bpms : 1u);
}

/* a token not come in SST_WAIT_US, or the stream's note started again meanwhile: let go (0); 1 the block is in */
static BYTE sst_block(sst_t *st, uint8_t *buf, uint8_t gen, BYTE *res)
{
    uint32_t t0 = SD_NOW();
    BYTE r;
    sd_rd_next(buf);
    while ((r = sd_rd_poll()) == SDR_BUSY)
        if (sst_us(SD_NOW() - t0) >= SST_WAIT_US || st->gen != gen) {
            *res = SDR_BUSY;
            return 0;
        }
    *res = r;
    return r == SDR_READY && sd_rd.crc_ok;
}

/* one run for stream i: up to SST_RUN blocks into its free slots */
static void sst_run(uint32_t i)
{
    sst_t *st = &sst[i];
    const sst_src_t *s = &sst_src[st->src];
    uint8_t gen = st->pgen;
    uint32_t left, lba = sst_lba(s, st->next, &left), n = 0, sec = st->next, k, t0, slot;
    BYTE res = SDR_ERR, ok = 1;
    for (k = st->wr - st->rd; n < SST_RUN && n < left && k + n < SST_K; ) {   /* contiguous in play order and on the card */
        n++;
        sec = sst_after(s, st, sec);
        if (sec != st->next + n)
            break;
    }
    if (!n) {                                        /* (a sector outside the runs: the map is wrong) */
        st->done = 1;
        sst_st.errors++;
        return;
    }
    t0 = SD_NOW();
    SD_SESSION();
    if (sd_rd_open(lba, n) != RES_OK) {
        ok = 0;
    } else {
        for (k = 0; k < n; k++) {
            slot = st->wr % SST_K;
            st->tag[slot] = 0;                       /* (given back by the audio: it reads it again only once tagged) */
            if (!sst_block(st, st->ring + slot * 512u, gen, &res))
                break;
            if (st->gen != gen)
                break;
            st->tag[slot] = sst_tag(gen, st->next);
            st->wr++;
            st->fails = 0;
            st->next = sst_after(s, st, st->next);
            sst_st.blocks++;
        }
        if (k < n) {
            if (res == SDR_BUSY && st->gen == gen) {   /* the card busy: the bus back to the screen for a while */
                sst_st.stalls++;
                sst_st.backoff = 1;
                sst_st.backoff_t0 = SD_NOW();
            } else if (res == SDR_READY && st->gen == gen) {   /* the CRC16 failed: read again (or the card is bad) */
                sst_st.crc++;
                ok = sd_crc_retry(&st->fails);
            } else if (st->gen != gen) {
                sst_st.aborted++;
            } else {
                ok = 0;                              /* an error token, a DMA that never ended */
            }
        }
        if (sd_rd_close() != RES_OK)
            ok = 0;
    }
    SD_SESSION_END();
    t0 = sst_us(SD_NOW() - t0);
    if (t0 > sst_st.run_max_us)
        sst_st.run_max_us = t0;
    sst_st.runs++;
    if (st->next == SST_NONE)
        st->done = 1;
    if (ok) {
        sst_st.errs = 0;
        return;
    }
    sst_st.errors++;
    if (++sst_st.errs >= SST_DEAD_ERRS)
        sd_gone = 1;                                 /* (as a disk read's failure: every later call fails at once) */
}

static uint32_t sst_pick(uint32_t limit_ms)
{
    uint32_t i, best = SST_NONE, lead, best_lead = SST_NONE, now = SD_NOW();
    for (i = 0; i < SST_N; i++) {
        lead = sst_lead(i, now);
        if (lead < best_lead) {
            best_lead = lead;
            best = i;
        }
    }
    if (best == SST_NONE || best_lead >= limit_ms)
        return SST_NONE;
    if (best_lead < sst_st.lead_min_ms)
        sst_st.lead_min_ms = best_lead;
    return best;
}

#ifndef SST_CARD_OK
#define SST_CARD_OK() 1                              /* (sdcard.c: the card mounted) */
#endif
static uint32_t sst_go(uint32_t limit_ms)
{
    uint32_t i;
    if (sst_st.in || sd_gone || sd_wr.state != SDW_IDLE || !SST_CARD_OK())   /* (a recording writing: it waits) */
        return 0;
    if (sst_st.backoff) {
        if (sst_us(SD_NOW() - sst_st.backoff_t0) < SST_BACKOFF_MS * 1000u)
            return 0;
        sst_st.backoff = 0;
    }
    sst_st.in = 1;
    i = sst_pick(limit_ms);
    if (i != SST_NONE)
        sst_run(i);
    sst_st.in = 0;
    return i != SST_NONE;
}

static uint32_t sst_service(void) { return sst_go(SST_NONE); }
static uint32_t sst_strip(void) { return sst_go(SST_URGENT_MS); }

static uint32_t sst_kbs(void)                        /* KB/s read since the stats' reset */
{
    uint32_t ms = sst_us(SD_NOW() - sst_st.t0) / 1000u;
    return ms ? (uint32_t)((uint64_t)sst_st.blocks * 512u / ms) : 0u;   /* (B/ms = KB/s, near enough: 1000 / 1024) */
}

/* ---- sources: a WAV on the card ---- */
static uint32_t sst_le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* the "fmt " and "data" chunks: FR_OK with s's format and data set, FR_INVALID_OBJECT not a WAV the streams play */
static FRESULT sst_wav(FIL *fp, sst_src_t *s, uint8_t *buf)
{
    UINT got;
    uint32_t at = 12, size = (uint32_t)f_size(fp), n, fmt = 0, rate = 0, bits = 0, ch = 0, ba = 0;
    FRESULT r;
    if ((r = f_lseek(fp, 0)) != FR_OK || (r = f_read(fp, buf, 12, &got)) != FR_OK)
        return r;
    if (got < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
        return FR_INVALID_OBJECT;
    s->dlen = 0;
    while (at + 8u <= size) {
        if ((r = f_lseek(fp, at)) != FR_OK || (r = f_read(fp, buf, 8, &got)) != FR_OK)
            return r;
        if (got < 8)
            break;
        n = sdh_le32(buf + 4);
        if (!memcmp(buf, "fmt ", 4) && n >= 16u) {
            if ((r = f_read(fp, buf + 8, 26, &got)) != FR_OK)
                return r;
            if (got < 16)
                break;
            fmt = sst_le16(buf + 8);
            ch = sst_le16(buf + 10);
            rate = sdh_le32(buf + 12);
            ba = sst_le16(buf + 20);
            bits = sst_le16(buf + 22);
            if (fmt == 0xFFFEu && n >= 40u && got >= 26)   /* WAVE_FORMAT_EXTENSIBLE: the sub-format's tag */
                fmt = sst_le16(buf + 32);
        } else if (!memcmp(buf, "data", 4)) {
            s->dofs = at + 8u;
            s->dlen = n < size - s->dofs ? n : size - s->dofs;
            break;
        }
        if (n > size - at - 8u)
            break;
        at += 8u + n + (n & 1u);
    }
    if (fmt != 1u || (bits != 8u && bits != 16u) || (ch != 1u && ch != 2u) || ba != ch * bits / 8u || !rate ||
        rate > 4u * 44100u || !s->dlen || (s->dofs & 1u))
        return FR_INVALID_OBJECT;
    s->bits = bits == 16u ? SST_F16 : SST_F8;
    s->chans = (uint8_t)ch;
    s->fsh = (uint8_t)((bits == 16u) + (ch == 2u));
    s->frames = s->dlen >> s->fsh;
    s->dlen = s->frames << s->fsh;                   /* (whole frames) */
    s->rate = (uint32_t)(((uint64_t)rate << 16) / 44100u);
    s->hash = 0;
    if (!s->frames)
        return FR_INVALID_OBJECT;
    return FR_OK;
}

static uint32_t sst_head_bytes(const sst_src_t *s, uint32_t pre_ms)   /* pre_ms of the data at its own rate */
{
    uint64_t b = (uint64_t)pre_ms * s->rate * 441u / (10u * 65536u);   /* (frames) */
    uint32_t h = (uint32_t)((b << s->fsh) + 3u) & ~3u;
    return h <= s->dlen ? h : s->dlen & ~3u;
}

static FRESULT sst_src_load(uint32_t si, const char *path, uint32_t pre_ms, uint8_t *mem, uint32_t n, uint32_t *used)
{
    static FIL f IN_POOL;
    static uint8_t hd[64] __attribute__((aligned(4)));
    sst_src_t s, *d = &sst_src[si];
    const FATFS *fs;
    sst_run_t *run = (sst_run_t *)mem;
    uint32_t nrun, i, fsec = 0, cs, need, sects;
    UINT got;
    FRESULT r;
    *used = 0;
    if (si >= SST_NSRC)
        return FR_INVALID_PARAMETER;
    if (mem)
        d->ready = 0;                                /* (the audio no longer starts it; the caller waited for its voices) */
    memset(&s, 0, sizeof s);
    if ((r = f_open(&f, path, FA_READ)) != FR_OK)
        return r;
    if ((r = sst_wav(&f, &s, hd)) != FR_OK)
        goto out;
    sst_map[0] = SST_MAP_N;
    f.cltbl = sst_map;
    if ((r = f_lseek(&f, CREATE_LINKMAP)) != FR_OK) {
        r = r == FR_NOT_ENOUGH_CORE ? FR_DENIED : r;
        goto out;
    }
    nrun = (sst_map[0] - 2u) / 2u;
    s.hlen = sst_head_bytes(&s, pre_ms);
    need = nrun * (uint32_t)sizeof(sst_run_t) + s.hlen;
    *used = need;
    if (!mem)
        goto out;
    if (need > n || ((uintptr_t)mem & 3u)) {
        r = FR_NOT_ENOUGH_CORE;
        goto out;
    }
    fs = f.obj.fs;
    cs = fs->csize;
    sects = ((uint32_t)f_size(&f) + 511u) / 512u;
    for (i = 0; i < nrun; i++) {                     /* (pairs: a run's clusters, its first cluster) */
        run[i].fsec = fsec;
        run[i].lba = (uint32_t)fs->database + (sst_map[2u + 2u * i] - 2u) * cs;
        run[i].n = sst_map[1u + 2u * i] * cs;
        if (fsec + run[i].n > sects)
            run[i].n = sects > fsec ? sects - fsec : 0u;   /* (the last cluster's tail is past the file's end) */
        fsec += run[i].n;
    }
    s.run = run;
    s.nrun = (uint16_t)nrun;
    s.head = mem + nrun * sizeof(sst_run_t);
    f.cltbl = 0;
    if (s.hlen && ((r = f_lseek(&f, s.dofs)) != FR_OK || (r = f_read(&f, (uint8_t *)s.head, s.hlen, &got)) != FR_OK ||
                   got != s.hlen)) {
        r = r != FR_OK ? r : FR_INT_ERR;
        goto out;
    }
    s.ready = 0;
    *d = s;
    d->ready = 1;
out:
    f.cltbl = 0;
    (void)f_close(&f);
    return r;
}

static FRESULT sst_src_need(const char *path, uint32_t pre_ms, uint32_t *bytes)
{
    return sst_src_load(0, path, pre_ms, 0, 0, bytes);
}
