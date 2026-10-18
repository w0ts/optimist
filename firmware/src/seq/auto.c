/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store (FELUCCA_AUTO: motion recording, SLOOP 2.4's step extras, per-step chance; docs/UI-OPTIMIST-
 * DESIGN.md section 6), included by seq.c. One list of events per track (auto.h): what motion recording (motion.c)
 * and the locks, nudges and fill conditions (seq24.c) kept in two stores before (motion.c's 64 shared events,
 * stepx.h's 176 B a track), and chance on drum and synth steps.
 *
 *   auto_w          the working project's lists and motion's PLAY bits: what the sequencer plays; the storage
 *                   (storage/auto_proj.c) captures and applies it with the project buffers
 *   auto_step       the sequencer enters a step (seq_tick, the audio ISR, once a step): one scan of the track's list.
 *                   Its hold events apply in list order (motion.c motion_hold), its step-only events are gathered:
 *                   the fill condition (FELUCCA_FILLS: a step that fails it plays as a REST, its step-only events
 *                   skipped, its hold events applied: a recorded sweep goes on through a fill), the chance
 *                   (FELUCCA_CHANCE; an event wins over a synth step's chance bits, chance.c), the locks (FELUCCA_PLOCK,
 *                   seq24.c lock_apply: each saves the base it found, which is whatever a hold event left, and lets go
 *                   at the next step without one). Order kept: automation, then fill, then macros (EN_STEP)
 *   auto_nudge      FELUCCA_MICRO: each step's nudge, from the list, for micro_units (read a block, ahead of the step):
 *                   rebuilt whenever a list changes (auto_touch), never per sample
 * The main loop changes a list with the interrupts off (as the parameter batches); auto_touch after each change. */
#include "auto.h"

_Static_assert(NTRK == AUTO_NTRK && NSTEP == SX_NSTEP, "the automation store: four tracks of 64 steps");
typedef struct {
    uint32_t psum;                       /* the project the store belongs to (storage/auto_proj.c; the working one: 0) */
    uint8_t on, rsv[3];                  /* bit k: track k plays its hold events (motion's PLAY) */
    auto_list_t l[NTRK];
} auto_store_t;
static auto_store_t auto_w;              /* the working project's */
#define AUTO_L(k) (&auto_w.l[(uint32_t)(k) % NTRK])
#define AL(t) AUTO_L(trk_index(t))

/* Motion's old form, kept as a stored form (motion_flash.c's MOTN record, sec_codec.c's SEC_MOT chunk, a V1 pattern
 * record's chunk): at most 64 hold events for the four tracks together, place = track << 6 | step */
#define MOTION_MAX 64u
typedef auto_ev_t motion_ev_t;
typedef struct {
    uint32_t psum;
    uint8_t count, on, rsv[2];
    motion_ev_t ev[MOTION_MAX];
} motion_store_t;
_Static_assert(sizeof(motion_store_t) == 200u, "motion store layout");

/* The ids an event stores do not depend on the build: P_LEVEL .. P_E7 are the same in every build of a project format,
 * and after P_E7 the layout of a build with every value there: SLOOP 2.4's FILT STRUM VLEAD (SL24_TP: P_TFLT P_STRUM
 * P_VLEAD), then the COMP insert's amount (MOT_TCOMP). A build without 2.4's values numbers P_TCOMP P_ENG_END itself,
 * the id 2.4's FILT has elsewhere: without the map a FILT event from a TRK_FILT build played as COMP here. An event for
 * a value after P_E7 that this build lacks is kept and not played: it plays again in a build that has the value. The
 * pseudo-parameters come after every stored id (auto.h) */
#define MOT_NTAIL 29u                                         /* (stored: P_ENG_END FILT, +1 STRUM, +2 VLEAD, +3 COMP,
                                                              * +4 .. +9 the further inserts' FOLD FUZZ PHSR FLNG FRMT WIDE,
                                                              * +10 +11 CHRS ECHO, +12 GATER's DEPTH, +13 DUST, +14 JUNO
                                                              * CHORUS's MIX, +15 .. +18 ENV 1 MOD's PIT and its
                                                              * three moves: 2026-10-16, every build has them; +19 .. +21
                                                              * GATER's PATTERN RATE SHAPE, +22 JUNO's MODE, +23 .. +28 the
                                                              * shapers SFLD RECT HARM RING SUB SLEW: 2026-10-18, the user:
                                                              * "automate / plock / modulate all fx params") */
