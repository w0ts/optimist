/* SPDX-License-Identifier: GPL-3.0-only */
/* The keys' lights (the user, 2026-10-12, page organisation D10: "keys should lit up when they have a function in the
 * selected mode"), the same rule as the buttons' (op_input.c op_btn_avail): full = active or selected, dim = it has a
 * job in this mode, dark = none. Keys that only play notes, the default everywhere, stay dark: the ERASE and REPEAT
 * layers' keys too, and the FM6 layer's white (audition) keys. What each mode lights is what its tiles show
 * (op_laydraw.c tiles_fill):
 *   FM6 layer (ENV held)  its 10 black keys dim; the target edited and the voice mode full (op_fm6.c)
 *   FX layer              the 16 white keys dim (punch-in); the latched effect full
 *   GLO layer (MIX)       mute 1-4, fill 9, bar 10, tap 16 and the black 1-4 (FX on / off) dim; muted, a fill
 *                         playing or armed, bypassed full
 *   SCL layer (KEY)       all 27 keys dim; the song's root full
 *   LFO layer (PATTERNS)  the 16 patterns dim, the playing one full, the queued one blinking (black 10: the scenes, the
 *                         playing one full, the queued one blinking); the black keys with a job dim, the selected
 *                         track's and black 10 while the scenes are up full
 *   TEMPO (PLAY held)     the white keys tap: dim
 *   PROJECT               the cursor row's slots (PROJECT, SNAPSHOT, USER): used full, free dim, the selected one blinking
 *   STEP                  set steps full, the playhead blinking, empty steps inside LEN dim, past LEN dark
 *   SONG's PATTERNS row   as the LFO layer
 *   NAME                  every key with a job dim, the key cycling its letters full
 *   SHIFT / SHIFT2 held   the lanes' black keys, the first 4 white keys and the last upper key dim (lanes, T1 T2 T3 DR,
 *                         ACCENT: op_input.c shift_keys), the muted (SHIFT) or the selected (SHIFT2) full; every page
 *                         but a layer's (STEP: its white keys stay its steps)
 *   the drum track        the lanes' black keys and the last upper key (ACCENT) dim, the selected one full (every page)
 *   STEP, SEQ held        the drum track: the steps with any lane set full, the empty ones inside LEN dim (SEQ + a step clears it)
 * Main loop only (ui_leds); nothing here runs in the audio interrupt. After op_input.c (op_t0, pick_mod). */
static uint32_t keys_white(uint32_t n) { return keys_of(n < 16u ? (1u << n) - 1u : 0xFFFFu); }   /* the first n white keys (the tiles, the steps) */
static uint32_t key_w(int32_t w) { return w >= 0 && w < 16 ? 1u << white_key((uint32_t)w) : 0u; }
static void keys_mix(uint32_t *f, uint32_t *d)          /* GLO held: the MIX layer (op_layers.c glo_key) */
{
    uint32_t i;
    *d = keys_white(4) | key_w(15);
    for (i = 0; i < 4u; i++) {
        if (mx_muted(MXR_T1 + i))
            *f |= key_w((int32_t)i);
    }
#if FELUCCA_FILLS
    {
        static const uint8_t BLACK[4] = {1, 3, 5, 8};   /* black keys 1-4: the FX on / off (op_layers.c black_index) */
        *d |= key_w(8) | key_w(9);
        *f |= (fill_now ? key_w(8) : 0u) | (fill_arm || fill_bar_on ? key_w(9) : 0u);
        for (i = 0; i < 4u; i++) {
            *d |= 1u << BLACK[i];
            if (!fx_on(&trk[i]))
                *f |= 1u << BLACK[i];
        }
    }
#else
    for (i = 0; i < 4u; i++) {
        *d |= key_w(8 + (int32_t)i);
        if (!fx_on(&trk[i]))
            *f |= key_w(8 + (int32_t)i);
    }
#endif
}
static void keys_scenes(uint32_t *f, uint32_t *d, uint32_t blink)   /* LFO held, black 10: the scenes */
{
    uint32_t i;
    *d = keys_white(LAY_NSCN);
    for (i = 0; i < LAY_NSCN; i++)
        if ((live_sec == (int8_t)i && !arrangement_clock.running) || (live_req == (int8_t)i && blink))
            *f |= key_w((int32_t)i);
}
#if FELUCCA_PATTERNS
static void keys_pat_white(uint32_t *f, uint32_t *d, uint32_t blink)   /* the selected track's patterns */
{
    uint32_t sel = song.sel % NTRK;
    *d |= keys_white(PAT_N);
    *f |= (pat_cur[sel] < PAT_N ? key_w(pat_cur[sel]) : 0u) | (pat_req[sel] < PAT_N && blink ? key_w(pat_req[sel]) : 0u);
}
static void keys_pat(uint32_t *f, uint32_t *d, uint32_t blink)       /* LFO held: the patterns (black 10: the scenes), the black keys' jobs */
{
    uint32_t k, b;
    if (lay.pmod == 10u)
        keys_scenes(f, d, blink);
    else
        keys_pat_white(f, d, blink);
    for (k = 0; k < 27u; k++) {
        b = pat_black(k);
        if (b < 1u || b > 10u || (b >= 6u && b <= 8u))
            continue;                                   /* (6 7 8 were store, copy, clear: now SAVE + n, SHIFT + n) */
        *d |= 1u << k;
        if (b == song.sel % NTRK + 1u || b == lay.pmod)
            *f |= 1u << k;
    }
}
#endif
static uint32_t step_dim(void)                          /* STEP, the keys steps: the window's empty steps inside LEN */
{
    const track_t *t = tsel();
    uint32_t w, m = 0, len = trk_len(t);
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w;
        if (idx < len && !step_shown(t, idx))
            m |= 1u << w;
    }
    return keys_of(m);
}
/* SHIFT or SHIFT2 held alone (op_input.c shift_keys), past the hold time: the keys they name dim, the muted (SHIFT) or
 * the selected (SHIFT2) full */
