/* SPDX-License-Identifier: GPL-3.0-only
 * The notes played on the keys, all of them, and the chord they make (the user, 2026-11-02: "we display the note played
 * on keyboard, but just one, could we list them all and say which chord it is"). Read by op_draw.c nn_tick / nn_post and
 * op_help.c hl_post, which draw the line at the screen's bottom left.
 *   nn_take  on a key going down: the notes of every note key down now (CHORD mode: each key's chord), lowest first,
 *            each once; a key let go changes nothing (a chord let go key by key stays named whole)
 *   nn_line  "C4 E4 G4 C": the notes, then the chord's name when the pitch classes make one of CH_T (rooted on the
 *            lowest note, else on another one of them: "C/E"); the octaves dropped when the line would be too wide */
#define NN_MAX 8u                                       /* notes listed at most */
static uint8_t nn_set[NN_MAX], nn_cnt;                  /* the notes shown, lowest first */

static uint32_t nn_hash(void)                           /* what is shown, for the help line's redraw (op_help.c hl_post) */
{
    uint32_t h = nn_cnt, i;
    for (i = 0; i < nn_cnt; i++)
        h = h * 131u + nn_set[i];
    return h;
}
static void nn_take(void)
{
    uint32_t k, i, j, n;
    nn_cnt = 0;
    for (k = 0; k < 27u; k++) {
        if (!((kb_prev >> k) & 1u) || kb_kind[k] != KS_NOTE)
            continue;
        for (i = 0; i < kb_n[k] && i < 4u; i++) {
            n = kb_nt[k][i];
            for (j = 0; j < nn_cnt && nn_set[j] < n; j++)
                ;
            if ((j < nn_cnt && nn_set[j] == n) || (j == nn_cnt && nn_cnt == NN_MAX))
                continue;                               /* (twice, or the list full of lower ones) */
            if (nn_cnt == NN_MAX)
                nn_cnt--;                               /* (the highest makes room) */
            memmove(nn_set + j + 1u, nn_set + j, nn_cnt - j);
            nn_set[j] = (uint8_t)n;
            nn_cnt++;
        }
    }
    if (!nn_cnt)                                        /* (a key tapped and let go before this frame: the note it played) */
        nn_set[0] = kb_shown, nn_cnt = 1;
}

/* chord shapes by their intervals from the root (bit i: i semitones), the name after the root's */
static const struct {
    uint16_t m;
    char s[6];
} CH_T[] = {
    {0x091, ""},     {0x089, "m"},    {0x049, "dim"},  {0x111, "aug"},  {0x085, "sus2"},  {0x0A1, "sus4"},  {0x081, "5"},
    {0x491, "7"},    {0x891, "maj7"}, {0x489, "m7"},   {0x449, "m7b5"}, {0x249, "dim7"},  {0x291, "6"},     {0x289, "m6"},
    {0x889, "mMaj7"}, {0x4A1, "7sus4"}, {0x095, "add9"}, {0x495, "9"}, {0x895, "maj9"},  {0x48D, "m9"},
};
static uint32_t ch_shape(uint32_t pcs, uint32_t r)      /* CH_T's entry for the pitch classes rooted on r, or its length */
{
    uint32_t m = ((pcs >> r) | (pcs << (12u - r))) & 0xFFFu, i;
    for (i = 0; i < sizeof CH_T / sizeof CH_T[0] && CH_T[i].m != m; i++)
        ;
    return i;
}
static void ch_name(char *b, uint32_t size)             /* "Cm7", "C/E"; "" when the notes make no chord of CH_T */
{
    uint32_t pcs = 0, i, r, s, bass = nn_set[0] % 12u;
    b[0] = 0;
    for (i = 0; i < nn_cnt; i++)
        pcs |= 1u << (nn_set[i] % 12u);
    if (__builtin_popcount(pcs) < 2)
        return;
    for (i = 0; i < 12u; i++) {                         /* the lowest note as the root first, then the others upward */
        r = (bass + i) % 12u;
        if (!((pcs >> r) & 1u) || (s = ch_shape(pcs, r)) >= sizeof CH_T / sizeof CH_T[0])
            continue;
        str_cpy(b, N_NOTE[r], size);
        str_app(b, CH_T[s].s, size);
        if (r != bass) {
            str_app(b, "/", size);
            str_app(b, N_NOTE[bass], size);
        }
        return;
    }
}
static void nn_line(char *b, uint32_t size, int32_t room)   /* the line, cut to room px */
{
    char nm[12], w[6];
    uint32_t i, oct;
    ch_name(nm, sizeof nm);
    for (oct = 1; ; oct = 0) {
        b[0] = 0;
        for (i = 0; i < nn_cnt; i++) {
            if (i)
                str_app(b, " ", size);
            if (oct)
                note_name(w, nn_set[i]);
            else
                str_cpy(w, N_NOTE[nn_set[i] % 12u], sizeof w);
            str_app(b, w, size);
        }
        if (nm[0]) {
            str_app(b, "  ", size);
            str_app(b, nm, size);
        }
        if (!oct || text_ws(b) <= room)
            break;
    }
    for (i = str_len(b); i > 1u && text_ws(b) > room; i--)
        b[i - 1u] = 0;
}
