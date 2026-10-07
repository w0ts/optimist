/* SPDX-License-Identifier: GPL-3.0-only */
/* Projects the section codec is measured and tested on (tests/sec_codec_test.c, tools/patterns_measure), made on
 * the tracks (trk[], hostsim.c), captured with proj_capture: needs hostsim.c and project.c first.
 *   sp_demo   tests/hostsim.c tracks_demo's patterns: acid 16, held chords 32, lead 12, drums 16;
 *   sp_busy   64 steps everywhere, played densely: 16ths bass, held chords, 8ths lead, a full kit (a "full" loop);
 *   sp_dense  every byte of 64 steps x 4 random: what codec B cannot shrink. */
static void sp_demo(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FM[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[12] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = &trk[TRK_DRUM];
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, ACIDF[i]);
    }
    t2->p[P_SLEN] = 32;
    t2->p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(t2, i, i % 16u == 0u ? 4u : 0u, i < 16u ? AM : FM, i % 16u == 0u ? ST_NOTE : i % 16u < 14u ? ST_TIE : ST_REST, 0);
    t3->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        if (i % 2u == 0u)
            n[k++] = i == 14u ? 46 : 42;
        put_step(td, i, k, n, k ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
}
/* the demo on its factory sounds (ANALOG ACID, DIGITAL PAD, LOFI PULSE LD where built) */
static void sp_demo_sounds(void)
{
    host_tracks_init();
    host_preset(&trk[0], 0, 4);
    host_preset(&trk[1], 1 % NENGINES, 5);
    host_preset(&trk[2], 3 % NENGINES, 0);
    sp_demo();
}
static void sp_busy(void)
{
    uint32_t i;
    sp_demo_sounds();
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = 64;
    for (i = 0; i < 64u; i++) {
        static const uint8_t C1[4] = {57, 60, 64, 67}, C2[4] = {53, 57, 60, 64};
        uint8_t b = (uint8_t)(33u + (i * 5u) % 12u), lead = (uint8_t)(72u + (i * 7u) % 12u), d[4], k = 0;
        put_step(&trk[0], i, 1, &b, ST_NOTE, i % 3u == 0u ? SF_ACCENT : 0u);
        put_step(&trk[1], i, i % 8u == 0u ? 4u : 0u, i % 16u < 8u ? C1 : C2, i % 8u == 0u ? ST_NOTE : i % 8u < 6u ? ST_TIE : ST_REST, 0);
        put_step(&trk[2], i, i % 2u == 0u ? 1u : 0u, &lead, i % 2u == 0u ? ST_NOTE : ST_REST, 0);
        if (i % 4u == 0u)
            d[k++] = 36;
        if (i % 8u == 4u)
            d[k++] = 38;
        d[k++] = i % 4u == 2u ? 46 : 42;
        if (i % 16u == 15u)
            d[k++] = 39;
        put_step(&trk[TRK_DRUM], i, k, d, ST_NOTE, i % 4u == 0u ? SF_ACCENT : 0u);
        trk[0].step[i].lvl = (uint8_t)(i % 4u), trk[0].step[i].rat = (uint8_t)(i % 8u == 7u);
        trk[TRK_DRUM].dstep[i].lvl[0] = (uint8_t)(i % 3u);
    }
}
static uint32_t sp_rng = 4242u;
static void sp_dense(void)
{
    uint32_t i, k, n;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = NSTEP;
        trk[i].p[P_SDIV] = 3, trk[i].p[P_SSWING] = 40, trk[i].p[P_SGATE] = 99;
        for (k = 0; k < NSTEP; k++)
            for (n = 0; n < 10u; n++) {
                sp_rng = sp_rng * 1664525u + 1013904223u;
                ((uint8_t *)&trk[i].step[k])[n] = (uint8_t)(1u + (sp_rng >> 8) % 200u);
            }
    }
}
