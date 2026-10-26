/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* EXPERIMENTAL Bluetooth Classic probe, stages 3 and 4 (BT_AUDIO): L2CAP over the link btpage set up; once LMP is
 * set up, an SDP client looks for an A2DP sink (class 0x110B: its AVDTP PSM), then AVDTP as the initiator: the
 * signalling channel, DISCOVER, GET_CAPABILITIES, SET_CONFIGURATION (SBC), OPEN, the media channel (bt_l2cap.c,
 * bt_avdtp.c). It answers the device's information requests and its SDP channel (bt_sdps.c: our A2DP source record),
 * refuses its other connections and its AVDTP commands. Every
 * step is a `btpage: L2CAP` / `SDP` / `AVDTP` line. Included by bt_page.c. No vendor code. */
#include "bt_l2cap.c"
#include "bt_sdps.c"                               /* our SDP server: the device reads our record (the AirPods do) */
#include "bt_avrcp.c"                              /* the least AVRCP: the device's control channel, its volume set */
#include "bt_avdtp.c"

#define BA_RXMAX 700u                              /* one L2CAP frame (the default MTU 672 + headers) */
#define BA_SDPMAX 1024u                            /* the attribute lists, all continuations together */
enum { BA_CH_SDP, BA_CH_SIG, BA_CH_MEDIA, BA_CH_SDPS, BA_CH_AVC, BA_NCH };   /* SDPS: the device's SDP channel to our
                                                    * server; AVC: its AVRCP control channel (AVCTP, PSM 0x0017) */
static const uint16_t ba_cid[BA_NCH] = {0x0040u, 0x0041u, 0x0042u, 0x0043u, 0x0044u};   /* our channel ids */
#define BL_PSM_AVCTP 0x0017u
#define BA_VOLUME 90u                              /* the sink's absolute volume we set (0..127): the AirPods play at
                                                    * 0/127 until the source sets it (a Raspberry Pi's capture, 2026-10-21) */

struct ba_ch {
    uint16_t psm, dcid;
    uint8_t state, cfg_in, cfg_out;                /* state: 0 closed, 1 connecting, 2 configuring, 3 open */
};

static struct {
    uint8_t rx[BA_RXMAX];
    uint32_t rxn, rxneed;                          /* bytes of the frame so far, its whole length */
    struct ba_ch ch[BA_NCH];
    uint8_t stage, id, label, cont[17], contn, acp, conf[4];
    uint32_t t0;                                   /* fm1_ms: the set-up's start (BA_SETUP_MS: given up) */
    uint32_t sdps_ms;                              /* fm1_ms: the device's last SDP request to us */
    uint32_t auth_ms;                              /* fm1_ms: our authentication done, the device's awaited (BA_AUTH_WAIT_MS) */
    uint8_t avc_label;                             /* our AVCTP transaction label (4 bits) */
    uint8_t fresh;                                 /* this link paired afresh: our search, then let go and connect again
                                                    * with the key (dedicated bonding, as BlueZ) */
    uint8_t rejoin;                                /* let go on purpose to connect again: no "lost" (bt_audio.c) */
    uint8_t gave_up;                               /* BA_FAILED by BA_SETUP_MS, not by the device: the screen says TIMED
                                                    * OUT, not REFUSED (bt_audio.c bta_gave_up) */
    uint8_t paused;                                /* the stream suspended: the FM-1's own output plays (SHIFT + the
                                                    * volume knob, bt_audio.c bta_output) */
    uint16_t tid, avdtp_psm;
    uint8_t sdp[BA_SDPMAX];
    uint32_t sdpn;
} ba IN_POOL;

enum { BA_IDLE, BA_SDP, BA_SDP_DONE, BA_AV_SIG, BA_AV_DISC, BA_AV_CAPS, BA_AV_CONF, BA_AV_OPEN, BA_AV_MEDIA,
       BA_AV_START, BA_AV_READY, BA_FAILED };

static void ba_reset(void)
{
    uint32_t i;
    ba.rxn = ba.rxneed = 0;
    ba.stage = BA_IDLE;
    ba.id = 0;
    ba.label = 0;
    ba.sdpn = 0;
    ba.contn = 0;
    ba.paused = 0;
    ba.gave_up = 0;
    ba.sdps_ms = 0;
    ba.fresh = 0;
    ba.rejoin = 0;
    ba.auth_ms = 0;
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
static void ba_avc_volume(uint32_t v);
static void ba_avc_in(const uint8_t *p, uint32_t n);

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
    {                                              /* our search over: the channel closed, as BlueZ does. The AirPods
                                                    * opened theirs to us 1.8 s after a Raspberry Pi closed its own
                                                    * (2026-10-20); the response comes for no channel of ours */
        uint8_t f[16];
        ba_send(f, bl_disc_req(f, ++ba.id, ba.ch[BA_CH_SDP].dcid, ba_cid[BA_CH_SDP]));
        ba.ch[BA_CH_SDP].state = 0;
        con_puts("btpage: SDP search over: our channel closed\r\n");
    }
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
    } else if (sig == BV_START) {
        con_puts("btpage: AVDTP STREAM OPEN (signalling and media channels up, stream started)\r\n");
        ba.stage = BA_AV_READY;
        ba.paused = 0;                             /* (a START after a SUSPEND: Bluetooth again) */
    } else {
        ba_line("AVDTP accepted (signal, label)", sig, label);
    }
}

