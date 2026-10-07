/* SPDX-License-Identifier: GPL-3.0-only */
/* Whole-state snapshots (FELUCCA_SNAPSHOTS, docs/SNAPSHOTS.md): the working project, every section and the song in
 * one stream, kept in a slot of the snapshot area (snap_store.c), loaded back. Included by project.c at its end (the
 * firmware; tests/snapshots_test.c includes it after its own doubles).
 *
 *   stream  info (sn_info_t, 48 B: "SNAP", version 1, the name, the saving build, BPM, the sections stored, the
 *           song's length), then records: u8 kind, u8 id, u16 length, the bytes. WORK: the working project as a
 *           sec_codec.c record (motion, drum record and all); SEC id: a section's record (the log's bytes as they
 *           are; FELUCCA_SECTIONS 4: a project slot encoded); SONG: count, loop, 2 spare, (section, bars) per part.
 *
 * Referenced, not copied: the user samples, presets, kits and the FM6 bank (a load says MISSING for an empty USR
 * slot: miss.c). SAVE and LOAD only stopped and quiet (an erase stops the audio ~50 ms), as autosave. LOAD saves the
 * state to BEFORE LOAD first (loading BEFORE LOAD itself swaps: its old version is read before it is dropped).
 * Uses project.c (proj_capture, proj_put, project_apply, autosave_buf, proj_tmp), sections.c or the four project
 * slots, sec_codec.c. One record buffer: proj_tmp.rec (each record encoded or read just before it is written). */
#if !SEC_LOGGED
#include "sec_codec.c"                                 /* (FELUCCA_SECTIONS 4: the slots go in as section records) */
#endif
#include "snap_store.c"

#define SN_IMAGIC 0x50414E53u                          /* "SNAP" */
#define SN_VERSION 1u
#define SN_NAME 12u
#define SN_CHUNK 256u                                  /* a piece copied or sent (st_buf as scratch) */
enum { SNR_WORK = 1, SNR_SEC, SNR_SONG, SNR_TAIL };
#define SN_TAIL_WORK 0x80u                             /* a TAIL record's id: the work's (else the section's, SECTIONS 4) */
typedef struct {
    uint32_t magic;
    uint16_t ver, ilen;
    char name[SN_NAME];
    uint32_t pmagic, cfg;
    uint8_t nsec, nrec;
    uint16_t bpm;
    uint32_t secmask;
    uint8_t parts, flags;                              /* flags bit 0: a record carries motion */
    uint16_t rsv;
    uint32_t count, rsv2;
} sn_info_t;
_Static_assert(sizeof(sn_info_t) == 48u, "the stream's info: 48 bytes");
/* results (the editor's rc too: web/EDITOR_PROTOCOL.md) */
enum { SNE_OK, SNE_ARGS, SNE_FULL, SNE_PLAYING, SNE_NOFLASH, SNE_FLASH, SNE_BAD, SNE_BUSY, SNE_EMPTY, SNE_RING,
       SNE_FORMAT, SNE_USR3, SNE_CRC, SNE_ORDER };
/* what a load left out (sn_note): sections past this build's (FELUCCA_SECTIONS 4: E..P), a song naming them, a
 * section the store did not take */
enum { SNN_SECS = 1, SNN_SONG = 2, SNN_FAIL = 4 };
#ifdef FELUCCA_CFG_HASH
#define SN_CFG ((uint32_t)FELUCCA_CFG_HASH)
#else
#define SN_CFG 0u
#endif
#define SN_REC ((uint8_t *)(void *)&proj_tmp)          /* (SEC_REC_MAX: project.c proj_tmp.rec) */
_Static_assert(sizeof proj_tmp >= SEC_REC_MAX, "a record is built in proj_tmp");
#define SN_SECS 16u                                    /* sections in the format (A..P) */

static uint8_t sn_note;
static uint32_t sn_count;                              /* saves made (the info's counter) */

/* ---- an import from the editor (ed_snap.c, SN_WRITE): a stream received straight into free sectors, committed
 * only when it is all there, its CRC holds and it parses (until then the slot keeps what it had). A device save,
 * load or clear ends it; so does 10 s without a chunk */
