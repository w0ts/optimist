/* SPDX-License-Identifier: GPL-3.0-only */
/* Every factory preset plays a phrase that fits it (a bass line, held chords, comping or a melody),
 * 4.75 s each, through the whole mix (sends, master), for tools/level_presets.py.
 *   argv[1]: a WAV of them all, argv[2]: the list (engine preset role name), argv[3]: mono int32 raw
 *   cc -O2 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_level.c -lm -o build/host/preset_level */
#define main hostsim_main
#include "hostsim.c"
#undef main

enum { R_BASS, R_HELD, R_COMP, R_MELODY, R_ONE };
static int has(const char *n, const char *w) { return strstr(n, w) != 0; }
static int role_of(const char *n)
{
    if (has(n, "BASS") || has(n, "808") || has(n, "ACID") || has(n, "REESE") || has(n, "WOBBLE") || has(n, "SUB") || has(n, "BOOM"))
        return R_BASS;
    if (has(n, "PAD") || has(n, "STR") || has(n, "CHOIR") || has(n, "OOH") || has(n, "ORGAN") || has(n, "ORGN") || has(n, "GOSPEL") ||
        has(n, "B3") || has(n, "CLOUD") || has(n, "HAZE") || has(n, "ATMOS"))
        return R_HELD;
    if ((has(n, "STAB") && !has(n, "HORN") && !has(n, "STRING")) || has(n, "CHORD"))
        return R_ONE;                                  /* TRIO stabs: one key plays the chord */
    if (has(n, "RHODES") || has(n, "WURLI") || has(n, "CLAV") || has(n, "KEYS") || has(n, "STAB") || has(n, "PNO") || has(n, "PIANO") ||
        has(n, "VIBES") || has(n, "BRASS") || has(n, "HORN") || has(n, "CHRD"))   /* SUPER CHRD: a chord a hit */
        return R_COMP;
    return R_MELODY;
}

typedef struct { uint8_t step, note, len; } ev_t;   /* 1/16 steps at 100 BPM */
static const ev_t BASS[] = {{0, 36, 3}, {4, 36, 1}, {6, 43, 2}, {8, 41, 3}, {12, 39, 2}, {14, 38, 2},
                            {16, 36, 3}, {20, 36, 1}, {22, 43, 2}, {24, 46, 3}, {28, 43, 4}};
static const ev_t MELODY[] = {{0, 72, 2}, {2, 75, 2}, {4, 77, 3}, {8, 79, 2}, {10, 77, 2}, {12, 75, 4},
                              {16, 72, 2}, {18, 70, 2}, {20, 72, 3}, {24, 67, 4}, {28, 70, 2}, {30, 72, 2}};
static const uint8_t CHORD[2][4] = {{60, 63, 67, 70}, {58, 62, 65, 68}};   /* Cm7, Bb7sus-ish */
static const uint8_t COMP[] = {0, 3, 6, 10, 12, 16, 19, 22, 26, 28};

static uint8_t held[128];
static uint32_t off_at[128];
static void play(uint32_t note, uint32_t vel, uint32_t off)
{
    trk_note_on(&trk[0], note, vel);
    held[note] = 1;
    off_at[note] = off;
}

int main(int argc, char **argv)
{
    FILE *w = fopen(argv[1], "wb"), *rep = fopen(argv[2], "w"), *raw = fopen(argv[3], "wb");
    uint32_t frames = 0, e, pi, b, n, i;
    const uint32_t step = FS * 60u / 100u / 4u, nb = (FS * 9u / 2u) / CTL, tail = (FS / 4u) / CTL;
    int32_t o[2 * CTL];
    if (!w || !rep || !raw)
        return 1;
    wav_hdr(w, 0);
    for (e = 0; e < NENGINES; e++)
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            const char *name = ENGINES[e]->presets[pi].name;
            int role = role_of(name);
            host_tracks_init();
            song.g[G_BPM] = 100;
            host_preset(&trk[0], e, pi);
            song.sel = 0;
            memset(held, 0, sizeof held);
            for (b = 0; b < FS / CTL; b++)                    /* the last one's sends die out (not kept) */
                mix_block(o, CTL);
            fprintf(rep, "%u %u %d %s\n", e, pi, role, name);
            for (b = 0; b < nb + tail; b++) {
                uint32_t s0 = b * CTL, s1 = s0 + CTL, st;
                for (n = 0; n < 128u; n++)
                    if (held[n] && (off_at[n] < s1 || b >= nb)) {
                        trk_note_off(&trk[0], n);
                        held[n] = 0;
                    }
                for (st = 0; st < 32u && b < nb; st++) {
                    uint32_t at = st * step;
                    if (at < s0 || at >= s1)
                        continue;
                    if (role == R_BASS || role == R_MELODY) {
                        const ev_t *ev = role == R_BASS ? BASS : MELODY;
                        uint32_t ne = role == R_BASS ? sizeof BASS / sizeof BASS[0] : sizeof MELODY / sizeof MELODY[0];
                        for (i = 0; i < ne; i++)
                            if (ev[i].step == st)
                                play(ev[i].note, 100, at + ev[i].len * step - step / 4u);
                    } else if (role == R_HELD) {
                        if (st == 0 || st == 16)
                            for (i = 0; i < 4u; i++)
                                play(CHORD[st / 16u][i], 90, at + 15u * step);
                    } else {
                        for (n = 0; n < sizeof COMP; n++)
                            if (COMP[n] == st)
                                for (i = 0; i < (role == R_ONE ? 1u : 4u); i++)
                                    play(CHORD[st / 16u][i], 95, at + 2u * step - step / 3u);
                    }
                }
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++) {
                    int32_t m = (o[2 * i] + o[2 * i + 1]) / 2;
                    wav_put(w, o[2 * i], o[2 * i + 1]);
                    fwrite(&m, 4, 1, raw);
                    frames++;
                }
            }
        }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames);
    fclose(w);
    fclose(rep);
    fclose(raw);
    return 0;
}
