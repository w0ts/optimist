/* SPDX-License-Identifier: GPL-3.0-only */
/* A SLOOP 2.4 backup file's project into Optimist through the editor (FELUCCA_SL24_EDIMPORT: ed_sl24.c commands 90
 * SL24_PUT and 91 SL24_BANK), on a simulated NOR: begin / data / commit, the CRCs, the FM6 parts' bank patches carried
 * with it, rc for every refusal, and nothing written to flash (2.4's data whole). Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#define FELUCCA_SL24_EXPORT 0
#define FELUCCA_SL24_EDIMPORT 1
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
#include "../firmware/src/storage/sections/sections.c"   /* (and sl24_guard.c) */

/* the editor's reply buffer and its 7-bit helpers, as editor.c / ed_drums.c have them */
static uint8_t ed_out[1024];
static uint32_t ed_n;
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[j]);
        p += k;
        n -= k;
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
#include "../firmware/src/io/editor/ed_sl24.c"

static int bad;
static project_t cap;
static dlrec_t capd;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint8_t req[600];
static uint32_t nreq;
static void w7(uint32_t v, uint32_t n)
{
    while (n--)
        req[nreq++] = (uint8_t)(v & 127u), v >>= 7;
}
static void pk7(const uint8_t *p, uint32_t n)             /* the editor's pack7 (into req) */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        req[nreq++] = (uint8_t)m;
        for (j = 0; j < k; j++)
            req[nreq++] = p[j] & 127u;
        p += k;
        n -= k;
    }
}
static uint32_t r7(const uint8_t *a, uint32_t n)
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)a[i] << (7u * i);
    return v;
}
static int cmd(uint32_t c)                                /* -> handled; the reply in ed_out[0..ed_n) */
{
    ed_n = 0;
    return ed_sl24(c, req, nreq);
}
/* begin / data in 256 B chunks (one sent with a bad CRC first, when bad >= 0: that chunk) / commit -> commit's rc */
static int put(const uint8_t *o, uint32_t len, int badchunk, uint32_t *rcs)
{
    uint32_t off, i = 0;
    nreq = 0, req[nreq++] = 0, w7(len, 3), w7(st_crc32(o, len), 5);
    cmd(90);
    rcs[0] = ed_out[1];
    if (rcs[0])
        return -1;
    for (off = 0; off < len; off += 256, i++) {
        uint32_t n = len - off < 256u ? len - off : 256u;
        if ((int)i == badchunk) {
            nreq = 0, req[nreq++] = 1, w7(off, 3), w7(st_crc32(o + off, n) ^ 1u, 5), pk7(o + off, n);
            cmd(90);
            rcs[1] = ed_out[4];
        }
        nreq = 0, req[nreq++] = 1, w7(off, 3), w7(st_crc32(o + off, n), 5), pk7(o + off, n);
        cmd(90);
        if (ed_out[4])
            return -2;
        fm1_ms += 50;
    }
    nreq = 0, req[nreq++] = 2;
    cmd(90);
    return ed_out[1];
}

