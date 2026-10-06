/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' own sounds (firmware/src/drum_edit.c) on the host: the sound editor's offsets on a
 * rendered hit (synthesised and sampled kits), a lane on a user sample (hit, start, length; the start's
 * ADPCM state), a lane on another kit's sound, edits applying from the next hit, and the project format
 * (FUN7 -> FUN8, capture / apply). Writes DIR/drum-edit.wav: each sound neutral, then edited. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define RN (FS * 3u / 2u)                 /* 1.5 s */
static int32_t ra[RN], rb[RN];
static FILE *wav;
static uint32_t wav_n;

/* one hit of note at kit, rendered (left channel) into out; returns the samples the voice sounded */
static uint32_t hit(uint32_t kit, uint32_t note, uint32_t vel, int32_t *out)
{
    uint32_t j, k, last = 0;
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    rng_state = 0x1234567u;                       /* (the noise: the same each time) */
    TDRUM->p[P_E0] = (int16_t)kit;
    TDRUM->p[P_PAN] = 0;
    song.g[G_DRLVL] = 100;
    drum_on(note, vel);
    for (j = 0; j < RN / CTL; j++) {
        int32_t l[CTL] = {0}, r[CTL] = {0}, rev[CTL] = {0}, any = 0;
        drums_render(l, r, rev, CTL);
        for (k = 0; k < CTL; k++)
            out[j * CTL + k] = l[k];
        for (k = 0; k < NDRUM; k++)
            any |= drums.v[k].active;
        if (any)
            last = (j + 1u) * CTL;
    }
    if (wav) {
        for (j = 0; j < FS / 2u; j++)
            wav_put(wav, out[j], out[j]);
        wav_n += FS / 2u;
    }
    return last;
}
static int same(const int32_t *a, const int32_t *b) { return !memcmp(a, b, RN * sizeof *a); }
static double energy(const int32_t *a, uint32_t from, uint32_t to)
{
    double e = 0;
    uint32_t i;
    for (i = from; i < to && i < RN; i++)
        e += (double)a[i] * a[i];
    return e;
}
static double hf(const int32_t *a, uint32_t n)       /* energy of the first difference: the bright part */
{
    double e = 0;
    uint32_t i;
    for (i = 1; i < n; i++)
        e += (double)(a[i] - a[i - 1]) * (a[i] - a[i - 1]);
    return e;
}
static int32_t peak(const int32_t *a)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < RN; i++)
        p = a[i] > p ? a[i] : -a[i] > p ? -a[i] : p;
    return p;
}
static int bounded(const int32_t *a)
{
    uint32_t i;
    for (i = 0; i < RN; i++)
        if (a[i] <= -131072 || a[i] >= 131072)
            return 0;
    return 1;
}
/* zero crossings between two times: twice the frequency x the time */
static uint32_t crossings(const int32_t *a, uint32_t from, uint32_t to)
{
    uint32_t i, n = 0;
    int sg = 0;
    for (i = from; i < to; i++) {                 /* (with hysteresis: the click's noise is no crossing) */
        int s = a[i] > 2000 ? 1 : a[i] < -2000 ? -1 : 0;
        if (s && s != sg) {
            n += sg != 0;
            sg = s;
        }
    }
    return n;
}
static uint32_t first_neg(const int32_t *a)   /* the first sample of the first negative half wave */
{
    uint32_t i;
    for (i = 0; i < RN && a[i] > -8000; i++)
        ;
    return i;
}
static void lanes_zero(void) { memset(&dl, 0, sizeof dl); }

