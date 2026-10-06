/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 input HAL: key/button/encoder matrix and LEDs.
 *
 * One 11-column x 6-row diode matrix behind a 2x74HC595 chain (PA4 SER, PA3
 * SRCLK, PA1 RCLK), bit-banged and polled; rows PA0, PA5..PA8, PB7 with
 * pull-ups (low = closed). LED lines PH6/PH9/PA9/PA10 light the LED on the
 * same column, row PA7/PA8/PA5/PA6 respectively.
 *
 *   fm1_input_init();
 *   polled:  for (;;) { fm1_input_scan(); ... }
 *   IRQ:     call fm1_input_tick() from a ~10 kHz timer ISR; it advances one
 *            column per call (rows are sampled one tick after the column was
 *            latched, LEDs stay lit in between) and processes a frame every
 *            FM1_NCOL ticks. The main loop reads fm1_in.notes / buttons and
 *            takes edges/steps with fm1_input_edges() / fm1_enc_take().
 *
 * fm1_input_scan() runs one full frame (11 columns, ~0.6 ms) and calls
 * FM1_INPUT_IDLE() while it waits.
 * Keys/buttons: integrating debounce, asymmetric: a press counts after FM1_PRESS closed
 * frames (~3 ms: notes are played in time), a release once the count is back to 0 (up to
 * FM1_DEBOUNCE frames), so contact bounce never retriggers.
 * Encoders: quadrature decoder (no filter, a skipped state continues the turn; X0X b637df3;
 * + = clockwise) with detent
 * counting: states an encoder rests in for >= FM1_REST_FRAMES are learned, and
 * a step is emitted on reaching a rest state after >= 2 net transitions, or
 * when a complementary rest state is first learned after a click.
 * One click = one step at any speed, whether a detent is a half or a full
 * quadrature cycle. fm1_enc_take() returns the steps.
 * LEDs: set fm1_led[col] (packed row bits, bit1 PA5..bit4 PA8); they are lit
 * while that column is selected; fm1_led_dim[col] the same, lit one frame in four
 * (dim). fm1_led_key/btn helpers address them by id.
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"

#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_LED_US
#define FM1_LED_US 40u           /* LED on-time per column (brightness vs scan rate) */
#endif
#define FM1_DEBOUNCE 8u           /* frames (~1.1 ms each in the IRQ scan): a release */
#define FM1_PRESS 3u              /* a press: fast (keys are played in time), still 3 frames sure */
#define FM1_SETTLE_US 10u
#define FM1_REST_FRAMES 40u       /* ~44 ms still (10 kHz / 11-column scan) = a detent position */
#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u


/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};
enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };

static const int8_t FM1_LED_PORT[6] = {-1, FM1_PA, FM1_PA, FM1_PH, FM1_PH, -1};
static const uint8_t FM1_LED_BIT[6] = {0, 9, 10, 6, 9, 0};

static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = button i (0..13) */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    uint8_t raw[FM1_NCOL];       /* last frame, packed rows, 1 = closed */
    uint8_t cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t enc_rest[FM1_NENC];  /* learned rest (detent) states, bit per state */
    uint8_t enc_still[FM1_NENC]; /* frames since the last state change */
    int8_t enc_sub[FM1_NENC];    /* net transitions since the last rest state */
    int8_t enc_dir[FM1_NENC];    /* the last valid transition: +1 / -1 (0: none yet) (X0X b637df3) */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    uint32_t frames;
} fm1_in;
static uint8_t fm1_led[FM1_NCOL];
#ifndef FM1_LED_DIM_MASK
#define FM1_LED_DIM_MASK 3u      /* dim LEDs: lit one scan frame in (mask + 1), ~225 Hz, no flicker */
#endif
static uint8_t fm1_led_dim[FM1_NCOL];   /* same layout as fm1_led: half-light marks */
#ifndef FELUCCA_LIGHTS
#define FELUCCA_LIGHTS 0
#endif
#if FELUCCA_LIGHTS
/* menu LIGHTS (FELUCCA_LIGHTS, from SLOOP 2.3 by isod89 after Felucca 1.0.1 #35, GPL-3.0): two dim layers timed
 * by TIMER4 instead of a frame in four (the eye is logarithmic: an LED lit 1/4 or 1/6 of the time reads as nearly
 * lit). A short pulse every frame (~910 Hz, no flicker) at the start of the column's next tick, riding on the
 * 595 shift of the next column (its outputs change only at the latch, so the pulse stays on column p and the key
 * read before it is unchanged). fm1_led_dim: the glow (landmarks), FM1_GLOW_NS; fm1_led_bg: the backlight
 * (menu LIGHTS), fm1_led_bg_ns. Only a pulse longer than the whole shift waits for the rest. An LED in several
 * layers takes the brightest. (fm1_input_scan, the polled path, keeps a frame in four.) */
