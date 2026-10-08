/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRUM MIXER (docs/UI-OPTIMIST-DESIGN.md sections 4.1, 11.5): the drum track's 16 sounds as mixer strips, four at
 * a time, KNOB k the block's lane k. It looks and works as the mixer (op_screens.c MIX): no cards, SELECT walks the
 * strips' controls, the control lit on the four strips, PRESETS the hot strip's value one unit, YES on the SOUND row
 * opens the hot lane's SOUND rows. Its rows are the mixer's, "like other tracks" (the user, 2026-10-08), with "-"
 * where a sound has no such value:
 *   LEVEL   the sound's (SOUND 2: DSD LEVEL, -24..+6 dB), the fader
 *   CUT     its filter cut (SOUND 2), in PAN's place: drawn at the strip's foot, from the centre (a sound has no pan)
 *   REV DLY CHO  its sends (SOUND 3: drum_sends.c)
 *   DRIVE   its drive (SOUND 2; "-" on a sampled sound, which has none)
 *   FILTER, FX ON  "-" (the track's, not a sound's: the mixer's drum strip)
 * Entry: the pick, HOME held + a drum key on the mixer (the key plays it): the drum mixer opens on that sound's block
 * with the sound selected (lane_sel); the same pick moves inside it; HOME + OCT- / OCT+ scroll the block; HOME tapped
 * goes back to the mixer; ALGORITHM to a synth track too (the drum mixer is the drum track's).
 * The view (SYSTEM > SCREEN > DR MIX): 4 strips (the default), or 8 narrower ones on one screen; the knobs still edit
 * the block of four, lit among them. Kept in the settings word (storage/settings_word.c, bits 21..22). A 16-strip
 * view was tried on the emulator and left out: at 14 px a strip's control rows are 8 px bars that show no value
 * (docs/UI-OPTIMIST-DESIGN.md section 11.5). */
enum { DK_SND, DK_NONE, DK_ENTER };
static const struct { const char *name; uint8_t kind, id; } DMX[] = {   /* id: the lane's value (dsnd_desc_lane) */
    {"LEVEL", DK_SND, DE_LEVEL},                        /* (the first row: the strips' fader) */
    {"CUT", DK_SND, DE_CUT},                            /* (PAN's place in the walk and at the foot) */
#if FELUCCA_FX_REVERB
    {"REV", DK_SND, 16u},
#endif
#if FELUCCA_FX_DELAY
    {"DLY", DK_SND, 17u},
#endif
#if FELUCCA_FX_CHORUS
    {"CHO", DK_SND, 18u},
#endif
#if FELUCCA_FX_DIST
    {"DRIVE", DK_SND, DE_DRIVE},
#endif
#if FELUCCA_TRK_FILT
    {"FILTER", DK_NONE, 0},
#endif
    {"FX ON", DK_NONE, 0},
    {"SOUND", DK_ENTER, SCR_SOUND},
};
#define NDMX (sizeof DMX / sizeof DMX[0])
#define DM_FOOT_ID DE_CUT                               /* the value drawn at the strips' foot (the mixer's PAN) */
enum { DMV_4, DMV_8, DMV_N };
static const uint8_t DMV_STRIPS[DMV_N] = {4, 8};
static uint8_t dm_view;                                 /* DMV_*: the strips on one screen (the settings word) */
static struct {
    uint8_t blk;                                        /* the knobs' block: lanes 4 blk .. 4 blk + 3 */
    uint8_t lit[DRUM_LANES];                            /* frames each lane's hit stays lit (the kit pads' 6) */
    uint8_t lit_drawn[DRUM_LANES];                      /* the flashes as drawn (0xFF: again) */
    uint8_t step_drawn[DRUM_LANES];                     /* the lanes' playheads as drawn */
} dm;

static int mix_screen(uint32_t scr) { return scr == SCR_HOME || scr == SCR_DMIX; }   /* the strips, no cards */
static uint32_t dm_strips(void) { return DMV_STRIPS[dm_view % DMV_N]; }
static uint32_t dm_lane(uint32_t k) { return (dm.blk * 4u + (k & 3u)) % DRUM_LANES; }   /* KNOB k's lane */
static uint32_t dm_first(void)                          /* the first lane shown: the page holding the block */
{
    uint32_t n = dm_strips();
    return dm.blk * 4u & ~(n - 1u);                     /* (n: 4 or 8) */
}
static uint32_t dm_rows(void) { return NDMX; }
static void dm_name(uint32_t r, char *b) { str_cpy(b, DMX[r % NDMX].name, 12); }

/* lane l's value of row r: its descriptor and value (0: the sound has none, "-") */
static const param_desc_t *dm_desc(uint32_t r, uint32_t l, int16_t **vp)
{
    const param_desc_t *d;
    *vp = 0;
    if (DMX[r % NDMX].kind != DK_SND)
        return 0;
    d = dsnd_desc_lane(l, DMX[r % NDMX].id, vp);
    return d && !PARAM_HIDDEN(d) ? d : 0;
}
static void dm_cell_lane(uint32_t r, uint32_t l, cell_t *c)
{
    int16_t *vp;
    cell_clear(c);
    switch (DMX[r % NDMX].kind) {
    case DK_SND:
    case DK_NONE:
        cell_param(c, dm_desc(r, l, &vp), vp);
        if (!c->d)
            str_cpy(c->val, "-", sizeof c->val);
        break;
    default:
        c->kind = CK_RO;                                /* the SOUND row: the sounds' names, YES opens one */
        str_cpy(c->val, LANE_NAME[l % DRUM_LANES], sizeof c->val);
        break;
    }
    c->label = LANE_SHORT[l % DRUM_LANES];
    c->col = lane_col(l);
}
static void dm_cell(uint32_t r, uint32_t k, cell_t *c) { dm_cell_lane(r, dm_lane(k), c); }
/* lane l's value id set to v (as op_cells.c op_dsnd_set, for any lane: the offsets and the sends) */
static void dm_set(uint32_t l, uint32_t id, int32_t v)
{
    if (id >= 16u)
        dsend_set(l, id - 16u, v);
    else if (id < DE_N)
        dl.ofs[l % DRUM_LANES][id] = (int8_t)v;
}
static void dm_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    int16_t *vp;
    uint32_t l = dm_lane(k);
    const param_desc_t *d = dm_desc(r, l, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    dm_set(l, DMX[r % NDMX].id, s == OP_RESET ? d->def : param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
}
static int dm_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    (void)ok;
    if (DMX[r % NDMX].kind != DK_ENTER)
        return 0;
    lane_select(dm_lane(k));                            /* the hot strip's sound: its SOUND rows */
    op_enter(SCR_SOUND);
    return 1;
}

/* the pick on the mixer or here: lane l selected, its block shown (op_input.c: HOME held + a drum key) */
static void dm_open(uint32_t l)
{
    dm.blk = (uint8_t)((l % DRUM_LANES) / 4u);
    if (ui.scr != SCR_DMIX) {
        uint8_t r = ui.row[SCR_DMIX];
        op_enter(SCR_DMIX);
        ui.row[SCR_DMIX] = r;                           /* (the control it was left on) */
    }
    ui.hot = (uint8_t)(l % 4u);                         /* (PRESETS on the picked sound) */
    ui.force = 1;
}
static void dm_scroll(int32_t d)                        /* HOME + OCT- / OCT+: the block of four before / after */
{
    dm.blk = (uint8_t)clamp((int32_t)dm.blk + d, 0, DRUM_LANES / 4 - 1);
    ui.force = 1;
}
/* once a frame: the hits since the last one (drums.c drums.hits, as SLOOP's kit pads take them); a synth track
 * selected (ALGORITHM) leaves the drum mixer for the mixer */
static void dm_tick(void)
{
    uint32_t i, hits;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++)
        dm.lit[i] = (uint8_t)((hits >> i) & 1u ? 6u : dm.lit[i] ? dm.lit[i] - 1u : 0u);
    if (ui.scr == SCR_DMIX && !is_drum(TSEL))
        op_enter(SCR_HOME);
}

/* the SCOPE screen's rows (op_scope.c, after the renderer: its trace draws with it): for the screen table */
static uint32_t scope_rows(void);
static void scope_name(uint32_t r, char *b);
static void scope_cell(uint32_t r, uint32_t k, cell_t *c);
static void scope_turn(uint32_t r, uint32_t k, int32_t s, int fine);
static int scope_yes(uint32_t r, uint32_t k, uint32_t ok);
