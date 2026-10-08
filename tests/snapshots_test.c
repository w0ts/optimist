/* SPDX-License-Identifier: GPL-3.0-only */
/* Whole-state snapshots (firmware/src/storage/snapshots/snapshots.c, snap_store.c, ed_snap.c) on a simulated NOR, the store of the
 * build (FELUCCA_SECTIONS 16: the section log; 4: the project slots), with and without FELUCCA_MOTION:
 *   - three sections, a song, a working project with motion saved to slot 1; everything changed and saved to slot 2;
 *     changed again; a power cycle; slot 1 loaded: every track, section, the song, the motion and the kit exactly
 *     back; slot 2 the same; another power cycle keeps it; BEFORE LOAD held the state before each load (loading it
 *     swaps)
 *   - a save cut at every flash program: the slot old or new, the other slots, the work and the autosave untouched
 *   - a load cut at every flash program: after the restart the state as before, or BEFORE LOAD holds it (loaded
 *     back: exactly)
 *   - FULL, a damaged slot (refused, nothing changed), USR3's long sample in the way, stopped / quiet only
 *   - the editor: list, export (SN_READ) and import (SN_WRITE) into another slot: the same bytes, loads; a bad chunk,
 *     a stream that is not a snapshot: refused, the slot as it was
 *   - another build: "write FILE" saves a snapshot (FM6 on part 3, six sections A..F, a 20-part song) into a NOR
 *     image; "read FILE" in a build without FM6 / with 4 sections loads it: the part keeps FM6 (MISSING says so),
 *     A..D in the slots, E..F reported. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t nor[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)nor + 0xA0000u + (k) * 0x14000u)   /* (USR1..3 read from the NOR) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"
#define MISS_SCAN_ONLY 1
#include "../firmware/src/storage/miss.c"

static long progs, cut_at = -1;
static int dead;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (dead || progs++ == cut_at)
        return dead = 1, -9;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (dead)
        return -9;
    if (progs++ == cut_at) {                          /* (cut: half its bytes, then nothing more) */
        for (i = 0; i < n / 2u; i++)
            nor[off + i] &= ((const uint8_t *)src)[i];
        return dead = 1, -9;
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
#if FELUCCA_MOTION
#include "../firmware/src/storage/motion_flash.c"
#define MOTION_SAVED(obj, p) motion_flash_write(obj, p)
#define MOTION_READ(obj, p) motion_flash_read(obj, p)
#define MOTION_HASH() motion_hash()
#else
#define MOTION_SAVED(obj, p) ((void)0)
#define MOTION_READ(obj, p) ((void)0)
#define MOTION_HASH() 0u
#endif

/* ---- project.c's firmware side, as the device has it */
static struct { int force; uint8_t arm, arm_t; } ui;
static uint8_t sync_reload, flash_ok = 1, song_dirty, quiet = 1;
static uint16_t sec_dirty;
static char last_msg[64];
static uint32_t last_st;
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
    last_st = 0;
}
static void ui_message(const char *m) { ui_say(m, ""); }
static void ui_say_st(uint32_t st, const char *a, const char *b) { ui_say(a, b); last_st = st; }
static void song_backup(void) {}
static void song_restore(void) {}
static project_t autosave_buf;
static dlrec_t autosave_dl;
static uint32_t autosave_hash;
static int audio_quiet(void) { return quiet; }
static arr_rec_t saved_rec;                            /* the settings record's chain (persist_t) */
static uint32_t settings_saved;
#if SEC_LOGGED
static uint16_t song_tag;
static void settings_save(void) { arr_to_rec(&saved_rec, &arrangement, song_tag); settings_saved++; }
#else
static void settings_save(void) { arr_to_rec(&saved_rec, &arrangement, 0); settings_saved++; }
#endif
static void project_apply(const project_t *p, const dlrec_t *d)
{
    proj_apply(p, d, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
}
#if SEC_LOGGED
#include "../firmware/src/storage/sections/sections.c"
#else
static uint32_t arrangement_ready(void) { return 15u; }
static void arrangement_apply(uint32_t s) { (void)s; }
static void proj_slots_drop(void) {}
static void sections_write(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (((sec_dirty >> i) & 1u) && proj_put(OBJ_PROJECT0 + i, &proj_slot[i], &proj_dl[i]) == 0) {
            MOTION_SAVED(OBJ_PROJECT0 + i, &proj_slot[i]);
            sec_dirty &= (uint16_t)~(1u << i);
        }
}
static void project_save(uint32_t s)                    /* (PROJECT > SAVE: the slot and flash) */
{
    proj_capture(&proj_slot[s], &proj_dl[s]);
    sec_dirty |= (uint16_t)(1u << s);
    sections_write();
}
#endif
#include "../firmware/src/storage/snapshots/snapshots.c"
/* the editor's reply builder (editor.c) */
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
static uint32_t pack7(const uint8_t *b, uint32_t n, uint8_t *o)
{
    uint32_t k = 0;
    while (n) {
        uint32_t c = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < c; j++)
            m |= (uint32_t)(b[j] >> 7) << j;
        o[k++] = (uint8_t)m;
        for (j = 0; j < c; j++)
            o[k++] = b[j] & 0x7Fu;
        b += c, n -= c;
    }
    return k;
}
#include "../firmware/src/drums/drum_kits.c"
#include "../firmware/src/io/editor/ed_drums.c"                 /* (ed_pack7) */
#include "../firmware/src/io/editor/ed_snap.c"
static int flushed;
#define BK_FLUSH() (flushed++)
#include "../firmware/src/io/editor/ed_backup.c"                /* (the SNAP object) */

