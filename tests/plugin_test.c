/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the plugins' flash loader (firmware/src/core/plugin.c, docs/SD-MODE-DESIGN.md section 10) against a
 * simulated NOR flash (erase -> 0xFF, a program only clears bits) with a 64 KB region at the normal layout's place:
 *   - a synthetic plugin file (an image of known words, relocations of the three kinds, two of them straddling the first
 *     sector's end; kind 2 the API table's address) installed: placed at the region's start, the header whole (base where it sits, the image's CRC as
 *     patched, nrel 0), every relocated word the file's word plus the image's XIP address, the untouched words as
 *     they were, slot 0 its descriptor's address and its UID;
 *   - a second file (another UID) placed right after the first's sectors; the same UID again: its slot, nothing written;
 *   - a boot (plug_init) finds both, in region order; the first forgotten: its sector erased, the second keeps playing
 *     from slot 0, a third install reuses the first's room;
 *   - a byte of a placed image flipped: not found at boot, its sectors free; a header with a bad CRC the same;
 *   - refused: not a plugin, another API, a relocation past the image, a file larger than the room (FULL), more
 *     plugins than slots; the reader failing in the middle: nothing registered, no header written, the room free;
 *   - the Python tool's place() agrees: its CRC of this test's first vector (tests/plugin_build_test.py prints it).
 *   build/host/plugin_test   Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FELUCCA_PLUGINS 1
#define FELUCCA_PLUGIN_KB 64
#define FELUCCA_PLUGIN_SLOTS 2
#define FELUCCA_LAYOUT 0
#define PLUG_ABI 0x12345678u                  /* (no core.h here: a stand-in for the structs' shapes) */
static uint32_t test_api = 0x02ABCDE0u;       /* (a stand-in for the firmware's API table: tests/plugin_build_test.py's) */
#define PLUG_API_ADDR test_api
#define PLUG_DESC_OK(d) ((void)(d), 1)        /* (the test's descriptors are pattern words: no part memory to check) */
#include "fm1_xip.h"
#include "core/plugin.h"

typedef struct engine_s engine_t;
#define NBUILT 3u
static const engine_t *ENGINES[NBUILT + FELUCCA_PLUGIN_SLOTS];
static int plug_none_;
#define PLUG_NONE ((const engine_t *)&plug_none_)
#define PLUG_RAM
static uint8_t plug_uid[FELUCCA_PLUGIN_SLOTS] = {0xFF, 0xFF};
static uint32_t plug_slot_of(uint32_t uid)
{
    uint32_t k;
    for (k = 0; k < FELUCCA_PLUGIN_SLOTS; k++)
        if (plug_uid[k] == uid)
            return k;
    return 0xFFu;
}

/* ---- the simulated flash */
static uint8_t flash[1u << 20];
static uint32_t n_erase, n_prog, n_bad_range;
#define PLUG_XIP(off) ((const uint8_t *)flash + (off))
static int in_region(uint32_t off, uint32_t n) { return off >= FL_PLUG_LO && n <= FL_PLUG_HI - off; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    *took = 0;
    if (!in_region(off, 4096u) || (off & 0xFFFu))
        return n_bad_range++, -1;
    memset(flash + off, 0xFF, 4096u);
    n_erase++;
    return 0;
}
static void plug_sfc(uint8_t *b, uint32_t off, uint32_t n);   /* (core/plugin.c) */
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)   /* the loader programs encrypted bytes; the flash
                                                                      * model keeps what XIP reads: decrypted */
{
    static uint8_t plain[PLUG_SECTOR];
    uint32_t i;
    if (!in_region(off, n) || n > sizeof plain)
        return n_bad_range++, -1;
    memcpy(plain, src, n);
    plug_sfc(plain, off, n);
    for (i = 0; i < n; i++)
        flash[off + i] &= plain[i];
    n_prog++;
    return 0;
}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint32_t st_crc_upd(uint32_t c, const void *p, uint32_t n)   /* storage.c's */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
#include "core/plugin.c"
/* 40 zero bytes at flash 0xA4000 as tools/fm1pkg_make.py sfc() (the same ENC stream) encrypts them, 2026-11-06 */
static const char SFC_VECTOR[] = "0f1e3c78d183060c3953a64cb972c58a356af5cbb74fbf7efcd993072f5e9d3a070e1c385183060c";

