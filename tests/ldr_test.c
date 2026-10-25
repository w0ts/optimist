/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the Felucca update loader (firmware/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc
 *   ldr_test --layout          only the switch of flash layout (ldr_layout_switch), for a build of another
 *                              FELUCCA_LAYOUT (docs/SD-MODE-DESIGN.md) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, bad_range, record_cleared, f0_asked;
static uint32_t corrupt_at, corrupt_once, no_finish;   /* the host serves a damaged byte / never says "success" */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xF0000000u && no_finish) {
        f0_asked++;
        return 0;                                    /* no reply */
    }
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len)
            return 0;                                /* past a cut-short package: the host has nothing to send */
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && addr <= corrupt_at && corrupt_at < addr + len) {
            m[14 + corrupt_at - addr] ^= 0x08;       /* (the frame's own checksum is fine: only the CRC sees it) */
            if (corrupt_once)
                corrupt_at = 0;
        }
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/system/ota.c"

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= 0xFC000u || (off & 0xFFFu)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > FL_APP_HI || ((off & 0xFFu) + n) > 256u) bad_range++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static uint32_t flash_unknown;
static int ldr_flash_known(void) { return !flash_unknown; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
static void ldr_checking(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

/* rewrite flash.bin's size in the logical image's UFW entry list, with its data CRC16 over the new size (the entries'
 * and the header's CRCs too): a consistent package for the same chip whose app area is another board's size (after
 * Felucca 1.6.1's tests; theirs keeps the old data CRC, which our CRC pass would refuse anyway). lg holds at least
 * flash.bin's offset + size bytes. */
static void set_fl_size(uint8_t *lg, uint32_t size)
{
    uint8_t h[0x40 + 11 * 0x50];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) {
            ota_wr32(e + 12, size);
            ota_wr16(e + 4, ota_crc16(lg + ota_rd32(e + 8), size, 0));
        }
        ota_jl_enc(e, 0x50);
    }
    ota_wr16(h + 2, ota_crc16(h + 0x40, n * 0x50u, 0));   /* (over the entries as stored) */
    ota_wr16(h, ota_crc16(h + 2, 0x3E, 0));
    ota_jl_enc(h, 0x40);
    memcpy(lg, h, 0x40 + n * 0x50u);
}

