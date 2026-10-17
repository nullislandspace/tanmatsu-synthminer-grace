// =====================================================================
//  SynthMiner  --  the Deep Mine (see deepmine.h, and Part M)
// =====================================================================

#include "world/deepmine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common/rng.h"
#include "world/blocks.h"
#include "world/worldgen.h"

// The salts. Separate from worldgen.c's, because a vein in the Deep Mine
// is the same SHAPE (worldgen_vein_at) with different odds, and sharing a
// salt would put the two dimensions' ore in the same places -- which is
// exactly the kind of coincidence a check would never notice.
#define S_DM_WALL    0xE120u
#define S_DM_CEIL_W  0xE160u
#define S_DM_BROAD_F 0xE180u
#define S_DM_HEAT    0xE1A0u
#define S_DM_DAMP    0xE1B0u
#define S_DM_MASS    0xE1C0u
#define S_DM_LOW     0xE1D0u
#define S_DM_ROOM    0xE1E0u
#define S_DM_IRON    0xE300u
#define S_DM_COAL    0xE350u
#define S_DM_DIAMOND 0xE400u
#define S_DM_TITANIUM 0xE450u
#define S_DM_PACK    0xE500u

// The four biome fields' scales, in blocks. Measured, not guessed: a
// 2-octave value-noise field has a mean that depends on where the sample
// window sits, and at 170-300 blocks that swing is huge -- 192x192 windows
// gave means from 0.27 to 0.73, so a fixed edge is a dice roll and a biome
// either never appears or takes half the map. At 80-95 the same windows are
// stable (tools/worldcheck.c prints the shares every run). They are still
// far slower than the terrace and boulder terms and far faster than the
// broad surfaces: a biome is a few minutes across on foot, near enough to
// find another and far enough to feel like somewhere.
#define DM_HEAT_SCALE 90.0f
#define DM_DAMP_SCALE 85.0f
#define DM_MASS_SCALE 95.0f
#define DM_LOW_SCALE  80.0f

// --- The biome table --------------------------------------------------

dm_biome_def_t const DM_BIOMES[DM_BIOME_COUNT] = {
    // CAVERN: the default, and the big room. Its room is the tallest in the
    // cave -- 24 blocks at its openest -- and its relief is two thirds of it,
    // so a cavern is neither one hall nor one squeeze: the same district is
    // both, depending on where the room field is.
    [DM_BIOME_Cavern] =
        {.name         = "Cavern",
         .floor_base   = 10,
         .floor_relief = 10,
         .room_base    = 24,
         .room_relief  = 16,
         .iron         = 0.34f,
         .iron_r       = 2.0f,
         .coal         = 0.30f,
         .coal_r       = 2.0f,
         .diamond      = 0.05f,
         .diamond_r    = 1.2f,
         .titanium     = 0.05f,
         .titanium_r   = 1.2f},


    // HILLS: the macro roughness the user asked for. The floor's relief is
    // nearly twice the cavern's, so the broad field makes real slopes and
    // real drops, and the room stays big enough that you can see the slope
    // you are standing on rather than only the next block of it.
    [DM_BIOME_Hills] =
        {.name         = "Hills",
         .floor_base   = 12,
         .floor_relief = 16,
         .room_base    = 26,
         .room_relief  = 18,
         .iron         = 0.30f,
         .iron_r       = 2.2f,
         .coal         = 0.26f,
         .coal_r       = 2.0f,
         .diamond      = 0.04f,
         .diamond_r    = 1.2f,
         .titanium     = 0.04f,
         .titanium_r   = 1.2f},


    // LAVA SEA: low ground with a lake on it. The lava is a LEVEL, so this
    // is not "occasional blobs" -- it is a sea with shores, and the ground
    // dips in and out of it. It is a biome rather than a rule because its
    // floor sits BELOW DM_LAVA_LEVEL: 8 with a relief of 6, where every
    // other biome starts at 10 or 12. The sea is the level; this is the
    // ground that lies under it. The richest iron, and the diamond the user
    // asked for, because a sea is where a cluster is worth finding.
    [DM_BIOME_LavaSea] =
        {.name         = "LavaSea",
         .floor_base   = 8,
         .floor_relief = 6,
         .room_base    = 18,
         .room_relief  = 12,
         .iron         = 0.40f,
         .iron_r       = 2.0f,
         .coal         = 0.20f,
         .coal_r       = 1.8f,
         .diamond      = 0.08f,
         .diamond_r    = 1.4f,
         .titanium     = 0.08f,
         .titanium_r   = 1.4f},


    // LOW HALL: the claustrophobic one. Its roof base sits barely above its
    // floor base, so the room is a few blocks -- and its relief is BIGGER
    // than its base, which is the whole point: the room field can take more
    // height than there is, the roof lands ON the floor or under it, and the
    // column is solid rock. That is a wall of the cave, and walking around
    // it is what makes the place feel enclosed. It is also the biome a
    // player has to dig through, which is what a mine is.
    [DM_BIOME_LowHall] =
        {.name         = "LowHall",
         .floor_base   = 12,
         .floor_relief = 8,
         .room_base    = 9,
         .room_relief  = 12,
         .iron         = 0.36f,
         .iron_r       = 1.8f,
         .coal         = 0.34f,
         .coal_r       = 2.0f,
         .diamond      = 0.03f,
         .diamond_r    = 1.0f,
         .titanium     = 0.03f,
         .titanium_r   = 1.0f},

};

