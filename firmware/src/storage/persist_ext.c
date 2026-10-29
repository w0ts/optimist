/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings record's sync extension (CLK OUT's mode, LINK, CLK IN = LINK, CLK OFS / BT OFS / LINK OFS): how a record read
 * sets them and how settings_save writes them. Included by project.c (FELUCCA_FLASH, after persist_rec.c and settings_word.c)
 * and by tests/favorites_test.c, whose project.c is PROJ_HOST (no flash part): one copy both compile. Needs clock_out.c's
 * clko_on, clock_sync.c's link_mode / sync_ofs (through seq.c) and settings_word.c's sync_boot. */
/* CLK OUT's mode and LINK's, beside the settings word (whose bit 26 stays ON / OFF): a record from before has ext_ver 0, bit 26 = BOTH / OFF,
 * LINK = ON+S/S. Without BLE a BOTH is USB (the list has no BOTH) */
static void persist_ext_load(const persist_t *p)
{
#if FELUCCA_MIDI_CLKOUT
    if (p->ext_ver && clko_on && p->clko_sel >= CLKO_BOTH && p->clko_sel <= CLKO_BLE)
        clko_on = p->clko_sel;
#if !FELUCCA_BLE
    if (clko_on == CLKO_BOTH || clko_on == CLKO_BLE)
        clko_on = CLKO_USB;                        /* (no BLE: the list is OFF, USB) */
#endif
#endif
#if FELUCCA_LINK
    link_mode = (uint8_t)(p->ext_ver && p->link_sel <= LINK_ONSS ? p->link_sel : LINK_ONSS);
    if (p->ext_ver && p->sync_sel == 1u && GP[G_SYNC].max >= SYNC_LINK)
        sync_boot = (uint8_t)(song.g[G_SYNC] = SYNC_LINK);   /* CLK IN LINK (the word read AUTO) */
#endif
    (void)p;
}
static void persist_ext_save(persist_t *p)
{
    p->ext_ver = 1;
#if FELUCCA_MIDI_CLKOUT
    p->clko_sel = clko_on;
#endif
#if FELUCCA_LINK
    p->link_sel = link_mode;
    p->sync_sel = song.g[G_SYNC] == SYNC_LINK;
#endif
}
#if FELUCCA_MIDI_CLOCK
static void persist_ofs_load(const persist_t *p)  /* CLK OFS, BT OFS, LINK OFS (clock_sync.c): out of range read as 0 */
{
    uint32_t i;
    int16_t l = p->ext_ver ? p->link_ofs : p->clk_ofs[0];   /* (a record from before: Link used CLK OFS) */
    for (i = 0; i < 2u; i++)
        sync_ofs[i] = (int16_t)(p->clk_ofs[i] >= SY_OFS_MIN && p->clk_ofs[i] <= SY_OFS_MAX ? p->clk_ofs[i] : 0);
    sync_ofs[2] = (int16_t)(l >= SY_OFS_MIN && l <= SY_OFS_MAX ? l : 0);
}
static void persist_ofs_save(persist_t *p)
{
    p->clk_ofs[0] = sync_ofs[0];
    p->clk_ofs[1] = sync_ofs[1];
    p->link_ofs = sync_ofs[2];
}
#else
#define persist_ofs_load(p) ((void)0)              /* (no clock: the bytes kept as read) */
#define persist_ofs_save(p) ((void)0)
#endif
