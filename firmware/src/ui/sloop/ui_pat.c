/* SPDX-License-Identifier: GPL-3.0-only */
/* The PATTERN layer (FELUCCA_PATTERNS, docs/PATTERNS-DESIGN.md 6.2): LFO held (tapped: the LFO pages as before;
 * LFO + HOME: locked open, the session screen). Included by ui_layers.c. The keys (black keys numbered from F#):
 *   white n          launch pattern n of the selected track at the end of its pattern (OCT- held: the next bar,
 *                    OCT+ held: now, where it is); stopped: at once
 *   black 1..4       select T1 T2 T3 DR          black 5   stop the track (at the end of its pattern)
 *   black 6 + n      STORE the working copy into n ("AGAIN" within 3 s over a used one)
 *   black 7 + a, b   COPY a to b (a synth track's to another: pick it between the keys)
 *   black 8 + n      CLEAR n (again within 3 s; stopped)
 *   black 9          DUPLICATE: the working copy into the first free slot, which it plays from then on
 *   black 10 + n     launch scene n (A..P): playing on the next bar, stopped at once (the SAVE layer's keeps its banks)
 *   KNOB 1..4        cue track k's next / previous stored pattern (at its end)
 * The tiles: the selected track's 16 slots (green: playing, amber: queued, grey: stored, dark: empty, "*": the
 * playing pattern changed since); the dials: each track's pattern, "3>5" when queued. */
static void pat_launch(uint32_t k, uint32_t s, uint32_t when);   /* storage/sections/pat.c, later in the unit */
static int pat_has(uint32_t k, uint32_t s);
static uint32_t pat_users(uint32_t k, uint32_t s, uint32_t but);
static uint32_t pat_free(uint32_t k);
static int pat_store_slot(uint32_t k, uint32_t s);
static int pat_copy(uint32_t k, uint32_t a, uint32_t k2, uint32_t b);
static int pat_write(uint32_t k, uint32_t s, uint32_t n);
static uint32_t pat_changed(void);
static int pat_scene_refs(uint32_t i, uint8_t *r);
static int proj_tmp_busy(void);
static uint8_t pat_mod;                               /* the black key held (6 store, 7 copy, 8 clear, 10 scene), 0 none */
static uint8_t pat_ca = 0xFF, pat_ck;                   /* COPY: the first slot and its track, 0xFF none yet */
static uint8_t pat_arm;                                 /* the slot (track << 4 | slot) + 1 a STORE / CLEAR waits on */
static uint32_t pat_arm_ms, pat_chg_ms, pat_chg;        /* (the "*" marks, refreshed twice a second) */

/* the "*" marks (the PATTERN layer's, the SAVE layer's), refreshed twice a second */
static void pat_refresh(void)
{
    if (fm1_ms - pat_chg_ms > 500u) {
        pat_chg_ms = fm1_ms;
        pat_chg = proj_tmp_busy() ? pat_chg : pat_changed();
    }
}
/* scene s is a scene and the tracks no longer play its patterns as stored (another one, or edited since): the SAVE
 * layer's "B*" (a track the scene keeps does not count) */
static int pat_scene_dirty(uint32_t s)
{
    uint8_t r[NTRK];
    uint32_t k;
    pat_refresh();
    if (!pat_scene_refs(s, r))
        return 0;
    for (k = 0; k < NTRK; k++)
        if (r[k] != PAT_KEEP && (r[k] != pat_cur[k] || ((pat_chg >> k) & 1u)))
            return 1;
    return 0;
}

