/* SPDX-License-Identifier: GPL-3.0-only */
/* Novation RGB Launchpads in Programmer mode (jank_lpx.h). */
#include "jank_lpx.h"
#include "jank_lpstr.h"

#define CIN_NOTE_ON 0x9u
#define CIN_CC 0xBu
#define CIN_SYSEX_CONT 0x4u            /* a SysEx packet carrying three bytes, more to come; 5 / 6 / 7 end with 1 / 2 / 3 */
#define CIN_SYSEX_END3 0x7u
#define UNKNOWN 0xFFu                  /* a light the Launchpad may show anything on */

/* X and Mini MK3: Programmer mode (0Eh 1); then what Launchpad 95 sends (Launchpad.py handle_sysex), without which the X takes the mode and every
 * light message and shows nothing (measured 2026-11-24): LED feedback internal off, external on (0Ah 0 1), sleep off (09h 1) */
static const uint8_t PROG[] = {2, 0x0E, 0x01, 0}, PROG_SETUP[] = {3, 0x0A, 0x00, 0x01, 2, 0x09, 0x01, 0}, PROG_QUERY[] = {1, 0x0E, 0};

#define X_MODEL {"Launchpad X", NULL, 0x0C, 1, 91, 0, 1, 0, 1, 0x0E, 1, PROG, PROG_SETUP, PROG_QUERY}
#if JLX_ONLY_X
const struct jlx_model jlx_models[JLX_MODELS] = {X_MODEL};
static const struct jlx_model *const ONLY_X = &jlx_models[0];
#define LPX_MODEL(m) ((void)(m), ONLY_X)                       /* the X alone (jank_lpunit.h): every model field a constant, the other models' paths compiled away */
#else
/* MK2: Session layout (22h 0), the numbers above; it answers nothing */
static const uint8_t MK2_START[] = {2, 0x22, 0x00, 0};
/* Pro: Standalone mode (21h 1), Programmer layout (2Ch 3), then which layout is it in (2Fh: it answers 2Fh <layout>) */
static const uint8_t PRO_START[] = {2, 0x21, 0x01, 2, 0x2C, 0x03, 1, 0x2F, 0}, PRO_QUERY[] = {1, 0x2F, 0};
const struct jlx_model jlx_models[JLX_MODELS] = {
    X_MODEL,
    {"Launchpad Mini MK3", NULL, 0x0D, 1, 91, 0, 1, 0, 0, 0x0E, 1, PROG, PROG_SETUP, PROG_QUERY},
    {"Launchpad MK2", NULL, 0x18, 0, 104, 1, 0, 0, 0, 0, 0, MK2_START, NULL, NULL},
    {"Launchpad Pro", "MK3", 0x10, 1, 91, 0, 1, 1, 1, 0x2F, 3, PRO_START, NULL, PRO_QUERY},
};
#define LPX_MODEL(m) (m)
#endif

int jlx_has(const char *s, const char *part)
{
    for (;; s++) {
        size_t i = 0;
        while (part[i] && s[i] == part[i])
            i++;
        if (!part[i])
            return 1;
        if (!*s)
            return 0;
    }
}

const struct jlx_model *jlx_model_find(const char *product)
{
    for (size_t i = 0; product && i < JLX_MODELS; i++)
        if (jlx_has(product, jlx_models[i].name) && !(jlx_models[i].not_name && jlx_has(product, jlx_models[i].not_name)))
            return &jlx_models[i];
    return NULL;
}

/* one SysEx, F0 00 20 29 02 <dev> body F7, as packets: 3 bytes each, the last one's code says how many it carries */
static size_t sysex(const struct jlx_model *m, const uint8_t *body, size_t len, uint8_t *out)
{
    uint8_t sx[16] = {0xF0, 0x00, 0x20, 0x29, 0x02, LPX_MODEL(m)->dev};
    size_t n = 6u + len + 1u, w = 0;
    memcpy(sx + 6, body, len);
    sx[n - 1u] = 0xF7;
    for (size_t k = 0; k < n; k += 3u, w += 4u) {
        size_t left = n - k < 3u ? n - k : 3u;
        if (!out)
            continue;
        out[w] = (uint8_t)((LPX_MODEL(m)->cable << 4) | (n - k > 3u ? CIN_SYSEX_CONT : 4u + left));
        for (size_t b = 0; b < 3u; b++)
            out[w + 1u + b] = b < left ? sx[k + b] : 0u;
    }
    return w;
}

size_t jlx_sysex(const struct jlx_model *m, const uint8_t *list, uint8_t *out)
{
    size_t w = 0;
    for (; list && *list; list += 1u + *list)
        w += sysex(m, list + 1, *list, out ? out + w : NULL);
    return w;
}