static int bad;
#if FELUCCA_SL24_XSTEP
#if !SEC_LOGGED
/* SECTIONS 4 keeps the working extras in RAM only (no log, so no autosave record: stepx_proj.c): a restart loses them
 * until a snapshot is loaded (then they are the snapshot's) */
static int xw_lost;
static int sn_load_t(uint32_t k)
{
    int r = sn_load(k);
    xw_lost = r == SNE_OK ? 0 : xw_lost;
    return r;
}
#define sn_load sn_load_t
#endif
static void sx_bind(void)                              /* (persist_boot: the working extras, a store per buffer) */
{
    sx_init();
    (void)sx_for(&proj_tmp.cur, 1);
#if SEC_LOGGED
    (void)sx_for(&sec_stage_p, 1);
#endif
    (void)sx_for(&autosave_buf, 1);
}
#endif
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* ---- states */
/* the tracks as seed s: engines, presets, every value off its default, steps on every track, a kit, swing, BPM */
static void make(uint32_t s)
{
    static const uint8_t ENG[3][NPART] = {{1, 4, 9}, {0, 2, 3}, {9, 1, 0}};   /* (UIDs: DIGITAL SAMPLE FM6 ...) */
    uint32_t i, k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        if (k < NPART) {
            uint32_t e = eng_slot(ENG[s % 3u][k]);
            host_preset_req(t, e, (1u + s + k) % (ENGINES[e]->npresets ? ENGINES[e]->npresets : 1u));
        }
        for (i = 0; i < P_E0; i++) {
            const param_desc_t *d = &TP[i];
            int32_t span = d->max - d->min;
            if (span > 0)
                t->p[i] = (int16_t)(d->min + (int32_t)((i * 7u + k * 3u + s * 11u + 1u) % (uint32_t)(span + 1)));
        }
        t->p[P_SLEN] = (int16_t)(8 + 4 * (int32_t)((s + k) % 5u));
        for (i = 0; i < 8u; i++)
            if (k == TRK_DRUM)
                dstep_set(&t->dstep[i], (i * 3u + s) % 16u, LV_NORM, 0);
            else {
                t->step[i].note[0] = (uint8_t)(40u + i + k + s);
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
            }
    }
#if FELUCCA_ANALOG2
    for (i = P_A2WAVE; i < P_E0; i++)
        trk[TRK_DRUM].p[i] = TP[i].def;
#endif
    TDRUM->p[P_E0] = (int16_t)((3u + s) % 8u);
#if FELUCCA_SL24_XSTEP
    for (k = 0; k < NTRK; k++) {                      /* SLOOP 2.4's step extras: by seed, some of them none */
        stepx_clear(STEPX(k));
        if (s % 4u == 3u && k == 1u)
            continue;
        STEPX(k)->micro[(s + k) % 64u] = (int8_t)(-3 - (int)k - (int)(s % 5u));
        (void)stepx_lock_set(STEPX(k), (s + 2u * k) % 64u, P_PAN, (int16_t)(100 + s + k));
        (void)stepx_lock_set(STEPX(k), (s + 2u * k) % 64u, P_LEVEL, (int16_t)(-7 - (int)s));
        stepx_fill_set(STEPX(k), (s + 5u) % 64u, k & 1u ? FC_FILL : FC_NOFILL);
    }
#endif
    for (k = 0; k < NPART; k++) {                     /* (the FM6 functions as after any load: proj_apply) */
        fm6_fn_reset(fm6_ed[k]);
        fm6_set(fm6_ed[k], FN_PBUP, (int32_t)((s + k) % 12u));
        fm6_fnok[k] = 1;
    }
    song.g[G_SWING] = (int16_t)(10 + s * 7u % 60u);
    song.g[G_BPM] = (int16_t)(90 + s * 5u % 100u);
    song.g[G_MIDI] = 0;
    memset(&dl, 0, sizeof dl);
    if (s & 1u)
        dl.ofs[2][DE_CUT] = (int8_t)(-(int32_t)s % 30);
#if FELUCCA_MOTION
    memset(&motion, 0, sizeof motion);
    for (i = 0; i < 3u + s % 5u; i++)
        motion_set_event(&trk[i % 3u], i, P_CHOR, (int32_t)((s * 13u + i * 7u) % 100u));
    motion.on = 7;
#endif
}
static void song_make(uint32_t s, uint32_t parts)
{
    uint32_t i;
    arr_defaults(&arrangement);
    arrangement.count = (uint8_t)parts;
    arrangement.loop = (uint8_t)(s & 1u);
    for (i = 0; i < parts; i++)
        arrangement.entry[i].scene = (uint8_t)((i + s) % 3u), arrangement.entry[i].bars = (uint8_t)(1u + (i * 3u + s) % 8u);
}
static void song_store(void)
{
#if SEC_LOGGED
    sec_song_put(&arrangement, &song_tag);
#endif
    settings_save();
}
/* a state: three sections from seeds a, a+1, a+2 (sections a % 4 .. : A B C, or B C D), a song, the work from a+5 */
static void state_make(uint32_t a)
{
    uint32_t s;
    for (s = 0; s < 3u; s++) {
        make(a + s);
        project_save((a + s) % 4u);
    }
    song_make(a, ARR_STEPS > 16u && (a & 1u) ? 20u : 4u);
    song_store();
    make(a + 5u);
    proj_capture(&autosave_buf, &autosave_dl);       /* (autosave_tick, once the panel rests) */
    if (proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl) == 0) {
        MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_SL24_XSTEP && SEC_LOGGED
        (void)sx_log_put(SX_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);
#endif
    }
}

