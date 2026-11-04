/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The chip's CRC block (WL82.h JL_CRC: FIFO, REG at 0x13500): CRC-16/XMODEM, poly 0x1021 init 0, the SD card's data CRC. The SDK's
 * CRC16 (cpu.a crc16.c): REG = 0, each byte written to FIFO, csync, REG read; in an interrupt it falls back to its crc16_xmodem(.., 0),
 * so the two are the same sum. One user at a time: main loop only here; the SDK's own CRC16 takes a mutex and never uses the block in an interrupt. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_CRC_FIFO (*(volatile uint32_t *)0x13500u)
#define FM1_CRC_REG (*(volatile uint32_t *)0x13504u)

FM1_INLINE void fm1_crc_start(void) { FM1_CRC_REG = 0; }
FM1_INLINE void fm1_crc_byte(uint8_t b) { FM1_CRC_FIFO = b; }
FM1_INLINE uint16_t fm1_crc_end(void)
{
    __asm__ volatile("csync" ::: "memory");
    return (uint16_t)FM1_CRC_REG;
}
static uint16_t fm1_crc16(const uint8_t *p, uint32_t n)   /* (from XIP: 20 us a block, measured; RAM code gains nothing here) */
{
    fm1_crc_start();
    while (n--)
        fm1_crc_byte(*p++);
    return fm1_crc_end();
}
