/* SPDX-License-Identifier: GPL-3.0-only */
/* The face of one FM-1 stack unit (tests/ble_f2f_unit.c) for tests/ble_f2f_test.c: F2F_DECLARE(u) declares u_init, ...
 * Inside a unit (F2F_U defined), F2F(name) names its own functions. */
#ifndef BLE_F2F_H
#define BLE_F2F_H
#include <stdint.h>

#define F2F_CAT2(a, b) a##_##b
#define F2F_CAT(a, b) F2F_CAT2(a, b)
#define F2F(name) F2F_CAT(F2F_U, name)

#define F2F_DECLARE(u)                                                                                              \
    void u##_init(const uint8_t addr[6], uint32_t seed);                                                            \
    int u##_adv_on(void);                                                                                           \
    int u##_connect(const uint8_t peer[6]);                                                                         \
    const uint8_t *u##_cind(void);                                                                                  \
    int u##_take_cind(const uint8_t *cind);                                                                         \
    void u##_master_start(void);                                                                                    \
    int u##_conn_on(void);                                                                                          \
    uint8_t u##_tx(uint8_t *pdu);                                                                                   \
    void u##_acked(void);                                                                                           \
    void u##_rx(const uint8_t *pdu, uint8_t n);                                                                     \
    void u##_event_end(uint16_t counter);                                                                           \
    int u##_central_state(void);                                                                                    \
    int u##_central_fail(void);                                                                                     \
    uint32_t u##_passkey(void);                                                                                     \
    int u##_midi_ready(void);                                                                                       \
    int u##_connected(void);                                                                                        \
    int u##_encrypted(void);                                                                                        \
    uint32_t u##_pairings(void);                                                                                    \
    uint32_t u##_mitm_pairings(void);                                                                               \
    uint32_t u##_keys(void);                                                                                        \
    void u##_midi_out(uint32_t pkt);                                                                                \
    uint32_t u##_midi_in_n(void);                                                                                   \
    uint32_t u##_midi_in(uint32_t i);

#ifdef F2F_U
#define F2F_SELF(u) F2F_DECLARE(u)
F2F_SELF(F2F_U)
#endif

#endif
