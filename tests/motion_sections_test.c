/* SPDX-License-Identifier: GPL-3.0-only */
/* Motion recording (FELUCCA_MOTION, firmware/src/motion*.c) with every FELUCCA_SECTIONS (4, 8, 16) on a
 * simulated NOR. Built three times by tests/run_tests.sh (-DFELUCCA_SECTIONS=4 / 8 / 16).
 *   8 / 16   a section's motion travels in its record (sec_codec.c SEC_MOT): record, save, load and it plays;
 *            none recorded, no chunk (the record as before); stored while playing (the arena), staged for the
 *            song; a save cut at every flash program leaves the old section with its own motion; the reserve
 *            keeps a dense raw section with 64 events savable; the four old slots with their motion (beside
 *            them in their sectors) migrate into A..D, cut anywhere; the editor's backup / restore of a section
 *            brings its motion back
 *   4        the slot's motion beside the project (motion_flash.c): saved, read back, a save cut at every
 *            program never attaches a motion to another project */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_MOTION 1
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
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/drum_store.c"
#include "../firmware/src/motion_flash.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the tracks as section s: distinct notes, a reverb send per section */
static void make(uint32_t s)
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        for (k = 0; k < 8u; k++)
            if (i == TRK_DRUM)
                dstep_set(&trk[i].dstep[k], (k + s) % 16u, LV_NORM, 0);
            else {
                trk[i].step[k].note[0] = (uint8_t)(40u + 3u * s + k + i);
                trk[i].step[k].n = 1;
                trk[i].step[k].time = ST_NOTE;
            }
    }
    trk[1].p[P_REV] = (int16_t)(10 * (s % 10u));
    memset(&dl, 0, sizeof dl);
}
/* a recorded motion of n events (n <= 64: CHORUS sends on steps 0..15 of the four tracks), values by seed */
static void rec_motion(uint32_t n, uint32_t seed)
{
    uint32_t i;
    memset(&motion, 0, sizeof motion);
    for (i = 0; i < n; i++)
        motion_set_event(&trk[i / 16u], i % 16u, P_CHOR, (int32_t)((seed * 13u + i * 7u) % 100u));
}
static int motion_is(const motion_store_t *m, const motion_store_t *w)
{
    return m->count == w->count && m->on == w->on && !memcmp(m->ev, w->ev, 3u * w->count);
}
/* the working motion plays: each event's value on its track at its step (from the base each pass) */
static int plays(const motion_store_t *w)
{
    uint32_t i, ok = 1;
    motion_begin();
    for (i = 0; i < w->count; i++) {
        uint32_t k = w->ev[i].place >> 6, st = w->ev[i].place & 63u;
        motion_step(&trk[k], st);
        ok &= trk[k].p[w->ev[i].param] == w->ev[i].value;
    }
    motion_end();
    return ok;
}

