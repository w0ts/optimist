/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/editor_sync_test.c: editor protocol v14 (web/EDITOR_PROTOCOL.md "v14"): 102 UNDO, 103 NOTE, 104 SOLO, 105 STEPLANE
 * (firmware/src/io/editor/ed_undo.c, ed_note.c, ed_solo.c, ed_steplane.c; seq/ednote.c). SLOOP's-UI path of STEPLANE (the Optimist UI's list and its golden
 * are in tests/ui_optimist_edv14.h). Included by editor_sync_test.c after ed_sync9.c, its helpers (ed_begin, ed_out, ed_n, check) in scope. */
static void check(int c, const char *what);            /* (editor_sync_test.c) */
static uint8_t sync_reload;                                 /* (model.c's: UNDO sets it, as the panel's undo does) */
static struct { uint32_t last_ms; uint32_t st[NSTEP]; } ed_w;                 /* (editor.c's; the parts these files read) */
static uint32_t ed_step_sig(const track_t *t, uint32_t i) { (void)t; return i; }
static void ed_known(uint32_t k, uint32_t id) { ed9_known_p(k, id); }
#include "../firmware/src/io/editor/ed_undo.c"
#include "../firmware/src/io/editor/ed_solo.c"
#include "../firmware/src/io/editor/ed_note.c"
#include "../firmware/src/io/editor/ed_steplane.c"

