/* SPDX-License-Identifier: GPL-3.0-only */
/* LZB: what the boot copies into RAM (.data's initial values, the pool's tables, the RAM code) is one stream in flash,
 * coded by tools/lzb.py: LZ4's block shape (a 3-byte minimum match, 1-byte offsets up to 256 or 2-byte ones) whose
 * bytes are not stored but coded by an adaptive binary range coder (LZMA's: 11-bit probabilities moved by a 32nd a
 * bit, 5 bytes read first), each byte as 8 bits down a tree of 255 probabilities, a tree per byte class: the tokens
 * (bits 7..4 the literal count, 15 = more in length bytes, each added, 255 = another; bit 3 = a 2-byte offset; bits
 * 2..0 the match length - 3, 7 = more, the same way), the length bytes, the 1-byte offsets (distance - 1), the low and
 * the high byte of a 2-byte offset, and the literals in a context of the previous byte's top LZB_LC bits and the
 * block position's low LZB_PB bits. 55.8 KB of RAM code and tables take 38.9 KB of flash (the parse's bytes alone
 * 44.9 KB, 2026-10-23). One lzb_t (the stream cursor, the coder, the model: 4.6 KB on the boot stack) for the whole
 * stream, a block for each section in the order main.c fm1_cstart decodes them; a block starts with prev 0 at
 * position 0. The match is copied byte by byte from distance back (it may overlap). The block's last sequence is
 * literals only: the loop ends when d reaches e, which is where the next block starts. Never past e whatever the
 * stream says. Runs from flash before any RAM code or global exists: no global (its state is the caller's), calls
 * only what is here (tests/lzb_test.c runs it on tools/lzb.py's vectors). */
#define LZB_LC 1
#define LZB_PB 1
#define LZB_TOK 0
#define LZB_LEN 256
#define LZB_OFF1 512
#define LZB_OFF2LO 768
#define LZB_OFF2HI 1024
#define LZB_LIT 1280
#define LZB_NPROBS (LZB_LIT + (256 << (LZB_LC + LZB_PB)))

typedef struct {
    const uint8_t *s;                           /* the stream: the next byte the coder takes */
    uint32_t range, code;
    uint16_t p[LZB_NPROBS];                     /* the model: 1024 = a half */
} lzb_t;

static void lzb_init(lzb_t *z, const uint8_t *s)
{
    uint32_t i;
    for (i = 0; i < LZB_NPROBS; i++)
        z->p[i] = 1024;
    z->range = 0xFFFFFFFFu;
    z->code = 0;
    for (i = 0; i < 5u; i++)
        z->code = (z->code << 8) | *s++;
    z->s = s;
}

/* one byte of the tree at probs (8 bits, the top first) */
static uint32_t lzb_byte(lzb_t *z, uint16_t *probs)
{
    uint32_t m = 1;
    do {
        uint16_t *p = probs + m;
        uint32_t bound = (z->range >> 11) * *p;
        if (z->code < bound) {
            z->range = bound;
            *p = (uint16_t)(*p + ((2048u - *p) >> 5));
            m <<= 1;
        } else {
            z->code -= bound;
            z->range -= bound;
            *p = (uint16_t)(*p - (*p >> 5));
            m = (m << 1) | 1u;
        }
        while (z->range < (1u << 24)) {
            z->range <<= 8;
            z->code = (z->code << 8) | *z->s++;
        }
    } while (m < 256u);
    return m & 255u;
}

/* a length past the token's field: bytes added while one is 255 */
static uint32_t lzb_more(lzb_t *z, uint32_t n)
{
    uint32_t b;
    do {
        b = lzb_byte(z, z->p + LZB_LEN);
        n += b;
    } while (b == 255u);
    return n;
}

/* one block: d..e from the stream at z */
static void lzb_expand(lzb_t *z, uint8_t *d, uint8_t *e)
{
    const uint8_t *d0 = d;
    uint32_t prev = 0;
    while (d < e) {
        uint32_t t = lzb_byte(z, z->p + LZB_TOK), n = t >> 4, b;
        if (n == 15u)
            n = lzb_more(z, n);
        for (; n && d < e; n--, d++) {
            uint32_t ctx = ((prev >> (8 - LZB_LC)) << LZB_PB) | ((uint32_t)(d - d0) & ((1u << LZB_PB) - 1u));
            *d = (uint8_t)(prev = lzb_byte(z, z->p + LZB_LIT + 256u * ctx));
        }
        if (d >= e)
            break;                                  /* (the block's last literals) */
        if (t & 8u) {
            b = lzb_byte(z, z->p + LZB_OFF2LO);
            b |= lzb_byte(z, z->p + LZB_OFF2HI) << 8;
        } else {
            b = lzb_byte(z, z->p + LZB_OFF1) + 1u;
        }
        n = (t & 7u) + 3u;
        if ((t & 7u) == 7u)
            n = lzb_more(z, n);
        for (; n && d < e; n--, d++)
            *d = d[-(int32_t)b];
        prev = d[-1];
    }
}
