/* SPDX-License-Identifier: GPL-3.0-only */
/* The Session page as clips (jank_lpclips.h), and the page in the app (jank_lpapp_int.h). */
#include "jank_lpapp_int.h"
#include "jank_lpstr.h"

#define SCENE_COL 7u                   /* the rightmost pad column: the scenes */

void jlapp_page_init(struct jlapp *a)
{
    for (unsigned i = 0; i < JLCL_TRACKS; i++)
        a->sess.t[i].playing = a->sess.t[i].queued = JLCL_NONE;
}

void jlapp_page_open(struct jlapp *a, int live)   /* nothing to read: the unit sends its news */
{
    (void)a;
    (void)live;
}

void jlapp_on_clips(struct jlapp *a, const struct jlcl_track t[JLCL_TRACKS], uint32_t scenes)
{
    memcpy(a->sess.t, t, sizeof a->sess.t);
    a->sess.scenes = scenes;
}

size_t jlapp_session_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    unsigned s = a->sess.half * 8u + 7u - ev->row;     /* the slot (or the scene) of the row */
    if (!ev->value || max < 1)
        return 0;
    if (ev->col == SCENE_COL) {
        jlapp_say(a, "SCENE %u", s + 1u);
        jlapp_set_act(out, JLAPP_ACT_SCENE, 0, (uint8_t)s, 0);
        return 1;
    }
    if (ev->col >= JLCL_TRACKS)
        return 0;
    if (s == a->sess.t[ev->col].playing) {   /* the playing slot: a stop */
        s = JLCL_NONE;
        jlapp_say(a, "TRACK %u STOP", ev->col + 1u);
    } else {
        jlapp_say(a, "TRACK %u CLIP %u", ev->col + 1u, s + 1u);
    }
    jlapp_set_act(out, JLAPP_ACT_CLIP, ev->col, (uint8_t)s, 0);   /* when: at the pattern's end */
    return 1;
}

void jlapp_page_frame(const struct jlapp *a, struct jlx_frame *f)
{
    for (unsigned y = 0; y < 8u; y++) {
        unsigned s = a->sess.half * 8u + 7u - y;
        struct jlx_led *l = &f->led[JLX_PAD0 + y * 8u];
        l[SCENE_COL].color = (a->sess.scenes >> s) & 1u ? JLCL_COLOR_SCENE : 0u;
        for (unsigned x = 0; x < JLCL_TRACKS; x++) {
            const struct jlcl_track *t = &a->sess.t[x];
            if (s == t->playing || s == t->queued) {   /* (flashing with dark) */
                l[x].color = JLCL_COLOR_PLAYING;
                l[x].mode = s == t->playing && t->queued != JLCL_STOP ? JLX_PULSE : JLX_FLASH;
            } else if ((t->filled >> s) & 1u) {
                l[x].color = JLCL_COLOR_FILLED;
            }
        }
    }
}
