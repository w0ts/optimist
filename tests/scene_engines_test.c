/* SPDX-License-Identifier: GPL-3.0-only */
/* The engines ported from the scene (tests/run_tests.sh builds this with $SCENE_ON: Clipper's STRINGS VPS TERRAIN
 * WSEQ VECTOR, engines/clipper/, Mini-Tone's SID, engines/sid/, PROPHET, engines/prophet/, Clipper's WAVE), each engine the same checks:
 *   - its UID and fallback (registry.h), 26 engine UIDs;
 *   - every preset: a 4-note chord held 2 s, the keys up, 10 s more: audible, bounded, no DC, every voice free at the
 *     end, its render's FNV-1a 64 the one in tests/golden_scene_engines.txt (GOLDEN_UPDATE=1 rewrites it); the
 *     presets' level within 9 dB of ANALOG's;
 *   - each EDIT value changes the render of the first preset, bounded;
 *   - the corners (every value 0, every value 127, two mixes) on notes 24 / 60 / 108: bounded, no DC run-away, quiet
 *     and free after the release;
 *   - the render alone twice the same (deterministic), and its period the key's (220 Hz) on a plain setting.
 * env: SCENE_WAV=DIR writes each preset's render to DIR/ENGINE-NN-NAME.wav.
 *   build/host/scene_engines_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *eng, const char *what)
{
    printf("scene: %-8s %-80s %s\n", eng, what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++)
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
}

typedef struct { uint64_t hash; double rms, dc, tail; int32_t peak; uint32_t busy; } se_res_t;
#define KEEP INT16_MIN               /* (an EDIT value left as the preset has it) */
static const uint8_t CHORD[4] = {48, 55, 60, 64};

/* each engine: its slot, UID, fallback UID, a plain setting whose period is the key's (KEEP: the preset's value) */
typedef struct {
    uint32_t slot, uid, fb;
    int16_t pure[8];
} se_eng_t;
static const se_eng_t ENGS[] = {
    {ENG_SLOT_STRINGS, ENG_UID_STRINGS, 0, {0, 127, 0, 0, 127, KEEP, 0, 0}},    /* (8' alone: the key's pitch) */
    {ENG_SLOT_VPS, ENG_UID_VPS, 2, {64, 16, 0, 0, 127, 0, 0, 0}},
    {ENG_SLOT_TERRAIN, ENG_UID_TERRAIN, 0, {0, 30, 32, 16, 64, 0, 127, 0}},   /* (off the surface's centre: at it the pitch doubles) */
    {ENG_SLOT_WSEQ, ENG_UID_WSEQ, 0, {3, 40, 0, 0, 0, 0, 127, 0}},            /* (PITCH 0: every step at the key) */
    {ENG_SLOT_VECTOR, ENG_UID_VECTOR, 0, {2, 2, 2, 2, 0, 0, 0, 127}},         /* (four saws, MOVE off) */
    {ENG_SLOT_SID, ENG_UID_SID, 3, {2, 64, 0, 0, 0, 127, 0, 1}},              /* (a square, the filter off, the 8580) */
    {ENG_SLOT_PROPHET, ENG_UID_PROPHET, 0, {0, 0, 0, -64, 127, 0, 0, 0}},     /* (A's saw alone, the filter open) */
    {ENG_SLOT_WAVE, ENG_UID_WAVE, 0, {0, 0, 0, 0, 127, 0, 0, 0}},             /* (BASIC's first frame, the filter open) */
};
#define NENG (sizeof ENGS / sizeof ENGS[0])

static void dry(void) { trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0; }
/* WAVE's table built for the values set after the preset (hostsim.c host_main_services), before the notes */
static void tables(void) { host_main_services(); }
static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

/* preset pi of engine slot eng, the EDIT values set[k] (KEEP: the preset's), nn notes from notes[]: held 2 s, the keys
 * up, 10 s more (SPACE DRONE's release is long); tail: the RMS of the last second */