#define MOT_TAIL_END (P_ENG_END + MOT_NTAIL)
_Static_assert(MOT_TAIL_END < AUTO_NUDGE, "the pseudo-parameters: past every stored id");
_Static_assert(MOT_TAIL_END <= AUTO_NGATE0, "the note ends and the drum lanes' ids (auto.h): past every build's stored track ids");
_Static_assert(MOT_TAIL_END <= AUTO_ACC, "the ACCENT lane's id (auto.h): past every build's stored track ids");
_Static_assert(MOT_TAIL_END <= AUTO_MUTE0 && DRUM_LANES <= 10u, "the mutes' ids (auto.h): past every build's stored track ids, a lane each");
/* each stored tail id's value in this build (P_COUNT: none here). LFO 2's values (P_L2RATE ..) have no stored id: none
 * maps to them (an event stored past 2.4's in a build without them is never LFO 2's) */
static const uint8_t MOT_TAIL[MOT_NTAIL] = {
#if SL24_TP
    P_TFLT, P_STRUM, P_VLEAD,
#else
    P_COUNT, P_COUNT, P_COUNT,
#endif
#if FELUCCA_MASTER_COMP
    P_TCOMP,
#else
    P_COUNT,
#endif
#if INS_FX
    P_TFOLD, P_TFUZZ, P_TPHSR, P_TFLNG, P_TFRMT, P_TWIDE, P_TCHRS, P_TECHO, P_TGATE,
#else
    P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT,
#endif
#if FELUCCA_FX_DUST
    P_TDUST,
#else
    P_COUNT,
#endif
#if FELUCCA_INS_JCHORUS
    P_TJCHO,
#else
    P_COUNT,
#endif
    P_E1PIT, P_E1X, P_E1Y, P_E1Z,                            /* (ENV 1 MOD: locks and motion on them since 2026-10-16) */
#if INS_FX
    P_TGPAT, P_TGRATE, P_TGSHP,                              /* (the insert settings, 2026-10-18) */
#else
    P_COUNT, P_COUNT, P_COUNT,
#endif
#if FELUCCA_INS_JCHORUS
    P_TJMODE,
#else
    P_COUNT,
#endif
#if INS_FX
    P_TSFLD, P_TRECT, P_THARM, P_TRING, P_TSUBO, P_TSLEW,
#else
    P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT,
#endif
};
/* the stored tail's second range (2026-10-18: the first was full at AUTO_MUTE0), auto.h AUTO_TAIL2 .. (0xCC ..): the
 * track's DYN settings RATIO ATK REL GAIN CEIL, EQ3's LO MID HI FREQ L FREQ H (fx_slots.c: track values since) */
