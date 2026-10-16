# Optimist UI: design (the second UI on the pluggable seam)

> **Landed state, 2026-10-15 (what is built on `optimist` at 5756d5ac; where the sections below say otherwise, this list and the banners above win).**
> - **Gestures:** SHIFT = OCT- held, SHIFT2 = OCT+ held (the banner above). HOME is only HOME / NO. OCT- / OCT+ glow only where they do something, like any button; ARP and SCL are dark on the drum track and its lanes, and SAVE is dark on STEP lines with no YES action. SAVE works on GLOBAL, SYSTEM and PROJECT's read-outs (YES, else PROJECT).

> **2026-10-16, solo on every row but the returns (the user).** One solo set: any soloed track (GLO + keys 5-8, or the row), drum lane or USB IN
> silences every item of those three groups that is not soloed; a soloed lane keeps the drum track heard and only that lane (and the click)
> sounds in it, the other tracks and USB IN are silent; the returns A / B / C and MASTER are never silenced, the ACCENT lane has no solo. A mute
> wins over a solo. **Gesture: SHIFT + SHIFT2 (OCT- and OCT+ both held) + the volume card's knob** on a track, DR, a lane or USB IN toggles that
> row's solo, once per hold, the value untouched (SHIFT + the knob alone is the reset, so SHIFT2 + SHIFT is the free neighbour of the mute
> gesture; both OCT held and a knob turned is no master toggle). A soloed row: the S badge (a lane: its code in green), a toast "T2 SOLO". The
> lanes' solo is saved with the project (the FX record's lane TLV), the tracks' and USB IN's are not (a load starts without them). Editor
> protocol v14 SOLO ops 5 and 6 carry USB IN.
> - **No preset rows:** no SOUND and no FAVS row on EDIT (the SOUND row, the mixer's PRESET and FX ON cards, PAN / MUTE on EDIT, the drum LANE card and the ARP / LANE rows on STEP are gone). Presets are picked with PRESETS; SHIFT + PRESETS steps the engine. Favorites: SHIFT2 + PRESETS marks, SHIFT2 + SELECT browses only them (section 37 words the old FAVS row). INIT is PROJECT > USER's 4th card, SAVE AS is USER's SAVE.
> - **PROJECT:** the storage rows are BANK, ACTION, ITEM (+ NEW on PROJECT); the actions are LOAD / SAVE / DELETE (a project slot is cleared through the section log's tombstones, asked first); a key selects the slot of the bank and runs the action; OCT- / OCT+ tapped on the page pick the previous or next bank (toast and LEDs); a user preset's LOAD over an edited sound is asked in red.
> - **Mixer:** the sends set is REV DLY CHO (blank where no slot holds one) and a fourth card PAN on the synth tracks, the drum track and the drum lanes; a muted track is struck through (the banner above); the ACCENT row's MUTE is the lane's sequencer mute.
> - **Effects pages:** the master FX screen reads INSERTS (S1 / S2 TYPE and amount), INS DYN 1 (THRS RATIO ATK REL) and INS DYN 2 (GAIN CEIL GR) while a master slot holds DYN, DUCK, then the returns (A B C); the mixer's MASTER row has INSERT, INS DYN (THRS RATIO GAIN CEIL), RETURNS, A SENDS, B SENDS (the sends between the returns, left to right), DUCK; a track's FX family has DYN (THRS RATIO GAIN CEIL) and DYN 2 (ATK REL). TUNE is on GLOBAL's Tempo list. Every drum sound has two insert slots.
> - **Modulation:** ENV 1 MOD (*ENV mod*) has four depth knobs made per engine, the amplitude always ENV 1's (2026-10-16); ENV2 DEST and LFO DEST each have DST A / AMT A / DST B / AMT B from one shared list (LFO and ENV rates and amounts included); VOICE 2 is ALLOC, DETUNE, VOICES, DROP (VOICES: 1 to the engine's maximum, never AUTO). The p-locks reach the ARP and SCL pages; EUCLID is a note generator with ON / OFF; a drum step tapped cycles normal, hard, ghost, off.
> - **GLOBAL / SYSTEM:** the title is in the top bar (long ones in their short form), the cursor line is drawn inverted, ALGORITHM scrolls the lines; GLOBAL's MIDI page reads CLK IN, CLK OUT, MIDI IN, MIDI OUT (SYNC is PLAY's TEMPO page 4th card; fixed SYNC reads Int / USB / TRS); the MIDI IN setting is removed from the Optimist UI (it ignores settings bit 15; the router never drops notes). SYSTEM > INFO shows the battery as n/3 raw (no volts), each core's load, CPU1 JOB / REWAKE and CPU1 STOP (when and where it stopped), and, in a USB audio build, the USB AUDIO lines (underruns, overruns, missed, feedback, ring low, poll gaps, flash IRQ-off), which have no bar.
> - **Boot logo:** the dinghy splash is always built (the SPLASH builder item is retired; sections below that mention a ring logo or a logo choice are history).

