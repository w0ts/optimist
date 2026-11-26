/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 as the Launchpad's unit (FELUCCA_LAUNCHPAD; jank's Launchpad core, third_party/jank, played the Digitone II through its
 * live control; on the FM-1 the app and the synth are one, so its requests are calls here, its news filled in jank's own structs).
 * Main loop only, except the tap (seq/live_tap.h: the notes the sequencer, the arp and the rolls play, from the audio interrupt).
 * Included by launchpad.c after jank's headers.
 *
 * jank's tracks (owner 2026-11-26, "4 + 11"): 0..2 the parts, 3 the drum track as a whole (a note is the lane whose instrument it
 * is, as the FM-1's MIDI input plays it), 4..14 its lanes 0..10 (14 the ACCENT lane: no sound, no mute, no pan): a lane track's
 * notes are its hits whatever their pitch; its steps, mute, solo, pan and VOL (its sound's LEVEL edit, -24..+6 dB) are the lane's;
 * its length and its clips the drum track's. The writes go where the panel's and the web editor's go (an undo level a track for
 * the steps), so the screen and the editor's live sync follow. */
#include "../../seq/live_tap.h"
#if FELUCCA_LAUNCHPAD
#define LU_HELD 16u                              /* notes held from the pads */
#define LU_LANE0 NTRK                            /* jank's track of lane 0 */
#define LU_TRACKS (NTRK + DRUM_SLANES)
#define LU_NONE 255u
#define LU_DB_MIN (-24)
#define LU_DB_MAX 6
_Static_assert(LU_TRACKS <= JLN_TRACKS && LU_TRACKS <= JDL_TRACKS, "jank's core has a track for each of the FM-1's tracks and lanes");

static struct {
    struct { uint8_t trk, note, off; } held[LU_HELD];   /* off: to end, the ring had no room yet */
    uint8_t nheld;
    uint32_t dropped;                            /* notes the tap queued and nobody took */
} lu;

static uint32_t lu_trk(uint32_t jt) { return jt < NTRK ? jt : TRK_DRUM; }             /* the FM-1 track of jank's track jt */
static uint32_t lu_lane(uint32_t jt) { return jt >= LU_LANE0 ? jt - LU_LANE0 : LU_NONE; }   /* its lane, LU_NONE: a track */
static uint32_t lu_note_of(uint32_t l) { return l < DRUM_LANES ? LANE_NOTE_OF(l) : 60u; }  /* a lane's MIDI note (the pads' pitch) */
static uint32_t lu_len(uint32_t k)               /* a note's length as the DN2's LEN code (14 = a step): the track's GATE, a hit a step */
{
    uint32_t g = IS_DRUMK(k) ? 8u : (uint32_t)clamp(trk[k].p[P_SGATE], 0, 127) / 16u;
    return (g ? g : 1u) * 2u - 2u;               /* (eighths of a step e: code 2e - 2) */
}
static void lu_mark(const track_t *t)            /* an undo level, as ed_clip.c clip_mark (one a track within 800 ms) */
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

/* ------------------------------------------------------------------ play: jank's track jt, its note (pitch, or a lane's) --- */
enum { LU_ON, LU_OFF };
static int lu_ring(uint32_t op, uint32_t k, uint32_t note, uint32_t vel)   /* 1: queued for the audio interrupt */
{
#ifdef EDN_Q                                     /* (seq/ednote.c was built: its ring) */
    return ednote_push(op == LU_ON ? EDN_OP_ON : EDN_OP_OFF, k, note, vel);
#else
    (void)op, (void)k, (void)note, (void)vel;
    return 0;
#endif
}
static void lu_flush(void)                       /* the offs due go to the ring; one it had no room for stays for the next pass */
{
    uint32_t i, j = 0;
    for (i = 0; i < lu.nheld; i++)
        if (!lu.held[i].off || !lu_ring(LU_OFF, lu.held[i].trk, lu.held[i].note, 0u))
            lu.held[j++] = lu.held[i];
    lu.nheld = (uint8_t)j;
}
static void lu_all_off(void)
{
    uint32_t i;
    for (i = 0; i < lu.nheld; i++)
        lu.held[i].off = 1;
    lu_flush();
}
/* what a press of jank's track jt plays: the FM-1 track and the note (a lane on the drum track); 0 when nothing (no lane has that
 * note, the ACCENT lane). own: the track's own note (a track pad) */
