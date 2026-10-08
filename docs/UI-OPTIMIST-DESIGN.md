# Optimist UI: design (the second UI on the pluggable seam)

Status: **design; phase 1 (the skeleton), 1b (the review's rulings), 2 (STEP), 4 (the layers, TEMPO, SONG) and the loose ends (NAME, FM6, 11.4) built**
(2026-10-08, section 11). Written from a brainstorm with the user on 2026-10-08; the user's
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
| Track colours (review of phase 1, 2026-10-08) | **"The track should be coloured after the instruments"**: a track is always drawn in its engine's colour (the drum track: its kit's kind), tools/colors.json, wherever this UI shows it: the mixer's strip, the header's badge, the cursor bar on SOUND, a track named in a question. There is no colour per track number or per knob |
| Questions (review of phase 1) | **"make them more visible, in the middle, like a modal popup"**: a confirm is a box over the panel (the cards stay above), the verb big, its target named big (a track in its colour), "HOME no" on the left and "SAVE yes" on the right at its bottom, as the buttons sit (the user, after the 1b screenshots; the footer does not repeat them); a red frame when it destroys or replaces the work (clear, erase, an overwrite, a load), amber otherwise. The result of an action just confirmed is a small toast in the middle; passive status (MISSING, RECORDING, REC OFF) stays in the header |
| Values (review of phase 1) | **"almost no place should have a number value with no graph representation"**: every value is drawn with a form beside its number (section 3); names stay text |
| A page button on SOUND (phase 2) | **only that family's rows**, "why I see env2 and slicer on the lfo screen?": LFO shows the LFO rows and their graph, ENV the envelopes...; the button again its next row, round; HOME > Sound keeps every row (section 4.3) |
| The mixer's cards (phase 2) | **none**: "on the mixer view, no top four cards; instead we highlight fader, the pan, etc. We should scroll with SELECT from volume to pan, to send, etc."; the strips take the height, the control SELECT is on lit on all four, the master values as the walk's last row, the selected track's strip framed in its colour (section 4.1) |
| A shortcut to SONG (review of phase 4) | **SAVE held + SELECT turned** (11.3's proposal 1c): the SONG screen; built in section 11.4 |
| Graphs on a family's rows (emulator review of phase 4) | **"LFO and ENV should always display their shape"**: every row of the ENV, ENV2 and LFO families shows its family's graph, the DEST rows too (section 11.4) |
| Which engine a preset is (emulator review of phase 4) | **"when I select a preset, I don't see which engine preset it is"**: a preset is always shown with its engine, in the engine's colour (section 11.4) |
| The footer (emulator review of phase 4) | **no footer anywhere**: its two lines were noise on a small screen; the useful hints move into the header, the cards or the modal (the new screens of 11.4 draw none; the removal from the other screens is feat/ui-drummix's, section 11.5) |
| The look (review of phase 4) | **"I actually like our look at the moment"**: phase 5 (Felucca's font and faces) is dropped for now |

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
  knobs. The knobs are the tracks: the same reading as ours (KNOB k is track k); the colours are not per knob:
  each track is drawn in its engine's colour (section 0).
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

- **One confirm idiom.** A destructive action (store over a used slot, clear, NEW, LOAD) arms and a **modal box over
  the panel** asks what YES will do [D, section 0]: *CLEAR T2?* in the large font, the track in its engine's colour,
  *HOME no* / *SAVE yes* at its bottom (as the buttons sit), the frame red when it destroys or replaces the work, amber otherwise. NO or
  3 s cancel. The four idioms of today go; the live clear of a track is HOME + REC, then YES, instead of the 2 s ring.
- **Messages**: what a confirmed action says (*T2 CLEARED*, *SAVED*, *STORED T2 5*) is a **small toast in the middle**
  of the panel [D]; passive status (*T2: NO FREE PATTERN*, MISSING, RECORDING) stays in the header (2.5 s).
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

**Every value has a form** [D, the review of phase 1]: no value is shown as a bare number. One small set of forms,
chosen from the value's range alone (its descriptor's min, max and kind: `param_desc_t`), never per parameter:

| Value | Form | Where |
|---|---|---|
| a number, min 0 | a bar filled from the left | the card under the number, the list row under the number (2 px), the mixer's sends |
| a number, min < 0 < max (PAN, FILT, TRANSPOSE, the ENV DEST amounts, a drum TUNE) | a bar from the centre | the same; the mixer's PAN and FILT |
| a list of up to 12 (WAVE, SYNC, a palette) | a row of segments, the current one lit | the same |
| a longer list (an engine, a kit, a slot) | a tick at its place on a line | the same |
| on / off (FX, SERIAL, MUTE) | a pill, filled when on | the same; the mixer's FX |
| a name (a preset, a sound) or an action | text | |

**Text casing** [D, the user, 2026-10-08]: "sentence case for UI words, capitals for short labels". Screen titles,
row names, messages, toasts, the modal's question and target, and the hints are sentence case (*Mix master*, *Save?*,
*Project 1*, *Clear DR?*, *T1 cleared*, *Keys play T1*, *Home back*); the cards' short labels (5 letters or fewer:
ATK, BPM, FILT), track names (T1, DR), acronyms (FX, LFO, ENV, MIDI, USB, BPM, CPU ...), any word with a digit, and
units keep their capitals like printed panel legends; preset, kit and lane names stay as stored. The casing is done
when this UI draws (ui/optimist `op_case`), so the shared tables and the core's messages keep their capitals.

On SOUND the cursor row's picture is drawn over the list where the core has one: the envelope (ENV, ENV2), the LFO's
wave as it runs, the FX slots' sends as needles under their cards (SLOOP's graph maths, copied), the scale on an
octave of keys (the root white), the pattern's steps, the drum lane's name in its source's colour. Felucca's
270-degree rings are phase 5's (with its font); phase 1b draws bars (section 11.1).

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

