/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's step sequencer modes (jank_lpstep.h). */
#include "jank_lpstep.h"
#include "jank_lpstr.h"

#define VEL_OWN 255u                  /* STEP: the track's own velocity and length */
#define LEN_OWN 255u
#define QUAD 4u                        /* the quadrants are 4 x 4 */
#define STEP_ROWS_FROM 4u              /* pad rows 4..7 are the steps */
#define PAGE_ROW 3u                    /* the right quadrant's top row holds the page pads */

void jlst_init(struct jlst *s, enum jlst_kind kind)
{
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)kind;
    s->note = -1;
    for (unsigned i = 0; i < JLST_KEYS; i++)
        s->key_note[i] = -1;
}

/* page p holds a page of `track` */
static int is_of(const struct jlst *s, unsigned p, uint8_t track) { return s->have[p] && s->pg[p].track == track; }

/* (jank_lpact.h) the one copy, here: every file that makes acts links this one */
size_t jlapp_act(struct jlapp_act *o, uint8_t kind, uint8_t track, uint8_t note)
{
    memset(o, 0, sizeof *o);
    o->kind = kind;
    o->track = track;
    o->note = note;
    return 1;
}

size_t jlapp_tracks_off(uint16_t *held, struct jlapp_act *out, size_t max)
{
    size_t n = 0;
    for (uint8_t t = 0; t < 16u && n < max; t++)
        if (*held & (1u << t)) {
            *held &= (uint16_t)~(1u << t);
            n += jlapp_act(&out[n], JLAPP_ACT_TRACK_OFF, t, 0);
        }
    return n;
}

int jlapp_played(const struct jlapp_recent *r, unsigned n, uint32_t now_ms, uint8_t track, int note)
{
    for (; n; n--, r++)
        if (r->used && r->track == track && (note < 0 || r->note == note) && (uint32_t)(now_ms - r->at_ms) < JLAPP_PUSH_LIGHT_MS)
            return 1;
    return 0;
}

/* ---- the steps ---- */

static size_t step_press(struct jlst *s, const struct jlst_ctx *c, unsigned k, struct jlapp_act *out, size_t max)
{
    unsigned p = k / 16u, idx = k % 16u, abs_page = s->first_page + p;
    uint8_t track = c->n->track, op, note = 0;
    struct jdl_step *st;
    if (max < 1 || !is_of(s, p, track) || abs_page >= s->pages_known)
        return 0;                      /* the Digitone has not told this page of this track yet (the pads may still show the last track's): nothing to toggle */
    st = &s->pg[p].step[idx];
    if (c->deleting) {
        if (!st->trig)
            return 0;                  /* nothing there to clear: and a Delete press never sets a trig */
        op = JDL_STEP_CLEAR;
    } else if (s->kind == JLST_MELODIC && s->note >= 0) {
        if (st->trig && st->note == s->note) {
            op = JDL_STEP_CLEAR;
        } else {
            op = JDL_STEP_NOTE;
            note = (uint8_t)s->note;
        }
    } else {
        op = st->trig ? JDL_STEP_CLEAR : JDL_STEP_TRIG;
    }
    jlapp_act(out, JLAPP_ACT_STEP, track, note);
    out->vel = VEL_OWN;
    out->step = (uint8_t)(abs_page * 16u + idx);
    out->op = op;
    out->len = LEN_OWN;
    st->trig = op != JDL_STEP_CLEAR;   /* shown at once; the next read confirms it */
    if (op == JDL_STEP_NOTE)
        st->note = note;
    s->edit_on = 1;
    s->edit_ms = c->now_ms;
    return 1;
}

/* ---- the left quadrant: tracks (drums) or a keyboard (melodic) ---- */

static int auditions(const struct jlst_ctx *c) { return c->can_play && !c->transport_playing; }

