/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings record (one flash object, OBJ_SETTINGS; project.c writes it in settings_save and reads it in persist_boot,
 * tests/favorites_test.c reads it too): its layout, and how a record an older build wrote is brought to this build's.
 *
 * Every field after `view` was appended: a record written before it is shorter, and a build that has the field reads the
 * rest as saved (the missing tail as 0). Ours may only grow at the end. The favorites (FELUCCA_FAVORITES, core/favorites.c)
 * are the last field: a record of any older length reads whole, with none; a build without them reads ours as its prefix
 * (it saves it back shorter, so it forgets them). Included by project.c after sl24_guard.c (sl_word_in). */
#include "../core/favorites.c"
#define PERSIST_UA_SRC 24u                         /* the sources' room in the record: every one ua_tap.h has, and more */
#if FELUCCA_USB_AUDIO
_Static_assert(UAS_N <= PERSIST_UA_SRC, "the record holds every USB source");
#endif
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
    uint8_t ua_src[UA_ROUTE_N];                    /* appended (io/usb/ua_tap.h): the USB capture channels' sources, until
                                                    * 2026-10-17 (UAR2, UAR1: read, converted; written 0) */
    uint32_t ua_mark;                              /* PERSIST_UA_MARK: they are set (else, or another build's layout: the
                                                    * defaults, T1 T2 T3 DRUMS) */
    uint8_t ua_dst[PERSIST_UA_SRC];                /* appended after them: each source's channels (ua_tap.h ua_dst, UAD_*) */
    uint32_t ua_mark3;                             /* PERSIST_UA_MARK3: they are set */
#endif
#if FELUCCA_BT_UI
    uint8_t bt_rec[32];                            /* appended after them: SYSTEM > Bluetooth's MODE and AUDIO's last device
                                                    * (ui/optimist/op_bt.c bt_rec_out, its own mark; zero: none) */
#endif
#if FELUCCA_BT_AUDIO
    uint8_t bt_key[32];                            /* appended after it: our BR/EDR address and the last audio device's link
                                                    * key (op_bt.c bt_key_out, its own mark; zero: none, a new address) */
#endif
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    dualcfg_t dualcfg;                             /* the second-core test image only (system/dual.c, the console's dualcfg):
                                                    * after everything; a normal build reads it as a longer record's tail */
#endif
} persist_v3_t;                                    /* ("PER3": the layout of before 2026-10-20, its fields by the build's
                                                    * switches; read once and turned into persist_t, below) */
#define PERSIST_UA_MARK3 0x33524155u               /* "UAR3": each source's channels (ua_tap.h ua_dst) */
#define PERSIST_UA_MARK 0x32524155u                /* "UAR2": each channel's source, the list without -3 / -6 dB */
#define PERSIST_UA_MARK1 0x31524155u               /* "UAR1": the list with them (feat/usb-out-routing 1074f2ee) */
#define PERSIST_NO_VIEW ((int)__builtin_offsetof(persist_v3_t, view))   /* the record's length before view */
#if FELUCCA_BLE
#define PERSIST3_BLE(f) ((int)__builtin_offsetof(persist_v3_t, f))
#define PERSIST3_BLE_END (PERSIST3_BLE(ble_dev) + (int)sizeof(((persist_v3_t *)0)->ble_dev))   /* (the BLE fields: bytes, in a row) */
_Static_assert(PERSIST3_BLE(ble_rf) == PERSIST3_BLE(ble_addr) + 8 && PERSIST3_BLE(ble_bond) == PERSIST3_BLE(ble_rf) + BLE_RF_COPY_SIZE &&
               PERSIST3_BLE(ble_dev) == PERSIST3_BLE(ble_bond) + BLE_BOND_SIZE, "the settings record's BLE fields in a row");
#endif
#if FELUCCA_BRIGHT || BP23_SET
#define PERSIST_NO_BRIGHT ((int)__builtin_offsetof(persist_v3_t, bright))   /* .. before bright (a build without it) */
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif

/* the n bytes st_load (or sl25_set_load) gave, in *pp (the rest of it not set): brought to this build's record. Returns
 * the length to take it as: sizeof(persist_t) for a whole record (anything shorter that is complete for an older build is
 * filled in and made whole), PERSIST_NO_VIEW for the one short form still read, else what n was (not read) */
