/* SPDX-License-Identifier: GPL-3.0-only */
/* EXPERIMENTAL Bluetooth Classic probe, stages 3 and 4 (BT_INQ_PROBE): L2CAP over the link btpage set up; once LMP is
 * set up, an SDP client looks for an A2DP sink (class 0x110B: its AVDTP PSM), then AVDTP as the initiator: the
 * signalling channel, DISCOVER, GET_CAPABILITIES, SET_CONFIGURATION (SBC), OPEN, the media channel (bt_l2cap.c,
 * bt_avdtp.c). It answers the device's information requests, refuses its connections and its AVDTP commands. Every
 * step is a `btpage: L2CAP` / `SDP` / `AVDTP` line. Included by bt_page.c. No vendor code. */
#include "bt_l2cap.c"
#include "bt_avdtp.c"

#define BA_RXMAX 700u                              /* one L2CAP frame (the default MTU 672 + headers) */
#define BA_SDPMAX 1024u                            /* the attribute lists, all continuations together */
enum { BA_CH_SDP, BA_CH_SIG, BA_CH_MEDIA, BA_NCH };
static const uint16_t ba_cid[BA_NCH] = {0x0040u, 0x0041u, 0x0042u};   /* our channel ids */

struct ba_ch {
    uint16_t psm, dcid;
    uint8_t state, cfg_in, cfg_out;                /* state: 0 closed, 1 connecting, 2 configuring, 3 open */
};

static struct {
    uint8_t rx[BA_RXMAX];
    uint32_t rxn, rxneed;                          /* bytes of the frame so far, its whole length */
    struct ba_ch ch[BA_NCH];
    uint8_t stage, id, label, cont[17], contn, acp, conf[4];
    uint16_t tid, avdtp_psm;
    uint8_t sdp[BA_SDPMAX];
    uint32_t sdpn;
} ba IN_POOL;

enum { BA_IDLE, BA_SDP, BA_SDP_DONE, BA_AV_SIG, BA_AV_DISC, BA_AV_CAPS, BA_AV_CONF, BA_AV_OPEN, BA_AV_MEDIA,
       BA_AV_READY, BA_FAILED };

static void ba_reset(void)
{
    uint32_t i;
    ba.rxn = ba.rxneed = 0;
    ba.stage = BA_IDLE;
    ba.id = 0;
    ba.label = 0;
    ba.sdpn = 0;
    ba.contn = 0;
    for (i = 0; i < BA_NCH; i++)
        ba.ch[i].state = 0;
}

static void ba_send(const uint8_t *frame, uint32_t len)   /* an L2CAP frame in DM1 fragments: LLID 2, then 1 */
{
    uint32_t at = 0, n, k;
    while (at < len) {
        n = len - at > BTL_MAX ? BTL_MAX : len - at;
        if (bp.txw - bp.txr >= BP_TXQ)
            return;
        for (k = 0; k < n; k++)
            bp.txq[bp.txw % BP_TXQ][k] = frame[at + k];
        bp.txl[bp.txw % BP_TXQ] = (uint8_t)n;
        bp.txllid[bp.txw % BP_TXQ] = (uint8_t)(at ? 1u : 2u);
        bp.txw++;
        at += n;
    }
}

static uint32_t ba_le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

static void ba_line(const char *what, uint32_t a, uint32_t b)
{
    con_puts("btpage: ");
    con_puts(what);
    con_puts(" 0x");
    con_hex(a, 4);
    con_puts(" 0x");
    con_hex(b, 4);
    con_puts("\r\n");
}

static void ba_open(uint32_t c, uint32_t psm)      /* an L2CAP channel to psm on our id ba_cid[c] */
{
    uint8_t f[16];
    ba.ch[c].psm = (uint16_t)psm;
    ba.ch[c].state = 1;
    ba.ch[c].cfg_in = ba.ch[c].cfg_out = 0;
    ba_send(f, bl_conn_req(f, ++ba.id, psm, ba_cid[c]));
    ba_line("L2CAP connection request (PSM, our CID)", psm, ba_cid[c]);
}

static int ba_chan(uint32_t ours)                  /* our CID -> the channel, -1 */
{
    uint32_t c;
    for (c = 0; c < BA_NCH; c++)
        if (ba_cid[c] == ours && ba.ch[c].state)
            return (int)c;
    return -1;
}

static void ba_chan_data(uint32_t c, const uint8_t *p, uint32_t n);