> **2026-10-15: a muted track is struck through** (a 1 px line, 2 in the large face, in the text's own colour through the glyphs' middle): its mixer row's name, the header badge ("2 DIGITAL") on its pages, and its volume card's label and value (a lane's LEVEL, the ACCENT lane's amount). Unmuted: no line.
>
> **2026-10-14, SHIFT moves to OCT- (the user: "shift on home is not working well, two buttons on the same hand").** **SHIFT = OCT- held, SHIFT2 = OCT+ held.**
> Every gesture this document words as "HOME held" / "HOME + x" is now **SHIFT** / **SHIFT + x** (op_input.c `op_shift(held)`, op_state.c): the knob to its default,
> the drum lane pick, a button's clear, the layer lock, PRESETS the engine, the NAME clear. **HOME is only HOME / NO** (SAVE then HOME is still undo).
> OCT- / OCT+ **change the octave when let go, and only if no other control was used while they were held** (with the toast "Octave: -1"); both together (changed 2026-10-15, e59c75e3): the track <-> MASTER toggle, back to the track and page it left (STEP's FOLLOW comes back with SHIFT + PRESETS). The STEP window scrolls with PRESETS only (HOME + OCT is gone). SHIFT2 on the mixer's volume card (SHIFT2 + its knob) toggles the track's
> mute (the MUTE card is gone). The mixer's track rows show the track's badge (not "MIX"). SAVE tapped opens PROJECT on every page where YES has nothing to do (not SEQ). The keys are silent on the mixer's MASTER row.
> On the drum track's STEP, OCT- is the lane pick's SHIFT, so it no longer makes a ghost hit there (OCT+ still a hard hit).
> The layer lock by "the layer held, then SHIFT" works except for FX, EDIT and ENV (FM6), whose OCT- / OCT+ keep their own jobs: use SHIFT held, then the button.

Status: **design; phase 1 (the skeleton), 1b (the review's rulings), 2 (STEP), 4 (the layers, TEMPO, SONG), the
loose ends (NAME, FM6, 11.4), 5b (the drum mixer, SCOPE, no footer, 11.5), the follow-up (nothing wraps, 11.7) and 3 (the automation store, 11.8)
built** (2026-10-08, section 11).
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
| The three left encoders | **SELECT = the cursor, ALGORITHM = the track (past T1, MASTER), PRESETS = the selected track's sound only** (the drum track: its kit), on every screen; superseded 2026-10-10, see section 12: PRESETS was the hot cell's value. **SHIFT (the shift) + PRESETS = the instrument** (2026-10-12, the user: "home held (shift) + preset should navigate instruments, not presets"): the first sound of the next / previous engine, round; the factory list is sorted by instrument (engine order, then as BANK has them; the Optimist UI only, the SLOOP UI keeps SLOOP's order); the drum track: its kits as PRESETS. On the mixer's MASTER row and the master FX PRESETS does nothing (master has no instrument) |
| The step sequencer | **the 16 keys are the 16 steps**, on drums and synths alike; a pattern longer than 16 steps: **PRESETS scrolls the window of 16** (SHIFT + OCT is gone since 2026-10-14); OCT alone keeps its job |
| Picking the drum sub-track (the lane) | **the current mode's button, held, + a key**: on every screen where the keys play except the mixer (and not on GLOBAL, SYSTEM, PROJECT, MASTER FX, SONG, SCOPE: no track, section 24b), **SHIFT + a key**, on STEP too (the key plays the sound; on the mixer the same chord mutes the sequencer, section 21). One lane for the whole device. On STEP **SEQ held + a key clears the step** instead (user, 2026-10-14: the pick is SHIFT + a key on every drum page) |
| HOME in combination | **SHIFT + anything = clear it**: a step + HOME clears the step, a knob + HOME clears the cell's event, REC + HOME clears the track, a scene or pattern key + HOME clears it. HOME tapped alone is NO: it answers a dialog and goes back; while a dialog asks, the keys do nothing |
| Locks and motion | **one store, two ways to edit it**: a step held + a knob writes a lock, REC + a knob while playing records motion, both into one list per pattern (section 6) |
| SAVE and HOME with a button | YES / NO with a button was first read as a shift, but **SAVE cannot be a shift**: it is used to save something (preset, pattern, project). So: **SAVE held + a page button saves what that button owns**, nothing else; **SHIFT + a page button clears or switches off** what it owns; section 2.1 proposes the table [P] |
| SONG REC | there is a REC button: recording the song is **SAVE + REC** (section 2.1), not SAVE + PLAY |
| Undo / redo | **SAVE then HOME = undo, SHIFT then SAVE = redo**; the order of the two tells them apart [P] |
| Lock a layer | **SHIFT + the layer's button, in either order** (the layer held then SHIFT, or SHIFT then the layer's button): the layer stays open hands-free, any button lets it go. So HOME with a layer button never means "off" |
| Scenes on the keys | **16 scenes on the 16 keys, no bank** |
| The pattern layer's keys | [O] hard to say before trying it on hardware: the built black-key modifiers against the uniform launch / store / clear vocabulary, decided on the FM-1 (it arrives 2026-10-08) |
| EDIT + OCT as a second undo / redo | **keep both**: SAVE then HOME / SHIFT then SAVE, and SLOOP's EDIT held + OCT- / OCT+ (edit left / right); a point to clarify on it is open (question 12) |
| Names | **Felucca's NAME screen**: projects and user presets get names typed with the keys; kits keep numbers |
| The pick on SOUND | **SHIFT + a key only**; the sound buttons' hold is not a pick |
| Per-step chance | **just p-lock / motion data**: chance is an event of the automation store (section 6), on drums and synths alike; no step bit is needed |
| FOLLOW on STEP | **follows the playhead by default**; PRESETS scroll left / right and stop the follow; **both OCT together resume it** |
| Every per-step extra as an event | **yes: one list of 128 events a pattern** holds nudge, fill, chance, locks and motion (to be revisited); SLOOP 2.4's stepx record becomes an import / export form |
| EDIT held's knobs | **kept**: SHIFT · LENGTH · TRANSPOSE on EDIT held, erase on the keys |
| Fills and automation | **hold events apply through a fill, step-only events are skipped with their step** |
| Typed notes on STEP | **no**: the keys and the pick are enough |
| SEQ tapped on STEP | **toggles the keys between steps and playing** |
| Nudge | **a few percent slower / faster while OCT- / OCT+ is held, back on release**; the BPM value never changes |
| The mixer's drum column | **the whole drum track, the sum of the 16 sounds**; a way to switch to a drum mixer showing its 16 channels (in blocks of 4) was wanted: section 4.1's DRUM MIXER (superseded after the merge: the horizontal mixer, its lanes as rows) |
| The DRUM MIXER | entered by **the pick: SHIFT + a drum key on the mixer** opens it on that sound's block; its rows are **like other tracks**: the mixer's rows, "-" where a sound has no such value; a compact mode with 8 or 16 on a screen is to be evaluated (switching would be an option in the main settings) [O, question 15] (superseded: the horizontal mixer has no drum mixer) |
| Macros (COLOR MOTN SPACE ENRGY) | parked (open question 16) |
| Visualiser | **not SLOOP 2.4's styles** (they add little): instead **an oscilloscope view for the master and per track, and levels on the mixer with the compressor's effect shown, a bar pushing down** (sections 4.1 and 4.10) |
| Play the song | **SAVE + PLAY plays the song from the start; PLAY alone stops** |
| Store the loop into scene n | SAVE held + REC held + key n, three fingers: not decidable without trying, to confirm on the hardware (open question 18) |
| Tempo (SELECT is no longer the tempo) | **a tempo page held open by PLAY**, with nudge on OCT- / OCT+, fine tuning, MIDI sync (section 4.8) |
| Next | this document, then review |
| Track colours (review of phase 1, 2026-10-08) | **Tracks are coloured after their instrument**: a track is always drawn in its engine's colour (the drum track: its kit's kind), tools/colors.json, wherever this UI shows it: the mixer's strip, the header's badge, the cursor bar on SOUND, a track named in a question. There is no colour per track number or per knob |
| Questions (review of phase 1) | **questions must be more visible: in the middle, like a modal popup**: a confirm is a box over the panel (the cards stay above), the verb big, its target named big (a track in its colour), "HOME no" on the left and "SAVE yes" on the right at its bottom, as the buttons sit (decided after the 1b screenshots; the footer does not repeat them); a red frame when it destroys or replaces the work (clear, erase, an overwrite, a load), amber otherwise. The result of an action just confirmed is a small toast in the middle; passive status (MISSING, RECORDING, REC OFF) stays in the header |
| Values (review of phase 1) | **almost no place should show a number value with no graphic representation**: every value is drawn with a form beside its number (section 3); names stay text |
| A page button on SOUND (phase 2) | **only that family's rows** (the LFO screen should not show ENV2 and SLICER rows): LFO shows the LFO rows and their graph, ENV the envelopes...; the button again its next row, past the last the first again (a button wraps, section 2); HOME > Sound keeps every row (section 4.3) |
| The mixer's cards (phase 2) | **none**: the mixer has no top four cards; instead the fader, the pan, etc. are highlighted, and SELECT scrolls from volume to pan, to send, etc.; the strips take the height, the control SELECT is on lit on all four, the master values as the walk's last row, the selected track's strip framed in its colour (section 4.1) |
| A shortcut to SONG (review of phase 4) | **SAVE held + SELECT turned** (11.3's proposal 1c): the SONG screen; built in section 11.4 |
| Graphs on a family's rows (emulator review of phase 4) | **LFO and ENV always display their shape**: every row of the ENV, ENV2 and LFO families shows its family's graph, the DEST rows too (section 11.4) |
| Which engine a preset is (emulator review of phase 4) | **a preset must show which engine it belongs to**: a preset is always shown with its engine, in the engine's colour (section 11.4) |
| The footer (emulator review of phase 4) | **no footer anywhere**: its two lines were noise on a small screen; the useful hints move into the header, the cards or the modal (the new screens of 11.4 draw none; the removal from the other screens is feat/ui-drummix's, section 11.5) |
| The look (review of phase 4) | **the current look is kept**: phase 5 (Felucca's font and faces) is dropped for now |
| The header's badge (phase 5b) | **the selected track's number and engine name, in its colour** (*2 DIGITAL*, *1 ANALOG*, *4 DRUMS*; a picked lane *4 SNARE*, the accent lane *4 ACCENT*; the number only, no T or DR, the drum track is 4: the user, 2026-10-14 later) (the user, 2026-10-14: "we should always have the track number on the top left with the engine/preset"; 2026-10-08 had dropped the tag), and **the preset follows the page's name in the track's colour where it fits whole**: the page's name and its n/N take their room first (head_fit, the n/N always whole), the preset is dropped before the name shortens, so on the narrow pages (SOUND's n/N pages) it is mostly absent and shows on a short title (a layer, "Scale"); a picked lane has none (op_draw.c head_badge, head_preset); **MASTER has no instrument**: on the master FX, a layer on MASTER and the mixer's MASTER row the badge is *MASTER* in C_HI, and on the MASTER row the track buttons ENV LFO EDIT ARP SCL SEQ (their layers and SHIFT + them too) are dark and do nothing, FX opens the master FX (FX held: the master FX cards) (the user, 2026-10-12). **The track's badge shows only on pages whose values belong to the selected track or lane** (the user, 2026-10-13): SOUND and its families, STEPS and a held step's cards, the ERASE, KEY and FM6 OPS layers. **Every other page has a badge too, the screen's name in C_HI** (batch D, section 33): *MIX* (the mixer), *MASTER* (the master FX, the mixer's MASTER row, MASTER in a layer), *PROJECT*, *SONG*, *TEMPO*, *SCOPE*, *GLOBAL*, *SYSTEM*, *NAME*, a layer's name (*FX*, *REPEAT*, *MIX*, *SCENES*, *PATTERNS*); the title is then the page's name alone (op_draw.c HEAD_TRK_SCR / HEAD_TRK_LY say which pages are the track's, head_other names the rest). The track-name flash on a track change stays on every screen but MASTER's |
| The footer (phase 5b) | **no footer anywhere**: every screen draws to the screen's foot (the mixers' strips, the lists, STEP, the layers, SCOPE); what the footer said moved to the header (STEP's window and pick; a layer's *Home locks it*, once) or under STEP's grid (a held step's nudge, chance, fill); section 11.5 lists what went |
| Pattern length (after the merge of 11.4 and 11.5) | **LEN moves in powers of two by default, 1 2 4 8 16 32 64**: a knob detent goes to the next / previous value of that list (from a value off the list, to the next list value in the turn's direction); **with SHIFT held, by one**. **SHIFT is the LFO button held** (decided): a general flag the cells can read, used by LEN only for now; PRESETS stays the one-unit fine encoder on any hot cell, LEN included (section 11.6) |
| The cards (after the merge; settled 2026-10-12) | **always 2x2 on every screen with cards except the mixer** (2026-10-12, the user: "Cards are ALWAYS 2x2 on every screen with cards except the mixer"): SLOOP 2.4's big values, the four values in large type as a 2 x 2 block laid out like the knobs (KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom right), the hot one white, each with its form; on SOUND, STEP (an overlay), SONG, TEMPO, FX, PROJECT, SYSTEM, GLOBAL, the layers, SCOPE. **The mixer keeps its compact 1x4 cards.** **There is no CARDS setting**: SYSTEM > SCREEN > CARDS and the 1x4 path of those screens are gone (the user did not know the setting existed, and an FM-1 that came up 1x4 kept showing one line of small cards and a list); the settings word's bit 23 is no longer read (reserved). **Under the cards: the screen's picture when it has one, else its row list** (2026-10-12, the user: "Keep the graph"; "we can keep the list below when there is nothing to display besides the 4 cards"; "arp is the same as lfo/env. no list"): ENV, LFO, the FX needles, ARP (every ARP row), SCL, the drum lane, the SOUND row's presets, FM6's algorithm, TEMPO and SCOPE show their picture alone on the whole panel; EDIT, VOICE, PROJECT, SYSTEM, SONG, GLOBAL, the master FX and any other screen with no picture show their rows under the big values (this reverses e0b476e's "empty below"); SONG's PATTERNS row keeps its rows under the session grid. The header names the page and its n/N on every one. History: 1x4 was the default with 2x2 an option (2026-10-08), then 2x2 the default (2026-10-12 morning), then no list at all in 2x2 (e0b476e), all superseded (sections 3, 11.6) |
| GLO tapped (2026-10-12) | **GLO tapped opens GLOBAL**, as SLOOP's GLO (the user: "what is supposed to be the GLO page ? i thought it would be the page where we have bpm, nudge, midi track settings etc"): the global, non-track settings a user changes while playing, as rows of 2x2 cards with the row list under them (no picture): TEMPO (BPM · NUDGE · SWING · SYNC), REC (MODE · LENGTH · START · CLICK), REC 2 (QUANT), MIDI (OUT · IN · SYNC · CLK), CLOCK OUT, CHANNELS (T1 · T2 · T3 · DR); GLO tapped again pages on through SYSTEM's rows and round (section 17, D1). Each row is TEMPO's or SYSTEM's own (one place each value lives); MIDI, CLOCK OUT and CHANNELS leave SYSTEM, which keeps the setup (SCREEN, LIGHTS, AUDIO, USB, BLUETOOTH, CPU, CALIBRATE, ABOUT). (GLO tapped again: superseded by section 17, then by section 20: GLOBAL and SYSTEM are two settings lists, GLO switches between them, no cards.) **GLO held stays the performance layer**, unchanged. The mixer's knob sets, which GLO tapped paged, are SELECT's alone (it already paged them). The mixer stays the HOME screen; HOME tapped goes back to it. TEMPO (PLAY held) stays: not a pure duplicate (held over any screen, tap tempo, its picture). The GLO LED: full on GLOBAL, half lit elsewhere (section 4.11) |
| A click against a hold (found on the FM-1, after the merge) | **a click on a button showed the long-press screen about one time out of two**: SLOOP's 140 ms is shorter than a click on the FM-1's buttons. **A hold is 350 ms by default, the shared HOLD setting (main's, settings word bits 21..22; CARDS is bit 23) 250 / 350 / 500 ms**, one threshold for every tap-or-hold decision of this UI: a layer's map, PLAY held for TEMPO; a page button let go within HOLD + 150 ms still counts as a tap (section 11.6) |
| The mixer, horizontal (after the merge) | **the horizontal mixer is better**: **the rows are the tracks**, MASTER (above T1, out of view until the cursor goes up: it starts on track one, but the cursor can go up to master), T1 T2 T3, DR, then **the drum track's 16 lanes as rows** (indented, named, in their source's colour); **the four knobs are four values of the selected row**, on its 1x4 cards; **ALGORITHM walks the rows** (it is the lane encoder there), **SELECT pages the knob sets** (GLO tapped did too until 2026-10-12: it opens GLOBAL now) (VOLUME INSERT SEND PAN, then the rest; a lane LEVEL DRIVE REV CUT, then DLY CHO); each row **a VU meter, the compressor's reduction pushing in from the right, and its sequence under it, like SLOOP's TRACKS**; **the DRUM MIXER screen and its DR MIX setting are gone** (its lanes are the rows); the GR column went into the MASTER row; the four levels on four knobs stay on GLO held (section 4.1) |
| A drum lane picked (found on the FM-1) | **its sound previews once when the transport is stopped, and stays silent while playing**, however the lane changes (the pick SHIFT + a key, on STEP too since 2026-10-14, ALGORITHM over the mixer's lane rows, SELECT over STEP's lanes); **the pick's key itself is silent while playing** (it must not sound over the running pattern) and plays when stopped (that is the preview). Synth tracks: unchanged (section 11.6) |
| STEP's first page (on the FM-1) | **the first SEQ page should have the length of the pattern**: STEP opens on **PATTERN: LEN · DIV · SWING · GATE**, on synth and drum tracks, the first time (it keeps the page it was left on since 2026-10-13, section 24); then a synth track's ARP rows, the drum track's 16 lanes (each its sound: LEVEL · TUNE · DECAY · REV); SELECT walks PATTERN, lane 1 .. lane 16 and selects the lane it lands on (the lane encoder on STEP, ALGORITHM keeps switching tracks: the track should not switch while editing a sequence) (section 4.2) |
| Paging (after the merge) | **a button again wraps, SELECT stops** (2026-10-10; the follow-up of 2026-10-08 had stopped both) | **a page button tapped again goes to its next page and, past the last, to the first** (the user, 2026-10-10: "when paging thru pages by pressing again the same button, we should cycle/wrap at the end"); **SELECT moves through the same pages, back and forth, stopping at the ends**, on every screen: SOUND's families, STEP, the mixer's knob sets, the FM6 layer (section 2: the rules and their exceptions) |
| The other screens leave the mixer (follow-up, 2026-10-08) | **the mixer's knob sets are only values** (no screens set). The screens have their own gestures: **the global FX screen: FX, then ALGORITHM left past T1** (decided; the MASTER row's YES with a cell picked still opens it), **SONG: SAVE held + SELECT** (decided), and, **PROVISIONAL (the user: "I don't know")**: **PROJECT: SAVE tapped on the mixer** (no cell picked, nothing asked), **SYSTEM: SHIFT alone** on the mixer (released with nothing else touched, past HOLD). SCOPE keeps HOME tapped on the mixer (section 11.7). Superseded 2026-10-12 (section 17): SAVE on the mixer is always PROJECT, SYSTEM is the second half of GLO's cycle, the master FX is FX on MASTER or ALGORITHM left of T1 |
| Kept as built (follow-up, 2026-10-08) | **SEQ tapped on STEP pages, SEQ held (past HOLD) toggles the keys between steps and playing; LFO held is SHIFT** (the patterns layer keeps launching from its keys only, no knobs). Decided, nothing to change |
| Clear a track by holding REC (after the merge) | **use SLOOP's long press on REC to delete a track**: REC held 0.7 s undoes its press and a ring fills in the track's colour; held 1.3 s more, the selected track is cleared (undoable); let go before, nothing. No question: the ring is the confirmation. Every screen, playing too; REC tapped keeps its jobs; SHIFT + REC (the modal) stays as a second way (section 11.6) |
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
| **ALGORITHM** | the walk, the same on every page and layer (2026-10-12, section 16): MASTER, T1 T2 T3, DR, the 16 drum lanes, stopping at both ends. Every screen retargets to it, the colour follows. It is the track and never anything else, and **only where there is a track** (section 28: on PROJECT, SONG, TEMPO, NAME and the global layers it does nothing); **the one exception: on GLOBAL's and SYSTEM's lists it scrolls the cursor one line a detent, across the pages** (section 31, 2026-10-14, the user: *"ALGORITHM should scroll one item at a time"*) |
| **PRESETS** | **browses the selected track's presets** (the drum track: its kits; a drum lane on an instrument: that lane's sound, section 16), one a detent, on every screen (2026-10-10, section 12) but STEP, where it pages the window (2026-10-11, section 4.2), and GLOBAL / SYSTEM, where it does nothing (2026-10-13, section 27: a knob is line k, YES runs an action). SHIFT + PRESETS: the next / previous engine's first sound (on STEP and GLOBAL / SYSTEM: nothing). It is no longer the hot cell's value, a layer's hot knob or a held step's chance; under a question, the REC ring, the NAME screen or on MASTER it does nothing |
| **KNOB 1..4** | the four cells of the cursor row; a turn makes its cell hot. **LEN** (the PATTERN row: SOUND, STEP) moves through 1 2 4 8 16 32 64, a list value a detent [D] |
| **SHIFT = LFO held** + a knob | the cell's fine path [D]: **LEN by one**; every other cell turns as without it (LEN only, for now). LFO held + a knob is SHIFT, so the patterns layer (LFO held) keeps its keys and has no knobs (section 11.6); LFO tapped alone still opens the LFO rows |
| **YES** (SAVE tapped) | act on the cursor row: enter a ▸ row, toggle an on / off cell, **do** an action cell, confirm an armed action |
| **NO** (HOME tapped) | back one level; at the root nothing. Cancel an armed action |
| **SHIFT + anything** | **clear it** [D]: + a step key on a synth track's STEP, the step (its notes, events and extras; the drum track's STEP: SEQ + a step key, and SHIFT + a key is the lane pick, 2026-10-14; a step held, HOME tapped, clears it on both); + a knob, the hot cell's event (a step held) or the cell back to its default; + a scene or pattern key in its layer, that scene or pattern (SHIFT + REC clears a SONG scene row's scene and nothing else since 2026-10-13: a track is cleared by the REC ring alone, section 27). SHIFT alone does nothing, on the mixer too (2026-10-12, section 17: SYSTEM is in GLO's cycle) |
| **a page button, tapped** | jump the cursor to that family's rows (ENV: the envelope rows of SOUND); tapped again: its next row, past the last the first |
| **SHIFT + a key** where the keys play (the drum track's STEP too), **SEQ held + a key** on a synth track's STEP | **pick** [D]: on the drum track the lane (the key plays the sound, so you hear what you picked; the last upper key: the ACCENT lane, section 29); on a synth track, on STEP, the note or chord the next tapped steps carry (SEQ + keys). The selected lane is one value for the whole device, like the selected track: the mixer's drum column, SOUND and STEP all show it. On a drum track's STEP the keys are steps and **SEQ held + a key clears the step** (everything of it; the swap of 2026-10-14: HOME picks as on every drum page, SEQ clears; SEQ + the ACCENT key does nothing, it is no step); a synth track's STEP: SHIFT + a step clears, SEQ picks. **On the mixer SHIFT + a key is the sequencer MUTE, not the pick** (2026-10-13, section 21)**On GLOBAL, SYSTEM, PROJECT, MASTER FX, SONG and SCOPE the keys do nothing at all, the pick included** (2026-10-13, section 24b) |
| **GLO tapped** | **GLOBAL** [D, 2026-10-12]: the tempo, REC and MIDI settings, and the way into SYSTEM (section 4.11); tapped again: SYSTEM, and again GLOBAL (the two lists, section 20) |
| **a page button, held** | the performance layers, as today (section 4.9) |
| **OCT- / OCT+** | synth tracks the octave, the drum track ghost / hard (as today), on STEP too; **PRESETS scroll the 16-step window** over a longer pattern [D]; **on STEP's roll the same value: the keyboard octave and the roll's view are one (2026-10-13, section 4.2)** |
| **SAVE then HOME · SHIFT then SAVE** | **undo · redo** [D]: the order of the pair tells them apart; **EDIT held + OCT- / OCT+** do the same, as SLOOP (open question 12 holds a point to clarify) |
| **PLAY, REC** | transport, as today, in every layer |

**Paging** [D, after the merge: two rules for every screen]. A screen's pages are its rows (a list), its
families' rows (SOUND), STEP's pages (PATTERN, then ARP or the 16 lanes), the mixer's knob sets, the FM6 layer's
pages. (1) **A button tapped again goes to its next page and, past the last, back to the first** (it wraps: the user,
2026-10-10, "when paging thru pages by pressing again the same button, we should cycle/wrap at the end"; the follow-up of
2026-10-08 had made it stop at the last, section 11.7): ENV LFO FX SCL ARP EDIT their families on SOUND, FX the global FX screen's pages, SEQ STEP's pages, a
page button with a step held its locks' pages, OCT- /
OCT+ in the FM6 layer (GLO is the exception: it switches GLOBAL and SYSTEM, section 4.11). (2) **SELECT moves through the same
pages, forwards and backwards, and stops at the ends** (a knob that wraps loses the user's place); SELECT is the way
back. What SELECT does otherwise, and stays so:

| Where | SELECT | Why |
|---|---|---|
| STEP, a step held | the held step's card pages: the step, its extras (NUDGE CHANCE FILL COND), then the track's sound pages as p-locks | the one exception named |
| SAVE held | the SONG screen (11.4's shortcut) | an earlier decision |
| the mixer | its knob sets (ALGORITHM walks its rows) | the rows are the tracks (4.1) |
| a performance layer held (FX ARP SCL GLO EDIT LFO) | nothing | a layer has one page (its map) |
| SCOPE | its pages: one (the source is ALGORITHM's, section 27) | one page |
| GLOBAL, SYSTEM (GLO tapped) | one sequence: GLOBAL's rows then SYSTEM's, as GLO tapped again walks them (section 17) | the paging rule |
| NAME | nothing | one page (the field and the keys) |
| PLAY held (TEMPO) | its rows (TEMPO, REC) | PLAY cannot be tapped again while it holds the page open (a tap is start / stop), so its rows are SELECT's only |

Three consequences:

- **One confirm idiom.** A destructive action (store over a used slot, clear, NEW, LOAD) arms and a **modal box over
  the panel** asks what YES will do [D, section 0]: *CLEAR T2?* in the large font, the track in its engine's colour,
  *HOME no* / *SAVE yes* at its bottom (as the buttons sit), the frame red when it destroys or replaces the work, amber otherwise. NO or
  3 s cancel. The four idioms of today go; the live clear of a track is the REC ring (SHIFT + REC cleared it behind this modal until 2026-10-13, section 27).
- **Messages**: what a confirmed action says (*T2 CLEARED*, *SAVED*, *STORED T2 5*) is a **small toast in the middle**
  of the panel [D]; passive status (*T2: NO FREE PATTERN*, MISSING, RECORDING) stays in the header (2.5 s).
- **Lock a layer** is no longer needed for editing: editing never needs a held button. For the performance layers
  (FX with both hands) the gesture is **SHIFT + the layer's button, in either order** [D]: the layer held then SHIFT
  (today's), or SHIFT then the layer's button. The layer stays open, any button lets it go. HOME with a
  layer button therefore never means "off" (section 2.1).

### 2.1 The two held buttons: SAVE saves, HOME clears [P]

Decided (2026-10-08): SAVE cannot be a shift, it is used to
save something (preset, pattern, project). So **SAVE held + a page button saves what that button owns**, nothing
else; **SHIFT + a page button clears** what it owns, or, when the button's hold is a layer, **locks the layer
open** [D]. There is no positive shift: latching lives on its row (ARP's HOLD). Keys are not shifted
this way: SAVE held + keys stays the scene layer, SHIFT + keys the pick or the step clear (section 2). The
order of the two held buttons matters only for SAVE and HOME together: SAVE then HOME is undo, SHIFT then SAVE is
redo.

| Button | SAVE + it: **save** | SHIFT + it: **clear / off** |
|---|---|---|
| **SEQ** | **STORE** the selected track's working pattern into its slot (the explicit store of PATTERNS-DESIGN §2.4; *AGAIN* becomes *YES?* over a used slot) | clear the pattern's automation and extras, the notes stay |
| **REC** | tapped: **SONG REC** on / off (there is a REC button): the song is saved as you play it. Held + a key n: **save the loop into scene n**, every track's pattern and the scene (today's "save the loop into the section"; the playing scene's own key updates it) | a SONG scene row: **clear the scene** (asked); elsewhere nothing (the track is cleared by the REC ring, 2026-10-13) |
| **PLAY** | **play the song from the start** (song mode on; PLAY alone stops) [D] | stop, and every voice off at once: the panic (CC120's effect) |
| **ENV · LFO · EDIT · FX · SCL · ARP** (a sound page button) | **save the sound as a user preset** (the slot, the NAME screen); on the drum track, **save the 16 lanes as a user kit** | **lock that layer open** [D]: FX, ARP, SCL, EDIT (erase), LFO (patterns), ENV on an FM6 track; ENV elsewhere: INIT, the preset back over the edits (YES confirms) [P]; LFO: locks only if no slot was cleared with HOME (SHIFT + n clears a pattern slot, section 27) |
| **GLO** | — | **lock the mix layer open** [D] |
| **HOME · SAVE** | SAVE then HOME: **undo** | SHIFT then SAVE: **redo** |
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
 y  76..239  the panel, to the screen's foot: the list of rows (the cursor row the line's colour, text white), or a grid (STEP), or a graph
             (SOUND), or the scope; the mixers' strips take 27..239 (no cards)
```

**The cards are 2x2** [D, 2026-10-12: always, on every screen with cards but the mixer; no setting]: the cursor row's four values are drawn large, 2 x 2 as the knobs sit (KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom right; label and unit small above the value, its form under it, the hot one white, a name too wide for the large face in the small one), in the band y 28..120; the panel then runs y 123..239 (117 rows). The layout drawn above (four small cards in a line, y 28..72, the panel from y 76) is **the mixer's only** (its rows want the height: 2x2 would leave 29 px a row). **Under the big values, the picture when the screen has one, otherwise the row list** (2026-10-12, the user: "Keep the graph", "we can keep the list below when there is nothing to display besides the 4 cards", "arp is the same as lfo/env. no list"): SOUND's graphs (the envelope, the LFO's wave, the FX slots' needles, the scale, the arpeggio on every ARP row, the pattern's steps, the drum lane, the SOUND row's presets, FM6's algorithm), TEMPO's tempo and beat and SCOPE's trace (no source tabs) take the whole panel, 117 rows, with no rows under them; a screen or row with no picture (EDIT, VOICE, PROJECT, SYSTEM, SONG's scenes, MODE and parts, GLOBAL, the master FX ...) shows its rows under the big values, five of 20 px (taller when there are fewer, at most 40), the cursor row a dark tint of the track's colour with white text (cursor row look, 2026-10-14, revised the same day: the line's colour at FULL strength, cur_bg; the colour the context uses: the track's on a track list, the knob's on a GLOBAL / SYSTEM line with KNOB COLORS, a line's own elsewhere; the row's text, value and unit white, its bars and marks white over a trough of 3/8 of the colour. The dark tint (3/8 over black) was tried and could not be recognised. Every colour that can be a cursor background, the engine and kit-kind colours of tools/colors.json and KNOB_COL, holds white text at a WCAG contrast of at least 3:1; the ones that failed were darkened a little, hue kept, at the source: tools/colors.json (the web editor's COLORS follow), knobcol.h). SONG's PATTERNS row shows the session grid with its rows under it. The header names the row and its n/N on every screen (*Sound ENV 2/21*, *Tempo REC 2/3*, *Scope T2 3/5*, *Edit 1 1/7*), and SELECT walks the rows. The BLE devices list (SYSTEM > BLUETOOTH) keeps its own rows and status line. STEP's grid and roll have the whole panel from y 25 (section 4.2); its cards are an overlay, the 2 x 2 block over the grid's top while they show, a held step's two lines (y 192..229) under it. A layer's tiles are 24 px (a layer held over the mixer is the layer's screen, so it takes the big values too), and the modal takes the whole panel. There is no CARDS setting any more (SYSTEM > SCREEN's KNOB 3 is empty); the settings word's bit 23, which held it, is reserved: kept as read, never read by the UI.

**No footer** [D, phase 5b: "no footer anywhere"]: the band that held the hints, the keys' meaning and the steps'
playhead went; the panel runs to the screen's foot on every screen. **The header's badge is the selected track's
engine name** in its colour (*ANALOG*, *FM6*, *DRUMS* on the drum track) [D, phase 5b], not the track's number.

**Every value has a form** [D, the review of phase 1]: no value is shown as a bare number. One small set of forms,
chosen from the value's range alone (its descriptor's min, max and kind: `param_desc_t`), never per parameter:

| Value | Form | Where |
|---|---|---|
| a number, min 0 | a bar filled from the left | the card under the number, the list row under the number (2 px), the mixer's sends |
| a number, min < 0 < max (PAN, FILT, TRANSPOSE, ENV 1 MOD's depths, the slots' amounts, a drum TUNE) | a bar from the centre | the same; the mixer's PAN and FILT |
| a list of up to 12 (WAVE, SYNC, a palette) | a row of segments, the current one lit | the same |
| a longer list (an engine, a kit, a slot) | a tick at its place on a line | the same |
| on / off (FX, SERIAL, MUTE) | a pill, filled when on | the same; the mixer's FX |
| a name (a preset, a sound) or an action | text | |

**Text casing** [D, 2026-10-08; names table 2026-10-14, section 32]: sentence case for UI words, capitals for labels.
Screen titles, row names, messages, toasts, the modal's question and target, and the hints are sentence case (*Mix master*,
*Save?*, *Project 1*, *Clear DR?*, *New project?*, *T1 cleared*, *Home back*); every card label is in capitals whatever its
length (LEVEL, DETUNE, DEST A: the names table, `op_names.c`), as are list cells, track names (T1, DR), acronyms (FX, LFO,
ENV, MIDI, USB, BPM, CPU ...) and units' own spelling (dB, ms, steps). Preset, kit, lane, voice and project names are drawn
as stored, never re-cased (*ACID SQR*, *808 BOOM*, *c.hat*). The casing is done when this UI draws (`op_case` for our words,
`op_names.c` for the names), so the shared tables and the core's messages keep their capitals.

On SOUND the cursor row's picture is drawn over the list where the core has one: the envelope (ENV, ENV2), the LFO's
wave as it runs, the FX slots' sends as needles under their cards (SLOOP's graph maths, copied), the scale on an
octave of keys (the root white), the pattern's steps, the drum lane's name in its source's colour (on the drum
track's ENV row: the lane's envelope). Felucca's
270-degree rings are phase 5's (with its font); phase 1b draws bars (section 11.1).

What we take from Felucca [P]: the layout above, the SURF cards and panels, the theme tokens (ours already name
colours by meaning, tools/colors.json), the circled numerals, the rolling digits, the list with a bar cursor, the
confirm dialog, the NAME screen (projects and user presets could get names; kits keep numbers), the MIXER columns
with meters and rings, the ADSR / LFO / FX / scale graphs. What we do not take: its gestures (OCT+ does, OCT-
backs; SAVE held = undo; SHIFT = menu): ours are section 2.

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
shown on the cards (the mixer's own 1x4 cards, section 3) like any other screen's cursor row. It replaces the vertical strips of
phases 2 to 5b (one track a column, KNOB k track k) and the DRUM MIXER screen.

**The rows**, shown **one group at a time** (section 13, 2026-10-11): the four channels T1 T2 T3 DR when the mixer
opens, four rows of 41 px; ALGORITHM past DR shows the 16 drum sounds at once (two columns of eight), past T1 up MASTER
alone on the whole panel. The mixer keeps its 1x4 cards (every other screen is 2x2; 2x2 would leave 29 px a row):

| Row | Its knob sets (SELECT) | Notes |
|---|---|---|
| **MASTER** (above T1) | FILT · DUST · DUCK, those the build has (header *Ins tone*, the MASTER badge); then the compressor THRS · RATIO · GAIN · CEIL (section 13; header *Comp*); then the **returns** (last since 2026-10-14, the master FX screen's order: inserts, returns; header *Returns*): REV RT · DLY RT · CHO RT, a card a send a FX slot holds, in the one send order (batch D, section 33; 2026-10-12 had a card a slot, S1..S4, "-" for an insert or an empty slot) | out of view when the mixer opens: it opens on the selected track (T1 at the top); ALGORITHM turned back past T1 reveals it [D]. The master LEVEL is the analog knob: no read-out. The returns (the user, 2026-10-12: "on the mixer screen Master, we should have a volume control for the return tracks"): CHO RET, DLY RET, REV RET, 0..127 shown 0..100 %, 100 % (unity) by default, the mix as before; 0 silences that effect and stops its bus (core.h G_CRET; fx.c). One instance per type (fx_slots.c), so one return per send, wherever it sits; DLY RET is apart from the delay's own MIX. No send in any slot: no Returns set. **Return sends** (2026-10-16, the user: "ideally A should have send B, C etc"): after the returns, *A sends* and *B sends*, a set each in a track's sends grammar (card k is return k; blank: the return itself, one to its left, an empty one; card 4 blank, a return has no PAN): A's cards 2 3 (to B, to C), B's card 3 (to C), named by what the target holds; 0..127 as a track's send, 0 by default; left to right only, so the returns render in one order (fx_slots.c `rs_amt`, fx.c `rs_block`). C sends to none: no set. A return holding nothing, or with nothing to its right: no set. With the builder item RET_FB (the feedback sends, 2026-10-16: "feedback is good :)") every return sends to the other two: *B sends* gets card 1 (to A), *C sends* (cards 1 2: to A, B) follows; those sends are a block late, a quarter of the gain, limited and soft-clipped (fx.c `rs_fbv`) |
| **T1 · T2 · T3** | the mix MUTE · VOLUME · PAN (MUTE: 2026-10-12, section 16); the inserts S1 TYPE · S1's amount · S2 TYPE · S2's amount (the track's two insert slots, section 20); the sends REV · DLY · CHO; then FX on / dry · SOUND (section 13) | the sends only in a master slot |
| **DR** | MUTE · VOLUME (GLO > DRUMS); its two insert slots (the drum bus, as a track's); its send LEVELS REV · DLY · CHO (0..100 %, section 20); FX on · KIT (section 13) | the drum track as a whole; its sequence shows every lane merged |
| **the 16 lanes** (after DR, indented: kick, kick2, snare ...) | MUTE (the lane's own) · LEVEL · CUT (in PAN's place: a sound has no pan); DRIVE · DST · CMP (the sound's own inserts, always heard); REV · DLY · CHO; SOUND (section 13) | the sound's own values (SOUND 2's offsets, SOUND 3's sends); "-" where a sound has none |

The mixer's sets are values only (follow-up, section 11.7): the screens are reached by their own gestures, SONG by
SAVE held + SELECT, PROJECT by SAVE tapped, GLOBAL and then SYSTEM by GLO tapped (one cycle), SCOPE by HOME tapped on
the mixer. **The mixer has no YES** (2026-10-12, section 17, D6): SAVE tapped there always opens PROJECT. FX on and MUTE
are knobs (a turn switches them), a row's SOUND row is EDIT's first row, MASTER's effects are FX on its row.

**The encoders** [D]: **ALGORITHM walks the rows**, T1 T2 T3 DR, then the 16 lanes, and back, stopping at the ends
(MASTER above T1; the same walk on every page and layer since 2026-10-12, section 16): the track follows (song.sel), and a lane row selects the lane (lane_sel; its sound previews when
the transport is stopped), so ALGORITHM is the lane encoder on the mixer. **SELECT pages the knob sets** (section
2's paging rule; GLO tapped paged them too until 2026-10-12, it opens GLOBAL now, section 4.11); the header names the set
(*T2 mix*, *T2 insert*, *T2 sends*, *T2 sound*, *Mix master*, *Mix comp*). PRESETS browses the sound (section 12); on SOUND / KIT it
browses the row's sounds. SHIFT + a drum key picks the lane on every other screen; on the mixer it mutes that lane's sequencer instead (2026-10-13, section 21; ALGORITHM and a played key still put the cursor on a lane's row).

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
already routes keys to the UI with no layer button). **PRESETS scroll the window** over a longer pattern:
1..16, 17..32, 33..48, 49..64 up to the track's LEN, a page at a time [P]; the header says *steps 17-32*. **OCT on a synth's roll is the
keyboard octave and the roll's view in ONE value** (2026-10-13, the user: "can we make them one value?"; before it was a
view offset beside the keyboard octave, and the two drifted apart): `song.octave` (-3..+3, the entry octave) moves the
roll an octave per step from the fit (the pattern's notes, at least 13 rows), kept inside 0..127 (op_stepdraw.c
`roll_range`); OCT- + OCT+ together: octave 0, the fit. The same value is what the keys play and what the pick follows, on
every screen. On the drum track OCT stays ghost / hard. **The pick follows the octave** (2026-10-11): a note
played and then OCT+ is entered an octave up, as live playing would sound it (seq.c keeps the octave the pen was played
at, `pen_oct`; op_step.c `pick_note` moves the pen by the octaves turned since; the header's pick names it). **A step
held + OCT- / OCT+ transposes it** (2026-10-11): every note of the synth steps held an octave down / up, each clamped
to 0..127; while a step is held OCT changes neither the keyboard octave nor FM6's paging (on the drum track it does
nothing). The cards come to the step's page with NOTE lit, so the new note shows (`held_octave`). FOLLOW (the window follows the playhead, as
FELUCCA_DRUM_STEP) is the default while playing [D]; a scroll turns it off, and **both OCT together
resumes it** [D]. **PRESETS pages the window too** (2026-10-11, the user: "in the step sequencer it scroll pages thru
the piano roll, in mixer it select preset indeed"): a page a detent on every track, stopping at the first and the last
(no wrap), the follow off as a scroll; with a step held it does nothing (the held keys are the window's). It replaces
batch 33's PRESETS = the drum lane on STEP: the lane is picked with SHIFT + a key (SEQ + a key until 2026-10-14) or SELECT on the lane rows.

**The keys' lights** [P]: on STEP the set steps of the window are lit (the selected lane's on the drum track) and
the playhead's key blinks, as FELUCCA_DRUM_STEP lights them today; with the keys toggled to playing, the played
notes as KEYLIT; in a held layer its map, as today.

The panel on the drum track: the 16 lanes x the 16 steps of the page, levels as shades, ratchets as notches
(today's DRUMS grid), the selected lane highlighted; at the left each lane's colour chip and its two-letter code
(2026-10-11, the user: "put the name of the instrument (two letters) on the drum piano roll"; `LANE_CODE`, BD .. CB,
the mixer's, now in drums.c), the selected lane's white; the 16 lanes fit, nothing scrolls. SLOOP's DRUMS grid shows
the same codes in a 3 x 5 pixel font (its rows are 6 px), in the lane's colour, white when picked or hit. On a synth track: the page's 16 steps as a piano roll
(2026-10-11, the user: "looks a bit sad compared to sloop. could we have the note names on the left, and more
details ?"): the note names in a 24-px gutter on the left, the Cs with their octave (*C4*, C_HI); every row named when
the rows are 11 px or taller (a row with a note amber, a black key dim, a white key grey), else the Cs and the rows
with a note where they do not touch; the black keys' rows a faint tint (C_LINE at 2/8, the gutter too); each octave
ruled under its C; the bar's start and the end of LEN in C_DIM, the beats in C_LINE, a foot tick a step; each note a
bar as long as its ties (joined over the column gaps), its level the shade (ghost dim .. hard full), an accent a white
top edge, the ratchet as notches; the chords stacked; the steps held framed white; the playhead's column white while
it plays (only the two columns it left and reached are painted again, on a canvas of their own, never under the
cards); the window's page lit in the gutter of the playhead strip (1-16, 17-32 ...). The rows cover the whole
pattern's notes (every page on the same rows), 13 semitones at least, as tall as fit (16 px at most). The columns
are 13 px from x 26, the drum grid's too (since the lanes' codes, 2026-10-11). Black ground, C_LINE lines, no grey fills.

| State | Cards (KNOB 1..4) | SELECT | PRESETS | YES | HOME |
|---|---|---|---|---|---|
| no key held, PATTERN (the first page, every track) [D] | the pattern: LEN · DIV · SWING · GATE (LEN 1 2 4 ... 64, SHIFT by one) | the pages: PATTERN, then ARP, ARP 2 (synth) or the 16 lanes (drums) | the window's page (2026-10-11) | — | tapped: back |
| no key held, a lane's page (drums) | the lane's sound: LEVEL · TUNE · DECAY · REV | the pages; landing on a lane selects it (the lane encoder; previewed when stopped) | the window's page (2026-10-11) | open the lane's SOUND rows | tapped: back. Held + a lane's key: clear the lane's steps [P] |
| **a step held** | drums: LEVEL · RATCHET · TUNE · DECAY (the lane's locks, section 18); synth: NOTE · LEVEL · RATCHET · LENGTH (ties; a recorded fraction in tenths, section 5) | **NUDGE** (micro, -32..31) | *(none: PRESETS no longer edits the chance; the chance is on the held step's page 2, section 12)* | cycle the fill condition: normal, FILL, NO FILL | **SHIFT + the step: clear it** (notes, events, extras) |
| **a step held + a page button** (ENV, LFO, FX, SCL, ARP) | that page's four cells **as the step's locks**: a cell with an event shows a lock mark; a turn writes one (section 6) | the family's next row | the hot cell, fine | toggle HOLD of the hot cell's event | SHIFT + the knob: clear the hot cell's event |
| **SEQ held + keys** | — | — | — | — | — |

**Drums (2026-10-14, the swap):** SHIFT + a key is the **lane pick** on STEP as on every drum page (the white key's
lane, the keys play stopped and are silent playing; the last upper key picks the ACCENT lane; HOME is the modifier only
with no step held, a step held + HOME is the clear), and **SEQ held + a key clears that step** (everything of it; SEQ +
the ACCENT key does nothing: it is no step; SEQ tapped and held alone keep their meanings). Key LEDs: SHIFT shows the
lanes (the 16 dim, the selected full, the ACCENT key dim), SEQ held shows the steps with any lane set (full) and the empty
ones inside LEN (dim). A synth track keeps the rest of this paragraph.

SEQ held + keys on a **synth track** is the **pick** [D] (the screen's own button: on STEP the keys are steps, and SHIFT + a step clears
it): the keys play and the notes (one, or
several for a chord) become what the next tapped steps carry. Letting SEQ go keeps the pick. A tapped empty step takes the pick
(today's rule: "the sound shown / the note you played last"); a set step tapped is cleared; a held step is edited
and kept, several held together (as today).

**A synth step remembers its pitch** (2026-10-12, the user: "click a step remove the note, clicking again add the note
at the same pitch for that step"): a set step tapped off becomes a rest that keeps its notes (silent; saved with the
project, as any non-empty step), and tapped on again it comes back at those notes, not the pick's; a step never set,
or cleared whole with SHIFT + it, takes the pick as before (`step_off_keep`, `step_back`). The drum lanes are unchanged.

**Only a tap toggles a step** (2026-10-11, the user: "long press on step to open plock, should not toggle the step
on/off"). The toggle happens **when the key is let go**, and only if it was let go before the hold time (SYSTEM HOLD,
`op_hold_ms`) with nothing done meanwhile: a knob turned (a value, a lock, PRESETS, SELECT paging the cards) or a page
button (the step's locks) makes the press no tap, and the step stays as it was. **An empty step held long opens its
cards and makes no note**: the step values show "-", and a lock written there is a step-only event on the rest
(`lock_turn`, as before): the sequencer applies it whenever the playhead enters the step, note or not (`auto_step`),
so the notes still ringing follow it until the next step without it. A step held + another key tapped toggles the other
(the one held stays). The feedback is immediate: the key's LED is lit and its column framed white from the press;
the note itself appears when the key is let go, so a tap's latency is the tap's length (about 100 ms).

What a step key does when nothing is held, in short: tap an empty step, it is set with the pick; tap a set step, it
is cleared (both when let go); hold it, its cards open and the step stays as it was; hold it and turn, it is edited. **SEQ tapped again goes to STEP's next page** (the paging rule; STEP
opens on the page it was left on, section 24); **SEQ held alone past HOLD and let go** toggles the keys to playing with the STEP screen still up
(to audition while the grid follows the playhead); the same again, steps [D, 11.6].

The erase-as-it-plays gesture (MPC style: hold the sound's key while the pattern plays and its hits leave) stays
**EDIT held + the key**, as today, on every screen: a performance gesture, not an edit.

### 4.3 SOUND

The selected track's pages as rows, in today's PAGES order (params.c:488): SOUND (the preset, the engine, INIT,
SAVE AS), ENV, ENV 1 MOD, ENV2, ENV2 DEST, LFO, LFO DEST, (LFO 2, LFO2 DEST: built with LFO2, synth tracks), EDIT 1, EDIT 2, OSC 2, SWARM, FLT 2, VOICE, VOICE 2,
FX (the sends), FILTER, SLICER, SCL, SCL 2, ARP, ARP 2, PATTERN. The panel draws the cursor row's graph (ADSR,
LFO, the FX bars, the scale), as Felucca. **A page button tapped shows only its family's rows** [D, 2026-10-08: the LFO screen should not show ENV2 and SLICER rows]: LFO the LFO and LFO DEST rows (*Sound LFO*, its graph),
ENV the envelopes and their DEST rows, FX the track's effects (FX, FILTER, SLICER ...), EDIT the engine's EDIT rows,
SCL and ARP theirs (the families are PAGES' `fam`); SELECT moves within the family, the button again goes to the
family's next row and from its last to its first. *(Superseded 2026-10-15: no SOUND row on EDIT; see the landed-state list at the top.)* **EDIT starts with the SOUND row** (PRESET · ENGINE · INIT · SAVE AS;
the drum track KIT · LANE: section 17, D7), then the engine's rows. The whole list (every row, the SOUND row first) is
what ALGORITHM back from MASTER shows when MASTER was opened from the mixer. On the drum track the page buttons narrow the lane's rows the same way where a
family has rows there (EDIT: the lane's SOUND rows, ENV the lane's ENV row); a family with none there leaves the rows as
they are.
On a synth track a **FAVS** row follows the SOUND row (FAV, LIST: favorites, section 37). *(Superseded 2026-10-15: EDIT has no SOUND or FAVS row; see the landed-state list at the top.)*

**The drum track's ENV** [D, 2026-10-11: "most of the drum sounds have env parameters decay, attack. put them on the
env page"]: ENV opens the selected lane's ENV row (params.c PAGES, `SC_DSND` in the ENV family; the lane picked as
ever, SHIFT + its key): CLICK (the attack's transient), DECAY, BEND (the pitch drop), LEVEL (its peak). They are
the SOUND rows' own values (drum_edit.c `DE_*`, the same storage: no new value, no format change), so SOUND keeps them
too. What a sound has, it shows: a synthesised kit all four, a sampled kit or a user sample DECAY and LEVEL, an X0X
voice what its model has (`x0x_show`); none of them has an attack time, CLICK is the nearest. Its graph is the lane's
envelope as the drums run it (`graph_denv`: the level over the sound's length, the body and the noise as they hold
and decay, CLICK a white tick at the start, the pitch drop in white, a sample's peaks with DECAY, an X0X model's decay
sketched; the length at the right). A sound with LEVEL only (the X0X 909's rim shot) shows "-" in the other three
cells and *No envelope: level only* as its graph. A build without the drum editor (FELUCCA_DRUM_EDIT 0) has no such
row: ENV leaves the rows as they are, as before. The synth's ENV pages are not shown on the drum track
(`page_shown`), and ENV's button LED follows its rows (`op_fam_mask`). **The row needs a picked lane** (2026-10-13): on DR itself ENV does nothing and is dark.

*(Superseded 2026-10-15: this row is gone; presets are picked with PRESETS, the engine with SHIFT + PRESETS, INIT and SAVE AS live on PROJECT > USER.)* The SOUND row: cell 1 the preset (PRESETS browses by kind, as today; the list in the panel), cell 2 the engine,
cell 3 INIT (an action: YES), cell 4 SAVE AS (an action: YES, then the NAME screen [D]).

On the drum track the rows are the **selected lane's**: ENV (its envelope values, section 4.3's drum ENV), SOUND,
SOUND 2, SOUND 3 (the sends), SOURCE (a lane playing a user sample only: HIT START LEN), KIT, then. The SOUND row on the drum track is KIT, LANE and, on knob 3, the lane's SRC (the user, 2026-10-13: "this should be on third knob on page 1"; op_screens.c `snd_cell`, op_cells.c `LANE_SRC_PG`); PAGES' SOURCE keeps SRC for SLOOP's UI, this UI shows it as `LANE_SMP_PG`
**The first card follows the lane's source** (the user, 2026-10-14: "its first card should not allow to switch kit"; op_walk.c `lane_card_form`, op_screens.c `snd_lane_card`): DR itself and a lane on the KIT show KIT (the track's, tagged DR with a lane picked); a lane on an instrument (another kit's sound, an X0X voice) shows its **PRESET**, the lane's sound, and a turn browses the lane's sounds as PRESETS does (the kit and the other lanes stay; the edited-sound confirm "Discard DR edits?" applies); a lane on a user sample shows its **SLOT**, USR1..3, round; a lane with nothing to pick (the ACCENT lane) shows KIT dim "-" and a turn does nothing. SHIFT + the knob: the first sound of the kind the lane is on (USR1 for a sample), asked like any change; the KIT card has no default to go back to, so nothing. The help line follows the form (op INSERTS table: LANE PRESET, LANE SLOT, NO KIT). The mixer's lane rows and the picture under the cards already named the lane's own sound ("-" on the ACCENT lane), so they are as they were
the track rows it has today (FX FILTER, SLICER, PATTERN). SHIFT + a key picks the lane (the keys play on
this screen, so you hear it). On an FM6 track ENV held stays the operator editor as built
(ui_fm6.c): its own screen, the black keys OP1..OP6 PIT GLO MONO POLY.

### 4.4 MASTER (the master FX)

Reached by FX tapped on the mixer's MASTER row, or ALGORITHM left of T1 on any page (section 16); FX tapped again: its
next page. The badge reads MASTER, the header the page ("Ins tone 1/8"). Its pages (section 17, D3; 2026-10-12) **in the signal's order since 2026-10-14** (the user: "the master fx menu is a bit strange... we should have inserts, then send returns, then fx slots, then macro"): the master bus' **inserts** (INS TONE, INS COMP, INS LIMIT), the **returns** (RETURNS, the three return levels, then the effects they return: RET DELAY, RET REV, RET TYPE), the **slots** (SLOTS), the **macros** (MACRO). Page memory is the row number (RAM only): the order changed, no stored index points anywhere.

| Page | KNOB 1 · 2 · 3 · 4 |
|---|---|
| INS TONE | DUST · DUCK · FILT · ROLL |
| INS COMP | THRS · RATIO · ATK · REL (the master compressor) |
| INS LIMIT | GAIN · CEIL · **TUNE** · GR (a read-out) |
| RETURNS | CHO RET · DLY RET · REV RET (the FX returns, 100 % unity: the same values as the mixer's MASTER returns set, which keeps one card a slot) |
| RET DELAY | TIME · FDBK · COLR · MIX |
| RET REV | SIZE · DAMP · CRT · CDP (the chorus' rate and depth) |
| RET TYPE | TYPE (builds with several reverbs) |
| SLOTS | the four FX slots' types |
| MACRO | COLOR · MOTN · SPACE · ENRGY (MACROS builds) |

The mixer's MASTER row follows the same order: FILT · DUST · DUCK, the compressor, then the returns. Page names are at most 9 characters (a host check); the badge MASTER already names the screen, so the header reads *Ins tone 1/8*, *Ret delay 5/8*, *Slots 7/8*. (op_screens.c `FX_ORDER`, `fxs_rows`: PAGES keeps SLOOP's order for the SLOOP UI.) Before 2026-10-14: DELAY REVERB REV TYPE SLOTS TONE COMP LIMIT MACRO, with no RETURNS page (the return levels were on the mixer only).

The GLOBAL page (BPM SWING CLICK TUNE) and the DRUMS page (the drum level) are gone: BPM and SWING are GLOBAL's TEMPO
row, CLICK its REC row, the drum level the mixer's DR row (VOLUME); TUNE moved to LIMIT's free card. The names are the
Optimist UI's (op_screens.c `FX_ALIAS`, `FX_LIMIT`): PAGES keeps SLOOP's titles (DLY, REV/CHO, MASTER), which the SLOOP
UI draws.

### 4.5 SONG: scenes, patterns, the chain

| Row | Cells | YES | REC | Notes |
|---|---|---|---|---|
| scene A .. P (16 rows) | T1 · T2 · T3 · DR: the pattern slot each track plays in it (*3*, *keep*, *--*); **PRESETS edits the hot cell's reference** | launch at the next bar; stopped: load | **store the loop into it** (today's SONG screen gesture; *AGAIN* becomes *YES?* over a used one) | SHIFT + REC: clear it. The playing scene green, a queued one amber, a changed one with a dot (PATTERNS-DESIGN §6.3) |
| PATTERNS | T1 · T2 · T3 · DR: each track's playing pattern, a turn cues the next / previous stored one | — | store the working copy into the first free slot (DUPLICATE) | **the panel is the 4 x 16 session grid** of PATTERNS-DESIGN §6.2 and the keys launch the selected track's 16 patterns while the cursor is on this row; **SAVE held + key n stores the working copy into slot n, SHIFT + key n clears it**; copy a to b = launch a (stopped: it loads), then SAVE + b [P] |
| part 1 .. 64 (the chain) | SCENE · BARS · INSERT · DELETE | the actions | — | the cursor part plays white while the song runs |
| MODE | LOOP / SONG · SONG REC · MEM gauge | toggle, start / stop | — | |

Launching stays on the held layers for performance (section 4.9): SAVE held = scenes, LFO held = patterns.

### 4.6 PROJECT

Rows: PROJECT, SNAPSHOT, USER (the stores with slots), TOOLS (CLEAR SEQ · INIT SOUND · MISSING · NEW), SLOOP 2.4 (the
import of its slots and autosave, as built) and the MEM gauge. **A storage row is BANK · ACTION · ITEM · (NEW)**
(2026-10-14, the user: "select bank (if there are more than 16), the operation (save, load, delete/clear), and then press
one of the 16 keys and then confirm with the popup yes/no. Default action is load"; "one knob for the item, and below we
display the info about the item"; "NEW is just clearing what is in memory and creating something blank"), `op_prjflow.c`:

| Card | What | Notes |
|---|---|---|
| 1 BANK | the bank of 16 slots the keys are | **only on a store of more than 16 slots** (USER: 32, 2 banks); otherwise an empty card. Turning it goes to the same place in the other bank |
| 2 ACTION | **LOAD** (the default), SAVE, CLEAR | the card says ACTION (the card labels are 6 letters at most; the help line says "Load, save, clear, then a key"). CLEAR is the one verb for removing a slot (the names table: USER's old ERASE is CLEAR too). **PROJECT has LOAD and SAVE only**: a project slot has no clear (nothing in the section log deletes a slot; NEW is the blank project). The value is red when it would destroy or replace (the rule below) |
| 3 ITEM | the slot | one knob that **walks all the slots, across the banks** (the BANK card follows); dim when free, the snapshots in their status colours |
| 4 | PROJECT: **NEW**, an action card with its confirm "New project?" (always red: it drops the work in memory); SNAPSHOT, USER: empty | NEW touches **no slot** (the help line: "Blank project (slots kept)") and is not an ACTION |

- **The 16 white keys are the slots of the current bank: key n selects item n AND runs the action on it**, through the
  existing confirm (SAVE yes, HOME no, 3 s): "Load project 3?", "Save snapshot 2?", "Clear user U18?". The ITEM follows the
  key, so the knob and the key LEDs (a used slot full, a free one dim, the item blinking) always agree.
- **YES on the row (any card but NEW) runs the action on the ITEM** the knob is on, with the same confirm: the knob path
  works without the keys.
- **A free slot to LOAD or CLEAR has nothing to do**: a toast "Slot U10 is empty", no question. SAVE into a free slot asks
  (neutral).
- **The action goes back to LOAD after each action, when the row changes and when the screen is left** (`op_enter`): a
  stray key never saves. HOME on the question keeps the action as chosen.
- Risk colours (`prj_danger`, card and modal alike): SAVE over a used slot and CLEAR of a used one are red; LOAD is red only
  when the work is not as stored (it replaces edits), neutral otherwise; SAVE into a free slot neutral; NEW always red.
- **The panel under the cards** (replaces the row list on the storage rows; TOOLS, SLOOP and MEMORY keep theirs): a strip of
  the bank's slots (used filled, free outlined, the item framed in the track's colour), then the sentence a key would run
  now in the action's colour ("Load project 3", red when it would replace), the item's name large, and its info read from
  what is stored, nothing scanned: a project its name (the section log's, `sec_name`; "Project 3" without one) and stored /
  free; a snapshot its name, its status (saved, saved by another build in amber, damaged in red) and its size in KB; a user
  preset its name and engine (in amber when the engine is not in the build). A project's tempo and size are not shown: they
  would need the record read.
- Projects and user presets are named on Felucca's NAME screen when saved, the keys typing the letters [D]; kits keep
  numbers (the drum track's user kits are the KIT page's, not PROJECT's).
- Tests: tests/ui_optimist_prjflow.h (the default, the knob, a key with each action, HOME, YES, the free slot toast, BANK
  only past 16, the ITEM walk across banks, the keys moving the item, the return to LOAD, NEW, the info per store).

### 4.7 SYSTEM

**A settings list since section 20** (one setting a line, pages of 4 lines, no cards; what follows names its sections). Today's HOME menu, as rows: **INFO (section 31; section 38: CLOCK, the CPU, DROPS, UNDERRUNS, GUARD, the battery; first), INTERNALS, USB STATS (section 38)**, SCREEN (COLOR BRIGHT -
KNOB COLORS), LIGHTS (LIGHTS KEYS), AUDIO (LOWCUT / BASS+ ...), USB (SERIAL), BLUETOOTH, USB AUDIO (CH1 CH2 CH3 CH4), CALIBRATE (PANEL, HOLD), ABOUT:
the setup. **USB AUDIO** (USB audio builds, 2026-10-15, the user: "four lines that allow to select the source for each of
the 4 channels"): one line a capture channel, its source as text (no bar): OFF, MAIN L, MAIN R, MAIN L+R,
T1, T2, T3, DRUMS, DRUM BUS L, DRUM BUS R, DRUM BUS L+R, DR1..DR16 (L+R: (L + R) / 2, each side x 0.5, -6.02 dB:
never clips, a mono signal keeps its level; no -3 dB choice, the user 2026-10-15); the knob walks the list (stops at the
ends), YES steps it round, OCT- held + the knob its default (T1 T2 T3 DRUMS, the stems as before). Where each is taken:
io/usb/ua_tap.h; kept in the settings record. CALIBRATE > PANEL asks first ("CALIBRATE? PANEL"); its screen says that 30 s with no input cancels. **MIDI (OUT IN SYNC CLOCK), CLOCK OUT and CHANNELS
(T1 T2 T3 DR) moved to GLOBAL** (2026-10-12, section 4.11): they are defined once, at the end of SYSTEM's table, and
shown there only. **Reached by GLO tapped on GLOBAL** (section 20; GLO tapped on SYSTEM goes back to GLOBAL). HOME
held alone on the mixer no longer opens it.

**2ND CORE (REMOVED 2026-10-14, section 31: the core is always on; what follows is the old design).** The CPU section is CLOCK · CPU0 · CPU1 ·
**2ND CORE**, exactly one page of 4 lines (a single-core build has no such line: CLOCK · LOAD, the next section on page 1).
2ND CORE is **AUTO** (default: the second core starts at power-on and is held for the session on its first misread, wrong
canary, late job or fault) or **OFF** (one core). The knob (KNOB 4 on that page) or PRESETS edits it (right OFF, left AUTO),
YES toggles it, **SHIFT + PRESETS resets it to AUTO**. It is `dual_mode`, in the settings word (bits 27..28, as the other
settings: saved when SYSTEM or GLOBAL is left, once stopped). It applies at the next start, so the value is **amber** while it
differs from the setting this boot started with. This UI sets `DUAL_MODE_MENU 1` (system/dual_mode.h): a self-disable (AUTO
turning itself OFF after a misread, canary, timeout, fault or flash failure) is then saved, and this line is how the user sets it
back. **CPU1's cell** shows the load while the core runs; **when it is down it shows why**, in amber, from `dual_why_name`
(NO START, TIMEOUT, FAULT, FLASH, MISREAD, CANARY, AFTER CRASH, SETTING), OFF when no reason is recorded. SLOOP's UI (UI=0)
has no 2ND CORE line (its menu is unchanged; the boot message "CPU1 OFF <why>" still names the reason), so a self-disable is
not saved there. Tests: tests/ui_optimist_core.h.

### 4.8 TEMPO, a held page [D]

A tempo page opened by holding a button, with nudge (OCT+/-), fine adjust,
MIDI sync and so on, was asked for; the page below is the decided form. **PLAY held** (past the hold threshold; a tap stays start /
stop):

| | |
|---|---|
| cards | BPM · FINE (0.1) · SWING · SYNC (AUTO INT USB TRS) |
| OCT- / OCT+ held | **nudge** [D]: the clock runs a few percent slower / faster while held and comes back when let go; the BPM value never changes (DJ beat-matching; a no-op while following an external clock) |
| a white key | tap tempo (two taps or more) |
| SELECT | the rows (the screen's pages): TEMPO (BPM · NUDGE · SWING · SYNC: the tempo and its nudge together), REC (MODE · LENGTH · START · CLICK: the REC MODES screen's dials), REC 2 (QUANT · - · - · -: REC QUANTIZE, section 5; the free cells for what joins it) |
| the panel | the beat as a big number, the clock source it follows (A:USB ...), the RX light |

Its three rows are also GLOBAL's first three (2026-10-12, section 4.11), the same functions (op_tempo.c `tp_*`). TEMPO
stays: it is not a pure duplicate (it opens over any screen while PLAY is held, its white keys tap the tempo, and its
panel is the tempo picture); GLOBAL is where the rows sit with the MIDI settings, as a list.

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
| **HOME** | **+ a key where the keys play: pick the drum sound** (the key plays it); **+ a step on a synth track's STEP: clear it** (the drum track's STEP: SEQ + a step; SHIFT + a key picks there); **+ REC: clear the track** (YES confirms) | + a knob: the cell's event, or the cell to its default | the pick and the clear modifier [D]; HOME tapped alone is NO |
| **SAVE** (= YES held) | **1..16 launch scene A..P** at the next bar (one bank of 16, no OCT bank; an 8-section build uses keys 1..8) [D]; **REC tapped: SONG REC** on / off; **REC held + key n: store the loop into n** (three fingers, to confirm on the hardware); **SHIFT + key n: clear n**; **PLAY: play the song from the start** [D]; the quick chain (tap several) as built | — | today: 1-4 launch, 5-8 store, 13 loop / song, 14 song rec, 16 the song page, OCT the bank. The uniform vocabulary: *layer + key = launch, layer + REC + key = store, layer + SHIFT + key = clear* |
| **LFO** (patterns) | **1..16 launch pattern n** of the selected track at its end (OCT- held: next bar, OCT+ held: now, as built); **REC held + key n: store** the working copy into n; **SHIFT + key n: clear** n [P] | none: LFO held + a knob is **SHIFT** (section 11.6; the cue was the built layer's, taken as a default) | [O] decided on the hardware: this uniform vocabulary (ALGORITHM is the track, copy is a SONG row action, duplicate is the PATTERNS row's REC), or the built black-key modifiers (6 store, 7 copy, 8 clear, 9 duplicate, 1..4 the track), or both |
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

### 4.11 GLOBAL, GLO tapped [D, 2026-10-12]

The user: "what is supposed to be the GLO page ? i thought it would be the page where we have bpm, nudge, midi track
settings etc". **Since section 20 GLOBAL is a settings list** (one setting a line, pages of 4 lines, no cards; the
rows below are its sections, their cells its lines); what follows is the first version. **GLO tapped opens GLOBAL**, as SLOOP's GLO page: the settings of the whole instrument, not of a track, a
user changes while playing. 2x2 cards, the rows under them (no picture), the header *Global TEMPO 1/7*:

| Row | KNOB 1 · 2 · 3 · 4 | Where it lives |
|---|---|---|
| TEMPO | BPM · NUDGE (a read-out: OCT- / OCT+ held nudge the clock on this row, as on the TEMPO page) · SWING (GLOBAL shows these three; SYNC is PLAY's TEMPO page's 4th card, GLOBAL's CLK IN) | TEMPO's (op_tempo.c `tp_*`) |
| REC | MODE · LENGTH · START · CLICK | TEMPO's |
| REC 2 | QUANT | TEMPO's |
| MIDI | CLK IN (SYNC's value) · CLK OUT · MIDI IN · MIDI OUT (one page: the clock and the notes, in and out; 3 lines without MIDI_CLKOUT; the CLK read-out and the Clock out page are gone, "Auto: Int" already names the clock followed; GLOBAL's Tempo has no SYNC, PLAY's TEMPO page keeps it as a card) | SYSTEM's table (op_project.c `SYS`, `sys_*`), no longer shown on SYSTEM |
| CHANNELS | T1 · T2 · T3 · DR (the MIDI channels) | SYSTEM's |

**GLO tapped again switches to SYSTEM, and tapped on SYSTEM back to GLOBAL** (as built since section 20, op_global.c
`glo_tap`; GLO tapped on any other screen opens GLOBAL; the coherence audit's #45 corrected this paragraph, which still
said a tap walks the rows: GLOBAL's rows then SYSTEM's, round, section 17's D1). SELECT walks one sequence, GLOBAL's
pages then SYSTEM's, and stops at its two ends. The
header's badge names the screen (GLOBAL or SYSTEM, as MASTER's does), its n/N counts within that screen. GLOBAL keeps its row
when left, as every screen. Leaving it saves a changed setting of the FM-1, as leaving SYSTEM does (`sys_leave`).
SYNC shows on PLAY's TEMPO page (SYNC) and GLOBAL's MIDI page (CLK IN) (one value, `song.g[G_SYNC]`, as SYSTEM's MIDI row always had it). **GLO held is the
performance layer** (section 4.9), unchanged: a hold is no tap. **The GLO LED** is full on GLOBAL and SYSTEM and
half lit elsewhere. The mixer's knob sets, which GLO tapped used to page, are SELECT's alone; nothing else needed GLO tapped (a
step held + GLO does nothing, as before; the FX screen's GLO pages, the master's, are reached by FX on the mixer's
MASTER or ALGORITHM left of T1). The mixer is still the HOME screen, HOME tapped goes back to it.

Not on GLOBAL: TUNE (it stays with the master: MASTER's LIMIT page since the GLOBAL page went, section 17, D3 / D5),
the drums' VOLUME (the mixer's DR row), the macros (MASTER). SYNC stays on both TEMPO and MIDI (D4).

## 5. Recording

- **Live ("real record")**: REC as today. Playing: records the selected track at once, REC again stops. Stopped:
  arms; the first note starts the loop, or the free take on an empty project, or the count-in (REC MODES). A take
  ends at a pattern switch (PATTERNS-DESIGN §5.4). Overdub, latency-compensated, chords and ties, as today.
- **REC QUANTIZE** (2026-10-11, feat/rec-quant; per project, the TEMPO screen's REC 2 row, SLOOP's tempo page 2):
  **ON** (the default, new and old projects) a recorded note snaps to the grid, its start and its length in whole
  steps (TIEs, the half-step rule, no lock: as before the recorded lengths below); **OFF** keeps how it was played:
  its start as its step's nudge (MICRO, 1/64 of a step, own steps only; without MICRO on the grid) and its length
  as below, from where it starts (the time held). Saved in the project's G_INFO slot (bit 0 = OFF, project.c
  RECQ_PACK: the slot is the CPU read-out's, 0 in every project before, clamped to 0 by an older build); a SLOOP
  2.4 import or export clears it.
- **Recorded lengths** (2026-10-11, feat/rec-length; with REC QUANTIZE OFF; FELUCCA_PLOCK, both UIs, every input and the free take): the
  whole steps a note is held stay TIE steps, the rest of its length a GATE lock on its last step (seq.c
  `rec_len_end`): a NOTE step plays GATE / 128 of a step, the last TIE of a chain half a step + GATE / 128. Held 0.9
  steps locks 116 and plays 0.9; 1.8 locks 39 on its TIE and plays 1.8. A TIE never sounds past its own step (a REST
  or a note after it releases it), so a NOTE and k TIEs play k + 0.5 .. k + 1 steps and a NOTE alone up to 1: in the
  gaps the nearer one (a TIE is kept from a quarter into it: 1.4 plays 1.5, 1.1 plays 1, 2.1 plays 2). The length
  runs from the start of the step the note was quantised to, to the release as heard (a note pressed late gets
  longer by as much). Within 4 of the track's GATE: no lock. A note cut by the next one (MONO / LEGATO / UNISON, or a POLY step full: overlapping notes below) locks 127 and joins it
  (`REC_LEGATO_JOIN`). Never on a ratchet step or a step that held notes before (overdub keeps their locks). The
  STEP card LENGTH shows the length in tenths (*1.8 ST*) when the last step has the lock; KNOB 4 resizes in whole
  steps and drops the fraction (the note then plays at the track's GATE).
- **Overlapping notes** (2026-10-12, fix/rec-overlap; POLY, REC QUANTIZE ON and OFF, both UIs, every input and the
  free take; user report "I hold a note, press another, the first is cut"): a note keeps sounding while you play
  others over it, live and played back. Before, the step model ended a held note where the next note's step began
  (seq.c `seq_step` releases what sounds at every NOTE step). Now that step becomes a chord of the new note and the
  held ones with the step flag `SF_CARRY` (step_t.flags bit 7, free until now; chance uses bits 2..6): the notes of a
  carry step already sounding go on, not struck again, the others sounding end at its start (`seq_carry`). A held
  note up while notes that started in another step are still down ends at the nearest step start (the key-to-ear
  latency taken back): from there a carry step of the rest (`rec_drop`). One step is the precision of such an end
  (one GATE a step); the last notes up keep the exact length above. Held into a step with older notes (overdub): they
  join that step the same way (its notes struck, the held ones not). A chord's keys (same step) still share their
  length until the last is up. MONO / LEGATO / UNISON keep one note a step: the held note ends where the next one
  starts, joined to it, as heard live (the voice plays one note). A carry step at the start of the loop, or after a
  REST, strikes all its notes. A SLOOP build ignores the bit (the carried notes are struck again there). The web editor
  keeps it since protocol v12 (the hi byte's bit 2: a flags byte on the wire has 7 bits).
- **Each note's own end** (2026-10-12, feat/note-gates; the user's decisions D1 option C, D2, D3; POLY, REC QUANTIZE OFF,
  every input and the free take): a note up while others are still down ends at its own recorded time, not at a step.
  A step in RAM holds a byte per note (step_t.ngate: note k ends ngate / 128 of the step after its start, 0 = the step's
  GATE); the playback counts each such note down by itself (seq.c `seq_noffs`, once a block, nothing when no note has
  one). The step where the note ends names it: a TIE there becomes a carry step of the notes sounding (it plays as the
  TIE did), and the step after carries the rest without it, so a build without own ends ends the note at that step. A
  chord's key (the notes started in one step) up more than 1/8 of a step before the chord's last key keeps its own end;
  within 1/8 the chord ends together, at its last key (D3). REC QUANTIZE ON: whole steps, as before (D2). MONO / LEGATO /
  UNISON: as before. Stored: no project format change; a stored step is 10 bytes as ever, the own ends travel as step-only
  events of the track's automation list (ids 0x78 + the note's slot, never in a working list: seq/ngate.h), so an older
  build loads the project, keeps the events and plays the notes to the step. A full list (128 events with them): the note
  ends at its step, as before. The STEP page's LENGTH (KNOB 4 with a step held) shows the first note's length in tenths
  through its TIEs and carry steps to its own end (seq.c `note_span`, the one measure the piano roll and the web editor's
  `noteSpan` share); turning it resizes in whole steps and drops the own ends of the step it ended in. The piano roll
  draws a note on through the carry steps that name it (no new head) and its last cell as long as it sounds. The SLOOP
  UI shows the steps as they are. The SLOOP 2.4 export keeps the step's GATE and says what is lost (lost2 bit 5).
- **Step entry**: section 4.2.
- **Automation**: section 6. Two gestures, one store.
- **Undo / redo**: SAVE then HOME, SHIFT then SAVE, and EDIT held + OCT- / OCT+ as SLOOP [D]; many levels as
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

**A drum lane's value is an event too** (the user, 2026-10-12): on the drum track, the parameter **0x7C + (lane << 3 |
value)** (0x7C..0xFB, seq/auto.h `AUTO_LANE`) is lane 0..15's TUNE DECAY SNAP CLICK BEND CUT DRIVE LEVEL (drum_edit.c
DE_*: a stored order), its value the lane's offset. A step-only one is the lane's lock on the step (it needs the lane's
hit there; every hit of the lane during the step hears it), a hold one its motion. Both apply **at the hit** through an
overlay (drum_edit.c `dla`, drums/lane_play.c): a sounding hit keeps what it started with, and the lane's own values
(dl.ofs, what SAVE keeps) are never written. A build before them keeps the events and plays none; they never go into
the old MOTN or stepx forms (a list with one is stored in the new forms), and a 2.4 export reports them lost (LANE
MOTION NOT IN 2.4). The UIs' calls: seq/auto_lane.c.

**Each drum lane has its own LFO** (the user, 2026-10-12): RATE (P_LRATE's range: free, or synced below 0), WAVE, PHS,
FADE and two slots, each a DEST (OFF or one of the 8 lane values) and an AMT (-64..63). An LFO with no slot acting (DEST
OFF or AMT 0) is off and costs nothing. A live one is sampled at the lane's hits (as the values above: the overlay, never
dl.ofs), restarts at PLAY at PHS, and has no per-block tick: its value is figured at the hit from the blocks or the beats
since PLAY. It is not an automation event: it is kept in a record of its own (storage/dlfo_rec.c; in flash: log ids
122..138, sections/dlfo_rec_log.c; the snapshots' SNR_DLFO; the editor's backup object DLFO), which an older firmware
skips (it loads the project with every LFO off, nothing else lost). A 2.4 export reports it (DRUM LFO NOT IN 2.4). The
UIs' calls: drums/lane_play.c `dlfo_get`, `dlfo_set`, `dlfo_field`, `dlfo_field_set` (DLFO_DESC: the cells' ranges and
names); the slot logic, shared with the synth LFO to come: core/lfo_slot.h.

**Nudge and the fill condition are events too** [D]: the pseudo-parameters NUDGE (-32..31, in 1/64 of a step) and
FILL (1 FILL ONLY, 2 NO FILL), step-only. One list then holds every per-step extra, and SLOOP 2.4's stepx record
(seq/stepx.h, 176 B a track) is only the import / export form: its micro, lock and fill arrays become events on
read and are rebuilt from the events on export. To be revisited if the size or the
sequencer's scan bites. The sequencer reads a step's NUDGE and FILL in the same scan as its values; micro timing
needs the next step's nudge ahead of time, as `micro_units` reads it from the array today, so the scan is per step,
never per sample [E].

**A step with automation is coloured** (2026-10-16, the user: "in SEQ the step with automation should be colored differently"):
a step that carries a lock or a motion event of the sound (`op_step_auto`, op_state.c; NUDGE, FILL, CHANCE and COND are step
extras, not automation) is drawn in the automation colour `C_AUTO` (colors.json "marks" / auto, `#ffec80`, a pale yellow:
lighter and less saturated than every engine, kind and status colour, and not white, which is the playhead and the held
frame) instead of its lane's or the track's colour, its level still the shade (ghost 5/8 .. hard full, the plain ones 3/8 ..
full). It applies to the drum grid cells, the synth roll's notes and the mixer's step strips (a drum lane's strip: that lane's
locks; the DR row: any lane's). A lane's lock marks that lane's cell only, a track's lock or motion every cell of the step.
The dot below the grid stays. Host test: `tests/ui_optimist_stepauto.h` (opt-step-auto-drums / -roll, opt-mixer-auto).

The STEP screen shows an event as a dot under its step in the lane's or the roll's colour; a hold event with a tail
to the next event of that parameter. With a step held and a page button down, a cell with an event on that step
shows a lock mark and its value; the hot cell's event: YES toggles bit 6, SHIFT + the knob clears it. SHIFT + the step
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
| **the pick gesture**: SHIFT + keys (where the keys play) and SEQ held + keys (on STEP) go to the UI *and* play the sound | layer keys are silent (keyboard_block) | a per-layer "keys also play" flag; SHIFT and SEQ held become pick layers whose keys the UI drains from `lk_q`; UI-FEASIBILITY step 6's contract extended |
| **SHIFT as the clear modifier** | SHIFT is the menu | on STEP the keys are the UI's already (steps), so SHIFT + a step is UI-side, as SHIFT + a knob and SHIFT + REC; the menu moves to a SYSTEM row; EDIT held stays LY_ERASE |
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
4. **Lock a layer**: decided, SHIFT + the layer's button in either order.
5. **Scenes on the keys**: decided, 16 on 16 keys, no bank; an 8-section build uses keys 1-8.
6. **The LFO layer**: the uniform *launch / REC store / HOME clear* vocabulary against the built black-key
   modifiers: to be tried on the hardware (arriving 2026-10-08).
7. **Names**: decided, Felucca's NAME screen for projects and user presets; kits keep numbers.
8. **FOLLOW on STEP**: decided (section 4.2).
9. **Fill-skipped steps**: decided, hold events apply, step-only events are skipped with their step.
10. **EDIT held's knobs**: decided, SHIFT · LENGTH · TRANSPOSE stay on EDIT held.
11. **The pick on SOUND**: decided, SHIFT + a key only.
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
| PROJECT | PROJECT (ACTION ITEM NEW), SNAPSHOT (ACTION ITEM), USER (BANK ACTION ITEM), TOOLS (the TOOLS page: CLRSQ INIT MISS NEW), SLOOP 2.4 (IMPORT) | the slots as a strip and the item's info (section 4.6); a slot lit when used, dim when empty; snapshots in their status colours |
| SYSTEM | SCREEN (COLOR BRIGHT), LIGHTS (LIGHTS KEYS), AUDIO (LOWCUT), MIDI (OUT IN SYNC CLK), CHANNELS (T1 T2 T3 DR), USB (SERIAL), CPU (LOAD CLOCK), CALIBRATE (PANEL), ABOUT (VERSION) | saved to flash when SYSTEM is left (once stopped, as SLOOP's menu) |

The grammar as built: SELECT the row (stops at the ends), ALGORITHM the track, PRESETS the selected track's sound
(section 12: no longer the hot cell's value), KNOB 1..4 the cells (a turn makes its cell hot; an action cell only becomes
hot). SAVE tapped = YES (enter, toggle an on / off value, do an action, confirm), HOME tapped = NO (cancel, back to
the mixer; at the root nothing). SHIFT + a knob: the cell to its default. SHIFT + REC: *CLEAR T2? YES*. HOME
held + a drum key: the lane (the key plays). SAVE then HOME: undo; SHIFT then SAVE: redo; EDIT held + OCT- / OCT+ too.
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
the compressor's GR bar, the pick on synth tracks, SAVE + a page button (save) and SHIFT + a page button (clear / lock),
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
| The hot cell when the row changes | back to cell 1 (the one YES acts on) | keep the column |
| PRESETS on a value | *(superseded 2026-10-10: PRESETS only browses sounds; the knobs reach every value)* | `op_presets_turn` |
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
  FILT, TRANSPOSE, ENV 1 MOD's depths, the slots' amounts, a drum TUNE), a row of segments with the current one lit (a list of up to
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
| The header | the screen and its row as one sentence (*Mix master*, *Sound ENV*, *Project snapshot*); a row that starts with the screen's name stands alone (*Project*, *Sound*), so no header repeats a word (a host check over every screen and row); the font has no middle dot for *Sound · ENV* ; **the page's n/N after it** (2026-10-11, the user: "when i page thru with select ... display the name of the page and the 1/4 (current/total pages). ideally everywhere"): permanent wherever SELECT walks pages, *Sound ENV dest 2/4*, *T1 insert 2/4* (the mixer's sets), *ENV2 5/22* (a held step's card pages: *Step*, *Extra*, then the p-lock pages), *ARP 2/3* (STEP's pages while their cards show; the window's *Steps 1-16* otherwise, and a window scroll takes the cards away); the n/N always whole, a long name losing the screen's word first, then cut (`head_fit`); the same button's repeat walks the same pages; the track-name flash and a message win while they show | `head_title`, `head_pos` |
| Casing: the exception list | the agreed list (DR, T1..T3 through the digit rule, FX, LFO, ENV, MIDI, USB, BPM, CPU, OCT) and ENV2, GLO, ARP, SCL, UI, CC, ACID, GEN, MHZ; a message's lane name is cased with it (*Reset pedal hat*) | `case_keep` |
| The footer under the modal | its first line (the YES / NO hint) blank, its KEYS line kept: the keys still play while a question waits | `draw_foot` |
| The toast over the panel | the panel keeps drawing under it and the toast is drawn again on top when it does; the mixer's meters hold still while it shows (it covers two of them) | `op_frame_draw`, `draw_mixer` |
| Bars instead of rings | the ruling allows "a bar or a 270-degree ring": bars, because Felucca's rings need its anti-aliased renderer (phase 5) | `draw_gauge` |
| The number on the list rows | the same Terminus S as before ("the number small" has no smaller face in this build); the form is a 2 px bar under it | the font of phase 5 |
| What stays text | names (a preset, the mixer's SOUND row, a palette's name has its segments too), actions (YES), read-outs with no range (LIMIT > GR); the clock has a bar to 240 MHz, the CPU load to 100 % | the `cell_gauge` callers |
| SOUND's graphs | the pages with a graph in the core (ENV, ENV2, LFO, FX, SCL, PATTERN, the drum lane); none for VOICE, ARP, EDIT, SLICER, the ENV 1 MOD rows; the list shortened to three rows under a graph | `has_graph` |
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
  while playing and no step is held; PRESETS scroll it and stop the follow, SHIFT + OCT- + OCT+ resumes
  it; OCT alone moves a synth roll's view (2026-10-12, section 4.2; the keys playing: the octave) and is ghost / hard
  on the drums.
- **Step keys**: an empty step tapped is set with the pick at once; a set one is cleared when let go unless it was
  edited meanwhile (SLOOP's rule); SHIFT + a step clears it all (notes, ties, nudge, locks, fill); a step held
  and HOME tapped clears the steps held. Every step key pressed opens an undo level (`undo_mark`, a new session).
- **A step held**: the cards are the step's (drums LEVEL · RATCHET · - · -, the selected lane's hit; synths NOTE ·
  LEVEL · RATCHET · LENGTH, the ties as SLOOP's / Melodee's `step_note_resize`), several held edit together; SELECT
  the nudge (FELUCCA_MICRO), PRESETS the chance (synth steps, FELUCCA_CHANCE; drums "-"), SAVE the fill condition
  (FELUCCA_FILLS), shown in the footer (*Nudge +4  Chance 85%*, *Step 6  Fill only*). ALGORITHM does nothing while a
  step is held.
- **Locks** (FELUCCA_PLOCK, seq24.c's store, `lock_set`): a page button (ENV LFO FX SCL ARP) pressed with a step held
  makes that family's first page on this track the cards, as the held steps' locks; a turn writes a lock on every
  step held (from its lock value, else the track's), a locked cell shows a padlock and its value in amber; SHIFT + the
  knob drops the lock; the button again: the family's next page; the last step let go: the cards come back.
- **A step let go**: its cards keep showing the page that was held until their timer ends, on STEP only. The
  snapshot goes with the cards and with STEP: every other screen's cards are its own page's (2026-10-11 fix: a step
  let go just before leaving STEP left its p-lock / extra cards on every page).
- **The pick**: drums: SHIFT + a white key picks the lane (`lane_sel`) and plays it (2026-10-14; it was SEQ held + keys, now SEQ + a key clears the step: `step_key`). Synths: SEQ held + keys, the keys play
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
(SHIFT + OCT+ to 17-32 with its header, a key past LEN, the stop at LEN, FOLLOW back, the window following a playing
pattern), the clears (SHIFT + a step, a step held + HOME), the SEQ toggle, a question blocking the step keys, an
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
| The drum pick vs "the last sound hit" | one value, `lane_sel`: SHIFT + a key sets it (SEQ held + a key until 2026-10-14), and so does a drum key played while STEP's keys are toggled to playing; a hit elsewhere (the mixer, SOUND) does not | `step_played` |
| The synth pick | the core's pen (`pen_note`, `pen_n`): the notes played last anywhere, a chord when keys are held together; one note on a MONO track, as SLOOP | a pick of the UI's own |
| A drum step's level when tapped | NORM; GHOST with OCT- held, HARD with OCT+ held | `step_set` |
| What a tap clears | drums the selected lane's hit (the step's other lanes stay), synths the note with its ties; an emptied step loses its nudge, locks, fill; SEQ + a step (drums), SHIFT + a step (synths): everything | `step_wipe` |
| HOME with a step held | HOME tapped (nothing turned while down) clears the held steps; SHIFT + a knob drops a lock (or a step value back to NORM, x1, one step) | `op_no`, `step_turn` |
| The lock page | latched by pressing the page button with a step held (no need to keep it down); the button again: the family's next page; the last step let go ends it; EDIT and GLO do nothing there | `step_lock_page` |
| SELECT with a lock page | stays NUDGE (the table's "the family's next row" is the page button again) | `step_knobs` |
| SAVE with a step held | the fill condition, also with a lock page (the table's "toggle HOLD" is phase 3's) | `op_yes` |
| Undo levels | one per step key pressed; the knob edits while it is held join it; nudge, locks and fills are not in the undo history (undo.c keeps steps and LEN / DIV) | `st.sess` |
| Black keys on STEP | nothing | `step_key` |
| ALGORITHM with a step held | nothing (the step belongs to the track shown) | `step_knobs` |
| FOLLOW when stopped | the window stays where it is; a scroll turns FOLLOW off playing or stopped, only SHIFT + OCT- + OCT+ resumes it (no indicator in the header; the playhead strip marks the side when it is outside) | `step_tick` |
| STEP's drum rows | the 16 lanes with LANE_SHORT's names, the cards LEVEL TUNE DECAY REV; synth rows PATTERN, ARP, ARP 2 | `step_rows` |
| The roll's range | the window's notes, at least 13 semitones, at most 56 (2 px each) | `roll_range` |
| SOUND's family on the drum track with no such family | from another screen: every row; on SOUND: the rows stay as they are (ENV has the lane's ENV row since 2026-10-11, with the drum editor built) | `op_jump_sound` |
| SOUND's family shown | without the SOUND row (preset, engine, INIT, SAVE AS): that row is only on HOME > Sound | `snd_first` |
| The mixer's walk | the strips' controls, MASTER, then SOUND FX PROJECT SYSTEM; the cursor rests on VOLUME | `MIX[]` |
| The mixer's master values | at the strips' foot, label, value and bar under each knob, lit on the MASTER row | `draw_strip` |
| The strips' control rows | no labels (8 px a row: the header names the lit control); the drum strip's "-" a dash | `draw_strip_ctl` |
| The mixer's "FX" rows | the on / dry row is FX ON, the screen's entry stays FX | `MIX[]` |
| STEP's footer | the hints *Seq play  Home back*; the keys' line *Keys steps* + the pick, *Pick* + it while SEQ is held, *Keys play* when toggled | `step_foot` |

**Not built** (later phases, or not asked): the automation store and the event marks under the steps (phase 3);
chance on drum steps (an event, phase 3); YES toggling HOLD of a lock; SELECT as "the family's next row" with a lock
page; EDIT held as the erase layer, and the other performance layers (FX ARP SCL GLO held still do nothing); SHIFT +
OCT on other screens; SHIFT + a lane's key clearing a lane on STEP ([P] in 4.2's table); the fifth master
column; the DRUM MIXER.

**Found in the spec** (not changed; the smallest reading taken): section 4.2's table gives SELECT two jobs with a
step held (NUDGE, and with a page button "the family's next row"): NUDGE kept, the page button again moves the
family. 4.2 says SHIFT + a lane's key clears the lane's steps [P] and also that SHIFT + a step clears the step:
on STEP the keys are steps, so the second was built. Section 7's per-layer "keys also play" flag turned out
unnecessary (above). The phase 2 gate names "the layers kept", which no item of the content list asks for: not
built. The ruling's "Sound LFO" header: the header names the screen and the cursor row, so the LFO DEST row reads
*Sound LFO dest*.

### 11.3 Phase 4: what was built (feat/ui-optimist, 2026-10-08)

The held performance layers (section 4.9), the layer lock, the TEMPO page (4.8), SAVE / SHIFT + a button (2.1), the
SONG screen (4.5), and two mixer rulings made during the phase (section 4.1: no master values at the
strips' foot, PAN at the foot). UI=1 only; the three core additions are under `#if FELUCCA_UI == 1`, so the SLOOP-UI
build (UI=0) is unchanged (measured below).

**What changed** (`firmware/src/ui/optimist/`, new: `op_layers.c` the layers' keys, knobs and lock; `op_laydraw.c`
their map and the TEMPO and session-grid pictures; `op_tempo.c` the TEMPO page; `op_song.c` SONG; `op_combos.c`
SAVE / SHIFT + a button, all in SIZE_FILES):

- **The layers.** FX, EDIT, ARP, SCL, GLO, LFO (PATTERNS) and SAVE held are the core's layers as for SLOOP's UI:
  `ly_bit[]` gets their buttons (`lay_bits`, each frame), so seq.c `layer_now` routes the keys in the ISR (FX the
  punch-in, EDIT the erase, ARP the note repeat) or to `lk_q` (SCL, GLO, SAVE, LFO), which the UI drains with STEP's
  keys (`op_drain`). The keys' and knobs' logic is copied from ui/sloop's `ui_layers.c` and `ui_pat.c` (nothing there
  is called): SCL the key of the song and CHORD SCALE KEYS TRANSPOSE; GLO mute 1-4, solo 5-8, 9-12 FX on / off (FILLS:
  9 a fill held, 10 the next bar, FX on the black keys 1-4), 16 tap tempo, the four levels; EDIT SHIFT LENGTH TRANSPOSE
  (one undo level for the hold); LFO the patterns with the built black-key modifiers (open question 6); FX FILTER DUST
  DUCK (TRK_FILT: the track's FILTER); ARP RATE. SAVE: keys 1-16 launch scenes A-P at the next bar (stopped: loaded;
  an 8-section build keys 1-8), REC tapped = SONG REC on / off, REC held + key n = the loop into scene n (the modal
  over a used one), SHIFT + key n = clear n (the modal), PLAY = the song from its start, two taps or more = the
  quick chain. While a layer is held the screen is its map: the header names it, the cards are its four knobs (their
  forms), the panel its 16 tiles in the instruments' colours, the footer its other gestures and its state (the key
  and scale, the chain, a modifier held). It shows after 140 ms or at once when used; a page button held longer than
  450 ms is no tap (no jump to its rows). PRESETS is the sound as everywhere (no longer the layer's hot knob), ALGORITHM is the track (past T1: MASTER),
  SELECT does nothing.
- **The lock** [D]: SHIFT + the layer's button in either order: the layer held then SHIFT pressed, or SHIFT then
  the layer's button. The map says *FX locked*, the button blinks; any button but PLAY, REC and OCT lets it go and
  does only that.
- **TEMPO** [D]: PLAY held 400 ms shows the page (a key while PLAY is down: at once), let go the screen before comes
  back. Rows TEMPO (BPM · NUDGE · SWING · SYNC), REC (MODE · LENGTH · START · CLICK) and REC 2 (QUANT · - · - · -,
  2026-10-11: REC QUANTIZE, SELECT down to it; BPM and NUDGE stay together on TEMPO). OCT- / OCT+ held nudge the
  clock 3.9 % slower / faster, the BPM value unchanged, nothing while an external clock is followed (*Nudge: external
  clock*); a white key taps the tempo. The panel: the tempo large (amber while nudged), the four beats, the clock
  followed (*A:INT*, *A:USB*) and its RX light.
- **SAVE / SHIFT + a button** (section 2.1's table): SAVE + SEQ the working pattern into its slot (the modal over a
  used one); SAVE + ENV LFO EDIT FX SCL ARP the sound into the first free user preset slot (the drum track: its 16
  lanes into the first free user kit); SAVE + REC, SAVE + PLAY as the SAVE layer; SHIFT + REC (a SONG scene row only: that scene cleared, asked; the
  track: the REC ring, 2026-10-13); SHIFT + SEQ its locks, nudges, fills and motion (the modal,
  *Clear T1 locks?*), the notes stay; SHIFT + PLAY stop and every voice off (CC 120's `midi_silence_track` on each
  track); SHIFT + ENV INIT (the modal); SHIFT + OCT off STEP the octave back to 0; SHIFT + a layer's button: the lock.
  SAVE then HOME (undo), SHIFT then SAVE (redo) and EDIT + OCT kept; the undo / redo now acts when one of the two is
  let go, so SAVE + SHIFT + a scene key clears that scene and undoes nothing.
- **SONG** [D: the mixer's walk, *SONG* after *FX*]: SCENE A..P (each track's pattern in it, *KEEP*, *--*, a plain
  section *SEC*; PRESETS edits the hot cell's reference, YES launches at the next bar or loads when stopped, REC
  stores the loop into it, SHIFT + REC clears the scene; the playing scene marked green, a queued one amber, *Scene B\**
  when its tracks changed since it was stored), PATTERNS (each track's playing pattern, *3>5* queued; a turn cues the
  next stored one; the panel is the 4 x 16 session grid; while the cursor is there the keys launch the selected
  track's patterns, SAVE held + key n stores into slot n, SHIFT + key n clears it, REC duplicates into the first
  free slot), MODE (LOOP / SONG, SONG REC, the MEM gauge, SAVE), then the chain PART 1..64 (SCENE, BARS, INSERT,
  DELETE; the part playing marked white).
- **The mixer** [D, two rulings]: the master values (BPM SWING LEVEL FILT) left the strips' foot and the SELECT walk;
  the fader and meter are 56 px (were 44), the control rows 10 px; PAN is drawn at each strip's very foot.

**The core additions** (each under `#if FELUCCA_UI == 1`; the SLOOP-UI build has none of them):

| Where | What |
|---|---|
| seq/seq.c, `clk_nudge` and `events_block` | `static volatile int8_t clk_nudge` (1/256 of the tempo); `adv = adv + ((int32_t)adv * clk_nudge >> 8)` before the transport, so the steps, the arp, the rolls, the click and the arranger move together; an external clock overrides `adv` after it as before. No divide in the ISR. G_BPM is never written |
| storage/sections/pat.c, `pat_scene_ref_set` | scene s's reference of track k set to a slot, KEEP or none: stopped only; its record (the arena's or the log's) written again into the log with that byte changed, its FX record (`fx_rec_log.c`, paired by the record's hash) and its drum lane LFO record (`dlfo_rec_log.c`) keyed again |
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
SONG REC on / off, SAVE + SHIFT + key clears and does not undo, SAVE + PLAY the song); TEMPO (a tap starts / stops,
held: the page with no transport change, BPM, the REC row, tap tempo, the nudge measured on the clock: 2-10 % faster
over 200 blocks, the BPM value unchanged, back to 0 when let go, OCT- slower, still playing after a hold); the SAVE /
HOME table (a user preset, a user kit, the pattern into its slot and asked again, the extras' clear and its question,
INIT asked, the octave, the panic); SONG (the mixer's row, the rows' count, REC stores into a scene and asks over a
used one, PRESETS edits a reference, amber until written, written once it rests, YES loads, the PATTERNS row's keys,
SAVE + key, SHIFT + key asked and cleared, REC duplicates, SHIFT + REC clears a scene, MODE's toggle, BARS, INSERT,
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
| SHIFT + PLAY | the panic of 2.1's table; PLAY's page is not lockable (the lock rule names the layers; PLAY's hold is a page) | `home_combo` |
| The SAVE layer and the lock | not lockable: SAVE then HOME is undo [D] | a gesture of its own |
| Undo / redo | when SAVE or HOME is let go (was: at HOME's / SAVE's press), so a scene key between them clears instead | `op_released` |
| SHIFT + ENV | INIT, asked (2.1's [P]); nothing on the drum track; ENV is no layer here (no FM6 operator editor in this UI) | `home_combo` |
| SHIFT + SEQ | the selected track's step extras (nudges, locks, fills) and its motion, asked; the notes stay; not in the undo history | `op_act_more` |
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
| The knobs in a layer | PRESETS the sound, ALGORITHM the track (past T1 MASTER, whose four cards are FILTER DUST DUCK COMP), SELECT nothing | `lay_knobs` |
| EDIT's SHIFT and LENGTH on the drum track | the drum steps too (SLOOP's rotate the synth steps only) | `pattern_rotate`, `pattern_length` |
| Labels | *REC* for SONG REC on the cards (8 letters do not fit a card) | `song_cell`, `lay_cell` |
| Sentence case | a letter alone and a note keep their capital (*Scene B*, *Key C#*) | `case_keep` |
| The mixer | the fader and meter 56 px, the control rows 10 px, PAN at the foot; the master LEVEL read-out dropped (the analog knob) | `op_draw.c` MX_* |

**Found in the spec** (not changed; the reading taken): 2.1 makes SHIFT + PLAY the panic while 4.9 lists PLAY's hold
as a layer and the lock rule says SHIFT + a layer's button locks: the panic kept, PLAY's page not lockable. 4.9's
SAVE layer has SHIFT + key n clear n, while SAVE then HOME is undo: the undo moved to the release. 4.8's FINE
0.1 needs a clock change (not built). 4.5 puts MODE after the 64 parts: before them. 4.5's REC on a scene row
stores, so REC does not record on SONG's scene rows (it does on every other row and screen); SHIFT + REC there clears
the scene, not the track. 2.1 lists ENV's HOME as INIT "elsewhere" and the lock "on an FM6 track": this UI has no
FM6 layer, so INIT everywhere but the drum track. Section 11.2's "the master values" on the mixer are gone (section
4.1's rulings).

**Proposals** [P]:

1. **A shortcut to SONG** (the mixer's row is the only way in; SAVE tapped is YES): (a) GLO tapped twice (the FX
   screen, then SONG), (b) a SONG row on the TEMPO page (PLAY held + SELECT), (c) the SAVE layer's map with a third
   gesture (SAVE held + SELECT turned: SONG when let go).
2. **FINE**: a clock counted in tenths of a BPM (BEAT_U x 10, `adv = n x BPM10`): every division stays whole; a core
   change of the sample-accurate clock, to measure.
3. **Locking the scenes**: SAVE held + SHIFT, both let go together = lock (today an undo); or keep it unlockable.
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
  deletes before the cursor (at the start: cancel, nothing written), SHIFT + a knob clears the name. The header says
  *Name U03* / *Name project 3*, the cards' band is the field (12 cells, *4/12*), the panel the keyboard; no footer.
  The keys type and never sound (they reach the UI as STEP's do, `ly_lock` LY_STEP). A user preset's name is its
  record's (`up_rec_t.name`, as before); a project's is a new record of the section log (below). Kits keep numbers.
- **FM6's operator editor** (section 2.1's ENV row, 4.3, 4.9): on an FM6 track, **ENV held is the layer** (seq.c
  LY_OPS, `ly_ops_on` set each frame from the selected track): a black key picks OP1..OP6, PIT, GLO or MONO / POLY
  by the FM-1's printed labels, the white keys play, the cards are the page's four values, the panel the algorithm
  (the operator picked white, carriers in the engine's colour, a switched-off one dim) over the black keys' map;
  OCT- / OCT+ the page back / on; SHIFT + ENV locks it. Let go after use: SOUND's FM6 rows on that page. **ENV tapped
  is SOUND's FM6 family**: OPERATOR (OP ALG FDBK VCE), the picked operator's six pages (FREQ, LEVEL, RATES, LEVELS,
  KEY SCALE, CURVES), PIT's two, ALGO, LFO, LFO 2, PORTA, STORE (14 rows), the algorithm drawn over the rows on each;
  ENV again the next row, round. Edits go into the part's voice (`fm6_ed`), as SLOOP's editor. On any other engine
  ENV is what it was (the envelopes' rows; no layer). The page tables and the algorithm's maths are copied from
  `ui/sloop/ui_fm6.c`; nothing there is called.
- **Graphs on every row of a family** [D]: SOUND's row without a graph of its own shows the nearest row of its family
  that has one (above, then below): LFO DEST the LFO's wave, ENV 1 MOD and ENV2 DEST their envelopes
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
  written or dropped now marks a level (`lock_turn`; before, only the nudge and fill paths did), and SHIFT + SEQ's
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
step's nudge, fill and lock and their redo byte for byte, SHIFT + SEQ's clear undone; NAME (opened by SAVE + ENV /
ARP, SAVE AS, PROJECT USER, PROJECT with the log; the header; multi-tap, the digits, space, delete, the knobs, HOME
cancel, refused while playing, the name written and read back, a used project slot asked then prefilled); FM6 (ENV
tapped: the 14 rows, the operator, a page's value in the voice, the algorithm drawn; ENV held: the layer, a black
key's operator, PIT, OCT, a knob, let go on that page; another engine: as before); the graphs on every LFO and ENV
row; the SOUND row's engine and chip, the mixer's toast and its colour. 224 to 259 checks a switch set. Fixed while
running them all: the lock write marked no undo level (the PLOCK-only set), SHIFT + SEQ's clear marked none, the
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
| SHIFT + SEQ | its clear is one undo level (11.3: not in the history) | the `undo_mark` in `op_act_more` |
| Where NAME opens | SAVE + a sound button, SAVE AS, PROJECT USER SAVE, PROJECT SAVE (log builds); not a kit (the drum track's SAVE + a button saves a user kit at once), not a snapshot | `name_user_free`, `name_user`, `name_project` |
| NAME's grammar | SAVE done; HOME tapped delete, at the start cancel; SHIFT + a knob clears; Felucca's keys and multi-tap | `name_frame`, `nm_knob`, `nm_black` |
| NAME while playing | SAVE refused (*STOP BEFORE SAVE*), the screen stays with the name typed (a flash erase stops the audio; `up_ui` refused it the same way) | `name_ok` |
| An empty name | a user preset gets the automatic one (*DIGITAL 01*), a project none | `name_ok`, `up_store` |
| A used project slot | the modal first, then NAME prefilled with its name | `prj_yes` |
| Project names | the section log's record id 23, 16 x 12 B, stopped only; builds without the log save unnamed | `SEC_ID_NAMES`, `name_project` |
| A name's case | stored as typed (upper case), drawn in sentence case | `name_draw` |
| ENV on an FM6 track | held = the layer, tapped = SOUND's FM6 rows; SHIFT + ENV the lock there (INIT elsewhere, as 11.3) | `LAYER_BTN`, `lay_btn_layer`, `op_jump`, `home_combo` |
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
no layer here" and "SHIFT + SEQ not in the undo history" are superseded above. 4.3 shows no FM6 family on SOUND:
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
  SHIFT + a drum key on the mixer (the key plays the sound): it opens on that sound's block with the sound
  selected; the same pick moves inside it; PRESETS scroll the block (no NO); HOME tapped goes back to the
  mixer; ALGORITHM to a synth track goes back too (the drum mixer is the drum track's). The rows are the mixer's
  [D: like other tracks]: LEVEL (the sound's, SOUND 2: the fader), CUT (its filter cut, in PAN's place: drawn at
  the strip's foot, from the centre), REV · DLY · CHO (its sends, SOUND 3), DRIVE ("-" on a sampled sound), FILTER
  and FX ON ("-": the track's, on the mixer's drum strip), SOUND (the names; YES opens the hot sound's SOUND rows).
  The strips look and work as the mixer's: no cards, SELECT walks the controls, the control lit on the block's four
  strips, PRESETS the hot strip's value one unit, SHIFT + a knob its default. Each strip in its source's colour
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
- **The header** [D]: the badge is the track number and engine (*1 ANALOG*, *3 FM6*, *4 DRUMS*, 2026-10-14) in
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
on the four strips, CUT at the foot, KNOB 2 / PRESETS / SHIFT + a knob on the right lane, REV on KNOB 4, DRIVE and
FX ON "-", the pick inside, SHIFT + OCT- / OCT+ (stopping at the last block), a hit's flash drawn and falling, the
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
| SHIFT + OCT on the drum mixer | the block before / after, stopping at the ends (no wrap); OCT alone keeps its job | `dm_scroll`, `op_press` |
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
2. **Sixteen sounds at a glance** without the 16-strip view: on the 8 view, SHIFT + OCT already moves the knobs' block across both pages; a level-only row of 16 small meters over the strips would give the whole kit's hits on one screen.
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

**2026-10-12 (final): the cards always 2x2, the picture or the list under them, no CARDS setting.** The user, after e0b476e: "Cards are ALWAYS 2x2 on every screen with cards except the mixer" (their FM-1 had very likely come up 1x4 after bit 23's inversion, and they did not know the setting existed); "we can keep the list below when there is nothing to display besides the 4 cards"; "Keep the graph"; "arp is the same as lfo/env. no list".

- op_layers.c `cards_2x2()`: true everywhere but the mixer (and on the mixer while a layer's map shows); `op_cards`, `CARDS_LINE` / `CARDS_2X2` / `CARDS_N` (op_state.c) and SYSTEM's `SI_CARDS` (op_project.c) are gone; SYSTEM > SCREEN is COLOR, BRIGHT, -, KNOB COLORS (KNOB COLORS stays on KNOB 4).
- storage/settings_word.c: bit 23 is neither read nor written; `bp23_kept` keeps it as it was read, so the record's format does not change and an old 1x4 choice is simply ignored.
- op_draw.c `draw_list`: a picture (`pic`, SONG's PATTERNS grid excepted) takes the whole panel with no rows; else the rows under the big values (five of 20 px, taller when fewer, at most 40). The picture of every ARP row is ARP's (`snd_graph_page` finds the family's graph: ARP 2 has none of its own). SONG's PATTERNS row keeps the grid and its rows at the foot, as before e0b476e. `head_pos` gives every screen its n/N (TEMPO and SCOPE included); SONG's title no longer takes the cursor row's colour (the rows show the marks again).
- Tests: tests/ui_optimist_cards.h (no CARDS cell, bit 23 set by an old record ignored and kept, 2x2 on every screen with cards, the mixer 1x4, a layer 2x2, KNOB 4 hot), tests/ui_optimist_cardspic.h (ENV, LFO, FX, SCL and every ARP row: the picture alone; every SOUND row of every track: a picture alone or its rows; TEMPO and SCOPE with their n/N; PROJECT, SYSTEM, SONG, the master FX, EDIT 1 and VOICE: their rows under the big values); the other UI tests run in 2x2 now (they ran in 1x4: main set it), with the few checks that read the 1x4 geometry moved to the 2x2 panel.

**2026-10-12: GLOBAL, GLO tapped** (section 4.11). op_global.c (new, after op_project.c): `glo_rows` / `glo_name` / `glo_cell` / `glo_turn` / `glo_yes` call TEMPO's `tp_*` for rows 0..2 and SYSTEM's `sys_*` for the global rows, so no value is defined twice; the SYSTEM row's YES is `op_enter(SCR_SYSTEM)`; `glo_tap` opens GLOBAL or steps its row; the screen table moved there (`SCREENS`, `SCR_GLOBAL`). op_project.c: MIDI, CLOCK OUT and CHANNELS are SYS's last rows (`NSYS_GLO`); SYSTEM shows the first `NSYS_SETUP`. op_input.c: GLO tapped calls `glo_tap` (was op_mixer.c `mx_glo_tap`, gone; `mx_page` lost its wrap); `op_jump` is SOUND's only (its FX-screen branch served GLO alone); `op_enter` saves on leaving SYSTEM or GLOBAL; OCT on GLOBAL's TEMPO row is the nudge (op_tempo.c `tempo_nudging`); `op_screen_btn` lights GLO on GLOBAL. Tests: tests/ui_optimist_glo.h (the rows, SYSTEM without them, the cells being TEMPO's and SYSTEM's, BPM on KNOB 1, every row 2x2 with its list, GLO again and round, the nudge on TEMPO and the octave on REC, the save on leaving, the SYSTEM row, GLO tapped on SOUND and on MASTER, GLO held the layer), ui_optimist_mixer.h (SELECT alone pages the sets, GLO tapped opens GLOBAL), ui_optimist_clkout.h (CLOCK OUT on GLOBAL), the LED test (GLO full on GLOBAL).

History (superseded by the blocks above):

**CARDS 1x4 or 2x2** (section 0, 3): SYSTEM > SCREEN's fourth cell, *CARDS* 1x4 / 2x2, in the
settings word's bit 23 next to DR MIX's bits (FELUCCA_UI == 1). `op_cards` (op_state.c); the bands follow it
(`OP_CH`, `OP_PY`, `OP_PH`, `MODAL_H` in op_graph.c); `draw_big` and `draw_card_band` (op_draw.c) draw the 2 x 2
block (SLOOP 2.4's graph_big layout, copied: ui/sloop is not called); the cards of the screens and of the layers
both go through `draw_card_band`. What shrank in 2x2 as first built (2026-10-08; STEP since has its whole panel, section 4.2, and the pictures the whole panel, below): `ROWS_SHOWN`, `GRAPH_H` (40: the envelope, the LFO, the FX
needles, the scale scale with it; the pattern's steps in two rows of 12 px; the drum lane's kit name beside its
name; the SOUND row's presets picture shows the one playing only; TEMPO's picture drops its clock line, SYNC being a
card), STEP's `SG_LH` (5) and its held-step lines' pitch (13), the tiles' `TILE_H` (24), SCOPE's `SC_H` (94),
FM6's map (`F6_MAP_Y` 80). Tests (tests/ui_optimist_cards.h): the band as each mode draws it on SOUND with and
without a graph, the SOUND row, STEP (roll and drums), TEMPO, SONG, FX, PROJECT, SYSTEM, SCOPE and a layer; the
geometry inside the panel; KNOB 4's value white at the bottom right; the setting and its bit; 3000 frames of random
use in 2x2.

**2026-10-12 (superseded): 2x2 the default, the picture alone** (the user: "drop the row list"; "but not in mixer !"; 2x2 the
default). op_draw.c `draw_list`: in 2x2 (`cards_2x2()`, never on the mixer) a screen with a picture other than SONG
draws no rows, `gr_h` = the whole panel (117), no rule and no scroll bar; its signature takes the cursor row's and the
graph page's cells (the rows that carried them are gone). `head_pos` gives TEMPO and SCOPE their n/N in 2x2 (1x4: their
rows and tabs say it, unchanged). op_scope.c: `SC_H` is the whole panel in 2x2 and `scope_tabs` draws nothing. `cards_2x2()` moved to op_layers.c: true in 2x2 everywhere but the mixer, and on the mixer while a layer's map shows (it was 1x4 over the mixer and 2x2 over the other screens).
op_laydraw.c `tempo_draw`: the large beat's four boxes are placed from the right edge (the fourth was off the screen
whenever the picture was 100 rows or more, in 1x4 too). op_state.c: `op_cards` starts at `CARDS_2X2`; the settings
word's bit 23 now means 1x4 (settings_word.c), so a record without it reads 2x2. Tests (tests/ui_optimist_cardspic.h):
the default at boot and from an empty word; SOUND ENV, LFO and FX alone in 2x2 with the header's n/N and with their rows
in 1x4; every SOUND row of every track, a picture alone or its rows; TEMPO alone with 1/3 then 2/3 (1x4: three rows, no
n/N); SCOPE without tabs (1x4: with); PROJECT, SYSTEM, SONG and FX keep their rows; the mixer 1x4 in 2x2, a layer held over it 2x2 and the mixer 1x4 again after; a held step's
lines under the big values; 3000 frames of random use in 2x2. ui_optimist_cards.h's setting test now turns 2x2 and back
to 1x4 and reads the bit both ways; the other UI tests still run in 1x4 (main sets it after reading the default).

| Question | Chosen | Undo |
|---|---|---|
| The default | ~~1x4~~ **2x2** (2026-10-12, the user): the values read at a distance, and the picture no longer shares the panel with rows, so it is larger than in 1x4's few-row pages; 1x4 is the option for a long list in view | `op_cards`' initial value (op_state.c) |
| Old saved settings | **2x2**: bit 23 is inverted (1 = 1x4), no new bit and no format change: every record from before (the bit 0, the default then) comes up 2x2; a unit where 2x2 had been chosen (bit 1) comes up 1x4 once and is set back with SYSTEM > SCREEN > CARDS. A marker bit (27) would have kept that choice, at the cost of a new bit | settings_word.c `bp23_word` / `bp23_from_word` |
| A screen with a picture, in 2x2 | **the picture alone on the whole panel**, the header naming the row and its n/N; ~~SONG keeps its rows~~ SONG's PATTERNS row too (2026-10-12) | op_draw.c `draw_list` |
| A screen with only a list, in 2x2 | ~~its rows under the big values~~ **the big values and an empty panel** (2026-10-12, the user: "remove the bottom list"; "same cards, empty below"): PROJECT, SYSTEM, SONG, the master FX, a SOUND row with no picture (EDIT, VOICE, ARP 2 ...); the header's n/N places the cursor; **the BLE devices keep their list** (a picker) | op_draw.c `draw_list` |
| SCOPE's source tabs, in 2x2 | **gone**: the trace to the foot (94 to 117 rows), the header *Scope T2 3/5* | op_scope.c `SC_H`, `scope_tabs` |
| A layer held over the mixer, in 2x2 | **2x2**: the map is the layer's screen, not the mixer (the same layer over SOUND was already 2x2) | op_layers.c `cards_2x2` |
| STEP in 2x2 | **unchanged**: its grid and roll already have the whole panel from y 25 in both modes (lanes 12-13 px, the roll 204 px); the cards are an overlay, a held step's two lines under the 2 x 2 block | op_stepdraw.c |

**2026-10-12 (later; its "no list" superseded): no list anywhere in 2x2, the MASTER page's returns, OCT on the roll.** The user tested firmware
d31a7ae on the FM-1: EDIT 1..5, VOICE, PROJECT and SYSTEM still showed the rows under the big values, and the mixer's
MASTER page showed the four channels' meters again. Rulings: "use 2 lines for 4 bigger cards everywhere we can (not
mixer screen) and remove the bottom list"; the MASTER page "we have the level of the 4 instrument tracks vu meters,
useless ... replace them with the fx return levels" (the GR line kept); "the oct up / down keys in piano roll should
actually shift the piano roll" (only the view).

- op_draw.c `draw_list`: in 2x2 `shown` is 0 on every screen (the picture, if any, on the whole panel; else nothing
  drawn under the cards). `draw_head`: on SONG in 2x2 the title takes the cursor row's mark colour (`song_row_col`).
- What the list alone showed, and where it went: a row's values and actions were always the cursor row's cards, so
  every action stays on the cards and SELECT (PROJECT's LOAD / SAVE / NEW, the snapshots, the user presets, TOOLS;
  SONG's scenes, MODE, the chain's parts with INSERT / DELETE; SYSTEM's settings; the master FX pages). Lost: the view
  of the other rows' values at a glance (the header's n/N says where the cursor is), and SONG's marks of the scene
  playing / queued and the part playing on rows other than the cursor's (the cursor row's mark is the title's colour;
  the scenes layer and the session grid still show what plays). Kept as a list: the BLE devices (a picker: the
  devices' names, the status line and the pairing code are drawn only there; its cards are CHOOSE / CONNECT and the
  signal).
- The MASTER page (op_mixdraw.c `mx_paint_master`, `mx_draw`, `mx_rets`): under GR the lines REV DLY CHO, each built
  return only (`FELUCCA_FX_REVERB` / `_DELAY` / `_CHORUS`), names and meters in C_HI; the meters' state reuses the
  channels' places (T1 + line), and the peaks are cleared when the view's group changes (`mxd.pk_first`). The levels:
  fx.c `fx_ret_tap` (FELUCCA_UI == 1 only; UI=0 is byte-identical), called once a block at the end of `fx_buses` when
  a bus ran: the chorus' right output of the block's last sample (`yr`), the delay's (`x`), the reverb's as
  `wet_r[n - 1]` less those two; their |value| kept as a running maximum in `fx_ret_pk` (main RAM code, `HOT2`: no
  per-sample work, the call costs 44 B of RAMTEXT), taken by ui/meters.c `meter_tap` like the tracks' peaks and read by
  `meter_ret_take`. It is a sampled peak (one sample a block), not a scan of every sample; how
  far under the true peak it reads has not been measured.
- STEP's roll: the view is `song.octave` octaves (the keyboard octave, one value; 2026-10-13) from the fit, applied at the
  end of `roll_range` and clamped there to 0..127; op_input.c's OCT on a synth track is the keyboard octave on every
  screen (after the held-step, HOME and EDIT cases, which are unchanged). The roll's signature takes the octave.
- SLOOP's UI is unchanged: its STEP page's roll (ui_draw.c `graph_roll`) fits the pattern's notes (an empty pattern
  48..72) and OCT alone there is the keyboard octave; SEQ held + OCT pages the step bank.

Tests (tests/ui_optimist_cardspic.h, tests/ui_optimist_cards2.h): in 2x2 every row of PROJECT, SYSTEM, SONG and the
master FX, EDIT 1, VOICE and every SOUND row of every track: the picture alone or the panel black under the big
values, with the header's n/N; 1x4 keeps the rows on the same screens; the MASTER page's three return names and no
track colour under GR, the GR line, the REV meter filling alone; the tap seeing a reverb, a delay and a chorus send
each on its own return, `meter_tap` and `meter_ret_take`; OCT+ / OCT- on the roll moving the view by 12 with
`song.octave` untouched, both together back to the fit, the top stop, a track change, a held step + OCT still
transposing, SHIFT + OCT the window and FOLLOW, the drum track. ui_optimist_step35.h's entry test sets the octave with
SEQ held + OCT now. Sizes (user-default UI=1, before / after): flash 519,340 / 520,016, RAM 84,824 / 84,916, pool
324,620 / 324,620, RAMTEXT 31,684 / 31,728 of 32,512; UI=0 byte-identical (530,176, 85,792, 316,428, 31,584).
| A value too wide for the large face | the small face in its place (a preset name) | `draw_big` |

**HOLD** (a bug found on the FM-1, section 0): `op_hold_ms()` (op_state.c, SYSTEM > CALIBRATE's second
cell, 250 / 350 / 500 ms, default 350; main's shared HOLD setting (core/hold.h `hold_sel`, the HOME menu's too: 0 = 350 ms, 1 = 250, 2 = 500; the settings word's bits 21..22, so a word without them keeps the
default); bit 23 was CARDS (reserved since 2026-10-12), and bits 24..25 are free. The SLOOP 2.4 export carries none of bits 21..31 (`sl24_word`). It replaces op_layers.c's `LAY_SHOW_MS` (140 ms, SLOOP's SHOW_MS: a layer's map) and op_tempo.c's
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
(SHIFT + a key, on STEP too since 2026-10-14; op_step.c `step_played` for the keys-play mode), STEP's SELECT (below). **The pick's key while
playing**: `pick_silent` (the drum track, the transport running, SHIFT, with no step held on STEP) sets the
keys' lock to LY_STEP, the core's existing "the keys reach the UI and play nothing" (seq.c), and op_drain drops
those keys so they set no step; stopped, the lock stays LY_PLAY and the key plays. **The core's change**: seq.c's
audition (`audition_req`, `audition_lane`, `audition_block`, SLOOP 2.4's, built only with FELUCCA_DRUM_STEP) is now
also built with `FELUCCA_UI == 1`: one guard widened on its two blocks, no code changed; UI=0 builds are unchanged.
Tests (tests/ui_optimist_lane.h): the preview once per lane change when stopped (requested, then played by the next
block), none while playing, none on a synth track; SHIFT + a drum key on STEP stopped (the key plays, no second preview)
and playing (silent, the lane picked, no step set); SHIFT + a drum key on the mixer while playing (silent, the
cursor on the lane's row).

| Question | Chosen | Undo |
|---|---|---|
| The lane encoder on SOUND (the drum track) | none: ALGORITHM there is the track (as everywhere but the mixer) and SELECT walks the lane's pages (the paging rule); the lane changes by the pick (SHIFT + a key) or on the mixer / STEP | — |
| A lane re-selected (no change) | no preview | `lane_pick_from` |

**STEP's first page and the paging rules** (section 0, 2, 4.2): op_step.c `step_rows` puts
PATTERN (`stp_pattern`, the FAM_SEQ page whose first value is LEN) first on a synth track, and the drum track's
pages are PATTERN then the 16 lanes (`STP_LANE0`); op_input.c `op_enter` opens STEP on row 0; `op_row_pick` on a
lane page selects the lane (`lane_pick`, previewed when stopped), and `op_rows_fix` keeps a lane page on the lane
picked by SHIFT + a key (SEQ + a key until 2026-10-14). SEQ tapped on STEP: its next page, round (`step_seq_tap`); the keys' toggle moved to SEQ held
alone past HOLD. SELECT in the FM6 layer (ENV held) now pages it (`fm6_lay_select`, stopping at its kind's ends).
Checked against the rules and unchanged: ENV LFO FX SCL ARP EDIT tapped again (op_input.c `op_jump_sound`, round),
SELECT on every list (`op_row_pick` clamps), the mixer (4.1). Tests (tests/ui_optimist_lane.h `step_pages_tests`, the
STEP tests updated, the FM6 test): STEP opens on PATTERN on both kinds of track, SEQ again pages round, ALGORITHM on
STEP switches tracks, SELECT from PATTERN to the lanes with lane_sel following and the preview only when stopped, a
step held keeps SELECT as NUDGE, SELECT in the FM6 layer stops at its first page.

| Question | Chosen | Undo |
|---|---|---|
| SEQ tapped on STEP, which toggled the keys steps / playing (section 0's ruling) | the newer paging rule wins: SEQ again pages; the toggle moved to **SEQ held alone past HOLD** (SEQ held + a key stays the pick on a synth track; on drums it clears the step since 2026-10-14) | `step_seq_tap`, `op_tap` |
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
back, the same while playing, SHIFT + REC clears nothing (2026-10-13).

| Question | Chosen | Undo |
|---|---|---|
| SHIFT + REC (the clear behind the modal) | gone 2026-10-13 (section 27): the REC ring is the only track clear; SHIFT + REC still takes REC's press (no ring, no arm) and clears a SONG scene row's scene | `home_combo` B_REC |
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

### 11.7 Follow-up: nothing wraps, the screens leave the mixer (feat/ui-followup, 2026-10-08)

**Nothing wraps** (section 0, 2): a page button tapped on the last page stays there. (Superseded 2026-10-10: a button
tapped again wraps again, past the last page to the first, in the same functions; SELECT and ALGORITHM still stop at the
ends. The SLOOP UI's page buttons, ui.c `open_family` and ui_fm6.c `fm6k_next_page`, always wrapped.) Changed: op_input.c `op_jump_sound`
(SOUND's families) and `op_jump` (the FX screen's), op_step.c `step_seq_tap` and `step_lock_page` (STEP's pages, the
locks' pages), op_mixer.c `mx_page` (GLO's round), op_fm6.c `fm6_lay_page` (OCT- / OCT+ in the FM6 layer). SELECT
already stopped. Four lines of tools/div_audit.txt went with the modulos.

**The screens leave the mixer**: `mx_items` gives the values only (the MI_GO kind and the "SCREENS" set are gone).
The gestures (op_input.c `op_tap`, `mx_quiet`):

| Screen | Gesture | State |
|---|---|---|
| global FX (delay, reverb, master compressor, dust, duck, filter) | **FX tapped, then ALGORITHM turned left past T1** (op_input.c `fx_algo`); right goes back to T1's FX pages, left on it stays; FX tapped again on it: its next page, past the last the first; the header says "FX master"; the MASTER row's YES (a cell picked) still opens it too, kept: it is the mixer's way, not redundant | decided |
| SONG | SAVE held + SELECT (op_song.c `song_shortcut`) | decided |
| PROJECT | **SAVE tapped on the mixer**, no cell picked, nothing asked | provisional |
| SYSTEM | **SHIFT alone** on the mixer: released past HOLD with nothing else touched | provisional |
| SCOPE | HOME tapped on the mixer (it was not in the removed set) | as built |

Clashes checked: SAVE tapped is YES elsewhere, and on the mixer YES still acts when a knob picked a cell (a toggle, the
SOUND rows: `ui.hot_lit`), so PROJECT opens only on a fresh row; a question pending keeps SAVE = confirm; SHIFT +
a knob / REC / a key / a layer button is taken first (default, clear, pick, lock) and is no tap, so only SHIFT alone
reaches SYSTEM; HOME tapped (under HOLD) is still the scope; off the mixer a long HOME is still NO.

**Kept** (section 0): SEQ tapped on STEP pages, SEQ held toggles the keys / playing; LFO held is SHIFT.

**Decisions open for review** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| PROJECT's gesture | SAVE tapped on the mixer with no cell picked (provisional: the user "does not know") | one branch in `op_tap` (`mx_quiet`); another gesture: call `op_enter(SCR_PROJECT)` from it, or put a MI_GO cell back in `mx_items` |
| SYSTEM's gesture | SHIFT alone on the mixer past HOLD (provisional) | the HOME branch in `op_tap`; the same two ways |
| The global FX's place | FX then ALGORITHM left past T1 (the user, \"FX, and then scroll left with ALGO\"), as MASTER above T1 on the mixer | `fx_algo` in op_input.c; header in `head_title` |
| Pages stop at the last | decided; the round trip is gone, SELECT goes back | `op_jump_sound`, `op_jump`, `step_seq_tap`, `step_lock_page`, `mx_page`, `fm6_lay_page` |

### 11.8 Phase 3: the automation store (feat/ui-automation, 2026-10-08)

Section 6 built: **one list of events a track** (its working pattern; a pattern record carries its own), each 3 bytes
{place: step 0..63 + bit 6 STEP-ONLY, param, int8 value}, up to **128**. It replaces motion.c's 64 shared events and
SLOOP 2.4's stepx arrays (micro[64], lock[24], fill[16]) as the one store the sequencer, both UIs, undo, the editor and
the storage use; stepx.h stays as the 2.4 import / export form and as the old stored form (below).

**The int8 check (section 6.2) [M]**: `tests/auto_int8_check.c` (run by tests/run_tests.sh) walks TP[] and every
engine's EDIT descriptors (edit[] and the mode-dependent desc(); every engine built, PHYS ACID CZ included; ANALOG 2,
the track FILTER, CHORD+, COMP): **175 descriptors, none of the lockable or recordable ones outside -128..127** (no
track value at all is wider than a signed byte). So the 3-byte event holds every lock and every motion value; no id
needed a 2-byte form or had to stop being lockable. A 2.4 import's lock value past a signed byte (only a damaged or
foreign 2.4 project has one) is clamped and the import says *2.4 IMPORTED, LOCK CLAMPED*.

**What was built** (firmware/src):
- `seq/auto.h` (header only, shared with the storage, the editor and the tests): the event and the list, the pseudo-
  parameters past every build's stored ids (**NUDGE 253** -32..31, **FILL 254** 1 fill only / 2 no fill, **CHANCE 255**
  0..99 %; a default value is no event; always STEP-ONLY), the list operations, SLOOP 2.4's stepx form both ways
  (`auto_from_stepx`, `auto_to_stepx`: what it cannot hold, hold events, chance, locks past 24, is said), and the stored
  forms: a track's list (n, n x 3: a pattern record's chunk) and a store's (`AUTO_ENC_TAG` 0xFF, version 1, the PLAY
  bits, four lists: the extras record's new form).
- `seq/auto.c` (FELUCCA_AUTO = MOTION or SL24_XSTEP or CHANCE): `auto_w` (four lists and motion's PLAY bits), the old
  motion form as a type (`motion_store_t`, for the MOTN record and the SEC_MOT chunk), the stored-id map (`mot_id`,
  `mot_sid`, moved from motion.c: locks are stored with the same build-independent ids as motion now; they are equal
  to our ids for every lockable value), **`auto_step(t, idx)`**: one scan of the track's list a step: hold events
  apply in list order (motion.c `motion_hold`, PLAY on), the step-only ones are gathered (FILL, CHANCE, up to 24 value
  events); the fill condition decides the step (FELUCCA_FILLS: a skipped step's hold events applied, its step-only ones
  not); seq24.c `lock_apply` takes the gathered locks over the in-force ones (the base a lock saves is what the hold
  events left). It returns whether the step plays; seq.c calls it once a step in both `seq_tick`s, where
  `motion_step` and `lock_step` were. The edits for the UIs and the editor (`step_micro(_set)`, `step_fill(_set)`,
  `step_chance_ev` / `_set`, `lock_get` / `lock_set` / `lock_drop`, `hold_get`, `auto_step_clear`, `auto_only_clear`,
  `auto_kind_toggle`).
- **Micro timing**: `micro_units` is read every block, ahead of the step (as before), so it reads **`auto_nudge`**, a
  64-byte index of each track's nudges rebuilt from the list whenever the list changes (`auto_touch`: the edits, a
  load, a pattern switch in the ISR, undo), never per sample. The render with nudges, fills, locks, motion and chance
  bits is **sample for sample 2907501's** (`tests/auto_render.c`, the same program built against 2907501's sources:
  hash 0x60AF78F6 both).
- **Chance**: `chance.c` reads the step's CHANCE event (`auto_ch`) first, then a synth step's bits as before (a step
  with neither draws no random number); **drum steps get chance**: a drum step whose event says no plays nothing,
  its ratchets neither (`chance_drum_drop`). The SLOOP UI keeps writing the bits; the Optimist UI writes events only.
- `seq/motion.c` on the list (the hold events): recording with REC + a knob writes them as before, and **a recording
  pass's motion is now in the pass's undo level**.
- `seq/undo.c`: a mark copies the track's list; a record keeps it when it changed (header bit 4); `UNDO_MIN` 1,152 B
  (build.py checks the same).
- **Storage** (`storage/auto_proj.c`, replacing motion_proj.c and stepx_proj.c: one store a project buffer):
  - **today's forms whenever they hold the store exactly**: its hold events in motion's form (the MOTN record beside a
    project or the autosave, `motion_flash.c`; a section's SEC_MOT chunk, `sec_codec.c`: at most 64 for the four
    tracks, recordable values), its step-only events in 2.4's stepx form (the extras record, `stepx_log.c`; a pattern
    record's PF_SX chunk: no chance, at most 24 locks a track). A project with only what 2907501 could hold is stored
    **byte for byte** as 2907501 stored it, and every older firmware reads it whole.
  - otherwise the new forms: **a pattern record V2** (`PF_V2` 0x40 in the version bits; its motion chunk is the list,
    n up to 128, no PF_SX), and **the extras record in its new form** (the whole store; for a section and the autosave,
    log ids 88..104; the MOTN record / SEC_MOT chunk still carry the first 64 hold events for older readers).
  - **2026-10-15 (perf/automation-merge; the Formats ruling: no compatibility duty to other firmwares or older
    builds)**: the old forms are now only READ (an older Optimist's projects load and convert), never written. With the
    section log a store is always written in the new forms: the extras record's AUTO_ENC_TAG form, a pattern record V2
    for a track with automation; no MOTN record, no SEC_MOT chunk, no PF_SX, no 2.4 stepx extras (the one exception: the
    first start's move of an old slot into its section writes its motion as the chunk, `sec_encode_mig`). The four-slot
    builds (SECTIONS 4, no log) still write the MOTN record: it is their only automation storage. A capture while a
    p-lock is in force now stores the patch under the lock (`auto_patch`), not the lock's value.
  - **One list, one base (2026-10-15, user ruling)**: `seq/auto_base.c` `auto_param` is the one list of values an
    event may set, hold or step-only: everything lockable is recordable and the other way round (locks gain the FX
    insert amounts, DUST, COMP, STRUM/VLEAD; motion gains ARP, scale, CHORD, GATE, LFO WAVE, the trim, SLICER's values,
    the drum kit and EDIT values). Out of both: VOICE GLMODE PRIO ALLOC VOICES (voices in flight), MUTE, FX OFF, LEN
    DIV SWING, the LFO DEST slots, and values with no stored id (LFO 2, GATER PATTERN RATE SHAPE, JUNO MODE). One base
    (`motion_base` / `motion_active`) under holds and locks; a lock lets go to that base, or to the hold value set under
    it; PLAY, STOP, a pass's step 0 (motion playing), a load, a pattern switch and an engine change put every automated
    value back. A preset, MIDI or editor change made while playing on an automated value is put back at STOP (motion
    and locks alike); a knob turned while not recording is the new patch at once (`auto_knob_base`).
    The two bullets above describe what was written before.
  - read: the motion form first (`auto_fresh`: the buffer's store emptied, then its hold events), then the extras record
    (the old form adds its step-only events, the new form is the store). autosave_resume reads the MOTN record before
    the extras record now (it read them the other way).
  - snapshots and the editor's backup carry the records as they are (the extras record's bytes, either form);
    `persist_flush_now` (an update, UPDATE MODE) writes the autosave's extras and FX records too, as autosave_tick does.
- **SLOOP 2.4**: the import's extras become step-only events (`sl24_auto_in`); the export (`sl24_auto_out`, editor 78)
  rebuilds micro, fill and the first 24 step-only value events a track as locks; hold events and chance are not
  exported and the export says so (`SX24_MOTION` 8192, which `SX24_CHANCE` shares: the 14 bits of the reply are full past the FX slots' 4096; the editor: *the motion or the steps' chance
  (motion not in 2.4)*; more than 24 locks: `SX24_LOCK`).
- **The editor protocol v11** (v10 is the FX slots, 86 FX and 87 FX_PUSH; v11 = v10 plus the pair) (`io/editor/ed_stepx.c`, web/EDITOR_PROTOCOL.md): **92 AUTO_GET** (a track's list in
  pages of 64) and **93 AUTO_SET** (SET, DEL, CLEAR holds / step-only / all, PLAY), 85 left to the patterns' push the
  document keeps; 72..77 still answer, on the list. web/editor.html: the commands, the mock and tests; pattern
  records V2 parsed and written back as they came. **The editor's step detail still uses 72..77** (it shows locks,
  nudges and fills, not hold events or chance): switching it to AUTO_GET / AUTO_SET is a follow-up.
- **The Optimist UI (STEP)**: with a step held and a page button, a cell shows the step's event of either kind (a
  padlock: step-only; an arrow, amber: a hold event); a turn writes a step-only event (or the hold event's value when
  only a hold is there); **YES toggles HOLD** of the hot cell's event on every step held (*HOLD: UNTIL THE NEXT* /
  *THIS STEP ONLY*; a value motion does not record, GATE for one: *NO HOLD HERE*); **SHIFT + the knob clears both
  kinds**; YES without a lock page (or with no event there) is the fill condition as before. PRESETS is the chance on
  **drum and synth steps alike**, an event. **The marks under the steps**: a 3-px row between the grid and the
  playhead strip, a dot under each step with an event (the drum track: the selected lane's colour; a synth track: its
  colour), a hold event's tail on to the next hold event of its parameter or to LEN. SHIFT + a step clears its notes and
  every event of it, hold events included; a tapped step that ends empty loses its step-only events, its hold events
  stay (the motion is the pattern's). REC + a knob while playing records hold events (op_cells.c's motion hook, as
  before).
- **The SLOOP UI (UI=0)**: its lock (PRESETS / ALGORITHM), nudge (KNOB 4), fill (OCT+), clear (OCT-) and motion
  gestures write the list; what it shows is unchanged, but for SEQ > MOTION's FREE, now the track's 128 less its
  events (it was the 64 shared by the four tracks). Its tests unchanged in what they check (ui_pages_test with the
  2.4 sequencer switches, the backports, Felucca 1.0.2's motion mark).

**Tests** [M]: `tests/auto_test.c` (new; every switch of the store, the section log, the patterns): 128 events and the
129th refused (no lock, no motion, no nudge); both kinds on PAN on one track (*0 20 20 -30 20 10 40 40*, the next pass
0: the hold from step 2, a lock on step 4 lets go to the hold's value, a hold and a lock on step 6: the lock wins
there, then the hold), STOP; a fill-skipped step (its hold applied, its lock not, no note) and the same step in a fill;
drum chance (0 % never, 50 % about half: 46 of 64 here, none: every pass) and a synth step's event over its bits; an old
MOTN record read and written back; an old extras record read and **written back byte for byte**; an old V1 pattern
record read and **written back byte for byte**; V2 for a chance, 65 hold events, 25 locks, a hold of GATE, and 128
events (n 128, within PAT_REC_MAX), each read back equal and refused by the V1 check; the new extras form read back
and refused by stepx's decoder; a scene with 20 holds, 30 locks, a nudge, a fill and a chance a track saved and loaded;
the autosave (MOTN + the new extras record) read back; the 2.4 export / import round trip (nudges, fills and the
first 24 locks survive; motion, chance, locks past 24, FX OFF reported or dropped) and a clamped lock; undo / redo of
a lock, a nudge and a hold event byte for byte; AUTO_GET / AUTO_SET and LOCK_SET; the HOLD toggle (and none for
GATE). `tests/auto_render.c`: the render above, and its `cpu` mode (below). `tests/ui_optimist_test.c`: a fifth set
with MOTION (`ui_optimist_auto_test`): YES toggles HOLD (the arrow, PLAY on), again step-only (the padlock), the marks
under the steps by pixel (the dot, the tail, none where no event), a tapped step keeps its hold event, SHIFT + the step
clears it, drum chance 70 % by PRESETS. The tests that read motion's or stepx's internals read the list now
(`tests/auto_view.h` shows it as motion's old store). **All of tests/run_tests.sh green** (user-default linked first),
`tests/builder_test.py` green, `node web/test_web.mjs` green.

**CPU** [M, host instructions counted by the kernel as tests/regress.c, `auto_render cpu`, the same program against
2907501]: a step entered with no event: **86** instructions (2907501's motion_step + lock_step: 249); a step with 24
locks and 30 hold events: **3,676** (2907501: 4,753); a full list of 128 events on one step: **5,012**. Budget 6,000 a
step (1/32 at 300 BPM on four tracks: under 0.5 % of 240 MHz); run_tests checks it. RAMTEXT unchanged (auto_step is
not RAM code, as motion_step and lock_step were not).

**Emulator** [M] (fm1-emulator `play_check` feat/upstream-merge, headless, no flash state, 96 MHz; user-default
packages built here and from 2907501): a free take of five notes closed into a loop, then played 4 s, recorded
sample by sample; and the same with motion recorded on it (FX page, REC, KNOB 2 turned five times while it plays).
**Without the store's switches the two sessions' audio is identical to 2907501's** (MD5 of the WAVs equal); with the
switches, 2907501's own build with and without them differs from itself in this session too (the free take's notes
land where the main loop sees the keys, which the code's layout moves), so the like-for-like render check is the host
one above, and the emulator shows the switched build playing the recorded motion without a fault. Its profile
(`FM1_HOT`, the 4 s with motion): `auto_step` is inlined into the sequencer's block code (no symbol of its own; 2907501's
`lock_step` showed 0.01 %); `mix_block` + `events_block` 8.7 % of the instructions here against 10.6 % there.

**Sizes** [M] (`tools/optimist.py build --profile user-default --measure`; 2907501 built from `git archive` in a
scratch tree; "automation" = `--set MOTION=1 PLOCK=1 MICRO=1 FILLS=1 CHANCE=1 SL24_XSTEP=1`):

| user-default | flash | RAM | pool | RAMTEXT |
|---|---|---|---|---|
| 2907501, UI=0 | 488,788 | 79,192 | 307,376 | 30,872 |
| this tree, UI=0 | **494,900** (+6,112) | 81,196 (+2,004) | 307,376 | 31,144 (+272) |
| 2907501, UI=1 | 478,260 | 86,552 | 307,376 | 31,156 |
| this tree, UI=1 | **487,176** (+8,916) | 88,588 (+2,036) | 307,376 | 31,144 (-12) |
| 2907501, UI=0 + automation | 501,236 | 81,128 | 313,040 | 30,880 |
| this tree, UI=0 + automation | **510,616** (+9,380) | **84,236** (+3,108) | **316,428** (+3,388) | 31,232 (+352) |
| 2907501, UI=1 + automation | 491,880 | 88,456 | 313,040 | 31,152 |
| this tree, UI=1 + automation | **502,648** (+10,768) | **91,580** (+3,124) | **316,428** (+3,388) | 31,164 (+12) |

The "this tree" rows were re-measured on tree `2a1b1945ce61f30725effcc7e2699783869e18a9` (commit 1a487a1, `feat/ui-batch26`, with
the follow-up commit's firmware change, a warning fix in fx.c that does not change a size), the same configurations, the
deltas against the 2907501 rows. The tree has the other work merged since (the FX slots, the Optimist UI, ...), so the
deltas are no longer the store's alone: the store's own cost here is the "+ automation" row less the plain one of the
same UI (UI=0: +15,716 flash, +3,040 RAM, +9,052 pool; UI=1: +15,472, +2,992, +9,052).

Every configuration fits (the slot is 581,564 B); no sample set was left out. RAM: the working lists (1,548 B) and the
nudge index (256) and undo's copy (385) for motion's 200 and stepx's 704 + 176; pool: four buffer stores of 1,548 B
(motion's four of 200 and stepx's four of 708 before) [M by the struct sizes; the totals above measured]. Section 6.3
estimated +630 B of RAM: measured +1,088 against 2907501 when the store was built (on 2907501's own tree), and the pool's +3,388 it did not count.

**Not built** (or not asked): the editor's step detail on AUTO_GET / AUTO_SET (the protocol, its mock and tests are
there); chance per drum lane (an event is the step's, every lane together); FINE tempo in 0.1 BPM (not part of this
phase; nothing of it fell out of it); the per-switch costs in docs/BUILDER.md were measured before the store (re-measure
with tools/builder/measure_costs.py); user-default still builds none of the store's switches (the Optimist UI's lock
gesture needs PLOCK, its HOLD MOTION).

**Decisions open for review** (how to undo each):

| Question | Chosen | Undo |
|---|---|---|
| The stored form | today's forms whenever they hold the store exactly (byte for byte as 2907501, every older firmware reads it), the new forms (V2, the extras record's new form) only for what they cannot hold; section 6.3 wanted the list always in the motion chunk and the extras chunk empty | `auto_trk_old_ok` / `auto_old_ok` returning 0 |
| An older PATTERNS firmware and a V2 record | refuses it (its version): the pattern reads as missing there, counted (MISSING), kept in the log. An older reader refused a chunk of more than 64 anyway, so section 6.3's "reads a lock as motion" could not hold | `pat_encode` |
| The autosave's list | the MOTN record keeps the first 64 hold events (its 224 B can hold no more), the extras record (id 104) the whole store in the new form when needed | `motion_flash.c`, `stepx_log.c` |
| FELUCCA_SECTIONS 4 | no log: the step-only events stay in RAM as stepx's did, the MOTN record keeps the first 64 hold events | `auto_proj.c` |
| The nudge for micro timing | a 64-B index a track rebuilt at every change of the list (`auto_nudge`), read as `micro[]` was; section 6.1 wanted it read in the step's scan, but `micro_units` is read every block, before the step | `auto_touch`, `micro_units` |
| Locks in force | at most 24 a track at once (the in-force table as before); a step with more step-only value events applies the first 24 | `NLOCK` in `lock_apply` |
| Hold events and PLAY | a track's hold events play only with its PLAY bit (SEQ > MOTION), its step-only events always (as locks did) | `auto_step` |
| Chance on drums | an event of the step: every lane together, rolled once (a lane's own chance needs 16 pseudo-parameters) | `chance_drum_drop` |
| Motion and undo | a recording pass's hold events are in its undo level (they were not undoable) | `motion_knob`'s `undo_mark` |
| A turn on a cell with only a hold event on the step | changes the hold's value (adds no lock) | `lock_turn` |
| SHIFT + the knob | clears the step's events of that cell, both kinds | `lock_turn` |
| YES with a step held | HOLD of the hot cell's event when the lock page shows one, else the fill condition (FELUCCA_FILLS) | `held_yes` |
| HOLD for a value motion does not record (GATE, SLICER, ARP ...) | refused, *NO HOLD HERE* | `auto_kind_toggle` |
| A tapped step that ends empty | loses its step-only events, keeps its hold events; SHIFT + the step loses both | `step_wipe` |
| The marks' row | 3 px under the grid; the playhead strip 4 px lower; CARDS 2x2: the two held-step lines 12 px apart | op_stepdraw.c `SG_MK_Y` |
| SEQ > MOTION's FREE (SLOOP UI) | the track's 128 less its events (locks and nudges count) | ui_draw.c |
| The editor's commands | 92 / 93 (85 kept by the document for the patterns' push; 86, 87 are main's FX); v11 asked, INFO unchanged | ed_stepx.c |
| The pseudo-parameters' ids | 253 254 255 (past every stored id: MOT_TAIL_END is P_ENG_END + 4) | auto.h |
| persist_flush_now | writes the autosave's extras and FX records with the project (it wrote the project and its motion only) | project.c |

**Found in the spec** (section 6; not changed in the spec text, the reading taken above): 6.3 says the motion chunk *is*
the list and PF_SX is written empty: kept for what the old forms cannot hold only (downgrade safety). 6.3's "a phase
1..3 PATTERNS firmware reading a new record reads a lock as a motion event": such a firmware refuses a chunk past 64
and any version it does not know, so V2 records read as missing there instead. 6.3's "motion_proj's MOTN record the
same way": the MOTN record has 224 B; the list goes to the extras record. 6.1's dot "in the lane's colour": the
events are the track's, not a lane's; the drum track's dots take the selected lane's colour. 6.2's "one list scan a
step": one scan, then the gathered locks (at most 24) against the in-force ones; the nudges from an index.

## 12. Rulings of 2026-10-10 (batch 33)

**The mixer's rows name their sound.** The user: "we need each track to show the synth name and the preset, so the vu
meter and pattern lane must be thinner". A row (41 px) is now: the colour stripe, the code (T1, DR, a lane's BD ...) with
its M / S badge, then three lines from x 28: the text line (the engine's name in the row's colour, then the preset,
the kit for DR and its lanes, a user preset's name; each cut to the room), the step strip (6 px cells, a page bar under it
when LEN > 16) and, at the foot, a 3 px VU bar with the compressor's reduction in amber. Names come from `snd_name` and
`ENGINES[]`, the helpers the cards and the header use.

**The SOUND pages fill the panel.** A page with a picture (ENV, LFO, FX, FM6's algorithm, TEMPO, the session grid, the
SOUND row's presets) gives the picture all the height its few rows leave: the rows sit at the panel's foot, `gr_h`
(op_graph.c, `GRAPH_H`) is set each time `draw_list` paints and every drawer is correct from `GRAPH_MIN` (58, 40 with
CARDS 2x2) to the whole panel; in 2x2 the picture has the whole panel and no rows (2026-10-12, section 3). A list that scrolls keeps `GRAPH_MIN`. A page with no picture (ARP, ...) and rows to spare
takes a taller row pitch (panel / rows), its text and gauge together in the middle of the row, the gauge thicker.
(`draw_list` also no longer shifts a short list's window: the first row stays in view.)

**PRESETS = browse presets only.** The user: "i dont like that presets knobs act as select everywhere. remove it for
now." PRESETS changes the selected track's sound (`op_preset_step`, the drum track's `drum_kit_step`) on every screen
and layer, with the sound's toast, and does nothing under a question, the REC ring, the NAME screen or on MASTER. Gone:
the hot cell's value (op_input.c), a layer's hot knob (op_layers.c), the NAME screen's character (op_name.c) and the
held step's CHANCE (op_step.c; `edit_chance` removed). **The chance has no knob any more**: a held step's cards are NOTE
LEVEL RATCHET LENGTH (synth) or LEVEL RATCHET (drums), none is the chance, so a chance is not editable from this UI until
it gets a cell or a button (it is still read: the step's line shows it, the store and SLOOP 2.4 imports keep it). SONG's
scene references are edited with KNOB 1..4.

**ALGORITHM reaches MASTER.** Turned left past T1, ALGORITHM selects MASTER on every SOUND page and every layer about the
track; turned right it goes back to T1 where it was; it stops at MASTER. MASTER is a UI state, never a track in the
song data (`song.sel` stays T1). On SOUND, MASTER is the FX screen (SCR_FX: the delay, reverb, master compressor, dust,
duck, filter: the one place the master FX rows are defined, shared with the mixer's MASTER row), the family and row left
(`ui.m_fam`, `ui.m_row`) restored on the way back; its header badge reads MASTER in C_HI. In a layer (`ui.master`: FX,
ARP, SCL, EDIT, FM6's operators) the four cards become the master FX: FILTER DUST DUCK COMP, drawn with the knob colours;
the layers' keys that act on T1's data (erase, repeat, FM6 keys) are off while MASTER is selected. A page with no master
equivalent shows the same master FX rows. **STEP and the pattern layers stop at T1**: the automation store keeps one
list of events per track (seq/auto.h) and has no master target, so MASTER is not reachable there even with the store
built in. The mixer keeps MASTER above T1 as before.

**The held step's card pages** (follow-up, 2026-10-10: "on held step we can record plock and select allow then to page
between pages of plock"). With a step held on STEP, SELECT pages the cards, stopping at the first and the last page;
KNOB 1..4 edit the page's values. Page 1: the step (NOTE LEVEL RATCHET LENGTH; drums LEVEL RATCHET). Page 2, the extras,
as built: CHANCE, NUDGE, FILL (an event of the automation store each; none built, no page). Then the track's sound
pages, the SOUND screen's rows in order (a drum lane's own pages left out): a turn writes the step's p-lock, a hold event
there takes the value, SHIFT + the knob clears it (FELUCCA_PLOCK; none: no pages). A page button with a step held still
jumps to its family's page. The header reads *Step 3/22 ENV des*. HOME tapped on a held step clears everything,
motion included. The page counts (a build with PLOCK MICRO FILLS CHANCE): ANALOG 22, every other engine 17 (FM6 too); the
user-default build has none of those switches, so its held step has the one page.

**ARP draws its pattern** (the same day): ARP's graph is the arpeggiator over a C major chord, OCT octaves, a mark a step of
RATE, GATE long, every other step SWG late, the notes by MODE, a step PROB would skip dim; OFF the chord held. It fills
the height the rows leave, as ENV's envelope does, the rows at ROW_H. Remaining screens with no picture and few rows (PROJECT, checked; any other list that fits the panel) keep a row pitch capped at 40 px, at the top of the panel.

## 13. Requests of 2026-10-11 (batch 35: the mixer)

**PRESETS repaints in place.** The user: "the screen flashes in mixer when i select drum lane presets". PRESETS on the
drum track steps the kit (`drum_kit_step`) and on a synth the preset (`op_preset_go`); both used to set `ui.force`,
which clears the whole LCD to black before the bands come back one blit at a time (and the user-preset and native-bank
loads of the core ask for it too). Both now end in `op_preset_shown`: `ui.force` back to what it was before the load
(a clear asked already stays), the rows fixed (another engine may have other rows) and `op_repaint`, every band drawn
again over itself, as a track change has done since batch 34. Measured on the emulator (play_check, 96 MHz, a capture
every 2 ms for 50 ms after each detent, counting pixels drawn before and after but black in between): PRESETS on the
mixer's kick lane, 3 detents, worst capture 14,862 blanked pixels before, 0 after; on T1, 2 detents, 13,413 before,
937 after (the toast's frame redrawn over the row, not a clear).

**The mixer's knob sets in one order.** The user: the sets in a logical order, the same on every row: the mix first, then
the insert effects, then the send effects, then the rest. Each row's items now carry their group (`mitem_t.grp`: MG_MIX,
MG_INS, MG_SEND, MG_LAST) and `mx_items` starts each group on a set of its own, padding the set before with empty cells
as long as what follows still fits the 20 cells (MX_MAXI); the header names the set by its first item's group (*T1 mix*,
*T1 insert*, *T1 sends*, *T1 sound*; DR's last *Kit*; MASTER *Mix master*, *Mix comp*).

| Row | Mix | Inserts | Sends | Last |
|---|---|---|---|---|
| T1 T2 T3 | VOLUME PAN | S1 TYPE, S1's amount, S2 TYPE, S2's amount (section 20) | REV DLY CHO, each in a slot | FX ON, SOUND |
| DR | VOLUME (GLO > DRUMS) | the same, the drum bus's slots | its send levels REV DLY CHO | FX ON, KIT |
| a lane | LEVEL CUT | DRIVE DST CMP | REV DLY CHO, each in a slot | SOUND |
| MASTER | FILT DUST DUCK | THRS RATIO GAIN CEIL (FELUCCA_MASTER_COMP) | | |

Since the track insert slots (section 20) the inserts are the row's own two slots, in their order; the master slots hold
the sends only. Gone:
INSERT and SEND as "the first insert / send in the slots" (`mx_insert`, `mx_send`).

The master FX's SLOTS cards (S1..S4) name each slot's role in the unit's place, **INS** or **SEND**, from its type
(DST FILT COMP an insert, CHO DLY REV a send, an empty slot nothing), never from its place (2026-10-12, the user: "on
edit master i have slot 1, 2, 3, 4 for the effects, but slot 1 is insert, right ? it is not clear in the ui";
`cell_param`). The mixer's insert and send sets already name the effects (DST, Filter; REV DLY CHO).

**The mixer in three views.** The user: the mixer shows one group at a time, MASTER alone, or the four channels (T1 T2
T3 and DR, the drum bus as a whole), or the drum sounds, and the view follows ALGORITHM. ALGORITHM walks the rows as
before (MASTER, T1 .. DR, the 16 lanes, stopping at the ends); the panel shows only the cursor's group (`mx_view`:
`mxd.first` .. `mxd.last`), redrawn in place by the panel's `cv_tall` when the group changes (no clear to black: the
host test counts full-screen fills, and the emulator measurement of section 13's PRESETS applies to the walk too).
`mx_rect` gives each row its place, steps and meter in its view; the live parts (meters, steps) are canvases of their
own per row as before.

| View | Rows | Each row |
|---|---|---|
| MASTER | the master alone, 240 x 164 | its name; a tall VU meter (14 px, 224 px wide) over a dB scale (a tick each 6 dB, 0 -12 -24 -48 named); the compressor's reduction as an amber bar from the right with its dB ("off" while THRS and CEIL are 0); ~~then the four channels' meters (T1 T2 T3 DR, each in its colour)~~ then **the FX returns' meters, REV DLY CHO** (those built, in C_HI; 2026-10-12, the user: the four channels' meters were "useless", the channels' view shows them): what the effects give back to the master |
| CHANNELS | T1 T2 T3 DR, 240 x 41 each | as before (section 12): the code and M / S badge, the engine and the preset, the 16 steps of the page playing with the page bars, the 3 px VU with the compressor's reduction |
| SOUNDS | the 16 lanes, two columns of eight (1-8 left, 9-16 right), 120 x 20 each | the colour chip and code (BD SD CH ...), the lane's own hits on the page playing (6 px a step; an empty step a foot line, the playhead white), its hit meter (drums.hits, falling over 12 frames). The selected lane is framed and tinted; the header names it (*Snare mix*) |

The levels are kept on a 0 .. 255 scale (`MX_LV`) and drawn to each view's meter width.

**The track's name on a track change.** The user: when the selected track changes (ALGORITHM or any other way), the
title bar shows briefly the full track name, engine and preset, the three synth tracks only, on every page. `draw_head`
watches `song.sel` once a frame (`tflash_tick`): a change to T1, T2 or T3 starts ~1.5 s (`OP_TFLASH_FRAMES`, 90
frames) in which the header reads the badge *TRACK 2* in the track's colour, then the engine in that colour and the
preset in white (sentence case, cut to the room), with no position or tempo; then the normal header comes back. The
drum track, MASTER (`ui.master`) and power-on show none; a message (`ui.msg_t`) and the NAME screen keep the header.
It is drawn as the header always is, when its signature changes: in place, never a clear.
## 14. Button LEDs: what can be pressed is half lit (batch 35, feat/ui-b35-led)

The user, 2026-10-11: "the buttons that are available should be half lit (for instance no LFO on a drum lane)".
**Three levels.** Full: the active button, as before (the screen's button, the layer shown, SAVE blinking on a question,
PLAY on the beat, REC armed or recording, OCT while ghost / hard or the octave is shifted). Half: a button that does
something now. Dark: one that does nothing. The half light is the HAL's glow layer (`fm1_led_dim`, hal/fm1_input.h: the
landmark glow SLOOP's UI lights on the drum track's keys; one frame in four, or the GLOW pulse with LIGHTS), which sits
under the full light, so a blinking button swings between full and half. The LIGHTS backlight stays under both.

**What counts as available** (`op_btn_avail`, op_input.c, the conditions ui_input uses):

| Button | Half lit when |
|---|---|
| HOME, SAVE, PLAY | always (NAME: the only buttons with a job besides the keys) |
| REC, OCT-, OCT+, GLO, SEQ | always (NAME: dark) |
| ENV, LFO | the selected track has SOUND rows of that family (`sound_page`, the rows `op_jump` opens); ENV also on an FM6 track (the operators) |
| FX, SCL, EDIT, ARP | the track has rows of the family, or the button's held layer works (FX punch, SCL key, EDIT erase, ARP repeat: `ly_bit`, off on MASTER's EDIT and ARP) |
| FX on the global FX screen | its next page |
| any page button, STEP with a step held | the step's page lock (all but EDIT and GLO; EDIT too on the drum track: the lane's SOUND pages, section 18) |

**Walked 2026-10-13 (fix/button-leds; tests/ui_optimist_btnled.h):** every screen x track kind (synth, DR with no lane, a lane, MASTER) x button (not PLAY, REC, OCT) is tapped and held, and its LED must be lit exactly when the tap changes the screen or the hold works a layer. **Rulings that followed (the user, 2026-10-13):** (1) *the lane's pages need a picked lane*: on DR with no lane picked ENV does nothing, like LFO, and its LED is dark (`sound_page` hides the drum ENV row without `lane_picked()`); SHIFT + a key picks a lane and ENV and LFO light dim. EDIT stays (the kit's SOUND rows), SCL and ARP stay half lit on DR (hold-only layers: the song's scale, rolls). (2) *one dim level*: `FM1_LED_DIM_MASK` (hal/fm1_input.h) is the dim of every LED, buttons and key landmarks alike (one layer, `fm1_led_dim`): lit one scan frame in 8 (about 12%) in UI=1 builds, was one in 4 (read as lit); SLOOP's UI keeps 4. With menu LIGHTS the dim layer is the glow pulse `FM1_GLOW_NS` (about 4%), and the LIGHTS level scales the backlight only, so the dim does not follow it. (3) *the mixer's MASTER row*: holding SCL, EDIT or ARP shows no layer map (`lay_frame`: their `ly_bit` is 0).

So on the drum track LFO and ENV are dark on DR itself and half lit with a lane selected (the lane's LFO, section 18, and envelope, section 4.3); EDIT, FX, SCL, GLO, SEQ stay
half lit. **Judgment call:** LFO's held layer (the patterns, FELUCCA_PATTERNS) does not count, so LFO is dark on the drum
track as the user asked, though LFO held there would still launch a pattern; the hold combinations (SAVE + page button
saves a preset, SHIFT + it locks the layer) are not counted either: a tap decides. On the user-default builds OCT-
and OCT+ are never dark (a tap always shifts, or is ghost / hard on the drums).

**Writes.** The levels are rebuilt each pass with the key LEDs and copied into `fm1_led` / `fm1_led_dim` only when a
byte differs, so nothing is written while nothing changes; a track, lane, page or engine change is seen on the next pass.
The code is in op_input.c next to `ui_leds`, not in HOT / ramtext. SLOOP's UI (ui/sloop/ui_input.c) lights buttons full
only (the layer, PLAY, REC, OCT) and uses `fm1_led_dim` for the keys' landmarks; it shares none of this code and is
unchanged.

## 15. SLOOP backports (feat/sloop-backports, 2026-10-11)

Fixes ported from upstream SLOOP (isod89/sloop-fm1, GPL-3.0-only). Most are the SLOOP UI's (docs/SLOOP-UI-CHANGES.md);
what reaches this UI:

- **A held or latched arp follows TRN, ROOT, SCALE and QNT** (SLOOP #191, 2.6 d30d655, after Felucca 1.5). The arp keeps
  the keys played (seq.c `held_key`, `held_dt`) and maps them again on every step through `kb_pitch`, so a latched chord
  moves with TRN or a new scale. Each key keeps the octave OCT had when it went down; a key a new QNT silences drops
  out; MIDI in notes play as they came. Shared sequencer code: both UIs. Test: tests/arp_follow_test.c.
- **SEQ never lands on SONG here** (SLOOP #33 / #107). The user's ruling ("Follow SLOOP in the SLOOP UI") keeps SLOOP's
  convention in the SLOOP UI only, where SONG is the SEQ family's last page. In this UI SEQ tapped is STEP, also after
  the SONG screen was visited (SAVE held + SELECT). Unchanged; tests/ui_optimist_test.c `seq_never_song_tests` pins it.
- **Chord mode on STEP** (SLOOP #41) was the SLOOP UI's bug: here the step pick is the chord as it sounds (seq.c's pen),
  so a tapped step takes the whole chord. Unchanged; tests/ui_optimist_test.c checks it.
- **LFO RATE with tempo sync** (feat/lfo-rate-ui). RATE runs -10 .. 127 (-1 .. -10 are the divisions 4BAR .. 1/32). A free
  rate is the bar from 0 as before (`cell_gauge_d` in op_cells.c, also the step lock cells); a synced value is the
  list form, one of the ten divisions lit, 4BAR at the left. The text comes from the core. The LFO graph reads no rate, so
  it does not change with it. Test: tests/ui_optimist_lforate.h.

## 16. Requests of 2026-10-12: one ALGORITHM walk, a lane's PRESETS, the ARP picture, the mixer's MUTE (feat/algo-lanes)

The user: *"the algo button should work the same on all pages, master (if it makes sense), t1, t2,t3,t4 and then
individual drum lanes (if it makes sense)."*, *"the preset on a drum lane should change the drum lane preset, not the
kit."*, then *"SLOT: kit, or specific instrument. if Slot=kit. preset knob changes the preset kit, if Slot=something else
preset changes the preset for the drum lane."*, *"arp is the same as lfo/env. no list"* and *"in the mixer page add a
card for muting the track, first card."*

**The walk** (op_walk.c `algo_walk`, the one function every page and layer calls). The user, after the first rulings:
*"really the algo knob should select the track and drum sub track every where. step seq, edit, env, etc."*, then *"i
want uniformity"*. One order, the mixer's rows, **identical on every page and layer: MASTER, T1, T2, T3, DR ("T4"),
then the 16 lanes, stopping at both ends.** The only pages that do not take it are those that ate it before: NAME, a
held step on STEP and a free take. SHIFT + a drum key still picks a lane straight away, on every page.

| Where | MASTER is | A lane is | What changed |
|---|---|---|---|
| The mixer | its MASTER row | its lane row | nothing in the order; a lane row is now "a lane selected" (below) |
| SOUND, every family | the master FX screen (SCR_FX), as `fx_algo` did from T1 | the drum track's pages for that lane (SOUND, SOUND 2, 3, SOURCE on a user-sample lane, KIT, ENV); a family the drum track has none of (LFO, ARP, SCL) shows every drum row (`snd_family`'s fallback) | MASTER from any track (it was T1 only); on past DR into the lanes |
| STEP | the master FX screen; right comes back to STEP on its page | the drum view: the lane's steps on the grid; on a lane page the page follows the lane; back to DR from a lane page: PATTERN | it went T1 .. DR |
| PROJECT, SYSTEM, SONG, SCOPE, GLOBAL | the master FX screen; right comes back to the page and row (the TEMPO page, PLAY held: it closes, right comes back to the screen under it) | the lane selected (the header's badge), nothing per lane on these pages | it went T1 .. DR |
| The master FX screen itself | it is MASTER | - | right goes back to the page and row it was left from (SOUND T1 when it was opened by FX on the mixer's MASTER row), and on from there |
| A layer (FX, ARP, SCL, EDIT, FM6's ENV, GLO, SAVE) | ui.master: the FX ARP SCL EDIT and FM6 ENV layers show the master FX's four cards (FILTER DUST DUCK COMP); GLO's levels and SAVE's song shortcut keep their own cards (they are not a track's) | the lane selected (the layers have nothing per lane: the FX layer's FILTER is DR's) | the walk went T1 .. DR with MASTER in FX ARP SCL EDIT FM6 |
| NAME, a held step, a free take | eaten | eaten | unchanged |

On MASTER, on every page, **the header's badge reads MASTER and the track buttons (ENV LFO EDIT ARP SCL SEQ) are dark
and do nothing**, as on the mixer's MASTER row (op_mixer.c `master_sel`); FX stays lit (the master FX); PRESETS does
nothing.

**Selected: DR itself or a lane.** `ui.lane` says the drum track's selection is its lane `lane_selected()`, not DR;
`lane_picked()` reads it (on the mixer the cursor row is the selection). The walk onto a lane, the mixer's lane rows,
the pick (SHIFT + a key, STEP too) and a drum key played (section 17) set it; the walk onto DR or a track
clears it. (STEP's lane pages and the SOUND row's LANE knob set it too until section 17: SELECT and the knobs no
longer change the lane.) The header's badge shows the lane ("SNARE") in its sound's colour while a lane is selected.

**PRESETS on the drum track** (op_walk.c, op_input.c `op_presets_turn`), by the lane's SOURCE (SRC on the drum SOUND
row's knob 3):

| Selected | PRESETS | SHIFT + PRESETS | The toast |
|---|---|---|---|
| DR itself | the kit (as before) | the kits (as before, 64a4174) | "KIT name" in the kit kind's colour |
| a lane, SRC KIT | the kit, as on DR | the kits, as on DR | "KIT name" |
| a lane on an instrument (any other SRC) | that lane's sound only: the next / previous source of the SOURCE list (USR1..3, each kit's sound for this lane, the X0X voices), round, those this build has; KIT is left out, so PRESETS never turns the lane back to the kit (SRC, the SOUND row's knob 3, does) | the first sound of the next / previous kind, round: your samples (USR), a sampled kit's, a synthesised kit's, the X0X machines' (kits, styles and voices): the colour language's kinds (sloop/ui_colors.c `lane_col`) | "SNARE: X9 SD" in the lane's colour |

The kit and the other lanes stay. The mixer's lane rows follow the same rules; their SOUND card and the row's name show
the lane's sound (the kit's name while it plays KIT). The SOUND row's picture on the drum track: DR the kit big and
its style, a lane its name big and its sound.

**The ARP picture** (op_graph.c `graph_arp`), on ARP and ARP 2 (every ARP row shows it, no list): never blank. The notes
are those held (the latch's too), else a reference chord C4 E4 G4 (E C G with ORD PLAY), over OCT octaves, a guide line
at each pitch; the top line names MODE and RATE at the left and the notes at the right ("HELD C4 E4 ..."). The marks: a
step of RATE, GATE long, SWG late, by MODE, PROB's skips dim; MODE OFF draws the pattern UP would play, shaded, with
"OFF" on the top line. STEP's ARP card page keeps the roll under its cards (the cards are a 1.5 s overlay there).

**The mixer's MUTE card** (op_mixer.c). KNOB 1 of the first knob set is MUTE (OFF / ON, a turn switches it; YES did until section 17's D6), VOLUME and PAN
move to KNOB 2 and 3: on T1 T2 T3 and DR the track's mute (P_MUTE: GLO held + keys 1-4, the row's M badge), on a lane
the lane's own (drum_mix.c `dlm_mute`, SLOOP's kit pads; a muted lane's code is drawn in the M badge's amber), MASTER
none. One storage each, so the card, the M badge and the GLO layer's mute keys agree with every other way of
muting.

Tests: tests/ui_optimist_algo.h (the same walk on the mixer, SOUND's families, STEP, GLOBAL, PROJECT, SYSTEM, SONG,
SCOPE and the FX ARP SCL EDIT GLO layers; STEP's lane pages; PRESETS on DR, on a lane on KIT and on an instrument,
SHIFT + PRESETS on each, the mixer's lane row; SHIFT + a key;
the ARP picture off and held), tests/ui_optimist_mute.h.

## 17. Page organisation (the user's decisions, 2026-10-12; feat/page-org)

The user: *"also i dont know where to see the cpu... (we need to cleanup/organise pages a bit i think)"*. The map, the
problems (P1..P39) and the proposal were made first (optimist-work/pagemap); the user decided D1..D11. Where a decision
differs from the proposal's recommendation, the decision is what was built.

| | Decision | Built |
|---|---|---|
| D1 (revised by the user the same day; superseded by section 20: GLO switches GLOBAL / SYSTEM, SELECT and ALGORITHM stay within the screen) | GLO tapped keeps paging through one cycle: GLOBAL's rows, then SYSTEM's (CPU first), then GLOBAL's first; SELECT walks the same sequence. SHIFT on the mixer no longer opens SYSTEM | op_global.c `glo_step` / `glo_tap`, op_input.c `op_knobs` (SELECT stops at the cycle's two ends); GLOBAL's SYSTEM row gone (the cycle reaches SYSTEM); the badge reads GLOBAL or SYSTEM, the n/N counts within the screen ("CPU 1/7"), which keeps each list's numbers short and says when the screen changes; GLO on the BLE devices list closes it; `op_tap`: SHIFT alone on the mixer does nothing; the GLO LED full on both |
| D2 | CPU in SYSTEM only: its first row is CLOCK (MHz) + LOAD. No header warning | op_project.c `SYS`: CPU first, CLOCK then LOAD (read-outs) |
| D3 | the master FX is MASTER, 8 pages in the order INS TONE COMP LIMIT, RETURNS RET DELAY RET REV, SLOTS, MACRO (2026-10-14; first 7 pages: DELAY REVERB SLOTS TONE COMP LIMIT MACRO); its GLOBAL and DRUMS pages go | section 4.4; op_cells.c `fx_page`, op_screens.c `FX_ALIAS` / `FX_LIMIT`, op_state.c `SCR_NAME` |
| D4 / D5 | keep both: SYNC stays on TEMPO and MIDI; TUNE stays with the master | TUNE on MASTER's LIMIT page (GAIN CEIL TUNE GR): its page went with D3, TONE is full, LIMIT had the one free card |
| D6 | SAVE tapped on the mixer: always PROJECT | op_input.c `mx_quiet`; the mixer's YES (`mix_yes`) does nothing: FX on and MUTE are knobs |
| D7 | the SOUND row (PRESET ENGINE INIT SAVE AS) is EDIT's first row | op_screens.c `snd_first`; EDIT's LED full on it |
| D8 | SONG: keep SAVE held + SELECT, cue it on the SCENES layer | the SCENES layer's foot: "Rec+key store  Select: song" |
| D9 | HOME tapped on the mixer: SCOPE | unchanged |
| D10 | keys full / dim / dark in every mode and layer | op_keys.c (below) |
| D11 | headers: cut then case, full names from an Optimist-side table, digits never collide with n/N | op_draw.c `head_finish`, `HEAD_CONT` (below) |

Fixed with them: **P25** a family button with no rows on the selected track (its LED dark) does nothing when tapped
(it opened every row); **P28** CALIBRATE > PANEL asks first; op_song.c's header and section 4.4 described entrances
that no longer existed.

**Where the values of the removed pages are** (D3; nothing lost):

| Was | Value | Now |
|---|---|---|
| MASTER > GLOBAL | BPM | GLOBAL TEMPO KNOB 1 (and the TEMPO page, PLAY held; GLO layer's tap key) |
| MASTER > GLOBAL | SWING | GLOBAL TEMPO KNOB 3 (and the TEMPO page) |
| MASTER > GLOBAL | CLICK | GLOBAL REC KNOB 4 (and the TEMPO page's REC row) |
| MASTER > GLOBAL | TUNE | MASTER LIMIT KNOB 3 |
| MASTER > DRUMS | the drum level (G_DRLVL) | the mixer's DR row VOLUME (KNOB 2), the GLO layer's DR knob, SCOPE DR |

**The keys' lights** (D10, op_keys.c `keys_lit`, from `ui_leds`, main loop only). Full = active or selected, dim = it
has a job in this mode, dark = none; keys that only play stay dark:

| Mode | Dim | Full |
|---|---|---|
| FM6 layer (ENV held) | the 10 black keys (OP1-6 PIT GLO MONO POLY) | the target edited, the voice mode |
| FX layer | the 16 effects | the latched effect |
| GLO layer | mute 1-4, solo 5-8, fill 9, bar 10, tap 16, FX on / off (black 1-4; without FILLS white 9-12) | muted, soloed, a fill on / armed, FX off |
| SCL layer | all 27 keys | the song's root |
| SAVE layer | the scenes (SONG's PATTERNS row: the slots) | the playing scene (the queued one blinks) |
| LFO layer | the 16 patterns, the 10 black keys | the playing pattern (the queued one blinks), the selected track's key, the modifier held |
| ERASE, REPEAT | none: play surfaces | none |
| TEMPO (PLAY held) | the white keys (tap) | none |
| STEP, keys as steps | empty steps inside LEN | set steps, held steps; the playhead blinks |
| SONG's PATTERNS row | the selected track's patterns | the playing one (the queued one blinks) |
| NAME | every key with a job | the key cycling its letters |
| the lane pick (drum track, SHIFT past the hold) | the 16 lanes | the selected lane |
| anything else | none | none |

The selected drum lane's key is no longer full on every screen (it meant something else in the layers, P37); the
LIGHTS / KEYS backlight is off while a mode lights its keys.

**Headers** (D11). draw_head cuts the title to its room first (the screen's word goes first, as before) and cases it
after, so it never starts lower case: "Tempo 1/6", "Screen 2/7", "Master 1/2", "Ins tone 1/8". With the n/N shown, a
continuation page says its number once (op_draw.c `HEAD_CONT`: EDIT 1 / 2, REC 2, ARP 2, SCL 2, VOICE 2, SOUND 2 / 3,
STEP 2, FM6's LFO 2): "Edit 2/8", "Rec 3/6", "ARP 2/2". OSC 2, FLT 2, ENV2 and SONG's PART n keep their number (a
second unit, a part). Long names are still cut to the room left of the loop position and the tempo ("Clock O 5/6"
with a 6-letter badge).

**SELECT is the page of the four knobs, ALGORITHM the track or the lane** (the user, the same day: *"in step seq:
select should always select the page for the 4 knobs, and algo always the track/drum sub track. currently select does a
bit of both for drum seq. confusing."*). On the drum track STEP's pages are PATTERN and LANE: LANE shows the selected
lane's LEVEL TUNE DECAY REV and is named after it ("SNARE"); the 16 per-lane rows are gone (op_step.c `step_rows`).
SELECT pages them and never changes the lane; ALGORITHM walks DR, then the lanes (section 16), SHIFT + a key (STEP too, since 2026-10-14;
SEQ + a key there clears the step) picks one. The drum SOUND row's LANE card is a read-out (KNOB 2 no longer steps the lane). Checked on every
page: the mixer keeps its own rule (ALGORITHM the rows, SELECT the knob sets). **SCOPE still mixes them**: its SELECT
walks the sources MASTER T1 T2 T3 DR (the scope's view, not the selection, which ALGORITHM keeps); making the source
ALGORITHM's would need MASTER on SCOPE, where the walk now opens the master FX: left as it is, to decide (it is SELECT's, not ALGORITHM's: section 26's audit).

**A drum key played selects its lane** (the user: *"when i play a note for the drums on the keyboard it should switch to
that instrument in all the relevant pages"*; then: always, DR itself selected too; ALGORITHM back to DR returns to the
whole kit). With the drum track selected, a key down while the keys play selects that key's lane (`ui.lane`,
`lane_pick`), as SHIFT + a key does: the mixer's lane row, the drum SOUND / ENV / FX pages, STEP's LANE page and PRESETS
by the lane's SOURCE follow; the lowest key of a chord wins. Not where the keys mean something else (STEP's steps; STEP
with the keys playing already picks the last hit, `step_played`), a layer's tiles, NAME, the FM6 layer, the TEMPO page,
nor on MASTER; and not while a value is turned (a knob in the last 300 ms keeps the lane, so the cards never move under a
turn). It runs in the main loop from the note edges (op_input.c `drum_hit_pick`), nothing in seq.c's key ISR.

Not built (not decided): the layer titles' renames ("GLO mix" ...) and "Home locks it" after the name (P21); the header
CPU warning (D2 said no).

Tests: tests/ui_optimist_pages.h (pages_nav_tests D1 D2 D6 D7 D8 D9 P25 P28; pages_master_tests D3; pages_keys_tests
D10; pages_head_tests D11; pages_select_tests, SELECT and ALGORITHM; pages_hit_tests, a drum key's lane), and the tests
that reached SOUND by the mixer's YES or walked the lanes by SELECT updated.

## 18. The drum lanes' gestures (feat/lane-ui, 2026-10-12)

The lane-auto core (section 6.1: a lane's values as automation events, its own LFO) reached through this UI. The user's
rulings are lanedes/DESIGN.md section 6 (D1..D12); what was built:

| Gesture | Does | Code |
|---|---|---|
| REC + a lane knob, playing | On the drum SOUND, SOUND 2 and ENV rows, and the mixer's lane LEVEL, CUT and DRIVE (each row its own lane, D12): the lane's motion, a hold event on the step playing (`lane_knob`), heard at once at the lane's hits; the patch (dl.ofs, what SAVE keeps) is not written. While recording the card shows the value recorded, so the next detent goes on from it (as a synth card does from its motion). The list full: *MOTION FULL*, nothing written. Not recording: the patch as ever, and a hold in force on that value lets go so the knob is heard | op_cells.c `lane_value_rec`, `dsv_rec`; op_mixer.c `mx_lane_set` |
| A drum step held | The cards are LEVEL · RATCHET · TUNE · DECAY (D3): TUNE and DECAY are the selected lane's locks on the steps held, a padlock when there is one, the arrow for a hold event; with no lock they show the patch | op_step.c `held_cell`, `held_lane_param`, `lock_cell_id` |
| ... + EDIT or ENV | The lane's SOUND / SOUND 2 (EDIT, again for the next) or ENV page, its values as locks (SOUND 3, SOURCE, KIT and the LFO are not). EDIT locks only on the drum track; on a synth track it stays as it was | op_step.c `lock_pg_ok`, `lock_param` (`LOCK_LANE`); op_input.c `op_press` |
| ... a turn on a step without the lane's hit | Nothing written, *NO HIT ON THIS STEP* (D4: a lock there would never be heard) | op_step.c `lock_step_put` |
| ... YES | The hot lane cell's event: a lock becomes a hold event, a hold event a lock (`lane_kind_toggle`); SHIFT + the knob drops both | op_step.c `held_yes`, `lock_step_put` |
| The lane's hit removed (a tap, other lanes still on the step) | Its locks on that step go with it (`lane_step_drop`, D4); SHIFT + the step clears everything as before | op_step.c `step_wipe` |
| STEP's marks | A lane's event is a dot in that lane's colour (`lane_col`), whichever lane is selected (D9) | op_stepdraw.c `sg_marks` |
| LFO with a lane selected | SOUND with the lane's LFO (RATE WAVE PHS FADE, its wave as the picture) and LFO DEST (DEST AMT DEST2 AMT2), drums/lane_play.c `dlfo_field` / `dlfo_field_set`. The LFO LED is half lit on a lane (full on these rows) and dark on DR itself, where LFO does nothing; walking from a lane to DR leaves the LFO rows (every drum row). These two pages are this UI's (`LANE_LFO_PG`, index NPAGES + i in the page lists), not PAGES': SLOOP's UI is unchanged | op_cells.c `op_dsnd_desc`, `op_pg`, `sound_page`; op_graph.c `graph_lfo` |

Not built: the SLOOP UI's side of these gestures (lanedes section 3.3: `lock_par` as a stored id, `auto_marks_lane`), not
trivial, left for a brief of its own. The lane LFO's values are not recordable or lockable (they are not automation
events, section 6.1).

Tests: tests/ui_optimist_laneui.h (`lane_rec_tests`, `lane_held_tests`, `lane_lfo_tests`), in the automation switch
set; tests/ui_optimist_test.c's held drum step check follows D3.

## 19. Live effective values: one number, two bars (feat/lane-ui, 2026-10-12)

The user: *"for the destination %age (instrument setting) and the +/- (lfo, env, plock etc) values we should have a
visualisation of the current effective values (moving in real time)"*; then the decisions (optimist-work/effval
DECISIONS.md): *"one number, two bars. the number is what i enter. one bar is what i enter, the other is the actual
value"*, the same for modulated values and for the +/- cards; POLY: the last note played still sounding.

**On every card** (2x2 and the mixer's 1x4): the big number is the value as entered, the knob's own, as before. Under it
the entered bar, as before; and, while the actual value differs from it, a second bar: the actual value now, white
(amber on the hot card), 2 px, no trough. In a big card it sits 3 px above the entered bar (under the digits' foot); in
a 1x4 card the entered bar becomes 2 px at y 37 and the actual bar is at y 41. A bar whose value comes back to the
entered one stays 400 ms, so an LFO crossing it does not make the cards blink.

| Card | The actual value |
|---|---|
| A drum lane's value (SOUND, SOUND 2, ENV, STEP's LANE page, the mixer's lane rows) | What a hit now plays: the patch, the lane's motion and locks in force, its LFO now. The hit's own formula: drums/lane_play.c `dl_play_ofs`, which `dl_play_hit` now calls too (one code, no drift; drums_mix untouched). On a synthesised kit DECAY, CLICK, BEND and DRIVE are the absolute value the sound gets (`de_synth` on the kit's own sound: kit + offset, 0..127, BEND 0..96), always shown: DRIVE -5 on a sound whose drive is 67 shows -7 % big (the offset, as stored) with the actual bar at 57 of 127 (45 %). TUNE, SNAP, CUT, LEVEL and every sampled, user-sample and X0X sound: the offset a hit plays, shown when it differs (motion, a lock, the LFO) |
| A synth part's cutoff and shape cards (the engine's: ANALOG CUT, TRIO CUT and DUTY, GRAIN TONE and POS, FM6 MOD, VOICE BUZZ, PHASE DPTH, LOFI / SAMPLE / SLICE TONE or CUT, PHYS BRIT; `EFFV_DEST`) | The card's value + the part's cutoff (shape) modulation now, as track_render computes it: the LFO now (`lfo_val` x `lfo_fade`), the envelope of the newest active voice (`v->env`), the accent; with the macros as they play (`mac_effective`) |
| Any synth value a macro moves (FELUCCA_MACROS) | `mac_effective` |
| LFO DEST's FLT and SHP, ENV 1 MOD's | The destination card's actual value, on its scale (an engine without such a card: what the source adds, on -64..63), shown while the amount is not 0 |
| ... PIT | The pitch now from the LFO and the pitch envelope, in 1/16 semitones on +-12 |
| ... AMP (LFO) | The newest voice's level now (`env_out`: envelope, velocity, the LFO's dip), 0..127 |
| ... RES PAN LVL ATK DEC E1..E8 CHO DLY REV INS1 INS2 (either LFO's AMT A / AMT B) | The value as the render has it now: its authored value moved by every slot on it in turn (LFO's two, then LFO 2's) from the LFOs' values now, the same `lo_move` as voice.c `lfo_over_in` (v x AMT over half the range at full, clamped), on the value's own range (PAN -64..63, LVL ATK DEC 0..127, E1..E8 and RES the engine's `edit[]` range; INS1 / INS2 the amount of the insert in the slot, its own range: none when the slot is empty). The DST cards of INS1 / INS2 are named by the type the selected track's slot holds, as INSERTS names it (`FILT`, `DST`, `COMP`, `CHRS` ...; an empty slot `S1 --` / `S2 --`; params.c `lt_text`, both UIs); the help line stays "What LFO moves (slot A)". XRATE / XAMT: no second bar |
| LFO2 DEST's AMT (LFO2) | Its destination as the LFO DEST cards above (PIT the pitch in 1/16 semitones, AMP the voice's level, FLT / SHP the engine's card), while DEST is not OFF and AMT not 0. LFO 2's block (its phase and S&H value, `lfo2[]`) is part of the cutoff, shape and pitch modulation every card shows (`effv_mods`), so the engine's own cards move with it too |
| A lane LFO's AMT / AMT2 | Its destination's actual value (as the lane value above), while the slot acts |

Not on a held step's cards (they describe the step, not now), nor on the layers' cards. An ANALOG 2 part's own ENV2 and
keytrack are not in the cutoff bar (they are per voice inside the engine); CZ's amplitude curve is not in the envelope
term. **How**: the UI recomputes from RAM the audio side already keeps (ui/optimist/op_effval.c, `effv_cell`), as
`mac_effective` does; nothing is published, nothing added to the ISR, to HOT or to RAM code. The cards' band is drawn
again only when a bar appears or goes (or anything else on it changes); a moving bar alone is its 2 px strip, at most
every 33 ms (op_draw.c `effv_strips`). No builder switch: the flash cost is small (the report has the sizes).

Tests: tests/ui_optimist_effval.h: the lane's offsets and absolute values equal `dl_play_hit`'s and `de_synth`'s
(patch, a hold, a lock over it, the LFO playing, stopped and synced); ANALOG 2's cutoff as rendered (`v->s[6]` after
`track_render`: the LFO, the envelope, both, an accent) equals op_effval.c's, block after block; the bars (DRIVE -5,
a value nothing moves, a hold in force and its linger, the lane LFO's AMT, a synth CUT moving with the LFO between two
frames while its number stays, the LFO amount FLT on the cutoff's scale, SHP at 0).

## 21. The track insert slots (feat/insert-slots, 2026-10-12)

The user: "i would like two slot, configurable per track (filter, disto, comp etc)". Design and decisions D1..D9:
optimist-work/inslots/DESIGN.md, DECISIONS.md. Each track, T1 T2 T3 and DR, has two insert slots, S1 then S2, run after
the SLICER (D2). A slot holds DIST, FILTER (when built) or COMP, or nothing. One type once a track: picking the type the
other slot holds swaps the two (D3). The amount belongs to the type (P_DIST, P_TFLT, P_TCOMP; D4), so a lock or a motion
taken on an insert's card records that type, and stays with it when the slot changes. A new track: S1 DIST, S2 FILTER,
both at 0 (D5). The four master slots hold the sends only (D1). Each track's COMP has its own RATIO ATK REL (D6). The
drum sounds' own DST and CMP are always heard (D7).

| Where | What | Notes |
|---|---|---|
| SOUND > FX: **INSERTS** | S1 TYPE · S1's amount · S2 TYPE · S2's amount | the amount's card is labelled by its type (DST FILT CMP), the knob's frame alone for an empty slot; a TYPE card lists EMPTY (dim) and the inserts built; the needles show the amounts; no reset (YES) and no lock on a TYPE card |
| SOUND > FX: **CMP** | RATIO · ATK · REL, the selected track's | a row only while one of the track's slots holds the COMP; the master COMP's value lists |
| SOUND > FX: **GATER** (INS_GATER, 2026-10-14) | DEPTH (the amount INSERTS calls GATER) · PAT (16 names: 16TH 8TH 4TH HALF OFF8 OFF16 DOT8 DOT4 TRNC1-4 GALOP RVGAL CLAVE TRESL) · RATE (a division, 1/8 .. 32T) · SHAPE | after CMP, before SENDS; a row only while one of the track's slots holds the GATER (the drum track's too: it gates the drum bus); remembered with its FX family like any row; the picture alone under the cards: the pattern's 16 steps as a strip, the gate's gain under it (DEPTH, SHAPE's ramps), the step the gate is at in white (none at DEPTH 0, drawn dim); the SLOOP UI shows the same page and picture (shared PAGES) |
| SOUND > FX: **SENDS** (was FX) | card k is the send of return A, B or C (card k = return k), REV DLY CHO by default (2026-10-15, the user: "match the order between master return and the send": every sends set, the mixer's returns and RETURNS follow A B C; an empty return: a blank card) | on the drum track: its send **levels**, in the same order, 0..100 %, 100 % by default, each lane's send of that bus scaled by it (the user: "the main drum track as fx: on/off, not the normal send levels ... it should be like any other track") |
| SOUND > FX: **BUS SENDS** | the drum bus's own sends (its mid, after its inserts) | the drum track only |
| the mixer | a track's INS set: S1 TYPE · amount · S2 TYPE · amount; DR the same, then its send levels; a lane DRIVE DST CMP | the COMP meter follows the row's slots |
| FX + KNOB 4 (TRK FLT) | the track's FILTER amount | "FILT: NO SLOT" when none of the track's slots holds the FILTER |
| TOOLS > MISS | "3RD INS T2" | an old project's third insert on a track (DIST FILTER COMP all used, all in master slots): the first two kept |

Old projects and SLOOP 2.4 imports map onto the slots with the same sound (fx_rec.c ins_map); the one difference is the
SLICER moving before DIST (2.9 ms ramps, golden slicer/song_gate_stut). The SLOOP UI keeps its FX page (K1 S1's amount,
K2..K4 the master sends in their slots' order: DST REV DLY CHO by default) and gets INSERTS; its own redesign (design phase 4) is left.
Tests: tests/ui_optimist_inslots.h, ui_optimist_mixer.h; tests/fx_slots_test.c (the model, the audio, the record).

### Limited insert types: CHRS and ECHO (feat/fx-instances-ui, 2026-10-13)

CHRS (INS_CHORUS) and ECHO (INS_DELAY, 75 ms) are insert copies of which the build holds a count of instances, 0..4
(fx/insfx/fxi.c). FLNG and the other inserts are "any number": no note. The UI reads the core's queries (main loop only):
`fxi_cap` `fxi_free` `fxi_state(k, s)` `fxi_can(k, t)`. The picker has no confirm: a detent loads the type at once
(`ins_load`), the main loop hands out an instance in its next pass (`fxi_poll`). So the slot's TYPE cell is the picker, and
it says what it holds:

| State | The TYPE cell (S1 / S2, on INSERTS and the mixer's insert set) | Help line (kind `op`, ctx INSERTS) |
|---|---|---|
| a type with an instance (`FXI_OK`) | the name, with the free count beside it: **CHRS 2/4** = 2 free of 4 available (the built instances plus the idle send lines lent now, fx-lend; the held one counted used); on a card too narrow for it, only the name | CHRS: "Chorus, shared: free/avail"; ECHO the same |
| none free after the pass (`FXI_FULL`) | the label becomes *S1 CHRS*, the value **FULL**, both dim: the type is bypassed on this track; its amount's needle goes grey | "No free chorus: bypassed" (ECHO: "No free echo: bypassed") |
| picked with none free, before the pass (`FXI_WAIT`, `fxi_can` 0) | the value **IN USE**, dim: one frame, then FULL (a detent cannot be taken back before the pass: picking it stays allowed, as asked) | as FULL |
| picked with one free, before the pass (`FXI_WAIT`, `fxi_can` 1) | the value **WAIT**: one pass of the main loop, never seen in use | the type's own |
| a type with 0 instances built | not in the list at all (it is not built: `FXT_BUILT`) | none |

`FXI_WAIT` in the core means "asked, the next pass gives it" (fxi.c): a pass, not a queue. A track waiting for another
track's instance to fade out is already `FXI_FULL` (bypassed until one is given back, then it takes it). Every slot
reads its own track: INSERTS the selected one, the mixer's insert set its row. The SLOOP UI (UI=0) shows **FULL** in dim in
the value of a bypassed slot's TYPE card on its INSERTS page (a few instructions under `INS_LIM`: none in a build without
the limited types); the free count and the help are not there. Tests: tests/ui_optimist_fxi.h (the counts 0, 1 and 4,
with and without the help line).

## 20. GLOBAL and SYSTEM as settings lists (feat/settings-list, 2026-10-12)

The user: "the system menu would be better and more readable as vertical list of options, we scroll thru them using
select, change value using preset, and also using the 4 knobs, algo and preset should not change anything outside of
systems settings (track presets and current track)"; then "select scroll by page", "algo one by one", "more inline with
other pages", "consistency is key in our ui", "when we scroll with algo, we scroll page by page (the selection on the
page moves line by line)", and "glo still pages thru pages of glo and then system, it should switch between the two".

**Logical pages (feat/settings-pages, 2026-10-13; the user: "grouped in logical pages, dont cut a page in the middle", "i prefer two pages with 2 items than messy pages", "the midi channels for the tracks should be on one page").** A page is one section: sections are never combined to fill a page, a short one leaves the page's last lines empty (their knobs do nothing). Only a section of more than 4 lines is split, between its rows (REC: MODE LENGTH START CLICK, then QUANT: "Rec 1/2" over each page). The pages are made from the lines the build has (op_global.c `gl_build`, `gl_sid`: a section's lines, REC 2's being REC's), so DUAL, BLE, CDC, CLKOUT, BRIGHT, LIGHTS, REC_MODES move the rest up and no page number is fixed. The header's n/N counts the pages; SELECT steps a page (the cursor on its first line), knob k is line k of its page (ALGORITHM scrolls the lines here, section 31). Tests: tests/ui_optimist_glpages.h (every variant).

**The look.** GLOBAL and SYSTEM have no cards. Under the header, to the screen's foot, one page of up to 4 lines; a line is one
setting: its knob's number (1..4) at the left, the setting's name, its value on the right as the cards wrote it (the
unit after it, small), the value's form (bar, dots, pill) under the value. A section's name, small and dim with a rule
after it, stands over its first line on the page; a page that starts inside a section names it again. The cursor line
is a bar in the track's colour, ink on it. A read-out (CPU's CLOCK and LOAD, Tempo's NUDGE, MIDI's CLK) has its value in
amber and its number dim: no knob turns it; About's VERSION is one too, the version itself ("0.1"; YES still says it
whole). An action (Calibrate's PANEL) shows its word in amber, CALIBRATE (the user, 2026-10-13; it fits the list's small face),
never a bare YES (fix/settings-lines, 2026-10-13: the user read the YES lines as dead): YES or PRESETS runs it, PANEL asking first; its knob only makes it the cursor. With
KNOB COLORS on, the numbers and values take the knob caps' colours. The header: the badge GLOBAL or SYSTEM, the cursor
line's section and the page n/N within the screen ("Tempo 1/5", "Screen 2/4").

| Screen | Sections and lines (what this build has) |
|---|---|
| GLOBAL | Tempo: BPM, NUDGE, SWING. Rec: MODE, LENGTH, START, CLICK, QUANT (REC 2's). MIDI (one page, 4 lines; 3 without CLK OUT): CLK IN, CLK OUT, MIDI IN, MIDI OUT. Channels: TRK 1, TRK 2, TRK 3, DRUMS |
| SYSTEM | CPU: CLOCK, LOAD. Screen: COLOR, BRIGHT, KNOB COLORS. Lights: LIGHTS, KEYS. Audio: LOWCUT. USB: SERIAL. Bluetooth: BLUETOOTH, STATE, DEVICE, MIDI. Devices: DEVICES, RECONNECT, LAST DEVICE (section 39). Calibrate: PANEL, HOLD. About: VERSION |

A line is a cell of the rows the screens had (TEMPO's `tp_*`, SYSTEM's `sys_*` over the `SYS` table), called through
their functions: each value still lives in one place and keeps its format; a cell this build lacks is no line. KNOB
COLORS' label is the full name now (it was COLORS to fit a card).

**The controls**, the same on both screens:

| Control | What it does |
|---|---|
| KNOB 1..4 | the page's line 1..4 (its number at the left); the turn makes that line the cursor |
| SELECT | the page: a page of 4 lines a detent, within the screen, stopping at its ends; the cursor on the page's first line |
| ALGORITHM | **nothing (no track here, section 28)**; it was the track walk (op_walk.c, section 26); never the cursor (the user, 2026-10-13: "ALGORITHM moves track - NO. only the track!"). It was a line-by-line cursor until then |
| PRESETS | the cursor line's value; SHIFT + PRESETS: back to its default (the values with a descriptor: BPM, SWING, SYNC, CLICK, QUANT, MIDI OUT / IN, the channels); the others to their FACTORY value, what a blank FM-1 or an empty settings record gives (the user, 2026-10-13): MODE FREE, LENGTH 1 bar, START NOTE, CLK OUT OFF, COLOR MONO, BRIGHT 8, KNOB COLORS OFF, LIGHTS and KEYS OFF, LOWCUT OFF, SERIAL OFF, HOLD 350 ms, 2ND CORE AUTO. BLE is never reset (a reset must not drop a live connection). An action line: runs it, as YES (PANEL asks first). A read-out: nothing. Never the track or its sound on these screens |
| SAVE (YES) | the cursor line: an action (PANEL asks first; VERSION says it; BLE opens the devices list), an on / off value toggled |
| GLO tapped | from any other screen GLOBAL; on GLOBAL SYSTEM; on SYSTEM (the devices list too, which closes) GLOBAL. Each screen keeps its cursor line |
| OCT- / OCT+ held | on a GLOBAL page with Tempo's lines: the nudge, as on the TEMPO page; elsewhere the octave |

PRESETS never selects a track and never changes a preset here; ALGORITHM selects the track as everywhere (the settings are the same whichever track is selected). The BLE devices list
(SYSTEM > Bluetooth > BLE, YES) is unchanged: its rows and cards, SELECT moves its cursor, PRESETS does
nothing, ALGORITHM walks the track. The keys have no job on these screens: dark (section 17, D10). The GLO LED is full on both.

**How.** op_global.c: the lines (`gl_build`, once: what a row shows depends on the build only), the screens' functions
(a row is a line, a cell the 4 lines of its page; YES acts on the cursor line and a question asked there comes back to
it), `glo_step` (SELECT), `gl_presets`, `gl_pick` (a knob's line the cursor), `glo_tap`, `gl_on_tempo`
(op_tempo.c's nudge). op_gldraw.c draws the list (main-loop code, in size_fns' SIZE_FILES; nothing in HOT or RAM code).
op_draw.c: no cards on these screens, the page n/N in the header. op_input.c: PRESETS and the knobs' pick (ALGORITHM is `algo_walk` there as on every screen).

Tests: tests/ui_optimist_glo.h (the lines and their order against the rows' cells, the sections, every line named; the
drawing: no cards, the 1..4 numbers, the cursor bar; SELECT page by page, stopping at each
screen's ends and never crossing (section 26: ALGORITHM no longer moves the cursor); KNOB k the page's line k; PRESETS the cursor line, SHIFT + PRESETS its default;
PRESETS leaves the track and its preset alone; the read-outs; VERSION, PANEL's question, the BLE devices;
GLO toggling with each screen's line kept; the nudge; a setting saved when left), tests/ui_optimist_glwalk.h (every
line of both screens walked (the cursor set directly, `line_at`): a value's knob and PRESETS change a setting and its text, SHIFT + PRESETS a
descriptor's default; a read-out's knob and PRESETS change nothing; no line draws YES, PANEL draws CALIBRATE and PRESETS asks; every line without a descriptor back to its factory value, BLE kept;
VERSION the version), tests/ui_optimist_pages.h (D1 as now:
GLO toggles, SELECT pages within a screen; the headers), and the cards, knob colours, CLOCK OUT, HOLD and BLE tests on
the lines. Screens: opt-glo-global-top, opt-glo-global-mid, opt-glo-system-cpu.

## LFO 2 pages (2026-10-13; always built since 2026-10-16, the builder item LFO2 was retired)

A synth track's LFO family is LFO, LFO DEST, **LFO 2** and **LFO2 DEST** (core/params.c PAGES; the drum track has no LFO 2: `page_for_drum`, its lanes have their own LFOs). Cards are 2 x 2, SELECT is the page, ALGORITHM the track, SHIFT + a knob the default.

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| LFO 2 | RATE (free 0..127 as the bar from 0, or synced -1..-10: ten cells, 4BAR .. 1/32, exactly LFO's gauge: `is_lrate`) | WAVE | TRIG (NOTE / FREE) | POL (BI / UNI) |
| LFO2 DEST | DST A | AMT A (+/-, -128..127: +-100 %, the live second bar: the table of actual values) | DST B | AMT B |

LFO 2's graph (`GR_LFO2`, op_graph.c `graph_lfo2`) is its WAVE over two cycles, POL UNI one way only; LFO2 DEST shows it, as LFO DEST shows LFO's. The header keeps the number of "LFO 2" with the n/N (a second unit, as OSC 2 and FLT 2; FM6's LFO 2 row is still a continuation, op_draw.c `head_cont`). Not recorded as motion and no locks yet (the core: motion.c). **The DEST cards (LFO DEST and LFO2 DEST, feat/lfo-slots-ui)**: both LFOs have two slots, KNOB 1 DST A, KNOB 2 AMT A, KNOB 3 DST B, KNOB 4 AMT B (the same table, `PAGES`; the SLOOP UI gets them from it). The destinations: OFF PIT FLT SHP AMP RES PAN LVL ATK DEC E1..E8, and with LFO 2 XRATE XAMT. The card's text (params.c `param_format`, so both UIs): **E1..E8 show the selected part's engine's own name for that EDIT value** (the card the engine's `edit[]` / `desc` hook gives that knob: FM's RATIO, ACID's ACCENT ...; "E1" where the engine names none), **XRATE / XAMT show the other LFO's**: "L2 RT" / "L2 AMT" on LFO's DEST, "L1 RT" / "L1 AMT" on LFO 2's. The SLOOP UI (UI=0) gets the same two pages through PAGES (the LFO button steps four pages; VIEW ALL's LFO family shows four rows and no graph), with no graph of its own for LFO 2.

Tests: tests/ui_optimist_lfo2.h (the pages, order, cards to params, the RATE sync display and knob, SHIFT + each knob, the graph, the AMT bar equal to the render), tests/ui_optimist_effval.h (`effv_over_tests`: the AMT bar of RES PAN LVL ATK DEC E1..E8 equals what `lfo_over_in` gave the render, one and two LFOs on a value; the DEST cards' E and L2 RT text), tests/lfo_slots_test.c (`t_names`: every engine's E1..E8, the L1 / L2 names), tests/ui_pages_test.c (the SLOOP UI's four-page LFO family, the RATE gauge), tests/lforate_ui.c (both LFOs' RATE gauge and division icon on the SLOOP UI's screens). Screens: opt-lfo2-page, opt-lfo2-dest, opt-lfo2-sync.

## 21. Sequencer mutes (feat/seq-mutes, 2026-10-13)

The user: sequencer mutes, "for midi, not audio", on the mixer, SHIFT + a key. A muted track or drum lane stops
triggering new notes, for the internal sound and for MIDI out; the notes already sounding finish their tails. It is not the
mixer's MUTE card, which stays as it is (the track's P_MUTE, a lane's dlm_mute: those silence the sound and MIDI out
goes on).

| | |
|---|---|
| **The gesture** | on the **mixer (HOME screen)**, **SHIFT alone + a key** toggles. On every other screen SHIFT + a key keeps picking the lane, on STEP too since 2026-10-14 (SEQ + a key clears the step there). With SAVE or another button also held, it is that chord's (SAVE + SHIFT + a key clears a scene). The key plays nothing (the keys go to the UI only, playing or stopped), so muting does not sound the track |
| **The keys** | the 16 white keys the drum lanes play = the 16 lanes (the key of lane n is `key_of_lane(n)`, F3 upward); the 4 black keys F#, G#, A#, C# of the first octave = T1, T2, T3, DR; **the last upper key (key 25, the lone F# at the right end) = the ACCENT lane's sequencer mute** (2026-10-14, `smute_toggle(SMUTE_ACC)`; the row's MUTE card does the same; its LED dim, full while muted). The other black keys do nothing. Works whichever track is selected and whichever group the mixer shows (channels or sounds) |
| **What a mute does** | a muted **track**: its steps trigger nothing (synth notes, drum hits, ratchets), no lock of its steps applies; a synth note sounding ends by its own gate (a tie chain is let go, not cut), the drum hits' MIDI notes end with the step as ever. Its recorded motion keeps running (a filter sweep goes on through the mute). A muted **lane** (DR): no hit of that lane, no ratchet, no MIDI note, and no lock or motion of the lane's events (they would only sound at its next hit). Notes **played live** on a muted track still sound (only the sequencer is muted); the arp and the rolls are played live |
| **Saved** | runtime state saved **with the project**, like the mixer state: a TLV of the FX record (`seq_mute.c`, TLV 0x43, 3 bytes: the tracks' bits, the lanes' mask; none when nothing is muted, so a project from before loads with nothing muted). A song section load applies it as the FX record's other parts; NEW and a project with none clear it |
| **On the mixer** | a muted row's steps are drawn grey (no white playhead) and an **X** stands beside its code (channels, under T1..DR, next to the M / S badge); a muted lane's code is grey (the audio-muted lane's code stays amber). The audio M badge, the MUTE card and the VU are as before. With SHIFT (past the hold time) the **keys' lights** show it: the 16 drum keys, the 4 black keys and the last upper key (ACCENT) dim, the muted ones full (the full / dim / dark rule); a toast names it (*T2 SEQ MUTE*, *BD SEQ ON*) |
| **SLOOP UI (UI=0)** | unchanged: its mute UI (GLO + keys) is the audio mute, a different state; the sequencer reads the mutes in both UIs, only this UI sets them |

Code: `seq/seq_mute.c` (the state, `smute_key`, the TLV), `seq.c` (`drum_step`, `seq_ratchets`, `smute_step` at the step
entry), `auto.c` (the locks, `dla_event`), `fx/fx_rec.c` (the TLV), `ui/optimist/op_input.c` (`mute_mod`, `smute_notes`),
`op_keys.c` (the lights), `op_mixdraw.c` (the rows). Tests: `tests/smute_test.c` (the key map, a muted track and lane play
nothing for sound and MIDI, the tails end, a live key sounds, the TLV and PROJECT round trip), `tests/ui_optimist_smute.h`
(the gesture on the mixer, the other screens keep the pick and the clear, the X and the grey steps, the keys' lights).
Open: whether a track's motion should stop too when muted (it runs now), and whether the key should sound as a preview
(it is silent now).

## 22. Step conditions and the sequence tools, from Felucca 1.5.6 (feat/seq-tools, 2026-10-13)

Ported from Felucca 1.5.6 (hugelton/Felucca 9f6a885, Leo Kuroshita, Hügelton Instruments, GPL-3.0-only): its step
conditions (1.5.5, #209) and its SEQ TOOLS (`ui_tools.c`). Design and the user's rulings: optimist-work/seqtools/DESIGN.md
(Q1..Q15, every one as recommended; Q5, Q7, Q8 and Q14 the user's own). Two builder items, both on in user-default:
**COND** (`FELUCCA_COND`, both UIs) and **SEQTOOLS** (`FELUCCA_SEQTOOLS`, this UI only).

**Conditions.** 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4: the step plays on loop a of every b of its own track (a:b). Each
track counts the passes through its LEN from PLAY, a song section and a launched pattern (Felucca's points); a LEN
changed while playing keeps counting. A condition is a step-only event of the automation store (`seq/auto.h`
`AUTO_COND` 0xFC, values 1..9), so neither the step nor the project format changes; on the drum track it is the whole
step (every lane), as the chance. With FILL and the chance on the same step: the condition, then FILL, then the chance
(a failed one is a REST: no notes, ratchets or locks). Old forms: a track with one is never written in the 2.4 stepx
form (it would be dropped); the 2.4 export reports it (lost2 bit 8, `SX24_COND`); an older Optimist build keeps the
event and plays the step always.

| Where | What |
|---|---|
| A step held, SELECT to EXTRA | the 4th card **COND**: `-`, 1:2 .. 4:4, stopping at both ends, on every step held; SHIFT + its knob clears it. EXTRA is CHANCE · NUDGE · FILL · COND as built |
| The held step's line | *Step 6  1:4* (after the fill) |
| The marks | the step's event dot, as any non-fill event |
| SLOOP's UI | plays them, draws the dot; nothing edits them |

**The tools.** STEP gains two last pages, **TOOLS** and **EUCLID** (SELECT; SEQ tapped again cycles to them):

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | YES |
|---|---|---|---|---|---|
| TOOLS | REVERSE | RANDOM | COOK | LENGTH: x2 (turned right) / 1/2 (left) | does the hot card; a toast (*T1 REVERSED*, *SNARE RANDOM*, *DOUBLED: 32 STEPS*, *PATTERN FULL*); one undo each, no question |
| EUCLID | HITS | STEPS | ROT | LANE (a read-out; a synth track: none) | - (each turn writes at once; a run of turns is one undo) |

A turn of a TOOLS card arms it (its YES), so a knob bumped does nothing. The scope is ALGORITHM's: a synth track; DR
itself the whole kit; a drum lane walked to that lane only (REVERSE RANDOM COOK EUCLID; EUCLID on DR itself the last
picked lane, named on LANE). LENGTH is always the whole pattern. What they do (Felucca's, `ui/optimist/op_tools.c`):

- **REVERSE**: the steps mirrored inside LEN. A note moves whole: its NOTE, its TIEs and the carry steps that go on
  with its pitch are one unit, kept in order at the mirrored place, so its exact length (a step's own end, `ngate`)
  is the same. A lane: that lane's hits and its lane events only.
- **RANDOM**: a synth track keeps its rhythm, levels, ratchets, chance and conditions, and gets new notes: a walk
  through its scale (ROOT, SCL; **SCL CHR: the minor pentatonic of the ROOT**) around the notes' mean, a chord keeping
  its shape; a carried note changes all along its chain. DR: a beat (KICK on step 1, SNARE on 2 and 4, CLOSED HAT 8..12
  a bar, OPEN HAT and lanes 2..8 sparse); a lane: that lane drawn the same way, at least one hit.
- **COOK**: a few changes (15..25 % of the notes: a scale step, an octave, the level NORM <-> HARD, the chance
  100 <-> 50 / 75 % as an event, a condition left alone; drums 10..20 % of the hits moved, added or taken away, never a
  lane's last nor the kick on step 1); again until something changed.
- **LENGTH x2** (DOUBLE): the steps after themselves, LEN x2 (64 at most), their events copied (refused, *PATTERN
  FULL*, when the copies would pass the track's 128). **1/2** (HALF): LEN / 2, the steps past it kept (as the LEN card).
- **EUCLID**: HITS spread over STEPS, turned by ROT, repeated through LEN, written from the pattern as the run began
  (the undo session's start: no copy of our own); a synth hit keeps the step's note, else the root near C4; the drum
  lane's hits only. Values kept until the power goes (first 4 / 16 / 0); SHIFT + a knob its default.
- Every tool works on steps 1..LEN; a step it empties loses its step-only events; locks, motion, chance, conditions,
  nudges and fills move with their steps. **The EDIT layer's SHIFT and LENGTH x2 now move and copy the events too**
  (before, they stayed on their old steps); SLOOP's EDIT layer is unchanged.

Not ported (Q11): Felucca's BEAT and lane FILL; CLEAR and SHIFT exist (the REC ring, EDIT KNOB 1).

Tests: tests/cond_test.c (the conditions alone), tests/auto_test.c `t_cond` (with FILL and chance, the forms, the 2.4
bit), tests/ui_optimist_seqtools.h (every tool on note chains, the drum kit and a lane, EUCLID's run and undo, the EDIT
layer's SHIFT, the COND card), run with the store whole and with COND alone.

## 23. The help line, and the risky actions asked (feat/help-line, 2026-10-13)

The user: "we should have a help text on the bottom last line. this info text should be displayed everywhere when i
modify a value or in the vertical menus"; and "all action that are 'risk' we should have a popup yes no (home/save)".

**The help line** (builder item PARAM_HELP, the one SLOOP's top-bar help uses; off in user-default, on in the user's own
config). One line of the small face on the screen's last 18 rows (y 222..239): a 1 px rule, then the words in amber.

| When | What it says |
|---|---|
| a value turned, any screen (SOUND, the mixer, MASTER, STEP's cards, TEMPO, SONG, SCOPE) | what the knob changes ("Filter cutoff"), while it turns and ~2 s (120 frames) after its last detent; another screen lets it go. A knob that only picks an action cell shows the action's line too |
| GLOBAL, SYSTEM (the settings list) | always: the cursor line's ("Tempo of the song", "Teach buttons, knobs (asks)") |
| PROJECT | always: the cursor row's hot cell ("Load: replaces work (asks)") |
| never | under a question (the modal), the REC ring, a layer's map, NAME, the BLE devices list (its status line is at the foot) |

Room: the settings list's lines are 29 px instead of 33 (4 lines and 4 section names, 196 px, end at y 221); PROJECT's
rows end at y 221 (op_draw.c `LIST_H`). Elsewhere the line covers the panel's foot while it shows (the bottom of SOUND's
picture, the mixer's last strip, STEP's playhead strip and the held step's values under the grid): gfx.c `cv_clip` keeps
every band off its rows, and when it goes every band is drawn again over itself (`op_repaint`, no flash). An action that
asks first says "(asks)".

The words: tools/param_help.json, one table with SLOOP's UI and the web editor's tooltips. A new kind `op` holds this UI's
own lines (the settings, PROJECT's actions, the SOUND row, the mixer's sends, SONG), by the row's name as the screen
names it (GLOBAL / SYSTEM: the section) and the cell's label; built only with FELUCCA_UI == 1, not in the editor's copy.
A line is found as (op, the row's name, the label), then (op, the screen's name, the label), then by the value's
descriptor: the first page of PAGES showing it that has SLOOP's line (an engine's value by the engine). Looked up only
when the cell it is about changes; the strings are PH_BLOB, flash (XIP), the texts packed with a byte pair code and
unpacked into a line on the stack when drawn (ui/ph_blob.c); nothing in RAM code. Each fits 232 px
(gen_param_help.py checks). Code: ui/optimist/op_help.c.

**The risky actions** (audit: every action cell and button combination; the table in the branch's notes, ACTIONS.md).
Risky: it destroys or overwrites what undo cannot bring back, or changes the device. Each asks through the one confirm
(the modal: SAVE yes, HOME no, 3 s). Already asking: PROJECT LOAD / NEW / a SAVE over a used slot, SNAPSHOT LOAD / CLEAR /
a SAVE over a used one, USER LOAD / ERASE / a SAVE over a used one, TOOLS CLRSQ / INIT / NEW, SOUND INIT, SHIFT + ENV
(INIT), SHIFT + SEQ (the locks), the SLOOP import, CALIBRATE, BLE FORGET, a scene / pattern store
over a used one, a scene / pattern clear, a pattern copy over a used one, a drum lane's RESET / SAVE / ERASE, FM6 STORE /
INIT. Added: SONG's **DELETE part** ("Delete part 2?": the song's order has no undo and is saved when SONG is left); a
drum lane's user kit SAVE / ERASE now name the slot ("Save kit 3?", was "Save?"). Kept as they are (the user): STEP's
tools, single-step edits, what one undo restores.

Tests: tests/ui_optimist_help.h (every GLOBAL / SYSTEM line and PROJECT cell has its line, the table's, within 232 px;
SOUND and the mixer: a turn shows it ~2 s, then the bands come back; every value without a line listed),
tests/ui_optimist_risk.h (every risky action asks and HOME changes nothing), run_tests.sh's two help-line units.
SONG REC over a song, a scene loaded while stopped and a sound change over edits: section 24. A SYSTEM > HELP LINE
ON / OFF switch is not built.

## 24. Asked before edits are replaced (feat/risk-asks, 2026-10-13)

The user's rulings on section 23's open points: "always ask if the sound/preset has edits"; a scene loaded stopped
asks "only when the current work differs from what is stored"; SONG REC over a stored song asks; SHIFT + SEQ as it is.

| What | Asks | When |
|---|---|---|
| PRESETS, SHIFT + PRESETS, the SOUND row's PRESET and ENGINE; on DR the kit; a lane's own sound and its kind | "Discard T2 edits?" ("Discard DR edits?") | the track's sound has edits; YES makes the change, HOME keeps the edited sound (nothing moved) |
| INIT (the SOUND row, TOOLS, SHIFT + ENV), a user preset's LOAD | "Init T2?", "Load preset U03?" | the sound has edits; none: done at once |
| a scene loaded stopped (SONG's scene row, SAVE + a scene key), PROJECT's LOAD | "Load scene B?", "Load project 3?" | the work differs from what was last loaded, stored or made NEW; while playing a scene launch is never asked |
| SONG REC armed (SAVE + REC, SONG's REC) | "Record over song?" | a stored song of more than one part |

**Edited.** A track's sound is edited when the values a preset sets (model.c `param_kept`: not the pattern, not the
mix) and, on the drum track, the lanes' own sounds (offsets, sources, samples) and sends differ from when the sound was
loaded or saved. The signature is taken again whenever the sound is another one (its engine, preset or user preset; the
drum track's kit or user kit: checked every frame, no hash) and when the UI loads or saves it (a preset step, INIT, a
user preset LOAD or SAVE, a project or scene LOAD or store, NEW). After YES the new sound is unedited, so browsing on
asks nothing until the next edit. **The work** (the tracks' values and steps, the lanes, the globals but the clock
followed and the slot shown) is compared with its signature at the last load, store or NEW, only when a load asks.
Not counted: the FM6 operator editor's voice buffer (not in the track's values); recorded motion that writes the
values while playing may make a sound read as edited (it asks; never the other way).

Code: ui/optimist/op_edits.c (the signatures, `ed_ask`, `ed_run`), the call sites in op_screens.c, op_walk.c,
op_cells.c, op_combos.c, op_project.c, op_name.c, op_layers.c. Tests: tests/ui_optimist_risk.h (each question over
edits, a changed work, a song; HOME changes nothing, YES does it, an unedited sound browses freely).

Open: SONG REC over an existing song, a scene loaded while stopped (it replaces the work, no question), a preset or
engine change over an edited sound (not in undo): see the report; a SYSTEM > HELP LINE ON / OFF switch is not built.

## 25. Every screen remembers its page (feat/page-memory, 2026-10-13)

The user: "screen should remember its page, including plocks etc." A screen shows the page it was left on when it is entered
again; nothing is saved in the project (RAM, cleared at power-on, a few bytes). What was found: the lists (PROJECT, SYSTEM,
GLOBAL with its lines, SONG, MASTER's FX pages, TEMPO, SCOPE) and the mixer's knob set already kept their cursor
(`ui.row[]`, `mx.set`; GLO tapped: each of GLOBAL and SYSTEM keeps its line); the gaps were STEP (its row went back to
PATTERN on every entry), SOUND (a page button always opened its family on the first row) and the held step's card page
(back to STEP on every hold).

| Screen | Remembered | Fallback, and what still jumps |
|---|---|---|
| SOUND | per track and per family (ENV LFO FX SCL EDIT ARP, FM6's operators): the row the family showed last on that track, op_screens.c `snd_mem`, kept by `snd_mem_tick` once a frame while SOUND is up | a row the track no longer has (another engine): the nearest, the family's last. A page button still opens its family, **on the row it last showed**, and tapped again goes to the next row, round. ALGORITHM on SOUND still keeps the page's name across tracks (the walk is not a jump) |
| STEP | its row (PATTERN, ARP, the LANE page, TOOLS, EUCLID); the first time PATTERN | a row the track lacks: the last. The drum track's page is the track's, not the lane's: another lane picked meanwhile shows the same page |
| a step held | per track: the card page last used (STEP, EXTRA or a p-lock page), saved when the last step is let go or STEP is left, reopened by the next step held on that track (op_step.c `hp_mem`) | a p-lock page the track (or build) lacks: STEP. A page button with a step held still jumps to its family's p-lock page, and that becomes the page remembered; OCT- / OCT+ on a held step go back to STEP's NOTE, and that is remembered |
| lists, TEMPO, SCOPE, MASTER, the mixer's knob set | the cursor row / line / set, as before | SONG's SAVE-held SELECT shortcut still puts the cursor on the scene playing |

Not remembered, on purpose: the performance layers (one page, their map), NAME, a held modal. Tests:
tests/ui_optimist_pagemem.h (leave and return on each screen; SOUND per track; the engine-change fallback; the held-step
page reopened per track, EXTRA, and its fallback; the lists keep their cursor); tests/ui_optimist_test.c's `reset_ui`
clears the memory, as a power-on.

**2026-10-13: wide values in the GLOBAL list (the user: "Auto: Int", "seq/key", "notes, clock")**. op_cells.c `value_long`: the list (op_gldraw.c `gl_text`) and the tempo panel (op_laydraw.c) write SYNC AUTO as *Auto: Int* / *Auto: USB* / *Auto: TRS* (the clock followed, G_MIDI) and MIDI OUT / IN as *Keys* *Seq* / *Notes* *Clock*; the cards and param_format keep the 5-character *A:INT* ..., *KEYS* ... A fixed SYNC still reads INT / USB / TRS. There is no Both, Other or All option (bp_set.c: two options each, one settings-word bit each), none was added. GLOBAL's MIDI page is CLK IN (SYNC), CLK OUT, MIDI IN, MIDI OUT in one page; GLOBAL's Tempo is BPM NUDGE SWING; PLAY's TEMPO page keeps SYNC as its 4th card. A fixed SYNC reads *Int* / *USB* / *TRS* in the list (cards INT / USB / TRS). "Auto: Int" follows the clock live (G_MIDI = clock_sync.c's source; the line's signature holds its text, tempo_sig holds G_MIDI).

**2026-10-13: action cards, and PROJECT's keys** (feat/action-cards; the user, from screenshots). An action card's big value is the **verb itself**, never "YES": the small label above is its object (`cell_t.obj`: PROJECT, SNAPSHOT, USER, SOUND, PART, SONG, PATTERN, KIT, VOICE, DEVICE, SLOOP, ACID LINE), the verb is its `val` when set (CLRSQ shows CLEAR, PANEL shows CALIBRATE over PANEL) else its label; the narrow list columns keep the label. CLEAR, ERASE, NEW, INIT and DELETE are drawn in the alarm colour (`cell_t.danger`, C_ERR). **Running one always asks** (the modal, SAVE yes, HOME no, 3 s), changed work or not: PROJECT LOAD / SAVE / NEW, SNAPSHOT, USER, SOUND INIT and SAVE AS, TOOLS, the KIT page's SAVE / ERASE / RESET, SONG SAVE / INSERT / DELETE, FM6's STORE / SEND / INIT, the BLE devices' CHOOSE / CONNECT (the modal puts "Connect?" over the device's name as advertised, "Connect device?" when none is known). STEP's REVERSE / RANDOM / COOK show their verb but run at once, no question: undo covers them; the smarter asks of the other paths (PRESETS over edits, a scene load over a changed work) are unchanged. A knob turn only picks an action card, it never runs or arms it (the old "twice" scheme belongs to the SLOOP UI). Help lines say "(asks)", never "(twice)". **On PROJECT the note keys pick the slot** (2026-10-14: and run the action on it, the SLOT / LOAD / SAVE / CLEAR cards gone: section 4.6) and play nothing (`ly_lock` = LY_STEP, `prj_key`): on PROJECT, SNAPSHOT and USER the 16 white keys pick the cursor row's slot, key n = slot n (USER: the n-th of the 16 slots of the page the cursor is on, U01..U16 or U17..U32); lit: a used slot full, a free one dim, the selected one blinking (`prj_keys_lit`); SHIFT + a drum key no longer picks the lane there (section 24b). Leaving PROJECT restores the keys. Tests: tests/ui_optimist_actcards.h.

## 24b. Pages with no track: the note keys do nothing (2026-10-13)

The user: "global pages dont play tracks. period." (the same day, extending the first ruling, which named GLOBAL, SYSTEM and
PROJECT): "on pages that are global, save/load, glo, system, the keyboard notes keys should not do anything. context has
no track on these pages". The keys of **GLOBAL, SYSTEM (the BLE devices list too), PROJECT, MASTER FX, SCOPE and SONG** play no sound, send no
MIDI note, light nothing, pick no lane (SHIFT + a key) and record nothing. In `ui_input` the ISR's `ly_lock` is
LY_STEP on these pages (the keys reach the UI only, seq.c `key_down` makes them KS_UI), `op_drain` drops their presses
(`keys_none`, op_input.c) and `keys_lit` leaves them dark (op_keys.c). The exceptions:

- **PROJECT's slot rows** (PROJECT, SNAPSHOT, USER): a white key picks the slot (`prj_key`) and lights as the slot's state
  (used full, free dim, the selected one blinking); not with SHIFT. The TOOLS, SLOOP and MEMORY rows stay dark.
- **A held layer** is not the page: SAVE (the scenes), FX, MIX, SCL, LFO held show their screen over these pages and
  their keys are theirs (SAVE held + a key launches the scene), because `lay.held != LY_PLAY` unlocks the keys.
- **SONG's PATTERNS row** (patterns builds) keeps its keys: they launch the selected track's patterns, SAVE + n stores, SHIFT + n
  clears (the LFO layer's keys). SONG's other rows are silent.
- **TEMPO** (PLAY held): a white key taps the tempo and plays nothing (it is the MIX layer's key).
- **NAME** types with the keys, as before.
- A key down before the page changed keeps its kind (`kb_kind`), so its release is the right note-off (or, for a UI key,
  a harmless step release): no stuck note across a page change.

The pages whose keys still play: the mixer (SHIFT + key mutes the sequencer), SOUND (SHIFT picks the lane on the
drum track) and the drum lane pages; STEP's keys are the steps (the drum track: SHIFT picks, SEQ held clears; a synth track: SEQ held plays and picks).

## 27. The uniform-UI rulings (feat/uniform-ui, 2026-10-13)

The user's rulings on the uniformity audit (`optimist-work/uniform/AUDIT.md`, `DECISIONS.md`). Kept as deliberate exceptions, unchanged:
PRESETS on STEP scrolls the 16-step window; HOME tapped on the mixer opens SCOPE; SHIFT + ENV = INIT on a synth; ALGORITHM to MASTER on a
non-track page opens the master FX screen (SCOPE excepted, below); OCT on GLOBAL's first page nudges the clock; SAVE held + a key launches a
scene and SAVE + REC + a key stores it; a held step clears with HOME tapped and with SHIFT + its key; the EDIT layer's knobs rewrite the
pattern; the upper keys 1-4 are T1..DR under different held buttons; SONG's PATTERNS-row knobs launch, REC on a scene row stores.

Changed:

| # | Before | Now | Where |
|---|---|---|---|
| 2b | on STEP's roll OCT moved a view offset beside the keyboard octave | **one value**: `song.octave` is the keyboard octave and the roll's view (the fit moved by it, kept inside 0..127); both OCT: octave 0, the fit | op_stepdraw.c `roll_range`; `st.roll_oct` gone |
| 7a | the REC ring and SHIFT + REC (a modal) cleared a track | **the REC ring alone**; SHIFT + REC does nothing, except on a SONG scene row where it still clears the scene (asked) | op_combos.c `home_combo`; `ARM_TRACK` gone |
| 9 | the SOUND row's ENGINE card loaded the engine's defaults, SHIFT + PRESETS its first preset | **both: the next engine with its FIRST preset**; one edited-sound confirm ("Discard T2 edits?") for both | op_screens.c `snd_turn` calls `op_engine_step`; `snd_engine_step`, `EK_SNDENG` gone |
| 16a | SONG MODE row, KNOB 2 toggled SONG REC | KNOB 2 only **picks** the REC card (an action card: ARM / STOP); **YES arms it** through "Record over song?" (over a stored song of parts). SAVE + REC unchanged | op_song.c |
| 10 | GLOBAL / SYSTEM: PRESETS set the cursor line and ran an action line | **PRESETS does nothing there** (SHIFT + PRESETS neither): a knob is line k, SHIFT + knob its default, **YES runs an action card** (CALIBRATE asks) | op_input.c `op_presets_turn`; `gl_presets` gone |
| 12 | SCOPE: SELECT picked the source (MASTER T1 T2 T3 DR) | **the source is ALGORITHM's**: the selected track (a drum lane: DR), **MASTER = the master output** (`ui.master`; ALGORITHM left of T1 stays on SCOPE, the one exception to the master-FX jump; SCOPE's badge is SCOPE, not the track's, but its source is the track, so it is a track page for ALGORITHM (section 28); opened from the mixer's MASTER row it starts on the master). SCOPE has one page, so SELECT has nothing to move and the header shows no n/N; the tabs under the trace stay as the read-out | op_scope.c `scope_row`, op_walk.c, op_input.c `op_enter` |
| 13 | on the drum track an upper key played and picked the lane on its left | **only the 16 white keys play and pick lanes**; the upper keys are silent (UI=1 only: seq.c `key_down`, op_input.c). SHIFT + upper keys 1-4 on the mixer (sequencer mutes) and GLO + upper keys (FX bypass) are layer keys and stay; the ERASE layer on a drum track is unchanged | seq.c, op_input.c |
| 15 | patterns layer (LFO held): black 6 + n store, 7 + a, b copy, 8 + n clear | **LFO held, then SAVE + key n stores** the working pattern into slot n (asked over a used one), **SHIFT + key n clears** it (asked), as on SONG's PATTERNS row. Copy = launch the source (key n), then SAVE + the target key. Black 6 / 7 / 8 do nothing; 1-4 (track), 5 (stop), 9 (duplicate), 10 + n (scene) stay. SHIFT + LFO still locks the layer, decided when one of the two is let go: if a slot was cleared with SHIFT, no lock. SAVE then LFO is still SAVE + LFO = save the sound (press LFO first) | op_layers.c `pat_layer_key`, op_combos.c, op_keys.c |
| 1 (accident) | on STEP SHIFT + PRESETS changed the engine (branch order) | **does nothing** (no engine change, no scroll); PRESETS alone still scrolls the window | op_input.c `op_presets_turn` |

Not done / left as is: a locked patterns layer (SHIFT + LFO) is let go by SAVE or HOME like any button, so SAVE + n / SHIFT + n need LFO held;
the SHIFT + upper key lane pick off the mixer also does nothing now (a white key still picks).
Tests: tests/ui_optimist_uniform.h (every item: the old behaviour gone, the new one there, the kept layer meanings), and the tests that
encoded the removed behaviour were rewritten: ui_optimist_cards2.h (the roll), ui_optimist_step35.h, ui_optimist_test.c (the confirm idiom,
the patterns layer, SCOPE), ui_optimist_risk.h (SHIFT + REC), ui_optimist_rechold.h, ui_optimist_glwalk.h and ui_optimist_glo.h (PRESETS on
GLOBAL / SYSTEM), ui_optimist_mixer.h (SCOPE), ui_optimist_algotrack.h (SCOPE's MASTER), ui_optimist_cardspic.h.

## 28. ALGORITHM does nothing where there is no track (fix/algo-none, 2026-10-14)

The user: *"it makes no sense to change track when i am on a page that has no track"*. Section 26 made ALGORITHM walk on every screen;
that goes. One predicate, `algo_has_track()` (op_input.c, next to `keys_none()`), asked by `algo_walk` (op_walk.c) before anything moves.
It uses the header's classification (`HEAD_TRK_SCR` / `HEAD_TRK_LY`, op_draw.c: the badge only where the page's values belong to the track),
so the badge and the walk agree, with two additions: the mixer (its rows are the tracks) and SCOPE (its badge SCOPE, but its source follows
ALGORITHM).

| Where | ALGORITHM |
|---|---|
| the mixer, SOUND (and its lanes), STEP, SCOPE | walks (MASTER T1 T2 T3 DR, the 16 lanes), as section 16 |
| GLOBAL, SYSTEM (their lists) | **scrolls the cursor** a line a detent, across the pages (the page follows), stopping at the ends (op_global.c `glo_walk`; **reverses this section's earlier rule for these two screens only**, 2026-10-14, section 31). No track, no screen, no setting changes; a layer held over them keeps the layer's rule |
| the BLE devices list, PROJECT, SONG, TEMPO (PLAY held), NAME, an asked question | **nothing**: no track, no screen, no cursor, no setting |
| the master FX screen opened by the FX button or the mixer | **nothing** |
| the master FX screen reached by the walk (left of T1 on SOUND / STEP) | walks: right goes back to the page, so the jump has a way out (`fx_by_walk`, op_walk.c; cleared by any `op_enter`) |
| layers over a page: EDIT (erase), SCL (scale), ENV on FM6 (operators), STEP | walk, the layer's ui.master as section 26 |
| layers: FX, ARP (roll), GLO (mix), SAVE (scenes), LFO (all patterns) | **nothing** (the header's badge is the layer's name, not a track's: filter, dust and duck are the song's, the others are every track's) |

The jump to the master FX screen from PROJECT / SYSTEM / GLOBAL / SONG / SCOPE / TEMPO (op_walk.c) is gone with the walk there. Judgment
call: the master FX screen is inert when opened by FX (no track); only when ALGORITHM put you there does it keep the walk, else the jump
from SOUND / STEP would be a dead end. FX held on the mixer's MASTER row still shows the master cards.
Tests: tests/ui_optimist_algotrack.h (`at_inert_tests`: PROJECT, SONG, the master FX; `at_replacement_tests`: GLOBAL and SYSTEM scroll), ui_optimist_algo.h (SCOPE, the EDIT / SCL layers), ui_optimist_master.h (the FX layer).

## 29. The ACCENT lane (feat/accent-ui on feat/drum-accent, 2026-10-14)

The user's ruling: "it should work like any other drum track, it is a drum track. just no audio output, no synth engine. but everything
else is the same". The drum track's 17th step lane (core.h `DRUM_ACC`, a silent lane whose steps raise every hit of the step by the
ACCENT amount `P_DACC`, default 27) is a lane of this UI: `DRUM_UILANES` (17 with the lane built, `ACC_UI`), `lane_selected()` 0..16.
- **Key**: the LAST upper key (key 25, the lone F# at the right end of the panel's 3-2-3-2-1 groups; `DACC_KEY`; it was upper key 1 until 2026-10-14) is the
  lane's key: SHIFT + the key (STEP too; not on the mixer), or the key played, picks it; played it records, erases and rolls the lane, silently
  (`DACC_KEY_ON`, seq.c `kb_drum_lane`, drums.c `key_of_lane`). SEQ + the key on STEP does nothing (the ACCENT lane has no step key). The other
  upper keys, key 1 again included, stay silent. Held-layer meanings: key 1's are unchanged (SHIFT alone on the mixer: T1's sequencer mute; GLO: T1's FX bypass;
  LFO patterns: select T1; ENV on FM6: OP1). The last upper key has none of its own anywhere else (the patterns layer's black 11 is unused, GLO's
  bypass keys are 1-4, FM6's end at key 22), so no clash; on the mixer SHIFT + it is the ACCENT lane's sequencer mute. The pick's key LEDs dim it too.
- **STEP**: a 17th row "AC" under the 16 sounds (12 px rows), edited as any lane's (tap, level, ratchet, SHIFT + step). The held step's
  first page is LEVEL RATCHET ACCENT (no TUNE / DECAY: no sound); KNOB 3 locks the ACCENT amount on the step (needs the accent hit there,
  as a lane's locks need the lane's: "NO HIT ON THIS STEP"; the hit removed takes the lock). The amount records as motion (YES: a lock <-> a hold). STEP's LANE page of the lane shows the amount alone. TOOLS and EUCLID work on the lane.
- **The amount** is the SOUND row's fourth card next to KIT / LANE / SRC on every lane, and the lane's mixer row's ACCENT card (that row: MUTE, ACCENT; section 32).
- **Pages**: the lane has no SOUND, SOURCE, KIT, ENV or LFO pages (`sound_page`); `lane_snd()` guards every per-lane sound array (dl.*, the
  names); PRESETS on it steps the kit as on DR; the SOUND row's picture says "+amount to the hits".
- **ALGORITHM** walks on past the 16th lane to the ACCENT lane (the mixer's row `MXR_ACC`: a lane's card layout, MUTE's slot empty until the lane's sequencer mute lands (core follow-up), LEVEL = the ACCENT amount, no CUT / sends / sound, across the foot of the
  SOUNDS view, `MXA_H`); the header badge says ACCENT. The row's MUTE is the lane's sequencer mute (seq_mute.c `SMUTE_ACC`; since 2026-10-14 also SHIFT + the last upper key on the mixer; SHIFT + key 1 stays T1's), and the ERASE /
  REPEAT layer tiles stay the 16 sounds (the last upper key still erases / repeats the lane).
- **Help lines** (tools/param_help.json, ops SOUND / STEPS "ACCENT"; the mixer row's LEVEL "Accent amount added to hits": "Velocity added on accent step"; the lane's: "Raises every hit
  of its step"), shortened to the 29 characters of the line.
Tests: tests/ui_optimist_accent.h.

## 30. A picked lane shows the lane's values; the track's own are marked DR (fix/lane-values, 2026-10-14)

The user: "on SEQ while i edit the BD lane, i think the fx sends are the ones from the main drum track, not from the lane; when i
switch drum lane the value remain the same; remember on a lane i want to see the lane". The FX family's SENDS (and INSERTS) were the
drum track's (`dsend_kit`, the bus), the same under every lane. The rule, on every page reached while a lane is picked (`lane_picked()`,
the ACCENT lane included): **a value the lane has of its own is the lane's; a value only the track has stays the track's and is tagged DR.**
- **The lane's pages** (op_cells.c `LANE_FX_PG`, this UI's own, like the lane's LFO; only with a lane picked, never on DR): **INSERTS**
  (the sound's own DST, CMP, DRIVE: always heard) and **SENDS** (its REV DLY CHO, those the master slots hold, 0..31 shown in %: `lane_send_vid`; section 32). They open the FX family,
  ahead of the track's. DRIVE records as the lane's motion (`lane_value_rec`); the sends, DST and CMP are patch values, as on SOUND 3 and the mixer's lane rows (no lane event
  exists for them; the lane's LFO has no send destination either), so the card edits what it shows. The ACCENT lane has no sound, so neither page.
- **The track's, kept and tagged**: the drum bus's inserts are "DR INSERT", the kit send levels "DR SENDS" (the lane pages took the plain names), then BUS SENDS, SLICER, FILTER, GATER, CMP;
  PATTERN (LEN DIV SWG GATE; the STEP screen's first page too); the SOUND row's KIT card and the KIT page's SLOT / SAVE / ERASE (RESET is the lane's). Every such card carries a small **DR**
  top right, amber (`cell_t.dr`, op_draw.c; in the card signature). The mixer builds its own cards and has no tag.
- **DR itself picked**: the track's values as before, nothing tagged, no DR rows, the names plain.
- Already the lane's, unchanged: SOUND, SOUND 2, SOUND 3, SOURCE, ENV, LFO, STEP's lane page (LEVEL TUNE DECAY REV), the held step's cards and locks, the mixer's lane rows.
  Step-wide values (NUDGE, chance, fill, condition on a held step) belong to the step, not to a lane.
Tests: tests/ui_optimist_lanevals.h (two lanes picked on SOUND and from STEP, the sends of each shown and edited alone; the DR tags; DR itself).

## 31. The settings lists' look (feat/settings-look, 2026-10-14)

The user, from a screenshot of "Global channels 5/5". Section 20's lists (GLOBAL, SYSTEM; op_gldraw.c) are redrawn:

- **Header** (2026-10-14, the user: *"move back the title in the top bar"*, reversing the first version of this section): the badge GLOBAL / SYSTEM, then the page's title and n/N as on every screen ("Channels 5/5", the n/N grey; op_draw.c `head_other` takes the name from `gl_ptitle`), then bar.beat and BPM, which always stay. A title short of room takes its short form from the names table (op_names.c `NM_SHORT`: "Screen & audio" -> "Scr & aud", "Connections" -> "Connect"), never a cut. **There is no title line under the header any more**: its room went to the lines.
- **Page title**: the section's name, or a joined pair's (above); the n/N is dropped on a screen of one page.
- **The 4 lines** share the room between the header and the help line evenly (`gl_pitch`); a page of fewer lines keeps that pitch. Name left, value right, the value's form under it.
- **Value forms**: a ranged value is a bar 4 px thick over the screen's width (6..234), the trough a line; a multi-choice list (Auto: Int / USB / TRS, Keys / Seq, Notes / Clock, COLOR) is a segmented bar, one segment per choice, the chosen one lit; an **on / off value (ON, OFF; the radio's LINK, NO CAL) has no bar but a square mark at the right**, filled when on, outlined when off, in the line's colour (a pill is too thin at this size). Read-outs keep their bars, actions their verb.
- **The cursor line is inverted** (2026-10-14, the user: *"invert the highlight"*): no coloured background; the line's text (the number, the name, the value and unit) is drawn in the line's colour (the knob's with KNOB COLORS, else the track's) on the normal background, the other lines keeping grey names and white values. The bar, the on / off mark and the segments are as on every line (the cursor's in the line's colour). **The other lists keep the full-colour cursor background (section 34): the two list styles now differ** (open: unify?).
- **ALGORITHM scrolls the lines** one a detent across the pages (section 28); knob k still edits line k of the page, SELECT pages, SHIFT + a knob resets.
- **Joined pages** (op_global.c `GL_JOIN`, one table): SYSTEM's SCREEN and AUDIO share one page, "Screen & audio", and USB and BLUETOOTH share "Connections", when together they are 4 lines or fewer in the build; otherwise each keeps its own pages. The BLE line's YES still opens the devices. A page is one section or one joined pair (the layout test allows exactly those).
- **Keys**: the LIGHTS section is "Keys", its lines BUTTONS (the button glow OFF / LOW / MID / HIGH) and KEYS (OFF / C / WHITE / ALL); the settings word is unchanged. SYSTEM's order: Info, Screen & audio, Keys, Connections, Calibrate, About.
- **INFO** (SYSTEM's first page, replacing CPU): one dense read-only page that is not 4 lines, **text only: no bars** (op_gldraw.c `gl_info_draw`: the name left, the value right, a unit after it; no line is the cursor, no knob does anything). Its lines, in order (op_project.c `sys_info_cell`): CLOCK; CPU0 (LOAD); the 2ND CORE line (always there: `2ND CORE ON` + the CPU1 load %; `2ND CORE OFF` + why in amber, from `dual_why_name`, until power-off; `2ND CORE` + `NOT IN BUILD` in a DUAL=0 build); BATTERY (the last base line: n/3 and the raw ADC count, `NO READING` before the first sample, no volts or percentage); then, where the build has them, LAST RESET (builds with DBG_MAGIC: how the boot before ended, POWER ON / CRASH / WDT / SOFT / KEY / LOW V, how long it ran, the crash pc or the stage), the second core's four lines in a DUAL build (CPU1 JOB/REWAKE: jobs in thousands / rewakes; CPU1 STOP: when and where CPU1 stopped; CPU1 SLEEP or SPIN: its stage and counts; CPU1 FAULT: the fault, or the IC word), and in a USB audio build the USB AUDIO lines (UA U/O/M/B: underruns / overruns / missed frames / bad packets; UA RX/FB: packets received in thousands / the feedback in Hz; UA LOW/GAP: the ring's lowest fill / the longest gap between USB polls in us / the gaps over 900 us; FLASH OFF: the longest interrupts-off window of a flash write, us). The old FLASH, RAM, POOL and RAM CODE lines with their bars are gone from this page (where the memory goes: docs/MEMORY-MAP.md).
- **Line numbers**: the GLOBAL and SYSTEM lists draw no number on a line (op_gldraw.c `gl_line_draw`: the name, the value, the bar or the check box under it). A line's knob (1 to 4 on its page of four) shows only in its colour (`knob_col`, the KNOB COLORS setting).
- **2ND CORE is gone**: the second core is always on, `dual_mode` stays AUTO. The guard in dual.c still parks the core on a misread, a canary, a late job or a fault and INFO's CPU1 line says why. With no menu to set it back (`DUAL_MODE_MENU` 0), the self-disable lasts for the session and is not saved; a stored OFF from an older build is dropped when the Optimist UI loads its settings; a boot that crashed in its first 30 s still stays on one core.
- **About** (3 lines, one page): VERSION (and the commit it was built from, `-DFELUCCA_COMMIT` from tools/build.py), BUILD (date and time, the compiler's), REPO (github.com/w0ts/optimist).
- Help lines: KEYS / BUTTONS "Buttons glow in the dark", KEYS "Which keys glow", INFO, BUILD, CHIP, REPO (tools/param_help.json; the 232 px limit shortened the user's wording).

Tests: tests/ui_optimist_glstyle.h (header: the badge, the title and n/N whole, bar.beat; no title line; pitch, bars, marks, joined pages, INFO, About); ui_optimist_curline.h (no fill, the line's colour on the cursor's text); ui_optimist_algotrack.h (the scroll); glpages, glo, glwalk, core and pages updated.

## 32. The names table (feat/ui-names, 2026-10-14; the coherence audit's batch A)

The audit (optimist-work/visaudit: STYLE.md, FINDINGS.md) found titles cut per engine ("ENV de", "LFO2 D", "Patter"), labels
cased by length, stored names re-cased, one thing under several names (DST for the distortion and for the LFO's destination;
SLD, ACC, R2..R4; FILT / Filter / Trk flt; CMP / COMP; TRN / Transp; DTN / DTUNE; SWG / SWING; SCL / SCALE) and list cells cut
at 4 letters (ANAL, DIGI, CLEA, ERAS, Inse, Dele). One table now names every thing (ui/optimist/op_names.c, flash only),
in three forms, looked up when drawn; what the cells, rows and help lines key on stays the shared name (params.c, the
engines, tools/param_help.json), so SLOOP's UI and the web editor are unchanged.
- **Titles** (`nm_title`): a row's name in the list is the header's title, sentence case, at most 9 characters, chosen so
  that it fits whole with its n/N next to the widest badge of its screen ("2 DIGITAL", "4 ACCENT"); the header keeps
  bar.beat (the user's ruling). Renamed: ENV 1 MOD *ENV mod* (ENV DEST *ENV to* until 2026-10-16), ENV2 DEST *ENV2 to*, LFO DEST *LFO to*, LFO 2 *LFO2*, LFO2 DEST
  *LFO2 to*, INSERTS *Insert* (as the mixer's set), SCL *Scale*, FLT 2 *Filter 2*, DR INSERT *DR ins*, BUS SENDS *Bus send*,
  CMP *Comp*, MASTER's RET DELAY / RET REV *Delay* / *Reverb*, FM6's OPn LEVEL *OPn out*, OPn RATES *OPn rate*, OPn LEVELS
  *OPn level*, OPn KEY SCA *OPn scale*, OPn CURVES *OPn curve*, PIT RATES / LVLS *Pit rate* / *Pit level*; the rest in
  sentence case as named. A track's page (SOUND, STEP: the badge names the track) shows the title alone, never the screen's
  word; GLOBAL / SYSTEM's header says only *Global* / *System* (section 31); the other screens keep the screen's word
  (*Master ins tone*, *Project tools*): since batch D (section 33) the screen's word is their badge and the title the page's name alone. GLOBAL / SYSTEM's line names follow the card rule: capitals (KNOB COLORS, CLK IN, LOWCUT).
- **The header's room**: the badge's text 2 px inside it, the title 5 px after it, its room ending 1 px before the REC
  mark; SOUND's whole list (every family, ALGORITHM back from MASTER) counts a row within its family, as its page button
  shows it ("Voice 4/5", not "Voice 17/25"). A title is cut only where the table fails (`head_finish` says so; the test
  finds none). STEP's window names the pick only when the badge does not ("4 DRUMS Steps 1-16 kick", "4 SNARE Steps 1-16").
- **Card labels** (`nm_card`): capitals, 6 characters at most (an action's object 8): VOLUME / LVL LEVEL, DST DIST (the
  insert; its amount is named as the type, as FILTER's), DST A / B DEST A / B (also a lane LFO's DEST / AMT), CMP COMP, FILT /
  FLT FILTER, PIT PITCH, SHP SHAPE, TRN / TRNSP TRANSP, DTN / DTUNE DETUNE, RES RESO, SWG SWING, SCL SCALE, QNT QUANT, ACC
  ACCENT, SLD SLIDE, DRV DRIVE, LENGTH LEN, RATCHET RATCH, GLD GLIDE, MOTN MOTION, ENRGY ENERGY, COLR COLOR, the returns
  REV RT / DLY RT / CHO RT, BPM TEMPO (its unit says BPM), DIGITAL's R2..R4 RAT2..RAT4 (FM6's R1..R4 are rates), TRK FLT
  FILTER (the FX layer: the track's tag in the unit's place, beside the master's FILTER), ACID LINE LINE, GLOBAL's CLK IN
  SYNC (as TEMPO's card; the value: A:INT on a card, "Auto: Int" in the lists and the tempo panel).
- **List cells** (`nm_cell`): engines ANLG DGTL PHSE LOFI SMPL VOX WHL GRN FM6 PHYS ACID, verbs LOAD SAVE NEW INIT CLR ERS
  RST INS DEL (SAVE AS: SAVE), DIST, COMP, USED / - for a scene. The 5-letter names of SLOOP's lists (BLUES, REUSE, L1 RT ...)
  still show their first 4 letters when picked: the cell's width is batch B's.
- **Values and units**: the drum track's LEVEL in dB (its linear gain read through LEVEL_Q12's half-dB steps), a lane's
  sends in % (0..31 of the range), a step length and a pattern length in *steps*, the DJ filter "LP20" with no unit, a
  leading zero (0.24). The ACCENT lane's mixer card is ACCENT (a velocity amount, not a level).
- **One send order**: REV DLY CHO on SENDS, BUS SENDS, a lane's SENDS, MASTER's RETURNS (display only: the stored values and
  SLOOP's pages are untouched; SOUND 3 keeps the FX slots' order).
- **Stored names** are drawn as stored: the header's preset and the track flash, the preset list, the PRESETS toast, NAME's
  field, FM6's voice name, the mixer's lane titles. **Modal** questions are one sentence (*New project?*).
- **STEP TOOLS**: REVERSE, RANDOM, COOK show their verb alone in the large face (the title says Tools); a 7-letter verb is
  drawn large from the card's edge.
Tests: tests/ui_optimist_names.h (every screen, engine, the drum track and each lane, STEP's pages and a held step's, the
mixer's sets, the layers: no title cut at the widest tempo, every card label in capitals and short, no list cell cut;
NAMES_DUMP=file lists them all).

## 33. Layout and empties (feat/ui-layout, 2026-10-14; the coherence audit's batch D)

The audit's items #7, #11, #21, #22 (optimist-work/visaudit: STYLE.md sections 1, 5, 6; FINDINGS.md). The Optimist UI only:
SLOOP's UI and the shared tables (params.c PAGES, the descriptors' lists) are unchanged.
- **One rule for "nothing"** (#7, style guide 5-6). A knob with no parameter is its frame alone: no label, no value, no
  dash (a value a page does not show, an empty slot's amount, a value a lane's sound or the build has not, SCOPE's PAN and
  DIST on the drum track, TEMPO's MODE and START without REC MODES, the mixer's empty cells). A parameter with no value
  says one "-" under its label: an empty step's NOTE, LEVEL, RATCHET and LENGTH (were C-1, blank cards and "1 steps"), a
  scene's track with no pattern (was "--"), the ACCENT amount's lock on a step without the accent hit. A slot with nothing
  in it says **EMPTY, dim**: an FX slot's or an insert slot's TYPE (was "----"), a kit SLOT (was "--"), a scene without
  PATTERNS (STORED / EMPTY); a list cell shows it "-" (the names table). op_cells.c `cell_param`, `cell_empty_slot`.
- **A badge on every screen** (#11, style guide 1). Track pages keep "<n> <ENGINE>" in the track's colour; every other
  page's badge is the screen's name in C_HI: MIX (the mixer's track and lane rows), MASTER (the master FX, the mixer's
  MASTER row, MASTER in a layer: the layer's name is the title), PROJECT, SONG, TEMPO, SCOPE, GLOBAL and SYSTEM (the badge
  alone: the list draws the page's title big, section 31), NAME (the slot the title: "U03", "Project 3"), a layer that is
  not the track's (its name: FX, REPEAT, MIX, SCENES, PATTERNS; the title "Locked" while the layer is locked (lay.lock)). The title is then
  the page's name alone, never the screen's word again: *Ins tone 3/8*, *Tools 4/5*, *Scene A 1/22*, *Rec 2/3*, *T1*
  (SCOPE); a page whose own name is the screen's keeps it (*Project 1/5*, *Tempo 1/3*). The mixer's track rows keep the
  row's name and the set (*T1 mix 1/4*); its MASTER row's sets are *Ins tone*, *Comp*, *Returns* (as the master FX's
  pages). bar.beat and the BPM stay. A track change's name flash (TRACK 2 ...) is drawn in the track's colour on every
  screen but MASTER's (there the MASTER badge stays: the audit's #8 in part). op_draw.c `head_other`, `head_make`.
- **No page of one card** (#21, style guide 6), each merge listed:
  - FLT 2 (ANALOG's FTYP alone) is OSC 2's fourth card: OSC 2 = WAVE2 · SEMI · SYNC · FTYP; no FLT 2 row (op_cells.c
    OP_OSC2, `merged_away`).
  - FILTER (the track filter's amount alone, on the synth and the drum track) has no row: INSERTS shows the same value, the
    amount card of the slot that holds the filter (S2 FILTER · FILTER by default). The brief put it on SENDS' free card;
    a third place for one value, an insert among the sends, was not taken.
  - TEMPO without REC MODES: QUANT is REC's first card (MODE's, empty there): two pages, Tempo and Rec. With REC MODES
    Tempo (BPM NUDGE SWING SYNC) and Rec (MODE LENGTH START CLICK) are full and QUANT keeps its page *Rec 3/3* (op_tempo.c
    `tps_*`; GLOBAL's lines are unchanged).
  - The ACCENT lane's STEP page (its amount alone, *Accnt 2/4*) is gone: the amount is the SOUND row's ACCENT card and the
    mixer's ACCENT row, a held step's lock is on the held step's cards. STEP on the ACCENT lane: Pattern, Tools, Euclid.
  - REVERB's TYPE (builds with two reverb algorithms or more, while a slot holds the reverb) is RETURNS' fourth card on the
    master FX, after REV DLY CHO RT: no *Ret type* page (op_screens.c `fxs_at`).
  - Kept, a full family page beside them: SCL 2 (TRANSP alone) without CHORD+ (SCL is ROOT SCALE QUANT CHORD), TEMPO's
    REC 2 with REC MODES. Kept, groups of their own of two or three cards: PROJECT's Memory (USED FREE), SENDS / BUS SENDS
    (REV DLY CHO), ENV dest, CMP, the mixer's sets.
  A row that goes takes the page memory with it: a screen's row past its end is its last (op_input.c `op_rows_fix`,
  SOUND's `snd_mem_row`).
- **What the build lacks never shows** (#22): without FX_DUST (or DUCK, DJF) the mixer's MASTER set has no DUST card (was
  DUST 0%), the FX layer's and a MASTER layer's DUST knob is its frame alone, the master FX's INS TONE puts the values the
  build has first (DUCK FILTER ROLL) and the empty frame last (was an empty first card). The master FX's RETURNS, like the
  mixer's, shows the returns of the sends a slot holds (op_screens.c `fxs_at`, op_cells.c `g_built`).
Tests: tests/ui_optimist_layout.h (every screen, engine, the drum track and each lane, STEP's pages and a held step's empty
and set, the mixer's sets, the master FX, PROJECT, SONG, TEMPO, SCOPE, GLOBAL, SYSTEM, the layers plain and on MASTER: a
badge, the title not repeating it, the empty rule, no one-card page but the two kept, no DUST unbuilt; the merges);
run_tests.sh runs it once more without DUST built.

## 34. Colour means something (feat/ui-colours, 2026-10-14; the coherence audit's batch C)

The audit's items #26, #27, #39, #47 and the amber / green checks (STYLE.md section 7). Colour choices only: no geometry.
- **Mute tiles** (#26): the GLO layer's MUTE T1..T4 tiles are outlined (black, a band in the track's colour, as the SOLO
  tiles) while the track is heard and filled in the track's colour only when it is muted (they were filled when nothing
  was muted). op_laydraw.c `tiles_fill`.
- **Red only destroys or replaces work** (#27). One predicate per action, shared by the card's verb and the modal's frame
  (a modal that is not red keeps the amber frame: pending until YES):
  - SONG REC is never red: the card's verb and the song layer's REC read-out are green while recording, amber while
    armed; the "Record over song?" modal (a stored song of parts) is the red one. The header's REC mark follows
    (`rec_col`: recording green, waiting for the note amber). op_cells.c `rec_col`, `srec_col`, op_draw.c `act_col`.
  - PROJECT / SNAPSHOT / USER (`prj_danger(kind, action, slot)`, 2026-10-14: the ACTION card's value and the modal): SAVE and CLEAR are red over a slot that holds something and neutral
    into a free one; LOAD is red when the work is not as stored (it replaces edits), neutral otherwise; NEW is always red;
    IMPORT is red on the card too (as its modal). The card and the modal agree (the card's SAVE was white over a used slot).
  - GO buttons on the pages (`go_danger`): every one is red but SAVE (a kit into a slot) and the ACID line's GEN; the
    modal now follows the card (SAVE and GEN asked in red before). FM6: STORE and INIT red, SEND neutral, card and modal.
  - Unchanged and right: SOUND INIT red, SAVE AS neutral (a new preset), PART DELETE red / INSERT neutral, the scene /
    pattern STORE (asked only over a used slot) and CLEAR red, DISCARD, FORGET red, CONNECT / CALIBRATE neutral.
- **Dim is free / unused** (#39): the SONG SCENE value and the SLOT cards were already dim only when nothing is stored
  there; what was dim for another reason is grey: a list row's action verbs (LOAD SAVE...: labels). Units and the empty
  step marks stay dim (typography of a value, an empty cell). Kept as unused: scale degrees outside the scale, steps the
  probability cuts, a muted operator or sequencer lane.
- **Amber is changed / pending / gain reduction / a notice** (C_WARN). The palette's fourth step (C_AMB, "secondary
  text") is not amber: a read-out (RO cell) is C_GRAY in cards and the settings lists; the state marks that meant amber
  now are C_WARN (SERIAL pending a restart, BLE NO CAL, CLK OUT blocked, the 2ND CORE OFF reason, a hold event's arrow,
  the step lock's hold arrow); the drum track's "DR" tag and a device's status tag are grey.
- **SUPER** (#47): SUPER's colour moves from #da775d to #c653af (a magenta, hue 312) so ANALOG (34), the drum coral (11) and
  SUPER differ by >= 15 degrees of hue and SUPER holds white text at 4:1 (it is >= 15 degrees from every engine and kit
  colour). tools/colors.json and web/editor.html (the web editor's test checks them equal); SLOOP's UI draws the same table.
Tests: tests/ui_optimist_colours.h (mute tiles; every action card walked against the list of what may be red, the SONG
REC colours, SAVE red exactly over a used slot and the modal equal to the card; dim only on free / unused; no cell in the
palette's secondary step; the three hues).
## 35. Geometry (feat/ui-geometry, 2026-10-14; the coherence audit's batch B)

The style guide's sections 3, 5, 8 and 9 (optimist-work/visaudit/STYLE.md) as drawn. UI=0 unchanged.

- **The help band** (#6, style guide 9). A build with the help line (PARAM_HELP) gives the screen's last 18 rows
  (y 222..239) to it, **whether the line shows or not** (op_graph.c `OP_FOOT`, `OP_FOOT_Y`; `OP_PH` ends there). Reserving it
  always was picked over making room only while the line shows: the line comes for ~2 s on every detent, and the list,
  the mixer's rows and the pictures would jump each time. Every screen's content ends above it: the list (five rows of 19
  px under the 2x2 cards, `ROW_H`; 20 without the band), the pictures, the mixer's rows (36 px channels; nine lane rows),
  STEP's grid (the lanes 10-11 px, the marks and the playhead strip at y 218..221; the roll as tall as fits), GLOBAL /
  SYSTEM's list (its lines where they were), a layer's tiles (19 px rows, the state line under them), FM6's ENV held map,
  the modal (its *Home no / Save yes* from the box's foot), the REC ring (28 px over the band), the BLE devices. Nothing
  else ever draws in the band: the help line no longer clips the bands (gfx.c `cv_clip` is gone) nor redraws them when it
  goes, it clears its band. A build without the help line has no band: everything runs to the foot as before.
- **Units in the label row** (#12, style guide 5). Every card, the mixer's 1x4 cards too: the label at the left, the unit
  right-aligned in the label row, never after the value (so a long value never drops it: *-10.0* and *dB* both show). A
  value that carries its unit (the compressors' ATK / REL *10ms*, a list of names) shows its number on the card and the unit
  above it (op_draw.c `card_unit`); the lists keep *10ms* whole. On a 57-px card a label and its unit need 3 px of air: the
  two that cannot (LEVEL + dB, REV RT / DLY RT / CHO RT + %) take their narrow form there, LVL / REV / DLY / CHO
  (op_names.c `NM_NARROW`): the label gives way, never the unit. Lists: the unit right after the number, no space; GLOBAL /
  SYSTEM's lines lose their 4-px gap (*90BPM*, *0%*; the one change to the settings look, by that rule).
- **The list's columns** (#23 and batch A's cut 5-letter cells, style guide 3). A row's name has its column (9 characters
  from x 4), then a character's gap, then the four cells at x 84 + 38 k (four characters and a gap). A text longer than its
  place reflows its row: it keeps its place and pushes the cells after it right (6 px apart at least), or a long last one
  pulls the ones before it left, while the row fits; only a row that cannot hold every text whole gives a character, the
  longest text first, its names-table form first (op_draw.c `row_layout`). *BLUES*, *REUSE*, *L1 RT*, *L2 AMT*, *100ms*,
  *1/16D* show whole; the columns stay put on every row that needs no room. No more *Ret delay8T*.
- **STEP's held-step cards** (#16): the three rows between the header and the cards (y 25..27) are cleared when the cards
  come up: no grid fragments behind them (op_stepdraw.c `step_frame`).
- **FM6's operator map** (#25): the boxes are as tall as the rows allow but never less than their number (12 rows: the
  digit's 10 of ink inside, centred); rows too short for that draw the numbers alone, in the boxes' colours, never a box
  over its number. ENV held's map: the voice and the page on the first line, the algorithm (*Alg n*, *Car Fb n* on its
  top line), the black keys' map at the foot (op_fm6draw.c `F6_*`); every algorithm's four rows fit at 14 px.
- **The FX picture** (#31): a SOUND FX page's picture is its own four values, read from the page's cells, in the cards'
  order (SLOOP's `fx_needle` read SLOOP's order: CHO DLY REV under REV DLY CHO), the needles placed for the 2x2 cards
  under each card's column (KNOB 1 x 32, KNOB 3 x 90 | KNOB 2 x 150, KNOB 4 x 208) and named with their card's label. An
  FX page with no picture of its own (COMP, SLICER) shows its own needles, never a neighbour's (op_graph.c `graph_fx`,
  `snd_graph_page`). An insert's TYPE card has no needle.
- **The mixer's ACCENT lane** (#34): a ninth row across both columns, as tall as the lanes' (146 / 9 px), its steps over
  the width at a channel's pitch, no meter (it has no sound) (op_mixdraw.c `mx_rect`, `MXS_NROW`).

Tests: tests/ui_optimist_geometry.h: no blit reaches the help band but the line's own in the whole run (every test and the
20000 random frames; counted in the harness's lcd_blit) and the band black on every screen and layer with the line off;
every mixer card's unit beside its label; *10ms* split on the card; every list row's name column and cells apart and inside
the row; the held-step strip black; the FX needles the page's values, placed and named; the ACCENT row full width; FM6's
boxes for every algorithm. tests/ui_optimist_names.h: every value of every list parameter laid out whole in its row.
Updated for the band: the mixer's row height, STEP's foot, the SOUND picture's foot, GLOBAL's foot, the BLE code's place,
the help line's going.

## 36. Per-screen oddities (feat/ui-screens, 2026-10-14; the coherence audit's batch E)

The last batch of the audit (optimist-work/visaudit/FINDINGS.md), by the style guide. UI=0 unchanged.

- **The header's title is the cursor row's name** (#2, style guide 1). Verbatim, its continuation number kept: *Edit 1 2/7*
  on the row Edit 1 (was *Edit 2/7*: the row's number had been swapped for the page's, D11 of 2026-10-12, so the header and
  the list disagreed); the n/N is drawn grey, a read-out, which keeps the two numbers apart. GLOBAL's second REC page is
  *Rec 2 3/5* (was *Rec 3/5*). One counter: the n/N. **bar.beat always stays** (the user, 2026-10-14): a title short of
  room takes a deliberate short form, never a mid-word cut, the n/N whole (op_draw.c `head_short`): a continuation's
  number joins its word (*Voice2*, *Edit1*), then the names table's short title (op_names.c `NM_SHORT`: *Pat*, *Scl2*,
  *Vox2*, *Kit snd*, *Bus snd*, *L2 to*, *E2 to*, *Slcr*; only where the widest badge, a three-digit tempo and the n/N leave less).
- **STEP's header** (#17): the page SELECT is on and its n/N, cards up or not (*Pattern 1/19*, a lane's name on a drum lane's
  row), and bar.beat as on every screen (was *Steps 1-16 C4*, no n/N, no bar.beat). The window stays the playhead strip's
  (its page lit in the gutter); the pick moved to the grid: the drum track's selected lane is lit (as before), a synth's
  pick is a white mark at the roll's gutter edge on its notes' rows (op_stepdraw.c `sg_roll`).
- **The drum lane under a held step's cards** (#15): with the cards up, the grid shifts so the selected lane's row is the
  first under them, if it was hidden (behind the cards, or under the held step's two lines); the rows above go under the
  cards, those past the grid's foot are not drawn; the grid comes back in place with the cards' going (op_stepdraw.c
  `sg_shift`, `sg_dy`). Shifting was picked over drawing that one lane's row apart: the lanes round it stay in view, and
  the grid stays one drawing.
- **The drum track's page names** (#19, #20): EDIT's are *Lane* (KIT LANE SRC ACCENT), *Body* (TUNE DECAY SNAP CLICK),
  *Tone* (BEND CUT DRIVE LEVEL), *Sends* (the lane's), *Kit*, where all were *Sound*; FX's two send pages are *Kit sends*
  (the kit's send levels, every lane's; *Lane sends* is past the titles' 9 letters) and *Bus sends* (the drum bus's own),
  on a picked lane too, next to its own *Sends* (was *DR sends*). Titles only (op_screens.c `dr_alias`, `snd_title_row`): the
  shared names the help line keys on stay.
- **A layer's hint** (#24): *Home locks it*, once a power-on per layer, shows ~2.5 s on the help line (op_help.c; a build
  without the help line: on the state line under the tiles), never in the header, which keeps the layer's badge (FX, ARP...).
- **The drum ENV's length** (#32): in the picture's label row, before the source's tag (*KICK  505ms SYN*), no longer on the
  baseline over the curve.
- **A long name on a 1x4 card** (#33): the mixer's SOUND / KIT card, a name longer than 6 letters with an empty card after it,
  is drawn two cards wide, the name whole in the small face (*808 BOOM*, was *808 BO*; op_draw.c `card_wide`).
- **The mixer's ACCENT row** (#35): *Accent*, no set word, no n/N (it has one set; was *accent mix*). A row of one set is its
  name alone.
- **The SOUND row in the list** (#36): the preset's name over the ENGINE cell's place too (the badge names the engine), then
  INIT, then SAVE AS in its fixed form SAVE (was *808 ANLG INIT SAVE*); a name of 10 letters or more keeps its room whole and
  SAVE AS stays the card's alone. The cards keep all four (op_screens.c `snd_list_cells`). The list's reflow now takes a
  text's fixed form (*SAVE AS* -> *SAVE*) before it cuts any text.
- **PROJECT** (#43): TOOLS' *MISS* card is *MISSING* (the names table; its 2x2 card holds it), MEMORY's *MORE* is *FREE*.
- **GLO tapped** (#45): section 4.11 now says what is built: GLO switches GLOBAL and SYSTEM (it said a tap walks the rows).
- **The ARP picture** (#46): no text line (MODE and RATE are the cards'; the notes are the guide lines).
- **NAME** (#49): its hints (*Save done  Home delete*) on the help line's place, the screen's last 18 rows (they were at the
  field's top); the field keeps its length (*9/12*).
- **GLOBAL's REC LENGTH**: *1* with the unit *bar* (was the value *1 BAR*), style guide 5.
- Already done by the earlier batches: #8 (D), #14 (the send order), #38 (an empty held step's NOTE is *-*), #48 (A's
  RAT2..RAT4).

Tests: tests/ui_optimist_screens.h (each item, the old gone and the new there); ui_optimist_pages.h's header walk checks every
SOUND row's title against the list's; the tests that held the old titles, STEP's window header and the layer hint follow.

## 37. Favorites (feat/op-favorites, 2026-10-15)

> **Superseded the same day by the end of this section**: the FAVS row described first was removed; the gestures are SHIFT2 + PRESETS (mark) and SHIFT2 + SELECT (browse only favorites). Read the last paragraph first. "HOME held" in this section is SHIFT (OCT- held) since 2026-10-14.

The favorites store, its saving in the settings record and its older-record upgrade are shared with SLOOP's UI (builder item
FAVORITES, core/favorites.c, storage/persist_rec.c: a factory sound by engine and name, 64 marks, a user preset by slot, 32).
This UI's gesture is a **row of its own under the SOUND row, FAVS, on a synth track**: SOUND's four cards are all taken
(PRESET, ENGINE, INIT, SAVE AS), and a held button or a shift layer is out (section 2.1), so the two values go on a row's
cards as every other value does. **KNOB 1 FAV**: ON / OFF for the selected track's sound, a set (right marks, left clears; a
user preset by its slot; "--" and dim where a sound has no mark: the FM6 / CZ collections); YES on the card toggles it; **HOME
held + the knob clears it** (section 2.1's clear). **KNOB 2 LIST**: ALL / FAV, YES toggles, HOME held + the knob is ALL; with FAV
the PRESETS knob, the SOUND row's PRESET card and HOME held + PRESETS (the next instrument, among the marked ones) walk only the
marked sounds, round; from a sound that is not marked the first step goes to the first (right) or last (left) marked one; none
marked: the message *NO FAVORITES* and the sound stays. The row has the SOUND row's picture (the sound before, the one playing,
the one after, each with its engine), so the sound is in front of you while you mark it; **a favorite has a star after its name**
in that picture, so every browse shows it, and with LIST FAV and a sound not in the list the playing sound stays in the middle
with the last and first favorite around it. The drum track has no FAVS row (kits are not marked). The mark is a settings change:
written once the audio is quiet (settings_later), never on a detent; LIST starts at ALL after a power cycle. The row is the
EDIT family's second (SOUND, FAVS, then the engine's pages), so a page button tapped again reaches it before EDIT 1 (the EDIT
tests' tap counts follow); a family button (ENV, LFO, FX, SCL, ARP) shows its own rows only, without it. Options weighed: **(1)**
the row (built: nothing existing changes meaning, both values are on the knobs the grammar gives a row); **(2)** FAV on the
ENGINE card (that card is the same as HOME + PRESETS, a ruling of 2026-10-13) or on SAVE AS (SAVE + EDIT does it too) and LIST
in SYSTEM: fewer rows but removes a visible action and splits one feature over two screens; **(3)** a gesture, SAVE held + PRESETS
marks and HOME held + PRESETS clears: no row, but PRESETS under HOME is already the instrument step, and it hides the feature.
Tests: tests/ui_optimist_favorites.h (the row, the cards, FAV and LIST by knob, YES and HOME, the walk and its edges, the star, the
user preset, the drum track); the EDIT tap counts in ui_optimist_mixer.h, _cardspic.h and _pages.h follow the new row; a build
without FAVORITES runs the whole test again (tests/run_tests.sh).

**2026-10-15, no preset rows (the user: "I don't want preset everywhere. We have a preset button, that is enough").** The SOUND row
(PRESET ENGINE INIT SAVE AS) and the FAVS row are gone from EDIT; a synth track's rows are its pages; the drum track's first row is the
lane's SRC and ACCENT. Favorites now live where presets are picked: **SHIFT2 (OCT+ held) + PRESETS** marks the sound playing (right) or
clears it (left), toast "FAV + NAME" / "FAV - NAME"; **SHIFT2 + SELECT** browses only the favorites (right, "FAVORITES ONLY") or every sound
(left); a PRESETS toast puts a star after a favorite's name; **PROJECT > USER: SHIFT2 + the ITEM knob** marks the slot (a star after its
name in the info). SHIFT + PRESETS is still the engine step. INIT is PROJECT > USER's 4th card, SAVE AS is USER's SAVE. The store and the
list filter are unchanged; the drum track has no mark.

## 38. Overload monitor; INFO split in three (feat/overload-monitor, 2026-10-15)

The user: "we should have actually a MAX of the last few seconds and a DROP-OUT DETECTOR"; "the second encoder should switch
between CURRENT, MAX OVER A FEW SECONDS, MAX FOR EVER. And we should also monitor UNDERRUNS"; "the 1st system screen would gain
to be split in two pages with easier-to-read text" (three pages are fine).

- **INFO** (SYSTEM 1/n), what a musician reads, text only: CLOCK; the CPU line (`LOAD`, `CPU0` in DUAL builds): KNOB 2 turns its
  measure NOW (the smoothed load) -> MAX 5S (the longest audio half of the last ~5 s, its wait for the interrupt included) -> MAX
  (since boot or the last clear), round; the label says which (`CPU0 MAX 5S`); % of a half, over 100 when a half ran late, in the
  load colours. The 2ND CORE line follows the same measure while the core is up (`CPU1 MAX 5S`). DROPS: late audio halves (gaps in
  the sound), green 0, red when one came in the last 5 s, else amber. UNDERRUNS: everything else lost (USB audio's playback / capture
  under- and overruns and missed frames, MIDI in that found its ring full: USB, TRS, BLE), the total and, dim, where ("USB3
  MIDI1"). GUARD (CPU_GUARD builds): the guard's level (OFF, QUALITY, UNISON, REVERB, NOTES), `MAX n` while it caps the voices,
  `-n` the voices shed. BATTERY.
- **SHIFT + YES on INFO** clears what the page shows: DROPS, UNDERRUNS, the MAX, the voices shed (display baselines; the
  counters go on, the console reads them since boot). On INFO it takes the place of the redo chord.
- **The header's rule** (its bottom line, 2 px) turns red for ~1 s after a drop-out and amber after another underrun, on every page.
- **INTERNALS** (2/n): LAST RESET, FLASH IRQS OFF (the longest interrupts-off window of a flash operation and its kind, erase / write / read; amber past an audio half; a sector erase runs in ~1 ms windows, hal/fm1_flash_slice.h),
  and in DUAL builds CPU1 JOBS DONE, CPU1 RE-WAKES, CPU1 LAST STOP (NEVER | `<t> IDLE` | `<t> NOT TAKEN` | `<t> IN JOB n`, unit the
  job's index), CPU1 GOT TO (how far it got), CPU1 HEARTBEAT (unit: its wakes), CPU1 FAULT (NONE, or the code and the pc).
- **USB STATS** (3/n, USB audio builds): USB UNDERRUNS, USB OVERRUNS, MISSED FRAMES, BAD PACKETS, PACKETS IN, RATE ASKED (Hz, the
  feedback to the computer), LOWEST BUFFER (frames), LONGEST GAP (us between USB polls), GAPS > 0.9 MS. One value a line, words
  and units, no `a/b/c` values (before: `UA U/O/M/B`, `k/n`).
- Row names keep to 9 characters (rows_ok), so the pages are INTERNALS and USB STATS (not "Second core" / "Diagnostics").
- Code: system/overload.h (the ISR's two stores a half, the per-second maxima rings for CPU0 and CPU1, the MIDI loss counters),
  op_project.c (the cells, info_cell's pages), op_gldraw.c (the pages drawn whole), op_global.c (KNOB 2, SHIFT + YES), op_draw.c
  (the rule). Tests: ui_optimist_core.h overload_tests, glo / glwalk / glstyle updated. The guard's side: docs/CPU-GUARD.md "Spikes".

## 21. The transport LEDs (feat/play-led, 2026-10-16)

The user (2026-10-16): "there IS a green led below the play button ... i think we should put green in play mode and red in record mode."

**What the hardware has.** The LED matrix is 4 LED lines (PA5..PA8) x 11 columns, one single-colour LED per site; 41 sites carry a key (FM1_KEYMAP) and three have none (row PA5, columns 8, 9, 10). **PLAY has a second LED, green, at the keyless site PA5 column 8** (row bit 1): found on a unit by MAKU (okumurari2/maku, `firmware/src/ui_input.c` `LED_PLAY_GREEN`, "found on the hardware at column 8, row PA9 (bit 1)"; it lights it green while playing and on each count-in beat) and drawn by the FM-1 emulator simonjohansson/fm1-emulator (`fm1-panel.c`, `docs/architecture.md`: "PLAY's extra green LED sits at column 8, row 1, where no key does"; REC's LED is red). Measured here: stock V15 lights that site at idle (about 20 %, like the buttons' glow). The LEDs are not RGB or bi-colour: a colour is a site. REC's own LED is the red one, so red needs no new site. SMK-37's PLAY (a different device) has green and red; not the FM-1. Not found: any firmware driving a red lamp under PLAY; columns 9 and 10 stay dark in stock. The web search found nothing on the FM-1's LEDs.

**States** (`lamp_on`, op_input.c; one bit in `ui_leds`; the PLAY button's own light and REC's are unchanged):

| State | Green (PLAY's second LED) | Red (REC's LED) |
|---|---|---|
| stopped | dark | half lit |
| playing | **steady** (no blink: the PLAY button's own light flashes the beat) | half lit |
| REC armed, stopped | dark | blinking |
| count-in (START = COUNT) | a pulse on each of the four beats (as MAKU) | blinking |
| recording (live, overdub) | dark | **full** |
| free take | dark | full |

Never both at once. SLOOP's UI (ui/sloop/ui_input.c) has its own `ui_leds` and is unchanged. The web editor does not render the LEDs and is unchanged. Tests: tests/ui_optimist_lamp.h (every state, and PLAY / REC taps). The legend is in docs/guide/interface.md. Credits: LICENSING.md.

## 39. Bluetooth and Devices pages (feat/ble-ui, 2026-10-16)

The user (2026-10-16): "we still don't have a BLE UI to connect ... you are still supposed to implement it in optimist",
"it is more a matter of wiring/adapting what we have". What was there: one SYSTEM line, BLE ON / OFF; the devices list
(op_project.c dev_*) opened only by YES on it, which nothing said (no DEVICES line; the Synth profile has no help line),
while every other on / off line toggles on YES. Now two pages of plain lines, read from io/midi/ble_devices.c (no new
radio setting):

| Page | Line | What it is |
|---|---|---|
| Bluetooth | BLUETOOTH | ON / OFF (NO CAL in amber); the knob, or YES, as every on / off line (YES no longer opens the list) |
| | STATE | read-out, ble_dev_status in words: OFF, STARTING (ON, the radio not started yet), ADVERTISING, SCANNING (the list open), CONNECTING, SEARCHING (for LAST), PAIRING, CONNECTED (green), FAILED (red, kept until the user acts), NO CAL (amber) |
| | DEVICE | read-out: the device of the link (connecting, pairing, connected, searched for): its name as advertised; HOST when a computer or phone connected to the FM-1 (its name is not known); NONE |
| | MIDI | read-out: IN / OUT / IN OUT for 0.3 s after a message passes over Bluetooth (midi_ble.c's ring counts), else -- ; green while lit |
| Devices | DEVICES | action SCAN: YES opens the list (unchanged: NONE, LAST, nearby with signal bars; its header now says Devices) |
| | RECONNECT | the list's NONE / LAST as a line: OFF, or LAST's name; right LAST, left NONE, YES the other (ble_dev_pick: the same as picking the row); no LAST: NO LAST DEVICE. BLE_CENTRAL builds |
| | LAST DEVICE | action FORGET (alarm colour): asks "Forget <name>?", YES forgets it (ble_dev_forget); none: NO LAST DEVICE. BLE_CENTRAL builds |

STATE, DEVICE and MIDI draw their value in the large face (FONT_L) when it fits beside the name, else the list's small
face (the user's complaint about small text on INFO); a cell's `big` flag (op_cells.c) asks for it (op_gldraw.c). In the
list, LAST is tagged PAIRED when its keys are kept (BLE_DEV_BONDED). The two pages follow SYSTEM's page rules (section 20):
"Bluetooth" and "Devices" are two sections; with USB SERIAL (CDC) the Connections join no longer fits one page, so USB
stands alone. GL_MAXL is sized from the build (SYSTEM's lines, at least 24: Synth 24), and the MIDI line's counters share the devices list's drawing state (bdp), so the pages cost no main RAM. The editor's SCRN command hands the new
lines out as it does every SYSTEM line (descriptor-driven; YES on DEVICES from the editor opens the list on the device,
where SCRN then answers nothing until it closes, as before). Tests: tests/ui_optimist_ble.h ble_page_tests (the lines, their
order and pages, every state, MIDI activity and its timeout, RECONNECT both ways and without LAST, FORGET asked, HOME no,
YES yes, none left), ble_sys_walk (SYSTEM page by page drawn, opt-ble-sys-*), and the tests that opened the list from the
BLE line now open it from DEVICES (glo, glstyle, pages, glwalk).