static se_res_t se_render(uint32_t eng, uint32_t pi, const int16_t *set, const uint8_t *notes, uint32_t nn, FILE *wav)
{
    se_res_t r = {1469598103934665603ull, 0, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0, tl = 0;
    uint32_t b, i, nb = 12u * FS / CTL;
    reset();
    host_preset(&trk[0], eng, pi);
    dry();
    for (i = 0; set && i < 8u; i++)
        if (set[i] != KEEP)
            trk[0].p[P_E0 + i] = set[i];
    tables();
    for (i = 0; i < nn; i++)
        trk_note_on(&trk[0], notes[i], 100);
    if (wav)
        wav_hdr(wav, nb * CTL);
    for (b = 0; b < nb; b++) {
        if (b == 2u * FS / CTL)
            for (i = 0; i < nn; i++)
                trk_note_off(&trk[0], notes[i]);
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            acc += (double)out[i] * out[i];
            sum += out[i];
            if (b >= nb - FS / CTL)
                tl += (double)out[i] * out[i];
            if (a > r.peak)
                r.peak = a;
            r.hash = (r.hash ^ (uint32_t)out[i]) * 1099511628211ull;
        }
        if (wav)
            for (i = 0; i < CTL; i++)
                wav_put(wav, out[2 * i], out[2 * i + 1]);
    }
    r.busy = active(&trk[0]);
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    r.dc = sum / (2.0 * nb * CTL);
    r.tail = sqrt(tl / (2.0 * FS));
    return r;
}

static double analog_rms;
static void t_level(const se_eng_t *g)
{
    const engine_t *e = ENGINES[g->slot];
    uint32_t pi;
    double c = 0;
    char what[96];
    for (pi = 0; pi < e->npresets; pi++)
        c += se_render(g->slot, pi, 0, CHORD, 4, 0).rms / e->npresets;
    snprintf(what, sizeof what, "the presets' level within 9 dB of ANALOG's (%+.1f dB)", 20 * log10(c / analog_rms));
    check(fabs(20 * log10(c / analog_rms)) < 9.0, e->name, what);
}

