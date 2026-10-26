/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* BLE-MIDI in, the whole path on the host (tests/run_tests.sh): a central's ATT Write Commands on the MIDI I/O value
 * (handle 0x000E) -> ble_att.c -> the BLE-MIDI decoder (ble_midi.c) -> midi_ble.c's ring (ble_app_midi_in) -> the
 * TIMER5 tick (ble_midi_poll) -> midi_in_q -> seq.c's events_block -> the synth's voices. The packets are shaped as
 * CoreMIDI's: a header, a timestamp before every status, several messages per packet, running status with and
 * without its own timestamp, the low timestamp wrapping into the header's high part, clock (F8) and active sensing
 * (FE) mixed in, a SysEx over two packets, note offs as 9x velocity 0. It also checks blell's counters (ble_dg.mi_*,
 * ble_mdg), a flood the size of the FM-1's run (5050 writes) without a ring overflow, clock and transport queued for
 * clock_sync.c (MSRC_BLE) with the sender's times on ours (ble_in_time), and CLOCK OUT's F8s out at their time.
 * Exit status: the number of failed checks. */
#define FELUCCA_BLE 1
#define FELUCCA_BLE_STUB 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static void fm1_ble_irqs_hold(int on) { (void)on; }
#define FELUCCA_FLASH 0
#include "../firmware/src/io/midi/midi_ble.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("ble_midi_in: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void blocks(uint32_t n)
{
    int32_t out[CTL * 2];
    while (n--) {
        mix_block(out, CTL);
        mo_r = mo_w;                            /* (MIDI OUT: not looked at here) */
    }
}

/* one Write Command (opcode 0x52, the handle, the BLE-MIDI packet) as the L2CAP layer hands it to the ATT server */
static void wcmd(uint16_t h, const uint8_t *p, uint32_t n)
{
    uint8_t f[260];
    f[0] = 0x52;
    f[1] = (uint8_t)h;
    f[2] = (uint8_t)(h >> 8);
    memcpy(f + 3, p, n);
    ble_att_rx(f, (uint16_t)(3u + n));
}
#define WCMD(...)                                                                                                  \
    do {                                                                                                           \
        static const uint8_t pk_[] = {__VA_ARGS__};                                                                \
        wcmd(0x000E, pk_, sizeof pk_);                                                                             \
    } while (0)

static void tick(void)                          /* the TIMER5 tick's BLE part, then an audio block (events_block) */
{
    ble_midi_poll();
    blocks(1);
}

static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t ngated(void)
{
    uint32_t i, k, n = 0;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].gate;
    return n;
}

static void ble_diag_clear_midi(void);
static void reset(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_held, 0, sizeof midi_held);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    midi_nheld = 0;
    host_tracks_init();
    bps_defaults();
    mi_w = mi_r = mo_w = mo_r = 0;
    bmi_w = bmi_r = 0;
    song.sel = 0;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = trk[i].p[P_AHOLD] = 0;
        trk[i].p[P_SUS] = 127;
    }
    song.g[G_BPM] = 120;
    ble_att_reset();
    memset(&ble_mdg, 0, sizeof ble_mdg);
    ble_diag_clear_midi();
    ble_on = 1;
    ble_midi_route = BLE_ROUTE_IN | BLE_ROUTE_OUT;
}

static void ble_diag_clear_midi(void)
{
    ble_dg.mi_wcmd = ble_dg.mi_wreq = ble_dg.mi_w_other = ble_dg.mi_pkts = ble_dg.mi_bad_hdr = 0;
    ble_dg.mi_on = ble_dg.mi_off = ble_dg.mi_cc = ble_dg.mi_clock = ble_dg.mi_sense = ble_dg.mi_other = 0;
    ble_dg.mi_raw_n = ble_dg.mi_msg_n = 0;
}

