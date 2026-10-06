/* SPDX-License-Identifier: GPL-3.0-only */
/* The ACID engine's float side (FELUCCA_ENG_ACID, eng_acid.c): X0X's TB-303 voice (bass303.c: Charles Vestal,
 * charlesvestal/fm1-x0x; Open303 by Robin Schmidt, MIT, with schwung-303's Devilfish ranges and Soft / RAT
 * drive) for the three synth parts, behind a small integer interface. The firmware is otherwise integer-only:
 * this file is its own translation unit, built with the FPU flags X0X uses (-mcpu=r3 -mfprev1
 * -ffp-contract=off; tools/build.py), and only the audio ISR runs it. The host tests include it directly. */
#define BASS303_LITE 0                             /* (the overload guard's lite mode: not used here) */
#include "bass303.c"

#define ACID_PARTS 3
static bass303_t acid_b[ACID_PARTS];
static uint8_t acid_ready[ACID_PARTS], acid_gate[ACID_PARTS];

/* our 8 EDIT values -> the 303's pots: CUT RESO ENV DEC ACC WAVE DRV SLD (the rest: X0X's defaults; DRV > 0 runs
 * the Soft drive) */
static const uint8_t ACID_POT[8] = {BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY, BASS303_ACCENT,
                                    BASS303_WAVE, BASS303_DRIVE, BASS303_SLIDE};

static bass303_t *acid_part(int k)
{
    if (k < 0 || k >= ACID_PARTS)
        return 0;
    if (!acid_ready[k]) {
        bass303_init(&acid_b[k]);
        acid_ready[k] = 1;
    }
    return &acid_b[k];
}

void acid_pots(int k, const int16_t *e)
{
    bass303_t *b = acid_part(k);
    int i;
    if (!b)
        return;
    for (i = 0; i < 8; i++)
        if (bass303_get(b, ACID_POT[i]) != e[i])
            bass303_set(b, ACID_POT[i], e[i]);
}

void acid_note(int k, int note, int accent, int slide)
{
    bass303_t *b = acid_part(k);
    if (!b)
        return;
    bass303_note_on(b, note, accent, slide);
    acid_gate[k] = 1;
}

void acid_off(int k)
{
    bass303_t *b = acid_part(k);
    if (b && acid_gate[k]) {
        bass303_note_off(b);
        acid_gate[k] = 0;
    }
}

/* n (<= 64) samples of part k added to out at gain (Q15 full scale = gain) */
void acid_render(int k, int32_t *out, int n, int32_t gain)
{
    bass303_t *b = acid_part(k);
    float y[64], g = (float)gain;
    int i;
    if (!b || n <= 0)
        return;
    if (n > 64)
        n = 64;
    bass303_render(b, y, n);
    for (i = 0; i < n; i++) {
        float s = y[i] * g;
        s = s > 1048576.0f ? 1048576.0f : s < -1048576.0f ? -1048576.0f : s;
        out[i] += (int32_t)s;
    }
}
