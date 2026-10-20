/* SPDX-License-Identifier: GPL-3.0-only */
/* Bluetooth audio's state as the screen reads it (bt_audio.c; SYSTEM > Bluetooth, op_project.c): included by felucca.c
 * before the UI, the code after the Classic stack */
#define BTA_DEVS 8u
#define BTA_SCAN_S 10u                                  /* the scan: 10.24 s of inquiry, as btinq's default */
#define BTA_PAGE_SLOTS 8192u                            /* a page: ~5 s, then given up */
enum { BTA_OFF, BTA_MAN, BTA_AUTO };
enum { BTA_S_OFF, BTA_S_SCANNING, BTA_S_FOUND, BTA_S_NONE, BTA_S_PAGING, BTA_S_LINKED, BTA_S_SETUP, BTA_S_STREAMING,
       BTA_S_REFUSED, BTA_S_NO_LAST, BTA_S_FAILED, BTA_S_READY };
struct bta_dev {
    uint8_t addr[6], audio;                             /* audio: its class of device says Audio / Video (a sink, likely) */
    char name[17];
};
static struct {
    uint8_t mode, n, cur, have_last, last[6], tried;
    char last_name[17];
    struct bta_dev d[BTA_DEVS];
} bta;

static void bta_start(uint32_t m);
static void bta_stop(void);
static void bta_hangup(void);
static void bta_pick(uint32_t i);
static int bta_page(const uint8_t a[6]);
static void bta_scan(int on);
static uint32_t bta_state(void);
static const char *bta_dev_name(uint32_t i);
static const char *bta_lost(void);
