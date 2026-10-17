/* SPDX-License-Identifier: GPL-3.0-only */
/* The four capture channels built from the sources routed to them (ua_tap.h: the list, the tap points, the defaults). In
 * the audio interrupt, once a mix block, before the USB service is held off (audio.c ua_block): nothing is added to the
 * USB poll or its interrupt. The default routing (T1 T2 T3 DRUMS on 1 2 3 4) is no work at all: the stems go out as they
 * always did (the emulator's USB audio test taps them at ua_audio's entry). Included by usb.c after usb_audio_stream.c.
 * tests/usb_audio_route_test.c. */
_Static_assert(UA_ROUTE_N == UA_CAP_CHANNELS, "a capture channel a bit of a destination");
static int32_t ua_routed[CTL * UA_CAP_CHANNELS];

/* the names (SYSTEM > USB AUDIO's lines: the source the name, its channels the value; at most 13 characters) */
static const char *ua_src_name(uint32_t v, char *b)
{
    static const char *const N[UAS_DR1] = {"MASTER", "T1", "T2", "T3", "DRUMS", "DRUM BUS"};
    uint32_t l;
    if (v < UAS_DR1)
        return N[v];
    l = v - UAS_DR1 + 1u;                               /* DR1..DR10 */
    b[0] = 'D', b[1] = 'R';
    if (l >= 10u) {
        b[2] = '1', b[3] = (char)('0' + l - 10u), b[4] = 0;
    } else {
        b[2] = (char)('0' + l), b[3] = 0;
    }
    return b;
}
static const char *ua_dst_name(uint32_t d)
{
    static const char *const N[UAD_N] = {"NONE", "1", "2", "1+2", "3", "4", "3+4"};
    return N[d % UAD_N];
}

/* the mono sum: (l + r) / 2, each side x 0.5 (-6.02 dB) exactly, >> 1 (a floor: toward minus infinity). Two Q15
 * sides give a Q15 sum: it never clips */
AINL int32_t ua_sum(int32_t l, int32_t r) { return (l + r) >> 1; }

/* n samples added into o (stride os): a, b a source's sides with their strides; how: 1 a, 2 the sum (L + R) / 2 */
static void ua_add(int32_t *o, uint32_t os, const int32_t *a, uint32_t sa, const int32_t *b, uint32_t sb, uint32_t how,
                   uint32_t n)
{
    uint32_t i;
    if (how == 1u)                                      /* (one loop a kind: no test a sample) */
        for (i = 0; i < n; i++)
            o[i * os] += a[i * sa];
    else
        for (i = 0; i < n; i++)
            o[i * os] += ua_sum(a[i * sa], b[i * sb]);
}

/* THE SOURCE MAPPER, output-agnostic (the device-mode capture below is one consumer; a USB host-mode output to a 4-output
 * sound card can be another): channel c (0..UA_ROUTE_N-1) of this mix block, n samples (Q15 scale, int32, not clipped)
 * into o with stride os: the lanes routed to it (dlt_buf, summed as they rendered), then every other source on it. master
 * = the master output of the block (stereo interleaved, Q15, before the codec shift); the stems in track_capture, the
 * drum bus in ua_dbus (live only while dlt_block was told the channels are taken). In the audio interrupt, after mix_block */
static void ua_route_ch(uint32_t c, int32_t *o, uint32_t os, const int32_t *master, uint32_t n)
{
    uint32_t s, i, bit = 1u << c;
    if (dlt_live && (ua_lane_ch & bit))
        for (i = 0; i < n; i++)
            o[i * os] = dlt_buf[c % UA_ROUTE_N][i];
    else
        for (i = 0; i < n; i++)
            o[i * os] = 0;
    for (s = 0; s < UAS_DR1; s++) {
        uint32_t m = UAD_MASK[ua_dst[s] % UAD_N], sa = 1u, sb = 1u, how = 1u;
        const int32_t *a, *b = 0;
        if (!(m & bit))
            continue;
        if (s == UAS_MAIN || s == UAS_DBUS) {           /* stereo: L / R on a pair, else the sum */
            a = s == UAS_MAIN ? master : ua_dbus[0];
            b = s == UAS_MAIN ? master + 1 : ua_dbus[1];
            sa = sb = s == UAS_MAIN ? 2u : 1u;
            if (s == UAS_DBUS && !ua_dbus_live)
                continue;                               /* (the bus not taken this block: nothing in it) */
            if (m == 3u || m == 12u) {
                if (bit & 10u)                          /* (the pair's second channel: the right side) */
                    a = b;
            } else {
                how = 2u;
            }
        } else {
            a = track_capture + (s - UAS_T1), sa = NTRK;   /* (a stem: T1 T2 T3 DRUMS, mono) */
        }
        ua_add(o, os, a, sa, b, sb, how, n);
    }
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

/* the setting: source s's channels stepped st (> 0 right; 0: YES, round), then applied */
static void ua_dst_step(uint32_t s, int32_t st)
{
    int32_t v = ua_dst[s % UAS_N];
    v = st ? clamp(v + (st > 0 ? 1 : -1), 0, UAD_N - 1) : (v >= UAD_N - 1 ? 0 : v + 1);
    ua_dst[s % UAS_N] = (uint8_t)v;
    ua_route_apply();
}
