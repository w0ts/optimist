/* SPDX-License-Identifier: GPL-3.0-only */
/* The PLAITS engine (FELUCCA_ENG_PLAITS, engines/plaits/eng_plaits.c: four of Plaits' models; tests/run_tests.sh builds
 * this with the switch on):
 *   - engine UID 17 is PLAITS, its fallback ANALOG (registry.h), 28 engine UIDs;
 *   - every preset: a 4-note chord held 2 s, the keys up, 6 s more: audible, bounded, no DC, every voice free at the
 *     end, its render's FNV-1a 64 the one in tests/golden_plaits.txt (GOLDEN_UPDATE=1 rewrites it); the presets'
 *     level within 9 dB of ANALOG's;
 *   - each model (WAVSH ADDTV GRFMT PRTCL): HARM TIMB MORPH AUX TONE LPG VEL each change the render, bounded; the
 *     corners (every value 0, every value 127) on a low and a high note: bounded, no DC run-away, quiet after the
 *     release; the same render twice the same (deterministic);
 *   - MODEL changed under a sounding note: bounded, the new model heard; 8 keys: 4 voices (the cap).
 * env: PLAITS_WAV=DIR writes each preset's render to DIR/plaits-NN-NAME.wav.
 *   build/host/plaits_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("plaits: %-86s %s\n", what, ok ? "ok" : "FAIL");
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

typedef struct { uint64_t hash; double rms, dc, tail; int32_t peak; uint32_t busy; } pl_res_t;
static const uint8_t CHORD[4] = {48, 55, 60, 64};

static void dry(void) { trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0; }
static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

/* preset pi of engine eng, the EDIT values set[k] (-1: the preset's), nn notes from notes[]: held 2 s, the keys up,
 * 6 s more; tail: the RMS of the last second */
