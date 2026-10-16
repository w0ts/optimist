/* SPDX-License-Identifier: GPL-3.0-only */
/* Favorites (FELUCCA_FAVORITES; Felucca 1.6's marked presets, ported): the store (core/favorites.c) and how the settings
 * record keeps it (storage/persist_rec.c persist_t / persist_upgrade, which project.c reads at power-on):
 *   store     a factory sound is marked by its engine's UID and its name (stable over builds that leave engines out),
 *             a user preset by its slot; marking twice or clearing twice changes nothing; the 65th factory mark is
 *             refused (FULL), user marks are 32 of their own; an unknown slot is refused
 *   record    the favorites are the LAST field of the settings record: a record written before them (any build's
 *             length) reads whole, with no favorites and every other field as saved; a record with them reads them
 *             back; a build without them reads the same record as its own (its prefix), the BLE fields included
 *   flash     the settings object round trip through storage.c (A / B sectors 0xFC000 / 0xFD000), nothing written
 *             anywhere else
 * Exit status: the number of failed checks. Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1
#endif
#define FELUCCA_UP_FM6 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static uint32_t prog_lo = 0xFFFFFFFFu, prog_hi;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    prog_lo = off < prog_lo ? off : prog_lo;
    prog_hi = off + n > prog_hi ? off + n : prog_hi;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
    uint8_t sl24[3840];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static void ui_message(const char *m) { (void)m; }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"   /* (and sl24_guard.c: sl_word_in) */
#include <stddef.h>
#define PANEL_MAGIC 0x50414E35u                     /* (ui/panel.c's table, the record's field: its type, not the UI) */
typedef struct {
    uint32_t magic;
    uint8_t btn[14];
    uint8_t enc[7];
    int8_t dir[7];
} panel_t;
#if FELUCCA_BLE                                     /* (the BLE fields persist_t carries: their sizes, as ble_store_test.c has them) */
#ifndef BLE_RF_COPY_SIZE
#define BLE_RF_COPY_SIZE 100u
#endif
#ifndef BLE_BOND_SIZE
#define BLE_BOND_SIZE 28u
#endif
#ifndef BLE_DEV_STORE_SIZE
#define BLE_DEV_STORE_SIZE 70u
#endif
#endif
#include "../firmware/src/storage/persist_rec.c"