static uint32_t v14_call(int (*fn)(uint32_t, const uint8_t *, uint32_t), uint32_t cmd, const uint8_t *q, uint32_t n)
{
    ed_begin(cmd);
    return (uint32_t)fn(cmd, q, n);
}
static uint32_t v14_gates(void)                          /* voices sounding (gate on) on the synth tracks */
{
    uint32_t k, i, n = 0;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].gate;
    return n;
}
static void v14_blocks(uint32_t n)                       /* audio blocks: the events (the ring, the voices' gates), then the render */
{
    int32_t out[CTL * 2];
    while (n--) {
        events_block(CTL);
        mix_block(out, CTL);
        mo_r = mo_w;
    }
}
static void v14_stable(void)                             /* the history, the sounds and the editor's tables as a test starts */
{
    uint32_t k;
    undo_clear();
    for (k = 0; k < NTRK; k++)
        trk_all_off(&trk[k]);
#if EDN_ON
    memset(&ed_hn, 0, sizeof ed_hn);
    edn_nh = 0;
    edn_r = edn_w;
#endif
}
#if FELUCCA_ED_UNDO
static void v14_undo(void)
{
    static const uint8_t GET[] = {0}, UNDO[] = {1}, REDO[] = {2}, BAD[] = {3};
    step_t was = trk[0].step[5];
    uint32_t r;
    v14_stable();
    fm1_ms += 2000;
    ed_undo_edit(&trk[0]);
    trk[0].step[5].time = ST_NOTE, trk[0].step[5].n = 1, trk[0].step[5].note[0] = 64;
    r = v14_call(ed_undo, ED_UNDO, GET, 1);
    check(r && ed_out[5] == 0 && ed_out[6] == 0 && ed_out[12] == 1 && ed_out[13] == 0, "UNDO 0: the editor's own step write is a level to undo (can_undo 1, can_redo 0)");
    r = v14_call(ed_undo, ED_UNDO, UNDO, 1);
    check(r && ed_out[5] == 1 && ed_out[6] == 0 && trk[0].step[5].n == was.n && trk[0].step[5].time == was.time, "UNDO 1: the step is as it was (the device's own undo_apply)");
    check(ed_out[11] == 0 && ed_out[12] == 0 && ed_out[13] == 1, "UNDO 1: the last track, can_undo 0, can_redo 1");
    r = v14_call(ed_undo, ED_UNDO, UNDO, 1);
    check(r && ed_out[6] == 1 && trk[0].step[5].n == was.n, "UNDO 1 again: nothing to undo (rc 1), nothing moved");
    r = v14_call(ed_undo, ED_UNDO, REDO, 1);
    check(r && ed_out[5] == 2 && ed_out[6] == 0 && trk[0].step[5].n == 1 && trk[0].step[5].note[0] == 64, "UNDO 2: redo puts the step back");
    r = v14_call(ed_undo, ED_UNDO, REDO, 1);
    check(r && ed_out[6] == 1, "UNDO 2 again: nothing to redo (rc 1)");
    check(!v14_call(ed_undo, ED_UNDO, BAD, 1) && ed_n == 5u && !v14_call(ed_undo, ED_UNDO, GET, 0), "UNDO: a bad op, an empty request: no reply");
    /* writes to one track within 800 ms are one level; later ones another */
    v14_stable();
    fm1_ms += 2000;
    ed_undo_edit(&trk[1]);
    trk[1].step[6].vel = 11;
    fm1_ms += 100;
    ed_undo_edit(&trk[1]);
    trk[1].step[7].vel = 12;
    fm1_ms += 900;
    ed_undo_edit(&trk[1]);
    trk[1].step[8].vel = 13;
    (void)v14_call(ed_undo, ED_UNDO, UNDO, 1);
    check(trk[1].step[8].vel != 13 && trk[1].step[6].vel == 11 && trk[1].step[7].vel == 12, "UNDO: the drag is one level: the last write alone is undone first");
    (void)v14_call(ed_undo, ED_UNDO, UNDO, 1);
    check(trk[1].step[6].vel != 11 && trk[1].step[7].vel != 12, "UNDO: the two writes inside 800 ms are undone together");
    trk[1].step[6].vel = trk[1].step[7].vel = trk[1].step[8].vel = trk[0].step[5].vel = 0;
    memset(&trk[0].step[5], 0, sizeof trk[0].step[5]);
    ed_begin(1); ed_undo_info();
    check(ed_n == 9u && ed_out[5] == 0x5Fu && ed_out[7] == 1u && ed_out[8] == (FELUCCA_UNDO_HISTORY ? 1u : 0u), "ED_UNDO on: INFO carries tag 0x5F (version 1, the history flag)");
    v14_stable();
}
#endif
#if EDN_ON
static void v14_note(void)
{
    uint8_t on[5] = {0, 0, 60, 100, 0}, off[4] = {1, 0, 60, 0};
    uint32_t r, i;
    for (i = 0; i < NPART; i++) {                        /* (sounding parts: a tempo, a preset, polyphony; the earlier tests left bare tracks) */
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_MUTE] = 0;
    }
    song.g[G_BPM] = 120;
    song.solo = 0;
    v14_stable();
    usb.config = 1;
    fm1_ms += 10000;
    ed_w.last_ms = fm1_ms;
    ed_hn_resets = usb.resets;
    r = v14_call(ed_note, ED_NOTE, on, 4);
    check(r && ed_out[5] == 0 && ed_out[6] == 0 && ed_out[7] == 0 && ed_out[8] == 60 && ed_out[9] == 1, "NOTE ON: queued, rc 0, the track and note echoed, one note held");
    check(v14_gates() == 0, "NOTE ON: nothing sounds until the audio interrupt takes it from the ring");
    v14_blocks(2);
    check(v14_gates() >= 1, "NOTE ON: the audio interrupt starts the note (the keys' input_on path)");
    r = v14_call(ed_note, ED_NOTE, off, 3);
    check(r && ed_out[5] == 1 && ed_out[6] == 0 && ed_out[9] == 0, "NOTE OFF: rc 0, nothing held");
    v14_blocks(2);
    check(v14_gates() == 0, "NOTE OFF: the note's gate is off");
    /* no hanging note: the host quiet for 3 s, a USB reset, a dur */
    (void)v14_call(ed_note, ED_NOTE, on, 4);
    v14_blocks(2);
    fm1_ms += 2500;
    ed_note_tick();
    v14_blocks(2);
    check(v14_gates() >= 1, "NOTE: 2.5 s of silence from the host: the note still holds");
    fm1_ms += 1000;
    ed_note_tick();
    v14_blocks(2);
    check(v14_gates() == 0 && ed_hn_n() == 0, "NOTE: 3.5 s without a request: the device ends the note by itself");
    ed_w.last_ms = fm1_ms;
    (void)v14_call(ed_note, ED_NOTE, on, 4);
    v14_blocks(2);
    usb.resets++;
    ed_note_tick();
    v14_blocks(2);
    check(v14_gates() == 0 && ed_hn_n() == 0, "NOTE: USB reset (cable out): the note ends");
    ed_w.last_ms = fm1_ms;
    on[4] = 2;
    (void)v14_call(ed_note, ED_NOTE, on, 5);
    v14_blocks(2);
    fm1_ms += 100;
    ed_w.last_ms = fm1_ms;
    ed_note_tick();
    v14_blocks(2);
    check(v14_gates() >= 1, "NOTE dur 2: 100 ms in, still sounding");
    fm1_ms += 150;
    ed_w.last_ms = fm1_ms;
    ed_note_tick();
    v14_blocks(2);
    check(v14_gates() == 0, "NOTE dur 2: ends by itself after 200 ms");
    on[4] = 0;
    /* a note repeated replaces its press; eight held at most, the oldest goes */
    (void)v14_call(ed_note, ED_NOTE, on, 4);
    (void)v14_call(ed_note, ED_NOTE, on, 4);
    v14_blocks(2);
    check(ed_hn_n() == 1 && v14_gates() >= 1, "NOTE: ON of a held note retriggers it, one note held");
    for (i = 0; i < 10u; i++) {
        on[2] = (uint8_t)(48 + i);
        (void)v14_call(ed_note, ED_NOTE, on, 4);
        v14_blocks(1);
    }
    check(ed_hn_n() == EDN_HELD && edn_nh <= EDN_HELD, "NOTE: ten notes: the editor holds eight (the oldest ended)");
    v14_blocks(2);
    {
        static const uint8_t all[] = {2, 127};
        r = v14_call(ed_note, ED_NOTE, all, 2);
        v14_blocks(2);
        check(r && ed_out[5] == 2 && ed_out[9] == 0 && v14_gates() == 0, "NOTE ALL (127): every note the editor holds ends");
    }
    /* the ring: full, rc 1, nothing queued */
    v14_stable();
    for (i = 0; i < EDN_Q; i++)
        (void)ednote_push(EDN_OP_ON, 0, 70, 90);
    on[2] = 71;
    r = v14_call(ed_note, ED_NOTE, on, 4);
    check(r && ed_out[6] == 1 && ed_hn_n() == 0, "NOTE ON with the ring full: rc 1, nothing held, nothing happened");
    v14_blocks(2);
    v14_stable();
    /* the drum track: the note is the lane; a one-shot; the ACCENT lane only with the store */
    {
        uint8_t d[4] = {0, 3, 5, 100};
        pen_lane = 0;
        r = v14_call(ed_note, ED_NOTE, d, 4);
        v14_blocks(2);
        check(r && ed_out[6] == 0 && ed_out[9] == 0 && pen_lane == 5, "NOTE ON on the drum track: the note is the lane (5), a one-shot, nothing held");
        d[2] = 17;
        check(!v14_call(ed_note, ED_NOTE, d, 4), "NOTE ON on the drum track: a lane past 16: no reply");
    }
    {
        uint8_t b[4] = {0, 0, 60, 0};
        check(!v14_call(ed_note, ED_NOTE, b, 4), "NOTE ON with vel 0: no reply");
        b[3] = 100, b[1] = 9;
        check(!v14_call(ed_note, ED_NOTE, b, 4), "NOTE ON on track 9: no reply");
        b[1] = 0, b[2] = 128;
        check(!v14_call(ed_note, ED_NOTE, b, 3) && !v14_call(ed_note, ED_NOTE, b, 1), "NOTE: a short request: no reply");
    }
    ed_begin(1); ed_note_info();
    check(ed_n == 9u && ed_out[5] == 0x60u && ed_out[7] == 1u && ed_out[8] == 3u, "ED_NOTE on: INFO carries tag 0x60 (version 1, 3 s of quiet)");
    v14_stable();
}
#endif
#if FELUCCA_ED_SOLO
static void v14_solo(void)
{
    uint8_t q[3] = {1, 1, 1};
    uint32_t r;
    song.solo = 0;
    trk[0].p[P_MUTE] = 0;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[5] == 1 && ed_out[6] == 0 && ed_out[7] == 2 && ed_out[9] == 13 && song.solo == 2 && trk_silent(&trk[0]) && !trk_silent(&trk[1]),
          "SOLO 1: track 2 soloed: song.solo bit 1, the other three silent (the reply says so)");
    q[2] = 2;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    check(song.solo == 0 && !trk_silent(&trk[0]), "SOLO 1 toggle (2): the solo is off again, as the panel key does");
    q[0] = 2, q[1] = 0, q[2] = 1;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[8] == 1 && trk[0].p[P_MUTE] == 1 && trk_silent(&trk[0]), "SOLO 2: track 1 muted (P_MUTE, the mute mask in the reply)");
    q[2] = 0;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    check(trk[0].p[P_MUTE] == 0, "SOLO 2 off: unmuted");
    q[0] = 3, q[1] = 4, q[2] = 1;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[10] == 16 && dlm_soloed(4) && dlm_lane_heard(4) && !dlm_lane_heard(3), "SOLO 3: lane 4 soloed (drum_mix.c): only it is heard, the reply's lane mask has bit 4");
    q[0] = 4, q[1] = 9, q[2] = 1;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[14] == 4 && dlm_muted(9), "SOLO 4: lane 9 muted (the mask's second group, bit 2)");
    q[0] = 3, q[1] = 4, q[2] = 0;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    q[0] = 4, q[1] = 9;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    check(!dlm_soloed(4) && !dlm_muted(9) && dlm_lane_heard(3), "SOLO 3 / 4 off: every lane heard again");
    r = v14_call(ed_solo, ED_SOLO, (const uint8_t[]){0}, 1);
    check(r && ed_out[5] == 0 && ed_out[7] == 0 && ed_out[8] == 0 && ed_out[9] == 0, "SOLO 0: the state, nothing set");
    q[0] = 1, q[1] = 4, q[2] = 1;
    check(!v14_call(ed_solo, ED_SOLO, q, 3), "SOLO 1 on track 4: no reply");
    q[0] = 3, q[1] = 16;
    check(!v14_call(ed_solo, ED_SOLO, q, 3), "SOLO 3 on lane 16: no reply");
    q[0] = 1, q[1] = 0, q[2] = 3;
    check(!v14_call(ed_solo, ED_SOLO, q, 3) && !v14_call(ed_solo, ED_SOLO, q, 2) && !v14_call(ed_solo, ED_SOLO, (const uint8_t[]){5}, 1), "SOLO: on 3, a short request, an unknown op: no reply");
    ed_begin(1); ed_solo_info();
    check(ed_n == 9u && ed_out[5] == 0x61u, "ED_SOLO on: INFO carries tag 0x61");
    song.solo = 0;
}
#endif
#if STL_ON
static uint32_t stl_u(const uint8_t *a, uint32_t p) { return (uint32_t)(a[p] | a[p + 1] << 7); }
static uint32_t stl_call(uint32_t op, uint32_t track, uint32_t lane, uint32_t step, uint32_t x, int32_t v)
{
    uint8_t q[8];
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    q[0] = (uint8_t)op, q[1] = (uint8_t)track, q[2] = (uint8_t)lane, q[3] = (uint8_t)step, q[4] = (uint8_t)x, q[5] = (uint8_t)(u & 127u), q[6] = (uint8_t)(u >> 7);
    return v14_call(ed_steplane, ED_STEPLANE, q, op == 0u ? 3u : op == 1u ? 4u : op == 2u ? 4u : op == 5u || op == 6u ? 5u : 7u);
}
/* a state reply's fields: ed_out[5] op, 6 rc, 7 track, 8 lane, 9 step, 10 flags, 11 level, 12 ratchet, 13 chance, 14 cond, 15 nudge + 64, 16 fill, 17 n, then n x (i, kind, v14) */
#define OKC(c) do { int c_ = (c); if (!c_) printf("  (STEPLANE check failed at editor_v14.h:%d)\n", __LINE__); ok &= (uint32_t)c_; } while (0)
static void v14_steplane(void)
{
    uint32_t n, np, i, ok, r, seen[P_COUNT];
    track_t *t = &trk[0];
    v14_stable();
    memset(t->step, 0, sizeof t->step);
    memset(seen, 0, sizeof seen);
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = 16;
    memset(AL(t), 0, sizeof *AL(t));
    r = stl_call(0, 0, 127, 0, 0, 0);
    n = ed_out[8], np = ed_out[9];
    check(r && ed_out[5] == 0 && n > 0 && np > 0 && n <= 127u, "STEPLANE LIST (synth track): the device's lockable values, at least one page");
    ok = 1;
    for (i = 0; i < n; i++) {
        uint32_t id;
        if (!stl_call(1, 0, 127, i, 0, 0)) {
            ok = 0;
            continue;
        }
        id = ed_out[10];
        ok &= ed_out[8] == i && ed_out[9] == 0 && id < P_COUNT && lock_ok(t, id) && !seen[id] && ed_out[11] < np;
        seen[id] = 1;
    }
    check(ok, "STEPLANE DESC: every value is a track parameter lock_ok takes, once, on a page below np");
    check(!seen[P_MUTE] && !seen[P_SLEN] && !seen[P_SDIV] && !seen[P_VOICES], "STEPLANE LIST: the values the device refuses (MUTE, LEN, DIV, VOICES) are not offered");
    r = stl_call(1, 0, 127, n, 0, 0);
    check(r && ed_n == 9u, "STEPLANE DESC past the list: the header only");
    /* locks on a synth step */
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 60;
    stl_call(1, 0, 127, 0, 0, 0);
    {
        uint32_t id0 = ed_out[10];
        const param_desc_t *d0 = lock_desc(t, id0);
        int32_t v;
        r = stl_call(4, 0, 127, 5, 0, d0->max);
        check(r && ed_out[5] == 4 && ed_out[6] == 0 && ed_out[17] == 1 && ed_out[18] == 0 && ed_out[19] == 1 && lock_get(t, 5, id0, &v) && v == d0->max, "STEPLANE LOCK: the lock is the step's event; the state lists it (i 0, kind 1, the value)");
        r = stl_call(4, 0, 127, 5, 0, 9000);
        check(r && lock_get(t, 5, id0, &v) && v == d0->max, "STEPLANE LOCK: a value past the range is clamped");
        r = stl_call(6, 0, 127, 5, 0, 0);
#if FELUCCA_MOTION
        check(r && ed_out[6] == 0 && ed_out[17] == 1 && ed_out[19] == 2 && hold_get(t, 5, id0, &v) && !lock_get(t, 5, id0, &v), "STEPLANE KIND: the lock becomes a hold event (the device's YES)");
        r = stl_call(4, 0, 127, 5, 0, d0->min);
        check(r && hold_get(t, 5, id0, &v) && v == d0->min && !lock_get(t, 5, id0, &v), "STEPLANE LOCK on a hold event: its value moves, the kind stays");
#else
        check(r && ed_out[6] == 4, "STEPLANE KIND without motion: nothing to do (rc 4)");
#endif
        r = stl_call(5, 0, 127, 5, 0, 0);
        check(r && ed_out[6] == 0 && ed_out[17] == 0 && !lock_get(t, 5, id0, &v) && !hold_get(t, 5, id0, &v), "STEPLANE UNLOCK: both kinds of the value go");
        r = stl_call(6, 0, 127, 5, 0, 0);
        check(r && ed_out[6] == 4, "STEPLANE KIND with no event: rc 4");
        r = stl_call(4, 0, 127, 5, 99, 0);
        check(r && ed_out[6] == 1, "STEPLANE LOCK of a value past the list: rc 1");
        stl_call(4, 0, 127, 5, 0, d0->max);
        (void)v14_call(ed_undo, ED_UNDO, (const uint8_t[]){1}, 1);
        check(!lock_get(t, 5, id0, &v), "STEPLANE: the lock written is an undo level (UNDO removes it)");
    }
    /* the extras, level and ratchet */
    ok = 1;
#if FELUCCA_CHANCE
    r = stl_call(3, 0, 127, 5, 2, 50);
    OKC(r && ed_out[6] == 0 && ed_out[13] == 50 && step_chance_ev(t, 5) == 50);
    r = stl_call(3, 0, 127, 5, 2, 101);
    OKC(r && ed_out[6] == 1);
    r = stl_call(3, 0, 127, 9, 2, 50);
    OKC(r && ed_out[6] == 3);                               /* (an empty step has no chance) */
#endif
#if FELUCCA_COND
    r = stl_call(3, 0, 127, 5, 3, 4);
    OKC(r && ed_out[6] == 0 && ed_out[14] == 4 && step_cond(t, 5) == 4);
    r = stl_call(3, 0, 127, 5, 3, 10);
    OKC(r && ed_out[6] == 1);
#endif
#if FELUCCA_MICRO
    r = stl_call(3, 0, 127, 5, 4, -7);
    OKC(r && ed_out[6] == 0 && ed_out[15] == 64 - 7 && step_micro(t, 5) == -7);
    r = stl_call(3, 0, 127, 5, 4, 32);
    OKC(r && ed_out[6] == 1);
#endif
#if FELUCCA_FILLS
    r = stl_call(3, 0, 127, 5, 5, 1);
    OKC(r && ed_out[6] == 0 && ed_out[16] == 1 && step_fill(t, 5) == 1);
#endif
    r = stl_call(3, 0, 127, 5, 0, 3);
    OKC(r && ed_out[6] == 0 && ed_out[11] == 3 && (t->step[5].lvl & 3u) == 3u);
    r = stl_call(3, 0, 127, 5, 1, 2);
    OKC(r && ed_out[12] == 2 && (t->step[5].rat & 3u) == 2u);
    r = stl_call(3, 0, 127, 5, 7, 0);
    OKC(r && ed_out[6] == 1);
    r = stl_call(2, 0, 127, 5, 0, 0);
    OKC(r && ed_out[10] == 3 && ed_out[11] == 3 && ed_out[12] == 2);
    check(ok, "STEPLANE SET / STEP: chance, condition, nudge, fill, level, ratchet through the store; out-of-range rc 1; an empty step rc 3");
    /* the drum track */
    {
        dstep_t *d = &TDRUM->dstep[7];
        int32_t v;
        memset(d, 0, sizeof *d);
        memset(AL(TDRUM), 0, sizeof *AL(TDRUM));
        dstep_set(d, 2, LV_NORM, 0);
        r = stl_call(0, 3, 2, 0, 0, 0);
#if DLA_ON && FELUCCA_PLOCK
        n = ed_out[8];
        ok = r && n == DE_N;
        for (i = 0; i < n; i++) {
            stl_call(1, 3, 2, i, 0, 0);
            ok &= ed_out[8] == i && ed_out[9] == 1 && ed_out[10] == i;
        }
        check(ok, "STEPLANE LIST (drum lane): the lane's eight values TUNE .. LEVEL, kind 1");
        r = stl_call(4, 3, 2, 7, 0, 5);
        check(r && ed_out[6] == 0 && ed_out[10] & 1 && lane_lock_get(TDRUM, 7, 2, 0, &v) && v == 5 && ed_out[17] == 1, "STEPLANE LOCK on a drum lane: the lane's lock (TUNE = 5) on its hit");
        r = stl_call(4, 3, 2, 8, 0, 5);
        check(r && ed_out[6] == 3 && !lane_lock_get(TDRUM, 8, 2, 0, &v), "STEPLANE LOCK on a step the lane does not hit: rc 3 (D4), nothing written");
        r = stl_call(5, 3, 2, 7, 0, 0);
        check(r && ed_out[17] == 0 && !lane_lock_get(TDRUM, 7, 2, 0, &v), "STEPLANE UNLOCK on a drum lane");
        r = stl_call(4, 3, 3, 7, 0, 5);
        check(r && ed_out[6] == 3, "STEPLANE LOCK on another lane that does not hit there: rc 3");
#else
        check(r && ed_out[8] == 0, "STEPLANE LIST (drum lane) without lane locks: empty");
#endif
        r = stl_call(3, 3, 2, 7, 0, 3);
        check(r && ed_out[6] == 0 && ed_out[11] == 3 && dstep_lvl(d, 2) == 3, "STEPLANE SET level on a drum lane: the lane's level only");
        r = stl_call(3, 3, 2, 7, 1, 2);
        check(r && ed_out[6] == 0 && dstep_rat(d, 2) == 2 && dstep_lvl(d, 2) == 3, "STEPLANE SET ratchet on a drum lane");
        r = stl_call(3, 3, 5, 7, 0, 3);
        check(r && ed_out[6] == 3 && !dstep_has(d, 5), "STEPLANE SET level on a lane without a hit: rc 3, no hit made");
        r = stl_call(2, 3, 2, 7, 0, 0);
        check(r && ed_out[10] == 3 && ed_out[11] == 3 && ed_out[12] == 2, "STEPLANE STEP on a drum lane: flags (hit, lockable), level, ratchet");
        r = stl_call(2, 3, 127, 7, 0, 0);
        check(r && ed_out[10] == 3, "STEPLANE STEP on DR itself (lane 127): the step has a hit");
        memset(d, 0, sizeof *d);
        memset(AL(TDRUM), 0, sizeof *AL(TDRUM));
    }
    check(!stl_call(0, 4, 127, 0, 0, 0) && !stl_call(0, 0, 3, 0, 0, 0) && !stl_call(0, 3, 20, 0, 0, 0) && !stl_call(2, 0, 127, 64, 0, 0) && !stl_call(9, 0, 127, 0, 0, 0),
          "STEPLANE: a track past 3, a lane on a synth track, a lane past 16, a step past 63, an unknown op: no reply");
    ed_begin(1); ed_steplane_info();
    check(ed_n == 10u && ed_out[5] == 0x62u && ed_out[7] == 1u && (ed_out[8] & 2u) == (FELUCCA_PLOCK ? 2u : 0u) && (ed_out[9] & 1u) == (FELUCCA_CHANCE ? 1u : 0u), "ED_STEPLANE on: INFO carries tag 0x62 (version 1, the flags)");
    memset(t->step, 0, sizeof t->step);
    memset(AL(t), 0, sizeof *AL(t));
    v14_stable();
}
#endif
static void v14_tests(void)
{
#if FELUCCA_ED_UNDO
    v14_undo();
#else
    ed_begin(102);
    check(!ed_undo(102, (const uint8_t[]){0}, 1) && ed_n == 5u, "ED_UNDO off: cmd 102 is not handled (no reply)");
    ed_begin(1); ed_undo_info();
    check(ed_n == 5u, "ED_UNDO off: INFO has no tag 0x5F");
#endif
#if EDN_ON
    v14_note();
#else
    ed_begin(1); ed_note_info();
    check(ed_n == 5u && !ed_note(103, (const uint8_t[]){0, 0, 60, 100}, 4), "ED_NOTE off: cmd 103 is not handled, INFO has no tag 0x60");
#endif
#if FELUCCA_ED_SOLO
    v14_solo();
#else
    ed_begin(1); ed_solo_info();
    check(ed_n == 5u && !ed_solo(104, (const uint8_t[]){0}, 1), "ED_SOLO off: cmd 104 is not handled, INFO has no tag 0x61");
#endif
#if STL_ON
    v14_steplane();
#else
    ed_begin(1); ed_steplane_info();
    check(ed_n == 5u && !ed_steplane(105, (const uint8_t[]){0, 0, 127}, 3), "ED_STEPLANE off (or no automation store): cmd 105 is not handled, INFO has no tag 0x62");
#endif
}
