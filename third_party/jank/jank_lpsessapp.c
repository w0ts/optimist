/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad app's Session button and page (jank_lpsess.h), the colour chart's opening, and who starts and stops (jank_lptrans.h): the part of jank_lpapp.h that talks to
 * the Digitone's stage 4 and stage 8 commands. */
#include "jank_lpapp_int.h"

static void say_chart_page(struct jlapp *a) { jlapp_say(a, "COLOURS %u-%u", (unsigned)a->palette_page * 64u, (unsigned)a->palette_page * 64u + 63u); }

/* Session held: the colour chart (jank_lpapp.h), on its first page; it goes back to the mode it came from (the Session page too) */
void jlapp_open_chart(struct jlapp *a)
{
    a->palette_prev = a->mode;
    a->palette_page = 0;
    a->mode = JLAPP_MODE_PALETTE;
    say_chart_page(a);
}

static void open_session(struct jlapp *a, uint8_t from)
{
    a->sess_prev = from;
    a->mode = JLAPP_MODE_SESSION;
    jlss_open(&a->sess);               /* every bank read again: patterns may have been filled or cleared since */
    jlapp_say(a, "SESSION %s", a->sess.half ? "BANKS E-H" : "BANKS A-D");
}

/* Session (jank_lpapp.h): in the chart a press turns its page, then leaves it, as before. Elsewhere the press only starts the clock: let go within JLAPP_SESSION_HOLD_MS, it
 * is the Session page (or, on it, back to where it came from); held that long, jlapp_tick opens the chart */
void jlapp_session_button(struct jlapp *a, int down)
{
    if (down) {
        if (a->mode != JLAPP_MODE_PALETTE) {
            a->sess_down = 1;
            a->sess_down_ms = a->now_ms;
        } else if (a->palette_page + 1u < JLAPP_PALETTE_PAGES) {
            a->palette_page++;
            say_chart_page(a);
        } else {
            a->mode = a->palette_prev;
            if (a->mode == JLAPP_MODE_SESSION)
                jlss_open(&a->sess);
            jlapp_say(a, "COLOURS OFF");
        }
        return;
    }
    if (!a->sess_down)                 /* the release of a chart press, or of the hold that opened the chart */
        return;
    a->sess_down = 0;
    if (a->mode != JLAPP_MODE_SESSION) {
        open_session(a, a->mode);
        return;
    }
    a->mode = a->sess_prev;
    jlapp_say(a, "SESSION OFF");
}

/* the held pad let go: the chain built on it goes (CHAIN SET), or the playing pattern held alone clears the chain (CHAIN CLEAR) */
static size_t session_release(struct jlapp *a, uint8_t pad, struct jlapp_act *out, size_t max)
{
    int k = jlss_release(&a->sess, pad, a->now_ms);
    size_t n;
    if (k == JLSS_RELEASE_NONE)
        return 0;
    if (a->live_state != JDL_LIVE) {
        jlapp_say_why_not(a);
        return 0;
    }
    if (!a->chain_ok) {
        jlapp_say(a, "CHAIN: DN2 TOO OLD");
        return 0;
    }
    if (max < 1)
        return 0;
    if (k == JLSS_RELEASE_CLEAR) {
        jlss_chain_cleared(&a->sess);
        jlapp_say(a, "CHAIN CLEARED");
        jlapp_set_act(out, JLAPP_ACT_CHAIN, 0, 0, JDL_CHAIN_CLEAR);
        return 1;
    }
    jlss_chain_list(&a->sess, &n);
    jlss_chain_sent(&a->sess);         /* shown at once, read back; the list stays until the next press (the firmware reads it with jlapp_chain_list) */
    jlapp_say(a, "CHAIN OF %u", (unsigned)n);
    jlapp_set_act(out, JLAPP_ACT_CHAIN, 0, (uint8_t)n, JDL_CHAIN_SET);
    return 1;
}

/* A pad of the Session page: the first one pressed cues its pattern (PATTERN how 0), which flashes at once; with it held, the others build a chain (jank_lpsess.h) */
size_t jlapp_session_pad(struct jlapp *a, const struct jlx_ev *ev, struct jlapp_act *out, size_t max)
{
    uint8_t pad = (uint8_t)(ev->row * 8u + ev->col), p = jlss_pattern_of_pad(&a->sess, ev->col, ev->row);
    char name[4];
    if (!ev->value)
        return session_release(a, pad, out, max);
    if (a->live_state != JDL_LIVE) {
        jlapp_say_why_not(a);
        return 0;
    }
    jlss_name(p, name);
    switch (jlss_press(&a->sess, pad, p, a->now_ms)) {
    case JLSS_PRESS_ADDED:
        jlapp_say(a, "CHAIN %u: %s", (unsigned)a->sess.build_n, name);
        return 0;
    case JLSS_PRESS_FULL:
        jlapp_say(a, "CHAIN FULL (%u)", (unsigned)JDL_CHAIN_MAX);
        return 0;
    default:
        break;
    }
    if (!a->pat_cue_ok) {
        jlapp_say(a, "PATTERN: DN2 TOO OLD");
        return 0;
    }
    if (max < 1)
        return 0;
    jlss_cue(&a->sess, p, a->now_ms);
    jlapp_say(a, "CUE %s", name);
    jlapp_set_act(out, JLAPP_ACT_PATTERN, 0, p, JDL_PATTERN_CUE);
    return 1;
}

