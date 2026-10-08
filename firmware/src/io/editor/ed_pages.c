/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the device's pages, so the editor lays out a track's sound as the device does, whatever the engine
 * and the build (included by editor.c; web/EDITOR_PROTOCOL.md "Pages").
 *   52 PAGES  start  -> start, total, n, then n x (family, scope, shown, id0..id3, title): params.c PAGES in order;
 *                       family FAM_*, scope SC_* (0 a track's P_*, 1 a global G_*, 2 the engine's P_E*; others are the
 *                       device's own screens), shown 1 when the page is there for the selected track (page_shown: an
 *                       engine's own pages, the drum track's), an id 127 = an empty slot. At most ED_PG_PAGE a reply */
enum { ED_PAGES = 52 };
#define ED_PG_PAGE 24u

static int ed_pages(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, n = 0, at, k;
    if (cmd != ED_PAGES || na < 1u)
        return 0;
    ed_b(a[0]);
    ed_b(NPAGES);
    at = ed_n;
    ed_b(0);
    for (i = a[0]; i < NPAGES && n < ED_PG_PAGE; i++, n++) {
        const page_t *pg = &PAGES[i];
        ed_b(pg->fam);
        ed_b(pg->scope);
        ed_b((uint32_t)!!page_shown(pg));
        for (k = 0; k < 4u; k++)
            ed_b(pg->id[k] == 0xFFu ? 127u : pg->id[k]);
        ed_str(pg->title, 12);
    }
    ed_out[at] = (uint8_t)n;
    return 1;
}
