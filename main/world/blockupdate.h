#pragma once
// =====================================================================
//  SynthMiner  --  scheduled block updates
// ---------------------------------------------------------------------
//  THE MACHINE THAT MAKES PHYSICS CHEAP. Water flows, sand falls, and
//  one day a crop ripens -- and the world is a hundred thousand
//  resident cells, almost none of which are doing any of that. So
//  nothing here ever SCANS. A cell is looked at only because something
//  asked for it to be, and a cell that settles asks for nothing and
//  costs nothing from then on. That is the user's requirement, in one
//  sentence: "once water blocks reach a steady state, we can basically
//  stop physics calculations for them, until something around them
//  changes."
//
//  TWO TIERS, AND THE SPLIT IS BY HOW FAST THE THING IS:
//
//    1. THIS FILE -- fast, ticking, sub-second. Fluids, falling blocks.
//       A delay of a few ticks, a wheel of buckets, and an explicit
//       set of cells waiting their turn.
//    2. THE LAZY CLOCK (world/blockent.h, game/furnace.h) -- slow,
//       minutes and hours. A furnace, a trashcan, a crop. Those keep a
//       `stamp` and are worked out from `now - stamp` the moment
//       anybody looks, so a furnace in a chunk nobody has visited costs
//       exactly nothing and comes back right after a week.
//
//  The second tier is the better one wherever it fits, and it is why a
//  growing wheat field will never enter this queue. Water cannot use
//  it: where the water GOES depends on the shape of the world at every
//  step, so it has to be walked rather than extrapolated. The user drew
//  the same line: "We don't have to do that for fluids, but doing it
//  for plants, animal growth and machines take time to work would make
//  it feel much more natural."
//
//  --- WHAT IS ACTIVE, AND WHERE THAT IS WRITTEN DOWN ---
//
//  One bit per cell, in a fourth chunk plane (chunk_t.act). Set means
//  "this cell is in the queue". It answers the user's question -- does
//  this block have physics going on -- in one PSRAM read, and it is
//  what stops the queue filling with duplicates: a cell with four
//  neighbours changing at once is scheduled once, not four times.
//
//  It is DERIVED, like light, and for the same reason it is never
//  saved: the fluid LEVELS are in the state plane and they are the
//  truth, so the active set can always be rebuilt from the world. See
//  blockupdate_chunk_join() -- that is the whole of the load path, and
//  there is no section in the chunk format for any of this.
//
//  --- CHUNK AND REGION BOUNDARIES (the user, 2026-09-28) ---
//
//  Fluid crosses chunks, and a chunk is the unit of residency, so this
//  is the one hard part of the design.
//
//  REGIONS DO NOT MATTER. A region is how chunks are grouped into files
//  (world/region.h) and nothing else; two chunks either side of a
//  region boundary are neighbours like any others.
//
//  CHUNKS MATTER TWICE.
//
//    * FLOWING OUT. A cell in a chunk that is not resident reads as
//      BLK_BARRIER (chunk.h, D-14), which is solid and not replaceable,
//      so water simply stops at the edge of the loaded world. That is
//      the correct thing to do and it needs no code -- but it means the
//      flow is INTERRUPTED, not finished, and something has to notice
//      when the world catches up.
//    * ARRIVING. So when a chunk becomes resident, the seam is woken:
//      the new chunk's own unsettled fluid, AND the facing border
//      columns of the four resident chunks beside it, which may have
//      been sitting against a wall that has just turned back into
//      water. Without the second half, water poured near a chunk edge
//      stops in a straight line for ever and the player sees a wall of
//      water with nothing holding it up.
//
//  This is exactly the shape light already has -- light_chunk_local on
//  the worker, light_chunk_join on the main task as the load lands
//  (world/light.h) -- and blockupdate_chunk_join() is called from the
//  same place in chunk_worker.c's apply(), for the same reason.
//
//  --- THREADING ---
//
//  MAIN TASK ONLY, all of it. Updates run inside the 20 Hz tick, they
//  write resident chunks through world_set(), and Part K's ownership
//  contract says only the main task may do that. This must NOT be
//  moved to the chunk worker on core 1 without changing that contract
//  first: the worker writes a chunk's planes only while it is
//  CS_LOADING and nothing else may look, which is the opposite of what
//  a fluid tick needs.
//
//  Pure: no engine, no RTOS. The one allocation goes through psram.h.
//  tools/worldcheck.c builds and drives it as-is.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/chunk.h"

// --- Sizing -----------------------------------------------------------

// Ticks of horizon. A delay is clamped to this, which is safe because
// everything in tier 1 is fast by definition: water is 5 ticks and lava
// 30. Anything wanting longer than three seconds belongs in tier 2.
#define BU_WHEEL 64

// Cells that may be waiting at once, across the whole resident set.
// Overflow is COUNTED AND REPORTED, never silent -- a dropped update is
// a puddle that never finishes, and this project has been bitten once
// already by a queue that refused work without saying so (F-113).
#define BU_MAX 4096

// The most cells one tick will look at. A bucket emptied onto a plain
// wakes a few hundred cells at once and the frame still has to be
// drawn; the rest are carried to the next tick, which costs a fiftieth
// of a second of settling and keeps the frame rate flat.
#define BU_PER_TICK 512

// --- Life -------------------------------------------------------------

bool blockupdate_init(void);
void blockupdate_shutdown(void);

// Forget everything waiting. For a world being closed or swapped: the
// title screen's water must not still be flowing into a real world.
void blockupdate_clear(void);

// --- Asking for an update ---------------------------------------------

// Look at (x, y, z) again in `delay` ticks. Doing it twice is free: the
// cell is already marked and the second call returns at once, leaving
// the time already asked for in place.
//
// A cell in a chunk that is NOT RESIDENT is silently not scheduled. It
// has nowhere to keep its bit and nothing could run there anyway; the
// chunk arriving is what picks it up (blockupdate_chunk_join).
void blockupdate_at(int32_t x, int32_t y, int32_t z, int delay);

// The cell and its six neighbours, each at its own block's rate -- what
// a block changing means. Only cells whose block actually has rules are
// enqueued, so digging out stone in dry ground puts nothing in the
// queue at all.
void blockupdate_around(int32_t x, int32_t y, int32_t z);

// Is this cell waiting for an update? The user's "does this block even
// have active physics going on", and what the debug overlay draws.
bool blockupdate_active(int32_t x, int32_t y, int32_t z);

// --- Running ----------------------------------------------------------

// Everything due now. Called once per world tick, from the main task,
// and returns how many cells it looked at.
int blockupdate_tick(void);

// --- Hooks ------------------------------------------------------------

// A block changed at (x, y, z). world_set() calls this, next to
// light_block_changed() and for the same reason: it is the one funnel
// every write goes through, so nothing can forget.
void blockupdate_block_changed(int32_t x, int32_t y, int32_t z, uint8_t was, uint8_t now);

// A chunk has become resident: wake its own unsettled cells and the
// seam it shares with the chunks already here. See the boundary note
// above -- this is the half that makes fluid survive streaming.
void blockupdate_chunk_join(chunk_t* c);

// --- Counters ---------------------------------------------------------

typedef struct {
    int pending;  // cells waiting right now
    int peak;     // the most that have ever waited at once
    int fired;    // cells looked at, since the world opened
    int dropped;  // updates refused because the queue was full
    int carried;  // cells held over by BU_PER_TICK on the last tick
} blockupdate_stats_t;

blockupdate_stats_t blockupdate_stats(void);
void                blockupdate_stats_reset(void);
