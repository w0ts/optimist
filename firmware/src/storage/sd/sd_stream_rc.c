/* SPDX-License-Identifier: GPL-3.0-only */
/* Card samples read in any order: the main loop's side (the audio's, the caches, wants and pins: sd_stream_rc.h;
 * docs/SD-CARD-DESIGN.md 7, "Random access"). Included by sd_stream.c before its scheduler, which weighs a cache's most
 * urgent want against the streams' leads (sst_pick: the least time left first, between the screen's strips only under
 * SST_URGENT_MS, as a stream's) and runs it as one CMD18 of at most SST_RUN blocks (sst_rc_run).
 *
 *   sst_rc_place(p, mem, kb)  part p's cache of kb KB at mem (SST_RC_BYTES(kb), 4-byte aligned; the plan's block),
 *                         its tags clear;
 *                         0: none (the plan's gate: the audio stops reading it at once, the memory goes 20 ms later)
 *   sst_rc_lead(p, &w)    the cache's most urgent want that has a sector missing and a slot to put it in: its lead in
 *                         ms (its due on the audio's clock, less now), the range in w; the region when no want is
 *                         missing (lead SST_RC_IDLE: sst_service only, slots no one used for SST_RC_COLD); SST_NONE
 *   sst_rc_run(p, &w)     one run: the range's missing sectors from its first, contiguous in the file and on the
 *                         card, each into a VICTIM slot: an empty one, else the one used longest ago, never a pinned
 *                         or wanted one (its tag cleared first, the pins read after: a pin since puts it back)
 * The card busy (a token not come in SST_WAIT_US), a CRC failing, the card gone: as a stream's run (sd_stream.c). */
#define SST_RC_IDLE (SST_NONE - 1u)                  /* the region's lead: only when nothing else wants the bus */
#define SST_RC_SPAN 128u                             /* a range's sectors looked at at most (a want, the region) */
#define SST_RC_AHEAD_MS 30u                          /* a full cache takes a want only this close to its due: what is read
                                                      * ahead waits in the slots, and a grain's reader moves on through them */

static struct {
    uint32_t runs, blocks, cold_runs, kept;          /* runs, blocks read; the region's runs; victims pinned meanwhile */
    BYTE fails;                                      /* a block's CRC failures (sd_crc_retry) */
} sst_rcs IN_POOL;

static void sst_rc_stats_reset(void)
{
    uint32_t p;
    sst_rcs.runs = sst_rcs.blocks = sst_rcs.cold_runs = sst_rcs.kept = 0;
    for (p = 0; p < SST_NPART; p++)
        sst_rc[p].hit = sst_rc[p].late = sst_rc[p].miss = 0;
}

typedef struct {
    uint32_t lo, hi;                                 /* the range (tags of one source) */
    int32_t due;                                     /* its due, relative (the region: after every want's) */
    uint8_t cold;                                    /* the region */
    uint8_t k;                                       /* the want (SST_RW: the region) */
} sst_rcw_t;
static sst_rcw_t sst_rcw IN_POOL;                    /* (sst_pick's choice for sst_go) */

static void sst_rc_place(uint32_t p, uint8_t *mem, uint32_t kb)
{
    sst_rc_t *c = &sst_rc[p];
    uint32_t k;
    c->on = 0;
    SST_FENCE();
    if (!mem) {
        c->mem = 0;
        return;
    }
    c->nk = (uint8_t)(kb * 2u < SST_RK ? kb * 2u : SST_RK);
    c->tag = (uint32_t *)(void *)(mem + c->nk * 512u);   /* (after the slots: 4-byte aligned, the block is) */
    c->use = c->tag + c->nk;
    for (k = 0; k < c->nk; k++)
        c->tag[k] = c->use[k] = 0;
    sst_rc_reset(c);
    c->hit = c->late = c->miss = 0;
    c->mem = mem;
    c->gen = c->gen + 1u;
    SST_FENCE();
    c->on = 1;
}

AINL int sst_rc_in(uint32_t t, uint32_t lo, uint32_t hi) { return lo && t >= lo && t <= hi; }

static int sst_rc_pinned(const sst_rc_t *c, uint32_t t)
{
    uint32_t k, lo;
    for (k = 0; k < SST_RP; k++) {
        lo = c->plo[k];
        if (lo && t >= lo && t <= c->phi[k])
            return 1;
    }
    return 0;
}

/* sector tag t holds no data the reader takes from the cache (all of it below the head's end, or past the data) */
static int sst_rc_headonly(const sst_src_t *s, uint32_t t)
{
    uint32_t sec = sst_rc_s(t), end = (sec + 1u) << 9, dend = s->dofs + s->dlen;
    if (end > dend)
        end = dend;
    return end <= s->dofs + s->hlen || (sec << 9) >= dend;
}

