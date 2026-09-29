// =====================================================================
//  SynthMiner  --  farming (see crops.h)
// =====================================================================

#include "world/crops.h"

#include "items/items.h"
#include "world/chunk_codec.h"
#include "world/chunk_worker.h"
#include "world/fluid.h"
#include "world/tree.h"

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

// A RIPE SAPLING IS A TREE THAT HAS NOT HAPPENED YET. Every path into
// growth ends here: the sweep, the catch-up on load, and the compost a
// player throws at it.
//
// IT MAY FAIL, and failing has to be harmless. A sapling under an
// overhang has nowhere to put a canopy, so tree_grow refuses and the
// sapling is left standing -- which is why the sweep asks again every
// time it passes (walk_chunk) rather than only when a stage is owed.
// Without that, one planted in the wrong place would sit at its last
// stage for ever with nothing ever looking at it again.
static void try_tree(int32_t x, int32_t y, int32_t z) {
    uint8_t const b = world_block(x, y, z);
    if (!tree_is_sapling(b)) return;
    if (!crop_is_ripe(b, world_state(x, y, z))) return;
    tree_grow(x, y, z, chunk_worker_seed());
}

int crops_advance(int32_t x, int32_t y, int32_t z, int steps) {
    if (steps <= 0) return 0;
    uint8_t const b = world_block(x, y, z);
    if (!block_crop(b)) return 0;

    uint8_t const max = block_def(b)->growth_max;
    uint8_t const st  = world_state(x, y, z);
    uint8_t const was = crop_stage(st);
    if (was >= max) {
        // Already at its last stage. For a wheat plant that is the end
        // of the story; for a sapling it is the moment it has been
        // waiting for, and it may have been refused before.
        try_tree(x, y, z);
        return 0;
    }

    int want = (int)was + steps;
    if (want > (int)max) want = (int)max;
    world_set(x, y, z, b, crop_state_with(st, (uint8_t)want));  // keeps the phase bits

    // A TWO-BLOCK PLANT GROWS AS ONE. Both halves are crops and the
    // sweep would reach them separately, but compost reaches only the
    // cell that was clicked -- so the other half is brought along here,
    // where every path into growth passes.
    uint8_t const other = block_tall_other(b);
    if (other != BLK_AIR) {
        int32_t const oy = block_tall_top(b) ? y - 1 : y + 1;
        if (oy >= 0 && oy < CH_H && world_block(x, oy, z) == other) {
            uint8_t const ost = world_state(x, oy, z);
            if (crop_stage(ost) != (uint8_t)want) world_set(x, oy, z, other, crop_state_with(ost, (uint8_t)want));
        }
    }
    if (want >= (int)max) try_tree(x, y, z);
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
static int walk_chunk(chunk_t* c, uint32_t prev, uint32_t now, int* ripe_out) {
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
                if (now > prev) {
                    uint8_t const st = c->st[CH_IDX(lx, y, lz)];
                    crops_advance(wx, y, wz, crop_steps_between(col[y], st, prev, now));
                }
                // A SAPLING THAT IS READY BECOMES A TREE, asked every
                // pass and not only when a stage is owed -- see the
                // note on try_tree(). The cell stops being a crop as it
                // goes, and the loop simply does not match it again.
                if (crop_is_ripe(col[y], c->st[CH_IDX(lx, y, lz)])) {
                    if (tree_is_sapling(col[y])) {
                        try_tree(wx, y, wz);
                        if (!block_crop(col[y])) continue;  // it grew; the cell is a trunk now
                    }
                    ripe++;
                }
            }
        }
    }
    if (ripe_out != NULL) *ripe_out = ripe;
    return found;
}

