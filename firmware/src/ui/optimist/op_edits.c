/* SPDX-License-Identifier: GPL-3.0-only */
/* What the work has that is not stored (docs/UI-OPTIMIST-DESIGN.md section 23; the user, 2026-10-13: "always ask if the
 * sound/preset has edits"; a scene loaded stopped asks "only when the current work differs from what is stored").
 *   a track's sound   edited when its sound differs from what it was when it was loaded or saved: a signature of
 *                     the values a preset sets (model.c param_kept: not the pattern, not the mix) and, on the drum
 *                     track, the lanes' own sounds (dl: offsets, sources, samples; the sends), taken again whenever
 *                     the sound is another one (its engine, preset, user preset; the drum track's kit, user kit:
 *                     ed_tick, every frame, no hash) or the UI loads / saves it (snd_mark). Anything that replaces an
 *                     edited sound asks "Discard T2 edits?" first: PRESETS, HOME + PRESETS, the SOUND row's PRESET
 *                     and ENGINE, a lane's own sound, INIT, a user preset's LOAD. An unedited sound never asks, so
 *                     browsing a preset at a time goes on freely; after YES the new sound is unedited again
 *   the work          changed when the tracks, sounds, lanes and globals differ from when a scene or project was last
 *                     loaded, stored or made NEW (work_mark): a scene or project LOAD asks only then
 * Signatures: FNV-1a in the main loop, on demand (the work's only when a load asks); 4 words of RAM a track. */
enum { EK_NONE, EK_PRESET, EK_ENGINE, EK_LSOUND, EK_LKIND, EK_SNDENG };
static struct {
    uint32_t id[NTRK], sig[NTRK];       /* the sound as loaded / saved: what it is, its signature */
    uint8_t ok;                         /* bit t: id / sig taken for track t */
    uint8_t kind, trk, go;              /* the change asked for (ED_*), its track; go: running it, confirmed */
    int8_t s;                           /* its direction */
    uint8_t work_ok;
    uint32_t work;                      /* the work's signature as last loaded / stored */
} edt;

static uint32_t ed_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t snd_id(uint32_t i)                      /* which sound track i plays (not how it is edited) */
{
    const track_t *t = &trk[i % NTRK];
    if (is_drum(t))
        return 0x80000000u | (uint32_t)dl.ukit << 16 | (uint16_t)t->p[P_E0];
    return (uint32_t)t->eng_req | (uint32_t)t->preset << 8 | (uint32_t)t->user << 16;
}
static uint32_t snd_sig(uint32_t i)                     /* the sound as it is now */
{
    const track_t *t = &trk[i % NTRK];
    uint32_t h = 2166136261u, k;
    for (k = 0; k < P_COUNT; k++)
        if (!param_kept(k))
            h = ed_mix(h, &t->p[k], sizeof t->p[k]);
    if (is_drum(t)) {
        h = ed_mix(h, dl.ofs, sizeof dl.ofs);
        h = ed_mix(h, dl.src, sizeof dl.src);
        h = ed_mix(h, dl.ref, sizeof dl.ref);
        h = ed_mix(h, dsend, sizeof dsend);
    }
    return h;
}
static void snd_mark(uint32_t i)                        /* track i's sound is as loaded / saved now */
{
    i %= NTRK;
    edt.id[i] = snd_id(i);
    edt.sig[i] = snd_sig(i);
    edt.ok |= (uint8_t)(1u << i);
}
static void snd_mark_all(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        snd_mark(i);
}
static int snd_edited(uint32_t i)                       /* track i's sound edited since it was loaded / saved */
{
    i %= NTRK;
    if (!((edt.ok >> i) & 1u) || edt.id[i] != snd_id(i)) {
        snd_mark(i);                                    /* (another sound since: as loaded) */
        return 0;
    }
    return edt.sig[i] != snd_sig(i);
}
static uint32_t work_sig(void)                          /* the tracks, their sounds, the lanes, the globals */
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NTRK; i++) {
        h = ed_mix(h, trk[i].p, sizeof trk[i].p);
        h = ed_mix(h, trk[i].step, sizeof trk[i].step);
        h = ed_mix(h, trk[i].dstep, sizeof trk[i].dstep);
        h = ed_mix(h, &trk[i].eng_req, 1);
        h = ed_mix(h, &trk[i].preset, 1);
    }
    h = ed_mix(h, &dl, sizeof dl);
    h = ed_mix(h, dsend, sizeof dsend);
    for (i = 0; i < G_COUNT; i++)
        if (i != G_MIDI && i != G_SLOT)                 /* (the clock followed now, the slot shown: not the work) */
            h = ed_mix(h, &song.g[i], sizeof song.g[i]);
    return h;
}
static void work_mark(void)                             /* the work is as stored now (a load, a store, NEW) */
{
    edt.work = work_sig();
    edt.work_ok = 1;
    snd_mark_all();
}
static int work_changed(void) { return edt.work_ok && edt.work != work_sig(); }
static void ed_tick(void)                               /* once a frame: another sound is a fresh one (no hash) */
{
    uint32_t i;
    if (!edt.work_ok)
        work_mark();                                    /* (power-on: the work as loaded) */
    for (i = 0; i < NTRK; i++)
        if (edt.id[i] != snd_id(i))
            snd_mark(i);
}

static void op_preset_step(int32_t s);                  /* op_screens.c, op_walk.c: what may replace the sound */
static void op_engine_step(int32_t s);
static void snd_engine_step(int32_t s);
static void lane_sound_step(int32_t s);
static void lane_kind_step(int32_t s);
/* before a change of kind on the selected track's sound: 1 = asked ("Discard T2 edits?"), the caller does nothing now
 * (YES runs it: ed_run). 0: go on (unedited, or confirmed) */
static int ed_ask(uint32_t kind, int32_t s)
{
    char a[10];
    if (edt.go)
        return 0;
    if (op_armed())
        return 1;                                       /* (a question up: the turn is drained) */
    if (!snd_edited(song.sel))
        return 0;
    edt.kind = (uint8_t)kind;
    edt.trk = song.sel;
    edt.s = (int8_t)(s < 0 ? -1 : 1);
    str_cpy(a, trk_tag(song.sel), sizeof a);
    str_cpy(a + 2, " EDITS", sizeof a - 2u);
    op_arm(ARM_OP, OA_DISCARD, song.sel, "DISCARD", a, 1);   /* ("Discard T2 edits?") */
    return 1;
}
static void ed_done(void) { snd_mark(song.sel); }       /* a change made: the new sound is unedited */
static void ed_run(uint32_t trk_i)                      /* YES on "Discard T2 edits?": the change asked for */
{
    if (trk_i != song.sel)
        return;                                         /* (another track since: nothing) */
    edt.go = 1;
    switch (edt.kind) {
    case EK_PRESET:
        op_preset_step(edt.s);
        break;
    case EK_ENGINE:
        op_engine_step(edt.s);
        break;
    case EK_SNDENG:
        snd_engine_step(edt.s);
        break;
    case EK_LSOUND:
        lane_sound_step(edt.s);
        break;
    case EK_LKIND:
        lane_kind_step(edt.s);
        break;
    default:
        break;
    }
    edt.go = 0;
    edt.kind = EK_NONE;
    ed_done();
}
