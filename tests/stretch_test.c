/* SPDX-License-Identifier: GPL-3.0-only */
/* The STRETCH engine (FELUCCA_ENG_STRETCH=1, engines/stretch/eng_stretch.c; tests/run_tests.sh builds this with it):
 *   - its UID 27 and fallback SAMPLE (registry.h);
 *   - the FFT chain alone (the forward FFT, the split, the inverse split and FFT, the windows): a frame of a tone comes
 *     back as itself times the two windows (SMEAR 0, the spectrum copied), to 1 %;
 *   - every preset: a chord held 3 s, the keys up, 9 s more: audible, bounded, no DC, silent and idle after;
 *   - each EDIT value changes the sound of a preset, bounded; the corners (every value at its least, at its most, two
 *     mixes) on notes 24 / 60 / 108: bounded, silent after;
 *   - the work is paced: no block does more than its cap, a frame is ready at every frame boundary of a steady note;
 *     nothing runs while no voice sounds;
 *   - one instance: a second part playing STRETCH while the first sounds stays silent, then takes it over;
 *   - deterministic: the same render twice.
 *   build/host/stretch_test
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("stretch: %-104s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++)
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    memset(&pstx, 0, sizeof pstx);
}

typedef struct { uint64_t hash; double rms, dc, tail; int32_t peak; uint32_t busy, maxwork, late, idle; } sr_res_t;
#define KEEP INT16_MIN
static const uint8_t CHORD[3] = {48, 55, 60};

/* track trk_i plays preset pi with set[] (KEEP: the preset's); nn notes held 3 s, up, 9 s more */
static sr_res_t sr_render(uint32_t pi, const int16_t *set, const uint8_t *notes, uint32_t nn)
{
    sr_res_t r = {1469598103934665603ull, 0, 0, 0, 0, 0, 0, 0, 0};
    int32_t out[CTL * 2];
    double acc = 0, sum = 0, tl = 0;
    uint32_t b, i, nb = 12u * FS / CTL, w0;
    reset();
    host_preset(&trk[0], ENG_SLOT_STRETCH, pi);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (i = 0; set && i < 8u; i++)
        if (set[i] != KEEP)
            trk[0].p[P_E0 + i] = set[i];
    for (i = 0; i < nn; i++)
        trk_note_on(&trk[0], notes[i], 100);
    for (b = 0; b < nb; b++) {
        if (b == 3u * FS / CTL)
            for (i = 0; i < nn; i++)
                trk_note_off(&trk[0], notes[i]);
        w0 = pstx.work;
        mix_block(out, CTL);
        if (pstx.phase != SP_IDLE && pstx.work >= w0 && pstx.work - w0 > r.maxwork && pstx.blk > 1u)
            r.maxwork = pstx.work - w0;
        if (b > 2u * ST_BLK && b < 3u * FS / CTL && pstx.blk == 1u && !pstx.ready)
            r.late++;                                 /* (a steady chord: every frame ready at its boundary) */
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            acc += (double)out[i] * out[i];
            sum += out[i];
            if (b >= nb - FS / CTL)
                tl += (double)out[i] * out[i];
            if (a > r.peak)
                r.peak = a;
            r.hash = (r.hash ^ (uint32_t)out[i]) * 1099511628211ull;
        }
    }
    for (i = 0; i < NVOICE; i++)
        r.busy += trk[0].v[i].active;
    r.idle = pstx.phase == SP_IDLE;
    r.rms = sqrt(acc / (2.0 * nb * CTL));
    r.dc = sum / (2.0 * nb * CTL);
    r.tail = sqrt(tl / (2.0 * FS));
    return r;
}

