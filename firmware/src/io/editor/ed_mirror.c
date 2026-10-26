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
 * Mirroring ends 3 s after the last request of any kind (ask again or PING about every 1 s), with 106 0, and when its link goes
 * (a USB reset; no Bluetooth central listening). Over Bluetooth 106 is answered, and INFO (1), 108 PANEL, 109 NAV and 111 CLIP
 * (mir_ble_rx: 18 arguments at most, MBR_CB bytes of them waiting; the main loop runs them as USB's, the reply into the Bluetooth ring: ed_send, mir_bt_reply),
 * nothing else; with BT_PROJ (MBR_PROJ) also the projects' moves: the backup's 43 BK_LIST .. 48 BK_END and 110 PJ_OP, a
 * BK_DATA's chunk (up to MBR_BIGN bytes) in a slot of its own (the client waits for each reply, so one is enough).
 * INFO tag 0x65: 1 byte, the version; 0x66's opti_mirbt_1: PANEL and NAV over Bluetooth too (0x63 was REC's: retired,
 * not reused). */
#include "ed_mirror_enc.h"
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
static uint8_t mbr_q[MIR_BLE_RING] IN_POOL;
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
static int mbr_put(uint32_t b)
{
    if (b == 0xF0u) {
        mbr.fw = mbr.w;
        if (fm1_ms - mir.catch_ms >= MIR_CATCH_MS && mbr.w - ((int32_t)(mbr.base - mbr.r) > 0 ? mbr.base : mbr.r) > MIR_BLE_LAG)
            return 0;                   /* (behind: the catch-up sends the screen as it is then) */
    }
    if (mbr.fw - mbr.r >= MIR_BLE_RING)
        return 0;                       /* (full: this frame is dropped, mbr.w never saw it) */
    mbr_q[mbr.fw++ % MIR_BLE_RING] = (uint8_t)b;
    return 1;
}
static void mbr_end(void)
{
    if (fm1_ms - mir.catch_ms < MIR_CATCH_MS)
        mbr.base = mbr.fw;              /* (a catch-up: its own bytes never count, or a slow link would catch up again and again) */
    RING_PUBLISH();
    mbr.w = mbr.fw;
}
/* midi_ble.c: the next USB-MIDI packet of a frame (cin 4, or 5..7 with the F7); 0: none written whole */
static int mir_ble_peek(uint32_t *pkt)
{
    uint32_t i, b[3] = {0, 0, 0}, n = 0;
    if (mbr.r == mbr.w)
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
    if (cmd == ED_MIRROR) {
        if (mbr.rx_n == 5u)
            mbr.req = (uint8_t)(0x80u | mbr_rx[4]);
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
    for (i = 0; i < n && mbr_put(p[i]); i++)
        ;
    if (i == n)
        mbr_end();
}
static int mir_bt_up(void) { return ble_out_on; }
static int mir_bt_clear(void) { return mbr.r == mbr.w; }
#else
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
        if (mbr_put(b))
            return 1;
        mir.behind = 1;
        return 0;
    }
    if (b == 0xF0u)
        sx_busy = 1, mir.k = 0;         /* (no other MIDI out inside the frame: usb.c ep1_tx) */
    if (mir.k == 3u && !mir_pkt(4)) {   /* (a full packet goes when the next byte comes: the last one carries F7) */
        mir.behind = 1;                 /* (the host's next F0 drops the frame cut here) */
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
        mir.behind = 1;
    sx_busy = 0;
}
/* display/lcd.c: what it drew (the colour as given, the pixels as the panel takes them) */
static void mir_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (mir_live())
        mir_enc_fill_skip(x, y, w, h, c);
}
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (mir_live())
        mir_enc_blit_skip(x, y, w, h, px);   /* (what the client already has: not again) */
}
/* op_mixdraw.c, once a mixer frame: the meters may go to the client this frame (USB only) */
static uint32_t mir_meters_due(void)
{
    if (mir.link != MIR_USB || !mir_live() || fm1_ms - mir.meter_ms < MIR_METER_MS)
        return 0;
    mir.meter_ms = fm1_ms;
    return 1;
}
/* op_mixdraw.c: the client takes no meters at all, not even in a page drawn whole (Bluetooth) */
static uint32_t mir_meters_off(void)
{
    return mir.on && mir.link == MIR_BT;
}
/* a request on link l: on / off, the whole panel again (bit 1), the panel's STATE too (bit 2); a first on catches up */
static void mir_ask(uint32_t bits, uint32_t l)
{
    MIR_SEEN();
    mir_live();                         /* (a mirror that ended: this is a first on again) */
    if ((bits & 1u) && (!mir.on || mir.link != l || (bits & 2u)))
        mir.behind = 1, mir.catch_ms = fm1_ms - MIR_CATCH_MS;   /* (asked: at once) */
    if ((bits & 1u) && !mir.on)
        mir.phave = 0;                  /* (a new client: no PANEL seq known) */
    if ((bits & 4u) && !mir.state)
        mir.state_ms = fm1_ms - MIR_STATE_IDLE_MS;   /* (asked: a STATE at once) */
    mir.state = (uint8_t)(bits >> 2 & 1u);
    mir.on = (uint8_t)(bits & 1u);
    if (!mir.on)
        mir_letgo();
    mir.link = (uint8_t)l;
    mir.resets = usb.resets;
}
static void ed_mirror(const uint8_t *a, uint32_t na)
{
    mir_ask(na ? a[0] : 0u, MIR_USB);
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
        return MIR_BLE_RING - (mbr.fw - mbr.r) >= 64u;
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
        ed_b(MXR_LN);
        for (i = 0; i < MXR_LN; i++)
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
/* ed_service, every main-loop pass: a request over Bluetooth answered; a link that fell behind caught up once it is clear */
static void mir_poll(void)
{
#if MIR_BLE
    uint32_t q = mbr.req;
    if (q && mir_bt_up()) {
        static const uint8_t R[6] = {1, MIR_VERSION, 240u & 127u, 240u >> 7, 240u & 127u, 240u >> 7};
        uint32_t i;
        mbr.req = 0;
        mir_ask(q & 0x7Fu, MIR_BT);
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
    if (!mir.on || !mir.behind || ui.overlay == 5u || (mir.link == MIR_BT ? !mir_bt_clear() : so_w != so_r) ||
        fm1_ms - mir.catch_ms < MIR_CATCH_MS)
        return;
    mir.behind = 0;
    mir.catch_ms = fm1_ms;
    mir_enc_forget_all();               /* (a frame may have been lost: nothing known; the black makes every row known black) */
    mir_enc_fill_skip(0, 0, 240, 240, 0);   /* (black to the client only: the panel is not cleared) */
    op_repaint();
}
static void ed_mirror_info(void) { ed_b3(0x65, 1, MIR_VERSION); }
