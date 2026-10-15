/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.5 / 2.6's user preset banks (isod89/sloop-fm1 v2.5 fa9ce57, v2.6 d30d655: upreset.c, "UPB1", UP_VER 1
 * records of their P_COUNT, 61 / 71). Golden bank 0 of each made by SLOOP's own up_parse (tests/sl25_gen.c:
 * tests/sl25_upb1.bin, sl26_upb1.bin; slots 0 ANALOG, 1 FM6, 2 PHYS, 3 NOISE, 5 GRAIN), put in flash as SLOOP's storage
 * writes an object. Optimist (upreset.c, storage.c on a simulated NOR; FELUCCA_SL24_SAFE, FELUCCA_ANALOG2):
 *
 *   - reads them: 2.5's (np 61) and 2.6's (np 71) records with 2.5's engine numbers (PHYS 10 ours, NOISE 11 and up
 *     ANALOG at its EDIT defaults), their values 0..49 ours, E0..E7 from their P_E0 (53, 63); 2.6's ANALOG filter TYPE
 *     -> ANALOG 2's FTYP (as a 2.6 project's); np 71 is no P_COUNT SLOOP plus stored (53 57 58 59 69 72);
 *   - saves and erases a slot of such a bank without destroying it: the bank stays "UPB1", every other record byte for
 *     byte as SLOOP wrote it, so SLOOP still reads all of its presets; ours is an UP_VER 2 record SLOOP skips (to it an
 *     empty slot); Optimist reads it back after a restart; SLOOP saving the bank again keeps it;
 *   - a bank of our own (UPB2, or none yet) is written as before.
 * Run by tests/run_tests.sh. */
#define FELUCCA_FLASH 1
#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

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
static uint8_t flash_ok = 1;
static struct { int force; } ui;
static char last_msg[32];
static void ui_message(const char *m) { str_cpy(last_msg, m, sizeof last_msg); }
static void ui_say(const char *a, const char *b) { str_cpy(last_msg, a, sizeof last_msg); (void)b; }
static uint32_t up_gen;
static uint8_t sync_reload;
static int param_kept(uint32_t i) { return i == P_LEVEL || i == P_PAN || i == P_MUTE; }
#include "../firmware/src/storage/upreset.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-112s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define BANK_N 3080u                              /* (SLOOP's up_bank_t: 8 + 16 x 192) */
_Static_assert(sizeof(up_bank2_t) == BANK_N, "a bank as SLOOP's (UPB1: our up_bank2_t)");
/* obj as SLOOP's storage.c writes it (the copy that is not its current one, seq + 1) */
static void sloop_save(uint32_t obj, const void *p, uint32_t len)
{
    st_hdr_t a, b, h;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    uint32_t cur = vb && (!va || b.seq > a.seq) ? 1u : va ? 0u : 2u, c = cur == 0u ? 1u : 0u, off = st_sector(obj, c);
    memset(nor + off, 0xFF, 4096);
    memcpy(nor + off + ST_PAYLOAD_OFF, p, len);
    h.magic = ST_MAGIC, h.type = (uint16_t)obj, h.slot = (uint16_t)c, h.seq = (cur == 2u ? 0u : cur ? b.seq : a.seq) + 1u;
    h.len = len, h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + off, &h, sizeof h);
}
/* the bank SLOOP reads at its start (its storage.c st_current, the newest valid copy) -> b; its length, -1 none */
static int sloop_load(uint32_t obj, uint8_t *b)
{
    st_hdr_t a, h2, h;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &h2) == 0;
    uint32_t c;
    if (!va && !vb)
        return -1;
    c = vb && (!va || h2.seq > a.seq) ? 1u : 0u;
    h = c ? h2 : a;
    memcpy(b, nor + st_sector(obj, c) + ST_PAYLOAD_OFF, h.len);
    return st_crc32(b, h.len) == h.crc ? (int)h.len : -1;
}
/* SLOOP's view of record k of bank b (its upreset.c up_bank_check, up_valid: "UPB1", UP_VER 1, engine < its NENGINES
 * 12, np 8..72, a name) */
