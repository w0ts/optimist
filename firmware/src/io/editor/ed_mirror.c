/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the screen mirrored (builder item ED_MIRROR; included by editor.c; web/EDITOR_PROTOCOL.md "The screen mirrored";
 * the encoder ed_mirror_enc.h; the client ios/OptimistMirror). While a client asks, every write to the panel also goes out, so a phone
 * shows the panel as it is drawn. No frame buffer on the device. Two links, the one the last request came on:
 *   USB        the bytes go into usb.c's SysEx ring three at a time as they are encoded (a USB-MIDI packet each); the main loop waits
 *              on the ring only while the host reads (at most MIR_STALL_MS without progress)
 *   Bluetooth  (with BLE) whole frames into a ring of their own in the pool (MIR_BLE_RING), which the radio drains between
 *              the channel messages (io/midi/midi_ble.c ble_app_midi_peek); the main loop never waits for it
 * A link that falls behind (the host stops reading; the ring is full, or over Bluetooth holds MIR_BLE_LAG) drops what is drawn and catches up once the link is clear
 * again: black to the client, then every band drawn again in place (op_repaint: the panel itself is not cleared, no flash; the
 * host test's catch-ups give the panel's pixels every time). Never under PRESETS' popup list (the panel under it is not drawn
 * again): it waits for the list to close. A first on catches up the same way. So a slow link shows small changes as they come and
 * whole screens at the rate it carries. The mixer's meters go over USB at most every MIR_METER_MS, all of them in one frame, those
 * that changed since the client last had them (op_mixdraw.c mx_vu_at, mir_mute between; the user, 2026-10-23: "given it is super
 * responsive, we could send the meter ... 5 or 10 times per sec"), never over Bluetooth (the user, 2026-10-23: "let's try to make
 * BT work... without vu meters"). Nothing while no client asks.
 *   106 MIRROR  bits -> on, version, w (2 x 7 bit), h (2 x 7 bit)
 *       bits  bit 0 on (0: off), bit 1 the whole panel again
 *       (opti_miraa_1) a last byte: bit 0 the AA token, after the bits or bit 4's / 5's two bytes (ed_mirror_enc.h MIR_AA; mir_aa_ask)
 * Mirroring ends 3 s after the last request of any kind (ask again or PING about every 1 s), with 106 0 from its own link (from
 * the other: nothing, the other client's mirror goes on; the reply says on), and when its link goes
 * (a USB reset; no Bluetooth central listening). Over Bluetooth 106 is answered, and INFO (1), 108 PANEL, 109 NAV and 111 CLIP
 * (mir_ble_rx: 18 arguments at most, MBR_CB bytes of them waiting; the main loop runs them as USB's, the reply into the Bluetooth ring: ed_send, mir_bt_reply),
 * nothing else; with BT_PROJ (MBR_PROJ) also the projects' moves: the backup's 43 BK_LIST .. 48 BK_END and 110 PJ_OP, a
 * BK_DATA's chunk (up to MBR_BIGN bytes) in a slot of its own (the client waits for each reply, so one is enough).
 * INFO tag 0x65: 1 byte, the version; 0x66's opti_mirbt_1: PANEL and NAV over Bluetooth too (0x63 was REC's: retired,
 * not reused). */
#include "ed_mirror_enc.h"
#if MIR_SKIP
#define MIR_SUMS 1                      /* the self-check (SUMS, opti_mirsum_1): needs what the client holds (MIR_SKIP) to send a band again alone */
#include "ed_mirror_sum.h"
#define MSUM_CAP() (msum.cap)
#else
#define MIR_SUMS 0
#define MSUM_CAP() 0
#endif
#define MSUM_ASK_ 112u                  /* (ed_mirror_sum.h MSUM_ASK, MSUM_MASKN: the bands request's bits and mask bytes, any build) */
#define MSUM_MASKN_ 5u
static void msum_ask(const uint8_t *m); /* (below: the client's bands request, 106 bits 112) */
#if !MIR_BLE
#define ed_bt 0                         /* (editor.c's flag exists with Bluetooth only: every request came over USB) */
#endif
enum { MIR_STALL_MS = 30, MIR_ALIVE_MS = 3000, MIR_USB = 0, MIR_BT = 1,
       MIR_CATCH_MS = 250,
       MIR_METER_MS = 100,                 /* the meters to a USB client: 10 times a second */               /* catch-ups at most this often: the panel repainted for the mirror 4 times a second at most,
                                            * and a screen whose catch-up is more than the Bluetooth ring retries at that pace */
       MIR_STATE_MS = 20, MIR_STATE_BT_MS = 100,   /* STATE when the panel changed, this far apart at least (USB; Bluetooth) */
       MIR_STATE_IDLE_MS = 500,            /* ... and this often anyway */
       MIR_STATE_N = 29 };                 /* STATE's bytes after its op */
static struct {
    uint8_t on, behind, link;           /* behind: what is drawn is dropped until the catch-up (mir_poll) */
    uint8_t state;                      /* the client asked for STATE (106 bit 2) */
    uint8_t k, acc[3];                  /* USB: the bytes of the next USB-MIDI packet */
    uint8_t pseq, prc, phave;           /* PANEL: the last request's seq, its answer; one since on */
    uint16_t master;                    /* MASTER from the client (108 kind 3): its value + 1; 0 the pot's */
    uint16_t pot;                       /* MASTER's pot, 0..1023 (system/main.c master_pot) */
    uint32_t resets, last_ms;           /* usb.resets when asked; fm1_ms of the last request of any kind */
    uint32_t catch_ms;                  /* fm1_ms of the last catch-up */
    uint32_t meter_ms;                  /* fm1_ms of the last meters sent */
    uint32_t state_ms;                  /* fm1_ms of the last STATE sent */
    uint8_t last[MIR_STATE_N];          /* the last STATE's bytes */
    uint8_t test_req, test_had;         /* 108 kind 5: 1 a test session to start (again), 2 to end, after its reply has gone;
                                         * test_had: the mirror was on in this test session */
    uint32_t test_ms;                   /* fm1_ms of the kind 5 request */
} mir;
/* Why the mirror fell behind, for its catch-up (2026-11-01: the user saw "the whole screen again" often): LAG, frames the
 * device itself dropped (the lag rule, a full ring), only repaints in place (the client holds all it was sent: MIR_SKIP and the
 * write cache send what changed); the others do not know what the client holds and send the whole panel, black first */
enum { MIR_WHY_NONE, MIR_WHY_LAG, MIR_WHY_ASK, MIR_WHY_FIX,
       MIR_WHY_PAGE,                    /* a page's clear (a fill of the whole panel) dropped, cut or lost: a repaint in place draws
                                         * only what the page draws, the rows it leaves black would keep the page before's pixels
                                         * (2026-11-01: SYSTEM > INFO's last line at the foot of HOME on the client, for good) */
       MIR_WHY_N };
static uint8_t mir_why;
static void mir_behind(uint32_t why)
{
    mir.behind = 1;
    if (why > mir_why)
        mir_why = (uint8_t)why;
}
/* the counts the client logs (106 op 5 STATS, after LINK, with checked frames): whole panels by why (+ the UI's own whole-panel
 * fills: a page drawn whole), repaints in place for a lag, repairs of lost frames, the bytes sent, those within 100 ms of a repair */
static struct { uint32_t whole[MIR_WHY_N], page, soft, bytes, fix_bytes, fix_b0, fix_ms; uint8_t fix_open; } mst;
#define MIR_TEST_MAGIC 0x5453544Du      /* "MTST" in felucca.c mir_test_boot: the next start is a test session */
enum { MIR_TEST_REPLY_MS = 100,         /* the restart this long after the request: its answer out first */
       MIR_TEST_WAIT_MS = 15000 };      /* a test session no client mirrors this long after its start: ended */
/* the client's hands off the panel: its keys up, MASTER the pot's again (it went away, or asked: 108 kind 4) */
static void mir_letgo(void)
{
    fm1_remote_clear();
    mir.master = 0;
}
#define MIR_SEEN() (mir.last_ms = fm1_ms)   /* (editor.c, ed_lite.c: each request that comes in) */
static void mir_fix_ask(uint32_t lo, uint32_t hi);   /* (below: frames lo .. hi lost, the radio's interrupts or USB; used with and without BLE) */
static void op_repaint(void);

#if MIR_BLE
static void ed_handle(const uint8_t *f, uint32_t n);   /* (editor.c / ed_lite.c: a request from Bluetooth, mir_poll) */
/* ---- Bluetooth: a byte ring in the pool, frames made visible whole (mbr.w only at F7), drained by the radio's interrupts */
#define MIR_BLE_RING 8192u              /* a catch-up is 1.65 KB on average, 6.3 KB at most (version 2; tests/ui_optimist_test.c MIRROR_CHECK) */
#ifndef MIR_BLE_LAG                     /* (tests/mirror_test.c: a smaller one) */
#define MIR_BLE_LAG 4096u               /* outside a catch-up no frame starts past this much waiting: 0.65 s at the 6.2 KB/s an iPad's
                                         * 50-octet packets carry (2026-10-23); more is the screen as it was, sooner sent whole again.
                                         * Above a page drawn whole (1.65 KB on average, the mixer's more): at 1.5 KB a page change
                                         * went over it and came whole twice (the user: "switch ... back to mixer, there is a lag") */
#endif
#ifndef MIR_LEND
#define MIR_LEND 0                      /* the ring lent by the arena while a Bluetooth session uses it (fm1base/optimist/core/opt_lend.h) */
#endif
#if MIR_LEND
/* the ring a block of the arena (core/arena_bufs.c, core/feat_mem.c: "MIRROR 8 KB") only while a Bluetooth client uses it
 * (mir_lend_want: the mirror on over Bluetooth, a request from Bluetooth in the last MIR_ALIVE_MS, frames still going out),
 * else 0: no frame queued (mbr_put refuses: the mirror falls behind and catches up once the ring is there, a reply is
 * asked for again by the client), the radio finds nothing to send (mir_ble_peek). Lent and taken back with the interrupts
 * off (arb_set): the radio's never see it half there */
static uint8_t *mbr_q;
#define MBR_HAVE() (mbr_q != 0)
#if MIR_SKIP
#define MIR_LEND_BYTES (MIR_BLE_RING + MIR_SEG_BYTES)   /* (the ring, then the encoder's segment hashes: ed_mirror_enc.h me_seg) */
#else
#define MIR_LEND_BYTES MIR_BLE_RING
#endif
static uint32_t mbr_seen;               /* fm1_ms of the last request from Bluetooth (mbr_take, the radio's interrupts) */
static uint8_t mbr_no;                  /* the question answered NO (or the mirror switched off for room): not wanted again
                                         * until the central has gone */
#else
static uint8_t mbr_q[MIR_BLE_RING] IN_POOL;
#define MBR_HAVE() 1
#endif
#define MBR_RXN 22u                     /* a request's bytes between F0 and F7: 7D 46 4C cmd + 18 arguments at most (CLIP 2: op,
                                         * track, index, the step's 15) */
#if MBR_PROJ
#define MBR_BIGN 320u                   /* BK_DATA: 7D 46 4C 46, i, offset 3, CRC 5, a 256 B chunk packed 7 bit (293) */
#else
#define MBR_BIGN MBR_RXN
#endif
#define MBR_CB 64u                      /* the requests waiting, each its length then its bytes (8 PANELs, or 2 CLIP writes and
                                         * more): PANEL's down / up pairs and a knob's drag come quicker than a main-loop pass.
                                         * A power of 2 (the counters wrap); rows of 23 would take 184 B of the pool's headroom */
static struct {
    volatile uint32_t w, r;             /* w: the frames written whole; r: the radio's read position */
    uint32_t fw;                        /* the frame being written */
    uint32_t base;                      /* the end of the last catch-up's frames: MIR_BLE_LAG counts what came after */
    uint8_t pk, mid;                    /* the radio's side: bytes in the packet peeked; inside a frame (a channel message would end it) */
    volatile uint8_t req;               /* 0x80 | bits: a request came in (mir_ble_rx), for the main loop */
    volatile uint8_t req_aa;            /* its form << 1 | its second byte's bit 0 (mir_aa_ask), written before req */
    uint16_t rx_n;                      /* the request being received: 7D 46 4C cmd, its arguments (rx) */
    uint8_t big_n;                      /* MBR_PROJ: a request too long for mbr_cb waits in mbr_big (0: none) */
    volatile uint32_t cw, cr;           /* the INFO / PANEL / NAV / CLIP requests waiting (mbr_cb, bytes), for the main loop */
} mbr;
static uint8_t mbr_cb[MBR_CB] IN_POOL;   /* each: its length, then 7D 46 4C cmd args (in the pool: RAM is full) */
static uint8_t mbr_rx[MBR_BIGN] IN_POOL;  /* the request being received */
#if MBR_PROJ
static uint8_t mbr_big[MBR_BIGN] IN_POOL;   /* a long request (BK_DATA) for the main loop; mbr.big_n its length */
static uint16_t mbr_big_len;
static int mbr_cmd_ok(uint32_t cmd)       /* the projects' moves: the backup's 43..48, 110 PJ_OP */
{
    return (cmd >= 43u && cmd <= 48u) || cmd == 110u;
}
#else
#define mbr_cmd_ok(cmd) 0
#endif
static volatile uint8_t mbr_acked;       /* an ACK alone came (106 bits 5 and 6): the client is there, mir_poll */
static uint8_t mbr_reply;                /* a reply being queued (mir_bt_reply): never held back by MIR_BLE_LAG, only a full ring
                                         * stops it (the user, 2026-10-31: INFO, PJ_OP and the library's replies were dropped 1 in 3
                                         * while the screen streamed: the client asked again and again, Projects never answered) */
/* The lag rule follows the link (2026-11-01): what the radio drained while frames waited, measured each MIR_LAG_WIN_MS, gives
 * the bytes MIR_LAG_MS of waiting holds, between MIR_LAG_MIN (above a page drawn whole, see MIR_BLE_LAG) and MIR_BLE_LAG. A slow
 * link (the Mac's 7.5 ms, 4.6 KB/s: 2.5 KB, 0.55 s instead of 0.9) catches up sooner; a fast one keeps MIR_BLE_LAG */
#ifndef MIR_LAG_ADAPT
#define MIR_LAG_ADAPT 1
#endif
#if MIR_LAG_ADAPT
enum { MIR_LAG_MS = 500, MIR_LAG_WIN_MS = 250, MIR_LAG_MIN = 2560 };
static struct { uint32_t t, r, rate; uint8_t busy; } mbl;   /* the window's start, mbr.r then; bytes a second; frames waited */
static uint32_t mbr_lag(void)
{
    uint32_t dt = fm1_ms - mbl.t, lag, lo = MIR_LAG_MIN < MIR_BLE_LAG ? MIR_LAG_MIN : MIR_BLE_LAG;
    if (dt >= MIR_LAG_WIN_MS) {
        if (mbl.busy && mbr.r != mbr.w) {   /* (waited at both ends: the link ran full) */
            uint32_t now = (mbr.r - mbl.r) * 1000u / dt;
            mbl.rate = mbl.rate ? (mbl.rate * 3u + now) / 4u : now;
        }
        mbl.t = fm1_ms, mbl.r = mbr.r, mbl.busy = mbr.r != mbr.w;
    }
    if (!mbl.rate)
        return MIR_BLE_LAG;
    lag = mbl.rate * MIR_LAG_MS / 1000u;
    return lag < lo ? lo : lag > MIR_BLE_LAG ? MIR_BLE_LAG : lag;
}
#else
#define mbr_lag() MIR_BLE_LAG
#endif
/* The Bluetooth stream paced to a budget (2026-11-01, AIMD): pushed at the link's limit (the lag rule above: 2.5-4 KB waiting) the
 * Mac's MIDI input lost whole frames in bursts, 1-4 a second, each answered by repairs that pushed more. Now the frames go out
 * only within a budget in bytes a second (a token bucket: the replies count too, never held back), and the ring is a short
 * transmit queue (MBA_Q_MS of the budget). A frame the budget refuses puts the mirror behind (MIR_WHY_LAG): nothing drawn is
 * queued meanwhile, and once the budget has refilled the panel is repainted in place, MIR_SKIP sending only what the client
 * does not hold (the encoder's segment hashes and whole writes are the dirty map: a write dropped never reached them, a frame
 * lost is forgotten there by mir_fix). The latest pixels win, nothing falls behind: a busy screen just updates less often.
 * The budget: from MBA_START x1.5 each MBA_SS_MS it was spent until the first loss (slow start), then halved on a loss (a repair asked, 106 bit 4, or the client's loss count risen, 106 bit 5; once per MBA_HOLD_MS:
 * the losses of frames sent before the cut are not counted twice), raised a tenth each second it was spent and nothing was
 * lost, between MBA_FLOOR and the link's octets a connection event (MBA_CEIL at most) */
#ifndef MIR_AIMD
#define MIR_AIMD 0                              /* (2026-11-01: off on the FM-1: the budget throttled a healthy link, 10 s a screen; the user) */
#endif
#if MIR_AIMD
enum { MBA_FLOOR = 1000, MBA_CEIL = 20000, MBA_START = 3000, MBA_SS_MS = 250, MBA_BURST_MS = 200, MBA_RESUME_MS = 100, MBA_Q_MS = 80,
       MBA_Q_MIN = 1024, MBA_HOLD_MS = 400, MBA_DELAY_MS = 500, MBA_INC_MS = 1000, MBA_CATCH_MS = 100 };
static struct {
    uint32_t rate, t, inc_ms, cut_ms;   /* bytes a second; tokens last added; the last raise or its check; the last cut */
    int32_t tok;                        /* the budget left, in thousandths of a byte */
    uint8_t limited, ack_have, ack_lost;   /* a frame refused since the last raise; the client's loss count, as last read */
    uint8_t ss;                         /* slow start: until the first loss, x1.5 each MBA_SS_MS it was spent */
    volatile uint8_t loss;              /* (the radio's interrupts) a loss reported, for mba_poll */
    volatile uint8_t ack_seq, ack_new;  /* (the radio's interrupts) the client's next seq, an ACK not yet read */
    uint8_t ack_last;                   /* the next seq the last ACK read had (the delay measured only when it moved: an idle
                                         * screen's last frame grows old, it is not late) */
    uint32_t delay, delays;             /* the last ACK's delay (ms, the frame before ack_seq: sent to acknowledged); cuts for it */
    uint32_t cuts, ups, refused;        /* the counts the client logs (106 op 6) */
} mba;
static int mba_on(void) { return mir.link == MIR_BT; }
/* the ceiling: a quarter over the link's pace as measured (mbr_lag), MBA_CEIL until it is. The link's own pace is not known ahead (one 50-octet packet an event, 5 KB/s, with a Mac's 7.5 ms;
 * 13-19 KB/s with the refill within an event, fm1base BLE_LL_REFILL), so the queue's limit stops what the link does not take
 * (mba_q: a refusal, no loss) and the losses stop what the Mac does not */
/* (2026-11-01, on the FM-1: the measured pace, ~2.6 KB/s, held the budget at 3280 B/s with 0 raises while the client's ACKs came
 * 28-150 ms after their frames and nothing was lost: a 9 KB screen took 3 s. The losses and the ACK delay are the signals; the
 * pace measured is no ceiling) */
static uint32_t mba_ceil(void) { return MBA_CEIL; }
static void mba_fill(void)
{
    uint32_t dt = fm1_ms - mba.t;
    int32_t cap = (int32_t)(mba.rate * MBA_BURST_MS);
    if (!mba.rate) {                    /* (a new session: the start) */
        mba.rate = MBA_START < mba_ceil() ? MBA_START : mba_ceil();
        mba.t = mba.inc_ms = fm1_ms, mba.cut_ms = fm1_ms - MBA_HOLD_MS, mba.tok = 0, mba.ss = 1;
        return;
    }
    mba.t = fm1_ms;
    mba.tok += (int32_t)(mba.rate * (dt < 1000u ? dt : 1000u));
    cap = cap < 600000 ? 600000 : cap;  /* (600 B at least: two cut frames) */
    if (mba.tok > cap)
        mba.tok = cap;
}
static uint32_t mba_q(void) { uint32_t q = mba.rate * MBA_Q_MS / 1000u; return q < MBA_Q_MIN ? MBA_Q_MIN : q; }
/* a frame may start now (else refused: the mirror falls behind, the repaint sends what changed) */
static int mba_admit(void)
{
    mba_fill();
    if (mba.tok >= 0 && mbr.w - mbr.r <= mba_q())
        return 1;
    if (mba.tok < 0)
        mba.limited = 1;                /* (the budget held it back: a raise may follow, up to the link's pace, mba_ceil) */
    mba.refused++;
    return 0;
}
/* the repaint may start: the budget refilled for MBA_RESUME_MS (300 B at least), the queue half empty */
static int mba_ready(void)
{
    int32_t want = (int32_t)(mba.rate * MBA_RESUME_MS);
    mba_fill();
    return mba.tok >= (want < 300000 ? 300000 : want) && mbr.w - mbr.r <= mba_q() / 2u;
}
static void mba_ack(uint32_t lost, uint32_t seq)   /* (the radio's interrupts: 106 bit 5) */
{
    mba.ack_seq = (uint8_t)(seq & 127u), mba.ack_new = 1;
    if (mba.ack_have && (lost & 127u) != mba.ack_lost)
        mba.loss = 1;
    mba.ack_lost = (uint8_t)(lost & 127u), mba.ack_have = 1;
}
static void mba_poll(void)              /* (mir_poll: the cut and the raise) */
{
    if (!mba_on() || !mir.on)
        return;
    mba_fill();
    if (mba.ack_new) {                  /* (the time the client's last frame took: a queue past the FM-1, in the Mac or on the
                                         * air, shows here long before a loss does; 2026-11-01: CoreMIDI saw packets 2.2 s late) */
        uint32_t k = (mba.ack_seq - 1u) & (MIR_RR - 1u);
        mba.ack_new = 0;
        if (mba.ack_seq != mba.ack_last && me_rr[k].seq == (((mba.ack_seq - 1u) & 127u) | 0x80u)) {
            mba.delay = (uint16_t)((uint16_t)fm1_ms - me_rr[k].ms);
            if (mba.delay > MBA_DELAY_MS && fm1_ms - mba.cut_ms >= MBA_HOLD_MS)
                mba.loss = 1, mba.delays++;   /* (late: as a loss, the budget halved) */
        }
        mba.ack_last = mba.ack_seq;
    }
    if (mba.loss) {
        mba.loss = 0;
        if (fm1_ms - mba.cut_ms >= MBA_HOLD_MS) {
            mba.rate = mba.rate / 2u < MBA_FLOOR ? MBA_FLOOR : mba.rate / 2u;
            mba.cut_ms = mba.inc_ms = fm1_ms;
            mba.tok = mba.tok > 0 ? 0 : mba.tok;
            mba.cuts++;
            mba.ss = 0;
        }
    }
    if (fm1_ms - mba.inc_ms >= (mba.ss ? (uint32_t)MBA_SS_MS : (uint32_t)MBA_INC_MS)) {
        uint32_t up = mba.ss ? mba.rate / 2u : mba.rate / 10u < 50u ? 50u : mba.rate / 10u, c = mba_ceil();
        if (mba.limited && mba.rate < c) {
            mba.rate = mba.rate + up > c ? c : mba.rate + up;
            mba.ups++;
        }
        mba.limited = 0;
        mba.inc_ms = fm1_ms;
    }
}
#define MBA_ON() mba_on()
static void mba_reset(void) { uint32_t c = mba.cuts, u = mba.ups, r = mba.refused; memset(&mba, 0, sizeof mba); mba.cuts = c, mba.ups = u, mba.refused = r; }
#else
#define mba_reset() ((void)0)
#define MBA_ON() 0
enum { MBA_CATCH_MS = MIR_CATCH_MS };
#define mba_admit() 1
#define mba_ready() 1
#define mba_ack(l, q) ((void)(l), (void)(q))
#define mba_poll() ((void)0)
#endif
/* A catch-up larger than the ring (2026-11-01, measured on the FM-1 from the Mac: the mixer of mots-config, 8.7-9.5 KB a catch-up,
 * the ring 8 KB) was cut at its last band every time: behind again, the next catch-up cut the same, 2 a second for ever, the
 * client showing the top of the screen only ("stuck there, not updating"). Inside a catch-up a full ring is now waited on while
 * the radio drains it (its interrupts read the ring: progress), given up after MIR_STALL_MS without progress (no central
 * reading). mbr.wait_ms: the time waited in this catch-up, which extends its window (the lag rule and base) by as much */
#ifndef MBR_IDLE
#define MBR_IDLE() ota_idle()
#endif
static uint32_t mbr_wait_ms;
static int mir_bt_up(void);
static int mbr_in_catch(void) { return !MBA_ON() && fm1_ms - mir.catch_ms < MIR_CATCH_MS + mbr_wait_ms; }   /* (paced: a catch-up
                                         * is budgeted as all else, never waited on) */
static void mbr_wait_room(void)
{
    uint32_t t0 = fm1_ms, r = mbr.r, start = fm1_ms;
    while (mbr.fw - mbr.r >= MIR_BLE_RING && MBR_HAVE() && mir_bt_up()) {
        if (mbr.r != r)
            t0 = fm1_ms, r = mbr.r;
        if (fm1_ms - t0 > MIR_STALL_MS)
            break;
        MBR_IDLE();
    }
    mbr_wait_ms += fm1_ms - start;
}
static int mbr_put(uint32_t b)
{
    if (b == 0xF0u) {
        mbr.fw = mbr.w;
        if (MBA_ON()) {
            if (!mbr_reply && !mba_admit())
                return 0;               /* (over the budget: behind, the repaint sends what changed meanwhile) */
        } else if (!mbr_reply && !mbr_in_catch() && mbr.w - ((int32_t)(mbr.base - mbr.r) > 0 ? mbr.base : mbr.r) > mbr_lag())
            return 0;                   /* (behind: the catch-up sends the screen as it is then) */
    }
    if (mbr.fw - mbr.r >= MIR_BLE_RING && mbr_in_catch())
        mbr_wait_room();                /* (a catch-up goes out whole: waited on while the radio drains) */
    if (mbr.fw - mbr.r >= MIR_BLE_RING || !MBR_HAVE())
        return 0;                       /* (full, or no ring: this frame is dropped, mbr.w never saw it) */
    mbr_q[mbr.fw++ % MIR_BLE_RING] = (uint8_t)b;
#if MIR_AIMD
    if (MBA_ON())
        mba.tok -= 1000;                /* (every byte spends the budget, a reply's too) */
#endif
    return 1;
}
static void mbr_end(void)
{
    if (mbr_in_catch())
        mbr.base = mbr.fw;              /* (a catch-up: its own bytes never count, or a slow link would catch up again and again) */
    RING_PUBLISH();
    mbr.w = mbr.fw;
}
/* midi_ble.c: the next USB-MIDI packet of a frame (cin 4, or 5..7 with the F7); 0: none written whole */
static int mir_ble_peek(uint32_t *pkt)
{
    uint32_t i, b[3] = {0, 0, 0}, n = 0;
    if (mbr.r == mbr.w || !MBR_HAVE())
        return 0;
    RING_PUBLISH();
    for (i = 0; i < 3u && mbr.r + i != mbr.w; i++) {
        b[i] = mbr_q[(mbr.r + i) % MIR_BLE_RING];
        n = i + 1u;
        if (b[i] == 0xF7u)
            break;
    }
    mbr.pk = (uint8_t)n;
    *pkt = (b[n - 1u] == 0xF7u ? 4u + n : 4u) | b[0] << 8 | b[1] << 16 | b[2] << 24;
    return 1;
}
static void mir_ble_pop(void)
{
    if (!MBR_HAVE() || !mbr.pk) {
        mbr.pk = 0;                     /* (the ring taken back since the peek: nothing to pop) */
        return;
    }
    mbr.mid = mbr_q[(mbr.r + mbr.pk - 1u) % MIR_BLE_RING] != 0xF7u;
    RING_PUBLISH();
    mbr.r += mbr.pk;
    mbr.pk = 0;
}
static int mir_ble_mid(void) { return mbr.mid && mbr.r != mbr.w; }   /* (a frame begun goes on before any channel message) */
static void mir_ble_flush(void) { mbr.r = mbr.w, mbr.mid = 0, mbr.pk = 0; }   /* (no central listening) */
/* a whole request in rx (rx_n bytes): 106 for mir_poll's own answer; INFO, PANEL, NAV, CLIP queued; anything else dropped */
static void mbr_take(void)
{
    uint32_t cmd = mbr_rx[3], i;
    if (mbr.rx_n < 4u || mbr_rx[0] != 0x7Du || mbr_rx[1] != 0x46u || mbr_rx[2] != 0x4Cu)
        return;
#if MIR_LEND
    mbr_seen = fm1_ms;                  /* (a reply follows: the ring wanted, mir_lend_want) */
#endif
    if (cmd == ED_MIRROR) {
        if (mbr.rx_n == 5u + MSUM_MASKN_ && (mbr_rx[4] & MSUM_ASK_) == MSUM_ASK_) {
            msum_ask(mbr_rx + 5);       /* (the bands that differ, or none: SUMS wanted; keeps the mirror alive as an ACK) */
            mbr_acked = 1;
            return;
        }
        uint32_t lng = mbr.rx_n == 7u || (MIR_AA && mbr.rx_n == 8u);   /* (bits 4, 5: two bytes follow; opti_miraa_1: and its byte) */
        if (mbr.rx_n == 5u || (MIR_AA && mbr.rx_n == 6u) || (lng && (mbr_rx[4] & (MIR_FIX_BIT | MIR_ACK_BIT)))) {
            if (lng && (mbr_rx[4] & MIR_FIX_BIT))
                mir_fix_ask(mbr_rx[5], mbr_rx[6]);
            else if (lng)
                mba_ack(mbr_rx[5], mbr_rx[6]);   /* (the keep-alive with the client's losses, opti_miraimd_1) */
            if (lng && (mbr_rx[4] & MIR_QUIET_BIT) && (mbr_rx[4] & MIR_ACK_BIT))
                mbr_acked = 1;          /* (an ACK alone: kept alive, no reply; mir_poll) */
            else {
                mbr.req_aa = (uint8_t)((mbr.rx_n - 4u) << 1 | (mbr.rx_n == 6u || mbr.rx_n == 8u ? mbr_rx[mbr.rx_n - 1u] & 1u : 0u));
                mbr.req = (uint8_t)(0x80u | (mbr_rx[4] & ~(MIR_FIX_BIT | MIR_ACK_BIT | MIR_QUIET_BIT)));
            }
        }
        return;
    }
    if (cmd != 1u && cmd != ED_PANEL && cmd != ED_NAV && cmd != 111u && !mbr_cmd_ok(cmd))
        return;
#if MBR_PROJ
    if (mbr.rx_n > MBR_RXN) {           /* (a long one: its own slot; busy: dropped, the client asks again) */
        if (!mbr_cmd_ok(cmd))
            return;                     /* (only the projects' moves come long) */
        if (!mbr.big_n) {
            for (i = 0; i < mbr.rx_n; i++)
                mbr_big[i] = mbr_rx[i];
            mbr_big_len = mbr.rx_n;
            RING_PUBLISH();
            mbr.big_n = 1;
        }
        return;
    }
#endif
    if (mbr.cw - mbr.cr + 1u + mbr.rx_n > MBR_CB)
        return;                         /* (full: dropped; the client's request times out and is sent again) */
    mbr_cb[mbr.cw % MBR_CB] = mbr.rx_n;
    for (i = 0; i < mbr.rx_n; i++)
        mbr_cb[(mbr.cw + 1u + i) % MBR_CB] = mbr_rx[i];
    RING_PUBLISH();
    mbr.cw += 1u + mbr.rx_n;
}
/* the radio's interrupts: a SysEx packet that came in; F0 7D 46 4C cmd args F7 taken whole (mbr_take), the main loop answers it */
static void mir_ble_rx(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u, i;
    for (i = 0; i < n; i++) {
        uint32_t b = (pkt >> (8u + 8u * i)) & 0xFFu;
        if (b == 0xF0u)
            mbr.rx_n = 0;
        else if (b == 0xF7u) {
            if (mbr.rx_n <= MBR_BIGN)
                mbr_take();
            mbr.rx_n = MBR_BIGN + 1u;   /* (nothing until the next F0) */
        } else if (mbr.rx_n < MBR_BIGN && b < 0x80u)
            mbr_rx[mbr.rx_n++] = (uint8_t)b;
        else
            mbr.rx_n = MBR_BIGN + 1u;   /* (too long, or a status inside: not ours) */
    }
}
/* ed_send over Bluetooth (ed_bt): the reply F0 .. F7 into the ring as one frame */
static void mir_bt_reply(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    mbr_reply = 1;
    for (i = 0; i < n && mbr_put(p[i]); i++)
        ;
    mbr_reply = 0;
    if (i == n)
        mbr_end();
}
static int mir_bt_up(void) { return ble_out_on; }
static int mir_bt_clear(void) { return mbr.r == mbr.w; }
#if MIR_LEND
/* the arena's hooks (fm1base/optimist/core/opt_lend.h): the ring wanted while a central listens and a client uses it
 * (the mirror on over Bluetooth, a request in the last MIR_ALIVE_MS) or frames still go out; never after a NO until the
 * central has gone. mir_lend: lent (p) or taken back (0), the interrupts off: nothing queued from before */
static int mir_lend_want(void)
{
    if (!mir_bt_up())
        mbr_no = 0;
    return mir_bt_up() && !mbr_no &&
           ((mir.on && mir.link == MIR_BT) || fm1_ms - mbr_seen < MIR_ALIVE_MS || mbr.r != mbr.w);
}
static void mir_lend(void *p)
{
    mbr_q = (uint8_t *)p;
#if MIR_SKIP
    memset(me_known, 0, sizeof me_known);     /* (the hashes start again with the block, or are gone with it) */
    me_seg = p ? (uint32_t (*)[MIR_NSEG])((uint8_t *)p + MIR_BLE_RING) : 0;
#endif
    mbr.fw = mbr.w = mbr.r;
    mbr.base = mbr.r, mbr.pk = 0, mbr.mid = 0;
    if (mir.on && mir.link == MIR_BT)
        mir_behind(MIR_WHY_ASK);        /* (what went before is gone: the screen sent whole again) */
}
static void mir_lend_off(void)          /* (its question's NO, or the offer's YES: the Bluetooth mirror ends) */
{
    mbr_no = 1;
    if (mir.link == MIR_BT && mir.on)
        mir.on = 0, mir_letgo();
}
static int mir_lend_on(void) { return mir_lend_want(); }   /* (core/feat_mem.c FMEM_ON: a client wants it) */
#endif
#else
#define MBA_ON() 0
enum { MBA_CATCH_MS = MIR_CATCH_MS };
#define mba_ready() 1
#define mba_poll() ((void)0)
static int mbr_put(uint32_t b) { (void)b; return 0; }
static void mbr_end(void) {}
static int mir_bt_up(void) { return 0; }
static int mir_bt_clear(void) { return 1; }
#endif

static int mir_link_up(void)
{
    return mir.link == MIR_BT ? mir_bt_up() : usb.config && mir.resets == usb.resets;
}
static int mir_live(void)
{
    if (mir.on && (!mir_link_up() || fm1_ms - mir.last_ms > MIR_ALIVE_MS))
        mir.on = 0, mir_letgo();          /* (its keys up: none stays held once the client is gone) */
    return mir.on && !mir.behind && !mir_mute;
}
/* USB: one USB-MIDI SysEx packet (cin 4: three bytes go on; 5..7: one to three, the last F7) into the ring, waiting while the host reads */
static int mir_pkt(uint32_t cin)
{
    uint32_t t0 = fm1_ms, r = so_r;
    while (so_w - so_r >= SXQ) {
        if (so_r != r)
            t0 = fm1_ms, r = so_r;
        if (!usb.config || fm1_ms - t0 > MIR_STALL_MS) {
            sx_busy = 0;
            return 0;
        }
        ota_idle();
    }
    sx_out_q[so_w % SXQ] = cin | (uint32_t)mir.acc[0] << 8 | (uint32_t)mir.acc[1] << 16 | (uint32_t)mir.acc[2] << 24;
    RING_PUBLISH();
    so_w++;
    mir.k = 0;
    return 1;
}
static int mir_wire(uint32_t b)
{
    if (mir.link == MIR_BT) {
        if (mbr_put(b)) {
            mst.bytes++;
            return 1;
        }
        mir_behind(MIR_WHY_LAG);
        return 0;
    }
    mst.bytes++;
    if (b == 0xF0u)
        sx_busy = 1, mir.k = 0;         /* (no other MIDI out inside the frame: usb.c ep1_tx) */
    if (mir.k == 3u && !mir_pkt(4)) {   /* (a full packet goes when the next byte comes: the last one carries F7) */
        mir_behind(MIR_WHY_LAG);        /* (the host's next F0 drops the frame cut here) */
        return 0;
    }
    mir.acc[mir.k++] = (uint8_t)b;
    return 1;
}
static void mir_wire_end(void)
{
    uint32_t k = mir.k;
    if (mir.link == MIR_BT) {
        mbr_end();
        return;
    }
    mir.acc[1] = k > 1u ? mir.acc[1] : 0u;
    mir.acc[2] = k > 2u ? mir.acc[2] : 0u;
    if (!mir_pkt(4u + k))
        mir_behind(MIR_WHY_LAG);
    sx_busy = 0;
}
/* display/lcd.c: what it drew (the colour as given, the pixels as the panel takes them) */
static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
#if MIR_SUMS
    if (msum.cap && !mir_mute)
        msum_see(x, y, w, h, 0, c);     /* (a frame the self-check hashes: ed_mirror_sum.h) */
#endif
    if (mir_live()) {
        mst.page += w == 240u && h == 240u;  /* (the UI drew a page whole: ui.force) */
        mir_enc_fill_skip(x, y, w, h, c);
    }
    if (w == 240u && h == 240u && mir.on && mir.behind && !mir_mute)
        mir_behind(MIR_WHY_PAGE);       /* (a page's clear the client never got: the catch-up sends the panel whole, black first) */
}
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
#if MIR_SUMS
    if (msum.cap && !mir_mute)
        msum_see(x, y, w, h, px, 0);
#endif
    if (mir_live())
        mir_enc_blit_skip(x, y, w, h, px);   /* (what the client already has: not again) */
}
/* op_mixdraw.c, once a mixer frame: the meters may go to the client this frame (USB only) */
static uint32_t mir_meters_due(void)
{
    if (mir.link != MIR_USB || !mir_live() || (fm1_ms - mir.meter_ms < MIR_METER_MS && !MSUM_CAP()))
        return 0;                       /* (a frame the self-check hashes: its meters go too, the client holds what is hashed) */
    mir.meter_ms = fm1_ms;
    return 1;
}
/* op_mixdraw.c: the client takes no meters at all, not even in a page drawn whole (Bluetooth) */
static uint32_t mir_meters_off(void)
{
    return mir.on && mir.link == MIR_BT;
}
/* 106's last byte with opti_miraa_1 (2026-11-01): bit 0, the AA token (ed_mirror_enc.h MIR_AA), after the bits (form 2) or after
 * bit 4's or 5's two bytes (form 4) sets it; a request without it clears it (form 1: an older client gets today's tokens byte for
 * byte) or, with bits 4, 5 (form 3), keeps it while mirroring. Its pixels are the same either way: nothing is drawn again */
