/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin, which must be exactly
 *      [0, LDR_APP_HI): the head and this board's app area, so another
 *      device's package (its app area another size) or one that would write
 *      past the app slot is refused before anything is erased (-15, after
 *      Felucca 1.6.1); the package identity is not in the image, this is not
 *      about it;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   2b. reads the whole flash.bin once and checks it against the CRC16 of its
 *      UFW entry, keeping each app sector's CRC16 (ldr_check): a damaged or
 *      truncated package is refused before anything is erased (after Felucca
 *      1.1.5, which had this); a mismatch: up to two more passes;
 *   3. writes ONLY the app area [0x4000, 0x93000), sector by sector, skipping
 *      sectors that are already equal, and verifies each one; a sector that
 *      comes in different from the checked pass is read again, never written;
 *      the flash head (SPL, isd_config) is never written;
 *   4. asks 0xF0000000 ("success"), then invalidates the update record (RAM
 *      and flash) so the SPL boots the new app, and resets. A damaged package
 *      or no "success" leaves the record: the loader stays (also after a
 *      power cycle) and the host can resume.
 * Power loss during 3: the record is still there, the SPL runs the loader
 * again on the next power-on and the host can resume.
 * The fast install (OTA_FAST_CHUNK, when the host says it can: ldr_pass_fast below) replaces 2b and 3 by one pass
 * over a per-sector CRC table: only the sectors that differ are sent, 4 KiB at a time, each erased while it comes in.
 *
 * Hooks from the platform (loader.c, tests/ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_flash_known()     the flash chip is the 1 MiB part SLOOP's storage expects (else nothing is
 *                         written: the update records go and the old firmware starts again)
 *   ldr_checking(done, total)   the package check (2b), every 32 KB
 *   ldr_progress(done, total)
 *   ldr_erase_begin(off) ldr_erase_end()   the fast install's erase while a sector comes in (no flash access between)
 * plus the ota.c hooks (frames, time, idle). */
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI FL_APP_HI                   /* fm1_flash_map.h: by FELUCCA_LAYOUT (docs/SD-MODE-DESIGN.md) */
#define LDR_REC_LO FL_APP_HI                    /* update records live above the app ... */
#define LDR_REC_HI 0xFC000u                     /* ... and below Felucca's globals */
#define LDR_NSEC ((LDR_APP_HI - LDR_APP_LO) >> 12)
_Static_assert(LDR_APP_HI == FL_APP_HI, "the loader writes the app slot of fm1_flash_map.h (its FELUCCA_LAYOUT)");

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static int ldr_flash_known(void);
static void ldr_progress(uint32_t done, uint32_t total);
static void ldr_checking(uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
static int ldr_chip_key(uint32_t *key)
{
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

#define LDR_EBADPKG (-12)                       /* flash.bin as served does not match the package's CRC16 */
#define LDR_ENOFIN (-13)                        /* no "success" from the host */
#define LDR_EFLASH (-14)                        /* another flash chip: nothing written, the records dropped */
#define LDR_EBOARD (-15)                        /* flash.bin is not this board's app area: nothing erased */

#define LDR_SDK_LO 0xE7000u                     /* never erased: the retired UP_FM6 sector, the stock firmware's SDK */
#define LDR_SDK_HI 0xEA000u                     /* VM (0xE8000: its settings, the radio calibration) and BTIF (0xE9000) */
static uint32_t ldr_crc32(const uint8_t *p, uint32_t n)   /* zlib's, as storage.c st_crc32 */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}
/* a storage object of ours at the sector's head (storage.c: "FELU", its header CRC-32): data, never an update
 * record, whatever its last 256 B hold (the FM6 voices at 0x95000 / 0x96000 end at +0xF08) */
static int ldr_ours(uint32_t sec)
{
    uint8_t h[32];
    return !ldr_fread(sec, h, 32) && ota_rd32(h) == 0x554C4546u && ldr_crc32(h, 28) == ota_rd32(h + 28);
}

/* the update records (RAM, and flash: a 4K boundary - 256) go: the SPL boots the app. Never in the SDK's sectors
 * (0xE7000..0xE9FFF; the erase refuses them too, fm1_flash.h FL_SDK_SYS), nor in a sector holding an object of ours. */
static void ldr_records_drop(void)
{
    uint32_t s;
    ldr_record_clear();
    for (s = LDR_REC_HI; s > LDR_REC_LO; s -= 0x1000u) {
        uint8_t r[80];
        if (s - 0x1000u >= LDR_SDK_LO && s - 0x1000u < LDR_SDK_HI)
            continue;
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0) &&
            !ldr_ours(s - 0x1000u))
            ldr_erase(s - 0x1000u);
    }
}

