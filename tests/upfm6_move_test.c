/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA_UP_FM6's FM6 voices off the stock firmware's SDK VM (fix/upfm6-off-vm), on a simulated 1 MiB NOR:
 *   - OBJ_UPFM6 lives at 0x95000 / 0x96000 (before the main store; 0x93000..0x94FFF is the app slot's room); saves never touch 0xE7000..0xE9FFF;
 *   - the move at start (storage.c st_upf_move): from the old copy A (0xE7000) or B (0xE8000), the newest valid one
 *     of ours (FELU, type OBJ_UPFM6, the payload's "UPF6"); never the SDK VM (55 AA AA 55); the old sectors are only
 *     read; a cut move is tried again; once moved, never again;
 *   - the flash map (hal/fm1_flash_map.h): the store's allow-list and the never-list over every sector, the edges,
 *     wrapping ranges; the update loader's window without the SDK's own sectors.
 * The erase / program hooks are the firmware's (felucca.c st_erase / st_prog: FL_STORE_OK first). Run by
 * tests/run_tests.sh. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define FELUCCA_UP_FM6 1
#include "../firmware/hal/fm1_flash_map.h"

static uint8_t nor[0x100000];
static int fail_after = -1;                      /* a cut: programs left before the power goes */
static uint32_t bad_writes, refused;            /* an erase / program reaching 0xE7000..0xFBFFF or 0xFF000.. */

static void watch(uint32_t off, uint32_t n)
{
    if ((off < 0xFC000u && off + n > 0xE7000u) || off + n > 0xFF000u)
        bad_writes++;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (!FL_STORE_OK(off, 0x1000u)) {           /* (felucca.c: the same check, then the RAM driver's) */
        refused++;
        return -8;
    }
    watch(off, 4096u);
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (!FL_STORE_OK(off, n)) {
        refused++;
        return -8;
    }
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    watch(off, n);
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"

#define UPF_MAGIC 0x36465055u                    /* "UPF6" (upreset.c) */
#define UPF_LEN 3592u                            /* sizeof(upf_t): magic, used, 32 x 112 B */
#define OLD_A 0xE7000u
#define OLD_B 0xE8000u

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint8_t pay[ST_PAYLOAD_MAX], got[ST_PAYLOAD_MAX], img[0x100000];

static void upf_payload(uint8_t *p, uint32_t seed)
{
    uint32_t i, r = seed * 2654435761u + 7u, m = UPF_MAGIC;
    for (i = 0; i < UPF_LEN; i++) {
        r = r * 1103515245u + 12345u;
        p[i] = (uint8_t)(r >> 16);
    }
    memcpy(p, &m, 4);
}
/* an object as an older Optimist wrote it there (the header of storage.c st_save) */
static void obj_at(uint32_t off, uint32_t type, uint32_t slot, uint32_t seq, const uint8_t *p, uint32_t len)
{
    st_hdr_t h;
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off + ST_PAYLOAD_OFF, p, len);
    h.magic = ST_MAGIC, h.type = (uint16_t)type, h.slot = (uint16_t)slot, h.seq = seq, h.len = len;
    h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + off, &h, sizeof h);
}
/* the stock firmware's SDK VM as read on an FM-1 (magic 55 AA AA 55, a record log from +4) and BTIF beside it */
static void sdk_vm(uint32_t off)
{
    static const uint8_t head[] = {0x55, 0xAA, 0xAA, 0x55, 0x91, 0x6A, 0x20, 0x00, 0x0B, 0x0B, 0x2C, 0x6B, 0x70, 0x00,
                                   0x01, 0x07};
    uint32_t i;
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off, head, sizeof head);
    for (i = sizeof head; i < 0xADu; i++)
        nor[off + i] = (uint8_t)(i * 37u + 11u);
}
static void fresh(void)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    sdk_vm(OLD_B);
    for (i = 0; i < 10u; i++)
        nor[0xE9000u + i] = (uint8_t)(0xB0u + i);       /* BTIF */
    bad_writes = refused = 0;
}
static int same(uint32_t lo, uint32_t hi) { return !memcmp(nor + lo, img + lo, hi - lo); }
static int loads(const uint8_t *p)
{
    return st_load(OBJ_UPFM6, got, sizeof got) == (int)UPF_LEN && !memcmp(got, p, UPF_LEN);
}

static void test_place(void)
{
    uint32_t i, ok = 1;
    fresh();
    memcpy(img, nor, sizeof nor);
    check("OBJ_UPFM6: copies A / B at 0x95000 / 0x96000 (before the main store)",
          st_sector(OBJ_UPFM6, 0) == 0x95000u && st_sector(OBJ_UPFM6, 1) == 0x96000u);
    check("no old object: the move does nothing", st_upf_move(UPF_MAGIC) == 0 && !memcmp(nor, img, sizeof nor));
    for (i = 0; i < 12u; i++) {                  /* twelve saves: both copies, turn about */
        upf_payload(pay, 100u + i);
        ok &= st_save(OBJ_UPFM6, pay, UPF_LEN) == 0 && loads(pay);
    }
    check("12 saves load back", ok);
    check("... with an SDK VM at 0xE8000: 0xE7000..0xFFFFF (but the settings) never erased or written",
          !bad_writes && !refused && same(0xE7000u, 0x100000u));
    check("... only 0x95000..0x96FFF changed (the app's room 0x93000..0x94FFF too untouched)", same(0, 0x95000u) && same(0x97000u, 0x100000u));
}

