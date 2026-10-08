/* SPDX-License-Identifier: GPL-3.0-only */
/* Optimist started on flash that SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f) wrote: nothing of 2.4's that Optimist
 * cannot read is erased or written over (FELUCCA_SL24_SAFE, storage.c st_keep / project.c pj_alien_scan).
 *
 *   - the four project slots (FELU objects 1..4 at 0x97000..0x9EFFF, FUN5 of 3840 B: their current copies) stay
 *     whole through the first start (sec_migrate), the log's use (compactions, MEM FULL) and later starts; the log
 *     works around them; PROJECT shows them as 2.4's, never EMPTY;
 *   - the autosave (object 7, 0x9F000 / 0xFE000): its current copy stays whole however often Optimist autosaves;
 *   - 2.4's FM6 bank (object 8 at 0xE5000 / 0xE6000, our drum records' sectors): never erased by a drum record save;
 *   - a user sample longer than our USR3 (2.4's USR3 runs to 0xDBFFF) and 2.4's USR4 (0xE7000..): the sectors it
 *     holds are never erased by our stores (UKIT, UP_FM6);
 *   - the settings record's last word, 2.4's lights word (MIDI OUT SEQ, IN CLOCK, USB SERIAL, the visualiser), is
 *     kept when Optimist writes the record back.
 * Run by tests/run_tests.sh. */
#define FELUCCA_ARRANGER 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1
#endif
#define FELUCCA_UP_FM6 1                          /* (OBJ_UPFM6 at 0xE7000: 2.4's USR4) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/storage/project.c"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
static union {
    project_t cur;
    uint8_t v8[PROJ_V8_N];
    uint8_t rec[SEC_REC_N];
    uint8_t sl24[3840];
} proj_tmp;
#include "../firmware/src/storage/drum_store.c"

static struct { int force; } ui;
static uint8_t sync_reload;
static void song_backup(void) {}
static void song_restore(void) {}
static char last_msg[64];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static uint8_t flash_ok = 1;
static uint16_t sec_dirty;
static uint8_t song_dirty, settings_saved;
static void settings_save(void) { settings_saved++; }
static void project_apply(const project_t *p, const dlrec_t *d) { proj_apply(p, d, 1); }
#include "../firmware/src/storage/sections/sections.c"   /* (and sl24_guard.c) */

static int bad;
static void check(const char *what, int ok)
{
    printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a SLOOP 2.4 project as 2.4 stores it (project.c proj_export: "FUN5", 3840 B, FNV-1a over all but the sum) */
static void fun5(uint8_t *b, uint32_t seed)
{
    uint32_t i, s = 0x811C9DC5u, r = seed * 2654435761u + 1u;
    for (i = 0; i < 3840u; i++) {
        r = r * 1103515245u + 12345u;
        b[i] = (uint8_t)(r >> 16);
    }
    memcpy(b, "5NUF", 4);                         /* (0x46554E35 little-endian) */
    b[4] = 0x00, b[5] = 0x0F, b[6] = b[7] = 0;    /* 3840 */
    for (i = 0; i < 3836u; i++)
        s = (s ^ b[i]) * 16777619u;
    memcpy(b + 3836, &s, 4);
}
/* a FELU object as 2.4's storage.c writes it (the same header as ours: magic, type, slot = the copy, seq, len, crc) */
static void obj_put(uint32_t off, uint32_t type, uint32_t copy, uint32_t seq, const void *p, uint32_t len)
{
    st_hdr_t h;
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off + ST_PAYLOAD_OFF, p, len);
    h.magic = ST_MAGIC, h.type = (uint16_t)type, h.slot = (uint16_t)copy, h.seq = seq, h.len = len;
    h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + off, &h, sizeof h);
}
static uint8_t keep_img[0x100000];
static project_t as_buf;
static dlrec_t as_dl;
static uint32_t cur_off[5];                       /* the 2.4 current copies (4 slots, the autosave) */
#define FM6B_A 0xE5000u                           /* 2.4's FM6 bank, copy A (object 8) */
#define USR3_HDR (SMP_USER_BASE + 2u * SMP_USER_SIZE)
#define USR4_HDR 0xE7000u
static void sample_hdr(uint32_t off, uint32_t len)   /* a 2.4 user sample slot's header and len bytes of data */
{
    smp_user_hdr_t h;
    uint32_t i;
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC, h.version = 1, h.nz = 1, h.data_len = len;
    memcpy(h.name, "SL24", 4);
    for (i = 0; i < len; i++)
        nor[off + SMP_USER_DATA + i] = (uint8_t)(i * 7u + 3u);
    memcpy(nor + off, &h, sizeof h);
}
static void sl24_flash(void)                      /* what SLOOP 2.4 leaves in flash */
{
    static uint8_t b[3840], fm6[27 * 128];
    uint32_t s;
    memset(nor, 0xFF, sizeof nor);
    for (s = 0; s < 4u; s++) {                    /* each slot saved twice: copy A seq 1, copy B seq 2 (current) */
        fun5(b, 10u + s);
        obj_put(st_sector(OBJ_PROJECT0 + s, 0), OBJ_PROJECT0 + s, 0, 1, b, 3840);
        fun5(b, 20u + s);
        obj_put(st_sector(OBJ_PROJECT0 + s, 1), OBJ_PROJECT0 + s, 1, 2, b, 3840);
        cur_off[s] = st_sector(OBJ_PROJECT0 + s, 1);
    }
    fun5(b, 30);
    obj_put(st_sector(OBJ_AUTOSAVE, 1), OBJ_AUTOSAVE, 1, 40, b, 3840);   /* (B current) */
    fun5(b, 31);
    obj_put(st_sector(OBJ_AUTOSAVE, 0), OBJ_AUTOSAVE, 0, 39, b, 3840);
    cur_off[4] = st_sector(OBJ_AUTOSAVE, 1);
    memset(fm6, 0x2A, sizeof fm6);
    obj_put(FM6B_A, 8, 0, 3, fm6, sizeof fm6);    /* 2.4's OBJ_FM6BANK (8), copy A only */
    sample_hdr(USR3_HDR, 0x13000u);              /* a USR3 of 76 KiB: to 0xDB200, past our USR3 */
    sample_hdr(USR4_HDR, 0x9000u);               /* USR4 */
    memcpy(keep_img, nor, sizeof nor);
}
static int whole(uint32_t off) { return !memcmp(nor + off, keep_img + off, 4096); }
static int all_whole(void)
{
    uint32_t s, ok = 1;
    for (s = 0; s < 5u; s++)
        ok &= whole(cur_off[s]);
    return ok;
}
static void boot(void)
{
    sec_pend_clear();
#if FELUCCA_SL24_SAFE
    sl24_boot_scan();
#endif
    sec_boot();
}
static void make(uint32_t s)                      /* the tracks as some section (a drum lane edited) */
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = NSTEP;
        for (k = 0; k < NSTEP; k++)
            if (i != TRK_DRUM)
                memset(&trk[i].step[k], (int)(1u + (s * 7u + k + i) % 200u), sizeof trk[i].step[k]);
    }
    memset(&dl, 0, sizeof dl);
    dl.ofs[2][DE_CUT] = (int8_t)(-1 - (int)s);
}

