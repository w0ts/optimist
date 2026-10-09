/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with FELUCCA_BLE=1: SYSTEM > BLUETOOTH (the radio's ON / OFF, as SLOOP's HOME menu has it), and
 * the settings word's bits 21..22 HOLD, 23 CARDS, 24 BLUETOOTH, which must not collide (storage/settings_word.c).
 * Included after the helpers. The radio itself is the stub in ui_optimist_test.c (midi_ble.c is host-tested apart). */

#if FELUCCA_BLE
static void ble_sys_row(void)                           /* the BLUETOOTH row of SYSTEM, the cursor on it */
{
    uint32_t w;
    reset_ui();
    op_enter(SCR_SYSTEM);
    for (w = 0; w < SCR->rows(); w++) {
        char nm[12];
        SCR->name(w, nm);
        if (!strcmp(nm, "BLUETOOTH"))
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)w;
    ui.force = 1;
    frame();
}
static void ble_word_tests(void)
{
    uint32_t w0, w;
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
    w0 = bp23_word();
    hold_sel = 2;
    w = bp23_word();
    check((w ^ w0) == (2u << 21), "settings word: HOLD moves bits 21..22 only");
    hold_sel = 0;
    op_cards = CARDS_2X2;
    w = bp23_word();
    check((w ^ w0) == (1u << 23), "settings word: CARDS moves bit 23 only");
    op_cards = CARDS_LINE;
    ble_on = 1;
    w = bp23_word();
    check((w ^ w0) == (1u << 24), "settings word: BLUETOOTH moves bit 24 only");
    hold_sel = 2;
    op_cards = CARDS_2X2;
    w = bp23_word();
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
    bp23_from_word(w);
    check(hold_sel == 2u && op_cards == CARDS_2X2 && ble_on == 1u,
          "settings word: HOLD, CARDS and BLUETOOTH all set read back each as itself (21..22, 23, 24 do not collide)");
    bp23_from_word(w & ~(1u << 24));
    check(hold_sel == 2u && op_cards == CARDS_2X2 && ble_on == 0u, "... clearing bit 24 leaves HOLD and CARDS");
    bp23_from_word(w & ~(1u << 23));
    check(hold_sel == 2u && op_cards == CARDS_LINE && ble_on == 1u, "... clearing bit 23 leaves HOLD and BLUETOOTH");
    bp23_from_word(w & ~(3u << 21));
    check(hold_sel == 0u && op_cards == CARDS_2X2 && ble_on == 1u, "... clearing bits 21..22 leaves CARDS and BLUETOOTH");
    hold_sel = 0;
    op_cards = CARDS_LINE;
    ble_on = 0;
}
static void ble_tests(void)
{
    ble_word_tests();
    ble_sys_row();
    check(!ble_on, "SYSTEM BLUETOOTH: OFF by default");
    turn(EN_K1, 1);
    check(ble_on == 1u && ble_sets == 1u, "SYSTEM BLUETOOTH: KNOB 1 right is ON, the radio told once");
    turn(EN_K1, 1);
    check(ble_on == 1u && ble_sets == 1u, "... right again: still ON, the radio not told again");
    turn(EN_K1, -1);
    check(ble_on == 0u && ble_sets == 2u, "... left is OFF");
    tap(B_SAVE);
    check(ble_on == 1u, "SYSTEM BLUETOOTH: YES toggles");
    tap(B_SAVE);
    check(ble_on == 0u, "... and back");
    ppm("opt-system-ble");
    reset_ui();
}
#endif
