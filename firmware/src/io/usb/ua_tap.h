/* SPDX-License-Identifier: GPL-3.0-only */
/* USB audio capture routing (FELUCCA_USB_AUDIO): what each of the four capture channels carries (SYSTEM > USB AUDIO,
 * one line a channel, CH1..CH4) and the taps in the mix that only a channel's choice turns on. Included by core.h
 * (the drums, fx.c and dual.c write the taps); usb_audio_route.c builds the channels from them (audio.c ua_block).
 *
 * The sources, and where each is taken:
 *   OFF          silence.
 *   MAIN L / R   the master output as the FM-1's own output gets it: after the master inserts, the volume, the limiter
 *                (or LIMIT's brickwall) and the knee, before the codec's -6 dB shift (Q15 full scale = 0 dBFS here).
 *   MAIN L+R -3dB / -6dB   the master's two sides summed to mono, at 0.70709 (23170 / 32768: -3.0103 dB, within
 *                0.0002 dB) or at 0.5 exactly (-6.0206 dB).
 *   T1 T2 T3     a synth track's stem, as before: after its inserts, its LEVEL and its mute, before its pan, its sends,
 *                the FX buses and the master (POST-fader: what the track contributes to the mix, without the space).
 *   DRUMS        the drum track's stem, as before: its lanes summed (mono), each after its lane level, cut and inserts and
 *                the drum track's level and mute, before the lanes' and the track's pan and the drum bus inserts.
 *   DRUM BUS L / R, DRUM L+R -3dB / -6dB   the drum bus in stereo: after its pans and its two insert slots (dbus_run),
 *                before its sends and the master.
 *   DR1..DR16    one drum lane's own share of DRUMS (the same point: post lane level / cut / inserts and the track's
 *                level and mute, pre pan). Only the lanes a channel picks are tapped, only while the host records: a
 *                voice of another lane costs one mask test a block (drums.c, drum_x0x.c, fx.c dins_post). The declick
 *                tail of a stopped voice (drums.tail) belongs to no lane: it is in DRUMS only.
 * PRE-fader stems are not offered: a stem is the track's contribution at its mixer level (a muted track records silence),
 * as the stems always were. Defaults = the routing before this setting: T1, T2, T3, DRUMS. */
enum { UAS_OFF, UAS_MAIN_L, UAS_MAIN_R, UAS_MAIN_S3, UAS_MAIN_S6, UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS,
       UAS_DBUS_L, UAS_DBUS_R, UAS_DBUS_S3, UAS_DBUS_S6, UAS_DR1, UAS_N = UAS_DR1 + DRUM_LANES };
#define UA_ROUTE_N 4u                           /* the capture channels (usb_audio_stream.c UA_CAP_CHANNELS) */
static uint8_t ua_src[UA_ROUTE_N] = {UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS};   /* each channel's source (the setting) */
static uint8_t ua_ident = 1;                    /* ua_src is the default: the stems go out as they are (no routing) */
static uint8_t ua_dbus_on;                      /* a channel takes the drum bus */
static uint16_t dlt_mask;                       /* the lanes a channel takes (main loop: ua_route_apply) */
static uint8_t dlt_slot[DRUM_LANES];            /* each of those lanes' tap buffer */
/* this block's (fx.c capture_clear, at the block's start): the taps above while the host takes the capture, else 0 */
static uint16_t dlt_live;
static uint8_t ua_dbus_live;
static int32_t dlt_buf[UA_ROUTE_N][CTL];        /* the tapped lanes, this block */
static int32_t ua_dbus[2][CTL];                 /* the drum bus, this block (dbus_run) */

/* lane ln's samples i0..i1 of b (its voice's, drum_sends.c dsend_buf) added to its tap, when a channel takes it */
AINL void dlt_add(uint32_t ln, const int32_t *b, uint32_t i0, uint32_t i1)
{
    if (ln < DRUM_LANES && ((dlt_live >> ln) & 1u)) {
        int32_t *t = dlt_buf[dlt_slot[ln] & (UA_ROUTE_N - 1u)];
        uint32_t i;
        for (i = i0; i < i1; i++)
            t[i] += b[i];
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
/* ua_src set (the setting changed, or loaded): the taps it needs (main loop; the audio interrupt reads them) */
static void ua_route_apply(void)
{
    uint32_t c, m = 0, s = 0, bus = 0, id = 1;
    static const uint8_t DEF[UA_ROUTE_N] = {UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS};
    for (c = 0; c < UA_ROUTE_N; c++) {
        uint32_t v = ua_src[c];
        if (v >= UAS_N)
            ua_src[c] = (uint8_t)(v = DEF[c]);
        id &= v == DEF[c];
        bus |= v >= UAS_DBUS_L && v <= UAS_DBUS_S6;
        if (v >= UAS_DR1 && !((m >> (v - UAS_DR1)) & 1u)) {
            dlt_slot[v - UAS_DR1] = (uint8_t)s++;
            m |= 1u << (v - UAS_DR1);
        }
    }
    ua_dbus_on = (uint8_t)bus;
    dlt_mask = (uint16_t)m;
    ua_ident = (uint8_t)id;
}
static void ua_route_default(void)
{
    ua_src[0] = UAS_T1, ua_src[1] = UAS_T2, ua_src[2] = UAS_T3, ua_src[3] = UAS_DRUMS;
    ua_route_apply();
}
