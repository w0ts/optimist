/* SPDX-License-Identifier: GPL-3.0-only */
/* The performance macros (FELUCCA_MACROS, FELUCCA_ENERGY: firmware/src/macro.c). tests/run_tests.sh builds it with
 * both switches on (and motion recording), and once with MACROS=0 for the neutral hash:
 *   neutral   all four at home: mac_pre writes nothing; "macro_test hash" prints the hash of a 4-track mix (synths,
 *             drums, sends) that the MACROS=0 build prints too (run_tests.sh compares them)
 *   mapping   each engine's brightness parameter, the scalings toward 0, the stereo spread, the delay feedback's cap,
 *             the macros summed on a target, every base back after the block (a value written meanwhile stays)
 *   storage   the positions in the drum track's values: project capture / apply, an older project at home, the
 *             song section codec
 *   energy    the bands, their edges and hysteresis, on the beat; what each band plays of a drum step
 *   motion    a macro turned while a track records: a drum-track event, played back into mac_pre
 *   audible   each macro at -64 and +63 against home on the mix (renders: build/host/macro-*.wav): a difference of
 *             more than -20 dB, COLOR brighter / darker, SPACE wider / narrower, ENERGY louder / softer
 * Exit status: the number of failed checks. */
#include <sys/mman.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#if FELUCCA_MACROS
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 2}[i] : 0u; }
#include "../firmware/src/project.c"
#include "../firmware/src/sec_codec.c"
#if FELUCCA_MOTION
#include "../firmware/src/macro_ui.c"
#endif
#endif

static int fails;
static void check(int ok, const char *what)
{
    printf("macros: %-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the scene every render plays: three synth parts and the drums, 16 steps, the presets' sends */
static int no_drums;                                 /* the scene without its drum pattern (COLOR's brightness) */
static int no_synths;                                /* the synth parts muted (ENERGY on the drums) */
static int solo_eng = -1;                            /* >= 0: track 1 alone on this engine, its brightness halfway */
static void scene(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++) {
        memset(trk[i].v, 0, sizeof trk[i].v);
        trk[i].seq_n = trk[i].seq_hold = 0;
    }
    host_tracks_init();
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    rng_state = 0x1234567u;
    song.rec = 0;
    host_preset(&trk[0], 0, 8);                      /* ANALOG FUNK BASS */
    host_preset(&trk[1], 1 % NENGINES, 2);           /* DIGITAL WURLI */
    host_preset(&trk[2], 6 % NENGINES, 6);           /* TRIO SYNC LEAD */
    for (i = 0; i < 16u; i += 4u)
        put_step(&trk[0], i, 1, (const uint8_t[]){36 + (i & 4u ? 7u : 0u)}, ST_NOTE, 0);
    put_step(&trk[1], 0, 3, (const uint8_t[]){60, 63, 67}, ST_NOTE, 0);
    put_step(&trk[1], 8, 3, (const uint8_t[]){58, 62, 65}, ST_NOTE, 0);
    for (i = 0; i < 16u; i += 2u)
        put_step(&trk[2], i, 1, (const uint8_t[]){72 + (uint8_t)(i % 5u)}, ST_NOTE, 0);
    for (i = 0; i < 16u && !no_drums; i++) {
        dstep_t *s = &TDRUM->dstep[i];
        if (!(i & 3u))
            dstep_set(s, 0, LV_NORM, 0);             /* kick */
        if ((i & 7u) == 4u)
            dstep_set(s, 2, LV_NORM, 0);             /* snare */
        dstep_set(s, 4, (i & 1u) ? LV_GHOST : LV_NORM, 0);   /* hats, ghosts between */
        if (i == 14u)
            dstep_set(s, 13, LV_SOFT, 0);            /* shaker */
    }
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = 16;
    for (i = 0; i < NPART && no_synths; i++)
        trk[i].p[P_MUTE] = 1;
#if FELUCCA_MACROS
    if (solo_eng >= 0) {                             /* (one engine: its first preset, held notes, no drums) */
        uint32_t b = MAC_BRIGHT[eng_uid((uint32_t)solo_eng) % ENG_UID_N] & 7u;
        const param_desc_t *d = &ENGINES[solo_eng]->edit[b];
        for (i = 0; i < NTRK; i++)
            steps_clear(&trk[i]);
        host_preset(&trk[0], (uint32_t)solo_eng, ENG_IS(ENGINES[solo_eng], ANALOG) ? 8u : 0u);   /* (not the 808 sine) */
        trk[0].p[P_E0 + b] = (int16_t)((d->min + d->max) / 2);
        for (i = 0; i < 16u; i += 4u)
            put_step(&trk[0], i, 1, (const uint8_t[]){48}, ST_NOTE, 0);
    }
#endif
}

#define R_FRAMES (CTL * 1536u)                       /* ~2.2 s at 44.1 kHz: a bar at 120 BPM and its tail */
static void play(int32_t *buf, uint32_t frames)
{
    uint32_t b;
    int32_t out[CTL * 2];
    transport_req = 1;
    for (b = 0; b < frames / CTL; b++) {
        mix_block(out, CTL);
        memcpy(buf + 2u * CTL * b, out, sizeof out);
        if (b == frames / CTL * 2u / 3u)
            transport_req = 2;                       /* stop: the tails ring out */
    }
}
static uint32_t hash32(const int32_t *b, uint32_t n)
{
    uint32_t h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)b[i]) * 0x01000193u;
    return h;
}