static void mir_aa_ask(uint32_t form, uint32_t ext)
{
#if MIR_AA
    if (form == 2u || form == 4u)
        me.aa = (uint8_t)(ext & 1u);
    else if (form != 3u || !mir.on)
        me.aa = 0;
#else
    (void)form, (void)ext;
#endif
}
/* a request on link l: on / off, the whole panel again (bit 1), the panel's STATE too (bit 2), checked frames (bit 3); a first on
 * catches up */
static void mir_ask(uint32_t bits, uint32_t l)
{
    MIR_SEEN();
    mir_live();                         /* (a mirror that ended: this is a first on again) */
    if (!(bits & 1u) && mir.on && mir.link != l)
        return;                         /* (off from a link the mirror is not on: another client's, left as it is. 2026-11-01: a USB
                                         * script's 106 0 at its end switched off the app's mirror over Bluetooth, its keys let go) */
    if ((bits & 1u) && (!mir.on || mir.link != l || (bits & 2u)))
        mir_behind(MIR_WHY_ASK), mir.catch_ms = fm1_ms - MIR_CATCH_MS;   /* (asked: at once) */
    if ((bits & 1u) && !mir.on)
        mir.phave = 0;                  /* (a new client: no PANEL seq known) */
    if ((bits & 4u) && !mir.state)
        mir.state_ms = fm1_ms - MIR_STATE_IDLE_MS;   /* (asked: a STATE at once) */
    mir.state = (uint8_t)(bits >> 2 & 1u);
    me.chk = (uint8_t)((bits & MIR_CHK_BIT) != 0u);   /* (bit 3: checked frames, ed_mirror_enc.h) */
    mir.on = (uint8_t)(bits & 1u);
    if (!mir.on)
        mir_letgo();
    mir.link = (uint8_t)l;
    mir.resets = usb.resets;
}
static void mir_fix_ask(uint32_t lo, uint32_t hi);   /* (below; the one above is the Bluetooth build's alone) */
static void ed_mirror(const uint8_t *a, uint32_t na)
{
    if (na == 1u + MSUM_MASKN_ && (a[0] & MSUM_ASK_) == MSUM_ASK_) {   /* the bands request (opti_mirsum_1): no on / off in it */
        MIR_SEEN();
        msum_ask(a + 1);
        ed_b(mir.on);
        ed_b(MIR_VERSION);
        ed_b4(240u & 127u, 240u >> 7, 240u & 127u, 240u >> 7);
        return;
    }
    if ((na == 3u || na == 4u) && (a[0] & MIR_FIX_BIT))
        mir_fix_ask(a[1], a[2]);
    mir_aa_ask(na, na == 2u || na == 4u ? a[na - 1u] : 0u);
    mir_ask(na ? a[0] & ~(MIR_FIX_BIT | MIR_ACK_BIT | MIR_QUIET_BIT) : 0u, MIR_USB);   /* (bit 5 over USB: nothing, no budget there) */
    ed_b(mir.on);
    ed_b(MIR_VERSION);
    ed_b4(240u & 127u, 240u >> 7, 240u & 127u, 240u >> 7);
}
/* 108 PANEL: the client presses the panel as a finger would (the app's keyboard: the FM-1 Emulator's keys), while it mirrors
 * (its keys come up when the mirror ends, mir_live): kind 0 a button by its label (panel.c B_*: FX .. REC, OCT-, OCT+; a
 * calibrated panel too) and 1 a note key (0 = F3 .. 26 = G5), then 1 down / 0 up; kind 2 a knob by its role (EN_*: SELECT,
 * ALGORITHM, PRESETS, KNOB 1..4), then its detents + 64 (+ clockwise). Reply: kind, 0 done / 1 refused.
 * With opti_panelx_1 (after fm1-base's remote, device/mirror.c cmd 0x54): kind 3 MASTER, its value 0..1023 as 2 x 7 bit (1024 and
 * up: the pot's again), played instead of the pot's until the mirror ends; kind 4 let go (every key up, MASTER the pot's); and a
 * fourth byte, seq: a request with the seq of the one before is answered again, not done twice (its answer lost, the client
 * asks again). The reply then carries the seq too */
