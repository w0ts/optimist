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
static struct { uint8_t overlay, scr; } ui;
static uint8_t mir_mute;
/* the Optimist UI's navigation, stood in (ui/optimist/op_nav.c, op_gldraw.c, op_global.c, op_mixer.c: their logic is
 * tests/ui_optimist_nav.h's; here 109 NAV's reply is what is checked): three rows, the cursor on the second, the walk on T2,
 * the four lines with the knobs; ROW 2 refused, TRACK 1..4 and OPEN accepted */
#define OP_MAXROWS 40u
#define GL_TOP 20
#define MXR_LN 5u
static uint32_t nav_log[4];
static uint32_t nav_rows(void) { return 3; }
static uint32_t nav_cur(void) { return 1; }
static uint32_t nav_mode(void) { return 127; }
static uint32_t nav_track(void) { return 2; }
static uint32_t nav_knobs(void) { return 3; }
static int glo_cycle(void) { return 0; }
static int32_t gl_y0(void) { return 7; }
static int32_t gl_pitch(void) { return 30; }
static void nav_row_name(uint32_t r, char *b, uint32_t n) { snprintf(b, n, "ROW %u", (unsigned)r); }
static void nav_track_name(uint32_t p, char *b) { snprintf(b, 12, "TRK %u", (unsigned)p); }
static int nav_row(uint32_t r) { nav_log[1] = r + 1u; return r != 2u; }
static int nav_walk(uint32_t p) { nav_log[2] = p + 1u; return p >= 1u && p <= 4u; }
static int nav_open(uint32_t scr, uint32_t b) { nav_log[3] = scr << 8 | b; return 1; }
static uint8_t rep[64];
static uint32_t nrep;
static void ed_b(uint32_t v) { if (nrep < sizeof rep) rep[nrep++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)   /* (editor.c's: ASCII, 0-terminated) */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i]);
    ed_b(0);
}
#include "../firmware/src/io/editor/ed_reply.h"

/* ---- the host: packets back into frames (as CoreMIDI does), each decoded */
#include "mirror_dec.h"
static uint8_t hf[4096];
static uint32_t hn, frames, bytes, bad, max_frame, replies, last_reply_on, links, link[7], ed_replies, ed_last[3];
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
        if (hn >= 6u && hf[4] != 106u)
            ed_replies++, ed_last[0] = hf[4], ed_last[1] = hn > 5u ? hf[5] : 0u, ed_last[2] = hn > 6u ? hf[6] : 0u;   /* (an editor reply: Bluetooth's PANEL, NAV, INFO) */
        else if (hn == 12u && hf[5] <= 1u && hf[6] >= 1u)
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
/* the panel's table (ui/panel.c, its default) and the input layer's remote keys (hal/fm1_input.h): 108 PANEL */
enum { NB = 14, NE = 7 };
static const struct { uint8_t btn[NB], enc[NE]; int8_t dir[NE]; } panel_tab = {
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1}, {0, 1, 6, 2, 3, 4, 5}, {1, 1, -1, 1, 1, 1, 1}};
static uint32_t rem_btn, rem_note;
static int32_t rem_enc[7];
static void fm1_remote_key(uint32_t id, uint32_t down)
{
    uint32_t *m = id >= 14u ? &rem_note : &rem_btn, bit = id >= 14u ? 1u << (id - 14u) : 1u << id;
    *m = down ? *m | bit : *m & ~bit;
}
static void fm1_remote_enc(uint32_t e, int32_t s) { rem_enc[e] += s; }
static void fm1_remote_clear(void) { rem_btn = rem_note = 0; }
static uint8_t ed_bt;                  /* (editor.c's: the request came over Bluetooth) */
#define panel panel_tab
#define MIR_BLE_LAG 1536u               /* (smaller than the firmware's: the random screens here catch up in less than its 4 KB) */
#include "../firmware/src/io/editor/ed_mirror.c"
#undef panel
/* editor.c's ed_handle and ed_send, the commands Bluetooth takes: the reply framed, over Bluetooth into the ring */
static uint32_t ed_handled[128];
static void ed_handle(const uint8_t *f, uint32_t n)
{
    uint8_t o[96];
    uint32_t cmd = f[3], i, k = 0;
    ed_handled[cmd & 127u]++;
    nrep = 0;
    if (cmd == ED_PANEL)
        ed_panel(f + 4, n - 4u);
    else if (cmd == ED_NAV)
        ed_nav(f + 4, n - 4u);
    else
        ed_b(0x66);                     /* (INFO: a stand-in) */
    o[k++] = 0xF0, o[k++] = 0x7D, o[k++] = 0x46, o[k++] = 0x4C, o[k++] = (uint8_t)cmd;
    for (i = 0; i < nrep && k < sizeof o - 1u; i++)
        o[k++] = rep[i];
    o[k++] = 0xF7;
    if (ed_bt)
        mir_bt_reply(o, k);
}

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