static void test_move(const char *what, int a_seq, int b_seq, int want_b)
{
    uint8_t pa[UPF_LEN], pb[UPF_LEN];
    int rc;
    char line[160];
    fresh();
    upf_payload(pa, 1), upf_payload(pb, 2);
    if (a_seq)
        obj_at(OLD_A, OBJ_UPFM6, 0, (uint32_t)a_seq, pa, UPF_LEN);
    if (b_seq)
        obj_at(OLD_B, OBJ_UPFM6, 1, (uint32_t)b_seq, pb, UPF_LEN);   /* (B: the VM erased by an older save) */
    memcpy(img, nor, sizeof nor);
    rc = st_upf_move(UPF_MAGIC);
    snprintf(line, sizeof line, "move from %s: the newest (%s) at 0x95000, the voices load", what, want_b ? "B" : "A");
    check(line, rc == 1 && loads(want_b ? pb : pa) && st_sector(OBJ_UPFM6, 0) == 0x95000u);
    check("... 0xE7000..0xE9FFF read only: as they were (no erase, no write)",
          same(0xE7000u, 0xEA000u) && !bad_writes && !refused);
    check("... a second start: nothing moved again, nothing written", st_upf_move(UPF_MAGIC) == 0 && !bad_writes);
}

static void test_refused(void)
{
    uint8_t p[UPF_LEN];
    fresh();
    memcpy(img, nor, sizeof nor);
    check("the SDK VM at 0xE8000 alone: not taken for ours, untouched",
          st_upf_move(UPF_MAGIC) == 0 && st_load(OBJ_UPFM6, got, sizeof got) < 0 && !memcmp(nor, img, sizeof nor));
    upf_payload(p, 3);
    obj_at(OLD_A, 8, 0, 9, p, UPF_LEN);          /* a FELU object of another type (SLOOP 2.4's FM6 bank number) */
    memcpy(img, nor, sizeof nor);
    check("0xE7000: a FELU object of another type: not moved, untouched",
          st_upf_move(UPF_MAGIC) == 0 && !memcmp(nor, img, sizeof nor));
    upf_payload(p, 4);
    p[0] ^= 0x20;                                 /* not "UPF6" */
    obj_at(OLD_A, OBJ_UPFM6, 0, 9, p, UPF_LEN);
    memcpy(img, nor, sizeof nor);
    check("0xE7000: type OBJ_UPFM6 without UPF6 in it: not moved, untouched",
          st_upf_move(UPF_MAGIC) == 0 && !memcmp(nor, img, sizeof nor));
    upf_payload(p, 5);
    obj_at(OLD_A, OBJ_UPFM6, 1, 9, p, UPF_LEN);   /* written to copy B's slot: not where A was written */
    memcpy(img, nor, sizeof nor);
    check("0xE7000: a header naming the other copy: not moved", st_upf_move(UPF_MAGIC) == 0 && !memcmp(nor, img, sizeof nor));
}

static void test_rot_and_cut(void)
{
    uint8_t pa[UPF_LEN], pb[UPF_LEN], pn[UPF_LEN];
    int rc;
    fresh();
    upf_payload(pa, 11), upf_payload(pb, 12);
    obj_at(OLD_A, OBJ_UPFM6, 0, 3, pa, UPF_LEN);
    obj_at(OLD_B, OBJ_UPFM6, 1, 4, pb, UPF_LEN);
    nor[OLD_B + ST_PAYLOAD_OFF + 500] ^= 0x04;    /* B, the newer, rotten */
    memcpy(img, nor, sizeof nor);
    check("B newer but its CRC fails: A moved", st_upf_move(UPF_MAGIC) == 1 && loads(pa) && same(0xE7000u, 0xEA000u));
    fresh();
    obj_at(OLD_A, OBJ_UPFM6, 0, 3, pa, UPF_LEN);
    memcpy(img, nor, sizeof nor);
    fail_after = 5;                                /* the power goes during the move */
    rc = st_upf_move(UPF_MAGIC);
    fail_after = -1;
    check("a cut move: fails, the old copy as it was, nothing at the new place",
          rc < 0 && same(0xE7000u, 0xEA000u) && st_load(OBJ_UPFM6, got, sizeof got) < 0);
    check("... the next start moves it", st_upf_move(UPF_MAGIC) == 1 && loads(pa) && same(0xE7000u, 0xEA000u));
    upf_payload(pn, 13);
    check("... a new save goes to 0x96000, the old copies still as they were",
          st_save(OBJ_UPFM6, pn, UPF_LEN) == 0 && loads(pn) && same(0xE7000u, 0xEA000u) && !bad_writes);
    obj_at(OLD_A, OBJ_UPFM6, 0, 99, pa, UPF_LEN); /* an old copy with a higher seq, after the move */
    memcpy(img, nor, sizeof nor);
    check("moved once: an old copy (even a higher seq) never brought back over the new place",
          st_upf_move(UPF_MAGIC) == 0 && loads(pn) && !memcmp(nor, img, sizeof nor));
}

