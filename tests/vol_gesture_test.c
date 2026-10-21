/* SPDX-License-Identifier: GPL-3.0-only */
/* SHIFT + the volume knob, the output switch (firmware/src/ui/optimist/op_volgest.h): 15 % of the travel switches once
 * a press, the volume holds while SHIFT is down and, after a switch, until the knob comes back (no jump).
 *   cc -o build/host/vol_gesture_test tests/vol_gesture_test.c && build/host/vol_gesture_test */
#include <stdio.h>
#include "../firmware/src/ui/optimist/op_volgest.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("vol_gesture: %-88s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    struct vgest g = {0};
    int t, held, n;
    int32_t k, k0 = VG_FULL / 2;
    held = vgest_step(&g, 0, k0, &t);
    check("no SHIFT: the volume follows the knob, nothing switched", !held && !t);
    held = vgest_step(&g, 1, k0, &t);
    check("SHIFT down: the volume holds", held && !t);
    for (k = k0, n = 0; k < k0 + VG_STEP - 16; k += 64) {
        vgest_step(&g, 1, k, &t);
        n += t;
    }
    check("SHIFT + a move under 15 %: no switch, still held", n == 0 && g.on);
    for (n = 0; k < k0 + VG_FULL / 3; k += 64) {
        held = vgest_step(&g, 1, k, &t);
        n += t;
    }
    check("SHIFT + 15 % and more: one switch (not one per reading), the volume held throughout", n == 1 && held);
    held = vgest_step(&g, 0, k, &t);
    check("SHIFT let go, the knob left up: the volume still holds (no jump)", held && !t);
    held = vgest_step(&g, 0, k0 + VG_NEAR * 3, &t);
    check("... the knob on its way back, 3 % off: still held", held);
    held = vgest_step(&g, 0, k0 + VG_NEAR / 2, &t);
    check("... back within 1 % of where it was: the volume follows again", !held);
    held = vgest_step(&g, 0, k0 + VG_STEP, &t);
    check("... and then follows freely", !held && !t);
    g = (struct vgest){0};
    vgest_step(&g, 1, k0, &t);
    vgest_step(&g, 1, k0 - VG_STEP - 1, &t);
    check("downwards counts too: SHIFT + 15 % down switches", t == 1);
    held = vgest_step(&g, 0, k0 - VG_STEP - 1, &t);
    held &= vgest_step(&g, 0, k0 - VG_NEAR * 2, &t);
    check("... the takeover from below: held under it", held);
    held = vgest_step(&g, 0, k0 + VG_NEAR * 5, &t);
    check("... a fast turn past where it was (no reading near it): it follows again", !held);
    g = (struct vgest){0};
    vgest_step(&g, 1, k0, &t);
    held = vgest_step(&g, 0, k0, &t);
    check("SHIFT pressed and let go, the knob not moved: the volume follows at once", !held);
    g = (struct vgest){0};
    vgest_step(&g, 1, k0, &t);
    vgest_step(&g, 1, k0 + VG_NEAR * 4, &t);
    held = vgest_step(&g, 0, k0 + VG_NEAR * 4, &t);
    check("SHIFT pressed (an octave), the knob 4 % off but no switch: it follows at once (the user, 2026-10-21)", !held);
    printf("vol_gesture: %d failed\n", fails);
    return fails != 0;
}
