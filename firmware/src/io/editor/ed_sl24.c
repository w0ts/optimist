/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: SLOOP 2.4's files (included by editor.c; web/EDITOR_PROTOCOL.md "SLOOP 2.4 export" and "SLOOP 2.4
 * import"). FELUCCA_SL24_EXPORT: the editor reads parts and writes them into a SLOOP 2.4 backup file ("sloop-backup",
 * its objects 0, 1 and, with the FM6 voices, 8), which 2.4's editor restores: the project becomes 2.4's working
 * project, the settings its settings, the bank its FM6 patch bank.
 *   78 SL24_GET  part, offset (3 x 7 bit) -> part, rc (0 ok, 1 arguments, 2 busy: a restore or an import holds the
 *                buffer), length (3 x 7 bit), CRC-32 of the part (5 x 7 bit), lost (2 x 7 bit: sl24_export.c SX24_*),
 *                offset, pack7 bytes (<= 256; none at its end)
 *     part 0  the working project as 2.4's FUN5 (3840 B, sl24_export.c proj_to_sl24), its step extras too; an FM6
 *             voice that is not one of 2.4's F1..F8: the closest of them (lost SX24_FM6)
 *     part 1  the settings as 2.4's persist_t (88 B: palette, low cut, zoom, the panel table, 2.4's song order, the
 *             lights word: sl24_persist)
 *     part 2  as 0, but such a voice goes into 2.4's FM6 bank (B1..B27; lost SX24_BANK when one did)
 *     part 3  that bank (3472 B, 2.4's object 8 as 2.4 stores it): 2.4's own bank kept in flash with the voices
 *             added to it (the slot already holding a voice, else a free one), else a bank of only those
 * Each read makes the part again from the state now (no buffer kept, nothing written): the editor checks that every
 * reply carries the same CRC and that it matches what it put together, else reads again (it changed meanwhile).
 * A build without the export does not answer: the editor shows no button.
 * FELUCCA_SL24_EDIMPORT: a SLOOP 2.4 project from a 2.4 backup file into the working project (as PROJECT > LOAD twice
 * does with one kept in flash: sl24_guard.c sl24_import_buf; nothing written to flash, SAVE keeps it), and 2.4's FM6
 * bank kept in flash read out (the editor puts its patches in the user bank's free slots).
 *   90 SL24_PUT  0 (begin), length (3 x 7), CRC-32 (5 x 7) -> 0, rc
 *                1 (data), offset (3 x 7), CRC-32 of the chunk (5 x 7), pack7 bytes (<= 256, in order) -> 1, offset, rc
 *                2 (commit) -> 2, rc (0 imported, the working project now)
 *                the object: 2.4's FUN5 (3840 B), then optionally a byte of FM6 parts (bit k: part k's patch follows)
 *                and NPART x 128 B, each FM6 part's bank patch (PTCH B1..B27) from the file's object 8 (none: 2.4's
 *                INIT, as 2.4 plays an empty slot)
 *   91 SL24_BANK offset (3 x 7) -> rc (0, 4 none kept), length (3 x 7: 3472), CRC-32 of it all (5 x 7), offset, pack7
 *                bytes (<= 256)
 * rc: 0 ok, 1 arguments / no import begun, 2 CRC, 3 the transport plays, 4 none, 6 too long, 8 not a SLOOP 2.4 project,
 * 9 busy (a restore holds the buffer). A begun import holds proj_tmp as a restore does (10 s without a command: let go). */
enum { ED_SL24_GET = 78, ED_SL24_PUT = 90, ED_SL24_BANK = 91 };

static void ed_b7(uint32_t v, uint32_t n)                /* n 7-bit groups of v, low first */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ed_b(v >> (7u * i));
}
static uint32_t ed_g7(const uint8_t *a, uint32_t n)      /* n 7-bit groups, low first */
{
    uint32_t i, v = 0;
    for (i = 0; i < n; i++)
        v |= (uint32_t)(a[i] & 127u) << (7u * i);
    return v;
}

