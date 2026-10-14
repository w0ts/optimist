/* SPDX-License-Identifier: GPL-3.0-only */
/* The JUNO engine (FELUCCA_ENG_JUNO, eng_juno.c; tests/run_tests.sh builds this with the switch on):
 *   - engine UID 14 is JUNO, its fallback ANALOG (registry.h);
 *   - every preset: a 4-note chord held 2 s, then 4 s of release: audible, bounded, no DC, every voice free at the
 *     end, its render's FNV-1a 64 the one in tests/golden_juno.txt (GOLDEN_UPDATE=1 rewrites it);
 *   - the level beside ANALOG's presets (printed, and within 6 dB);
 *   - the EDIT values act: WAVE PW SUB NOIS CUT RES KEY HPF each change the render of a preset;
 *   - six voices at most (the Juno-106's), a retrigger keeps its voice and phase (no click).
 * env: JUNO_WAV=DIR writes each preset's render to DIR/juno-NN-NAME.wav.
 *   build/host/juno_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("juno: %-86s %s\n", what, ok ? "ok" : "FAIL");
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

typedef struct { uint64_t hash; double rms, dc; int32_t peak; uint32_t busy; } juno_res_t;
static const uint8_t CHORD[4] = {48, 55, 60, 64};

/* preset pi with EDIT value k (0..7, 8 = none) set to v: the chord, 2 s held, 4 s more */
static juno_res_t jt_render_e(uint32_t eng, uint32_t pi, uint32_t k, int32_t v, FILE *wav)
{
    juno_res_t r = {1469598103934665603ull, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0;
    uint32_t b, i, nb = 6u * FS / CTL;
    reset();
    host_preset(&trk[0], eng, pi);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* dry: the engine's sound */
    if (k < 8u)
        trk[0].p[P_E0 + k] = (int16_t)v;
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], CHORD[i], 100);
    if (wav)
        wav_hdr(wav, nb * CTL);
    for (b = 0; b < nb; b++) {
        if (b == 2u * FS / CTL)
            for (i = 0; i < 4u; i++)
                trk_note_off(&trk[0], CHORD[i]);
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            acc += (double)out[i] * out[i];
            sum += out[i];
            if (a > r.peak)
                r.peak = a;
            r.hash = (r.hash ^ (uint32_t)out[i]) * 1099511628211ull;
        }
        if (wav)
            for (i = 0; i < CTL; i++)
                wav_put(wav, out[2 * i], out[2 * i + 1]);
    }
    for (i = 0; i < NVOICE; i++)
        r.busy |= trk[0].v[i].active;
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    r.dc = sum / (2.0 * nb * CTL);
    return r;
}
static juno_res_t jt_render(uint32_t pi, uint32_t k, int32_t v, FILE *wav) { return jt_render_e(ENG_IX_JUNO, pi, k, v, wav); }

/* the level beside ANALOG's presets (the same chord, dry): the mean RMS over the 6 s */
static void t_level(void)
{
    uint32_t pi;
    double p = 0, c = 0;
    for (pi = 0; pi < ENGINES[ENG_SLOT_ANALOG]->npresets; pi++)
        p += jt_render_e(ENG_SLOT_ANALOG, pi, 8, 0, 0).rms / ENGINES[ENG_SLOT_ANALOG]->npresets;
    for (pi = 0; pi < ENGINES[ENG_IX_JUNO]->npresets; pi++)
        c += jt_render(pi, 8, 0, 0).rms / ENGINES[ENG_IX_JUNO]->npresets;
    printf("juno: mean rms over 6 s: ANALOG presets %.0f, JUNO presets %.0f (%.1f dB)\n", p, c, 20 * log10(c / p));
    check(fabs(20 * log10(c / p)) < 6.0, "the presets' level within 6 dB of ANALOG's");
}