static void ed_panel(const uint8_t *a, uint32_t na)
{
    uint32_t ok = mir.on && na >= 3u, again = na >= 4u && mir.phave && a[3] == mir.pseq;
    if (again)
        ok = !mir.prc;
    else if (ok && a[0] == 0u && a[1] < NB)
        fm1_remote_key(panel.btn[a[1]], a[2] & 1u);
    else if (ok && a[0] == 1u && a[1] < 27u)
        fm1_remote_key(14u + a[1], a[2] & 1u);
    else if (ok && a[0] == 2u && a[1] < NE)
        fm1_remote_enc(panel.enc[a[1]], ((int32_t)a[2] - 64) * panel.dir[a[1]]);
    else if (ok && a[0] == 3u) {
        uint32_t v = a[1] | (uint32_t)a[2] << 7;
        mir.master = (uint16_t)(v < 1024u ? v + 1u : 0u);
    } else if (ok && a[0] == 4u)
        mir_letgo();
    else if (ok && a[0] == 5u && !ed_bt && (a[1] == 1u || st_blank)) {
        mir.test_req = (uint8_t)(a[1] == 1u ? 1u : 2u);   /* (opti_mtest_1: start, again, or end; the restart in mir_poll) */
        mir.test_ms = fm1_ms;
    } else
        ok = 0;
    if (na >= 4u && !again)
        mir.pseq = a[3], mir.prc = (uint8_t)!ok, mir.phave = 1;
    ed_b(na ? a[0] : 0u);
    ed_b(!ok);
    if (na >= 4u)
        ed_b(a[3]);
}
/* system/main.c master_pot: MASTER's pot (0..1023) -> what plays, the client's value while it sets one */
static uint32_t mir_master(uint32_t pot)
{
    mir.pot = (uint16_t)pot;
    return mir.on && mir.master ? mir.master - 1u : pot;
}
/* a key id's LED (0..13 a button, 14..40 a note key) in m (fm1_led, fm1_led_dim: by column, a row bit each) */
static uint32_t mir_led(const uint8_t *m, uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return m[p] >> r & 1u;
    return 0;
}
static void mir_bits(uint8_t *b, uint32_t v, uint32_t n)   /* v as n 7-bit bytes, the lowest first */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] = (uint8_t)(v >> (7u * i) & 127u);
}
/* the panel's state now, as STATE carries it (MIR_STATE_N bytes; the knobs' detents taken from fm1_turned) */
static void mir_state_now(uint8_t *b)
{
    uint8_t dim[FM1_NCOL];
    uint32_t i, held = 0, lit = 0, dimb = 0, litk = 0, dimk = 0;
    for (i = 0; i < FM1_NCOL; i++)
        dim[i] = (uint8_t)(fm1_led_dim[i] | fm1_led_half[i]);
    for (i = 0; i < NB; i++) {
        held |= (fm1_in.buttons >> panel.btn[i] & 1u) << i;
        lit |= mir_led(fm1_led, panel.btn[i]) << i;
        dimb |= mir_led(dim, panel.btn[i]) << i;
    }
    lit |= (uint32_t)(fm1_led[OP_LAMP_COL] >> OP_LAMP_ROW & 1u) << 14;   /* PLAY's green lamp (op_input.c) */
    for (i = 0; i < 27u; i++) {
        litk |= mir_led(fm1_led, 14u + i) << i;
        dimk |= mir_led(dim, 14u + i) << i;
    }
    mir_bits(b, held, 2), mir_bits(b + 2, fm1_in.notes, 4);
    mir_bits(b + 6, lit, 3), mir_bits(b + 9, litk, 4);
    mir_bits(b + 13, dimb, 2), mir_bits(b + 15, dimk, 4);
    for (i = 0; i < NE; i++) {
        int32_t s = fm1_turned[panel.enc[i]] * panel.dir[i];
        b[19 + i] = (uint8_t)(s < -63 ? 1 : s > 63 ? 127 : s + 64);
    }
    mir_bits(b + 26, mir.pot, 2);
    b[28] = (uint8_t)((mir.on && mir.master) | (st_blank ? 2u : 0u));
}
/* the room for one STATE frame on the link now (never a frame dropped for it: that would catch the screen up) */
static int mir_state_room(void)
{
#if MIR_BLE
    if (mir.link == MIR_BT)
        return MIR_BLE_RING - (mbr.fw - mbr.r) >= 64u && (!MBA_ON() || mba_ready());   /* (paced: within the budget) */
#endif
    return mir.link == MIR_USB && SXQ - (so_w - so_r) >= 16u;
}
/* 106 op 4 STATE, the client asking (106 bit 2; INFO "opti_mstate_1"; after fm1-base's input report, device/mirror.c 0x53): held
 * buttons by label (bit B_*, 2 x 7 bit), held note keys (bit 0 = F3, 4 x 7 bit), the lit buttons (bit 14: PLAY's green lamp, 3 x 7
 * bit), the lit note keys (4 x 7 bit), the dim buttons and keys (the half light: 2 and 4 x 7 bit), the knobs' detents since the
 * last STATE (EN_* order, + 64 each, 1..127), MASTER's pot (0..1023, 2 x 7 bit), 1 when the client's MASTER plays instead.
 * Held is the panel's or the client's (108). Sent when it changed, MIR_STATE_MS apart at least (Bluetooth MIR_STATE_BT_MS), and
 * every MIR_STATE_IDLE_MS anyway; never while the link has no room for it */
