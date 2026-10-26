/* SPDX-License-Identifier: GPL-3.0-only */
/* Ableton Link peer (link.h; docs/LINK-DESIGN.md). Follows Ableton's library (include/ableton/discovery,
 * include/ableton/link; GPL-2.0-or-later): the file each rule comes from is named where it is used.
 *
 * Wire (discovery/v1/Messages.hpp, NetworkByteStreamSerializable.hpp): big endian.
 *   discovery  "_asdp_v" 1, type (1 ALIVE, 2 RESPONSE, 3 BYEBYE), ttl, group 0 (u16), node id (8), payload
 *   link       "_link_v" 1, type (1 PING, 2 PONG), payload
 *   payload    entries: key (u32), size (u32), value; an entry with an unknown key is skipped
 *     tmln  tempo (us a beat, i64), beat origin (micro-beats, i64), time origin (ghost us, i64)
 *     sess  session id (8)          stst  playing (u8), beats (i64), time stamp (ghost us, i64)
 *     mep4  ip (u32), port (u16)    __ht host time, __gt ghost time, _pgt previous ghost time (i64 us)
 * Ghost time: a session's shared clock, host time + intercept here (Link's GhostXForm, slope 1). A new session
 * starts at ghost 0 now (Controller.hpp initXForm), so an older session's ghost time runs ahead of a new one's. */
#include "link.h"

#define LK_TTL 5u                          /* s (discovery/PeerGateway.hpp) */
#define LK_BC_US 250000                    /* ttl / ttlRatio 20 (UdpMessenger.hpp broadcastState) */
#define LK_BC_MIN_US 50000                 /* .. never closer than that */
#define LK_PING_US 50000                   /* Measurement.hpp resetTimer */
#define LK_PING_TRIES 5u                   /* kNumberMeasurements */
#define LK_PTS_DONE 100u                   /* kNumberDataPoints: done above it */
#define LK_REMEASURE_US 30000000           /* Sessions.hpp scheduleRemeasurement */
#define LK_SESSION_EPS 500000              /* Sessions.hpp SESSION_EPS */

enum { D_ALIVE = 1, D_RESPONSE = 2, D_BYEBYE = 3 };
enum { L_PING = 1, L_PONG = 2 };
#define K_TMLN 0x746d6c6eu
#define K_SESS 0x73657373u
#define K_STST 0x73747374u
#define K_MEP4 0x6d657034u
#define K_HT 0x5f5f6874u
#define K_GT 0x5f5f6774u
#define K_PGT 0x5f706774u
static const uint8_t ASDP[8] = {'_', 'a', 's', 'd', 'p', '_', 'v', 1};
static const uint8_t LINKV[8] = {'_', 'l', 'i', 'n', 'k', '_', 'v', 1};

/* ---- arithmetic without 64-bit division */
static uint64_t lk_udiv(uint64_t n, uint32_t d, uint32_t *rem)   /* n / d, *rem = n % d */
{
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = r << 1 | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t)1 << i;
        }
    }
    if (rem)
        *rem = (uint32_t)r;
    return q;
}

/* round(a * m / d) half away from zero, for |a| < 2^63, m and d < 2^32 (a split so a * m never overflows) */
static int64_t lk_muldiv(int64_t a, uint32_t m, uint32_t d)
{
    uint64_t u = a < 0 ? (uint64_t)-a : (uint64_t)a, q, f;
    uint32_t r;
    q = lk_udiv(u, d, &r);
    f = lk_udiv((uint64_t)r * m + d / 2u, d, 0);
    u = q * m + f;
    return a < 0 ? -(int64_t)u : (int64_t)u;
}

int64_t lk_us_to_ub(int64_t us, int64_t mpb) { return lk_muldiv(us, LK_UBEAT, (uint32_t)mpb); }
int64_t lk_ub_to_us(int64_t ub, int64_t mpb) { return lk_muldiv(ub, (uint32_t)mpb, LK_UBEAT); }

int64_t lk_phase(int64_t ub, int64_t q)
{
    uint32_t r;
    if (q <= 0)
        return 0;
    if (ub >= 0) {
        lk_udiv((uint64_t)ub, (uint32_t)q, &r);
        return r;
    }
    lk_udiv((uint64_t)-ub, (uint32_t)q, &r);
    return r ? q - r : 0;
}

