/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* BLE-MIDI in, the whole path on the host (tests/run_tests.sh): a central's ATT Write Commands on the MIDI I/O value
 * (handle 0x000E) -> ble_att.c -> the BLE-MIDI decoder (ble_midi.c) -> midi_ble.c's ring (ble_app_midi_in) -> the
 * TIMER5 tick (ble_midi_poll) -> midi_in_q -> seq.c's events_block -> the synth's voices. The packets are shaped as
 * CoreMIDI's: a header, a timestamp before every status, several messages per packet, running status with and
 * without its own timestamp, the low timestamp wrapping into the header's high part, clock (F8) and active sensing
 * (FE) mixed in, a SysEx over two packets, note offs as 9x velocity 0. It also checks what each stage passed on (the
 * decoder's messages, midi_ble.c's ring indexes bmi_w / bmi_r, what reached midi_in_q, the overload monitor's BLE
 * losses), a flood the size of the FM-1's run (5050 writes) without a ring overflow, clock and transport queued for
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

/* The test's own counts. The decoder's view: the same packets written to the MIDI I/O value go through a second
 * ble_midi_dec (the stack's decoder, its own state, reset with the stack's) into kind counts, as the stack's decoder
 * hands them to ble_app_midi_in. The router's view: each tick, what ble_midi_poll put into midi_in_q (before the
 * audio block drains it), by kind and source. */
static struct {
    uint32_t pkts, bad_hdr;                      /* the decoder: packets with a header / without */
    uint32_t on, off, cc, clock, sense, other;   /* the decoder's messages by kind (9x vel 0 an off) */
    uint32_t last_msg;                           /* the decoder's last message */
} tdec_n;
static struct ble_midi_dec tdec;
static struct {
    uint32_t seen;                               /* midi_in_q's write index looked at up to */
    uint32_t n, on, off, rt, not_ble;            /* entries; note ons, offs; clock / transport / F2; another source */
    uint32_t last;                               /* the last entry */
    uint32_t rt_seq[8];                          /* the first clock / transport statuses, in order */
} rq;

static uint32_t msg_kind(uint32_t pkt, uint32_t *on, uint32_t *off, uint32_t *cc, uint32_t *clock, uint32_t *sense)
{
    uint32_t st = (pkt >> 8) & 0xFFu;
    if ((st & 0xF0u) == 0x90u && ((pkt >> 24) & 0x7Fu))
        return ++*on, 1u;
    if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
        return ++*off, 1u;
    if ((st & 0xF0u) == 0xB0u)
        return ++*cc, 1u;
    if (st == 0xF8u)
        return ++*clock, 1u;
    if (st == 0xFEu)
        return ++*sense, 1u;
    return 0u;
}
static void tdec_sink(void *ctx, uint32_t pkt, uint16_t ts)
{
    (void)ctx, (void)ts;
    if (!msg_kind(pkt, &tdec_n.on, &tdec_n.off, &tdec_n.cc, &tdec_n.clock, &tdec_n.sense))
        tdec_n.other++;
    tdec_n.last_msg = pkt;
}

static void rq_look(void)                       /* what ble_midi_poll queued into midi_in_q since the last look */
{
    uint32_t dummy = 0;
    for (; rq.seen != mi_w; rq.seen++) {
        uint32_t pkt = midi_in_q[rq.seen % MQ];
        rq.n++;
        rq.not_ble += midi_in_src[rq.seen % MQ] != MSRC_BLE;
        rq.last = pkt;
        if (ble_clock_msg(pkt)) {
            if (rq.rt < 8u)
                rq.rt_seq[rq.rt] = (pkt >> 8) & 0xFFu;
            rq.rt++;
        } else
            (void)msg_kind(pkt, &rq.on, &rq.off, &dummy, &dummy, &dummy);
    }
}

