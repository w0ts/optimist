/* SPDX-License-Identifier: GPL-3.0-only */
/* A Launchpad X (or Mini MK3, MK2, Pro) on the USB host plays and edits the FM-1 (FELUCCA_LAUNCHPAD): jank's Launchpad app
 * (third_party/jank, copied from github.com/w0ts/jank by tools/sync_jank.sh: its core as docs/launchpad.md "Using the core on another
 * unit" says; there it drives a Digitone II through its live control)
 * with the FM-1 as its unit (lp_unit.c). This file is jank's firmware/main/usb_lp.c for the FM-1: the Launchpad's side as there
 * (Programmer mode, its answer, the setup, a settle, the lights as diffs, every light again every LP_REFRESH_MS), the unit's
 * side as calls instead of SysEx.
 *
 * jank's tracks (owner 2026-11-26, "4 + 11"): 0..2 the parts T1..T3, 3 the drum track as a whole (a note is the lane whose
 * instrument it is, as the FM-1's MIDI input plays it), 4..14 its lanes 0..10 (14 the ACCENT lane: no sound, no mute): a
 * lane track's notes are that lane's hits whatever their pitch; its steps, mute, solo, pan and VOL (its LEVEL edit) are the
 * lane's; its length and its clips the drum track's.
 *
 * The platform (fm1-base's app: ui/opt_launchpad.h) calls, main loop only:
 *   lp_name_ok(name)   1 when the USB device's product name is a Launchpad jank knows
 *   lp_attach(name), lp_detach()   it came / it went (its notes end, the lights' state is forgotten)
 *   lp_in(pkt)         a USB-MIDI packet from it (raw: every cable, SysEx too)
 *   lp_step(now)       the app's clock, the unit's news, what goes out
 *   lp_out(&pkt)       1 and the next packet for it, 0 when none */
#include "lp_unit.c"
#if FELUCCA_LAUNCHPAD
#include "../../../../third_party/jank/jank_fm1.h"   /* the build's switches for jank's app (the FM-1's tracks, the clips) */
#include "../../../../third_party/jank/jank_lpx.c"
#include "../../../../third_party/jank/jank_lpnote.c"
#include "../../../../third_party/jank/jank_lpscales.c"   /* (Optimist's own: SCL's 16) */
#include "../../../../third_party/jank/jank_lpmix.c"
#include "../../../../third_party/jank/jank_lpstep.c"
#include "../../../../third_party/jank/jank_lpclips.c"
#include "../../../../third_party/jank/jank_lptrans.c"
#include "../../../../third_party/jank/jank_lpprefs.c"
#include "../../../../third_party/jank/jank_lpapp.c"
#define LP_TRACKS (NTRK + DRUM_SLANES)          /* jank's tracks: the 4, then the drum track's lanes (with ACCENT) */
_Static_assert(LP_TRACKS <= JLN_TRACKS, "jank's app has a track for each of the FM-1's tracks and lanes");
#define LP_LANE0 NTRK                            /* jank track of lane 0 */
#define LP_ATTACH_MS 500u                        /* (jank's usb_lp.c: ATTACH_WAIT_MS, ACK_WAIT_MS, ACK_TRIES, SETTLE_MS, REFRESH_MS) */
#define LP_ACK_MS 400u
#define LP_ACK_TRIES 4u
#define LP_SETTLE_MS 400u
#define LP_REFRESH_MS 2000u
#define LP_BATCH 16u                             /* lights a pass */
#define LP_OUT 32u                               /* packets waiting for fm1-base's ring (a power of two): a batch of lights, or a SysEx */
#define LP_PAGE_MS 100u                          /* a step mode's pages read again this often (a local read: cheap) */
#define LP_NEWS_MS 100u                          /* the mixer, the scale, the clips, the track: as often */
#define CIN_NOTE_OFF 0x8u
#define CIN_NOTE_ON 0x9u
#define CIN_CC 0xBu

static struct {
    struct jlapp app;
    struct jlx_frame shown;
    const struct jlx_model *m;
    uint32_t out[LP_OUT];                        /* what goes to the Launchpad, in order (a ring: out_r .. out_w) */
    uint8_t out_r, out_w;
    uint8_t up, init, programmer_pending, setup_pending, tries;
    uint32_t try_at, settle_at, refresh_at, page_ms, news_ms;
    uint8_t sel_seen, playing_seen, scale_seen, root_seen;
} lp;

