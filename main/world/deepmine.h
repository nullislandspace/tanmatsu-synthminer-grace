#pragma once
// =====================================================================
//  SynthMiner  --  the Deep Mine (see claudeplans/synthminer.md, Part M)
// ---------------------------------------------------------------------
//  The second dimension: a sealed cave, normal stone, lava in its
//  depressions, richer iron, occasional clusters of diamond, and no sky.
//  Nothing here is "nether" -- the comparison to the Nether is about the
//  SHAPE (sealed ceiling, no sky, one cave), not the material.
//
//  WHAT IT HAS TO FEEL LIKE (the user, 2026-10-12): *"more rough on a
//  macro level... Cliffs, sharp drops... a little more claustrophobic.
//  Yes, some huge open spaces, but also some areas where the floor meets
//  the ceiling."* A cave that is one flat room with a bumpy floor is an
//  unfinished overworld, and that is what the first version of this file
//  produced. What it produces now is a SHELL: a floor that rolls in
//  terraces and hills, a roof that hangs off that floor by a height that
//  changes as you walk, terraces of its own, and lakes in the depressions.
//
//  PURE, like every other generator (Part T's determinism rule 3): a
//  chunk's contents are a function of (seed, cx, cz) alone. Nothing reads
//  a neighbouring chunk, nothing reads the clock, nothing draws from
//  rand(). That is what makes the host checks able to assert the ceiling
//  is sealed across a fixed sample, and what makes a replay reproduce the
//  same cave.
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

// --- The two surfaces -------------------------------------------------
//
// THE ROOF IS NOT A SECOND SURFACE, IT IS A DISTANCE. The old version gave
// the floor and the roof each its own broad field and let them wander
// independently, and the result read like an overworld with a lid on it:
// the ceiling never followed the ground, so the cave had no shape. A cave
// is a SHELL -- the roof hangs off the floor by a room height, and what
// varies is that height. So the generator answers for the floor first and
// then adds the room to it.
//
// Two terms each, and each one is there for a different thing the player
// sees. Read once per grid SQUARE, not once per column: a per-column noise
// puts every column one or two blocks from its neighbour, so nothing ever
// reads as a wall, which is exactly the "random one block steps" the user
// complained about.
//
//   THE BROAD FIELD  -- fbm at DM_BROAD_SCALE, two octaves. This is the
//      only term that can make a HILL: about 120 blocks of slope, so the
//      cave has a high side and a low side and a walk from one to the other.
//   THE TERRACE      -- a plateau per DM_WALL_GRID square. This is the
//      CLIFF. A step of up to DM_WALL_SPAN-1 between two squares is a wall
//      you can see and have to walk around, and it is the difference
//      between a slope and a cave. Going DOWN a step is free (falling is,
//      in the player's own physics); going UP more than one block is not,
//      which is what turns a terrace into a cliff rather than a staircase.
//
// THE ROOM is its own field, at DM_ROOM_SCALE -- faster than the biome
// fields and slower than the terrace, so one biome holds both a hall and a
// place where you have to crouch. It is SUBTRACTIVE: room_base is the height
// of the biggest room that biome ever has, and the field takes up to
// room_relief of it away. That is what makes a hall and a pinch come out of
// the same number, and where the field takes more than the base the roof
// lands on the floor and the column is solid rock -- a wall of the cave.
//
// THE FLOOR IS BLENDED BETWEEN BIOMES AND THE ROOM IS NOT, and that is
// deliberate. The floor is what you walk on, so its height has to be
// continuous across a biome border or every border is a cliff you cannot
// climb. The roof is what you look up at: a step in it is a rock face, and
// the biome borders are exactly the district lines a cave system has. The
// floor's numbers are also close together (8-12) so the blend barely moves
// them; the room's are not (9-26), so the blend would have cancelled the
// whole point of the claustrophobic biome.
#define DM_BROAD_SCALE   120.0f
#define DM_BROAD_OCTAVES 2

#define DM_WALL_GRID  16
#define DM_WALL_SPAN  4
#define DM_CEIL_GRID  12
#define DM_CEIL_SPAN  4

// The room field. Faster than the biome fields (80-95) on purpose: a biome
// is a district, and a district that is one height from one side to the
// other is a room, not a cave.
#define DM_ROOM_SCALE   70.0f
#define DM_ROOM_OCTAVES 2

// --- Biomes -----------------------------------------------------------
//
// THE SAME FOURTH REGISTRY the overworld has (worldgen.h), for the same
// reason: adding a kind of place is a row, not an edit in five functions.
// Four of them, and they are the four things the user asked for -- a big
// room, hills, a lava sea, and a low hall where the floor nearly meets
// the ceiling.
//
// Chosen from four slow fields, NOT from the terrain: a biome that
// followed the terrain would put the same place on every dip. The HEIGHT
// NUMBERS are blended between biomes (so a border is a slope, not a
// vertical cliff) and the DISCRETE ONE -- which biome's ore rates -- comes
// from the single largest share, exactly as the overworld's surface block
// does.
typedef enum {
    DM_BIOME_Cavern = 0,
    DM_BIOME_Hills,
    DM_BIOME_LavaSea,
    DM_BIOME_LowHall,
    DM_BIOME_COUNT
} dm_biome_t;

