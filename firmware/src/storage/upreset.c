/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, 0xDC000..0xDFFFF), 16
 * records each. A record: engine, name, the sound's values, a 16-step pattern.
 *
 * UPB4 (2026-10-24; the formats before it, ours and SLOOP's, are not read: a
 * bank of another magic, record size or slot count reads as empty). Every
 * value of a sound is a signed byte (tests/auto_int8_check.c checks that every
 * one's range fits; FM6's stored VOICE, 0 or 64 .. 143, is kept less 64), so a
 * record holds UP_NV of them in 216 bytes, twice the 86 a sound has today:
 *   v[0 .. ne0 - 1]          P_LEVEL .. the value before P_E0, in P_* order
 *   v[ne0 .. ne0 + 7]        P_E0 .. P_E7 (the engine's own)
 *   v[ne0 + 8 .. nv - 1]     the sound's values after P_E7: P_E1PIT .. P_SLAST
 * A record of another build (another ne0, or fewer values after P_E7) is read
 * by count: the values it lacks take their defaults, the ones this build lacks
 * are dropped. A new value of a sound goes after P_SLAST (core.h): older
 * records then load it at its default. One before P_E0 moves P_E0 for every
 * record and project (FUNB's PJ_NP): UP_LAYOUT stops the build until UP_VER
 * moves with it.
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_NV 167u                               /* a record's values (bytes) */
#define UP_USED 0xA5u
#define UP_VER 5u
#define UP_NTAIL (P_SLAST - P_E1PIT + 1u)        /* the sound's values after P_E7 */
#define UP_FM6_OFS 64                            /* FM6's stored VOICE (fm6_voice_store: 0, 64 .. 143) less this */
#define UP_BANK_MAGIC 0x34425055u                /* "UPB4" */
typedef struct {
    uint8_t used, ver, engine, nv, ne0;          /* UP_USED, UP_VER, engine UID, values stored, where P_E0's are */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    int8_t v[UP_NV];
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 216 && sizeof(up_bank_t) <= 3840u, "user preset record layout (UPB4)");
_Static_assert(P_E0 + 8u + UP_NTAIL <= UP_NV && P_ENG_END + UP_NTAIL < 256u, "user preset record: the sound's values");
#if FELUCCA_ANALOG2
#define UP_LAYOUT (P_E0 == 67u && UP_NTAIL == 11u)
#else
#define UP_LAYOUT (UP_NTAIL == 4u)
#endif
_Static_assert(UP_LAYOUT, "the values of a sound moved: a new one goes after P_SLAST; else bump UP_VER and read UP_VER 5 by its own map");
/* len bytes at src (-1: none) are a UPB4 bank (any other shape: read as empty) */
static int up_bank_ok(const void *src, int len)
{
    const up_bank_t *p = (const up_bank_t *)src;
    return len == (int)sizeof *p && p->magic == UP_BANK_MAGIC && p->rsize == sizeof(up_rec_t) && p->nslot == UP_PER_BANK;
}
#if ST_XIP
/* the banks are read where they are in flash (storage.c st_xip: the current copy's payload through the XIP window, cached
 * as the code is), not mirrored in RAM: 6,928 B of the pool back (2026-10-23). up_xip[b]: a UPB4 bank as it is, else 0
 * (no valid copy, or another shape: every record empty). A save builds the bank in st_buf (st_save takes a source there)
 * and reads the copy written; without a flash that reads (flash_ok 0) there is no bank at all: a save fails, nothing is
 * kept for the session. The host keeps the RAM mirror unless a test maps the flash image (storage.c ST_XIP_PTR) */
#define UP_XIP 1
static const up_bank_t *up_xip[UP_SLOTS / UP_PER_BANK];
#if FELUCCA_SD_UI
static up_rec_t up_card IN_POOL;                 /* SD card mode: a preset on its way to or from the card, slot UP_CARD (sd_mode.c) */
#define UP_CARD UP_SLOTS
#endif
static const up_rec_t *up_rec(uint32_t k)       /* 0: a slot of a bank that is not there (up_valid and up_name take it
                                                  * as empty; every other reader comes after up_used; a 216-byte zero
                                                  * record in flash cost as much, 2026-10-24) */
{
    uint32_t b = k / UP_PER_BANK;
#if FELUCCA_SD_UI
    if (k == UP_CARD)
        return &up_card;
#endif
    return up_xip[b] ? &up_xip[b]->r[k % UP_PER_BANK] : 0;
}
#else
#define UP_XIP 0
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK] IN_POOL;

#if FELUCCA_SD_UI
static up_rec_t up_card IN_POOL;                 /* SD card mode: a preset on its way to or from the card, slot UP_CARD (sd_mode.c) */
#define UP_CARD UP_SLOTS
static up_rec_t *up_rec(uint32_t k) { return k == UP_CARD ? &up_card : &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }
#else
static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }
#endif
#endif