static void t_apple_packets(void)
{
    reset();
    WCMD(0xA8, 0xC5, 0x90, 0x3C, 0x64);                              /* one note on, channel 1 */
    tick();
    check(gated(&trk[0], 60), "a single note on (header, timestamp, 90 3C 64) plays the selected track");
    WCMD(0xA8, 0xC6, 0xF8, 0xC7, 0x90, 0x40, 0x64, 0xC8, 0x43, 0x64);   /* clock, a note, running status + timestamp */
    tick();
    check(gated(&trk[0], 64) && gated(&trk[0], 67), "clock then two notes, the second by running status after its timestamp");
    WCMD(0xA8, 0xC9, 0x80, 0x3C, 0x00, 0x40, 0x00, 0xCA, 0x90, 0x43, 0x00);   /* offs: running status without one */
    tick();
    check(!gated(&trk[0], 60) && !gated(&trk[0], 64) && !gated(&trk[0], 67),
          "note offs: 80 with running status (no timestamp), 9x velocity 0");
    WCMD(0xA8, 0xFF, 0x90, 0x48, 0x50, 0x81, 0x4A, 0x50);            /* the low part wraps: 0x7F then 0x01 */
    tick();
    check(gated(&trk[0], 72) && gated(&trk[0], 74), "the timestamp wrapping inside a packet");
    WCMD(0xA9, 0x82, 0xF0, 0x7E, 0x7F, 0x0D, 0x70);                  /* a SysEx (MIDI-CI like) over two packets */
    WCMD(0xA9, 0x01, 0x02, 0x03, 0x83, 0xF7, 0x84, 0x80, 0x48, 0x00, 0x85, 0xFE);
    tick();
    check(!gated(&trk[0], 72) && gated(&trk[0], 74), "a SysEx over two packets, then a note off and active sensing");
    WCMD(0xA9, 0x86, 0x80, 0x4A, 0x00);
    tick();
    check(ngated() == 0u, "everything ended");
    check(ble_dg.mi_wcmd == 7u && ble_dg.mi_pkts == 7u && ble_dg.mi_bad_hdr == 0u && ble_dg.mi_w_other == 0u,
          "blell: 7 Write Commands on 0x000E, 7 packets decoded");
    check(ble_dg.mi_on == 5u && ble_dg.mi_off == 5u && ble_dg.mi_clock == 1u && ble_dg.mi_sense == 1u &&
              ble_dg.mi_other == 3u,
          "blell: 5 note ons, 5 offs, 1 clock, 1 active sensing, 3 SysEx event packets");
    check(ble_mdg.pushed == 11u && ble_mdg.drained == 11u && ble_mdg.drained_on == 5u && ble_mdg.not_chan == 4u &&
              ble_mdg.overflow == 0u && ble_mdg.off == 0u,
          "blell: 10 channel messages and the clock queued and drained into midi_in_q, the 4 others left out");
    check(ble_dg.mi_raw_len[(ble_dg.mi_raw_n - 1u) & 3u] == 5u && ble_dg.mi_raw[(ble_dg.mi_raw_n - 1u) & 3u][2] == 0x80 &&
              ble_dg.mi_msg[(ble_dg.mi_msg_n - 1u) & 3u] == (0x08u | 0x80u << 8 | 0x4Au << 16),
          "blell: the last raw packet and the last decoded message");
}

static void t_channels(void)
{
    reset();
    song.sel = 1;
    WCMD(0x80, 0x80, 0x93, 0x3C, 0x64);                              /* channel 4: nobody's -> the selected track */
    tick();
    check(gated(&trk[1], 60), "a channel no track has plays the selected track");
    WCMD(0x80, 0x81, 0x90, 0x3E, 0x64);                              /* channel 1: track 1's */
    tick();
    check(gated(&trk[0], 62), "channel 1 plays track 1");
    WCMD(0x80, 0x82, 0x83, 0x3C, 0x00, 0x83, 0x80, 0x3E, 0x00);
    tick();
    check(ngated() == 0u, "... and their note offs end them");
}

