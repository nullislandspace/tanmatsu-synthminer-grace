// =====================================================================
//  SynthMiner  --  the mine shaft (see mineshaft.h, and Part M)
// =====================================================================

#include "world/mineshaft.h"

#include "world/blocks.h"
#include "world/deepmine.h"
#include "world/dimension.h"
#include "world/worldgen.h"

// --- The shape --------------------------------------------------------

// Four corner posts, three high, and four braces across the top. Sixteen
// cells, and sixteen ingots -- which is why the shape is exactly this.
//
// The centre column (dx, dz == 0) is not in the list at all. It is the
// open middle the player steps into, and the only cell of a shaft that
// is not a block.
shaft_cell_t const SHAFT_CELLS[16] = {
    // The four posts, three high.
    {-1, 1, -1, BLK_SHAFT_COLUMN}, {1, 1, -1, BLK_SHAFT_COLUMN},
    {-1, 1,  1, BLK_SHAFT_COLUMN}, {1, 1,  1, BLK_SHAFT_COLUMN},
    {-1, 2, -1, BLK_SHAFT_COLUMN}, {1, 2, -1, BLK_SHAFT_COLUMN},
    {-1, 2,  1, BLK_SHAFT_COLUMN}, {1, 2,  1, BLK_SHAFT_COLUMN},
    {-1, 3, -1, BLK_SHAFT_COLUMN}, {1, 3, -1, BLK_SHAFT_COLUMN},
    {-1, 3,  1, BLK_SHAFT_COLUMN}, {1, 3,  1, BLK_SHAFT_COLUMN},
    // The four braces, across the top: the edge middles at height 3.
    {-1, 3,  0, BLK_SHAFT_TRUSS}, {1, 3,  0, BLK_SHAFT_TRUSS},
    {0, 3, -1, BLK_SHAFT_TRUSS},  {0, 3,  1, BLK_SHAFT_TRUSS},
};


// --- The Deep Mine's ground -------------------------------------------

// THE PAD, as a pure function of the column. mineshaft.h has the three
// answers and why each one is there; the reason the function exists at all is
// that a crossing has to know the arrival height before the chunk it lands in
// exists, because the store is empty at the moment it decides.
int mineshaft_pad_y(int32_t x, int32_t z, uint32_t seed) {
    // THE ROOF, over the whole footprint. A post under a column whose roof
    // comes lower than the middle's is a post inside bedrock, and levelling
    // the ground is not allowed to carve the ceiling.
    int hi = CH_H - 4;
    for (int dz = -SHAFT_PAD; dz <= SHAFT_PAD; dz++) {
        for (int dx = -SHAFT_PAD; dx <= SHAFT_PAD; dx++) {
            int const room = deepmine_ceiling_y(x + dx, z + dz, seed) - DM_ROOM_MIN;
            if (room < hi) hi = room;
        }
    }
    if (hi < 1) return -1;

    // THE LAVA, over a 9x9 rather than the 5x5. Lava flows now, three cells
    // (fluid.c), so a lake whose surface sits just outside the footprint would
    // run into the room the levelling carves. Four is more than three.
    int lo = 1;
    for (int dz = -SHAFT_PAD - 2; dz <= SHAFT_PAD + 2; dz++) {
        for (int dx = -SHAFT_PAD - 2; dx <= SHAFT_PAD + 2; dx++) {
            int const top = deepmine_lava_top(x + dx, z + dz, seed);
            if (top + 1 > lo) lo = top + 1;
        }
    }

    // THE GROUND: the slab sits on the middle column's own floor, so the
    // shaft looks like it was built on the ground it stands on.
    int const ground = deepmine_ground_y(x, z, seed) - 1;
    if (ground > lo) lo = ground;

    // THE ROOF WINS. A pad that has to dig down into the rock is a shaft, and
    // a shaft is what this is. It cannot land in a lake by doing that:
    // DM_CEIL_MIN sits above DM_LAVA_LEVEL + DM_ROOM_MIN, so the lowest roof
    // still leaves a frame of dry air over the highest sea.
    if (lo > hi) lo = hi;
    return lo;
}

