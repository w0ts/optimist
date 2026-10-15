/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v9 on the host (firmware/src/io/editor/ed_sync9.c, ed_status.c's STREAM, meters.c): the device side of "only what
 * changed": every track's parameters, steps and drum lanes pushed, a burst coalesced to the last value per key per
 * window, the editor's own writes not echoed, pushes only into a half-empty SysEx ring with no request waiting (a reply
 * never waits behind one), the status stream at 25 Hz at most and only while something changes, and the meter tap: the
 * TRACKS screen and the editor each get every peak, neither steals the other's; a peak never latches (the Optimist 0.1
 * meters read trk.peak, which only the TRACKS screen ever cleared). Also: the bandwidth of a busy song with knob sweeps
 * on every track, and the scan's cost in host instructions per window (printed). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_OTA 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) {}
static void arrangement_apply(uint32_t s) { (void)s; }
static uint32_t arrangement_ready(void) { return 0; }
static void song_backup(void) {}
static uint32_t arr_saves;
static void arrangement_save(void) { arr_saves++; }
static void song_restore(void) {}
/* the audio ISR's side the meter tap reads (audio.c) */
#define OUT_SHIFT 7
#define HALF_WORDS (HALF_FRAMES * 2u)
static volatile uint32_t audio_halves;
static int32_t abuf[2u * HALF_WORDS];
static uint32_t host_half;
static uint32_t fm1_audio_free_half(void) { return host_half; }
static uint32_t ticks;
static uint32_t fm1_ticks(void) { return ticks; }
#define FM1_TICKS_PER_US 1u
#include "../firmware/src/ui/meters.c"
/* the reply builder as editor.c has it; ed_send records the frames */
static uint8_t ed_out[600];
static uint32_t ed_n;
static void ed_begin(uint32_t cmd) { ed_out[0] = 0xF0; ed_out[1] = 0x7D; ed_out[2] = 0x46; ed_out[3] = 0x4C; ed_out[4] = (uint8_t)cmd; ed_n = 5; }
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static struct { uint32_t force; } ui;
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
static void ed_str(const char *s, uint32_t max) { uint32_t n = 0; if (s) for (; s[n] && n < max; n++) ed_b((uint8_t)s[n]); ed_b(0); }
#define NF 4096
static uint8_t fr[NF][128];
static uint32_t frn[NF], nf, bytes_total;
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    if (nf < NF) { memcpy(fr[nf], ed_out, ed_n < 128u ? ed_n : 128u); frn[nf++] = ed_n; }
    bytes_total += ed_n;
    so_w += (ed_n + 2u) / 3u;                        /* (in the ring until the host takes it) */
}
static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }
/* the drum lane helpers (ed_drums.c, ed_dsend.c): the same 12 bytes and pack7 */
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++) m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++) ed_b(p[j]);
        p += k, n -= k;
    }
}
static void ed_lane_get(uint32_t l, uint8_t *b) { memcpy(b, dl.ofs[l], DE_N); b[8] = dl.src[l]; memcpy(b + 9, dl.ref[l], 3); }
#define ED_V2 0x40u
static void ed_snd_get(const uint16_t *w, uint8_t *b, uint32_t n)
{
    uint32_t l;
    for (l = 0; l < n; l++, b += 3) { b[0] = (uint8_t)(int8_t)dsend_rev(w[l]); b[1] = (uint8_t)dsend_dly(w[l]); b[2] = (uint8_t)dsend_cho(w[l]); }
}
static uint32_t song_sig_host, snap_sig_host;
#define ED9_SONG_SIG() song_sig_host
#define ED9_SNAP_SIG() snap_sig_host
static uint32_t ed9_ends_host;                                 /* (WATCH bit 4, v12: a synth step pushed with its own note ends) */
#define ED9_ENDS ed9_ends_host
static int project_used(uint32_t s) { return s == 1u; }
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s) { return (uint32_t)project_used(s); }   /* (sl24_guard.c: no slot of 2.4's here) */
static void sl24_auto_import(void) {}                          /* (sl24_guard.c: no 2.4 autosave here) */
#endif
#include "../firmware/src/io/editor/ed_steps.c"
#include "../firmware/src/io/editor/ed_status.c"
#include "../firmware/src/io/editor/ed_song.c"
#include "../firmware/src/io/editor/ed_dlan.c"
static uint8_t settings_later;                                 /* (model.c: the settings are saved a moment after an edit) */
static const char *preset_name(uint32_t e, uint32_t k) { return k == 0xFFu || !ENGINES[e]->npresets ? "INIT" : ENGINES[e]->presets[k % ENGINES[e]->npresets].name; }
#include "../firmware/src/core/favorites.c"
#include "../firmware/src/io/editor/ed_fav.c"
#include "../firmware/src/io/editor/ed_fxp.c"
#include "../firmware/src/io/editor/ed_fxs.c"
#include "../firmware/src/io/editor/ed_sync9.c"

static int fails;

/* v13 FXP: the device's own parameter tables as JSON, for the web editor's mock (web/editor.html FXP_TABLE, tools/gen_fxp_table.py):
 * per kind of owner (track, master, usb, return, lane) and type it may hold, the parameters (label, fmt, min, max, def, unit,
 * names). tests/fxp_table.json is the golden: this test fails when the firmware's tables moved (FXP_WRITE=1 rewrites it) */
