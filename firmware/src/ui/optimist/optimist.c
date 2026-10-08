/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist UI (FELUCCA_UI=1), phase 1 of docs/UI-OPTIMIST-DESIGN.md: the skeleton. Every screen is a list of
 * rows, a row up to four cells; the cursor row's cells are the four cards and KNOB 1..4 edit them. SELECT is the
 * cursor, ALGORITHM the track, PRESETS the hot cell's value one unit a detent; SAVE tapped is YES, HOME tapped is
 * NO. The screens: HOME (the mixer), SOUND (the track's pages as rows), FX (the global effects), PROJECT, SYSTEM.
 * Drawn with the Terminus font in Felucca 1.0's structure (the header 0..24, the cards 28..72, the panel 76..198,
 * the footer 202..240); the Felucca look is phase 5. Main loop only: nothing here runs in the audio interrupt.
 *
 * The entry points the core calls are SLOOP's names (system/main.c, project.c, editor.c): ui_input, ui_leds,
 * ui_draw, go_home, layers_init, panel_setup, track_select, ui_say, ui_message, ui_say_st, and the fields of `ui`
 * the core reads (force, menu, page; miss.c: msg, msg_t, layer, hold_kind). The model operations come from
 * core/model.c and drums/dsnd_desc.c, shared with SLOOP's UI.
 *
 * Files, in this order: op_state.c (the state, messages, the confirm), op_cells.c (cells, the rows of PAGES),
 * op_screens.c (HOME, SOUND, FX), op_project.c (PROJECT, SYSTEM, the screen table), op_draw.c (the renderer),
 * op_input.c (the panel, the entry points). */
#include "../sloop/ui_colors.c"         /* the colour language (engine, kit kind): shared with SLOOP's UI */
#if FELUCCA_BRIGHT
#include "../sloop/bright.c"            /* the backlight level (main.c reads BL_DUTY, project.c bright_boot) */
#endif
#include "op_state.c"
#if FELUCCA_MISSING_WARN
#include "../../storage/miss.c"         /* "MISSING: PHYS T2" after a load (cur_page, ui.msg: op_state.c) */
#endif
#include "op_cells.c"
#include "op_screens.c"
#include "op_project.c"
#include "op_draw.c"
#include "op_input.c"