static void t_presets(const char *gpath)
{
    const engine_t *e = ENGINES[ENG_IX_JUNO];
    uint32_t pi, ok = 1, n = e->npresets, changed = 0, missing = 0;
    uint64_t gold[32] = {0};
    char gname[32][48], line[128], s[48];
    uint32_t ng = 0;
    const char *wdir = getenv("JUNO_WAV");
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    FILE *f = fopen(gpath, "r");
    juno_res_t res[32];
    while (f && ng < 32u && fgets(line, sizeof line, f)) {
        unsigned long long h;
        if (line[0] == '#' || sscanf(line, "%47s %llx", gname[ng], &h) != 2)
            continue;
        gold[ng++] = h;
    }
    if (f)
        fclose(f);
    for (pi = 0; pi < n && pi < 32u; pi++) {
        FILE *w = 0;
        uint32_t g, found = 0;
        snprintf(s, sizeof s, "%02u_", pi);
        for (g = 0; e->presets[pi].name[g] && g < 40u; g++)
            s[3 + g] = e->presets[pi].name[g] == ' ' ? '_' : e->presets[pi].name[g];
        s[3 + g] = 0;
        if (wdir) {
            char p[512];
            snprintf(p, sizeof p, "%s/juno-%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        res[pi] = jt_render(pi, 8, 0, w);
        if (w)
            fclose(w);
        for (g = 0; g < ng && !found; g++)
            if (!strcmp(gname[g], s)) {
                found = 1;
                if (gold[g] != res[pi].hash)
                    changed++, printf("juno: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        printf("juno: %-16s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, res[pi].rms, res[pi].peak, res[pi].dc,
               (unsigned long long)res[pi].hash, res[pi].busy ? "STILL SOUNDING" : "");
        ok &= res[pi].rms > 300 && res[pi].peak <= 32767 && !res[pi].busy && res[pi].dc < 50 && res[pi].dc > -50;
        snprintf(gname[16 + pi % 16], sizeof gname[0], "%s", s);
    }
    check(ok, "every preset: audible (rms > 300), bounded, no DC, its voices free 4 s after the release");
    if (upd) {
        f = fopen(gpath, "w");
        if (f) {
            fprintf(f, "# JUNO engine golden renders (tests/juno_test.c): preset, FNV-1a 64 of the output (4-note chord,\n"
                       "# 2 s held, 4 s release, dry). Rewritten by GOLDEN_UPDATE=1 -- only for an intended change.\n");
            for (pi = 0; pi < n; pi++)
                fprintf(f, "%s %016llx\n", gname[16 + pi % 16], (unsigned long long)res[pi].hash);
            fclose(f);
        }
        printf("juno: %s rewritten\n", gpath);
    } else {
        check(!changed && !missing && ng == n, "every preset renders as in tests/golden_juno.txt");
    }
}

static void t_edits(void)
{
    static const struct { uint8_t pi, k; int8_t v; const char *what; } E[] = {
        {0, 0, 3, "WAVE"}, {2, 1, 90, "PW"}, {0, 2, 100, "SUB"}, {0, 3, 80, "NOIS"},
        {0, 4, 40, "CUT"}, {1, 5, 110, "RES"}, {1, 6, 127, "KEY"}, {0, 7, 3, "HPF"}};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof E / sizeof E[0]; i++) {
        juno_res_t a = jt_render(E[i].pi, 8, 0, 0), b = jt_render(E[i].pi, E[i].k, E[i].v, 0);
        if (a.hash == b.hash || b.peak > 32767 || b.busy)
            printf("juno: EDIT %s does not act (or unbounded)\n", E[i].what), ok = 0;
    }
    check(ok, "the EDIT values WAVE PW SUB NOIS CUT RES KEY HPF change the sound, bounded");
}

/* eight keys held: six voices sound (the cap) */
static void t_poly(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i, nact = 0;
    reset();
    host_preset(&trk[0], ENG_IX_JUNO, 1);
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (b = 0; b < 20u; b++)
        mix_block(out, CTL);
    for (i = 0; i < NVOICE; i++)
        nact += trk[0].v[i].active && trk[0].v[i].stage != 4u;
    printf("juno: 8 keys held: %u voices\n", nact);
    check(nact == 6u, "six voices at most");
}

/* VOICES (P_VOICES): AUTO = the engine's six, 8 asked = still six, 3 = three; a mono mode is still one */
static uint32_t held_voices(uint32_t vcs, uint32_t mode)
{
    int32_t out[CTL * 2];
    uint32_t b, i, n = 0;
    reset();
    host_preset(&trk[0], ENG_IX_JUNO, 1);
    trk[0].p[P_VOICES] = (int16_t)vcs;
    trk[0].p[P_VOICE] = (int16_t)mode;
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 100);
    for (b = 0; b < 20u; b++)
        mix_block(out, CTL);
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active && trk[0].v[i].stage != 4u;
    return n;
}
static void t_voices(void)
{
    check(held_voices(0, V_POLY) == 6u, "VOICES AUTO: JUNO's own six");
    check(held_voices(8, V_POLY) == 6u, "VOICES 8: capped at the engine's six");
    check(held_voices(3, V_POLY) == 3u, "VOICES 3: three voices");
    check(held_voices(1, V_POLY) == 1u, "VOICES 1: one voice");
    check(held_voices(5, V_MONO) == 1u, "VOICES 5 in MONO: still one");
    check(trk_nvoice(&trk[0]) == 5u, "trk_nvoice: min(VOICES, the engine's cap)");
}

/* a sounding voice retriggered: the same voice, its phase kept, the step between blocks small */
static void t_retrigger(void)
{
    int32_t out[CTL * 2], last = 0, jump = 0;
    uint32_t b, i, nact = 0;
    reset();
    host_preset(&trk[0], ENG_IX_JUNO, 1);            /* JUNO STRNG: sustains */
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_VOICE] = V_MONO;
    trk_note_on(&trk[0], 57, 100);
    for (b = 0; b < FS / CTL; b++) {
        if (b == FS / (2u * CTL))
            trk_note_on(&trk[0], 57, 100);           /* the same key again, mid-note */
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t d = out[2 * i] - last;
            d = d < 0 ? -d : d;
            if (b > FS / (4u * CTL) && d > jump)
                jump = d;
            last = out[2 * i];
        }
    }
    for (i = 0; i < NVOICE; i++)
        nact += trk[0].v[i].active;
    printf("juno: retrigger: largest step between samples %d, voices %u\n", jump, nact);
    check(nact == 1u && jump < 8000, "a retrigger keeps the one voice and its phase (no click)");
}

int main(int argc, char **argv)
{
    host_tracks_init();
    check(ENG_IX_JUNO < NENGINES && !strcmp(ENGINES[ENG_IX_JUNO]->name, "JUNO") && eng_uid(ENG_IX_JUNO) == ENG_UID_JUNO &&
              ENG_UID_JUNO == 14u && ENG_FALLBACK[ENG_UID_JUNO] == 0u,
          "engine UID 14 is JUNO, its fallback ANALOG");
    check(ENGINES[ENG_IX_JUNO]->npresets >= 5u && !strcmp(ENGINES[ENG_IX_JUNO]->presets[0].name, "JUNO INIT"),
          "an INIT preset first, and our own patches");
    t_level();
    t_presets(argc > 1 ? argv[1] : "tests/golden_juno.txt");
    t_edits();
    t_poly();
    t_voices();
    t_retrigger();
    printf("juno: %d failed\n", fails);
    return fails;
}
