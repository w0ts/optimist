/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the BLE-MIDI packet codec (firmware/src/ble/ble_midi.c), after the MIDI Association's
 * "Specification for MIDI over Bluetooth Low Energy" 1.0:
 *   decoder   one note; running status with and without its own timestamp octet; a low timestamp that wraps
 *             (the high part steps); real time between and inside messages and inside SysEx; SysEx in one packet
 *             and over three; SysEx cut short by a status; system common (F1 F2 F3 F6, no running status after);
 *             program change running status; a message cut at a packet's end; bad headers; the last timestamp
 *   encoder   header and timestamps; running status inside a packet only; real time keeps it; the high part
 *             stepping inside a packet, a jump that needs a new packet; packing to the cap; SysEx over packets
 *             (continuation packets start with data); events that are not messages dropped
 *   round trip 20000 random events (notes, CC, program change, bend, real time, system common, SysEx of
 *             1..300 octets) through packets of random size 20..244 and back: the same events, the same
 *             13-bit times (SysEx data inside has none of its own: F0, F7 and real time keep theirs) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ble/ble_midi.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint32_t got[4096];
static uint16_t got_ts[4096];
static unsigned ngot;
static void sink(void *ctx, uint32_t pkt, uint16_t ts)
{
    (void)ctx;
    got_ts[ngot % 4096] = ts;
    got[ngot++ % 4096] = pkt;
}
static struct ble_midi_dec D;
static int dec(const uint8_t *p, unsigned n)
{
    return ble_midi_dec(&D, p, (uint16_t)n, sink, 0);
}
#define DEC(...)                                                                                                   \
    do {                                                                                                           \
        static const uint8_t b_[] = {__VA_ARGS__};                                                                 \
        dec(b_, sizeof b_);                                                                                        \
    } while (0)
#define P(cin, a, b, c) ((uint32_t)(cin) | (uint32_t)(a) << 8 | (uint32_t)(b) << 16 | (uint32_t)(c) << 24)

