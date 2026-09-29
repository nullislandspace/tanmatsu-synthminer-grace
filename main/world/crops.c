// =====================================================================
//  SynthMiner  --  farming (see crops.h)
// =====================================================================

#include "world/crops.h"

#include "items/items.h"
#include "world/chunk_codec.h"
#include "world/fluid.h"

#define SLOT_COUNT (CH_RING * CH_RING)

// Where the round-robin sweep is. One slot a tick, so 256 ticks -- 13
// seconds -- is a full round of the resident world.
static int s_sweep;

// The world clock, as of the last tick. See crops_now() in the header.
static uint32_t s_now;

uint32_t crops_now(void) {
    return s_now;
}

bool crop_is_ripe(uint8_t block, uint8_t state) {
    if (!block_crop(block)) return false;
    return crop_stage(state) >= block_def(block)->growth_max;
}

uint8_t crops_block_for_seed(uint16_t seed) {
    if (seed == 0) return BLK_AIR;
    // Table-driven, like everything else: a sixth crop is a row in
    // blocks.c with its seed named, and nothing here changes.
    for (int b = 1; b < BLK_COUNT; b++) {
        block_def_t const* d = &BLOCKS[b];
        if ((d->flags & BF_CROP) != 0 && d->seed_item == seed) return (uint8_t)b;
    }
    return BLK_AIR;
}

int crops_advance(int32_t x, int32_t y, int32_t z, int steps) {
    if (steps <= 0) return 0;
    uint8_t const b = world_block(x, y, z);
    if (!block_crop(b)) return 0;

    uint8_t const max = block_def(b)->growth_max;
    uint8_t const st  = world_state(x, y, z);
    uint8_t const was = crop_stage(st);
    if (was >= max) return 0;

    int want = (int)was + steps;
    if (want > (int)max) want = (int)max;
    world_set(x, y, z, b, crop_state_with(st, (uint8_t)want));
    return want - (int)was;
}

// --- The clock --------------------------------------------------------

// Walk one chunk's crops, advancing each by `steps`. Column by column,
// and only as far up as that column actually reaches: the chunk already
// keeps the summary (chunk_t.top), so an empty sky costs nothing.
//
// Returns how many crop cells were found -- which is how the CF_CROPS
// flag gets CLEARED again once a field is harvested, so an empty chunk
// stops being walked at all.
static int walk_chunk(chunk_t* c, int steps, int* ripe_out) {
    int found = 0, ripe = 0;
    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            int const top = (int)c->top[lz * CH_W + lx];
            if (top <= 0) continue;
            uint8_t const* col = &c->id[CH_IDX(lx, 0, lz)];
            for (int y = 0; y < top; y++) {
                if (!block_crop(col[y])) continue;
                found++;
                int32_t const wx = c->cx * CH_W + lx, wz = c->cz * CH_D + lz;
                // Through world_set (crops_advance), not by poking the
                // plane: a stage is a visible change, so the mesh has to
                // be marked stale and the chunk has to be marked edited.
                if (steps > 0) crops_advance(wx, y, wz, steps);
                if (crop_is_ripe(col[y], c->st[CH_IDX(lx, y, lz)])) ripe++;
            }
        }
    }
    if (ripe_out != NULL) *ripe_out = ripe;
    return found;
}

// How many whole stages the chunk owes, and move its clock on by exactly
// that much -- the remainder is KEPT, so growth does not drift and a
// chunk visited often grows at the same rate as one visited rarely.
static int steps_due(chunk_t* c, uint32_t now) {
    // A CLOCK OF ZERO MEANS "NEVER SET", not "the first tick of the
    // world". A chunk that reached CS_READY without going through
    // crops_chunk_join -- a test harness, a title world, anything built
    // by hand -- would otherwise be told that a hundred thousand ticks
    // had passed and would ripen every seed in it on the spot.
    //
    // Zero is safe to spend on this: a new world's clock starts at
    // DAY_START (worldstore.c), never at 0.
    if (c->stamp == 0u) {
        c->stamp = now;
        return 0;
    }

    // Time only runs forward. A stamp from the future means the world's
    // clock went back under it (a restored save, a test that rewinds);
    // treat it as no time at all rather than as four billion ticks of
    // free growth -- the same rule furnace_catch_up() uses.
    uint32_t const elapsed = now >= c->stamp ? now - c->stamp : 0u;
    uint32_t const steps   = elapsed / CROP_STAGE_TICKS;
    if (steps == 0) {
        if (now < c->stamp) c->stamp = now;
        return 0;
    }
    c->stamp += steps * CROP_STAGE_TICKS;
    // A chunk that has been away for a week owes more stages than any
    // crop has. Cap it: the arithmetic below is per cell, and 400000 is
    // as ripe as 4.
    return steps > 8u ? 8 : (int)steps;
}

void crops_tick(uint32_t now) {
    s_now = now;
    // ONE SLOT A TICK, and almost always nothing at all: an unused slot
    // and a chunk with no crops in it are both a couple of compares.
    chunk_t* c = chunk_slot_at(s_sweep);
    s_sweep    = (s_sweep + 1) % SLOT_COUNT;
    if (c == NULL || c->cstate != CS_READY || c->id == NULL) return;

    int const steps = steps_due(c, now);
    if ((c->flags & CF_CROPS) == 0) return;  // the clock still moves; nothing grows
    if (steps <= 0) return;

    if (walk_chunk(c, steps, NULL) == 0) c->flags &= (uint8_t)~CF_CROPS;
}

