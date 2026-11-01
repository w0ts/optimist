/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, a DX7 voice received over SysEx (issue #133, 2026-10-31): fm6_service set ui.force for each voice
 * or bank, a whole-screen black fill and redraw each (30 voices, 30 flashes). Now only what shows the voice draws again:
 * no fill of the whole screen, the page shows the new voice. Included after fm6_tests (fm6_slot) */

#if OP_FM6 && FELUCCA_FM6_SYSEX
#include "../firmware/src/engines/fm6/fm6_sysex.c"
static uint32_t screen_hash(void)
{
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 240u * 240u; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static void fm6rx_send(uint32_t salt)                    /* one VCED frame through the USB byte path, the main loop's service, one UI frame */
{
    uint8_t m[163];
    uint32_t i;
    m[0] = 0xF0, m[1] = 0x43, m[2] = 0, m[3] = 0x00, m[4] = 0x01, m[5] = 0x1B;
    for (i = 0; i < 155u; i++)
        m[6 + i] = (uint8_t)((i * 7u + salt * 3u) % 100u);
    m[6 + 145] = (uint8_t)('A' + salt % 26u);            /* the name's first letter (VCED 145..154) */
    m[161] = fm6_chk(m + 6, 155);
    m[162] = 0xF7;
    for (i = 0; i < 163u; i++)
        fm6_sx_byte(m[i]);
    fm6_service();
    frame();
}
static void fm6rx_tests(void)
{
    uint32_t e = fm6_slot(), keep = trk[0].eng_req, n, k, h;
    if (e >= NENGINES)
        return;
    reset_ui();
    song.sel = 0;
    set_engine_of(&trk[0], e);
    frame();
    tap(B_ENV);
    frames(4);
    h = screen_hash();
    n = full_fills;
    for (k = 0; k < 30u; k++)
        fm6rx_send(k + 1u);
    check(full_fills == n, "#133 30 DX7 voices received on the FM6 page: no fill of the whole screen (before: 30)");
    check(screen_hash() != h && !ui.force && !ui.redraw, "... the page shows the last voice, nothing left asked");
    n = full_fills;
    for (k = 0; k < 4u; k++) {                           /* on the other pages the voice may be shown too (the cards, the header): still no fill */
        op_enter(k == 0 ? SCR_HOME : k == 1 ? SCR_FX : k == 2 ? SCR_SCOPE : SCR_TEMPO);
        frames(3);
        n = full_fills;
        fm6rx_send(40u + k);
        frames(2);
        if (full_fills != n)
            break;
    }
    check(k == 4u, "... and on HOME, FX, SCOPE, TEMPO");
    set_engine_of(&trk[0], keep);
    frame();
}
#else
static void fm6rx_tests(void) {}
#endif
