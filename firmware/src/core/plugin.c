/* SPDX-License-Identifier: GPL-3.0-only */
/* The PLUGINS region of the flash (core/plugin.h): what is there at boot, and the loader that writes a plugin file
 * into it. Included after the storage (st_crc_upd, fl_erase4k_quiet, fl_write, fl_inval) and the engines (ENGINES[],
 * the slots after the built-in engines, registry.h plug_uid). The reader a caller passes to plug_install is the
 * file's bytes wherever they are: the card (sd_mode.c), the USB console, the editor.
 *
 * A placed plugin is its header in the first bytes of a sector, the image right after, over as many whole sectors as
 * it takes. plug_scan walks the region sector by sector: a sector whose header reads whole (plug_whole: the CRCs, the
 * base where it sits) starts a plugin and the walk skips its sectors; every other sector is free. So there is no
 * directory to keep, and a write cut short (the header is written last) leaves free sectors.
 *
 * The loader works in windows of one sector with 8 bytes of margin either side (plug_win), so a relocation whose
 * bytes straddle two sectors is patched whole in both windows, from the file's bytes both times. The header's area
 * of the first sector is left erased until the end. */
#if FELUCCA_PLUGINS
#ifndef PLUG_XIP
#define PLUG_XIP(off) ((const uint8_t *)FL_XIP(off))   /* the placed bytes as the CPU reads them */
#endif
#ifndef PLUG_NONE
#define PLUG_NONE (&ENG_PLUG_NONE)        /* an empty slot's engine (engines.c) */
#endif
#ifndef PLUG_SET_SLOT
#define PLUG_SET_SLOT(k, p) (ENGINES[NBUILT + (k)] = (p))
#endif
#ifndef PLUG_RAM
#define PLUG_RAM IN_POOL
#endif
#ifndef PLUG_ENCRYPT
#define PLUG_ENCRYPT 1                    /* the region is inside the app area: the SFC decrypts every read of it */
#endif
/* the API table (core/plugin.h plug_api_t): built once, const, in the image; its XIP address is what the loader
 * writes into a plugin's plugin_api word (PLUG_REL_API). A host test names its own (PLUG_API_ADDR) */
#ifndef PLUG_API_ADDR
static void *plug_part_mem(uint32_t part) { return PART_MEM_AT(part % NPART); }
static uint32_t plug_rng(void) { return rng(); }
static const plug_api_t plug_api_tab __attribute__((used)) = {   /* (used: kept whether or not this build loads
                                                                   * plugins itself; a preloaded package's plugins
                                                                   * point at it, tools/build.py OPTIMIST_PLUGIN_PRELOAD) */
    .version = PLUG_API,
    .part_mem = plug_part_mem,
    .tab = {[PLUG_TAB_SINE] = SINE, [PLUG_TAB_TANH] = TANH_Q15},
    .trk = trk,
    .rng = plug_rng,
};
#define PLUG_API_ADDR ((uint32_t)(uintptr_t)&plug_api_tab)
#endif
/* a placed plugin's descriptor the firmware can run: its part memory fits what a part can have (engines.c: the arena
 * places any size by need, a change that does not fit is refused there; the fixed memories are the largest built
 * engine's) */
#ifndef PLUG_PART_MAX
#if PART_DROPS
#define PLUG_PART_MAX 0xFFFFu
#elif PART_MEM_ANY
#define PLUG_PART_MAX sizeof(part_mem_t)
#else
#define PLUG_PART_MAX 0u
#endif
#endif
#ifndef PLUG_DESC_OK
#define PLUG_DESC_OK(d) ((d)->part_bytes <= PLUG_PART_MAX)
#endif
#define PLUG_MARGIN 8u
#define PLUG_RELCHUNK 64u                 /* relocation words read at a time */
#define PLUG_SFC_KEY 0x980Fu              /* the chip key our packages set (tools/fm1pkg_make.py KEY) */
#define PLUG_SFC_BASE 0x4000u             /* the app area's start: the cipher's block addresses count from it */

typedef int (*plug_rd_t)(void *ctx, uint32_t off, void *dst, uint32_t n);   /* the file's bytes [off, off + n): 0 ok */

