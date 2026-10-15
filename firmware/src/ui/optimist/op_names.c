/* SPDX-License-Identifier: GPL-3.0-only */
/* The names table (the Optimist UI's style guide, sections 1-4; the 2026-10-14 coherence audit, batch A): one spelling
 * for each thing the screens name, in three forms, from the names the shared tables give (params.c PAGES' titles, the
 * descriptors' labels, the engines', the drums', this UI's own cells). Looked up when drawn: what the cells, the rows
 * and the help line key on (op_help.c: the label, the row's name) stays the shared name, so SLOOP's UI, the web editor
 * and tools/param_help.json are untouched.
 *   title   a row's name in the list and the page's title in the header, sentence case, 9 characters at most and short
 *           enough to fit whole with its n/N next to the widest badge of its screen ("2 DIGITAL", "4 ACCENT"):
 *           "ENV to", "Scale", "Inserts" (tests/ui_optimist_names.h walks every one)
 *   card    a card's label, in capitals, 6 characters at most (an action's object 8): "LEVEL", "DETUNE", "DEST A"
 *   cell    a list cell, 4 characters: the engines' and the verbs' fixed forms ("ANLG", "CLR", "INS"), never a cut
 * A name with no entry is shown as given: a card label as it is (in capitals already), a title in sentence case
 * (op_case: "VOICE 2" -> "Voice 2"; ENV, LFO, ARP kept). Names the user or a pack owns (presets, kits, lanes, projects)
 * never pass through here: they are drawn as stored.
 * The tables: "key\0form\0" pairs, an empty key ends them. A '#' in a key matches one digit, which the form's '#'
 * repeats ("OP# LEVEL" -> "OP# out"); "CTX:" before a card key limits it to the engine CTX ("DIGITAL:R#": DIGITAL's
 * ratios, not FM6's rates). Flash only: const strings, searched in the main loop when a band is drawn. */
static const char NM_TITLE[] =
    "ENV DEST\0ENV to\0"         "ENV2 DEST\0ENV2 to\0"       "LFO DEST\0LFO to\0"         "LFO 2\0LFO2\0"
    "LFO2 DEST\0LFO2 to\0"       "SCL\0Scale\0"               "SCL 2\0Scale 2\0"           "FLT 2\0Filter 2\0"
    "INSERTS\0Insert\0"         "DR INSERT\0DR ins\0"        "DR SENDS\0DR sends\0"       "BUS SENDS\0Bus sends\0"      "CMP\0Comp\0"
    "INS COMP\0Ins comp\0"       "RET DELAY\0Delay\0"         "RET REV\0Reverb\0"
    "OP# LEVEL\0OP# out\0"       "OP# RATES\0OP# rate\0"      "OP# LEVELS\0OP# level\0"    "OP# KEY SCA\0OP# scale\0"
    "OP# CURVES\0OP# curve\0"    "PIT RATES\0Pit rate\0"      "PIT LVLS\0Pit level\0"
    "\0";
/* a title's short form, where a header is short of room (op_draw.c head_short, after "Voice 2" -> "Voice2"): bar.beat
 * always stays (the user, 2026-10-14), so the title gives way, by a form chosen here, never a cut */
static const char NM_SHORT[] =
    "Pattern\0Pat\0"           "Scale2\0Scl2\0"            "Voice2\0Vox2\0"            "Kit sends\0Kit snd\0"
    "Bus sends\0Bus snd\0"       "LFO2 to\0L2 to\0"           "ENV2 to\0E2 to\0"           "Slicer\0Slcr\0"
    "Screen & audio\0Scr & aud\0" "Connections\0Connect\0"  "Insert\0Ins\0"
    "\0";
static const char NM_CARD[] =
    "VOLUME\0LEVEL\0"            "LVL\0LEVEL\0"               "DST\0DIST\0"                "DST A\0DEST A\0"
    "DST B\0DEST B\0"            "DEST\0DEST A\0"             "DEST2\0DEST B\0"            "AMT\0AMT A\0"
    "AMT2\0AMT B\0"              "CMP\0COMP\0"                "FILT\0FILTER\0"             "FLT\0FILTER\0"
    "PIT\0PITCH\0"               "SHP\0SHAPE\0"               "OSC2\0OSC 2\0"              "TRN\0TRANSP\0"
    "TRNSP\0TRANSP\0"            "DTN\0DETUNE\0"              "DTUNE\0DETUNE\0"            "RES\0RESO\0"
    "SWG\0SWING\0"               "SCL\0SCALE\0"               "QNT\0QUANT\0"               "ACC\0ACCENT\0"
    "SLD\0SLIDE\0"               "DRV\0DRIVE\0"               "LENGTH\0LEN\0"              "RATCHET\0RATCH\0"
    "GLD\0GLIDE\0"               "MOTN\0MOTION\0"             "ENRGY\0ENERGY\0"            "COLR\0COLOR\0"
    "CHO RET\0CHO RT\0"          "DLY RET\0DLY RT\0"          "REV RET\0REV RT\0"          "S# CHO\0CHO RT\0"
    "S# DLY\0DLY RT\0"           "S# REV\0REV RT\0"           "MISS\0MISSING\0"           "ACID LINE\0LINE\0"          "BPM\0TEMPO\0"              "TRK FLT\0FILTER\0"          "CLK IN\0SYNC\0"          "DIGITAL:R#\0RAT#\0"
    "\0";
