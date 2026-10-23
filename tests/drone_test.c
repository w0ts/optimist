/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRONE engine (FELUCCA_ENG_DRONE, engines/drone/eng_drone.c: the coupled network after Caravel's; tests/run_tests.sh
 * builds this with the switch on):
 *   - engine UID 16 is DRONE, its fallback ANALOG (registry.h), 26 engine UIDs;
 *   - every preset: a 4-note chord held 2 s (then the keys up and HOLD OFF), 6 s more: audible, bounded, no DC,
 *     every voice free at the end, its render's FNV-1a 64 the one in tests/golden_drone.txt (GOLDEN_UPDATE=1
 *     rewrites it); the level within 9 dB of ANALOG's presets;
 *   - the EDIT values SPREAD COUPLE TOPO CHAR DRIFT TONE BEAT and the track's DTUNE each change the render; each
 *     TOPO (PAIRS CHAIN RING) its own sound with COUPLE up, none with COUPLE 0;
 *   - BEAT: each ear its own pitch, BEAT apart (10.2 Hz measured from the zero crossings), the ears as loud as
 *     each other and as the mono tone; BEAT OFF: the ears the same sample for sample;
 *   - HOLD: the key's release keeps the note, the key again lets it go; HOLD OFF lets a latched note go;
 *   - a part switched from DRONE to another engine mixes no stale side; 8 keys: 1 voice (the cap), bounded.
 * env: DRONE_WAV=DIR writes each preset's render to DIR/drone-NN-NAME.wav.
 *   build/host/drone_test [GOLDEN_FILE]
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("drone: %-86s %s\n", what, ok ? "ok" : "FAIL");
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

typedef struct { uint64_t hash; double rms, dc; int32_t peak; uint32_t busy; } dr_res_t;
static const uint8_t CHORD[4] = {48, 55, 60, 64};

static void dry(void) { trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0; }

static void blocks(uint32_t nb)
{
    int32_t out[CTL * 2];
    while (nb--)
        mix_block(out, CTL);
}
static uint32_t gated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].gate;
    return n;
}
static uint32_t active(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active;
    return n;
}

/* preset pi of engine eng with EDIT value k (0..7, 8 = none) set to v: the chord, 2 s held, the keys up and HOLD
 * OFF, 6 s more */
static dr_res_t dr_render_e(uint32_t eng, uint32_t pi, uint32_t k, int32_t v, FILE *wav)
{
    dr_res_t r = {1469598103934665603ull, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0;
    uint32_t b, i, nb = 8u * FS / CTL;
    reset();
    host_preset(&trk[0], eng, pi);
    dry();                                            /* dry: the engine's sound */
    if (k < 8u)
        trk[0].p[P_E0 + k] = (int16_t)v;
    else if (k == 9u)
        trk[0].p[P_DETUNE] = (int16_t)v;              /* (9: the track's DTUNE, the generators' detune) */
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], CHORD[i], 100);
    if (wav)
        wav_hdr(wav, nb * CTL);
    for (b = 0; b < nb; b++) {
        if (b == 2u * FS / CTL) {
            for (i = 0; i < 4u; i++)
                trk_note_off(&trk[0], CHORD[i]);
            if (eng == ENG_IX_DRONE)
                trk[0].p[P_E7] = 0;                     /* (HOLD OFF: the latched chord let go) */
        }
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
    r.busy = active(&trk[0]);
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    r.dc = sum / (2.0 * nb * CTL);
    return r;
}
static dr_res_t dr_render(uint32_t pi, uint32_t k, int32_t v, FILE *wav) { return dr_render_e(ENG_IX_DRONE, pi, k, v, wav); }