static void test_decoder(void)
{
    ble_midi_dec_reset(&D);
    ngot = 0;
    DEC(0x80, 0x80, 0x90, 0x3C, 0x64);
    check("one note on at 0", ngot == 1 && got[0] == P(9, 0x90, 0x3C, 0x64) && got_ts[0] == 0);
    ngot = 0;
    DEC(0x81, 0x85, 0x90, 0x3C, 0x64, 0x3E, 0x64, 0x87, 0x40, 0x00);
    check("running status: without a timestamp (same time), with one (its time)",
          ngot == 3 && got[1] == P(9, 0x90, 0x3E, 0x64) && got_ts[1] == (1 << 7 | 5) && got[2] == P(9, 0x90, 0x40, 0) &&
              got_ts[2] == (1 << 7 | 7));
    ngot = 0;
    DEC(0x85, 0xFF, 0xB0, 0x07, 0x10, 0x81, 0xB0, 0x07, 0x11, 0x83, 0xF8, 0x82, 0xFA);
    check("a low timestamp wrapping: 127 then 1 -> high + 1; 3 (that high); 2 -> high + 1 again",
          ngot == 4 && got_ts[0] == (5 << 7 | 127) && got_ts[1] == (6 << 7 | 1) && got_ts[2] == (6 << 7 | 3) &&
              got_ts[3] == (7 << 7 | 2));
    check("the last timestamp of that packet", ble_midi_last_ts((const uint8_t[]){0x85, 0xFF, 0xB0, 0x07, 0x10, 0x81,
                                                                                   0xB0, 0x07, 0x11, 0x83, 0xF8, 0x82,
                                                                                   0xFA}, 13) == (7 << 7 | 2));
    ngot = 0;
    DEC(0xBF, 0xFF, 0x80, 0x01, 0x02, 0x81, 0x7F, 0x00);     /* high 63 + 1 wraps to 0 */
    check("the high part wraps 63 -> 0", ngot == 2 && got_ts[1] == 1);
    ngot = 0;
    DEC(0x80, 0x81, 0x90, 0x3C, 0x64, 0x81, 0xF8, 0x82, 0x3E, 0x64);
    check("real time between running-status messages keeps the status",
          ngot == 3 && got[1] == P(0xF, 0xF8, 0, 0) && got[2] == P(9, 0x90, 0x3E, 0x64) && got_ts[2] == 2);
    ngot = 0;
    DEC(0x80, 0x81, 0xF0, 0x01, 0x02, 0x03, 0x04, 0x82, 0xF7);
    check("SysEx in one packet: F0 01 02 (CIN 4), 03 04 F7 (CIN 7) at the F7's time",
          ngot == 2 && got[0] == P(4, 0xF0, 1, 2) && got[1] == P(7, 3, 4, 0xF7) && got_ts[1] == 2);
    ngot = 0;
    DEC(0x80, 0x81, 0xF0, 0x01);
    DEC(0x80, 0x02, 0x03, 0x81, 0xF8, 0x04);
    DEC(0x80, 0x05, 0x82, 0xF7);
    check("SysEx over three packets, real time inside: F0 01 02 | F8 | 03 04 05 | F7",
          ngot == 4 && got[0] == P(4, 0xF0, 1, 2) && got[1] == P(0xF, 0xF8, 0, 0) && got[2] == P(4, 3, 4, 5) &&
              got[3] == P(5, 0xF7, 0, 0));
    ngot = 0;
    DEC(0x80, 0x81, 0xF0, 0x01, 0x82, 0x90, 0x3C, 0x64);
    check("SysEx cut short by a status: dropped, the note played", ngot == 1 && got[0] == P(9, 0x90, 0x3C, 0x64) &&
                                                                       !D.sysex);
    ngot = 0;
    DEC(0x80, 0x81, 0xF2, 0x10, 0x20, 0x81, 0xF1, 0x33, 0x81, 0xF6, 0x81, 0xF3, 0x05, 0x06);
    check("system common: F2 (CIN 3), F1 (CIN 2), F6 (CIN 5), F3; no running status after",
          ngot == 4 && got[0] == P(3, 0xF2, 0x10, 0x20) && got[1] == P(2, 0xF1, 0x33, 0) && got[2] == P(5, 0xF6, 0, 0) &&
              got[3] == P(2, 0xF3, 5, 0));
    ngot = 0;
    DEC(0x80, 0x81, 0xC3, 0x05, 0x06, 0x82, 0x07);
    check("program change, running status: three", ngot == 3 && got[2] == P(0xC, 0xC3, 7, 0) && got_ts[2] == 2);
    ngot = 0;
    DEC(0x80, 0x81, 0x90, 0x3C);
    DEC(0x80, 0x81, 0x64);
    check("a message cut at a packet's end: lost (nothing half-played)", ngot == 0);
    ngot = 0;
    check("header 0x40 / 0xC0: not a packet", !dec((const uint8_t[]){0x40, 0x80, 0xF8}, 3) &&
                                                  !dec((const uint8_t[]){0xC0, 0x80, 0xF8}, 3) && ngot == 0);
    check("a header alone: nothing", dec((const uint8_t[]){0x80}, 1) && ngot == 0);
    DEC(0x80, 0x81, 0xF7, 0x81, 0xF4, 0x05);
    check("a lone F7, an undefined F4: ignored", ngot == 0);
}

