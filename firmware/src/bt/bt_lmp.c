/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Classic probe's minimal LMP (bt_lmp.h), from the Core spec Vol 2 Part C; host-tested (tests/bt_lmp_test.c). */
#include "bt_lmp.h"

#define BTL_VERS 0x06u                             /* LMP version 4.0: only the basic set is claimed */
#define BTL_COMP 0xFFFFu                           /* company identifier: not assigned (a test device) */
#define BTL_SUBVERS 0x0001u

static uint32_t btl_pdu(uint8_t *out, uint32_t op, uint32_t tid)
{
    out[0] = (uint8_t)(op << 1 | (tid & 1u));
    return 1;
}

uint32_t btl_opcode(const uint8_t *p, uint32_t len, uint32_t *tid)
{
    uint32_t op;
    if (len < 1u)
        return 0;
    *tid = p[0] & 1u;
    op = p[0] >> 1;
    if (op == BTL_ESCAPE)
        return len < 2u ? 0 : (BTL_ESCAPE << 8 | p[1]);
    return op;
}

/* our features, page 0 (Core Vol 2 Part C 3.3). Before: 0x08 / 0x21 in bytes 0 / 2, read as encryption and SSP, are
 * slot offset, CVSD and a flow-lag bit: no encryption, no SSP, no multi-slot said. The AirPods refused 3 and 5 slots
 * (0x1F) and stopped pairing after our public key (the user, 2026-10-20) */
static const uint8_t BTL_FEAT0[8] = {
    0x07u,                                         /* byte 0: 3-slot, 5-slot packets (bits 0, 1), encryption (bit 2) */
    0, 0, 0, 0,
    0x08u,                                         /* byte 5: AFH capable master (bit 43; bt_afh.c, LMP_set_AFH) */
    0x18u,                                         /* byte 6: Secure Simple Pairing (bit 51), encapsulated PDU (bit 52) */
    0x80u,                                         /* byte 7: extended features (bit 63): page 1 below */
};
#define BTL_FEAT1_SSP_HOST 0x01u                   /* page 1 bit 0: Secure Simple Pairing (Host Support) */
static uint32_t btl_features(uint8_t *out, uint32_t op, uint32_t tid)
{
    uint32_t n = btl_pdu(out, op, tid);
    for (uint32_t i = 0; i < 8u; i++)
        out[n++] = BTL_FEAT0[i];
    return n;
}

static uint32_t btl_version(uint8_t *out, uint32_t op, uint32_t tid)
{
    uint32_t n = btl_pdu(out, op, tid);
    out[n++] = BTL_VERS;
    out[n++] = (uint8_t)BTL_COMP;
    out[n++] = (uint8_t)(BTL_COMP >> 8);
    out[n++] = (uint8_t)BTL_SUBVERS;
    out[n++] = (uint8_t)(BTL_SUBVERS >> 8);
    return n;
}

uint32_t btl_features_req(uint8_t *out) { return btl_features(out, BTL_FEATURES_REQ, 0); }
uint32_t btl_version_req(uint8_t *out) { return btl_version(out, BTL_VERSION_REQ, 0); }
uint32_t btl_host_conn_req(uint8_t *out) { return btl_pdu(out, BTL_HOST_CONN_REQ, 0); }
uint32_t btl_setup_complete(uint8_t *out) { return btl_pdu(out, BTL_SETUP_COMPLETE, 0); }

uint32_t btl_set_afh(uint8_t *out, uint32_t instant, uint32_t mode, const uint8_t map[10])   /* LMP_set_AFH (Part C 5.2):
                                                    * the slot of the switch (the master's CLK[27:1]: the vendor controller
                                                    * compares the field with its slot clock as it is, btctrler.a
                                                    * connection_rx_handler; doubled, the XM4 switched 26 s after us,
                                                    * 2026-10-23), 0 off / 1 on, the 79-bit map */
{
    uint32_t n = btl_pdu(out, BTL_SET_AFH, 0), i;
    for (i = 0; i < 4u; i++)
        out[n++] = (uint8_t)(instant >> 8u * i);
    out[n++] = (uint8_t)mode;
    for (i = 0; i < 10u; i++)
        out[n++] = map[i];
    return n;
}
uint32_t btl_detach(uint8_t *out, uint32_t reason)
{
    uint32_t n = btl_pdu(out, BTL_DETACH, 0);
    out[n++] = (uint8_t)reason;
    return n;
}

