/* SPDX-License-Identifier: GPL-3.0-only */
/* Novation RGB Launchpads in Programmer mode (or the MK2's Session layout): what their pads and buttons send, and the 4-byte USB-MIDI event
 * packets that light them. Pure: no clock, no USB stack, host-tested (tests/test_lpx.c).
 *
 * The X, Mini MK3, MK2 and Pro (MK1) are one surface (their programmer's reference manuals, Novation/Focusrite): a pad's number is 10 x row +
 * column (rows 1..8 from the bottom, columns 1..8), the 8 side buttons are 89 (top) down to 19 (bottom), and a colour is the velocity (value)
 * of a Note On (pads) or a CC (buttons) and an index in the same 128-colour palette (the four manuals' colour tables: the same colour at every
 * index, 0 off). What differs is in struct jlx_model: the SysEx header's device byte, the USB-MIDI cable that takes lights, the top buttons'
 * numbers (CC 91..98, the MK2 CC 104..111), the MK2's side buttons (Notes, not CCs), the logo (CC 99; the MK2 has none, the Pro's is its side
 * LED), how a light flashes or pulses (MIDI channel 2 / 3; the Pro in Programmer layout by SysEx 23h / 28h), whether the pads send velocity
 * (the Mini MK3 and the MK2 send 127), and the SysEx that brings each into the mode. The Pro's 16 other buttons (left column CC 10..80, bottom
 * row CC 1..8) are not read (owner 2026-11-26: like an X at first). Flashing flips between the colour set static (A) and the flashing one (B),
 * so a flash is sent as its static A, then B. */
#ifndef JANK_LPX_H
#define JANK_LPX_H
#include <stddef.h>
#include <stdint.h>
#include "jank_lpunit.h"

/* the lights, in one list: pads row * 8 + column (row 0 = bottom), then the buttons */
#define JLX_PAD0 0u
#define JLX_TOP0 64u                   /* 8 top buttons, left to right: up, down, left, right, then 4 that each model names its own way */
#define JLX_SIDE0 72u                  /* 8 side buttons, top (scene 1) to bottom (scene 8) */
#define JLX_LOGO 80u
#define JLX_LEDS 81u

struct jlx_model {
    const char *name;                  /* its USB product name contains this ... */
    const char *not_name;              /* ... and not this (NULL: no such rule): "Launchpad Pro" is not the Pro MK3, another protocol */
    uint8_t dev;                       /* F0 00 20 29 02 <dev>: X 0Ch, Mini MK3 0Dh, MK2 18h, Pro 10h */
    uint8_t cable;                     /* the cable of its port for lights: the X's and Mini MK3's "MIDI", the Pro's "Standalone"; the MK2 has one */
    uint8_t top;                       /* the top buttons' first CC number */
    uint8_t side_note;                 /* 1: the side buttons send and take Notes */
    uint8_t logo;                      /* 1: a logo light at CC 99 */
    uint8_t sysex_flash;               /* 1: flash and pulse by SysEx (23h, 28h) */
    uint8_t velocity;                  /* 1: the pads send how hard they are hit */
    uint8_t ack, ack_on;               /* its answer to start / query ends `ack <value> F7`, ready when value == ack_on; ack 0: it answers nothing */
    const uint8_t *start;              /* the SysEx bodies (after the header) that bring it into the mode: <length> <bytes> ..., 0 ends */
    const uint8_t *setup;              /* ... sent once it answered (NULL: none) */
    const uint8_t *query;              /* ... asking which mode it is in (NULL: none) */
};

#define JLX_MODELS (JLX_ONLY_X ? 1u : 4u)   /* jank_lpunit.h JLX_ONLY_X: the X alone */
extern const struct jlx_model jlx_models[JLX_MODELS];   /* X first: the one jank was made with */

/* the model whose name the USB product name carries; NULL: not a Launchpad jank drives */
const struct jlx_model *jlx_model_find(const char *product);
/* `part` is in s (strstr, without the C library: the FM-1 has none) */
int jlx_has(const char *s, const char *part);

/* the SysEx messages of a list (m->start, m->setup, m->query) as packets on m->cable into out; out NULL: only counted. Returns the bytes (4 per
 * packet); 0 for a NULL list */
size_t jlx_sysex(const struct jlx_model *m, const uint8_t *list, uint8_t *out);

/* 1: pkt is the last packet of its answer to start / query (`ack value F7`, any cable); *mode (may be NULL) is 1 when it is in the mode, else 0.
 * The X answers the Programmer mode message with the same message (measured 2026-11-24: 07 0E 01 F7) */
int jlx_programmer_ack(const struct jlx_model *m, const uint8_t pkt[4], int *mode);

enum jlx_kind { JLX_NONE, JLX_PAD, JLX_TOP, JLX_SIDE };

struct jlx_ev {
    uint8_t kind;                      /* enum jlx_kind */
    uint8_t row, col;                  /* JLX_PAD: 0..7, row 0 = bottom */
    uint8_t idx;                       /* JLX_TOP, JLX_SIDE: 0..7 as above */
    uint8_t value;                     /* 0 = released, else the velocity (a button: 127) */
};

/* msg = the three bytes of a Note On / Note Off / Control Change (any channel), from any of the models (the MK2's top CC 104..111 and side Notes
 * included). 1 and *ev filled: a pad or a button; 0: not one */
int jlx_decode(const uint8_t msg[3], struct jlx_ev *ev);

/* the number (note, or CC) of light i (< JLX_LEDS) on model m */
uint8_t jlx_number(const struct jlx_model *m, uint32_t i);

enum jlx_mode { JLX_STATIC, JLX_FLASH, JLX_PULSE };

struct jlx_led {
    uint8_t color;                     /* the palette index, 0 off; for JLX_FLASH the flashing colour (B) */
    uint8_t alt;                       /* JLX_FLASH only: the static colour it flips with (A) */
    uint8_t mode;                      /* enum jlx_mode */
};

struct jlx_frame {
    struct jlx_led led[JLX_LEDS];
};

/* marks every light of `shown` as not known: the next diff sends them all (after a Programmer mode switch or a reconnect) */
void jlx_forget(struct jlx_frame *shown);

/* writes the packets that bring model m from `shown` to `want` (a light that is the same sends nothing) into out, at most max_packets, and
 * updates `shown` for the lights it sent only; returns the packets written. A light takes 1, a flash 2 (by SysEx: a pulse 5, a flash 5). The rest
 * goes out in the next call. */
#define JLX_LIGHT_MAX 5u
size_t jlx_diff(const struct jlx_model *m, struct jlx_frame *shown, const struct jlx_frame *want, uint8_t *out, size_t max_packets);
#endif
