/* SPDX-License-Identifier: GPL-3.0-only */
/* See s2_flash.h. The package format as tools/fm1pkg_make.py writes it and tools/fm1_rescue.py reads it: a marker byte
 * after each of the first 20 47-byte blocks (the identity), then a 64-byte header and 80-byte entries, each ENC'd. */
#include "s2_flash.h"
#include <string.h>
#define FELUCCA_LAYOUT 0          /* (fm1_steps.h wants one; only its table of ends is used here) */
#include "fm1_steps.h"

#define MARK_BLOCKS 20u
#define BLOCK 48u
#define KEEP 47u
#define MARK_END 0x7Du            /* the marker after the identity's last character */
#define LOGICAL_HEAD (MARK_BLOCKS * KEEP)
#define HEADER 0x40u
#define ENTRY 0x50u
#define MAX_ENTRIES 16u
#define HEAD_ENTRIES 0x400u       /* flash.bin's head: 32-byte entries up to here */
#define ENTRY32 32u
#define KEY_BLOB 34u              /* the chip-key record: 32 bytes and their CRC */
#define MODEL "FM-1_"
#define ISD_NAME "isd_config.ini"

const char *s2_err_text(enum s2_err e)
{
    static const char *const text[] = {
        "OK", "CARD READ ERROR", "NOT A PACKAGE", "NOT FOR THE FM-1", "NO FLASH IMAGE", "BAD IMAGE SIZE",
        "PACKAGE DAMAGED", "OTHER CHIP KEY", "LAYOUT CHANGE: USE USB", "FLASH WRITE FAILED"
    };
    return (unsigned)e < sizeof text / sizeof text[0] ? text[e] : "ERROR";
}

bool s2_is_firmware(const char *name)
{
    size_t n = strlen(name);
    if (n < 5 || name[n - 4] != '.' || name[0] == '_')   /* (_: macOS's ._ sidecar files, as 8.3 names) */
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        char c = name[n - 3 + i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (c != "FWS"[i])
            return false;
    }
    return true;
}

bool s2_is_folder(const char *name)
{
    static const char prefix[] = "FIRMWA";
    for (unsigned i = 0; i < sizeof prefix - 1; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (c != prefix[i])
            return false;
    }
    return true;
}

#define LFN_ATTR 0x0Fu
#define LFN_ATTR_MASK 0x3Fu
#define LFN_LAST 0x40u
#define LFN_SEQ 0x1Fu
#define LFN_CHARS 13u
#define DIR_ATTR 11u
#define DIR_SUM 13u
#define DIR_DELETED 0xE5u
#define SHORT_NAME 11u

static uint8_t short_sum(const uint8_t *e)
{
    uint8_t sum = 0;
    for (unsigned i = 0; i < SHORT_NAME; ++i)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + e[i]);
    return sum;
}

