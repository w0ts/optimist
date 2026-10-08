/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the native tone collections (FELUCCA_NATIVE_BANKS, firmware/src/storage/nbank.c, ed_cz.c; after Melodee 0.12,
 * keremimo/melodee) against simulated NOR flash (erase -> 0xFF, a program only clears bits):
 *   - the CZ collection: empty at boot; a tone written (czb_put) reads back the same 144 bytes, in the A/B pair
 *     below the snapshot area (every other byte of USR1..USR3 and the banks untouched), alternating copies; found
 *     again at boot; a save cut short (the new copy's commit record torn) leaves the previous one; an erase;
 *   - st_save of a payload built in st_buf saves that payload (storage.c: it no longer reads the current copy over it);
 *   - PRESETS: an FM6 / CZ track lists its used slots (nb_list: count, its own place, the n-th), "F03" / "Z05" and the
 *     name (nb_row); a load sets only the engine's values (VOICE / TONE to the slot, the rest of EDIT to defaults; FX,
 *     mix and steps kept), and a CZ part plays the stored bytes (cz_block);
 *   - command 66 (ed_cz): GET / PUT / erase, nibbles low first, round trip; bad nibbles, a wrong length, a slot past
 *     the last refused (rc 1); the transport playing: rc 3.
 *   build/host/nbank_test   Exit status: the number of failed checks. */
