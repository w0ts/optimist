/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the screen mirrored (builder item ED_MIRROR; included by editor.c; web/EDITOR_PROTOCOL.md "The screen mirrored";
 * the encoder ed_mirror_enc.h; the client ios/OptimistMirror). While a client asks, every write to the panel also goes out over USB,
 * so a phone shows the panel as it is drawn. No frame buffer: the bytes go into the SysEx ring three at a time as they are encoded
 * (a USB-MIDI packet each), and the main loop waits on the ring only while the host reads; a host that stops reading for
 * MIR_STALL_MS ends the mirror (the client asks again: the panel is drawn whole again). The meters are not sent (mir_mute, display/lcd.c,
 * set around the mixer's VU canvases: the user, 2026-10-22, "we don't need vu meters"), nor anything while no client asks.
 *   106 MIRROR  bits -> on, version, w (2 x 7 bit), h (2 x 7 bit)
 *       bits  bit 0 on (0: off), bit 1 the whole panel again (ui.force); a first on draws it whole anyway
 * Mirroring ends 3 s after the last request of any kind (ask again or PING about every 1 s), on a USB reset and with 106 0.
 * INFO tag 0x65: 1 byte, the version (0x63 was REC's: retired, not reused). */
#include "ed_mirror_enc.h"
enum { MIR_STALL_MS = 30, MIR_ALIVE_MS = 3000 };
static struct {
    uint8_t on;
    uint8_t k, acc[3];                 /* the bytes of the next USB-MIDI packet */
    uint32_t resets, last_ms;           /* usb.resets when asked; fm1_ms of the last request of any kind */
} mir;
#define MIR_SEEN() (mir.last_ms = fm1_ms)   /* (editor.c, ed_lite.c: each request that comes in) */
static int mir_live(void)
{
    if (mir.on && (!usb.config || mir.resets != usb.resets || fm1_ms - mir.last_ms > MIR_ALIVE_MS))
        mir.on = 0;
    return mir.on && !mir_mute;
}
/* one USB-MIDI SysEx packet (cin 4: three bytes go on; 5..7: one to three, the last F7) into the ring, waiting while the host reads */
static int mir_pkt(uint32_t cin)
{
    uint32_t t0 = fm1_ms, r = so_r;
    while (so_w - so_r >= SXQ) {
        if (so_r != r)
            t0 = fm1_ms, r = so_r;
        if (!usb.config || fm1_ms - t0 > MIR_STALL_MS) {
            mir.on = 0, sx_busy = 0;
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
    if (b == 0xF0u)
        sx_busy = 1, mir.k = 0;         /* (no other MIDI out inside the frame: usb.c ep1_tx) */
    if (mir.k == 3u && !mir_pkt(4))     /* (a full packet goes when the next byte comes: the last one carries F7) */
        return 0;
    mir.acc[mir.k++] = (uint8_t)b;
    return 1;
}
static void mir_wire_end(void)
{
    uint32_t k = mir.k;
    mir.acc[1] = k > 1u ? mir.acc[1] : 0u;
    mir.acc[2] = k > 2u ? mir.acc[2] : 0u;
    mir_pkt(4u + k);
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
static void ed_mirror(const uint8_t *a, uint32_t na)
{
    uint32_t bits = na ? a[0] : 0u;
    MIR_SEEN();
    mir_live();                         /* (a mirror that ended: this is a first on again) */
    if ((bits & 1u) && (!mir.on || (bits & 2u)))
        ui.force = 1;                   /* (the whole panel, from black) */
    mir.on = (uint8_t)(bits & 1u);
    mir.resets = usb.resets;
    ed_b(mir.on);
    ed_b(1);
    ed_b4(240u & 127u, 240u >> 7, 240u & 127u, 240u >> 7);
}
static void ed_mirror_info(void) { ed_b3(0x65, 1, 1); }
