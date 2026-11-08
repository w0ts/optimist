/* SPDX-License-Identifier: GPL-3.0-only */
/* The working project's autosave (firmware/src/storage/autosave.c: autosave_tick, autosave_resume) driven as the main
 * loop drives it, on a simulated NOR, the sequencer playing a pattern for real (mix_block: voices sounding), then a
 * power cycle: the work wiped as at a fresh boot, autosave_resume, the edit must be back. When the work is written
 * (2026-11-08, "i lost some changes after black reboot"):
 *   stopped and silent   an edit, the panel at rest, AUTOSAVE_SETTLE later: written, back after a power cycle;
 *   playing              with LIVE_SAVE (its erases in short windows, the audio playing on) the same, while the
 *                        transport runs; before the fix nothing was written until a stop and the silence (the edit
 *                        lost at a power-off while playing); without LIVE_SAVE, or once the flash part held an erase
 *                        (live_save_off), it waits for the stop, as before;
 *   playing, no edit     nothing written (the playback itself changes nothing the hash covers: no flash wear);
 *   a voice sounding     stopped, a voice held on: with LIVE_SAVE written all the same (before: never while it sounded);
 *   recording            playing and recording (the audio ISR writes the steps): waits (a capture could be torn).
 * Built by tests/run_tests.sh with and without FELUCCA_LIVE_SAVE (FELUCCA_SECTIONS 16: the log). */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#include <stdint.h>
#if FELUCCA_LIVE_SAVE
static uint8_t live_save_off;                     /* (felucca.c's: an erase held the audio) */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { erases++; memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"
#if FELUCCA_MOTION
#include "../firmware/src/storage/motion_flash.c"
#define MOTION_SAVED(obj, p) motion_flash_write(obj, p)
#define MOTION_READ(obj, p) motion_flash_read(obj, p)
#else
#define MOTION_SAVED(obj, p) ((void)0)
#if FELUCCA_AUTO
#define MOTION_READ(obj, p) ((void)auto_fresh(p))
#else
#define MOTION_READ(obj, p) ((void)0)
#endif
#endif
#if FELUCCA_AUTO
#define AUTO_HASH() auto_hash()
#include "auto_view.h"              /* (the automation store as motion's old store) */
#else
#define AUTO_HASH() 0u
#endif

static struct { int force, redraw, menu; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty;
static void settings_save(void) {}
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
static void ui_say(const char *a, const char *b) { str_cpy(last_msg, a, sizeof last_msg); (void)b; }
static void sections_write(void);
static void persist_flush_now(void) { sections_write(); }   /* (project.c's: the arena written before a project opens) */
#include "../firmware/src/storage/sections/sections.c"

/* the main loop's world autosave.c reads */
static uint32_t ui_input_ms;
#include "../firmware/src/storage/autosave.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* the main loop for ms milliseconds: a pass every 15 ms (autosave_tick), the audio rendered between (the sequencer
 * runs: transport_req taken, steps played, voices sounding) */
static void run_ms(uint32_t ms)
{
    static uint32_t frac;
    uint32_t end = fm1_ms + ms;
    int32_t o[2 * CTL];
    while ((int32_t)(end - fm1_ms) > 0) {
        frac += 15u * FS;
        while (frac >= 1000u * CTL) {
            frac -= 1000u * CTL;
            mix_block(o, CTL);
        }
        fm1_ms += 15u;
        autosave_tick();
    }
}
static void play(void) { transport_req = 1; run_ms(100u); }
static void stop(void) { transport_req = 2; run_ms(100u); }
/* an edit on the panel: a value of track k, the knob let go now */
static void edit(uint32_t k, int16_t v)
{
    trk[k].p[P_CHOR] = v;
    ui_input_ms = fm1_ms;
}
static void voices_off(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            trk[p].v[i].active = 0;
}
/* a power cycle: the work wiped as at a fresh boot (felucca_init's defaults), the autosave read back; track k's value */
static int16_t after_power_cycle(uint32_t k)
{
    uint32_t i;
    transport_req = 0;
    song.playing = 0;
    song.rec = 0;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset_req(&trk[i], 0, 0);
    voices_off();
    memset(&autosave_buf, 0, sizeof autosave_buf);
    autosave_hash = autosave_seen = 0;
    autosave_dirty = autosave_tried = 0;
    autosave_resume();
    return trk[k].p[P_CHOR];
}
/* a pattern on every track (long notes on the synth tracks, a drum on each beat) */
static void pattern(void)
{
    static const uint8_t N[4] = {48, 55, 60, 64};
    uint32_t i, k;
    song.g[G_BPM] = 120;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < 16u; i++)
            put_step(&trk[k], i, i % 4u == 0u ? 1u : 0u, &N[k], i % 4u == 0u ? ST_NOTE : ST_TIE, 0);
    for (i = 0; i < 16u; i++) {
        uint8_t n = 36;
        put_step(TDRUM, i, i % 4u == 0u ? 1u : 0u, &n, i % 4u == 0u ? ST_NOTE : ST_REST, 0);
    }
}
static int sounding(void) { return !audio_quiet(); }