/* 108 PANEL: the client's keys held as the panel's, by label and role; refused while it does not mirror; up when it ends */
static void panel_tests(void)
{
    static const uint8_t FX_DOWN[3] = {0, 0, 1}, FX_UP[3] = {0, 0, 0}, NOTE5[3] = {1, 5, 1}, K1[3] = {2, 3, 64 + 2}, PRE[3] = {2, 2, 64 - 1};
    ask(1);
    nrep = 0, ed_panel(FX_DOWN, 3);
    check(rem_btn == 1u << 2 && nrep == 2 && rep[1] == 0, "PANEL: FX down holds matrix button 2 (its label's)");
    ed_panel(NOTE5, 3);
    check(rem_note == 1u << 5, "PANEL: note key 5 down");
    ed_panel(K1, 3), ed_panel(PRE, 3);
    check(rem_enc[2] == 2 && rem_enc[6] == 1, "PANEL: KNOB 1 two detents on encoder 2, PRESETS one back on encoder 6 (its direction -1)");
    ed_panel(FX_UP, 3);
    check(rem_btn == 0u && rem_note == 1u << 5, "PANEL: FX up, the note still held");
    ask(0);
    check(rem_btn == 0u && rem_note == 0u, "PANEL: the mirror off: every remote key up");
    nrep = 0, ed_panel(FX_DOWN, 3);
    check(rem_btn == 0u && nrep == 2 && rep[1] == 1, "PANEL: refused while not mirroring");
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
        uint32_t was = mir.behind, w0 = mbr.w, lag = mbr.w - ((int32_t)(mbr.base - mbr.r) > 0 ? mbr.base : mbr.r) > MIR_BLE_LAG && fm1_ms - mir.catch_ms >= MIR_CATCH_MS;
        random_write();
        check(!lag || mbr.w == w0, "a frame past MIR_BLE_LAG waiting, outside a catch-up");
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
    /* a link slower than a catch-up (the iPad scanning in its Bluetooth sheet) with small writes going on: one catch-up, not one
     * after the other (its own bytes are not the lag) */
    for (k = 0; k < 60u; k++)
        random_write();
    radio(~0u);
    mir.behind = 1, mir.catch_ms = fm1_ms - MIR_CATCH_MS;
    MIR_SEEN();
    mir_poll();
    r0 = repaints;
    check(mbr.w - mbr.r > MIR_BLE_LAG, "a catch-up longer than MIR_BLE_LAG: %u bytes", mbr.w - mbr.r);
    for (k = 0; k < 300u; k++) {
        lcd_fill(rnd(230), rnd(230), 4, 4, rnd(65536));
        radio(60u);
        fm1_ms += 16u;
        MIR_SEEN();
        mir_poll();
    }
    radio(~0u);
    check(repaints == r0 && !mir.behind && same(), "a slow link: %u catch-ups more while small writes went on", repaints - r0);
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
    {   /* INFO, 108 PANEL and 109 NAV over Bluetooth too (opti_mirbt_1): run in the main loop, the replies in the ring */
        static const uint8_t DOWN[] = {0xF0, 0x7D, 0x46, 0x4C, 108, 0, 3, 1, 0xF7}, UP[] = {0xF0, 0x7D, 0x46, 0x4C, 108, 0, 3, 0, 0xF7},
                             NAV[] = {0xF0, 0x7D, 0x46, 0x4C, 109, 0, 0xF7}, INFO[] = {0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7},
                             SET[] = {0xF0, 0x7D, 0x46, 0x4C, 3, 1, 2, 3, 0xF7},
                             TOOLONG[] = {0xF0, 0x7D, 0x46, 0x4C, 108, 0, 3, 1, 1, 1, 0xF7};
        uint32_t e0, i, btn_seen = 0;
        bt_send(ON, sizeof ON);
        mir_poll();
        radio(~0u);
        e0 = ed_replies;
        memset(ed_handled, 0, sizeof ed_handled);
        bt_send(DOWN, sizeof DOWN);
        bt_send(INFO, sizeof INFO);
        bt_send(SET, sizeof SET);
        bt_send(TOOLONG, sizeof TOOLONG);
        check((uint8_t)(mbr.cw - mbr.cr) == 2u, "PANEL and INFO queued, SET (not Bluetooth's) and a frame too long not: %u", mbr.cw - mbr.cr);
        mir_poll();
        btn_seen = rem_btn != 0u;
        bt_send(UP, sizeof UP);
        bt_send(NAV, sizeof NAV);
        mir_poll();
        radio(~0u);
        check(ed_handled[108] == 2u && ed_handled[109] == 1u && ed_handled[1] == 1u && ed_handled[3] == 0u && btn_seen && !rem_btn,
              "Bluetooth PANEL down then up pressed and let go the button, NAV and INFO run, SET not");
        check(ed_replies - e0 == 4u && ed_last[0] == 109u && ed_last[1] == 0u && ed_last[2] == 0u && !bad,
              "their 4 replies came over Bluetooth, whole, the last NAV's (op 0, done)");
        for (i = 0; i < MBR_CQ + 3u; i++)
            bt_send(DOWN, sizeof DOWN);
        check((uint8_t)(mbr.cw - mbr.cr) == MBR_CQ, "more than MBR_CQ waiting: the rest dropped (the client sends again)");
        ble_out_on = 0;
        mir_poll();
        check(mbr.cw == mbr.cr, "no central: the requests waiting dropped");
        fm1_remote_clear();
    }
}

