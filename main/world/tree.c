// =====================================================================
//  SynthMiner  --  a sapling, and the tree it becomes (see tree.h)
// =====================================================================

#include "world/tree.h"

#include "common/rng.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/worldgen.h"

// THE TWO SPECIES, and a third would be a row. Which log it grows, which
// leaf, and which sapling comes off that log when the tree is felled.
static struct {
    uint8_t sapling;
    uint8_t log;
    uint8_t leaf;
} const SPECIES[] = {
    {BLK_SAPLING_OAK, BLK_LOG, BLK_LEAVES},
    {BLK_SAPLING_BIRCH, BLK_BIRCH_LOG, BLK_BIRCH_LEAVES},
};

#define SPECIES_N ((int)(sizeof SPECIES / sizeof SPECIES[0]))

uint8_t tree_sapling_for(uint8_t log_block) {
    for (int i = 0; i < SPECIES_N; i++) {
        if (SPECIES[i].log == log_block) return SPECIES[i].sapling;
    }
    return BLK_AIR;
}

bool tree_is_sapling(uint8_t block) {
    for (int i = 0; i < SPECIES_N; i++) {
        if (SPECIES[i].sapling == block) return true;
    }
    return false;
}

int tree_drops_at(int32_t x, int32_t y, int32_t z) {
    uint32_t const h = sm_hash3(x, y, z, 0x5EED1Fu);
    return TREE_DROP_MIN + (int)(h % (uint32_t)(TREE_DROP_MAX - TREE_DROP_MIN + 1));
}

// One cell of the tree, written into the LIVE world -- so the light
// flood, the mesher and the chunk's dirty flag all hear about it, which
// is the whole difference between this and the generator's stamp().
static void grow_cell(void* ctx, int32_t wx, int y, int32_t wz, uint8_t block, bool overwrite) {
    (void)ctx;
    if (y < 0 || y >= CH_H) return;
    // A CHUNK THAT IS NOT RESIDENT IS NOT WRITTEN TO. world_block reads
    // an absent chunk as the barrier (D-14), so this would otherwise
    // silently drop half a canopy at the edge of the loaded ring -- and
    // the half it dropped would never come back, because the sapling is
    // gone by then.
    if (chunk_find(chunk_of(wx), chunk_of(wz)) == NULL) return;

    uint8_t const was = world_block(wx, y, wz);
    if (!overwrite && was != BLK_AIR && !block_replaceable(was)) return;
    // GROWN, NOT PLACED: ST_PLACED stays clear, which is what makes the
    // new tree one the felling rule will take whole (Part F). A tree
    // somebody planted should come down like any other.
    world_set(wx, y, wz, block, 0);
}

bool tree_grow(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint8_t const sap = world_block(x, y, z);
    int           n   = -1;
    for (int i = 0; i < SPECIES_N; i++) {
        if (SPECIES[i].sapling == sap) n = i;
    }
    if (n < 0) return false;

    int const h = worldgen_tree_height(x, z, seed);
    // ROOM TO GROW, checked before a single cell is written. Half a tree
    // with a sapling gone from under it is worse than a sapling that
    // waits: this returns false and the caller leaves it standing, so it
    // tries again on the next tick of the chunk's clock.
    if (y + h + 2 >= CH_H) return false;
    for (int up = 1; up < h + 2; up++) {
        uint8_t const above = world_block(x, y + up, z);
        if (above != BLK_AIR && !block_replaceable(above)) return false;
    }

    // The sapling goes first, so the trunk is written into air.
    world_set(x, y, z, BLK_AIR, 0);
    worldgen_tree_shape(x, y, z, h, SPECIES[n].log, SPECIES[n].leaf, seed, grow_cell, NULL);
    return true;
}
