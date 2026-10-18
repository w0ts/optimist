/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's ALNK0 (firmware/hal/fm1_audio.h): a model of the double-buffered DMA. At each half's end (the
 * scheduler, sim.c sim_audio_switch) the half just played goes to the output, the DMA moves to the other half, the
 * one it left is free and the HALF pending is set; the handler renders into it as on the FM-1. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "fm1_irq.h"
#include "fm1_gpio.h"

#define FM1_AUDIO_HALF 0x80u      /* CON2 pending: a half buffer is free */

static struct {
    int32_t *buf;                 /* the two halves (L, R int32, 24-bit) */
    uint32_t half_words;
    uint8_t on;                   /* the DMA runs */
    uint8_t playing;              /* the half the DMA reads (CON0 bit 15) */
    uint8_t con2;                 /* pendings */
} sim_alnk;

FM1_INLINE void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    fm1_delay_ms(5);
    sim_alnk.buf = buf;
    sim_alnk.half_words = half_words;
    sim_alnk.playing = 0;
    sim_alnk.con2 = 0;
    fm1_irq_attach(FM1_IRQ_ALNK0, isr, prio);
    sim_alnk.on = 1;
    fm1_delay_ms(5);
}

FM1_INLINE uint8_t fm1_audio_pending(void) { return sim_alnk.con2; }
FM1_INLINE void fm1_audio_ack_aux(uint8_t p) { sim_alnk.con2 &= (uint8_t)~(p & 0x70u); }
FM1_INLINE uint32_t fm1_audio_free_half(void) { return sim_alnk.playing ^ 1u; }
FM1_INLINE void fm1_audio_ack_half(void) { sim_alnk.con2 &= (uint8_t)~FM1_AUDIO_HALF; }
FM1_INLINE void fm1_audio_sync(void) {}
FM1_INLINE void fm1_audio_stop(void) { sim_alnk.on = 0; }
