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
    fav_t fav;                                     /* appended last (core/favorites.c): the marked sounds, 136 B. A record from
                                                    * before it reads whole, with none (persist_upgrade) */
#endif
} persist_t;
#define PERSIST_NO_VIEW ((int)__builtin_offsetof(persist_t, view))   /* the record's length before view */
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
    persist_t p = *pp;
#if FELUCCA_FAVORITES
    if (n < (int)sizeof p)                         /* (st_load sets the first n bytes only) */
        memset(&p.fav, 0, sizeof p.fav);
    if (n == (int)__builtin_offsetof(persist_t, fav))
        n = (int)sizeof p;                         /* a record from before the favorites: whole, none (it must not be taken */
#endif                                             /* for a shorter one below: that would drop the BLE fields or the word) */
#if FELUCCA_BLE
    if (n == (int)__builtin_offsetof(persist_t, ble_dev)) {   /* saved by a BLE build before the device store */
        memset(p.ble_dev, 0, sizeof p.ble_dev);
        n = (int)sizeof p;
    }
    if (n == (int)__builtin_offsetof(persist_t, ble_bond)) {   /* saved by a BLE build before the bond */
        memset(p.ble_bond, 0, sizeof p.ble_bond);
        memset(p.ble_dev, 0, sizeof p.ble_dev);
        n = (int)sizeof p;
    }
    if (n == (int)__builtin_offsetof(persist_t, ble_rf)) {   /* saved by a BLE build before the trim copy */
        memset(p.ble_rf, 0, sizeof p.ble_rf);
        memset(p.ble_bond, 0, sizeof p.ble_bond);
        memset(p.ble_dev, 0, sizeof p.ble_dev);
        n = (int)sizeof p;
    }
    if (n < (int)sizeof p) {                   /* saved by a build without BLE: no address yet, the rest ours */
        memset(p.ble_addr, 0, sizeof p.ble_addr);
        memset(p.ble_rf, 0, sizeof p.ble_rf);
        memset(p.ble_bond, 0, sizeof p.ble_bond);
        memset(p.ble_dev, 0, sizeof p.ble_dev);
        if (n == (int)__builtin_offsetof(persist_t, ble_addr))
            n = (int)sizeof p;
    }
#endif
#if BP23_SET
    if (n < (int)sizeof p) {                   /* a shorter record (an older build, one with fewer switches) */
        if (n < PERSIST_NO_VIEW + 4)
            p.view = 1;                        /* saved before VIEW (or PER2): the overview, as a fresh device */
        if (n < PERSIST_NO_BRIGHT + 4)
            p.bright = 0;                      /* saved without BRIGHT: full */
        p.bp23 = 0;                            /* without the SLOOP 2.3 settings: as before */
#if FELUCCA_SL24_SAFE
        if (n == PERSIST_NO_VIEW + 4 && p.magic == PERSIST_MAGIC && p.view > 1u)
            p.bp23 = sl_word_in(p.view);       /* SLOOP 2.3 / 2.4's record: their lights word, NOTES inverted (sl24_guard.c) */
#endif
        if (n > PERSIST_NO_VIEW && p.magic == PERSIST_MAGIC)
            n = (int)sizeof p;                 /* (the rest as ours) */
    }
#if FELUCCA_SL24_SAFE
    else if (p.bright == SL25_DSU_MAGIC && p.magic == PERSIST_MAGIC) {   /* written back by a build that took SLOOP */
        p.bright = 0;                          /* 2.5's kits as ours: their header as BRIGHT and the settings */
        p.bp23 = p.view > 1u ? sl_word_in(p.view) : 0u;   /* word; the word as 2.4's record gives it */
    }
#endif
#else
#if FELUCCA_BRIGHT
    if (n < (int)sizeof p)
        p.bright = 0;                          /* saved without BRIGHT: full */
    if (n == PERSIST_NO_BRIGHT)
        n = (int)sizeof p;                     /* (a build without it: the rest as ours) */
#endif
    if (n < (int)sizeof p)
        p.view = 1;                            /* saved before VIEW (or PER2): the overview, as a fresh device */
#endif
    *pp = p;
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
