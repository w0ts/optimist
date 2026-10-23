/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen mirrored (io/editor/ed_mirror.c, ed_mirror_enc.h; builder item ED_MIRROR), both links:
 *   USB        every panel write, encoded, cut into USB-MIDI packets through a stand-in SysEx ring, put back together as a host
 *              does and decoded (mirror_dec.h): the decoded screen is the panel after every write; a host that stops reading
 *              puts the mirror behind (nothing waits longer than MIR_STALL_MS), and once it reads again the catch-up gives the
 *              panel again; the keep-alive, a USB reset, 106 off
 *   Bluetooth  the request parsed from SysEx packets as the radio hands them over (garbage refused), the reply, frames drained
 *              slowly as the radio does (mir_ble_peek / pop, a few bytes a write): the ring overflows, the mirror falls behind and
 *              catches up, and after the last catch-up the decoded screen is the panel; only whole frames reach the radio
 * Also long runs, many colours, the 7-bit wire, the frame cut, the meters muted. op_repaint stands in for the UI's in-place repaint
 * (every band drawn again: here the whole panel blitted again; tests/ui_optimist_test.c MIRROR_CHECK checks the real one).
 * MIRROR_FIXTURE=path writes the frames of a sample screen (and its pixels) for ios/OptimistMirror's tests. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define check(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the device's side, stood in: usb.c's SysEx ring, editor.c's reply, midi_ble.c's listener, the UI */
#define MIR_BLE 1
#define IN_POOL
#define SXQ 64u
#define RING_PUBLISH() ((void)0)
static uint32_t sx_out_q[SXQ];
static volatile uint32_t so_w, so_r;
static volatile uint8_t sx_busy;
static struct { uint8_t config; uint32_t resets; } usb = {1, 0};
static volatile uint32_t fm1_ms;
static volatile uint8_t ble_out_on;
static uint8_t ble_ll_tx_octets(void) { return 251; }     /* (the radio's: ble/ble_ll.c, ble_att.c) */
static uint16_t ble_att_mtu(void) { return 185; }
static uint16_t ble_ll_interval(void) { return 9; }
static uint16_t ble_ll_peer_len(uint32_t i) { static const uint16_t V[4] = {251, 2120, 50, 512}; return V[i & 3u]; }
static struct { uint8_t overlay; } ui;
static uint8_t mir_mute;
static uint8_t rep[64];
static uint32_t nrep;
static void ed_b(uint32_t v) { if (nrep < sizeof rep) rep[nrep++] = (uint8_t)(v & 0x7Fu); }
#include "../firmware/src/io/editor/ed_reply.h"

/* ---- the host: packets back into frames (as CoreMIDI does), each decoded */
#include "mirror_dec.h"
static uint8_t hf[4096];
static uint32_t hn, frames, bytes, bad, max_frame, replies, last_reply_on, links, link[7];
static int stalled;                     /* 1: the USB host reads nothing (the ring stays full) */
static FILE *fix;
static void host_byte(uint8_t b)
{
    if (b == 0xF0u)
        hn = 0;
    if (hn < sizeof hf)
        hf[hn++] = b;
    if (b == 0xF7u) {
        frames++, bytes += hn;
        max_frame = hn > max_frame ? hn : max_frame;
        if (hn == 12u && hf[5] <= 1u && hf[6] >= 1u)
            replies++, last_reply_on = hf[5];
        else if (hn == 21u && hf[5] == 3u) {             /* (op 3 LINK, after a Bluetooth reply) */
            uint32_t i;
            links++;
            for (i = 0; i < 7u; i++)
                link[i] = hf[6u + 2u * i] | hf[7u + 2u * i] << 7;
        }
        else
            bad += !md_frame(hf, hn);
        if (fix)
            fwrite(hf, 1, hn, fix);
        hn = 0;
    }
}
static void host_pkt(uint32_t p)
{
    uint32_t cin = p & 15u, k = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u, i;
    check(k, "a packet with cin %u", cin);
    for (i = 0; i < k; i++) {
        uint8_t b = (uint8_t)(p >> (8u + 8u * i));
        check(b < 0x80u || b == 0xF0u || b == 0xF7u, "byte %02X on the wire", b);
        check(cin == 4u || i + 1u < k || b == 0xF7u, "an end packet without F7");
        host_byte(b);
    }
}
static void host_drain(void)
{
    while (so_r != so_w)
        host_pkt(sx_out_q[so_r++ % SXQ]);
}
static void ota_idle(void)
{
    if (stalled)
        fm1_ms++;                       /* (time goes on; nothing read) */
    else
        host_drain();
}
static void op_repaint(void);
#include "../firmware/src/io/editor/ed_mirror.c"