#define LDR_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x93000u) || FL_IN(off, n, 0x93000u, 0xFC000u))   /* loader.c */
static int ldr_may(uint32_t off, uint32_t n) { return LDR_RANGE_OK(off, n) && !FL_SDK_SYS(off, n); }

static void test_map(void)
{
    uint32_t s, ok = 1, never = 1, sdk = 1;
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        int store = FL_STORE_OK(s, 0x1000u);
        int own = (s >= 0x95000u && s < 0x97000u) || (s >= 0x97000u && s < 0xE0000u) ||
                  (s >= 0xE5000u && s < 0xE7000u) || (s >= 0xFC000u && s < 0xFF000u);
        int sys = (s >= 0xE7000u && s < 0xFC000u) || s >= 0xFF000u;
        ok &= store == own;
        never &= FL_NEVER(s, 0x1000u) == sys;
        sdk &= ldr_may(s, 0x1000u) == ((s >= 0x4000u && s < 0xE7000u) || (s >= 0xEA000u && s < 0xFC000u));
    }
    check("the store's sectors: 0x95000..0x96FFF, 0x97000..0xDFFFF, 0xE5000..0xE6FFF, 0xFC000..0xFEFFF; no other", ok);
    check("never: 0xE7000..0xFBFFF (old UP_FM6 A, SDK VM, BTIF, USR) and 0xFF000.. (key_mac)", never);
    check("the update loader: the app area and its record sweep, but never 0xE7000..0xE9FFF", sdk);
    check("edges: a range ending at 0xE7000 is allowed; one byte over is not; 0xE8000 / 0xE9000 / 0xFF000 never",
          FL_STORE_OK(0xE6F00u, 0x100u) && !FL_STORE_OK(0xE6F00u, 0x101u) && !FL_STORE_OK(0xE8000u, 1u) &&
          !FL_STORE_OK(0xE8FFFu, 1u) && !FL_STORE_OK(0xE9000u, 0x100u) && !FL_STORE_OK(0xFF000u, 1u) &&
          FL_NEVER(0xE6FFFu, 2u) && !FL_NEVER(0xE6FFFu, 1u) && FL_SDK_SYS(0xE8000u, 0x1000u) &&
          FL_SDK_SYS(0xE9F00u, 0x100u) && !FL_SDK_SYS(0xEA000u, 0x1000u) && FL_NEVER(0xEA000u, 0x1000u));
    check("wrapping or huge ranges: refused (0xFFFFFF00 + 0x200, 0x97000 + 4 GiB - 1, 0x95000 + 0xFFFFF000)",
          !FL_STORE_OK(0xFFFFFF00u, 0x200u) && !FL_STORE_OK(0x97000u, 0xFFFFFFFFu) &&
          !FL_STORE_OK(0x95000u, 0xFFFFF000u) && FL_NEVER(0x10000u, 0xF0000u) && FL_SDK_SYS(0xFFFFF000u, 0x1000u));
    check("the app slot's 8 KiB of room (0x93000..0x94FFF) kept free, then the FM6 voices; the update staging is neither",
          FL_APP_HI + 0x2000u == FL_UPF_LO && !FL_STORE_OK(0x92000u, 0x1000u) && !FL_STORE_OK(0x93000u, 0x1000u) &&
          !FL_STORE_OK(0x94000u, 0x1000u) && !FL_STORE_OK(FL_OTA_LO, 0x1000u) &&
          !FL_NEVER(FL_OTA_LO, FL_OTA_HI - FL_OTA_LO));
    fresh();
    memcpy(img, nor, sizeof nor);
    check("the guard: an erase / program of the SDK VM through the store's hooks is refused, the VM whole",
          st_erase(0xE8000u) != 0 && st_prog(0xE8000u, pay, 16) != 0 && st_erase(0xE9000u) != 0 &&
          st_erase(0xE7000u) != 0 && st_prog(0xEA000u, pay, 16) != 0 && !memcmp(nor, img, sizeof nor));
}

int main(void)
{
    test_place();
    test_move("copy A (0xE7000) alone", 5, 0, 0);
    test_move("copy B (0xE8000) alone, where the SDK VM was", 0, 6, 1);
    test_move("A (seq 7) and B (seq 6)", 7, 6, 0);
    test_move("A (seq 7) and B (seq 8)", 7, 8, 1);
    test_refused();
    test_rot_and_cut();
    test_map();
    printf("upfm6 move test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
