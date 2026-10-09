/* SPDX-License-Identifier: GPL-3.0-only */
/* Connecting out (BLE_CENTRAL; docs/BLE-DEVICES-DESIGN.md §1.4, §3.3, §9.3, P3 / P4, the LAST ruling §0.1): the main
 * loop's side of the central role, part of ble_devices.c (included there: it shares the store, the scan table, the
 * list's messages).
 *   a pick       YES on a nearby row: connect to it (the stack: initiator, master link, pairing, BLE-MIDI subscribed,
 *                ble/ble_central.c). Ready -> it becomes LAST (the one remembered device, replacing the old one; its
 *                bond, identity and security level from the pairing kept with it) and LAST is the choice. 10 s to
 *                find it, else "NOT FOUND"; any other end: "FAILED: <why>";
 *   security     pairing happens only when the peer asks (ble_central.c): Just Works, silent on both screens of ours
 *                (another FM-1, a controller). A peer that refuses again after Just Works (Insufficient
 *                Authentication: an iPhone app's MIDI characteristic) needs an authenticated pairing: the stack pairs
 *                again with a passkey on the same link (shown on the DEVICES status area; the phone's user types it);
 *                only a peer that refuses that is left and connected to again at once, once, with the passkey from
 *                the start. That level is kept with LAST (and, until the next power-off, with the device picked), so
 *                its next pairing goes to the passkey straight away. A pairing that fails stops there: "FAILED: <why>"
 *                stays, and nothing connects (or prompts the phone) again until the user acts (a pick, NONE, LAST,
 *                FORGET, BLUETOOTH OFF);
 *   retries      a link that is not made (0x3E: no answer in its transmit window; the FM-1 saw 2 in 6) or lost before
 *                any pairing started on it is made again at once, up to RC_TRIES attempts per user action (or per
 *                try of the search): "CONNECTING (TRY n/6) <name>" meanwhile, FAILED only after the last. These are
 *                link attempts: one that had a pairing (a prompt on the phone) is never made again by itself;
 *   LAST         while BLUETOOTH is ON, LAST is the choice, DEVICES is closed and no link of either role is up: search
 *                for it (initiate 2 s, advertise 1 s, for 30 s; then initiate 1 s every 10 s). A LAST with an IRK
 *                (it uses resolvable private addresses: iOS, macOS) is found by scanning and resolving each AdvA
 *                (ah, ble_rpa_resolve) instead, then initiating to the address just heard;
 *   incoming     a central that connects to the visible FM-1 is accepted as always (the search waits) and never
 *                changes LAST;
 *   NONE         leaves our central link and stops the search.
 * What the status area shows: ble_connect_status (CONNECTING / PAIRING / the passkey / CONNECTED <name> / FAILED: <why>,
 * the failure kept until the user acts). The stack's calls go through the BLE interrupts' hold (fm1_ble_irqs_hold),
 * as ble_midi_set's do. */

enum { RC_OFF, RC_WAIT, RC_SCAN, RC_TRY, RC_PICK, RC_LINK, RC_HELD };
#define RC_FAST_MS 30000u                         /* the fast search: 2 s initiating, 1 s advertising */
#define RC_TRY_FAST_MS 2000u
#define RC_GAP_FAST_MS 1000u
#define RC_TRY_SLOW_MS 1000u                      /* then 1 s every 10 s */
#define RC_GAP_SLOW_MS 9000u
#define RC_PICK_MS 10000u                         /* a pick that is not heard in this long: NOT FOUND */
#define RC_TRIES 6u                               /* link attempts per user action (a link not made, or lost early) */
#define RC_RETRY_MS 2000u                         /* an attempt made again: its time to hear the device advertising */
enum { RCS_NONE, RCS_INFO, RCS_GOOD, RCS_BAD };   /* ble_connect_status: nothing to say, under way, connected, failed */

