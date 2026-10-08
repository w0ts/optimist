# Optimist UI: design (the second UI on the pluggable seam)

Status: **design, nothing built** (2026-10-08). Written from a brainstorm with the user on 2026-10-08; the user's
rulings are in section 0. The study it rests on is docs/UI-FEASIBILITY.md (the seam); the sequencer facts come from
docs/PATTERNS-DESIGN.md (patterns, scenes, the song) and the SLOOP 2.4 ports (seq/stepx.h, seq/seq24.c).

Labels: **[M]** measured (a file and line, a tool run, a build), **[E]** estimate (how it was made is said), **[P]**
proposal (for the user to accept or change), **[D]** the user's decision (section 0), **[O]** open.

## 0. Decisions (the user, 2026-10-08)

| Question | Decision |
|---|---|
| Scope | **a new UI** on the pluggable seam, chosen at build time beside SLOOP's (`ui/sloop`), not a rework of SLOOP's layers |
| Other UIs | **none**: "multi ui is replaced by implementing our own ui" (the user, 2026-10-08, in another session): exactly two UIs, SLOOP's and ours, a builder choice; no X0X or Felucca UI ports, and only the seam those two need |
| Look | **Felucca 1.0's** (section 3): flat cards, the panel, circled numerals, its font and icons; the grammar is ours |
| YES / NO | **SAVE = YES, HOME = NO** |
| The three left encoders | **SELECT = the cursor, ALGORITHM = the track, PRESETS = the value** of the hot cell; it changes the sound only where the value *is* the sound (the SOUND row), "we don't want to change preset all the time" |
| The step sequencer | **the 16 keys are the 16 steps**, on drums and synths alike; a pattern longer than 16 steps: **HOME + OCT- / OCT+ scroll the window of 16** ("logically"); OCT alone keeps its job |
| Picking the drum sub-track (the lane) | **"the current mode's button, held, + a key"**: on the mixer and on every screen where the keys play, **HOME held + a key** (the key plays the sound); on STEP, where the keys are steps, **SEQ held + a key**. One lane for the whole device |
| HOME in combination | **HOME held + anything = clear it**: a step + HOME clears the step, a knob + HOME clears the cell's event, REC + HOME clears the track, a scene or pattern key + HOME clears it. HOME tapped alone is NO: it answers a dialog and goes back; while a dialog asks, the keys do nothing |
| Locks and motion | **one store, two ways to edit it**: a step held + a knob writes a lock, REC + a knob while playing records motion, both into one list per pattern (section 6) |
| SAVE and HOME with a button | the user: "yes or no + button could be a shift", then "SAVE cannot be a shift... it should be used to save something (preset, pattern, project)". So: **SAVE held + a page button saves what that button owns**, nothing else; **HOME held + a page button clears or switches off** what it owns; section 2.1 proposes the table [P] |
| SONG REC | "we have a rec button": recording the song is **SAVE + REC** (section 2.1), not SAVE + PLAY |
| Undo / redo | the user's proposal: **SAVE then HOME = undo, HOME then SAVE = redo**; the order of the two tells them apart [P] |
| Lock a layer | **HOME + the layer's button, in either order** (the layer held then HOME tapped, or HOME held then the layer's button): the layer stays open hands-free, any button lets it go. So HOME with a layer button never means "off" |
| Scenes on the keys | **16 scenes on the 16 keys, no bank** |
| The pattern layer's keys | [O] "hard to say before I try on hardware": the built black-key modifiers against the uniform launch / store / clear vocabulary, decided on the FM-1 (it arrives 2026-10-08) |
| EDIT + OCT as a second undo / redo | **keep both**: SAVE then HOME / HOME then SAVE, and SLOOP's EDIT held + OCT- / OCT+ ("edit left / right, clever"); the user asked for a point to clarify on it (open question 12) |
| Names | **Felucca's NAME screen**: projects and user presets get names typed with the keys; kits keep numbers |
| The pick on SOUND | **HOME + a key only**; the sound buttons' hold is not a pick |
| Per-step chance | **"it should be just plock/motion data"**: chance is an event of the automation store (section 6), on drums and synths alike; no step bit is needed |
| FOLLOW on STEP | **follows the playhead by default**; HOME + OCT- / OCT+ scroll left / right and stop the follow; **HOME + OCT- + OCT+ together resumes it** |
| Every per-step extra as an event | **yes: one list of 128 events a pattern** holds nudge, fill, chance, locks and motion ("but we might come back to it"); SLOOP 2.4's stepx record becomes an import / export form |
| EDIT held's knobs | **kept**: SHIFT · LENGTH · TRANSPOSE on EDIT held, erase on the keys |
| Fills and automation | **hold events apply through a fill, step-only events are skipped with their step** |
| Typed notes on STEP | **no**: the keys and the pick are enough |
| SEQ tapped on STEP | **toggles the keys between steps and playing** |
| Nudge | **a few percent slower / faster while OCT- / OCT+ is held, back on release**; the BPM value never changes |
| The mixer's drum column | **the whole drum track, the sum of the 16 sounds**; "we might want a way to switch to the Drum mixer displaying its 16 channels (in blocks of 4??)": section 4.1's DRUM MIXER |
| The DRUM MIXER | entered by **the pick: HOME + a drum key on the mixer** opens it on that sound's block; its rows are **"like other tracks"**: the mixer's rows, "-" where a sound has no such value; "maybe we can evaluate a compacted mode with 8 or 16 on a screen (switching would be an option in the main settings)" [O, question 15] |
| Macros (COLOR MOTN SPACE ENRGY) | parked (open question 16) |
| Visualiser | **not SLOOP 2.4's styles** ("useless vis"): instead **an oscilloscope view for the master and per track, and levels on the mixer with the compressor's effect shown, a bar pushing down** (sections 4.1 and 4.10) |
| Play the song | **SAVE + PLAY plays the song from the start; PLAY alone stops** |
| Store the loop into scene n | SAVE held + REC held + key n, three fingers: "can't really answer without trying", to confirm on the hardware (open question 18) |
| Tempo (SELECT is no longer the tempo) | **a tempo page held open by PLAY**, with nudge on OCT- / OCT+, fine tuning, MIDI sync (section 4.8: "looks good") |
| Next | this document, then review |

## 1. What exists today (the facts the design rests on)

