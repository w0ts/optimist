/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The audio link's TX side (docs/BT-CLASSIC.md §5.5, §7): what goes into the engine's two TX buffers, and the AFH switch
 * at its instant. Included by bt_page.c; portable C, host-tested (tests/bt_ltx_test.c, the CPU-load stress model).
 *
 *   blt_put(pdu, n, llid)   the main loop queues an LMP PDU (LLID 3) or an L2CAP fragment (2 start, 1 continuation)
 *   blt_publish()           the main loop, once the handler that queued is over (bt_inq.c bti_poll, bp_tx_service):
 *                           the queued PDUs may go (an E0 key loaded after its PDU was queued goes first, bp_e0)
 *   blt_fill(f, tab)        the hand-over: the published signalling first (DM1), else the stream's packets (DH5 / DH3 /
 *                           DH1). From IRQ 40 (each reception and the 18-slot alarm, priority 5, above the audio
 *                           render) and from the main loop with IRQ 40 held
 *   blt_afh_stage(..)       the main loop: the next hop set (table, map words, on / off), its LMP_set_AFH queued
 *   blt_afh_alarm(now, ..)  the 18-slot alarm: at the instant the hop set goes to the engine -> slots to the next alarm
 *
 * Why the signalling goes from IRQ 40 and the instant is set at the hand-over (2026-11-01, the user: "with high cpu, bt
 * audio was dropping connection a few times"; CPU0 at 100 %): before, the main loop put one signalling PDU a pass into
 * a TX buffer, and LMP_set_AFH's instant (320 slots, 200 ms) was fixed when the PDU was queued. The AFH assessment runs
 * every 30 s and moved the hop set 11 times in 13 minutes on the XM4 (2026-10-23). With the audio render holding CPU0,
 * the main loop's passes come hundreds of ms apart: the PDU left after its instant, our side switched alone (the alarm
 * is IRQ 40's, on time), the headset hopped on the old set and the link died (no good packet for 4 s, or the
 * headset's own supervision timeout). The model in tests/bt_ltx_test.c shows it; on the FM-1 it is inferred, not seen in
 * a log (no BT_DIAG since 2026-10-29). Now: the instant is the hand-over's clock + 320 slots, wherever the main loop is,
 * and the table and the map registers are written at the instant (as the vendor controller does, btctrler.a
 * bdhw_set_afh from a slot timer), not at the announcement over a hop set still in use.
 *
 * The includer defines BLT_CLKN() (the slot clock), BLT_AFH_MAP(words) (map registers 0) and the TX buffer offsets
 * BLT_OFF_TX0 / 1 (DM1) and BLT_OFF_TXB0 / 1 (DH5). */
#include <stdint.h>
#include "fm1_bredr_frame.h"
#include "bt_lmp.h"

#ifndef BLT_IN_POOL
#define BLT_IN_POOL
#endif
#define BLT_Q 32u                                  /* signalling PDUs queued (DM1 payloads) */
#define BLT_MQ 40u                                 /* the audio packets queued (~350 ms of the stream at 3 SBC frames a
                                                    * packet; 4 = 23 ms overflowed on every radio hiccup: audio_drop
                                                    * 11 %, heard as crackles, 2026-10-21; 16 = 140 ms overflowed on the
                                                    * XM4's 100-150 ms refusals of our DH5s while it kept answering,
                                                    * 2026-10-23: a phone's sink buffer rides those out, so does this) */
#define BLT_MQB 332u                               /* the longest audio packet queued: 17 B of L2CAP and media headers and
                                                    * BA_MFRAMES SBC frames at BA_BITPOOL (bt_acl.c asserts the sum; a
                                                    * lower agreed bitpool makes shorter ones); 7 B a slot less than a DH5 */
#define BLT_AFH_AHEAD 320u                         /* the switch this many slots (200 ms) after the hand-over */
#define BLT_CLK 0x07FFFFFFu                        /* 27 bits of slots (FM1_BR_CLK_MASK) */
#define BLT_AFH_OP ((uint8_t)(BTL_SET_AFH << 1))   /* LMP_set_AFH's first byte (the master's, TID 0) */

static struct {
    uint8_t q[BLT_Q][BTL_MAX], ql[BLT_Q], qllid[BLT_Q];
    volatile uint32_t w, pub, r;                   /* queued (main loop), may go (main loop), handed over (IRQ 40) */
    uint8_t afh_tab[80];                           /* the next hop set: frq_table[1]'s content, the map words, on / off */
    uint32_t afh_words[3];
    uint8_t afh_on;
    volatile uint8_t afh_state;                    /* BLT_AFH_*: none, queued (no instant yet), due at afh_at */
    volatile uint32_t afh_at;                      /* the instant, CLKN slots (even: a master-to-slave slot) */
} blt;
enum { BLT_AFH_NONE, BLT_AFH_QUEUED, BLT_AFH_DUE };

/* the audio packets (bt_acl.c ba_media_send, from the audio interrupt): whole L2CAP frames, each one DH5 (or DH3 / DH1
 * when shorter), never cut */
static struct {
    uint8_t d[BLT_MQ][BLT_MQB];
    uint16_t n[BLT_MQ];
    volatile uint32_t w, r;
} bp_mq BLT_IN_POOL;

static void blt_reset(void)                        /* a new link (IRQ 40 held) */
{
    blt.w = blt.pub = blt.r = 0;
    blt.afh_state = BLT_AFH_NONE;
    bp_mq.r = bp_mq.w;
}
static int blt_put(const uint8_t *pdu, uint32_t len, uint32_t llid)   /* the main loop -> 1 queued */
{
    uint32_t i, k = blt.w % BLT_Q;
    if (blt.w - blt.r >= BLT_Q || len == 0 || len > BTL_MAX)
        return 0;
    for (i = 0; i < len; i++)
        blt.q[k][i] = pdu[i];
    blt.ql[k] = (uint8_t)len;
    blt.qllid[k] = (uint8_t)llid;
    if (llid == 3u && pdu[0] == BLT_AFH_OP && len >= 5u)
        blt.afh_state = BLT_AFH_QUEUED;            /* (its instant set at the hand-over) */
    __asm__ volatile("" ::: "memory");             /* (the slot before the index: RING_PUBLISH) */
    blt.w++;
    return 1;
}
static void blt_publish(void)                      /* the main loop: what is queued may go */
{
    blt.pub = blt.w;
}

/* one signalling PDU into TX buffer i: DM1 (an LMP_set_AFH gets its instant now) */
static void blt_load_sig(struct fm1_br_frame *f, uint8_t *tab, uint32_t i)
{
    uint32_t k = blt.r % BLT_Q, n = blt.ql[k], j;
    uint8_t *dst = tab + (i ? BLT_OFF_TX1 : BLT_OFF_TX0);
    if (blt.qllid[k] == 3u && blt.q[k][0] == BLT_AFH_OP && n >= 5u) {
        uint32_t at = (BLT_CLKN() + BLT_AFH_AHEAD) & BLT_CLK & ~1u;
        for (j = 0; j < 4u; j++)
            blt.q[k][1u + j] = (uint8_t)(at >> 8u * j);   /* (Part C 5.2: AFH_instant, the slot as it is, bt_lmp.c) */
        blt.afh_at = at;
        blt.afh_state = BLT_AFH_DUE;
    }
    for (j = 0; j < n; j++)
        dst[j] = blt.q[k][j];
    f->txptr[i] = (uint16_t)(i ? BLT_OFF_TX1 : BLT_OFF_TX0);
    f->txphdr[i] = (uint16_t)(n << 3 | 1u << 2 | blt.qllid[k]);   /* LLID 3 LMP, 2 / 1 L2CAP; FLOW 1 */
    f->linkcntl[i] = (uint16_t)((f->linkcntl[i] & 0x80B0u) | 3u | 0x4000u);   /* DM1 */
    f->txbufcntl[i] &= (uint8_t)~1u;
    blt.r++;
}
static void blt_load_media(struct fm1_br_frame *f, uint8_t *tab, uint32_t i)
{
    uint32_t q = bp_mq.r % BLT_MQ, n = bp_mq.n[q], k, t;
    uint8_t *dst = tab + (i ? BLT_OFF_TXB1 : BLT_OFF_TXB0);
    for (k = 0; k < n; k++)
        dst[k] = bp_mq.d[q][k];
    t = n > 183u ? 15u : n > 27u ? 11u : 4u;       /* DH5, DH3, DH1 (BR §5.2) */
    f->txptr[i] = (uint16_t)(i ? BLT_OFF_TXB1 : BLT_OFF_TXB0);
    f->txphdr[i] = (uint16_t)(n << 3 | 1u << 2 | 2u);   /* LLID 2: an L2CAP start, whole; FLOW 1 */
    f->linkcntl[i] = (uint16_t)((f->linkcntl[i] & 0x80B0u) | t | 0x4000u);
    f->txbufcntl[i] &= (uint8_t)~1u;
    bp_mq.r++;
}
/* every empty TX buffer (BR §5.5: bit 0 of txbufcntl set = empty): the published signalling first, the stream after it
 * (never ahead of a published PDU). IRQ 40, or the main loop with IRQ 40 held */
static void blt_fill(struct fm1_br_frame *f, uint8_t *tab)
{
    uint32_t pass, i;
    for (pass = 0; pass < 2u; pass++) {
        int sig = blt.r != blt.pub;
        if (!sig && bp_mq.r == bp_mq.w)
            return;
        i = f->txtog & 1u;
        if (!(f->txbufcntl[i] & 1u))
            i ^= 1u;
        if (!(f->txbufcntl[i] & 1u))
            return;                                /* (both buffers full: the engine has two to send) */
        if (sig)
            blt_load_sig(f, tab, i);
        else
            blt_load_media(f, tab, i);
    }
}

/* the next hop set (the main loop, bt_page.c bp_afh_send): kept here until its instant; its PDU queued by the caller */
static void blt_afh_stage(const uint8_t table[80], const uint32_t words[3], uint32_t on)
{
    uint32_t i;
    for (i = 0; i < 80u; i++)
        blt.afh_tab[i] = table[i];
    for (i = 0; i < 3u; i++)
        blt.afh_words[i] = words[i];
    blt.afh_on = (uint8_t)on;
}
/* the 18-slot alarm (IRQ 40): at the instant the staged hop set goes to the engine (frq_table[1], map 0, miscntl [7:6],
 * format bit 6; BR §7) -> the slots to arm the next alarm with (the instant's own when it comes sooner than 18) */
static uint32_t blt_afh_alarm(uint32_t now, struct fm1_br_frame *f, uint8_t *tab)
{
    uint32_t left, i;
    if (blt.afh_state != BLT_AFH_DUE)
        return 18u;
    left = (blt.afh_at - now) & BLT_CLK;
    if (left == 0 || left > (BLT_CLK >> 1)) {      /* (at or past it) */
        if (blt.afh_on) {
            for (i = 0; i < 80u; i++)
                tab[FM1_BR_OFF_FRQ + 80u + i] = blt.afh_tab[i];
            BLT_AFH_MAP(blt.afh_words);
        }
        f->miscntl = (uint8_t)((f->miscntl & 0x3Fu) | (blt.afh_on ? 1u << 6 : 0u));   /* hop table 1 / basic (BR §4) */
        f->format = (uint8_t)((f->format & 0xBFu) | (blt.afh_on ? 0x40u : 0u));
        blt.afh_state = BLT_AFH_NONE;
        return 18u;
    }
    return left < 18u ? left : 18u;
}
