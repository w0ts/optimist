/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors */
/* GRAIN on SD card mode's sets (eng_grain.c's header: the card's zones, the plan, the pins, waiting and fading):
 * included by eng_grain.c with GR_CARD (FELUCCA_SD_STREAM), after its geometry (gr_geom) and its window (gr_win). */
/* ---- SD card mode's sets: the grains read the card's zones through the part's cache (sd_stream_rc.h) ---- */
AINL uint32_t gr_card_cap(const sst_rc_t *c, const sst_src_t *s)   /* a grain's most source samples: the cache less 4 */
{
    return ((c->nk > 8u ? c->nk : 8u) - 4u) * (512u >> s->fsh) - 8u;
}
/* the frames a grain at pos reads (forward up, reverse down; the margin: the fraction's carry, the interpolator) */
AINL void gr_card_range(const smp_zone_t *z, uint32_t pos, uint32_t span, uint32_t rev, uint32_t *f0, uint32_t *f1)
{
    if (rev) {
        *f0 = pos > span + 4u ? pos - span - 4u : 0u;
        *f1 = pos + 1u;
    } else {
        *f0 = pos;
        *f1 = pos + span + 4u < z->n ? pos + span + 4u : z->n - 1u;
    }
}

/* a forward grain's pin: GR_PIN_MS of its reading ahead of the reader (and a sector), the rest WANTED by the time it
 * reaches it; a reverse one pins its whole range (reversed grains are RAND's few: up to half at RAND 127) */
#define GR_PIN_SMP (GR_PIN_MS * 441u / 10u)
AINL uint32_t gr_card_ahead(const sst_src_t *s, uint32_t step)   /* (sectors) */
{
    return (((GR_PIN_SMP * (step >> 4)) >> 12) >> (9u - s->fsh)) + 2u;   /* (frames / a sector's) */
}

/* the reader's place in its sectors: the pin slid up behind and ahead of it, the sectors newly pinned looked at
 * (after the pin: a slot the main loop was taking is either seen gone or kept), the rest wanted (slot k: the grain's)
 * by when the reader reaches it; reverse: its first missing sector wanted now */
static void gr_card_slide(gr_part_t *P, sst_rc_t *c, gr_grain_t *g)
{
    const sst_src_t *s = &sst_src[g->si % SST_NSRC];
    uint32_t k = (uint32_t)(g - P->g), s0, nh, f, sh;
    int32_t d;
    if (g->rev) {
        if (g->cur <= g->end && !sst_rc_scan(c, s, g->si, &g->cur, g->end))
            sst_rc_want(c, k, s, g->si, (g->cur << 9) > s->dofs ? ((g->cur << 9) - s->dofs) >> s->fsh : 0u,
                        g->pos + 1u, c->now);
        else
            sst_rc_unwant(c, k);
        return;
    }
    s0 = g->pos < g->z->n ? sst_rc_sec(s, g->pos) : g->end;
    nh = s0 + gr_card_ahead(s, g->step);
    nh = nh < g->end ? nh : g->end;
    c->plo[k] = sst_rc_tag(g->si, s0);               /* (only ever up: what it read is free) */
    if (nh > g->ph) {
        SST_FENCE();
        c->phi[k] = sst_rc_tag(g->si, nh);
        g->ph = nh;
        SST_FENCE();
    }
    if (g->cur < s0)
        g->cur = s0;
    (void)sst_rc_scan(c, s, g->si, &g->cur, g->ph);   /* (the pinned ones only: past the pin, only the want holds them) */
    if (g->cur > g->end) {
        sst_rc_unwant(c, k);
        return;
    }
    f = (g->cur << 9) > s->dofs ? ((g->cur << 9) - s->dofs) >> s->fsh : 0u;   /* (its first frame) */
    d = (int32_t)(f - g->pos);
    for (sh = 9u; sh < 18u && g->step >> sh; sh++)  /* (output samples until the reader is there: frames / step, the */
        ;                                            /* step taken as the power of two above it: never later, no divide) */
    d = d <= 0 ? 0 : (int32_t)(((uint32_t)d << 16) >> sh);
    c->wlo[k] = 0;
    SST_FENCE();
    c->whi[k] = sst_rc_tag(g->si, g->end);
    c->wdue[k] = c->now + (uint32_t)d;
    SST_FENCE();
    c->wlo[k] = sst_rc_tag(g->si, g->cur);
}

