/* The FM-1's name to other devices, "FM1-XXXX" (firmware/src/system/devname.h, ruling 2026-10-29): the digits' sources in
 * their order (the kept BLE address, then the flash ID's hash, then "0000"), the name's format, and the USB strings a host
 * reads: the product (string 2: the MIDI device and its ports), with USB audio "... Out" (3) and "... In" (4); the
 * manufacturer (1) is the maker's and does not change. The hash is checked against values computed apart from the code
 * (FNV-1a over the 16 octets, h ^= h >> 16): fm1-base's tests/dev_name_test.c has the same ones, so the two builds name an
 * FM-1 alike. Run by tests/run_tests.sh. Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static int usb_str_is(uint32_t idx, const char *want)       /* string descriptor idx = want, UTF-16LE of ASCII */
{
    const uint8_t *d;
    uint16_t l;
    uint32_t i, n = (uint32_t)strlen(want);
    if (!get_desc(0x0300u | idx, &d, &l) || l != 2u + 2u * n || d[0] != l || d[1] != 3)
        return 0;
    for (i = 0; i < n; i++)
        if (d[2 + 2 * i] != (uint8_t)want[i] || d[3 + 2 * i] != 0)
            return 0;
    return 1;
}

int main(void)
{
    static const uint8_t SEQ[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    static const uint8_t HOST[16] = {0x46, 0x4D, 0x31, 0x2D, 0x68, 0x6F, 0x73, 0x74};
    static const uint8_t FF[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t ZERO[16] = {0};
    /* a kept address 6B:3A:C4:A1:B2 .. as the record keeps it: octets 0..5 least significant first, [6] the mark */
    static const uint8_t KEPT[8] = {0xB2, 0xA1, 0xC4, 0x3A, 0x6B, 0xDF, DEV_ADDR_KEPT, 0};
    static const uint8_t NOMARK[8] = {0xB2, 0xA1, 0xC4, 0x3A, 0x6B, 0xDF, 0, 0};
    static const uint8_t NOTRND[8] = {0xB2, 0xA1, 0xC4, 0x3A, 0x6B, 0x1F, DEV_ADDR_KEPT, 0};   /* (top bits not 11: not an address of ours) */
    char n[DEV_NAME_LEN + 1u];

    check(dev_uid_hash16(SEQ) == 0x2FBB && dev_uid_hash16(HOST) == 0x3B9C, "the flash ID's hash: values computed apart");
    check(dev_uid_hash16(FF) == -1 && dev_uid_hash16(ZERO) == -1 && dev_uid_hash16(0) == -1, "no ID (all FF, all 0, none): -1");
    check(dev_id_pick(KEPT, 0x2FBB) == 0xA1B2, "the kept BLE address's octets 1 and 0 win: A1B2");
    check(dev_id_pick(NOMARK, 0x2FBB) == 0x2FBB && dev_id_pick(NOTRND, 0x2FBB) == 0x2FBB && dev_id_pick(0, 0x2FBB) == 0x2FBB,
          "no mark / not a random static address / none kept: the ID's");
    check(dev_id_pick(0, -1) == -1, "neither: -1");
    dev_name_fmt(n, 0xA1B2);
    check(!strcmp(n, "FM1-A1B2") && strlen(n) == DEV_NAME_LEN, "the name: FM1-A1B2");
    dev_name_fmt(n, 0x0);
    check(!strcmp(n, "FM1-0000"), "digit 0: FM1-0000");
    dev_name_fmt(n, 0x00AF);
    check(!strcmp(n, "FM1-00AF"), "upper case, four digits, leading zeros kept");
    dev_name_fmt(n, -1);
    check(!strcmp(n, "FM1-0000"), "no id: FM1-0000");

    hostsim_dev_id = 0xA1B2;
    check(usb_str_is(2, "FM1-A1B2"), "USB string 2 (the product, the MIDI device and its ports): FM1-A1B2");
    hostsim_dev_id = 0x2FBB;
    check(usb_str_is(2, "FM1-2FBB"), "another FM-1: FM1-2FBB (read again each time)");
    hostsim_dev_id = -1;
    check(usb_str_is(2, "FM1-0000"), "no ID: FM1-0000");
    {
        const uint8_t *d;
        uint16_t l;
        check(get_desc(0x0301, &d, &l) && l == 42 && d[2] == 'H' && d[4] == 0xFC, "USB string 1 (the manufacturer): the maker's, fixed");
    }
    check(usb_str_is(2, "Optimist") == 0 && usb_str_is(2, "Optimist (Felucca)") == 0, "not the old names");
#if FELUCCA_USB_AUDIO
    hostsim_dev_id = 0xA1B2;
    check(usb_str_is(3, "FM1-A1B2 Out") && usb_str_is(4, "FM1-A1B2 In"), "USB audio: FM1-A1B2 Out, FM1-A1B2 In");
    check(usb_str_is(5, "CH1") && usb_str_is(8, "CH4"), "the capture channels keep CH1..CH4");
#endif
    printf("%s\n", fails ? "devname test FAILED" : "devname test passed");
    return fails;
}
