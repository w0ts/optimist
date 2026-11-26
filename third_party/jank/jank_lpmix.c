/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad's mixer screen (jank_lpmix.h). */
#include "jank_lpmix.h"
#include "jank_lpstr.h"

#define PAD_ROWS 8u
#define PAN_CENTRE_LOW 3u              /* the centre pair of the pan's 8 positions */
#define PAN_CENTRE_HIGH 4u
#define PAN_CENTRE_A 63u               /* Launchpad 95's _send_value_pan: 63 and 64 are the centre */
#define PAN_CENTRE_B 64u
#define OVERVIEW_LAST_VALUE_ROW 4u     /* the overview's rows from the top: 0 volume .. 4 chorus (the views), 5 mute, 6 solo, 7 nothing */
#define OVERVIEW_MUTE_ROW 5u
#define OVERVIEW_SOLO_ROW 6u
#define MUTE_SOLO_ROWS 4u              /* the mute / solo screen: the lower four pad rows */

/* Launchpad 95's value maps (SubSelectorComponent.py), its 0..1 values on the Digitone's 0..127. Volume: 0 and the dB steps -42 -24 -18 -12 -6 0 +6 through its level_to_value
 * (0.1424 0.3066 0.40 0.55 0.70 0.85 1.0); the sends: SEND_VALUE_MAP; pan: PAN_VALUE_MAP (-1 -0.63 -0.32 0 0 0.32 0.63 1) as 0..127, the two middle pads the centre (64). The
 * Digitone's own curves are not measured: these are Launchpad 95's steps taken as fractions of its range. */
static const uint8_t VOL_MAP[PAD_ROWS] = {0, 18, 39, 51, 70, 89, 108, 127};
static const uint8_t SEND_MAP[PAD_ROWS] = {0, 13, 21, 30, 44, 70, 98, 127};
static const uint8_t PAN_MAP[PAD_ROWS] = {0, 23, 43, 64, 64, 84, 104, 127};
/* Launchpad 95's slow faders (Settings.py TDC_MAP, in ms): how long a sweep of the whole range takes, by the time the pad was held (0 .. 2 s in ten steps) */
static const uint32_t SWEEP_MS[JLMX_HOLD_STEPS] = {0, 750, 1500, 3000, 5000, 8000, 12000, 17000, 25000, 40000};
/* ... and a held pad's colours (SkinMK2.py Device.ColorSteps, ColorsMK2.py: RED AMBER YELLOW GREEN MINT LIGHT_BLUE BLUE PINK PURPLE WHITE) */
static const uint8_t HOLD_COLOUR[JLMX_HOLD_STEPS] = {5, 9, 13, 21, 29, 37, 45, 53, 48, 3};
#define FULL_RANGE 127u

void jlmx_init(struct jlmx *s)
{
    memset(s, 0, sizeof *s);
    s->view = JLMX_OVERVIEW;
}

static uint8_t value_of(const struct jdl_mix_track *t, uint8_t view);
static uint8_t *value_ref(struct jdl_mix_track *t, uint8_t view);

/* a pad held when the screen changes does nothing at its release (Launchpad 95: _end_timed_press when the column is remapped) */
static void drop_holds(struct jlmx *s) { memset(s->hold, 0, sizeof s->hold); }

void jlmx_on_mixer(struct jlmx *s, const struct jdl_mixer *m, uint32_t now_ms)
{
    if (s->have && s->held && (uint32_t)(now_ms - s->hold_ms) < JLMX_HOLD_MS)
        return;                        /* a reading from before the press was applied */
    s->held = 0;
    s->m = *m;
    s->have = 1;
    for (unsigned i = 0; i < JLMX_SLIDES; i++) {   /* a slide's fader shows where the slide is, unless the Digitone moved it off the path (its knob): then the slide ends */
        struct jlmx_slide *sl = &s->slide[i];
        uint8_t r, lo, hi;
        if (!sl->on)
            continue;
        r = value_of(&s->m.t[sl->track], sl->view);
        lo = sl->from < sl->sent ? sl->from : sl->sent;
        hi = sl->from < sl->sent ? sl->sent : sl->from;
        if (r + 1u < lo || r > hi + 1u)
            sl->on = 0;                /* (a reading a few steps old is still on the path: the slide goes on) */
        else
            *value_ref(&s->m.t[sl->track], sl->view) = sl->sent;
    }
}