#if FELUCCA_ED_FXP && FELUCCA_ED_DLAN
static void fxp_json_desc(FILE *f, const param_desc_t *d)
{
    uint32_t i;
    fprintf(f, "{\"label\":\"%s\",\"fmt\":%u,\"min\":%d,\"max\":%d,\"def\":%d,\"unit\":\"%s\"", d->label, (unsigned)d->fmt, d->min, d->max, d->def, d->unit ? d->unit : "");
    if (d->fmt == F_ENUM && d->names) {
        fprintf(f, ",\"names\":[");
        for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)
            fprintf(f, "%s\"%s\"", i ? "," : "", d->names[i]);
        fprintf(f, "]");
    }
    fprintf(f, "}");
}
static void fxp_json(FILE *f)
{
    static const struct { const char *name; uint32_t ow; } K[] = {{"track", 0}, {"master", FXP_MST}, {"usb", FXP_USB}, {"return", FXP_RET0 + 1}, {"lane", FXP_LANE0}};
    uint32_t k, t, i, first, nk = 0;
    fprintf(f, "{\n");
    for (k = 0; k < 5u; k++) {
        uint32_t ow = K[k].ow, sl = 0, ok = fxp_owner(ow) ? fxp_ok(ow) : 0;
        if (!fxp_owner(ow))
            continue;
        fprintf(f, "%s \"%s\": {\"slots\": %u, \"types\": [\n", nk++ ? ",\n" : "", K[k].name, fxp_slots(ow));
        for (first = 1, t = 1; t < FXT_N; t++) {
            if (!(ok >> t & 1u))
                continue;
            fxp_set_type(ow, sl, t);
            fprintf(f, "%s   {\"id\": %u, \"params\": [", first ? "" : ",\n", t);
            first = 0;
            for (i = 0; i < fxp_n(ow, sl); i++) {
                int16_t *vp;
                const param_desc_t *d = fxp_loc(ow, sl, i, &vp);
                fprintf(f, "%s", i ? ", " : "");
                fxp_json_desc(f, d);
            }
            fprintf(f, "]}");
        }
        fprintf(f, "\n  ]");
        if (ow == FXP_USB) {
            fprintf(f, ", \"values\": [");
            for (i = 0; i < fxp_n(ow, 2); i++) {
                int16_t *vp;
                fprintf(f, "%s", i ? ", " : "");
                fxp_json_desc(f, fxp_loc(ow, 2, i, &vp));
            }
            fprintf(f, "]");
        }
        fprintf(f, "}");
        fxp_set_type(ow, sl, FXT_NONE);
    }
    {   /* the drum lanes' mix, LFO and the mutes (ed_dlan.c): the parameters of group g, lane l */
        static const struct { const char *name; uint32_t g, l; } G[] = {{"mix", DLG_MIX, 0}, {"accent", DLG_MIX, DRUM_ACC}, {"lfo", DLG_LFO, 0}, {"track", DLG_TRK, 0}};
        uint32_t n = 0;
        for (k = 0; k < 4u; k++) {
            if (!dln_ok(G[k].g, G[k].l))                /* (the ACCENT lane: only with the automation store) */
                continue;
            fprintf(f, "%s \"%s\": [", n++ ? ",\n" : ",\n \"dlane\": {\n", G[k].name);
            for (i = 0; i < dln_n(G[k].g, G[k].l); i++) {
                fprintf(f, "%s", i ? ", " : "");
                fxp_json_desc(f, dln_d(G[k].g, G[k].l, i));
            }
            fprintf(f, "]");
        }
        fprintf(f, "\n }");
    }
    fprintf(f, "\n}\n");
}
#endif
static void check(int c, const char *what) { printf("%-100s %s\n", what, c ? "ok" : "FAIL"); if (!c) fails++; }
static void drain(void) { so_r = so_w; }               /* the host took everything */
static void run_ms(uint32_t ms)                         /* the main loop for ms: a scan when due, the host drains */
{
    while (ms--) {
        fm1_ms++;
        ed9_sync(fm1_ms);
        drain();
    }
}
static uint32_t count(uint32_t cmd, uint32_t from) { uint32_t i, n = 0; for (i = from; i < nf; i++) n += fr[i][4] == cmd; return n; }
/* the PARAMS entries for (where, id) in frames from..nf: how many, the last value */
static uint32_t entries(uint32_t from, uint32_t where, uint32_t id, int32_t *last)
{
    uint32_t i, k, n = 0;
    for (i = from; i < nf; i++)
        if (fr[i][4] == ED_PARAMS)
            for (k = 5; k + 4u <= frn[i] - 1u; k += 4)
                if (fr[i][k] == where && fr[i][k + 1] == id) { n++; *last = (int32_t)(fr[i][k + 2] | fr[i][k + 3] << 7) - 8192; }
    return n;
}