#if FELUCCA_BLE
void ble_midi_out(uint32_t pkt) { (void)pkt; }   /* (the sequencer's BLE out: the radio is not here) */
#endif
static int bad;
static void check(const char *what, int ok)
{
    printf("favorites: %-110s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static void t_store(void)
{
    uint32_t i;
    char nm[16];
    memset(&fav, 0, sizeof fav);
    check("the store is a mark, 64 hashes and the user mask: 136 B", sizeof(fav_t) == 136u && FAV_N == 64u);
    check("a mark is nonzero (0 is an empty place), and differs by engine and by name",
          fav_hash(1, "RHODES") != 0 && fav_hash(1, "RHODES") != fav_hash(2, "RHODES") &&
          fav_hash(1, "RHODES") != fav_hash(1, "WURLI"));
    check("nothing is marked at first", !fav_has(1, "RHODES") && !fav_count());
    check("mark: changed (1), marked, counted", fav_set(1, "RHODES", 1) == 1 && fav_has(1, "RHODES") && fav_count() == 1u);
    check("mark again: no change (0), still one", fav_set(1, "RHODES", 1) == 0 && fav_count() == 1u);
    check("the same name of another engine is not marked", !fav_has(2, "RHODES"));
    check("clear: changed (1), gone; clear again: no change", fav_set(1, "RHODES", 0) == 1 && !fav_has(1, "RHODES") &&
          fav_set(1, "RHODES", 0) == 0 && !fav_count());
    check("clear keeps the others (the list stays packed)",
          fav_set(1, "A", 1) == 1 && fav_set(1, "B", 1) == 1 && fav_set(1, "C", 1) == 1 && fav_set(1, "B", 0) == 1 &&
          fav_has(1, "A") && !fav_has(1, "B") && fav_has(1, "C") && fav_count() == 2u);
    memset(&fav, 0, sizeof fav);
    for (i = 0; i < FAV_N; i++) {
        nm[0] = 'N', nm[1] = (char)('0' + i / 10u), nm[2] = (char)('0' + i % 10u), nm[3] = 0;
        if (fav_set(3, nm, 1) != 1)
            break;
    }
    check("64 marks fit", i == FAV_N && fav_count() == FAV_N);
    check("the 65th is refused (FULL: -1), nothing changed", fav_set(3, "NEW", 1) == -1 && !fav_has(3, "NEW") && fav_count() == FAV_N);
    check("a clear then makes room again", fav_set(3, "N00", 0) == 1 && fav_set(3, "NEW", 1) == 1 && fav_has(3, "NEW"));
    memset(&fav, 0, sizeof fav);
    check("user slot 5: marked, changed (1); again 0; slot 31 the last; slot 32 refused (0), not marked",
          fav_user_set(5, 1) == 1 && fav_user_has(5) && fav_user_set(5, 1) == 0 && fav_user_set(31, 1) == 1 &&
          fav_user_has(31) && fav_user_set(UP_SLOTS, 1) == 0 && !fav_user_has(UP_SLOTS));
    check("the user marks do not take factory places", fav_count() == 0u && !fav_has(0, "INIT"));
    check("clearing a user slot", fav_user_set(5, 0) == 1 && !fav_user_has(5) && fav_user_has(31));
    check("an erased user preset loses its mark (fav_user_drop)", (fav_user_drop(31), !fav_user_has(31)));
}

/* a record as a build before the favorites wrote it: n bytes of recognisable values */
static void fill(persist_t *p, uint32_t seed)
{
    uint8_t *b = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < sizeof *p; i++)
        b[i] = (uint8_t)(i * 7u + seed);
    memset(&p->panel, 0, sizeof p->panel);
    p->panel.magic = PANEL_MAGIC;
    p->magic = PERSIST_MAGIC;
    p->view = 1;
    p->bright = 0;                                 /* (as written: never SLOOP's kit header) */
    p->bp23 = 0x01A5C3E1u;
    memset(&p->arrangement, 0, sizeof p->arrangement);
}

static void t_record(void)
{
    persist_t p, q;
    int n, r;
    uint32_t off = (uint32_t)offsetof(persist_t, fav);
    fill(&p, 3);
    q = p;
#if FELUCCA_USB_AUDIO
    check("the favorites are the record's last field but the USB routing", off + sizeof(fav_t) == offsetof(persist_t, ua_src));
#else
    check("the favorites are the record's last field", off + sizeof(fav_t) == sizeof(persist_t));
#endif
    check("... and the record still fits a sector and stays below SLOOP 2.5's (checked at build)", sizeof(persist_t) <= ST_PAYLOAD_MAX);

    /* a record a build before the favorites wrote: its length is the offset of fav */
    memset(&p.fav, 0xA5, sizeof p.fav);            /* (what st_load leaves past n: not zeroed) */
    r = persist_upgrade(&p, (int)off);
    check("a record from before the favorites reads whole (length = the new record's)", r == (int)sizeof p);
    check("... its settings word, view, palette, panel and magic as saved",
          p.bp23 == q.bp23 && p.view == q.view && p.palette == q.palette && p.magic == q.magic && p.panel.magic == PANEL_MAGIC &&
          p.zoom == q.zoom && p.lowcut == q.lowcut && p.bright == q.bright);
#if FELUCCA_BLE
    check("... the BLE address, trims, bond and device store kept as saved",
          !memcmp(p.ble_addr, q.ble_addr, sizeof p.ble_addr) && !memcmp(p.ble_rf, q.ble_rf, sizeof p.ble_rf) &&
          !memcmp(p.ble_bond, q.ble_bond, sizeof p.ble_bond) && !memcmp(p.ble_dev, q.ble_dev, sizeof p.ble_dev));
#endif
    check("... and no favorites", !memcmp(&p.fav, &(fav_t){0}, sizeof p.fav));

    /* a record with favorites */
    memset(&fav, 0, sizeof fav);
    fav_set(1, "RHODES", 1), fav_set(4, "GRAND PNO", 1), fav_user_set(7, 1);
    fill(&p, 11);
    persist_fav_save(&p);
    q = p;
    r = persist_upgrade(&p, (int)sizeof p);
    check("a full record keeps its length and everything in it", r == (int)sizeof p && !memcmp(&p, &q, sizeof p));
    memset(&fav, 0, sizeof fav);
    persist_fav_load(&p);
    check("... the favorites load back from it", fav_has(1, "RHODES") && fav_has(4, "GRAND PNO") && fav_user_has(7) && fav_count() == 2u);

    /* a record whose tail is another layout (a BLE build's address and trims where a build without BLE has the favorites) */
    fill(&p, 13);
    memset(&fav, 0, sizeof fav);
    fav_set(5, "X", 1);
    persist_fav_load(&p);
    check("bytes of another layout in the tail are no favorites (no mark): the store is cleared", !fav_count() && !fav.user);

    /* a record saved before VIEW: still short (accepted as it was), no favorites */
    fill(&p, 5);
    memset(&p.fav, 0xA5, sizeof p.fav);
    r = persist_upgrade(&p, PERSIST_NO_VIEW);
    memset(&fav, 0, sizeof fav);
    persist_fav_load(&p);
    check("a record from before VIEW: as before (its short length), no favorites", r == PERSIST_NO_VIEW && !fav_count() && !fav.user);
#if FELUCCA_BLE
    fill(&p, 9);
    q = p;
    memset(&p.ble_dev, 0xA5, sizeof p.ble_dev);
    memset(&p.fav, 0xA5, sizeof p.fav);
    r = persist_upgrade(&p, (int)offsetof(persist_t, ble_dev));
    check("a BLE build's record from before the device store: whole, address, trims and bond kept, no device, no favorites",
          r == (int)sizeof p && !memcmp(p.ble_addr, q.ble_addr, sizeof p.ble_addr) && !memcmp(p.ble_bond, q.ble_bond, sizeof p.ble_bond) &&
          !memcmp(p.ble_rf, q.ble_rf, sizeof p.ble_rf) && p.bp23 == q.bp23 && !memcmp(&p.fav, &(fav_t){0}, sizeof p.fav));
    fill(&p, 9);
    q = p;
    memset(&p.fav, 0xA5, sizeof p.fav);
    r = persist_upgrade(&p, (int)offsetof(persist_t, ble_addr));
    check("a record from a build without BLE: whole, no address, no favorites, the settings word kept",
          r == (int)sizeof p && p.bp23 == q.bp23 && !memcmp(&p.fav, &(fav_t){0}, sizeof p.fav));
#endif
}

#if FELUCCA_USB_AUDIO
/* the USB routing (SYSTEM > USB AUDIO), appended last: an older record reads whole with the defaults, a UAR1 record's
 * -3 / -6 dB sums load as L+R (the user, 2026-10-15: one sum, (L + R) / 2) */
static void t_ua(void)
{
    persist_t p, q;
    int r;
    fill(&p, 7);
    q = p;
    memset(p.ua_src, 0xA5, sizeof p.ua_src), p.ua_mark = 0xA5A5A5A5u;
    r = persist_upgrade(&p, (int)offsetof(persist_t, ua_src));
    persist_ua_load(&p);
    check("USB AUDIO: a record from before it reads whole, the routing the default, the favorites kept",
          r == (int)sizeof p && ua_ident && !memcmp(&p.fav, &q.fav, sizeof p.fav) && p.bp23 == q.bp23);
    p.ua_mark = PERSIST_UA_MARK1;
    p.ua_src[0] = 4, p.ua_src[1] = 11, p.ua_src[2] = 13 + 9, p.ua_src[3] = 13 + 10;   /* (M-6, B-3, DR10, out of range: 10 lanes) */
    persist_ua_load(&p);
    check("USB AUDIO: a UAR1 record: -6 / -3 dB as L+R, a lane kept, out of range the default",
          ua_src[0] == UAS_MAIN_S && ua_src[1] == UAS_DBUS_S && ua_src[2] == UAS_N - 1u && ua_src[3] == UAS_DRUMS);
    persist_ua_save(&q);
    ua_route_default();
    persist_ua_load(&q);
    check("USB AUDIO: saved as UAR2 and read back", q.ua_mark == PERSIST_UA_MARK && ua_src[0] == UAS_MAIN_S && ua_src[2] == UAS_N - 1u);
    ua_route_default();
}
#endif

static void t_flash(void)
{
    persist_t p, q, r;
    int n;
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    memset(&fav, 0, sizeof fav);
    fav_set(2, "SOUL ORGAN", 1), fav_user_set(0, 1);
    fill(&p, 21);
    persist_fav_save(&p);
    check("saved (st_save of the settings object)", st_save(OBJ_SETTINGS, &p, sizeof p) == 0);
    memset(&q, 0xCC, sizeof q);
    n = st_load(OBJ_SETTINGS, &q, sizeof q);
    check("read back whole, byte for byte", n == (int)sizeof q && !memcmp(&p, &q, sizeof p));
    n = persist_upgrade(&q, n);
    memset(&fav, 0, sizeof fav);
    persist_fav_load(&q);
    check("... and its favorites come back", n == (int)sizeof q && fav_has(2, "SOUL ORGAN") && fav_user_has(0) && fav_count() == 1u);
    /* a build without the favorites reads the same object with its smaller buffer: the prefix, as its own */
    memset(&r, 0xCC, sizeof r);
    n = st_load(OBJ_SETTINGS, &r, (uint32_t)offsetof(persist_t, fav));
    check("a build without the favorites (a smaller buffer) reads the same prefix, whole", n == (int)offsetof(persist_t, fav) &&
          !memcmp(&p, &r, (size_t)n));
    for (i = 0; i < 0x100000u; i++)
        if (nor[i] != 0xFF && (i < 0xFC000u || i >= 0xFE000u)) {
            check("nothing is written outside the settings object's two sectors", 0);
            return;
        }
    check("nothing is written outside the settings object's two sectors", 1);
}

int main(void)
{
    t_store();
    t_record();
#if FELUCCA_USB_AUDIO
    t_ua();
#endif
    t_flash();
    printf("favorites: %s\n", bad ? "FAILED" : "all ok");
    return bad;
}
