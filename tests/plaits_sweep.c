/* SPDX-License-Identifier: GPL-3.0-only */
/* Every Plaits algorithm Optimist has, swept, one line a case: its name and the FNV-1a 64 of its samples. Two builds that
 * print the same lines render the same samples (a change to the PLAITS kit's or engine's code meant to keep the sound:
 * diff the two outputs).
 *   drums   (FELUCCA_DS_PLAITS) KPUNC and SCRAK (drum_synth.c ds_on_snd / ds_render, drum_plaits.c): the four PLAITS kit
 *           sounds at every GM note of their lanes and velocities 1..127; then each sound's every field (pitch, fine,
 *           DECAY, TONE / FM, CLICK / SNAP, level, drive, the kit's crush) one at a time over 0..127, and 600 hits each
 *           with every field at random; each hit rendered until it ends (6 s at most)
 *   engine  (FELUCCA_ENG_PLAITS) the PLAITS engine's models (eng_plaits.c) through the host mix (dry): every preset
 *           with a chord; 80 runs a model with every EDIT value at random, the notes, their velocities and the values
 *           moving at random while they sound (a value every few blocks, new notes on sounding voices, notes off)
 *   build/host/plaits_sweep [drums|engine]   (no argument: both)
 * PLAITS_SWEEP_N=k: k random cases instead (a quick run). */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t sw_lcg = 0x2545F491u;
static uint32_t sw_rnd(uint32_t m) { sw_lcg = sw_lcg * 1664525u + 1013904223u; return (sw_lcg >> 8) % m; }
static uint32_t sw_n(uint32_t def)
{
    const char *e = getenv("PLAITS_SWEEP_N");
    return e && atoi(e) > 0 ? (uint32_t)atoi(e) : def;
}
#define H0 1469598103934665603ull
static uint64_t hput(uint64_t h, int32_t x) { return (h ^ (uint32_t)x) * 1099511628211ull; }

#if FELUCCA_DS_PLAITS
/* sound d hit (crush, semi, note, vel): its samples until it ends */
static void sw_hit(const char *name, const dsnd_t *d, uint32_t crush, int32_t semi, uint32_t note, uint32_t vel)
{
    static dsv_t s;
    int32_t o[CTL];
    uint64_t h = H0;
    uint32_t b, i;
    ds_on_snd(&s, d, crush, semi, note, vel);
    for (b = 0; b < 7u * FS / CTL; b++) {
        int alive = ds_render(&s, o, CTL);
        for (i = 0; i < CTL; i++)
            h = hput(h, o[i]);
        if (!alive)
            break;
    }
    printf("%s %016llx %u\n", name, (unsigned long long)h, b);
}
static void sw_drums(void)
{
    static const uint8_t NOTES[4] = {36, 35, 38, 40};   /* the PLAITS kit's KICK, KICK 2, SNARE, SNARE 2 */
    const dkit_t *k = ds_kit(DRUM_UID_XKIT + 1u);
    char nm[96];
    uint32_t si, v, f, c, n = sw_n(600);
    for (si = 0; si < 4u; si++) {
        int32_t semi;
        uint32_t lane = ds_lane(NOTES[si], &semi), note;
        dsnd_t d;
        ds_unpack(&d, DS_SND(k, lane));
        for (note = 0; note < 128u; note++) {           /* every GM note that plays this lane */
            int32_t sm;
            if (ds_lane(note, &sm) != lane)
                continue;
            for (v = 1; v < 128u; v += 9u) {
                snprintf(nm, sizeof nm, "kit lane%u note%u vel%u", lane, note, v);
                sw_hit(nm, &d, k->crush, sm, note, v);
            }
        }
        for (f = 0; f < 9u; f++)                        /* each field alone, 0..127 */
            for (v = 0; v < 128u; v += 3u) {
                dsnd_t e = d;
                uint32_t crush = k->crush;
                switch (f) {
                case 0: e.pitch = (uint8_t)v; break;
                case 1: e.fine = (uint8_t)(v & 15u); break;
                case 2: e.decay = (uint8_t)v; break;
                case 3: e.fcut = (uint8_t)v; break;
                case 4: e.click = (uint8_t)v; break;
                case 5: e.nlev = (uint8_t)v; break;
                case 6: e.level = (uint8_t)(v * 2u); break;
                case 7: e.drive = (uint8_t)v; break;
                default: crush = v; break;
                }
                snprintf(nm, sizeof nm, "field lane%u f%u v%u", lane, f, v);
                sw_hit(nm, &e, crush, 0, NOTES[si], 100u);
            }
        for (c = 0; c < n; c++) {                       /* every field at random */
            dsnd_t e = d;
            uint32_t crush = sw_rnd(4u) ? 0u : sw_rnd(256u);
            e.pitch = (uint8_t)sw_rnd(128u), e.fine = (uint8_t)sw_rnd(16u), e.decay = (uint8_t)sw_rnd(128u);
            e.fcut = (uint8_t)sw_rnd(128u), e.click = (uint8_t)sw_rnd(128u), e.nlev = (uint8_t)sw_rnd(128u);
            e.drive = (uint8_t)(sw_rnd(3u) ? 0u : sw_rnd(128u)), e.level = (uint8_t)(64u + sw_rnd(128u));
            snprintf(nm, sizeof nm, "rand lane%u c%u", lane, c);
            sw_hit(nm, &e, crush, (int32_t)sw_rnd(25u) - 12, NOTES[si], 1u + sw_rnd(127u));
        }
    }
}
#endif