static int keys_shifted(uint32_t held)
{
    uint32_t b = mute_mod(held) ? B_OCTDN : sel_mod(held) ? B_OCTUP : NB;
    return b != NB && fm1_ms - op_t0[panel.btn[b] % 14u] >= op_hold_ms();
}
static void keys_shift_lit(uint32_t *f, uint32_t *d)
{
    uint32_t k, mute = (uint32_t)mute_mod(fm1_in.buttons);
    for (k = 0; k < 27u; k++) {
        int32_t id = smute_key(k);
        uint32_t on;
        if (id < 0 || shift_step_key(k))
            continue;
        *d |= 1u << k;
        if (mute)
            on = mx_muted(mx_row_of_smute(id));
        else if (id == SMUTE_ACC || id < SMUTE_T0)      /* (a lane: the drum track's, picked) */
            on = is_drum(TSEL) && ui.lane && lane_selected() == (id == SMUTE_ACC ? DRUM_ACC : (uint32_t)id);
        else
            on = song.sel == (uint32_t)(id - SMUTE_T0);
        *f |= on << k;
    }
}
/* the keys' full (*f) and dim (*d) lights now: a bit per key, 0 = F3 .. 26 = G5 */
static void keys_lit(uint32_t *f, uint32_t *d)
{
    uint32_t blink = (fm1_ms / 250u) & 1u, k;
    *f = *d = 0;
    if (name_on()) {
        *f = name_leds(d);
        return;
    }
    if (lay.shown == LY_OPS) {
        *f = fm6_keys_lit();
        *d = FM6_KEYS_FN;
        return;
    }
    if (ui.scr == SCR_TEMPO) {
        *d = keys_white(16);                            /* (PLAY held: a white key taps the tempo) */
        return;
    }
    switch (lay.shown) {
    case LY_FX:
        *d = keys_white(16);
        *f = key_w(punch.req);
        return;
    case LY_MIX:
        keys_mix(f, d);
        return;
    case LY_SCALE:
        *d = (1u << 27) - 1u;
        for (k = 0; k < 27u; k++)
            if ((53u + k) % 12u == (uint32_t)trk[0].p[P_ROOT] % 12u)
                *f |= 1u << k;
        return;
#if FELUCCA_PATTERNS
    case LY_PAT:
        keys_pat(f, d, blink);
        return;
#endif
    case LY_PLAY:
        break;
    default:
        return;                                         /* (ERASE, REPEAT: play surfaces, dark) */
    }
    if (keys_shifted(fm1_in.buttons)) {
        if (ui.scr == SCR_STEP && step_keys(fm1_in.buttons)) {
            *f = step_leds(blink) & keys_white(16);     /* (STEP: its white keys stay its steps) */
            *d = step_dim();
        }
        keys_shift_lit(f, d);
        return;
    }
    if (ui.scr == SCR_PROJECT) {
        prj_keys_lit(f, d, blink);                      /* (the slots: used full, free dim, the selected one blinking) */
        return;
    }
    if (ui.scr == SCR_STEP) {
        *f = step_leds(blink);
        *d = step_keys(fm1_in.buttons) ? step_dim() : 0u;
        if (is_drum(TSEL))
            for (k = 0; k < 27u; k++)                   /* (the lanes' black keys: dim, the selected one full) */
                *d |= lane_key(k) ? 1u << k : 0u, *f |= lane_key(k) && lane_of(k) == lane_selected() ? 1u << k : 0u;
        return;
    }
#if FELUCCA_PATTERNS
    if (song_on_pat_row()) {
        keys_pat(f, d, blink);                          /* (the LFO layer's keys: the patterns, black 10 the scenes) */
        return;
    }
#endif
    if (keys_none())
        return;                                         /* (GLOBAL, SYSTEM, MASTER FX, SONG, SCOPE: no track, the keys do nothing: dark) */
    if (is_drum(TSEL) && !master_sel()) {               /* the drum track: the lanes' black keys dim, the selected one full; */
        uint32_t in, on = step_win(blink, &in);         /* the white keys its steps, as on STEP (they are steps on every page) */
        for (k = 0; k < 27u; k++)
            *d |= lane_key(k) ? 1u << k : 0u;
        *f |= 1u << key_of_lane(lane_selected()) | keys_of(on);
        *d |= keys_of(in & ~on);
    }
}
