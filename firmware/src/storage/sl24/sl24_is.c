/* SPDX-License-Identifier: GPL-3.0-only */
/* A SLOOP 2.4 .. 2.7 project (isod89/sloop-fm1, "FUN5" 3840 B / "FUN6" 3816 B), told apart from ours (FELUCCA_SL24_SAFE,
 * backports24.h). Included by project.c before sl24_guard.c, which keeps what it finds. Nothing here converts or reads
 * one: Optimist neither imports nor exports SLOOP projects (removed 2026-10-29); a SLOOP project in a slot or the
 * autosave is only recognised, shown as SLOOP's and never erased. */
#define SL24_MAGIC 0x46554E35u                         /* "FUN5", as our SLOOP plus format, told apart by size */
#define SL24_SIZE 3840u
#define SL26_MAGIC 0x46554E36u                         /* "FUN6": SLOOP 2.6 (ours used it in test builds, other sizes) */
#define SL26_SIZE 3816u

/* n bytes at b are a SLOOP project of magic m and size z (project.c proj_import there: magic, size, FNV-1a sum) */
static int sl24_is_of(const void *b, int n, uint32_t m0, uint32_t z0)
{
    const uint8_t *c = (const uint8_t *)b;
    uint32_t m, z, s;
    if (n != (int)z0)
        return 0;
    memcpy(&m, c, 4), memcpy(&z, c + 4, 4), memcpy(&s, c + z0 - 4u, 4);
    return m == m0 && z == z0 && s == proj_hash(c, z0 - 4u);
}
/* a SLOOP 2.4 / 2.5 project (FUN5) */
static int sl24_is(const void *b, int n) { return sl24_is_of(b, n, SL24_MAGIC, SL24_SIZE); }
/* a SLOOP 2.6 project (FUN6) */
static int sl26_is(const void *b, int n) { return sl24_is_of(b, n, SL26_MAGIC, SL26_SIZE); }
/* either: PROJECT shows it as SLOOP's */
static int sl24_any(const void *b, int n) { return sl24_is(b, n) || sl26_is(b, n); }
