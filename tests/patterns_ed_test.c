/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor commands 83 PAT_READ / 84 PAT_WRITE (firmware/src/io/editor/ed_pat.c; web/EDITOR_PROTOCOL.md) on the real storage
 * and sections (FELUCCA_PATTERNS=1, a simulated NOR): a stored pattern read in chunks of 256 bytes comes back byte for
 * byte; a record written in chunks (pack7, in order) is checked as the stage would decode it, then stored (stopped:
 * the log; playing: the arena) and plays; a chunk out of order, a record of another track's kind, one too long, a
 * restore holding proj_tmp, a clear while playing: refused with their rc and nothing written; proj_tmp is lent
 * only while a record is received (given back at the end, and after 10 s); the launch waiting for the slot written
 * is decoded again. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_PATTERNS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

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
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"

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
#include "../firmware/src/storage/sections/sections.c"

/* ---- the editor's reply builder and pack7 (editor.c, ed_drums.c) as doubles; the reply is read back from ed_out */
static uint8_t ed_out[700];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[j]);
        p += k, n -= k;
    }
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
#include "../firmware/src/io/editor/ed_pat.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void steps(uint32_t k, uint32_t n, uint32_t base)
{
    uint32_t i;
    steps_clear(&trk[k]);
    trk[k].p[P_SLEN] = (int16_t)n;
    for (i = 0; i < n; i++)
        if (k == TRK_DRUM)
            dstep_set(&trk[k].dstep[i], (i + base) % 16u, LV_NORM, 0);
        else
            trk[k].step[i].note[0] = (uint8_t)(base + i), trk[k].step[i].n = 1, trk[k].step[i].time = ST_NOTE,
            trk[k].step[i].vel = (uint8_t)(90 + i % 30u), trk[k].step[i].flags = (uint8_t)(i & 3u), trk[k].step[i].lvl = (uint8_t)(i & 3u),
            trk[k].step[i].rat = (uint8_t)(i % 4u);
}
/* PAT_READ of (k, s): the record, in chunks of at most 256 bytes -> out; its length (-1: a reply that is wrong) */
static int rd(uint32_t k, uint32_t s, uint8_t *out)
{
    uint32_t o = 0, tot = 0;
    do {
        uint8_t a[4] = {(uint8_t)k, (uint8_t)s, (uint8_t)(o & 127u), (uint8_t)(o >> 7)};
        uint32_t n;
        ed_n = 0;
        if (!ed_pat(ED_PAT_READ, a, 4) || ed_n < 6u || ed_out[0] != k || ed_out[1] != s || (ed_out[2] | ed_out[3] << 7) != o)
            return -1;
        tot = ed_out[4] | ed_out[5] << 7;
        n = ed_unpack7(ed_out + 6, ed_n - 6u, out + o, 256u);
        if (n > 256u || (tot > o && !n))
            return -1;
        o += n;
    } while (o < tot);
    return (int)tot;
}
/* PAT_WRITE of rec (n bytes) to (k, s), chunks of `ch` bytes in order from `from`; the rc of the last reply (-1: none) */
static int wr(uint32_t k, uint32_t s, const uint8_t *rec, uint32_t n, uint32_t ch, uint32_t from)
{
    uint32_t o = from;
    int rc = -1;
    do {
        uint8_t a[6 + 300];
        uint32_t m = n - o < ch ? n - o : ch, j, q = 6;
        a[0] = (uint8_t)k, a[1] = (uint8_t)s, a[2] = (uint8_t)(o & 127u), a[3] = (uint8_t)(o >> 7), a[4] = (uint8_t)(n & 127u), a[5] = (uint8_t)(n >> 7);
        for (j = 0; j < m; j += 7u) {
            uint32_t g = m - j < 7u ? m - j : 7u, i, hi = 0;
            for (i = 0; i < g; i++)
                hi |= (uint32_t)(rec[o + j + i] >> 7) << i;
            a[q++] = (uint8_t)hi;
            for (i = 0; i < g; i++)
                a[q++] = rec[o + j + i] & 0x7Fu;
        }
        ed_n = 0;
        if (!ed_pat(ED_PAT_WRITE, a, q) || ed_n != 5u)
            return -1;
        rc = ed_out[4];
        o += m;
        if (rc)
            break;
    } while (o < n);
    return rc;
}