static sn_wr_t sn_imp;
static uint32_t sn_imp_crc, sn_imp_ms;
static void ed_snap_cancel(void)
{
    sn_wr_release(&sn_imp);
}
static void sn_imp_tick(void)
{
    if (sn_imp.open && fm1_ms - sn_imp_ms > 10000u)
        ed_snap_cancel();
}

/* ---- what is stored now: the work (in autosave_buf), each section, the song */
static void sn_work_capture(void)
{
    fm1_irq_off();
    proj_capture(&autosave_buf, &autosave_dl);
    fm1_irq_on();
}
static uint32_t sn_work_rec(void) { return sec_encode(&autosave_buf, &autosave_dl, SN_REC); }
/* section id's record -> SN_REC, its length (0 empty) */
static uint32_t sn_sec_rec(uint32_t id)
{
#if SEC_LOGGED
    int n;
    if (id < SEC_IDS && sec_pend_has(id)) {           /* (one the log did not take: MEM FULL, still in RAM) */
        memcpy(SN_REC, sec_pend.data + sec_pend.off[id], sec_pend.len[id]);
        return sec_pend.len[id];
    }
    n = flash_ok ? slg_get(id, SN_REC) : 0;           /* (every id of the log: a 16-section snapshot from an */
    return n > 0 ? (uint32_t)n : 0u;                   /* 8-section build keeps I..P) */
#else
    return id < 4u && proj_ok(&proj_slot[id]) ? sec_encode(&proj_slot[id], &proj_dl[id], SN_REC) : 0u;
#endif
}
static uint32_t sn_sec_len(uint32_t id)                /* (without reading it, where the log knows) */
{
#if SEC_LOGGED
    if (id < SEC_IDS && sec_pend_has(id))
        return sec_pend.len[id];
    return slg_has(id) ? slg.alen[id] : 0u;
#else
    return sn_sec_rec(id);
#endif
}
static uint32_t sn_song(uint8_t *b)                    /* the song chain -> b, its length */
{
    uint32_t i;
    b[0] = arrangement.count, b[1] = arrangement.loop, b[2] = b[3] = 0;
    for (i = 0; i < arrangement.count && i < ARR_STEPS; i++)
        b[4u + 2u * i] = arrangement.entry[i].scene, b[5u + 2u * i] = arrangement.entry[i].bars;
    return 4u + 2u * i;
}

/* TAIL: what a section record leaves out (sec_codec.c keeps a section as it plays): the steps past each track's
 * LEN that are not empty, the FM6 functions of the parts that are not FM6. With it a project comes back byte for
 * byte. Per track: a count, then (step, its 10 bytes) each; then a mask of the parts, their 16 functions each.
 * -> its length in b (SN_TAIL_MAX room), 0 nothing left out */
