/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v9: only what changed, for every track (included by editor.c; tests/editor_sync_test.c;
 * web/EDITOR_PROTOCOL.md "v9"). Asked with WATCH bit 2; while on it replaces the v2..v4 pushes CHANGED (23),
 * STEP_CHANGED (26) and TRACK_CHANGED (32). RELOAD (24) stays the "re-read everything" signal.
 *
 *   59 PARAMS (push)  n x (where, id, v14): where 0..NTRK-1 a track's P_*, 127 a global G_*
 *   60 STEPS  (push)  track, first index, count, count x step (a synth track: the 11 TRACK_STEP bytes from n; the drum
 *                     track: the 13 DRUM_STEP bytes)
 *   61 LANE   (push)  as the DRUM_LANE (37) reply: lane (+ 0x40: with its 3 send bytes), pack7 lane
 *   62 TRACKS (push)  as the TRACK (27) reply: selected, NTRK, per track engine, preset, level, mute, armed; solo mask
 *   63 SONG   (push)  the sections stored (3 x 7 bit, bit n = section A + n), the song's parts, loop, then a change
 *                     count of the snapshot list (7 bit: re-read SN_LIST when it moved)
 *
 * How: shadows of what the editor knows (every track's P_*, the globals, a signature per step and per drum lane, the
 * engine / preset / mix flags of every track, the song and snapshot signatures). Every ED9_WIN ms one scan compares
 * them with the device and sends what differs with its value now: a key that moves many times inside a window goes
 * out once, with its last value (a knob sweep: one entry per window). A window sends at most ED9_FRAMES frames, each
 * only into a SysEx ring at most half full and while no request waits (sx_ready), so a reply never waits behind a
 * push; what did not fit stays different from its shadow and goes in a later window. The editor's own writes update
 * the shadows (ed9_known_*): nothing echoes back. Main loop only. */
enum { ED_PARAMS = 59, ED_STEPS, ED_LANE_PUSH, ED_TRACKS_PUSH, ED_SONG_PUSH, ED_SYNC_STATS };
#define ED9_WIN 20u                                     /* ms: the coalescing window */
#define ED9_SLOW 250u                                   /* ms: the song / snapshot signatures (proj_ok sums) */
#define ED9_FRAMES 4u                                   /* frames a window at most */
#define ED9_MAX 96u                                     /* bytes a push frame at most (32 USB-MIDI packets) */
#define ED9_GLOBAL 127u
#ifndef ED9_SONG_SIG                                    /* (tests replace them) */
#define ED9_SONG_SIG() ed9_song_sig()
#endif
#ifndef ED9_SNAP_SIG
#if FELUCCA_SNAPSHOTS
#define ED9_SNAP_SIG() sn_ui_sig()
#else
#define ED9_SNAP_SIG() 0u
#endif
#endif
static struct {
    int16_t p[NTRK][P_COUNT];                           /* what the editor knows */
    int16_t g[G_COUNT];
    uint32_t st[NTRK][NSTEP];                           /* step signatures */
#if DL_ANY
    uint32_t lane[DRUM_LANES];
#endif
    uint32_t trk_sig, song_sig, snap_sig;
    uint32_t win_ms, slow_ms;
    uint16_t ppos;                                      /* round robin: nothing starves */
    uint8_t spos, lpos, snap_n;
    /* measured (SYNC_STATS 64): scans, frames and bytes pushed, the longest scan and all of them (ticks) */
    uint32_t scans, frames, bytes, t_max, t_sum;
} e9 __attribute__((section(".bss.ed9")));      /* (its own section: kept out of the merged globals the RAM code addresses) */

static uint32_t ed9_sig(const uint8_t *b, uint32_t n)  /* FNV-1a */
{
    uint32_t h = 0x811C9DC5u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t ed9_step_sig(uint32_t t, uint32_t i) { return ed9_sig((const uint8_t *)&trk[t].step[i], sizeof(step_t)); }
#if DL_ANY
static uint32_t ed9_lane_sig(uint32_t l)
{
    uint8_t b[12];
    ed_lane_get(l, b);
#if FELUCCA_DRUM_SENDS
    return ed9_sig(b, sizeof b) ^ (uint32_t)dsend[l] * 2654435761u;
#else
    return ed9_sig(b, sizeof b);
#endif
}
#endif
static uint32_t ed9_trk_sig(void)                       /* what TRACK (27) answers, beyond the parameters */
{
    uint32_t c, h = 0x811C9DC5u ^ song.sel;
    for (c = 0; c < NTRK; c++)
        h = (h ^ (ed_eng(&trk[c]) | (uint32_t)trk[c].preset << 8)) * 16777619u;
    return (h ^ (song.rec & 15u) ^ (uint32_t)(song.solo & 15u) << 4) * 16777619u;
}
static uint32_t ed9_song_sig(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < FELUCCA_SECTIONS && i < 21u; i++)
        m |= (uint32_t)(project_used(i) != 0) << i;
    return m ^ ed9_sig((const uint8_t *)&arrangement, 4u + (uint32_t)sizeof(arr_entry_t) * (arrangement.count % (ARR_STEPS + 1u))) * 0x9E3779B1u;
}

