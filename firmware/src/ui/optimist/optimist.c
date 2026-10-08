/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI (FELUCCA_UI=1), phases 1, 2 and 4 of docs/UI-OPTIMIST-DESIGN.md: the skeleton, STEP, the
 * layers, TEMPO and SONG. Every screen
 * is a list of rows, a row up to four cells; the cursor row's cells are the four cards and KNOB 1..4 edit them.
 * SELECT is the cursor, ALGORITHM the track, PRESETS the hot cell's value one unit a detent; SAVE tapped is YES,
 * HOME tapped is NO. The screens: HOME (the mixer), SOUND (the track's pages as rows; a page button: its family's),
 * FX (the global effects), PROJECT, SYSTEM, STEP (the keys as the 16 steps of a window, the drum grid, the roll),
 * SONG (scenes, patterns, the chain), TEMPO (PLAY held), the DRUM MIXER (the 16 sounds as strips), SCOPE (the
 * master's or a track's wave). A page button held is a performance layer (its map).
 * Drawn with the Terminus font in Felucca 1.0's structure (the header 0..24, the cards 28..72, the panel 76..239,
 * no footer; the mixers' strips 27..239); the Felucca look is phase 5. Main loop only: nothing here runs in the audio
 * interrupt (the SCOPE's tap is in fx.c).
 *
 * The entry points the core calls are SLOOP's names (system/main.c, project.c, editor.c): ui_input, ui_leds,
 * ui_draw, go_home, layers_init, panel_setup, track_select, ui_say, ui_message, ui_say_st, and the fields of `ui`
 * the core reads (force, menu, page; miss.c: msg, msg_t, layer, hold_kind). The model operations come from
 * core/model.c and drums/dsnd_desc.c, shared with SLOOP's UI.
 *
 * Files, in this order: op_state.c (the state, messages, the confirm), op_cells.c (cells, the rows of PAGES),
 * op_screens.c (HOME, SOUND, FX), op_dmix.c (the DRUM MIXER's rows), op_step.c (STEP), op_layers.c (the held layers, the lock), op_tempo.c (TEMPO),
 * op_song.c (SONG), op_combos.c (SAVE / HOME + a button), op_project.c (PROJECT, SYSTEM, the screen table),
 * op_graph.c (the forms of values, the SOUND graphs, the modal), op_preset.c (a preset named with its engine),
 * op_name.c (NAME: a user preset or a project named on the device), op_draw.c (the renderer, the mixer's strips),
 * op_dmixdraw.c (the drum mixer's strips), op_scope.c (SCOPE, the mixer's master column), op_stepdraw.c (STEP's
 * grid and roll), op_laydraw.c (a layer's map, the TEMPO and session-grid pictures), op_input.c (the panel, the
 * entry points). After op_cells.c, op_fm6.c (FM6's operator editor: ENV held, the SOUND family) and after
 * op_laydraw.c its drawing, op_fm6draw.c. */
#include "../sloop/ui_colors.c"         /* the colour language (engine, kit kind): shared with SLOOP's UI */
#if FELUCCA_BRIGHT
#include "../sloop/bright.c"            /* the backlight level (main.c reads BL_DUTY, project.c bright_boot) */
#endif
#include "op_state.c"
#if FELUCCA_MISSING_WARN
#include "../../storage/miss.c"         /* "MISSING: PHYS T2" after a load (cur_page, ui.msg: op_state.c) */
#endif
#include "op_cells.c"
#include "op_fm6.c"
#include "op_screens.c"
#include "op_dmix.c"
#include "op_step.c"
#include "op_layers.c"
#include "op_tempo.c"
#include "op_song.c"
#include "op_combos.c"
#include "op_project.c"
#include "op_graph.c"
#include "op_preset.c"
#include "op_name.c"
#include "op_draw.c"
#include "op_dmixdraw.c"
#include "op_scope.c"
#include "op_stepdraw.c"
#include "op_laydraw.c"
#include "op_fm6draw.c"
#include "op_input.c"
