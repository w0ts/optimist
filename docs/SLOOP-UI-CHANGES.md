# SLOOP UI changes (batch 31)

What changed in the SLOOP UI (builder UI=0, the default) in this batch, as built. The Optimist UI (UI=1) gets only the knob colours.

## Mixer (TRACKS)

- The rows are MASTER, T1, T2, T3 and DR, then the drum track's 16 lanes. Four rows show at a time, and the list scrolls one row at a time, so the selected row is always in view. The header says where you are and which dial page is up ("Master 1/2", "Tracks 1/2", "Trk, L1-2 2/2", "Lanes 3-6 1/2", "Ln 13-16 1/2").
- Each track or lane row has its tile, the instrument and engine name (or the lane and its sound source), a thin VU line under the name with the compressor's gain reduction pushing in from the right, and the 16 steps in view with the playhead.
- The MASTER row has an M tile, "Master", a VU line of the output and the master compressor's gain reduction from the right. It has no steps.
- ALGORITHM walks the rows. Up from T1 is MASTER, a plain step: a fast turn up from DR runs on into MASTER. A turn going down stops on DR, and a turn going up through the lanes stops on lane 1 (kick). Only a fresh turn crosses between DR and the lanes: the knob must rest 350 ms first. A lane row selects the drum track and that lane, silently (the mixer never previews). MASTER selects no track.
- The dials have two pages. A HOME tap while TRACKS is up flips them:
  - Page 1: KNOB 1 VOL, KNOB 2 INSERT, KNOB 3 SEND. KNOB 4 is empty.
  - Page 2: KNOB 1 SEND 2, KNOB 2 INSERT 2, KNOB 3 PAN, KNOB 4 COMP.
- INSERT and INSERT 2 are the row's first and second insert in the FX slots' order (DIST, COMP, FILT). SEND is REV when it is in a slot, else the first send, and SEND 2 is the first other send in the slots' order. COMP is the row's COMP insert amount (a lane's CMP). A dial the row has no effect for shows "--".
- MASTER's dials: page 1 VOL shows the VOLUME knob's level (grey, a read-out: the hardware knob sets it). Page 2 COMP is the master compressor's THRS. The FX slots hold no master insert or send, so its other dials show "--".
- From another page HOME goes home and keeps the dial page. Swing is on the tempo page and steps on the SEQ page.
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

- HOME held past the hold time (shift), then PLAY, opens the tempo page. That PLAY press does not start or stop the transport. With a layer button held there is no page, and PLAY plays.
- SELECT only changes the BPM, as in stock SLOOP. It never opens the page; while the page is up, a turn keeps it up.
- On the page: KNOB 1 BPM, KNOB 2 swing, KNOB 3 sync (INT, USB, TRS, AUTO), KNOB 4 a read-out of the nudge.
- OCT- / OCT+ held on the page nudge the clock 3.9 % slower or faster. The BPM itself does not change, and the nudge ends when you let go. There is no octave step, and on the drum track no ghost or hard hits. While an external clock is followed there is no nudge ("NUDGE: EXTERNAL CLOCK").
- The page stays up until 3 s after the last SELECT, knob, OCT or HOME + PLAY, or until any other button is pressed, which then does its usual job. Letting HOME go does not close it, and playing keys keeps it open.
- PLAY alone acts at once on the press, as before.

## HOME, undo and redo

- HOME tapped (released before the hold time) acts on the release. On TRACKS it flips the mixer's dial page; elsewhere it goes home. This is so in every build (the visualiser no longer takes the tap).
- HOME tapped twice (the second press within 300 ms of the first release) opens the SYSTEM menu, or closes it. The first tap has already acted (on TRACKS it flipped the dial page).
- HOME held past the hold time is a shift. HOME shift + PLAY opens the tempo page. Let go with nothing else pressed, HOME does nothing, except that a held layer button then locks (as with a tap).
- Undo: SAVE pressed, then HOME while SAVE is still held. Redo: HOME pressed, then SAVE while HOME is still held. Each further press of the second button is another level. Neither button does its own job in the chord. Both pressed in the same frame do nothing. EDIT + OCT- / OCT+ still undo and redo.

## Knobs held with a layer button

- A knob must move two detents (net, from the press) before a held layer button counts as used. One detent of jitter neither shows the layer's map nor takes the tap away.

## No scope

- The slim scope screen is gone. The SLOOP 2.4 visualiser (VIS, 12 styles) is dropped from the builder for now (user ruling 2026-10-10: "We can drop the visu for now"). Its code stays in the tree (ui_vis.c and the FELUCCA_VIS core hooks, default 0; the settings word keeps bits 17..20 for 2.4's style) so it can come back with a new gesture; nothing in ui/sloop opens it. A builder config naming VIS still loads: the key is ignored with a warning. The ring is built only with the Optimist UI.

## Knob colours

- HOME menu > SYSTEM, the HOLD screen, row 2: KNOB COLORS, off by default. OCT+ toggles it, and KNOB 2 right turns it on, left off. It is kept in the settings word (bit 25) and is not carried into a SLOOP 2.4 export.
- When on, everything a knob drives takes that knob's cap colour: 1 blue, 2 yellow, 3 pink, 4 orange. This covers the labels, values, gauges and big values, and every dial strip (mixer, layers, held steps, drums, FM6, tempo page).
- Optimist UI: the COLORS cell on SYSTEM > SCREEN.

## User rulings this batch follows

- Mixer row: SLOOP's row with a thin VU and an opposing GR bar.
- Mixer dials on two pages, flipped by a HOME tap on TRACKS: VOL / INSERT / SEND, then SEND 2 / INSERT 2 / PAN / COMP.
- A MASTER row above T1. The drum lanes come after DR. Stop at the edge (lane 1 up, DR down), and only a fresh turn crosses. The fresh-turn gap is 350 ms.
- A lane preview plays only when stopped, and never from the mixer.
- The tempo page has BPM, swing, sync and nudge (OCT, 3.9 %). It opens with HOME shift + PLAY, never on SELECT.
- No scope screen.
- Undo and redo: EDIT + OCT stays, plus SAVE-then-HOME and HOME-then-SAVE.
- Knob jitter must not count: two detents before a held button shows its map.
- In confirms, SAVE is yes and HOME is no. UI words are in sentence case, and short labels stay in capitals.
- Flash is never a reason to drop a feature. Sizes are reported instead.

## Open

- What HOME shift does beyond the tempo page, the layer lock and redo is undecided.
- The mixer's GR bar comes from each row's own COMP insert. A row without COMP shows no GR. MASTER's comes from the master compressor.
- MASTER's VOL is a read-out of the hardware VOLUME knob, and MASTER has no insert or send dial, because the FX slots belong to the parts.
- A HOME tap on TRACKS flips to dial page 2 in every build.
- The ramtext budget is nearly used. The UI=0 user-default build has 31,696 of 32,512 B (816 B left; it was 31,144 before this batch). With the store's switches on it is 31,680 B (832 B left), and with UI=1 31,492 B (1,020 B left). The next feature that puts code in RAM may need something moved out.
