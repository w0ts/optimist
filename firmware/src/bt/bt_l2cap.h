/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's L2CAP signalling and SDP client (Core spec Vol 3 Part A, Part B): the frames we send and the
 * reading of an SDP attribute list (the A2DP sink record: its PSM, versions, name). Portable C, no C library;
 * host-tested in tests/bt_l2cap_test.c. Written from the specification, no vendor code.
 *
 * An L2CAP frame: length (2, the payload's), channel id (2), payload; all little-endian. Signalling (CID 1): code,
 * identifier, length (2), data. SDP PDUs are big-endian. */
#ifndef BT_L2CAP_H
#define BT_L2CAP_H
#include <stdint.h>

enum { BL_CID_SIG = 1, BL_PSM_SDP = 1, BL_PSM_AVDTP = 0x19 };
enum { BL_CMD_REJECT = 1, BL_CONN_REQ = 2, BL_CONN_RSP = 3, BL_CONF_REQ = 4, BL_CONF_RSP = 5, BL_DISC_REQ = 6,
       BL_DISC_RSP = 7, BL_INFO_REQ = 10, BL_INFO_RSP = 11 };

/* signalling frames (whole L2CAP frames, CID 1) -> their length */
uint32_t bl_conn_req(uint8_t *out, uint32_t id, uint32_t psm, uint32_t scid);
uint32_t bl_conn_rsp(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid, uint32_t result);
uint32_t bl_conf_req(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t mtu);
uint32_t bl_conf_rsp(uint8_t *out, uint32_t id, uint32_t scid, uint32_t result);
uint32_t bl_disc_req(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid);
uint32_t bl_disc_rsp(uint8_t *out, uint32_t id, uint32_t dcid, uint32_t scid);
uint32_t bl_info_req(uint8_t *out, uint32_t id, uint32_t type);
uint32_t bl_info_rsp(uint8_t *out, uint32_t id, uint32_t type);
uint32_t bl_cmd_reject(uint8_t *out, uint32_t id);

/* an SDP ServiceSearchAttributeRequest for a 16-bit UUID, all attributes, with a continuation state (cont, n bytes,
 * n = 0 the first time), as a whole L2CAP frame on channel dcid -> its length */
uint32_t bl_sdp_ssa_req(uint8_t *out, uint32_t dcid, uint32_t tid, uint32_t uuid16, const uint8_t *cont, uint32_t n);

/* what the probe reads out of an attribute list (or a list of them) */
struct bl_sdp_info {
    uint8_t has_class;                             /* the ServiceClassIDList holds the class searched for */
    uint16_t psm;                                  /* the L2CAP PSM in the ProtocolDescriptorList (0: none) */
    uint16_t proto_uuid, proto_ver;                /* the protocol on top of it (AVDTP 0x0019) and its version */
    uint16_t profile_uuid, profile_ver;            /* BluetoothProfileDescriptorList (A2DP 0x110D) */
    uint16_t features;                             /* SupportedFeatures (0x0311) */
    char name[32];                                 /* ServiceName (0x0100) */
    uint8_t records;                               /* attribute lists seen */
};
/* the AttributeLists data element of a ServiceSearchAttributeResponse (p, n bytes) read for class `cls` -> 1 when it
 * was well formed */
int bl_sdp_parse(const uint8_t *p, uint32_t n, uint32_t cls, struct bl_sdp_info *info);

#endif