/* a waiting grain: its pinned sectors all there now? Then its first two samples (as the flash's: a = the sample at
 * pos); 1 it sounds */
static int gr_card_try(sst_rc_t *c, gr_grain_t *g)
{
    const sst_src_t *s = &sst_src[g->si % SST_NSRC];
    uint32_t pos = g->pos, top = g->rev ? g->end : g->ph;
    int32_t a, b;
    if (!s->ready || !sst_rc_scan(c, s, g->si, &g->cur, top))
        return 0;
    if (!sst_rc_frame(c, s, g->si, &g->w, pos, &a) || !sst_rc_frame(c, s, g->si, &g->w, pos + 1u, &b))
        return 0;                                   /* (never: pinned and there) */
    g->a = a;
    g->b = b;
    if (!g->rev)
        g->pos = pos + 2u;
    g->wait = 0;
    return 1;
}

/* the grain set up (gr_spawn): its sectors pinned (a forward one's first GR_PIN_MS), then looked at; not all there:
 * it waits (silent), wanted now */
static void gr_card_go(gr_part_t *P, uint32_t part, gr_grain_t *g, uint32_t pos, uint32_t span)
{
    sst_rc_t *c = &sst_rc[part % SST_NPART];
    const sst_src_t *s = &sst_src[g->si % SST_NSRC];
    uint32_t f0, f1, k = (uint32_t)(g - P->g);
    gr_card_range(g->z, pos, span, g->rev, &f0, &f1);
    g->pos = pos;
    g->dry = 0;
    g->w.wn = 0;
    g->wait = 1;
    g->cur = sst_rc_sec(s, f0);
    g->end = sst_rc_esec(s, f1);
    g->ph = g->cur + gr_card_ahead(s, g->step);
    g->ph = g->rev || g->ph > g->end ? g->end : g->ph;
    c->plo[k] = 0;
    SST_FENCE();
    c->phi[k] = sst_rc_tag(g->si, g->ph);
    SST_FENCE();
    c->plo[k] = sst_rc_tag(g->si, g->cur);
    SST_FENCE();
    if (gr_card_try(c, g)) {
        c->hit = c->hit + 1u;
        gr_card_slide(P, c, g);
    } else {
        sst_rc_want(c, k, s, g->si, f0, f1, c->now);   /* (now) */
    }
}

AINL void gr_card_end(gr_part_t *P, uint32_t part, gr_grain_t *g)   /* a grain over: its pin and want off */
{
    sst_rc_unpin(&sst_rc[part % SST_NPART], (uint32_t)(g - P->g));
    sst_rc_unwant(&sst_rc[part % SST_NPART], (uint32_t)(g - P->g));
    g->owner = 0;
}

/* a card grain's block: waiting (its sectors looked at again: there, it starts now; GR_LATE_MS on, given up), or
 * sounding through the reader (a sector gone: the sample held, the grain faded out over this block); 1 it goes on */
