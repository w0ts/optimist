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
#define DBG_MAGIC 0x44424732u                       /* "DBG2" (2026-10-15: alive_ms, c1, crash_seen) */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
    uint32_t alive_ms, c1;                /* fm1_ms at the last TIMER5 millisecond; CPU1's stage then (DUAL builds) */
    uint32_t prev_alive_ms, prev_c1;      /* (as found at boot: how long the boot before ran) */
    uint32_t crash_seen, crash_new;       /* fm1_crash.count at boot; 1: it changed since (that boot ended in a crash) */
    uint32_t boot_failed, boot_pending;   /* bootguard as this boot found it (after bootguard_begin) */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;

#if FELUCCA_USB_AUDIO
/* USB audio (usb_audio*.c), once per mix block (CTL frames, in the audio ISR): the stems out, the computer's
 * audio into `out`. TIMER5 outranks rendering, so serialize only this short PCM copy (and the
 * stream resets / alternate changes it races with), not the synth / FX work. Not inlined: the
 * render loop stays as it was without USB audio (tests/target_budget.py).
 * Only TIMER5 (the USB service) is held off, in the interrupt controller: the global enable is not touched here
 * nor anywhere in the audio interrupt, which returns with it as it found it, as in a build without USB audio. A cli
 * / sti here and at the interrupt's entry and end hung every USB audio build on the FM-1 (2026-10-12: interrupts
 * dead 0.6-2.4 s after enumeration, the watchdog reset; this fix streams on the FM-1). */
static __attribute__((noinline)) void ua_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    const int32_t *cap = track_capture_on ? ua_route(out, n) : track_capture;   /* the channels' sources (SYSTEM > USB
                                                         * AUDIO, usb_audio_route.c; the default: the stems as they are),
                                                         * before the hold: no work added to the USB service */
    fm1_irq_hold(FM1_IRQ_TIMER5, 1);
    if (usb.up && usb.config && !usb.suspended && (ua.play_alt | ua.cap_alt)) {
        if (ua.cap_alt && !track_capture_on)    /* (the host started taking them during this block: not */
            for (i = 0; i < n * NTRK; i++)      /*  cleared for it, its stems are silence) */
                track_capture[i] = 0;
        ua_audio(out, cap, n, song.master_q12);
    }
    fm1_irq_hold(FM1_IRQ_TIMER5, 0);
}
#endif

#if FELUCCA_USB_AUDIO
/* the USB input track's frames (fx.c ut_mix, from mix_block): the playback ring's next n, the USB service held off for
 * the copy only (as ua_block); 0 none */
static __attribute__((noinline)) uint32_t ua_take(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t got = 0;
    fm1_irq_hold(FM1_IRQ_TIMER5, 1);
    if (usb.up && usb.config && !usb.suspended)
        got = ua_play_take(l, r, n);
    fm1_irq_hold(FM1_IRQ_TIMER5, 0);
    return got;
}
#endif

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
#if FELUCCA_USB_AUDIO
    track_capture_on = ua.cap_alt != 0;                 /* (the stems this block: fx.c capture_clear) */
#endif
#if FELUCCA_DUAL >= 2
    FAR(mix_block_dual)(out, n);                        /* (dual.c; RAM code) */
#else
    FAR(mix_block)(out, n);                             /* (RAM code: core.h HOT) */
#endif
#if FELUCCA_USB_AUDIO
    ua_block(out, n);                                   /* the stems out, the computer's audio in */
#endif
    for (i = 0; i < n; i++) {                           /* (no scope tap: SLOOP's HOME scope is gone) */
        out[2u * i] <<= OUT_SHIFT;
        out[2u * i + 1u] <<= OUT_SHIFT;
    }
}

/* overload: two halves in a row over 85 % of their time shed one voice before the next one (voice.c shed_voice,
 * SLOOP 2.3 d691ba7 after Felucca 1.0, unchanged in SLOOP 2.6): the quietest releasing voice, else the oldest held one
 * that is no part's bass or lead, fades out over the next block, so a dense chord on a heavy engine thins out from
 * the top instead of starving the CPU. (With CPU_GUARD, cpuguard.c decides when.) */
static volatile uint8_t shed_req;

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

/* how long after the DMA switched halves the interrupt began, us (PEAK counts it: a half is late when this wait and its render
 * pass the half's time, and the render's own time misses an interrupt held off): t0 against the lower envelope of the entries
 * one half apart, as sync_anchor but up to two halves off (further: a stall, taken as the new start, 0) */
static uint32_t entry_wait(uint32_t t0)
{
    static uint32_t ent;
    int32_t e;
    ent += (HALF_FRAMES * SY_TPS_Q8) >> 8;
    e = (int32_t)(t0 - ent);
    if (e < 0 || e > (int32_t)(2u * OVL_HALF_US * FM1_TICKS_PER_US)) {
        ent = t0;
        return 0;
    }
    ent += (uint32_t)e >> 4;
    return (uint32_t)e / FM1_TICKS_PER_US;
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    uint32_t t0 = fm1_ticks();
    fm1_audio_ack_aux(p);
    /* (no sti / cli here, with or without USB audio: see ua_block. Whether TIMER5, the USB service, priority 4,
     * preempts the render now rests on the core keeping the global enable through the interrupt's entry: not
     * measured on the FM-1; the emulator's model clears it, and its USB audio host test then misses packets) */
    felucca_dbg.in_audio = 1;
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us, late, a = sync_anchor(t0), w = entry_wait(t0);
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
        late = fm1_audio_free_half() != half;              /* the DMA moved on while we rendered: a drop-out */
        ovl_half(us + w, late, fm1_ms);                    /* PEAK (its wait too), DROPS, the header's mark (system/overload.h) */
#if FELUCCA_CPU_GUARD
        cg.late = (uint8_t)late;                           /* (a late half the guard reacts to: cpuguard.c cg_spike) */
        FAR(cg_post)(us);                                  /* the load: ease back, shed, or let go (cpuguard.c) */
#else
        {   /* two halves over 85 % in a row: a single late half (a USB burst, a flash write) sheds nothing */
            static uint8_t over;
            over = (uint8_t)(over << 1 | (us * 100u > (HALF_FRAMES * 1000000u / FS) * 85u));
            if ((over & 3u) == 3u)
                shed_req = 1;
        }
#endif
        song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
#if FELUCCA_DUAL >= 2
        dual_load_half();                                  /* CPU1's, the same way (dual.c, dual_load.h) */
        felucca_dbg.c1 = fm1_dual_mb.stage;                /* (kept by a reset; TIMER5 keeps it too, from IRQS on) */
#endif
        if (late)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
