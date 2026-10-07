/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, the layers (ui_layers.c), holds, SEQ step entry, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* button of each page family (TRACKS: HOME) */

static uint32_t cur_fam(void) { return cur_page()->fam; }
static uint32_t cur_btn(void)                      /* the button of the screen shown */
{
    if (cur_page()->scope == SC_SONG)
        return B_SAVE;
    if (cur_page()->scope == SC_DRUM)
        return B_SEQ;
    return FAM_BTN[cur_fam()];
}

/* PLAY flashes on every beat, as heard (the sound leaves ~9 ms after it is rendered) */
static int play_led(void)
{
    uint32_t p = clk_pos, lat = HALF_FRAMES * 3u / 2u * (uint32_t)song.g[G_BPM];
    if (!song.playing)
        return 0;
    p = p >= lat ? p - lat : p + BEAT_U - lat;
    return p < BEAT_U / 4u;
}

#if FELUCCA_KEYLIT
#include "keylit.c"            /* the notes the selected synth track plays, on its keys (Felucca 1.0.1, renebohne) */
#endif

#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
/* what sounds on track t, as keys (menu NOTES): the drum hits (each lights its key a few frames) or, on a synth
 * track, its steps, ARP note and held voices (keylit.c). SLOOP 2.3 shows them on every layer: lit where the keys
 * are notes, dim under the tiles (keys_notes_dim) */
static uint32_t keys_sounding(const track_t *t)
{
    uint32_t i, m = 0;
    if (lights_notes_off)
        return 0u;
    if (!is_drum(t))
        return keylit_play(t);
    for (i = 0; i < DRUM_LANES; i++)
        if (pad_lit[i])
            m |= 1u << key_of_white(i);
    return m;
}
static uint32_t scale_keys(uint32_t root_only)   /* SCL: the keys in the scale (or its roots only) */
{
    uint32_t i, m = 0, root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
    for (i = 0; i < 27u; i++) {
        uint32_t d = (53u + i - root + 120u) % 12u;
        if ((mask >> d) & 1u && (!root_only || !d))
            m |= 1u << i;
    }
    return m;
}
static uint32_t erase_lanes(const track_t *t)   /* EDIT erase, drum track: the sounds the pattern holds */
{
    uint32_t i, m = 0;
    if (is_drum(t))
        for (i = 0; i < trk_len(t); i++) {
            uint32_t l, d = dstep_mask(&t->dstep[i]);
            for (l = 0; d; l++, d >>= 1)
                if (d & 1u)
                    m |= 1u << key_of_white(l);
        }
    return m;
}
#endif

/* the keys' lights: what the layer held does, else the keys down and the drum hits */
static uint32_t keys_lit(void)
{
    uint32_t m = 0, i, blink = (fm1_ms / 125u) & 1u;
    track_t *t = TSEL;
    switch (ui.layer) {
    case LY_FX:
        return punch.req >= 0 ? 1u << key_of_white((uint32_t)punch.req) : 0u;
    case LY_STEP: {                                /* the steps that play; the playhead blinks */
        uint32_t len = trk_len(t);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ui.step_page * 16u + i, on;
            if (idx >= len)
                continue;
            on = is_drum(t) ? dstep_has(&t->dstep[idx], pen_lane) : step_on(&t->step[idx]);
            if (song.playing && idx == t->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes;
    }
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    case LY_SCALE:                                 /* the keys in the scale; the root blinks (NOTES: the scale goes */
        if (!lights_notes_off)                     /* dim, keys_notes_dim; what sounds is lit) */
            return (blink ? scale_keys(1) : 0u) | keys_sounding(t) | fm1_in.notes;
        return scale_keys(0) & ~(blink ? 0u : scale_keys(1));
#else
    case LY_SCALE: {                               /* the keys in the scale; the root blinks */
        uint32_t root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
        for (i = 0; i < 27u; i++) {
            uint32_t d = (53u + i - root + 120u) % 12u;
            if ((mask >> d) & 1u && (d || blink))
                m |= 1u << i;
        }
        return m;
    }
#endif
    case LY_MIX:                                   /* tracks heard: 1..4; soloed: 5..8; FX on: 9..12; tap: the beat */
        for (i = 0; i < 4u; i++) {
            if (!trk_silent(&trk[i]))
                m |= 1u << key_of_white(i);
            if ((song.solo >> i) & 1u)
                m |= 1u << key_of_white(4u + i);
#if FELUCCA_FILLS
            if (fx_on(&trk[i]))                    /* (the FX bypass: black keys 1..4) */
                m |= 1u << (i < 3u ? 1u + 2u * i : 8u);
#else
            if (fx_on(&trk[i]))
                m |= 1u << key_of_white(8u + i);
#endif
        }
#if FELUCCA_FILLS
        if (fill_now)                              /* a fill: 9; armed / on: 10 */
            m |= 1u << key_of_white(8);
        if (fill_arm || fill_bar_on)
            m |= 1u << key_of_white(9);
#endif
        if (play_led())
            m |= 1u << key_of_white(15);
        return m;
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    case LY_ERASE:                                 /* the sounds the pattern holds (NOTES: dim, the hits lit) */
        return (!lights_notes_off ? keys_sounding(t) : erase_lanes(t)) | fm1_in.notes;
#else
    case LY_ERASE:                                 /* the sounds the pattern holds */
        if (is_drum(t))
            for (i = 0; i < trk_len(t); i++) {
                uint32_t l, d = dstep_mask(&t->dstep[i]);
                for (l = 0; d; l++, d >>= 1)
                    if (d & 1u)
                        m |= 1u << key_of_white(l);
            }
        return m | fm1_in.notes;
#endif
    case LY_OPS:                                   /* the black key of what is edited, MONO / POLY */
        return fm6k_keys_lit() | fm1_in.notes;
    default:
        break;
    }
#if FELUCCA_DRUM_STEP
    if (kb_grid) {                                 /* the DRUMS grid page: the sound's steps of this page (SLOOP 2.4) */
        uint32_t len = trk_len(TDRUM), b0 = drum_cursor / 16u * 16u;
        for (i = 0; i < 16u; i++) {
            uint32_t idx = b0 + i, on;
            if (idx >= len)
                continue;
            on = dstep_has(&TDRUM->dstep[idx], drum_lane);
            if (song.playing && idx == TDRUM->seq_idx)
                on = !on || blink;
            if (on)
                m |= 1u << key_of_white(i);
        }
        return m | fm1_in.notes;
    }
#endif
    m = fm1_in.notes;
    if (is_drum(t))                                /* the drum track: each hit lights its key */
        for (i = 0; i < DRUM_LANES; i++)
            if (pad_lit[i])
                m |= 1u << key_of_white(i);
#if FELUCCA_KEYLIT
#if FELUCCA_LIGHTS
    if (!is_drum(t) && !lights_notes_off)
#else
    if (!is_drum(t))
#endif
        m |= keylit_play(t);                       /* a synth track: the notes it plays */
#endif
    return m;
}

/* the keys' landmarks, dim: the first key of each row of the 4 x 4 grid on screen (white keys
 * 1, 5, 9, 13) while a layer is held, and on the drum track (its 16 sounds, 4 x 4 as on KIT) */
static uint32_t keys_guide(void)
{
    if ((ui.layer == LY_PLAY || ui.layer == LY_OPS) && !is_drum(TSEL))
        return 0u;
    return 1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12);
}