static int persist_upgrade(persist_v3_t *pp, int n)
{
#if FELUCCA_BT_AUDIO
    if (n == (int)__builtin_offsetof(persist_v3_t, bt_key)) {   /* (first: a record from before the key is whole without it) */
        memset(pp->bt_key, 0, sizeof pp->bt_key);
        n = (int)sizeof *pp;
    } else if (n < (int)sizeof *pp)
        memset(pp->bt_key, 0, sizeof pp->bt_key);
#endif
#if FELUCCA_BT_UI
    if (n == (int)__builtin_offsetof(persist_v3_t, bt_rec)) {   /* (first: a record from before it is whole without it) */
        memset(pp->bt_rec, 0, sizeof pp->bt_rec);
        n = (int)sizeof *pp;
    } else if (n < (int)sizeof *pp)
        memset(pp->bt_rec, 0, sizeof pp->bt_rec);
#endif
#if FELUCCA_USB_AUDIO
    if (n < (int)__builtin_offsetof(persist_v3_t, ua_dst))   /* (first: a record from before the USB routing is whole without it, */
        memset(pp->ua_src, 0, sizeof pp->ua_src), pp->ua_mark = 0;
    if (n < (int)sizeof *pp)                         /* one from before the routing by source keeps its channels' sources) */
        memset(pp->ua_dst, 0, sizeof pp->ua_dst), pp->ua_mark3 = 0;
    if (n == (int)__builtin_offsetof(persist_v3_t, ua_src) || n == (int)__builtin_offsetof(persist_v3_t, ua_dst))
        n = (int)sizeof *pp;
#endif
#if FELUCCA_FAVORITES
    if (n < (int)sizeof *pp)                         /* (st_load sets the first n bytes only) */
        memset(&pp->fav, 0, sizeof pp->fav);
    if (n == (int)__builtin_offsetof(persist_v3_t, fav))
        n = (int)sizeof *pp;                         /* a record from before the favorites: whole, none (it must not be taken */
#endif                                             /* for a shorter one below: that would drop the BLE fields or the word) */
#if FELUCCA_BLE
    if (n == PERSIST3_BLE(ble_dev) || n == PERSIST3_BLE(ble_bond) || n == PERSIST3_BLE(ble_rf)) {   /* saved by a BLE build
                                                    * before the device store, the bond or the trim copy: those 0 */
        memset((uint8_t *)pp + n, 0, (uint32_t)(PERSIST3_BLE_END - n));
        n = (int)sizeof *pp;
    }
    if (n < (int)sizeof *pp) {                   /* saved by a build without BLE: no address yet, the rest ours */
        memset(pp->ble_addr, 0, (uint32_t)(PERSIST3_BLE_END - PERSIST3_BLE(ble_addr)));
        if (n == PERSIST3_BLE(ble_addr))
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

/* "PER4", 2026-10-20: one layout for every build. Each field is there whatever the build's switches, at its place, its
 * size fixed here (and checked against the code's own when the feature is built): a build without a feature keeps those
 * bytes as it read them (settings_save starts from the record read), so switching USB audio, the console, BLE or
 * Bluetooth audio in the builder no longer loses the settings. Before, the layout followed the switches: a record of
 * another build was read shifted (longer) or had its settings word cleared (shorter): SERIAL, KNOB COLORS and the rest
 * went off "by themselves" (the user, 2026-10-20). New fields: appended at the end only. */
#define PERSIST_MAGIC4 0x34524550u                 /* "PER4" */
#define PERSIST4_BLE_RF 100u                       /* ble/ble_vm.h BLE_RF_COPY_SIZE */
#define PERSIST4_BLE_BOND 28u                      /* io/midi/midi_ble.c BLE_BOND_SIZE */
#define PERSIST4_BLE_DEV 70u                       /* ble/ble_store.h BLE_DEV_STORE_SIZE */
#define PERSIST4_UA_ROUTE 4u                       /* io/usb/ua_tap.h UA_ROUTE_N */
#define PERSIST4_FAV 136u                          /* core/favorites.c fav_t (its bytes: the type is the build's) */
#define PERSIST4_BT_KEPT 120u                      /* ui/optimist/op_bt.c BT_KEPT_SIZE: 3 devices x 40 */
#define PERSIST4_BLE_KEPT 198u                     /* io/midi/ble_devices.c ble_kept: 3 devices x 66 (struct ble_dev) */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_rec_t arrangement;
#endif
    uint32_t view, bright, bp23;                   /* (bright: always 0; bp23: the settings word, bp23_word) */
    uint8_t ble_addr[8], ble_rf[PERSIST4_BLE_RF], ble_bond[PERSIST4_BLE_BOND], ble_dev[PERSIST4_BLE_DEV];   /* (in a row) */
    uint8_t fav[PERSIST4_FAV];                     /* (a fav_t when FAVORITES is built: persist_fav_load / _save) */
    uint8_t ua_src[PERSIST4_UA_ROUTE];
    uint8_t pad[2];                                /* (ua_mark on 4 bytes, written down: nothing left to the compiler) */
    uint32_t ua_mark;
    uint8_t ua_dst[PERSIST_UA_SRC];
    uint32_t ua_mark3;
    uint8_t bt_rec[32], bt_key[32];
    uint8_t open_proj, mix_big, rsv_pj[2];         /* 2026-10-22: the open project (PATTERNS: sections.c); a record from before: 0;
                                                    * 2026-10-25 mix_big: MIXER_2X "both", the mixer's face (op_mixer.c mx_big,
                                                    * 1 big), a record from before: 0, small */
    int16_t clk_ofs[2];                            /* 2026-10-24: GLOBAL > MIDI CLK OFS, BT OFS, ms (clock_sync.c sync_ofs);
                                                    * a record from before: 0 */
    uint8_t bt_kept[PERSIST4_BT_KEPT];             /* 2026-10-24: BT AUDIO's other remembered devices (op_bt.c bt_kept_out);
                                                    * a record from before: none */
    uint8_t ble_kept[PERSIST4_BLE_KEPT];           /* 2026-10-24: BT MIDI's other remembered devices (io/midi/ble_devices.c
                                                    * ble_kept, struct ble_dev x 3); a record from before: none */
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    dualcfg_t dualcfg;                             /* (the second-core test image only: after everything) */
#endif
} persist_t;
#if FELUCCA_DUAL && FELUCCA_DUALCFG
#define PERSIST4_LEN ((int)__builtin_offsetof(persist_t, dualcfg))   /* a normal build's record: whole without it */
#else
#define PERSIST4_LEN ((int)sizeof(persist_t))
#endif
#define PERSIST4_LEN0 ((int)__builtin_offsetof(persist_t, open_proj))   /* a record from before open_proj: whole without it (the rest 0) */
#if FELUCCA_BLE
#define PERSIST_BLE(f) ((int)__builtin_offsetof(persist_t, f))
#define PERSIST_BLE_END (PERSIST_BLE(ble_dev) + (int)sizeof(((persist_t *)0)->ble_dev))
_Static_assert(BLE_RF_COPY_SIZE == PERSIST4_BLE_RF && BLE_BOND_SIZE == PERSIST4_BLE_BOND &&
               BLE_DEV_STORE_SIZE == PERSIST4_BLE_DEV, "the record's BLE fields: their sizes");
#endif
#if FELUCCA_FAVORITES
_Static_assert(sizeof(fav_t) == PERSIST4_FAV, "the record's favorites: their size");
#endif
#if FELUCCA_USB_AUDIO
_Static_assert(UA_ROUTE_N == PERSIST4_UA_ROUTE, "the record's USB capture sources: their count");
#endif

/* a "PER3" record read (persist_upgrade's, accepted) -> ours: the fields this build had then, at their new places */
static void persist_from_v3(persist_t *d, const persist_v3_t *s)
{
    memset(d, 0, sizeof *d);
    d->magic = PERSIST_MAGIC4;
    d->palette = s->palette, d->lowcut = s->lowcut, d->zoom = s->zoom;
    d->panel = s->panel;
#if FELUCCA_ARRANGER
    d->arrangement = s->arrangement;
#endif
    d->view = s->view;
#if FELUCCA_BRIGHT || BP23_SET
    d->bright = s->bright;
#endif
#if BP23_SET
    d->bp23 = s->bp23;
#endif
#if FELUCCA_BLE
    memcpy(d->ble_addr, s->ble_addr, sizeof d->ble_addr);
    memcpy(d->ble_rf, s->ble_rf, sizeof d->ble_rf);
    memcpy(d->ble_bond, s->ble_bond, sizeof d->ble_bond);
    memcpy(d->ble_dev, s->ble_dev, sizeof d->ble_dev);
#endif
#if FELUCCA_FAVORITES
    memcpy(d->fav, &s->fav, sizeof d->fav);
#endif
#if FELUCCA_USB_AUDIO
    memcpy(d->ua_src, s->ua_src, sizeof d->ua_src);
    d->ua_mark = s->ua_mark;
    memcpy(d->ua_dst, s->ua_dst, sizeof d->ua_dst);
    d->ua_mark3 = s->ua_mark3;
#endif
#if FELUCCA_BT_UI
    memcpy(d->bt_rec, s->bt_rec, sizeof d->bt_rec);
#endif
#if FELUCCA_BT_AUDIO
    memcpy(d->bt_key, s->bt_key, sizeof d->bt_key);
#endif
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    d->dualcfg = s->dualcfg;
#endif
}

#if FELUCCA_FAVORITES
/* the favorites of a record read (persist_boot) into the store, and out of it (settings_save) */
static void persist_fav_load(const persist_t *p)
{
    fav_t f;
    memcpy(&f, p->fav, sizeof f);
    if (f.mark != FAV_MARK) {                      /* none saved */
        memset(&fav, 0, sizeof fav);
        return;
    }
    fav = f;
    fav_tidy(&fav);
}
static void persist_fav_save(persist_t *p)
{
    fav_t f = fav;
    f.mark = FAV_MARK;
    memcpy(p->fav, &f, sizeof f);
}
#endif

#if FELUCCA_USB_AUDIO
/* the USB capture channels' sources of a record read (persist_boot; none, or out of range: the defaults), and out (settings_save) */
/* a record of each channel's source (UAR2, UAR1) as each source's channels: the channels that took a source are its
 * channels (MAIN L on 1 and MAIN R on 2: MASTER on 1+2), a set no destination has (1 and 3) its first channel */
static void persist_ua_old(const persist_t *p)
{
    /* UAR2's indices: OFF, MAIN L R L+R, T1 T2 T3 DRUMS, DRUM BUS L R L+R, DR1..; UAR1's: OFF, MAIN L R -3 -6, T1 T2 T3
     * DRUMS, DRUM BUS L R -3 -6, DR1.. -> today's source (0xFF: none) */
    static const uint8_t V2[11] = {0xFF, UAS_MAIN, UAS_MAIN, UAS_MAIN, UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS, UAS_DBUS, UAS_DBUS, UAS_DBUS};
    static const uint8_t V1[13] = {0xFF, UAS_MAIN, UAS_MAIN, UAS_MAIN, UAS_MAIN, UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS, UAS_DBUS,
                                   UAS_DBUS, UAS_DBUS, UAS_DBUS};
    uint8_t m[UAS_N] = {0};
    uint32_t c, s, d, nv = p->ua_mark == PERSIST_UA_MARK1 ? 13u : 11u;
    for (c = 0; c < UA_ROUTE_N; c++) {
        uint32_t v = p->ua_src[c];
        s = v < nv ? (nv == 13u ? V1[v] : V2[v]) : v - nv < DRUM_LANES ? UAS_DR1 + (v - nv) : 0xFFu;
        if (s < UAS_N)
            m[s] |= (uint8_t)(1u << c);
    }
    for (s = 0; s < UAS_N; s++) {
        for (d = UAD_N - 1u; d && UAD_MASK[d] != m[s]; d--)
            ;
        if (!d && m[s])                                 /* (no destination is that set: its first channel) */
            d = (m[s] & 1u) ? UAD_1 : (m[s] & 2u) ? UAD_2 : (m[s] & 4u) ? UAD_3 : UAD_4;
        ua_dst[s] = (uint8_t)d;
    }
}
static void persist_ua_load(const persist_t *p)
{
    uint32_t s;
    ua_route_default();
    if (p->ua_mark3 == PERSIST_UA_MARK3) {
        for (s = 0; s < UAS_N; s++)
            ua_dst[s] = p->ua_dst[s];                   /* (out of range: ua_route_apply gives the default) */
    } else if (p->ua_mark == PERSIST_UA_MARK || p->ua_mark == PERSIST_UA_MARK1) {
        persist_ua_old(p);
    } else {
        return;
    }
    ua_route_apply();
}
static void persist_ua_save(persist_t *p)
{
    memset(p->ua_src, 0, sizeof p->ua_src);
    p->ua_mark = 0;                                     /* (an older build reads none: its defaults) */
    memset(p->ua_dst, 0, sizeof p->ua_dst);
    memcpy(p->ua_dst, ua_dst, sizeof ua_dst);
    p->ua_mark3 = PERSIST_UA_MARK3;
}
#endif
