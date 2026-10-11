/* SPDX-License-Identifier: GPL-3.0-only */
/* Optimist started on flash SLOOP 2.5 or 2.6 wrote (isod89/sloop-fm1 v2.5 fa9ce57, v2.6 d30d655): their settings
 * object is their persist_t (88 B, the start of ours), then their SYN1..SYN4 drum kits (1464 B, "DSU1"), then in 2.6
 * a MIDI part (40 B). Golden objects written with their own types: tests/sl25_settings.bin, tests/sl26_settings.bin
 * (tests/sl25_gen.c). FELUCCA_SL24_SAFE, sl24_guard.c sl25_set_scan / sl25_set_load, storage.c st_set_*:
 *
 *   - Optimist's settings read (project.c persist_boot: the scan, st_load, else their 88 bytes) takes their 88 bytes,
 *     as 2.4's record, never the kits as its own (BRIGHT, the settings word, the BLE fields);
 *   - Optimist's settings saves (st_save) never write over their record: the kits stay byte for byte, and SLOOP at its
 *     next start still reads its own record, kits included (its newest copy); Optimist at its next start its own;
 *   - SLOOP saving again (over Optimist's copy), a cut write, a record an older Optimist wrote beside theirs; our own
 *     records (96 B, a BLE build's 302 B) and 2.4's (88 B) as before.
 * With -DSL25_OLD the same checks run the read and save as they were before (st_load as ours, st_save): they fail
 * (the RED run of the fix). Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1
#endif
#define FELUCCA_UP_FM6 1
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
    uint8_t sl24[3840];
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
#include "../firmware/src/storage/sections/sections.c"   /* (and sl24_guard.c) */

#ifdef SL25_OLD                                   /* before the fix: the record read as ours, saved with st_save */
static void sl25_set_scan(void) {}
static int sl25_set_load(void *p, uint32_t max) { (void)p, (void)max; return -1; }
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define SET_A 0xFC000u                            /* the settings object's copies */
#define SET_B 0xFD000u
static uint8_t sl25[1552], sl26[1592];
/* the object as SLOOP's storage.c writes it (st_save3: the copy that is not its current one, seq + 1) */
static void sloop_save(const uint8_t *p, uint32_t len)
{
    st_hdr_t a, b, h;
    int va = st_head(OBJ_SETTINGS, 0, &a) == 0, vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
    uint32_t cur = vb && (!va || b.seq > a.seq) ? 1u : va ? 0u : 2u, c = cur == 0u ? 1u : 0u, off = c ? SET_B : SET_A;
    uint32_t seq = cur == 2u ? 0u : cur ? b.seq : a.seq;
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off + ST_PAYLOAD_OFF, p, len);
    h.magic = ST_MAGIC, h.type = OBJ_SETTINGS, h.slot = (uint16_t)c, h.seq = seq + 1u, h.len = len;
    h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + off, &h, sizeof h);
}
/* what SLOOP reads at its start (its storage.c st_current: the valid copy with the higher seq, A on a tie; its
 * project.c dsu_boot: the kits when the record is persist_t + dsu_bank_t (+ the MIDI part) with "DSU1" at 88) */
static int sloop_reads(const uint8_t *want, uint32_t len)
{
    st_hdr_t a, b, h;
    static uint8_t buf[4096];
    int va = st_head(OBJ_SETTINGS, 0, &a) == 0, vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
    uint32_t c;
    if (!va && !vb)
        return 0;
    c = vb && (!va || b.seq > a.seq) ? 1u : 0u;
    h = c ? b : a;
    memcpy(buf, nor + (c ? SET_B : SET_A) + ST_PAYLOAD_OFF, h.len);
    return h.len == len && st_crc32(buf, h.len) == h.crc && !memcmp(buf, want, len) &&
           !memcmp(buf + 88, "DSU1", 4);
}
/* Optimist's start (project.c persist_boot): the scan, its own record, else SLOOP's 88 bytes */
static int boot_read(uint8_t *p, uint32_t max)
{
    int n;
    sl25_set_scan();
    n = st_load(OBJ_SETTINGS, p, max);
    if (n < 0)
        n = sl25_set_load(p, max);
    return n;
}
/* a settings record of ours (n bytes: PER3, palette .. the panel, the song order, the view word, BRIGHT 0, the
 * settings word; a BLE build's then its BLE fields), its bytes from seed */
static void ours(uint8_t *o, uint32_t n, uint32_t seed)
{
    uint32_t i, m = 0x50455233u;
    for (i = 0; i < n; i++)
        o[i] = (uint8_t)(i * 3u + seed);
    memcpy(o, &m, 4);
    memset(o + 88, 0, 4);
}
static int load(const char *path, uint8_t *b, uint32_t n)
{
    FILE *f = fopen(path, "rb");
    int ok = f && fread(b, 1, n, f) == n && fgetc(f) == EOF;
    if (f)
        fclose(f);
    return ok;
}

/* SLOOP's object (blob, len) saved twice by SLOOP (copy A seq 1, copy B seq 2), then Optimist starts, reads, saves
 * several times, starts again; then SLOOP saves again, Optimist again */
