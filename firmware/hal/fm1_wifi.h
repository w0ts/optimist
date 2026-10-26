/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) Wi-Fi MAC (docs/WIFI-HW-FACTS.md §2): JieLi's "wl30" core (MAC_CSR0 reads 0x00300100 on
 * the FM-1) with Ralink's RT3070 register layout; register r (Ralink's numbering: MAC_CSR0 0x1000, BBP_CSR_CFG 0x101C
 * ...) is plain MMIO at 0x30000 + r, as the JieLi AC79 SDK reaches it (wl_sdio.c wl30_mmc_io_rw_extended, Apache-2.0).
 * The frame DMA is at 0x30F00-0x30F1C, the Wi-Fi clock / reset block at 0x14040-0x1405C. Used by the console's
 * wifiregs (read only) and wifirx (one RX DMA fetch), with the radio started: the BT start-up clocks the block, and
 * with it unclocked an access may not answer. */
#pragma once
#include <stdint.h>
#include "fm1_sys.h"
#include "fm1_time.h"
#include "fm1_wifi_wlc.h"

#define FM1_WIFI_MAC 0x30000u            /* MAC register r at FM1_WIFI_MAC + r */
#define FM1_WIFI_WDMA0 0x30F00u          /* frame DMA control (the SDK's wdma_init) */
#define FM1_WIFI_WDMA1 0x30F04u          /* .. status: bit 20 TX done, bit 21 RX done */
#define FM1_WIFI_CLK0 0x14040u           /* the clock / reset block, 0x14040-0x1405C (wf_clk_rst_init) */
#define FM1_WIFI_CLK_END 0x1405Cu

#define FM1_WIFI_RX_PEND (1u << 17)      /* 0x30F04: a received frame waits (the SDK's sdio_recv_int_hdl) */
#define FM1_WIFI_RX_DONE (1u << 21)      /* 0x30F04: an RX DMA transfer finished */
#define FM1_WIFI_RX_CLR (1u << 11)       /* 0x30F04: written before / after each RX transfer */
#define FM1_WIFI_RX_ADDR 0x30F18u        /* RX DMA: the buffer */
#define FM1_WIFI_RX_LEN 0x30F1Cu         /* .. and the byte count */
#define FM1_WIFI_RX_ACK 0x324u           /* MAC register written 2 before a frame is fetched (sdio_recv_int_hdl) */
#define FM1_WIFI_RX_MAX 4088u            /* the SDK takes a length below 4089 */

FM1_INLINE uint32_t fm1_wifi_mac_rd(uint32_t r) { return *(const volatile uint32_t *)(uintptr_t)(FM1_WIFI_MAC + r); }
FM1_INLINE void fm1_wifi_mac_wr(uint32_t r, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(FM1_WIFI_MAC + r) = v; }
FM1_INLINE uint32_t fm1_wifi_rd(uint32_t a) { return *(const volatile uint32_t *)(uintptr_t)a; }   /* the DMA / clock words */

#define FM1_WIFI_RX_FILTER 0x1400u       /* MAC: Ralink's RX_FILTR_CFG (bit n set = drop that kind) */
#define FM1_WIFI_RX_FILTER_ALL 0x1u      /* .. drop only frames with a bad CRC */
#define FM1_WIFI_CHAN 0x1405Cu           /* byte 0 = channel + 101 (the SDK's wifi_set_channel) */
FM1_INLINE uint32_t fm1_wifi_channel(void) { return (fm1_wifi_rd(FM1_WIFI_CHAN) & 0xFFu) - 101u; }
FM1_INLINE void fm1_wifi_set_channel(uint32_t ch)
{
    volatile uint32_t *c = (volatile uint32_t *)(uintptr_t)FM1_WIFI_CHAN;
    *c = (*c & ~0xFFu) | ((ch + 101u) & 0xFFu);
}
#define FM1_WIFI_IRQ_EN 0x328u           /* MAC: 2 = the receive interrupt on (the SDK's wfhw_iqr_init) */
#define FM1_WIFI_IRQ_CTL 0x14050u        /* bit 0: the block's interrupt (wfhw_iqr_en; the handler clears it) */
#define FM1_WIFI_IRQ 52u                 /* its CPU interrupt (request_irq(52, ...)): never attached by us */
FM1_INLINE void fm1_wifi_rx_arm(int on)    /* the SDK's RX arming: wfhw_iqr_init + wfhw_iqr_en (off: both back to 0) */
{
    volatile uint32_t *c = (volatile uint32_t *)(uintptr_t)FM1_WIFI_IRQ_CTL;
    fm1_wifi_mac_wr(FM1_WIFI_IRQ_EN, on ? 2u : 0u);
    *c = on ? *c | 1u : *c & ~1u;
}
FM1_INLINE uint32_t fm1_wifi_irq_pending(void)   /* the CPU's pending bit of interrupt 52 */
{
    return (*(const volatile uint32_t *)(uintptr_t)(0x1EEF180u + 4u * (FM1_WIFI_IRQ >> 5)) >> (FM1_WIFI_IRQ & 31u)) & 1u;
}
/* the Ralink STA driver's MAC settings when the interface comes up (rt28xx_init / NICInitializeAsic / RTMPEnableRxTx,
 * after the radio start-up), in its order, as the SDK's wl_wifi_sta.a writes them in the emulator capture
 * (tools/wifi_capture, docs/WIFI-HW-FACTS.md §2b); the WCID / key table clears left out */
