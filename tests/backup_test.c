/* SPDX-License-Identifier: GPL-3.0-only */
/* Backup and restore (firmware/src/ed_backup.c, editor cmds 43..48) on a simulated NOR through storage.c:
 *   round trip   every stored object listed, read in CRC-checked chunks, the flash wiped, written back: each
 *                object byte for byte as it was; the projects load with their drum records
 *   torn         a transfer cut before its COMMIT writes nothing; a COMMIT cut in the flash write (at every
 *                program) leaves the old copy; a chunk with a wrong CRC is refused and sent again
 *   migration    an older project (FUN8, its lanes inline) restored into a slot loads as today's format
 *   rules        stopped only; a restore session holds proj_tmp (saves refused, given back after 10 s);
 *                BK_END done drops the RAM slots and asks for the restart */
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
} proj_tmp;
#include "../firmware/src/drum_store.c"

static struct { int force; uint8_t arm, arm_t; } ui;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
#include "../firmware/src/drum_kits.c"

static uint8_t ed_out[700];
static uint32_t ed_n;
static uint8_t flash_ok = 1;
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
static int flushed, dropped;
#define BK_FLUSH() (flushed++)
static void proj_slots_drop(void) { dropped++; }
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

static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ---- the editor's side, as web/editor.html does it */
typedef struct { char tag[5]; uint32_t kind, flags, len, crc; uint8_t *data; } obj_t;
static obj_t objs[32];
static uint32_t nobj;
static uint32_t caps_at;
static int list(void)
{
    uint8_t a[1] = {1};
    uint32_t i, p;
    if (!cmd(ED_BK_LIST, a, 1) || ed_out[0] != BK_VERSION)
        return 0;
    nobj = ed_out[1];
    p = 10;
    for (i = 0; i < nobj; i++, p += 14) {
        memcpy(objs[i].tag, ed_out + p, 4);
        objs[i].tag[4] = 0;
        objs[i].kind = ed_out[p + 4];
        objs[i].flags = ed_out[p + 5];
        objs[i].len = ed_r32(ed_out + p + 6, 3);
        objs[i].crc = ed_r32(ed_out + p + 9, 5);
    }
    caps_at = p;                                       /* v2: what the build holds (bk_caps), the builder's bits last */
    return p + 29u <= ed_n && p + 29u + ed_out[p + 28] == ed_n;
}
static uint32_t chunks_read;
static int read_obj(uint32_t i)          /* -> objs[i].data, CRC-checked per chunk and whole */
{
    uint32_t off = 0;
    free(objs[i].data);
    objs[i].data = malloc(objs[i].len + 1u);
    while (off < objs[i].len) {
        uint8_t a[4];
        uint32_t n;
        a[0] = (uint8_t)i;
        put32(a + 1, off, 3);
        if (!cmd(ED_BK_READ, a, 4) || ed_out[0] != i || ed_r32(ed_out + 1, 3) != off)
            return 0;
        n = ed_unpack7(ed_out + 9, ed_n - 9u, objs[i].data + off, objs[i].len - off);
        if (!n || st_crc32(objs[i].data + off, n) != ed_r32(ed_out + 4, 5))
            return 0;
        off += n;
        chunks_read++;
    }
    return st_crc32(objs[i].data, objs[i].len) == objs[i].crc;
}
/* object i := n bytes at d (BEGIN, DATA chunks, COMMIT); cut: stop after that many chunks (no COMMIT); the rc of
 * COMMIT (or of the step that failed, + 10) */