int main(void)
{
    uint32_t n0, i, k;
    int32_t v = 0;
    song.sel = 0;
    fm1_ms = 1000;
    ed9_shadow_all();
    run_ms(100);
    check(nf == 0, "v9: nothing changed: nothing pushed");

    /* any track, coalesced: a knob swept 40 times inside one window goes out once, with its last value */
    e9.win_ms = fm1_ms;                                  /* (a window just began) */
    for (i = 0; i < 40; i++) trk[2].p[P_PAN] = (int16_t)(i - 20);
    n0 = nf;
    run_ms(ED9_WIN);
    check(entries(n0, 2, P_PAN, &v) == 1 && v == 19, "v9 PARAMS: a burst inside a window: one entry, the last value, with its track (not selected)");
    n0 = nf;
    for (i = 0; i < 100; i++) { trk[1].p[P_CHOR] = (int16_t)(i & 127); run_ms(1); }   /* 1 change a ms for 100 ms */
    run_ms(ED9_WIN);
    k = entries(n0, 1, P_CHOR, &v);
    check(k >= 4 && k <= 100 / ED9_WIN + 1 && v == 99, "v9 PARAMS: a sweep (1 change / ms): at most one entry per 20 ms window, ends on the last value");
    n0 = nf;
    song.g[G_DRLVL] = 44;
    trk[song.sel].p[P_DLY] = 77;
    run_ms(ED9_WIN);
    check(entries(n0, ED9_GLOBAL, G_DRLVL, &v) == 1 && v == 44 && entries(n0, song.sel, P_DLY, &v) == 1 && v == 77,
          "v9 PARAMS: a global (where 127) and the selected track's parameter (its track number)");

    /* the editor's own writes: known, nothing echoes */
    n0 = nf;
    trk[0].p[P_REV] = 12; ed9_known_p(0, P_REV);
    song.g[G_DRLVL] = 90; ed9_known_g(G_DRLVL);
    run_ms(3 * ED9_WIN);
    check(nf == n0, "v9: the editor's own writes (ed9_known_*): no push back");

    /* steps of any track, with their bytes; a run in one frame; the drum track's lanes */
    n0 = nf;
    for (i = 4; i < 7; i++) { trk[1].step[i].n = 1; trk[1].step[i].note[0] = (uint8_t)(60 + i); trk[1].step[i].time = ST_NOTE; trk[1].step[i].vel = 100; }
    dstep_set(&trk[TRK_DRUM].dstep[9], 3, LV_HARD, 1);
    run_ms(ED9_WIN);
    {
        int okp = count(ED_STEPS, n0) == 2u, found1 = 0, found3 = 0;
        for (i = n0; i < nf; i++) {
            if (fr[i][4] != ED_STEPS) continue;
            if (fr[i][5] == 1) {
                found1 = fr[i][6] == 4 && fr[i][7] == 3 && frn[i] == 9u + 3u * 11u && fr[i][8] == 1 && fr[i][9] == 64 && fr[i][8 + 11] == 1 && fr[i][9 + 11] == 65
                         && fr[i][8 + 22 + 5] == ST_NOTE;
            } else if (fr[i][5] == TRK_DRUM) {
                uint32_t on = fr[i][8] | fr[i][9] << 7, lv = fr[i][11] | fr[i][12] << 7, rt = fr[i][16] | fr[i][17] << 7;
                found3 = fr[i][6] == 9 && fr[i][7] == 1 && frn[i] == 9u + 13u && on == 8u && ((lv >> 6) & 3u) == LV_HARD && ((rt >> 6) & 3u) == 1u;
            }
        }
        check(okp && found1 && found3, "v9 STEPS: three changed steps of track 2 in one frame (TRACK_STEP bytes), a drum step (DRUM_STEP bytes)");
    }
    n0 = nf;
    ed9_known_step(1, 4);                              /* (unchanged: no push) */
    trk[2].step[0].vel = 5; ed9_known_step(2, 0);      /* the editor's own STEP write */
    run_ms(2 * ED9_WIN);
    check(count(ED_STEPS, n0) == 0, "v9 STEPS: the editor's own step writes are known");

    /* drum lanes */
#if DL_ANY
    n0 = nf;
    dl.src[5] = (uint8_t)(DL_KIT0 + 2);
    run_ms(ED9_WIN);
    check(count(ED_LANE_PUSH, n0) == 1 && (fr[nf - 1][5] & 15u) == 5u, "v9 LANE: a drum lane's source changed: that lane pushed (replaces the editor's kit polling)");
    n0 = nf;
    dl.ofs[6][0] = 3; ed9_known_lanes();
    run_ms(2 * ED9_WIN);
    check(count(ED_LANE_PUSH, n0) == 0, "v9 LANE: the editor's own lane writes are known");
#endif

    /* TRACKS and SONG */
    n0 = nf;
    song.solo = 2;
    run_ms(ED9_WIN);
    check(count(ED_TRACKS_PUSH, n0) == 1 && fr[nf - 1][5] == song.sel && fr[nf - 1][6] == NTRK && fr[nf - 1][7 + 6 * NTRK] == 2,
          "v9 TRACKS: a solo on the device: the TRACK reply's shape (selected, NTRK, per track, the solo mask)");
    n0 = nf;
    snap_sig_host = 7;
    run_ms(ED9_SLOW + ED9_WIN);
    check(count(ED_SONG_PUSH, n0) == 1 && fr[nf - 1][5] == 2u && fr[nf - 1][10] == 1u, "v9 SONG: the snapshot list moved: SONG with the sections stored and the count 1");

    /* v10 FX_PUSH (ed_fxs.c): the slots, the COMP settings or a drum sound's insert changed on the device: one frame, the
     * FX (86) reply's bytes with op 0; the editor's own FX writes are known (no push) */
    n0 = nf;
    fxs_load(1, FXT_CHO);                              /* (FX > SLOTS: CHO into R1 swaps with REV) */
    run_ms(ED9_WIN);
    check(count(ED_FX_PUSH, n0) == 1 && frn[nf - 1] == 7u + ED_FX_N && fr[nf - 1][5] == 0u && fr[nf - 1][6] == FXT_NONE &&
          fr[nf - 1][7] == FXT_CHO && fr[nf - 1][8] == FXT_DLY && fr[nf - 1][9] == FXT_REV,
          "v10 FX_PUSH: a slot loaded on the device (a swap): one frame, op 0, the four slots");
    n0 = nf;
    dins_amt[1][2] = 90;
#if FELUCCA_MASTER_COMP
    ins_cset[0][0] = 5;
#endif
    run_ms(ED9_WIN);
    check(count(ED_FX_PUSH, n0) == 1 && fr[nf - 1][6 + 4 + 3 + DRUM_LANES + 2] == 90u
#if FELUCCA_MASTER_COMP
          && fr[nf - 1][10] == 5u
#endif
          , "v10 FX_PUSH: a drum sound's COMP and the COMP's RATIO: pushed (sounds' DIST x 16, then COMP x 16)");
    {   /* INFO tag 0x56 (ed_fx_info): every type built is listed once, its SOUND 3 value id the fx_slots.c lane id (127 none: the inserts) */
        uint32_t t, i, n, ok = 1, seen = 0;
        ed_begin(ED_FX);
        ed_fx_info();
        n = ed_out[8];
        ok = ed_out[5] == 0x56u && ed_out[7] == 1u && n == (uint32_t)__builtin_popcount((unsigned)(FXT_BUILT & ~1u));
        for (i = 0; ok && i < n; i++) {
            const uint8_t *e = &ed_out[9u + 4u * i];
            uint32_t want = e[0] == FXT_CHO ? 18u : e[0] == FXT_DLY ? 17u : e[0] == FXT_REV ? 16u : e[0] == FXT_COMP ? 20u :
                            e[0] == FXT_DIST ? (FELUCCA_FX_DIST ? 19u : 127u) : 127u;
            ok = e[0] < FXT_N && (FXT_BUILT >> e[0] & 1u) && e[3] == want && e[3] == ED_FX_LID[e[0]] && !(seen >> e[0] & 1u);
            seen |= 1u << e[0];
        }
        for (t = 1; t < FXT_N; t++)   /* (every type, built or not: JUNO CHORUS, type 17, is 127 like the other inserts) */
            ok = ok && ED_FX_LID[t] == (t == FXT_CHO ? 18u : t == FXT_DLY ? 17u : t == FXT_REV ? 16u : t == FXT_COMP ? 20u : t == FXT_DIST ? 19u : 127u)
                     && ((seen >> t & 1u) == (FXT_BUILT >> t & 1u));
        check(ok, "v10 INFO tag 0x56: the types built, each with its SOUND 3 value id (JUNO and the other inserts 127, not 0)");
    }
    n0 = nf;
    {
        static const uint8_t SL[] = {1, FXT_REV, FXT_REV, FXT_DLY, 99}, IN[] = {3, 4, 0, 77}, BAD[] = {3, 16, 0, 1}, OP[] = {9};
        int r1, r2, r3, r4;
        ed_begin(ED_FX); r1 = ed_fxs(ED_FX, SL, sizeof SL); ed9_known_fx();
        check(r1 && fxs_slot[0] == FXT_NONE && fxs_slot[1] == FXT_REV && fxs_slot[2] == FXT_DLY && fxs_slot[3] == FXT_NONE &&
              ed_out[5] == 1u && ed_out[6] == FXT_NONE && ed_out[7] == FXT_REV && ed_n == 6u + ED_FX_N,
              "v10 FX op 1: the layout as fxs_set takes it (a type twice: its later slot empty; an unknown id: empty; slot 0's "
              "send onto the first empty return, R1), the state replied");
        ed_begin(ED_FX); r2 = ed_fxs(ED_FX, IN, sizeof IN); ed9_known_fx();
        ed_begin(ED_FX); r3 = ed_fxs(ED_FX, BAD, sizeof BAD);
        ed_begin(ED_FX); r4 = ed_fxs(ED_FX, OP, sizeof OP);
        check(r2 && dins_amt[0][4] == (FELUCCA_FX_DIST ? 77u : 0u) && !r3 && !r4, "v10 FX op 3: a drum sound's DIST; lane 16 or op 9: no reply");
    }
    {   /* the track insert slots (2026-10-12): op 4 a track's S1 S2, op 5 its COMP settings, op 6 the drum send levels;
         * the state's tail: T1..DR's slots, their RATIO ATK REL, the drum track's REV DLY CHO levels */
        static const uint8_t I4[] = {4, 2, FXT_COMP, FXT_COMP}, I5[] = {5, 1, 3, 2, 5}, I6[] = {6, 64, 127, 200}, B4[] = {4, 4, 1, 1};
        const uint32_t T = 6u + 4u + 3u + 2u * DRUM_LANES;   /* (ed_out: the tail's first byte) */
        int r1, r2, r3, r4;
        ed_begin(ED_FX); r1 = ed_fxs(ED_FX, I4, sizeof I4); ed9_known_fx();
        check(r1 && ins_slot[2][0] == FXT_COMP && ins_slot[2][1] == FXT_NONE && ed_out[T + 4u] == FXT_COMP &&
              ed_out[T + 5u] == FXT_NONE && ed_n == 6u + ED_FX_N,
              "v10 FX op 4: T3's slots (the same type twice: S2 empty), the slots in the state's tail");
        ed_begin(ED_FX); r2 = ed_fxs(ED_FX, I5, sizeof I5); ed9_known_fx();
        ed_begin(ED_FX); r3 = ed_fxs(ED_FX, I6, sizeof I6); ed9_known_fx();
        ed_begin(ED_FX); r4 = ed_fxs(ED_FX, B4, sizeof B4);
        check(r2 && r3 && !r4 && dsend_kit[0] == 64 && dsend_kit[1] == 127 && dsend_kit[2] == 127 &&
              ed_out[T + 8u + 3u * NTRK] == 64u
#if FELUCCA_MASTER_COMP
              && ins_cset[1][0] == 3 && ins_cset[1][2] == 5 && ins_cset[0][0] == 5 && ed_out[T + 8u + 3u] == 3u
#endif
              , "v10 FX op 5: T2's own COMP settings; op 6 the drum send levels (past 127: 127); track 4: no reply");
        ins_defaults();
        dsend_kit[0] = dsend_kit[1] = dsend_kit[2] = DKS_DEF;
#if FELUCCA_MASTER_COMP
        ins_cset[0][0] = ins_cset[1][0] = 1, ins_cset[1][1] = 4, ins_cset[1][2] = 6;
#endif
        ed9_known_fx();
    }
    run_ms(2 * ED9_WIN);
    check(count(ED_FX_PUSH, n0) == 0, "v10 FX_PUSH: the editor's own FX writes are known");
    fxs_set(FXS_DEF);
    memset(dins_amt, 0, sizeof dins_amt);
    ed9_known_fx();


#if FELUCCA_ED_FXP
    /* v13 FXP (ed_fxp.c, cmd 94): every effect slot by owner (tracks, master, USB input, returns, drum sounds), a type's
     * parameters from the device's own descriptors; a set is clamped; the editor's writes are known, a device-side change
     * of a setting no other push covers (an EQ band, the lane slots) goes out in the FX push */
    {
        static const uint8_t G0[] = {0, 0}, G9[] = {0, 99}, OP9[] = {9, 0};
        uint8_t q[8];
        int r;
        uint32_t ow, sl;
        ins_defaults();
        fxs_set(FXS_DEF);
        ed_begin(ED_FXP); r = ed_fxp(ED_FXP, G0, sizeof G0);
        check(r && ed_out[5] == 0u && ed_out[6] == 0u && ed_out[7] == 2u && ed_out[8] == INS_DEF0 && ed_out[10] == INS_DEF1 &&
              ed_n == 5u + 3u + 4u + 5u, "v13 FXP op 0: T1's two slots (types and parameter counts), the types it may hold");
        ed_begin(ED_FXP); r = ed_fxp(ED_FXP, G9, sizeof G9);
        check(!r, "v13 FXP: an owner past the table: no reply");
        ed_begin(ED_FXP); r = ed_fxp(ED_FXP, OP9, sizeof OP9);
        check(!r, "v13 FXP: an unknown op: no reply");
        {   /* every owner, slot and allowed type: each parameter answers a label and a value in its range; the type reads back */
            uint32_t bad = 0, t, i, tried = 0;
            for (ow = 0; ow < FXP_NOWN; ow++) {
                if (!fxp_owner(ow))
                    continue;
                for (sl = 0; sl < fxp_slots(ow); sl++)
                    for (t = 0; t < FXT_N; t++) {
                        uint32_t ok = 1;
                        if (t != FXT_NONE && !(fxp_ok(ow) >> t & 1u))
                            continue;
                        if (!(ow == FXP_USB && sl == 2u))
                            fxp_set_type(ow, sl, t), ok = fxp_type(ow, sl) == t, tried++;
                        for (i = 0; i < fxp_n(ow, sl); i++) {
                            int16_t *vp;
                            const param_desc_t *d = fxp_loc(ow, sl, i, &vp);
                            ok = ok && d && vp && d->label && d->label[0] && *vp >= d->min && *vp <= d->max;
                        }
                        if (!ok)
                            printf("owner %u slot %u type %u\n", ow, sl, t);
                        bad += !ok;
                    }
            }
            printf("(%u owner / slot / type combinations)\n", tried);
            check(!bad, "v13 FXP: every owner, slot and allowed type: its parameters have labels, values in range, and the type reads back");
            ins_defaults();
            fxs_set(FXS_DEF);
#if FELUCCA_USB_AUDIO
            ut_defaults();
#endif
            memset(dins_amt, 0, sizeof dins_amt);
            ed9_known_fx();
        }
#if FELUCCA_INS_EQ3
        {   /* EQ3 into T2's S1: six parameters; LO is dB, ON an enum; a band set is clamped (KILL at the bottom) */
            static const uint8_t TY[] = {1, 1, 0, FXT_EQ3}, DS0[] = {2, 1, 0, 0}, DS3[] = {2, 1, 0, 3}, VA[] = {3, 1, 0};
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, TY, sizeof TY);
            check(r && ins_slot[1][0] == FXT_EQ3 && ed_out[5] == 1u && ed_out[8] == FXT_EQ3 && ed_out[9] == 6u, "v13 FXP op 1: EQ3 into T2's S1: it has six parameters");
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, DS0, sizeof DS0);
            check(r && ed_out[5] == 2u && ed_out[9] == F_INT && ed_out[8] == 0u, "v13 FXP op 2: EQ3's LO is an integer (dB)");
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, DS3, sizeof DS3);
            check(r && ed_out[9] == F_ENUM, "v13 FXP op 2: EQ3's fourth parameter is ON (an enum)");
            q[0] = 4, q[1] = 1, q[2] = 0, q[3] = 0, q[4] = (uint8_t)((-100 + 8192) & 127), q[5] = (uint8_t)((-100 + 8192) >> 7);
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, q, 6);
            check(r && eq3_v[1][0] == EQ_KILL, "v13 FXP op 4: a band set below its range is clamped to KILL");
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, VA, sizeof VA);
            check(r && ed_out[8] == 6u && ed_n == 5u + 4u + 12u, "v13 FXP op 3: the slot's six values");
        }
        n0 = nf;
        eq3_v[0][1] = 3;
        run_ms(ED9_WIN);
        check(count(ED_FX_PUSH, n0) == 1, "v13 FXP: an EQ band moved on the device: the FX push goes out");
        n0 = nf;
        {
            static const uint8_t RT[] = {1, FXP_RET0 + 1, 0, FXT_EQ3};
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, RT, sizeof RT); ed9_known_fx();
            check(r && fxs_slot[2] == FXT_EQ3 && ed_out[8] == FXT_EQ3 && ed_out[9] == 7u && ed_n == 5u + 3u + 2u + 5u + 1u && ed_out[ed_n - 1u] == fxs_line[2],
                  "v13 FXP op 1: EQ3 into R2: its line's RET and the six; the reply ends with the return's send line");
            q[0] = 4, q[1] = FXP_RET0 + 1, q[2] = 0, q[3] = 5, q[4] = (uint8_t)((10 + 8192) & 127), q[5] = (uint8_t)((10 + 8192) >> 7);
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, q, 6); ed9_known_fx();
            check(r && eq3_v[NTRK + 2u + fxs_line[2]][3] == 10, "v13 FXP op 4: a return's EQ3 FREQ L (the owner of its line)");
        }
        run_ms(2 * ED9_WIN);
        check(count(ED_FX_PUSH, n0) == 0, "v13 FXP: the editor's own writes are known (no push back)");
        fxs_set(FXS_DEF);
        ins_defaults();
        eq3_defaults();
        ed9_known_fx();