int main(void)
{
    static uint8_t fun5[3840], obj[3840 + 1 + NPART * 128], img[0x100000], rec[128];
    uint32_t rcs[2] = {0, 0}, s, k, ok;
    uint8_t *t;
    FILE *f = fopen("tests/sl24_fun5.bin", "rb");
    if (!f || fread(fun5, 1, sizeof fun5, f) != sizeof fun5) {
        printf("tests/sl24_fun5.bin missing (run from the repo root)\n");
        return 1;
    }
    fclose(f);
    memset(nor, 0xFF, sizeof nor);
    memcpy(img, nor, sizeof nor);
    host_tracks_init();
    song.playing = 0, transport_req = 0;
    check("2.4's project alone (3840 B): imported, rc 0", put(fun5, sizeof fun5, -1, rcs) == 0 && rcs[0] == 0 &&
          !strcmp(last_msg, "2.4 IMPORTED: SAVE IT") + !strcmp(last_msg, "2.4 IMPORTED, NO LOCKS") == 1);
    ok = trk[1].p[P_E0] == 2 && trk[1].p[P_E1] == 15 && !proj_tmp_lent;
    check("... the working project: its FM6 part (F3 with its macros: VOICE R03, MOD 15); proj_tmp let go", ok);
    /* with a bank patch: track 0 an FM6 part on B4, its patch in the object */
    memcpy(obj, fun5, 3840);
    t = obj + 12 + 64;
    t[122] = 9, t[120] = 8 + 3, t[121] = 0;
    for (k = 0; k < 7u; k++)
        t[2 * (53 + k)] = 0, t[2 * (53 + k) + 1] = 0;
    s = proj_hash(obj, 3836), memcpy(obj + 3836, &s, 4);
    memcpy(rec, SL24_FM6_F[4], 128), memcpy(rec + 118, "FILE PAD  ", 10);
    memset(obj + 3840, 0, sizeof obj - 3840);
    obj[3840] = 1;
    memcpy(obj + 3841, rec, 128);
    ok = put(obj, sizeof obj, 7, rcs) == 0 && rcs[1] == 2;
    check("with the FM6 parts' patches (4225 B), a chunk with a bad CRC refused (rc 2) and sent again: imported", ok);
    proj_capture(&cap, &capd);
    check("... part 1's voice: the file's B4 patch", ((cap.fm6_has & 1u) != 0) && !memcmp(cap.fm6[0], rec, 128));
    obj[3840] = 0;
    ok = put(obj, sizeof obj, -1, rcs) == 0;
    proj_capture(&cap, &capd);
    check("... no patch for it in the file (bit clear): 2.4's INIT, as 2.4 plays an empty slot",
          ok && !memcmp(cap.fm6[0], SL24_FM6_INIT, 128));
    /* refusals */
    obj[100] ^= 1;
    check("not a SLOOP 2.4 project (its sum): rc 8, nothing imported", put(obj, sizeof obj, -1, rcs) == 8);
    obj[100] ^= 1;
    nreq = 0, req[nreq++] = 0, w7(1000, 3), w7(0, 5);
    cmd(90);
    check("a length that is neither 3840 nor 4225: rc 6", ed_out[1] == 6 && !proj_tmp_lent);
    song.playing = 1;
    nreq = 0, req[nreq++] = 0, w7(3840, 3), w7(0, 5);
    cmd(90);
    check("while playing: rc 3", ed_out[1] == 3);
    song.playing = 0;
    nreq = 0, req[nreq++] = 2;
    cmd(90);
    check("commit without a begin: rc 1", ed_out[1] == 1);
    proj_tmp_lent = 1, proj_tmp_t0 = fm1_ms;
    nreq = 0, req[nreq++] = 0, w7(3840, 3), w7(0, 5);
    cmd(90);
    check("a restore holds proj_tmp: rc 9", ed_out[1] == 9);
    fm1_ms += 20000;                                      /* (it gives up after 10 s) */
    cmd(90);
    check("... let go after 10 s: begun", ed_out[1] == 0 && proj_tmp_lent);
    fm1_ms += 20000;
    nreq = 0, req[nreq++] = 2;
    cmd(90);
    check("an import left for 10 s: let go too (commit: rc 1)", ed_out[1] == 1);
    check("nothing written to flash by any of it", !memcmp(img, nor, sizeof nor));
    /* 91: 2.4's FM6 bank kept in flash, read out */
    nreq = 0, w7(0, 3);
    cmd(91);
    check("91 SL24_BANK, no bank in flash: rc 4", ed_out[0] == 4 && r7(ed_out + 1, 3) == 0);
    {
        static uint8_t b[SL24_BANK_LEN], got[SL24_BANK_LEN];
        uint32_t w[4] = {SL24_BANK_MAGIC, 1u | SL24_BANK_N << 16, 1u << 3, 0}, off, crc = 0;
        st_hdr_t h;
        memset(b, 0, sizeof b), memcpy(b, w, sizeof w), memcpy(b + 16 + 3 * 128, rec, 128);
        memcpy(nor + 0xE5000 + 256, b, sizeof b);
        memset(&h, 0, sizeof h);
        h.magic = ST_MAGIC, h.type = 8, h.seq = 1, h.len = sizeof b, h.crc = st_crc32(b, sizeof b), h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + 0xE5000, &h, sizeof h);
        memcpy(img, nor, sizeof nor);
        for (off = 0, ok = 1; off < sizeof b; off += 256) {
            uint32_t n;
            nreq = 0, w7(off, 3);
            cmd(91);
            n = sizeof b - off < 256u ? sizeof b - off : 256u;
            ok &= ed_out[0] == 0 && r7(ed_out + 1, 3) == sizeof b && r7(ed_out + 9, 3) == off &&
                  ed_unpack7(ed_out + 12, ed_n - 12u, got + off, n) == n;
            crc = r7(ed_out + 4, 5);
        }
        check("... a bank there: read in 256 B chunks, its length and CRC; the bytes as 2.4 wrote them",
              ok && crc == (st_crc32(b, sizeof b) & 0x7FFFFFFFFu) && !memcmp(got, b, sizeof b));
        check("... flash untouched", !memcmp(img, nor, sizeof nor));
    }
    printf("sl24 editor import test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