int main(void)
{
    int16_t got;
    uint32_t e0;
    int heard;
    memset(nor, 0xFF, sizeof nor);
    sec_pend_clear();
    sec_boot();
    host_tracks_init();
    pattern();

    /* stopped, silent: the edit settles and is written */
    edit(1, 23);
    run_ms(AUTOSAVE_SETTLE + 3000u);
    got = after_power_cycle(1);
    check("stopped, silent: an edit autosaved once settled, back after a power cycle", got == 23);

    /* playing: an edit while the transport runs, the panel at rest, a power-off before any stop */
    play();
    run_ms(AUTOSAVE_MIN_GAP);                         /* (the gap after the first save) */
    heard = sounding();
    edit(1, 57);
    run_ms(AUTOSAVE_SETTLE + 3000u);
    check("playing: the sequencer plays the pattern back from the autosave (voices sounding)", heard && song.playing);
    got = after_power_cycle(1);
#if FELUCCA_LIVE_SAVE
    check("playing (LIVE_SAVE): an edit autosaved while the transport runs, back after a power cycle", got == 57);
#else
    check("playing (no LIVE_SAVE): the edit waits for the stop (an erase stops the audio)", got == 23);
#endif

    /* playing a long time without an edit: nothing written (no wear from the playback itself) */
    play();
    run_ms(AUTOSAVE_SETTLE + 3000u);                  /* (the work as it was resumed: equal to the saved one) */
    e0 = erases;
    run_ms(4u * AUTOSAVE_MAX_WAIT);
    check("playing 8 minutes, no edit: nothing written", erases == e0);
    stop();
    voices_off();

    /* stopped, a voice held on (a drone, a key held, a long tail): before, never written while it sounded */
    run_ms(AUTOSAVE_MIN_GAP);
    edit(1, 77);
    input_on(&trk[0], 60, 100);                       /* (a key held: the voice sustains) */
    run_ms(AUTOSAVE_SETTLE + 3000u);
    heard = sounding();
    input_off(&trk[0], 60);
    got = after_power_cycle(1);
    check("stopped, a key held: a voice sounds through the wait", heard);
#if FELUCCA_LIVE_SAVE
    check("stopped, a voice sounding (LIVE_SAVE): the edit autosaved all the same", got == 77);
#else
    check("stopped, a voice sounding (no LIVE_SAVE): the edit waits for the silence", got != 77);
#endif

#if FELUCCA_LIVE_SAVE
    /* playing and recording (the audio ISR writes the steps): waits; recording over, still playing: written */
    play();
    run_ms(AUTOSAVE_MIN_GAP);
    song.rec = 1u << 3;
    edit(1, 91);
    run_ms(AUTOSAVE_SETTLE + 3000u);
    got = after_power_cycle(1);
    check("playing and recording: the autosave waits (a capture could be torn by the audio ISR)", got != 91);
    play();
    song.rec = 1u << 3;
    edit(1, 92);
    run_ms(AUTOSAVE_SETTLE);
    song.rec = 0;
    run_ms(AUTOSAVE_SETTLE + 3000u);
    got = after_power_cycle(1);
    check("... the recording over, still playing: written, back after a power cycle", got == 92);

    /* the flash part held an erase in one window (no suspend): saves while playing wait for the stop again */
    play();
    run_ms(AUTOSAVE_MIN_GAP);
    live_save_off = 1;
    edit(1, 33);
    run_ms(AUTOSAVE_SETTLE + 3000u);
    got = after_power_cycle(1);
    check("an erase held the audio (live_save_off): playing, the edit waits for the stop", got != 33);
    edit(1, 34);
    run_ms(AUTOSAVE_SETTLE + 3000u);
    got = after_power_cycle(1);
    check("... stopped and silent: written, back after a power cycle", got == 34);
    live_save_off = 0;
#endif
    printf("autosave test %s (%u erases)\n", bad ? "FAILED" : "passed", erases);
    return bad != 0;
}
