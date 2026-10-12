/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes at the hit (the user, 2026-10-12: D2), included by seq.c after the automation store: what a hit of
 * lane l plays over the patch dl.ofs (drum_edit.c dl_ofs_play), computed once a hit by drum_on (dl_play_hit, the ISR):
 *   a lock      the step playing's (auto.c auto_step: dla.lv, dla.lm), wins
 *   a hold      the motion's last value (dla.hv, dla.hm), else the patch's
 *   the LFO     the lane's own (dlfo_mod), added on top
 * then clamped into the value's range (DE_MIN..DE_MAX). A lane with none of them: dl_play_hit leaves it alone and the hit
 * reads dl_ofs as before (no other work). XIP: none of it is in RAM code. */
#if DLA_ON || DLFO_ON
static int8_t dlp_o[DE_N];                 /* the offsets of the hit dl_play_hit computed (dlp_p) */
#if DLFO_ON
static int32_t dlfo_now(uint32_t l);       /* lane l's LFO now (Q15; below) */
static int32_t dlfo_add(uint32_t l, uint32_t v, int32_t lfo);
#endif

static void dl_play_hit(uint32_t l)
{
    uint32_t v, hm, lm, any = 0;
    int32_t lfo = 0;
    dlp_l = 0xFFu;
    if (l >= DRUM_LANES || !(DLA_ANY(l) | ((uint32_t)(dlfo_on >> l) & 1u)))
        return;                            /* (none on l: dl_ofs, as before) */
#if DLA_ON
    hm = dla.hm[l], lm = dla.lm[l];
#else
    hm = lm = 0;
#endif
#if DLFO_ON
    if ((dlfo_on >> l) & 1u)
        lfo = dlfo_now(l);
#endif
    for (v = 0; v < DE_N; v++) {
        int32_t x = dl.ofs[l][v];
#if DLA_ON
        if ((lm >> v) & 1u)
            x = dla.lv[l][v];
        else if ((hm >> v) & 1u)
            x = dla.hv[l][v];
#endif
#if DLFO_ON
        if (lfo)
            x += dlfo_add(l, v, lfo);
#endif
        x = clamp(x, DE_MIN[v], DE_MAX[v]);
        dlp_o[v] = (int8_t)x;
        any |= (uint32_t)x;
    }
    (void)hm, (void)lm, (void)lfo;
    dlp_p = any ? dlp_o : 0;               /* (all 0: the kit as it is, dl_ofs's contract) */
    dlp_l = (uint8_t)l;
}
#endif