static int lu_target(uint32_t jt, uint32_t note, int own, uint32_t *k, uint32_t *n)
{
    *k = lu_trk(jt);
    if (jt >= LU_TRACKS)
        return 0;
    if (*k != TRK_DRUM)
        *n = own ? 60u : note;
    else
        *n = jt >= LU_LANE0 ? lu_lane(jt) : own ? 0u : lane_of_note(note);
    return *k != TRK_DRUM || *n < DRUM_LANES;
}
static void lu_play(uint32_t jt, uint32_t note, uint32_t vel, int on, int own)   /* vel 0: 100 */
{
    uint32_t k, n, i;
    if (!lu_target(jt, note, own, &k, &n))
        return;
    if (!on) {                                   /* only a note the pads hold: a key the user holds is left alone */
        for (i = 0; i < lu.nheld; i++)
            if (lu.held[i].trk == k && lu.held[i].note == n && !lu.held[i].off) {
                lu.held[i].off = 1;
                lu_flush();
                return;
            }
    } else if (n <= 127u && lu.nheld < LU_HELD && lu_ring(LU_ON, k, n, vel && !own ? vel : 100u)) {
        lu.held[lu.nheld].trk = (uint8_t)k, lu.held[lu.nheld].note = (uint8_t)n, lu.held[lu.nheld++].off = 0;
    }
}

/* ------------------------------------------------------------------ steps --- */
static int lu_page(uint32_t jt, uint32_t page, struct jdl_page *o)   /* a page of 16 steps of jank's track jt; 0 when none */
{
    uint32_t k = lu_trk(jt), lane = lu_lane(jt), len, pages, i;
    const track_t *t = &trk[k];
    if (jt >= LU_TRACKS || (lane != LU_NONE && lane >= DRUM_LANES))
        return 0;                                /* (the ACCENT lane's steps: not shown yet) */
    len = trk_len(t);
    pages = (len + JDL_PAGE_STEPS - 1u) / JDL_PAGE_STEPS;
    if (page >= pages)
        return 0;
    o->track = (uint8_t)jt, o->page = (uint8_t)page, o->pages = (uint8_t)pages, o->length = (uint8_t)len;
    for (i = 0; i < JDL_PAGE_STEPS; i++) {
        uint32_t x = page * JDL_PAGE_STEPS + i;
        struct jdl_step *s = &o->step[i];
        memset(s, 0, sizeof *s);
        if (x >= NSTEP)
            continue;
        s->len = (uint8_t)lu_len(k);
        if (IS_DRUMK(k)) {
            const dstep_t *d = &t->dstep[x];
            uint32_t m = dstep_mask(d), l = lane;
            if (l == LU_NONE)                    /* (the drum track as a whole: any hit, the lowest lane's note) */
                for (l = 0; l < DRUM_LANES && !(m >> l & 1u); l++)
                    ;
            s->trig = (uint8_t)(l < DRUM_LANES && (m >> l & 1u));
            s->note = (uint8_t)lu_note_of(l);
            s->vel = (uint8_t)(s->trig ? lvl_vel(dstep_lvl(d, l), 100) : 100u);
            for (; m; m &= m - 1u)
                s->notes++;
        } else {
            const step_t *st = &t->step[x];
            s->trig = (uint8_t)(st->time == ST_NOTE && st->n);
            s->note = (uint8_t)(st->n ? st->note[0] & 127u : 60u);
            s->vel = (uint8_t)(st->vel ? st->vel & 127u : 100u);
            s->notes = st->n;
        }
    }
    return 1;
}
enum { LU_CLEAR, LU_TRIG, LU_NOTE };              /* jank's jdl_step_op */
/* a step of jank's track jt: LU_CLEAR (the drum track as a whole: the lane of `note`, every lane when it has none), LU_TRIG (note 60,
 * velocity 100 when the step has none), LU_NOTE (note, vel; vel 0 / 255 keeps the step's, 100 when none) */
