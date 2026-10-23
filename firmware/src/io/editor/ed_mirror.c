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
 * (a USB reset; no Bluetooth central listening). Over Bluetooth only 106 is answered (mir_ble_rx). INFO tag 0x65: 1 byte, the version
 * (0x63 was REC's: retired, not reused). */
#include "ed_mirror_enc.h"
enum { MIR_STALL_MS = 30, MIR_ALIVE_MS = 3000, MIR_USB = 0, MIR_BT = 1,
       MIR_CATCH_MS = 250,
       MIR_METER_MS = 100 };               /* the meters to a USB client: 10 times a second */               /* catch-ups at most this often: the panel repainted for the mirror 4 times a second at most,
                                            * and a screen whose catch-up is more than the Bluetooth ring retries at that pace */
static struct {
    uint8_t on, behind, link;           /* behind: what is drawn is dropped until the catch-up (mir_poll) */
    uint8_t k, acc[3];                  /* USB: the bytes of the next USB-MIDI packet */
    uint32_t resets, last_ms;           /* usb.resets when asked; fm1_ms of the last request of any kind */
    uint32_t catch_ms;                  /* fm1_ms of the last catch-up */
    uint32_t meter_ms;                  /* fm1_ms of the last meters sent */
} mir;
#define MIR_SEEN() (mir.last_ms = fm1_ms)   /* (editor.c, ed_lite.c: each request that comes in) */
static void op_repaint(void);

#if MIR_BLE
/* ---- Bluetooth: a byte ring in the pool, frames made visible whole (mbr.w only at F7), drained by the radio's interrupts */
#define MIR_BLE_RING 8192u              /* a catch-up is 1.65 KB on average, 6.3 KB at most (version 2; tests/ui_optimist_test.c MIRROR_CHECK) */
#ifndef MIR_BLE_LAG                     /* (tests/mirror_test.c: a smaller one) */
#define MIR_BLE_LAG 4096u               /* outside a catch-up no frame starts past this much waiting: 0.65 s at the 6.2 KB/s an iPad's
                                         * 50-octet packets carry (2026-10-23); more is the screen as it was, sooner sent whole again.
                                         * Above a page drawn whole (1.65 KB on average, the mixer's more): at 1.5 KB a page change
                                         * went over it and came whole twice (the user: "switch ... back to mixer, there is a lag") */
#endif
static uint8_t mbr_q[MIR_BLE_RING] IN_POOL;
static struct {
    volatile uint32_t w, r;             /* w: the frames written whole; r: the radio's read position */
    uint32_t fw;                        /* the frame being written */
    uint32_t base;                      /* the end of the last catch-up's frames: MIR_BLE_LAG counts what came after */
    uint8_t pk, mid;                    /* the radio's side: bytes in the packet peeked; inside a frame (a channel message would end it) */
    volatile uint8_t req;               /* 0x80 | bits: a request came in (mir_ble_rx), for the main loop */
    uint8_t rx_n, rx[7];                /* the request being received */
} mbr;
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
/* the radio's interrupts: a SysEx packet that came in; only F0 7D 46 4C 6A bits F7 is taken (the main loop answers it) */
static void mir_ble_rx(uint32_t pkt)
{
    static const uint8_t HDR[5] = {0xF0, 0x7D, 0x46, 0x4C, ED_MIRROR};
    uint32_t cin = pkt & 15u, n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u, i;
    for (i = 0; i < n; i++) {
        uint32_t b = (pkt >> (8u + 8u * i)) & 0xFFu;
        if (b == 0xF0u)
            mbr.rx_n = 0;
        if (b == 0xF7u) {
            if (mbr.rx_n == 6u)
                mbr.req = (uint8_t)(0x80u | mbr.rx[5]);
            mbr.rx_n = 7;               /* (nothing until the next F0) */
        } else if (mbr.rx_n < 6u && (mbr.rx_n == 5u ? b < 0x80u : b == HDR[mbr.rx_n])) {
            mbr.rx[mbr.rx_n++] = (uint8_t)b;
        } else {
            mbr.rx_n = 7;
        }
    }
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
        mir.on = 0;
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
        mir_enc_fill(x, y, w, h, c);
}
static void mir_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (mir_live())
        mir_enc_blit(x, y, w, h, px);
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
/* a request on link l: on / off, the whole panel again (bit 1); a first on catches up */
static void mir_ask(uint32_t bits, uint32_t l)
{
    MIR_SEEN();
    mir_live();                         /* (a mirror that ended: this is a first on again) */
    if ((bits & 1u) && (!mir.on || mir.link != l || (bits & 2u)))
        mir.behind = 1, mir.catch_ms = fm1_ms - MIR_CATCH_MS;   /* (asked: at once) */
    mir.on = (uint8_t)(bits & 1u);
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
#endif
    mir_live();                         /* (the link gone, the keep-alive out: off) */
    if (!mir.on || !mir.behind || ui.overlay == 5u || (mir.link == MIR_BT ? !mir_bt_clear() : so_w != so_r) ||
        fm1_ms - mir.catch_ms < MIR_CATCH_MS)
        return;
    mir.behind = 0;
    mir.catch_ms = fm1_ms;
    mir_enc_fill(0, 0, 240, 240, 0);    /* (black to the client only: the panel is not cleared) */
    op_repaint();
}
static void ed_mirror_info(void) { ed_b3(0x65, 1, MIR_VERSION); }
