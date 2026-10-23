/* SPDX-License-Identifier: GPL-3.0-only */
/* The synthesised kits after the X0X ones (kit UIDs 47..: tools/gen_drumkits.py XKITS) and what they play:
 *   uids      KAWA 47, PLAITS 48, their names; a build without one plays its stand-in (the 808, the 909; without the 32
 *             kits, the first kit built); the 32 kits, KAWA, PLAITS each a switch of its own (FELUCCA_DRUM_SYNTH 0 with
 *             KAWA or PLAITS: every kit UID plays a built kit, the power-on kit is built)
 *   lanes     a lane source names them past the X0X voices (DL_XKIT0 96 + k), both ways; a lane of the 808 kit
 *             borrowing PLAITS' kick plays PLAITS' kick
 *   kawa      (DS_KAWA) the re-strikes: the guiro's ridges (onsets), the clap's hands; RISE: the shaker's peak comes
 *             later than the 808's; another METAL tuning, the partial's RING / FM: another sound than MIX
 *   plaits    (DS_PLAITS) KPUNC's body at its note (zero crossings ~ 2 x 49 Hz), SCRAK noisy (brighter than the
 *             kick), the velocity its accent (softer, quieter); every hit ends
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("ds_xkits: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
#define RN (FS * 2u)
static int32_t ra[RN], rb[RN];
/* sound d hit at note / vel, rendered alone (drum_synth.c), up to 2 s; the blocks it lasted */
static uint32_t render(const dsnd_t *d, uint32_t note, uint32_t vel, int32_t *o)
{
    static dsv_t s;
    uint32_t n, b = 0;
    memset(o, 0, RN * sizeof *o);
    ds_on_snd(&s, d, 0, 0, note, vel);
    for (n = 0; n + CTL <= RN; n += CTL) {
        b++;
        if (!ds_render(&s, &o[n], CTL))
            break;
    }
    return b;
}
static uint64_t hash(const int32_t *x)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < RN; i++)
        h = (h ^ (uint32_t)x[i]) * 1099511628211ull;
    return h;
}
static double hf(const int32_t *x, uint32_t n)        /* the first difference's energy over the signal's */
{
    double e = 0, d = 0;
    uint32_t i;
    for (i = 1; i < n; i++)
        e += (double)x[i] * x[i], d += ((double)x[i] - x[i - 1]) * ((double)x[i] - x[i - 1]);
    return e > 0 ? d / e : 0;
}
static int32_t peak(const int32_t *x, uint32_t a, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = a; i < a + n && i < RN; i++)
        p = x[i] < 0 ? (-x[i] > p ? -x[i] : p) : (x[i] > p ? x[i] : p);
    return p;
}
static double energy(const int32_t *x, uint32_t a, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = a; i < a + n && i < RN; i++)
        e += (double)x[i] * x[i];
    return e;
}
/* onsets: 2 ms windows whose peak is 2x the last window's and above a tenth of the whole peak */
static uint32_t onsets(const int32_t *x, uint32_t n)
{
    uint32_t w = FS / 500u, i, c = 0;
    int32_t last = 0, all = peak(x, 0, n);
    for (i = 0; i + w <= n; i += w) {
        int32_t p = peak(x, i, w);
        c += p > 2 * last && p > all / 10;
        last = p;
    }
    return c;
}
static const dsnd_t *snd(uint32_t kit, uint32_t note)
{
    static dsnd_t full[4];                              /* (the kit's compact form unpacked, as drum_on does; 4 held at once) */
    static uint32_t k;
    int32_t semi;
    dsnd_t *d = &full[k++ & 3u];
    ds_unpack(d, DS_SND(ds_kit(kit), ds_lane(note, &semi)));
    return d;
}

