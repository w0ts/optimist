/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' own settings that are no G_ / P_ parameter (G_COUNT and P_COUNT are the project
 * format: they cannot grow). Their pages use the scope SC_BPSET: page_desc gives these descriptors and values,
 * so the pages draw, edit and show in the overview as any other. Included by params.c (backports.h switches).
 *   BPS_RTYPE  REVERB > TYPE: an index into the reverb algorithms built (rev_type.c REV_ALGO; REV_MULTI: two or more
 *              built, else no page); saved in the project (project_t.rsv[0], rev_type.c rev_pack)
 *   BPS_CH0..CH2, BPS_CHD  the MIDI channel of each synth track and of the drum track (FELUCCA_MIDI_CH, seq_midi.c):
 *              0 OFF, 1..16; per project (packed into the project's G_MIDI slot, project.c midi_ch_pack). The drum
 *              track's is the old GLO > DRUMS CH (song.g[G_DRCH], 0 = OFF), reached here through a pointer
 *   BPS_MOUT, BPS_MIN  MIDI OUT OFF / KEYS / SEQ / BOTH (FELUCCA_MIDI_OUT; stored KEYS 0, BOTH 1, SEQ 2, OFF 3), MIDI IN NOTES / CLOCK (FELUCCA_MIDI_INCLK): settings
 *              of the FM-1, not of a project (the settings word, project.c bp23_word); a NEW project leaves them
 *   BPS_SELCH  SEL CH: the channel that plays the selected track (FELUCCA_MIDI_CH, seq_midi.c midi_track_idx): 0 OFF
 *              (a channel no track has plays nothing), 1..16, 17 ANY (every channel no track has, as before 2026-11-15);
 *              default 4 (issue 265: the other channels free for other gear). Of the FM-1, not of a project (the settings
 *              record's ext byte, persist_ext.c), so a NEW project leaves it
 *   BPS_GDENS..BPS_GGO  ACID GEN (FELUCCA_ENG_ACID, eng_acid.c): TB-3PO's density, accent and slide (%), GO;
 *                      not saved (TB-3PO's defaults at power-on and NEW) */
enum { BPS_RTYPE, BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO, BPS_CH0, BPS_CH1, BPS_CH2, BPS_CHD, BPS_MOUT, BPS_MIN,
       BPS_SELCH, BPS_COUNT };
#define SELCH_ANY 17                                   /* SEL CH: OFF 0, 1..16, ANY 17 (seq_midi.c midi_track_idx) */
#define SELCH_DEF 4
static int16_t bp_set[BPS_COUNT] = {[BPS_GDENS] = 70, [BPS_GACC] = 40, [BPS_GSLD] = 25, [BPS_CH0] = 1, [BPS_CH1] = 2,
                                    [BPS_CH2] = 3, [BPS_SELCH] = SELCH_DEF};
static const char *const N_BPGO[] = {"--", "GO"};
static const char *const N_MCH[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                    "16", "ANY"};                             /* a track's MIDI channel (OFF, 1..16); SEL CH's also ANY (17) */
/* N_MOUT, MOUT_*: params.c (before the order tables) */
static const char *const N_MIN[] = {"NOTES", "CLOCK"};                        /* MIDI IN: notes and clock, or the clock only */
static const param_desc_t BPS_DESC[BPS_COUNT] = {
    [BPS_RTYPE] = PE("TYPE", N_RTYPE, 0),
    [BPS_GDENS] = PD("DENS", F_INT, 0, 100, 70),
    [BPS_GACC] = PD("ACC", F_INT, 0, 100, 40),
    [BPS_GSLD] = PD("SLD", F_INT, 0, 100, 25),
    [BPS_GGO] = PE("GEN", N_BPGO, 0),
    [BPS_CH0] = PN("TRK 1", F_ENUM, 0, 16, 1, N_MCH),
    [BPS_CH1] = PN("TRK 2", F_ENUM, 0, 16, 2, N_MCH),
    [BPS_CH2] = PN("TRK 3", F_ENUM, 0, 16, 3, N_MCH),
    [BPS_CHD] = PN(FELUCCA_DRUMS ? "DRUMS" : "TRK 4", F_ENUM, 0, 16, FELUCCA_DRUMS ? 10 : 4, N_MCH),
    [BPS_MOUT] = PE("OUT", N_MOUT, 0),
    [BPS_MIN] = PE("IN", N_MIN, 0),
    [BPS_SELCH] = PE("SEL CH", N_MCH, SELCH_DEF),
};

static const param_desc_t *bps_desc(uint32_t id, int16_t **vp)
{
    if (id >= BPS_COUNT) {
        *vp = 0;
        return 0;
    }
    *vp = id == BPS_CHD ? &song.g[G_DRCH] : &bp_set[id];   /* (the drum channel is the project's G_DRCH) */
    return &BPS_DESC[id];
}

/* SEL CH in the settings record (storage/project.c: rsv_pj[0], VU FLOOR's retired byte): 0x80 | the value (0 OFF, 1..16,
 * 17 ANY); a byte without bit 7 (0, or VU FLOOR's 0..7 from a build of 2026-10-28..31) reads the default, 4 */
static uint8_t selch_to_rec(void) { return (uint8_t)(0x80u | (uint32_t)clamp(bp_set[BPS_SELCH], 0, SELCH_ANY)); }
static void selch_from_rec(uint32_t b)
{
    bp_set[BPS_SELCH] = (int16_t)((b & 0x80u) && (b & 0x7Fu) <= SELCH_ANY ? b & 0x7Fu : SELCH_DEF);
}

/* a new project: the defaults */
static void bps_defaults(void)
{
    uint32_t i;
    for (i = 0; i < BPS_MOUT; i++)                     /* (MIDI OUT / IN: the FM-1's, a new project keeps them) */
        if (i != BPS_CHD)
            bp_set[i] = BPS_DESC[i].def;
#if FELUCCA_MIDI_CH
    song.g[G_DRCH] = GP[G_DRCH].def;                   /* the drum channel is a project's too */
#endif
}

#if REV_MULTI
/* TYPE's value (rev_type.c): an index into REV_ALGO, the first one when out of range */
static uint32_t rev_sel(void)
{
    uint32_t i = (uint32_t)bp_set[BPS_RTYPE];
    return i < REV_NLIST ? i : 0u;
}
static void rev_sel_set(uint32_t i) { bp_set[BPS_RTYPE] = (int16_t)i; }
#endif
