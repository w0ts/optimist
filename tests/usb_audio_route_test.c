/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > USB AUDIO (io/usb/ua_tap.h, usb_audio_route.c): where each source goes among the four capture channels, through
 * the real mixer. A source (MASTER the master output, T1..T3 and DRUMS the stems, DRUM BUS the drum bus, DRn a lane's share
 * of DRUMS) goes to NONE, 1, 2, 1+2, 3, 4 or 3+4: a stereo source on a pair L then R, on one channel the mono sum (L + R) / 2
 * (a floor); a mono source on a pair on both; sources on one channel add up; the default routing (T1 T2 T3 DRUMS on 1 2 3 4)
 * is no routing (the stems' own buffer); a tapped lane leaves the mix as it was. The settings record: favorites_test.c. */
#include <assert.h>
#include <math.h>
#include <sys/mman.h>
#define FELUCCA_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main

/* every source NONE, then the pairs given (source, destination), then applied */
static void set_route(uint32_t n, const uint8_t (*sd)[2])
{
    uint32_t i;
    for (i = 0; i < UAS_N; i++)
        ua_dst[i] = UAD_NONE;
    for (i = 0; i < n; i++)
        ua_dst[sd[i][0]] = sd[i][1];
    ua_route_apply();
}
#define ROUTE(...) do { const uint8_t r_[][2] = {__VA_ARGS__}; set_route(sizeof r_ / sizeof r_[0], r_); } while (0)

static void setup(void)
{
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], 0, 5);
    memset(drums.v, 0, sizeof drums.v);
    drums.tail = 0;
}

/* the sum: (L + R) / 2 exactly (a floor), each side x 0.5; two Q15 sides never leave Q15 */
static void test_sums(void)
{
    for (int32_t l = -32768; l <= 32767; l += 97)
        for (int32_t r = -32768; r <= 32767; r += 89) {
            int32_t y = ua_sum(l, r);
            assert(y == (int32_t)floor((l + r) / 2.0) && y >= -32768 && y <= 32767);
        }
    assert(ua_sum(-32768, -32768) == -32768 && ua_sum(32767, 32767) == 32767);   /* (the ends: no clipping) */
    assert(ua_sum(-3, 0) == -2 && ua_sum(3, 0) == 1);
    for (int32_t x = 4000; x <= 16000; x += 4000)        /* alike on both sides: its level kept */
        assert(ua_sum(x, x) == x);
}

/* one block mixed, then routed */
static const int32_t *block(int32_t *out)
{
    mix_block(out, CTL);
    return ua_route(out, CTL);
}

static void test_sources(void)
{
    int32_t out[CTL * 2];
    uint64_t e[4] = {0};
    setup();
    ua_route_default();
    assert(ua_ident && !dlt_mask && !ua_dbus_on);
    for (uint32_t k = 0; k < NPART; k++) trk_note_on(&trk[k], 48 + 7 * k, 100);
    drum_on(36, 100);
    for (uint32_t b = 0; b < 64; b++)
        assert(block(out) == track_capture);              /* the default: the stems as they are, no copy */
    ROUTE({UAS_MAIN, UAD_12}, {UAS_T1, UAD_3}, {UAS_T2, UAD_3});   /* MASTER L R on 1 2; T1 + T2 on 3; nothing on 4 */
    assert(!ua_ident);
    for (uint32_t b = 0; b < 64; b++) {
        const int32_t *f = block(out);
        assert(f != track_capture);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS, *s = track_capture + i * NTRK, l = out[2 * i], r = out[2 * i + 1];
            assert(x[0] == l && x[1] == r && x[2] == s[0] + s[1] && x[3] == 0);
            e[0] += (int64_t)l * l, e[1] += (int64_t)s[1] * s[1];
        }
    }
    assert(e[0] && e[1]);
    {   /* the mapper on its own (another consumer: four mono buffers): the same samples as the capture's frames */
        int32_t m[UA_ROUTE_N][CTL], *const ch[UA_ROUTE_N] = {m[0], m[1], m[2], m[3]};
        const int32_t *f = block(out);
        ua_route_block(ch, out, CTL);
        for (uint32_t i = 0; i < CTL; i++)
            for (uint32_t c = 0; c < UA_ROUTE_N; c++)
                assert(m[c][i] == f[i * UA_CAP_CHANNELS + c]);
    }
    ROUTE({UAS_T3, UAD_12}, {UAS_MAIN, UAD_3}, {UAS_DRUMS, UAD_4});   /* a mono stem on a pair: on both; the master on one: its sum */
    for (uint32_t b = 0; b < 64; b++) {
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS, *s = track_capture + i * NTRK;
            assert(x[0] == s[2] && x[1] == s[2] && x[2] == (out[2 * i] + out[2 * i + 1]) >> 1 && x[3] == s[TRK_DRUM]);
            e[2] += (int64_t)s[2] * s[2];
        }
    }
    assert(e[2]);
    ROUTE({UAS_MAIN, UAD_34}, {UAS_DRUMS, UAD_2});        /* the second pair; 1 silent */
    for (uint32_t b = 0; b < 16; b++) {
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS;
            assert(x[0] == 0 && x[1] == track_capture[i * NTRK + TRK_DRUM] && x[2] == out[2 * i] && x[3] == out[2 * i + 1]);
        }
    }
}