static void t_wrong_handle_and_off(void)
{
    reset();
    WCMD(0x80, 0x80, 0x90, 0x3C, 0x64);
    {
        static const uint8_t p[] = {0x80, 0x80, 0x90, 0x3E, 0x64};
        wcmd(0x0010, p, sizeof p);                                   /* a handle that is not ours */
    }
    tick();
    check(gated(&trk[0], 60) && !gated(&trk[0], 62) && ble_dg.mi_w_other == 1u && ble_dg.mi_w_other_h == 0x10u,
          "a Write Command on another handle is counted, not played");
    ble_on = 0;
    WCMD(0x80, 0x81, 0x90, 0x40, 0x64);
    tick();
    check(!gated(&trk[0], 64) && ble_mdg.off == 1u, "BLUETOOTH OFF: nothing queued (counted)");
}

static void t_flood(void)
{
    uint32_t i, ons = 0;
    reset();
    for (i = 0; i < 5050u; i++) {                                    /* the FM-1's run: 5050 writes, clock heavy */
        uint8_t lo = (uint8_t)(0x80u | (i & 0x7Fu));
        if (i % 8u == 3u) {
            uint8_t p[] = {0xA0, lo, 0xF8, lo, 0x90, (uint8_t)(48u + (i / 8u) % 24u), 0x64};
            wcmd(0x000E, p, sizeof p);
            ons++;
        } else if (i % 8u == 6u) {
            uint8_t p[] = {0xA0, lo, 0x80, (uint8_t)(48u + (i / 8u) % 24u), 0x00};
            wcmd(0x000E, p, sizeof p);
        } else {
            uint8_t p[] = {0xA0, lo, 0xF8};
            wcmd(0x000E, p, sizeof p);
        }
        if (i % 4u == 3u)                                            /* (7.5 ms connection events, a 0.5 ms tick) */
            tick();
    }
    tick();
    check(ble_dg.mi_wcmd == 5050u && ble_dg.mi_on == ons && ble_mdg.overflow == 0u && ble_mdg.drained_on == ons &&
              ble_mdg.drained == ble_mdg.pushed,
          "5050 writes, clock between the notes: every note on reaches midi_in_q, no overflow");
    check(ngated() == 0u, "... and every note ended");
}

/* F8s at 120 BPM (20.83 ms), stamped by the sender to the ms, delivered at 15 ms connection events (one, two or none
 * a packet): their times in midi_in_q keep the sender's spacing, not the events'; FA FB FC F2 go too, as MSRC_BLE */
static void t_clock_in(void)
{
    uint32_t k, n = 0, bad = 0, src_ok = 1, ev;
    double send[64];
    uint32_t got_t[64];
    reset();
    host_now = 1000000u;
    for (k = 0; k < 64u; k++)
        send[k] = 5000.0 + k * 20.8333;                       /* ms, the sender's clock */
    for (ev = 0, k = 0; k < 64u; ev++) {                      /* the connection events */
        double at = 5000.0 + ev * 15.0 + 3.0;                 /* (3 ms after the first F8: the air, the stack) */
        uint8_t p[16];
        uint32_t m = 1, ms;
        if (send[k] > at)
            continue;
        ms = (uint32_t)send[k];
        p[0] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu));
        while (k < 64u && send[k] <= at) {
            ms = (uint32_t)send[k++];
            p[m++] = (uint8_t)(0x80u | (ms & 0x7Fu));
            p[m++] = 0xF8;
        }
        host_now = (uint32_t)(1000000.0 + (at - 5000.0) * 24000.0);
        wcmd(0x000E, p, m);
        while (bmi_r != bmi_w) {
            got_t[n] = ble_in_t[bmi_r % BMQ];
            src_ok &= ((ble_in_q[bmi_r % BMQ] >> 8) & 0xFFu) == 0xF8u;
            bmi_r++;
            n++;
        }
    }
    for (k = 9; k < n; k++) {                                 /* (from the 10th: the least delay found) */
        double d = (double)(got_t[k] - got_t[k - 1]) / 24000.0;
        bad += d < 19.5 || d > 22.0;
    }
    check(n == 64u && src_ok && !bad, "BLE F8s at 120 BPM over 15 ms connection events: 20.8 +- 1 ms apart, as sent");
    reset();
    WCMD(0x80, 0x80, 0xFA, 0x81, 0xF8, 0x82, 0xFC, 0x83, 0xF2, 0x08, 0x01, 0x84, 0xFB);
    tick();
    check(ble_mdg.pushed == 5u && ble_mdg.not_chan == 0u, "FA F8 FC F2 FB from BLE: queued for the clock (clock_sync.c)");
}

