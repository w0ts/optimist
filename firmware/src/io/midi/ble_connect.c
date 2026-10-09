/* SPDX-License-Identifier: GPL-3.0-only */
/* Connecting out (BLE_CENTRAL; docs/BLE-DEVICES-DESIGN.md §1.4, §3.3, P3 / P4, the LAST ruling §0.1): the main loop's
 * side of the central role, part of ble_devices.c (included there: it shares the store, the scan table, the list's
 * messages).
 *   a pick       YES on a nearby row: connect to it (the stack: initiator, master link, pairing, BLE-MIDI subscribed,
 *                ble/ble_central.c). Ready -> it becomes LAST (the one remembered device, replacing the old one; its
 *                bond and identity from the pairing kept with it) and LAST is the choice. 10 s to find it, else
 *                "NOT FOUND"; any other end: "FAILED <why>";
 *   LAST         while BLUETOOTH is ON, LAST is the choice, DEVICES is closed and no link of either role is up: search
 *                for it (initiate 2 s, advertise 1 s, for 30 s; then initiate 1 s every 10 s). A LAST with an IRK
 *                (it uses resolvable private addresses: iOS, macOS) is found by scanning and resolving each AdvA
 *                (ah, ble_rpa_resolve) instead, then initiating to the address just heard;
 *   incoming     a central that connects to the visible FM-1 is accepted as always (the search waits) and never
 *                changes LAST;
 *   NONE         leaves our central link (a Mac connected to us stays) and stops the search.
 * The stack's calls go through the BLE interrupts' hold (fm1_ble_irqs_hold), as ble_midi_set's do. */

enum { RC_OFF, RC_WAIT, RC_SCAN, RC_TRY, RC_PICK, RC_LINK };
#define RC_FAST_MS 30000u                         /* the fast search: 2 s initiating, 1 s advertising */
#define RC_TRY_FAST_MS 2000u
#define RC_GAP_FAST_MS 1000u
#define RC_TRY_SLOW_MS 1000u                      /* then 1 s every 10 s */
#define RC_GAP_SLOW_MS 9000u
#define RC_PICK_MS 10000u                         /* a pick that is not heard in this long: NOT FOUND */

static struct {
    uint8_t phase, to_last, was_ready;            /* RC_*; the attempt is to LAST; it reached READY */
    uint32_t t_end, next_try, search_t0;          /* fm1_ms: the attempt's end, the next try, the search's start */
    struct ble_found pick;                        /* the device picked (its name and kind for LAST) */
    struct ble_keys keys;                         /* a pairing's keys (from the BLE interrupts) */
    volatile uint8_t keys_new;
    char msg[40];
} brc;

BLE_API void ble_app_central_keys(const struct ble_keys *k)   /* BLE interrupts: kept for the main loop */
{
    brc.keys = *k;
    brc.keys_new = 1;
}

static int rc_is_last(const uint8_t a[6], uint8_t rnd)   /* this address is LAST's, or resolves with its IRK */
{
    const struct ble_dev *d = &ble_store.dev;
    if (!ble_store_has_last(&ble_store))
        return 0;
    if ((d->info & BLE_DEV_RANDOM ? 1u : 0u) == rnd && ble_eq(d->addr, a, 6))
        return 1;
    return rnd && (d->info & BLE_DEV_IRK) && ble_rpa_resolve(d->irk, a);
}

static void rc_say(const char *a, const char *b)    /* "A b" on the list's status line */
{
    uint32_t n = 0;
    while (*a && n < sizeof brc.msg - 1u)
        brc.msg[n++] = *a++;
    while (b && *b && n < sizeof brc.msg - 1u)
        brc.msg[n++] = *b++;
    brc.msg[n] = 0;
    ble_dev_say(brc.msg);
}

static void rc_name(char out[BLE_NAME_MAX + 1u])  /* the device of the attempt, as shown */
{
    if (brc.to_last)
        ble_store_name(&ble_store, out);
    else if (brc.pick.name[0])
        ble_cpy((uint8_t *)out, (const uint8_t *)brc.pick.name, BLE_NAME_MAX + 1u);   /* (terminated: ble_scan.h) */
    else
        ble_addr_text(brc.pick.addr, out);
}

/* connect to a (its AdvA now): LAST's bond goes with it when it is LAST -> 1 started */
static int rc_connect(const uint8_t a[6], uint8_t rnd)
{
    struct ble_peer p;
    const struct ble_dev *d = &ble_store.dev;
    int ok;
    ble_zero((uint8_t *)&p, sizeof p);
    ble_cpy(p.addr, a, 6);
    p.addr_rand = rnd;
    if (brc.to_last && (d->info & BLE_DEV_BONDED)) {
        p.bonded = 1;
        ble_cpy(p.ltk, d->ltk, 16);
        ble_cpy(p.rand, d->rand, 8);
        p.ediv = ble_rd16(d->ediv);
    }
    brc.keys_new = 0;
    brc.was_ready = 0;
    fm1_ble_irqs_hold(1);
    ok = ble_central_connect(&p);
    fm1_ble_irqs_hold(0);
    BLE_DG(ble_dgc.rc_tries++);
    return ok;
}