/* 2c. a switch of step (the MEMORY steps, docs/SD-MODE-DESIGN.md 9.3): the device's own app header (copy at 0x4000,
 * the chip key) says how far the installed app reaches; its end rounded up to a sector names the installed step (the
 * tail after app.bin is under a sector) and FL_ENDS (fm1_steps.h, from tools/flash_layout.py) its R, the top of what
 * it stores elsewhere than normal. One rule for every pair (tools/flash_layout.py switch_clear_ends): this step (end E,
 * its R) erases [E, max(R_old, R)) before anything is written; after another build of the same step (the release's
 * L1 at 0xCE000) only the old code's tail [E, E_old), the store being the same; the same end nothing. No header of
 * ours, or an end below normal's no step has: as after normal (user data may be there). An end above normal's no step
 * has (a newer step): [E, FL_CLEAR_TOP), the safe side.
 * No clear reaches FL_CLEAR_TOP (the kits, the presets, the settings). Run before the app is written, so a resumed
 * install (the record kept) sees the old header again and does the same. */
#define LDR_END_STEP(e, l, r) if (end == (e)) old_l = (l), old_r = (r);
_Static_assert(FL_RELOC_HI >= FL_APP_HI && FL_RELOC_HI <= FL_CLEAR_TOP, "the step's relocated store: above its app, below the kits");
static void ldr_switch_range(uint32_t end, uint32_t *lo, uint32_t *hi)   /* end: the installed app area's, 0 none */
{
    int old_l = -1;
    uint32_t old_r = FL_CLEAR_TOP;
    end = (end + 0xFFFu) & ~0xFFFu;
    FL_ENDS(LDR_END_STEP)
    if (old_l < 0 && end <= FL_APP_HI_NORMAL)
        old_l = 0, old_r = FL_APP_HI_NORMAL;           /* (no header of ours, or an end no slot has below normal's: as
                                                        * after normal) */
    *lo = *hi = 0;
    if (old_l == FELUCCA_LAYOUT) {                     /* the same step: only an old code tail past this app's end */
        if (end > LDR_APP_HI)
            *lo = LDR_APP_HI, *hi = end;
        return;
    }
    *hi = old_r > FL_RELOC_HI ? old_r : FL_RELOC_HI;
    if (*hi > FL_CLEAR_TOP)
        *hi = FL_CLEAR_TOP;
    *lo = *hi > LDR_APP_HI ? LDR_APP_HI : *hi;
}

static int ldr_layout_switch(uint32_t key, uint8_t *b)
{
    uint32_t end, s, lo, hi;
    if (ldr_fread(LDR_APP_LO, b, 32))
        return -8;
    ldr_sfc(b, 32, 0, key);
    end = ota_crc16(b + 2, 30, 0) == ota_rd16(b) ? LDR_APP_LO + ota_rd32(b + 8) : 0u;   /* the app area block's size
                                                     * (fm1pkg_make.py app_area_head); 0 no header of ours */
    ldr_switch_range(end, &lo, &hi);
    for (s = lo; s < hi; s += 0x1000u)
        if (ldr_erase(s))
            return -9;
    return 0;
}

/* 2b. flash.bin [0, fl_size) from the host, before any erase: its CRC16 must be the UFW entry's (want); sc gets
 * the CRC16 of each app sector as it came in (the write pass reads a sector again if it differs) */
static int ldr_check(uint32_t fl_off, uint32_t fl_size, uint32_t want, uint16_t *sc, uint8_t *b)
{
    uint32_t i, n, crc = 0;
    for (i = 0; i < fl_size; i += n) {
        n = fl_size - i > 512u ? 512u : fl_size - i;
        if (ota_read(fl_off + i, b, n))
            return -7;
        crc = ota_crc16(b, n, crc);
        if (!(i & 0x7FFFu))
            ldr_checking(i, fl_size);                /* every 32 KB */
        if (i >= LDR_APP_LO && i < LDR_APP_HI) {     /* (512-byte reads from 0: never across a sector) */
            uint32_t k = (i - LDR_APP_LO) >> 12;
            sc[k] = (uint16_t)ota_crc16(b, n, (i & 0xFFFu) ? sc[k] : 0u);
        }
    }
    return crc == want ? 0 : LDR_EBADPKG;
}