#define SN_TAIL_MAX (NTRK * (1u + (NSTEP - 1u) * (1u + (uint32_t)sizeof(step_t))) + 1u + NPART * 16u)
_Static_assert(SN_TAIL_MAX <= ST_PAYLOAD_MAX && SN_TAIL_MAX <= SEC_REC_MAX, "a TAIL is built in st_buf");
static uint32_t sn_tail(const project_t *p, uint8_t *b)
{
    uint8_t *o = b;
    uint32_t k, s, any = 0, m = 0;
    for (k = 0; k < NTRK; k++) {
        uint8_t *cnt = o++;
        *cnt = 0;
        for (s = sec_len(&p->t[k]); s < NSTEP; s++)
            if (!sec_step_empty(&p->t[k].step[s], k)) {
                *o++ = (uint8_t)s;
                memcpy(o, &p->t[k].step[s], sizeof(step_t));
                o += sizeof(step_t);
                (*cnt)++, any = 1;
            }
    }
    for (k = 0; k < NPART; k++)
        if (!((p->fm6_has >> k) & 1u) && p->fm6_fn[k][0] >= 0) {
            for (s = 0; s < 16u; s++)                  /* (the defaults, as a load sets them: nothing to keep) */
                if (p->fm6_fn[k][s] != (s < FM6_NFN ? (int8_t)FM6_FNDEF[s] : -1))
                    break;
            if (s < 16u)
                m |= 1u << k;
        }
    *o++ = (uint8_t)m;
    for (k = 0; k < NPART; k++)
        if ((m >> k) & 1u)
            memcpy(o, p->fm6_fn[k], 16), o += 16;
    return any || m ? (uint32_t)(o - b) : 0u;
}
/* a TAIL (n bytes) onto p, decoded from its record: its sum (and its motion's) again; 0 not well formed */
static int sn_tail_apply(project_t *p, const uint8_t *b, uint32_t n)
{
    const uint8_t *a = b, *e = b + n;
    uint32_t k, c, m, old = p->sum;
    for (k = 0; k < NTRK; k++) {
        if (a >= e)
            return 0;
        for (c = *a++; c; c--, a += 1u + sizeof(step_t))
            if (a + 1u + sizeof(step_t) > e || a[0] >= NSTEP)
                return 0;
            else
                memcpy(&p->t[k].step[a[0]], a + 1, sizeof(step_t));
    }
    if (a >= e)
        return 0;
    for (m = *a++, k = 0; k < NPART; k++)
        if ((m >> k) & 1u) {
            if (a + 16 > e)
                return 0;
            memcpy(p->fm6_fn[k], a, 16), a += 16;
        }
    p->sum = proj_sum(p);
#if FELUCCA_MOTION
    {
        motion_store_t *ms = motion_for(p, 0);
        if (ms && ms->psum == old)
            ms->psum = p->sum;                         /* (its motion stays its own) */
    }
#endif
    (void)old;
    return a == e;
}

/* the name made from the work: "120 ABCD", "96 A-H", "120 WORK" */
static void sn_auto_name(char *b, uint32_t mask)
{
    uint32_t i, n = 0, first = 0, last = 0, k;
    fmt_int(b, song.g[G_BPM]);
    k = str_len(b);
    b[k++] = ' ';
    for (i = 0; i < SN_SECS; i++)
        if ((mask >> i) & 1u) {
            if (!n++)
                first = i;
            last = i;
        }
    if (!n)
        str_cpy(b + k, "WORK", SN_NAME - k);
    else if (n <= 4u) {
        for (i = 0; i < SN_SECS; i++)
            if ((mask >> i) & 1u)
                b[k++] = (char)('A' + i);
        b[k] = 0;
    } else {
        b[k] = (char)('A' + first), b[k + 1] = '-', b[k + 2] = (char)('A' + last), b[k + 3] = 0;
    }
}

static int sn_put_rec(sn_wr_t *w, uint32_t kind, uint32_t id, const uint8_t *p, uint32_t n)
{
    uint8_t h[4];
    h[0] = (uint8_t)kind, h[1] = (uint8_t)id, h[2] = (uint8_t)n, h[3] = (uint8_t)(n >> 8);
    return sn_wr_put(w, h, 4) || (n && sn_wr_put(w, p, n));
}
/* the state now -> slot k (the work captured into autosave_buf first); name: 0 = made from the work. keep: the
 * slot's old version stays in flash (a load from it reads it after) */
