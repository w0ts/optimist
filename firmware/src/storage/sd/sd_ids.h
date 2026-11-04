/* SPDX-License-Identifier: GPL-3.0-only */
/* The card's presets' and kits' ids (the user, 2026-11-04: "like samples, kits and presets should have a unique id so
 * that if they are moved a project finds them back"; "the ids should be the same for the factory presets for all
 * devices"). Every .ops / .opk file carries one in its header (sd_mode.c sdm_fh, version 2); the sample index holds it
 * for those files as a sample's hash (sd_index.c, SDX_F_ID), so a project finds a preset or kit moved, renamed, or put
 * from Factory/ into User/ by it. Needs sd_hash.c (XXH64).
 *   A FACTORY id (bit 63 clear) is a pure function of the sound (tools/sd_factory.c, on any machine, every zip): XXH64
 *   of "OPID", the kind ('P' a preset, 'K' a kit), the engine's UID (registry.h, stable across builds) and the preset's
 *   name with its occurrence in the engine's list when the name repeats ("#2" ...), or the kit's UID. Its bank slot,
 *   a device, a time, a path never enter it. What changes it: the preset's name or its engine's UID (a kit: its UID),
 *   nothing else (its values retuned keep it).
 *   A USER id (bit 63 set) is made at a preset's or kit's first save on an FM-1 (sd_mode.c sdm_new_id): 31 bits of the
 *   XXH64 of the FM-1's flash part's factory unique ID (system/devname.c dev_uid: one a chip, so two FM-1s never share
 *   them), then a 16-bit count the device record keeps (sdm_rec.ids: one a save as), then 16 bits of the milliseconds
 *   since power-on (a count reset with the device record still differs). Saved over by its name: kept. */
#ifndef SD_IDS_H
#define SD_IDS_H
#define SDI_USER 0x8000000000000000ull

static uint64_t sdi_mix(char kind, uint32_t uid, const char *name, uint32_t nth)
{
    sdh_t s;
    uint8_t h[9] = {'O', 'P', 'I', 'D', (uint8_t)kind, (uint8_t)uid, (uint8_t)(uid >> 8), (uint8_t)(uid >> 16),
                    (uint8_t)(uid >> 24)};
    uint32_t n = 0;
    sdh_init(&s);
    sdh_add(&s, h, sizeof h);
    while (name && name[n])
        n++;
    if (name)
        sdh_add(&s, name, n);
    if (nth > 1u) {
        uint8_t t[5] = {'#', (uint8_t)nth, (uint8_t)(nth >> 8), (uint8_t)(nth >> 16), (uint8_t)(nth >> 24)};
        sdh_add(&s, t, sizeof t);
    }
    return sdh_end(&s) & ~SDI_USER;
}
/* engine UID uid's preset named name, the nth of that name in its list (1 the first): its factory id */
static uint64_t sdi_factory_preset(uint32_t uid, const char *name, uint32_t nth) { return sdi_mix('P', uid, name, nth); }
static uint64_t sdi_factory_kit(uint32_t kit_uid) { return sdi_mix('K', kit_uid, 0, 1); }
#endif