int jlmx_scene(struct jlmx *s, uint8_t idx)
{
    if (idx >= JLMX_VIEWS)
        return 0;
    s->view = idx;
    drop_holds(s);
    return 1;
}

void jlmx_overview(struct jlmx *s)
{
    s->view = JLMX_OVERVIEW;
    drop_holds(s);
}

int jlmx_bank_step(struct jlmx *s, int dir)
{
    if (dir > 0 && s->bank + 1u < JLMX_BANKS) {
        s->bank++;
        drop_holds(s);
        return 1;
    }
    if (dir < 0 && s->bank > 0u) {
        s->bank--;
        drop_holds(s);
        return 1;
    }
    return 0;
}

/* a pad row counted from the top (0 the top one), as Launchpad 95 counts them */
static unsigned row_from_top(uint8_t y) { return PAD_ROWS - 1u - y; }

int jlmx_open(struct jlmx *s, uint8_t y)
{
    unsigned r = row_from_top(y);
    if (s->view != JLMX_OVERVIEW || y >= PAD_ROWS || r > OVERVIEW_LAST_VALUE_ROW)
        return 0;
    s->view = (uint8_t)r;              /* the rows are in the scene buttons' order: volume, pan, delay, reverb, chorus */
    drop_holds(s);
    return 1;
}

/* the value a view shows for a track */
static uint8_t value_of(const struct jdl_mix_track *t, uint8_t view)
{
    switch (view) {
    case JLMX_VOL: return t->level;   /* the volume view is the track LEVEL, the Digitone's mixer fader (owner 2026-11-25), not the sound's VOL */
    case JLMX_PAN: return t->pan;
    case JLMX_DEL: return t->del;
    case JLMX_REV: return t->rev;
    default: return t->chr;
    }
}

static const uint8_t *map_of(uint8_t view)
{
    switch (view) {
    case JLMX_VOL: return VOL_MAP;
    case JLMX_PAN: return PAN_MAP;
    default: return SEND_MAP;
    }
}

/* Launchpad 95's PreciseButtonSliderElement._send_value_volume: nothing is lit for 0, else the pads up to the first step the value does not pass */
static int volume_lit(const uint8_t *map, uint8_t v, unsigned row)
{
    if (v == 0)
        return 0;
    for (unsigned i = 0; i < PAD_ROWS; i++)
        if (v <= map[i])
            return row <= i;
    return 1;                          /* above the top step (a value past 127): the whole fader */
}

/* ... and _send_value_pan: the centre pair for 63 / 64, else a bar from the centre pair out to the value */
static int pan_lit(uint8_t v, unsigned row)
{
    if (v == PAN_CENTRE_A || v == PAN_CENTRE_B)
        return row == PAN_CENTRE_LOW || row == PAN_CENTRE_HIGH;
    if (v < PAN_CENTRE_A)
        return row <= PAN_CENTRE_LOW && PAN_MAP[row] >= v;
    return row >= PAN_CENTRE_HIGH && PAN_MAP[row] <= v;
}

static uint8_t *value_ref(struct jdl_mix_track *t, uint8_t view)
{
    switch (view) {
    case JLMX_VOL: return &t->level;
    case JLMX_PAN: return &t->pan;
    case JLMX_DEL: return &t->del;
    case JLMX_REV: return &t->rev;
    default: return &t->chr;
    }
}

static uint8_t param_of(uint8_t view)
{
    switch (view) {
    case JLMX_VOL: return JDL_MIX_LEVEL;
    case JLMX_PAN: return JDL_MIX_PAN;
    case JLMX_DEL: return JDL_MIX_DEL;
    case JLMX_REV: return JDL_MIX_REV;
    default: return JDL_MIX_CHR;
    }
}