#endif
        if (LIN_OK >> FXT_FOLD & 1u) {                 /* a drum sound's slot: the type, then the amount by the lane's rule */
            static const uint8_t LT[] = {1, FXP_LANE0 + 2, 0, FXT_FOLD};
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, LT, sizeof LT);
            check(r && lin_slot[2][0] == FXT_FOLD && ed_out[8] == FXT_FOLD && ed_out[9] == 1u, "v13 FXP op 1: FOLD into a drum sound's S1");
            q[0] = 4, q[1] = FXP_LANE0 + 2, q[2] = 0, q[3] = 0, q[4] = (uint8_t)((300 + 8192) & 127), q[5] = (uint8_t)((300 + 8192) >> 7);
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, q, 6);
            check(r && dins_amt[0][2] == 127u, "v13 FXP op 4: the drum sound's amount, clamped to the type's range");
            lin_defaults();
            memset(dins_amt, 0, sizeof dins_amt);
        }
#if FELUCCA_MASTER_COMP
        {   /* the master's DYN is the GLO > COMP / LIMIT globals; a track's CEIL is its own setting */
            uint8_t m[8] = {1, FXP_MST, 0, FXT_DYN};
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, m, 4);
            check(r && mst_slot[0] == FXT_DYN && ed_out[9] == 6u, "v13 FXP op 1: DYN into the master's S1: THRS RATIO ATK REL GAIN CEIL");
            q[0] = 4, q[1] = FXP_MST, q[2] = 0, q[3] = 5, q[4] = (uint8_t)((GP[G_CCEIL].max + 8192) & 127), q[5] = (uint8_t)((GP[G_CCEIL].max + 8192) >> 7);
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, q, 6);
            check(r && song.g[G_CCEIL] == GP[G_CCEIL].max, "v13 FXP op 4: the master's DYN CEIL is G_CCEIL");
            m[1] = 0, m[2] = 0;
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, m, 4);
            q[0] = 4, q[1] = 0, q[2] = 0, q[3] = 4, q[4] = (uint8_t)((GP[G_CCEIL].max + 8192) & 127), q[5] = (uint8_t)((GP[G_CCEIL].max + 8192) >> 7);
            ed_begin(ED_FXP); r = ed_fxp(ED_FXP, q, 6);
            check(r && ins_cset[0][3] == GP[G_CCEIL].max, "v13 FXP op 4: a track's DYN GAIN is its own setting (ins_cset)");
            ins_defaults();
        }
