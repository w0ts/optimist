/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > USB AUDIO (io/usb/ua_tap.h, usb_audio_route.c): each capture channel's source through the real mixer.
 * Each source picks its samples (MAIN L / R the master output, T1..T3 and DRUMS the stems, DRUM BUS the drum bus,
 * DRn a lane's share of DRUMS), the mono sums at -3.0103 / -6.0206 dB (within 0.001 dB on a constant), OFF is silence,
 * the default routing is no routing (the stems' own buffer), a tapped lane leaves the mix as it was, the settings
 * record keeps the choice. */
#include <assert.h>
#include <math.h>
#include <sys/mman.h>
#define FELUCCA_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static void set_route(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    ua_src[0] = a, ua_src[1] = b, ua_src[2] = c, ua_src[3] = d;
    ua_route_apply();
}

static void setup(void)
{
    host_tracks_init();
    for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], 0, 5);
    memset(drums.v, 0, sizeof drums.v);
    drums.tail = 0;
}

/* the sums: exact factors on a constant */
static void test_sums(void)
{
    assert(fabs(20.0 * log10(23170.0 / 32768.0) + 3.0103) < 0.001);   /* the factors themselves */
    for (int32_t x = -16000; x <= 16000; x += 7) {        /* each output: its factor applied */
        assert(fabs(ua_sum3(x, x + 3) - (2.0 * x + 3) * (23170.0 / 32768.0)) <= 0.5);   /* rounded: within half an LSB */
        assert(ua_sum6(x, x + 3) == (2 * x + 3) >> 1);
    }
    for (int32_t x = 4000; x <= 16000; x += 4000) {       /* on a constant: the gain in dB */
        assert(fabs(20.0 * log10(ua_sum3(x, x) / (2.0 * x)) + 3.0103) < 0.001);
        assert(fabs(20.0 * log10(ua_sum6(x, x) / (2.0 * x)) + 6.0206) < 0.001);
    }
    assert(ua_sum3(40000, 40000) == (65535 * 23170 + 16384) >> 15);   /* (held: it clips at Q15 anyway) */
    assert(ua_sum6(-3, 0) == -2 && ua_sum6(3, 0) == 1);
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
    uint64_t e[UA_CAP_CHANNELS] = {0};
    setup();
    set_route(UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS);
    assert(ua_ident && !dlt_mask && !ua_dbus_on);
    for (uint32_t k = 0; k < NPART; k++) trk_note_on(&trk[k], 48 + 7 * k, 100);
    drum_on(36, 100);
    for (uint32_t b = 0; b < 64; b++)
        assert(block(out) == track_capture);              /* the default: the stems as they are, no copy */
    set_route(UAS_MAIN_L, UAS_MAIN_R, UAS_MAIN_S3, UAS_MAIN_S6);
    for (uint32_t b = 0; b < 64; b++) {
        const int32_t *f = block(out);
        assert(f != track_capture);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS, l = out[2 * i], r = out[2 * i + 1];
            assert(x[0] == l && x[1] == r && x[2] == ua_sum3(l, r) && x[3] == (l + r) >> 1);
            e[0] += (int64_t)l * l;
        }
    }
    assert(e[0]);
    {   /* the mapper on its own (another consumer: four mono buffers): the same samples as the capture's frames */
        int32_t m[UA_ROUTE_N][CTL], *const ch[UA_ROUTE_N] = {m[0], m[1], m[2], m[3]};
        const int32_t *f = block(out);
        ua_route_block(ch, out, CTL);
        for (uint32_t i = 0; i < CTL; i++)
            for (uint32_t c = 0; c < UA_ROUTE_N; c++)
                assert(m[c][i] == f[i * UA_CAP_CHANNELS + c]);
    }
    set_route(UAS_DRUMS, UAS_T3, UAS_OFF, UAS_T1);        /* any stem on any channel; OFF silent */
    for (uint32_t b = 0; b < 64; b++) {
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS, *s = track_capture + i * NTRK;
            assert(x[0] == s[TRK_DRUM] && x[1] == s[2] && x[2] == 0 && x[3] == s[0]);
            e[1] += (int64_t)s[2] * s[2];
            e[2] += (int64_t)s[0] * s[0];
        }
    }
    assert(e[1] && e[2]);
}

/* DRn: the lane's share of DRUMS (the stem), the mix untouched (an X0X kit: its tapped lane leaves the shared sum, an
 * LSB of rounding at most); DRUM BUS: the bus, panned */
