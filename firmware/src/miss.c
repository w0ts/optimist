/* SPDX-License-Identifier: GPL-3.0-only */
/* What a load uses and this build leaves out (FELUCCA_MISSING_WARN, registry.h). A project, a song section, a
 * user preset or a user kit that names an engine, a drum kit or a sample set this build does not have still
 * loads: it plays a stand-in and keeps its data (project.c orphans, registry.h fallbacks). Here the user is told:
 *   - each load bumps miss_gen (core.h; proj_apply, also from the audio ISR when a song changes section: one
 *     byte, nothing else there); the main loop (ui_draw: miss_tick) then looks at the working project and says
 *     what is new in the top bar, "MISSING: PHYS T2, KIT 909 +1", once per item until power-off; it waits
 *     for the bar to be free and costs a scan of the four tracks, never the audio;
 *   - SAVE > TOOLS > MISS: the count; turning it shows the items one by one ("MISSING 2/3: KIT 909").
 * The scan looks at what plays now (the parts, the drum lanes, the master), so an item the user replaced is
 * no longer listed. What counts: engines (by UID, the orphans), the drum kit and other kits on lanes (a lane
 * kit the same as the track's is not audible), sample sets left out (SAMPLE SET, GRAIN SRC) and USR slots
 * that are empty (SAMPLE, GRAIN, SLICE, drum lanes), FM6 modes left out, and the data of a switch left out
 * that would sound: DELAY / REVERB / CHORUS sends (parts with FX on, the drums' REV, the lanes' own), DIST,
 * SLICER, the master's FILT / DUST / DUCK, the drum lanes' edits, samples, kits and sends, per-step chance.
 * Not seen: motion (its own record in flash), PUNCH (nothing stored), QNT SEQ (clamped at load), the spring
 * reverb's TYPE (a byte a build without it drops). Names: the tables already built in (ENG_UID_NAME, ...).
 * Built on the host without the UI part (MISS_SCAN_ONLY): tests/missing_test.c. */
static uint32_t proj_orph_uid(uint32_t k);            /* project.c: the engine UID an orphan part keeps, 0xFF */

enum { MS_ENG = 1, MS_KIT, MS_SET, MS_USR, MS_FX, MS_FM6 };   /* an item: type << 12 | track << 8 | id */
#define MS_ITEM(ty, k, id) ((uint32_t)(ty) << 12 | (uint32_t)(k) << 8 | (uint32_t)(id))
#define MISS_MAX 12u
enum { MF_DELAY, MF_REVERB, MF_CHORUS, MF_DIST, MF_SLICER, MF_FILT, MF_DUST, MF_DUCK, MF_SNDED, MF_LSMP, MF_LKIT,
       MF_LFX, MF_CHANCE };
/* their names, one string, MF_* order: only those of the switches this build leaves out (the others: "") */
#define MF_S(f, s) FIF(FNOT(f))(s) "\0"
static const char MF_NAMES[] = MF_S(FELUCCA_FX_DELAY, "DELAY") MF_S(FELUCCA_FX_REVERB, "REVERB")
    MF_S(FELUCCA_FX_CHORUS, "CHORUS") MF_S(FELUCCA_FX_DIST, "DIST") MF_S(FELUCCA_FX_SLICER, "SLICER")
    MF_S(FELUCCA_FX_DJF, "FILT") MF_S(FELUCCA_FX_DUST, "DUST") MF_S(FELUCCA_FX_DUCK, "DUCK")
    MF_S(FELUCCA_DRUM_EDIT, "SOUND EDIT") MF_S(FELUCCA_DRUM_USR, "LANE SMP") MF_S(FELUCCA_DRUM_KITS, "LANE KIT")
    MF_S(FELUCCA_DRUM_SENDS, "LANE FX") MF_S(FELUCCA_CHANCE, "CHANCE");
#undef MF_S
static uint16_t miss_m[MISS_MAX];                     /* the last scan's items */
static uint32_t miss_cnt;

/* add an item once (an item twice, from another track: the first) */
static void miss_add(uint32_t it)
{
    uint32_t i;
    for (i = 0; i < miss_cnt; i++)
        if (!((miss_m[i] ^ it) & 0xF0FFu))
            return;
    if (miss_cnt < MISS_MAX)
        miss_m[miss_cnt++] = (uint16_t)it;
}
#define MFX(f, k) miss_add(MS_ITEM(MS_FX, k, f))

/* sample set / USR slot s (SMP_ALL_NAMES numbering) played by track k: left out or empty */
static void miss_set(uint32_t k, uint32_t s)
{
    if (s < SMP_NSETS ? !SMP_SETS[s].nz : s < SMP_NALL && !usr_nz[s - SMP_NSETS])
        miss_add(s < SMP_NSETS ? MS_ITEM(MS_SET, k, s) : MS_ITEM(MS_USR, k, s - SMP_NSETS));
}

