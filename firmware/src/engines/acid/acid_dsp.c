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
#include "acid_mem.h"
#include "bass303.c"

#define ACID_PARTS 3
_Static_assert(sizeof(bass303_t) <= ACID_MEM_BYTES, "acid_mem.h ACID_MEM_BYTES: a part's engine memory holds a bass303_t");
/* a part's state besides its bass303_t: its 303 set up (ready), a note sounding (gate), a note's start waiting for the
 * part's next render (pend: acid_start below). Built in: one static instance a part (66 B, as the three arrays before);
 * a plugin (PLUG_ENG_ACID): in the part's engine memory after the bass303_t and the shared coefficients' copy
 * (acid_mem.h ACID_PLUG_BYTES), zeroed when the part comes to the plugin (as acid_forget) */
typedef struct {
    uint8_t ready, gate;
    struct acid_pend_s {
        uint8_t on, note, acc;
        int16_t e[8];
    } pend;
} acid_st_t;
#if defined(PLUG_ENG_ACID)
#define ACID_ST(k) ((acid_st_t *)((uint8_t *)acid_mem(k) + ACID_MEM_BYTES + sizeof(bass303_fixed_t)))
_Static_assert(ACID_MEM_BYTES + sizeof(bass303_fixed_t) + sizeof(acid_st_t) <= ACID_PLUG_BYTES,
               "acid_mem.h ACID_PLUG_BYTES: a plugin's part memory holds the bass303_t, the coefficients and acid_st_t");
#else
static acid_st_t acid_st[ACID_PARTS];
#define ACID_ST(k) (&acid_st[k])
#endif

void acid_forget(int k)                            /* (engines.c part_mem_claim: another engine had the memory) */
{
    if (k >= 0 && k < ACID_PARTS)
        ACID_ST(k)->ready = ACID_ST(k)->gate = ACID_ST(k)->pend.on = 0;
}

/* our 8 EDIT values -> the 303's pots: CUT RESO ENV DEC ACC WAVE DRV SLD (the rest: X0X's defaults). WAVE 0..3:
 * bit 0 the wave, bit 1 the drive type (0 Soft, 1 RAT); DRV > 0 runs it */
static const uint8_t ACID_POT[8] = {BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY, BASS303_ACCENT,
                                    BASS303_WAVE, BASS303_DRIVE, BASS303_SLIDE};

static FASTI(acid) bass303_t *acid_part(int k)
{
    if (k < 0 || k >= ACID_PARTS)
        return 0;
    if (!ACID_ST(k)->ready) {
        bass303_init((bass303_t *)acid_mem(k));
        ACID_ST(k)->ready = 1;
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
    ACID_ST(k)->gate = 1;
}

/* A note's start (acid_start, in the block's events on CPU0) waits for the part's next render (acid_due, in its job:
 * either core): its pots as the note found them (the authored EDIT values; the render's own come after, LFO / macro
 * moved), then the 303's note on, as before in the events. Anything else that touches the 303 before then (another
 * note, a slide, the render, a note off) does the waiting start first: the same calls in the same order */
FAST(acid) FM_SETUP void acid_due(int k)
{
    acid_st_t *s;
    if (k < 0 || k >= ACID_PARTS || !ACID_ST(k)->pend.on)
        return;
    s = ACID_ST(k);
    s->pend.on = 0;
    acid_pots(k, s->pend.e);
    acid_note0(k, s->pend.note, s->pend.acc, 0);
}
FAST(acid) FM_SETUP void acid_start(int k, const int16_t *e, int note, int accent)
{
    int i;
    if (k < 0 || k >= ACID_PARTS)
        return;
    acid_st_t *s;
    acid_due(k);
    s = ACID_ST(k);
    for (i = 0; i < 8; i++)
        s->pend.e[i] = e[i];
    s->pend.note = (uint8_t)note, s->pend.acc = (uint8_t)(accent != 0), s->pend.on = 1;
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
    if (b && ACID_ST(k)->gate) {
        bass303_note_off(b);
        ACID_ST(k)->gate = 0;
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
