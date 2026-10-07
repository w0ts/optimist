/* SPDX-License-Identifier: GPL-3.0-only */
/* The song sections' record codec (firmware/src/sec_codec.c): round trips (a decoded record is the project as
 * the codec keeps it: steps past LEN and the functions of parts without an FM6 voice at their defaults), the raw
 * fallback for the dense worst case, damaged records refused, and the sizes it reaches: the power-on project,
 * typical 16-step sections, a dense one; how many fit a 4 KiB sector. Codec B (docs/PATTERNS-DESIGN.md phase 0b):
 * the demo, typical, busy and dense projects (tests/sec_projects.h), every engine x preset on the demo's patterns and
 * 2,000 random projects give the same bytes after load -> store -> load, their codec A records (an older firmware's)
 * load as the same project and store again as the same codec B record, an older firmware refuses codec B (shorter
 * than the raw length it checks); 20,000 records changed at random decode without a fault (run once under
 * -fsanitize=address,undefined by hand); flags a later firmware's refused. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#define SEC_TEST_A 1                                  /* (codec A on demand: the records before phase 0b) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/project.c"
#include "../firmware/src/sec_codec.c"
#include "sec_projects.h"

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
/* codec B (phase 0b) for p: a load, a store, a load gives the same bytes and the same project; the record codec A
 * wrote for it (an older firmware's) loads as the same project and is stored again as this record (the lazy
 * migration); an older firmware refuses this record (it takes SEC_RAW | SEC_B for raw and checks the raw length:
 * the record is shorter). -> 1 all held; *na, *nb: the two records' lengths */
static uint8_t rec_a[SEC_REC_MAX], rec_b[SEC_REC_MAX];
static int b_round(const project_t *p, const dlrec_t *d, uint32_t *na, uint32_t *nb)
{
    static project_t q;
    static dlrec_t e;
    uint32_t n2, raw;
    int ok;
    R = *p;
    sec_canon(&R);
    sec_test_a = 1;
    *na = sec_encode(p, d, rec_a);
    sec_test_a = 0;
    *nb = sec_encode(p, d, rec_b);
    ok = *nb <= *na && *nb <= SEC_REC_MAX && sec_decode(rec_b, *nb, &Q, &E) && !memcmp(&Q, &R, sizeof Q);
    n2 = ok ? sec_encode(&Q, &E, rec) : 0u;
    ok &= n2 == *nb && !memcmp(rec, rec_b, n2) && sec_decode(rec, n2, &q, &e) && !memcmp(&q, &R, sizeof q);
    ok &= sec_decode(rec_a, *na, &q, &e) && !memcmp(&q, &R, sizeof q) && sec_encode(&q, &e, rec) == *nb &&
          !memcmp(rec, rec_b, *nb);
    raw = 1u + (uint32_t)sizeof *p + (p->dl_hash ? (uint32_t)sizeof *d : 0u);
    ok &= !(rec_b[0] & SEC_B) || (rec_b[0] & SEC_RAW && *nb < raw);
    return ok;
}
/* a record changed at random (bytes flipped, cut, grown): decoded without reading past it (the fuzz run under
 * -fsanitize=address once, by hand); -> 1 when the decoder took it */