static void ba_chan_open(uint32_t c)               /* both configurations done */
{
    ba.ch[c].state = 3;
    ba_line("L2CAP channel open (PSM, the device's CID)", ba.ch[c].psm, ba.ch[c].dcid);
    if (c == BA_CH_SDPS) {
        return;                                    /* (the device's: its requests come, bsd_answer answers) */
    } else if (c == BA_CH_AVC) {
        ba_avc_volume(BA_VOLUME);                  /* (its control channel up: its volume, else 0 on the AirPods) */
        return;
    } else if (c == BA_CH_SDP) {
        ba.stage = BA_SDP;
        ba_sdp_ask();
    } else if (c == BA_CH_SIG) {
        ba_av_cmd(BV_DISCOVER, 0, 0);
        ba.stage = BA_AV_DISC;
    } else {
        uint8_t par[1];
        con_puts("btpage: AVDTP media channel open: sending START\r\n");
        par[0] = (uint8_t)(ba.acp << 2);
        ba_av_cmd(BV_START, par, 1);
        ba.stage = BA_AV_START;
    }
}

#if FELUCCA_BT_AUDIO                               /* the media channel: BT_AUDIO only (the probe stops at the channel's set-up) */
#include "bt_sbc.h"
#include "bt_sbc.c"                                /* the SBC encoder the media channel sends (bt_audio_feed) */
static uint16_t ba_rtp_seq;
static uint32_t ba_rtp_ts;
static struct bt_sbc_ctx ba_sbc;
static int16_t ba_pcm_acc[BT_SBC_SAMPLES_PER_FRAME * 2];
static uint32_t ba_pcm_cnt;
static uint8_t ba_sbc_init_done;

#define BA_MFRAMES 3u                              /* SBC frames a packet: 3 x 105 B (bitpool 46) + 17 B of L2CAP and
                                                    * media headers in one DH5 (339 B): 115 DH5 a second instead of 172
                                                    * at bitpool 53 (2 x 119 B), a third of the air time left for the
                                                    * retransmissions (2026-10-21) */
#define BA_BITPOOL 46u                             /* the most that fits BA_MFRAMES frames in a DH5 */
/* the queue's slot (bt_page.c BP_MQB) holds the longest packet: the headers and BA_MFRAMES joint-stereo frames at
 * BA_BITPOOL (bt_sbc_frame_len: 4 + 8 + ceil((8 + 16 x bitpool) / 8)); a lower agreed bitpool, or plain stereo, is shorter */
_Static_assert(4u + 12u + 1u + BA_MFRAMES * (12u + (8u + 16u * BA_BITPOOL + 7u) / 8u) == BP_MQB && BP_MQB <= BP_TXB,
               "bt_page.c BP_MQB: the longest audio packet");
