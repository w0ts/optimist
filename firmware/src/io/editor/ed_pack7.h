/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: bytes as pack7 on ed_b (a byte of high bits, then up to 7 bytes): the slots read back, kits, backup,
 * snapshots. Included by ed_drums.c (the live editor) and ed_lite.c (a backup-only build has backup and snapshots without
 * the drums' commands); host tests that drive ed_backup.c or ed_snap.c alone keep their own copy. */
#ifndef ED_PACK7_H
#define ED_PACK7_H
#include "ed_reply.h"         /* (ed_bs) */
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[j] >> 7) << j;
        ed_b(m);
        ed_bs(p, k);
        p += k;
        n -= k;
    }
}
#endif
