# SLOOP UI changes (batch 31)

What changed in the SLOOP UI (builder UI=0, the default) in this batch, as built. The Optimist UI (UI=1) gets only the knob colours.

## Mixer (TRACKS)

- The rows are T1, T2, T3 and DR, then the drum track's 16 lanes. Four rows show at a time, and the list scrolls one row at a time, so the selected row is always in view. The header says where you are ("Tracks", "Tracks, L1-2", "Lanes 3-6").
- Each row has its tile, the instrument and engine name (or the lane and its sound source), a thin VU line under the name with the compressor's gain reduction pushing in from the right, and the 16 steps in view with the playhead.
- ALGORITHM walks the rows. A turn going down stops on DR, and a turn going up through the lanes stops on lane 1 (kick). Only a fresh turn crosses: the knob must rest 350 ms first. A lane row selects the drum track and that lane, silently (the mixer never previews).
- The four dials are VOL, INSERT, SEND and PAN of the selected row (a track, the drum bus or a lane). INSERT is the first insert effect in the FX slots' order (DIST, COMP, FILT). SEND is REV when it is in a slot, else the first send. Swing moved to the tempo page and steps to the SEQ page.
- GLO + white key 4 or key 8 on a lane row mutes or solos that lane. On a track row they work as before.
- Each lane has its own pan, mute and solo (drums/drum_mix.c). They are saved in the FX record (TLV 0x40). Older projects load with every lane centred and heard, and older firmware skips the record.

## Drums

- Drum steps on the keys (builder DRUM_STEP) is now on by default.
- SEQ held on the drum track: the 16 white keys pick the lane (the layer map shows the lanes). Steps go in on the DRUMS grid.
- ALGORITHM on the DRUMS grid walks the lanes. A turn going up stops on the kick, and a fresh turn after that stop goes to T3. Walking down stops at the last lane.
- Picking a lane (SEQ + key, ALGORITHM, KNOB 1, EDIT + key) and entering a step on the grid play the lane's sound only while the transport is stopped. While playing they are silent.
- The picked lane's row on the grid is tinted in the drum colour, and its number and name are in the header.
- Grid KNOB 3 on the cursor's step: up sets the step, then ratchet x2 to x4; down removes the ratchet, then clears the step.
- With the automation store built (the CHANCE, MOTION, SL24_XSTEP, MICRO, FILLS, PLOCK switches), a grid step key held past the hold time is a held step for the picked lane: KNOB 4 nudge, PRESETS lock value, ALGORITHM lock parameter, KNOB 2 chance, KNOB 3 ratchet, OCT+ fill, OCT- clear. Several keys held edit together. A tap still sets or clears the step. The held column is outlined in red.
- Drum chance is on STEP 2 (every lane of the step). The SEQ + SELECT gesture is gone.
- OCT- on held steps clears everything the store holds there, recorded motion included. The step's mark is one dot for any event, plus the fill marks.
- A full pattern (128 events) says "Pattern full: 128 events".
- FOLLOW works in the SEQ layer on every track: while playing, the page follows the playhead. A page key or SEQ + OCT- / OCT+ turns it off, and the fifth black key (D#) toggles it.

## Tempo page

- Turning SELECT where it sets the tempo (TRACKS, DRUMS, REC) changes the BPM and shows the tempo page. A held layer button or a page where SELECT pages shows no page. With BPM LOCK, SELECT alone shows no page.
- On the page: KNOB 1 BPM, KNOB 2 swing, KNOB 3 sync (INT, USB, TRS, AUTO), KNOB 4 a read-out of the nudge.
- OCT- / OCT+ held on the page nudge the clock 3.9 % slower or faster. The BPM itself does not change, and the nudge ends when you let go. There is no octave step, and on the drum track no ghost or hard hits. While an external clock is followed there is no nudge ("NUDGE: EXTERNAL CLOCK").
- The page closes 3 s after the last SELECT, knob or OCT, or at once on any other button press, which then does its usual job. Playing keys keeps it open.
- PLAY acts at once on the press, as before.

## HOME, undo and redo

- HOME tapped (released before the hold time) acts on the release: it goes home, and on TRACKS it opens the scope.
- HOME tapped twice (the second press within 300 ms of the first release) opens the SYSTEM menu, or closes it. The first tap has already acted.
- HOME held past the hold time is a shift. Let go with nothing else pressed, it does nothing, except that a held layer button then locks (as with a tap).
- Undo: SAVE pressed, then HOME while SAVE is still held. Redo: HOME pressed, then SAVE while HOME is still held. Each further press of the second button is another level. Neither button does its own job in the chord. Both pressed in the same frame do nothing. EDIT + OCT- / OCT+ still undo and redo.

## Knobs held with a layer button

- A knob must move two detents (net, from the press) before a held layer button counts as used. One detent of jitter neither shows the layer's map nor takes the tap away.

## Scope

- HOME tapped on TRACKS shows a slim scope: the master mix as a standing wave. HOME again or any page closes it. It is on whenever the full visualiser (VIS) is not built, and VIS is off in user-default. Its 512-frame ring is in the pool (VIS's ring moved there too).

## Knob colours

- HOME menu > SYSTEM, the HOLD screen, row 2: KNOB COLORS, off by default. OCT+ toggles it, and KNOB 2 right turns it on, left off. It is kept in the settings word (bit 25) and is not carried into a SLOOP 2.4 export.
- When on, everything a knob drives takes that knob's cap colour: 1 blue, 2 yellow, 3 pink, 4 orange. This covers the labels, values, gauges and big values, and every dial strip (mixer, layers, held steps, drums, FM6, tempo page).
- Optimist UI: the COLORS cell on SYSTEM > SCREEN.

## User rulings this batch follows

- Mixer row: SLOOP's row with a thin VU and an opposing GR bar. The bottom dials are VOL / INSERT / SEND / PAN.
- The drum lanes come after DR in the mixer. Stop at the edge (lane 1 up, DR down), and only a fresh turn crosses. The fresh-turn gap is 350 ms.
- A lane preview plays only when stopped, and never from the mixer.
- The tempo page has BPM, swing, sync and nudge (OCT, 3.9 %).
- Undo and redo: EDIT + OCT stays, plus SAVE-then-HOME and HOME-then-SAVE.
- Knob jitter must not count: two detents before a held button shows its map.
- In confirms, SAVE is yes and HOME is no. UI words are in sentence case, and short labels stay in capitals.
- Flash is never a reason to drop a feature. Sizes are reported instead.

## Open

- What HOME shift does beyond the layer lock and redo is undecided. Today it has no functions of its own.
- The mixer's GR bar comes from each row's own COMP insert. A row without COMP shows no GR.
- On TRACKS, the first tap of a HOME double tap opens the scope, so the SYSTEM menu opens over the scope.
- The ramtext budget is nearly used. The UI=0 user-default build has 31,712 of 32,512 B (800 B left; it was 31,144 before this batch). With the store's switches on it is 31,668 B, and with UI=1 31,492 B. The next feature that puts code in RAM may need something moved out.
