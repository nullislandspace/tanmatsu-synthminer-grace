#pragma once
// =====================================================================
//  SynthMiner  --  fluids
// ---------------------------------------------------------------------
//  Minecraft's rules, as asked (minecraft.wiki/w/Fluid), driven by the
//  scheduler in world/blockupdate.h so that still water is free.
//
//  --- WHAT A FLUID CELL IS ---
//
//  A block id and its state byte, and no side record: a fluid is the
//  second tier of chunk.h's three, which is exactly what that tier was
//  described for. Inside the state byte's own 0..127 field:
//
//      bits 0-2   LEVEL, 0..7. 0 is FULL, 7 is the thinnest film --
//                 Minecraft's sense, where the number counts DOWN from
//                 the source, not up from the ground.
//      bit  3     FALLING: this cell is fed from directly above. It is
//                 full whatever its level says, and when it lands it
//                 spreads outwards at full strength.
//
//  ST_PLACED (bit 0 of the state byte) is untouched and still means
//  what it always did, so a bucket's source is marked player-placed and
//  a flow is not.
//
//  A SOURCE IS level 0 AND NOT FALLING, and that distinction is the
//  whole of why the falling bit exists. Both are full cells. But a
//  source is the thing water comes FROM and never changes on its own,
//  while a falling cell is the thing water is PASSING THROUGH and must
//  dry up the moment the column above it does. Encoding falling as
//  "level 0" alone would make every waterfall an infinite spring.
//
//  --- THE STEP ---
//
//  When a cell comes up, in this order:
//
//    1. WHAT SHOULD I BE? Anything but a source recomputes its own
//       level from its neighbours -- one more than the shallowest
//       horizontal neighbour of the same fluid, or full if the cell
//       above is the same fluid. Past the fluid's reach it dries up and
//       becomes air. THIS IS THE HALF THAT MAKES WATER RECEDE when its
//       source is taken away, and it is why nothing has to remember
//       where a flow came from.
//    2. TWO SOURCES MAKE A THIRD (the infinite spring). A non-source
//       cell with two or more source neighbours, standing on something
//       that is not itself replaceable, becomes a source. That is the
//       2x2 hole every player digs, and `infinite` in the table below
//       is what lets lava be denied it.
//    3. DOWN FIRST. If the cell below can take fluid, it gets a falling
//       cell and this step ENDS -- water does not spread sideways while
//       it has somewhere to fall, which is what makes a waterfall a
//       column rather than a cone.
//    4. OTHERWISE OUTWARDS, one level thinner, into whatever of the
//       four sides will take it.
//
//  Minecraft also has flowing water LOOK for a hole within five blocks
//  and prefer that direction. We do not, yet: water here spreads evenly
//  and finds the hole a tick later than Minecraft would. The rule is a
//  refinement of step 4 alone and nothing else would change.
//
//  --- WHAT IT LOOKS LIKE ---
//
//  EVERY LEVEL DRAWS AT FULL HEIGHT for now. The mesher is a greedy
//  quad mesher over integer slices (voxel/voxel_mesh.c) and a partial
//  height means a cell whose top face is not on a slice boundary --
//  which is not a tweak to it, it is a different mesher. So the
//  simulation is Minecraft's and the picture is not: you cannot yet see
//  that a flow is thinning out, only where it has reached. Worth doing,
//  and separate.
//
//  --- WHAT IS SAVED ---
//
//  The LEVELS, because they are in the state plane and the state plane
//  is saved with every chunk. Nothing else: the queue of cells waiting
//  is derived and is rebuilt on load (blockupdate.h). So a world put
//  down mid-pour comes back mid-pour and finishes the job, and no new
//  chunk section was needed for any of it.
//
//  Pure: no engine, no RTOS, no allocation. tools/worldcheck.c drives
//  it as-is.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/chunk.h"

// --- The state byte ---------------------------------------------------

// The bit layout lives in voxel/voxel_mesh.h, next to the torch's, and
// these are aliases for it. ONE definition: the mesher has to read the
// same field to know how high to draw the surface, and two copies of a
// bit layout is two copies of a bug.
#define FLUID_LEVEL_MASK VOX_FLUID_LEVEL_MASK  // within st_data(): 0 full, 7 thinnest
#define FLUID_FALLING    VOX_FLUID_FALLING     // within st_data(): fed from above

static inline uint8_t fluid_level(uint8_t st) {
    return (uint8_t)(st_data(st) & FLUID_LEVEL_MASK);
}
static inline bool fluid_is_falling(uint8_t st) {
    return (st_data(st) & FLUID_FALLING) != 0;
}
// FULL: a source or a falling cell. What "this cell cannot take any
// more" means, and the test every spread does before writing.
static inline bool fluid_is_full(uint8_t st) {
    return fluid_level(st) == 0;
}
// A SPRING: full, and not merely passing through. See the header.
static inline bool fluid_is_source(uint8_t st) {
    return fluid_level(st) == 0 && !fluid_is_falling(st);
}

// Keep bit 0 (ST_PLACED) and write the fluid's own field.
static inline uint8_t fluid_state(uint8_t st, uint8_t level, bool falling) {
    uint8_t const d = (uint8_t)((level & FLUID_LEVEL_MASK) | (falling ? FLUID_FALLING : 0u));
    return st_with_data(st, d);
}

// --- The registry -----------------------------------------------------
//
// A FOURTH TABLE, keyed by block id, the way biome_def_t is keyed by
// biome (worldgen.h): two blocks will ever be in it and neither wants
// four more columns in the block table that every other row leaves
// blank. Adding a fluid is a row here and a row in blocks.h.

typedef struct {
    uint8_t block;     // BLK_AIR ends the table
    uint8_t delay;     // ticks between one cell's steps
    uint8_t reach;     // the thinnest level it will make: how far it runs
    bool    infinite;  // two adjacent sources make a third
} fluid_def_t;

// The rules for `block`, or NULL if it is not a fluid.
fluid_def_t const* fluid_def(uint8_t block);

static inline bool fluid_has_rules(uint8_t block) {
    return fluid_def(block) != NULL;
}

// How long before this cell should be looked at again.
int fluid_delay(uint8_t block);

// --- Running ----------------------------------------------------------

// One step for the cell at (x, y, z). Does nothing if it does not hold
// a fluid -- which is the normal case for a cell woken by a neighbour.
void fluid_update(int32_t x, int32_t y, int32_t z);

// COULD THIS CELL STILL DO SOMETHING? The filter a chunk arriving uses,
// so that an ocean -- every cell of which is a full source with full
// neighbours -- puts nothing at all in the queue, while the lip of a
// waterfall that was cut off by the edge of the loaded world puts
// itself back in (blockupdate.h, the boundary note).
bool fluid_unsettled(int32_t x, int32_t y, int32_t z);

// --- The bucket ------------------------------------------------------

// Put a source down at (x, y, z), replacing whatever is there if it can
// be replaced. False if the cell will not take it.
bool fluid_place_source(int32_t x, int32_t y, int32_t z, uint8_t block);

// Take a source OUT of (x, y, z) and say which fluid it was, or BLK_AIR
// if that cell holds no source. A bucket only ever picks up a source --
// scooping a flow would let a player carry a puddle away one film at a
// time and is Minecraft's rule for the same reason.
uint8_t fluid_take_source(int32_t x, int32_t y, int32_t z);
