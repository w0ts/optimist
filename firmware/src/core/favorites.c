/* SPDX-License-Identifier: GPL-3.0-only */
/* Favorites (FELUCCA_FAVORITES): marked sounds, after Felucca 1.6's favorites (hugelton/Felucca, GPL-3.0-only, favorites.c).
 * Included by core/model.c (the list) and storage/persist_rec.c (the settings record); the host tests include either.
 *
 * Felucca keeps a bit per preset of 16 engines (512 B) in its settings. Here a factory sound is marked by a 16-bit mark
 * of its engine's UID and its name (a sound's place in this build's list moves with the engines and kits a build leaves
 * out; its UID and name do not), up to FAV_N of them, packed from 0; a user preset by its slot, one bit of 32. 136 B in
 * all, the last field of the settings record (persist_rec.c). A mark of a sound this build leaves out stays as saved
 * (it is no place in the list; it comes back with the engine). The engines' own collections (nbank.c: FM6 F01.., CZ
 * Z01..) are not marked: their sounds are slots whose contents the player edits.
 * A mark is nonzero; 0 is an empty place. Two sounds sharing a mark (1 in 65536 a pair) would both read as marked. */
#ifndef FAV_C
#define FAV_C
#if FELUCCA_FAVORITES
#ifndef FAV_SAVE_LATER
#define FAV_SAVE_LATER() ((void)0)     /* (model.c: settings_later = 1; a host test with no panel: nothing) */
#endif
#define FAV_N 64u
#define FAV_MARK 0x31564146u                 /* "FAV1": the record's tail is a store (a record of another build's layout: its bytes here are not) */
typedef struct {
    uint32_t mark;                           /* FAV_MARK once saved */
    uint16_t h[FAV_N];                       /* factory marks, packed from 0 (0 = empty) */
    uint32_t user;                           /* bit k: user preset slot k */
} fav_t;
_Static_assert(UP_SLOTS <= 32u, "favorites: the user marks are one 32-bit word");
static fav_t fav IN_POOL;
static uint8_t fav_list;                     /* PRESETS > LIST: 0 ALL, 1 FAV (not kept over a power cycle) */

static uint16_t fav_hash(uint32_t uid, const char *name)
{
    uint32_t h = 2166136261u ^ uid;
    h *= 16777619u;
    for (; *name; name++) {
        h ^= (uint8_t)*name;
        h *= 16777619u;
    }
    h ^= h >> 16;
    return (uint16_t)((h & 0xFFFFu) ? h : 1u);
}
static uint32_t fav_find(uint16_t m)         /* the place of mark m, FAV_N: none */
{
    uint32_t i;
    for (i = 0; i < FAV_N && fav.h[i]; i++)
        if (fav.h[i] == m)
            return i;
    return FAV_N;
}
static uint32_t fav_count(void)
{
    uint32_t i = 0;
    while (i < FAV_N && fav.h[i])
        i++;
    return i;
}
static int fav_has(uint32_t uid, const char *name) { return fav_find(fav_hash(uid, name)) < FAV_N; }
/* mark (on) or clear a factory sound: 1 changed, 0 as it was, -1 FULL (FAV_N marks; nothing changed) */
static int fav_set(uint32_t uid, const char *name, int on)
{
    uint16_t m = fav_hash(uid, name);
    uint32_t i = fav_find(m), n;
    if ((i < FAV_N) == !!on)
        return 0;
    if (on) {
        n = fav_count();
        if (n >= FAV_N)
            return -1;
        fav.h[n] = m;
        return 1;
    }
    for (; i + 1u < FAV_N; i++)              /* (packed: the later ones close the gap) */
        fav.h[i] = fav.h[i + 1u];
    fav.h[FAV_N - 1u] = 0;
    return 1;
}
static int fav_user_has(uint32_t k) { return k < UP_SLOTS && ((fav.user >> k) & 1u); }
static int fav_user_set(uint32_t k, int on)  /* 1 changed, 0 as it was or no such slot */
{
    if (k >= UP_SLOTS || fav_user_has(k) == !!on)
        return 0;
    fav.user = on ? fav.user | 1u << k : fav.user & ~(1u << k);
    return 1;
}
static void fav_user_drop(uint32_t k)        /* (a user preset erased: its mark goes with it, saved with the settings) */
{
    if (fav_user_set(k, 0))
        FAV_SAVE_LATER();
}

/* a store read from flash: packed from 0, no mark twice (anything else a damaged record could hold) */
static void fav_tidy(fav_t *f)
{
    uint16_t o[FAV_N];
    uint32_t i, j, n = 0;
    for (i = 0; i < FAV_N; i++) {
        for (j = 0; j < n && o[j] != f->h[i]; j++)
            ;
        if (f->h[i] && j == n)
            o[n++] = f->h[i];
    }
    for (i = 0; i < FAV_N; i++)
        f->h[i] = i < n ? o[i] : 0u;
#if UP_SLOTS < 32u
    f->user &= (1u << UP_SLOTS) - 1u;
#endif
}
#endif /* FELUCCA_FAVORITES */
#endif /* FAV_C */