// LEVEL THE GROUND, on arrival only (mineshaft.h). Twenty-five cells of slab
// and up to seventy-five of clear air: the whole of what a frame needs, and
// nothing more. The roof is untouched -- `y` is under the lowest roof of the
// 5x5 by `mineshaft_pad_y()`, and the bedrock test below is the belt on top
// of that, because a hole in the ceiling is a hole in the cave.
//
// Through world_set, so the light flood, the mesher and the save all hear
// about it, exactly like a player's placement. Cells that are already right
// are skipped: a hundred writes that change nothing would mark the chunk
// dirty and re-mesh it for nothing.
void mineshaft_flatten(int32_t x, int y, int32_t z) {
    if (y < 1 || y + 3 >= CH_H) return;

    for (int dz = -SHAFT_PAD; dz <= SHAFT_PAD; dz++) {
        for (int dx = -SHAFT_PAD; dx <= SHAFT_PAD; dx++) {
            int32_t const px = x + dx, pz = z + dz;

            if (world_block(px, y, pz) != BLK_STONE) world_set(px, y, pz, BLK_STONE, 0);
            for (int dy = 1; dy <= 3; dy++) {
                uint8_t const b = world_block(px, y + dy, pz);
                if (b == BLK_BEDROCK) continue;
                if (b != BLK_AIR) world_set(px, y + dy, pz, BLK_AIR, 0);
            }
        }
    }
}


// --- The player's writer ----------------------------------------------

// Every cell of the shape has to be somewhere it can be written, and
// every chunk it needs has to be here. An absent chunk reads as the
// barrier (D-14), so a frame half inside the ring would be half a frame
// for ever -- refusing outright is cheaper than that.
bool mineshaft_can_place(int32_t x, int y, int32_t z) {
    if (y < 1 || y + 3 >= CH_H) return false;

    // The ground it stands on: all nine cells of the footprint, not just
    // the middle. A post with nothing under it is a post in the air.
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            uint8_t const b = world_block(x + dx, y, z + dz);
            if (!block_solid(b)) return false;
        }
    }

    for (size_t i = 0; i < 16; i++) {
        shaft_cell_t const* s = &SHAFT_CELLS[i];
        int32_t const wx = x + s->dx, wz = z + s->dz, wy = y + s->dy;
        if (wy < 0 || wy >= CH_H) return false;
        if (chunk_find(chunk_of(wx), chunk_of(wz)) == NULL) return false;
        uint8_t const b = world_block(wx, wy, wz);
        if (b != BLK_AIR && !block_replaceable(b)) return false;
    }
    return true;
}

bool mineshaft_place(int32_t x, int y, int32_t z) {
    // Check first, then write. A half-built frame is worse than a
    // refused one: the shape is the trigger, and half of it is not a
    // shaft anybody could walk into.
    if (!mineshaft_can_place(x, y, z)) return false;
    for (size_t i = 0; i < 16; i++) {
        shaft_cell_t const* s = &SHAFT_CELLS[i];
        world_set(x + s->dx, y + s->dy, z + s->dz, s->block, 0);
    }
    return true;
}