#ifndef FM1_GLOW_NS
#define FM1_GLOW_NS 4000u        /* the glow pulse a frame (ns); a lit LED ~95 us: ~1/24 the brightness */
#endif
#define FM1__NS_T(ns) (((uint32_t)(ns) * FM1_TICKS_PER_US + 500u) / 1000u)   /* ns -> TIMER4 ticks */
static uint8_t fm1_led_bg[FM1_NCOL];    /* same layout: the backlight (labels readable in the dark) */
static volatile uint16_t fm1_led_bg_ns; /* the backlight pulse a frame (ns), 0 = off (menu LIGHTS) */
#endif

static void fm1__led_lines(uint32_t rowmask)
{
    uint32_t r;
    for (r = 1; r < 5u; r++) {
        if (rowmask & (1u << r))
            FM1_PR(FM1_LED_PORT[r], FM1_OUT) |= 1u << FM1_LED_BIT[r];
        else
            FM1_PR(FM1_LED_PORT[r], FM1_OUT) &= ~(1u << FM1_LED_BIT[r]);
    }
}

#if FELUCCA_LIGHTS
static void fm1__sr_bit(uint32_t w, uint32_t i)  /* bit i of w (msb first) into the 595 */
{
    if (w & (0x8000u >> i))
        FM1_PR(FM1_PA, FM1_OUT) |= 1u << 4;
    else
        FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 4);
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 3;         /* read-modify-write per edge: each SFR write is */
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 3);      /* far slower than the 595 needs */
}
static void fm1__sr_latch(void)                 /* the outputs change here only */
{
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
static void fm1__sr_word(uint32_t w)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        fm1__sr_bit(w, i);
    fm1__sr_latch();
}
#else
static void fm1__sr_word(uint32_t w)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        if (w & (0x8000u >> i))
            FM1_PR(FM1_PA, FM1_OUT) |= 1u << 4;
        else
            FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 4);
        FM1_PR(FM1_PA, FM1_OUT) |= 1u << 3;     /* each SFR write is far slower than the 595 needs */
        FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 3);
    }
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
#endif

static uint32_t fm1__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN), b = FM1_PR(FM1_PB, FM1_IN);
    return (~((a & 1u) | ((a >> 4) & 0x1Eu) | ((b >> 2) & 0x20u))) & 0x3Fu;
}

static void fm1__wait(uint32_t us)
{
    uint32_t t0 = fm1_ticks(), span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(fm1_ticks() - t0) < span)
        FM1_INPUT_IDLE();
}

static void fm1_input_init(void)
{
    static const uint8_t LEDP[4][2] = {{FM1_PH, 6}, {FM1_PH, 9}, {FM1_PA, 9}, {FM1_PA, 10}};
    const uint32_t rows_a = (1u << 0) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    const uint32_t sr = (1u << 1) | (1u << 3) | (1u << 4), row_b = 1u << 7;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t p = LEDP[i][0], m = 1u << LEDP[i][1];
        FM1_PR(p, FM1_DIE) |= m;
        FM1_PR(p, FM1_OUT) &= ~m;
        FM1_PR(p, FM1_DIR) &= ~m;
        FM1_PR(p, FM1_HD0) |= m;
        FM1_PR(p, FM1_HD) |= m;
    }
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PB, FM1_DIE) |= row_b;
    FM1_PR(FM1_PB, FM1_DIR) |= row_b;
    FM1_PR(FM1_PB, FM1_PD) &= ~row_b;
    FM1_PR(FM1_PB, FM1_PU) |= row_b;
    FM1_PR(FM1_PA, FM1_DIE) |= sr;
    FM1_PR(FM1_PA, FM1_PU) &= ~sr;
    FM1_PR(FM1_PA, FM1_PD) &= ~sr;
    FM1_PR(FM1_PA, FM1_OUT) &= ~sr;
    FM1_PR(FM1_PA, FM1_DIR) &= ~sr;
    fm1__sr_word(0xFFFFu);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;   /* seeded by the first frame */
}

