/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#include "../../hal/fm1_flash_map.h"          /* FL_STORE_OK, FL_NEVER (relative: the host tests include this file) */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000 / 0xFD000, projects
 * 0x97000..0x9EFFF, user sample slots 0xA0000..0xD7FFF (eng_sample.c: USR3 64 KiB), the banks area
 * 0xD8000..0xDBFFF: the FM6 user bank 0xD8000 (header) / 0xD9000 (data) (fm6_store.c), the user drum kit
 * bank 0xDA000 / 0xDB000 (drum_kits.c); user preset banks 0xDC000..0xDFFFF
 * (upreset.c); the working project (autosave, project.c): copy A 0x9F000, copy B 0xFE000 (the two sectors
 * left: A/B needs no two neighbours); the projects' drum records (drum_store.c) 0xE5000 / 0xE6000, in
 * FL_DLANE (hal/fm1_flash_map.h: after the update loader's staging 0xE0000..0xE4FFF); 0x95000 / 0x96000 (the two
 * sectors before the main store; 0x93000..0x94FFF stays the app slot's room to grow): the user presets' FM6
 * voices with FELUCCA_UP_FM6 (upreset.c), else free. Never written (hal/fm1_flash_map.h FL_NEVER): 0xE7000
 * (retired, UP_FM6's old copy A), 0xE8000 (the stock firmware's SDK VM: its settings and radio calibration;
 * UP_FM6's old copy B was there), 0xE9000 (BTIF), 0xEA000..0xFBFFF (the SDK's USR), 0xFF000 (key_mac); with
 * FELUCCA_SNAPSHOTS the snapshot area ends USR3 below the banks: (slots + 4) x 4 KiB up to 0xD8000 (snap_store.c);
 * with FELUCCA_NATIVE_BANKS (and CZ) the CZ collection's A/B pair just below it (cz_bank.c OBJ_CZBANK) */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_AUTOSAVE = OBJ_UPRESET0 + 2, OBJ_UKIT,
       OBJ_DLANES,
#if FELUCCA_UP_FM6 || CZ_NUSER
       OBJ_UPFM6,                              /* the user presets' FM6 voices (upreset.c), 0x95000 / 0x96000 */
#endif
#if CZ_NUSER
       OBJ_CZBANK,                             /* the CZ collection (cz_bank.c): its number the same with UP_FM6 or not */
#endif
       OBJ_COUNT };
#define ST_UKIT_SECTOR 0xDA000u                /* the banks area's last 8 KiB (eng_sample.c SMP_BANKS: the FM6 bank before) */
#define ST_DLANES_SECTOR 0xE5000u              /* FL_DLANE_LO */
#define ST_UPF_SECTOR FL_UPF_LO                /* 0x95000: OBJ_UPFM6 (until fix/upfm6-off-vm 0xE7000 / 0xE8000: st_upf_move) */
#define ST_UPF_OLD FL_OLD_UPF_LO               /* 0xE7000: the old copy A; B at 0xE8000 (read only, never written) */
/* the flash map, checked when the firmware builds: every fixed object sector is Optimist's own, none the SDK's */
_Static_assert(FL_STORE_OK(0xFC000u, 2u * ST_SECTOR) && FL_STORE_OK(0x9F000u, ST_SECTOR) &&
               FL_STORE_OK(0xFE000u, ST_SECTOR) && FL_STORE_OK(ST_UKIT_SECTOR, 2u * ST_SECTOR) &&
               FL_STORE_OK(ST_DLANES_SECTOR, 2u * ST_SECTOR) && FL_STORE_OK(0x97000u, 8u * ST_SECTOR) &&
               FL_STORE_OK(0xDC000u, 4u * ST_SECTOR), "storage objects: inside the store's allow-list");
#if FELUCCA_UP_FM6
_Static_assert(FL_STORE_OK(ST_UPF_SECTOR, 2u * ST_SECTOR), "fm1_flash_map.h included before FELUCCA_UP_FM6 was set");
#endif
#ifdef SMP_USER_BASE                             /* (eng_sample.c before: the user sample slots, the banks) */
_Static_assert(FL_STORE_OK(SMP_USER_BASE, SMP_USER_SLOTS * SMP_USER_SIZE), "user sample slots: in the store");
#endif
_Static_assert(FL_IN(ST_UPF_SECTOR, 2u * ST_SECTOR, FL_UPF_LO, FL_UPF_HI) && !FL_NEVER(ST_UPF_SECTOR, 2u * ST_SECTOR) &&
               ST_UPF_SECTOR >= FL_APP_HI + 2u * ST_SECTOR,
               "OBJ_UPFM6: past the app slot and its 8 KiB of room to grow, off the SDK's sectors");

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

