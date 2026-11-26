/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's Session page (jank_lpsess.h). */
#include "jank_lpsess.h"

#define ROWS_PER_BANK 2u
#define PADS_PER_ROW 8u

static int bit_of(const uint8_t *set, uint8_t p) { return (set[p >> 3] >> (p & 7u)) & 1u; }

void jlss_init(struct jlss *s)
{
    memset(s, 0, sizeof *s);
    s->playing = JLSS_UNKNOWN;
    s->cued = JDL_NO_PATTERN;
    s->sweep = JDL_BANKS;              /* nothing to read until the page opens */
}

void jlss_open(struct jlss *s)
{
    s->sweep = 0;
    s->asked = 0;
    s->cue_due = 1;
    s->retry_due = 0;
    s->chain_due = 1;
    s->chain_first = 0;
    s->hold_on = 0;                    /* a pad held when the page was left is forgotten */
    s->build_n = 0;
}

void jlss_forget(struct jlss *s)
{
    uint8_t half = s->half, sweep = s->sweep;
    jlss_init(s);
    s->half = half;                    /* the page shown stays */
    s->sweep = sweep;
}

/* the first bank not read yet, -1 if every one has been */
static int missing_bank(const struct jlss *s)
{
    for (unsigned b = 0; b < JDL_BANKS; b++)
        if (!(s->have & (1u << b)))
            return (int)b;
    return -1;
}

static int req(struct jlss_req *r, uint8_t kind, uint8_t bank)
{
    r->kind = kind;
    r->bank = bank;
    return 1;
}

static int bank_req(struct jlss *s, uint32_t now_ms, uint8_t bank, struct jlss_req *r)
{
    s->asked = 1;
    s->last_ms = now_ms;
    return req(r, JLSS_REQ_BANK, bank);
}

int jlss_due(struct jlss *s, uint32_t now_ms, unsigned ok, struct jlss_req *r)
{
    int reads = (ok & JLSS_OK_READS) != 0, live7 = (ok & JLSS_OK_LIVE7) != 0;
    if (!reads)
        s->sweep = JDL_BANKS;          /* a Digitone without PATTERNS: no bank to read */
    if (s->sweep < JDL_BANKS) {
        if (s->asked && (uint32_t)(now_ms - s->last_ms) < JLSS_OPEN_GAP_MS)
            return 0;
        return bank_req(s, now_ms, s->sweep++, r);
    }
    if (s->chain_due && (ok & JLSS_OK_CHAIN)) {
        s->chain_due = 0;
        return req(r, JLSS_REQ_CHAIN_READ, s->chain_first);
    }
    s->chain_due = 0;
    if (s->cue_due && (ok & JLSS_OK_CUE) && !live7) {
        s->cue_due = 0;
        return req(r, JLSS_REQ_CUE_READ, 0);
    }
    s->cue_due = 0;
    if (!reads)
        return 0;
    if (s->retry_due) {                /* one bank whose answer never came (a request lost on the way), at most one per poll */
        int b = missing_bank(s);
        s->retry_due = 0;
        if (b >= 0)
            return req(r, JLSS_REQ_BANK, (uint8_t)b);
    }
    if (!s->asked || (uint32_t)(now_ms - s->last_ms) < (live7 ? JLSS_POLL_SLOW_MS : JLSS_POLL_MS))   /* (not open: nothing) */
        return 0;
    s->cue_due = 1;
    s->retry_due = 1;
    return bank_req(s, now_ms, JDL_BANK_PLAYING, r);
}

void jlss_on_patterns(struct jlss *s, const struct jdl_patterns *p)
{
    if (p->bank >= JDL_BANKS || p->playing >= JDL_PATTERNS)
        return;
    s->mask[p->bank] = p->mask;
    s->have |= (uint8_t)(1u << p->bank);
    s->playing = p->playing;
    if (s->cued == s->playing)         /* the cued pattern plays now */
        s->cued = JDL_NO_PATTERN;
}

void jlss_on_next(struct jlss *s, uint8_t playing, uint8_t next, uint32_t now_ms)
{
    if (playing >= JDL_PATTERNS || (next >= JDL_PATTERNS && next != JDL_NO_PATTERN))
        return;
    s->playing = playing;
    if (next != JDL_NO_PATTERN && next != playing)
        s->cued = next;
    else if (s->cued == playing || (uint32_t)(now_ms - s->cue_ms) >= JLSS_CUE_HOLD_MS)
        s->cued = JDL_NO_PATTERN;      /* (a press's cue is kept a moment: this may be older than it) */
}

void jlss_on_chain(struct jlss *s, const struct jdl_chain *c)
{
    if (c->first == 0u)
        memset(s->chain, 0, sizeof s->chain);
    for (unsigned i = 0; i < sizeof s->chain; i++)
        s->chain[i] |= c->member[i];
    s->chain_count = c->count;
    if (!c->count)
        memset(s->chain, 0, sizeof s->chain);
    if (c->k && (unsigned)c->first + c->k < c->count && (unsigned)c->first + c->k < JDL_CHAIN_MAX) {   /* more than one answer holds: the rest */
        s->chain_due = 1;
        s->chain_first = (uint8_t)(c->first + c->k);
    }
}

