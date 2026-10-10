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

// The y of the first air above the cave floor at this column -- the cell
// the player stands in. Same convention as worldgen_height(), so the two
// dimensions answer the same question the same way.
int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed);

// Is this cell part of the sealed ceiling?
static inline bool deepmine_is_ceiling(int y) {
    return y >= DM_CEILING_Y;
}

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
