/* SPDX-License-Identifier: GPL-3.0-only */
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

static uint32_t btl_features(uint8_t *out, uint32_t op, uint32_t tid)
{
    uint32_t i, n = btl_pdu(out, op, tid);
    for (i = 0; i < 8u; i++)                       /* no optional feature claimed: DM1 / DH1 basic rate only */
        out[n++] = 0;
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
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ:      /* a page of extended features: none */
        n = btl_pdu(out, BTL_ESCAPE, tid);
        out[n++] = BTL_X_FEATURES_RES;
        out[n++] = len > 2u ? p[2] : 0;            /* the page asked */
        out[n++] = 0;                              /* our maximum page */
        for (op = 0; op < 8u; op++)
            out[n++] = 0;
        return n;
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
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ: return "features_req_ext";
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_RES: return "features_res_ext";
    case BTL_ESCAPE << 8 | BTL_X_NOT_ACCEPTED: return "not_accepted_ext";
    default: return "?";
    }
}