static void test_encoder(void)
{
    struct ble_midi_enc e;
    uint8_t b[300];
    int i, r;
    memset(&e, 0, sizeof e);
    ble_midi_enc_begin(&e, b, 20);
    check("nothing added: length 0", ble_midi_enc_len(&e) == 0);
    ble_midi_enc_add(&e, P(9, 0x90, 0x3C, 0x64), 1000);
    ble_midi_enc_add(&e, P(9, 0x90, 0x3E, 0x64), 1001);
    ble_midi_enc_add(&e, P(0xF, 0xF8, 0, 0), 1001);
    ble_midi_enc_add(&e, P(9, 0x90, 0x40, 0x64), 1002);
    ble_midi_enc_add(&e, P(8, 0x80, 0x3C, 0x00), 1002);
    {
        static const uint8_t want[] = {0x80 | 7, 0x80 | 104, 0x90, 0x3C, 0x64, 0x80 | 105, 0x3E, 0x64, 0x80 | 105, 0xF8,
                                       0x80 | 106, 0x40, 0x64, 0x80 | 106, 0x80, 0x3C, 0x00};
        check("header, timestamps, running status, real time keeps it, a new status",
              ble_midi_enc_len(&e) == sizeof want && !memcmp(b, want, sizeof want));
    }
    ble_midi_enc_begin(&e, b, 20);
    r = ble_midi_enc_add(&e, P(9, 0x90, 0x3C, 0x64), 1002);
    check("running status not carried into a new packet", r == 1 && b[2] == 0x90);
    ble_midi_enc_begin(&e, b, 64);
    ble_midi_enc_add(&e, P(9, 0x90, 1, 1), 127);
    r = ble_midi_enc_add(&e, P(9, 0x90, 2, 1), 129);           /* high 0 -> 1, low 127 -> 1 */
    check("the high part steps inside a packet (low 127 -> 1)", r == 1 && b[0] == 0x80 && b[5] == 0x81);
    r = ble_midi_enc_add(&e, P(9, 0x90, 3, 1), 129 + 200);
    check("a jump past the next high part: needs a new packet", r == 0);
    r = ble_midi_enc_add(&e, P(9, 0x90, 3, 1), 120);
    check("time going back: needs a new packet", r == 0);
    ble_midi_enc_begin(&e, b, 20);
    for (i = 0; i < 10 && ble_midi_enc_add(&e, P(0xB, 0xB0, i, 0), 5) == 1; i++)
        ;
    check("packing to 20 octets: header + 4 + 5 x 3 = 20", i == 6 && ble_midi_enc_len(&e) == 20);
    ble_midi_enc_reset(&e);
    ble_midi_enc_begin(&e, b, 8);
    r = ble_midi_enc_add(&e, P(4, 0xF0, 1, 2), 10);
    r += ble_midi_enc_add(&e, P(4, 3, 4, 5), 10);
    r += 10 * ble_midi_enc_add(&e, P(6, 6, 0xF7, 0), 11);
    check("SysEx: F0 01 02 03 04 05 fills 8 octets; the end needs the next packet",
          r == 2 && ble_midi_enc_len(&e) == 8 && !memcmp(b, (const uint8_t[]){0x80, 0x8A, 0xF0, 1, 2, 3, 4, 5}, 8) &&
              e.sysex);
    ble_midi_enc_begin(&e, b, 8);
    r = ble_midi_enc_add(&e, P(6, 6, 0xF7, 0), 11);
    check("continuation packet: header, data at once, timestamp before F7",
          r == 1 && ble_midi_enc_len(&e) == 4 && !memcmp(b, (const uint8_t[]){0x80, 6, 0x8B, 0xF7}, 4) && !e.sysex);
    ble_midi_enc_begin(&e, b, 64);
    check("dropped: CIN 0 / 1, a status not its CIN's, a data octet with bit 7, a continuation with no SysEx open",
          ble_midi_enc_add(&e, P(0, 0x90, 1, 1), 0) == -1 && ble_midi_enc_add(&e, P(1, 0x90, 1, 1), 0) == -1 &&
              ble_midi_enc_add(&e, P(9, 0x80, 1, 1), 0) == -1 && ble_midi_enc_add(&e, P(9, 0x90, 0x81, 1), 0) == -1 &&
              ble_midi_enc_add(&e, P(4, 1, 2, 3), 0) == -1 && ble_midi_enc_add(&e, P(0xF, 0x40, 0, 0), 0) == -1 &&
              ble_midi_enc_len(&e) == 0);
    r = ble_midi_enc_add(&e, P(6, 0xF0, 0xF7, 0), 3);
    check("a whole SysEx F0 F7 in one USB packet", r == 1 && !memcmp(b, (const uint8_t[]){0x80, 0x83, 0xF0, 0x83, 0xF7}, 5));
}

/* ---- round trip */
static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}
#define N_EV 20000
static uint32_t ev[N_EV + 400], ev_t[N_EV + 400];
static unsigned nev;

