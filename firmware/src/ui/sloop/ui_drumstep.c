/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM STEP (FELUCCA_DRUM_STEP): the drum track's steps on the keys, as in SLOOP 2.4 "Drums with the keys"
 * (isod89, https://github.com/isod89/sloop-fm1, tag v2.4, GPL-3.0-only; its ui_studio.c grid_key, seq.c kb_grid and
 * audition_*, ui_layers.c pen_lane_move). The idea of keys as the 16 steps of a sound on TR-style pages first came
 * from PR #45 of isod89/sloop-fm1 by Erick Buendia Barrientos (Erbubar23, GPL-3.0). The data is the existing
 * dstep_t (16 lanes x 64 steps, a level and a ratchet per lane): nothing to save, projects interchange.
 *
 * As in 2.4:
 *   DRUMS grid page (SEQ or EDIT tapped on the drum track, no layer held)
 *     white keys                    the 16 steps of the sound KNOB 1 picks: press to set the step (you hear the
 *                                   sound), again to clear it; the keys light that sound's steps
 *     first four black keys         the page of steps (1-16, 17-32, 33-48, 49-64)
 *     KNOB 1 / KNOB 2               the sound / the step, heard (the step's sounds at their levels)
 *     SELECT                        grid <-> kit
 *   SEQ layer: KNOB 1 (and step held + KNOB 1) the sound, heard
 * Ours, where 2.4 defines nothing:
 *   FOLLOW: while playing, the page (the grid's bank, the SEQ layer's page) follows the playhead. On while stopped;
 *   a page key turns it off until the next stop; black key 5 (D#3) turns it on / off. */

#include "edge_walk.c"                              /* the stop-at-edge walk, shared with the mixer (tests/edge_walk_test.c) */

#define DS_FOLLOW_KEY 10u                              /* the black key (D#) that turns FOLLOW on / off */

/* THE LANE: one pick for the grid, the SEQ layer and the SOUND pages (they keep two copies, drum_lane and pen_lane).
 * hear: also when it is the lane already (SEQ + its key again). Heard only while the transport is stopped
 * (seq.c audition_req); playing, a pick is silent. Never from the mixer. */
static void lane_pick(uint32_t l, uint32_t hear)
{
    uint32_t changed;
    l &= 15u;
    changed = l != (uint32_t)pen_lane || l != (uint32_t)drum_lane;
    if (changed)
        gh_off = 0;                                     /* (a step key down on the old lane no longer clears it) */
    pen_lane = (uint8_t)l;
    drum_lane = (uint8_t)l;
    ui.force = 1;
    if (changed || hear)
        audition_lane(l);
}

/* ALGORITHM on the DRUMS screen: walks the lanes, stops at the kick; a fresh turn after the stop returns -1, the caller
 * goes to the track above (T3). Down at the last lane just stops. The walk's list: row 0 is T3, rows 1..16 the lanes,
 * the edge at row 1 (edge_walk.c). */
#define DS_WALK_ROWS (1u + DRUM_LANES)
static int32_t ds_algo_walk(int32_t s)
{
    static uint32_t last_ms;
    uint32_t r = edge_walk_n(1u + (drum_lane & 15u), s, DS_WALK_ROWS, 1u, edge_fresh(&last_ms, fm1_ms));
    if (r == 0u)
        return -1;
    if (r - 1u != (uint32_t)drum_lane)
        lane_pick(r - 1u, 0u);
    return 0;
}

/* KNOB 3 of the grid, on the cursor's step for the picked lane: up sets it, then its ratchet x2 .. x4; down takes the
 * ratchet off, then clears the step (the 2.4 grid had set / clear only: a ratchet was the SEQ layer's step-held KNOB 3) */
static void ds_ratchet_step(dstep_t *st, int32_t s)
{
    if (dstep_has(st, drum_lane)) {
        uint32_t rt = dstep_rat(st, drum_lane), lv = dstep_lvl(st, drum_lane);
        if (s > 0)
            dstep_set(st, drum_lane, lv, rt < 3u ? rt + 1u : 3u);
        else if (rt)
            dstep_set(st, drum_lane, lv, rt - 1u);
        else
            dstep_clr(st, drum_lane);
    } else if (s > 0) {
        dstep_set(st, drum_lane, LV_NORM, 0);
    }
}

static uint32_t ds_pages(uint32_t len) { return (len + 15u) / 16u; }

/* the page that holds the playhead (the page FOLLOW shows) */
static uint32_t ds_follow_page(uint32_t seq_idx, uint32_t len)
{
    uint32_t pg = seq_idx / 16u, n = ds_pages(len);
    return pg < n ? pg : n - 1u;
}

/* black key k -> the page it opens (-1: not a page key, or the page lies beyond the pattern) */
static int32_t ds_page_key(uint32_t k, uint32_t len)
{
    static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    if (k >= 12u || PG[k] < 0 || (uint32_t)PG[k] * 16u >= len)
        return -1;
    return PG[k];
}

/* step index of white key w on a page, or -1 when it lies beyond the pattern */
static int32_t ds_step_of(uint32_t page, uint32_t w, uint32_t len)
{
    uint32_t idx = page * 16u + w;
    return idx < len ? (int32_t)idx : -1;
}

/* a page key while it plays: FOLLOW off; stopped: armed again */
static void ds_follow_hand(void)
{
    if (song.playing)
        ui.step_follow = 0;
}
static void ds_follow_toggle(void)
{
    ui.step_follow = (uint8_t)!ui.step_follow;
    ui_message(ui.step_follow ? "FOLLOW ON" : "FOLLOW OFF");
}

/* once a frame, SEQ layer open */
static void ds_follow_tick(void)
{
    track_t *t = TSEL;
    if (!is_drum(t))
        return;
    if (!song.playing) {
        ui.step_follow = 1;
        return;
    }
    if (ui.step_follow && !ui.step_held)
        ui.step_page = (uint8_t)ds_follow_page(t->seq_idx, trk_len(t));
}

/* once a frame, DRUMS grid page: the cursor moves to the playhead's page, in its column */
static void ds_grid_follow(void)
{
    uint32_t len = trk_len(TDRUM), idx;
    if (!song.playing) {
        ui.step_follow = 1;
        return;
    }
    if (!ui.step_follow || gh_down)                    /* (a step key down keeps its page) */
        return;
    idx = ds_follow_page(TDRUM->seq_idx, len) * 16u + drum_cursor % 16u;
    drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
}

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
static int grid_keys_on(void) { return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu; }

/* THE HELD STEPS of the grid (the store's switches on): a step key held past HOLD_MS (core/hold.h) is a held step for the
 * picked lane, as in the SEQ layer: ui.step_held / ui.step_page carry it, so steps_held_* (ui_layers.c) edit it with no
 * copy. gh_down the keys down, gh_held those that became held, gh_off the set steps that clear when let go (a tap) */
static void grid_hold_drop(void)                       /* every key forgotten (a page key, a layer, the page left) */
{
    ui.step_held &= (uint16_t)~gh_held;
    gh_down = gh_held = gh_off = 0;
}

/* once a frame, no layer held */
static void grid_hold_tick(void)
{
    uint32_t w;
    if (!gh_down)
        return;
    if (!kb_grid) {
        grid_hold_drop();
        return;
    }
    for (w = 0; w < 16u; w++) {
        uint32_t bit = 1u << w;
        if (!(gh_down & bit))
            continue;
        if (!((fm1_in.notes >> key_of_lane(w)) & 1u)) {   /* (its release was lost: forgotten, not a tap) */
            gh_down &= (uint16_t)~bit;
            gh_held &= (uint16_t)~bit;
            gh_off &= (uint16_t)~bit;
            ui.step_held &= (uint16_t)~bit;
            continue;
        }
#if FELUCCA_AUTO
        if (!(gh_held & bit) && fm1_ms - gh_t0[w] >= HOLD_MS) {
            gh_held |= (uint16_t)bit;
            gh_off &= (uint16_t)~bit;                   /* (held: not a tap, the step stays) */
            ui.step_held |= (uint16_t)bit;
            ui.step_page = (uint8_t)(drum_cursor / 16u);
            pen_lane = drum_lane;
            ui.force = 1;
        }
#endif
    }
}

/* a key down on the GRID page: the white keys are the 16 steps of this page for the sound of KNOB 1 (an empty one is
 * set at NORM and the sound heard, at once; a set one goes when its key is let go, unless it was held: a held step is
 * edited, SEQ layer style); the first four black keys pick the page. The cursor follows */
static void grid_key(uint32_t k)
{
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k), pg;
    dstep_t *st;
    if (w < 0) {
        if ((pg = ds_page_key(k, len)) >= 0) {
            grid_hold_drop();                           /* (a held step keeps its page: a page key lets go of them) */
            idx = (uint32_t)pg * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ds_follow_hand();
            ui.force = 1;
        } else if (k == DS_FOLLOW_KEY) {
            ds_follow_toggle();
        }
        return;
    }
    if ((pg = ds_step_of(drum_cursor / 16u, (uint32_t)w, len)) < 0)
        return;
    idx = (uint32_t)pg;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    gh_down |= (uint16_t)(1u << w);
    gh_t0[w] = fm1_ms;
    st = &TDRUM->dstep[idx];
    if (dstep_has(st, drum_lane)) {
        gh_off |= (uint16_t)(1u << w);                  /* (cleared when let go, unless held) */
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    fm1_irq_off();
    dstep_set(st, drum_lane, LV_NORM, 0);
    TDRUM->seq_active = 1;
    fm1_irq_on();
    audition_lane(drum_lane);
    sync_reload = 1;
}

/* a step key let go: a tap on a set step clears it (a held one stays) */
static void grid_key_up(uint32_t k)
{
    int32_t w = punch_key(k);
    uint32_t bit, idx;
    dstep_t *st;
    if (w < 0)
        return;
    bit = 1u << w;
    if (!(gh_down & bit))
        return;
    gh_down &= (uint16_t)~bit;
    gh_held &= (uint16_t)~bit;
    ui.step_held &= (uint16_t)~bit;
    if (!(gh_off & bit))
        return;
    gh_off &= (uint16_t)~bit;
    idx = (drum_cursor / 16u) * 16u + (uint32_t)w;
    if (idx >= trk_len(TDRUM) || !dstep_has(&TDRUM->dstep[idx], drum_lane))
        return;
    st = &TDRUM->dstep[idx];
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    fm1_irq_off();
    dstep_clr(st, drum_lane);
#if FELUCCA_AUTO
    if (!dstep_mask(st))                                /* (SLOOP 2.4: an empty step keeps no nudge, lock, condition) */
        (void)auto_step_clear(TDRUM, idx, AUTO_ONLYS);
#endif
    fm1_irq_on();
    sync_reload = 1;
}
