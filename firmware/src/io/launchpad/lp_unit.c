/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 as the Launchpad's unit (FELUCCA_LAUNCHPAD; jank's Launchpad app, third_party/jank, played the Digitone II through its live
 * control; on the FM-1 the app and the synth are one, so its requests are calls here). Main loop only, except the tap
 * (seq/live_tap.h: the notes the sequencer, the arp and the rolls play, from the audio interrupt).
 *
 *   play      lu_note_on / lu_note_off / lu_track_note / lu_all_off: through the editor's note ring (seq/ednote.c: the keys'
 *             path, scale, chord, arp, REC); the drum track's note is its lane. At most LU_HELD held; LU_FULL when the
 *             ring has no room (an off waits and goes at the next lu_poll)
 *   steps     lu_page reads 16 steps of a track (the drum track: one lane, or any); lu_step writes one, an undo level a track
 *             within 800 ms (as the panel's)
 *   the rest  the mixer (level, pan, the three sends, mute and solo), the scale and root, the selected track, the transport
 *             (the panel's PLAY / STOP request), the patterns as clips and the scenes (the patterns page's launch)
 * The writes go where the panel's and the web editor's go, so the screen and the editor's live sync follow. */
#include "../../seq/live_tap.h"
#if FELUCCA_LAUNCHPAD
enum { LU_OK = 0, LU_ARGS = 2, LU_FULL = 5 };
enum { LU_SYNTH = 0, LU_DRUM = 1 };
#define LU_HELD 16u                          /* notes held from the pads */
#define LU_PAGE 16u                          /* steps a page */
#define LU_NO_LANE 255u
#define LU_FREE_STEP 255u                    /* a note played with the transport stopped (the arp, a roll) */
#define LUF_LANE 0x10u                       /* lu_note_t flags: the drum track, the note is the lane */
#define LUF_FREE 0x20u                       /* not a step of the transport */

typedef struct { uint8_t trig, note, vel, len, notes; } lu_step_t;   /* len: eighths of a step */
typedef struct {
    uint8_t track, page, pages, len;         /* len: the track's LEN in steps */
    uint8_t play_step, playing, lane;        /* play_step 255: stopped; lane LU_NO_LANE: any */
    lu_step_t s[LU_PAGE];
} lu_page_t;
typedef struct {
    uint32_t mute, solo;                     /* bit t: track t */
    uint8_t sel;
    uint8_t v[NTRK][6];                      /* VOL PAN DEL REV CHR LEVEL, 0..127 (VOL and LEVEL: the track's one level; PAN 64 the centre) */
} lu_mixer_t;
typedef struct { uint8_t track, step, note, vel, len, flags; } lu_note_t;
#if FELUCCA_PATTERNS
#define LU_SLOTS PAT_N
#define LU_SCENES SEC_IDS
typedef struct {
    uint8_t playing[NTRK], queued[NTRK], when[NTRK];   /* a slot 0..LU_SLOTS-1, 255 none, 254 a stop queued */
    uint16_t has[NTRK];                      /* bit s: slot s holds a pattern */
    uint32_t scenes;                         /* bit n: scene n stored */
} lu_clips_t;
#endif

typedef struct { uint8_t trk, note, off; } lu_held_t;   /* off: to end, the ring had no room yet */
static struct {
    lu_held_t held[LU_HELD];
    uint8_t nheld;
    uint32_t dropped;                        /* notes the tap queued and nobody took (the push off) */
} lu;

static uint32_t lu_kind(uint32_t k) { return IS_DRUMK(k) ? LU_DRUM : LU_SYNTH; }
static uint32_t lu_lanes(uint32_t k) { return IS_DRUMK(k) ? DRUM_LANES : 0u; }
static int16_t *lu_level(uint32_t k) { return IS_DRUMK(k) ? &song.g[G_DRLVL] : &trk[k].p[P_LEVEL]; }
static uint32_t lu_gate8(const track_t *t) { uint32_t g = (uint32_t)clamp(t->p[P_SGATE], 0, 127) / 16u; return g ? g : 1u; }
static uint32_t lu_pop(uint32_t m) { uint32_t n = 0; for (; m; m &= m - 1u) n++; return n; }
static uint32_t lu_low(uint32_t m) { uint32_t n = 0; while (m && !(m & 1u)) m >>= 1, n++; return n; }
static void lu_mark(const track_t *t)        /* an undo level, as ed_clip.c clip_mark (one a track within 800 ms) */
{
    static uint32_t last_ms, sess;
    static uint8_t last_trk = 0xFF;
    uint32_t k = trk_index(t) % NTRK;
    if (last_trk != k || fm1_ms - last_ms > 800u || !sess)
        sess = (undo_sess += 4u) | 3u;
    last_trk = (uint8_t)k;
    last_ms = fm1_ms;
    undo_mark(t, sess);
}

/* ------------------------------------------------------------------ play --- */
enum { LU_ON, LU_OFF };
static int lu_ring(uint32_t op, uint32_t k, uint32_t note, uint32_t vel)   /* 1: queued for the audio interrupt */
{
#ifdef EDN_Q                                 /* (seq/ednote.c was built: its ring) */
    return ednote_push(op == LU_ON ? EDN_OP_ON : EDN_OP_OFF, k, note, vel);
#else
    (void)op, (void)k, (void)note, (void)vel;
    return 0;
#endif
}
static void lu_flush(void)                   /* the offs due go to the ring; one it had no room for stays for the next pass */
{
    uint32_t i, j = 0;
    for (i = 0; i < lu.nheld; i++)
        if (!lu.held[i].off || !lu_ring(LU_OFF, lu.held[i].trk, lu.held[i].note, 0u))
            lu.held[j++] = lu.held[i];
    lu.nheld = (uint8_t)j;
}
static void lu_all_off(uint32_t k)           /* the notes the pads hold on track k end (NTRK and up: every track) */
{
    uint32_t i;
    for (i = 0; i < lu.nheld; i++)
        if (k >= NTRK || lu.held[i].trk == k)
            lu.held[i].off = 1;
    lu_flush();
}
static int lu_note_on(uint32_t k, uint32_t note, uint32_t vel)   /* vel 0: 100; the drum track: note = the lane */
{
    if (k >= NTRK || note > 127u || vel > 127u || (IS_DRUMK(k) && note >= DRUM_LANES))
        return LU_ARGS;
    if (lu.nheld >= LU_HELD || !lu_ring(LU_ON, k, note, vel ? vel : 100u))
        return LU_FULL;
    lu.held[lu.nheld++] = (lu_held_t){(uint8_t)k, (uint8_t)note, 0};
    return LU_OK;
}
static int lu_note_off(uint32_t k, uint32_t note)   /* only a note the pads hold: a key the user holds is left alone */
{
    uint32_t i;
    for (i = 0; i < lu.nheld; i++)
        if (lu.held[i].trk == k && lu.held[i].note == note && !lu.held[i].off) {
            lu.held[i].off = 1;
            lu_flush();
            return LU_OK;
        }
    return LU_ARGS;
}
static uint32_t lu_track_note(uint32_t k) { return IS_DRUMK(k) ? 0u : 60u; }   /* a track's own note (a track pad): a part's C, the first lane */

/* ------------------------------------------------------------------ steps --- */
static int lu_page(uint32_t k, uint32_t page, uint32_t lane, lu_page_t *o)   /* page 255: the one playing (0 stopped) */
{
    const track_t *t;
    uint32_t len, pages, i, play;
    if (k >= NTRK || (lane != LU_NO_LANE && (!IS_DRUMK(k) || lane >= DRUM_LANES)))
        return LU_ARGS;
    t = &trk[k];
    len = trk_len(t);
    pages = (len + LU_PAGE - 1u) / LU_PAGE;
    play = song.playing && t->seq_abs != SEQ_NONE;
    if (page == 255u)
        page = play ? t->seq_idx / LU_PAGE : 0u;
    if (page >= pages)
        return LU_ARGS;
    *o = (lu_page_t){(uint8_t)k, (uint8_t)page, (uint8_t)pages, (uint8_t)len, play ? (uint8_t)t->seq_idx : 255u, song.playing ? 1u : 0u, (uint8_t)lane, {{0}}};
    for (i = 0; i < LU_PAGE; i++) {
        uint32_t x = page * LU_PAGE + i;
        lu_step_t *s = &o->s[i];
        if (x >= NSTEP)
            continue;
        if (IS_DRUMK(k)) {
            const dstep_t *d = &t->dstep[x];
            uint32_t m = dstep_mask(d), l = lane == LU_NO_LANE ? lu_low(m) : lane, on = lane == LU_NO_LANE ? m != 0u : (m >> lane & 1u);
            *s = (lu_step_t){(uint8_t)on, (uint8_t)(l < DRUM_LANES ? l : 0u), (uint8_t)(on && l < DRUM_LANES ? lvl_vel(dstep_lvl(d, l), 100) : 100u), 8, (uint8_t)lu_pop(m)};
        } else {
            const step_t *st = &t->step[x];
            *s = (lu_step_t){(uint8_t)(st->time == ST_NOTE && st->n), (uint8_t)(st->n ? st->note[0] & 127u : 60u), (uint8_t)(st->vel ? st->vel & 127u : 100u),
                             (uint8_t)lu_gate8(t), st->n};
        }
    }
    return LU_OK;
}
enum { LU_CLEAR, LU_TRIG, LU_NOTE };
/* op LU_CLEAR (the drum track: lane `note`, 255 every lane), LU_TRIG (note 60, velocity 100 when the step has none), LU_NOTE (note,
 * vel; vel 255 keeps the step's, 100 when none) */
static int lu_step(uint32_t k, uint32_t x, uint32_t op, uint32_t note, uint32_t vel)
{
    track_t *t;
    if (k >= NTRK || op > LU_NOTE || (vel > 127u && vel != 255u) || vel == 0u)
        return LU_ARGS;
    t = &trk[k];
    if (x >= trk_len(t) || x >= NSTEP)
        return LU_ARGS;
    if (IS_DRUMK(k) ? (note != 255u && note >= DRUM_LANES) || (op != LU_CLEAR && note == 255u) : (op && note > 127u))
        return LU_ARGS;
    lu_mark(t);
    fm1_irq_off();
    if (IS_DRUMK(k)) {
        dstep_t *d = &t->dstep[x];
        uint32_t l;
        if (op == LU_CLEAR) {
            for (l = 0; l < DRUM_LANES; l++)
                if (note == 255u || l == note)
                    dstep_clr(d, l);
        } else {
            dstep_set(d, note, vel == 255u ? LV_NORM : vel_lvl(vel), 0);
        }
    } else {
        step_t *s = &t->step[x];
        if (op == LU_CLEAR) {
            s->n = 0;
            s->time = ST_REST;
        } else {
            if (op == LU_NOTE || !s->n)
                s->note[0] = (uint8_t)(op == LU_NOTE ? note : 60u);
            if (!s->n)
                s->n = 1;
            s->time = ST_NOTE;
            if (vel != 255u)
                s->vel = (uint8_t)vel;
            else if (!s->vel)
                s->vel = 100;
        }
    }
    fm1_irq_on();
    ui.force = 1;
    return LU_OK;
}

/* ------------------------------------------------------------------ the mixer --- */
enum { LU_VOL, LU_PAN, LU_DEL, LU_REV, LU_CHR, LU_LEVEL };
static uint32_t lu_mix_val(uint32_t k, uint32_t p)
{
    const track_t *t = &trk[k];
    switch (p) {
    case LU_PAN: return (uint32_t)clamp(t->p[P_PAN] + 64, 0, 127);
    case LU_DEL: return (uint32_t)clamp(t->p[P_DLY], 0, 127);
    case LU_REV: return (uint32_t)clamp(t->p[P_REV], 0, 127);
    case LU_CHR: return (uint32_t)clamp(t->p[P_CHOR], 0, 127);
    default: return (uint32_t)clamp(*lu_level(k), 0, 127);
    }
}
static void lu_mixer(lu_mixer_t *o)
{
    uint32_t k, p;
    o->mute = o->solo = 0;
    for (k = 0; k < NTRK; k++) {
        o->mute |= (uint32_t)(trk[k].p[P_MUTE] != 0) << k;
        for (p = 0; p < 6u; p++)
            o->v[k][p] = (uint8_t)lu_mix_val(k, p);
    }
    o->solo = song.solo & ((1u << NTRK) - 1u);
    o->sel = song.sel;
}
static int lu_mix_set(uint32_t k, uint32_t p, uint32_t v)
{
    if (k >= NTRK || p > LU_LEVEL || v > 127u)
        return LU_ARGS;
    switch (p) {
    case LU_PAN: trk[k].p[P_PAN] = (int16_t)((int32_t)v - 64); break;
    case LU_DEL: trk[k].p[P_DLY] = (int16_t)v; break;
    case LU_REV: trk[k].p[P_REV] = (int16_t)v; break;
    case LU_CHR: trk[k].p[P_CHOR] = (int16_t)v; break;
    default: *lu_level(k) = (int16_t)v; break;
    }
    ui.force = 1;
    return LU_OK;
}
static void lu_mute_set(uint32_t solo, uint32_t mask)   /* every track's mute (solo 0) or solo (1) as mask says */
{
    uint32_t k;
    mask &= (1u << NTRK) - 1u;
    if (!solo) {
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_MUTE] = (int16_t)(mask >> k & 1u);
    } else {
        song.solo = (uint8_t)((song.solo & ~((1u << NTRK) - 1u)) | mask);
    }
    ui.force = 1;
}