static void round_trip(const char *who, const uint8_t *blob, uint32_t len)
{
    static uint8_t img[0x2000], p[512], q[512];
    char what[160];
    int n, ok, i;
    memset(nor, 0xFF, sizeof nor);
    sloop_save(blob, len);
    sloop_save(blob, len);
    n = boot_read(p, 96);
    snprintf(what, sizeof what, "%s: Optimist's settings read takes their 88 bytes (as 2.4's), never the kits as ours", who);
    check(what, n == 88 && !memcmp(p, blob, 88));
    memcpy(img, nor + SET_B, 0x1000);             /* (their current copy) */
    for (i = 0, ok = 1; i < 5; i++) {
        ours(q, 96, (uint32_t)i);
        ok &= st_save(OBJ_SETTINGS, q, 96) == 0;
    }
    snprintf(what, sizeof what, "%s: Optimist saves its settings 5 times (each SAVED)", who);
    check(what, ok);
    snprintf(what, sizeof what, "%s: ... their record and its SYN kits untouched, byte for byte", who);
    check(what, !memcmp(img, nor + SET_B, 0x1000));
    snprintf(what, sizeof what, "%s: ... SLOOP's next start reads its own record, kits included (its newest copy)", who);
    check(what, sloop_reads(blob, len));
    n = boot_read(p, 512);
    snprintf(what, sizeof what, "%s: ... Optimist's next start reads its own record back (beside theirs)", who);
    check(what, n == 96 && !memcmp(p, q, 96));
    sloop_save(blob, len);                        /* (SLOOP saves: over Optimist's copy) */
    n = boot_read(p, 96);
    ours(q, 96, 9);
    ok = n == 88 && !memcmp(p, blob, 88) && st_save(OBJ_SETTINGS, q, 96) == 0 && sloop_reads(blob, len) &&
         boot_read(p, 512) == 96 && !memcmp(p, q, 96);
    snprintf(what, sizeof what, "%s: SLOOP saved again over ours: Optimist reads their 88 B, saves beside, both read their own", who);
    check(what, ok);
    {                                             /* (a cut while Optimist wrote its copy: the one that is not theirs) */
        st_hdr_t h;
        st_erase(st_head(OBJ_SETTINGS, 0, &h) == 0 && h.len == len ? SET_B : SET_A);
    }
    n = boot_read(p, 96);
    snprintf(what, sizeof what, "%s: Optimist's copy cut short: their 88 bytes again, their record whole", who);
    check(what, n == 88 && !memcmp(p, blob, 88) && sloop_reads(blob, len));
}

int main(void)
{
    static uint8_t p[512], q[512];
    int n, ok;
    if (!load("tests/sl25_settings.bin", sl25, sizeof sl25) || !load("tests/sl26_settings.bin", sl26, sizeof sl26)) {
        printf("tests/sl25_settings.bin, sl26_settings.bin missing (run from the repo root)\n");
        return 1;
    }
    check("golden: 2.5's object is 88 + 1464 B with DSU1 at 88; 2.6's has its 40-B MIDI part after",
          !memcmp(sl25 + 88, "DSU1", 4) && !memcmp(sl26 + 88, "DSU1", 4) && !memcmp(sl25, sl26, 1552));
    round_trip("2.5", sl25, sizeof sl25);
    round_trip("2.6", sl26, sizeof sl26);

    memset(nor, 0xFF, sizeof nor);                /* an older Optimist saved over 2.5's start: ours newer, theirs kept */
    sloop_save(sl25, sizeof sl25);
    ours(q, 96, 3);
    sloop_save(q, 96);                            /* (as st_save did: the other copy, seq + 1) */
    n = boot_read(p, 512);
    ok = n == 96 && !memcmp(p, q, 96);
    ours(q, 96, 4);
    ok &= st_save(OBJ_SETTINGS, q, 96) == 0 && sloop_reads(sl25, sizeof sl25) && boot_read(p, 512) == 96 && !memcmp(p, q, 96);
    check("an older Optimist's record beside 2.5's: read as ours; saved below theirs, so 2.5 finds its kits again", ok);

    memset(nor, 0xFF, sizeof nor);                /* our own records only */
    sl25_set_scan();                              /* (a start: nothing of SLOOP's) */
    ours(q, 96, 5);
    ok = st_save(OBJ_SETTINGS, q, 96) == 0 && boot_read(p, 512) == 96 && !memcmp(p, q, 96);
    ours(q, 302, 6);
    ok &= st_save(OBJ_SETTINGS, q, 302) == 0 && boot_read(p, 512) == 302 && !memcmp(p, q, 302);
    {
        st_hdr_t a, b;
        ok &= st_head(OBJ_SETTINGS, 0, &a) == 0 && st_head(OBJ_SETTINGS, 1, &b) == 0 && a.seq + b.seq == 3u;
    }
    check("our own records (96 B; a BLE build's 302 B): read as ours, saved A then B as always", ok);

    memset(nor, 0xFF, sizeof nor);                /* 2.4's record (88 B: no kits) */
    sloop_save(sl25, 88);
    n = boot_read(p, 96);
    ours(q, 96, 7);
    ok = n == 88 && !memcmp(p, sl25, 88) && st_save(OBJ_SETTINGS, q, 96) == 0 && boot_read(p, 512) == 96;
    check("2.4's record (88 B): read as before (st_load: their 88 bytes), our save becomes the current copy", ok);
    {
        static uint8_t fake[1552];
        memcpy(fake, sl25, sizeof fake);
        memcpy(fake + 88, "DSU2", 4);
        memset(nor, 0xFF, sizeof nor);
        sloop_save(fake, sizeof fake);
        sl25_set_scan();
        check("1552 B without 2.5's kit header: not taken for SLOOP 2.5's", st_load(OBJ_SETTINGS, p, 96) == 96);
    }
    printf("%s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
