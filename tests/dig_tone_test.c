/* SPDX-License-Identifier: GPL-3.0-only */
/* DIGITAL's TONE routings (FELUCCA_DIG_TONE=1, engines/digital/eng_digital.c; tests/run_tests.sh builds this with it):
 *   - the TONE presets: a 4-note chord held 2 s, the keys up, 10 s more: audible, bounded, no DC, silent after;
 *   - the eight routings T1..T8 sound eight ways on the same values, each bounded;
 *   - each EDIT value 2..8 changes the sound of a TONE preset, bounded;
 *   - the corners (every value at its least, at its most, two mixes) on T1..T8, notes 24 / 60 / 108: bounded, no DC run-away,
 *     silent and free after;
 *   - the names: TONE's (C A B A LEV B LEV FDBK MIX) on T1..T8, "-" for EDIT 8 on the classic routings;
 *   - a plain setting (A LEV, B LEV and FDBK 0, MIX all X, C at 1): key 57 sounds 220 Hz.
 * (The classic routings' renders are tests/golden.txt's: tests/regress.c built with FELUCCA_DIG_TONE=1 changes none.)
 *   build/host/dig_tone_test
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("dig_tone: %-100s %s\n", what, ok ? "ok" : "FAIL");
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

typedef struct { uint64_t hash; double rms, dc, tail; int32_t peak; uint32_t busy, zc; } dt_res_t;
#define KEEP INT16_MIN               /* (an EDIT value left as the preset has it) */
static const uint8_t CHORD[4] = {48, 55, 60, 64};
static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

/* DIGITAL's preset pi, the EDIT values set[k] (KEEP: the preset's), nn notes: held 2 s, up, 10 s more; zc: the left
 * channel's rising zero crossings in the second second */
static dt_res_t dt_render(uint32_t pi, const int16_t *set, const uint8_t *notes, uint32_t nn)
{
    dt_res_t r = {1469598103934665603ull, 0, 0, 0, 0, 0, 0};
    int32_t out[CTL * 2], last = 0;
    double acc = 0, sum = 0, tl = 0;
    uint32_t b, i, nb = 12u * FS / CTL;
    reset();
    host_preset(&trk[0], ENG_SLOT_DIGITAL, pi);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (i = 0; set && i < 8u; i++)
        if (set[i] != KEEP)
            trk[0].p[P_E0 + i] = set[i];
    for (i = 0; i < nn; i++)
        trk_note_on(&trk[0], notes[i], 100);
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
            if (!(i & 1u)) {
                if (b >= FS / CTL && b < 2u * FS / CTL && last < 0 && out[i] >= 0)
                    r.zc++;
                last = out[i];
            }
        }
    }
    r.busy = active(&trk[0]);
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    r.dc = sum / (2.0 * nb * CTL);
    r.tail = sqrt(tl / (2.0 * FS));
    return r;
}

static const engine_t *dig(void) { return ENGINES[ENG_SLOT_DIGITAL]; }
static uint32_t first_tone(void)
{
    uint32_t pi;
    for (pi = 0; pi < dig()->npresets; pi++)
        if (dig()->presets[pi].e[0] >= 8)
            return pi;
    return 0;
}

static void t_presets(void)
{
    uint32_t pi, n = 0, ok = 1;
    for (pi = 0; pi < dig()->npresets; pi++) {
        dt_res_t r;
        if (dig()->presets[pi].e[0] < 8)
            continue;
        r = dt_render(pi, 0, CHORD, 4);
        printf("dig_tone: %-12s rms %6.0f peak %5d dc %6.1f %s\n", dig()->presets[pi].name, r.rms, r.peak, r.dc,
               r.busy ? "STILL SOUNDING" : "");
        ok &= (r.rms > 300 || r.peak > 2000) && r.peak <= 32767 && (!r.busy || r.tail < 2) && r.dc < 50 && r.dc > -50;
        n++;
    }
    check(n == 6u && ok, "the 6 TONE presets: audible, bounded, no DC, silent 10 s after the release");
}

static void t_routings(void)
{
    uint64_t h[8];
    uint32_t a, b, ok = 1, distinct = 1;
    for (a = 0; a < 8u; a++) {
        int16_t set[8] = {(int16_t)(8 + a), 1, 2, 3, 80, 80, 40, 0};
        dt_res_t r = dt_render(first_tone(), set, CHORD, 4);
        h[a] = r.hash;
        ok &= r.peak <= 32767 && r.rms > 300 && !r.busy;
    }
    for (a = 0; a < 8u; a++)
        for (b = a + 1u; b < 8u; b++)
            distinct &= h[a] != h[b];
    check(ok && distinct, "T1..T8: eight different sounds on the same values, each audible and bounded");
}

