/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen mirrored over Bluetooth to a Mac, a transport model (2026-11-01; ed_mirror.c MIR_AIMD). The FM-1's side is the real
 * io/editor/ed_mirror.c (through tests/mirror_test.c's stand-ins); the panel's writes are the real UI's, recorded by
 * tests/ui_optimist_test.c (MIRROR_WORKLOAD: HOME idle, knob drags, single detents, HOME playing, playing + drags, ALGORITHM
 * walked, page changes; 40 s). The rest is a model:
 *   the radio  drains the mirror's ring at LINK_BPS (5.6 KB/s: the Mac's 50 octets each 7.5 ms event, measured 4.6-5.6 KB/s),
 *              one connection event each 7.5 ms
 *   the Mac    takes whole SysEx frames into its MIDI input; the app reads them at MAC_BPS (default 5 KB/s, in 10 ms ticks,
 *              a 40 ms stall now and then: its threads); a frame that arrives while more than MAC_Q bytes wait (1 KB) is lost
 *              whole (the mechanism inferred from the user's logs: losses in bursts exactly while the stream runs at the link)
 *   the app    MirrorDecoder (mirror_dec.h): a gap in seq asks for the lost frames (106 bit 4) 15 ms later; the keep-alive each
 *              second, with opti_miraimd_1 its loss count (106 bit 5)
 * Measured: frames the Mac lost, the client's losses, whole panels, bytes, and the latency from a knob's detent (a page, a track)
 * to the client's pixels in what that frame changed being the panel's (p50 / p90 / max); after 3 s still, the client's picture
 * is the panel. Built twice by run_tests.sh: MIR_AIMD=1 (the budget, checked) and MIR_AIMD=0 (the lag rule before, printed).
 * Usage: mirror_aimd_test WORKLOAD.bin [MAC_BPS [MAC_Q [LINK_BPS]]] */
#define main mirror_test_main
#define MT_OCTETS 50
#define MT_INTERVAL 6
#ifndef MIR_BLE_LAG
#define MIR_BLE_LAG 4096u               /* (the firmware's, not mirror_test.c's smaller one) */
#endif
#include "mirror_test.c"
#undef main

enum { EVENT_US = 7500, MAC_TICK_MS = 10, MAC_STALL_MS = 40, MAX_EV = 4096, QF = 1024 };
static uint32_t mac_bps = 5000, mac_q = 1024, link_bps = 5600;   /* (the link: 5.6 KB/s, one packet an event; ~15 with the refill) */
/* ---- the radio's side: packets back into frames, each into the Mac's input */
static uint8_t rf[1024];
static uint32_t rfn;
static struct { uint16_t n; uint8_t b[512]; } macq[QF];
static uint32_t mq_w, mq_r, mq_bytes, mac_lost, mac_frames, mac_peak;
static void mac_in(void)
{
    if (rfn > 512u || mq_w - mq_r >= QF || mq_bytes + rfn > mac_q) {
        mac_lost++;                     /* (the Mac's input over its queue: the frame lost whole) */
        return;
    }
    macq[mq_w % QF].n = (uint16_t)rfn;
    memcpy(macq[mq_w % QF].b, rf, rfn);
    mq_w++, mq_bytes += rfn;
    mac_peak = mq_bytes > mac_peak ? mq_bytes : mac_peak;
}
static void radio_event(uint32_t budget)
{
    uint32_t p, got = 0;
    while (got < budget && mir_ble_peek(&p)) {
        uint32_t cin = p & 15u, k = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u, i;
        for (i = 0; i < k; i++) {
            uint8_t b = (uint8_t)(p >> (8u + 8u * i));
            if (b == 0xF0u)
                rfn = 0;
            if (rfn < sizeof rf)
                rf[rfn++] = b;
            if (b == 0xF7u)
                mac_in();
        }
        got += mbr.pk;
        mir_ble_pop();
    }
}
/* ---- the app: its sends to the FM-1 (15 ms on the way), the frames it reads */
static struct { uint32_t t, n; uint8_t b[12]; } up[64];
static uint32_t up_w, up_r, fix_asks;
static void app_send(const uint8_t *b, uint32_t n, uint32_t delay)
{
    if (up_w - up_r >= 64u)
        return;
    up[up_w % 64u].t = fm1_ms + delay, up[up_w % 64u].n = n;
    memcpy(up[up_w % 64u].b, b, n);
    up_w++;
}
static void app_mirror(uint32_t bits, int ack)
{
    uint8_t f[9] = {0xF0, 0x7D, 0x46, 0x4C, 106, (uint8_t)bits, 0, 0, 0xF7};
    if (ack) {
        f[5] |= MIR_ACK_BIT, f[6] = (uint8_t)(md.dropped & 127u), f[7] = (uint8_t)(md.seq & 127u);
        app_send(f, 9, 15);
    } else {
        f[6] = 0xF7;
        app_send(f, 7, 15);
    }
}
/* ---- the latency: a detent's frame changed these pixels; resolved once the client holds the panel's there */
static struct { uint32_t t0, n, *px, done; } ev[MAX_EV];
static uint32_t nev, lat[MAX_EV], nlat, ev_open;
static void ev_check(void)
{
    uint32_t e, i;
    for (e = ev_open; e < nev; e++) {
        if (ev[e].done)
            continue;
        for (i = 0; i < ev[e].n && md_fb[ev[e].px[i]] == panel[ev[e].px[i]]; i++)
            ;
        if (i == ev[e].n) {
            ev[e].done = 1;
            lat[nlat++] = fm1_ms - ev[e].t0;
            free(ev[e].px);
            ev[e].px = 0;
        }
    }
    while (ev_open < nev && ev[ev_open].done)
        ev_open++;
}
static void mac_tick(void)
{
    static uint32_t stall_until, budget;
    if (fm1_ms < stall_until)
        return;
    if (mt_rnd(60) == 0u) {             /* (about every 0.6 s: the app's thread away 40 ms) */
        stall_until = fm1_ms + MAC_STALL_MS;
        return;
    }
    budget += mac_bps * MAC_TICK_MS / 1000u;
    while (mq_r != mq_w && macq[mq_r % QF].n <= budget) {
        uint32_t i, n = macq[mq_r % QF].n;
        for (i = 0; i < n; i++)
            host_byte(macq[mq_r % QF].b[i]);
        budget -= n, mq_bytes -= n, mq_r++, mac_frames++;
        if (md.fix_have) {              /* (MirrorLink: each run of lost seqs asked alone, opti_mirfix_1) */
            uint32_t q;
            for (q = 0; q < md.fix_have; q++) {
                uint8_t f[9] = {0xF0, 0x7D, 0x46, 0x4C, 106, 1 | MIR_CHK_BIT | MIR_FIX_BIT, (uint8_t)md.fix_lo[q], (uint8_t)md.fix_hi[q], 0xF7};
                app_send(f, 9, 15);
                fix_asks++;
            }
            md.fix_have = 0;
        }
    }
    if (mq_r == mq_w && budget > 300u)
        budget = 300u;                  /* (nothing waiting: no credit saved up) */
    ev_check();
}
/* ---- the workload: the UI's writes, frame by frame */
static uint8_t *wl;
static long wl_n, wl_i;
static uint16_t prev[240 * 240];
static int ui_frame(void)               /* the next frame's writes; 0: the workload ended */
{
    int knob = 0;
    while (wl_i + 12 <= wl_n) {
        uint16_t hd[6];
        memcpy(hd, wl + wl_i, 12);
        wl_i += 12;
        if (hd[0] == 'M')
            break;
        if (hd[0] == 'K') {
            knob = 1;
            memcpy(prev, panel, sizeof prev);
        } else if (hd[0] == 'F') {
            lcd_fill(hd[1], hd[2], hd[3], hd[4], hd[5]);
        } else if (hd[0] == 'B') {
            static uint16_t px[240 * 240];
            memcpy(px, wl + wl_i, (size_t)hd[3] * hd[4] * 2u);
            wl_i += (long)hd[3] * hd[4] * 2;
            lcd_blit(hd[1], hd[2], hd[3], hd[4], px);
        }
    }
    if (knob && nev < MAX_EV) {
        uint32_t i, n = 0;
        for (i = 0; i < 240u * 240u; i++)
            n += prev[i] != panel[i];
        if (n) {
            ev[nev].px = malloc(n * sizeof(uint32_t));
            for (n = 0, i = 0; i < 240u * 240u; i++)
                if (prev[i] != panel[i])
                    ev[nev].px[n++] = i;
            ev[nev].n = n, ev[nev].t0 = fm1_ms, ev[nev].done = 0;
            nev++;
        }
    }
    return wl_i + 12 <= wl_n;
}
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    uint32_t acked_at = 0, t, next_frame = 0, next_ev_us = 0, end = 0, whole, b_end, same_end;
    int more = 1, ack = MIR_AIMD;
    if (!f) {
        printf("usage: mirror_aimd_test WORKLOAD.bin [MAC_BPS [MAC_Q]] (tests/ui_optimist_test.c MIRROR_WORKLOAD)\n");
        return 2;
    }
    mac_bps = argc > 2 ? (uint32_t)atoi(argv[2]) : mac_bps;
    mac_q = argc > 3 ? (uint32_t)atoi(argv[3]) : mac_q;
    link_bps = argc > 4 ? (uint32_t)atoi(argv[4]) : link_bps;
    fseek(f, 0, SEEK_END);
    wl_n = ftell(f);
    fseek(f, 0, SEEK_SET);
    wl = malloc((size_t)wl_n);
    if (!wl || fread(wl, 1, (size_t)wl_n, f) != (size_t)wl_n)
        return 2;
    fclose(f);
    ble_out_on = 1;
    fm1_ms = 1;
    app_mirror(1 | 2 | MIR_CHK_BIT, 0);   /* (the first ask: on, the whole panel, checked frames) */
    for (t = 1; ; t++) {
        fm1_ms = t;
        while (up_r != up_w && up[up_r % 64u].t <= t) {
            bt_send(up[up_r % 64u].b, up[up_r % 64u].n);
            up_r++;
        }
        if (t >= next_frame && more && t > 200u) {
            more = ui_frame();
            next_frame = t + 16u;
            ev_check();
            if (!more)
                end = t;
        }
        mir_poll();
        while (next_ev_us <= t * 1000u) {
            radio_event(link_bps * EVENT_US / 1000000u);
            next_ev_us += EVENT_US;
        }
        if (t % MAC_TICK_MS == 0u)
            mac_tick();
        if (t % 1000u == 0u)
            app_mirror(1 | MIR_CHK_BIT, ack);
        else if (ack && t % 200u == 0u && mac_frames != acked_at) {   /* (MirrorLink: an ACK alone ~5 times a second, frames came) */
            acked_at = mac_frames;
            app_mirror(1 | MIR_CHK_BIT | MIR_QUIET_BIT, 1);
        }
        if (end && t >= end + 3000u)
            break;
    }
    ev_check();
    same_end = same();
    whole = mst.whole[MIR_WHY_LAG] + mst.whole[MIR_WHY_FIX] + (mst.whole[MIR_WHY_ASK] ? mst.whole[MIR_WHY_ASK] - 1u : 0u);
    b_end = mst.bytes;
    qsort(lat, nlat, sizeof lat[0], cmp_u32);
    printf("mirror over Bluetooth to a Mac (%s; link %u B/s, the Mac %u B/s, its queue %u B): %.1f s, %u B sent (%.0f B/s)\n",
           MIR_AIMD ? "paced, AIMD" : "the lag rule, before", (unsigned)link_bps, (unsigned)mac_bps, (unsigned)mac_q, t / 1000.0,
           (unsigned)b_end, b_end * 1000.0 / t);
    printf("  frames the Mac lost %u of %u (%.2f %%), the client's losses %u, repairs asked %u, whole panels %u, repaints for a lag %u\n",
           (unsigned)mac_lost, (unsigned)(mac_lost + mac_frames), 100.0 * mac_lost / (mac_lost + mac_frames + 1u),
           (unsigned)md.dropped, (unsigned)fix_asks, (unsigned)whole, (unsigned)mst.soft);
    printf("  detent -> pixels: %u events, p50 %u ms, p90 %u ms, max %u ms, %u never; the Mac's queue at most %u B\n",
           (unsigned)nev, nlat ? (unsigned)lat[nlat / 2u] : 0u, nlat ? (unsigned)lat[nlat * 9u / 10u] : 0u,
           nlat ? (unsigned)lat[nlat - 1u] : 0u, (unsigned)(nev - nlat), (unsigned)mac_peak);
#if MIR_AIMD
    printf("  the budget at the end %u B/s, %u cuts (%u for a delay over %u ms), %u raises, %u frames refused\n", (unsigned)mba.rate,
           (unsigned)mba.cuts, (unsigned)mba.delays, (unsigned)MBA_DELAY_MS, (unsigned)mba.ups, (unsigned)mba.refused);
    check(same_end, "paced: 3 s after the workload the client's picture is the panel");
    check(nlat == nev, "paced: every detent's pixels reached the client (%u of %u)", (unsigned)nlat, (unsigned)nev);
    check(whole == 0u, "paced: no whole panel after the first (%u)", (unsigned)whole);
    check(mac_lost * 100u <= mac_lost + mac_frames, "paced: the Mac lost at most 1 %% of the frames (%u of %u)", (unsigned)mac_lost,
          (unsigned)(mac_lost + mac_frames));
    check(nlat && lat[nlat * 9u / 10u] < 1500u, "paced: a detent's pixels within 1.5 s at p90 (%u ms)", nlat ? (unsigned)lat[nlat * 9u / 10u] : 0u);
#else
    printf("  (before: printed for the comparison; the picture %s the panel 3 s after)\n", same_end ? "is" : "is NOT");
#endif
    printf("mirror_aimd: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