#if FELUCCA_LIGHTS
/* menu NOTES, on the layers whose keys are tiles (FX effects, SEQ steps, GLO mute / solo): what sounds glows
 * dimly under the tiles, which keep their full light. SCL (the scale) and EDIT on the drum track (the sounds of
 * the pattern): those glow, and what sounds is lit (keys_lit) */
static uint32_t keys_notes_dim(void)
{
#if FELUCCA_KEYLIT
    if (lights_notes_off)
        return 0u;
    switch (ui.layer) {
    case LY_FX:
    case LY_STEP:
    case LY_MIX:
        return keys_sounding(TSEL);
    case LY_SCALE:
        return scale_keys(0);
    case LY_ERASE:
        return erase_lanes(TSEL);
    default:
        return 0u;
    }
#else
    return 0u;
#endif
}
#endif

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t k, c, keys, guide;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[ui.layer != LY_PLAY ? LAYER_BTN[ui.layer] : cur_btn()],
            ly_lock == LY_PLAY || ((fm1_ms / 300u) & 1u) != 0u || (fm1_in.buttons & ly_bit[ly_lock % LY_COUNT]) != 0u);   /* locked: blinks */
#if FELUCCA_PUNCH_LATCH
    led_put(nl, panel.btn[B_FX], punch.req >= 0);  /* a latched punch effect plays */
#endif
#if FELUCCA_REC_MODES
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on) ||
                                      (ci_on && ci_u % BEAT_U < BEAT_U / 4u));   /* (the count-in's beats) */
#else
    led_put(nl, panel.btn[B_PLAY], play_led() || (song.playing && !song.rec && ft_on));