// --- The fields -------------------------------------------------------

// Which grid square a column belongs to. Negative-safe, because C division
// rounds toward zero: -1 / 16 is 0, so the square it names is not the one the
// generator would have read for that column. Every grid in this file -- the
// terraces, the room, the veins -- goes through here for that reason.
static int32_t dm_square(int32_t v, int grid) {
    return (v >= 0 ? v : v - grid + 1) / grid;
}

// One value per grid square: 0..span-1. Everything inside a square is the
// same height, which is what makes it a terrace, and the step from one
// square to the next is the wall.
static int dm_plateau(int32_t x, int32_t z, uint32_t seed, int grid, int span, uint32_t salt) {
    int32_t const gx = dm_square(x, grid), gz = dm_square(z, grid);
    return (int)(sm_rand2(gx, gz, seed ^ salt) * (float)span);
}

// A height inside the band it is allowed to be in. The bands are what the
// host check asserts, so this is the one place they are enforced.
static int dm_clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// How far a smoothstep takes a field past an edge. The same helper
// worldgen.c uses: a hard threshold would make every biome border a line,
// and the height numbers have to be blended for the ground to be continuous.
#define DM_BAND 0.09f

static float dm_over(float v, float edge) {
    float t = (v - (edge - DM_BAND)) / (2.0f * DM_BAND);
    t       = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static float dm_under(float v, float edge) {
    return 1.0f - dm_over(v, edge);
}

void deepmine_biome_weights(int32_t x, int32_t z, uint32_t seed, float w[DM_BIOME_COUNT]) {
    // Four fields, because a cave has more than one axis to vary along: heat
    // (which is what makes a lava sea), damp, mass (which is what makes a
    // hill) and lowness (which is what makes a hall the roof lands on).
    float const heat = sm_fbm2((float)x, (float)z, DM_HEAT_SCALE, 2, seed ^ S_DM_HEAT);
    float const damp = sm_fbm2((float)x, (float)z, DM_DAMP_SCALE, 2, seed ^ S_DM_DAMP);
    float const mass = sm_fbm2((float)x, (float)z, DM_MASS_SCALE, 2, seed ^ S_DM_MASS);
    float const low  = sm_fbm2((float)x, (float)z, DM_LOW_SCALE, 2, seed ^ S_DM_LOW);

    // In order of precedence, each taking what the ones before it left --
    // the same shape as the overworld's, so a place is one thing and not a
    // compromise between four. A low hall is a low hall whatever the climate
    // is; hot AND damp ground is a lava sea; heavy ground is hills; the rest
    // is cavern. The edges sit where the measured distributions put them, so
    // that over a 512x512 window on any seed the shares land near cavern
    // 40-58% / hills 6-24% / lava sea 6-13% / low hall 20-38%. The low edge
    // came down from 0.66 to 0.60 with the room model: the claustrophobic
    // biome is the one the user asked for, and it has to be findable.
    w[DM_BIOME_LowHall] = dm_over(low, 0.60f);
    w[DM_BIOME_LavaSea] = (1.0f - w[DM_BIOME_LowHall]) * dm_over(heat, 0.52f) * dm_under(damp, 0.43f);
    w[DM_BIOME_Hills]   = (1.0f - w[DM_BIOME_LowHall]) * (1.0f - w[DM_BIOME_LavaSea]) * dm_over(mass, 0.62f);
    float rest          = 1.0f - w[DM_BIOME_LowHall] - w[DM_BIOME_LavaSea] - w[DM_BIOME_Hills];
    w[DM_BIOME_Cavern]  = rest < 0.0f ? 0.0f : rest;
}

// THE LARGEST SHARE, not a second set of thresholds: one rule means the
// shape of the ground and the lava and ore it carries can never disagree
// about where a place is. One function answers it because the column and the
// public biome question must never pick different biomes.
static int dm_pick(float const w[DM_BIOME_COUNT]) {
    int best = 0;
    for (int b = 1; b < DM_BIOME_COUNT; b++) {
        if (w[b] > w[best]) best = b;
    }
    return best;
}

uint8_t deepmine_biome(int32_t x, int32_t z, uint32_t seed) {
    float w[DM_BIOME_COUNT];
    deepmine_biome_weights(x, z, seed, w);
    return (uint8_t)dm_pick(w);
}

// --- The shell: a floor, and a room on top of it ----------------------

// Everything a column is asked about comes out of ONE answer. Four public
// questions (ground, ceiling, lava, standing room) plus the chunk filler all
// want the same column, and computing them separately is how the surfaces,
// the lakes and the ore ended up disagreeing about which place a column is
// in. It is also the cheap way: the fields are the expensive part, and one
// call answers five questions with them instead of five calls.
typedef struct {
    int     floor_y;   // first air above the ground
    int     roof_y;    // first bedrock above the cave
    int     lava_top;  // top cell of the lava at this column, -1 when dry
    int     stand_y;   // the cell a body actually stands in
    uint8_t biome;
} dm_col_t;

static void dm_column(int32_t x, int32_t z, uint32_t seed, dm_col_t* out) {
    float w[DM_BIOME_COUNT];
    deepmine_biome_weights(x, z, seed, w);
    int             const b  = dm_pick(w);
    dm_biome_def_t const* bd = &DM_BIOMES[b];

    // THE FLOOR: blended height numbers, because the floor is what you walk
    // on and a biome border may not be a cliff you cannot climb (header).
    // The hill first -- 120 blocks of slope, the only term that can make a
    // macro hill -- then the terrace, which is the cliff.
    float base = 0.0f, relief = 0.0f;
    for (int i = 0; i < DM_BIOME_COUNT; i++) {
        base += w[i] * (float)DM_BIOMES[i].floor_base;
        relief += w[i] * (float)DM_BIOMES[i].floor_relief;
    }
    float const broad = sm_fbm2((float)x, (float)z, DM_BROAD_SCALE, DM_BROAD_OCTAVES, seed ^ S_DM_BROAD_F);
    int     const fy  = dm_clamp((int)(base + broad * relief) +
                                  dm_plateau(x, z, seed, DM_WALL_GRID, DM_WALL_SPAN, S_DM_WALL),
                                  DM_FLOOR_MIN, DM_FLOOR_MAX);

    // THE ROOM: this biome's number, NOT blended, taken away by a field of
    // its own and then stepped by its own terraces. Where the field takes
    // more than the base -- LowHall, whose relief is bigger than its base --
    // the roof lands on the floor and the column is solid rock.
    float const field = sm_fbm2((float)x, (float)z, DM_ROOM_SCALE, DM_ROOM_OCTAVES, seed ^ S_DM_ROOM);
    int     const room = (int)((float)bd->room_base - field * (float)bd->room_relief) -
                         dm_plateau(x, z, seed, DM_CEIL_GRID, DM_CEIL_SPAN, S_DM_CEIL_W);
    int     const cy   = dm_clamp(fy + room, DM_CEIL_MIN, DM_CEILING_Y);

    // THE LAKE: this column's floor is under the one level the whole
    // dimension has, so it is filled up to that level -- clamped below the
    // roof, because a lake pressed into the ceiling has no air above it and
    // the crossing needs room under a roof (mineshaft.c).
    int top = -1;
    if (fy <= DM_LAVA_LEVEL) {
        top = DM_LAVA_LEVEL;
        if (top > cy - 1) top = cy - 1;
        if (top < fy) top = -1;
    }

    out->floor_y  = fy;
    out->roof_y   = cy;
    out->lava_top = top;
    out->stand_y  = top >= fy ? top + 1 : fy;
    out->biome    = (uint8_t)b;
}

// --- Asking one column ------------------------------------------------
//
// Each one is a one-line read of the same answer, which is the point: the
// shaft pairing, the mob population and the chunk filler all ask a different
// one of these, and they can only agree about a column if they are all the
// same function of it.

int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed) {
    dm_col_t col;
    dm_column(x, z, seed, &col);
    return col.floor_y;
}

