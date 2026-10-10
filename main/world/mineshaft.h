#pragma once
// =====================================================================
//  SynthMiner  --  the mine shaft (see claudeplans/synthminer.md, Part M)
// ---------------------------------------------------------------------
//  A shaft is 16 blocks: four columns at the four corners of a 3x3
//  footprint, three blocks tall, joined by four trusses at height 3. The
//  middle is open, and standing in the centre cell crosses over.
//
//  16 ingots in, 16 blocks out -- the recipe and the shape are the same
//  number, which is why the shape is exactly this and not one block
//  more.
//
//  TWO WRITERS, ONE SHAPE, and the same argument as worldgen_tree_shape:
//  the generator stamps it into one chunk (and drops whatever falls
//  outside, so a shaft at a chunk border is written twice and comes out
//  seamless), while a player-placed shaft goes through world_set so the
//  light flood, the mesher and the save all hear about it.
//
//  THE TRIGGER IS STATELESS. There is no link record, because the
//  pairing is by column (D-142): a shaft at (x,z) in either dimension
//  meets the shaft at (x,z) in the other. So "am I standing in a shaft"
//  is a shape test on the cells around the player, not a lookup, and
//  nothing can dangle.
//
//  Pure: no engine, no RTOS, no allocation.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/chunk.h"
#include "world/worldgen.h"  // worldgen_cell_fn, worldgen_height

// Shafts in the Deep Mine sit on a coarse grid, one candidate per cell,
// so two of them cannot grow into each other -- the same shape as the
// tree grid, for the same reason.
#define SHAFT_GRID   12
#define SHAFT_CHANCE 0.10f

// One cell of the shape, as an offset from the GROUND cell the shaft
// stands on. The shaft itself occupies y+1..y+3; the centre at y+1 is
// left open, because that is where the player steps in.
typedef struct {
    int8_t  dx, dy, dz;
    uint8_t block;
} shaft_cell_t;

// The 16 blocks. Twelve column cells (four corners, three high) and four
// truss cells (the edge middles at height 3).
extern shaft_cell_t const SHAFT_CELLS[16];

// Emit the shape cell by cell, exactly like worldgen_tree_shape. `y` is
// the ground cell it stands on.
void mineshaft_shape(int32_t x, int y, int32_t z, worldgen_cell_fn emit, void* ctx);

// Is this column one of the Deep Mine's generated shafts? A pure
// function of the position, so two chunks agree along a border.
bool mineshaft_is_candidate(int32_t x, int32_t z, uint32_t seed);

// The exact column inside grid square (gx, gz), so the shafts are not on a
// visible lattice -- the same trick as the tree grid. Exported because a
// check has to ask the generator where a shaft is: every column in a
// candidate square passes mineshaft_is_candidate(), but only this one is
// stamped, so a check that guesses from a cell tests the wrong shaft.
void mineshaft_candidate_spot(int32_t gx, int32_t gz, uint32_t seed, int32_t* wx, int32_t* wz);

// Stamp every shaft that could reach into `c`: the 3x3 neighbourhood of
// chunks, walked on the shaft grid, exactly as worldgen.c walks the tree
// grid. Cells outside `c` are dropped by the writer, so a shaft on a
// chunk border is written twice and comes out seamless.
void mineshaft_stamp(chunk_t* c, uint32_t seed);


// CAN A SHAFT BE PLACED HERE? `y` is the ground cell. Both ends have to
// stand on solid ground, and in the overworld that is a placement rule
// rather than a generator rule. Refuses outright if a chunk it needs is
// not resident: an absent chunk reads as the barrier (D-14), and half a
// frame at the edge of the ring would never come back.
bool mineshaft_can_place(int32_t x, int y, int32_t z);

// Place it through world_set. False if any cell could not be written --
// a half-built frame is worse than a refused one, so this writes nothing
// unless all sixteen cells are writable.
bool mineshaft_place(int32_t x, int y, int32_t z);

// Is (x, y, z) -- the cell the player is standing in -- the open centre
// of a shaft? Reads only this chunk's cells, so it works in either
// dimension and needs no record of which shaft this is.
bool mineshaft_is_trigger(int32_t x, int y, int32_t z);

// The cell a crossing lands in at this column, in `to`. Pure, because
// the arrival height has to be knowable before the target chunk exists.
int mineshaft_arrival_y(int32_t x, int32_t z, uint32_t seed, int dimension);

// If a shaft is already standing at this column in the resident world,
// the y its centre is at; -1 when there is no shaft there. Used to send
// the player back into the frame they built rather than on to the open
// ground above it.
int mineshaft_trigger_y(int32_t x, int32_t z);
