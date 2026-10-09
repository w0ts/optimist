/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE-MIDI in, the whole path on the host (tests/run_tests.sh): a central's ATT Write Commands on the MIDI I/O value
 * (handle 0x000E) -> ble_att.c -> the BLE-MIDI decoder (ble_midi.c) -> midi_ble.c's ring (ble_app_midi_in) -> the
 * TIMER5 tick (ble_midi_poll) -> midi_in_q -> seq.c's events_block -> the synth's voices. The packets are shaped as
 * CoreMIDI's: a header, a timestamp before every status, several messages per packet, running status with and
 * without its own timestamp, the low timestamp wrapping into the header's high part, clock (F8) and active sensing
 * (FE) mixed in, a SysEx over two packets, note offs as 9x velocity 0. It also checks blell's counters (ble_dg.mi_*,
 * ble_mdg) and a flood the size of the FM-1's run (5050 writes) without a ring overflow.
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
    drums.set = -2;
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
    check(ble_mdg.pushed == 10u && ble_mdg.drained == 10u && ble_mdg.drained_on == 5u && ble_mdg.not_chan == 5u &&
              ble_mdg.overflow == 0u && ble_mdg.off == 0u,
          "blell: 10 channel messages queued and drained into midi_in_q, the 5 others left out");
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

int main(void)
{
    t_apple_packets();
    t_channels();
    t_wrong_handle_and_off();
    t_flood();
    printf("ble_midi_in: %d failed\n", fails);
    return fails;
}