static void mir_state(void)
{
    uint8_t b[MIR_STATE_N];
    uint32_t i, turned = 0, gap = mir.link == MIR_BT ? MIR_STATE_BT_MS : MIR_STATE_MS, idle = fm1_ms - mir.state_ms >= MIR_STATE_IDLE_MS;
    if (!mir.on || !mir.state || mir.behind || fm1_ms - mir.state_ms < gap || !mir_state_room())
        return;
    mir_state_now(b);
    for (i = 0; i < NE; i++)
        turned |= b[19 + i] != 64u;
    if (!idle && !turned && !memcmp(b, mir.last, 19) && !memcmp(b + 26, mir.last + 26, MIR_STATE_N - 26u))
        return;
    me.ok = 1;
    me_open(4);
    for (i = 0; i < MIR_STATE_N; i++)
        me_b(b[i]);
    me_close();
    if (!me.ok)
        return;
    for (i = 0; i < FM1_NENC; i++)
        fm1_turned[i] = 0;
    memcpy(mir.last, b, sizeof b);
    mir.state_ms = fm1_ms;
}
/* 109 NAV: the client's touch navigation, while it mirrors (ui/optimist/op_nav.c: each jump by the panel's own path). op, then:
 *   0 STATE              nothing
 *   1 ROW    r           the cursor to row r of the screen's rows (SELECT)
 *   2 TRACK  p           the walk to mixer row p: 0 MASTER, 1..3 T1..T3, 4 DR, 5.. the lanes, then ACCENT (ALGORITHM)
 *   3 OPEN   scr, b      screen scr (SCR_*), on SOUND page button b's family (panel.c B_*; 127: every row)
 *   4 TRACKS             nothing
 * Reply: op, 0 done / 1 refused, then for 0..3 the state: screen, page button (127: none), cursor row, n, the n row names
 * (ASCII, 0-terminated; GLOBAL and SYSTEM: their pages' titles, 20 at most, the cursor the page), the walk's row (127: no track
 * here), then where KNOB 1..4 are on the panel (op_nav.c nav_knobs: 0 nowhere, 1 the 1x4 cards, 2 the 2x2 cards, 3 the four
 * lines, then the first line's top and the lines' pitch; a drag there turns the knob); for 4: n, the n mixer rows' names */