/* everything as it is now is known (WATCH from off) */
static void ed9_shadow_all(void)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        memcpy(e9.p[t], trk[t].p, sizeof e9.p[t]);
        for (i = 0; i < NSTEP; i++)
            e9.st[t][i] = ed9_step_sig(t, i);
    }
    memcpy(e9.g, song.g, sizeof e9.g);
#if DL_ANY
    for (i = 0; i < DRUM_LANES; i++)
        e9.lane[i] = ed9_lane_sig(i);
#endif
    e9.trk_sig = ed9_trk_sig();
    e9.song_sig = ED9_SONG_SIG();
    e9.snap_sig = ED9_SNAP_SIG();
}
/* the editor's own writes (and what it reads): no push for them */
static void ed9_known_p(uint32_t t, uint32_t id) { if (t < NTRK && id < P_COUNT) e9.p[t][id] = trk[t].p[id]; }
static void ed9_known_g(uint32_t id) { if (id < G_COUNT) e9.g[id] = song.g[id]; }
static void ed9_known_track(uint32_t t) { if (t < NTRK) memcpy(e9.p[t], trk[t].p, sizeof e9.p[t]); }
static void ed9_known_step(uint32_t t, uint32_t i) { if (t < NTRK && i < NSTEP) e9.st[t][i] = ed9_step_sig(t, i); }
static void ed9_known_lanes(void)
{
#if DL_ANY
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        e9.lane[l] = ed9_lane_sig(l);
#endif
}

/* room for a push of n bytes: no request waiting, the ring at most half full with it */
static int ed9_room(uint32_t n) { return !sx_ready && so_w - so_r + (n + 2u) / 3u <= SXQ / 2u; }
/* the longest push that fits now (bytes, F0..F7), at most ED9_MAX; 0: none (a request waits, or the ring is half full) */
static uint32_t ed9_space(void)
{
    uint32_t used = so_w - so_r, n;
    if (sx_ready || used >= SXQ / 2u)
        return 0;
    n = (SXQ / 2u - used) * 3u;
    return n > ED9_MAX ? ED9_MAX : n;
}
static void ed9_send(void)
{
    e9.frames++;
    e9.bytes += ed_n + 1u;
    ed_send();
}

