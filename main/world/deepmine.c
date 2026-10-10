// =====================================================================
//  SynthMiner  --  the Deep Mine (see deepmine.h, and Part M)
// =====================================================================

#include "world/deepmine.h"

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
#define S_DM_LAVA     0xE200u
#define S_DM_IRON     0xE300u
#define S_DM_COAL     0xE350u
#define S_DM_DIAMOND  0xE400u
#define S_DM_PACK     0xE500u

// The cave is wide rather than lumpy. A floor that changed every column
// would make a room with no walls, and the whole point of the Deep Mine
// is that it reads as one big room you can see the far side of.
#define DM_FLOOR_SCALE 64.0f

// --- The floor --------------------------------------------------------

int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed) {
    // Plain value noise, not fbm. Two octaves never reach 1.0 (F-126), so
    // an fbm floor would sit low in its band for ever and the top of the
    // band would be a number nothing ever produces. This one is honest
    // about the range it can produce.
    float const n = sm_noise2((float)x, (float)z, DM_FLOOR_SCALE, seed ^ S_DM_FLOOR);
    int const   span = DM_FLOOR_MAX - DM_FLOOR_MIN + 1;
    int         y    = DM_FLOOR_MIN + (int)(n * (float)span);
    if (y > DM_FLOOR_MAX) y = DM_FLOOR_MAX;
    return y;
}

// --- Lava -------------------------------------------------------------

// A patch, not a coin flip per cell: one candidate per coarse cell, and a
// column is lava if it falls inside one. The same shape as an ore vein,
// for the same reason -- fill_column has to give the same answer from
// either side of a chunk border, so nothing here can be grown by walking.
bool deepmine_lava(int32_t x, int32_t z, uint32_t seed) {
    int32_t const gx = (x >= 0 ? x : x - VEIN_GRID + 1) / VEIN_GRID;
    int32_t const gz = (z >= 0 ? z : z - VEIN_GRID + 1) / VEIN_GRID;

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
            bool    const wet = deepmine_lava(wx, wz, seed);

            for (int y = 0; y < CH_H; y++) {
                uint8_t b = BLK_AIR;

                if (y >= DM_CEILING_Y) {
                    // The sealed ceiling. Every column, every time -- a
                    // hole here is the difference between a cave and a
                    // shaft open to the sky, and the check that says so
                    // walks a fixed sample of columns.
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
