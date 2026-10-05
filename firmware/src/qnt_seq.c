/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0.1: QNT SEQ, #37,
 * hugelton/Felucca 20c275e, seq.c seq_step / kb_map) */
/* SCL > QNT SEQ (FELUCCA_QNT_SEQ, backports.h): the keys play as SNAP (every key, rounded down onto the scale),
 * and the sequenced notes snap onto the scale as they play: a pattern follows a change of ROOT or SCALE. The
 * steps keep the notes as written (turn QNT back and they play as before). Never on a part that plays raw
 * notes (the GM KIT sample set, SLICE: kb_raw). Two notes of a chord that snap together play once.
 * Included by seq.c (scale_map, seq_step, seq_ratchets).
 *
 * The value is P_QUANT 4 (after OFF SNAP WHITE ALL). A build without the switch clamps a project's 4 to ALL
 * (3) when it loads it: such a build plays those keys one degree per key (tools/backports.json: limits). */

/* a part whose sequence snaps */
static int qseq_on(const track_t *t)
{
    return t->p[P_QUANT] == Q_SEQ && !kb_raw(t);
}

/* note n rounded down onto the part's scale (as SNAP rounds a key; n already holds TRANSPOSE) */
static uint32_t qseq_snap_note(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return (uint32_t)clamp(n, 0, 127);
}

/* the step s as it plays: its notes snapped (into the copy q); returns the notes (bits) that repeat an earlier
 * one after snapping: not triggered again */
static uint32_t qseq_step(const track_t *t, const step_t *s, step_t *q)
{
    uint32_t i, j, dup = 0;
    *q = *s;
    for (i = 0; i < q->n && i < 4u; i++) {
        q->note[i] = (uint8_t)qseq_snap_note(t, q->note[i]);
        for (j = 0; j < i; j++)
            if (q->note[j] == q->note[i] && !((dup >> j) & 1u))
                dup |= 1u << i;
    }
    return dup;
}
