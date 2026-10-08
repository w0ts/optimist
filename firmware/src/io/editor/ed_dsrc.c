/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: what the drum lanes' SOURCE and SOUND pages offer, so the editor builds its kit editor from the
 * device (included by editor.c after ed_drums.c; web/EDITOR_PROTOCOL.md "Drum sources"). Built with the lanes'
 * pages (DL_ANY, ui_drums.c); a build without them does not answer.
 *   50 DRUM_SRCS  start                -> start, total, n, then n x (src, kind, name): the SRC list as the device
 *                                         steps through it (ui_drums.c DS_SRC_NAMES, dsnd_idx_src); kind: 0 the
 *                                         project's kit, 1 a user sample slot, 2 a sampled kit, 3 a synthesised kit,
 *                                         4 an X0X machine (its kits and voices); + 8: not in this build (a stand-in
 *                                         plays). At most ED_SRC_PAGE entries a reply: ask again from start + n
 *   51 DRUM_SHOW  lane                 -> lane, mask (2 x 7 bit: bit i = SOUND value i, TUNE..LEVEL, applies to this
 *                                         lane's source), flags (bit 0 TUNE counts steps, not semitones (an X0X
 *                                         model); bit 1 a user sample: HIT START LEN apply), the lane's name */
enum { ED_DRUM_SRCS = 50, ED_DRUM_SHOW };
#define ED_SRC_PAGE 24u

#if DL_ANY
static uint32_t ed_src_kind(uint32_t src)
{
    uint32_t k = src - DL_KIT0;
    if (src == DL_KIT)
        return 0;
    if (src < DL_KIT0)
        return 1;
    if (k >= DRUM_KITS)
        return 4;                                         /* (past the kits: the X0X voices) */
    return (k < DRUM_SAMPLED ? 2u : k < DRUM_SYNTH_END ? 3u : 4u) + (drum_kit_built(k) ? 0u : 8u);
}

static int ed_dsrc(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    if (cmd == ED_DRUM_SRCS) {
        uint32_t total = (uint32_t)DSD[8].max + 1u, i, n = 0, at;
        if (na < 1u)
            return 0;
        ed_b(a[0]);
        ed_b(total);
        at = ed_n;
        ed_b(0);
        for (i = a[0]; i < total && n < ED_SRC_PAGE; i++, n++) {
            uint32_t src = dsnd_idx_src(i);
            if (!FELUCCA_DRUM_USR && src >= DL_USR && src < DL_KIT0)
                src = 0xFFu;                              /* (the device steps over USR1..3: listed as absent) */
            ed_b(src == 0xFFu ? 127u : src);
            ed_b(src == 0xFFu ? 9u : ed_src_kind(src));
            ed_str(DS_SRC_NAMES[i], 12);
        }
        ed_out[at] = (uint8_t)n;
        return 1;
    }
    if (cmd == ED_DRUM_SHOW) {
        uint32_t id, m = 0, f = 0;
        int16_t *vp;
        if (na < 1u || a[0] >= DRUM_LANES)
            return 0;
        for (id = 0; id < DE_N; id++) {
            const param_desc_t *d = dsnd_desc_lane(a[0], id, &vp);
            if (d) {
                m |= 1u << id;
                if (id == DE_TUNE && d->fmt != F_SEMI)
                    f |= 1u;
            }
        }
        if (dl_usr_of(a[0]))
            f |= 2u;
        ed_b(a[0]);
        ed_b(m);
        ed_b(m >> 7);
        ed_b(f);
        ed_str(LANE_NAME[a[0]], 12);                     /* (its name, as the device shows it) */
        return 1;
    }
    return 0;
}
#else
#define ed_dsrc(cmd, a, na) 0
#endif