static void ed_nav(const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 0u, ok = mir.on, i, n;
    char b[24];
    if (ok && op == 1u)
        ok = na >= 2u && nav_row(a[1]);
    else if (ok && op == 2u)
        ok = na >= 2u && nav_walk(a[1]);
    else if (ok && op == 3u)
        ok = na >= 3u && nav_open(a[1], a[2]);
    else if (op > 4u)
        ok = 0;
    ed_b2(op, !ok);
    if (op == 4u) {
        ed_b(NAV_NPLACE);                               /* (MASTER, T1 .. the last lane: op_nav.c) */
        for (i = 0; i < NAV_NPLACE; i++)
            nav_track_name(i, b), ed_str(b, 11);
        return;
    }
    n = nav_rows();
    n = n < OP_MAXROWS ? n : OP_MAXROWS;
    ed_b4(ui.scr, nav_mode(), nav_cur(), n);
    for (i = 0; i < n; i++)
        nav_row_name(i, b, sizeof b), ed_str(b, glo_cycle() ? 20u : 11u);
    ed_b(nav_track());
    n = nav_knobs();
    ed_b(n);
    if (n == 3u)
        ed_b2((uint32_t)(GL_TOP + gl_y0()), (uint32_t)gl_pitch());
}
/* The test session (108 kind 5, INFO "opti_mtest_1"; after fm1-base's, device/mirror.c op 5): the FM-1 as a blank one, for a
 * demonstration or an automated test from the client, the user's own flash untouched. It starts with a restart (the request's
 * answer sent first): felucca.c st_blank, set at the start from mir_test_boot, makes Optimist's own data read erased and every
 * write to it a no-op (the radio's calibration and the firmware read as ever; the user's samples, read in place, still sound;
 * sample uploads refused). Asked again: blank again (another restart). It ends with a restart into the user's own: asked (kind 5,
 * 0), the mirror off (asked, or its keep-alive out), or no client mirroring within MIR_TEST_WAIT_MS of the start; a crash ends
 * it too (main.c clears mir_test_boot first thing). USB only: the restart drops a Bluetooth link, and a blank FM-1 has no bond
 * to take it back. STATE's last byte has bit 1 set in a session */