/* ------------------------------------------------------------------ the drum lanes as tracks --- */
/* jank presents the FM-1 as 15 tracks: 0..2 the parts, 3 the drum track, 4..14 its lanes 0..10 (10 the ACCENT lane: no sound,
 * no mute, no pan). A lane's mute, solo and pan are the mixer's lane rows (drums/drum_mix.c); its VOL is its sound's LEVEL
 * edit, -24..+6 dB (drums/drum_edit.c DE_LEVEL), 0..127 here. The lanes share the drum track's length, DIV and patterns. */
#define LU_DB_MIN (-24)
#define LU_DB_MAX 6
static uint32_t lu_lane_mute_mask(void) { return dlm_mute & DRUM_LMASK; }
static uint32_t lu_lane_solo_mask(void) { return dlm_solo & DRUM_LMASK; }
static void lu_lane_mute_set(uint32_t solo, uint32_t mask)   /* every lane's mute (solo 0) or solo (1) as mask says */
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++) {
        if (solo)
            dlm_set_solo(l, (int)(mask >> l & 1u));
        else
            dlm_set_mute(l, (int)(mask >> l & 1u));
    }
    ui.force = 1;
}
static uint32_t lu_lane_pan(uint32_t l) { return l < DRUM_LANES ? (uint32_t)clamp(dlm_pan[l] + 64, 0, 127) : 64u; }
static void lu_lane_pan_set(uint32_t l, uint32_t v)
{
    if (l < DRUM_LANES && v <= 127u) {
        dlm_set_pan(l, (int32_t)v - 64);
        ui.force = 1;
    }
}
static uint32_t lu_lane_vol(uint32_t l)      /* its LEVEL edit as 0..127 (100: 0 dB, as the kit) */
{
#if FELUCCA_DRUM_EDIT
    if (l < DRUM_LANES)
        return (uint32_t)((dl.ofs[l][DE_LEVEL] - LU_DB_MIN) * 127 / (LU_DB_MAX - LU_DB_MIN));
#endif
    (void)l;
    return (uint32_t)((0 - LU_DB_MIN) * 127 / (LU_DB_MAX - LU_DB_MIN));
}
static void lu_lane_vol_set(uint32_t l, uint32_t v)
{
#if FELUCCA_DRUM_EDIT
    if (l < DRUM_LANES && v <= 127u) {
        dl.ofs[l][DE_LEVEL] = (int8_t)(LU_DB_MIN + ((int32_t)v * (LU_DB_MAX - LU_DB_MIN) + 63) / 127);
        ui.force = 1;
    }
#else
    (void)l, (void)v;
#endif
}
static uint32_t lu_lane_of(uint32_t note) { return lane_of_note(note); }   /* a MIDI note on the drum track: its lane (DLANE_NONE: none) */

