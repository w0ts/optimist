# Optimist UI: how it works now

The Optimist UI is the only user interface of the firmware (SLOOP's UI was removed on 2026-10-19). This page is the reference for how it is built and why: the grammar, the screens, the rules that cut across them, and where each lives in the code. It describes the state of `optimist` at 8c392850 (2026-10-19). Old design history, dated rulings, build logs and size tables are in git (`git log -- docs/UI-OPTIMIST-DESIGN.md`).

Two other pages carry the user's view and are not repeated here: [guide/shortcuts.md](guide/shortcuts.md) lists every gesture, [guide/interface.md](guide/interface.md) explains the controls and pages. Where this page and the code differ, the code wins: report it.

The look and the first screens come from Felucca 1.0 (Leo Kuroshita, GPL-3.0-only); NAME is ported from its `ui_name.c`, the step conditions and the sequence tools from Felucca 1.5.6 (credits: LICENSING.md).

## 1. Basics

### 1.1 The hardware and the screen

- 14 buttons: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+. 7 encoders: SELECT, ALGORITHM, PRESETS, KNOB 1..4. No encoder pushes.
- 27 keys: 16 white (F3..G5), 11 black (F#3..D#5 and F#5). On the drum track the white keys are always steps and the black keys are the lanes.
- 240 x 240 screen. A canvas holds at most 124 rows, so screens draw in bands of at most 240 x 124, each redrawn only when its signature changes (`op_draw.c`).
- Layout (y): header 0..24; the cards 28..120 as a 2 x 2 block (the mixer: four small cards 28..72); the panel under them; the help band 222..239 when the build has the help line (section 12).
- Button LEDs, key LEDs and PLAY's second lamp: section 11.

### 1.2 The code

All of it is `firmware/src/ui/optimist/` (about 17,000 lines, one translation unit through `optimist.c`). It calls the core (`core/`, `seq/`, `drums/`, `fx/`, `storage/`) and owns no audio state.

| File | Holds |
|---|---|
| `op_state.c`, `op_input.c` | state, messages, the confirm; the panel: buttons, knobs, keys, LEDs, the frame |
| `op_walk.c`, `op_combos.c`, `op_edits.c`, `op_keys.c` | ALGORITHM's walk and a lane's sound; SAVE / SHIFT + a button; the "discard edits?" signatures; the keys' lights |
| `op_cells.c`, `op_names.c`, `op_effval.c` | cells (label, value, unit, kind) made from the shared pages; the names table; effective values |
| `op_screens.c`, `op_mixer.c`, `op_mixdraw.c` | SOUND and the master FX; the mixer's rows and sets; its drawing |
| `op_step.c`, `op_stepdraw.c`, `op_tools.c` | STEP; its grid and roll; TOOLS, GEN, GEN OUT |
| `op_global.c`, `op_gldraw.c`, `op_project.c`, `op_prjflow.c` | GLOBAL and SYSTEM lists and their drawing, the SYSTEM table; PROJECT |
| `op_layers.c`, `op_laydraw.c`, `op_tempo.c`, `op_song.c`, `op_scope.c`, `op_name.c`, `op_fm6.c`, `op_fm6draw.c`, `op_preset.c`, `op_help.c` | the held layers and their maps; TEMPO; SONG; SCOPE; NAME; the FM6 editor; a preset with its engine; the help line |
| `op_draw.c`, `op_graph.c` | the renderer, the forms, the SOUND pictures, the modal |

A screen is five functions (rows, a row's name, a cell, a turn, a YES). A cell is a label, a value, a unit and a kind (value, read-out, action, a row to enter), mostly straight from a descriptor of the shared tables (`core/params.c` PAGES) and `param_format`, so a value lives in one place and the web editor shares its names.

### 1.3 The screens

| Screen | Reached by |
|---|---|
| The mixer (HOME) | HOME tapped, anywhere; the root |
| SOUND (a track's pages) | ENV LFO EDIT FX SCL ARP tapped |
| STEP | SEQ tapped |
| The master FX | FX tapped on the mixer's MASTER row; ALGORITHM left of T1 |
| GLOBAL, SYSTEM | GLO tapped (again: the other) |
| PROJECT | SAVE tapped where YES has nothing to do |
| SONG | SAVE held on PROJECT (on SONG: back) |
| TEMPO | PLAY held |
| SCOPE | HOME tapped on the mixer |
| NAME | a save that names (section 4.11) |
| a layer's map | its button held (section 4.9) |

## 2. The grammar

Every screen is a list of rows. A row has up to four cells; the cells of the cursor row are the four cards and KNOB 1..4 edit them. Nothing has to be held to edit.

### 2.1 The knobs

| Control | Does |
|---|---|
| SELECT | the cursor: which row (or page). Stops at the ends |
| ALGORITHM | the track, on every screen that has one: MASTER, T1 T2 T3, DR, the 10 drum lanes, ACCENT. Stops at both ends. Section 2.7 lists where it does nothing |
| PRESETS | the selected track's sound, one a detent: a synth preset, DR the kit, a lane on its own source that lane's sound. On STEP it scrolls the 16-step window; on GLOBAL, SYSTEM and MASTER it does nothing; under a question, the REC ring and NAME it does nothing |
| KNOB 1..4 | the cursor row's four cells; a turn makes its cell hot |
| SHIFT + a knob | the value back to its default (the mixer's volume cards: the row's mute) |

PATTERN's LEN moves through 1 2 4 8 16 32 64; LFO held + the knob moves it by one.

### 2.2 Taps, holds, YES and NO

- SAVE tapped is YES: enter a row, toggle an on / off value, do an action, confirm a question. HOME tapped is NO: cancel, go back to the mixer; at the root nothing.
- SAVE, HOME and the page buttons act when let go, and only if nothing else was pressed, turned or played meanwhile. That is how a tap, a layer and a chord are told apart.
- A hold is 350 ms by default, one threshold for every tap-or-hold decision (a layer's map, PLAY for TEMPO, a step key, NAME). SYSTEM > CALIBRATE > HOLD sets 250 / 350 / 500 ms (`core/hold.h`, settings word bits 21..22). A page button let go within hold + 150 ms still counts as a tap.
- A page button tapped goes to its family's rows; tapped again, to the next row, past the last the first (section 2.4).

### 2.3 The two shifts, and SAVE

SHIFT is OCT- held, SHIFT2 is OCT+ held. They read one language, as the buttons sit (OCT- down, OCT+ up):

- **SHIFT: off, less, remove.** Mute, clear, default, a ghost hit, the panic, INIT, a return muted.
- **SHIFT2: pick, on, more.** Select, a hard hit, launch now, the next instrument or kind, a layer locked open, STEP's window following again.
- OCT- and OCT+ change the octave (-3..+3) when let go, and only if no other control was used while held. Both tapped together (let go within 400 ms): the track <-> MASTER toggle, back to the page left; on a synth track's STEP, FOLD instead (section 4.2).
- HOME is only HOME / NO. SAVE is never a shift: it saves.

| Held | Plus | Does |
|---|---|---|
| SAVE | SEQ | store the working pattern into its slot (asked over a used one) |
| SAVE | ENV LFO EDIT FX SCL ARP | the sound as a user preset (NAME first); on the drum track its 10 lanes as a user kit |
| SEQ | REC tapped | SONG REC on / off |
| SEQ | PLAY | play the song from its start |
| SEQ held alone (not on STEP) | | SONG (on SONG: SELECT's cursor unchanged) |
| (on SONG) | white keys 1..16 | scene A..P: the cursor there and the scene launched (playing: next bar, stopped: loaded); SHIFT2 + the key: launched as soon as possible (the next block, not the bar) |
| SHIFT2 (OCT+) held | SEQ | SONG, from any screen (STEP too; a long SEQ is the same off STEP) |
| (SONG, SCENES) | KNOB 1 (and SELECT) / ALGORITHM and KNOB 3 / KNOB 4 / KNOB 2 | a table of 8 scenes a window (A-H, I-P) by 5 lanes (T1 T2 T3 DR: the pattern each track plays; BARS, purple: AUTO or 1..64, how long the scene plays before something else may start): KNOB 1 the cursor along the scenes, ALGORITHM and KNOB 3 up / down the lanes, KNOB 4 the value under the cursor (the STEP editor's language); KNOB 2 does nothing here (the scenes stay where their letters are). A hand launch while playing waits for the end of a pass of the playing scene's bars |
| (SONG, PARTS) | the same | a timeline of 8 parts a window by 2 lanes: SCN (the scene's letter) and BARS (purple): S the scene's own BARS (the default), A the scene's longest pattern whatever its BARS says, 1..64. KNOB 2 moves the part itself left / right (it swaps with its neighbour, the cursor goes with it, the clock follows when playing); SAVE asks to insert a copy after it; a part key held (or SHIFT + it) cuts it and what follows |
| (on SONG) | SEQ tapped | the views in turn: SONG (scenes and parts: white keys the scenes, black keys the parts), patterns, settings (MODE), each at the row it was left on |
| (on SONG) | SAVE + a scene key | stores the loop into that scene (asked over a used one) |
| (on SONG) | HOME + a scene key | asks to clear that scene |
| (on SONG) | a scene key held (alone) | asks to clear that scene |
| (on SONG) | SHIFT + a scene key / a part key | the same questions as the holds: clear that scene, delete that part and what follows |
| (patterns view: the PATTERNS row, the LFO layer) | a pattern key held (alone) | asks to clear that pattern (HOME + key still does; SHIFT + key is the launch's next-bar timing there) |
| (on SONG, PATTERNS row) | SEQ held + a white key | SEQ + n stores the four tracks' patterns as scene n (asked over a used one; SEQ + SAVE + n does too), SEQ + HOME + n clears it; scenes are launched from the SONG view or black key 10's scenes view |
| (on SONG) | a part key held (alone) | asks to delete that part and every part after it (part 1 stays) |
| (on SONG) | a part key held + a scene key | that scene is the part's; the part (and those between) made when past the chain's end |
| (on SONG) | black keys 1..8, OCT- / OCT+ | the cursor to part n of the window of eight the cursor there and the song jumps to it at the next bar (stopped: its scene loaded); SHIFT2 + the key: at once (the part from its first bar, the next block); OCT moves the window by eight |
| (on SONG) | F#5 (the last black key) | the cursor between the PATTERNS row and MODE (on PATTERNS the layer's keys stay) |
| SAVE | PRESETS | the sound a favorite (right) or not (left) (section 13) |
| SHIFT | a key | that track's or lane's sequencer mute |
| SHIFT2 | a key | that track or lane selected |
| SHIFT | ENV | INIT the sound (asked over edits) |
| SHIFT | PLAY | stop, every voice off |
| SHIFT2 | a layer's button | the layer locked open (either order) |

Keys under the shifts: black key n is drum lane n, the last upper key the ACCENT lane, white keys 1..4 are T1 T2 T3 DR. On STEP the white keys stay steps (SHIFT / SHIFT2 + a step key: a ghost / hard hit). On a lane key the roles invert on STEP (the key alone selects there, plays elsewhere). Pages with no track take no shifted key (section 2.7). `shift_keys` in `op_input.c`.

### 2.4 Paging

1. A page button tapped again goes to its next page and, past the last, to the first (ENV LFO FX SCL ARP EDIT on SOUND; FX on the master FX; SEQ on STEP; a page button with a step held; OCT-/OCT+ in the FM6 layer). GLO is the exception: it switches GLOBAL and SYSTEM.
2. SELECT moves through the same pages forwards and back and stops at the ends: a knob that wraps loses the user's place.

SELECT's other jobs: a held step: the step's card pages; the mixer: the row's knob sets; GLOBAL / SYSTEM: a page of four lines; PLAY held: TEMPO's rows. A layer held: nothing. SCOPE has one page. NAME: nothing.

Every screen remembers its page (RAM only, cleared at power-on): SOUND per track and per family, STEP's row, the held step's card page per track, the lists' cursor, the mixer's set. A row a track no longer has falls back to the nearest. Not remembered: layers, NAME, a modal. `snd_mem`, `hp_mem`, `ui.row[]`.

### 2.5 Questions, toasts and what asks

- **One confirm.** A destructive action arms and a modal box covers the panel: the verb large, the target large in its colour (a track in its engine's colour), "HOME no" left and "SAVE yes" right at the box's foot. The frame is red when the action destroys or replaces work, amber otherwise. YES does it; NO, another row or 3 s let it go. Cards, header and help line stay.
- **Toasts.** What a confirmed action says (*T2 CLEARED*, *SAVED*, a preset's name and engine) is a small box in the middle of the panel for about 1.5 s, framed in the colour of its words. Passive status (MISSING, RECORDING, STOP BEFORE SAVE) is a header message for about 2.5 s.
- An action card shows its verb, never "YES"; its small label is its object. CLEAR, ERASE, NEW, INIT, DELETE are drawn in the alarm colour. A knob turn only picks an action card, it never runs it. Help lines say "(asks)".
- **Red** means destroys or replaces work and nothing else (`prj_danger`, `go_danger`): SAVE and DELETE over a used slot, LOAD over edited work, NEW always, INIT, FORGET. SONG REC is never red (green recording, amber armed); only "Record over song?" is.
- **What asks** (`op_edits.c`): everything that replaces an edited sound ("Discard T2 edits?": PRESETS, SHIFT2 + PRESETS, the SOUND row's PRESET and ENGINE, a lane's own sound, the kit), INIT, a user preset's LOAD; a scene or project LOAD only when the work differs from what was last loaded, stored or made NEW; SONG REC over a stored song; a scene or pattern store over a used slot, and every clear; SONG's DELETE part; a lane's RESET / SAVE / ERASE; FM6 STORE / SEND / INIT; CALIBRATE PANEL; BLE FORGET.
- A sound is *edited* when the values a preset sets (`model.c param_kept`) and, on the drum track, the lanes' sounds and sends differ from when it was loaded or saved. Recorded motion that writes values while playing may make a sound read as edited (it asks; never the other way).
- Not asked, because undo covers them: STEP's TOOLS, single-step edits.

### 2.6 Undo and redo

SAVE then HOME is undo, HOME then SAVE is redo; both act when one of the two is let go, with nothing else used. Several levels on every track; each step key pressed, a lock written, a motion pass and a clear opens one (`seq/undo.c` keeps steps, the automation list and LEN / DIV). On STEP with a note picked, SAVE held alone and HOME held alone are mute and delete (section 4.2), which is why the chords act on release.

### 2.7 Pages with no track

GLOBAL, SYSTEM (the BLE devices list too), PROJECT, the master FX, SCOPE, SONG, and the mixer's MASTER and USB rows play no sound, send no MIDI note, light no key, pick no lane and record nothing: the ISR's `ly_lock` is LY_STEP there and `op_drain` drops the presses (`keys_none`). Exceptions: PROJECT's slot rows (a white key picks and runs the slot), SONG's PATTERNS row (the LFO layer's keys), TEMPO (PLAY held: a white key taps the tempo), NAME (types), a held layer (its keys are its own), and MASTER entered by both OCT (the track's keys stay live, `mk_live`).

ALGORITHM follows the same rule (`algo_has_track`, using the header's classification): it walks on the mixer, SOUND, STEP, SCOPE and the layers EDIT, SCL, STEP and FM6's ENV. It does nothing on PROJECT, SONG, TEMPO, NAME, the BLE list, a question, the master FX opened by FX, and the layers FX, ARP, GLO, LFO. On GLOBAL and SYSTEM it scrolls the cursor one line a detent. The master FX reached by walking left of T1 keeps the walk, so that right leaves it (`fx_by_walk`).

## 3. The look

### 3.1 Cards and lists

- **Cards are 2 x 2** on every screen with cards except the mixer's channel and lane rows (which want the height and keep four small cards; its MASTER row takes the big cards). KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom right; label small at the left of the label row, unit right-aligned in it, the value large, its form under it, the hot card white. There is no CARDS setting (settings word bit 23 is reserved).
- **Under the cards**, a screen shows its picture alone on the rest of the panel when the picture is worth it: the envelope (ENV, ENV2), the LFO wave, the arpeggio (ARP, every row), the scale, the gater, the pattern's steps, FM6's algorithm, TEMPO, SCOPE. A row's picture is its family's nearest one (LFO DEST shows the LFO).
- **No picture: four lines** (always on: the builder item `PAGE_LINES` was retired 2026-10-21). EDIT, VOICE, a lane's pages, PROJECT, SONG, the master FX, and SOUND pages whose picture only repeats the cards (the FX needles, a lane's name and kit) show the cursor row as four lines in GLOBAL's look, value k on line k, SELECT the next row; the header names the row and its n/N. Off: the 2 x 2 cards with the row list (five rows of 19 px) under them. `pl_on`, `pl_draw`, `pic_useful`.
- Lists put a row's name in a column (9 characters from x 4) and its four cells at x 84 + 38 k; a longer text pushes its neighbours right, and only a row that cannot hold every text whole gives up characters, the longest first (`row_layout`).
- The cursor row is the line's colour at full strength with black or white ink chosen by contrast (`ink_on`: no text on a fill is hard to read on the FM-1). GLOBAL / SYSTEM's cursor line is inverted instead: the line's colour as text on black. The BLE devices list keeps its own rows.

### 3.2 Forms

Every value has a form beside its number, chosen from its descriptor's range, never per parameter (`cell_gauge`, `draw_gauge`):

| Value | Form |
|---|---|
| number, min 0 | bar from the left |
| min < 0 < max (PAN, FILT, TRANSPOSE, depths, a drum TUNE) | bar from the centre |
| list of up to 12 | segments, the current one lit |
| longer list | a tick on a line |
| on / off | a pill (GLOBAL / SYSTEM: a square mark at the right) |
| a name, an action | text |

### 3.3 The header

Badge, title with n/N, bar.beat, tempo. The badge is the selected track's number and engine in its colour (*2 DIGITAL*, *4 DRUMS*; a picked lane *4 SNARE*; *4 ACCENT*), as coloured text with a 1 px underline, no fill. It shows only on pages whose values belong to the track: SOUND and its families, STEP and a held step's cards, ERASE / KEY / FM6 layers. Every other page's badge is its name in the neutral colour: MIX, MASTER, PROJECT, SONG, TEMPO, SCOPE, GLOBAL, SYSTEM, NAME, a layer's name (FX, REPEAT, MIX, SCENES, PATTERNS). MASTER has no instrument. The title is the cursor row's name alone, never the screen's word, with its continuation number (*Edit 1 2/7*), the n/N grey. bar.beat always stays: a title short of room takes a short form from the names table (`NM_SHORT`: *Pat*, *Scl2*, *Vox2*, *Kit snd*), never a cut. GLOBAL / SYSTEM show a battery icon. A track change flashes the full name (badge, engine, preset) for about 1.5 s on the three synth tracks (`OP_TFLASH_FRAMES`). The rule under the header turns red ~1 s after a drop-out and amber after another underrun.

### 3.4 Names and case (`op_names.c`)

One table names every thing, looked up when drawn; what cells, rows and help lines key on stays the shared name, so the web editor and `tools/param_help.json` are untouched. Three forms: a **title** (sentence case, at most 9 characters, fits whole with its n/N beside the widest badge), a **card label** (capitals, at most 6; an action's object 8), a **list cell** (4 characters, never cut: ANLG DGTL PHSE, LOAD SAVE CLR INS DEL). The mixer's 1x4 cards use narrow forms where a label and its unit cannot share the row (LVL, REV, DLY, CHO): the label gives way, never the unit. One send order everywhere: REV DLY CHO. Preset, kit, lane, voice and project names are drawn as stored. `tests/ui_optimist_names.h` walks every name.

### 3.5 Colour means something

- A track is drawn in its engine's colour (the drum track: its kit's kind, a lane its source's), from `tools/colors.json`; there is no colour per track number. Every colour that can sit under text holds white at 3:1 or better; SUPER is magenta so ANALOG, the drum coral and SUPER stay 15 degrees of hue apart. The palette is fixed (there is no COLOR setting).
- **C_AUTO** (pale yellow, `#ffec80`): a step carrying a lock or a motion event of the sound; **C_NOTE** (cyan, `#4de6ff`): the note a held step's SELECT picked and the note picker's frame. Red only destroys or replaces; amber is changed, pending, gain reduction, a notice; dim is free or unused; grey is a read-out. A muted track is struck through (a 1 px line, 2 in the large face) in its row name, header badge, and its volume card's label and value.
- Mute tiles of the GLO layer are outlined while the track is heard and filled only when muted.

### 3.6 Empties and geometry

- A knob with no parameter is its frame alone (no label, no dash). A parameter with no value says one "-". A slot with nothing says EMPTY, dim. What the build lacks never shows. No page has one card, except the kept SCL 2, TEMPO's REC 2, PROJECT's Memory and the groups of two or three (SENDS, ENV dest, CMP).
- A long name on a 1x4 card with an empty card after it is drawn two cards wide (`card_wide`).
- Nothing else draws in the help band (222..239) in a build with the help line: it is reserved whether the line shows or not, so nothing jumps.

## 4. The screens

### 4.1 The mixer (HOME)

Horizontal: **the rows are the tracks, the four knobs are four values of the selected row** on its 1x4 cards. Rows: MASTER (above T1, out of view until the cursor goes up), T1 T2 T3, DR, the 10 drum lanes, ACCENT, and with USB audio built the USB input after the drums (`MXR_*`, `op_mixer.c`). ALGORITHM walks them; a lane row selects that lane (its sound previews once if the transport is stopped, `lane_pick_from`).

The panel shows one group at a time and the view follows the cursor (`mx_view`): **MASTER** alone (a tall VU over a dB scale, the compressor's gain reduction as an amber bar from the right with its dB, the REV DLY CHO returns' meters, `fx_ret_tap`); **channels** T1 T2 T3 DR, four rows of 41 px (code and chip, the engine and preset in the track's colour, the page's 16 steps with a page bar when LEN > 16, a thin VU with the part's compressor reduction pushing from the right); **the drum lanes** and ACCENT in one column (13 px rows over the help band): code, hits on the page's steps, a hit meter that falls over 12 frames; **USB IN** alone. DR shows every lane's hits merged. The selected row is outlined in its colour. A sequencer-muted row's steps are grey with an X by its code.

SELECT pages the row's knob sets, in one order on every row: the mix, the insert slots, the sends, the rest (each group starts a set; `mx_items`). The header names the set (*T1 mix*, *T1 insert*, *T1 sends*).

| Row | Sets |
|---|---|
| T1 T2 T3 | VOLUME PAN \| S1 TYPE, amount, S2 TYPE, amount \| the sends of returns A B C (REV DLY CHO by default; blank card for an empty return), PAN |
| DR | VOLUME \| its two insert slots \| its send levels (0..100 %), PAN |
| a lane | LEVEL CUT DRIVE \| its two insert slots \| REV DLY CHO, PAN |
| ACCENT | ACCENT (the amount) |
| MASTER | its insert slots; DYN's THRS RATIO GAIN CEIL while a master slot holds DYN; the returns A B C levels; A SENDS and B SENDS (the sends between the returns, left to right; C SENDS and the feedback ones with `RET_FB`); DUCK |
| USB IN | VOLUME PAN FX ON \| its slots, then each insert's settings (USB EQ3, EQ3 F, GATER, JUNO) \| its sends |

Mute is not a card: SHIFT + the row's volume knob mutes the sequencer (a return: SHIFT + its card's knob; USB IN keeps its own mute). There is no solo. SAVE tapped always opens PROJECT (the mixer has no YES). FX tapped on MASTER: the master FX; on USB IN: its inserts. PRESETS browses the row's sound. HOME tapped opens SCOPE. A layer held over the mixer is the layer's own 2 x 2 screen.

### 4.2 STEP

SEQ tapped. With nothing held the **16 white keys are the 16 steps of the window** (`ly_lock` = LY_STEP: the keys reach the UI and play nothing). Pages (SELECT, or SEQ tapped again): **PATTERN** (LEN DIV SWING GATE), **TOOLS**, **GEN**, **GEN OUT** (`step_rows`). STEP opens on the page it was left on. The window is 16 steps of up to 64; PRESETS scrolls it and stops the follow; SHIFT2 + PRESETS follows the playhead again; FOLLOW is the default while playing.

- **The grid.** Drum track: the lanes (10 + ACCENT) x 16 steps, a hit in its lane's colour shaded by level, ratchets as notches, the selected lane lit, each lane's chip and two-letter code at the left. Synth track: a piano roll with note names in a 24 px gutter, ties as bars, chords stacked, the level as the shade, an accent a white top edge, the window's page lit in the playhead strip. The roll's view is `song.octave` (the keyboard octave, one value): OCT moves it an octave; both OCT: octave 0. Under the grid a dot per event of the store (a hold event with a tail), then the playhead strip. A step with automation is drawn in C_AUTO.
- **A step key tapped** (let go before the hold time, nothing done meanwhile): an empty step is set with the pick, a set one cleared; a cleared synth step remembers its notes and comes back with them. A drum step cycles normal, hard, ghost, off. SHIFT / SHIFT2 + a step: a ghost / hard hit (drums); SHIFT + a step on a synth track: its p-locks cleared. **A step held** (past the hold time, even empty): its cards open and nothing toggles.
- **The cards** are the step's: synth NOTE LEVEL RATCHET LENGTH (ties; a recorded fraction in tenths), drums LEVEL RATCHET TUNE DECAY (the selected lane's locks). SELECT then pages EXTRA (CHANCE NUDGE FILL COND) and the track's sound pages as p-locks; a page button with a step held jumps to its family's page. A turn writes a step-only event (a padlock), YES toggles the hot cell between step-only and hold (an amber arrow), SHIFT + the knob clears it, HOME tapped clears the step. A turn on a step without the lane's hit writes nothing (*NO HIT ON THIS STEP*). On a held chord SELECT picks the note ALGORITHM moves; past the last it goes on to the card pages.
- **A held step moves.** PRESETS moves it in time (events go with it; the step it passes takes its place; refuses at the first / last step, *TIED NOTE*, *PAGE EDGE*), ALGORITHM moves a synth note a semitone or a drum hit one lane. OCT-/OCT+ transpose its notes an octave.
- **The note picker.** The four knobs edit only while the cards are up (SELECT turned or SEQ tapped brings them back, same page). With the cards down: KNOB 1 picks a note, note by note (by step, in a chord lowest first, carried notes passed: op_step.c pk_heads; a lane's hit), KNOB 2 moves it a step (SHIFT: its nudge), KNOB 3 its pitch (drums: its lane), KNOB 4 its length (drums: its level). SAVE opens the picked note's cards as if its step were held, HOME lets them go. SAVE held alone mutes the note (kept, dim, played as a rest; not on ACCENT), HOME held alone deletes it. A mute is a store event (`AUTO_MUTE`, `AUTO_SMUTE`) that moves with its note. `pk_*` in `op_step.c`.
- **FOLD.** On a synth track both OCT tapped together (within 400 ms) fold the roll to the rows of the notes the pattern plays (a scale set: the scale's notes), toasts *FOLD: NOTES* / *FOLD: SCALE* / *FOLD OFF*; no OCT view while folded (`roll_fold`).
- **Synth note entry.** SEQ held + keys plays and sets the pick (the core's pen: a chord if keys are down together); letting SEQ go keeps it. SEQ held alone past the hold time toggles the keys to playing with STEP up. The pick follows the octave.
- **Clears.** Every clear of recorded material is on REC (section 6). EDIT held + a key erases that note or lane as the pattern plays.
- **TOOLS** is one card: REVERSE, RANDOM, COOK, LENGTH x2 / x1/2 and QUANTIZE (QNT 1, 1/2, 1/4 snap nudged starts); a knob picks, YES does it, one undo each; the scope is ALGORITHM's (a synth track, the whole kit, or one lane). **GEN** and **GEN OUT**: section 8.
- Page memory: section 2.4.

### 4.3 SOUND

The selected track's pages as rows, in `core/params.c` PAGES order, with the page buttons narrowing to a family (LFO shows LFO, LFO DEST, LFO 2, LFO2 DEST; ENV the envelopes and DEST rows; FX the track's effects; EDIT the engine's rows; SCL, ARP theirs). The whole list is shown when SOUND is entered from the master FX or by a walk.

- **EDIT opens on MAIN**, the four values that matter most: a synth engine's `engine_t.macro` (CUT RES first where there is a filter; FM6 has none and no MAIN), a drum lane's TUNE DECAY CUT LEVEL. One SELECT back is the **SOUND row**: a synth track ENGINE PRESET INIT SAVE AS; the drum track the lane's SRC, ACCENT, INSTR and the KIT. SHIFT2 + PRESETS steps the engine (the next engine's first sound); presets are otherwise picked with PRESETS, so the toasts name the preset and its engine in the engine's colour.
- **Drum track.** The pages are the selected lane's (section 5). With DR itself picked, ENV and LFO do nothing (dark LEDs). A value the lane has of its own is the lane's; a value only the track has stays the track's and is tagged **DR**, amber, top right (the drum bus's inserts "DR ins", the kit send levels "DR sends", Bus send, Slicer, Filter, Gater, Comp, PATTERN, the KIT card). The first SOUND card follows the lane's source: KIT, a PRESET (a lane on an instrument), a SLOT (USR1..3), or dim "-" (ACCENT).
- **FM6**: section 4.12.
- **LFO 2** (always built): LFO 2 RATE WAVE TRIG POL; LFO2 DEST DST A AMT A DST B AMT B. DEST lists OFF PIT FLT SHP AMP RES PAN LVL ATK DEC E1..E8 (an engine's own names), and XRATE / XAMT to the other LFO. The drum track has none (its lanes have their own, section 5).
- **ENV 1 MOD** has four depth knobs made per engine, the amplitude always ENV 1's. VOICE 2 is ALLOC DETUNE VOICES DROP.
- Pictures: ENV, ENV2 envelopes; LFO and LFO 2 waves (`lfo_shape`); FX slots as needles named with their card's label; the scale on an octave of keys; the pattern's steps; ARP draws the arpeggio over the held notes or a C E G reference, never blank.

### 4.4 The master FX

Reached by FX on the mixer's MASTER row or ALGORITHM left of T1; FX again: the next page. Badge MASTER. Pages in the signal's order (`FX_ORDER`, `FX_ALIAS` in `op_screens.c`): **Inserts** (the master bus's slots and amounts), the dynamics (INS DYN 1 THRS RATIO ATK REL, INS DYN 2 GAIN CEIL GR, while a slot holds DYN), JUNO / EQ3 settings while a slot holds them, **Duck**, **Returns** (A B C levels, REVERB's TYPE as the fourth card), the effects they return (Delay and its filter TYPE SLOPE FREQ RES, Reverb), the return sends and EQ, **Slots** (the types), **Macro** (COLOR MOTN SPACE ENRGY, MACROS builds). Page names are at most 9 characters. TUNE is on GLOBAL's Tempo list. The mixer's MASTER row follows the same order.

### 4.5 SONG

Entered by SAVE held on PROJECT, any other way is gone. Rows: **SCENE A..P** (T1 T2 T3 DR: the pattern each track plays in it, *KEEP*, *-*; knobs edit the references while stopped, written once they rest; YES launches at the next bar, stopped it loads; REC stores the loop into it; SHIFT + REC clears it; the playing scene green, a queued one amber, *\** a changed one); **PATTERNS** (each track's playing pattern, a turn cues the next; the panel is the 4 x 16 session grid; the keys are the LFO layer's); **MODE** (LOOP / SONG, SONG REC, the MEM gauge, SAVE writes the chain) before the parts; **PART 1..64** (SCENE BARS INSERT DELETE, edited stopped, the part playing white). The chain is saved when SONG is left, stopped. Without PATTERNS a scene says STORED / EMPTY.

### 4.6 PROJECT

Rows: PROJECT, SNAPSHOT, USER, TOOLS (CLEAR SEQ, INIT SOUND, MISSING, NEW), SLOOP (import of SLOOP 2.4's slots and autosave), MEMORY (USED, FREE). A storage row is four cards (`op_prjflow.c`):

| Card | |
|---|---|
| 1 BANK | the bank of 16 slots the keys are; only on a store of more than 16 (USER: 32) |
| 2 ACTION | LOAD (default), SAVE, DELETE. PROJECT has no DELETE (NEW is the blank project). Red when it would destroy or replace |
| 3 ITEM | the slot; walks all of them across the banks |
| 4 | PROJECT: NEW; USER: INIT; SNAPSHOT: empty |

The 16 white keys are the slots of the bank: key n selects item n and runs the action through the confirm. YES runs it on ITEM. A free slot to LOAD or DELETE: a toast, no question. The action returns to LOAD after each action, when the row changes and when the screen is left, so a stray key never saves. OCT- / OCT+ tapped pick the previous / next bank. SHIFT2 + ITEM on USER marks a favorite. Under the cards: the bank as a strip (used filled, free outlined), the sentence the key would run, the item's name and what is stored (a project's name from the section log, a snapshot's status and size, a user preset's engine). Projects and user presets are named on NAME; project names are record 23 of the section log (16 x 12 bytes); builds with four RAM project slots save unnamed. Kits keep numbers.

### 4.7 GLOBAL and SYSTEM

Two settings lists, one setting a line, pages of four lines, no cards (`op_global.c`, `op_gldraw.c`). GLO tapped switches between them; each keeps its cursor line. KNOB k edits line k of the page and makes it the cursor; SELECT moves a page; ALGORITHM moves the cursor a line, across pages; PRESETS does nothing; SHIFT + a knob resets to the default (or the factory value; BLE is never reset); SAVE is YES on the cursor line (an action, a toggle). OCT-/OCT+ held on a page with Tempo's lines nudge the clock; elsewhere the octave.

A page is one section; sections are never combined to fill a page except two joined pairs when together they fit four lines (*Screen & audio*: SCREEN with AUDIO; *Connections*: USB with BLUETOOTH, which only fits in a build without USB SERIAL). Only a section of more than four lines is split between rows. Pages are built from the lines the build has (`gl_build`), so no page number is fixed. A line: name left, value right in the large face where it fits, its form under it; read-outs grey, actions their verb; the line's colour is the knob's with KNOB COLORS. The help line shows the cursor line always.

| Screen | Sections |
|---|---|
| GLOBAL | Tempo (BPM NUDGE SWING TUNE), Rec (MODE LENGTH START CLICK, then QUANT), MIDI (CLK IN, CLK OUT, MIDI OUT), Channels (T1 T2 T3 DR) |
| SYSTEM | INFO, INTERNALS and USB STATS (debug builds, `SYS_DEBUG`), Screen & audio (BRIGHT, KNOB COLORS, LOWCUT), Keys (BUTTONS glow, KEYS), Connections (USB SERIAL; Bluetooth), Devices, USB AUDIO, Calibrate (PANEL, HOLD), About (VERSION, BUILD, REPO), Credits |

- **INFO** is text only, no bars. The player's page: CPU (KNOB 1 picks NOW / MAX 5S / MAX; CPU0 and CPU1 in a dual-core build, the other core's stop reason in amber), DROPS (late audio halves), UNDERRUNS (everything else lost, with where), BATTERY (n/3, raw count). With `SYS_DEBUG` it also has clock, GUARD, and **INTERNALS** (LAST RESET, flash interrupts-off time, the second core's counters) and **USB STATS**. SHIFT + SAVE on INFO clears DROPS, UNDERRUNS and the MAX. Code: `system/overload.h`, `sys_info_cell`.
- **USB AUDIO** is a line a source (MASTER, T1 T2 T3, DRUMS, DRUM BUS, DR1..DR10) choosing NONE, 1, 2, 1+2, 3, 4 or 3+4 (`io/usb/ua_tap.h`).
- **Bluetooth**: BLUETOOTH on / off, STATE, DEVICE, MIDI activity (read-outs from `io/midi/ble_devices.c`). **Devices**: DEVICES opens the list (NONE, LAST, nearby with signal bars; SELECT moves, SAVE connects, asked), RECONNECT (OFF or LAST), LAST DEVICE (FORGET, asked). A pairing code shows over any screen.
- CALIBRATE > PANEL asks first and cancels after 30 s of no input. MIDI IN is not a setting (notes come through the track channels). The second core is always on; `dual_mode` stays AUTO and the guard's reason shows on INFO.
- Leaving SYSTEM or GLOBAL saves a changed setting, once stopped.

### 4.8 TEMPO

PLAY held (past the hold time; a tap stays start / stop, acting when let go; a key while PLAY is down opens it at once). Rows: TEMPO (BPM NUDGE SWING SYNC), REC (MODE LENGTH START CLICK), REC 2 (QUANT); without REC MODES, QUANT is REC's first card. OCT- / OCT+ held nudge the clock 3.9 % slower / faster (10/256, `seq.c clk_nudge`; BPM never changes; nothing on an external clock); white keys tap the tempo (two taps or more). The panel: the tempo large (amber while nudged), the four beats, the clock followed (*Auto: Int / USB / TRS*) and its RX light. GLOBAL repeats these rows; TEMPO stays because it opens over any screen. The clock counts whole BPM; a 0.1 BPM FINE was not built.

### 4.9 The performance layers

A page button held shows the layer's map (16 tiles for the white keys, the knobs' four cells) after the hold time or at once when used. Tapped, the button still opens its rows. Inside any layer PRESETS is the sound and ALGORITHM the track (MASTER: the master FX's four cards). SHIFT2 + the button locks the layer open in either order (ENV on FM6, ARP, LFO: SHIFT2 first); any button but PLAY, REC and OCT lets it go. SAVE is no layer.

| Layer | Keys | Knobs |
|---|---|---|
| FX | the 16 punch-in effects | FILTER DUST DUCK, the track's FILT |
| ARP | note repeat | RATE |
| SCL | the song's key | CHORD SCALE QUANT TRANSPOSE |
| GLO (mix) | 1-4 sequencer mute T1..DR; with FILLS 9 a fill held, 10 the next bar, black 1-4 FX bypass (without FILLS 9-12); 16 tap tempo | the four levels |
| EDIT | erase as it plays | SHIFT LENGTH TRANSPOSE |
| (SOUND's first row, EDIT) | PRESETS, SHIFT2 + PRESETS | four lines (ENGINE, PRESET, INIT, SAVE AS) and the popup list below, no picture (the drum track keeps its kit picture) |
| PRESETS, SHIFT2 + PRESETS | a popup list of the sounds over the panel (the drum track: its kits): the one playing lit in the middle, in its engine's colour, scrolling as you turn, gone ~1.5 s after the last turn |
| LFO (patterns, scenes; held or locked it shows SONG's PATTERNS row, back to the screen it left when let go; a knob turned at once, before the hold, is SHIFT and keeps the screen; KNOB 1..4 there select T1..DR and cue their patterns) | white n launches pattern n at its pass's end (OCT+: now), SAVE + n stores, HOME + n clears; black 1-4 pick the track, 5 stop, 9 duplicate, 10 turns the white keys to scenes A-P (same verbs; two launched in one hold: a chain) | none: LFO held + a knob is the fine edit of the screen under it |
| ENV on FM6 | black keys pick OP1..OP6 PIT GLO MONO POLY | the page's four values |

A layer's "Oct- locks it" hint shows once per power-on (on the help line, else under the tiles). The tiles for the 16 sounds are the 16 white keys; ERASE and REPEAT on the drum track act on the lanes.

### 4.10 SCOPE

HOME tapped on the mixer (again: back). One row, one page of cards: the source's values (a track LEVEL PAN FX DRIVE; MASTER FILT THRS RATIO GR). The source is ALGORITHM's: the selected track, a lane counts as DR, MASTER the master output (it stays on SCOPE). The panel is the oscilloscope: trigger-stable (the latest rising zero crossing after a swing below peak / 8 that leaves a whole screen), 236 points two frames apart (10.7 ms), scaled to the peak, 30 frames a second, in the source's colour. Audio side (`fx.c`): one 8 KB ring, `vis_pcm`, switched by `scope_src`: 0 the master mix (before the volume, limiter and knee), 1..3 a part after its inserts and before level and pan, 4 the drums' part of the mix. Off SCOPE the source is the master, which the mixer's MASTER row reads.

### 4.11 NAME

A user preset or a project is named before it is written: SAVE + a sound button, the SOUND row's SAVE AS, PROJECT's SAVE (prefilled with the slot's name) and USER's SAVE. The white keys type like a phone (the same key within 0.8 s cycles), the black keys: F# left, G# space, A# right, C# delete, D# letters / digits. KNOB 1 the cursor, KNOB 2 and PRESETS the character, SHIFT + a knob clears. SAVE done; HOME tapped deletes a character (at the start: cancel); HOME held 0.6 s cancels. 12 characters, stored as typed, drawn in sentence case. The keys type and never sound. SAVE while playing is refused (*STOP BEFORE SAVE*, a flash erase stops audio). An empty user-preset name gets the automatic one.

### 4.12 The FM6 editor

On an FM6 track ENV held is the operator layer (the black keys pick OP1..6, PIT, GLO, MONO / POLY; OCT-/OCT+ the page). ENV tapped is SOUND's FM6 family: OPERATOR (OP ALG FDBK VCE), the picked operator's six pages (TUNE, RATES, LEVELS, SENS, SCALE, CURVES, in M-VAVE's order), PIT's two, ALGO, LFO, LFO 2, PORTA; the algorithm is drawn over the rows (RATES, LEVELS and PIT draw the envelope instead). On those rows SHIFT2 + a black key picks the operator, SHIFT + it switches it off. Edits go into the part's voice (`fm6_ed`); a voice is kept by SAVE AS, a DX7 voice arrives by SysEx.

## 5. The drum track

- **10 lanes plus ACCENT**, each playing an instrument the user picks (SOUND's LANE row, KNOB 3 INSTR; 18 instruments, KICK .. CLAVE) and named after it everywhere. A lane set to an X0X voice takes its instrument. Core: `DRUM_LANES` 10, `DRUM_ACC`, `DRUM_UILANES` (`core/core.h`); `drums.c INST_*`, `lane_of_key`.
- **Keys.** The 16 white keys are steps of the selected lane on every page; the 11 black keys select and play lanes (F#3..D#5 lanes 1-10, F#5 ACCENT). Off STEP a lane key plays and picks; on STEP it picks (heard when stopped, silent while playing). A drum key played selects its lane on the pages where keys play, but not within 300 ms of a knob turn. MIDI in: a GM note plays the lane on that note, else on its instrument, else of its family.
- **ACCENT** is a silent lane: its steps raise every hit of the step by the amount `P_DACC` (default 27). It has no sound pages and no step key (it is not a step). Its sequencer mute is SHIFT + its key or SHIFT + its card's knob on the mixer.
- **A lane's values are events.** On the drum track the parameter `AUTO_LANE0 + (lane << 3 | value)` is the lane's TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL offset, a lock on a step or motion; applied at the hit through an overlay (`drums/lane_play.c dla`), never written to the lane's own values. REC held + a lane knob records them while playing.
- **Each lane has its own LFO, LFO 2 and ENV2** (per hit, two slots each: a DEST among the lane's values and an AMT; ENV2 ATK DEC SUS RTRG): pages LFO, LFO DEST, LFO2, LFO2 DEST, ENV2, ENV2 DEST, only with a lane picked. They are kept in their own record (`storage/dlfo_rec.c`, log ids 122..138), not as automation events, so they are not recordable.
- **Sends are multipliers**: the drum track's REV DLY CHO scale each lane's send of that bus; the drum bus sends nothing of its own.
- **Preview.** A lane picked previews once when the transport is stopped, silent while playing (`lane_pick_from`, `audition_lane`).
- **PRESETS by source**: DR or a lane on KIT: the kit; a lane on an instrument: that lane's sound only; SHIFT2 + PRESETS: the first sound of the next kind (your samples, a sampled kit, a synthesised kit, X0X).

## 6. Recording

- REC: playing, records the selected track at once, again stops. Stopped, arms: the first note starts the loop, or the free take on an empty project, or the count-in (REC MODES). A take ends at a pattern switch. Overdub and latency compensation as in the core.
- **REC held** clears. A ring fills after 0.7 s (the press is undone) and 1.3 s later the selected track is cleared (undoable, toast *T2 cleared*); let go before: nothing. With a lane picked only that lane (*Clear SNARE*). **REC held + a knob** clears that parameter's motion and p-locks over the whole track (a lane value: the lane's); the turn stops the ring so the track is never cleared this way. **SHIFT + REC held** clears the track's (the lane's) motion, p-locks, nudges, fills and chance; notes, levels and recorded lengths stay. SHIFT + REC on a SONG scene row clears the scene (asked).
- **REC QUANTIZE** (TEMPO / GLOBAL, per project, saved in the G_INFO slot, bit 0 = OFF): ON (default) snaps a recorded note's start and length to whole steps. OFF keeps the start as the step's nudge (own steps only, with MICRO) and the length as TIE steps plus a GATE lock on the last step (`rec_len_end`): a NOTE plays GATE / 128 of a step. Within 4 of the track's GATE: no lock.
- **Overlapping notes** (POLY): a note keeps sounding while others are played over it. The new step becomes a chord of the new note and the held ones with the step flag `SF_CARRY` (`step_t.flags` bit 7; chance uses bits 2..6): carried notes already sounding go on, not struck again. MONO / LEGATO / UNISON keep one note a step.
- **Each note's own end** (POLY, REC QUANTIZE OFF): a note up while others are down ends at its own time. The byte `step_t.ngate` (ends at ngate / 128 of the step) lives in RAM; stored as step-only events `AUTO_NGATE0 + k` (0x78..0x7B, `seq/ngate.h`). A chord's keys up within 1/8 of a step share an end. The STEP card LENGTH shows tenths (*1.8 ST*) through TIEs and carry steps (`note_span`, shared with the web editor's `noteSpan`).
- Undo, redo and the song: sections 2.6 and 4.5. SONG REC (SEQ + REC) records the scenes you launch and their bars.

## 7. The automation store

One list of events per pattern (a track's working pattern; a pattern record carries its own), at most **128**, each 3 bytes (`seq/auto.h`, `auto.c`, `auto_base.c`):

```
byte 0  place: bits 0..5 the step, bit 6 STEP-ONLY (0 = hold: motion; 1 = this step only: a lock), bit 7 reserved
byte 1  the parameter: a stored id, or a pseudo-parameter
byte 2  the value, a signed byte (every lockable or recordable value fits: tests/auto_int8_check.c)
```

| Gesture | Writes |
|---|---|
| a step held + a knob | a step-only event (a lock: the parameter goes back at the next step without one) |
| REC + a knob while playing | a hold event (motion: until the next event of that parameter or the pattern restart; STOP puts the patch back) |

Ids: stored track values (`mot_sid`, build-independent); the ACCENT amount `AUTO_ACC` 0x77; note ends `AUTO_NGATE0` 0x78..0x7B; drum lane values `AUTO_LANE0` 0x7C + (lane << 3 | value); insert-slot values `AUTO_SLOTP`; mutes `AUTO_MUTE0` 0x6C (+ lane 0..9, `AUTO_SMUTE` a synth step); pseudo-parameters `AUTO_COND` 0xFC (1..9: 1:2 .. 4:4), `AUTO_NUDGE` 0xFD (-32..31, 1/64 step), `AUTO_FILL` 0xFE (1 FILL ONLY, 2 NO FILL), `AUTO_CHANCE` 0xFF (0..99 %). Chance, nudge, fill, condition are step-only events on drums and synths alike; the drum chance is the step's (every lane together).

`auto_step(t, idx)` applies a step's events in list order in one scan: hold events set the value and leave it; step-only events save the base and let go at the next step without one. On the same parameter the step's event wins on its step; the base a lock returns to is what a hold left. A fill-skipped step skips its step-only events, not its hold events. Every lockable value is recordable and the other way round (`auto_param`); excluded: voice allocation, MUTE, FX OFF, LEN DIV SWING, the LFO DEST slots, values with no stored id. A micro-timing index of nudges (`auto_nudge`) is rebuilt when the list changes, never per sample. At most 24 locks apply at once.

Storage: with the section log a store is written in the new forms (the extras record's `AUTO_ENC_TAG` form, a pattern record V2 for a track with automation); the old MOTN / stepx forms are only read. Four-slot builds (no log) still write the MOTN record (64 hold events). Undo copies the track's list. SLOOP 2.4 import turns its nudges, 24 locks and fills into events (a lock clamped to int8 says so); its export rebuilds nudges, fills and the first 24 step-only events and reports the rest lost (*MOTION NOT IN 2.4*, drum lane values, drum LFO, conditions). The web editor reads and writes the list with AUTO_GET / AUTO_SET (web/EDITOR_PROTOCOL.md).

## 8. Sequencer features

- **Sequencer mutes** (one mute, the sequencer's): a muted track or lane triggers no new notes (internal sound and MIDI out), its steps' locks do not apply, sounding tails finish; its recorded motion keeps running; notes played live still sound. Saved with the project in the FX record (TLV 0x43: the tracks' bits, the lanes' mask, `seq/seq_mute.c`). A sound mute an older project left shows as muted and the toggle clears it.
- **Conditions** 1:2 2:2 1:3 2:3 3:3 1:4 .. 4:4: the step plays on loop a of every b of its own track (counts from PLAY, a song section, a launched pattern). Order on one step: condition, FILL, chance; a failed one is a REST. Edited on the held step's EXTRA page, 4th card COND.
- **TOOLS**: REVERSE mirrors the steps inside LEN (a note moves whole: ties and carry steps with it); RANDOM keeps a synth track's rhythm and gets new scale notes (SCL CHR: the minor pentatonic of ROOT), a drum beat on DR, a lane drawn the same way; COOK changes 15..25 % of notes (drums 10..20 % of hits; never a lane's last nor the kick on step 1); LENGTH x2 copies the steps after themselves (64 at most; refused when the events would pass 128), x1/2 halves LEN. Events move with their steps. The EDIT layer's SHIFT and LENGTH move and copy events too.
- **GEN** (`seq/gen.c`): a note generator is a layer the sequencer plays over the pattern, never written into it. One slot per synth track and per drum step lane; several on at once. TYPE EUCLID (HITS STEPS ROT), RANDOM (DENSITY % SEED ACCENT %; its own hash, so chance rolls are undisturbed), PULSE (EVERY OFFSET RATCHET). **GEN OUT**: GEN on / off, MODE (REPLACE: the hits are the steps; ADD: pattern plus hits; MASK: the pattern only where it hits), WRITE (asks): bakes exactly what plays into steps 1..LEN and turns the slot off; one undo level brings back the pattern and the ON. A synth hit plays the pattern's NOTE, else a TIE's, else ROOT near C4. The grid draws a generated hit outlined and a silenced pattern hit as a dim stub. Saved with the project and each scene (a TLV of the FX record, `GEN_TLV` in `seq/gen.c`).

## 9. Track insert slots and effects pages

Each track (T1 T2 T3, DR) has **two insert slots** S1 and S2, run after the SLICER; a slot holds DIST, FILTER, COMP, GATER, EQ3 or another insert type, or nothing. A type once per track: turning a TYPE skips the type the other slot holds and the ones that would not be heard. The amount belongs to the type, so a lock or motion on an insert's card follows the type. A new track: S1 DIST, S2 FILTER at 0. The master has four slots (`INSERTS` shows two slots and amounts; DYN, JUNO, EQ3 are insert types) and the sends; the USB input has the same two. A drum sound's own DST and CMP are always heard. Code: `fx/fx_slots.c`, `ins_map` for old projects.

SOUND > FX pages (`op_screens.c`): **Inserts** (S1 TYPE, amount, S2 TYPE, amount; a TYPE card lists ---- (empty) then the types built, A to Z: `fx_slots.c` FXT_ALPHA, fixed at compile time; no lock or reset on a TYPE card), **Comp** (RATIO ATK REL) only while a slot holds COMP, **Gater** (DEPTH PAT RATE SHAPE; 16 patterns) only while a slot holds it, **Sends** (card k is the send of return A, B or C; REV DLY CHO by default), **Bus sends** (drum track). FX + KNOB 4 is the track FILTER amount (*FILT: NO SLOT* when no slot holds it).

CHRS and ECHO are insert types of which the build holds 0..4 instances (`fx/insfx/fxi.c`). The TYPE cell is the picker and says what it holds: *CHRS 2/4* (free of available), *FULL* dim (bypassed on this track), *IN USE* for a frame, *WAIT* for one pass. Types with 0 instances built are not listed. `fxi_cap fxi_free fxi_state fxi_can`.

## 10. Effective values: one number, two bars

On every card the big number and its bar are the value as entered. While the actual value differs from it, a second bar shows the actual value now (white, amber on the hot card, 2 px, no trough); a bar whose value returns stays 400 ms so an LFO crossing it does not blink. The UI recomputes from RAM the audio side already keeps (`op_effval.c effv_cell`); nothing is published and nothing runs in the ISR. A moving bar redraws only its 2 px strip, at most every 33 ms.

| Card | The actual value |
|---|---|
| a drum lane's value | what a hit plays now: the patch, the lane's motion and locks, its LFOs and ENV2 (`dl_play_ofs`, the hit's own formula) |
| a synth cutoff or shape card | the card plus the part's modulation now: LFOs, the newest voice's envelope, the accent, the macros (`mac_effective`) |
| an LFO / ENV DEST card, ENV 1 MOD | the destination's actual value on its scale, while the amount is not 0 |
| PIT, AMP, RES, PAN, LVL, ATK, DEC, E1..E8, CHO DLY REV, INS1 INS2 | the value as the render has it, moved by every slot on it in turn (`lo_move`, as `voice.c lfo_over_in`) |

Not on a held step's cards or the layers' cards. Not counted: an ANALOG 2 part's own ENV2 and keytrack, CZ's amplitude curve.

## 11. LEDs

- **Buttons**: three levels. Full: the screen's button, the layer shown, SAVE blinking on a question, PLAY, REC armed or recording, OCT while the octave is shifted or held on a drum STEP. Half: the button does something now (`op_btn_avail`: HOME SAVE GLO SEQ REC PLAY OCT always; ENV LFO when the track has rows of the family; FX SCL EDIT ARP when it has rows or its layer works; with a step held, the page buttons that lock a page). Dark: nothing. ARP and SCL are dark on the drum track and its lanes; SAVE on STEP is lit only where YES acts; OCT-/OCT+ glow only where they do something. On the MASTER row the track buttons are dark and FX is the master FX. Half is the HAL's dim layer (`fm1_led_dim`, one scan frame in eight; with LIGHTS the glow pulse). Buffers are rebuilt each pass and written only when a byte differs.
- **Keys** (`op_keys.c`): full = active or selected, dim = it has a job here, dark = none; keys that only play stay dark. FX layer: the 16 effects. GLO: mute 1-4, fill, bar, tap. SCL: all 27, the root full. LFO and SONG's PATTERNS row: the patterns (the playing full, the queued one blinking). TEMPO: the white keys. PROJECT: used slots full, free dim, the selected blinking. STEP: set steps full, the playhead blinking, empty steps inside LEN dim. NAME: every key with a job. Under SHIFT / SHIFT2: the 10 lane keys, the first four white keys and the last upper key dim, the muted (SHIFT) or selected (SHIFT2) full. The drum track: the lane keys dim, the selected one full. The LIGHTS / KEYS backlight is off while a mode lights its keys.
- **PLAY's two LEDs** read as one transport lamp (`lamp_on`, `lamp_dim`, `play_led` in `op_input.c`). PLAY's own LED is red; its second LED is green at a keyless matrix site (column 8, row bit 1; found on a unit by MAKU, drawn by the FM-1 emulator, stock V15 lights it too). The LEDs are single colour: a colour is a site.

| State | Green (second LED) | Red (PLAY's) |
|---|---|---|
| stopped | dim | off |
| playing | full | on each beat |
| recording (a take, or the free take) | on each beat | full |
| count-in | a pulse on each beat of the bar | |

REC's own LED shows the take as before (blinking while armed). Legend: guide/interface.md.

## 12. The help line

Builder item `PARAM_HELP` (off in the user-default profile, on in the user's own config; about 7.7 KB of flash). One line of the small face on the screen's last 18 rows: a 1 px rule, then amber words. It shows: what a knob changes while it turns and for about 2 s after, on any screen; always the cursor line on GLOBAL and SYSTEM and the hot cell on PROJECT; never under a question, the REC ring, a layer's map, NAME or the BLE list. An automated value says where its change comes from (LOCK MOTION LFO-A/B LFO2-A/B ENV2-A/B ENV1 MACRO); a DEST names what it moves ("ANALOG CUT - Filter cutoff", "T1 S1 FILT - ..."); an action that asks says "(asks)". The words are `tools/param_help.json` (one table with the web editor's tooltips; kind `op` holds this UI's own lines, found by row name then screen name, then by the value's descriptor), packed in flash (`ph_blob.c`) and unpacked onto the stack when drawn; each fits 232 px (`gen_param_help.py` checks). Code: `op_help.c`. The band is reserved whether or not the line shows (section 3.6).

## 13. Favorites

A shared store (builder item `FAVORITES`, `core/favorites.c`, saved in the settings record): a factory sound by engine and name (64 marks), a user preset by slot (32). Marked and browsed where presets are picked: **SAVE + PRESETS** marks the sound playing (right) or unmarks it (left), toast *FAV + NAME*; **SHIFT2 + SELECT** browses only the favorites (right, *FAVORITES ONLY*) or every sound (left); a PRESETS toast puts a star after a favorite. PROJECT > USER: SHIFT2 + ITEM marks the slot. Synth tracks only; none on the drum track, STEP, GLOBAL, SYSTEM or MASTER. The mark is written once the audio is quiet; the list starts at ALL after a power cycle.

## 14. Tests

The host suite is `tests/ui_optimist_test.c` plus the `tests/ui_optimist_*.h` it includes (one per area: algo, mixer, glo, names, layout, geometry, colours, lamp, plines, risk ...), run by `tests/run_tests.sh` / `tools/optimist.py test` in several switch sets (default, with the sequencer extras, on a real section log with PATTERNS, with PAGE_LINES, with and without the help line, without DUST). They check keys and knobs to actions, every question and toast within its box, every value on every screen has a form, no blit reaches the help band, no title is cut, 20,000 random frames. The host renders are `build/host/optimist/opt-*.ppm`. `tests/auto_test.c`, `auto_render.c` and `auto_int8_check.c` cover the store.

## 15. Open limits

- The web editor's SOLO / MUTE command (104, `web/EDITOR_PROTOCOL.md`) still offers a solo this UI does not have.
- The 0.1 BPM FINE tempo was not built; the clock counts whole BPM.
- A track's recorded motion keeps running while the track is sequencer-muted. A lane's own LFO / LFO 2 / ENV2 values are not recordable or lockable.
- A muted key does not sound as a preview (it is silent).
- FM6's algorithm picture overlaps its digits on deep stacks in SOUND's band; the layer's full panel draws them clean.