static const uint8_t MOT_TAIL2[] = {
#if FELUCCA_MASTER_COMP
    P_TDRAT, P_TDATK, P_TDREL, P_TDGAIN, P_TDCEIL,
#else
    P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT,
#endif
#if FELUCCA_INS_EQ3
    P_TEQLO, P_TEQMID, P_TEQHI, P_TEQFL, P_TEQFH,
#else
    P_COUNT, P_COUNT, P_COUNT, P_COUNT, P_COUNT,
#endif
};
#define MOT_NTAIL2 ((uint32_t)sizeof MOT_TAIL2)
_Static_assert(MOT_NTAIL2 <= AUTO_TAIL2_N, "the second tail range: inside its ids");
static uint32_t mot_id(uint32_t s)                           /* stored -> this build's (P_COUNT: none here) */
{
    if (s - AUTO_TAIL2 < MOT_NTAIL2)
        return MOT_TAIL2[s - AUTO_TAIL2];
    return s < P_ENG_END ? s : s < MOT_TAIL_END ? MOT_TAIL[s - P_ENG_END] : (uint32_t)P_COUNT;
}
static uint32_t mot_sid(uint32_t id)                         /* this build's -> stored */
{
    uint32_t k;
    for (k = 0; id >= P_ENG_END && k < MOT_NTAIL; k++)
        if (MOT_TAIL[k] == id)
            return P_ENG_END + k;
    for (k = 0; id >= P_ENG_END && id < P_COUNT && k < MOT_NTAIL2; k++)
        if (MOT_TAIL2[k] == id)
            return AUTO_TAIL2 + k;
    return id;
}

#if FELUCCA_MICRO
static int8_t auto_nudge[NTRK][NSTEP];   /* each step's nudge (the ISR: micro_units) */
#endif
/* track k's list changed (the IRQ off, or the ISR): the nudges again */
static void auto_touch(uint32_t k)
{
#if FELUCCA_MICRO
    const auto_list_t *l = AUTO_L(k);
    uint32_t i;
    memset(auto_nudge[k % NTRK], 0, sizeof auto_nudge[0]);
    for (i = 0; i < l->n; i++)
        if (l->ev[i].param == AUTO_NUDGE && (l->ev[i].place & AUTO_ONLY))
            auto_nudge[k % NTRK][l->ev[i].place & AUTO_STEP] = l->ev[i].value;
#else
    (void)k;
#endif
}
static void auto_touch_all(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        auto_touch(k);
}

/* the old motion form <-> the lists. A list's hold events (track order) into m: 1 all fitted. A drum lane's (auto.h
 * AUTO_LANE) is left out: an older motion_valid refuses the whole form for it; it goes in the new forms only (the
 * extras record's AUTO_ENC_TAG form, a V2 pattern record: auto_trk_old_ok is 0 with one) */
static int auto_to_motion(motion_store_t *m, const auto_list_t *l, uint32_t on)
{
    uint32_t k, i, all = 1;
    m->count = 0;
    m->on = (uint8_t)on;
    m->rsv[0] = m->rsv[1] = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < l[k].n; i++) {
            const auto_ev_t *e = &l[k].ev[i];
            if ((e->place & AUTO_ONLY) || AUTO_IS_LANE(e->param))
                continue;
            if (m->count >= MOTION_MAX) {
                all = 0;
                continue;
            }
            m->ev[m->count].place = (uint8_t)(k << 6 | (e->place & AUTO_STEP));
            m->ev[m->count].param = e->param;
            m->ev[m->count++].value = e->value;
        }
    return (int)all;
}
/* m's events added to the lists as hold events (a list full: the rest cut, 0) */
static int auto_from_motion(auto_list_t *l, const motion_store_t *m)
{
    uint32_t i, all = 1;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        const motion_ev_t *e = &m->ev[i];
        if (auto_put(&l[(e->place >> 6) % NTRK], e->place & AUTO_STEP, e->param, e->value) < 0)
            all = 0;
    }
    return (int)all;
}

#if FELUCCA_MOTION || FELUCCA_PLOCK
#include "auto_base.c"         /* the one list of values an event sets, the one base under them, the locks in force */
#endif
#if FELUCCA_MOTION
#include "motion.c"            /* hold events: knob moves recorded per step (from Felucca 1.0) */
#endif
#if SL24_STEPX
#include "seq24.c"             /* SLOOP 2.4: micro timing, fills, parameter locks (backports24seq.h) */
#endif
#if FELUCCA_MOTION || FELUCCA_PLOCK
/* the patch's value of parameter id of track t under its automation (auto_base.c's base while a hold event or a lock
 * changed it, else the value itself). What a project stores (storage/project.c proj_patch: a capture while playing
 * never takes a lock's or a hold's value for the patch) and what a recorded GATE compares with (seq.c rec_gate_set) */