/* The mute / solo screen (owner 2026-11-24: "make them on/off buttons for 16 tracks, both solo and mute, 4 rows"): both scene buttons 6 and 7 show it. The lower four pad rows
 * (y 3 .. 0), as the Digitone's trig keys lie, tracks 1-8 above 9-16: y 3 mute 1-8, y 2 mute 9-16, y 1 solo 1-8, y 0 solo 9-16. Whatever the bank. */
static int is_mute_solo(uint8_t view) { return view == JLMX_MUTE || view == JLMX_SOLO; }

static unsigned mute_solo_track(uint8_t y, uint8_t x) { return (y & 1u ? 0u : JLMX_COLUMNS) + x; }

/* mute or solo of a track toggled: the whole new mask (the mute / solo screen, or the overview's mute / solo row) */
static void toggle_mask(struct jlmx *s, unsigned track, int solo, struct jlmx_write *w)
{
    uint32_t *mask = solo ? &s->m.solo : &s->m.mute;
    *mask ^= 1u << track;
    w->kind = JLMX_WRITE_MASK;
    w->which = solo ? 1u : 0u;
    w->mask = (uint16_t)*mask;
}

int jlmx_press(struct jlmx *s, uint8_t x, uint8_t y, uint32_t now_ms, struct jlmx_write *w)
{
    unsigned track = s->bank * JLMX_COLUMNS + x;
    if (!s->have || x >= JLMX_COLUMNS || y >= PAD_ROWS || JLN_NO_TRACK(is_mute_solo(s->view) ? mute_solo_track(y, x) : track))   /* (a pad past the unit's last track: nothing) */
        return 0;
    memset(w, 0, sizeof *w);
    if (s->view == JLMX_OVERVIEW) {
        unsigned r = row_from_top(y);
        if (r != OVERVIEW_MUTE_ROW && r != OVERVIEW_SOLO_ROW)
            return 0;                  /* the value rows open a view (jlmx_open); the last row has no job */
        toggle_mask(s, track, r == OVERVIEW_SOLO_ROW, w);
    } else if (is_mute_solo(s->view)) {
        if (y >= MUTE_SOLO_ROWS)
            return 0;                  /* the screen is the lower four pad rows; the rest is dark */
        toggle_mask(s, mute_solo_track(y, x), y < MUTE_SOLO_ROWS / 2u, w);
    } else {                           /* a fader pad: held until its release (jlmx_release) */
        s->hold[x] = (struct jlmx_held){1, y, (uint8_t)track, s->view, now_ms};
        return 0;
    }
    s->held = 1;
    s->hold_ms = now_ms;
    return 1;
}

static void value_write(struct jlmx *s, uint8_t track, uint8_t view, uint8_t v, struct jlmx_write *w)
{
    *value_ref(&s->m.t[track], view) = v;
    memset(w, 0, sizeof *w);
    w->kind = JLMX_WRITE_VALUE;
    w->track = track;
    w->param = param_of(view);
    w->value = v;
}

/* where a slide is at now: 127 every sweep_ms from start_ms, never past its target */
static uint8_t slide_at(const struct jlmx_slide *sl, uint32_t now_ms)
{
    uint64_t moved = (uint64_t)(uint32_t)(now_ms - sl->start_ms) * FULL_RANGE / sl->sweep_ms;
    if (sl->from < sl->target)
        return moved >= (uint64_t)(sl->target - sl->from) ? sl->target : (uint8_t)(sl->from + moved);
    return moved >= (uint64_t)(sl->from - sl->target) ? sl->target : (uint8_t)(sl->from - moved);
}

/* the slide of this track and fader (it is replaced), else a free one, else the oldest */
static struct jlmx_slide *slide_for(struct jlmx *s, uint8_t track, uint8_t view)
{
    struct jlmx_slide *free_one = NULL, *oldest = &s->slide[0];
    for (unsigned i = 0; i < JLMX_SLIDES; i++) {
        struct jlmx_slide *sl = &s->slide[i];
        if (sl->on && sl->track == track && sl->view == view)
            return sl;
        if (!sl->on && !free_one)
            free_one = sl;
        if (sl->serial < oldest->serial)
            oldest = sl;
    }
    return free_one ? free_one : oldest;
}

