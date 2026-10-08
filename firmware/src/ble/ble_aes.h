/* SPDX-License-Identifier: GPL-3.0-only */
/* AES-128 (FIPS-197, encryption only) and the LL's AES-CCM (Core Specification Vol 6 Part E, sample data in
 * Vol 6 Part C 1). Byte order: AES's own (the Bluetooth security function e() with its most significant octet
 * first); the LL turns the least-significant-first fields it receives around before calling it.
 *
 * Replacing the software AES with the chip's AES block: build with BLE_AES_HW=1 and give the driver a
 * ble_aes128() with this signature; ble_aes.c then leaves its own out. CCM stays here, on top of it. */
#ifndef BLE_AES_H
#define BLE_AES_H
#include <stdint.h>

void ble_aes128(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* one direction of an encrypted link */
struct ble_ccm {
    uint8_t key[16];                     /* the session key SK, most significant octet first */
    uint8_t iv[8];                       /* IV = IVm || IVs as the nonce takes it: IVm's octets (least significant
                                          * first) then IVs's */
    uint32_t ctr;                        /* packetCounter, low 32 bits */
    uint8_t ctr_hi;                      /* ... and bits 32..38 */
    uint8_t dir;                         /* directionBit: 1 central -> peripheral, 0 peripheral -> central */
};
/* p: the payload (len octets, 1..251 - 4); the MIC goes after it. The counter steps on. */
void ble_ccm_encrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len);
/* p: payload and MIC (len octets, 5..255); 1 if the MIC is right (p then holds len - 4 octets of plain text,
 * and the counter steps on), 0 if not */
int ble_ccm_decrypt(struct ble_ccm *c, uint8_t hdr, uint8_t *p, uint8_t len);

#endif