static void rc_stop(void)                         /* initiating or scanning for the search: back to advertising */
{
    fm1_ble_irqs_hold(1);
    if (brc.phase == RC_SCAN)
        ble_ll_scan(ble_devs_open);               /* (DEVICES opened meanwhile: its scan goes on) */
    else if (ble_ll_initiating())
        ble_central_cancel();
    fm1_ble_irqs_hold(0);
}

static void rc_phase(uint8_t ph)
{
    brc.phase = ph;
    BLE_DG(ble_dgc.rc_phase = ph);
}

/* the search: the next try after a gap of the schedule */
static void rc_wait(void)
{
    int fast = fm1_ms - brc.search_t0 < RC_FAST_MS;
    brc.next_try = fm1_ms + (fast ? RC_GAP_FAST_MS : RC_GAP_SLOW_MS);
    rc_phase(RC_WAIT);
}

static void rc_try(void)
{
    const struct ble_dev *d = &ble_store.dev;
    brc.to_last = 1;
    brc.t_end = fm1_ms + (fm1_ms - brc.search_t0 < RC_FAST_MS ? RC_TRY_FAST_MS : RC_TRY_SLOW_MS);
    if (d->info & BLE_DEV_IRK) {                  /* private addresses: scan and resolve */
        fm1_ble_irqs_hold(1);
        ble_ll_scan(1);
        fm1_ble_irqs_hold(0);
        BLE_DG(ble_dgc.rc_scans++);
        rc_phase(RC_SCAN);
        return;
    }
    if (rc_connect(d->addr, (uint8_t)(d->info & BLE_DEV_RANDOM ? 1u : 0u)))
        rc_phase(RC_TRY);
    else
        rc_wait();
}

/* a scan report while searching for LAST behind a private address (ble_devices_poll) */
static void rc_report(const uint8_t *pdu, uint8_t n)
{
    uint8_t rnd = (uint8_t)(pdu[0] >> 6 & 1u);
    if (brc.phase != RC_SCAN || n < 8u || (pdu[0] & 0x0Fu) != 0x0u)
        return;                                   /* (an ADV_IND: connectable) */
    if (rnd && (pdu[7] >> 6) == 1u)
        BLE_DG(ble_dgc.rc_rpa_seen++);
    if (!rc_is_last(pdu + 2, rnd))
        return;
    BLE_DG(ble_dgc.rc_rpa_ok++);
    fm1_ble_irqs_hold(1);
    ble_ll_scan(0);
    fm1_ble_irqs_hold(0);
    if (rc_connect(pdu + 2, rnd))                 /* (the rest of this try's time to find it advertising) */
        rc_phase(RC_TRY);
    else
        rc_wait();
}

static const char *rc_why(uint8_t f)
{
    switch (f) {
    case BLE_CF_NO_MIDI: return "NO MIDI SERVICE";
    case BLE_CF_PAIRING: return "PAIRING FAILED";
    case BLE_CF_AUTH: return "NEEDS PAIRING";
    case BLE_CF_GATT: return "GATT ERROR";
    default: return "LINK LOST";
    }
}

/* the attempt connected and is ready: a pick becomes LAST (and the choice); a pairing's keys go with LAST */
static void rc_ready(void)
{
    char nm[BLE_NAME_MAX + 1u];
    brc.was_ready = 1;
    if (!brc.to_last) {
        ble_store_set_last(&ble_store, brc.pick.addr, brc.pick.addr_rand, brc.pick.name, brc.pick.kind);
        brc.to_last = 1;                          /* (it is LAST now: keys and the name below are its) */
    }
    ble_store_select(&ble_store, BLE_SEL_LAST);
    ble_store_changed();
    rc_name(nm);
    rc_say("CONNECTED ", nm);
    BLE_DG(ble_dgc.rc_ok++);
}

static void rc_keys(void)                         /* a pairing's keys, once the device is LAST */
{
    struct ble_keys k;
    if (!brc.keys_new || !brc.was_ready)
        return;
    k = brc.keys;
    brc.keys_new = 0;
    if (k.has & BLE_KEYS_LTK)
        ble_store_set_bond(&ble_store, k.ltk, k.rand, k.ediv);
    if (k.has & BLE_KEYS_ID)
        ble_store_set_id(&ble_store, k.irk, k.id, k.id_rand);
    ble_store_changed();
}

/* the attempt has a link, or had one: ready, or gone (with why) */
static void rc_link(void)
{
    uint8_t st = ble_central_state();
    char nm[BLE_NAME_MAX + 1u];
    if (st == BLE_CS_READY && !brc.was_ready)
        rc_ready();
    rc_keys();
    if (st != BLE_CS_IDLE)
        return;
    rc_name(nm);
    if (brc.was_ready) {
        rc_say("LOST ", nm);
        rc_phase(RC_OFF);                         /* (LAST chosen: a new search starts) */
        return;
    }
    BLE_DG(ble_dgc.rc_fails++);
    BLE_DG(ble_dgc.rc_last_fail = ble_central_fail());
    rc_say("FAILED: ", rc_why(ble_central_fail()));
    if (brc.phase == RC_LINK && brc.search_t0)
        rc_wait();                                /* (the search goes on) */
    else
        rc_phase(RC_OFF);
}

