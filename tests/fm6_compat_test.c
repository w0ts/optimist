/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6 after the 2026-10-18 cut: projects and user presets of before still load, and the packs' ids are their own.
 *   - the stored VOICE (project.c, upreset.c): 0 INIT VOICE; a pack voice 64 + its pack's fixed number x 16 + its place,
 *     the same in every build whatever packs it has (eng_fm6.c fm6_voice_store / fm6_voice_load); a pack the build lacks:
 *     INIT VOICE with the voice itself kept inline;
 *   - an old R01..R16 / U01..U32 number (1..63 stored, then 0..47) never lands on a pack voice: INIT VOICE, whether or not
 *     the project holds the voice inline (it does, mostly: then the sound is kept), in a build with every pack too;
 *   - an old MOD / M.TIM / C.TIM over an inline voice is baked into the voice (modulators' output level, the rates of the
 *     modulators / carriers), the stored values read 0 after; with no inline voice nothing to bake (said in the docs);
 *   - the same for a user preset record (up_values, up_vals_put).
 * Built by tests/run_tests.sh several times (packs KEYS+80S, 80S alone, BASS alone, none, all):
 *   fm6_compat_test write A.bin    (packs KEYS+80S) a project with an 80S voice as VOICE
 *   fm6_compat_test check A.bin    it loads in a build with 80S alone as the same voice; with BASS alone as INIT, the voice kept
 *   fm6_compat_test                the old-number, baking and user preset checks (every build)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static uint8_t flash_ok = 1;
static struct { int force; } ui;
static char last_msg[32];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b) { str_cpy(last_msg, a, sizeof last_msg); (void)b; }
static uint32_t up_gen;
static uint8_t sync_reload;
static int param_kept(uint32_t i) { return i == P_LEVEL || i == P_PAN || i == P_MUTE; }
#include "../firmware/src/storage/upreset.c"
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? eng_slot((const uint8_t[]){0, 1, 3}[i]) : 0u; }
#include "../firmware/src/storage/project.c"
static dlrec_t rt_dl;

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
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
static void fm6_part(uint32_t k, int32_t voice)          /* part k plays FM6 on that VOICE (the list index of this build) */
{
    trk[k].eng_req = trk[k].engine = (uint8_t)ENG_SLOT_FM6;
    trk[k].p[P_E0] = (int16_t)voice;
    fm6_cur[k] = 0;
    fm6_sync(&trk[k], 0);
}
static void sealed(project_t *q) { q->sum = proj_sum(q); }
static const char *voice_name(uint32_t k, char *b) { fm6_name(b, fm6_ed[k]); return b; }

static void old_numbers(void)
{
    static project_t q;
    static const int16_t OLD[] = {1, 2, 15, 16, 17, 47, 63};   /* R02, R03, R16, U01, U02, U32 and the top of the old range */
    char nm[16], inl[16];
    uint32_t i, ok = 1, kept = 1, none = 1;
    host_tracks_init();
    fm6_part(0, 0);
    fm6_ed[0][FV_NAME] = 'Q';
    proj_capture(&q, &rt_dl);
    voice_name(0, inl);
    for (i = 0; i < sizeof OLD / sizeof OLD[0]; i++) {
        q.t[0].p[PJ_E0] = OLD[i];                          /* (an old project: VOICE R / U by its number, the voice inline) */
        q.fm6_has |= 1u;
        sealed(&q);
        host_tracks_init();
        proj_apply(&q, &rt_dl, 1);
        ok &= trk[0].p[P_E0] == 0;
        kept &= !strcmp(voice_name(0, nm), inl) && fm6_cur[0] == 1;
        q.fm6_has &= (uint8_t)~1u;                          /* (an older one still: the number alone) */
        sealed(&q);
        host_tracks_init();
        proj_apply(&q, &rt_dl, 1);
        none &= trk[0].p[P_E0] == 0 && fm6_cur[0] == 0;
    }
    check("an old R / U VOICE number (1..63, stored) is INIT VOICE, never a pack voice (every build)", ok);
    check("... the voice kept inline plays on (its name, the buffer loaded)", kept);
    check("... with no voice inline: INIT VOICE, the engine loads it afresh", none);
}

