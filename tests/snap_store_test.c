/* SPDX-License-Identifier: GPL-3.0-only */
/* The snapshot area (firmware/src/snap_store.c) on a simulated NOR: streams of 1..8 sectors written, read back and
 * found again after a restart; a power cut at every erase and program of a save and of a clear (the cut program
 * leaves half its bytes, the cut erase half its sector): the slot is its old version or its new one, never a mix,
 * never damaged, the other slots untouched, a cleared slot never brings an older version back; FULL; a damaged
 * part (DAMAGED, the others fine, cleared, saved over); a slot from a build with more slots kept; a writer's
 * sectors never given to another; 3000 random operations with random cuts against a model. Run by
 * tests/run_tests.sh. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef SN_SECTORS
#define SN_SECTORS 8u                                 /* (FELUCCA_SNAPSHOTS 4) */
#endif

static uint8_t nor[0x100000];
static long ops, cut_at = -1;                         /* the op that loses power (-1 none) */
static int dead;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int cut(void) { if (dead) return 1; if (ops++ == cut_at) { dead = 1; return 2; } return 0; }
static int st_erase(uint32_t off)
{
    int c = cut();
    if (c == 1)
        return -9;
    memset(nor + off, 0xFF, c == 2 ? 2048u : 4096u);  /* (cut: half the sector) */
    return c ? -9 : 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    int c = cut();
    if (c == 1)
        return -9;
    for (i = 0; i < (c == 2 ? n / 2u : n); i++)       /* (cut: half the bytes) */
        nor[off + i] &= ((const uint8_t *)src)[i];
    return c ? -9 : 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/snap_store.c"
#define NB (SN_SECTORS - 2u < SN_PARTS ? SN_SECTORS - 2u : SN_PARTS)   /* (the second writer: what is left, at most 8) */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static void boot(void) { dead = 0; cut_at = -1; sn.up = 0; sn_scan(); }

/* stream v of length n for slot k: bytes from a seed */
static uint8_t *mk(uint32_t seed, uint32_t n)
{
    static uint8_t b[SN_MAX];
    uint32_t i, x = seed * 2654435761u + 1u;
    for (i = 0; i < n; i++)
        x = x * 1103515245u + 12345u, b[i] = (uint8_t)(x >> 16);
    return b;
}
static int save(uint32_t k, uint32_t seed, uint32_t n)
{
    sn_wr_t w;
    const uint8_t *b = mk(seed, n);
    uint32_t o;
    int rc = sn_wr_begin(&w, k, n);
    for (o = 0; !rc && o < n; o += 300u) {            /* (in pieces, as the editor and the codec put them) */
        static uint8_t piece[300];
        uint32_t c = n - o < 300u ? n - o : 300u;
        memcpy(piece, b + o, c);
        rc = sn_wr_put(&w, piece, c);
    }
    if (!rc)
        rc = sn_wr_commit(&w);
    else
        sn_wr_release(&w);
    return rc;
}
static int is(uint32_t k, uint32_t seed, uint32_t n)  /* slot k holds that stream */
{
    static uint8_t got[SN_MAX];
    return sn.slot[k].state == SN_OK && sn_size(k) == n && !sn_read(k, 0, got, n) && !memcmp(got, mk(seed, n), n);
}
static int empty(uint32_t k) { return sn.slot[k].state == SN_EMPTY && !sn_size(k); }

int main(void)
{
    uint32_t k, i, ok, n;
    long c, total;
    memset(nor, 0xFF, sizeof nor);
    boot();
    ok = sn_free_count() == SN_SECTORS;
    for (k = 0; k < SN_NSLOT; k++)
        ok &= empty(k);
    check("an erased area: every slot empty, every sector free", ok);
    check("a stream of 1,000 B into slot 1: one sector, read back", !save(0, 1, 1000) && is(0, 1, 1000) && sn_free_count() == SN_SECTORS - 1u);
    check("10,000 B into slot 2: three sectors (4,064 B a part), read back", !save(1, 2, 10000) && is(1, 2, 10000) && sn.slot[1].parts == 3u);
    if (SN_SECTORS - 4u < SN_PARTS)
        check("the longest stream (8 x 4,064 B) refused with fewer sectors free: FULL, nothing changed",
              save(2, 3, SN_MAX) == -2 && is(0, 1, 1000) && is(1, 2, 10000) && empty(2));
    else
        check("the longest stream (8 x 4,064 B) into 8 free sectors, read back, cleared",
              !save(2, 3, SN_MAX) && is(2, 3, SN_MAX) && !sn_clear(2) && empty(2));
    boot();
    check("... a restart: the same two slots, the seq counter past them", is(0, 1, 1000) && is(1, 2, 10000) && sn.seq > 2u);
    check("slot 1 saved again (5,000 B, 2 parts): the new one, its old sector free again",
          !save(0, 4, 5000) && is(0, 4, 5000) && is(1, 2, 10000) && sn_free_count() == SN_SECTORS - 5u);
    check("BEFORE LOAD (slot 8 in every build) apart from the user's", !save(SN_BAK, 5, 777) && is(SN_BAK, 5, 777) && is(0, 4, 5000));
    check("clear slot 2: empty, the others as they were", !sn_clear(1) && empty(1) && is(0, 4, 5000) && is(SN_BAK, 5, 777));
    boot();
    check("... a restart: still empty", empty(1) && is(0, 4, 5000));

    /* a cut at every flash operation of a save over slot 1 (5,000 B -> 9,000 B): old or new, never a mix */
    for (ok = 1, total = 0, c = 0;; c++) {
        memset(nor, 0xFF, sizeof nor);
        boot();
        save(0, 10, 5000), save(2, 11, 3000);
        ops = 0, cut_at = c;
        save(0, 12, 9000);
        total = ops;
        boot();
        ok &= (is(0, 10, 5000) || is(0, 12, 9000)) && is(2, 11, 3000) && empty(1);
        if (c > total + 1)
            break;
    }
    check("a save cut at each of its erases and programs: the old version or the new, the others untouched", ok && total > 6);
    /* the same for a clear, with an older version of the slot left behind (its housekeeping erase cut) */
    for (ok = 1, c = 0; c < 8; c++) {
        memset(nor, 0xFF, sizeof nor);
        boot();
        static uint8_t keep[SN_SECT];
        uint32_t s1;
        save(0, 20, 1000);
        s1 = sn.slot[0].sec[0];
        memcpy(keep, nor + sn_off(s1), SN_SECT);
        save(0, 21, 1500);                            /* (its housekeeping erased the older one: put back, as a */
        if (sn.own[s1] == SN_FREE)                    /* cut before that erase would have left it) */
            memcpy(nor + sn_off(s1), keep, SN_SECT);
        boot();
        n = is(0, 21, 1500);
        {
            sn_head_t h;
            uint32_t s, olds = 0;
            for (s = 0; s < SN_SECTORS; s++)
                olds += sn_head(s, &h) && h.slot == 0u && h.part == 0u && h.seq < sn.slot[0].seq;
            n &= olds == 1u;                          /* (the older version still there) */
        }
        ops = 0, cut_at = c;
        sn_clear(0);
        boot();
        ok &= n && (is(0, 21, 1500) || empty(0));
    }
    check("a clear cut anywhere, an older version still in flash: the newest or empty, never the older", ok);

    /* damaged: a byte of part 2 */
    memset(nor, 0xFF, sizeof nor);
    boot();
    save(0, 30, 9000), save(1, 31, 2000);
    nor[sn_off(sn.slot[0].sec[1]) + SN_HEAD + 100] ^= 0x40;
    boot();
    check("a byte of a part flipped: that slot DAMAGED, its size 0; the other fine", sn.slot[0].state == SN_BAD && !sn_size(0) && is(1, 31, 2000));
    check("... its sectors stay taken until cleared or saved over", sn_free_count() == SN_SECTORS - 4u);
    check("... saved over: the new one (the damaged one's sectors free)", !save(0, 32, 100) && is(0, 32, 100) && sn_free_count() == SN_SECTORS - 2u);
    nor[sn_off(sn.slot[1].sec[0]) + 3] ^= 1;          /* (part 0's header) */
    boot();
    check("part 0's header damaged: that slot reads empty (no commit)", empty(1) && is(0, 32, 100));
    nor[sn_off(sn.slot[0].sec[0]) + SN_HEAD] ^= 1;
    boot();
    check("... a damaged one cleared: empty", sn.slot[0].state == SN_BAD && !sn_clear(0) && empty(0));

    /* a slot saved by a build with more slots (slot 6): kept, its sectors not reused */
    memset(nor, 0xFF, sizeof nor);
    boot();
    save(6, 40, 4000);
    for (i = 0; i < SN_SECTORS; i++)
        save(0, 41 + i, 3000);
    check("a slot of a build with more slots (7 of 8): kept through 8 saves of slot 1", is(6, 40, 4000) && is(0, 40u + SN_SECTORS, 3000));

    /* a writer's sectors: never another's */
    {
        sn_wr_t a, b;
        memset(nor, 0xFF, sizeof nor);
        boot();
        n = sn_wr_begin(&a, 0, 2u * SN_PAY) == 0;
        sn_clear(3);                                  /* (a rescan meanwhile) */
        n &= sn_wr_begin(&b, 1, NB * SN_PAY) == 0;
        for (i = 0; i < 2u; i++)
            for (k = 0; k < NB; k++)
                n &= a.sec[i] != b.sec[k];
        n &= b.seq != a.seq;
        sn_wr_release(&b);
        n &= sn_free_count() == SN_SECTORS - 2u;
        sn_wr_release(&a);
        check("two writers at once (one through a rescan): no sector and no seq shared; given back", n && sn_free_count() == SN_SECTORS);
    }

    /* random: saves, clears, damage-free cuts, restarts, against a model */
    {
        uint32_t mseed[SN_NSLOT], mlen[SN_NSLOT], r = 7, fulls = 0, cuts = 0;
        memset(nor, 0xFF, sizeof nor);
        memset(mlen, 0, sizeof mlen);
        boot();
        for (ok = 1, i = 0; i < 3000u; i++) {
            uint32_t s, len, nseed = i + 100u, nlen;
            int rc, was_cut;
            r = r * 1103515245u + 12345u;
            s = (r >> 8) % SN_NSLOT;
            len = 1u + (r >> 4) % (2u * SN_PAY);
            ops = 0, cut_at = (r >> 20) % 7u == 0u ? (long)((r >> 12) % 9u) : -1;
            if ((r >> 16) % 3u == 0u) {
                nlen = 0;
                rc = sn_clear(s);
            } else {
                nlen = len;
                rc = save(s, nseed, len);
                fulls += rc == -2;
            }
            was_cut = dead;
            cuts += was_cut;
            if (!was_cut && rc && rc != -2)
                ok = 0;                               /* (no cut: done, or FULL) */
            if (was_cut || (r >> 24) % 11u == 0u)
                boot();
            for (k = 0; k < SN_NSLOT; k++) {
                int old = mlen[k] ? is(k, mseed[k], mlen[k]) : empty(k);
                int neu = k == s && (nlen ? is(k, nseed, nlen) : empty(k));
                if (k != s || (!was_cut && rc == -2))
                    ok &= old;
                else if (!was_cut)
                    ok &= neu;
                else
                    ok &= old || neu;                 /* (a cut: the old one or the new, nothing else) */
                if (neu)
                    mseed[k] = nseed, mlen[k] = nlen;
            }
        }
        printf("  (%u FULL, %u cuts)\n", fulls, cuts);
        check("3000 random saves / clears / cuts / restarts: every slot the last one written, or before a cut its old one", ok);
    }
    printf(bad ? "snap store test FAILED (%d)\n" : "snap store test passed\n", bad);
    return bad != 0;
}