static int sn_write(uint32_t k, const char *name, int keep)
{
    static sn_info_t in;
    sn_wr_t w;
    uint16_t len[SN_SECS], tl[SN_SECS];
    uint32_t i, n, total, nwork, nsong, nrec = 2, tw;
    int rc;
    memset(&in, 0, sizeof in);
    memset(tl, 0, sizeof tl);
    sn_work_capture();
    nwork = sn_work_rec();
    in.flags = (SN_REC[0] & SEC_MOT) ? 1u : 0u;        /* (the work carries motion) */
    tw = sn_tail(&autosave_buf, st_buf);
    nsong = 4u + 2u * (arrangement.count < ARR_STEPS ? arrangement.count : ARR_STEPS);   /* (sn_song: written last) */
    total = sizeof in + 4u + nwork + (tw ? 4u + tw : 0u) + 4u + nsong;
    for (i = 0; i < SN_SECS; i++)
        if ((len[i] = (uint16_t)sn_sec_len(i)) != 0) {
            total += 4u + len[i];
            in.secmask |= 1u << i;
            nrec++;
#if !SEC_LOGGED
            if ((tl[i] = (uint16_t)sn_tail(&proj_slot[i], st_buf)) != 0)
                total += 4u + tl[i];
#endif
        }
    in.magic = SN_IMAGIC, in.ver = SN_VERSION, in.ilen = sizeof in;
    if (name && name[0])
        str_cpy(in.name, name, SN_NAME);
    else
        sn_auto_name(in.name, in.secmask);
    in.pmagic = PROJ_MAGIC, in.cfg = SN_CFG, in.nsec = FELUCCA_SECTIONS, in.nrec = (uint8_t)nrec;
    in.bpm = (uint16_t)song.g[G_BPM];
    in.parts = arrangement.count;
    in.count = ++sn_count;
    if ((rc = sn_wr_begin(&w, k, total)) != 0)
        return rc == -2 ? SNE_FULL : rc == -3 ? SNE_ARGS : SNE_FLASH;
    rc = sn_wr_put(&w, (const uint8_t *)&in, sizeof in);
    if (!rc) {
        nwork = sn_work_rec();                         /* (encoded again: the buffer held the others' sizes) */
        rc = sn_put_rec(&w, SNR_WORK, 0, SN_REC, nwork);
    }
    if (!rc && tw)
        rc = sn_tail(&autosave_buf, st_buf) != tw || sn_put_rec(&w, SNR_TAIL, SN_TAIL_WORK, st_buf, tw);
    for (i = 0; !rc && i < SN_SECS; i++)
        if ((in.secmask >> i) & 1u) {
            n = sn_sec_rec(i);
            rc = n != len[i] || sn_put_rec(&w, SNR_SEC, i, SN_REC, n);
#if !SEC_LOGGED
            if (!rc && tl[i])
                rc = sn_tail(&proj_slot[i], st_buf) != tl[i] || sn_put_rec(&w, SNR_TAIL, i, st_buf, tl[i]);
#endif
        }
    if (!rc)
        rc = sn_song(SN_REC) != nsong || sn_put_rec(&w, SNR_SONG, 0, SN_REC, nsong);
    if (rc) {
        sn_wr_release(&w);
        return SNE_FLASH;
    }
    if (keep) {                                        /* (commit without dropping the old version yet) */
        uint32_t s0 = sn.slot[k].state ? sn.slot[k].sec[0] : SN_SECTORS;
        if (s0 < SN_SECTORS)
            sn.own[s0] = SN_HELD;                      /* (its part 0 kept out of the housekeeping) */
        rc = sn_wr_commit(&w);
        if (s0 < SN_SECTORS && sn.own[s0] == SN_HELD)
            sn.own[s0] = SN_FREE;
        return rc ? SNE_FLASH : SNE_OK;
    }
    return sn_wr_commit(&w) ? SNE_FLASH : SNE_OK;
}

/* the checks every save and load makes */
static int sn_usr3_in_way(void)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(2);
    return h->magic == SMP_USER_MAGIC && h->version == 1 && h->data_len > SMP_USER_CAP(2) - SMP_USER_DATA &&
           h->data_len <= SMP_BANKS - (SMP_USER_BASE + 2u * SMP_USER_SIZE) - SMP_USER_DATA;   /* (a build without snapshots wrote it) */
}
static int sn_can(void)
{
    if (!flash_ok)
        return SNE_NOFLASH;
    if (song.playing || transport_req)
        return SNE_PLAYING;
    if (proj_tmp_busy())
        return SNE_BUSY;
    if (!audio_quiet())
        return SNE_RING;
    if (sn_usr3_in_way())
        return SNE_USR3;
    return SNE_OK;
}

/* save the state now into slot k (name 0: made from the work) */
static int sn_save(uint32_t k, const char *name)
{
    int rc = sn_can();
    if (rc)
        return rc;
    if (k >= SN_NSLOT)
        return SNE_ARGS;
    ed_snap_cancel();
    sections_write();                                  /* (what waits in RAM goes into flash first) */
    return sn_write(k, name, 0);
}

/* ---- loading */
/* record walk over slot e's stream: the next record at *at -> kind, id, length, where its bytes are; 0 at the end,
 * -1 not well formed */
