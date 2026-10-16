/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v14: track and drum-lane SOLO and MUTE (included by editor.c; web/EDITOR_PROTOCOL.md "v14: solo and mute"). INFO tag 0x61 = here.
 * The device's own grammar and no state of its own: a track's SOLO is a bit of song.solo (GLO + key 5..8 toggles it; every other track is silent:
 * trk_silent), a track's MUTE is P_MUTE (GLO + key 1..4, TRACK_MIX), a drum lane's SOLO and MUTE are drums/drum_mix.c's masks (dlm_set_solo,
 * dlm_set_mute: any lane soloed silences the others, a soloed lane also muted stays silent), the values DLAN (95) group 0 edits.
 *   104 SOLO  op [x, on] -> op, rc, solo, mute, silent, lsolo0..2, lmute0..2
 *       op 0 the state, 1 TRACK SOLO, 2 TRACK MUTE (x = the track 0..3), 3 LANE SOLO, 4 LANE MUTE (x = the lane 0..15); on: 0 off, 1 on, 2 toggle
 *       reply: rc 0; solo, mute, silent: bit n = track n; lsolo, lmute: the lanes as 3 x 7 bits (0..6, 7..13, 14..15). A bad op, x or on: no reply.
 *       INFO tag 0x61: version 1, flags 0. */
enum { ED_SOLO = 104 };
static void ed_known(uint32_t k, uint32_t id);   /* editor.c (defined after the includes) */
#if FELUCCA_ED_SOLO
static void ed_solo_state(uint32_t op)
{
    uint32_t i, solo = song.solo & 15u, mute = 0, silent = 0, ls = 0, lm = 0;
    for (i = 0; i < NTRK; i++) {
        mute |= (uint32_t)(trk[i].p[P_MUTE] != 0) << i;
        silent |= (uint32_t)(trk_silent(&trk[i]) != 0) << i;
    }
    for (i = 0; i < DRUM_LANES; i++) {
        ls |= (uint32_t)(dlm_soloed(i) != 0) << i;
        lm |= (uint32_t)(dlm_muted(i) != 0) << i;
    }
    ed_b(op), ed_b(0), ed_b(solo), ed_b(mute), ed_b(silent);
    ed_b(ls & 127u), ed_b((ls >> 7) & 127u), ed_b(ls >> 14);
    ed_b(lm & 127u), ed_b((lm >> 7) & 127u), ed_b(lm >> 14);
}
static int ed_solo(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, x, on;
    if (cmd != ED_SOLO || op > 4u || (op && (na < 3u || a[1] >= (op <= 2u ? (uint32_t)NTRK : (uint32_t)DRUM_LANES) || a[2] > 2u)))
        return 0;
    if (op) {
        x = a[1], on = a[2];
        if (op == 1u) {
            on = on == 2u ? !((song.solo >> x) & 1u) : on;
            song.solo = (uint8_t)(on ? song.solo | (1u << x) : song.solo & ~(1u << x));
        } else if (op == 2u) {
            on = on == 2u ? !trk[x].p[P_MUTE] : on;
            trk[x].p[P_MUTE] = (int16_t)(on != 0u);
            ed_known(x, P_MUTE);                          /* (the editor's own change: no PARAMS push for it) */
        } else if (op == 3u) {
            dlm_set_solo(x, on == 2u ? !dlm_soloed(x) : on != 0u);
        } else {
            dlm_set_mute(x, on == 2u ? !dlm_muted(x) : on != 0u);
        }
        ui.force = 1;
    }
    ed_solo_state(op);
    return 1;
}
static void ed_solo_info(void)
{
    ed_b(0x61);
    ed_b(2);
    ed_b(1);
    ed_b(0);
}
#else
static int ed_solo(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
static void ed_solo_info(void) {}
#endif
