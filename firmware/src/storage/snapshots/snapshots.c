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
#include "../sections/sec_codec.c"                                 /* (FELUCCA_SECTIONS 4: the slots go in as section records) */
#endif
#include "snap_store.c"

#define SN_IMAGIC 0x50414E53u                          /* "SNAP" */
#define SN_VERSION 1u
#define SN_NAME 12u
#define SN_CHUNK 256u                                  /* a piece copied or sent (st_buf as scratch) */
enum { SNR_WORK = 1, SNR_SEC, SNR_SONG, SNR_TAIL, SNR_XSTEP, SNR_LOG };   /* XSTEP: SLOOP 2.4's step extras; LOG: another record of the log (below) */
#define SNR_FXR 16u                                    /* the FX slots' record (below; a kind kept clear of others') */
#define SN_TAIL_WORK 0x80u                             /* a TAIL / XSTEP record's id: the work's (else the section's) */
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
#if SEC_LOGGED
/* LOG: the log's records of the patterns and their state (ids SN_LOG0..SN_LOG1 - 1: pat.c), as they are: every build
 * with the log keeps them (a scene a PATTERNS build stored plays in any). Log id -> SN_REC, its length (0 none) */
#define SN_LOG0 SEC_ID_PSTATE
#define SN_LOG1 (SEC_ID_PAT0 + NTRK * PAT_N)
static uint32_t sn_log_rec(uint32_t id)
{
    int n;
#if FELUCCA_PATTERNS
    if (id == SEC_ID_PSTATE) {                         /* the work's sources as they are now (the log's: the last */
        memcpy(SN_REC, pat_cur, NTRK);                 /* autosave's, none before the first) */
        return NTRK;
    }
#endif
    if (id >= SEC_ID_PAT0)
        return pat_get((id - SEC_ID_PAT0) / PAT_N, (id - SEC_ID_PAT0) % PAT_N, SN_REC);
    n = flash_ok ? slg_get(id, SN_REC) : 0;
    return n > 0 ? (uint32_t)n : 0u;
}
#endif
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
#if FELUCCA_AUTO
    {
        auto_store_t *ms = auto_for(p, 0);
        if (ms && ms->psum == old)
            ms->psum = p->sum;                         /* (its automation stays its own) */
    }
#endif
    (void)old;
    return a == e;
}

#if FELUCCA_AUTO && !SEC_LOGGED
static uint8_t sx_rbuf[AX_ENC_MAX] __attribute__((section(".pool"), aligned(4)));   /* (SECTIONS 4: stepx_log.c has it) */
#endif
#if FELUCCA_AUTO
/* XSTEP (FELUCCA_AUTO; auto_proj.c): a project's extras record (its step-only events as SLOOP 2.4's step extras, or
 * its whole automation store in the new form: ax_encode, no key) after the record they belong to: id SN_TAIL_WORK the work's, else a
 * section's (16 sections A..P of the log; the four RAM slots of SECTIONS 4 keep none). A snapshot without them
 * (older, or a project with none) loads with none; an older build skips the record. The bytes go through sx_rbuf. */
/* the work's extras (captured into autosave_buf's store with it) -> sx_rbuf, their length, 0 none */
static uint32_t sn_xs_work(void)
{
    const auto_store_t *m = auto_of(&autosave_buf);
    return m ? ax_encode(m, sx_rbuf) : 0u;
}
/* section id's extras (the pending arena's, else the log's, when they belong to the record as it is) -> sx_rbuf,
 * their length, 0 none; the section's record is left in SN_REC */
static uint32_t sn_xs_sec(uint32_t id)
{
#if SEC_LOGGED
    uint32_t n = sn_sec_rec(id), key, k, rl = 0;
    if (!n)
        return 0;
    key = proj_hash(SN_REC, n);
    if (id < SEC_IDS && sec_pend_has(SEC_IDS + id)) {
        rl = sec_pend.len[SEC_IDS + id];
        if (rl <= sizeof sx_rbuf)
            memcpy(sx_rbuf, sec_pend.data + sec_pend.off[SEC_IDS + id], rl);
        else
            rl = 0;
    } else if (flash_ok && slg_has(SX_ID0 + id) && slg.alen[SX_ID0 + id] <= sizeof sx_rbuf) {
        int g = slg_get(SX_ID0 + id, sx_rbuf);
        rl = g > 0 ? (uint32_t)g : 0u;
    }
    if (rl <= 4u || (memcpy(&k, sx_rbuf, 4), k != key))
        return 0;
    for (k = 0; k < rl - 4u; k++)                       /* (down by 4: forward is safe; the libc has no memmove) */
        sx_rbuf[k] = sx_rbuf[k + 4];
    return rl - 4u;
#else
    (void)id;
    return 0;
#endif
}
/* the work's extras from the stream (xn bytes at xb; none: its motion only) into autosave_buf's store, which belongs
 * to the project as it is now (sn_tail_apply changed its sum; the record's motion read into it before) */