#if UP_XIP
/* bank b's current copy in flash when it is a UPB4 bank (st_xip read the payload once through the driver, for its CRC) */
static void up_bank_find(uint32_t b)
{
    uint32_t len = 0;
    const up_bank_t *p = flash_ok ? (const up_bank_t *)st_xip(OBJ_UPRESET0 + b, &len) : 0;
    up_xip[b] = p && up_bank_ok(p, (int)len) ? p : 0;
}
#else
static void up_bank_check(uint32_t b, const void *src, int len)
{
    up_bank_t *bk = &up_bank[b];
    if (!up_bank_ok(src, len))
        memset(bk, 0, sizeof *bk);
    else if ((const void *)bk != src)
        memcpy(bk, src, sizeof *bk);
}
#endif

static int up_valid(const up_rec_t *r)
{
    return r && r->used == UP_USED && r->ver == UP_VER && r->engine < ENG_UID_N && r->name[0] && r->ne0 <= r->nv &&
           r->nv <= UP_NV && r->nv >= r->ne0 + 8u;
}

#if FELUCCA_SD_UI
static int up_used(uint32_t k) { return k <= UP_CARD && up_valid(up_rec(k)); }   /* (UP_CARD: the card's preset) */
#else
static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }
#endif
/* the engine slot of user preset k (the Optimist UI names it beside the preset), NENGINES: empty or not built */
static uint32_t up_engine_slot(uint32_t k)
{
    return up_used(k) && eng_built(up_rec(k)->engine) ? eng_slot_built(up_rec(k)->engine) : NENGINES;
}

#include "../core/lfo_store.h"                     /* (LFO DEST: lfo1_store, lfo1_load) */
#include "../core/env_store.h"                     /* (ENV 1 MOD, ENV2 DEST: env1_fold, env_store, env_load) */
static int8_t up_b(int32_t x) { return (int8_t)(x < -128 ? -128 : x > 127 ? 127 : x); }
/* values v (today's P_* order, P_COUNT) -> record r's v[], nv, ne0 (the layout above). Values in the words of the depths
 * of before (ENV2's amounts, which today's values hold at 0) that are not 0 come from outside (the editor, a file of
 * before): read as those depths (env_load), then 0 */
static void up_vals_put(up_rec_t *r, const int16_t *v0)
{
    int16_t v[P_COUNT];
    uint32_t i;
    memcpy(v, v0, sizeof v);
#if FELUCCA_ANALOG2
    if (v[P_A2FENV] || v[P_A2EPIT] || v[P_A2ESHP] || v[P_A2EOS2] || v[P_A2ESDT])
        (void)env_load(v);
#endif
#if FELUCCA_ENG_FM6 && (!defined(UP_HOST) || defined(UP_WITH_ENGINES))
    if (r->engine == ENG_UID_FM6)                        /* (VOICE as stored: INIT, or a pack voice by its fixed number) */
        v[P_E0] = (int16_t)(fm6_voice_store(v[P_E0]) - UP_FM6_OFS);
#endif
    r->ver = UP_VER;
    r->ne0 = P_E0;
    r->nv = (uint8_t)(P_ENG_END + UP_NTAIL);
    memset(r->v, 0, sizeof r->v);
    for (i = 0; i < P_ENG_END; i++)
        r->v[i] = up_b(v[i]);
    for (i = 0; i < UP_NTAIL; i++)
        r->v[P_ENG_END + i] = up_b(v[P_E1PIT + i]);
}

/* the record's values in today's P_* order (mapped by count, see above; def = the defaults), each DEST slot inside the
 * list (a DEST this build lacks: OFF) */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    uint32_t i, k, nt = r->nv - r->ne0 - 8u;
    for (i = 0; i < P_COUNT; i++)
        out[i] = def[i];
    for (i = 0; i < P_E0 && i < r->ne0; i++)
        out[i] = r->v[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = r->v[r->ne0 + i];
    for (i = 0; i < UP_NTAIL && i < nt; i++)
        out[P_E1PIT + i] = r->v[r->ne0 + 8u + i];
#if FELUCCA_ENG_FM6
    if (r->engine == ENG_UID_FM6)
        out[P_E0] = (int16_t)(out[P_E0] + UP_FM6_OFS);
#endif
    for (k = 0; k < 2u; k++)
        if ((uint32_t)out[P_L1DA + 2u * k] >= LT_NB)
            out[P_L1DA + 2u * k] = LT_OFF, out[P_L1DA + 2u * k + 1u] = 0;
#if FELUCCA_ANALOG2
    for (k = 0; k <= P_NLAST - P_N2DA; k += 2u)
        if ((uint32_t)out[P_N2DA + k] >= LT_NB)
            out[P_N2DA + k] = LT_OFF, out[P_N2DA + k + 1u] = 0;
#endif
}


