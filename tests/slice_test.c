/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE engine tests on the Mac (same sources as the firmware, through hostsim.c; run_tests.sh):
 *   build/host/slice_test LOOP DEMO_DIR
 * LOOP.hdr / LOOP.bin: a user slot image written by `fm1_sample_upload.py build` from tests/slice_loop.py's
 * WAV (two bars of drums at 96 BPM with a swing), LOOP.hits its onsets (s). The slot is put into a RAM
 * image of the flash slots (SMP_USER_XIP) and read by smp_user_scan as at boot.
 * 1. BREAK's build-time table (gen_samples.py ima_states) == the firmware decoder's states.
 * 2. AUTO: the detector (slc_scan) on BREAK finds its hits; on the user loop every onset has a slice
 *    start at most 6 ms before it and 1 ms after it, and no slice starts away from an onset.
 * 3. REV: a slice read backwards (64-sample windows from checkpoints) == the forward decode reversed.
 * 4. keys / steps -> slices (mod the count, START, ROOT, an empty slot plays BREAK); ONE / GATE / LOOP.
 * 5. demos into DEMO_DIR: every preset with its pattern, BREAK re-sequenced, the user loop sliced AUTO. */
#include <stdarg.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3, as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("slice: %-58s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

/* 1: every stored state == the decoder's state there; returns the mismatches */
static uint32_t table_check(const slc_src_t *s)
{
    slc_dec_t d;
    uint32_t pos, k = 0, a = 0, bad = 0;
    slc_dec_at(&d, 0, 0);
    for (pos = 0; pos < s->len; pos++) {
        while (k < SLC_GRID && slc_gpos(s, k) == pos)
            bad += s->grid[k++] != slc_dec_st(&d);
        while (a < s->nauto && s->apos[a] == pos)
            bad += s->ast[a++] != slc_dec_st(&d);
        slc_dec_next(s, &d);
    }
    return bad + (SLC_GRID - k) + (s->nauto - a);
}

/* 2: hits h[] (samples) against slice starts: each hit has a start in [h - early, h + late], each start
 * (but the first, at 0) is within that of a hit. Writes a list into msg. */
static int match(const slc_src_t *s, const uint32_t *h, uint32_t nh, uint32_t early, uint32_t late, char *msg,
                 size_t mn, int32_t *worst)
{
    uint32_t i, j, miss = 0, extra = 0;
    int32_t w = 0;
    for (i = 0; i < nh; i++) {
        int found = 0;
        for (j = 0; j < s->nauto; j++)
            if (s->apos[j] + early >= h[i] && s->apos[j] <= h[i] + late) {
                int32_t e = (int32_t)h[i] - (int32_t)s->apos[j];
                found = 1;
                w = abs(e) > abs(w) ? e : w;
            }
        miss += !found;
    }
    for (j = 1; j < s->nauto; j++) {
        int near = 0;
        for (i = 0; i < nh; i++)
            near |= s->apos[j] + early >= h[i] && s->apos[j] <= h[i] + late;
        extra += !near;
    }
    snprintf(msg, mn, "%u onsets, %u slices, %u missed, %u extra, worst %+.1f ms", nh, s->nauto, miss, extra,
             w * 1000.0 / (s->rate * 44100.0 / 65536.0));
    *worst = w;
    return !miss && !extra && (nh == 0 || h[0] > early || s->apos[0] == 0);
}

/* 3: slice j of DIV div read backwards == forwards reversed */
static uint32_t rev_check(uint32_t src, uint32_t div, uint32_t j)
{
    const slc_src_t *s = slc_get(src);
    static int32_t fw[1 << 18];
    static int16_t rb[SLC_RB];
    voice_t v;
    slc_dec_t d;
    uint32_t a, b, st, i, n, bad = 0;
    int32_t x;
    slc_bounds(s, div, j, &a, &b, &st);
    slc_dec_at(&d, a, st);
    for (n = 0; d.pos < b && n < (1u << 18); n++)
        fw[n] = slc_dec_next(s, &d);
    memset(&v, 0, sizeof v);
    v.ph[0] = b;
    v.ph[2] = a;
    v.s[0] = 0x7FFFFFFF;
    v.s[4] = (int32_t)(src | (st >> 24) << 2 | 1u << 6 | j << 8 | div << 16);
    for (i = 0; i < n; i++)
        bad += !slc_rev(s, &v, rb, 0, &x) || x != fw[n - 1u - i];
    bad += slc_rev(s, &v, rb, 0, &x) != 0;               /* and then it ends */
    return bad;
}