/* the level beside ANALOG's presets (the same chord, dry): the mean RMS over the 8 s */
static void t_level(void)
{
    uint32_t pi;
    double p = 0, c = 0;
    for (pi = 0; pi < ENGINES[ENG_SLOT_ANALOG]->npresets; pi++)
        p += dr_render_e(ENG_SLOT_ANALOG, pi, 8, 0, 0).rms / ENGINES[ENG_SLOT_ANALOG]->npresets;
    for (pi = 0; pi < ENGINES[ENG_IX_DRONE]->npresets; pi++)
        c += dr_render(pi, 8, 0, 0).rms / ENGINES[ENG_IX_DRONE]->npresets;
    printf("drone: mean rms over 8 s: ANALOG presets %.0f, DRONE presets %.0f (%.1f dB)\n", p, c, 20 * log10(c / p));
    check(fabs(20 * log10(c / p)) < 9.0, "the presets' level within 9 dB of ANALOG's");
}

static void t_presets(const char *gpath)
{
    const engine_t *e = ENGINES[ENG_IX_DRONE];
    uint32_t pi, ok = 1, n = e->npresets, changed = 0, missing = 0;
    uint64_t gold[32] = {0};
    char gname[32][48], line[128], s[48];
    uint32_t ng = 0;
    const char *wdir = getenv("DRONE_WAV");
    int upd = getenv("GOLDEN_UPDATE") && atoi(getenv("GOLDEN_UPDATE"));
    FILE *f = fopen(gpath, "r");
    dr_res_t res[16];
    while (f && ng < 16u && fgets(line, sizeof line, f)) {
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
            snprintf(p, sizeof p, "%s/drone-%s.wav", wdir, s);
            w = fopen(p, "wb");
        }
        res[pi] = dr_render(pi, 8, 0, w);
        if (w)
            fclose(w);
        for (g = 0; g < ng && !found; g++)
            if (!strcmp(gname[g], s)) {
                found = 1;
                if (gold[g] != res[pi].hash)
                    changed++, printf("drone: GOLDEN CHANGED %s\n", s);
            }
        missing += !found;
        printf("drone: %-16s rms %6.0f peak %5d dc %6.1f %016llx %s\n", s, res[pi].rms, res[pi].peak, res[pi].dc,
               (unsigned long long)res[pi].hash, res[pi].busy ? "STILL SOUNDING" : "");
        ok &= res[pi].rms > 300 && res[pi].peak <= 32767 && !res[pi].busy && res[pi].dc < 50 && res[pi].dc > -50;
        snprintf(gname[16 + pi % 16], sizeof gname[0], "%s", s);
    }
    check(ok, "every preset: audible (rms > 300), bounded, no DC, its voices free 6 s after the release");
    if (upd) {
        f = fopen(gpath, "w");
        if (f) {
            fprintf(f, "# DRONE engine golden renders (tests/drone_test.c): preset, FNV-1a 64 of the output (4-note chord,\n"
                       "# 2 s held, the keys up and HOLD OFF, 6 s more, dry). Rewritten by GOLDEN_UPDATE=1 -- only for an\n"
                       "# intended change.\n");
            for (pi = 0; pi < n; pi++)
                fprintf(f, "%s %016llx\n", gname[16 + pi % 16], (unsigned long long)res[pi].hash);
            fclose(f);
        }
        printf("drone: %s rewritten\n", gpath);
    } else {
        check(!changed && !missing && ng == n, "every preset renders as in tests/golden_drone.txt");
    }
}

/* each EDIT value changes the render of a preset in which it acts (HOLD: t_hold; 9: the track's DTUNE) */
static void t_edits(void)
{
    static const struct { uint8_t pi, k; int8_t v; const char *what; } E[] = {
        {0, 0, 100, "SPREAD"}, {0, 1, 90, "COUPLE"}, {0, 2, 2, "TOPO"}, {0, 3, 100, "CHAR"},
        {0, 4, 0, "DRIFT"}, {0, 5, 30, "TONE"}, {0, 6, 60, "BEAT"}, {0, 9, 100, "DTUNE"}};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof E / sizeof E[0]; i++) {
        dr_res_t a = dr_render(E[i].pi, 8, 0, 0), b = dr_render(E[i].pi, E[i].k, E[i].v, 0);
        if (a.hash == b.hash || b.peak > 32767 || b.busy)
            printf("drone: EDIT %s does not act (or unbounded)\n", E[i].what), ok = 0;
    }
    check(ok, "the EDIT values SPREAD COUPLE TOPO CHAR DRIFT TONE BEAT and the track's DTUNE change the sound, bounded");
}

