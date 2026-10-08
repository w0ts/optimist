/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the stored-RF-calibration reader (firmware/src/ble/ble_vm.c) and of the copy's precedence:
 *   images built from the record format of docs/BLE-HW-FACTS.md §14 (feat/ble-facts d907ce3): the magic, the check
 *   byte (CRC-16/XMODEM low byte over the data), the 12-bit id and length, last-valid-wins, the end at the first bad
 *   check or past the area, which area is live, 187's inner CRC, a read error;
 *   the two raw extracts of docs/ble-traces/v15-vm-trim-map.txt (V15 and demo_ble after a first boot in the emulator:
 *   the emulator's calibration results, not an FM-1's), copied below;
 *   the copy (§15.4): made, checked, damaged; VM -> copy -> none, the copy rewritten only when the VM's set differs;
 *   and that a scan leaves the image as it was (the reader has no write path at all). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ble/ble_vm.c"
#include "../firmware/src/system/bootguard.h"   /* the boot guard ble_boot_radio reads (main.c fm1_cstart) */

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* flash 0x093000 .. 0x096FFF */
static uint8_t img[2 * BLE_VM_AREA];
static int reads, fail_at = -1;
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
    reads++;
    if (off < BLE_VM_BASE || off + n > BLE_VM_BASE + sizeof img)
        return -1;
    if (fail_at >= 0 && (int)(off - BLE_VM_BASE) <= fail_at && fail_at < (int)(off - BLE_VM_BASE + n))
        return -2;
    memcpy(dst, img + (off - BLE_VM_BASE), n);
    return 0;
}

static uint32_t put;                                  /* the next record's offset in img */
static void area(uint32_t a)                          /* 0 = A, 1 = B: the magic, the rest erased */
{
    uint8_t *p = img + a * BLE_VM_AREA;
    memset(p, 0xFF, BLE_VM_AREA);
    p[0] = 0x55, p[1] = 0xAA, p[2] = 0xAA, p[3] = 0x55;
    put = a * BLE_VM_AREA + 4;
}
static void rec(uint32_t id, const uint8_t *d, uint32_t n)
{
    uint8_t *p = img + put;
    p[0] = (uint8_t)ble_crc16_xmodem(0, d, n);
    p[1] = (uint8_t)id;
    p[2] = (uint8_t)((id >> 8) & 15u) | (uint8_t)((n & 15u) << 4);
    p[3] = (uint8_t)(n >> 4);
    memcpy(p + 4, d, n);
    put += 4 + n;
}
static void r187(uint8_t d[68], uint8_t fill)        /* a payload and its inner CRC */
{
    uint32_t i, c;
    for (i = 0; i < 64; i++)
        d[i] = (uint8_t)(fill + i);
    c = ble_crc16_xmodem(0, d, 64);
    d[64] = (uint8_t)c, d[65] = (uint8_t)(c >> 8), d[66] = 0, d[67] = 0;
}

static const uint8_t X106[2] = {0x0B, 0x0B}, X107[7] = {1, 7, 4, 7, 11, 1, 7};
static uint8_t X108[20], X187[68];
static void full_set(uint32_t a)
{
    area(a);
    rec(106, X106, 2);
    rec(107, X107, 7);
    rec(108, X108, 20);
    rec(187, X187, 68);
}

static int hexload(uint8_t *dst, const char *s)
{
    int n = 0;
    unsigned v;
    while (sscanf(s, " %2x", &v) == 1) {
        dst[n++] = (uint8_t)v;
        while (*s == ' ')
            s++;
        s += 2;
    }
    return n;
}

/* docs/ble-traces/v15-vm-trim-map.txt (feat/ble-facts d907ce3): V15 and demo_ble, area A 0x093000, first 0xB0 bytes */
static const char *V15_A =
    "55 aa aa 55 91 6a 20 00 0b 0b 2c 6b 70 00 01 07 04 07 0b 01 07 ea bb 40 04 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40 40 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 ff ff ff ff ff ff ff ff 00 02 00 02 00 02 00 02 00 02 00 02 00 02 00 02 35 1d 00 00 00 6c 40 "
    "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 97 71 e0 00 06 1a 00 00 ff ff ff "
    "ff ff ff ff ff ff ff cc 6d 20 02 a1 55 5b 5b 78 78 a6 a6 60 60 60 fe fe fb fb 29 29 37 02 02 ac "
    "ac e4 e4 30 30 30 27 27 56 56 c8 c8 ca ff ff ff";
