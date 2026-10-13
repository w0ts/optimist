/* SPDX-License-Identifier: GPL-3.0-only */
/* LFO DEST's two slots (core.h LT_*, P_L1DA .. P_L1AB; voice.c lfo_dep / lfo_apply / lfo_over; params.c lfo_*), both
 * LFOs. Run by tests/run_tests.sh with LFO2=1 (everything) and LFO2=0 (the destinations without XRATE XAMT):
 *   depths    the four depths of before -> two slots: the two of the most weight (AMP's at half), in the depths' order,
 *             AMT 2 x (AMP 1 x), the rest counted lost; back to depths exactly, every combination of up to two
 *   sound     a part with up to two depths of before renders sample for sample as their slots (the old arithmetic
 *             replayed here); the presets' depths (engines' x[]) become slots
 *   stored    a project or a user preset of before (depths) reads as its slots and writes back byte for byte; slots the
 *             depths cannot be (another destination, AMP < 0, B without A, AMT 0 on a DEST) take the tagged layout
 *             and read back the same; three or four depths: two kept
 *   values    PAN LVL ATK DEC RES E1..E8 moved for the block between pold_in and pold_out (+-half the range at full,
 *             clamped), the authored values back after; RES: the engine's EDIT value named RES / RESO / Q
 *   cross     (LFO2) XRATE: the other LFO's free RATE moved (a synced one stays), XAMT: its AMTs x 0..2; both LFOs on
 *             each other for 2000 blocks: every value in range, the authored ones untouched (no runaway)
 *   macros    (MACROS) MOTION gives LFO's FLT depth as before, with or without a slot on FLT
 *   events    an old lock / motion on a depth: onto the slot on it (AMT units), else onto a slot that does nothing
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"
#define UP_HOST 1
#define UP_WITH_ENGINES 1
#include "../firmware/src/storage/upreset.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static dlrec_t tdl;
static project_t q, q2;
static int32_t o_[2 * CTL];
static void init(void)
{
    uint32_t k, i;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        for (i = P_ENG_END; i < P_COUNT; i++)
            trk[k].p[i] = TP[i].def;
    rng_state = 0x1234567u;
}

static void t_depths(void)
{
    static const int16_t V[] = {0, 1, -1, 2, 33, -64, 63, 127};
    int16_t d[4], s[4], back[4];
    uint32_t a, b, i, j, ok = 1, n = 0;
    for (a = 0; a < 4u; a++)                           /* every pair of depths (and one alone), every value */
        for (b = a; b < 4u; b++)
            for (i = 0; i < sizeof V / sizeof V[0]; i++)
                for (j = 0; j < sizeof V / sizeof V[0]; j++) {
                    int32_t va = a == 3u ? (V[i] < 0 ? -V[i] : V[i]) : clamp(V[i], -64, 63);
                    int32_t vb = b == 3u ? (V[j] < 0 ? -V[j] : V[j]) : clamp(V[j], -64, 63);
                    memset(d, 0, sizeof d);
                    d[a] = (int16_t)va, d[b] = (int16_t)(a == b ? va : vb);
                    ok &= lfo_from_depths(d, s) == 0u && lfo_depths(s, back) == 0u && !memcmp(back, d, sizeof d);
                    ok &= (s[0] == 0 || LT_CLASSIC(s[0])) && (s[2] == 0 || s[0] < s[2]);
                    n++;
                }
    check(ok && n > 500u, "depths: up to two -> two slots (in the depths' order) -> the same depths, every value");
    d[0] = 10, d[1] = -30, d[2] = 20, d[3] = 50;       /* weights 10 30 20 25: FLT and AMP */
    ok = lfo_from_depths(d, s) == 2u && s[0] == LT_FLT && s[1] == -60 && s[2] == LT_AMP && s[3] == 50;
    d[3] = 41;                                         /* AMP's weight 21: SHP (20) loses to it, PIT too */
    ok &= lfo_from_depths(d, s) == 2u && s[0] == LT_FLT && s[2] == LT_AMP;
    d[0] = d[1] = d[2] = d[3] = 5;                     /* a tie: the first ones (AMP's weight 3) */
    ok &= lfo_from_depths(d, s) == 2u && s[0] == LT_PIT && s[1] == 10 && s[2] == LT_FLT && s[3] == 10;
    check(ok, "depths: three or four: the two of the most weight (AMP's at half; a tie: the first), the rest counted lost");
    s[0] = LT_PAN, s[1] = 50, s[2] = LT_FLT, s[3] = 20;
    ok = (lfo_depths(s, d) & LFO_NOSND) && d[1] == 10 && !d[0];
    s[0] = LT_AMP, s[1] = -20, s[2] = 0, s[3] = 0;
    ok &= (lfo_depths(s, d) & LFO_NOSND) != 0;
    s[0] = LT_SHP, s[1] = 20, s[2] = LT_PIT, s[3] = 6;  /* (B before A: heard the same, another layout) */
    ok &= lfo_depths(s, d) == LFO_NOFORM;
    s[0] = LT_FLT, s[1] = 0, s[2] = 0, s[3] = 0;       /* (a DEST at 0) */
    ok &= lfo_depths(s, d) == LFO_NOFORM;
    s[0] = LT_FLT, s[1] = 21, s[2] = 0, s[3] = 0;      /* (odd on FLT: half a depth) */
    ok &= lfo_depths(s, d) == LFO_NOFORM && d[1] == 10;
    check(ok, "depths: another destination or AMP < 0: not heard as depths; another order, AMT 0, odd: another layout");
}

