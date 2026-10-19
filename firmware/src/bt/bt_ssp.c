/* SPDX-License-Identifier: GPL-3.0-only */
/* Secure Simple Pairing, Just Works, P-192, the FM-1 as the responder (Core Vol 2 Part C 4.2.7, Part H 7.7): the device
 * (a phone, headphones, the Raspberry Pi of docs/BT-CLASSIC.md 14) starts it after its host asked for authentication;
 * then the device's LMP_au_rand gets our SRES (E1 with the new link key) and the encryption is started (we are the
 * master: we send the key size and LMP_start_encryption_req, Kc = E3(link key, EN_RAND, COF)). Written from the
 * specification; portable, no C library, no hardware: bt_page.c feeds it the LMP PDUs and sends what it queues, and
 * gives it a random source and the two addresses. Host-tested against a simulated initiator built from the same spec
 * functions (tests/bt_ssp_test.c).
 *
 * Byte orders: the PDUs carry every value least significant octet first (air order); the f functions take the spec's
 * order, most significant first (bt_safer.h). P-192 keys travel as X then Y, 24 octets each, air order.
 *
 *   btp_begin(p, own, peer, rng, send, ctx)  a new link (own and peer addresses in air order, as the FHS carries them)
 *   btp_in(p, pdu, len)                      a received LMP PDU: 1 it was the pairing's (answered here), 0 not ours
 *   p->st                                    BTP_* : where the pairing is; BTP_ENCRYPTED: kc is the key to load */
#include "bt_lmp.h"
#include "bt_safer.h"

enum { BTP_IDLE, BTP_IOCAP, BTP_PK_RX, BTP_PK_TX, BTP_CONFIRM, BTP_NUMBER, BTP_DHCHECK, BTP_PAIRED, BTP_AUTHED,
       BTP_KEYSIZE, BTP_STARTING, BTP_ENCRYPTED, BTP_FAILED };
#define BTP_IOCAP_NOIO 3u                               /* NoInputNoOutput: Just Works (Part C 4.2.7.1) */
#define BTP_AUTH_GENERAL_BONDING 4u                     /* MITM not required, general bonding (we keep the key while on) */
struct btp {
    uint8_t st, tid, pk_rx, pk_tx, reason;
    uint8_t own_addr[6], peer_addr[6];                  /* air order */
    uint8_t priv[24], pub[48], peer_pub[48], dhkey[24]; /* spec order */
    uint8_t na[16], nb[16];                             /* the initiator's and our nonce, spec order */
    uint8_t iocap_a[3], iocap_b[3];                     /* the IO capability, OOB, auth requirements of each side */
    uint8_t lk[16];                                     /* the link key, air order (E1 / E3 take it so: bt_safer.h) */
    uint8_t aco[12], en_rand[16], kc[16];               /* air order */
    int (*rng)(uint8_t *dest, unsigned size);
    void (*send)(void *ctx, const uint8_t *pdu, uint32_t len);
    void *ctx;
};