#if FELUCCA_MACROS
static void pos(int32_t c, int32_t m, int32_t s, int32_t e)
{
    TDRUM->p[MAC_ID[0]] = (int16_t)c;
    TDRUM->p[MAC_ID[1]] = (int16_t)m;
    TDRUM->p[MAC_ID[2]] = (int16_t)s;
    TDRUM->p[MAC_ID[3]] = (int16_t)e;
}

/* a render in a child process (every static as the scene left it: no tail of the render before) */
static int32_t *render(int32_t c, int32_t m, int32_t s, int32_t e)
{
    int32_t *buf = mmap(0, R_FRAMES * 2u * sizeof(int32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pid_t pid = fork();
    if (!pid) {
        scene();
        pos(c, m, s, e);
        play(buf, R_FRAMES);
        _exit(0);
    }
    waitpid(pid, 0, 0);
    return buf;
}
static void wav(const char *name, const int32_t *b)
{
    char path[128];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "build/host/macro-%s.wav", name);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, R_FRAMES);
    for (i = 0; i < R_FRAMES; i++)
        wav_put(f, b[2u * i], b[2u * i + 1u]);
    fclose(f);
}
typedef struct { double rms, hf, side; } stats_t;
static stats_t stats(const int32_t *b)
{
    double e = 0, d = 0, mid = 0, side = 0;
    uint32_t i;
    stats_t st;
    for (i = 1; i < R_FRAMES; i++) {
        double l = b[2u * i], r = b[2u * i + 1u], lp = b[2u * i - 2u];
        e += l * l + r * r;
        d += (l - lp) * (l - lp);
        mid += (l + r) * (l + r);
        side += (l - r) * (l - r);
    }
    st.rms = sqrt(e / (2.0 * R_FRAMES));
    st.hf = d / (e / 2.0 + 1.0);
    st.side = side / (mid + 1.0);
    return st;
}
static double diff_db(const int32_t *a, const int32_t *b)
{
    double e = 0, d = 0;
    uint32_t i;
    for (i = 0; i < 2u * R_FRAMES; i++) {
        e += (double)b[i] * b[i];
        d += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
    }
    return 10.0 * log10((d + 1.0) / (e + 1.0));
}