/* everything a snapshot holds, as this build keeps it */
typedef struct {
    project_t work;
    dlrec_t wdl;
#if FELUCCA_MOTION
    motion_store_t mot;
#endif
    uint16_t secn[SN_SECS];
    uint8_t sec[SN_SECS][SEC_REC_MAX];
    arr_config_t arr;
#if FELUCCA_SL24_XSTEP
    stepx_t xwork[NTRK];                              /* the working extras */
    uint16_t xsn[SN_SECS];                            /* each section's, in the stored form (0: none) */
    uint8_t xs[SN_SECS][STEPX_ENC_MAX];
#endif
} want_t;
static want_t W1, W2, W3, G;
static void state_get(want_t *w)
{
    uint32_t i;
    memset(w, 0, sizeof *w);
    song.g[G_MIDI] = 0;
    proj_capture(&w->work, &w->wdl);
#if FELUCCA_MOTION
    w->mot = motion;
    w->mot.psum = 0;
#endif
    for (i = 0; i < SN_SECS; i++) {
        w->secn[i] = (uint16_t)sn_sec_rec(i);
        memcpy(w->sec[i], SN_REC, w->secn[i]);
    }
    w->arr = arrangement;
#if FELUCCA_SL24_XSTEP
    for (i = 0; i < NTRK; i++)
        w->xwork[i] = *STEPX(i);
    for (i = 0; i < SN_SECS; i++)
        if (w->secn[i] && (w->xsn[i] = (uint16_t)sn_xs_sec(i)) != 0)
            memcpy(w->xs[i], sx_rbuf, w->xsn[i]);
#endif
}
static int state_is(const want_t *w, const char *what)
{
    uint32_t i;
    state_get(&G);
    if (memcmp(&G.work, &w->work, sizeof G.work) || memcmp(&G.wdl, &w->wdl, sizeof G.wdl)) {
        const uint8_t *a = (const uint8_t *)&G.work, *b = (const uint8_t *)&w->work;
        for (i = 0; i < sizeof G.work && a[i] == b[i]; i++)
            ;
        printf("  %s: the work differs at byte %u (fm6 %u, fm6_on %u, fm6_fn %u, size %u): %d vs %d\n", what, i, (unsigned)__builtin_offsetof(project_t, fm6), (unsigned)__builtin_offsetof(project_t, fm6_on), (unsigned)__builtin_offsetof(project_t, fm6_fn), (unsigned)sizeof(project_t), a[i], b[i]);
        return 0;
    }
#if FELUCCA_MOTION
    if (G.mot.count != w->mot.count || G.mot.on != w->mot.on || memcmp(G.mot.ev, w->mot.ev, 3u * w->mot.count)) {
        printf("  %s: the motion differs (%u events, was %u)\n", what, G.mot.count, w->mot.count);
        return 0;
    }
#endif
    for (i = 0; i < SN_SECS; i++)
        if (G.secn[i] != w->secn[i] || memcmp(G.sec[i], w->sec[i], w->secn[i])) {
            printf("  %s: section %c differs (%u B, was %u)\n", what, 'A' + i, G.secn[i], w->secn[i]);
            return 0;
        }
#if FELUCCA_SL24_XSTEP
    for (i = 0; i < NTRK; i++)
        if (
#if !SEC_LOGGED
            !xw_lost &&
#endif
            memcmp(&G.xwork[i], &w->xwork[i], sizeof(stepx_t))) {
            printf("  %s: the work's step extras of track %u differ\n", what, i);
            return 0;
        }
    for (i = 0; i < SN_SECS; i++)
        if (G.xsn[i] != w->xsn[i] || memcmp(G.xs[i], w->xs[i], w->xsn[i])) {
            printf("  %s: section %c's step extras differ (%u B, was %u)\n", what, 'A' + i, G.xsn[i], w->xsn[i]);
            return 0;
        }
#endif
    if (G.arr.count != w->arr.count || G.arr.loop != w->arr.loop || memcmp(G.arr.entry, w->arr.entry, 2u * w->arr.count)) {
        printf("  %s: the song differs (%u parts, was %u)\n", what, G.arr.count, w->arr.count);
        return 0;
    }
    return 1;
}

