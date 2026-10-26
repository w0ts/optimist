/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's AVDTP initiator (AVDTP 1.3 §8): the signalling PDUs to find an audio sink's SBC endpoint, its
 * capabilities, configure SBC and open the stream, and the SBC choice (A2DP 1.3 §4.3). Portable C, no C library;
 * host-tested in tests/bt_avdtp_test.c. Written from the specifications, no vendor code. */
#ifndef BT_AVDTP_H
#define BT_AVDTP_H
#include <stdint.h>

enum { BV_DISCOVER = 1, BV_GET_CAPS = 2, BV_SET_CONF = 3, BV_OPEN = 6, BV_START = 7, BV_CLOSE = 8, BV_SUSPEND = 9 };
enum { BV_CMD = 0, BV_ACCEPT = 2, BV_REJECT = 3 };

/* a single-packet command: label 0-15, signal id, parameters -> its length in out */
uint32_t bv_cmd(uint8_t *out, uint32_t label, uint32_t sig, const uint8_t *par, uint32_t n);
/* a received PDU's header -> 1 when single-packet; *label, *msg (BV_ACCEPT...), *sig */
int bv_hdr(const uint8_t *p, uint32_t n, uint32_t *label, uint32_t *msg, uint32_t *sig);
/* the first audio sink endpoint not in use in a DISCOVER answer -> its SEID (0: none) */
uint32_t bv_pick_sink(const uint8_t *p, uint32_t n);
/* the SBC codec information (4 bytes) in a GET_CAPABILITIES answer -> 1 found */
int bv_caps_sbc(const uint8_t *p, uint32_t n, uint8_t sbc[4]);
/* our SBC configuration from the sink's capabilities: 44.1 kHz else 48, joint stereo else stereo, 16 blocks, 8
 * subbands, loudness, bitpool min..min(max, 53) -> 1 when a choice exists */
int bv_sbc_choose(const uint8_t cap[4], uint8_t conf[4]);
/* SET_CONFIGURATION's parameters (ACP SEID, INT SEID, media transport, SBC codec) -> their length */
uint32_t bv_setconf_par(uint8_t *out, uint32_t acp, uint32_t intseid, const uint8_t conf[4]);

#endif
