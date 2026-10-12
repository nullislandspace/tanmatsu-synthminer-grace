#pragma once
// =====================================================================
//  SynthMiner  --  dimensions (see claudeplans/synthminer.md, Part M)
// ---------------------------------------------------------------------
//  One world save, two dimensions: the overworld, and the **Deep Mine**
//  under it. One seed, one `level.smw`, one player, one inventory -- the
//  player's health, hunger and recipe book are shared. What is not
//  shared is the terrain, the entities, the light and the sky.
//
//  ONE DIMENSION IS RESIDENT AT A TIME. The chunk store is keyed by
//  (cx, cz) alone, with no dimension in the key, so it cannot hold both;
//  a switch releases every chunk and the ring refills from the other
//  dimension's region directory. That is why the current dimension is a
//  single global rather than a field of `chunk_t`: every reader that
//  needs the answer needs the same answer at the same moment, and a
//  chunk that disagrees with the store about which world it is in is the
//  bug this design avoids by not having a second answer.
//
//  1:1 in x and z (D-142), so a shaft at (x,z) in either dimension meets
//  the shaft at (x,z) in the other and nothing has to be stored about
//  the pairing. Height is free -- each end stands on its own ground.
//
//  Pure: no engine, no RTOS, no allocation. tools/worldcheck.c builds
//  this as-is.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIM_OVERWORLD = 0,
    DIM_DEEP_MINE = 1,
    DIM_COUNT
} dimension_t;

// --- The Deep Mine's numbers ------------------------------------------
//
// Provisional, on the user's word: these are tuned after playtesting, and
// the checks below measure them rather than the code guessing them.
//
// The playable band is one column: solid ground up to the floor, open
// cave above it, and a sealed bedrock ceiling. Everything has to fit
// inside CH_H 64, and `deepmine_ground_y()` is a pure function of the
// column so the pairing works before the chunk exists.

// The band the cave floor may wander in. Kept clear of CH_BEDROCK (the
// forced floor) and wide enough for the hills: the broad field has 150
// blocks of slope to work with, so a cave with a 6-block band has nowhere
// to put a hill.
#define DM_FLOOR_MIN     8
#define DM_FLOOR_MAX     30
// The roof, and the two numbers that bound it. It is not one flat height:
// a cave with a flat roof and a flat floor is a corridor, and the Deep
// Mine is supposed to read as a room with walls. `DM_CEILING_Y` is the
// hard cap -- no column is open above it, so the ceiling is sealed on
// every column whatever the roof is doing.
//
// THE FLOOR MAY COME OUT ABOVE THE ROOF, and that is not an oversight. A
// column whose roof is below its floor is solid rock from the forced
// floor up to the ceiling: a WALL of the cave rather than part of it, and
// the thing that makes the place enclosed. `DM_CEIL_MIN` sits below
// `DM_FLOOR_MAX` on purpose.
#define DM_CEIL_MIN      18
#define DM_CEILING_Y     44
// The open cells a COLUMN OF AIR must have -- the height of a shaft frame
// (the ground cell plus three cells of post). It is no longer a promise
// every column makes, because a pinched column is a wall; it is what a
// column the player is put in has to offer, which is what
// `mineshaft_pad_y()` looks for over the shaft's footprint.
#define DM_ROOM_MIN      4
// The surface of the lava, an absolute y, exactly the way the overworld's
// sea is CH_SEA_LEVEL. ONE NUMBER FOR THE WHOLE DIMENSION, and that is
// what makes it stable: a column whose floor is below it is wet, one whose
// floor is above it walls its neighbour's lava with solid rock, so a lake
// is walled by its own basin and nothing has to be simulated to know that.
// A per-biome level would leak at every border where a dry biome's ground
// sits lower than a wet one's.
//
// `DM_CEIL_MIN >= DM_LAVA_LEVEL + DM_ROOM_MIN + 1` is the invariant that
// keeps a shaft buildable over a lake: the lowest roof still leaves a
// frame's height of dry air above the highest lava surface.
#define DM_LAVA_LEVEL    12
// "Global illumination" as a number: the sky nibble of the light plane
// is this constant everywhere the cave is open, and block light still
// floods -- a torch matters here, for seeing rather than for spawning.
#define DM_SKY_LIGHT     12

// How many chunks hold a pack, and how big a pack is (D-147).
#define DM_PACK_CHANCE_PCT 12
#define DM_PACK_MIN      2
#define DM_PACK_MAX      3

// Which dimension the game is in right now. Defaults to the overworld,
// so anything that does not know about dimensions -- the host checks,
// the title world -- is answering the same question the game does.
dimension_t dimension_current(void);
void dimension_set_current(dimension_t d);

char const* dimension_name(dimension_t d);

// The directory a dimension's region files live in, relative to the
// world's region directory (D-143). The overworld keeps the flat
// directory so existing worlds are untouched; the Deep Mine is a
// subdirectory of it. No header field: under 1:1 mapping the same
// `r.<rx>.<rz>` name means the same column in both dimensions, so the
// directory IS the dimension.
char const* dimension_region_sub(dimension_t d);

// Is there a sky to draw and a clock to run? The Deep Mine has neither
// (D-139): the ceiling is geometry, and the light is a constant.
bool dimension_has_sky(dimension_t d);

// The sky nibble a cell in this dimension gets when nothing is blocking
// it. LIGHT_MAX for the overworld, DM_SKY_LIGHT here.
int dimension_sky_light(dimension_t d);