| Thing | Where | Facts [M] |
|---|---|---|
| Panel | ui/panel.c:9-14 | 14 buttons (FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+), 7 encoders (SELECT ALGORITHM PRESETS KNOB 1..4). **No encoder push**: the map has none |
| Keys | OPTIMIST.md, "The drum track" | 16 white keys F3..G5, the black keys between them; the drum track plays one sound per white key |
| Screen | ui/sloop/ui_song.c:2, :37 | 240 x 240; the canvas holds at most 124 rows, screens draw in bands |
| SLOOP's UI | ui/sloop, 8,958 lines; UI-FEASIBILITY §1.2 | tap = pages, hold = a layer (keys and knobs change job), hold + HOME = lock; 58,195 B of code and icons [S], FONT_L dropped since (the large font from the small one, 2026-10-08) |
| The ISR's key routing | seq/seq.c:66-84 | `ly_bit[]` per layer, `layer_now()` (the held function button in LY_* order, else `ly_lock`), `dyn_bit` (OCT: ghost / hard), `lk_q` (layer keys to the UI); KB_GRID (the drum grid's keys with no layer, FELUCCA_DRUM_STEP) |
| PRESETS today | ui/sloop/ui_input.c:1213 | a turn loads a sound **only on the browse page and TRACKS**: "elsewhere a stray turn would throw away the sound being edited" |
| Pages | core/params.c:488 | a table of {title, family, scope, graph, 4 param ids}: about 45 rows with every switch; VIEW ALL shows a family as rows of four values, the lit row is the one the knobs edit |
| Patterns, scenes, song | PATTERNS-DESIGN §11.1-11.4 | 16 patterns a track in the section log, scenes A..P with a pattern reference per track, the chain of 64 parts; `pat_launch(track, slot, when)`, `pat_store_slot`, `pat_copy`, `scene` launch and store are core calls; the PATTERN layer (LFO held) is built in `ui/sloop/ui_pat.c` |
| Step extras | seq/stepx.h | per track 176 B as SLOOP 2.4 keeps them: `micro[64]` (a nudge, 1/64 step), `lock[24]` {step, param, int16 val}, `fill[16]` (2 bits a step); stored as three counted lists |
| Locks | seq/seq24.c:58-65 | on its step the parameter takes the value, **back at the next step without a lock on it**; a knob turned meanwhile wins; the order ruled: motion, then fill, then lock, then macros |
| Motion | seq/motion.c:19-22 | an event is **3 bytes** {place, param, int8 value}: "every recordable value fits a signed byte"; **holds until the next event**, the patch comes back at the pattern restart and at STOP; 64 events; with PATTERNS a pattern carries its own 64 |
| Chance | seq/chance.c | a synth step's chance lives in bits 2..6 of `step_t.flags`; the drum step has no free bit |
| Recording | OPTIMIST.md "Recording", REC MODES | live, latency-compensated, overdub; free take; tempo and count-in modes; a take closes at a pattern switch |
| Confirm idioms today | OPTIMIST.md | four: AGAIN within 3 s (sections, patterns), a second detent (STORE, SNAPSHOT), the 2 s ring (REC held clears a track), "turn to GO" (NEW) |
| The seam | UI-FEASIBILITY §4.1 | six steps; 1..4 are zero-behaviour refactors (notifications, model ops out of the UI, the entry interface, params.c split), 5 is the build-time choice `FELUCCA_UI`, 6 the keyboard contract |

**Felucca 1.0.1's UI** (`~/GitHub/refs/Felucca`, GPL-3.0-only, Leo Kuroshita; 5,838 lines of ui*.c) [M]:

- One screen structure (ui.c:40-47): the header y 0..24, **four cards** 28..72 (57 x 44 px at x 3 + 59 c, one per
  KNOB), **the panel** 76..198 (the graphs, lists, the drum grid, the piano roll; the MIXER draws four columns
  there), the footer 202..240 (the steps, the engine, the preset, key hints).
- Pages as data (params.c:303): 37 rows of {title, family, scope, graph, 4 ids}, the same shape as ours.
- The look (ui.c:3-6, ui_draw.c:3-8): "flat SURF cards and panels on the palette's background, no rules, one type
  family (Inter Tight, three sizes), tracks named by circled numerals"; rounded corners, theme tokens only, lazy
  redraw with a signature per element, rolling digits on value changes; a list's selected row is a THEME bar with
  INK text.
- Dialogs (ui.c:109-112, ui_input.c:773): one confirm kind (`CF_*`), "OCT- cancels, OCT+ does it; nothing else
  reacts"; a NAME screen types with the keys (ui_name.c).
- The MIXER page (ui_graph.c:1001-1006): four SURF columns, one under each card, each with the circled numeral,
  a REC / ARM / MUTE badge, the sound's short name, a LEVEL knob with the output meter beside it, PAN and REV
  knobs. The knobs are the tracks: the same reading as our colour code (KNOB 1 is track 1, blue).