static size_t drum_pad(struct jlst *s, const struct jlst_ctx *c, uint8_t x, uint8_t y, uint8_t value, struct jlapp_act *out, size_t max)
{
    uint8_t t = (uint8_t)((QUAD - 1u - y) * QUAD + x);     /* reading order: track 1 top left of the quadrant; past the unit's last: nothing */
    uint16_t bitv = (uint16_t)(1u << t);
    size_t n = 0, need;
    int play = auditions(c);
    if (JLN_NO_TRACK(t))
        return 0;
    if (!value) {
        if (!(s->track_held & bitv) || max < 1)
            return 0;
        s->track_held &= (uint16_t)~bitv;
        return jlapp_act(out, JLAPP_ACT_TRACK_OFF, t, 0);
    }
    if (s->track_held & bitv)
        return 0;
    need = (size_t)(t != c->n->track) + (size_t)play;
    if (need > max)
        return 0;
    if (t != c->n->track) {
        c->n->track = t;
        n = jlapp_act(out, JLAPP_ACT_SELECT, t, 0);
    }
    if (play) {
        s->track_held |= bitv;
        n += jlapp_act(&out[n], JLAPP_ACT_TRACK_ON, t, 0);
    }
    return n;
}

static size_t key_pad(struct jlst *s, const struct jlst_ctx *c, uint8_t x, uint8_t y, uint8_t value, struct jlapp_act *out, size_t max)
{
    unsigned i = y * QUAD + x;
    struct jln_pad p;
    if (!value) {
        if (s->key_note[i] < 0 || max < 1)
            return 0;
        jlapp_act(out, JLAPP_ACT_NOTE_OFF, s->key_track[i], (uint8_t)s->key_note[i]);
        s->key_note[i] = -1;
        return 1;
    }
    if (s->key_note[i] >= 0)
        return 0;
    jln_key(c->n, c->scale, i, &p);    /* 16 different notes: no pitch twice, unlike Note mode's overlapping layout */
    if (p.note < 0)
        return 0;
    s->note = p.note;                  /* a step press places this note from now on */
    if (!auditions(c) || max < 1)
        return 0;
    s->key_note[i] = p.note;
    s->key_track[i] = c->n->track;
    jlapp_act(out, JLAPP_ACT_NOTE_ON, c->n->track, (uint8_t)p.note);
    out->vel = value;
    return 1;
}

size_t jlst_pad(struct jlst *s, const struct jlst_ctx *c, uint8_t x, uint8_t y, uint8_t value, struct jlapp_act *out, size_t max)
{
    if (x > 7u || y > 7u)
        return 0;
    if (y >= STEP_ROWS_FROM)
        return value ? step_press(s, c, (7u - y) * 8u + x, out, max) : 0;
    if (x < QUAD)
        return s->kind == JLST_DRUM ? drum_pad(s, c, x, y, value, out, max) : key_pad(s, c, x, y, value, out, max);
    if (y == PAGE_ROW && value) {      /* the page pads */
        unsigned page = x - QUAD;
        if (s->pages_known && page >= s->pages_known)
            return 0;
        if (page != s->first_page) {
            s->first_page = (uint8_t)page;
            s->have[0] = s->have[1] = 0;
            s->edit_on = 0;
        }
    }
    return 0;
}

/* ---- the Digitone's answers ---- */

size_t jlst_wanted(const struct jlst *s, uint8_t pages[2])
{
    size_t n = 0;
    if (!s->pages_known) {
        pages[0] = 0;
        return 1;
    }
    if (s->first_page < s->pages_known)
        pages[n++] = s->first_page;
    if (s->first_page + 1u < s->pages_known)
        pages[n++] = (uint8_t)(s->first_page + 1u);
    return n;
}

