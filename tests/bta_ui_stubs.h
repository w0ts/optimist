/* SPDX-License-Identifier: GPL-3.0-only */
/* Bluetooth audio's stand-in for the host UI tests (tests/ui_optimist_test.c with FELUCCA_BT_AUDIO=1): the state the screen
 * reads (firmware/src/bt/bt_audio.h, as felucca.c includes it before the UI) and the calls bt_audio.c makes, recorded.
 * The test moves the link's state (btafk.st). Included after ble_ui_stubs.h, before the UI. */
#ifndef TEST_BTA_UI_STUBS_H
#define TEST_BTA_UI_STUBS_H
#if FELUCCA_BT_AUDIO
#include "../firmware/src/ble/ble_util.h"
#include "../firmware/src/bt/bt_audio.h"
static struct {
    uint8_t st;                                         /* BTA_S_* while a mode is on (OFF: BTA_S_OFF whatever this is) */
    uint8_t out_bt, lost, gave_up;
    uint32_t starts, stops, hangups, pages, picks, scans, scan_on, rescans;
} btafk = {.st = BTA_S_NONE, .out_bt = 1};
static void bta_start(uint32_t m) { bta.mode = (uint8_t)m, bta.tried = 0, btafk.starts++; }
static void bta_stop(void) { bta.mode = BTA_OFF, btafk.stops++; }
static void bta_hangup(void) { btafk.hangups++, btafk.st = BTA_S_FAILED; }
static uint8_t btafk_paged[6];                         /* the address the last page went to */
static int bta_page(const uint8_t a[6]) { ble_cpy(btafk_paged, a, 6); btafk.pages++, btafk.st = BTA_S_PAGING; bta.tried = 1; return 1; }
#include "../firmware/src/bt/bta_mem.c"                 /* (the remembered devices: the firmware's own list code) */
static void bta_pick(uint32_t i)
{
    if (i >= bta.n)
        return;
    bta_remember(bta.d[i].addr, bta.d[i].name);
    btafk.picks++;
    (void)bta_page(bta.d[i].addr);
}
static void bta_pick_row(uint32_t i)                    /* (as bt_audio.c's) */
{
    int r = bta_row(i);
    if (r >= 0) {
        bta_front((uint32_t)r);
        btafk.picks++;
        (void)bta_page(bta.mem[0].addr);
    } else if (r > -1 - (int)BTA_DEVS)
        bta_pick((uint32_t)(-1 - r));
}
static void bta_scan(int on)                            /* (2: again, the list kept: bt_audio.c) */
{
    btafk.scans += on != 0, btafk.scan_on = (uint32_t)(on != 0);
    btafk.rescans += on == 2;
}
static uint32_t bta_state(void) { return bta.mode == BTA_OFF ? BTA_S_OFF : btafk.st; }
static const char *bta_dev_name(uint32_t i) { return i < bta.n ? bta.d[i].name : ""; }
static const char *bta_lost(void)
{
    if (!btafk.lost)
        return 0;
    btafk.lost = 0;
    return "LINK LOST";
}
static int bta_out_bt(void) { return btafk.out_bt; }
static int bta_gave_up(void) { return btafk.gave_up; }
static int32_t bta_lat_t(void) { return -1; }     /* (bt_acl.c: the stream's own delay, clock_sync.c; -1 not streaming) */
static int bta_rejoin(void) { return 0; }
static void bta_output(int bt) { btafk.out_bt = (uint8_t)(bt != 0); }
static void bta_fake_dev(uint32_t i, uint8_t a0, const char *name)   /* the inquiry heard device i */
{
    memset(&bta.d[i], 0, sizeof bta.d[i]);
    bta.d[i].addr[0] = a0, bta.d[i].addr[5] = 0x5A, bta.d[i].audio = 1;
    str_cpy(bta.d[i].name, name, sizeof bta.d[i].name);
    if (bta.n <= i)
        bta.n = (uint8_t)(i + 1u);
}
#endif
#endif
