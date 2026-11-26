/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad part's remembered settings (jank_lpprefs.h). */
#include "jank_lpprefs.h"
#include "jank_lpstr.h"

#define CHECK_SEED 0xA5u

void jlprefs_defaults(struct jlprefs *p)
{
    memset(p, 0, sizeof *p);
    p->layout = JLN_DIATONIC;
    p->horizontal = 1;
    p->octave = 3;
    p->step_kind = JLST_MELODIC;
    p->mix_view = JLMX_OVERVIEW;
}

static uint8_t checksum(const uint8_t *b, size_t n)
{
    uint8_t x = CHECK_SEED;
    for (size_t i = 0; i < n; i++)
        x = (uint8_t)((x << 1 | x >> 7) ^ b[i]);
    return x;
}

size_t jlprefs_pack(const struct jlprefs *p, uint8_t *out, size_t cap)
{
    if (cap < JLPREFS_SIZE)
        return 0;
    out[0] = JLPREFS_VERSION;
    out[1] = p->layout;
    out[2] = p->horizontal;
    out[3] = p->absolute;
    out[4] = p->octave;
    out[5] = p->track_pads;
    out[6] = p->step_kind;
    out[7] = p->mix_view;
    out[8] = p->follow;
    out[9] = p->fixed_vel;
    out[10] = checksum(out, 10);
    return JLPREFS_SIZE;
}

int jlprefs_unpack(struct jlprefs *p, const uint8_t *in, size_t n)
{
    struct jlprefs t;
    size_t body;
    if (n == JLPREFS_SIZE && in[0] == JLPREFS_VERSION)
        body = JLPREFS_SIZE - 1u;
    else if (n == JLPREFS_SIZE_V1 && in[0] == 1u)   /* saved before fixed velocity: kept, the switch off */
        body = JLPREFS_SIZE_V1 - 1u;
    else
        return 0;
    if (in[body] != checksum(in, body))
        return 0;
    t.layout = in[1];
    t.horizontal = in[2];
    t.absolute = in[3];
    t.octave = in[4];
    t.track_pads = in[5];
    t.step_kind = in[6];
    t.mix_view = in[7];
    t.follow = in[8];
    t.fixed_vel = body > 9u ? in[9] : 0u;
    if (t.fixed_vel > 1u ||
        t.layout > JLN_CHORDS || t.horizontal > 1u || t.absolute > 1u || t.octave > 6u || t.track_pads >= JLAPP_TRACKS_MODES || t.step_kind > JLST_DRUM || t.mix_view > JLMX_OVERVIEW ||
        t.follow > 1u)
        return 0;
    *p = t;
    return 1;
}

int jlprefs_equal(const struct jlprefs *a, const struct jlprefs *b) { return memcmp(a, b, sizeof *a) == 0; }

void jlprefs_from_app(const struct jlapp *a, struct jlprefs *p)
{
    p->layout = a->n.layout;
    p->horizontal = a->n.horizontal;
    p->absolute = a->n.absolute;
    p->octave = a->n.octave;
    p->track_pads = a->track_pads;
    p->step_kind = a->step_kind;
    p->mix_view = a->mix.view;
    p->follow = a->follow;
    p->fixed_vel = a->fixed_vel;
}

void jlprefs_apply(struct jlapp *a, const struct jlprefs *p)
{
    jln_set_layout(&a->n, (enum jln_layout)p->layout);     /* it pulls the octave under the layout's top and sets the layout's own orientation ... */
    a->n.horizontal = p->horizontal;                       /* ... which the stored switch then overrides */
    a->n.absolute = p->absolute;
    jln_set_octave(&a->n, p->octave);                      /* a place past the layout's last octave changes nothing: the pulled-down one stays */
    a->track_pads = p->track_pads;
    a->step_kind = p->step_kind;
    a->step.kind = p->step_kind;
    a->mix.view = p->mix_view;
    a->follow = p->follow;
    a->fixed_vel = p->fixed_vel;
}