static int sn_next(const sn_slot_t *e, uint32_t *at, uint32_t *kind, uint32_t *id, uint32_t *n, uint32_t *bytes)
{
    uint8_t h[4];
    if (*at == e->total)
        return 0;
    if (*at + 4u > e->total || sn_read_e(e, *at, h, 4))
        return -1;
    *kind = h[0], *id = h[1], *n = (uint32_t)h[2] | (uint32_t)h[3] << 8;
    *bytes = *at + 4u;
    if (*bytes + *n > e->total)
        return -1;
    *at = *bytes + *n;
    return 1;
}
/* the stream's info -> in; 1 a snapshot this build reads */
static int sn_info_e(const sn_slot_t *e, sn_info_t *in)
{
    memset(in, 0, sizeof *in);
    return e->state == SN_OK && e->total >= sizeof *in && !sn_read_e(e, 0, in, sizeof *in) && in->magic == SN_IMAGIC &&
           in->ver >= 1u && in->ilen >= sizeof *in && in->ilen <= e->total;
}
static int sn_info(uint32_t k, sn_info_t *in) { return k < SN_NSLOT && sn_info_e(&sn.slot[k], in); }

/* the sections of the stream into this build's store; *note gets SNN_SECS for those it cannot keep */
static void sn_apply_secs(const sn_slot_t *e, uint32_t at0)
{
    uint32_t at = at0, kind, id, n, b, i;
#if SEC_LOGGED
    sec_pend_clear();
    sec_stage_id = -1;
    for (i = 0; i < SN_SECS; i++)                      /* (all sixteen: the log keeps ids past this build's) */
        (void)slg_put(i, SN_REC, 0, 0);
    while (sn_next(e, &at, &kind, &id, &n, &b) > 0)
        if (kind == SNR_SEC && id < SN_SECS) {
            if (!n || n > SEC_REC_MAX || sn_read_e(e, b, SN_REC, n) || slg_put(id, SN_REC, n, 1))
                sn_note |= SNN_FAIL;                   /* (the room: the log held them all when it was saved) */
            else if (id >= SEC_IDS)
                sn_note |= SNN_SECS;
        }
    sec_gen++;
#else
    uint32_t got = 0;
    while (sn_next(e, &at, &kind, &id, &n, &b) > 0)
        if (kind == SNR_SEC && id < SN_SECS) {
            if (id >= 4u) {
                sn_note |= SNN_SECS;
                continue;
            }
            if (n <= SEC_REC_MAX && !sn_read_e(e, b, SN_REC, n) && sec_decode(SN_REC, n, &proj_slot[id], &proj_dl[id])) {
                got |= 1u << id;
                sec_dirty |= (uint16_t)(1u << id);
            } else
                sn_note |= SNN_FAIL;
        } else if (kind == SNR_TAIL && id < 4u && ((got >> id) & 1u) &&
                   (n > SN_TAIL_MAX || sn_read_e(e, b, st_buf, n) || !sn_tail_apply(&proj_slot[id], st_buf, n)))
            sn_note |= SNN_FAIL;
    for (i = 0; i < 4u; i++)
        if (!((got >> i) & 1u) && (proj_ok(&proj_slot[i]) || st_current(OBJ_PROJECT0 + i, &(st_hdr_t){0}) >= 0)) {
            proj_slot[i].magic = 0;                    /* (empty in the snapshot: the slot and its flash copies) */
            sec_dirty &= (uint16_t)~(1u << i);
            (void)st_erase(st_sector(OBJ_PROJECT0 + i, 0));
            (void)st_erase(st_sector(OBJ_PROJECT0 + i, 1));
        }
#endif
    live_sec = -1;
    sections_write();
}
/* the song of the stream (none: the default), stored */
static void sn_apply_song(const sn_slot_t *e, uint32_t at0)
{
    uint32_t at = at0, kind, id, n, b, i;
    arr_config_t c;
    arr_defaults(&c);
    while (sn_next(e, &at, &kind, &id, &n, &b) > 0)
        if (kind == SNR_SONG && n >= 4u && n <= 4u + 2u * 64u && !sn_read_e(e, b, SN_REC, n) && n == 4u + 2u * SN_REC[0]) {
            arr_config_t t;
            arr_defaults(&t);
            t.count = SN_REC[0], t.loop = SN_REC[1];
            for (i = 0; i < t.count && i < ARR_STEPS; i++)
                t.entry[i].scene = SN_REC[4u + 2u * i], t.entry[i].bars = SN_REC[5u + 2u * i];
            if (t.count <= ARR_STEPS && arr_stored_ok(&t))
                c = t;
            else
                sn_note |= SNN_SONG;
        }
    arrangement = c;
#if SEC_LOGGED
    if (sec_song_put(&arrangement, &song_tag))
        song_tag = 0;
#endif
    settings_save();
}