#if FELUCCA_ENG_PLAITS
static void sw_reset(void)
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
}
/* preset pi (its EDIT values set[] where >= 0), moving at random when mv; nb blocks */
static void sw_eng(const char *name, uint32_t pi, const int16_t *set, uint32_t mv, uint32_t nb)
{
    int32_t out[CTL * 2];
    uint64_t h = H0;
    uint32_t b, i;
    uint8_t held[4] = {0};
    sw_reset();
    host_preset(&trk[0], ENG_IX_PLAITS, pi);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    for (i = 0; set && i < 8u; i++)
        if (set[i] >= 0)
            trk[0].p[P_E0 + i] = set[i];
    for (i = 0; i < 4u; i++) {
        held[i] = (uint8_t)(mv ? 24u + sw_rnd(80u) : 48u + 7u * i);
        trk_note_on(&trk[0], held[i], mv ? 1u + sw_rnd(127u) : 100u);
    }
    for (b = 0; b < nb; b++) {
        if (mv && sw_rnd(6u) == 0u)                     /* a value moves (MODEL kept: the run's model) */
            trk[0].p[P_E1 + sw_rnd(7u)] = (int16_t)sw_rnd(128u);
        if (mv && sw_rnd(40u) == 0u) {                  /* a key again (a sounding voice struck), or another */
            i = sw_rnd(4u);
            if (sw_rnd(2u)) {
                trk_note_off(&trk[0], held[i]);
                held[i] = (uint8_t)(24u + sw_rnd(80u));
            }
            trk_note_on(&trk[0], held[i], 1u + sw_rnd(127u));
        }
        if (mv && sw_rnd(60u) == 0u)
            trk_note_off(&trk[0], held[sw_rnd(4u)]);
        if (!mv && b == 2u * FS / CTL)
            for (i = 0; i < 4u; i++)
                trk_note_off(&trk[0], held[i]);
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = hput(h, out[i]);
    }
    printf("%s %016llx\n", name, (unsigned long long)h);
}
static void sw_engine(void)
{
    const engine_t *e = ENGINES[ENG_IX_PLAITS];
    char nm[64];
    uint32_t pi, md, c, i, n = sw_n(80);
    for (pi = 0; pi < e->npresets; pi++) {
        snprintf(nm, sizeof nm, "preset %u", pi);
        sw_eng(nm, pi, 0, 0, 5u * FS / CTL);
    }
    for (md = 0; md < 4u; md++)
        for (c = 0; c < n; c++) {
            int16_t set[8];
            set[0] = (int16_t)md;
            for (i = 1; i < 8u; i++)
                set[i] = (int16_t)sw_rnd(128u);
            snprintf(nm, sizeof nm, "model %u c%u", md, c);
            sw_eng(nm, 0, set, 1, 3u * FS / CTL);
        }
}
#endif

int main(int argc, char **argv)
{
    const char *w = argc > 1 ? argv[1] : "";
    host_tracks_init();
#if FELUCCA_DS_PLAITS
    if (!*w || !strcmp(w, "drums"))
        sw_drums();
#endif
#if FELUCCA_ENG_PLAITS
    if (!*w || !strcmp(w, "engine"))
        sw_engine();
#endif
    return 0;
}