/* a user slot in the host image: one zone of n samples of ADPCM (pseudo-random codes) at 44.1 kHz */
static void host_usr_slot(uint32_t k, uint32_t n, uint32_t nz)
{
    uint8_t *s = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    smp_user_hdr_t *h = (smp_user_hdr_t *)s;
    uint32_t i, x = 12345u;
    memset(s, 0xFF, SMP_USER_SIZE);
    memset(h, 0, sizeof *h);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = (uint8_t)nz;
    memcpy(h->name, "HITS", 4);
    h->data_len = nz * ((n + 1u) / 2u);
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h->zone[i];
        z->off = i * ((n + 1u) / 2u);
        z->n = n;
        z->ls = 0;
        z->le = n - 1u;
        z->rate = 65536u;               /* 44.1 kHz: one source sample per output sample */
        z->root16 = 60 * 16;
        z->lo = (uint8_t)(60u + i);
        z->hi = (uint8_t)(60u + i);
    }
    for (i = 0; i < h->data_len; i++) {
        x = x * 1103515245u + 12345u;
        s[SMP_USER_DATA + i] = (uint8_t)(x >> 16) & 0x77u;   /* codes 0..7: a slow wander, no sign flips */
        if (i % 7u == 3u)
            s[SMP_USER_DATA + i] |= 0x88u;
    }
    smp_user_scan(k);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    char path[256];
    uint32_t na, nb, i, l;
    host_tracks_init();
    snprintf(path, sizeof path, "%s/drum-edit.wav", dir);
    wav = fopen(path, "wb");
    if (wav)
        wav_hdr(wav, 0);

    /* ---- neutral: all zero is the kit; one lane's edits leave the others alone */
    lanes_zero();
    hit(DRUM_SAMPLED, 38, 100, ra);
    dl.ofs[0][DE_TUNE] = 7;
    dl.ofs[0][DE_DECAY] = -20;
    dl.src[5] = DL_USR + 1u;
    hit(DRUM_SAMPLED, 38, 100, rb);
    check("808 snare: edits on the kick lane (and a user sample on another) leave it as the kit", same(ra, rb));
    lanes_zero();
    hit(0, 38, 100, ra);
    dl.ofs[0][DE_LEVEL] = -6;
    hit(0, 38, 100, rb);
    check("ACOUSTIC snare: the same", same(ra, rb));

    /* ---- synthesised: TUNE, DECAY, SNAP, CLICK, BEND, CUT, DRIVE, LEVEL */
    lanes_zero();
    na = hit(DRUM_SAMPLED, 36, 110, ra);                       /* 808 kick */
    dl.ofs[0][DE_TUNE] = 12;
    hit(DRUM_SAMPLED, 36, 110, rb);
    {
        uint32_t ca = crossings(ra, FS / 10u, FS / 4u), cb = crossings(rb, FS / 10u, FS / 4u);
        check("808 kick TUNE +12: the body an octave up (zero crossings x2 +-10 %)", cb * 10u >= ca * 18u && cb * 10u <= ca * 22u);
    }
    lanes_zero();
    dl.ofs[0][DE_DECAY] = -32;
    nb = hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick DECAY -32: shorter (ends sooner, less tail)", nb < na && energy(rb, FS / 5u, RN) < energy(ra, FS / 5u, RN) / 4.0);
    dl.ofs[0][DE_DECAY] = 24;
    nb = hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick DECAY +24: longer", nb > na && energy(rb, FS / 5u, RN) > energy(ra, FS / 5u, RN) * 2.0);
    lanes_zero();
    dl.ofs[0][DE_LEVEL] = -6;
    hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick LEVEL -6 dB: half the peak (+-2 %)", peak(rb) * 100 >= peak(ra) * 49 && peak(rb) * 100 <= peak(ra) * 51);
    lanes_zero();
    dl.ofs[0][DE_SNAP] = 63;
    hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick SNAP +63: brighter (noise in it)", hf(rb, FS / 10u) > hf(ra, FS / 10u) * 4.0 && bounded(rb));
    lanes_zero();
    dl.ofs[0][DE_CLICK] = 63;
    hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick CLICK +63: a brighter attack", hf(rb, FS / 100u) > hf(ra, FS / 100u) * 2.0 && bounded(rb));
    lanes_zero();
    dl.ofs[0][DE_BEND] = 24;
    hit(DRUM_SAMPLED, 36, 110, rb);
    check("808 kick BEND +24: higher at the hit (its first half wave twice as short)",
          first_neg(rb) * 2u < first_neg(ra) && bounded(rb));
    lanes_zero();
    hit(DRUM_SAMPLED, 38, 100, ra);                            /* 808 snare: noise through its filter */
    dl.ofs[2][DE_CUT] = -40;
    hit(DRUM_SAMPLED, 38, 100, rb);
    check("808 snare CUT -40: darker", hf(rb, FS / 5u) < hf(ra, FS / 5u) * 0.5 && bounded(rb));
    dl.ofs[2][DE_CUT] = 0;
    dl.ofs[2][DE_DRIVE] = 63;
    hit(DRUM_SAMPLED, 38, 100, rb);
    check("808 snare DRIVE +63: another sound, bounded", !same(ra, rb) && bounded(rb));
    {   /* every kit x lane at the extremes: bounded, and every voice ends */
        int ok = 1;
        uint32_t kk, e;
        FILE *keep = wav;
        wav = 0;
        for (kk = DRUM_SAMPLED; kk < DRUM_KITS && ok; kk += 3u)
            for (e = 0; e < 2u && ok; e++) {
                for (l = 0; l < DRUM_LANES; l++)
                    for (i = 0; i < DE_N; i++)
                        dl.ofs[l][i] = e ? DE_MAX[i] : DE_MIN[i];
                for (l = 0; l < DRUM_LANES && ok; l += 3u) {
                    nb = hit(kk, LANE_NOTE[l], 127, rb);
                    ok &= bounded(rb) && nb < RN;
                }
            }
        wav = keep;
        check("every 3rd kit x lane at all minima / maxima: bounded, the voice ends", ok);
    }

    /* ---- sampled (ACOUSTIC): TUNE, DECAY, CUT, LEVEL */
    lanes_zero();
    na = hit(0, 38, 100, ra);
    dl.ofs[2][DE_TUNE] = 12;
    nb = hit(0, 38, 100, rb);
    check("ACOUSTIC snare TUNE +12: twice as fast (half as long +-5 %)", nb * 100u >= na * 45u && nb * 100u <= na * 55u);
    lanes_zero();
    dl.ofs[2][DE_DECAY] = -40;
    nb = hit(0, 38, 100, rb);
    check("ACOUSTIC snare DECAY -40: ends sooner, less tail", nb < na / 2u && energy(rb, FS / 10u, RN) < energy(ra, FS / 10u, RN) / 4.0);
    lanes_zero();
    dl.ofs[2][DE_CUT] = -40;
    hit(0, 38, 100, rb);
    check("ACOUSTIC snare CUT -40: darker", hf(rb, FS / 5u) < hf(ra, FS / 5u) * 0.25);
    lanes_zero();
    dl.ofs[2][DE_LEVEL] = -6;
    hit(0, 38, 100, rb);
    check("ACOUSTIC snare LEVEL -6 dB: half the peak (+-2 %)", peak(rb) * 100 >= peak(ra) * 49 && peak(rb) * 100 <= peak(ra) * 51);

    /* ---- edits apply from the next hit: a sounding hit keeps its sound */
    lanes_zero();
    {
        uint32_t j, k;
        memset(&drums, 0, sizeof drums);
        drums.set = -2;
        rng_state = 0x1234567u;
        TDRUM->p[P_E0] = DRUM_SAMPLED;
        drum_on(36, 110);
        for (j = 0; j < RN / CTL; j++) {
            int32_t lb[CTL] = {0}, r[CTL] = {0}, rev[CTL] = {0};
            if (j == 40u)
                dl.ofs[0][DE_TUNE] = -12, dl.ofs[0][DE_LEVEL] = -24;
            drums_render(lb, r, rev, CTL);
            for (k = 0; k < CTL; k++)
                rb[j * CTL + k] = lb[k];
        }
        lanes_zero();
        hit(DRUM_SAMPLED, 36, 110, ra);
        check("an edit while a hit sounds: that hit as it was (the next one changes)", same(ra, rb));
    }

    /* ---- a lane on another kit's sound */
    lanes_zero();
    hit(DRUM_SAMPLED + 1u, 36, 110, ra);                       /* the 909 kick */
    dl.src[0] = DL_KIT0 + DRUM_SAMPLED + 1u;
    hit(DRUM_SAMPLED, 36, 110, rb);                            /* the 808 kit, its kick lane on the 909's */
    check("808 kit, KICK lane on the 909's kick: the 909 kick", FELUCCA_DRUM_KITS ? same(ra, rb) : 1);
    dl.src[2] = DL_KIT0 + 0u;
    hit(0, 38, 100, ra);
    hit(DRUM_SAMPLED, 38, 100, rb);
    check("808 kit, SNARE lane on ACOUSTIC's: the sampled snare", FELUCCA_DRUM_KITS ? same(ra, rb) : 1);

    /* ---- a lane on a user sample: hit, start, length */