/* golden file lines: "ENGINE_NN_NAME hash" */
#define GMAX 160u
static char gname[GMAX][48];
static uint64_t gold[GMAX];
static uint32_t ng, gseen[GMAX];
static FILE *gout;
static void t_presets(const se_eng_t *g, int upd)
{
    const engine_t *e = ENGINES[g->slot];
    uint32_t pi, ok = 1, changed = 0, missing = 0;
    const char *wdir = getenv("SCENE_WAV");
    for (pi = 0; pi < e->npresets; pi++) {
        char s[48];
        FILE *w = 0;
        uint32_t k, n0, found = 0;
        se_res_t r;
        n0 = (uint32_t)snprintf(s, sizeof s, "%s_%02u_", e->name, pi);
        for (k = 0; e->presets[pi].name[k] && n0 + k < sizeof s - 1u; k++)
            s[n0 + k] = e->presets[pi].name[k] == ' ' ? '_' : e->presets[pi].name[k];
        s[n0 + k] = 0;
        if (wdir) {
            char p[512];
            snprintf(p, sizeof p, "%s/%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        r = se_render(g->slot, pi, 0, CHORD, 4, w);
        if (w)
            fclose(w);
        for (k = 0; k < ng && !found; k++)
            if (!strcmp(gname[k], s)) {
                found = 1, gseen[k] = 1;
                if (gold[k] != r.hash)
                    changed++, printf("scene: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        if (gout)
            fprintf(gout, "%s %016llx\n", s, (unsigned long long)r.hash);
        printf("scene: %-28s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, r.rms, r.peak, r.dc,
               (unsigned long long)r.hash, r.busy ? "STILL SOUNDING" : "");
        ok &= (r.rms > 300 || r.peak > 2000) && r.peak <= 32767 && (!r.busy || r.tail < 2) && r.dc < 50 && r.dc > -50;   /* (a long release: inaudible, freed later) */
    }
    check(ok, e->name, "every preset: audible (rms > 300, a hit: peak > 2000), bounded, no DC, silent 10 s after the release (its voices free, or inaudible)");
    if (!upd)
        check(!changed && !missing, e->name, "every preset renders as in tests/golden_scene_engines.txt");
}

/* each EDIT value moved a third of its range away from the preset's: on some preset it changes the render (a value
 * may act only with another on: VPS's DTN needs OSC2), and on every preset tried it stays bounded */
static void t_edits(const se_eng_t *g)
{
    const engine_t *e = ENGINES[g->slot];
    uint32_t k, ok = 1;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = &e->edit[k];
        uint32_t pi, acts = 0, bounded = 1;
        for (pi = 0; pi < e->npresets && !acts; pi++) {
            int16_t set[8] = {KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP};
            int32_t v = e->presets[pi].e[k], lo = d->min, hi = d->max;
            se_res_t a = se_render(g->slot, pi, 0, CHORD, 4, 0), b;
            set[k] = (int16_t)(v - lo > hi - v ? lo + (v - lo) / 3 : hi - (hi - v) / 3);
            b = se_render(g->slot, pi, set, CHORD, 4, 0);
            acts = a.hash != b.hash;
            bounded &= b.peak <= 32767 && !b.busy;
        }
        if (!acts || !bounded) {
            printf("scene: %s: %s %s\n", e->name, d->label, !acts ? "changes no preset's sound" : "unbounded");
            ok = 0;
        }
    }
    check(ok, e->name, "each EDIT value changes the sound (on some preset), bounded");
}

static void t_corners(const se_eng_t *g)
{
    static const uint8_t NOTES[3] = {24, 60, 108};
    const engine_t *e = ENGINES[g->slot];
    uint32_t c, nt, ok = 1;
    for (c = 0; c < 4u; c++)
        for (nt = 0; nt < 3u; nt++) {
            int16_t set[8];
            uint32_t k;
            se_res_t r;
            for (k = 0; k < 8u; k++) {
                const param_desc_t *d = &e->edit[k];
                int32_t lo = d->min, hi = d->max;
                set[k] = (int16_t)(c == 0 ? lo : c == 1 ? hi : ((k + c) & 1u) ? hi : lo);
            }
            r = se_render(g->slot, 0, set, &NOTES[nt], 1, 0);
            if (r.peak > 32767 || fabs(r.dc) > 200 || r.busy || r.tail > 2) {
                printf("scene: %s corner %u note %u: peak %d dc %.1f tail %.1f busy %u\n", e->name, c, NOTES[nt], r.peak,
                       r.dc, r.tail, r.busy);
                ok = 0;
            }
        }
    check(ok, e->name, "the corners, notes 24 / 60 / 108: bounded, no DC, silent and free after the release");
}

static int32_t se_os2;                                   /* (se_direct: vmod_t.os2, OSC2's move, 1/16 semitone) */
/* the render alone: the first preset with set[] on voice 0, key 57 (220 Hz) at full amplitude, nb blocks into buf */
static void se_direct(const se_eng_t *g, const int16_t *set, int32_t *buf, uint32_t nb)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    const engine_t *e = ENGINES[g->slot];
    vmod_t m;
    uint32_t b, k;
    reset();
    host_preset(t, g->slot, 0);
    for (k = 0; set && k < 8u; k++)
        if (set[k] != KEEP)
            t->p[P_E0 + k] = set[k];
    memset(v, 0, sizeof *v);
    v->note = 57, v->vel = 100, v->active = 1, v->gate = 1, v->pitch16 = 57 * 16;
    memset(&m, 0, sizeof m);
    m.pitch16 = 57 * 16;
    m.inc = pitch_inc(m.pitch16);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    m.envq15 = 32767;
    m.os2 = se_os2;
    tables();
    e->note_on(t, v);
    memset(buf, 0, nb * CTL * sizeof buf[0]);
    for (b = 0; b < nb; b++) {
        if (e->block)
            e->block(t);
        e->render(t, v, buf + b * CTL, CTL, &m);
    }
}
#define SE_DN (FS / CTL)
static void t_direct(const se_eng_t *g)
{
    static int32_t a[SE_DN * CTL], b[SE_DN * CTL];
    const engine_t *e = ENGINES[g->slot];
    uint32_t lag, best = 0, i, n = SE_DN * CTL / 2u;
    double bc = -1e300;
    char what[96];
    se_direct(g, g->pure, a, SE_DN);
    se_direct(g, g->pure, b, SE_DN);
    check(!memcmp(a, b, sizeof a), e->name, "the same render twice the same (deterministic)");
    for (lag = 150; lag < 400u; lag++) {                 /* (110 .. 294 Hz: an octave up is no answer) */
        double c = 0, e0 = 0, e1 = 0;
        for (i = n; i < n + 8000u; i++)
            c += (double)a[i] * a[i + lag], e0 += (double)a[i] * a[i], e1 += (double)a[i + lag] * a[i + lag];
        c /= sqrt(e0 * e1) + 1;
        if (c > bc)
            bc = c, best = lag;
    }
    snprintf(what, sizeof what, "key 57 on a plain setting: the period of 220 Hz (%u samples, %.3f)", best, bc);
    check(best >= 199u && best <= 202u && bc > 0.9, e->name, what);
}

#if FELUCCA_ENG_PROPHET
/* PROPHET: OSC2 moves A's pitch alone (issue 75: a sync sweep, B staying) */
static int32_t se_period(const int32_t *a)
{
    uint32_t lag, best = 0, i, n = SE_DN * CTL / 2u;
    double bc = -1e300;
    for (lag = 60; lag < 400u; lag++) {
        double c = 0, e0 = 0, e1 = 0;
        for (i = n; i < n + 8000u; i++)
            c += (double)a[i] * a[i + lag], e0 += (double)a[i] * a[i], e1 += (double)a[i + lag] * a[i + lag];
        c /= sqrt(e0 * e1) + 1;
        if (c > bc)
            bc = c, best = lag;
    }
    return (int32_t)best;
}
static void t_prophet_osc2(void)
{
    static int32_t a[SE_DN * CTL], b[SE_DN * CTL];
    static const int16_t AONLY[8] = {0, 0, 0, -64, 127, 0, 0, 0}, BONLY[8] = {0, 0, 7, 63, 127, 0, 0, 0};
    static const int16_t SYNC[8] = {3, 0, 0, 0, 127, 0, 0, 0};
    const se_eng_t *g = 0;
    uint32_t i;
    int32_t pa, pb;
    for (i = 0; i < sizeof ENGS / sizeof ENGS[0]; i++)
        if (ENGS[i].slot == ENG_SLOT_PROPHET)
            g = &ENGS[i];
    if (!g)
        return;
    se_os2 = 0;
    se_direct(g, AONLY, a, SE_DN);
    se_os2 = 12 * 16;                                    /* (OSC2: an octave up) */
    se_direct(g, AONLY, b, SE_DN);
    pa = se_period(a), pb = se_period(b);
    check(pa >= 199 && pa <= 202 && pb >= 99 && pb <= 101, "PROPHET", "OSC2 +12: A alone an octave up (A's pitch, issue 75)");
    se_os2 = 0;
    se_direct(g, BONLY, a, SE_DN);
    se_os2 = 12 * 16;
    se_direct(g, BONLY, b, SE_DN);
    pa = se_period(a), pb = se_period(b);                /* (B a fifth up, 330 Hz: 133.6 samples; MIX 63 leaves A at 1/64) */
    check(pa >= 132 && pa <= 135 && pb == pa, "PROPHET", "OSC2 +12: B (a fifth up, MIX 63) keeps its period (B stays)");
    se_os2 = 0;
    se_direct(g, SYNC, a, SE_DN);
    se_os2 = 7 * 16;
    se_direct(g, SYNC, b, SE_DN);
    pa = se_period(a), pb = se_period(b);
    check(memcmp(a, b, sizeof a) && pa == pb, "PROPHET", "A synced to B, OSC2 +7: the timbre moves, the period stays B's (a sync sweep)");
    se_os2 = 0;
}
#endif

int main(int argc, char **argv)
{
    const char *gpath = argc > 1 ? argv[1] : "tests/golden_scene_engines.txt";
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    uint32_t i, pi;
    char line[128];
    FILE *f;
    host_tracks_init();
    f = fopen(gpath, "r");
    while (f && ng < GMAX && fgets(line, sizeof line, f)) {
        unsigned long long h;
        if (line[0] == '#' || sscanf(line, "%47s %llx", gname[ng], &h) != 2)
            continue;
        gold[ng++] = h;
    }
    if (f)
        fclose(f);
    if (upd && (gout = fopen(gpath, "w")) != 0)
        fprintf(gout, "# The scene engines' golden renders (tests/scene_engines_test.c): ENGINE_NN_PRESET, FNV-1a 64 of the\n"
                      "# output (4-note chord, 2 s held, the keys up, 10 s more, dry). Rewritten by GOLDEN_UPDATE=1 -- only for\n"
                      "# an intended change.\n");
    for (pi = 0; pi < ENGINES[ENG_SLOT_ANALOG]->npresets; pi++)
        analog_rms += se_render(ENG_SLOT_ANALOG, pi, 0, CHORD, 4, 0).rms / ENGINES[ENG_SLOT_ANALOG]->npresets;
    check(ENG_UID_N == 26u, "", "26 engine UIDs");
    for (i = 0; i < NENG; i++) {
        const se_eng_t *g = &ENGS[i];
        const engine_t *e = ENGINES[g->slot];
        check(g->slot < NENGINES && eng_uid(g->slot) == g->uid && ENG_FALLBACK[g->uid] == g->fb, e->name,
              "built, its UID and its fallback as registry.h has them");
        t_level(g);
        t_presets(g, upd);
        t_edits(g);
        t_corners(g);
        t_direct(g);
    }
#if FELUCCA_ENG_PROPHET
    t_prophet_osc2();
#endif
    if (gout) {
        fclose(gout);
        printf("scene: %s rewritten\n", gpath);
    } else {
        uint32_t stale = 0;
        for (i = 0; i < ng; i++)
            stale += !gseen[i];
        check(!stale, "", "no golden line without its preset");
    }
    printf("scene: %d failed\n", fails);
    return fails;
}
