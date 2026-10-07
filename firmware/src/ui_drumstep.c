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

#define DS_FOLLOW_KEY 10u                              /* the black key (D#) that turns FOLLOW on / off */

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
    if (!ui.step_follow)
        return;
    idx = ds_follow_page(TDRUM->seq_idx, len) * 16u + drum_cursor % 16u;
    drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
}

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
static int grid_keys_on(void) { return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu; }

/* a key down on the GRID page: the white keys are the 16 steps of this page for the sound of KNOB 1 (an empty one
 * is set at NORM and the sound heard, a set one cleared); the first four black keys pick the page. The cursor follows */
static void grid_key(uint32_t k)
{
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k), pg;
    dstep_t *st;
    if (w < 0) {
        if ((pg = ds_page_key(k, len)) >= 0) {
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
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        dstep_clr(st, drum_lane);
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    if (dstep_has(st, drum_lane))
        audition_lane(drum_lane);
    sync_reload = 1;
}
