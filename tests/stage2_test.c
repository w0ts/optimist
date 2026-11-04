/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD flasher's logic (firmware/stage2/s2_flash.c) on packages built here, as tools/fm1pkg_make.py builds them, and
 * a fake flash. With a path argument it opens that real .fwsc too and prints what it found (tests/run_tests.sh compares
 * it with tools/fm1_rescue.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "s2_flash.h"

#define KEY 0x980Fu
#define FLASH_BYTES 0x100000u
#define HEAD_BLOB 0x1000u
#define FLASH_OFF 0x400u          /* flash.bin in the logical image */

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint8_t file[0x110000];
static uint32_t file_len;
static uint8_t flash[FLASH_BYTES];
static unsigned erases, fail_program_at, reads_fail;
static uint8_t fresh[S2_SECTOR], held[S2_SECTOR];

static bool src_read(void *ctx, uint32_t at, void *dst, uint32_t n)
{
    (void)ctx;
    if (reads_fail && !--reads_fail)
        return false;
    if (at > file_len || n > file_len - at)
        return false;
    memcpy(dst, file + at, n);
    return true;
}
static const struct s2_src src = {0, src_read};

static bool nor_read(void *ctx, uint32_t a, void *d, uint32_t n) { (void)ctx; memcpy(d, flash + a, n); return true; }
static bool nor_erase(void *ctx, uint32_t a, uint32_t n)
{
    (void)ctx;
    ++erases;
    memset(flash + a, 0xFF, n);
    return true;
}
static bool nor_program(void *ctx, uint32_t a, const void *s, uint32_t n)
{
    (void)ctx;
    if (fail_program_at && a == fail_program_at)
        return false;
    memcpy(flash + a, s, n);
    return true;
}
static const struct core_nor nor = {{0, FLASH_BYTES, nor_read}, nor_program, nor_erase};

/* A 34-byte chip-key blob that decodes to some key: its 32 bytes plus their CRC. */
static void key_blob(uint8_t *b, uint8_t seed)
{
    for (unsigned i = 0; i < 32; ++i)
        b[i] = (uint8_t)(seed + i * 3);
    core_put_le16(b + 32, core_crc16(0, b, 32));
}

static void head_entry(uint8_t *at, uint32_t blob_at, uint32_t blob_size, const char *name, int last)
{
    uint8_t e[32];
    memset(e, 0, sizeof e);
    core_put_le32(e + 4, blob_at);
    core_put_le32(e + 8, blob_size);
    core_put_le16(e + 14, (uint16_t)last);
    strncpy((char *)e + 16, name, 16);
    core_put_le16(e, core_crc16(0, e + 2, 30));
    core_enc(0xffff, e, 32);
    memcpy(at, e, 32);
}

/* The package: logical image, then the marker bytes woven in. */
static void build(const char *product, uint32_t size, uint8_t seed, int type)
{
    static uint8_t logical[0x110000];
    uint32_t total = FLASH_OFF + size;
    uint8_t *img = logical + FLASH_OFF;
    memset(logical, 0xFF, sizeof logical);
    for (uint32_t i = 0; i < size; ++i)
        img[i] = (uint8_t)(i * 7 + (i >> 12) + seed);
    head_entry(img + 32, 0x4A0 + 0x400, 34, "uboot.boot", 0);
    head_entry(img + 64, 0x500, 34, "isd_config.ini", 1);
    key_blob(img + 0x500, seed);
    uint8_t e[0x50], hdr[0x40];
    memset(e, 0, sizeof e);
    core_put_le16(e, (uint16_t)type);
    core_put_le16(e + 4, core_crc16(0, img, size));
    core_put_le32(e + 8, FLASH_OFF);
    core_put_le32(e + 12, size);
    core_enc(0xffff, e, sizeof e);
    memcpy(logical + 0x40, e, sizeof e);
    memset(hdr, 0, sizeof hdr);
    core_put_le16(hdr + 8, 1);
    memcpy(hdr + 16, "AC791N", 6);
    core_put_le16(hdr, core_crc16(0, hdr + 2, 62));
    core_enc(0xffff, hdr, sizeof hdr);
    memcpy(logical, hdr, sizeof hdr);
    uint32_t w = 0;
    size_t n = strlen(product);
    for (unsigned i = 0; i < 20; ++i) {
        memcpy(file + w, logical + i * 47, 47);
        w += 47;
        file[w++] = i < n ? (uint8_t)(product[i] + i + 1) : 0x7D;
    }
    memcpy(file + w, logical + 20 * 47, total - 20 * 47);
    file_len = w + total - 20 * 47;
}

static uint16_t built_key(uint8_t seed)
{
    uint8_t b[34];
    uint16_t k = 0;
    key_blob(b, seed);
    CHECK(core_chipkey(b, &k));
    return k;
}

static void image_of(uint8_t *out, uint32_t size)
{
    struct s2_pkg p;
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_OK);
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t l = FLASH_OFF + i;
        out[i] = file[l + 20];
    }
}