void jlst_on_page(struct jlst *s, uint8_t track, const struct jdl_page *pg, uint32_t now_ms)
{
    if (pg->track != track || pg->page >= JLST_PAGES || pg->pages == 0 || pg->pages > JLST_PAGES)
        return;
    s->pages_known = pg->pages;
    if (s->first_page >= pg->pages) {  /* a shorter pattern: the last page */
        s->first_page = (uint8_t)(pg->pages - 1u);
        s->have[0] = s->have[1] = 0;
    }
    for (unsigned i = 0; i < 2u; i++) {
        if (pg->page != s->first_page + i)
            continue;
        if (is_of(s, i, track) && s->edit_on && (uint32_t)(now_ms - s->edit_ms) < JLST_EDIT_HOLD_MS)
            continue;                  /* what a press just changed (on this very track): the next read will agree */
        s->pg[i] = *pg;
        s->have[i] = 1;
        s->pages_ms = now_ms;
        s->edit_on = 0;                /* a page that was read replaces whatever a press showed */
    }
}

int jlst_urgent(const struct jlst *s, uint8_t track)
{
    uint8_t pages[2];
    size_t n = jlst_wanted(s, pages);
    for (size_t i = 0; i < n; i++) {
        unsigned idx = (unsigned)pages[i] - s->first_page;   /* before the first read page 0 is wanted whatever the window says: no slot then */
        if (idx > 1u || !is_of(s, idx, track))
            return 1;
    }
    return 0;
}

/* the pattern's length in steps, from what the Digitone said for the track (0: not known) */
static unsigned pattern_len(const struct jlst *s)
{
    for (unsigned i = 0; i < 2u; i++)
        if (s->have[i] && s->pg[i].length)
            return s->pg[i].length;
    return s->pages_known * 16u;
}

/* the clock the playhead goes by, in sixteenths, and which one it is (0: none): the Digitone's own while its ticks come, else Jank's session beat */
static unsigned base16(const struct jlst *s, uint32_t now_ms, uint32_t *base)
{
    if (!JLAPP_CLIPS && s->rt_run && (uint32_t)(now_ms - s->rt_last_ms) < JLST_RT_ALIVE_MS) {   /* (the Digitone's clock: jank's; another unit's playhead runs on jlapp_set_beat) */
        *base = s->rt_ticks / JLST_TICKS_PER_STEP;
        return 2;
    }
    if (s->run) {
        *base = s->tick16;
        return 1;
    }
    return 0;
}

void jlst_on_push(struct jlst *s, uint8_t track, uint8_t step, uint32_t now_ms)
{
    unsigned len = pattern_len(s);
    uint32_t base = 0;
    unsigned src = base16(s, now_ms, &base);
    s->head_on = 1;
    s->head_track = track;
    s->head_step = step;
    s->head_ms = now_ms;
    if (src && len && step < len) {    /* a pushed note's step is exact: the clock's phase is learned from it */
        s->offset = (uint8_t)((step + len - base % len) % len);
        s->offset_src = (uint8_t)src;
    }
}

#if !JLAPP_CLIPS
void jlst_on_rt(struct jlst *s, uint8_t byte, uint32_t now_ms)
{
    if (byte == 0xFAu) {
        s->rt_run = 1;
        s->rt_ticks = 0;
        s->rt_starts++;
    } else if (byte == 0xFBu) {
        s->rt_run = 1;
    } else if (byte == 0xFCu) {
        s->rt_run = 0;
        s->rt_stops++;
    } else if (byte == 0xF8u) {
        s->rt_clocks++;
        s->rt_last_ms = now_ms;
        if (s->rt_run)
            s->rt_ticks++;
    }
}
#endif

void jlst_set_clock(struct jlst *s, int running, uint32_t tick16)
{
    s->run = running != 0;
    s->tick16 = tick16;
}

size_t jlst_release(struct jlst *s, struct jlapp_act *out, size_t max)
{
    size_t n = 0;
    for (unsigned i = 0; i < JLST_KEYS && n < max; i++)
        if (s->key_note[i] >= 0) {
            n += jlapp_act(&out[n], JLAPP_ACT_NOTE_OFF, s->key_track[i], (uint8_t)s->key_note[i]);
            s->key_note[i] = -1;
        }
    return n + jlapp_tracks_off(&s->track_held, out + n, max - n);
}