/* PARAMS: the parameters that differ, round robin over every track's and the globals; 1 = sent */
static int ed9_params(void)
{
    uint32_t i, n = 0, total = NTRK * P_COUNT + G_COUNT, k = e9.ppos, room = ed9_space();
    if (room < 10u)                                      /* (one entry: 5 + 4 + F7) */
        return 0;
    ed_begin(ED_PARAMS);
    for (i = 0; i < total && ed_n + 5u <= room; i++, k = k + 1u == total ? 0u : k + 1u) {
        int16_t v, *sh;
        uint32_t where, id;
        if (k < NTRK * P_COUNT) {
            where = k / P_COUNT, id = k % P_COUNT;
            v = trk[where].p[id], sh = &e9.p[where][id];
        } else {
            where = ED9_GLOBAL, id = k - NTRK * P_COUNT;
            v = song.g[id], sh = &e9.g[id];
        }
        if (v == *sh)
            continue;
        *sh = v;
        ed_b(where);
        ed_b(id);
        ed_v(v);
        n++;
    }
    e9.ppos = (uint16_t)k;
    if (!n)
        return 0;
    ed9_send();
    return 1;
}
/* STEPS: the first run of changed steps from the next track on (one track a frame); 1 = sent */
static int ed9_steps(void)
{
    uint32_t j, t, i, first, cnt, per, room;
    for (j = 0; j < NTRK; j++) {
        t = (e9.spos + j) % NTRK;
        per = t == TRK_DRUM ? 13u : 11u;
        for (i = 0; i < NSTEP && e9.st[t][i] == ed9_step_sig(t, i); i++)
            ;
        if (i == NSTEP)
            continue;
        if ((room = ed9_space()) < 9u + per)                /* (one step at least) */
            return 0;
        first = i;
        ed_begin(ED_STEPS);
        ed_b(t);
        ed_b(first);
        ed_b(0);                                         /* (the count, set below) */
        for (cnt = 0; i < NSTEP && ed_n + per + 1u <= room; i++) {
            uint32_t h = ed9_step_sig(t, i);
            if (h == e9.st[t][i])
                break;                                   /* a run of changed steps */
            e9.st[t][i] = h;
            if (t == TRK_DRUM)
                ed_dstep_out(&trk[t].dstep[i]);
            else
                ed_step_out(&trk[t], i);
            cnt++;
        }
        ed_out[7] = (uint8_t)cnt;
        e9.spos = (uint8_t)((t + 1u) % NTRK);
        ed9_send();
        return 1;
    }
    return 0;
}
#if DL_ANY
static int ed9_lane(void)                               /* LANE: one changed drum lane; 1 = sent */
{
    uint32_t j;
    for (j = 0; j < DRUM_LANES; j++) {
        uint32_t l = (e9.lpos + j) % DRUM_LANES, h = ed9_lane_sig(l);
        uint8_t b[15];
        if (h == e9.lane[l])
            continue;
        if (!ed9_room(32u))
            return 0;
        e9.lane[l] = h;
        ed_lane_get(l, b);
        ed_begin(ED_LANE_PUSH);
#if FELUCCA_DRUM_SENDS
        ed_snd_get(&dsend[l], b + 12, 1);
        ed_b(ED_V2 | l);
        ed_pack7(b, 15u);
#else
        ed_b(l);
        ed_pack7(b, 12u);
#endif
        e9.lpos = (uint8_t)((l + 1u) % DRUM_LANES);
        ed9_send();
        return 1;
    }
    return 0;
}
#else
#define ed9_lane() 0
#endif
static int ed9_tracks(void)                             /* TRACKS: an engine, preset, selection, arm or solo moved */
{
    uint32_t h = ed9_trk_sig(), c;
    if (h == e9.trk_sig || !ed9_room(48u))
        return 0;
    e9.trk_sig = h;
    ed_begin(ED_TRACKS_PUSH);
    ed_b(song.sel);
    ed_b(NTRK);
    for (c = 0; c < NTRK; c++) {
        ed_b(ed_eng(&trk[c]));
        ed_b(trk[c].preset);
        ed_v(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL]);
        ed_b(trk[c].p[P_MUTE] != 0);
        ed_b((song.rec >> c) & 1u);
    }
    ed_b(song.solo & 15u);
    ed9_send();
    return 1;
}
static int ed9_song(uint32_t now)                       /* SONG: the sections stored, the chain, the snapshots */
{
    uint32_t s, n;
    if (now - e9.slow_ms < ED9_SLOW)
        return 0;
    s = ED9_SONG_SIG(), n = ED9_SNAP_SIG();
    if (s == e9.song_sig && n == e9.snap_sig) {
        e9.slow_ms = now;
        return 0;
    }
    if (!ed9_room(24u))
        return 0;
    e9.slow_ms = now;
    if (n != e9.snap_sig)
        e9.snap_n++;
    e9.song_sig = s, e9.snap_sig = n;
    ed_begin(ED_SONG_PUSH);
    {
        uint32_t i, m = 0;
        for (i = 0; i < FELUCCA_SECTIONS && i < 21u; i++)
            m |= (uint32_t)(project_used(i) != 0) << i;
        ed_b(m);
        ed_b(m >> 7);
        ed_b(m >> 14);
    }
    ed_b(arrangement.count);
    ed_b(arrangement.loop);
    ed_b(e9.snap_n & 127u);
    ed9_send();
    return 1;
}

/* main loop, while WATCH has bit 2: one scan a window */
static void ed9_sync(uint32_t now)
{
    uint32_t n = 0, t0;
    if (now - e9.win_ms < ED9_WIN)
        return;
    e9.win_ms = now;
    t0 = fm1_ticks();
    n += (uint32_t)ed9_tracks();
    while (n < ED9_FRAMES && ed9_params())
        n++;
    while (n < ED9_FRAMES && ed9_steps())
        n++;
    while (n < ED9_FRAMES && ed9_lane())
        n++;
    if (n < ED9_FRAMES)
        ed9_song(now);
    t0 = fm1_ticks() - t0;
    e9.scans++;
    e9.t_sum += t0;
    if (t0 > e9.t_max)
        e9.t_max = t0;
}

/* 64 SYNC_STATS (a measurement aid): scans, frames, bytes pushed (5 x 7 bit each), the longest scan and the sum of
 * all (us, 5 x 7 bit each), the stream's frames (5 x 7 bit), the master meter's halves scanned and missed (5 x 7 bit
 * each); 1 (any byte) resets them */
static void ed9_b35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        ed_b(v >> (7u * i));
}