/* ------------------------------------------------------------------------------------------------- mapping --- */
static void t_neutral(void)
{
    static int16_t p0[NTRK][P_COUNT], g0[G_COUNT];
    uint32_t k;
    scene();
    pos(0, 0, 0, 0);
    for (k = 0; k < NTRK; k++)
        memcpy(p0[k], trk[k].p, sizeof p0[k]);
    memcpy(g0, song.g, sizeof g0);
    mac_pre();
    k = mov.n;
    check(!k && !memcmp(g0, song.g, sizeof g0) && !memcmp(p0[0], trk[0].p, sizeof p0[0]) &&
          !memcmp(p0[1], trk[1].p, sizeof p0[1]) && !memcmp(p0[2], trk[2].p, sizeof p0[2]),
          "neutral: all four at home, mac_pre writes nothing");
    mac_post();
}

static void t_roles(void)
{
    uint32_t e, ok = 1, moved = 0;
    for (e = 0; e < NENGINES; e++) {
        track_t *t = &trk[0];
        uint32_t b = MAC_BRIGHT[eng_uid(e) % ENG_UID_N], k, sh;
        const param_desc_t *d;
        int32_t base, up, dn, want;
        if (b == 0xFFu)
            continue;
        k = b & 7u;
        sh = b >> 4;
        d = &ENGINES[e]->edit[k];
        scene();
        t->engine = t->eng_req = (uint8_t)e;
        base = t->p[P_E0 + k] = (int16_t)((d->min + d->max) / 2);   /* (the middle: room both ways) */
        pos(63, 0, 0, 0);
        mac_pre();
        up = t->p[P_E0 + k];
        mac_post();
        ok &= t->p[P_E0 + k] == base;
        pos(-64, 0, 0, 0);
        mac_pre();
        dn = t->p[P_E0 + k];
        mac_post();
        want = clamp(base + 48 / (1 << sh), d->min, d->max);
        ok &= up == want && dn == clamp(base - 48 / (1 << sh), d->min, d->max) && up > base && dn < base;
        moved += up != base;
        printf("macros: COLOR on %-7s EDIT %u %-5s base %4d: -64 -> %4d, +63 -> %4d\n", ENGINES[e]->name, k + 1u,
               d->label, base, dn, up);
    }
    check(ok && moved >= 10u, "mapping: COLOR moves each engine's brightness both ways (+48 / -48 its steps, WHEEL >> 3), "
                              "back after the block");
    scene();
    trk[0].p[P_E4] = 60;                             /* ANALOG CUT */
    pos(63, 0, 0, 63);
    mac_pre();
    check(trk[0].p[P_E4] == 60 + 48 + 16, "mapping: COLOR and ENERGY on one target add up");
    mac_post();
}

static void t_shapes(void)
{
    track_t *t = &trk[0];
    int32_t a, b, c, d;
    scene();
    t->p[P_LD_FLT] = 20;
    pos(0, -64, 0, 0);
    mac_pre();
    a = t->p[P_LD_FLT];
    mac_post();
    pos(0, -32, 0, 0);
    mac_pre();
    b = t->p[P_LD_FLT];
    mac_post();
    pos(0, 63, 0, 0);
    mac_pre();
    c = t->p[P_LD_FLT];
    mac_post();
    check(a == 0 && b == 10 && c == 52 && t->p[P_LD_FLT] == 20, "mapping: MOTION -64 scales LFO > FLT to 0, -32 halves it, +63 adds 32");
    trk[0].p[P_PAN] = trk[1].p[P_PAN] = trk[2].p[P_PAN] = 0;
    pos(0, 0, 63, 0);
    mac_pre();
    a = trk[0].p[P_PAN], b = trk[1].p[P_PAN], c = trk[2].p[P_PAN];
    mac_post();
    check(a == -28 && b == 0 && c == 28, "mapping: SPACE +63 spreads tracks 1 / 3 left / right (pan -28 / +28), 2 stays");
    trk[0].p[P_PAN] = 40;
    trk[0].p[P_REV] = 50;
    pos(0, 0, -64, 0);
    mac_pre();
    a = trk[0].p[P_PAN], b = trk[0].p[P_REV];
    mac_post();
    check(a == 0 && b == 0 && trk[0].p[P_PAN] == 40 && trk[0].p[P_REV] == 50, "mapping: SPACE -64: pans to the centre, sends to 0, back after");
    song.g[G_DFDBK] = 90;
    pos(0, 0, 63, 0);
    mac_pre();
    a = song.g[G_DFDBK];
    mac_post();
    song.g[G_DFDBK] = 110;
    mac_pre();
    b = song.g[G_DFDBK];
    mac_post();
    check(a == MAC_DFDBK_MAX && b == 110, "mapping: SPACE never takes the delay feedback past 100 %, nor moves a base past it");
    song.g[G_DRLVL] = 100;
    pos(0, 0, 0, 63);
    mac_pre();
    a = song.g[G_DRLVL];
    song.g[G_DRLVL] = 77;                            /* (written between the two: that value stays) */
    mac_post();
    d = song.g[G_DRLVL];
    check(a == 110 && d == 77, "mapping: ENERGY +63 adds 10 to the drums' level; a value written during the block is kept");
    pos(0, 0, 0, 0);
}