static void lu_step(uint32_t jt, uint32_t x, uint32_t op, uint32_t note, uint32_t vel)
{
    uint32_t k = lu_trk(jt), l = lu_lane(jt);
    track_t *t = &trk[k];
    if (jt >= LU_TRACKS || op > LU_NOTE || x >= trk_len(t) || x >= NSTEP)
        return;
    if (IS_DRUMK(k)) {
        if (l == LU_NONE)
            l = lane_of_note(note);              /* (DLANE_NONE: a clear takes every lane, a set nothing) */
        if (l >= DRUM_LANES && (op != LU_CLEAR || jt >= LU_LANE0))
            return;
    }
    lu_mark(t);
    fm1_irq_off();
    if (IS_DRUMK(k)) {
        dstep_t *d = &t->dstep[x];
        uint32_t i;
        if (op != LU_CLEAR)
            dstep_set(d, l, vel == 0u || vel > 127u ? LV_NORM : vel_lvl(vel), 0);
        else
            for (i = 0; i < DRUM_LANES; i++)
                if (l >= DRUM_LANES || i == l)
                    dstep_clr(d, i);
    } else {
        step_t *s = &t->step[x];
        if (op == LU_CLEAR) {
            s->n = 0;
            s->time = ST_REST;
        } else {
            if (op == LU_NOTE || !s->n)
                s->note[0] = (uint8_t)(op == LU_NOTE ? note & 127u : 60u);
            if (!s->n)
                s->n = 1;
            s->time = ST_NOTE;
            if (vel && vel <= 127u)
                s->vel = (uint8_t)vel;
            else if (!s->vel)
                s->vel = 100;
        }
    }
    fm1_irq_on();
    ui.force = 1;
}

/* ------------------------------------------------------------------ the mixer: jank's VOL PAN DEL REV CHR LEVEL --- */
static int16_t *lu_slot(uint32_t jt, uint32_t p)  /* the value a fader of jank's track jt sets; NULL: none (a lane's sends) */
{
    static const uint8_t ID[6] = {P_LEVEL, P_PAN, P_DLY, P_REV, P_CHOR, P_LEVEL};
    uint32_t l = lu_lane(jt);
    if (p > 5u || jt >= LU_TRACKS)
        return NULL;
    if (jt < NTRK)
        return IS_DRUMK(jt) && ID[p] == P_LEVEL ? &song.g[G_DRLVL] : &trk[jt].p[ID[p]];
#if FELUCCA_DRUM_EDIT
    static int16_t lane_vol;                     /* (a lane's VOL: a mark only; lu_mix_get / lu_mix_set convert its dB) */
    if (l < DRUM_LANES && ID[p] == P_LEVEL)
        return &lane_vol;
#endif
    return NULL;
}
static uint32_t lu_mix_get(uint32_t jt, uint32_t p)
{
    uint32_t l = lu_lane(jt);
    const int16_t *v = lu_slot(jt, p);
    if (l < DRUM_LANES && p == 1u)
        return (uint32_t)clamp(dlm_pan[l] + 64, 0, 127);
#if FELUCCA_DRUM_EDIT
    if (l < DRUM_LANES && v)
        return (uint32_t)((dl.ofs[l][DE_LEVEL] - LU_DB_MIN) * 127 / (LU_DB_MAX - LU_DB_MIN));
#endif
    return v ? (uint32_t)clamp(*v + (p == 1u ? 64 : 0), 0, 127) : 0u;
}
static void lu_mix_set(uint32_t jt, uint32_t p, uint32_t v)
{
    uint32_t l = lu_lane(jt);
    int16_t *s = lu_slot(jt, p);
    if (v > 127u)
        return;
    if (l < DRUM_LANES && p == 1u)
        dlm_set_pan(l, (int32_t)v - 64);
#if FELUCCA_DRUM_EDIT
    else if (l < DRUM_LANES && s)
        dl.ofs[l][DE_LEVEL] = (int8_t)(LU_DB_MIN + ((int32_t)v * (LU_DB_MAX - LU_DB_MIN) + 63) / 127);
#endif
    else if (s)
        *s = (int16_t)((int32_t)v - (p == 1u ? 64 : 0));
    ui.force = 1;
}
static void lu_mixer(struct jdl_mixer *o)        /* every track's and lane's faders, the mute and solo masks over jank's tracks */
{
    uint32_t jt, p, k;
    memset(o, 0, sizeof *o);
    for (jt = 0; jt < LU_TRACKS; jt++)
        for (p = 0; p < 6u; p++)
            ((uint8_t *)&o->t[jt])[p] = (uint8_t)lu_mix_get(jt, p);
    for (k = 0; k < NTRK; k++)
        o->mute |= (uint32_t)(trk[k].p[P_MUTE] != 0) << k;
    o->mute |= (uint32_t)(dlm_mute & DRUM_LMASK) << LU_LANE0;
    o->solo = (song.solo & ((1u << NTRK) - 1u)) | (uint32_t)(dlm_solo & DRUM_LMASK) << LU_LANE0;
    o->active = song.sel;
}
_Static_assert(sizeof(struct jdl_mix_track) == 6u, "a mixer row is jank's six faders, in order");
static void lu_mute_set(uint32_t solo, uint32_t mask)   /* the whole new mask over jank's tracks: the 4, then the lanes */
{
    uint32_t k, l;
    for (k = 0; k < NTRK; k++)
        if (!solo)
            trk[k].p[P_MUTE] = (int16_t)(mask >> k & 1u);
    if (solo)
        song.solo = (uint8_t)((song.solo & ~((1u << NTRK) - 1u)) | (mask & ((1u << NTRK) - 1u)));
    for (l = 0; l < DRUM_LANES; l++) {
        if (solo)
            dlm_set_solo(l, (int)(mask >> (LU_LANE0 + l) & 1u));
        else
            dlm_set_mute(l, (int)(mask >> (LU_LANE0 + l) & 1u));
    }
    ui.force = 1;
}