int main(void)
{
    static uint8_t r[PAT_REC_MAX + 8], r2[PAT_REC_MAX + 8], g[PAT_REC_MAX + 8];
    int n, n2;
    uint32_t i, ok;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    /* ---- read */
    steps(0, 64, 30);
    check("T1 slot 4 stored (64 steps)", !pat_store_slot(0, 3));
    n = rd(0, 3, r);
    ok = n > 256 && (uint32_t)n == slg.alen[PAT_ID(0, 3)] && pat_get(0, 3, g) == (uint32_t)n && !memcmp(r, g, (size_t)n);
    check("PAT_READ: the record in chunks of 256 bytes, byte for byte the stored one", ok);
    check("PAT_READ: an empty slot: total 0", rd(0, 9, g) == 0);
    {
        uint8_t a[4] = {0, 16, 0, 0}, b[3] = {0, 0, 0};
        check("PAT_READ: a slot past 15 / a short request: no reply", !ed_pat(ED_PAT_READ, a, 4) && !ed_pat(ED_PAT_READ, b, 3));
    }
    /* ---- write: stopped, into the log */
    steps(0, 12, 70);
    (void)sec_capture();
    n2 = (int)pat_encode(&proj_tmp.cur, 0, r2);
    check("a pattern record made (12 steps)", n2 > 5 && n2 < n);
    check("PAT_WRITE in chunks of 100 bytes: rc 0, proj_tmp given back", wr(0, 5, r2, (uint32_t)n2, 100u, 0) == 0 && !proj_tmp_busy());
    ok = pat_get(0, 5, g) == (uint32_t)n2 && !memcmp(g, r2, (size_t)n2) && slg_has(PAT_ID(0, 5)) && !sec_pend_has(pat_pend(0, 5));
    check("... stored in the log, byte for byte", ok);
    steps(0, 64, 30);
    pat_launch(0, 5, PW_NOW);
    check("... launched: the track plays it", trk[0].p[P_SLEN] == 12 && trk[0].step[0].note[0] == 70 && trk[0].step[11].note[0] == 81);
    check("... the one chunk record (200 bytes at a time) also: rc 0", wr(0, 6, r2, (uint32_t)n2, 256u, 0) == 0 && pat_get(0, 6, g) == (uint32_t)n2);
    /* ---- refused */
    {
        uint32_t seq = slg.seq;
        check("a chunk out of order (offset 100 first): rc 1, nothing lent", wr(0, 7, r2, (uint32_t)n2, 50u, 50u) == 1 && !proj_tmp_busy());
        check("a record of the drum kind for a synth track's slot... for the drum track: rc 2", wr(3, 7, r2, (uint32_t)n2, 256u, 0) == 2 &&
              !proj_tmp_busy() && !slg_has(PAT_ID(3, 7)));
        r2[5] ^= 0xFF;
        r2[0] = 0x00;
        check("a record that is not a pattern (no version bits): rc 2, nothing written", wr(0, 7, r2, (uint32_t)n2, 256u, 0) == 2 && !slg_has(PAT_ID(0, 7)) && slg.seq == seq);
        r2[0] = g[0];
    }
    {
        uint8_t big[PAT_REC_MAX + 8] = {0};
        check("a record longer than a pattern can be: rc 1", wr(0, 7, big, PAT_REC_MAX + 1u, 256u, 0) == 1 && !slg_has(PAT_ID(0, 7)));
    }
    proj_tmp_lent = 1;
    proj_tmp_t0 = fm1_ms;
    check("proj_tmp lent to a restore: rc 3, and it stays the restore's", wr(0, 7, g, (uint32_t)n2, 256u, 0) == 3 && proj_tmp_lent == 1);
    fm1_ms += 11000u;
    check("... a restore left for 10 s gives it back: the write goes", wr(0, 7, g, (uint32_t)n2, 256u, 0) == 0 && slg_has(PAT_ID(0, 7)));
    /* a half-received record: lent, then given back after 10 s; a new record restarts it */
    {
        uint8_t a[6 + 16] = {0, 8, 0, 0, (uint8_t)(n2 & 127), (uint8_t)(n2 >> 7), 0, 1, 2, 3, 4, 5, 6, 7};
        ed_n = 0;
        ed_pat(ED_PAT_WRITE, a, 14);
        check("a record half received: proj_tmp lent", proj_tmp_busy() && ed_out[4] == 0);
        check("... a record from the start again (the editor retried): restarts, ends, rc 0", wr(0, 8, g, (uint32_t)n2, 256u, 0) == 0 && !proj_tmp_busy());
        ed_n = 0;
        ed_pat(ED_PAT_WRITE, a, 14);
        fm1_ms += 11000u;
        check("... left for 10 s: given back", !proj_tmp_busy());
    }
    /* ---- clear (total 0) */
    check("PAT_WRITE with total 0 clears the slot", wr(0, 8, g, 0, 256u, 0) == 0 && !slg_has(PAT_ID(0, 8)) && pat_get(0, 8, g) == 0u);
    pat_cur[0] = 5;
    check("... clearing the working copy's own source: it has none then (unsaved, not missing)", wr(0, 5, g, 0, 256u, 0) == 0 && pat_cur[0] == PAT_NONE);
    /* ---- playing: the arena; a clear is refused */
    song.playing = 1;
    {
        uint32_t seq = slg.seq;
        steps(1, 16, 50);
        (void)sec_capture();
        n2 = (int)pat_encode(&proj_tmp.cur, 1, r2);
        check("playing: PAT_WRITE (T2, 16 steps): rc 0, the record waits in the arena", wr(1, 3, r2, (uint32_t)n2, 100u, 0) == 0 && sec_pend_has(pat_pend(1, 3)) &&
              slg.seq == seq && pat_get(1, 3, g) == (uint32_t)n2 && !memcmp(g, r2, (size_t)n2));
        check("playing: a clear (total 0): rc 4, nothing changed", wr(0, 6, g, 0, 256u, 0) == 4 && slg_has(PAT_ID(0, 6)));
    }
    /* a launch waiting for the slot written meanwhile is decoded again */
    pat_req[1] = 3;
    pat_staged |= 2u;
    check("playing: a launch waiting for the slot written: staged again", wr(1, 3, r2, (uint32_t)n2, 256u, 0) == 0 && !(pat_staged & 2u));
    song.playing = 0;
    printf("patterns editor test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
