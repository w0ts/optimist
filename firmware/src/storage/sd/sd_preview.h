/* SPDX-License-Identifier: GPL-3.0-only */
/* A card sample heard on the SAMPLES list (the user, 2026-11-05; ui/optimist/op_sdcard.c, the last black key): the audio's
 * side, included by fx.c, called with the USB input after the parts. A voice of its own, one stream and one source of
 * their own (sd_stream.h SST_PV, SST_PVS: past the plan's, which never touch them), no track's engine or sound in it: the
 * file comes through the stream scheduler like any card voice and goes into the master mix, mono at half level, faded in
 * and out over SPV_FADE samples. The main loop's side: sd_mode.c sdm_pv_*. It runs on the core that runs mix_finish */
#if FELUCCA_SD_STREAM && FELUCCA_SD_UI
#define SPV_G_ONE 4096
#define SPV_FADE 256                                    /* (5.8 ms) */
static HOT2 __attribute__((noinline)) void sst_pv_mix(uint32_t n)
{
    sst_t *st = &sst[SST_PV];
    const sst_src_t *s = &sst_src[SST_PVS];
    uint32_t i;
    if (sst_pv.cmd == SPV_START) {
        sst_pv.cmd = 0;
        if (s->ready && st->ring) {
            sst_pv.fr = sst_pv.frac = 0;
            sst_pv.a = sst_pv.b = 0;
            sst_pv.g = 0;
            sst_pv.end = 0;
            sst_pv.act = 1;
            sst_arm(st, s, SST_PVS, 0, 0, 0, sst_rate(s, s->rate), &sst_pv.act);
            if (!sst_frame(st, s, 0, &sst_pv.b))
                sst_pv.end = 1;
        }
    } else if (sst_pv.cmd == SPV_STOP) {
        sst_pv.cmd = 0;
        sst_pv.end = 1;
    }
    if (!sst_pv.act)
        return;
    st->beat = st->beat + 1u;
    for (i = 0; i < n; i++) {
        int32_t x;
        sst_pv.frac += s->rate;
        while (sst_pv.frac >= 65536u) {                 /* (the next frame: a slot read never goes back) */
            sst_pv.frac -= 65536u;
            sst_pv.a = sst_pv.b;
            if (!sst_pv.end && (++sst_pv.fr >= s->frames || !sst_frame(st, s, sst_pv.fr, &sst_pv.b)))
                sst_pv.end = 1;                         /* (its end, or dry: faded out) */
        }
        sst_pv.g += sst_pv.end ? -(SPV_G_ONE / SPV_FADE) : SPV_G_ONE / SPV_FADE;
        sst_pv.g = sst_pv.g < 0 ? 0 : sst_pv.g > SPV_G_ONE ? SPV_G_ONE : sst_pv.g;
        if (sst_pv.end && !sst_pv.g) {
            sst_stop(SST_PV);
            sst_pv.act = 0;
            return;
        }
        x = sst_pv.a + (((sst_pv.b - sst_pv.a) * (int32_t)(sst_pv.frac >> 1)) >> 15);
        x = (x * sst_pv.g) >> 13;                       /* (half level: over what the tracks play) */
        mix_l[i] += x;
        mix_r[i] += x;
    }
}
#endif