/* part 0 (ANALOG's preset pi), a chord, n blocks, rendered alone (track_render) -> a hash; legacy: the four depths d
 * replayed by hand (the old voice.c arithmetic), else the slots of t as set */
static uint32_t rnd(uint32_t pi, uint32_t n)
{
    uint32_t i, j, h = 2166136261u;
    track_t *t = &trk[0];
    memset(t->v, 0, sizeof t->v);
    t->lfo_ph = 0, t->lfo_val = 0, t->lfo_fade = 32767, t->lfo_rnd = 0;
    vage = 0;
    (void)pi;
    trk_note_on(t, 48, 100);
    trk_note_on(t, 55, 90);
    for (i = 0; i < n; i++) {
        track_render(t, o_, CTL);
        for (j = 0; j < CTL; j++)
            h = (h ^ (uint32_t)o_[j]) * 16777619u;
    }
    memset(t->v, 0, sizeof t->v);
    return h;
}
static void t_sound(void)
{
    track_t *t = &trk[0];
    lfo_mod_t o;
    int32_t lfo, d[4], ok = 1;
    uint32_t c, w;
    int16_t dd[4], s[4];
    for (c = 0; c < 64u; c++) {                        /* the block's offsets: the old per-voice terms, exactly */
        d[0] = (int32_t)(c * 37u % 128u) - 64, d[1] = (int32_t)(c * 53u % 128u) - 64, d[2] = 0, d[3] = (int32_t)(c * 29u % 128u);
        dd[0] = (int16_t)d[0], dd[1] = (int16_t)(c & 1u ? 0 : d[1]), dd[2] = 0, dd[3] = (int16_t)(c & 1u ? d[3] : 0);
        (void)lfo_from_depths(dd, s);
        memcpy(&t->p[P_L1DA], s, sizeof s);
        for (w = 0; w < 64u; w++) {
            lfo = (int32_t)(w * 1031u % 65535u) - 32767;
            o = (lfo_mod_t){{0, 0, 0}, -1, -1};
            lfo1_voices(t, lfo, &o);
            ok &= o.m[0] == ((lfo * dd[0] * 3) >> 7) && o.m[1] == ((lfo * dd[1]) >> 7) && o.m[2] == 0;
            ok &= dd[3] ? o.g1 == 32767 - mulq15((lfo + 32768) >> 1, dd[3] * 258) : o.g1 < 0;
        }
    }
    check(ok, "sound: up to two depths as slots: the pitch, cutoff, shape terms and the gain of before, exactly");
    init();
    host_preset(t, 0, 3);                              /* (ANALOG: a preset with LFO FLT of before, x[]) */
    {
        const engine_t *e = ENGINES[t->engine];
        uint32_t pi, found = 0;
        for (pi = 0; pi < e->npresets && !found; pi++) {
            const preset_t *pr = &e->presets[pi];
            uint32_t i;
            for (i = 0; i + 1u < 8u; i += 2u)
                if (pr->x[i] - 1 == P_LEG_FLT) {
                    host_preset(t, 0, pi);
                    found = t->p[P_L1DA] == LT_FLT && t->p[P_L1AA] == 2 * pr->x[i + 1u] && !t->p[P_L1DB];
                }
        }
        check(found, "presets: an engine's depth of before (x[]: P_LEG_FLT) -> slot A on FLT at 2 x");
    }
    {
        uint32_t h1, h2;
        init();
        host_preset(t, 0, 0);
        t->p[P_L1DA] = LT_FLT, t->p[P_L1AA] = 80, t->p[P_L1DB] = LT_AMP, t->p[P_L1AB] = 90, t->p[P_LRATE] = 100;
        h1 = rnd(0, 200);
        t->p[P_L1DA] = LT_AMP, t->p[P_L1AA] = 90, t->p[P_L1DB] = LT_FLT, t->p[P_L1AB] = 80;   /* (the same, other order) */
        h2 = rnd(0, 200);
        check(h1 == h2 && h1 != (t->p[P_L1AA] = 0, t->p[P_L1AB] = 0, rnd(0, 200)), "render: slots in either order the same; off differs");
    }
}