static const char *DEMO_A =
    "55 aa aa 55 91 6a 20 00 0b 0b 2c 6b 70 00 01 07 04 07 0b 01 07 00 6c 40 01 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 f6 71 e0 00 07 1a 00 00 ff ff ff ff ff ff ff ff ff ff 0f "
    "6d 20 02 a1 55 86 86 89 89 10 10 94 94 da da f7 f7 f7 3e 3e 69 fc fc 22 22 8e 8e e6 e6 89 89 59 "
    "59 59 31 31 8d ea bb 40 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 40 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff ff ff ff ff ff ff ff 00 02 00 02 00 02 00 "
    "02 00 02 00 02 00 02 00 02 35 1d 00 00 ff ff ff";

static uint32_t seen_ids[16], seen_n;
static void visit(void *ctx, uint32_t off, uint32_t id, uint32_t len)
{
    (void)ctx, (void)off, (void)len;
    if (seen_n < 16)
        seen_ids[seen_n++] = id;
}

static int scan(struct ble_vm_info *in, struct ble_rf_trims *t)
{
    memset(t, 0xEE, sizeof *t);
    seen_n = 0;
    return ble_vm_scan(rd, 0, in, t, visit, 0);
}

int main(void)
{
    struct ble_vm_info in;
    struct ble_rf_trims t, use;
    uint8_t copy[BLE_RF_COPY_SIZE], before[sizeof img], ff[4095];
    uint32_t i;
    int ok, save, src;

    check("CRC-16/XMODEM of \"123456789\" is 0x31C3 (the catalogue's check value)",
          ble_crc16_xmodem(0, (const uint8_t *)"123456789", 9) == 0x31C3u);
    memset(ff, 0xFF, sizeof ff);
    check("an erased header reads as length 0xFFF; CRC of 4095 FF bytes 0x0E1F (§14.2, demo_ble's crc_c 0x1f)",
          ble_crc16_xmodem(0, ff, sizeof ff) == 0x0E1Fu);
    for (i = 0; i < 20; i++)
        X108[i] = (uint8_t)(i * 3);
    r187(X187, 0x40);

    /* the real extracts */
    memset(img, 0xFF, sizeof img);
    check("V15 extract parses", hexload(img, V15_A) == 0xB0);
    ok = scan(&in, &t);
    check("V15: area A live, six records (106 107 187 108 113 109), complete", ok && in.area == BLE_VM_BASE &&
          in.nrec == 6 && seen_ids[0] == 106 && seen_ids[2] == 187 && seen_ids[5] == 109);
    check("V15: 106 = 0B 0B, 107 = 1,7,4,7,11,1,7 (demo_ble's logged xosc and PA_C_I)",
          !memcmp(t.x106, X106, 2) && !memcmp(t.x107, X107, 7));
    check("V15: 187's inner CRC holds (35 1D), the log ends at +0xAD (the first erased header)",
          in.ok187 && t.x187[64] == 0x35 && t.x187[65] == 0x1D && in.end == 0xAD);
    memset(img, 0xFF, sizeof img);
    check("demo_ble extract parses", hexload(img, DEMO_A) == 0xB0);
    ok = scan(&in, &t);
    check("demo_ble: the same record bytes in another order, complete", ok && in.nrec == 6 && seen_ids[2] == 108 &&
          !memcmp(t.x107, X107, 7) && in.ok187);

    /* synthetic: which area, last valid wins, the end of the log */
    memset(img, 0xFF, sizeof img);
    ok = scan(&in, &t);
    check("no magic in either area: no VM, nothing found", !ok && in.area == 0 && in.have == 0);
    full_set(1);
    ok = scan(&in, &t);
    check("only B marked (after a compaction): B is live and complete",
          ok && in.area == BLE_VM_BASE + BLE_VM_AREA && !memcmp(t.x187, X187, 68) && !memcmp(t.x108, X108, 20));
    full_set(0);
    {
        uint8_t odd[2] = {0x0C, 0x0D};
        put = BLE_VM_AREA + 4;                         /* B: another 106 */
        rec(106, odd, 2);
    }
    ok = scan(&in, &t);
    check("both marked: A is used (§14.3, one measured case)", ok && in.area == BLE_VM_BASE && t.x106[0] == 0x0B);
    {
        uint8_t later[2] = {0x0C, 0x0E};
        put = 4 + 4 + 2 + 4 + 7 + 4 + 20 + 4 + 68;
        rec(106, later, 2);
        rec(109, X108, 20);
    }
    ok = scan(&in, &t);
    check("a later 106 supersedes the earlier one (last valid wins)", ok && t.x106[0] == 0x0C && t.x106[1] == 0x0E &&
          in.nrec == 6);
    {
        uint8_t wrong[3] = {1, 2, 3};
        rec(108, wrong, 3);
    }
    ok = scan(&in, &t);
    check("a later 108 of another length: 108 no longer usable", !ok && !(in.have & 4u) && (in.wrong_len & 4u));
    full_set(0);
    img[4 + 4 + 2 + 4] ^= 0x01;                        /* 107's first data byte: its check fails */
    ok = scan(&in, &t);
    check("a bad check after 106 ends the log: 107 / 108 / 187 behind it are invisible",
          !ok && in.have == 1u && in.nrec == 1 && in.end == 4 + 4 + 2);
    full_set(0);
    img[4 + 4 + 2 + 4 + 7 + 4 + 20 + 4 + 10] ^= 0x03;   /* 187's payload, its check byte fixed, inner CRC stale */
    img[4 + 4 + 2 + 4 + 7 + 4 + 20] = (uint8_t)ble_crc16_xmodem(0, img + 4 + 4 + 2 + 4 + 7 + 4 + 20 + 4, 68);
    ok = scan(&in, &t);
    check("187 with a stale inner CRC: found, but the set is not complete (V15 recalibrates there)",
          !ok && (in.have & 8u) && !in.ok187);
    area(0);
    rec(106, X106, 2);
    {   /* valid fillers (at most 4095 bytes each) up to 6 bytes before the end, then a 7-byte 107 that would cross it */
        static uint8_t fill[4000];
        memset(fill, 0x5A, sizeof fill);
        rec(200, fill, 4000);
        rec(200, fill, 4000);
        rec(200, fill, BLE_VM_AREA - 6 - put - 4);
        img[put] = (uint8_t)ble_crc16_xmodem(0, X107, 7), img[put + 1] = 107, img[put + 2] = 0x70, img[put + 3] = 0;
    }
    ok = scan(&in, &t);
    check("a record that would end past the area ends the log", !ok && in.nrec == 4 && in.end == BLE_VM_AREA - 6 &&
          in.have == 1u);
    full_set(0);
    fail_at = 4 + 4 + 2 + 4 + 3;
    ok = scan(&in, &t);
    fail_at = -1;
    check("a flash read error stops the scan, nothing complete", !ok && in.read_err);
    full_set(0);
    memcpy(before, img, sizeof img);
    reads = 0;
    ok = scan(&in, &t);
    check("a scan only reads: the image is unchanged", ok && reads > 0 && !memcmp(before, img, sizeof img));

    /* the copy and the precedence (§15.4) */
    memset(copy, 0, sizeof copy);
    check("an empty copy is not valid", !ble_rf_copy_ok(copy));
    src = ble_rf_choose(ok, &t, in.area, copy, &use, &save);
    check("VM complete, no copy: the VM is used and the copy made (to be saved)",
          src == BLE_RF_FROM_VM && save && ble_rf_copy_ok(copy) && !memcmp(&use, &t, sizeof t) && copy[0] == 0xAA);
    src = ble_rf_choose(ok, &t, in.area, copy, &use, &save);
    check("VM complete, the same copy: VM used, nothing to save", src == BLE_RF_FROM_VM && !save);
    {
        struct ble_rf_trims t2 = t;
        t2.x106[0] ^= 1;
        src = ble_rf_choose(1, &t2, BLE_VM_BASE + BLE_VM_AREA, copy, &use, &save);
        check("VM complete but different from the copy (from B): the copy follows the VM",
              src == BLE_RF_FROM_VM && save && copy[0] == 0xAB && copy[1] == t2.x106[0]);
        ble_rf_copy_make(copy, &t, BLE_VM_BASE);
    }
    memset(&use, 0, sizeof use);
    src = ble_rf_choose(0, &t, 0, copy, &use, &save);
    check("VM gone, the copy valid: the copy's trims are used, nothing saved",
          src == BLE_RF_FROM_COPY && !save && !memcmp(&use, &t, sizeof t));
    copy[50] ^= 0x10;
    check("a damaged copy fails its CRC", !ble_rf_copy_ok(copy));
    src = ble_rf_choose(0, &t, 0, copy, &use, &save);
    check("VM gone, the copy damaged: none (the radio stays off)", src == BLE_RF_NONE && !save);
    copy[50] ^= 0x10;
    copy[1 + 29 + 3] ^= 0x01;                          /* 187's payload with the outer CRC recomputed: inner fails */
    {
        uint32_t c = ble_crc16_xmodem(0, copy, BLE_RF_COPY_SIZE - 2);
        copy[98] = (uint8_t)c, copy[99] = (uint8_t)(c >> 8);
    }
    check("a copy whose 187 fails its inner CRC is not valid", !ble_rf_copy_ok(copy));
    memset(copy, 0xFF, sizeof copy);
    src = ble_rf_choose(0, &t, 0, copy, &use, &save);
    check("an erased copy (FF) and no VM: none", src == BLE_RF_NONE);

    {   /* the boot-time decision with the firmware's boot guard (system/bootguard.h, as main.c fm1_cstart uses it):
         * a saved ON skips the radio for one boot after a start-up that ended in a warm reset (a hang's watchdog) */
        bootguard_t bg;
        uint32_t mode;
        check("boot: OFF never starts the radio, even with trims and no failed start-up", !ble_boot_radio(0, 0, 1));
        check("boot: ON without stored trims: the radio stays off", !ble_boot_radio(1, 0, 0));
        bootguard_clear(&bg);                          /* a power-on */
        mode = bootguard_begin(&bg);
        check("boot guard, power-on with ON saved: a normal boot, the radio starts",
              mode == BOOT_NORMAL && ble_boot_radio(1, bg.failed, 1));
        mode = bootguard_begin(&bg);                   /* that boot hung in rf_init: the watchdog's reset, pending kept */
        check("... it hung, the watchdog reset it: the next boot is normal but leaves the radio off (ON kept)",
              mode == BOOT_NORMAL && bg.failed == 1u && !ble_boot_radio(1, bg.failed, 1));
        bootguard_clear(&bg);                          /* that boot ran 30 s (main.c), the user switched OFF or not */
        mode = bootguard_begin(&bg);
        check("... a boot that ran 30 s clears the count: the next boot starts the radio again",
              mode == BOOT_NORMAL && ble_boot_radio(1, bg.failed, 1));
        mode = bootguard_begin(&bg);                   /* hung again */
        mode = bootguard_begin(&bg);                   /* and the boot without the radio crashed too */
        check("... two failed start-ups in a row: USB rescue, as for any crash (the radio off as well)",
              mode == BOOT_RECOVERY && !ble_boot_radio(1, bg.failed, 1));
        bootguard_clear(&bg);                          /* a power-on (main.c: no failed boot to count) */
        mode = bootguard_begin(&bg);
        check("... a power-off / on clears the count: ON saved starts the radio at that boot",
              mode == BOOT_NORMAL && ble_boot_radio(1, bg.failed, 1));
    }

    printf(fails ? "BLE VM: %d FAILED\n" : "BLE VM: all passed\n", fails);
    return fails != 0;
}
