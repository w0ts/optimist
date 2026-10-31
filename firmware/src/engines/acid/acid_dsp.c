/* SPDX-License-Identifier: GPL-3.0-only */
/* The ACID engine's float side (FELUCCA_ENG_ACID, eng_acid.c): X0X's TB-303 voice (bass303.c: Charles Vestal,
 * charlesvestal/fm1-x0x; Open303 by Robin Schmidt, MIT, with schwung-303's Devilfish ranges and Soft / RAT
 * drive) for the three synth parts, behind a small integer interface. The firmware is otherwise integer-only:
 * this file is its own translation unit, built with the FPU flags X0X uses (-mcpu=r3 -mfprev1
 * -ffp-contract=off; tools/build.py), and only the audio ISR runs it. The host tests include it directly. */
#ifndef FELUCCA_CPU_GUARD
#define FELUCCA_CPU_GUARD 0                        /* (the target builds this unit alone: tools/build.py passes it) */
#endif
#define BASS303_LITE FELUCCA_CPU_GUARD             /* the overload guard's lite mode: the CPU guard's (cpuguard.c) */
/* ACID "on, code in RAM" (the builder, FAST_RAM_acid; core.h FAST): its render path is RAM code, this unit's part of it
 * in the RAM code's section (.ram_hot; the release's opt_fpu_acid.c: .app_ram, FAST_SECTION), the float helpers too */
#ifndef FAST
#if defined(__PI32V2__) && defined(FAST_RAM_acid) && FAST_RAM_acid
#ifndef FAST_SECTION
#define FAST_SECTION ".ram_hot"
#endif
#define FAST(n) __attribute__((section(FAST_SECTION)))
#else
#define FAST(n)
#endif
#endif
#ifndef FASTI
#if defined(__PI32V2__) && defined(FAST_RAM_acid) && FAST_RAM_acid
#define FASTI(n) __attribute__((always_inline))
#else
#define FASTI(n)
#endif
#endif
#ifndef DSPF_FAST
#define DSPF_FAST FAST(acid)
#endif
#include "bass303.c"

#include "acid_mem.h"

#define ACID_PARTS 3
_Static_assert(sizeof(bass303_t) <= ACID_MEM_BYTES, "acid_mem.h ACID_MEM_BYTES: a part's engine memory holds a bass303_t");
static uint8_t acid_ready[ACID_PARTS], acid_gate[ACID_PARTS];
static struct acid_pend_s {
    uint8_t on, note, acc;
    int16_t e[8];
} acid_pend[ACID_PARTS];   /* (acid_start below) */

void acid_forget(int k)                            /* (engines.c part_mem_claim: another engine had the memory) */
{
    if (k >= 0 && k < ACID_PARTS)
        acid_ready[k] = acid_gate[k] = acid_pend[k].on = 0;
}

/* our 8 EDIT values -> the 303's pots: CUT RESO ENV DEC ACC WAVE DRV SLD (the rest: X0X's defaults). WAVE 0..3:
 * bit 0 the wave, bit 1 the drive type (0 Soft, 1 RAT); DRV > 0 runs it */
static const uint8_t ACID_POT[8] = {BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY, BASS303_ACCENT,
                                    BASS303_WAVE, BASS303_DRIVE, BASS303_SLIDE};

static FASTI(acid) bass303_t *acid_part(int k)
{
    if (k < 0 || k >= ACID_PARTS)
        return 0;
    if (!acid_ready[k]) {
        bass303_init((bass303_t *)acid_mem(k));
        acid_ready[k] = 1;
    }
    return (bass303_t *)acid_mem(k);
}

FAST(acid) FM_SETUP void acid_pots(int k, const int16_t *e)
{
    bass303_t *b = acid_part(k);
    int i;
    int16_t v, drv;
    if (!b)
        return;
    for (i = 0; i < 8; i++) {
        v = ACID_POT[i] == BASS303_WAVE ? (int16_t)(e[i] & 1) : e[i];
        if (bass303_get(b, ACID_POT[i]) != v)
            bass303_set(b, ACID_POT[i], v);
    }
    drv = (e[5] & 2) ? BASS303_DRV_RAT : BASS303_DRV_SOFT;   /* (e[5]: WAVE) */
    if (bass303_get(b, BASS303_DRVTYPE) != drv)
        bass303_set(b, BASS303_DRVTYPE, drv);
}

static FAST(acid) FM_SETUP void acid_note0(int k, int note, int accent, int slide)
{
    bass303_t *b = acid_part(k);
    if (!b)
        return;
    bass303_note_on(b, note, accent, slide);
    acid_gate[k] = 1;
}

/* A note's start (acid_start, in the block's events on CPU0) waits for the part's next render (acid_due, in its job:
 * either core): its pots as the note found them (the authored EDIT values; the render's own come after, LFO / macro
 * moved), then the 303's note on, as before in the events. Anything else that touches the 303 before then (another
 * note, a slide, the render, a note off) does the waiting start first: the same calls in the same order */
FAST(acid) FM_SETUP void acid_due(int k)
{
    if (k < 0 || k >= ACID_PARTS || !acid_pend[k].on)
        return;
    acid_pend[k].on = 0;
    acid_pots(k, acid_pend[k].e);
    acid_note0(k, acid_pend[k].note, acid_pend[k].acc, 0);
}
FAST(acid) FM_SETUP void acid_start(int k, const int16_t *e, int note, int accent)
{
    int i;
    if (k < 0 || k >= ACID_PARTS)
        return;
    acid_due(k);
    for (i = 0; i < 8; i++)
        acid_pend[k].e[i] = e[i];
    acid_pend[k].note = (uint8_t)note, acid_pend[k].acc = (uint8_t)(accent != 0), acid_pend[k].on = 1;
}
FAST(acid) FM_SETUP void acid_note(int k, int note, int accent, int slide)
{
    acid_due(k);
    acid_note0(k, note, accent, slide);
}

#if FELUCCA_CPU_GUARD
FAST(acid) void acid_lite(int k, int on)                      /* the CPU guard: 1 = no oversampling from the next block */
{
    bass303_t *b = acid_part(k);
    if (b)
        bass303_set_lite(b, on);
}
#endif

FAST(acid) FM_SETUP void acid_off(int k)
{
    bass303_t *b;
    acid_due(k);
    b = acid_part(k);
    if (b && acid_gate[k]) {
        bass303_note_off(b);
        acid_gate[k] = 0;
    }
}

/* n (<= 64) samples of part k added to out at gain (Q15 full scale = gain) */
FAST(acid) void acid_render(int k, int32_t *out, int n, int32_t gain)
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
        s = fm_clip_sym(s, 1048576.0f);              /* (Optimist: dsp_float.h, the X0X kits' clip too) */
        out[i] += (int32_t)s;
    }
}