static int sloop_valid(const uint8_t *b, int len, uint32_t k)
{
    const up_bank2_t *bk = (const up_bank2_t *)(const void *)b;
    const up_rec2_t *r = &bk->r[k];
    return len == (int)BANK_N && bk->magic == 0x31425055u && bk->rsize == 192u && bk->nslot == 16u &&
           r->used == 0xA5u && r->ver == 1u && r->engine < 12u && r->np >= 8u && r->np <= 72u && r->name[0];
}
/* a record in RAM (today's struct) as the 192 bytes m of a record of before */
static int same2(const up_rec_t *r, const uint8_t *m)
{
    up_rec2_t o;
    up_rec_to2(&o, r);
    return !memcmp(&o, m, sizeof o);
}
static int load(const char *path, uint8_t *b, uint32_t n)
{
    FILE *f = fopen(path, "rb");
    int ok = f && fread(b, 1, n, f) == n && fgetc(f) == EOF;
    if (f)
        fclose(f);
    return ok;
}
static void boot(void)                            /* Optimist starts: the banks from flash */
{
    memset(up_bank, 0, sizeof up_bank);
    up_boot();
}
static int16_t gold_v(uint32_t j, uint32_t i) { return (int16_t)((int32_t)((i * 5u + j) % 90u) - 30); }   /* (sl25_gen.c ubank) */
static int16_t edit_def(uint32_t uid, uint32_t i) { return ENGINES[eng_slot(uid)]->edit[i].def; }
/* golden record j's value i as Optimist plays it on engine uid (clamped to the parameter's range, up_values) */
static int16_t gold_at(uint32_t j, uint32_t i, uint32_t uid)
{
    int32_t g = i >= P_E0 ? 100 + 10 * (int32_t)j + (int32_t)(i - P_E0) : gold_v(j, i);
    return (int16_t)clamp(g, up_desc(uid, i)->min, up_desc(uid, i)->max);
}

/* LFO DEST (2026-10): the golden's four depths -> the slots Optimist reads (lfo_store.h) */
static int16_t slot_of(uint32_t j, uint32_t n)
{
    int16_t d[4], s[4];
    uint32_t i;
    for (i = 0; i < 4u; i++)
        d[i] = (int16_t)gold_v(j, P_LEG_PIT + i);
    (void)lfo_from_depths(d, s);
    return s[n];
}
/* the golden's records as Optimist reads them (np: 61 or 71, P_E0 at e0) */
static void reads(const char *who, uint32_t e0)
{
    int16_t v[P_COUNT];
    char what[160];
    uint32_t i;
    int ok;
    up_values(up_rec(0), v);
    ok = up_used(0) && up_rec(0)->engine == 0u && v[P_FXOFF] == TP[P_FXOFF].def && v[P_A2SWRM] == TP[P_A2SWRM].def;
    for (i = 0; i <= P_CHORD; i++)
        ok &= i >= P_L1DA && i <= P_L1AB ? v[i] == slot_of(0, i - P_L1DA) :
              i == P_ED_FLT || i == P_ED_SHP ? !v[i] : v[i] == gold_at(0, i, 0);   /* (ENV DEST FLT SHP: its slots) */
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == gold_at(0, P_E0 + i, 0);
    {   /* ENV DEST (2026-10-14): the golden's FLT SHP -> ENV's slots (env_store.h) */
        int16_t e[4];
        (void)env1_from_depths(gold_v(0, P_ED_FLT), gold_v(0, P_ED_SHP), e);
        ok &= !memcmp(&v[P_N1DA], e, sizeof e);
    }
    snprintf(what, sizeof what, "%s: ANALOG preset: values 0..49 theirs, FX OFF and ANALOG 2 at defaults, E0..E7 from %u", who,
             (unsigned)e0);
    check(what, ok);
    snprintf(what, sizeof what, "%s: ANALOG's filter TYPE: %s", who, e0 == 63u ? "HP -> ANALOG 2's FTYP HP (3)" : "none (LP, as before)");
    check(what, v[P_A2FTYP] == (e0 == 63u ? 3 : TP[P_A2FTYP].def));
    up_values(up_rec(2), v);
    ok = up_used(1) && up_rec(1)->engine == ENG_UID_FM6 && up_used(2) && up_rec(2)->engine == ENG_UID_PHYS &&
         v[P_E0] == gold_at(2, P_E0, ENG_UID_PHYS) && v[P_E7] == gold_at(2, P_E7, ENG_UID_PHYS) &&
         v[P_REV] == gold_at(2, P_REV, ENG_UID_PHYS);
    snprintf(what, sizeof what, "%s: FM6 (9) stays FM6, PHYS (10) is our PHYS with its E0..", who);
    check(what, ok);
    up_values(up_rec(3), v);
    ok = up_used(3) && up_rec(3)->engine == 0u && v[P_ATK] == gold_at(3, P_ATK, 0);
    for (i = 0; i < 8u; i++)
        ok &= v[P_E0 + i] == edit_def(0, i);
    ok &= v[P_A2FTYP] == TP[P_A2FTYP].def && up_used(5) && up_rec(5)->engine == 8u && !up_used(6) && !up_used(15);
    snprintf(what, sizeof what, "%s: NOISE (11) -> ANALOG at its EDIT defaults; GRAIN as it is; empty slots empty", who);
    check(what, ok);
}