static int check(const char *what, int ok) { printf("%-56s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static void put_record(void)                         /* a valid update record at 0xE4F00 (the SPL runs the loader) */
{
    uint8_t r[112] = {0};
    r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
    ota_wr16(r, ota_crc16(r + 2, 78, 0));
    memcpy(nor + 0xE4F00, r, sizeof r);
}

static void record_at(uint32_t off)                   /* the same record elsewhere (a 4K boundary - 256) */
{
    put_record();
    memcpy(nor + off, nor + 0xE4F00, 112);
    memset(nor + 0xE4F00, 0xFF, 112);
}
/* an object of ours as storage.c writes it: "FELU", type, slot, seq, len, crc, rsv, header CRC-32 */
static void felu_at(uint32_t sec, uint16_t type, uint16_t slot)
{
    uint8_t h[32];
    uint32_t v[5] = {0x554C4546u, (uint32_t)type | (uint32_t)slot << 16, 1u, 3592u, 0x12345678u}, c;
    memcpy(h, v, 20);
    memset(h + 20, 0xFF, 8);
    c = ldr_crc32(h, 28);
    memcpy(h + 28, &c, 4);
    memset(nor + sec, 0xFF, 0x1000);
    memcpy(nor + sec, h, 32);
}

/* the record sweep keeps the SDK's sectors and our objects: a record-looking tail there is never erased. The objects
 * where this layout keeps them (FELUCCA_LAYOUT 1: the presets' FM6 voices at 0xD8000, autosave copy A at 0xD7000) */
static int sweep_test(void)
{
    static uint8_t img[0x100000];
    const uint32_t upf = FL_UPF_LO, obj = LDR_APP_HI == FL_APP_HI_BIG ? 0xD7000u : 0x97000u, bare = LDR_APP_HI;
    int bad = 0;
    uint32_t i;
    bad += check("sweep: its CRC-32 is zlib's, as storage.c's", ldr_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    nor[0xE8000] = 0x55, nor[0xE8001] = 0xAA, nor[0xE8002] = 0xAA, nor[0xE8003] = 0x55;   /* the stock SDK VM */
    for (i = 0; i < 3u; i++)
        record_at(0xE7F00u + i * 0x1000u);                /* 0xE7F00, 0xE8F00, 0xE9F00 */
    felu_at(upf, 10, 0);                                  /* UP_FM6's voices, A / B, each with a record-like tail */
    felu_at(upf + 0x1000u, 10, 1);
    record_at(upf + 0xF00u), record_at(upf + 0x1F00u);
    felu_at(obj, 1, 0);                                   /* a project (layout 1: autosave) object */
    record_at(obj + 0xF00u);
    if (LDR_APP_HI == FL_APP_HI_BIG)
        record_at(0x93F00u);                              /* inside this layout's app: never swept */
    memcpy(img, nor, sizeof nor);
    record_at(bare + 0xF00u);                             /* a bare record (past the app): erased */
    put_record();                                         /* ours, 0xE4F00: erased */
    erases = 0;
    ldr_records_drop();
    bad += check("sweep: the records at 0xE4F00 and past the app erased", nor[0xE4F06] == 0xFF &&
                 nor[bare + 0xF06u] == 0xFF && erases == 2);
    bad += check("sweep: 0xE7000..0xE9FFF (SDK VM, BTIF) never erased", !memcmp(nor + 0xE7000, img + 0xE7000, 0x3000));
    bad += check("sweep: UP_FM6's two sectors and a project kept",
                 !memcmp(nor + upf, img + upf, 0x2000) && !memcmp(nor + obj, img + obj, 0x1000));
    if (LDR_APP_HI == FL_APP_HI_BIG)
        bad += check("sweep: a record-like tail inside the app kept", nor[0x93F06] == img[0x93F06]);
    nor[upf + 1u] ^= 1;                                   /* a broken head: not ours, the record goes */
    ldr_records_drop();
    bad += check("sweep: a sector whose FELU head is broken is not taken for ours", nor[upf + 0xF06u] == 0xFF);
    return bad;
}

/* the switch of flash layout (ldr_core.c ldr_layout_switch): the installed app's own header at 0x4000 (SFC'd with the
 * chip key) says how far it reaches; the sectors the other layout used differently are erased, nothing else */
static void installed_head(uint32_t key, uint32_t blk)
{
    uint8_t e[32];
    uint32_t c;
    memset(e, 0xFF, sizeof e);
    e[4] = 0x20; e[5] = 0x01; e[6] = 0x00; e[7] = 0x02;               /* (0x02000120: fm1pkg_make.py app_area_head) */
    e[8] = (uint8_t)blk; e[9] = (uint8_t)(blk >> 8); e[10] = (uint8_t)(blk >> 16); e[11] = (uint8_t)(blk >> 24);
    c = ota_crc16(e + 2, 30, 0);
    e[0] = (uint8_t)c; e[1] = (uint8_t)(c >> 8);
    ldr_sfc(e, 32, 0, key);
    memcpy(nor + 0x4000, e, sizeof e);
}

static int all(uint32_t lo, uint32_t hi, uint8_t v)
{
    uint32_t i;
    for (i = lo; i < hi; i++)
        if (nor[i] != v)
            return 0;
    return 1;
}

static int layout_switch_test(void)
{
    static const uint32_t KEY = 0x980Fu, NORMAL_BLK = 0x8E000u, BIG_BLK = 0xCB25Bu;   /* (blocks as the packages have) */
    static const char *const WHAT[3] = {"after a normal app", "after a fake-SD-mode app", "after no header of ours"};
    uint8_t buf[32];
    int bad = 0, k, rc;
    for (k = 0; k < 3; k++) {
        char what[96];
        memset(nor, 0xFF, sizeof nor);
        memset(nor + 0x5000, 0xA5, 0xE0000u - 0x5000u);               /* the app, the store: all non-erased */
        if (k < 2)
            installed_head(KEY, k ? BIG_BLK : NORMAL_BLK);
        erases = 0; bad_range = 0;
        rc = ldr_layout_switch(KEY, buf);
        snprintf(what, sizeof what, "layout %d loader, %s", FELUCCA_LAYOUT, WHAT[k]);
        if (LDR_APP_HI == FL_APP_HI_NORMAL && k == 1)                  /* back to normal: the old app's tail cleared */
            bad += check(what, rc == 0 && erases == 61u && all(0x93000u, 0xD0000u, 0xFF) && all(0x5000u, 0x93000u, 0xA5)
                         && all(0xD0000u, 0xE0000u, 0xA5));
        else if (LDR_APP_HI == FL_APP_HI_BIG && k != 1)                /* to the fake SD mode: its store cleared */
            bad += check(what, rc == 0 && erases == 10u && all(0xD0000u, 0xDA000u, 0xFF) && all(0x5000u, 0xD0000u, 0xA5)
                         && all(0xDA000u, 0xE0000u, 0xA5));
        else
            bad += check(what, rc == 0 && erases == 0 && all(0x5000u, 0xE0000u, 0xA5));
        bad += check("... nothing outside the loader's windows", bad_range == 0);
    }
    return bad;
}

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo;
    int bad = 0, rc;
    if (argc == 2 && !strcmp(argv[1], "--layout")) {
        bad = layout_switch_test();
        printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
        return bad != 0;
    }
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc | --layout\n"); return 2; }
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    /* device: OLD installed, plus a valid update record at 0xE4F00 */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, 0x93000);
    memcpy(head, nor, sizeof head);
    put_record();
    /* refused before anything is erased (the app area, the records): another board's package (an SMK-37's app area
     * ends at 0x9D000; a stock-loader package such as Baud Girl's writes to 0xB7000; one byte short), and a package
     * cut short inside flash.bin. The record stays: the host can serve the right package. */
    {
        static const uint32_t SZ[3] = {0x9D000u, 0xB7000u, LDR_APP_HI - 1u};
        static const char *const WHAT[3] = {"another board's size (0x9D000): -15, nothing erased",
                                            "a flash.bin past the app area (0xB7000): -15, nothing erased",
                                            "a flash.bin a byte short of the app area: -15, nothing erased"};
        static uint8_t big[0x100000];                /* room for the larger flash.bin (0xFF past the package) */
        uint8_t *const mine = logical;
        size_t full = logical_len;
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            memset(big, 0xFF, sizeof big);
            memcpy(big, mine, full);
            logical = big;
            logical_len = nfo + SZ[k] > full ? nfo + SZ[k] : full;
            set_fl_size(logical, SZ[k]);
            erases = 0; record_cleared = 0;
            rc = ldr_session();
            printf("  rc %d, %u sector erases\n", rc, erases);
            bad += check(WHAT[k], rc == -15 && erases == 0 && !record_cleared && nor[0xE4F06] == 0x41 &&
                         !memcmp(nor, old + ofo, 0x93000));
            memcpy(nor, old + ofo, 0x93000);         /* (the old firmware back, whatever happened) */
            put_record();
            logical = mine;
            logical_len = full;
        }
        logical_len = nfo + 0x50000u;                /* cut short in the app area */
        erases = 0; record_cleared = 0;
        rc = ldr_session();
        bad += check("cut short inside flash.bin: refused, nothing erased", rc < 0 && erases == 0 && !record_cleared &&
                     !memcmp(nor, old + ofo, 0x93000));
        logical_len = full;
        requests = 0;
    }
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes (flash.bin exactly the app area)", rc == 0);
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, logical + nfo + 0x4000, 0x93000 - 0x4000));
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update record cleared (RAM + flash)", record_cleared && nor[0xE4F06] == 0xFF);
    bad += check("no write outside the allowed windows", bad_range == 0);
    /* same package again: nothing to erase */
    erases = 0; requests = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
    /* a package for another chip key is refused before any erase */
    {
        uint8_t save = logical[nfo + 0x4000 + 5];
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
        memcpy(nor, old + ofo, 0x93000);
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[nfo + 0x4000 + 5] = save;
    }
    /* one bit damaged on the way, once: the CRC sees it, the next pass rewrites that sector */
    {
        const uint8_t r6 = 0x41;                     /* byte 6 of a valid record ("AT"); 0xFF once erased */
        memcpy(nor, old + ofo, 0x93000);
        put_record();
        record_cleared = 0;
        corrupt_at = nfo + 0x30123u;
        corrupt_once = 1;
        rc = ldr_session();
        bad += check("a byte damaged once: caught by the CRC, fixed by a 2nd pass", rc == 0 && record_cleared
                     && nor[0xE4F06] == 0xFF && !memcmp(nor + 0x4000, logical + nfo + 0x4000, 0x93000 - 0x4000));
        /* the package itself damaged (every pass): the record stays, nothing is booted */
        memcpy(nor, old + ofo, 0x93000);
        put_record();
        record_cleared = 0;
        corrupt_at = nfo + 0x30123u;
        corrupt_once = 0;
        erases = 0;
        rc = ldr_session();
        bad += check("a damaged package: refused after 3 passes, the record stays", rc == -12 && !record_cleared
                     && nor[0xE4F06] == r6);
        bad += check("... and the working app was never erased or written (checked before anything is erased)",
                     erases == 0 && !memcmp(nor, old + ofo, 0x93000));
        corrupt_at = nfo + 0x100u;                   /* in the head: never written, still checked */
        rc = ldr_session();
        bad += check("... also when the damage is in the head", rc == -12 && !record_cleared && erases == 0
                     && !memcmp(nor, old + ofo, 0x93000));
        corrupt_at = nfo + 0x91000u;                 /* the last app sector */
        rc = ldr_session();
        bad += check("... and in the last app sector", rc == -12 && erases == 0 && !memcmp(nor, old + ofo, 0x93000));
        corrupt_at = 0;
        /* no "success" from the host: the record stays (the host can finish later) */
        no_finish = 1;
        rc = ldr_session();
        bad += check("no \"success\" from the host: the record stays", rc == -13 && !record_cleared && nor[0xE4F06] == r6);
        no_finish = 0;
        rc = ldr_session();
        bad += check("... and the next session finishes", rc == 0 && record_cleared && nor[0xE4F06] == 0xFF);
    }
    /* another flash chip: nothing written, the records go (the old firmware starts again) */
    {
        memcpy(nor, old + ofo, 0x93000);
        put_record();
        record_cleared = 0;
        erases = 0;
        flash_unknown = 1;
        rc = ldr_session();
        bad += check("unknown flash chip: nothing written, record dropped", rc == -14 && record_cleared && erases == 1
                     && nor[0xE4F06] == 0xFF && !memcmp(nor, old + ofo, 0x93000));
        flash_unknown = 0;
    }
    bad += sweep_test();
    bad += layout_switch_test();
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