int deepmine_ceiling_y(int32_t x, int32_t z, uint32_t seed) {
    dm_col_t col;
    dm_column(x, z, seed, &col);
    return col.roof_y;
}

// THE LAVA IS A LEVEL, not a patch. That is the whole of why it behaves:
// fill every column whose floor is below the level, up to the level, and the
// surface comes out flat, the shore comes out where the ground rises, and
// the lake is walled by its own basin. A blob rolled per coarse cell -- what
// this file used to do -- put a block of lava on top of a floor it was not
// connected to, which is what the user saw as "lava blocks just standing
// around".
int deepmine_lava_top(int32_t x, int32_t z, uint32_t seed) {
    dm_col_t col;
    dm_column(x, z, seed, &col);
    return col.lava_top;
}

// The cell a body stands in: above the floor, and out of the lava. The floor
// function answers "first air above the rock"; this adds the lake, which is
// the question a mob, a crossing or a spawn actually asks. Without it a
// zombie is generated standing in a lava lake, because the cell is empty.
int deepmine_stand_y(int32_t x, int32_t z, uint32_t seed) {
    dm_col_t col;
    dm_column(x, z, seed, &col);
    return col.stand_y;
}

bool deepmine_has_room(int32_t x, int32_t z, uint32_t seed) {
    dm_col_t col;
    dm_column(x, z, seed, &col);
    return col.stand_y < col.roof_y;
}