/* the topologies: with COUPLE up PAIRS CHAIN RING sound apart; with COUPLE 0 (and the track's SHP centred) no
 * generator hears another, so TOPO changes nothing: the network's state after 1 s the same (its phases, outputs,
 * filters; the mix after it has a dither of its own, so the state, not the output) */
static void t_topo(void)
{
    dr_res_t r[3];
    int32_t st[3][14];
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        reset();
        r[i] = dr_render(2, 2, (int32_t)i, 0);          /* (SLOW CHAIN, COUPLE 54) */
    }
    check(r[0].hash != r[1].hash && r[1].hash != r[2].hash && r[0].hash != r[2].hash, "COUPLE up: PAIRS CHAIN RING sound apart");
    for (i = 0; i < 3u; i++) {
        const drone_pt_t *d = &drone_pt[0];
        reset();
        host_preset(&trk[0], ENG_IX_DRONE, 2);
        dry();
        trk[0].p[P_E1] = 0, trk[0].p[P_E2] = (int16_t)i;
        trk_note_on(&trk[0], 48, 100);
        blocks(FS / CTL);
        memcpy(st[i], d->ph[0], 16), memcpy(st[i] + 4, d->prev[0], 16), memcpy(st[i] + 8, d->inc, 16);
        st[i][12] = d->low[0], st[i][13] = d->dco[0];
    }
    check(!memcmp(st[0], st[1], sizeof st[0]) && !memcmp(st[1], st[2], sizeof st[0]),
          "COUPLE 0: TOPO changes nothing (no generator hears another)");
}

/* one sine (SPREAD 0, no COUPLE, CHAR, DRIFT, DTUNE: four generators in unison; TONE open) on key 57 at BEAT beat: 1 s of
 * each ear after 0.5 s */