typedef struct {
    char const* name;

    // What the FLOOR does here: the level it sits on, and how far the broad
    // field lifts it above that. Blended between biomes, because the floor
    // has to be continuous. The terrace is shared.
    int   floor_base;
    int   floor_relief;
    // The ROOM above that floor: the tallest the roof ever gets over this
    // biome's ground, and how much of it the room field takes away. NOT
    // blended -- see the header. A biome whose room_base is smaller than
    // its room_relief is the claustrophobic one, because the field can take
    // more room than there is and the roof lands on the floor.
    int   room_base;
    int   room_relief;

    // No lava column here, deliberately: the lava has ONE level for the
    // whole dimension (DM_LAVA_LEVEL), exactly as the overworld's sea has
    // one CH_SEA_LEVEL, and that is what makes it stable -- a column whose
    // floor is above the level walls its neighbour's lava with solid rock,
    // so a lake is walled by its own basin. A level per biome would leak at
    // every border where a dry biome's ground sits lower than a wet one's.
    // What makes a biome a LAVA SEA is a floor_base low enough to fall
    // below that one level: LavaSea's 8 is under it, every other biome's
    // 10-12 is at or over it.

    // Ore, in the shape worldgen_vein_at already knows: `chance` is the
    // share of coarse cells carrying a vein, `r` its rough radius. Iron is
    // richer here than above (D-139); diamond is the occasional cluster.
    float iron, iron_r;
    float coal, coal_r;
    float diamond, diamond_r;
    // Titanium at diamond's odds, because the user asked for "just as rare
    // as diamonds". It is a separate salt and a separate pass, so the two
    // rare veins are rolled independently and only fight over a cell when
    // both land on the same one.
    float titanium, titanium_r;
} dm_biome_def_t;

extern dm_biome_def_t const DM_BIOMES[DM_BIOME_COUNT];

// The biome at (x, z), and each biome's smooth share of it. Pure, like
// everything else here, so two chunks agree along their border.
uint8_t deepmine_biome(int32_t x, int32_t z, uint32_t seed);
void    deepmine_biome_weights(int32_t x, int32_t z, uint32_t seed, float w[DM_BIOME_COUNT]);

// --- Asking a column --------------------------------------------------

// The y of the first air above the cave floor at this column -- the cell
// the player stands in. Same convention as worldgen_height(), so the two
// dimensions answer the same question the same way.
int deepmine_ground_y(int32_t x, int32_t z, uint32_t seed);

// The y of the first bedrock above the cave at this column -- its roof.
// Per column, because the roof is not one flat height -- it is the floor
// plus this column's room. It may come down TO the floor or below it: that
// column is solid rock, a wall of the cave rather than part of it, and that
// is the claustrophobia the user asked for.
int deepmine_ceiling_y(int32_t x, int32_t z, uint32_t seed);

// Is this cell part of the sealed ceiling? Per column for the same reason,
// and the same function the generator fills with: two answers to "is this
// the roof" would be the difference between a cave and a hole in it.
static inline bool deepmine_is_ceiling(int32_t x, int32_t z, int y, uint32_t seed) {
    return y >= deepmine_ceiling_y(x, z, seed);
}

// The top cell of the lava at this column, or -1 for dry ground. A LEVEL,
// not a patch: the same idea as the overworld's sea, so a lake fills the
// depression it sits in and has a shore where the ground rises out of it.
int deepmine_lava_top(int32_t x, int32_t z, uint32_t seed);

// The cell a body stands in at this column: above the floor, and above the
// lava. The floor function answers "first air above the rock"; this adds
// "and not in the middle of a lake", which is the question a mob, a
// crossing or a spawn actually asks.
int deepmine_stand_y(int32_t x, int32_t z, uint32_t seed);

// Is there room to stand at this column at all? False where the roof has
// come down onto the floor -- a column of solid rock.
bool deepmine_has_room(int32_t x, int32_t z, uint32_t seed);

// Fill `c` with the Deep Mine's terrain. The chunk must already carry
// cx / cz. Marks CF_GENERATED and refreshes the column summaries; leaves
// cstate alone (the caller owns the state machine), exactly like
// worldgen_chunk.
void deepmine_chunk(chunk_t* c, uint32_t seed);

// Does this chunk hold a pack, and how big is it (D-147)? Rolled from
// the world's hash, so a replay reproduces the same population and the
// host check can assert the share across a fixed sample.
bool deepmine_pack(int32_t cx, int32_t cz, uint32_t seed, int* n);
