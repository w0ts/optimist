/* SPDX-License-Identifier: GPL-3.0-only */
/* A Launchpad X on the USB host plays and edits the FM-1 (FELUCCA_LAUNCHPAD; the X only: jank_fm1.h): jank's Launchpad core
 * (third_party/jank, copied from github.com/w0ts/jank by tools/sync_jank.sh: its core as docs/launchpad.md "Using the core on another
 * unit" says; there it drives a Digitone II through its live control) with the FM-1 as its unit (lp_unit.c). This file is jank's
 * firmware/main/usb_lp.c for the FM-1: the Launchpad's side as there (Programmer mode, its answer, the setup, a settle, the lights
 * as diffs, every light again every LP_REFRESH_MS), the unit's side as calls.
 *
 * The platform (fm1-base's app: ui/opt_launchpad.h) calls, main loop only:
 *   lp_name_ok(name)   1 when the USB device's product name is a Launchpad jank knows
 *   lp_attach(name), lp_detach()   it came / it went (its notes end, the lights' state is forgotten)
 *   lp_in(pkt)         a USB-MIDI packet from it (raw: every cable, SysEx too)
 *   lp_step(now)       the app's clock, the unit's news, what goes out
 *   lp_out(&pkt)       1 and the next packet for it, 0 when none */
#if FELUCCA_LAUNCHPAD
#include "../../../../third_party/jank/jank_fm1.h"   /* the build's switches for jank's core (the FM-1's tracks, the clips) */
#include "../../../../third_party/jank/jank_lpx.c"
#include "../../../../third_party/jank/jank_lpnote.c"
#include "../../../../third_party/jank/jank_lpscales.c"   /* (Optimist's own: SCL's 16) */
#include "../../../../third_party/jank/jank_lpmix.c"
#include "../../../../third_party/jank/jank_lpstep.c"
#include "../../../../third_party/jank/jank_lpclips.c"
#include "../../../../third_party/jank/jank_lpapp.c"
#endif
#include "lp_unit.c"
#if FELUCCA_LAUNCHPAD
#define LP_ATTACH_MS 500u                        /* (jank's usb_lp.c: ATTACH_WAIT_MS, ACK_WAIT_MS, ACK_TRIES, SETTLE_MS, REFRESH_MS) */
#define LP_ACK_MS 400u
#define LP_ACK_TRIES 4u
#define LP_SETTLE_MS 400u
#define LP_REFRESH_MS 2000u
#define LP_BATCH 16u                             /* lights a pass */
#define LP_OUT 32u                               /* packets waiting for fm1-base's ring (a power of two): a batch of lights, or a SysEx */
#define LP_NEWS_MS 100u                          /* the step pages, the mixer, the scale, the clips, the track: this often */

static struct {
    struct jlapp app;
    struct jlx_frame shown;
    const struct jlx_model *m;
    uint32_t out[LP_OUT];                        /* what goes to the Launchpad, in order (a ring: out_r .. out_w) */
    uint8_t out_r, out_w;
    uint8_t up, init, programmer_pending, setup_pending, tries;
    uint32_t try_at, settle_at, refresh_at, news_ms;
    uint8_t sel_seen, playing_seen, scale_seen, root_seen;
} lp;

static void lp_send(const uint8_t *b, size_t n)  /* n bytes of packets for the Launchpad (whole or not at all: a cut SysEx is lost) */
{
    size_t i;
    if (LP_OUT - (uint8_t)(lp.out_w - lp.out_r) < n / 4u)
        return;
    for (i = 0; i + 4u <= n; i += 4u)
        lp.out[lp.out_w++ % LP_OUT] = (uint32_t)b[i] | (uint32_t)b[i + 1] << 8 | (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 3] << 24;
}
static uint32_t lp_room(void) { return LP_OUT - (uint8_t)(lp.out_w - lp.out_r); }
static void lp_clips_news(void)                  /* the clips page shows only this: after a launch at once, and with the news */
{
#if FELUCCA_PATTERNS
    struct jlcl_track t[JLCL_TRACKS];
    uint32_t scenes = lu_clips(t);
    jlapp_on_clips(&lp.app, t, scenes);
#endif
}