static uint32_t btl_reply(uint8_t *out, uint32_t op, uint32_t tid, uint32_t of, uint32_t reason)
{
    uint32_t n = btl_pdu(out, op, tid);
    out[n++] = (uint8_t)of;
    if (op == BTL_NOT_ACCEPTED)
        out[n++] = (uint8_t)reason;
    return n;
}

static uint32_t btl_reply_ext(uint8_t *out, uint32_t tid, uint32_t ext, uint32_t reason)
{
    uint32_t n = btl_pdu(out, BTL_ESCAPE, tid);
    out[n++] = BTL_X_NOT_ACCEPTED;
    out[n++] = BTL_ESCAPE;
    out[n++] = (uint8_t)ext;
    out[n++] = (uint8_t)reason;
    return n;
}

static uint32_t btl_name_res(const uint8_t *p, uint32_t len, uint32_t tid, const char *name, uint8_t *out)
{
    uint32_t total = 0, off = len > 1u ? p[1] : 0, n = btl_pdu(out, BTL_NAME_RES, tid), k;
    while (name[total] && total < 248u)
        total++;
    out[n++] = (uint8_t)off;
    out[n++] = (uint8_t)total;
    for (k = 0; k < 14u; k++)                      /* the 14-byte fragment from the offset, zero padded */
        out[n++] = (uint8_t)(off + k < total ? name[off + k] : 0);
    return n;
}

/* SSP Just Works pairing: IO capability response, confirm exchange, DHKey exchange, start encryption.
 * INTEGRATION: These call bts_ssp_* functions (bt_safer.h) when the crypto layer is ready.
 * For now, they return NOT_ACCEPTED to signal lack of support. */




uint32_t btl_answer(const uint8_t *p, uint32_t len, const char *name, uint8_t *out)
{
    uint32_t tid = 0, op = btl_opcode(p, len, &tid), n;
    if (op == 0)
        return 0;
    switch (op) {
    case BTL_FEATURES_REQ:
        return btl_features(out, BTL_FEATURES_RES, tid);
    case BTL_VERSION_REQ:
        return btl_version(out, BTL_VERSION_RES, tid);
    case BTL_NAME_REQ:
        return btl_name_res(p, len, tid, name, out);
    case BTL_CLKOFFSET_REQ:                        /* (the master's own clock: offset 0) */
        n = btl_pdu(out, BTL_CLKOFFSET_RES, tid);
        out[n++] = 0;
        out[n++] = 0;
        return n;
    case BTL_TIMING_ACC_REQ:
        n = btl_pdu(out, BTL_TIMING_ACC_RES, tid);
        out[n++] = 250;                            /* drift, ppm: the spec's worst case */
        out[n++] = 10;                             /* jitter, us */
        return n;
    case BTL_MAX_SLOT_REQ:
    case BTL_QOS_REQ:
        return btl_reply(out, BTL_ACCEPTED, tid, op, 0);
    case BTL_SWITCH_REQ:
        return btl_reply(out, BTL_NOT_ACCEPTED, tid, op, BTL_ERR_ROLE_SWITCH_NOT_ALLOWED);
    /* (pairing and encryption, SSP Just Works: bt_ssp.c answers them before this: bt_page.c bp_lmp_in) */
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ: {    /* a page of features: 0, 1 (SSP host support), else none */
        uint32_t pg = len > 2u ? p[2] : 0;
        n = btl_pdu(out, BTL_ESCAPE, tid);
        out[n++] = BTL_X_FEATURES_RES;
        out[n++] = (uint8_t)pg;                    /* the page asked */
        out[n++] = 1;                              /* our maximum page */
        for (op = 0; op < 8u; op++)
            out[n++] = pg == 0u ? BTL_FEAT0[op] : pg == 1u && op == 0u ? BTL_FEAT1_SSP_HOST : 0u;
        return n;
    }
    case BTL_ACCEPTED:
    case BTL_NOT_ACCEPTED:
    case BTL_FEATURES_RES:
    case BTL_VERSION_RES:
    case BTL_NAME_RES:
    case BTL_SETUP_COMPLETE:
    case BTL_HOST_CONN_REQ:                        /* (a slave never sends it; nothing to answer) */
    case BTL_DETACH:
    case BTL_MAX_SLOT:
    case BTL_AUTO_RATE:
    case BTL_PREFERRED_RATE:
    case BTL_INCR_POWER:
    case BTL_DECR_POWER:
    case BTL_SUPERVISION_TO:
    case BTL_SLOT_OFFSET:
    case BTL_QOS:
    case BTL_TIMING_ACC_RES:
    case BTL_CLKOFFSET_RES:
    case BTL_SRES:
    case BTL_ESCAPE << 8 | BTL_X_ACCEPTED:
    case BTL_ESCAPE << 8 | BTL_X_NOT_ACCEPTED:
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_RES:
        return 0;
    default:
        if (op >> 8 == BTL_ESCAPE)
            return btl_reply_ext(out, tid, op & 0xFFu, BTL_ERR_UNKNOWN_PDU);
        return btl_reply(out, BTL_NOT_ACCEPTED, tid, op, BTL_ERR_UNKNOWN_PDU);
    }
}