static void mir_test(void)
{
    if (mir.test_req && fm1_ms - mir.test_ms >= MIR_TEST_REPLY_MS) {
        mir_test_boot = mir.test_req == 1u ? MIR_TEST_MAGIC : 0u;
        fm1_reboot();
    }
    if (!st_blank)
        return;
    if (mir.on)
        mir.test_had = 1;
    if ((mir.test_had && !mir.on) || (!mir.test_had && fm1_ms > MIR_TEST_WAIT_MS)) {
        mir_test_boot = 0;
        fm1_reboot();                   /* (the user's own again) */
    }
}
/* Frames lost (106 bit 4, seq lo .. hi, the client's: a sum failed, a gap, a MORE dropped after one): their rectangles forgotten
 * (ed_mirror_enc.h me_rr, MIR_SKIP's knowledge) and the panel repainted in place with no black first: only what the client may
 * lack goes out, the rest is skipped as held. A seq no longer in me_rr, or a range over MIR_RR: the whole panel (a catch-up).
 * The client draws on meanwhile (no whole fill: nothing held). At most one repaint every MIR_FIX_MS, the ranges merged */
enum { MIR_FIX_MS = 60, MIR_FIXQ = 8 };
static struct { uint8_t lo, hi; } mfix_q[MIR_FIXQ];      /* the runs asked, each alone (merging two would forget what came whole) */
static volatile uint8_t mfix_w, mfix_r, mfix_over;       /* (the radio's interrupts write; the main loop reads); over: the whole panel */
static uint8_t mfix_due;                                 /* rectangles forgotten, the repaint not yet done */
static uint32_t mfix_ms, mfix_n, mfix_whole;            /* the last repaint; repaints done; whole panels instead (the tests') */
static void mir_fix_ask(uint32_t lo, uint32_t hi)
{
#if MIR_BLE && MIR_AIMD
    mba.loss = 1;                       /* (a loss: the budget halved, mba_poll) */
#endif
    if ((uint8_t)(mfix_w - mfix_r) >= MIR_FIXQ) {
        mfix_over = 1;
        return;
    }
    mfix_q[mfix_w % MIR_FIXQ].lo = (uint8_t)(lo & 127u), mfix_q[mfix_w % MIR_FIXQ].hi = (uint8_t)(hi & 127u);
    RING_PUBLISH();
    mfix_w++;
}
static void mir_fix(void)
{
    uint32_t whole = mfix_over;
    mfix_over = 0;
    while (mfix_r != mfix_w) {
        uint32_t lo, hi, n, k;
        RING_PUBLISH();
        lo = mfix_q[mfix_r % MIR_FIXQ].lo, hi = mfix_q[mfix_r % MIR_FIXQ].hi;
        mfix_r++;
        n = ((hi - lo) & 127u) + 1u;
        if (n > MIR_RR || !MIR_SKIP)
            whole = 1;
        for (k = 0; k < n && !whole; k++) {
            uint32_t sq = (lo + k) & 127u, e = sq & (MIR_RR - 1u);
            if (me_rr[e].seq != (sq | 0x80u))
                whole = 1;                               /* (overwritten since: which rectangle is not known) */
            else if (me_rr[e].w == 240u && me_rr[e].h == 240u)
                whole = 1;                               /* (a page's clear lost: MIR_WHY_PAGE) */
            else
                mir_enc_forget(me_rr[e].x, me_rr[e].y, me_rr[e].w, me_rr[e].h);
        }
        if (!whole)
            mfix_due = 1;
    }
    if (whole) {
        mfix_whole++;
        mir_behind(MIR_WHY_FIX), mir.catch_ms = fm1_ms - MIR_CATCH_MS;
        mfix_due = 0;
        return;
    }
#if MIR_BLE
    if (mfix_due && MBA_ON()) {         /* (paced: the rectangles forgotten are repainted with the rest, within the budget) */
        mfix_due = 0;
        mfix_n++;
        mir_behind(MIR_WHY_LAG);
        return;
    }
#endif
    if (mfix_due && mir_live() && fm1_ms - mfix_ms >= MIR_FIX_MS) {
        mfix_due = 0;
        mfix_ms = fm1_ms;
        mfix_n++;
        if (!mst.fix_open)
            mst.fix_open = 1, mst.fix_b0 = mst.bytes;
        mst.fix_ms = fm1_ms;
        op_repaint();                                    /* (in place: what the client holds is skipped) */
    }
}

