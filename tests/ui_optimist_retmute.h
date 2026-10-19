/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, 2026-10-16: the returns' MUTE on the mixer's MASTER row (SHIFT = OCT- held, + the RETURNS card's knob, once per hold; the card struck
 * through; a turned level unmutes it; fx.c fx_rmute) and the editor's SOLO op 7 on the Optimist UI (the same bits the gesture sets; the editor's REC,
 * cmd 107, is gone since 2026-10-19). Included after ui_optimist_edv14.h and ui_optimist_solo.h. */
static void ed_known(uint32_t k, uint32_t id) { (void)k, (void)id; }
#include "../firmware/src/io/editor/ed_solo.c"
static int rm_find_ret_card(uint32_t *k, mitem_t *m)   /* the MASTER row's RETURNS set shown, a card of it */
{
    uint32_t s, c;
    for (s = 0; s < mx_sets(MXR_MASTER); s++) {
        mx.set = (uint8_t)s;
        for (c = 0; c < 4u; c++)
            if (mx_item(MXR_MASTER, c, m) && m->kind == MI_RET) {
                *k = c;
                return 1;
            }
    }
    return 0;
}
static void retmute_tests(void)
{
    mitem_t m;
    cell_t c;
    uint32_t k = 0, ln, bit;
    int16_t lv;
    fx_rmute = 0;
    reset_ui();
    frames(2);
    ui.row[SCR_HOME] = MXR_MASTER;
    frames(2);
    if (!rm_find_ret_card(&k, &m)) {
        check(0, "retmute: the MASTER row has a RETURNS set in this build");
        return;
    }
    ln = (uint32_t)(m.id - G_CRET) % 3u, bit = 1u << ln;
    lv = song.g[m.id];
    press(B_OCTDN);
    turn(EN_K1 + k, 1);
    turn(EN_K1 + k, 1);
    release(B_OCTDN);
    check(fx_rmute == bit && song.g[m.id] == lv, "retmute: SHIFT + a RETURNS knob: that line's return muted (once per hold, two turns), its level kept");
    mix_cell(MXR_MASTER, k, &c);
    check(c.strike, "retmute: the muted return's card is struck through");
    press(B_OCTDN);
    turn(EN_K1 + k, 1);
    release(B_OCTDN);
    check(fx_rmute == 0, "retmute: ... the next hold: unmuted");
    fx_rmute = bit;
    song.g[m.id] = 100;
    turn(EN_K1 + k, -1);
    check(fx_rmute == 0 && song.g[m.id] < 100, "retmute: a muted return's level turned: unmuted, the level moves");
    song.g[m.id] = lv;
    fx_rmute = 0;
#if FELUCCA_ED_SOLO
    {
        uint32_t r = 0, i;
        uint8_t q[3] = {7, 0, 1};
        for (i = 0; i < 3u; i++)                        /* (the return A B C that holds this line) */
            if (ed_solo_rline(i) == ln)
                r = i;
        q[1] = (uint8_t)r;
        ed_begin(ED_SOLO);
        check(ed_solo(ED_SOLO, q, 3) && fx_rmute == bit && ed_out[17] == (1u << r), "retmute: the editor's SOLO 7 sets the bit the gesture sets, the reply's ret mask in return order");
        mix_cell(MXR_MASTER, k, &c);
        check(c.strike, "retmute: ... and the card is struck through");
        fx_rmute = 0;
    }
#endif
}