#endif
    }

    {   /* the tables as JSON: the golden (an every-item build only: the default build has most types off) */
#if FELUCCA_INS_EQ3 && FELUCCA_INS_GATER && FELUCCA_INS_JCHORUS && FELUCCA_USB_AUDIO
        const char *gold = "tests/fxp_table.json";
        FILE *f = tmpfile();
        char *a, *b;
        long na, nb;
        fxp_json(f);
        fflush(f);
        na = ftell(f);
        a = malloc((size_t)na + 1u), b = malloc(1u << 20);
        rewind(f);
        na = (long)fread(a, 1, (size_t)na, f);
        fclose(f);
        if (getenv("FXP_WRITE") && (f = fopen(gold, "w"))) {
            fwrite(a, 1, (size_t)na, f);
            fclose(f);
        }
        f = fopen(gold, "r");
        nb = f ? (long)fread(b, 1, 1u << 20, f) : -1;
        if (f)
            fclose(f);
        check(nb == na && !memcmp(a, b, (size_t)na), "v13 FXP: the parameter tables equal tests/fxp_table.json (FXP_WRITE=1 rewrites it; web/editor.html's mock follows: tools/gen_fxp_table.py)");
        free(a), free(b);
#endif
    }

#else
    {   /* the item ED_FXP left out: no command, no INFO tag (ed_fxp.c's stubs), the FX push's signature unchanged by it */
        static const uint8_t G0[] = {0, 0};
        ed_begin(ED_FXP);
        check(!ed_fxp(ED_FXP, G0, sizeof G0) && ed_n == 5u, "ED_FXP off: cmd 94 is not handled (no reply)");
        ed_begin(1); ed_fxp_info();
        check(ed_n == 5u, "ED_FXP off: INFO has no tag 0x57");
    }