/* DRn: the lane's share of DRUMS (the stem), the mix untouched (an X0X kit: its tapped lane leaves the shared sum, an
 * LSB of rounding at most); two lanes on one channel add up; DRUM BUS: the bus, panned */
static void test_lanes(uint32_t kit, int x0x, int ins)
{
    int32_t (*refs)[CTL * 2] = mmap(0, 256u * sizeof *refs, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    int32_t out[CTL * 2];
    uint32_t la = lane_of_note(36), lb = lane_of_note(38), lc = 0, blocks = 0;
    uint64_t ea = 0, eb = 0, eo = 0;
    (void)x0x;
    assert(la != lb && la < DRUM_LANES && lb < DRUM_LANES);
    while (lc == la || lc == lb)
        lc++;
    assert(refs != MAP_FAILED);
    for (int pass = 0; pass < 3; pass++) {                /* pass 0: no tap, the reference mix; 1: a lane a channel; 2: */
        int status;                                       /* both lanes on 1+2 (each from the same state: a child each) */
        pid_t child = fork();
        assert(child >= 0);
        if (child) {
            assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
            continue;
        }
        uint32_t ran = 0;
        setup();
        TDRUM->p[P_E0] = (int16_t)kit;
#if DINS
        dins_amt[0][la] = (uint8_t)(ins ? 100 : 0);       /* lane a's S1 (DIST): its voices through dins_post */
#endif
        if (pass == 1)
            ROUTE({UAS_DR1 + la, UAD_1}, {UAS_DR1 + lb, UAD_2}, {UAS_DR1 + lc, UAD_3}, {UAS_DRUMS, UAD_4});
        else if (pass == 2)
            ROUTE({UAS_DR1 + la, UAD_12}, {UAS_DR1 + lb, UAD_12}, {UAS_DRUMS, UAD_34});
        else
            ua_route_default();
        assert(pass ? (dlt_mask >> la & 1u) && (dlt_mask >> lb & 1u) : !dlt_mask);
        for (uint32_t b = 0; b < 256; b++) {
            int32_t tail0;
            if (b % 64u == 0) drum_on(36, 100);
            if (b % 64u == 9) drum_on(38, 110);
            tail0 = drums.tail;
            const int32_t *f = block(out);
            if (!pass) {
                memcpy(refs[b], out, sizeof out);
                continue;
            }
            for (uint32_t i = 0; i < CTL * 2; i++)        /* the mix as without the taps */
                assert(refs[b][i] == out[i]);
#if DINS
            for (uint32_t k = 0; k < NDRUM; k++)
                ran |= dins_ran[k];
#endif
            if (tail0 || drums.tail)
                continue;                                 /* (the declick tail: in DRUMS, no lane's) */
            blocks++;
            for (uint32_t i = 0; i < CTL; i++) {
                const int32_t *x = f + i * UA_CAP_CHANNELS, d = track_capture[i * NTRK + TRK_DRUM];
                if (pass == 1) {
                    assert(x[0] + x[1] == d);             /* (only the two lanes sound) */
                    assert(x[2] == 0);                    /* a lane with nothing: silence */
                    assert(x[3] == d);
                    ea += (int64_t)x[0] * x[0], eb += (int64_t)x[1] * x[1];
                } else {
                    assert(x[0] == d && x[1] == d && x[2] == d && x[3] == d);   /* (the two lanes summed: the stem) */
                    ea += (int64_t)x[0] * x[0], eb += (int64_t)x[0] * x[0];
                }
                eo += (int64_t)out[2 * i] * out[2 * i];
            }
        }
        assert(!pass || (blocks > 100 && ea && eb && eo));
        assert(!pass || !ins || ran);                     /* (the inserts did run) */
        _exit(0);
    }
    munmap(refs, 256u * sizeof *refs);
    /* the drum bus in stereo on 1+2: the drum track hard left, nothing on the right; the master's sum on 3 */
    setup();
    TDRUM->p[P_E0] = (int16_t)kit;
    trk[TRK_DRUM].p[P_PAN] = -64;
    ROUTE({UAS_DBUS, UAD_12}, {UAS_MAIN, UAD_3});
    assert(ua_dbus_on && !ua_ident);
    ea = eb = 0;
    for (uint32_t b = 0; b < 128; b++) {
        if (b % 64u == 0) drum_on(36, 100);
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS;
            assert(x[0] == ua_dbus[0][i] && x[1] == ua_dbus[1][i]);
            assert(x[2] == (out[2 * i] + out[2 * i + 1]) >> 1 && x[3] == 0);
            ea += (int64_t)x[0] * x[0], eb += (int64_t)x[1] * x[1];
        }
    }
    assert(ea && eb * 1000u < ea);                        /* (hard left: the right side at least 30 dB down) */
    ROUTE({UAS_DBUS, UAD_4});                             /* on one channel: its sum */
    for (uint32_t b = 0; b < 16; b++) {
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++)
            assert(f[i * UA_CAP_CHANNELS + 3] == (ua_dbus[0][i] + ua_dbus[1][i]) >> 1 && f[i * UA_CAP_CHANNELS] == 0);
    }
}