static void baking(void)
{
    static project_t q;
    int16_t *ed;
    uint32_t j, ok = 1, alg, any_mod = 0, any_car = 0;
    host_tracks_init();
    fm6_part(0, 0);
    ed = fm6_ed[0];
    alg = (uint32_t)ed[FV_ALG] & 31u;
    for (j = 0; j < 6u; j++) {                              /* every operator: level 50, rates 60 */
        uint32_t i;
        ed[j * FO_N + FO_OL] = 50;
        for (i = 0; i < 4u; i++)
            ed[j * FO_N + FO_R1 + i] = 60;
    }
    proj_capture(&q, &rt_dl);
    q.t[0].p[PJ_E0 + 1] = 20;                               /* MOD 20: +10 output level steps on the modulators */
    q.t[0].p[PJ_E0 + 2] = 10;                               /* M.TIM 10: -2 rate index steps = -3 rate steps */
    q.t[0].p[PJ_E0 + 3] = -15;                              /* C.TIM -15: +3 rate index steps = +5 rate steps */
    q.t[0].p[PJ_E0 + 4] = 1;                                /* ENGINE MARK I: nothing to bake */
    sealed(&q);
    host_tracks_init();
    proj_apply(&q, &rt_dl, 1);
    ed = fm6_ed[0];
    for (j = 0; j < 6u; j++) {
        int car = !(FM6_ALG[alg][j] & 3u);
        int exp_ol = car ? 50 : 60, exp_r = car ? 65 : 57, i;
        ok &= ed[j * FO_N + FO_OL] == exp_ol;
        for (i = 0; i < 4; i++)
            ok &= ed[j * FO_N + FO_R1 + i] == exp_r;
        any_mod += !car;
        any_car += car;
    }
    check("an old project's MOD / M.TIM / C.TIM baked into its inline voice (modulators +10 level, rates -3; carriers +5)", ok && any_mod && any_car);
    check("... the stored values read 0 after (ENGINE too: the one synthesis)",
          trk[0].p[P_E0 + 1] == 0 && trk[0].p[P_E0 + 2] == 0 && trk[0].p[P_E0 + 3] == 0 && trk[0].p[P_E0 + 4] == 0);
    proj_capture(&q, &rt_dl);
    host_tracks_init();
    proj_apply(&q, &rt_dl, 1);
    check("... and saved again, loaded again: nothing baked twice", fm6_ed[0][FV_ALG] == ed[FV_ALG] &&
          fm6_ed[0][0 * FO_N + FO_OL] == ed[0 * FO_N + FO_OL] && fm6_ed[0][5 * FO_N + FO_R1] == ed[5 * FO_N + FO_R1]);
}

static void user_records(void)
{
    static up_rec_t r;
    int16_t v[P_COUNT], out[P_COUNT];
    uint32_t i, ok = 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED, r.engine = ENG_UID_FM6, r.ver = UP_VER;
    for (i = 0; i < P_COUNT; i++)
        v[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        v[P_E0 + i] = ENG_FM6.edit[i].def;
    for (i = 0; i < FM6_NVOICE; i++) {                      /* every VOICE of this build: stored, read back the same */
        v[P_E0] = (int16_t)i;
        up_vals_put(&r, v);
        up_values(&r, out);
        ok &= out[P_E0] == (int16_t)i && (i == 0 ? r.p[P_E0] == 0 : r.p[P_E0] >= 64 && r.p[P_E0] < 64 + 5 * 16);
    }
    check("a user preset's VOICE: a pack voice stored by its fixed number (64 + pack x 16 + place), read back (every voice)", ok);
    ok = 1;
    v[P_E0] = 0;
    up_vals_put(&r, v);
    for (i = 1; i < 64u; i++) {
        r.p[P_E0] = (int16_t)i;                             /* an old R / U number (a record of before) */
        r.p[P_E0 + 1] = 20, r.p[P_E0 + 2] = 10, r.p[P_E0 + 3] = -15, r.p[P_E0 + 4] = 1;
        up_values(&r, out);
        ok &= out[P_E0] == 0 && out[P_E0 + 1] == 0 && out[P_E0 + 2] == 0 && out[P_E0 + 3] == 0 && out[P_E0 + 4] == 0 &&
              up_old_fm6[0] == 20 && up_old_fm6[1] == 10 && up_old_fm6[2] == -15;
    }
    check("an old user preset (R / U number 1..63, MOD / M.TIM / C.TIM, ENGINE): INIT VOICE, the old values kept for the bake", ok);
}

int main(int argc, char **argv)
{
    static project_t a;
    char nm[16];
    if (argc == 3 && !strcmp(argv[1], "write")) {           /* (packs KEYS + 80S) */
        host_tracks_init();
        check("write: this build has the packs KEYS and 80S", FM6_NPACKS == 2 && FM6_PACK_NO[0] == 0 && FM6_PACK_NO[1] == 4);
        fm6_part(0, 1 + 16 + 5);                            /* 80S voice 5 */
        proj_capture(&a, &rt_dl);
        check("write: stored as 64 + 4 x 16 + 5 (the 80S pack's fixed number), not the list index 22",
              a.t[0].p[PJ_E0] == 133 && (a.fm6_has & 1u));
        {
            FILE *h = fopen(argv[2], "wb");
            if (h) {
                fwrite(&a, 1, sizeof a, h);
                fclose(h);
            }
        }
    } else if (argc == 3 && !strcmp(argv[1], "check")) {    /* (80S alone, or BASS alone) */
        check("check: A.bin reads", rd(argv[2], &a));
        host_tracks_init();
        proj_apply(&a, &rt_dl, 1);
        voice_name(0, nm);
        if (FM6_NPACKS == 1 && FM6_PACK_NO[0] == 4)
            check("check (80S alone): VOICE is the list index 6 of this build, the same voice (its name)",
                  trk[0].p[P_E0] == 6 && !strncmp(nm, N_FM6V[6], strlen(nm)));
        else
            check("check (a build without the 80S pack): INIT VOICE, the voice kept inline (not INIT's name)",
                  trk[0].p[P_E0] == 0 && fm6_cur[0] == 1 && strcmp(nm, N_FM6V[0]) != 0);
    } else {
        old_numbers();
        baking();
        user_records();
    }
    printf(bad ? "fm6_compat: %d failed\n" : "fm6_compat: all passed\n", bad);
    return bad;
}