/* each slot's standing (the main loop's, for one cache at a time): pinned or (a region run) warm in hard, else the
 * nearest due of the wants holding it (relative, INT32_MAX none). A want takes a victim among the slots not hard whose
 * nearest due comes after its own: an empty one, else one no want holds (used longest ago), else the one held for
 * the latest due (a want never takes a sooner one's) */
static int32_t sst_rc_md[SST_RK];
static void sst_rc_standing(const sst_rc_t *c, uint32_t *hard, int cold)
{
    uint32_t k, w, t, now = c->now;
    int32_t d;
    hard[0] = hard[1] = 0;
    for (k = 0; k < c->nk; k++) {
        t = c->tag[k];
        sst_rc_md[k] = INT32_MAX;
        if (!t)
            continue;
        if (sst_rc_pinned(c, t) || (cold && now - c->use[k] < SST_RC_COLD)) {
            hard[k >> 5] |= 1u << (k & 31u);
            continue;
        }
        for (w = 0; w < SST_RW; w++)
            if (sst_rc_in(t, c->wlo[w], c->whi[w])) {
                d = (int32_t)(c->wdue[w] - now);
                if (d >= -(int32_t)SST_RC_STALE && d < sst_rc_md[k])
                    sst_rc_md[k] = d;
            }
    }
}

/* the victims a want due at d (relative) may take: the slots not hard, held for no sooner due */
static uint32_t sst_rc_victims(const sst_rc_t *c, const uint32_t *hard, int32_t d)
{
    uint32_t k, n = 0;
    for (k = 0; k < c->nk; k++)
        n += !(hard[k >> 5] >> (k & 31u) & 1u) && sst_rc_md[k] > d + (int32_t)SST_RC_STALE;
    return n;
}

/* the first sector of lo .. hi (one source's tags) neither in the cache nor in the head: its tag, 0 none */
static uint32_t sst_rc_gap(const sst_rc_t *c, uint32_t lo, uint32_t hi)
{
    uint32_t have[SST_RC_SPAN / 32u] = {0}, k, t, n;
    const sst_src_t *s = &sst_src[sst_rc_si(lo) % SST_NSRC];
    if (!lo || hi < lo || sst_rc_si(hi) != sst_rc_si(lo) || !s->ready)
        return 0;
    n = hi - lo + 1u < SST_RC_SPAN ? hi - lo + 1u : SST_RC_SPAN;
    for (k = 0; k < c->nk; k++) {
        t = c->tag[k] - lo;
        if (t < n)
            have[t >> 5] |= 1u << (t & 31u);
    }
    for (t = 0; t < n; t++)
        if (!(have[t >> 5] >> (t & 31u) & 1u) && !sst_rc_headonly(s, lo + t))
            return lo + t;
    return 0;
}

static uint32_t sst_rc_lead(uint32_t p, sst_rcw_t *w)
{
    sst_rc_t *c = &sst_rc[p];
    uint32_t k, best = SST_NONE, lead, lo, hi, due, now, hard[2], empty = 0;
    int32_t d;
    if (!sst_rc_live(c))
        return SST_NONE;
    now = c->now;
    sst_rc_standing(c, hard, 0);
    for (k = 0; k < c->nk; k++)
        empty += !c->tag[k];
    for (k = 0; k < SST_RW; k++) {
        lo = c->wlo[k];
        hi = c->whi[k];
        due = c->wdue[k];
        if (!lo || c->wlo[k] != lo)
            continue;                                /* (none, or the audio changed it meanwhile) */
        d = (int32_t)(due - now);
        if (d < -(int32_t)SST_RC_STALE)
            continue;
        lead = d <= 0 ? 0u : (uint32_t)d * 10u / 441u;
        if (lead >= best || (lead > SST_RC_AHEAD_MS && !empty) || !sst_rc_gap(c, lo, hi) ||
            !sst_rc_victims(c, hard, d))
            continue;
        best = lead;
        w->lo = lo;
        w->hi = hi;
        w->cold = 0;
        w->k = (uint8_t)k;
        w->due = d;
    }
    if (best != SST_NONE)
        return best;
    lo = c->rlo;
    hi = c->rhi;
    if (!lo || c->rlo != lo || !sst_rc_gap(c, lo, hi))
        return SST_NONE;
    sst_rc_standing(c, hard, 1);
    if (!sst_rc_victims(c, hard, INT32_MAX - (int32_t)SST_RC_STALE - 1))
        return SST_NONE;
    w->lo = lo;
    w->hi = hi;
    w->cold = 1;
    w->k = (uint8_t)SST_RW;
    w->due = INT32_MAX - (int32_t)SST_RC_STALE - 1;  /* (only slots no want holds) */
    return SST_RC_IDLE;
}

