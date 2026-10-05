/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' own settings that are no G_ / P_ parameter (G_COUNT and P_COUNT are the project
 * format: they cannot grow). Their pages use the scope SC_BPSET: page_desc gives these descriptors and values,
 * so the pages draw, edit and show in the overview as any other. Included by params.c (backports.h switches).
 *   BPS_RTYPE  REVERB > TYPE: ROOM / SPRING (FELUCCA_SPRING, spring.c); saved in the project (project_t.rsv[0])
 * A build without the switch has no page, ignores the byte and plays ROOM. */
enum { BPS_RTYPE, BPS_COUNT };
static int16_t bp_set[BPS_COUNT];               /* (0: every default) */
static const char *const N_RTYPE[] = {"ROOM", "SPRNG"};   /* (5 characters: the value column) */
static const param_desc_t BPS_DESC[BPS_COUNT] = {
    [BPS_RTYPE] = PE("TYPE", N_RTYPE, 0),
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

/* the project's byte of them (project_t.rsv[0]): bit 0 SPRING */
static uint8_t bps_pack(void) { return (uint8_t)(bp_set[BPS_RTYPE] == 1); }
static void bps_unpack(uint8_t b) { bp_set[BPS_RTYPE] = (int16_t)(b & 1u); }