int jlapp_session_due(struct jlapp *a, struct jlss_req *r)
{
    unsigned ok = (a->pat_reads_ok ? JLSS_OK_READS : 0u) | (a->pat_cue_ok ? JLSS_OK_CUE : 0u) | (a->chain_ok ? JLSS_OK_CHAIN : 0u) | (a->live7 ? JLSS_OK_LIVE7 : 0u);
    if (a->mode != JLAPP_MODE_SESSION || a->live_state != JDL_LIVE)
        return 0;
    return jlss_due(&a->sess, a->now_ms, ok, r);
}

void jlapp_on_patterns(struct jlapp *a, const struct jdl_patterns *p) { jlss_on_patterns(&a->sess, p); }

void jlapp_on_pattern(struct jlapp *a, const struct jdl_pattern *p) { jlss_on_next(&a->sess, p->playing, p->next, a->now_ms); }

void jlapp_on_chain(struct jlapp *a, const struct jdl_chain *c) { jlss_on_chain(&a->sess, c); }

const uint8_t *jlapp_chain_list(const struct jlapp *a, size_t *n) { return jlss_chain_list(&a->sess, n); }

void jlapp_set_patterns(struct jlapp *a, int reads_ok, int cue_ok)
{
    if (a->pat_cue_ok && !cue_ok) {    /* a Digitone without PATTERN: its pads cue nothing (said again at each press) */
        a->sess.cued = JDL_NO_PATTERN;
        jlapp_say(a, "PATTERN: DN2 TOO OLD");
    }
    a->pat_cue_ok = cue_ok != 0;
    a->pat_reads_ok = reads_ok != 0;
}

/* ---- who starts and stops (jank_lptrans.h) ---- */

void jlapp_set_peers(struct jlapp *a, int peers) { jltr_set_peers(&a->tr, peers); }

int jlapp_dn2_leads(const struct jlapp *a) { return jltr_dn2_leads(&a->tr, a->live_state == JDL_LIVE); }

void jlapp_set_stage8(struct jlapp *a, int transport_ok, int chain_ok)
{
    /* silent: the probe runs each time live control comes up, and a Digitone without stage 8 would show the same refusal every time (owner's screen). Scene 5 is
     * then Jank's session as before stage 8; a chain or a cue press says "... DN2 TOO OLD" itself */
    a->tr.cmd_ok = transport_ok != 0;
    if (!transport_ok)
        a->tr.has_cmd = 0;
    a->chain_ok = chain_ok != 0;
}

int jlapp_on_dn2_transport(struct jlapp *a, int state)
{
    if (state < 0)
        return JLTR_NO_REQUEST;
    return jltr_on_dn2(&a->tr, (unsigned)state, 1, a->now_ms);
}

int jlapp_on_live7(struct jlapp *a, const struct jdl_live7 *s)
{
    a->live7 = 1;
    jlss_on_next(&a->sess, s->playing, s->next, a->now_ms);
    jlss_on_chain_count(&a->sess, s->chain_count);
    return jltr_on_dn2(&a->tr, s->transport, 0, a->now_ms);
}

/* scene 5: while the Digitone leads, its TRANSPORT (PLAY / STOP: no clock, no Start byte from Jank); else Jank's session, as before */
size_t jlapp_start_stop(struct jlapp *a, struct jlapp_act *out, size_t max)
{
    uint8_t op;
    if (max < 1)
        return 0;
    if (jltr_press(&a->tr, a->live_state == JDL_LIVE, a->now_ms, &op)) {
        jlapp_say(a, op == JDL_TR_PLAY ? "START" : "STOP");
        jlapp_set_act(out, JLAPP_ACT_DN2_TRANSPORT, 0, 0, op);
        return 1;
    }
    jlapp_say(a, a->transport ? "STOP" : "START");
    jlapp_set_act(out, JLAPP_ACT_TRANSPORT, a->n.track, 0, 0);
    return 1;
}