/* ------------------------------------------------------------------------------------------------- storage --- */
static void t_storage(void)
{
    static project_t pj, old;
    static dlrec_t dl;
    static uint8_t rec[SEC_REC_MAX];
    static project_t q;
    static dlrec_t qd;
    uint32_t n, m, ok = 1;
    scene();
    pos(-40, 17, 63, -64);
    proj_capture(&pj, &dl);
    old = pj;
    scene();
    pos(0, 0, 0, 0);
    proj_apply(&pj, &dl, 1);
    for (m = 0; m < 4u; m++)
        ok &= TDRUM->p[MAC_ID[m]] == (int16_t)(m == 0 ? -40 : m == 1 ? 17 : m == 2 ? 63 : -64);
    check(ok, "storage: the positions are in the project (the drum track's values) and come back with it");
    for (m = 0; m < 4u; m++)
        old.t[TRK_DRUM].p[MAC_ID[m]] = TP[MAC_ID[m]].def;   /* (a project from before: the defaults there) */
    old.sum = proj_sum(&old);
    proj_apply(&old, &dl, 1);
    ok = 1;
    for (m = 0; m < 4u; m++)
        ok &= TDRUM->p[MAC_ID[m]] == 0;
    check(ok, "storage: an older project loads with every macro at home");
    n = sec_encode(&pj, &dl, rec);
    ok = n && sec_decode(rec, n, &q, &qd);
    for (m = 0; ok && m < 4u; m++)
        ok &= q.t[TRK_DRUM].p[MAC_ID[m]] == pj.t[TRK_DRUM].p[MAC_ID[m]];
    check(ok, "storage: a song section keeps them (the section codec's round trip)");
}