int jlx_programmer_ack(const struct jlx_model *m, const uint8_t pkt[4], int *mode)
{
    if (!LPX_MODEL(m)->ack || (pkt[0] & 0x0Fu) != CIN_SYSEX_END3 || pkt[1] != LPX_MODEL(m)->ack || pkt[3] != 0xF7)
        return 0;
    if (mode)
        *mode = pkt[2] == LPX_MODEL(m)->ack_on;
    return 1;
}

uint8_t jlx_number(const struct jlx_model *m, uint32_t i)
{
    if (i < JLX_TOP0)
        return (uint8_t)(10u * (i / 8u + 1u) + i % 8u + 1u);
    if (i < JLX_SIDE0)
        return (uint8_t)(LPX_MODEL(m)->top + (i - JLX_TOP0));
    if (i < JLX_LOGO)
        return (uint8_t)(89u - 10u * (i - JLX_SIDE0));
    return 99u;
}

int jlx_decode(const uint8_t msg[3], struct jlx_ev *ev)
{
    uint8_t kind = (uint8_t)(msg[0] & 0xF0u), n = msg[1], row = (uint8_t)(n / 10u - 1u), col = (uint8_t)(n % 10u - 1u);   /* 0-based; 255 for a 0 */
    int note = kind == 0x80u || kind == 0x90u;
    memset(ev, 0, sizeof *ev);
    if (!note && kind != 0xB0u)
        return 0;
    ev->value = kind == 0x80u ? 0u : msg[2];
    if (row < 8u && col < 8u && note) {
        ev->kind = JLX_PAD;
        ev->row = row;
        ev->col = col;
    } else if (row < 8u && col == 8u) {   /* a CC, or the MK2's Note */
        ev->kind = JLX_SIDE;
        ev->idx = (uint8_t)(7u - row);
    } else if (!note && (uint8_t)(n - 91u) < 8u) {
        ev->kind = JLX_TOP;
        ev->idx = (uint8_t)(n - 91u);
    } else if (!JLX_ONLY_X && !note && (uint8_t)(n - 104u) < 8u) {   /* the MK2's */
        ev->kind = JLX_TOP;
        ev->idx = (uint8_t)(n - 104u);
    } else {
        return 0;
    }
    return 1;
}

void jlx_forget(struct jlx_frame *shown)
{
    memset(shown, UNKNOWN, sizeof *shown);
}

static int same(const struct jlx_led *a, const struct jlx_led *b)
{
    return a->mode == b->mode && a->color == b->color && (a->mode != JLX_FLASH || a->alt == b->alt);
}

/* one colour message: channel 1 static, 2 flashing, 3 pulsing; a Note On for a pad (and the MK2's side buttons), else a Control Change */
static uint8_t *put(const struct jlx_model *m, uint8_t *out, uint32_t i, uint8_t channel, uint8_t color)
{
    int note = i < JLX_TOP0 || (LPX_MODEL(m)->side_note && i >= JLX_SIDE0 && i < JLX_LOGO);
    *out++ = (uint8_t)((LPX_MODEL(m)->cable << 4) | (note ? CIN_NOTE_ON : CIN_CC));
    *out++ = (uint8_t)((note ? 0x90u : 0xB0u) | channel);
    *out++ = jlx_number(m, i);
    *out++ = color;
    return out;
}

/* the packets of light w at i: by SysEx (the Pro), A static then 23h (flash to B) or off then 28h (pulse, from dark as the others do) */
static size_t light(const struct jlx_model *m, uint32_t i, const struct jlx_led *w, uint8_t *out)
{
    uint8_t *p = out;
    if (w->mode == JLX_STATIC) {
        p = put(m, p, i, 0, w->color);
    } else if (LPX_MODEL(m)->sysex_flash) {
        const uint8_t body[3] = {(uint8_t)(w->mode == JLX_FLASH ? 0x23u : 0x28u), jlx_number(m, i), w->color};
        p = put(m, p, i, 0, w->mode == JLX_FLASH ? w->alt : 0u);
        p += sysex(m, body, sizeof body, p);
    } else if (w->mode == JLX_FLASH) {
        p = put(m, put(m, p, i, 0, w->alt), i, 1, w->color);
    } else {
        p = put(m, p, i, 2, w->color);
    }
    return (size_t)(p - out);
}

size_t jlx_diff(const struct jlx_model *m, struct jlx_frame *shown, const struct jlx_frame *want, uint8_t *out, size_t max_packets)
{
    uint8_t one[4 * JLX_LIGHT_MAX];
    size_t n = 0;
    for (uint32_t i = 0; i < JLX_LEDS; i++) {
        const struct jlx_led *w = &want->led[i];
        size_t need;
        if (same(&shown->led[i], w) || (i == JLX_LOGO && !LPX_MODEL(m)->logo))
            continue;
        need = light(m, i, w, one) / 4u;
        if (n + need > max_packets)    /* in order: a later light does not jump over one that waits for room */
            break;
        memcpy(out + 4u * n, one, 4u * need);
        n += need;
        shown->led[i] = *w;
    }
    return n;
}
