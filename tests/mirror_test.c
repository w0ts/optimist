/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen mirrored (io/editor/ed_mirror.c, ed_mirror_enc.h; builder item ED_MIRROR): every panel write, encoded, cut into
 * USB-MIDI packets through a stand-in SysEx ring, put back together as a host does and decoded (mirror_dec.h), leaves the
 * decoded screen equal to the panel after every write. Also: long runs, many colours, the 7-bit wire, the frame cut, a host
 * that stops reading (the mirror ends, nothing waits longer than MIR_STALL_MS), 106 on / off and the keep-alive, the meters
 * muted. MIRROR_FIXTURE=path writes the frames of a sample screen (and its pixels) for ios/OptimistMirror's tests. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define check(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the device's side, stood in: usb.c's SysEx ring, editor.c's reply and keep-alive, the UI's force */
#define SXQ 64u
#define RING_PUBLISH() ((void)0)
static uint32_t sx_out_q[SXQ];
static volatile uint32_t so_w, so_r;
static volatile uint8_t sx_busy;
static struct { uint8_t config; uint32_t resets; } usb = {1, 0};
static volatile uint32_t fm1_ms;
static struct { uint32_t last_ms; } ed_w;   /* (unused by the mirror: its own last_ms) */
static struct { uint8_t force; } ui;
static uint8_t mir_mute;
static uint8_t rep[64];
static uint32_t nrep;
static void ed_b(uint32_t v) { if (nrep < sizeof rep) rep[nrep++] = (uint8_t)(v & 0x7Fu); }
#include "../firmware/src/io/editor/ed_reply.h"

/* ---- the host: packets back into frames (as CoreMIDI does), each decoded */
#include "mirror_dec.h"
static uint8_t hf[4096];
static uint32_t hn, frames, bytes, bad, max_frame;
static int stalled;                     /* 1: the host reads nothing (the ring stays full) */
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
        bad += !md_frame(hf, hn);
        if (fix)
            fwrite(hf, 1, hn, fix);
        hn = 0;
    }
}
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r % SXQ], cin = p & 15u, k = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u, i;
        check(k, "a packet with cin %u", cin);
        for (i = 0; i < k; i++) {
            uint8_t b = (uint8_t)(p >> (8u + 8u * i));
            check(b < 0x80u || b == 0xF0u || b == 0xF7u, "byte %02X on the wire", b);
            check(cin == 4u || i + 1u < k || b == 0xF7u, "an end packet without F7");
            host_byte(b);
        }
        so_r++;
    }
}
static void ota_idle(void)
{
    if (stalled)
        fm1_ms++;                       /* (time goes on; nothing read) */
    else
        host_drain();
}
#include "../firmware/src/io/editor/ed_mirror.c"

/* ---- the panel itself, as display/lcd.c sends it (pixels byte-swapped) */
static uint16_t panel[240 * 240];
static uint16_t sw16(uint32_t c) { return (uint16_t)((c >> 8 | c << 8) & 0xFFFFu); }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            panel[(y + j) * 240u + x + i] = c;
    mir_fill(x, y, w, h, c);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            panel[(y + j) * 240u + x + i] = sw16(px[j * w + i]);
    mir_blit(x, y, w, h, px);
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
    ed_w.last_ms = fm1_ms;
    ed_mirror(a, 1);
}
static uint16_t cv[240 * 124];
/* a canvas like the UI's: black, a few colours of text-like dots and bars */
static void canvas(uint32_t w, uint32_t h, uint32_t ncol)
{
    uint32_t i, cols[16];
    for (i = 0; i < ncol; i++)
        cols[i] = rnd(0x10000u);
    for (i = 0; i < w * h; i++)
        cv[i] = 0;
    for (i = 0; i < w * h / 6u; i++) {
        uint32_t at = rnd(w * h), len = 1u + rnd(8);
        uint16_t c = sw16(cols[rnd(ncol)]);
        for (; len-- && at < w * h; at++)
            cv[at] = c;
    }
}