/* a block of a cache's run: a token not come in SST_WAIT_US, or the cache let go meanwhile (0); 1 the block is in */
static BYTE sst_rc_block(const sst_rc_t *c, uint8_t *buf, BYTE *res)
{
    uint32_t t0 = SD_NOW();
    BYTE r;
    sd_rd_next(buf);
    while ((r = sd_rd_poll()) == SDR_BUSY)
        if (sst_us(SD_NOW() - t0) >= SST_WAIT_US || !c->on) {
            *res = SDR_BUSY;
            return 0;
        }
    *res = r;
    return r == SDR_READY && sd_rd.crc_ok;
}

/* the victim for the next block of a want due at d (sst_rc_standing's order); taken: hard from here; -1 none */
static int32_t sst_rc_victim(const sst_rc_t *c, uint32_t *hard, int32_t d)
{
    uint32_t k, now = c->now, age, oldest = 0;
    int32_t v = -1, md = 0;
    for (k = 0; k < c->nk; k++) {
        if ((hard[k >> 5] >> (k & 31u) & 1u) || sst_rc_md[k] <= d + (int32_t)SST_RC_STALE)
            continue;
        if (!c->tag[k]) {
            v = (int32_t)k;
            break;
        }
        age = now - c->use[k];
        if (v < 0 || sst_rc_md[k] > md || (sst_rc_md[k] == md && age > oldest)) {
            oldest = age;
            md = sst_rc_md[k];
            v = (int32_t)k;
        }
    }
    if (v >= 0)
        hard[(uint32_t)v >> 5] |= 1u << ((uint32_t)v & 31u);
    return v;
}

static void sst_rc_run(uint32_t p, sst_rcw_t *w)
{
    sst_rc_t *c = &sst_rc[p];
    uint32_t t0, first = sst_rc_gap(c, w->lo, w->hi), si = sst_rc_si(first), sec, left, lba, n = 0, k, old, hard[2];
    int32_t vs[SST_RUN], v;
    const sst_src_t *s = &sst_src[si % SST_NSRC];
    BYTE res = SDR_ERR, ok = 1, kept = 0;
    if (!first)
        return;
    sec = sst_rc_s(first);
    lba = sst_lba(s, sec, &left);
    sst_rc_standing(c, hard, w->cold);
    while (n < SST_RUN && n < left && first + n <= w->hi) {   /* contiguous: in the file, on the card, all missing */
        if (n && (sst_rc_gap(c, first + n, first + n) != first + n))
            break;
        v = sst_rc_victim(c, hard, w->due);
        if (v < 0)
            break;
        vs[n++] = v;
    }
    if (!n) {                                        /* (a sector outside the runs: the map is wrong; the range dropped,
                                                      * never asked again: the audio's next want or region replaces it) */
        if (!left) {
            sst_st.errors++;
            if (w->k < SST_RW)
                c->wlo[w->k] = 0;
            else
                c->rlo = 0;
        }
        return;
    }
    t0 = SD_NOW();
    SD_SESSION();
    if (sd_rd_open(lba, n) != RES_OK) {
        ok = 0;
    } else {
        for (k = 0; k < n; k++) {
            uint32_t j = (uint32_t)vs[k];
            old = c->tag[j];
            c->tag[j] = 0;                           /* (first: the audio no longer finds it) */
            SST_FENCE();
            if (old && sst_rc_pinned(c, old)) {      /* (a grain took it meanwhile: it stays) */
                c->tag[j] = old;
                sst_rcs.kept++;
                kept = 1;
                break;
            }
            if (!sst_rc_block(c, c->mem + j * 512u, &res))
                break;
            if (!c->on)
                break;
            SST_FENCE();
            c->use[j] = c->now;
            c->tag[j] = first + k;
            sst_rcs.blocks++;
            sst_st.blocks++;
            sst_rcs.fails = 0;
        }
        if (k < n && !kept) {
            if (res == SDR_BUSY && c->on) {          /* the card busy: the bus back to the screen for a while */
                sst_st.stalls++;
                sst_st.backoff = 1;
                sst_st.backoff_t0 = SD_NOW();
            } else if (res == SDR_READY && c->on) {  /* the CRC16 failed: read again (or the card is bad) */
                sst_st.crc++;
                ok = sd_crc_retry(&sst_rcs.fails);
            } else if (!c->on) {
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
    sst_rcs.runs++;
    sst_rcs.cold_runs += w->cold;
    if (ok) {
        sst_st.errs = 0;
        return;
    }
    sst_st.errors++;
    if (++sst_st.errs >= SST_DEAD_ERRS)
        sd_gone = 1;                                 /* (as a disk read's failure: every later call fails at once) */
}