static void sn_work_xs(const sn_slot_t *e, uint32_t xb, uint32_t xn)
{
    auto_store_t *m = auto_for(&autosave_buf, 1);
    if (!m)
        return;
    if (m->psum != autosave_buf.sum)
        auto_store_clear(m);
    m->psum = autosave_buf.sum;
    if (xn && xn <= AX_ENC_MAX && !sn_read_e(e, xb, sx_rbuf, xn))
        (void)ax_decode(m, sx_rbuf, xn);
}
#endif

/* FXR (fx_rec.c, fx_rec_log.c): the FX slots' record after the record it belongs to, as XSTEP: id SN_TAIL_WORK the
 * work's (its stored form, no key: it belongs to the work as loaded), else a section's (the log record, key and all:
 * the section's record is copied as it is, so its key holds). None: the default layout; an older build skips them. */
static uint32_t sn_fx_work(uint8_t *o)                 /* the work's -> o, its length, 0 none */
{
    const fxr_store_t *m = fxr_of(&autosave_buf);
    if (!m || !m->n)
        return 0;
    memcpy(o, m->b, m->n);
    return m->n;
}
static void sn_work_fx(const sn_slot_t *e, uint32_t b, uint32_t n)   /* the stream's (n 0: none) -> autosave_buf's store */
{
    fxr_store_t *m = fxr_for(&autosave_buf, 1);
    if (!m)
        return;
    m->psum = autosave_buf.sum;
    m->n = (uint16_t)(n && n <= FXR_MAX && !sn_read_e(e, b, m->b, n) ? n : 0u);
}
#if SEC_LOGGED
static uint32_t sn_fx_sec(uint32_t id)                 /* section id's, when it belongs to its record -> fxr_rbuf, 0 none */
{
    uint32_t n = sn_sec_rec(id), rl, k;
    if (!n)
        return 0;
    rl = fxr_sec_get(id);
    return rl > 4u && (memcpy(&k, fxr_rbuf, 4), k == proj_hash(SN_REC, n)) ? rl : 0u;
}
#endif

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
    uint32_t i, n, total, nwork, nsong, nrec = 2, tw, fw;
    uint8_t fl[SN_SECS];
#if FELUCCA_AUTO
    uint16_t xl[SN_SECS];
    uint32_t xw;
#endif
    int rc;
    memset(&in, 0, sizeof in);
    memset(tl, 0, sizeof tl);
    sn_work_capture();
    nwork = sn_work_rec();
    in.flags = (SN_REC[0] & SEC_MOT) ? 1u : 0u;        /* (the work carries motion) */
    tw = sn_tail(&autosave_buf, st_buf);
    nsong = 4u + 2u * (arrangement.count < ARR_STEPS ? arrangement.count : ARR_STEPS);   /* (sn_song: written last) */
    total = sizeof in + 4u + nwork + (tw ? 4u + tw : 0u) + 4u + nsong;
    memset(fl, 0, sizeof fl);
    if ((fw = sn_fx_work(st_buf)) != 0)
        total += 4u + fw, nrec++;
#if FELUCCA_AUTO
    memset(xl, 0, sizeof xl);
    if ((xw = sn_xs_work()) != 0)
        total += 4u + xw, nrec++;
#endif
    for (i = 0; i < SN_SECS; i++)
        if ((len[i] = (uint16_t)sn_sec_len(i)) != 0) {
            total += 4u + len[i];
            in.secmask |= 1u << i;
            nrec++;
#if !SEC_LOGGED
            if ((tl[i] = (uint16_t)sn_tail(&proj_slot[i], st_buf)) != 0)
                total += 4u + tl[i];
#endif
#if FELUCCA_AUTO
            if ((xl[i] = (uint16_t)sn_xs_sec(i)) != 0)
                total += 4u + xl[i], nrec++;
#endif
#if SEC_LOGGED
            if ((fl[i] = (uint8_t)sn_fx_sec(i)) != 0)
                total += 4u + fl[i], nrec++;
#endif
        }