/* -------------------------------------------------------------------------------------------------- energy --- */
#if FELUCCA_ENERGY
static void t_energy(void)
{
    static const int8_t X[] = {-64, -44, -43, -18, -17, 0, 16, 17, 42, 43, 63};
    static const uint8_t B[] = {0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4};   /* from band 2, up and down */
    uint32_t i, ok = 1;
    dstep_t s;
    const dstep_t *r;
    for (i = 0; i < sizeof X; i++)
        ok &= en_band(X[i], 2) == B[i];
    check(ok, "energy: the bands from home (edges -40 -14 14 40, crossed 3 past each)");
    check(en_band(12, 3) == 3 && en_band(10, 3) == 2 && en_band(-12, 1) == 1 && en_band(-10, 1) == 2 &&
          en_band(0, 0) == 2 && en_band(0, 4) == 2, "energy: hysteresis keeps a band 3 inside an edge; home is band 2 from anywhere");
    scene();
    memset(&s, 0, sizeof s);
    dstep_set(&s, 0, LV_NORM, 0);                    /* kick */
    dstep_set(&s, 4, LV_GHOST, 1);                   /* hat, a ghost x2 */
    dstep_set(&s, 9, LV_SOFT, 0);                    /* low tom */
    en.beat = 0xFFFFFFFFu;
    pos(0, 0, 0, 0);
    r = en_step(TDRUM, &s, 2);
    check(r == &s && !en.ptr, "energy: home plays the step itself (nothing copied)");
    pos(0, 0, 0, -64);
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 2);
    check(dstep_mask(r) == 1u && dstep_lvl(r, 0) == LV_SOFT && en.ptr == r, "energy: band 0 on an eighth: the kick only, a level softer");
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 3);
    check(dstep_mask(r) == 0u, "energy: band 0 between the eighths: nothing");
    pos(0, 0, 0, -30);
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 2);
    check(dstep_mask(r) == (1u | 1u << 9) && dstep_rat(r, 0) == 0u, "energy: band 1: the ghost hat left out, the rest as written");
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 3);
    check(dstep_mask(r) == 1u, "energy: band 1 between the eighths: the core lanes only");
    pos(0, 0, 0, 25);
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 3);
    check(dstep_mask(r) == dstep_mask(&s) && dstep_lvl(r, 0) == LV_HARD && dstep_lvl(r, 4) == LV_SOFT &&
          dstep_lvl(r, 9) == LV_NORM && dstep_rat(r, 4) == 1u, "energy: band 3: every hit a level harder, ratchets kept");
    pos(0, 0, 0, 63);
    en.beat = 0xFFFFFFFFu;
    memset(&s, 0, sizeof s);
    dstep_set(&s, 4, LV_NORM, 0);
    TDRUM->pass = 1;
    r = en_step(TDRUM, &s, 5);
    check(dstep_rat(r, 4) == 1u && !dstep_has(r, EN_SNARE), "energy: band 4: the hat ratchets x2; no fill before the end");
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 15);
    check(dstep_has(r, EN_SNARE) && dstep_lvl(r, EN_SNARE) == LV_HARD && dstep_rat(r, EN_SNARE) == 1u,
          "energy: band 4, second pass: a snare fill over the last four steps");
    TDRUM->pass = 2;
    en.beat = 0xFFFFFFFFu;
    r = en_step(TDRUM, &s, 15);
    check(!dstep_has(r, EN_SNARE), "energy: band 4, first pass: no fill");
    /* on the beat: a band asked mid-beat waits for the next one */
    pos(0, 0, 0, 0);
    en.beat = clk_beat = 7;
    en.band = 2;
    pos(0, 0, 0, 63);
    r = en_step(TDRUM, &s, 5);
    check(r == &s, "energy: ENERGY turned mid-beat: the step still plays as before");
    clk_beat = 8;
    r = en_step(TDRUM, &s, 6);
    check(r != &s && en.band == 4u, "energy: ... and through the new band from the next beat");
    clk_beat = 0;
    en.band = 2;
    pos(0, 0, 0, 0);
}
#endif