static pl_res_t pl_render_x(uint32_t eng, uint32_t pi, const int16_t *set, const uint8_t *notes, uint32_t nn, FILE *wav)
{
    pl_res_t r = {1469598103934665603ull, 0, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0, tl = 0;
    uint32_t b, i, nb = 8u * FS / CTL;
    reset();
    host_preset(&trk[0], eng, pi);
    dry();
    for (i = 0; set && i < 8u; i++)
        if (set[i] >= 0)
            trk[0].p[P_E0 + i] = set[i];
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
static pl_res_t pl_render(uint32_t pi, const int16_t *set, FILE *wav) { return pl_render_x(ENG_IX_PLAITS, pi, set, CHORD, 4, wav); }

static void t_level(void)
{
    uint32_t pi, m;
    double p = 0, c = 0, mm[4] = {0}, mn[4] = {0};
    const engine_t *e = ENGINES[ENG_IX_PLAITS];
    for (pi = 0; pi < ENGINES[ENG_SLOT_ANALOG]->npresets; pi++)
        p += pl_render_x(ENG_SLOT_ANALOG, pi, 0, CHORD, 4, 0).rms / ENGINES[ENG_SLOT_ANALOG]->npresets;
    for (pi = 0; pi < e->npresets; pi++) {
        double r = pl_render(pi, 0, 0).rms;
        c += r / e->npresets;
        mm[e->presets[pi].e[0] & 3] += r, mn[e->presets[pi].e[0] & 3] += 1;
    }
    printf("plaits: mean rms over 8 s: ANALOG presets %.0f, PLAITS presets %.0f (%.1f dB)", p, c, 20 * log10(c / p));
    for (m = 0; m < 4u; m++)
        printf(", model %u %.1f dB", m, mn[m] ? 20 * log10(mm[m] / mn[m] / p) : 0.0);
    printf("\n");
    check(fabs(20 * log10(c / p)) < 9.0, "the presets' level within 9 dB of ANALOG's");
}

static void t_presets(const char *gpath)
{
    const engine_t *e = ENGINES[ENG_IX_PLAITS];
    uint32_t pi, ok = 1, n = e->npresets, changed = 0, missing = 0, ng = 0;
    uint64_t gold[32] = {0};
    char gname[32][48], line[128], s[48], names[32][48];
    const char *wdir = getenv("PLAITS_WAV");
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    FILE *f = fopen(gpath, "r");
    pl_res_t res[32];
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
        snprintf(names[pi], sizeof names[pi], "%s", s);
        if (wdir) {
            char p[512];
            snprintf(p, sizeof p, "%s/plaits-%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        res[pi] = pl_render(pi, 0, w);
        if (w)
            fclose(w);
        for (g = 0; g < ng && !found; g++)
            if (!strcmp(gname[g], s)) {
                found = 1;
                if (gold[g] != res[pi].hash)
                    changed++, printf("plaits: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        printf("plaits: %-16s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, res[pi].rms, res[pi].peak, res[pi].dc,
               (unsigned long long)res[pi].hash, res[pi].busy ? "STILL SOUNDING" : "");
        ok &= res[pi].rms > 300 && res[pi].peak <= 32767 && !res[pi].busy && res[pi].dc < 50 && res[pi].dc > -50;
    }
    check(ok, "every preset: audible (rms > 300), bounded, no DC, its voices free 6 s after the release");
    if (upd) {
        f = fopen(gpath, "w");
        if (f) {
            fprintf(f, "# PLAITS engine golden renders (tests/plaits_test.c): preset, FNV-1a 64 of the output (4-note chord,\n"
                       "# 2 s held, the keys up, 6 s more, dry). Rewritten by GOLDEN_UPDATE=1 -- only for an intended change.\n");
            for (pi = 0; pi < n; pi++)
                fprintf(f, "%s %016llx\n", names[pi], (unsigned long long)res[pi].hash);
            fclose(f);
        }
        printf("plaits: %s rewritten\n", gpath);
    } else {
        check(!changed && !missing && ng == n, "every preset renders as in tests/golden_plaits.txt");
    }
}

/* the first preset of model md */
static uint32_t pl_first(uint32_t md)
{
    const engine_t *e = ENGINES[ENG_IX_PLAITS];
    uint32_t pi;
    for (pi = 0; pi < e->npresets; pi++)
        if ((uint32_t)e->presets[pi].e[0] == md)
            return pi;
    return 0;
}

static void t_edits(void)
{
    static const char *const NM[8] = {"MODEL", "HARM", "TIMB", "MORPH", "AUX", "TONE", "LPG", "VEL"};
    uint32_t md, k, ok = 1;
    for (md = 0; md < 4u; md++) {
        uint32_t pi = pl_first(md);
        pl_res_t a = pl_render(pi, 0, 0);
        for (k = 1; k < 8u; k++) {
            int16_t set[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
            pl_res_t b;
            int32_t v = ENGINES[ENG_IX_PLAITS]->presets[pi].e[k];
            set[k] = (int16_t)(v > 63 ? v - 50 : v + 50);
            if (k == 5u && v < 100)
                set[k] = 127;
            b = pl_render(pi, set, 0);
            if (a.hash == b.hash || b.peak > 32767 || b.busy) {
                printf("plaits: model %u: %s does not act (or unbounded)\n", md, NM[k]);
                ok = 0;
            }
        }
    }
    check(ok, "each model: HARM TIMB MORPH AUX TONE LPG VEL change the sound, bounded");
}

/* the corners: every value 0, every value 127 (and HARM / MORPH / TIMB mixed), a low and a high note */
static void t_corners(void)
{
    static const uint8_t LO[1] = {24}, HI[1] = {108}, MID[1] = {60};
    static const int16_t V[4][7] = {{0, 0, 0, 0, 0, 0, 0}, {127, 127, 127, 127, 127, 127, 127},
                                    {127, 0, 127, 0, 127, 0, 127}, {0, 127, 0, 64, 0, 127, 0}};
    uint32_t md, c, nt, ok = 1;
    for (md = 0; md < 4u; md++)
        for (c = 0; c < 4u; c++)
            for (nt = 0; nt < 3u; nt++) {
                int16_t set[8];
                uint32_t k;
                pl_res_t r;
                const uint8_t *nn = nt == 0 ? LO : nt == 1 ? HI : MID;
                set[0] = (int16_t)md;
                for (k = 1; k < 8u; k++)
                    set[k] = V[c][k - 1u];
                if (set[5] == 0)
                    set[5] = 64;                         /* (TONE 0 and LPG: inaudible, still checked bounded) */
                r = pl_render_x(ENG_IX_PLAITS, pl_first(md), set, nn, 1, 0);
                if (r.peak > 32767 || fabs(r.dc) > 200 || r.busy || r.tail > 2) {
                    printf("plaits: model %u corner %u note %u: peak %d dc %.1f tail %.1f busy %u\n", md, c, nn[0], r.peak,
                           r.dc, r.tail, r.busy);
                    ok = 0;
                }
            }
    check(ok, "every model at the corners, notes 24 / 60 / 108: bounded, no DC, silent and free after the release");
}

/* the render alone (the mix after it has a dither of its own): model md's first preset on voice 0, key 57 (220 Hz)
 * at full amplitude, nb blocks into buf (mono, the voice's own scale) */
static void pl_direct(uint32_t md, const int16_t *set, int32_t *buf, uint32_t nb)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    vmod_t m;
    uint32_t b, k;
    reset();
    host_preset(t, ENG_IX_PLAITS, pl_first(md));
    for (k = 0; set && k < 8u; k++)
        if (set[k] >= 0)
            t->p[P_E0 + k] = set[k];
    memset(v, 0, sizeof *v);
    v->note = 57, v->vel = 100, v->active = 1, v->gate = 1;
    memset(&m, 0, sizeof m);
    m.pitch16 = 57 * 16;
    m.inc = pitch_inc(m.pitch16);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;
    m.envq15 = 32767;
    ENGINES[ENG_IX_PLAITS]->note_on(t, v);
    memset(buf, 0, nb * CTL * sizeof buf[0]);
    for (b = 0; b < nb; b++)
        ENGINES[ENG_IX_PLAITS]->render(t, v, buf + b * CTL, CTL, &m);
}
#define PL_DN (FS / CTL)
static void t_direct(void)
{
    static int32_t a[PL_DN * CTL], b[PL_DN * CTL];
    uint32_t md, ok = 1, pok = 1;
    for (md = 0; md < 4u; md++) {
        pl_direct(md, 0, a, PL_DN);
        pl_direct(md, 0, b, PL_DN);
        ok &= !memcmp(a, b, sizeof a);
    }
    check(ok, "every model: the same render twice the same (deterministic)");
    for (md = 0; md < 3u; md++) {                       /* the pitched models: their period the key's (220 Hz) */
        static const int16_t PURE[3][8] = {{0, 64, 20, 0, 0, 127, 0, 0}, {1, 0, 0, 0, 0, 127, 0, 0}, {2, 64, 64, 30, 0, 127, 0, 0}};
        uint32_t lag, best = 0, i, n = PL_DN * CTL / 2u;
        double bc = -1e300;
        pl_direct(md, PURE[md], a, PL_DN);
        for (lag = 100; lag < 400u; lag++) {             /* (110 .. 441 Hz) */
            double c = 0, e0 = 0, e1 = 0;
            for (i = n; i < n + 8000u; i++)
                c += (double)a[i] * a[i + lag], e0 += (double)a[i] * a[i], e1 += (double)a[i + lag] * a[i + lag];
            c /= sqrt(e0 * e1) + 1;
            if (c > bc)
                bc = c, best = lag;
        }
        printf("plaits: model %u: period %u samples (%.1f Hz), correlation %.3f\n", md, best, (double)FS / best, bc);
        pok &= best >= 199u && best <= 202u && bc > 0.9;
    }
    check(pok, "WAVSH ADDTV GRFMT on key 57: the period of 220 Hz");
}

static void t_switch(void)
{
    int32_t out[CTL * 2];
    uint32_t b, j, md, ok = 1;
    for (md = 0; md < 4u; md++) {
        double a = 0;
        int32_t pk = 0;
        reset();
        host_preset(&trk[0], ENG_IX_PLAITS, pl_first(md));
        dry();
        trk_note_on(&trk[0], 60, 100);
        for (b = 0; b < FS / CTL / 4u; b++)
            mix_block(out, CTL);
        trk[0].p[P_E0] = (int16_t)((md + 1u) % 4u);
        for (b = 0; b < FS / CTL / 2u; b++) {
            mix_block(out, CTL);
            for (j = 0; j < 2u * CTL; j++) {
                a += fabs((double)out[j]);
                pk = abs(out[j]) > pk ? abs(out[j]) : pk;
            }
        }
        ok &= a > 1000 && pk <= 32767;
    }
    check(ok, "MODEL changed under a sounding note: the next model heard, bounded");
}

static void t_poly(void)
{
    uint32_t i, pk = 0;
    int32_t out[CTL * 2];
    reset();
    host_preset(&trk[0], ENG_IX_PLAITS, pl_first(1));
    dry();
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 3u * i, 127);
    for (i = 0; i < FS / CTL; i++) {
        uint32_t j;
        mix_block(out, CTL);
        for (j = 0; j < 2u * CTL; j++)
            pk = (uint32_t)abs(out[j]) > pk ? (uint32_t)abs(out[j]) : pk;
    }
    printf("plaits: 8 keys: %u voices, peak %u\n", active(&trk[0]), pk);
    check(active(&trk[0]) == 4u && pk <= 32767u, "8 keys: 4 voices (the cap), bounded");
}

int main(int argc, char **argv)
{
    host_tracks_init();
    check(ENG_IX_PLAITS < NENGINES && !strcmp(ENGINES[ENG_IX_PLAITS]->name, "PLAITS") && eng_uid(ENG_IX_PLAITS) == ENG_UID_PLAITS &&
              ENG_UID_PLAITS == 17u && ENG_UID_N == 28u && ENG_FALLBACK[ENG_UID_PLAITS] == 0u,
          "engine UID 17 is PLAITS, its fallback ANALOG, 28 engine UIDs");
    t_level();
    t_presets(argc > 1 ? argv[1] : "tests/golden_plaits.txt");
    t_edits();
    t_corners();
    t_direct();
    t_switch();
    t_poly();
    printf("plaits: %d failed\n", fails);
    return fails;
}