/* a power cycle: RAM as at power-on, then what persist_boot and autosave_resume read */
static void power_cycle(void)
{
    uint32_t i;
    dead = 0, cut_at = -1;
    make(99);
#if FELUCCA_SL24_XSTEP
    sx_bind();                                        /* (RAM as at power-on: no extras, no store belongs to a project) */
    for (i = 0; i < NTRK; i++)
        stepx_clear(STEPX(i));
    for (i = 0; i < SX_AUX; i++)
        sx_aux[i].psum = 0, sx_clear_all(sx_aux[i].x);
#if !SEC_LOGGED
    xw_lost = 1;
#endif
#endif
    host_tracks_init();
    memset(&dl, 0, sizeof dl);
#if FELUCCA_MOTION
    memset(&motion, 0, sizeof motion);
    memset(motion_aux, 0, sizeof motion_aux);
    memset(motion_aux_p, 0, sizeof motion_aux_p);
#endif
    arr_from_rec(&arrangement, &saved_rec);
#if SEC_LOGGED
    song_tag = (uint16_t)arr_tag_of(&saved_rec);
    sec_pend_clear();
    sec_boot();
    if (song_tag && !sec_song_get(&arrangement, song_tag))
        song_tag = 0;
#else
    for (i = 0; i < 4u; i++) {
        proj_slot[i].magic = 0;
        if (proj_get(OBJ_PROJECT0 + i, &proj_slot[i], &proj_dl[i]))
            MOTION_READ(OBJ_PROJECT0 + i, &proj_slot[i]);
        else
            proj_slot[i].magic = 0;
    }
#endif
    sec_dirty = 0;
    sn.up = 0;
    sn_boot();
    memset(&autosave_buf, 0, sizeof autosave_buf);
    if (proj_get(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl)) {
        MOTION_READ(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_SL24_XSTEP && SEC_LOGGED
        sx_log_get(SX_ID_AUTO, autosave_buf.sum, &autosave_buf);   /* (autosave_resume) */
#endif
        project_apply(&autosave_buf, &autosave_dl);
    }
    (void)i;
}
static void fresh_flash(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&saved_rec, 0, sizeof saved_rec);
    arr_defaults(&arrangement);
    arr_to_rec(&saved_rec, &arrangement, 0);
    power_cycle();
}

/* ---- the editor: a command and its reply (after the 5-byte header in the firmware: here ed_out[0..]) */
static int cmd(uint32_t c, const uint8_t *a, uint32_t na) { ed_n = 0; return ed_snap(c, a, na); }
static void put7(uint8_t *a, uint32_t v, uint32_t k) { while (k--) { *a++ = (uint8_t)(v & 0x7Fu); v >>= 7; } }
static uint32_t get7(const uint8_t *a, uint32_t k) { return sn_r7(a, k); }
static uint32_t export_slot(uint32_t k, uint8_t *out)  /* SN_READ to the end: the stream, its length */
{
    uint32_t off = 0, n;
    uint8_t a[4];
    for (;;) {
        a[0] = (uint8_t)k;
        put7(a + 1, off, 3);
        if (!cmd(ED_SN_READ, a, 4) || get7(ed_out + 1, 3) != off)
            return 0;
        n = ed_unpack7(ed_out + 9, ed_n - 9u, out + off, SN_CHUNK);
        if (!n)
            return off;
        if (st_crc32(out + off, n) != get7(ed_out + 4, 5))
            return 0;
        off += n;
    }
}
/* SN_WRITE begin, data, commit; bad_at: a chunk sent with a wrong CRC first (-1 none); -> the commit's rc */
static uint32_t import_slot(uint32_t k, const uint8_t *s, uint32_t n, long bad_at)
{
    uint8_t a[320];
    uint32_t off, rc;
    a[0] = 0, a[1] = (uint8_t)k;
    put7(a + 2, n, 3);
    put7(a + 5, st_crc32(s, n), 5);
    if (!cmd(ED_SN_WRITE, a, 10) || ed_out[2])
        return 100u + ed_out[2];
    for (off = 0; off < n;) {
        uint32_t c = n - off < SN_CHUNK ? n - off : SN_CHUNK;
        a[0] = 1, a[1] = (uint8_t)k;
        put7(a + 2, off, 3);
        put7(a + 5, st_crc32(s + off, c) ^ ((long)off == bad_at ? 1u : 0u), 5);
        cmd(ED_SN_WRITE, a, 10u + pack7(s + off, c, a + 10));
        rc = ed_out[5];
        if ((long)off == bad_at) {
            bad_at = -1;
            if (rc != SNE_CRC)
                return 200u + rc;
            continue;                                  /* (sent again) */
        }
        if (rc)
            return 300u + rc;
        off += c;
    }
    a[0] = 2, a[1] = (uint8_t)k;
    cmd(ED_SN_WRITE, a, 2);
    return ed_out[2];
}