const char *btl_name(uint32_t op)
{
#if defined(FELUCCA_BT_DIAG) && !FELUCCA_BT_DIAG      /* (the names are only ever printed: BT_DIAG) */
    (void)op;
    return "";
#else
    switch (op) {
    case BTL_NAME_REQ: return "name_req";
    case BTL_NAME_RES: return "name_res";
    case BTL_ACCEPTED: return "accepted";
    case BTL_NOT_ACCEPTED: return "not_accepted";
    case BTL_CLKOFFSET_REQ: return "clkoffset_req";
    case BTL_DETACH: return "detach";
    case BTL_SWITCH_REQ: return "switch_req";
    case BTL_AUTO_RATE: return "auto_rate";
    case BTL_PREFERRED_RATE: return "preferred_rate";
    case BTL_VERSION_REQ: return "version_req";
    case BTL_VERSION_RES: return "version_res";
    case BTL_FEATURES_REQ: return "features_req";
    case BTL_FEATURES_RES: return "features_res";
    case BTL_MAX_SLOT: return "max_slot";
    case BTL_MAX_SLOT_REQ: return "max_slot_req";
    case BTL_TIMING_ACC_REQ: return "timing_accuracy_req";
    case BTL_SETUP_COMPLETE: return "setup_complete";
    case BTL_HOST_CONN_REQ: return "host_connection_req";
    case BTL_SUPERVISION_TO: return "supervision_timeout";
    case BTL_SET_AFH: return "set_afh";
    case BTL_AU_RAND: return "au_rand";
    case BTL_SRES: return "sres";
    case BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_REQ: return "io_capability_req";
    case BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_RES: return "io_capability_res";
    case BTL_ENCAPSULATED_HEADER: return "encapsulated_header";
    case BTL_ENCAPSULATED_PAYLOAD: return "encapsulated_payload";
    case BTL_SIMPLE_PAIRING_CONFIRM: return "simple_pairing_confirm";
    case BTL_SIMPLE_PAIRING_NUMBER: return "simple_pairing_number";
    case BTL_DHKEY_CHECK: return "dhkey_check";
    case BTL_ENCRYPTION_MODE_REQ: return "encryption_mode_req";
    case BTL_ENCRYPTION_KEY_SIZE_REQ: return "encryption_key_size_req";
    case BTL_START_ENCRYPTION_REQ: return "start_encryption_req";
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ: return "features_req_ext";
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_RES: return "features_res_ext";
    case BTL_ESCAPE << 8 | BTL_X_NOT_ACCEPTED: return "not_accepted_ext";
    default: return "?";
    }
#endif
}
