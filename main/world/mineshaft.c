// =====================================================================
//  SynthMiner  --  the mine shaft (see mineshaft.h, and Part M)
// =====================================================================

#include "world/mineshaft.h"

#include <math.h>

#include "common/rng.h"
#include "world/blocks.h"
#include "world/deepmine.h"
#include "world/dimension.h"
#include "world/worldgen.h"

// The shaft's own salt. Separate from the ore salts and from the tree
// grid's, for the usual reason: a shaft that landed where a vein landed
// would be a coincidence no check would notice.
#define S_SHAFT 0xE700u

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


void mineshaft_shape(int32_t x, int y, int32_t z, worldgen_cell_fn emit, void* ctx) {
    for (size_t i = 0; i < 16; i++) {
        shaft_cell_t const* s = &SHAFT_CELLS[i];
        emit(ctx, x + s->dx, y + s->dy, z + s->dz, s->block, true);
    }
}

// --- The generator's writer -------------------------------------------

// Write a cell, but only if it falls inside this chunk -- the same
// argument as worldgen.c's stamp(): a shaft on a chunk border is written
// twice, once from each side, and that is what makes it seamless without
// either chunk reading the other.
static void stamp(chunk_t* c, int32_t wx, int y, int32_t wz, uint8_t block, bool overwrite) {
    if (y < 0 || y >= CH_H) return;
    if (chunk_of(wx) != c->cx || chunk_of(wz) != c->cz) return;
    size_t const i = CH_IDX(chunk_off(wx), y, chunk_off(wz));
    if (!overwrite && c->id[i] != BLK_AIR) return;
    c->id[i] = block;
    c->st[i] = 0;  // generated, so ST_PLACED stays clear
}

static void stamp_cell(void* ctx, int32_t wx, int y, int32_t z, uint8_t block, bool overwrite) {
    stamp((chunk_t*)ctx, wx, y, z, block, overwrite);
}

bool mineshaft_is_candidate(int32_t x, int32_t z, uint32_t seed) {
    int32_t const gx = (x >= 0 ? x : x - SHAFT_GRID + 1) / SHAFT_GRID;
    int32_t const gz = (z >= 0 ? z : z - SHAFT_GRID + 1) / SHAFT_GRID;
    return sm_rand2(gx, gz, seed ^ S_SHAFT) <= SHAFT_CHANCE;
}

// The exact column inside its grid square, so the shafts are not on a
// visible lattice -- the same trick as the tree grid.
void mineshaft_candidate_spot(int32_t gx, int32_t gz, uint32_t seed, int32_t* wx, int32_t* wz) {
    *wx = gx * SHAFT_GRID + (int32_t)(sm_rand2(gx, gz, seed ^ 0xE7A1u) * SHAFT_GRID);
    *wz = gz * SHAFT_GRID + (int32_t)(sm_rand2(gx, gz, seed ^ 0xE7B2u) * SHAFT_GRID);
}

void mineshaft_stamp(chunk_t* c, uint32_t seed) {
    if (c == NULL) return;

    int32_t const x0 = (c->cx - 1) * CH_W, x1 = (c->cx + 2) * CH_W;
    int32_t const z0 = (c->cz - 1) * CH_D, z1 = (c->cz + 2) * CH_D;
    int32_t const gx0 = (int32_t)floorf((float)x0 / SHAFT_GRID), gx1 = (int32_t)floorf((float)x1 / SHAFT_GRID);
    int32_t const gz0 = (int32_t)floorf((float)z0 / SHAFT_GRID), gz1 = (int32_t)floorf((float)z1 / SHAFT_GRID);

    for (int32_t gz = gz0; gz <= gz1; gz++) {
        for (int32_t gx = gx0; gx <= gx1; gx++) {
            int32_t wx, wz;
            mineshaft_candidate_spot(gx, gz, seed, &wx, &wz);
            if (!mineshaft_is_candidate(wx, wz, seed)) continue;

            // deepmine_ground_y() is the first air above the floor -- the
            // cell the player stands in -- so the cell the shaft stands ON
            // is the one below it. Getting this off by one would put every
            // generated shaft one block above its own floor.
            // THE PAD. The frame stands on the pad the generator flattened
            // for this square, so it only stamps where the pad exists --
            // which is where the whole five by five lies inside the square.
            // Without that, a post lands inside the rock of a neighbouring
            // column, and a post inside its own floor is a post with nothing
            // under it: the same thing the player's placement refuses.
            if (!deepmine_is_pad(wx, wz, seed)) continue;

            int const g = deepmine_ground_y(wx, wz, seed) - 1;
            if (g < 0) continue;

            // Every post has its own column, and every column has its own
            // roof and its own patch of lava, so the headroom and the dry
            // ground are tested over all nine cells of the footprint rather
            // than over the middle. All or nothing, as everywhere else: a
            // frame half inside the rock is a frame half of which was never
            // written.
            bool ok = true;
            for (int dz = -1; dz <= 1 && ok; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int32_t const px = wx + dx, pz = wz + dz;
                    if (g + 3 >= deepmine_ceiling_y(px, pz, seed) || deepmine_lava(px, pz, seed)) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) continue;

            mineshaft_shape(wx, g, wz, stamp_cell, c);
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
// dropped: the ground under the nine footprint cells. A portal the game
// opens was not placed by a player who could have picked a better spot, so
// a post standing on a column whose floor is lower than its neighbour's
// still has to be a post. What is not dropped is the rest -- every cell
// still has to be writable, and a half frame is still refused outright.
bool mineshaft_open_at(int32_t x, int y, int32_t z) {
    if (y < 1 || y + 3 >= CH_H) return false;

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
    // Both dimensions answer "the first air above the ground" the same
    // way, so the arrival cell is the standing cell and nothing has to be
    // added to it. This is the only place the 1:1 mapping is actually
    // used: the column is shared, the height is not.
    if (dimension == (int)DIM_DEEP_MINE) return deepmine_ground_y(x, z, seed);
    return worldgen_height(x, z, seed);
}

int mineshaft_trigger_y(int32_t x, int32_t z) {
    for (int y = 1; y + 2 < CH_H; y++) {
        if (mineshaft_is_trigger(x, y, z)) return y;
    }
    return -1;
}
