/* SPDX-License-Identifier: GPL-3.0-only */
/* A whole step-2 install replayed between a real host (tools/fm1_install.py, through tests/install_replay_test.py) and the
 * real update loader code (docs/INSTALL-SPEED.md): the loader's ldr_core.c and ota.c, the frames on stdin / stdout as
 * they go over USB-MIDI (F0 .. F7), a flash model in RAM. The host does not know it is not an FM-1.
 *   install_replay OLD.fwsc NEW.fwsc FLASH.bin [CUT]
 * FLASH.bin: the flash the replay starts from (OLD's flash.bin and an update record when it does not exist yet) and
 * writes back at the end; CUT: the number of answered reads after which the "FM-1" goes away (a cut-off install).
 * Built with -DREPLAY_BSP: fm1-base's loader (fm1base/bsp/loader, what the release packages carry), else Optimist's
 * own (firmware/loader). With LDR_FAST_CHUNK / OTA_FAST_CHUNK: the fast install's loader, else an older one.
 * Counts on stderr as one line of JSON. */
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FELUCCA_ID "ota-FM-1_700"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000];
static uint32_t answers, cut, erases, progs, frames_in, bytes_in, frames_out, bytes_out, ota_req;

/* ---- the wire ---- */
static uint8_t inbuf[8192], frame[8192];
static uint32_t in_len, frame_len, frame_full;

static uint32_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void save_and_exit(const char *path, int code);
static const char *flash_path;

/* take what stdin has (wait up to ms): whole F0..F7 messages; the upgrade command sets ota_req, any other one waits in
 * frame (one at a time, as usb.c: a second while one is pending is dropped) */
static void pump(int ms)
{
    struct pollfd p = {0, POLLIN, 0};
    uint32_t i, start;
    if (poll(&p, 1, ms) <= 0)
        return;
    ssize_t n = read(0, inbuf + in_len, sizeof inbuf - in_len);
    if (n <= 0)
        save_and_exit(flash_path, 3);                /* the host closed the link */
    in_len += (uint32_t)n;
    for (;;) {
        for (start = 0; start < in_len && inbuf[start] != 0xF0; start++) ;
        for (i = start; i < in_len && inbuf[i] != 0xF7; i++) ;
        if (i >= in_len) {
            memmove(inbuf, inbuf + start, in_len - start);
            in_len -= start;
            return;
        }
        frames_in++;
        bytes_in += i - start + 1;
        if (i - start == 5 && !memcmp(inbuf + start, "\xF0\x22\x24\x35\x7F", 5))
            ota_req = 1;
        else if (!frame_full) {
            memcpy(frame, inbuf + start + 1, i - start - 1);
            frame_len = i - start - 1;
            frame_full = 1;
        }
        memmove(inbuf, inbuf + i + 1, in_len - i - 1);
        in_len -= i + 1;
    }
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint32_t off = 0;
    frames_out++;
    bytes_out += n;
    if (n > 15 && p[0] == 0xF0) {                    /* a read request (not the identity, not the fast word) */
        uint8_t u[16];
        uint32_t acc = 0, nb = 0, o = 0, k;
        for (k = 1; k + 1 < n && o < 16; k++) {
            acc |= (uint32_t)(p[k] & 0x7F) << nb;
            nb += 7;
            if (nb >= 8) { u[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
        }
        if (o >= 3 && u[2] == 0x30 && cut && answers >= cut)
            save_and_exit(flash_path, 4);            /* the FM-1 goes away (unplugged, power) */
    }
    while (off < n) {
        ssize_t w = write(1, p + off, n - off);
        if (w <= 0 && errno != EINTR)
            return -1;
        off += w > 0 ? (uint32_t)w : 0;
    }
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!frame_full)
        return 0;
    *p = frame;
    *n = frame_len;
    return 1;
}
static void ota_frame_done(void)
{
    if (frame_full)
        answers++;
    frame_full = 0;
}
static uint32_t ota_now_ms(void) { return now(); }
static void ota_idle(void) { pump(0); }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }

/* ---- the flash ---- */
static uint32_t er_on;
static int ldr_fread(uint32_t off, void *p, uint32_t n)
{
    if (er_on) { fprintf(stderr, "flash read while an erase runs\n"); exit(9); }
    memcpy(p, nor + off, n);
    return 0;
}
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || (off & 0xFFFu)) { fprintf(stderr, "bad erase %#x\n", off); exit(9); }
    erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    progs++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ldr_erase_begin(uint32_t off) { int rc = ldr_erase(off); er_on = !rc; return rc; }
static int ldr_erase_end(void) { if (!er_on) return -1; er_on = 0; return 0; }
static uint32_t record_cleared;
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }

#ifdef REPLAY_BSP
#include "../fm1base/bsp/chip/ota.c"
#include "../fm1base/bsp/loader/ldr_core.c"
#define SERVICE() do { if (ota_service() == 10u && ota_dec[2] == 0x31) HELLO(); } while (0)
#ifdef LDR_FAST_CHUNK
#define HELLO() ldr_fast_hello()
#else
#define HELLO() (void)0
#endif
#else
static int ldr_flash_known(void) { return 1; }
static void ldr_checking(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/src/system/ota.c"
#include "../firmware/loader/ldr_core.c"
#define SERVICE() ota_service()
#endif

static void save_and_exit(const char *path, int code)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(nor, 1, sizeof nor, f);
        fclose(f);
    }
    fprintf(stderr, "{\"rc\": %d, \"answers\": %u, \"erases\": %u, \"progs\": %u, \"frames_in\": %u, \"bytes_in\": %u, "
            "\"frames_out\": %u, \"bytes_out\": %u, \"record_cleared\": %u}\n", code, answers, erases, progs, frames_in,
            bytes_in, frames_out, bytes_out, record_cleared);
    fflush(stdout);
    exit(code == 0 ? 0 : 1);
}

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}

static uint32_t flash_bin(const uint8_t *lg, uint32_t *size)
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) { *size = ota_rd32(e + 12); return ota_rd32(e + 8); }
    }
    return 0;
}

int main(int argc, char **argv)
{
    size_t olen;
    uint8_t *old;
    uint32_t ofo, osz = 0;
    FILE *f;
    if (argc < 4) { fprintf(stderr, "usage: install_replay OLD.fwsc NEW.fwsc FLASH.bin [CUT]\n"); return 2; }
    flash_path = argv[3];
    cut = argc > 4 ? (uint32_t)strtoul(argv[4], 0, 0) : 0;
    (void)argv[2];                                   /* (the host serves NEW: the "FM-1" never reads it) */
    if ((f = fopen(flash_path, "rb")) != NULL) {
        if (fread(nor, 1, sizeof nor, f) != sizeof nor) { fprintf(stderr, "short flash file\n"); return 2; }
        fclose(f);
    } else {
        uint8_t r[112] = {0};
        old = load_logical(argv[1], &olen);
        ofo = flash_bin(old, &osz);
        memset(nor, 0xFF, sizeof nor);
        memcpy(nor, old + ofo, osz < 0xE0000u ? osz : 0xE0000u);
        r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
        ota_wr16(r, ota_crc16(r + 2, 78, 0));
        memcpy(nor + 0xE4F00, r, sizeof r);
    }
    for (;;) {                                       /* loader.c's main loop: handshakes, the fast word, the upgrade */
        pump(50);
        SERVICE();
        if (ota_req) {
            int rc;
            ota_req = 0;
            rc = ldr_session();
            if (rc == 0)
                save_and_exit(flash_path, 0);
            fprintf(stderr, "session rc %d\n", rc);
        }
    }
}