static uint32_t lp_trk(uint32_t jt) { return jt < NTRK ? jt : TRK_DRUM; }        /* the FM-1 track of jank track jt */
static uint32_t lp_lane(uint32_t jt) { return jt >= LP_LANE0 ? jt - LP_LANE0 : LU_NO_LANE; }   /* its lane, LU_NO_LANE: a track */
static uint32_t lp_note_lane(uint32_t jt, uint32_t note)   /* the lane a drum note plays (DRUM_LANES and up: none) */
{
    return jt == TRK_DRUM ? lu_lane_of(note) : lp_lane(jt);
}
static uint32_t lp_lane_note(uint32_t l) { return l < DRUM_LANES ? LANE_NOTE_OF(l) : 60u; }   /* a lane's MIDI note (the pads' pitch) */

static void lp_init_once(void)
{
    if (lp.init)
        return;
    lp.init = 1;
    jlapp_init(&lp.app, jls_get(JLS_MAJOR));
    jlapp_set_dn2(&lp.app, 0);
}
static void lp_send(const uint8_t *b, size_t n)  /* n bytes of packets for the Launchpad (whole or not at all: a cut SysEx is lost) */
{
    size_t i;
    if (LP_OUT - (uint8_t)(lp.out_w - lp.out_r) < n / 4u)
        return;
    for (i = 0; i + 4u <= n; i += 4u)
        lp.out[lp.out_w++ % LP_OUT] = (uint32_t)b[i] | (uint32_t)b[i + 1] << 8 | (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 3] << 24;
}
static uint32_t lp_room(void) { return LP_OUT - (uint8_t)(lp.out_w - lp.out_r); }

/* ------------------------------------------------------------------ the clips page's news --- */
static void lp_clips_news(void)                  /* the patterns each track plays, has queued, holds; the scenes stored */
{
#if JLAPP_CLIPS && FELUCCA_PATTERNS
    lu_clips_t c;
    struct jlcl_track t[JLCL_TRACKS];
    uint32_t k;
    lu_clips(&c);
    for (k = 0; k < JLCL_TRACKS; k++)
        t[k] = k < NTRK ? (struct jlcl_track){c.playing[k], c.queued[k], c.has[k]} : (struct jlcl_track){JLCL_NONE, JLCL_NONE, 0};
    jlapp_on_clips(&lp.app, t, c.scenes);
#endif
}

