/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of our least AVRCP (firmware/src/bt/bt_avrcp.c): the SetAbsoluteVolume command byte for byte (AVRCP 1.6
 * §6.13.2, AVCTP §6.1, AV/C), and the sink's questions as the AirPods ask them (a Raspberry Pi's capture, 2026-10-21:
 * GetCapabilities, RegisterNotification) answered, a key press accepted, the rest refused the way the spec says. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_avrcp.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t o[BAVR_MAX];
    uint32_t n;
    /* SetAbsoluteVolume 100: AVCTP label 3 command, PID 110E; AV/C CONTROL, panel, VENDOR DEPENDENT, 00 19 58, PDU 50,
     * single, length 1, volume */
    static const uint8_t setvol[] = {0x30, 0x11, 0x0E, 0x00, 0x48, 0x00, 0x00, 0x19, 0x58, 0x50, 0x00, 0x00, 0x01, 0x64};
    /* the AirPods' GetCapabilities (events), label 0: STATUS, PDU 10, length 1, capability 3 */
    static const uint8_t caps3[] = {0x00, 0x11, 0x0E, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x10, 0x00, 0x00, 0x01, 0x03};
    static const uint8_t caps3_rsp[] = {0x02, 0x11, 0x0E, 0x0C, 0x48, 0x00, 0x00, 0x19, 0x58, 0x10, 0x00, 0x00, 0x03,
                                        0x03, 0x01, 0x01};
    static const uint8_t caps2[] = {0x20, 0x11, 0x0E, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x10, 0x00, 0x00, 0x01, 0x02};
    /* RegisterNotification PLAYBACK_STATUS_CHANGED, label 1: NOTIFY, PDU 31, length 5, event 1, interval 0 */
    static const uint8_t regn[] = {0x10, 0x11, 0x0E, 0x03, 0x48, 0x00, 0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x05,
                                   0x01, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t regn_rsp[] = {0x12, 0x11, 0x0E, 0x0F, 0x48, 0x00, 0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x02,
                                       0x01, 0x01};
    static const uint8_t regn_vol[] = {0x10, 0x11, 0x0E, 0x03, 0x48, 0x00, 0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x05,
                                       0x0D, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t play[] = {0x40, 0x11, 0x0E, 0x00, 0x48, 0x7C, 0x44, 0x00};              /* PASSTHROUGH play, pressed */
    static const uint8_t play_rsp[] = {0x42, 0x11, 0x0E, 0x09, 0x48, 0x7C, 0x44, 0x00};
    static const uint8_t status[] = {0x50, 0x11, 0x0E, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x30, 0x00, 0x00, 0x00};
    static const uint8_t elem[] = {0x60, 0x11, 0x0E, 0x01, 0x48, 0x00, 0x00, 0x19, 0x58, 0x20, 0x00, 0x00, 0x00};
    static const uint8_t unit[] = {0x70, 0x11, 0x0E, 0x01, 0xFF, 0x30, 0x07, 0xFF, 0xFF, 0xFF, 0xFF};   /* UNIT INFO */
    static const uint8_t ours_rsp[] = {0x32, 0x11, 0x0E, 0x09, 0x48, 0x00, 0x00, 0x19, 0x58, 0x50, 0x00, 0x00, 0x01, 0x64};
    static const uint8_t other_pid[] = {0x00, 0x11, 0x0C, 0x01, 0x48, 0x00};

    n = bavr_set_volume(o, 3, 100);
    check("SetAbsoluteVolume 100, label 3: the command frame byte for byte", n == sizeof setvol && !memcmp(o, setvol, n));
    n = bavr_set_volume(o, 3, 200);
    check("... a volume past 127 is 127", n == 14 && o[13] == 127);
    n = bavr_in(caps3, sizeof caps3, o);
    check("GetCapabilities(events) -> STABLE: one event, PLAYBACK_STATUS_CHANGED", n == sizeof caps3_rsp && !memcmp(o, caps3_rsp, n));
    n = bavr_in(caps2, sizeof caps2, o);
    check("GetCapabilities(company ids) -> STABLE: one, the SIG's 00 19 58, label kept",
          n == 18 && o[0] == 0x22 && o[3] == 0x0C && o[12] == 5 && o[13] == 2 && o[14] == 1 && o[15] == 0 && o[16] == 0x19 && o[17] == 0x58);
    n = bavr_in(regn, sizeof regn, o);
    check("RegisterNotification(playback status) -> INTERIM: playing", n == sizeof regn_rsp && !memcmp(o, regn_rsp, n));
    n = bavr_in(regn_vol, sizeof regn_vol, o);
    check("RegisterNotification(another event) -> REJECTED, invalid parameter", n == 14 && o[3] == 0x0A && o[9] == 0x31 && o[13] == 1);
    n = bavr_in(play, sizeof play, o);
    check("PASSTHROUGH play pressed -> ACCEPTED, the key echoed", n == sizeof play_rsp && !memcmp(o, play_rsp, n));
    n = bavr_in(status, sizeof status, o);
    check("GetPlayStatus -> STABLE: length and position unknown (FF), playing",
          n == 22 && o[3] == 0x0C && o[12] == 9 && o[13] == 0xFF && o[20] == 0xFF && o[21] == 1);
    n = bavr_in(elem, sizeof elem, o);
    check("GetElementAttributes -> REJECTED, invalid command", n == 14 && o[3] == 0x0A && o[9] == 0x20 && o[13] == 0);
    n = bavr_in(unit, sizeof unit, o);
    check("UNIT INFO (not vendor dependent) -> NOT IMPLEMENTED, the operands echoed", n == sizeof unit && o[3] == 0x08 && o[5] == 0x30);
    n = bavr_in(ours_rsp, sizeof ours_rsp, o);
    check("its response to our SetAbsoluteVolume: nothing to send", n == 0);
    n = bavr_in(other_pid, sizeof other_pid, o);
    check("another profile id -> the AVCTP invalid-PID frame", n == 3 && o[0] == 0x03 && o[1] == 0x11 && o[2] == 0x0C);
    printf(fails ? "bt_avrcp_test: %d FAILED\n" : "bt_avrcp_test: all ok\n", fails);
    return fails != 0;
}