/* ---- a plugin file in memory */
typedef struct { uint8_t *d; uint32_t len, fail_at; } file_t;
static int rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    file_t *f = ctx;
    if (off + n > f->len || off + n > f->fail_at)
        return -1;
    memcpy(dst, f->d + off, n);
    return 0;
}
static uint32_t le32(const uint8_t *p) { return plug_le32(p); }
static void st32(uint8_t *p, uint32_t v) { plug_st32(p, v); }

#define IMG_LEN 10000u                                  /* 3 sectors with the header */
#define NREL 6u
static const uint32_t REL[NREL] = {                     /* kind << 28 | image offset */
    0x10u, 0x1000u, (1u << 28) | 0x6Eu, (2u << 28) | 0x200u, 0x1FCEu, (1u << 28) | 0xFCAu};   /* ABS32, ABS32, MOV32, ABS32 straddling placed
                                                         * 0x2000 (image 0x1FCE = placed 0x1FFE), MOV32 straddling 0x1000
                                                         * (image 0xFCA = placed 0xFFA, 8 bytes) */
static uint8_t filebuf[PLUG_HDR_LEN + IMG_LEN + NREL * 4u + 64u];
static uint32_t file_len;
static void mk_hdr(plug_hdr_t *h, uint8_t uid, uint32_t len, uint32_t nrel, uint32_t desc)
{
    memset(h, 0, sizeof *h);
    h->magic = PLUG_MAGIC;
    h->api = PLUG_API;
    h->kind = PLUG_KIND_ENGINE;
    h->uid = uid;
    memcpy(h->name, "TEST", 4);
    h->len = len;
    h->desc = desc;
    h->nrel = nrel;
    h->abi = PLUG_ABI;
    h->apiw = 0xFFFFFFFFu;
}
static void finish_hdr(plug_hdr_t *h, const uint8_t *img) { h->crc = plug_crc(img, h->len); h->hcrc = plug_crc(h, PLUG_HDR_LEN - 4u); }
/* the image: word i = i * 0x01010101 + 7; the MOV32 pairs hold a 32-bit value split as the chip does */
static void mk_file(uint8_t uid, uint32_t len, const uint32_t *rel, uint32_t nrel, uint32_t desc)
{
    plug_hdr_t h;
    uint32_t i;
    uint8_t *img = filebuf + PLUG_HDR_LEN;
    for (i = 0; i < len; i += 4u)
        st32(img + i, i * 0x01010101u + 7u);
    for (i = 0; i < nrel; i++)
        if (PLUG_REL_KIND(rel[i]) == PLUG_REL_MOV32) {    /* value 0x00123456 split: lo16 in w0's top, hi16 in w1's bottom */
            uint32_t o = PLUG_REL_OFF(rel[i]);
            st32(img + o, 0x3456FFC8u);
            st32(img + o + 4, 0x18BA0012u);
        }
    mk_hdr(&h, uid, len, nrel, desc);
    finish_hdr(&h, img);
    memcpy(filebuf, &h, PLUG_HDR_LEN);
    for (i = 0; i < nrel; i++)
        st32(img + len + i * 4u, rel[i]);
    file_len = PLUG_HDR_LEN + len + nrel * 4u;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reset(void)
{
    memset(flash, 0xFF, sizeof flash);
    n_erase = n_prog = n_bad_range = 0;
    plug_init();
}

int main(void)
{
    file_t f;
    int rc;
    uint32_t base0 = FM1_XIP(FL_PLUG_LO) + PLUG_HDR_LEN, i;
    const uint8_t *img;
    plug_hdr_t h;
    printf("region 0x%x..0x%x, %u sectors\n", FL_PLUG_LO, FL_PLUG_HI, PLUG_NSECT);
    CHECK(FL_PLUG_HI == 0x92000u && FL_PLUG_LO == 0x82000u && PLUG_NSECT == 16u, "the region of the normal layout");
    CHECK(sizeof(plug_hdr_t) == PLUG_HDR_LEN, "header %u B", (unsigned)sizeof(plug_hdr_t));

    /* ---- one file installed */
    reset();
    mk_file(23, IMG_LEN, REL, NREL, 0x100u);
    f.d = filebuf, f.len = file_len, f.fail_at = 0xFFFFFFFFu;
    rc = plug_install(rd, &f);
    CHECK(rc == 0, "installed in slot 0: %d", rc);
    CHECK(n_erase == 3u && n_bad_range == 0u, "3 sectors erased (%u), every write in the region (%u out)", n_erase, n_bad_range);
    memcpy(&h, flash + FL_PLUG_LO, PLUG_HDR_LEN);
    CHECK(h.magic == PLUG_MAGIC && h.nrel == 0u && h.base == base0 && h.uid == 23u, "the placed header: base 0x%x", h.base);
    img = flash + FL_PLUG_LO + PLUG_HDR_LEN;
    CHECK(h.crc == plug_crc(img, IMG_LEN) && h.hcrc == plug_crc(&h, PLUG_HDR_LEN - 4u), "its CRCs");
    for (i = 0; i < IMG_LEN; i += 4u) {
        uint32_t want = i * 0x01010101u + 7u, got = le32(img + i), j;
        int touched = 0;
        for (j = 0; j < NREL; j++)
            if (i + 4u > PLUG_REL_OFF(REL[j]) && i < PLUG_REL_OFF(REL[j]) + PLUG_REL_SPAN(PLUG_REL_KIND(REL[j])))
                touched = 1;
        if (!touched)
            CHECK(got == want, "word %u untouched: %08x", i, got);
    }
    CHECK(le32(img + 0x10) == 0x10u * 0x01010101u + 7u + base0, "ABS32 at 0x10 patched: %08x", le32(img + 0x10));
    CHECK(le32(img + 0x1000) == 0x1000u * 0x01010101u + 7u + base0, "ABS32 at 0x1000 patched");
    CHECK(le32(img + 0x200) == PLUG_API_ADDR, "API at 0x200: the table's address, the file's word ignored: %08x", le32(img + 0x200));
    {   /* (0x1FCE is not a word of the pattern: its bytes are the top half of word 0x1FCC and the bottom of 0x1FD0) */
        uint32_t orig = ((0x1FCCu * 0x01010101u + 7u) >> 16) | ((0x1FD0u * 0x01010101u + 7u) << 16);
        CHECK(le32(img + 0x1FCE) == orig + base0, "ABS32 across the sector end patched: %08x", le32(img + 0x1FCE));
    }
    {
        uint32_t w0 = le32(img + 0x6E), w1 = le32(img + 0x72), v = (w0 >> 16) | (w1 & 0xFFFFu) << 16;
        CHECK(v == 0x00123456u + base0 && (w0 & 0xFFFFu) == 0xFFC8u && (w1 >> 16) == 0x18BAu, "MOV32 at 0x6e: %08x %08x", w0, w1);
        w0 = le32(img + 0xFCA), w1 = le32(img + 0xFCE), v = (w0 >> 16) | (w1 & 0xFFFFu) << 16;
        CHECK(v == 0x00123456u + base0 && (w0 & 0xFFFFu) == 0xFFC8u && (w1 >> 16) == 0x18BAu, "MOV32 across the sector end: %08x %08x", w0, w1);
    }
    CHECK(ENGINES[NBUILT] == (const engine_t *)(img + 0x100u) && plug_uid[0] == 23u, "slot 0: the descriptor, UID 23");
    CHECK(plug_uid[1] == 0xFFu && ENGINES[NBUILT + 1] == PLUG_NONE, "slot 1 empty");
    printf("vector 1: placed image CRC %08x (tests/plugin_build_test.py checks the tool's place() against it)\n", h.crc);

    /* ---- a second one, and the same again */
    mk_file(24, 300u, REL, 1u, 0x20u);
    f.d = filebuf, f.len = file_len;
    rc = plug_install(rd, &f);
    CHECK(rc == 1, "the second in slot 1: %d", rc);
    memcpy(&h, flash + FL_PLUG_LO + 3u * PLUG_SECTOR, PLUG_HDR_LEN);
    CHECK(h.magic == PLUG_MAGIC && h.uid == 24u && h.base == FM1_XIP(FL_PLUG_LO + 3u * PLUG_SECTOR) + PLUG_HDR_LEN, "placed after the first's three sectors");
    n_prog = 0;
    rc = plug_install(rd, &f);
    CHECK(rc == 1 && n_prog == 0u, "the same UID again: its slot, nothing written (%d, %u writes)", rc, n_prog);
    mk_file(25, 300u, REL, 1u, 0x20u);
    f.d = filebuf, f.len = file_len;
    rc = plug_install(rd, &f);
    CHECK(rc == PLUG_E_SLOTS, "a third with both slots taken: SLOTS (%d)", rc);

    /* ---- boot, forget, reuse */
    plug_init();
    CHECK(plug_uid[0] == 23u && plug_uid[1] == 24u, "a boot finds both in order: %u %u", plug_uid[0], plug_uid[1]);
    rc = plug_forget(0);
    CHECK(rc == 0 && flash[FL_PLUG_LO] == 0xFFu && flash[FL_PLUG_LO + PLUG_SECTOR] != 0xFFu, "forgotten: the header's sector erased, the next kept");
    CHECK(plug_uid[0] == 0xFFu && plug_uid[1] == 24u && ENGINES[NBUILT] == PLUG_NONE &&
          ENGINES[NBUILT + 1] == (const engine_t *)(flash + FL_PLUG_LO + 3u * PLUG_SECTOR + PLUG_HDR_LEN + 0x20u),
          "the second keeps slot 1, slot 0 empty");
    mk_file(25, 300u, REL, 1u, 0x20u);
    f.d = filebuf, f.len = file_len;
    rc = plug_install(rd, &f);
    CHECK(rc == 0 && le32(flash + FL_PLUG_LO) == PLUG_MAGIC, "the third reuses the first's room and slot 0: slot %d", rc);
    memcpy(&h, flash + FL_PLUG_LO, PLUG_HDR_LEN);
    CHECK(h.uid == 25u && plug_uid[1] == 24u, "at the region's start, the second still in slot 1");

    /* ---- damage */
    flash[FL_PLUG_LO + PLUG_HDR_LEN + 0x80u] ^= 0x10u;    /* (the third's 300-byte image) */
    plug_init();
    CHECK(plug_uid[0] == 24u && plug_uid[1] == 0xFFu, "a flipped byte: not found; a boot lists the other first");
    flash[FL_PLUG_LO + 3u * PLUG_SECTOR + 8u] ^= 1u;      /* the second's header's name */
    plug_init();
    CHECK(plug_uid[0] == 0xFFu, "a header's CRC wrong: not found");
    mk_file(26, 300u, REL, 1u, 0x20u);
    f.d = filebuf, f.len = file_len;
    rc = plug_install(rd, &f);
    CHECK(rc == 0 && le32(flash + FL_PLUG_LO) == PLUG_MAGIC, "the damaged ones' sectors are free again: slot %d at the start", rc);

    /* ---- refused */
    reset();
    mk_file(23, 300u, REL, 1u, 0x20u);
    filebuf[0] ^= 1u;
    f.d = filebuf, f.len = file_len;
    CHECK(plug_install(rd, &f) == PLUG_E_BAD, "not a plugin: BAD");
    mk_file(23, 300u, REL, 1u, 0x20u);
    memcpy(&h, filebuf, PLUG_HDR_LEN);
    h.api = PLUG_API + 1u;
    h.hcrc = plug_crc(&h, PLUG_HDR_LEN - 4u);
    memcpy(filebuf, &h, PLUG_HDR_LEN);
    CHECK(plug_install(rd, &f) == PLUG_E_BAD, "another API: BAD");
    mk_file(23, 300u, REL, 1u, 0x20u);
    memcpy(&h, filebuf, PLUG_HDR_LEN);
    h.abi ^= 0x100u;
    h.hcrc = plug_crc(&h, PLUG_HDR_LEN - 4u);
    memcpy(filebuf, &h, PLUG_HDR_LEN);
    CHECK(plug_install(rd, &f) == PLUG_E_BAD, "another build's structs (ABI): BAD");
    {
        uint32_t bad[1] = {298u};                           /* a word reaching past the 300-byte image */
        mk_file(23, 300u, bad, 1u, 0x20u);
        f.d = filebuf, f.len = file_len;
        rc = plug_install(rd, &f);
        CHECK(rc == PLUG_E_BAD, "a relocation past the image: BAD (%d)", rc);
        bad[0] = (3u << 28) | 0x10u;                        /* a kind the loader does not know */
        mk_file(23, 300u, bad, 1u, 0x20u);
        rc = plug_install(rd, &f);
        CHECK(rc == PLUG_E_BAD, "relocation kind 3: BAD (%d)", rc);
        plug_init();
        CHECK(plug_uid[0] == 0xFFu && flash[FL_PLUG_LO] == 0xFFu, "nothing registered, no header");
    }
    mk_file(23, IMG_LEN, REL, NREL, 0x100u);
    f.d = filebuf, f.len = file_len, f.fail_at = PLUG_HDR_LEN + 5000u;
    rc = plug_install(rd, &f);
    CHECK(rc == PLUG_E_READ, "the reader failing in the middle: READ (%d)", rc);
    plug_init();
    CHECK(plug_uid[0] == 0xFFu && flash[FL_PLUG_LO] == 0xFFu, "nothing registered, no header, the room free");
    f.fail_at = 0xFFFFFFFFu;
    CHECK(plug_install(rd, &f) == 0, "then installed in slot 0");
    {   /* a file larger than what is left: 64 KB region, 3 sectors used: 13 free = 53,248 B */
        static uint8_t big[PLUG_HDR_LEN + 60000u + 4u];
        plug_hdr_t bh;
        mk_hdr(&bh, 27, 60000u, 0u, 0u);
        memset(big + PLUG_HDR_LEN, 0x5A, 60000u);
        finish_hdr(&bh, big + PLUG_HDR_LEN);
        memcpy(big, &bh, PLUG_HDR_LEN);
        f.d = big, f.len = PLUG_HDR_LEN + 60000u;
        rc = plug_install(rd, &f);
        CHECK(rc == PLUG_E_FULL, "60 KB with 52 KB free: FULL (%d)", rc);
        reset();
        rc = plug_install(rd, &f);
        CHECK(rc == 0 && plug_uid[0] == 27u, "60 KB in an empty region: slot 0 (%d)", rc);
        memcpy(&h, flash + FL_PLUG_LO, PLUG_HDR_LEN);
        CHECK(h.crc == plug_crc(flash + FL_PLUG_LO + PLUG_HDR_LEN, 60000u) && flash[FL_PLUG_LO + PLUG_HDR_LEN + 59999u] == 0x5Au,
              "15 sectors written whole");
    }
    {   /* ---- kit heads (PLUG_KIND_HEADS): plain bytes keyed by the kit's hash, beside the engines, no slot */
        plug_hdr_t hh;
        uint32_t base = 0, len = 0, used = 0, fre = 0;
        reset();
        mk_file(23, 300u, REL, 1u, 0x20u);
        f.d = filebuf, f.len = file_len, f.fail_at = 0xFFFFFFFFu;
        CHECK(plug_install(rd, &f) == 0, "an engine first");
        mk_hdr(&hh, 0, 5000u, 0u, 0xC0FFEE12u);           /* the key in desc, the name the kit's */
        hh.kind = PLUG_KIND_HEADS;
        hh.api = 0xFFFFu, hh.abi = 0xDEADBEEFu;           /* (neither checked for heads) */
        memcpy(hh.name, "KIT 808", 8);
        memset(filebuf + PLUG_HDR_LEN, 0x5A, 5000u);
        finish_hdr(&hh, filebuf + PLUG_HDR_LEN);
        memcpy(filebuf, &hh, PLUG_HDR_LEN);
        f.d = filebuf, f.len = PLUG_HDR_LEN + 5000u;
        rc = plug_install(rd, &f);
        CHECK(rc == 0 && plug_uid[1] == 0xFFu, "heads placed (%d), no engine slot taken", rc);
        CHECK(plug_find(PLUG_KIND_HEADS, 0xC0FFEE12u, &base, &len) && base == FM1_XIP(FL_PLUG_LO + PLUG_SECTOR) + PLUG_HDR_LEN && len == 5000u,
              "found by key after the engine's sector: base 0x%x len %u", base, len);
        CHECK(flash[FL_PLUG_LO + PLUG_SECTOR + PLUG_HDR_LEN + 4999u] == 0x5Au, "its bytes whole");
        n_prog = 0;
        CHECK(plug_install(rd, &f) == 0 && n_prog == 0u, "the same key again: nothing written");
        plug_region_kb(&used, &fre);
        CHECK(used == 12u && fre == 52u, "the gauge: %u KB used, %u free", used, fre);
        CHECK(!plug_find(PLUG_KIND_ENGINE, 0xC0FFEE12u, 0, 0) && plug_find(PLUG_KIND_ENGINE, 23u, 0, 0), "kinds apart: the engine by UID");
        hh.nrel = 1;
        finish_hdr(&hh, filebuf + PLUG_HDR_LEN);
        memcpy(filebuf, &hh, PLUG_HDR_LEN);
        CHECK(plug_install(rd, &f) == PLUG_E_BAD, "heads with relocations: BAD (the header's shape is judged first)");
        CHECK(plug_free(PLUG_KIND_HEADS, 0xC0FFEE12u) == 0 && !plug_find(PLUG_KIND_HEADS, 0xC0FFEE12u, 0, 0), "freed: gone");
        plug_region_kb(&used, &fre);
        CHECK(used == 4u && fre == 60u, "the gauge after: %u used", used);
        CHECK(plug_free(PLUG_KIND_HEADS, 0xC0FFEE12u) == -1, "freeing what is not there: -1");
        CHECK(plug_uid[0] == 23u, "the engine untouched through it all");
    }
    {   /* ---- a plugin placed for another firmware: its API word differs from this table's address: not whole at boot */
        uint32_t aw[1] = {(2u << 28) | 0x40u}, used = 0, fre = 0;
        reset();
        mk_file(23, 300u, aw, 1u, 0x20u);                /* the API word at 0x40 (its file value 0) */
        memcpy(&h, filebuf, PLUG_HDR_LEN);
        h.apiw = 0x40u;
        h.hcrc = plug_crc(&h, PLUG_HDR_LEN - 4u);
        memcpy(filebuf, &h, PLUG_HDR_LEN);
        f.d = filebuf, f.len = file_len, f.fail_at = 0xFFFFFFFFu;
        rc = plug_install(rd, &f);
        CHECK(rc == 0 && plug_le32(flash + FL_PLUG_LO + PLUG_HDR_LEN + 0x40u) == test_api, "placed: the word holds this table's address");
        test_api += 0x1000u;                              /* another firmware: its table elsewhere */
        plug_init();
        plug_region_kb(&used, &fre);
        CHECK(plug_uid[0] == 0xFFu && used == 0u, "after an update: not whole, not registered, its sectors free (%u used)", used);
        test_api -= 0x1000u;
        plug_init();
        CHECK(plug_uid[0] == 23u, "the old firmware back: whole again");
    }
    {   /* ---- the SFC cipher: the package tool's stream, an involution, the 32-byte blocks by address */
        uint8_t b[40] = {0}, c[40];
        char hex[81];
        uint32_t i;
        plug_sfc(b, 0xA4000u, 40u);
        for (i = 0; i < 40u; i++)
            hex[2u * i] = "0123456789abcdef"[b[i] >> 4], hex[2u * i + 1u] = "0123456789abcdef"[b[i] & 15u];
        hex[80] = 0;
        CHECK(strcmp(hex, SFC_VECTOR) == 0, "the ENC stream at 0xA4000: %s", hex);
        memset(c, 0, sizeof c);
        plug_sfc(c + 16, 0xA4010u, 24u);            /* the same bytes from the middle of a block */
        CHECK(memcmp(c + 16, b + 16, 24u) == 0, "a write starting inside a block uses the block's stream");
        plug_sfc(b, 0xA4000u, 40u);
        for (i = 0; i < 40u; i++)
            if (b[i])
                break;
        CHECK(i == 40u, "applied twice: the bytes back");
    }
    printf("%d failed\n", fails);
    return fails;
}