/* the radio: at most n bytes of whole-frame packets (all: n = ~0) */
static uint32_t radio(uint32_t n)
{
    uint32_t p, got = 0;
    while (got < n && mir_ble_peek(&p)) {
        host_pkt(p);
        got += mbr.pk;
        mir_ble_pop();
    }
    return got;
}

/* ---- the panel itself, as display/lcd.c sends it (pixels byte-swapped) */
static uint16_t panel[240 * 240];
static uint32_t repaints;
static uint16_t sw16(uint32_t c) { return (uint16_t)((c >> 8 | c << 8) & 0xFFFFu); }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            panel[(y + j) * 240u + x + i] = c;
    mir_fill(x, y, w, h, c);
}
static uint16_t cv[240 * 240];
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            panel[(y + j) * 240u + x + i] = sw16(px[j * w + i]);
    mir_blit(x, y, w, h, px);
}
static void op_repaint(void)            /* the UI's: every band again, in place (here: the panel as it is, in two bands) */
{
    uint32_t i;
    repaints++;
    for (i = 0; i < 240u * 240u; i++)
        cv[i] = sw16(panel[i]);
    lcd_blit(0, 0, 240, 120, cv);
    lcd_blit(0, 120, 240, 120, cv + 240u * 120u);
}
static uint32_t rnd_s = 1;
static uint32_t rnd(uint32_t n) { rnd_s = rnd_s * 1103515245u + 12345u; return (rnd_s >> 8) % n; }
static int same(void)
{
    host_drain();
    return !memcmp(panel, md_fb, sizeof panel);
}
static void ask(uint32_t bits)
{
    uint8_t a[1] = {(uint8_t)bits};
    nrep = 0;
    ed_mirror(a, 1);
}
/* a canvas like the UI's: black, a few colours of text-like dots and bars */
static int quiet;                       /* 1: UI-like writes only (no canvas of noise: a catch-up as big as the UI's) */
static void canvas(uint32_t w, uint32_t h, uint32_t ncol)
{
    uint32_t i, cols[16];
    for (i = 0; i < ncol; i++)
        cols[i] = rnd(0x10000u);
    for (i = 0; i < w * h; i++)
        cv[i] = 0;
    for (i = 0; i < w * h / (quiet ? 60u : 6u); i++) {   /* (quiet: about as sparse as text on the UI's black) */
        uint32_t at = rnd(w * h), len = 1u + rnd(8);
        uint16_t c = sw16(cols[rnd(ncol)]);
        for (; len-- && at < w * h; at++)
            cv[at] = c;
    }
}
static void random_write(void)
{
    uint32_t i, w = 1u + rnd(240), h = 1u + rnd(124), x = rnd(241u - w), y = rnd(241u - h);
    if (rnd(4) == 0u) {
        lcd_fill(x, y, w, h, (uint16_t)rnd(0x10000u));
        return;
    }
    canvas(w, h, rnd(3) == 0u ? 1u + rnd(16) : 1u + rnd(4));
    if (!quiet && rnd(10) == 0u)        /* (every pixel its own colour: the literal path, the slots wrapping) */
        for (i = 0; i < w * h; i++)
            cv[i] = (uint16_t)rnd(0x10000u);
    lcd_blit(x, y, w, h, cv);
}
/* the Bluetooth request as the radio hands it over: USB-MIDI SysEx packets */
static void bt_send(const uint8_t *f, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i, last = i + k >= n;
        uint32_t cin = last ? 4u + k : 4u, p = cin, j;
        for (j = 0; j < k; j++)
            p |= (uint32_t)f[i + j] << (8u + 8u * j);
        mir_ble_rx(p);
        i += k;
    }
}