static int gr_card_run(gr_part_t *P, uint32_t part, gr_grain_t *g, int32_t *acc, uint32_t n)
{
    sst_rc_t *c = &sst_rc[part % SST_NPART];
    const sst_src_t *s = &sst_src[g->si % SST_NSRC];
    uint32_t i, m, step = g->step, pos, nz = g->z->n, si = g->si;
    int32_t a, b, frac = g->frac, w0, w1, wq, dq, x;
    if (g->wait) {
        if (!gr_card_try(c, g)) {
            if (!sst_rc_live(c) || !s->ready || ++g->wait > GR_LATE_BLK) {
                if (sst_rc_live(c) && s->ready)
                    c->miss = c->miss + 1u;          /* (given up; a gate's are not the card's) */
                gr_card_end(P, part, g);
                P->replan = 1;
                return 0;
            }
            return 1;                                /* (silent: its window not begun) */
        }
        c->late = c->late + 1u;
        P->replan = 1;
    }
    if (g->dry == 1u) {                              /* a sector gone: it fades out over GR_FADE_BLK blocks (counted once) */
        g->dry = (uint8_t)(1u + GR_FADE_BLK);
        if (sst_rc_live(c))
            c->miss = c->miss + 1u;
    }
    m = n < g->left ? n : g->left;
    pos = g->pos;
    a = g->a;
    b = g->b;
    w0 = mulq15(gr_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gr_win(g->wph), g->gain) : 0;
    if (g->dry > 1u) {                               /* fading: this block from (dry - 1) / GR_FADE_BLK to one less */
        uint32_t f = g->dry - 1u;
        w0 = w0 * (int32_t)f / (int32_t)GR_FADE_BLK;
        w1 = w1 * (int32_t)(f - 1u) / (int32_t)GR_FADE_BLK;
        if (--g->dry == 1u)
            g->left = 0;                             /* (its last block) */
    }
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    if (!g->rev) {
        for (i = 0; i < m; i++) {
            int32_t y = lerp16(a, b, (uint32_t)frac);   /* (dsp.c) */
            acc[i] += (y * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                if (pos < nz && !g->dry) {
                    if (sst_rc_frame(c, s, si, &g->w, pos, &x))
                        b = x, pos++;
                    else
                        g->dry = 1;                  /* (the sample held: b = a) */
                }
            }
        }
    } else {
        for (i = 0; i < m; i++) {
            int32_t y = lerp16(a, b, (uint32_t)frac);   /* (dsp.c) */
            acc[i] += (y * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pos && !g->dry) {                /* (fitted: always; at 0, a stays the first sample) */
                    if (sst_rc_frame(c, s, si, &g->w, pos - 1u, &x))
                        a = x, pos--;
                    else
                        g->dry = 1;
                }
            }
        }
    }
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left) {
        gr_card_end(P, part, g);
        return 0;
    }
    if (g->rev && pos)                               /* (a reverse pin shrinks from the top: the next read is pos - 1) */
        c->phi[(uint32_t)(g - P->g)] = sst_rc_tag(si, sst_rc_esec(s, pos - 1u));
    gr_card_slide(P, c, g);
    return 1;
}

/* the voice's modulation the plan uses (its last render's; a fresh note: none yet, its pitch) */
AINL void gr_card_mod(voice_t *v, const vmod_t *m)
{
    v->s[3] = m->shape;
    v->s[4] = m->pitch16;
    v->s[5] = m->fine;
}

/* THE PLAN: the part's next grains within GR_AHEAD_MS, as the renders will start them: each voice's countdown run
 * down block by block (its interval, the jitter: the generator's next number), the voice's grains at the time (a grain
 * only while it holds fewer than its share), the two numbers of each spawn, its geometry (gr_geom, the voice's last
 * modulation): its sectors WANTED by the block it starts in. The waiting grains' first (due now). The region: where
 * the soonest voice's grains can start at all (POS +- SPRD / 2, the grain's span on either side). Made again when the
 * generator moved (a grain or an interval drawn), a note came, a grain started late or was given up, or half the
 * horizon went by. Cheap: an event at a time (a voice's next countdown end), at most SST_RW */
