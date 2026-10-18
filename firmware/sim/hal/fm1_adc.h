/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's SARADC (firmware/hal/fm1_adc.h): MASTER is what the page set (sim_master), the battery reads full. */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define SIM_BATT_FULL 600                         /* (ui_draw.c / op_project.c: >= 591 is three bars) */

static int32_t sim_master_pot = 768;              /* 0..1023 (sim_master) */

static void fm1_adc_init(void) {}

static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return sim_master_pot;
    if (ch == FM1_ADC_BATT)
        return SIM_BATT_FULL;
    return -1;
}