/* the choice and its names */
static void test_setting(void)
{
    ua_route_default();
    assert(ua_ident && ua_dst[UAS_T1] == UAD_1 && ua_dst[UAS_T2] == UAD_2 && ua_dst[UAS_T3] == UAD_3 &&
           ua_dst[UAS_DRUMS] == UAD_4 && ua_dst[UAS_MAIN] == UAD_NONE && ua_dst[UAS_N - 1u] == UAD_NONE);
    ua_dst_step(UAS_T1, -1);
    assert(ua_dst[UAS_T1] == UAD_NONE && !ua_ident);
    ua_dst_step(UAS_T1, -1);
    assert(ua_dst[UAS_T1] == UAD_NONE);                    /* a knob stops at the ends */
    ua_dst_step(UAS_T1, 1);
    assert(ua_dst[UAS_T1] == UAD_1 && ua_ident);
    ua_dst[UAS_MAIN] = UAD_34;
    ua_dst_step(UAS_MAIN, 1);
    assert(ua_dst[UAS_MAIN] == UAD_34);
    ua_dst_step(UAS_MAIN, 0);
    assert(ua_dst[UAS_MAIN] == UAD_NONE && ua_ident);      /* YES goes round */
    ua_dst[UAS_T3] = 200;
    ua_route_apply();
    assert(ua_dst[UAS_T3] == UAD_3);                       /* out of range: the default */
    assert(!strcmp(ua_src_name(UAS_DR1), "DR1") && !strcmp(ua_src_name(UAS_N - 1), "DR10"));
    for (uint32_t v = 0; v < UAS_N; v++)
        assert(strlen(ua_src_name(v)) <= 13u);
    assert(!strcmp(ua_src_name(UAS_MAIN), "MASTER") && !strcmp(ua_src_name(UAS_DBUS), "DRUM BUS"));
    assert(!strcmp(ua_dst_name(UAD_NONE), "NONE") && !strcmp(ua_dst_name(UAD_12), "1+2") && !strcmp(ua_dst_name(UAD_34), "3+4"));
    assert(UAS_N == 6u + DRUM_LANES);                      /* (MASTER, T1..T3, DRUMS, DRUM BUS, the lanes) */
    /* two lanes on one channel, one lane on a pair: their channels */
    ua_route_default();
    ua_dst[UAS_DR1 + 4] = UAD_1, ua_dst[UAS_DR1 + 9] = UAD_1, ua_dst[UAS_DR1 + 2] = UAD_34;
    ua_route_apply();
    assert(dlt_mask == (1u << 2 | 1u << 4 | 1u << 9) && dlt_chm[4] == 1u && dlt_chm[9] == 1u && dlt_chm[2] == 12u &&
           ua_lane_ch == 13u);
    ua_route_default();
    assert(ua_ident && !dlt_mask);
}

int main(void)
{
    test_sums();
    test_setting();
    test_sources();
    test_lanes(0, 0, 0);                                  /* the sampled kit */
    test_lanes(DRUM_SAMPLED, 0, 0);                       /* a synthesised one */
#if DINS
    test_lanes(0, 0, 1);                                  /* a lane's insert: tapped after it */
    test_lanes(DRUM_SAMPLED, 0, 1);
#endif
#if DRUM_X0X
    assert(drum_kit_built(DRUM_UID_X909));
    test_lanes(DRUM_UID_X909, 1, 0);
    puts("(with the X0X 909)");
#endif
    puts("USB audio routing: each source (MASTER, T1..T3, DRUMS, DRUM BUS, DR1..DR10) to NONE 1 2 1+2 3 4 3+4, sums, defaults: OK");
    return 0;
}