/* ------------------------------------------------------------------ the core's acts on the FM-1 --- */
static void lp_act(const struct jlapp_act *a)
{
    uint32_t jt = a->track;
    switch (a->kind) {
    case JLAPP_ACT_NOTE_ON:
    case JLAPP_ACT_NOTE_OFF:
    case JLAPP_ACT_TRACK_ON:
    case JLAPP_ACT_TRACK_OFF:
        lu_play(jt, a->note, a->vel, a->kind == JLAPP_ACT_NOTE_ON || a->kind == JLAPP_ACT_TRACK_ON, a->kind >= JLAPP_ACT_TRACK_ON);
        return;
    case JLAPP_ACT_SELECT:
        if (jt < LU_TRACKS && lu_trk(jt) != song.sel)
            track_select(lu_trk(jt));
        return;
    case JLAPP_ACT_STEP:
        lu_step(jt, a->step, a->op, a->note, a->vel);
        return;
    case JLAPP_ACT_SCALE:                        /* the scale page: the selected part's SCL and ROOT */
        if (!IS_DRUMK(song.sel) && a->note < NSCALES && a->vel < 12u) {
            trk[song.sel].p[P_SCALE] = a->note;
            trk[song.sel].p[P_ROOT] = a->vel;
            ui.force = 1;
        }
        return;
    case JLAPP_ACT_MIX:
        lu_mix_set(jt, a->op, a->vel);
        return;
    case JLAPP_ACT_MUTE:
        lu_mute_set(a->op, a->note | (uint32_t)a->step << 8);
        return;
    case JLAPP_ACT_TRANSPORT:
    case JLAPP_ACT_DN2_TRANSPORT:                /* (the FM-1 leads its transport: a toggle, or jdl_transport_op's 0 stop 1 play) */
        lu_transport(a->kind == JLAPP_ACT_TRANSPORT ? !song.playing : a->vel == JDL_TR_PLAY);
        return;
#if FELUCCA_PATTERNS
    case JLAPP_ACT_CLIP:
        lu_clip(jt, a->note, a->vel);
        lp_clips_news();
        return;
    case JLAPP_ACT_SCENE:
        lu_scene(a->note);
        lp_clips_news();
        return;
#endif
    default:                                     /* RECORD, ERASE, PATTERN, CHAIN: not on the FM-1 (compiled away: jank_fm1.h) */
        return;
    }
}

/* ------------------------------------------------------------------ the unit's news --- */
static void lp_news(uint32_t now)
{
    struct jdl_note b[8];
    uint8_t want[2];
    uint32_t k = lu_trk(lp.app.n.track), sel = song.sel, abs;
    size_t i, n;
    while ((n = lu_played(b, sizeof b / sizeof b[0])) != 0)   /* the notes played: the lights, every pass */
        jlapp_on_push(&lp.app, b, n);
    if (song.playing != lp.playing_seen) {      /* the start / stop button */
        lp.playing_seen = song.playing;
        jlapp_set_transport(&lp.app, song.playing);
    }
    abs = song.playing ? trk[k].seq_abs : SEQ_NONE;   /* the step modes' playhead: the track's 16ths */
    jlapp_set_beat(&lp.app, abs != SEQ_NONE, abs != SEQ_NONE ? abs : 0u);
    if (now - lp.news_ms < LP_NEWS_MS && !jlapp_step_urgent(&lp.app))
        return;
    lp.news_ms = now;
    for (i = 0, n = jlapp_step_wanted(&lp.app, want); i < n; i++) {   /* a step mode's pages of the current track */
        struct jdl_page pg;
        if (lu_page(lp.app.n.track, want[i], &pg))
            jlapp_on_page(&lp.app, &pg);
    }
    if (jlapp_mixer_wanted(&lp.app)) {
        struct jdl_mixer mx;
        lu_mixer(&mx);
        jlapp_on_mixer(&lp.app, &mx);
    }
    if (!IS_DRUMK(sel) && (trk[sel].p[P_SCALE] != lp.scale_seen || trk[sel].p[P_ROOT] != lp.root_seen)) {
        lp.scale_seen = (uint8_t)trk[sel].p[P_SCALE];   /* (the FM-1's change: the Launchpad follows) */
        lp.root_seen = (uint8_t)trk[sel].p[P_ROOT];
        jlapp_on_dn2_scale(&lp.app, lp.scale_seen, 1, lp.root_seen);
    }
    if (jlapp_following(&lp.app) && sel != lp.sel_seen)
        jlapp_on_active_track(&lp.app, sel);
    lp.sel_seen = (uint8_t)sel;
    lp_clips_news();
}
static void lp_messages(void)
{
    static char m[JLAPP_MSG_MAX];
    if (jlapp_take_message(&lp.app, m, sizeof m))
        ui_message(m);
}

