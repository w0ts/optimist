/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's name to other devices, "FM1-XXXX" (devname.h): the flash part's unique ID read once at boot (persist_boot, the
 * rescue's start), the name asked of by usb.c each time a host reads its strings. Part of the unity build, after the flash
 * driver (felucca.c) and before the BLE code. */
#include "devname.h"

static uint8_t dev_uid[16];                    /* the flash part's factory unique ID, once read */
static uint8_t dev_uid_ok;
#define DEVNAME_UID 1                          /* (sd_mode.c: a user preset's id carries the ID's hash) */

#if FELUCCA_FLASH
/* the ID, with the interrupts off like fl_jedec_ram's read beside it (the far code of hal/fm1_flash.h; FL_UID) */
static void dev_uid_read(void)
{
    uint32_t f = irq_save();
    dev_uid_ok = (uint8_t)(FL_FAR(fl_uid_ram)(dev_uid) == 0);
    irq_restore(f);
}
#else
static void dev_uid_read(void) {}
#endif

static int dev_uid_id(void)                    /* the ID's digits, -1 none */
{
    return dev_uid_ok ? dev_uid_hash16(dev_uid) : -1;
}

/* the digits: the kept BLE address's when there is one (FELUCCA_BLE; the settings are read before USB starts), else the ID's */
static int dev_id(void)
{
#if FELUCCA_BLE
    return dev_id_pick(ble_addr_kept, dev_uid_id());
#else
    return dev_id_pick(0, dev_uid_id());
#endif
}

static const char *dev_name(void)              /* "FM1-XXXX" */
{
    static char name[DEV_NAME_LEN + 1u];
    dev_name_fmt(name, dev_id());
    return name;
}