// --- A chunk ----------------------------------------------------------

void deepmine_chunk(chunk_t* c, uint32_t seed) {
    if (c == NULL) return;

    memset(c->st, 0, CH_CELLS);

    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            int32_t const wx = c->cx * CH_W + lx, wz = c->cz * CH_D + lz;
            dm_col_t            col;
            dm_column(wx, wz, seed, &col);
            int         const fy  = col.floor_y;
            int         const cy  = col.roof_y;
            int         const lvl = col.lava_top;

            // The biome's own rates, from the same discrete answer the lava
            // and the roof came from -- so a column's ore, its lake and its
            // room can never disagree about which place it is in.
            dm_biome_def_t const* bd = &DM_BIOMES[col.biome];

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
                    // convention as worldgen_height(), so the solid part of
                    // a column is everything BELOW it. That is what makes
                    // the pairing work: the arrival cell is the cell the
                    // player stands in, and it is knowable before the chunk
                    // exists.
                    //
                    // Where cy <= fy this branch simply stops at the roof:
                    // the column is solid from the bottom of the world up to
                    // the ceiling, which is a wall of the cave.
                    b = BLK_STONE;
                    // Ore, in the same vein shape the overworld uses, with
                    // this biome's odds and the floor as its ceiling. The two
                    // rare ones first, for the same reason iron is before coal
                    // in fill_column: two ores cannot fight over one cell, and
                    // the one that loses should be the one that is worth less.
                    //
                    // Diamond before titanium rather than the other way round
                    // is a hair of an argument, not a rule: the two are rolled
                    // on different salts at the same odds, so the only cost of
                    // the order is the cells where both would have landed, and
                    // those go to the gem.
                    if (y > CH_BEDROCK) {
                        if (worldgen_vein_at(wx, y, wz, seed, S_DM_DIAMOND, fy, bd->diamond, bd->diamond_r)) {
                            b = BLK_DIAMOND_ORE;
                        } else if (worldgen_vein_at(wx, y, wz, seed, S_DM_TITANIUM, fy, bd->titanium,
                                                    bd->titanium_r)) {
                            b = BLK_TITANIUM_ORE;
                        } else if (worldgen_vein_at(wx, y, wz, seed, S_DM_IRON, fy, bd->iron, bd->iron_r)) {
                            b = BLK_IRON_ORE;
                        } else if (worldgen_vein_at(wx, y, wz, seed, S_DM_COAL, fy, bd->coal, bd->coal_r)) {
                            b = BLK_COAL_ORE;
                        }
                    }
                } else if (y <= lvl) {
                    // The lake, from the floor up to its level. Sources all
                    // the way down, exactly as the overworld fills below
                    // CH_SEA_LEVEL -- which is also what keeps them settled:
                    // a source standing on its own fluid does not spread
                    // (fluid.c), and the basin walls it on every side.
                    b = BLK_LAVA;
                }

                c->id[CH_IDX(lx, y, lz)] = b;
            }
        }
    }

    // The bottom of every column, exactly as the overworld's generator
    // leaves it. It is the same rule, so it is the same function -- a Deep
    // Mine with its own idea of what a floor is would be two floors in the
    // codebase.
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