#if SEC_LOGGED
static struct { int force; uint8_t arm, arm_t; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/sections.c"
#include "../firmware/src/drum_kits.c"

/* ---- the editor's backup (ed_backup.c), as tests/backup_test.c drives it */
static uint8_t ed_out[700];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
#include "../firmware/src/ed_drums.c"
static int flushed;
#define BK_FLUSH() (flushed++)
#include "../firmware/src/ed_backup.c"
static uint32_t pack(const void *p, uint32_t n, uint8_t *o)
{
    const uint8_t *b = p;
    uint32_t k = 0;
    while (n) {
        uint32_t c = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < c; j++)
            m |= (uint32_t)(b[j] >> 7) << j;
        o[k++] = (uint8_t)m;
        for (j = 0; j < c; j++)
            o[k++] = b[j] & 0x7Fu;
        b += c;
        n -= c;
    }
    return k;
}
static int cmd(uint32_t c, const uint8_t *a, uint32_t na) { ed_n = 0; return ed_backup(c, a, na); }
static void put32(uint8_t *a, uint32_t v, uint32_t k) { while (k--) { *a++ = (uint8_t)(v & 0x7Fu); v >>= 7; } }
/* object tag -> its index and length (BK_LIST) */
static int bk_find(const char *tag, uint32_t *len, uint32_t *crc)
{
    uint8_t a[1] = {1};
    uint32_t i, p = 10;
    if (!cmd(ED_BK_LIST, a, 1))
        return -1;
    for (i = 0; i < ed_out[1]; i++, p += 14)
        if (!memcmp(ed_out + p, tag, 4)) {
            *len = ed_r32(ed_out + p + 6, 3);
            *crc = ed_r32(ed_out + p + 9, 5);
            return (int)i;
        }
    return -1;
}
static uint32_t bk_read(uint32_t i, uint32_t len, uint8_t *d)   /* -> bytes read, CRC-checked per chunk */
{
    uint32_t off = 0;
    while (off < len) {
        uint8_t a[4];
        uint32_t n;
        a[0] = (uint8_t)i;
        put32(a + 1, off, 3);
        if (!cmd(ED_BK_READ, a, 4))
            return 0;
        n = ed_unpack7(ed_out + 9, ed_n - 9u, d + off, len - off);
        if (!n || st_crc32(d + off, n) != ed_r32(ed_out + 4, 5))
            return 0;
        off += n;
    }
    return off;
}
static int bk_write(uint32_t i, const uint8_t *d, uint32_t n)   /* -> COMMIT's rc */
{
    uint8_t a[400];
    uint32_t off = 0;
    a[0] = (uint8_t)i;
    put32(a + 1, n, 3);
    put32(a + 4, st_crc32(d, n), 5);
    if (!cmd(ED_BK_BEGIN, a, 9) || ed_out[1])
        return 10 + ed_out[1];
    while (off < n) {
        uint32_t c = n - off > BK_CHUNK ? BK_CHUNK : n - off;
        put32(a + 1, off, 3);
        put32(a + 4, st_crc32(d + off, c), 5);
        if (!cmd(ED_BK_DATA, a, 9u + pack(d + off, c, a + 9)) || ed_out[4])
            return 20 + ed_out[4];
        off += c;
    }
    cmd(ED_BK_COMMIT, a, 1);
    return ed_out[1];
}

static project_t oldp, got;
static dlrec_t oldd, gotd;
static motion_store_t keepm[4];

/* section s reads with motion w (0: none): the record decoded, its store */
static int sec_motion_is(uint32_t s, const motion_store_t *w)
{
    const motion_store_t *m;
    if (!sec_read(s, &got, &gotd))
        return 0;
    m = motion_for(&got, 0);
    if (!m || m->psum != got.sum)
        return 0;
    return w ? motion_is(m, w) : !m->count && !m->on;
}
/* four FUNA slots with their motion, as FELUCCA_SECTIONS 4 + MOTION wrote them (motion_flash.c) */
static void old_flash(void)
{
    uint32_t s;
    memset(nor, 0xFF, sizeof nor);
    for (s = 0; s < 4u; s++) {
        make(s);
        rec_motion(s == 3u ? 0u : 5u + 20u * s, s);   /* (D: none) */
        keepm[s] = motion;
        proj_capture(&oldp, &oldd);
        proj_put(OBJ_PROJECT0 + s, &oldp, &oldd);
        motion_flash_write(OBJ_PROJECT0 + s, &oldp);
    }
}
static void dense(uint32_t s)                     /* four full 64-step tracks (a capture this dense: ~2.7 KB) */
{
    uint32_t k, j;
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLEN] = NSTEP;
        for (j = 0; j < NSTEP; j++)
            memset(&trk[k].step[j], (int)(1u + (s * 7u + j) % 200u), sizeof trk[k].step[j]);
    }
}

