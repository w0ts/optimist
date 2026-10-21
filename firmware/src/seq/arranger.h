/* SPDX-License-Identifier: GPL-3.0-only */
/* Song order, independent of UI/flash/audio drivers. Durations are 4/4 bars.
 * Audio calls next BEFORE rendering a block, then elapse AFTER it. The clock
 * accumulates samples * BPM, keeping the remainder at every bar boundary. */
#ifndef FM1_ARRANGER_H
#define FM1_ARRANGER_H
#include <stdint.h>
/* the chain's parts: 16 in the settings record (ARR_REC_STEPS, its layout kept: older builds read it); with the
 * sections in the log (FELUCCA_SECTIONS 8 / 16), 64, the whole chain also in the log (sections.c sec_song_put) */
#define ARR_REC_STEPS 16u
#if defined(FELUCCA_SECTIONS) && FELUCCA_SECTIONS > 4
#define ARR_STEPS 64u
#else
#define ARR_STEPS ARR_REC_STEPS
#endif
#ifdef FELUCCA_SECTIONS
#define ARR_SCENES ((uint32_t)FELUCCA_SECTIONS)   /* (registry.h: A..D, A..H or A..P) */
#else
#define ARR_SCENES 4u
#endif
#define ARR_NONE (-1)
#define ARR_DONE (-2)
#define ARR_INVALID (-3)
#define ARR_BARS_A 255u                                    /* a part's BARS: A, the scene's longest pattern whatever its own BARS says */
typedef struct { uint8_t scene, bars; } arr_entry_t;   /* bars 0: S, follows the scene's BARS (auto_bars below), 1..64, ARR_BARS_A */
typedef struct {
    uint8_t count, loop, reserved[2];
    arr_entry_t entry[ARR_STEPS];
} arr_config_t;
typedef struct {                                   /* the settings record's chain (project.c persist_t) */
    uint8_t count, loop, tag[2];                   /* tag: the log's whole chain that goes with it (0: none) */
    arr_entry_t entry[ARR_REC_STEPS];
} arr_rec_t;
typedef struct {
    uint32_t phase;
    uint8_t running, index, bar, error;
    uint8_t jump, jump_now;                        /* a jump asked for by the UI: to part jump - 1, at the next bar, or (jump_now) at the next block */
    uint8_t auto_bars;                             /* the bars of the part playing when its entry says S: its scene's BARS, AUTO its longest pattern; set when the scene is applied (0: not known, 4) */
    uint8_t pat_bars;                              /* its scene's longest pattern, in bars: the entry's A (0: not known, 4) */
} arr_clock_t;

static void arr_defaults(arr_config_t *c)
{
    uint32_t i;
    c->count = 4; c->loop = 0; c->reserved[0] = c->reserved[1] = 0;
    for (i = 0; i < ARR_STEPS; i++) {
        c->entry[i].scene = (uint8_t)(i % ARR_SCENES);
        c->entry[i].bars = 0;                      /* (AUTO) */
    }
}
static int arr_valid(const arr_config_t *c, uint32_t ready)
{
    uint32_t i;
    if (!c->count || c->count > ARR_STEPS || c->loop > 1u) return 0;
    for (i = 0; i < c->count; i++) {
        const arr_entry_t *e = &c->entry[i];
        if (e->scene >= ARR_SCENES || (e->bars > 64u && e->bars != ARR_BARS_A) ||
            !(ready & (1u << e->scene))) return 0;
    }
    return 1;
}
/* a chain read back from the settings record: every part one of this build's sections (whether stored or not
 * is checked when the song starts) */
static int arr_stored_ok(const arr_config_t *c)
{
    return arr_valid(c, (uint32_t)((1ull << ARR_SCENES) - 1u));
}
/* the settings record's chain <-> the chain: past 16 parts the record keeps the first 16 (an older build plays
 * them), the tag names the whole chain in the log */
static void arr_from_rec(arr_config_t *c, const arr_rec_t *r)
{
    uint32_t i;
    arr_defaults(c);
    c->count = r->count;
    c->loop = r->loop;
    for (i = 0; i < ARR_REC_STEPS; i++)
        c->entry[i] = r->entry[i];
}
static void arr_to_rec(arr_rec_t *r, const arr_config_t *c, uint32_t tag)
{
    uint32_t i;
    r->count = (uint8_t)(c->count > ARR_REC_STEPS ? ARR_REC_STEPS : c->count);
    r->loop = c->loop;
    r->tag[0] = (uint8_t)tag;
    r->tag[1] = (uint8_t)(tag >> 8);
    for (i = 0; i < ARR_REC_STEPS; i++)
        r->entry[i] = c->entry[i];
}
static uint32_t arr_tag_of(const arr_rec_t *r) { return r->tag[0] | (uint32_t)r->tag[1] << 8; }
/* the bars the part of entry e plays (the clock's current one): a number, S its scene's BARS (auto_bars), A its longest
 * pattern (pat_bars); 4 where the scene's length is not known */
static uint32_t arr_eff_bars(const arr_entry_t *e, const arr_clock_t *r)
{
    return e->bars == ARR_BARS_A ? (r->pat_bars ? r->pat_bars : 4u) : e->bars ? e->bars : r->auto_bars ? r->auto_bars : 4u;
}
static int arr_begin(arr_clock_t *r, const arr_config_t *c, uint32_t ready)
{
    r->phase = 0; r->index = 0; r->bar = 0; r->running = 0; r->auto_bars = r->pat_bars = 0; r->jump = r->jump_now = 0;
    r->error = (uint8_t)!arr_valid(c, ready);
    if (r->error) return ARR_INVALID;
    r->running = 1;
    return c->entry[0].scene;
}
static int arr_next(arr_clock_t *r, const arr_config_t *c, uint32_t sample_rate)
{
    int result = ARR_NONE;
    uint32_t period = sample_rate * 240u;
    if (!r->running || !period) return ARR_NONE;
    if (r->jump && r->jump_now) {                  /* (SHIFT2 + a part key: now, the part from its first bar) */
        uint32_t j = (uint32_t)r->jump - 1u;
        r->jump = r->jump_now = 0;
        if (j < c->count) {
            r->index = (uint8_t)j; r->bar = 0; r->phase = 0;
            return c->entry[j].scene;
        }
    }
    while (r->phase >= period) {
        r->phase -= period;
        if (r->jump) {                             /* (a part key: at this bar line) */
            uint32_t j = (uint32_t)r->jump - 1u;
            r->jump = r->jump_now = 0;
            if (j < c->count) {
                r->index = (uint8_t)j; r->bar = 0;
                result = c->entry[j].scene;
                continue;
            }
        }
        uint32_t bars = arr_eff_bars(&c->entry[r->index], r);
        if (++r->bar < bars) continue;
        r->bar = 0;
        if (++r->index >= c->count) {
            if (!c->loop) { r->running = 0; return ARR_DONE; }
            r->index = 0;
        }
        result = c->entry[r->index].scene;
    }
    return result;
}
static void arr_elapse(arr_clock_t *r, uint32_t samples, uint32_t bpm)
{
    if (r->running) r->phase += samples * bpm;
}
#endif
