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
//  ONE SHAPE, TWO WRITERS, and both of them are players: a shaft the
//  player places goes through world_set so the light flood, the mesher and
//  the save all hear about it, and the far end of a crossing is built by
//  `mineshaft_open_at()` the first time that column is used.
//
//  THE GENERATOR DOES NOT MAKE SHAFTS (the user, 2026-10-12). It used to
//  stamp one per coarse grid square, and what the player saw was shafts
//  "randomly appearing that I didn't place" while walking into unexplored
//  terrain. A shaft is something you built with sixteen ingots; a cave
//  full of free ones is not a mine, it is a lottery. So the only shafts in
//  either dimension are the ones somebody placed and the far ends of the
//  columns they placed them in.
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

// The footprint is 3x3, so the ground rule looks at nine cells and the
// flattening the Deep Mine does looks at twenty-five. Both are here as
// numbers rather than literals because a check has to ask the same
// question the writer answers.
#define SHAFT_FOOT  1   // +1 each way from the centre: a 3x3
#define SHAFT_PAD   2   // +2 each way: a 5x5, the ground the frame stands in

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

// THE GROUND A DEEP MINE SHAFT STANDS ON, as a pure function of the column
// -- the same reason `deepmine_ground_y()` exists: the arrival height has to
// be answerable before the chunk exists, because the store is empty at the
// moment the crossing decides where to put the player.
//
// Three answers, in this order:
//
//   THE ROOF. The frame is four cells tall (its ground cell and three of
//      post), so the ground cell is at most the LOWEST roof over the 5x5
//      minus four. Not the middle column's roof -- a post under a column
//      whose roof comes lower would be a post inside bedrock.
//   THE LAVA. Nothing at or above the slab may be lava, over a 9x9 rather
//      than the 5x5: lava now flows, three cells (fluid.c), so a lake just
//      outside the footprint would run into the room the flattening carves.
//   THE GROUND. The slab sits on the middle column's own floor, so a shaft
//      looks like it was built on the ground it stands on.
//
// When the roof is lower than the ground wants, the roof wins and the pad
// digs down into the rock -- which is what a mine shaft does. That cannot
// land in a lake: DM_CEIL_MIN sits above DM_LAVA_LEVEL + DM_ROOM_MIN, so
// the lowest roof still leaves a frame of dry air over the highest sea.
//
// -1 only for a column that cannot hold a frame at all.
int mineshaft_pad_y(int32_t x, int32_t z, uint32_t seed);

// LEVEL THE GROUND the frame stands in, in the world being ENTERED by a
// crossing. The user's allowance (2026-10-12): "it's OK to flatten the
// terrain a bit (like in a 5x5 grid centered on the mineshaft center) after
// natural terrain generation." So: the 5x5 gets a solid slab at `y` and
// three cells of clear air above it. Never a hole in the roof -- `y` is
// under the lowest roof of the 5x5 by `mineshaft_pad_y()`, which is what
// makes this legal at all.
//
// A crossing only, never a player's placement: a player's shaft goes where
// the player put it, and carving their terrain behind their back is the
// same surprise the generator's free shafts were.
void mineshaft_flatten(int32_t x, int y, int32_t z);

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

// The same shape and the same writer, for the far end of a crossing: the
// shaft that has to be standing at this column in the world being entered,
// which the generator cannot know about. Same sixteen cells, same all-or-
// nothing rule, but no ground rule -- see mineshaft.c.
bool mineshaft_open_at(int32_t x, int y, int32_t z);

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
