/* SPDX-License-Identifier: GPL-3.0-only */
/* USB audio capture routing (FELUCCA_USB_AUDIO): where each source of the mix goes among the four capture channels
 * (SYSTEM > USB AUDIO, one line a source; the user, 2026-10-17: "each track, including master, can be assigned to none, 1,
 * 2, 1+2, 3, 4, 3+4", and the drum lanes), and the taps in the mix that only a source's choice turns on. Included by core.h
 * (the drums and fx.c write the taps); usb_audio_route.c builds the channels from them (audio.c ua_block).
 *
 * The sources, and where each is taken:
 *   MASTER       the master output as the FM-1's own output gets it, in stereo: after the master inserts, the volume, the
 *                limiter (or LIMIT's brickwall) and the knee, before the codec's -6 dB shift (Q15 full scale = 0 dBFS here).
 *   T1 T2 T3     a synth track's stem: after its inserts, its LEVEL and its mute, before its pan, its sends, the FX buses
 *                and the master (POST-fader: what the track contributes to the mix, without the space). Mono.
 *   DRUMS        the drum track's stem: its lanes summed (mono), each after its lane level, cut and inserts and the drum
 *                track's level and mute, before the lanes' and the track's pan and the drum bus inserts.
 *   DRUM BUS     the drum bus in stereo: after its pans and its two insert slots (dbus_run), before its sends and the master.
 *   DR1..DR10    one drum lane's own share of DRUMS (the same point: post lane level / cut / inserts and the track's level
 *                and mute, pre pan). Only the lanes routed are tapped, only while the host records: a voice of another
 *                lane costs one mask test a block (drums.c, drum_x0x.c, fx.c dins_post). The declick tail of a stopped
 *                voice (drums.tail) belongs to no lane: it is in DRUMS only.
 * The destinations: NONE, 1, 2, 1+2, 3, 4, 3+4. A stereo source (MASTER, DRUM BUS) on a pair goes left to the first channel,
 * right to the second; on one channel, its two sides to mono, (L + R) / 2 (each side x 0.5, -6.02 dB: never clips; a
 * floor). A mono source on a pair is on both. Sources on one channel add up (the capture clips at its conversion,
 * usb_audio_stream.c ua_clip). PRE-fader stems are not offered: a stem is the track's contribution at its mixer level (a
 * muted track records silence). Defaults = the routing before this setting: T1 on 1, T2 on 2, T3 on 3, DRUMS on 4. */
enum { UAS_MAIN, UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS, UAS_DBUS, UAS_DR1, UAS_N = UAS_DR1 + DRUM_LANES };   /* (stored:
                                                                                                         * settings record "UAR3") */
enum { UAD_NONE, UAD_1, UAD_2, UAD_12, UAD_3, UAD_4, UAD_34, UAD_N };   /* a source's channels */
#define UA_ROUTE_N 4u                           /* the capture channels (usb_audio_stream.c UA_CAP_CHANNELS) */
static const uint8_t UAD_MASK[UAD_N] = {0u, 1u, 2u, 3u, 4u, 8u, 12u};   /* each destination's channels, a bit each */
static uint8_t ua_dst[UAS_N] = {[UAS_T1] = UAD_1, [UAS_T2] = UAD_2, [UAS_T3] = UAD_3, [UAS_DRUMS] = UAD_4};   /* the setting */
static uint8_t ua_ident = 1;                    /* ua_dst is the default: the stems go out as they are (no routing) */
static uint8_t ua_dbus_on;                      /* the drum bus is routed */
static uint8_t ua_lane_ch;                      /* the channels some lane goes to */
static uint16_t dlt_mask;                       /* the lanes routed (main loop: ua_route_apply) */
static uint8_t dlt_chm[DRUM_LANES];             /* each lane's channels, a bit each (UAD_MASK) */
/* this block's (fx.c capture_clear, at the block's start): the taps above while the host takes the capture, else 0 */
static uint16_t dlt_live;
static uint8_t ua_dbus_live;
static int32_t dlt_buf[UA_ROUTE_N][CTL];        /* the routed lanes, summed per channel, this block */
static int32_t ua_dbus[2][CTL];                 /* the drum bus, this block (dbus_run) */

/* lane ln's samples i0..i1 of b (its voice's, drum_sends.c dsend_buf) added to its channels, when it is routed */
AINL void dlt_add(uint32_t ln, const int32_t *b, uint32_t i0, uint32_t i1)
{
    if (ln < DRUM_LANES && ((dlt_live >> ln) & 1u)) {
        uint32_t m = dlt_chm[ln], c, i;
        for (c = 0; m; c++, m >>= 1)
            if (m & 1u) {
                int32_t *t = dlt_buf[c & (UA_ROUTE_N - 1u)];
                for (i = i0; i < i1; i++)
                    t[i] += b[i];
            }
    }
}
/* the block's taps on (fx.c capture_clear): cleared for the block to add into */
AINL void dlt_block(uint32_t on, uint32_t n)
{
    uint32_t i, s;
    dlt_live = on ? dlt_mask : 0u;
    ua_dbus_live = (uint8_t)(on && ua_dbus_on);
    if (dlt_live)
        for (s = 0; s < UA_ROUTE_N; s++)
            for (i = 0; i < n; i++)
                dlt_buf[s][i] = 0;
}
/* the drum bus after its inserts (fx.c dbus_run's end; the drums alone on mix_l / mix_r then) */
AINL void ua_dbus_tap(const int32_t *l, const int32_t *r, uint32_t n)
{
    uint32_t i;
    if (!ua_dbus_live)
        return;
    for (i = 0; i < n; i++) {
        ua_dbus[0][i] = l[i];
        ua_dbus[1][i] = r[i];
    }
}
/* ua_dst set (the setting changed, or loaded): the taps it needs (main loop; the audio interrupt reads them) */
static void ua_route_apply(void)
{
    static const uint8_t DEF[UAS_N] = {[UAS_T1] = UAD_1, [UAS_T2] = UAD_2, [UAS_T3] = UAD_3, [UAS_DRUMS] = UAD_4};
    uint32_t s, m = 0, ch = 0, id = 1;
    for (s = 0; s < UAS_N; s++) {
        if (ua_dst[s] >= UAD_N)
            ua_dst[s] = DEF[s];
        id &= ua_dst[s] == DEF[s];
        if (s >= UAS_DR1) {
            uint32_t l = s - UAS_DR1;
            dlt_chm[l] = UAD_MASK[ua_dst[s]];
            m |= (uint32_t)(dlt_chm[l] != 0u) << l;
            ch |= dlt_chm[l];
        }
    }
    ua_dbus_on = (uint8_t)(ua_dst[UAS_DBUS] != UAD_NONE);
    ua_lane_ch = (uint8_t)ch;
    dlt_mask = (uint16_t)m;
    ua_ident = (uint8_t)id;
}
static void ua_route_default(void)
{
    uint32_t s;
    for (s = 0; s < UAS_N; s++)
        ua_dst[s] = UAD_NONE;
    ua_dst[UAS_T1] = UAD_1, ua_dst[UAS_T2] = UAD_2, ua_dst[UAS_T3] = UAD_3, ua_dst[UAS_DRUMS] = UAD_4;
    ua_route_apply();
}
