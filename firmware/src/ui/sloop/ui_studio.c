/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, greys, a track in its engine's colour
 * (the drum track: its kit's kind; ui_colors.c), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), DRUMS (GRID / KIT, pads that flash on each hit), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_lane, drum_cursor;   /* drum_page: 0 GRID, 1 KIT */
#if FELUCCA_DRUM_STEP
/* the grid's step keys (ui_drumstep.c): down, held past HOLD_MS (a held step: steps_held_* edit it), and the set steps
 * that clear when let go; when each went down. The grid's held steps need the store (one of its switches) */
static uint16_t gh_down, gh_held, gh_off;
static uint32_t gh_t0[16];
static uint32_t ds_held_first(void)                    /* the step of the first held key (the grid's page: the cursor's) */
{
    uint32_t w;
    for (w = 0; w < 16u && !((gh_held >> w) & 1u); w++)
        ;
    return (uint32_t)(drum_cursor / 16u) * 16u + w;
}
#else
#define gh_held 0u
#define gh_down 0u
#endif
static void trk_short_name(uint32_t c, char *b);
static int on_drum_page(void) { return cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
#define TE_G1 RGB(26, 26, 30)            /* tiles */
#define TE_G2 RGB(54, 54, 60)            /* empty steps, dial tracks */
#define TE_G3 RGB(118, 118, 126)         /* labels */
#define TE_G4 RGB(196, 196, 204)         /* secondary text */
#define TE_RED C_ERR                     /* recording, erasing (the status red) */
#define TE_REDDIM col_shade(C_ERR, 3u)
#define TE_DRUM trk_col(TRK_DRUM)        /* the drum track: its kit's kind */

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
#if FELUCCA_MACROS
/* GLO > MACRO on the screens (macro.c): the value a macro off home makes of the one the knob edits (the authored one,
 * saved), shown as what plays with an M and, on the gauge or dial, a second mark at its place. The value of *vp (a
 * synth part's parameter or a global) as it plays; *vp itself when no macro moves it (or the drum track's) */
static int32_t mac_shown(const int16_t *vp)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (vp >= trk[k].p && vp < trk[k].p + P_COUNT)
            return mac_effective(k, (uint32_t)(vp - trk[k].p), *vp);
    if (vp >= song.g && vp < song.g + G_COUNT)
        return mac_effective_g((uint32_t)(vp - song.g), *vp);
    if (vp == &dsend_v[0])                              /* a drum sound's REV (SOUND 3): SPACE moves it, the nearest level */
        return (int32_t)dsend_near(mac_effective_drev(dsend_lvl((uint32_t)*vp)));
    return *vp;
}
static void mac_mark(int32_t x, int32_t y, uint16_t c)      /* a small M, 5 x 5 */
{
    cv_rect(x, y, 1, 5, c);
    cv_rect(x + 4, y, 1, 5, c);
    cv_rect(x + 1, y + 1, 1, 1, c);
    cv_rect(x + 3, y + 1, 1, 1, c);
    cv_rect(x + 2, y + 2, 1, 1, c);
}
static uint8_t te_mac;                                  /* the next te_dials: the knobs a macro moves (bit per knob) */
static int32_t te_mac_r[4];                             /* and where it plays them on the dial, 0..1000 */
#else
#define te_mac 0
#endif
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* knob k's colour for what it drives (its dial, arc and label), def: the screen's own. The hook for SYSTEM > KNOB COLORS
 * (planned: 1 blue, 2 yellow, 3 pink, 4 orange while on); today def */
static uint16_t dial_col(uint32_t k, uint16_t def)
{
    (void)k;
    return def;
}
/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it. The dials of own (bit per knob) in col (what they belong to: a track's colour), the others grey */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache, uint16_t col, uint32_t own)
{
    uint32_t k;
#if FELUCCA_MACROS
    const uint32_t mac = te_mac;
    int32_t mr[4];
    memcpy(mr, te_mac_r, sizeof mr);
    te_mac = 0;                                         /* (this strip only) */
#else
    const uint32_t mac = 0;
#endif
    sig = studio_hash(sig * 31u + col * 7u + own + ui.msg_st, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
#if FELUCCA_MACROS
        if ((mac >> k) & 1u)
            sig = sig * 131u + (uint32_t)mr[k] + 977u;
#endif
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a bar, white or its status colour */
        cv_rect(0, 10, 240, 32, ui.msg_st ? C_STATUS[ui.msg_st & 3u] : C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], dial_col(k, (own >> k) & 1u ? col : TE_G4),
                dial_col(k, (own >> k) & 1u ? col_shade(col, 3u) : TE_G2));
        te_text_c(cx, 24, lab[k], dial_col(k, TE_G3));
#if FELUCCA_MACROS
        if ((mac >> k) & 1u) {                          /* a macro moves it: a mark on the ring where it plays, an M */
            uint32_t a = (uint32_t)(384 + mr[k] * 768 / 1000) & 1023u;
            te_disc(cx + ((SINE[(a + 256u) & 1023u] * 11) >> 15), 12 + ((SINE[a] * 11) >> 15), 1, C_WARN);
            mac_mark(cx + 14, 1, C_WARN);
        }
#endif
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k],
                  ui.hot_t && ui.hot_col == k ? C_WHITE : (mac >> k) & 1u ? C_WARN : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: the top bar every page has (ui_draw.c draw_head: track, transport, section, REC,
 * BPM, USB, battery; a message and the help line of the knob turned take it over), then a 20 px strip under it: the
 * page's title, the loop position and the four beat lights, solo / the click */
static void draw_head(void);
static void te_header(const char *title, uint16_t tc, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash(playing * 3u + (song.rec != 0) * 1999u + (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) +
                               (uint32_t)song.g[G_CLOCK] * 77u + arrangement_enabled * 5u + song.solo * 37u, title);
    draw_head();
    sig = sig * 31u + tc;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 20, C_BLACK);
    cv_text(4, 2, &FONT_S, title, tc);
    if (playing) {                                     /* bar.beat in the selected track's loop */
        loop_pos(TSEL, b);
        cv_text(112, 2, &FONT_S, b, C_WHITE);
    } else {
        cv_text(112, 2, &FONT_S, arrangement_enabled ? "song" : "loop", TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(160 + (int32_t)k * 9, 6, 7, 7, playing && beat % 4u == k ? (k ? TE_G4 : C_WHITE) : TE_G2);
    if (song.solo)
        cv_text(236 - text_w(&FONT_S, "solo"), 2, &FONT_S, "solo", C_WHITE);
    else if (song.rec && song.g[G_CLOCK] != 0)         /* the click is on */
        cv_text(236 - text_w(&FONT_S, "click"), 2, &FONT_S, "click", TE_G3);
    cv_rect(0, 19, 240, 1, TE_G1);
    cv_blit(0, 20);
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
static void swing_str(char *b, int32_t v)
{
    fmt_int(b, 50 + (clamp(v, 0, 100) + 2) / 4);
    str_cpy(b + str_len(b), "%", 2);
}

/* The mixer (the user's rulings, 2026-10-09): rows T1 T2 T3 DR, then the drum track's 16 lanes, 4 rows a screen.
 * A row: its tile, the name (an instrument and its engine, a lane and its source), a thin VU meter under the name
 * with the COMP insert's gain reduction pushing in from the right, the 16 steps in view with the playhead. The four
 * dials are the selected row's VOL INSERT SEND PAN (tracks_edit, ui_input.c).
 *   ALGORITHM   walks the rows (edge_walk.c: a turn stops on DR going down and on the first lane going up; a fresh
 *               turn crosses). A lane row selects the drum track and that lane (drum_lane: the DRUMS grid follows);
 *               never a sound
 *   INSERT      the first insert effect in the FX slots' order (DIST COMP FILT), its amount: the track's (P_*), the
 *               drum bus's (the drum track's P_*), a lane's (its sound's DST / CMP, drum_sends.c dsend_desc)
 *   SEND        REV when it is in a slot, else the first send in the slots' order (the lanes: their REV DLY CHO)
 *   PAN         a track's P_PAN; a lane's PAN (drums/drum_mix.c)
 *   GLO + key 4 / key 8 on a lane row: that lane's MUTE / SOLO (mix_glo_key; on a track row: the tracks' as ever) */
#include "edge_walk.c"
#define MIX_ROWS (NTRK + DRUM_LANES)
#define MIX_VU_X 34
#define MIX_VU_W 202                                    /* the meter line: x 34 .. 235 */
#define MIX_VU_Y 19
#define MIX_VU_H 3
static uint8_t mix_row;                                 /* the row selected: 0..3 the tracks, 4..19 the lanes */
static uint32_t mix_algo_ms;                            /* the last ALGORITHM detent (edge_walk.c: a fresh turn) */
static uint8_t mix_vu[MIX_ROWS], mix_gr[MIX_ROWS];      /* each row's meter now (px, falling) */
static uint16_t mix_vu_drawn[MIX_ROWS];                 /* what its line shows (0xFFFF: redraw) */
static int32_t meter_ui_take(uint32_t c);               /* (meters.c; a host test without it: ui_draw.c) */
static int32_t meter_lane_take(uint32_t l);
static uint32_t meter_gr_take(uint32_t r);

/* the row selected (a lane row only while the drum track is: another track picked elsewhere takes the row) */
static uint32_t mix_cur(void)
{
    if (mix_row >= MIX_ROWS || mix_row < NTRK || song.sel != TRK_DRUM)
        mix_row = song.sel;
    return mix_row;
}
static int32_t mix_lane(void)                           /* the lane row selected, -1: a track row */
{
    uint32_t r = mix_cur();
    return r >= NTRK ? (int32_t)(r - NTRK) : -1;
}
/* ALGORITHM on the mixer: one row a detent, stopping at the tracks / lanes edge */
static void mix_algo(int32_t s)
{
    uint32_t r = edge_walk(mix_cur(), s, MIX_ROWS, NTRK, edge_fresh(&mix_algo_ms, fm1_ms));
    if (r >= NTRK) {
        track_select(TRK_DRUM);
        drum_lane = (uint8_t)(r - NTRK);                /* (silent: the mixer never previews) */
    } else {
        track_select(r);
    }
    mix_row = (uint8_t)r;
}
/* GLO + key w (0-based white key) on the mixer: on a lane row keys 4 and 8 are its MUTE and SOLO; 1: taken */
static int mix_glo_key(int32_t w)
{
    int32_t l = cur_page()->scope == SC_TRK ? mix_lane() : -1;
    if (l < 0 || (w != 3 && w != 7))
        return 0;
    if (w == 3)
        dlm_set_mute((uint32_t)l, !dlm_muted((uint32_t)l));
    else
        dlm_set_solo((uint32_t)l, !dlm_soloed((uint32_t)l));
    return 1;
}

/* the row's INSERT (send 0) or SEND (send 1): its value id (a track: P_*; a lane: dsend id + 16), 0xFF none */
static uint32_t mix_fx_id(uint32_t r, uint32_t send)
{
    uint32_t k, best = 0xFFu;
    for (k = 0; k < FX_NSLOT; k++) {
        uint32_t t = fxs_slot[k], id;
        if (t >= FXT_N || !FXS_ON(t) || ((FXT_INSERT >> t) & 1u) == send)
            continue;
        id = r >= NTRK ? fxs_lane_id(k) : fxs_amt(k);
        if (id == 0xFFu)
            continue;
        if (send && t == FXT_REV)
            return id;
        if (best == 0xFFu)
            best = id;
    }
    return best;
}
static const param_desc_t MIX_LANE_PAN = PD("PAN", F_BIPCT, -64, 63, 0);
/* dial k (0 VOL, 1 INSERT, 2 SEND, 3 PAN) of row r: its descriptor (0: none), *v its value */
static const param_desc_t *mix_desc(uint32_t r, uint32_t k, int16_t *v)
{
    const param_desc_t *d = 0;
    int16_t *vp = 0;
    uint32_t id = k == 1u || k == 2u ? mix_fx_id(r, k == 2u) : 0u;
    if (id == 0xFFu)
        return 0;
    if (r >= NTRK) {                                    /* a lane: its sound's values */
        uint32_t l = r - NTRK;
        if (k == 3u) {
            *v = dlm_pan[l];
            return &MIX_LANE_PAN;
        }
        d = dsnd_desc_lane(l, k ? id : DE_LEVEL, &vp);
        if (d)
            *v = *vp;
        return d;
    }
    if (k == 0u && r == TRK_DRUM) {
        *v = song.g[G_DRLVL];
        return &GP[G_DRLVL];
    }
    id = k == 0u ? P_LEVEL : k == 3u ? P_PAN : id;
    *v = trk[r].p[id];
    return track_desc(&trk[r], id);
}
static void mix_set(uint32_t r, uint32_t k, int32_t v)
{
    uint32_t id = k == 1u || k == 2u ? mix_fx_id(r, k == 2u) : 0u;
    if (id == 0xFFu)
        return;
    if (r >= NTRK) {
        uint32_t l = r - NTRK;
        if (k == 3u)
            dlm_set_pan(l, v);
        else if (k == 0u)
            dl.ofs[l][DE_LEVEL] = (int8_t)v;            /* (the next hit hears it, as SOUND 2's LEVEL) */
        else
            dsend_set(l, id - 16u, v);
        return;
    }
    if (k == 0u && r == TRK_DRUM)
        song.g[G_DRLVL] = (int16_t)v;
    else
        trk[r].p[k == 0u ? P_LEVEL : k == 3u ? P_PAN : id] = (int16_t)v;
}

/* sentence case: the first letter capitalised, the rest lower; an acronym stays as it is: a word with a digit (FM6,
 * X0X, 808, USR1), a word of 2 letters alone (CZ) or beside a number (X9 BD) */
static void te_sentence(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0, w, anyd = 0, multi = 0, keep = 0;
    for (w = 0; s[w]; w++)
        anyd |= (uint32_t)(s[w] >= '0' && s[w] <= '9'), multi |= (uint32_t)(s[w] == ' ');
    for (w = 0; s[w] && i + 1u < n; w++) {
        char c = s[w];
        if (w == 0u || s[w - 1u] == ' ') {
            uint32_t e = w, dig = 0;
            while (s[e] && s[e] != ' ')
                dig |= (uint32_t)(s[e] >= '0' && s[e] <= '9'), e++;
            keep = dig || (e - w <= 2u && (!multi || anyd));
        }
        if (!keep && w && c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        else if (!keep && !w && c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        d[i++] = c;
    }
    d[i] = 0;
}
/* a peak (32767 = 0 dBFS) as the meter's width: -54 .. 0 dB */
static uint32_t mix_vu_px(int32_t a)
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);                /* 8 log2(a): 48 (-54 dB) .. 120 (0 dB) */
    return (uint32_t)clamp((v - 48) * MIX_VU_W / 72, 1, MIX_VU_W);
}
/* row r's step p: 0 none, 1 empty, 2 a hit / note */
static uint32_t mix_step(uint32_t r, uint32_t p)
{
    if (r >= NTRK)
        return dstep_has(&TDRUM->dstep[p], r - NTRK) ? 2u : 1u;
    return trk_step_on(&trk[r], p) ? 2u : 1u;
}
/* the 16 steps in view (playing: the page under the playhead; stopped, longer than 16: the pattern folded) */
static void mix_steps_draw(uint32_t r, uint32_t len, uint32_t pos, uint16_t on_c, uint16_t top_c)
{
    uint32_t j, bank = song.playing ? pos / 16u : 0u;
    for (j = 0; j < 16u; j++) {
        uint32_t p = bank * 16u + j, on = 0;
        if (len <= 16u || song.playing) {
            if (p < len)
                on = mix_step(r, p);
        } else {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k;
            on = 1;
            for (k = a; k < z; k++)
                if (mix_step(r, k) == 2u)
                    on = 2;
        }
        cv_rect(MIX_VU_X + (int32_t)j * 10, 23, 8, 8, !on ? C_BLACK : on == 2u ? on_c : TE_G1);
        if (on == 2u && top_c != on_c)
            cv_rect(MIX_VU_X + (int32_t)j * 10, 23, 8, 2, top_c);
        if (song.playing && p == pos)
            cv_rect(MIX_VU_X + (int32_t)j * 10, 33, 8, 2, C_WHITE);
    }
}
static uint32_t mix_steps_sig(uint32_t r, uint32_t len)
{
    uint32_t j, h = len;
    for (j = 0; j < len; j++)
        h = h * 31u + mix_step(r, j);
    return h;
}
/* the row's badge at x 200: REC, SOLO, MUTE, DRY */
static void mix_badge(uint32_t b)
{
    static const char *const B[5] = {"", "REC", "SOLO", "MUTE", "DRY"};
    if (b == 1u || b == 2u) {
        cv_rect(200, 2, 36, 15, b == 1u ? TE_RED : C_WHITE);
        te_text_c(218, 1, B[b], b == 1u ? C_WHITE : C_BLACK);
    } else if (b) {
        te_text_c(218, 1, B[b], TE_G3);
    }
}
/* the static part of row r (all but the meter line) at y; its signature in *sig (redrawn when it changed) */
static void mix_row_draw(uint32_t r, uint32_t y, uint32_t *sig)
{
    char b[24], e[24];
    uint32_t sel = mix_cur() == r, h, badge = 0, silent;
    uint32_t trk_i = r < NTRK ? r : TRK_DRUM, len = (uint32_t)clamp(trk[trk_i].p[P_SLEN], 1, 64);
    uint32_t pos = trk[trk_i].seq_idx % len;
    uint16_t col, dim;
    if (r < NTRK) {
        const track_t *t = &trk[r];
        uint32_t level = r == TRK_DRUM ? (uint32_t)song.g[G_DRLVL] : (uint32_t)t->p[P_LEVEL];
        col = trk_col(r);
        silent = trk_silent(t) || !level;
        badge = (song.rec >> r) & 1u ? 1u : (song.solo >> r) & 1u ? 2u : silent ? 3u : !fx_on(t) ? 4u : 0u;
        if (r == TRK_DRUM) {
            te_sentence(b, drum_kit_name(), sizeof b);
            str_cpy(e, "Drums", sizeof e);
        } else {
            trk_short_name(r, b);
            te_sentence(e, ENGINES[t->eng_req % NENGINES]->name, sizeof e);
        }
    } else {
        uint32_t l = r - NTRK, usr = dl_usr_of(l), kit = dl_kit_of(l, drum_kit());
        col = lane_col(l);
        silent = !dlm_lane_heard(l) || trk_silent(TDRUM);
        badge = dlm_muted(l) ? 3u : dlm_soloed(l) ? 2u : !dlm_lane_heard(l) ? 3u : 0u;   /* (another lane soloed: MUTE) */
        te_sentence(b, LANE_NAME[l], sizeof b);
        te_sentence(e, usr ? DS_SRC_NAMES[usr] : dl.src[l] == DL_KIT ? drum_kit_name() :
                       dl.src[l] >= DL_X909 && dsnd_src_idx(dl.src[l]) ? DS_SRC_NAMES[dsnd_src_idx(dl.src[l])] :
                       DRUM_KIT_NAMES[kit], sizeof e);
    }
    dim = col_shade(col, 3u);
    b[13] = 0;
    h = studio_hash(studio_hash(col * 3u + sel + silent * 997u + badge * 1999u + len * 37u + song.playing * 7u +
                                (song.playing ? pos * 71u : 0u) + mix_steps_sig(r, len) * 13u, b), e);
    if (!ui.force && h == *sig)
        return;
    *sig = h;
    mix_vu_drawn[r] = 0xFFFFu;                          /* (its meter line again, over the row) */
    cv_begin(240, 36, C_BLACK);
    if (sel)
        cv_rect(0, 3, 1, 30, C_WHITE);                  /* the row selected: a white edge, its tile lit, its name white */
    cv_rect(2, 3, 26, 30, sel ? col : dim);             /* the tile: the track's number, the lane's */
    {
        char n[4];
        fmt_int(n, (int32_t)(r < NTRK ? r + 1u : r - NTRK + 1u));
        te_text_c(15, 10, n, sel ? C_BLACK : col);
    }
    cv_text(34, 1, &FONT_S, b, sel ? C_WHITE : TE_G4);
    if (34 + text_w(&FONT_S, b) + 6 + text_w(&FONT_S, e) < 196)
        cv_text(34 + text_w(&FONT_S, b) + 6, 1, &FONT_S, e, sel ? col : TE_G3);
    mix_badge(badge);
    cv_rect(MIX_VU_X, MIX_VU_Y, MIX_VU_W, MIX_VU_H, TE_G1);
    mix_steps_draw(r, len, pos, silent ? TE_G3 : sel ? col : dim, silent ? TE_G3 : col);
    cv_blit(0, y);
}
/* row r's meter line at y: the level from the left in its colour, the gain reduction from the right */
static void mix_vu_draw(uint32_t r, uint32_t y, uint32_t sel)
{
    uint32_t vu = mix_vu[r], gr = mix_gr[r], key = vu | gr << 8;
    uint16_t col = r < NTRK ? trk_col(r) : lane_col(r - NTRK);
    if (key == mix_vu_drawn[r])
        return;
    mix_vu_drawn[r] = (uint16_t)key;
    cv_begin(MIX_VU_W, MIX_VU_H, TE_G1);
    if (vu)
        cv_rect(0, 0, (int32_t)vu, MIX_VU_H, sel ? col : col_shade(col, 3u));
    if (gr)
        cv_rect(MIX_VU_W - (int32_t)gr, 0, (int32_t)gr, MIX_VU_H, C_WARN);
    cv_blit(MIX_VU_X, y + MIX_VU_Y);
}
/* the meters, every frame (seen or not: no stale peak): each row's level, falling 4 px a frame, its reduction */
static void mix_meters(void)
{
    uint32_t r;
    for (r = 0; r < MIX_ROWS; r++) {
        uint32_t lv = mix_vu_px(r < NTRK ? meter_ui_take(r) : meter_lane_take(r - NTRK)), g = meter_gr_take(r);
        uint32_t pk = mix_vu[r];
        mix_vu[r] = (uint8_t)(lv > pk ? lv : pk > 4u ? pk - 4u : 0u);
        g = g > MIX_VU_W / 2u ? MIX_VU_W / 2u : g;     /* (dB x 4: 4 px a dB) */
        mix_gr[r] = (uint8_t)(g > mix_gr[r] ? g : mix_gr[r] > 2u ? mix_gr[r] - 2u : 0u);
    }
}

static void studio_tracks_draw(void)
{
    static uint32_t head, rows[4], footer;
    static uint8_t mix_top;                             /* the first row in view: the list slides one row at a time */
    uint32_t i, sel = mix_cur(), top = list_top(mix_top, sel, MIX_ROWS, 4u);
    char title[24];
    if (top != mix_top) {                               /* the list slid: every row again (their meter lines with them) */
        mix_top = (uint8_t)top;
        memset(rows, 0, sizeof rows);
    }
    if (top + 4u <= NTRK) {                             /* where the view is: "Tracks", "Tracks, L1-2", "Lanes 3-6" (fits before x 112) */
        str_cpy(title, "Tracks", sizeof title);
    } else {
        str_cpy(title, top < NTRK ? "Tracks, L1-" : "Lanes ", sizeof title);
        if (top >= NTRK) {
            fmt_int(title + str_len(title), (int32_t)(top - NTRK + 1u));
            str_cpy(title + str_len(title), "-", 2);
        }
        fmt_int(title + str_len(title), (int32_t)(top + 4u - NTRK));
    }
    te_header(title, TE_G3, &head);
    mix_meters();
    for (i = 0; i < 4u && top + i < MIX_ROWS; i++) {
        mix_row_draw(top + i, 40u + i * 36u, &rows[i]);
        mix_vu_draw(top + i, 40u + i * 36u, top + i == sel);
    }
    {   /* KNOB 1..4: VOL INSERT SEND PAN of the row selected, each its dial and its value */
        static const char *const lab[4] = {"VOL", "INSERT", "SEND", "PAN"};
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        uint32_t k, own = 0;
        for (k = 0; k < 4u; k++) {
            int16_t x;
            const param_desc_t *d = mix_desc(sel, k, &x);
            const char *unit;
            ratio[k] = -1;
            str_cpy(v[k], "--", sizeof v[k]);
            if (!d)
                continue;
            own |= 1u << k;
            param_format(d, x, v[k], &unit);
            if (k == 0u && sel < NTRK && trk[sel].p[P_MUTE])
                str_cpy(v[k], "MUTE", sizeof v[k]);     /* (muted with GLO: the first turn unmutes, tracks_edit) */
            else if (k == 1u || k == 2u) {              /* "DST 40": the effect, then its amount */
                char t[12];
                str_cpy(t, d->label, 5);
                str_cpy(t + str_len(t), " ", 2);
                str_cpy(t + str_len(t), v[k], sizeof t - str_len(t));
                str_cpy(v[k], t, sizeof v[k]);
            } else if (text_w(&FONT_S, v[k]) + text_w(&FONT_S, unit) <= 56)
                str_cpy(v[k] + str_len(v[k]), unit, sizeof v[k] - str_len(v[k]));
            ratio[k] = d->max > d->min ? (int32_t)(x - d->min) * 1000 / (d->max - d->min) : 0;
        }
#if FELUCCA_MACROS
        if (sel < NTRK) {   /* a macro moves the drums' level (ENERGY) and a part's pan (SPACE): shown as it plays */
            track_t *t = &trk[sel];
            int32_t lvl = sel == TRK_DRUM ? song.g[G_DRLVL] : t->p[P_LEVEL];
            int32_t e = sel == TRK_DRUM ? mac_effective_g(G_DRLVL, song.g[G_DRLVL]) : lvl;
            if (e != lvl && !t->p[P_MUTE])
                te_mac |= 1u, te_mac_r[0] = e * 1000 / 127;
            e = mac_shown(&t->p[P_PAN]);
            if (e != t->p[P_PAN])
                te_mac |= 8u, te_mac_r[3] = (e + 64) * 1000 / 127;
        }
#endif
        te_dials(184, lab, val, ratio, sel, &footer, sel < NTRK ? trk_col(sel) : lane_col(sel - NTRK), own);
    }
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? col_shade(TE_DRUM, 3u) : lvl == LV_SOFT ? col_shade(TE_DRUM, 5u) : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
#endif
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits;
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    uint32_t n[NPART][4], k, w;
#endif
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
#endif
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
#if FELUCCA_LIGHTS && FELUCCA_KEYLIT
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note (a 1/16 at
     * GATE 50 %: 60..80 ms) is seen even when it ends between two frames (SLOOP 2.4 ui_studio.c pads_tick) */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
#endif
}

#if FELUCCA_DRUM_STEP
static void ds_grid_follow(void);                      /* (ui_drumstep.c) */
static void lane_pick(uint32_t l, uint32_t hear);      /* (ui_drumstep.c) the one lane every pick goes through */
static int32_t ds_algo_walk(int32_t s);                /* (ui_drumstep.c) ALGORITHM: the lanes, stop at the kick */
static void ds_ratchet_step(dstep_t *st, int32_t s);   /* (ui_drumstep.c) KNOB 3: set / ratchet / clear */
#if FELUCCA_AUTO                                       /* the held steps (ui_layers.c, the SEQ layer's: no copy) */
static void steps_held_edit(uint32_t knob, int32_t s);
static void steps_held_clear(void);
#if FELUCCA_PLOCK
static void held_lock_text(char *sub, uint32_t n, const track_t *t, uint32_t idx);
static void lock_par_step(int32_t s);
#endif
#if FELUCCA_MICRO
static void held_nudge_str(char *b, int32_t m);
#endif
#if FELUCCA_FILLS
static void steps_held_fill(void);
#endif
#if FELUCCA_CHANCE
static void steps_held_chance(int32_t s);
#endif
#endif
#endif
static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, j, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
#if FELUCCA_DRUM_STEP && FELUCCA_AUTO
    static uint8_t mk[NSTEP];                          /* the store's marks of each step (auto_marks_all) */
#if FELUCCA_PLOCK
    char lktxt[24];
#endif
    auto_marks_all(TDRUM, mk);
#endif
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    {
        char st[16];
        te_lower(st, drum_kit_style(), sizeof st);
        st[12] = 0;
        te_header(st, TE_DRUM, &head);
    }
    sig = TE_DRUM * 3u + kit * 131u + drum_page * 7u + drum_lane * 977u + bank * 31u + len + drum_kit_pos() * 7919u;   /* title band */
#if FELUCCA_DRUM_STEP
    sig = sig * 31u + (uint32_t)ui.step_follow * (song.playing ? 1u : 0u) + 17u;
#endif
#if FELUCCA_DRUM_STEP && FELUCCA_AUTO && FELUCCA_PLOCK
    lktxt[0] = 0;
    if (gh_held && !drum_page)                         /* the held step's lock (PRESETS, ALGORITHM), under the lane */
        held_lock_text(lktxt, sizeof lktxt, TDRUM, ds_held_first() % NSTEP);
    sig = studio_hash(sig * 31u + gh_held, lktxt);
#endif
    if (ui.force || sig != title_sig) {
        char b[8];
        title_sig = sig;
        cv_begin(240, 44, C_BLACK);
        cv_rect(2, 4, 34, 34, TE_DRUM);
#if FELUCCA_DRUM_STEP
        if (!drum_page) {                              /* the GRID: the picked lane's number and name (the kit's under it) */
            const char *ln = text_w(&FONT_L, LANE_NAME[drum_lane & 15u]) <= 116 ? LANE_NAME[drum_lane & 15u]
                                                                                 : LANE_SHORT[drum_lane & 15u];
            fmt_int(b, (int32_t)(drum_lane & 15u) + 1);
            cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
            cv_text(44, 4, &FONT_L, ln, TE_DRUM);
#if FELUCCA_AUTO && FELUCCA_PLOCK
            cv_text(44, 26, &FONT_S, lktxt[0] ? lktxt : drum_kit_name(), lktxt[0] ? C_WHITE : TE_G3);
#else
            cv_text(44, 26, &FONT_S, drum_kit_name(), TE_G3);
#endif
        } else
#endif
        {
            fmt_int(b, (int32_t)drum_kit_pos() + 1);
            cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
            cv_text(44, 6, &FONT_L, drum_kit_name(), C_WHITE);
        }
#if FELUCCA_DRUM_STEP
        if (len > 16u && !drum_page) {                 /* the page of steps, and FOLLOW while it plays */
            char pg[4] = {(char)('1' + bank), '/', (char)('0' + (len + 15u) / 16u), 0};
            cv_text(164, 4, &FONT_S, pg, C_WHITE);
            if (song.playing && ui.step_follow)
                cv_rect(164, 24, 20, 3, TE_DRUM);
        }
#endif
        cv_text(202, 4, &FONT_S, "grid", drum_page == 0 ? C_WHITE : TE_G3);
        cv_text(202, 22, &FONT_S, "kit", drum_page == 1 ? C_WHITE : TE_G3);
        cv_rect(196, drum_page ? 26 : 8, 3, 9, TE_DRUM);
        cv_blit(0, 40);
    }
    sig = TE_DRUM * 5u + drum_page + drum_lane * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u;
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
#if FELUCCA_DRUM_STEP
    sig = sig * 31u + gh_held;                         /* (the held steps: their column) */
#if FELUCCA_AUTO
    for (i = 0; i < len; i++) sig = sig * 7u + mk[i];
#endif
#endif
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        cv_begin(240, 100, C_BLACK);
        if (!drum_page) {                              /* GRID: the 16 lanes x 16 steps of this bank */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t y = (int32_t)i * 6 + 2;
                if (i == drum_lane)                    /* the picked lane: its row tinted in the drum colour, a bar at the left */
                    cv_rect(0, y - 1, 240, 7, col_shade(TE_DRUM, 2u));
                cv_rect(i == drum_lane ? 0 : 2, i == drum_lane ? y - 1 : y, i == drum_lane ? 8 : 6, i == drum_lane ? 7 : 5,
                        pad_lit[i] ? C_WHITE : i == drum_lane ? TE_DRUM : TE_G2);
                for (j = 0; j < 16u; j++) {
                    uint32_t p = bank * 16u + j;
                    const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
                    uint16_t c;
                    if (p >= len) continue;
                    c = dstep_has(s, i) ? lvl_col(dstep_lvl(s, i)) : i == drum_lane ? TE_G2 : TE_G1;
                    if (song.playing && p == TDRUM->seq_idx && c == TE_G1) c = TE_G2;
                    cv_rect(12 + (int32_t)j * 14, y, 12, 5, c);
                    if (dstep_has(s, i) && dstep_rat(s, i)) {   /* a ratchet: a notch per extra hit */
                        uint32_t r;
                        for (r = 0; r < dstep_rat(s, i); r++)
                            cv_rect(13 + (int32_t)j * 14 + (int32_t)r * 3, y + 2, 2, 1, C_BLACK);
                    }
                    if (p == drum_cursor && i == drum_lane) {
                        cv_rect(11 + (int32_t)j * 14, y - 1, 14, 1, C_WHITE);
                        cv_rect(11 + (int32_t)j * 14, y + 5, 14, 1, C_WHITE);
                    }
                }
            }
#if FELUCCA_DRUM_STEP && FELUCCA_AUTO
            for (j = 0; j < 16u; j++) {                /* under the grid, a step's store marks (auto_step_marks) */
                uint32_t p = bank * 16u + j, m = p < len ? mk[p] : 0u;
                int32_t x = 12 + (int32_t)j * 14;
                if (m & AUTO_MK_ONLY)                  /* a dot: a nudge, a lock, a chance or motion */
                    cv_rect(x + 4, 98, 4, 2, C_WHITE);
                if (m & AUTO_MK_FILL)                  /* plays in a fill only: a bar at the left */
                    cv_rect(x, 98, 3, 2, C_WHITE);
                if (m & AUTO_MK_NOFILL)                /* silent in a fill: a bar at the right */
                    cv_rect(x + 9, 98, 3, 2, C_WHITE);
                if ((gh_held >> j) & 1u) {             /* a held step: its whole column outlined in red */
                    cv_rect(11 + (int32_t)j * 14, 0, 1, 98, TE_RED);
                    cv_rect(24 + (int32_t)j * 14, 0, 1, 98, TE_RED);
                    cv_rect(11 + (int32_t)j * 14, 0, 14, 1, TE_RED);
                }
            }
#endif
        } else {                                       /* KIT: 16 pads, lit on each hit */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 25;
                cv_rect(x, y, 56, 23, pad_lit[i] ? TE_DRUM : TE_G1);
                te_text_c(x + 28, y + 4, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_lane ? C_WHITE : TE_G3);
            }
        }
        cv_blit(0, 84);
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"sound", "step", "hit", "level"}, *const LK[4] = {"kit", "level", "reverb", "pan"};
        int32_t ratio[4];
        if (!drum_page) {
            const dstep_t *s = &TDRUM->dstep[drum_cursor];
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_cursor + 1);
            if (dstep_has(s, drum_lane) && dstep_rat(s, drum_lane)) {   /* a ratchet: x2 .. x4 */
                v[2][0] = 'x';
                v[2][1] = (char)('1' + dstep_rat(s, drum_lane));
                v[2][2] = 0;
            } else {
                str_cpy(v[2], dstep_has(s, drum_lane) ? "on" : "--", 4);
            }
            str_cpy(v[3], dstep_has(s, drum_lane) ? LV_NAME[dstep_lvl(s, drum_lane)] : "--", 8);
            ratio[0] = (int32_t)drum_lane * 1000 / (DRUM_LANES - 1);
            ratio[1] = (int32_t)drum_cursor * 1000 / (int32_t)(len > 1u ? len - 1u : 1u);
            ratio[2] = dstep_has(s, drum_lane) ? (int32_t)(1u + dstep_rat(s, drum_lane)) * 250 : 0;
            ratio[3] = dstep_has(s, drum_lane) ? (int32_t)((dstep_lvl(s, drum_lane) + 1u) % 4u) * 333 : 0;
#if FELUCCA_DRUM_STEP && FELUCCA_AUTO
            if (gh_held) {                             /* a step held: KNOB 2 its chance, KNOB 4 its nudge (when built) */
                static const char *lh[4] = {"sound", "step", "hit", "level"};
                uint32_t hi = ds_held_first() % NSTEP;
                const dstep_t *hs = &TDRUM->dstep[hi];
                lh[1] = FELUCCA_CHANCE ? "chance" : "step";
                lh[3] = FELUCCA_MICRO ? "nudge" : "level";
#if FELUCCA_CHANCE
                if (dstep_mask(hs)) {
                    fmt_int(v[1], (int32_t)chance_of(TDRUM, hi));
                    ratio[1] = (int32_t)chance_of(TDRUM, hi) * 10;
                } else {
                    str_cpy(v[1], "--", 4);
                    ratio[1] = 0;
                }
#endif
#if FELUCCA_MICRO
                held_nudge_str(v[3], step_micro(TDRUM, hi));
                ratio[3] = (step_micro(TDRUM, hi) - MICRO_MIN) * 1000 / (MICRO_MAX - MICRO_MIN);
#endif
                if (dstep_has(hs, drum_lane) && dstep_rat(hs, drum_lane)) {
                    v[2][0] = 'x';
                    v[2][1] = (char)('1' + dstep_rat(hs, drum_lane));
                    v[2][2] = 0;
                } else {
                    str_cpy(v[2], dstep_has(hs, drum_lane) ? "on" : "--", 4);
                }
                te_dials(184, (const char *const *)lh, val, ratio, 3u, &footer, TE_DRUM, 0xFu);
            } else
#endif
            te_dials(184, LG, val, ratio, 1u, &footer, TE_DRUM, 0xFu);
        } else {
            fmt_int(v[0], (int32_t)drum_kit_pos() + 1);
            fmt_int(v[1], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[2], (int32_t)dsend_rev(dsend[drum_lane & 15u]));   /* (the sound's REV: drum_sends.c) */
            fmt_int(v[3], TDRUM->p[P_PAN]);
            ratio[0] = (int32_t)drum_kit_pos() * 1000 / (int32_t)(drum_kit_total() - 1u);
            ratio[1] = song.g[G_DRLVL] * 1000 / 127;
            ratio[2] = (int32_t)dsend_rev(dsend[drum_lane & 15u]) * 1000 / (int32_t)DSEND_MAX;
            ratio[3] = (TDRUM->p[P_PAN] + 64) * 1000 / 127;
#if FELUCCA_MACROS
            {   /* ENERGY moves the drums' level, SPACE each sound's reverb send: shown as they play */
                int32_t e = mac_effective_g(G_DRLVL, song.g[G_DRLVL]);
                uint32_t r = dsend_rev(dsend[drum_lane & 15u]), er = dsend_near(mac_effective_drev(dsend_lvl(r)));
                if (e != song.g[G_DRLVL]) {
                    te_mac |= 2u, te_mac_r[1] = e * 1000 / 127;
                    if (!ui.hot_t || ui.hot_col != 1u)
                        fmt_int(v[1], e * 100 / 127);
                }
                if (er != r) {
                    te_mac |= 4u, te_mac_r[2] = (int32_t)er * 1000 / (int32_t)DSEND_MAX;
                    if (!ui.hot_t || ui.hot_col != 2u)
                        fmt_int(v[2], (int32_t)er);
                }
            }
#endif
            te_dials(184, LK, val, ratio, 2u, &footer, TE_DRUM, 0xFu);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

static void drum_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % 2u);
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            studio_open(SC_SONG);
            return;
        }
    }