static void t_stored(void)
{
    track_t *t = &trk[0];
    int16_t w[4], s[4];
    int32_t sp;
    uint32_t k;
    int ok;
    init();
    t->p[P_L1DA] = LT_PIT, t->p[P_L1AA] = -20, t->p[P_L1DB] = LT_AMP, t->p[P_L1AB] = 77;
    proj_capture(&q, &tdl);
    ok = q.t[0].p[P_LEG_PIT] == -10 && q.t[0].p[P_LEG_FLT] == 0 && q.t[0].p[P_LEG_SHP] == 0 && q.t[0].p[P_LEG_AMP] == 77;
    check(ok, "stored: slots that are depths: stored as the depths (an older build plays it, a project of before stays byte "
              "for byte)");
    init();
    proj_apply(&q, &tdl, 1);
    check(t->p[P_L1DA] == LT_PIT && t->p[P_L1AA] == -20 && t->p[P_L1DB] == LT_AMP && t->p[P_L1AB] == 77, "... and read back");
    {
        static const int16_t S[][4] = {{LT_PAN, 100, LT_FLT, -128}, {LT_AMP, -40, 0, 0}, {0, 0, LT_FLT, 30},
                                       {LT_FLT, 0, 0, 0}, {LT_SHP, 31, 0, 0}, {LT_E3, 1, LT_DEC, -1}, {LT_SHP, 20, LT_PIT, 2}};
        for (k = 0, ok = 1; k < sizeof S / sizeof S[0]; k++) {
            init();
            memcpy(&t->p[P_L1DA], S[k], sizeof S[k]);
            proj_capture(&q, &tdl);
            ok &= ((uint16_t)q.t[0].p[P_LEG_PIT] >> 8) == 0x5Au;
            init();
            proj_apply(&q, &tdl, 1);
            ok &= !memcmp(&t->p[P_L1DA], S[k], sizeof S[k]);
            proj_capture(&q2, &tdl);
            ok &= !memcmp(&q, &q2, sizeof q);
        }
        check(ok, "stored: slots that are no depths (PAN, AMP < 0, B alone, AMT 0, odd, E3 DEC, B before A): tagged, read back, "
                  "byte for byte again");
    }
    w[0] = 10, w[1] = -30, w[2] = 20, w[3] = 50;       /* a project of before with four depths */
    ok = lfo1_load(w, s, &sp) == 2u && s[0] == LT_FLT && s[2] == LT_AMP && !sp;
    check(ok, "stored: four depths of before: two kept (two counted lost)");
    {   /* user presets: the same stored form */
        up_rec_t r;
        int16_t v[P_COUNT], def[P_COUNT], out[P_COUNT];
        for (k = 0; k < P_COUNT; k++)
            v[k] = def[k] = TP[k].def;
        v[P_L1DA] = LT_FLT, v[P_L1AA] = 40;
        memset(&r, 0, sizeof r);
        r.engine = 0;
        up_vals_put(&r, v);
        ok = r.p[P_LEG_FLT] == 20 && r.p[P_LEG_PIT] == 0;
        up_params(&r, out, def);
        ok &= out[P_L1DA] == LT_FLT && out[P_L1AA] == 40 && !out[P_L1DB];
        v[P_L1DB] = LT_RES, v[P_L1AB] = -90;
        up_vals_put(&r, v);
        up_params(&r, out, def);
        ok &= ((uint16_t)r.p[P_LEG_PIT] >> 8) == 0x5Au && out[P_L1DA] == LT_FLT && out[P_L1AA] == 40 && out[P_L1DB] == LT_RES &&
              out[P_L1AB] == -90;
        r.p[P_LEG_PIT] = 3, r.p[P_LEG_FLT] = 0, r.p[P_LEG_SHP] = 0, r.p[P_LEG_AMP] = 100;   /* (a record of before) */
        up_params(&r, out, def);
        ok &= out[P_L1DA] == LT_PIT && out[P_L1AA] == 6 && out[P_L1DB] == LT_AMP && out[P_L1AB] == 100;
        check(ok, "user presets: depths stored as depths, other slots tagged, records of before read as their slots");
    }
}

