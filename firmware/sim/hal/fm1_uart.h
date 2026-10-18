/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's UART1 (firmware/hal/fm1_uart.h): no TRS MIDI IN; the ring stays as src/io/midi/midi_uart.c left it
 * (UM_EMPTY: nothing received). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

FM1_INLINE void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len) { (void)ring; (void)len; }
FM1_INLINE uint32_t fm1_uart1_rx_take(void) { return 0; }
