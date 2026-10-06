/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' own settings that are no G_ / P_ parameter (G_COUNT and P_COUNT are the project
 * format: they cannot grow). Their pages use the scope SC_BPSET: page_desc gives these descriptors and values,
 * so the pages draw, edit and show in the overview as any other. Included by params.c (backports.h switches).
 *   BPS_RTYPE  REVERB > TYPE: ROOM / SPRING (FELUCCA_SPRING, spring.c); saved in the project (project_t.rsv[0])
 * A build without the switch has no page, ignores the byte and plays ROOM. */
enum { BPS_RTYPE, BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO, BPS_COUNT };
/*   BPS_GDENS..BPS_GGO  ACID GEN (FELUCCA_ENG_ACID, eng_acid.c): TB-3PO's density, accent and slide (%), GO;
 *                      not saved (TB-3PO's defaults at power-on and NEW) */
static int16_t bps_v[BPS_COUNT] = {[BPS_GDENS] = 70, [BPS_GACC] = 40, [BPS_GSLD] = 25};
static const char *const N_RTYPE[] = {"ROOM", "SPRNG"};   /* (5 characters: the value column) */
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
    *vp = &bps_v[id];
    return &BPS_DESC[id];
}

/* a new project: the defaults */
static void bps_defaults(void)
{
    uint32_t i;
    for (i = 0; i < BPS_COUNT; i++)
        bps_v[i] = BPS_DESC[i].def;
}

/* the project's byte of them (project_t.rsv[0]): bit 0 SPRING */
static uint8_t bps_pack(void) { return (uint8_t)(bps_v[BPS_RTYPE] == 1); }
static void bps_unpack(uint8_t b) { bps_v[BPS_RTYPE] = (int16_t)(b & 1u); }