static int16_t auto_patch(const track_t *t, uint32_t id)
{
    uint32_t k = trk_index(t) % NTRK;
    return AB_ON(k, id) ? motion_base[k][id] : t->p[id];
}
#endif
#if FELUCCA_CHANCE
static uint8_t auto_ch[NTRK];            /* the step playing's chance event, 100: none (chance.c, seq_tick) */
#endif
#if SEQ_SKIP
static uint8_t seq_skip[NTRK];           /* the step playing failed its condition or its fill: nothing of it sounds */
#endif
/* the step playing's muted notes (auto.h AUTO_MUTE; auto_step sets them, seq.c plays around them): a drum lane's bit
 * (the drum track), AUTO_MUTED_SYN a synth step's (all its notes) */
#define AUTO_MUTED_SYN 0x8000u
static uint16_t auto_muted[NTRK];
#define STEP_MUTED_SYN(t) ((auto_muted[trk_index(t) % NTRK] & AUTO_MUTED_SYN) != 0u)
#define STEP_MUTED_LANES(t) ((uint32_t)auto_muted[trk_index(t) % NTRK] & ~AUTO_MUTED_SYN)
#if FELUCCA_COND
/* SPDX: the step conditions are Felucca 1.5.6's (hugelton/Felucca 9f6a885, seq.c seq_cond_miss / seq_enter, Leo
 * Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0-only), here events of the store (auto.h AUTO_COND).
 * a:b plays on loop a of every b of its own track: cond_loop counts the passes through the track's LEN, 0 the first,
 * modulo COND_LOOPS (a multiple of 2, 3 and 4). It starts again on PLAY and a song section (seq_reset_tracks) and
 * when a launched pattern takes over (pat.c); a LEN changed while playing keeps counting. A step nudged early into
 * the loop before fires after the wrap (seq_tick), so it is the next loop's, as Felucca's */
#define COND_LOOPS 12u
#define COND_NEW 0xFFu                   /* reset: the next step 0 is loop 0 */
static uint8_t cond_loop[NTRK] = {COND_NEW, COND_NEW, COND_NEW, COND_NEW};
static const uint8_t COND_A[AUTO_NCOND] = {1, 2, 1, 2, 3, 1, 2, 3, 4}, COND_B[AUTO_NCOND] = {2, 2, 3, 3, 3, 4, 4, 4, 4};
static void cond_reset(uint32_t k) { cond_loop[k % NTRK] = COND_NEW; }
static void cond_pass(uint32_t k)        /* track k enters its step 0 (the ISR) */
{
    uint32_t v = cond_loop[k % NTRK];
    cond_loop[k % NTRK] = (uint8_t)(v == COND_NEW || v + 1u >= COND_LOOPS ? 0u : v + 1u);
}
static int cond_miss(uint32_t k, uint32_t c)   /* condition c (1..AUTO_NCOND) fails on this loop of track k */
{
    uint32_t v = cond_loop[k % NTRK];
    c = (c - 1u) % AUTO_NCOND;
    return (v == COND_NEW ? 0u : v) % COND_B[c] != COND_A[c] - 1u;
}
#endif
#if DLA_ON
/* a drum lane's event e of the step entered (auto_step, the ISR; the drum track only): a hold event (its motion plays:
 * mo) holds until the next of its value or the pass's end, a step-only one (a lock) its step; heard at the lane's next
 * hits (drum_edit.c dla, lane_play.c), never in dl.ofs */
