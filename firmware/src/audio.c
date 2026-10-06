/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the audio ISR: each half buffer is
 * rendered in blocks of CTL samples by mix_block (fx.c: events -> each synth part
 * -> dist -> level / pan -> sends -> drums -> buses -> master), then scaled to 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;

#if FELUCCA_USB_AUDIO
/* USB audio (usb_audio*.c), once per mix block (CTL frames, in the audio ISR): the stems out, the computer's
 * audio into `out`. TIMER5 outranks rendering, so serialize only this short PCM copy (and the
 * stream resets / alternate changes it races with), not the synth / FX work. Not inlined: the
 * render loop stays as it was without USB audio (tests/target_budget.py). */
static __attribute__((noinline)) void ua_block(int32_t *out, uint32_t n)
{
    fm1_irq_off();
    if (usb.up && usb.config && !usb.suspended && (ua.play_alt | ua.cap_alt))
        ua_audio(out, track_capture, n, song.master_q12);
    fm1_irq_on();
}
#endif

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
#if FELUCCA_DUAL >= 2
    FAR(mix_block_dual)(out, n);                        /* (dual.c; RAM code) */
#else
    FAR(mix_block)(out, n);                             /* (RAM code: core.h HOT) */
#endif
#if FELUCCA_USB_AUDIO
    ua_block(out, n);                                   /* the stems out, the computer's audio in */
#endif
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
        out[2u * i] <<= OUT_SHIFT;
        out[2u * i + 1u] <<= OUT_SHIFT;
    }
}

/* overload: a half that took > 85 % of its time sheds one voice before the
 * next one, over all parts: the quietest releasing voice fades out over the next
 * block (voice_kill), else the oldest held one goes into its release (stopped by
 * a later shed if still needed). The only held voice is never touched, so a dense
 * chord on a heavy engine thins out instead of starving the CPU. */
static volatile uint8_t shed_req;
static uint32_t shed_count;

static void shed_voice(void)
{
    uint32_t p, i, ngate = 0;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        voice_kill(best);
        shed_count++;
        return;
    }
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate) {
                ngate++;
                if (!best || v->age < best->age)
                    best = v;
            }
        }
    if (ngate > 1u) {
        best->gate = 0;
        best->stage = 3;
        shed_count++;
    }
}

/* when the DMA switched halves: the entry time of the interrupt, as the lower envelope of the times
 * one half apart (an entry delayed by another interrupt or an IRQ-off window counts 1/16; earlier
 * than predicted: taken as is; 1 ms off: taken as is). The block's audio leaves HALF_FRAMES samples
 * after it (clock_sync.c: sync_out_t) */
static uint32_t sync_anchor(uint32_t t0)
{
    static uint32_t a;
    int32_t e;
    a += (HALF_FRAMES * SY_TPS_Q8) >> 8;
    e = (int32_t)(t0 - a);
    if (e < 0 || e > (int32_t)(1000u * FM1_TICKS_PER_US))
        a = t0;
    else
        a += (uint32_t)e >> 4;
    return a;
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    uint32_t t0 = fm1_ticks();
    fm1_audio_ack_aux(p);
#if FELUCCA_USB_AUDIO
    fm1_irq_on();                              /* TIMER5 (USB, priority 4) may preempt the render */
#endif
    felucca_dbg.in_audio = 1;
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us, a = sync_anchor(t0);
        int32_t *o = &abuf[half * HALF_WORDS];
#if FELUCCA_CPU_GUARD
        FAR(cg_pre)();                                     /* the predicted load; a shed it asked for (cpuguard.c) */
#else
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) {
            sync_out_t = a + (((HALF_FRAMES + b) * SY_TPS_Q8) >> 8);
            audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        audio_halves++;
        us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (us > audio_max_us)
            audio_max_us = us;
#if FELUCCA_CPU_GUARD
        FAR(cg_post)(us);                                  /* the load: ease back, shed, or let go (cpuguard.c) */
#else
        if (us * 100u > (HALF_FRAMES * 1000000u / FS) * 85u)
            shed_req = 1;
#endif
        song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
#if FELUCCA_USB_AUDIO
    /* off again before isr_alnk0 restores reti: a tick taken between that restore and its
     * rti would overwrite reti and return into the stub (seen in the emulator: rti twice) */
    fm1_irq_off();
#endif
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
