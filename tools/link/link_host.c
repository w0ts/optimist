/* SPDX-License-Identifier: GPL-3.0-only */
/* firmware/src/link/link.c on a computer (macOS / Linux), over real sockets: the FM-1's Link peer, before the
 * FM-1 has a network. tools/link/interop.sh runs it beside ref_peer (Ableton's library) and compares the two.
 *   link_host [-ip A.B.C.D] [-bpm B] [-for S] [-at S:tempo=B] [-at S:play=0|1] [-q Q]
 * -ip: the interface to use (127.0.0.1: peers on this computer; a LAN address: Live on another one).
 * Every 20 ms one line: "O host_us micro_beats bpm playing peers phase" (host_us as Link's on that system:
 * mach_absolute_time on macOS, CLOCK_MONOTONIC elsewhere; phase in beats against Q). */
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#include "../../firmware/src/link/link.h"

static int64_t now_us(void)                        /* Link's own clock (platforms/darwin/Clock.hpp) */
{
#ifdef __APPLE__
    static double k;
    if (!k) {
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        k = (double)tb.numer / ((double)tb.denom * 1e3);
    }
    return llround((double)mach_absolute_time() * k);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

static int s_uni = -1, s_mc = -1;

static int net_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    struct sockaddr_in to;
    (void)ctx;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(ip);
    to.sin_port = htons(port);
    if (sendto(s_uni, p, n, 0, (struct sockaddr *)&to, sizeof to) < 0) {
        fprintf(stderr, "link_host: sendto: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int open_sockets(uint32_t ip, uint16_t *port)
{
    struct sockaddr_in a;
    struct ip_mreq mr;
    socklen_t al = sizeof a;
    int on = 1;
    unsigned char loop = (ip >> 24) == 127u;       /* Link: multicast loopback only on a loopback interface */
    struct in_addr ifa;
    ifa.s_addr = htonl(ip);
    s_uni = socket(AF_INET, SOCK_DGRAM, 0);
    s_mc = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_uni < 0 || s_mc < 0)
        return -1;
    setsockopt(s_uni, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    setsockopt(s_uni, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    setsockopt(s_uni, IPPROTO_IP, IP_MULTICAST_IF, &ifa, sizeof ifa);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    if (bind(s_uni, (struct sockaddr *)&a, sizeof a) < 0 || getsockname(s_uni, (struct sockaddr *)&a, &al) < 0)
        return -1;
    *port = ntohs(a.sin_port);
    setsockopt(s_mc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#ifdef SO_REUSEPORT
    setsockopt(s_mc, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on);
#endif
    setsockopt(s_mc, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(LK_MCAST_IP);
    a.sin_port = htons(LK_MCAST_PORT);
    if (bind(s_mc, (struct sockaddr *)&a, sizeof a) < 0)
        return -1;
    mr.imr_multiaddr.s_addr = htonl(LK_MCAST_IP);
    mr.imr_interface = ifa;
    if (setsockopt(s_mc, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr) < 0)
        return -1;
    return 0;
}

struct ev { double at; int tempo; double v; int done; };

int main(int argc, char **argv)
{
    static struct lk_node k;
    struct lk_cfg cfg = {0};
    struct ev evs[16];
    int nev = 0, i;
    double bpm = 120, secs = 20, q = 4;
    uint32_t ip = 0x7F000001u;
    int64_t t0, next_print = 0;
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-ip")) {
            struct in_addr ia;
            if (!inet_aton(argv[i + 1], &ia))
                return 2;
            ip = ntohl(ia.s_addr);
        } else if (!strcmp(argv[i], "-bpm")) {
            bpm = atof(argv[i + 1]);
        } else if (!strcmp(argv[i], "-for")) {
            secs = atof(argv[i + 1]);
        } else if (!strcmp(argv[i], "-q")) {
            q = atof(argv[i + 1]);
        } else if (!strcmp(argv[i], "-at") && nev < 16) {
            const char *c = strchr(argv[i + 1], ':'), *e = c ? strchr(c, '=') : 0;
            if (!e)
                return 2;
            evs[nev].at = atof(argv[i + 1]);
            evs[nev].tempo = !strncmp(c + 1, "tempo=", 6);
            evs[nev].v = atof(e + 1);
            evs[nev].done = 0;
            nev++;
        }
    }
    if (open_sockets(ip, &cfg.port) < 0) {
        fprintf(stderr, "link_host: sockets: %s\n", strerror(errno));
        return 1;
    }
    cfg.ip = ip;
    cfg.send = net_send;
    lk_init(&k, &cfg, (uint32_t)now_us() ^ (uint32_t)getpid() << 16, (int32_t)llround(60e6 / bpm));
    t0 = now_us();
    lk_enable(&k, 1, t0);
    for (;;) {
        struct pollfd pf[2] = {{s_mc, POLLIN, 0}, {s_uni, POLLIN, 0}};
        int64_t now = now_us();
        double el = (double)(now - t0) / 1e6;
        if (el > secs)
            break;
        for (i = 0; i < nev; i++) {
            if (evs[i].done || el < evs[i].at)
                continue;
            evs[i].done = 1;
            if (evs[i].tempo) {
                lk_set_tempo(&k, (int32_t)llround(60e6 / evs[i].v), now);
            } else {                               /* start on the next bar (q), as LinkHut's start */
                int64_t qb = (int64_t)llround(q * LK_UBEAT), b = lk_beat_at(&k, now);
                lk_set_playing(&k, evs[i].v != 0, b - lk_phase(b, qb) + (evs[i].v != 0 ? qb : 0), now);
            }
        }
        if (poll(pf, 2, 2) > 0) {
            int s;
            for (s = 0; s < 2; s++) {
                uint8_t buf[LK_MSG_MAX + 1];
                struct sockaddr_in from;
                socklen_t fl = sizeof from;
                ssize_t n;
                if (!(pf[s].revents & POLLIN))
                    continue;
                n = recvfrom(pf[s].fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
                if (n > 0)
                    lk_rx(&k, now_us(), ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), buf, (uint32_t)n);
            }
        }
        now = now_us();
        lk_poll(&k, now);
        if (now >= next_print) {
            int64_t b = lk_beat_at(&k, now);
            printf("O %lld %lld %.6f %d %u %.6f\n", (long long)now, (long long)b, 60e6 / lk_mpb(&k), lk_playing(&k),
                   lk_peers(&k), (double)lk_phase(b, (int64_t)llround(q * LK_UBEAT)) / LK_UBEAT);
            fflush(stdout);
            next_print = now + 20000;
        }
    }
    lk_enable(&k, 0, now_us());
    return 0;
}