#endif

#if FELUCCA_ED_DLAN
    /* v13 DLAN (ed_dlan.c, cmd 95): a lane's PAN MUTE SOLO SEQ MUTE, its LFO, the sequencer mutes */
    {
        static const uint8_t M0[] = {0, 0, 3}, MD[] = {2, 0, 3, 0}, LD[] = {2, 1, 3, 4}, BAD[] = {0, 3, 0}, OP1[] = {1, 0, 0};
        uint8_t q[8];
        int r;
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, M0, sizeof M0);
        check(r && ed_out[5] == 0u && ed_out[8] == 4u, "v13 DLAN op 0: a lane's mix has four parameters");
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, MD, sizeof MD);
        check(r && ed_out[9] == F_BIPCT && ed_out[8] == 0u, "v13 DLAN op 2: PAN is the device's descriptor (a bipolar percentage)");
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, LD, sizeof LD);
        check(r && ed_out[9] == F_ENUM, "v13 DLAN op 2: the lane LFO's fifth parameter is DEST (a list)");
        q[0] = 4, q[1] = DLG_MIX, q[2] = 3, q[3] = 0, q[4] = (uint8_t)((-100 + 8192) & 127), q[5] = (uint8_t)((-100 + 8192) >> 7);
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, q, 6);
        check(r && dlm_pan[3] == -64, "v13 DLAN op 4: a lane's PAN, clamped");
        q[3] = 1, q[4] = (uint8_t)((1 + 8192) & 127), q[5] = (uint8_t)((1 + 8192) >> 7);
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, q, 6);
        q[3] = 2;
        ed_begin(ED_DLAN); r = r && ed_dlan(ED_DLAN, q, 6);
        check(r && dlm_muted(3) && dlm_soloed(3) && !dlm_lane_heard(3) && !dlm_lane_heard(2), "v13 DLAN op 4: MUTE and SOLO of a lane (the device's masks: a soloed and muted lane stays silent, the others go quiet)");
        q[1] = DLG_LFO, q[2] = 5, q[3] = 4, q[4] = (uint8_t)((2 + 8192) & 127), q[5] = (uint8_t)((2 + 8192) >> 7);
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, q, 6);
        q[3] = 5, q[4] = (uint8_t)((40 + 8192) & 127), q[5] = (uint8_t)((40 + 8192) >> 7);
        ed_begin(ED_DLAN); r = r && ed_dlan(ED_DLAN, q, 6);
        check(r && dlfo_field(5, DLF_DEST1) == 2 && dlfo_field(5, DLF_AMT1) == 40 && dlfo_live(5), "v13 DLAN op 4: a lane's LFO slot (DEST TUNE.., AMT): the lane's LFO acts");
        q[1] = DLG_TRK, q[2] = 2, q[3] = 0, q[4] = (uint8_t)((1 + 8192) & 127), q[5] = (uint8_t)((1 + 8192) >> 7);
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, q, 6);
        check(r && smute_trk_on(2), "v13 DLAN op 4: a track's sequencer mute");
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, BAD, sizeof BAD);
        check(!r, "v13 DLAN: a bad group: no reply");
        ed_begin(ED_DLAN); r = ed_dlan(ED_DLAN, OP1, sizeof OP1);
        check(!r, "v13 DLAN: an unknown op: no reply");
        n0 = nf;
        dlm_set_pan(1, 20);
        run_ms(ED9_WIN);
        check(count(ED_FX_PUSH, n0) == 1, "v13 DLAN: a lane's PAN moved on the device: the FX push goes out");
        dlm_none(); smute_set_trk(2, 0); dlfo_set(5, &DLFO_DEF);
        ed9_known_fx();
    }

#else
    {
        static const uint8_t M0[] = {0, 0, 3};
        ed_begin(ED_DLAN);
        check(!ed_dlan(ED_DLAN, M0, sizeof M0) && ed_n == 5u, "ED_DLAN off: cmd 95 is not handled (no reply)");
        ed_begin(1); ed_dlan_info();
        check(ed_n == 5u, "ED_DLAN off: INFO has no tag 0x58");
    }
#endif

    /* v13 FAVS (ed_fav.c, cmd 100): the device's favorites store: count, the marks of an engine's presets, mark / clear, FULL, the PRESETS list */