static int b_mutant(const uint8_t *src, uint32_t n)
{
    uint8_t *m;
    uint32_t k, len = n, flips = 1u + rnd(3);
    int took;
    if (rnd(4) == 0)
        len = 1u + rnd(n);
    else if (rnd(4) == 0)
        len = n + 1u + rnd(4);
    m = malloc(len);                                   /* (exactly len bytes: a read past them is a fault) */
    if (!m)
        return 0;
    memset(m, 0x5A, len);
    memcpy(m, src, n < len ? n : len);
    for (k = 0; k < flips; k++)
        m[rnd(len)] ^= (uint8_t)(1u << rnd(8));
    took = sec_decode(m, len, &Q, &E);
    free(m);
    return took;
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
    check("the power-on project: round trip (no steps: codec A, as before)", same && !(rec[0] & (SEC_RAW | SEC_B)));
    {   /* the drum lanes' sends (drum_sends.c): a project now says DRREV_MOVED; its record (own REV, DLY, CHO) and that
         * marker round-trip through codec A and B; a record written before (G_DRREV at its default 16, absent from the
         * record) decodes with 16, which proj_apply gives to its TRK lanes (the default: words unchanged) */
        uint32_t na, nb;
        int ok;
        dsend[2] = dsend_word(20, 3, 0), dsend[4] = dsend_word(0, 0, 9), dsend[7] = dsend_word(DSEND_DEF, 31, 31);
        proj_capture(&P, &D);
        ok = P.g[G_DRREV] == DRREV_MOVED && P.dl_hash && b_round(&P, &D, &na, &nb) && Q.g[G_DRREV] == DRREV_MOVED &&
             !memcmp(&E, &D, sizeof E) && E.snd[2] == dsend_word(20, 3, 0);
        check("lane sends + DRREV_MOVED: round trip through codec A and codec B", ok);
        P.g[G_DRREV] = GP[G_DRREV].def;            /* an older firmware's section */
        P.sum = proj_sum(&P);
        ok = b_round(&P, &D, &na, &nb) && Q.g[G_DRREV] == 16;
        check("an older section (DRUMS REV 16, not in the record): decodes with 16, A and B", ok);
        memset(dsend, 0, sizeof dsend);
    }
    /* typical: 16 steps on every track, a few sounds edited */
    for (i = 0; i < NTRK; i++)
        notes(&trk[i], 16, i == TRK_DRUM ? 1u : 2u, 48u + 5u * i);
    trk[0].p[P_E0 + 2] = 77, trk[1].p[P_REV] = 40, trk[2].p[P_DLY] = 30, trk[1].p[P_ATK] = 12;
    proj_capture(&P, &D);
    n_typ = round_trip(&P, &D, &same);
    check("a typical section (16 steps a track, a few edits): round trip, codec B", same && !sec_is_raw(rec[0]) && (rec[0] & SEC_B));
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
          same && sec_is_raw(rec[0]) && n_dense == SEC_RAW_N);
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
        check("today's records carry SEC_V2", (rec[0] & SEC_V2) != 0 && !sec_is_raw(rec[0]));
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
    {   /* codec B (docs/PATTERNS-DESIGN.md phase 0b) */
        uint32_t na, nb, e, pi, np = 0, ra[4], rb[4], fuzz_ok = 0, mut = 0, took = 0;
        static uint8_t keep[64][SEC_REC_MAX];
        static uint32_t keep_n[64];
        uint32_t nkeep = 0;
        all = 1;
        for (i = 0; i < 4u; i++) {                     /* the measured projects */
            if (i == 0u)
                sp_demo_sounds();
            else if (i == 1u) {
                host_tracks_init();
                for (k = 0; k < NTRK; k++)
                    notes(&trk[k], 16, k == TRK_DRUM ? 1u : 2u, 48u + 5u * k);
            } else if (i == 2u)
                sp_busy();
            else
                sp_dense();
            proj_capture(&P, &D);
            all &= b_round(&P, &D, &na, &nb);
            ra[i] = na, rb[i] = nb;
        }
        check("demo, typical, busy, dense: load -> store -> load the same bytes; codec A records migrate", all);
        check("... codec B where it is smaller (demo, typical, busy), A where it is not (dense: raw either way)",
              rb[0] < ra[0] && rb[1] < ra[1] && rb[2] < ra[2] && rb[3] == ra[3]);
        all = 1;
        for (e = 0; e < NENGINES; e++)                 /* every factory preset of every engine, the demo's patterns */
            for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
                host_tracks_init();
                for (k = 0; k < NPART; k++)
                    host_preset(&trk[k], e, pi + k);
                sp_demo();
                proj_capture(&P, &D);
                all &= b_round(&P, &D, &na, &nb);
                np++;
            }
        printf("preset projects: %u (every engine x preset, the demo's patterns)\n", np);
        check("every preset project: the same bytes after load -> store -> load; codec A records migrate", all && np > 50u);
        all = 1;
        for (i = 0; i < 2000u; i++) {                  /* random steps: sparse to full, random bytes, every LEN */
            uint32_t dens = 1u + rnd(4), zero = rnd(4);
            host_tracks_init();
            for (k = 0; k < NTRK; k++) {
                uint32_t s;
                trk[k].p[P_SLEN] = (int16_t)(1u + rnd(NSTEP));
                for (s = 0; s < NSTEP; s++)
                    if (rnd(dens) == 0) {
                        for (n = 0; n < 10u; n++)
                            ((uint8_t *)&trk[k].step[s])[n] = rnd(4) < zero ? 0u : (uint8_t)rnd(256);
                    }
            }
            proj_capture(&P, &D);
            for (k = 0; k < 10u; k++)
                P.t[rnd(NTRK)].p[rnd(PJ_NP)] = (int16_t)rnd(127);
            P.fm6_has = (uint8_t)rnd(8);
            P.dl_hash = rnd(3) ? 0u : 1u + rnd(1000);
            P.sum = proj_sum(&P);
            all &= b_round(&P, &D, &na, &nb);
            fuzz_ok += (rec_b[0] & SEC_B) != 0;
            if (nkeep < 64u && (rec_b[0] & SEC_B)) {
                memcpy(keep[nkeep], rec_b, nb);
                keep_n[nkeep++] = nb;
            }
        }
        check("2000 random projects: the same bytes after load -> store -> load, A records migrate, old firmware refuses B",
              all && fuzz_ok > 500u);
        for (i = 0; i < 20000u; i++) {
            k = rnd(nkeep);
            mut++;
            took += (uint32_t)b_mutant(keep[k], keep_n[k]);
        }
        printf("codec B records changed at random: %u decoded, %u taken (a flipped value byte still decodes)\n", mut, took);
        check("... and 20000 of them changed at random: decoded without a fault", mut == 20000u);
        sp_busy();
        proj_capture(&P, &D);
        n = sec_encode(&P, &D, rec);
        all = rec[0] & SEC_B && sec_decode(rec, n, &Q, &E);
        for (k = 1; k < n; k++)
            all &= !sec_decode(rec, k, &Q, &E);
        check("a codec B record cut short anywhere: refused", all);
        rec[0] = (uint8_t)(rec[0] & ~SEC_RAW);
        all = !sec_decode(rec, n, &Q, &E);
        rec[0] |= SEC_RAW;
        for (k = 0x20u; k <= 0x80u; k <<= 1) {        /* (a later firmware's flags: 0x20 a pattern scene) */
            rec[0] ^= (uint8_t)k;
            all &= !sec_decode(rec, n, &Q, &E);
            rec[0] ^= (uint8_t)k;
        }
        check("SEC_B without SEC_RAW, or a flag this build does not know (0x20 0x40 0x80): refused", all);
        printf("codec A -> B (bytes, with the log's 16-byte head): demo %u -> %u, typical %u -> %u, busy %u -> %u, dense %u -> %u\n",
               ra[0] + 16u, rb[0] + 16u, ra[1] + 16u, rb[1] + 16u, ra[2] + 16u, rb[2] + 16u, ra[3] + 16u, rb[3] + 16u);
    }
    printf("sections: power-on %u B, typical 16 steps %u B, dense %u B (raw), 300 random: %u B on average\n", n_def,
           n_typ, n_dense, sum / cnt);
    printf("sections per 4 KiB sector (16 B a record header, 16 B a sector header): typical %u, random %u, dense %u\n",
           4080u / (n_typ + 16u), 4080u / (sum / cnt + 16u), 4080u / (n_dense + 16u));
    printf("sec codec test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
