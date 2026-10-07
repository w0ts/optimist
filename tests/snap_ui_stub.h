/* SPDX-License-Identifier: GPL-3.0-only */
/* SAVE > SNAPSHOT's side of snapshots.c for the UI tests that do not build it (ui_pages_test, missing_test,
 * preset_cover_test): five rows to draw (saved, another build's, damaged, empty, BEFORE LOAD), the GO buttons
 * counted. tests/snapshots_test.c tests the real one. */
#if FELUCCA_SNAPSHOTS
static uint32_t sn_ops[3];                            /* LOAD, CLEAR, SAVE acted */
static uint32_t sn_ui_n(void) { return FELUCCA_SNAPSHOTS + 1u; }
static void sn_ui_label(char *b, uint32_t i)
{
    b[0] = i < FELUCCA_SNAPSHOTS ? (char)('1' + i) : 'B';
    b[1] = 0;
}
static uint32_t sn_ui_row(uint32_t i, char *name, uint32_t *bytes)
{
    static const char *const N[4] = {"120 ABC", "96 A-H", "DAMAGED", "EMPTY"};
    static const uint32_t ST[4] = {1, 2, 3, 0}, SZ[4] = {2121, 7930, 0, 0};
    uint32_t k = i < FELUCCA_SNAPSHOTS ? (i < 4u ? i : 3u) : 0u;
    str_cpy(name, i < FELUCCA_SNAPSHOTS ? N[k] : "118 AB", 13);
    *bytes = i < FELUCCA_SNAPSHOTS ? SZ[k] : 1650u;
    return i < FELUCCA_SNAPSHOTS ? ST[k] : 1u;
}
static uint32_t sn_ui_free(void) { return 3u * 4064u; }
static int sn_ui_fits(void) { return 1; }
static uint32_t sn_ui_sig(void) { return sn_ops[0] + sn_ops[1] * 3u + sn_ops[2] * 7u; }
static void sn_ui(uint32_t op, uint32_t i)
{
    char l[2];
    sn_ui_label(l, i);
    sn_ops[op % 3u]++;
    ui_say_st(1u, op == 2u ? "SNAP SAVED " : op == 1u ? "SNAP CLEARED " : "SNAP LOADED ", l);
}
#endif