static void btp_rev(uint8_t *d, const uint8_t *s, uint32_t n)   /* air order <-> spec order */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        d[i] = s[n - 1u - i];
}
static void btp_put(struct btp *p, uint32_t op, const uint8_t *par, uint32_t n)   /* a PDU in the pairing's transaction */
{
    uint8_t b[BTL_MAX];
    uint32_t i, k = 0;
    if (op >> 8 == BTL_ESCAPE) {
        b[k++] = (uint8_t)(BTL_ESCAPE << 1 | p->tid);
        b[k++] = (uint8_t)op;
    } else {
        b[k++] = (uint8_t)(op << 1 | p->tid);
    }
    for (i = 0; i < n && k < BTL_MAX; i++)
        b[k++] = par[i];
    p->send(p->ctx, b, k);
}
static void btp_accept(struct btp *p, uint32_t of)
{
    uint8_t a = (uint8_t)of;
    btp_put(p, BTL_ACCEPTED, &a, 1);
}
static void btp_fail(struct btp *p, uint32_t of, uint32_t reason)
{
    uint8_t a[2];
    a[0] = (uint8_t)of, a[1] = (uint8_t)reason;
    btp_put(p, BTL_NOT_ACCEPTED, a, 2);
    p->st = BTP_FAILED;
    p->reason = (uint8_t)reason;
}
static void btp_spec_addr(uint8_t d[6], const uint8_t air[6]) { btp_rev(d, air, 6); }
static void btp_send_pk(struct btp *p)                  /* our next piece of the public key: the header, then 3 x 16 octets */
{
    uint8_t air[48], b[16];
    uint32_t i;
    btp_rev(air, p->pub, 24);                           /* X then Y, each air order */
    btp_rev(air + 24, p->pub + 24, 24);
    if (p->pk_tx == 0u) {
        b[0] = 1, b[1] = 1, b[2] = 48;                  /* major 1, minor 1 (P-192), 48 octets */
        btp_put(p, BTL_ENCAPSULATED_HEADER, b, 3);
    } else {
        for (i = 0; i < 16u; i++)
            b[i] = air[(p->pk_tx - 1u) * 16u + i];
        btp_put(p, BTL_ENCAPSULATED_PAYLOAD, b, 16);
    }
    p->pk_tx++;
}
static void btp_keys(struct btp *p)                     /* both keys known: the DHKey, our nonce and commitment */
{
    uint8_t pa[64], pb[64], s[32];
    uint32_t i;
    for (i = 0; i < 24u; i++) {                         /* (bt_safer.h's ECDH takes little-endian keys in 32-octet halves) */
        pa[i] = p->peer_pub[23u - i], pa[32u + i] = p->peer_pub[47u - i];
        pb[i] = p->priv[23u - i];
    }
    for (i = 24u; i < 32u; i++)
        pa[i] = pa[32u + i] = pb[i] = 0;
    bts_ecc_shared_secret(pa, pb, s);
    btp_rev(p->dhkey, s, 24);
    (void)p->rng(p->nb, 16);
    {   /* Cb = f1(PKbx, PKax, Nb, 0) */
        uint8_t cb[16], air[16];
        bts_ssp_f1(p->pub, p->peer_pub, p->nb, 0, cb);
        btp_rev(air, cb, 16);
        btp_put(p, BTL_SIMPLE_PAIRING_CONFIRM, air, 16);
    }
    p->st = BTP_NUMBER;
}
static void btp_link_key(struct btp *p)                 /* LK = f2(DHKey, Na, Nb, "btlk", BD_ADDR_A, BD_ADDR_B), A the initiator */
{
    uint8_t a1[6], a2[6], lk[16];
    btp_spec_addr(a1, p->peer_addr);
    btp_spec_addr(a2, p->own_addr);
    bts_ssp_f2(p->dhkey, p->na, p->nb, a1, a2, lk);
    btp_rev(p->lk, lk, 16);
}
static int btp_dh_check(struct btp *p, const uint8_t *ea_air)   /* the initiator's Ea, then our Eb (Part H 7.7.4) */
{
    uint8_t a1[6], a2[6], r[16] = {0}, e[16], ea[16];
    uint32_t i;
    btp_spec_addr(a1, p->peer_addr);
    btp_spec_addr(a2, p->own_addr);
    bts_ssp_f3(p->dhkey, p->na, p->nb, r, p->iocap_a, a1, a2, e);   /* Ea = f3(DHKey, Na, Nb, rb, IOcapA, A, B) */
    btp_rev(ea, ea_air, 16);
    for (i = 0; i < 16u; i++)
        if (e[i] != ea[i])
            return 0;
    bts_ssp_f3(p->dhkey, p->nb, p->na, r, p->iocap_b, a2, a1, e);   /* Eb = f3(DHKey, Nb, Na, ra, IOcapB, B, A) */
    btp_accept(p, BTL_DHKEY_CHECK);
    btp_rev(ea, e, 16);
    btp_put(p, BTL_DHKEY_CHECK, ea, 16);
    return 1;
}