#if FELUCCA_DRUM_USR
    {
        const uint32_t N = 20000u;
        voice_t t;
        uint32_t s0, j, best = 0;
        double num = 0, da = 0, db = 0;
        host_usr_slot(0, N, 3);
        lanes_zero();
        dl.src[2] = DL_USR + 0u;                                /* USR1, hit 2, from the half, a quarter long */
        dl_set_ref(dl.ref[2], 1, 512, 256);
        dl_tick();
        check("the start's ADPCM state decoded (dl_tick)", dl_seek[2].key == dl_seek_key(2) && dl_seek[2].pos == N / 2u);
        nb = hit(DRUM_SAMPLED, 38, 127, rb);
        check("USR1 on the snare lane: as long as the quarter of the hit (+- 1 block)",
              nb + CTL >= N / 4u && nb <= N / 4u + CTL);
        memset(&t, 0, sizeof t);                                /* the reference: hit 2 decoded from its start */
        for (s0 = 0; s0 < N / 2u; s0++)
            sample_next(&usr_zone[0][1], &t, 0);
        for (j = 0; j < N / 4u - 2u; j++) {                     /* (at 44.1 kHz: sample for sample) */
            double a = sample_next(&usr_zone[0][1], &t, 0), b = rb[j];
            num += a * b;
            da += a * a;
            db += b * b;
        }
        check("its sound: hit 2 from its middle (correlation > 0.999)", num / sqrt(da * db + 1) > 0.999);
        for (j = 0; j < N / 4u; j++)
            best = rb[j] ? j : best;
        check("silent after the length", energy(rb, N / 4u + 512u, RN) == 0.0);
        dl.ofs[2][DE_TUNE] = 12;
        nb = hit(DRUM_SAMPLED, 38, 127, rb);
        check("TUNE +12 on it: half as long", nb + CTL >= N / 8u && nb <= N / 8u + CTL);
        dl.ofs[2][DE_TUNE] = 0;
        dl.ofs[2][DE_LEVEL] = -6;
        hit(DRUM_SAMPLED, 38, 127, ra);
        dl.ofs[2][DE_LEVEL] = 0;
        hit(DRUM_SAMPLED, 38, 127, rb);
        check("LEVEL -6 dB on it: half the peak (+-2 %)", peak(ra) * 100 >= peak(rb) * 49 && peak(ra) * 100 <= peak(rb) * 51);
        dl_set_ref(dl.ref[2], 5, 0, 0);                        /* a hit the slot does not have: silent */
        hit(DRUM_SAMPLED, 38, 127, rb);
        check("a hit the slot does not have: silent", energy(rb, 0, RN) == 0.0);
        dl_set_ref(dl.ref[2], 0, 0, 0);
        host_usr_slot(0, N, 0);                                 /* the slot emptied (an upload): silent */
        hit(DRUM_SAMPLED, 38, 127, rb);
        check("an empty slot: silent", energy(rb, 0, RN) == 0.0);
        check("ref packing: hit 9, start 1000, length 1023 / full", (dl_set_ref(dl.ref[3], 9, 1000, 1023),
              dl_hit(dl.ref[3]) == 9u && dl_start(dl.ref[3]) == 1000u && dl_len(dl.ref[3]) == 1023u) &&
              (dl_set_ref(dl.ref[3], 2, 0, 1024), dl_len(dl.ref[3]) == 1024u));
        (void)best;
    }
