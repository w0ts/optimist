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
    uint32_t n = btl_pdu(out, op, tid);
    out[n++] = 0x08u;                              /* byte 0: bit 3 = Encryption */
    out[n++] = 0x00u;                              /* byte 1 */
    out[n++] = 0x21u;                              /* byte 2: bit 0 = SSP (Controller), bit 5 = SSP (Host) */
    for (uint32_t i = 3; i < 8u; i++)
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

/* SSP Just Works pairing: IO capability response, confirm exchange, DHKey exchange, start encryption.
 * INTEGRATION: These call bts_ssp_* functions (bt_safer.h) when the crypto layer is ready.
 * For now, they return NOT_ACCEPTED to signal lack of support. */
static uint32_t btl_io_cap_res(uint8_t *out, uint32_t tid)
{
    uint32_t n = btl_pdu(out, BTL_IO_CAPABILITY_RES, tid);
    out[n++] = 0x03u;                              /* NoInputNoOutput: AirPods-compatible */
    out[n++] = 0x00u;                              /* OOB data flag: not present */
    out[n++] = 0x00u;                              /* authentication requirements: Just Works */
    return n;
}

static uint32_t btl_ssp_confirm(uint8_t *out, uint32_t tid, const uint8_t confirm[16])
{
    uint32_t n = btl_pdu(out, BTL_SIMPLE_PAIRING_CONFIRM, tid), i;
    for (i = 0; i < 16u; i++)
        out[n++] = confirm[i];
    return n;
}

static uint32_t btl_ssp_random(uint8_t *out, uint32_t tid, const uint8_t random[16])
{
    uint32_t n = btl_pdu(out, BTL_SIMPLE_PAIRING_RANDOM, tid), i;
    for (i = 0; i < 16u; i++)
        out[n++] = random[i];
    return n;
}

static uint32_t btl_encaps_pk(uint8_t *out, uint32_t op, uint32_t tid, const uint8_t pk[32])
{
    uint32_t n = btl_pdu(out, op, tid), i;
    for (i = 0; i < 32u; i++)
        out[n++] = pk[i];
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
    case BTL_IO_CAPABILITY_REQ:                    /* SSP pairing initiation: respond with NoInputNoOutput */
        return btl_io_cap_res(out, tid);
    case BTL_LINK_KEY_REQ:                         /* no stored link key yet (pairing in progress or first time) */
        return btl_reply(out, BTL_NOT_ACCEPTED, tid, op, 0x02u);  /* 0x02 = no link key for BD_ADDR */
    case BTL_ENCAPSULATED_P192:                    /* remote public key (32 B X || 32 B Y, little-endian) */
    {
        /* INTEGRATION: store remote_pub_x/y from p[1..32] and p[33..64]; compute DHKey via bts_ecc_shared_secret.
         * For now, return placeholder. The DHKey must match: the LTK generation depends on it. */
        uint8_t dummy_pk[32] = {0};
        return btl_encaps_pk(out, BTL_ENCAPSULATED_P192, tid, dummy_pk);
    }
    case BTL_SIMPLE_PAIRING_CONFIRM:               /* remote confirm value (16 bytes) */
    {
        /* INTEGRATION: store remote_confirm from p[1..16]; compute own confirm via bts_ssp_f1 and respond.
         * For now, return placeholder. */
        uint8_t dummy_confirm[16] = {0};
        return btl_ssp_confirm(out, tid, dummy_confirm);
    }
    case BTL_SIMPLE_PAIRING_RANDOM:                /* remote random value (16 bytes) */
    {
        /* INTEGRATION: verify remote_confirm via bts_ssp_f1, derive DHKey (already shared), compute LTK via
         * bts_ssp_f2 or bts_ssp_f3. For now, return placeholder. */
        uint8_t dummy_random[16] = {0};
        return btl_ssp_random(out, tid, dummy_random);
    }
    case BTL_START_ENCRYPTION_REQ:                 /* link established, begin E0 encryption */
    {
        /* INTEGRATION: enable E0 in the WL82 baseband (firmware/src/bt/bt_inq.c sets cryptk + linkcntl).
         * This is a hardware register write; see BT-AUDIO-AIRPODS.md §2.1 and BT-CLASSIC-HW-FACTS.md.
         * For now, return placeholder. */
        return btl_reply(out, BTL_START_ENCRYPTION_RES, tid, op, 0);
    }
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
    case BTL_ENCRYPTION_MODE_RES:
    case BTL_START_ENCRYPTION_RES:
    case BTL_LINK_KEY_RES:
    case BTL_SRES:
    case BTL_ENCAPSULATED_H256:                    /* remote P-256 key: handled in handler case */
    case BTL_ENCRYPTION_KEY_SIZE_RES:
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
    case BTL_LINK_KEY_REQ: return "link_key_req";
    case BTL_LINK_KEY_RES: return "link_key_res";
    case BTL_AU_RAND: return "au_rand";
    case BTL_SRES: return "sres";
    case BTL_IO_CAPABILITY_REQ: return "io_capability_req";
    case BTL_IO_CAPABILITY_RES: return "io_capability_res";
    case BTL_SIMPLE_PAIRING_CONFIRM: return "simple_pairing_confirm";
    case BTL_SIMPLE_PAIRING_RANDOM: return "simple_pairing_random";
    case BTL_ENCAPSULATED_H256: return "encapsulated_h256";
    case BTL_ENCAPSULATED_P192: return "encapsulated_p192";
    case BTL_ENCRYPTION_KEY_SIZE_REQ: return "encryption_key_size_req";
    case BTL_ENCRYPTION_KEY_SIZE_RES: return "encryption_key_size_res";
    case BTL_START_ENCRYPTION_REQ: return "start_encryption_req";
    case BTL_START_ENCRYPTION_RES: return "start_encryption_res";
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_REQ: return "features_req_ext";
    case BTL_ESCAPE << 8 | BTL_X_FEATURES_RES: return "features_res_ext";
    case BTL_ESCAPE << 8 | BTL_X_NOT_ACCEPTED: return "not_accepted_ext";
    default: return "?";
    }
}