HOME (NO at the root) shows the mixer. The **columns are the tracks**, as Felucca's MIXER page: KNOB k is track k
on every row, each column in its track's engine colour (section 0: no colour per knob). **The mixer has no cards**
[D, the user, 2026-10-08: "on the mixer view, no top four cards; instead we highlight fader, the pan, etc. We should
scroll with SELECT from volume to pan, to send, etc."]: the four strips take the screen's height, and **SELECT walks
the strips' controls**, the same control lit on the four strips at once (in each track's colour, framed; the hot
strip's frame white, where PRESETS acts finely), the others dim; the header names it (*Mix volume*, *Mix pan*).
**The selected track's strip** (ALGORITHM) is framed in its colour with its head tinted [D], so the lit control and
the selected track read together.

| Row (SELECT's walk) | KNOB 1 .. 4 (= T1 T2 T3 DR) | Notes |
|---|---|---|
| **VOLUME** (the cursor rests here) | the four levels: the faders | the drum column is the whole drum track, the sum of its 16 sounds [D]; the sounds have the DRUM MIXER below |
| PAN | the four pans | the drum track has no pan today: "-" |
| REV · DLY · CHO (three rows) | the sends x 3; the drum column "-" (the drums send per sound: the DRUM MIXER) | HOME held + a drum key on the mixer picks the sound (the key plays it) and opens the DRUM MIXER on its block [D] |
| DRIVE | DIST x 3, the drum track "-" (its sounds' DRIVE is per sound) | |
| FILTER | the track filter x 4 (when built) | |
| FX ON | on / dry x 4 (YES toggles the hot cell) | GLO + black keys 1..4 does the same live |
| SOUND ▸ | the four tracks' sound names (lit on the strips); PRESETS browses the selected track's | YES: the SOUND screen, every row |
| FX ▸ · SONG ▸ · PROJECT ▸ · SYSTEM ▸ | | past the end of the walk; YES enters; the page buttons are the shortcuts |

**No master values on the mixer** [D, the user, phase 4: "in the mixer view remove the bottom 4 cards please, let's
make better use of the space"]: the MASTER row (BPM · SWING · LEVEL · FILT at the strips' foot) left the walk; BPM
and SWING are the TEMPO page's (PLAY held, section 4.8), FILT DUST DUCK the FX layer's knobs and the FX screen's
MASTER row, and the master LEVEL is the analog knob (its read-out went). **PAN at the foot** [D, the user, phase 4:
"put the pan all at the bottom"]: in each strip the PAN form is drawn last, under the sends, DRIVE, FILTER, FX and
the steps, as a console's pan; SELECT's walk keeps its order (VOLUME, PAN, the sends ...).

The strips (Felucca's columns, the screen's height): the numeral, the M / S / REC badges, the sound or kit name, the
fader with its meter beside it (`track_t.peak` exists), a row a control as its form (FILTER from the centre, the
sends and DRIVE bars, FX a pill), the 16-step playhead strip (the TRACKS screen's steps, narrowed) and PAN, from the
centre, at the foot. Mute and solo stay on the GLO layer, performance gestures; the badges show them.
**A fifth, narrow master column** on the right [P] carries the master meter with
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
LFO, the FX bars, the scale), as Felucca. **A page button tapped shows only its family's rows** [D, the user,
2026-10-08: "why I see env2 and slicer on the lfo screen?"]: LFO the LFO and LFO DEST rows (*Sound LFO*, its graph),
ENV the envelopes and their DEST rows, FX the track's effects (FX, FILTER, SLICER ...), EDIT the engine's EDIT rows,
SCL and ARP theirs (the families are PAGES' `fam`); SELECT moves within the family, the button again goes to the
family's next row and from its last to its first. **The whole list stays on HOME > Sound** (the mixer's SOUND row):
every row, the SOUND row first. On the drum track the page buttons narrow the lane's rows the same way where a
family has rows there (EDIT: the lane's SOUND rows); a family with none there (ENV) leaves the rows as they are.

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

## 11. Phase 1: what was built (feat/ui-optimist, 2026-10-08)

Branch `feat/ui-optimist`, on optimist 024737d (rebased from aaa284f once feat/patterns-left landed). Builder item
**UI, bit 251, a choice, default 0** (UI group, EXPERIMENTAL): 0 = SLOOP's UI, 1 = this one. Every profile keeps 0.

**The seam** (UI-FEASIBILITY §4.1, the part this phase needs; zero behaviour, the SLOOP build byte for byte):

- `core/model.c`: moved verbatim from `ui/sloop/ui.c`: the core's forward declarations the UIs use, `user_of`,
  `up_gen`, `sync_reload`, `FELUCCA_VERSION`, the power-on sounds (`TRK_DEF`, `trk_def_*`), `param_kept`,
  `apply_preset_to`, `set_engine_of`, `apply_preset`, `set_engine`, `track_defaults(_steps)` and the curated preset
  list (`BANK`, `bank_resolve`, `preset_pos`, `preset_at`, `preset_kind`, `preset_name`). Step 2 of the seam.
- `drums/dsnd_desc.c`: moved verbatim from `ui/sloop/ui_drums.c`: the drum lanes' SOUND page values (`DSD`,
  `dsnd_desc_lane`, the SRC names) and the kit list (`drum_kit_step` and its kin), which the web editor's `ed_dsrc.c`
  reads too; and the new core value **`lane_sel`**, the selected drum lane, one for the device (section 7).
- `felucca.c` includes both before the UI and picks `ui/optimist/optimist.c` or SLOOP's file list with `FELUCCA_UI`.
  The rest of the core's calls into the UI (`ui_say`, `ui_message`, `ui_say_st`, `ui.force`, `ui.menu`, `ui.page`,
  miss.c's `ui.msg`, `track_select`, `go_home`, `layers_init`, `panel_setup`, `ui_input`, `ui_leds`, `ui_draw`) the new
  UI provides under the same names: seam steps 1 and 3 (`core_say`, `core_dirty`, the entry interface) are not done.
- ui/sloop's diff: the two moves (322 + 163 lines out), one comment line in ui_drums.c, nothing else.

**The UI** (`firmware/src/ui/optimist/`, 1,962 lines, every file in SIZE_FILES): `optimist.c` (the file list),
`op_state.c` (the state, messages, the confirm), `op_cells.c` (cells; the rows made of PAGES), `op_screens.c` (HOME,
SOUND, FX), `op_project.c` (PROJECT, SYSTEM, the screen table), `op_draw.c` (the renderer), `op_input.c` (the panel,
the entry points). A screen is five functions (rows, a row's name, a cell, a turn, a YES); a cell is a label, a value,
a unit and a kind (a value, a read-out, an action, a row to enter), most of them straight from a descriptor
(`page_desc`, `TP`, `GP`, `bps_desc`) and `param_format`.

| Screen | Rows | Notes |
|---|---|---|
| HOME (MIX) | MASTER (BPM SWING LEVEL FILT), LEVEL, PAN, FX, DRIVE, REV, DLY, CHO, FILTER (when built), SOUND ▸, FX ▸, PROJECT ▸, SYSTEM ▸ | the cards: KNOB k = track k (T1 T2 T3 DR in their engine colours); the panel: four columns with the name, the fader, the meter (`meter_ui_take`), M / S / R badges and the 16 steps playing |
| SOUND | the SOUND row (PRESET ENGINE INIT SAVE AS; the drum track: KIT LANE), then the track's PAGES in their order as `page_shown` / `page_for_drum` leave them | the drum track's rows are the selected lane's (`lane_sel`) |
| FX | the global FX and GLO pages (DLY, REV/CHO, REVERB, CMP, SLOTS, GLOBAL, MASTER, COMP, LIMIT, MACRO, DRUMS) | section 4.4, built because it costs only its row filter |
| PROJECT | PROJECT (SLOT LOAD SAVE NEW), SNAPSHOT (SLOT LOAD CLEAR SAVE), USER (SLOT LOAD ERASE SAVE), TOOLS (the TOOLS page: CLRSQ INIT MISS NEW), SLOOP 2.4 (IMPORT) | a slot lit when used, dim when empty; snapshots in their status colours |
| SYSTEM | SCREEN (COLOR BRIGHT), LIGHTS (LIGHTS KEYS), AUDIO (LOWCUT), MIDI (OUT IN SYNC CLK), CHANNELS (T1 T2 T3 DR), USB (SERIAL), CPU (LOAD CLOCK), CALIBRATE (PANEL), ABOUT (VERSION) | saved to flash when SYSTEM is left (once stopped, as SLOOP's menu) |

The grammar as built: SELECT the row (stops at the ends), ALGORITHM the track, PRESETS the hot cell one unit a
detent (on the SOUND row the hot cell is the preset: the one row where it changes the sound; on the mixer's SOUND row
it browses the selected track's sounds), KNOB 1..4 the cells (a turn makes its cell hot; an action cell only becomes
hot). SAVE tapped = YES (enter, toggle an on / off value, do an action, confirm), HOME tapped = NO (cancel, back to
the mixer; at the root nothing). HOME held + a knob: the cell to its default. HOME held + REC: *CLEAR T2? YES*. HOME
held + a drum key: the lane (the key plays). SAVE then HOME: undo; HOME then SAVE: redo; EDIT held + OCT- / OCT+ too.
ENV LFO EDIT FX SCL ARP SEQ tapped: their rows of SOUND, again the family's next; GLO: the FX screen. PLAY and REC as
today (REC records the selected track, stopped it arms). One confirm: LOAD, NEW, CLEAR, ERASE, INIT, RESET, a GO and
a SAVE over a used slot ask in the header, YES does it, NO, another row or 3 s let it go.

Tests [M]: `tests/ui_optimist_test.c`, in `tests/run_tests.sh` with three switch sets (default; the backports with
ACID, BRIGHT, BASS+, MOTION, MACROS; LIGHTS, USB SERIAL, the track FILTER, PLOCK): every screen's rows on every engine
and on the drum track (names fit the panel, labels a card, a value has its value), keys and knobs to actions,
the confirm (asked, YES, NO, 3 s, another row, a SAVE into an empty slot not asked), the undo / redo chords
(neither a YES nor a NO), every question and message within the header (232 px), 20,000 frames of random use; 48 checks. Host
renders: `build/host/optimist/opt-*.ppm`. All of `tests/run_tests.sh` green, `builder_test.py` green.

Emulator [M] (fm1-emulator `play_check`, 96 MHz, the user-default build with UI=1): boots to the mixer; the
screenshots `build/ui-optimist-shots/*.png` (git-ignored) show HOME (MASTER, LEVEL, playing), SOUND (its SOUND row,
ENV, the lower rows, the drum lane's rows), PROJECT (and *NEW? YES*), SYSTEM, *CLEAR DR? YES* and the undo message.
The emulator CPU budget test was not run.

**Sizes** [M] (optimist 024737d + this branch, `tools/optimist.py build --measure`; the slot is 581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| 024737d (before) | 579,988 | 80,728 | 307,376 | 30,752 |
| this branch, UI=0 | 579,988 | 80,728 | 307,376 | 30,752 |
| this branch, **UI=1** | **526,004** (-53,984; 55,560 free) | **77,816** (-2,912) | 307,376 | 30,680 (-72) |

The other profiles with UI=1 [M]: drum-machine 525,344 (-47,032 against its check build), x0x-drums 501,592
(-47,764), fm-va-studio 517,932 (-54,968), everything-that-fits 514,060 (-58,760; its RAMTEXT is over the 32,512 B
limit with either UI: 33,812 with UI=0, 33,768 with UI=1, a fact of that profile, not of this phase). costs.json:
UI=1 = -51,560 B flash, -2,928 B RAM against the all-defaults build.

**The font gate** (section 8): with the skeleton, user-default has 55,560 B free; the Felucca text renderer and its
three faces measured 43,608 B [M, section 8], so the Felucca look would leave about 12 KB [E: the two numbers added,
not built together] before the phases 2..4 code.

**Not built** (later phases, or not asked for phase 1): STEP and the keys as steps, SONG, the held performance layers
(only PLAY and REC work; FX ARP SCL GLO held do nothing), the automation store, the Felucca font and look, the NAME
screen, the graphs in SOUND's panel (it shows the list), the DRUM MIXER, SCOPE, the TEMPO page, the master column with
the compressor's GR bar, the pick on synth tracks, SAVE + a page button (save) and HOME + a page button (clear / lock),
the keys' lights of KEYLIT and the drum pads, motion of the MACRO values.

**Decisions taken without the user** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| The core's calls into ui/sloop: move or alias | moved the model operations (core/model.c, drums/dsnd_desc.c), the seam's step 2; the notifications and entry points are provided by the new UI under SLOOP's names (`ui` with `force`, `menu`, `page`, `msg`...) instead of seam steps 1 and 3 | seam steps 1 and 3 replace the shared names; `git revert` of the refactor commit undoes the moves |
| Code duplicated in ui/optimist because the original touches SLOOP's `ui` | `project_new`, `rec_toggle`, `panel_setup`, `dsnd_set`, `dsnd_tick`, `msg_status`, `undo_say` (about 200 lines) | after seam step 1 move them to the core and delete both copies |
| SLOOP files the new UI includes | `ui/sloop/ui_colors.c` (the colour language) and `ui/sloop/bright.c` (the backlight): shared helpers, no `ui` state | move them to `ui/` |
| The builder item | key `UI`, flag `FELUCCA_UI`, bit 251 (the next after 250 on every branch), a choice 0 / 1, default 0, EXPERIMENTAL | registry.py |
| Messages | 2.5 s for every message (today's `ui_say` is 40 frames, ~0.7 s; only MISSING stays 2.5 s) | `OP_MSG_FRAMES` |
| SAVE, HOME and the page buttons | act when let go, and only when nothing else was pressed, turned or played while down (how a tap and a chord are told apart; a held page button will be its layer) | act on press for the page buttons while no layer exists |
| The hot cell when the row changes | back to cell 1 (so PRESETS on the SOUND row is the preset again) | keep the column |
| PRESETS on a value | one unit a detent, no acceleration (the fine path); the knobs accelerate as today | `accel()` in the PRESETS path |
| The mixer's SOUND row | the four names as read-outs; PRESETS browses the selected track's sounds; the knobs do nothing there | knob k browses track k |
| The drum track's SOUND row | KIT (the kit list, as PRESETS on SLOOP's TRACKS) and LANE (the slow pick) | another layout |
| SAVE AS | into the first free user preset slot (the NAME screen is later); USER PRESETS FULL when none | the NAME screen |
| What asks | LOAD, NEW, CLEAR, ERASE, INIT, RESET, ACID's GEN, the 2.4 IMPORT, a SAVE over a used project or user slot, every snapshot SAVE | the conditions in op_project.c / op_cells.c |
| The FX screen (section 4.4) | built (its rows are a filter over PAGES); GLO tapped opens it | drop the row and the screen |
| SYSTEM's rows | COLOR and BRIGHT only on SCREEN (ZOOM and VIEW set SLOOP's UI only), no NOTES (KEYLIT's lights are not in this UI) | add the items to `SYS[]` |
| MASTER's LEVEL | a read-out of the MASTER knob (there is no master level value to edit) | a value when one exists |
| The drum column on the mixer | the drum track's LEVEL (GLO > DRUMS), its FX bypass and FILTER; PAN, DRIVE and the sends "-" (per sound: the DRUM MIXER) | the DRUM MIXER |
| The keys while a question is asked | they still play (the ISR has no "keys do nothing" state) | a keyboard-block flag in seq.c |
| Motion recording | the SOUND rows' values record motion as SLOOP's pages do; the mixer's values do not (as SLOOP's TRACKS screen) | `page_set` / `val_turn` |
| LEDs | the screen's button (the row's family on SOUND), SAVE blinks while a question waits, PLAY on the beat, REC, OCT, the selected lane's key on the drum track, the LIGHTS backlight | `ui_leds` |

**Found in the spec** (not changed; the smallest reading taken): section 4.3 says the SOUND rows follow today's PAGES
order and then lists another order (EDIT before FX): PAGES' order is used. Section 4.1's MASTER row names a LEVEL that
does not exist as a value (the MASTER knob is analog): a read-out. Section 4.1's fifth, narrow master column has no
room beside four 57 px columns under the cards (236 of 240 px): not built. Section 2's "while a dialog asks, the keys
do nothing" needs an ISR change: not built. Section 2's messages "as today, 2.5 s": today most messages are shorter;
2.5 s for all was taken.

### 11.1 Phase 1b: the review's three rulings (feat/ui-optimist, 2026-10-08)

The user's review of phase 1 (section 0, the last three rows): tracks coloured after their instrument, the
questions as a modal popup, and a graphic for every value; then, on the 1b screenshots, the modal's hints in the
buttons' order and sentence case (section 3, *Text casing*). Built on the phase 1 branch, UI=1 only; UI=0 is untouched.

**What changed** (`firmware/src/ui/optimist/`, 2,448 lines; the new file `op_graph.c`, 287 lines, in SIZE_FILES):

- **Track colours.** Every place this UI shows a track draws it with `trk_col`: its engine's colour, the drum track
  its kit's kind (tools/colors.json): the header's badge, the mixer's strip (numeral, fader, forms), the SOUND
  cursor bar, the modal's target. There is no colour per track number; sections 2 and 4.1 say so now.
- **The modal.** A question is a box over the panel (the header and the cards stay): `draw_modal`, one 240 x 123
  canvas (gfx.c's 124-row band). The question in the large font on one line when it fits, *CLEAR DR?*, the target
  in its colour; when it does not (*SAVE PROJECT 1?*), the verb over the target (*SAVE?* / *PROJECT 1*). *HOME no* on
  the left and *SAVE yes* on the right at its bottom, as the two buttons sit on the panel (the user, after the first
  1b screenshots); the footer then says no yes / no of its own, only its KEYS line. A red frame when it destroys or replaces the work, amber otherwise (`op_arm`'s `danger`).
- **The toast.** What an action just confirmed says (*T2 CLEARED*, *SAVED*, *LOADED*, *NEW PROJECT*) is a small box
  in the middle of the panel for 1.5 s, framed in the colour of its words (green for CLEARED / SAVED ...), drawn over
  the live panel (the list or the mixer keeps drawing around it). Passive status (MISSING, RECORDING, REC OFF, the
  result of a SAVE into an empty slot, UNDO) stays in the header.
- **Forms.** One primitive set, `draw_gauge`, chosen from a value's range alone (`cell_gauge`: the descriptor's
  min, max and kind, never per parameter): a bar from the left (min 0), a bar from the centre (min < 0 < max: PAN,
  FILT, TRANSPOSE, the ENV DEST amounts, a drum TUNE), a row of segments with the current one lit (a list of up to
  12), a tick on a line (a longer list: an engine, a kit, a slot), a pill (on / off). On the cards under the number
  (4 px), on the list rows under each cell (2 px), on the mixer's strips (PAN and FILTER from the centre, the three
  sends, FX a pill lit while on), on PROJECT (the slots; a MEMORY row with the section log's USED % and MORE) and
  SYSTEM (the settings, the CPU load, the clock).
- **SOUND's graphs.** The cursor row's picture over the panel (58 px), the list below it three rows: ENV and ENV2
  (the envelope), LFO (the wave as it runs, from the core's `lfo_wave`), FX (the slots' sends as needles under the
  cards, grey when bypassed), SCL (the scale on an octave of keys, the root white) with SLOOP's maths copied from
  `ui/sloop/ui_draw.c` (`graph_adsr4`, `graph_lfo`, `graph_fx`, `graph_scale`; nothing in ui/sloop is called);
  PATTERN (the steps over the length) and the drum lane (its name large in its source's colour, the kit) are this
  UI's own drawings.
- **Casing.** `op_case` (op_state.c) cases a string as this UI draws it: the first letter kept, the rest lowered,
  except the words of an exception list (DR FX LFO ENV ENV2 MIDI USB BPM CPU OCT GLO ARP SCL UI CC ACID GEN MHZ)
  and any word with a digit (T1, 2.4, U03); a trailing "?" or "," does not hide a word (*Clear DR?*). Applied to
  the header (title and messages), the list's row names, the toast, the modal, the footer's hints (each hint its
  own sentence: *Save load  Home back*); `op_label` keeps a card label of 5 characters or fewer as printed and cases
  a longer one (*Engine*, *Save as*). The large face is FONT_S's glyphs at 2x but generated with capitals only
  (gfx.c folds a..z to A..Z), so the modal draws with `font_big`, a copy of FONT_L with FONT_S's full range, made
  in ui/optimist (the generated font that SLOOP's UI uses is untouched).

Tests [M]: `tests/ui_optimist_test.c` grows to **64 checks** (from 48), in the same three switch sets: the modal's
state (asked as the overlay, its verb, its target in the track's colour, red), the modal drawn (the red frame, the
question on one line, the band within 124 rows, HOME no left and SAVE yes right, no yes / no in the footer, the large face has lower case), the casing (ten
titles, messages and questions, the card labels' 5-letter rule, the helper within its buffer), no header repeating a word, the toast (instead of the header, green, the mixer drawn around it,
gone after 1.5 s, the panel back), every question fits the modal (verb and target, 208 px), every toast fits its
box, every value on every screen and engine and on the drum track has a form (only names, actions and range-less
read-outs are text), the screens use all five forms, and every form kind resolves to a primitive that draws at both
ends of its range. `tests/run_tests.sh` green, `builder_test.py` green, `tools/div_audit.py` green (the two
envelope divides listed).

Emulator [M] (fm1-emulator `play_check`, 96 MHz, the user-default package with UI=1, the method of phase 1):
`build/ui-optimist-shots/p1b-01-home.png` (the mixer playing, the strips' forms), `p1b-02-sound-env.png` (the
envelope), `p1b-03-sound-lfo.png`, `p1b-04-sound-drum-lane.png`, `p1b-05-modal-clear-track.png` (*CLEAR DR?*),
`p1b-06-toast-cleared.png`, `p1b-07-project.png`, `p1b-08-modal-save-over.png` (a SAVE over a used slot),
`p1b-09-toast-saved.png`, `p1b-10-modal-load.png`, `p1b-11-modal-new.png`, `p1b-12-system.png`; the host renders
`opt-*.png` (the modals of a track clear, NEW and a SAVE over a used slot, the toast). The script is the session
scratchpad's `emu/shots3.sh`. The emulator CPU budget test was not run.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`, UI=0 and `--set UI=1`; the slot is
581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| 024737d (before) | 579,988 | 80,728 | 307,376 | 30,752 |
| this branch, UI=0 | 579,988 | 80,728 | 307,376 | 30,752 |
| phase 1, UI=1 | 526,004 | 77,816 | 307,376 | 30,680 |
| phase 1b, UI=1 | 531,976 (+5,972) | 77,880 (+64) | 307,376 | 30,704 (+24) |
| optimist 924c7c2 (the branch rebased onto it, 2026-10-08) | 580,276 | 80,728 | 307,376 | 30,832 |
| this branch on 924c7c2, UI=0 | 580,276 | 80,728 | 307,376 | 30,832 |
| **this branch on 924c7c2, UI=1** | **532,296** (-47,980; 49,268 free) | **77,880** (-2,848) | 307,376 | 30,928 (+96) |

The rows above 924c7c2 were measured on 024737d; the base's own growth since (+288 B flash, +80 B RAM code) moves
both builds. Within 1b [M]: the forms, graphs, modal and toast 530,736 / 77,848 / 30,676; the hints' order and the footer
without yes / no -24 B flash; the casing +1,056 B flash, +32 B RAM (the large face's copy and its alignment),
+28 B of RAM code (.ram_hot 27,676 -> 27,704; ui/optimist has no RAM-placed code, the cause was not looked into);
the header's screen name once +208 B flash.

The font gate (section 8) with phase 1b on 924c7c2: 49,268 B free against the Felucca renderer and faces' 43,608 B [M] leaves
about 6 KB [E: the two numbers added, not built together] before phases 2..4.

**Decisions taken without the user** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| Toast or header | **a toast** for what a confirmed action says; **the header** for passive status and for the result of an action that did not ask (a SAVE into an empty slot, REC OFF, UNDO). A toast goes when the screen changes or a question is asked | `toast_next` in op_input.c `op_yes` |
| The modal's text | the whole question on one line in the large font when it fits 208 px, the target in its colour; else the verb over the target (the small font when the target is too wide for the large) | `draw_modal` |
| Red or amber | red: clear, erase, reset, INIT, NEW, LOAD, an overwrite (a project or user preset SAVE over a used slot, a user kit SAVE), ACID GEN, the 2.4 import; amber: a snapshot SAVE into an empty slot (the only question that destroys nothing) | the `danger` argument of `op_arm` |
| The modal's extent | over the panel only, the panel around the box black; no countdown drawn (3 s still let it go) | `draw_modal` |
| Casing: what is not cased | values (OFF, SIN, ANALOG, YES: the descriptors' names, as SLOOP shows them), preset, kit and lane names, the mixer's M / S / R, the boot and setup screens' names (op_input.c) | `op_case` callers |
| The header | the screen and its row as one sentence (*Mix master*, *Sound ENV*, *Project snapshot*); a row that starts with the screen's name stands alone (*Project*, *Sound*), so no header repeats a word (a host check over every screen and row); the font has no middle dot for *Sound · ENV* | `head_title` |
| Casing: the exception list | the user's (DR, T1..T3 through the digit rule, FX, LFO, ENV, MIDI, USB, BPM, CPU, OCT) and ENV2, GLO, ARP, SCL, UI, CC, ACID, GEN, MHZ; a message's lane name is cased with it (*Reset pedal hat*) | `case_keep` |
| The footer under the modal | its first line (the YES / NO hint) blank, its KEYS line kept: the keys still play while a question waits | `draw_foot` |
| The toast over the panel | the panel keeps drawing under it and the toast is drawn again on top when it does; the mixer's meters hold still while it shows (it covers two of them) | `op_frame_draw`, `draw_mixer` |
| Bars instead of rings | the ruling allows "a bar or a 270-degree ring": bars, because Felucca's rings need its anti-aliased renderer (phase 5) | `draw_gauge` |
| The number on the list rows | the same Terminus S as before ("the number small" has no smaller face in this build); the form is a 2 px bar under it | the font of phase 5 |
| What stays text | names (a preset, the mixer's SOUND row, a palette's name has its segments too), actions (YES), read-outs with no range (LIMIT > GR); the clock has a bar to 240 MHz, the CPU load to 100 % | the `cell_gauge` callers |
| SOUND's graphs | the pages with a graph in the core (ENV, ENV2, LFO, FX, SCL, PATTERN, the drum lane); none for VOICE, ARP, EDIT, SLICER, the ENV DEST rows; the list shortened to three rows under a graph | `has_graph` |
| PATTERN and the drum lane's graphs | this UI's own (SLOOP's `graph_steps` and `graph_dsnd` read its UI's cursor and lane state) | port them when STEP (phase 2) needs a cursor |
| PROJECT's MEMORY row | USED (% of the section log, amber over 90 %) and MORE (sections that still fit), counted twice a second at most; only with the section log | `prj_mem` |
| The mixer's strips | the fader and meter 44 px (were 60), under them PAN, REV DLY CHO, FX, FILTER (when built); the drum column's PAN and sends empty (per sound) | `draw_strip_forms`, `METER_H` |

**Found in the spec** (not changed; the smallest reading taken): the ruling's example puts the question and its
target on one line (*CLEAR DR?*) and also asks the target to be named: one line where it fits, two where it does not
(*SAVE?* / *PROJECT 1*). "Panel list rows: the number small": the build has one small face, so the number keeps it.
The 270-degree ring is unbuildable without the phase 5 renderer: the bar, which the ruling allows.

### 11.2 Phase 2: what was built (feat/ui-optimist, 2026-10-08)

STEP (section 4.2), and two rulings the user gave while it was built: SOUND's page buttons show their family only
(section 4.3) and the mixer without cards (section 4.1). UI=1 only; **no core change**: the SLOOP-UI build (UI=0) is
byte for byte as before (measured below).

**What changed** (`firmware/src/ui/optimist/`, 3,510 lines; new `op_step.c`, STEP's model and keys, and
`op_stepdraw.c`, its grid and roll, both in SIZE_FILES):

- **The keys as steps, with no core change.** STEP sets `ly_lock = LY_STEP` and gives `ly_bit[LY_STEP]` a bit no
  button has (bit 31), so seq.c's `key_down` sends every key to `lk_q` and plays nothing; the UI drains `lk_q`. The
  pick needs the keys to play and reach the UI: while SEQ is held (and while the keys are toggled to playing) the UI
  lets `ly_lock` go (LY_PLAY), the keys play as anywhere, and the UI reads them from `fm1_input_note_edges` as the
  mixer's HOME pick does. So section 7's "keys also play" flag in `keyboard_block` was not needed.
- **The window**: 1-16 .. 49-64 up to LEN, the header *Steps 17-32*; FOLLOW (the default) moves it to the playhead
  while playing and no step is held; HOME + OCT- / OCT+ scroll it and stop the follow, HOME + OCT- + OCT+ resumes
  it; OCT alone keeps the octave (synths) and ghost / hard (drums).
- **Step keys**: an empty step tapped is set with the pick at once; a set one is cleared when let go unless it was
  edited meanwhile (SLOOP's rule); HOME held + a step clears it all (notes, ties, nudge, locks, fill); a step held
  and HOME tapped clears the steps held. Every step key pressed opens an undo level (`undo_mark`, a new session).
- **A step held**: the cards are the step's (drums LEVEL · RATCHET · - · -, the selected lane's hit; synths NOTE ·
  LEVEL · RATCHET · LENGTH, the ties as SLOOP's / Melodee's `step_note_resize`), several held edit together; SELECT
  the nudge (FELUCCA_MICRO), PRESETS the chance (synth steps, FELUCCA_CHANCE; drums "-"), SAVE the fill condition
  (FELUCCA_FILLS), shown in the footer (*Nudge +4  Chance 85%*, *Step 6  Fill only*). ALGORITHM does nothing while a
  step is held.
- **Locks** (FELUCCA_PLOCK, seq24.c's store, `lock_set`): a page button (ENV LFO FX SCL ARP) pressed with a step held
  makes that family's first page on this track the cards, as the held steps' locks; a turn writes a lock on every
  step held (from its lock value, else the track's), a locked cell shows a padlock and its value in amber; HOME + the
  knob drops the lock; the button again: the family's next page; the last step let go: the cards come back.
- **The pick**: SEQ held + keys. Drums: a white key picks the lane (`lane_sel`) and plays it. Synths: the keys play
  and the core's pen (seq.c `pen_note` / `pen_n`, which `key_down` keeps: the keys down together, a chord) is what the
  next tapped steps carry; letting SEQ go keeps it. The footer names it (*Keys steps SNARE*, *Pick C4 E4+*).
- **SEQ tapped on STEP** toggles the keys to playing (the window keeps following) and back.
- **No step held**: drums, the rows are the 16 lanes (SELECT the lane, the slow pick, synced with `lane_sel`), the
  cards the lane's LEVEL · TUNE · DECAY · REV (`dsnd_desc_lane`), YES opens the lane's SOUND rows; synths, the rows
  PATTERN (LEN DIV SWING GATE), ARP, ARP 2.
- **The panel**: the drum grid, 16 lanes x 16 steps of 7 px, a hit in its lane's colour shaded by its level, the
  ratchet as notches, the selected lane's row lit, the steps past LEN empty, the steps held framed white; the synth
  roll, the window's notes on a range of at least 13 semitones, ties as lines, chords stacked, the Cs faint. One
  240 x 123 canvas (the band limit); the playhead is a 4-px strip under the grid, its own canvas (a mark at the side
  when the playhead is outside the window), so a playing grid redraws only when a step changes.
- **The keys' lights**: on STEP the window's set steps (drums: the selected lane's), the playhead's key blinking,
  the steps held; toggled to playing, the keys down and the notes the track sounds (KEYLIT's logic, copied).
- **SOUND's families** [D]: a page button tapped shows only its family's rows (*Sound LFO*: LFO and LFO DEST with
  the LFO's graph); again, its next row, round; SELECT stays in the family; HOME > Sound shows every row as before.
- **The mixer without cards** [D]: the four strips take y 27..199 (a 57 x 173 canvas each); SELECT walks VOLUME,
  PAN, REV, DLY, CHO, DRIVE, FILTER, FX ON, then MASTER (BPM SWING LEVEL FILT at the strips' foot, each under its
  knob), then SOUND, FX, PROJECT, SYSTEM; the control SELECT is on is lit and framed on all four strips in each track's
  colour (the hot strip's frame white), the others dim, the drum strip's "-" a short dash; the header *Mix volume*,
  *Mix pan*; the selected track's strip framed in its colour with its head tinted.

Tests [M]: `tests/ui_optimist_test.c` **114 checks** in the three switch sets of phase 1b, **122** in a fourth set
added to `tests/run_tests.sh` with MICRO, FILLS, PLOCK and CHANCE: the keys to steps on the drum and a synth track
(set with the pick, cleared, no sound played, a ghost hit with OCT-), the pick (a drum lane heard; a chord), hold and
edit (LEVEL, RATCHET, NOTE, LENGTH through empty steps and up to the next note, two steps together), the window
(HOME + OCT+ to 17-32 with its header, a key past LEN, the stop at LEN, FOLLOW back, the window following a playing
pattern), the clears (HOME held + a step, a step held + HOME), the SEQ toggle, a question blocking the step keys, an
undo of a step edit, nudge, chance, fill, a lock written, marked and cleared, the footer lines' widths, the grid and
the roll within the band, the keys' lights; SOUND's families (LFO, ENV round, SELECT inside, HOME > Sound every row,
the drum track's EDIT); the mixer (no cards, the faders lit, PAN lit on the four strips, the walk's order, the
selected strip framed). Host renders `build/host/optimist/opt-step-*.ppm`, `opt-mixer-*.ppm`,
`opt-sound-lfo-family.ppm`. All of `tests/run_tests.sh` green, `builder_test.py` green, `tools/div_audit.py` green.

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state, `--fresh`; the script is the session
scratchpad's `emu/shots_p2.sh`): a user-default package with UI=1 **and PLOCK, MICRO, FILLS, CHANCE, SL24_XSTEP**
(547,704 B, so the locks and the footer's nudge and chance show; user-default builds none of them):
`build/ui-optimist-shots/p2-mixer-volume.png`, `p2-mixer-pan.png` (T2 selected), `p2-step-pick.png` (SEQ held, a
drum key: *Pick SNARE*), `p2-step-drums.png` (the drum grid playing), `p2-step-held-drum.png`, `p2-step-roll.png`,
`p2-step-held-synth.png` (NOTE LEVEL RATCHET LENGTH, nudge and chance), `p2-step-lock.png` (a step held + ENV, two
locks), `p2-step-window-17-32.png`, `p2-sound-lfo-family.png`. The emulator CPU budget test was not run.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`, UI=0 and `--set UI=1`; the slot is
581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| optimist 924c7c2 | 580,276 | 80,728 | 307,376 | 30,832 |
| this branch, UI=0 | **580,276** (unchanged) | 80,728 | 307,376 | 30,832 |
| phase 1b, UI=1 | 532,296 | 77,880 | 307,376 | 30,928 |
| **phase 2, UI=1** | **539,620** (+7,324; 41,944 free) | **77,944** (+64) | 307,376 | 30,928 |

The font gate (section 8): 41,944 B free against the Felucca renderer and faces' 43,608 B [M]: about 1.7 KB short
[E: the two numbers added, not built together], so phase 5's look no longer fits user-default with phases 2..4 unless
something gives (section 8's ways out).

**Decisions taken without the user** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| The core hook for "keys play and reach the UI" | none: the UI lets `ly_lock` go while SEQ is held or the keys are toggled to playing, and reads the note edges; a key pressed in the same 15 ms frame as SEQ may still land as a step | the flag of section 7 in seq.c `key_down` |
| The drum pick vs "the last sound hit" | one value, `lane_sel`: SEQ held + a key sets it, and so does a drum key played while STEP's keys are toggled to playing; a hit elsewhere (the mixer, SOUND) does not | `step_played` |
| The synth pick | the core's pen (`pen_note`, `pen_n`): the notes played last anywhere, a chord when keys are held together; one note on a MONO track, as SLOOP | a pick of the UI's own |
| A drum step's level when tapped | NORM; GHOST with OCT- held, HARD with OCT+ held | `step_set` |
| What a tap clears | drums the selected lane's hit (the step's other lanes stay), synths the note with its ties; an emptied step loses its nudge, locks, fill; HOME + a step: everything | `step_wipe` |
| HOME with a step held | HOME tapped (nothing turned while down) clears the held steps; HOME held + a knob drops a lock (or a step value back to NORM, x1, one step) | `op_no`, `step_turn` |
| The lock page | latched by pressing the page button with a step held (no need to keep it down); the button again: the family's next page; the last step let go ends it; EDIT and GLO do nothing there | `step_lock_page` |
| SELECT with a lock page | stays NUDGE (the table's "the family's next row" is the page button again) | `step_knobs` |
| SAVE with a step held | the fill condition, also with a lock page (the table's "toggle HOLD" is phase 3's) | `op_yes` |
| Undo levels | one per step key pressed; the knob edits while it is held join it; nudge, locks and fills are not in the undo history (undo.c keeps steps and LEN / DIV) | `st.sess` |
| Black keys on STEP | nothing | `step_key` |
| ALGORITHM with a step held | nothing (the step belongs to the track shown) | `step_knobs` |
| FOLLOW when stopped | the window stays where it is; a scroll turns FOLLOW off playing or stopped, only HOME + OCT- + OCT+ resumes it (no indicator in the header; the playhead strip marks the side when it is outside) | `step_tick` |
| STEP's drum rows | the 16 lanes with LANE_SHORT's names, the cards LEVEL TUNE DECAY REV; synth rows PATTERN, ARP, ARP 2 | `step_rows` |
| The roll's range | the window's notes, at least 13 semitones, at most 56 (2 px each) | `roll_range` |
| SOUND's family on the drum track with no such family (ENV) | from another screen: every row; on SOUND: the rows stay as they are | `op_jump_sound` |
| SOUND's family shown | without the SOUND row (preset, engine, INIT, SAVE AS): that row is only on HOME > Sound | `snd_first` |
| The mixer's walk | the strips' controls, MASTER, then SOUND FX PROJECT SYSTEM; the cursor rests on VOLUME | `MIX[]` |
| The mixer's master values | at the strips' foot, label, value and bar under each knob, lit on the MASTER row | `draw_strip` |
| The strips' control rows | no labels (8 px a row: the header names the lit control); the drum strip's "-" a dash | `draw_strip_ctl` |
| The mixer's "FX" rows | the on / dry row is FX ON, the screen's entry stays FX | `MIX[]` |
| STEP's footer | the hints *Seq play  Home back*; the keys' line *Keys steps* + the pick, *Pick* + it while SEQ is held, *Keys play* when toggled | `step_foot` |

**Not built** (later phases, or not asked): the automation store and the event marks under the steps (phase 3);
chance on drum steps (an event, phase 3); YES toggling HOLD of a lock; SELECT as "the family's next row" with a lock
page; EDIT held as the erase layer, and the other performance layers (FX ARP SCL GLO held still do nothing); HOME +
OCT on other screens; HOME held + a lane's key clearing a lane on STEP ([P] in 4.2's table); the fifth master
column; the DRUM MIXER.

**Found in the spec** (not changed; the smallest reading taken): section 4.2's table gives SELECT two jobs with a
step held (NUDGE, and with a page button "the family's next row"): NUDGE kept, the page button again moves the
family. 4.2 says HOME held + a lane's key clears the lane's steps [P] and also that HOME + a step clears the step:
on STEP the keys are steps, so the second was built. Section 7's per-layer "keys also play" flag turned out
unnecessary (above). The phase 2 gate names "the layers kept", which no item of the content list asks for: not
built. The ruling's "Sound LFO" header: the header names the screen and the cursor row, so the LFO DEST row reads
*Sound LFO dest*.

### 11.3 Phase 4: what was built (feat/ui-optimist, 2026-10-08)

The held performance layers (section 4.9), the layer lock, the TEMPO page (4.8), SAVE / HOME + a button (2.1), the
SONG screen (4.5), and the user's two mixer rulings given during the phase (section 4.1: no master values at the
strips' foot, PAN at the foot). UI=1 only; the three core additions are under `#if FELUCCA_UI == 1`, so the SLOOP-UI
build (UI=0) is unchanged (measured below).

**What changed** (`firmware/src/ui/optimist/`, new: `op_layers.c` the layers' keys, knobs and lock; `op_laydraw.c`
their map and the TEMPO and session-grid pictures; `op_tempo.c` the TEMPO page; `op_song.c` SONG; `op_combos.c`
SAVE / HOME + a button, all in SIZE_FILES):

- **The layers.** FX, EDIT, ARP, SCL, GLO, LFO (PATTERNS) and SAVE held are the core's layers as for SLOOP's UI:
  `ly_bit[]` gets their buttons (`lay_bits`, each frame), so seq.c `layer_now` routes the keys in the ISR (FX the
  punch-in, EDIT the erase, ARP the note repeat) or to `lk_q` (SCL, GLO, SAVE, LFO), which the UI drains with STEP's
  keys (`op_drain`). The keys' and knobs' logic is copied from ui/sloop's `ui_layers.c` and `ui_pat.c` (nothing there
  is called): SCL the key of the song and CHORD SCALE KEYS TRANSPOSE; GLO mute 1-4, solo 5-8, 9-12 FX on / off (FILLS:
  9 a fill held, 10 the next bar, FX on the black keys 1-4), 16 tap tempo, the four levels; EDIT SHIFT LENGTH TRANSPOSE
  (one undo level for the hold); LFO the patterns with the built black-key modifiers (open question 6); FX FILTER DUST
  DUCK (TRK_FILT: the track's FILTER); ARP RATE. SAVE: keys 1-16 launch scenes A-P at the next bar (stopped: loaded;
  an 8-section build keys 1-8), REC tapped = SONG REC on / off, REC held + key n = the loop into scene n (the modal
  over a used one), HOME held + key n = clear n (the modal), PLAY = the song from its start, two taps or more = the
  quick chain. While a layer is held the screen is its map: the header names it, the cards are its four knobs (their
  forms), the panel its 16 tiles in the instruments' colours, the footer its other gestures and its state (the key
  and scale, the chain, a modifier held). It shows after 140 ms or at once when used; a page button held longer than
  450 ms is no tap (no jump to its rows). PRESETS steps the layer's hot knob one unit, ALGORITHM is the track,
  SELECT does nothing.
- **The lock** [D]: HOME + the layer's button in either order: the layer held then HOME pressed, or HOME held then
  the layer's button. The map says *FX locked*, the button blinks; any button but PLAY, REC and OCT lets it go and
  does only that.
- **TEMPO** [D]: PLAY held 400 ms shows the page (a key while PLAY is down: at once), let go the screen before comes
  back. Rows TEMPO (BPM · NUDGE · SWING · SYNC) and REC (MODE · LENGTH · START · CLICK). OCT- / OCT+ held nudge the
  clock 3.9 % slower / faster, the BPM value unchanged, nothing while an external clock is followed (*Nudge: external
  clock*); a white key taps the tempo. The panel: the tempo large (amber while nudged), the four beats, the clock
  followed (*A:INT*, *A:USB*) and its RX light.
- **SAVE / HOME + a button** (section 2.1's table): SAVE + SEQ the working pattern into its slot (the modal over a
  used one); SAVE + ENV LFO EDIT FX SCL ARP the sound into the first free user preset slot (the drum track: its 16
  lanes into the first free user kit); SAVE + REC, SAVE + PLAY as the SAVE layer; HOME + REC the selected track
  cleared (the modal; on a SONG scene row: that scene); HOME + SEQ its locks, nudges, fills and motion (the modal,
  *Clear T1 locks?*), the notes stay; HOME + PLAY stop and every voice off (CC 120's `midi_silence_track` on each
  track); HOME + ENV INIT (the modal); HOME + OCT off STEP the octave back to 0; HOME + a layer's button: the lock.
  SAVE then HOME (undo), HOME then SAVE (redo) and EDIT + OCT kept; the undo / redo now acts when one of the two is
  let go, so SAVE + HOME + a scene key clears that scene and undoes nothing.
- **SONG** [D: the mixer's walk, *SONG* after *FX*]: SCENE A..P (each track's pattern in it, *KEEP*, *--*, a plain
  section *SEC*; PRESETS edits the hot cell's reference, YES launches at the next bar or loads when stopped, REC
  stores the loop into it, HOME + REC clears it; the playing scene marked green, a queued one amber, *Scene B\**
  when its tracks changed since it was stored), PATTERNS (each track's playing pattern, *3>5* queued; a turn cues the
  next stored one; the panel is the 4 x 16 session grid; while the cursor is there the keys launch the selected
  track's patterns, SAVE held + key n stores into slot n, HOME held + key n clears it, REC duplicates into the first
  free slot), MODE (LOOP / SONG, SONG REC, the MEM gauge, SAVE), then the chain PART 1..64 (SCENE, BARS, INSERT,
  DELETE; the part playing marked white).
- **The mixer** [D, two rulings]: the master values (BPM SWING LEVEL FILT) left the strips' foot and the SELECT walk;
  the fader and meter are 56 px (were 44), the control rows 10 px; PAN is drawn at each strip's very foot.

**The core additions** (each under `#if FELUCCA_UI == 1`; the SLOOP-UI build has none of them):

| Where | What |
|---|---|
| seq/seq.c, `clk_nudge` and `events_block` | `static volatile int8_t clk_nudge` (1/256 of the tempo); `adv = adv + ((int32_t)adv * clk_nudge >> 8)` before the transport, so the steps, the arp, the rolls, the click and the arranger move together; an external clock overrides `adv` after it as before. No divide in the ISR. G_BPM is never written |
| storage/sections/pat.c, `pat_scene_ref_set` | scene s's reference of track k set to a slot, KEEP or none: stopped only; its record (the arena's or the log's) written again into the log with that byte changed, its FX record (`fx_rec_log.c`, paired by the record's hash) keyed again |
| storage/sections/sections.c, `sec_scene_clear` | a scene / section cleared, stopped only: the arena's record, FX record and extras dropped, the log's written empty (`slg_put` of 0 bytes); `live_sec` let go; its patterns stay |

Tests [M]: `tests/ui_optimist_test.c`, a fifth switch set in `tests/run_tests.sh` on the real section log with
PATTERNS (the storage of `tests/patterns_ui_test.c`, a simulated NOR; SECTIONS 16, MICRO FILLS PLOCK REC_MODES); the
other four sets keep the doubles. New checks: each layer's map (FX's 16 tiles within the band), its keys to their
core actions through the ISR (FX punch-in 2, ARP a roll, SCL the key of every track, GLO mute / solo / FX or a fill
held / tap tempo, EDIT erase, LFO + black 6 + white n a stored pattern) and its knobs (FILTER DUST and PRESETS one
unit, RATE, SCALE on every track, TRANSPOSE, the levels, SHIFT LENGTH TRANSPOSE and their one undo level, a pattern
cued); a tap still opens the rows (also held over several frames, as on the device: a bug the emulator found,
the layer's knob pass took the tap), a used hold does not; the lock both orders (the ISR's `ly_lock`, keys still the
layer's, OCT keeps it, another button lets it go and does nothing else); the SAVE layer (load, empty, store asked,
SONG REC on / off, SAVE + HOME + key clears and does not undo, SAVE + PLAY the song); TEMPO (a tap starts / stops,
held: the page with no transport change, BPM, the REC row, tap tempo, the nudge measured on the clock: 2-10 % faster
over 200 blocks, the BPM value unchanged, back to 0 when let go, OCT- slower, still playing after a hold); the SAVE /
HOME table (a user preset, a user kit, the pattern into its slot and asked again, the extras' clear and its question,
INIT asked, the octave, the panic); SONG (the mixer's row, the rows' count, REC stores into a scene and asks over a
used one, PRESETS edits a reference, amber until written, written once it rests, YES loads, the PATTERNS row's keys,
SAVE + key, HOME + key asked and cleared, REC duplicates, HOME + REC clears a scene, MODE's toggle, BARS, INSERT,
DELETE, the chain saved when SONG is left); every layer's title and footer lines fit; a scene's letter and a note
keep their capital in sentence case (*Scene B*, *Key C#*). Host renders `build/host/optimist/opt-layer-*.ppm`,
`opt-tempo*.ppm`, `opt-song*.ppm`. 187 to 214 checks a switch set. All of `tests/run_tests.sh` green,
`builder_test.py` green, `tools/div_audit.py` green (tap tempo's divide listed).

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state `--fresh`; the session scratchpad's
`emu/shots_p4.sh`; a user-default package with UI=1 and PLOCK MICRO FILLS CHANCE SL24_XSTEP, as phase 2's):
`build/ui-optimist-shots/p4-mixer.png` (playing, PAN at the foot, no master values), `p4-mixer-pan.png`,
`p4-layer-fx.png`, `p4-layer-arp.png`, `p4-layer-scl.png`, `p4-layer-glo.png`, `p4-layer-edit.png`,
`p4-layer-lfo.png`, `p4-layer-save.png` (scenes A and B stored), `p4-layer-locked.png` (GLO locked),
`p4-tempo.png`, `p4-tempo-nudge.png`, `p4-song.png` (the scenes), `p4-song-patterns.png` (the session grid),
`p4-song-chain.png`. The emulator CPU budget test was not run.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`, UI=0 and `--set UI=1`; the slot is
581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| optimist 924c7c2 | 580,276 | 80,728 | 307,376 | 30,832 |
| this branch, UI=0 | **580,276** (unchanged) | 80,728 | 307,376 | 30,832 |
| phase 2, UI=1 | 539,620 | 77,944 | 307,376 | 30,928 |
| **phase 4, UI=1** | **554,244** (+14,624; 27,320 free) | **78,680** (+736) | 307,376 | 30,784 (-144) |

The rows above phase 4 include the mixer's two rulings (their own commit). The screenshot package (UI=1 with PLOCK
MICRO FILLS CHANCE SL24_XSTEP) is 562,712 B. Nothing was left out to fit.

The font gate (section 8): 27,320 B free against the Felucca renderer and faces' 43,608 B [M]: about 16 KB short [E:
the two numbers added], so phase 5's look needs one of section 8's ways out on user-default.

**Decisions taken without the user** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| PLAY's tap and the TEMPO page | start / stop when PLAY is let go (a tap: under 400 ms, nothing else done), so a hold opens TEMPO without touching the transport; the start comes the press's length later than before | act on the press and open TEMPO with another gesture (`op_press` B_PLAY, `op_released`) |
| The hold threshold, the nudge | 400 ms; a key while PLAY is down opens the page at once; the nudge 10/256 = 3.9 %, both ways | `TEMPO_HOLD_MS`, `TEMPO_NUDGE` |
| FINE (0.1 BPM) | not built: the clock counts whole BPM (`adv = n x BPM`, BEAT_U); NUDGE's read-out takes its card | a tenth-of-a-BPM clock unit in the core (section 10) |
| TEMPO's REC row | LENGTH (the selected track's 1 / 2 / 4 bars) and CLICK (`G_CLOCK`) always; MODE and START with REC MODES, "-" without | `tp_cell` |
| HOME + PLAY | the panic of 2.1's table; PLAY's page is not lockable (the lock rule names the layers; PLAY's hold is a page) | `home_combo` |
| The SAVE layer and the lock | not lockable: SAVE then HOME is undo [D] | a gesture of its own |
| Undo / redo | when SAVE or HOME is let go (was: at HOME's / SAVE's press), so a scene key between them clears instead | `op_released` |
| HOME + ENV | INIT, asked (2.1's [P]); nothing on the drum track; ENV is no layer here (no FM6 operator editor in this UI) | `home_combo` |
| HOME + SEQ | the selected track's step extras (nudges, locks, fills) and its motion, asked; the notes stay; not in the undo history | `op_act_more` |
| SAVE + a sound page button | the first free user preset slot (the drum track: the first free user kit), as SAVE AS; *USER PRESETS FULL* / *USER KITS FULL*; SAVE + GLO and SAVE + OCT do nothing | `save_combo` |
| SAVE + SEQ | into the working copy's source slot, else the first free; asked over a used one | `save_pattern` |
| A scene stored | playing: the arena (`section_store`, written when quiet), stopped: at once (`project_save`), as the editor; asked over a used scene | `scene_store_now` |
| A scene cleared | stopped only (*STOP FIRST*); its patterns stay | `sec_scene_clear` |
| A scene loaded (YES stopped, a SAVE key stopped) | at once, no question, as the SAVE layer's key [D: launch / load] | `scene_launch` |
| A scene's reference | edited stopped only; PRESETS steps through *--*, *KEEP*, then the stored slots; the cell amber until written, written 0.7 s after the last detent, or when the row or the screen is left; one flash write an edit, not a detent; a plain section (*SEC*) is not edited | `song_tick`, `song_ref_step` |
| SONG's rows | MODE before the chain (the chain is up to 64 rows); MODE's cells MODE · REC · USED · SAVE; the chain saved when SONG is left and stopped (and by MODE's SAVE) | `song_rows`, `song_tick` |
| The chain's parts | edited stopped only; INSERT a copy after the part (the cursor on it), DELETE keeps one; no question | `song_part_edit` |
| The part playing | marked white beside its name (4.5: "the cursor part plays white") | `song_row_col` |
| The LFO layer's confirms | its "AGAIN" over a used slot became the modal (store, clear, copy) | `pat_store_ask`, `pat_clear_ask` |
| The map's timing | shown after 140 ms or when used; a page button held over 450 ms is no tap | `LAY_SHOW_MS`, `LAY_TAP_MS` |
| Layers on STEP | without a step held a layer works (the UI lets STEP's `ly_lock` go while its button is down; a key in the same 15 ms frame may still land as a step); with a step held the page buttons stay the locks' pages and no layer takes the keys | `lay_bits`, `ui_input` |
| The knobs in a layer | PRESETS the layer's hot knob one unit, ALGORITHM the track, SELECT nothing | `lay_knobs` |
| EDIT's SHIFT and LENGTH on the drum track | the drum steps too (SLOOP's rotate the synth steps only) | `pattern_rotate`, `pattern_length` |
| Labels | *REC* for SONG REC on the cards (8 letters do not fit a card) | `song_cell`, `lay_cell` |
| Sentence case | a letter alone and a note keep their capital (*Scene B*, *Key C#*) | `case_keep` |
| The mixer | the fader and meter 56 px, the control rows 10 px, PAN at the foot; the master LEVEL read-out dropped (the analog knob) | `op_draw.c` MX_* |

**Found in the spec** (not changed; the reading taken): 2.1 makes HOME + PLAY the panic while 4.9 lists PLAY's hold
as a layer and the lock rule says HOME + a layer's button locks: the panic kept, PLAY's page not lockable. 4.9's
SAVE layer has HOME held + key n clear n, while SAVE then HOME is undo: the undo moved to the release. 4.8's FINE
0.1 needs a clock change (not built). 4.5 puts MODE after the 64 parts: before them. 4.5's REC on a scene row
stores, so REC does not record on SONG's scene rows (it does on every other row and screen); HOME + REC there clears
the scene, not the track. 2.1 lists ENV's HOME as INIT "elsewhere" and the lock "on an FM6 track": this UI has no
FM6 layer, so INIT everywhere but the drum track. Section 11.2's "the master values" on the mixer are gone (section
4.1's rulings).

**Proposals for the user** [P]:

1. **A shortcut to SONG** (the mixer's row is the only way in; SAVE tapped is YES): (a) GLO tapped twice (the FX
   screen, then SONG), (b) a SONG row on the TEMPO page (PLAY held + SELECT), (c) the SAVE layer's map with a third
   gesture (SAVE held + SELECT turned: SONG when let go).
2. **FINE**: a clock counted in tenths of a BPM (BEAT_U x 10, `adv = n x BPM10`): every division stays whole; a core
   change of the sample-accurate clock, to measure.
3. **Locking the scenes**: SAVE held + HOME held, both let go together = lock (today an undo); or keep it unlockable.
4. **Scene references while playing**: through the arena, as a scene stored playing (written when quiet).

**Not built** (later phases, or not asked): the NAME screen (SAVE + a button and SAVE AS save into the first free
slot); ENV held as the FM6 operator editor; the uniform LFO vocabulary (open question 6); pattern launches recorded
by SONG REC (PATTERNS-DESIGN Q11); the DRUM MIXER; SCOPE; the Felucca look; the emulator CPU budget test.

### 11.4 Loose ends (feat/ui-loose, 2026-10-08)

The user's rulings after phase 4 (section 0: the SONG shortcut, graphs on every row of a family, a preset shown
with its engine, no footer) and two items phase 4 left out (the NAME screen, ENV held as the FM6 operator editor),
plus the undo of a step's extras. UI=1 only, except the undo (below).

**What changed** (`firmware/src/ui/optimist/`, new: `op_name.c` NAME, `op_fm6.c` FM6's editor, `op_fm6draw.c` its
pictures, `op_preset.c` a preset with its engine, all in SIZE_FILES):

- **SONG shortcut** [D]: SAVE held + SELECT turned opens SONG, its cursor on the scene playing (none: scene A); on
  SONG already, SAVE + SELECT moves its cursor. SAVE let go after it is no YES (`song_shortcut`, `lay_knobs`).
- **NAME** (section 0 "Names"; ported from Felucca 1.0.1's `ui_name.c`, Leo Kuroshita, GPL-3.0-only, its copyright
  line kept): a user preset or a project is named before it is written. Where: SAVE + a sound page button and SOUND's
  SAVE AS (the first free user preset), PROJECT's USER row SAVE, PROJECT's SAVE (after the modal over a used slot,
  NAME prefilled with the slot's name). Felucca's key map and multi-tap typing in this UI's grammar: the white keys
  type phone style (AB CD EF GH IJK LM NO PQ RS TU VW XYZ 123 456 789 0-., the same key within 0.8 s the next
  letter), the black keys by name in both octaves (F# left, G# space, A# right, C# delete, D# letters / digits, the
  arrows and DELETE repeat when held); KNOB 1 the cursor, KNOB 2 and PRESETS the character; SAVE done, HOME tapped
  deletes before the cursor (at the start: cancel, nothing written), HOME + a knob clears the name. The header says
  *Name U03* / *Name project 3*, the cards' band is the field (12 cells, *4/12*), the panel the keyboard; no footer.
  The keys type and never sound (they reach the UI as STEP's do, `ly_lock` LY_STEP). A user preset's name is its
  record's (`up_rec_t.name`, as before); a project's is a new record of the section log (below). Kits keep numbers.
- **FM6's operator editor** (section 2.1's ENV row, 4.3, 4.9): on an FM6 track, **ENV held is the layer** (seq.c
  LY_OPS, `ly_ops_on` set each frame from the selected track): a black key picks OP1..OP6, PIT, GLO or MONO / POLY
  by the FM-1's printed labels, the white keys play, the cards are the page's four values, the panel the algorithm
  (the operator picked white, carriers in the engine's colour, a switched-off one dim) over the black keys' map;
  OCT- / OCT+ the page back / on; HOME + ENV locks it. Let go after use: SOUND's FM6 rows on that page. **ENV tapped
  is SOUND's FM6 family**: OPERATOR (OP ALG FDBK VCE), the picked operator's six pages (FREQ, LEVEL, RATES, LEVELS,
  KEY SCALE, CURVES), PIT's two, ALGO, LFO, LFO 2, PORTA, STORE (14 rows), the algorithm drawn over the rows on each;
  ENV again the next row, round. Edits go into the part's voice (`fm6_ed`), as SLOOP's editor. On any other engine
  ENV is what it was (the envelopes' rows; no layer). The page tables and the algorithm's maths are copied from
  `ui/sloop/ui_fm6.c`; nothing there is called.
- **Graphs on every row of a family** [D]: SOUND's row without a graph of its own shows the nearest row of its family
  that has one (above, then below): LFO DEST the LFO's wave, ENV DEST and ENV2 DEST their envelopes
  (`snd_graph_page`).
- **A preset with its engine** [D]: the SOUND row's picture is the preset before, the one playing (white) and the one
  after, each with its engine's name and colour chip (a user preset: its record's engine); PRESETS on the mixer's
  SOUND row shows a toast *Saw bass (FM6)* framed in the engine's colour; the strips' names are in their engine's
  colour on the other rows (were amber). A small accessor `up_engine_slot` in storage/upreset.c, `FELUCCA_UI == 1`.
- **No footer** [D]: NAME and the FM6 pictures draw none. The footer's removal from the other screens (and the hints
  moved to the header) is feat/ui-drummix's (section 11.5), so the two branches do not both rewrite `draw_foot`; on
  this branch alone the other screens keep phase 4's footer, as the screenshots show.
- **Undo of a step's extras** (core, seq/undo.c, under `SL24_STEPX`): a mark also copies the track's extras (seq/stepx.h,
  176 B), undo and redo swap them with the steps, the history keeps only the parts that changed (nudges 64 B, locks
  96 B, fills 16 B; header bits 4..6), so a nudge, a lock or a fill is undone with its step edit. In this UI a lock
  written or dropped now marks a level (`lock_turn`; before, only the nudge and fill paths did), and HOME + SEQ's
  clear of the extras is one level (11.3 said it was not). **This also changes SLOOP's UI on builds with the step
  extras** (its marks are the same calls); user-default has none, so its UI=0 build is unchanged.

**The core additions**:

| Where | What | Guard |
|---|---|---|
| seq/undo.c | the extras in a mark, the swap, the history's three parts (`UNDO_SXO` / `UNDO_SXL`), `_Static_assert` that the smallest ring holds a record | `SL24_STEPX` (both UIs) |
| storage/sections/sections.c | `sec_name` / `sec_name_set`: the project names, one record of the section log, id 23 (`SEC_ID_NAMES`), 16 names of 12 bytes; read again when its sequence number changes; written stopped only | `FELUCCA_UI == 1` (in the log's `#if`) |
| storage/upreset.c | `up_engine_slot(k)`: the engine of user preset k | `FELUCCA_UI == 1` |

Id 23 is one of the song ids (16..23) that every build with the log reads and keeps through a compaction, and the
snapshots carry it, so a firmware without names (SLOOP's UI, an older Optimist) keeps them; a record of another
length is read as no names. No storage format changes; the four RAM project slots (no log) save without NAME.

Tests [M]: `tests/ui_optimist_test.c`, all five switch sets: SAVE + SELECT (SONG, its cursor, no YES); the undo of a
step's nudge, fill and lock and their redo byte for byte, HOME + SEQ's clear undone; NAME (opened by SAVE + ENV /
ARP, SAVE AS, PROJECT USER, PROJECT with the log; the header; multi-tap, the digits, space, delete, the knobs, HOME
cancel, refused while playing, the name written and read back, a used project slot asked then prefilled); FM6 (ENV
tapped: the 14 rows, the operator, a page's value in the voice, the algorithm drawn; ENV held: the layer, a black
key's operator, PIT, OCT, a knob, let go on that page; another engine: as before); the graphs on every LFO and ENV
row; the SOUND row's engine and chip, the mixer's toast and its colour. 224 to 259 checks a switch set. Fixed while
running them all: the lock write marked no undo level (the PLOCK-only set), HOME + SEQ's clear marked none, the
PROJECT test now expects NAME on the log's builds; two lists (`tools/div_audit.txt`: six divides, each guarded;
`tools/size_fns.py`: the four files). All of `tests/run_tests.sh` green (with `builder_test.py` and
`tools/div_audit.py`).

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state `--fresh`; the session scratchpad's
`emu/shots_p5a.sh`; a user-default package with UI=1 and PLOCK MICRO FILLS CHANCE SL24_XSTEP, as phase 4's):
`build/ui-optimist-shots/p5a-preset-engine.png` (SOUND row: *Solid bass* FM6 between *LP24 bass* ANALOG and *Saw
bass* FM6), `p5a-fm6-layer.png` (ENV held on FM6: OP1's FREQ page, the algorithm, the black keys), `p5a-fm6-layer-op.png`
(a black key: OP6), `p5a-fm6-operator.png` (ENV tapped: the OPERATOR row), `p5a-fm6-page.png` (ENV again: *OP6 freq*),
`p5a-mixer-toast.png` (*Saw bass (FM6)*), `p5a-lfo-dest.png` and `p5a-env-dest.png` (a DEST row with its family's
graph), `p5a-name-open.png` (SAVE + ARP: NAME prefilled *Digital 01*) and `p5a-name.png` (*Bass* typed). Seen on
them: in SOUND's band the algorithm's deep stacks (algorithm 1: 6 over 5 over 4) overlap their digits (the boxes 8 px,
the digits 16 px); the layer's full panel draws them clean. Left as is (the user likes the look); a fix would draw the
digits only where the box holds them. The emulator CPU budget test was not run.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`; the slot is 581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| phase 4, UI=0 | 580,276 | 80,728 | 307,376 | 30,832 |
| **this branch, UI=0** | **580,276** (unchanged) | 80,728 | 307,376 | 30,832 |
| phase 4, UI=1 | 554,244 | 78,680 | 307,376 | 30,784 |
| **this branch, UI=1** | **565,084** (+10,840; 16,480 free) | **79,272** (+592) | 307,376 | 30,864 (+80) |

With the step extras (`--set PLOCK=1 --set MICRO=1 --set FILLS=1 --set CHANCE=1 --set SL24_XSTEP=1`), where the undo
change reaches SLOOP's UI:

| user-default + the extras | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| UI=0, phase 4's undo.c | 589,752 (8,188 over the slot) | 81,976 | 311,632 | 30,796 |
| **UI=0, this branch** | **590,008** (+256; 8,444 over) | **82,152** (+176) | 311,632 | 30,796 |
| UI=1, phase 4's package | 562,712 | | | |
| **UI=1, this branch** | **572,796** (+10,084; 8,768 free) | 80,712 | 311,632 | 30,896 |

The undo's cost in SLOOP's UI: +256 B of code, +176 B of RAM (the copy of a track's extras), so the history ring is
176 B smaller (253,560 to 253,384 B). SLOOP's UI with all five extras did not fit user-default before this branch
either (8,188 B over); nothing was left out of any configuration measured here: UI=1 fits with and without the extras.

**Decisions taken without the user** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| SAVE + SELECT on SONG | moves SONG's cursor; entering puts it on the scene playing (none: A) | `song_shortcut` |
| The extras in the undo | one copy (176 B) a mark, the record keeps only the changed parts; both UIs, under `SL24_STEPX` | the `#if SL24_STEPX` blocks of seq/undo.c |
| A lock written | marks an undo level with the step's session (`lock_turn`) | the `undo_mark` in `lock_turn` |
| HOME + SEQ | its clear is one undo level (11.3: not in the history) | the `undo_mark` in `op_act_more` |
| Where NAME opens | SAVE + a sound button, SAVE AS, PROJECT USER SAVE, PROJECT SAVE (log builds); not a kit (the drum track's SAVE + a button saves a user kit at once), not a snapshot | `name_user_free`, `name_user`, `name_project` |
| NAME's grammar | SAVE done; HOME tapped delete, at the start cancel; HOME + a knob clears; Felucca's keys and multi-tap | `name_frame`, `nm_knob`, `nm_black` |
| NAME while playing | SAVE refused (*STOP BEFORE SAVE*), the screen stays with the name typed (a flash erase stops the audio; `up_ui` refused it the same way) | `name_ok` |
| An empty name | a user preset gets the automatic one (*DIGITAL 01*), a project none | `name_ok`, `up_store` |
| A used project slot | the modal first, then NAME prefilled with its name | `prj_yes` |
| Project names | the section log's record id 23, 16 x 12 B, stopped only; builds without the log save unnamed | `SEC_ID_NAMES`, `name_project` |
| A name's case | stored as typed (upper case), drawn in sentence case | `name_draw` |
| ENV on an FM6 track | held = the layer, tapped = SOUND's FM6 rows; HOME + ENV the lock there (INIT elsewhere, as 11.3) | `LAYER_BTN`, `lay_btn_layer`, `op_jump`, `home_combo` |
| The layer let go | after use: SOUND's FM6 rows on its page (unused: nothing) | `fm6_lay_end` |
| FM6's SOUND rows | OPERATOR, the picked operator's 6 pages, PIT 2, ALGO, LFO, LFO 2, PORTA, STORE; the algorithm over each | `F6_*`, `FMK_PAGES` |
| A row with no graph | the nearest graph of its family, above first | `snd_graph_page` |
| The SOUND row's picture | three presets (before, playing, after) with engine names and chips; the drum track its kit as before | `pre_draw` |
| The mixer's PRESETS | a toast, the name and *(ENGINE)*, framed in the engine's colour | `pre_toast`, `ui.toast_col` |
| The strips' names | in the engine's colour off the SOUND row (were amber) | `draw_strip` |
| The footer | not removed here (feat/ui-drummix does it); the new screens draw none | `draw_foot` |

**Found in the spec** (not changed; the reading taken): section 0 says "SAVE cannot be a shift", while the user's
shortcut is SAVE held + SELECT: SELECT is no page button, so 2.1's table (SAVE + a page button saves what it owns)
still holds. Section 0 "Names: projects get names" against the four RAM project slots, which have no room for one
without a format change: those builds save unnamed (the user's brief: stop if names need a format change; the log's
id 23 needs none). 2.1's ENV row ("the lock on an FM6 track", INIT "elsewhere") now reads as built. 11.3's "ENV is
no layer here" and "HOME + SEQ not in the undo history" are superseded above. 4.3 shows no FM6 family on SOUND:
ENV tapped is it, on FM6 tracks.

**Not built**: the footer's removal on the other screens (feat/ui-drummix); names of user kits (kits keep numbers,
section 0); NAME for snapshots; the emulator CPU budget test.
