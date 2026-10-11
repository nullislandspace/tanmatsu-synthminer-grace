// =====================================================================
//  SynthMiner  --  the Deep Mine (see deepmine.h, and Part M)
// =====================================================================

#include "world/deepmine.h"

#include <stdlib.h>
#include <string.h>

#include "common/rng.h"
#include "world/blocks.h"
#include "world/mineshaft.h"
#include "world/worldgen.h"

// The salts. Separate from worldgen.c's, because a vein in the Deep Mine
// is the same SHAPE (worldgen_vein_at) with different odds, and sharing a
// salt would put the two dimensions' ore in the same places -- which is
// exactly the kind of coincidence a check would never notice.
#define S_DM_FLOOR    0xE100u
#define S_DM_CEIL     0xE150u
#define S_DM_FLOOR_ROUGH 0xE180u
#define S_DM_CEIL_ROUGH  0xE190u
#define S_DM_LAVA     0xE200u
#define S_DM_IRON     0xE300u
#define S_DM_COAL     0xE350u
#define S_DM_DIAMOND  0xE400u
#define S_DM_PACK     0xE500u

// --- The two surfaces -------------------------------------------------

// Which grid square a column belongs to. Negative-safe, because C division
// rounds toward zero: -1 / 16 is 0, so the square it names is not the one the
// generator would have read for that column. Every grid in this file -- the
// floor, the roof, the lava, the veins -- goes through here for that reason.
static int32_t dm_square(int32_t v, int grid) {
    return (v >= 0 ? v : v - grid + 1) / grid;
}

// One value per grid square. Everything inside a square is the same height,
// which is what makes it a plateau, and the step from one square to the
// next is the wall. A per-column noise would be smoother and would not be
// a cave.
static int dm_plateau(int32_t x, int32_t z, uint32_t seed, int grid, int base, int span, uint32_t salt) {
    int32_t const gx = dm_square(x, grid), gz = dm_square(z, grid);
    return base + (int)(sm_rand2(gx, gz, seed ^ salt) * (float)span);
}

// THE PAD A SHAFT STANDS ON.
//
// A shaft needs level ground under all nine cells of its footprint -- the
// same rule as the player's placement -- and a floor that changes per column
// almost never gives it: over 262144 columns, one candidate square in 26000
// had a flat three by three, so the generator was stamping frames into rock.
// So a candidate shaft brings its own pad, five by five, flattened to the
// height its square would have had anyway. That is the flattening the design
// allows, and it happens as the terrain is read, not as a decoration painted
// on it afterwards.
//
// It is still a pure function of the position (Part T): the spot is the same
// hash the generator uses. Two squares then have to agree about every column
// of the pad, and that is what the two conditions below are.
//
//   - THE SHAFT SQUARE. A column outside it would be asked by a different
//     square's shaft, and the two would answer with two different spots -- so
//     the whole pad has to sit inside the square the spot belongs to.
//   - THE FLOOR SQUARE. The pad is flattened to the height its own square
//     would have had, so a column across that border would read a different
//     height for the same cell.
//
// SHAFT_GRID divides DM_FLOOR_GRID, so the first condition implies the
// second. Both are written anyway, because the first is what makes the pad
// knowable per column and the second is what makes it flat -- and a check
// that only enforced one of them would pass on a grid that broke the other.
bool deepmine_is_pad(int32_t x, int32_t z, uint32_t seed) {
    int32_t const gx = dm_square(x, SHAFT_GRID), gz = dm_square(z, SHAFT_GRID);
    if (!mineshaft_is_candidate(x, z, seed)) return false;

    int32_t wx, wz;
    mineshaft_candidate_spot(gx, gz, seed, &wx, &wz);

    int const half = DM_PAD / 2;
    if (x - wx > half || wx - x > half || z - wz > half || wz - z > half) return false;

    int32_t const sx = wx - gx * SHAFT_GRID, sz = wz - gz * SHAFT_GRID;
    if (sx < half || sx > SHAFT_GRID - 1 - half) return false;
    if (sz < half || sz > SHAFT_GRID - 1 - half) return false;

    return dm_square(x, DM_FLOOR_GRID) == dm_square(wx, DM_FLOOR_GRID) &&
           dm_square(z, DM_FLOOR_GRID) == dm_square(wz, DM_FLOOR_GRID);
}

int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed) {
    int const base = dm_plateau(x, z, seed, DM_FLOOR_GRID, DM_FLOOR_MIN, DM_FLOOR_SPAN, S_DM_FLOOR);
    if (deepmine_is_pad(x, z, seed)) return base;

    // Rough edges. A plateau that is exactly flat reads as a floor with
    // something painted on it rather than as rock, so a column may sit a
    // block above its square. It can never sit below it: the square is the
    // floor, and the player standing on a column is standing on the plateau
    // it belongs to.
    int y = base + (int)(sm_rand2(x, z, seed ^ S_DM_FLOOR_ROUGH) * (float)DM_ROUGH);
    if (y > DM_FLOOR_MAX) y = DM_FLOOR_MAX;
    return y;
}

int deepmine_ceiling_y(int32_t x, int32_t z, uint32_t seed) {
    int y = dm_plateau(x, z, seed, DM_CEIL_GRID, DM_CEIL_MIN, DM_CEIL_SPAN, S_DM_CEIL);
    y += (int)(sm_rand2(x, z, seed ^ S_DM_CEIL_ROUGH) * (float)DM_ROUGH);
    if (y > DM_CEILING_Y) y = DM_CEILING_Y;

    // SEALED AND WALKABLE. The bands are set so this cannot fire -- floor
    // 6..12, roof 26..44, so fourteen open cells minimum against the four
    // this requires -- but a guarantee that only holds because of the numbers
    // is not a guarantee, and the consequence of breaking it is a player
    // placed inside rock. The check that says so is in tools/worldcheck.c.
    int const floor_y = deepmine_ground_y(x, z, seed);
    if (y < floor_y + DM_ROOM_MIN) y = floor_y + DM_ROOM_MIN;
    if (y > DM_CEILING_Y) y = DM_CEILING_Y;
    return y;
}

