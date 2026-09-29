#pragma once
// =====================================================================
//  SynthMiner  --  farming: tilled soil, five crops, and slow time
// ---------------------------------------------------------------------
//  THE SECOND TIER, and the first customer it has had that is a BLOCK
//  rather than a record. world/blockupdate.h is tier 1 -- fast, ticking,
//  sub-second, for water and one day falling sand -- and its own header
//  says a growing wheat field will never enter that queue. This is the
//  other tier: `now - stamp`, worked out when somebody looks, the trick
//  the furnace and the trashcan already run on (D-51, D-98).
//
//  A CROP IS A BLOCK, AND THAT IS THE WHOLE DIFFICULTY. Its STAGE fits
//  in the state byte -- three bits, free, reserved for this since step
//  0.3 -- but WHEN IT LAST GREW does not, and there are far too many
//  cells in a field for the block-entity pool (192 records across the
//  whole resident ring, world/blockent.h).
//
//  So the clock is kept PER CHUNK: chunk_t.stamp, saved in a section of
//  its own (SECTION_CHUNK_CLOCK). Growth is quantised to that one clock
//  rather than to each plant's own, which has two consequences worth
//  knowing and neither of which is a problem:
//
//    * a field ripens in steps, together, which looks like a field
//      ripening rather than like sixty independent timers;
//    * a seed planted just before the chunk's next step gets that step
//      almost free -- at most one stage, and on average half of one.
//
//  WHAT IT COSTS. Nothing scans the world. crops_tick() visits ONE chunk
//  slot per tick -- 256 slots, so a full round is 13 seconds at 20 Hz --
//  and a chunk with no crops in it is a flag test (CF_CROPS). A chunk
//  that does have crops is walked column by column, skipping to each
//  column's own height from the summary the chunk already keeps. A
//  player standing in a field pays for one field.
//
//  AND A CHUNK AWAY FOR AN HOUR SHOWS THE HOUR. crops_chunk_join() runs
//  as the chunk lands, from the same line of chunk_worker.c's apply() as
//  the light and the physics seam, and advances everything in it by the
//  time that passed while nobody was looking. That is the user's own
//  requirement: "When a block is unloaded, then loaded again, we can
//  advance events in one go to where they would be now as if the chunk
//  was never unloaded."
//
//  THE WATER RULE IS NOT WATCHED (D-106, the user's amendment). Tilled
//  soil is wet or dry, and which one is decided when it is TILLED or
//  when a seed is offered to it -- on a keypress, and nowhere else. Dry
//  soil refuses the seed rather than taking it and never growing. So
//  nothing here asks about water while a crop grows, the fluid scheduler
//  needs no four-block wake radius (its neighbourhood is one cell, by
//  design), and a moat drained after planting does not kill the crop.
//
//  Pure: no engine, no RTOS, no allocation. tools/worldcheck.c drives it.
// =====================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "world/chunk.h"

// Ticks a crop takes to gain one stage: 2.5 minutes at 20 Hz, so four
// stages is about seven and a half minutes of playing from seed to
// harvest -- long enough to be worth waiting for, short enough that a
// field is worth planting in the first place.
#define CROP_STAGE_TICKS 3000u

// How far a water block may be and still wet the soil, on the same
// level: the user's "within 4 blocks". Measured as a box, which is
// Minecraft's shape and the one people expect -- a 9 x 9 of cells
// centred on the soil, minus the soil itself.
#define FARM_WATER_RANGE 4

// A crop's growth stage, out of the state byte's bits 1..3.
static inline uint8_t crop_stage(uint8_t state) {
    return (uint8_t)(st_data(state) & 0x07u);
}

// The state byte for `stage`, keeping every other bit of `state`.
static inline uint8_t crop_state_with(uint8_t state, uint8_t stage) {
    uint8_t const d = (uint8_t)((st_data(state) & ~0x07u) | (stage & 0x07u));
    return st_with_data(state, d);
}

// Ripe: at its own last stage, which is a per-block number
// (block_def_t.growth_max) and not a constant.
bool crop_is_ripe(uint8_t block, uint8_t state);

// --- The clock --------------------------------------------------------

// One chunk slot's worth of slow time, once a tick. Cheap by
// construction: see the header note.
//
// It also REMEMBERS `now`, which is how a chunk landing on the worker's
// behalf can be caught up: the world clock lives in main.c's world
// metadata (D-52) and nothing under world/ has ever had a reason to know
// it. One cached tick count beats threading it through the loader.
void crops_tick(uint32_t now);

// The clock crops_tick() last saw. Zero before the first tick of a
// world, which crops_chunk_join() reads as "no time has passed".
uint32_t crops_now(void);

// A chunk has arrived (main task, from apply()). Finds what is growing
// in it, marks the chunk, and advances it by however long it was away.
void crops_chunk_join(chunk_t* c, uint32_t now);

// A block changed anywhere: if a crop appeared, the chunk has to know it
// has crops in it. Called from world_set, beside the light and the
// physics hooks, so no caller can forget.
void crops_block_changed(int32_t x, int32_t y, int32_t z, uint8_t was, uint8_t now_block);

// Advance the crop at (x, y, z) by `steps` stages, stopping at ripe.
// Returns the stages actually gained. Used by the sweep and by compost.
int crops_advance(int32_t x, int32_t y, int32_t z, int steps);

// --- The rules a keypress asks about ----------------------------------

// Is there water within FARM_WATER_RANGE of (x, y, z), on its level?
bool crops_water_near(int32_t x, int32_t y, int32_t z);

// Till the cell at (x, y, z) -- grass or dirt with nothing on top.
// Becomes wet or dry farmland depending on the water. False if this is
// not something a hoe can turn over.
bool crops_till(int32_t x, int32_t y, int32_t z);

// Why a seed did or did not go in. The caller turns this into the line
// the player reads, because only it knows the language (i18n.h).
typedef enum {
    PLANT_OK = 0,
    PLANT_NOT_SEED,     // whatever is in hand does not plant anything
    PLANT_NEEDS_SOIL,   // it wants tilled soil and this is not
    PLANT_TOO_DRY,      // tilled soil, but no water within four blocks
    PLANT_NEEDS_WATER,  // rice: one-deep water standing on sand
    PLANT_BLOCKED,      // something is already in the way
} plant_result_t;

// Plant `seed` at the cell the player pointed AT -- the soil or the
// water, not the air above it. Where the plant actually goes is this
// function's business: on top of soil, or into the water itself.
plant_result_t crops_plant(int32_t x, int32_t y, int32_t z, uint16_t seed);

// The crop `seed` plants, or BLK_AIR if it plants nothing.
uint8_t crops_block_for_seed(uint16_t seed);

// --- Saving the clock -------------------------------------------------

// Write the chunk's clock as a SECTION_CHUNK_CLOCK section, header and
// all. Returns the bytes written; never 0 for a valid chunk, because a
// chunk with no crops still has a clock and losing it would make the
// first crop planted after a reload ripen on the spot.
size_t crops_encode_chunk(chunk_t const* c, uint8_t* out, size_t cap);

// Read one back, into the chunk being decoded. Runs on the core-1
// worker while the chunk is CS_LOADING, which is the one moment the
// worker owns it (Part K).
void crops_decode_section(chunk_t* c, uint8_t const* data, size_t len);

// How many crop cells are in this chunk, and how many are ripe. For the
// host checks and for anything that wants to say "your field is ready".
int crops_count_in(chunk_t const* c, int* ripe_out);