/* ---- SDP (stage 3) */
static void ba_sdp_ask(void)
{
    uint8_t f[48];
    ba.tid++;
    ba_send(f, bl_sdp_ssa_req(f, ba.ch[BA_CH_SDP].dcid, ba.tid, 0x110Bu, ba.cont, ba.contn));
    ba_line("SDP search-attribute request for 0x110B (tid, continuation bytes)", ba.tid, ba.contn);
}

static void ba_sdp_report(void)
{
    struct bl_sdp_info in;
    int ok = bl_sdp_parse(ba.sdp, ba.sdpn, 0x110Bu, &in);
    con_puts("btpage: SDP answer ");
    con_dec((int32_t)ba.sdpn);
    con_puts(ok ? " bytes, well formed" : " bytes, NOT well formed");
    bti_kv(", records ", in.records);
    con_puts("\r\n");
    ba.avdtp_psm = 0;
    if (ok && in.records) {
        con_puts("btpage: SDP A2DP sink: class 0x110B ");
        con_puts(in.has_class ? "yes" : "no");
        con_puts(", L2CAP PSM 0x");
        con_hex(in.psm, 4);
        con_puts(", protocol 0x");
        con_hex(in.proto_uuid, 4);
        con_puts(" v");
        con_hex(in.proto_ver, 4);
        con_puts(", profile 0x");
        con_hex(in.profile_uuid, 4);
        con_puts(" v");
        con_hex(in.profile_ver, 4);
        con_puts(", features 0x");
        con_hex(in.features, 4);
        con_puts(", name \"");
        con_puts(in.name);
        con_puts("\"\r\n");
        if (in.has_class && in.proto_uuid == 0x0019u && in.psm)
            ba.avdtp_psm = in.psm;
    }
    ba.stage = BA_SDP_DONE;
}

static void ba_sdp_in(const uint8_t *p, uint32_t n)   /* an SDP PDU on our channel (big-endian) */
{
    uint32_t bytes, k, cn;
    if (n < 5u)
        return;
    if (p[0] != 0x07u) {                           /* not a ServiceSearchAttributeResponse */
        ba_line("SDP PDU (id, length)", p[0], n);
        ba.stage = BA_FAILED;
        return;
    }
    bytes = (uint32_t)p[5] << 8 | p[6];            /* AttributeListsByteCount */
    if (n < 7u + bytes + 1u)
        return;
    for (k = 0; k < bytes && ba.sdpn < BA_SDPMAX; k++)
        ba.sdp[ba.sdpn++] = p[7 + k];
    cn = p[7 + bytes];
    ba.contn = (uint8_t)(cn > 16u ? 16u : cn);
    for (k = 0; k < ba.contn; k++)
        ba.cont[k] = p[8 + bytes + k];
    if (cn)
        ba_sdp_ask();                              /* the next part */
    else
        ba_sdp_report();
}

/* ---- AVDTP (stage 4) */
static void ba_av_cmd(uint32_t sig, const uint8_t *par, uint32_t n)
{
    uint8_t f[40], *pdu = f + 4;
    uint32_t len = bv_cmd(pdu, ba.label = (uint8_t)((ba.label + 1u) & 15u), sig, par, n);
    f[0] = (uint8_t)len, f[1] = 0;
    f[2] = (uint8_t)ba.ch[BA_CH_SIG].dcid, f[3] = (uint8_t)(ba.ch[BA_CH_SIG].dcid >> 8);
    ba_send(f, 4u + len);
    ba_line("AVDTP command (signal, label)", sig, ba.label);
}

static void ba_av_reject(uint32_t label, uint32_t sig)   /* the device's command: rejected (BAD_STATE 0x31) */
{
    uint8_t f[8];
    f[0] = 3, f[1] = 0;
    f[2] = (uint8_t)ba.ch[BA_CH_SIG].dcid, f[3] = (uint8_t)(ba.ch[BA_CH_SIG].dcid >> 8);
    f[4] = (uint8_t)(label << 4 | BV_REJECT);
    f[5] = (uint8_t)sig;
    f[6] = 0x31;
    ba_send(f, 7);
}

