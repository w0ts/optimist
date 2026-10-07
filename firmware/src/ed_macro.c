/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: what the performance macros make of the values (FELUCCA_MACROS; included by editor.c;
 * web/EDITOR_PROTOCOL.md "Macros"). The editor sees the authored values (a macro only moves them inside the audio
 * ISR, macro.c); this says what plays, from the same rows and arithmetic the ISR uses (mac_effective*), so the editor
 * has no table of its own to keep in step.
 *   65 MACRO  track  -> track, COLOR MOTION SPACE ENERGY (4 x v14, each -64..63, 0 = home), n, then n x one of:
 *               0, id, v14                  the track's parameter P_id plays as v14 (only those a macro moves; a drum
 *                                           track has none)
 *               1, id, v14                  the global G_id plays as v14
 *               2, 0, sc v14, add v14       every drum lane's REV send plays as clamp(send * sc / 64 + add, 0, 127),
 *                                           the send in a synth track's steps (level v of a lane is 4v + v / 8)
 *             A build without the macros does not answer: the editor shows the authored values only. */
enum { ED_MACRO = 65 };

static int ed_macro(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t id, at, n = 0, k;
    int32_t x[4], sc, add;
    if (cmd != ED_MACRO || na < 1u || a[0] >= NTRK)
        return 0;
    k = a[0];
    ed_b(k);
    (void)mac_home(x);
    for (id = 0; id < 4u; id++)
        ed_v(x[id]);
    at = ed_n;
    ed_b(0);
    for (id = 0; k < NPART && id < P_COUNT; id++) {
        int32_t e = mac_effective(k, id, trk[k].p[id]);
        if (e != trk[k].p[id]) {
            ed_b(0), ed_b(id), ed_v(e);
            n++;
        }
    }
    for (id = 0; id < G_COUNT; id++) {
        int32_t e = mac_effective_g(id, song.g[id]);
        if (e != song.g[id]) {
            ed_b(1), ed_b(id), ed_v(e);
            n++;
        }
    }
    mac_drev(&sc, &add);
    if (sc != 64 || add) {
        ed_b(2), ed_b(0), ed_v(sc), ed_v(add);
        n++;
    }
    ed_out[at] = (uint8_t)n;
    return 1;
}
