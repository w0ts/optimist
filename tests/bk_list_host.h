/* SPDX-License-Identifier: GPL-3.0-only */
/* BK_LIST v3 as the web editor reads it (web/editor.html bkListAll): page after page, first = the objects so far, until
 * all n are in. Included by the host tests that drive ed_backup.c (after it; they have ed_out, ed_n, ed_backup).
 * bk_list_find: the object tag -> its index (and length, CRC, kind, flags; each may be NULL), -1 none or a page wrong. */
static int bk_list_find(const char *tag, uint32_t *len, uint32_t *crc, uint32_t *kind, uint32_t *flags)
{
    uint8_t a[2] = {BK_VERSION, 0};
    uint32_t got = 0, k, p, n;
    do {
        a[1] = (uint8_t)got;
        ed_n = 0;
        if (!ed_backup(ED_BK_LIST, a, 2) || ed_out[0] != BK_VERSION || ed_out[10] != got || !ed_out[11])
            return -1;
        n = ed_out[1];
        for (k = 0, p = 12; k < ed_out[11]; k++, p += 14)
            if (!memcmp(ed_out + p, tag, 4)) {
                if (len)
                    *len = ed_r32(ed_out + p + 6, 3);
                if (crc)
                    *crc = ed_r32(ed_out + p + 9, 5);
                if (kind)
                    *kind = ed_out[p + 4];
                if (flags)
                    *flags = ed_out[p + 5];
                return (int)(got + k);
            }
        got += ed_out[11];
    } while (got < n);
    return -1;
}
