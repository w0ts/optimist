/* SPDX-License-Identifier: GPL-3.0-only */
/* The switchable features' stand-ins for the host tests (tests/feat_mem_test.c, ui_optimist_test.c UI_FMEM): fm1-base's
 * Bluetooth audio lending (plat.h plat_bta_mem) on a stand-in radio that writes into its block while it runs and for a
 * few passes after its mode goes off (the device's run let go), the lending as the release has it
 * (fm1base/optimist/ui/opt_bta_lend.h), and the features' table: BT AUDIO lendable, WI-FI not. Under ASan a block given
 * back is poisoned (ARN_TOOK unpoisons one the arena hands out). Included before hostsim.c */
#include <string.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAVE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HAVE_ASAN 1
#endif
#ifndef HAVE_ASAN
#define HAVE_ASAN 0
#endif
#if HAVE_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n) ASAN_POISON_MEMORY_REGION(p, n)
#define UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION(p, n)
#define POISONED(p) __asan_address_is_poisoned(p)
#else
#define POISON(p, n) ((void)(p), (void)(n))
#define UNPOISON(p, n) ((void)(p), (void)(n))
#define POISONED(p) 1
#endif
#define ARN_TOOK(p, n) UNPOISON(p, n)

/* ---- the stand-in fm1-base: the radio and its lending (plat.h plat_bta_mem), as bt_own_audio.c answers */
#define RB_BYTES (13360u + 12u * 364u)                  /* (bt_page.c BP_MEM_BYTES: the queue and the ring) */
static struct {
    uint8_t mode, streaming, busy;                      /* an AUDIO mode on (Optimist's bta.mode); a device streaming;
                                                         * passes still reading after the mode went off */
    uint8_t *p;
    uint32_t writes, lends, takes, refused;
} rb;
static uint32_t plat_bta_mem_bytes(void) { return RB_BYTES; }
static int plat_bta_mem(void *p)
{
    if (!p) {
        if (rb.mode || rb.busy) {
            rb.refused++;
            return -1;
        }
        if (rb.p) {
            POISON(rb.p, RB_BYTES);                     /* (the arena has it back: the radio must not touch it) */
            rb.takes++;
        }
        rb.p = 0;
        return 0;
    }
    rb.p = (uint8_t *)p;
    rb.lends++;
    return 0;
}
static void radio(void)                                 /* the radio's queue at work, a pass */
{
    if (rb.p && (rb.mode || rb.busy))
        rb.p[(rb.writes++ * 7u) % RB_BYTES]++;
    if (!rb.mode && rb.busy)
        rb.busy--;
}
static void bta_set(int on)                             /* SYSTEM > BT's AUDIO MODE */
{
    if (!on && rb.mode)
        rb.busy = 5;                                    /* (bta_stop: the run let go over a few passes) */
    rb.mode = (uint8_t)(on != 0);
}
#define OPT_BTA_ON() (rb.mode != 0)
#include "../fm1base/optimist/ui/opt_bta_lend.h"

/* ---- the features' table: BT AUDIO lendable, WI-FI not (never offered) */
enum { TF_BTA, TF_WIFI };
static uint8_t wifi_on = 1, wifi_idle = 1, paused, resumed, unloads, full_said;
static uint32_t tf_bytes(uint32_t f) { return f == TF_BTA ? bta_lend_bytes() : 0u; }
static int tf_on(uint32_t f) { return f == TF_BTA ? rb.mode : wifi_on; }
static int tf_idle(uint32_t f) { return f == TF_BTA ? rb.mode && !rb.streaming : wifi_on && wifi_idle; }
static void tf_off(uint32_t f)
{
    if (f == TF_BTA)
        bta_set(0);
}
#define FMEM_BYTES(f) tf_bytes(f)
#define FMEM_ON(f) tf_on(f)
#define FMEM_IDLE(f) tf_idle(f)
#define FMEM_OFF(f) tf_off(f)
#define FMEM_FEATS {"BT AUDIO", "SYSTEM > BT", ARB_K(ARB_BTA)}, {"WI-FI", "SYSTEM > WI-FI", FMEM_NO_K}
static int tf_playing(void);
static void tf_pause(void);
static void tf_resume(void) { resumed++; }
static int tf_quiet(void);
static void tf_unload(void) { unloads++; }
static void tf_full(uint32_t f) { (void)f, full_said++; }
#define FMEM_PLAYING() tf_playing()
#define FMEM_PAUSE() tf_pause()
#define FMEM_RESUME() tf_resume()
#define FMEM_QUIET() tf_quiet()
#define FMEM_UNLOAD() tf_unload()
#define FMEM_SAY_FULL(f) tf_full(f)