#if FELUCCA_DRUM_STEP && FELUCCA_AUTO
    if (gh_held) {                                     /* a step key held: the SEQ layer's held-step edits (ui_layers.c) */
        if ((pressed >> panel.btn[B_OCTDN]) & 1u)
            steps_held_clear();                        /* OCT-: all it holds in the store goes, motion too */
#if FELUCCA_FILLS
        if ((pressed >> panel.btn[B_OCTUP]) & 1u)
            steps_held_fill();                         /* OCT+: its fill condition, round */
#endif
#if FELUCCA_PLOCK
        if ((s = panel_enc(EN_PRESET)))
            steps_held_edit(4u, s);                    /* PRESETS: the lock's value */
        if ((s = panel_enc(EN_ALGO)))
            lock_par_step(s);                          /* ALGORITHM: its parameter */
#endif
        (void)panel_enc(EN_SELECT);                    /* (grid / kit stays while a step is held) */
        for (k = 1; k < 4u; k++) {
            uint32_t on = k == 1u ? FELUCCA_CHANCE : k == 2u ? 1u : FELUCCA_MICRO;
            if (!on || !(s = panel_enc(EN_K1 + k)))
                continue;
            ui.hot_col = (uint8_t)k;
            ui.hot_t = PH_HOT;
            if (k == 1u) {
#if FELUCCA_CHANCE
                steps_held_chance(s);                  /* KNOB 2: the chance of the held steps, every lane of each */
#endif
            } else {
                steps_held_edit(k == 2u ? 2u : 3u, s); /* KNOB 3: their ratchet; KNOB 4: their nudge */
            }
        }
    }