static void usb_tests(void)
{
    uint32_t k, f0, b0;
    lcd_fill(0, 0, 240, 240, 0x1234u);
    check(so_w == 0u, "sent while off");
    /* on: the reply; the panel comes whole by the catch-up (black to the client, the bands again in place) */
    MIR_SEEN();
    ask(1);
    check(nrep == 6u && rep[0] == 1u && rep[1] == MIR_VERSION && (rep[2] | rep[3] << 7) == 240u && (rep[4] | rep[5] << 7) == 240u, "the reply");
    check(mir.behind == 1u, "a first on catches up");
    host_drain();
    mir_poll();
    check(repaints == 1u && !mir.behind && same(), "the first catch-up: the panel");
    ask(1);
    mir_poll();
    check(repaints == 1u, "on again keeps what was sent");
    ask(3);
    host_drain();
    mir_poll();
    check(repaints == 2u && same(), "bit 1: the whole panel again");
    fm1_ms += MIR_METER_MS;
    check(mir_meters_due() && !mir_meters_due() && !mir_meters_off(), "the meters over USB: due, then not before MIR_METER_MS");
    for (k = 0; k < 3000u; k++) {
        random_write();
        if (!same()) {
            check(0, "the screen after write %u", k);
            break;
        }
    }
    check(bad == 0u, "%u frames not decoded", bad);
    check(max_frame <= MIR_CUT + 24u, "a frame of %u bytes", max_frame);
    /* one run longer than the long token (16 + 16383) */
    for (k = 0; k < 240u * 124u; k++)
        cv[k] = sw16(0xBEEFu);
    lcd_blit(0, 0, 240, 124, cv);
    lcd_blit(0, 116, 240, 124, cv);
    check(same(), "long runs");
    f0 = frames, b0 = bytes;
    canvas(240, 124, 4);
    lcd_blit(0, 25, 240, 124, cv);
    check(same(), "a band");
    printf("a 240x124 band, 4 colours: %u bytes in %u frames (raw %u)\n", bytes - b0, frames - f0, 240u * 124u * 2u);
    /* a muted write (a mixer meter outside its window, op_mixdraw.c): not sent, the client keeps its last */
    f0 = frames;
    mir_mute = 1;
    lcd_fill(10, 10, 20, 3, 0xFFFFu);
    mir_mute = 0;
    host_drain();
    check(frames == f0, "a muted write went out");
    memcpy(md_fb, panel, sizeof panel);
    /* a host that stops reading: behind within MIR_STALL_MS, then nothing waits; reading again, the catch-up gives the panel */
    stalled = 1;
    canvas(240, 124, 16);
    k = fm1_ms;
    lcd_blit(0, 0, 240, 124, cv);
    check(mir.on && mir.behind && fm1_ms - k <= MIR_STALL_MS + 1u, "a stalled host: behind %u after %u ms", mir.behind, fm1_ms - k);
    check(sx_busy == 0u, "sx_busy left set");
    k = fm1_ms;
    random_write();
    random_write();
    check(fm1_ms == k, "behind: nothing waits");
    mir_poll();
    check(mir.behind, "no catch-up while the host reads nothing");
    stalled = 0;
    host_drain();
    MIR_SEEN();
    mir_poll();
    check(mir.behind, "a catch-up sooner than MIR_CATCH_MS after the last");
    fm1_ms += MIR_CATCH_MS;
    MIR_SEEN();
    mir_poll();
    check(!mir.behind && same(), "the host reads again: caught up, the panel");
    /* PRESETS' popup list: no catch-up under it */
    mir.behind = 1;
    ui.overlay = 5;
    mir_poll();
    check(mir.behind, "a catch-up under the popup list");
    ui.overlay = 0;
    fm1_ms += MIR_CATCH_MS;
    mir_poll();
    check(!mir.behind && same(), "the popup closed: caught up");
    /* the keep-alive: no request for 3 s ends it; a USB reset ends it */
    fm1_ms += MIR_ALIVE_MS + 1u;
    k = so_w;
    lcd_fill(0, 0, 10, 10, 2);
    check(so_w == k && !mir.on, "no request for 3 s: ended");
    MIR_SEEN();
    ask(1);
    usb.resets++;
    lcd_fill(0, 0, 10, 10, 3);
    check(so_w == k && !mir.on, "a USB reset: ended");
    ask(0);
    check(rep[0] == 0u && !mir.on, "106 0: off");
}