#define DR_N ((uint32_t)FS)
static void dr_ears(int32_t beat, int32_t *l, int32_t *r)
{
    int32_t out[CTL * 2];
    uint32_t b, i, got = 0;
    reset();
    host_preset(&trk[0], ENG_IX_DRONE, 5);            /* THETA: one sine */
    dry();
    trk[0].p[P_E0] = 0, trk[0].p[P_E1] = 0, trk[0].p[P_E3] = 0, trk[0].p[P_E4] = 0;
    trk[0].p[P_E5] = 127, trk[0].p[P_E6] = (int16_t)beat, trk[0].p[P_DETUNE] = 0;
    trk[0].p[P_ATK] = 0;
    trk_note_on(&trk[0], 57, 100);
    for (b = 0; got < DR_N + FS / 2u; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++, got++)
            if (got >= FS / 2u && got - FS / 2u < DR_N) {
                l[got - FS / 2u] = out[2 * i];
                r[got - FS / 2u] = out[2 * i + 1];
            }
    }
}
/* the frequency from the rising zero crossings (interpolated), over the whole buffer */
static double dr_freq(const int32_t *x, uint32_t n)
{
    double first = -1, last = 0;
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double t = (i - 1) + (double)-x[i - 1] / (double)(x[i] - x[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            c++;
        }
    return c > 1u ? (c - 1u) * (double)FS / (last - first) : 0;
}
static double dr_rms(const int32_t *x, uint32_t n)
{
    double a = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        a += (double)x[i] * x[i];
    return sqrt(a / n);
}
static void t_beat(void)
{
    static int32_t l[DR_N], r[DR_N], l0[DR_N], r0[DR_N];
    uint32_t i, same = 1;
    double fl, fr, want = LFO_HZ_X100[101] / 100.0;
    dr_ears(0, l0, r0);
    for (i = 0; i < DR_N; i++)
        same &= l0[i] == r0[i];
    check(same && dr_rms(l0, DR_N) > 1000, "BEAT OFF: the ears the same, sample for sample");
    dr_ears(101, l, r);
    fl = dr_freq(l, DR_N), fr = dr_freq(r, DR_N);
    printf("drone: BEAT 101: left %.3f Hz, right %.3f Hz, apart %.3f Hz (BEAT %.2f Hz); mono %.3f Hz\n", fl, fr, fr - fl,
           want, dr_freq(l0, DR_N));
    check(fabs((fr - fl) - want) < 0.25, "BEAT 101: the right ear 10.2 Hz above the left");
    check(fabs((fl + fr) / 2 - dr_freq(l0, DR_N)) < 0.25, "BEAT: the ears either side of the key's pitch");
    printf("drone: rms left %.0f right %.0f mono %.0f\n", dr_rms(l, DR_N), dr_rms(r, DR_N), dr_rms(l0, DR_N));
    check(fabs(20 * log10(dr_rms(l, DR_N) / dr_rms(r, DR_N))) < 0.5 &&
              fabs(20 * log10(dr_rms(l, DR_N) / dr_rms(l0, DR_N))) < 0.5,
          "BEAT: each ear as loud as the other and as the mono tone (0.5 dB)");
}

static void t_hold(void)
{
    uint32_t i, off = 1;
    reset();
    host_preset(&trk[0], ENG_IX_DRONE, 0);
    dry();
    for (i = 0; i < ENGINES[ENG_IX_DRONE]->npresets; i++)
        off &= ENGINES[ENG_IX_DRONE]->presets[i].e[7] == 0;
    check(off && trk[0].p[P_E7] == 0, "no preset latches (HOLD OFF in every preset)");
    trk[0].p[P_E7] = 1;
    trk_note_on(&trk[0], 60, 100);
    blocks(FS / CTL);
    trk_note_off(&trk[0], 60);
    blocks(FS / CTL);
    check(gated(&trk[0]) == 1u, "HOLD: the key's release keeps the note");
    trk_note_on(&trk[0], 60, 100);
    check(gated(&trk[0]) == 0u, "HOLD: the same key again lets it go");
    blocks(10u * FS / CTL);
    check(active(&trk[0]) == 0u, "HOLD: its voice free after the release");
    trk_note_on(&trk[0], 60, 100);
    trk_note_off(&trk[0], 60);
    blocks(4);
    check(gated(&trk[0]) == 1u, "HOLD: a note latched again");
    trk[0].p[P_E7] = 0;
    blocks(1);
    check(gated(&trk[0]) == 0u, "HOLD OFF: the latched note let go");
    blocks(10u * FS / CTL);
    trk[0].p[P_E7] = 1;
    trk_note_on(&trk[0], 60, 100);
    trk_note_off(&trk[0], 60);                        /* (latched) */
    trk_note_on(&trk[0], 72, 100);                    /* (the one voice to a key still down when HOLD goes OFF) */
    trk[0].p[P_E7] = 0;
    blocks(1);
    check(gated(&trk[0]) == 1u, "HOLD OFF: a key still down plays on");
    trk_note_off(&trk[0], 72);
    check(gated(&trk[0]) == 0u, "HOLD OFF: that one ends with its key");
    trk_note_on(&trk[0], 60, 100);
    trk_note_off(&trk[0], 60);
    check(gated(&trk[0]) == 0u, "HOLD OFF: a note ends with its key");
}

/* BEAT on, then the part on ANALOG: its output centred again after 2 s (no stale side from drone_side; a state of
 * the mix after the parts keeps the ears apart for a while: measured 0.5 s, then +-1 until 1.5 s), and back on DRONE
 * with HOLD OFF the notes play on (no stale latch) */
static void t_switch(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i, same = 1;
    double a = 0;
    reset();
    host_preset(&trk[0], ENG_IX_DRONE, 5);            /* THETA: BEAT */
    dry();
    trk_note_on(&trk[0], 57, 100);
    blocks(FS / CTL);
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    dry();
    memset(trk[0].v, 0, sizeof trk[0].v);
    trk_note_on(&trk[0], 57, 100);
    blocks(2u * FS / CTL);
    for (b = 0; b < FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            same &= out[2 * i] == out[2 * i + 1];
            a += fabs((double)out[2 * i]);
        }
    }
    check(same && a > 0 && drone_sd[0], "from DRONE (BEAT on) to ANALOG: the ears the same again (its side left unmixed)");
    host_preset(&trk[0], ENG_IX_DRONE, 0);            /* DRONE latched a note before: none now */
    trk[0].p[P_E7] = 1;
    trk_note_on(&trk[0], 60, 100);
    trk_note_off(&trk[0], 60);                        /* (latched) */
    host_preset(&trk[0], ENG_SLOT_ANALOG, 0);
    memset(trk[0].v, 0, sizeof trk[0].v);
    blocks(4);
    host_preset(&trk[0], ENG_IX_DRONE, 1);            /* OPEN FIFTH: HOLD OFF */
    trk_note_on(&trk[0], 60, 100);
    blocks(8);
    check(gated(&trk[0]) == 1u, "back on DRONE with HOLD OFF: a new note plays on (no stale latch)");
}

