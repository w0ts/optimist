/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Bluetooth Classic (BR/EDR) inquiry probe, stage 1 of our own Classic stack (docs/BT-CLASSIC.md §14):
 * the portable part, written from the Bluetooth Core specification (Vol 2 Part B §6.3.3 sync word, §6.5.1.4 FHS,
 * Vol 3 Part C §8 EIR) and the fact sheet. No vendor code. Portable C, no C library; host-tested in
 * tests/bt_inq_test.c.
 *
 *   bti_parity(lap, out)   the 34 parity bits s0..s33 of the sync word of a LAP, 5 bytes little-endian (s0 = bit 0 of
 *                          byte 0; bits 32-33 in byte 4): what the frame's bch field holds (HW §5.3, checked against
 *                          the GIAC constant and stock V15's own address)
 *   bti_fhs_parse(p, f)    an 18-byte FHS payload into its fields
 *   bti_eir_name(...)      the local name (complete 0x09, else shortened 0x08) of an EIR / extended inquiry response */
#ifndef BT_INQ_H
#define BT_INQ_H
#include <stdint.h>

#define BTI_GIAC 0x9E8B33u               /* the general inquiry access code's LAP */

struct bti_fhs {
    uint8_t addr[6];                     /* BD_ADDR little-endian: LAP [0..2], UAP [3], NAP [4..5] */
    uint32_t parity_lo;                  /* the 34 parity bits as sent: bits 0-31 */
    uint8_t parity_hi;                   /*   bits 32-33 */
    uint8_t eir;                         /* an extended inquiry response follows */
    uint8_t sr;                          /* page scan repetition mode R0 / R1 / R2 */
    uint8_t reserved;                    /* the 2-bit field after SR (the spec: 10) */
    uint32_t cod;                        /* class of device */
    uint8_t lt_addr;
    uint32_t clk;                        /* CLK[27:2] */
    uint8_t psm;                         /* page scan mode */
};

void bti_parity(uint32_t lap, uint8_t out[5]);
void bti_fhs_parse(const uint8_t p[18], struct bti_fhs *f);
/* the 18 bytes of an FHS from f's address, EIR bit, SR, class, LT_ADDR, clock and page scan mode; the parity field is
 * computed from the address's LAP and the reserved field written 10b (the spec's), whatever f holds there */
void bti_fhs_build(const struct bti_fhs *f, uint8_t p[18]);
/* the name in an EIR's data (len bytes, the significant part ends at a 0 length) -> its length (0: none), copied
 * to name (at most max - 1 bytes, printable ASCII only, '.' for others, 0-terminated) */
uint32_t bti_eir_name(const uint8_t *eir, uint32_t len, char *name, uint32_t max);

#endif
