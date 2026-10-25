/* SPDX-License-Identifier: GPL-3.0-only */
/* The RAMTEXT overlays (FELUCCA_OVERLAY; docs/SD-MODE-DESIGN.md 6): the engines' hot code (OVL / OVLX, core.h) in a
 * slot per part, loaded when the part starts playing an engine, instead of every engine's in RAMTEXT from power-on.
 *
 * tools/build.py links the firmware once per slot (NPART links; the slots' addresses differ, nothing else): each
 * engine's code is linked at every slot's address, so it runs from any slot as linked (no position-independent code),
 * and the resident image is checked to be the same in every link (nothing resident names an overlay's code: it is
 * reached through its hooks' table, rto_hdr_t, the first bytes of the code). An engine larger than a slot stays where
 * it runs without the overlays (RAMTEXT, or XIP for OVLX code). The slot size is the build's choice (build.py
 * ovl_link.plan: the least RAMTEXT). The firmware finds what build.py did in the overlay table at _ovl_rom (flash, after
 * .text): each engine's place and, for the slots, its code once per slot.
 *
 * The protocol (who may run a slot's code):
 *   - the audio (track_render, either core) calls part k's hooks only while rto_eng[k] names the part's engine; else the
 *     part plays nothing this block (its voices wait, the LFOs run) and it asks for the engine (rto_want[k]);
 *   - the main loop (rto_poll) loads an engine asked for: rto_eng[k] = none first, then the copy from flash, then
 *     rto_eng[k] = the engine. The audio interrupt that comes in during the copy sees none and leaves the slot alone;
 *     CPU1 renders only inside CPU0's audio interrupt (dual.c: fork and join in the ALNK0 interrupt; a late job puts it
 *     back into reset), so it is never in the middle of a block while the main loop runs.
 *   - an engine change (voice.c engine_block, after the fade) needs nothing more: the new engine is asked for at its
 *     first block, the notes of the fade wait in its voices until the code is there (one main-loop pass, ~15 ms).
 * At power-on (rto_boot, before the audio) each part's engine is loaded at once. */
#if FELUCCA_OVERLAY
#define RTO_MAGIC 0x314C564Fu                            /* "OVL1" */
enum { RTO_RAM, RTO_XIP, RTO_SLOT };                     /* an engine's code: resident in RAMTEXT, in XIP, the slots */
typedef struct {
    uint8_t eng, kind;                                   /* the engine's slot (ENGINES[]), RTO_RAM / XIP / SLOT */
    uint16_t len;                                        /* the code's bytes (its hooks' table first) */
    uint32_t at;                                         /* resident: its table's address; SLOT: slot 0's copy (from
                                                          * the table's start; slot k's: + k x the length rounded to 4) */
} rto_ent_t;
typedef struct {
    uint32_t magic, size;                                /* (size: the table and every copy) */
    uint16_t n, nslot;                                   /* entries; slots (NPART) */
    uint32_t slots, slot_len;                            /* slot 0's address, a slot's bytes */
    rto_ent_t e[];
} rto_rom_t;

static const rto_hdr_t *rto_res[NENGINES];               /* a resident engine's hooks (0: in the slots, or none) */
static const uint8_t *rto_src[NENGINES];                 /* an overlay's code for slot 0 (0: none) */
static uint16_t rto_len[NENGINES];                       /* its bytes rounded to 4 (the copies' stride) */
static uint8_t *rto_slot0;
static uint32_t rto_slot_len;
static volatile uint8_t rto_eng[NPART];                  /* the engine slot + 1 whose code slot k holds, whole (0: none,
                                                          * as from power-on: no code there) */
static volatile uint8_t rto_want[NPART];                 /* the engine slot + 1 part k asked for (track_render; 0 none) */
static struct {
    uint32_t loads, us_last, us_max, bytes_max;          /* the copies: how many, their time (us), the longest's bytes */
    uint8_t bad;                                         /* 1 the table was refused (the overlay engines play nothing);
                                                          * 2 a copy's hooks table did not name its engine */
} rto_st;

#if RTO_TARGET
extern const uint8_t _ovl_rom[];                         /* (the linker script build.py writes: the table, its copies) */
extern uint8_t _ovl_slots[], _ovl_slots_end[];
#define RTO_SRC(e, k) (rto_src[e] + (k) * (uint32_t)rto_len[e])
#else
/* the host (tests/overlay_test.c): no sections, one address space. The tables are listed here, each "copied" into a
 * slot of RAM as the target copies an engine's code (its hooks are then read from the slot); rto_host_resident: every
 * table resident instead (no slot) */
static const rto_hdr_t *const RTO_HOST[] = {
#if FELUCCA_ENG_ANALOG
    &rto_hdr_analog,
#endif
#if FELUCCA_ENG_DIGITAL
    &rto_hdr_digital,
#endif
#if FELUCCA_ENG_PHASE
    &rto_hdr_phase,
#endif
#if FELUCCA_ENG_LOFI
    &rto_hdr_lofi,
#endif
#if FELUCCA_ENG_SAMPLE
    &rto_hdr_sample,
#endif
#if FELUCCA_ENG_FORMANT
    &rto_hdr_formant,
#endif
#if FELUCCA_ENG_TRIO
    &rto_hdr_trio,
#endif
#if FELUCCA_ENG_DRAWBAR
    &rto_hdr_drawbar,
#endif
#if !FELUCCA_ANALOG2 && FELUCCA_ENG_SUPER
    &rto_hdr_super,
#endif
#if FELUCCA_ENG_STRINGS
    &rto_hdr_strings,
#endif
};
static uint32_t rto_host_slot[NPART][(sizeof(rto_hdr_t) + 3u) / 4u];
static uint8_t rto_host_resident;
#define RTO_SRC(e, k) rto_src[e]
#endif

