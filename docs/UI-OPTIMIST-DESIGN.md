# Optimist UI: design (the second UI on the pluggable seam)

Status: **design; phase 1 (the skeleton), 1b (the review's rulings), 2 (STEP), 4 (the layers, TEMPO, SONG), the
loose ends (NAME, FM6, 11.4) and 5b (the drum mixer, SCOPE, no footer, 11.5) built** (2026-10-08, section 11).
Written on 2026-10-08; the decisions are in section 0. The study it rests on is docs/UI-FEASIBILITY.md (the seam); the sequencer facts come from
docs/PATTERNS-DESIGN.md (patterns, scenes, the song) and the SLOOP 2.4 ports (seq/stepx.h, seq/seq24.c).

Labels: **[M]** measured (a file and line, a tool run, a build), **[E]** estimate (how it was made is said), **[P]**
proposal (to accept or change), **[D]** decided (section 0), **[O]** open.

## 0. Decisions (2026-10-08)

| Question | Decision |
|---|---|
| Scope | **a new UI** on the pluggable seam, chosen at build time beside SLOOP's (`ui/sloop`), not a rework of SLOOP's layers |
| Other UIs | **none**: the multi-UI plan is replaced by implementing our own UI: exactly two UIs, SLOOP's and ours, a builder choice; no X0X or Felucca UI ports, and only the seam those two need |
| Look | **Felucca 1.0's** (section 3): flat cards, the panel, circled numerals, its font and icons; the grammar is ours |
| YES / NO | **SAVE = YES, HOME = NO** |
| The three left encoders | **SELECT = the cursor, ALGORITHM = the track, PRESETS = the value** of the hot cell; it changes the sound only where the value *is* the sound (the SOUND row), so the preset does not change all the time |
| The step sequencer | **the 16 keys are the 16 steps**, on drums and synths alike; a pattern longer than 16 steps: **HOME + OCT- / OCT+ scroll the window of 16**; OCT alone keeps its job |
| Picking the drum sub-track (the lane) | **the current mode's button, held, + a key**: on the mixer and on every screen where the keys play, **HOME held + a key** (the key plays the sound); on STEP, where the keys are steps, **SEQ held + a key**. One lane for the whole device |
| HOME in combination | **HOME held + anything = clear it**: a step + HOME clears the step, a knob + HOME clears the cell's event, REC + HOME clears the track, a scene or pattern key + HOME clears it. HOME tapped alone is NO: it answers a dialog and goes back; while a dialog asks, the keys do nothing |
| Locks and motion | **one store, two ways to edit it**: a step held + a knob writes a lock, REC + a knob while playing records motion, both into one list per pattern (section 6) |
| SAVE and HOME with a button | YES / NO with a button was first read as a shift, but **SAVE cannot be a shift**: it is used to save something (preset, pattern, project). So: **SAVE held + a page button saves what that button owns**, nothing else; **HOME held + a page button clears or switches off** what it owns; section 2.1 proposes the table [P] |
| SONG REC | there is a REC button: recording the song is **SAVE + REC** (section 2.1), not SAVE + PLAY |
| Undo / redo | **SAVE then HOME = undo, HOME then SAVE = redo**; the order of the two tells them apart [P] |
| Lock a layer | **HOME + the layer's button, in either order** (the layer held then HOME tapped, or HOME held then the layer's button): the layer stays open hands-free, any button lets it go. So HOME with a layer button never means "off" |
| Scenes on the keys | **16 scenes on the 16 keys, no bank** |
| The pattern layer's keys | [O] hard to say before trying it on hardware: the built black-key modifiers against the uniform launch / store / clear vocabulary, decided on the FM-1 (it arrives 2026-10-08) |
| EDIT + OCT as a second undo / redo | **keep both**: SAVE then HOME / HOME then SAVE, and SLOOP's EDIT held + OCT- / OCT+ (edit left / right); a point to clarify on it is open (question 12) |
| Names | **Felucca's NAME screen**: projects and user presets get names typed with the keys; kits keep numbers |
| The pick on SOUND | **HOME + a key only**; the sound buttons' hold is not a pick |
| Per-step chance | **just p-lock / motion data**: chance is an event of the automation store (section 6), on drums and synths alike; no step bit is needed |
| FOLLOW on STEP | **follows the playhead by default**; HOME + OCT- / OCT+ scroll left / right and stop the follow; **HOME + OCT- + OCT+ together resumes it** |
| Every per-step extra as an event | **yes: one list of 128 events a pattern** holds nudge, fill, chance, locks and motion (to be revisited); SLOOP 2.4's stepx record becomes an import / export form |
| EDIT held's knobs | **kept**: SHIFT · LENGTH · TRANSPOSE on EDIT held, erase on the keys |
| Fills and automation | **hold events apply through a fill, step-only events are skipped with their step** |
| Typed notes on STEP | **no**: the keys and the pick are enough |
| SEQ tapped on STEP | **toggles the keys between steps and playing** |
| Nudge | **a few percent slower / faster while OCT- / OCT+ is held, back on release**; the BPM value never changes |
| The mixer's drum column | **the whole drum track, the sum of the 16 sounds**; a way to switch to a drum mixer showing its 16 channels (in blocks of 4) was wanted: section 4.1's DRUM MIXER (superseded after the merge: the horizontal mixer, its lanes as rows) |
| The DRUM MIXER | entered by **the pick: HOME + a drum key on the mixer** opens it on that sound's block; its rows are **like other tracks**: the mixer's rows, "-" where a sound has no such value; a compact mode with 8 or 16 on a screen is to be evaluated (switching would be an option in the main settings) [O, question 15] (superseded: the horizontal mixer has no drum mixer) |
| Macros (COLOR MOTN SPACE ENRGY) | parked (open question 16) |
| Visualiser | **not SLOOP 2.4's styles** (they add little): instead **an oscilloscope view for the master and per track, and levels on the mixer with the compressor's effect shown, a bar pushing down** (sections 4.1 and 4.10) |
| Play the song | **SAVE + PLAY plays the song from the start; PLAY alone stops** |
| Store the loop into scene n | SAVE held + REC held + key n, three fingers: not decidable without trying, to confirm on the hardware (open question 18) |
| Tempo (SELECT is no longer the tempo) | **a tempo page held open by PLAY**, with nudge on OCT- / OCT+, fine tuning, MIDI sync (section 4.8) |
| Next | this document, then review |
| Track colours (review of phase 1, 2026-10-08) | **Tracks are coloured after their instrument**: a track is always drawn in its engine's colour (the drum track: its kit's kind), tools/colors.json, wherever this UI shows it: the mixer's strip, the header's badge, the cursor bar on SOUND, a track named in a question. There is no colour per track number or per knob |
| Questions (review of phase 1) | **questions must be more visible: in the middle, like a modal popup**: a confirm is a box over the panel (the cards stay above), the verb big, its target named big (a track in its colour), "HOME no" on the left and "SAVE yes" on the right at its bottom, as the buttons sit (decided after the 1b screenshots; the footer does not repeat them); a red frame when it destroys or replaces the work (clear, erase, an overwrite, a load), amber otherwise. The result of an action just confirmed is a small toast in the middle; passive status (MISSING, RECORDING, REC OFF) stays in the header |
| Values (review of phase 1) | **almost no place should show a number value with no graphic representation**: every value is drawn with a form beside its number (section 3); names stay text |
| A page button on SOUND (phase 2) | **only that family's rows** (the LFO screen should not show ENV2 and SLICER rows): LFO shows the LFO rows and their graph, ENV the envelopes...; the button again its next row, round; HOME > Sound keeps every row (section 4.3) |
| The mixer's cards (phase 2) | **none**: the mixer has no top four cards; instead the fader, the pan, etc. are highlighted, and SELECT scrolls from volume to pan, to send, etc.; the strips take the height, the control SELECT is on lit on all four, the master values as the walk's last row, the selected track's strip framed in its colour (section 4.1) |
| A shortcut to SONG (review of phase 4) | **SAVE held + SELECT turned** (11.3's proposal 1c): the SONG screen; built in section 11.4 |
| Graphs on a family's rows (emulator review of phase 4) | **LFO and ENV always display their shape**: every row of the ENV, ENV2 and LFO families shows its family's graph, the DEST rows too (section 11.4) |
| Which engine a preset is (emulator review of phase 4) | **a preset must show which engine it belongs to**: a preset is always shown with its engine, in the engine's colour (section 11.4) |
| The footer (emulator review of phase 4) | **no footer anywhere**: its two lines were noise on a small screen; the useful hints move into the header, the cards or the modal (the new screens of 11.4 draw none; the removal from the other screens is feat/ui-drummix's, section 11.5) |
| The look (review of phase 4) | **the current look is kept**: phase 5 (Felucca's font and faces) is dropped for now |
| The header's badge (phase 5b) | **the selected track's engine name, in its colour** (the drum track: *DRUMS*), where *T1* was: the track tag brings no value, so the algorithm goes there; the track is already the colour and the selected strip (section 3) |
| The footer (phase 5b) | **no footer anywhere**: every screen draws to the screen's foot (the mixers' strips, the lists, STEP, the layers, SCOPE); what the footer said moved to the header (STEP's window and pick; a layer's *Home locks it*, once) or under STEP's grid (a held step's nudge, chance, fill); section 11.5 lists what went |
| Pattern length (after the merge of 11.4 and 11.5) | **LEN moves in powers of two by default, 1 2 4 8 16 32 64**: a knob detent goes to the next / previous value of that list (from a value off the list, to the next list value in the turn's direction); **with SHIFT held, by one**. **SHIFT is the LFO button held** (decided): a general flag the cells can read, used by LEN only for now; PRESETS stays the one-unit fine encoder on any hot cell, LEN included (section 11.6) |
| The cards, 1x4 or 2x2 (after the merge) | **a SYSTEM > SCREEN option, CARDS: 1x4** (the row of four small cards) **or 2x2** (SLOOP 2.4's big values: the four values in large type as a 2 x 2 block laid out like the knobs, KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom right, the hot one white, each with its form), because the screen is small, so large values help; on every screen with cards (SOUND, STEP, SONG, TEMPO, FX, PROJECT, SYSTEM, the layers, SCOPE); in 2x2 the panel under the block shrinks (section 3). Default 1x4 (section 11.6) |
| A click against a hold (found on the FM-1, after the merge) | **a click on a button showed the long-press screen about one time out of two**: SLOOP's 140 ms is shorter than a click on the FM-1's buttons. **A hold is 350 ms by default, a SYSTEM setting HOLD 250 / 350 / 500 ms**, one threshold for every tap-or-hold decision of this UI: a layer's map, PLAY held for TEMPO; a page button let go within HOLD + 150 ms still counts as a tap (section 11.6) |
| The mixer, horizontal (after the merge) | **the horizontal mixer is better**: **the rows are the tracks**, MASTER (above T1, out of view until the cursor goes up: it starts on track one, but the cursor can go up to master), T1 T2 T3, DR, then **the drum track's 16 lanes as rows** (indented, named, in their source's colour); **the four knobs are four values of the selected row**, on the cards (1x4 or 2x2); **ALGORITHM walks the rows** (it is the lane encoder there), **SELECT and GLO tapped again page the knob sets** (VOLUME INSERT SEND PAN, then the rest; a lane LEVEL DRIVE REV CUT, then DLY CHO); each row **a VU meter, the compressor's reduction pushing in from the right, and its sequence under it, like SLOOP's TRACKS**; **the DRUM MIXER screen and its DR MIX setting are gone** (its lanes are the rows); the GR column went into the MASTER row; the four levels on four knobs stay on GLO held (section 4.1) |
| A drum lane picked (found on the FM-1) | **its sound previews once when the transport is stopped, and stays silent while playing**, however the lane changes (the pick SEQ held + a key on STEP, HOME held + a key elsewhere, ALGORITHM over the mixer's lane rows, SELECT over STEP's lanes); **the pick's key itself is silent while playing** (it must not sound over the running pattern) and plays when stopped (that is the preview). Synth tracks: unchanged (section 11.6) |
| STEP's first page (on the FM-1) | **the first SEQ page should have the length of the pattern**: STEP always opens on **PATTERN: LEN · DIV · SWING · GATE**, on synth and drum tracks; then a synth track's ARP rows, the drum track's 16 lanes (each its sound: LEVEL · TUNE · DECAY · REV); SELECT walks PATTERN, lane 1 .. lane 16 and selects the lane it lands on (the lane encoder on STEP, ALGORITHM keeps switching tracks: the track should not switch while editing a sequence) (section 4.2) |
| Paging (after the merge) | **a page button tapped again goes to its next page, round**; **SELECT moves through the same pages, back and forth, stopping at the ends**, on every screen (section 2: the rules and their exceptions) |
| Clear a track by holding REC (after the merge) | **use SLOOP's long press on REC to delete a track**: REC held 0.7 s undoes its press and a ring fills in the track's colour; held 1.3 s more, the selected track is cleared (undoable); let go before, nothing. No question: the ring is the confirmation. Every screen, playing too; REC tapped keeps its jobs; HOME + REC (the modal) stays as a second way (section 11.6) |
| The mixer's rows, seen on the emulator | **four tracks per screen**: four rows a screen, a quarter of the panel each, T1 T2 T3 DR when it opens, ALGORITHM scrolling the window through the lanes and up to MASTER; then **more space for the VU meter, compressor and sequencer**: the name a short code at the left, the meter (with the compressor's bar) and the sequence across the rest of the width; the mixer keeps the compact 1x4 cards (section 4.1, 11.6) |

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

**Felucca 1.0.1's UI** (its source, GPL-3.0-only, Leo Kuroshita; 5,838 lines of ui*.c) [M]:

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
| **KNOB 1..4** | the four cells of the cursor row; a turn makes its cell hot. **LEN** (the PATTERN row: SOUND, STEP) moves through 1 2 4 8 16 32 64, a list value a detent [D] |
| **SHIFT = LFO held** + a knob | the cell's fine path [D]: **LEN by one**; every other cell turns as without it (LEN only, for now). LFO held + a knob is SHIFT, so the patterns layer (LFO held) keeps its keys and has no knobs (section 11.6); LFO tapped alone still opens the LFO rows |
| **YES** (SAVE tapped) | act on the cursor row: enter a ▸ row, toggle an on / off cell, **do** an action cell, confirm an armed action |
| **NO** (HOME tapped) | back one level; at the root nothing. Cancel an armed action |
| **HOME held + anything** | **clear it** [D]: + a step key on STEP, the step (its notes, events and extras); + a knob, the hot cell's event (a step held) or the cell back to its default; + REC, the selected track's pattern, YES confirms; + a scene or pattern key in its layer, that scene or pattern. HOME held alone does nothing (the menu is a SYSTEM row) |
| **a page button, tapped** | jump the cursor to that family's rows (ENV: the envelope rows of SOUND); tapped again: its next row |
| **HOME held + a key** where the keys play, **SEQ held + a key** on STEP | **pick** [D], "the current mode's button + a key": on the drum track the lane (the key plays the sound, so you hear what you picked); on a synth track, on STEP, the note or chord the next tapped steps carry. The selected lane is one value for the whole device, like the selected track: the mixer's drum column, SOUND and STEP all show it. On STEP the keys are steps, so there HOME + a key clears and the screen's own button picks |
| **a page button, held** | the performance layers, as today (section 4.9) |
| **OCT- / OCT+** | synth tracks the octave, the drum track ghost / hard (as today), on STEP too; **HOME + OCT- / OCT+ on STEP: scroll the 16-step window** over a longer pattern [D] |
| **SAVE then HOME · HOME then SAVE** | **undo · redo** [D]: the order of the pair tells them apart; **EDIT held + OCT- / OCT+** do the same, as SLOOP (open question 12 holds a point to clarify) |
| **PLAY, REC** | transport, as today, in every layer |

**Paging** [D, after the merge: two rules for every screen]. A screen's pages are its rows (a list), its
families' rows (SOUND), STEP's pages (PATTERN, then ARP or the 16 lanes), the mixer's knob sets, the FM6 layer's
pages. (1) **A button tapped again goes to its next page and wraps at the end**: ENV LFO FX SCL ARP EDIT their
families on SOUND, SEQ STEP's pages, GLO the mixer's knob sets (GLO tapped elsewhere opens the mixer). (2) **SELECT
moves through the same pages, forwards and backwards, and stops at the ends** (no wrap: a knob that wraps loses the
user's place; the button's round trip is the fast way back). What SELECT does otherwise, and stays so:

| Where | SELECT | Why |
|---|---|---|
| STEP, a step held | the step's NUDGE | the one exception named |
| SAVE held | the SONG screen (11.4's shortcut) | an earlier decision |
| the mixer | its knob sets (ALGORITHM walks its rows) | the rows are the tracks (4.1) |
| a performance layer held (FX ARP SCL GLO EDIT LFO) | nothing | a layer has one page (its map) |
| NAME | nothing | one page (the field and the keys) |
| PLAY held (TEMPO) | its rows (TEMPO, REC) | PLAY cannot be tapped again while it holds the page open (a tap is start / stop), so its rows are SELECT's only |

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

Decided (2026-10-08): SAVE cannot be a shift, it is used to
save something (preset, pattern, project). So **SAVE held + a page button saves what that button owns**, nothing
else; **HOME held + a page button clears** what it owns, or, when the button's hold is a layer, **locks the layer
open** [D]. There is no positive shift: latching lives on its row (ARP's HOLD). Keys are not shifted
this way: SAVE held + keys stays the scene layer, HOME held + keys the pick or the step clear (section 2). The
order of the two held buttons matters only for SAVE and HOME together: SAVE then HOME is undo, HOME then SAVE is
redo.

| Button | SAVE + it: **save** | HOME + it: **clear / off** |
|---|---|---|
| **SEQ** | **STORE** the selected track's working pattern into its slot (the explicit store of PATTERNS-DESIGN §2.4; *AGAIN* becomes *YES?* over a used slot) | clear the pattern's automation and extras, the notes stay |
| **REC** | tapped: **SONG REC** on / off (there is a REC button): the song is saved as you play it. Held + a key n: **save the loop into scene n**, every track's pattern and the scene (today's "save the loop into the section"; the playing scene's own key updates it) | **clear the selected track's pattern**: notes, events, extras (section 2; YES confirms) |
| **PLAY** | **play the song from the start** (song mode on; PLAY alone stops) [D] | stop, and every voice off at once: the panic (CC120's effect) |
| **ENV · LFO · EDIT · FX · SCL · ARP** (a sound page button) | **save the sound as a user preset** (the slot, the NAME screen); on the drum track, **save the 16 lanes as a user kit** | **lock that layer open** [D]: FX, ARP, SCL, EDIT (erase), LFO (patterns), ENV on an FM6 track; ENV elsewhere: INIT, the preset back over the edits (YES confirms) [P] |
| **GLO** | — | **lock the mix layer open** [D] |
| **HOME · SAVE** | SAVE then HOME: **undo** | HOME then SAVE: **redo** |
| **OCT- · OCT+** | — | on STEP: **scroll the 16-step window** over a longer pattern [D]; elsewhere the octave back to 0 (today: both OCT together) [P] |

What this gives: every save on one button, every clear or lock on the other, each on the button whose job it
modifies, with no screen needed. Everything in the table exists in the core today (`pat_store_slot`, the section store, SONG REC,
the user preset and kit stores, undo, the FX bypass, INIT SOUND) and only gains a gesture. Snapshots stay on their
PROJECT row.

## 3. The look [D]: Felucca's

Felucca 1.0's screen structure carries the grammar as it is:

```
 y   0..24   header: the selected track's engine name in its colour, the screen's name, bar.beat, tempo, a message
 y  28..72   four cards = the cursor row's four cells = KNOB 1..4 (label, value, unit; the hot cell white)
 y  76..239  the panel, to the screen's foot: the list of rows (the cursor row a bar), or a grid (STEP), or a graph
             (SOUND), or the scope; the mixers' strips take 27..239 (no cards)
```

**CARDS 1x4 or 2x2** [D, SYSTEM > SCREEN]: the layout above is 1x4. With 2x2 the cursor row's four values are drawn large, 2 x 2 as the knobs sit (KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom right; label and unit small above the value, its form under it, the hot one white, a name too wide for the large face in the small one), in the band y 28..120; the panel then runs y 123..239 (117 rows): a list shows 5 rows (3 under a picture), SOUND's graph keeps 40 rows, STEP's lanes are 5 px (16 lanes, the playhead strip and a held step's two lines still fit), a layer's tiles are 24 px, SCOPE's trace 94 px, and the modal takes the whole panel. The mixers have no cards and do not change. Kept in the settings word (bit 23).

**No footer** [D, phase 5b: "no footer anywhere"]: the band that held the hints, the keys' meaning and the steps'
playhead went; the panel runs to the screen's foot on every screen. **The header's badge is the selected track's
engine name** in its colour (*ANALOG*, *FM6*, *DRUMS* on the drum track) [D, phase 5b], not the track's number.

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

**Text casing** [D, 2026-10-08]: sentence case for UI words, capitals for short labels. Screen titles,
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

HOME (NO at the root) shows the mixer. **It is horizontal** [D, after the merge of 11.4 and 11.5]: **the rows are the tracks** and **the four knobs are four values of the selected row**,
shown on the cards (1x4 or 2x2, section 3) like any other screen's cursor row. It replaces the vertical strips of
phases 2 to 5b (one track a column, KNOB k track k) and the DRUM MIXER screen.

**The rows**, **four a screen** [D], each a quarter of the panel
(41 px): T1 T2 T3 DR when the mixer opens; ALGORITHM past DR scrolls the window a row at a time through the lanes,
past T1 up it shows MASTER. The mixer keeps the 1x4 cards whatever CARDS says (2x2 would leave 29 px a row):

| Row | Its knob sets (SELECT, GLO tapped again) | Notes |
|---|---|---|
| **MASTER** (above T1) | FILT · THRS · RATIO · DUCK, then DUST · GAIN · CEIL, then the screens | out of view when the mixer opens: it opens on the selected track (T1 at the top); ALGORITHM turned back past T1 reveals it [D]. The master LEVEL is the analog knob: no read-out |
| **T1 · T2 · T3** | VOLUME · INSERT · SEND · PAN, then the rest (the other effects in their slots' order, FILTER, FX on / dry, SOUND), then the screens | INSERT: the first insert effect in the FX slots' order (DIST, COMP, FILTER), its amount (the slot's one value: the core's main parameter); none in a slot: DRIVE. SEND: REV in a slot, else the first send in the slots' order |
| **DR** | VOLUME (GLO > DRUMS) · FILTER · FX on · KIT, then the screens | the drum track as a whole; its sequence shows every lane merged |
| **the 16 lanes** (after DR, indented: kick, kick2, snare ...) | LEVEL · DRIVE · REV · CUT (in PAN's place: a sound has no pan), then DLY · CHO · SOUND, then the screens | the sound's own values (SOUND 2's offsets, SOUND 3's sends); "-" where a sound has none |

The screens set (the last of every row): FX · SONG · PROJECT · SYSTEM, YES enters the hot one; SCOPE is HOME
tapped on the mixer. **YES** on a toggle toggles it (FX on), on any other cell opens the row's SOUND rows (a lane:
that lane's; MASTER: the FX screen).

**The encoders** [D]: **ALGORITHM walks the rows**, T1 T2 T3 DR, then the 16 lanes, and back, stopping at the ends
(MASTER above T1): the track follows (song.sel), and a lane row selects the lane (lane_sel; its sound previews when
the transport is stopped), so ALGORITHM is the lane encoder on the mixer. **SELECT pages the knob sets** (section
2's paging rule), and so does **GLO tapped again** (GLO tapped elsewhere opens the mixer); the header names the set
(*Mix levels*, *Mix more*, *Mix screens*, *Mix master*). PRESETS acts on the hot cell finely; on SOUND / KIT it
browses the row's sounds. HOME held + a drum key still picks the lane, and on the mixer puts the cursor on its row.

**Each row** [D: each row has a VU meter and its sequence below, like in SLOOP, with more space for the
VU meter, compressor and sequencer]: at the left a colour chip and a short code (T1 T2 T3 DR, a lane's BD SD CH
..., M for MASTER) with its M / S badge under it, 22 px; the rest of the width (214 px) is a **horizontal VU meter**,
16 px tall (the level from the left, its peak falling), with **the compressor's gain reduction as an amber bar
pushing in from the right** where a compressor works (a part's COMP insert; MASTER: THRS or CEIL set), 3 px a dB,
and under it **the row's sequence** as SLOOP's TRACKS screen draws it: the 16 steps of the page playing, 13 px a
step, in the row's colour, the playhead white, the steps past LEN empty, a thin bar a page under them when LEN > 16.
DR shows every lane merged, a lane only its hits. The selected row is framed in its colour, its ground tinted, and
its full name is the header's with the set (*Snare levels*, *T2 more*, *Mix master*). No pan, send or insert forms
on the rows: the cards show the selected row's values.

**Kept**: the four levels on four knobs at once, the balancing gesture, stay on **GLO held** (the mix layer, section
4.9); mute and solo stay there too.

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
| no key held, PATTERN (the first page, every track) [D] | the pattern: LEN · DIV · SWING · GATE (LEN 1 2 4 ... 64, SHIFT by one) | the pages: PATTERN, then ARP, ARP 2 (synth) or the 16 lanes (drums) | the hot cell | — | tapped: back |
| no key held, a lane's page (drums) | the lane's sound: LEVEL · TUNE · DECAY · REV | the pages; landing on a lane selects it (the lane encoder; previewed when stopped) | the hot cell | open the lane's SOUND rows | tapped: back. Held + a lane's key: clear the lane's steps [P] |
| **a step held** | drums: LEVEL · RATCHET · — · —; synth: NOTE · LEVEL · RATCHET · LENGTH (ties, as today) | **NUDGE** (micro, -32..31) | **CHANCE**, drums and synths alike (an event of the automation store, section 6) | cycle the fill condition: normal, FILL, NO FILL | **HOME + the step: clear it** (notes, events, extras) |
| **a step held + a page button** (ENV, LFO, FX, SCL, ARP) | that page's four cells **as the step's locks**: a cell with an event shows a lock mark; a turn writes one (section 6) | the family's next row | the hot cell, fine | toggle HOLD of the hot cell's event | HOME + the knob: clear the hot cell's event |
| **SEQ held + keys** | — | — | — | — | — |

SEQ held + keys is the **pick** [D] (the screen's own button: on STEP the keys are steps, and HOME + a step clears
it): on drums a white key picks the lane and plays its sound; on a synth the keys play and the notes (one, or
several for a chord) become what the next tapped steps carry. Letting SEQ go keeps the pick. A tapped empty step takes the pick
(today's rule: "the sound shown / the note you played last"); a set step tapped is cleared; a held step is edited
and kept, several held together (as today).

What a step key does when nothing is held, in short: tap an empty step, it is set with the pick; tap a set step, it
is cleared; hold it and turn, it is edited. **SEQ tapped again goes to STEP's next page** (the paging rule; STEP
opens on PATTERN); **SEQ held alone past HOLD and let go** toggles the keys to playing with the STEP screen still up
(to audition while the grid follows the playhead); the same again, steps [D, 11.6].

The erase-as-it-plays gesture (MPC style: hold the sound's key while the pattern plays and its hits leave) stays
**EDIT held + the key**, as today, on every screen: a performance gesture, not an edit.

### 4.3 SOUND

The selected track's pages as rows, in today's PAGES order (params.c:488): SOUND (the preset, the engine, INIT,
SAVE AS), ENV, ENV DEST, ENV2, ENV2 DEST, LFO, LFO DEST, EDIT 1, EDIT 2, OSC 2, SWARM, FLT 2, VOICE, VOICE 2,
FX (the sends), FILTER, SLICER, SCL, SCL 2, ARP, ARP 2, PATTERN. The panel draws the cursor row's graph (ADSR,
LFO, the FX bars, the scale), as Felucca. **A page button tapped shows only its family's rows** [D, 2026-10-08: the LFO screen should not show ENV2 and SLICER rows]: LFO the LFO and LFO DEST rows (*Sound LFO*, its graph),
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

A tempo page opened by holding a button, with nudge (OCT+/-), fine adjust,
MIDI sync and so on, was asked for; the page below is the decided form. **PLAY held** (past the hold threshold; a tap stays start /
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
| **LFO** (patterns) | **1..16 launch pattern n** of the selected track at its end (OCT- held: next bar, OCT+ held: now, as built); **REC held + key n: store** the working copy into n; **HOME held + key n: clear** n [P] | none: LFO held + a knob is **SHIFT** (section 11.6; the cue was the built layer's, taken as a default) | [O] decided on the hardware: this uniform vocabulary (ALGORITHM is the track, copy is a SONG row action, duplicate is the PATTERNS row's REC), or the built black-key modifiers (6 store, 7 copy, 8 clear, 9 duplicate, 1..4 the track), or both |
| **SEQ** | the pick on STEP (section 4.2) | — | today's step layer is the STEP screen |
| **ENV** on FM6 | the operator editor, as built | | none |
| **PLAY** | the TEMPO page [D] (section 4.8) | | new |

### 4.10 SCOPE [P]

SLOOP 2.4's visualiser styles add little; an oscilloscope view for the master and per track, and levels on the mixer
with the compressor's effect, are preferred. So no visualiser styles in this UI (the VIS builder item stays
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

**Chance is an event** [D] (it is just p-lock / motion data): a step-only event whose parameter is the
pseudo-parameter CHANCE, an id past P_COUNT that is never a sound value, on drums and synths alike. The synth step's
chance bits (chance.c) stay readable, an event wins over them, and the new UI writes events only. The drum step gets
chance with no format change.

**Nudge and the fill condition are events too** [D]: the pseudo-parameters NUDGE (-32..31, in 1/64 of a step) and
FILL (1 FILL ONLY, 2 NO FILL), step-only. One list then holds every per-step extra, and SLOOP 2.4's stepx record
(seq/stepx.h, 176 B a track) is only the import / export form: its micro, lock and fill arrays become events on
read and are rebuilt from the events on export. To be revisited if the size or the
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

**Measured 2026-10-08** (aaa284f, `tools/optimist.py build --profile user-default --measure`, the port in a
scratch tree): user-default is **2,592 B under the slot** (578,972 of
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
commit for review. Effort in focused working days [E].

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
    clarify**: the exact behaviour of EDIT + OCT on the drum track, where OCT alone is
    ghost / hard (today EDIT + OCT is undo there too, the hits are not affected), and whether EDIT + OCT inside the
    erase layer undoes the erase just made or the level before it.
13. **Every per-step extra as an event**: decided, one list of 128 events a pattern; revisit if the size or the
    sequencer's scan bites.
14. **The DRUM MIXER's entry**: decided, the pick from the mixer; its rows are the mixer's.
15. **The DRUM MIXER's compact view**: 8 or 16 columns on one screen as a SYSTEM setting (section 4.1): evaluate
    with a mock-up on the emulator; 16 columns leave about 13 px each. Built for the evaluation: section 11.5.
16. **The macros** (COLOR MOTN SPACE ENRGY), parked: a second MASTER row on HOME, the FX screen's MACRO row only,
    or not in this UI.
17. **SCOPE** (section 4.10): the entry (a row, or HOME at the root toggling mixer and scope), and one switched tap
    or four always-on ones for the per-track views. Built: both entries, one switched tap (section 11.5).
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

**Decisions open for review** (how to undo each):

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

The review of phase 1 (section 0, the last three rows): tracks coloured after their instrument, the
questions as a modal popup, and a graphic for every value; then, on the 1b screenshots, the modal's hints in the
buttons' order and sentence case (section 3, *Text casing*). Built on the phase 1 branch, UI=1 only; UI=0 is untouched.

**What changed** (`firmware/src/ui/optimist/`, 2,448 lines; the new file `op_graph.c`, 287 lines, in SIZE_FILES):

- **Track colours.** Every place this UI shows a track draws it with `trk_col`: its engine's colour, the drum track
  its kit's kind (tools/colors.json): the header's badge, the mixer's strip (numeral, fader, forms), the SOUND
  cursor bar, the modal's target. There is no colour per track number; sections 2 and 4.1 say so now.
- **The modal.** A question is a box over the panel (the header and the cards stay): `draw_modal`, one 240 x 123
  canvas (gfx.c's 124-row band). The question in the large font on one line when it fits, *CLEAR DR?*, the target
  in its colour; when it does not (*SAVE PROJECT 1?*), the verb over the target (*SAVE?* / *PROJECT 1*). *HOME no* on
  the left and *SAVE yes* on the right at its bottom, as the two buttons sit on the panel (decided after the first
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
`opt-*.png` (the modals of a track clear, NEW and a SAVE over a used slot, the toast). The emulator CPU budget test was not run.

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

**Decisions open for review** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| Toast or header | **a toast** for what a confirmed action says; **the header** for passive status and for the result of an action that did not ask (a SAVE into an empty slot, REC OFF, UNDO). A toast goes when the screen changes or a question is asked | `toast_next` in op_input.c `op_yes` |
| The modal's text | the whole question on one line in the large font when it fits 208 px, the target in its colour; else the verb over the target (the small font when the target is too wide for the large) | `draw_modal` |
| Red or amber | red: clear, erase, reset, INIT, NEW, LOAD, an overwrite (a project or user preset SAVE over a used slot, a user kit SAVE), ACID GEN, the 2.4 import; amber: a snapshot SAVE into an empty slot (the only question that destroys nothing) | the `danger` argument of `op_arm` |
| The modal's extent | over the panel only, the panel around the box black; no countdown drawn (3 s still let it go) | `draw_modal` |
| Casing: what is not cased | values (OFF, SIN, ANALOG, YES: the descriptors' names, as SLOOP shows them), preset, kit and lane names, the mixer's M / S / R, the boot and setup screens' names (op_input.c) | `op_case` callers |
| The header | the screen and its row as one sentence (*Mix master*, *Sound ENV*, *Project snapshot*); a row that starts with the screen's name stands alone (*Project*, *Sound*), so no header repeats a word (a host check over every screen and row); the font has no middle dot for *Sound · ENV* | `head_title` |
| Casing: the exception list | the agreed list (DR, T1..T3 through the digit rule, FX, LFO, ENV, MIDI, USB, BPM, CPU, OCT) and ENV2, GLO, ARP, SCL, UI, CC, ACID, GEN, MHZ; a message's lane name is cased with it (*Reset pedal hat*) | `case_keep` |
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

STEP (section 4.2), and two rulings made while it was built: SOUND's page buttons show their family only
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

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state, `--fresh`): a user-default package with UI=1 **and PLOCK, MICRO, FILLS, CHANCE, SL24_XSTEP**
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

**Decisions open for review** (how to undo each):

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
SONG screen (4.5), and two mixer rulings made during the phase (section 4.1: no master values at the
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

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state `--fresh`; a user-default package with UI=1 and PLOCK MICRO FILLS CHANCE SL24_XSTEP, as phase 2's):
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

**Decisions open for review** (how to undo each):

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

**Proposals** [P]:

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

The rulings after phase 4 (section 0: the SONG shortcut, graphs on every row of a family, a preset shown
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

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state `--fresh`; a user-default package with UI=1 and PLOCK MICRO FILLS CHANCE SL24_XSTEP, as phase 4's):
`build/ui-optimist-shots/p5a-preset-engine.png` (SOUND row: *Solid bass* FM6 between *LP24 bass* ANALOG and *Saw
bass* FM6), `p5a-fm6-layer.png` (ENV held on FM6: OP1's FREQ page, the algorithm, the black keys), `p5a-fm6-layer-op.png`
(a black key: OP6), `p5a-fm6-operator.png` (ENV tapped: the OPERATOR row), `p5a-fm6-page.png` (ENV again: *OP6 freq*),
`p5a-mixer-toast.png` (*Saw bass (FM6)*), `p5a-lfo-dest.png` and `p5a-env-dest.png` (a DEST row with its family's
graph), `p5a-name-open.png` (SAVE + ARP: NAME prefilled *Digital 01*) and `p5a-name.png` (*Bass* typed). Seen on
them: in SOUND's band the algorithm's deep stacks (algorithm 1: 6 over 5 over 4) overlap their digits (the boxes 8 px,
the digits 16 px); the layer's full panel draws them clean. Left as is (the look is accepted); a fix would draw the
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

**Decisions open for review** (how to undo each):

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

**Found in the spec** (not changed; the reading taken): section 0 says "SAVE cannot be a shift", while the
shortcut is SAVE held + SELECT: SELECT is no page button, so 2.1's table (SAVE + a page button saves what it owns)
still holds. Section 0 "Names: projects get names" against the four RAM project slots, which have no room for one
without a format change: those builds save unnamed (names must not need a format change; the log's
id 23 needs none). 2.1's ENV row ("the lock on an FM6 track", INIT "elsewhere") now reads as built. 11.3's "ENV is
no layer here" and "HOME + SEQ not in the undo history" are superseded above. 4.3 shows no FM6 family on SOUND:
ENV tapped is it, on FM6 tracks.

**Not built**: the footer's removal on the other screens (feat/ui-drummix); names of user kits (kits keep numbers,
section 0); NAME for snapshots; the emulator CPU budget test.

### 11.5 Drum mixer, scope, no footer (feat/ui-drummix, 2026-10-08)

The DRUM MIXER (section 4.1), SCOPE (4.10), the mixer's master column with the compressor's reduction (4.1), and the
two rulings of the phase (section 0): the header's badge is the selected track's engine, and there is no
footer anywhere. UI=1 only: the audio side is under `#if FELUCCA_UI == 1` in fx.c, so the SLOOP-UI build (UI=0) is
unchanged (measured below).

**What changed** (`firmware/src/ui/optimist/`, new: `op_dmix.c` the drum mixer's rows, keys and view; `op_dmixdraw.c`
its strips; `op_scope.c` SCOPE and the master column, all three in SIZE_FILES):

- **The DRUM MIXER** [D]. The drum track's 16 sounds as mixer strips, KNOB k the block's lane k. Entry: the pick,
  HOME held + a drum key on the mixer (the key plays the sound): it opens on that sound's block with the sound
  selected; the same pick moves inside it; HOME + OCT- / OCT+ scroll the block (no NO); HOME tapped goes back to the
  mixer; ALGORITHM to a synth track goes back too (the drum mixer is the drum track's). The rows are the mixer's
  [D: like other tracks]: LEVEL (the sound's, SOUND 2: the fader), CUT (its filter cut, in PAN's place: drawn at
  the strip's foot, from the centre), REV · DLY · CHO (its sends, SOUND 3), DRIVE ("-" on a sampled sound), FILTER
  and FX ON ("-": the track's, on the mixer's drum strip), SOUND (the names; YES opens the hot sound's SOUND rows).
  The strips look and work as the mixer's: no cards, SELECT walks the controls, the control lit on the block's four
  strips, PRESETS the hot strip's value one unit, HOME held + a knob its default. Each strip in its source's colour
  (`lane_col`: the drum synth, a sampled kit, an X0X voice, your sample); the selected sound framed with its head
  tinted; a hit lights a flash in the meter's place (the kit pads' 6 frames, falling); the lane's 16 steps playing.
- **The compact view** [O, question 15, built to evaluate]: SYSTEM > SCREEN > DR MIX (KNOB 3, a two-way pill): 4
  strips (the default, the mixer's geometry, the master column on the right) or 8 (29 px: a two-letter code BD B2
  SD ..., the knob's number under the block's four, no steps; lanes 1-8 or 9-16, the page holding the knobs'
  block). The knobs still edit the block of four; the other four are on a darker ground. Kept device-wide in the
  settings word, bits 21..22 (`storage/settings_word.c`; a word without them reads 4, a value this build has not
  reads 4 or 8; SLOOP 2.4's word translation does not carry them). **16 strips were built, looked at on the
  emulator and left out** (below).
- **SCOPE** [P, section 4.10]: rows MASTER T1 T2 T3 DR; the panel the oscilloscope of the cursor's source,
  trigger-stable (the latest rising zero crossing after a swing below an eighth of the peak that leaves a whole
  screen), 236 points 2 frames apart (10.7 ms), scaled to the peak (a quieter wave grows a step a frame), 30 frames
  a second, the master in the palette's ink, a track in its instrument's colour; the sources as tabs under it. The
  cards: a track's LEVEL PAN FX DRIVE (the mixer's values), the master's FILT and the compressor's THRS RATIO and GR.
  Entry: a SCOPE row at the end of the mixer's walk (YES), and HOME tapped on the mixer [P, question 17]; HOME
  tapped on SCOPE goes back.
- **The master column** [P, section 4.1]: 10 px on the mixer's right edge (and the 4-strip drum mixer's): the
  master's meter from the scope's ring (its last 16 ms, as heard with MASTER up) and the master compressor's gain
  reduction (`meters.c mc_gr_view`, whole dB, the COMP's and the LIMIT's) as an amber bar pushing down from the top,
  4 px a dB. The strips went from 57 to 55 px for it.
- **The header** [D]: the badge is the selected track's engine name (*ANALOG*, *FM6*; *DRUMS* on the drum track) in
  its colour, its width the name's; the title is cut to the room left. On STEP the loop position gives its room to
  the pick: *Steps 1-16 c.hat* on the drum track, the pick's notes on a synth.
- **No footer** [D]: the panel runs to 239 on every screen (two canvases a band: `cv_tall`, gfx.c's canvas holds 124
  rows). The lists show 8 rows (5 under a picture: SOUND's graph, TEMPO, SONG's grid); the mixer's fader and meter
  are 90 px (were 56); STEP's grid keeps its 16 x 7 px lanes and a held step's two lines go under it (*Step 6  Fill
  only*, *Nudge +4  Chance 85%*); a layer's tiles are 36 px high with its state under them (the key and scale, the
  chain, a modifier held); a layer says *Home locks it* in the header the first time it opens after power-on (once
  per layer; not the SAVE layer, which does not lock), and the header still says *FX locked* while locked. Under a
  question the modal covers the panel's first 123 rows and the rest is blank.

**The audio side** (fx.c, `#if FELUCCA_UI == 1`): one ring, the visualiser's (`vis_pcm`, 1024 frames a side, 8 KB;
built when FELUCCA_VIS or FELUCCA_UI == 1), switched by a byte the main loop writes (`scope_src`, `scope_tick` once a
frame) and the ISR reads: 0 the master (mix_finish: the mix after the buses and the master compressor, before the
volume, the limiter and the knee, as the visualiser takes it), 1..3 part n's block in `mix_part` after its inserts
(DIST, SLICER, FILTER, COMP) and before its level and pan, 4 the drums' part of the mix (`scope_drums_mark` before
`slicer_drums`, `scope_drums_take` after: the mix after them less the mix before). A silent part writes nothing (the
trace stays flat). Off SCOPE the source is the master, so the master tap runs on every block under UI=1 (the master
column reads it): 2 x n words copied a block, in a word loop (it was two `memcpy`, and libc.c's goes a byte at a
time: the profile below found it at 1.5 % of the core; the SLOOP UI's visualiser shares the function and the gain).

Tests [M]: `tests/ui_optimist_drummix.h` (included by `tests/ui_optimist_test.c`, all five switch sets): the pick
opens the drum mixer on the sound's block, the walk and the header, no cards, the selected strip framed, LEVEL lit
on the four strips, CUT at the foot, KNOB 2 / PRESETS / HOME + a knob on the right lane, REV on KNOB 4, DRIVE and
FX ON "-", the pick inside, HOME + OCT- / OCT+ (stopping at the last block), a hit's flash drawn and falling, the
SOUND row's YES, HOME back, ALGORITHM to T3 back; DR MIX's card, the settings word's bits both ways; 8 strips on the
screen, a pick in lanes 9-16 shows them, DR MIX stops at 8, a word with 2 or 3 in the bits reads a built view;
SCOPE's row and YES, the ISR copying part 2's block (a note held), a silent part not writing, DR without T2's note,
MASTER, the cards, the trigger on a sine, HOME back with the source to the master; the master column's GR bar (6 dB:
24 px) and none at 0; the header's engine on a synth and DRUMS on the drums, STEP's pick in the header, SOUND's rows
to the foot, *Home locks it* once and gone with the layer. The existing checks follow the bands (no footer, 8 list
rows, the 90 px faders). 239 to 266 checks a switch set. Host renders `build/host/optimist/opt-dmix-{4,8}.ppm`,
`opt-scope-{master,t2}.ppm`, `opt-mixer-gr.ppm`, `opt-step-head-pick.ppm`. All of `tests/run_tests.sh` green,
`builder_test.py` green, `tools/div_audit.py` green (three entries renamed with the functions: `list_paint`,
`draw_steps_of`, `strip_fader`).

Emulator [M] (fm1-emulator `play_check`, 96 MHz, its own flash state `--fresh`; phase 4's preamble: drum and T1 steps entered, playing; a user-default package with UI=1 and
PLOCK MICRO FILLS CHANCE SL24_XSTEP, 566,524 B): `build/ui-optimist-shots/p5b-mixer-gr.png` (the master column; this
configuration's compressor is off, so GR reads 0 and no bar shows: the host render `opt-mixer-gr.ppm` has one at
6 dB), `p5b-dmix-4.png` (the hats' block, P.HAT picked, its hit flash), `p5b-dmix-4-rev.png` (REV lit on the four),
`p5b-dmix-8.png` (lanes 1-8, the knobs on 5-8), `p5b-dmix-8-lanes9-16.png` (the pick on the crash),
`p5b-system-drmix.png` (SYSTEM > SCREEN, DR MIX 8), `p5b-scope-master.png`, `p5b-scope-t1.png` (the 808 bass),
`p5b-scope-dr.png` (a kick: 10.7 ms is less than one of its cycles), `p5b-list-sound.png` (SOUND ENV, 5 rows under
the graph), `p5b-step-held.png` (step 1 held: *Step 1*, *Nudge 0  Chance 100%* under the grid),
`p5b-step-drums.png` (*Steps 1-16 p.hat* in the header), `p5b-layer-fx.png` (*Home locks it* where the title was,
the tiles to the foot). (On the emulator a SELECT turn of 30 detents was still arriving when SAVE went down, which
made the SAVE a used hold, not a YES: the script turns the exact count.)

**The 16-strip view: left out** (the evaluation question 15 asked for). Built and looked at on the emulator at
1:1: the names (two letters stacked), the 5 px faders and the 4 px hit flashes read, as an overview of the kit;
the control rows do not: at 14 px a strip's CUT, sends and DRIVE are 8 px bars (two or three tellable levels) and
the drum mixer has no cards, so no number shows a value anywhere; the knobs' block is told from the rest only by a
slightly lighter ground. The 8-strip view shows the same sixteen sounds in two pages with 23 px bars and the knob
numbers, so 16 added a whole-kit glance and nothing an edit can use. Shipped: 4 (default) and 8. Undo: revert the commit
*refactor(ui): the drum mixer without its 16-strip view* (its geometry: 14 px, the stacked letters; `DMV_16`).

**CPU** [M] (emulator, `FM1_HOT` profile of the primary core, 96 MHz, 4 s playing the preamble's pattern: the 808
bass on T1 and the drums; same method): the master tap on the mixer (always on under UI=1) **0.25 %**
of the core (`vis_tap_block`, 0.96 M instructions in 384 M; before the word loop 1.49 %, 5.74 M); SCOPE on T1
0.10 % (the tap runs only while the part sounds; the master tap is off then); SCOPE on DR 0.73 % (`scope_drums_mark`
0.34 % + `take` 0.39 %: two passes over the block). Interrupt handlers took 34-35 % of the core in each window. The
host budget (tests/regress.c, the CPU entries in instructions per sample) is unchanged: the host tests build the
audio without FELUCCA_UI; a regress build with `-DFELUCCA_UI=1` (not in the suite) measured +4 to +9 instructions a
sample a render and +17 on the 3-part mix (under 1 %, every entry within its budget), with the old `memcpy` tap.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`, UI=0 and `--set UI=1`; the slot is
581,564 B):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| optimist 924c7c2 (phase 4's base) | 580,276 | 80,728 | 307,376 | 30,832 |
| this branch, UI=0 | **580,276** (unchanged) | 80,728 | 307,376 | 30,832 |
| phase 4, UI=1 | 554,244 | 78,680 | 307,376 | 30,784 |
| **phase 5b, UI=1** | **557,464** (+3,220; 24,100 free) | **87,384** (+8,704: the scope's 8 KB ring and its state) | 307,376 | 31,096 (+312) |

Nothing was left out to fit. The 16-strip view's code was in the first UI=1 measurement (557,816 B, +352).

**Decisions open for review** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| The drum mixer's rows | the mixer's walk with CUT in PAN's place (4.1's [P]); FILTER and FX ON kept as "-" rows so the walk matches the mixer's | `DMX[]` in op_dmix.c |
| DRIVE on a sampled sound | "-" (it has none) | `dm_desc` |
| A sound's LEVEL / CUT / DRIVE | the lane's offsets (`dl.ofs`, SOUND 2's), the sends `dsend_set` | `dm_set` |
| HOME + OCT on the drum mixer | the block before / after, stopping at the ends (no wrap); OCT alone keeps its job | `dm_scroll`, `op_press` |
| ALGORITHM on the drum mixer | to a synth track: back to the mixer | `dm_tick` |
| The hit flash | the kit pads' 6 frames, white at the hit then the sound's colour, falling | `dm_tick`, `dm_draw` |
| The compact view's geometry | 8: 29 px, two-letter codes, the knob's number, no steps, the non-knob strips on a darker ground; 16 left out (above) | `op_dmixdraw.c` DM_PITCH8, LANE_CODE, DM_BG_OFF |
| DR MIX's place | SYSTEM > SCREEN, KNOB 3; device-wide in the settings word bits 21..22, not a project's | `SYS[]`, `settings_word.c` |
| SCOPE's master tap point | before the volume, the limiter and the knee, as the visualiser | `mix_finish` |
| SCOPE's track tap point | after the inserts, before level and pan (the track's sound, not its place in the mix) | `mix_part` |
| SCOPE on DR | the mix after the drums less the mix before (dry; the drums' sends are not in it) | `scope_drums_mark` / `take` |
| The trace | trigger on a rising zero crossing after a swing below peak / 8, 10.7 ms a screen, auto-scaled, 30 frames a second, mono (L + R) / 2 | `op_scope.c` SC_* |
| SCOPE's master cards | FILT THRS RATIO GR (4.10 had LEVEL: the analog knob since phase 4) | `SCOPE_MASTER` |
| The master column | 10 px, the strips 55 px; the meter from the scope ring's last 16 ms; GR 4 px a dB from `mc_gr_view` | `MX_*`, `master_col` |
| The master tap off SCOPE | always on under UI=1 (the master column needs it) | `mix_finish`'s `if (!scope_src)` |
| The header on STEP | the loop position dropped for the pick | `draw_head` |
| *Home locks it* | once per layer per power-on, in the header's message slot, cleared when the layer goes; none for SAVE's layer | `lay_hint` |
| A held step's lines | under STEP's grid, white then grey | `step_info`, `sg_paint` |
| Under the modal | a blank band (the modal keeps its 123 rows) | `draw_under` |

**What the footer said and where it went** (the ruling, read literally): STEP's hints and pick: the header;
a held step's values: under the grid; a layer's state: under its tiles; *Home locks it*: the header, once. Gone with
no new place: *Save open* / *Save toggle* / *Home back*, *Keys play T1* (the lane on the drum track), the selected
track's 16 steps, TEMPO's *Oct nudge  Keys tap* / *Play let go: back*, SONG's PATTERNS row *Keys launch T1*, a locked
layer's *Any button lets go*, the SAVE layer's *Home + key clear  Play song*. To bring one back: a header message
when the screen or row is entered (as `lay_hint`).

**Found in the spec** (not changed; the reading taken): 4.1 gives the drum mixer "PAN '-'" and "CUT may take PAN's
place": CUT took it, no PAN row. 4.1 draws "the level ring" (Felucca's, phase 5): faders as the mixer's. 4.1 sizes
the compact views on Felucca's 220 px panel (55 / 27 / 13 px): the strips use the screen's 240 (55 / 29 px; 14 px for 16, left out).
4.10's master cards include LEVEL, which phase 4's ruling took off the screen (the analog knob). 3's layout and 0's
*Questions* row still described the footer (updated in 0 and 3). 0's *Track colours* row names "the header's badge"
in the track's colour: it is, now with the engine's name in it.

**Proposals** [P]:

1. **HOME tapped at the mixer toggles mixer / scope** (built, question 17): HOME tapped at the root did nothing
   before. Undo: `op_no`'s first branch.
2. **Sixteen sounds at a glance** without the 16-strip view: on the 8 view, HOME + OCT already moves the knobs' block across both pages; a level-only row of 16 small meters over the strips would give the whole kit's hits on one screen.
3. **The lost hints** (above): one header message when a screen is entered, or none.

### 11.6 Integration and LEN (feat/ui-optimist, 2026-10-08)

**The merge.** feat/ui-drummix (11.5) then feat/ui-loose (11.4) into feat/ui-optimist (phase 4 + w0ts/main 27dc239),
each a `--no-ff` merge. The rulings decided the conflicts: no footer anywhere, so loose's new panels paint to
the screen's foot like the others (the SOUND list's two new pictures, FM6's algorithm and the presets with their
engines, went into 5b's two-pass `list_paint`; ENV held on FM6 paints its map through `cv_tall` too); the header's
badge is the engine (5b), NAME's title takes the header's place and its hints stay in its own field (11.4). One name
clash with w0ts/main: NAME's black-key enum is `NMK_*` (`NB_NONE` is storage/nbank.c's). Both branches' tests run.
UI=0 user-default is byte for byte the size of optimist 27dc239 (flash 488,788 B, RAM 79,192, pool 307,376,
RAMTEXT 30,872).

**LEN** (section 0): the PATTERN row's LEN (SOUND, STEP) moves through **1 2 4 8 16 32 64**, a
list value a detent; from a value off the list, the next list value in the turn's direction (12: up 16, down 8);
**SHIFT + a knob moves it by one**, and PRESETS on the hot LEN is by one as on any cell. **SHIFT is the LFO button
held**: `ui.shift` (op_state.c), set once a frame in op_input.c `op_knobs`, read by op_cells.c `page_turn`
(`len_pow2`, `len_cell`). Every other cell ignores it for now (LEN only). EDIT held's LENGTH (x2 / half,
the pattern copied after itself) is unchanged: it already halves and doubles. SONG has no LEN cell; TEMPO's REC
LENGTH stays 1 / 2 / 4 bars.

**Decisions open for review** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| LFO held is also the patterns layer, whose KNOB 1..4 cued each track's next / previous pattern | **a knob turned while LFO is held is SHIFT**, never the cue: the patterns layer keeps its keys (launch, the black-key modifiers) and loses the knob cue; its cards show each track's pattern, read only. SONG's PATTERNS row still cues with its knobs | move the shift elsewhere: `ui.shift` in `op_knobs`, the `LY_PAT` return in `lay_knobs`, `pat_knob` back in `lay_turn` |
| What the screen shows after LFO held + a knob | the screen under it (the layer's map is hidden for the rest of the hold, `lay.quiet`), so the LEN being edited is seen; the keys still launch | the `lay.quiet` line in `op_knobs` |
| LFO tapped alone | still opens the LFO family (a knob turned while it was held makes it no tap) | — |
| SHIFT where LFO is no layer (builds without PATTERNS) | the same: LFO held + a knob is SHIFT | — |

Tests (tests/ui_optimist_len.h): LEN up 16 32 64 and stopping, down to 1 and stopping, from 12, 3 and 33, two
detents at once, LFO held = by one, PRESETS by one, another cell with LFO held as ever; on the PATTERNS build the
patterns layer's keys launching while LFO is held and no cue; LFO tapped opening its family.

**CARDS 1x4 or 2x2** (section 0, 3): SYSTEM > SCREEN's fourth cell, *CARDS* 1x4 / 2x2, in the
settings word's bit 23 next to DR MIX's bits (FELUCCA_UI == 1). `op_cards` (op_state.c); the bands follow it
(`OP_CH`, `OP_PY`, `OP_PH`, `MODAL_H` in op_graph.c); `draw_big` and `draw_card_band` (op_draw.c) draw the 2 x 2
block (SLOOP 2.4's graph_big layout, copied: ui/sloop is not called); the cards of the screens and of the layers
both go through `draw_card_band`. What shrinks in 2x2: `ROWS_SHOWN`, `GRAPH_H` (40: the envelope, the LFO, the FX
needles, the scale scale with it; the pattern's steps in two rows of 12 px; the drum lane's kit name beside its
name; the SOUND row's presets picture shows the one playing only; TEMPO's picture drops its clock line, SYNC being a
card), STEP's `SG_LH` (5) and its held-step lines' pitch (13), the tiles' `TILE_H` (24), SCOPE's `SC_H` (94),
FM6's map (`F6_MAP_Y` 80). Tests (tests/ui_optimist_cards.h): the band as each mode draws it on SOUND with and
without a graph, the SOUND row, STEP (roll and drums), TEMPO, SONG, FX, PROJECT, SYSTEM, SCOPE and a layer; the
geometry inside the panel; KNOB 4's value white at the bottom right; the setting and its bit; 3000 frames of random
use in 2x2.

| Question | Chosen | Undo |
|---|---|---|
| The default | **1x4**: 2x2 reads well (the values are twice the size), but it costs the panel 47 rows on every screen (a list 8 to 5 rows, STEP's lanes 7 to 5 px, the graph 58 to 40), and most screens are read for their panel (the list, the grid, the graph); 2x2 is the option for reading the four values at a distance | `op_cards`' initial value (op_state.c) |
| The setting's values | *1x4* / *2x2* (fit a card; "4 IN LINE" does not) | `sys_cell` SI_CARDS |
| A value too wide for the large face | the small face in its place (a preset name) | `draw_big` |

**HOLD** (a bug found on the FM-1, section 0): `op_hold_ms()` (op_state.c, SYSTEM > CALIBRATE's second
cell, 250 / 350 / 500 ms, default 350; the settings word's bits 24..25, 0 = 350 ms so a word without them keeps the
default). It replaces op_layers.c's `LAY_SHOW_MS` (140 ms, SLOOP's SHOW_MS: a layer's map) and op_tempo.c's
`TEMPO_HOLD_MS` (400 ms: PLAY held for TEMPO); `LAY_TAP_MS` (450 ms: a page button let go later is no tap) becomes
HOLD + 150 ms, so a slow click that just showed the map still counts as a tap. SAVE and HOME taps have no time limit
(a release with nothing else pressed meanwhile): unchanged. ui/sloop is not touched (its 140 ms is reported to its
branch). Tests (tests/ui_optimist_hold.h): 200 and 300 ms clicks are taps with no map, 420 ms shows the map and is
still a tap, 600 ms is no tap, the setting at 500 and 250 ms, its bits, PLAY's TEMPO at the same threshold.

| Question | Chosen | Undo |
|---|---|---|
| Where HOLD lives | SYSTEM > CALIBRATE's second cell (the panel's behaviour; SCREEN's four cells are taken) | `SYS[]` in op_project.c |
| The tap's grace after the map shows | 150 ms (`OP_TAP_GAP`) | op_state.c |

**The horizontal mixer** (section 0, 4.1): op_mixer.c (the rows and knob sets: `mx_items`,
`mx_insert`, `mx_send`, `mx_walk`, `mx_page`, `mx_glo_tap`, `mx_tick`) and op_mixdraw.c (the rows: `mx_paint`,
`mx_vu`, `mx_steps`) replace op_screens.c's MIX table, op_draw.c's strips, op_dmix.c and op_dmixdraw.c (the DRUM
MIXER, its SCR_DMIX and SYSTEM > SCREEN > DR MIX: gone, the settings word's bits 21..22 free and kept as read) and
op_scope.c's master column (the MASTER row carries the master meter and GR). The core gained one read-only accessor,
fx.c `tcomp_gr_q4(k)` under `FELUCCA_UI == 1` (a part's COMP insert's reduction now, dB x 4, from its `mc_t.gr16`),
for the per-track GR bar. The cards are back on the mixer (its rows no longer carry the knobs' mapping). Tests
(tests/ui_optimist_mixer.h, which replaces ui_optimist_drummix.h; SCOPE and the header's tests kept): the walk with
ALGORITHM both ways through the 16 lanes, MASTER only going up, the cursor following the track, the knob sets per row
kind, INSERT on COMP and DIST, SELECT's ends and GLO's round, KNOB 1 on a track and a lane, YES on a lane, on SONG,
on FX ON, every set's forms, DR's merged sequence and a lane's own, a track's VU, the master and a part's GR bar and
their absence, the GLO layer's levels, CARDS 2x2.

| Question | Chosen | Undo |
|---|---|---|
| ALGORITHM past DR | into the lanes, on the same screen (the DR row stays framed as the drum track, the lane row lit); no separate drum mixer to switch to | `mx_walk` |
| The screens (FX, SONG, PROJECT, SYSTEM) | a last knob set on every row, YES enters the hot one; SCOPE stays HOME tapped | `mx_items` |
| YES on a value | opens the row's SOUND rows (a lane: that lane's, MASTER: the FX screen); a toggle toggles | `mix_yes` |
| SELECT and the ends | stops at the first and last set (section 2's rule: SELECT does not wrap, a button tapped again does) | `mx_page` |
| A lane's meter | its hits (drums.hits), full at the hit, falling over 12 frames: the drum voices have no level of their own to read | `mx_level_px` |
| The sequence when LEN > 16 | the 16 steps of the page playing, with a mark a page (the one playing lit), not the whole pattern squeezed: 9 px a step reads, 2 px would not | `mx_steps` |
| The small forms (pan, sends, insert) on a row | left out for the meter and the sequence; the cards show the selected row's values | op_mixdraw.c |
| The per-track GR | built: one word read from the part's compressor state (no new state, no lock); the drum bus has no COMP insert yet, so DR and the lanes show none | `tcomp_gr_q4`, `mx_gr_q4` |

**The lane's preview** (section 0): op_input.c `lane_pick_from(l, key)` selects the lane and,
when it changed and the transport is stopped, asks the core to play it once (seq.c `audition_lane`, played by the
next audio block); `key` says the pick's own key did it (stopped it sounded as any key; playing it was kept silent),
so there is never a second hit. Every way a lane changes goes through it: the mixer's lane rows (ALGORITHM), the pick
(HOME held + a key; SEQ held + a key on STEP: op_step.c `step_played`), STEP's SELECT (below). **The pick's key while
playing**: `pick_silent` (the drum track, the transport running, SEQ held on STEP or HOME held elsewhere) sets the
keys' lock to LY_STEP, the core's existing "the keys reach the UI and play nothing" (seq.c), and op_drain drops
those keys so they set no step; stopped, the lock stays LY_PLAY and the key plays. **The core's change**: seq.c's
audition (`audition_req`, `audition_lane`, `audition_block`, SLOOP 2.4's, built only with FELUCCA_DRUM_STEP) is now
also built with `FELUCCA_UI == 1`: one guard widened on its two blocks, no code changed; UI=0 builds are unchanged.
Tests (tests/ui_optimist_lane.h): the preview once per lane change when stopped (requested, then played by the next
block), none while playing, none on a synth track; SEQ held + a drum key stopped (the key plays, no second preview)
and playing (silent, the lane picked, no step set); HOME held + a drum key on the mixer while playing (silent, the
cursor on the lane's row).

| Question | Chosen | Undo |
|---|---|---|
| The lane encoder on SOUND (the drum track) | none: ALGORITHM there is the track (as everywhere but the mixer) and SELECT walks the lane's pages (the paging rule); the lane changes by the pick (HOME + a key) or on the mixer / STEP | — |
| A lane re-selected (no change) | no preview | `lane_pick_from` |

**STEP's first page and the paging rules** (section 0, 2, 4.2): op_step.c `step_rows` puts
PATTERN (`stp_pattern`, the FAM_SEQ page whose first value is LEN) first on a synth track, and the drum track's
pages are PATTERN then the 16 lanes (`STP_LANE0`); op_input.c `op_enter` opens STEP on row 0; `op_row_pick` on a
lane page selects the lane (`lane_pick`, previewed when stopped), and `op_rows_fix` keeps a lane page on the lane
picked by SEQ + a key. SEQ tapped on STEP: its next page, round (`step_seq_tap`); the keys' toggle moved to SEQ held
alone past HOLD. SELECT in the FM6 layer (ENV held) now pages it (`fm6_lay_select`, stopping at its kind's ends).
Checked against the rules and unchanged: ENV LFO FX SCL ARP EDIT tapped again (op_input.c `op_jump_sound`, round),
SELECT on every list (`op_row_pick` clamps), the mixer (4.1). Tests (tests/ui_optimist_lane.h `step_pages_tests`, the
STEP tests updated, the FM6 test): STEP opens on PATTERN on both kinds of track, SEQ again pages round, ALGORITHM on
STEP switches tracks, SELECT from PATTERN to the lanes with lane_sel following and the preview only when stopped, a
step held keeps SELECT as NUDGE, SELECT in the FM6 layer stops at its first page.

| Question | Chosen | Undo |
|---|---|---|
| SEQ tapped on STEP, which toggled the keys steps / playing (section 0's ruling) | the newer paging rule wins: SEQ again pages; the toggle moved to **SEQ held alone past HOLD** (SEQ held + a key stays the pick) | `step_seq_tap`, `op_tap` |
| SELECT wraps or stops | stops, everywhere; the buttons wrap | `op_row_pick`, `mx_page`, `fm6_lay_select` |
| OCT- / OCT+ in the FM6 layer | unchanged (round, as built); SELECT is the stopping way | `fm6_lay_page` |

**REC held clears the track** (section 0): op_state.c `rh`, op_input.c `rec_held` (SLOOP's
ui_input.c holds_input, its timing copied: 0.7 s, then 1.3 s), op_draw.c `draw_ring` (SLOOP's hold ring's maths: 36
px, 7 thick, clockwise from the top), drawn as the modal is, over the panel (overlay 3). REC still acts on its press
(`op_rec`, now returning whether the press was a plain arm / disarm); only such a press becomes a hold: a free take
closed, SONG's REC (a scene stored, PATTERNS' duplicate) and *Stop the song first* have no ring. The clear is
`op_clear_track` (one undo level, "T2 cleared" as a toast, the latched arp chord too). SAVE + REC (SONG REC) and the
TEMPO page's REC row do not go through the REC press: unchanged. Tests (tests/ui_optimist_rechold.h): a tap arms and
disarms as ever, 0.5 s still the press, past 0.7 s the press undone and the ring up, the ring half full in the
track's colour, let go before the end nothing, held to the end the track cleared (said, not armed), undo brings it
back, the same while playing, HOME + REC's question still there.

| Question | Chosen | Undo |
|---|---|---|
| HOME + REC (the clear behind the modal) | kept as a second way: it does not conflict (HOME held takes REC's press, so no ring starts) | `home_combo` B_REC |
| The message | this UI's *T2 cleared* (toast), not SLOOP's *Track 2 cleared* | `op_clear_track` |

**Four rows, a full-width meter** (seen on the emulator, section 0): op_mixdraw.c rewritten: `MXL_N` 4 rows of
`OP_PH / 4` (41 px); `mx_view` moves the window only as far as the cursor needs (a row a step), MASTER in view only
while the cursor is on it; the code column (`LANE_CODE`, `mx_code`) and the meter (`MXL_VW` 214 x 16) and steps
(13 px a step, 14 tall, the pages as thin bars under them); op_state.c `cards_2x2()` is false on the mixer, so the
bands, the graph and the tiles of a layer over the mixer stay 1x4 there; op_draw.c `head_title` names the selected
row and its set. Tests updated (four 41 px rows, T1 T2 T3 DR at the start, the scroll a row at a time to the 16th
lane, the header *T1 more*, the meter and steps at their new places, 2x2 leaving the mixer 1x4).

| Question | Chosen | Undo |
|---|---|---|
| ALGORITHM past DR: a row or a page of four | a row at a time (the window moves only as far as the cursor needs): the row selected stays next to the one before it, a page jump would move every row at once | `mx_view` |
| CARDS 2x2 on the mixer | the mixer keeps 1x4 (four rows of 29 px would not hold the meter and the sequence readably) | `cards_2x2` |
| The row's name | a two-letter code at the left (the lanes as the kit's pads: BD SD CH ...); the full name in the header for the selected row | `mx_code`, `head_title` |

**Sizes** (user-default, exact, measured at 9ea0dba; B):

| Build | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| optimist 27dc239, UI=0 | 488,788 | 79,192 | 307,376 | 30,872 |
| this branch, UI=0 | **488,788** (unchanged) | 79,192 | 307,376 | 30,872 |
| this branch, UI=1 | **478,260** (103,304 free) | 86,552 | 307,376 | 31,156 |
| the playable package: UI=1 + PLOCK MICRO FILLS CHANCE SL24_XSTEP | 488,936 | 87,960 | 311,632 | 31,100 |

The package is build/optimist-0.1-dev-9ea0dba.fwsc; it fits, nothing was left out. Screenshots (build/ui-optimist-shots,
the emulator, a fresh state, playing, the master compressor at -30 dB, 20:1): `p6-mixer-h-master.png` (MASTER with
its reduction bar), `p6-mixer-h-t2.png`, `p6-mixer-h-set2.png` (the second knob set), `p6-mixer-h-lane.png` and
`p6-mixer-lane.png` (the snare lane selected), `p6-mixer-h-2x2.png` (CARDS 2x2: the mixer keeps 1x4),
`p6-cards-2x2-sound-env.png`, `p6-cards-2x2-sound-plain.png`, `p6-cards-2x2-step.png`, `p6-cards-2x2-tempo.png`,
`p6-sound-lfo-dest.png` (LFO DEST with the LFO's wave), `p6-name.png`, `p6-scope.png`, `p6-step-len32.png`,
`p6-step-first-page.png` (the drum track on PATTERN), `p6-step-lane.png`, `p6-rec-hold.png` (the ring half full).
