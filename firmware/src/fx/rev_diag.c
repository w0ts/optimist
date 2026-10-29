/* SPDX-License-Identifier: GPL-3.0-only */
/* Why the reverb's return is silent now (2026-10-29, the user on the FM-1: "the FDN8 reverb stops working randomly. No
 * idea if it is the send, the mixer or the reverb"): what the device can tell over USB the next time it happens (the
 * console's status rev_why / rev_sheds, fm1-base's LG readout "reverb why ..", the debug stats' rev byte). Main loop:
 * reads only. Included by fx.c after fx_arena.c. rev_why()'s bits, each a state that turns the reverb off by design:
 *   REVW_GUARD  the CPU guard's REVERB level (cpuguard.h CG_L_REVERB: the wet faded out, the tank off)
 *   REVW_RET0   REV RET at 0 (MASTER's RET card)
 *   REVW_RMUTE  the return's output muted (fx.c fx_rmute: SHIFT on MASTER's RET card, the editor's RET MUTE)
 *   REVW_IMUTE  the return's input muted (fx_slots.c fx_imute: the mute mode's E F G keys)
 *   REVW_MEM    its line not placed in the arena (fx_arena.c: refused, MEM FULL, dropped, or gated a moment for a move)
 *   REVW_SLOT   no master return slot holds REV (FX > the slots: ----, or another type there)
 *   REVW_OFF    the bus not on now (rev_guard's state: fading, clearing its line, off), whatever the reason
 * rev_sheds: how often the guard turned a running reverb off since the start (fx.c rev_guard) */
enum { REVW_GUARD = 1, REVW_RET0 = 2, REVW_RMUTE = 4, REVW_IMUTE = 8, REVW_MEM = 16, REVW_SLOT = 32, REVW_OFF = 128 };
static __attribute__((noinline, unused)) uint32_t rev_why(void)
{
    uint32_t w = 0;
#if FELUCCA_FX_REVERB
    w |= CG_LEVEL >= CG_L_REVERB ? (uint32_t)REVW_GUARD : 0u;
    w |= !song.g[G_RRET] ? (uint32_t)REVW_RET0 : 0u;
    w |= fx_rmute >> 2 & 1u ? (uint32_t)REVW_RMUTE : 0u;
    w |= fx_imute >> 2 & 1u ? (uint32_t)REVW_IMUTE : 0u;
#if FXM_NEED
    w |= ((fxm_off | fxm_full) >> FXM_REV & 1u) ? (uint32_t)REVW_MEM : 0u;
#endif
    w |= !FXS_HAS(FXT_REV) ? (uint32_t)REVW_SLOT : 0u;
#if CG_REV
    w |= rgd.st != RG_ON ? (uint32_t)REVW_OFF : 0u;
#endif
#endif
    return w;
}
static __attribute__((unused)) uint32_t rev_shed_count(void)
{
#if CG_REV
    return rev_sheds;
#else
    return 0;
#endif
}