void crops_chunk_join(chunk_t* c, uint32_t now) {
    if (c == NULL || c->id == NULL) return;

    // A chunk with no clock of its own has never been saved by a build
    // that had one -- a fresh generation, or a world written before step
    // 9. Either way "now" is the only honest answer: pretending it was
    // stamped at tick 0 would ripen every field in an old world the
    // moment it was walked into.
    if (c->stamp == 0u) c->stamp = now;

    int const steps = steps_due(c, now);
    int const found = walk_chunk(c, steps, NULL);
    if (found > 0) c->flags |= CF_CROPS;
    else c->flags &= (uint8_t)~CF_CROPS;
}

void crops_block_changed(int32_t x, int32_t y, int32_t z, uint8_t was, uint8_t now_block) {
    (void)was;
    if (!block_crop(now_block)) return;  // harvests clear the flag in the sweep
    chunk_t* c = chunk_find(chunk_of(x), chunk_of(z));
    if (c == NULL) return;
    (void)y;
    c->flags |= CF_CROPS;
}

// --- The rules a keypress asks about ----------------------------------

bool crops_water_near(int32_t x, int32_t y, int32_t z) {
    for (int32_t dz = -FARM_WATER_RANGE; dz <= FARM_WATER_RANGE; dz++) {
        for (int32_t dx = -FARM_WATER_RANGE; dx <= FARM_WATER_RANGE; dx++) {
            if (dx == 0 && dz == 0) continue;
            uint8_t const b = world_block(x + dx, y, z + dz);
            // BF_LIQUID rather than BLK_WATER, so lava does not wet a
            // field when it arrives -- and rice, which IS water as well
            // as a plant, counts (BF2_WATERLOGGED).
            if (block_liquid(b) || block_waterlogged(b)) return true;
        }
    }
    return false;
}

bool crops_till(int32_t x, int32_t y, int32_t z) {
    uint8_t const b = world_block(x, y, z);
    // Grass and dirt, and farmland again -- re-tilling an empty plot is
    // how a player refreshes its wet/dry state, which is the user's own
    // instruction and the only way the colour can be brought up to date
    // (D-106).
    bool const soil = b == BLK_GRASS || b == BLK_DIRT || b == BLK_FARMLAND || b == BLK_FARMLAND_WET;
    if (!soil) return false;

    // Nothing may be standing on it. Re-tilling under a growing crop
    // would be a way to destroy the crop by accident, and tilling under
    // a placed block would bury the farmland where nothing can reach it.
    if (y + 1 < CH_H) {
        uint8_t const above = world_block(x, y + 1, z);
        if (above != BLK_AIR && !block_replaceable(above)) return false;
        if (block_crop(above)) return false;
    }

    uint8_t const want = crops_water_near(x, y, z) ? BLK_FARMLAND_WET : BLK_FARMLAND;
    world_set(x, y, z, want, ST_PLACED);
    return true;
}

plant_result_t crops_plant(int32_t x, int32_t y, int32_t z, uint16_t seed) {
    uint8_t const crop = crops_block_for_seed(seed);
    if (crop == BLK_AIR) return PLANT_NOT_SEED;

    uint8_t const at = world_block(x, y, z);

    // RICE GOES IN THE WATER ITSELF, which is why it is the one crop
    // whose cell is the cell that was pointed at. One block deep, on
    // sand, and a source rather than a film -- the shallows, not a
    // river (D-107).
    if (block_waterlogged(crop)) {
        if (!block_liquid(at)) return PLANT_NEEDS_WATER;
        if (world_block(x, y - 1, z) != BLK_SAND) return PLANT_NEEDS_WATER;
        if (y + 1 >= CH_H || block_liquid(world_block(x, y + 1, z))) return PLANT_NEEDS_WATER;
        if (!fluid_is_source(world_state(x, y, z))) return PLANT_NEEDS_WATER;
        world_set(x, y, z, crop, ST_PLACED);
        return PLANT_OK;
    }

    // Everything else stands ON tilled soil, and dry soil refuses it.
    if (at != BLK_FARMLAND && at != BLK_FARMLAND_WET) return PLANT_NEEDS_SOIL;
    if (at == BLK_FARMLAND) return PLANT_TOO_DRY;
    if (y + 1 >= CH_H) return PLANT_BLOCKED;
    uint8_t const above = world_block(x, y + 1, z);
    if (above != BLK_AIR && !block_replaceable(above)) return PLANT_BLOCKED;
    if (block_liquid(above)) return PLANT_BLOCKED;  // a flooded plot is not a field

    world_set(x, y + 1, z, crop, ST_PLACED);
    return PLANT_OK;
}

// --- Saving the clock -------------------------------------------------

size_t crops_encode_chunk(chunk_t const* c, uint8_t* out, size_t cap) {
    if (c == NULL || out == NULL || cap < 5 + 4) return 0;
    // The section frame chunk_codec.h defines: u8 id, u32 length.
    out[0] = SECTION_CHUNK_CLOCK;
    out[1] = 4;
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    out[5] = (uint8_t)(c->stamp & 0xFFu);
    out[6] = (uint8_t)((c->stamp >> 8) & 0xFFu);
    out[7] = (uint8_t)((c->stamp >> 16) & 0xFFu);
    out[8] = (uint8_t)((c->stamp >> 24) & 0xFFu);
    return 9;
}

void crops_decode_section(chunk_t* c, uint8_t const* data, size_t len) {
    if (c == NULL || data == NULL || len < 4) return;
    c->stamp = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] << 24);
}

int crops_count_in(chunk_t const* c, int* ripe_out) {
    if (c == NULL || c->id == NULL) {
        if (ripe_out != NULL) *ripe_out = 0;
        return 0;
    }
    // Counting only: walk_chunk with no steps changes nothing, and the
    // cast is safe because that path never writes.
    return walk_chunk((chunk_t*)c, 0, ripe_out);
}
