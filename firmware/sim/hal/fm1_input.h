/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's key / button / encoder matrix and LEDs (firmware/hal/fm1_input.h): the same fm1_in, ids, tables and
 * calls, fed by the page instead of the matrix. sim_key / sim_button queue a press or release (sim.c sim_in_push);
 * the TIMER5 tick takes them a frame (FM1_NCOL ticks, ~1.1 ms) at a time, at most one change per key a frame, so a
 * press and its release sent together are both seen (no debounce: the page's events are clean). sim_enc adds detent
 * steps at once. The LEDs: fm1_led / fm1_led_dim as the UI sets them (sim.c sim_leds reads them). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u
#endif
#define FM1_SETTLE_US 10u
#define FM1_REST_FRAMES 900u
#define FM1_ENC_GO 20u
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u

static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };

static volatile struct {
    uint32_t notes;              /* bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];
    uint8_t cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t enc_sub[FM1_NENC];
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    int8_t enc_dir[FM1_NENC];
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];
#ifndef FM1_LED_DIM_MASK
#if defined(FELUCCA_UI) && FELUCCA_UI == 1
#define FM1_LED_DIM_MASK 7u
#else
#define FM1_LED_DIM_MASK 3u
#endif
#endif
static uint8_t fm1_led_dim[FM1_NCOL];
#ifndef FELUCCA_LIGHTS
#define FELUCCA_LIGHTS 0
#endif
#if FELUCCA_LIGHTS
#ifndef FM1_GLOW_NS
#define FM1_GLOW_NS 4000u
#endif
static uint8_t fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
#endif

/* the page's presses and releases, in order: id (0..40) | down << 7 */
#define SIM_INQ 128u
static uint8_t sim_inq[SIM_INQ];
static uint32_t sim_inq_w, sim_inq_r;

static uint32_t fm1_input_oct_raw(void) { return 0; }

static void fm1_input_init(void)
{
    uint32_t i;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
}

static void fm1__key(uint32_t id, uint32_t down)
{
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (down == on)
        return;
    if (note) {
        if (down)
            fm1_in.notes_pressed |= bit;
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (down)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}

static void fm1__frame(void)                      /* the queued changes, one per key a frame */
{
    uint64_t seen = 0;
    while (sim_inq_r != sim_inq_w) {
        uint32_t e = sim_inq[sim_inq_r % SIM_INQ], id = e & 0x7Fu;
        if (id >= FM1_NKEY) {
            sim_inq_r++;
            continue;
        }
        if ((seen >> id) & 1u)
            break;
        seen |= (uint64_t)1 << id;
        fm1__key(id, e >> 7);
        sim_inq_r++;
    }
    fm1_in.frames++;
}

static uint8_t fm1__tick_col;
static void fm1_input_tick(void)
{
    if (++fm1__tick_col >= FM1_NCOL) {
        fm1__tick_col = 0;
        fm1__frame();
    }
}

static void fm1_input_scan(void) { fm1__frame(); }

static inline uint32_t fm1__lock(void) { uint32_t ie = sim_ie; fm1_irq_off(); return ie; }
static inline void fm1__unlock(uint32_t v) { if (v) fm1_irq_on(); }

static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}
