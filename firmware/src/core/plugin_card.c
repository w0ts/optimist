/* SPDX-License-Identifier: GPL-3.0-only */
/* A plugin from the SD card into the PLUGINS region (core/plugin.c, docs/SD-MODE-DESIGN.md section 10): the file
 * Optimist/Plugins/<NAME>.opl, NAME the engine's name as the lists show it (core/registry.h ENGINE_LIST; the card
 * zip writes one per engine: tools/sd_zip.py). Included after the card (sdcard.c sd_open / sd_seek / sd_read: the
 * one open file). A project that names an engine neither built in nor placed asks for it here (project.c
 * proj_apply); the card not ready, no such file, or a file for another build: the project plays the fallback as
 * before. */
#if FELUCCA_PLUGINS && FELUCCA_SDCARD
#define PLUG_CARD_DIR SDL_ROOT "/Plugins"

static int plug_card_rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    uint32_t got = 0;
    (void)ctx;
    if (sd_seek(off) != FR_OK || sd_read(dst, n, &got) != FR_OK || got != n)
        return -1;
    return 0;
}

/* the plugin of this UID from the card: its slot, or PLUG_E_* (PLUG_E_READ: no card, no file) */
static int plug_card_install(uint32_t uid)
{
    char path[sizeof PLUG_CARD_DIR + 20];
    const char *name;
    uint32_t i, n = 0;
    int slot;
    if (uid >= ENG_UID_N)
        return PLUG_E_BAD;
    if (plug_slot_of(uid) != 0xFFu)
        return (int)plug_slot_of(uid);
    name = ENG_UID_NAME[uid];
    for (i = 0; i < sizeof PLUG_CARD_DIR - 1u; i++)
        path[n++] = PLUG_CARD_DIR[i];
    path[n++] = '/';
    for (i = 0; name[i] && i < 12u; i++)
        path[n++] = name[i];
    for (i = 0; i < 5u; i++)
        path[n++] = ".opl"[i];                       /* (its 0 too) */
    if (sd_open(path) != FR_OK)
        return PLUG_E_READ;
    slot = plug_install(plug_card_rd, 0);
    if (slot >= 0 && plug_uid[slot] != uid) {       /* a file holding another engine under this name: not kept */
        (void)plug_forget((uint32_t)slot);
        return PLUG_E_BAD;
    }
    return slot;
}
#endif