/* the chain alone on one frame: a tone at bin 97.3 (not on a bin), the spectrum copied as one voice at ratio 1 would */
static void t_chain(void)
{
    uint32_t i, bad = 0;
    double err = 0, ref = 0;
    memset(&pstx, 0, sizeof pstx);
    pstx.play = pst_buf[0], pstx.next = pst_buf[1], pstx.tail = pst_buf[2];
    memset(pst_buf, 0, sizeof pst_buf);
    for (i = 0; i < ST_N; i++) {
        int32_t s = (int32_t)(20000.0 * sin(2 * M_PI * 97.3 * i / ST_N));
        pst_a[i] = (int32_t)(((int64_t)(s << ST_IN_SH) * pst_win(i)) >> 15);
    }
    pstx.cur = 0, sp_brev(pst_a, 1u << 30);
    pstx.cur = 0, pstx.stage = 0, sp_fft(pst_a, 0, 1u << 30);
    pstx.cur = 0, sp_split(1u << 30, 0);
    for (i = 0; i < 2u * ST_M; i++)
        pst_b[i] = pst_a[i];
    pst_b[2u * ST_M] = pstx.nyq, pst_b[2u * ST_M + 1u] = 0;
    pstx.cur = 0, sp_isplit(1u << 30);
    pstx.cur = 0, sp_brev(pst_b, 1u << 30);
    pstx.cur = 0, pstx.stage = 0, sp_fft(pst_b, 1, 1u << 30);
    pstx.cur = 0, sp_ola(1u << 30);
    for (i = 0; i < ST_HOP; i++) {                    /* next = the frame's first half (the tail was 0): s w^2 */
        double s = 20000.0 * sin(2 * M_PI * 97.3 * i / ST_N), w = sin(M_PI * (i + 0.5) / ST_N), want = s * w * w;
        err += (pstx.next[i] - want) * (pstx.next[i] - want);
        ref += want * want;
        bad += fabs(pstx.next[i] - want) > 200.0;
    }
    {
        char what[128];
        snprintf(what, sizeof what, "the FFT chain alone: a frame of a tone back as itself x the two windows (error %.2f %%)",
                 100 * sqrt(err / ref));
        check(sqrt(err / ref) < 0.01 && !bad, what);
    }
}

static void t_presets(void)
{
    const engine_t *e = ENGINES[ENG_SLOT_STRETCH];
    uint32_t pi, ok = 1, paced = 1, late = 0;
    for (pi = 0; pi < e->npresets; pi++) {
        sr_res_t r = sr_render(pi, 0, CHORD, 3);
        printf("stretch: %-12s rms %6.0f peak %5d dc %6.1f work/block %u (cap %u) %s%s\n", e->presets[pi].name, r.rms,
               r.peak, r.dc, r.maxwork, pst_units(trk[0].p) / (ST_BLK / 2u), r.busy ? "STILL SOUNDING " : "",
               r.late ? "LATE" : "");
        ok &= r.rms > 300 && r.peak <= 32767 && !r.busy && r.idle && r.dc < 50 && r.dc > -50;
        paced &= r.maxwork <= (ST_N + 8u * 3u * ST_M + 40000u) / (ST_BLK / 2u) + 64u;
        late += r.late;
    }
    check(ok, "every preset: audible, bounded, no DC, its voices free and the engine idle after the release");
    {                                                 /* the level against ANALOG's first preset, the same chord */
        double an = 0, mine = 0;
        char what[96];
        int32_t out[CTL * 2];
        uint32_t b, i;
        reset();
        host_preset(&trk[0], 0, 0);
        trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        for (i = 0; i < 3u; i++)
            trk_note_on(&trk[0], CHORD[i], 100);
        for (b = 0; b < 3u * FS / CTL; b++) {
            mix_block(out, CTL);
            for (i = 0; i < 2u * CTL; i++)
                an += (double)out[i] * out[i];
        }
        for (pi = 0; pi < e->npresets; pi++) {
            sr_res_t r = sr_render(pi, 0, CHORD, 3);
            mine += r.rms / e->npresets;
        }
        an = sqrt(an / (2.0 * 3 * FS));
        snprintf(what, sizeof what, "the presets' level within 9 dB of ANALOG's (%+.1f dB)", 20 * log10(mine * 2 / an));
        check(fabs(20 * log10(mine * 2 / an)) < 9.0, what);   /* (x 2: the renders' 12 s hold 3 s of the chord, 3 + release) */
    }
    check(paced, "the work paced: no block more than its share's cap");
    check(!late, "a steady chord: a frame ready at every frame boundary");
}