int main(void)
{
    uint32_t s, ok, saved;
    sl24_flash();
    boot();
    printf("first start: slots A..D %s, autosave %s, FM6 bank %s; log up %u\n",
           whole(cur_off[0]) && whole(cur_off[1]) && whole(cur_off[2]) && whole(cur_off[3]) ? "whole" : "ERASED",
           whole(cur_off[4]) ? "whole" : "ERASED", whole(FM6B_A) ? "whole" : "ERASED", slg.up);
    check("the first start: 2.4's four projects (current copies) and its autosave left whole", all_whole());
    ok = 1;
    for (s = 0; s < 4u; s++)
        ok &= !project_used(s);
#if FELUCCA_SL24_SAFE
    for (s = 0; s < 4u; s++)
        ok &= project_state(s) == PJ_SL24;
    ok &= project_state(4) == PJ_EMPTY;
#endif
    check("... PROJECT: A..D not ours (2.4's), not EMPTY; E.. empty", ok);
    check("... the log is up (writable) beside them", slg.up == 1);
    song.playing = 0, transport_req = 0;
#if FELUCCA_SL24_SAFE
    project_load(1);
    check("... LOAD of B: no EMPTY SLOT, nothing loaded over the work", strcmp(last_msg, "EMPTY SLOT") != 0);
#endif
#if FELUCCA_SL24_SAFE && FELUCCA_SL24_IMPORT
    {   /* every import path reads only: a slot (LOAD twice), the autosave (A24), 2.4's FM6 bank for a B patch */
        static uint8_t img[0x100000], b[3840];
        uint8_t *t = b + 12 + 64 + 940;
        uint32_t sum;
        memcpy(img, nor, sizeof nor);
        project_load(2), fm1_ms += 100, project_load(2);
        ok = !strncmp(last_msg, "2.4 IMPORTED", 12);
        sl24_auto_import();
        ok &= !strncmp(last_msg, "2.4 IMPORTED", 12);
        fun5(b, 50);                              /* (track 2 an FM6 part on B3: the bank is looked up) */
        t[122] = 9, t[120] = 10, t[121] = 0;
        sum = proj_hash(b, 3836), memcpy(b + 3836, &sum, 4);
        ok &= sl24_import_buf(b, sizeof b, 0);
        check("imports: slot C (LOAD twice), the autosave (A24), an FM6 part on a bank patch: all three done", ok);
        check("... flash not written at all by them: 2.4's projects, autosave, FM6 bank, samples as they were",
              !memcmp(img, nor, sizeof nor) && all_whole() && whole(FM6B_A));
    }
#endif
    for (s = 0, saved = 0; s < 3u * SEC_IDS; s++) {   /* the log used hard: saves until MEM FULL, again and again */
        make(s);
        project_save(s % SEC_IDS);
        saved += !strcmp(last_msg, "SAVED");
    }
    printf("sections saved: %u of %u; MEM %u B\n", saved, 3u * SEC_IDS, (unsigned)SEC_ROOM);
    check("48 section saves (compactions, MEM FULL): 2.4's projects still whole", all_whole() && saved > 0u);
    ok = 1;
    for (s = 0; s < 4u; s++) {
        make(s);
        project_save(s);
        ok &= !strcmp(last_msg, "SAVED") || !strcmp(last_msg, "MEM FULL");
    }
    check("... saving sections A..D themselves: 2.4's projects still whole", all_whole() && ok);
    for (s = 0; s < 6u; s++) {                    /* autosave: six writes, a drum record each */
        make(100u + s);
        proj_capture(&as_buf, &as_dl);
        (void)proj_put(OBJ_AUTOSAVE, &as_buf, &as_dl);
    }
    check("six autosaves (with drum records): 2.4's autosave still whole", whole(cur_off[4]));
    check("... 2.4's FM6 bank (0xE5000, object 8) still whole", whole(FM6B_A));
    {
        project_t q;
        dlrec_t d;
        ok = proj_get(OBJ_AUTOSAVE, &q, &d) && !memcmp(q.t, as_buf.t, sizeof q.t);
        check("... and the last autosave reads back (resume works beside them)", ok);
    }
    boot();
    check("a second start: everything of 2.4's still whole", all_whole() && whole(FM6B_A));
    make(7);
    project_save(9);
    check("... sections still save (J)", !strcmp(last_msg, "SAVED") || !strcmp(last_msg, "MEM FULL"));
    {   /* user samples: 2.4's USR3 runs past ours (into the snapshots, the banks), USR4 sits on UP_FM6 */
        static uint8_t kit[2048];
        int rc;
        memset(kit, 0x11, sizeof kit);
        rc = st_save(OBJ_UKIT, kit, sizeof kit);
        check("a user kit save (0xDA000, inside 2.4's long USR3): refused, the sample whole",
              rc != 0 && whole(0xDA000u) && whole(0xDB000u));
        rc = st_save(OBJ_UPFM6, kit, sizeof kit);
        check("an FM6 user preset save (0xE7000, 2.4's USR4): refused, USR4 whole",
              rc != 0 && whole(USR4_HDR) && whole(USR4_HDR + 0x1000u));
    }
#if FELUCCA_SL24_SAFE
    {   /* the settings record's last word: 2.4's lights word kept (our view slot reads it as ALL) */
        uint32_t kept = 0, v = persist_view_in(0x0034A105u, &kept);
        ok = v == 1u && persist_view_out(1u, kept) == 0x0034A105u && persist_view_out(0u, kept) == 0u;
        v = persist_view_in(0u, &kept);
        ok &= v == 0u && persist_view_out(0u, kept) == 0u && persist_view_out(1u, kept) == 1u;
        check("settings: 2.4's lights word (bits 14-20 too) written back as read; our VIEW 0 / 1 as before", ok);
    }
    {   /* their word <-> ours: NOTES (bit 8) inverted, SYNC moved, the rest as is */
        const uint32_t w = 0x2u | 0x10u | 0x100u | 0x800u | 1u << 12 | 1u << 14 | 3u << 17;   /* LIGHTS 2, KEYS 1, notes
                                                         * lit, USB AUDIO, SYNC USB, MIDI OUT SEQ, visualiser 3 */
        const uint32_t ours = sl_word_in(w);
        ok = ours == (0x4012u | ((SYNC_USB ^ SYNC_AUTO) & 3u) << 11 | (FELUCCA_VIS ? 3u << 17 : 0u));   /* (VIS: its style as is) */   /* NOTES OFF 0: they light; G_SYNC USB */
        check("settings: 2.3 / 2.4's word read: NOTES lit -> our NOTES OFF 0, SYNC USB, LIGHTS KEYS MIDI OUT as they are", ok);
        ok = sl_word_in(w & ~0x100u) == (ours | 0x100u);
        check("... their NOTES off -> our NOTES OFF 1 (bit 8 inverted)", ok);
        ok = sl_word_out(ours, w) == w;
        check("... written back: their word again (USB AUDIO, the visualiser kept from what was read)", ok);
        ok = sl_word_out(ours | 0x100u, w) == (w & ~0x100u) && sl_word_out(ours & ~(3u << 11), w) == w &&
             sl_word_out((ours & ~(3u << 11)) | (SYNC_TRS ^ SYNC_AUTO) << 11, w) == ((w & ~(3u << 12)) | SYNC_TRS << 12);
        check("... our NOTES OFF -> their bit 8 clear; SYNC TRS -> theirs; AUTO (they have none) keeps theirs", ok);
    }
#endif
    printf("sl24 safety test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