/* 1. and 2.: the UFW header, flash.bin exactly this board's app area, the chip key, a known flash chip */
static int ldr_head(uint8_t *hdr, uint8_t *b, uint32_t *fl_off, uint32_t *want, uint32_t *key)
{
    uint32_t fl_size, ota_off, ota_len;
    int rc;
    if ((rc = ota_ufw(hdr, fl_off, &fl_size, &ota_off, &ota_len, want)) != 0)
        return rc;
    if (!*fl_off)
        return -3;
    if (fl_size != LDR_APP_HI)
        return LDR_EBOARD;                               /* another board's package: refused before any erase */
    /* 2. the package's app area must decrypt with this chip's key */
    if ((rc = ldr_chip_key(key)) != 0)
        return -40 + rc;
    if (ota_read(*fl_off + LDR_APP_LO, b, 32))
        return -5;
    ldr_sfc(b, 32, 0, *key);
    if (ota_crc16(b + 2, 30, 0) != ota_rd16(b))
        return -6;                                       /* package for another chip key */
    if (!ldr_flash_known()) {                            /* SLOOP would run without its storage and could not */
        ldr_records_drop();                              /* be updated again: keep the firmware there is */
        return LDR_EFLASH;
    }
    return 0;
}

/* 4. finish: the host confirms, the record goes, the SPL boots the app */
static int ldr_finish(uint8_t *b)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, b, 8))
            break;
    if (i == 4u)
        return LDR_ENOFIN;
    ldr_records_drop();
    return 0;
}

/* erase (unless erased already: the first try of the fast install's), program, verify: twice at most */
static int ldr_write_sector(uint32_t s, const uint8_t *sec, uint8_t *cur, uint32_t erased)
{
    uint32_t tries, k;
    for (tries = 0; tries < 2u; tries++) {
        if (!(erased && !tries) && ldr_erase(s))
            return -9;
        for (k = 0; k < 0x1000u; k += 256u)
            if (ldr_prog(s + k, sec + k, 256))
                return -10;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (ota_memeq(sec, cur, 0x1000u))
            return 0;
    }
    return -11;                                          /* verify failed twice */
}

static int ldr_pass(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000];
    static uint16_t sc[LDR_NSEC];
    uint32_t k, fl_off, key, s, want, done = 0;
    int rc;
    if ((rc = ldr_head(hdr, sec, &fl_off, &want, &key)) != 0)
        return rc;
    /* 2b. the whole package first: nothing is erased for a damaged one (the head is never written, but is checked) */
    if ((rc = ldr_check(fl_off, LDR_APP_HI, want, sc, sec)) != 0)
        return rc;
    if ((rc = ldr_layout_switch(key, cur)) != 0)     /* 2c. another layout before: its other sectors cleared */
        return rc;
    /* 3. app area, sector by sector */
    for (s = LDR_APP_LO; s < LDR_APP_HI; s += 0x1000u) {
        uint32_t again;
        for (again = 0;; again++) {                      /* as checked in 2b, else read it again */
            for (k = 0; k < 0x1000u; k += 512u)
                if (ota_read(fl_off + s + k, sec + k, 512))
                    return -7;
            if (ota_crc16(sec, 0x1000u, 0) == sc[(s - LDR_APP_LO) >> 12])
                break;
            if (again == 2u)
                return LDR_EBADPKG;                      /* it came in different three times: not written */
        }
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (!ota_memeq(sec, cur, 0x1000u) && (rc = ldr_write_sector(s, sec, cur, 0)) != 0)
            return rc;
        done += 0x1000u;
        ldr_progress(done, LDR_APP_HI - LDR_APP_LO);
    }
    return ldr_finish(sec);
}