/* -------------------------------------------------------------------------------------------------- motion --- */
#if FELUCCA_MOTION
static void t_motion(void)
{
    uint32_t b, seen = 0;
    int32_t out[CTL * 2];
    scene();
    memset(&motion, 0, sizeof motion);
    transport_req = 1;
    for (b = 0; b < div_samples(2) / CTL + div_samples(2) / CTL / 4u; b++)   /* a quarter into step 2 */
        mix_block(out, CTL);
    song.rec = 1;                                    /* track 1 records */
    TDRUM->p[MAC_ID[0]] = 50;                        /* (as edit_param: the value, then the hook) */
    mac_motion(MAC_ID[0], 50);
    check(motion.count == 1u && motion.ev[0].place == (TRK_DRUM << 6 | 1u) && motion.ev[0].param == MAC_ID[0] &&
          motion.ev[0].value == 50, "motion: COLOR turned while track 1 records: a drum-track event on step 2");
    song.rec = 0;
    TDRUM->p[MAC_ID[0]] = 0;
    mac_motion(MAC_ID[0], 0);                        /* (not recording: the base) */
    for (b = 0; b < div_samples(2) * 16u / CTL + 4u; b++) {   /* the next pass: step 2 again */
        mix_block(out, CTL);
        seen |= TDRUM->p[MAC_ID[0]] == 50;
    }
    check(seen, "motion: the next pass sets COLOR on its step");
    {
        int16_t c0 = trk[0].p[P_E4];
        uint32_t into, slen, i, ok = 0;
        for (i = 0; i < 3000u && !ok; i++) {         /* a block where step 2 plays: COLOR +50 in mac_pre */
            mix_block(out, CTL);
            (void)trk_grid(TDRUM, &into, &slen);
            if (TDRUM->p[MAC_ID[0]] == 50) {
                mac_pre();
                ok = trk[0].p[P_E4] == clamp(c0 + 48 * 50 / 63, 0, 127);
                mac_post();
            }
        }
        check(ok, "motion: ... which the block's mac_pre applies (ANALOG CUT moves)");
    }
    seq_stop();
    check(TDRUM->p[MAC_ID[0]] == 0, "motion: STOP puts COLOR back to its base");
    memset(&motion, 0, sizeof motion);
}
#endif

/* ------------------------------------------------------------------------------------------------- audible --- */
static void t_audible(void)
{
    static const char *const NAME[4] = {"COLOR", "MOTION", "SPACE", "ENERGY"};
    int32_t *home = render(0, 0, 0, 0), *b;
    stats_t h = stats(home), lo[4], hi[4];
    uint32_t m, k;
    double d[4][2];
    wav("home", home);
    printf("macros: home: rms %.0f, hf %.4f, side/mid %.4f\n", h.rms, h.hf, h.side);
    for (m = 0; m < 4u; m++)
        for (k = 0; k < 2u; k++) {
            int32_t v = k ? 63 : -64;
            char name[32];
            b = render(m == 0 ? v : 0, m == 1 ? v : 0, m == 2 ? v : 0, m == 3 ? v : 0);
            d[m][k] = diff_db(b, home);
            (k ? hi : lo)[m] = stats(b);
            snprintf(name, sizeof name, "%s%s", NAME[m], k ? "+63" : "-64");
            wav(name, b);
            printf("macros: %-6s %+3d: difference %6.1f dB, rms %.0f, hf %.4f, side/mid %.4f\n", NAME[m], v, d[m][k],
                   (k ? hi : lo)[m].rms, (k ? hi : lo)[m].hf, (k ? hi : lo)[m].side);
            munmap(b, R_FRAMES * 2u * sizeof(int32_t));
        }
    for (m = 0; m < 4u; m++) {
        char what[96];
        snprintf(what, sizeof what, "audible: %s at -64 and +63 changes the mix by more than -20 dB", NAME[m]);
        check(d[m][0] > -20.0 && d[m][1] > -20.0, what);
    }
    no_drums = 1;                                    /* (COLOR: the synths alone, the hats would weigh in) */
    {
        int32_t *s0 = render(0, 0, 0, 0), *sl = render(-64, 0, 0, 0), *sh = render(63, 0, 0, 0);
        stats_t a0 = stats(s0), al = stats(sl), ah = stats(sh);
        printf("macros: synths alone, high-frequency share: COLOR -64 %.4f, home %.4f, +63 %.4f\n", al.hf, a0.hf, ah.hf);
        check(ah.hf > a0.hf * 1.1 && al.hf < a0.hf / 1.1, "audible: COLOR +63 brighter, -64 darker (the synths' high-frequency share)");
        munmap(s0, R_FRAMES * 2u * sizeof(int32_t));
        munmap(sl, R_FRAMES * 2u * sizeof(int32_t));
        munmap(sh, R_FRAMES * 2u * sizeof(int32_t));
    }
    no_drums = 0;
    check(hi[2].side > h.side * 1.2 && lo[2].side < h.side / 1.2, "audible: SPACE +63 wider, -64 narrower (side / mid)");
    no_synths = 1;                                   /* (ENERGY: the drums alone) */
    {
        int32_t *s0 = render(0, 0, 0, 0), *sl = render(0, 0, 0, -64), *sh = render(0, 0, 0, 63);
        stats_t a0 = stats(s0), al = stats(sl), ah = stats(sh);
        printf("macros: drums alone, rms: ENERGY -64 %.0f, home %.0f, +63 %.0f\n", al.rms, a0.rms, ah.rms);
        check(ah.rms > a0.rms * 1.1 && al.rms < a0.rms / 1.1, "audible: ENERGY +63 louder, -64 softer drums (more / harder hits, fewer / softer)");
        munmap(s0, R_FRAMES * 2u * sizeof(int32_t));
        munmap(sl, R_FRAMES * 2u * sizeof(int32_t));
        munmap(sh, R_FRAMES * 2u * sizeof(int32_t));
    }
    no_synths = 0;
    {   /* COLOR on each engine alone (its brightness halfway): a change either way */
        uint32_t e, ok = 1;
        for (e = 0; e < NENGINES; e++) {
            int32_t *s0, *sl, *sh;
            double dl, dh;
            if (MAC_BRIGHT[eng_uid(e) % ENG_UID_N] == 0xFFu || !ENGINES[e]->npresets)
                continue;
            solo_eng = (int)e;
            s0 = render(0, 0, 0, 0), sl = render(-64, 0, 0, 0), sh = render(63, 0, 0, 0);
            dl = diff_db(sl, s0), dh = diff_db(sh, s0);
            printf("macros: COLOR on %-7s alone: -64 %6.1f dB, +63 %6.1f dB; hf %.4f / %.4f / %.4f\n", ENGINES[e]->name, dl, dh,
                   stats(sl).hf, stats(s0).hf, stats(sh).hf);
            ok &= (dh > -30.0 || dl > -30.0) && stats(sh).hf > stats(sl).hf;
            munmap(s0, R_FRAMES * 2u * sizeof(int32_t));
            munmap(sl, R_FRAMES * 2u * sizeof(int32_t));
            munmap(sh, R_FRAMES * 2u * sizeof(int32_t));
        }
        solo_eng = -1;
        check(ok, "audible: COLOR changes every engine alone (more than -30 dB), +63 brighter than -64");
    }
    munmap(home, R_FRAMES * 2u * sizeof(int32_t));
}