static uint8_t ba_mbuf[BA_MFRAMES * 128u];
static uint32_t ba_mlen, ba_mn;
static uint8_t ba_live;                            /* bt_audio_feed streams (the audio ISR's) */
static uint32_t ba_qavg;                           /* the audio packets queued (bt_page.c bp_mq), mean over ~0.7 s, Q8 */

/* how much later than the FM-1's own output the Bluetooth stream leaves us, TIMER4 ticks; -1 not streaming
 * (io/midi/clock_sync.c sync_lead_t: a clock followed that much earlier). Ours only: a sample waits half a packet
 * for its SBC frames, then the queue's mean depth of packets, less the DMA's half buffer sync_out_t counts. The
 * device's own buffer is not known here (no AVDTP delay report): GLOBAL > MIDI BT OFS is for it */
static int32_t bta_lat_t(void)
{
    uint32_t pk = (bp.slots5 == 2u ? BA_MFRAMES : 1u) * BT_SBC_SAMPLES_PER_FRAME;
    int32_t smp = (int32_t)(pk / 2u + ((ba_qavg * pk) >> 8)) - HALF_FRAMES;
    return ba_live ? (int32_t)(((int64_t)smp * SY_TPS_Q8) >> 8) : -1;
}
static void ba_media_send(const uint8_t *sbc_frame, uint32_t sbc_len, uint32_t frames)   /* one L2CAP frame, whole */
{
    uint8_t pkt[4 + 12 + 1 + BA_MFRAMES * 128u];
    uint32_t payload_len = 12u + 1u + sbc_len;
    uint32_t i;
    if (ba.stage != BA_AV_READY || !ba.ch[BA_CH_MEDIA].dcid || sbc_len > BA_MFRAMES * 128u)
        return;
    pkt[0] = (uint8_t)(payload_len & 0xFFu);
    pkt[1] = (uint8_t)(payload_len >> 8);
    pkt[2] = (uint8_t)(ba.ch[BA_CH_MEDIA].dcid & 0xFFu);
    pkt[3] = (uint8_t)(ba.ch[BA_CH_MEDIA].dcid >> 8);
    pkt[4] = 0x80u;
    pkt[5] = 0x60u;
    pkt[6] = (uint8_t)(ba_rtp_seq >> 8);
    pkt[7] = (uint8_t)(ba_rtp_seq & 0xFFu);
    ba_rtp_seq++;
    pkt[8] = (uint8_t)(ba_rtp_ts >> 24);
    pkt[9] = (uint8_t)((ba_rtp_ts >> 16) & 0xFFu);
    pkt[10] = (uint8_t)((ba_rtp_ts >> 8) & 0xFFu);
    pkt[11] = (uint8_t)(ba_rtp_ts & 0xFFu);
    ba_rtp_ts += 128u * frames;
    pkt[12] = 0x01u;
    pkt[13] = 0x02u;
    pkt[14] = 0x03u;
    pkt[15] = 0x04u;
    pkt[16] = (uint8_t)frames;                     /* (the media payload header: the frames in the packet) */
    for (i = 0; i < sbc_len; i++)
        pkt[17 + i] = sbc_frame[i];
    (void)bp_media_queue(pkt, 4u + payload_len);   /* (whole, one DH5; full: dropped whole, never cut) */
}