/* ------------------------------------------------------------------ scale, track, transport --- */
static uint32_t lu_scale(uint32_t k) { return (uint32_t)clamp(trk[k % NTRK].p[P_SCALE], 0, NSCALES - 1); }
static uint32_t lu_root(uint32_t k) { return (uint32_t)clamp(trk[k % NTRK].p[P_ROOT], 0, 11); }
static int lu_scale_set(uint32_t k, uint32_t scale, uint32_t root)   /* root 255: unchanged */
{
    if (k >= NTRK || scale >= NSCALES || (root > 11u && root != 255u))
        return LU_ARGS;
    trk[k].p[P_SCALE] = (int16_t)scale;
    if (root != 255u)
        trk[k].p[P_ROOT] = (int16_t)root;
    ui.force = 1;
    return LU_OK;
}
static uint32_t lu_selected(void) { return song.sel; }
static void lu_select(uint32_t k)
{
    if (k < NTRK && k != song.sel)
        track_select(k);
}
static uint32_t lu_playing(void) { return song.playing != 0; }
static void lu_transport(uint32_t play)      /* the panel's PLAY / STOP request */
{
    if (play && !song.playing)
        transport_req = 1;
    else if (!play && song.playing)
        transport_req = 2;
}

/* ------------------------------------------------------------------ clips and scenes --- */
#if FELUCCA_PATTERNS
static uint8_t lu_slot(uint32_t v) { return v < PAT_N ? (uint8_t)v : v == PAT_STOP ? 254u : 255u; }   /* (jank's JLCL_STOP, JLCL_NONE) */
static void lu_clips(lu_clips_t *o)
{
    uint32_t k, s;
    for (k = 0; k < NTRK; k++) {
        o->has[k] = 0;
        for (s = 0; s < PAT_N; s++)
            o->has[k] |= (uint16_t)((pat_has(k, s) != 0) << s);
        o->playing[k] = lu_slot(pat_cur[k]);
        o->queued[k] = lu_slot(pat_req[k]);
        o->when[k] = pat_when[k];
    }
    o->scenes = 0;
    for (s = 0; s < SEC_IDS && s < 32u; s++)
        o->scenes |= (uint32_t)(project_used(s) != 0) << s;
}
static int lu_clip_launch(uint32_t k, uint32_t slot, uint32_t when)   /* slot 255: stop; when 0 the pattern's end, 1 the next bar, 2 now */
{
    if (k >= NTRK || (slot >= PAT_N && slot != 255u) || when > PW_NOW)
        return LU_ARGS;
    pat_launch(k, slot < PAT_N ? slot : PAT_NONE, when);
    return LU_OK;
}
static int lu_scene(uint32_t n)              /* cued while playing, loaded when stopped */
{
    if (n >= SEC_IDS || !project_used(n) || (song.playing && !section_cue(n)))
        return LU_ARGS;
    if (!song.playing)
        section_load(n);
    return LU_OK;
}
#endif