static int write_obj(uint32_t i, const uint8_t *d, uint32_t n, int cut, int bad_chunk)
{
    uint8_t a[400];
    uint32_t off = 0, k = 0;
    a[0] = (uint8_t)i;
    put32(a + 1, n, 3);
    put32(a + 4, st_crc32(d, n), 5);
    if (!cmd(ED_BK_BEGIN, a, 9) || ed_out[1])
        return 10 + ed_out[1];
    while (off < n) {
        uint32_t c = n - off > BK_CHUNK ? BK_CHUNK : n - off;
        if (cut >= 0 && (int)k == cut)
            return -1;
        put32(a + 1, off, 3);
        put32(a + 4, st_crc32(d + off, c) ^ (bad_chunk == (int)k ? 1u : 0u), 5);
        {
            uint32_t m = 9u + pack(d + off, c, a + 9);
            if (!cmd(ED_BK_DATA, a, m))
                return 30;
            if (ed_out[4] == 2u && bad_chunk == (int)k) {   /* refused: sent again, right */
                bad_chunk = -1;
                continue;
            }
            if (ed_out[4])
                return 20 + ed_out[4];
        }
        off += c;
        k++;
    }
    cmd(ED_BK_COMMIT, a, 1);
    return ed_out[1];
}
static int find(const char *tag)
{
    uint32_t i;
    for (i = 0; i < nobj; i++)
        if (!strcmp(objs[i].tag, tag))
            return (int)i;
    return -1;
}
static int obj_is(uint32_t obj, const uint8_t *d, uint32_t n)   /* storage object obj holds exactly d[n] */
{
    static uint8_t b[ST_PAYLOAD_MAX];
    int got = st_load(obj, b, sizeof b);
    return got == (int)n && !memcmp(b, d, n);
}