int bt_audio_feed(const int32_t *q15_pcm, uint32_t n)   /* 1: it streams (audio.c mutes the FM-1's own output) */
{
    uint32_t i;
    if (ba.stage != BA_AV_READY || ba.paused || !ba.ch[BA_CH_MEDIA].dcid || bti.mode != BTI_M_CONN ||
        bti_st.phase != BTI_INQ_RUN || bp.stopping) {      /* (the run over or being let go: bti_run_stop keeps the mode
                                                    * and the stage, so the FM-1's own output stayed muted after a
                                                    * disconnect, the user 2026-10-20) */
        ba_pcm_cnt = 0;
        ba_live = 0;
        return 0;
    }
    if (!ba_live)
        ba_qavg = (bp_mq.w - bp_mq.r) << 8;        /* (the mean from the depth now) */
    ba_live = 1;
    ba_qavg += ((int32_t)((bp_mq.w - bp_mq.r) << 8) - (int32_t)ba_qavg) >> 9;
    if (!ba_sbc_init_done) {
        uint8_t bp_max = ba.conf[3] < BA_BITPOOL ? ba.conf[3] : (uint8_t)BA_BITPOOL;   /* (the agreed range) */
        bt_sbc_init(&ba_sbc, 44100, bp_max < ba.conf[2] ? ba.conf[2] : bp_max, 1);
        ba_sbc_init_done = 1;
    }
    for (i = 0; i < n; i++) {
        int32_t l = q15_pcm[2u * i];
        int32_t r = q15_pcm[2u * i + 1u];
        if (l < -32768) l = -32768; else if (l > 32767) l = 32767;
        if (r < -32768) r = -32768; else if (r > 32767) r = 32767;
        ba_pcm_acc[2u * ba_pcm_cnt] = (int16_t)l;
        ba_pcm_acc[2u * ba_pcm_cnt + 1u] = (int16_t)r;
        ba_pcm_cnt++;
        if (ba_pcm_cnt >= BT_SBC_SAMPLES_PER_FRAME) {
            uint8_t frame[128];
            uint32_t slen = bt_sbc_encode(&ba_sbc, ba_pcm_acc, frame), k;
            for (k = 0; k < slen && ba_mlen + k < sizeof ba_mbuf; k++)
                ba_mbuf[ba_mlen + k] = frame[k];
            ba_mlen += k;
            if (++ba_mn == (bp.slots5 == 2u ? BA_MFRAMES : 1u)) {   /* (DH5: BA_MFRAMES frames; DH3: one) */
                ba_media_send(ba_mbuf, ba_mlen, ba_mn);
                ba_mlen = ba_mn = 0;
            }
            ba_pcm_cnt = 0;
        }
    }
    return 1;
}
#endif

static void ba_sdps_in(const uint8_t *p, uint32_t n)   /* an SDP request from the device: our answer */
{
    uint8_t f[4 + BSD_OUT];
    uint32_t len = bsd_answer(p, n, f + 4);
    ba.sdps_ms = fm1_ms;
    ba_line("SDP request from the device (PDU, our answer's bytes)", n ? p[0] : 0, len);
    {                                              /* (its bytes: the UUIDs it searches, the attributes it wants) */
        uint32_t k;
        con_puts("btpage: SDP request bytes");
        for (k = 0; k < n && k < 40u; k++) {
            con_puts(" ");
            con_hex(p[k], 2);
        }
        con_puts("\r\n");
    }
    if (!len)
        return;
    f[0] = (uint8_t)len, f[1] = (uint8_t)(len >> 8);
    f[2] = (uint8_t)ba.ch[BA_CH_SDPS].dcid, f[3] = (uint8_t)(ba.ch[BA_CH_SDPS].dcid >> 8);
    ba_send(f, 4u + len);
}

static void ba_chan_data(uint32_t c, const uint8_t *p, uint32_t n)
{
    if (c == BA_CH_SDPS)
        ba_sdps_in(p, n);
    else if (c == BA_CH_AVC)
        ba_avc_in(p, n);
    else if (c == BA_CH_SDP)
        ba_sdp_in(p, n);
    else if (c == BA_CH_SIG)
        ba_av_in(p, n);
    else
        ba_line("media channel data (bytes, 0)", n, 0);
}

static void ba_accept(uint32_t c, uint32_t id, const uint8_t *d, uint8_t *f, const char *what)   /* its connection
                                                    * request (PSM, its CID at d) for our channel c: taken, configured */
{
    ba_line("L2CAP connection request from the device (PSM, its CID): ", ba_le16(d), ba_le16(d + 2));
    con_puts("btpage: ... ");
    con_puts(what);
    con_puts("\r\n");
    ba.ch[c].psm = (uint16_t)ba_le16(d);
    ba.ch[c].dcid = (uint16_t)ba_le16(d + 2);
    ba.ch[c].state = 2;
    ba.ch[c].cfg_in = ba.ch[c].cfg_out = 0;
    ba_send(f, bl_conn_rsp(f, id, ba_cid[c], ba.ch[c].dcid, 0));
    ba_send(f, bl_conf_req(f, ++ba.id, ba.ch[c].dcid, 672u));
}