/* 109 NAV: the reply's shape, each op by the stood-in navigation */
static void nav_tests(void)
{
    static const uint8_t ST[] = {0, 0, 4, 127, 1, 3, 'R', 'O', 'W', ' ', '0', 0, 'R', 'O', 'W', ' ', '1', 0, 'R', 'O', 'W', ' ', '2', 0, 2, 3, 27, 30};
    static const uint8_t TR[] = {4, 0, 5, 'T', 'R', 'K', ' ', '0', 0, 'T', 'R', 'K', ' ', '1', 0, 'T', 'R', 'K', ' ', '2', 0, 'T', 'R', 'K', ' ', '3', 0, 'T', 'R', 'K', ' ', '4', 0};
    uint8_t a[3];
    mir.on = 1, ui.scr = 4;
    nrep = 0, a[0] = 0, ed_nav(a, 1);
    check(nrep == sizeof ST && !memcmp(rep, ST, sizeof ST), "NAV STATE: op, done, screen, page, cursor, the 3 rows, the walk's row, the knobs on the lines, top 27 and pitch 30 (%u bytes)", (unsigned)nrep);
    nrep = 0, a[0] = 4, ed_nav(a, 1);
    check(nrep == sizeof TR && !memcmp(rep, TR, sizeof TR), "NAV TRACKS: op, done, the 5 mixer rows' names");
    nrep = 0, a[0] = 1, a[1] = 0, ed_nav(a, 2);
    check(rep[0] == 1u && rep[1] == 0u && nav_log[1] == 1u && nrep == sizeof ST, "NAV ROW 0: done, the state after");
    nrep = 0, a[0] = 1, a[1] = 2, ed_nav(a, 2);
    check(rep[0] == 1u && rep[1] == 1u && nav_log[1] == 3u, "NAV ROW 2: refused by the UI, the state still sent");
    nrep = 0, a[0] = 1, ed_nav(a, 1);
    check(rep[0] == 1u && rep[1] == 1u, "NAV ROW without its row: refused");
    nrep = 0, a[0] = 2, a[1] = 3, ed_nav(a, 2);
    check(rep[0] == 2u && rep[1] == 0u && nav_log[2] == 4u, "NAV TRACK 3: the walk asked, done");
    nrep = 0, a[0] = 3, a[1] = 2, a[2] = 7, ed_nav(a, 3);
    check(rep[0] == 3u && rep[1] == 0u && nav_log[3] == (2u << 8 | 7u), "NAV OPEN screen 2 button 7: asked, done");
    nrep = 0, a[0] = 3, a[1] = 2, ed_nav(a, 2);
    check(rep[0] == 3u && rep[1] == 1u, "NAV OPEN without its button: refused");
    nrep = 0, a[0] = 5, ed_nav(a, 1);
    check(rep[0] == 5u && rep[1] == 1u, "NAV op 5: refused");
    mir.on = 0;
    nrep = 0, a[0] = 0, ed_nav(a, 1);
    check(rep[0] == 0u && rep[1] == 1u, "NAV while not mirroring: refused");
}

int main(void)
{
    uint32_t k;
    usb_tests();
    panel_tests();
    bt_tests();
    nav_tests();
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
