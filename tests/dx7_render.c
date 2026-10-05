/* SPDX-License-Identifier: GPL-3.0-only */
/* Renders one note of a DX7 voice through firmware/src/dx7_core.c on the host, as raw int32 samples
 * (the voice's own Q24 output), for the comparison with msfa (tools/dx7_ref_compare.sh) and for WAVs.
 *   dx7_render VOICE NOTE VEL BLOCKS KEYUP_BLOCK [voice.bin]
 *   dx7_render dump DIR      (the factory voices as DIR/vNN.bin, 155 bytes each: the DX7 part)
 * VOICE: a factory voice index; with voice.bin: a 128- (packed), 155- or 200-byte voice file instead. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/dx7_core.c"
#include "../firmware/src/dx7_voices.c"

int main(int argc, char **argv)
{
    static dx7_note_t n;
    static dx7_lfo_t lfo;
    uint8_t v[DX7_VSIZE];
    int32_t buf[DX7_N];
    int voice, note, vel, blocks, keyup, b;
    if (argc == 3 && !strcmp(argv[1], "dump")) {
        size_t i;
        for (i = 0; i < DX7_NFACTORY; i++) {
            char p[512];
            FILE *f;
            snprintf(p, sizeof p, "%s/v%02u.bin", argv[2], (unsigned)i);
            if (!(f = fopen(p, "wb")))
                return 1;
            fwrite(DX7_FACTORY[i], 1, DX7_VCED, f);
            fclose(f);
        }
        printf("%u voices\n", (unsigned)DX7_NFACTORY);
        return 0;
    }
    if (argc < 6) {
        fprintf(stderr, "usage: dx7_render VOICE NOTE VEL BLOCKS KEYUP_BLOCK [voice.bin]\n");
        return 2;
    }
    voice = atoi(argv[1]);
    note = atoi(argv[2]);
    vel = atoi(argv[3]);
    blocks = atoi(argv[4]);
    keyup = atoi(argv[5]);
    if (argc > 6) {
        FILE *f = fopen(argv[6], "rb");
        size_t got;
        if (!f)
            return 1;
        memset(v, 0, sizeof v);
        got = fread(v, 1, sizeof v, f);
        fclose(f);
        if (got == DX7_PACKED) {                        /* a bank voice */
            uint8_t b[DX7_PACKED];
            memcpy(b, v, sizeof b);
            dx7_unpack(v, b);
        } else if (got == DX7_VCED) {
            dx7_ext_default(v);
        }
        dx7_voice_clamp(v);
    } else {
        if (voice < 0 || (size_t)voice >= DX7_NFACTORY)
            return 1;
        memcpy(v, DX7_FACTORY[voice], sizeof v);
    }
    dx7_lfo_reset(&lfo, v);
    dx7_lfo_keydown(&lfo);
    dx7_note_init(&n, v, note + v[DX7_TRNSP] - 24, vel, 0);
    for (b = 0; b < blocks; b++) {
        int32_t l = dx7_lfo_tick(&lfo), d = dx7_lfo_delay(&lfo);
        if (b == keyup)
            dx7_note_keyup(&n);
        dx7_note_compute(&n, v, buf, l, d, 0);
        fwrite(buf, sizeof buf[0], DX7_N, stdout);
    }
    return 0;
}