/* ------------------------------------------------------------------ the app's acts on the FM-1 --- */
static void lp_act(const struct jlapp_act *a)
{
    uint32_t jt = a->track, k = lp_trk(jt), l;
    switch (a->kind) {
    case JLAPP_ACT_NOTE_ON:
    case JLAPP_ACT_NOTE_OFF:
    case JLAPP_ACT_TRACK_ON:
    case JLAPP_ACT_TRACK_OFF: {
        uint32_t on = a->kind == JLAPP_ACT_NOTE_ON || a->kind == JLAPP_ACT_TRACK_ON, own = a->kind >= JLAPP_ACT_TRACK_ON, note = a->note;
        if (jt >= LP_TRACKS)
            return;
        if (k == TRK_DRUM) {                     /* the drum track or a lane: the note is a lane */
            l = own ? (jt == TRK_DRUM ? 0u : lp_lane(jt)) : lp_note_lane(jt, note);
            if (l >= DRUM_LANES)
                return;                          /* (no lane for it, or the ACCENT lane: no sound) */
            note = l;
        } else if (own) {
            note = lu_track_note(k);
        }
        if (on)
            (void)lu_note_on(k, note, own ? 0u : a->vel);
        else
            (void)lu_note_off(k, note);
        return;
    }
    case JLAPP_ACT_SELECT:
        if (jt < LP_TRACKS)
            lu_select(k);
        return;
    case JLAPP_ACT_STEP:                         /* jdl_step_op: 0 clear, 1 trig, 2 note (= LU_CLEAR, LU_TRIG, LU_NOTE) */
        if (jt >= LP_TRACKS)
            return;
        if (k == TRK_DRUM) {
            l = jt == TRK_DRUM ? (a->op == LU_CLEAR && lu_lane_of(a->note) >= DRUM_LANES ? 255u : lu_lane_of(a->note)) : lp_lane(jt);
            if (l != 255u && l >= DRUM_LANES)
                return;                          /* (the ACCENT lane's steps: not from the pads yet) */
            (void)lu_step(k, a->step, a->op, l, a->vel == 0u ? 255u : a->vel);
        } else {
            (void)lu_step(k, a->step, a->op, a->note, a->vel == 0u ? 255u : a->vel);
        }
        return;
    case JLAPP_ACT_SCALE:                        /* the scale page: the selected part's SCL and ROOT */
        if (!IS_DRUMK(song.sel))
            (void)lu_scale_set(song.sel, a->note, a->vel);
        return;
    case JLAPP_ACT_MIX:                          /* enum jdl_mix_param = LU_VOL .. LU_LEVEL */
        if (jt < NTRK)
            (void)lu_mix_set(jt, a->op, a->vel);
        else if ((l = lp_lane(jt)) < DRUM_LANES && (a->op == LU_VOL || a->op == LU_LEVEL))
            lu_lane_vol_set(l, a->vel);
        else if (l < DRUM_LANES && a->op == LU_PAN)
            lu_lane_pan_set(l, a->vel);
        return;
    case JLAPP_ACT_MUTE: {                       /* the whole new mask over jank's tracks: the 4, then the lanes */
        uint32_t mask = a->note | (uint32_t)a->step << 8;
        lu_mute_set(a->op, mask & ((1u << NTRK) - 1u));
        lu_lane_mute_set(a->op, (mask >> LP_LANE0) & DRUM_LMASK);
        return;
    }
    case JLAPP_ACT_TRANSPORT:
        lu_transport(!lu_playing());
        return;
    case JLAPP_ACT_DN2_TRANSPORT:                /* the unit leads (the FM-1 always does): vel = jdl_transport_op (0 stop, 1 play, 2 pause) */
        lu_transport(a->vel == JDL_TR_PLAY);
        return;
#if JLAPP_CLIPS && FELUCCA_PATTERNS
    case JLAPP_ACT_CLIP:
        (void)lu_clip_launch(a->track, a->note, a->vel);
        lp_clips_news();                         /* (the page shows only this news: at once, not at the next pass) */
        return;
    case JLAPP_ACT_SCENE:
        (void)lu_scene(a->note);
        lp_clips_news();
        return;
#endif
    default:                                     /* RECORD, ERASE, PATTERN, CHAIN: not on the FM-1 (the app is told: lp_tell) */
        return;
    }
}