static void dla_event(const auto_ev_t *e, uint32_t mo)
{
    uint32_t l = AUTO_LANE_L(e->param), v = AUTO_LANE_V(e->param);
    int8_t x;
    if (l >= DRUM_LANES || smute_lane_on(l))
        return;                                      /* (no such lane: an id of the 16 fixed lanes; its sequencer muted: no lock,
                                                      * no motion; seq_mute.c) */
    x = (int8_t)clamp(e->value, DE_MIN[v], DE_MAX[v]);
    if (e->place & AUTO_ONLY) {
#if FELUCCA_PLOCK
        dla.lv[l][v] = x;
        dla.lm[l] |= (uint8_t)(1u << v);
        dla.any |= 2u;
#endif
    } else if (mo) {
        dla.hv[l][v] = x;
        dla.hm[l] |= (uint8_t)(1u << v);
        dla.any |= 1u;
    }
    (void)x;
}
#endif

/* the sequencer enters step idx of track t (seq_tick, the ISR): its events. -> 1 the step plays, 0 its fill
 * condition fails (it plays as a REST) */
static uint32_t auto_step(track_t *t, uint32_t idx)
{
    uint32_t k = trk_index(t) % NTRK, i, plays = 1, fill = FC_NORM, cond = 0;
    const auto_list_t *l = AUTO_L(k);
#if FELUCCA_PLOCK
    uint8_t lp[NLOCK];
    int8_t lv[NLOCK];
    uint32_t m = 0, ltaken = 0;                      /* (ltaken: the LFO slots an old depth event took, lfo_leg_ev) */
#endif
#if FELUCCA_MOTION
    uint32_t mo = (auto_w.on >> k) & 1u;
    if (mo)
        motion_pass(t, idx);                         /* (its base; step 0: each pass from the patch) */
#endif
#if FELUCCA_CHANCE
    auto_ch[k] = 100;
#endif
    auto_muted[k] = 0;
#if DLA_ON
    if (k == TRK_DRUM)
        dla_locks_off();                             /* (the last step's lane locks let go) */
#endif
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &l->ev[i];
        if ((e->place & AUTO_STEP) != idx)
            continue;
        if (AUTO_IS_MUTE(e->param)) {                /* (a muted note: before the lanes and mot_id, which have no such id) */
            if ((e->place & AUTO_ONLY) && e->value)
                auto_muted[k] |= (uint16_t)(e->param == AUTO_SMUTE ? AUTO_MUTED_SYN : 1u << (e->param - AUTO_MUTE0));
            continue;
        }
#if DLA_ON
        if (AUTO_IS_LANE(e->param)) {                /* (a drum lane's value: before mot_id, which has no such id) */
            if (k == TRK_DRUM)
#if FELUCCA_MOTION
                dla_event(e, mo);
#else
                dla_event(e, 0);
#endif
            continue;
        }
#endif
        if (!(e->place & AUTO_ONLY)) {
#if FELUCCA_MOTION
            if (mo)
                motion_hold(t, k, e);
#endif
            continue;
        }
        if (e->param == AUTO_FILL)
            fill = (uint8_t)e->value;
        else if (e->param == AUTO_COND)
            cond = (uint8_t)e->value;
#if FELUCCA_CHANCE
        else if (e->param == AUTO_CHANCE)
            auto_ch[k] = (uint8_t)e->value;
#endif
#if FELUCCA_PLOCK
        else if (!AUTO_PSEUDO(e->param) && m < NLOCK) {
            uint32_t id = mot_id(e->param), did = P_COUNT, dest = 0;
            int32_t val = e->value;
            if (!is_drum(t) && LEG_ID(id)) {
                id = m + 1u < NLOCK ? lfo_leg_ev(t, id, &val, &ltaken, &did, &dest) : P_COUNT;   /* (a depth of before: */
                if (did < P_COUNT)                     /* the slot on it, AMT units; a free slot put on it for the step) */
                    lp[m] = (uint8_t)did, lv[m++] = (int8_t)dest;
            } else if (!lock_ok(t, id)) {
                id = P_COUNT;
            }
            if (id < P_COUNT)
                lp[m] = (uint8_t)id, lv[m++] = (int8_t)clamp(val, -128, 127);
        }
#endif
    }