static void ba_avc_volume(uint32_t v)              /* the sink's absolute volume, on its control channel when open */
{
    uint8_t f[4 + BAVR_MAX];
    uint32_t len;
    if (ba.ch[BA_CH_AVC].state != 3u)
        return;
    len = bavr_set_volume(f + 4, ba.avc_label = (uint8_t)((ba.avc_label + 1u) & 15u), v);
    f[0] = (uint8_t)len, f[1] = 0;
    f[2] = (uint8_t)ba.ch[BA_CH_AVC].dcid, f[3] = (uint8_t)(ba.ch[BA_CH_AVC].dcid >> 8);
    ba_send(f, 4u + len);
    ba_line("AVRCP SetAbsoluteVolume (volume of 127, label)", v, ba.avc_label);
}

static void ba_avc_in(const uint8_t *p, uint32_t n)   /* a frame on its control channel: answered (bt_avrcp.c) */
{
    uint8_t f[4 + BAVR_MAX];
    uint32_t len = bavr_in(p, n, f + 4);
    ba_line("AVRCP from the device (its first bytes, our answer's bytes)", n > 5u ? (uint32_t)p[3] << 8 | p[5] : 0, len);
    if (!len)
        return;
    f[0] = (uint8_t)len, f[1] = 0;
    f[2] = (uint8_t)ba.ch[BA_CH_AVC].dcid, f[3] = (uint8_t)(ba.ch[BA_CH_AVC].dcid >> 8);
    ba_send(f, 4u + len);
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
        } else if (code == BL_CONN_REQ && len >= 4u && ba_le16(d) == BL_PSM_SDP && !ba.ch[BA_CH_SDPS].state) {
            ba_accept(BA_CH_SDPS, id, d, f, "accepted, our SDP server");
        } else if (code == BL_CONN_REQ && len >= 4u && ba_le16(d) == BL_PSM_AVCTP && !ba.ch[BA_CH_AVC].state) {
            ba_accept(BA_CH_AVC, id, d, f, "accepted, AVRCP (its control channel: our volume goes there)");
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
            } else if (res == 4u && c == BA_CH_SIG) {
                /* (no resources: the AirPods 4 say it, then pending, then success to the same request, reported by
                 * Google to Apple: developer.apple.com/forums/thread/816622. Kept waiting) */
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

#define BA_NAME_WAIT_MS 1500u                      /* the device's name read before the pairing, or this long after the link */
#define BA_AUTH_WAIT_MS 1000u                      /* the device's authentication of us awaited this long before the
                                                    * encryption (bt_ssp.c: the AirPods do one, not every device may) */
#define BA_SETUP_MS 20000u                         /* the link up to the stream started: else given up, said */
/* the main loop: SDP once LMP is set up; then the pairing and the encryption, ours to start unless the device started
 * them (the FM-1, 2026-10-20: the WH-1000XM4 answered the AVDTP channel "pending: authentication", then "security
 * block"); then AVDTP */
static void ba_poll(void)
{
    if (bti.mode != BTI_M_CONN)
        return;
    if (bp_ssp.init && bp_ssp.st == BTP_AUTHED) {   /* ours done, its authentication of us awaited (bt_ssp.c) */
        if (!ba.auth_ms)
            ba.auth_ms = fm1_ms | 1u;
        else if (fm1_ms - ba.auth_ms >= BA_AUTH_WAIT_MS) {
            con_puts("btpage: the device did not authenticate us: the encryption anyway\r\n");
            btp_i_encrypt(&bp_ssp);
        }
    }
    if (ba.stage < BA_AV_READY && ba.stage != BA_FAILED && fm1_ms - bp.conn_ms >= BA_SETUP_MS) {   /* (first, from the link
                                                    * up, whatever the step: a pairing that never ends returns below each
                                                    * pass. 2026-10-20: SETTING UP for minutes; the device a Mac, "L..O",
                                                    * whose user was asked to confirm the pairing) */
        ba_line("set-up given up (stage, pairing state)", ba.stage, bp_ssp.st);
        ba.stage = BA_FAILED;
        ba.gave_up = 1;
        bp_leave();                                /* (and the link let go: the device free for the next try) */
        return;
    }
    if (ba.stage == BA_IDLE && bp.setup_in && bp.setup_out) {
        if (!bp.name_done && fm1_ms - bp.conn_ms < BA_NAME_WAIT_MS)
            return;                                /* (its name first, as BlueZ: bt_page.c) */
        /* the order a Raspberry Pi (BlueZ) took with the AirPods, 2026-10-20: pairing and encryption, our SDP search,
         * our channel closed; then the device's SDP questions to us; then the AVDTP channel. Searched before the
         * pairing, the AirPods were left with an idle encrypted link and hung up (0x13) */
        if (bp_ssp.st == BTP_IDLE) {
            con_puts("btpage: pairing: we start it (Secure Simple Pairing, Just Works), then the encryption\r\n");
            btp_start(&bp_ssp);
            return;
        }
        if (bp_ssp.st == BTP_PAIRED && bp_ssp.bonded) {
            con_puts("btpage: bonded: authenticating with the saved link key, then the encryption\r\n");
            btp_auth(&bp_ssp);
            return;
        }
        if (bp_ssp.st == BTP_FAILED && bp_ssp.bonded) {   /* (the device lost our key: a fresh pairing) */
            ba_line("the saved key refused (reason, 0): pairing afresh", bp_ssp.reason, 0);
            if (bta_mem_of(bp.remote))
                bta_mem_of(bp.remote)->bonded = 0;
            btp_begin(&bp_ssp, bti.own, bp.remote, bp_rng, bp_ssp_send, 0);
            btp_start(&bp_ssp);
            return;
        }
        if (bp_ssp.st == BTP_FAILED) {
            ba_line("pairing failed (reason, 0)", bp_ssp.reason, 0);
            ba.stage = BA_FAILED;
            return;
        }
        if (bp_ssp.st != BTP_ENCRYPTED)
            return;                                /* (under way: ours or the device's) */
        if (!bp_ssp.bonded && bta_mem_of(bp.remote)) {   /* a fresh pairing of a remembered device: its key kept */
            struct bta_mem *m = bta_mem_of(bp.remote);
            uint32_t k;
            for (k = 0; k < 16u; k++)
                m->lk[k] = bp_ssp.lk[k];
            m->bonded = 1;
            bta.key_new = 1;
            ba.fresh = 1;
            con_puts("btpage: bonded: the link key kept for the next connection\r\n");
        }
        con_puts("btpage: paired and encrypted: L2CAP information asked, our SDP search for the audio sink\r\n");
        {                                          /* (as BlueZ, before anything: its extended features, its fixed
                                                    * channels; the answers come as "for no channel of ours") */
            uint8_t f[16];
            ba_send(f, bl_info_req(f, ++ba.id, 2));
            ba_send(f, bl_info_req(f, ++ba.id, 3));
        }
        ba.stage = BA_SDP;
        ba.t0 = fm1_ms;
        ba_open(BA_CH_SDP, BL_PSM_SDP);
    } else if (ba.stage == BA_SDP_DONE && ba.avdtp_psm && ba.fresh) {
        /* paired on this link: let go and connect again with the key, as a Raspberry Pi did (BlueZ "pair", then
         * "connect"): the AirPods gave it the audio channel on the second link; on the first, ours stayed "pending" */
        con_puts("btpage: paired: letting go, connecting again with the key (as BlueZ)\r\n");
        ba.fresh = 0;
        ba.rejoin = 1;                             /* (nothing more on this link; the screen pages again: op_bt.c) */
        bp_leave();
    } else if (ba.stage == BA_SDP_DONE && ba.avdtp_psm && !ba.rejoin) {
        /* at once, as an Android phone does (developer.apple.com/forums/thread/816622): the AirPods ask their SDP
         * questions after our request, then answer it. Waiting for them instead, they hung up the idle link after
         * ~3 s (0x13, twice, 2026-10-20) */
        con_puts("btpage: opening the audio channel\r\n");
        ba.stage = BA_AV_SIG;
        ba_open(BA_CH_SIG, ba.avdtp_psm);
    }
}