static const char NM_CELL[] =
    "ANALOG\0ANLG\0"             "DIGITAL\0DGTL\0"            "PHASE\0PHSE\0"              "SAMPLE\0SMPL\0"
    "VOICE\0VOX\0"               "WHEEL\0WHL\0"               "GRAIN\0GRN\0"           "NOISE\0NOIS\0"
    "CLEAR\0CLR\0"               "ERASE\0ERS\0"               "RESET\0RST\0"               "INSERT\0INS\0"
    "DELETE\0DEL\0"              "SAVE AS\0SAVE\0"            "IMPORT\0IMPT\0"             "STORE\0STOR\0"
    "FILTER\0FILT\0"             "STORED\0USED\0"             "EMPTY\0-\0"                 "DST\0DIST\0"                "CMP\0COMP\0"
    "MODAL\0MODL\0"              "PIANO\0PNO\0"               "SMOOT\0SMTH\0"              "RATIO\0RTIO\0"
    "PEDAL\0PEDL\0"              "MARK \0MARK\0"
    "\0";
/* a card label's narrow form, for the mixer's 1x4 cards only (57 px): where the label and its unit cannot share the label
 * row (6 letters and a unit, 5 and "dB": 8 px a letter, the unit right-aligned), the label gives way, never the unit
 * (style guide 5); the forms are the names table's own (the cell's LVL, the send's REV DLY CHO on MASTER's returns) */
static const char NM_NARROW[] =
    "LEVEL\0LVL\0"               "REV RT\0REV\0"              "DLY RT\0DLY\0"              "CHO RT\0CHO\0"
    "\0";
/* key k against s: 1 when it matches whole ('#' any digit, its digit into *dg) */
static int nm_match(const char *k, const char *s, char *dg)
{
    for (; *k && *s; k++, s++) {
        if (*k == '#' && *s >= '0' && *s <= '9')
            *dg = *s;
        else if (*k != *s)
            return 0;
    }
    return !*k && !*s;
}
/* s's form in table tab (its '#' the digit matched) into b (n bytes); 0: no entry. ctx: a "CTX:" key's prefix, or 0 */
static const char *nm_find(const char *tab, const char *s, const char *ctx, char *b, uint32_t n)
{
    const char *k, *v;
    char dg = '0';
    uint32_t i, c = ctx ? str_len(ctx) : 0u;
    for (k = tab; *k; k = v + str_len(v) + 1) {
        v = k + str_len(k) + 1;
        if (c ? !(str_len(k) > c && !memcmp(k, ctx, c) && k[c] == ':' && nm_match(k + c + 1u, s, &dg))
              : !nm_match(k, s, &dg))
            continue;
        for (i = 0; v[i] && i + 1u < n; i++)
            b[i] = v[i] == '#' ? dg : v[i];
        b[i] = 0;
        return b;
    }
    return 0;
}
/* a card's label (capitals, 6 at most; an action's object 8): b holds n (12 or more); eng: the engine whose page it is
 * on (a "CTX:" key's), 0 none */
static const char *nm_card(const char *s, const char *eng, char *b, uint32_t n)
{
    if (!s)
        return "";
    if ((eng && nm_find(NM_CARD, s, eng, b, n)) || nm_find(NM_CARD, s, 0, b, n))
        return b;
    return s;
}
/* a card label's narrow form (NM_NARROW), or s as it is: b holds n */
static const char *nm_narrow(const char *s, char *b, uint32_t n) { return nm_find(NM_NARROW, s, 0, b, n) ? b : s; }
/* a list cell's fixed form (4 characters), or s as it is */
static const char *nm_cell(const char *s)
{
    static char b[8];
    return nm_find(NM_CELL, s, 0, b, sizeof b) ? b : s;
}
/* a row's name and a page's title, as the list and the header show it (sentence case), into b (n bytes) */
static const char *nm_title(const char *s, char *b, uint32_t n)
{
    if (!nm_find(NM_TITLE, s, 0, b, n))
        op_case(b, s, n);
    return b;
}