/* The self-check (ed_mirror_sum.h, opti_mirsum_1): about once a second (MSUM_MS), and soon (MSUM_SOON_MS) after a page drawn
 * whole, a repair or a catch-up (MSUM_GAP_MS apart at least), while a client that takes SUMS mirrors with checked frames and the
 * link is not behind: the panel repainted in place (op_repaint) and the next UI frame hashed as it goes out (mir_draw_begin, end),
 * then SUMS (never held back by the lag rule or the budget: 101 bytes). A frame that is not the last check's (its rectangles)
 * proves nothing: the next is checked, MSUM_TRIES at most. The client's bands request: those rows forgotten (MIR_SKIP) and the
 * panel repainted as a repair (mir_fix), only they going out */
#if MIR_SUMS
enum { MSUM_MS = 1000, MSUM_SOON_MS = 300, MSUM_GAP_MS = 500, MSUM_TRIES = 3 };
static struct {
    uint8_t want, arm, tries, soon;     /* the client takes SUMS; the next frame hashed; checks again; a check soon */
    uint32_t ms, soon_ms, page, fixes, sent, bands;   /* the last check; what asked one soon; mst.page, mfix_n as last seen; counts */
} msc;
static volatile uint32_t msum_mask;     /* (the radio's interrupts or USB) bands asked, for mir_poll */
static volatile uint8_t msum_asked;
static void msum_ask(const uint8_t *m)
{
    uint32_t k, v = 0;
    for (k = 0; k < MSUM_MASKN; k++)
        v |= (uint32_t)(m[k] & 127u) << (7u * k);
    msum_mask |= v & ((1u << MSUM_BANDS) - 1u);
    msum_asked = 1;
}
static void msum_soon(void) { msc.soon = 1, msc.soon_ms = fm1_ms; }
static void msum_poll(void)
{
    if (!mir.on) {
        msc.want = 0, msc.arm = 0, msum_mask = 0, msum_asked = 0;
        return;
    }
    if (msum_asked) {
        uint32_t m = msum_mask, b;
        msum_asked = 0, msum_mask = 0, msc.want = 1;
        for (b = 0; b < MSUM_BANDS; b++)
            if (m >> b & 1u)
                mir_enc_forget(0, b * MSUM_ROWS, 240, MSUM_ROWS), msc.bands++;
        if (m)
            mfix_due = 1;               /* (repainted as a repair: only the rows forgotten go out) */
    }
    if (mst.page != msc.page || mfix_n != msc.fixes)
        msc.page = mst.page, msc.fixes = mfix_n, msum_soon();
    if (!msc.want || !me.chk || msc.arm || msum.cap || mfix_due || mfix_r != mfix_w || !mir_live() ||
        (MBA_ON() && !mba_ready()))
        return;
    if (fm1_ms - msc.ms < MSUM_MS && !(msc.soon && fm1_ms - msc.soon_ms >= MSUM_SOON_MS && fm1_ms - msc.ms >= MSUM_GAP_MS))
        return;
    msc.ms = fm1_ms, msc.soon = 0, msc.tries = 0, msc.arm = 1;
    op_repaint();                       /* (the next frame drawn whole: hashed) */
}
static void mir_draw_begin(void)        /* (op_input.c: before the UI frame's drawing) */
{
    if (!msc.arm)
        return;
    msc.arm = 0;
    if (mir_live())
        msum_begin();
}
static void mir_draw_end(void)
{
    uint32_t valid, i;
    uint8_t f[MSUM_LEN];
    if (!msum.cap)
        return;
    if (!msum_end(&valid) || !mir_live()) {
        if (mir_live() && ++msc.tries < MSUM_TRIES)
            msc.arm = 1, op_repaint();  /* (another frame than the last check's: the next one) */
        return;
    }
    msum_frame(f, me.seq, valid);
#if MIR_BLE
    mbr_reply = 1;                      /* (as a reply: never held back) */
#endif
    for (i = 0; i < MSUM_LEN && mir_wire(f[i]); i++)
        ;
#if MIR_BLE
    mbr_reply = 0;
#endif
    if (i == MSUM_LEN)
        mir_wire_end(), msc.sent++;
}
#else
static void msum_ask(const uint8_t *m) { (void)m; }
#define msum_poll() ((void)0)
#define msum_soon() ((void)0)
static void mir_draw_begin(void) {}
static void mir_draw_end(void) {}
#endif

