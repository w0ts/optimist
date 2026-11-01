/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, PLAY held > TEMPO > NUDGE: following a clock (MIDI USB / TRS, Ableton Link: CLK OFS; Bluetooth MIDI: BT OFS) it is that
 * offset in ms, edited by its knob and OCT- / +, marked for the settings record (clk_ofs: project.c persist_ofs_*); on the internal clock it is the real-time nudge, nothing saved. */
static void syncnudge_tests(void)
{
#if FELUCCA_MIDI_CLOCK
    static const uint8_t SRC[3] = {MSRC_USB, MSRC_LINK, MSRC_BLE};
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        uint32_t ix = SRC[i] == MSRC_BLE ? 1u : FELUCCA_LINK && SRC[i] == MSRC_LINK ? 2u : 0u;
        const char *want = ix == 1u ? "BT OFS" : ix == 2u ? "LINK OFS" : "CLK OFS";
        cell_t c;
        int16_t o0;
        reset_ui();
        hold_sel = 0;                                   /* (350 ms: the walk of SYSTEM before may leave CALIBRATE > HOLD where its detents ended, PLAY is held 30 frames here) */
        sync_ofs[0] = sync_ofs[1] = sync_ofs[2] = 0;
        sy.mode = (uint8_t)song.g[G_SYNC];
        sy.src = SRC[i];                                /* (following: a running transport keeps the source, clock_sync.c sync_select) */
        song.playing = 1;
        settings_later = 0;
        clk_jog = 0;
        press(B_PLAY);
        frames(30);
        check(ui.scr == SCR_TEMPO, "synced NUDGE: PLAY held, the TEMPO page");
        tp_cell(0, 1, &c);
        check(c.label && !strcmp(c.label, want) && c.vp == &sync_ofs[ix], "synced: the NUDGE card is the clock's offset (CLK OFS; BT OFS for Bluetooth)");
        settings_later = 0;
        tp_turn(TPR_TEMPO, 1, 3, 0);                    /* (the knob's handler: the frame after would take settings_later) */
        o0 = sync_ofs[ix];
        check(o0 > 0 && sync_ofs[ix == 0u ? 1 : 0] == 0 && settings_later && clk_jog == 0, "synced: the knob moves the offset in ms, marked for the settings save, no real-time jog");
        press(B_OCTUP);
        frames(30);
        release(B_OCTUP);
        check(sync_ofs[ix] > o0 && clk_nudge == 0, "synced: OCT+ held raises the offset, the clock's tempo is not nudged");
        release(B_PLAY);
        frames(2);
        song.playing = 0;
    }
    reset_ui();
    hold_sel = 0;
    sync_ofs[0] = sync_ofs[1] = sync_ofs[2] = 0;
    sy.src = 0;
    settings_later = 0;
    clk_jog = 0;
    press(B_PLAY);
    frames(30);
    tp_turn(TPR_TEMPO, 1, 2, 0);
    check(tp.jog == 2 && sync_ofs[0] == 0 && sync_ofs[1] == 0, "internal clock: NUDGE is the real-time jog, no offset");
    release(B_PLAY);
    frames(2);
    clk_jog = 0;
    reset_ui();
#endif
}