/* the synth part k (engine, its set or slot, its FX, its steps' chance) */
static void miss_part(uint32_t k)
{
    const track_t *t = &trk[k];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t u = proj_orph_uid(k), i;
    if (u != 0xFFu)
        miss_add(MS_ITEM(MS_ENG, k, u));
    else if (ENG_IS(e, SAMPLE) || ENG_IS(e, GRAIN))
        miss_set(k, (uint32_t)t->p[P_E0]);           /* (in range: proj_apply, the knob) */
#if FELUCCA_SLICE
    else if (ENG_IS(e, SLICE) && t->p[P_E0] > 0)       /* (SRC 1..3: USR1..USR3) */
        miss_set(k, SMP_NSETS + (uint32_t)(t->p[P_E0] - 1));
#endif
#if FELUCCA_ENG_FM6
    else if (ENG_IS(e, FM6) && fm6_mode(t->p[P_E4]) != (uint32_t)clamp(t->p[P_E4], 0, 2))
        miss_add(MS_ITEM(MS_FM6, k, clamp(t->p[P_E4], 0, 2)));
#endif
    if (fx_on(t)) {
        if (!FELUCCA_FX_DELAY && t->p[P_DLY])
            MFX(MF_DELAY, k);
        if (!FELUCCA_FX_REVERB && t->p[P_REV])
            MFX(MF_REVERB, k);
        if (!FELUCCA_FX_CHORUS && t->p[P_CHOR])
            MFX(MF_CHORUS, k);
        if (!FELUCCA_FX_DIST && t->p[P_DIST])
            MFX(MF_DIST, k);
    }
    for (i = 0; !FELUCCA_CHANCE && i < NSTEP; i++)     /* (chance.c: step_t.flags bits 2..6) */
        if (t->step[i].flags & 0x7Cu)
            MFX(MF_CHANCE, k);
}

/* the drum track: its kit, the lanes' sources, edits and sends, its reverb send */
static void miss_drums(void)
{
    uint32_t l, i, kit = (uint32_t)TDRUM->p[P_E0], on = (uint32_t)fx_on(TDRUM);
    const uint32_t D = TRK_DRUM;
    if (kit < DRUM_KITS && !drum_kit_built(kit))
        miss_add(MS_ITEM(MS_KIT, D, kit));
    if (!FELUCCA_FX_REVERB && on && song.g[G_DRREV])
        MFX(MF_REVERB, D);
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t s = dl.src[l], w = dsend[l];
        if (s >= DL_USR && s < DL_USR + SMP_USER_SLOTS) {
            if (!FELUCCA_DRUM_USR)
                MFX(MF_LSMP, D);
            else if (!usr_nz[s - DL_USR])
                miss_add(MS_ITEM(MS_USR, D, s - DL_USR));
        } else if (s >= DL_KIT0 && s - DL_KIT0 < DRUM_KITS && s - DL_KIT0 != kit) {   /* (the track's kit: as is) */
            if (!FELUCCA_DRUM_KITS)
                MFX(MF_LKIT, D);
            else if (!drum_kit_built(s - DL_KIT0))
                miss_add(MS_ITEM(MS_KIT, D, s - DL_KIT0));
        }
        for (i = 0; !FELUCCA_DRUM_EDIT && i < DE_N; i++)
            if (dl.ofs[l][i])
                MFX(MF_SNDED, D);
        if (!FELUCCA_DRUM_SENDS && w)
            MFX(MF_LFX, D);
        if (FELUCCA_DRUM_SENDS && on) {
            if (!FELUCCA_FX_REVERB && (w & DSEND_OWN) && (w & 31u))
                MFX(MF_REVERB, D);
            if (!FELUCCA_FX_DELAY && dsend_dly(w))
                MFX(MF_DELAY, D);
            if (!FELUCCA_FX_CHORUS && dsend_cho(w))
                MFX(MF_CHORUS, D);
        }
    }
}

/* what the working project uses and this build lacks -> miss_m[], the count */
static uint32_t miss_scan(void)
{
    uint32_t k;
    miss_cnt = 0;
    for (k = 0; k < NPART; k++)
        miss_part(k);
    miss_drums();
    for (k = 0; !FELUCCA_FX_SLICER && k < NTRK; k++)
        if (trk[k].p[P_SLCR])
            MFX(MF_SLICER, k);
    if (!FELUCCA_FX_DJF && song.g[G_FILT])
        MFX(MF_FILT, TRK_DRUM);
    if (!FELUCCA_FX_DUST && song.g[G_DUST])
        MFX(MF_DUST, TRK_DRUM);
    if (!FELUCCA_FX_DUCK && song.g[G_DUCK])
        MFX(MF_DUCK, TRK_DRUM);
    return miss_cnt;
}
#undef MFX