#endif

    /* ---- project: capture / apply with the drum record; FUN7 (no lanes) and FUN8 / FUN9 (lanes inline) -> FUNA */
    {
        static project_t q, q2;
        static dlrec_t d, d2;
        static uint8_t v7[PROJ_V7_N + 4u], v8[PROJ_V8_N];
        int ok;
        lanes_zero();
        dl.ofs[3][DE_SNAP] = 9;
        dl.src[4] = DL_USR + 2u;
        dl_set_ref(dl.ref[4], 3, 100, 200);
        dl.ukit = 2;
        memcpy(dl.name, "MYKIT", 5);
        proj_capture(&q, &d);
        ok = proj_ok(&q) && q.magic == PROJ_MAGIC && !memcmp(&d.l, &dl, sizeof dl) && q.dl_hash == dlrec_hash(&d) &&
             q.dl_hash != 0u;
        lanes_zero();
        proj_apply(&q, &d, 1);
        check("capture / apply: the lanes come back (the project names its drum record)", ok && dl.ofs[3][DE_SNAP] == 9 &&
              dl.src[4] == DL_USR + 2u && dl_hit(dl.ref[4]) == 3u && dl_start(dl.ref[4]) == 100u &&
              dl_len(dl.ref[4]) == 200u && dl.ukit == 2u);
        memcpy(v7, &q, PROJ_V7_N);
        ((uint32_t *)v7)[0] = PROJ_MAGIC_V7;
        ((uint32_t *)v7)[1] = PROJ_V7_N + 4u;
        *(uint32_t *)(v7 + PROJ_V7_N) = proj_hash(v7, PROJ_V7_N);
        ok = proj_import(&q2, v7, (int)sizeof v7) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.dl_hash == 0u &&
             !memcmp(q2.t, q.t, sizeof q.t) && !memcmp(q2.fm6, q.fm6, sizeof q.fm6);
        proj_import_dl(&d2, v7, (int)sizeof v7);
        for (i = 0; i < sizeof d2; i++)
            ok &= ((const uint8_t *)&d2)[i] == 0;
        check("FUN7 -> FUNA: everything kept, the drum lanes 0 (the kit as it was)", ok);
        v7[100] ^= 1u;
        check("FUN7 with a bad checksum: refused", !proj_import(&q2, v7, (int)sizeof v7));
        memcpy(v8, &q, PROJ_V7_N);                              /* FUN8: FUN7 + the lanes inline */
        memcpy(v8 + PROJ_V7_N, &dl, sizeof dl);
        ((uint32_t *)v8)[0] = PROJ_MAGIC_V8;
        ((uint32_t *)v8)[1] = PROJ_V8_N;
        *(uint32_t *)(v8 + PROJ_V8_N - 4u) = proj_hash(v8, PROJ_V8_N - 4u);
        ok = proj_import(&q2, v8, (int)sizeof v8) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && !memcmp(q2.t, q.t, sizeof q.t);
        proj_import_dl(&d2, v8, (int)sizeof v8);
        ok &= !memcmp(&d2.l, &dl, sizeof dl) && q2.dl_hash == dlrec_hash(&d2);
        for (i = 0; i < DRUM_LANES; i++)
            ok &= d2.snd[i] == 0u;
        check("FUN8 -> FUNA: the lanes from the project, every send TRK / 0, the key names them", ok);
        lanes_zero();
        proj_apply(&q2, &d2, 1);
        check("... applied: the FUN8 lanes play", dl.ofs[3][DE_SNAP] == 9 && dl.src[4] == DL_USR + 2u);
        ((uint32_t *)v8)[0] = PROJ_MAGIC_V9;                    /* FUN9 (feat/ui-overview2): FUN8's layout */
        *(uint32_t *)(v8 + PROJ_V8_N - 4u) = proj_hash(v8, PROJ_V8_N - 4u);
        proj_import_dl(&d2, v8, (int)sizeof v8);
        check("FUN9 (ui-overview2's, FUN8's layout) -> FUNA: the lanes from it too",
              proj_import(&q2, v8, (int)sizeof v8) && proj_ok(&q2) && !memcmp(&d2.l, &dl, sizeof dl) && q2.dl_hash == dlrec_hash(&d2));
        v8[3000] ^= 4u;
        check("FUN8 / FUN9 with a bad checksum: refused", !proj_import(&q2, v8, (int)sizeof v8));
        d.l.ofs[0][DE_TUNE] = 100;                              /* out of range (a damaged or foreign file) */
        d.l.src[1] = 9;
        proj_apply(&q, &d, 1);
        check("apply: offsets clamped, an unknown source -> the kit", dl.ofs[0][DE_TUNE] == 24 && dl.src[1] == DL_KIT);
    }
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_hdr(wav, wav_n);
        fclose(wav);
    }
    printf(fails ? "DRUM EDIT TEST FAILED\n" : "drum edit test passed\n");
    return fails != 0;
}