static void t_poly(void)
{
    uint32_t i, pk = 0;
    int32_t out[CTL * 2];
    reset();
    host_preset(&trk[0], ENG_IX_DRONE, 3);            /* DARK RING: HOLD OFF */
    dry();
    trk[0].p[P_E6] = 127;                             /* (BEAT: the right ear too) */
    for (i = 0; i < 8u; i++)
        trk_note_on(&trk[0], 48 + 3u * i, 127);
    for (i = 0; i < 2u * FS / CTL; i++) {
        uint32_t j;
        mix_block(out, CTL);
        for (j = 0; j < 2u * CTL; j++)
            pk = (uint32_t)abs(out[j]) > pk ? (uint32_t)abs(out[j]) : pk;
    }
    printf("drone: 8 keys, BEAT 40 Hz: %u voices, peak %u\n", active(&trk[0]), pk);
    check(active(&trk[0]) == 1u && pk <= 32767u, "8 keys: 1 voice (the cap: one network a part), bounded");
}

/* BEAT at 0 (drone_desc's OFF descriptor): every value of its range reads OFF, no name past its list (params.c
 * param_format: a lock's value on STEP, every value of a list; the editor's DESC sends an F_ENUM's names min..max) */
static void t_beat_off(void)
{
    const param_desc_t *d;
    int32_t v, n = 0, ok = 1;
    char val[16];
    const char *u;
    reset();
    host_preset(&trk[0], ENG_IX_DRONE, 0);
    trk[0].p[P_E6] = 0;
    d = track_desc(&trk[0], P_E6);
    while (pd_names(d) && pd_names(d)[n])
        n++;
    for (v = d->min; v <= d->max; v++) {
        param_format(d, v, val, &u);
        ok &= !strcmp(val, "OFF");
    }
    check(d->max == 127 && ok && (d->fmt != F_ENUM || n > d->max - d->min),
          "BEAT 0: OFF for every value of its range, never a name past its list");
    trk[0].p[P_E6] = 40;
    check(track_desc(&trk[0], P_E6)->fmt == F_LFOHZ, "BEAT 40: its rate in Hz");
}

int main(int argc, char **argv)
{
    host_tracks_init();
    check(ENG_IX_DRONE < NENGINES && !strcmp(ENGINES[ENG_IX_DRONE]->name, "DRONE") && eng_uid(ENG_IX_DRONE) == ENG_UID_DRONE &&
              ENG_UID_DRONE == 16u && ENG_UID_N == 26u && ENG_FALLBACK[ENG_UID_DRONE] == 0u,
          "engine UID 16 is DRONE, its fallback ANALOG, 26 engine UIDs");
    t_level();
    t_presets(argc > 1 ? argv[1] : "tests/golden_drone.txt");
    t_edits();
    t_topo();
    t_beat();
    t_beat_off();
    t_hold();
    t_switch();
    t_poly();
    printf("drone: %d failed\n", fails);
    return fails;
}