/* ------------------------------------------------------------------ transport, clips, scenes --- */
static void lu_transport(uint32_t play)          /* the panel's PLAY / STOP request */
{
    if (play && !song.playing)
        transport_req = 1;
    else if (!play && song.playing)
        transport_req = 2;
}
#if FELUCCA_PATTERNS && JLAPP_CLIPS
static uint8_t lu_slot_of(uint32_t v) { return v < PAT_N ? (uint8_t)v : v == PAT_STOP ? JLCL_STOP : JLCL_NONE; }
static uint32_t lu_clips(struct jlcl_track t[JLCL_TRACKS])   /* each track's playing, queued and stored patterns; the scenes stored */
{
    uint32_t k, s, scenes = 0;
    for (k = 0; k < JLCL_TRACKS; k++) {
        t[k] = (struct jlcl_track){JLCL_NONE, JLCL_NONE, 0};
        if (k >= NTRK)
            continue;
        for (s = 0; s < PAT_N; s++)
            t[k].filled |= (uint16_t)((pat_has(k, s) != 0) << s);
        t[k].playing = lu_slot_of(pat_cur[k]);
        t[k].queued = lu_slot_of(pat_req[k]);
    }
    for (s = 0; s < SEC_IDS && s < 32u; s++)
        scenes |= (uint32_t)(project_used(s) != 0) << s;
    return scenes;
}
static void lu_clip(uint32_t k, uint32_t slot, uint32_t when)   /* slot JLCL_NONE: stop; when 0 the pattern's end, 1 the next bar, 2 now */
{
    if (k < NTRK && (slot < PAT_N || slot == JLCL_NONE) && when <= PW_NOW)
        pat_launch(k, slot < PAT_N ? slot : PAT_NONE, when);
}
static void lu_scene(uint32_t n)                 /* cued while playing, loaded when stopped */
{
    if (n < SEC_IDS && project_used(n) && (!song.playing || section_cue(n)) && !song.playing)
        section_load(n);
}
#endif

/* ------------------------------------------------------------------ the notes played (the lights) --- */
static size_t lu_played(struct jdl_note *o, size_t cap)   /* the notes played since, as jank's push (a drum hit: the drum track's and the lane's) */
{
    lv_ev_t e;
    size_t n = 0;
    while (n + 2u <= cap && lv_tap_get(&e)) {
        uint32_t k = e.trk % NTRK, note = e.note, l;
        struct jdl_note x = {(uint8_t)k, e.step, (uint8_t)note, e.vel, (uint8_t)lu_len(k), 0};
        if (IS_DRUMK(k)) {
            if ((l = lane_of_note(note)) >= DRUM_LANES)
                continue;                        /* (a note no lane plays now: the kit changed under it) */
            o[n++] = x;
            x.track = (uint8_t)(LU_LANE0 + l);
        }
        o[n++] = x;
    }
    return n;
}
static void lu_tap(int on)                       /* the tap on (the app shows what plays) or off (what waits dropped) */
{
    lv_tap_q.on = (uint8_t)(on != 0);
    if (!on)
        lu.dropped += lv_tap_drop();
}
#endif