static uint32_t plug_off_[FELUCCA_PLUGIN_SLOTS];        /* each slot's header's flash offset, 0 = empty */
static uint8_t plug_win[PLUG_SECTOR + 2u * PLUG_MARGIN] PLUG_RAM;
static uint32_t plug_rel[PLUG_RELCHUNK] PLUG_RAM;

static uint32_t plug_crc(const void *p, uint32_t n) { return ~st_crc_upd(0xFFFFFFFFu, p, n); }
static uint32_t plug_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void plug_st32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t plug_sectors(uint32_t len) { return (PLUG_HDR_LEN + len + PLUG_SECTOR - 1u) / PLUG_SECTOR; }

/* The app area is stored SFC-encrypted (tools/fm1pkg_make.py sfc: JieLi's ENC stream per 32-byte block, the key
 * mixed with the block's address) and the SFC decrypts it on every XIP read. So what the loader programs inside it
 * is encrypted the same way first: n bytes going to flash offset off, in place (an XOR: the same call decrypts) */
static void plug_sfc(uint8_t *b, uint32_t off, uint32_t n)
{
#if PLUG_ENCRYPT
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t a = off + i - PLUG_SFC_BASE, key = PLUG_SFC_KEY ^ (a & ~31u) >> 2, j;
        for (j = a & 31u; j; j--)
            key = (key << 1 ^ (key & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        b[i] ^= (uint8_t)key;
    }
#else
    (void)b; (void)off; (void)n;
#endif
}
static uint64_t plug_mask(uint32_t n) { return n >= 64u ? ~(uint64_t)0 : ((uint64_t)1 << n) - 1u; }

/* a header's own fields: the file's or a placed one's */
static int plug_hdr_ok(const plug_hdr_t *h)
{
    return h->magic == PLUG_MAGIC && plug_crc(h, PLUG_HDR_LEN - 4u) == h->hcrc && h->api == PLUG_API &&
           h->abi == PLUG_ABI && h->kind == PLUG_KIND_ENGINE && h->len >= 4u && (h->len & 3u) == 0u &&
           h->len <= PLUG_NSECT * PLUG_SECTOR - PLUG_HDR_LEN && h->desc <= h->len - 4u && h->nrel <= PLUG_NREL_MAX;
}

/* the plugin placed at flash offset off, whole: its header (nrel 0, base where it sits), its image's CRC */
static int plug_whole(uint32_t off, plug_hdr_t *h)
{
    memcpy(h, PLUG_XIP(off), PLUG_HDR_LEN);
    return plug_hdr_ok(h) && h->nrel == 0u && h->base == FM1_XIP(off) + PLUG_HDR_LEN &&
           (off - FL_PLUG_LO) / PLUG_SECTOR + plug_sectors(h->len) <= PLUG_NSECT &&
           plug_crc(PLUG_XIP(off) + PLUG_HDR_LEN, h->len) == h->crc;
}

static void plug_slot_clear(uint32_t k)
{
    PLUG_SET_SLOT(k, PLUG_NONE);
    plug_uid[k] = 0xFF;
    plug_off_[k] = 0;
}

/* the used sectors (a bit each, from FL_PLUG_LO); reg: the slots set from what is there. A UID already in a slot
 * keeps it (a part playing slot k goes on playing the same engine after an install or a forget), a new one takes
 * the first free slot, a slot whose plugin is gone is cleared. A UID placed twice: the first one plays, the other
 * only holds its sectors */
static uint64_t plug_scan(int reg)
{
    uint64_t used = 0;
    uint32_t off, k;
    uint8_t seen[FELUCCA_PLUGIN_SLOTS] = {0};
    for (off = FL_PLUG_LO; off < FL_PLUG_HI; off += PLUG_SECTOR) {
        plug_hdr_t h;
        uint32_t n;
        if (!plug_whole(off, &h))
            continue;
        n = plug_sectors(h.len);
        used |= plug_mask(n) << ((off - FL_PLUG_LO) / PLUG_SECTOR);
        if (reg && PLUG_DESC_OK((const engine_t *)(PLUG_XIP(off) + PLUG_HDR_LEN + h.desc))) {
            k = plug_slot_of(h.uid);
            if (k == 0xFFu)
                for (k = 0; k < FELUCCA_PLUGIN_SLOTS && plug_uid[k] != 0xFFu; k++)
                    ;
            if (k < FELUCCA_PLUGIN_SLOTS && !seen[k]) {
                PLUG_SET_SLOT(k, (const engine_t *)(PLUG_XIP(off) + PLUG_HDR_LEN + h.desc));
                plug_uid[k] = h.uid;
                plug_off_[k] = off;
                seen[k] = 1;
            }
        }
        off += (n - 1u) * PLUG_SECTOR;
    }
    if (reg)
        for (k = 0; k < FELUCCA_PLUGIN_SLOTS; k++)
            if (!seen[k])
                plug_slot_clear(k);
    return used;
}
static void plug_init(void)                           /* boot, once the flash reads (main.c): the slots in region order */
{
    uint32_t k;
    for (k = 0; k < FELUCCA_PLUGIN_SLOTS; k++)
        plug_slot_clear(k);
    (void)plug_scan(1);
}

/* a relocation at image offset o patched in the window buffer (b: the image offset of the buffer's first byte) */
static void plug_patch(uint32_t w, uint32_t o, uint32_t b, uint32_t base)
{
    uint8_t *p = plug_win + (o - b);
    if (PLUG_REL_KIND(w) == PLUG_REL_ABS32)
        plug_st32(p, plug_le32(p) + base);
    else if (PLUG_REL_KIND(w) == PLUG_REL_API)
        plug_st32(p, PLUG_API_ADDR);
    else {
        uint32_t w0 = plug_le32(p), w1 = plug_le32(p + 4), v = (w0 >> 16) | (w1 & 0xFFFFu) << 16;
        v += base;
        plug_st32(p, (w0 & 0xFFFFu) | v << 16);
        plug_st32(p + 4, (w1 & 0xFFFF0000u) | v >> 16);
    }
}

/* window w of a plugin of len bytes read at flash offset off: the buffer filled from the file (0xFF past the image),
 * every relocation that touches the window patched, the window's image bytes programmed, their CRC continued */
static int plug_window(plug_rd_t rd, void *ctx, const plug_hdr_t *h, uint32_t w, uint32_t off, uint32_t base, uint32_t *crc)
{
    uint32_t pw = w * PLUG_SECTOR;                                    /* the window: placed [pw, pw + SECTOR) */
    int32_t b = (int32_t)pw - (int32_t)PLUG_MARGIN - (int32_t)PLUG_HDR_LEN;   /* image offset of the buffer's start */
    uint32_t lo = b < 0 ? 0u : (uint32_t)b, hi = (uint32_t)(b + (int32_t)sizeof plug_win), r, start, end;
    if (hi > h->len)
        hi = h->len;
    memset(plug_win, 0xFF, sizeof plug_win);
    if (lo < hi && rd(ctx, PLUG_HDR_LEN + lo, plug_win + (lo - (uint32_t)b), hi - lo))
        return PLUG_E_READ;
    for (r = 0; r < h->nrel; r += PLUG_RELCHUNK) {
        uint32_t i, n = h->nrel - r < PLUG_RELCHUNK ? h->nrel - r : PLUG_RELCHUNK;
        if (rd(ctx, PLUG_HDR_LEN + h->len + r * 4u, plug_rel, n * 4u))
            return PLUG_E_READ;
        for (i = 0; i < n; i++) {
            uint32_t o = PLUG_REL_OFF(plug_rel[i]), span = PLUG_REL_SPAN(PLUG_REL_KIND(plug_rel[i]));
            uint32_t wlo = pw < PLUG_HDR_LEN ? 0u : pw - PLUG_HDR_LEN, whi = pw + PLUG_SECTOR - PLUG_HDR_LEN;
            if (PLUG_REL_KIND(plug_rel[i]) > PLUG_REL_KMAX || o + span > h->len)
                return PLUG_E_BAD;
            if (o < whi && o + span > wlo)                                /* touches the window's image bytes */
                plug_patch(plug_rel[i], o, (uint32_t)b, base);
        }
    }
    start = pw < PLUG_HDR_LEN ? PLUG_HDR_LEN : pw;                        /* placed bytes to program */
    end = pw + PLUG_SECTOR < PLUG_HDR_LEN + h->len ? pw + PLUG_SECTOR : PLUG_HDR_LEN + h->len;
    if (start >= end)
        return 0;
    *crc = st_crc_upd(*crc, plug_win + PLUG_MARGIN + (start - pw), end - start);
    plug_sfc(plug_win + PLUG_MARGIN + (start - pw), off + start, end - start);
    return fl_write(off + start, plug_win + PLUG_MARGIN + (start - pw), end - start) ? PLUG_E_FLASH : 0;
}

/* a plugin file written into the region: its slot, or PLUG_E_* (the file's bytes through rd). An engine already
 * placed (its UID in a slot): that slot, nothing written. The sound is stopped by the caller (a project load) */
static int plug_install(plug_rd_t rd, void *ctx)
{
    plug_hdr_t h;
    uint64_t used;
    uint32_t n, s, off, base, w, took, crc = 0xFFFFFFFFu, k;
    int rc;
    if (rd(ctx, 0, &h, PLUG_HDR_LEN))
        return PLUG_E_READ;
    if (!plug_hdr_ok(&h) || h.base != 0u)
        return PLUG_E_BAD;
    if (plug_slot_of(h.uid) != 0xFFu)
        return (int)plug_slot_of(h.uid);
    for (k = 0; k < FELUCCA_PLUGIN_SLOTS && plug_uid[k] != 0xFFu; k++)
        ;
    if (k == FELUCCA_PLUGIN_SLOTS)
        return PLUG_E_SLOTS;
    used = plug_scan(0);
    n = plug_sectors(h.len);
    for (s = 0; s + n <= PLUG_NSECT; s++)
        if (!((used >> s) & plug_mask(n)))
            break;
    if (s + n > PLUG_NSECT)
        return PLUG_E_FULL;
    off = FL_PLUG_LO + s * PLUG_SECTOR;
    base = FM1_XIP(off) + PLUG_HDR_LEN;
    for (w = 0; w < n; w++)
        if (fl_erase4k_quiet(off + w * PLUG_SECTOR, &took))
            return PLUG_E_FLASH;
    for (w = 0; w < n; w++)
        if ((rc = plug_window(rd, ctx, &h, w, off, base, &crc)) != 0) {
            fl_inval(off, n * PLUG_SECTOR);
            return rc;
        }
    h.nrel = 0;
    h.crc = ~crc;
    h.base = base;
    h.hcrc = plug_crc(&h, PLUG_HDR_LEN - 4u);
    memcpy(plug_win, &h, PLUG_HDR_LEN);
    plug_sfc(plug_win, off, PLUG_HDR_LEN);
    if (fl_write(off, plug_win, PLUG_HDR_LEN))
        return PLUG_E_FLASH;
    fl_inval(off, n * PLUG_SECTOR);
    (void)plug_scan(1);
    if (plug_slot_of(h.uid) == 0xFFu && !PLUG_DESC_OK((const engine_t *)(PLUG_XIP(off) + PLUG_HDR_LEN + h.desc))) {
        if (fl_erase4k_quiet(off, &took))                 /* (its part memory larger than a part's: not kept) */
            return PLUG_E_FLASH;
        fl_inval(off, PLUG_SECTOR);
        return PLUG_E_MEM;
    }
    return plug_slot_of(h.uid) == 0xFFu ? PLUG_E_FLASH : (int)plug_slot_of(h.uid);
}

/* slot k's plugin removed: its header's sector erased (the rest of its sectors read as free from then on) */
static int plug_forget(uint32_t k)
{
    uint32_t took, off = k < FELUCCA_PLUGIN_SLOTS ? plug_off_[k] : 0u;
    if (!off)
        return -1;
    if (fl_erase4k_quiet(off, &took))
        return PLUG_E_FLASH;
    fl_inval(off, PLUG_SECTOR);
    (void)plug_scan(1);
    return 0;
}
#endif
