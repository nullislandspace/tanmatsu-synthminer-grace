#pragma once
// =====================================================================
//  SynthMiner  --  the Deep Mine (see claudeplans/synthminer.md, Part M)
// ---------------------------------------------------------------------
//  The second dimension: a sealed cave, one big room, normal stone,
//  occasional lava, richer iron, occasional clusters of diamond, and no
//  sky. Nothing here is "nether" -- the comparison to the Nether is
//  about the SHAPE (sealed ceiling, no sky, one cave), not the material.
//
//  PURE, like every other generator (Part T's determinism rule 3): a
//  chunk's contents are a function of (seed, cx, cz) alone. Nothing
//  reads a neighbouring chunk, nothing reads the clock, nothing draws
//  from rand(). That is what makes the host checks able to assert the
//  ceiling is sealed across a fixed sample, and what makes a replay
//  reproduce the same cave.
//
//  THE FLOOR IS A COLUMN FUNCTION, and that is not a style choice. The
//  shaft pairing is by column (D-142), so the arrival height has to be
//  knowable BEFORE the chunk exists -- before it is resident, before it
//  is on the card. `deepmine_ground_y()` is the only way to answer that
//  without a link record.
//
//  Pure: no engine, no RTOS, no allocation.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/chunk.h"
#include "world/dimension.h"

// --- The generator's rates -------------------------------------------
//
// Ore. `chance` is the share of coarse cells that carry a vein, `r` its
// rough radius -- the same two numbers worldgen.c uses, so the Deep Mine
// is a rate table over the same vein shape rather than a second version
// of the same idea. Iron is richer here than above (D-139); diamond is
// the occasional cluster, and coal holds the overworld's rate because
// nothing said otherwise.
#define DM_IRON_CHANCE    0.34f
#define DM_IRON_R         2.0f
#define DM_DIAMOND_CHANCE 0.05f
#define DM_DIAMOND_R      1.2f
#define DM_COAL_CHANCE    0.30f
#define DM_COAL_R         2.0f

// Lava, rolled on the same coarse grid as a vein (VEIN_GRID) for the same
// reason: a patch you can walk around is a shape, and a coin flip per
// cell would be speckle.
#define DM_LAVA_CHANCE  0.06f
#define DM_LAVA_R       1.5f

// --- The two surfaces ------------------------------------------------
//
// The cave is the volume between a floor and a roof, and both are read
// ONCE PER GRID SQUARE rather than once per column. That is the whole
// difference between a plane and a cave: a smooth noise floor puts every
// column one or two blocks from its neighbour, so nothing ever reads as a
// wall, and a floor read once per square is a plateau with a step up
// between squares. Two grids of different size, so the floor and the roof
// never agree for long and the height of the room changes as you walk
// through it.
//
// The spans are what the plateau can land on BEFORE the rough term is
// added, so the bands in dimension.h are the bands the generator can
// actually produce -- a span that could reach past its own maximum would
// make the clamp the thing deciding the shape, and a check would measure
// the clamp rather than the cave.
#define DM_FLOOR_GRID  24
#define DM_CEIL_GRID   12
#define DM_FLOOR_SPAN  6
#define DM_CEIL_SPAN   17
// How many heights the rough term can add inside a square.
#define DM_ROUGH       2
// The pad a shaft stands on: the square around its spot, flattened. See
// deepmine.c -- a shaft needs level ground under all nine cells of its
// footprint, and a floor that changes per column almost never gives it.
#define DM_PAD         5

// A span the player can still walk is not a cosmetic preference. The jump
// apex is 1.25 blocks and step_up is 0 (player.h), so a step of two is a
// wall the player can only go around. A floor span of 15 put every square
// about seven blocks above or below its neighbour, and a flood fill from
// one column reached 12.7% of the cave: not a cave you walk through, a
// stack of rooms you cannot get out of. Six keeps the walls -- one column
// in ninety-six still steps two or more from its neighbour -- and leaves
// the cave walkable, 95.3% of it from a single starting column. That is
// what the grid is for: 24 is the largest that keeps both, and it is the
// number the check measures rather than the one a comment asserts.

// The y of the first air above the cave floor at this column -- the cell
// the player stands in. Same convention as worldgen_height(), so the two
// dimensions answer the same question the same way.
int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed);

// The y of the first bedrock above the cave at this column -- its roof.
// Per column, because the roof is not one flat height.
int deepmine_ceiling_y(int32_t x, int32_t z, uint32_t seed);

// Is this cell part of the sealed ceiling? Per column for the same reason,
// and the same function the generator fills with: two answers to "is this
// the roof" would be the difference between a cave and a hole in it.
static inline bool deepmine_is_ceiling(int32_t x, int32_t z, int y, uint32_t seed) {
    return y >= deepmine_ceiling_y(x, z, seed);
}

// Is (x, z) inside the flat pad a shaft stands on? Exported because the
// generator, the placement rule and the check all have to get the same
// answer for the same column -- the pad is part of the terrain, not a
// decoration painted on top of it afterwards.
bool deepmine_is_pad(int32_t x, int32_t z, uint32_t seed);

// Is the floor at (x, z) covered by lava? Asked per column, because the
// patch is a shape on a coarse grid rather than a per-cell coin flip.
bool deepmine_lava(int32_t x, int32_t z, uint32_t seed);

// Fill `c` with the Deep Mine's terrain. The chunk must already carry
// cx / cz. Marks CF_GENERATED and refreshes the column summaries; leaves
// cstate alone (the caller owns the state machine), exactly like
// worldgen_chunk.
void deepmine_chunk(chunk_t* c, uint32_t seed);

// Does this chunk hold a pack, and how big is it (D-147)? Rolled from
// the world's hash, so a replay reproduces the same population and the
// host check can assert the share across a fixed sample.
bool deepmine_pack(int32_t cx, int32_t cz, uint32_t seed, int* n);
