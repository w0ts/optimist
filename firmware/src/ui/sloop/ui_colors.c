/* SPDX-License-Identifier: GPL-3.0-only */
/* The colour language of the screens (docs/WEB-DESIGN-BRIEF.md "Colour language"), the web editor's: colour carries
 * meaning only. A track shows the colour of the engine it plays (the drum track: its kit's kind), the status colours
 * say ok / notice / error (gfx.c C_OK C_WARN C_ERR), white is what you touch, the rest is neutral (the palette's
 * steps, the studio screens' greys). No colour per track number or per knob. One table, tools/colors.json
 * (tools/gen_colors.py -> build/gen/felucca_colors.h, RGB565). */

/* the engines by the names the UI shows (tools/colors.json "engines"); ENGINE_LIST's own names for two of them */
#define COL_ENG_FORMANT COL_ENG_VOICE
#define COL_ENG_DRAWBAR COL_ENG_WHEEL
#define ENG_COLV_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(COL_ENG_##N,)
static const uint16_t ENG_COL[NENGINES] = {ENGINE_LIST(ENG_COLV_)};   /* by slot (ENGINES[]) */

/* c at n / 8 of its brightness (the dim and mid shades of a colour: 3, 5) */
static uint16_t col_shade(uint32_t c, uint32_t n)
{
    return (uint16_t)(((((c >> 11) & 31u) * n >> 3) << 11) | ((((c >> 5) & 63u) * n >> 3) << 5) | ((c & 31u) * n >> 3));
}

/* factory kit k's kind colour (the kit UIDs: sampled, synthesised, then the X0X machines and their voices) */
static uint16_t kind_col(uint32_t k)
{
    return k < DRUM_SAMPLED ? COL_KIND_SAMPLED : k < DRUM_SYNTH_END ? COL_KIND_SYNTH : COL_KIND_X0X;
}
/* the drum track's kit: its kind's colour (the editor's KIND_ATTR: a user kit, sampled, synthesised, X0X) */
static uint16_t kit_col(void)
{
    return dl.ukit ? COL_KIND_USERKIT : kind_col(drum_kit());
}

#if DL_UI
/* drum lane l's sound: its source's kind colour (the editor's laneKind: the project's kit, your sample, a kit's sound,
 * an X0X voice) */
static uint16_t lane_col(uint32_t l)
{
    uint32_t s = dl.src[l & 15u];
    if (s == DL_KIT)
        return kit_col();
    if (s < DL_KIT0)
        return COL_KIND_USR;
    return kind_col(s - DL_KIT0);
}
#endif

/* track i's colour: its engine's, the drum track's kit kind's */
static uint16_t trk_col(uint32_t i)
{
    const track_t *t = &trk[i % NTRK];
    return is_drum(t) ? kit_col() : ENG_COL[t->eng_req % NENGINES];
}
#define TRK_MID(i) col_shade(trk_col(i), 5u)
#define TRK_DIM(i) col_shade(trk_col(i), 3u)
#define SEL_COL trk_col(song.sel)