#if SEC_LOGGED
    for (i = SN_LOG0; i < SN_LOG1; i++)                /* (the patterns, their state) */
        if ((n = sn_log_rec(i)) != 0)
            total += 4u + n, nrec++;
#endif
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
        n = sn_work_rec();                             /* (encoded again: the buffer held the others' sizes) */
        rc = n != nwork || sn_put_rec(&w, SNR_WORK, 0, SN_REC, nwork);
    }
    if (!rc && tw)
        rc = sn_tail(&autosave_buf, st_buf) != tw || sn_put_rec(&w, SNR_TAIL, SN_TAIL_WORK, st_buf, tw);
#if FELUCCA_AUTO
    if (!rc && xw)
        rc = sn_xs_work() != xw || sn_put_rec(&w, SNR_XSTEP, SN_TAIL_WORK, sx_rbuf, xw);
#endif
    if (!rc && fw)
        rc = sn_fx_work(st_buf) != fw || sn_put_rec(&w, SNR_FXR, SN_TAIL_WORK, st_buf, fw);
    for (i = 0; !rc && i < SN_SECS; i++)
        if ((in.secmask >> i) & 1u) {
            n = sn_sec_rec(i);
            rc = n != len[i] || sn_put_rec(&w, SNR_SEC, i, SN_REC, n);
#if !SEC_LOGGED
            if (!rc && tl[i])
                rc = sn_tail(&proj_slot[i], st_buf) != tl[i] || sn_put_rec(&w, SNR_TAIL, i, st_buf, tl[i]);
#endif
#if FELUCCA_AUTO
            if (!rc && xl[i])
                rc = sn_xs_sec(i) != xl[i] || sn_put_rec(&w, SNR_XSTEP, i, sx_rbuf, xl[i]);
#endif
#if SEC_LOGGED
            if (!rc && fl[i])
                rc = sn_fx_sec(i) != fl[i] || sn_put_rec(&w, SNR_FXR, i, fxr_rbuf, fl[i]);
#endif
        }
#if SEC_LOGGED
    for (i = SN_LOG0; !rc && i < SN_LOG1; i++)
        if ((n = sn_log_rec(i)) != 0)
            rc = sn_put_rec(&w, SNR_LOG, i, SN_REC, n);
#endif
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
        if (rc)
            sn_scan();                                 /* (the old version stays the slot's: its sectors taken) */
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
    uint32_t fxh = 0;                                  /* (the sections stored: their FX records go after them) */
#if FELUCCA_AUTO
    uint32_t key[SN_SECS], have = 0;                   /* (each section's record hash: its extras' key) */
#endif
    sec_pend_clear();
    sec_stage_id = -1;
    for (i = 0; i < SN_SECS; i++) {                    /* (all sixteen: the log keeps ids past this build's) */
        (void)slg_put(i, SN_REC, 0, 0);
#if FELUCCA_AUTO
        (void)slg_put(SX_ID0 + i, SN_REC, 0, 0);       /* (and their extras: the snapshot's, or none) */
#endif
        (void)fxr_sec_put(i, SN_REC, 0);               /* (and their FX records) */
    }
    for (i = SN_LOG0; i < SN_LOG1; i++)                /* (the patterns, their state: the snapshot's, or none) */
        (void)slg_put(i, SN_REC, 0, 0);
    while (sn_next(e, &at, &kind, &id, &n, &b) > 0)
        if (kind == SNR_SEC && id < SN_SECS) {
            if (!n || n > SEC_REC_MAX || sn_read_e(e, b, SN_REC, n) || slg_put(id, SN_REC, n, 1))
                sn_note |= SNN_FAIL;                   /* (the room: the log held them all when it was saved) */
            else {
                if (id >= SEC_IDS)
                    sn_note |= SNN_SECS;
                fxh |= 1u << id;
#if FELUCCA_AUTO
                have |= 1u << id, key[id] = proj_hash(SN_REC, n);
#endif
            }
        } else if (kind == SNR_LOG && id >= SN_LOG0 && id < SN_LOG1) {
            if (!n || n > SEC_REC_MAX || sn_read_e(e, b, SN_REC, n) || slg_put(id, SN_REC, n, 1))
                sn_note |= SNN_FAIL;
        }
        else if (kind == SNR_FXR && id < SN_SECS && ((fxh >> id) & 1u)) {   /* (after its section's record) */
            if (n > sizeof fxr_rbuf || sn_read_e(e, b, fxr_rbuf, n) || fxr_sec_put(id, fxr_rbuf, n))
                sn_note |= SNN_FAIL;
        }
