/* SPDX-License-Identifier: GPL-3.0-only */
/* The sampled kits on the SD card (SD card mode, registry.h KITS_CARD; the user, 2026-11-04: "the drum kits samples as
 * well", docs/SD-CARD-DESIGN.md 9): ACOUSTIC and its treatments DEEP, TIGHT, BRIGHT, DUST (kit UIDs 0..4) play the card's
 * Optimist/Samples/Kits/Acoustic/<nn> - <name>.wav (tools/sd_samples.py KIT_ROLES: the CC0 kit's sounds at 44.1 kHz)
 * through the lanes' card streams (lane_card.c dlc_go), none of their samples in the flash. Included by lane_card.c.
 *   KC_MAP     a GM note -> its sound's file and the note the file plays at its rate (tools/gen_samples.py GM_KIT, the
 *              flash kit's zones: tests/sd_zip_test.py checks the two agree)
 *   kc_want()  the files the lanes play now (each lane on a sampled kit, its instrument's note): the streams' plan
 *              places those (sd_stream_plan.c SST_KIT_WANT), the main loop's
 *   kc_hit()   a hit (drum_on, the ISR's): its file's source (sst_kit_src), pitched as the flash kit's (the kit's shift,
 *              the lane's TUNE), the kit's treatment (drums_mix: drums.kit), its lane's DECAY / CUT / LEVEL. Not on the
 *              card (sd_mode.c says MISSING once) or not placed yet: silent */
#define KC_NFILE 16u
static const char *const KC_FILE[KC_NFILE] = {
    "01 - kick.wav", "02 - rim.wav", "03 - snare.wav", "04 - clap.wav", "05 - closed hat.wav", "06 - open hat.wav",
    "07 - low tom.wav", "08 - high tom.wav", "09 - crash.wav", "10 - ride.wav", "11 - tambourine.wav", "12 - cowbell.wav",
    "13 - conga.wav", "14 - shaker.wav", "15 - claves.wav", "16 - wood block.wav"};
_Static_assert(KC_NFILE == SST_NKIT, "kit_card.c: a source a file");
static const uint8_t KC_MAP[][4] = {                 /* lo, hi, root, file */
    {35, 36, 36, 0}, {37, 37, 37, 1}, {38, 38, 38, 2}, {40, 40, 40, 2}, {39, 39, 39, 3}, {42, 42, 42, 4},
    {44, 44, 44, 4}, {46, 46, 46, 5}, {41, 41, 43, 6}, {43, 43, 43, 6}, {45, 45, 43, 6}, {47, 48, 48, 7},
    {50, 50, 48, 7}, {49, 49, 49, 8}, {52, 52, 52, 8}, {55, 55, 55, 8}, {57, 57, 57, 8}, {51, 51, 51, 9},
    {53, 53, 53, 9}, {59, 59, 59, 9}, {54, 54, 54, 10}, {56, 56, 56, 11}, {62, 64, 63, 12}, {69, 70, 70, 13},
    {82, 82, 82, 13}, {75, 75, 75, 14}, {76, 77, 76, 15}};
#define KC_NMAP (sizeof KC_MAP / sizeof KC_MAP[0])
#define SST_KIT_WANT() kc_want()
#define SST_KIT_FILE(k) KC_FILE[(k) % KC_NFILE]

/* note's entry in KC_MAP, KC_NMAP none (the flash kit has no sound there either: silent) */
static uint32_t kc_of(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < KC_NMAP; i++)
        if (note >= KC_MAP[i][0] && note <= KC_MAP[i][1])
            return i;
    return KC_NMAP;
}

static uint32_t kc_want(void)                        /* the files the lanes on a sampled kit play, a mask */
{
    uint32_t l, m = 0, kit = drum_kit(), e;
    for (l = 0; l < DRUM_LANES; l++) {
        if (dl.src[l] == DL_CARD || dl_usr_of(l) || dl_kit_of(l, kit) >= DRUM_SAMPLED)
            continue;
        e = kc_of(LANE_NOTE_OF(l));
        if (e < KC_NMAP)
            m |= 1u << KC_MAP[e][3];
    }
    return m;
}

/* kit's hit on note (lane, DRUM_LANES none: a GM note no lane is near), as drum_on's flash kit plays it */
static void kc_hit(uint32_t lane, uint32_t note, uint32_t vel, uint32_t kit)
{
    uint32_t e = kc_of(note);
    int32_t shift;
    if (e >= KC_NMAP)
        return;
    shift = kit == 1u ? (note <= 36u ? -5 : -2) : kit == 3u ? 2 : kit == 4u ? -1 : 0;   /* (drum_on's treatments) */
    shift += dl_tune_play(lane);
    dlc_go(lane, sst_kit_src[KC_MAP[e][3]], note, vel, ((int32_t)note - KC_MAP[e][2] + shift) * 16, kit);
}
