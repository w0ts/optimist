/* SPDX-License-Identifier: GPL-3.0-only */
/* Ableton Link peer (firmware/src/link/link.c) on the host: nodes on a simulated network (a delay each way,
 * jitter, each node its own host clock), no sockets.
 *   wire      an ALIVE's bytes as Ableton's library writes them (discovery/v1/Messages.hpp, PayloadEntries)
 *   math      lk_us_to_ub / lk_ub_to_us = Link's llround of doubles, lk_phase
 *   join      a newer node joins the older session (its tempo, its beat), both ways round
 *   tempo     a change on either node reaches the other; the beat stays continuous
 *   phase     both nodes' beats agree within 0.2 ms of a beat's time, with clocks 7 s and -3 s apart
 *   start     start / stop reaches the other node, the later time stamp wins
 *   leave     BYEBYE and a silent peer (the ttl) drop it from the count
 * Exit status: the number of failed checks. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/link/link.c"

static int fails, checks;
#define CHECK(c, ...)                                                                                              \
    do {                                                                                                           \
        checks++;                                                                                                  \
        if (!(c)) {                                                                                                \
            fails++;                                                                                               \
            printf("FAIL %s:%d ", __FILE__, __LINE__);                                                             \
            printf(__VA_ARGS__);                                                                                   \
            printf("\n");                                                                                          \
        }                                                                                                          \
    } while (0)

/* ---- the network: datagrams in flight, delivered after a delay (us) to the node owning the address */
#define NN 3
#define QN 4096
struct dg { int64_t at; int to; uint32_t ip; uint16_t port; uint32_t n; uint8_t b[LK_MSG_MAX]; };
static struct dg q[QN];
static int qn;
static struct lk_node node[NN];
static int64_t ofs[NN];                            /* node i's host clock = true time + ofs[i] */
static int alive[NN];
static int64_t T;                                  /* true time, us */
static int64_t lat = 400, jit = 300;               /* one way */
static uint32_t rs = 1;
static int64_t rnd(int64_t m)
{
    rs = rs * 1103515245u + 12345u;
    return m ? (int64_t)((rs >> 8) % (uint32_t)m) : 0;
}
static uint8_t last_alive[LK_MSG_MAX];
static uint32_t last_alive_n;

static void push(int to, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    if (qn == QN)
        return;
    q[qn].at = T + lat + rnd(jit);
    q[qn].to = to;
    q[qn].ip = ip;
    q[qn].port = port;
    q[qn].n = n;
    memcpy(q[qn].b, p, n);
    qn++;
}

static int net_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    int from = (int)(intptr_t)ctx, i;
    if (ip == LK_MCAST_IP) {
        if (p[8] == 1) {
            memcpy(last_alive, p, n);
            last_alive_n = n;
        }
        for (i = 0; i < NN; i++)
            if (i != from && alive[i])
                push(i, node[from].cfg.ip, node[from].cfg.port, p, n);
        return 0;
    }
    for (i = 0; i < NN; i++)
        if (alive[i] && node[i].cfg.ip == ip && node[i].cfg.port == port)
            push(i, node[from].cfg.ip, node[from].cfg.port, p, n);
    return 0;
}

static int64_t host(int i) { return T + ofs[i]; }

static void run(int64_t us)                        /* advance true time, 1 ms polls, deliveries in time order */
{
    int64_t end = T + us;
    while (T < end) {
        int i, j;
        T += 100;
        for (j = 0; j < qn;) {
            if (q[j].at <= T) {
                struct dg d = q[j];
                q[j] = q[--qn];
                if (alive[d.to])
                    lk_rx(&node[d.to], host(d.to), d.ip, d.port, d.b, d.n);
            } else {
                j++;
            }
        }
        for (i = 0; i < NN; i++)
            if (alive[i])
                lk_poll(&node[i], host(i));
    }
}

static void start(int i, double bpm)
{
    struct lk_cfg c = {0};
    c.ip = 0x0A000001u + (uint32_t)i;
    c.port = (uint16_t)(50000 + i);
    c.send = net_send;
    c.ctx = (void *)(intptr_t)i;
    lk_init(&node[i], &c, 1234u + (uint32_t)i * 77u, (int32_t)llround(60e6 / bpm));
    alive[i] = 1;
    lk_enable(&node[i], 1, host(i));
}