/* ------------------------------------------------------------------ the platform's calls --- */
static int lp_name_ok(const char *name) { return name && name[0] && jlx_model_find(name) != NULL; }
static void lp_attach(const char *name)
{
    if (!lp.init) {
        lp.init = 1;
        jlapp_init(&lp.app, jls_get(JLS_MAJOR));
    }
    lp.m = jlx_model_find(name);
    if (!lp.m)
        lp.m = &jlx_models[0];
    lp.app.no_vel = (uint8_t)!lp.m->velocity;
    lp.up = lp.programmer_pending = 1;
    lp.setup_pending = lp.tries = 0;
    lp.try_at = fm1_ms + LP_ATTACH_MS;
    lp.playing_seen = lp.scale_seen = lp.root_seen = 255;   /* (told at the first pass) */
    lp.out_r = lp.out_w;
    jlapp_set_dn2(&lp.app, 1);
    jlapp_set_live(&lp.app, JDL_LIVE, 1, 1);     /* (it plays, it can be read) */
    jlapp_set_mixer_writes(&lp.app, 1);
    lu_tap(1);
}
static void lp_detach(void)
{
    struct jlapp_act off[JLAPP_PADS + JLN_TRACKS + 1u];
    size_t i, n;
    if (!lp.up)
        return;
    lp.up = lp.programmer_pending = lp.setup_pending = 0;
    for (i = 0, n = jlapp_release_acts(&lp.app, off, sizeof off / sizeof off[0]); i < n; i++)
        lp_act(&off[i]);
    jlapp_set_dn2(&lp.app, 0);
    lp.out_r = lp.out_w;
    lu_all_off();
    lu_tap(0);
}
static void lp_in(uint32_t pkt)
{
    uint8_t p[4] = {(uint8_t)pkt, (uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
    struct jlapp_act acts[JLAPP_ACTS_MAX];
    struct jlx_ev ev;
    uint32_t cin = p[0] & 0x0Fu;
    size_t i, made;
    int mode;
    if (!lp.up)
        return;
    if (jlx_programmer_ack(lp.m, p, &mode)) {   /* its answer to Programmer mode (or to the mode query) */
        if (mode && lp.programmer_pending)
            lp.programmer_pending = 0, lp.setup_pending = 1;
        else if (!mode && !lp.programmer_pending)   /* it fell into Live mode: Programmer mode again */
            lp.programmer_pending = 1, lp.tries = 0, lp.try_at = fm1_ms;
        return;
    }
    if ((cin != 0x8u && cin != 0x9u && cin != 0xBu) || !jlx_decode(p + 1, &ev))   /* (Note Off, Note On, CC) */
        return;
    jlapp_tick(&lp.app, fm1_ms);
    made = jlapp_on_lpx_acts(&lp.app, &ev, acts, JLAPP_ACTS_MAX);
    for (i = 0; i < made; i++)
        lp_act(&acts[i]);
    lp_messages();
}
static void lp_lights(uint32_t now)              /* the Launchpad's side: its mode, then the lights that changed */
{
    uint8_t b[4u * 24u];
    struct jlx_frame want;
    size_t n;
    if (lp.programmer_pending) {                 /* the whole message or none of it */
        if ((int32_t)(now - lp.try_at) < 0 || lp_room() < jlx_sysex(lp.m, lp.m->start, NULL) / 4u)
            return;
        lp_send(b, jlx_sysex(lp.m, lp.m->start, b));
        lp.try_at = now + LP_ACK_MS;
        if (!lp.m->ack || ++lp.tries >= LP_ACK_TRIES)   /* it answers nothing, or never answered: the setup and the lights go anyway */
            lp.programmer_pending = 0, lp.setup_pending = 1;
        return;
    }
    if (lp.setup_pending) {
        if (lp_room() < jlx_sysex(lp.m, lp.m->setup, NULL) / 4u)
            return;
        lp_send(b, jlx_sysex(lp.m, lp.m->setup, b));
        lp.setup_pending = 0;
        jlx_forget(&lp.shown);
        lp.settle_at = now + LP_SETTLE_MS;
        lp.refresh_at = lp.settle_at + LP_REFRESH_MS;
        return;
    }
    if ((int32_t)(now - lp.settle_at) < 0 || lp.out_r != lp.out_w)
        return;                                  /* (the next lights when the last ones went) */
    if ((int32_t)(now - lp.refresh_at) >= 0) {   /* every light again, and which mode is it in? */
        jlx_forget(&lp.shown);
        lp.refresh_at = now + LP_REFRESH_MS;
        if ((n = jlx_sysex(lp.m, lp.m->query, b)) != 0) {
            lp_send(b, n);
            return;
        }
    }
    jlapp_frame(&lp.app, &want);
    lp_send(b, 4u * jlx_diff(lp.m, &lp.shown, &want, b, LP_BATCH));
}
static void lp_step(uint32_t now)
{
    struct jlapp_act slides[JLMX_SLIDES];
    size_t i, made;
    if (lu.nheld)
        lu_flush();                              /* (an off the ring had no room for: again) */
    if (!lp.up)
        return;
    jlapp_tick(&lp.app, now);
    lp_news(now);
    made = jlapp_pump_acts(&lp.app, slides, JLMX_SLIDES);   /* the mixer's slow faders */
    for (i = 0; i < made; i++)
        lp_act(&slides[i]);
    lp_messages();
    lp_lights(now);
}
static int lp_out(uint32_t *pkt)
{
    if (lp.out_r == lp.out_w)
        return 0;
    *pkt = lp.out[lp.out_r++ % LP_OUT];
    return 1;
}
#endif