static uint32_t slice_of(const voice_t *v) { return ((uint32_t)v->s[4] >> 8) & 63u; }
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}
static void blocks(uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}

/* 5: a pattern on track 1 (SLICE preset pi, then edits), the sequencer for `bars` bars, a tail */
typedef struct {
    const char *file;
    uint32_t preset, bpm, nsteps, bars;
    const uint8_t *notes;
    int16_t src, div;                            /* -1 = the preset's */
} demo_t;
static int demo(const char *dir, const demo_t *dm)
{
    char path[512];
    FILE *w;
    track_t *t = &trk[0];
    uint32_t f, i, frames, peak = 0, bar = 0;
    snprintf(path, sizeof path, "%s/%s", dir, dm->file);
    if (!(w = fopen(path, "wb")))
        return 1;
    host_tracks_init();
    song.g[G_BPM] = (int16_t)dm->bpm;
    host_preset(t, 8, dm->preset);
    if (dm->src >= 0)
        t->p[P_E0] = dm->src;
    if (dm->div >= 0)
        t->p[P_E1] = dm->div;
    for (i = 0; i < dm->nsteps; i++) {
        uint8_t n = dm->notes[i];
        put_step(t, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    t->p[P_SLEN] = (int16_t)dm->nsteps;
    bar = (uint32_t)(4.0 * 60.0 / dm->bpm * FS);
    frames = dm->bars * bar + FS;
    wav_hdr(w, frames);
    transport_req = 1;
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        if (f >= dm->bars * bar && f < dm->bars * bar + CTL)
            transport_req = 2;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)abs(o[2 * i]);
            peak = a > peak ? a : peak;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    printf("slice: demo %-30s %u bars at %u BPM, peak %u\n", dm->file, dm->bars, dm->bpm, peak);
    return peak < 2000u || peak > 32767u;
}

static int in_child(int (*fn)(const char *, const demo_t *), const char *dir, const demo_t *dm)
{
    pid_t pid;
    int st = 0;
    fflush(stdout);
    if (!(pid = fork())) {
        int rc = fn(dir, dm);
        fflush(stdout);
        _exit(rc);
    }
    waitpid(pid, &st, 0);
    return !WIFEXITED(st) || WEXITSTATUS(st);
}

static long load(const char *path, void *dst, long max)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return -1;
    n = (long)fread(dst, 1, (size_t)max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static const uint8_t CHOP[16] = {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64};   /* ui.c 9..11 */
    static const uint8_t STUTTER[16] = {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67};
    static const uint8_t SLICES[16] = {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75};
    static const uint8_t RESEQ[32] = {60, 0, 62, 60, 64, 0, 67, 62, 60, 69, 70, 0, 72, 0, 64, 72,   /* 2 bars */
                                      60, 61, 60, 61, 64, 0, 66, 67, 68, 64, 70, 71, 72, 72, 72, 72};
    static const uint8_t USR[32] = {60, 0, 61, 0, 62, 0, 63, 64, 65, 0, 66, 0, 67, 0, 60, 61,
                                    60, 0, 60, 63, 62, 0, 68, 0, 65, 66, 67, 0, 62, 70, 71, 72};
    const char *loop = argc > 1 ? argv[1] : "build/host/slice_loop";
    const char *dir = argc > 2 ? argv[2] : "build/slice_demo";
    char path[512], msg[160];
    uint32_t hits[64], nh = 0, i, bad;
    int32_t worst;
    long n;

    /* 1 */
    check("BREAK: build-time table == decoder states", !(bad = table_check(&SLC_BREAK)), "%u of %u differ", bad,
          SLC_GRID + SLC_BREAK.nauto);

    /* 2: the detector on BREAK (hits = its build-time AUTO table) */
    {
        static slc_src_t tmp;
        tmp = SLC_BREAK;
        tmp.len = 0;
        slc_scan(&tmp, SLC_BREAK.len);
        tmp.len = SLC_BREAK.len;
        bad = memcmp(tmp.grid, SLC_BREAK.grid, sizeof tmp.grid) != 0;
        check("BREAK: slc_scan grid == build-time grid", !bad, 0);
        check("BREAK: slc_scan finds the hits (-6 .. +1 ms)",
              match(&tmp, SLC_BREAK.apos, SLC_BREAK.nauto, 132, 22, msg, sizeof msg, &worst), "%s", msg);
    }

    /* 2: the user loop, through the slot image and smp_user_scan */
    snprintf(path, sizeof path, "%s.hdr", loop);
    n = load(path, host_slots, SMP_USER_DATA);
    snprintf(path, sizeof path, "%s.bin", loop);
    n = n == (long)sizeof(smp_user_hdr_t) ? load(path, (uint8_t *)host_slots + SMP_USER_DATA, SMP_USER_SIZE - SMP_USER_DATA) : -1;
    {
        FILE *f;
        double s;
        snprintf(path, sizeof path, "%s.hits", loop);
        if ((f = fopen(path, "r"))) {
            while (nh < 64u && fscanf(f, "%lf", &s) == 1)
                hits[nh++] = (uint32_t)(s * 22050.0 + 0.5);
            fclose(f);
        }
    }
    if (n <= 0 || !nh) {
        printf("slice: no user loop (%s.hdr / .bin / .hits: tests/slice_loop.py + fm1_sample_upload.py build)\n", loop);
        return 1;
    }
    if ((uintptr_t)host_slots < (uintptr_t)SMP_DATA || (uintptr_t)host_slots - (uintptr_t)SMP_DATA > 0xF0000000u) {
        printf("slice: the slot image must lie above SMP_DATA within 4 GiB (32-bit offsets)\n");
        return 1;
    }
    smp_user_scan(0);
    check("USR1: valid after smp_user_scan, slice table built", usr_nz[0] && slc_usr[0].len && slc_get(1) != 0,
          "%u zones, %u samples", usr_nz[0], slc_usr[0].len);
    check("USR1: table == decoder states", !(bad = table_check(&slc_usr[0])), "%u differ", bad);
    {   /* a slot being rescanned (len 0) while a voice fills its buffer: silence, no divide by zero
         * (a wrong value on the FM-1, its div0 trap is off; tools/div_audit.txt) */
        slc_src_t z = slc_usr[0];
        int16_t rb[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        z.len = 0;
        slc_fill(&z, rb, 0, 8, 0, 0);
        check("USR1 rescanning (len 0): slc_fill gives silence", !(rb[0] | rb[3] | rb[7]), "%d %d %d", rb[0], rb[3], rb[7]);
    }
    check("USR1: AUTO slices at the onsets (-6 .. +1 ms)", match(&slc_usr[0], hits, nh, 132, 22, msg, sizeof msg, &worst),
          "%s", msg);
    printf("slice: USR1 AUTO starts (ms):");
    for (i = 0; i < slc_usr[0].nauto; i++)
        printf(" %.1f", slc_usr[0].apos[i] * 1000.0 / 22050.0);
    printf("\nslice: onsets (ms):          ");
    for (i = 0; i < nh; i++)
        printf(" %.1f", hits[i] * 1000.0 / 22050.0);
    printf("\n");
    check("USR2 / USR3: empty, no material", !slc_get(2) && !slc_get(3), 0);

    /* 3 */
    for (bad = 0, i = 0; i < 16u; i++)
        bad += rev_check(0, 2, i);
    for (i = 0; i < 4u; i++)
        bad += rev_check(0, 0, i);
    for (i = 0; i < SLC_BREAK.nauto; i++)
        bad += rev_check(0, SLC_DIV_AUTO, i);
    for (i = 0; i < 4u; i++)
        bad += rev_check(1, 0, i);
    for (i = 0; i < slc_usr[0].nauto; i++)
        bad += rev_check(1, SLC_DIV_AUTO, i);
    check("REV: backwards == forwards reversed (BREAK, USR1; 4 / 16 / AUTO)", !bad, "%u samples differ", bad);

    /* 4: keys / steps -> slices */
    {
        track_t *t = &trk[0];
        voice_t *v;
        uint32_t a, b, st, ok = 1;
        host_tracks_init();
        host_preset(t, 8, 0);                              /* BREAK 16 */
        trk_note_on(t, 65, 100);
        v = voice_of(t, 65);
        slc_bounds(&SLC_BREAK, 2, 5, &a, &b, &st);
        ok &= v && slice_of(v) == 5u && v->ph[0] == a && v->ph[2] == b;
        trk_note_on(t, 59, 100);
        ok &= (v = voice_of(t, 59)) && slice_of(v) == 15u;
        trk_note_on(t, 76, 100);
        ok &= (v = voice_of(t, 76)) && slice_of(v) == 0u;
        t->p[P_E2] = 3;                                    /* START */
        trk_note_on(t, 60, 100);
        ok &= (v = voice_of(t, 60)) && slice_of(v) == 3u;
        t->p[P_E2] = 0;
        t->p[P_ROOT] = 2;                                  /* ROOT D: D4 is slice 0 */
        trk_note_on(t, 62, 100);
        ok &= (v = voice_of(t, 62)) && slice_of(v) == 0u;
        check("keys: note - C4 - ROOT + START mod 16", ok, 0);
        ok = 1;
        t->p[P_ROOT] = 0;
        t->p[P_E1] = SLC_DIV_AUTO;
        trk_note_on(t, 72, 100);                           /* 12 mod 10 hits */
        ok &= (v = voice_of(t, 72)) && slice_of(v) == 12u % SLC_BREAK.nauto && v->ph[0] == SLC_BREAK.apos[12u % SLC_BREAK.nauto];
        t->p[P_E0] = 2;                                    /* USR2: empty -> BREAK */
        trk_note_on(t, 61, 100);
        ok &= (v = voice_of(t, 61)) && ((uint32_t)v->s[4] & 3u) == 0u && v->ph[0] == SLC_BREAK.apos[1];
        t->p[P_E0] = 1;                                    /* USR1 */
        trk_note_on(t, 63, 100);
        ok &= (v = voice_of(t, 63)) && ((uint32_t)v->s[4] & 3u) == 1u && v->ph[0] == slc_usr[0].apos[3];
        check("keys: AUTO count, an empty slot plays BREAK, USR1", ok, 0);
        ok = 1;
        song.octave = 0;
        t->p[P_E0] = 0;
        ok &= kb_map(t, 0) == 60u && kb_map(t, 9) == 69u;
        check("keys: the lowest key is slice 0 (no scale)", ok, 0);
    }
    {   /* ONE / GATE / LOOP: a 125 ms slice, the note released after 10 ms */
        static const char *const MN[3] = {"ONE", "GATE", "LOOP"};
        uint32_t m;
        for (m = 0; m < 3u; m++) {
            track_t *t = &trk[0];
            voice_t *v;
            uint32_t held, after, k;
            host_tracks_init();
            host_preset(t, 8, 0);
            t->p[P_E4] = (int16_t)m;
            t->p[P_REL] = 10;
            trk_note_on(t, 60, 100);
            v = voice_of(t, 60);
            if (m == SLC_LOOP) {
                blocks(FS / CTL);                          /* 1 s held: still looping */
                held = v && v->active;
            } else {
                held = 1;
            }
            blocks(FS / 100u / CTL);
            trk_note_off(t, 60);
            blocks(FS / 20u / CTL);                        /* 50 ms after the note-off */
            after = v && v->active;
            for (k = 0; k < FS / 4u / CTL && v && v->active; k++)
                blocks(1);
            snprintf(msg, sizeof msg, "MODE %s: %s", MN[m], m == SLC_ONE ? "plays on after the note-off, ends at the slice end" :
                     m == SLC_GATE ? "stops at the note-off" : "loops while held, ends after the note-off");
            check(msg, held && (m == SLC_ONE ? after : !after) && !(v && v->active), 0);
        }
    }

    /* 5 */
    {
        const demo_t D[] = {
            {"preset_break16.wav", 0, 120, 16, 4, CHOP, -1, -1},
            {"preset_chop8.wav", 1, 120, 16, 4, STUTTER, -1, -1},
            {"preset_reverse.wav", 2, 120, 16, 4, SLICES, -1, -1},
            {"preset_usr_slice_empty.wav", 3, 120, 16, 4, SLICES, -1, -1},     /* no slot: BREAK, AUTO */
            {"break_in_order.wav", 0, 120, 16, 2, SLICES, -1, -1},
            {"break_resequenced.wav", 0, 120, 32, 4, RESEQ, -1, -1},
            {"break_resequenced_32.wav", 0, 120, 32, 4, RESEQ, -1, 3},
            {"usr_loop_auto_in_order.wav", 3, 96, 16, 2, SLICES, 1, -1},
            {"usr_loop_auto_resequenced.wav", 3, 96, 32, 4, USR, 1, -1},
            {"usr_loop_16_resequenced.wav", 0, 96, 32, 4, USR, 1, 2},
        };
        int df = 0;
        for (i = 0; i < sizeof D / sizeof D[0]; i++)
            df += in_child(demo, dir, &D[i]);
        check("demos rendered (peak above -24 dBFS, no clipping)", !df, "%s", dir);
    }
    printf("slice: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