#if FELUCCA_FAVORITES && FELUCCA_ED_FAVS
    {
        uint8_t q[6];
        int r;
        uint32_t i, c0;
        memset(&fav, 0, sizeof fav);
        fav_list = 0;
        q[0] = 0;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 1);
        check(r && ed_out[5] == 0u && ed_out[6] == 0u && ed_out[7] == FAV_N && ed_out[8] == 0u && ed_out[9] == 0u, "v13 FAVS op 0: no favorites, FAV_N places, the list is ALL");
        q[0] = 2, q[1] = 0, q[2] = 3, q[3] = 1;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 4);
        check(r && ed_out[8] == 1u && ed_out[9] == 1u && fav_has(eng_uid(0), preset_name(0, 3)), "v13 FAVS op 2: preset 3 of engine 0 marked (the device's store: its engine UID and name)");
        q[0] = 1, q[1] = 0;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 2);
        check(r && ed_out[7] == ENGINES[0]->npresets && ((ed_out[8] >> 3) & 1u) && !(ed_out[8] & 1u), "v13 FAVS op 1: the engine's marks, 7 to a byte: bit 3 only");
        q[0] = 2, q[1] = NENGINES, q[2] = 2, q[3] = 1;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 4);
        check(r && ed_out[8] == 1u && fav_user_has(2) && ed_out[9] == 1u, "v13 FAVS op 2: a user preset slot (engine = NENGINES) marked, the factory count unchanged");
        q[3] = 1;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 4);
        check(r && ed_out[8] == 0u, "v13 FAVS op 2: marked again: as it was");
        for (i = 0, c0 = 0; i < FAV_N + 5u && c0 < 100u; i++, c0++) {
            q[0] = 2, q[1] = 0, q[2] = (uint8_t)(i % ENGINES[0]->npresets), q[3] = 1;
            ed_begin(ED_FAV);
            if (!(r = ed_fav(ED_FAV, q, 4)))
                break;
            if (ed_out[8] == 2u)
                break;
        }
        check(fav_count() <= FAV_N, "v13 FAVS: the store never holds more than FAV_N marks (FULL answers 2)");
        q[0] = 3, q[1] = 1;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 2);
        check(r && fav_list == 1u && ed_out[6] == 1u, "v13 FAVS op 3: the PRESETS list FAV");
        q[0] = 3, q[1] = 0;
        ed_begin(ED_FAV); r = ed_fav(ED_FAV, q, 2);
        q[0] = 2, q[1] = 0, q[2] = 3, q[3] = 0;
        ed_begin(ED_FAV); r = r && ed_fav(ED_FAV, q, 4);
        check(r && !fav_has(eng_uid(0), preset_name(0, 3)) && fav_list == 0u, "v13 FAVS op 2: cleared; the list back to ALL");
        q[0] = 1, q[1] = NENGINES + 1u;
        ed_begin(ED_FAV);
        check(!ed_fav(ED_FAV, q, 2), "v13 FAVS: a bad engine: no reply");
        memset(&fav, 0, sizeof fav);
    }
#else
    {
        static const uint8_t G[] = {0, 0};
        ed_begin(ED_FAV);
        check(!ed_fav(ED_FAV, G, sizeof G) && ed_n == 5u, "ED_FAVS off (or no FAVORITES): cmd 100 is not handled (no reply)");
        ed_begin(1); ed_fav_info();
        check(ed_n == 5u, "ED_FAVS off: INFO has no tag 0x5D");
    }
#endif


#if FELUCCA_ED_SONG
    /* v13 SONGC (ed_song.c, cmd 97): the song chain: parts, mode, save; edited stopped only */
    {
        static const uint8_t G[] = {0}, S1[] = {1, 1, 2, 8}, INS[] = {2, 1}, DEL[] = {3, 1}, MD[] = {4, 1}, SV[] = {5}, BADP[] = {1, 40, 0, 4}, BADS[] = {1, 0, 99, 4}, BADB[] = {1, 0, 0, 65}, OP9[] = {9};
        int r;
        uint32_t n;
        arr_defaults(&arrangement);
        song.playing = 0;
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, G, sizeof G);
        check(r && ed_out[5] == 0u && ed_out[6] == 0u && ed_out[11] == arrangement.count && ed_n == 5u + 7u + 2u * arrangement.count,
              "v13 SONGC op 0: the chain: rc, mode, playing, the scenes and steps built, the count, a (scene, bars) a part");
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, S1, sizeof S1);
        check(r && ed_out[6] == 0u && arrangement.entry[1].scene == 2 && arrangement.entry[1].bars == 8, "v13 SONGC op 1: a part set");
        n = arrangement.count;
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, INS, sizeof INS);
        check(r && arrangement.count == n + 1u && arrangement.entry[2].scene == 2 && arrangement.entry[2].bars == 8 && ed_out[11] == n + 1u, "v13 SONGC op 2: INSERT copies the part after it");
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, DEL, sizeof DEL);
        check(r && arrangement.count == n, "v13 SONGC op 3: DELETE removes it");
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, MD, sizeof MD);
        check(r && arrangement_enabled == 1u && ed_out[7] == 1u, "v13 SONGC op 4: SONG mode");
        arrangement_enabled = 0;
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, SV, sizeof SV);
        check(r && arr_saves == 1u, "v13 SONGC op 5: the chain is saved");
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, BADP, sizeof BADP);
        check(r && ed_out[6] == 4u, "v13 SONGC: a part past the count: rc 4");
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, BADS, sizeof BADS);
        ed_begin(ED_SONGC); r = r && ed_songc(ED_SONGC, BADB, sizeof BADB);
        check(r && ed_out[6] == 4u && arrangement.entry[0].bars != 65, "v13 SONGC: a scene or bars out of range: rc 4, nothing written");
        song.playing = 1;
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, S1, sizeof S1);
        check(r && ed_out[6] == 1u && ed_out[8] == 1u, "v13 SONGC: the transport plays: STOP FIRST (rc 1), the playing flag says so");
        song.playing = 0;
        ed_begin(ED_SONGC); r = ed_songc(ED_SONGC, OP9, sizeof OP9);
        check(!r, "v13 SONGC: an unknown op: no reply");
        arr_defaults(&arrangement);
    }
#else
    {
        static const uint8_t G[] = {0};
        ed_begin(ED_SONGC);
        check(!ed_songc(ED_SONGC, G, sizeof G) && ed_n == 5u, "ED_SONG off: cmd 97 is not handled (no reply)");
        ed_begin(1); ed_songc_info();
        check(ed_n == 5u, "ED_SONG off: INFO has no tag 0x5A");
    }