#if FELUCCA_SL24_EXPORT
_Static_assert(sizeof proj_tmp >= SL24_SIZE && sizeof(panel_t) == 32u, "the export is made in proj_tmp; 2.4's PAN5");
/* 2.4's bank as the export starts it, at b: its own kept in flash (sl24_guard.c), else an empty one */
static void ed_sl24_bank(uint8_t *b)
{
#if FELUCCA_SL24_SAFE
    uint32_t at = sl24_bank_at();
    if (at && !st_read(at, b, SL24_BANK_LEN))
        return;
#endif
    sl24_bank_empty(b);
}
static uint32_t ed_sl24_make(uint32_t part, uint32_t *lost)   /* -> its length, made in proj_tmp */
{
    uint8_t *o = (uint8_t *)&proj_tmp;
    *lost = 0;
    if (part != 1u) {
        const stepx_t *x[NTRK] = {0};
#if FELUCCA_SL24_XSTEP
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            x[k] = STEPX(k);
#endif
        proj_capture(&autosave_buf, &autosave_dl);        /* (autosave_buf: the work, as the snapshots take it) */
        if (part == 0u) {
            *lost = proj_to_sl24(&autosave_buf, x, o, 0);
            return SL24_SIZE;
        }
        ed_sl24_bank(part == 2u ? st_buf : o);            /* (st_buf: the half not sent, made again for each read) */
        *lost = proj_to_sl24(&autosave_buf, x, part == 2u ? o : st_buf, part == 2u ? st_buf : o);
        return part == 2u ? SL24_SIZE : SL24_BANK_LEN;
    }
#if BP23_SET
    sl24_persist(o, settings.palette, settings.lowcut, settings.zoom, &panel, sl24_word(bp23_word(), song.g[G_SYNC]));
#else
    sl24_persist(o, settings.palette, settings.lowcut, settings.zoom, &panel, sl24_word(0x100u, song.g[G_SYNC]));
#endif
    return SL24_PERSIST;
}

static void ed_sl24_get(const uint8_t *a, uint32_t na)
{
    uint32_t part = na ? a[0] : 127u, off = na >= 4u ? ed_g7(a + 1, 3) : 0u, len = 0, lost = 0;
    uint32_t rc = na != 4u || part > 3u, n, crc = 0;
    if (!rc && proj_tmp_busy())
        rc = 2;
    if (!rc) {
        len = ed_sl24_make(part, &lost);
        crc = st_crc32(&proj_tmp, len);
        rc = off > len;
    }
    n = rc ? 0u : len - off < 256u ? len - off : 256u;
    ed_b(part);
    ed_b(rc);
    ed_b7(len, 3);
    ed_b7(crc, 5);
    ed_b7(lost, 2);
    ed_b7(off, 3);
    if (n)
        ed_pack7((const uint8_t *)&proj_tmp + off, n);
}
#endif

#if FELUCCA_SL24_EDIMPORT
#define SL24_PUT_MAX (SL24_SIZE + 1u + NPART * 128u)      /* the FUN5, the parts' byte, their patches */
static struct {
    uint8_t on;                                           /* an import begun (holds proj_tmp: proj_tmp_lent) */
    uint32_t len, crc, got;
} s24in;
static uint8_t s24in_fv[1u + NPART * 128u] __attribute__((section(".pool")));   /* the object past the FUN5 (proj_tmp
                                                        * holds the FUN5 and has no room for both: 4225 B) */
static uint8_t *ed_sl24_at(uint32_t off) { return off < SL24_SIZE ? (uint8_t *)&proj_tmp + off : s24in_fv + (off - SL24_SIZE); }
static uint32_t ed_sl24_crc(void)                         /* the object received, its CRC-32 */
{
    uint32_t c = st_crc_upd(0xFFFFFFFFu, &proj_tmp, SL24_SIZE);
    return ~(s24in.len > SL24_SIZE ? st_crc_upd(c, s24in_fv, s24in.len - SL24_SIZE) : c);
}