/* one Write Command (opcode 0x52, the handle, the BLE-MIDI packet) as the L2CAP layer hands it to the ATT server */
static void wcmd(uint16_t h, const uint8_t *p, uint32_t n)
{
    uint8_t f[260];
    if (h == 0x000E) {
        if (ble_midi_dec(&tdec, p, (uint16_t)n, tdec_sink, 0))
            tdec_n.pkts++;
        else
            tdec_n.bad_hdr++;
    }
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
    rq_look();
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
    ble_midi_dec_reset(&tdec);                  /* (the test's decoder starts with the stack's) */
    memset(&tdec_n, 0, sizeof tdec_n);
    memset(&rq, 0, sizeof rq);
    ovl.midi_lost[OVL_MIDI_BLE] = 0;
    ble_on = 1;
    ble_midi_route = BLE_ROUTE_IN | BLE_ROUTE_OUT;
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
    check(tdec_n.pkts == 7u && tdec_n.bad_hdr == 0u,
          "the decoder: the 7 Write Commands' packets on 0x000E, every header valid");
    check(tdec_n.on == 5u && tdec_n.off == 5u && tdec_n.clock == 1u && tdec_n.sense == 1u && tdec_n.other == 3u,
          "the decoder: 5 note ons, 5 offs, 1 clock, 1 active sensing, 3 SysEx event packets");
    check(bmi_w == 11u && bmi_r == 11u && rq.n == 11u && rq.on == 5u && rq.off == 5u && rq.rt == 1u &&
              rq.not_ble == 0u && ovl.midi_lost[OVL_MIDI_BLE] == 0u,
          "10 channel messages and the clock queued (bmi_w) and drained into midi_in_q as BLE's, nothing lost");
    check(tdec_n.on + tdec_n.off + tdec_n.cc + tdec_n.clock + tdec_n.sense + tdec_n.other - bmi_w == 4u,
          "... the decoder's 4 others (active sensing, the SysEx) left out of the ring");
    check(tdec_n.last_msg == (0x08u | 0x80u << 8 | 0x4Au << 16) && rq.last == tdec_n.last_msg,
          "the last decoded message (80 4A 00) is midi_in_q's last");
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
    check(gated(&trk[0], 60) && !gated(&trk[0], 62) && bmi_w == 1u && rq.n == 1u,
          "a Write Command on another handle is not decoded, not queued (only 0x000E's note in the ring), not played");
    ble_on = 0;
    WCMD(0x80, 0x81, 0x90, 0x40, 0x64);
    tick();
    check(!gated(&trk[0], 64) && tdec_n.on == 2u && bmi_w == 1u && rq.n == 1u && ovl.midi_lost[OVL_MIDI_BLE] == 0u,
          "BLUETOOTH OFF: the note decoded, nothing queued (bmi_w, midi_in_q unchanged), not counted as lost");
}

static void t_flood(void)
{
    uint32_t i, ons = 0, msgs = 0;
    reset();
    for (i = 0; i < 5050u; i++) {                                    /* the FM-1's run: 5050 writes, clock heavy */
        uint8_t lo = (uint8_t)(0x80u | (i & 0x7Fu));
        if (i % 8u == 3u) {
            uint8_t p[] = {0xA0, lo, 0xF8, lo, 0x90, (uint8_t)(48u + (i / 8u) % 24u), 0x64};
            wcmd(0x000E, p, sizeof p);
            ons++, msgs += 2u;
        } else if (i % 8u == 6u) {
            uint8_t p[] = {0xA0, lo, 0x80, (uint8_t)(48u + (i / 8u) % 24u), 0x00};
            wcmd(0x000E, p, sizeof p);
            msgs++;
        } else {
            uint8_t p[] = {0xA0, lo, 0xF8};
            wcmd(0x000E, p, sizeof p);
            msgs++;
        }
        if (i % 4u == 3u)                                            /* (7.5 ms connection events, a 0.5 ms tick) */
            tick();
    }
    tick();
    check(tdec_n.pkts == 5050u && tdec_n.on == ons && bmi_w == msgs && ovl.midi_lost[OVL_MIDI_BLE] == 0u &&
              rq.on == ons && bmi_r == bmi_w && rq.n == bmi_w,
          "5050 writes, clock between the notes: every message queued, every note on reaches midi_in_q, no overflow");
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
    check(bmi_w == 5u && rq.rt == 5u && rq.n == 5u && rq.not_ble == 0u && rq.rt_seq[0] == 0xFAu &&
              rq.rt_seq[1] == 0xF8u && rq.rt_seq[2] == 0xFCu && rq.rt_seq[3] == 0xF2u && rq.rt_seq[4] == 0xFBu,
          "FA F8 FC F2 FB from BLE: all 5 queued, in order, into midi_in_q as BLE's for the clock (clock_sync.c)");
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
