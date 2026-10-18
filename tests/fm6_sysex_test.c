/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of DX7 SysEx into FM6 (firmware/src/engines/fm6/fm6_sysex.c, the USB-ISR side in eng_fm6.c): a single voice
 * replaces the voice of the FM6 track (the selected one, else the channel's, else the first), every operator on, the notes
 * stopped; a 32-voice bank becomes the pick list (fm6_pick, RAM) and nothing is written anywhere; bad checksums are told and
 * change nothing; with no FM6 track a voice is told "NO FM6 TRACK". The parameter changes, the dump requests, the bank in
 * flash and STORE of before 2026-10-18 are gone: they are let go. */
#define FELUCCA_FM6_SYSEX 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static struct { int force; } ui;
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b)
{
    str_cpy(last_msg, a, sizeof last_msg);
    str_cpy(last_msg + str_len(last_msg), b, sizeof last_msg - str_len(last_msg));
}
#include "../firmware/src/engines/fm6/fm6_sysex.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("fm6 sysex: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void frame(const uint8_t *m, uint32_t n)          /* a SysEx frame through the USB byte path, then the main loop */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        fm6_sx_byte(m[i]);
    fm6_service();
}
static uint32_t voice_msg(uint8_t *m, uint32_t ch, const int16_t *ed)   /* VCED: 155 bytes, checksum */
{
    uint32_t i;
    m[0] = 0xF0, m[1] = 0x43, m[2] = (uint8_t)ch, m[3] = 0x00, m[4] = 0x01, m[5] = 0x1B;
    for (i = 0; i < 155u; i++)
        m[6 + i] = (uint8_t)ed[i];
    m[161] = fm6_chk(m + 6, 155);
    m[162] = 0xF7;
    return 163;
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
    static uint8_t m[4104], keep[4097];
    static int16_t ed[FM6_NP], was[FM6_NP], other0[FM6_NP];
    uint32_t i, n;
    host_tracks_init();
    trk[0].eng_req = trk[0].engine = 0;                    /* track 1: ANALOG */
    host_preset(&trk[1], ENG_IX_FM6, 0);                   /* track 2: FM6 */
    trk[1].eng_req = trk[1].engine = (uint8_t)ENG_IX_FM6;
    song.sel = 1;
    fm6_from_rom(ed, &FM6_INIT);
    for (i = 0; i < 155u; i++)
        ed[i] = (int16_t)((i * 7u + 3u) % 100u);
    ed[FV_ALG] = 17;
    for (i = 0; i < 10u; i++)
        ed[FV_NAME + i] = (int16_t)(i ? 'a' + i : 'Z');
    for (i = 0; i < 155u; i++)
        fm6_set(ed, i, ed[i]);                             /* (clamped as a voice is) */

    n = voice_msg(m, 0, ed);
    memcpy(other0, fm6_ed[0], sizeof other0);
    fm6_ed[1][FV_ON + 2] = 0;
    frame(m, n);
    check("a voice dump on the FM6 track (selected): into its buffer, every operator on, the notes stop (panic)",
          !memcmp(fm6_ed[1], ed, 155 * sizeof ed[0]) && fm6_ed[1][FV_ON + 2] == 1 && fm6_pt[1].panic);
    check("... told \"FM6 VOICE <name>\", nothing waiting for the main loop", !strncmp(last_msg, "FM6 VOICE Z", 11) && !fm6_rx_ready);
    check("... the track's VOICE and the other tracks' buffers untouched", trk[1].p[P_E0] == 0 && !memcmp(fm6_ed[0], other0, sizeof other0));

    m[100] ^= 1;                                           /* a bad checksum */
    memcpy(was, fm6_ed[1], sizeof was);
    frame(m, n);
    check("a voice dump with a bad checksum: told, nothing changed", !strcmp(last_msg, "FM6: BAD CHECKSUM") && !memcmp(fm6_ed[1], was, sizeof was));

    song.sel = 0;                                          /* the selected track is not FM6: the channel's, else the first */
    ed[FV_ALG] = 3;
    n = voice_msg(m, 1, ed);
    frame(m, n);
    check("... selected track not FM6: the voice goes to the FM6 track (the channel's, 2)", fm6_ed[1][FV_ALG] == 3);
    trk[1].eng_req = trk[1].engine = 0;
    frame(m, n);
    check("... no FM6 track at all: \"FM6: NO FM6 TRACK\"", !strcmp(last_msg, "FM6: NO FM6 TRACK"));
    trk[1].eng_req = trk[1].engine = (uint8_t)ENG_IX_FM6;
    song.sel = 1;

    n = bank_msg(m, 0);
    memcpy(was, fm6_ed[1], sizeof was);
    memcpy(keep, m + 6, sizeof keep);
    frame(m, n);
    check("a 32-voice dump: the pick list (told \"FM6: 32 VOICES IN PRESETS\")", fm6_pick_ok && !strcmp(last_msg, "FM6: 32 VOICES IN PRESETS"));
    check("... the 4096 bytes as sent (7-bit), in RAM", !memcmp(fm6_pick, keep, 4096));
    check("... the track's buffer untouched by it", !memcmp(fm6_ed[1], was, sizeof was));
    n = bank_msg(m, 99);
    m[50] ^= 1;
    frame(m, n);
    check("a dump with a bad checksum: told, and the list is gone (a new bank had begun)", !fm6_pick_ok && !strcmp(last_msg, "FM6: BAD CHECKSUM"));
    n = bank_msg(m, 7);
    frame(m, n);
    check("a dump after it: the list again, the new voices", fm6_pick_ok && !memcmp(fm6_pick, m + 6, 4096));

    {   /* what is not taken: a parameter change, a dump request, another maker's frame */
        static const uint8_t pc[7] = {0xF0, 0x43, 0x10, 0x01, 0x06, 0x11, 0xF7}, rq[5] = {0xF0, 0x43, 0x20, 0x00, 0xF7};
        static const uint8_t other[6] = {0xF0, 0x41, 0x10, 0x00, 0x01, 0xF7};
        memcpy(was, fm6_ed[1], sizeof was);
        frame(pc, sizeof pc);
        frame(rq, sizeof rq);
        frame(other, sizeof other);
        check("a parameter change, a dump request and another maker's frame: let go, nothing changes", !fm6_rx_ready &&
                  !memcmp(fm6_ed[1], was, sizeof was));
    }
    printf("fm6 sysex: %s\n", fails ? "FAILED" : "all checks passed");
    return fails != 0;
}
