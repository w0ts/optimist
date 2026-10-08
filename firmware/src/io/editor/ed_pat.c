/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the per-track patterns and scenes (FELUCCA_PATTERNS, storage/sections/pat.c; web/EDITOR_PROTOCOL.md
 * "Patterns"), included by editor.c. INFO tag 0x55 says the build has them. The web editor shows the 16 slots of a
 * track in its mixer strip and the scenes on the MASTER strip (docs/PATTERNS-DESIGN.md 7.1). Values are 7-bit:
 * a slot 0..15, 127 none, 126 keep (a scene's), 125 stop (a launch).
 *   79 PAT_LIST   op (0 all, 1 the tracks only)  -> op, changed (the tracks whose working copy differs from its
 *                 source: bit k), per track: cur, req, when (0 end, 1 next bar, 2 now); op 0 then: per track x 16
 *                 slots: its LEN (0: empty); then 16 scenes x 4 patterns (the log's 16; 127 x 4: no scene there,
 *                 a plain section reads 125)
 *   80 PAT_LAUNCH track, slot (127 stop), when -> track, slot, rc (0 ok, 1 arguments)
 *   81 SCENE      op (0 launch: playing on the next bar, stopped at once; 1 store the playing tracks), scene
 *                 -> op, scene, rc (0 ok, 1 arguments / empty, 2 not stored: MEM FULL or no free pattern)
 *   82 PAT_OP     op (0 store the working copy, 1 copy, 2 clear, 3 duplicate), track, a [, track2, b]
 *                 -> op, rc (0 ok, 1 arguments, 2 not done: MEM FULL, the arena full, playing (clear))
 * 83..85 stay free for the patterns' read / write / push. */
enum { ED_PAT_LIST = 79, ED_PAT_LAUNCH, ED_SCENE, ED_PAT_OP };

/* pattern (k, s)'s LEN, 0 none (its record's byte 1: the arena's, else the log's) */
static uint32_t pat_len(uint32_t k, uint32_t s)
{
    uint32_t i = pat_pend(k, s), id = PAT_ID(k, s);
    uint8_t b = 0;
    if (sec_pend_has(i))
        return sec_pend.data[sec_pend.off[i] + 1u];
    return slg_has(id) && !st_read(slg_at(id) + SEC_HEAD + 1u, &b, 1) ? b : 0u;
}
static uint32_t ed7(uint32_t v) { return v == PAT_NONE ? 127u : v == PAT_KEEP ? 126u : v == PAT_STOP ? 125u : v & 127u; }

static int ed_pat(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, k, rc = 1;
    if (cmd < ED_PAT_LIST || cmd > ED_PAT_OP || na < 1u)
        return 0;
    switch (cmd) {
    case ED_PAT_LIST:
        ed_b(a[0]);
        ed_b(pat_changed());
        for (k = 0; k < NTRK; k++) {
            ed_b(ed7(pat_cur[k]));
            ed_b(ed7(pat_req[k]));
            ed_b(pat_when[k]);
        }
        if (a[0] == 0u) {
            uint8_t r[NTRK];
            for (k = 0; k < NTRK; k++)
                for (i = 0; i < PAT_N; i++)
                    ed_b(pat_len(k, i));
            for (i = 0; i < SEC_ID_SONG; i++) {
                int sc = pat_scene_refs(i, r);
                for (k = 0; k < NTRK; k++)
                    ed_b(sc ? ed7(r[k]) : slg_has(i) || (i < SEC_IDS && sec_pend_has(i)) ? 125u : 127u);
            }
        }
        return 1;
    case ED_PAT_LAUNCH:
        if (na >= 3u && a[0] < NTRK && (a[1] < PAT_N || a[1] == 127u) && a[2] <= PW_NOW) {
            pat_launch(a[0], a[1] < PAT_N ? a[1] : PAT_NONE, a[2]);
            rc = 0;
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(rc);
        return 1;
    case ED_SCENE: {
        uint32_t s = na > 1u ? a[1] : 127u, g = sec_gen;
        if (a[0] == 0u && s < SEC_IDS && project_used(s))
            rc = song.playing ? !section_cue(s) : (section_load(s), 0u);
        else if (a[0] == 1u && s < SEC_IDS) {
            if (song.playing)
                section_store(s);
            else
                project_save(s);
            rc = sec_gen == g ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(s);
        ed_b(rc);
        return 1;
    }
    case ED_PAT_OP: {
        uint32_t op = a[0], t = na > 2u ? a[1] : NTRK, x = na > 2u ? a[2] : PAT_N, t2 = na > 4u ? a[3] : NTRK, y = na > 4u ? a[4] : PAT_N;
        if (t < NTRK && (op == 3u || x < PAT_N)) {
            if (op == 0u)
                rc = pat_store_slot(t, x) ? 2u : 0u;
            else if (op == 1u && t2 < NTRK && y < PAT_N)
                rc = pat_copy(t, x, t2, y) ? 2u : 0u;
            else if (op == 2u)
                rc = pat_has(t, x) && pat_write(t, x, 0) ? 2u : 0u;
            else if (op == 3u)
                rc = (x = pat_free(t)) >= PAT_N || pat_store_slot(t, x) ? 2u : 0u;
        }
        ed_b(op);
        ed_b(rc);
        return 1;
    }
    default:
        return 0;
    }
}
