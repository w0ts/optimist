/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Bluetooth audio's state as the screen reads it (bt_audio.c; SYSTEM > Bluetooth, op_project.c): included by felucca.c
 * before the UI, the code after the Classic stack */
#define BTA_DEVS 8u
#define BTA_SCAN_S 10u                                  /* the scan: 10.24 s of inquiry, as btinq's default */
#define BTA_PAGE_SLOTS 8192u                            /* a page: ~5 s, then given up */
#define BTA_MEMS 4u                                     /* devices remembered (the user, 2026-10-24: "up to 4") */
enum { BTA_OFF, BTA_MAN, BTA_AUTO };
enum { BTA_S_OFF, BTA_S_SCANNING, BTA_S_FOUND, BTA_S_NONE, BTA_S_PAGING, BTA_S_LINKED, BTA_S_SETUP, BTA_S_STREAMING,
       BTA_S_REFUSED, BTA_S_NO_LAST, BTA_S_FAILED, BTA_S_READY };
struct bta_dev {
    uint8_t addr[6], audio;                             /* audio: its class of device says Audio / Video (a sink, likely) */
    char name[17];
};
struct bta_mem {                                        /* a remembered device (the settings record: op_bt.c bt_rec_out ..) */
    uint8_t addr[6], bonded, lk[16];                    /* lk: its link key from its pairing (air order), bonded: lk valid */
    char name[17];
};
static struct {
    uint8_t mode, n, cur, tried;
    struct bta_dev d[BTA_DEVS];
    uint8_t own[6], own_set;                            /* our BR/EDR address: made once, kept (bt_inq.c bti_own_addr) */
    struct bta_mem mem[BTA_MEMS];                       /* the remembered devices, mem[0] the last one (bta_mem.c) */
    uint8_t nmem, page_i;                               /* how many; the one a connect pages (AUTO goes through them) */
    uint8_t key_new;                                    /* the address or a key changed: the settings record to save */
} bta;

static void bta_start(uint32_t m);
static void bta_stop(void);
static void bta_hangup(void);
static void bta_pick(uint32_t i);
static struct bta_mem *bta_mem_of(const uint8_t a[6]);
static void bta_forget(uint32_t i);
static int bta_next(void);
static void bta_settle(int up);
static uint32_t bta_rows(void);
static int bta_row(uint32_t i);
static void bta_pick_row(uint32_t i);
static int bta_page(const uint8_t a[6]);
static void bta_scan(int on);
static uint32_t bta_state(void);
static const char *bta_dev_name(uint32_t i);
static const char *bta_lost(void);
static int bta_out_bt(void);
static int bta_gave_up(void);
static int bta_rejoin(void);
static void bta_output(int bt);
