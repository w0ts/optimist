/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the working project for SLOOP 2.4 (FELUCCA_SL24_EXPORT; included by editor.c; web/EDITOR_PROTOCOL.md
 * "SLOOP 2.4 export"). The editor reads two parts and writes them into a SLOOP 2.4 backup file ("sloop-backup", its
 * objects 0 and 1), which 2.4's editor restores: the project becomes 2.4's working project, the settings its settings.
 *   78 SL24_GET  part, offset (3 x 7 bit) -> part, rc (0 ok, 1 arguments), length (3 x 7 bit), CRC-32 of the part
 *                (5 x 7 bit), lost (2 x 7 bit: sl24_export.c SX24_*), offset, pack7 bytes (<= 256; none at its end)
 *     part 0  the working project as 2.4's FUN5 (3840 B, sl24_export.c proj_to_sl24), its step extras too
 *     part 1  the settings as 2.4's persist_t (88 B: palette, low cut, zoom, the panel table, 2.4's song order, the
 *             lights word: sl24_persist)
 * Each read makes the part again from the state now (no buffer kept, nothing written): the editor checks that every
 * reply carries the same CRC and that it matches what it put together, else reads again (it changed meanwhile).
 * A build without the export does not answer: the editor shows no button. */
enum { ED_SL24_GET = 78 };
_Static_assert(sizeof proj_tmp >= SL24_SIZE && sizeof(panel_t) == 32u, "the export is made in proj_tmp; 2.4's PAN5");

#if FELUCCA_AUTO
static stepx_t ed_sl24_x[NTRK] __attribute__((section(".pool")));   /* (the automation as 2.4's extras) */
#endif
static uint32_t ed_sl24_make(uint32_t part, uint32_t *lost)   /* -> its length, made in proj_tmp */
{
    uint8_t *o = (uint8_t *)&proj_tmp;
    *lost = 0;
    if (part == 0u) {
        const stepx_t *x[NTRK] = {0};
        uint32_t r = 0;
#if FELUCCA_AUTO
        uint32_t k;
        for (k = 0; k < NTRK; k++) {                      /* (the automation as 2.4's extras: auto.h) */
            r |= sl24_auto_out(&ed_sl24_x[k], AUTO_L(k));
            x[k] = &ed_sl24_x[k];
        }
#endif
        proj_capture(&autosave_buf, &autosave_dl);        /* (autosave_buf: the work, as the snapshots take it) */
        *lost = proj_to_sl24(&autosave_buf, x, o) | r;
        return SL24_SIZE;
    }
#if BP23_SET
    sl24_persist(o, settings.palette, settings.lowcut, settings.zoom, &panel, sl24_word(bp23_word(), song.g[G_SYNC]));
#else
    sl24_persist(o, settings.palette, settings.lowcut, settings.zoom, &panel, sl24_word(0x100u, song.g[G_SYNC]));
#endif
    return SL24_PERSIST;
}

static int ed_sl24(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t part = na ? a[0] : 127u, off = na >= 4u ? a[1] | a[2] << 7 | (uint32_t)a[3] << 14 : 0u, len = 0, lost = 0;
    uint32_t rc = na != 4u || part > 1u, n, crc = 0, i;
    if (cmd != ED_SL24_GET)
        return 0;
    if (!rc) {
        len = ed_sl24_make(part, &lost);
        crc = st_crc32(&proj_tmp, len);
        rc = off > len;
    }
    n = rc ? 0u : len - off < 256u ? len - off : 256u;
    ed_b(part);
    ed_b(rc);
    for (i = 0; i < 3u; i++)
        ed_b(len >> (7u * i));
    for (i = 0; i < 5u; i++)
        ed_b(crc >> (7u * i));
    ed_b(lost);
    ed_b(lost >> 7);
    for (i = 0; i < 3u; i++)
        ed_b(off >> (7u * i));
    if (n)
        ed_pack7((const uint8_t *)&proj_tmp + off, n);
    return 1;
}
