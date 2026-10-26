/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's minimal LMP (Core spec Vol 2 Part C), the master side of a fresh ACL link: what we send to set
 * the connection up (features, version, host connection, setup complete) and the answers to what a slave asks.
 * Portable C, no C library; host-tested in tests/bt_lmp_test.c. Written from the specification, no vendor code.
 *
 * A PDU is the DM1 payload with LLID 3: byte 0 = opcode << 1 | TID (TID 0: a transaction the master started, 1: the
 * slave's), then the parameters; opcode 127 escapes to an extended opcode in byte 1. */
#ifndef BT_LMP_H
#define BT_LMP_H
#include <stdint.h>

enum {
    BTL_NAME_REQ = 1, BTL_NAME_RES = 2, BTL_ACCEPTED = 3, BTL_NOT_ACCEPTED = 4, BTL_CLKOFFSET_REQ = 5,
    BTL_CLKOFFSET_RES = 6, BTL_DETACH = 7, BTL_SWITCH_REQ = 19, BTL_INCR_POWER = 31, BTL_DECR_POWER = 32,
    BTL_AUTO_RATE = 35, BTL_PREFERRED_RATE = 36, BTL_VERSION_REQ = 37, BTL_VERSION_RES = 38, BTL_FEATURES_REQ = 39,
    BTL_FEATURES_RES = 40, BTL_QOS = 41, BTL_QOS_REQ = 42, BTL_MAX_SLOT = 45, BTL_MAX_SLOT_REQ = 46,
    BTL_TIMING_ACC_REQ = 47, BTL_TIMING_ACC_RES = 48, BTL_SETUP_COMPLETE = 49, BTL_HOST_CONN_REQ = 51,
    BTL_SLOT_OFFSET = 52, BTL_SUPERVISION_TO = 55, BTL_SET_AFH = 60,
    /* pairing and encryption (Part C 5.2; checked against Wireshark's packet-btlmp.c table, 2026-10-19: the values
     * this file had before for these were not the spec's) */
    BTL_IN_RAND = 8, BTL_COMB_KEY = 9, BTL_AU_RAND = 11, BTL_SRES = 12, BTL_ENCRYPTION_MODE_REQ = 15,
    BTL_ENCRYPTION_KEY_SIZE_REQ = 16, BTL_START_ENCRYPTION_REQ = 17, BTL_STOP_ENCRYPTION_REQ = 18,
    BTL_ENCAPSULATED_HEADER = 61, BTL_ENCAPSULATED_PAYLOAD = 62, BTL_SIMPLE_PAIRING_CONFIRM = 63,
    BTL_SIMPLE_PAIRING_NUMBER = 64, BTL_DHKEY_CHECK = 65, BTL_ESCAPE = 127,
    BTL_X_ACCEPTED = 1, BTL_X_NOT_ACCEPTED = 2, BTL_X_FEATURES_REQ = 3, BTL_X_FEATURES_RES = 4,
    BTL_X_IO_CAPABILITY_REQ = 25, BTL_X_IO_CAPABILITY_RES = 26, BTL_X_NUMERIC_COMPARISON_FAILED = 27
};
#define BTL_ERR_AUTH_FAILURE 0x05u
#define BTL_ERR_PIN_OR_KEY_MISSING 0x06u
#define BTL_ERR_PAIRING_NOT_ALLOWED 0x18u
#define BTL_ERR_ENCRYPTION_MODE 0x25u
#define BTL_ERR_UNSUPPORTED_PARAM 0x20u
#define BTL_ERR_UNKNOWN_PDU 0x19u
#define BTL_ERR_ROLE_SWITCH_NOT_ALLOWED 0x21u
#define BTL_MAX 17u                               /* a DM1 payload */

/* the PDU in p (len bytes) -> its opcode (an extended one as 127 << 8 | ext), *tid its TID; 0 when too short */
uint32_t btl_opcode(const uint8_t *p, uint32_t len, uint32_t *tid);
/* our requests (TID 0): -> the PDU's length in out */
uint32_t btl_features_req(uint8_t *out);
uint32_t btl_version_req(uint8_t *out);
uint32_t btl_host_conn_req(uint8_t *out);
uint32_t btl_setup_complete(uint8_t *out);
uint32_t btl_detach(uint8_t *out, uint32_t reason);
/* the answer to the slave's PDU in p (len bytes), name our device's name (0-terminated) -> the answer's length in out
 * (0: none due). The answer carries the request's TID. Unknown or unsupported requests are not accepted. */
uint32_t btl_answer(const uint8_t *p, uint32_t len, const char *name, uint8_t *out);
/* a short name for an opcode (console), "?" for others */
const char *btl_name(uint32_t opcode);

#endif