/* --------------------------------------------------------------------------------------------------- cost --- */
static void t_cost(void)
{
    uint32_t i, n = 20000;
    uint64_t t0;
    double ns;
    scene();
    pos(40, -30, 50, 20);
    mac_pre();
    printf("macros: one block with all four off home: %u writes\n", (unsigned)mov.n);
    mac_post();
    t0 = now_ns();
    for (i = 0; i < n; i++) {
        mac_pre();
        mac_post();
    }
    ns = (double)(now_ns() - t0) / n;
    printf("macros: host cost of mac_pre + mac_post: %.0f ns a block (the block: %u samples)\n", ns, (unsigned)CTL);
    check(mov.n == 0u, "cost: mac_post empties the list");
    pos(0, 0, 0, 0);
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "hash")) {      /* the neutral render's hash (with or without the switch) */
        static int32_t buf[R_FRAMES * 2u];
        scene();
        play(buf, R_FRAMES);
        printf("%08x\n", hash32(buf, R_FRAMES * 2u));
        return 0;
    }
#if FELUCCA_MACROS
    t_neutral();
    t_roles();
    t_shapes();
    t_storage();
#if FELUCCA_ENERGY
    t_energy();
#endif
#if FELUCCA_MOTION
    t_motion();
#endif
    t_audible();
    t_cost();
#endif
    printf("macros: %d failed\n", fails);
    return fails;
}
