/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/editor_sync_test.c: editor protocol v14 (web/EDITOR_PROTOCOL.md "Undo", "Notes from the editor", "Solo and mute", "Step lanes"): 102 UNDO, 103 NOTE, 104 SOLO, 105 STEPLANE
 * (firmware/src/io/editor/ed_undo.c, ed_note.c, ed_solo.c, ed_steplane.c; seq/ednote.c). STEPLANE is built off here: its list is
 * the Optimist UI's lock pages (tests/ui_optimist_edv14.h, every check of it). Included by editor_sync_test.c after ed_sync9.c, its helpers (ed_begin, ed_out, ed_n, check) in scope. */
static void check(int c, const char *what);            /* (editor_sync_test.c) */
static uint8_t sync_reload;                                 /* (model.c's: UNDO sets it, as the panel's undo does) */
static struct { uint32_t last_ms; } ed_w;                                     /* (editor.c's; the part these files read) */
static void ed_known(uint32_t k, uint32_t id) { ed9_known_p(k, id); }
#include "../firmware/src/io/editor/ed_undo.c"
#include "../firmware/src/io/editor/ed_solo.c"
#include "../firmware/src/io/editor/ed_note.c"
#include "../firmware/src/io/editor/ed_steplane.c"
#include "../firmware/src/io/editor/ed_reads.h"

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
#if FELUCCA_USB_AUDIO
#define FELUCCA_USB_AUDIO_V 1
#else
#define FELUCCA_USB_AUDIO_V 0
#endif
#if FELUCCA_ED_SOLO
static void v14_ret_mute(void)                          /* op 7 (v4): a return's mute, the 13th reply arg */
{
    uint8_t q[3] = {7, 0, 1};
    uint32_t r, ln, k;
    fx_rmute = 0;
    r = v14_call(ed_solo, ED_SOLO, (const uint8_t[]){0}, 1);
    check(r && ed_n == 5u + 13u && ed_out[17] == 0, "SOLO 0: the reply has 13 args, ret 0 (no return muted)");
    for (k = 0; k < 3u; k++) {
        ln = fxs_line[1u + k];
        q[1] = (uint8_t)k, q[2] = 1;
        r = v14_call(ed_solo, ED_SOLO, q, 3);
        if (ln < 3u && (fxs_lnl >> ln & 1u)) {
            check(r && ed_out[5] == 7 && ed_out[6] == 0 && ed_out[17] == (1u << k) && fx_rmute == (1u << ln), "SOLO 7: return A / B / C muted: its bit in the reply (return order), the line's bit in fx_rmute");
            q[2] = 2;
            (void)v14_call(ed_solo, ED_SOLO, q, 3);
            check(fx_rmute == 0, "SOLO 7 toggle (2): unmuted");
            q[2] = 1;
            (void)v14_call(ed_solo, ED_SOLO, q, 3);
            q[2] = 0;
            r = v14_call(ed_solo, ED_SOLO, q, 3);
            check(r && ed_out[17] == 0 && fx_rmute == 0, "SOLO 7 off: unmuted");
        } else {
            check(!r && fx_rmute == 0, "SOLO 7 on an empty return: no reply, nothing set");
        }
    }
    q[1] = 3, q[2] = 1;
    check(!v14_call(ed_solo, ED_SOLO, q, 3), "SOLO 7 on return 3: no reply");
    q[1] = 0, q[2] = 3;
    check(!v14_call(ed_solo, ED_SOLO, q, 3) && !v14_call(ed_solo, ED_SOLO, q, 2), "SOLO 7: on 3, a short request: no reply");
    fx_rmute = 0;
}
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
    check(!v14_call(ed_solo, ED_SOLO, q, 3) && !v14_call(ed_solo, ED_SOLO, q, 2) && !v14_call(ed_solo, ED_SOLO, (const uint8_t[]){7}, 1), "SOLO: on 3, a short request, an unknown op: no reply");
    ed_begin(1); ed_solo_info();
    check(ed_n == 9u && ed_out[5] == 0x61u && ed_out[7] == 4u, "ED_SOLO on: INFO carries tag 0x61, version 4");
    song.solo = 0;
    dlm_solo_clear();
    q[0] = 3, q[1] = 4, q[2] = 1;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[9] == 7 && ed_out[16] == (FELUCCA_USB_AUDIO_V ? 4 : 0) && !trk_silent(TDRUM), "SOLO v3: a lane soloed: the drum track counts as soloed (silent mask: T1 T2 T3), the USB input silent (usb bit 2)");
    q[2] = 0;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