static int64_t tl_beats(const struct lk_tl *tl, int64_t g) { return tl->ub0 + lk_us_to_ub(g - tl->t0, tl->mpb); }
static int64_t tl_time(const struct lk_tl *tl, int64_t ub) { return tl->t0 + lk_ub_to_us(ub - tl->ub0, tl->mpb); }

static int64_t clamp_mpb(int64_t mpb)              /* ClientSessionTimelines.hpp clampTempo */
{
    return mpb < LK_MPB_MIN ? LK_MPB_MIN : mpb > LK_MPB_MAX ? LK_MPB_MAX : mpb;
}

static int id_eq(const uint8_t *a, const uint8_t *b)
{
    int i;
    for (i = 0; i < 8; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int id_lt(const uint8_t *a, const uint8_t *b)   /* std::array operator< */
{
    int i;
    for (i = 0; i < 8; i++)
        if (a[i] != b[i])
            return a[i] < b[i];
    return 0;
}
static void id_cp(uint8_t *d, const uint8_t *s)
{
    int i;
    for (i = 0; i < 8; i++)
        d[i] = s[i];
}

/* ---- the byte stream */
struct wr { uint8_t *p; uint32_t n; };
static void w8(struct wr *w, uint32_t v) { w->p[w->n++] = (uint8_t)v; }
static void w16(struct wr *w, uint32_t v) { w8(w, v >> 8); w8(w, v); }
static void w32(struct wr *w, uint32_t v) { w16(w, v >> 16); w16(w, v); }
static void w64(struct wr *w, int64_t v) { w32(w, (uint32_t)((uint64_t)v >> 32)); w32(w, (uint32_t)v); }
static void wid(struct wr *w, const uint8_t *id)
{
    int i;
    for (i = 0; i < 8; i++)
        w8(w, id[i]);
}
static void wkey(struct wr *w, uint32_t key, uint32_t size) { w32(w, key); w32(w, size); }

static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int64_t r64(const uint8_t *p) { return (int64_t)((uint64_t)r32(p) << 32 | r32(p + 4)); }

/* ---- ghost time */
static int64_t ghost(const struct lk_node *k, int64_t host) { return host + k->icpt; }

int64_t lk_beat_at(const struct lk_node *k, int64_t t) { return tl_beats(&k->tl, ghost(k, t)); }
int64_t lk_time_at(const struct lk_node *k, int64_t ub) { return tl_time(&k->tl, ub) - k->icpt; }
int32_t lk_mpb(const struct lk_node *k) { return (int32_t)k->tl.mpb; }
int lk_playing(const struct lk_node *k) { return k->ss.play; }
int64_t lk_play_beat(const struct lk_node *k) { return k->ss.ub; }

uint32_t lk_peers(const struct lk_node *k)
{
    uint32_t i, n = 0;
    for (i = 0; i < LK_PEERS; i++)
        n += k->peer[i].used && id_eq(k->peer[i].sess, k->sess);
    return n;
}

/* ---- discovery: our state out */
static void send_state(struct lk_node *k, uint32_t type, uint32_t ip, uint16_t port, int64_t now)
{
    uint8_t b[128];
    struct wr w = {b, 0};
    uint32_t i;
    if (!k->cfg.ip || !k->cfg.send)
        return;
    for (i = 0; i < 8; i++)
        w8(&w, ASDP[i]);
    w8(&w, type);
    w8(&w, type == D_BYEBYE ? 0u : LK_TTL);
    w16(&w, 0);                                    /* group */
    wid(&w, k->id);
    if (type != D_BYEBYE) {                        /* NodeState toPayload, then PeerState's mep4 */
        wkey(&w, K_TMLN, 24);
        w64(&w, k->tl.mpb);
        w64(&w, k->tl.ub0);
        w64(&w, k->tl.t0);
        wkey(&w, K_SESS, 8);
        wid(&w, k->sess);
        wkey(&w, K_STST, 17);
        w8(&w, k->ss.play);
        w64(&w, k->ss.ub);
        w64(&w, k->ss.ts);
        wkey(&w, K_MEP4, 6);
        w32(&w, k->cfg.ip);
        w16(&w, k->cfg.port);
    }
    k->cfg.send(k->cfg.ctx, ip, port, b, w.n);
    if (type == D_ALIVE)
        k->last_bc = now;
}

static void changed(struct lk_node *k, int64_t now)   /* our state changed: broadcast soon (>= 50 ms apart) */
{
    int64_t t = k->last_bc + LK_BC_MIN_US;
    if (t < now)
        t = now;
    if (t < k->next_bc)
        k->next_bc = t;
}

/* ---- measurement (Measurement.hpp, MeasurementService.hpp) */
static void send_ping(struct lk_node *k, uint32_t ip, uint16_t port, int64_t ht, int64_t pgt, int with_pgt)
{
    uint8_t b[48];
    struct wr w = {b, 0};
    uint32_t i;
    for (i = 0; i < 8; i++)
        w8(&w, LINKV[i]);
    w8(&w, L_PING);
    wkey(&w, K_HT, 8);
    w64(&w, ht);
    if (with_pgt) {
        wkey(&w, K_PGT, 8);
        w64(&w, pgt);
    }
    if (k->cfg.send)
        k->cfg.send(k->cfg.ctx, ip, port, b, w.n);
}

static struct lk_peer *session_peer(struct lk_node *k, const uint8_t *sess)   /* the founder first */
{
    struct lk_peer *f = 0;
    uint32_t i;
    for (i = 0; i < LK_PEERS; i++) {
        struct lk_peer *p = &k->peer[i];
        if (!p->used || !id_eq(p->sess, sess) || !p->mip)
            continue;
        if (id_eq(p->id, sess))
            return p;
        if (!f)
            f = p;
    }
    return f;
}

static int meas_start(struct lk_node *k, const uint8_t *sess, int64_t now)
{
    struct lk_peer *p;
    if (k->meas.on)
        return 0;
    p = session_peer(k, sess);
    if (!p)
        return 0;
    k->meas.on = 1;
    id_cp(k->meas.sess, sess);
    k->meas.ip = p->mip;
    k->meas.port = p->mport;
    k->meas.retries = 0;
    k->meas.n = 0;
    k->meas.due = now + LK_PING_US;
    send_ping(k, p->mip, p->mport, now, 0, 0);
    return 1;
}

static struct lk_other *other_find(struct lk_node *k, const uint8_t *sess)
{
    uint32_t i;
    for (i = 0; i < LK_OTHERS; i++)
        if (k->other[i].used && id_eq(k->other[i].id, sess))
            return &k->other[i];
    return 0;
}

static void forget_session(struct lk_node *k, const uint8_t *sess)   /* Peers.hpp forgetSession */
{
    uint32_t i;
    for (i = 0; i < LK_PEERS; i++)
        if (k->peer[i].used && id_eq(k->peer[i].sess, sess))
            k->peer[i].used = 0;
}

static void join(struct lk_node *k, struct lk_other *o, int64_t now)   /* Controller.hpp joinSession */
{
    struct lk_other cur;
    cur.used = 1;
    cur.measured = 1;
    id_cp(cur.id, k->sess);
    cur.tl = k->tl;
    cur.icpt = k->icpt;
    id_cp(k->sess, o->id);
    k->tl = o->tl;
    k->icpt = o->icpt;
    k->ss.play = 0;                                /* resetSessionStartStopState */
    k->ss.ub = 0;
    k->ss.ts = 0;
    *o = cur;                                      /* the old session becomes another one */
    k->remeasure = now + LK_REMEASURE_US;
    changed(k, now);
}

/* Sessions.hpp handleSuccessfulMeasurement / handleFailedMeasurement */
static void meas_done(struct lk_node *k, int ok, int64_t icpt, int64_t now)
{
    struct lk_other *o;
    k->meas.on = 0;
    if (id_eq(k->meas.sess, k->sess)) {
        if (ok && icpt != k->icpt) {
            k->icpt = icpt;
            changed(k, now);
        }
        k->remeasure = now + LK_REMEASURE_US;
        return;
    }
    o = other_find(k, k->meas.sess);
    if (!o)
        return;
    if (!ok) {
        o->used = 0;
        forget_session(k, k->meas.sess);
        return;
    }
    o->icpt = icpt;
    o->measured = 1;
    {
        int64_t diff = icpt - k->icpt;             /* new ghost - current ghost, now */
        int64_t ad = diff < 0 ? -diff : diff;
        if (diff > LK_SESSION_EPS || (ad < LK_SESSION_EPS && id_lt(o->id, k->sess)))
            join(k, o, now);
    }
}

static int64_t rdiv(int64_t a, int64_t d)          /* round(a / d) half away from zero, d 2 or 4 */
{
    return a < 0 ? -((-a + d / 2) / d) : (a + d / 2) / d;
}

static int64_t median2(int64_t *v, uint32_t n)     /* the median of twice-values, halved (Median.hpp) */
{
    uint32_t i, j;
    for (i = 1; i < n; i++) {                      /* insertion sort: n <= LK_PTS, once a measurement */
        int64_t x = v[i];
        for (j = i; j && v[j - 1] > x; j--)
            v[j] = v[j - 1];
        v[j] = x;
    }
    if (n & 1u)
        return rdiv(v[n / 2u], 2);
    return rdiv(v[n / 2u] + v[n / 2u - 1u], 4);
}

static void meas_pong(struct lk_node *k, uint32_t ip, uint16_t port, const uint8_t *p, const uint8_t *e, int64_t now)
{
    uint8_t sess[8] = {0};
    int64_t gt = 0, pgt = 0, pht = 0;
    while (e - p >= 8) {                           /* parsePayload<SessionMembership, GHostTime, PrevGHostTime, HostTime> */
        uint32_t key = r32(p), n = r32(p + 4);
        p += 8;
        if (n > (uint32_t)(e - p))
            return;
        if (key == K_SESS && n == 8u)
            id_cp(sess, p);
        else if (key == K_GT && n == 8u)
            gt = r64(p);
        else if (key == K_PGT && n == 8u)
            pgt = r64(p);
        else if (key == K_HT && n == 8u)
            pht = r64(p);
        else if (key == K_SESS || key == K_GT || key == K_PGT || key == K_HT)
            return;                                /* (a known entry of the wrong size: the message is bad) */
        p += n;
    }
    if (!id_eq(sess, k->meas.sess)) {
        meas_done(k, 0, 0, now);
        return;
    }
    send_ping(k, ip, port, now, gt, 1);
    if (gt && pht && k->meas.n < LK_PTS) {
        k->meas.d2[k->meas.n++] = 2 * gt - (now + pht);
        if (pgt && k->meas.n < LK_PTS)
            k->meas.d2[k->meas.n++] = gt + pgt - 2 * pht;
    }
    if (k->meas.n > LK_PTS_DONE) {
        meas_done(k, 1, median2(k->meas.d2, k->meas.n), now);
        return;
    }
    k->meas.due = now + LK_PING_US;
}

/* the ping responder (PingResponder.hpp): our session and ghost time, then their payload back */
static void pong(struct lk_node *k, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n, int64_t now)
{
    uint8_t b[96];
    struct wr w = {b, 0};
    uint32_t i;
    if (n > 32u)                                   /* bigger than HostTime + PrevGHostTime: not a ping */
        return;
    for (i = 0; i < 8; i++)
        w8(&w, LINKV[i]);
    w8(&w, L_PONG);
    wkey(&w, K_SESS, 8);
    wid(&w, k->sess);
    wkey(&w, K_GT, 8);
    w64(&w, ghost(k, now));
    for (i = 0; i < n; i++)
        w8(&w, p[i]);
    if (k->cfg.send)
        k->cfg.send(k->cfg.ctx, ip, port, b, w.n);
}

/* ---- discovery: peers in */
static struct lk_peer *peer_slot(struct lk_node *k, const uint8_t *id)
{
    struct lk_peer *f = 0;
    uint32_t i;
    for (i = 0; i < LK_PEERS; i++) {
        if (k->peer[i].used && id_eq(k->peer[i].id, id))
            return &k->peer[i];
        if (!k->peer[i].used && !f)
            f = &k->peer[i];
    }
    return f;
}

/* Sessions.hpp sawSessionTimeline, Controller.hpp handleStartStopStateFromSession */
static void saw_session(struct lk_node *k, const struct lk_peer *p, int64_t now)
{
    struct lk_other *o;
    uint32_t i;
    if (id_eq(p->sess, k->sess)) {
        if (p->tl.ub0 > k->tl.ub0) {               /* updateTimeline: the later beat origin wins */
            k->tl = p->tl;
            k->tl.mpb = clamp_mpb(k->tl.mpb);
            changed(k, now);
        }
        if (p->ss.ts > k->ss.ts) {
            k->ss = p->ss;
            changed(k, now);
        }
        return;
    }
    o = other_find(k, p->sess);
    if (o) {
        if (p->tl.ub0 > o->tl.ub0)
            o->tl = p->tl;
        return;
    }
    for (i = 0; i < LK_OTHERS; i++)
        if (!k->other[i].used)
            break;
    if (i == LK_OTHERS)
        return;                                    /* (full: seen again on the next broadcast) */
    o = &k->other[i];
    o->used = 1;
    o->measured = 0;
    id_cp(o->id, p->sess);
    o->tl = p->tl;
    o->tl.mpb = clamp_mpb(o->tl.mpb);
    meas_start(k, o->id, now);                     /* (busy: lk_poll starts it later) */
}

static void peer_state(struct lk_node *k, const uint8_t *id, uint32_t ttl, const uint8_t *p, const uint8_t *e, int64_t now)
{
    struct lk_peer np, *s;
    uint32_t got = 0;
    np.used = 1;
    id_cp(np.id, id);
    np.mip = 0;
    np.mport = 0;
    while (e - p >= 8) {                           /* NodeState / PeerState fromPayload */
        uint32_t key = r32(p), n = r32(p + 4);
        p += 8;
        if (n > (uint32_t)(e - p))
            return;
        if (key == K_TMLN) {
            if (n != 24u)
                return;
            np.tl.mpb = r64(p);
            np.tl.ub0 = r64(p + 8);
            np.tl.t0 = r64(p + 16);
            got |= 1u;
        } else if (key == K_SESS) {
            if (n != 8u)
                return;
            id_cp(np.sess, p);
            got |= 2u;
        } else if (key == K_STST) {
            if (n != 17u)
                return;
            np.ss.play = p[0] != 0;
            np.ss.ub = r64(p + 1);
            np.ss.ts = r64(p + 9);
            got |= 4u;
        } else if (key == K_MEP4) {
            if (n != 6u)
                return;
            np.mip = r32(p);
            np.mport = (uint16_t)(p[4] << 8 | p[5]);
        }
        p += n;
    }
    if ((got & 3u) != 3u || np.tl.mpb <= 0)
        return;                                    /* (no timeline or session: nothing to follow) */
    if (!(got & 4u)) {
        np.ss.play = 0;
        np.ss.ub = 0;
        np.ss.ts = 0;
    }
    np.dead = now + (int64_t)ttl * 1000000;
    s = peer_slot(k, id);
    if (!s)
        return;
    *s = np;
    saw_session(k, s, now);
}

void lk_rx(struct lk_node *k, int64_t now, uint32_t ip, uint16_t port, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!k->on || n < 9u || n > LK_MSG_MAX)
        return;
    for (i = 0; i < 8u && p[i] == LINKV[i]; i++)
        ;
    if (i == 8u) {
        if (p[8] == L_PING)
            pong(k, ip, port, p + 9, n - 9u, now);
        else if (p[8] == L_PONG && k->meas.on)
            meas_pong(k, ip, port, p + 9, p + n, now);
        return;
    }
    for (i = 0; i < 8u && p[i] == ASDP[i]; i++)
        ;
    if (i < 8u || n < 20u)
        return;
    if (id_eq(p + 12, k->id) || p[10] || p[11])    /* our own, or another group */
        return;
    if (p[8] == D_ALIVE) {
        send_state(k, D_RESPONSE, ip, port, now);
        peer_state(k, p + 12, p[9], p + 20, p + n, now);
    } else if (p[8] == D_RESPONSE) {
        peer_state(k, p + 12, p[9], p + 20, p + n, now);
    } else if (p[8] == D_BYEBYE) {
        struct lk_peer *s = peer_slot(k, p + 12);
        if (s && s->used && id_eq(s->id, p + 12))
            s->used = 0;
    }
}

void lk_poll(struct lk_node *k, int64_t now)
{
    uint32_t i;
    if (!k->on)
        return;
    for (i = 0; i < LK_PEERS; i++)                 /* the ttl ran out (PeerGateway.hpp pruneExpiredPeers) */
        if (k->peer[i].used && now >= k->peer[i].dead)
            k->peer[i].used = 0;
    if (k->meas.on && now >= k->meas.due) {        /* no pong in time */
        if (k->meas.retries < LK_PING_TRIES) {
            k->meas.retries++;
            k->meas.due = now + LK_PING_US;
            send_ping(k, k->meas.ip, k->meas.port, now, 0, 0);
        } else {
            meas_done(k, 0, 0, now);
        }
    }
    if (!k->meas.on) {
        for (i = 0; i < LK_OTHERS; i++)
            if (k->other[i].used && !k->other[i].measured && meas_start(k, k->other[i].id, now))
                break;
        if (!k->meas.on && now >= k->remeasure) {
            if (!meas_start(k, k->sess, now))
                k->remeasure = now + LK_REMEASURE_US;
        }
    }
    for (i = 0; i < LK_OTHERS; i++)                /* a session whose peers have all gone */
        if (k->other[i].used && !session_peer(k, k->other[i].id) &&
            !(k->meas.on && id_eq(k->meas.sess, k->other[i].id)))
            k->other[i].used = 0;
    if (now >= k->next_bc) {
        send_state(k, D_ALIVE, LK_MCAST_IP, LK_MCAST_PORT, now);
        k->next_bc = now + LK_BC_US;
    }
}

static void new_id(struct lk_node *k, uint8_t *id)  /* printable ASCII 33..126 (platforms/stl/Random.hpp) */
{
    int i;
    for (i = 0; i < 8; i++) {
        k->rnd ^= k->rnd << 13;
        k->rnd ^= k->rnd >> 17;
        k->rnd ^= k->rnd << 5;
        id[i] = (uint8_t)(33u + k->rnd % 94u);
    }
}

void lk_init(struct lk_node *k, const struct lk_cfg *cfg, uint32_t seed, int32_t mpb)
{
    uint8_t *b = (uint8_t *)k;
    uint32_t i;
    for (i = 0; i < sizeof *k; i++)
        b[i] = 0;
    k->cfg = *cfg;
    k->rnd = seed ? seed : 0x2545F491u;
    k->tl.mpb = clamp_mpb(mpb);
}

/* Controller.hpp resetState: a new node and a session of its own, the beat at now kept, ghost 0 now */
void lk_enable(struct lk_node *k, int on, int64_t now)
{
    uint32_t i;
    if (!on) {
        if (k->on)
            send_state(k, D_BYEBYE, LK_MCAST_IP, LK_MCAST_PORT, now);
        k->on = 0;
        return;
    }
    if (k->on)
        return;
    {
        int64_t b = k->tl.mpb ? lk_beat_at(k, now) : 0;
        new_id(k, k->id);
        id_cp(k->sess, k->id);
        k->icpt = -now;
        k->tl.ub0 = b;
        k->tl.t0 = 0;
    }
    k->ss.play = 0;
    k->ss.ub = 0;
    k->ss.ts = 0;
    for (i = 0; i < LK_PEERS; i++)
        k->peer[i].used = 0;
    for (i = 0; i < LK_OTHERS; i++)
        k->other[i].used = 0;
    k->meas.on = 0;
    k->remeasure = now + LK_REMEASURE_US;
    k->last_bc = now - LK_BC_MIN_US;
    k->next_bc = now;
    k->on = 1;
}

void lk_set_addr(struct lk_node *k, uint32_t ip, uint16_t port)
{
    k->cfg.ip = ip;
    k->cfg.port = port;
    k->next_bc = k->last_bc;                       /* (tell the new network at once) */
}

/* ClientSessionTimelines.hpp updateSessionTimelineFromClient: the beat at t kept, the beat origin moved past the
 * old one (so peers take it: the later origin wins) */
void lk_set_tempo(struct lk_node *k, int32_t mpb, int64_t t)
{
    int64_t g = ghost(k, t), b = tl_beats(&k->tl, g), o;
    struct lk_tl n;
    mpb = (int32_t)clamp_mpb(mpb);
    if (mpb == k->tl.mpb)
        return;
    n.mpb = mpb;
    n.ub0 = b;
    n.t0 = g;
    o = b > k->tl.ub0 + LK_UBEAT ? b : k->tl.ub0 + LK_UBEAT;
    n.t0 = tl_time(&n, o);
    n.ub0 = o;
    k->tl = n;
    changed(k, t);
}

void lk_set_playing(struct lk_node *k, int play, int64_t ub, int64_t t)
{
    int64_t g = ghost(k, t);
    if (g <= k->ss.ts)
        return;
    k->ss.play = (uint8_t)(play != 0);
    k->ss.ub = ub;
    k->ss.ts = g;
    changed(k, t);
}