static void test_open(void)
{
    struct s2_pkg p;
    build("FM-1_720", 0x93000, 3, 0);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_OK);
    CHECK(!strcmp(p.id, "FM-1_720"));
    CHECK(p.size == 0x93000);
    CHECK(p.offset == FLASH_OFF);
    CHECK(s2_pkg_verify(&src, &p, fresh, 0, 0) == S2_OK);
    uint16_t k = 0;
    CHECK(s2_pkg_key(&src, &p, fresh, &k) == S2_OK);
    CHECK(k == built_key(3));
    file[file_len - 5] ^= 1;
    CHECK(s2_pkg_verify(&src, &p, fresh, 0, 0) == S2_E_CRC);
    file[file_len - 5] ^= 1;
    reads_fail = 6;
    CHECK(s2_pkg_verify(&src, &p, fresh, 0, 0) == S2_E_READ);
    reads_fail = 0;

    build("SMK-37 Pro_016", 0x93000, 3, 0);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_E_NOT_FM1);
    build("FM-1_099", 0x93000, 3, 0x64);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_E_NO_FLASH);
    build("FM-1_720", 0x93800, 3, 0);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_E_SIZE);
    build("FM-1_720", 0x4000, 3, 0);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_E_SIZE);
    build("FM-1_720", 0x93000, 3, 0);
    file[0] ^= 0x40;
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_E_NOT_PKG);
    file[0] ^= 0x40;
    CHECK(s2_pkg_open(&src, 100, &p) == S2_E_NOT_PKG);
    CHECK(s2_pkg_open(&src, 0x500 + 20, &p) == S2_E_NOT_PKG);   /* flash.bin past the file's end */
}

static void test_fits(void)
{
    struct s2_pkg p = {0, 0x93000, 0, ""};
    CHECK(s2_pkg_fits(&p, KEY, KEY, 0x93000) == S2_OK);
    CHECK(s2_pkg_fits(&p, KEY, KEY, 0) == S2_OK);               /* nothing mounted: the way back after a broken write */
    CHECK(s2_pkg_fits(&p, KEY, KEY, 0xD0000) == S2_E_LAYOUT);
    CHECK(s2_pkg_fits(&p, KEY, KEY + 1, 0x93000) == S2_E_KEY);
    CHECK(s2_slot_end(0) == 0);
    CHECK(s2_slot_end(0x93000) == 0x93000);
    CHECK(s2_slot_end(0x92100) == 0x93000);                     /* rounded up to the sector */
    CHECK(s2_slot_end(0xD0000 - 0x800) == 0xD0000);
}

static void test_install(void)
{
    static uint8_t want[0x93000], before[FLASH_BYTES];
    struct s2_pkg p;
    build("FM-1_720", 0x93000, 9, 0);
    CHECK(s2_pkg_open(&src, file_len, &p) == S2_OK);
    image_of(want, sizeof want);
    memset(flash, 0x5A, sizeof flash);
    memcpy(flash + 0x4000, want + 0x4000, 0x93000 - 0x4000);
    flash[0x7123] ^= 1;
    flash[0x4010] ^= 1;
    memcpy(before, flash, sizeof before);
    erases = 0;
    CHECK(s2_install(&src, &p, &nor, fresh, held, 0, 0) == S2_OK);
    CHECK(erases == 2);                                         /* only the two sectors that differ */
    CHECK(!memcmp(flash + 0x4000, want + 0x4000, 0x93000 - 0x4000));
    CHECK(!memcmp(flash, before, 0x4000));                      /* the boot area untouched */
    CHECK(!memcmp(flash + 0x93000, before + 0x93000, FLASH_BYTES - 0x93000));   /* the store untouched */

    memset(flash + 0x4000, 0, 0x93000 - 0x4000);
    fail_program_at = 0x20000;
    CHECK(s2_install(&src, &p, &nor, fresh, held, 0, 0) == S2_E_FLASH);
    CHECK(flash[0x4000] == 0);                                  /* the first app sector not reached */
    fail_program_at = 0;
    CHECK(s2_install(&src, &p, &nor, fresh, held, 0, 0) == S2_OK);   /* and written again from the card */
    CHECK(!memcmp(flash + 0x4000, want + 0x4000, 0x93000 - 0x4000));
}

static void test_names(void)
{
    CHECK(s2_is_firmware("OPTIMI~1.FWS"));
    CHECK(s2_is_firmware("k4jam.fws"));
    CHECK(!s2_is_firmware("MENU.BIN"));
    CHECK(!s2_is_firmware("FWS"));
    CHECK(!s2_is_firmware(".FWS"));
    CHECK(!s2_is_firmware("A.FW"));
    CHECK(!s2_is_firmware("_OLDER~4.FWS"));
    CHECK(s2_is_folder("FIRMWA~1"));
    CHECK(s2_is_folder("FIRMWARE"));
    CHECK(s2_is_folder("firmwa~1"));
    CHECK(!s2_is_folder("MBOOT"));
    CHECK(!s2_is_folder("FIRM"));
}

static void real_package(const char *path)
{
    FILE *f = fopen(path, "rb");
    struct s2_pkg p;
    uint16_t k = 0;
    if (!f) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    file_len = (uint32_t)fread(file, 1, sizeof file, f);
    fclose(f);
    enum s2_err e = s2_pkg_open(&src, file_len, &p);
    if (e == S2_OK)
        e = s2_pkg_verify(&src, &p, fresh, 0, 0);
    if (e == S2_OK)
        e = s2_pkg_key(&src, &p, fresh, &k);
    if (e != S2_OK) {
        printf("%s\n", s2_err_text(e));
        exit(1);
    }
    printf("%s size=0x%x crc=0x%04X key=0x%04X\n", p.id, (unsigned)p.size, p.crc, k);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        real_package(argv[1]);
        return 0;
    }
    test_names();
    test_open();
    test_fits();
    test_install();
    printf("stage2_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