int main(void)
{
    host_tracks_init();
    /* ---- uids, names, stand-ins */
    check(DRUM_KITS == DRUM_UID_XKIT + 2u && !strcmp(DRUM_KIT_NAMES[47], "KAWA") && !strcmp(DRUM_KIT_NAMES[48], "PLAITS"),
          "kit UIDs 47 KAWA, 48 PLAITS (after the X0X kits)");
    check(drum_kit_of(47) == (FELUCCA_DS_KAWA ? 47u : FELUCCA_DRUM_SYNTH ? DRUM_SAMPLED : DRUM_OTHER),
          "KAWA: built, or the 808 plays for it (without the 32 kits: the first kit built)");
    check(drum_kit_of(48) == (FELUCCA_DS_PLAITS ? 48u : FELUCCA_DRUM_SYNTH ? DRUM_SAMPLED + 1u : DRUM_OTHER),
          "PLAITS: built, or the 909 plays for it (without the 32 kits: the first kit built)");
    {   /* the three drum synths independent (the 32 kits, KAWA, PLAITS): every kit UID plays a kit of this build */
        uint32_t k, ok = 1, first = DRUM_KITS;
        for (k = 0; k < DRUM_KITS; k++) {
            ok &= drum_kit_built(drum_kit_of((int32_t)k));
            if (first == DRUM_KITS && drum_kit_built(k))
                first = k;
        }
        printf("ds_xkits: the 32 kits %d, KAWA %d, PLAITS %d, sampled 0x%02x: the first kit built %u, power-on %u, "
               "the stand-in of a kit not built %u\n", FELUCCA_DRUM_SYNTH, FELUCCA_DS_KAWA, FELUCCA_DS_PLAITS, DRUM_SMASK,
               first, DRUM_DEFAULT_KIT, DRUM_OTHER);
        check(ok, "every kit UID (a project's, a lane's) plays a kit this build has");
        check(drum_kit_built(DRUM_DEFAULT_KIT) && drum_kit_built(DRUM_OTHER), "the power-on kit and the stand-in: built");
        check(drum_kit_built(DRUM_SAMPLED) == FELUCCA_DRUM_SYNTH && drum_kit_built(DRUM_SYNTH_END - 1u) == FELUCCA_DRUM_SYNTH &&
              drum_kit_built(47) == FELUCCA_DS_KAWA && drum_kit_built(48) == FELUCCA_DS_PLAITS,
              "the 32 kits, KAWA, PLAITS: each its own switch");
        check(FELUCCA_DRUM_SYNTH || DRUM_SMASK || DRUM_DEFAULT_KIT == first,
              "without the 32 kits and the sampled ones: the power-on kit is the first kit built");
#if DRUM_X0X
        check(x0x_standin(DRUM_UID_X909) == DRUM_KITS || drum_kit_built(x0x_standin(DRUM_UID_X909)),
              "an X0X kit's missing sounds: a built kit's, or silent");
#endif
    }
    check(DRUM_IS_SYNTH(47) && DRUM_IS_SYNTH(48) && !DRUM_IS_X0X(47) && DRUM_IS_X0X(46) && DRUM_UID_XMACH(47) == 47u,
          "47.. are synthesised kits, not X0X ones");
    /* ---- lane sources */
    check(dl_kit_src(47) == 96u && dl_kit_src(48) == 97u && dl_src_kit(96) == 47u && dl_src_kit(97) == 48u &&
          dl_src_kit(DL_KIT0 + 5u) == 5u && dl_src_kit(DL_X909) == DRUM_KITS && dl_src_kit(DL_X808 + 3u) == DRUM_KITS,
          "lane sources: kits 0..46 at 16.., the X0X voices at 64.., kits 47.. at 96..");
    {   /* the 808 kit's kick lane borrowing PLAITS' kick */
        int32_t l[CTL], r[CTL], rv[CTL];
        uint32_t n;
        uint64_t ha = 1469598103934665603ull, hb = ha;
        memset(&dl, 0, sizeof dl);
        TDRUM->p[P_E0] = (int16_t)DRUM_SAMPLED;
        dl.src[0] = (uint8_t)dl_kit_src(48);
        check(dl_kit_of(0, DRUM_SAMPLED) == drum_kit_of(48), "the 808's kick lane on PLAITS: that kit (or its stand-in)");
        memset(&drums, 0, sizeof drums);
        drums_set = -2;
        drum_on(36, 110);
        {
            uint32_t v, hit = 0;
            for (v = 0; v < NDRUM; v++)
                hit |= drums.v[v].active && (!DRUM_IS_SYNTH(drum_kit_of(48)) ||
                                             (drums.synth[v] && !memcmp(drums.ds[v].d, snd(drum_kit_of(48), 36), sizeof(dsnd_t))));
            check(hit, "... the voice plays PLAITS' kick sound (DS_XKITS), not the 808's (or its stand-in's)");
        }
        for (int k = 0; k < 2; k++) {
            memset(&drums, 0, sizeof drums);
            drums_set = -2;
            if (k) {
                memset(&dl, 0, sizeof dl);
                TDRUM->p[P_E0] = 48;
            }
            drum_on(36, 110);
            for (n = 0; n < FS / CTL; n++) {
                memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
                drums_render(l, r, rv, CTL);
                for (int i = 0; i < CTL; i++)
                    *(k ? &hb : &ha) = (*(k ? &hb : &ha) ^ (uint32_t)l[i]) * 1099511628211ull;
            }
        }
        check(ha != 1469598103934665603ull && (hb != ha || !DRUM_IS_SYNTH(drum_kit_of(48))),
              "... both heard (the noise seeds differ hit to hit; a sampled stand-in plays the same twice)");
        memset(&dl, 0, sizeof dl);
    }
#if FELUCCA_DS_KAWA
    {
        const dsnd_t *g = snd(47, 75), *cl = snd(47, 39), *sh = snd(47, 70), *s8 = snd(DRUM_SAMPLED, 70);
        dsnd_t m;
        uint32_t og, oc;
        render(g, 75, 110, ra);
        og = onsets(ra, FS / 2u);
        render(cl, 39, 110, ra);
        oc = onsets(ra, FS / 2u);
        printf("ds_xkits: onsets: KAWA guiro %u, clap %u\n", og, oc);
        check(DK_HITS(g) == 6u && og >= 5u, "KAWA CLAVE (a guiro): its re-strikes, the ridges heard (>= 5 onsets)");
        check(DK_HITS(cl) == 3u && oc >= 3u, "KAWA CLAP: its hands (>= 3 onsets)");
        m = *sh;
        render(&m, 70, 110, ra);
        m.krise = 0;
        render(&m, 70, 110, rb);
        {
            double e0 = energy(ra, 0, FS * 3u / 1000u) / (energy(rb, 0, FS * 3u / 1000u) + 1);
            double e1 = energy(ra, FS * 20u / 1000u, FS / 10u) / (energy(rb, FS * 20u / 1000u, FS / 10u) + 1);
            printf("ds_xkits: shaker with / without RISE: the first 3 ms x%.2f, 20..120 ms x%.2f\n", e0, e1);
            check(sh->krise && e0 < 0.3 && e1 > 0.7, "KAWA SHAKER: RISE, it swells in (quiet at first, as loud after)");
        }
        (void)s8;
        m = *snd(47, 42);
        render(&m, 42, 110, ra);
        m.kx = (uint8_t)(m.kx & ~3u);
        render(&m, 42, 110, rb);
        check(DK_MSET(snd(47, 42)) == 2u && hash(ra) != hash(rb), "KAWA CHH: METAL's tight cluster, another sound than the 808's tuning");
        m = *snd(47, 41);
        render(&m, 41, 110, ra);
        m.kx = (uint8_t)(m.kx & ~0x0Cu);
        render(&m, 41, 110, rb);
        check(DK_T2MODE(snd(47, 41)) == DK_RING && hash(ra) != hash(rb) && peak(ra, 0, RN) < 40000,
              "KAWA TOMLO: the partial RINGs the tone (another sound than MIX), bounded");
        m = *snd(47, 36);
        render(&m, 36, 110, ra);
        m.kx = (uint8_t)(m.kx & ~0x0Cu);
        render(&m, 36, 110, rb);
        check(DK_T2MODE(snd(47, 36)) == DK_FM && hash(ra) != hash(rb) && hf(ra, FS / 10u) > hf(rb, FS / 10u),
              "KAWA KICK: the partial's FM, brighter than MIX");
    }
#endif
#if FELUCCA_DS_PLAITS
    {
        const dsnd_t *k = snd(48, 36), *s = snd(48, 38);
        uint32_t i, c = 0, bk, bs;
        double hk, hs;
        int32_t p1, p2;
        bk = render(k, 36, 110, ra);
        for (i = FS / 10u; i < FS * 3u / 10u; i++)
            c += (ra[i - 1] < 0) != (ra[i] < 0);
        printf("ds_xkits: KPUNC: %u zero crossings in 0.2 s (49 Hz: ~20), lasts %u ms\n", c, bk * CTL * 1000u / FS);
        check(k->wave == DW_PKICK && c >= 14u && c <= 28u, "PLAITS KICK (KPUNC): its body at its note, 49 Hz");
        hk = hf(ra, FS / 5u);
        p1 = peak(ra, 0, RN);
        bs = render(s, 38, 110, rb);
        hs = hf(rb, FS / 5u);
        printf("ds_xkits: brightness KPUNC %.4f SCRAK %.4f; SCRAK lasts %u ms\n", hk, hs, bs * CTL * 1000u / FS);
        check(s->wave == DW_PSNARE && hs > hk * 10.0, "PLAITS SNARE (SCRAK): noisy, far brighter than the kick");
        check(bk < RN / CTL && bs < RN / CTL, "both hits end (within 2 s)");
        render(k, 36, 40, ra);
        p2 = peak(ra, 0, RN);
        printf("ds_xkits: KPUNC peak at velocity 110 / 40: %d %d\n", p1, p2);
        check(p2 < p1 / 2 && p2 > p1 / 8, "the velocity: the model's accent and the level (softer, quieter)");
    }
#endif
    printf("ds_xkits: %d failed\n", fails);
    return fails != 0;
}
