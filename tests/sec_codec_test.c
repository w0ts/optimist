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
          same && (rec[0] & SEC_RAW) && n_dense == SEC_REC_MAX);
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
