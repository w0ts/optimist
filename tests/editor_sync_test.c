/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v9 on the host (firmware/src/ed_sync9.c, ed_status.c's STREAM, meters.c): the device side of "only what
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
#include "../firmware/src/meters.c"
/* the reply builder as editor.c has it; ed_send records the frames */
static uint8_t ed_out[600];
static uint32_t ed_n;
static void ed_begin(uint32_t cmd) { ed_out[0] = 0xF0; ed_out[1] = 0x7D; ed_out[2] = 0x46; ed_out[3] = 0x4C; ed_out[4] = (uint8_t)cmd; ed_n = 5; }
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
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
static int project_used(uint32_t s) { return s == 1u; }
#if FELUCCA_SL24_SAFE
static uint32_t project_state(uint32_t s) { return (uint32_t)project_used(s); }   /* (sl24_guard.c: no slot of 2.4's here) */
#endif
#include "../firmware/src/ed_steps.c"
#include "../firmware/src/ed_status.c"
#include "../firmware/src/ed_sync9.c"

static int fails;
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
    printf("%s\n", fails ? "EDITOR SYNC TESTS FAILED" : "editor sync: all ok");
    return fails != 0;
}
