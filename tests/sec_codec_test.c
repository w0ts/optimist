/* SPDX-License-Identifier: GPL-3.0-only */
/* The song sections' record codec (firmware/src/sec_codec.c): round trips (a decoded record is the project as
 * the codec keeps it: steps past LEN and the functions of parts without an FM6 voice at their defaults), the raw
 * fallback for the dense worst case, damaged records refused, and the sizes it reaches: the power-on project,
 * typical 16-step sections, a dense one; how many fit a 4 KiB sector. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/project.c"
#include "../firmware/src/sec_codec.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t sc_rng = 12345u;
static uint32_t rnd(uint32_t n) { sc_rng = sc_rng * 1664525u + 1013904223u; return (sc_rng >> 8) % n; }

static uint8_t rec[SEC_REC_MAX];
static project_t P, Q, R;
static dlrec_t D, E;

/* p -> record -> back: the project as the codec keeps it; -> the record's length (0: failed) */
static uint32_t round_trip(const project_t *p, const dlrec_t *d, int *same)
{
    uint32_t n = sec_encode(p, d, rec);
    R = *p;
    sec_canon(&R);
    *same = n <= SEC_REC_MAX && sec_decode(rec, n, &Q, &E) && !memcmp(&Q, &R, sizeof Q) &&
            (p->dl_hash ? !memcmp(&E, d, sizeof E) : 1);
    return n;
}
static void notes(track_t *t, uint32_t len, uint32_t every, uint32_t base)
{
    uint32_t i;
    t->p[P_SLEN] = (int16_t)len;
    for (i = 0; i < len; i += every) {
        uint8_t n = (uint8_t)(base + (i * 7u) % 12u);
        if (is_drum(t))
            dstep_set(&t->dstep[i], i % 4u == 0u ? 0u : i % 4u == 2u ? 2u : 4u, LV_NORM, 0);
        else {
            t->step[i].note[0] = n;
            t->step[i].n = 1;
            t->step[i].time = ST_NOTE;
        }
    }
}