/* the LFOs' values of the block before: LFO at v1, LFO 2 at v2; one bracket (pold_in .. pold_out); f run inside */
static int16_t in_v[P_COUNT];
static void bracket(int32_t v1, int32_t v2)
{
    uint32_t m;
    trk[0].lfo_val = v1, trk[0].lfo_fade = 32767;
#if FELUCCA_LFO2
    lfo2[0].v = v2;
#else
    (void)v2;
#endif
    m = pold_any() ? pold_in() : 0u;
    memcpy(in_v, trk[0].p, sizeof in_v);
    if (m)
        pold_out(m);
}
static void t_values(void)
{
    track_t *t = &trk[0];
    int16_t keep[P_COUNT];
    int ok;
    init();
    host_preset(t, 0, 0);
    t->engine = t->eng_req, t->pold = 0;
    t->p[P_PAN] = 0, t->p[P_LEVEL] = 100, t->p[P_ATK] = 10, t->p[P_DEC] = 60;
    t->p[P_L1DA] = LT_PAN, t->p[P_L1AA] = 127, t->p[P_L1DB] = LT_LVL, t->p[P_L1AB] = -128;
    memcpy(keep, t->p, sizeof keep);
    bracket(32767, 0);
    ok = in_v[P_PAN] == 63 && in_v[P_LEVEL] == 100 - 64 && !memcmp(keep, t->p, sizeof keep);
    bracket(-32767, 0);
    ok &= in_v[P_PAN] == -64 && in_v[P_LEVEL] == 127 && !memcmp(keep, t->p, sizeof keep);
    check(ok, "values: PAN at full: +-half its range (clamped), LVL -128 the other way; authored back after the mix");
    t->p[P_L1DA] = LT_ATK, t->p[P_L1AA] = 64, t->p[P_L1DB] = LT_DEC, t->p[P_L1AB] = 64;
    bracket(32767, 0);
    ok = in_v[P_ATK] > 10 && in_v[P_DEC] > 60 && t->p[P_ATK] == 10 && t->p[P_DEC] == 60;
    check(ok, "values: ATK DEC moved for the block (their index into the time tables), back after");
    t->p[P_L1DA] = LT_RES, t->p[P_L1AA] = 100, t->p[P_L1DB] = LT_E2, t->p[P_L1AB] = 100;
    {
        uint32_t ri = lo_res(ENGINES[t->engine]);
        const param_desc_t *d1 = &ENGINES[t->engine]->edit[1];
        bracket(32767, 0);
        ok = ri < 8u && in_v[P_E0 + ri] != t->p[P_E0 + ri] && (d1->max <= d1->min || in_v[P_E1] != t->p[P_E1] ||
                                                                 t->p[P_E1] == d1->max);
        check(ok, "values: RES (ANALOG's RES), E2 moved");
    }
    t->p[P_L1DA] = t->p[P_L1AA] = t->p[P_L1DB] = t->p[P_L1AB] = 0;
    check(!lfo_over_any(), "values: no slot on a value: nothing bracketed");
}
#if FELUCCA_LFO2
static void t_cross(void)
{
    track_t *t = &trk[0];
    int16_t keep[P_COUNT];
    uint32_t i, ok = 1;
    init();
    host_preset(t, 0, 0);
    t->engine = t->eng_req, t->pold = 0;
    t->p[P_LRATE] = 50, t->p[P_L2RATE] = 50;
    t->p[P_L1DA] = LT_XRATE, t->p[P_L1AA] = 127, t->p[P_L1DB] = LT_XAMT, t->p[P_L1AB] = 127;
    t->p[P_L2DA] = LT_XRATE, t->p[P_L2AA] = -128, t->p[P_L2DB] = LT_XAMT, t->p[P_L2AB] = 100;
    memcpy(keep, t->p, sizeof keep);
    bracket(32767, 32767);
    ok = in_v[P_L2RATE] == 50 + 63 && in_v[P_LRATE] < 50 && in_v[P_L2AB] == 127 && in_v[P_L1AB] == 127;
    check(ok, "cross: XRATE moves the other LFO's RATE, XAMT its AMTs (x up to 2, clamped)");
    t->p[P_L2RATE] = -3;
    bracket(32767, 0);
    check(in_v[P_L2RATE] == -3, "cross: a synced RATE stays its note value");
    t->p[P_L2RATE] = 50;
    for (i = 0; i < 2000u; i++) {                      /* both on each other, the renders between: bounded, authored kept */
        uint32_t m = pold_any() ? pold_in() : 0u, j;
        for (j = 0; j < P_COUNT; j++)
            ok &= t->p[j] >= TP[j].min && t->p[j] <= TP[j].max || j >= P_E0;
        track_render(t, o_, CTL);
        if (m)
            pold_out(m);
        ok &= !memcmp(keep, t->p, sizeof keep);
    }
    check(ok, "cross: LFO and LFO 2 on each other's RATE and AMT for 2000 blocks: every value in range, the authored untouched");
}
#endif
#if FELUCCA_MACROS
static void t_macros(void)
{
    track_t *t = &trk[0];
    lfo_mod_t o = {{0, 0, 0}, -1, -1};
    init();
    t->p[P_L1DA] = t->p[P_L1AA] = t->p[P_L1DB] = t->p[P_L1AB] = 0;
    TDRUM->p[MAC_ID[1]] = 63;                          /* MOTION all the way up */
    mac_pre();
    lfo1_voices(t, 32767, &o);
    mac_post();
    check(o.m[1] == (32767 * 64) >> 8 && o.m[2] == (32767 * 40) >> 8 && o.g1 >= 0,
          "macros: MOTION +63 with no slot on FLT: LFO's FLT depth 32 (AMT 64), SHP 20, AMP 12, as before");
    TDRUM->p[MAC_ID[1]] = 0;
}
#endif
static void t_events(void)
{
    track_t *t = &trk[0];
    uint32_t taken = 0, did, id;
    int32_t v;
    init();
    t->p[P_L1DA] = LT_PIT, t->p[P_L1AA] = 10, t->p[P_L1DB] = LT_FLT, t->p[P_L1AB] = 30;
    v = 25, id = lfo_leg_ev(t, P_LEG_FLT, &v, &taken, &did);
    check(id == P_L1AB && v == 50 && did == P_COUNT, "events: a lock of FLT 25 of before -> slot B (on FLT) at 50");
    taken = 0, v = 9, id = lfo_leg_ev(t, P_LEG_AMP, &v, &taken, &did);
    check(id == P_COUNT, "events: AMP with both slots in use: not played");
    t->p[P_L1AB] = 0, taken = 0, v = 9;
    id = lfo_leg_ev(t, P_LEG_AMP, &v, &taken, &did);
    check(id == P_L1AB && did == P_L1DB && v == 9, "events: a slot that does nothing (B at 0) takes AMP 9 for the step");
}

