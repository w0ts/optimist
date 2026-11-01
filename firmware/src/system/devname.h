/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's name to other devices: "FM1-XXXX" (ruling 2026-10-29). XXXX = four upper-case hex digits, the same on USB
 * (usb.c: the MIDI device and its ports, "FM1-XXXX In" / "Out" for the audio), Bluetooth (the advertised name and the GAP
 * Device Name: ble/ble_host.c, the last two octets of the address as an address is written) and wherever a tool reads them.
 *
 * Where the digits come from: the BLE address this FM-1 already keeps (an FM-1 that was "Optimist A1B2" is "FM1-A1B2"),
 * else the hash of the flash part's factory unique ID (FNV-1a over the 16 octets, h ^= h >> 16, the low 16 bits), which is
 * also what fm1-base makes the low two octets of its Bluetooth address of, and what a new BLE address here is given
 * (midi_ble.c ble_midi_addr): the name does not change when Bluetooth starts for the first time. No ID (a part that does not
 * answer, a build without the flash): "FM1-0000".
 *
 * Pure functions, no hardware: tests/devname_test.c. fm1-base's twin: include/plat_uid.h (plat_uid_hash16, plat_dev_name). */
#ifndef DEVNAME_H
#define DEVNAME_H
#include <stdint.h>

#define DEV_NAME_LEN 8u                       /* "FM1-XXXX" */
#define DEV_ADDR_KEPT 0xA5u                   /* the kept BLE address's mark ([6]: midi_ble.c BLE_ADDR_KEPT) */

/* the hash of a unique ID (16 octets) -> 0..0xFFFF; -1: no ID (none, or all the octets alike: all FF or all 0) */
static inline int dev_uid_hash16(const uint8_t *u)
{
    uint32_t h = 2166136261u, i = 1;
    while (u && i < 16u && u[i] == u[0])
        i++;
    if (!u || i >= 16u)
        return -1;
    for (i = 0; i < 16u; i++)
        h = (h ^ u[i]) * 16777619u;
    h ^= h >> 16;
    return (int)(h & 0xFFFFu);
}

/* the digits: the kept BLE address's (its octets 1 and 0; kept = the 8 octets of the settings record, or 0 for none),
 * else uid_hash (dev_uid_hash16); -1 neither */
static inline int dev_id_pick(const uint8_t *kept, int uid_hash)
{
    if (kept && kept[6] == DEV_ADDR_KEPT && (kept[5] >> 6) == 3u)
        return (int)((uint32_t)kept[1] << 8 | kept[0]);
    return uid_hash;
}

/* "FM1-XXXX" into out[9]; id < 0: "FM1-0000" */
static inline void dev_name_fmt(char out[DEV_NAME_LEN + 1u], int id)
{
    static const char HEX[] = "0123456789ABCDEF";
    uint32_t i;
    out[0] = 'F', out[1] = 'M', out[2] = '1', out[3] = '-';
    for (i = 0; i < 4u; i++)
        out[4 + i] = id < 0 ? '0' : HEX[((uint32_t)id >> (12u - 4u * i)) & 15u];
    out[DEV_NAME_LEN] = 0;
}
#endif
