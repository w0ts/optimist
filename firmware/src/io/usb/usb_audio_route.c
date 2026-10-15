/* SPDX-License-Identifier: GPL-3.0-only */
/* The four capture channels built from their sources (ua_tap.h: the list, the tap points, the defaults). In the audio
 * interrupt, once a mix block, before the USB service is held off (audio.c ua_block): nothing is added to the USB poll
 * or its interrupt. The default routing (T1 T2 T3 DRUMS) is no work at all: the stems go out as they always did (the
 * emulator's USB audio test taps them at ua_audio's entry). Included by usb.c after usb_audio_stream.c.
 * tests/usb_audio_route_test.c. */
_Static_assert(UA_ROUTE_N == UA_CAP_CHANNELS, "one source a capture channel");
static int32_t ua_routed[CTL * UA_CAP_CHANNELS];

/* the names (SYSTEM > USB AUDIO's lines; at most 13 characters: a value of the settings list) */
static const char *ua_src_name(uint32_t v, char *b)
{
    static const char *const N[UAS_DR1] = {"OFF", "MAIN L", "MAIN R", "MAIN L+R -3dB", "MAIN L+R -6dB", "T1", "T2", "T3",
                                           "DRUMS", "DRUM BUS L", "DRUM BUS R", "DRUM L+R -3dB", "DRUM L+R -6dB"};
    uint32_t l;
    if (v < UAS_DR1)
        return N[v];
    l = v - UAS_DR1 + 1u;                               /* DR1..DR16 */
    b[0] = 'D', b[1] = 'R';
    if (l >= 10u) {
        b[2] = '1', b[3] = (char)('0' + l - 10u), b[4] = 0;
    } else {
        b[2] = (char)('0' + l), b[3] = 0;
    }
    return b;
}

/* the mono sums: (l + r) x 23170 / 32768, rounded (-3.0103 dB), or / 2 (-6.0206 dB). The sum is held to +-65536 first: past it
 * the channel clips at Q15 either way (65536 x 0.7071 > 32767), and the product stays in 32 bits */
AINL int32_t ua_sum3(int32_t l, int32_t r) { return (clamp(l + r, -65536, 65535) * 23170 + 16384) >> 15; }
AINL int32_t ua_sum6(int32_t l, int32_t r) { return (l + r) >> 1; }

/* one channel's n samples into o (stride os): a, b the source's sides with their strides; how: 0 silence, 1 a, 3 / 6
 * the sum at -3 / -6 dB */
static void ua_col(int32_t *o, uint32_t os, const int32_t *a, uint32_t sa, const int32_t *b, uint32_t sb, uint32_t how,
                   uint32_t n)
{
    uint32_t i;
    if (how == 1u)                                      /* (one loop a kind: no test a sample) */
        for (i = 0; i < n; i++)
            o[i * os] = a[i * sa];
    else if (how == 3u)
        for (i = 0; i < n; i++)
            o[i * os] = ua_sum3(a[i * sa], b[i * sb]);
    else if (how == 6u)
        for (i = 0; i < n; i++)
            o[i * os] = ua_sum6(a[i * sa], b[i * sb]);
    else
        for (i = 0; i < n; i++)
            o[i * os] = 0;
}

/* THE SOURCE MAPPER, output-agnostic (the device-mode capture below is one consumer; a USB host-mode output to a 4-output
 * sound card can be another): channel c (0..UA_ROUTE_N-1) of this mix block, n samples (Q15 scale, int32, not clipped)
 * into o with stride os. master = the master output of the block (stereo interleaved, Q15, before the codec shift); the
 * stems in track_capture, the taps in dlt_buf / ua_dbus (live only while dlt_block was told the channels are taken).
 * In the audio interrupt, after mix_block */
static void ua_route_ch(uint32_t c, int32_t *o, uint32_t os, const int32_t *master, uint32_t n)
{
    uint32_t v = ua_src[c % UA_ROUTE_N], how = 1u, sa = 1u, sb = 1u;
    const int32_t *a = 0, *b = 0;
    if (v >= UAS_MAIN_L && v <= UAS_MAIN_S6) {
        a = master, b = master + 1, sa = sb = 2u;
        if (v == UAS_MAIN_R)
            a = master + 1;
        how = v == UAS_MAIN_S3 ? 3u : v == UAS_MAIN_S6 ? 6u : 1u;
    } else if (v >= UAS_T1 && v <= UAS_DRUMS) {
        a = track_capture + (v - UAS_T1), sa = NTRK;
    } else if (v >= UAS_DBUS_L && v <= UAS_DBUS_S6) {
        a = ua_dbus[v == UAS_DBUS_R], b = ua_dbus[1];
        how = v == UAS_DBUS_S3 ? 3u : v == UAS_DBUS_S6 ? 6u : 1u;
    } else if (v >= UAS_DR1 && v < UAS_N) {
        a = dlt_buf[dlt_slot[v - UAS_DR1] & (UA_ROUTE_N - 1u)];
    } else {
        how = 0;                                        /* OFF */
    }
    ua_col(o, os, a, sa, b, sb, how, n);
}
/* the four channels into four mono buffers (a consumer that wants them apart) */
static void ua_route_block(int32_t *const ch[UA_ROUTE_N], const int32_t *master, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < UA_ROUTE_N; c++)
        ua_route_ch(c, ch[c], 1u, master, n);
}

/* the consumer here: the device-mode capture's frames (interleaved, UA_CAP_CHANNELS a frame, as ua_audio takes them);
 * the default routing: the stems' own buffer, no work */
static __attribute__((noinline)) const int32_t *ua_route(const int32_t *master, uint32_t n)
{
    uint32_t c;
    if (ua_ident)
        return track_capture;
    for (c = 0; c < UA_CAP_CHANNELS; c++)
        ua_route_ch(c, ua_routed + c, UA_CAP_CHANNELS, master, n);
    return ua_routed;
}

/* the setting: channel c's source stepped s (> 0 right; 0: YES, round), then applied */
static void ua_src_step(uint32_t c, int32_t s)
{
    int32_t v = ua_src[c % UA_ROUTE_N];
    v = s ? clamp(v + (s > 0 ? 1 : -1), 0, UAS_N - 1) : (v >= UAS_N - 1 ? 0 : v + 1);
    ua_src[c % UA_ROUTE_N] = (uint8_t)v;
    ua_route_apply();
}
