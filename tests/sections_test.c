/* SPDX-License-Identifier: GPL-3.0-only */
/* FELUCCA_SECTIONS 16 (firmware/src/sections.c) on a simulated NOR: the four old project slots (FUNA objects with
 * their drum records) move into the log as A..D at the first start, a start cut at every flash program loses
 * nothing; PROJECT SAVE / LOAD of any of A..P; a section stored while playing waits in the RAM arena, survives a
 * warm reset and reaches flash when written; the stage the audio ISR applies (the song's first part, a live jump);
 * MEM FULL. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1                        /* (the song: arranger.c, seq.c's live sections) */
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

static uint8_t nor[0x100000];
static int fail_after = -1;
static uint32_t progs;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    progs++;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[sizeof(project_t) + sizeof(dlrec_t) + 1u];
} proj_tmp;
#include "../firmware/src/drum_store.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/sections.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static project_t want[4], got;
static dlrec_t wantd[4], gotd;

/* the tracks as section s (distinct notes, a drum lane edited for B) */
static void make(uint32_t s)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = (int16_t)(8 + 4 * s);
        for (k = 0; k < 8u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k], (k + s) % 16u, LV_NORM, 0);
            else {
                trk[i].step[k].note[0] = (uint8_t)(40u + 3u * s + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
    }
    trk[1].p[P_REV] = (int16_t)(10 * s);
    memset(&dl, 0, sizeof dl);
    if (s == 1u)
        dl.ofs[2][DE_CUT] = -10;
}
static int same(uint32_t s)                       /* section s reads as made (the codec's view of it) */
{
    project_t w = want[s];
    sec_canon(&w);
    return sec_read(s, &got, &gotd) && !memcmp(&got, &w, sizeof got) && (!w.dl_hash || !memcmp(&gotd, &wantd[s], sizeof gotd));
}
static void old_flash(void)                       /* four FUNA slots as FELUCCA_SECTIONS 4 wrote them */
{
    uint32_t s;
    memset(nor, 0xFF, sizeof nor);
    for (s = 0; s < 4u; s++) {
        make(s);
        proj_capture(&want[s], &wantd[s]);
        proj_put(OBJ_PROJECT0 + s, &want[s], &wantd[s]);
        if (s == 2u)                              /* (C saved twice: both copies of its pair hold a project) */
            proj_put(OBJ_PROJECT0 + s, &want[s], &wantd[s]);
    }
}