#endif
#if FELUCCA_DRUM_STEP
    if ((s = panel_enc(EN_SELECT))) {                  /* SLOOP 2.4: SELECT switches the grid and the kit page */
        if ((uint32_t)(s > 0) != drum_page) {
            drum_page = (uint8_t)(s > 0);
            ui.msg_t = 0;
            ui.force = 1;
        }
    }
    ds_grid_follow();
#else
    if ((s = panel_enc(EN_SELECT)))
        tempo_knob(s);
#endif
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
#if FELUCCA_DRUM_STEP
        if (ds_algo_walk(s) < 0) {                     /* the lanes end at the kick: a fresh turn goes up to T3 */
            track_select(TRK_DRUM - 1u);
            go_home();
            return;
        }
#else
        track_select((uint32_t)clamp((int32_t)song.sel + s, 0, 3));
        if (song.sel != TRK_DRUM) {
            go_home();
            return;
        }
#endif
    }
    if ((s = panel_enc(EN_PRESET))) { PH_CLEAR(); drum_kit_step(s); }
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = PH_HOT;
        PH_SLOT(drum_page ? "DRUM KIT" : "DRUM GRID", k);   /* its help line (param_help.c) */
        if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
#if FELUCCA_DRUM_STEP
            if (k == 0) {                              /* the sound: heard (SLOOP 2.4) */
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) lane_pick(l, 0);   /* (heard only stopped: seq.c audition_req) */
            }
            if (k == 1) {                              /* the step: what it holds, heard */
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
#else
            if (k == 0) drum_lane = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
            if (k == 1) drum_cursor = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
#endif
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
#if FELUCCA_DRUM_STEP
                    ds_ratchet_step(st, s);
#else
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else dstep_clr(st, drum_lane);
#endif
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else {
            if (k == 0) drum_kit_step(s);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) dsend_set(drum_lane, 0, (int32_t)dsend_rev(dsend[drum_lane & 15u]) + s);   /* the sound's REV */
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
#if FELUCCA_REC_MODES
/* REC screen with its dials (FELUCCA_REC_MODES, from SLOOP 2.3 by isod89, GPL-3.0): the four tracks, compact,
 * from y0: the one that records is framed in red */
static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit_pos() * 977u + y0 + kit_col() * 13u, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, drum_kit_name(), sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[16];
        if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
        cv_rect(4, y + 3, 12, 12, armed ? trk_col(i) : TRK_DIM(i));
        if (i == TRK_DRUM) str_cpy(b, drum_kit_name(), sizeof b);
        else trk_short_name(i, b);
        b[10] = 0;
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? trk_col(i) : TRK_DIM(i)) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_REDDIM);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, trk_col(rt));
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"play freely", "play a note", "press play"};
        static const char *const L2[3] = {"then rec on the 1", "it starts the loop", "4 clicks, then rec"};
        static const char *const L3[3] = {"the tempo follows you", "play: go  rec: cancel", "rec: cancel"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(96, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, "count-in", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_REDDIM);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
        static const char *const LAB_E[4] = {"mode", "length", "start", ""};
        static const char *const LAB_F[4] = {"mode", "", "", ""};
        static const char *const LAB_N[4] = {"", "length", "start", ""};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], ""};
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = rec_tempo ? 1000 : 0;
        ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
        ratio[2] = rec_count ? 1000 : 0;
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";                        /* (a dial not shown: no value either) */
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials, TE_G4, 0u);
        }
    }
}
#else
/* REC armed (stopped: waiting for the first note) or a free take running (seq.c). The four
 * tracks stay in view below; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot;
    uint32_t sig, i, j, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    te_header(take ? "free take" : "rec ready", TE_RED, &head);
    sig = take * 1000003u + empty * 7u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
    if (ui.force || sig != body) {
        char b[24];
        body = sig;
        cv_begin(240, 106, C_BLACK);
        if (take) {                                     /* free take: the time, the loop it makes */
            uint32_t n = secs > 99u ? 99u : secs;
            te_disc(18, 30, 9, blink ? TE_RED : TE_REDDIM);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, trk_col(rt));
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
        } else {                                        /* ready: the first note starts everything */
            te_disc(120, 30, 22, blink ? TE_RED : TE_REDDIM);
            te_disc(120, 30, 9, C_BLACK);
            te_text_c(120, 62, empty ? "play freely" : "play a note", C_WHITE);
            te_text_c(120, 80, empty ? "then rec on the 1" : "it starts the loop", TE_G3);
        }
        cv_blit(0, 42);
    }
    /* the four tracks, compact: the one that records is framed in red */
    sig = rt * 3u + rec_wait + take * 5u + drum_kit_pos() * 977u + kit_col() * 13u;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, drum_kit_name(), sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (ui.force || sig != rows) {
        rows = sig;
        cv_begin(240, 76, C_BLACK);
        for (i = 0; i < NTRK; i++) {
            const track_t *t = &trk[i];
            uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
            int32_t y = (int32_t)i * 19;
            char b[16];
            if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
            cv_rect(4, y + 3, 12, 12, armed ? trk_col(i) : TRK_DIM(i));
            if (i == TRK_DRUM) str_cpy(b, drum_kit_name(), sizeof b);
            else trk_short_name(i, b);
            b[10] = 0;
            cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
            for (j = 0; j < 16u; j++) {
                uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
                if (z == a) z = a + 1u;
                for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
                cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? trk_col(i) : TRK_DIM(i)) : TE_G1);
            }
        }
        cv_blit(0, 148);
    }
    sig = take;
    if (ui.force || sig != foot) {
        foot = sig;
        cv_begin(240, 16, C_BLACK);
        cv_text(4, 0, &FONT_S, take ? "rec: close" : "rec: cancel", TE_G3);
        cv_text(236 - text_w(&FONT_S, take ? "play: drop" : "play: go"), 0, &FONT_S, take ? "play: drop" : "play: go", TE_G3);
        cv_blit(0, 224);
    }
}
#endif
