/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of FM6's voices outside the engine (firmware/src/fm6_store.c) against simulated USR sample
 * slots in NOR flash (erase -> 0xFF, a program only clears bits): the DX7 SysEx it takes (a voice, a
 * 32-voice bank, a voice / function parameter change), the bank in a free USR slot (never over a
 * sample; none free: refused), found again at boot, lost when damaged or written over, STORE into it
 * and VOICE U.. reading it back. (The emulator has no flash data area: flash_ok stays 0 there.) */
#define FELUCCA_FLASH 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t flash_ok = 1;
static uint32_t n_erase, n_prog;
static uint8_t *slot_mem(uint32_t off) { return (uint8_t *)host_slots + (off - SMP_USER_BASE); }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    *took = 0;
    if (off < SMP_USER_BASE || off + 0x1000u > SMP_USER_BASE + 3u * SMP_USER_SIZE || (off & 0xFFFu))
        return -1;
    memset(slot_mem(off), 0xFF, 0x1000u);
    n_erase++;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    if (off < SMP_USER_BASE || off + n > SMP_USER_BASE + 3u * SMP_USER_SIZE)
        return -1;
    for (i = 0; i < n; i++)
        slot_mem(off)[i] &= src[i];
    n_prog++;
    return 0;
}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
#include "../firmware/src/storage.c"
static struct { int force; } ui;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
#include "../firmware/src/fm6_store.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("fm6 store: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void frame(const uint8_t *m, uint32_t n)          /* a SysEx frame through the USB byte path */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        fm6_sx_byte(m[i]);
    fm6_service();
}

static uint32_t bank_msg(uint8_t *m, uint32_t salt)      /* a 32-voice dump of distinct random voices */
{
    static int16_t ed[FM6_NP];
    uint32_t k, i, x = 12345u + salt;
    m[0] = 0xF0, m[1] = 0x43, m[2] = 0x00, m[3] = 0x09, m[4] = 0x20, m[5] = 0x00;
    for (k = 0; k < 32u; k++) {
        for (i = 0; i < FM6_NP; i++) {
            x = x * 1103515245u + 12345u;
            fm6_set(ed, i, (int32_t)((x >> 16) % 128u));
        }
        ed[FV_NAME] = (int16_t)('A' + k % 26u);
        fm6_pack(m + 6 + k * 128u, ed);
    }
    m[4102] = fm6_chk(m + 6, 4096);
    m[4103] = 0xF7;
    return 4104;
}