// --- Lava -------------------------------------------------------------

// A patch, not a coin flip per cell: one candidate per coarse cell, and a
// column is lava if it falls inside one. The same shape as an ore vein,
// for the same reason -- fill_column has to give the same answer from
// either side of a chunk border, so nothing here can be grown by walking.
bool deepmine_lava(int32_t x, int32_t z, uint32_t seed) {
    int32_t const gx = dm_square(x, VEIN_GRID), gz = dm_square(z, VEIN_GRID);

    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            int32_t const cx = gx + dx, cz = gz + dz;
            if (sm_rand2(cx, cz, seed ^ S_DM_LAVA) > DM_LAVA_CHANCE) continue;
            float const ox = (float)(cx * VEIN_GRID) + sm_rand2(cx, cz, seed ^ (S_DM_LAVA + 1u)) * VEIN_GRID;
            float const oz = (float)(cz * VEIN_GRID) + sm_rand2(cx, cz, seed ^ (S_DM_LAVA + 2u)) * VEIN_GRID;
            float const d  = (float)(x - ox) * (float)(x - ox) + (float)(z - oz) * (float)(z - oz);
            if (d <= DM_LAVA_R * DM_LAVA_R) return true;
        }
    }
    return false;
}

// --- A chunk ----------------------------------------------------------

void deepmine_chunk(chunk_t* c, uint32_t seed) {
    if (c == NULL) return;

    memset(c->st, 0, CH_CELLS);

    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            int32_t const wx = c->cx * CH_W + lx, wz = c->cz * CH_D + lz;
            int     const fy = deepmine_ground_y(wx, wz, seed);
            int     const cy = deepmine_ceiling_y(wx, wz, seed);
            bool    const wet = deepmine_lava(wx, wz, seed);

            for (int y = 0; y < CH_H; y++) {
                uint8_t b = BLK_AIR;

                if (y >= cy) {
                    // The sealed ceiling, per column. Every column, every
                    // time -- a hole here is the difference between a cave
                    // and a shaft open to the sky, and the check that says
                    // so walks a fixed sample of columns.
                    b = BLK_BEDROCK;
                } else if (y < fy) {
                    // `fy` is the first air above the floor, the same
                    // convention as worldgen_height(), so the solid part
                    // of a column is everything BELOW it. That is what
                    // makes the pairing work: the arrival cell is the
                    // cell the player stands in, and it is knowable
                    // before the chunk exists.
                    b = BLK_STONE;
                    // Ore, in the same vein shape the overworld uses.
                    // Diamond is tested first for the same reason iron is
                    // tested before coal in fill_column: two ores cannot
                    // fight over one cell.
                    if (y > CH_BEDROCK) {
                        if (worldgen_vein_at(wx, y, wz, seed, S_DM_DIAMOND, DM_FLOOR_MAX, DM_DIAMOND_CHANCE,
                                            DM_DIAMOND_R)) {
                            b = BLK_DIAMOND_ORE;
                        } else if (worldgen_vein_at(wx, y, wz, seed, S_DM_IRON, DM_FLOOR_MAX, DM_IRON_CHANCE,
                                                    DM_IRON_R)) {
                            b = BLK_IRON_ORE;
                        } else if (worldgen_vein_at(wx, y, wz, seed, S_DM_COAL, DM_FLOOR_MAX, DM_COAL_CHANCE,
                                                    DM_COAL_R)) {
                            b = BLK_COAL_ORE;
                        }
                    }
                } else if (y == fy && wet) {
                    // The patch of lava sits ON the floor, rather than
                    // replacing it: a pool you can walk around is a
                    // hazard, a hole in the floor is a way to lose the
                    // floor. It is the surface cell -- the one the player
                    // would be standing in -- so a pack is never rolled
                    // into one (game/mob.c).
                    b = BLK_LAVA;
                }

                c->id[CH_IDX(lx, y, lz)] = b;
            }
        }
    }

    // THE SHAFTS, and on the same grid the trees are on. A shaft is a
    // shape that straddles chunk borders, so this walks the 3x3
    // neighbourhood and stamps only the cells landing here -- the same
    // argument as worldgen.c's tree pass, and the reason there is one
    // shape in the codebase rather than two.
    mineshaft_stamp(c, seed);

    // The bottom of every column, exactly as the overworld's generator
    // leaves it. It is the same rule, so it is the same function -- a
    // Deep Mine with its own idea of what a floor is would be two floors
    // in the codebase.
    worldgen_force_floor(c);

    c->flags |= CF_GENERATED;
    chunk_resummarise(c);
}

// --- Population (D-147) -----------------------------------------------

bool deepmine_pack(int32_t cx, int32_t cz, uint32_t seed, int* n) {
    uint32_t const r = sm_hash3(cx, 0, cz, seed ^ S_DM_PACK);
    if ((r % 100u) >= (uint32_t)DM_PACK_CHANCE_PCT) return false;

    int const span = DM_PACK_MAX - DM_PACK_MIN + 1;
    if (n != NULL) *n = DM_PACK_MIN + (int)((r >> 8) % (uint32_t)span);
    return true;
}