int main(void)
{
    static uint8_t img[0x8000], bk[SEC_REC_MAX];
    motion_store_t m1, m2;
    uint32_t s, n, n0, cut, ok, olds, news, full;
    (void)motion_for(&proj_tmp.cur, 1), (void)motion_for(&sec_stage_p, 1);   /* (as persist_boot binds them) */
    (void)motion_for(&oldp, 1), (void)motion_for(&got, 1);
    printf("motion with %u sections: a record at most %u B (%u B in the log; a sector holds %u)\n", FELUCCA_SECTIONS,
           SEC_REC_MAX, SEC_ALIGN(SEC_HEAD + SEC_REC_MAX), SEC_SECT - SEC_HEAD);
    check("the longest record (raw, 64 events) fits one log sector", SEC_ALIGN(SEC_HEAD + SEC_REC_MAX) <= SEC_SECT - SEC_HEAD);

    /* ---- migration: the old slots and the motion beside them become A..D */
    old_flash();
    sec_pend_clear();
    sec_boot();
    ok = 1;
    for (s = 0; s < 4u; s++)
        ok &= sec_motion_is(s, s == 3u ? 0 : &keepm[s]);
    check("4 -> sections: the first start moves each old slot's motion into its section (D: none)", ok);
    for (cut = 0, ok = 1; cut < 40u; cut++) {
        old_flash();
        sec_pend_clear();
        fail_after = (int)cut;
        sec_boot();
        fail_after = -1;
        sec_boot();
        for (s = 0; s < 4u; s++)
            ok &= sec_motion_is(s, s == 3u ? 0 : &keepm[s]);
    }
    check("... a first start cut at each of 40 flash programs, started again: A..D with their motion", ok);

    /* ---- record in B, save, reload: it plays */
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    song.playing = 0, transport_req = 0;
    make(1);
    project_save(2);                                  /* (C: no motion recorded) */
    n0 = slg.alen[2];
    check("no motion recorded: no chunk, the record as without motion", !strcmp(last_msg, "SAVED") &&
          slg_get(2, sec_rbuf) > 0 && !(sec_rbuf[0] & SEC_MOT) && n0 == sec_body(&proj_tmp.cur, &sec_tmp_dl, bk));
    {   /* a knob recorded while playing (REC armed), then more events */
        song.playing = 1, song.rec = 1;
        memset(&motion, 0, sizeof motion);
        motion_knob(&trk[0], P_CHOR, 33);
        ok = motion.count == 1u && motion.ev[0].param == P_CHOR && motion.ev[0].value == 33;
        song.playing = 0, song.rec = 0;
        motion_end();
        check("REC + a knob while playing: an event recorded", ok);
    }
    rec_motion(10, 1);
    m1 = motion;
    project_save(1);
    n = slg.alen[1];
    check("B saved with 10 events: the chunk in its record (+2 +3 x 10 bytes)", !strcmp(last_msg, "SAVED") &&
          slg_get(1, sec_rbuf) > 0 && (sec_rbuf[0] & SEC_MOT) && n == n0 + 2u + 30u);
    make(0);
    memset(&motion, 0, sizeof motion);
    project_load(1);
    check("... loaded back: its motion is the working one", !strcmp(last_msg, "LOADED") && motion_is(&motion, &m1));
    check("... and it plays: each event's value on its step", plays(&m1));
    slg_boot();                                       /* (a restart) */
    memset(&motion, 0, sizeof motion);
    project_load(1);
    check("... after a restart too", motion_is(&motion, &m1) && plays(&m1));
    project_load(2);
    check("loading C (no motion): no motion plays", !motion.count && !motion.on);
    {
        uint32_t pct1, more1, pct2, more2, before = slg_live_bytes();
        sec_mem(&pct1, &more1);
        make(3);
        rec_motion(64, 3);
        project_save(3);
        sec_mem(&pct2, &more2);
        check("the MEM gauge counts the motion: a section with 64 events takes its record + 194 B",
              slg_live_bytes() - before == SEC_ALIGN(SEC_HEAD + slg.alen[3]) && pct2 >= pct1 &&
              slg.alen[3] == sec_body(&proj_tmp.cur, &sec_tmp_dl, bk) + 2u + 192u);
    }

    /* ---- stored while playing, then written; the song's stage */
    make(5);
    rec_motion(7, 5);
    m2 = motion;
    song.playing = 1;
    section_store(5);
    ok = sec_pend_has(5);
    song.playing = 0;
    sections_write();
    memset(&motion, 0, sizeof motion);
    project_load(5);
    check("SAVE + key while playing: F with its motion in the arena, written when stopped, loads back",
          ok && slg_has(5) && motion_is(&motion, &m2));
    song.playing = 1;
    ok = section_cue(1);
    memset(&motion, 0, sizeof motion);
    arrangement_apply(1);
    check("a live jump to B: staged, applied by the ISR on the bar with B's motion", ok && motion_is(&motion, &m1));
    song.playing = 0, live_req = -1;

    /* ---- a save cut at every flash program: B is the old one (its motion) or the new one (its motion) */
    memcpy(img, nor + SEC_LOG_BASE, sizeof img);
    for (cut = 0, ok = 1, olds = news = 0; cut < 12u; cut++) {
        memcpy(nor + SEC_LOG_BASE, img, sizeof img);
        slg_boot();
        make(1);
        rec_motion(20, 9);
        m2 = motion;
        fail_after = (int)cut;
        project_save(1);
        fail_after = -1;
        slg_boot();                                   /* (the power came back) */
        memset(&motion, 0, sizeof motion);
        project_load(1);
        if (motion_is(&motion, &m1))
            olds++;
        else if (motion_is(&motion, &m2) && plays(&m2))
            news++;
        else
            ok = 0;
    }
    check("B saved again, cut at each of 12 flash programs: the old B or the new one, each with its own motion",
          ok && olds && news);
    printf("motion: the cut save left the old B %u times, the new one %u times\n", olds, news);

    /* ---- the editor's backup: S02 read, B cleared, S02 written back */
    {
        uint32_t len = 0, crc = 0, len2 = 0, crc2 = 0;
        int i = bk_find("S02 ", &len, &crc);
        ok = i >= 0 && len == slg.alen[1] && bk_read((uint32_t)i, len, bk) == len && st_crc32(bk, len) == crc;
        ok &= slg_put(1, 0, 0, 0) == 0 && !project_used(1);
        ok &= bk_write((uint32_t)i, bk, len) == 0;
        cmd(ED_BK_END, (const uint8_t[]){0}, 1);
        memset(&motion, 0, sizeof motion);
        project_load(1);
        ok &= bk_find("S02 ", &len2, &crc2) == i && len2 == len && crc2 == crc;
        check("backup / restore of B (object S02): the record byte for byte, its motion back", ok && motion_is(&motion, &m2));
    }

    /* ---- the reserve: the log full (dense sections with 64 events, then records of the longest size in the
     * ids past this build's: another build's sections count too), the playing section is still saved */
    for (s = 0, ok = 1; s < SEC_ID_SONG && ok; s++) {
        if (s == 1u)
            continue;
        if (s < SEC_IDS) {
            dense(s);
            rec_motion(64, s);
            project_save(s);
            ok = !strcmp(last_msg, "SAVED");
        } else {
            memset(bk, (int)s, sizeof bk);
            ok = !slg_put(s, bk, SEC_REC_MAX, 0);
        }
    }
    check("MEM FULL: dense sections with 64 events each, then the longest records, until the log refuses one",
          !ok);
    full = s - 1u;                                    /* (the one refused) */
    printf("motion: full with %u B of records, the reserve %u B (the longest record, raw with 64 events)\n",
           slg_live_bytes(), SEC_ALIGN(SEC_HEAD + SEC_REC_MAX));
    dense(40);
    rec_motion(64, 4);
    m2 = motion;
    live_sec = 1;
    project_save(1);
    ok = !strcmp(last_msg, "SAVED") && slg_get(1, sec_rbuf) > 0 && (sec_rbuf[0] & SEC_MOT);
    memset(&motion, 0, sizeof motion);
    project_load(1);
    check("... the playing section (B), dense with 64 events, is still saved and loads back with its motion",
          ok && motion_is(&motion, &m2) && plays(&m2));
    if (full < SEC_IDS) {
        dense(full);
        rec_motion(64, full);
        project_save(full);
        ok = !strcmp(last_msg, "MEM FULL");
    } else
        ok = slg_put(full, bk, SEC_REC_MAX, 0) == 1;
    check("... the section refused before is still refused: MEM FULL", ok);
    live_sec = -1;
    printf("motion sections test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}

#else /* FELUCCA_SECTIONS 4: the slots, the motion beside them in their sectors (motion_flash.c) */
static void song_backup(void) {}
static void song_restore(void) {}
static uint32_t arrangement_ready(void) { return 15u; }
static void arrangement_apply(uint32_t s) { (void)s; }
int main(void)
{
    static project_t q;
    static dlrec_t qd;
    motion_store_t m1, m2;
    uint32_t cut, ok, olds = 0, news = 0;
    memset(nor, 0xFF, sizeof nor);
    make(1);
    rec_motion(10, 1);
    m1 = motion;
    proj_capture(&proj_slot[1], &proj_dl[1]);
    ok = proj_put(OBJ_PROJECT0 + 1, &proj_slot[1], &proj_dl[1]) == 0;
    motion_flash_write(OBJ_PROJECT0 + 1, &proj_slot[1]);
    memset(&motion_slot[1], 0, sizeof motion_slot[1]);
    ok &= proj_get(OBJ_PROJECT0 + 1, &proj_slot[1], &proj_dl[1]);
    motion_flash_read(OBJ_PROJECT0 + 1, &proj_slot[1]);
    memset(&motion, 0, sizeof motion);
    make(0);
    proj_apply(&proj_slot[1], &proj_dl[1], 1);
    check("4 slots: B saved with its motion beside it, read back, loaded: it plays", ok && motion_is(&motion, &m1) && plays(&m1));
    for (cut = 0, ok = 1; cut < 40u; cut++) {
        static uint8_t img[0x10000];
        if (!cut)
            memcpy(img, nor + 0x97000u, sizeof img);
        memcpy(nor + 0x97000u, img, sizeof img);
        make(1);
        trk[1].p[P_REV] = 77;
        rec_motion(20, 9);
        m2 = motion;
        proj_capture(&proj_slot[1], &proj_dl[1]);
        fail_after = (int)cut;
        if (!proj_put(OBJ_PROJECT0 + 1, &proj_slot[1], &proj_dl[1]))
            motion_flash_write(OBJ_PROJECT0 + 1, &proj_slot[1]);
        fail_after = -1;
        memset(&proj_slot[1], 0, sizeof proj_slot[1]);
        memset(&motion_slot[1], 0, sizeof motion_slot[1]);
        if (!proj_get(OBJ_PROJECT0 + 1, &q, &qd)) {
            ok = 0;
            continue;
        }
        proj_slot[1] = q, proj_dl[1] = qd;
        motion_flash_read(OBJ_PROJECT0 + 1, &proj_slot[1]);
        memset(&motion, 0, sizeof motion);
        proj_apply(&proj_slot[1], &proj_dl[1], 1);
        if (trk[1].p[P_REV] != 77)
            ok &= motion_is(&motion, &m1), olds++;     /* the old project: its own motion */
        else if (motion_is(&motion, &m2))
            news++;                                     /* the new one, its motion written */
        else
            ok &= !motion.count, news++;                /* the new one, cut before its motion: none, never m1 */
    }
    check("4 slots: a save cut at each of 40 programs: the old B with its motion, or the new B (its motion or none)",
          ok && olds && news);
    printf("motion sections test (4 slots) %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
#endif
