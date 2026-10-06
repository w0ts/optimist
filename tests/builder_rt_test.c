/* SPDX-License-Identifier: GPL-3.0-only */
/* Builder: a project survives a reduced build (firmware/src/registry.h stable IDs, project.c orphans).
 * Built twice by tests/run_tests.sh: once as the full build, once with FM6 and GRAIN left out
 * (-DFELUCCA_ENG_FM6=0 -DFELUCCA_ENG_GRAIN=0).
 *   full:    builder_rt_test write A.bin   a project with an FM6 part (edited voice), a GRAIN part, ANALOG,
 *                                          as the full build captures it
 *   reduced: builder_rt_test reduce A.bin B.bin C.bin
 *                                          loads A: the parts play the fallbacks (FM6 -> DIGITAL, GRAIN ->
 *                                          SAMPLE) with their defaults; B = captured untouched (must be A,
 *                                          byte for byte); C = captured after the GRAIN part's EDIT changed
 *                                          (that part is the fallback's now, the FM6 part still kept)
 *   full:    builder_rt_test check A.bin B.bin C.bin
 *                                          B loads back as A; C keeps the FM6 part and has the new GRAIN one */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? eng_slot((const uint8_t[]){0, 1, 3}[i]) : 0u; }
#include "../firmware/src/project.c"
static dlrec_t rt_dl;                            /* (the drum record a capture / apply takes: format 10) */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static int rd(const char *f, project_t *p)
{
    FILE *h = fopen(f, "rb");
    int n = h ? (int)fread(p, 1, sizeof *p, h) : 0;
    if (h)
        fclose(h);
    return n == (int)sizeof *p && proj_ok(p);
}
static void wr(const char *f, const project_t *p)
{
    FILE *h = fopen(f, "wb");
    if (h) {
        fwrite(p, 1, sizeof *p, h);
        fclose(h);
    }
}

int main(int argc, char **argv)
{
    static project_t a, b, c, q;
    uint32_t i;
    if (argc == 3 && !strcmp(argv[1], "write")) {          /* (the full build) */
        host_tracks_init();
        host_preset(&trk[0], ENG_SLOT_FM6, 3);              /* BELLS, its voice edited */
        trk[0].eng_req = trk[0].engine = (uint8_t)ENG_SLOT_FM6;
        fm6_sync(&trk[0], 0);
        fm6_ed[0][FV_ALG] = 21;
        fm6_ed[0][FV_NAME] = 'Q';
        host_preset(&trk[1], ENG_SLOT_GRAIN, 1);
        trk[1].eng_req = trk[1].engine = (uint8_t)ENG_SLOT_GRAIN;
        trk[1].p[P_E0 + 2] = 17;
        host_preset(&trk[2], ENG_SLOT_ANALOG, 2);
        trk[2].eng_req = trk[2].engine = (uint8_t)ENG_SLOT_ANALOG;
        proj_capture(&q, &rt_dl);
        proj_apply(&q, &rt_dl, 1);                                  /* as the full build keeps it */
        proj_capture(&a, &rt_dl);
        check("full: FM6 and GRAIN parts stored by UID (9, 8) with the FM6 voice",
              a.t[0].engine == 9u && a.t[1].engine == 8u && a.t[2].engine == 0u && (a.fm6_has & 1u));
        wr(argv[2], &a);
    } else if (argc == 5 && !strcmp(argv[1], "reduce")) {   /* (FM6 and GRAIN left out) */
        check("reduced: this build leaves FM6 and GRAIN out", !ENG_HAS(FM6) && !ENG_HAS(GRAIN) && ENG_HAS(DIGITAL));
        check("reduced: A.bin reads", rd(argv[2], &a));
        host_tracks_init();
        proj_apply(&a, &rt_dl, 1);
        check("reduced: the FM6 part plays DIGITAL, the GRAIN part SAMPLE (the fallbacks)",
              ENGINES[trk[0].eng_req] == &ENG_DIGITAL && ENGINES[trk[1].eng_req] == &ENG_SAMPLE &&
              ENGINES[trk[2].eng_req] == &ENG_ANALOG);
        for (i = 0; i < 8u; i++)
            if (trk[1].p[P_E0 + i] != ENG_SAMPLE.edit[i].def)
                break;
        check("reduced: ... with the fallback's own EDIT values", i == 8u);
        check("reduced: the orphans are known by UID (the UI marks them: FM6*)",
              proj_orph_uid(0) == 9u && proj_orph_uid(1) == 8u && proj_orph_uid(2) == 0xFFu);
        proj_capture(&b, &rt_dl);
        check("reduced: captured untouched = the full build's project, byte for byte", !memcmp(&a, &b, sizeof a));
        wr(argv[3], &b);
        trk[1].p[P_E0 + 1] = 5;                             /* the user edits the GRAIN part's sound */
        proj_capture(&c, &rt_dl);
        check("reduced: an edited orphan becomes the fallback's sound; the FM6 part is still kept",
              c.t[1].engine == 4u && c.t[1].p[PJ_E0 + 1] == 5 && c.t[0].engine == 9u &&
              (c.fm6_has & 1u) && !memcmp(c.fm6[0], a.fm6[0], sizeof c.fm6[0]));
        wr(argv[4], &c);
    } else if (argc == 5 && !strcmp(argv[1], "check")) {    /* (the full build again) */
        check("full: A, B, C read", rd(argv[2], &a) && rd(argv[3], &b) && rd(argv[4], &c));
        host_tracks_init();
        proj_apply(&b, &rt_dl, 1);
        proj_capture(&q, &rt_dl);
        check("full: the project back from the reduced build loads and saves as it was", !memcmp(&q, &a, sizeof a));
        check("full: ... the FM6 part plays FM6 with its edited voice",
              ENG_IS(ENGINES[trk[0].eng_req], FM6) && fm6_ed[0][FV_ALG] == 21 && fm6_ed[0][FV_NAME] == 'Q');
        proj_apply(&c, &rt_dl, 1);
        check("full: the edited part is SAMPLE now, the FM6 part FM6",
              ENG_IS(ENGINES[trk[1].eng_req], SAMPLE) && ENG_IS(ENGINES[trk[0].eng_req], FM6));
    } else {
        fprintf(stderr, "usage: builder_rt_test write A | reduce A B C | check A B C\n");
        return 2;
    }
    printf("builder round trip (%s): %s\n", argv[1], bad ? "FAILED" : "PASS");
    return bad != 0;
}