/* the work of stream e (its record at wb, wn bytes; its TAIL at tb, tn bytes, 0 none) -> autosave_buf; 1 ok */
static int sn_work_get(const sn_slot_t *e, uint32_t wb, uint32_t wn, uint32_t tb, uint32_t tn)
{
    if (sn_read_e(e, wb, SN_REC, wn) || !sec_decode(SN_REC, wn, &autosave_buf, &autosave_dl))
        return 0;
    return !tn || (tn <= SN_TAIL_MAX && !sn_read_e(e, tb, st_buf, tn) && sn_tail_apply(&autosave_buf, st_buf, tn));
}
/* load slot k: the state saved to BEFORE LOAD, then the work, the sections and the song replaced */
static int sn_load(uint32_t k)
{
    sn_slot_t src;
    sn_info_t in;
    uint32_t at, kind, id, n, b, work = 0, wb = 0, tail = 0, tb = 0;
    int rc = sn_can(), r;
    if (rc)
        return rc;
    if (k >= SN_NSLOT)
        return SNE_ARGS;
    if (sn.slot[k].state != SN_OK)
        return sn.slot[k].state == SN_BAD ? SNE_BAD : SNE_EMPTY;
    if (!sn_info(k, &in))
        return SNE_FORMAT;
    for (at = in.ilen; (r = sn_next(&sn.slot[k], &at, &kind, &id, &n, &b)) > 0;)
        if (kind == SNR_WORK && !work)
            work = n, wb = b;
        else if (kind == SNR_TAIL && id == SN_TAIL_WORK)
            tail = n, tb = b;
    if (r < 0 || !work || work > SEC_REC_MAX)
        return SNE_FORMAT;
    ed_snap_cancel();
    sections_write();
    src = sn.slot[k];
    if (k == SN_BAK)                                   /* (its old version read after the new one is in) */
        rc = sn_write(SN_BAK, 0, 1);
    else {
        rc = sn_write(SN_BAK, 0, 0);
        if (rc == SNE_FULL && !sn_clear(SN_BAK))       /* (an older BEFORE LOAD gives way) */
            rc = sn_write(SN_BAK, 0, 0);
        src = sn.slot[k];                              /* (the area rescanned: the same sectors) */
    }
    if (rc)
        return rc;
    if (!sn_work_get(&src, wb, work, tb, tail))
        return SNE_FORMAT;                             /* (nothing replaced; BEFORE LOAD holds the state) */
    sn_note = 0;
    sn_apply_secs(&src, in.ilen);
    sn_apply_song(&src, in.ilen);
    if (!sn_work_get(&src, wb, work, tb, tail))
        return SNE_FLASH;                              /* (decoded again: the buffer held the sections) */
    project_apply(&autosave_buf, &autosave_dl);
    if (proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl) == 0) {   /* (the loaded work kept at once) */
        MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
        autosave_hash = autosave_buf.sum ^ MOTION_HASH();
    }
    if (k == SN_BAK)
        (void)sn_erase_older(SN_BAK, sn.slot[SN_BAK].seq), sn_scan();
    return SNE_OK;
}

/* a stream this firmware would load (an import from the editor, before its commit): the info, records to the
 * end, one WORK, every record no longer than a section record */
static int sn_stream_ok(const sn_slot_t *e)
{
    sn_info_t in;
    uint32_t at, kind, id, n, b, work = 0;
    int r;
    if (!sn_info_e(e, &in))
        return 0;
    for (at = in.ilen; (r = sn_next(e, &at, &kind, &id, &n, &b)) > 0;) {
        if (n > SEC_REC_MAX || (kind == SNR_SEC && id >= SN_SECS))
            return 0;
        work += kind == SNR_WORK;
    }
    return r == 0 && work == 1u;
}

