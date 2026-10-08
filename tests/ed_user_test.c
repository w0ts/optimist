/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor's sample-slot and user-preset commands (firmware/src/io/editor/ed_user.c, cmds 11..21) on the host: the user
 * preset bank through upreset.c and storage.c on a simulated NOR, the sample slots on the same NOR; a minimal reply
 * harness as editor.c has it. Checks: the commands work stopped; while the transport plays (or PLAY is queued) the
 * ones that erase or write flash are refused with "stop first" (UP_PUT / UP_STORE / UP_ERASE rc 3, SMP_BEGIN /
 * SMP_ERASE rc 3, SMP_WRITE rc 5) and leave the flash untouched: an erase silences the audio ~50 ms (SLOOP 2.4).
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t nor[0x100000];
static uint32_t writes;                           /* erases and programs, any */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { writes++; memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    writes++;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static uint8_t flash_ok = 1;
static struct { int force; } ui;
static char last_msg[32];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b) { str_cpy(last_msg, a, sizeof last_msg); (void)b; }
static uint32_t up_gen;
static uint8_t sync_reload;
static int param_kept(uint32_t i) { return i == P_LEVEL || i == P_PAN || i == P_MUTE; }   /* (ui.c; not used here) */
#include "../firmware/src/storage/upreset.c"

/* the sample slots' flash (felucca.c), on the same NOR */
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { *took = 0; return st_erase(off); }
static int fl_write(uint32_t off, const void *src, uint32_t n) { return st_prog(off, src, n); }
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static void fm1_wdt_feed(void) {}

/* the reply builder, as editor.c has it */
enum { ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO,
       ED_UP_LIST, ED_UP_GET, ED_UP_PUT, ED_UP_STORE, ED_UP_LOAD, ED_UP_ERASE };
static uint8_t ed_out[600];
static uint32_t ed_n;
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
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
#include "../firmware/src/io/editor/ed_user.c"

static int cmd(uint32_t c, const uint8_t *a, uint32_t na) { ed_n = 0; return ed_user(c, a, na); }
static int fails;
static void check(const char *what, int ok)
{
    printf("ed_user: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t put_frame(uint8_t *a, uint32_t slot, const char *name)
{
    uint32_t n = 0, i;
    a[n++] = (uint8_t)slot;
    a[n++] = 0;                                     /* (engine 0) */
    for (i = 0; name[i]; i++)
        a[n++] = (uint8_t)name[i];
    a[n++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        uint32_t u = (uint32_t)(TP[i].def + 8192);
        a[n++] = u & 127u;
        a[n++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 32u; i++)
        a[n++] = 0;
    return n;
}

int main(void)
{
    static uint8_t a[640];
    uint32_t n, w0, k;
    host_tracks_init();
    memset(nor, 0xFF, sizeof nor);
    song.sel = 0;
    song.playing = 0;
    transport_req = 0;

    n = put_frame(a, 3, "MY LEAD");
    check("stopped: UP_PUT writes the slot (rc 0)", cmd(ED_UP_PUT, a, n) && ed_out[1] == 0 && up_used(3));
    a[0] = 4; a[1] = 0;
    check("stopped: UP_STORE stores the sound (rc 0)", cmd(ED_UP_STORE, a, 2) && ed_out[1] == 0 && up_used(4));

    for (k = 0; k < 2u; k++) {
        song.playing = (uint8_t)!k;
        transport_req = k ? 1u : 0u;
        w0 = writes;
        n = put_frame(a, 5, "MY PAD");
        check(k ? "PLAY queued: UP_PUT refused (rc 3), the slot not written" : "playing: UP_PUT refused (rc 3), the slot not written",
              cmd(ED_UP_PUT, a, n) && ed_out[1] == 3 && !up_used(5) && writes == w0);
        a[0] = 6; a[1] = 0;
        check(k ? "PLAY queued: UP_STORE refused (rc 3)" : "playing: UP_STORE refused (rc 3)",
              cmd(ED_UP_STORE, a, 2) && ed_out[1] == 3 && !up_used(6) && writes == w0);
        a[0] = 3;
        check(k ? "PLAY queued: UP_ERASE refused (rc 3), the preset kept" : "playing: UP_ERASE refused (rc 3), the preset kept",
              cmd(ED_UP_ERASE, a, 1) && ed_out[1] == 3 && up_used(3) && writes == w0);
        a[0] = 0;
        check(k ? "PLAY queued: SMP_BEGIN refused (rc 3), no erase" : "playing: SMP_BEGIN refused (rc 3), no erase",
              cmd(ED_SMP_BEGIN, a, 1) && ed_out[1] == 3 && writes == w0);
        check(k ? "PLAY queued: SMP_ERASE refused (rc 3), no erase" : "playing: SMP_ERASE refused (rc 3), no erase",
              cmd(ED_SMP_ERASE, a, 1) && ed_out[1] == 3 && writes == w0);
        {
            uint32_t off = SMP_USER_DATA;               /* a write at a sector start (it would erase it) */
            uint8_t w[64] = {0};
            w[0] = 0; w[1] = off & 127u; w[2] = (off >> 7) & 127u; w[3] = (off >> 14) & 127u;
            w[4] = 0; w[5] = 1; w[6] = 2;              /* pack7: 2 bytes */
            check(k ? "PLAY queued: SMP_WRITE refused (rc 5), no erase" : "playing: SMP_WRITE refused (rc 5), no erase",
                  cmd(ED_SMP_WRITE, w, 7) && ed_out[4] == 5 && writes == w0);
        }
    }
    song.playing = 0;
    transport_req = 0;
    a[0] = 3;
    check("stopped again: UP_ERASE erases (rc 0)", cmd(ED_UP_ERASE, a, 1) && ed_out[1] == 0 && !up_used(3));
    a[0] = 0;
    check("stopped again: SMP_BEGIN (rc 0)", cmd(ED_SMP_BEGIN, a, 1) && ed_out[1] == 0);
    printf("ed_user: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