/* a slide that has arrived: done, and the Digitone's older readings are kept off the fader a moment (as after a press) */
static void slide_done(struct jlmx *s, struct jlmx_slide *sl, uint32_t now_ms)
{
    sl->on = 0;
    s->held = 1;
    s->hold_ms = now_ms;
}

int jlmx_release(struct jlmx *s, uint8_t x, uint8_t y, uint32_t now_ms, struct jlmx_write *w)
{
    struct jlmx_held h;
    struct jlmx_slide *sl;
    uint32_t held_ms;
    uint8_t target, step;
    if (x >= JLMX_COLUMNS || !s->hold[x].on || s->hold[x].y != y)
        return 0;
    h = s->hold[x];
    s->hold[x].on = 0;
    held_ms = now_ms - h.ms;
    step = held_ms >= JLMX_HOLD_STEP_MS * JLMX_HOLD_STEPS ? JLMX_HOLD_STEPS - 1u : (uint8_t)(held_ms * (JLMX_HOLD_STEPS - 1u) / (JLMX_HOLD_STEP_MS * JLMX_HOLD_STEPS));
    target = map_of(h.view)[h.y];      /* the pad's step: the lowest sets 0 and darkens the fader, as Launchpad 95 does */
    sl = slide_for(s, h.track, h.view);
    if (sl->on && sl->track == h.track && sl->view == h.view)
        sl->on = 0;                    /* a new press on the same fader replaces its slide */
    if (!SWEEP_MS[step]) {             /* a tap: there at once */
        value_write(s, h.track, h.view, target, w);
        s->held = 1;
        s->hold_ms = now_ms;
        return 1;
    }
    *sl = (struct jlmx_slide){1, h.track, h.view, value_of(&s->m.t[h.track], h.view), target, 0, now_ms - JLMX_SLIDE_FIRST_MS, SWEEP_MS[step], ++s->slide_serial};
    sl->sent = slide_at(sl, now_ms);
    value_write(s, h.track, h.view, sl->sent, w);
    if (sl->sent == target)
        slide_done(s, sl, now_ms);
    return 1;
}

size_t jlmx_slides(struct jlmx *s, uint32_t now_ms, struct jlmx_write *w, size_t max)
{
    size_t n = 0;
    if ((uint32_t)(now_ms - s->tick_ms) < JLMX_SLIDE_TICK_MS)
        return 0;
    s->tick_ms = now_ms;
    for (unsigned i = 0; i < JLMX_SLIDES && n < max; i++) {
        struct jlmx_slide *sl = &s->slide[i];
        uint8_t v;
        if (!sl->on)
            continue;
        v = slide_at(sl, now_ms);
        if (v == sl->sent)
            continue;                  /* a slow slide: not a whole step yet */
        sl->sent = v;
        value_write(s, sl->track, sl->view, v, &w[n++]);
        if (v == sl->target)
            slide_done(s, sl, now_ms);
    }
    return n;
}

void jlmx_stop_slides(struct jlmx *s)
{
    memset(s->slide, 0, sizeof s->slide);
    drop_holds(s);
}

/* a fader view's colour (the owner's picks) */
static uint8_t view_colour(uint8_t view)
{
    if (view == JLMX_VOL)
        return JLMX_COLOR_VOL;
    if (view == JLMX_PAN)
        return JLMX_COLOR_PAN;
    return JLMX_COLOR_SEND;
}

/* ... and that colour's third (a view button whose view is not shown) */
static uint8_t view_dim(uint8_t view)
{
    if (view == JLMX_VOL)
        return JLMX_COLOR_VOL_DIM;
    if (view == JLMX_PAN)
        return JLMX_COLOR_PAN_DIM;
    return JLMX_COLOR_SEND_DIM;
}