static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; r && i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) -> *r (values not yet
 * clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k;
    int16_t v[P_COUNT];
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        na < k + 2u * P_COUNT + 32u)
        return 1;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->engine = (uint8_t)eng_uid(a[1]);                 /* (the wire: a slot; the record: the UID) */
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        v[i] = (int16_t)((int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192);
    up_vals_put(r, v);                                  /* (values not yet clamped: editor.c up_values) */
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    return 0;
}

#ifndef UP_HOST
#include "../core/favorites.c"                    /* (fav_user_drop: an erased slot loses its mark; once only) */
static const param_desc_t *up_desc(uint32_t uid, uint32_t i)   /* (by the record's engine UID) */
{
#if P_TAIL
    if (i > P_E7)
        return &TP[i];                                  /* (SLOOP 2.4's track values, the COMP insert's) */
#endif
    return i >= P_E0 ? &ENGINES[eng_slot(uid)]->edit[i - P_E0] : &TP[i];
}

#if FELUCCA_ENG_FM6
static int16_t up_old_fm6[3];                           /* MOD, M.TIM, C.TIM of a record from before 2026-10-18 (up_load bakes them) */
#endif
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
#if FELUCCA_ENG_FM6
    if (r->engine == ENG_UID_FM6) {
        for (i = 0; i < 3u; i++)
            up_old_fm6[i] = v[P_E0 + 1u + i];
        v[P_E0] = fm6_voice_load(v[P_E0]);              /* (stored: INIT, or a pack by its fixed number; R / U of before: INIT) */
    }
#endif
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], up_desc(r->engine, i)->min, up_desc(r->engine, i)->max);
}

#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
/* FELUCCA_UP_FM6: an FM6 user preset keeps its voice. The preset record holds only the VOICE number, and loading
 * it reset the part's buffer to that voice: operator edits not stored into the bank were lost. Here a store of an
 * FM6 track also keeps its buffer (the packed DX7 voice, 128 bytes of 7 bits in 112) in OBJ_UPFM6 (storage.c,
 * 0x95000 / 0x96000; at 0xE7000 / 0xE8000 before fix/upfm6-off-vm: moved once at start, st_upf_move), and a load
 * puts it back. Idea from Felucca 1.0.3 (up_fm6.c, hugelton/Felucca b22a24b, by Leo
 * Kuroshita, GPL-3.0-only), written for Melodee's FM6 (eng_fm6.c fm6_ed / fm6_pack, fm6_store.c). The object is
 * built and read in storage.c's st_buf (no RAM of its own); a record written by the editor (UP_PUT) or erased
 * drops the slot's voice (it no longer matches); a preset without one loads its VOICE as before. */