// THE FAR END OF A CROSSING.
//
// The pairing is by column (D-142), so a shaft only works in both
// directions if that column has one in each dimension -- and the generator
// cannot know which column a player chose. It is not allowed to: a
// generator that remembered what the player did would read a neighbouring
// chunk, and Part T's determinism rules say it cannot.
//
// So the far end is built the first time a column is crossed. It is the
// same shape and the same writer as the player's placement, with one rule
// dropped and one added.
//
// DROPPED: the ground under the nine footprint cells. A portal the game
// opens was not placed by a player who could have picked a better spot, so
// a post standing on a column whose floor is lower than its neighbour's
// still has to be a post.
//
// ADDED, in the Deep Mine only: `mineshaft_flatten`, first. The cave's
// ground is rough on purpose -- terraces, cliffs, columns where the roof
// lands on the floor -- and a frame dropped into that would be refused more
// often than not, which is the "the mineshaft would not open" the user ran
// into. The pad is what the arrival height was computed for
// (`mineshaft_pad_y`), so the ground the frame stands on is levelled first
// and every cell of the shape is then air. The overworld is NOT flattened:
// a shaft the player arrives in on the surface appears where the surface
// already is.
bool mineshaft_open_at(int32_t x, int y, int32_t z) {
    if (y < 1 || y + 3 >= CH_H) return false;

    if (dimension_current() == DIM_DEEP_MINE) mineshaft_flatten(x, y, z);

    for (size_t i = 0; i < 16; i++) {
        shaft_cell_t const* s = &SHAFT_CELLS[i];
        int32_t const wx = x + s->dx, wz = z + s->dz, wy = y + s->dy;
        if (wy < 0 || wy >= CH_H) return false;
        if (chunk_find(chunk_of(wx), chunk_of(wz)) == NULL) return false;
        uint8_t const b = world_block(wx, wy, wz);
        if (b != BLK_AIR && !block_replaceable(b)) return false;
    }
    for (size_t i = 0; i < 16; i++) {
        shaft_cell_t const* s = &SHAFT_CELLS[i];
        world_set(x + s->dx, y + s->dy, z + s->dz, s->block, 0);
    }
    return true;
}

// --- The trigger ------------------------------------------------------

// (x, y, z) is the cell the player's feet are in. The test is the shape
// seen from the middle: four posts at the same height on the diagonals,
// four braces two above it on the edge middles, and ground underneath.
//
// It is stateless on purpose. There is no link record to get wrong and
// nothing to migrate, because the pairing is by column (D-142): the same
// test answers in either dimension, and a shaft the player built and a
// shaft the generator grew are the same shape.
bool mineshaft_is_trigger(int32_t x, int y, int32_t z) {
    if (y < 1 || y + 2 >= CH_H) return false;
    if (world_block(x, y, z) != BLK_AIR) return false;
    if (!block_solid(world_block(x, y - 1, z))) return false;

    for (int d = 0; d < 4; d++) {
        int const dx = (d == 0) ? -1 : (d == 1) ? 1 : (d == 2) ? -1 : 1;
        int const dz = (d < 2) ? -1 : 1;
        if (world_block(x + dx, y, z + dz) != BLK_SHAFT_COLUMN) return false;
    }
    for (int d = 0; d < 4; d++) {
        int const dx = (d == 0) ? -1 : (d == 1) ? 1 : 0;
        int const dz = (d == 2) ? -1 : (d == 3) ? 1 : 0;
        if (world_block(x + dx, y + 2, z + dz) != BLK_SHAFT_TRUSS) return false;
    }
    return true;
}

int mineshaft_arrival_y(int32_t x, int32_t z, uint32_t seed, int dimension) {
    // THE CENTRE OF THE SHAFT, in both dimensions. The pairing is by column
    // (D-142), so what is shared is x and z and what is not is the height --
    // and in each dimension the answer is "the cell the player stands in at
    // the middle of a frame built on that column's own ground".
    //
    // In the overworld that is the surface. In the Deep Mine it is the cell
    // above the pad, which is NOT the cave floor: the cave floor is rough,
    // and the frame is built on a levelled 5x5 of it. Answering with
    // `deepmine_ground_y()` here put the player at the terrain height while
    // the frame was built one or two cells elsewhere, which is what the user
    // saw as arriving on top of the world instead of in the shaft.
    if (dimension == (int)DIM_DEEP_MINE) {
        int const pad = mineshaft_pad_y(x, z, seed);
        return pad < 0 ? deepmine_ground_y(x, z, seed) : pad + 1;
    }
    return worldgen_height(x, z, seed);
}

int mineshaft_trigger_y(int32_t x, int32_t z) {
    for (int y = 1; y + 2 < CH_H; y++) {
        if (mineshaft_is_trigger(x, y, z)) return y;
    }
    return -1;
}