/* slot k again under another name (a new version, the old one dropped after: FULL without the room) */
static int sn_rename(uint32_t k, const char *name)
{
    uint8_t *b = st_buf;                               /* (scratch between storage operations: 256 B a piece) */
    sn_info_t in;
    sn_slot_t src;
    sn_wr_t w;
    uint32_t o;
    int rc = sn_can();
    if (rc)
        return rc;
    if (k >= SN_NSLOT || !sn_info(k, &in))
        return k < SN_NSLOT && sn.slot[k].state == SN_BAD ? SNE_BAD : SNE_EMPTY;
    ed_snap_cancel();
    src = sn.slot[k];
    memset(in.name, 0, sizeof in.name);
    str_cpy(in.name, name && name[0] ? name : "SNAPSHOT", SN_NAME);
    if ((rc = sn_wr_begin(&w, k, src.total)) != 0)
        return rc == -2 ? SNE_FULL : SNE_FLASH;
    memcpy(b, &in, sizeof in);
    rc = sn_wr_put(&w, b, sizeof in);
    for (o = sizeof in; !rc && o < src.total; o += SN_CHUNK) {
        uint32_t c = src.total - o < SN_CHUNK ? src.total - o : SN_CHUNK;
        rc = sn_read_e(&src, o, b, c) || sn_wr_put(&w, b, c);
    }
    if (rc) {
        sn_wr_release(&w);
        return SNE_FLASH;
    }
    return sn_wr_commit(&w) ? SNE_FLASH : SNE_OK;
}

/* the import: begin (slot, the stream's length and CRC), data (in order), commit */
static int sn_imp_begin(uint32_t k, uint32_t len, uint32_t crc)
{
    int rc;
    ed_snap_cancel();
    if (!flash_ok)
        return SNE_NOFLASH;
    if (song.playing || transport_req)                 /* (stopped, as a backup restore: the erases) */
        return SNE_PLAYING;
    if (sn_usr3_in_way())
        return SNE_USR3;
    if (k >= SN_NSLOT || len < sizeof(sn_info_t) || len > SN_MAX)
        return SNE_ARGS;
    if ((rc = sn_wr_begin(&sn_imp, k, len)) != 0)
        return rc == -2 ? SNE_FULL : SNE_FLASH;
    sn_imp_crc = crc;
    sn_imp_ms = fm1_ms;
    return SNE_OK;
}
static int sn_imp_data(uint32_t k, uint32_t off, const uint8_t *b, uint32_t n)
{
    sn_imp_tick();
    if (!sn_imp.open || sn_imp.slot != k || off != sn_imp.at || !n)
        return SNE_ORDER;
    sn_imp_ms = fm1_ms;
    return sn_wr_put(&sn_imp, b, n) ? SNE_FLASH : SNE_OK;
}
static int sn_imp_commit(uint32_t k)
{
    sn_slot_t e;
    sn_imp_tick();
    if (!sn_imp.open || sn_imp.slot != k || sn_imp.at != sn_imp.total)
        return SNE_ORDER;
    sn_wr_entry(&sn_imp, &e);
    if (~sn_imp.c != sn_imp_crc) {
        ed_snap_cancel();
        return SNE_CRC;
    }
    if (!sn_stream_ok(&e)) {
        ed_snap_cancel();
        return SNE_FORMAT;                             /* (not a snapshot this firmware loads: nothing written) */
    }
    return sn_wr_commit(&sn_imp) ? SNE_FLASH : SNE_OK;
}

static void sn_boot(void)                              /* persist_boot */
{
    if (flash_ok)
        sn_scan();
}