#define UPF_MAGIC 0x36465055u                    /* "UPF6" */
#define UPF_VB 112u                              /* a packed voice, 7 bits a byte */
typedef struct {
    uint32_t magic, used;                        /* bit k: slot k has its voice */
    uint8_t v[UP_SLOTS][UPF_VB];
} upf_t;
_Static_assert(sizeof(upf_t) <= ST_PAYLOAD_MAX && UP_SLOTS <= 32u, "OBJ_UPFM6 fits one storage object");
#define UPF ((upf_t *)(void *)st_buf)
static uint32_t upf_used;                        /* RAM copy of the mask (up_boot, every write) */
static uint8_t upf_hold;                         /* up_store of an FM6 sound: up_put leaves the voice to it */
static int upf_read(void)                        /* the object -> st_buf: valid 1 */
{
    return flash_ok && st_load(OBJ_UPFM6, st_buf, sizeof(upf_t)) == (int)sizeof(upf_t) && UPF->magic == UPF_MAGIC;
}
static void upf_set(uint32_t k, const int16_t *ed)   /* slot k's voice = ed (0: none), then to flash */
{
    uint8_t b[128];
    uint32_t i, j, acc = 0, n = 0;
    if (!flash_ok || k >= UP_SLOTS || (!ed && (upf_hold || !((upf_used >> k) & 1u))))
        return;
    if (!upf_read()) {
        memset(st_buf, 0, sizeof(upf_t));
        UPF->magic = UPF_MAGIC;
    }
    if (ed) {
        fm6_pack(b, ed);
        for (i = j = 0; i < 128u; i++) {         /* 8 x 7 bits -> 7 bytes */
            acc |= (uint32_t)(b[i] & 127u) << n;
            for (n += 7u; n >= 8u; n -= 8u, acc >>= 8)
                UPF->v[k][j++] = (uint8_t)acc;
        }
        UPF->used |= 1u << k;
    } else {
        UPF->used &= ~(1u << k);
    }
    upf_used = st_save(OBJ_UPFM6, st_buf, sizeof(upf_t)) ? upf_used & ~(1u << k) : UPF->used;
}
static int upf_get(uint32_t k, int16_t *ed)      /* slot k's voice -> ed: 1, or none: 0 */
{
    uint8_t b[128];
    uint32_t i, j, acc = 0, n = 0;
    if (k >= UP_SLOTS || !((upf_used >> k) & 1u) || !upf_read() || !((UPF->used >> k) & 1u))
        return 0;
    for (i = j = 0; i < 128u; i++) {
        while (n < 7u) {
            acc |= (uint32_t)UPF->v[k][j++] << n;
            n += 8u;
        }
        b[i] = (uint8_t)(acc & 127u);
        acc >>= 7;
        n -= 7u;
    }
    fm6_unpack(ed, b);
    return 1;
}
#define UPF_BOOT() (flash_ok ? (void)st_upf_move(UPF_MAGIC) : (void)0, upf_used = upf_read() ? UPF->used : 0u)
#else
#define upf_set(k, ed) ((void)0)
#define UPF_BOOT() ((void)0)
#endif

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
#if UP_XIP
        up_bank_find(b);
#else
        up_bank_check(b, st_buf, flash_ok ? st_load(OBJ_UPRESET0 + b, st_buf, sizeof up_bank[b]) : -1);
#endif
    UPF_BOOT();
#endif
#if FELUCCA_FAVORITES
    {   /* a mark of a slot that is empty (erased by another firmware, or a record that does not read) goes */
        uint32_t k;
        for (k = 0; k < UP_SLOTS; k++)
            if (!up_used(k))
                fav_user_drop(k);
    }
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM; UP_XIP: 2, nothing kept).
 * A bank of another format in flash is written over as an empty UPB4 bank with this record */
static int up_put(uint32_t k, const up_rec_t *r)
{
#if FELUCCA_SD_UI
    if (k == UP_CARD) {                                 /* (the card's: RAM only, sd_mode.c writes the file) */
        if (r)
            up_card = *r;
        return 0;
    }
#endif
#if UP_XIP
    uint32_t b = k / UP_PER_BANK;
    up_bank_t *bk = (up_bank_t *)(void *)st_buf;        /* (built here after upf_set, which reads its object into st_buf) */
#else
    up_bank_t *bk = &up_bank[k / UP_PER_BANK];
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#endif
    if (!r) {
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
#if FELUCCA_FAVORITES
        fav_user_drop(k);                               /* (an erased preset keeps no mark) */
#endif
    }
    up_gen++;
    upf_set(k, 0);                                      /* (UP_FM6: a voice kept for the old record goes) */
#if UP_XIP
    if (!flash_ok)
        return 2;
    if (up_xip[b])                                      /* the bank as it is in flash, in st_buf, the record changed */
        memcpy(bk, up_xip[b], sizeof *bk);
    else
        memset(bk, 0, sizeof *bk);
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        bk->r[k % UP_PER_BANK] = *r;
    else
        memset(&bk->r[k % UP_PER_BANK], 0, sizeof(up_rec_t));
    if (st_save(OBJ_UPRESET0 + b, st_buf, sizeof *bk))  /* (storage.c: a source in st_buf, ST_SRC_BUF) */
        return 2;
    up_bank_find(b);                                    /* (the copy written) */
    return up_xip[b] ? 0 : 2;
#elif FELUCCA_FLASH
    if (flash_ok)
        return st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk) ? 2 : 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the selected part's sound -> slot k; name 0 or "": engine name + slot number ("ANALOG 07").
 * 1 = the drum track is selected (it has no sound to store) */
static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    if (is_drum(TSEL))
        return 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.engine = (uint8_t)eng_uid(TSEL->eng_req % NENGINES);   /* (a UID) */
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        char b[16], l[4];
        str_cpy(b, ENGINES[TSEL->eng_req]->name, 9);
        up_slot_label(l, k);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), l + 1, 3);
        for (i = 0; i < 12u && b[i]; i++)
            r.name[i] = b[i];
    }
    up_vals_put(&r, TSEL->p);
    up_pat_from(&r, TSEL->step);
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
    {
        int rc;
        upf_hold = r.engine == ENG_UID_FM6;
        rc = up_put(k, &r);
        upf_hold = 0;
        if (r.engine == ENG_UID_FM6)                    /* the part's voice as it is, edits and all */
            upf_set(k, rc ? (const int16_t *)0 : fm6_ed[song.sel % NPART]);
        return rc;
    }