#if FELUCCA_COND
    if (cond && cond_miss(k, cond))
        plays = 0;                                   /* (the condition first, then FILL, then the chance: seq_tick) */
#endif
#if FELUCCA_FILLS
    if (plays)
        plays = fill == FC_FILL ? fill_now : fill == FC_NOFILL ? !fill_now : 1u;
#else
    (void)fill;
#endif
    (void)cond;
#if FELUCCA_PLOCK
    lock_apply(t, lp, lv, plays && !smute_trk_on(k) ? m : 0u);   /* (a fill-skipped step, a track muted: its locks do not apply) */
#endif
#if DLA_ON
    if ((!plays || smute_trk_on(k)) && k == TRK_DRUM)
        dla_locks_off();                             /* (nor its lane locks: its lane holds stay) */
#endif
    return plays;
}

/* ---- the edits (the UIs, the editor; the main loop with the IRQ off, unless said) */
/* step idx's nudge, fill condition, chance (100: none) */
static int32_t step_micro(const track_t *t, uint32_t idx) { return auto_pseudo(AL(t), idx % NSTEP, AUTO_NUDGE, 0); }
static uint32_t step_fill(const track_t *t, uint32_t idx) { return (uint32_t)auto_pseudo(AL(t), idx % NSTEP, AUTO_FILL, FC_NORM); }
static uint32_t step_chance_ev(const track_t *t, uint32_t idx) { return (uint32_t)auto_pseudo(AL(t), idx % NSTEP, AUTO_CHANCE, 100); }
/* ... set (clamped; the default: no event); 0 the list is full */
static int step_micro_set(track_t *t, uint32_t idx, int32_t v)
{
    int ok = auto_pseudo_set(AL(t), idx % NSTEP, AUTO_NUDGE, clamp(v, MICRO_MIN, MICRO_MAX));
    auto_touch(trk_index(t));
    return ok;
}
static int step_fill_set(track_t *t, uint32_t idx, uint32_t v)
{
    return auto_pseudo_set(AL(t), idx % NSTEP, AUTO_FILL, v > FC_NOFILL ? FC_NORM : (int32_t)v);
}
/* step idx's condition (0 none, 1..AUTO_NCOND); set it (0: none, the event goes); 0 the list is full */
static uint32_t step_cond(const track_t *t, uint32_t idx) { return (uint32_t)auto_pseudo(AL(t), idx % NSTEP, AUTO_COND, 0); }
static int step_cond_set(track_t *t, uint32_t idx, uint32_t c) { return auto_pseudo_set(AL(t), idx % NSTEP, AUTO_COND, (int32_t)c); }
static int step_chance_set(track_t *t, uint32_t idx, uint32_t pct) { return auto_pseudo_set(AL(t), idx % NSTEP, AUTO_CHANCE, pct >= 100u ? 100 : (int32_t)pct); }
/* the step-only event of (step idx, our id): 1 there is one, its value into *v */
static int lock_get(const track_t *t, uint32_t idx, uint32_t id, int32_t *v)
{
    return id < P_COUNT && auto_get(AL(t), (idx % NSTEP) | AUTO_ONLY, mot_sid(id), v);
}
static void lock_drop(track_t *t, uint32_t idx, uint32_t id)
{
    int i = id < P_COUNT ? auto_find(AL(t), (idx % NSTEP) | AUTO_ONLY, mot_sid(id)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
}
/* the hold event of (step idx, our id) goes; 1 there was one */
static int auto_step_hold_drop(track_t *t, uint32_t idx, uint32_t id)
{
    int i = id < P_COUNT ? auto_find(AL(t), idx % NSTEP, mot_sid(id)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
    return i >= 0;
}
/* the hold event of (step idx, our id) */
static int hold_get(const track_t *t, uint32_t idx, uint32_t id, int32_t *v)
{
    return id < P_COUNT && auto_get(AL(t), idx % NSTEP, mot_sid(id), v);
}
/* step idx's events of the kinds which (AUTO_HOLDS, AUTO_ONLYS) go; -> how many */
static uint32_t auto_step_clear(track_t *t, uint32_t idx, uint32_t which)
{
    uint32_t n = auto_drop_step(AL(t), idx % NSTEP, which);
    auto_touch(trk_index(t));
    return n;
}
/* what step idx of track t holds, as marks to draw (SLOOP's SEQ tiles, the DRUMS grid's cells): AUTO_MK_ONLY any event
 * but a fill: a nudge, a lock, a chance or a motion (hold) event (the user treats them as one: "motion is a plock", so
 * one dot); AUTO_MK_FILL / AUTO_MK_NOFILL its fill condition. Main loop, one pass of the list */
#define AUTO_MK_ONLY 1u
#define AUTO_MK_FILL 4u
#define AUTO_MK_NOFILL 8u
static uint32_t auto_mark_of(const auto_ev_t *e)
{
    if (!(e->place & AUTO_ONLY) || e->param != AUTO_FILL)
        return AUTO_MK_ONLY;
    return e->value == FC_FILL ? AUTO_MK_FILL : AUTO_MK_NOFILL;
}
static uint32_t auto_step_marks(const track_t *t, uint32_t idx)
{
    const auto_list_t *l = AL(t);
    uint32_t i, m = 0, s = idx % NSTEP;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        if ((l->ev[i].place & AUTO_STEP) == s)
            m |= auto_mark_of(&l->ev[i]);
    return m;
}
/* the marks of every step at once (the DRUMS grid draws 16 and hashes all: one pass, not NSTEP) */
static void auto_marks_all(const track_t *t, uint8_t *m)
{
    const auto_list_t *l = AL(t);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        m[i] = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        m[l->ev[i].place & AUTO_STEP] |= (uint8_t)auto_mark_of(&l->ev[i]);
}
/* every step-only event of track t gone (a cleared pattern: seq.c steps_clear) */
static void auto_only_clear(track_t *t)
{
    (void)auto_drop_kind(AL(t), AUTO_ONLYS);
    auto_touch(trk_index(t));
}
/* does auto_clear_where(sid, lane) take event e? The step-only GATE locks never: they are the recorded notes' lengths
 * (seq.c rec_gate_set) */
static int auto_clear_hit(const auto_ev_t *e, int32_t sid, int32_t lane)
{
    uint32_t p = e->param;
    if ((e->place & AUTO_ONLY) && (p == mot_sid(P_SGATE) || AUTO_IS_MUTE(p)))
        return 0;                                    /* (nor a muted note's mute: the note's, not a lock) */
    if (sid >= 0)
        return p == (uint32_t)sid;
    if (lane >= 0)
        return (AUTO_IS_LANE(p) && AUTO_LANE_L(p) == (uint32_t)lane) || (lane == (int32_t)DRUM_ACC && p == mot_sid(P_DACC));
    return 1;
}
/* how many events of track t auto_clear_where would take (the main loop) */
static uint32_t auto_count_where(const track_t *t, int32_t sid, int32_t lane)
{
    const auto_list_t *l = AL(t);
    uint32_t i, k = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        k += (uint32_t)auto_clear_hit(&l->ev[i], sid, lane);
    return k;
}
/* a muted note (auto.h AUTO_MUTE): step idx's hit of drum lane l, or the synth step (l ignored); the main loop */
static int step_muted(const track_t *t, uint32_t idx, uint32_t l)
{
    int32_t v;
    uint32_t id = is_drum(t) ? AUTO_MUTE(l) : AUTO_SMUTE;
    return (is_drum(t) ? l < DRUM_LANES : 1) && auto_get(AL(t), (idx % NSTEP) | AUTO_ONLY, id, &v) && v;
}
/* mute it (1) or not (0); -> 0 the list is full (the IRQ off) */
static int step_mute_set(track_t *t, uint32_t idx, uint32_t l, uint32_t on)
{
    uint32_t id = is_drum(t) ? AUTO_MUTE(l) : AUTO_SMUTE, pl = (idx % NSTEP) | AUTO_ONLY;
    int i;
    if (is_drum(t) && l >= DRUM_LANES)
        return 0;
    if (on) {
        if (auto_put(AL(t), pl, id, 1) < 0)
            return 0;
    } else if ((i = auto_find(AL(t), pl, id)) >= 0) {
        auto_del(AL(t), (uint32_t)i);
    }
    auto_touch(trk_index(t));
    return 1;
}
/* track t's motion and locks cleared, the notes kept (the Optimist UI's SHIFT + SEQ, SHIFT + SHIFT2 + a knob): every
 * event (sid < 0, lane < 0: the nudges, fills, chances and conditions too), one parameter's (sid: a stored id,
 * mot_sid, or AUTO_LANE(l, v)) or one drum lane's (lane: its values', with DRUM_ACC the ACCENT amount's too), the
 * recorded notes' lengths always kept (auto_clear_hit). What the motion set goes back to the patch (motion_restore;
 * the events left set theirs again at their steps). Callers hold the IRQ off. -> how many went */
static uint32_t auto_clear_where(track_t *t, int32_t sid, int32_t lane)
{
    auto_list_t *l = AL(t);
    uint32_t i, n = 0, k = 0, holds = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &l->ev[i];
        if (auto_clear_hit(e, sid, lane)) {
            k++;
            continue;
        }
        holds += !(e->place & AUTO_ONLY);
        l->ev[n++] = *e;
    }
    l->n = (uint8_t)n;
    if (k) {
#if FELUCCA_MOTION || FELUCCA_PLOCK
        motion_restore(t);
#endif
#if FELUCCA_MOTION
        if (!holds)
            auto_w.on &= (uint8_t)~(1u << trk_index(t));
#endif
        auto_touch(trk_index(t));
    }
    (void)holds;
    return k;
}
/* the event of (step idx, our id) changes kind (the Optimist UI's YES with a step held and a cell hot): a step-only
 * event becomes a hold event (FELUCCA_MOTION, a recordable value; its PLAY bit on), a hold event a step-only one
 * (FELUCCA_PLOCK, a lockable value); one of each there: the step-only one goes, the hold one takes its value. -> 1
 * now a hold, 2 now step-only, 0 none there or the other kind not allowed for it */
static uint32_t auto_kind_toggle(track_t *t, uint32_t idx, uint32_t id)
{
    auto_list_t *l = AL(t);
    uint32_t s = idx % NSTEP, sid = id < P_COUNT ? mot_sid(id) : AUTO_NUDGE;
    int o = auto_find(l, s | AUTO_ONLY, sid), h = auto_find(l, s, sid);
    int32_t v;
    if (sid == AUTO_NUDGE)
        return 0;
    if (o >= 0) {
#if FELUCCA_MOTION
        if (!motion_param(t, id))
            return 0;
        v = l->ev[o].value;
        auto_del(l, (uint32_t)o);
        (void)auto_put(l, s, sid, v);                    /* (a slot freed above: never full) */
        auto_w.on |= (uint8_t)(1u << trk_index(t));
        return 1;
#else
        return 0;
#endif
    }
    if (h >= 0) {
#if FELUCCA_PLOCK
        if (!lock_ok(t, id))
            return 0;
        v = l->ev[h].value;
        auto_del(l, (uint32_t)h);
        (void)auto_put(l, s | AUTO_ONLY, sid, v);
        return 2;
#else
        return 0;
#endif
    }
    (void)v;
    return 0;
}
#if DLA_ON
#include "auto_lane.c"         /* the drum lanes' motion and locks: the UIs' calls */
#endif