static struct {
    uint8_t phase, to_last, was_ready;            /* RC_*; the attempt is to LAST; it reached READY */
    uint8_t mitm, escalated;                      /* the attempt pairs with a passkey; it was made again for that */
    uint8_t failed;                               /* msg is a failure, shown until the user acts */
    uint8_t tries;                                /* link attempts of this user action (or search try), 1..RC_TRIES */
    uint8_t hint_has, hint_addr[6], hint_rand, hint_irk[16];   /* a device that needed a passkey (RAM: 1 its address,
                                                   * 2 its IRK too): picked again, it pairs with the passkey at once */
    uint8_t addr[6], addr_rand;                   /* the attempt's address (as heard: made again to the same) */
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

static int rc_hint(const uint8_t a[6], uint8_t rnd)   /* this device needed a passkey on an earlier attempt */
{
    if ((brc.hint_has & 1u) && brc.hint_rand == rnd && ble_eq(brc.hint_addr, a, 6))
        return 1;
    return rnd && (brc.hint_has & 2u) && ble_rpa_resolve(brc.hint_irk, a);
}

static void rc_hint_keep(void)                    /* the attempt's device needs a passkey: remembered for a new pick */
{
    ble_cpy(brc.hint_addr, brc.addr, 6);
    brc.hint_rand = brc.addr_rand;
    brc.hint_has = 1u;
    if (brc.keys.has & BLE_KEYS_ID) {             /* (its IRK from the Just Works pairing: its next private address) */
        ble_cpy(brc.hint_irk, brc.keys.irk, 16);
        brc.hint_has |= 2u;
    }
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

static void rc_text(char *out, uint32_t room, const char *a, const char *b)   /* "A b" */
{
    uint32_t n = 0;
    while (*a && n < room - 1u)
        out[n++] = *a++;
    while (b && *b && n < room - 1u)
        out[n++] = *b++;
    out[n] = 0;
}

static void rc_failed(const char *a, const char *b)   /* a failure for the status area, kept until the user acts */
{
    rc_text(brc.msg, sizeof brc.msg, a, b);
    brc.failed = 1;
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

/* connect to a (its AdvA now). LAST's bond goes with it when it is LAST, unless a passkey is needed and the bond is
 * not one (or the attempt was made again for the passkey) -> 1 started */
static int rc_connect(const uint8_t a[6], uint8_t rnd)
{
    struct ble_peer p;
    const struct ble_dev *d = &ble_store.dev;
    int ok;
    ble_zero((uint8_t *)&p, sizeof p);
    ble_cpy(p.addr, a, 6);
    p.addr_rand = rnd;
    ble_cpy(brc.addr, a, 6);
    brc.addr_rand = rnd;
    if ((brc.to_last && (d->sec & BLE_DEV_SEC_MITM)) || rc_hint(a, rnd))
        brc.mitm = 1;
    if (brc.mitm)
        p.sec |= BLE_PEER_MITM;
    if (brc.to_last && (d->info & BLE_DEV_BONDED) && !brc.escalated &&
        (!brc.mitm || (d->sec & BLE_DEV_SEC_AUTH))) {
        p.bonded = 1;
        p.sec |= (uint8_t)(d->sec & BLE_DEV_SEC_AUTH ? BLE_PEER_AUTH : 0u);
        ble_cpy(p.ltk, d->ltk, 16);
        ble_cpy(p.rand, d->rand, 8);
        p.ediv = ble_rd16(d->ediv);
    }
    brc.keys_new = 0;
    brc.was_ready = 0;
    fm1_ble_irqs_hold(1);
    ok = ble_central_connect(&p);
    fm1_ble_irqs_hold(0);
    brc.tries++;
    BLE_DG(ble_dgc.rc_tries++);
    BLE_DG(ble_dgc.rc_try = brc.tries);
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

/* the attempt's link was not made, or lost before any pairing started on it: made again at once (the same address),
 * up to RC_TRIES attempts in all -> 1 under way */
static int rc_retry(void)
{
    if (brc.tries >= RC_TRIES || !rc_connect(brc.addr, brc.addr_rand))
        return 0;
    BLE_DG(ble_dgc.rc_retries++);
    brc.t_end = fm1_ms + RC_RETRY_MS;
    rc_phase(brc.search_t0 ? RC_TRY : RC_PICK);
    return 1;
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
    brc.mitm = brc.escalated = brc.tries = 0;
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
    case BLE_CF_PAIRING: return "PAIRING";
    case BLE_CF_AUTH:
    case BLE_CF_NEED_MITM: return "AUTH";
    case BLE_CF_GATT: return "GATT ERROR";
    default: return ble_central_code() == 0x3Eu ? "NO LINK" : "LINK LOST";   /* (0x3E: not established) */
    }
}

/* "FAILED: <why>", with the attempts made when there were several ("FAILED: NO LINK (6 TRIES)") */
static void rc_failed_why(const char *why)
{
    char t[32];
    uint32_t n = 0;
    while (*why && n < sizeof t - 11u)
        t[n++] = *why++;
    if (brc.tries > 1u) {
        t[n++] = ' ', t[n++] = '(';
        t[n++] = (char)('0' + brc.tries % 10u);
        ble_cpy((uint8_t *)t + n, (const uint8_t *)" TRIES)", 7), n += 7;
    }
    t[n] = 0;
    rc_failed("FAILED: ", t);
}

/* the attempt connected and is ready: a pick becomes LAST (and the choice); the level it needed kept */
static void rc_ready(void)
{
    brc.was_ready = 1;
    brc.failed = 0;
    if (!brc.to_last) {
        ble_store_set_last(&ble_store, brc.pick.addr, brc.pick.addr_rand, brc.pick.name, brc.pick.kind);
        brc.to_last = 1;                          /* (it is LAST now: keys and the name below are its) */
    }
    if (brc.mitm || ble_central_mitm())           /* (a passkey asked for on its link, or the link made for it) */
        ble_store_set_mitm(&ble_store);
    ble_store_select(&ble_store, BLE_SEL_LAST);
    ble_store_changed();
    BLE_DG(ble_dgc.rc_ok++);
}

static void rc_keys(void)                         /* a pairing's keys, once the device is LAST */
{
    struct ble_keys k;
    if (!brc.keys_new || brc.was_ready != 1u)
        return;
    k = brc.keys;
    brc.keys_new = 0;
    if (k.has & BLE_KEYS_LTK)
        ble_store_set_bond(&ble_store, k.ltk, k.rand, k.ediv, (k.has & BLE_KEYS_AUTH) != 0u);
    if (k.has & BLE_KEYS_ID)
        ble_store_set_id(&ble_store, k.irk, k.id, k.id_rand);
    ble_store_changed();
}

/* refused again after Just Works: the same device again at once, pairing with a passkey (once per attempt) -> 1 */
static int rc_escalate(void)
{
    if (brc.escalated)
        return 0;
    brc.escalated = 1;
    brc.mitm = 1;
    brc.tries = 0;                                /* (a new link: its own attempts) */
    if (brc.to_last) {
        ble_store_set_mitm(&ble_store);           /* (its next pairing: the passkey straight away) */
        ble_store_changed();
    }
    if (!rc_connect(brc.addr, brc.addr_rand))
        return 0;
    brc.t_end = fm1_ms + RC_PICK_MS;
    rc_phase(brc.search_t0 ? RC_TRY : RC_PICK);
    return 1;
}

/* the attempt has a link, or had one: ready, or gone (with why) */
static void rc_link(void)
{
    uint8_t st = ble_central_state(), f;
    char nm[BLE_NAME_MAX + 1u];
    if (st == BLE_CS_READY && !brc.was_ready)
        rc_ready();
    rc_keys();
    if (st != BLE_CS_IDLE)
        return;
    rc_name(nm);
    if (brc.was_ready) {
        rc_failed("LOST ", nm);
        rc_phase(RC_OFF);                         /* (LAST chosen: a new search starts) */
        return;
    }
    f = ble_central_fail();
    BLE_DG(ble_dgc.rc_fails++);
    BLE_DG(ble_dgc.rc_last_fail = f);
    if (ble_central_mitm())
        rc_hint_keep();
    if (f == BLE_CF_NEED_MITM && rc_escalate())
        return;
    if (f == BLE_CF_LOST && !ble_central_prompted() && rc_retry())
        return;                                   /* (a link attempt again: no pairing was started on it) */
    rc_failed_why(rc_why(f));
    if (f == BLE_CF_PAIRING || f == BLE_CF_AUTH || f == BLE_CF_NEED_MITM)
        rc_phase(RC_HELD);                        /* (no new attempt, no new prompt on the phone, until the user acts) */
    else if (brc.phase == RC_LINK && brc.search_t0)
        rc_wait();                                /* (the search goes on) */
    else
        rc_phase(RC_OFF);
}

/* a pick (YES on a nearby row, or on LAST while the list shows it) */
static void ble_connect_pick(const struct ble_found *e)
{
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
    brc.mitm = brc.escalated = brc.failed = brc.tries = 0;
    BLE_DG(ble_dgc.picks++);
    brc.t_end = fm1_ms + RC_PICK_MS;
    rc_phase(RC_PICK);
    if (ble_ll_central() || !rc_connect(e->addr, e->addr_rand))
        brc.was_ready = 2;                        /* (the old link is still closing: tried again in the poll) */
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
    brc.failed = 0;
    rc_phase(RC_OFF);
}

/* LAST picked again while a failure is held: the search may start again */
static void ble_connect_rearm(void)
{
    brc.failed = 0;
    if (brc.phase == RC_HELD)
        rc_phase(RC_OFF);
}

static uint8_t ble_connect_phase(void) { return brc.phase; }

/* the nearby row being connected to (CONNECTING in the list) */
static int ble_connect_on(const struct ble_found *e)
{
    return (brc.phase == RC_PICK || (brc.phase == RC_LINK && brc.was_ready != 1u)) && !brc.to_last &&
           brc.pick.addr_rand == e->addr_rand && ble_eq(brc.pick.addr, e->addr, 6);
}

/* LAST's row shows CONNECTED: our link to it is ready */
static int ble_connect_last_up(void)
{
    return brc.to_last && brc.was_ready == 1 && ble_central_state() == BLE_CS_READY;
}

/* the passkey to show (0..999999) while our pairing waits for the phone's user, else BLE_NO_PASSKEY */
static uint32_t ble_connect_passkey(void)
{
    return ble_up && brc.phase == RC_LINK ? ble_central_passkey() : BLE_NO_PASSKEY;
}

/* what the radio's connecting out is, for the menu: 0 nothing, 1 setting up a link of ours, 2 pairing, 3 a failure
 * kept */
static uint32_t ble_connect_ui(void)
{
    if (ble_up && brc.phase == RC_LINK && ble_ll_central() && ble_central_state() == BLE_CS_SETUP)
        return ble_central_pairing() ? 2u : 1u;
    return brc.failed ? 3u : 0u;
}

/* the status area's line (out, room octets): RCS_* (RCS_NONE: nothing of ours to say) */
static uint32_t ble_connect_status(char *out, uint32_t room)
{
    char nm[BLE_NAME_MAX + 1u];
    uint8_t st = ble_up ? ble_central_state() : BLE_CS_IDLE;
    if (brc.phase == RC_PICK || (brc.phase == RC_TRY && (brc.escalated || brc.tries > 1u)) ||
        (brc.phase == RC_LINK && (st == BLE_CS_CONNECTING || st == BLE_CS_SETUP))) {
        rc_name(nm);
        if (ble_connect_passkey() != BLE_NO_PASSKEY)
            rc_text(out, room, "ENTER THIS CODE ON THE PHONE", 0);
        else if (brc.phase == RC_LINK && ble_central_pairing())
            rc_text(out, room, "PAIRING ", nm);
        else if (brc.tries > 1u) {                /* "CONNECTING (TRY n/6) <name>" while a link is made again */
            char t[24] = "CONNECTING (TRY n/6) ";
            t[16] = (char)('0' + brc.tries % 10u);
            t[18] = (char)('0' + RC_TRIES % 10u);
            rc_text(out, room, t, nm);
        } else
            rc_text(out, room, brc.escalated ? "PAIRING " : "CONNECTING ", nm);
        return RCS_INFO;
    }
    if (brc.phase == RC_LINK && st == BLE_CS_READY && brc.was_ready == 1u) {
        rc_name(nm);
        rc_text(out, room, "CONNECTED ", nm);
        return RCS_GOOD;
    }
    if (brc.failed) {
        rc_text(out, room, brc.msg, 0);
        return RCS_BAD;
    }
    return RCS_NONE;
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
    case RC_HELD:                                 /* a failure kept: nothing until the user acts (or BLUETOOTH OFF) */
        if (!ble_on) {
            brc.failed = 0;
            rc_phase(RC_OFF);
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
                rc_failed("FAILED: ", "BUSY");
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
                if (brc.tries > 1u && rc_retry())
                    return;                       /* (an attempt made again, not heard in its time: the next) */
                BLE_DG(ble_dgc.rc_fails++);
                rc_failed_why(brc.tries > 1u ? rc_why(BLE_CF_LOST) : "NOT FOUND");
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