#define FELUCCA_FLASH 1
#define FELUCCA_NATIVE_BANKS 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t flash_ok = 1;
static uint32_t n_erase;
static uint8_t *mem(uint32_t off) { return (uint8_t *)host_slots + (off - SMP_USER_BASE); }
static int in_sim(uint32_t off, uint32_t n) { return off >= SMP_USER_BASE && off + n <= SMP_USER_BASE + sizeof host_slots; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    *took = 0;
    if (!in_sim(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    memset(mem(off), 0xFF, 0x1000u);
    n_erase++;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    if (!in_sim(off, n))
        return -1;
    for (i = 0; i < n; i++)
        mem(off)[i] &= src[i];
    return 0;
}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!in_sim(off, n))
        return -1;
    memcpy(dst, mem(off), n);
    return 0;
}
static int st_erase(uint32_t off) { uint32_t t; return fl_erase4k_quiet(off, &t); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return fl_write(off, src, n); }
#include "../firmware/src/storage/storage.c"
static struct { int force; } ui;
static uint8_t sync_reload;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
static void up_slot_label(char *b, uint32_t k)            /* (upreset.c's) */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}
#include "../firmware/src/engines/fm6/fm6_store.c"
#include "../firmware/src/storage/nbank.c"
/* ed_cz.c's surroundings (editor.c) */
static uint8_t ed_out[600];
static uint32_t ed_n;
static void ed_b(uint32_t v) { ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static uint32_t ed_flash_busy(void) { return song.playing; }
#include "../firmware/src/io/editor/ed_cz.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("nbank: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void tone(uint8_t *b, uint32_t salt, const char *name)   /* a CZ-1 tone: valid ranges, its name */
{
    uint32_t i, x = 777u + salt * 31u;
    for (i = 0; i < 128u; i++) {
        x = x * 1103515245u + 12345u;
        b[i] = (uint8_t)(x >> 16);
    }
    b[0] = (uint8_t)(salt % 3u);
    memset(b + 128, ' ', 16);
    memcpy(b + 128, name, strlen(name));
}
static uint32_t sum_outside(uint32_t lo, uint32_t hi)    /* FNV-1a of the simulated flash outside [lo, hi) */
{
    uint32_t off, h = 2166136261u;
    for (off = SMP_USER_BASE; off < SMP_USER_BASE + sizeof host_slots; off++)
        if (off < lo || off >= hi)
            h = (h ^ *mem(off)) * 16777619u;
    return h;
}
static uint32_t cmd(const uint8_t *a, uint32_t na) { ed_n = 0; return (uint32_t)ed_cz(ED_CZ_BANK, a, na); }

int main(void)
{
    static uint8_t t1[144], t2[144], a[300], bank[4096];
    const uint32_t lo = st_sector(OBJ_CZBANK, 0), hi = lo + 2u * ST_SECTOR;
    uint32_t i, r, at, n, before, e1, e2;
    int ok;
    char tag[5], nm[13];
    host_tracks_init();
    memset(host_slots, 0xFF, sizeof host_slots);
    for (i = 0; i < sizeof host_slots / 4u; i++)          /* (USR1..USR3 hold something everywhere) */
        if ((i * 4u) % 0x1000u < 64u)
            host_slots[i] = 0x5A5A5A5Au ^ i;
    memset(mem(lo), 0xFF, 2u * ST_SECTOR);
    fm6_boot();
    czb_find();
    check("the CZ pair: 2 sectors right below the snapshot area, inside USR3, past USR3's samples",
          hi == 0xD8000u - SN_SECTORS * 0x1000u && lo >= SMP_USER_BASE + 2u * SMP_USER_SIZE && lo == SMP_USR3_END);
    check("boot, nothing stored: no collection, every slot empty", !czb_xip && !czb_tone(CZT_N) && !czb_tone(CZT_N + 25u));

    before = sum_outside(lo, hi);
    tone(t1, 1, "BRIGHT BRASS");
    check("a tone into slot 4: saved", czb_put(4, t1) == 0);
    check("... read back the same 144 bytes, in flash (copy A), the other slots empty",
          czb_xip == mem(lo) + ST_PAYLOAD_OFF && czb_tone(CZT_N + 4u) && !memcmp(czb_tone(CZT_N + 4u), t1, 144) &&
              !czb_tone(CZT_N + 3u) && !czb_tone(CZT_N + 5u));
    check("... nothing else in USR1..USR3 or the banks written", sum_outside(lo, hi) == before);
    tone(t2, 2, "SOFT PAD");
    check("a second tone into slot 25 (the last): saved into copy B, slot 4 kept",
          czb_put(25, t2) == 0 && czb_xip == mem(lo + ST_SECTOR) + ST_PAYLOAD_OFF && !memcmp(czb_tone(CZT_N + 25u), t2, 144) &&
              !memcmp(czb_tone(CZT_N + 4u), t1, 144));
    check("slot 26 (past the last): refused", czb_put(26, t1) == 1);
    czb_xip = 0;
    czb_find();
    check("boot again: both tones found", czb_tone(CZT_N + 4u) && czb_tone(CZT_N + 25u) && !memcmp(czb_tone(CZT_N + 25u), t2, 144));
    check("erase slot 4: empty, slot 25 kept", czb_put(4, 0) == 0 && !czb_tone(CZT_N + 4u) && !memcmp(czb_tone(CZT_N + 25u), t2, 144));
    check("st_save of a payload built in st_buf: that payload saved (not the copy before it)",
          czb_put(7, t1) == 0 && !memcmp(czb_tone(CZT_N + 7u), t1, 144) && czb_tone(CZT_N + 25u));
    {   /* a save cut short: the new copy's commit record torn -> the previous copy */
        uint32_t cur = czb_xip == mem(lo) + ST_PAYLOAD_OFF ? 0u : 1u, other = st_sector(OBJ_CZBANK, cur ^ 1u);
        tone(t2, 3, "LOST");
        czb_put(9, t2);
        mem(other)[20] ^= 0xFFu;                          /* (its seq: the header's CRC fails) */
        czb_find();
        check("a save cut short (its header torn): the previous copy in charge (slot 7 there, slot 9 not)",
              czb_xip == mem(st_sector(OBJ_CZBANK, cur)) + ST_PAYLOAD_OFF && czb_tone(CZT_N + 7u) && !czb_tone(CZT_N + 9u));
    }

    /* PRESETS on a CZ track */
    song.sel = 0;
    host_preset(&trk[0], ENG_IX_CZ, 4);
    trk[0].p[P_E1] = 30;
    trk[0].p[P_DIST] = 77;
    trk[0].p[P_REV] = 55;
    trk[0].p[P_LEVEL] = 90;
    trk[0].step[3].note[0] = 61;
    n = nb_list(&r, 1, &at);
    check("a CZ track: its used slots listed (7, 25), it plays none", n == 2u && r == 2u && at == 25u);
    nb_row(25, tag, nm);
    check("... row: \"Z26\" SOFT PAD", str_eq(tag, "Z26") && str_eq(nm, "SOFT PAD"));
    nb_load(25);
    check("load: TONE = U26, the other EDIT values to their defaults",
          trk[0].p[P_E0] == (int16_t)(CZT_N + 25u) && trk[0].p[P_E1] == ENG_CZ.edit[1].def && !trk[0].user);
    check("... FX, mix and pattern kept", trk[0].p[P_DIST] == 77 && trk[0].p[P_REV] == 55 && trk[0].p[P_LEVEL] == 90 &&
          trk[0].step[3].note[0] == 61);
    nb_list(&r, ~0u, &at);
    check("... the list's cursor on it", r == 1u);
    cz_block(&trk[0]);
    tone(t2, 2, "SOFT PAD");
    check("... the part plays the stored bytes (cz_block)", !memcmp(cz_tone[0], t2, 128));
    czb_put(25, t1);
    cz_block(&trk[0]);
    check("the slot written again: the part takes the new bytes", !memcmp(cz_tone[0], t1, 128));
    czb_put(25, 0);
    cz_block(&trk[0]);
    cz_encode(&CZ_TONES[CZT_INIT], t2);
    check("the slot erased: the part plays INIT, the list skips it", !memcmp(cz_tone[0], t2, 128) && nb_list(&r, ~0u, &at) == 1u && r == 1u);
    nb_load(25);
    check("an empty slot: no load", trk[0].p[P_E0] == (int16_t)(CZT_N + 25u));

    /* PRESETS on an FM6 track: the FM6 bank's named voices */
    {
        static int16_t ed[FM6_NP];
        for (i = 0; i < 32u; i++) {
            fm6_from_rom(ed, &FM6_INIT);
            if (i == 2u)
                memcpy(&ed[FV_NAME], (int16_t[10]){'B', 'R', 'A', 'S', 'S', ' ', ' ', ' ', ' ', ' '}, sizeof(int16_t[10]));
            fm6_pack(bank + i * 128u, ed);
        }
    }
    ok = !fm6_bank_save(bank);
    host_preset(&trk[1], ENG_IX_FM6, 0);
    trk[1].p[P_DLY] = 44;
    song.sel = 1;
    n = nb_list(&r, 0, &at);
    nb_row(2, tag, nm);
    check("an FM6 track: the bank's voices not INIT VOICE (U03 BRASS), \"F03\"",
          ok && n == 1u && at == 2u && str_eq(tag, "F03") && str_eq(nm, "BRASS"));
    nb_load(2);
    check("... load: VOICE U03, delay send kept", trk[1].p[P_E0] == (int16_t)(FM6_NROM + 2u) && trk[1].p[P_DLY] == 44 &&
          !fm6_cur[1]);

    /* command 66 */
    tone(t1, 5, "CMD TONE");
    a[0] = 1, a[1] = 3;
    for (i = 0; i < 144u; i++)
        a[2 + 2 * i] = t1[i] & 15u, a[3 + 2 * i] = t1[i] >> 4;
    ok = cmd(a, 290) && ed_n == 3u && ed_out[0] == 1 && ed_out[1] == 3 && ed_out[2] == 0;
    a[0] = 0;
    ok = ok && cmd(a, 2) && ed_n == 3u + 288u && ed_out[2] == 0;
    for (i = 0; ok && i < 144u; i++)
        ok = (uint8_t)(ed_out[3 + 2 * i] | ed_out[4 + 2 * i] << 4) == t1[i];
    check("66: PUT slot 3 then GET: the same 144 bytes back (nibbles low first)", ok);
    a[0] = 0, a[1] = 4;
    check("66: GET of an empty slot: rc 1, no data", cmd(a, 2) && ed_n == 3u && ed_out[2] == 1);
    a[0] = 1, a[1] = 3, a[2] = 16;
    e1 = n_erase;
    check("66: PUT with a nibble past 15: rc 1, nothing written", cmd(a, 290) && ed_out[2] == 1 && n_erase == e1);
    check("66: PUT of a wrong length: rc 1", cmd(a, 100) && ed_out[2] == 1);
    a[1] = 26;
    check("66: slot 26 (past the last): rc 1", cmd(a, 2) && ed_out[2] == 1);
    song.playing = 1;
    a[1] = 3;
    e2 = n_erase;
    check("66: the transport plays: rc 3, nothing written", cmd(a, 2) && ed_out[2] == 3 && n_erase == e2);
    song.playing = 0;
    check("66: erase slot 3 (no nibbles): rc 0, empty", cmd(a, 2) && ed_out[2] == 0 && !czb_tone(CZT_N + 3u));
    check("66: another command: not ours", !ed_cz(ED_CZ_BANK + 1u, a, 2));
    printf("nbank: %d failed\n", fails);
    return fails;
}
