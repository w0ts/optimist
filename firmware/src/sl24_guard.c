/* SPDX-License-Identifier: GPL-3.0-only */
/* FELUCCA_SL24_SAFE (backports24.h): Optimist started on flash SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f) wrote.
 * Included by project.c (FELUCCA_FLASH) after the slots' code, before persist_boot.
 *
 * Without it (measured, tests/sl24_safety_test.c): the first start's sec_migrate erased all four of 2.4's project
 * slots (a FUN5 of 3840 B is no project of ours: "empty", then its sectors erased as old objects); the second autosave
 * wrote over 2.4's autosave; the first drum record saved erased 2.4's FM6 bank (object 8 at 0xE5000); a user kit
 * save erased the tail of a long 2.4 USR3 sample (0xDA000), an FM6 user preset (UP_FM6) the head of USR4 (0xE7000).
 *
 * Now: at each start (sl24_boot_scan, before the log starts) every project slot and the autosave whose current copy
 * is a valid object Optimist cannot read is kept (storage.c st_keep: never erased; the log works around it, the
 * autosave writes its other copy) and shown as not ours: PROJECT and the editor say 2.4 (a FUN5 of 3840 B with its
 * sum: SLOOP 2.4's) or OTHER, never EMPTY. Its stale copy (2.4's previous save of that slot) is not kept: 2.4 itself
 * writes over it next. A user sample longer than ours (2.4's USR3, USR4) keeps its sectors (st_keep_sample). */

static uint8_t pj_alien[5];                            /* the old slots 0..3, the autosave (4): PJ_SL24 / PJ_ALIEN / 0 */

#if FELUCCA_FLASH
static void sl24_boot_scan(void)
{
    uint32_t i;
    st_keep_n = 0;
#ifdef SMP_USR3_END
    st_keep_sample(0, SMP_USER_BASE + 2u * SMP_USER_SIZE, SMP_USR3_END);   /* (2.3 / 2.4: USR3 to 0xDBFFF) */
#endif
    st_keep_sample(1, 0xE7000u, 0xE7000u);             /* 2.4's USR4 (0xE7000..0xFAFFF) */
    for (i = 0; i < 5u; i++) {
        uint32_t obj = i < 4u ? OBJ_PROJECT0 + i : OBJ_AUTOSAVE;
        st_hdr_t h;
        int cur = st_current(obj, &h);                 /* (its payload in st_buf) */
        pj_alien[i] = 0;
        if (cur < 0 || proj_import(&proj_tmp.cur, st_buf, (int)h.len))
            continue;
        pj_alien[i] = (uint8_t)(sl24_is(st_buf, (int)h.len) ? PJ_SL24 : PJ_ALIEN);
        st_keep(st_sector(obj, (uint32_t)cur));
    }
}
#endif

/* PROJECT, the editor: a slot's state; an empty section A..D over a kept project of another firmware: that one's */
static int project_used(uint32_t slot);
static uint32_t project_state(uint32_t s)
{
    s %= FELUCCA_SECTIONS;
    if (project_used(s))
        return PJ_USED;
    return s < 4u && pj_alien[s] ? pj_alien[s] : PJ_EMPTY;
}

/* the settings record's last word: our VIEW (0 / 1); SLOOP 2.3 / 2.4 write their lights word there (panel.c
 * lights_word: LIGHTS, KEYS, NOTES, the REC screen, USB AUDIO, SYNC; 2.4: MIDI OUT SEQ bit 14, IN CLOCK 15, USB SERIAL
 * 16, the visualiser 17-20). A word past 1 is theirs: VIEW reads ALL (as before) and the word is written back as read
 * while VIEW stays ALL (we read it as ALL again; 2.3 / 2.4 find their settings) */
static uint32_t persist_view_in(uint32_t w, uint32_t *kept)
{
    *kept = w > 1u ? w : 0u;
    return w > 1u ? 1u : w;
}
static uint32_t persist_view_out(uint32_t view, uint32_t kept) { return view == 1u && kept ? kept : view; }

/* LOAD of an empty slot: another firmware's project kept there is said so, nothing is loaded */
#if FELUCCA_SL24_IMPORT && FELUCCA_FLASH
/* FELUCCA_SL24_IMPORT: the 2.4 project kept in old slot `slot` -> the working project (sl24_import.c), its step
 * extras with it (FELUCCA_SL24_XSTEP; else dropped, said so). Nothing is written: the original stays where 2.4 left
 * it; SAVE puts the import in a section. -> 1 imported */
static int sl24_import_slot(uint32_t slot)
{
#if SEC_LOGGED
    dlrec_t *d = &sec_tmp_dl;                          /* (2.4 has no drum record: the kit as it is) */
#else
    static dlrec_t dd;
    dlrec_t *d = &dd;
#endif
    stepx_t *x = 0;
    st_hdr_t h;
    uint32_t lost;
    int cur = st_current(OBJ_PROJECT0 + slot, &h);     /* (its payload in st_buf) */
#if FELUCCA_SL24_XSTEP
    sx_store_t *m = sx_for(&proj_tmp.cur, 1);
    x = m ? m->x : 0;
#endif
    if (cur < 0 || !sl24_is(st_buf, (int)h.len))
        return 0;
    lost = !x && sl24_has_extras(st_buf);
    if (!proj_from_sl24(&proj_tmp.cur, st_buf, (int)h.len, x))
        return 0;
#if FELUCCA_SL24_XSTEP
    if (m)
        m->psum = proj_tmp.cur.sum;
#endif
    memset(d, 0, sizeof *d);
    project_apply(&proj_tmp.cur, d);
    ui_message(lost ? "2.4 IMPORTED, NO LOCKS" : "2.4 IMPORTED: SAVE IT");
    return 1;
}
static int8_t sl24_armed = -1;                         /* the slot a first LOAD armed */
static uint32_t sl24_armed_ms;
#endif
/* LOAD of an empty slot: another firmware's project kept there is said so; a SLOOP 2.4 one: LOAD again within 4 s
 * imports it (offered, never automatic) */
static void sl24_load(uint32_t slot)
{
    uint32_t st = project_state(slot);
#if FELUCCA_SL24_IMPORT && FELUCCA_FLASH
    if (st == PJ_SL24) {
        if (sl24_armed == (int8_t)slot && fm1_ms - sl24_armed_ms < 4000u) {
            sl24_armed = -1;
            if (sl24_import_slot(slot))
                return;
        } else {
            sl24_armed = (int8_t)slot;
            sl24_armed_ms = fm1_ms;
            ui_message("SLOOP 2.4: LOAD = IMPORT");
            return;
        }
    }
#endif
    ui_message(st == PJ_SL24 ? "SLOOP 2.4 PROJECT" : st == PJ_ALIEN ? "NOT OURS: KEPT" : "EMPTY SLOT");
}
