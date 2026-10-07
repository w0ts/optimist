/* SPDX-License-Identifier: GPL-3.0-only */
/* User drum kits (firmware/src/drum_kits.c) on a simulated NOR flash through storage.c: store (KIT lanes
 * written as the kit they play), numbers for names (KIT n: no name stored), list order, load into the
 * project, erase, a torn write, an older bank with names (DKB1, 204-byte kits) read as the new one,
 * 16 kits, where the bank lives (0xDA000..0xDBFFF; USR3 64 KiB, the FM6 bank at 0xD8000), the project keeping its kit after the
 * bank changes; and the editor commands 36..42 (ed_drums.c) through a minimal reply harness. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

static uint8_t nor[0x100000];
static int fail_after = -1;                       /* torn write: the n-th program fails */
static uint32_t lo_touch = 0xFFFFFFFFu, hi_touch;  /* the flash written / erased */
static void touch(uint32_t a, uint32_t n) { lo_touch = a < lo_touch ? a : lo_touch; hi_touch = a + n > hi_touch ? a + n : hi_touch; }
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { touch(off, 4096); memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    touch(off, n);
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
static struct { int force; uint8_t arm, arm_t; } ui;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
static uint32_t kit_tmp[4096 / 4];
#define UK_HOST 1
#define UK_TMP ((ukit_bank_t *)(void *)kit_tmp)
#include "../firmware/src/drum_kits.c"

/* the editor's reply builder, as editor.c has it */
static uint8_t ed_out[600];
static uint32_t ed_n;
static uint8_t flash_ok = 1;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
#include "../firmware/src/ed_drums.c"
static uint32_t pack(const void *p, uint32_t n, uint8_t *o)   /* pack7, as the editor sends */
{
    const uint8_t *b = p;
    uint32_t k = 0;
    while (n) {
        uint32_t c = n > 7u ? 7u : n, j, m = 0;
        for (j = 0; j < c; j++)
            m |= (uint32_t)(b[j] >> 7) << j;
        o[k++] = (uint8_t)m;
        for (j = 0; j < c; j++)
            o[k++] = b[j] & 0x7Fu;
        b += c;
        n -= c;
    }
    return k;
}
static int cmd(uint32_t c, const uint8_t *a, uint32_t na) { ed_n = 0; return ed_drums(c, a, na); }

static int fails;
static void check(const char *what, int ok)
{
    printf("%-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    ukit_t k;
    char nm[9];
    uint32_t i, l;
    int ok;
    host_tracks_init();
    memset(nor, 0xFF, sizeof nor);
    memset(&dl, 0, sizeof dl);
    check("an empty flash: no user kits", ukit_count() == 0u && !ukit_used(0));

    /* store from the project: the 909 kit, the snare on the 808's, a user sample on the rim, edits */
    TDRUM->p[P_E0] = DRUM_SAMPLED + 1u;
    dl.src[2] = DL_KIT0 + DRUM_SAMPLED;
    dl.src[7] = DL_USR + 1u;
    dl_set_ref(dl.ref[7], 4, 100, 300);
    dl.ofs[0][DE_TUNE] = -3;
    dl.ofs[15][DE_LEVEL] = -12;
    check("SAVE into slot 1: ok", ukit_store(0) == 0);
    ukit_name(0, nm);
    check("... shown as KIT 1, the project plays it (ukit 1)", !strcmp(nm, "KIT 1") && dl.ukit == 1u);
    ok = ukit_get(0, &k) && k.base == DRUM_SAMPLED + 1u && k.src[0] == DL_KIT0 + DRUM_SAMPLED + 1u &&
         k.src[2] == DL_KIT0 + DRUM_SAMPLED && k.src[7] == DL_USR + 1u && dl_hit(k.ref[7]) == 4u &&
         dl_start(k.ref[7]) == 100u && dl_len(k.ref[7]) == 300u && k.ofs[0][DE_TUNE] == -3 && k.ofs[15][DE_LEVEL] == -12;
    check("... read back: KIT lanes as the kit they played, the others as set", ok);
    check("... 196 bytes a kit (no name), the bank in one flash object", sizeof(ukit_t) == 196u && sizeof(ukit_bank_t) <= ST_PAYLOAD_MAX);
    check("the bank's sectors: 0xDA000 / 0xDB000 (after the FM6 bank, 0xD8000; USR3 and the snapshot area end there)", st_sector(OBJ_UKIT, 0) == 0xDA000u &&
          st_sector(OBJ_UKIT, 1) == 0xDB000u && SMP_USER_BASE + 2u * SMP_USER_SIZE + SMP_USER_CAP(2) + SN_SECTORS * 0x1000u == 0xD8000u);
    check("... every write went there", lo_touch >= 0xDA000u && hi_touch <= 0xDC000u);

    memset(&dl, 0, sizeof dl);
    TDRUM->p[P_E0] = 0;
    check("SAVE into slot 6", ukit_store(5) == 0 && (ukit_name(5, nm), !strcmp(nm, "KIT 6")));
    check("list: 2 kits, slot 6 the second, rank 1", ukit_count() == 2u && ukit_nth(1) == 5u && ukit_rank(5) == 1u &&
          ukit_used(5) && !ukit_used(4));
    memset(&dl, 0, sizeof dl);
    TDRUM->p[P_E0] = DRUM_SAMPLED + 5u;
    ok = ukit_load(0);
    check("load slot 1: the lanes, its kit (909)", ok && dl.ukit == 1u && TDRUM->p[P_E0] == DRUM_SAMPLED + 1u &&
          dl.src[2] == DL_KIT0 + DRUM_SAMPLED && dl.ofs[0][DE_TUNE] == -3 && dl_hit(dl.ref[7]) == 4u && dl_e0 == TDRUM->p[P_E0]);
    check("SAVE over slot 1: still KIT 1", ukit_store(0) == 0 && (ukit_name(0, nm), !strcmp(nm, "KIT 1")));
    {   /* a torn write: the bank before it stays */
        ukit_t a, b;
        ukit_get(5, &a);
        dl.ofs[3][DE_SNAP] = 40;
        fail_after = 3;
        check("a save cut short: an error", ukit_store(5) != 0);
        fail_after = -1;
        uk_read = 0;
        check("... the bank as it was (A/B)", ukit_get(5, &b) && !memcmp(&a, &b, sizeof a) && ukit_count() == 2u);
    }
    check("erase slot 1", ukit_put(0, 0) == 0 && !ukit_used(0) && ukit_count() == 1u);
    check("... the project keeps the kit it loaded (its lanes)", dl.ofs[0][DE_TUNE] == -3);
    for (i = 0; i < UK_N; i++) {
        dl.ofs[i][DE_DECAY] = (int8_t)(i - 8);
        ukit_store(i);
    }
    ok = ukit_count() == UK_N;
    for (i = 0; i < UK_N && ok; i++)
        ok &= ukit_get(i, &k) && k.ofs[i][DE_DECAY] == (int8_t)(i - 8);
    check("16 kits, each read back", ok);
    uk_read = 0;
    check("after a restart: 16 kits, KIT 16 the last", (ukit_name(15, nm), !strcmp(nm, "KIT 16")) && ukit_count() == UK_N);
    {   /* an older bank (DKB1: 204-byte kits with a name after used / base): read as the new one, names dropped */
        static uint8_t old[8u + 16u * 204u];
        uint32_t u;
        ukit_get(3, &k);
        memset(old, 0, sizeof old);
        old[0] = 'D', old[1] = 'K', old[2] = 'B', old[3] = '1', old[4] = 204, old[6] = 16;
        for (u = 0; u < 16u; u += 3u) {
            uint8_t *r = old + 8u + u * 204u;
            r[0] = UK_USED, r[1] = k.base;
            memcpy(r + 2, "OLDNAME", 7);
            memcpy(r + 10, &k.src, 196u - 4u);           /* src, ref, ofs as before the name went */
        }
        st_save(OBJ_UKIT, old, sizeof old);
        uk_read = 0;
        ok = ukit_count() == 6u && ukit_used(0) && ukit_used(15) && !ukit_used(1);
        for (u = 0; u < 16u && ok; u += 3u) {
            ukit_t g;
            ok = ukit_get(u, &g) && g.base == k.base && !memcmp(g.src, k.src, sizeof g.src) && !memcmp(g.ref, k.ref, sizeof g.ref) &&
                 !memcmp(g.ofs, k.ofs, sizeof g.ofs);
        }
        check("an older bank with names (DKB1): its 6 kits read, lanes intact, shown as numbers", ok && (ukit_name(15, nm), !strcmp(nm, "KIT 16")));
        dl.ofs[0][DE_TUNE] = 5;
        ok = ukit_store(1) == 0 && ukit_count() == 7u;
        uk_read = 0;
        ok &= ukit_count() == 7u && ukit_get(15, &k) && ((const ukit_bank_t *)(const void *)st_buf)->magic == UK_MAGIC;
        check("... the next save writes the new format (DKB2), every kit kept", ok);
    }

    /* USR3 is 16 KiB shorter (the banks): a longer sample there reads as empty */
    {
        smp_user_hdr_t *h = (smp_user_hdr_t *)((uint8_t *)host_slots + 2u * SMP_USER_SIZE);
        memset(h, 0, sizeof *h);
        h->magic = SMP_USER_MAGIC;
        h->version = 1;
        h->nz = 1;
        h->data_len = SMP_USER_SIZE - SMP_USER_DATA;    /* (80 KiB: as USR1 / USR2 hold) */
        h->zone[0].n = 2000;
        h->zone[0].le = 1999;
        h->zone[0].rate = 32768;
        h->zone[0].hi = 127;
        smp_user_scan(2);
        ok = usr_nz[2] == 0;
        h->data_len = SMP_USER_CAP(2) - SMP_USER_DATA;
        smp_user_scan(2);
        check("USR3: 80 KiB of data refused, 64 KiB accepted", ok && usr_nz[2] == 1);
    }

    /* the editor commands */
    {
        uint8_t a[300], b[12];
        uint32_t n;
        dlanes_t got;
        for (i = 0; i < 12u; i++)
            b[i] = 0;
        b[DE_CUT] = (uint8_t)-20;
        b[8] = DL_KIT0 + 3u;
        a[0] = 9;
        n = 1u + pack(b, 12, a + 1);
        ok = cmd(ED_DRUM_LANE, a, n) && ed_out[0] == 9 && dl.ofs[9][DE_CUT] == -20 && dl.src[9] == DL_KIT0 + 3u;
        check("DRUM_LANE 37: set lane 10 (CUT -20, ACOUSTIC's... kit 3)", ok);
        ok = cmd(ED_DRUM_LANES, a, 0) && ed_unpack7(ed_out, ed_n, (uint8_t *)&got, sizeof got) == sizeof got &&
             !memcmp(&got, &dl, sizeof got);
        check("DRUM_LANES 36: the 204 bytes as the device has them", ok);
        got.ofs[1][DE_TUNE] = 99;                        /* out of range: clamped */
        n = pack(&got, sizeof got, a);
        check("DRUM_LANES 36: set, clamped", cmd(ED_DRUM_LANES, a, n) && dl.ofs[1][DE_TUNE] == 24);
        ok = cmd(ED_UKIT_LIST, a, 0) && ed_n == 1u + UK_N && ed_out[0] == UK_N && ed_out[1] == 1 && ed_out[2] == 1;
        check("UKIT_LIST 38: 16, then used per slot (no names)", ok);
        a[0] = 3;                                        /* (used: the older bank's kits are 1, 4, 7, ..) */
        ok = cmd(ED_UKIT_GET, a, 1) && ed_out[0] == 3 && ed_out[1] == 1 &&
             ed_unpack7(ed_out + 2, ed_n - 2u, (uint8_t *)&k, sizeof k) == sizeof k && k.used == UK_USED;
        check("UKIT_GET 39: slot 4, 196 bytes", ok);
        k.ofs[0][DE_CUT] = -7;
        a[0] = 2;
        n = 1u + pack(&k, sizeof k, a + 1);
        ok = cmd(ED_UKIT_PUT, a, n) && ed_out[1] == 0 && ukit_get(2, &k) && k.ofs[0][DE_CUT] == -7;
        check("UKIT_PUT 40: slot 3 written (an import)", ok);
        song.playing = 1;
        check("... refused while playing (rc 3)", cmd(ED_UKIT_PUT, a, n) && ed_out[1] == 3);
        song.playing = 0;
        a[0] = 2, a[1] = 3, memcpy(a + 2, "NEW\0", 4);
        check("UKIT_OP 41 op 3 (rename) gone: refused (rc 1)", cmd(ED_UKIT_OP, a, 6) && ed_out[2] == 1);
        a[1] = 0;
        check("UKIT_OP 41 load", cmd(ED_UKIT_OP, a, 2) && ed_out[2] == 0 && dl.ukit == 3u);
        a[1] = 1;
        check("UKIT_OP 41 erase", cmd(ED_UKIT_OP, a, 2) && ed_out[2] == 0 && !ukit_used(2));
        a[1] = 2, memcpy(a + 2, "AGAIN\0", 6);
        check("UKIT_OP 41 store the project's lanes (a name sent by an older editor: ignored)",
              cmd(ED_UKIT_OP, a, 8) && ed_out[2] == 0 && ukit_used(2) && (ukit_name(2, nm), !strcmp(nm, "KIT 3")));
        for (i = 0; i < 300u; i++)
            ((uint8_t *)host_slots)[SMP_USER_SIZE + 512u + i] = (uint8_t)(i * 7u);
        a[0] = 1, a[1] = 512u & 127u, a[2] = 512u >> 7, a[3] = 0, a[4] = 100, a[5] = 0;
        ok = cmd(ED_SMP_READ, a, 6) && ed_out[0] == 1 && ed_unpack7(ed_out + 4, ed_n - 4u, b, 12) == 12u;
        for (l = 0; l < 12u; l++)
            ok &= b[l] == (uint8_t)(l * 7u);
        check("SMP_READ 42: USR2 bytes at 512", ok);
        a[0] = 2, a[1] = 0, a[2] = (SMP_USER_CAP(2) - 50u) >> 7 & 127u, a[3] = (SMP_USER_CAP(2) - 50u) >> 14;
        a[1] = (SMP_USER_CAP(2) - 50u) & 127u, a[4] = 0, a[5] = 2;
        ok = cmd(ED_SMP_READ, a, 6) && ed_n == 4u + 50u + 8u;
        check("SMP_READ 42: not past USR3's end (50 bytes left)", ok);
    }
    printf(fails ? "DRUM KITS TEST FAILED\n" : "drum kits test passed\n");
    return fails != 0;
}
