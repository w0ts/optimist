/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The headphone jack's switch (measured on an FM-1 2026-10-29 with the GPIO probe, fm1-next-gpioprobe fe2f1ee9): PB5,
 * port B's input bit 5, 0 with headphones in, 1 with nothing plugged. Read only: its direction, pull and function are
 * left as the boot found them (input, enabled; no driver claims it). The speaker mutes itself in hardware on a plug;
 * here the reading picks the output's correction (fx/outcorr.c: SPK LOWCUT without, HP BASS with).
 * The contact bounces ~150 ms a plug (several flips within 50..150 ms): a new state counts once the pin has held it
 * HPJ_MS (hpj_step: one change a plug). Polled every main loop pass (ui/optimist/op_input.c ui_input: hp_poll).
 * fm1-base (the release): its plat_headphones, debounced there the same way (HPJ_PLAT, optimist/ui/opt_ui.c). */
#pragma once
#include <stdint.h>

#define HPJ_MS 100u                     /* the pin's state held this long before it counts */
typedef struct {
    uint8_t st, raw, on;                /* the state that counts (1 headphones in), the pin's last, read once */
    uint32_t t;                         /* when the pin took its last value (ms) */
} hpj_t;
/* one reading (raw: 1 headphones in) at ms -> the state that counts. The first reading counts at once (the power-on's) */
static inline uint32_t hpj_step(hpj_t *j, uint32_t raw, uint32_t ms)
{
    raw = raw != 0u;
    if (!j->on) {
        j->on = 1;
        j->st = j->raw = (uint8_t)raw;
        j->t = ms;
    } else if (raw != j->raw) {
        j->raw = (uint8_t)raw;
        j->t = ms;
    } else if (raw != j->st && ms - j->t >= HPJ_MS)
        j->st = (uint8_t)raw;
    return j->st;
}

#ifdef HPJ_PLAT                         /* (fm1-base: plat_headphones, 1 in, 0 none, -1 a board without the switch) */
static void hp_poll(void) { hp_in = (uint8_t)(HPJ_PLAT() > 0); }
#else
#ifdef __PI32V2__
#include "fm1_jack.h"                   /* (hal/: PB5 read) */
#define HPJ_RAW() fm1_hp_in()
#else
static volatile uint8_t hpj_host_in;    /* the host tests and the simulator: the jack as a test or sim_jack sets it (0 none) */
#define HPJ_RAW() hpj_host_in
#endif
static hpj_t hpj;
static void hp_poll(void)               /* (every main loop pass) */
{
    hp_in = (uint8_t)hpj_step(&hpj, HPJ_RAW(), fm1_ms);
}
#endif