#else
    return up_put(k, &r);
#endif
}

/* slot k -> the selected part's sound: engine and every parameter except its mix (LEVEL,
 * PAN, MUTE: the TRACKS faders) and its pattern parameters (param_kept). LIVE: the pattern
 * stored in the record is not loaded: changing the sound never changes the sequence.
 * 0 ok, 1 empty (or the drum track is selected), or its engine is left out of this build (kept, not loaded:
 * "MISSING: PHYS") */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k) || is_drum(t))
        return 1;
    if (!eng_built(up_rec(k)->engine)) {
        ui_say("MISSING: ", ENG_UID_NAME[up_rec(k)->engine % ENG_UID_N]);
        return 1;
    }
    r = up_rec(k);
    up_values(r, v);
    for (i = 0; i < P_COUNT; i++)                       /* (LEN etc. of a kept pattern changed too, and */
        if (param_kept(i))                              /* a preset pattern then counted as edited) */
            v[i] = t->p[i];
    if ((uint32_t)(t - trk) < NPART && part_mem_reserve((uint32_t)(t - trk), eng_slot_built(r->engine))) {
        ui_say("MEM FULL", "");                         /* no room in the arena: refused (part_mem_refused: what to drop) */
        return 1;
    }
    sound_change(t);                                    /* (SLOOP 2.6: what sounds fades out with the old sound, no click) */
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = (uint8_t)eng_slot_built(r->engine);
#if PART_DROPS
    t->silent = 0;                                      /* (a sound loaded: the placeholder plays again) */
#endif
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
    t->preset = 0;
#if FELUCCA_UP_FM6 && FELUCCA_ENG_FM6 && FELUCCA_FLASH
    if (r->engine == ENG_UID_FM6 && upf_get(k, fm6_ed[song.sel % NPART])) {   /* its own voice (UP_FM6): kept as the */
        fm6_bake_old(fm6_ed[song.sel % NPART], up_old_fm6[0], up_old_fm6[1], up_old_fm6[2]);   /* (MOD ..: in the voice) */
        fm6_cur[song.sel % NPART] = (int16_t)(t->p[P_E0] + 1);   /* VOICE's, no reload */
    } else
#endif
    fm6_cur[song.sel % NPART] = 0;                      /* FM6: the VOICE it names, afresh */
    fm1_irq_on();
    t->user = (uint8_t)(k + 1u);
    sync_reload = 1;
    ui.force = 1;
    MISS_BUMP();                                        /* (its set, its sends: miss.c) */
    return 0;
}

#if FELUCCA_SD_UI
/* SD card mode (storage/sd/sd_mode.c): a preset file's record. up_card_take: the selected part's sound as a record named
 * name (12 characters at most) -> out (sizeof(up_rec_t)): its size, 0 the drum track. up_card_give: a record (n bytes)
 * into the selected part, as a user preset's LOAD: 0 loaded, 1 refused (said: MISSING, MEM FULL, the drum track), -1 not
 * a record of this format. An FM6 sound keeps its VOICE number (the operator edits are the flash slots' UP_FM6 only) */
static uint32_t up_card_take(uint8_t *out, const char *name)
{
    if (up_store(UP_CARD, name))
        return 0;
    memcpy(out, &up_card, sizeof up_card);
    return sizeof up_card;
}
static int up_card_give(const uint8_t *in, uint32_t n)
{
    int rc;
    if (n != sizeof up_card)
        return -1;
    memcpy(&up_card, in, sizeof up_card);
    if (!up_valid(&up_card))
        return -1;
    rc = up_load(UP_CARD);
    if (!rc)
        TSEL->user = 0;                                 /* (no flash slot plays: SAVE asks for a free one) */
    return rc;
}
#endif

static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar */
static void up_ui(uint32_t op, uint32_t k)     /* 0 load, 1 erase, 2 save */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if (op != 1u && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return;
    }
    if (op < 2u && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op == 0u) {
        if (!up_load(k))                               /* (its engine left out: up_load says MISSING) */
            ui_say("LOADED ", l);
        return;
    }
    if (SAVE_WAITS_PLAY()) {                           /* a flash erase stops the audio ~50 ms (seq.c: LIVE_SAVE) */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    rc = op == 1u ? up_put(k, 0) : up_store(k, 0);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
#endif
