/* SPDX-License-Identifier: GPL-3.0-only */
/* Ableton Link, the tempo / beat / start-stop part (no Link Audio), as a peer on one IPv4 network
 * (docs/LINK-DESIGN.md). A C port of the protocol of Ableton's Link library (github.com/Ableton/link,
 * GPL-2.0-or-later, Copyright 2016 Ableton AG): its discovery (_asdp_v 1), its measurement (_link_v 1) and its
 * session rules, with the same wire format and timings, so the FM-1 joins a session of Live, the iOS apps and the
 * library's own peers.
 *
 * No sockets, no threads, no clock: the caller (the Wi-Fi side; tools/link_host.c on a computer) hands in each
 * datagram received on the two sockets (lk_rx), calls lk_poll every few ms, and sends what lk asks through
 * lk_cfg.send. Times are host microseconds (int64, monotonic). Integer only, no 64-bit division (the runtime
 * has none): the firmware can run it from the main loop.
 *
 * Two sockets on the caller's side: the multicast one, bound to LK_MCAST_PORT and joined to LK_MCAST_IP, and one
 * unicast socket bound to (cfg.ip, cfg.port) that every datagram is sent from. Both feed lk_rx. */
#ifndef LINK_H
#define LINK_H
#include <stdint.h>

#define LK_MCAST_IP 0xE04C4E4Bu            /* 224.76.78.75 */
#define LK_MCAST_PORT 20808u
#define LK_MSG_MAX 512u                    /* datagrams longer are not Link's */
#define LK_PEERS 8u                        /* peers kept (more are not seen) */
#define LK_OTHERS 4u                       /* other sessions kept, waiting to be measured / compared */
#define LK_PTS 102u                        /* measurement points (Link: done above 100) */
#define LK_MPB_MIN 60060                   /* us a beat: 999 BPM (Link's clamp) */
#define LK_MPB_MAX 3000000                 /* 20 BPM */
#define LK_UBEAT 1000000                   /* micro-beats a beat (the wire's unit) */

typedef int (*lk_send_fn)(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n);

struct lk_cfg {
    uint32_t ip;                           /* our address (host order); 0: not on a network */
    uint16_t port;                         /* the unicast socket's port */
    lk_send_fn send;
    void *ctx;
};

struct lk_tl {                             /* a timeline: beats against ghost time */
    int64_t mpb;                           /* tempo, us a beat */
    int64_t ub0;                           /* beat origin, micro-beats */
    int64_t t0;                            /* time origin, ghost us */
};

struct lk_ss {                             /* the start / stop state */
    uint8_t play;
    int64_t ub;                            /* the session beat it took effect at */
    int64_t ts;                            /* its ghost time stamp (the later wins) */
};

struct lk_peer {
    uint8_t used;
    uint8_t id[8], sess[8];
    struct lk_tl tl;
    struct lk_ss ss;
    uint32_t mip;                          /* its measurement endpoint (mep4) */
    uint16_t mport;
    int64_t dead;                          /* host us it is forgotten at (its ttl) */
};

struct lk_other {                          /* a session we are not in */
    uint8_t used, measured;
    uint8_t id[8];
    struct lk_tl tl;
    int64_t icpt;                          /* its ghost time = host + icpt, once measured */
};

struct lk_meas {                           /* one measurement at a time (Link: one a peer) */
    uint8_t on;
    uint8_t sess[8];                       /* the session measured */
    uint8_t retries;                       /* pings sent without an answer (Link: 5, then it failed) */
    uint32_t ip;
    uint16_t port;
    int64_t due;                           /* host us of the next ping without an answer */
    uint32_t n;
    int64_t d2[LK_PTS];                    /* twice each offset ghost - host, us */
};

struct lk_node {
    struct lk_cfg cfg;
    uint8_t on;
    uint8_t id[8];                         /* ours, random, printable ASCII (as Link's) */
    uint8_t sess[8];                       /* the session we are in */
    struct lk_tl tl;                       /* its timeline */
    struct lk_ss ss;                       /* its start / stop state */
    int64_t icpt;                          /* ghost time = host + icpt */
    int64_t remeasure;                     /* host us the session is measured again (Link: every 30 s) */
    int64_t last_bc, next_bc;              /* the last broadcast; the next one due */
    uint32_t rnd;                          /* xorshift state (ids) */
    struct lk_peer peer[LK_PEERS];
    struct lk_other other[LK_OTHERS];
    struct lk_meas meas;
};

void lk_init(struct lk_node *k, const struct lk_cfg *cfg, uint32_t seed, int32_t mpb);
void lk_enable(struct lk_node *k, int on, int64_t now);        /* on: a new session of our own; off: bye-bye */
void lk_set_addr(struct lk_node *k, uint32_t ip, uint16_t port); /* the network changed (0: gone) */
void lk_rx(struct lk_node *k, int64_t now, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n);
void lk_poll(struct lk_node *k, int64_t now);                  /* timers: broadcast, measurement, peers' ttl */

/* the session, at host time t */
int64_t lk_beat_at(const struct lk_node *k, int64_t t);          /* micro-beats */
int64_t lk_time_at(const struct lk_node *k, int64_t ub);         /* host us of a beat */
int32_t lk_mpb(const struct lk_node *k);
int lk_playing(const struct lk_node *k);
int64_t lk_play_beat(const struct lk_node *k);                   /* the beat the start / stop took effect at */
uint32_t lk_peers(const struct lk_node *k);                      /* peers in our session */

/* changes of our own, at host time t (sent to the session) */
void lk_set_tempo(struct lk_node *k, int32_t mpb, int64_t t);    /* the beat at t kept */
void lk_set_playing(struct lk_node *k, int play, int64_t ub, int64_t t);   /* play from session beat ub */

/* helpers (exported for the tests) */
int64_t lk_us_to_ub(int64_t us, int64_t mpb);                    /* llround(us / mpb * 1e6), as Link */
int64_t lk_ub_to_us(int64_t ub, int64_t mpb);                    /* llround(ub / 1e6 * mpb) */
int64_t lk_phase(int64_t ub, int64_t q_ub);                      /* ub mod q, in [0, q) */
#endif