int main(void)
{
    uint32_t s, k, ok, cut;
    static uint8_t img[0x8000];
    old_flash();
    sec_pend_clear();
    sec_boot();
    ok = 1;
    for (s = 0; s < 4u; s++)
        ok &= project_used(s) && same(s);
    check("the first start: the four old slots are sections A..D (drum records too)", ok && !project_used(4));
    ok = 1;
    for (s = 0; s < SEC_LOG_SECTORS; s++) {
        st_hdr_t h;
        st_read(slg_off(s), &h, sizeof h);
        ok &= h.magic != ST_MAGIC;
    }
    check("... no old project object is left in the area", ok);
    sec_boot();
    ok = 1;
    for (s = 0; s < 4u; s++)
        ok &= same(s);
    check("... a second start: the same, nothing migrated twice", ok && slg.seq <= 6u);
    /* the first start cut at every program: each next start goes on, nothing lost */
    for (cut = 0, ok = 1; cut < 40u; cut++) {
        old_flash();
        sec_pend_clear();
        fail_after = (int)cut;
        sec_boot();
        fail_after = -1;
        sec_boot();
        for (s = 0; s < 4u; s++)
            ok &= same(s);
    }
    check("a first start cut at each of 40 flash programs, then started again: A..D all there", ok);
    /* PROJECT SAVE / LOAD of any section */
    make(9);
    song.playing = 0, transport_req = 0;
    project_save(9);
    proj_capture(&want[0], &wantd[0]);                /* (want[0] reused as J's) */
    ok = !strcmp(last_msg, "SAVED") && project_used(9);
    make(0);
    project_load(9);
    proj_capture(&got, &gotd);
    {
        project_t w = want[0];
        sec_canon(&w), sec_canon(&got);
        ok &= !memcmp(&got.t, &w.t, sizeof got.t);
    }
    check("PROJECT page: SAVE into J, LOAD it back (16 slots)", ok);
    /* a section stored while playing: the arena, a warm reset, then flash */
    make(5);
    song.playing = 1;
    section_store(12);
    ok = sec_pend_has(12) && project_used(12) && !slg_has(12) && (sec_dirty >> 12 & 1u);
    memcpy(img, nor + SEC_LOG_BASE, sizeof img);
    sec_dirty = 0;
    sec_boot();                                       /* (a warm reset: .noinit keeps the arena) */
    ok &= sec_pend_has(12) && (sec_dirty >> 12 & 1u) && !memcmp(img, nor + SEC_LOG_BASE, sizeof img);
    check("SAVE + key while playing: M waits in RAM, a warm reset keeps it", ok);
    song.playing = 0;
    sections_write();
    check("... written when stopped: in the log, the arena empty", slg_has(12) && !sec_pend_has(12) && !sec_dirty);
    /* the stage: the song's first part while stopped, the ISR applies it on the bar */
    arrangement_enabled = 1;
    arrangement.count = 2, arrangement.loop = 0;
    arrangement.entry[0].scene = 12, arrangement.entry[0].bars = 1;
    arrangement.entry[1].scene = 1, arrangement.entry[1].bars = 1;
    sec_service();
    ok = sec_stage_id == 12;
    make(0);
    arrangement_apply(12);
    ok &= sec_stage_id == -1 && trk[1].p[P_REV] == 50;
    check("song mode: M (the first part) staged by the main loop, applied by the ISR", ok);
    arrangement_apply(1);
    check("... a part not staged in time: the ISR keeps playing (counted)", sec_misses == 1u);
    song.playing = 1;
    ok = section_cue(1) && sec_stage_id == 1 && live_req == 1;
    arrangement_apply(1);
    check("a live jump: staged at once (section_cue), applied on the bar", ok && sec_stage_id == -1);
    song.playing = 0, live_req = -1, arrangement_enabled = 0;
    /* the chain stored in the settings record: a part past D (M, P) is valid at boot; past P is not */
    ok = arr_stored_ok(&arrangement);
    arrangement.entry[1].scene = 15;
    ok &= arr_stored_ok(&arrangement);
    arrangement.entry[1].scene = 16;
    check("the stored song chain: parts E..P kept at boot, past P refused", ok && !arr_stored_ok(&arrangement));
    arrangement.entry[1].scene = 1;
    /* MEM FULL: dense sections until refused; the playing one can still be saved */
    for (s = 0; s < SEC_IDS; s++) {
        host_tracks_init();
        for (k = 0; k < NTRK; k++) {
            uint32_t j;
            trk[k].p[P_SLEN] = NSTEP;
            for (j = 0; j < NSTEP; j++)
                memset(&trk[k].step[j], (int)(1u + (s * 7u + j) % 200u), sizeof trk[k].step[j]);
        }
        project_save(s);
        if (!strcmp(last_msg, "MEM FULL"))
            break;
    }
    check("MEM FULL: dense sections until the log refuses one", !strcmp(last_msg, "MEM FULL") && s < SEC_IDS);
    {
        uint32_t pct, more;
        sec_mem(&pct, &more);
        live_sec = 0;
        project_save(0);
        check("... the gauge near full, 0 more; the playing section (A) can still be saved",
              pct >= 60u && more == 0u && !strcmp(last_msg, "SAVED"));
        printf("sections: MEM %u %%, %u more; the log %u B, a raw section %u B kept in reserve\n", pct, more, SEC_ROOM,
               SEC_REC_MAX);
    }
    printf("sections test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