static void t_edits(void)
{
    uint32_t k, ok = 1;
    for (k = 1; k < 8u; k++) {
        const param_desc_t *d = dig()->desc(&(track_t){.p = {[P_E0] = 8}}, k);
        uint32_t pi, acts = 0, bounded = 1;
        for (pi = first_tone(); pi < dig()->npresets && !acts; pi++) {
            int16_t set[8] = {KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP};
            int32_t v = dig()->presets[pi].e[k], lo = d->min, hi = d->max;
            dt_res_t a = dt_render(pi, 0, CHORD, 4), b;
            set[k] = (int16_t)(v - lo > hi - v ? lo + (v - lo) / 3 : hi - (hi - v) / 3);
            b = dt_render(pi, set, CHORD, 4);
            acts = a.hash != b.hash;
            bounded &= b.peak <= 32767 && !b.busy;
        }
        if (!acts || !bounded) {
            printf("dig_tone: %s %s\n", d->label, !acts ? "changes no TONE preset's sound" : "unbounded");
            ok = 0;
        }
    }
    check(ok, "each TONE value (C A B A LEV B LEV FDBK MIX) changes the sound, bounded");
}

static void t_corners(void)
{
    static const uint8_t NOTES[3] = {24, 60, 108};
    uint32_t alg, c, nt, ok = 1;
    for (alg = 0; alg < 8u; alg++)
        for (c = 0; c < 4u; c++)
            for (nt = 0; nt < 3u; nt++) {
                int16_t set[8];
                uint32_t k;
                dt_res_t r;
                for (k = 1; k < 8u; k++) {
                    const param_desc_t *d = dig()->desc(&(track_t){.p = {[P_E0] = 8}}, k);
                    int32_t lo = d->min, hi = d->max;
                    set[k] = (int16_t)(c == 0 ? lo : c == 1 ? hi : ((k + c) & 1u) ? hi : lo);
                }
                set[0] = (int16_t)(8 + alg);
                r = dt_render(first_tone(), set, &NOTES[nt], 1);
                if (r.peak > 32767 || fabs(r.dc) > 200 || r.busy || r.tail > 2) {
                    printf("dig_tone: T%u corner %u note %u: peak %d dc %.1f tail %.1f busy %u\n", alg + 1u, c, NOTES[nt],
                           r.peak, r.dc, r.tail, r.busy);
                    ok = 0;
                }
            }
    check(ok, "the corners on T1..T8, notes 24 / 60 / 108: bounded, no DC, silent and free after the release");
}

static void t_names(void)
{
    static const char *const T[7] = {"C", "A", "B", "A LEV", "B LEV", "FDBK", "MIX"};
    track_t t = {.p = {[P_E0] = 8}};
    uint32_t k, ok = dig()->desc(&t, 0) == 0;
    for (k = 1; k < 8u; k++) {
        const param_desc_t *d = dig()->desc(&t, k);
        ok &= d && !strcmp(d->label, T[k - 1u]) && d->min == dig()->edit[k].min && d->max == dig()->edit[k].max;
    }
    t.p[P_E0] = 3;
    ok &= dig()->desc(&t, 1) == 0 && dig()->desc(&t, 7) && !strcmp(dig()->desc(&t, 7)->label, "-");
    ok &= dig()->edit[0].max == 15 && !strcmp(pd_names(&dig()->edit[0])[8], "T1") && !strcmp(pd_names(&dig()->edit[0])[15], "T8");
    check(ok, "the names: ALG 1..8 and T1..T8; on T1..T8 EDIT 2..8 are C A B A LEV B LEV FDBK MIX (the same ranges); "
              "classic EDIT 8 is \"-\"");
}

static void t_pitch(void)
{
    static const uint8_t KEY[1] = {57};
    int16_t set[8] = {8, 1, 1, 1, 0, 0, 0, -64};
    dt_res_t r = dt_render(first_tone(), set, KEY, 1);
    char what[96];
    snprintf(what, sizeof what, "a plain setting (T1, C at 1, A LEV B LEV FDBK 0, MIX X): key 57 at 220 Hz (%u a second)", r.zc);
    check(r.zc >= 218u && r.zc <= 222u, what);
}

int main(void)
{
    host_tracks_init();
    check(FELUCCA_DIG_TONE && dig()->desc, "built with FELUCCA_DIG_TONE, DIGITAL has its mode-dependent names");
    t_names();
    t_presets();
    t_routings();
    t_edits();
    t_corners();
    t_pitch();
    printf("dig_tone: %d failed\n", fails);
    return fails;
}