#endif
    led_put(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && ((fm1_ms / 125u) & 1u)) ||
                                     (ui.hold_kind == 1u && ((fm1_ms / 60u) & 1u)));   /* blinks: armed; fast: clearing */
    if (is_drum(TSEL)) {                           /* the drum track: OCT- / OCT+ lit while ghost / hard */
        led_put(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        led_put(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
#if FELUCCA_LIGHTS
    {   /* menu LIGHTS / KEYS: the backlight layer (every button, the C or white keys), under what is lit or dim */
        uint8_t bl[FM1_NCOL] = {0};
        uint32_t back;
        keys = keys_lit();
        guide = (keys_guide() | keys_notes_dim()) & ~keys;
        back = lights_keys_mask() & ~keys & ~guide;
        for (k = 0; k < 27u; k++) {
            led_put(nl, 14u + k, (int)((keys >> k) & 1u));
            led_put(dl, 14u + k, (int)((guide >> k) & 1u));
            led_put(bl, 14u + k, (int)((back >> k) & 1u));
        }
        if (lights_lvl)                            /* every button glows, the lit ones stay full */
            for (k = 0; k < NB; k++)
                led_put(bl, panel.btn[k], 1);
        for (c = 0; c < FM1_NCOL; c++) {
            fm1_led[c] = nl[c];
            fm1_led_dim[c] = dl[c];
            fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
        }
        fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
    }
#else
    keys = keys_lit();
    guide = keys_guide() & ~keys;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((keys >> k) & 1u));
        led_put(dl, 14u + k, (int)((guide >> k) & 1u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
#endif
}

/* ---------------------------------------------------------- input --- */
/* knob acceleration by turn speed (knob_accel.h, from X0X 61654ba; FELUCCA_KNOB_ACCEL); range 0 = a list */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
#if FELUCCA_KNOB_ACCEL
    return knob_accel(dt / (1000u * FM1_TICKS_PER_US), s, range);
#else
    (void)dt; (void)range;
    return s;
#endif
}
/* the range a knob accelerates over: none for lists (engines, voices, modes: one entry a detent) */
static int32_t accel_range(const param_desc_t *d)
{
    return d->fmt == F_ENUM || d->fmt == F_ONOFF ? 0 : d->max - d->min;
}

/* TRACKS page: KNOB 1 SWING (the groove of every track, MPC 50..75 %), 2 LEVEL (0 = mute; the drum
 * track: GLO > DRUMS LEVEL), 3 LEN of its pattern, 4 PAN. A track muted with MUTE (GLO + key, the
 * editor): the first turn of KNOB 2 unmutes it */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 0:
        vp = &song.g[G_SWING];
        d = &GP[G_SWING];
        break;
    case 1:
        if (t->p[P_MUTE]) {
            t->p[P_MUTE] = 0;
            return;
        }
        vp = is_drum(t) ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        d = is_drum(t) ? &GP[G_DRLVL] : &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_SLEN];
        d = &TP[P_SLEN];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, accel_range(d)), d->min, d->max);
}

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    if (is_drum(TSEL))
        return;                                           /* (the drum track: its grid) */
#if FELUCCA_CHANCE
    switch (cur_page()->id[slot]) {                       /* (STEP: the column; STEP 2: 0, CHANCE) */
    case STEP_ID_CHANCE:                                  /* CHANCE: 5 % a detent */
        if (st->n && st->time == ST_NOTE)
            step_set_chance(st, (uint32_t)clamp((int32_t)step_chance(st) + steps * (int32_t)CH_STEP, 0, 100));
        break;
    case 0xFF:
        break;
#else
    switch (slot) {
#endif
    case 0:                                               /* STEP: the cursor */
        cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

/* a new project: every track empty, the default sounds, 90 BPM (TOOLS > NEW) */
static void project_new(void)
{
    uint32_t i, sess = (undo_sess += 4u) | 3u;        /* (one session: the history undoes NEW at once) */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        undo_mark(t, sess);
        fm1_irq_off();
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, trk_def_engine(i));
            apply_preset_to(t, trk_def_preset(i));
        }
        fm1_irq_on();
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_DRCH && i != G_VIEW && i != G_SYNC)
            song.g[i] = GP[i].def;
    song.solo = 0;
    song.octave = 0;
#if BP_SET_ANY
    bps_defaults();                                       /* (bp_set.c) */
#endif
    rev_defaults();                                       /* the first reverb algorithm built (rev_type.c) */
    sync_reload = 1;
    ui.force = 1;
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (is_drum(TSEL) && !page_for_drum(pg))
        return;                                           /* "DRUM TRACK": nothing to edit here */
#if FELUCCA_UART
    if (pg->scope == SC_GLOBAL && id == G_MIDI) {         /* the MIDI column: which input it shows */
        if (steps)
            ui.midi_view = steps > 0;
        return;
    }
#endif
#if FELUCCA_MISSING_WARN
    if (pg->scope == SC_GLOBAL && id == G_MISS) {         /* TOOLS > MISS: the items one by one (miss.c) */
        if (steps)
            miss_knob(steps);
        return;
    }
#endif
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
#if FELUCCA_MOTION
    if (pg->scope == SC_MOTION) {                         /* PLAY: on / off; CLEAR: one detent arms, a second acts */
        if (slot == 0u && steps)
            motion_set_enabled(TSEL, steps > 0);
        if (slot == 3u && steps > 0) {
            if (ui.arm != 0xD3u) {
                ui.arm = 0xD3u;
                ui.arm_t = 90;
                ui_say("AGAIN: ", "CLEAR");
                return;
            }
            ui.arm = 0;
            motion_clear(TSEL);
            ui_message("MOTION CLEARED");
        }
        return;
    }
#endif
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u && !is_drum(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((uint32_t)(((int32_t)cur + steps % (int32_t)total + (int32_t)total) % (int32_t)total));
        } else if (slot == 1u && !is_drum(TSEL)) {
#if FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID
        {
            uint32_t e = TSEL->eng_req;
            do                                            /* (stepping over the numbers kept free: engines.c) */
                e = (e + (steps > 0 ? 1u : NENGINES - 1u)) % NENGINES;
            while (eng_free(e));
            select_engine(e);
        }
#else
            select_engine((TSEL->eng_req + (steps > 0 ? 1u : NENGINES - 1u)) % NENGINES);
#endif
        }
        return;
    }
