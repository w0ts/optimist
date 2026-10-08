/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (docs: fm1-firmware FX-SLOTS-INSERTS-DESIGN.md; included by params.c after rev_type.c).
 *   - Four slots, the FX page's four knobs. A slot is empty or holds one effect TYPE and is named after it (its
 *     amount's label: DST CHO DLY REV FILT ...). Default, and every project from before: DIST CHO DLY REV.
 *   - One instance per type: a type sits in one slot at most (loading one held elsewhere swaps the two slots).
 *   - The amounts belong to the type, not the slot (decision D3): a synth track's are its P_DIST P_CHOR P_DLY P_REV
 *     (P_TFLT: the FILTER, decision D6), so presets, locks, motion, MACRO SPACE and SLOOP 2.4 keep their meaning
 *     whatever the layout. A type in no slot is not heard and keeps its amounts.
 *   - The inserts run in a fixed order whatever the slots (decision D4): DIST, SLICER, FILTER, COMP, pre-fader.
 * Type ids are stored (the FX record): never reuse one. */
enum { FXT_NONE, FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV, FXT_COMP, FXT_FILT, FXT_N };
#define FX_NSLOT 4u
#define FXT_BIT(x) (1u << (x))
#define FXT_INSERT (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_COMP) | FXT_BIT(FXT_FILT))   /* the others: send buses */
#define FXT_BUILT ((FELUCCA_FX_DIST ? FXT_BIT(FXT_DIST) : 0u) | (FELUCCA_FX_CHORUS ? FXT_BIT(FXT_CHO) : 0u) | \
                   (FELUCCA_FX_DELAY ? FXT_BIT(FXT_DLY) : 0u) | (FELUCCA_FX_REVERB ? FXT_BIT(FXT_REV) : 0u) |  \
                   (FELUCCA_TRK_FILT ? FXT_BIT(FXT_FILT) : 0u))
/* each type's per-track amount (a P_* id; its label names the slot), 0xFF: none in this build */
static const uint8_t FXT_AMT[FXT_N] = {
    0xFF, P_DIST, P_CHOR, P_DLY, P_REV, 0xFF,
#if FELUCCA_TRK_FILT
    P_TFLT,
#else
    0xFF,
#endif
};
static uint8_t fxs_slot[FX_NSLOT] = {FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV};

/* slot k's amount id (0xFF: empty, or a type this build lacks) */
static uint32_t fxs_amt(uint32_t k)
{
    uint32_t t = fxs_slot[k & 3u];
    return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
}
