/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb's algorithms: which this build has (registry.h FELUCCA_REV_ROOM / _PLATE / _FDN8 / _AIRWIN, backports.h
 * FELUCCA_SPRING), FX > REVERB > TYPE's list, the project's byte. Included by params.c before bp_set.c.
 *   - TYPE lists the algorithms built, in this order: ROOM, PLATE, FDN8, VTINY (AIRWIN), SPRING; its value (bp_set.c
 *     BPS_RTYPE) is an index into REV_ALGO. One built: no TYPE (no page, no editor control), that one plays. fx.c's
 *     rev_bus switches tanks at run time (the old one fades out, the shared line buffer is cleared).
 *   - The project keeps the algorithm in project_t.rsv[0] (FUNB, no new field): bit 0 SPRING (as SPRING builds wrote
 *     it), bit 1 PLATE, bit 2 FDN8, bits 1 and 2 AIRWIN (VTINY: a pair no build wrote), none ROOM. Older projects: 0
 *     ROOM, 1 SPRING, as before. An older SPRING build reads only bit 0: PLATE, FDN8 and AIRWIN play ROOM there; a
 *     build older than AIRWIN reads it as PLATE (bit 1 first).
 *   - A project asking for an algorithm this build lacks plays the first one built and keeps the request (rev_orph):
 *     a save writes it back, MISSING names it ("MISSING: REVERB FDN8", miss.c), until TYPE is turned away. */
enum { RT_ROOM, RT_SPRING, RT_PLATE, RT_FDN8, RT_AIRWIN, RT_N };   /* an algorithm's code (the editor's INFO mask: bit
                                                                * RT_x) */
#define REV_NALGO (FELUCCA_REV_ROOM + FELUCCA_REV_PLATE + FELUCCA_REV_FDN8 + FELUCCA_REV_AIRWIN + FELUCCA_SPRING)
#define REV_MULTI (FELUCCA_FX_REVERB && REV_NALGO > 1)   /* TYPE on the device and in the editor */
#if FELUCCA_FX_REVERB && REV_NALGO == 0
#error "the reverb bus needs an algorithm: FELUCCA_REV_ROOM, _PLATE, _FDN8, _AIRWIN or FELUCCA_SPRING"
#endif
static const uint8_t REV_ALGO[] = {      /* the built ones, TYPE's order */
#if FELUCCA_REV_ROOM || REV_NALGO == 0
    RT_ROOM,
#endif
#if FELUCCA_REV_PLATE
    RT_PLATE,
#endif
#if FELUCCA_REV_FDN8
    RT_FDN8,
#endif
#if FELUCCA_REV_AIRWIN
    RT_AIRWIN,
#endif
#if FELUCCA_SPRING
    RT_SPRING,
#endif
};
static const char *const N_RTYPE[] = {   /* their names on the knob (5 characters: the value column) */
#if FELUCCA_REV_ROOM || REV_NALGO == 0
    "ROOM",
#endif
#if FELUCCA_REV_PLATE
    "PLATE",
#endif
#if FELUCCA_REV_FDN8
    "FDN8",
#endif
#if FELUCCA_REV_AIRWIN
    "VTINY",
#endif
#if FELUCCA_SPRING
    "SPRNG",
#endif
};
#define REV_NLIST (sizeof REV_ALGO / sizeof REV_ALGO[0])
#define REV_FIRST (FELUCCA_REV_ROOM || REV_NALGO == 0 ? RT_ROOM : FELUCCA_REV_PLATE ? RT_PLATE :     \
                   FELUCCA_REV_FDN8 ? RT_FDN8 : FELUCCA_REV_AIRWIN ? RT_AIRWIN : RT_SPRING)   /* REV_ALGO[0] */
_Static_assert(sizeof N_RTYPE / sizeof N_RTYPE[0] == REV_NLIST, "rev_type.c: a name per algorithm");
#define REV_MASK ((FELUCCA_REV_ROOM << RT_ROOM) | (FELUCCA_SPRING << RT_SPRING) | (FELUCCA_REV_PLATE << RT_PLATE) | \
                  (FELUCCA_REV_FDN8 << RT_FDN8) | (FELUCCA_REV_AIRWIN << RT_AIRWIN))   /* the editor's INFO tag 0x52 */

#if REV_MULTI
static uint32_t rev_sel(void);           /* bp_set.c: TYPE's value, an index into REV_ALGO */
static void rev_sel_set(uint32_t i);
#else
static uint32_t rev_sel(void) { return 0; }
static void rev_sel_set(uint32_t i) { (void)i; }
#endif
static uint8_t rev_orph = 0xFF;          /* the code a loaded project asked for and this build lacks; 0xFF none */

static int32_t rev_index(uint32_t c)     /* code -> its place in TYPE's list, -1 not built */
{
    uint32_t i;
    for (i = 0; FELUCCA_FX_REVERB && i < REV_NLIST; i++)
        if (REV_ALGO[i] == c)
            return (int32_t)i;
    return -1;
}
static uint32_t rev_cur(void) { return REV_ALGO[rev_sel()]; }   /* the algorithm TYPE picks */
/* TYPE turned away from the stand-in: the request is the user's no longer (fx.c's switch, a save, the MISSING scan) */
static void rev_seen(void)
{
    if (rev_sel() != 0u)
        rev_orph = 0xFF;
}
/* the project byte <-> the algorithm (see above) */
static uint8_t rev_pack(void)
{
    uint32_t c;
    rev_seen();
    c = rev_orph != 0xFFu ? rev_orph : rev_cur();
    return (uint8_t)(c == RT_SPRING ? 1u : c == RT_PLATE ? 2u : c == RT_FDN8 ? 4u : c == RT_AIRWIN ? 6u : 0u);
}
static void rev_unpack(uint8_t b)
{
    uint32_t c = b & 1u ? RT_SPRING : (b & 6u) == 6u ? RT_AIRWIN : b & 2u ? RT_PLATE : b & 4u ? RT_FDN8 : RT_ROOM;
    int32_t i = rev_index(c);
    rev_orph = (uint8_t)(i < 0 ? c : 0xFFu);
    rev_sel_set(i < 0 ? 0u : (uint32_t)i);
}
static void rev_defaults(void)           /* a new project: the first algorithm built, nothing asked for */
{
    rev_orph = 0xFF;
    rev_sel_set(0);
}
