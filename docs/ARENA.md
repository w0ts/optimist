# The engines' arena

Each synth part (NPART = 3) keeps its engine's state in a part memory when the engine needs one: FM6, PHYS, GRAIN,
ACID, PLAITS, JUNO, DRAWBAR, CZ and SLICE. The state is zeroed when the part comes to that engine. It stays as it was
when the part switches to an engine that keeps nothing there, so switching back finds it.

## Without ARENA (the default)

`engines.c part_mem` holds NPART memories in the pool, at fixed places. Each is the size of the largest built engine's
state (the union `part_mem_t`). Nothing is ever refused, and no arena code is built.

JUNO, DRAWBAR, CZ and SLICE keep their per-part state there too (2026-10-26; before that they had static arrays in
main RAM). None of them is the largest, so the memories grow by none of them. On the reference profile that is 1.3 KB
less RAM and 144 B less flash.

## With ARENA (builder item, bit 411, KB)

The memories are placed by need in one pool region of that size (`core/arena.c`, `arn_mem`), first fit in address
order. Blocks never move: the audio and the second core read them while the main loop plans.

- **Placing a block.** The main loop reserves a block before a part's engine changes (`part_mem_reserve`, interrupts
  off). The audio takes the reservation at the switch (`part_mem_claim`, every voice of the part over).
- **Idle blocks.** A part that moves to an engine with no state leaves its block idle. An idle block is taken back
  without asking when another reservation needs the room.
- **What does not fit is refused.** The device proposes a part to drop with the one confirm, "DROP T2 FM6"
  (`model.c mem_offer`; `op_input.c mem_tick`). Parts whose memory alone would make room come first.
  - YES: that part becomes a silent placeholder, and the change is made once its switch is done.
  - Declined, and nothing else left to propose: MEM FULL.
- **Silent placeholder** (`track_t.silent`). The part keeps its sequence, plays nothing and shows NONE in grey. It is
  saved as engine byte `0xFE` (`ENG_UID_SILENT`) and stays a placeholder until an engine is picked.
- **A project load that does not fit.** Every refused part is collected. Each is a placeholder with the project's
  sound, then offered in turn on the same confirm (`mem_load_next`); placed, it keeps the project's sound.

Cost on the reference profile (2026-10-26): ARENA 32 is 1.5 KB more flash than off. It frees what the fixed memories
held beyond its size (37 KB → 32 KB there) for the undo history.

Tests: `tests/arena_test.c` (placement, refusals, what to drop; random switches), `tests/placeholder_test.c` (the
placeholder through a save and a load; the drop flow with an arena of one part's memory), `tests/backports_test.c`
with an arena of two parts' memory (FM6, PHYS, ACID swapped on a part).
