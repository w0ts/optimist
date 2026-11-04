/* SPDX-License-Identifier: GPL-3.0-only */
/* The switchable features' memory (the user, 2026-10-30: "clearly RAM is now our limiting factor"): a feature the device
 * switches on and off (Bluetooth audio first; BT MIDI, USB audio, Wi-Fi, its AP, the web editor and the USB MIDI host
 * as their buffers become lendable) holds a block of the arena only while it is on, and the arena has it back while it
 * is off. This file is the policy, the same for every feature; the features themselves are the includer's table
 * (FMEM_FEATS: fm1-base's fm1base/optimist/ui/opt_feat.h, the host tests' fakes), so the idle signals live in one place:
 *   name   what the questions say ("BT AUDIO")
 *   where  where it is switched back on ("SYSTEM > BT")
 *   k      its arena client (core/arena_bufs.c ARB_*), FMEM_NO_K: not lendable in this build (never offered)
 *   bytes  its block
 *   on     its setting is on
 *   idle   on, and nothing uses it now (no device connected, no host streaming, no peer, no session seen lately)
 *   off    switched off as the user would (kept in the settings record): its block goes back by itself (arb_poll)
 * Switching on (the user's rules, 2026-10-30):
 *   FMEM_FITS   the block fits in the free room as it is, or once quiet blocks move (nothing heard: core/arena.c
 *               arn_pack): taken, nothing said beyond the setting
 *   FMEM_PACK   it fits only once every block moves, as a load's pack does: asked first ("BT AUDIO 18 KB PAUSE 3 MS"),
 *               on YES the audio paused (the transport stopped, what sounds faded out, as a load), the arena packed,
 *               the block placed, the transport started again if it ran; never compacted unasked while it plays
 *   FMEM_NOFIT  not even so: "BT AUDIO 18 KB, 6 KB FREE", and UNLOAD offered: the project saved (the PROJECT page's
 *               SAVE), a new one in its place, then the block placed; NO: the setting back off, nothing changed
 * Switching off never pauses: the block given back (arb_poll, two audio blocks later), packed at the next load or
 * quiet moment as today.
 * A load or an edit that finds no room (model.c mem_offer): before any part is proposed for dropping, the features on
 * but idle are offered, all at once ("OFF BT AUDIO 18 KB"); YES switches them off (kept), the change tried again
 * once their blocks are back. A feature in use is never offered, and none is switched off without the YES.
 * The pause's estimate (fmem_pause_ms): the fade of what sounds (FMEM_HUSH_MS: voice_kill's 1.5 ms and a block; a key
 * still held is waited for FMEM_FADE_MS at most)
 * and the moves' copy, FMEM_US_PER_KB a KB (the rate the move's windows are sized for, ARN_MV_CHUNK in ARN_MV_US:
 * 2,048 B in 40 us; the FM-1's own copy rate is unmeasured). Measured on the host (tests/feat_mem_test.c): 2 ms of audio
 * blocks from the YES to the block placed, 4 KB moved. Main loop only. Included by core/arena_bufs.c */
#ifndef FMEM_FADE_MS
#define FMEM_FADE_MS 30u                                /* (the wait for the parts to be quiet, at most: voice_kill fades a voice
                                                         * in 1.5 ms, a key still held is not waited for longer) */
#endif
#define FMEM_HUSH_MS ((KILL_BLOCKS + 1u) * CTL * 1000u / FS + 1u)   /* (voice_kill's fade and a block: 2 ms) */
#ifndef FMEM_US_PER_KB
#define FMEM_US_PER_KB ((ARN_MV_US * 1024u + ARN_MV_CHUNK - 1u) / ARN_MV_CHUNK)   /* (the windows' rate: 20 us) */
#endif
#define FMEM_NO_K 0xFFu
enum { FMEM_FITS, FMEM_PACK, FMEM_NOFIT };
typedef struct {
    const char *name, *where;
    uint8_t k;
} fmem_feat_t;                                          /* (its bytes, on, idle and off: FMEM_BYTES(f) .. FMEM_OFF(f)) */
static const fmem_feat_t FMEM[] = {FMEM_FEATS};
#define FMEM_N (sizeof FMEM / sizeof FMEM[0])
_Static_assert(sizeof FMEM / sizeof FMEM[0] <= 8u, "feat_mem.c: a byte of features (fmem_offer's mask)");

static struct {
    uint8_t ask;                                        /* the feature whose switch-on waits for the question (+1, 0 none) */
    uint8_t how;                                        /* .. FMEM_PACK or FMEM_NOFIT */
    uint8_t packing;                                    /* YES to PACK: the pack under way (+1), its transport to start */
    uint8_t was_playing;
    uint8_t stage;                                      /* .. 1 fading (what sounds, before any block moves), 2 packing */
    uint8_t passes;                                     /* (passes since: the fade's bound, the placement's tries) */
    uint8_t offered;                                    /* the idle ones offered to this refusal (a bit each) */
    uint8_t shown;                                      /* the question armed (op_input.c mem_tick): let go without YES = NO */
    uint32_t need;                                      /* (the question's bytes) */
    uint32_t t0;                                        /* (fm1_ms at the YES: the fade's bound) */
} fmem;