#ifndef FELUCCA_KEYS_FAST
#define FELUCCA_KEYS_FAST 0
#endif
#if FELUCCA_KEYS_FAST
/* keys as SLOOP 2.3 / Felucca 1.0 read them (FELUCCA_KEYS_FAST): debounced as soon as their column is read
 * (fm1__keys), a press after FM1_DEB_PRESS samples closed in a row (the matrix has diodes and no ghosting; two
 * keep one stray sample from playing a note), a release after FM1_DEB_RELEASE open in a row (~9 ms): bounce on
 * the way down or chatter on the way up never ends a note early or plays it twice. About 1 ms sooner than the
 * integrating debounce at the frame's end (3 frames closed) */
#define FM1_DEB_PRESS 2u
#define FM1_DEB_RELEASE 8u
static void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t note = id >= 14u, bit = note ? 1u << (id - 14u) : 1u << id;
    uint32_t on = ((note ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
    if (closed == on) {                            /* agrees with the state: start over */
        *c = 0;
        return;
    }
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    if (note) {
        if (!on)
            fm1_in.notes_pressed |= bit;
        fm1_in.notes ^= bit;
    } else {
        fm1_in.buttons ^= bit;
        if (!on)
            fm1_in.pressed |= bit;
        else
            fm1_in.released |= bit;
    }
}
static void fm1__keys(uint32_t p)                  /* the keys of column p, just read */
{
    uint32_t r, raw = fm1_in.raw[p];
    for (r = 1; r < 5u; r++)
        if (FM1_KEYMAP[r][p] >= 0)
            fm1__key((uint32_t)FM1_KEYMAP[r][p], (raw >> r) & 1u);
}
#else
static void fm1__key(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[id];
    uint32_t on;
    if (closed) {
        if (*c < FM1_DEBOUNCE)
            (*c)++;
    } else if (*c) {
        (*c)--;
    }
    if (closed && *c >= FM1_PRESS)
        on = 1;
    else if (*c == 0)
        on = 0;
    else
        return;
    if (id >= 14u) {
        if (on) {
            if (!(fm1_in.notes & (1u << (id - 14u))))
                fm1_in.notes_pressed |= 1u << (id - 14u);
            fm1_in.notes |= 1u << (id - 14u);
        }
        else
            fm1_in.notes &= ~(1u << (id - 14u));
    } else if (on != ((fm1_in.buttons >> id) & 1u)) {
        fm1_in.buttons ^= 1u << id;
        if (on)
            fm1_in.pressed |= 1u << id;
        else
            fm1_in.released |= 1u << id;
    }
}
#endif

static void fm1__frame(void);

static void fm1_input_scan(void)
{
    uint32_t p;
    for (p = 0; p < FM1_NCOL; p++) {
        fm1__led_lines(0);
        fm1__sr_word(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
        fm1__wait(FM1_SETTLE_US);
        fm1_in.raw[p] = (uint8_t)fm1__rows();
#if FELUCCA_KEYS_FAST
        fm1__keys(p);
#endif
        fm1__led_lines(fm1_led[p] | ((fm1_in.frames & FM1_LED_DIM_MASK) ? 0u : fm1_led_dim[p]));
        fm1__wait(FM1_LED_US);
    }
    fm1__led_lines(0);
    fm1__frame();
}

static void fm1__frame(void)
{
#if FELUCCA_KEYS_FAST
    uint32_t e;                                    /* (the keys: fm1__keys, as each column is read) */
#else
    uint32_t p, r, e;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] >= 0)
                fm1__key((uint32_t)FM1_KEYMAP[r][p], (fm1_in.raw[p] >> r) & 1u);
#endif
    for (e = 0; e < FM1_NENC; e++) {               /* quadrature decoder + detents */
        const uint8_t *m = FM1_ENC[e];
        uint32_t cur = ((fm1_in.raw[m[0]] >> m[1]) & 1u) << 1 | ((fm1_in.raw[m[2]] >> m[3]) & 1u);
        uint32_t idx;
        volatile int8_t *sub = &fm1_in.enc_sub[e];
        /* No two-scan filter (X0X b637df3, Charles Vestal): a state had to be seen on two scans
         * running (2.2 ms) to count, so a fast turn, whose states last less than that, lost steps.
         * The 1.1 ms between scans is longer than a contact bounce, and a step still needs >= 2
         * net transitions between detents, so a glitch cannot make one. */
        if (cur != fm1_in.enc_last[e]) {
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {          /* first frame: the knob rests here */
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)(1u << cur);
        }
        if (fm1_in.enc_still[e] < 255u && ++fm1_in.enc_still[e] == FM1_REST_FRAMES) {
            /* learn detent states: one state, or a complementary pair (00/11 or
             * 01/10). Anything else restarts the set; with 3-4 rest states every
             * arrival would look like a detent with |sub| < 2. */
            uint32_t r = fm1_in.enc_rest[e], bit = 1u << cur, comp = 1u << (cur ^ 3u);
            if (!(r & bit)) {
                if (r == comp) {
                    fm1_in.enc_rest[e] = (uint8_t)(r | bit);
                    /* The first half-cycle has already arrived here. Without
                     * reporting it now, the first physical click is lost.
                     * (Melodee 6ec2deb, Kerem Kilic) */
                    if (*sub >= 2)
                        fm1_in.enc_steps[e]++;
                    else if (*sub <= -2)
                        fm1_in.enc_steps[e]--;
                } else {
                    fm1_in.enc_rest[e] = (uint8_t)bit;
                }
                *sub = 0;
            }
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
        idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
        if ((0x4182u >> idx) & 1u) {
            (*sub)++;
            fm1_in.enc_dir[e] = 1;
        } else if ((0x2814u >> idx) & 1u) {
            (*sub)--;
            fm1_in.enc_dir[e] = -1;
        } else {                                   /* both lines changed: a state skipped (a flick) */
            *sub = (int8_t)(*sub + 2 * fm1_in.enc_dir[e]);   /* two more the way it was going (X0X) */
        }
        fm1_in.enc_prev[e] = (uint8_t)cur;
        if ((fm1_in.enc_rest[e] >> cur) & 1u) {    /* back on a detent */
            if (*sub >= 2)
                fm1_in.enc_steps[e]++;
            else if (*sub <= -2)
                fm1_in.enc_steps[e]--;
            *sub = 0;
        }
    }
    fm1_in.frames++;
}

/* one column per call, from a timer ISR (see top) */
static uint8_t fm1__tick_col;
#if FELUCCA_LIGHTS
/* the dim pulses of column p ride on the shift of column n: the lines go lit | glow | backlight of p, each layer
 * ends when its time is up (TIMER4, checked after every bit), and the latch comes with the lines dark */
static void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u, i;
    uint32_t w = 0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u);
    uint32_t lit = fm1_led[p], a = fm1_led_dim[p] & ~lit, b = fm1_led_bg[p] & ~lit & ~a;
    uint32_t ta = a ? FM1__NS_T(FM1_GLOW_NS) : 0u, tb = b ? FM1__NS_T(fm1_led_bg_ns) : 0u;
    uint32_t tmax = ta > tb ? ta : tb;
    fm1__led_lines(0);
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick (the lines dark) */
    if (tmax) {
        uint32_t t0 = fm1_ticks(), cur = lit | a | (tb ? b : 0u), on, d;
        fm1__led_lines(cur);                       /* (the 595 still drives column p) */
        for (i = 0; i < 16u || cur; i++) {         /* the shift; then wait if the pulse is longer */
            if (i < 16u)
                fm1__sr_bit(w, i);
            d = fm1_ticks() - t0;
            on = d < tmax ? lit | (d < ta ? a : 0u) | (d < tb ? b : 0u) : 0u;
            if (on != cur) {
                fm1__led_lines(on);
                cur = on;
            }
        }
        fm1__sr_latch();                           /* column n, the lines dark */
    } else {
        fm1__sr_word(w);
    }
    fm1__led_lines(fm1_led[n]);
    fm1__tick_col = (uint8_t)n;
#if FELUCCA_KEYS_FAST
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
#endif
    if (n == 0u)
        fm1__frame();
}
#else
static void fm1_input_tick(void)
{
    uint32_t p = fm1__tick_col, n = p + 1u == FM1_NCOL ? 0u : p + 1u;
    fm1__led_lines(0);
    fm1_in.raw[p] = (uint8_t)fm1__rows();          /* column p has been latched one tick */
    fm1__sr_word(0xFFFFu ^ (1u << n) ^ (n < 2u ? 1u << (11u + n) : 0u));
    fm1__led_lines(fm1_led[n] | ((fm1_in.frames & FM1_LED_DIM_MASK) ? 0u : fm1_led_dim[n]));
    fm1__tick_col = (uint8_t)n;
#if FELUCCA_KEYS_FAST
    fm1__keys(p);                                  /* its keys now: no wait for the frame's end */
#endif
    if (n == 0u)
        fm1__frame();
}
#endif

/* main-loop critical section against fm1_input_tick (main loop only: it
 * re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* detent steps turned since the last call, + = clockwise */
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

/* LED of key id (button 0..13 or note key 14..40) */
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
