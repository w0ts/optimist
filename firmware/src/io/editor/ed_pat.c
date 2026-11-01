/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the per-track patterns and scenes (FELUCCA_PATTERNS, storage/sections/pat.c; web/EDITOR_PROTOCOL.md
 * "Patterns and scenes"), included by editor.c. INFO tag 0x55 says the build has them. The web editor shows the 16 slots of a
 * track in its mixer strip and the scenes on the MASTER strip (docs/PATTERNS-DESIGN.md section 5). Values are 7-bit:
 * a slot 0..15, 127 none, 126 keep (a scene's), 125 stop (a launch).
 *   79 PAT_LIST   op (0 all, 1 the tracks only)  -> op, changed (the tracks whose working copy differs from its
 *                 source: bit k), per track: cur, req, when (0 end, 1 next bar, 2 now); op 0 then: per track x 16
 *                 slots: its LEN as it plays (0: empty); then 16 scenes x 4 patterns (the log's 16; 127 x 4: no scene
 *                 there, a plain section reads 125); then (issue 142, live patterns) per track 3 bytes: the slots with a
 *                 live pattern (edits not saved), bits 0..6, 7..13, 14..15 (an older firmware: none, the editor shows no
 *                 marks and offers no SAVE / RELOAD)
 *   80 PAT_LAUNCH track, slot (127 stop), when -> track, slot, rc (0 ok, 1 arguments)
 *   81 SCENE      op (0 launch: playing on the next bar, stopped at once; 1 store the playing tracks), scene
 *                 -> op, scene, rc (0 ok, 1 arguments / empty, 2 not stored: MEM FULL or no free pattern)
 *   82 PAT_OP     op (0 store the working copy, 1 copy, 2 clear, 3 duplicate, 4 SAVE: the slot's live pattern its
 *                 saved one (the slot playing: the working copy saved there), 5 RELOAD: its live pattern dropped (the slot
 *                 playing: its saved one plays again)), track, a [, track2, b]
 *                 -> op, rc (0 ok, 1 arguments, 2 not done: MEM FULL, the arena full, playing (clear))
 *   83 PAT_READ   track, slot, offset (2 x 7 bit) -> track, slot, offset (2), total (2: the record's bytes, 0 none),
 *                 then pack7 bytes (<= 256) of the record from offset: the pattern as it plays (its live one, else its
 *                 saved one; the arena's, else the log's); track + 4: the saved one
 *   84 PAT_WRITE  track, slot, offset (2), total (2), pack7 bytes (<= 256): in order from offset 0; the chunk that
 *                 completes `total` writes the record as the slot's live pattern (issue 142: SAVE or RELOAD it later;
 *                 the slot a track plays takes it, at once stopped, at its end playing; stopped: the log, playing: the
 *                 arena), track + 4: as its saved one (as before); total 0 clears a slot (saved and live) that no
 *                 clear-while-playing refuses -> track, slot, offset (2), rc (0 ok, 1 not in order / bad
 *                 arguments, 2 not a record of this track, 3 busy (proj_tmp is lent: a restore, another write),
 *                 4 not written: MEM FULL, the arena full, a clear while playing). The chunks wait in proj_tmp
 *                 (lent: a session left for 10 s is given back); the slot's launch waiting is read again
 * 85 stays free for a push of the tracks' patterns. */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
enum { ED_PAT_LIST = 79, ED_PAT_LAUNCH, ED_SCENE, ED_PAT_OP, ED_PAT_READ, ED_PAT_WRITE };
static struct { uint16_t total, got; uint8_t k, s, on; } pw;   /* PAT_WRITE: the record being received */

/* pattern (k, s)'s LEN as it plays, 0 none (its record's byte 1: its live one, else the arena's, else the log's) */
static uint32_t pat_len(uint32_t k, uint32_t s)
{
    uint32_t i = pat_pend(k, s), id = PAT_ID(k, s);
    uint8_t b = 0;
    int w = pat_live_at(k, s);
    if (w == 1)
        return sec_pend.len[pat_lpend(k, s)] > 5u ? sec_pend.data[sec_pend.off[pat_lpend(k, s)] + 5u] : 0u;
    if (w == 2)
        return slg.alen[PAT_LIX(k, s)] > 5u && !st_read(slg_at(PAT_LIX(k, s)) + SEC_HEAD + 5u, &b, 1) ? b : 0u;
    if (sec_pend_has(i))
        return sec_pend.data[sec_pend.off[i] + 1u];
    return slg_has(id) && !st_read(slg_at(id) + SEC_HEAD + 1u, &b, 1) ? b : 0u;
}
static uint32_t ed7(uint32_t v) { return v == PAT_NONE ? 127u : v == PAT_KEEP ? 126u : v == PAT_STOP ? 125u : v & 127u; }

static int ed_pat(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, k, rc = 1;
    if (cmd < ED_PAT_LIST || cmd > ED_PAT_WRITE || na < 1u)
        return 0;
    switch (cmd) {
    case ED_PAT_LIST:
        ed_b2(a[0], proj_tmp_busy() ? 0u : pat_changed());       /* (it captures into proj_tmp: a write holds it) */
        for (k = 0; k < NTRK; k++) {
            ed_b3(ed7(pat_cur[k]), ed7(pat_req[k]), pat_when[k]);
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
            for (k = 0; k < NTRK; k++) {                       /* the live patterns (issue 142): 16 bits a track */
                uint32_t m = 0;
                for (i = 0; i < PAT_N; i++)
                    m |= (uint32_t)(pat_live_at(k, i) != 0) << i;
                ed_b3(m & 127u, (m >> 7) & 127u, m >> 14);
            }
        }
        return 1;
    case ED_PAT_LAUNCH:
        if (na >= 3u && a[0] < NTRK && (a[1] < PAT_N || a[1] == 127u) && a[2] <= PW_NOW) {
            pat_launch(a[0], a[1] < PAT_N ? a[1] : PAT_NONE, a[2]);
            rc = 0;
        }
        ed_b3(a[0], a[1], rc);
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
        ed_b3(a[0], s, rc);
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
            else if (op == 4u)
                rc = pat_save_slot(t, x) ? 2u : 0u;
            else if (op == 5u)
                rc = pat_reload_slot(t, x) ? 2u : 0u;
        }
        ed_b2(op, rc);
        return 1;
    }
    case ED_PAT_READ:
    case ED_PAT_WRITE: {
        uint32_t o, n = 0, tot, w = cmd == ED_PAT_WRITE, sv = (a[0] >> 2) & 1u, t = a[0] & 3u;
        if (na < 4u + 2u * w || a[0] >= 2u * NTRK || t >= NTRK || a[1] >= PAT_N)
            return 0;
        o = ed_r32(a + 2, 2);
        if (!w) {
            tot = sv ? pat_get_saved(t, a[1], sec_rbuf) : pat_get(t, a[1], sec_rbuf);
            n = o < tot ? tot - o : 0u;
        } else {
            tot = ed_r32(a + 4, 2);
            if (o == 0u && tot <= PAT_REC_MAX && ((pw.on && proj_tmp_lent) || !proj_tmp_busy())) {   /* a new record: proj_tmp is lent to it */
                proj_tmp_lent = 1;
                pw.total = (uint16_t)tot, pw.got = 0, pw.k = a[0], pw.s = a[1], pw.on = 1;
            }
            rc = pw.on && proj_tmp_lent ? (pw.k == a[0] && pw.s == a[1] && pw.total == tot && pw.got == o ? 0u : 1u) : proj_tmp_lent ? 3u : 1u;
            if (!rc) {
                proj_tmp_t0 = fm1_ms;
                n = ed_unpack7(a + 6, na - 6u, (uint8_t *)(void *)&proj_tmp + o, tot - o < 256u ? tot - o : 256u);
                rc = n || tot == o ? 0u : 1u;
                pw.got = (uint16_t)(o + n);
            }
            if (!rc && pw.got == tot) {                         /* complete: check it as the stage would, write it */
                memcpy(sec_rbuf, &proj_tmp, tot);
                pw.on = proj_tmp_lent = 0;
                rc = tot && !pat_decode(sec_rbuf, tot, t, &proj_tmp.cur.t[t], 0, 0) ? 2u :
                     (tot && !sv ? pat_live_write(t, a[1], tot) : pat_write(t, a[1], tot)) ? 4u : 0u;   /* (the live one; + 4: saved) */
                if (!rc && pat_req[t] == a[1])
                    pat_staged &= (uint8_t)~(1u << t);          /* (a launch waiting for it: decoded again) */
            }
        }
        ed_b4(a[0], a[1], o, o >> 7);
        if (w) {
            ed_b(rc);
        } else {
            ed_b2(tot, tot >> 7);
            ed_pack7(sec_rbuf + o, n < 256u ? n : 256u);
        }
        return 1;
    }
    default:
        return 0;
    }
}
#define ED_PAT_FN ed_pat,                   /* (editor.c: its table of commands) */