static void ba_av_in(const uint8_t *p, uint32_t n)
{
    uint32_t label, msg, sig;
    uint8_t cap[4], par[16];
    if (!bv_hdr(p, n, &label, &msg, &sig)) {
        ba_line("AVDTP packet not read (first byte, length)", n ? p[0] : 0, n);
        return;
    }
    if (msg == BV_CMD) {
        ba_line("AVDTP command from the device (signal, label): rejected", sig, label);
        ba_av_reject(label, sig);
        return;
    }
    if (msg != BV_ACCEPT) {
        ba_line("AVDTP rejected (signal, error)", sig, n > 2u ? p[2] : 0);
        ba.stage = BA_FAILED;
        return;
    }
    if (sig == BV_DISCOVER) {
        ba.acp = (uint8_t)bv_pick_sink(p, n);
        ba_line("AVDTP DISCOVER accepted (endpoints, the free audio sink's SEID)", (n - 2u) / 2u, ba.acp);
        if (!ba.acp)
            return;
        par[0] = (uint8_t)(ba.acp << 2);
        ba_av_cmd(BV_GET_CAPS, par, 1);
        ba.stage = BA_AV_CAPS;
    } else if (sig == BV_GET_CAPS) {
        if (!bv_caps_sbc(p, n, cap) || !bv_sbc_choose(cap, ba.conf)) {
            ba_line("AVDTP GET_CAPABILITIES: no SBC we can use (bytes, 0)", n, 0);
            ba.stage = BA_FAILED;
            return;
        }
        ba_line("AVDTP SBC capabilities (freq|mode block|sub|alloc, bitpool min|max)", cap[0] << 8 | cap[1],
                cap[2] << 8 | cap[3]);
        ba_av_cmd(BV_SET_CONF, par, bv_setconf_par(par, ba.acp, 1u, ba.conf));
        ba_line("AVDTP our SBC configuration (freq|mode block|sub|alloc, bitpool min|max)",
                ba.conf[0] << 8 | ba.conf[1], ba.conf[2] << 8 | ba.conf[3]);
        ba.stage = BA_AV_CONF;
    } else if (sig == BV_SET_CONF) {
        par[0] = (uint8_t)(ba.acp << 2);
        ba_av_cmd(BV_OPEN, par, 1);
        ba.stage = BA_AV_OPEN;
    } else if (sig == BV_OPEN) {
        con_puts("btpage: AVDTP OPEN accepted: opening the media channel\r\n");
        ba_open(BA_CH_MEDIA, ba.avdtp_psm);
        ba.stage = BA_AV_MEDIA;
    } else {
        ba_line("AVDTP accepted (signal, label)", sig, label);
    }
}

static void ba_chan_open(uint32_t c)               /* both configurations done */
{
    ba.ch[c].state = 3;
    ba_line("L2CAP channel open (PSM, the device's CID)", ba.ch[c].psm, ba.ch[c].dcid);
    if (c == BA_CH_SDP) {
        ba.stage = BA_SDP;
        ba_sdp_ask();
    } else if (c == BA_CH_SIG) {
        ba_av_cmd(BV_DISCOVER, 0, 0);
        ba.stage = BA_AV_DISC;
    } else {
        con_puts("btpage: AVDTP STREAM OPEN (signalling and media channels up, SBC configured)\r\n");
        ba.stage = BA_AV_READY;
    }
}

static void ba_chan_data(uint32_t c, const uint8_t *p, uint32_t n)
{
    if (c == BA_CH_SDP)
        ba_sdp_in(p, n);
    else if (c == BA_CH_SIG)
        ba_av_in(p, n);
    else
        ba_line("media channel data (bytes, 0)", n, 0);
}