- The font: `tools/gen_aa_font.py --preset inter-tight` generates 4-bit alpha glyphs, Huffman-packed, with
  sub-pixel phases and kerning. **Run here: S 12 px 16,537 B, M 15 px 19,427 B, L 28 px 5,818 B, 41,782 B of
  tables in all** [M, 2026-10-08, the script on the clone's own TTF]. The renderer that draws them (its gfx.c) and
  the knob-ring masks (`gen_aa_keycaps.py`) are not measured. Licences: Inter Tight SIL OFL 1.1, Fukiai icons MIT
  (its LICENSING.md:39-45).

## 2. The grammar

Every screen is a **list of rows**. A row has up to **four cells**; the cells of the cursor row are the four cards,
and the knobs edit them. Nothing has to be held to edit. The rules:

| Control | Always means |
|---|---|
| **SELECT** | the cursor: which row. In a list screen (sounds, scenes, slots) the cursor row is the item |
| **ALGORITHM** | the track, T1 T2 T3 DR. Every screen retargets to it, the colour follows |
| **PRESETS** | the value of the **hot cell**, one unit a detent (the fine path). The hot cell is the cell last touched by a knob, drawn white as today. On the SOUND row the value is the sound, so that is the one row where a turn changes the sound (today's gate, ui_input.c:1213, generalised). In a list, it scrolls the list |
| **KNOB 1..4** | the four cells of the cursor row; a turn makes its cell hot |
| **YES** (SAVE tapped) | act on the cursor row: enter a ▸ row, toggle an on / off cell, **do** an action cell, confirm an armed action |
| **NO** (HOME tapped) | back one level; at the root nothing. Cancel an armed action |
| **HOME held + anything** | **clear it** [D]: + a step key on STEP, the step (its notes, events and extras); + a knob, the hot cell's event (a step held) or the cell back to its default; + REC, the selected track's pattern, YES confirms; + a scene or pattern key in its layer, that scene or pattern. HOME held alone does nothing (the menu is a SYSTEM row) |
| **a page button, tapped** | jump the cursor to that family's rows (ENV: the envelope rows of SOUND); tapped again: its next row |
| **HOME held + a key** where the keys play, **SEQ held + a key** on STEP | **pick** [D], "the current mode's button + a key": on the drum track the lane (the key plays the sound, so you hear what you picked); on a synth track, on STEP, the note or chord the next tapped steps carry. The selected lane is one value for the whole device, like the selected track: the mixer's drum column, SOUND and STEP all show it. On STEP the keys are steps, so there HOME + a key clears and the screen's own button picks |
| **a page button, held** | the performance layers, as today (section 4.9) |
| **OCT- / OCT+** | synth tracks the octave, the drum track ghost / hard (as today), on STEP too; **HOME + OCT- / OCT+ on STEP: scroll the 16-step window** over a longer pattern [D] |
| **SAVE then HOME · HOME then SAVE** | **undo · redo** [D]: the order of the pair tells them apart; **EDIT held + OCT- / OCT+** do the same, as SLOOP (open question 12 holds a point to clarify) |
| **PLAY, REC** | transport, as today, in every layer |

Three consequences:

- **One confirm idiom.** A destructive action (store over a used slot, clear, NEW, LOAD) arms and the header says
  what YES will do: *CLEAR T2? YES*. NO or 3 s cancel. The four idioms of today go; the live clear of a track is
  HOME + REC, then YES, instead of the 2 s ring.
- **Messages** stay as today (the header, 2.5 s): *STORED T2 5*, *T2: NO FREE PATTERN*, MISSING.
- **Lock a layer** is no longer needed for editing: editing never needs a held button. For the performance layers
  (FX with both hands) the gesture is **HOME + the layer's button, in either order** [D]: the layer held then HOME
  tapped (today's), or HOME held then the layer's button. The layer stays open, any button lets it go. HOME with a
  layer button therefore never means "off" (section 2.1).

### 2.1 The two held buttons: SAVE saves, HOME clears [P]

The user (2026-10-08): "yes or no + button could be a shift", then "SAVE cannot be a shift... it should be used to
save something (preset, pattern, project)". So **SAVE held + a page button saves what that button owns**, nothing
else; **HOME held + a page button clears** what it owns, or, when the button's hold is a layer, **locks the layer
open** [D]. There is no positive shift: latching lives on its row (ARP's HOLD). Keys are not shifted
this way: SAVE held + keys stays the scene layer, HOME held + keys the pick or the step clear (section 2). The
order of the two held buttons matters only for SAVE and HOME together: SAVE then HOME is undo, HOME then SAVE is
redo.

| Button | SAVE + it: **save** | HOME + it: **clear / off** |
|---|---|---|
| **SEQ** | **STORE** the selected track's working pattern into its slot (the explicit store of PATTERNS-DESIGN §2.4; *AGAIN* becomes *YES?* over a used slot) | clear the pattern's automation and extras, the notes stay |
| **REC** | tapped: **SONG REC** on / off ("we have a rec button"): the song is saved as you play it. Held + a key n: **save the loop into scene n**, every track's pattern and the scene (today's "save the loop into the section"; the playing scene's own key updates it) | **clear the selected track's pattern**: notes, events, extras (section 2; YES confirms) |
| **PLAY** | **play the song from the start** (song mode on; PLAY alone stops) [D] | stop, and every voice off at once: the panic (CC120's effect) |
| **ENV · LFO · EDIT · FX · SCL · ARP** (a sound page button) | **save the sound as a user preset** (the slot, the NAME screen); on the drum track, **save the 16 lanes as a user kit** | **lock that layer open** [D]: FX, ARP, SCL, EDIT (erase), LFO (patterns), ENV on an FM6 track; ENV elsewhere: INIT, the preset back over the edits (YES confirms) [P] |
| **GLO** | — | **lock the mix layer open** [D] |
| **HOME · SAVE** | SAVE then HOME: **undo** | HOME then SAVE: **redo** (the user's proposal) |
| **OCT- · OCT+** | — | on STEP: **scroll the 16-step window** over a longer pattern [D]; elsewhere the octave back to 0 (today: both OCT together) [P] |

What this gives: every save on one button, every clear or lock on the other, each on the button whose job it
modifies, with no screen needed. Everything in the table exists in the core today (`pat_store_slot`, the section store, SONG REC,
the user preset and kit stores, undo, the FX bypass, INIT SOUND) and only gains a gesture. Snapshots stay on their
PROJECT row.

## 3. The look [D]: Felucca's

Felucca 1.0's screen structure carries the grammar as it is:

```
 y   0..24   header: the screen's name, track + engine colour, tempo, bar.beat, transport, MIDI / USB, a message
 y  28..72   four cards = the cursor row's four cells = KNOB 1..4 (label, value, unit; the hot cell white)
 y  76..198  the panel: the list of rows (the cursor row a bar), or a grid (STEP), or a graph (SOUND), or columns (the mixer)
 y 202..240  footer: the keys' current meaning, YES / NO hints ("SAVE open  HOME back"), the steps' playhead
```

What we take from Felucca [P]: the layout above, the SURF cards and panels, the theme tokens (ours already name
colours by meaning, tools/colors.json), the circled numerals, the rolling digits, the list with a bar cursor, the
confirm dialog, the NAME screen (projects and user presets could get names; kits keep numbers), the MIXER columns
with meters and rings, the ADSR / LFO / FX / scale graphs. What we do not take: its gestures (OCT+ does, OCT-
backs; SAVE held = undo; HOME held = menu): ours are section 2.

**The font is the cost.** 41,782 B of tables [M] plus the anti-aliased renderer [E: a few KB] against our 1-bit
Terminus (its size after the 2026-10-08 flash diet is not in these numbers). The new UI replaces SLOOP's 58 KB of UI
code [S], so there is room in principle, but the user-default profile was 13.2 KB from the limit on 2026-10-07
(BUILDER.md:449) before this morning's -21.8 KB diet and the batches that landed with it. **The first gate of phase 1
is a measured build with the three faces and the renderer** (section 8). The fallback, as a builder choice: the same
screens drawn in Terminus, with the M and L sizes as the S glyphs scaled, which is what SLOOP's UI does now.
Terminus S stays in every build whatever the choice: main.c's UBOOT and RESTORED screens, the OTA screens, recovery
and dual.c draw with it (UI-FEASIBILITY §1.2), so the Felucca faces come on top of it, not instead of it.

## 4. The screens

### 4.1 HOME is the mixer

HOME (NO at the root) shows the mixer. The **columns are the tracks**, as Felucca's MIXER page and as our colour
code: KNOB k is track k on every row. SELECT picks the parameter row.

| Row | KNOB 1 .. 4 (= T1 T2 T3 DR) | Notes |
|---|---|---|
| **MASTER** (the cursor rests here) | TEMPO · SWING · LEVEL · FILT | the one row whose cells are not the tracks: the master. TEMPO here is the one-tap tempo until section 4.8 is decided |
| LEVEL | the four levels | the drum column is the whole drum track, the sum of its 16 sounds [D]; the sounds have the DRUM MIXER below |
| PAN | the four pans | the drum track has no pan today: "-" |
| FX | on / dry x 4 (YES toggles the hot cell) | GLO + black keys 1..4 does the same live |
| DRIVE | DIST x 3, the drum track "-" (its sounds' DRIVE is per sound) | |
| REV · DLY · CHO (three rows) | the sends x 3; the drum column "-" (the drums send per sound: the DRUM MIXER) | HOME held + a drum key on the mixer picks the sound (the key plays it) and opens the DRUM MIXER on its block [D] |
| FILTER | the track filter x 4 (when built) | |
| SOUND ▸ | the four tracks' sound names; PRESETS browses the selected track's | YES: the SOUND screen |
| FX ▸ · SONG ▸ · PROJECT ▸ · SYSTEM ▸ | | YES enters; the page buttons are the shortcuts |

The panel: four columns (Felucca's), each with its numeral, the sound or kit name, the engine colour, the level
ring with a meter beside it (`track_t.peak` exists), the M / S / REC badges, and a 16-step playhead strip at the
bottom of the column (the TRACKS screen's steps, narrowed). Mute and solo stay on the GLO layer, performance
gestures; the badges show them. **A fifth, narrow master column** on the right [P] carries the master meter with
**the compressor's gain reduction as a bar pushing down from the top** [D] (the COMP / LIMIT pages' GR readout,
`G_CGR`), so the mixer shows the compressor at work; the track meters are plain, the tracks have no compressor.

**The DRUM MIXER** [D]. The user: "we might want a way to switch to the Drum mixer displaying its 16 channels (in
blocks of 4??)". A second mixer for the drum track: the 16 sounds as columns, **four at a time**, KNOB 1..4 the
block's four lanes (KICK KICK2 SNARE CLAP, then the hats and the rim, the second snare and the toms with the crash,
the ride, shaker, conga and cowbell). **Entry: the pick** [D]: HOME + a drum key on the mixer opens it on that
sound's block with the sound highlighted; the same pick moves inside it; HOME + OCT- / OCT+ scroll the block, as
STEP's window; HOME (NO) goes back to the mixer. **Rows "like other tracks"** [D]: the mixer's rows, with "-" where a
sound has no such value: LEVEL (the sound's, SOUND 2), PAN "-", FX "-", DRIVE, REV · DLY · CHO (its sends, SOUND 3),
FILTER "-"; CUT may take PAN's place [P]. The panel: four columns with the sound's name and source colour (drum
synth, sampled, X0X, your sample), the level ring, and a flash on each hit as the kit page's pads.

**A compact view to evaluate** [O, question 15]: "a compacted mode with 8 or 16 on a screen (switching would be an
option in the main settings)", a SYSTEM setting DRUM MIXER: 4 / 8 / 16 columns. The knobs still edit the block of
four, drawn wider and lit; the rest are narrower bars. Room [E]: the panel is 220 px wide (Felucca's PANEL_W), so 4
columns get 55 px as the cards do, 8 about 27 px (a short name fits), 16 about 13 px (a bar and a two-letter
abbreviation at most). Worth a mock-up on the emulator before deciding.

### 4.2 STEP, one screen for drums and synths

SEQ tapped. **The keys are the 16 steps of the page shown**, nothing held (the ISR's `ly_lock` LY_STEP, or KB_GRID,
already routes keys to the UI with no layer button). **HOME + OCT- / OCT+ scroll the window** over a longer pattern:
1..16, 17..32, 33..48, 49..64 up to the track's LEN, a page at a time [P]; the header says *steps 17-32*. OCT alone
keeps the octave and ghost / hard, so the pick can be auditioned. FOLLOW (the window follows the playhead, as
FELUCCA_DRUM_STEP) is the default while playing [D]; a scroll turns it off, and **HOME + OCT- + OCT+ together
resumes it** [D].

**The keys' lights** [P]: on STEP the set steps of the window are lit (the selected lane's on the drum track) and
the playhead's key blinks, as FELUCCA_DRUM_STEP lights them today; with the keys toggled to playing, the played
notes as KEYLIT; in a held layer its map, as today.

The panel on the drum track: the 16 lanes x the 16 steps of the page, levels as shades, ratchets as notches
(today's DRUMS grid), the selected lane highlighted. On a synth track: the page's 16 steps as a mini roll (notes
stacked, ties as lines, the chords of CHORD), as Felucca's GR_ROLL.

| State | Cards (KNOB 1..4) | SELECT | PRESETS | YES | HOME |
|---|---|---|---|---|---|
| no key held, drums | the cursor lane's sound: LEVEL · TUNE · DECAY · REV | the cursor lane (the slow pick) | the hot cell | open the lane's SOUND rows | tapped: back. Held + a lane's key: clear the lane's steps [P] |
| no key held, synth | the pattern: LEN · DIV · SWING · GATE | the rows: PATTERN, ARP | the hot cell | — | tapped: back |
| **a step held** | drums: LEVEL · RATCHET · — · —; synth: NOTE · LEVEL · RATCHET · LENGTH (ties, as today) | **NUDGE** (micro, -32..31) | **CHANCE**, drums and synths alike (an event of the automation store, section 6) | cycle the fill condition: normal, FILL, NO FILL | **HOME + the step: clear it** (notes, events, extras) |
| **a step held + a page button** (ENV, LFO, FX, SCL, ARP) | that page's four cells **as the step's locks**: a cell with an event shows a lock mark; a turn writes one (section 6) | the family's next row | the hot cell, fine | toggle HOLD of the hot cell's event | HOME + the knob: clear the hot cell's event |
| **SEQ held + keys** | — | — | — | — | — |

SEQ held + keys is the **pick** [D] (the screen's own button: on STEP the keys are steps, and HOME + a step clears
it): on drums a white key picks the lane and plays its sound; on a synth the keys play and the notes (one, or
several for a chord) become what the next tapped steps carry. Letting SEQ go keeps the pick. A tapped empty step takes the pick
(today's rule: "the sound shown / the note you played last"); a set step tapped is cleared; a held step is edited
and kept, several held together (as today).

What a step key does when nothing is held, in short: tap an empty step, it is set with the pick; tap a set step, it
is cleared; hold it and turn, it is edited. SEQ tapped again toggles the keys back to playing with the STEP screen
still up (to audition while the grid follows the playhead); SEQ again, steps [D].

The erase-as-it-plays gesture (MPC style: hold the sound's key while the pattern plays and its hits leave) stays
**EDIT held + the key**, as today, on every screen: a performance gesture, not an edit.

### 4.3 SOUND

The selected track's pages as rows, in today's PAGES order (params.c:488): SOUND (the preset, the engine, INIT,
SAVE AS), ENV, ENV DEST, ENV2, ENV2 DEST, LFO, LFO DEST, EDIT 1, EDIT 2, OSC 2, SWARM, FLT 2, VOICE, VOICE 2,
FX (the sends), FILTER, SLICER, SCL, SCL 2, ARP, ARP 2, PATTERN. The panel draws the cursor row's graph (ADSR,
LFO, the FX bars, the scale), as Felucca. The page buttons jump: ENV to ENV, LFO to LFO, EDIT to EDIT 1, FX to FX,
SCL to SCL, ARP to ARP; tapped again, the family's next row.

The SOUND row: cell 1 the preset (PRESETS browses by kind, as today; the list in the panel), cell 2 the engine,
cell 3 INIT (an action: YES), cell 4 SAVE AS (an action: YES, then the NAME screen [D]).

On the drum track the rows are the **selected lane's**: SOUND, SOUND 2, SOUND 3 (the sends), SOURCE, KIT, then
the track rows it has today (FX FILTER, SLICER, PATTERN). HOME held + a key picks the lane (the keys play on
this screen, so you hear it). On an FM6 track ENV held stays the operator editor as built
(ui_fm6.c): its own screen, the black keys OP1..OP6 PIT GLO MONO POLY.

### 4.4 FX (the global effects)

FX tapped from SOUND's FX row, or HOME's FX ▸: DLY, REV/CHO, REVERB (TYPE), MASTER (DUST DUCK FILT ROLL), COMP,
LIMIT, MACRO, DRUMS (level). Graphs as today where there is one.

### 4.5 SONG: scenes, patterns, the chain

| Row | Cells | YES | REC | Notes |
|---|---|---|---|---|
| scene A .. P (16 rows) | T1 · T2 · T3 · DR: the pattern slot each track plays in it (*3*, *keep*, *--*); **PRESETS edits the hot cell's reference** | launch at the next bar; stopped: load | **store the loop into it** (today's SONG screen gesture; *AGAIN* becomes *YES?* over a used one) | HOME + REC: clear it. The playing scene green, a queued one amber, a changed one with a dot (PATTERNS-DESIGN §6.3) |
| PATTERNS | T1 · T2 · T3 · DR: each track's playing pattern, a turn cues the next / previous stored one | — | store the working copy into the first free slot (DUPLICATE) | **the panel is the 4 x 16 session grid** of PATTERNS-DESIGN §6.2 and the keys launch the selected track's 16 patterns while the cursor is on this row; **SAVE held + key n stores the working copy into slot n, HOME held + key n clears it**; copy a to b = launch a (stopped: it loads), then SAVE + b [P] |
| part 1 .. 64 (the chain) | SCENE · BARS · INSERT · DELETE | the actions | — | the cursor part plays white while the song runs |
| MODE | LOOP / SONG · SONG REC · MEM gauge | toggle, start / stop | — | |

Launching stays on the held layers for performance (section 4.9): SAVE held = scenes, LFO held = patterns.

### 4.6 PROJECT

Rows: PROJECT (SLOT · LOAD · SAVE · NEW), SNAPSHOT (SLOT · LOAD · CLEAR · SAVE), USER PRESET (SLOT · LOAD · ERASE ·
SAVE), TOOLS (CLEAR SEQ · INIT SOUND · MISSING · NEW), SLOOP 2.4 (the import of its slots and autosave, as built),
and the MEM gauge. **An action cell does nothing when turned**: YES does it, with the confirm when it destroys. This
replaces "a second detent arms" everywhere. The panel lists the slots with their names, sizes and dots (the
SNAPSHOT list as today). Projects and user presets are named on Felucca's NAME screen when saved, the keys typing
the letters [D]; kits keep numbers.

### 4.7 SYSTEM

Today's HOME menu, as rows: SCREEN (COLOR BRIGHT ZOOM VIEW), LIGHTS (LIGHTS KEYS NOTES), AUDIO (LOWCUT / BASS+ ...),
MIDI (OUT IN SYNC CLOCK), CHANNELS (T1 T2 T3 DR), USB (SERIAL), CPU, CALIBRATION (an action), ABOUT. The menu
sections of 2026-10-07 map one to one.

### 4.8 TEMPO, a held page [D]

The user: "we could have a tempo page (holding a button) where we would also have nudge (OCT+/-), fine adjust,
MIDI sync etc.", and on the page below: "looks good". **PLAY held** (past the hold threshold; a tap stays start /
stop):

| | |
|---|---|
| cards | BPM · FINE (0.1) · SWING · SYNC (AUTO INT USB TRS) |
| OCT- / OCT+ held | **nudge** [D]: the clock runs a few percent slower / faster while held and comes back when let go; the BPM value never changes (DJ beat-matching; a no-op while following an external clock) |
| a white key | tap tempo (two taps or more) |
| SELECT | the rows: TEMPO, REC (MODE · LENGTH · START · CLICK: the REC MODES screen's dials) |
| the panel | the beat as a big number, the clock source it follows (A:USB ...), the RX light |

Why PLAY: it is the one button whose hold does nothing today, and tempo is transport. The alternative is GLO held
(the mix layer: it has tap tempo on key 16 already), which would put the levels and the tempo on one hold. The
nudge needs a core addition: a transient tempo multiplier applied in the sample-accurate clock (fx.c BEAT_U), never
written to G_BPM.

### 4.9 The performance layers (held), kept from today

| Hold | Keys | Knobs | Change from today |
|---|---|---|---|
| **FX** | the 16 punch-in effects | FILTER · DUST · DUCK · — | none |
| **ARP** | note repeat on the grid | RATE | none |
| **SCL** | the key of the song | CHORD · SCALE · KEYS · TRANSPOSE | none |
| **GLO** | 1-4 mute, 5-8 solo, 9-12 FX on / off or fills (as built), 16 tap tempo | the four levels | none |
| **EDIT** | erase as it plays (that sound / note leaves the pattern while held) | SHIFT · LENGTH · TRANSPOSE · — | none [D] |
| **HOME** | **+ a key where the keys play: pick the drum sound** (the key plays it); **+ a step on STEP: clear it**; **+ REC: clear the track** (YES confirms) | + a knob: the cell's event, or the cell to its default | the pick and the clear modifier [D]; HOME tapped alone is NO |
| **SAVE** (= YES held) | **1..16 launch scene A..P** at the next bar (one bank of 16, no OCT bank; an 8-section build uses keys 1..8) [D]; **REC tapped: SONG REC** on / off; **REC held + key n: store the loop into n** (three fingers, to confirm on the hardware); **HOME held + key n: clear n**; **PLAY: play the song from the start** [D]; the quick chain (tap several) as built | — | today: 1-4 launch, 5-8 store, 13 loop / song, 14 song rec, 16 the song page, OCT the bank. The uniform vocabulary: *layer + key = launch, layer + REC + key = store, layer + HOME + key = clear* |
| **LFO** (patterns) | **1..16 launch pattern n** of the selected track at its end (OCT- held: next bar, OCT+ held: now, as built); **REC held + key n: store** the working copy into n; **HOME held + key n: clear** n [P] | track k's next / previous stored pattern | [O] decided on the hardware: this uniform vocabulary (ALGORITHM is the track, copy is a SONG row action, duplicate is the PATTERNS row's REC), or the built black-key modifiers (6 store, 7 copy, 8 clear, 9 duplicate, 1..4 the track), or both |
| **SEQ** | the pick on STEP (section 4.2) | — | today's step layer is the STEP screen |
| **ENV** on FM6 | the operator editor, as built | | none |
| **PLAY** | the TEMPO page [D] (section 4.8) | | new |

### 4.10 SCOPE [P]

The user, on SLOOP 2.4's visualiser styles: "useless vis. I would prefer to have an osc view for master, per track,
and levels on mixer with compressor effect". So no visualiser styles in this UI (the VIS builder item stays
SLOOP-UI only), and a SCOPE screen in the grammar: the rows are the sources, MASTER, T1, T2, T3, DR; the panel is the
oscilloscope of the cursor row's source; the cards are that source's mix values (a track: LEVEL · PAN · the FX
on / dry · DRIVE; the master: LEVEL · FILT · COMP threshold · the GR readout). Entry: a SCOPE ▸ row on HOME, and
**HOME tapped at the root toggles the mixer and the scope** [P] (open question 17).

What the core has for it: Felucca's `scope_buf` is gone (no hit in firmware/src at aaa284f [M]); what exists is
SLOOP 2.4's visualiser tap, FELUCCA_VIS (backports24.h, voice.c; one block copy of the mix into an 8 KB int32 ring
per the 2026-10-08 report), whose 12 styles this UI does not use. The scope reuses that tap and ring, switched by
the main loop to the cursor's source (the master or one track, the ISR copying that track's block instead of the
mix): one ring, not five [E]; four always-on taps would cost four rings.

## 5. Recording

- **Live ("real record")**: REC as today. Playing: records the selected track at once, REC again stops. Stopped:
  arms; the first note starts the loop, or the free take on an empty project, or the count-in (REC MODES). A take
  ends at a pattern switch (PATTERNS-DESIGN §5.4). Overdub, latency-compensated, chords and ties, as today.
- **Step entry**: section 4.2.
- **Automation**: section 6. Two gestures, one store.
- **Undo / redo**: SAVE then HOME, HOME then SAVE, and EDIT held + OCT- / OCT+ as SLOOP [D]; many levels as
  today; a pattern switch is a level (built).
- **The song**: SAVE + REC records the scenes you launch and their bars (SONG REC); pattern launches in it come
  later (PATTERNS-DESIGN Q11 [D]).

## 6. One automation store, two gestures [D]

### 6.1 The event

Felucca's motion event, 3 bytes, with one bit given a meaning:

```
 byte 0  place: bits 0..5 the step (0..63), bit 6 STEP-ONLY (0 = hold: a motion event; 1 = this step only: a lock), bit 7 reserved
 byte 1  the parameter (our P_* id)
 byte 2  the value, a signed byte
```

Bit 6 is 0 on every motion record written so far (`place` was track << 6 | step in the shared store; in a
pattern's own list the track bits are free): **today's records read unchanged as hold events**. A list holds up to
**128 events per pattern** [D], 384 B (today: 64 motion events, 24 locks, and the nudge and fill arrays).

| Gesture | Writes | Bit 6 | Plays as |
|---|---|---|---|
| a step held + a knob (STEP, with a page button for the sound pages) | an event on that step | 1 | its step only; the parameter goes back at the next step without an event on it (seq24.c lock_step) |
| REC + a knob while playing | an event on the step playing (the next one past its half, as motion.c) | 0 | until the next event of that parameter, or the pattern restart; STOP puts the patch back (motion.c) |

**Chance is an event** [D] ("it should be just plock/motion data"): a step-only event whose parameter is the
pseudo-parameter CHANCE, an id past P_COUNT that is never a sound value, on drums and synths alike. The synth step's
chance bits (chance.c) stay readable, an event wins over them, and the new UI writes events only. The drum step gets
chance with no format change.

**Nudge and the fill condition are events too** [D]: the pseudo-parameters NUDGE (-32..31, in 1/64 of a step) and
FILL (1 FILL ONLY, 2 NO FILL), step-only. One list then holds every per-step extra, and SLOOP 2.4's stepx record
(seq/stepx.h, 176 B a track) is only the import / export form: its micro, lock and fill arrays become events on
read and are rebuilt from the events on export. The user: "we might come back to it", if the size or the
sequencer's scan bites. The sequencer reads a step's NUDGE and FILL in the same scan as its values; micro timing
needs the next step's nudge ahead of time, as `micro_units` reads it from the array today, so the scan is per step,
never per sample [E].

The STEP screen shows an event as a dot under its step in the lane's or the roll's colour; a hold event with a tail
to the next event of that parameter. With a step held and a page button down, a cell with an event on that step
shows a lock mark and its value; the hot cell's event: YES toggles bit 6, HOME + the knob clears it. HOME + the step
clears every event of the step with its notes.

### 6.2 The sequencer

One `auto_step(t, idx)` replaces `motion_step` and `lock_step`: the events of this step apply in list order; a
step-only event saves the base it found (`lk_base`, as today) and lets go at the next step without one; a hold event
sets the value and leaves it. The two kinds on one parameter: the step's event wins on its step; the base a
step-only event returns to is whatever a hold event left (today's ruling: "the lock lets go to the motion value"). A
fill-skipped step: its step-only events do not apply (2.4), its hold events do [D: a recorded sweep keeps moving
through a fill, as motion ignores fills today].
Order kept: automation, then fill, then macros. The ISR work is one list scan a step instead of two.

**Check before building [O]**: every lockable parameter must fit a signed byte. Motion's set does (motion.c:19).
The lock set adds SGATE, the SLICER values, E0..E7, TFLT and the ANALOG 2 ids (seq24.c:69-80): a one-off script over
TP[] / the engines' edit descriptors (min, max) decides; a value outside int8 keeps 2.4's int16 lock form for that
id, or the id is not lockable.

### 6.3 Storage and the edges

| Where | Change |
|---|---|
| the pattern record (PATTERNS-DESIGN §2.1) | the motion chunk (flag bit 0: n, n x 3 B) **is the list**, n up to 128; the extras chunk (PF_SX) is written empty, and an older record's micro, locks and fills are merged into the list on read |
| the working project (autosave) | motion_proj's MOTN record the same way; the stepx autosave record merged on read, written empty |
| snapshots, backups | carry the records as they are: nothing to change |
| SLOOP 2.4 import | its micro, 24 locks and fills a track become events (step-only; a lock value clamped to int8 says so in the import report) |
| SLOOP 2.4 export | nudges and fills go back to their arrays, the first 24 step-only sound events a track become its locks; hold events and chance are not exported and the report says *MOTION NOT IN 2.4* |
| Felucca motion | reads as it is |
| the web editor | one AUTO_GET / AUTO_SET pair replaces the lock commands 72..77 and the motion page (a later protocol version) |
| a phase 1..3 PATTERNS firmware reading a new record | reads a lock as a motion event (it holds instead of reverting): a semantic change on downgrade, no data lost |

RAM [E, from the struct sizes]: four working lists of 128 x 3 B = 1,536 B, against today's 200 B shared motion
store plus 4 x 176 B of stepx (704 B): about +630 B; the stage's copy the same.

## 7. What the core must provide (beyond the seam)

| Need | Today | Change [E] |
|---|---|---|
| the seam | UI-FEASIBILITY §4.1 steps 1..4 | prerequisite, zero-behaviour (~900 lines moved) |
| **the selected lane**, one value for the device | `dl_ui_pick` (drum_edit.c:28), `ui.lane`: UI state | a core value next to `song.sel`, set by the pick, read by the mixer's drum column, SOUND and STEP; ~20 lines |
| **keys as steps with no held button** | `ly_lock` LY_STEP, KB_GRID (seq.c:84-90) | the STEP screen sets it; the SEQ-tapped-again toggle clears it |
| **the pick gesture**: HOME held + keys (where the keys play) and SEQ held + keys (on STEP) go to the UI *and* play the sound | layer keys are silent (keyboard_block) | a per-layer "keys also play" flag; HOME held and SEQ held become pick layers whose keys the UI drains from `lk_q`; UI-FEASIBILITY step 6's contract extended |
| **HOME held as the clear modifier** | HOME held is the menu | on STEP the keys are the UI's already (steps), so HOME + a step is UI-side, as HOME + a knob and HOME + REC; the menu moves to a SYSTEM row; EDIT held stays LY_ERASE |
| **YES / NO** | — | UI state (the armed action, 3 s), the actions as core calls (seam step 2) |
| **the scope's audio tap** | FELUCCA_VIS's ring (the mix, one block copy per ISR block); `scope_buf` no longer exists | the same ring, switched to the cursor's source (the master or one track); the 12 styles not built with this UI; the mixer's GR bar reads `G_CGR` |
| **the automation store** | motion.c, seq24.c, stepx.h | section 6: one list of 128 events replaces motion.c's store and seq24.c's lock, micro and fill arrays; ~400 lines changed, the records' readers, the 2.4 edges, tests |
| **the tempo page's nudge** | the clock (fx.c BEAT_U) | a transient multiplier, ~30 lines; a no-op when following a clock |
| PRESETS as the fine value | `preset_go`, the param set with clamp | the UI calls the same core set with ±1 |
| scene store from a row, pattern copy from a row | `sections` store, `pat_copy` | exist |

## 8. Budget [E] and what to measure first

| Item | Bytes | Source |
|---|---|---|
| SLOOP's UI code and icons, which the new UI replaces at build time | **-58,195** | UI-FEASIBILITY §1.2 [S] |
| the new UI: one list renderer, one grid, the cards, the mixer columns, the dialogs, the screens | +30,000 .. +45,000 | [E: Felucca's ui_draw + ui_graph + ui_input + ui.c are 4,260 lines; SLOOP's UI is 8,958 lines for 58 KB] |
| Inter Tight tables, three faces | **+41,890** | [M, 2026-10-08: a scratch user-default build of aaa284f with Felucca's text renderer and the three faces linked in: S 16,573, M 19,463, L 5,854] |
| the anti-aliased text renderer (decoder, phases, kerning) | **+1,464** of code, **+1,250 B of RAM** (the Huffman lookup tables 1,032, the ramp cache 218) | [M, the same build; the whole port: **+43,608 B flash, +1,248 B RAM**, pool and RAMTEXT unchanged; the build ends 41,016 B over the slot from a baseline 2,592 B under] |
| the ring masks, the Fukiai icons (not ported) | +2,000 .. +6,000 | [E] |
| Terminus S: stays, the core screens draw with it; only SLOOP's scaled large size goes | 0 | UI-FEASIBILITY §1.2 |
| **net** | about **+20 .. +40 KB** against the SLOOP UI build | [E] |

**Measured 2026-10-08** (aaa284f, `tools/optimist.py build --profile user-default --measure`, the port in the
scratch tree `fontm/` of this session's scratchpad): user-default is **2,592 B under the slot** (578,972 of
581,564); with Felucca's text renderer and the three faces it is **41,016 B over**. So the Felucca look on
user-default pays for itself only through what the new UI drops: SLOOP's 58 KB of UI code and icons [S] against
the new UI's own code (30-45 KB [E]) plus the 43.6 KB measured here, which leaves user-default **roughly 13-28 KB
over [E]**. Ways out, to decide at phase 1 with the skeleton's real size: the Terminus fallback as the builder's
other choice; the S and M faces only (36 KB, the big numerals drawn as M scaled); the Felucca look on the profiles
with room (x0x-drums had 40 KB free on 2026-10-07) and Terminus on user-default; or a sample set traded for it.
The renderer itself is cheap (1.5 KB of code); the tables are the cost, and they were measured, not estimated.

RAM: the canvas is shared (59,520 B of pool, UI-FEASIBILITY §1.2); the new UI's own state is a few hundred bytes
[E]. CPU: the UI runs in the main loop only; the XIP cache risk of UI-FEASIBILITY §4.2 applies: every screen must
pass the emulator CPU budget test.

## 9. Phases

Each phase ends with the firmware working, `tests/run_tests.sh` green, every profile linking and fitting, and a
commit for review. Effort in focused agent-days [E].

| Phase | Content | Gate | Effort |
|---|---|---|---|
| **0** the seam | UI-FEASIBILITY steps 1..4: `core_say` / `core_dirty`, `model.c`, the entry interface, params.c split | sizes within ±64 B, every golden, ui_pages_test's 185 checks | 2 |
| **1** the skeleton | builder choice `FELUCCA_UI` (step 5); `ui/optimist`: the row / cards / panel / footer renderer in Terminus, HOME mixer, SOUND rows from PAGES, PROJECT, SYSTEM, YES / NO and the confirm; **the font measurement** (section 8) | a UI test (keys and encoders to actions, every message fits), emulator screenshots for review, the measured sizes | 3 |
| **2** STEP | keys as steps, OCT pages, the pick layer (core), the step cards, the lock gesture on today's two stores, the layers kept | ui tests, sl24_seq_test and patterns_seq_test unchanged | 2.5 |
| **3** the automation store | section 6: the event, `auto_step`, the records, the edges, the int8 check, the STEP marks | host tests: both kinds on one parameter, fills, a 2.4 round trip, a Felucca motion record, every pattern test | 2.5 |
| **4** SONG and the layers' vocabulary | the scene and chain rows, the PATTERNS row's grid, SAVE / LFO layers as section 4.9, the TEMPO page once decided | patterns_ui_test rewritten for this UI, song tests | 2 |
| **5** the look | Felucca's gfx text renderer, the three faces, the rings, the icons, as a builder choice; profiles set | measured sizes per profile, screenshots, the CPU budget test | 2 |
| **6** docs and the editor | an OPTIMIST.md chapter per screen; the editor needs no protocol change for the UI itself (AUTO_GET / SET comes with phase 3's protocol version) | | 1 |

## 10. Open questions

1. **Tempo**: decided, PLAY held, the nudge a few percent while held (section 4.8). The MASTER row keeps TEMPO
   as the one-tap path [P].
2. **Synth note entry on STEP**: decided, the pick and the last notes played; no typed path.
3. **Drum chance**: decided, chance is an event of the automation store on every track (section 6.1).
4. **Lock a layer**: decided, HOME + the layer's button in either order.
5. **Scenes on the keys**: decided, 16 on 16 keys, no bank; an 8-section build uses keys 1-8.
6. **The LFO layer**: the uniform *launch / REC store / HOME clear* vocabulary against the built black-key
   modifiers: to be tried on the hardware (arriving 2026-10-08).
7. **Names**: decided, Felucca's NAME screen for projects and user presets; kits keep numbers.
8. **FOLLOW on STEP**: decided (section 4.2).
9. **Fill-skipped steps**: decided, hold events apply, step-only events are skipped with their step.
10. **EDIT held's knobs**: decided, SHIFT · LENGTH · TRANSPOSE stay on EDIT held.
11. **The pick on SOUND**: decided, HOME held + a key only.
12. **Undo / redo**: decided, both the SAVE / HOME chord and EDIT held + OCT- (undo) / OCT+ (redo). **A point to
    clarify, at the user's request**: the exact behaviour of EDIT + OCT on the drum track, where OCT alone is
    ghost / hard (today EDIT + OCT is undo there too, the hits are not affected), and whether EDIT + OCT inside the
    erase layer undoes the erase just made or the level before it.
13. **Every per-step extra as an event**: decided, one list of 128 events a pattern; revisit if the size or the
    sequencer's scan bites.
14. **The DRUM MIXER's entry**: decided, the pick from the mixer; its rows are the mixer's.
15. **The DRUM MIXER's compact view**: 8 or 16 columns on one screen as a SYSTEM setting (section 4.1): evaluate
    with a mock-up on the emulator; 16 columns leave about 13 px each.
16. **The macros** (COLOR MOTN SPACE ENRGY), parked: a second MASTER row on HOME, the FX screen's MACRO row only,
    or not in this UI.
17. **SCOPE** (section 4.10): the entry (a row, or HOME at the root toggling mixer and scope), and one switched tap
    or four always-on ones for the per-track views.
18. **Storing the loop into scene n with three fingers** (SAVE + REC + key): to confirm on the hardware; the
    alternatives are SAVE + key held, or SAVE + REC tapped for the playing scene only.