static int fmem_lendable(uint32_t f) { return f < FMEM_N && FMEM[f].k != FMEM_NO_K && FMEM_BYTES(f) != 0u; }

/* feature f's block (n bytes) on, now: FMEM_FITS, FMEM_PACK or FMEM_NOFIT (nothing changed) */
static int fmem_how(uint32_t f, uint32_t *moved)
{
    uint32_t k = FMEM[f].k, n = FMEM_BYTES(f);
    *moved = 0;
    if (arn_fits(k, n, 0) || arn_would_pack(k, n, 0, moved) >= 0)
        return FMEM_FITS;
    return arn_would_pack(k, n, 1, moved) > 0 ? FMEM_PACK : FMEM_NOFIT;
}
static uint32_t fmem_pause_ms(uint32_t moved)           /* the pause a PACK costs, ms (rounded up) */
{
    return FMEM_HUSH_MS + (moved / 1024u * FMEM_US_PER_KB + 999u) / 1000u;
}
static uint32_t fmem_kb(uint32_t n) { return (n + 1023u) / 1024u; }

/* the features on and idle that could make room (a bit each), and their bytes */
static uint32_t fmem_idle_mask(uint32_t *bytes)
{
    uint32_t f, m = 0;
    *bytes = 0;
    for (f = 0; f < FMEM_N; f++)
        if (fmem_lendable(f) && FMEM_ON(f) && FMEM_IDLE(f)) {
            m |= 1u << f;
            *bytes += FMEM_BYTES(f);
        }
    return m;
}
/* the question's text for those (a: "BT AUDIO 18 KB WI-FI 9 KB", cut to n) */
static void fmem_list(char *a, uint32_t n, uint32_t mask)
{
    uint32_t f, l;
    a[0] = 0;
    for (f = 0; f < FMEM_N; f++) {
        if (!(mask >> f & 1u))
            continue;
        if (a[0])
            str_app(a, " ", n);
        str_app(a, FMEM[f].name, n);
        str_app(a, " ", n);
        l = str_len(a);
        if (l + 6u < n)
            fmt_int(a + l, (int32_t)fmem_kb(FMEM_BYTES(f)));
        str_app(a, " KB", n);
    }
}
/* YES to the offer: those switched off (kept, as the user's own switch) */
static void fmem_off(uint32_t mask)
{
    uint32_t f;
    for (f = 0; f < FMEM_N; f++)
        if ((mask >> f & 1u) && FMEM_ON(f))
            FMEM_OFF(f);
}

/* model.c mem_offer, a load's or an edit's refusal: the idle features not offered yet, all at once (a mask, 0 none;
 * fmem_offer_reset with each new refusal) */
static uint32_t fmem_offer(void)
{
    uint32_t b, m = fmem_idle_mask(&b) & ~(uint32_t)fmem.offered;
    fmem.offered |= (uint8_t)m;
    return m;
}
static void fmem_offer_reset(void) { fmem.offered = 0; }

/* core/arena_bufs.c arb_poll: feature client c's block refused (no room as it is, no quiet move makes it): the question
 * asked by the UI (fmem_question), not the MEM FULL of the other buffers. 1: taken over (the feature's), 0 not one */
static int fmem_refused(uint32_t k)
{
    uint32_t f, moved;
    for (f = 0; f < FMEM_N; f++) {
        if (!fmem_lendable(f) || FMEM[f].k != k)
            continue;
        if (!fmem.ask && !fmem.packing) {
            fmem.how = (uint8_t)fmem_how(f, &moved);
            if (fmem.how == FMEM_FITS)
                return 1;                               /* (a quiet move will make it: arb_poll's pack, next pass) */
            fmem.ask = (uint8_t)(f + 1u);
            fmem.need = FMEM_BYTES(f);
        }
        return 1;
    }
    return 0;
}
/* the UI (op_input.c mem_tick): the question waiting, 0 none -> its verb, text and red; *f its feature */
static const char *fmem_question(char *a, uint32_t n, uint32_t *f, int *red)
{
    uint32_t moved, l;
    if (!fmem.ask)
        return 0;
    *f = fmem.ask - 1u;
    if (!FMEM_ON(*f)) {                               /* (switched off meanwhile: nothing to ask) */
        fmem.ask = 0;
        return 0;
    }
    fmem.how = (uint8_t)fmem_how(*f, &moved);
    if (fmem.how == FMEM_FITS) {                        /* (room made meanwhile: placed by arb_poll) */
        fmem.ask = 0;
        return 0;
    }
    str_cpy(a, FMEM[*f].name, n);                      /* the confirm's two lines (op_state.c: 11 and 16 characters):
                                                         * "PAUSE 31MS?" / "BT AUDIO 18 KB", or "UNLOAD?" / the same
                                                         * with "6 KB FREE" on the message line */
    str_app(a, " ", n);
    l = str_len(a);
    fmt_int(a + l, (int32_t)fmem_kb(fmem.need));
    str_app(a, " KB", n);
    if (fmem.how == FMEM_PACK) {
        static char v[12];
        str_cpy(v, "PAUSE ", sizeof v);
        l = str_len(v);
        fmt_int(v + l, (int32_t)fmem_pause_ms(moved));
        str_app(v, "MS", sizeof v);
        *red = 0;
        return v;
    }
    {
        char m[20];
        fmt_int(m, (int32_t)(arn_room() / 1024u));
        str_app(m, " KB FREE", sizeof m);
        ui_message(m);
    }
    *red = 1;
    return "UNLOAD";
}
/* the UI each frame (op_input.c mem_tick; armed: a confirm is shown) -> the question to arm now (its verb; a its text,
 * *f its feature, *red), 0 none. The question armed and then let go without YES (NO, another row, 3 s): the answer
 * NO (the setting back off); a YES comes through fmem_answer (op_layers.c OA_FMEM) */
