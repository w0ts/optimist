/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the favorites (core/favorites.c, FELUCCA_FAVORITES; included by editor.c; web/EDITOR_PROTOCOL.md "v13: the favorites").
 * The device marks sounds as favorites (SHIFT2 + PRESETS on its panel) and PRESETS can browse the marked ones only (SHIFT2 + SELECT); this is the
 * same store: a factory sound is marked by its engine and name, a user preset by its slot, up to FAV_N (64) marks; saved with the settings.
 *   100 FAVS  op [...] -> op [...]
 *       op 0                       -> 0, factory marks (of FAV_N at most), FAV_N, list (the PRESETS list: 0 ALL, 1 FAV), user presets marked
 *       op 1  engine               -> 1, engine, n, the marks of the engine's n presets, 7 to a byte (bit k of the stream: preset k is marked)
 *                                     engine NENGINES (the number of engines): the user presets, n = UP_SLOTS
 *       op 2  engine, k, on        -> 2, engine, k, rc (0 as it was, 1 changed, 2 FULL: FAV_N factory marks), factory marks
 *       op 3  list                 -> 3, list (the PRESETS list shown: ALL or FAV), the device's own browsing follows it
 * A bad op, engine or k: no reply. INFO tag 0x5D: version 1, FAV_N. */
enum { ED_FAV = 100 };
#if FELUCCA_FAVORITES
static uint32_t edf_n(uint32_t e) { return e == NENGINES ? (uint32_t)UP_SLOTS : e < NENGINES ? ENGINES[e]->npresets : 0u; }
static int edf_has(uint32_t e, uint32_t k) { return e == NENGINES ? fav_user_has(k) : fav_has(eng_uid(e), preset_name(e, k)); }
static int ed_fav(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, e, k, n, i;
    if (cmd != ED_FAV || op > 3u)
        return 0;
    if (op == 0u) {
        {
        uint32_t u = 0;
        for (i = 0; i < UP_SLOTS; i++)
            u += (uint32_t)fav_user_has(i);
        ed_b(0), ed_b(fav_count()), ed_b(FAV_N), ed_b(fav_list), ed_b(u);
    }
        return 1;
    }
    if (op == 3u) {
        if (na < 2u || a[1] > 1u)
            return 0;
        fav_list = a[1];
        settings_later = 1;
        ui.force = 1;
        ed_b(3), ed_b(fav_list);
        return 1;
    }
    if (na < 2u || (e = a[1]) > NENGINES)
        return 0;
    n = edf_n(e);
    if (op == 1u) {
        ed_b(1), ed_b(e), ed_b(n);
        for (i = 0; i < n; i += 7u) {
            uint32_t b = 0, j;
            for (j = 0; j < 7u && i + j < n; j++)
                b |= (uint32_t)(edf_has(e, i + j) != 0) << j;
            ed_b(b);
        }
        return 1;
    }
    if (na < 4u || (k = a[2]) >= n)
        return 0;
    {
        int r = e == NENGINES ? fav_user_set(k, a[3] != 0u) : fav_set(eng_uid(e), preset_name(e, k), a[3] != 0u);
        if (r > 0) {
            settings_later = 1;
            ui.force = 1;
        }
        ed_b(2), ed_b(e), ed_b(k), ed_b(r < 0 ? 2u : (uint32_t)r), ed_b(fav_count());
    }
    return 1;
}
static void ed_fav_info(void)
{
    ed_b(0x5D);
    ed_b(2);
    ed_b(1);
    ed_b(FAV_N);
}
#else
static int ed_fav(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_fav_info(void) {}
#endif
