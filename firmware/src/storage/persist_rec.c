/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings record (one flash object, OBJ_SETTINGS; project.c writes it in settings_save and reads it in persist_boot,
 * tests/favorites_test.c reads it too): its layout, and how a record an older build wrote is brought to this build's.
 *
 * Every field after `view` was appended: a record written before it is shorter, and a build that has the field reads the
 * rest as saved (the missing tail as 0). Ours may only grow at the end. The favorites (FELUCCA_FAVORITES, core/favorites.c)
 * are the last field: a record of any older length reads whole, with none; a build without them reads ours as its prefix
 * (it saves it back shorter, so it forgets them). Included by project.c after sl24_guard.c (sl_word_in). */
#include "../core/favorites.c"
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_rec_t arrangement;                         /* (16 parts; a longer song: the log, paired by its tag) */
#endif
    uint32_t view;                                 /* (appended: a shorter record, saved before it, reads as ALL) */
#if FELUCCA_BRIGHT || BP23_SET
    uint32_t bright;                               /* appended (bright.c): 0 = full, now always written 0 and never
                                                    * applied (a saved dim level froze an FM-1 at every boot: bright.c). A
                                                    * build with BP23_SET and no BRIGHT keeps it as read (its place) */
#endif
#if BP23_SET
    uint32_t bp23;                                 /* appended: the SLOOP 2.3 settings of the FM-1 (bp23_word) */
#endif
#if FELUCCA_BLE
    uint8_t ble_addr[8];                           /* appended (midi_ble.c): the random static BLE address made once,
                                                    * [6] its mark. Last, so every other build reads the rest as its own */
    uint8_t ble_rf[BLE_RF_COPY_SIZE];              /* appended after it: the copy of the radio's stored trims (VM 106,
                                                    * 107, 108, 187; ble/ble_vm.c, its own mark and CRC) */
    uint8_t ble_bond[BLE_BOND_SIZE];               /* appended after it: a bonded central's key (midi_ble.c; zero
                                                    * without SMP) */
    uint8_t ble_dev[BLE_DEV_STORE_SIZE];           /* appended after it: the DEVICES list's LAST and choice (ble/
                                                    * ble_store.h, io/midi/ble_devices.c; zero: nothing remembered) */
#endif
#if FELUCCA_FAVORITES
    fav_t fav;                                     /* appended (core/favorites.c): the marked sounds, 136 B. A record from
                                                    * before it reads whole, with none (persist_upgrade) */
#endif
#if FELUCCA_USB_AUDIO
    uint8_t ua_src[UA_ROUTE_N];                    /* appended last (io/usb/ua_tap.h): the USB capture channels' sources */
    uint32_t ua_mark;                              /* PERSIST_UA_MARK: they are set (else, or another build's layout: the
                                                    * defaults, T1 T2 T3 DRUMS) */
#endif
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    dualcfg_t dualcfg;                             /* the second-core test image only (system/dual.c, the console's dualcfg):
                                                    * after everything; a normal build reads it as a longer record's tail */
#endif
} persist_t;
#define PERSIST_UA_MARK 0x32524155u                /* "UAR2": the list without -3 / -6 dB (ua_tap.h) */
#define PERSIST_UA_MARK1 0x31524155u               /* "UAR1": the list with them (feat/usb-out-routing 1074f2ee) */
#define PERSIST_NO_VIEW ((int)__builtin_offsetof(persist_t, view))   /* the record's length before view */
#if FELUCCA_BLE
#define PERSIST_BLE(f) ((int)__builtin_offsetof(persist_t, f))
#define PERSIST_BLE_END (PERSIST_BLE(ble_dev) + (int)sizeof(((persist_t *)0)->ble_dev))   /* (the BLE fields: bytes, in a row) */
_Static_assert(PERSIST_BLE(ble_rf) == PERSIST_BLE(ble_addr) + 8 && PERSIST_BLE(ble_bond) == PERSIST_BLE(ble_rf) + BLE_RF_COPY_SIZE &&
               PERSIST_BLE(ble_dev) == PERSIST_BLE(ble_bond) + BLE_BOND_SIZE, "the settings record's BLE fields in a row");
#endif
#if FELUCCA_BRIGHT || BP23_SET
#define PERSIST_NO_BRIGHT ((int)__builtin_offsetof(persist_t, bright))   /* .. before bright (a build without it) */
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif

/* the n bytes st_load (or sl25_set_load) gave, in *pp (the rest of it not set): brought to this build's record. Returns
 * the length to take it as: sizeof(persist_t) for a whole record (anything shorter that is complete for an older build is
 * filled in and made whole), PERSIST_NO_VIEW for the one short form still read, else what n was (not read) */