#ifdef OTA_FAST_CHUNK
/* The fast install (the host said so before the upgrade: ota.c ota_fast_hello; docs/INSTALL-SPEED.md). One pass instead
 * of two, 4 KiB reads instead of 512 B, and only the sectors that differ come over the wire:
 *   2b'. the host's table of flash.bin, one entry per 4 KiB sector [CRC16 (the UFW's), CRC-32 (zlib's)], read at
 *        LDR_TBL + flash.bin's offset; the CRC16s combined must give the UFW entry's CRC16 of the whole flash.bin, so
 *        the table is the package's (a damaged package or table is refused before anything is erased, as 2b);
 *   3'.  a sector whose flash already has both CRCs is kept and never sent; any other one is erased while it comes in
 *        (one 4 KiB read), checked against both CRCs (three tries: never written otherwise), programmed and verified
 *        byte for byte. A cut-off install resumes the same way: what was written already is skipped.
 * The CRC16 of a sector carried in the table is the same check as 2b's per-sector one; the CRC-32 next to it makes
 * "already there" a 48-bit match */
#define LDR_TBL 0xD0000000u
#define LDR_NALL (LDR_APP_HI >> 12)                      /* flash.bin's sectors, the head's included */
_Static_assert(OTA_FAST_CHUNK == 0x1000u && LDR_NALL * 6u <= OTA_FAST_CHUNK, "one read per sector, the table in one");

static int ldr_table_ok(const uint8_t *t, uint32_t want, uint8_t *zero)
{
    uint32_t k, i, c = 0;
    for (k = 0; k < 0x200u; k++)
        zero[k] = 0;
    for (k = 0; k < LDR_NALL; k++) {                     /* crc(A || B) = crc(A shifted over |B| zeros) ^ crc(B) */
        for (i = 0; i < 0x1000u; i += 0x200u)
            c = ota_crc16(zero, 0x200u, c);
        c ^= ota_rd16(t + k * 6u);
        if (!(k & 31u))
            ota_idle();                                  /* (the watchdog, USB) */
    }
    return c == want;
}

static int ldr_same(const uint8_t *b, const uint8_t *t)
{
    return ota_crc16(b, 0x1000u, 0) == ota_rd16(t) && ldr_crc32(b, 0x1000u) == ota_rd32(t + 2);
}

/* one sector from the host, as the table says, while its erase runs */
static int ldr_fetch(uint32_t src, uint8_t *b, const uint8_t *t)
{
    uint32_t again;
    for (again = 0; again < 3u; again++) {
        if (ota_read(src, b, 0x1000u))
            return -7;
        if (ldr_same(b, t))
            return 0;
    }
    return LDR_EBADPKG;                                  /* it came in different three times: not written */
}

static int ldr_pass_fast(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000], tbl[LDR_NALL * 6u];
    uint32_t fl_off, key, s, want, done = 0;
    int rc;
    if ((rc = ldr_head(hdr, sec, &fl_off, &want, &key)) != 0)
        return rc;
    if (ota_read(LDR_TBL + fl_off, tbl, sizeof tbl))
        return -7;
    if (!ldr_table_ok(tbl, want, sec))
        return LDR_EBADPKG;                              /* nothing erased */
    ldr_checking(1, 1);
    if ((rc = ldr_layout_switch(key, cur)) != 0)
        return rc;
    for (s = LDR_APP_LO; s < LDR_APP_HI; s += 0x1000u) {
        const uint8_t *t = tbl + (s >> 12) * 6u;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (!ldr_same(cur, t)) {
            if (ldr_erase_begin(s))
                return -9;
            rc = ldr_fetch(fl_off + s, sec, t);
            if (ldr_erase_end())
                return -9;
            if (rc)
                return rc;
            if ((rc = ldr_write_sector(s, sec, cur, 1)) != 0)
                return rc;
        }
        done += 0x1000u;
        ldr_progress(done, LDR_APP_HI - LDR_APP_LO);
    }
    return ldr_finish(sec);
}
#endif

/* a pass that does not add up (a byte damaged on the way) is run again: the host answers any read, so it
 * just serves the image once more and the sectors that differ are rewritten; a package that is damaged
 * itself fails every pass and the record stays */
static int ldr_session(void)
{
    int rc = LDR_EBADPKG;
    uint32_t pass;
#ifdef OTA_FAST_CHUNK
    const uint32_t fast = ota_fast;
    ota_fast = 0;                                        /* (the word holds for this session only) */
    for (pass = 0; pass < 3u && rc == LDR_EBADPKG; pass++)
        rc = fast ? ldr_pass_fast() : ldr_pass();
#else
    for (pass = 0; pass < 3u && rc == LDR_EBADPKG; pass++)
        rc = ldr_pass();
#endif
    return rc;
}
