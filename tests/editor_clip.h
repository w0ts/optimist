/* SPDX-License-Identifier: GPL-3.0-only */
/* cmd 111 CLIP (firmware/src/io/editor/ed_clip.c), included by tests/editor_sync_test.c: a synth track's steps read a page at a
 * time and written one by one, as the app's piano roll does (add, remove, move, stretch: a TIE, a carry step of one note) */
static uint32_t clip_req(const uint8_t *a, uint32_t na)   /* -> the reply's payload length (after the 5-byte header), 0: none */
{
    ed_begin(ED_CLIP);
    if (!ed_clip(ED_CLIP, a, na))
        return 0;
    return ed_n - 5u;
}
static void clip_step(uint8_t *b, uint32_t n, uint32_t n0, uint32_t n1, uint32_t time, uint32_t carry, uint32_t e0)
{
    memset(b, 0, 15);
    b[0] = (uint8_t)n, b[1] = (uint8_t)n0, b[2] = (uint8_t)n1, b[5] = (uint8_t)time, b[7] = 100;
    b[9] = (uint8_t)(carry ? 4u : 0u);
    b[11] = (uint8_t)e0;
}
static uint32_t clip_write(uint32_t t, uint32_t i, const uint8_t *st)
{
    uint8_t a[18];
    a[0] = 2, a[1] = (uint8_t)t, a[2] = (uint8_t)i;
    memcpy(a + 3, st, 15);
    clip_req(a, 18);
    return ed_out[6];
}
static void clip_tests(void)
{
    uint8_t st[15], a[18];
    uint32_t n, i, ok;
    track_t *t = &trk[2];
    song.sel = 1;
    trk[2].p[P_SLEN] = 48;
    for (i = 40; i < 48; i++) memset(&t->step[i], 0, sizeof t->step[i]), t->step[i].time = ST_REST;

    a[0] = 0;
    n = clip_req(a, 1);
    ok = n == 4u + 2u * NTRK && ed_out[5] == 0 && ed_out[6] == 0 && ed_out[7] == 1 && ed_out[8] == NTRK;
    for (i = 0; ok && i < NTRK; i++)
        ok = ed_out[9 + 2 * i] == (trk_len(&trk[i]) & 127u) && ed_out[10 + 2 * i] == (uint8_t)is_drum(&trk[i]);
    check(ok && ed_out[9 + 4] == 48, "CLIP 0: the selected track, NTRK, each track's LEN and kind (the drums: 1)");

    ui.force = 0;
    clip_step(st, 2, 60, 64, ST_NOTE, 0, 32);
    check(clip_write(2, 40, st) == 0 && t->step[40].n == 2 && t->step[40].note[0] == 60 && t->step[40].note[1] == 64 &&
          t->step[40].time == ST_NOTE && t->step[40].vel == 100 && ngate_of(&t->step[40], 0) == 32 && ui.force,
          "CLIP 2: a chord added to a step (its notes, NOTE, vel, the first note's own end), the panel redrawn");
    check(memcmp(ed_out + 9, st, 15) == 0 && ed_n == 5u + 4u + 15u, "CLIP 2: the reply is the step as written, 15 bytes");
    check(note_span(t, 40, 0) == 32u, "CLIP 2: a note shortened inside its step: 32 / 128 (seq.c note_span)");

    /* stretch: the chord's 64 held over the next step alone (a carry step naming it), 60 ends with its step */
    clip_step(st, 2, 60, 64, ST_NOTE, 0, 0);
    clip_write(2, 40, st);
    clip_step(st, 1, 64, 0, ST_NOTE, 1, 0);
    check(clip_write(2, 41, st) == 0 && (t->step[41].flags & SF_CARRY), "CLIP 2: a carry step (hi bit 2) written");
    check(note_span(t, 40, 1) == 256u && note_span(t, 40, 0) == 128u, "CLIP 2: one note of a chord stretched over the next step alone (carry), the other unchanged");
    /* stretch the whole step: TIEs */
    clip_step(st, 0, 0, 0, ST_TIE, 0, 0);
    clip_write(2, 41, st);
    clip_write(2, 42, st);
    check(note_span(t, 40, 0) == 384u && note_span(t, 40, 1) == 384u, "CLIP 2: a step held 3 steps long by two TIEs");

    a[0] = 1, a[1] = 2, a[2] = 40, a[3] = 3;
    n = clip_req(a, 4);
    check(n == 5u + 3u * 15u && ed_out[5] == 1 && ed_out[6] == 0 && ed_out[7] == 2 && ed_out[8] == 40 && ed_out[9] == 3 &&
          ed_out[10] == 2 && ed_out[11] == 60 && ed_out[10 + 15 + 5] == ST_TIE, "CLIP 1: three steps read, each as CLIP 2 gives it");
    a[3] = 40;
    check(clip_req(a, 4) == 5u + 16u * 15u && ed_out[9] == 16, "CLIP 1: a page is 16 steps at most");
    a[2] = NSTEP - 4u, a[3] = 16;
    check(clip_req(a, 4) == 5u + 4u * 15u && ed_out[9] == 4, "CLIP 1: never past NSTEP");

    {   /* refused: the drum track, an index past NSTEP, a short step; nothing written */
        dstep_t was = trk[TRK_DRUM].dstep[5];
        step_t s41 = t->step[41];
        clip_step(st, 1, 36, 0, ST_NOTE, 0, 0);
        check(clip_write(TRK_DRUM, 5, st) == 1 && memcmp(&was, &trk[TRK_DRUM].dstep[5], sizeof was) == 0 && ed_n == 5u + 4u,
              "CLIP 2: the drum track refused, untouched, no step in the reply");
        check(clip_write(2, NSTEP, st) == 1, "CLIP 2: an index past NSTEP refused");
        a[0] = 2, a[1] = 2, a[2] = 41;
        memcpy(a + 3, st, 14);
        clip_req(a, 17);
        check(ed_out[6] == 1 && memcmp(&s41, &t->step[41], sizeof s41) == 0, "CLIP 2: a step of 14 bytes refused, untouched");
        a[0] = 1, a[1] = TRK_DRUM, a[2] = 0, a[3] = 4;
        check(clip_req(a, 4) == 5u && ed_out[6] == 1 && ed_out[9] == 0, "CLIP 1: the drum track: refused, no steps");
        a[0] = 3;
        check(clip_req(a, 1) == 0, "CLIP: an unknown op: no reply");
    }
    {   /* a write is an undo level of the device's own history: the panel's undo puts the step back */
        step_t before;
        fm1_ms += 2000;                                       /* (a new level: past the 800 ms of the last write) */
        before = t->step[44];
        clip_step(st, 1, 72, 0, ST_NOTE, 0, 0);
        clip_write(2, 44, st);
        check(t->step[44].note[0] == 72 && undo_can(0) && undo_apply(0) && memcmp(&before, &t->step[44], sizeof before) == 0,
              "CLIP 2: an undo level of the device's history (the panel's undo puts the step back)");
    }
}