#if FELUCCA_USB_AUDIO
    q[0] = 5, q[1] = 0, q[2] = 1;
    r = v14_call(ed_solo, ED_SOLO, q, 3);
    check(r && ed_out[16] == 1 && song.solo == SOLO_USB && ed_out[9] == 15 && trk_silent(&trk[0]), "SOLO 5: the USB input soloed: usb bit 0, every track silent, the USB input heard");
    q[2] = 2;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    check(song.solo == 0 && !ut_silent(), "SOLO 5 toggle (2): off again");
    q[0] = 5, q[2] = 1;
    (void)v14_call(ed_solo, ED_SOLO, q, 3);
    ut.v[UTV_MUTE] = 1;
    r = v14_call(ed_solo, ED_SOLO, (const uint8_t[]){0}, 1);
    check(r && ed_out[16] == 7, "SOLO 0: the USB input soloed and muted (its FXP value): usb bits 0 1 2, a mute wins");
    ut.v[UTV_MUTE] = 0;
    song.solo = 0;
    check(!v14_call(ed_solo, ED_SOLO, (const uint8_t[]){5, 0, 3}, 3) && !v14_call(ed_solo, ED_SOLO, (const uint8_t[]){6, 0, 1}, 3), "SOLO 5 with on 3, an op 6: no reply");
    ed_begin(1); ed_solo_info();
    check(ed_out[8] == 3u, "ED_SOLO: INFO flags bit 0 (the USB input is here) and bit 1 (the returns' mute)");
#else
    check(!v14_call(ed_solo, ED_SOLO, (const uint8_t[]){5, 0, 1}, 3), "SOLO 5 without the USB input: no reply");
    ed_begin(1); ed_solo_info();
    check(ed_out[8] == 2u, "ED_SOLO: INFO flags bit 1 alone (no USB input)");
#endif
    song.solo = 0;
    v14_ret_mute();
}
#endif
static void v14_reads(void)                             /* ed_reads.h: a read asks for no ui.force (the panel flashed while the editor polled) */
{
    const uint8_t o0[] = {0}, o1[] = {1}, o2[] = {2}, o3[] = {3}, o4[] = {4};
    check(ed_reads(ED_UNDO, o0, 1) && !ed_reads(ED_UNDO, o1, 1) && !ed_reads(ED_UNDO, o2, 1)
          && ed_reads(ED_SOLO, o0, 1) && !ed_reads(ED_SOLO, o1, 1)
          && ed_reads(ED_FX, o0, 1) && !ed_reads(ED_FX, o1, 1) && ed_reads(ED_SONGC, o0, 1) && !ed_reads(ED_SONGC, o1, 1),
          "reads: UNDO SOLO FX SONG op 0 read, their other ops write");
    check(ed_reads(ED_FXP, o0, 1) && !ed_reads(ED_FXP, o1, 1) && ed_reads(ED_FXP, o2, 1) && ed_reads(ED_FXP, o3, 1) && !ed_reads(ED_FXP, o4, 1)
          && ed_reads(ED_DLAN, o0, 1) && ed_reads(ED_DLAN, o2, 1) && ed_reads(ED_DLAN, o3, 1) && !ed_reads(ED_DLAN, o4, 1)
          && ed_reads(ED_FAV, o1, 1) && !ed_reads(ED_FAV, o2, 1) && ed_reads(ED_STEPLANE, o2, 1) && !ed_reads(ED_STEPLANE, o3, 1),
          "reads: FXP DLAN ops 0 2 3, FAV 0 1, STEPLANE 0..2 read; the rest write");
    check(!ed_reads(ED_NOTE, o0, 1) && !ed_reads(ED_NOTE, o1, 1) && !ed_reads(ED_UNDO, o0, 0) && !ed_reads(5u, o0, 1),
          "reads: NOTE always plays (a write); an empty request or another command: not a read");
}
static void v14_tests(void)
{
    v14_reads();
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
    ed_begin(1); ed_steplane_info();                    /* (STEPLANE needs the Optimist UI's lock pages: tests/ui_optimist_edv14.h) */
    check(ed_n == 5u && !ed_steplane(105, (const uint8_t[]){0, 0, 127}, 3), "ED_STEPLANE off (the editor without the UI): cmd 105 is not handled, INFO has no tag 0x62");
}