static void btp_begin(struct btp *p, const uint8_t own[6], const uint8_t peer[6], int (*rng)(uint8_t *, unsigned),
                      void (*send)(void *, const uint8_t *, uint32_t), void *ctx)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)p;
    for (i = 0; i < sizeof *p; i++)
        b[i] = 0;
    for (i = 0; i < 6u; i++)
        p->own_addr[i] = own[i], p->peer_addr[i] = peer[i];
    p->rng = rng, p->send = send, p->ctx = ctx;
}
static int btp_in(struct btp *p, const uint8_t *pdu, uint32_t len)
{
    uint32_t tid = 0, op = btl_opcode(pdu, len, &tid), i;
    const uint8_t *par = pdu + (op >> 8 == BTL_ESCAPE ? 2 : 1);
    uint32_t n = len - (uint32_t)(par - pdu);
    switch (op) {
    case BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_REQ:     /* the device starts pairing: our IO capability back */
        if (n < 3u)
            return 1;
        p->tid = (uint8_t)tid;
        p->iocap_a[0] = par[0], p->iocap_a[1] = par[1], p->iocap_a[2] = par[2];
        p->iocap_b[0] = BTP_IOCAP_NOIO, p->iocap_b[1] = 0, p->iocap_b[2] = BTP_AUTH_GENERAL_BONDING;
        btp_put(p, BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_RES, p->iocap_b, 3);
        {
            uint8_t pub_be[48], priv_be[24], pub[64], priv[32];
            bts_ecc_make_key(pub, priv);                /* (a fresh key a pairing; little-endian halves: bt_safer.h) */
            for (i = 0; i < 24u; i++)
                pub_be[i] = pub[23u - i], pub_be[24u + i] = pub[32u + 23u - i], priv_be[i] = priv[23u - i];
            for (i = 0; i < 48u; i++)
                p->pub[i] = pub_be[i];
            for (i = 0; i < 24u; i++)
                p->priv[i] = priv_be[i];
        }
        p->pk_rx = p->pk_tx = 0;
        p->st = BTP_PK_RX;
        return 1;
    case BTL_ENCAPSULATED_HEADER:                       /* its public key's header: P-192 only */
        if (p->st != BTP_PK_RX)
            return 0;
        if (n < 3u || par[0] != 1u || par[1] != 1u || par[2] != 48u) {
            btp_fail(p, op, BTL_ERR_PAIRING_NOT_ALLOWED);   /* (P-256, Secure Connections: not offered) */
            return 1;
        }
        btp_accept(p, op);
        return 1;
    case BTL_ENCAPSULATED_PAYLOAD:                      /* 16 octets of its key; after the third, ours goes out */
        if (p->st != BTP_PK_RX || n < 16u || p->pk_rx >= 3u)
            return 0;
        {
            uint8_t air[48];
            for (i = 0; i < 16u; i++)
                air[i] = par[i];
            for (i = 0; i < 16u; i++) {
                uint32_t at = p->pk_rx * 16u + i;       /* the octet's place in X (0..23) or Y (24..47), air order */
                if (at < 24u)
                    p->peer_pub[23u - at] = air[i];
                else
                    p->peer_pub[24u + 47u - at] = air[i];
            }
        }
        btp_accept(p, op);
        if (++p->pk_rx == 3u) {
            p->st = BTP_PK_TX;
            btp_send_pk(p);                             /* (our header; the payloads follow each LMP_accepted) */
        }
        return 1;
    case BTL_SIMPLE_PAIRING_NUMBER:                     /* Na: accepted, then our Nb (the device checks our commitment) */
        if (p->st != BTP_NUMBER || n < 16u)
            return 0;
        btp_rev(p->na, par, 16);
        btp_accept(p, op);
        {
            uint8_t air[16];
            btp_rev(air, p->nb, 16);
            btp_put(p, BTL_SIMPLE_PAIRING_NUMBER, air, 16);
        }
        p->st = BTP_DHCHECK;
        return 1;
    case BTL_DHKEY_CHECK:                               /* Ea checked, our Eb back; the link key */
        if (p->st != BTP_DHCHECK || n < 16u)
            return 0;
        if (!btp_dh_check(p, par)) {
            btp_fail(p, op, BTL_ERR_AUTH_FAILURE);
            return 1;
        }
        btp_link_key(p);
        p->st = BTP_PAIRED;
        return 1;
    case BTL_AU_RAND:                                   /* the device authenticates us: SRES = E1(LK, AU_RAND, our address) */
        if (p->st < BTP_PAIRED || p->st == BTP_FAILED || n < 16u)
            return 0;
        {
            uint8_t sres[4];
            bts_e1(p->lk, par, p->own_addr, sres, p->aco);
            p->tid = (uint8_t)tid;
            btp_put(p, BTL_SRES, sres, 4);
        }
        if (p->st == BTP_PAIRED)
            p->st = BTP_AUTHED;
        return 1;
    case BTL_ENCRYPTION_MODE_REQ:                       /* the device asks for encryption: yes, and we start it (the master's) */
        if (p->st < BTP_AUTHED || p->st == BTP_FAILED)
            return 0;
        p->tid = (uint8_t)tid;
        btp_accept(p, op);
        if (n && par[0]) {                              /* (on: the key size, 16 octets, then EN_RAND once it is taken) */
            uint8_t ks = 16;
            p->st = BTP_KEYSIZE;
            btp_put(p, BTL_ENCRYPTION_KEY_SIZE_REQ, &ks, 1);
        }
        return 1;
    case BTL_ACCEPTED:                                  /* the device took what we sent: our next step */
        if (n < 1u)
            return 0;
        if ((par[0] == BTL_ENCAPSULATED_HEADER || par[0] == BTL_ENCAPSULATED_PAYLOAD) && p->st == BTP_PK_TX) {
            if (p->pk_tx < 4u)
                btp_send_pk(p);
            else
                btp_keys(p);                            /* (both keys exchanged: commitment) */
            return 1;
        }
        if (par[0] == BTL_SIMPLE_PAIRING_NUMBER || par[0] == BTL_DHKEY_CHECK || par[0] == BTL_SIMPLE_PAIRING_CONFIRM)
            return 1;                                   /* (its acknowledgements of ours: nothing more to send) */
        if (par[0] == BTL_ENCRYPTION_KEY_SIZE_REQ && p->st == BTP_KEYSIZE) {
            uint8_t r[16];
            (void)p->rng(r, 16);                        /* EN_RAND; Kc = E3(LK, EN_RAND, COF = the ACO of the authentication) */
            for (i = 0; i < 16u; i++)
                p->en_rand[i] = r[i];
            bts_e3(p->lk, p->en_rand, p->aco, p->kc);
            btp_put(p, BTL_START_ENCRYPTION_REQ, p->en_rand, 16);
            p->st = BTP_STARTING;
            return 1;
        }
        if (par[0] == BTL_START_ENCRYPTION_REQ && p->st == BTP_STARTING) {
            p->st = BTP_ENCRYPTED;                      /* (the engine's key load: bt_page.c, on this state) */
            return 1;
        }
        return 0;
    case BTL_NOT_ACCEPTED:
        if (p->st == BTP_IDLE || p->st == BTP_ENCRYPTED)
            return 0;
        p->st = BTP_FAILED;
        p->reason = n > 1u ? par[1] : 0;
        return 0;                                       /* (the page layer prints it) */
    default:
        return 0;
    }
}