/* zlib CRC-32, 4 bits per step, in pieces: c = st_crc_upd(0xFFFFFFFF, ..) .. then ~c (snap_store.c: a stream
 * spread over sectors) */
static uint32_t st_crc_upd(uint32_t c, const void *p, uint32_t n)
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
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc_upd(0xFFFFFFFFu, p, n); }

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_AUTOSAVE)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_UKIT)
        return ST_UKIT_SECTOR + copy * ST_SECTOR;
    if (obj == OBJ_DLANES)
        return ST_DLANES_SECTOR + copy * ST_SECTOR;
#if FELUCCA_UP_FM6 || CZ_NUSER
    if (obj == OBJ_UPFM6)
        return ST_UPF_SECTOR + copy * ST_SECTOR;   /* (FL_UPF: the two sectors before the main store) */
#endif
#if CZ_NUSER
    if (obj == OBJ_CZBANK)                      /* below the snapshot area (eng_sample.c SMP_USR3_END) */
        return 0xD8000u - (SN_SECTORS + 2u - copy) * ST_SECTOR;
#endif
    if (obj >= OBJ_UPRESET0 && obj < OBJ_AUTOSAVE)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));
#define ST_E_MAP (-8)                                  /* a sector outside the store's allow-list (st_save) */

#if FELUCCA_SL24_SAFE
/* Sectors Optimist never erases (backports24.h): another firmware's data it cannot read, found at boot. SLOOP 2.4
 * writes this same store (the same FELU objects 0..7, the same places) and more: its projects (FUN5, 3840 B) in the
 * slots and the autosave (kept: the current copy of each, sl24_guard.c), its FM6 bank as object 8 at 0xE5000 /
 * 0xE6000 (our drum records' sectors: any valid object of another type is kept, st_alien), a USR3 sample past our
 * USR3 (2.3 / 2.4: to 0xDBFFF, over our snapshots and banks) and USR4 at 0xE7000..0xFAFFF (UP_FM6's old place): the
 * sectors such a sample holds (st_keep_scan). A store that would erase one writes its other copy, or refuses. */
#define ST_E_KEPT (-12)
#define ST_KEEP_N 8u
static uint32_t st_keep_at[ST_KEEP_N], st_keep_n;
static uint32_t st_keep_lo[2], st_keep_hi[2];          /* a foreign user sample's sectors [lo, hi), 0: none */
static void st_keep(uint32_t off)
{
    uint32_t i;
    off &= ~(ST_SECTOR - 1u);
    for (i = 0; i < st_keep_n; i++)
        if (st_keep_at[i] == off)
            return;
    if (st_keep_n < ST_KEEP_N)
        st_keep_at[st_keep_n++] = off;
}
static int st_kept(uint32_t off)
{
    uint32_t i;
    off &= ~(ST_SECTOR - 1u);
    for (i = 0; i < st_keep_n; i++)
        if (st_keep_at[i] == off)
            return 1;
    for (i = 0; i < 2u; i++)
        if (off >= st_keep_lo[i] && off < st_keep_hi[i])
            return 1;
    return 0;
}
/* a sector holding another firmware's user sample: its header (FSMP, version 1, 1..16 zones, at most a slot of
 * 80 KiB) at hdr, its sectors from first on (the sample's own start, or where our USR3 ends) when they hold no
 * record of ours (a snapshot, an object, the FM6 bank: written since, the sample's tail is gone there) */
static void st_keep_sample(uint32_t k, uint32_t hdr, uint32_t first)
{
    struct { uint32_t magic; uint16_t version; uint8_t nz, rsv; char name[8]; uint32_t data_len; } h;
    uint32_t end, s, w[8], i, blank;
    st_keep_lo[k] = st_keep_hi[k] = 0;
    if (st_read(hdr, &h, sizeof h) || h.magic != 0x504D5346u || h.version != 1u || !h.nz || h.nz > 16u ||
        h.data_len > 0x14000u - 512u)
        return;
    end = (hdr + 512u + h.data_len + ST_SECTOR - 1u) & ~(ST_SECTOR - 1u);
    for (s = first; s < end; s += ST_SECTOR) {
        if (st_read(s, w, sizeof w))
            return;
        for (i = 0, blank = 1; i < 8u; i++)
            blank &= w[i] == 0xFFFFFFFFu;
        if (blank || w[0] == ST_MAGIC || w[0] == 0x31534E53u || w[0] == 0x42364D46u)   /* ("SNS1", "FM6B") */
            break;                                     /* (erased, or ours since: the sample ends before) */
    }
    if (s > first)
        st_keep_lo[k] = first, st_keep_hi[k] = s;
}
#endif

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
#if FELUCCA_ST_STRICT
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
#endif
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->len > ST_PAYLOAD_MAX ||
#if FELUCCA_ST_STRICT
        h->slot != copy ||                       /* the copy it was written to (SLOOP 2.3, after Felucca 1.0) */