/* black key k -> 1.. (F#, G#, A#, C#, D#, F#, ..), 0 a white key */
static uint32_t pat_black(uint32_t k)
{
    static const uint8_t B[12] = {0, 1, 0, 2, 0, 3, 0, 0, 4, 0, 5, 0};
    return B[k % 12u] ? (k / 12u) * 5u + B[k % 12u] : 0u;
}
/* "T2 5", "DR 16" -> b */
static void pat_name(char *b, uint32_t k, uint32_t s)
{
    b[0] = k == TRK_DRUM ? 'D' : 'T', b[1] = k == TRK_DRUM ? 'R' : (char)('1' + k), b[2] = ' ';
    fmt_int(b + 3, (int32_t)s + 1);
}
/* the confirm of STORE / CLEAR over a used slot: 1 go */
static int pat_again(uint32_t k, uint32_t s, const char *what)
{
    char b[16];
    uint32_t a = (k << 4 | s) + 1u;
    if (pat_arm == a && fm1_ms - pat_arm_ms < 3000u) {
        pat_arm = 0;
        return 1;
    }
    pat_arm = (uint8_t)a, pat_arm_ms = fm1_ms;
    pat_name(b, k, s);
    ui_say(what, b);
    return 0;
}
static void pat_layer_key(uint32_t k, int32_t w)
{
    uint32_t t = song.sel % NTRK, b = pat_black(k), ob = 1u << panel.btn[B_OCTDN], pb = 1u << panel.btn[B_OCTUP];
    char nm[8];
    if (b >= 1u && b <= 4u) {
        track_select(b - 1u);
        return;
    }
    if (b == 5u) {
        pat_launch(t, PAT_NONE, PW_END);
        ui_message(song.playing ? "STOP AT END" : "STOPPED");
        return;
    }
    if ((b >= 6u && b <= 8u) || b == 10u) {
        pat_mod = (uint8_t)b;
        pat_ca = 0xFF;
        return;
    }
    if (b == 9u) {
        uint32_t s = pat_free(t);
        pat_name(nm, t, s);
        if (s >= PAT_N)
            ui_message("NO FREE PATTERN");
        else if (!pat_store_slot(t, s))
            ui_say("DUPLICATE ", nm);
        return;
    }
    if (w < 0 || w >= (int32_t)PAT_N)
        return;
    pat_name(nm, t, (uint32_t)w);
    if (pat_mod == 10u) {                               /* SCENE n (as the SAVE layer's keys) */
        nm[0] = (char)('A' + w), nm[1] = 0;
        if (arrangement_clock.running)
            ui_message("SONG PLAYS");
        else if (!((arrangement_ready() >> w) & 1u) || (song.playing && !section_cue((uint32_t)w)))
            ui_say("EMPTY ", nm);
        else if (song.playing)
            ui_say("NEXT: ", nm);
        else
            section_load((uint32_t)w), ui_say("LOADED ", nm);
    } else if (pat_mod == 6u) {                         /* STORE */
        if ((!pat_has(t, (uint32_t)w) || pat_again(t, (uint32_t)w, "AGAIN: ")) && !pat_store_slot(t, (uint32_t)w))
            ui_say("STORED ", nm);
    } else if (pat_mod == 7u) {                         /* COPY a, then b */
        if (pat_ca == 0xFF) {
            pat_ca = (uint8_t)w, pat_ck = (uint8_t)t;
            ui_say("COPY ", nm);
        } else if ((!pat_has(t, (uint32_t)w) || pat_again(t, (uint32_t)w, "AGAIN: ")) &&
                   !pat_copy(pat_ck, pat_ca, t, (uint32_t)w)) {
            pat_ca = 0xFF;
            ui_say("COPIED ", nm);
        }
    } else if (pat_mod == 8u) {                         /* CLEAR */
        uint32_t u = pat_users(t, (uint32_t)w, PAT_ALL);
        if (pat_has(t, (uint32_t)w) && pat_again(t, (uint32_t)w, u ? "USED: AGAIN " : "AGAIN: ") && !pat_write(t, (uint32_t)w, 0))
            ui_say("CLEARED ", nm);
    } else {
        uint32_t when = (fm1_in.buttons & ob) ? PW_BAR : (fm1_in.buttons & pb) ? PW_NOW : PW_END;
        pat_launch(t, (uint32_t)w, when);
        if (!song.playing)
            ui_say("LOADED ", nm);
        else
            ui_say(nm, when == PW_BAR ? " NEXT BAR" : when == PW_NOW ? " NOW" : " AT END");
    }
}
static void pat_layer_up(uint32_t k)
{
    if (pat_black(k) == pat_mod)
        pat_mod = 0;
}
/* KNOB k turned s: track k's next / previous stored pattern, cued at its end */
static void pat_knob(uint32_t k, int32_t s)
{
    uint32_t c = pat_req[k] < PAT_N ? pat_req[k] : pat_cur[k] < PAT_N ? pat_cur[k] : (s > 0 ? PAT_N - 1u : 0u), i;
    for (i = 1; i <= PAT_N; i++) {
        uint32_t n = (c + (s > 0 ? i : PAT_N - i)) % PAT_N;
        if (pat_has(k, n)) {
            pat_launch(k, n, PW_END);
            return;
        }
    }
}
/* the screen (ui_layers.c layer_screen_draw): tiles, the subtitle, the dials */
static void pat_layer_draw(tile_t *tl, char *sub, const char **lab, char (*v)[10], int32_t *ratio)
{
    static const char *const L[NTRK] = {"t1", "t2", "t3", "dr"};
    uint32_t t = song.sel % NTRK, i;
    pat_refresh();
    str_cpy(sub, pat_mod == 6u ? "store" : pat_mod == 7u ? (pat_ca == 0xFF ? "copy: from" : "copy: to") :
                 pat_mod == 8u ? "clear" : "at end", 24);
    for (i = 0; i < PAT_N; i++) {
        int used = pat_has(t, i), cur = pat_cur[t] == i, q = pat_req[t] == i;
        fmt_int(tl[i].lab, (int32_t)i + 1);
        if (cur && ((pat_chg >> t) & 1u))
            str_cpy(tl[i].lab + str_len(tl[i].lab), "*", 2);
        tl[i].bg = cur ? C_OK : q ? C_WARN : used ? TE_G3 : TE_G1;
        tl[i].fg = cur || q || used ? C_BLACK : TE_G3;
        tl[i].top = used ? trk_col(t) : 0;
    }
    for (i = 0; i < NTRK; i++) {
        lab[i] = L[i];
        if (pat_cur[i] < PAT_N)
            fmt_int(v[i], pat_cur[i] + 1);
        else
            str_cpy(v[i], "-", 2);
        if (pat_req[i] != PAT_NONE) {
            str_cpy(v[i] + str_len(v[i]), ">", 2);
            if (pat_req[i] < PAT_N)
                fmt_int(v[i] + str_len(v[i]), pat_req[i] + 1);
            else
                str_cpy(v[i] + str_len(v[i]), "-", 2);
        }
        ratio[i] = pat_cur[i] < PAT_N ? (int32_t)pat_cur[i] * 1000 / 15 : -1;
    }
}