static int persist_upgrade(persist_t *pp, int n)
{
#if FELUCCA_USB_AUDIO
    if (n < (int)sizeof *pp)                         /* (first: a record from before the USB routing is whole without it) */
        memset(pp->ua_src, 0, sizeof pp->ua_src), pp->ua_mark = 0;
    if (n == (int)__builtin_offsetof(persist_t, ua_src))
        n = (int)sizeof *pp;
#endif
#if FELUCCA_FAVORITES
    if (n < (int)sizeof *pp)                         /* (st_load sets the first n bytes only) */
        memset(&pp->fav, 0, sizeof pp->fav);
    if (n == (int)__builtin_offsetof(persist_t, fav))
        n = (int)sizeof *pp;                         /* a record from before the favorites: whole, none (it must not be taken */
#endif                                             /* for a shorter one below: that would drop the BLE fields or the word) */
#if FELUCCA_BLE
    if (n == PERSIST_BLE(ble_dev) || n == PERSIST_BLE(ble_bond) || n == PERSIST_BLE(ble_rf)) {   /* saved by a BLE build
                                                    * before the device store, the bond or the trim copy: those 0 */
        memset((uint8_t *)pp + n, 0, (uint32_t)(PERSIST_BLE_END - n));
        n = (int)sizeof *pp;
    }
    if (n < (int)sizeof *pp) {                   /* saved by a build without BLE: no address yet, the rest ours */
        memset(pp->ble_addr, 0, (uint32_t)(PERSIST_BLE_END - PERSIST_BLE(ble_addr)));
        if (n == PERSIST_BLE(ble_addr))
            n = (int)sizeof *pp;
    }
#endif
#if BP23_SET
    if (n < (int)sizeof *pp) {                   /* a shorter record (an older build, one with fewer switches) */
        if (n < PERSIST_NO_VIEW + 4)
            pp->view = 1;                        /* saved before VIEW (or PER2): the overview, as a fresh device */
        if (n < PERSIST_NO_BRIGHT + 4)
            pp->bright = 0;                      /* saved without BRIGHT: full */
        pp->bp23 = 0;                            /* without the SLOOP 2.3 settings: as before */
#if FELUCCA_SL24_SAFE
        if (n == PERSIST_NO_VIEW + 4 && pp->magic == PERSIST_MAGIC && pp->view > 1u)
            pp->bp23 = sl_word_in(pp->view);       /* SLOOP 2.3 / 2.4's record: their lights word, NOTES inverted (sl24_guard.c) */
#endif
        if (n > PERSIST_NO_VIEW && pp->magic == PERSIST_MAGIC)
            n = (int)sizeof *pp;                 /* (the rest as ours) */
    }
#if FELUCCA_SL24_SAFE
    else if (pp->bright == SL25_DSU_MAGIC && pp->magic == PERSIST_MAGIC) {   /* written back by a build that took SLOOP */
        pp->bright = 0;                          /* 2.5's kits as ours: their header as BRIGHT and the settings */
        pp->bp23 = pp->view > 1u ? sl_word_in(pp->view) : 0u;   /* word; the word as 2.4's record gives it */
    }
#endif
#else
#if FELUCCA_BRIGHT
    if (n < (int)sizeof *pp)
        pp->bright = 0;                          /* saved without BRIGHT: full */
    if (n == PERSIST_NO_BRIGHT)
        n = (int)sizeof *pp;                     /* (a build without it: the rest as ours) */
#endif
    if (n < (int)sizeof *pp)
        pp->view = 1;                            /* saved before VIEW (or PER2): the overview, as a fresh device */
#endif
    return n;
}

#if FELUCCA_FAVORITES
/* the favorites of a record read (persist_boot) into the store, and out of it (settings_save) */
static void persist_fav_load(const persist_t *p)
{
    if (p->fav.mark != FAV_MARK) {                 /* none saved, or a record whose layout is another build's (BLE) here */
        memset(&fav, 0, sizeof fav);
        return;
    }
    fav = p->fav;
    fav_tidy(&fav);
}
static void persist_fav_save(persist_t *p)
{
    p->fav = fav;
    p->fav.mark = FAV_MARK;
}
#endif

#if FELUCCA_USB_AUDIO
/* the USB capture channels' sources of a record read (persist_boot; none, or out of range: the defaults), and out (settings_save) */
static void persist_ua_load(const persist_t *p)
{
    uint32_t c;
    /* UAR1's indices: OFF ML MR M-3 M-6 T1 T2 T3 DRUMS BL BR B-3 B-6 DR1.. -> today's (each sum: L+R) */
    static const uint8_t V1[13] = {UAS_OFF, UAS_MAIN_L, UAS_MAIN_R, UAS_MAIN_S, UAS_MAIN_S, UAS_T1, UAS_T2, UAS_T3,
                                   UAS_DRUMS, UAS_DBUS_L, UAS_DBUS_R, UAS_DBUS_S, UAS_DBUS_S};
    ua_route_default();
    if (p->ua_mark != PERSIST_UA_MARK && p->ua_mark != PERSIST_UA_MARK1)
        return;
    for (c = 0; c < UA_ROUTE_N; c++) {
        uint32_t v = p->ua_src[c];
        if (p->ua_mark == PERSIST_UA_MARK1)
            v = v < 13u ? V1[v] : v - 13u < DRUM_LANES ? UAS_DR1 + (v - 13u) : 0xFFu;
        if (v < UAS_N)
            ua_src[c] = (uint8_t)v;
    }
    ua_route_apply();
}
static void persist_ua_save(persist_t *p)
{
    memcpy(p->ua_src, ua_src, sizeof p->ua_src);
    p->ua_mark = PERSIST_UA_MARK;
}
#endif