static void ba_sig_in(const uint8_t *p, uint32_t n)   /* one or more signalling commands */
{
    uint8_t f[32];
    uint32_t at = 0;
    int c;
    while (at + 4u <= n) {
        uint32_t code = p[at], id = p[at + 1], len = ba_le16(p + at + 2);
        const uint8_t *d = p + at + 4;
        if (at + 4u + len > n)
            break;
        if (code == BL_INFO_REQ && len >= 2u) {
            ba_line("L2CAP information request (type), answered", ba_le16(d), id);
            ba_send(f, bl_info_rsp(f, id, ba_le16(d)));
        } else if (code == BL_CONN_REQ && len >= 4u) {
            ba_line("L2CAP connection request from the device (PSM, its CID): refused", ba_le16(d), ba_le16(d + 2));
            ba_send(f, bl_conn_rsp(f, id, 0, ba_le16(d + 2), 2u));
        } else if (code == BL_CONN_RSP && len >= 8u && (c = ba_chan(ba_le16(d + 2))) >= 0) {
            uint32_t res = ba_le16(d + 4);
            ba_line("L2CAP connection response (result, status)", res, ba_le16(d + 6));
            if (res == 0) {
                ba.ch[c].dcid = (uint16_t)ba_le16(d);
                ba.ch[c].state = 2;
                ba_send(f, bl_conf_req(f, ++ba.id, ba.ch[c].dcid, 672u));
            } else if (res != 1u) {
                ba.ch[c].state = 0;
                ba.stage = BA_FAILED;
            }
        } else if (code == BL_CONF_REQ && len >= 4u && (c = ba_chan(ba_le16(d))) >= 0) {
            ba_line("L2CAP configuration request from the device (our CID, flags): accepted", ba_le16(d), ba_le16(d + 2));
            ba_send(f, bl_conf_rsp(f, id, ba.ch[c].dcid, 0));
            ba.ch[c].cfg_in = 1;
            if (ba.ch[c].cfg_out && ba.ch[c].state == 2)
                ba_chan_open((uint32_t)c);
        } else if (code == BL_CONF_RSP && len >= 6u && (c = ba_chan(ba_le16(d))) >= 0) {
            ba_line("L2CAP configuration response (result, our CID)", ba_le16(d + 4), ba_le16(d));
            ba.ch[c].cfg_out = ba_le16(d + 4) == 0;
            if (ba.ch[c].cfg_out && ba.ch[c].cfg_in && ba.ch[c].state == 2)
                ba_chan_open((uint32_t)c);
        } else if (code == BL_DISC_REQ && len >= 4u) {
            ba_line("L2CAP disconnection request (CIDs)", ba_le16(d), ba_le16(d + 2));
            ba_send(f, bl_disc_rsp(f, id, ba_le16(d), ba_le16(d + 2)));
            if ((c = ba_chan(ba_le16(d))) >= 0)
                ba.ch[c].state = 0;
        } else if (code == BL_CMD_REJECT) {
            ba_line("L2CAP command reject (reason, id)", len >= 2u ? ba_le16(d) : 0, id);
        } else if (code == BL_DISC_RSP || code == BL_INFO_RSP || code == BL_CONN_RSP || code == BL_CONF_REQ ||
                   code == BL_CONF_RSP) {
            ba_line("L2CAP signalling for no channel of ours (code, id)", code, id);
        } else {
            ba_line("L2CAP command not understood (code, id): rejected", code, id);
            ba_send(f, bl_cmd_reject(f, id));
        }
        at += 4u + len;
    }
}

static void ba_frame(void)                         /* a whole L2CAP frame in ba.rx */
{
    uint32_t len = ba_le16(ba.rx), cid = ba_le16(ba.rx + 2);
    int c;
    if (cid == BL_CID_SIG)
        ba_sig_in(ba.rx + 4, len);
    else if ((c = ba_chan(cid)) >= 0)
        ba_chan_data((uint32_t)c, ba.rx + 4, len);
    else
        ba_line("L2CAP frame on another channel (CID, length)", cid, len);
}

static void ba_in(const struct bti_rx *r)          /* an ACL fragment: LLID 2 starts a frame, 1 continues it */
{
    uint32_t k;
    if (r->idx == 2u) {
        ba.rxn = 0;
        ba.rxneed = 0;
    } else if (ba.rxn == 0) {
        return;                                    /* (a continuation without its start) */
    }
    for (k = 0; k < r->len && ba.rxn < BA_RXMAX; k++)
        ba.rx[ba.rxn++] = r->data[k];
    if (ba.rxneed == 0 && ba.rxn >= 2u)
        ba.rxneed = 4u + ba_le16(ba.rx);
    if (ba.rxneed && ba.rxn >= ba.rxneed) {
        ba_frame();
        ba.rxn = ba.rxneed = 0;
    }
}

static void ba_poll(void)                          /* the main loop: SDP once LMP is set up, then AVDTP */
{
    if (bti.mode != BTI_M_CONN)
        return;
    if (ba.stage == BA_IDLE && bp.setup_in && bp.setup_out) {
        ba.stage = BA_SDP;
        ba_open(BA_CH_SDP, BL_PSM_SDP);
    } else if (ba.stage == BA_SDP_DONE && ba.avdtp_psm) {
        ba.stage = BA_AV_SIG;
        ba_open(BA_CH_SIG, ba.avdtp_psm);
    }
}