/* ---- SAVE > SNAPSHOT (ui_input.c, ui_draw.c): rows 0..FELUCCA_SNAPSHOTS-1 the slots, the last BEFORE LOAD */
static uint32_t sn_ui_slot(uint32_t i) { return i < FELUCCA_SNAPSHOTS ? i : SN_BAK; }
static uint32_t sn_ui_n(void) { return FELUCCA_SNAPSHOTS + 1u; }
static void sn_ui_label(char *b, uint32_t i)
{
    b[0] = i < FELUCCA_SNAPSHOTS ? (char)('1' + i) : 'B';
    b[1] = 0;
}
/* row i: its name and size; -> 0 empty, 1 saved (green), 2 saved by another build (amber), 3 damaged (red) */
static uint32_t sn_ui_row(uint32_t i, char *name, uint32_t *bytes)
{
    uint32_t k = sn_ui_slot(i);
    sn_info_t in;
    *bytes = 0;
    str_cpy(name, k == SN_BAK ? "BEFORE LOAD" : "EMPTY", SN_NAME + 1u);
    if (!sn.up || sn.slot[k].state == SN_EMPTY)
        return 0;
    if (sn.slot[k].state == SN_BAD || !sn_info(k, &in)) {
        str_cpy(name, "DAMAGED", SN_NAME + 1u);
        return 3;
    }
    memcpy(name, in.name, SN_NAME);
    name[SN_NAME] = 0;
    *bytes = sn.slot[k].total;
    return in.cfg != SN_CFG || in.nsec > FELUCCA_SECTIONS || in.pmagic != PROJ_MAGIC ? 2u : 1u;
}
static uint32_t sn_ui_free(void) { return sn.up ? sn_free_count() * SN_PAY : 0u; }
/* the state now would fit (counted when asked, at most every 2 s: a capture and an encode) */
static int sn_ui_fits(void)
{
    static uint32_t t, need;
    uint32_t i;
    if (!t || fm1_ms - t > 2000u) {
        t = fm1_ms | 1u;
        sn_work_capture();
        need = sizeof(sn_info_t) + 4u + sn_work_rec() + 4u + 4u + 2u * arrangement.count;
        for (i = 0; i < SN_SECS; i++)
            need += sn_sec_len(i) ? 4u + sn_sec_len(i) : 0u;
    }
    return sn.up && sn_fits(need);
}
static uint32_t sn_ui_sig(void)
{
    uint32_t i, h = sn.seq * 31u + sn_free_count();
    for (i = 0; i < SN_NSLOT; i++)
        h = h * 7u + sn.slot[i].state;
    return h;
}
/* the page's GO buttons: 0 load, 1 clear, 2 save; the message in the status colours */
static void sn_ui(uint32_t op, uint32_t i)
{
    static const char *const ERR[] = {"", "SNAPSHOT ERROR", "SNAPSHOTS FULL", "STOP FIRST", "NO FLASH", "SAVE ERROR",
                                      "SNAPSHOT DAMAGED", "EDITOR BUSY", "EMPTY SLOT", "WAIT FOR SILENCE",
                                      "CANNOT READ IT", "USR3 SAMPLE IN THE WAY"};
    char l[2], m[24];
    uint32_t k = sn_ui_slot(i);
    int rc;
    sn_ui_label(l, i);
    str_cpy(m, "SNAP ", sizeof m);
    str_cpy(m + 5, l, sizeof m - 5u);
    if (op == 1u) {
        rc = sn.slot[k].state == SN_EMPTY ? SNE_EMPTY : !flash_ok ? SNE_NOFLASH : song.playing || transport_req ? SNE_PLAYING :
             sn_clear(k) ? SNE_FLASH : SNE_OK;
        if (rc)
            ui_say_st(rc == SNE_EMPTY ? 0u : 3u, ERR[rc], "");
        else
            ui_say_st(1u, m, " CLEARED");
    } else {
        rc = op ? sn_save(k, 0) : sn_load(k);
        if (rc)
            ui_say_st(rc == SNE_PLAYING || rc == SNE_RING || rc == SNE_EMPTY ? 2u : 3u, ERR[rc], "");
        else if (op)
            ui_say_st(1u, m, " SAVED");
        else if (sn_note & SNN_FAIL)
            ui_say_st(3u, "LOADED: MEM FULL", "");
        else if (sn_note)
            ui_say_st(2u, "LOADED: ", sn_note & SNN_SECS ? (FELUCCA_SECTIONS == 4 ? "E-P SKIPPED" : "I-P NOT IN BUILD") : "NO SONG");
        else
            ui_say_st(1u, m, " LOADED");
    }
    ui.force = 1;
}