int main(void)
{
    uint32_t i, k, f0, b0;
    /* off: nothing goes out */
    lcd_fill(0, 0, 240, 240, 0x1234u);
    check(so_w == 0u, "sent while off");
    /* on: the reply, a whole panel asked for */
    ask(1);
    check(nrep == 6u && rep[0] == 1u && rep[1] == 1u && (rep[2] | rep[3] << 7) == 240u && (rep[4] | rep[5] << 7) == 240u, "the reply");
    check(ui.force == 1u, "a first on draws the panel whole");
    ui.force = 0;
    ask(1);
    check(ui.force == 0u, "on again keeps what was sent");
    ask(3);
    check(ui.force == 1u, "bit 1: the whole panel again");
    lcd_fill(0, 0, 240, 240, 0);
    check(same(), "a black panel");
    /* random fills and blits, few and many colours, any size, the edges */
    for (k = 0; k < 3000u; k++) {
        uint32_t w = 1u + rnd(240), h = 1u + rnd(124), x = rnd(241u - w), y = rnd(241u - h);
        if (rnd(4) == 0u) {
            lcd_fill(x, y, w, h, (uint16_t)rnd(0x10000u));
        } else {
            canvas(w, h, rnd(3) == 0u ? 1u + rnd(16) : 1u + rnd(4));
            if (rnd(10) == 0u)          /* (every pixel its own colour: the literal path, the slots wrapping) */
                for (i = 0; i < w * h; i++)
                    cv[i] = (uint16_t)rnd(0x10000u);
            lcd_blit(x, y, w, h, cv);
        }
        if (!same()) {
            check(0, "the screen after write %u (%ux%u at %u,%u)", k, w, h, x, y);
            break;
        }
    }
    check(bad == 0u, "%u frames not decoded", bad);
    check(max_frame <= MIR_CUT + 16u, "a frame of %u bytes", max_frame);
    /* one run longer than the long token (16 + 16383): a whole 240 x 124 canvas of one colour, then 240 x 240 by blits */
    for (i = 0; i < 240u * 124u; i++)
        cv[i] = sw16(0xBEEFu);
    lcd_blit(0, 0, 240, 124, cv);
    lcd_blit(0, 116, 240, 124, cv);
    check(same(), "long runs");
    /* what a UI-like band costs: a 240 x 124 canvas, 4 colours */
    f0 = frames, b0 = bytes;
    canvas(240, 124, 4);
    lcd_blit(0, 25, 240, 124, cv);
    check(same(), "a band");
    printf("a 240x124 band, 4 colours: %u bytes in %u frames (raw %u)\n", bytes - b0, frames - f0, 240u * 124u * 2u);
    /* the meters: not sent */
    f0 = frames;
    mir_mute = 1;
    lcd_fill(10, 10, 20, 3, 0xFFFFu);
    mir_mute = 0;
    host_drain();
    check(frames == f0, "a muted write went out");
    memcpy(md_fb, panel, sizeof panel);   /* (the client keeps its last meter: equal again for what follows) */
    /* a host that stops reading: the mirror ends within MIR_STALL_MS, nothing more is sent */
    stalled = 1;
    canvas(240, 124, 16);
    k = fm1_ms;
    lcd_blit(0, 0, 240, 124, cv);
    check(!mir.on && fm1_ms - k <= MIR_STALL_MS + 1u, "a stalled host: on %u after %u ms", mir.on, fm1_ms - k);
    check(sx_busy == 0u, "sx_busy left set");
    k = so_w;
    lcd_fill(0, 0, 10, 10, 1);
    check(so_w == k, "sent after the stall");
    stalled = 0;
    host_drain();
    ask(1);
    check(mir.on && ui.force == 1u, "asked again: on, the panel whole");
    ui.force = 0;
    /* the keep-alive: no request for 3 s ends it; a USB reset ends it */
    fm1_ms += MIR_ALIVE_MS + 1u;
    k = so_w;
    lcd_fill(0, 0, 10, 10, 2);
    check(so_w == k && !mir.on, "no request for 3 s: ended");
    ask(1);
    usb.resets++;
    lcd_fill(0, 0, 10, 10, 3);
    check(so_w == k && !mir.on, "a USB reset: ended");
    ask(0);
    check(rep[0] == 0u && !mir.on, "106 0: off");
    /* the fixture for the app's tests: a panel drawn whole, then its frames and its pixels */
    if (getenv("MIRROR_FIXTURE")) {
        char p[512];
        FILE *px;
        snprintf(p, sizeof p, "%s.sysex", getenv("MIRROR_FIXTURE"));
        fix = fopen(p, "wb");
        ask(1);
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
