/* SPDX-License-Identifier: GPL-3.0-only
 * Native tone collections (FELUCCA_NATIVE_BANKS): after Melodee 0.12 (Kerem Kilic, github.com/keremimo/melodee,
 * native_presets.c, cz_patch.h at v0.12), GPL-3.0-only; written for SLOOP's stores. */
/* An FM6 or CZ track's PRESETS list ends with its engine's own collection: tones that set only the engine's values
 * (EDIT: VOICE / TONE to the slot, the others to their defaults), so the track keeps its FX, mix and pattern.
 *   FM6  U01..U32: the FM6 user bank (fm6_store.c, VOICE U..; DX7 VMEM, written by STORE, DX7 SysEx, the editor)
 *   CZ   U01..U26: OBJ_CZBANK (storage.c, A/B below the snapshot area): u32 used, then 26 Casio CZ-1 tones of 144 B
 *        (128 synthesis bytes, the 16-byte name; Casio's MIDI layout, as Melodee keeps them), read in place (czb_xip)
 * Melodee has 64 FM6 and 128 CZ slots; Optimist's flash has the bank's 32 and one object's 26. A track refers to its
 * slot (VOICE / TONE): a slot written again changes the tracks playing it. Not ported: favorites, CZ STORE on the
 * device (the editor stores CZ tones). */
enum { NB_NONE, NB_FM6, NB_CZ };

static uint32_t nb_kind(const track_t *t)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (is_drum(t))
        return NB_NONE;
    if (FELUCCA_ENG_FM6 && ENG_IS(e, FM6))
        return NB_FM6;
#if CZ_NUSER
    if (e == &ENG_CZ)
        return NB_CZ;
#endif
    return NB_NONE;
}
static uint32_t nb_slots(uint32_t kind) { return kind == NB_FM6 ? FM6_NUSER : kind == NB_CZ ? CZ_NUSER : 0u; }
#if CZ_NUSER
#define NB_CZ_BASE CZT_N                                  /* TONE of U01: after the built-in tones */
#else
#define NB_CZ_BASE 0u
#endif
static uint32_t nb_base(uint32_t kind) { return kind == NB_FM6 ? FM6_NROM : NB_CZ_BASE; }

/* slot k's stored bytes and where its name is in them, 0: empty */
static const uint8_t *nb_raw(uint32_t kind, uint32_t k, uint32_t *name_at)
{
    if (kind == NB_FM6) {
        const uint8_t *b = fm6_bank_xip ? fm6_bank_xip + k * 128u : 0;
        *name_at = 118u;
        return b && memcmp(b + 118, "INIT VOICE", 10) ? b : 0;   /* (an erased slot holds the init voice) */
    }
    *name_at = 128u;
#if CZ_NUSER
    return czb_tone(CZT_N + k);
#else
    return 0;
#endif
}
static int nb_used(uint32_t kind, uint32_t k) { uint32_t o; return k < nb_slots(kind) && nb_raw(kind, k, &o); }

static void nb_name(uint32_t kind, uint32_t k, char *nm)  /* 12 characters at most, trailing blanks cut */
{
    uint32_t o, i, n = 0;
    const uint8_t *b = nb_raw(kind, k, &o);
    for (i = 0; b && i < 12u && i < (kind == NB_FM6 ? 10u : 16u); i++) {
        nm[i] = (char)(b[o + i] >= 32u && b[o + i] <= 126u ? b[o + i] : ' ');
        if (nm[i] != ' ')
            n = i + 1u;
    }
    nm[n] = 0;
}

/* the selected track's collection as the PRESETS list shows it, its used slots in order: their count; *rank the
 * place of the one the track plays (VOICE / TONE), else the count. n < count: *at the n-th one's slot */
static uint32_t nb_list(uint32_t *rank, uint32_t n, uint32_t *at)
{
    uint32_t kind = nb_kind(TSEL), v = (uint32_t)TSEL->p[P_E0] - nb_base(kind), k, c = 0;
    *rank = ~0u;
    for (k = 0; k < nb_slots(kind); k++)
        if (nb_used(kind, k)) {
            if (k == v)
                *rank = c;
            if (c++ == n)
                *at = k;
        }
    if (*rank > c)
        *rank = c;
    return c;
}
static void nb_row(uint32_t k, char *tag, char *nm)       /* "F07" / "Z07" (Melodee's letters), its name */
{
    uint32_t kind = nb_kind(TSEL);
    up_slot_label(tag, k);
    tag[0] = kind == NB_FM6 ? 'F' : 'Z';
    nb_name(kind, k, nm);
}

/* slot k into the selected track: the engine's values only (FM6: its buffer loaded afresh) */
static void nb_load(uint32_t k)
{
    track_t *t = TSEL;
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t kind = nb_kind(t), i;
    if (!nb_used(kind, k))
        return;
    fm1_irq_off();
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    t->p[P_E0] = (int16_t)(nb_base(kind) + k);
    t->user = 0;
    if (kind == NB_FM6)
        fm6_cur[song.sel % NPART] = 0;
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
}

#if CZ_NUSER
/* ------------------------------------------------------------ the CZ store --- */
#define CZB_LEN (4u + CZ_NUSER * 144u)
#if FELUCCA_FLASH
_Static_assert(CZB_LEN <= ST_PAYLOAD_MAX, "the CZ collection: one storage object");
#endif
_Static_assert(0xD8000u - (SN_SECTORS + 2u) * 0x1000u >= 0xC8000u, "the CZ collection: inside USR3");
#define CZB_XIP(off) (smp_user_xip(2) + ((off) - (SMP_USER_BASE + 2u * SMP_USER_SIZE)))   /* (the host's slot image too) */

static void czb_find(void)                                /* boot, after a save: the current copy's payload */
{
    const uint8_t *b = 0;
#if FELUCCA_FLASH
    st_hdr_t h;
    int c = flash_ok ? st_current(OBJ_CZBANK, &h) : -1;
    if (c >= 0 && h.len == CZB_LEN)
        b = CZB_XIP(st_sector(OBJ_CZBANK, (uint32_t)c) + ST_PAYLOAD_OFF);
#endif
    czb_xip = b;
    memset(cz_key, 0, sizeof cz_key);                     /* (the parts on a slot take it again: eng_cz.c cz_block) */
}

/* slot k <- raw (144 B), 0 = erase it. -> 0 saved, 1 no such slot, 2 the flash said no. The bytes are not checked
 * (the editor checks them, Melodee's cz_patch_valid): the playback masks every field it indexes with */
static int czb_put(uint32_t k, const uint8_t *raw)
{
    int rc = 2;
    if (k >= CZ_NUSER)
        return 1;
#if FELUCCA_FLASH
    {
        const uint8_t *b = czb_xip;
        uint32_t *used = (uint32_t *)(void *)st_buf;      /* (the payload, built in st_buf: st_save takes it there) */
        if (b)
            memcpy(st_buf, b, CZB_LEN);
        else
            *used = 0;                                    /* (the empty slots' bytes are never read) */
        *used = raw ? *used | 1u << k : *used & ~(1u << k);
        if (raw)
            memcpy(st_buf + 4u + k * 144u, raw, 144);
        czb_xip = 0;                                      /* (meanwhile the parts on a slot play INIT) */
        rc = flash_ok && !st_save(OBJ_CZBANK, st_buf, CZB_LEN) ? 0 : 2;
        fm1_irq_off();
        fl_inval(st_sector(OBJ_CZBANK, 0), 2u * ST_SECTOR);
        fm1_irq_on();
        czb_find();
    }
#endif
    return rc;
}
#endif