/* ------------------------------------------------------------------ the notes played (the lights) --- */
static void lu_tap(int on)                   /* the tap on (the app shows what plays) or off (what waits dropped) */
{
    lv_tap_q.on = (uint8_t)(on != 0);
    if (!on)
        lu.dropped += lv_tap_drop();
}
static int lu_played(lu_note_t *o)           /* the oldest note played since; 0 when none */
{
    lv_ev_t e;
    while (lv_tap_get(&e)) {
        uint32_t k = e.trk % NTRK, note = e.note, fl = e.step == LV_NO_STEP ? LUF_FREE : 0u;
        if (IS_DRUMK(k)) {
            uint32_t l = lane_of_note(note);
            if (l >= DRUM_LANES)
                continue;                   /* (a note no lane plays now: the kit changed under it) */
            note = l;
            fl |= LUF_LANE;
        }
        *o = (lu_note_t){(uint8_t)k, e.step, (uint8_t)note, e.vel, (uint8_t)(IS_DRUMK(k) ? 8u : lu_gate8(&trk[k])), (uint8_t)fl};
        return 1;
    }
    return 0;
}
static void lu_poll(void) { if (lu.nheld) lu_flush(); }   /* every main-loop pass: offs the ring had no room for */
static void lu_detach(void)                  /* the Launchpad went away: its notes end, the tap stops */
{
    lu_all_off(NTRK);
    lu_tap(0);
}
#endif