#if FELUCCA_SNAPSHOTS
    if (pg->graph == GR_SNAP) {                           /* KNOB 1 the slot; LOAD / CLEAR / SAVE: GO buttons */
        static const char *const SN_GO[3] = {"LOAD", "CLEAR", "SAVE"};
        if (slot == 0u) {
            ui.sslot = (uint8_t)clamp((int32_t)ui.sslot + steps, 0, (int32_t)sn_ui_n() - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xF0u + slot) {                     /* one detent arms (the confirmation: LOAD replaces the */
            ui.arm = (uint8_t)(0xF0u + slot);             /* work, SAVE over a used slot), a second within ~1.5 s acts */
            ui.arm_t = 90;
            ui_say("AGAIN: ", SN_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        sn_ui(slot - 1u, ui.sslot);
        return;
    }
#endif
    if (pg->graph == GR_USER) {                           /* KNOB 1 slot; LOAD / ERASE / SAVE: GO buttons */
        static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
        if (slot == 0u) {
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE0u + slot) {                     /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xE0u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", UP_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        up_ui(slot - 1u, ui.uslot);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min || PARAM_HIDDEN(d))   /* ("-": not shown, not edited) */
        return;
    v = param_step(d, *vp, accel(EN_K1 + slot, steps, accel_range(d)));   /* (lists: past a mode not built) */
    *vp = (int16_t)v;
#if FELUCCA_PLOCK
    if ((pg->scope == SC_TRACK || pg->scope == SC_ENGINE) && vp >= TSEL->p && vp < TSEL->p + P_COUNT &&
        lock_ok(TSEL, (uint32_t)(vp - TSEL->p)))
        lock_par = (uint8_t)(vp - TSEL->p);               /* the SEQ layer's lock parameter: the last one touched */
#endif
#if FELUCCA_MOTION
    if (pg->scope == SC_TRACK || pg->scope == SC_ENGINE) {   /* recording: a step event (motion.c) */
        motion_knob(TSEL, (uint32_t)(vp - TSEL->p), v);
        if (motion_full) {
            motion_full = 0;
            ui_message("MOTION FULL");
        }
    }
#if FELUCCA_MACROS
    if (pg->scope == SC_MACRO) {                          /* a macro: the drum track's motion (macro_ui.c) */
        mac_motion((uint32_t)(vp - TDRUM->p), v);
        if (motion_full) {
            motion_full = 0;
            ui_message("MOTION FULL");
        }
    }
#endif
#endif
#if DL_UI
    if (pg->scope == SC_DSND) {                           /* a SOUND page: the sound picked (ui_drums.c) */
        dsnd_set(id, v, steps);
        return;
    }
#endif
    if (!v)
        return;
#if FELUCCA_ENG_ACID
    if (pg->scope == SC_BPSET && id == BPS_GGO) {         /* ACID GEN > GO: one detent arms, a second writes */
        *vp = 0;
        if (ui.arm != 0xD7u) {
            ui.arm = 0xD7u;
            ui.arm_t = 90;
            ui_say("AGAIN: ", "NEW LINE");
            return;
        }
        ui.arm = 0;
        undo_mark(TSEL, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        acid_generate(TSEL, (uint32_t)bp_set[BPS_GDENS], (uint32_t)bp_set[BPS_GACC], (uint32_t)bp_set[BPS_GSLD],
                      fm1_ms * 2654435761u ^ rng());
        fm1_irq_on();
        ui_message("NEW LINE");
        return;
    }
#endif
    if (pg->scope == SC_GLOBAL && (id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND || id == G_NEWPRJ) &&
        ui.arm != id) {                                   /* one detent arms, a second one within ~1.5 s acts */
        *vp = 0;
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    if (pg->scope != SC_GLOBAL)
        return;
    switch (id) {                                         /* GO buttons: act, then back to 0 */
    case G_LOAD:
        *vp = 0;
        project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        undo_mark(TSEL, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(TSEL);
        fm1_irq_on();
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        set_engine(TSEL->eng_req);                              /* engine defaults + its first preset */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    case G_NEWPRJ:
        *vp = 0;
#if FELUCCA_ARRANGER
        if (song.playing && arrangement_enabled) {      /* (the song's stop would bring the old loop back) */
            ui_message("STOP THE SONG FIRST");
            break;
        }
#endif
        project_new();
        ui_message("NEW PROJECT");
        break;
    default:
        break;
    }
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4, MONO:
 * the last one) become the cursor step; releasing all keys moves on. Not while recording or armed:
 * the keys record live then (they would be written twice) */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    if (is_drum(t) || song.rec || rec_wait || ft_on)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;
            st->lvl = st->rat = 0;
            st->time = ST_NOTE;
        }
        if (t->p[P_VOICE]) {
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !fm1_in.notes)
        cursor_set(ui.cursor + 1);
}

/* HOME: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* a layer button tapped (pressed and let go, nothing touched): its pages, as before the layers */
static void layer_tap(uint32_t layer)
{
    switch (layer) {
    case LY_FX:
        open_family(FAM_FX);
        break;
    case LY_ERASE:
    case LY_STEP:
#if DL_UI
        if (layer == LY_ERASE && is_drum(TSEL)) {         /* the drum track: EDIT is its SOUND pages (ui_drums.c) */
            open_family(FAM_EDIT);
            break;
        }
#endif
        if (on_drum_page()) {                             /* DRUMS: GRID <-> KIT */
            drum_page = (uint8_t)((drum_page + 1u) % 2u);
            ui.force = 1;
            break;
        }
        if (cur_page()->scope == SC_TRK && is_drum(TSEL)) {
            studio_open(SC_DRUM);
            break;
        }
        if (layer == LY_ERASE && song.seq_mode && cur_page()->scope == SC_STEP && !is_drum(TSEL)) {
            undo_mark(TSEL, (undo_sess += 4u) | 3u);      /* STEP page: EDIT clears the step */
            step_clear(&TSEL->step[ui.cursor]);
            cursor_set(ui.cursor + 1);
            ui_message("STEP CLEARED");
            break;
        }
        open_family(layer == LY_STEP ? FAM_SEQ : FAM_EDIT);
        break;
    case LY_ROLL:
        open_family(FAM_ARP);
        break;
    case LY_SCALE:
        open_family(FAM_SCL);
        break;
    case LY_MIX:
        open_family(FAM_GLO);
        break;
    case LY_OPS:                                          /* ENV tapped on an FM6 track: the editor, its next page */
        fm6k_tap();
        break;
    case LY_SONG:                                         /* SAVE tapped: TRACKS -> the song, else the SAVE pages */
        if (on_song_page())
            arrangement_save();
        else if (cur_page()->scope == SC_TRK)
            studio_open(SC_SONG);
        else if (on_drum_page())
            studio_open(SC_SONG);
        else
            open_family(FAM_SAVE);
        break;
    default:
        break;
    }
}

/* a layer locked open: a layer button held + HOME tapped. The layer stays with the button let go (both
 * hands free for the keys and the knobs); any other button but PLAY, REC and OCT- / OCT+ lets it go
 * (and does only that: its press is eaten) */
static uint8_t home_eat;                                  /* HOME pressed to unlock: its tap is eaten */
static void layer_unlock(void)
{
    if (ly_lock != LY_PLAY) {
        ly_lock = LY_PLAY;
        ui.force = 1;
    }
}

#if FELUCCA_LAYER_QUIET
/* #39 (after Felucca 1.0.2, hugelton/Felucca db70550, ui_layer.c layer_knobs_quiet, by Leo Kuroshita,
 * GPL-3.0-only): KNOB 1..4 belong to no page while a layer lets go: the frame its button is let go (the turns read
 * then were made with it held: a combo, no tap) and LY_QUIET_MS after a layer that opened or was used closed.
 * Before, they edited the page under the layer (ARP let go while turning: the ARP page opened, MODE UP) */
#define LY_QUIET_MS 250u                                  /* (a button pressed ends it sooner) */
static uint8_t ly_quiet;
static uint32_t ly_quiet_t;
static uint32_t knobs_drop(void)                          /* KNOB 1..4's turns taken and dropped: any? */
{
    uint32_t k, any = 0;
    for (k = 0; k < 4u; k++)
        any |= panel_enc(EN_K1 + k) != 0;
    enc_hold |= 15u << EN_K1;                             /* #102: none read again in this pass */
    return any;
}
#endif

/* the layers, once a frame: which one is held (or locked), the taps on release, its keys and knobs.
 * Returns 1 while one is held or locked (the page does not take the knobs then) */
static int layers_input(uint32_t note_edges, uint32_t *pressed, uint32_t home)
{
    static uint8_t down[LY_COUNT], used[LY_COUNT];
    static uint32_t t0[LY_COUNT];
    uint32_t l, now = fm1_ms, held = LY_PLAY, eat = 0;
    if (ly_lock != LY_PLAY) {
        uint32_t keep = 1u << panel.btn[B_PLAY] | 1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
        eat = *pressed & ~keep;
        if (eat) {
            layer_unlock();
            if (eat & 1u << panel.btn[B_HOME])
                home_eat = 1;
            *pressed &= ~eat;
        }
    }
#if FELUCCA_LAYER_QUIET
    if (*pressed)
        ly_quiet = 0;                                     /* a button pressed since: the hand has moved on */
#endif
    for (l = LY_FX; l < LY_COUNT; l++) {
        uint32_t d = (fm1_in.buttons & ly_bit[l]) != 0u && (l != LY_OPS || ly_ops_on);
        if (d && !down[l]) {
            t0[l] = now;
            used[l] = (uint8_t)((eat & ly_bit[l]) != 0u);  /* (the press that unlocked: not a tap) */
        }
        if (d && note_edges)
            used[l] = 1;                                  /* a key while held: not a tap */
#if FELUCCA_LAYER_QUIET
        if (!d && down[l]) {
            if (knobs_drop())
                used[l] = 1;                              /* a knob turned as it was let go: a combo, no tap */
            if (used[l] || ui.layer == l) {
                ly_quiet = 1;                             /* (it opened or was used: the quiet window) */
                ly_quiet_t = now;
            }
        }
#endif
        if (!d && down[l] && !used[l] && now - t0[l] < TAP_MS && !ui.menu && !ui.confirm)
            layer_tap(l);
        down[l] = (uint8_t)d;
        if (d && held == LY_PLAY)
            held = l;
    }
    if (held != LY_PLAY && home == BT_TAP && !home_eat) {  /* held + HOME: locked open */
        ly_lock = (uint8_t)held;
        used[held] = 1;
        ui.layer = (uint8_t)held;
        ui.force = 1;
    }
    if (held == LY_PLAY && ly_lock != LY_PLAY) {
        held = ly_lock;
        used[held] = 1;
    }
    punch.hold = (uint8_t)(held == LY_FX);
#if FELUCCA_PUNCH_LATCH
    if (held == LY_FX && (*pressed & 1u << panel.btn[B_OCTDN])) {
        punch.keybit = 0;                                 /* FX + OCT-: the latched effect off (punch.c) */
        punch.req = -1;
        used[held] = 1;
    }
#endif
    if (held != LY_PLAY && held != ui.layer && ui.layer != LY_PLAY)
        ui.layer = (uint8_t)held;                         /* (from one layer straight to another) */
    if (held == LY_PLAY) {
#if FELUCCA_LAYER_QUIET
        if (ly_quiet && now - ly_quiet_t < LY_QUIET_MS)
            knobs_drop();                                 /* the hand still turning: not the page's */
        else
            ly_quiet = 0;
#endif
        while (lk_r != lk_w) {                            /* a key let go after its layer: its release only */
            uint32_t e = lk_q[lk_r % LKQ];
            lk_r++;
#if FELUCCA_DRUM_STEP
            if ((e >> 8) == KB_GRID) {                    /* the DRUMS grid page: a step key (ui_drumstep.c) */
                if (((e >> 7) & 1u) && grid_keys_on())
                    grid_key(e & 31u);
                continue;
            }
#endif
            if (!((e >> 7) & 1u))
                layer_key(e >> 8, e & 31u, 0);
        }
#if FELUCCA_DRUM_STEP
        kb_grid = (uint8_t)grid_keys_on();                /* (seq.c: the keys are the grid's steps) */
#endif
        if (ui.layer != LY_PLAY) {
            ui.layer = LY_PLAY;
            ui.step_held = 0;
            ui.force = 1;                                 /* the page comes back */
        }
        if (ui.step_sess)
            undo_end(ui.step_sess);                       /* (the hold's session: one level now) */
        ui.step_sess = 0;
        return 0;
    }
    ui.layer_used = used[held];
    while (lk_r != lk_w) {                                /* the keys of SEQ, SCL, GLO (seq.c) */
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        used[held] = 1;
        layer_key(e >> 8, e & 31u, (e >> 7) & 1u);
    }
    layer_knobs(held);
    if (ui.layer_used)
        used[held] = 1;
    if (ui.layer == LY_PLAY && (used[held] || now - t0[held] >= SHOW_MS)) {
        ui.layer = (uint8_t)held;                         /* show it (not for a quick tap) */
        if (held == LY_STEP && (uint32_t)ui.step_page * 16u >= trk_len(TSEL))
            ui.step_page = 0;
    }
#if FELUCCA_DRUM_STEP
    if (held == LY_STEP)
        ds_follow_tick();                                 /* the page follows the playhead (a page key turns it off) */
#endif
    if (held == LY_ERASE) {                               /* EDIT + OCT- / OCT+: undo / redo */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev;
        prev = b;
        if (press) {
            used[held] = 1;
            undo_say(!(press & ob));
        }
    }
    if (held == LY_OPS && fm6k_layer_oct())                /* ENV + OCT- / OCT+: the FM6 editor's page */
        used[held] = 1;
#if SEC_LOGGED
    if (held == LY_SONG) {                                /* SAVE + OCT- / OCT+: the bank of sections (A..D, E..H, ..) */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP], nb = FELUCCA_SECTIONS / 4u;
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev;
        prev = b;
        if (press) {
            char m[12] = "BANK A-D";
            used[held] = 1;
            sec_bank = (uint8_t)((sec_bank + ((press & pb) ? 1u : nb - 1u)) % nb);
            m[5] = (char)('A' + 4u * sec_bank), m[7] = (char)('D' + 4u * sec_bank);
            ui_message(m);
            ui.force = 1;
        }
    }
#endif
    if (held == LY_STEP) {                                /* SEQ + OCT- / OCT+: the page */
        uint32_t ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
        static uint32_t prev;
        uint32_t b = fm1_in.buttons & (ob | pb), press = b & ~prev, pages = (trk_len(TSEL) + 15u) / 16u;
        prev = b;
        if (press) {
            used[held] = 1;
#if SL24_STEPX                                            /* (SLOOP 2.4: a step held: OCT- clears its nudge, locks,
                                                           * fill; OCT+ cycles its fill condition) */
            if ((press & ob) && ui.step_held)
                steps_held_clear();
            else
#endif
#if FELUCCA_FILLS
            if ((press & pb) && ui.step_held)
                steps_held_fill();
            else
#endif
#if FELUCCA_DRUM_STEP
            if (song.sel == TRK_DRUM)
            {
                ui.step_page = (uint8_t)((ui.step_page + ((press & pb) ? 1u : pages - 1u)) % pages);
                ds_follow_hand();
            }
            else
#endif
            ui.step_page = (uint8_t)((ui.step_page + ((press & pb) ? 1u : pages - 1u)) % pages);
        }
    }
    return 1;
}

#if FELUCCA_REC_MODES
/* the REC screen while armed (ui_studio.c rec_screen_draw; FELUCCA_REC_MODES, from SLOOP 2.3 by isod89,
 * GPL-3.0): KNOB 1 MODE free / tempo (an empty project), KNOB 2 the length of the selected track (1, 2 or 4
 * bars), KNOB 3 START note / count; MODE and START are settings of the FM-1 (the settings record: saved once
 * stopped) */
static void rec_knobs(void)
{
    static const uint8_t LENS[3] = {16u, 32u, 64u};
    int32_t s;
    uint32_t empty = (uint32_t)project_empty(), tempo = !empty || rec_tempo;
    if ((s = panel_enc(EN_K1)) != 0 && empty) {
        rec_tempo = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 0, ui.hot_t = PH_HOT;
        PH_SLOT("REC", 0u);
    }
    if ((s = panel_enc(EN_K2)) != 0 && tempo) {
        track_t *t = TSEL;
        int32_t len = t->p[P_SLEN], i, to = len;
        if (s > 0) {
            for (i = 0; i < 3; i++) if (LENS[i] > len) { to = LENS[i]; break; }
        } else {
            for (i = 2; i >= 0; i--) if (LENS[i] < len) { to = LENS[i]; break; }
        }
        t->p[P_SLEN] = (int16_t)to;
        ui.hot_col = 1, ui.hot_t = PH_HOT;
        PH_SLOT("REC", 1u);
    }
    if ((s = panel_enc(EN_K3)) != 0 && tempo) {
        rec_count = (uint8_t)(s > 0);
        settings_later = 1;
        ui.hot_col = 2, ui.hot_t = PH_HOT;
        PH_SLOT("REC", 2u);
    }
    panel_enc(EN_K4);
}
#endif

/* REC: acts on the press (no lag). Held 0.7 s the press is undone and a ring fills: held on to the
 * end, the selected track is cleared (EDIT + OCT- brings it back); let go before, nothing happens.
 * (SAVE is the SONG layer: ui_layers.c; tapped, layer_tap) */
static void holds_input(uint32_t pressed, uint32_t now_ms)
{
    static uint8_t prev_rec, prev_wait, eaten, rec_on, save_on;   /* *_on: pressed, *_t0 its time */
    static uint32_t rec_t0, save_t0;
    uint32_t rb = 1u << panel.btn[B_REC], sb = 1u << panel.btn[B_SAVE];
    uint32_t rec_down = (fm1_in.buttons & rb) != 0u, save_down = (fm1_in.buttons & sb) != 0u;
    if (on_song_page())
        rec_down = 0, pressed &= ~rb;                     /* (the song page: REC stores a section) */
    /* REC */
    if (pressed & rb) {
        eaten = (uint8_t)ft_owns_press();                 /* (the press closed a free take: seq.c) */
        prev_rec = song.rec;
        prev_wait = rec_wait;
        rec_t0 = now_ms;
        rec_on = 1;
        rec_toggle();
    }
    if (rec_down && rec_on && !eaten && !ui.hold_kind && now_ms - rec_t0 >= 700u) {
        song.rec = prev_rec;                              /* a hold: the press is undone */
        rec_wait = prev_wait;
        ui.msg_t = 0;
        ui.hold_kind = 1;
        ui.hold_t0 = now_ms;
        ui.hold_trk = song.sel;
        ui.force = 1;
    }
    if (ui.hold_kind == 1u && rec_down && now_ms - ui.hold_t0 >= HOLD_CLEAR_MS) {
        track_t *t = &trk[ui.hold_trk % NTRK];
        char b[12] = "1 CLEARED";
        undo_mark(t, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        track_defaults_steps(t);
        t->nheld = 0;                                     /* and the latched arp chord */
        t->arp_phys = 0;
        fm1_irq_on();
        b[0] = (char)('1' + ui.hold_trk);
        ui_say("TRACK ", b);
        ui.hold_kind = 0;
        rec_on = 0;                                       /* (until REC is up: nothing more) */
        ui.force = 1;
    }
    if (!rec_down) {
        if (ui.hold_kind == 1u) {
            ui.hold_kind = 0;                             /* let go before the end: nothing */
            ui.force = 1;
        }
        rec_on = 0;
    }
    (void)sb, (void)save_down, (void)save_on, (void)save_t0;   /* (SAVE is a layer now: ui_layers.c) */
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    int32_t s;
    int layered;
    enc_hold = 0;                                       /* (panel.c: every knob readable again this pass) */
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    if (pressed)
        PH_CLEAR();                                     /* a button: no help line (only a knob turning shows one) */
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            layer_unlock();
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu) {                                      /* HOME / REC taps do nothing here */
        punch.hold = 0;
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    ly_ops_on = (uint8_t)fm6k_sel();                     /* ENV: the FM6 editor's layer, or its pages */
    if (!ly_ops_on && ly_lock == LY_OPS)
        layer_unlock();                                 /* (locked open, then the track or its engine changed) */
    layered = layers_input(notes, &pressed, home);
    fm6k_follow_layer();
    if (home_eat && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {   /* (the HOME that unlocked: let go) */
        if (home == BT_TAP)
            home = BT_NONE;
        home_eat = 0;
    }
    pressed &= ~(ly_bit[LY_FX] | ly_bit[LY_ERASE] | ly_bit[LY_ROLL] | ly_bit[LY_STEP] | ly_bit[LY_SCALE] | ly_bit[LY_MIX] |
                 ly_bit[LY_SONG] | (ly_ops_on ? ly_bit[LY_OPS] : 0u));
    holds_input(pressed, fm1_ms);
    pressed &= ~((on_song_page() ? 0u : 1u << panel.btn[B_REC]) | (1u << panel.btn[B_SAVE]));
    if (layered || ui.hold_kind) {                      /* a layer / a hold: the rest waits */
        if ((s = panel_enc(EN_SELECT)) != 0)             /* (the tempo always; BPM LOCK: GLO's own, ui_layers.c) */
            tempo_knob(s);
        panel_enc(EN_ALGO);                             /* (track and sound wait: no jump afterwards) */
        panel_enc(EN_PRESET);
        if (ui.hold_kind)
            for (k = 0; k < 4u; k++)
                panel_enc(EN_K1 + k);
        pressed &= ~((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));   /* (undo, pages, ghost / hard) */
        if (!(pressed & (1u << panel.btn[B_PLAY])))
            return;
        pressed &= 1u << panel.btn[B_PLAY];             /* PLAY still plays */
        enc_hold = (1u << NE) - 1u;                     /* #102: the knobs the layer took are not read again this
                                                         * pass (a detent counted since would go to the page) */
    }
#if FELUCCA_REC_MODES
    if (rec_wait && !ft_on && !ci_on) {                 /* the REC screen, armed: how it records */
        rec_knobs();                                    /* (tempo and sound still work; the track too: */
    } else if (rec_wait || ft_on) {                     /* the arm follows) */
        for (k = 0; k < 4u; k++)                        /* a take / the count-in: KNOB 1..4 edit nothing */
            panel_enc(EN_K1 + k);
    }
#else
    if (rec_wait || ft_on) {                            /* the REC screen is up: KNOB 1..4 edit nothing */
        for (k = 0; k < 4u; k++)                        /* hidden (tempo and sound still work; the track */
            panel_enc(EN_K1 + k);                       /* too, while armed: the arm follows) */
    }
#endif
#if FELUCCA_ARRANGER
    if (on_song_page()) {
        song_screen_input(pressed, home);
        return;
    }
    if (on_drum_page()) {
        drum_screen_input(pressed, home);
        return;
    }
#endif
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        go_home();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
#if FELUCCA_ARRANGER
            if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
                ui_message("EMPTY SECTION: REC");
                break;
            }
#endif
            if (!ft_owns_press())                       /* (it closed or dropped a free take: seq.c) */
                transport_req = song.playing ? 2 : 1;
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (is_drum(TSEL))
                break;                                  /* the drum track: ghost / hard while held (seq.c) */
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        case B_ENV:
            open_family(FAM_ENV);
            break;
        case B_LFO:
            open_family(FAM_LFO);
            break;
        default:
            break;
        }
    }
    if (song.seq_mode && cur_page()->scope == SC_STEP)
        seq_entry(notes);

    if ((s = panel_enc(EN_PRESET)) != 0 && (cur_page()->graph == GR_BROWSE || cur_fam() == FAM_TRK)) {
        /* PRESETS browses the selected part's presets (all engines, then user presets) on the PRESETS
         * page and TRACKS only (the drum track: its kits); elsewhere a stray turn would throw away the sound
         * being edited. Every detent counts */
        uint32_t total, cur = preset_pos(&total);
        if (is_drum(TSEL))
            drum_kit_step(s);                           /* (the user kits after the factory ones) */
        else if (total)
            preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
    }
    if ((s = panel_enc(EN_ALGO)) != 0 && !ft_on)     /* ALGORITHM: the selected track, on every page (not in a take) */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
#if FELUCCA_SEL_PAGES
    if ((s = panel_enc(EN_SELECT)) != 0 && (rec_wait || ft_on || !page_walk(s)))   /* SELECT: the pages of the family */
        tempo_knob(s);                              /* shown, else (and on the REC screen) the tempo */
#else
    if ((s = panel_enc(EN_SELECT)) != 0)            /* SELECT knob = global tempo */
        tempo_knob(s);
#endif
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (on_fm6k_page()) {                            /* the FM6 editor: the values of its page */
            fm6k_knob(k, s);
            continue;
        }
        if (pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_SNAP) && k == 0u)) {     /* (not an empty column, nor "DRUM TRACK") */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = PH_HOT;
            PH_PICK(pg, k);                              /* its help line (param_help.c) */
        }
        edit_param(k, s);
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