/* a pick (YES on a nearby row, or on LAST while the list shows it) */
static void ble_connect_pick(const struct ble_found *e)
{
    char nm[BLE_NAME_MAX + 1u];
    if (!ble_on)
        ble_midi_set(1);                          /* (YES with BLUETOOTH OFF switches it ON: decided, §6.1 #8) */
    if (!ble_up)
        return;
    if (ble_connected() && !ble_ll_central()) {
        ble_dev_say("BUSY: A HOST IS CONNECTED");
        return;
    }
    rc_stop();
    if (ble_ll_central()) {                       /* (one link: the device we are on goes first) */
        fm1_ble_irqs_hold(1);
        ble_central_cancel();
        fm1_ble_irqs_hold(0);
    }
    brc.pick = *e;
    brc.to_last = (uint8_t)rc_is_last(e->addr, e->addr_rand);
    brc.search_t0 = 0;
    BLE_DG(ble_dgc.picks++);
    rc_name(nm);
    if (ble_ll_central() || !rc_connect(e->addr, e->addr_rand)) {
        brc.t_end = fm1_ms + RC_PICK_MS;          /* (the old link is still closing: tried again in the poll) */
        rc_phase(RC_PICK);
        brc.was_ready = 2;                        /* (not started yet) */
        rc_say("CONNECTING ", nm);
        return;
    }
    brc.t_end = fm1_ms + RC_PICK_MS;
    rc_phase(RC_PICK);
    rc_say("CONNECTING ", nm);
}

/* NONE: our central link and the search end (a central connected to us stays) */
static void ble_connect_none(void)
{
    rc_stop();
    if (ble_up && (ble_ll_central() || ble_ll_initiating())) {
        fm1_ble_irqs_hold(1);
        ble_central_cancel();
        fm1_ble_irqs_hold(0);
    }
    rc_phase(RC_OFF);
}

static uint8_t ble_connect_phase(void) { return brc.phase; }

/* the nearby row being connected to (CONNECTING in the list) */
static int ble_connect_on(const struct ble_found *e)
{
    return brc.phase == RC_PICK && brc.pick.addr_rand == e->addr_rand && ble_eq(brc.pick.addr, e->addr, 6);
}

/* LAST's row shows CONNECTED: our link to it is ready */
static int ble_connect_last_up(void)
{
    return brc.to_last && brc.was_ready == 1 && ble_central_state() == BLE_CS_READY;
}

/* main loop */
static void ble_connect_poll(void)
{
    int want = ble_on && ble_up && ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_LAST && !ble_devs_open;
    switch (brc.phase) {
    case RC_OFF:
        if (want && !ble_connected() && !ble_ll_initiating()) {
            brc.search_t0 = fm1_ms | 1u;
            brc.next_try = fm1_ms;
            rc_phase(RC_WAIT);
        }
        return;
    case RC_WAIT:
        if (!want)
            rc_phase(RC_OFF);
        else if (!ble_connected() && (int32_t)(fm1_ms - brc.next_try) >= 0)
            rc_try();                             /* (a central connected to us: the search waits) */
        return;
    case RC_SCAN:
        if (!want || (int32_t)(fm1_ms - brc.t_end) >= 0) {
            rc_stop();
            want ? rc_wait() : rc_phase(RC_OFF);
        }
        return;
    case RC_TRY:
    case RC_PICK:
        if (brc.was_ready == 2) {                 /* a pick waiting for the old link to close */
            if (!ble_ll_central() && rc_connect(brc.pick.addr, brc.pick.addr_rand))
                brc.was_ready = 0;
            else if ((int32_t)(fm1_ms - brc.t_end) >= 0) {
                rc_say("FAILED: ", "BUSY");
                rc_phase(RC_OFF);
            }
            return;
        }
        if (ble_central_state() == BLE_CS_SETUP || ble_central_state() == BLE_CS_READY ||
            (!ble_ll_initiating() && ble_central_fail() != BLE_CF_NONE)) {   /* (up, or up and gone already) */
            if (brc.phase == RC_PICK)
                brc.search_t0 = 0;
            rc_phase(RC_LINK);
            rc_link();
            return;
        }
        if (brc.phase == RC_TRY && !want) {
            rc_stop();
            rc_phase(RC_OFF);
        } else if ((int32_t)(fm1_ms - brc.t_end) >= 0 || !ble_ll_initiating()) {
            rc_stop();
            if (brc.phase == RC_PICK) {
                BLE_DG(ble_dgc.rc_fails++);
                rc_say("FAILED: ", "NOT FOUND");
                rc_phase(RC_OFF);
            } else
                rc_wait();
        }
        return;
    default:                                      /* RC_LINK */
        rc_link();
        return;
    }
}