/* the beat difference between two nodes at the same true time, in us of a beat */
static double beat_diff_us(int a, int b)
{
    int64_t ba = lk_beat_at(&node[a], host(a)), bb = lk_beat_at(&node[b], host(b));
    return (double)(ba - bb) * (double)lk_mpb(&node[a]) / 1e6;
}

static void t_math(void)
{
    static const int64_t us[] = {0, 1, -1, 499999, 500000, -500000, 123456789, -987654321, 86400000000LL, 7};
    static const int64_t mpb[] = {500000, 451128, 60060, 3000000, 333333, 481927};
    unsigned i, j;
    for (i = 0; i < sizeof us / sizeof *us; i++)
        for (j = 0; j < sizeof mpb / sizeof *mpb; j++) {
            int64_t want = llround((double)us[i] / (double)mpb[j] * 1e6), got = lk_us_to_ub(us[i], mpb[j]);
            int64_t want2 = llround((double)us[i] / 1e6 * (double)mpb[j]), got2 = lk_ub_to_us(us[i], mpb[j]);
            CHECK(llabs(want - got) <= 1, "us_to_ub(%lld, %lld) = %lld, Link %lld", (long long)us[i],
                  (long long)mpb[j], (long long)got, (long long)want);
            CHECK(llabs(want2 - got2) <= 1, "ub_to_us(%lld, %lld) = %lld, Link %lld", (long long)us[i],
                  (long long)mpb[j], (long long)got2, (long long)want2);
        }
    CHECK(lk_phase(5500000, 4000000) == 1500000, "phase 5.5 q4");
    CHECK(lk_phase(-500000, 4000000) == 3500000, "phase -0.5 q4");
    CHECK(lk_phase(-4000000, 4000000) == 0, "phase -4 q4");
}

static void t_wire(void)
{
    /* node 0 alone: its first ALIVE, field by field (big endian, entries tmln sess stst mep4) */
    static const uint8_t hdr[] = {'_', 'a', 's', 'd', 'p', '_', 'v', 1, 1, 5, 0, 0};
    const uint8_t *p = last_alive;
    memset(node, 0, sizeof node);
    memset(alive, 0, sizeof alive);
    qn = 0;
    T = 1000000;
    ofs[0] = 0;
    start(0, 120);
    run(1000);
    CHECK(last_alive_n == 20u + 32u + 16u + 25u + 14u, "ALIVE length %u", last_alive_n);
    CHECK(!memcmp(p, hdr, sizeof hdr), "ALIVE header");
    CHECK(!memcmp(p + 12, node[0].id, 8), "ALIVE node id");
    CHECK(r32(p + 20) == 0x746d6c6eu && r32(p + 24) == 24u, "tmln key / size");
    CHECK(r64(p + 28) == 500000, "tmln tempo = 500000 us a beat (120 BPM)");
    CHECK(r32(p + 52) == 0x73657373u && r32(p + 56) == 8u && !memcmp(p + 60, node[0].id, 8),
          "sess = our id (a session of our own)");
    CHECK(r32(p + 68) == 0x73747374u && r32(p + 72) == 17u && p[76] == 0, "stst, not playing");
    CHECK(r32(p + 93) == 0x6d657034u && r32(p + 97) == 6u && r32(p + 101) == 0x0A000001u &&
              (p[105] << 8 | p[106]) == 50000,
          "mep4 = our unicast endpoint");
    {
        int i, ok = 1;
        for (i = 0; i < 8; i++)
            ok &= node[0].id[i] >= 33 && node[0].id[i] <= 126;
        CHECK(ok, "node id printable ASCII");
    }
}