#if FELUCCA_SL24_XSTEP
/* ---- the editor's backup objects (ed_backup.c): the index of a tag, an object read whole, an object written */
static int bk_find(const char *tag)
{
    uint8_t a[2] = {1};
    uint32_t i, p;
    ed_n = 0;
    ed_backup(ED_BK_LIST, a, 1);
    for (i = 0, p = 10; i < ed_out[1]; i++, p += 14)
        if (!memcmp(ed_out + p, tag, 4))
            return (int)i;
    return -1;
}
static uint32_t bk_get(int idx, uint8_t *out)
{
    uint8_t a[4];
    uint32_t off = 0, n;
    for (;;) {
        a[0] = (uint8_t)idx;
        put7(a + 1, off, 3);
        ed_n = 0;
        ed_backup(ED_BK_READ, a, 4);
        n = ed_unpack7(ed_out + 9, ed_n - 9u, out + off, 256);
        if (!n)
            return off;
        off += n;
    }
}
static uint32_t bk_put(int idx, const uint8_t *d, uint32_t n)   /* COMMIT's rc; 10 + a BEGIN's, 20 + a DATA's */
{
    uint8_t a[320];
    uint32_t off, c;
    a[0] = (uint8_t)idx;
    put7(a + 1, n, 3);
    put7(a + 4, st_crc32(d, n), 5);
    ed_n = 0;
    if (!ed_backup(ED_BK_BEGIN, a, 9) || ed_out[1])
        return 10u + ed_out[1];
    for (off = 0; off < n; off += c) {
        c = n - off > 256u ? 256u : n - off;
        a[0] = (uint8_t)idx;
        put7(a + 1, off, 3);
        put7(a + 4, st_crc32(d + off, c), 5);
        ed_n = 0;
        if (!ed_backup(ED_BK_DATA, a, 9u + pack7(d + off, c, a + 9)) || ed_out[4])
            return 20u + ed_out[4];
    }
    a[0] = (uint8_t)idx;
    ed_n = 0;
    ed_backup(ED_BK_COMMIT, a, 1);
    return ed_out[1];
}
#endif

static void cross_write(const char *file)
{
    FILE *f;
    fresh_flash();
    {
        uint32_t s;
        for (s = 0; s < 6u; s++) {
            make(s);
            project_save(s);
        }
    }
    song_make(1, ARR_STEPS > 16u ? 20u : 4u);
    song_store();
    make(3);                                           /* (part 3: FM6) */
    check("another build: a snapshot with FM6 on part 3, six sections, a song", sn_save(0, "CROSS") == SNE_OK);
    f = fopen(file, "wb");
    fwrite(nor, 1, sizeof nor, f);
    fclose(f);
}
static void cross_read(const char *file)
{
    FILE *f = fopen(file, "rb");
    uint32_t got = 0;
    if (!f || fread(nor, 1, sizeof nor, f) != sizeof nor) {
        check("another build's NOR image read", 0);
        return;
    }
    fclose(f);
    memset(&saved_rec, 0, sizeof saved_rec);
    arr_defaults(&arrangement);
    arr_to_rec(&saved_rec, &arrangement, 0);
    power_cycle();
    {
        sn_info_t in;
        check("another build's snapshot listed (amber: another build)", sn_info(0, &in) && !memcmp(in.name, "CROSS", 6));
    }
    miss_gen = 0;
    check("... loaded", sn_load(0) == SNE_OK);
    check("... MISSING bumped by the load", miss_gen != 0);
#if !FELUCCA_ENG_FM6
    got = miss_scan();
    check("... FM6 left out: part 3 plays a stand-in and keeps FM6 (MISSING lists it)", proj_orph_uid(2) == 9u && got >= 1u);
#endif
#if !SEC_LOGGED
    check("... 4 sections: A..D in the slots, E..F reported skipped", proj_ok(&proj_slot[0]) && proj_ok(&proj_slot[3]) &&
          (sn_note & SNN_SECS) != 0);
    check("... the 20-part song names E..F? no: a 4-part chain loads, or the default with a notice",
          arrangement.count <= ARR_STEPS);
#else
    check("... every section A..F there", sn_sec_len(0) && sn_sec_len(5) && !sn_sec_len(6));
#endif
    (void)got;
}