/* ---- lights ---- */

static uint8_t step_colour(const struct jlst *s, const struct jlst_ctx *c, unsigned k)
{
    unsigned p = k / 16u, idx = k % 16u, abs_page = s->first_page + p, abs_step = abs_page * 16u + idx;
    const struct jdl_step *st;
    if (!s->have[p] || abs_page >= s->pages_known)
        return 0;
    if (s->pg[p].track != c->n->track && (uint32_t)(c->now_ms - s->pages_ms) >= JLST_STALE_MS)
        return 0;                      /* the last track's steps have waited long enough for the new track's pages (no live control?): dark, not wrong */
    if (s->pg[p].length && abs_step >= s->pg[p].length)
        return 0;                      /* past the pattern's length */
    st = &s->pg[p].step[idx];
    {
        uint32_t base = 0;
        unsigned src = base16(s, c->now_ms, &base), len = pattern_len(s);
        if (src && len) {              /* the playhead: where the clock says the sequencer is, on every step, whether it plays a note or not */
            unsigned off = s->offset_src == src ? s->offset : 0u;
            if (abs_step == (base + off) % len)
                return st->trig ? JLST_COLOR_PLAYHEAD_NOTE : JLST_COLOR_PLAYHEAD;
        } else if (s->head_on && s->head_track == c->n->track && s->head_step == abs_step && (uint32_t)(c->now_ms - s->head_ms) < JLST_HEAD_MS) {
            return JLST_COLOR_HEAD;    /* no clock to go by: only the step that just played a note */
        }
    }
    return st->trig ? JLST_COLOR_ON : JLST_COLOR_OFF;
}

/* a note of this track (and this pitch, unless note < 0) that the Digitone played in the last JLST_HEAD_MS */
static int played_lately(const struct jlst_ctx *c, uint8_t track, int note) { return jlapp_played(c->recent, c->recent_n, c->now_ms, track, note); }

static uint8_t quadrant_colour(const struct jlst *s, const struct jlst_ctx *c, uint8_t x, uint8_t y)
{
    unsigned i = y * QUAD + x;
    if (s->kind == JLST_DRUM) {
        uint8_t t = (uint8_t)((QUAD - 1u - y) * QUAD + x);
        if (JLN_NO_TRACK(t))
            return 0;
        if ((s->track_held & (1u << t)) || played_lately(c, t, -1))   /* a pad that is down, or a track that plays: RED, as Launchpad 95's playing note */
            return JLST_COLOR_TRACK_PLAYING;
        return t == c->n->track ? JLST_COLOR_TRACK_CURRENT : JLST_COLOR_TRACK_IDLE;
    } else {
        struct jln_pad p;
        jln_key(c->n, c->scale, i, &p);
        if (p.note < 0)
            return 0;
        if (s->key_note[i] >= 0 || played_lately(c, c->n->track, p.note))   /* a key that is down, or a pitch the track plays (every key of that pitch) */
            return JLST_COLOR_TRACK_PLAYING;
        return p.note == s->note ? JLST_COLOR_HEAD : jln_color(&p, 0);
    }
}

void jlst_frame(const struct jlst *s, const struct jlst_ctx *c, struct jlx_frame *f)
{
    for (uint8_t y = 0; y < 8u; y++)
        for (uint8_t x = 0; x < 8u; x++) {
            uint8_t col = 0;
            if (y >= STEP_ROWS_FROM)
                col = step_colour(s, c, (7u - y) * 8u + x);
            else if (x < QUAD)
                col = quadrant_colour(s, c, x, y);
            else if (y == PAGE_ROW && (!s->pages_known || (x - QUAD) < s->pages_known))
                col = (x - QUAD) == s->first_page || (x - QUAD) == s->first_page + 1u ? JLST_COLOR_PAGE_ON : JLST_COLOR_PAGE_OFF;
            f->led[JLX_PAD0 + y * 8u + x].color = col;
        }
}