static void t_edits(void)
{
    const engine_t *e = ENGINES[ENG_SLOT_STRETCH];
    uint32_t k, ok = 1;
    for (k = 0; k < 8u; k++) {
        const param_desc_t *d = &e->edit[k];
        uint32_t pi, acts = 0, bounded = 1;
        for (pi = 0; pi < e->npresets && !acts; pi++) {
            int16_t set[8] = {KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP, KEEP};
            int32_t v = e->presets[pi].e[k], lo = d->min, hi = d->max;
            sr_res_t a = sr_render(pi, 0, CHORD, 3), b;
            set[k] = (int16_t)(v - lo > hi - v ? lo + (v - lo) / 3 : hi - (hi - v) / 3);
            b = sr_render(pi, set, CHORD, 3);
            acts = a.hash != b.hash;
            bounded &= b.peak <= 32767 && !b.busy;
        }
        if (!acts || !bounded) {
            printf("stretch: %s %s\n", d->label, !acts ? "changes no preset's sound" : "unbounded");
            ok = 0;
        }
    }
    check(ok, "each EDIT value changes the sound (on some preset), bounded");
}

static void t_corners(void)
{
    static const uint8_t NOTES[3] = {24, 60, 108};
    const engine_t *e = ENGINES[ENG_SLOT_STRETCH];
    uint32_t c, nt, ok = 1;
    for (c = 0; c < 4u; c++)
        for (nt = 0; nt < 3u; nt++) {
            int16_t set[8];
            uint32_t k;
            sr_res_t r;
            for (k = 0; k < 8u; k++) {
                int32_t lo = e->edit[k].min, hi = e->edit[k].max;
                set[k] = (int16_t)(c == 0 ? lo : c == 1 ? hi : ((k + c) & 1u) ? hi : lo);
            }
            set[0] = 0;                                /* (a built-in set: a user slot is empty here) */
            r = sr_render(0, set, &NOTES[nt], 1);
            if (r.peak > 32767 || fabs(r.dc) > 200 || r.busy || r.tail > 2 || !r.idle) {
                printf("stretch: corner %u note %u: peak %d dc %.1f tail %.1f busy %u idle %u\n", c, NOTES[nt], r.peak,
                       r.dc, r.tail, r.busy, r.idle);
                ok = 0;
            }
        }
    check(ok, "the corners, notes 24 / 60 / 108: bounded, no DC, silent, free and idle after the release");
}

/* two parts on STRETCH: the second stays silent while the first sounds, then takes the instance over */
static void t_owner(void)
{
    int32_t out[CTL * 2];
    uint32_t b, i;
    double during = 0, after = 0;
    reset();
    host_preset(&trk[0], ENG_SLOT_STRETCH, 0);
    host_preset(&trk[1], ENG_SLOT_STRETCH, 0);
    for (i = 0; i < 2u; i++)
        trk[i].p[P_DIST] = trk[i].p[P_CHOR] = trk[i].p[P_DLY] = trk[i].p[P_REV] = 0;
    trk_note_on(&trk[0], 60, 100);
    for (b = 0; b < FS / CTL; b++)
        mix_block(out, CTL);
    trk_note_on(&trk[1], 67, 100);
    for (b = 0; b < FS / CTL; b++)
        mix_block(out, CTL);
    during = pstx.owner;
    trk_note_off(&trk[0], 60);
    for (b = 0; b < 8u * FS / CTL; b++)
        mix_block(out, CTL);
    after = pstx.owner;
    check(during == 1.0 && after == 2.0, "one instance: the second part waits while the first sounds, then owns it");
}

static void t_ratio(void)
{
    const smp_zone_t *z = pst_zone(0, 0);
    uint32_t r0 = (uint32_t)(((uint64_t)pow2_q16(0) * z->rate) >> 16);
    uint32_t r12 = (uint32_t)(((uint64_t)pow2_q16(192) * z->rate) >> 16);
    check(r12 > 2u * r0 - 2u * (r0 >> 10) && r12 < 2u * r0 + 2u * (r0 >> 10) && r0,
          "a voice's ratio: an octave up twice the bins, the zone's rate in it");
}

int main(void)
{
    sr_res_t a, b;
    host_tracks_init();
    check(ENG_UID_STRETCH == 27u && ENG_FALLBACK[ENG_UID_STRETCH] == 4u && eng_built(27u) &&
          !strcmp(ENGINES[ENG_SLOT_STRETCH]->name, "STRETCH"), "STRETCH: UID 27, its fallback SAMPLE");
    t_chain();
    t_ratio();
    t_presets();
    t_edits();
    t_corners();
    t_owner();
    a = sr_render(0, 0, CHORD, 3);
    b = sr_render(0, 0, CHORD, 3);
    check(a.hash == b.hash, "deterministic: the same render twice");
    printf("stretch: %d failed\n", fails);
    return fails;
}