// Floor division that is right for negative numerators, which C's / is
// not: -1 / 8000 is 0 and this has to be -1, or a plant whose phase puts
// its origin after the epoch gains a stage it has not earned.
static int64_t floor_div(int64_t a, int64_t b) {
    int64_t const q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// HOW MANY STAGES A PLANT OWES between two moments. Growth happens on
// absolute boundaries -- one stage every grow_ticks since the world's
// clock began, offset by the plant's own phase -- so the answer depends
// only on the two times and not on how often anybody looked. That is
// what lets the chunk keep ONE clock for crops that grow at different
// speeds (blocks.h, grow_ticks), and it is why this needs no remainder
// and no per-cell timer.
int crop_steps_between(uint8_t block, uint8_t state, uint32_t prev, uint32_t now) {
    uint32_t const iv = block_def(block)->grow_ticks;
    if (iv == 0u || now <= prev) return 0;

    int64_t const off = (int64_t)crop_phase(state) * (int64_t)(iv / CROP_PHASES);
    int64_t const ka  = floor_div((int64_t)prev - off, (int64_t)iv);
    int64_t const kb  = floor_div((int64_t)now - off, (int64_t)iv);
    int64_t const d   = kb - ka;
    if (d <= 0) return 0;
    // A chunk away for a month owes more stages than any crop has, and
    // the arithmetic below is per cell: cap it.
    return d > 8 ? 8 : (int)d;
}

void crops_tick(uint32_t now) {
    s_now = now;
    // ONE SLOT A TICK, and almost always nothing at all: an unused slot
    // and a chunk with no crops in it are both a couple of compares.
    chunk_t* c = chunk_slot_at(s_sweep);
    s_sweep    = (s_sweep + 1) % SLOT_COUNT;
    if (c == NULL || c->cstate != CS_READY || c->id == NULL) return;

    uint32_t const prev = c->stamp;
    // A clock of zero means "never set" -- see crops_chunk_join.
    c->stamp = now;
    if (prev == 0u || now <= prev) return;
    if ((c->flags & CF_CROPS) == 0) return;  // the clock still moves; nothing grows

    if (walk_chunk(c, prev, now, NULL) == 0) c->flags &= (uint8_t)~CF_CROPS;
}

void crops_chunk_join(chunk_t* c, uint32_t now) {
    if (c == NULL || c->id == NULL) return;

    // A chunk with no clock of its own has never been saved by a build
    // that had one -- a fresh generation, or a world written before step
    // 9. Either way "now" is the only honest answer: pretending it was
    // stamped at tick 0 would ripen every field in an old world the
    // moment it was walked into.
    uint32_t const prev = c->stamp == 0u ? now : c->stamp;
    c->stamp            = now;

    int const found = walk_chunk(c, prev, now, NULL);
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

// THE STATE A SEED GOES IN WITH: stage 0, and the plant's own phase --
// where in its growth cycle this particular plant sits, so that it
// ripens grow_ticks x growth_max after IT was sown rather than on
// whatever boundary the chunk happens to cross next (crops.h).
static uint8_t planted_state(uint8_t crop) {
    uint32_t const iv = block_def(crop)->grow_ticks;
    if (iv == 0u) return ST_PLACED;

    // Where in the cycle this moment falls, in sixteenths. The plant's
    // boundaries run from here, so it ripens grow_ticks x growth_max
    // after IT was sown, to within a sixteenth of a stage -- 25 seconds
    // for wheat.
    uint8_t const phase = (uint8_t)(((uint64_t)(s_now % iv) * CROP_PHASES) / iv);
    return crop_state_with_phase(ST_PLACED, phase);
}

// BRING THE CHUNK'S CLOCK UP TO DATE BEFORE SOWING INTO IT.
//
// Growth counts the stage boundaries between the chunk's last sweep and
// its next one, and that window reaches into the past -- 256 ticks in
// ordinary play, and as far as you like after the debug key that jumps
// the clock or a chunk that has just been sitting. A seed dropped into
// that window is credited with time that passed before it existed: a
// host check measured one ripening in 15360 ticks instead of 24000,
// which is a whole free stage.
//
// So the chunk is swept to NOW first. Everything already growing there
// gets exactly the time it earned, the clock is left at this moment, and
// the new seed starts from a window of zero.
static void catch_up_for_planting(int32_t x, int32_t z) {
    chunk_t* c = chunk_find(chunk_of(x), chunk_of(z));
    if (c == NULL || c->cstate != CS_READY || c->id == NULL) return;
    uint32_t const prev = c->stamp;
    c->stamp            = s_now;
    if (prev == 0u || s_now <= prev) return;
    if ((c->flags & CF_CROPS) != 0) walk_chunk(c, prev, s_now, NULL);
}

plant_result_t crops_plant(int32_t x, int32_t y, int32_t z, uint16_t seed) {
    uint8_t const crop = crops_block_for_seed(seed);
    if (crop == BLK_AIR) return PLANT_NOT_SEED;
    catch_up_for_planting(x, z);

    uint8_t const at = world_block(x, y, z);

    // RICE GOES IN THE WATER ITSELF, which is why it is the one crop
    // whose cell is the cell that was pointed at. One block deep, on
    // sand, and a source rather than a film -- the shallows, not a
    // river (D-107).
    if (block_waterlogged(crop)) {
        if (!block_liquid(at)) return PLANT_NEEDS_WATER;
        if (world_block(x, y - 1, z) != BLK_SAND) return PLANT_NEEDS_WATER;
        if (!fluid_is_source(world_state(x, y, z))) return PLANT_NEEDS_WATER;
        // ONE BLOCK DEEP, AND ROOM TO STAND UP IN. Rice is two cells
        // tall (the user), so the air above the water is not merely a
        // depth test any more -- it is where the top half goes.
        uint8_t const above_id = block_tall_other(crop);
        if (y + 1 >= CH_H) return PLANT_NEEDS_WATER;
        uint8_t const above = world_block(x, y + 1, z);
        if (block_liquid(above)) return PLANT_NEEDS_WATER;  // deeper than one block
        if (above_id != BLK_AIR && above != BLK_AIR && !block_replaceable(above)) return PLANT_BLOCKED;
        // ONE PHASE FOR BOTH HALVES, or the two would cross their
        // boundaries at different moments and the plant would spend half
        // its life with its top and bottom a stage apart.
        uint8_t const st = planted_state(crop);
        world_set(x, y, z, crop, st);
        if (above_id != BLK_AIR) world_set(x, y + 1, z, above_id, st);
        return PLANT_OK;
    }

    // A SAPLING GOES IN THE GROUND, not in a field. Grass or dirt, and
    // no hoe -- a tree that needed tilling and a moat would be a tree
    // nobody planted, and a forest is not a crop you tend.
    //
    // It is the one thing here that plants on something a hoe has NOT
    // been at, which is why it is a branch and not a column: every
    // other seed in the game wants wet farmland, and a "what does this
    // plant on" column with one exception in it would be a column with
    // one exception in it.
    if (tree_is_sapling(crop)) {
        if (at != BLK_GRASS && at != BLK_DIRT) return PLANT_NEEDS_GROUND;
        if (y + 1 >= CH_H) return PLANT_BLOCKED;
        uint8_t const above = world_block(x, y + 1, z);
        if (above != BLK_AIR && !block_replaceable(above)) return PLANT_BLOCKED;
        if (block_liquid(above)) return PLANT_BLOCKED;
        world_set(x, y + 1, z, crop, planted_state(crop));
        return PLANT_OK;
    }

    // Everything else stands ON tilled soil, and dry soil refuses it.
    if (at != BLK_FARMLAND && at != BLK_FARMLAND_WET) return PLANT_NEEDS_SOIL;
    if (at == BLK_FARMLAND) return PLANT_TOO_DRY;
    if (y + 1 >= CH_H) return PLANT_BLOCKED;
    uint8_t const above = world_block(x, y + 1, z);
    if (above != BLK_AIR && !block_replaceable(above)) return PLANT_BLOCKED;
    if (block_liquid(above)) return PLANT_BLOCKED;  // a flooded plot is not a field

    world_set(x, y + 1, z, crop, planted_state(crop));
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
    // Counting only: prev == now means no time has passed, so nothing is
    // written, and the cast is safe because that path never writes.
    return walk_chunk((chunk_t*)c, 0u, 0u, ripe_out);
}