#endif
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load object into dst (up to max bytes); returns the length, or -1 */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0)
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

#if FELUCCA_SL24_SAFE
/* copy c of obj may not be erased: a kept sector, or a valid object of another type there (SLOOP 2.4's FM6 bank,
 * object 8, at our drum records' 0xE5000 / 0xE6000) */
static int st_off_limits(uint32_t obj, uint32_t c)
{
    uint32_t off = st_sector(obj, c);
    st_hdr_t h;
    return st_kept(off) || (!st_read(off, &h, sizeof h) && h.magic == ST_MAGIC && h.type != obj &&
                            h.hcrc == st_crc32(&h, sizeof h - 4u));
}
#endif

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off, c;
    int cur, rc;
    st_hdr_t h;
#if FELUCCA_ST_STRICT
    if (obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX)
#else
    if (len > ST_PAYLOAD_MAX)
#endif
        return -1;
#if FELUCCA_UP_FM6 || CZ_NUSER
    if (src == st_buf) {                              /* (built in st_buf: st_current would read over it) the newest */
        st_hdr_t b;                                   /* valid header is the current copy */
        int va = st_head(obj, 0, &h) == 0, vb = st_head(obj, 1, &b) == 0;
        cur = vb && (!va || b.seq > h.seq) ? 1 : va ? 0 : -1;
        if (cur == 1)
            h = b;
    } else
#endif
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    c = cur == 0 ? 1u : 0u;                           /* write the other copy */
#if FELUCCA_SL24_SAFE
    if (st_off_limits(obj, c)) {                      /* (another firmware's there: our current copy, written over in */
        c ^= 1u;                                      /* place, a cut loses it; none: the other empty copy) */
        if (st_off_limits(obj, c))
            return ST_E_KEPT;
    }
#endif
    base = st_sector(obj, c);
    if (!FL_STORE_OK(base, ST_SECTOR))                /* the flash map (hal/fm1_flash_map.h): never the SDK's sectors */
        return ST_E_MAP;
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)c;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
#if FELUCCA_ST_STRICT
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))   /* the whole record */
#else
        if (st_head(obj, c, &chk) || chk.seq != h.seq || st_body(obj, c, &chk))
#endif
            return -7;
    }
    return 0;
}

#if FELUCCA_UP_FM6
/* OBJ_UPFM6 moved (fix/upfm6-off-vm): until then its copies were 0xE7000 (A) and 0xE8000 (B), and 0xE8000 is the
 * stock firmware's SDK VM (its settings and the radio calibration), which a save there erased. At start, when the
 * new place holds no valid object, the newest valid one of the old copies is copied there. Only an object of ours
 * is taken (FELU, type OBJ_UPFM6, the copy it was written to, both CRCs, the payload's own magic pmagic); the SDK
 * VM (55 AA AA 55) or anything else is not. The old sectors are only read: never erased or written again (0xE8000:
 * an object of ours there already took the VM's place, erasing it restores nothing; stock rebuilds its VM).
 * 1: copied, 0: nothing to copy (or already moved), < 0: the save failed (the old copies stay, tried next start). */
static __attribute__((unused)) int st_upf_move(uint32_t pmagic)
{
    st_hdr_t h, best;
    uint32_t c, w, found = 0;
    if (st_current(OBJ_UPFM6, &h) >= 0)
        return 0;
    for (c = 0; c < 2u; c++) {
        uint32_t at = ST_UPF_OLD + c * ST_SECTOR;
        if (st_read(at, &h, sizeof h) || h.magic != ST_MAGIC || h.type != OBJ_UPFM6 || h.slot != c ||
            h.len < 4u || h.len > ST_PAYLOAD_MAX || h.hcrc != st_crc32(&h, sizeof h - 4u))
            continue;
        if (st_read(at + ST_PAYLOAD_OFF, st_buf, h.len) || st_crc32(st_buf, h.len) != h.crc)
            continue;
        w = st_buf[0] | (uint32_t)st_buf[1] << 8 | (uint32_t)st_buf[2] << 16 | (uint32_t)st_buf[3] << 24;
        if (w != pmagic || (found && h.seq <= best.seq))
            continue;
        best = h;
        found = 1u + c;
    }
    if (!found)
        return 0;
    if (st_read(ST_UPF_OLD + (found - 1u) * ST_SECTOR + ST_PAYLOAD_OFF, st_buf, best.len))
        return -1;
    return st_save(OBJ_UPFM6, st_buf, best.len) == 0 ? 1 : -1;
}
#endif
