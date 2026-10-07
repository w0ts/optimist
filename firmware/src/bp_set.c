/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' own settings that are no G_ / P_ parameter (G_COUNT and P_COUNT are the project
 * format: they cannot grow). Their pages use the scope SC_BPSET: page_desc gives these descriptors and values,
 * so the pages draw, edit and show in the overview as any other. Included by params.c (backports.h switches).
 *   BPS_RTYPE  REVERB > TYPE: an index into the reverb algorithms built (rev_type.c REV_ALGO; REV_MULTI: two or more
 *              built, else no page); saved in the project (project_t.rsv[0], rev_type.c rev_pack) */
enum { BPS_RTYPE, BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO, BPS_COUNT };
/*   BPS_GDENS..BPS_GGO  ACID GEN (FELUCCA_ENG_ACID, eng_acid.c): TB-3PO's density, accent and slide (%), GO;
 *                      not saved (TB-3PO's defaults at power-on and NEW) */
static int16_t bp_set[BPS_COUNT] = {[BPS_GDENS] = 70, [BPS_GACC] = 40, [BPS_GSLD] = 25};
static const char *const N_BPGO[] = {"--", "GO"};
static const param_desc_t BPS_DESC[BPS_COUNT] = {
    [BPS_RTYPE] = PE("TYPE", N_RTYPE, 0),
    [BPS_GDENS] = PD("DENS", F_INT, 0, 100, 70),
    [BPS_GACC] = PD("ACC", F_INT, 0, 100, 40),
    [BPS_GSLD] = PD("SLD", F_INT, 0, 100, 25),
    [BPS_GGO] = PE("GEN", N_BPGO, 0),
};

static const param_desc_t *bps_desc(uint32_t id, int16_t **vp)
{
    if (id >= BPS_COUNT) {
        *vp = 0;
        return 0;
    }
    *vp = &bp_set[id];
    return &BPS_DESC[id];
}

/* a new project: the defaults */
static void bps_defaults(void)
{
    uint32_t i;
    for (i = 0; i < BPS_COUNT; i++)
        bp_set[i] = BPS_DESC[i].def;
}

#if REV_MULTI
/* TYPE's value (rev_type.c): an index into REV_ALGO, the first one when out of range */
static uint32_t rev_sel(void)
{
    uint32_t i = (uint32_t)bp_set[BPS_RTYPE];
    return i < REV_NLIST ? i : 0u;
}
static void rev_sel_set(uint32_t i) { bp_set[BPS_RTYPE] = (int16_t)i; }
#endif