static void t_session(void)
{
    double d;
    int64_t b0, b1;
    memset(node, 0, sizeof node);
    memset(alive, 0, sizeof alive);
    qn = 0;
    T = 5000000;
    ofs[0] = 7000000;                              /* clocks far apart */
    ofs[1] = -3000000;
    ofs[2] = 123456;
    start(0, 120);
    run(3000000);                                  /* node 0's session is 3 s older */
    start(1, 90);
    run(2000000);
    CHECK(id_eq(node[1].sess, node[0].sess), "join: the newer node took the older session");
    CHECK(lk_mpb(&node[1]) == 500000, "join: its tempo (%d)", lk_mpb(&node[1]));
    CHECK(lk_peers(&node[0]) == 1 && lk_peers(&node[1]) == 1, "join: one peer each (%u %u)", lk_peers(&node[0]),
          lk_peers(&node[1]));
    d = beat_diff_us(0, 1);
    CHECK(fabs(d) < 200, "phase: beats agree, %.1f us", d);
    printf("  join: beat difference %.1f us\n", d);

    lk_set_tempo(&node[0], 451128, host(0));      /* 133 BPM */
    b0 = lk_beat_at(&node[1], host(1));
    run(500000);
    b1 = lk_beat_at(&node[1], host(1));
    CHECK(lk_mpb(&node[1]) == 451128, "tempo: node 1 follows node 0 (%d)", lk_mpb(&node[1]));
    CHECK(b1 > b0 && b1 - b0 < 1300000, "tempo: node 1's beat moved on smoothly (%lld)", (long long)(b1 - b0));
    d = beat_diff_us(0, 1);
    CHECK(fabs(d) < 200, "tempo: beats agree after, %.1f us", d);

    lk_set_tempo(&node[1], 600000, host(1));      /* 100 BPM, from the joiner */
    run(500000);
    CHECK(lk_mpb(&node[0]) == 600000, "tempo: node 0 follows node 1 (%d)", lk_mpb(&node[0]));
    d = beat_diff_us(0, 1);
    CHECK(fabs(d) < 200, "tempo: beats agree, %.1f us", d);

    {
        int64_t b = lk_beat_at(&node[0], host(0)), qb = 4 * LK_UBEAT;
        lk_set_playing(&node[0], 1, b - lk_phase(b, qb) + qb, host(0));
        run(300000);
        CHECK(lk_playing(&node[1]) && lk_play_beat(&node[1]) == lk_play_beat(&node[0]),
              "start: reached node 1, same beat");
        lk_set_playing(&node[1], 0, lk_beat_at(&node[1], host(1)), host(1));
        run(300000);
        CHECK(!lk_playing(&node[0]), "stop: reached node 0");
    }

    start(2, 140);                                 /* a third, much newer: joins too */
    run(2000000);
    CHECK(id_eq(node[2].sess, node[0].sess) && lk_peers(&node[0]) == 2, "join: third node (%u peers)",
          lk_peers(&node[0]));
    d = beat_diff_us(0, 2);
    CHECK(fabs(d) < 200, "phase: third node, %.1f us", d);

    lk_enable(&node[2], 0, host(2));               /* BYEBYE */
    alive[2] = 0;
    run(100000);
    CHECK(lk_peers(&node[0]) == 1, "leave: BYEBYE drops it (%u)", lk_peers(&node[0]));
    alive[1] = 0;                                  /* silent: the ttl (5 s) */
    run(4000000);
    CHECK(lk_peers(&node[0]) == 1, "leave: still there within its ttl");
    run(2000000);
    CHECK(lk_peers(&node[0]) == 0, "leave: gone after its ttl (%u)", lk_peers(&node[0]));
    CHECK(lk_mpb(&node[0]) == 600000, "leave: the session's tempo kept");
}

static void t_older_joins_back(void)
{
    /* two sessions met late: the one that started later gives way, whichever node hears first */
    memset(node, 0, sizeof node);
    memset(alive, 0, sizeof alive);
    qn = 0;
    T = 1000000;
    ofs[0] = 0;
    ofs[1] = 50000000;
    start(1, 100);                                 /* node 1 first: the older session */
    alive[1] = 0;                                  /* (not on the network yet) */
    run(4000000);
    start(0, 120);
    alive[1] = 1;
    run(3000000);
    CHECK(id_eq(node[0].sess, node[1].sess) && lk_mpb(&node[0]) == 600000,
          "older session wins: node 0 joined node 1's (%d)", lk_mpb(&node[0]));
    CHECK(fabs(beat_diff_us(0, 1)) < 200, "older session wins: beats agree, %.1f us", beat_diff_us(0, 1));
}

int main(void)
{
    t_math();
    t_wire();
    t_session();
    t_older_joins_back();
    printf("link_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
