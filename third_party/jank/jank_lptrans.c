/* SPDX-License-Identifier: GPL-3.0-only */
/* Who starts and stops (jank_lptrans.h). */
#include "jank_lptrans.h"
#include "jank_lpstr.h"
#include "jank_dn2live.h"

void jltr_init(struct jltr *t)
{
    memset(t, 0, sizeof *t);
    t->cmd_ok = 1;
}

void jltr_set_peers(struct jltr *t, int peers) { t->peers = peers != 0; }

int jltr_dn2_leads(const struct jltr *t, int live) { return live && !t->peers && t->cmd_ok && t->has_cmd; }

void jltr_lost(struct jltr *t)
{
    t->has_cmd = 0;
    t->known = 0;
    t->hold_on = 0;
}

int jltr_press(struct jltr *t, int live, uint32_t now_ms, uint8_t *op)
{
    if (!jltr_dn2_leads(t, live))
        return 0;
    *op = t->known && t->state == JDL_TR_PLAYING ? (uint8_t)JDL_TR_STOP : (uint8_t)JDL_TR_PLAY;
    t->known = 1;                      /* shown at once */
    t->state = *op == JDL_TR_STOP ? (uint8_t)JDL_TR_STOPPED : (uint8_t)JDL_TR_PLAYING;
    t->hold_on = 1;
    t->hold_ms = now_ms;
    return 1;
}

int jltr_on_dn2(struct jltr *t, unsigned state, int answer, uint32_t now_ms)
{
    int was_known = t->known, was_playing = t->state == JDL_TR_PLAYING, playing = state == JDL_TR_PLAYING;
    if (state > JDL_TR_PAUSED)
        return JLTR_NO_REQUEST;
    if (!answer && t->hold_on && (uint32_t)(now_ms - t->hold_ms) < JLTR_HOLD_MS)
        return JLTR_NO_REQUEST;        /* a status older than the press */
    t->hold_on = 0;
    t->known = 1;
    t->state = (uint8_t)state;
    if (answer)
        t->has_cmd = 1;
    if (!t->peers || !was_known || was_playing == playing || playing == t->jank)
        return JLTR_NO_REQUEST;        /* the Digitone leads, or nothing changed, or it only follows Jank */
    if (t->guard_on && (uint32_t)(now_ms - t->guard_ms) < JLTR_GUARD_MS)
        return JLTR_NO_REQUEST;        /* Jank's own change coming back */
    t->guard_on = 1;
    t->guard_ms = now_ms;
    return playing;
}

void jltr_set_jank(struct jltr *t, int playing, uint32_t now_ms)
{
    if ((playing != 0) == t->jank)
        return;
    t->jank = playing != 0;
    t->guard_on = 1;
    t->guard_ms = now_ms;
}

int jltr_stop_follows(const struct jltr *t, int live) { return live && t->peers && t->cmd_ok && t->has_cmd; }

int jltr_lit(const struct jltr *t, int live)
{
    if (jltr_dn2_leads(t, live) && t->known)
        return t->state == JDL_TR_PLAYING;
    return t->jank;
}