/* the same F8s over 30 ms connection events (a Mac's while Bluetooth audio streams, ble_host.c ble_sig_audio), about 4 in
 * 10 lost: their F8s go at the next event, 30 ms later and more (the FM-1, 2026-10-25: the clock wandered). Each late
 * packet must not move the times: only a run of them does (ble_in_time BIN_LATE_N) */
static void t_clock_in_lossy(void)
{
    enum { NF = 480 };
    uint32_t k, n = 0, bad = 0, ev;
    double send[NF], dmax = 0;
    static uint32_t got_t[NF];
    reset();
    memset(&bin, 0, sizeof bin);
    for (k = 0; k < NF; k++)
        send[k] = 9000.0 + k * 20.8333;
    for (ev = 0, k = 0; k < NF; ev++) {
        double at = 9000.0 + ev * 30.0 + 3.0;
        uint8_t p[40];
        uint32_t m = 1, ms;
        if (send[k] > at || (ev * 7u) % 10u < 4u)
            continue;                                         /* (nothing yet, or this event lost: the next one) */
        ms = (uint32_t)send[k];
        p[0] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu));
        while (k < NF && send[k] <= at && m + 2u <= sizeof p) {
            ms = (uint32_t)send[k++];
            p[m++] = (uint8_t)(0x80u | (ms & 0x7Fu));
            p[m++] = 0xF8;
        }
        host_now = (uint32_t)(1000000.0 + (at - 5000.0) * 24000.0);
        wcmd(0x000E, p, m);
        while (bmi_r != bmi_w) {
            got_t[n++] = ble_in_t[bmi_r % BMQ];
            bmi_r++;
        }
    }
    for (k = 24; k < n; k++) {                                /* (from the second beat: the least delay found) */
        double d = (double)(got_t[k] - got_t[k - 1]) / 24000.0;
        if (fabs(d - 20.8333) > dmax)
            dmax = fabs(d - 20.8333);
        bad += d < 19.5 || d > 22.0;
    }
    if (bad)
        printf("  lossy: %u of %u F8s off, the worst %.1f ms off its 20.8 ms\n", bad, n, dmax);
    check(n == NF && !bad, "BLE F8s over 30 ms events, 4 in 10 lost: still 20.8 +- 1 ms apart (a late packet moves nothing)");
}

/* CLOCK OUT to a listening central: an F8 due later waits, then goes with its time; nobody listening: nothing */
static void t_clock_out(void)
{
    uint32_t pkt = 0, t = 0;
    reset();
    host_now = 24000000u;
    fm1_ms = 1000u;
    ble_out_on = 1;
    ble_rt_push(host_now + 48000u, 0x0Fu | 0xF8u << 8);       /* due in 2 ms */
    check(!ble_app_midi_peek(&pkt, &t), "CLOCK OUT: an F8 due in 2 ms is not sent yet");
    host_now += 72000u;                                       /* 3 ms on */
    fm1_ms += 3u;
    check(ble_app_midi_peek(&pkt, &t) && pkt == (0x0Fu | 0xF8u << 8) && t == 1002u,
          "... 3 ms later: sent, its timestamp the ms it was due");
    ble_app_midi_pop();
    check(!ble_app_midi_peek(&pkt, &t), "... and gone from the queue");
    ble_out_on = 0;
    check(!ble_rt_on(), "nobody listening: CLOCK OUT sends nothing to BLE");
}

int main(void)
{
    t_apple_packets();
    t_channels();
    t_wrong_handle_and_off();
    t_flood();
    t_clock_in();
    t_clock_in_lossy();
    t_clock_out();
    printf("ble_midi_in: %d failed\n", fails);
    return fails;
}