static void gr_plan(gr_part_t *P, const track_t *t, uint32_t part, sst_rc_t *c)
{
    const int16_t *p = t->p;
    uint32_t nw = GR_NG, i, vi, nact = 0, cap, iv, bb[GR_POLY], ev, b, now = c->now, f0, f1, mine, live, load, nsim = 0;
    int32_t cd[GR_POLY], rng = P->rng, best;
    struct { uint32_t b, len, ld; uint8_t vi; } sim[GR_NG];
    uint32_t reg = 0, rlo = 0, rhi = 0, rsi = 0;
    for (i = 0; i < GR_POLY; i++)
        nact += t->v[i].active;
    cap = nact > 1u ? (GR_NG / nact > 2u ? GR_NG / nact : 2u) : GR_NG;
    iv = (44100u * pow2_q16(-p[P_E3] * 10)) >> 16;
    for (vi = 0; vi < GR_POLY; vi++) {
        const voice_t *v = &t->v[vi];
        bb[vi] = 0;
        cd[vi] = v->active && v->s[0] >= 0 && v->s[0] < P->nz ? v->s[1] : INT32_MIN;   /* (not rendered: no events) */
    }
    while (nw < SST_RW) {
        for (vi = 0, ev = 0xFFFFFFFFu, best = -1; vi < GR_POLY; vi++) {   /* the next countdown's end (by voice order) */
            if (cd[vi] == INT32_MIN)
                continue;
            b = bb[vi] + (cd[vi] <= (int32_t)CTL ? 0u : ((uint32_t)cd[vi] + CTL - 1u) / CTL - 1u);
            if (b < ev)
                ev = b, best = (int32_t)vi;
        }
        if (best < 0 || ev > GR_AHEAD_BLK)
            break;
        vi = (uint32_t)best;
        {
            const voice_t *v = &t->v[vi];
            uint32_t r = noise32(&rng), zl = (uint32_t)v->s[0], r1, r2, si;
            int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * p[P_E5]) / 254, x;
            const smp_zone_t *z = gr_zone(part, P->src - 1u, zl);
            gr_geo_t o;
            x = cd[vi] - (int32_t)((ev - bb[vi] + 1u) * CTL) + (int32_t)iv + mulq15((int32_t)iv, j);
            cd[vi] = x < 0 ? 0 : x;
            bb[vi] = ev + 1u;
            for (i = 0, mine = 0, live = 0, load = 0; i < GR_NG; i++) {   /* the grains after this block's run */
                const gr_grain_t *g = &P->g[i];
                if (g->owner && (g->wait || g->left > (ev + 1u) * CTL)) {
                    live++;
                    load += g->step << g->rev;
                    mine += g->owner == vi + 1u;
                }
            }
            for (i = 0; i < nsim; i++)
                if (sim[i].len > (ev - sim[i].b) * CTL) {
                    live++;
                    load += sim[i].ld;
                    mine += sim[i].vi == vi;
                }
            if (mine >= cap)
                continue;
            r1 = noise32(&rng);
            r2 = noise32(&rng);
            si = smp_cz_src(part, zl);
            if (live >= GR_NG || !gr_geom(P, p, z, zl, v->s[3], v->s[4], v->s[5], r1, r2, gr_card_cap(c, &sst_src[si]), &o) ||
                load + (o.step << o.rev) > GR_LOAD_MAX)
                continue;
            if (nsim < GR_NG) {
                sim[nsim].b = ev;
                sim[nsim].len = o.len;
                sim[nsim].ld = o.step << o.rev;
                sim[nsim++].vi = (uint8_t)vi;
            }
            gr_card_range(z, (uint32_t)o.pos, o.span, o.rev, &f0, &f1);
            sst_rc_want(c, nw++, &sst_src[si], si, f0, f1, now + ev * CTL);
            if (!reg) {                              /* the region: this voice's */
                uint32_t n = z->n, cc, sp, lo, hi, h;
                int32_t posq = clamp((p[P_E1] << 8) + v->s[3] - (64 << 8), 0, 127 << 8);
                cc = gr_scale(n - 1u, (uint32_t)posq * 65535u / (127u << 8));
                sp = gr_scale(n, (uint32_t)p[P_E5] * 516u) >> 1;
                h = o.span + 4u;
                lo = cc > sp + (p[P_E6] ? h : 0u) ? cc - sp - (p[P_E6] ? h : 0u) : 0u;
                hi = cc + sp + h < n ? cc + sp + h : n - 1u;
                reg = 1, rlo = lo, rhi = hi, rsi = si;
            }
        }
    }
    for (i = nw; i < SST_RW; i++)
        sst_rc_unwant(c, i);
    if (reg)
        sst_rc_region(c, &sst_src[rsi], rsi, rlo, rhi);
    P->prng = P->rng;
    P->pnow = now;
    P->replan = 0;
}

/* the block's card work: the cache's clock, its placement followed (a new one: its pins and wants again; gone: the
 * grains fade, the waiting ones go), the plan when due */
static void gr_card_block(gr_part_t *P, const track_t *t, uint32_t part)
{
    sst_rc_t *c = &sst_rc[part % SST_NPART];
    uint32_t i;
    c->now = c->now + CTL;
    if (!sst_rc_live(c) || P->rcg != c->gen) {
        for (i = 0; i < GR_NG; i++)
            if (P->g[i].owner && !P->g[i].dry)
                P->g[i].dry = 1;                     /* (sounding: fades; waiting: given up) */
        if (!sst_rc_live(c))
            return;
        sst_rc_reset(c);
        P->rcg = c->gen;
        P->replan = 1;
    }
    if (P->replan || P->prng != P->rng || c->now - P->pnow >= GR_AHEAD_BLK * CTL / 2u)
        gr_plan(P, t, part, c);
}