static const uint32_t fm1_wifi_sta_init[][2] = {
    {0x042Cu, 0xF8F0E8E0u}, {0x0430u, 0x6F77D0C8u}, {0x1408u, 0x0000013Fu}, {0x140Cu, 0x00008003u},
    {0x1400u, 0x00017F97u}, {0x1350u, 0x00001020u}, {0x1018u, 0x00001F00u}, {0x040Cu, 0x1F3FBF9Fu},
    {0x134Cu, 0x47D01F0Fu}, {0x1404u, 0x00000013u}, {0x1364u, 0x05740003u}, {0x1368u, 0x05740003u},
    {0x0408u, 0x00F40006u}, {0x1370u, 0x03F44084u}, {0x1374u, 0x01744004u}, {0x1378u, 0x03F44084u},
    {0x136Cu, 0x01744004u}, {0x1340u, 0x0000583Fu}, {0x1344u, 0x00092B20u}, {0x1380u, 0x002400CAu},
    {0x1608u, 0x00000002u}, {0x1204u, 0x00000003u}, {0x1114u, 0x00000000u}, {0x1340u, 0x0000583Fu},
    {0x1008u, 0x00000000u}, {0x100Cu, 0x00FF0000u}, {0x1300u, 0x00064300u}, {0x1304u, 0x000A4700u},
    {0x1308u, 0x00043538u}, {0x130Cu, 0x0003212Fu}, {0x1400u, 0x00017F97u},
};
FM1_INLINE void fm1_wifi_sta_settings(void)
{
    uint32_t i;
    for (i = 0; i < sizeof fm1_wifi_sta_init / sizeof *fm1_wifi_sta_init; i++)
        fm1_wifi_mac_wr(fm1_wifi_sta_init[i][0], fm1_wifi_sta_init[i][1]);
}
#define FM1_WL_EN 0x14000u
FM1_INLINE void fm1_wifi_wlc_setup(void)   /* the SDK's wlc_main: both RF paths on, then the WLC tables (fm1_wifi_wlc.h) */
{
    volatile uint32_t *e = (volatile uint32_t *)(uintptr_t)0x14000u, *w = (volatile uint32_t *)(uintptr_t)FM1_WLC_PORT;
    uint32_t i;
    *e |= 3u;
    for (i = 0; i < sizeof fm1_wlc_init / sizeof *fm1_wlc_init; i++)
        *w = fm1_wlc_init[i];
}               /* bit 0 BT, bit 1 Wi-Fi: the RF path on (the SDK's wifi_rf_on / wifi_rf_off) */
#define FM1_WL_EN_WIFI 2u
FM1_INLINE void fm1_wifi_rf(int on)           /* the Wi-Fi side of the shared RF on / off (the clock words left as they are) */
{
    volatile uint32_t *e = (volatile uint32_t *)(uintptr_t)FM1_WL_EN;
    *e = on ? *e | FM1_WL_EN_WIFI : *e & ~FM1_WL_EN_WIFI;
}

FM1_INLINE int fm1_wifi_dma_wait(uint32_t bit, uint32_t ticks)   /* 1: the bit came up within ticks (TIMER4) */
{
    uint32_t t0 = FM1_T4_CNT;
    while (!(fm1_wifi_rd(FM1_WIFI_WDMA1) & bit))
        if (FM1_T4_CNT - t0 > ticks)
            return 0;
    return 1;
}

/* one waiting frame into buf (4-aligned, FM1_WIFI_RX_MAX + 8 bytes), as the SDK's wl30_mmc_io_rw_extended (addr 12):
 * the length word first (big endian), then length + 4 bytes from buf + 4. Returns the length, 0 if none waited,
 * -1 if the DMA did not finish within 10 ms or the length was out of range. The SDK writes no cache maintenance
 * around it (a csync only): neither does this */
FM1_INLINE int32_t fm1_wifi_rx(uint32_t *buf)
{
    volatile uint32_t *s = (volatile uint32_t *)(uintptr_t)FM1_WIFI_WDMA1;
    uint32_t n, w;
    if (!(*s & FM1_WIFI_RX_PEND))
        return 0;
    fm1_wifi_mac_wr(FM1_WIFI_RX_ACK, 2u);
    *s |= FM1_WIFI_RX_CLR;
    __asm__ volatile("csync;");
    *(volatile uint32_t *)(uintptr_t)FM1_WIFI_RX_ADDR = (uint32_t)(uintptr_t)buf;
    *(volatile uint32_t *)(uintptr_t)FM1_WIFI_RX_LEN = 4u;
    __asm__ volatile("csync;");
    if (!fm1_wifi_dma_wait(FM1_WIFI_RX_DONE, 10000u * FM1_TICKS_PER_US))
        return -1;
    *s |= FM1_WIFI_RX_CLR;
    __asm__ volatile("csync;");
    w = *(volatile uint32_t *)buf;
    n = w >> 24 | (w >> 8 & 0xFF00u) | (w << 8 & 0xFF0000u) | w << 24;
    if (!w || n > FM1_WIFI_RX_MAX)
        return -1;
    *(volatile uint32_t *)(uintptr_t)FM1_WIFI_RX_ADDR = (uint32_t)(uintptr_t)buf + 4u;
    *(volatile uint32_t *)(uintptr_t)FM1_WIFI_RX_LEN = n + 4u;
    __asm__ volatile("csync;");
    if (!fm1_wifi_dma_wait(FM1_WIFI_RX_DONE, 10000u * FM1_TICKS_PER_US))
        return -1;
    *s |= FM1_WIFI_RX_CLR;
    return (int32_t)n;
}