static void round_trip(const char *who, const uint8_t *gold, uint32_t e0)
{
    static uint8_t b[4096], mine[192];
    char what[160];
    int n, ok;
    uint32_t k;
    memset(nor, 0xFF, sizeof nor);
    sloop_save(OBJ_UPRESET0, gold, BANK_N);
    sloop_save(OBJ_UPRESET0, gold, BANK_N);
    boot();
    reads(who, e0);

    TSEL->eng_req = 1;                            /* (a sound of ours into their empty slot 4) */
    for (k = 0; k < P_COUNT; k++)
        TSEL->p[k] = (int16_t)clamp((int32_t)(k % 17u), TP[k].min, TP[k].max);
    ok = up_store(4, "OURS") == 0;
    n = sloop_load(OBJ_UPRESET0, b);
    memcpy(mine, b + 8u + 192u * 4u, sizeof mine);    /* (ours in their bank: an UP_VER 2 record, upreset.c up_vals_put2) */
    ok &= mine[0] == UP_USED && mine[1] == 2u && mine[4] == 'O';
    snprintf(what, sizeof what, "%s: Optimist saves its preset into their empty slot 4: SAVED, the bank stays UPB1", who);
    check(what, ok && n == (int)BANK_N && !memcmp(b, gold, 8));
    for (k = 0, ok = 1; k < 16u; k++)
        if (k != 4u)
            ok &= !memcmp(b + 8u + 192u * k, gold + 8u + 192u * k, 192u);
    snprintf(what, sizeof what, "%s: ... every other record byte for byte as SLOOP wrote it", who);
    check(what, ok);
    ok = sloop_valid(b, n, 0) && sloop_valid(b, n, 1) && sloop_valid(b, n, 2) && sloop_valid(b, n, 3) &&
         sloop_valid(b, n, 5) && !sloop_valid(b, n, 4);
    snprintf(what, sizeof what, "%s: ... SLOOP still reads its five presets; ours (UP_VER 2) is an empty slot to it", who);
    check(what, ok);
    boot();
    snprintf(what, sizeof what, "%s: ... Optimist's next start: its preset read back as saved, theirs as before", who);
    check(what, up_used(4) && same2(up_rec(4), mine) && up_rec(4)->ver == 2u);
    reads(who, e0);

    memcpy(b + 8u + 192u * 6u, gold + 8u, 192u);  /* SLOOP saves (its whole bank, ours in it): a copy of slot 0 in 6 */
    sloop_save(OBJ_UPRESET0, b, BANK_N);
    boot();
    snprintf(what, sizeof what, "%s: SLOOP saves the bank again: Optimist still reads its preset and SLOOP's new one", who);
    check(what, up_used(4) && same2(up_rec(4), mine) && up_used(6) && up_rec(6)->engine == 0u);

    ok = up_put(4, 0) == 0 && up_put(5, 0) == 0;   /* Optimist erases its own, then one of SLOOP's (asked for) */
    n = sloop_load(OBJ_UPRESET0, b);
    ok &= n == (int)BANK_N && !memcmp(b, gold, 8) && !sloop_valid(b, n, 4) && !sloop_valid(b, n, 5);
    for (k = 0; k < 4u; k++)
        ok &= !memcmp(b + 8u + 192u * k, gold + 8u + 192u * k, 192u);
    ok &= !memcmp(b + 8u + 192u * 6u, gold + 8u, 192u);
    snprintf(what, sizeof what, "%s: Optimist erases slot 4 (ours) and 5 (theirs): the rest byte for byte, still UPB1", who);
    check(what, ok);
}

int main(void)
{
    static uint8_t g25[BANK_N], g26[BANK_N], b[4096];
    int ok, n;
    if (!load("tests/sl25_upb1.bin", g25, sizeof g25) || !load("tests/sl26_upb1.bin", g26, sizeof g26)) {
        printf("tests/sl25_upb1.bin, sl26_upb1.bin missing (run from the repo root)\n");
        return 1;
    }
    check("golden: 2.5's bank UPB1, its records np 61; 2.6's np 71", !memcmp(g25, "UPB1", 4) && !memcmp(g26, "UPB1", 4) &&
          g25[8 + 3] == 61u && g26[8 + 3] == 71u && g25[8 + 192 * 4] == 0 && g26[8 + 192 * 5 + 2] == 8u);
    round_trip("2.5", g25, 53u);
    round_trip("2.6", g26, 63u);

    memset(nor, 0xFF, sizeof nor);                /* nothing of SLOOP's: our banks as before */
    boot();
    ok = up_store(3, "MINE") == 0 && up_store(17, "MINE 2") == 0;
    n = sloop_load(OBJ_UPRESET0, b);
    ok &= n == (int)sizeof(up_bank_t) && !memcmp(b, "UPB3", 4) && sloop_load(OBJ_UPRESET0 + 1u, b) == (int)sizeof(up_bank_t) &&
          !memcmp(b, "UPB3", 4);
    boot();
    ok &= up_used(3) && up_used(17) && up_count() == 2u;
    ok &= up_store(5, "MINE 3") == 0 && sloop_load(OBJ_UPRESET0, b) == (int)sizeof(up_bank_t) && !memcmp(b, "UPB3", 4);
    check("no SLOOP bank: our banks written as UPB3 (2026-10-14: every value in its word), and read back", ok);
    printf("%s\n", bad ? "FAILED" : "all ok");
    return bad != 0;
}
