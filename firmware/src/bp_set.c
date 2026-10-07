/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features' own settings that are no G_ / P_ parameter (G_COUNT and P_COUNT are the project
 * format: they cannot grow). Their pages use the scope SC_BPSET: page_desc gives these descriptors and values,
 * so the pages draw, edit and show in the overview as any other. Included by params.c (backports.h switches).
 *   BPS_RTYPE  REVERB > TYPE: an index into the reverb algorithms built (rev_type.c REV_ALGO; REV_MULTI: two or more
 *              built, else no page); saved in the project (project_t.rsv[0], rev_type.c rev_pack)
 *   BPS_CH0..CH2, BPS_CHD  the MIDI channel of each synth track and of the drum track (FELUCCA_MIDI_CH, seq_midi.c):
 *              0 OFF, 1..16; per project (packed into the project's G_MIDI slot, project.c midi_ch_pack). The drum
 *              track's is the old GLO > DRUMS CH (song.g[G_DRCH], 0 = OFF), reached here through a pointer
 *   BPS_MOUT, BPS_MIN  MIDI OUT KEYS / SEQ (FELUCCA_MIDI_OUT), MIDI IN NOTES / CLOCK (FELUCCA_MIDI_INCLK): settings
 *              of the FM-1, not of a project (the settings word, project.c bp23_word); a NEW project leaves them
 *   BPS_GDENS..BPS_GGO  ACID GEN (FELUCCA_ENG_ACID, eng_acid.c): TB-3PO's density, accent and slide (%), GO;
 *                      not saved (TB-3PO's defaults at power-on and NEW) */
enum { BPS_RTYPE, BPS_GDENS, BPS_GACC, BPS_GSLD, BPS_GGO, BPS_CH0, BPS_CH1, BPS_CH2, BPS_CHD, BPS_MOUT, BPS_MIN,
       BPS_COUNT };
static int16_t bp_set[BPS_COUNT] = {[BPS_GDENS] = 70, [BPS_GACC] = 40, [BPS_GSLD] = 25, [BPS_CH0] = 1, [BPS_CH1] = 2,
                                    [BPS_CH2] = 3};
static const char *const N_BPGO[] = {"--", "GO"};
static const char *const N_MCH[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                    "16"};                                    /* a track's MIDI channel */
static const char *const N_MOUT[] = {"KEYS", "SEQ"};                          /* MIDI OUT: what goes out */
static const char *const N_MIN[] = {"NOTES", "CLOCK"};                        /* MIDI IN: notes and clock, or the clock only */
static const param_desc_t BPS_DESC[BPS_COUNT] = {
    [BPS_RTYPE] = PE("TYPE", N_RTYPE, 0),
    [BPS_GDENS] = PD("DENS", F_INT, 0, 100, 70),
    [BPS_GACC] = PD("ACC", F_INT, 0, 100, 40),
    [BPS_GSLD] = PD("SLD", F_INT, 0, 100, 25),
    [BPS_GGO] = PE("GEN", N_BPGO, 0),
    [BPS_CH0] = PE("TRK 1", N_MCH, 1),
    [BPS_CH1] = PE("TRK 2", N_MCH, 2),
    [BPS_CH2] = PE("TRK 3", N_MCH, 3),
    [BPS_CHD] = PE("DRUMS", N_MCH, 10),
    [BPS_MOUT] = PE("OUT", N_MOUT, 0),
    [BPS_MIN] = PE("IN", N_MIN, 0),
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
