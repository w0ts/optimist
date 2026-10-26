/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The least AVRCP a sink needs from us (AVRCP 1.6 §6, AVCTP §6, AV/C): the AirPods open the control channel (L2CAP
 * PSM 0x0017) right after the stream starts and play at the volume the source sets, 0/127 until then (a Raspberry
 * Pi's capture, 2026-10-21: the stream open, the AirPods silent). We are the target they ask (their questions
 * answered: the capabilities, a notification, the play status, their key presses accepted) and the controller that
 * sets their volume (bavr_set_volume). Pure: from a frame to a frame, no state; included by bt_acl.c, host-tested
 * in tests/bt_avrcp_test.c. Written from the specifications, no vendor code.
 *   an AVCTP frame: label (4 bits) | packet type (2, single: 0) | C/R (1) | IPID (1); the profile id (2, big-endian)
 *   an AV/C frame:  ctype; subunit (panel: 0x48); opcode; the operands
 *   vendor dependent (opcode 0): company id (3: the SIG's 00 19 58), PDU id, packet type (1, single: 0), parameter
 *   length (2, big-endian), the parameters */
#define BAVR_PID 0x110Eu
#define BAVR_SUBUNIT 0x48u                         /* panel, id 0 */
#define BAVR_OP_VENDOR 0x00u
#define BAVR_OP_PASSTHROUGH 0x7Cu
enum { BAVR_CT_CONTROL = 0, BAVR_CT_STATUS = 1, BAVR_CT_NOTIFY = 3 };                       /* commands */
enum { BAVR_RS_NOT_IMPLEMENTED = 8, BAVR_RS_ACCEPTED = 9, BAVR_RS_REJECTED = 10, BAVR_RS_STABLE = 12,
       BAVR_RS_INTERIM = 15 };                                                             /* responses */
enum { BAVR_PDU_GET_CAPS = 0x10, BAVR_PDU_GET_PLAY_STATUS = 0x30, BAVR_PDU_REGISTER_NOTIFICATION = 0x31,
       BAVR_PDU_SET_ABSOLUTE_VOLUME = 0x50 };
enum { BAVR_EV_PLAYBACK_STATUS = 1 };
#define BAVR_PLAYING 1u
#define BAVR_MAX 64u                               /* the longest frame here (room a caller gives) */

static uint32_t bavr_head(uint8_t *out, uint32_t label, uint32_t response, uint32_t ctype)
{
    out[0] = (uint8_t)(label << 4 | (response ? 2u : 0u));
    out[1] = (uint8_t)(BAVR_PID >> 8), out[2] = (uint8_t)BAVR_PID;
    out[3] = (uint8_t)ctype;
    out[4] = BAVR_SUBUNIT;
    return 5;
}
static uint32_t bavr_vendor(uint8_t *out, uint32_t pdu, uint32_t plen)   /* after the head: opcode, company, PDU */
{
    out[5] = BAVR_OP_VENDOR;
    out[6] = 0x00, out[7] = 0x19, out[8] = 0x58;
    out[9] = (uint8_t)pdu;
    out[10] = 0;
    out[11] = (uint8_t)(plen >> 8), out[12] = (uint8_t)plen;
    return 13;
}

/* our command: the sink's absolute volume (0..127) -> the frame's length */
static uint32_t bavr_set_volume(uint8_t *out, uint32_t label, uint32_t volume)
{
    bavr_head(out, label, 0, BAVR_CT_CONTROL);
    bavr_vendor(out, BAVR_PDU_SET_ABSOLUTE_VOLUME, 1);
    out[13] = (uint8_t)(volume > 127u ? 127u : volume);
    return 14;
}

static uint32_t bavr_reject(uint8_t *out, uint32_t label, uint32_t pdu, uint32_t err)
{
    bavr_head(out, label, 1, BAVR_RS_REJECTED);
    bavr_vendor(out, pdu, 1);
    out[13] = (uint8_t)err;                        /* 0 invalid command, 1 invalid parameter */
    return 14;
}

/* a frame from the sink (an AVCTP frame, n bytes) -> our answer in out (BAVR_MAX) -> its length; 0 nothing to send
 * (its answers to our commands, fragments, another profile) */
static uint32_t bavr_in(const uint8_t *f, uint32_t n, uint8_t *out)
{
    uint32_t label, ctype, op, pdu, plen, k;
    if (n < 6u || (f[0] & 0x0Cu) || (f[0] & 0x02u))
        return 0;                                  /* (short, fragmented, or a response) */
    label = f[0] >> 4;
    if ((f[1] << 8 | f[2]) != BAVR_PID) {          /* not AVRCP: an invalid profile id back (AVCTP 6.1.1) */
        out[0] = (uint8_t)(label << 4 | 3u), out[1] = f[1], out[2] = f[2];
        return 3;
    }
    ctype = f[3] & 15u, op = f[5];
    if (op == BAVR_OP_PASSTHROUGH) {               /* a key (play, pause, next...): accepted, as pressed (§6.13) */
        if (n < 8u)
            return 0;
        bavr_head(out, label, 1, BAVR_RS_ACCEPTED);
        out[5] = BAVR_OP_PASSTHROUGH, out[6] = f[6], out[7] = 0;
        return 8;
    }
    if (op != BAVR_OP_VENDOR || n < 13u || f[6] != 0x00u || f[7] != 0x19u || f[8] != 0x58u) {
        bavr_head(out, label, 1, BAVR_RS_NOT_IMPLEMENTED);   /* (another opcode or company: echoed back) */
        for (k = 5; k < n && k < BAVR_MAX; k++)
            out[k] = f[k];
        return k;
    }
    pdu = f[9], plen = (uint32_t)f[11] << 8 | f[12];
    if (13u + plen > n)
        return bavr_reject(out, label, pdu, 0);
    if (pdu == BAVR_PDU_GET_CAPS && ctype == BAVR_CT_STATUS) {   /* the company ids, or the events we notify */
        if (plen < 1u || (f[13] != 2u && f[13] != 3u))
            return bavr_reject(out, label, pdu, 1);
        bavr_head(out, label, 1, BAVR_RS_STABLE);
        if (f[13] == 2u) {
            bavr_vendor(out, pdu, 5);
            out[13] = 2, out[14] = 1, out[15] = 0x00, out[16] = 0x19, out[17] = 0x58;
            return 18;
        }
        bavr_vendor(out, pdu, 3);
        out[13] = 3, out[14] = 1, out[15] = BAVR_EV_PLAYBACK_STATUS;
        return 16;
    }
    if (pdu == BAVR_PDU_REGISTER_NOTIFICATION && ctype == BAVR_CT_NOTIFY) {   /* the play status: playing, for good */
        if (plen < 5u || f[13] != BAVR_EV_PLAYBACK_STATUS)
            return bavr_reject(out, label, pdu, 1);
        bavr_head(out, label, 1, BAVR_RS_INTERIM);
        bavr_vendor(out, pdu, 2);
        out[13] = BAVR_EV_PLAYBACK_STATUS, out[14] = BAVR_PLAYING;
        return 15;
    }
    if (pdu == BAVR_PDU_GET_PLAY_STATUS && ctype == BAVR_CT_STATUS) {   /* no song: length and position unknown */
        bavr_head(out, label, 1, BAVR_RS_STABLE);
        bavr_vendor(out, pdu, 9);
        for (k = 13; k < 21u; k++)
            out[k] = 0xFF;
        out[21] = BAVR_PLAYING;
        return 22;
    }
    return bavr_reject(out, label, pdu, 0);        /* (the rest of AVRCP: not ours) */
}