void jlss_on_chain_count(struct jlss *s, uint16_t count)
{
    if (count == s->chain_count)
        return;
    s->chain_count = count;            /* changed on the Digitone: read it again */
    s->chain_due = 1;
    s->chain_first = 0;
}

uint8_t jlss_pattern_of_pad(const struct jlss *s, uint8_t x, uint8_t y)
{
    unsigned r = 7u - (y & 7u), bank = s->half * JLSS_BANKS_SHOWN + r / ROWS_PER_BANK;
    return (uint8_t)(bank * JDL_BANK_PATTERNS + (r % ROWS_PER_BANK) * PADS_PER_ROW + (x & 7u));
}

int jlss_press(struct jlss *s, uint8_t pad, uint8_t pattern, uint32_t now_ms)
{
    if (!s->hold_on) {
        s->hold_on = 1;
        s->hold_pad = pad;
        s->hold_ms = now_ms;
        s->build[0] = pattern;
        s->build_n = 1;
        return JLSS_PRESS_CUE;
    }
    if (s->build_n >= JDL_CHAIN_MAX)
        return JLSS_PRESS_FULL;
    s->build[s->build_n++] = pattern;
    return JLSS_PRESS_ADDED;
}

int jlss_holds(const struct jlss *s, uint8_t pad) { return s->hold_on && s->hold_pad == pad; }

int jlss_release(struct jlss *s, uint8_t pad, uint32_t now_ms)
{
    if (!jlss_holds(s, pad))
        return JLSS_RELEASE_NONE;
    s->hold_on = 0;
    if (s->build_n >= JDL_CHAIN_MIN)
        return JLSS_RELEASE_SET;
    s->build_n = 0;
    if (s->build[0] == s->playing && (uint32_t)(now_ms - s->hold_ms) >= JLSS_CLEAR_HOLD_MS)
        return JLSS_RELEASE_CLEAR;
    return JLSS_RELEASE_NONE;
}

const uint8_t *jlss_chain_list(const struct jlss *s, size_t *n)
{
    *n = s->build_n;
    return s->build;
}

void jlss_chain_sent(struct jlss *s)
{
    memset(s->chain, 0, sizeof s->chain);
    for (unsigned i = 0; i < s->build_n; i++)
        s->chain[s->build[i] >> 3] |= (uint8_t)(1u << (s->build[i] & 7u));
    s->chain_due = 1;                  /* read back what the Digitone made of it */
    s->chain_first = 0;
}

void jlss_chain_cleared(struct jlss *s)
{
    memset(s->chain, 0, sizeof s->chain);
    s->build_n = 0;
    s->chain_due = 1;
    s->chain_first = 0;
}

void jlss_cue(struct jlss *s, uint8_t pattern, uint32_t now_ms)
{
    if (pattern < JDL_PATTERNS) {
        s->cued = pattern;
        s->cue_ms = now_ms;
    }
}

int jlss_half_step(struct jlss *s, int dir)
{
    if (dir < 0 && s->half > 0u) {
        s->half--;
        return 1;
    }
    if (dir > 0 && s->half + 1u < JLSS_HALVES) {
        s->half++;
        return 1;
    }
    return 0;
}

void jlss_name(uint8_t pattern, char out[4])
{
    unsigned k = pattern % JDL_BANK_PATTERNS + 1u;
    out[0] = (char)('A' + (pattern / JDL_BANK_PATTERNS) % JDL_BANKS);
    out[1] = (char)('0' + k / 10u);
    out[2] = (char)('0' + k % 10u);
    out[3] = 0;
}

/* a pattern of the chain being built (its lights come before the Digitone hears of it) */
static int in_build(const struct jlss *s, uint8_t p)
{
    for (unsigned i = 0; i < s->build_n && s->build_n >= JDL_CHAIN_MIN; i++)
        if (s->build[i] == p)
            return 1;
    return 0;
}

void jlss_frame(const struct jlss *s, struct jlx_frame *f)
{
    for (uint8_t y = 0; y < 8u; y++)
        for (uint8_t x = 0; x < 8u; x++) {
            struct jlx_led *l = &f->led[JLX_PAD0 + y * 8u + x];
            uint8_t p = jlss_pattern_of_pad(s, x, y), bank = p / JDL_BANK_PATTERNS;
            memset(l, 0, sizeof *l);
            if (p == s->playing) {
                l->color = JLSS_COLOR_PLAYING;
                l->mode = JLX_PULSE;
            } else if (p == s->cued) {
                l->color = JLSS_COLOR_CUED;
                l->alt = 0;                /* flashes with dark */
                l->mode = JLX_FLASH;
            } else if (bit_of(s->chain, p) || in_build(s, p)) {
                l->color = JLSS_COLOR_CHAIN;
            } else if ((s->have & (1u << bank)) && (s->mask[bank] & (1u << (p % JDL_BANK_PATTERNS)))) {
                l->color = JLSS_COLOR_FILLED;
            }
        }
}