/* ------------------------------------------------------------------ the unit's news for the app --- */
static void lp_tell(void)                        /* what the FM-1 can do (once at attach) */
{
    jlapp_set_dn2(&lp.app, 1);
    jlapp_set_live(&lp.app, JDL_LIVE, 1, 1);     /* (it plays, it can be read) */
    jlapp_set_mixer_writes(&lp.app, 1);          /* (RECORD and ERASE: compiled away, jank_fm1.h) */
}
static void lp_push(void)                        /* the notes played: the lights */
{
    struct jdl_note b[8];
    size_t n = 0;
    lu_note_t e;
    while (n + 2u <= sizeof b / sizeof b[0] && lu_played(&e)) {
        if (e.flags & LUF_LANE) {                /* a drum hit: the drum track's pad (its pitch) and the lane's track */
            uint8_t nt = (uint8_t)lp_lane_note(e.note);
            b[n++] = (struct jdl_note){TRK_DRUM, e.step, nt, e.vel, (uint8_t)(e.len * 2u - 2u), e.flags};
            b[n++] = (struct jdl_note){(uint8_t)(LP_LANE0 + e.note), e.step, nt, e.vel, (uint8_t)(e.len * 2u - 2u), e.flags};
        } else {
            b[n++] = (struct jdl_note){e.track, e.step, e.note, e.vel, (uint8_t)(e.len * 2u - 2u), e.flags};
        }
        if (n + 2u > sizeof b / sizeof b[0]) {
            jlapp_on_push(&lp.app, b, n);
            n = 0;
        }
    }
    if (n)
        jlapp_on_push(&lp.app, b, n);
}
static void lp_pages(void)                       /* a step mode's pages of the current track */
{
    uint8_t want[2];
    size_t k = jlapp_step_wanted(&lp.app, want), i, s;
    uint32_t jt = lp.app.n.track;
    if (jt >= LP_TRACKS)
        return;
    for (i = 0; i < k; i++) {
        lu_page_t pg;
        struct jdl_page o;
        uint32_t lane = jt >= LP_LANE0 ? lp_lane(jt) : LU_NO_LANE;
        if (lane != LU_NO_LANE && lane >= DRUM_LANES)
            continue;                            /* (the ACCENT lane: not shown yet) */
        if (lu_page(lp_trk(jt), want[i], lane, &pg) != LU_OK)
            continue;
        o.track = (uint8_t)jt, o.page = pg.page, o.pages = pg.pages, o.length = pg.len;
        for (s = 0; s < JDL_PAGE_STEPS; s++) {
            const lu_step_t *x = &pg.s[s];
            uint8_t nt = lp_trk(jt) == TRK_DRUM ? (uint8_t)lp_lane_note(x->note) : x->note;
            o.step[s] = (struct jdl_step){x->trig, nt, x->vel, (uint8_t)(x->len * 2u - 2u), x->notes};   /* (len: eighths -> the DN2's code, 14 = 1 step) */
        }
        jlapp_on_page(&lp.app, &o);
    }
}
static void lp_news(uint32_t now)
{
    if (now - lp.news_ms < LP_NEWS_MS)
        return;
    lp.news_ms = now;
    if (jlapp_mixer_wanted(&lp.app)) {
        lu_mixer_t mx;
        struct jdl_mixer o;
        uint32_t k, l;
        lu_mixer(&mx);
        memset(&o, 0, sizeof o);
        o.mute = mx.mute | lu_lane_mute_mask() << LP_LANE0;
        o.solo = mx.solo | lu_lane_solo_mask() << LP_LANE0;
        o.active = mx.sel;
        for (k = 0; k < NTRK; k++)
            o.t[k] = (struct jdl_mix_track){mx.v[k][LU_VOL], mx.v[k][LU_PAN], mx.v[k][LU_DEL], mx.v[k][LU_REV], mx.v[k][LU_CHR], mx.v[k][LU_LEVEL]};
        for (l = 0; l < DRUM_LANES && LP_LANE0 + l < JDL_TRACKS; l++) {
            uint8_t v = (uint8_t)lu_lane_vol(l);
            o.t[LP_LANE0 + l] = (struct jdl_mix_track){v, (uint8_t)lu_lane_pan(l), 0, 0, 0, v};
        }
        jlapp_on_mixer(&lp.app, &o);
    }
    if (!IS_DRUMK(song.sel) && (lu_scale(song.sel) != lp.scale_seen || lu_root(song.sel) != lp.root_seen)) {
        lp.scale_seen = (uint8_t)lu_scale(song.sel);
        lp.root_seen = (uint8_t)lu_root(song.sel);
        jlapp_on_dn2_scale(&lp.app, lp.scale_seen, 1, lp.root_seen);   /* (the FM-1's change: the Launchpad follows) */
    }
    if (jlapp_following(&lp.app) && lu_selected() != lp.sel_seen)
        jlapp_on_active_track(&lp.app, lu_selected());
    lp.sel_seen = (uint8_t)lu_selected();
    lp_clips_news();
}
static void lp_transport_news(void)              /* the start / stop button, and the step modes' playhead (the selected track's 16ths) */
{
    uint32_t playing = lu_playing(), k = lp_trk(lp.app.n.track < LP_TRACKS ? lp.app.n.track : 0u);
    if (playing != lp.playing_seen) {
        lp.playing_seen = (uint8_t)playing;
        jlapp_set_transport(&lp.app, (int)playing);
    }
    jlapp_set_beat(&lp.app, (int)(playing && trk[k].seq_abs != SEQ_NONE), playing && trk[k].seq_abs != SEQ_NONE ? trk[k].seq_abs : 0u);
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
    lp_init_once();
    lp.m = jlx_model_find(name);
    if (!lp.m)
        lp.m = &jlx_models[0];
    lp.app.no_vel = (uint8_t)!lp.m->velocity;
    lp.up = 1;
    lp.programmer_pending = 1;
    lp.setup_pending = 0;
    lp.tries = 0;
    lp.try_at = fm1_ms + LP_ATTACH_MS;
    lp.playing_seen = 2;                         /* (told at the first step) */
    lp.scale_seen = lp.root_seen = 255;
    lp.out_r = lp.out_w;
    lp_tell();
    lu_tap(1);
}
static void lp_detach(void)
{
    struct jlapp_act off[JLAPP_PADS + JLN_TRACKS + 1u];
    size_t i, n;
    if (!lp.up)
        return;
    lp.up = 0;
    lp.programmer_pending = lp.setup_pending = 0;
    for (i = 0, n = jlapp_release_acts(&lp.app, off, sizeof off / sizeof off[0]); i < n; i++)
        lp_act(&off[i]);
    jlapp_set_dn2(&lp.app, 0);
    lp.out_r = lp.out_w;
    lu_detach();
}
static void lp_in(uint32_t pkt)
{
    uint8_t p[4] = {(uint8_t)pkt, (uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
    const uint8_t msg[3] = {p[1], p[2], p[3]};
    struct jlapp_act acts[JLAPP_ACTS_MAX];
    struct jlx_ev ev;
    uint32_t cin = p[0] & 0x0Fu;
    size_t i, made;
    int mode;
    if (!lp.up)
        return;
    if (jlx_programmer_ack(lp.m, p, &mode)) {   /* its answer to Programmer mode (or to the mode query) */
        if (mode && lp.programmer_pending) {
            lp.programmer_pending = 0;
            lp.setup_pending = 1;
        } else if (!mode && !lp.programmer_pending) {   /* it fell into Live mode: Programmer mode again */
            lp.programmer_pending = 1;
            lp.tries = 0;
            lp.try_at = fm1_ms;
        }
        return;
    }
    if ((cin != CIN_NOTE_ON && cin != CIN_NOTE_OFF && cin != CIN_CC) || !jlx_decode(msg, &ev))
        return;
    jlapp_tick(&lp.app, fm1_ms);
    made = jlapp_on_lpx_acts(&lp.app, &ev, acts, JLAPP_ACTS_MAX);
    for (i = 0; i < made; i++)
        lp_act(&acts[i]);
    lp_messages();
}
static void lp_lights(uint32_t now)              /* the Launchpad's side: its mode, then the lights that changed */
{
    uint8_t b[4u * (LP_BATCH > 24u ? LP_BATCH : 24u)];
    struct jlx_frame want;
    size_t n;
    if (lp.programmer_pending) {                 /* the whole message or none of it */
        if ((int32_t)(now - lp.try_at) < 0 || lp_room() < jlx_sysex(lp.m, lp.m->start, NULL) / 4u)
            return;
        lp_send(b, jlx_sysex(lp.m, lp.m->start, b));
        lp.tries++;
        lp.try_at = now + LP_ACK_MS;
        if (!lp.m->ack || lp.tries >= LP_ACK_TRIES) {   /* it answers nothing, or never answered: the setup and the lights go anyway */
            lp.programmer_pending = 0;
            lp.setup_pending = 1;
        }
        return;
    }
    if (lp.setup_pending) {
        if (lp_room() < jlx_sysex(lp.m, lp.m->setup, NULL) / 4u)
            return;
        if ((n = jlx_sysex(lp.m, lp.m->setup, b)) != 0)
            lp_send(b, n);
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
    if ((n = jlx_diff(lp.m, &lp.shown, &want, b, LP_BATCH)) != 0)
        lp_send(b, n * 4u);
}
static void lp_step(uint32_t now)
{
    struct jlapp_act slides[JLMX_SLIDES];
    size_t i, made;
    lu_poll();
    if (!lp.up)
        return;
    jlapp_tick(&lp.app, now);
    lp_push();
    lp_transport_news();
    if (now - lp.page_ms >= LP_PAGE_MS || jlapp_step_urgent(&lp.app)) {
        lp.page_ms = now;
        lp_pages();
    }
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
