/* SPDX-License-Identifier: GPL-3.0-only */
/* The keys' lights (the user, 2026-10-12, page organisation D10: "keys should lit up when they have a function in the
 * selected mode"), the same rule as the buttons' (op_input.c op_btn_avail): full = active or selected, dim = it has a
 * job in this mode, dark = none. Keys that only play notes, the default everywhere, stay dark: the ERASE and REPEAT
 * layers' keys too, and the FM6 layer's white (audition) keys. What each mode lights is what its tiles show
 * (op_laydraw.c tiles_fill):
 *   FM6 layer (ENV held)  its 10 black keys dim; the target edited and the voice mode full (op_fm6.c)
 *   FX layer              the 16 white keys dim (punch-in); the latched effect full
 *   GLO layer (MIX)       mute 1-4, solo 5-8, fill 9, bar 10, tap 16 and the black 1-4 (FX on / off) dim; muted,
 *                         soloed, a fill playing or armed, bypassed full
 *   SCL layer (KEY)       all 27 keys dim; the song's root full
 *   SAVE layer (SCENES)   the scenes dim; the playing one full, the queued one blinking (SONG's PATTERNS row: the
 *                         slots SAVE + n stores into)
 *   LFO layer (PATTERNS)  the 16 patterns dim, the playing one full, the queued one blinking; the 10 black keys dim,
 *                         the selected track's and the modifier held full
 *   TEMPO (PLAY held)     the white keys tap: dim
 *   STEP                  set steps full, the playhead blinking, empty steps inside LEN dim, past LEN dark
 *   SONG's PATTERNS row   the selected track's patterns, as the LFO layer's white keys
 *   NAME                  every key with a job dim, the key cycling its letters full
 *   the lane pick         the drum track, HOME held (on STEP: SEQ): the 16 lanes dim, the selected one full
 * Main loop only (ui_leds); nothing here runs in the audio interrupt. After op_input.c (op_t0, pick_mod). */
static uint32_t keys_white(uint32_t n)                  /* the first n white keys (the tiles, the steps) */
{
    uint32_t i, m = 0;
    for (i = 0; i < n && i < 16u; i++)
        m |= 1u << key_of_lane(i);
    return m;
}
static uint32_t key_w(int32_t w) { return w >= 0 && w < 16 ? 1u << key_of_lane((uint32_t)w) : 0u; }
static void keys_mix(uint32_t *f, uint32_t *d)          /* GLO held: the MIX layer (op_layers.c glo_key) */
{
    uint32_t i;
    *d = keys_white(8) | key_w(15);
    for (i = 0; i < 4u; i++) {
        if (trk[i].p[P_MUTE])
            *f |= key_w((int32_t)i);
        if ((song.solo >> i) & 1u)
            *f |= key_w(4 + (int32_t)i);
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
static void keys_scenes(uint32_t *f, uint32_t *d, uint32_t blink)   /* SAVE held: the scenes */
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
static void keys_pat(uint32_t *f, uint32_t *d, uint32_t blink)       /* LFO held: the patterns, the black keys' jobs */
{
    uint32_t k, b;
    keys_pat_white(f, d, blink);
    for (k = 0; k < 27u; k++) {
        b = pat_black(k);
        if (b < 1u || b > 10u)
            continue;
        *d |= 1u << k;
        if (b == song.sel % NTRK + 1u || b == lay.pmod)
            *f |= 1u << k;
    }
}
#endif
static uint32_t step_dim(void)                          /* STEP, the keys steps: the window's empty steps inside LEN */
{
    const track_t *t = TSEL;
    uint32_t w, m = 0, len = trk_len(t);
    for (w = 0; w < 16u; w++) {
        uint32_t idx = st.page * 16u + w;
        if (idx < len && !step_has(t, idx))
            m |= 1u << key_of_lane(w);
    }
    return m;
}
static int keys_picking(uint32_t held)                  /* the lane pick's modifier held past the hold time */
{
    uint32_t b = ui.scr == SCR_STEP ? B_SEQ : B_HOME;
    return pick_mod(held) && fm1_ms - op_t0[panel.btn[b] % 14u] >= op_hold_ms();
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
    case LY_SONG:
#if FELUCCA_PATTERNS
        if (song_on_pat_row()) {
            keys_pat_white(f, d, blink);                /* (SONG's PATTERNS row: SAVE + n stores into slot n) */
            return;
        }
#endif
        keys_scenes(f, d, blink);
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
    if (ui.scr == SCR_STEP) {
        *f = step_leds(blink);
        *d = step_keys(fm1_in.buttons) ? step_dim() : 0u;
        return;
    }
#if FELUCCA_PATTERNS
    if (song_on_pat_row()) {
        keys_pat_white(f, d, blink);                    /* (the keys launch the selected track's patterns) */
        return;
    }
#endif
    if (keys_picking(fm1_in.buttons)) {
        *d = keys_white(16);
        *f = 1u << key_of_lane(lane_selected());
    }
}
