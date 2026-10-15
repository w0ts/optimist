/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v13: the SCOPE (ui/optimist/op_scope.c; included by editor.c; web/EDITOR_PROTOCOL.md "v13: the scope"; builds with the Optimist UI only).
 * The device's oscilloscope: one ring of the audio (fx.c vis_pcm) of one source at a time, drawn trigger-stable (the latest rising zero crossing after a swing
 * below) and scaled to its peak. This hands out the same trace: the editor asks for a source and gets the points the device would draw.
 *   101 SCOPE  src -> src, shift, peak v14, n (2 x 7 bits), n heights (0..127, 64 the zero line, up is above)
 *       src   0 MASTER, 1..4 the tracks T1 T2 T3 DR (a drum lane is DR); a bad src: no reply
 * Asking sets the ring's source as the panel's SCOPE would (op_scope.c scope_tick) for the next 2 s: ask again about every 50 ms to watch; while asked, the mixer's MASTER
 * meter on the device reads that source too. shift is the right shift that fits the peak in the heights (never amplified: a quiet source is a small trace); peak is the
 * ring's largest sample (>> 2, at most 8191). INFO tag 0x5E: version 1, n. */
enum { ED_SCOPE = 101 };
#if FELUCCA_UI == 1 && FELUCCA_ED_SCOPE
static int ed_scope(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t w, i, pk = 0, sh, at;
    if (cmd != ED_SCOPE || na < 1u || a[0] > (uint32_t)NTRK)
        return 0;
    scope_ed_src = a[0];
    scope_ed_ms = fm1_ms | 1u;
    scope_src = a[0];
    w = vis_wr;
    for (i = 0; i < VIS_RING; i++) {
        int32_t x = sc_at(w, i);
        uint32_t m = (uint32_t)(x < 0 ? -x : x);
        pk = m > pk ? m : pk;
    }
    for (sh = 0; (pk >> sh) > 60u; sh++)
        ;
    at = sc_trigger(w, (int32_t)(pk >> 3));
    ed_b(a[0]), ed_b(sh), ed_v((int32_t)(pk >> 2 < 8191u ? pk >> 2 : 8191u));
    ed_b(SC_N & 127u), ed_b(SC_N >> 7);
    for (i = 0; i < SC_N; i++)
        ed_b((uint32_t)clamp(64 - (sc_at(w, at + i * SC_DEC) >> sh), 1, 127));
    return 1;
}
static void ed_scope_info(void)
{
    ed_b(0x5E);
    ed_b(3);
    ed_b(1);
    ed_b(SC_N & 127u);
    ed_b(SC_N >> 7);
}
#else
static int ed_scope(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_scope_info(void) {}
#endif