static void test_lanes(uint32_t kit, int x0x, int ins)
{
    int32_t (*refs)[CTL * 2] = mmap(0, 256u * sizeof *refs, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    int32_t out[CTL * 2];
    uint32_t la = dlm_lane(36), lb = dlm_lane(38), lc = 0, blocks = 0;
    uint64_t ea = 0, eb = 0, eo = 0;
    assert(la != lb && la < DRUM_LANES && lb < DRUM_LANES);
    while (lc == la || lc == lb)
        lc++;
    assert(refs != MAP_FAILED);
    for (int pass = 0; pass < 2; pass++) {                /* pass 0: no tap, the reference mix; 1: tapped (each from */
        int status;                                       /* the same state: a child each) */
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
        if (pass)
            set_route(UAS_DR1 + la, UAS_DR1 + lb, UAS_DR1 + lc, UAS_DRUMS);
        else
            set_route(UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS);
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
                const int32_t *x = f + i * UA_CAP_CHANNELS;
                assert(x[0] + x[1] == track_capture[i * NTRK + TRK_DRUM]);   /* (only the two lanes sound) */
                assert(x[2] == 0);                         /* a lane with nothing: silence */
                assert(x[3] == track_capture[i * NTRK + TRK_DRUM]);
                ea += (int64_t)x[0] * x[0], eb += (int64_t)x[1] * x[1];
                eo += (int64_t)out[2 * i] * out[2 * i];
            }
        }
        assert(!pass || (blocks > 100 && ea && eb && eo));
        assert(!pass || !ins || ran);                     /* (the inserts did run) */
        _exit(0);
    }
    munmap(refs, 256u * sizeof *refs);
    /* the drum bus in stereo: the drum track hard left, nothing on the right */
    setup();
    TDRUM->p[P_E0] = (int16_t)kit;
    trk[TRK_DRUM].p[P_PAN] = -64;
    set_route(UAS_DBUS_L, UAS_DBUS_R, UAS_DBUS_S6, UAS_DBUS_S3);
    assert(ua_dbus_on && !ua_ident);
    ea = eb = 0;
    for (uint32_t b = 0; b < 128; b++) {
        if (b % 64u == 0) drum_on(36, 100);
        const int32_t *f = block(out);
        for (uint32_t i = 0; i < CTL; i++) {
            const int32_t *x = f + i * UA_CAP_CHANNELS;
            assert(x[0] == ua_dbus[0][i] && x[1] == ua_dbus[1][i]);
            assert(x[2] == (x[0] + x[1]) >> 1 && x[3] == ua_sum3(x[0], x[1]));
            ea += (int64_t)x[0] * x[0], eb += (int64_t)x[1] * x[1];
        }
    }
    assert(ea && eb * 1000u < ea);                        /* (hard left: the right side at least 30 dB down) */
}

/* the choice and its names */
static void test_setting(void)
{
    char b[6];
    set_route(UAS_T1, UAS_T2, UAS_T3, UAS_DRUMS);
    ua_src_step(0, -1);
    assert(ua_src[0] == UAS_MAIN_S6 && !ua_ident);
    ua_src_step(0, 1);
    assert(ua_src[0] == UAS_T1 && ua_ident);
    ua_src[3] = UAS_N - 1;
    ua_src_step(3, 1);
    assert(ua_src[3] == UAS_N - 1);                       /* a knob stops at the ends */
    ua_src_step(3, 0);
    assert(ua_src[3] == UAS_OFF);                          /* YES goes round */
    ua_src[2] = 200;
    ua_route_apply();
    assert(ua_src[2] == UAS_T3);                           /* out of range: the default */
    assert(!strcmp(ua_src_name(UAS_DR1, b), "DR1") && !strcmp(ua_src_name(UAS_N - 1, b), "DR16"));
    for (uint32_t v = 0; v < UAS_N; v++)
        assert(strlen(ua_src_name(v, b)) <= 13u);
    assert(!strcmp(ua_src_name(UAS_MAIN_S3, b), "MAIN L+R -3dB"));
    /* two channels on one lane: one tap */
    set_route(UAS_DR1 + 4, UAS_DR1 + 4, UAS_OFF, UAS_DR1 + 15);
    assert(dlt_mask == (1u << 4 | 1u << 15) && dlt_slot[4] == 0 && dlt_slot[15] == 1);
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
    puts("USB audio routing: MAIN L/R/sums, T1..T3, DRUMS, DRUM BUS, DR1..DR16 lane taps, OFF, defaults: OK");
    return 0;
}