/* Checked frames: a lost frame shows as a gap in seq at the next one, which may come late (an idle screen). So after frames went
 * out, an empty checked MORE (no pixels: seq only) follows within MIR_MARK_MS, and every MIR_MARK_IDLE_MS while the client
 * mirrors: a client that lost the last of them knows */
enum { MIR_MARK_MS = 300,              /* frames went out: the mark this soon after */
       MIR_MARK_IDLE_MS = 1000 };        /* and again this often (a mark lost too: 10 bytes a second) */
static struct { uint32_t ms; uint8_t seq; } mmark;
static void mir_mark(void)
{
    if (!me.chk || !mir_live() || fm1_ms - mmark.ms < (me.seq == mmark.seq ? MIR_MARK_IDLE_MS : MIR_MARK_MS))
        return;
#if MIR_BLE
    if (mir.link == MIR_BT && (MIR_BLE_RING - (mbr.fw - mbr.r) < 64u || (MBA_ON() && !mba_admit())))
        return;
#endif
    me.ok = 1;
    me.rw = 0;                          /* (the mark draws nothing: lost, nothing to send again) */
    me_open_px(2);
    me_close();
    mmark.ms = fm1_ms;
    mmark.seq = me.seq;
}
/* ed_service, every main-loop pass: a request over Bluetooth answered; a link that fell behind caught up once it is clear */
static void mir_poll(void)
{
#if MIR_BLE
    uint32_t q = mbr.req;
    if (mbr_acked) {
        mbr_acked = 0;
        if (mir.on && mir.link == MIR_BT)
            MIR_SEEN();                 /* (an ACK keeps the mirror alive as a request does) */
    }
    if (q && mir_bt_up()) {
        static const uint8_t R[6] = {1, MIR_VERSION, 240u & 127u, 240u >> 7, 240u & 127u, 240u >> 7};
        uint32_t i;
        mbr.req = 0;
        if (!mir.on || mir.link != MIR_BT)
            mba_reset();                /* (a new Bluetooth session: the budget from its start) */
        mir_aa_ask(mbr.req_aa >> 1, mbr.req_aa & 1u);
        mir_ask(q & 0x7Fu, MIR_BT);
        mbr_reply = 1;                  /* (the answers below: never held back by the budget) */
        me.ok = 1;                      /* (the reply: F0 7D 46 4C 6A on 1 w w h h F7, as USB's) */
        me_open(mir.on);
        for (i = 1; i < 6u; i++)
            me_b(R[i]);
        me_close();
        {   /* then op 3 LINK: what the radio agreed, data length, ATT MTU, interval (x 1.25 ms), then the central's own LL_LENGTH
             * values (MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime), 2 x 7 bit each (the app logs it) */
            uint32_t v[7] = {ble_ll_tx_octets(), ble_att_mtu(), ble_ll_interval(), ble_ll_peer_len(0), ble_ll_peer_len(1),
                             ble_ll_peer_len(2), ble_ll_peer_len(3)};
            me_open(3);
            for (i = 0; i < 7u; i++)
                me_b2(v[i]);
            me_close();
        }
        if (me.chk) {   /* then op 5 STATS (the app logs each second's change): whole panels for a lag, an ask (bit 1, a first on), a
                         * repair's seq no longer known; the UI's pages drawn whole; repaints in place for a lag; repairs; the bytes
                         * sent; the bytes within 100 ms of a repair. 3 x 7 bit each, free-running (mod 2^21) */
            uint32_t v[8] = {mst.whole[MIR_WHY_LAG], mst.whole[MIR_WHY_ASK], mst.whole[MIR_WHY_FIX], mst.page, mst.soft, mfix_n,
                             mst.bytes, mst.fix_bytes};
            me_open(5);
            for (i = 0; i < 8u; i++)
                me_col(v[i] & 0x1FFFFFu);
            me_close();
        }
#if MIR_AIMD
        if (me.chk) {   /* then op 6 PACE (opti_miraimd_1; the app logs each second's change): the budget now (bytes a second), its
                         * cuts, its raises, the frames it refused, the last
                         * ACK's delay (ms), the cuts for a delay over MBA_DELAY_MS; 3 x 7 bit each, free-running */
            uint32_t v[6] = {mba.rate, mba.cuts, mba.ups, mba.refused, mba.delay, mba.delays};
            me_open(6);
            for (i = 0; i < 6u; i++)
                me_col(v[i] & 0x1FFFFFu);
            me_close();
        }
#endif
        mbr_reply = 0;
    }
    while (mbr.cr != mbr.cw && mir_bt_up()) {   /* INFO, PANEL, NAV, CLIP from Bluetooth: run as USB's, answered there */
        uint8_t f[MBR_RXN];
        uint32_t n, k;
        RING_PUBLISH();
        n = mbr_cb[mbr.cr % MBR_CB];
        MIR_SEEN();
        ed_bt = 1;
        for (k = 0; k < n; k++)
            f[k] = mbr_cb[(mbr.cr + 1u + k) % MBR_CB];
        ed_handle(f, n);
        ed_bt = 0;
        mbr.cr += 1u + n;
    }
#if MBR_PROJ
    if (mbr.big_n && mir_bt_up()) {     /* a long request (a project's chunk): answered there too */
        RING_PUBLISH();
        MIR_SEEN();
        ed_bt = 1;
        ed_handle(mbr_big, mbr_big_len);
        ed_bt = 0;
        mbr.big_n = 0;
    }
#endif
    if (!mir_bt_up()) {
        mbr.cr = mbr.cw;                /* (no central: nothing to answer) */
#if MBR_PROJ
        mbr.big_n = 0;
#endif
    }
#endif
    mir_live();                         /* (the link gone, the keep-alive out: off) */
    mir_test();
    mir_state();
    mir_fix();
    msum_poll();
    mba_poll();
    if (mst.fix_open && fm1_ms - mst.fix_ms >= 100u)
        mst.fix_open = 0, mst.fix_bytes += mst.bytes - mst.fix_b0;   /* (a repair's bytes: those within 100 ms of it) */
    mir_mark();
    if (!mir.on || !mir.behind || ui.overlay == 5u ||
        (mir.link == MIR_BT ? (MBA_ON() ? !mba_ready() : !mir_bt_clear()) : so_w != so_r) ||
        fm1_ms - mir.catch_ms < (MBA_ON() ? (uint32_t)MBA_CATCH_MS : (uint32_t)MIR_CATCH_MS))
        return;
    mir.behind = 0;
    mir.catch_ms = fm1_ms;
#if MIR_BLE
    mbr_wait_ms = 0;
#endif
    msum_soon();                        /* (a catch-up: checked soon after) */
    if (mir_why == MIR_WHY_LAG && MIR_SKIP) {   /* (only what the device dropped: the client holds the rest, repainted in place) */
        mir_why = MIR_WHY_NONE;
        mst.soft++;
        op_repaint();
        return;
    }
    mst.whole[mir_why]++;
    mir_why = MIR_WHY_NONE;
    mir_enc_forget_all();               /* (a frame may have been lost: nothing known; the black makes every row known black) */
    mir_enc_fill_skip(0, 0, 240, 240, 0);   /* (black to the client only: the panel is not cleared) */
    op_repaint();
}
static void ed_mirror_info(void) { ed_b3(0x65, 1, MIR_VERSION); }