/* the DEST card's text (params.c lt_text): E1 .. E8 the engine's own name of that EDIT value, XRATE XAMT the other LFO's */
static void dest_text(const param_desc_t *d, int32_t v, char *b)
{
    const char *u;
    param_format(d, v, b, &u);
}
static void t_names(void)
{
    track_t *t = &trk[0];
    char b[16];
    uint32_t e, i, ok = 1, named = 0;
    init();
    for (e = 0; e < NENGINES; e++) {                   /* every engine: E1 .. E8 are its own labels (E<n> when it has none) */
        t->eng_req = (uint8_t)e;
        for (i = 0; i < 8u; i++) {
            const param_desc_t *d = track_desc(t, P_E0 + i);
            const char *l = d->label && d->label[0] && d->label[0] != '-' ? d->label : 0;
            char want[8];
            snprintf(want, sizeof want, "%.6s", l ? l : N_LT[LT_E1 + i]);
            dest_text(&TP[P_L1DA], LT_E1 + (int32_t)i, b);
            ok &= !strcmp(b, want);
            named += l != 0;
            if (!l) {
                snprintf(want, sizeof want, "E%u", i + 1u);
                ok &= !strcmp(b, want);
            }
        }
    }
    check(ok && named > 8u * 3u, "names: E1 .. E8 show each engine's own label for that slot, \"E<n>\" when it has none");
    t->eng_req = 0;                                    /* ANALOG */
    dest_text(&TP[P_L1DA], LT_E1, b);
    check(!strcmp(b, ENGINES[0]->edit[0].label), "names: ANALOG's E1 is its first EDIT label");
    dest_text(&TP[P_L1DA], LT_PAN, b);
    ok = !strcmp(b, "PAN");
    dest_text(&TP[P_L1DA], LT_OFF, b);
    check(ok && !strcmp(b, "OFF"), "names: the other destinations keep their plain names");
#if FELUCCA_LFO2
    dest_text(&TP[P_L1DA], LT_XRATE, b);
    ok = !strcmp(b, "L2 RT");
    dest_text(&TP[P_L1DB], LT_XAMT, b);
    ok &= !strcmp(b, "L2 AMT");
    dest_text(&TP[P_L2DA], LT_XRATE, b);
    ok &= !strcmp(b, "L1 RT");
    dest_text(&TP[P_L2DB], LT_XAMT, b);
    ok &= !strcmp(b, "L1 AMT");
    check(ok, "names: XRATE XAMT are L2 RT / L2 AMT on LFO's DEST, L1 RT / L1 AMT on LFO 2's");
#endif
}

int main(void)
{
    t_depths();
    t_sound();
    t_stored();
    t_values();
    t_names();
#if FELUCCA_LFO2
    t_cross();
#endif
#if FELUCCA_MACROS
    t_macros();
#endif
    t_events();
    (void)q2;
    printf("lfo_slots_test (LFO2=%d): %d failed\n", FELUCCA_LFO2, fails);
    return fails;
}