#if FELUCCA_AUTO
        else if (kind == SNR_XSTEP && id < SN_SECS && ((have >> id) & 1u)) {   /* (after its section's record) */
            memcpy(sx_rbuf, &key[id], 4);
            if (!n || n > AX_ENC_MAX || sn_read_e(e, b, sx_rbuf + 4, n) || slg_put(SX_ID0 + id, sx_rbuf, 4u + n, 1))
                sn_note |= SNN_FAIL;
        }
#endif
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
#if FELUCCA_AUTO
#define SN_WORK_GET(e, wb, wn, tb, tn, xb, xn) sn_work_get(e, wb, wn, tb, tn, xb, xn)
static int sn_work_get(const sn_slot_t *e, uint32_t wb, uint32_t wn, uint32_t tb, uint32_t tn, uint32_t xb, uint32_t xn)
#else
#define SN_WORK_GET(e, wb, wn, tb, tn, xb, xn) ((void)(xb), (void)(xn), sn_work_get(e, wb, wn, tb, tn))
static int sn_work_get(const sn_slot_t *e, uint32_t wb, uint32_t wn, uint32_t tb, uint32_t tn)
#endif
{
    if (sn_read_e(e, wb, SN_REC, wn) || !sec_decode(SN_REC, wn, &autosave_buf, &autosave_dl))
        return 0;
    if (tn && (tn > SN_TAIL_MAX || sn_read_e(e, tb, st_buf, tn) || !sn_tail_apply(&autosave_buf, st_buf, tn)))
        return 0;
#if FELUCCA_AUTO
    sn_work_xs(e, xb, xn);
#endif
    return 1;
}
/* load slot k: the state saved to BEFORE LOAD, then the work, the sections and the song replaced */
static int sn_load(uint32_t k)
{
    sn_slot_t src;
    sn_info_t in;
    uint32_t at, kind, id, n, b, work = 0, wb = 0, tail = 0, tb = 0, xs = 0, xb = 0, fx = 0, fxb = 0;
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
        else if (kind == SNR_XSTEP && id == SN_TAIL_WORK)
            xs = n, xb = b;
        else if (kind == SNR_FXR && id == SN_TAIL_WORK)
            fx = n, fxb = b;
    if (r < 0 || !work || work > SEC_REC_MAX)
        return SNE_FORMAT;
    if (!SN_WORK_GET(&sn.slot[k], wb, work, tb, tail, xb, xs))
        return SNE_FORMAT;                             /* (checked before anything is replaced) */
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
    if (!SN_WORK_GET(&src, wb, work, tb, tail, xb, xs))
        return SNE_FORMAT;                             /* (nothing replaced; BEFORE LOAD holds the state) */
    sn_note = 0;
    sn_apply_secs(&src, in.ilen);
    sn_apply_song(&src, in.ilen);
    if (!SN_WORK_GET(&src, wb, work, tb, tail, xb, xs))
        return SNE_FLASH;                              /* (decoded again: the buffer held the sections) */
    sn_work_fx(&src, fxb, fx);                         /* (its FX record: the project as decoded) */
    project_apply(&autosave_buf, &autosave_dl);
#if FELUCCA_PATTERNS
    pat_state_load();                                  /* (the tracks' pattern sources, as the snapshot left them) */
#endif
    if (proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl) == 0) {   /* (the loaded work kept at once) */
        MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_AUTO && SEC_LOGGED
        (void)sx_log_put(SX_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its step extras, as autosave_tick */
#endif
#if SEC_LOGGED
        (void)fxr_log_put(FXR_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its FX record, as autosave_tick */
#endif
        autosave_hash = autosave_buf.sum ^ AUTO_HASH() ^ fxr_hash(&autosave_buf);
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
    sn_imp_tick();
    if ((!t || fm1_ms - t > 2000u) && !proj_tmp_busy()) {
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