int main(void)
{
    static uint8_t m[4104], keep[4104];
    static int16_t ed[FM6_NP], ed2[FM6_NP];
    smp_user_hdr_t *h0 = (smp_user_hdr_t *)slot_mem(SMP_USER_BASE);
    uint32_t i, n;
    host_tracks_init();
    memset(host_slots, 0xFF, sizeof host_slots);          /* three erased USR slots */
    fm6_boot();
    fm6_user(ed, 3);
    fm6_from_rom(ed2, &FM6_INIT);
    check("boot, no bank in flash: U01..U32 are the init voice", !fm6_bank_xip && !memcmp(ed, ed2, FN_PBUP * sizeof ed[0]));

    h0->magic = SMP_USER_MAGIC;                            /* USR1 holds a sample (its header) */
    smp_user_scan(1);
    host_preset(&trk[0], ENG_IX_FM6, 0);
    trk[0].eng_req = trk[0].engine = (uint8_t)ENG_IX_FM6;
    song.sel = 0;
    n = bank_msg(m, 0);
    memcpy(keep, m, n);
    frame(m, n);
    check("a 32-voice dump: saved (\"FM6 BANK SAVED\")", str_eq(last_msg, "FM6 BANK SAVED"));
    check("... into USR2, the first slot never written (USR1 holds a sample: untouched)",
          fm6_bank_xip == smp_user_xip(1) + FM6_BANK_OFF && h0->magic == SMP_USER_MAGIC &&
              ((const smp_user_hdr_t *)smp_user_xip(1))->magic == FM6_MAGIC);
    check("... the 4096 bytes as sent", !memcmp(fm6_bank_xip, keep + 6, 4096));
    check("... the sample engine sees no sample there", !usr_nz[1]);
    fm6_bank_xip = 0;
    fm6_boot();
    check("boot: the bank found again (header, CRC)", fm6_bank_xip == smp_user_xip(1) + FM6_BANK_OFF);
    fm6_user(ed, 7);
    fm6_unpack(ed2, keep + 6 + 7 * 128u);
    check("VOICE U08 reads voice 8 of the bank", !memcmp(ed, ed2, FN_PBUP * sizeof ed[0]));

    n = bank_msg(m, 99);
    frame(m, n);
    check("a second dump goes over the bank (USR2 again, USR3 left free)",
          fm6_bank_xip == smp_user_xip(1) + FM6_BANK_OFF && !memcmp(fm6_bank_xip, m + 6, 4096) &&
              ((const smp_user_hdr_t *)smp_user_xip(2))->magic == 0xFFFFFFFFu);
    memcpy(keep, fm6_bank_xip, 4096);
    m[50] ^= 1;
    frame(m, n);
    check("a dump with a bad checksum: ignored", fm6_bank_xip && !memcmp(fm6_bank_xip, keep, 4096));
    memmove(keep + 6, keep, 4096);                      /* (keep + 6: the bank in flash now) */

    /* a voice dump into the FM6 part (the selected track), then a parameter change, a function change */
    {
        uint8_t v[163] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
        fm6_from_rom(ed, &FM6_ROM[5]);
        for (i = 0; i < 155u; i++)
            v[6 + i] = (uint8_t)ed[i];
        v[161] = fm6_chk(v + 6, 155);
        v[162] = 0xF7;
        fm6_ed[0][FV_ON + 2] = 0;
        frame(v, sizeof v);
        check("a voice dump: into the part's buffer, every operator on, the notes stop (panic)",
              !memcmp(fm6_ed[0], ed, 155 * sizeof ed[0]) && fm6_ed[0][FV_ON + 2] == 1 && fm6_pt[0].panic);
    }
    {
        static const uint8_t alg[7] = {0xF0, 0x43, 0x10, 0x01, 0x06, 0x11, 0xF7};   /* 128 + 6: ALG = 18 */
        static const uint8_t sw[7] = {0xF0, 0x43, 0x10, 0x01, 0x1B, 0x2F, 0xF7};    /* 155: OP2 off (bit 4) */
        static const uint8_t fn[7] = {0xF0, 0x43, 0x10, 0x08, 0x45, 0x63, 0xF7};    /* 69: portamento time 99 */
        frame(alg, 7);
        frame(sw, 7);
        frame(fn, 7);
        check("parameter changes: ALG, the operator switches, a function (portamento time)",
              fm6_ed[0][FV_ALG] == 17 && fm6_ed[0][FV_ON + 1] == 0 && fm6_ed[0][FV_ON + 0] == 1 &&
                  fm6_ed[0][FN_PTIME] == 127);
    }

    /* STORE: the part's voice into U05; VOICE follows; read back as VOICE U05 */
    fm6_ed[0][FV_NAME] = 'Z';
    memcpy(ed, fm6_ed[0], sizeof ed);
    fm6_store(4);
    check("STORE into U05: \"STORED <name>\"", !strncmp(last_msg, "STORED Z", 8));
    check("... VOICE and the loaded mark follow", trk[0].p[P_E0] == (int16_t)(FM6_NROM + 4u) && fm6_cur[0] == (int16_t)(FM6_NROM + 5u));
    fm6_user(ed2, 4);
    check("... U05 in flash holds the voice (switches are not part of a DX7 voice)", !memcmp(ed2, ed, 155 * sizeof ed[0]));
    fm6_user(ed2, 7);
    fm6_unpack(ed, keep + 6 + 7 * 128u);
    check("... the other voices of the bank kept", !memcmp(ed2, ed, FN_PBUP * sizeof ed[0]));
    check("... fm6_rx free again for the USB receiver", !fm6_rx_ready);

    /* a sample upload over the bank's slot (its header erased): the bank is gone, U.. the init voice */
    memset(slot_mem(SMP_USER_BASE + SMP_USER_SIZE), 0xFF, 0x1000u);
    smp_user_scan(1);
    fm6_service();
    fm6_user(ed, 4);
    fm6_from_rom(ed2, &FM6_INIT);
    check("the bank's slot written over: found gone at the next service, U05 the init voice",
          !fm6_bank_xip && !memcmp(ed, ed2, FN_PBUP * sizeof ed[0]));
    /* a damaged bank (CRC) is not used */
    n = bank_msg(m, 7);
    frame(m, n);
    slot_mem(SMP_USER_BASE + SMP_USER_SIZE + FM6_BANK_OFF)[100] ^= 0x40;
    fm6_bank_find();
    check("a damaged bank (CRC): not used", !fm6_bank_xip);

    /* every USR slot holds a sample: nothing is written, the dump and STORE are refused */
    for (i = 0; i < 3u; i++)
        ((smp_user_hdr_t *)slot_mem(SMP_USER_BASE + i * SMP_USER_SIZE))->magic = SMP_USER_MAGIC;
    n_erase = n_prog = 0;
    n = bank_msg(m, 3);
    frame(m, n);
    check("three samples: a dump refused (\"FM6 BANK: NO USR SLOT\"), no flash written",
          str_eq(last_msg, "FM6 BANK: NO USR SLOT") && !n_erase && !n_prog);
    trk[0].p[P_E0] = 2;
    fm6_store(9);
    check("three samples: STORE refused (\"NO FREE USR SLOT\"), VOICE unchanged",
          str_eq(last_msg, "NO FREE USR SLOT") && trk[0].p[P_E0] == 2 && !fm6_rx_ready);

    printf("fm6 store: %s\n", fails ? "FAILED" : "all checks passed");
    return fails != 0;
}