#endif

    /* never in the way of a reply: a request waiting, or the ring past half: nothing goes out; later it does */
    n0 = nf;
    trk[0].p[P_PAN] = 33;
    sx_ready = 1;
    run_ms(3 * ED9_WIN);
    check(nf == n0, "v9: a request waiting (sx_ready): no push");
    sx_ready = 0;
    for (i = 0; i < 3u * ED9_WIN; i++) { fm1_ms++; so_w = so_r + SXQ / 2u; ed9_sync(fm1_ms); so_r = so_w - SXQ / 2u; }
    check(nf == n0, "v9: the SysEx ring past half: no push (the reply's room stays free)");
    run_ms(ED9_WIN);
    check(entries(n0, 0, P_PAN, &v) == 1 && v == 33, "v9: room again: the change goes out, with its value now");
    for (i = n0, k = 0; i < nf; i++) k |= frn[i] > ED9_MAX;
    check(!k, "v9: no push frame longer than 96 bytes (32 USB-MIDI packets: under half the ring)");

    /* the stream: 25 Hz at most, only while something changes or sounds; the peaks of every window, none lost */
    memset(est.last, 0xFF, sizeof est.last);
    est.ms = fm1_ms - ED_STREAM_MS;
    mt.master = 1;
    n0 = nf;
    for (i = 0; i < 400; i++) { fm1_ms++; ed_stream(fm1_ms, 1); drain(); }
    check(count(ED_STREAM, n0) == 1, "STREAM: silent and stopped: the first frame, then nothing (only what changed)");
    n0 = nf;
    for (i = 0; i < 1000; i++) {                         /* 1 s of sound: the ISR's peaks, the tap every 5.8 ms half */
        fm1_ms++;
        trk[0].peak = trk[0].peak > 9000 ? trk[0].peak : (int32_t)(i % 97 == 0 ? 30000 : 8000);
        drums.peak = 12000;
        if (i % 6 == 0) { audio_halves++; host_half ^= 1u; abuf[host_half * HALF_WORDS + 3] = 20000 << OUT_SHIFT; meter_tap(); }
        ed_stream(fm1_ms, 1);
        drain();
    }
    {
        uint32_t s = count(ED_STREAM, n0), pk0 = 0, pkm = 0, j, big = 0;
        for (j = n0; j < nf; j++) {
            if (fr[j][4] != ED_STREAM) continue;
            pk0 = fr[j][10] | fr[j][11] << 7;
            pkm = fr[j][9 + 3 * NTRK] | fr[j][10 + 3 * NTRK] << 7;
            big += pk0 == 30000u >> 2;
        }
        check(s >= 24u && s <= 26u, "STREAM: sounding: 25 frames a second (40 ms apart)");
        check(big >= 9u && pkm == 20000u >> 2, "STREAM: every 30000 peak reached a frame (none falls between frames); the master's peak from the output");
    }
    n0 = nf;
    trk[0].peak = drums.peak = 0;
    for (i = 0; i < 400; i++) { fm1_ms++; if (i % 6 == 0) { audio_halves++; abuf[0] = abuf[HALF_WORDS + 3] = abuf[3] = 0; meter_tap(); } ed_stream(fm1_ms, 1); drain(); }
    check(count(ED_STREAM, n0) <= 2u && fr[nf - 1][10] == 0 && fr[nf - 1][11] == 0 && fr[nf - 1][9 + 3 * NTRK] == 0, "STREAM: silence after sound: the last peaks, one frame at 0, then quiet (no latched peak)");
    n0 = nf;
    for (i = 0; i < 200; i++) { fm1_ms++; ed_stream(fm1_ms, 0); }
    check(count(ED_STREAM, n0) == 0, "STREAM: no room: nothing sent");

    /* the tap: the TRACKS screen and the editor both see a peak; a reader's look clears only its own */
    trk[1].peak = 5000;
    audio_halves++;
    meter_tap();
    check(meter_ui_take(1) == 5000 && meter_ui_take(1) == 0 && mt.ed[1] == 5000 && trk[1].peak == 0,
          "meters: the tap takes the ISR's peak once per half for both readers; the TRACKS screen's look leaves the editor's");

    /* bandwidth and cost: a busy song (every track's steps playing, a knob swept on each track) for 2 s */
    {
        uint32_t b0 = bytes_total, f0 = nf, t;
        e9.scans = e9.t_sum = e9.t_max = 0;
        for (t = 0; t < 2000; t++) {
            uint32_t c;
            fm1_ms++;
            for (c = 0; c < NTRK; c++) trk[c].p[P_REV] = (int16_t)((t / 3 + c) & 127);   /* 4 sweeps, 1 change / 3 ms each */
            if (t % 125 == 0) trk[1].step[t / 125 % 64].vel ^= 1;   /* live recording: 8 steps a second */
            ed9_sync(fm1_ms);
            drain();
        }
        printf("busy song, 4 knob sweeps + 8 step edits a second, 2 s: %u frames, %u bytes (%u B/s); %u scans\n",
               nf - f0, bytes_total - b0, (bytes_total - b0) / 2u, e9.scans);
        check((bytes_total - b0) / 2u < 3000u, "v9 bandwidth: a busy song with sweeps on every track stays under 3 KB/s (USB-MIDI full speed: ~100 KB/s)");
    }
    /* the INFO tags of the commands built (the builder's ED_* items): sent when built, nothing when not */
#if FELUCCA_ED_FXP
    ed_begin(1); ed_fxp_info(); check(ed_n > 5u && ed_out[5] == 0x57u, "ED_FXP on: INFO carries tag 0x57");
#endif
#if FELUCCA_ED_DLAN
    ed_begin(1); ed_dlan_info(); check(ed_n > 5u && ed_out[5] == 0x58u, "ED_DLAN on: INFO carries tag 0x58");
#endif
#if FELUCCA_ED_SONG
    ed_begin(1); ed_songc_info(); check(ed_n > 5u && ed_out[5] == 0x5Au, "ED_SONG on: INFO carries tag 0x5A");
#endif
#if FELUCCA_FAVORITES && FELUCCA_ED_FAVS
    ed_begin(1); ed_fav_info(); check(ed_n > 5u && ed_out[5] == 0x5Du, "ED_FAVS on: INFO carries tag 0x5D");
#endif
    printf("%s\n", fails ? "EDITOR SYNC TESTS FAILED" : "editor sync: all ok");
    return fails != 0;
}