/* the audio (track_render, RAM code): part t's sound hooks into h. 1: there; 0: not loaded (the part asks for it) */
static HOT int rto_hooks(track_t *t, const engine_t *e, rto_hdr_t *h)
{
    uint32_t k = (uint32_t)(t - trk), en = t->engine;
    const rto_hdr_t *o;
    if (e->render) {                                     /* (an engine of no overlay: its engine_t's) */
        h->render = e->render, h->block = e->block, h->amp = e->amp, h->post = e->post;
        return 1;
    }
    o = rto_res[en];
    if (!o) {
        if (k >= NPART)
            return 0;
        if (rto_eng[k] != en + 1u) {
            rto_want[k] = (uint8_t)(en + 1u);
            return 0;
        }
        o = (const rto_hdr_t *)(rto_slot0 + k * rto_slot_len);
    }
    h->render = o->render, h->block = o->block, h->amp = o->amp, h->post = o->post;   /* (field by field: no memcpy) */
    return 1;
}

/* the table: where each engine's code is. 0 taken; -1 refused (the overlay engines then play nothing) */
static int rto_init(void)
{
    uint32_t i;
    for (i = 0; i < NENGINES; i++)
        rto_res[i] = 0, rto_src[i] = 0, rto_len[i] = 0;
    for (i = 0; i < NPART; i++)
        rto_eng[i] = rto_want[i] = 0;
#if RTO_TARGET
    {
        const rto_rom_t *r = (const rto_rom_t *)(const void *)_ovl_rom;
        uint32_t span = (uint32_t)(_ovl_slots_end - _ovl_slots);
        if (r->magic != RTO_MAGIC || r->nslot != NPART || r->slots != (uint32_t)(uintptr_t)_ovl_slots ||
            r->slot_len * NPART != span || r->slot_len % 4u || r->size < sizeof *r + r->n * sizeof r->e[0]) {
            rto_st.bad = 1;
            return -1;
        }
        rto_slot0 = _ovl_slots;
        rto_slot_len = r->slot_len;
        for (i = 0; i < r->n; i++) {
            const rto_ent_t *x = &r->e[i];
            uint32_t len = (x->len + 3u) & ~3u;
            if (x->eng >= NENGINES || len < sizeof(rto_hdr_t)) {
                rto_st.bad = 1;
                return -1;
            }
            if (x->kind == RTO_SLOT) {
                if (len > r->slot_len || x->at + NPART * len > r->size) {
                    rto_st.bad = 1;
                    return -1;
                }
                rto_src[x->eng] = _ovl_rom + x->at;
                rto_len[x->eng] = (uint16_t)len;
            } else {
                const rto_hdr_t *o = (const rto_hdr_t *)(uintptr_t)x->at;
                if (o->tag != RTO_TAG(x->eng)) {
                    rto_st.bad = 1;
                    return -1;
                }
                rto_res[x->eng] = o;
            }
        }
    }
#else
    rto_slot0 = (uint8_t *)rto_host_slot;
    rto_slot_len = sizeof rto_host_slot[0];
    for (i = 0; i < sizeof RTO_HOST / sizeof RTO_HOST[0]; i++) {
        uint32_t e = RTO_HOST[i]->tag & 0xFFu;
        if (e >= NENGINES)
            continue;
        if (rto_host_resident)
            rto_res[e] = RTO_HOST[i];
        else
            rto_src[e] = (const uint8_t *)RTO_HOST[i], rto_len[e] = (uint16_t)rto_slot_len;
    }
#endif
    return 0;
}

/* the main loop: engine e's code into part k's slot (the audio leaves the slot alone meanwhile: rto_eng[k] none) */
static void rto_load(uint32_t k, uint32_t e)
{
    const uint32_t *s = (const uint32_t *)(const void *)RTO_SRC(e, k);
    uint32_t *d = (uint32_t *)(void *)(rto_slot0 + k * rto_slot_len), n = rto_len[e] / 4u, i;
#if RTO_TARGET
    uint32_t t0 = fm1_ticks(), us;
#endif
    rto_eng[k] = 0;
    RING_PUBLISH();                                      /* (none, before the first word written) */
    for (i = 0; i < n; i++)
        d[i] = s[i];
    RING_PUBLISH();
#if FELUCCA_DUAL
    fm1_dual_sync();                                     /* (the code written out before CPU1 is told it is there) */
#endif
    if (((const rto_hdr_t *)(const void *)d)->tag != RTO_TAG(e)) {
        rto_st.bad = 2;                                  /* (a table that does not name the engine: never run) */
        return;
    }
    rto_eng[k] = (uint8_t)(e + 1u);
    rto_st.loads++;
#if RTO_TARGET
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    rto_st.us_last = us;
    if (us > rto_st.us_max)
        rto_st.us_max = us, rto_st.bytes_max = n * 4u;
#endif
}

/* the main loop, every pass: the engines the parts asked for */
static void rto_poll(void)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        uint32_t w = rto_want[k];
        if (w && w <= NENGINES && w != rto_eng[k] && rto_src[w - 1u])
            rto_load(k, w - 1u);
    }
}

/* power-on, before the audio (main.c fm1_main, after felucca_init, a bench's setup): the table, each part's engine */
static void rto_boot(void)
{
    uint32_t k;
    if (rto_init())
        return;
    for (k = 0; k < NPART; k++) {                        /* (the engine it plays, or the one a load asked for) */
        uint32_t e = trk[k].eng_req % NENGINES;
        if (rto_src[e])
            rto_load(k, e);
    }
}
#else
#define rto_poll() ((void)0)
#define rto_boot() ((void)0)
#endif
