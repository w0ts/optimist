/* SPDX-License-Identifier: GPL-3.0-only */
/* The NOISE engine (FELUCCA_ENG_NOISE, engines/noise/eng_noise.c, MIT; tests/run_tests.sh builds this with the switch on):
 *   - engine UID 15 is NOISE, its fallback ANALOG (registry.h), 16 engine UIDs with JUNO's 14 reserved;
 *   - every preset: a 4-note chord held 2 s, then 4 s of release: audible, bounded, no DC, every voice free at the
 *     end, its render's FNV-1a 64 the one in tests/golden_noise.txt (GOLDEN_UPDATE=1 rewrites it);
 *   - the EDIT values act: MODE COLR FREQ RES TRK DENS DRFT CRSH each change the render of a preset;
 *   - COLR tilts the spectrum (white brighter than brown), FREQ closes the filter, the LFSR is pitched (its period
 *     follows the key), a note from silence sounds the same each time, the mode-dependent names (LEN, CLK);
 *   - a retrigger keeps its voice and its filter (no click), the voices are free after a long hold.
 * env: NOISE_WAV=DIR writes each preset's render to DIR/noise-NN-NAME.wav.
 *   build/host/noise_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("noise: %-86s %s\n", what, ok ? "ok" : "FAIL");
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

typedef struct { uint64_t hash; double rms, dc; int32_t peak; uint32_t busy; } noise_res_t;
static const uint8_t CHORD[4] = {48, 55, 60, 64};

static void dry(void) { trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0; }

/* preset pi with EDIT value k (0..7, 8 = none) set to v: the chord, 2 s held, 4 s more */
static noise_res_t nt_render_e(uint32_t eng, uint32_t pi, uint32_t k, int32_t v, FILE *wav)
{
    noise_res_t r = {1469598103934665603ull, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0;
    uint32_t b, i, nb = 6u * FS / CTL;
    reset();
    host_preset(&trk[0], eng, pi);
    dry();                                            /* dry: the engine's sound */
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
static noise_res_t nt_render(uint32_t pi, uint32_t k, int32_t v, FILE *wav) { return nt_render_e(ENG_IX_NOISE, pi, k, v, wav); }

/* the level beside ANALOG's presets (the same chord, dry): the mean RMS over the 6 s */
static void t_level(void)
{
    uint32_t pi;
    double p = 0, c = 0;
    for (pi = 0; pi < ENGINES[ENG_SLOT_ANALOG]->npresets; pi++)
        p += nt_render_e(ENG_SLOT_ANALOG, pi, 8, 0, 0).rms / ENGINES[ENG_SLOT_ANALOG]->npresets;
    for (pi = 0; pi < ENGINES[ENG_IX_NOISE]->npresets; pi++)
        c += nt_render(pi, 8, 0, 0).rms / ENGINES[ENG_IX_NOISE]->npresets;
    printf("noise: mean rms over 6 s: ANALOG presets %.0f, NOISE presets %.0f (%.1f dB)\n", p, c, 20 * log10(c / p));
    check(fabs(20 * log10(c / p)) < 9.0, "the presets' level within 9 dB of ANALOG's");
}

static void t_presets(const char *gpath)
{
    const engine_t *e = ENGINES[ENG_IX_NOISE];
    uint32_t pi, ok = 1, n = e->npresets, changed = 0, missing = 0;
    uint64_t gold[32] = {0};
    char gname[32][48], line[128], s[48];
    uint32_t ng = 0;
    const char *wdir = getenv("NOISE_WAV");
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    FILE *f = fopen(gpath, "r");
    noise_res_t res[32];
    while (f && ng < 32u && fgets(line, sizeof line, f)) {
        unsigned long long h;
        if (line[0] == '#' || sscanf(line, "%47s %llx", gname[ng], &h) != 2)
            continue;
        gold[ng++] = h;
    }
    if (f)
        fclose(f);
    for (pi = 0; pi < n && pi < 16u; pi++) {
        FILE *w = 0;
        uint32_t g, found = 0;
        snprintf(s, sizeof s, "%02u_", pi);
        for (g = 0; e->presets[pi].name[g] && g < 40u; g++)
            s[3 + g] = e->presets[pi].name[g] == ' ' ? '_' : e->presets[pi].name[g];
        s[3 + g] = 0;
        if (wdir) {
            char p[512];
            snprintf(p, sizeof p, "%s/noise-%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        res[pi] = nt_render(pi, 8, 0, w);
        if (w)
            fclose(w);
        for (g = 0; g < ng && !found; g++)
            if (!strcmp(gname[g], s)) {
                found = 1;
                if (gold[g] != res[pi].hash)
                    changed++, printf("noise: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        printf("noise: %-16s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, res[pi].rms, res[pi].peak, res[pi].dc,
               (unsigned long long)res[pi].hash, res[pi].busy ? "STILL SOUNDING" : "");
        ok &= res[pi].rms > 300 && res[pi].peak <= 32767 && !res[pi].busy && res[pi].dc < 50 && res[pi].dc > -50;
        snprintf(gname[16 + pi % 16], sizeof gname[0], "%s", s);
    }
    check(ok, "every preset: audible (rms > 300), bounded, no DC, its voices free 4 s after the release");
    if (upd) {
        f = fopen(gpath, "w");
        if (f) {
            fprintf(f, "# NOISE engine golden renders (tests/noise_test.c): preset, FNV-1a 64 of the output (4-note chord,\n"
                       "# 2 s held, 4 s release, dry). Rewritten by GOLDEN_UPDATE=1 -- only for an intended change.\n");
            for (pi = 0; pi < n; pi++)
                fprintf(f, "%s %016llx\n", gname[16 + pi % 16], (unsigned long long)res[pi].hash);
            fclose(f);
        }
        printf("noise: %s rewritten\n", gpath);
    } else {
        check(!changed && !missing && ng == n, "every preset renders as in tests/golden_noise.txt");
    }
}

/* each EDIT value changes the render of a preset in which it acts */
static void t_edits(void)
{
    static const struct { uint8_t pi, k; int8_t v; const char *what; } E[] = {
        {0, 0, 1, "MODE"}, {0, 1, 0, "COLR"}, {0, 2, 110, "FREQ"}, {0, 3, 10, "RES"},
        {0, 4, 0, "TRK"}, {1, 5, 40, "DENS"}, {0, 6, 0, "DRFT"}, {0, 7, 100, "CRSH"}};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof E / sizeof E[0]; i++) {
        noise_res_t a = nt_render(E[i].pi, 8, 0, 0), b = nt_render(E[i].pi, E[i].k, E[i].v, 0);
        if (a.hash == b.hash || b.peak > 32767 || b.busy)
            printf("noise: EDIT %s does not act (or unbounded)\n", E[i].what), ok = 0;
    }
    check(ok, "the EDIT values MODE COLR FREQ RES TRK DENS DRFT CRSH change the sound, bounded");
}

/* one note (key), a preset with EDIT overrides (e[8] < 0: the preset's own), `nb` blocks after the first 8 */
static uint32_t nt_one(uint32_t pi, const int16_t *ed, uint32_t key, int32_t *buf, uint32_t n)
{
    int32_t out[CTL * 2];
    uint32_t b, i, got = 0;
    reset();
    host_preset(&trk[0], ENG_IX_NOISE, pi);
    dry();
    trk[0].p[P_VOICE] = V_MONO;
    trk[0].p[P_ATK] = 0;
    for (i = 0; i < 8u; i++)
        if (ed && ed[i] >= 0)
            trk[0].p[P_E0 + i] = ed[i];
    trk_note_on(&trk[0], key, 100);
    for (b = 0; got < n + 4u * CTL && b < 4000u; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++, got++)
            if (got >= 4u * CTL && got - 4u * CTL < n)
                buf[got - 4u * CTL] = out[2 * i];
    }
    return n;
}

/* the mean |first difference| over the mean |x|: how bright the noise is */
static double nt_bright(const int32_t *x, uint32_t n)
{
    double d = 0, a = 0;
    uint32_t i;
    for (i = 1; i < n; i++) {
        d += fabs((double)x[i] - x[i - 1]);
        a += fabs((double)x[i]);
    }
    return a > 0 ? d / a : 0;
}

static void t_colour(void)
{
    static int32_t x[8192];
    const int16_t white[8] = {NZ_ANLG, 0, 127, 0, 0, 0, 0, 0}, brown[8] = {NZ_ANLG, 127, 127, 0, 0, 0, 0, 0};
    const int16_t closed[8] = {NZ_ANLG, 64, 40, 0, 0, 0, 0, 0}, open[8] = {NZ_ANLG, 64, 127, 0, 0, 0, 0, 0};
    double bw, bb, bc, bo;
    nt_one(0, white, 60, x, 8192);
    bw = nt_bright(x, 8192);
    nt_one(0, brown, 60, x, 8192);
    bb = nt_bright(x, 8192);
    nt_one(0, closed, 60, x, 8192);
    bc = nt_bright(x, 8192);
    nt_one(0, open, 60, x, 8192);
    bo = nt_bright(x, 8192);
    printf("noise: brightness (|dx| / |x|): white %.2f brown %.2f, FREQ 40 %.2f FREQ 127 %.2f\n", bw, bb, bc, bo);
    check(bw > 2.0 * bb, "COLR: white noise is at least twice as bright as brown");
    check(bo > 2.0 * bc, "FREQ: a closed low-pass is at least twice as dark as an open one");
}

/* the best normalised autocorrelation over the lags within 2 of `lag` (a fractional period falls between two) */
static double nt_corr(const int32_t *x, uint32_t n, uint32_t lag)
{
    double best = -1, e0 = 0;
    uint32_t l, i;
    for (i = 0; i < n - 1024u; i++)
        e0 += (double)x[i] * x[i];
    for (l = lag - 2u; l <= lag + 2u; l++) {
        double c = 0;
        for (i = 0; i < n - 1024u; i++)
            c += (double)x[i] * x[i + l];
        if (c / e0 > best)
            best = c / e0;
    }
    return best;
}

/* the LFSR with CLK 84 and a 7-bit register, and META: one cycle a period of the key. Keys 57 (220 Hz) and 52 (164.8 Hz):
 * the signal repeats at its own key's period and not at the other's (not a multiple of it) */
static void t_pitched(void)
{
    static int32_t x[16384];
    const int16_t lf[8] = {NZ_LFSR, 0, 127, 0, 127, 112, 0, 84}, me[8] = {NZ_META, 0, 127, 0, 127, 32, 0, 0};
    const uint32_t l57 = (uint32_t)(FS / 220.0 + 0.5), l52 = (uint32_t)(FS / 164.814 + 0.5);
    double a57, a52, b57, b52, c57, c52, d57, d52;
    nt_one(2, lf, 57, x, 16384);
    a57 = nt_corr(x, 16384, l57), a52 = nt_corr(x, 16384, l52);
    nt_one(2, lf, 52, x, 16384);
    b57 = nt_corr(x, 16384, l57), b52 = nt_corr(x, 16384, l52);
    nt_one(3, me, 57, x, 16384);
    c57 = nt_corr(x, 16384, l57), c52 = nt_corr(x, 16384, l52);
    nt_one(3, me, 52, x, 16384);
    d57 = nt_corr(x, 16384, l57), d52 = nt_corr(x, 16384, l52);
    printf("noise: autocorrelation at the period of key 57 (%u) / key 52 (%u): LFSR key 57 %.2f / %.2f, key 52 %.2f / %.2f;"
           " META key 57 %.2f / %.2f, key 52 %.2f / %.2f\n", l57, l52, a57, a52, b57, b52, c57, c52, d57, d52);
    check(a57 > 0.6 && a52 < 0.3 && b52 > 0.6 && b57 < 0.3, "LFSR: it repeats at the period of its key (220 Hz, 164.8 Hz) and not the other's");
    check(c57 > 0.6 && c52 < 0.4 && d52 > 0.6 && d57 < 0.4, "META: it repeats at the period of its key (220 Hz, 164.8 Hz) and not the other's");
}

/* the engine's own render of a voice started cold (the mix's master chain keeps state across runs, so not through it):
 * `blocks` blocks of the preset pi with a voice on `note` and unison detune `fine`, into buf */
static void nt_voice(uint32_t pi, uint32_t note, int32_t fine, int32_t *buf, uint32_t blocks)
{
    const engine_t *e = ENGINES[ENG_IX_NOISE];
    voice_t v;
    vmod_t m;
    int32_t out[CTL];
    uint32_t b, i;
    reset();
    host_preset(&trk[0], ENG_IX_NOISE, pi);
    memset(&v, 0, sizeof v);
    v.note = (uint8_t)note;
    v.fine = fine;
    v.pitch16 = v.pitch_cur = (int32_t)note * 16;
    e->note_on(&trk[0], &v);
    memset(&m, 0, sizeof m);
    m.pitch16 = v.pitch16;
    m.inc = PITCH_INC[m.pitch16];
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < CTL; i++)
            out[i] = 0;
        e->render(&trk[0], &v, out, CTL, &m);
        for (i = 0; i < CTL; i++)
            buf[b * CTL + i] = out[i];
    }
}

/* a note from silence sounds the same each time (the generator is seeded by the note and the unison detune) */
static void t_repeat(void)
{
    static int32_t a[4096], b[4096];
    uint32_t pi, same = 1, note_differs = 1, fine_differs = 1, i;
    for (pi = 0; pi < 4u; pi++) {                     /* all four modes (WIND has DRFT: its random walk too) */
        nt_voice(pi, 52, 0, a, 4096 / CTL);
        nt_voice(pi, 52, 0, b, 4096 / CTL);
        for (i = 0; i < 4096u; i++)
            same &= a[i] == b[i];
        nt_voice(pi, 53, 0, b, 4096 / CTL);
        for (i = 0, note_differs &= 0; i < 4096u && !note_differs; i++)
            note_differs |= a[i] != b[i];
        nt_voice(pi, 52, 40, b, 4096 / CTL);
        for (i = 0, fine_differs &= 0; i < 4096u && !fine_differs; i++)
            fine_differs |= a[i] != b[i];
        if (pi == 0 || pi == 1)                       /* (the LFSR / META renders depend on the seed less: a clock) */
            check(note_differs && fine_differs, pi ? "DUST: another note, another unison voice, another noise"
                                                    : "ANLG: another note, another unison voice, another noise");
        note_differs = fine_differs = 1;
    }
    check(same, "a note from silence renders the same each time, in every mode (seeded by the note)");
}

/* mode-dependent names: LFSR names the register LEN and the clock CLK, ANLG keeps DENS / CRSH */
static void t_desc(void)
{
    const engine_t *e = ENGINES[ENG_IX_NOISE];
    track_t *t;
    int ok;
    reset();
    host_preset(&trk[0], ENG_IX_NOISE, 0);
    t = &trk[0];
    ok = !e->desc(t, 5) && !e->desc(t, 7);
    t->p[P_E0] = NZ_LFSR;
    ok &= e->desc(t, 5) && !strcmp(e->desc(t, 5)->label, "LEN") && !strcmp(e->desc(t, 5)->names[0], "L23") &&
          e->desc(t, 7) && !strcmp(e->desc(t, 7)->label, "CLK") && !e->desc(t, 1);
    t->p[P_E0] = NZ_META;
    ok &= e->desc(t, 5) && !strcmp(e->desc(t, 5)->names[0], "127");
    check(ok, "ANLG: DENS / CRSH as edit[]; LFSR: LEN (L23..) and CLK; META: LEN (127..)");
}

/* a sounding voice retriggered: the same voice, its filter and phases kept, the step between samples small */
static void t_retrigger(void)
{
    int32_t out[CTL * 2], last = 0, jump = 0;
    uint32_t b, i, nact = 0;
    reset();
    host_preset(&trk[0], ENG_IX_NOISE, 0);           /* WIND: sustains */
    dry();
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
    printf("noise: retrigger: largest step between samples %d, voices %u\n", jump, nact);
    check(nact == 1u && jump < 12000, "a retrigger keeps the one voice (and its filter: no click)");
}

/* eight keys held for a second, then released: every voice free, bounded */
static void t_poly(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i, nact = 0, pk = 0;
    reset();
    host_preset(&trk[0], ENG_IX_NOISE, 3);
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 3 * i, 127);
    for (b = 0; b < FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            pk = (uint32_t)(out[i] < 0 ? -out[i] : out[i]) > pk ? (uint32_t)(out[i] < 0 ? -out[i] : out[i]) : pk;
    }
    for (i = 0; i < 8u; i++)
        trk_note_off(&trk[0], 48 + 3 * i);
    for (b = 0; b < 6u * FS / CTL; b++)
        mix_block(out, CTL);
    for (i = 0; i < NVOICE; i++)
        nact += trk[0].v[i].active;
    printf("noise: 8 keys held 1 s: peak %u, voices after the release %u\n", pk, nact);
    check(pk <= 32767u && nact == 0u, "eight keys: bounded, every voice free after the release");
}

int main(int argc, char **argv)
{
    host_tracks_init();
    check(ENG_IX_NOISE < NENGINES && !strcmp(ENGINES[ENG_IX_NOISE]->name, "NOISE") && eng_uid(ENG_IX_NOISE) == ENG_UID_NOISE &&
              ENG_UID_NOISE == 15u && ENG_UID_N == 16u && ENG_FALLBACK[ENG_UID_NOISE] == 0u,
          "engine UID 15 is NOISE, its fallback ANALOG, 16 engine UIDs (14 is JUNO's)");
    check(ENGINES[ENG_IX_NOISE]->npresets >= 4u && !strcmp(ENGINES[ENG_IX_NOISE]->presets[0].name, "WIND"),
          "the presets WIND RAIN ARCADE METAL");
    t_level();
    t_presets(argc > 1 ? argv[1] : "tests/golden_noise.txt");
    t_edits();
    t_colour();
    t_pitched();
    t_repeat();
    t_desc();
    t_retrigger();
    t_poly();
    printf("noise: %d failed\n", fails);
    return fails;
}