int main(void)
{
    static project_t P, Q;
    static dlrec_t D, E;
    static uint8_t settings_b[120], bank_b[2000];
    uint32_t i, k;
    int ok, r;
    host_tracks_init();
    memset(nor, 0xFF, sizeof nor);
    /* the device's flash: two projects with drum lanes and sends, the working project, a kit, two user
     * preset banks and the settings (opaque bytes here: a restore does not look inside), a sample in USR1 */
    memset(&dl, 0, sizeof dl);
    dl.ofs[2][DE_TUNE] = 5;
    dsend[2] = dsend_word(20, 3, 0);
    TDRUM->p[P_SLEN] = 12;
    proj_capture(&P, &D);
    proj_put(OBJ_PROJECT0, &P, &D);
    dsend[4] = dsend_word(-1, 9, 9);
    proj_capture(&P, &D);
    proj_put(OBJ_PROJECT0 + 2, &P, &D);
    proj_put(OBJ_AUTOSAVE, &P, &D);
    ukit_store(3);
    for (i = 0; i < sizeof settings_b; i++)
        settings_b[i] = (uint8_t)(i * 13u + 1u);
    for (i = 0; i < sizeof bank_b; i++)
        bank_b[i] = (uint8_t)(i * 7u);
    st_save(OBJ_SETTINGS, settings_b, sizeof settings_b);
    st_save(OBJ_UPRESET0 + 1, bank_b, sizeof bank_b);
    {
        smp_user_hdr_t *h = (smp_user_hdr_t *)(void *)host_slots;
        memset(host_slots, 0xFF, sizeof host_slots);
        memset(h, 0, sizeof *h);
        h->magic = SMP_USER_MAGIC;
        h->version = 1;
        h->nz = 1;
        h->data_len = 3000;
        for (i = 0; i < 3000u; i++)
            ((uint8_t *)host_slots)[SMP_USER_DATA + i] = (uint8_t)(i ^ 0x5A);
    }

    check("BK_LIST: version 2, every object with tag, kind, length, CRC, then what the build holds", list() && nobj == BK_N && nobj >= 14u);
    check("... the build's caps: every engine of this build, 4 sections, USR3 64 KiB, the FUNA layout",
          ed_r32(ed_out + caps_at, 2) == ((1u << NENGINES) - 1u) && ed_out[caps_at + 10] == 4u &&
          ed_r32(ed_out + caps_at + 17, 3) == SMP_USER_CAP(2) && SMP_USER_CAP(2) == 0x10000u &&
          ed_r32(ed_out + caps_at + 20, 2) == __builtin_offsetof(project_t, t[0].engine) &&
          ed_r32(ed_out + caps_at + 22, 2) == sizeof(proj_trk_t));
    check("... the order: settings, drum records before the projects, ..., USR1..3",
          !strcmp(objs[0].tag, "SETT") && find("DLNS") < find("PRJ1") && !strcmp(objs[nobj - 1u].tag, "USR3"));
    ok = 1;
    for (i = 0; i < nobj; i++) {
        int want;
        if (!strcmp(objs[i].tag, "FM6B")) {               /* (this host test has no fm6_store.c: not in its build) */
            ok &= !(objs[i].flags & 3u) && objs[i].kind == BK_FM6;
            continue;
        }
        want = !strcmp(objs[i].tag, "PRJ2") || !strcmp(objs[i].tag, "PRJ4") || !strcmp(objs[i].tag, "UPR1") ||
                   !strcmp(objs[i].tag, "USR2") || !strcmp(objs[i].tag, "USR3") ? 0 : 1;
        ok &= !!(objs[i].flags & 2u) == want && (objs[i].flags & 1u) && !!(objs[i].flags & 4u) == (objs[i].kind == BK_ST);
    }
    check("... has data: what was stored (2 projects, autosave, kit, a preset bank, settings, USR1)", ok);
    check("... USR1: header + 3000 bytes of data", objs[find("USR1")].len == SMP_USER_DATA + 3000u);
    ok = 1;
    chunks_read = 0;
    for (i = k = 0; i < nobj; i++)
        if (objs[i].len) {
            ok &= read_obj(i);
            k += objs[i].len;
        }
    printf("backup: %u bytes in %u chunks of <= %u B\n", k, chunks_read, BK_CHUNK);
    check("backup: every object read in CRC-checked chunks, whole CRCs right", ok);

    /* wipe the storage objects, then write them all back */
    memset(nor, 0xFF, sizeof nor);
    uk_read = 0;
    check("wiped: no project left", !proj_get(OBJ_PROJECT0, &Q, &E));
    ok = 1;
    for (i = 0; i < nobj; i++)
        if (objs[i].kind == BK_ST && objs[i].len)
            ok &= write_obj(i, objs[i].data, objs[i].len, -1, -1) == 0;
    check("restore: every storage object BEGIN / DATA / COMMIT ok (a session: RAM work flushed once)", ok && flushed == 1 &&
          proj_tmp_lent);
    check("... while the session holds proj_tmp: a project save, a load, a kit save are refused", proj_put(OBJ_PROJECT0 + 1, &P, &D) != 0 &&
          !proj_get(OBJ_PROJECT0, &Q, &E) && ukit_store(5) != 0);
    {
        uint8_t a[1] = {1};
        check("BK_END done: the RAM slots dropped, the restart asked, proj_tmp given back",
              cmd(ED_BK_END, a, 1) && !ed_out[0] && dropped == 1 && bk.reboot && !proj_tmp_lent);
        bk.reboot = 0;
    }
    ok = 1;
    for (i = 0; i < nobj; i++)
        if (objs[i].kind == BK_ST && objs[i].len)
            ok &= obj_is(BK_OBJS[i].id, objs[i].data, objs[i].len);
    check("round trip: every object byte for byte as backed up", ok);
    ok = proj_get(OBJ_PROJECT0 + 2, &Q, &E) && proj_ok(&Q) && !memcmp(&Q, &P, sizeof P) && !memcmp(&E, &D, sizeof D);
    check("... project 3 loads with its drum record (lanes, sends)", ok && E.snd[4] == dsend_word(-1, 9, 9));
    uk_read = 0;
    check("... the kit bank: BACKUP in slot 4", ukit_used(3) && ukit_count() == 1u);

    /* torn transfers */
    r = write_obj((uint32_t)find("SETT"), bank_b, 1500, 3, -1);
    check("a transfer cut after 3 chunks (no COMMIT): nothing written", r == -1 && obj_is(OBJ_SETTINGS, settings_b, sizeof settings_b));
    r = write_obj((uint32_t)find("SETT"), bank_b, 1500, -1, 2);
    check("a chunk with a wrong CRC: refused (rc 2), sent again, the object written", r == 0 && obj_is(OBJ_SETTINGS, bank_b, 1500));
    {
        uint32_t total, n, bad = 0;
        progs = 0;
        write_obj((uint32_t)find("SETT"), settings_b, sizeof settings_b, -1, -1);
        total = progs;
        for (n = 0; n < total; n++) {
            fail_after = (int)n;
            r = write_obj((uint32_t)find("SETT"), bank_b, sizeof bank_b, -1, -1);
            fail_after = -1;
            bad += !(r == 7 && obj_is(OBJ_SETTINGS, settings_b, sizeof settings_b));
        }
        check("a COMMIT cut at each flash program: rc 7, the old copy stays", total >= 2u && !bad);
    }
    {
        uint8_t a[2] = {0, 0};
        cmd(ED_BK_END, a, 1);
        check("BK_END abort: no restart, proj_tmp given back", !bk.reboot && !proj_tmp_lent && proj_put(OBJ_PROJECT0 + 1, &P, &D) == 0);
    }
    /* an older format restored: FUN8 (its lanes inline) into slot 2 */
    {
        static uint8_t v8[PROJ_V8_N];
        memset(&dl, 0, sizeof dl);
        dl.ofs[7][DE_DECAY] = -9;
        memset(dsend, 0, sizeof dsend);
        proj_capture(&P, &D);
        memcpy(v8, &P, PROJ_V7_N);
        memcpy(v8 + PROJ_V7_N, &dl, sizeof dl);
        ((uint32_t *)v8)[0] = PROJ_MAGIC_V8;
        ((uint32_t *)v8)[1] = PROJ_V8_N;
        *(uint32_t *)(v8 + PROJ_V8_N - 4u) = proj_hash(v8, PROJ_V8_N - 4u);
        r = write_obj((uint32_t)find("PRJ2"), v8, sizeof v8, -1, -1);
        cmd(ED_BK_END, (const uint8_t *)"\0", 1);
        ok = r == 0 && proj_get(OBJ_PROJECT0 + 1, &Q, &E) && Q.magic == PROJ_MAGIC && E.l.ofs[7][DE_DECAY] == -9;
        check("an older project (FUN8) restored: loads as FUNA, its lanes kept", ok);
    }
    /* the rules */
    {
        uint8_t a[9];
        a[0] = (uint8_t)find("PRJ1");
        put32(a + 1, 100, 3);
        put32(a + 4, 0, 5);
        song.playing = 1;
        check("playing: BEGIN refused (rc 3)", cmd(ED_BK_BEGIN, a, 9) && ed_out[1] == 3u && !proj_tmp_lent);
        song.playing = 0;
        a[0] = (uint8_t)find("USR1");
        check("a sample slot: not through BEGIN (rc 1: the sample upload commands write it)", cmd(ED_BK_BEGIN, a, 9) && ed_out[1] == 1u);
        a[0] = (uint8_t)find("PRJ1");
        put32(a + 1, ST_PAYLOAD_MAX + 1u, 3);
        check("longer than a storage object: rc 6", cmd(ED_BK_BEGIN, a, 9) && ed_out[1] == 6u);
        put32(a + 1, 100, 3);
        cmd(ED_BK_BEGIN, a, 9);
        fm1_ms += 11000u;
        check("a session left for 10 s: proj_tmp given back (saves work again)", proj_put(OBJ_PROJECT0 + 1, &P, &D) == 0 &&
              !proj_tmp_lent);
        a[0] = 99;
        check("an unknown object: no reply", !cmd(ED_BK_READ, a, 4) && !cmd(ED_BK_BEGIN, a, 9));
    }
    for (k = 0; k < nobj; k++)
        free(objs[k].data);
    printf(fails ? "BACKUP TEST FAILED\n" : "backup test passed\n");
    return fails != 0;
}