int main(int argc, char **argv)
{
    uint32_t i, n, ok;
    long c, total;
    static uint8_t s1[SN_MAX], s2[SN_MAX];
    if (argc == 3 && !strcmp(argv[1], "write")) {
        cross_write(argv[2]);
        return bad != 0;
    }
    if (argc == 3 && !strcmp(argv[1], "read")) {
        cross_read(argv[2]);
        printf("snapshots (another build) %s\n", bad ? "FAILED" : "passed");
        return bad != 0;
    }
    fresh_flash();
    check("an erased area: every slot empty", sn_size(0) == 0u && sn_size(SN_BAK) == 0u && sn_free_count() == SN_SECTORS);
    {   /* sizes (docs/SNAPSHOTS.md): the power-on state; a typical one (3 sections of 16 steps a track, a few edits) */
        uint32_t p0, s, k, t;
        host_tracks_init();
        sn_save(2, 0);
        p0 = sn_size(2);
        for (s = 0; s < 3u; s++) {
            host_tracks_init();
            for (i = 0; i < NTRK; i++) {
                trk[i].p[P_SLEN] = 16;
                for (k = 0; k < 16u; k++)
                    if (i == TRK_DRUM)
                        dstep_set(&trk[i].dstep[k], (k + s) % 16u, LV_NORM, 0);
                    else if (k % 2u == 0u) {
                        trk[i].step[k].note[0] = (uint8_t)(48u + 3u * s + k + i);
                        trk[i].step[k].n = 1;
                        trk[i].step[k].time = ST_NOTE;
                    }
            }
            trk[1].p[P_REV] = (int16_t)(20 + 10 * s);
            project_save(s);
        }
        song_make(0, 4);
        song_store();
        sn_save(2, 0);
        t = sn_size(2);
        printf("  sizes: power-on %u B, typical (3 sections of 16 steps, a song, the work) %u B (%u sector%s)\n", p0, t,
               sn.slot[2].parts, sn.slot[2].parts > 1u ? "s" : "");
        check("the typical snapshot fits one sector", sn.slot[2].parts == 1u);
        fresh_flash();
    }
    state_make(0);
    state_get(&W1);
    check("state 1 (3 sections, a song, the work with motion) saved to slot 1", sn_save(0, 0) == SNE_OK);
    {
        sn_info_t in;
        sn_info(0, &in);
        printf("  slot 1: %u B (%u sector%s), named \"%.12s\"\n", sn_size(0), sn.slot[0].parts, sn.slot[0].parts > 1u ? "s" : "", in.name);
        check("... its name made from the work (BPM, sections)", in.name[0] >= '0' && in.name[0] <= '9' && strchr(in.name, ' '));
    }
    state_make(1);
    state_get(&W2);
    check("everything changed (sections, song, work), saved to slot 2", sn_save(1, "SECOND") == SNE_OK);
    state_make(2);
    state_get(&W3);
    power_cycle();
    check("a power cycle: the work as it was (autosave), slots 1 and 2 there", state_is(&W3, "after the restart") &&
          sn_size(0) && sn_size(1));
    check("slot 1 loaded", sn_load(0) == SNE_OK && last_st == 0u);
    check("... every track, section, the song, the motion, the kit exactly as saved", state_is(&W1, "slot 1"));
    check("... BEFORE LOAD holds the state before the load", sn_size(SN_BAK) != 0u);
    check("slot 2 loaded", sn_load(1) == SNE_OK && state_is(&W2, "slot 2"));
    power_cycle();
    check("a power cycle after the load: still slot 2's state (work, log, song saved)", state_is(&W2, "slot 2 after a restart"));
#if FELUCCA_SL24_XSTEP && !SEC_LOGGED
    for (i = 0; i < NTRK; i++)
        stepx_clear(&W2.xwork[i]);                     /* (SECTIONS 4: the working extras did not survive the restart) */
#endif
    check("BEFORE LOAD loaded (it held slot 1's state): back, and it swapped (now holds slot 2's)",
          sn_load(SN_BAK) == SNE_OK && state_is(&W1, "BEFORE LOAD") && sn_load(SN_BAK) == SNE_OK && state_is(&W2, "BEFORE LOAD again"));

    /* the device page's checks */
    song.playing = 1;
    check("SAVE while playing: STOP FIRST, nothing written", sn_save(2, 0) == SNE_PLAYING && !sn_size(2));
    song.playing = 0;
    quiet = 0;
    check("SAVE while a note rings: WAIT FOR SILENCE", sn_save(2, 0) == SNE_RING && !sn_size(2));
    quiet = 1;
    sn_ui(1, 2);
    check("CLEAR of an empty slot: EMPTY SLOT (no colour)", !strcmp(last_msg, "EMPTY SLOT") && last_st == 0u);
    sn_ui(2, 2);
    check("the page's SAVE: green SNAP 3 SAVED", !strcmp(last_msg, "SNAP 3 SAVED") && last_st == 1u);
    sn_ui(1, 2);
    check("the page's CLEAR: SNAP 3 CLEARED", !strcmp(last_msg, "SNAP 3 CLEARED") && !sn_size(2));

    /* a save cut at every flash program and erase: the slot old or new, nothing else changed */
    for (ok = 1, total = 0, c = 0;; c++) {
        static want_t before;
        fresh_flash();
        state_make(0);
        sn_save(0, 0);
        sn_save(1, 0);
        state_make(1);
        state_get(&before);
        n = sn.slot[1].seq;
        progs = 0, cut_at = c;
        sn_save(0, 0);
        total = progs;
        power_cycle();
        ok &= state_is(&before, "the work after a cut save") && sn.slot[1].state == SN_OK && sn.slot[1].seq == n &&
              sn.slot[0].state == SN_OK;
        if (c > total + 1)
            break;
    }
    check("a save cut at each erase and program: slot 1 old or new, slot 2, the work and the autosave untouched", ok && total > 4);

    /* a load cut at every flash program and erase: the state as before, or BEFORE LOAD holds it */
    for (ok = 1, total = 0, c = 0;; c++) {
        static want_t before;
        fresh_flash();
        state_make(0);
        sn_save(0, 0);
        state_make(1);
        state_get(&before);
        progs = 0, cut_at = c;
        sn_load(0);
        total = progs;
        power_cycle();
        if (!state_is(&before, "the state after a cut load")) {
            n = sn_load(SN_BAK) == SNE_OK && state_is(&before, "BEFORE LOAD after a cut load");
            ok &= n;
        }
        if (c > total + 1)
            break;
    }
    check("a load cut at each erase and program: the state as before, or BEFORE LOAD brings it back exactly", ok && total > 8);

    /* loading BEFORE LOAD (the swap) with a write failing at each step: it is never lost, not even in RAM */
    for (ok = 1, total = 0, c = 0;; c++) {
        static want_t before, bak;
        fresh_flash();
        state_make(0);
        sn_save(0, 0);
        state_make(1);
        sn_load(0);                                   /* (B: state 1's work) */
        state_get(&bak);                              /* (now: slot 1's) */
        sn_save(1, 0);
        state_make(2);
        state_get(&before);
        progs = 0, cut_at = c;
        n = sn_load(SN_BAK) == SNE_OK;
        total = progs;
        dead = 0, cut_at = -1;                        /* (the flash works again, no restart) */
        if (!n)
            ok &= sn.slot[SN_BAK].state == SN_OK && sn_free_count() < SN_SECTORS;
        power_cycle();
        ok &= sn.slot[SN_BAK].state == SN_OK;
        if (c > total + 1)
            break;
    }
    check("loading BEFORE LOAD with a write failing at each step: B never shows empty nor loses its sectors", ok && total > 8);

#if FELUCCA_SL24_XSTEP
    {   /* SLOOP 2.4's step extras: records of their own in the stream, the real read path, an older stream, the backup */
        static uint8_t a1[SN_MAX], a2[SN_MAX];
        static stepx_t exp[NTRK];
        sn_info_t in;
        uint32_t at, kind, id, nn, b, o, len, nx = 0;
        fresh_flash();
        state_make(0);
        state_get(&W1);
        check("step extras: saved with the snapshot", sn_save(0, 0) == SNE_OK);
        sn_info(0, &in);
        for (at = in.ilen; sn_next(&sn.slot[0], &at, &kind, &id, &nn, &b) > 0;)
            nx += kind == SNR_XSTEP;
        check("... as records of their own: the work's and each section's (SECTIONS 4: the work's)", nx == (SEC_LOGGED ? 4u : 1u));
        state_make(1);
        check("... loaded back after other work: slot 1's", sn_load(0) == SNE_OK && state_is(&W1, "extras after a load"));
        make(5);                                       /* (the work was made from seed 5) */
        for (o = 0, nn = 1; o < NTRK; o++)
            nn &= !memcmp(STEPX(o), &W1.xwork[o], sizeof(stepx_t)) && !stepx_is_empty(STEPX(o));
        check("... the working extras are the snapshot's (not empty)", nn);
#if SEC_LOGGED
        {
            const sx_store_t *m;
            make(0);                                   /* (section A: seed 0) */
            for (o = 0; o < NTRK; o++)
                exp[o] = *STEPX(o);
            check("... section A read as the sequencer reads it: its extras are seed 0's", sec_read(0, &proj_tmp.cur, &sec_tmp_dl) &&
                  (m = sx_for(&proj_tmp.cur, 0)) != 0 && m->psum == proj_tmp.cur.sum && !memcmp(m->x, exp, sizeof exp));
        }
#endif
        /* an older stream (no XSTEP records) through the editor's import: loads with none */
        fresh_flash();
        state_make(0);
        state_get(&W1);
        sn_save(0, 0);
        len = export_slot(0, a1);
        memcpy(a2, a1, ((const sn_info_t *)(const void *)a1)->ilen);
        o = ((const sn_info_t *)(const void *)a1)->ilen;
        for (at = o; at + 4u <= len; at += 4u + nn) {
            nn = (uint32_t)a1[at + 2u] | (uint32_t)a1[at + 3u] << 8;
            if (a1[at] != SNR_XSTEP)
                memcpy(a2 + o, a1 + at, 4u + nn), o += 4u + nn;
        }
        check("an older snapshot (no extras records): imported", o < len && import_slot(1, a2, o, -1) == 0);
        state_make(1);
        for (id = 0; id < NTRK; id++)
            stepx_clear(&W1.xwork[id]);
        memset(W1.xsn, 0, sizeof W1.xsn);
        check("... loads with no extras (the work's and the sections' cleared)", sn_load(1) == SNE_OK && state_is(&W1, "an older snapshot"));
#if SEC_LOGGED && !FELUCCA_MOTION                      /* (the motion has objects of its own: motion_sections_test) */
        {   /* the backup: sections, autosave, drum records and the extras object; into a wiped device */
            static const char *const TAG[6] = {"DLNS", "S01 ", "S02 ", "S03 ", "AUTO", "XSTP"};
            static uint8_t d[6][SEC_REC_MAX + 64];
            int ix[6];
            uint32_t ln[6], rc = 0, k;
            uint8_t z[1] = {0};
            fresh_flash();
            state_make(0);
            state_get(&W1);
            arr_defaults(&W1.arr);                     /* (the song is the settings' object, not in this list) */
            for (k = 0, nn = 1; k < 6u; k++) {
                ix[k] = bk_find(TAG[k]);
                ln[k] = ix[k] >= 0 ? bk_get(ix[k], d[k]) : 0u;
                nn &= ix[k] >= 0 && ln[k] > 0u;
            }
            check("backup: XSTP (a raw log object, kind 5) is listed with data beside the sections, AUTO and DLNS", nn);
            fresh_flash();
            for (k = 0; k < 6u; k++)
                rc |= bk_put(ix[k], d[k], ln[k]);
            ed_n = 0;
            ed_backup(ED_BK_END, z, 1);
            check("... written back into a wiped device: every object accepted", rc == 0);
            power_cycle();
            check("... after a restart: sections, the work and every step extras exactly as backed up", state_is(&W1, "after a restore"));
            /* a bad pack is refused and writes nothing */
            fresh_flash();
            d[5][0] = 17u;                             /* (an id past the autosave's) */
            rc = bk_put(ix[5], d[5], ln[5]);
            ed_n = 0;
            ed_backup(ED_BK_END, z, 1);
            check("... an XSTP object that is not one (id 17): refused at the commit (rc 2)", rc == 2u && !slg_has(SX_ID0) && !slg_has(SX_ID_AUTO));
        }
#endif
    }
#endif
    /* damaged, FULL, USR3 in the way */
    fresh_flash();
    state_make(0);
    sn_save(0, 0);
    state_make(1);
    state_get(&W2);
    nor[sn_off(sn.slot[0].sec[0]) + SN_HEAD + 60] ^= 0x10;
    power_cycle();
    check("a damaged slot: LOAD refused (red), nothing changed", sn_load(0) == SNE_BAD && state_is(&W2, "after a refused load"));
    sn_ui(0, 0);
    check("... the page says SNAPSHOT DAMAGED in red", !strcmp(last_msg, "SNAPSHOT DAMAGED") && last_st == 3u);
    for (i = 0, n = 0; i < 2u * SN_SECTORS && !n; i++)
        n = sn_save(i % FELUCCA_SNAPSHOTS, 0) == SNE_FULL;
    check("saves until FULL (the damaged slot keeps its sectors)", n || sn_free_count() >= 1u);
    {
        smp_user_hdr_t h;
        fresh_flash();
        memset(&h, 0, sizeof h);
        h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = 1, h.data_len = 60000;   /* (a 64 KiB build's USR3) */
        memcpy(nor + SMP_USER_BASE + 2u * SMP_USER_SIZE, &h, sizeof h);
        check("a long USR3 sample of a build without snapshots: SAVE refused, the area untouched",
              sn_save(0, 0) == SNE_USR3 && nor[SN_BASE] == 0xFFu);
        memset(nor + SMP_USER_BASE + 2u * SMP_USER_SIZE, 0xFF, 4096);
    }

    /* the editor: list, export, import, refusals */
    fresh_flash();
    state_make(0);
    sn_save(0, "EXPORT ME");
    state_get(&W1);
    {
        uint8_t a[16] = {1};
        ok = cmd(ED_SN_LIST, a, 1) && ed_out[0] == 1u && ed_out[1] == FELUCCA_SNAPSHOTS && ed_out[2] == SN_NSLOT &&
             ed_out[3] == SN_SECTORS;
        check("SN_LIST: the version, the slots, the area", ok);
        n = export_slot(0, s1);
        check("SN_READ: the stream exported, CRC per chunk", n == sn_size(0) && n > 0u);
        check("SN_WRITE: imported into slot 4 (a bad chunk sent again)", import_slot(3, s1, n, 256) == SNE_OK);
        check("... the same bytes", export_slot(3, s2) == n && !memcmp(s1, s2, n));
        state_make(1);
        check("... and it loads: the state exported", sn_load(3) == SNE_OK && state_is(&W1, "imported"));
        memcpy(s2, s1, n);
        s2[48] = 9;                                    /* (the first record's kind) */
        i = sn.slot[3].seq;
        check("an import that is not a snapshot: refused at the commit, slot 4 as it was",
              import_slot(3, s2, n, -1) == SNE_FORMAT && sn.slot[3].seq == i && sn.slot[3].state == SN_OK);
        a[0] = 3, a[1] = 0;
        memcpy(a + 2, "RENAMED", 8);
        n = sn.slot[0].seq;
        check("SN_OP rename: done, a new version of the slot", cmd(ED_SN_OP, a, 10) && ed_out[2] == SNE_OK && sn.slot[0].seq > n);
        {
            sn_info_t in;
            check("... its name", sn_info(0, &in) && !strncmp(in.name, "RENAMED", 12));
        }
    }
    {   /* the full backup: object SNAP (kind 6), the area raw, read in chunks; written only through SN_WRITE */
        uint8_t a[16] = {1};
        uint32_t p, idx = 0xFFu, len = 0, crc = 0, flags = 0, off;
        static uint8_t got[SN_SECTORS * SN_SECT];
        ed_n = 0;
        ed_backup(ED_BK_LIST, a, 1);
        for (i = 0, p = 10; i < ed_out[1]; i++, p += 14)
            if (!memcmp(ed_out + p, "SNAP", 4))
                idx = i, flags = ed_out[p + 5], len = ed_r32(ed_out + p + 6, 3), crc = ed_r32(ed_out + p + 9, 5), n = ed_out[p + 4];
        check("backup: BK_LIST has SNAP (kind 6, the whole area, not written with BK_BEGIN)",
              idx < 0xFFu && n == BK_SNP && len == SN_SECTORS * SN_SECT && (flags & 3u) == 3u && !(flags & 4u));
        for (off = 0, ok = 1; ok && off < len; off += n) {
            a[0] = (uint8_t)idx;
            put7(a + 1, off, 3);
            ed_n = 0;
            ed_backup(ED_BK_READ, a, 4);
            n = ed_unpack7(ed_out + 9, ed_n - 9u, got + off, 256);
            ok = n && st_crc32(got + off, n) == ed_r32(ed_out + 4, 5);
        }
        check("backup: SNAP read in CRC-checked chunks: the area as it is in flash", ok && st_crc32(got, len) == crc &&
              !memcmp(got, nor + SN_BASE, len));
        a[0] = (uint8_t)idx;
        put7(a + 1, len, 3);
        put7(a + 4, crc, 5);
        ed_n = 0;
        ed_backup(ED_BK_BEGIN, a, 9);
        check("backup: BK_BEGIN of SNAP refused (rc 1: the editor imports each snapshot)", ed_out[1] == 1u);
    }
    printf("snapshots test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