static void bt_tests(void)
{
    static const uint8_t ON[] = {0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0xF7}, BAD[] = {0xF0, 0x7D, 0x46, 0x4C, 105, 1, 0xF7},
                         LONG[] = {0xF0, 0x7D, 0x46, 0x4C, 106, 1, 2, 0xF7}, OFF[] = {0xF0, 0x7D, 0x46, 0x4C, 106, 0, 0xF7};
    uint32_t k, r0, lost = 0, sent = 0;
    host_drain();
    quiet = 1;
    lcd_fill(0, 0, 240, 240, 0);        /* (a UI-like panel: black, low-colour bands; a catch-up of a few KB) */
    host_drain();
    memcpy(md_fb, panel, sizeof panel);
    /* the request: only 106 with one byte; nothing while no central listens */
    bt_send(BAD, sizeof BAD);
    bt_send(LONG, sizeof LONG);
    check(!mbr.req, "another command or a longer frame taken for a request");
    bt_send(ON, sizeof ON);
    check(mbr.req == 0x81u, "the request parsed: %02X", mbr.req);
    mir_poll();
    check(!mir.on && mbr.req, "answered while no central listens");
    ble_out_on = 1;
    r0 = replies;
    mir_poll();
    check(mir.on && mir.link == MIR_BT && mir.behind, "on over Bluetooth, a catch-up due");
    fm1_ms += MIR_METER_MS;
    k = mir.behind, mir.behind = 0;
    check(!mir_meters_due() && mir_meters_off(), "no meters over Bluetooth, not even in a page drawn whole");
    mir.behind = (uint8_t)k;
    check(mbr.w != mbr.r, "the reply in the ring");
    mir_poll();
    check(mir.behind, "no catch-up while the reply waits");
    radio(~0u);
    check(replies == r0 + 1u && last_reply_on == 1u, "the reply came: on");
    check(links == 1u && link[0] == 251u && link[1] == 185u && link[2] == 9u && link[3] == 251u && link[4] == 2120u && link[5] == 50u &&
              link[6] == 512u, "then the link: data length, MTU, interval, the central's LL_LENGTH values");
    mir_poll();
    k = radio(~0u);
    check(!mir.behind && same(), "the first catch-up over Bluetooth: %u bytes, the panel", k);
    printf("a catch-up over Bluetooth: %u bytes\n", k);
    /* a slow radio: a few hundred bytes a write; the ring overflows, the mirror falls behind and catches up */
    for (k = 0; k < 4000u; k++) {
        uint32_t was = mir.behind;
        random_write();
        lost += !was && mir.behind;
        sent += radio(150u + rnd(300));
        fm1_ms += 16u;                  /* (60 frames a second) */
        if (k % 25u == 0u)
            lcd_fill(0, 0, 240, 240, 0);   /* (a page change: keeps the panel UI-like) */
        if (k % 7u == 0u) {
            MIR_SEEN();
            mir_poll();
        }
        check(mbr.w - mbr.r <= MIR_BLE_RING, "the ring past its size");
    }
    for (k = 0; k < 100u && (mir.behind || mbr.r != mbr.w); k++) {
        radio(~0u);
        fm1_ms += 16u;
        MIR_SEEN();
        mir_poll();
    }
    check(k < 100u, "never caught up");
    check(lost > 10u && same(), "a slow radio: behind %u times, caught up, the panel (%u bytes sent)", lost, sent);
    check(bad == 0u, "%u frames not decoded", bad);
    /* the central goes: off, the ring emptied (midi_ble.c ble_app_state) */
    ble_out_on = 0;
    mir_ble_flush();
    k = mbr.w;
    lcd_fill(0, 0, 10, 10, 4);
    check(!mir.on && mbr.w == k, "no central: ended");
    ble_out_on = 1;
    bt_send(ON, sizeof ON);
    mir_poll();
    bt_send(OFF, sizeof OFF);
    mir_poll();
    check(!mir.on, "106 0 over Bluetooth: off");
    radio(~0u);
}

int main(void)
{
    uint32_t k;
    usb_tests();
    bt_tests();
    /* the fixture for the app's tests: a panel drawn whole, then its frames and its pixels */
    if (getenv("MIRROR_FIXTURE")) {
        char p[512];
        FILE *px;
        ble_out_on = 0;
        snprintf(p, sizeof p, "%s.sysex", getenv("MIRROR_FIXTURE"));
        fix = fopen(p, "wb");
        MIR_SEEN();
        ask(1);
        mir_poll();                     /* (the first catch-up: black, the panel) */
        lcd_fill(0, 0, 240, 240, 0);
        for (k = 0; k < 40u; k++) {
            uint32_t w = 8u + rnd(232), h = 4u + rnd(60), x = rnd(241u - w), y = rnd(241u - h);
            canvas(w, h, 1u + rnd(6));
            lcd_blit(x, y, w, h, cv);
            if (k % 5u == 0u)
                lcd_fill(x, y, w / 2u, 2, (uint16_t)rnd(0x10000u));
        }
        host_drain();
        fclose(fix);
        fix = NULL;
        snprintf(p, sizeof p, "%s.rgb565", getenv("MIRROR_FIXTURE"));
        px = fopen(p, "wb");
        fwrite(panel, 2, 240u * 240u, px);   /* (host order: little-endian) */
        fclose(px);
        check(same(), "the fixture");
    }
    printf("mirror: %d checks, %d failed (%u frames, %u bytes)\n", checks, fails, frames, bytes);
    return fails != 0;
}
