/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store's int8 check (docs/UI-OPTIMIST-DESIGN.md 6.2): an event's value is a signed byte, so every
 * parameter an event may set (motion's set, the locks' set) must have its range inside -128..127, in TP[] and in
 * every engine's edit descriptors (and the mode-dependent ones, desc()). Prints each id whose range does not fit,
 * with whether it is recordable (motion) or lockable; exit status: the number of such ids that are recordable or
 * lockable. Built with every switch that adds values (run_tests.sh: ANALOG 2, the track FILTER, CHORD+, COMP). */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fits(const param_desc_t *d) { return d && d->min >= -128 && d->max <= 127; }

int main(void)
{
    uint32_t id, e, k, bad = 0, seen = 0;
    track_t t;
    host_tracks_init();
    for (id = 0; id < P_COUNT; id++) {
        int lockable = p_lockable(id), rec_part = motion_param(&trk[0], id), rec_drum = motion_param(&trk[TRK_DRUM], id);
        if (id >= P_E0 && id <= P_E7) {
            for (e = 0; e < NENGINES; e++) {
                const engine_t *en = ENGINES[e];
                const param_desc_t *d = &en->edit[id - P_E0];
                t = trk[0];
                t.engine = t.eng_req = (uint8_t)e;
                for (k = 0; k < 2u; k++) {
                    if (k && en->desc)
                        d = en->desc(&t, id - P_E0);
                    if (!d || (k && !en->desc))
                        continue;
                    seen++;
                    if (!fits(d) && (lockable || rec_part)) {
                        printf("E%u engine %-8s %-8s %6d..%-6d lockable %d motion %d\n", (unsigned)(id - P_E0),
                               en->name ? en->name : "?", d->label ? d->label : "-", d->min, d->max, lockable, rec_part);
                        bad++;
                    }
                }
            }
            if (!fits(&DRUM_KIT_DESC) && rec_drum)
                printf("drum kit %d..%d motion\n", DRUM_KIT_DESC.min, DRUM_KIT_DESC.max), bad++;
            continue;
        }
        seen++;
        if (!fits(&TP[id]) && (lockable || rec_part || rec_drum)) {
            printf("id %3u %-8s %6d..%-6d lockable %d motion %d\n", (unsigned)id, TP[id].label ? TP[id].label : "-",
                   TP[id].min, TP[id].max, lockable, rec_part || rec_drum);
            bad++;
        } else if (!fits(&TP[id])) {
            printf("id %3u %-8s %6d..%-6d (neither lockable nor recordable)\n", (unsigned)id,
                   TP[id].label ? TP[id].label : "-", TP[id].min, TP[id].max);
        }
    }
    printf("int8 check: %u descriptors, %u lockable / recordable ranges outside -128..127\n", (unsigned)seen,
           (unsigned)bad);
    return (int)bad;
}
