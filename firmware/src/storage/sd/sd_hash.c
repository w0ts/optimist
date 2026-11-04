/* SPDX-License-Identifier: GPL-3.0-only */
/* A sample's identity (docs/SD-CARD-DESIGN.md 8: "samples identified by a hash of the file", Elektron's way): XXH64
 * (Yann Collet's xxHash, 64-bit, seed 0; written here from its specification, xxhash.com doc/xxhash_spec.md). Why it:
 * 64 bits (a card's few thousand samples never collide by chance: ~1e-12 for 6,000), a published specification and
 * test vectors (tests/sd_layout_test.c checks them), a small state (88 B) and code; it is not a security hash and needs
 * not be. Its speed does not matter here: the card reads ~1-2 MB/s, the hash on a 32-bit core is many times that
 * (four 64-bit multiplies per 32 bytes). Full FatFs builds (FELUCCA_SD_FATFS).
 *   sdh_init(&s) / sdh_add(&s, p, n) / sdh_end(&s)   XXH64 by pieces (any split gives the same hash)
 *   sdh_file(fp, buf, n, &len)   the hash of what identifies a file's sound: a WAV's "data" chunk (so a header edit,
 *                                loop points, slice markers or a rename keep it), any other file whole; from the
 *                                file's start, through buf (n bytes, 512 or more: a sector at a time); len: the bytes
 *                                hashed. FR_OK or FatFs' error */
#define SDH_P1 0x9E3779B185EBCA87ull
#define SDH_P2 0xC2B2AE3D27D4EB4Full
#define SDH_P3 0x165667B19E3779F9ull
#define SDH_P4 0x85EBCA77C2B2AE63ull
#define SDH_P5 0x27D4EB2F165667C5ull

typedef struct {
    uint64_t v[4];
    uint64_t total;
    uint8_t mem[32];
    uint32_t used;
} sdh_t;

static uint64_t sdh_rotl(uint64_t x, uint32_t r) { return x << r | x >> (64u - r); }
static uint64_t sdh_rd64(const uint8_t *p)          /* little-endian, any alignment */
{
    return (uint64_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) |
           (uint64_t)(p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24) << 32;
}
static uint64_t sdh_round(uint64_t acc, uint64_t in) { return sdh_rotl(acc + in * SDH_P2, 31) * SDH_P1; }
static uint64_t sdh_merge(uint64_t h, uint64_t v) { return (h ^ sdh_round(0, v)) * SDH_P1 + SDH_P4; }

static void sdh_init(sdh_t *s)
{
    s->v[0] = SDH_P1 + SDH_P2;
    s->v[1] = SDH_P2;
    s->v[2] = 0;
    s->v[3] = 0 - SDH_P1;
    s->total = 0;
    s->used = 0;
}

static void sdh_stripe(sdh_t *s, const uint8_t *p)
{
    s->v[0] = sdh_round(s->v[0], sdh_rd64(p));
    s->v[1] = sdh_round(s->v[1], sdh_rd64(p + 8));
    s->v[2] = sdh_round(s->v[2], sdh_rd64(p + 16));
    s->v[3] = sdh_round(s->v[3], sdh_rd64(p + 24));
}

static void sdh_add(sdh_t *s, const void *data, uint32_t n)
{
    const uint8_t *p = data;
    s->total += n;
    if (s->used) {                                   /* the stripe begun by the last piece */
        while (n && s->used < 32u) {
            s->mem[s->used++] = *p++;
            n--;
        }
        if (s->used < 32u)
            return;
        sdh_stripe(s, s->mem);
        s->used = 0;
    }
    for (; n >= 32u; n -= 32u, p += 32)
        sdh_stripe(s, p);
    while (n--)
        s->mem[s->used++] = *p++;
}

static uint64_t sdh_end(const sdh_t *s)
{
    uint64_t h;
    const uint8_t *p = s->mem;
    uint32_t n = s->used;
    if (s->total >= 32u) {
        h = sdh_rotl(s->v[0], 1) + sdh_rotl(s->v[1], 7) + sdh_rotl(s->v[2], 12) + sdh_rotl(s->v[3], 18);
        h = sdh_merge(sdh_merge(sdh_merge(sdh_merge(h, s->v[0]), s->v[1]), s->v[2]), s->v[3]);
    } else {
        h = SDH_P5;                                  /* (the seed, 0, + P5) */
    }
    h += s->total;
    for (; n >= 8u; n -= 8u, p += 8)
        h = sdh_rotl(h ^ sdh_round(0, sdh_rd64(p)), 27) * SDH_P1 + SDH_P4;
    if (n >= 4u) {
        h = sdh_rotl(h ^ (uint64_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) * SDH_P1, 23) * SDH_P2 + SDH_P3;
        n -= 4u;
        p += 4;
    }
    while (n--)
        h = sdh_rotl(h ^ *p++ * SDH_P5, 11) * SDH_P1;
    h ^= h >> 33;
    h *= SDH_P2;
    h ^= h >> 29;
    h *= SDH_P3;
    return h ^ h >> 32;
}

static uint32_t sdh_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* a WAV's "data" chunk: its offset and length (clamped to the file), 0 when the file is no RIFF WAVE or has none */
static FRESULT sdh_wav_data(FIL *fp, uint8_t *buf, uint32_t *ofs, uint32_t *len)
{
    UINT got;
    uint32_t at = 12, size = (uint32_t)f_size(fp), n;
    FRESULT r;
    *ofs = *len = 0;
    if ((r = f_lseek(fp, 0)) != FR_OK || (r = f_read(fp, buf, 12, &got)) != FR_OK || got < 12 ||
        memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
        return r;
    while (at + 8u <= size) {                        /* the chunks: an id, a length, the data padded to even */
        if ((r = f_lseek(fp, at)) != FR_OK || (r = f_read(fp, buf, 8, &got)) != FR_OK || got < 8)
            return r;
        n = sdh_le32(buf + 4);
        if (!memcmp(buf, "data", 4)) {
            *ofs = at + 8u;
            *len = n < size - *ofs ? n : size - *ofs;
            return FR_OK;
        }
        if (n > size - at - 8u)
            break;
        at += 8u + n + (n & 1u);
    }
    return FR_OK;
}

static FRESULT sdh_file(FIL *fp, uint8_t *buf, uint32_t bufn, uint64_t *hash, uint32_t *len)
{
    sdh_t s;
    UINT got;
    uint32_t ofs, left;
    FRESULT r = sdh_wav_data(fp, buf, &ofs, &left);
    if (r != FR_OK)
        return r;
    if (!ofs)
        left = (uint32_t)f_size(fp);                 /* (no WAV: the whole file) */
    *len = left;
    sdh_init(&s);
    if ((r = f_lseek(fp, ofs)) != FR_OK)
        return r;
    while (left && (r = f_read(fp, buf, left < bufn ? left : bufn, &got)) == FR_OK && got) {
        sdh_add(&s, buf, got);
        left -= got;
    }
    *hash = sdh_end(&s);
    return r != FR_OK ? r : left ? FR_INT_ERR : FR_OK;
}