static uint8_t overview_colour(const struct jlmx *s, unsigned track, unsigned row)
{
    unsigned r = row_from_top((uint8_t)row);
    if (r <= OVERVIEW_LAST_VALUE_ROW)  /* a pad that opens its fader screen, in that fader's colour (Launchpad 95: Mixer.Volume / Pan / Sends, all MINT) */
        return view_colour((uint8_t)r);
    if (r == OVERVIEW_MUTE_ROW)
        return (s->m.mute >> track) & 1u ? JLMX_COLOR_MUTED : JLMX_COLOR_UNMUTED;
    if (r == OVERVIEW_SOLO_ROW)
        return (s->m.solo >> track) & 1u ? JLMX_COLOR_SOLO : JLMX_COLOR_NOSOLO;
    return 0;
}

static uint8_t column_colour(const struct jlmx *s, unsigned track, unsigned row, uint32_t now_ms)
{
    const struct jdl_mix_track *t = &s->m.t[track];
    const struct jlmx_held *h = &s->hold[track % JLMX_COLUMNS];
    uint8_t v;
    if (JLN_NO_TRACK(is_mute_solo(s->view) ? mute_solo_track((uint8_t)row, (uint8_t)(track % JLMX_COLUMNS)) : track))
        return 0;                      /* past the unit's last track: dark */
    if (s->view == JLMX_OVERVIEW)
        return overview_colour(s, track, row);
    if (is_mute_solo(s->view)) {
        unsigned of;
        if (row >= MUTE_SOLO_ROWS)
            return 0;
        of = mute_solo_track((uint8_t)row, (uint8_t)(track % JLMX_COLUMNS));
        if (row < MUTE_SOLO_ROWS / 2u)
            return (s->m.solo >> of) & 1u ? JLMX_COLOR_SOLO : JLMX_COLOR_NOSOLO;
        return (s->m.mute >> of) & 1u ? JLMX_COLOR_MUTED : JLMX_COLOR_UNMUTED;
    }
    if (h->on && h->track == track && h->view == s->view && h->y == row) {   /* the held pad: its hold colour, one more every JLMX_HOLD_STEP_MS */
        uint32_t step = (uint32_t)(now_ms - h->ms) / JLMX_HOLD_STEP_MS;
        return HOLD_COLOUR[step < JLMX_HOLD_STEPS ? step : JLMX_HOLD_STEPS - 1u];
    }
    v = value_of(t, s->view);
    if (s->view == JLMX_PAN)
        return pan_lit(v, row) ? view_colour(s->view) : view_dim(s->view);   /* the fader's empty part in its dim colour (owner 2026-11-25), as its scene button off */
    return volume_lit(map_of(s->view), v, row) ? view_colour(s->view) : view_dim(s->view);
}

void jlmx_frame(const struct jlmx *s, uint32_t now_ms, struct jlx_frame *f)
{
    for (uint8_t x = 0; x < JLMX_COLUMNS; x++)
        for (uint8_t y = 0; y < PAD_ROWS; y++)
            f->led[JLX_PAD0 + y * 8u + x].color = s->have ? column_colour(s, s->bank * JLMX_COLUMNS + x, y, now_ms) : 0;
}

uint8_t jlmx_scene_colour(const struct jlmx *s, uint8_t idx)
{
    int on = s->view == idx;
    if (idx <= JLMX_CHR)
        return on ? (idx >= JLMX_DEL ? JLMX_COLOR_SEND_ON : view_colour(idx)) : view_dim(idx);   /* its fader's colour, full or third; the sends: the owner's own pair */
    on = is_mute_solo(s->view);        /* scene buttons 6 and 7 are the same screen: both bright while it is shown */
    if (idx == JLMX_MUTE)
        return on ? JLMX_COLOR_UNMUTED : JLMX_COLOR_MUTED;
    if (idx == JLMX_SOLO)
        return on ? JLMX_COLOR_SOLO : JLMX_COLOR_NOSOLO;
    return 0;
}