static void fmem_answer(uint32_t f, int yes);
static const char *fmem_ui(int armed, char *a, uint32_t n, uint32_t *f, int *red)
{
    const char *v;
    if (fmem.shown) {
        if (armed)
            return 0;
        fmem.shown = 0;
        if (fmem.ask)
            fmem_answer(fmem.ask - 1u, 0);
        return 0;
    }
    if (armed)
        return 0;
    v = fmem_question(a, n, f, red);
    fmem.shown = (uint8_t)(v != 0);
    return v;
}
static const char *fmem_where(uint32_t mask)          /* (after the offer's YES: where the first is switched back on) */
{
    uint32_t f;
    for (f = 0; f < FMEM_N; f++)
        if (mask >> f & 1u)
            return FMEM[f].where;
    return "";
}
/* the pause's silence: every part's sounding voices faded out over KILL_BLOCKS (voice.c voice_kill: no click), the
 * interrupts off (the audio reads them) */
static void fmem_hush(void)
{
    uint32_t k, i;
    fm1_irq_off();
    for (k = 0; k < NPART; k++) {
        trk[k].nmono = 0, trk[k].mono_note = 0;
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active && trk[k].v[i].stage)
                voice_kill(&trk[k].v[i]);
    }
    fm1_irq_on();
}
/* the answer: YES to PACK (the pause and the pack, fmem_poll places it), YES to UNLOAD (the includer's save and new
 * project, FMEM_UNLOAD, then placed as any switch-on), NO (the confirm let go): the setting back off */
static void fmem_answer(uint32_t f, int yes)
{
    uint32_t how = fmem.how;
    fmem.ask = 0;
    if (!yes) {
        if (FMEM_ON(f))
            FMEM_OFF(f);
        return;
    }
    if (how == FMEM_PACK) {
        fmem.was_playing = (uint8_t)FMEM_PLAYING();
        FMEM_PAUSE();                                   /* (the transport stopped) */
        fmem_hush();                                    /* (what sounds faded out, 1.5 ms: voice.c voice_kill) */
        fmem.packing = (uint8_t)(f + 1u);
        fmem.stage = 1, fmem.passes = 0, fmem.t0 = fm1_ms;
    } else if (how == FMEM_NOFIT) {
        FMEM_UNLOAD(f);                                  /* (saved, then a new project: its blocks back as it switches) */
    }
}
/* every pass (arb_poll, before it places): a YES to PACK followed through. The fade first (no block moves while a part
 * still sounds: FMEM_QUIET, at most FMEM_FADE_MS: a key still held is not waited for), then every block packed as a load's (core/arena.c arn_poll, the
 * parts gated while each moves: silent by then), then arb_poll places the block; then the transport goes on as it was.
 * Still no room after the pack (something took it): the setting back off, MEM FULL said */
static void fmem_poll(void)
{
    uint32_t f;
    if (!fmem.packing)
        return;
    f = fmem.packing - 1u;
    if (!FMEM_ON(f) || arn_ptr(FMEM[f].k)) {         /* (placed, or switched off meanwhile) */
        fmem.packing = fmem.stage = 0;
        if (fmem.was_playing)
            FMEM_RESUME();
        return;
    }
    fmem.passes++;
    if (fmem.stage == 1u) {
        if (!FMEM_QUIET() && fm1_ms - fmem.t0 < FMEM_FADE_MS)
            return;
        arn_pack_all();                                 /* (every block, as a load's) */
        fmem.stage = 2, fmem.passes = 0;
        return;
    }
    if (arn_busy() || arn_pk.all || fmem.passes < 3u)
        return;                                         /* (the pack under way; then arb_poll's two tries) */
    fmem.packing = fmem.stage = 0;
    FMEM_OFF(f);
    FMEM_SAY_FULL(f);
    if (fmem.was_playing)
        FMEM_RESUME();
}