static __attribute__((noinline)) char *miss_cat(char *p, const char *s)   /* s at p, the end */
{
    while ((*p = *s++))
        p++;
    return p;
}

/* an item as text at p (14 characters at most), the end: "PHYS T2", "KIT 909", "PIANO T1", "USR2 EMPTY",
 * "DELAY", "OPL T1" */
static char *miss_name(char *p, uint32_t it)
{
    static const char *const *const NAMES[] = {ENG_UID_NAME, DRUM_KIT_NAMES, SMP_ALL_NAMES, 0, 0,
#if FELUCCA_ENG_FM6
                                               N_FM6ENG,
#endif
    };
    uint32_t ty = it >> 12, id = it & 255u;
    const char *s = MF_NAMES;
    if (ty == MS_KIT)
        p = miss_cat(p, "KIT ");
    if (ty == MS_USR) {
        p = miss_cat(p, "USR1 EMPTY");
        p[-7] = (char)('1' + id);
        return p;
    }
    if (ty == MS_FX)
        while (id--)
            s += str_len(s) + 1u;
    else
        s = NAMES[ty - 1u][id];
    p = miss_cat(p, s);
    if (ty == MS_ENG || ty == MS_SET || ty == MS_FM6)
        p = miss_cat(p, " T1"), p[-1] = (char)('1' + ((it >> 8) & 3u));
    return p;
}

/* "MISSING: " and the items of miss_m[0..n) that end within lim characters (the first always), " +n" for the
 * rest: at most 29 with lim 25 (the top bar); 14 on a layer screen (its title holds ~18 after the layer's name) */
static void miss_line(char *b, uint32_t n, uint32_t lim)
{
    char t[16];
    uint32_t i, more = 0;
    char *p = miss_cat(b, "MISSING: ");
    for (i = 0; i < n; i++) {
        uint32_t w = (uint32_t)(miss_name(t, miss_m[i]) - t);
        if (more || (i && (uint32_t)(p - b) + 2u + w > lim)) {
            more++;
            continue;
        }
        p = miss_cat(miss_cat(p, i ? ", " : ""), t);
    }
    if (more)
        fmt_int(miss_cat(p, " +"), (int32_t)more);
}

/* once per item and power-on: keep the items not said yet (and mark them said), the count */
static uint32_t miss_said[8];
static uint32_t miss_fresh(uint32_t n)
{
    uint32_t i, r = 0;
    for (i = 0; i < n; i++) {
        uint32_t b = (((uint32_t)miss_m[i] >> 12) * 40u + (miss_m[i] & 63u)) & 255u;   /* (ids < 40) */
        if ((miss_said[b >> 5] >> (b & 31u)) & 1u)
            continue;
        miss_said[b >> 5] |= 1u << (b & 31u);
        miss_m[r++] = miss_m[i];
    }
    return r;
}

#ifndef MISS_SCAN_ONLY
static uint8_t miss_done, miss_ix;                    /* the miss_gen looked at; TOOLS > MISS: the item shown */
#define MISS_SHOW 150u                                /* frames the message stays (~2.5 s; ui_say's 40 are short) */

/* a frame of the main loop (ui_draw): after a load, say what is new; on TOOLS, the count for MISS */
static void miss_tick(void)
{
    uint32_t n;
    if (cur_page()->id[2] == G_MISS && cur_page()->scope == SC_GLOBAL)
        miss_n = (int16_t)miss_scan();
    if (miss_done == miss_gen || ui.msg_t)            /* (a message up: this one waits for it) */
        return;
    miss_done = miss_gen;
    n = miss_fresh(miss_scan());
    if (n) {
        miss_line(ui.msg, n, ui.layer != LY_PLAY || ui.hold_kind ? 14u : 25u);
        ui.msg_t = MISS_SHOW;
    }
}

/* TOOLS > MISS turned: the next / previous item, "MISSING 2/3: KIT 909" */
static void miss_knob(int32_t steps)
{
    uint32_t n = miss_scan();
    char *p;
    miss_n = (int16_t)n;
    if (!n)
        return;                                       /* (MISS 0 says it) */
    miss_ix = (uint8_t)(steps < 0 ? (miss_ix && miss_ix <= n ? miss_ix : n) - 1u : miss_ix + 1u < n ? miss_ix + 1u : 0u);
                                                      /* (one item a turn, either way, round; no divide by n) */
    p = miss_cat(ui.msg, "MISSING ");
    fmt_int(p, (int32_t)miss_ix + 1);
    p = miss_cat(p + str_len(p), "/");
    fmt_int(p, (int32_t)n);
    miss_name(miss_cat(p + str_len(p), ": "), miss_m[miss_ix]);
    ui.msg_t = MISS_SHOW;
}
#endif