static void gen(uint32_t *t)
{
    uint32_t k = rnd(12), ch = rnd(16);
    *t += rnd(4) ? rnd(3) : rnd(200);              /* mostly close together, sometimes far */
    if (k < 4)
        ev_t[nev] = *t, ev[nev++] = P(9, 0x90 | ch, rnd(128), rnd(128));
    else if (k < 6)
        ev_t[nev] = *t, ev[nev++] = P(0xB, 0xB0 | ch, rnd(128), rnd(128));
    else if (k == 6)
        ev_t[nev] = *t, ev[nev++] = P(0xC, 0xC0 | ch, rnd(128), 0);
    else if (k == 7)
        ev_t[nev] = *t, ev[nev++] = P(0xE, 0xE0 | ch, rnd(128), rnd(128));
    else if (k == 8)
        ev_t[nev] = *t, ev[nev++] = P(0xF, 0xF8 + rnd(8), 0, 0);
    else if (k == 9)
        ev_t[nev] = *t, ev[nev++] = rnd(2) ? P(3, 0xF2, rnd(128), rnd(128)) : P(2, 0xF3, rnd(128), 0);
    else {                                         /* SysEx of 1..300 data octets, maybe real time inside */
        uint32_t len = 1 + rnd(rnd(4) ? 20 : 300), i, d[310], n = 0;
        d[n++] = 0xF0;
        for (i = 0; i < len; i++)
            d[n++] = rnd(128);
        d[n++] = 0xF7;
        for (i = 0; i < n;) {
            uint32_t left = n - i;
            if (left <= 3) {
                ev_t[nev] = *t, ev[nev++] = (4 + left) | d[i] << 8 | (left > 1 ? d[i + 1] << 16 : 0) |
                                            (left > 2 ? d[i + 2] << 24 : 0);
                i += left;
            } else {
                ev_t[nev] = *t, ev[nev++] = P(4, d[i], d[i + 1], d[i + 2]);
                i += 3;
                if (!rnd(8))
                    ev_t[nev] = *t, ev[nev++] = P(0xF, 0xFE, 0, 0);
            }
            *t += rnd(3) == 0;
        }
    }
}

static void test_round_trip(void)
{
    struct ble_midi_enc e;
    uint8_t b[300];
    uint32_t t = 7000, i, pkts = 0, ok = 1;
    unsigned k = 0;
    nev = 0;
    while (nev < N_EV)
        gen(&t);
    memset(&e, 0, sizeof e);
    ble_midi_dec_reset(&D);
    ngot = 0;
    while (k < nev) {
        uint16_t cap = (uint16_t)(20 + rnd(225)), len;
        int r = 1;
        ble_midi_enc_begin(&e, b, cap);
        while (k < nev && (r = ble_midi_enc_add(&e, ev[k], ev_t[k])) != 0)
            k++;
        len = ble_midi_enc_len(&e);
        if (!len) {
            printf("stuck at event %u (%08X)\n", k, ev[k]);
            ok = 0;
            break;
        }
        ngot = 0;
        dec(b, len);
        pkts++;
        for (i = 0; i < ngot && ok; i++) {
            static unsigned m;
            /* (SysEx data in the middle has no time of its own: it takes the last one) */
            int timed = (ev[m] & 15) != 4 || ((ev[m] >> 8) & 0xFF) == 0xF0;
            if (got[i] != ev[m] || (timed && got_ts[i] != (ev_t[m] & 0x1FFF))) {
                printf("event %u: got %08X at %u, want %08X at %u\n", m, got[i], got_ts[i], ev[m], ev_t[m] & 0x1FFF);
                ok = 0;
            }
            m++;
            if (i + 1 == ngot && k == nev)
                ok &= m == nev;
        }
    }
    printf("  (%u events in %u packets)\n", nev, pkts);
    check("round trip: random events through packets of 20..244 octets, same events and times", ok);
}

int main(void)
{
    test_decoder();
    test_encoder();
    test_round_trip();
    printf("%s\n", fails ? "BLE-MIDI: FAILED" : "BLE-MIDI: all passed");
    return fails != 0;
}