static void lfn_piece(struct s2_lfn *l, const uint8_t *e)
{
    static const uint8_t at[LFN_CHARS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    unsigned pos = ((e[0] & LFN_SEQ) - 1u) * LFN_CHARS;
    for (unsigned i = 0; i < LFN_CHARS && pos + i < S2_LFN_MAX; ++i) {
        unsigned c = core_le16(e + at[i]);
        if (!c || c == 0xffffu)
            return;
        l->text[pos + i] = c < 0x7f ? (char)c : '?';
    }
}

void s2_lfn_feed(struct s2_lfn *l, const uint8_t entry[32])
{
    if ((entry[DIR_ATTR] & LFN_ATTR_MASK) != LFN_ATTR) {
        l->valid = l->live && short_sum(entry) == l->sum;
        l->live = false;
        return;
    }
    if (entry[0] == DIR_DELETED || !(entry[0] & LFN_SEQ)) {
        l->live = false;
        return;
    }
    if (entry[0] & LFN_LAST) {
        memset(l->text, 0, sizeof l->text);
        l->sum = entry[DIR_SUM];
        l->live = true;
    }
    if (l->live && l->sum == entry[DIR_SUM])
        lfn_piece(l, entry);
    else
        l->live = false;
}

void s2_label(char out[S2_LABEL_MAX + 1], const struct s2_lfn *l, const char *short_name)
{
    const char *from = l->valid ? l->text : short_name;
    size_t n = strlen(from);
    if (l->valid && n > 5 && from[n - 5] == '.')   /* .fwsc */
        n -= 5;
    if (n > S2_LABEL_MAX)
        n = S2_LABEL_MAX;
    memcpy(out, from, n);
    out[n] = 0;
}

/* Logical bytes: the marker bytes skipped. */
static bool logical_read(const struct s2_src *src, uint32_t at, uint8_t *dst, uint32_t n)
{
    while (n) {
        uint32_t k = n, raw = at + MARK_BLOCKS;
        if (at < LOGICAL_HEAD) {
            uint32_t rest = KEEP - at % KEEP;
            raw = at / KEEP * BLOCK + at % KEEP;
            if (k > rest)
                k = rest;
        }
        if (!src->read(src->ctx, raw, dst, k))
            return false;
        at += k;
        dst += k;
        n -= k;
    }
    return true;
}

static enum s2_err read_identity(const struct s2_src *src, struct s2_pkg *pkg)
{
    unsigned i;
    for (i = 0; i < MARK_BLOCKS; ++i) {
        uint8_t m;
        if (!src->read(src->ctx, i * BLOCK + KEEP, &m, 1))
            return S2_E_READ;
        if (m == MARK_END)
            break;
        pkg->id[i] = (char)(m - (i + 1));
    }
    pkg->id[i] = 0;
    return i < sizeof MODEL - 1 || memcmp(pkg->id, MODEL, sizeof MODEL - 1) ? S2_E_NOT_FM1 : S2_OK;
}

enum s2_err s2_pkg_open(const struct s2_src *src, uint32_t file_size, struct s2_pkg *pkg)
{
    uint8_t head[HEADER], entry[ENTRY];
    struct s2_pkg found;
    memset(&found, 0, sizeof found);
    if (file_size < MARK_BLOCKS * BLOCK + HEADER)
        return S2_E_NOT_PKG;
    if (!logical_read(src, 0, head, HEADER))
        return S2_E_READ;
    core_enc(0xffff, head, HEADER);
    unsigned count = core_le16(head + 8);
    if (core_crc16(0, head + 2, HEADER - 2) != core_le16(head) || memcmp(head + 16, "AC791N", 6) || !count ||
        count >= MAX_ENTRIES)
        return S2_E_NOT_PKG;
    enum s2_err err = read_identity(src, &found);
    if (err != S2_OK)
        return err;
    for (unsigned i = 0; i < count; ++i) {
        if (!logical_read(src, HEADER + i * ENTRY, entry, ENTRY))
            return S2_E_READ;
        core_enc(0xffff, entry, ENTRY);
        if (core_le16(entry))
            continue;
        found.crc = core_le16(entry + 4);
        found.offset = core_le32(entry + 8);
        found.size = core_le32(entry + 12);
        uint32_t logical_size = file_size - MARK_BLOCKS;
        if (found.offset > logical_size || found.size > logical_size - found.offset)
            return S2_E_NOT_PKG;
        if (found.size % S2_SECTOR || found.size <= S2_APP_LO || found.size > S2_STORE_TOP)
            return S2_E_SIZE;
        *pkg = found;
        return S2_OK;
    }
    return S2_E_NO_FLASH;
}

enum s2_err s2_pkg_verify(const struct s2_src *src, const struct s2_pkg *pkg, uint8_t *buf, s2_progress cb, void *ctx)
{
    uint16_t crc = 0;
    for (uint32_t a = 0; a < pkg->size; a += S2_SECTOR) {
        if (!logical_read(src, pkg->offset + a, buf, S2_SECTOR))
            return S2_E_READ;
        crc = core_crc16(crc, buf, S2_SECTOR);
        if (cb)
            cb(ctx, a + S2_SECTOR, pkg->size);
    }
    return crc == pkg->crc ? S2_OK : S2_E_CRC;
}

/* The chip-key blob a head entry names, as its key. */
static enum s2_err key_of_entry(const struct s2_src *src, const struct s2_pkg *pkg, const uint8_t *e, uint16_t *key)
{
    uint8_t blob[KEY_BLOB];
    uint32_t at = core_le32(e + 4), size = core_le32(e + 8);
    if (size < KEY_BLOB || at < HEAD_ENTRIES || at > S2_APP_LO || size > S2_APP_LO - at)
        return S2_E_NOT_PKG;
    if (!logical_read(src, pkg->offset + at, blob, sizeof blob))
        return S2_E_READ;
    return core_chipkey(blob, key) ? S2_OK : S2_E_KEY;
}

enum s2_err s2_pkg_key(const struct s2_src *src, const struct s2_pkg *pkg, uint8_t *buf, uint16_t *key)
{
    if (!logical_read(src, pkg->offset, buf, S2_SECTOR))
        return S2_E_READ;
    for (uint32_t off = ENTRY32; off < HEAD_ENTRIES; off += ENTRY32) {
        uint8_t e[ENTRY32];
        memcpy(e, buf + off, ENTRY32);
        core_enc(0xffff, e, ENTRY32);
        if (core_crc16(0, e + 2, ENTRY32 - 2) != core_le16(e))
            return S2_E_NOT_PKG;
        if (!memcmp(e + 16, ISD_NAME, sizeof ISD_NAME))
            return key_of_entry(src, pkg, e, key);
        if (core_le16(e + 14))     /* the list's last entry */
            break;
    }
    return S2_E_KEY;
}

#define S2_END_ROW(end, layout, reloc) (end),
static const uint32_t known_ends[] = {FL_ENDS(S2_END_ROW)};

uint32_t s2_slot_end(uint32_t head_end)
{
    uint32_t up = (head_end + S2_SECTOR - 1) & ~(S2_SECTOR - 1), next = 0;
    if (!head_end)
        return 0;
    for (unsigned i = 0; i < sizeof known_ends / sizeof known_ends[0]; ++i) {
        if (known_ends[i] == up)
            return up;
        if (known_ends[i] >= head_end && (!next || known_ends[i] < next))
            next = known_ends[i];
    }
    return next ? next : up;
}

enum s2_err s2_pkg_fits(const struct s2_pkg *pkg, uint16_t pkg_key, uint16_t unit_key, uint32_t old_end)
{
    if (pkg_key != unit_key)
        return S2_E_KEY;
    return old_end && old_end != pkg->size ? S2_E_LAYOUT : S2_OK;
}

static enum s2_err write_sector(const struct s2_src *src, const struct s2_pkg *pkg, const struct core_nor *nor,
                                uint32_t a, uint8_t *fresh, uint8_t *held)
{
    if (!logical_read(src, pkg->offset + a, fresh, S2_SECTOR))
        return S2_E_READ;
    if (!core_read(&nor->flash, a, held, S2_SECTOR))
        return S2_E_FLASH;
    if (!memcmp(fresh, held, S2_SECTOR))
        return S2_OK;
    if (!nor->erase(nor->flash.ctx, a, S2_SECTOR) || !nor->program(nor->flash.ctx, a, fresh, S2_SECTOR) ||
        !core_read(&nor->flash, a, held, S2_SECTOR) || memcmp(fresh, held, S2_SECTOR))
        return S2_E_FLASH;
    return S2_OK;
}

enum s2_err s2_install(const struct s2_src *src, const struct s2_pkg *pkg, const struct core_nor *nor, uint8_t *fresh,
                       uint8_t *held, s2_progress cb, void *ctx)
{
    uint32_t total = pkg->size - S2_APP_LO;
    for (uint32_t a = S2_APP_LO + S2_SECTOR; a < pkg->size; a += S2_SECTOR) {
        enum s2_err err = write_sector(src, pkg, nor, a, fresh, held);
        if (err != S2_OK)
            return err;
        if (cb)
            cb(ctx, a - S2_APP_LO, total);
    }
    enum s2_err err = write_sector(src, pkg, nor, S2_APP_LO, fresh, held);
    if (err == S2_OK && cb)
        cb(ctx, total, total);
    return err;
}
