/* SPDX-License-Identifier: GPL-3.0-only */
/* The CZ engine (FELUCCA_ENG_CZ, eng_cz.c; tests/run_tests.sh builds this with the switch on):
 *   - engine UID 13 is CZ, its fallback PHASE (registry.h);
 *   - every preset: a 4-note chord held 2 s, then 4 s of release: audible, bounded, no DC, every voice free at the
 *     end (the DCA envelopes end the voices: cz_amp), its render's FNV-1a 64 the one in tests/golden_cz.txt
 *     (GOLDEN_UPDATE=1 rewrites it; the shared tests/golden.txt cannot hold renders of an engine off by default);
 *   - the EDIT values act: DCW, W.TIM, A.TIM, DTN, LINE, MOD and VIB each change the render of INIT / a tone;
 *   - a retrigger of a sounding voice keeps its phase (no new voice, no click: the step at the note bounded).
 * env: CZ_WAV=DIR writes each preset's render to DIR/cz-NN-NAME.wav.
 *   build/host/cz_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("cz: %-86s %s\n", what, ok ? "ok" : "FAIL");
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
    drums.set = -2;
}

typedef struct { uint64_t hash; double rms, dc; int32_t peak; uint32_t busy; } cz_res_t;
static const uint8_t CHORD[4] = {48, 55, 60, 64};

/* preset pi with EDIT value k (0..7, 8 = none) set to v: the chord, 2 s held, 4 s more */
static cz_res_t cz_render_e(uint32_t eng, uint32_t pi, uint32_t k, int32_t v, FILE *wav)
{
    cz_res_t r = {1469598103934665603ull, 0, 0, 0, 0};
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
static cz_res_t cz_render(uint32_t pi, uint32_t k, int32_t v, FILE *wav) { return cz_render_e(ENG_IX_CZ, pi, k, v, wav); }

/* the level beside PHASE's presets (the same chord, dry), for information: the loudest second of each */
static void t_level(void)
{
#if FELUCCA_ENG_PHASE
    uint32_t pi;
    double p = 0, c = 0;
    for (pi = 0; pi < ENGINES[ENG_SLOT_PHASE]->npresets; pi++)
        p += cz_render_e(ENG_SLOT_PHASE, pi, 8, 0, 0).rms / ENGINES[ENG_SLOT_PHASE]->npresets;
    for (pi = 0; pi < ENGINES[ENG_IX_CZ]->npresets; pi++)
        c += cz_render(pi, 8, 0, 0).rms / ENGINES[ENG_IX_CZ]->npresets;
    printf("cz: mean rms over 6 s: PHASE presets %.0f, CZ presets %.0f (%.1f dB)\n", p, c, 20 * log10(c / p));
#endif
}

static void t_presets(const char *gpath)
{
    const engine_t *e = ENGINES[ENG_IX_CZ];
    uint32_t pi, ok = 1, n = e->npresets, changed = 0, missing = 0;
    uint64_t gold[32] = {0};
    char gname[32][48], line[128], s[48];
    uint32_t ng = 0;
    const char *wdir = getenv("CZ_WAV");
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    FILE *f = fopen(gpath, "r");
    cz_res_t res[32];
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
            snprintf(p, sizeof p, "%s/cz-%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        res[pi] = cz_render(pi, 8, 0, w);
        if (w)
            fclose(w);
        for (g = 0; g < ng && !found; g++)
            if (!strcmp(gname[g], s)) {
                found = 1;
                if (gold[g] != res[pi].hash)
                    changed++, printf("cz: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        printf("cz: %-16s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, res[pi].rms, res[pi].peak, res[pi].dc,
               (unsigned long long)res[pi].hash, res[pi].busy ? "STILL SOUNDING" : "");
        ok &= res[pi].rms > 300 && res[pi].peak <= 32767 && !res[pi].busy && res[pi].dc < 50 && res[pi].dc > -50;
        snprintf(gname[16 + pi % 16], sizeof gname[0], "%s", s);
    }
    check(ok, "every preset: audible (rms > 300), bounded, no DC, its voices free 4 s after the release");
    if (upd) {
        f = fopen(gpath, "w");
        if (f) {
            fprintf(f, "# CZ engine golden renders (tests/cz_test.c): preset, FNV-1a 64 of the output (4-note chord,\n"
                       "# 2 s held, 4 s release, dry). Rewritten by GOLDEN_UPDATE=1 -- only for an intended change.\n");
            for (pi = 0; pi < n; pi++)
                fprintf(f, "%s %016llx\n", gname[16 + pi % 16], (unsigned long long)res[pi].hash);
            fclose(f);
        }
        printf("cz: %s rewritten\n", gpath);
    } else {
        check(!changed && !missing && ng == n, "every preset renders as in tests/golden_cz.txt");
    }
}

static void t_edits(void)
{
    static const struct { uint8_t pi, k; int8_t v; const char *what; } E[] = {
        {0, 1, 40, "DCW"}, {0, 2, 40, "W.TIM"}, {0, 4, -40, "A.TIM"}, {4, 3, 30, "DTN"},
        {4, 5, 1, "LINE"}, {3, 6, 2, "MOD"}, {0, 7, 40, "VIB"}};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof E / sizeof E[0]; i++) {
        cz_res_t a = cz_render(E[i].pi, 8, 0, 0), b = cz_render(E[i].pi, E[i].k, E[i].v, 0);
        if (a.hash == b.hash || b.peak > 32767 || b.busy)
            printf("cz: EDIT %s does not act (or unbounded)\n", E[i].what), ok = 0;
    }
    check(ok, "the EDIT values DCW W.TIM A.TIM DTN LINE MOD VIB change the sound, bounded");
}

/* a sounding voice retriggered: the same voice, its phase kept, the step between blocks small */
static void t_retrigger(void)
{
    int32_t out[CTL * 2], last = 0, jump = 0;
    uint32_t b, i, nact = 0;
    reset();
    host_preset(&trk[0], ENG_IX_CZ, 4);              /* WIRE BRASS: sustains */
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
    printf("cz: retrigger: largest step between samples %d, voices %u\n", jump, nact);
    check(nact == 1u && jump < 8000, "a retrigger keeps the one voice and its phase (no click)");
}

int main(int argc, char **argv)
{
    host_tracks_init();
    check(ENG_IX_CZ < NENGINES && !strcmp(ENGINES[ENG_IX_CZ]->name, "CZ") && eng_uid(ENG_IX_CZ) == ENG_UID_CZ &&
              ENG_UID_CZ == 13u && ENG_FALLBACK[ENG_UID_CZ] == 2u,
          "engine UID 13 is CZ, its fallback PHASE");
    check(ENGINES[ENG_IX_CZ]->npresets >= 5u && !strcmp(ENGINES[ENG_IX_CZ]->presets[0].name, "CZ INIT"),
          "an INIT preset first, and our own tones");
    t_level();
    t_presets(argc > 1 ? argv[1] : "tests/golden_cz.txt");
    t_edits();
    t_retrigger();
    printf("cz: %d failed\n", fails);
    return fails;
}