static uint32_t ed_sl24_put(const uint8_t *a, uint32_t na)
{
    uint8_t *b = (uint8_t *)&proj_tmp;
    uint32_t op = na ? a[0] : 127u, n;
    if (s24in.on && !proj_tmp_busy())
        s24in.on = 0;                                     /* (let go meanwhile: ed_backup.c's 10 s) */
    if (op == 0u) {
        if (na != 9u)
            return 1;
        n = ed_g7(a + 1, 3);
        if (n != SL24_SIZE && n != SL24_PUT_MAX)
            return 6;
        if (song.playing || transport_req)
            return 3;
        if (proj_tmp_busy() && !s24in.on)
            return 9;
        proj_tmp_lent = 1;
        proj_tmp_t0 = fm1_ms;
        s24in.on = 1, s24in.len = n, s24in.crc = ed_g7(a + 4, 5), s24in.got = 0;
        return 0;
    }
    if (!s24in.on)
        return 1;
    proj_tmp_t0 = fm1_ms;                                 /* (the import goes on) */
    if (op == 1u) {
        uint8_t ch[256];
        uint32_t i;
        if (na < 10u || ed_g7(a + 1, 3) != s24in.got)
            return 1;
        n = ed_unpack7(a + 9, na - 9u, ch, s24in.len - s24in.got < sizeof ch ? s24in.len - s24in.got : (uint32_t)sizeof ch);
        if (!n)
            return 1;
        if (st_crc32(ch, n) != ed_g7(a + 4, 5))
            return 2;                                     /* (the editor sends it again) */
        for (i = 0; i < n; i++)
            *ed_sl24_at(s24in.got + i) = ch[i];
        s24in.got += n;
        return 0;
    }
    if (op != 2u)
        return 1;
    if (song.playing || transport_req)
        return 3;
    s24in.on = 0;
    proj_tmp_lent = 0;
    if (s24in.got != s24in.len || ed_sl24_crc() != s24in.crc)
        return 2;
    if (!sl24_is(b, SL24_SIZE))
        return 8;
    {
        const uint8_t *fv[NPART] = {0};
        uint32_t k, m = s24in.len > SL24_SIZE ? s24in_fv[0] : 0u;
        for (k = 0; k < NPART; k++)
            if ((m >> k) & 1u)
                fv[k] = s24in_fv + 1u + 128u * k;
        memcpy(st_buf, b, SL24_SIZE);                     /* (the import's source: not in proj_tmp, its destination) */
        return sl24_import_buf(st_buf, SL24_SIZE, fv) ? 0u : 8u;
    }
}
static void ed_sl24_bank_get(const uint8_t *a, uint32_t na)
{
    uint8_t ch[256];
    uint32_t at = na == 3u ? sl24_bank_at() : 0u, off = na == 3u ? ed_g7(a, 3) : 0u, crc = 0xFFFFFFFFu, i, n = 0;
    uint32_t rc = na != 3u ? 1u : !at ? 4u : off > SL24_BANK_LEN ? 1u : 0u;
    for (i = 0; !rc && i < SL24_BANK_LEN; i += n) {      /* (the CRC of it all, in pieces: no RAM copy) */
        n = SL24_BANK_LEN - i < sizeof ch ? SL24_BANK_LEN - i : (uint32_t)sizeof ch;
        rc = st_read(at + i, ch, n) ? 4u : 0u;
        crc = st_crc_upd(crc, ch, n);
    }
    n = rc ? 0u : SL24_BANK_LEN - off < sizeof ch ? SL24_BANK_LEN - off : (uint32_t)sizeof ch;
    if (n && st_read(at + off, ch, n))
        rc = 4, n = 0;
    ed_b(rc);
    ed_b7(rc ? 0u : SL24_BANK_LEN, 3);
    ed_b7(rc ? 0u : ~crc, 5);
    ed_b7(off, 3);
    if (n)
        ed_pack7(ch, n);
}
#endif

static int ed_sl24(uint32_t cmd, const uint8_t *a, uint32_t na)
{
#if FELUCCA_SL24_EXPORT
    if (cmd == ED_SL24_GET) {
        ed_sl24_get(a, na);
        return 1;
    }
#endif
#if FELUCCA_SL24_EDIMPORT
    if (cmd == ED_SL24_PUT) {
        uint32_t rc = ed_sl24_put(a, na);
        ed_b(na ? a[0] : 127u);
        if (na && a[0] == 1u)
            ed_b7(na >= 4u ? ed_g7(a + 1, 3) : 0u, 3);
        ed_b(rc);
        return 1;
    }
    if (cmd == ED_SL24_BANK) {
        ed_sl24_bank_get(a, na);
        return 1;
    }
#endif
    (void)a, (void)na;
    return 0;
}