int main(void)
{
    uint32_t i, k, n, n_def, n_typ, n_dense, sum = 0, cnt = 0;
    int same, all = 1;
    /* the power-on project: the default sounds, empty patterns */
    host_tracks_init();
    proj_capture(&P, &D);
    n_def = round_trip(&P, &D, &same);
    check("the power-on project: round trip", same && !(rec[0] & SEC_RAW));
    /* typical: 16 steps on every track, a few sounds edited */
    for (i = 0; i < NTRK; i++)
        notes(&trk[i], 16, i == TRK_DRUM ? 1u : 2u, 48u + 5u * i);
    trk[0].p[P_E0 + 2] = 77, trk[1].p[P_REV] = 40, trk[2].p[P_DLY] = 30, trk[1].p[P_ATK] = 12;
    proj_capture(&P, &D);
    n_typ = round_trip(&P, &D, &same);
    check("a typical section (16 steps a track, a few edits): round trip", same && !(rec[0] & SEC_RAW));
    /* dense: 64 full steps on every track, every value moved, three FM6 voices, a drum record */
    host_tracks_init();
    proj_capture(&P, &D);
    for (i = 0; i < NTRK; i++) {
        P.t[i].p[P_SLEN] = NSTEP;
        for (k = 0; k < PJ_NP; k++)
            if (k != P_SLEN)
                P.t[i].p[k] = (int16_t)(sec_base(k) + 1 + (int32_t)rnd(5));
        for (k = 0; k < NSTEP; k++)
            for (n = 0; n < 10u; n++)
                ((uint8_t *)&P.t[i].step[k])[n] = (uint8_t)(1u + rnd(200));
    }
    P.fm6_has = 7;
    for (i = 0; i < NPART; i++) {
        for (k = 0; k < 128u; k++)
            P.fm6[i][k] = (uint8_t)rnd(100);
        P.fm6_on[i] = 0x3F;
    }
    for (k = 0; k < sizeof D; k++)
        ((uint8_t *)&D)[k] = (uint8_t)rnd(256);
    P.dl_hash = 0x12345678u;
    for (k = 0; k < G_COUNT; k++)
        P.g[k] = (int16_t)(GP[k].def + 1);
    P.sum = proj_sum(&P);
    n_dense = round_trip(&P, &D, &same);
    check("the dense worst case (64 full steps x 4, 3 FM6 voices, a drum record): the raw record, round trip",
          same && (rec[0] & SEC_RAW) && n_dense == SEC_RAW_N);
    {   /* a motion build's records (SEC_MOT, tests/motion_sections_test.c writes them): read here, without motion */
        static uint8_t m[SEC_REC_MAX];
        static const uint8_t ch[] = {64, 1};
        uint32_t c = 2u + 3u * 64u;
        m[0] = (uint8_t)(rec[0] | SEC_MOT);               /* the dense one: raw less magic, size, sum */
        memcpy(m + 1, ch, 2);
        memset(m + 3, 0, 3u * 64u);
        memcpy(m + 1 + c, rec + 1 + 8, SEC_RAWT_N);
        memcpy(m + 1 + c + SEC_RAWT_N, rec + 1 + sizeof P, sizeof D);
        check("a motion build's longest record (raw, 64 events, a drum record): SEC_REC_MAX, decodes here",
              1u + c + SEC_RAWT_N + sizeof D == SEC_REC_MAX && sec_decode(m, SEC_REC_MAX, &Q, &E) &&
              !memcmp(&Q, &R, sizeof Q) && !memcmp(&E, &D, sizeof E));
        check("... cut short by a byte, or its chunk counting 65 events: refused",
              !sec_decode(m, SEC_REC_MAX - 1u, &Q, &E) && (m[1] = 65, !sec_decode(m, SEC_REC_MAX, &Q, &E)));
        host_tracks_init();
        for (i = 0; i < NTRK; i++)
            notes(&trk[i], 16, 2, 50);
        proj_capture(&P, &D);
        n = sec_encode(&P, &D, rec);
        R = P;
        sec_canon(&R);
        m[0] = (uint8_t)(rec[0] | SEC_MOT), m[1] = 2, m[2] = 3;   /* a compressed one with 2 events */
        memset(m + 3, 9, 6);
        memcpy(m + 9, rec + 1, n - 1u);
        check("... a compressed record with a 2-event chunk: decodes here, the chunk skipped",
              sec_decode(m, n + 8u, &Q, &E) && !memcmp(&Q, &R, sizeof Q) && !sec_decode(m, n + 7u, &Q, &E));
    }
#if FELUCCA_ANALOG2
    {   /* records written before ENV2 DEST (no SEC_V2: format 10's layout, the parts' SUS2 REL2 DST2 a word each in the
         * drum track): converted on decode, DST2 with AMT2 -> that amount (project.c proj_va_fix); compressed, raw */
        static project_t va, want;
        static uint8_t raw[SEC_REC_MAX];
        static const int16_t DST[NPART] = {1, 4, 2}, AMT[NPART] = {-40, 25, 63};
        host_tracks_init();
        for (i = 0; i < NTRK; i++)
            notes(&trk[i], 16, 2, 50);
        proj_capture(&va, &D);
        for (i = 0; i < NPART; i++) {
            int16_t *w = &va.t[TRK_DRUM].p[P_A2WAVE + 3u * i];
            w[0] = (int16_t)(20 + i), w[1] = (int16_t)(100 + i), w[2] = DST[i];
            va.t[i].p[P_A2FENV] = AMT[i];
        }
        va.sum = proj_sum(&va);
        want = va;
        proj_va_fix(&want, 0);
        sec_canon(&want);
        n = sec_encode(&va, &D, rec);                  /* (the old codec: the same record, without SEC_V2) */
        check("today's records carry SEC_V2", (rec[0] & SEC_V2) != 0 && !(rec[0] & SEC_RAW));
        rec[0] &= (uint8_t)~SEC_V2;
        all = sec_decode(rec, n, &Q, &E) && !memcmp(&Q, &want, sizeof Q) && proj_ok(&Q);
        for (i = 0; i < NPART; i++) {
            int16_t x[A2X_N];
            a2x_unpack(x, &Q.t[TRK_DRUM].p[P_A2WAVE + 3u * i]);
            all &= x[0] == (int16_t)(20 + i) && x[1] == (int16_t)(100 + i) && x[1 + DST[i]] == AMT[i] && Q.t[i].p[P_A2FENV] == 0;
        }
        check("an old compressed record (no SEC_V2): DST2 PITCH / SDTN / SHAPE with AMT2 -> those amounts, FUNB", all);
        raw[0] = SEC_RAW;                              /* an old raw record: FUNA's bytes, its own sum */
        va.magic = PROJ_MAGIC_VA;
        va.sum = proj_sum(&va);
        memcpy(raw + 1, &va, sizeof va);
        want = va;
        proj_va_fix(&want, 0);
        check("an old raw record (FUNA, its sum): converted, FUNB",
              sec_decode(raw, 1u + sizeof va, &Q, &E) && !memcmp(&Q, &want, sizeof Q) && proj_ok(&Q));
        raw[1 + 20] ^= 1u;
        check("... its sum wrong: refused", !sec_decode(raw, 1u + sizeof va, &Q, &E));
        raw[1 + 20] ^= 1u;
        raw[0] = SEC_RAW | SEC_V2;                     /* (FUNA bytes said to be FUNB: refused, the magic) */
        check("FUNA bytes in a record that says SEC_V2: refused", !sec_decode(raw, 1u + sizeof va, &Q, &E));
    }
#endif
    /* random sections: every one comes back as kept */
    for (i = 0; i < 300u; i++) {
        host_tracks_init();
        for (k = 0; k < NTRK; k++)
            notes(&trk[k], 1u + rnd(NSTEP), 1u + rnd(4), 36u + rnd(40));
        proj_capture(&P, &D);
        for (k = 0; k < 20u; k++)
            P.t[rnd(NTRK)].p[rnd(PJ_NP)] = (int16_t)rnd(127);
        P.fm6_has = (uint8_t)rnd(8);
        for (k = 0; k < NPART; k++)
            P.fm6_on[k] = (uint8_t)rnd(64), P.fm6[k][rnd(128)] = (uint8_t)rnd(99);
        P.dl_hash = rnd(3) ? 0u : 1u + rnd(1000);
        P.sum = proj_sum(&P);
        n = round_trip(&P, &D, &same);
        all &= same;
        sum += n;
        cnt++;
    }
    check("300 random sections: each decodes to the project as kept", all);
    /* damaged records */
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        notes(&trk[i], 16, 2, 50);
    proj_capture(&P, &D);
    n = sec_encode(&P, &D, rec);
    all = 1;
    for (k = 1; k < n; k++)
        all &= !sec_decode(rec, k, &Q, &E);
    check("a record cut short anywhere: refused", all);
    rec[0] = SEC_RAW;
    check("a record that says raw with a compressed length: refused", !sec_decode(rec, n, &Q, &E));
    printf("sections: power-on %u B, typical 16 steps %u B, dense %u B (raw), 300 random: %u B on average\n", n_def,
           n_typ, n_dense, sum / cnt);
    printf("sections per 4 KiB sector (16 B a record header, 16 B a sector header): typical %u, random %u, dense %u\n",
           4080u / (n_typ + 16u), 4080u / (sum / cnt + 16u), 4080u / (n_dense + 16u));
    printf("sec codec test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
