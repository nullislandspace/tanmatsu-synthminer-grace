// =====================================================================
//  SynthMiner  --  host-side check of the pure game modules
// ---------------------------------------------------------------------
//  Built and run by `make worldcheck` with the host compiler. No badge,
//  no engine, no RTOS: everything here compiles with SM_HOST and plain
//  malloc (main/common/psram.h). Exit status 0 = all checks passed.
//
//  Sections arrive with the milestones they guard
//  (claudeplans/synthminer.md, Part H):
//    registries   now      the block table's invariants
//    worldgen     step 1   determinism, cross-chunk equivalence
//    far lands    step 7   Java's maths, the wall, the tunnels, the cliff, the asymmetry
//    codec        step 1   RLE and region round trips, torn-write recovery
//    physics      step 3   swept AABB, no tunnelling, step-up
//    raycast      step 3   DDA against a brute-force march
//    felling      step 3   the tree rule
//    items        step 4   stacking, recipes
//    music        step 14  every shipped MIDI file parses, and ends
// =====================================================================

#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stdlib.h>

#include "common/rng.h"
#include "common/tags.h"
#include "world/blocks.h"
#include "world/chunk.h"
#include "world/chunk_codec.h"
#include "world/region.h"
#include "world/vfs_compat.h"
#include "world/worldgen.h"
#include "world/farlands.h"
#include "world/datadir.h"
#include <sys/stat.h>
#include <time.h>
#include "world/chunk_worker.h"
#include "common/trace.h"
#include "world/chunkmesh.h"
#include "world/blockupdate.h"
#include "world/crops.h"
#include "game/composter.h"
#include "game/fishing.h"
#include "game/food.h"
#include "world/tree.h"
#include "game/maker.h"
#include "game/stove.h"
#include "game/mob.h"
#include "game/daytime.h"
#include "world/fluid.h"
#include "world/light.h"
#include "world/worldstore.h"
#include "se_nbt.h"
#include "game/physics.h"
#include "game/raycast.h"
#include "game/interact.h"
#include "game/benchpath.h"
#include "game/player.h"
#include "game/replay.h"
#include "items/inventory.h"
#include "items/items.h"
#include "items/item_entity.h"
#include "items/recipes.h"
#include "game/furnace.h"
#include "world/blockent.h"
#include "i18n/fold.h"
#include "i18n/i18n.h"
#include "audio/midi_seq.h"
// The engine's own glyph tables, so "can the font draw this?" is
// answered by the code that will have to draw it (engine-internal on
// purpose: a check may look where a game may not).
#include "hershey_text.h"

static int s_fail = 0;

#define CHECK(cond, ...)                    \
    do {                                    \
        if (!(cond)) {                      \
            printf("  FAIL: " __VA_ARGS__); \
            printf("\n");                   \
            s_fail++;                       \
        }                                   \
    } while (0)

// ---------------------------------------------------------------------
//  Registries
//
//  Cheap, but they catch the failure mode a table-driven design invites:
//  a row added without its materials, or an id that outgrows the byte
//  the chunk planes store it in.
// ---------------------------------------------------------------------

static void check_blocks(void) {
    printf("blocks: %d entries\n", BLK_COUNT);

    CHECK(BLK_COUNT <= 255, "BLK_COUNT is %d: a block id must fit the chunk's uint8 plane", BLK_COUNT);

    for (int i = 0; i < BLK_COUNT; i++) {
        block_def_t const* d = &BLOCKS[i];
        CHECK(d->name != NULL && d->name[0] != '\0', "block %d has no name", i);
        CHECK(d->kind <= K_LIQUID, "block %s: kind %u out of range", d->name ? d->name : "?", d->kind);

        // Every block that meshes needs three real materials, or the
        // mesher indexes a texture that was never loaded.
        if (d->kind != K_AIR) {
            for (int f = 0; f < 3; f++) {
                CHECK(d->mat[f] < VM_COUNT, "block %s: face %d material %u past VM_COUNT", d->name, f, d->mat[f]);
            }
        }

        CHECK(d->drop_max >= d->drop_min, "block %s: drop_max %u below drop_min %u", d->name, d->drop_max,
              d->drop_min);
        CHECK(d->growth_max <= 7, "block %s: growth_max %u does not fit state bits 1..3", d->name, d->growth_max);
        CHECK(d->light <= 15, "block %s: light %u above 15", d->name, d->light);

        // An unbreakable block that drops something is a contradiction
        // the interaction code would have to special-case.
        if (d->hardness == HARDNESS_UNBREAKABLE) {
            CHECK(d->drop_item == ITEM_NONE, "block %s is unbreakable but drops item %u", d->name, d->drop_item);
        }
    }

    // Names are the stable id a future save format would key on, so
    // they have to be unique.
    for (int i = 0; i < BLK_COUNT; i++) {
        for (int j = i + 1; j < BLK_COUNT; j++) {
            CHECK(strcmp(BLOCKS[i].name, BLOCKS[j].name) != 0, "blocks %d and %d share the name \"%s\"", i, j,
                  BLOCKS[i].name);
        }
    }

    // The specific invariants the rest of the code relies on.
    CHECK(BLOCKS[BLK_AIR].kind == K_AIR, "air must mesh as K_AIR");
    CHECK(block_replaceable(BLK_AIR), "air must be replaceable, or nothing can be placed");
    CHECK(!block_solid(BLK_AIR), "air must not be solid");
    CHECK(block_solid(BLK_BARRIER), "the barrier must be solid (D-14): the player stands on the world's edge");
    CHECK(BLOCKS[BLK_BARRIER].hardness == HARDNESS_UNBREAKABLE, "the barrier must be unbreakable");
    CHECK(block_fellable(BLK_LOG) && block_fellable(BLK_LEAVES), "logs and leaves carry the felling rule (Part F)");
    // Which of them STARTS a fell is a different question from which the
    // fell spreads through, and they were one flag until 2026-09-28.
    CHECK(block_trunk(BLK_LOG) && block_trunk(BLK_BIRCH_LOG), "logs are trunks (Part F)");
    CHECK(!block_trunk(BLK_LEAVES) && !block_trunk(BLK_BIRCH_LEAVES), "leaves are NOT trunks: cutting one must not fell the tree");
    CHECK((BLOCKS[BLK_LEAVES].flags & BF_SEE_SELF) != 0, "leaves show their faces against other leaves");
    CHECK((BLOCKS[BLK_GLASS].flags & BF_SEE_SELF) == 0, "glass hides glass");

    // block_def() must be total: a corrupt save can hand it anything.
    CHECK(block_def(255) == &BLOCKS[BLK_BARRIER],
          "block_def(255) must fall back to the barrier, not read past the table");
    CHECK(block_def(BLK_COUNT) == &BLOCKS[BLK_BARRIER], "block_def(BLK_COUNT) must fall back to the barrier");
}

// ---------------------------------------------------------------------
//  Chunks
//
//  The coordinate maths is the classic place a voxel game goes wrong:
//  x = -1 belongs to chunk -1 at offset 15, not to chunk 0 at offset
//  -1. A plain division rounds towards zero and gets it wrong, so
//  chunk_of / chunk_off shift and mask -- and that is worth pinning
//  down before anything is built on top of it.
// ---------------------------------------------------------------------

// THE ID REGISTRY (D-74). Block ids and names, and item names, are what
// saves on people's cards are made of. tools/ids.txt is the list of every
// one ever shipped, and this is the check that the code still agrees with
// it -- both ways, so a new block cannot slip in unlisted either.
static void check_ids(void) {
    printf("the id registry\n");
    FILE* f = fopen("tools/ids.txt", "r");
    CHECK(f != NULL, "tools/ids.txt is missing");
    if (f == NULL) return;
    bool block_listed[BLK_COUNT] = {false};
    bool item_listed[ITEM_COUNT] = {false};
    int  blocks = 0, items = 0;
    char line[160];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int  id = -1;
        char name[64];
        if (sscanf(line, "block %d %63s", &id, name) == 2) {
            blocks++;
            CHECK(id >= 0 && id < BLK_COUNT, "ids.txt lists block %d (%s), which the code does not have: a block "
                  "is never removed, only retired", id, name);
            if (id < 0 || id >= BLK_COUNT) continue;
            CHECK(strcmp(BLOCKS[id].name, name) == 0, "block %d is \"%s\" in the code but \"%s\" in ids.txt: ids "
                  "and names never change once shipped", id, BLOCKS[id].name, name);
            CHECK(!block_listed[id], "block %d is listed twice in ids.txt", id);
            block_listed[id] = true;
        } else if (sscanf(line, "item %63s", name) == 1) {
            items++;
            uint16_t const it = item_by_name(name);
            CHECK(it >= BLK_COUNT, "ids.txt lists item \"%s\", which the code does not have: an item is never "
                  "renamed or removed", name);
            if (it >= BLK_COUNT && it < ITEM_COUNT) item_listed[it] = true;
        } else {
            CHECK(false, "ids.txt: cannot read the line \"%s\"", line);
        }
    }
    fclose(f);
    for (int b = 0; b < BLK_COUNT; b++)
        CHECK(block_listed[b], "block %d (%s) is not in tools/ids.txt: append \"block %d %s\" to it", b,
              BLOCKS[b].name, b, BLOCKS[b].name);
    for (int i = BLK_COUNT; i < ITEM_COUNT; i++)
        CHECK(item_listed[i], "item \"%s\" is not in tools/ids.txt: append \"item %s\" to it", item_def(i).name,
              item_def(i).name);
    printf("  %d blocks and %d items, all as shipped\n", blocks, items);
}

static void check_chunk_coords(void) {
    printf("chunk coords\n");

    struct {
        int32_t w;
        int32_t cx;
        int     off;
    } const CASES[] = {
        {0, 0, 0},      {1, 0, 1},       {15, 0, 15},      {16, 1, 0},      {31, 1, 15},
        {-1, -1, 15},   {-16, -1, 0},    {-17, -2, 15},    {-32, -2, 0},
        // The Far Lands are at -100000: the maths has to hold there too.
        {-100000, -6250, 0}, {-99999, -6250, 1}, {-100001, -6251, 15},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        CHECK(chunk_of(CASES[i].w) == CASES[i].cx, "chunk_of(%d) = %d, expected %d", CASES[i].w,
              chunk_of(CASES[i].w), CASES[i].cx);
        CHECK(chunk_off(CASES[i].w) == CASES[i].off, "chunk_off(%d) = %d, expected %d", CASES[i].w,
              chunk_off(CASES[i].w), CASES[i].off);
    }

    // The two must compose back into the original coordinate, over a
    // range that crosses zero in both directions.
    for (int32_t w = -5000; w <= 5000; w++) {
        int32_t const back = chunk_of(w) * CH_W + chunk_off(w);
        CHECK(back == w, "chunk_of/chunk_off do not compose at %d (got %d)", w, back);
        if (s_fail) break;
    }

    // CH_IDX must be a bijection onto [0, CH_CELLS).
    CHECK(CH_IDX(0, 0, 0) == 0, "CH_IDX(0,0,0) is not 0");
    CHECK(CH_IDX(CH_W - 1, CH_H - 1, CH_D - 1) == (size_t)CH_CELLS - 1, "CH_IDX does not end at CH_CELLS-1");
    // A column is contiguous -- this is what lets the mesher read it
    // and what CH_IDX exists to guarantee.
    CHECK(CH_IDX(3, 1, 5) == CH_IDX(3, 0, 5) + 1, "a column is not contiguous in y");
}

static void check_chunk_state_bits(void) {
    printf("chunk state byte\n");

    // The block's own 7-bit field must round-trip across its whole
    // range without touching ST_PLACED, which is the one bit with a
    // meaning that does not belong to the block type.
    for (uint32_t d = 0; d <= ST_DATA_MAX; d++) {
        uint8_t const st = st_with_data(ST_PLACED, (uint8_t)d);
        CHECK(st_data(st) == d, "block data %u did not round-trip (got %u)", d, st_data(st));
        CHECK((st & ST_PLACED) != 0, "block data %u clobbered ST_PLACED", d);

        uint8_t const st0 = st_with_data(0, (uint8_t)d);
        CHECK((st0 & ST_PLACED) == 0, "block data %u invented an ST_PLACED bit", d);
    }

    // Growth is that same field under a name the crop code can read.
    CHECK(st_growth(st_with_growth(0, 7)) == 7, "growth did not round-trip");
    CHECK(ST_DATA_MAX >= 15, "the block data field is too small for a redstone power level");

    // The two fields must not overlap, and must cover the byte.
    CHECK((ST_PLACED & ST_DATA_MASK) == 0, "ST_PLACED overlaps the block data field");
    CHECK((ST_PLACED | ST_DATA_MASK) == 0xFFu, "the state byte has bits belonging to nobody");
}

static void check_chunk_store(void) {
    printf("chunk store: %d slots, %zu bytes\n", CH_SLOT_COUNT, chunk_store_bytes());

    // Residency: claim a chunk, fill it, read it back through world_*.
    chunk_t* c = chunk_claim(-7, 3);
    CHECK(c != NULL, "could not claim a free slot");
    if (c == NULL) return;
    c->cstate = CS_READY;

    int32_t const wx = -7 * CH_W + 2, wz = 3 * CH_W + 9;
    world_set(wx, 10, wz, BLK_STONE, 0);
    CHECK(world_block(wx, 10, wz) == BLK_STONE, "a block written did not read back");
    CHECK(world_block(wx, 11, wz) == BLK_AIR, "a neighbouring cell was disturbed");
    CHECK((c->flags & CF_EDITED) != 0, "a write did not mark the chunk edited");

    // The state byte rides along.
    world_set(wx, 12, wz, BLK_LOG, ST_PLACED);
    CHECK(world_state(wx, 12, wz) == ST_PLACED, "the state byte did not read back");

    // The column summary tracks writes both ways.
    CHECK(world_ground(wx, wz) == 13, "world_ground = %d, expected 13", world_ground(wx, wz));
    world_set(wx, 12, wz, BLK_AIR, 0);
    CHECK(world_ground(wx, wz) == 11, "world_ground after a break = %d, expected 11", world_ground(wx, wz));

    // Anything not resident is the barrier, so callers never need a
    // "might be missing" branch (D-14).
    CHECK(world_block(900000, 10, 900000) == BLK_BARRIER, "a non-resident chunk did not read as the barrier");
    CHECK(world_solid_at(900000, 10, 900000), "the barrier must be solid");
    // Above the world is air, below it is not passable.
    CHECK(world_block(wx, CH_H, wz) == BLK_AIR, "above the world must be air");
    CHECK(world_block(wx, -1, wz) == BLK_BARRIER, "below bedrock must not be passable");

    // A slot holding a different chunk reads as absent, not as the
    // wrong terrain: this is what makes the ring safe (D-13).
    CHECK(chunk_find(-7 + CH_RING, 3) == NULL, "an aliasing chunk coordinate returned the wrong chunk");
    CHECK(chunk_slot(-7, 3) == chunk_slot(-7 + CH_RING, 3), "the test's premise is wrong: those should alias");

    // An edited chunk must not be silently evicted -- its edits are not
    // on disk yet.
    CHECK(chunk_claim(-7 + CH_RING, 3) == NULL, "an unsaved edited chunk was evicted");
    c->flags &= (uint8_t)~CF_EDITED;
    CHECK(chunk_claim(-7 + CH_RING, 3) != NULL, "a saved chunk could not be evicted");
}

// ---------------------------------------------------------------------
//  Hashing and noise
//
//  The donor folded a lattice point into one int key,
//  hash01(ix*7919 + iz*104729). That aliases -- (ix, iz) and
//  (ix + 104729, iz - 7919) hash the same -- and overflows int32 beyond
//  |iz| = 20505, which the block-resolution density field for caves and
//  the Far Lands would reach in ordinary play (F-10). These checks are
//  what stop it coming back.
// ---------------------------------------------------------------------

static int cmp_u32(void const* a, void const* b) {
    uint32_t const x = *(uint32_t const*)a, y = *(uint32_t const*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void check_rng(void) {
    printf("hashing and noise\n");

    // Determinism: the same point always gives the same value.
    CHECK(sm_hash2(3, -9, 42) == sm_hash2(3, -9, 42), "sm_hash2 is not deterministic");
    CHECK(sm_hash3(3, 4, -9, 42) == sm_hash3(3, 4, -9, 42), "sm_hash3 is not deterministic");

    // A different seed gives a different world.
    CHECK(sm_hash2(3, -9, 42) != sm_hash2(3, -9, 43), "the seed does not change sm_hash2");

    // Injectivity where it matters: hash a 200 x 200 lattice block
    // AROUND THE FAR LANDS and count collisions. The old formulation
    // fails this twice over -- it aliases, and the multiply overflows.
    enum { N = 200 };
    static uint32_t h[N * N];
    int             n = 0;
    for (int32_t dz = 0; dz < N; dz++) {
        for (int32_t dx = 0; dx < N; dx++) h[n++] = sm_hash2(-100000 + dx, -50 + dz, 12345u);
    }
    qsort(h, (size_t)n, sizeof(h[0]), cmp_u32);
    int dup = 0;
    for (int i = 1; i < n; i++) dup += (h[i] == h[i - 1]);
    // 40000 draws from 2^32 collide about 0.19 times by chance; more
    // than a handful means the map is not injective.
    printf("  %d lattice points near x=-100000, %d hash collisions\n", n, dup);
    CHECK(dup <= 3, "%d hash collisions in %d points: the lattice hash aliases (F-10)", dup, n);

    // The donor's exact aliasing pair: 104729 in x and -7919 in z left
    // its key unchanged, so those two points grew identical terrain.
    CHECK(sm_hash2(0, 0, 7u) != sm_hash2(104729, -7919, 7u), "sm_hash2 aliases the donor's way");
    CHECK(sm_hash3(0, 5, 0, 7u) != sm_hash3(104729, 5, -7919, 7u), "sm_hash3 aliases the donor's way");

    // The donor's overflow range: |iz| = 20505 is where iz * 104729
    // left int32. A block-resolution density field reaches that in
    // ordinary play, so hash and noise must stay healthy well past it.
    for (int32_t z = 20000; z <= 21000; z += 250) {
        CHECK(sm_hash3(7, 30, z, 3u) != sm_hash3(7, 30, z + 1, 3u), "sm_hash3 degenerates around z = %d", z);
    }
    {
        float lo3 = 1.0f, hi3 = 0.0f;
        for (int i = 0; i < 4000; i++) {
            // scale 1: the lattice index IS the block coordinate, which
            // is the case the donor could not survive.
            float const v = sm_noise3(9.5f, 30.25f, 20000.0f + (float)i * 0.5f, 1.0f, 3u);
            if (v < lo3) lo3 = v;
            if (v > hi3) hi3 = v;
        }
        printf("  noise3 at block resolution near z=20000: range %.3f..%.3f\n", lo3, hi3);
        CHECK(hi3 - lo3 > 0.5f, "sm_noise3 went degenerate past the donor's overflow point (%g..%g)", lo3, hi3);
    }

    // Noise stays in range, including far from the origin, and is
    // continuous (neighbouring samples cannot jump by much).
    float lo = 1.0f, hi = 0.0f, maxstep = 0.0f, prev = 0.0f;
    for (int i = 0; i < 20000; i++) {
        float const x = -100000.0f + (float)i * 0.25f;
        float const v = sm_noise2(x, 17.0f, 24.0f, 99u);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if (i > 0) {
            float const d = fabsf(v - prev);
            if (d > maxstep) maxstep = d;
        }
        prev = v;
    }
    printf("  noise2 near x=-100000: range %.3f..%.3f, largest step over 0.25 blocks %.4f\n", lo, hi, maxstep);
    CHECK(lo >= 0.0f && hi < 1.0f, "sm_noise2 left [0,1): %g..%g", lo, hi);
    CHECK(hi - lo > 0.5f, "sm_noise2 barely varies near the Far Lands (%g..%g): it has gone degenerate", lo, hi);
    CHECK(maxstep < 0.08f, "sm_noise2 is not continuous near the Far Lands (step %g)", maxstep);

    // fbm stays in range too.
    lo = 1.0f;
    hi = 0.0f;
    for (int i = 0; i < 5000; i++) {
        float const v = sm_fbm2((float)i * 0.7f, (float)i * -0.3f, 64.0f, 4, 5u);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    CHECK(lo >= 0.0f && hi < 1.0f, "sm_fbm2 left [0,1): %g..%g", lo, hi);

    // The stream never sticks and never repeats too soon.
    sm_rng_t r;
    sm_rng_seed(&r, 0);
    uint32_t const first = sm_rng_u32(&r);
    int            same  = 0;
    for (int i = 0; i < 1000; i++) same += (sm_rng_u32(&r) == first);
    CHECK(same == 0, "sm_rng repeated its first value %d times in 1000 draws", same);
}

// ---------------------------------------------------------------------
//  World generation
//
//  Two properties carry the whole design:
//
//    determinism          a seed and a coordinate always give the same
//                         block, so a world can be regenerated instead
//                         of stored, for ever.
//    load-order independence
//                         a chunk generated alone equals the same chunk
//                         generated with its neighbours around it. This
//                         is the one that breaks silently: the moment a
//                         decoration reads a neighbour chunk, terrain
//                         starts depending on which way the player
//                         walked into it, and a replay stops matching.
// ---------------------------------------------------------------------

#define GEN_SEED 0xC0FFEEu

// Generate into a standalone chunk, outside the ring store, so the
// checks can hold several at once and compare them.
static void gen_into(chunk_t* c, uint8_t* id, uint8_t* st, int32_t cx, int32_t cz, uint32_t seed) {
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->st = st;
    c->cx = cx;
    c->cz = cz;
    worldgen_chunk(c, seed, FARLANDS_X_DEFAULT);
}

// ---------------------------------------------------------------------
//  The Far Lands (Part X, D-78)
//
//  Beta 1.7.3's generator, overflowed. What is checked: that the Java it
//  depends on behaves like Java; that the fast path makes the same blocks
//  as the unabridged port; that the Far Lands look like the Edge Far
//  Lands (a wall to the top, tunnels running west, flooded below the
//  sea); that the change at the edge is sudden; and that nothing east,
//  north or south of it changed -- the guard against a sign bug turning
//  the whole world into Far Lands.
// ---------------------------------------------------------------------

#define FL_SEED 0xC0FFEEu

static bool is_rock(uint8_t b) {
    return b == BLK_STONE || b == BLK_BEDROCK || b == BLK_GRAVEL || b == BLK_COAL_ORE;
}

static void check_farlands(void) {
    printf("far lands\n");

    // Java, where C would differ.
    CHECK(java_d2i(3.0e9) == INT32_MAX && java_d2i(-3.0e9) == INT32_MIN, "java_d2i does not saturate");
    CHECK(java_d2i(-1.7) == -1 && java_d2i(1.7) == 1 && java_d2i(2147483646.9) == 2147483646,
          "java_d2i does not truncate toward zero");
    CHECK(java_d2i(NAN) == 0, "java_d2i(NaN) is not 0");
    java_random_t jr;
    java_random_seed(&jr, 42);
    CHECK(java_random_next_int(&jr) == -1170105035, "java.util.Random(42).nextInt() is not -1170105035");
    java_random_seed(&jr, 0);
    double const d0 = java_random_next_double(&jr);
    CHECK(fabs(d0 - 0.730967787376657) < 1e-15, "java.util.Random(0).nextDouble() is %.17g, not 0.730967787376657", d0);

    // Where the edge is.
    CHECK(farlands_chunk_is(-129, -2048) && !farlands_chunk_is(-128, -2048), "the edge at -2048 is not between chunks -129 and -128");
    CHECK(!farlands_chunk_is(-100000, FARLANDS_NONE), "a world without Far Lands has some");
    CHECK(farlands_beta_cx(-129, -2048) == -784428 && farlands_beta_cx(-1, 0) == -784428,
          "the first Far Lands chunk is not Beta chunk -784428");
    CHECK(farlands_beta_y(0) == 0 && farlands_beta_y(CH_SEA_LEVEL) == 63 && farlands_beta_y(CH_SEA_LEVEL + 1) == 64 &&
              farlands_beta_y(CH_H - 1) == 127,
          "Beta's rows do not map onto ours end to end (%d %d %d %d)", farlands_beta_y(0), farlands_beta_y(CH_SEA_LEVEL),
          farlands_beta_y(CH_SEA_LEVEL + 1), farlands_beta_y(CH_H - 1));
    for (int y = 1; y < CH_H; y++) CHECK(farlands_beta_y(y) > farlands_beta_y(y - 1), "Beta row mapping not rising at %d", y);

    // The fast path against the unabridged port, block for block.
    static uint8_t fast[16 * 16 * 128], full[16 * 16 * 128];
    long           diff = 0, cells = 0;
    clock_t        t_fast = 0, t_full = 0;
    for (int32_t bx = -784431; bx <= -784428; bx++) {
        for (int32_t bz = -2; bz <= 1; bz++) {
            clock_t t = clock();
            CHECK(farlands_beta_column(bx, bz, FL_SEED, false, fast), "no Far Lands tables");
            t_fast += clock() - t;
            t = clock();
            farlands_beta_column(bx, bz, FL_SEED, true, full);
            t_full += clock() - t;
            for (size_t i = 0; i < sizeof(fast); i++) diff += fast[i] != full[i];
            cells += (long)sizeof(fast);
        }
    }
    printf("  fast path against the full port: %ld of %ld cells differ; %.1f ms against %.1f ms a chunk (host)\n", diff,
           cells, 1000.0 * (double)t_fast / CLOCKS_PER_SEC / 16.0, 1000.0 * (double)t_full / CLOCKS_PER_SEC / 16.0);
    CHECK(diff == 0, "the fast path makes %ld blocks the full port does not", diff);

    // What the Far Lands are made of, over a block of chunks at the edge.
    static uint8_t id[CH_CELLS], st[CH_CELLS];
    chunk_t        c;
    long           n_rock = 0, n_air = 0, n_water = 0, n_soil = 0, n_sand = 0, n_other = 0;
    int            cols = 0, tall = 0, floor_ok = 0;
    long           same_x = 0, pairs_x = 0, same_z = 0, pairs_z = 0;
    for (int32_t cx = -132; cx <= -129; cx++) {
        for (int32_t cz = -4; cz <= 3; cz++) {
            gen_into(&c, id, st, cx, cz, FL_SEED);
            for (int z = 0; z < CH_D; z++) {
                for (int x = 0; x < CH_W; x++) {
                    int top = -1;
                    for (int y = 0; y < CH_H; y++) {
                        uint8_t const b = id[CH_IDX(x, y, z)];
                        if (is_rock(b)) n_rock++;
                        else if (b == BLK_AIR) n_air++;
                        else if (b == BLK_WATER) n_water++;
                        else if (b == BLK_DIRT || b == BLK_GRASS) n_soil++;
                        else if (b == BLK_SAND) n_sand++;
                        else n_other++;
                        if (b != BLK_AIR && b != BLK_WATER) top = y;
                        // Tunnels: is a cell the same kind (open or not)
                        // as its neighbour along x, and along z?
                        bool const open = b == BLK_AIR || b == BLK_WATER;
                        if (x + 1 < CH_W) {
                            uint8_t const nb = id[CH_IDX(x + 1, y, z)];
                            same_x += open == (nb == BLK_AIR || nb == BLK_WATER);
                            pairs_x++;
                        }
                        if (z + 1 < CH_D) {
                            uint8_t const nb = id[CH_IDX(x, y, z + 1)];
                            same_z += open == (nb == BLK_AIR || nb == BLK_WATER);
                            pairs_z++;
                        }
                    }
                    cols++;
                    tall += top >= CH_H - 8;
                    floor_ok += id[CH_IDX(x, 0, z)] == BLK_BEDROCK;
                }
            }
        }
    }
    long const all = n_rock + n_air + n_water + n_soil + n_sand + n_other;
    printf("  composition: %.0f%% rock, %.0f%% air, %.0f%% water, %.0f%% dirt and grass, %.0f%% sand, %.0f%% other\n",
           100.0 * n_rock / all, 100.0 * n_air / all, 100.0 * n_water / all, 100.0 * n_soil / all, 100.0 * n_sand / all,
           100.0 * n_other / all);
    printf("  %d of %d columns reach within 8 of the top; bedrock under %d\n", tall, cols, floor_ok);
    printf("  neighbours alike: %.1f%% along x (west), %.1f%% along z\n", 100.0 * same_x / pairs_x, 100.0 * same_z / pairs_z);
    CHECK(floor_ok == cols, "%d of %d Far Lands columns have no bedrock floor", cols - floor_ok, cols);
    // Measured when this was written: 67% of columns reach within 8 of
    // the top (the top is full of holes, as Beta's was), 98.7% of
    // neighbours alike along x and 87% along z, and 42% rock, 30% air,
    // 19% water, 9% dirt and grass -- against the wiki's 36 / 25 / 23 /
    // 10 for Beta's own Edge Far Lands.
    CHECK(tall * 2 >= cols, "only %d of %d Far Lands columns reach the top: that is no wall", tall, cols);
    CHECK(same_x * 100 >= pairs_x * 97, "the Far Lands change along x (%.1f%% alike): the tunnels do not run west",
          100.0 * same_x / pairs_x);
    CHECK(same_z * 100 <= pairs_z * 95, "the Far Lands hardly change along z either (%.1f%% alike)", 100.0 * same_z / pairs_z);
    CHECK(n_water * 10 >= all, "the Far Lands are not flooded below the sea (%.0f%% water)", 100.0 * n_water / all);
    CHECK(n_air * 10 >= all, "the Far Lands have no tunnels (%.0f%% air)", 100.0 * n_air / all);

    // Sudden: the last ordinary chunk is ordinary, the first Far Lands
    // chunk is the wall. Chunk -127 is untouched by the edge in every
    // cell; -128 may differ only where a sign stands or where a tree
    // rooted west of the edge would have leaned in.
    static uint8_t id2[CH_CELLS], st2[CH_CELLS];
    chunk_t        c2;
    memset(&c2, 0, sizeof(c2));
    c2.id = id2, c2.st = st2, c2.cx = -127, c2.cz = 0;
    worldgen_chunk(&c2, FL_SEED, FARLANDS_NONE);
    gen_into(&c, id, st, -127, 0, FL_SEED);
    CHECK(memcmp(id, id2, CH_CELLS) == 0, "the chunk east of the edge's chunk changed with the Far Lands");
    int tops_east = 0, tops_west = 0;
    gen_into(&c, id, st, -128, 0, FL_SEED);
    for (int z = 0; z < CH_D; z++) tops_east += worldgen_height(-2048, z, FL_SEED);
    gen_into(&c, id, st, -129, 0, FL_SEED);
    for (int z = 0; z < CH_D; z++) {
        int y = CH_H - 1;
        while (y > 0 && (id[CH_IDX(CH_W - 1, y, z)] == BLK_AIR || id[CH_IDX(CH_W - 1, y, z)] == BLK_WATER)) y--;
        tops_west += y;
    }
    printf("  at the edge: ground %.1f high on the ordinary side, the wall %.1f on the other\n", tops_east / 16.0,
           tops_west / 16.0);
    CHECK(tops_west - tops_east >= 16 * 15, "no cliff at the edge: ground %.1f against %.1f", tops_east / 16.0,
          tops_west / 16.0);

    // The asymmetry guard: east, north and south of the origin, and far
    // out, the Far Lands world is the ordinary world.
    struct {
        int32_t cx, cz;
    } const ORDINARY[] = {{128, 0}, {0, 128}, {0, -128}, {6250, 0}, {0, 6250}, {0, -6250}, {-127, 6250}};
    for (size_t i = 0; i < sizeof(ORDINARY) / sizeof(ORDINARY[0]); i++) {
        gen_into(&c, id, st, ORDINARY[i].cx, ORDINARY[i].cz, FL_SEED);
        memset(&c2, 0, sizeof(c2));
        c2.id = id2, c2.st = st2, c2.cx = ORDINARY[i].cx, c2.cz = ORDINARY[i].cz;
        worldgen_chunk(&c2, FL_SEED, FARLANDS_NONE);
        CHECK(memcmp(id, id2, CH_CELLS) == 0, "chunk (%d,%d) is not ordinary terrain", ORDINARY[i].cx, ORDINARY[i].cz);
    }

    // Signs along the edge: in the edge's chunk only, on the ground,
    // about one chunk in four.
    int signs = 0, misplaced = 0;
    for (int32_t cz = -64; cz < 64; cz++) {
        for (int32_t cx = -128; cx <= -127; cx++) {
            gen_into(&c, id, st, cx, cz, FL_SEED);
            for (int i = 0; i < CH_CELLS; i++) {
                if (id[i] != BLK_SIGN) continue;
                int const y = i % CH_H;
                if (cx != -128 || !block_solid(id[i - 1])) misplaced++;
                else signs++;
                (void)y;
            }
        }
    }
    printf("  %d signs along 2048 blocks of edge\n", signs);
    CHECK(misplaced == 0, "%d signs away from the edge or not standing on the ground", misplaced);
    CHECK(signs >= 10 && signs <= 40, "%d signs in 128 chunks of edge, expected about a quarter", signs);
}

static void check_worldgen(void) {
    printf("worldgen\n");

    static uint8_t ia[CH_CELLS], sa[CH_CELLS], ib[CH_CELLS], sb[CH_CELLS];
    chunk_t        a, b;

    // Determinism, including out at the Far Lands and far from the
    // origin in z, where the donor's hash could not have held (F-10).
    struct {
        int32_t cx, cz;
    } const WHERE[] = {{0, 0}, {1, -3}, {-40, 17}, {6250, -6250}, {-6250, 0}, {0, 1400}, {-1, -1}};
    for (size_t i = 0; i < sizeof(WHERE) / sizeof(WHERE[0]); i++) {
        gen_into(&a, ia, sa, WHERE[i].cx, WHERE[i].cz, GEN_SEED);
        gen_into(&b, ib, sb, WHERE[i].cx, WHERE[i].cz, GEN_SEED);
        CHECK(memcmp(ia, ib, CH_CELLS) == 0, "chunk (%d,%d) generated differently twice", WHERE[i].cx, WHERE[i].cz);
        CHECK(memcmp(sa, sb, CH_CELLS) == 0, "chunk (%d,%d) state differed between runs", WHERE[i].cx, WHERE[i].cz);
    }

    // A different seed is a different world.
    gen_into(&a, ia, sa, 0, 0, GEN_SEED);
    gen_into(&b, ib, sb, 0, 0, GEN_SEED + 1);
    CHECK(memcmp(ia, ib, CH_CELLS) != 0, "two seeds produced the identical chunk");

    // Nothing generated may carry ST_PLACED: that bit means "a player
    // put this here", and the felling rule turns on it (Part F).
    int placed = 0;
    for (int i = 0; i < CH_CELLS; i++) placed += (sa[i] & ST_PLACED) != 0;
    CHECK(placed == 0, "%d generated cells carry ST_PLACED", placed);

    // Load-order independence. Generate a 3 x 3 block of chunks one at
    // a time, then generate the middle one again on its own: it must
    // come out identical. A tree rooted in a neighbour reaches into it,
    // so this is a real test of the stamp() approach, not a tautology.
    for (int32_t cz = -1; cz <= 1; cz++) {
        for (int32_t cx = -1; cx <= 1; cx++) {
            gen_into(&a, ia, sa, cx, cz, GEN_SEED);  // the neighbours, discarded
        }
    }
    gen_into(&a, ia, sa, 0, 0, GEN_SEED);
    gen_into(&b, ib, sb, 0, 0, GEN_SEED);
    CHECK(memcmp(ia, ib, CH_CELLS) == 0, "a chunk differed when generated after its neighbours");

    // And the decoration really does cross borders -- otherwise the
    // check above proves nothing. Count leaf cells touching an edge.
    int edge_leaves = 0, total_leaves = 0, logs = 0;
    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            for (int y = 0; y < CH_H; y++) {
                uint8_t const t = ia[CH_IDX(lx, y, lz)];
                if (t == BLK_LEAVES) {
                    total_leaves++;
                    if (lx == 0 || lx == CH_W - 1 || lz == 0 || lz == CH_D - 1) edge_leaves++;
                }
                logs += (t == BLK_LOG);
            }
        }
    }
    printf("  chunk (0,0): %d logs, %d leaves (%d on an edge)\n", logs, total_leaves, edge_leaves);
    CHECK(total_leaves > 0 && logs > 0, "chunk (0,0) grew no trees: the test cannot say anything");
    CHECK(edge_leaves > 0, "no decoration reaches a chunk edge, so load-order independence is untested here");

    // Structure: bedrock everywhere, nothing above the roof, water only
    // up to sea level, and a surface that is reachable.
    int no_floor = 0, water_above_sea = 0, solid_roof = 0;
    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            uint8_t const* col = &ia[CH_IDX(lx, 0, lz)];
            no_floor += (col[CH_BEDROCK] == BLK_AIR);
            solid_roof += (col[CH_H - 1] != BLK_AIR);
            for (int y = CH_SEA_LEVEL + 1; y < CH_H; y++) water_above_sea += (col[y] == BLK_WATER);
        }
    }
    CHECK(no_floor == 0, "%d columns have no floor at y = 0: the player could fall out of the world", no_floor);
    CHECK(water_above_sea == 0, "%d water cells sit above sea level", water_above_sea);
    CHECK(solid_roof == 0, "%d columns reach the top of the world", solid_roof);

    // worldgen_height agrees with what was actually written, for the
    // columns a tree or a cave has not rearranged.
    int checked = 0, mismatch = 0;
    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            int32_t const wx = lx, wz = lz;
            int const     h   = worldgen_height(wx, wz, GEN_SEED);
            uint8_t const at  = ia[CH_IDX(lx, h, lz)];
            if (at == BLK_GRASS || at == BLK_SAND) {
                checked++;
                if (ia[CH_IDX(lx, h + 1, lz)] != BLK_AIR && ia[CH_IDX(lx, h + 1, lz)] != BLK_WATER &&
                    ia[CH_IDX(lx, h + 1, lz)] != BLK_TALL_GRASS && ia[CH_IDX(lx, h + 1, lz)] != BLK_FLOWER_RED &&
                    ia[CH_IDX(lx, h + 1, lz)] != BLK_FLOWER_YELLOW && ia[CH_IDX(lx, h + 1, lz)] != BLK_LOG &&
                    ia[CH_IDX(lx, h + 1, lz)] != BLK_LEAVES) {
                    mismatch++;
                }
            }
        }
    }
    printf("  %d surface columns checked against worldgen_height, %d odd\n", checked, mismatch);
    CHECK(checked > 100, "too few surface columns to say anything (%d)", checked);
    CHECK(mismatch == 0, "%d columns have something unexpected on the surface", mismatch);

    // A spread of chunks: the world must contain caves, ore, water and
    // dry land, or the generator is producing one boring thing.
    int air_below = 0, ore = 0, water = 0, grass = 0;
    for (int32_t cz = 0; cz < 4; cz++) {
        for (int32_t cx = 0; cx < 4; cx++) {
            gen_into(&a, ia, sa, cx * 7 - 13, cz * 7 - 13, GEN_SEED);
            for (int i = 0; i < CH_CELLS; i++) {
                int const y = (int)(i % CH_H);
                if (y > CH_BEDROCK && y < 18 && ia[i] == BLK_AIR) air_below++;
                ore += (ia[i] == BLK_COAL_ORE);
                water += (ia[i] == BLK_WATER);
                grass += (ia[i] == BLK_GRASS);
            }
        }
    }
    printf("  16 chunks: %d deep air (caves), %d coal, %d water, %d grass\n", air_below, ore, water, grass);
    CHECK(air_below > 0, "no caves anywhere in 16 chunks");
    CHECK(ore > 0, "no coal anywhere in 16 chunks");
    CHECK(grass > 0, "no grass anywhere in 16 chunks");
}

// ---------------------------------------------------------------------
//  Persistence
//
//  The properties that matter, in order of how badly they fail:
//
//    round trip     a chunk written and read back is the same chunk,
//                   including the ST_PLACED bits the felling rule needs.
//    corruption     a damaged region loses chunks, never the world, and
//                   never returns terrain that is subtly wrong.
//    torn writes    a power cut mid-save leaves the previous save
//                   readable, because the writer updates the STALE
//                   directory copy and the reader picks by serial.
//    compaction     preserves every chunk while shrinking the file.
// ---------------------------------------------------------------------

#define TEST_DIR "build/host/worldtest"

static uint8_t g_ia[CH_CELLS], g_sa[CH_CELLS], g_ib[CH_CELLS], g_sb[CH_CELLS];

static void fill_chunk(chunk_t* c, uint8_t* id, uint8_t* st, int32_t cx, int32_t cz, uint32_t seed) {
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->st = st;
    c->cx = cx;
    c->cz = cz;
    worldgen_chunk(c, seed, FARLANDS_X_DEFAULT);
}

static void check_codec(void) {
    printf("chunk codec\n");

    chunk_t a, b;
    fill_chunk(&a, g_ia, g_sa, 3, -5, GEN_SEED);

    // A generated chunk carries no state bits, so plant some: the
    // ST_PLACED bit is what the felling rule turns on, and losing it in
    // a save would be a bug nobody notices until a tree misbehaves.
    for (int i = 0; i < CH_CELLS; i += 97) g_sa[i] = ST_PLACED;
    g_sa[CH_IDX(2, 30, 4)] = st_with_growth(ST_PLACED, 5);

    static uint8_t buf[CHUNK_PAYLOAD_MAX];
    size_t const   n = chunk_encode(&a, NULL, 0, buf, sizeof(buf));
    printf("  a generated chunk packs to %zu bytes (raw would be %d)\n", n, 2 * CH_CELLS);
    CHECK(n > 0, "chunk_encode failed");
    CHECK(n < (size_t)(2 * CH_CELLS), "the encoding (%zu) is no smaller than raw (%d)", n, 2 * CH_CELLS);

    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = a.cx;
    b.cz = a.cz;
    CHECK(chunk_decode(buf, n, &b, NULL), "chunk_decode failed");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "the block plane did not survive the round trip");
    CHECK(memcmp(g_sa, g_sb, CH_CELLS) == 0, "the state plane did not survive the round trip");
    CHECK(st_growth(g_sb[CH_IDX(2, 30, 4)]) == 5, "a growth stage did not survive the round trip");

    // The worst case the codec has to survive: no runs at all, where RLE
    // would double the size. It must fall back to raw, not fail.
    for (int i = 0; i < CH_CELLS; i++) g_ia[i] = (uint8_t)((i & 1) ? BLK_STONE : BLK_DIRT);
    size_t const worst = chunk_encode(&a, NULL, 0, buf, sizeof(buf));
    CHECK(worst > 0, "chunk_encode gave up on an incompressible chunk instead of storing it raw");
    CHECK(worst <= CHUNK_PAYLOAD_MAX, "an incompressible chunk (%zu) exceeded CHUNK_PAYLOAD_MAX (%zu)", worst,
          (size_t)CHUNK_PAYLOAD_MAX);
    CHECK(chunk_decode(buf, worst, &b, NULL), "an incompressible chunk did not decode");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "an incompressible chunk did not round-trip");

    // Truncation must fail cleanly, and leave nothing half-written.
    CHECK(!chunk_decode(buf, worst / 2, &b, NULL), "a truncated payload decoded anyway");
    int nonzero = 0;
    for (int i = 0; i < CH_CELLS; i++) nonzero += (g_ib[i] != BLK_AIR);
    CHECK(nonzero == 0, "a failed decode left %d cells behind instead of clearing", nonzero);
    CHECK(!chunk_decode(buf, 0, &b, NULL), "an empty payload decoded anyway");
}

static void check_region(void) {
    printf("region files\n");
    CHECK(sm_mkdir_p(TEST_DIR), "could not create " TEST_DIR);

    // Clear anything a previous run left.
    for (int32_t rz = -1; rz <= 1; rz++) {
        for (int32_t rx = -1; rx <= 1; rx++) {
            char path[192];
            region_path(path, sizeof(path), TEST_DIR, rx, rz);
            sm_remove(path);
        }
    }

    // Region coordinate maths, negatives included.
    CHECK(region_of(0) == 0 && region_local(0) == 0, "region_of/local wrong at 0");
    CHECK(region_of(7) == 0 && region_local(7) == 7, "region_of/local wrong at 7");
    CHECK(region_of(8) == 1 && region_local(8) == 0, "region_of/local wrong at 8");
    CHECK(region_of(-1) == -1 && region_local(-1) == 7, "region_of/local wrong at -1");
    CHECK(region_of(-8) == -1 && region_local(-8) == 0, "region_of/local wrong at -8");
    CHECK(region_of(-9) == -2 && region_local(-9) == 7, "region_of/local wrong at -9");

    chunk_t a, b;

    // Absent is not an error.
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 2;
    b.cz = 2;
    CHECK(region_read_chunk(TEST_DIR, &b, NULL) == 0, "reading from a region that does not exist was an error");

    // Write a whole region's worth, spanning both signs, then read back.
    int written = 0;
    for (int32_t cz = -8; cz < 8; cz += 3) {
        for (int32_t cx = -8; cx < 8; cx += 3) {
            fill_chunk(&a, g_ia, g_sa, cx, cz, GEN_SEED);
            g_sa[CH_IDX(1, 20, 1)] = ST_PLACED;  // something to recognise it by
            if (!region_write_chunk(TEST_DIR, &a)) {
                CHECK(false, "region_write_chunk failed at (%d,%d)", cx, cz);
                return;
            }
            written++;
        }
    }
    int read_back = 0, mismatch = 0;
    for (int32_t cz = -8; cz < 8; cz += 3) {
        for (int32_t cx = -8; cx < 8; cx += 3) {
            fill_chunk(&a, g_ia, g_sa, cx, cz, GEN_SEED);
            g_sa[CH_IDX(1, 20, 1)] = ST_PLACED;
            memset(&b, 0, sizeof(b));
            b.id = g_ib;
            b.st = g_sb;
            b.cx = cx;
            b.cz = cz;
            int const r = region_read_chunk(TEST_DIR, &b, NULL);
            if (r != 1) {
                mismatch++;
                continue;
            }
            read_back++;
            if (memcmp(g_ia, g_ib, CH_CELLS) != 0 || memcmp(g_sa, g_sb, CH_CELLS) != 0) mismatch++;
        }
    }
    printf("  wrote %d chunks across 4 regions, read back %d\n", written, read_back);
    CHECK(mismatch == 0, "%d chunks did not come back as written", mismatch);
    CHECK(read_back == written, "wrote %d chunks but read back %d", written, read_back);

    // Summaries must be rebuilt by the load, not left at zero: the
    // renderer's AABB and world_ground() both read them.
    CHECK(b.top[1 * CH_W + 1] > 0, "chunk_decode did not rebuild the column summaries");

    // Rewriting the same chunk must not corrupt its neighbours, and must
    // win over the old copy.
    fill_chunk(&a, g_ia, g_sa, 2, 2, GEN_SEED);
    a.id[CH_IDX(5, 40, 5)] = BLK_GLASS;
    a.st[CH_IDX(5, 40, 5)] = ST_PLACED;
    CHECK(region_write_chunk(TEST_DIR, &a), "rewrite failed");
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 2;
    b.cz = 2;
    CHECK(region_read_chunk(TEST_DIR, &b, NULL) == 1, "could not read the rewritten chunk");
    CHECK(g_ib[CH_IDX(5, 40, 5)] == BLK_GLASS, "the rewrite did not take");
    CHECK((g_sb[CH_IDX(5, 40, 5)] & ST_PLACED) != 0, "the rewrite lost ST_PLACED");
}

// Damage a region on purpose and check what survives.
static void poke(char const* path, long off, int len, uint8_t with) {
    // region.c keeps region files open (region.h). Going behind its back
    // with a second handle reads and writes around its buffers, so the
    // cache has to be dropped first -- which is exactly the rule real
    // code follows when it deletes a world.
    region_close_all();
    FILE* f = fopen(path, "r+b");
    if (f == NULL) {
        CHECK(false, "cannot open %s to damage it", path);
        return;
    }
    fseek(f, off, SEEK_SET);
    for (int i = 0; i < len; i++) fwrite(&with, 1, 1, f);
    fclose(f);
}

// Region files live in buckets (region.h). A world written flat by an
// older build must migrate the first time it is opened, and a world
// that is already bucketed must not be disturbed.
static void check_region_buckets(void) {
    printf("region buckets\n");
    char path[192];
    region_path(path, sizeof(path), "D", 0, 0);
    CHECK(strcmp(path, "D/0.0/r.0.0" REGION_EXT) == 0, "region path is %s", path);
    region_path(path, sizeof(path), "D", -1, -1);
    CHECK(strcmp(path, "D/-1.-1/r.-1.-1" REGION_EXT) == 0, "region path is %s", path);
    // 16 regions to a bucket, and negatives must floor rather than
    // truncate or regions -1 and 0 would share one.
    region_path(path, sizeof(path), "D", 16, -16);
    CHECK(strcmp(path, "D/1.-1/r.16.-16" REGION_EXT) == 0, "region path is %s", path);
    region_path(path, sizeof(path), "D", 15, -17);
    CHECK(strcmp(path, "D/0.-2/r.15.-17" REGION_EXT) == 0, "region path is %s", path);

    // A world written flat: put three files where the old build left
    // them and open it the way worldstore does.
    char const* const FLAT = "build/host/bucketmig";
    sm_mkdir_p(FLAT);
    struct {
        int32_t rx, rz;
    } const WHERE[] = {{0, 0}, {-1, 3}, {40, -40}};
    for (size_t i = 0; i < sizeof WHERE / sizeof WHERE[0]; i++) {
        char flat[192];
        snprintf(flat, sizeof(flat), "%s/r.%ld.%ld%s", FLAT, (long)WHERE[i].rx, (long)WHERE[i].rz, REGION_EXT);
        FILE* f = fopen(flat, "wb");
        CHECK(f != NULL, "could not lay down %s", flat);
        if (f != NULL) {
            fputs("not a real region, but a real file", f);
            fclose(f);
        }
    }
    // Something that is NOT a region file must be left exactly alone.
    char other[192];
    snprintf(other, sizeof(other), "%s/notes.txt", FLAT);
    FILE* o = fopen(other, "wb");
    if (o != NULL) { fputs("x", o); fclose(o); }

    int const moved = region_migrate(FLAT);
    printf("  migrated %d flat region file(s)\n", moved);
    CHECK(moved == 3, "migrated %d region files, expected 3", moved);

    for (size_t i = 0; i < sizeof WHERE / sizeof WHERE[0]; i++) {
        char to[192], from[192];
        region_path(to, sizeof(to), FLAT, WHERE[i].rx, WHERE[i].rz);
        snprintf(from, sizeof(from), "%s/r.%ld.%ld%s", FLAT, (long)WHERE[i].rx, (long)WHERE[i].rz, REGION_EXT);
        FILE* f = fopen(to, "rb");
        CHECK(f != NULL, "%s is not in its bucket", to);
        if (f != NULL) fclose(f);
        f = fopen(from, "rb");
        CHECK(f == NULL, "%s is still lying flat", from);
        if (f != NULL) fclose(f);
    }
    FILE* f = fopen(other, "rb");
    CHECK(f != NULL, "the migration moved a file that was not a region");
    if (f != NULL) fclose(f);

    // Running it again must move nothing: a bucketed world is left be,
    // and the bucket DIRECTORIES must not be mistaken for region files.
    CHECK(region_migrate(FLAT) == 0, "a second migration moved something");
    printf("  a second open moves nothing\n");
}

static void check_region_damage(void) {
    printf("region damage\n");
    char path[192];
    region_path(path, sizeof(path), TEST_DIR, 0, 0);

    chunk_t a, b;
    fill_chunk(&a, g_ia, g_sa, 1, 1, GEN_SEED);
    CHECK(region_write_chunk(TEST_DIR, &a), "setup write failed");

    // Directory copy A sits at 0x040. Destroy it: copy B must carry the
    // region, because the writer alternates and both are kept current
    // within one save of each other.
    poke(path, 0x040, 64, 0xAB);
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 1;
    b.cz = 1;
    int const r = region_read_chunk(TEST_DIR, &b, NULL);
    printf("  directory copy A destroyed: read returned %d\n", r);
    CHECK(r == 1, "losing one directory copy lost the chunk (the dual copy is not working)");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "the chunk came back wrong after losing a directory copy");

    // Now destroy the other copy too: the region is beyond saving and
    // must say so, rather than hand back rubbish.
    poke(path, 0x248, 64, 0xCD);
    int const r2 = region_read_chunk(TEST_DIR, &b, NULL);
    printf("  both directory copies destroyed: read returned %d\n", r2);
    CHECK(r2 == -1, "a region with no valid directory did not report an error (got %d)", r2);

    // A damaged payload must read as "no chunk", so it is regenerated,
    // not as broken terrain.
    char path2[192];
    region_path(path2, sizeof(path2), TEST_DIR, -1, -1);
    region_close_all();
    sm_remove(path2);
    fill_chunk(&a, g_ia, g_sa, -8, -8, GEN_SEED);
    CHECK(region_write_chunk(TEST_DIR, &a), "setup write failed");
    poke(path2, 0x480 + 8, 200, 0xEE);
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = -8;
    b.cz = -8;
    int const r3 = region_read_chunk(TEST_DIR, &b, NULL);
    printf("  payload corrupted: read returned %d\n", r3);
    CHECK(r3 == 0 || r3 == -1, "a corrupt payload decoded as if it were fine");
    if (r3 == 0) {
        int nonzero = 0;
        for (int i = 0; i < CH_CELLS; i++) nonzero += (g_ib[i] != BLK_AIR);
        CHECK(nonzero == 0, "a rejected payload still left %d cells behind", nonzero);
    }
}

// The durability claim, tested rather than asserted: a save interrupted
// part way must leave the PREVIOUS save readable.
//
// The writer appends the payload, then rewrites whichever directory copy
// is stale with serial+1, which makes it current. A crash during that
// directory write leaves it garbage -- so destroying the current copy is
// exactly what an interrupted save looks like from the reader's side.
// The region is fresh, so the sequence of serials is known: create
// writes A=1, B=0; the first save writes B=2; the second writes A=3.
// After two saves the current copy is A, and behind it sits the first
// save, intact.
static void check_torn_write(void) {
    printf("torn write\n");
    char path[192];
    region_path(path, sizeof(path), TEST_DIR, 1, 0);
    sm_remove(path);

    chunk_t a, b;
    size_t const marker = CH_IDX(6, 35, 6);

    fill_chunk(&a, g_ia, g_sa, 8, 0, GEN_SEED);
    g_ia[marker] = BLK_GLASS;                       /* save 1 */
    CHECK(region_write_chunk(TEST_DIR, &a), "first save failed");

    fill_chunk(&a, g_ia, g_sa, 8, 0, GEN_SEED);
    g_ia[marker] = BLK_PLANKS;                      /* save 2 */
    CHECK(region_write_chunk(TEST_DIR, &a), "second save failed");

    // Sanity: save 2 is what a healthy region returns.
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 8;
    b.cz = 0;
    CHECK(region_read_chunk(TEST_DIR, &b, NULL) == 1, "the healthy region did not read");
    CHECK(g_ib[marker] == BLK_PLANKS, "the second save is not the one that reads back");

    // Now interrupt save 2: destroy the directory copy it wrote.
    poke(path, 0x040, 520, 0x5A);

    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 8;
    b.cz = 0;
    int const r = region_read_chunk(TEST_DIR, &b, NULL);
    printf("  save interrupted: read returned %d, marker is %s\n", r,
           g_ib[marker] == BLK_GLASS    ? "the FIRST save (recovered)"
           : g_ib[marker] == BLK_PLANKS ? "the second save"
                                        : "neither");
    CHECK(r == 1, "an interrupted save lost the chunk entirely (got %d)", r);
    CHECK(g_ib[marker] == BLK_GLASS,
          "an interrupted save did not fall back to the previous one -- the dual directory is not doing its job");

    // And the region must still be writable afterwards: recovery is not
    // much use if the next save fails.
    fill_chunk(&a, g_ia, g_sa, 8, 0, GEN_SEED);
    g_ia[marker] = BLK_COBBLE;
    CHECK(region_write_chunk(TEST_DIR, &a), "could not write to a region after recovering from a torn save");
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    b.cx = 8;
    b.cz = 0;
    CHECK(region_read_chunk(TEST_DIR, &b, NULL) == 1, "the region did not read after a post-recovery save");
    CHECK(g_ib[marker] == BLK_COBBLE, "the post-recovery save did not take");
}

static void check_compaction(void) {
    printf("compaction\n");
    char path[192];
    region_path(path, sizeof(path), TEST_DIR, 0, 1);
    sm_remove(path);

    chunk_t a;
    // Rewrite the same few chunks many times, so the file fills with
    // dead copies.
    for (int pass = 0; pass < 12; pass++) {
        for (int32_t cz = 8; cz < 11; cz++) {
            for (int32_t cx = 0; cx < 3; cx++) {
                fill_chunk(&a, g_ia, g_sa, cx, cz, GEN_SEED);
                g_ia[CH_IDX(0, 40, 0)] = (uint8_t)(pass % 2 ? BLK_GLASS : BLK_PLANKS);
                CHECK(region_write_chunk(TEST_DIR, &a), "compaction setup write failed");
            }
        }
    }

    FILE* f = fopen(path, "rb");
    CHECK(f != NULL, "no region to compact");
    if (f == NULL) return;
    fseek(f, 0, SEEK_END);
    long const before = ftell(f);
    fclose(f);

    CHECK(region_should_compact(TEST_DIR, 0, 1), "12 rewrites did not make the region worth compacting");
    CHECK(region_compact(TEST_DIR, 0, 1), "region_compact failed");

    f = fopen(path, "rb");
    CHECK(f != NULL, "the region vanished during compaction");
    if (f == NULL) return;
    fseek(f, 0, SEEK_END);
    long const after = ftell(f);
    fclose(f);
    printf("  %ld bytes -> %ld after compaction\n", before, after);
    CHECK(after < before, "compaction did not shrink the region (%ld -> %ld)", before, after);

    // And every chunk still reads, with the last value written.
    int lost = 0, wrong = 0;
    for (int32_t cz = 8; cz < 11; cz++) {
        for (int32_t cx = 0; cx < 3; cx++) {
            chunk_t b;
            memset(&b, 0, sizeof(b));
            b.id = g_ib;
            b.st = g_sb;
            b.cx = cx;
            b.cz = cz;
            if (region_read_chunk(TEST_DIR, &b, NULL) != 1) {
                lost++;
                continue;
            }
            if (g_ib[CH_IDX(0, 40, 0)] != BLK_GLASS) wrong++;
        }
    }
    CHECK(lost == 0, "compaction lost %d chunks", lost);
    CHECK(wrong == 0, "compaction resurrected %d stale chunk copies", wrong);
    CHECK(!region_should_compact(TEST_DIR, 0, 1), "the region still wants compacting afterwards");
}

// ---------------------------------------------------------------------
//  Tagged fields
//
//  The point of this codec is that formats can grow without a migration
//  for every change, so the checks are about exactly that: a build that
//  does not know a field must step over it, and a build that expects a
//  field the save does not have must keep its default. Everything else
//  is bookkeeping.
// ---------------------------------------------------------------------

// A cow, as an OLD build knows it.
typedef struct {
    float   x, y, z;
    int32_t health;
} cow_v1_t;

// The same cow after someone added the things the user asked for:
// whether it is aggressive, whether the dog is sitting, a name.
typedef struct {
    float   x, y, z;
    int32_t health;
    int32_t aggressive;
    int32_t sitting;
    char    nametag[24];
    int64_t bred_at;
} cow_v2_t;

static void cow_v1_write(tag_writer_t* w, cow_v1_t const* c) {
    tag_put_f32(w, "x", c->x);
    tag_put_f32(w, "y", c->y);
    tag_put_f32(w, "z", c->z);
    tag_put_i32(w, "health", c->health);
}

static void cow_v2_write(tag_writer_t* w, cow_v2_t const* c) {
    tag_put_f32(w, "x", c->x);
    tag_put_f32(w, "y", c->y);
    tag_put_f32(w, "z", c->z);
    tag_put_i32(w, "health", c->health);
    tag_put_i32(w, "aggressive", c->aggressive);
    tag_put_i32(w, "sitting", c->sitting);
    tag_put_str(w, "nametag", c->nametag);
    tag_put_i64(w, "bred_at", c->bred_at);
}

// Both readers follow the same three steps: default, overwrite what is
// recognised, skip the rest.
static void cow_v1_read(tag_reader_t* r, cow_v1_t* c) {
    *c = (cow_v1_t){.x = 0, .y = 0, .z = 0, .health = 10};
    char name[TAG_NAME_MAX + 1];
    for (;;) {
        int const t = tag_next(r, name, sizeof(name));
        if (t == TAG_END) break;
        if (t == TAG_F32 && strcmp(name, "x") == 0) c->x = tag_get_f32(r);
        else if (t == TAG_F32 && strcmp(name, "y") == 0) c->y = tag_get_f32(r);
        else if (t == TAG_F32 && strcmp(name, "z") == 0) c->z = tag_get_f32(r);
        else if (t == TAG_I32 && strcmp(name, "health") == 0) c->health = tag_get_i32(r);
        else tag_skip(r, t);
    }
}

static void cow_v2_read(tag_reader_t* r, cow_v2_t* c) {
    memset(c, 0, sizeof(*c));
    c->health     = 10;
    c->aggressive = 0;
    c->sitting    = 0;
    c->bred_at    = -1;      /* the default a new field gets in an old save */
    snprintf(c->nametag, sizeof(c->nametag), "%s", "");
    char name[TAG_NAME_MAX + 1];
    for (;;) {
        int const t = tag_next(r, name, sizeof(name));
        if (t == TAG_END) break;
        if (t == TAG_F32 && strcmp(name, "x") == 0) c->x = tag_get_f32(r);
        else if (t == TAG_F32 && strcmp(name, "y") == 0) c->y = tag_get_f32(r);
        else if (t == TAG_F32 && strcmp(name, "z") == 0) c->z = tag_get_f32(r);
        else if (t == TAG_I32 && strcmp(name, "health") == 0) c->health = tag_get_i32(r);
        else if (t == TAG_I32 && strcmp(name, "aggressive") == 0) c->aggressive = tag_get_i32(r);
        else if (t == TAG_I32 && strcmp(name, "sitting") == 0) c->sitting = tag_get_i32(r);
        else if (t == TAG_STR && strcmp(name, "nametag") == 0) tag_get_str(r, c->nametag, sizeof(c->nametag));
        else if (t == TAG_I64 && strcmp(name, "bred_at") == 0) c->bred_at = tag_get_i64(r);
        else tag_skip(r, t);
    }
}

static void check_tags(void) {
    printf("tagged fields\n");
    static uint8_t buf[512];

    // Every type survives a round trip.
    {
        tag_writer_t w;
        tag_write_init(&w, buf, sizeof(buf));
        tag_put_i8(&w, "a", -7);
        tag_put_i16(&w, "b", -30000);
        tag_put_i32(&w, "c", -123456789);
        tag_put_i64(&w, "d", -1234567890123LL);
        tag_put_f32(&w, "e", 3.25f);
        tag_put_str(&w, "f", "a dog called Rex");
        uint8_t const blob[5] = {1, 2, 3, 4, 5};
        tag_put_blob(&w, "g", blob, sizeof(blob));
        size_t const n = tag_write_done(&w);
        CHECK(n > 0, "the writer overflowed on a small record");

        tag_reader_t r;
        tag_read_init(&r, buf, n);
        char name[TAG_NAME_MAX + 1];
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_I8 && tag_get_i8(&r) == -7, "i8 did not round-trip");
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_I16 && tag_get_i16(&r) == -30000, "i16 did not round-trip");
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_I32 && tag_get_i32(&r) == -123456789, "i32 did not round-trip");
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_I64 && tag_get_i64(&r) == -1234567890123LL,
              "i64 did not round-trip");
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_F32 && tag_get_f32(&r) == 3.25f, "f32 did not round-trip");
        char str[32];
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_STR, "str tag lost its type");
        tag_get_str(&r, str, sizeof(str));
        CHECK(strcmp(str, "a dog called Rex") == 0, "str did not round-trip (got \"%s\")", str);
        uint8_t back[5] = {0};
        CHECK(tag_next(&r, name, sizeof(name)) == TAG_BLOB, "blob tag lost its type");
        CHECK(tag_get_blob(&r, back, sizeof(back)) == 5 && memcmp(back, blob, 5) == 0, "blob did not round-trip");
        CHECK(!r.error, "the reader errored on a well-formed record");
    }

    // THE POINT, part one: a NEW save read by an OLD build. The fields
    // it does not know must be stepped over, and the ones it does must
    // come back right.
    cow_v2_t const newer = {.x = 1.5f, .y = 64.0f, .z = -2.25f, .health = 18, .aggressive = 1, .sitting = 1,
                            .nametag = "Rex", .bred_at = 999};
    tag_writer_t w;
    tag_write_init(&w, buf, sizeof(buf));
    cow_v2_write(&w, &newer);
    size_t const n2 = tag_write_done(&w);
    CHECK(n2 > 0, "writing the newer record overflowed");

    tag_reader_t r;
    tag_read_init(&r, buf, n2);
    cow_v1_t old_read;
    cow_v1_read(&r, &old_read);
    CHECK(!r.error, "an old build errored reading a newer record");
    CHECK(old_read.x == newer.x && old_read.z == newer.z, "an old build misread the fields it does know");
    CHECK(old_read.health == 18, "an old build lost a field it does know");

    // THE POINT, part two: an OLD save read by a NEW build. The fields
    // that did not exist yet must keep their defaults, not rubbish.
    cow_v1_t const older = {.x = -8.0f, .y = 30.0f, .z = 4.0f, .health = 7};
    tag_write_init(&w, buf, sizeof(buf));
    cow_v1_write(&w, &older);
    size_t const n1 = tag_write_done(&w);

    tag_read_init(&r, buf, n1);
    cow_v2_t new_read;
    cow_v2_read(&r, &new_read);
    CHECK(!r.error, "a new build errored reading an older record");
    CHECK(new_read.x == older.x && new_read.health == 7, "a new build misread an older record");
    CHECK(new_read.aggressive == 0 && new_read.sitting == 0, "a field absent from an old save did not default");
    CHECK(new_read.bred_at == -1, "a new field did not keep its default (%lld)", (long long)new_read.bred_at);
    CHECK(new_read.nametag[0] == '\0', "a new string field did not default to empty");

    // Nesting, and skipping a whole compound: a furnace's inventory is
    // the case this has to survive.
    {
        tag_write_init(&w, buf, sizeof(buf));
        tag_put_i32(&w, "burn", 40);
        tag_begin(&w, "items");
        tag_put_i32(&w, "slot0", 11);
        tag_begin(&w, "nested");
        tag_put_i32(&w, "deep", 5);
        tag_end(&w);
        tag_put_i32(&w, "slot1", 22);
        tag_end(&w);
        tag_put_i32(&w, "after", 77);
        size_t const n = tag_write_done(&w);
        CHECK(n > 0, "the nested record overflowed");

        tag_read_init(&r, buf, n);
        char name[TAG_NAME_MAX + 1];
        int  burn = 0, after = 0;
        for (;;) {
            int const t = tag_next(&r, name, sizeof(name));
            if (t == TAG_END) break;
            if (t == TAG_I32 && strcmp(name, "burn") == 0) burn = tag_get_i32(&r);
            else if (t == TAG_I32 && strcmp(name, "after") == 0) after = tag_get_i32(&r);
            else tag_skip(&r, t);   /* the whole "items" compound, nesting included */
        }
        CHECK(!r.error, "skipping a nested compound errored");
        CHECK(burn == 40, "the field before a skipped compound was lost");
        CHECK(after == 77, "skipping a nested compound did not land on the next field (got %d)", after);
    }

    // A truncated record must fail, not read past its buffer.
    tag_read_init(&r, buf, 3);
    char name[TAG_NAME_MAX + 1];
    while (tag_next(&r, name, sizeof(name)) != TAG_END) { /* drain */ }
    CHECK(r.pos <= r.len, "the reader ran past the end of a truncated record");

    // And a writer given too little room must report it rather than
    // write a record that decodes as something else.
    uint8_t tiny[8];
    tag_write_init(&w, tiny, sizeof(tiny));
    cow_v2_write(&w, &newer);
    CHECK(tag_write_done(&w) == 0, "a writer that overflowed still reported a length");
}

// ---------------------------------------------------------------------
//  Sections, and the world store
// ---------------------------------------------------------------------

typedef struct {
    int     seen;
    uint8_t last_id;
    size_t  last_len;
    uint8_t first_byte;
} section_tally_t;

static void tally_section(uint8_t id, uint8_t const* data, size_t len, void* user) {
    section_tally_t* t = user;
    t->seen++;
    t->last_id    = id;
    t->last_len   = len;
    t->first_byte = len > 0 ? data[0] : 0;
}

static void check_sections(void) {
    printf("chunk sections\n");

    chunk_t a, b;
    fill_chunk(&a, g_ia, g_sa, 0, 0, GEN_SEED);

    // Build two sections by hand: one this build knows about, and one
    // with an id it has never heard of -- which is what a save from a
    // future build looks like.
    uint8_t sec[64];
    size_t  w = 0;
    uint8_t const payload_known[3]   = {0xA1, 0xA2, 0xA3};
    uint8_t const payload_unknown[5] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5};

    sec[w++] = SECTION_ENTITIES;
    sec[w++] = (uint8_t)sizeof(payload_known);
    sec[w++] = 0;
    sec[w++] = 0;
    sec[w++] = 0;
    memcpy(&sec[w], payload_known, sizeof(payload_known));
    w += sizeof(payload_known);

    sec[w++] = 99;  // from the future
    sec[w++] = (uint8_t)sizeof(payload_unknown);
    sec[w++] = 0;
    sec[w++] = 0;
    sec[w++] = 0;
    memcpy(&sec[w], payload_unknown, sizeof(payload_unknown));
    w += sizeof(payload_unknown);

    static uint8_t buf[CHUNK_PAYLOAD_MAX];
    size_t const   n = chunk_encode(&a, sec, w, buf, sizeof(buf));
    CHECK(n > 0, "encoding a chunk with sections failed");

    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    section_tally_t t = {0};
    CHECK(chunk_decode_ex(buf, n, &b, NULL, tally_section, &t), "decoding a chunk with sections failed");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "sections disturbed the block plane");
    CHECK(t.seen == 2, "expected 2 sections, saw %d", t.seen);
    CHECK(t.last_id == 99 && t.last_len == 5 && t.first_byte == 0xB1,
          "the unknown section did not arrive intact (id %u len %zu)", t.last_id, t.last_len);

    // And the decoder that does NOT care about sections must step over
    // them without complaint -- that is an old build reading this save.
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    CHECK(chunk_decode(buf, n, &b, NULL), "a section-unaware decode rejected a chunk with sections");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "a section-unaware decode got the wrong blocks");

    // A section whose length runs off the end is corruption.
    buf[n - 1] = 0xFF;
    uint8_t bad[CHUNK_PAYLOAD_MAX];
    memcpy(bad, buf, n);
    bad[n - w + 1] = 0xFF;  /* blow up the known section's length */
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    CHECK(!chunk_decode(bad, n, &b, NULL), "a section running past the end decoded anyway");
}

#define STORE_BASE "build/host/storetest"

static void check_worldstore(void) {
    printf("world store\n");

    CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");

    world_meta_t   meta;
    player_state_t player;

    // Delete anything a previous run left, so the check is repeatable.
    world_meta_t old[SM_WORLDS_MAX];
    int const    prior = worldstore_list(old, SM_WORLDS_MAX);
    for (int i = 0; i < prior; i++) worldstore_delete(old[i].slug);
    CHECK(worldstore_list(old, SM_WORLDS_MAX) == 0, "could not clear the world directory");

    // Create. The slug must be a legal FAT name whatever was typed.
    CHECK(worldstore_create("Kurt's Far Lands!", 12345u, &meta, &player), "worldstore_create failed");
    printf("  \"%s\" -> slug \"%s\", seed %u\n", meta.name, meta.slug, meta.seed);
    CHECK(strcmp(meta.name, "Kurt's Far Lands!") == 0, "the display name was mangled");
    for (char const* c = meta.slug; *c; c++) {
        CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_',
              "slug \"%s\" has a character FAT will not like", meta.slug);
    }

    // The player defaults, and a round trip of every field.
    CHECK(player.health == 20 && player.hunger == 20, "a new player did not get default health/hunger");
    player.x       = -100000.5;   /* out at the Far Lands, to prove the doubles survive */
    player.y       = 71.25;
    player.z       = 12.5;
    player.yaw     = 1.5f;
    player.health  = 13;
    player.hunger  = 7;
    player.has_bed = true;
    player.bed_x   = -99998;
    player.bed_y   = 70;
    player.bed_z   = 11;
    meta.time_of_day   = 4321;
    meta.play_secs     = 99;
    CHECK(worldstore_save(&meta, &player, NULL), "worldstore_save failed");

    world_meta_t   m2;
    player_state_t p2;
    worldstore_close();
    CHECK(worldstore_open(meta.slug, &m2, &p2, NULL), "worldstore_open failed");
    CHECK(strcmp(m2.name, meta.name) == 0, "the name did not survive a save/load");
    CHECK(m2.seed == 12345u, "the seed did not survive a save/load");
    CHECK(m2.play_secs == 99, "play_secs did not survive");
    CHECK(p2.x == player.x && p2.y == player.y && p2.z == player.z, "the player position did not survive");
    CHECK(p2.health == 13 && p2.hunger == 7, "player health/hunger did not survive");
    CHECK(p2.has_bed && p2.bed_x == -99998, "the bed spawn did not survive");
    CHECK(m2.time_of_day == 4321, "the world's time of day did not survive");

    // A second world with the same name must not collide.
    world_meta_t   m3;
    player_state_t p3;
    CHECK(worldstore_create("Kurt's Far Lands!", 777u, &m3, &p3), "creating a second world failed");
    CHECK(strcmp(m3.slug, meta.slug) != 0, "two worlds of the same name got the same slug (%s)", m3.slug);

    // Listing finds both.
    world_meta_t list[SM_WORLDS_MAX];
    int const    n = worldstore_list(list, SM_WORLDS_MAX);
    printf("  %d worlds listed\n", n);
    CHECK(n == 2, "expected 2 worlds, listed %d", n);

    // Chunks go through the store, which owns the paths.
    CHECK(worldstore_open(meta.slug, &m2, &p2, NULL), "reopening failed");
    chunk_t c;
    fill_chunk(&c, g_ia, g_sa, 4, -9, m2.seed);
    g_sa[CH_IDX(3, 33, 3)] = ST_PLACED;
    CHECK(world_chunk_save(&c), "world_chunk_save failed");

    chunk_t back;
    memset(&back, 0, sizeof(back));
    back.id = g_ib;
    back.st = g_sb;
    back.cx = 4;
    back.cz = -9;
    CHECK(world_chunk_load(&back) == 1, "world_chunk_load did not find the chunk it just saved");
    CHECK(memcmp(g_ia, g_ib, CH_CELLS) == 0, "the chunk did not survive the store round trip");
    CHECK((g_sb[CH_IDX(3, 33, 3)] & ST_PLACED) != 0, "ST_PLACED did not survive the store round trip");

    // Deleting removes the world and its regions.
    CHECK(worldstore_delete(m3.slug), "worldstore_delete failed");
    CHECK(worldstore_list(list, SM_WORLDS_MAX) == 1, "the deleted world is still listed");
    CHECK(!worldstore_open(m3.slug, &m3, &p3, NULL), "a deleted world still opens");
}

// A level.smw exactly as the builds before save slots wrote it: no
// "placed", no inventory. The Testworld people already have on their
// cards looks like this, so this is the file the adoption must handle.
static void write_legacy_level(char const* slug, double x, double y, double z) {
    char dir[192], path[224];
    snprintf(dir, sizeof(dir), "%s/worlds/%s/region", STORE_BASE, slug);
    CHECK(sm_mkdir_p(dir), "could not make the legacy world's directory");
    snprintf(path, sizeof(path), "%s/worlds/%s/level.smw", STORE_BASE, slug);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL, "could not write the legacy level.smw");
    if (f == NULL) return;
    fwrite("SMW1", 1, 4, f);
    NbtWriter w;
    nbt_write_open(&w, f);
    nbt_write_compound(&w, "level");
    nbt_write_int32(&w, "format", 1);
    nbt_write_string(&w, "name", slug);
    nbt_write_int32(&w, "seed", (int32_t)0xC0FFEEu);
    nbt_write_int64(&w, "created", 1);
    nbt_write_int64(&w, "last_played", 1);
    nbt_write_int32(&w, "play_secs", 0);
    nbt_write_int32(&w, "spawn_x", 0);
    nbt_write_int32(&w, "spawn_y", CH_SEA_LEVEL + 2);
    nbt_write_int32(&w, "spawn_z", 0);
    nbt_write_compound(&w, "player");
    nbt_write_double(&w, "x", x);
    nbt_write_double(&w, "y", y);
    nbt_write_double(&w, "z", z);
    nbt_write_double(&w, "yaw", 0.5);
    nbt_write_double(&w, "pitch", 0.0);
    nbt_write_int32(&w, "health", 20);
    nbt_write_int32(&w, "hunger", 20);
    nbt_write_int64(&w, "time_of_day", 7777);  // where builds before D-52 kept the clock
    nbt_write_end(&w);
    nbt_write_compound(&w, "palette");
    for (int i = 0; i < BLK_COUNT; i++) nbt_write_int32(&w, BLOCKS[i].name, i);
    nbt_write_end(&w);
    nbt_write_end(&w);
    fclose(f);
}

static void clear_store(void) {
    world_meta_t old[SM_WORLDS_MAX];
    int const    prior = worldstore_list(old, SM_WORLDS_MAX);
    for (int i = 0; i < prior; i++) worldstore_delete(old[i].slug);
}

// The player's data moving out of the install directory (datadir.h):
// everything moves, a second start finds nothing to do, and an entry
// already at the new place is never overwritten.
static bool dd_write(char const* path, char const* text) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

// Delete a directory tree. The rename test builds a whole card and has
// to start from nothing: a hand-written list of files to remove is a
// list somebody forgets to extend, and a leftover directory makes the
// NEXT run's adoption refuse to merge -- which looks like a migration
// bug and is not one.
static void dd_rmtree(char const* path) {
    // Bounded so the compiler can see that the recursion below cannot
    // grow a path without limit; the test's own paths are ~60 bytes.
    if (path == NULL || strlen(path) > 180) return;
    char      names[64][96];
    int       n = 0;
    sm_dir_t* d = sm_dir_open(path);
    if (d != NULL) {
        char const* name;
        while (n < 64 && (name = sm_dir_next(d, NULL)) != NULL) {
            if (strlen(name) < sizeof(names[0])) snprintf(names[n++], sizeof(names[0]), "%s", name);
        }
        sm_dir_close(d);
    }
    for (int i = 0; i < n; i++) {
        char child[288];
        if (snprintf(child, sizeof(child), "%s/%s", path, names[i]) >= (int)sizeof(child)) continue;
        dd_rmtree(child);
        sm_remove(child);
    }
    sm_remove(path);
}

static bool dd_reads(char const* path, char const* text) {
    char  buf[64] = {0};
    FILE* f       = fopen(path, "rb");
    if (f == NULL) return false;
    size_t const n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strcmp(buf, text) == 0;
}

static bool dd_exists(char const* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// The title's world is kept on the card and rebuilt only when this
// build would make a different one (worldstore.h). Three things must
// hold: it is invisible to the world list, a matching seed and version
// reopens it, and either one differing throws it away.
static void check_title_world(void) {
    printf("the title's world\n");
    // This check runs before check_worldstore, which is where the store
    // is normally opened; open it here so the order of the checks is
    // not load-bearing.
    CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");
    world_meta_t   m;
    player_state_t pl;
    bool           fresh = false;

    CHECK(worldstore_open_title(0xB05u, 1u, &m, &pl, &fresh), "the title world would not open");
    CHECK(fresh, "a title world that was never made did not report itself fresh");
    // Not marked yet: an interrupted first boot must be generated again
    // rather than come back with half a word in it.
    world_meta_t m2;
    CHECK(worldstore_open_title(0xB05u, 1u, &m2, &pl, &fresh), "reopen failed");
    CHECK(fresh, "an UNMARKED title world was reused");
    CHECK(worldstore_title_mark(1u), "could not mark the title world");

    CHECK(worldstore_open_title(0xB05u, 1u, &m2, &pl, &fresh), "reopen after marking failed");
    CHECK(!fresh, "a marked title world of the same seed and version was regenerated");
    CHECK(m2.seed == 0xB05u, "the reopened title world has seed %u", (unsigned)m2.seed);
    printf("  a marked world of the same seed and version is reused\n");

    // A NEW SM_TITLE_GEN means this build wants a different picture.
    CHECK(worldstore_open_title(0xB05u, 2u, &m2, &pl, &fresh), "open at a new version failed");
    CHECK(fresh, "a title world built to version 1 was reused at version 2");
    CHECK(worldstore_title_mark(2u), "could not mark at the new version");
    CHECK(worldstore_open_title(0xB05u, 2u, &m2, &pl, &fresh), "reopen at the new version failed");
    CHECK(!fresh, "the world marked at version 2 was not reused");
    // ... and so does a new seed.
    CHECK(worldstore_open_title(0xB06u, 2u, &m2, &pl, &fresh), "open at a new seed failed");
    CHECK(fresh, "a title world of another seed was reused");
    printf("  a new version or a new seed throws it away\n");

    // INVISIBLE TO THE PLAY MENU. It lives beside bench, outside
    // worlds/, and worldstore_list() enumerates worlds/ only.
    CHECK(worldstore_title_mark(2u), "could not mark before listing");
    worldstore_close();
    world_meta_t list[16];
    int const    n = worldstore_list(list, 16);
    int          seen = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].slug, SM_TITLE_SLUG) == 0) seen++;
    }
    CHECK(seen == 0, "the title world is listed in the world-select screen");
    printf("  %d world(s) listed, and the title is not one of them\n", n);
    worldstore_delete(SM_TITLE_SLUG);
}

// Does a chunk generated in the TITLE world actually reach the card?
// On the badge it did not: the world was created and marked, and its 81
// chunks were generated and "saved" every boot without one region file
// appearing. This walks the same path the title does.
static void check_title_persists(void) {
    printf("the title world keeps its terrain\n");
    world_meta_t   m;
    player_state_t pl;
    bool           fresh = false;

    CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");
    worldstore_delete(SM_TITLE_SLUG);
    CHECK(chunk_store_init(), "chunk_store_init failed");
    CHECK(worldstore_open_title(0xB05u, 1u, &m, &pl, &fresh), "open_title failed");
    CHECK(fresh, "a title world that was just deleted did not report itself fresh");
    CHECK(chunk_worker_start(m.seed), "chunk_worker_start failed");

    // Generate one, exactly as the loading screen does.
    CHECK(chunk_worker_request_load(0, 0), "request_load refused");
    chunk_t* c = chunk_find(0, 0);
    CHECK(c != NULL, "the chunk never became resident");
    if (c == NULL) return;
    CHECK((c->flags & CF_EDITED) != 0, "a freshly generated chunk is not marked for saving");
    uint8_t const sample = c->id[CH_IDX(3, CH_BEDROCK, 3)];

    // And save it, exactly as the title does when it is fresh.
    CHECK(chunk_worker_request_save(0, 0), "request_save refused");
    while (!chunk_worker_idle()) chunk_worker_collect(64);

    // THE FILE HAS TO BE THERE. This is the assertion the badge failed.
    char dir[192], path[224];
    snprintf(dir, sizeof(dir), "%s/%s/region", STORE_BASE, SM_TITLE_SLUG);
    CHECK(region_path(path, sizeof(path), dir, region_of(0), region_of(0)), "region_path failed");
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL, "no region file at %s -- the title's terrain was not written", path);
    long sz = 0;
    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fclose(f);
    }
    CHECK(sz > 0, "the region file is empty (%ld bytes)", sz);
    printf("  one generated chunk wrote %ld bytes to %s\n", sz, path);

    // And it must come BACK, rather than being generated again.
    chunk_worker_stop();
    chunk_store_shutdown();
    worldstore_close();
    CHECK(chunk_store_init(), "chunk_store_init (second) failed");
    CHECK(worldstore_open_title(0xB05u, 1u, &m, &pl, &fresh), "reopen failed");
    CHECK(chunk_worker_start(m.seed), "chunk_worker_start (second) failed");
    CHECK(chunk_worker_request_load(0, 0), "request_load (second) refused");
    chunk_t* c2 = chunk_find(0, 0);
    CHECK(c2 != NULL, "the chunk did not come back");
    if (c2 != NULL) {
        CHECK(c2->id[CH_IDX(3, CH_BEDROCK, 3)] == sample, "the reloaded chunk differs from the saved one");
    }
    chunk_worker_stop();
    chunk_store_shutdown();
    worldstore_delete(SM_TITLE_SLUG);
}

static void check_datadir(void) {
    printf("the data directory\n");
    char const* const OLD = "build/host/ddtest/apps/at.cavac.synthminer";
    char const* const NEW = "build/host/ddtest/synthminer";
    // A clean slate: what an earlier run left is renamed out of the way
    // by removing the files it could have made.
    char const* const LEFT[] = {"build/host/ddtest/synthminer/worlds/slot1/level.smw", "build/host/ddtest/synthminer/settings.txt",
                                "build/host/ddtest/synthminer/replays/last.smr", "build/host/ddtest/synthminer/screenshots/shot001.png"};
    for (size_t i = 0; i < sizeof(LEFT) / sizeof(LEFT[0]); i++) remove(LEFT[i]);
    remove("build/host/ddtest/synthminer/worlds/slot1");
    remove("build/host/ddtest/synthminer/worlds");
    remove("build/host/ddtest/synthminer/replays");
    remove("build/host/ddtest/synthminer/screenshots");
    remove("build/host/ddtest/synthminer");

    // An install directory as a build before this left it.
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.synthminer/worlds/slot1"), "could not make the old worlds");
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.synthminer/replays"), "could not make the old replays");
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.synthminer/screenshots"), "could not make the old screenshots");
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.synthminer/textures"), "could not make the textures");
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/worlds/slot1/level.smw", "testworld");
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/settings.txt", "view=2");
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/replays/last.smr", "replay");
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/screenshots/shot001.png", "png");
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/textures/dirt.png", "dirt");

    char      report[1024];
    int const moved = datadir_adopt(OLD, NEW, DD_INSTALL, report, sizeof(report));
    printf("  %d entries moved\n", moved);
    CHECK(moved == 4, "%d entries moved, expected 4 (worlds, settings.txt, replays, screenshots)", moved);
    CHECK(dd_reads("build/host/ddtest/synthminer/worlds/slot1/level.smw", "testworld"), "the world did not arrive");
    CHECK(dd_reads("build/host/ddtest/synthminer/settings.txt", "view=2"), "settings.txt did not arrive");
    CHECK(dd_reads("build/host/ddtest/synthminer/replays/last.smr", "replay"), "the replay did not arrive");
    CHECK(dd_reads("build/host/ddtest/synthminer/screenshots/shot001.png", "png"), "the screenshot did not arrive");
    CHECK(!dd_exists("build/host/ddtest/apps/at.cavac.synthminer/worlds"), "the old worlds are still there");
    CHECK(dd_reads("build/host/ddtest/apps/at.cavac.synthminer/textures/dirt.png", "dirt"),
          "the app's own files were touched");

    // Started again: nothing to do.
    CHECK(datadir_adopt(OLD, NEW, DD_INSTALL, report, sizeof(report)) == 0, "a second start moved something");

    // An old build run after this one writes settings into the install
    // directory again: the new place's copy wins, nothing is overwritten.
    dd_write("build/host/ddtest/apps/at.cavac.synthminer/settings.txt", "view=0");
    CHECK(datadir_adopt(OLD, NEW, DD_INSTALL, report, sizeof(report)) == 0, "an entry was moved over an existing one");
    CHECK(dd_reads("build/host/ddtest/synthminer/settings.txt", "view=2"), "the new settings.txt was overwritten");
    CHECK(strstr(report, "left") != NULL, "a clash was not reported: \"%s\"", report);
    remove("build/host/ddtest/apps/at.cavac.synthminer/settings.txt");
}

// --- The rename ------------------------------------------------------
//
// A card as CraftMiner left it (D-91), start to finish: the data in
// /sd/craftminer and every saved file ending in .cmw or .cmr. After a
// start there must be nothing left under the old name and nothing lost
// under the new one -- and a second start must be a quiet no-op, since
// this runs every time.
static void check_rename(void) {
    printf("the rename from CraftMiner\n");
    char const* const WAS = "build/host/ddtest/craftminer";
    char const* const NOW = "build/host/ddtest/renamed";

    // A clean slate, all of it.
    dd_rmtree(NOW);
    dd_rmtree(WAS);
    dd_rmtree("build/host/ddtest/apps/at.cavac.craftminer");

    // The card, exactly as the old name left it.
    CHECK(sm_mkdir_p("build/host/ddtest/craftminer/worlds/slot1/region"), "could not make slot 1");
    CHECK(sm_mkdir_p("build/host/ddtest/craftminer/worlds/slot2"), "could not make slot 2");
    CHECK(sm_mkdir_p("build/host/ddtest/craftminer/bench/region"), "could not make the bench world");
    CHECK(sm_mkdir_p("build/host/ddtest/craftminer/replays"), "could not make the replays");
    dd_write("build/host/ddtest/craftminer/worlds/slot1/level.cmw", "CMW1 slot one");
    dd_write("build/host/ddtest/craftminer/worlds/slot1/region/r.0.0.cmr", "CMR1 terrain");
    dd_write("build/host/ddtest/craftminer/worlds/slot1/region/r.-1.2.cmr", "CMR1 more terrain");
    dd_write("build/host/ddtest/craftminer/worlds/slot2/level.cmw", "CMW1 slot two");
    dd_write("build/host/ddtest/craftminer/bench/level.cmw", "CMW1 the bench world");
    dd_write("build/host/ddtest/craftminer/bench/region/r.0.0.cmr", "CMR1 bench terrain");
    dd_write("build/host/ddtest/craftminer/replays/last.cmr", "CMRP a replay");
    dd_write("build/host/ddtest/craftminer/settings.txt", "view=1");

    // What main() does on start, in that order.
    char      report[1024];
    int const moved = datadir_adopt(WAS, NOW, DD_DATA, report, sizeof(report));
    printf("  %d entries moved out of /sd/craftminer\n", moved);
    CHECK(moved == 4, "%d entries moved, expected 4 (worlds, bench, settings.txt, replays)", moved);

    int const renamed = datadir_rename_saves(NOW, report, sizeof(report));
    printf("  %d file(s) given their new extension\n", renamed);
    CHECK(renamed == 7, "%d files renamed, expected 7", renamed);

    // Every byte is where it should be, under the new name.
    CHECK(dd_reads("build/host/ddtest/renamed/worlds/slot1/level.smw", "CMW1 slot one"), "slot 1's level did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/worlds/slot1/region/r.0.0.smr", "CMR1 terrain"), "slot 1's terrain did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/worlds/slot1/region/r.-1.2.smr", "CMR1 more terrain"),
          "a negative region coordinate did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/worlds/slot2/level.smw", "CMW1 slot two"), "slot 2's level did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/bench/level.smw", "CMW1 the bench world"), "the bench world did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/bench/region/r.0.0.smr", "CMR1 bench terrain"), "the bench terrain did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/replays/last.smr", "CMRP a replay"), "the replay did not survive");
    CHECK(dd_reads("build/host/ddtest/renamed/settings.txt", "view=1"), "settings.txt did not survive");

    // And nothing is left under the old name.
    CHECK(!dd_exists("build/host/ddtest/renamed/worlds/slot1/level.cmw"), "slot 1 still has a .cmw");
    CHECK(!dd_exists("build/host/ddtest/renamed/worlds/slot1/region/r.0.0.cmr"), "slot 1 still has a .cmr");
    CHECK(!dd_exists("build/host/ddtest/craftminer/worlds"), "the old data directory still has the worlds");

    // A second start finds nothing to do -- it runs every time.
    CHECK(datadir_adopt(WAS, NOW, DD_DATA, report, sizeof(report)) == 0, "a second start moved something");
    CHECK(datadir_rename_saves(NOW, report, sizeof(report)) == 0, "a second start renamed something");

    // The readers take the old magic, so a file that never got renamed
    // -- a card pulled mid-way -- still loads (worldstore.h, region.h).
    CHECK(memcmp("CMW1 slot one", SM_LEVEL_MAGIC_WAS, 3) == 0, "the legacy level magic is not what CraftMiner wrote");
    CHECK(memcmp("CMR1 terrain", REGION_MAGIC_WAS, 4) == 0, "the legacy region magic is not what CraftMiner wrote");

    // --- and then the old game goes (D-92) ---------------------------
    //
    // The adoption above emptied /sd/craftminer, so retiring it must
    // take the directory itself. The old INSTALL directory still has
    // the app's own files in it -- textures, music, app.so -- and those
    // go too: they are CraftMiner's, not the player's.
    char const* const OLD_APP = "build/host/ddtest/apps/at.cavac.craftminer";
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.craftminer/textures"), "could not make the old textures");
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.craftminer/music"), "could not make the old music");
    dd_write("build/host/ddtest/apps/at.cavac.craftminer/app.so", "an old binary");
    dd_write("build/host/ddtest/apps/at.cavac.craftminer/metadata.json", "{}");
    dd_write("build/host/ddtest/apps/at.cavac.craftminer/textures/dirt.png", "dirt");
    dd_write("build/host/ddtest/apps/at.cavac.craftminer/music/satie.mid", "notes");

    // IT REFUSES while a world is still in there. This is the case that
    // matters: adoption leaves an entry whose destination exists, and
    // that entry is somebody's world.
    CHECK(sm_mkdir_p("build/host/ddtest/apps/at.cavac.craftminer/worlds/slot1"), "could not make a stranded world");
    dd_write("build/host/ddtest/apps/at.cavac.craftminer/worlds/slot1/level.cmw", "SMW1 stranded");
    CHECK(datadir_retire(OLD_APP, "build/host/ddtest/apps/at.cavac.synthminer", DD_INSTALL, report, sizeof(report)) == -1,
          "a directory with a world still in it was retired anyway");
    CHECK(dd_reads("build/host/ddtest/apps/at.cavac.craftminer/worlds/slot1/level.cmw", "SMW1 stranded"),
          "the stranded world was deleted");
    CHECK(strstr(report, "still in it") != NULL, "the refusal was not reported: \"%s\"", report);
    printf("  refused while a world was in it: %s", report);

    // It refuses a path that reaches the live one, whatever is in it.
    CHECK(datadir_retire("build/host/ddtest/apps", "build/host/ddtest/apps/at.cavac.synthminer", DD_INSTALL, report,
                         sizeof(report)) == -1,
          "the parent of the live install directory was retired");
    CHECK(datadir_retire(OLD_APP, OLD_APP, DD_INSTALL, report, sizeof(report)) == -1, "a directory retired itself");
    CHECK(dd_exists(OLD_APP), "a refusal deleted something anyway");

    // With the world gone -- adopted, as it would have been -- it goes,
    // shipped files and all.
    remove("build/host/ddtest/apps/at.cavac.craftminer/worlds/slot1/level.cmw");
    remove("build/host/ddtest/apps/at.cavac.craftminer/worlds/slot1");
    remove("build/host/ddtest/apps/at.cavac.craftminer/worlds");
    int const gone = datadir_retire(OLD_APP, "build/host/ddtest/apps/at.cavac.synthminer", DD_INSTALL, report, sizeof(report));
    printf("  %d entries removed with the old install directory\n", gone);
    CHECK(gone == 7, "%d entries removed, expected 7 (4 files, 2 directories, the directory itself)", gone);
    CHECK(!dd_exists(OLD_APP), "the old install directory is still there");
    CHECK(!dd_exists("build/host/ddtest/apps/at.cavac.craftminer/textures/dirt.png"), "a shipped texture survived");

    // The old data directory was emptied by the adoption, so it goes too.
    int const dgone = datadir_retire(WAS, NOW, DD_DATA, report, sizeof(report));
    CHECK(dgone >= 1, "the emptied data directory was not removed");
    CHECK(!dd_exists("build/host/ddtest/craftminer"), "the old data directory is still there");
    printf("  %s", report);

    // AN INTERRUPTED MIGRATION MUST NOT LOOK LIKE AN EMPTY SLOT (F-93).
    // Regions are renamed before the level file, so a migration that
    // stops half way leaves `level.cmw` behind -- which this build does
    // not look for, so the slot reads DAMAGED and nothing may be built
    // over it. The dangerous shape is the other way round: a world that
    // OPENS with no terrain would generate fresh ground over the
    // player's and save it under the new names.
    {
        world_meta_t peek;
        CHECK(worldstore_init(NOW), "worldstore_init on the migrated directory failed");
        char dir[256], path[288];
        snprintf(dir, sizeof(dir), "%s/worlds/slot5/region", NOW);
        CHECK(sm_mkdir_p(dir), "could not make a half-migrated world");
        snprintf(path, sizeof(path), "%s/worlds/slot5/region/r.0.0.smr", NOW);
        dd_write(path, "SMR1 terrain that was renamed");
        snprintf(path, sizeof(path), "%s/worlds/slot5/level.cmw", NOW);
        dd_write(path, "CMW1 a level file the rename had not reached");
        CHECK(worldstore_slot_state(4, &peek) == SLOT_DAMAGED,
              "a half-migrated world does not read as damaged");
        world_meta_t   m2;
        player_state_t p2;
        CHECK(!worldstore_create_in(4, "Over it", 1u, &m2, &p2), "a world was created over a half-migrated one");
        snprintf(path, sizeof(path), "%s/worlds/slot5/level.cmw", NOW);
        CHECK(dd_exists(path), "the half-migrated level file was destroyed");

        // And the next start finishes it, which is the self-healing half.
        int const finished = datadir_rename_saves(NOW, report, sizeof(report));
        CHECK(finished == 1, "the next start renamed %d files, expected 1 (the level)", finished);
        snprintf(path, sizeof(path), "%s/worlds/slot5/level.smw", NOW);
        CHECK(dd_exists(path), "the level file was not renamed on the next start");
        printf("  an interrupted world read as damaged, then healed on the next start\n");
        worldstore_delete("slot5");
    }

    // Started again with both already gone: quiet, and not a refusal.
    CHECK(datadir_retire(WAS, NOW, DD_DATA, report, sizeof(report)) == 0, "retiring a directory that is gone complained");
    CHECK(datadir_retire(OLD_APP, "build/host/ddtest/apps/at.cavac.synthminer", DD_INSTALL, report, sizeof(report)) == 0,
          "retiring an install directory that is gone complained");
}

static void check_slots(void) {
    printf("save slots\n");
    CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");
    clear_store();

    world_meta_t   meta, peek;
    player_state_t player;
    for (int i = 0; i < SM_SLOTS; i++) CHECK(!worldstore_slot_peek(i, &peek), "slot %d is not empty", i + 1);

    // Nothing to adopt on a fresh card: the common case, and it must be
    // a quiet no-op.
    CHECK(worldstore_adopt_legacy("flyover", "Testworld") == -1, "adopted a world from an empty card");

    // Creating fills exactly the slot asked for, and refuses a taken one.
    CHECK(worldstore_create_in(2, "My World", 99u, &meta, &player), "worldstore_create_in failed");
    CHECK(strcmp(meta.slug, "slot3") == 0, "slot 3 went into \"%s\"", meta.slug);
    CHECK(worldstore_slot_peek(2, &peek) && strcmp(peek.name, "My World") == 0, "slot 3 does not show its world");
    CHECK(!worldstore_create_in(2, "Other", 1u, &meta, &player), "created a world over an existing one");
    CHECK(worldstore_slot_peek(2, &peek) && peek.seed == 99u, "the refused create damaged slot 3");

    // The inventory and the exact position round trip, by name.
    CHECK(!player.has_inv && !player.placed, "a new player claims a saved inventory or position");
    player.has_inv = true;
    memset(player.inv, 0, sizeof(player.inv));
    player.inv[0]       = (inv_slot_t){ITEM_PICK_STONE, 1, 17};
    player.inv[4]       = (inv_slot_t){BLK_COBBLE, 37, 0};
    player.inv[INV_SLOTS - 1] = (inv_slot_t){BLK_TORCH, 5, 0};
    player.inv_selected = 4;
    player.placed       = true;
    player.x            = 12.25;
    player.y            = 11.0;  // in a cave, far below the surface
    player.z            = -3.75;
    CHECK(worldstore_save(&meta, &player, NULL), "saving the slot world failed");
    worldstore_close();

    player_state_t back;
    CHECK(worldstore_open("slot3", &meta, &back, NULL), "reopening slot 3 failed");
    CHECK(back.placed && back.y == 11.0 && back.x == 12.25 && back.z == -3.75, "the exact position did not survive");
    CHECK(back.has_inv, "the inventory did not come back");
    CHECK(back.inv_selected == 4, "the selected slot did not survive (%d)", (int)back.inv_selected);
    CHECK(back.inv[0].item == ITEM_PICK_STONE && back.inv[0].count == 1 && back.inv[0].wear == 17,
          "the worn pickaxe did not survive");
    CHECK(back.inv[4].item == BLK_COBBLE && back.inv[4].count == 37, "the cobblestone did not survive");
    CHECK(back.inv[INV_SLOTS - 1].item == BLK_TORCH && back.inv[INV_SLOTS - 1].count == 5,
          "the last slot did not survive");
    int filled = 0;
    for (int i = 0; i < INV_SLOTS; i++) filled += back.inv[i].item != 0;
    CHECK(filled == 3, "expected 3 filled slots back, got %d", filled);

    // Dropped items round-trip with the world, by name, age and delay
    // intact (D-68).
    static world_items_t items, items_back;
    memset(&items, 0, sizeof(items));
    items.n    = 2;
    items.e[0] = (item_entity_t){.alive = true, .item = BLK_LOG, .count = 5, .age = 300, .pickup_at = 310};
    phys_body_init(&items.e[0].body, 4.25, 30.0, -6.5);
    items.e[1] = (item_entity_t){.alive = true, .item = ITEM_AXE_STONE, .count = 1, .wear = 40, .age = 11999,
                                 .pickup_at = 12039};
    phys_body_init(&items.e[1].body, -1.0, 12.5, 2.0);
    CHECK(worldstore_open("slot3", &meta, &back, NULL), "reopening slot 3 for the items failed");
    CHECK(worldstore_save(&meta, &back, &items), "saving the items failed");
    worldstore_close();
    CHECK(worldstore_open("slot3", &meta, &back, &items_back), "reopening slot 3 with its items failed");
    CHECK(items_back.n == 2, "%d items came back, not 2", items_back.n);
    CHECK(items_back.e[0].item == BLK_LOG && items_back.e[0].count == 5 && items_back.e[0].age == 300 &&
              items_back.e[0].pickup_at == 310 && items_back.e[0].body.x == 4.25,
          "the logs on the ground did not survive");
    CHECK(items_back.e[1].item == ITEM_AXE_STONE && items_back.e[1].wear == 40 && items_back.e[1].age == 11999,
          "the axe on the ground did not survive");
    // In an unloaded chunk, an item holds still: it neither falls nor
    // ages. (Nothing is resident here: the store was cleared.)
    item_entity_restore(items_back.e, items_back.n);
    item_entity_tick(NULL, 0.0, 0.0, 0.0);
    CHECK(item_entity_at(0)->age == 300 && item_entity_at(0)->body.y == 30.0,
          "an item in an unloaded chunk aged or fell (age %u, y %.2f)", item_entity_at(0)->age,
          item_entity_at(0)->body.y);
    item_entity_reset();
    worldstore_close();

    // Renaming touches the name only.
    CHECK(worldstore_rename("slot3", "Renamed"), "rename failed");
    CHECK(worldstore_open("slot3", &meta, &back, NULL), "reopening after the rename failed");
    CHECK(strcmp(meta.name, "Renamed") == 0 && meta.seed == 99u, "rename lost the seed or the name");
    CHECK(back.inv[4].count == 37 && back.y == 11.0, "rename lost the player");
    worldstore_close();

    // THE TESTWORLD. A pre-slots world, with a chunk in it, goes into
    // the first free slot under its new name, with its terrain and its
    // player intact -- and a second start finds nothing more to do.
    write_legacy_level("flyover", 40.5, 30.0, -7.5);
    CHECK(worldstore_open("flyover", &meta, &back, NULL), "the legacy world does not open as it is");
    chunk_t c;
    fill_chunk(&c, g_ia, g_sa, 2, 3, meta.seed);
    g_ia[CH_IDX(5, 40, 5)] = BLK_GLASS;
    g_sa[CH_IDX(5, 40, 5)] = ST_PLACED;
    CHECK(world_chunk_save(&c), "could not save a chunk into the legacy world");
    worldstore_close();

    int const slot = worldstore_adopt_legacy("flyover", "Testworld");
    printf("  the legacy world went to slot %d\n", slot + 1);
    CHECK(slot == 0, "the legacy world went to slot %d, not the first free one", slot + 1);
    CHECK(worldstore_slot_peek(0, &peek) && strcmp(peek.name, "Testworld") == 0, "slot 1 is not called Testworld");
    CHECK(peek.seed == 0xC0FFEEu, "the Testworld lost its seed");
    CHECK(!worldstore_open("flyover", &meta, &back, NULL), "the legacy world is still where it was");
    CHECK(worldstore_open("slot1", &meta, &back, NULL), "the adopted world does not open");
    CHECK(back.placed && back.x == 40.5 && back.y == 30.0, "a legacy player who had played was not put back exactly");
    CHECK(!back.has_inv, "a legacy player got an inventory they never saved");
    CHECK(meta.time_of_day == 7777, "the clock from the old player record was not moved to the world (%lld)",
          (long long)meta.time_of_day);
    chunk_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.id = g_ib;
    cb.st = g_sb;
    cb.cx = 2;
    cb.cz = 3;
    CHECK(world_chunk_load(&cb) == 1, "the adopted world lost its terrain");
    CHECK(g_ib[CH_IDX(5, 40, 5)] == BLK_GLASS && (g_sb[CH_IDX(5, 40, 5)] & ST_PLACED) != 0,
          "the player's block did not come with the world");
    worldstore_close();
    CHECK(worldstore_adopt_legacy("flyover", "Testworld") == -1, "the adoption is not idempotent");
    CHECK(worldstore_slot_peek(2, &peek) && strcmp(peek.name, "Renamed") == 0, "the adoption disturbed slot 3");

    // A legacy player who never left through Esc still has the default
    // at the spawn column's centre: that is a guess, not a position.
    write_legacy_level("flyover", 0.5, (double)CH_SEA_LEVEL + 2.0, 0.5);
    CHECK(worldstore_open("flyover", &meta, &back, NULL), "the second legacy world does not open");
    CHECK(!back.placed, "the untouched default position was taken as a real one");
    worldstore_close();
    CHECK(worldstore_adopt_legacy("flyover", "Testworld") == 1, "with slot 1 taken, the next free slot is 2");

    // A world from a newer build is not an empty slot: it is told apart,
    // and nothing may be created over it. Nor is a damaged one.
    {
        char dir[192], path[224];
        snprintf(dir, sizeof(dir), "%s/worlds/slot8", STORE_BASE);
        CHECK(sm_mkdir_p(dir), "could not make slot 8's directory");
        snprintf(path, sizeof(path), "%s/level.smw", dir);
        FILE* f = fopen(path, "wb");
        if (f != NULL) {
            fwrite("SMW9 something a later build understands", 1, 41, f);
            fclose(f);
        }
        CHECK(worldstore_slot_state(7, &peek) == SLOT_NEWER, "a world from a newer build does not read as newer");
        CHECK(!worldstore_create_in(7, "Over it", 1u, &meta, &player), "a world was created over a newer build's");
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite("SMW1 not nbt at all", 1, 19, f);
            fclose(f);
        }
        CHECK(worldstore_slot_state(7, &peek) == SLOT_DAMAGED, "an unreadable level.smw does not read as damaged");
        CHECK(worldstore_slot_state(6, &peek) == SLOT_EMPTY, "an empty slot does not read as empty");
        CHECK(worldstore_slot_state(0, &peek) == SLOT_WORLD && strcmp(peek.name, "Testworld") == 0,
              "a readable world does not read as a world");
        worldstore_delete("slot8");
        CHECK(worldstore_slot_state(7, &peek) == SLOT_EMPTY, "deleting a damaged world did not free its slot");
    }

    // Deleting frees the slot, directory and all.
    CHECK(worldstore_delete("slot3"), "deleting slot 3 failed");
    CHECK(!worldstore_slot_peek(2, &peek), "slot 3 still shows a world");
    CHECK(worldstore_create_in(2, "Again", 5u, &meta, &player), "the freed slot could not be reused");
    worldstore_close();
    clear_store();
}

// The remap is what lets block ids be added, reordered or removed
// without breaking existing worlds. Two things have to hold: the
// palette really is written into level.smw (so a future build can read
// what the ids meant), and chunk_decode really applies a remap.
static void check_palette(void) {
    printf("block palette\n");

    // 1. level.smw carries every block's NAME. Without that, a future
    //    build has nothing to map old ids by.
    char path[256];
    snprintf(path, sizeof(path), "%s/worlds/kurt_s_far_lands/level.smw", STORE_BASE);
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL, "no level.smw at %s", path);
    if (f == NULL) return;
    static char blob[16384];
    size_t const got = fread(blob, 1, sizeof(blob) - 1, f);
    fclose(f);
    blob[got] = '\0';

    // The magic carries the major version, ahead of the NBT.
    CHECK(got > 4 && memcmp(blob, SM_LEVEL_MAGIC, 3) == 0, "level.smw does not start with its magic");
    CHECK(blob[3] == SM_LEVEL_MAJOR, "level.smw's major version byte is '%c', expected '%c'", blob[3],
          SM_LEVEL_MAJOR);

    int found = 0;
    for (int i = 0; i < BLK_COUNT; i++) {
        size_t const n = strlen(BLOCKS[i].name);
        for (size_t at = 0; at + n <= got; at++) {
            if (memcmp(&blob[at], BLOCKS[i].name, n) == 0) {
                found++;
                break;
            }
        }
    }
    printf("  level.smw names %d of %d blocks\n", found, BLK_COUNT);
    CHECK(found == BLK_COUNT, "the palette names only %d of %d blocks", found, BLK_COUNT);

    // 2. A remap really is applied on decode. Stand in for "a future
    //    build renumbered everything" by shifting every id by hand.
    chunk_t a, b;
    fill_chunk(&a, g_ia, g_sa, 1, 1, GEN_SEED);

    // Count what we are about to move, so the check cannot pass vacuously.
    int stone_before = 0;
    for (int i = 0; i < CH_CELLS; i++) stone_before += (g_ia[i] == BLK_STONE);
    CHECK(stone_before > 0, "the test chunk has no stone to remap");

    static uint8_t buf[CHUNK_PAYLOAD_MAX];
    size_t const   n = chunk_encode(&a, NULL, 0, buf, sizeof(buf));

    // A remap that turns every saved stone into cobblestone and leaves
    // the rest alone -- the shape of what a renumbering produces.
    uint8_t remap[256];
    for (int i = 0; i < 256; i++) remap[i] = (uint8_t)i;
    remap[BLK_STONE] = BLK_COBBLE;

    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    CHECK(chunk_decode(buf, n, &b, remap), "decoding with a remap failed");

    int stone_after = 0, cobble_after = 0;
    for (int i = 0; i < CH_CELLS; i++) {
        stone_after += (g_ib[i] == BLK_STONE);
        cobble_after += (g_ib[i] == BLK_COBBLE);
    }
    printf("  %d stone cells remapped\n", stone_before);
    CHECK(stone_after == 0, "%d cells kept the old id despite the remap", stone_after);
    CHECK(cobble_after >= stone_before, "the remapped cells did not arrive as the new id");

    // And a remap that drops a block (it no longer exists) turns it into
    // air rather than into whatever happens to sit at that number.
    remap[BLK_STONE] = BLK_AIR;
    memset(&b, 0, sizeof(b));
    b.id = g_ib;
    b.st = g_sb;
    CHECK(chunk_decode(buf, n, &b, remap), "decoding with a dropping remap failed");
    int air_now = 0;
    for (int i = 0; i < CH_CELLS; i++) air_now += (g_ib[i] == BLK_AIR);
    CHECK(air_now >= stone_before, "a removed block did not become air");
}

// ---------------------------------------------------------------------
//  Streaming
//
//  On the host the worker runs every job inline, which is the same code
//  path the badge takes in synchronous mode -- so this exercises the
//  request / do / apply cycle end to end without a badge.
//
//  It exists because the first version of that cycle looked perfectly
//  correct and loaded nothing at all: do_load() reached for the chunk
//  with chunk_find(), which deliberately hides a chunk while it is
//  CS_LOADING so no game code can read a half-filled one -- and the
//  loader is the code doing the filling. The badge drew an empty world
//  at a confident 30 fps. Nothing below would have passed.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  The flight recorder. It runs on every world the user plays, so the
//  two things that must hold are that it writes what it says it writes
//  and that it CANNOT grow without bound on the card.
// ---------------------------------------------------------------------
static long file_size(char const* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return -1;
    fseek(f, 0, SEEK_END);
    long const n = ftell(f);
    fclose(f);
    return n;
}

static void check_trace(void) {
    printf("the flight recorder\n");
    char cur[256], prev[256];
    snprintf(cur, sizeof cur, "%s/trace.txt", STORE_BASE);
    snprintf(prev, sizeof prev, "%s/trace.prev.txt", STORE_BASE);
    sm_remove(cur);
    sm_remove(prev);

    CHECK(trace_open(STORE_BASE, "deadbee", "2.4", 12648430u, "Testworld"), "trace_open failed");
    trace_note("H view=%d textures=%s", 1, "on");

    // An edit, then the mesh that answers it: the lag between them is
    // the number nothing else in the game reports.
    trace_set_time(10.0);
    trace_edit('P', -2031, 33, 255, "torch", 1);
    trace_set_time(10.31);
    trace_mesh(chunk_of(-2031), chunk_of(255), 0, ch_sect_of(33));

    trace_tick(&(trace_tick_t){.t = 11.0, .fps = 11.4, .px = -2032.8, .py = 32.0, .pz = 254.7,
                               .flat = 1184, .flat_cap = 6144, .tex = 412, .tex_cap = 4096,
                               .drawn = 14, .sections = 25, .resident = 49, .queue_cap = 48});
    trace_close();

    // What came out.
    FILE* f = fopen(cur, "rb");
    CHECK(f != NULL, "no trace file was written");
    if (f == NULL) return;
    char body[4096] = {0};
    size_t const got = fread(body, 1, sizeof body - 1, f);
    fclose(f);
    CHECK(got > 0, "the trace file is empty");
    CHECK(strstr(body, "seed=12648430") != NULL, "the trace does not name the seed");
    CHECK(strstr(body, "world=\"Testworld\"") != NULL, "the trace does not name the world");
    CHECK(strstr(body, "blk=torch") != NULL, "the trace did not record the placed block");
    CHECK(strstr(body, "lag=310ms") != NULL, "the trace did not time the mesh that answered the edit");
    CHECK(strstr(body, "tex=412/4096") != NULL, "the trace did not record how full the lists got");
    printf("  %d lines, %ld bytes, and the mesh lag came out at 310 ms\n", trace_lines(), file_size(cur));

    // ONE PLAYTHROUGH PER FILE. Opening again rotates unconditionally:
    // the run just finished becomes trace.prev.txt and the new one
    // starts empty. Two files on the card, never more, and a file that
    // holds exactly one run cannot be misread as two.
    long const first = file_size(cur);
    CHECK(first > 0, "the first session wrote nothing");
    CHECK(trace_open(STORE_BASE, "deadbee", "2.4", 1u, "again"), "trace_open failed on the second run");
    trace_set_time(1.0);
    trace_edit('B', 1, 1, 1, "stone", 1);
    trace_close();
    CHECK(file_size(prev) == first, "the previous run did not become trace.prev.txt (%ld vs %ld)",
          file_size(prev), first);
    CHECK(file_size(cur) < first, "the new run did not start empty (%ld bytes, previous was %ld)",
          file_size(cur), first);

    // And the new file must hold ONLY the new run.
    f = fopen(cur, "rb");
    CHECK(f != NULL, "the second run wrote no file");
    if (f != NULL) {
        char again[2048] = {0};
        (void)!fread(again, 1, sizeof again - 1, f);
        fclose(f);
        CHECK(strstr(again, "world=\"again\"") != NULL, "the new run is not in the new file");
        CHECK(strstr(again, "Testworld") == NULL, "the previous run is still in the new file");
        CHECK(strstr(again, "blk=torch") == NULL, "an edit from the previous run survived into the new file");
    }
    printf("  a second run starts empty; the first is kept as trace.prev.txt\n");

    // THE CAP IS A CEILING, not a rotation: a single run long enough to
    // fill it stops recording rather than throwing away its own start.
    CHECK(trace_open(STORE_BASE, "deadbee", "2.4", 2u, "long"), "trace_open failed for the cap check");
    for (unsigned i = 0; i < TRACE_MAX_BYTES / 64 + 64; i++) {
        trace_tick(&(trace_tick_t){.t = (double)i, .fps = 10.0f, .flat_cap = 6144, .tex_cap = 4096});
    }
    long const capped = file_size(cur);
    trace_close();
    CHECK(capped <= (long)TRACE_MAX_BYTES + 4096, "a long run grew to %ld, past the cap", capped);
    CHECK(file_size(prev) < first + 4096, "the cap check rotated instead of stopping");
    printf("  a run that fills the file stops at %ld bytes instead of rotating\n", capped);
    sm_remove(cur);
    sm_remove(prev);
}

static void check_streaming(void) {
    printf("streaming\n");

    // The store was shut down after the earlier checks; streaming needs
    // it back.
    CHECK(chunk_store_init(), "chunk_store_init failed");

    world_meta_t   meta;
    player_state_t player;
    CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");
    CHECK(worldstore_create("stream test", 4242u, &meta, &player), "could not create the streaming world");
    CHECK(chunk_worker_start(meta.seed), "chunk_worker_start failed");
    CHECK(chunk_worker_synchronous(), "the host worker should be synchronous");

    // A chunk nobody has visited: it must be generated and resident.
    CHECK(chunk_find(0, 0) == NULL, "chunk (0,0) was already resident");
    CHECK(chunk_worker_request_load(0, 0), "request_load(0,0) was refused");
    chunk_t* c = chunk_find(0, 0);
    CHECK(c != NULL, "a requested chunk never became resident");
    if (c == NULL) return;
    CHECK(c->cstate == CS_READY, "a loaded chunk is in state %u, expected CS_READY", c->cstate);
    CHECK((c->flags & CF_GENERATED) != 0 || (c->flags & CF_EDITED) != 0, "a loaded chunk has no content flags");

    int solid = 0;
    for (int i = 0; i < CH_CELLS; i++) solid += (c->id[i] != BLK_AIR);
    printf("  chunk (0,0) streamed in with %d non-air cells\n", solid);
    CHECK(solid > 0, "a streamed chunk is entirely air");
    CHECK(world_ground(4, 4) > 0, "the streamed chunk has no ground to stand on");

    // Its neighbours, so meshing has real borders to work with.
    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dz == 0) continue;
            CHECK(chunk_worker_request_load(dx, dz), "request_load(%d,%d) was refused", dx, dz);
        }
    }

    // Meshing, at each level of detail.
    uint8_t* scratch = malloc(chunkmesh_scratch_bytes());
    CHECK(scratch != NULL, "no scratch for the mesher");
    if (scratch == NULL) return;
    for (int lod = 0; lod < LOD_COUNT; lod++) {
        int total = 0, surface = 0, empty = 0;
        for (int sect = 0; sect < CH_SECT_N; sect++) {
            mesh_t m;
            CHECK(chunkmesh_build(0, 0, lod, sect, scratch, &m), "chunkmesh_build failed at lod %d section %d", lod,
                  sect);
            total += m.tn;
            if (m.tn == 0) empty++;
            CHECK(m.vn <= 40000, "lod %d section %d produced %d vertices, close to mesh_t's 65535 limit (F-13)", lod,
                  sect, m.vn);

            // Every greedy face must carry a direction, or the
            // renderer's cull silently drops it.
            int none = 0;
            for (int i = 0; i < m.tn; i++) none += (m.t[i].dir == MESH_DIR_NONE);
            CHECK(none == 0 || lod == LOD_FANCY, "lod %d section %d has %d triangles with no face direction", lod,
                  sect, none);

            // The mesh must fit inside ITS SECTION's box, or the
            // renderer's per-section bounding box and its frustum cull
            // are both lies -- and a section drawn in the wrong band is
            // exactly the bug sectioning could introduce (D-34).
            float const ylo = (float)(sect * CH_SECT), yhi = ylo + (float)CH_SECT;
            for (int i = 0; i < m.vn; i++) {
                CHECK(m.v[i].x >= -0.01f && m.v[i].x <= (float)CH_W + 0.01f,
                      "lod %d section %d vertex %d is outside the chunk in x (%g)", lod, sect, i, m.v[i].x);
                CHECK(m.v[i].z >= -0.01f && m.v[i].z <= (float)CH_D + 0.01f,
                      "lod %d section %d vertex %d is outside the chunk in z (%g)", lod, sect, i, m.v[i].z);
                CHECK(m.v[i].y >= ylo - 0.01f && m.v[i].y <= yhi + 0.01f,
                      "lod %d section %d vertex %d is at y %g, outside its band %g..%g", lod, sect, i, m.v[i].y, ylo,
                      yhi);
                if (s_fail) break;
            }

            // Which section the ground is in, for the line below: the
            // one holding the most triangles.
            if (m.tn > surface) surface = m.tn;
            mesh_free(&m);
            if (s_fail) break;
        }
        printf("  lod %d: %d tris over %d sections (%d empty, biggest %d)\n", lod, total, CH_SECT_N, empty, surface);
        CHECK(total > 0, "lod %d produced no triangles at all", lod);
        if (s_fail) break;
    }

    // Where a chunk's triangles sit, per section (D-34).
    //
    // THIS IS THE ORIGIN, AND THE ORIGIN IS OCEAN. Do not read a claim
    // about the world out of these three numbers -- that is exactly the
    // mistake F-33 made, and D-37 is the rule that came out of it. The
    // world-wide figures are in F-35, measured over 49 chunks spread
    // across 4000 blocks: 34% underground, 47% surface, 17% above. This
    // print is here so that a change which quietly moves geometry
    // between sections shows up in `make check` at all.
    if (!s_fail) {
        int by_sect[CH_SECT_N];
        memset(by_sect, 0, sizeof(by_sect));
        int chunks = 0;
        for (int32_t cz = -1; cz <= 1; cz++) {
            for (int32_t cx = -1; cx <= 1; cx++) {
                if (chunk_find(cx, cz) == NULL) continue;
                chunks++;
                for (int sect = 0; sect < CH_SECT_N; sect++) {
                    mesh_t m;
                    if (!chunkmesh_build(cx, cz, LOD_FAST, sect, scratch, &m)) continue;
                    by_sect[sect] += m.tn;
                    mesh_free(&m);
                }
            }
        }
        int total = 0, top = 0;
        for (int i = 0; i < CH_SECT_N; i++) {
            total += by_sect[i];
            if (by_sect[i] > by_sect[top]) top = i;
        }
        printf("  the origin 3x3 (ocean, not typical -- F-35): %d chunks, %d tris: ", chunks, total);
        for (int i = 0; i < CH_SECT_N; i++) {
            printf("y %2d-%2d: %d%s", i * CH_SECT, (i + 1) * CH_SECT - 1, by_sect[i], i + 1 < CH_SECT_N ? ", " : "");
        }
        printf("\n");
        CHECK(by_sect[top] > 0, "every section of the origin 3x3 meshed to nothing");
    }

    // The Far Lands wall looked like the most face-dense terrain there
    // could be: a full-height chunk of holes. Its meshes must fit mesh_t
    // with room to spare (F-13), at every level of detail. (Measured: it
    // is the other way round. The tunnels do not change along x, so
    // nearly every face runs the chunk's whole width and the greedy
    // mesher takes it in one rectangle -- a couple of hundred triangles
    // a chunk.)
    if (!s_fail) {
        int32_t const fx = FARLANDS_X_DEFAULT / CH_W - 2;  // two chunks into the wall
        for (int32_t cz = -1; cz <= 1; cz++)
            for (int32_t cx = fx - 1; cx <= fx + 1; cx++) chunk_worker_request_load(cx, cz);
        int most = 0, tris = 0;
        for (int lod = 0; lod < LOD_COUNT; lod++) {
            for (int sect = 0; sect < CH_SECT_N; sect++) {
                mesh_t m;
                CHECK(chunkmesh_build(fx, 0, lod, sect, scratch, &m), "a Far Lands chunk failed to mesh (lod %d, section %d)",
                      lod, sect);
                if (m.vn > most) most = m.vn;
                if (lod == LOD_FANCY) tris += m.tn;
                CHECK(m.vn <= 40000, "a Far Lands section meshed to %d vertices (lod %d), close to mesh_t's 65535 (F-13)",
                      m.vn, lod);
                mesh_free(&m);
            }
        }
        printf("  a Far Lands chunk: %d triangles in full detail, at most %d vertices a section\n", tris, most);
    }
    free(scratch);

    // Edits must survive the round trip through the worker's save path.
    world_set(3, world_ground(3, 3), 3, BLK_GLASS, ST_PLACED);
    CHECK((c->flags & CF_EDITED) != 0, "an edit did not mark the chunk for saving");
    uint8_t const seq_before = c->edit_seq;
    CHECK(chunk_worker_request_save(0, 0), "request_save was refused");
    CHECK((c->flags & CF_EDITED) == 0, "a saved chunk is still marked edited");
    CHECK(c->edit_seq == seq_before, "saving changed the edit sequence");

    chunk_worker_stop();
    chunk_store_shutdown();
}


// --- Every face that should be there, is there ------------------------
//
// WHY THIS EXISTS AND THE AREA TEST DID NOT DO IT. The mesher's own
// harness (tools/meshcheck_assets.h) asserts that the surface area of a
// mesh equals the number of exposed faces -- a TOTAL, over six-cell
// grids built by hand, and only for a lump floating in open air. A
// total cannot tell a face missing here from a face drawn twice there,
// and a hand-built lump is not terrain: it has no water, no light
// boundaries to split a merge on, no section seams and no chunk
// borders. Every one of those is a place a face could go missing, and
// none of them was covered.
//
// The user's report was "some sides of blocks not rendering" (F-129),
// and the flight recorder said nothing had been dropped -- the geometry
// lists peaked a fifth full. So the question "is the face in the mesh
// at all" had to be asked directly, of real generated terrain, through
// the same chunkmesh_build() the badge runs.
//
// THE RULE, RESTATED. Deliberately not voxel_mesh.c's own face_shows():
// a test that calls the code under test agrees with it by construction.
// This is the rule as blocks.h states it in prose.
// `fancy` is the near mesh; the far one draws leaves and glass as solid
// cubes, so they hide their neighbours there and do not here.
static bool face_should_show(uint8_t b, uint8_t n, bool fancy) {
    switch (block_kind(n)) {
        case K_CUBE: return false;   // a full cube hides what is behind it
        case K_SEE:  return fancy && (n != b || (block_def(b)->flags & BF_SEE_SELF) != 0);
        case K_LIQUID: return true;  // a lake is a lid over ground you can see
        default: return true;        // air, a plant, a torch, a fence, a bed
    }
}

// Is `p` inside the triangle, seen down `axis`? The two coordinates
// that are not the axis, and a sign-consistent cross product. The
// tolerance lets a point sitting exactly on the diagonal between a
// quad's two triangles count for both, which a cell centre under a
// merged rectangle can easily do.
static bool tri_covers(vec3_t a, vec3_t b, vec3_t c, int axis, float pu, float pv) {
    int const   iu = (axis + 1) % 3, iv = (axis + 2) % 3;
    float const au = (&a.x)[iu], av = (&a.x)[iv];
    float const bu = (&b.x)[iu], bv = (&b.x)[iv];
    float const cu = (&c.x)[iu], cv = (&c.x)[iv];
    float const d0 = (bu - au) * (pv - av) - (bv - av) * (pu - au);
    float const d1 = (cu - bu) * (pv - bv) - (cv - bv) * (pu - bu);
    float const d2 = (au - cu) * (pv - cv) - (av - cv) * (pu - cu);
    float const e  = 1e-4f;
    return (d0 >= -e && d1 >= -e && d2 >= -e) || (d0 <= e && d1 <= e && d2 <= e);
}

// One chunk, every section, at one level of detail: each face the rule
// says is visible must be covered by a triangle facing that way, on
// that plane. LOD_COARSE is left out on purpose -- it meshes a
// half-resolution grid with skirts, so "the face of this cell" is not
// the question to ask of it.
static int faces_of_chunk(int32_t cx, int32_t cz, int lod, uint8_t* scratch, char const* what) {
    bool const fancy = lod == LOD_FANCY;
    int expected = 0, missing = 0, reported = 0;
    for (int sect = 0; sect < CH_SECT_N; sect++) {
        mesh_t m;
        if (!chunkmesh_build(cx, cz, lod, sect, scratch, &m)) {
            CHECK(false, "%s: chunk (%d,%d) section %d would not mesh", what, cx, cz, sect);
            return 0;
        }
        int const y0 = sect * CH_SECT, y1 = y0 + CH_SECT;
        for (int z = 0; z < CH_D; z++) {
            for (int x = 0; x < CH_W; x++) {
                for (int y = y0; y < y1; y++) {
                    int32_t const       wx = cx * CH_W + x, wz = cz * CH_D + z;
                    uint8_t const       b = world_block(wx, y, wz);
                    block_kind_t const  k = block_kind(b);
                    if (k != K_CUBE && k != K_SEE) continue;
                    static int const NB[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (int f = 0; f < 6; f++) {
                        uint8_t const n = world_block(wx + NB[f][0], y + NB[f][1], wz + NB[f][2]);
                        if (!face_should_show(b, n, fancy)) continue;
                        expected++;
                        int const   axis  = f / 2;
                        bool const  plus  = (f % 2) == 0;
                        uint8_t const dir = (uint8_t)(axis * 2 + (plus ? 0 : 1));
                        // Chunk-local, which is what the mesh is in.
                        float const cell[3] = {(float)x, (float)y, (float)z};
                        float const plane   = cell[axis] + (plus ? 1.0f : 0.0f);
                        float const pu      = cell[(axis + 1) % 3] + 0.5f;
                        float const pv      = cell[(axis + 2) % 3] + 0.5f;
                        bool covered = false;
                        for (int i = 0; i < m.tn && !covered; i++) {
                            if (m.t[i].dir != dir) continue;
                            vec3_t const va = m.v[m.t[i].a];
                            if (fabsf((&va.x)[axis] - plane) > 1e-3f) continue;
                            covered = tri_covers(va, m.v[m.t[i].b], m.v[m.t[i].c], axis, pu, pv);
                        }
                        if (covered) continue;
                        missing++;
                        if (reported < 8) {
                            reported++;
                            CHECK(false, "%s lod %d: the %c%c face of %s at (%d,%d,%d) against %s is in no triangle",
                                  what, lod, "xyz"[axis], plus ? '+' : '-', block_def(b)->name, wx, y, wz,
                                  block_def(n)->name);
                        }
                    }
                }
            }
        }
        mesh_free(&m);
    }
    CHECK(missing == 0, "%s lod %d: %d of %d visible face(s) in chunk (%d,%d) are in no triangle", what, lod, missing,
          expected, cx, cz);
    return expected;
}

// Two seeds: the one the checks already use, and the one out of the
// trace the user sent when they reported the glitch, so the exact
// terrain they were standing in is meshed here as well.
static void check_faces(void) {
    printf("faces\n");
    CHECK(chunk_store_init(), "chunk_store_init failed");
    uint8_t* scratch = malloc(chunkmesh_scratch_bytes());
    CHECK(scratch != NULL, "no scratch for the mesher");
    if (scratch == NULL) return;

    // The seed out of the trace the user sent with F-129, and the chunks
    // they were actually standing in: (-35.6, 27, -0.3) and
    // (-10.7, 25, 5.4), which are chunks (-3,-1) and (-1,0). Terrain
    // nobody chose is the point -- a hand-built fixture has no water, no
    // light boundary and no chunk seam to get wrong.
    static const struct {
        uint32_t    seed;
        int32_t     cx, cz;
        char const* what;
    } CASES[3] = {
        {4242u, 0, 0, "seed 4242 at the origin"},
        {1187696032u, -3, -1, "the reported world, where they stood first"},
        {1187696032u, -1, 0, "the reported world, where they stood looking"},
    };

    for (int i = 0; i < (int)(sizeof(CASES) / sizeof(CASES[0])); i++) {
        world_meta_t   meta;
        player_state_t player;
        char           name[32];
        snprintf(name, sizeof(name), "faces %d", i);
        CHECK(worldstore_init(STORE_BASE), "worldstore_init failed");
        if (!worldstore_create(name, CASES[i].seed, &meta, &player)) {
            CHECK(false, "could not create the face-check world for %s", CASES[i].what);
            continue;
        }
        CHECK(chunk_worker_start(meta.seed), "chunk_worker_start failed");
        // The ring around the ring: a border chunk that is not resident
        // reads as solid, and would hide the very faces this looks for.
        for (int32_t dz = -2; dz <= 2; dz++)
            for (int32_t dx = -2; dx <= 2; dx++) chunk_worker_request_load(CASES[i].cx + dx, CASES[i].cz + dz);
        int faces = 0;
        for (int32_t dz = -1; dz <= 1; dz++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                faces += faces_of_chunk(CASES[i].cx + dx, CASES[i].cz + dz, LOD_FANCY, scratch, CASES[i].what);
                faces += faces_of_chunk(CASES[i].cx + dx, CASES[i].cz + dz, LOD_FAST, scratch, CASES[i].what);
            }
        }
        printf("  %s: 9 chunks, both near levels, %d visible faces, all in the mesh\n", CASES[i].what, faces);
        chunk_worker_stop();
        chunk_store_clear();
    }
    free(scratch);
    chunk_store_shutdown();
}

// --- The player ------------------------------------------------------
//
// Physics, picking and the felling rule are pure, so all three are
// tested here in seconds rather than by walking into things on the
// badge. A hand-built chunk is the test fixture: exact terrain, no
// generation, no seed.

// Claim chunk (0,0) and its ring, fill them with a flat floor at y, and
// hand back the chunk so a test can carve shapes into it.
static chunk_t* flat_world(int floor_y) {
    for (int32_t cz = -1; cz <= 1; cz++) {
        for (int32_t cx = -1; cx <= 1; cx++) {
            chunk_t* c = chunk_claim(cx, cz);
            if (c == NULL) continue;
            memset(c->id, BLK_AIR, CH_CELLS);
            memset(c->st, 0, CH_CELLS);
            for (int z = 0; z < CH_D; z++) {
                for (int x = 0; x < CH_W; x++) {
                    for (int y = 0; y < floor_y; y++) c->id[CH_IDX(x, y, z)] = BLK_STONE;
                }
            }
            c->cstate = CS_READY;
            chunk_resummarise(c);
        }
    }
    return chunk_find(0, 0);
}

static void set_block(int32_t x, int32_t y, int32_t z, uint8_t b, uint8_t st) {
    chunk_t* c = chunk_find(chunk_of(x), chunk_of(z));
    if (c == NULL) return;
    c->id[CH_IDX(chunk_off(x), y, chunk_off(z))] = b;
    c->st[CH_IDX(chunk_off(x), y, chunk_off(z))] = st;
    chunk_resummarise(c);
}

// Light (world/light.h): the floods have to agree with the rule they
// implement, both ways -- light arriving where it should, and going
// away again when its source does. A solid world 20 deep with rooms
// carved into it, lit and changed through world_set like the game does.
static int lsky(int32_t x, int32_t y, int32_t z) {
    return light_sky(world_light(x, y, z));
}
static int lblk(int32_t x, int32_t y, int32_t z) {
    return light_block(world_light(x, y, z));
}

static void check_light(void) {
    printf("light\n");
    CHECK(light_init(), "light_init failed");
    chunk_store_clear();  // flat_world will not reuse a chunk an earlier check edited
    CHECK(flat_world(20) != NULL, "the light world would not become resident");
    for (int32_t cz = -1; cz <= 1; cz++)
        for (int32_t cx = -1; cx <= 1; cx++) light_chunk_ready(chunk_find(cx, cz));

    CHECK(lsky(3, 20, 3) == 15 && lsky(3, 40, 3) == 15, "open air above the ground is not full sky");
    CHECK(lsky(3, 19, 3) == 0, "solid stone carries sky light");

    // A sealed room underground: dark.
    for (int x = 2; x <= 8; x++)
        for (int z = 2; z <= 8; z++)
            for (int y = 10; y <= 12; y++) world_set(x, y, z, BLK_AIR, 0);
    CHECK(lsky(5, 11, 5) == 0 && lblk(5, 11, 5) == 0, "a sealed room is not dark");

    // A torch lights it, one level less per block, and takes it back.
    world_set(5, 10, 5, BLK_TORCH, ST_PLACED);
    printf("  torch: %d at it, %d one away, %d three away, %d round a corner\n", lblk(5, 10, 5), lblk(6, 10, 5),
           lblk(8, 10, 5), lblk(8, 12, 8));
    CHECK(lblk(5, 10, 5) == 14, "a torch's own cell is %d, not 14", lblk(5, 10, 5));
    CHECK(lblk(6, 10, 5) == 13 && lblk(8, 10, 5) == 11, "torchlight does not fall off one level a block");
    CHECK(lblk(8, 12, 8) == 14 - (3 + 2 + 3), "torchlight does not travel by the Manhattan path");
    CHECK(lblk(5, 10, 10) == 0, "torchlight went through solid stone");
    world_set(5, 10, 5, BLK_AIR, 0);
    int left = 0;
    for (int x = 2; x <= 8; x++)
        for (int z = 2; z <= 8; z++)
            for (int y = 10; y <= 12; y++) left += lblk(x, y, z);
    CHECK(left == 0, "removing the torch left %d levels of its light behind", left);

    // A shaft to the surface: daylight falls straight down it undimmed,
    // spreads into the room, and goes when the shaft is capped.
    for (int y = 13; y <= 19; y++) world_set(5, y, 5, BLK_AIR, 0);
    printf("  shaft: %d at its foot, %d beside it, %d in the far corner\n", lsky(5, 10, 5), lsky(6, 10, 5),
           lsky(2, 10, 2));
    CHECK(lsky(5, 12, 5) == 15 && lsky(5, 10, 5) == 15, "daylight does not reach the foot of an open shaft");
    CHECK(lsky(6, 10, 5) == 14, "daylight does not spread from the shaft into the room");
    world_set(5, 19, 5, BLK_STONE, ST_PLACED);
    CHECK(lsky(5, 10, 5) == 0 && lsky(6, 11, 5) == 0 && lsky(5, 18, 5) == 0, "capping the shaft did not darken it");
    world_set(5, 19, 5, BLK_AIR, 0);
    CHECK(lsky(5, 10, 5) == 15, "uncapping the shaft did not bring the daylight back");

    // Leaves and water dim light rather than stop it.
    CHECK(light_filter(BLK_LEAVES) == 1 && light_filter(BLK_WATER) == 2 && light_filter(BLK_GLASS) == 0 &&
              light_filter(BLK_STONE) == 15 && light_filter(BLK_TORCH) == 0,
          "the light filters are not what light.h says");

    // Across a chunk border, and into a chunk that arrives afterwards.
    for (int x = 12; x <= 19; x++) world_set(x, 11, 5, BLK_AIR, 0);  // a tunnel through x = 15|16
    world_set(15, 11, 5, BLK_TORCH, ST_PLACED);
    CHECK(lblk(16, 11, 5) == 13 && lblk(19, 11, 5) == 10, "torchlight stops at the chunk border");
    chunk_t* nb = chunk_find(1, 0);
    memset(nb->lt, 0, CH_CELLS);  // as if (1, 0) had only just arrived
    light_chunk_ready(nb);
    CHECK(lblk(16, 11, 5) == 13 && lblk(19, 11, 5) == 10, "a chunk arriving next to a torch was not lit by it");
    CHECK(lsky(20, 20, 3) == 15, "the newly arrived chunk has no daylight");
    world_set(15, 11, 5, BLK_AIR, 0);
    CHECK(lblk(17, 11, 5) == 0, "removing a torch left its light in the next chunk");
    chunk_store_clear();
}

// Fluids (world/fluid.h) and the scheduler under them
// (world/blockupdate.h). Three things have to be true and only one of
// them is about water:
//
//   * the RULE -- levels count down from a source, a fall is a column,
//     two sources make a third, and a cut-off flow dries up;
//   * the COST -- still water is not in the queue at all, which is the
//     whole reason the scheduler exists;
//   * the SEAM -- a flow interrupted by the edge of the loaded world
//     carries on when the chunk it was heading for arrives. That one is
//     the user's, 2026-09-28: "fluid physics can cross chunk and region
//     boundaries."
static int fl_lvl(int32_t x, int32_t y, int32_t z) {
    return world_block(x, y, z) == BLK_WATER ? (int)fluid_level(world_state(x, y, z)) : -1;
}

static int fl_run(int n) {
    int fired = 0;
    for (int i = 0; i < n; i++) fired += blockupdate_tick();
    return fired;
}

// How many cells of the resident set hold water. The cheapest way to
// say "it all went away".
static int fl_count(void) {
    int n = 0;
    for (int i = 0; i < CH_SLOT_COUNT; i++) {
        chunk_t const* c = chunk_slot_at(i);
        if (c == NULL || c->cstate != CS_READY) continue;
        for (size_t k = 0; k < CH_CELLS; k++)
            if (c->id[k] == BLK_WATER) n++;
    }
    return n;
}

// REAL TERRAIN, ARRIVING. Every check above builds its own world out
// of flat stone, which proves the rules and proves nothing at all about
// whether they can be switched on.
//
// The question this answers is the one that decides it: when a chunk of
// GENERATED world becomes resident, how much of it wants to move? If
// the answer is thousands of cells then every shoreline in the game
// stalls the streamer on arrival and floods the queue, and the whole
// thing has to be rethought. The reasoning said it should be zero --
// worldgen floods every air cell below sea level and refuses to carve a
// cave into the sea, so an ocean generates already settled -- but that
// is an argument, and this is a measurement.
static void check_fluid_worldgen(void) {
    printf("fluids: real terrain on arrival\n");
    // AT THE COAST, not at the origin. The first draft of this check
    // sat on spawn at three seeds and reported 0 water cells three
    // times, which is a pass that proves nothing -- the land round
    // spawn is simply above sea level. Chunk (-25, -15) of GEN_SEED is
    // about 1500 cells of sea with a shoreline through it, which is the
    // case worth asking about.
    static int32_t const AT_X = -25, AT_Z = -15;
    static uint32_t const SEEDS[3] = {GEN_SEED, 0x5EEDu, 0xA11CEu};
    for (int si = 0; si < 3; si++) {
        chunk_store_clear();
        blockupdate_clear();
        blockupdate_stats_reset();

        int wet = 0;
        for (int32_t cz = AT_Z - 2; cz <= AT_Z + 2; cz++) {
            for (int32_t cx = AT_X - 2; cx <= AT_X + 2; cx++) {
                chunk_t* c = chunk_claim(cx, cz);
                CHECK(c != NULL, "the ring would not take a generated chunk");
                if (c == NULL) return;
                worldgen_chunk(c, SEEDS[si], FARLANDS_X_DEFAULT);
                c->cstate = CS_READY;
                chunk_resummarise(c);
            }
        }
        // Joined one at a time, in the order the streamer would, so the
        // seam logic sees the same half-built neighbourhood it will on
        // the badge.
        for (int32_t cz = AT_Z - 2; cz <= AT_Z + 2; cz++)
            for (int32_t cx = AT_X - 2; cx <= AT_X + 2; cx++) blockupdate_chunk_join(chunk_find(cx, cz));
        wet = fl_count();

        blockupdate_stats_t const on_arrival = blockupdate_stats();
        int const                 moved      = fl_run(400);
        int const                 after      = fl_count();
        printf("  seed %08x: %d water cells, %d woken on arrival, %d step(s) run, %d cells after\n",
               (unsigned)SEEDS[si], wet, on_arrival.pending, moved, after);

        // A GENERATED WORLD DOES NOT MOVE. Not "settles quickly" -- does
        // not move at all: every cell of it is a source with sources or
        // rock around it, which is what makes fluids affordable to have.
        CHECK(on_arrival.dropped == 0, "seed %08x overflowed the queue on arrival", (unsigned)SEEDS[si]);
        CHECK(after == wet, "seed %08x: %d water cells became %d without anybody touching them",
              (unsigned)SEEDS[si], wet, after);
        CHECK(blockupdate_stats().pending == 0, "seed %08x: generated terrain never went quiet",
              (unsigned)SEEDS[si]);
    }
    chunk_store_clear();
    blockupdate_clear();
}

static void check_fluid(void) {
    printf("fluids\n");
    CHECK(blockupdate_init(), "blockupdate_init failed");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the fluid world would not become resident");

    // --- A source on the floor spreads, and thins as it goes ---------
    world_set(5, 20, 5, BLK_WATER, ST_PLACED);
    fl_run(200);
    printf("  a source on flat ground reached %d cells\n", fl_count());
    CHECK(fl_lvl(5, 20, 5) == 0, "the source did not stay a source (level %d)", fl_lvl(5, 20, 5));
    for (int d = 1; d <= 7; d++) {
        CHECK(fl_lvl(5 + d, 20, 5) == d, "water %d east of the source is level %d, not %d", d,
              fl_lvl(5 + d, 20, 5), d);
    }
    CHECK(fl_lvl(13, 20, 5) == -1, "water reached 8 blocks, past its reach of 7");
    // Manhattan, not a square: the corner at (6, 6) is two steps away.
    CHECK(fl_lvl(6, 20, 6) == 2, "water spread diagonally at the wrong level (%d)", fl_lvl(6, 20, 6));
    CHECK(fl_lvl(5, 21, 5) == -1, "water climbed");

    // --- AND IT COSTS NOTHING ONCE IT HAS SETTLED --------------------
    //
    // The claim the whole design rests on. If this ever fails, still
    // water is being walked every tick and the queue is doing nothing
    // but burn frames.
    blockupdate_stats_t st = blockupdate_stats();
    printf("  settled: %d cells waiting, %d dropped, peak %d\n", st.pending, st.dropped, st.peak);
    CHECK(st.pending == 0, "settled water left %d cells in the queue", st.pending);
    CHECK(st.dropped == 0, "the update queue overflowed (%d dropped)", st.dropped);
    CHECK(fl_run(20) == 0, "a settled pool still had work to do");
    CHECK(!blockupdate_active(6, 20, 5), "a settled cell is still marked active");

    // --- Take the source away and it all drains ----------------------
    world_set(5, 20, 5, BLK_AIR, 0);
    fl_run(600);
    CHECK(fl_count() == 0, "%d cells of water outlived their source", fl_count());
    CHECK(blockupdate_stats().pending == 0, "draining left work behind");

    // --- A fall is a column, and spreads only where it lands ---------
    world_set(5, 30, 5, BLK_WATER, ST_PLACED);
    fl_run(400);
    CHECK(fl_lvl(5, 25, 5) == 0, "the falling column is not full at y=25");
    CHECK(fl_lvl(6, 25, 5) == -1, "a fall sprayed sideways on the way down");
    CHECK(fl_lvl(6, 20, 5) == 1, "the foot of the fall did not spread at level 1 (%d)", fl_lvl(6, 20, 5));
    CHECK(fl_lvl(12, 20, 5) == 7, "the pool at the foot of the fall is too small");
    world_set(5, 30, 5, BLK_AIR, 0);
    fl_run(600);
    CHECK(fl_count() == 0, "the waterfall did not drain (%d cells left)", fl_count());

    // --- Two sources make a third (the 2x2 hole) ---------------------
    for (int x = 4; x <= 5; x++)
        for (int z = 4; z <= 5; z++) world_set(x, 19, z, BLK_AIR, 0);  // dig a 2x2 pit
    world_set(4, 19, 4, BLK_WATER, ST_PLACED);
    world_set(5, 19, 5, BLK_WATER, ST_PLACED);
    fl_run(200);
    CHECK(fl_lvl(4, 19, 5) == 0 && fl_lvl(5, 19, 4) == 0, "two sources in a 2x2 pit did not fill the other corners");
    CHECK(fluid_is_source(world_state(4, 19, 5)), "the filled corner is a flow, not a source");
    // ... and taking one out again leaves the spring standing, which is
    // what makes it infinite rather than merely full.
    CHECK(fluid_take_source(4, 19, 4) == BLK_WATER, "a bucket could not take one of the four sources");
    fl_run(200);
    CHECK(fl_lvl(4, 19, 4) == 0, "the 2x2 spring did not refill itself");
    for (int x = 4; x <= 5; x++)
        for (int z = 4; z <= 5; z++) world_set(x, 19, z, BLK_STONE, 0);
    fl_run(400);

    // --- A BUCKET TAKES SOURCES AND NOT FLOWS ------------------------
    world_set(5, 20, 5, BLK_WATER, ST_PLACED);
    fl_run(200);
    CHECK(fluid_take_source(7, 20, 5) == BLK_AIR, "a bucket scooped a flowing cell");
    CHECK(fluid_take_source(5, 20, 5) == BLK_WATER, "a bucket could not take a source");
    CHECK(world_block(5, 20, 5) == BLK_AIR, "taking a source left something behind");
    fl_run(600);
    CHECK(fl_count() == 0, "the pool survived its source going into a bucket");
    CHECK(item_bucket_filled_with(BLK_WATER) == ITEM_BUCKET_WATER, "the water bucket is not in the bucket table");
    CHECK(item_bucket_contents(ITEM_BUCKET) == BLK_AIR, "an empty bucket is not empty");

    // --- THE SEAM (the user, 2026-09-28) -----------------------------
    //
    // Water poured next to the edge of the loaded world stops at the
    // edge, as it must -- a missing chunk reads as BLK_BARRIER and that
    // is a wall. The test is what happens NEXT: the chunk arrives, and
    // the flow has to pick up where it left off rather than stand there
    // as a cliff of water for ever.
    chunk_t* east = chunk_find(1, 0);
    CHECK(east != NULL, "the east chunk is not resident");
    east->cstate = CS_FREE;  // as if it had been streamed out
    CHECK(world_block(16, 20, 5) == BLK_BARRIER, "an absent chunk does not read as barrier");

    world_set(14, 20, 5, BLK_WATER, ST_PLACED);
    fl_run(300);
    CHECK(fl_lvl(15, 20, 5) == 1, "water did not reach the last cell of the loaded world");
    CHECK(fl_count() > 0, "the water by the border vanished");
    int const before = fl_count();

    // It arrives. Nothing has changed in chunk 0, so ONLY the join can
    // start this up again.
    east->cstate = CS_READY;
    memset(east->act, 0, CH_ACT_BYTES);
    CHECK(blockupdate_stats().pending == 0, "the border flow was still busy before the chunk arrived");
    blockupdate_chunk_join(east);
    CHECK(blockupdate_stats().pending > 0, "a chunk arriving beside a flow woke nothing");
    fl_run(400);
    printf("  across the seam: %d cells before the chunk arrived, %d after\n", before, fl_count());
    CHECK(fl_lvl(16, 20, 5) == 2, "water did not cross the chunk border (level %d)", fl_lvl(16, 20, 5));
    CHECK(fl_lvl(21, 20, 5) == 7, "water stopped short of its reach across the border");
    CHECK(fl_lvl(22, 20, 5) == -1, "water ran past its reach across the border");

    // A NEIGHBOUR GOING AWAY MUST NOT DELETE THE FLOW. The missing
    // chunk is an unknown, not a "nothing is feeding me" (fluid.c).
    east->cstate = CS_FREE;
    world_set(14, 21, 5, BLK_STONE, ST_PLACED);  // poke it: wake the border cells
    world_set(14, 21, 5, BLK_AIR, 0);
    fl_run(300);
    CHECK(fl_lvl(15, 20, 5) == 1, "the flow at the border dried up when the next chunk left");
    east->cstate = CS_READY;

    world_set(14, 20, 5, BLK_AIR, 0);
    fl_run(800);
    CHECK(fl_count() == 0, "%d cells survived across the seam", fl_count());

    // --- AN OCEAN ARRIVING PUTS NOTHING IN THE QUEUE -----------------
    //
    // The other half of the cost claim, and the one that decides
    // whether this can be switched on at all: a chunk of generated sea
    // is thousands of water cells, and if each of them wanted an update
    // on arrival the queue would overflow on the first shoreline.
    // ALL NINE CHUNKS, not one. A single flooded chunk in a dry world
    // is a column of water with a cliff of air round it, and that
    // genuinely does have somewhere to go -- the first draft of this
    // check built exactly that and then complained the queue was not
    // empty. An ocean is a thing with no shore in sight.
    for (int32_t cz = -1; cz <= 1; cz++) {
        for (int32_t cx = -1; cx <= 1; cx++) {
            chunk_t* c = chunk_find(cx, cz);
            for (int z = 0; z < CH_D; z++)
                for (int x = 0; x < CH_W; x++)
                    for (int y = 20; y <= CH_SEA_LEVEL; y++) c->id[CH_IDX(x, y, z)] = BLK_WATER;
            chunk_resummarise(c);
        }
    }
    chunk_t* sea = chunk_find(0, 0);
    blockupdate_clear();
    blockupdate_chunk_join(sea);
    blockupdate_stats_t const ocean = blockupdate_stats();
    printf("  a full sea chunk (%d water cells) woke %d of them\n", fl_count(), ocean.pending);
    CHECK(ocean.pending == 0, "an ocean chunk put %d cells in the queue", ocean.pending);
    CHECK(fl_run(40) == 0, "an ocean chunk had work to do");

    blockupdate_stats_reset();
    chunk_store_clear();
    blockupdate_clear();
}

// A replay is a start and a stream of per-tick inputs; it has to come
// back from the card exactly, gyro turns included, or it replays some
// other walk.
// ---------------------------------------------------------------------
//  The animals, their two machines, and the fence (step 10)
//
//  The claims worth defending:
//
//    * a fence is a block and a half tall, so nothing walks over one
//      and nothing jumps it -- which is the whole reason it exists;
//    * an open gate is a hole in that fence and a shut one is not;
//    * a creature saved with its chunk comes back the same creature,
//      tame, sitting, half-grown and all (D-33, D-30);
//    * a bucket on a cow gives milk, a bone on a wild dog gives a dog,
//      and neither works on the wrong animal;
//    * feeding two adults makes one calf and not a herd;
//    * a calf drops nothing, so breeding is not a meat machine;
//    * the two makers turn time into things at the rates the user gave,
//      bank nothing while idle, and hand the bucket back at once;
//    * everything random here comes from the world's hash, so the same
//      world always puts the same herd in the same field (Part T).
// ---------------------------------------------------------------------

// Run the creatures for `n` ticks with the player standing at (px, pz)
// holding `held` -- which is all an animal knows about anybody.
static void mob_run(uint32_t* clock, int n, double px, double pz, uint16_t held) {
    // A player standing well out of the way, so a herd is not being
    // shoved about while the test is watching it (mob.h, MOB_PUSH).
    phys_body_t you;
    phys_body_init(&you, px, 20.0, pz);
    for (int i = 0; i < n; i++) mob_tick((*clock)++, &you, held);
}

static int mob_count_kind(uint8_t kind) {
    int n = 0;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = mob_at(i);
        if (m != NULL && m->alive && m->kind == kind) n++;
    }
    return n;
}

// ---------------------------------------------------------------------
//  The texture budget (F-120)
//
//  The bug that cost a round of work: the cache held 48 entries, the
//  game wanted 64 materials plus every item icon, and everything past
//  the end loaded as a flat average colour. Nothing crashed. The badge
//  logged "cache full" about twenty times a boot and what it looked
//  like from the outside was bad art -- crops that were coloured
//  rectangles, and a transparent-water setting that would not turn on.
//
//  Two things stop it now, and this is the second of them:
//
//    * chunk_render.c asserts VM_COUNT + TEX_BY_NAME <= TEXCACHE_MAX at
//      COMPILE TIME, and TEX_BY_NAME is derived from the item table, so
//      adding an item moves the requirement by itself;
//    * this counts what the game will REALLY ask for by name -- an icon
//      per non-block item, one per block drawn as a thing rather than a
//      cube, and the few files that belong to nothing -- and fails if
//      those ever outgrow the slack in TEX_BY_NAME.
//
//  Between them, the estimate cannot rot and the cache cannot be too
//  small: one of the two fails the build first.
static void check_texture_budget(void) {
    printf("textures: what the cache has to hold\n");

    // An item that is not a block draws from item_<name>.png (hud.c).
    int icons = 0;
    for (uint16_t id = BLK_COUNT; id < ITEM_COUNT; id++) icons++;

    // ... and so does a block that is not drawn as a cube.
    int block_icons = 0;
    for (uint16_t id = 1; id < BLK_COUNT; id++) {
        if (block_has_item_icon((uint8_t)id)) block_icons++;
    }

    // The ones that belong to nothing: water_blend.png, three torch
    // frames, the flame, and Fred's face.
    int const loose = 6;

    int const want = icons + block_icons + loose;
    printf("  %d materials + %d by name (%d item icons, %d block icons, %d loose) = %d\n", VM_COUNT, want, icons,
           block_icons, loose, VM_COUNT + want);
    CHECK(want <= TEX_BY_NAME, "the game asks for %d textures by name and TEX_BY_NAME allows %d", want,
          TEX_BY_NAME);
    // How much room is left before the compile-time assert fires. Not a
    // failure -- it is the number worth seeing in the log of the round
    // that finally uses it up.
    printf("  TEX_BY_NAME allows %d, so %d to spare before the assert has to move\n", TEX_BY_NAME,
           TEX_BY_NAME - want);

    // AND DOES EACH ONE EXIST?
    //
    // Counting them was never the question. The bed shipped with a
    // BLACK SQUARE in the inventory for a week (F-128): it carries
    // BF2_ITEM_ICON, so hud.c asked for `item_bed_foot.png` after the
    // block's own name, and the file drawn for it was called
    // `item_bed.png`. Nothing said so -- a texture that will not load
    // falls back to the flat colour, and the flat colour for a block
    // with no row in BLOCK_ARGB is zero, which is black.
    //
    // So the rule is checked instead of counted: every file the game
    // WILL ask for is opened here, from the same two facts hud.c uses
    // (item_def(id).name and block_has_item_icon). This runs in the
    // repo, where the PNGs are, so it costs one fopen each.
    int missing = 0;
    for (uint16_t id = 1; id < ITEM_COUNT; id++) {
        if (id < BLK_COUNT && !block_has_item_icon((uint8_t)id)) continue;  // drawn as its own side
        if (id == BLK_BARRIER) continue;                                    // never carried
        char path[128];
        snprintf(path, sizeof(path), "textures/item_%s.png", item_def(id).name);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            continue;
        }
        missing++;
        CHECK(false, "%s asks for %s, which does not exist", item_def(id).name, path);
    }
    printf("  %d icon file(s) asked for by name, %d missing\n", icons + block_icons, missing);

    // ... and the colour under it, which is what shows when a texture
    // will not load. Zero is not a colour: it is the hole the bed fell
    // through.
    for (uint16_t id = 1; id < BLK_COUNT; id++) {
        if (id == BLK_AIR || id == BLK_BARRIER) continue;
        if (block_def((uint8_t)id)->name[0] == '\0') continue;  // an id with no block on it yet
        CHECK((item_def(id).argb >> 24) != 0, "block %s has no fallback colour: it draws as black",
              block_def((uint8_t)id)->name);
    }
}

// A 6 x 6 pen of fence, with whatever `extra` puts inside it, and a cow
// left in it for `ticks`. Returns true if it was still in there.
//
// THIS IS THE CHECK THE FIRST ROUND NEEDED AND DID NOT HAVE. It ran a
// PIG round a bare pen for two minutes, which is the one case that
// always worked; the user built a pen with something in it and the cows
// walked out over the fence (F-122). Every shape below is a shape
// somebody's farm actually has.
typedef enum { PEN_BARE = 0, PEN_BLOCK, PEN_GATE_SHUT, PEN_GATE_OPEN, PEN_STAIRS } pen_kind_t;

static bool pen_holds(pen_kind_t kind, int ticks, double* high_out) {
    chunk_store_clear();
    blockupdate_clear();
    flat_world(20);
    mob_reset();
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    for (int i = 4; i <= 9; i++) {
        set_block(i, 20, 4, BLK_FENCE, ST_PLACED);
        set_block(i, 20, 9, BLK_FENCE, ST_PLACED);
        set_block(4, 20, i, BLK_FENCE, ST_PLACED);
        set_block(9, 20, i, BLK_FENCE, ST_PLACED);
    }
    // A BLOCK TO STAND ON, which is what a real pen has -- a tuft of
    // terrain, a plot of soil, a chest -- and what let the cows out.
    if (kind == PEN_BLOCK) set_block(6, 20, 5, BLK_DIRT, ST_PLACED);
    if (kind == PEN_STAIRS) {
        set_block(6, 20, 5, BLK_DIRT, ST_PLACED);
        set_block(6, 21, 5, BLK_DIRT, ST_PLACED);  // deliberately a way out
    }
    if (kind == PEN_GATE_SHUT) set_block(6, 20, 4, BLK_FENCE_GATE, ST_PLACED);
    if (kind == PEN_GATE_OPEN) set_block(6, 20, 4, BLK_FENCE_GATE_OPEN, ST_PLACED);

    int const c = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, false);
    if (c < 0) return false;
    uint32_t    clk  = 1000;
    double      high = 0.0;
    bool        out  = false;
    phys_body_t watcher;
    phys_body_init(&watcher, 60.0, 20.0, 60.0);  // nobody near the pen
    for (int t = 0; t < ticks && !out; t++) {
        mob_tick(clk++, &watcher, 0);
        mob_t const* m = mob_at(c);
        if (m->body.y > high) high = m->body.y;
        out = m->body.x < 4.0 || m->body.x > 10.0 || m->body.z < 4.0 || m->body.z > 10.0;
    }
    if (high_out != NULL) *high_out = high;
    return !out;
}

// ---------------------------------------------------------------------
//  Nobody shares a space (F-123)
//
//  The user, after the fences: "cows phase through each other and the
//  player." They did: the collider knows about the world and nothing
//  else, which is right for a collider and wrong for a field of cows.
//
//  What is checked here is what a soft push has to get right:
//
//    * two bodies in the same place come apart, and do not oscillate;
//    * they stop as soon as they are clear, rather than drifting;
//    * the world still wins -- a push cannot force a body through a
//      wall or out of a pen;
//    * a sitting dog holds its ground, which is the whole point of
//      telling one to sit.
// ---------------------------------------------------------------------
static double flat_dist(phys_body_t const* a, phys_body_t const* b) {
    double const dx = a->x - b->x, dz = a->z - b->z;
    return sqrt(dx * dx + dz * dz);
}

// ---------------------------------------------------------------------
//  Breeding, from across a field (F-124)
//
//  The user bred cows and then fed "a load of potatoes" to pigs with
//  nothing to show for it. The mechanism was never broken -- two fed
//  pigs standing together make a piglet, and the check said so. What
//  was broken is everything around it:
//
//    * two fed animals had to WANDER within two and a half blocks of
//      each other before the mood wore off in thirty seconds, which is
//      a coincidence, not a mechanic. They now walk to each other;
//    * a fed animal and an unfed one looked exactly the same, so
//      feeding the same pig twice was indistinguishable from feeding
//      two of them. The crosshair says which;
//    * and a pig is 0.9 blocks tall against a cow's 1.4, with the eye
//      at 1.62 -- so the crosshair passes over a pig's back at two
//      paces unless you look twenty degrees down.
// ---------------------------------------------------------------------
static void check_breeding(void) {
    printf("breeding: two fed animals find each other (F-124)\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the breeding world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);

    phys_body_t watcher;
    phys_body_init(&watcher, 14.0, 20.0, 14.0);  // nobody anywhere near them
    uint32_t clk = 4000;

    // --- ACROSS A FIELD, not standing together --------------------------
    for (int kind = MOB_PIG; kind <= MOB_COW; kind++) {
        mob_reset();
        int const a = mob_spawn((uint8_t)kind, 3.5, 20.0, 6.5, false);
        int const b = mob_spawn((uint8_t)kind, 11.5, 20.0, 6.5, false);
        CHECK(a >= 0 && b >= 0, "the pool would not take two of %s", mob_def((uint8_t)kind)->name);
        uint16_t const food = mob_def((uint8_t)kind)->feed[0];
        CHECK(mob_use(a, food).what == MOB_USE_FED, "%s would not take %s", mob_def((uint8_t)kind)->name,
              item_def(food).name);
        CHECK(mob_use(b, food).what == MOB_USE_FED, "the second %s would not be fed", mob_def((uint8_t)kind)->name);

        int born = 0;
        for (int t = 0; t < (int)MOB_LOVE_TICKS && born == 0; t++) {
            mob_tick(clk++, &watcher, 0);
            int n = 0;
            for (int i = 0; i < MOB_MAX; i++) n += mob_at(i)->alive ? 1 : 0;
            if (n > 2) born = t;
        }
        printf("  two %ss eight blocks apart, both fed: %s\n", mob_def((uint8_t)kind)->name,
               born ? "bred" : "NEVER MET");
        CHECK(born > 0, "two fed %ss eight blocks apart never found each other in %u ticks",
              mob_def((uint8_t)kind)->name, MOB_LOVE_TICKS);
    }

    // --- A pig eats a potato; a cow does not ----------------------------
    mob_reset();
    int const pig = mob_spawn(MOB_PIG, 6.5, 20.0, 6.5, false);
    int const pig_b = mob_spawn(MOB_PIG, 7.5, 20.0, 6.5, false);  // a pig of its own: one feed each
    int const cow = mob_spawn(MOB_COW, 8.5, 20.0, 6.5, false);
    CHECK(mob_use(pig, ITEM_POTATO).what == MOB_USE_FED, "a pig would not eat a potato");
    CHECK(mob_use(pig_b, ITEM_BEANS).what == MOB_USE_FED, "a pig would not eat beans");
    CHECK(mob_use(cow, ITEM_POTATO).what == MOB_USE_NOTHING, "a cow ate a potato");
    CHECK(mob_use(cow, ITEM_WHEAT).what == MOB_USE_FED, "a cow would not eat wheat");

    // FEEDING THE SAME ONE TWICE IS NOT FEEDING TWO. It is the mistake
    // the crosshair line exists to make visible, so it had better be a
    // mistake the code agrees about.
    mob_reset();
    int const p1 = mob_spawn(MOB_PIG, 6.5, 20.0, 6.5, false);
    int const p2 = mob_spawn(MOB_PIG, 7.5, 20.0, 6.5, false);
    int       eaten = 0;
    for (int i = 0; i < 8; i++) {
        mob_use_result_t const f = mob_use(p1, ITEM_POTATO);
        if (f.consume) eaten++;
    }
    // ONLY THE FIRST ONE IS EATEN. Seven potatoes offered to a pig that
    // is already looking for a partner stay in the player's hand, which
    // is the user's rule and the thing that made a whole stack vanish.
    CHECK(eaten == 1, "eight potatoes offered to one pig, %d eaten", eaten);
    for (int t = 0; t < 200; t++) mob_tick(clk++, &watcher, 0);
    int n = 0;
    for (int i = 0; i < MOB_MAX; i++) n += mob_at(i)->alive ? 1 : 0;
    CHECK(n == 2, "eight potatoes into one pig made %d pigs", n);
    CHECK(mob_at(p1)->love > 0 && mob_at(p2)->love == 0, "the wrong pig is in the mood");

    // A RESTING ONE IS NOT INTERESTED EITHER, and neither of them comes
    // running when the player waves more food about.
    mob_reset();
    int const r1 = mob_spawn(MOB_PIG, 6.5, 20.0, 6.5, false);
    mob_at_mut(r1)->breed_cd = 500;
    CHECK(mob_use(r1, ITEM_POTATO).what == MOB_USE_BUSY, "a resting pig ate a potato");
    CHECK(!mob_use(r1, ITEM_POTATO).consume, "a resting pig took the potato anyway");
    {
        phys_body_t near_by;
        phys_body_init(&near_by, 9.5, 20.0, 6.5);  // three blocks off, holding potatoes
        double const was = mob_at(r1)->body.x;
        for (int t = 0; t < 100; t++) mob_tick(clk++, &near_by, ITEM_POTATO);
        // It may still amble -- what it must not do is come running.
        printf("  a resting pig drifted %.2f blocks while a player waved potatoes three away\n",
               mob_at(r1)->body.x - was);
        CHECK(mob_at(r1)->intent != MOB_FOLLOW, "a resting pig followed the food");
        CHECK(fabs(mob_at(r1)->body.x - was) < 2.5, "a resting pig went to the player anyway");
    }

    // --- How far down you have to look at a pig -------------------------
    //
    // Reported rather than asserted at a number: what matters is that
    // it is a shallow glance and not a stare at your own boots.
    mob_reset();
    int const target = mob_spawn(MOB_PIG, 8.5, 20.0, 6.5, false);
    CHECK(target >= 0, "the pool would not take the pig");
    int    first = 99;
    for (int deg = 0; deg <= 40 && first == 99; deg += 2) {
        float dx, dy, dz;
        ray_forward(1.5708f, (float)deg * 3.14159f / 180.0f, &dx, &dy, &dz);
        if (mob_pick(6.5, 20.0 + (double)PHYS_PLAYER_EYE, 6.5, dx, dy, dz, RAY_REACH, NULL) >= 0) first = deg;
    }
    printf("  a pig two blocks off is under the crosshair from %d degrees down (a cow: ", first);
    mob_reset();
    mob_spawn(MOB_COW, 8.5, 20.0, 6.5, false);
    int cow_first = 99;
    for (int deg = 0; deg <= 40 && cow_first == 99; deg += 2) {
        float dx, dy, dz;
        ray_forward(1.5708f, (float)deg * 3.14159f / 180.0f, &dx, &dy, &dz);
        if (mob_pick(6.5, 20.0 + (double)PHYS_PLAYER_EYE, 6.5, dx, dy, dz, RAY_REACH, NULL) >= 0) cow_first = deg;
    }
    printf("%d)\n", cow_first);
    CHECK(first <= 16, "a pig needs %d degrees of looking down to point at", first);
    mob_reset();
}

static void check_shoving(void) {
    printf("shoving: two bodies do not share a space (F-123)\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the shoving world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    mob_reset();

    phys_body_t watcher;
    phys_body_init(&watcher, 12.0, 20.0, 12.0);
    uint32_t clk = 2000;

    // --- Two cows in exactly the same place ---------------------------
    int const a = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, false);
    int const b = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, false);
    CHECK(a >= 0 && b >= 0, "the pool would not take two cows");
    // Standing still: intent is what the tick picks, so pin them and
    // watch only the push.
    for (int t = 0; t < 120; t++) {
        mob_at_mut(a)->intent = mob_at_mut(b)->intent = MOB_STAND;
        mob_at_mut(a)->intent_for = mob_at_mut(b)->intent_for = 10000;
        mob_tick(clk++, &watcher, 0);
    }
    double const apart = flat_dist(&mob_at(a)->body, &mob_at(b)->body);
    double const want  = (double)mob_def(MOB_COW)->w;
    printf("  two cows spawned on the same spot stood %.2f apart (they are %.2f wide)\n", apart, want);
    CHECK(apart > want * 0.9, "two cows in the same place stayed there (%.3f apart)", apart);
    // AND THEY STOP. A push that kept pushing would walk a herd off the
    // edge of the world over an afternoon.
    for (int t = 0; t < 400; t++) {
        mob_at_mut(a)->intent = mob_at_mut(b)->intent = MOB_STAND;
        mob_at_mut(a)->intent_for = mob_at_mut(b)->intent_for = 10000;
        mob_tick(clk++, &watcher, 0);
    }
    double const later = flat_dist(&mob_at(a)->body, &mob_at(b)->body);
    CHECK(later < apart + 0.35, "the push kept pushing: %.2f apart, then %.2f", apart, later);

    // --- A cow walks into the player ----------------------------------
    mob_reset();
    phys_body_init(&watcher, 6.5, 20.0, 6.5);
    int const c = mob_spawn(MOB_COW, 6.9, 20.0, 6.5, false);
    CHECK(c >= 0, "the pool would not take the cow");
    double const px0 = watcher.x;
    for (int t = 0; t < 60; t++) {
        mob_at_mut(c)->intent     = MOB_STAND;
        mob_at_mut(c)->intent_for = 10000;
        mob_tick(clk++, &watcher, 0);
    }
    printf("  a cow standing in the player moved them %.2f blocks and itself %.2f\n", fabs(watcher.x - px0),
           fabs(mob_at(c)->body.x - 6.9));
    CHECK(flat_dist(&mob_at(c)->body, &watcher) > (double)mob_def(MOB_COW)->w * 0.8,
          "a cow and the player ended up in the same place");
    CHECK(fabs(watcher.x - px0) > 0.01, "the player was not moved at all");
    CHECK(fabs(mob_at(c)->body.x - 6.9) > fabs(watcher.x - px0),
          "the player was shoved further than the cow");

    // --- The world still wins -----------------------------------------
    //
    // Three cows in a two-block slot with a wall at one end: the push
    // must not squeeze any of them through it.
    mob_reset();
    phys_body_init(&watcher, 12.0, 20.0, 12.0);
    for (int y = 20; y <= 21; y++) {
        for (int z = 4; z <= 8; z++) set_block(3, y, z, BLK_STONE, ST_PLACED);
    }
    for (int i = 0; i < 3; i++) mob_spawn(MOB_COW, 4.5, 20.0, 6.0 + 0.1 * i, false);
    for (int t = 0; t < 600; t++) mob_tick(clk++, &watcher, 0);
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = mob_at(i);
        if (!m->alive) continue;
        CHECK(m->body.x > 4.0, "a cow was pushed through a wall (x %.2f)", m->body.x);
    }

    // --- A sitting dog is furniture -----------------------------------
    mob_reset();
    int const dog = mob_spawn(MOB_DOG, 6.5, 20.0, 6.5, false);
    mob_at_mut(dog)->tame    = true;
    mob_at_mut(dog)->sitting = true;
    double const dx0 = mob_at(dog)->body.x, dz0 = mob_at(dog)->body.z;
    phys_body_init(&watcher, 6.6, 20.0, 6.5);  // the player standing in it
    for (int t = 0; t < 60; t++) mob_tick(clk++, &watcher, 0);
    CHECK(fabs(mob_at(dog)->body.x - dx0) < 0.01 && fabs(mob_at(dog)->body.z - dz0) < 0.01,
          "a sitting dog was shoved (moved %.3f, %.3f)", mob_at(dog)->body.x - dx0, mob_at(dog)->body.z - dz0);
    CHECK(flat_dist(&mob_at(dog)->body, &watcher) > 0.3, "the player stood inside a sitting dog");
    printf("  a sitting dog held its ground and the player went round it\n");
    mob_reset();
}

// ---------------------------------------------------------------------
//  Sheep, wool and the bed (step 10, round two)
//
//  The sheep is the first animal worth KEEPING rather than killing --
//  everything else gives what it gives once -- so the claims are about
//  the fleece and what it becomes:
//
//    * shears take a fleece and the sheep lives; it gives nothing more
//      until the coat is back, and it looks shorn in the meantime;
//    * a fleece survives a save, coat clock and all, or a flock read
//      back off the card is an infinite supply of wool;
//    * ONE FLEECE IS THREE STRINGS, which is where string comes from in
//      this game -- an open question since the fishing rod was designed
//      (the user: "strings will not come from spiders");
//    * a bed is TWO CELLS and behaves like one thing: placed together,
//      broken together, and only the foot pays out.
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
//  Fishing (step 12)
//
//  The claims are the user's own design, and two of them are the whole
//  reason this step is not Minecraft's:
//
//    * ONE WORM PER CAST, not per catch. A cast that brings nothing up
//      still costs one -- that is what makes the composter the throttle
//      on fishing rather than the rod;
//    * A MISSED BITE IS NOT A LOST WORM. The worm buys the cast, so the
//      line stays out and another fish comes along;
//    * the rod does not wear out (the user's call);
//    * and everything random -- the wait, the bite, the fish -- is a
//      hash of where the float landed and when, so a replay fishes the
//      same river the same way (Part T).
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
//  The population, and the water (F-125)
//
//  Four things the user found by playing, and every one of them was
//  invisible from inside the code:
//
//    * the pool held 48 and a FAR-VIEW RING HOLDS 225 CHUNKS. It filled
//      after 43 chunks of real terrain and then the world stopped
//      spawning anything, anywhere, for ever. What that looks like is
//      "a few animals at spawn and then a long empty walk";
//    * the rarest creature therefore never appeared at all: a rare roll
//      that comes up against a full pool is a roll that never happened;
//    * ... and dogs were rare in the wrong PLACE. This world is four
//      fifths plains and they lived only in the woods;
//    * animals walked into water and SANK, because they had a swim
//      stroke with full gravity under it. From the shore, a herd on the
//      sea bed reads as animals spawning in the ocean.
//
//  So the checks below are about a LANDSCAPE rather than a pen: how
//  many animals a real ring of real terrain holds, whether the pool can
//  take them, and what happens to one that gets its feet wet.
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
//  Caves that reach daylight (F-126)
//
//  The user, after days of walking: "I never saw a cave entrance on the
//  surface." There were none. `cave_mouth` tested a field against 0.84
//  and that field never exceeds 0.642, so it was false in every column
//  of every world this game has ever generated.
//
//  THE OLD CHECK PRINTED A NUMBER AND BELIEVED IT. It counted columns
//  whose surface cell is air -- which is nearly the definition of a
//  surface cell -- and called them cave mouths. A measure that cannot
//  distinguish a hillside from a hole cannot fail when the holes stop.
//
//  So this one floods the SKY INTO THE GROUND and asks how far it gets.
//  That is the player's question: can I see a way in, and does it go
//  anywhere. It fails if the answer is no, and it fails if the ground
//  turns into a colander, because both are worlds nobody wants.
static void check_cave_mouths(void) {
    printf("caves: does the sky get into the ground\n");
    uint32_t const seed = 20260929u;

    chunk_t* c = (chunk_t*)malloc(sizeof(chunk_t));
    CHECK(c != NULL, "no room for a scratch chunk");
    if (c == NULL) return;
    memset(c, 0, sizeof(*c));
    c->id = (uint8_t*)malloc(CH_CELLS);
    c->st = (uint8_t*)malloc(CH_CELLS);
    CHECK(c->id != NULL && c->st != NULL, "no room for a scratch chunk's planes");
    if (c->id == NULL || c->st == NULL) return;

    static uint8_t seen[CH_W][CH_H][CH_D];
    static int16_t stack[CH_W * CH_H * CH_D][3];

    long columns = 0, carved = 0, deep3 = 0, deep6 = 0;
    int  best = 0, chunks_with = 0, chunks = 0;

    for (int32_t cz = -16; cz < 16; cz++) {
        for (int32_t cx = -16; cx < 16; cx++) {
            c->cx = cx;
            c->cz = cz;
            worldgen_chunk(c, seed, FARLANDS_NONE);
            memset(seen, 0, sizeof(seen));
            chunks++;

            // Flood down from the top plane, through air only: water
            // and stone both stop it, which is what a player can see
            // into as well.
            int sp = 0;
            for (int lz = 0; lz < CH_D; lz++) {
                for (int lx = 0; lx < CH_W; lx++) {
                    if (c->id[CH_IDX(lx, CH_H - 1, lz)] != BLK_AIR) continue;
                    seen[lx][CH_H - 1][lz] = 1;
                    stack[sp][0] = (int16_t)lx;
                    stack[sp][1] = (int16_t)(CH_H - 1);
                    stack[sp][2] = (int16_t)lz;
                    sp++;
                }
            }
            while (sp > 0) {
                sp--;
                int const x = stack[sp][0], y = stack[sp][1], z = stack[sp][2];
                int const dx[6] = {1, -1, 0, 0, 0, 0}, dy[6] = {0, 0, 1, -1, 0, 0}, dz[6] = {0, 0, 0, 0, 1, -1};
                for (int k = 0; k < 6; k++) {
                    int const nx = x + dx[k], ny = y + dy[k], nz = z + dz[k];
                    if (nx < 0 || nx >= CH_W || nz < 0 || nz >= CH_D || ny < 0 || ny >= CH_H) continue;
                    if (seen[nx][ny][nz] || c->id[CH_IDX(nx, ny, nz)] != BLK_AIR) continue;
                    seen[nx][ny][nz] = 1;
                    stack[sp][0] = (int16_t)nx;
                    stack[sp][1] = (int16_t)ny;
                    stack[sp][2] = (int16_t)nz;
                    sp++;
                }
            }

            bool here = false;
            for (int lz = 0; lz < CH_D; lz++) {
                for (int lx = 0; lx < CH_W; lx++) {
                    columns++;
                    int const h = worldgen_height(cx * CH_W + lx, cz * CH_D + lz, seed);
                    if (h <= CH_SEA_LEVEL + 1 || h >= CH_H) continue;
                    if (c->id[CH_IDX(lx, h, lz)] == BLK_AIR && seen[lx][h][lz]) carved++;
                    int depth = 0;
                    for (int y = h; y > CH_BEDROCK; y--) {
                        if (!seen[lx][y][lz]) break;
                        depth++;
                    }
                    if (depth >= 3) {
                        deep3++;
                        here = true;
                    }
                    if (depth >= 6) deep6++;
                    if (depth > best) best = depth;
                }
            }
            if (here) chunks_with++;
        }
    }

    double const pct3 = 100.0 * (double)deep3 / (double)columns;
    printf("  %ld columns in %d chunks: the sky gets 3 deep in %ld (%.3f%%), 6 deep in %ld; deepest %d blocks\n",
           columns, chunks, deep3, pct3, deep6, best);
    printf("  %d chunks in %d have a way in (one every %.1f)\n", chunks_with, chunks,
           chunks_with > 0 ? (double)chunks / (double)chunks_with : 0.0);

    // THE ONE THAT MATTERS: a world with no way into the ground is the
    // one the user walked around for days.
    CHECK(deep3 > 0, "the sky never gets three blocks into the ground: there are no cave entrances");
    CHECK(best >= 8, "the deepest the sky gets is %d blocks: those are dimples, not entrances", best);
    CHECK(chunks_with * 20 >= chunks, "only %d chunks in %d have a way in, which is a world without caves",
          chunks_with, chunks);
    // ... and not a colander. Both directions, because both are wrong.
    CHECK(pct3 < 3.0, "%.2f%% of columns are open to the sky: the ground is a sieve", pct3);
    CHECK(carved > 0, "no column anywhere has its surface block carved by a cave");

    free(c->id);
    free(c->st);
    free(c);
}

static void check_population(void) {
    printf("population: what a ring of real terrain holds\n");
    uint32_t const seed = 20260929u;

    chunk_store_clear();
    mob_reset();

    // A FULL FAR-VIEW RING, which is the worst case the pool has to
    // survive: 15 x 15 chunks resident at once (chunk_render.h,
    // VIEW_FAR_EVICT is 7, so 2*7+1 across).
    int kinds[MOB_KIND_COUNT];
    memset(kinds, 0, sizeof(kinds));
    int chunks = 0, wet = 0;
    for (int32_t cz = -7; cz <= 7; cz++) {
        for (int32_t cx = -7; cx <= 7; cx++) {
            chunk_t* c = chunk_claim(cx, cz);
            if (c == NULL) continue;
            worldgen_chunk(c, seed, FARLANDS_NONE);
            c->cstate = CS_READY;
            chunk_resummarise(c);
            chunks++;
            mob_populate_chunk(cx, cz, seed);
        }
    }
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = mob_at(i);
        if (!m->alive) continue;
        kinds[m->kind]++;
        int32_t const bx = (int32_t)floor(m->body.x), by = (int32_t)floor(m->body.y), bz = (int32_t)floor(m->body.z);
        if (block_liquid(world_block(bx, by, bz)) || block_liquid(world_block(bx, by - 1, bz))) wet++;
    }
    printf("  %d chunks resident at once: %d animals (%d pig, %d cow, %d sheep, %d dog)\n", chunks, mob_live(),
           kinds[MOB_PIG], kinds[MOB_COW], kinds[MOB_SHEEP], kinds[MOB_DOG]);

    // THE POOL MUST NOT FILL. This is the whole of F-125: a full pool
    // does not fail, it goes quiet.
    CHECK(mob_refused() == 0, "%u animals were turned away: the pool is too small for a far-view ring",
          mob_refused());
    CHECK(mob_live() < MOB_MAX, "a single ring filled the pool (%d of %d)", mob_live(), MOB_MAX);
    // ... and it must not be empty either. A landscape with no animals
    // in sight is the other half of what the user reported.
    CHECK(mob_live() > 20, "only %d animals in %d chunks: a walk would meet nothing", mob_live(), chunks);
    CHECK(wet == 0, "%d animals were generated standing in water", wet);

    // EVERY KIND HAS TO TURN UP SOMEWHERE, over an area a player could
    // walk in an evening. The dog is the one this is really about.
    mob_reset();
    int seen[MOB_KIND_COUNT];
    memset(seen, 0, sizeof(seen));
    int wide = 0;
    for (int32_t cz = -14; cz < 14; cz++) {
        for (int32_t cx = -14; cx < 14; cx++) {
            chunk_t* c = chunk_claim(cx, cz);
            if (c == NULL) continue;
            worldgen_chunk(c, seed, FARLANDS_NONE);
            c->cstate = CS_READY;
            chunk_resummarise(c);
            wide++;
            mob_reset();  // counting what each chunk OFFERS, not what fits
            mob_populate_chunk(cx, cz, seed);
            for (int i = 0; i < MOB_MAX; i++) {
                if (mob_at(i)->alive) seen[mob_at(i)->kind]++;
            }
        }
    }
    int total = 0;
    for (int k = MOB_PIG; k < MOB_KIND_COUNT; k++) total += seen[k];
    printf("  %d chunks walked: %d animals, one every %.1f chunks; %d dogs, one every %.0f\n", wide, total,
           (double)wide / (total > 0 ? total : 1), seen[MOB_DOG],
           seen[MOB_DOG] > 0 ? (double)wide / seen[MOB_DOG] : 0.0);
    for (int k = MOB_PIG; k < MOB_KIND_COUNT; k++) {
        CHECK(seen[k] > 0, "no %s in %d chunks of real terrain", mob_def((uint8_t)k)->name, wide);
    }
    // A dog is a FIND, not an errand: rarer than the livestock, and not
    // so rare that a player never meets one.
    CHECK(seen[MOB_DOG] * 4 < total, "dogs are as common as livestock");
    CHECK(seen[MOB_DOG] * 200 > wide, "a dog turns up less than once in 200 chunks, which is never");

    mob_reset();
    chunk_store_clear();
}

static void check_swimming(void) {
    printf("swimming: an animal in water floats\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(16) != NULL, "the swimming world would not become resident");
    mob_reset();

    // A LAKE FOUR DEEP WITH A SHORE ON ONE SIDE, and the shore is at
    // the water's own level -- which is the only way a lake can be. The
    // first version of this fixture put the bank four blocks BELOW the
    // surface, which no world could contain and which sent the swimming
    // rule looking for land in the wrong place.
    //
    //   z < 8   ground to y = 19, so it is walked on at y = 20
    //   z >= 8  water from y = 16 to 19 on a bed at 15: four deep,
    //           surface flush with the bank
    // ACROSS THE WHOLE RESIDENT AREA, not one chunk of it: an animal
    // that swims to the edge of the fixture and out into bare test
    // terrain has not been tested, it has been lost.
    for (int x = -16; x < 32; x++) {
        for (int z = -16; z < 32; z++) {
            for (int y = 16; y < 20; y++) {
                set_block(x, y, z, z >= 8 ? BLK_WATER : BLK_DIRT, z >= 8 ? ST_PLACED : 0);
            }
            if (z < 8) set_block(x, 19, z, BLK_GRASS, 0);
        }
    }

    phys_body_t watcher;
    phys_body_init(&watcher, 40.0, 20.0, 40.0);
    uint32_t clk = 8000;

    // DROPPED IN THE MIDDLE OF IT, a cow must float and then make for
    // the shore. Both halves matter, and the second one is what repairs
    // a world where animals are already standing on the sea bed.
    int const c = mob_spawn(MOB_COW, 6.5, 19.5, 12.5, false);
    CHECK(c >= 0, "the pool would not take a cow");
    double low = 99.0;
    int    out = -1;
    for (int t = 0; t < 1200 && out < 0; t++) {
        mob_tick(clk++, &watcher, 0);
        if (mob_at(c)->body.y < low) low = mob_at(c)->body.y;
        if (mob_at(c)->body.z < 7.8) out = t;
    }
    printf("  a cow dropped in four blocks of water never sank below y %.2f (the bed is at 16) and "
           "reached the shore in %d ticks\n", low, out);
    CHECK(low > 16.5, "the cow sank to the bottom of the lake (y %.2f)", low);
    CHECK(out >= 0, "the cow never got out of the lake (at %.2f, %.2f, %.2f)", mob_at(c)->body.x,
          mob_at(c)->body.y, mob_at(c)->body.z);

    // AND IT DOES NOT WALK IN. A herd on the shore stays on the shore:
    // deep water is a cliff as far as an animal is concerned.
    mob_reset();
    int const walkers[4] = {mob_spawn(MOB_COW, 4.5, 20.0, 5.5, false), mob_spawn(MOB_PIG, 8.5, 20.0, 6.5, false),
                            mob_spawn(MOB_SHEEP, 11.5, 20.0, 5.5, false), mob_spawn(MOB_COW, 2.5, 20.0, 6.5, false)};
    for (int t = 0; t < 4000; t++) mob_tick(clk++, &watcher, 0);
    int swam = 0;
    for (int i = 0; i < 4; i++) {
        if (walkers[i] < 0) continue;
        if (mob_at(walkers[i])->body.z > 8.5) swam++;
    }
    printf("  four animals wandering a shore for three minutes: %d ended up in the lake\n", swam);
    CHECK(swam == 0, "%d animals wandered into deep water", swam);

    mob_reset();
    chunk_store_clear();
}

static void check_fishing(void) {
    printf("fishing: a worm a cast, and what comes up\n");

    fishing_t f;
    fishing_reset(&f);

    // --- Casting ------------------------------------------------------
    CHECK(fishing_use(&f, 0, -1, 0, true, 100).what == FISH_NO_WATER, "the line went out over dry land");
    CHECK(!f.out, "a refused cast left the line in the water");
    CHECK(fishing_use(&f, 4, 20, 4, false, 100).what == FISH_NO_WORM, "the line went out with no bait");
    CHECK(!f.out, "a cast with no worm left the line out");
    // WATER IS ASKED ABOUT FIRST: a player pointing at a wall should not
    // also be told they are out of worms, and must not lose one.
    CHECK(fishing_use(&f, 0, -1, 0, false, 100).what == FISH_NO_WATER, "no water and no worm blamed the worm");

    CHECK(fishing_use(&f, 4, 20, 4, true, 100).what == FISH_CAST, "the line would not go out");
    CHECK(f.out, "casting did not put the line in the water");

    // --- The wait, and the bite ---------------------------------------
    int bite_at = -1;
    for (int t = 0; t < 400 && bite_at < 0; t++) {
        if (fishing_tick(&f)) bite_at = t;
    }
    CHECK(bite_at >= 0, "nothing bit in 400 ticks (20 seconds)");
    CHECK(bite_at + 1 >= (int)FISH_WAIT_MIN, "a bite came after %d ticks, sooner than the minimum wait", bite_at);
    CHECK(fishing_biting(&f), "the bite was not open on the tick it started");
    printf("  the first bite came %d ticks in (%.1f s), and the window is %u ticks\n", bite_at,
           (double)bite_at / 20.0, FISH_BITE_TICKS);

    // STRIKING ON THE BITE LANDS A FISH, and it is one of the three.
    fish_use_t const got = fishing_use(&f, 4, 20, 4, true, 200);
    CHECK(got.what == FISH_CAUGHT, "striking on the bite caught nothing");
    CHECK(got.item == ITEM_SARDINE || got.item == ITEM_SALMON || got.item == ITEM_SHRIMP,
          "what came up was %s", item_def(got.item).name);
    CHECK(!f.out, "landing a fish left the line in the water");

    // --- A MISSED BITE IS NOT A LOST WORM ------------------------------
    fishing_reset(&f);
    CHECK(fishing_use(&f, 7, 20, 7, true, 500).what == FISH_CAST, "the second cast would not go out");
    for (int t = 0; t < 400 && !fishing_biting(&f); t++) fishing_tick(&f);
    CHECK(fishing_biting(&f), "nothing bit on the second cast");
    for (uint32_t t = 0; t < FISH_BITE_TICKS + 1; t++) fishing_tick(&f);
    CHECK(!fishing_biting(&f), "the bite window never closed");
    CHECK(f.out, "a missed bite reeled the line in -- that would cost a worm");
    int second = -1;
    for (int t = 0; t < 400 && second < 0; t++) {
        if (fishing_tick(&f)) second = t;
    }
    CHECK(second >= 0, "no second fish came after a missed bite");
    printf("  a missed bite keeps the line out; the next one came %d ticks later\n", second);

    // Striking with nothing on the line does not end the cast either,
    // as long as it is soon after casting -- an impatient press.
    fishing_reset(&f);
    fishing_use(&f, 7, 20, 7, true, 900);
    CHECK(fishing_use(&f, 7, 20, 7, true, 901).what == FISH_TOO_SOON, "an early strike was not called early");
    CHECK(f.out, "an early strike lost the cast");
    // ... but a player who keeps at it gets the line back on purpose.
    for (int t = 0; t < 60; t++) fishing_tick(&f);
    CHECK(fishing_use(&f, 7, 20, 7, true, 960).what == FISH_REELED, "the line could not be reeled in");
    CHECK(!f.out, "reeling in left the line out");

    // --- The same river, twice -----------------------------------------
    //
    // Determinism is the whole of Part T: two casts made in the same
    // place on the same tick have to behave identically.
    fishing_t a, b;
    fishing_reset(&a);
    fishing_reset(&b);
    fishing_use(&a, 3, 20, 9, true, 4242);
    fishing_use(&b, 3, 20, 9, true, 4242);
    int ta = 0, tb = 0;
    while (!fishing_biting(&a) && ta < 500) {
        fishing_tick(&a);
        ta++;
    }
    while (!fishing_biting(&b) && tb < 500) {
        fishing_tick(&b);
        tb++;
    }
    CHECK(ta == tb, "the same cast bit after %d ticks, then %d", ta, tb);
    CHECK(fishing_use(&a, 3, 20, 9, true, 0).item == fishing_use(&b, 3, 20, 9, true, 0).item,
          "the same cast landed two different fish");

    // --- What a day's fishing looks like --------------------------------
    int counts[3] = {0, 0, 0};
    for (uint32_t n = 0; n < 600; n++) {
        uint16_t const got_n = fishing_catch_for(12, 20, 12, 77u, n);
        if (got_n == ITEM_SARDINE) counts[0]++;
        if (got_n == ITEM_SHRIMP) counts[1]++;
        if (got_n == ITEM_SALMON) counts[2]++;
    }
    printf("  600 fish: %d sardines, %d shrimp, %d salmon\n", counts[0], counts[1], counts[2]);
    CHECK(counts[0] > 0 && counts[1] > 0 && counts[2] > 0, "600 casts never landed one of the three");
    // The pizza wants TWO shrimp, so shrimp must not be the rare one.
    CHECK(counts[1] > counts[2], "shrimp are rarer than salmon, and the pizza wants two of them");

    // --- The rod itself ---------------------------------------------------
    CHECK(item_def(ITEM_ROD).durability == 0, "the fishing rod wears out");
    recipe_t const* rod = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->out == ITEM_ROD) rod = recipe_at(i);
    }
    CHECK(rod != NULL, "nothing makes a fishing rod");
}

static void check_sheep(void) {
    printf("sheep: the fleece, and what it becomes\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the sheep world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    mob_reset();
    item_entity_reset();

    phys_body_t watcher;
    phys_body_init(&watcher, 12.0, 20.0, 12.0);
    uint32_t clk = 6000;

    int const s = mob_spawn(MOB_SHEEP, 6.5, 20.0, 6.5, false);
    CHECK(s >= 0, "the pool would not take a sheep");
    CHECK(!mob_at(s)->shorn, "a new sheep is already shorn");

    // Anything but shears gets nothing off it.
    CHECK(mob_use(s, ITEM_AXE_IRON).what == MOB_USE_NOTHING, "an axe sheared a sheep");
    CHECK(item_entity_live() == 0, "something came off the sheep without shears");

    mob_use_result_t const cut = mob_use(s, ITEM_SHEARS);
    CHECK(cut.what == MOB_USE_SHORN, "shears did not shear a sheep");
    CHECK(cut.wear && !cut.consume, "shearing did not wear the shears, or ate them");
    CHECK(mob_at(s)->shorn, "the sheep is not shorn after shearing");
    CHECK(mob_at(s)->alive, "shearing killed the sheep");
    int wool = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e != NULL && e->alive && e->item == ITEM_WOOL) wool += e->count;
    }
    CHECK(wool >= mob_def(MOB_SHEEP)->shear_min && wool <= mob_def(MOB_SHEEP)->shear_max,
          "shearing gave %d wool, outside %u..%u", wool, mob_def(MOB_SHEEP)->shear_min,
          mob_def(MOB_SHEEP)->shear_max);

    // A SHORN SHEEP GIVES NOTHING until its coat is back.
    CHECK(mob_use(s, ITEM_SHEARS).what == MOB_USE_BARE, "a shorn sheep was sheared again");

    // ... and the coat takes an in-game day, counted in TICKS ELAPSED
    // (D-51), so a flock nobody is looking at grows no wool.
    for (uint32_t t = 0; t < MOB_REGROW_TICKS - 2; t++) mob_tick(clk++, &watcher, 0);
    CHECK(mob_at(s)->shorn, "the fleece grew back early");
    for (int t = 0; t < 4; t++) mob_tick(clk++, &watcher, 0);
    CHECK(!mob_at(s)->shorn, "the fleece never grew back");
    printf("  a fleece takes %u ticks to grow back, and the sheep lives through it\n", MOB_REGROW_TICKS);

    // A LAMB HAS NO FLEECE TO GIVE.
    int const lamb = mob_spawn(MOB_SHEEP, 7.5, 20.0, 6.5, true);
    CHECK(mob_use(lamb, ITEM_SHEARS).what == MOB_USE_NOTHING, "a lamb was sheared");

    // --- The fleece survives a save -------------------------------------
    mob_reset();
    int const keep = mob_spawn(MOB_SHEEP, 6.5, 20.0, 6.5, false);
    mob_at_mut(keep)->shorn     = true;
    mob_at_mut(keep)->age_shorn = 1234;
    static uint8_t buf[2048];
    size_t const   n = mob_encode_chunk(0, 0, buf, sizeof buf);
    CHECK(n > 0, "a chunk with a sheep in it wrote nothing");
    mob_drop_chunk(0, 0);
    mob_decode_section(buf + 5, n - 5);
    mob_t const* back = NULL;
    for (int i = 0; i < MOB_MAX; i++) {
        if (mob_at(i)->alive) back = mob_at(i);
    }
    CHECK(back != NULL && back->kind == MOB_SHEEP, "what came back is not a sheep");
    CHECK(back->shorn && back->age_shorn == 1234, "the sheep came back with its coat on (shorn %d, age %u)",
          back->shorn, back->age_shorn);

    // --- Wool is where string comes from --------------------------------
    inventory_t inv;
    inv_clear(&inv);
    inv_add(&inv, ITEM_WOOL, 4, 0);
    recipe_t const* str = NULL;
    recipe_t const* bed = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->out == ITEM_STRING) str = r;
        if (r->out == BLK_BED_FOOT) bed = r;
    }
    CHECK(str != NULL, "nothing makes string");
    CHECK(bed != NULL, "nothing makes a bed");
    CHECK(str->out_n == 3 && str->n_in == 1 && str->in[0].item == ITEM_WOOL && str->in[0].count == 1,
          "one wool does not make three strings");
    CHECK(recipe_make(str, &inv, 1) == 1, "the string recipe would not run");
    CHECK(inv_count(&inv, ITEM_STRING) == 3, "one wool made %d strings", inv_count(&inv, ITEM_STRING));
    CHECK(inv_count(&inv, ITEM_WOOL) == 3, "the string recipe took the wrong amount of wool");

    // And the bed: three wool and three planks, which is most of a
    // flock's first shearing.
    inv_clear(&inv);
    inv_add(&inv, ITEM_WOOL, 3, 0);
    inv_add(&inv, BLK_PLANKS, 3, 0);
    CHECK(recipe_make(bed, &inv, 1) == 1, "three wool and three planks would not make a bed");

    mob_reset();
    item_entity_reset();
}

static void check_bed(void) {
    printf("the bed: two cells, one thing\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the bed world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    item_entity_reset();

    // Aiming at the ground at (6, 19, 6) while facing +z: the foot goes
    // in the cell above it and the head one further along.
    ray_hit_t hit = {0};
    hit.x = 6, hit.y = 19, hit.z = 6;
    hit.px = 6, hit.py = 20, hit.pz = 6;
    hit.block = BLK_GRASS;
    hit.face  = MESH_DIR_PY;
    CHECK(interact_place_dir(&hit, BLK_BED_FOOT, NULL, 0.0f, 1.0f), "a bed would not go down");
    CHECK(world_block(6, 20, 6) == BLK_BED_FOOT, "the foot of the bed is not where it was put");
    CHECK(world_block(6, 20, 7) == BLK_BED_HEAD, "the head of the bed is not beside the foot");
    CHECK(st_data(world_state(6, 20, 6)) == st_data(world_state(6, 20, 7)),
          "the two halves of the bed disagree about which way they lie");

    // Each half knows the other.
    int32_t ox, oy, oz;
    CHECK(interact_bed_other(6, 20, 6, &ox, &oy, &oz) && ox == 6 && oy == 20 && oz == 7,
          "the foot does not know where its head is");
    CHECK(interact_bed_other(6, 20, 7, &ox, &oy, &oz) && ox == 6 && oy == 20 && oz == 6,
          "the head does not know where its foot is");
    CHECK(!interact_bed_other(6, 20, 9, NULL, NULL, NULL), "empty air claims to be half a bed");

    // NO ROOM, NO BED: the second cell is taken, so nothing is placed --
    // half a bed is not a thing.
    set_block(9, 20, 7, BLK_STONE, ST_PLACED);
    ray_hit_t h2 = {0};
    h2.x = 9, h2.y = 19, h2.z = 6;
    h2.px = 9, h2.py = 20, h2.pz = 6;
    h2.block = BLK_GRASS;
    h2.face  = MESH_DIR_PY;
    CHECK(!interact_place_dir(&h2, BLK_BED_FOOT, NULL, 0.0f, 1.0f), "a bed went down with one cell of room");
    CHECK(world_block(9, 20, 6) == BLK_AIR, "a refused bed left half of itself behind");

    // BREAKING EITHER HALF TAKES BOTH, and only the foot pays out.
    item_entity_reset();
    break_result_t const r = interact_break(6, 20, 7, ITEM_AXE_IRON);  // the HEAD
    CHECK(r.ok, "the head of the bed would not break");
    CHECK(world_block(6, 20, 6) == BLK_AIR && world_block(6, 20, 7) == BLK_AIR,
          "breaking one half of the bed left the other");
    int beds = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e != NULL && e->alive && e->item == BLK_BED_FOOT) beds += e->count;
    }
    CHECK(beds == 1, "breaking a bed dropped %d of them", beds);
    printf("  placed as two cells, broken as two cells, dropped as one bed\n");
    item_entity_reset();
}

static void check_pens(void) {
    printf("pens: what actually keeps a cow in (F-122)\n");
    double high = 0.0;

    // 30000 ticks is about twenty-five minutes of play, and an animal
    // that wanders for that long has tried every corner of a 6 x 6 pen
    // many times over.
    CHECK(pen_holds(PEN_BARE, 30000, &high), "a cow got out of a bare pen");
    printf("  bare pen: it never got above y %.2f (the fence tops out at %.2f)\n", high, 20.0 + BLOCK_FENCE_TOP);
    CHECK(high < 20.0 + (double)BLOCK_FENCE_TOP, "a cow in a bare pen reached the top of the fence");

    // THE USER'S PEN. One block inside is all it took: the step-up is a
    // whole block here, so from 21.0 the lift reaches 22.0 -- over a
    // fence that tops out at 21.5 -- and the sideways move that follows
    // carried the cow clean out the far side.
    CHECK(pen_holds(PEN_BLOCK, 30000, &high), "a cow got out of a pen with a block to stand on");
    printf("  with a block inside: it never got above y %.2f\n", high);

    // A SHUT GATE IS FENCE. An open one is a hole, and has to be.
    CHECK(pen_holds(PEN_GATE_SHUT, 30000, NULL), "a cow walked through a shut gate");
    CHECK(!pen_holds(PEN_GATE_OPEN, 30000, NULL), "an open gate did not let a cow out");

    // AND WHAT IS STILL ALLOWED: two blocks stacked inside is a
    // staircase, and a creature standing 2.0 up is simply above a fence
    // that reaches 1.5. That is the player's doing and it stays
    // possible -- the rule is "a fence is not a step", not "a fence is
    // a forcefield".
    bool const stairs = pen_holds(PEN_STAIRS, 30000, &high);
    printf("  with a two-block stack inside: %s (high %.2f) -- allowed either way\n", stairs ? "held" : "walked out",
           high);

    // THE PLAYER IS THE SAME BODY. Standing on a block against the
    // fence, walking at it must not lift them over either.
    chunk_store_clear();
    flat_world(20);
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    for (int i = 4; i <= 9; i++) set_block(i, 20, 4, BLK_FENCE, ST_PLACED);
    set_block(6, 20, 5, BLK_DIRT, ST_PLACED);
    phys_body_t p;
    phys_body_init(&p, 6.5, 21.0, 5.5);  // standing on the block, facing the fence
    for (int t = 0; t < 60; t++) phys_move(&p, 0.0, -0.08, -0.08);
    CHECK(p.z > 5.0, "the player stepped over a fence from a block beside it (z %.2f, y %.2f)", p.z, p.y);
    printf("  a player on a block beside the fence stayed at z %.2f\n", p.z);
}

static void check_animals(void) {
    printf("animals: fences, feeding, milking, taming, and what a save keeps\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the animal world would not become resident");
    mob_reset();
    item_entity_reset();
    uint32_t clock = 50000;

    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);

    // --- The registry ------------------------------------------------
    for (uint8_t k = MOB_PIG; k < MOB_KIND_COUNT; k++) {
        mob_def_t const* d = mob_def(k);
        CHECK(d->name != NULL && d->name[0] != '\0', "creature %u has no name on disk", k);
        CHECK(T(d->label) != NULL && T(d->label)[0] != '\0', "creature %s has no name on screen", d->name);
        CHECK(d->health_max > 0, "%s has no health", d->name);
        CHECK(d->w > 0.0f && d->h > 0.0f, "%s has no body", d->name);
        CHECK(d->drop_item == ITEM_NONE || d->drop_max >= d->drop_min, "%s drops backwards", d->name);
    }

    // --- A FENCE IS A BLOCK AND A HALF -------------------------------
    //
    // The player's own box against it, which is the case that decides
    // whether a pen is a pen: a jump reaches 1.33 blocks (player.h), so
    // standing on the ground beside a fence there must be no height at
    // which the body fits over it.
    set_block(5, 20, 5, BLK_FENCE, ST_PLACED);
    phys_body_t b;
    phys_body_init(&b, 5.5, 20.0, 4.3);
    CHECK(!phys_fits(&b, 5.5, 20.0, 5.5), "a body stands inside a fence");
    CHECK(!phys_fits(&b, 5.5, 20.6, 5.5), "a jump of 0.6 blocks clears a fence");
    CHECK(!phys_fits(&b, 5.5, 21.0, 5.5), "a jump of a whole block clears a fence");
    CHECK(!phys_fits(&b, 5.5, 21.33, 5.5), "the top of the player's jump clears a fence");
    CHECK(phys_fits(&b, 5.5, 21.55, 5.5), "a body one and a half blocks up is still inside the fence");
    // And the step-up: walking into one must not climb it, which a
    // one-block-tall block would allow (PHYS_STEP is a whole block).
    b.x = 5.5, b.y = 20.0, b.z = 4.3;
    for (int t = 0; t < 40; t++) phys_move(&b, 0.0, 0.0, 0.08);
    CHECK(b.z < 5.0, "the player walked over a fence (ended at z %.2f)", b.z);

    // --- ... AND A GATE IS THE HOLE IN IT ----------------------------
    set_block(5, 20, 6, BLK_FENCE, ST_PLACED);
    set_block(5, 20, 7, BLK_FENCE, ST_PLACED);
    set_block(6, 20, 5, BLK_FENCE_GATE, ST_PLACED);
    phys_body_init(&b, 6.5, 20.0, 4.3);
    for (int t = 0; t < 40; t++) phys_move(&b, 0.0, 0.0, 0.08);
    CHECK(b.z < 5.0, "a shut gate let the player through (ended at z %.2f)", b.z);

    CHECK(interact_toggle_gate(6, 20, 5), "the gate would not open");
    CHECK(world_block(6, 20, 5) == BLK_FENCE_GATE_OPEN, "opening the gate did not change the block");
    CHECK(st_data(world_state(6, 20, 5)) == st_data(world_state(6, 20, 5)), "the gate lost which way it lies");
    phys_body_init(&b, 6.5, 20.0, 4.3);
    for (int t = 0; t < 40; t++) phys_move(&b, 0.0, 0.0, 0.08);
    CHECK(b.z > 6.0, "an open gate did not let the player through (ended at z %.2f)", b.z);
    CHECK(interact_toggle_gate(6, 20, 5) && world_block(6, 20, 5) == BLK_FENCE_GATE, "the gate would not shut again");
    CHECK(!interact_toggle_gate(5, 20, 5), "a fence opened like a gate");

    // --- A PEN HOLDS ---------------------------------------------------
    //
    // A pig in a four-by-four pen, left to wander for two minutes of
    // ticks. It may go anywhere inside and nowhere outside.
    chunk_store_clear();
    CHECK(flat_world(20) != NULL, "the pen world would not become resident");
    mob_reset();
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    for (int i = 4; i <= 9; i++) {
        set_block(i, 20, 4, BLK_FENCE, ST_PLACED);
        set_block(i, 20, 9, BLK_FENCE, ST_PLACED);
        set_block(4, 20, i, BLK_FENCE, ST_PLACED);
        set_block(9, 20, i, BLK_FENCE, ST_PLACED);
    }
    // ONE OF EACH, because they are not the same body: a cow is 1.4
    // blocks tall and a pig 0.9, and it was a COW that got out when the
    // user built a pen (F-122). Long enough to be a real afternoon:
    // 20000 ticks is about sixteen minutes of play.
    int const pen[3] = {mob_spawn(MOB_PIG, 6.5, 20.0, 6.5, false), mob_spawn(MOB_COW, 7.5, 20.0, 6.5, false),
                        mob_spawn(MOB_COW, 6.5, 20.0, 7.5, false)};
    CHECK(pen[0] >= 0 && pen[1] >= 0 && pen[2] >= 0, "the pool would not take the pen's animals");
    double      high = 0.0;
    phys_body_t watcher;
    phys_body_init(&watcher, 60.0, 20.0, 60.0);
    for (int t = 0; t < 20000; t++) {
        mob_tick(clock++, &watcher, 0);
        for (int i = 0; i < 3; i++) {
            mob_t const* m = mob_at(pen[i]);
            if (m->body.y > high) high = m->body.y;
        }
    }
    printf("  16 minutes in a 6 x 6 pen: the highest anything got was y %.2f (the fence tops out at %.2f)\n", high,
           20.0 + (double)BLOCK_FENCE_TOP);
    for (int i = 0; i < 3; i++) {
        mob_t const* m = mob_at(pen[i]);
        CHECK(m->alive && m->body.x > 4.0 && m->body.x < 10.0 && m->body.z > 4.0 && m->body.z < 10.0,
              "a %s got out of the pen (at %.2f, %.2f, %.2f)", mob_def(m->kind)->name, m->body.x, m->body.y,
              m->body.z);
        CHECK(m->body.y >= 19.9 && m->body.y <= 20.2, "a %s did not stay on the ground (y %.2f)",
              mob_def(m->kind)->name, m->body.y);
    }

    // --- Milking, feeding, taming --------------------------------------
    mob_reset();
    int const cow = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, false);
    int const calf = mob_spawn(MOB_COW, 7.5, 20.0, 6.5, true);
    CHECK(cow >= 0 && calf >= 0, "the pool would not take two cows");

    mob_use_result_t u = mob_use(cow, ITEM_BUCKET);
    CHECK(u.what == MOB_USE_MILKED && u.becomes == ITEM_BUCKET_MILK, "a bucket on a cow gave no milk");
    CHECK(!u.consume, "milking took the bucket as well");
    CHECK(mob_use(calf, ITEM_BUCKET).what == MOB_USE_NOTHING, "a calf gave milk");
    CHECK(mob_use(cow, ITEM_BUCKET_MILK).what == MOB_USE_NOTHING, "a full bucket milked the cow again");

    u = mob_use(cow, ITEM_WHEAT);
    CHECK(u.what == MOB_USE_FED && u.consume, "wheat did not feed a cow");
    CHECK(mob_use(cow, ITEM_POTATO).what == MOB_USE_NOTHING, "a cow ate a potato");
    // A BREEDING PAIR MAKES ONE CALF. Both have to be fed: one in the
    // mood and one not is nothing at all.
    CHECK(mob_count_kind(MOB_COW) == 2, "the herd was not two to start with");
    mob_run(&clock, 5, 60.0, 60.0, 0);
    CHECK(mob_count_kind(MOB_COW) == 2, "one fed cow bred on its own");
    int const cow2 = mob_spawn(MOB_COW, 7.0, 20.0, 6.5, false);
    CHECK(cow2 >= 0, "the pool would not take a third cow");
    // AN ANIMAL ALREADY IN THE MOOD EATS NOTHING (the user's rule): the
    // first cow is still looking for a partner from the feed above.
    CHECK(mob_use(cow, ITEM_WHEAT).what == MOB_USE_BUSY, "a cow already in the mood ate again");
    CHECK(!mob_use(cow, ITEM_WHEAT).consume, "a refused feed still took the wheat");
    CHECK(mob_use(cow2, ITEM_WHEAT).what == MOB_USE_FED, "the second cow would not be fed");
    mob_run(&clock, 3, 60.0, 60.0, 0);
    CHECK(mob_count_kind(MOB_COW) == 4, "two fed cows made %d cows, not four", mob_count_kind(MOB_COW));
    // ... and not a second one the next tick: that is what the cooldown
    // is for, and without it a pair is a herd in ten seconds.
    mob_run(&clock, 200, 60.0, 60.0, 0);
    CHECK(mob_count_kind(MOB_COW) == 4, "the pair kept breeding (%d cows)", mob_count_kind(MOB_COW));

    // A dog is nobody's until it is given a bone.
    int const dog = mob_spawn(MOB_DOG, 8.5, 20.0, 6.5, false);
    CHECK(dog >= 0, "the pool would not take a dog");
    CHECK(mob_use(dog, ITEM_BEEF).what != MOB_USE_TAMED, "raw beef tamed a dog");
    CHECK(!mob_at(dog)->tame, "the dog was tame before the bone");
    u = mob_use(dog, ITEM_BONE);
    CHECK(u.what == MOB_USE_TAMED && u.consume, "a bone did not tame the dog");
    CHECK(mob_at(dog)->tame, "the dog is not tame after being tamed");
    CHECK(mob_use(dog, ITEM_BONE).what == MOB_USE_SIT && mob_at(dog)->sitting, "a tame dog would not sit");
    CHECK(mob_use(dog, ITEM_BONE).what == MOB_USE_STAND && !mob_at(dog)->sitting, "a sitting dog would not get up");
    CHECK(mob_use(cow, ITEM_BONE).what == MOB_USE_NOTHING, "a bone tamed a cow");

    // --- Killing one ----------------------------------------------------
    item_entity_reset();
    mob_reset();
    int const beef_cow = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, false);
    int       blows    = 0;
    while (mob_at(beef_cow)->alive && blows < 100) {
        mob_at_mut(beef_cow)->hurt = 0;  // the flinch is a timer, not a shield against the test
        mob_hit(beef_cow, mob_damage_of(0), 6.5, 4.0);
        blows++;
    }
    CHECK(!mob_at(beef_cow)->alive, "a cow would not die");
    CHECK(blows == mob_def(MOB_COW)->health_max, "a bare fist took %d blows, not %d", blows,
          mob_def(MOB_COW)->health_max);
    CHECK(item_entity_live() > 0, "a dead cow dropped nothing");
    int found = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e != NULL && e->alive && e->item == ITEM_BEEF) found += e->count;
    }
    CHECK(found >= mob_def(MOB_COW)->drop_min && found <= mob_def(MOB_COW)->drop_max,
          "a cow dropped %d beef, outside %u..%u", found, mob_def(MOB_COW)->drop_min, mob_def(MOB_COW)->drop_max);

    // AN IRON AXE IS FOUR BLOWS OF THE TEN. Not a balance test: it is
    // the claim that what is in the hand matters at all.
    CHECK(mob_damage_of(ITEM_AXE_IRON) > mob_damage_of(0), "an iron axe hits no harder than a fist");
    CHECK(mob_damage_of(ITEM_HOE_IRON) == mob_damage_of(0), "a hoe is a weapon");

    // A CALF DROPS NOTHING, or breeding is a meat machine.
    item_entity_reset();
    mob_reset();
    int const young = mob_spawn(MOB_COW, 6.5, 20.0, 6.5, true);
    for (int t = 0; t < 40 && mob_at(young)->alive; t++) {
        mob_at_mut(young)->hurt = 0;
        mob_hit(young, 4, 6.5, 4.0);
    }
    CHECK(!mob_at(young)->alive, "the calf would not die");
    CHECK(item_entity_live() == 0, "a calf dropped %d stacks", item_entity_live());

    // --- What a save keeps ----------------------------------------------
    mob_reset();
    int const keep = mob_spawn(MOB_DOG, 6.25, 20.0, 6.75, true);
    mob_t*    k    = mob_at_mut(keep);
    k->tame        = true;
    k->sitting     = true;
    k->age         = 4321;
    k->health      = 3;
    k->yaw         = 1.25f;

    static uint8_t buf[4096];
    size_t const   n = mob_encode_chunk(0, 0, buf, sizeof buf);
    CHECK(n > 0, "a chunk with a dog in it wrote no entities section");
    CHECK(buf[0] == SECTION_ENTITIES, "the entities section has the wrong id");
    mob_drop_chunk(0, 0);
    CHECK(mob_live() == 0, "dropping the chunk left creatures behind");
    // The section's CONTENTS, as the reader is handed them (region.c).
    mob_decode_section(buf + 5, n - 5);
    CHECK(mob_live() == 1, "the dog did not come back");
    mob_t const* back = NULL;
    for (int i = 0; i < MOB_MAX; i++) {
        if (mob_at(i)->alive) back = mob_at(i);
    }
    CHECK(back != NULL && back->kind == MOB_DOG, "what came back is not a dog");
    CHECK(back->tame && back->sitting && back->baby, "the dog came back untamed, standing or grown");
    CHECK(back->age == 4321 && back->health == 3, "the dog came back with age %u and %d health", back->age,
          back->health);
    CHECK(back->body.x > 6.2 && back->body.x < 6.3, "the dog moved in the save (x %.3f)", back->body.x);

    // --- Where a herd comes from -----------------------------------------
    //
    // Deterministic in (cx, cz, seed): the same field, twice, has to put
    // the same animals in the same places, or a world is a different
    // world every time it is opened.
    chunk_store_clear();
    CHECK(flat_world(20) != NULL, "the herd world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    mob_reset();
    mob_populate_chunk(0, 0, 4242u);
    int const first = mob_live();
    double    fx = 0.0, fz = 0.0;
    for (int i = 0; i < MOB_MAX; i++) {
        if (mob_at(i)->alive) {
            fx = mob_at(i)->body.x;
            fz = mob_at(i)->body.z;
            break;
        }
    }
    mob_reset();
    mob_populate_chunk(0, 0, 4242u);
    CHECK(mob_live() == first, "the same chunk generated %d animals, then %d", first, mob_live());
    if (first > 0) {
        for (int i = 0; i < MOB_MAX; i++) {
            if (mob_at(i)->alive) {
                CHECK(mob_at(i)->body.x == fx && mob_at(i)->body.z == fz, "the herd moved between two generations");
                break;
            }
        }
    }
    // And how many a landscape holds, which is the number that decides
    // whether a walk finds anything. Reported rather than asserted at a
    // point: it is a feel, and the range is what matters.
    mob_reset();
    int herds = 0, animals = 0;
    for (int32_t cz = 0; cz < 24; cz++) {
        for (int32_t cx = 0; cx < 24; cx++) {
            int const was = mob_live();
            mob_populate_chunk(cx, cz, 99u);
            if (mob_live() > was) herds++;
            animals += mob_live() - was;
            mob_reset();  // the pool is 48; this is a count, not a world
        }
    }
    printf("  576 chunks (a 384 x 384 block world): %d with a herd in them, %d animals\n", herds, animals);
    CHECK(herds > 10 && herds < 300, "%d chunks in 576 held a herd, which is not a landscape", herds);
    mob_reset();
    item_entity_reset();
}

static void check_makers(void) {
    printf("the cheese maker and the sausage maker: a day and a minute\n");
    chunk_store_clear();
    CHECK(flat_world(20) != NULL, "the maker world would not become resident");
    blockent_clear();

    // What each one takes, which is the recipe table and not a list in
    // the machine.
    CHECK(maker_accepts(BE_CHEESE, ITEM_BUCKET_MILK), "the cheese maker does not take milk");
    CHECK(!maker_accepts(BE_CHEESE, ITEM_PORK), "the cheese maker takes pork");
    CHECK(maker_accepts(BE_SAUSAGE, ITEM_PORK), "the sausage maker does not take pork");
    CHECK(maker_accepts(BE_SAUSAGE, BLK_FLOWER_RED) && maker_accepts(BE_SAUSAGE, BLK_FLOWER_YELLOW),
          "the sausage maker refuses a flower");
    CHECK(maker_accepts(BE_SAUSAGE, ITEM_BEANS), "the sausage maker does not take beans");
    CHECK(maker_returns(BE_CHEESE, ITEM_BUCKET_MILK) == ITEM_BUCKET, "the milk bucket does not come back");
    CHECK(maker_returns(BE_SAUSAGE, ITEM_PORK) == 0, "the sausage maker hands something back");

    // --- The cheese maker: one bucket, one day ------------------------
    blockent_t* be = blockent_add(2, 20, 2, BE_CHEESE);
    CHECK(be != NULL, "the pool would not give a cheese maker a record");
    be->stamp                    = 0;
    be->slot[BE_MAKER_IN_A].item  = ITEM_BUCKET_MILK;
    be->slot[BE_MAKER_IN_A].count = 1;

    CHECK(maker_barrel_state(be) == BARREL_MILK, "a barrel with milk in it does not show milk");
    maker_catch_up(be, MAKER_DAY - 1);
    CHECK(be->slot[BE_MAKER_OUT].count == 0, "the cheese maker paid out before the day was up");
    CHECK(maker_progress_pct(be, MAKER_DAY - 1) > 90, "the progress bar is not nearly full after a day less a tick");
    maker_catch_up(be, MAKER_DAY);
    CHECK(be->slot[BE_MAKER_OUT].item == ITEM_CHEESE && be->slot[BE_MAKER_OUT].count == 1,
          "a day did not turn a bucket of milk into one cheese");
    CHECK(be->slot[BE_MAKER_IN_A].count == 0, "the milk is still in the barrel");
    CHECK(maker_barrel_state(be) == BARREL_CHEESE, "a barrel with cheese in it does not show cheese");
    // AN EMPTY BARREL BANKS NOTHING: a week of standing idle must not
    // turn the next bucket into cheese on the spot.
    maker_catch_up(be, MAKER_DAY * 8);
    be->slot[BE_MAKER_IN_A].item  = ITEM_BUCKET_MILK;
    be->slot[BE_MAKER_IN_A].count = 1;
    maker_catch_up(be, MAKER_DAY * 8 + 10);
    CHECK(be->slot[BE_MAKER_OUT].count == 1, "an idle barrel banked a week and made cheese at once");

    // --- The sausage maker: a minute, and two ways to make one --------
    blockent_t* sm = blockent_add(4, 20, 2, BE_SAUSAGE);
    CHECK(sm != NULL, "the pool would not give a sausage maker a record");
    sm->stamp                     = 0;
    sm->slot[BE_MAKER_IN_A].item  = ITEM_PORK;
    sm->slot[BE_MAKER_IN_A].count = 4;
    sm->slot[BE_MAKER_IN_B].item  = BLK_FLOWER_RED;
    sm->slot[BE_MAKER_IN_B].count = 4;
    maker_catch_up(sm, MAKER_MINUTE - 1);
    CHECK(sm->slot[BE_MAKER_OUT].count == 0, "the sausage maker paid out before the minute was up");
    maker_catch_up(sm, MAKER_MINUTE * 4);
    CHECK(sm->slot[BE_MAKER_OUT].item == ITEM_SAUSAGE && sm->slot[BE_MAKER_OUT].count == 4,
          "four minutes made %u sausages", sm->slot[BE_MAKER_OUT].count);
    CHECK(sm->slot[BE_MAKER_IN_A].count == 0 && sm->slot[BE_MAKER_IN_B].count == 0,
          "the sausage maker did not eat its pork and flowers");

    // Two beans make the vegetarian one, and it is worth the same.
    blockent_remove(4, 20, 2);
    sm = blockent_add(4, 20, 2, BE_SAUSAGE);
    CHECK(sm != NULL, "the pool would not give a second sausage maker a record");
    sm->stamp                     = 0;
    sm->slot[BE_MAKER_IN_A].item  = ITEM_BEANS;
    sm->slot[BE_MAKER_IN_A].count = 2;
    maker_catch_up(sm, MAKER_MINUTE);
    CHECK(sm->slot[BE_MAKER_OUT].item == ITEM_SAUSAGE_VEG && sm->slot[BE_MAKER_OUT].count == 1,
          "two beans did not make a bean sausage");
    CHECK(sm->slot[BE_MAKER_EXTRA].count == 0, "a bean sausage left a bone");

    // --- What it says when it is one thing short -----------------------
    //
    // The user, playing step 11: "when putting in pork and there is
    // nothing in the second slot, it should clearly state that it needs
    // yellow flowers as well". Either colour will do, so the machine
    // names BOTH -- and it gets them out of the recipe table, so a third
    // flower would appear in the message on its own.
    blockent_remove(4, 20, 2);
    sm = blockent_add(4, 20, 2, BE_SAUSAGE);
    CHECK(sm != NULL, "the pool would not give a third sausage maker a record");
    if (sm != NULL) {
        sm->stamp = 0;
        CHECK(maker_idle_reason(sm) == MAKER_IDLE_NO_INPUT, "an empty machine does not say it is empty");

        sm->slot[BE_MAKER_IN_A].item  = ITEM_PORK;
        sm->slot[BE_MAKER_IN_A].count = 1;
        CHECK(maker_idle_reason(sm) == MAKER_IDLE_MISSING, "pork with no flower does not say what is missing");

        uint16_t what[MAKER_MISSING_MAX];
        int      need = 0;
        int const cnt = maker_missing(sm, what, &need, MAKER_MISSING_MAX);
        CHECK(cnt == 2, "pork alone is short of %d things, wanted both flowers", cnt);
        CHECK(need == 1, "it wants %d flowers, wanted 1", need);
        bool red = false, yellow = false;
        for (int i = 0; i < cnt; i++) {
            red |= what[i] == BLK_FLOWER_RED;
            yellow |= what[i] == BLK_FLOWER_YELLOW;
        }
        CHECK(red && yellow, "the missing list does not name both flowers");

        // ONE BEAN is short of one more bean, and of nothing else: a
        // row nobody has started is not a row the machine is short of.
        sm->slot[BE_MAKER_IN_A].item  = ITEM_BEANS;
        sm->slot[BE_MAKER_IN_A].count = 1;
        int const beans = maker_missing(sm, what, &need, MAKER_MISSING_MAX);
        CHECK(beans == 1 && what[0] == ITEM_BEANS && need == 1,
              "one bean is short of %d things, wanted one more bean", beans);

        // AND WHAT IT IS NOT USING IS NOT ITS. Two beans make a sausage
        // on their own, so a flower in the other slot belongs to the
        // player (the user: "these should automatically return").
        sm->slot[BE_MAKER_IN_A].count = 2;
        sm->slot[BE_MAKER_IN_B].item  = BLK_FLOWER_YELLOW;
        sm->slot[BE_MAKER_IN_B].count = 3;
        CHECK(maker_match(sm) != NULL, "two beans do not match a recipe");
        CHECK(maker_uses(sm, ITEM_BEANS), "the bean sausage does not use beans");
        CHECK(!maker_uses(sm, BLK_FLOWER_YELLOW), "the bean sausage claims to use a flower");
        // ... and with pork in instead, the flower IS an ingredient and
        // must stay exactly where it is.
        sm->slot[BE_MAKER_IN_A].item  = ITEM_PORK;
        sm->slot[BE_MAKER_IN_A].count = 1;
        CHECK(maker_uses(sm, BLK_FLOWER_YELLOW), "the pork sausage does not use its flower");
    }
    printf("  pork alone asks for either flower; a bean sausage does not want one\n");

    // --- The bone, which is what a dog costs ---------------------------
    //
    // One in six of the PORK ones, from the world's hash: counted over
    // enough units that the rate is a fact rather than a coincidence.
    int bones = 0;
    for (uint32_t u = 0; u < 600; u++) {
        if (maker_extra_for(4, 20, 2, u)) bones++;
    }
    printf("  600 pork sausages left %d bones (one in %.1f)\n", bones, bones > 0 ? 600.0 / bones : 0.0);
    CHECK(bones > 60 && bones < 140, "%d bones in 600 is not one in six", bones);
    // The same box asked twice gives the same answer, which is what
    // makes a replay reproduce a farm (Part T).
    CHECK(maker_extra_for(4, 20, 2, 7) == maker_extra_for(4, 20, 2, 7), "the bone roll is not deterministic");

    blockent_remove(2, 20, 2);
    blockent_remove(4, 20, 2);
}

// =====================================================================
//  THE KITCHEN STOVE (step 11, D-105 and D-110)
// ---------------------------------------------------------------------
//  Three things worth proving, and they are the three that would each
//  cost a player something real:
//
//    the PAIR      -- one item, two blocks, each knowing the other, and
//                     breaking either takes both and drops ONE item;
//    the COOKING   -- ingredients come out of the chest and only when a
//                     dish is finished, and an idle stove banks nothing;
//    the REFUSALS  -- the user asked for "an appropriate info message",
//                     and there are five different ways to be idle.
// =====================================================================
static void check_stove(void) {
    printf("the kitchen stove: two blocks, one item, and a chest it reads\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the stove world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    blockent_clear();
    item_entity_reset();

    // --- One item, two blocks ----------------------------------------
    //
    // Facing +z, so the chest goes to the player's LEFT, which is -x.
    ray_hit_t hit = {0};
    hit.x = 6, hit.y = 19, hit.z = 6;
    hit.px = 6, hit.py = 20, hit.pz = 6;
    hit.block = BLK_GRASS;
    hit.face  = MESH_DIR_PY;
    CHECK(interact_place_dir(&hit, BLK_STOVE, NULL, 0.0f, 1.0f), "a stove would not go down");
    CHECK(world_block(6, 20, 6) == BLK_STOVE, "the stove is not where it was put");
    CHECK(world_block(5, 20, 6) == BLK_STOVE_CHEST, "the chest did not come down on the stove's left");

    int32_t ox, oy, oz;
    CHECK(interact_stove_other(6, 20, 6, &ox, &oy, &oz) && ox == 5 && oy == 20 && oz == 6,
          "the stove does not know where its chest is");
    CHECK(interact_stove_other(5, 20, 6, &ox, &oy, &oz) && ox == 6 && oy == 20 && oz == 6,
          "the chest does not know which stove it belongs to");
    CHECK(!interact_stove_other(6, 20, 9, NULL, NULL, NULL), "empty air claims to be half a stove");

    blockent_t* be    = blockent_at(6, 20, 6);
    blockent_t* chest = blockent_at(5, 20, 6);
    CHECK(be != NULL && be->kind == BE_STOVE, "the stove has no record");
    CHECK(chest != NULL && chest->kind == BE_CHEST, "the stove's chest is not a chest");
    if (be == NULL || chest == NULL) return;

    // A SECOND STOVE BESIDE THE FIRST still finds its own chest, which
    // is the case blocks.h worried would need stored coordinates: a row
    // of them is `chest stove chest stove` and every facing is exact.
    ray_hit_t h2 = {0};
    h2.x = 8, h2.y = 19, h2.z = 6;
    h2.px = 8, h2.py = 20, h2.pz = 6;
    h2.block = BLK_GRASS;
    h2.face  = MESH_DIR_PY;
    CHECK(interact_place_dir(&h2, BLK_STOVE, NULL, 0.0f, 1.0f), "a second stove would not go down");
    CHECK(world_block(7, 20, 6) == BLK_STOVE_CHEST, "the second stove's chest is missing");
    CHECK(interact_stove_other(8, 20, 6, &ox, NULL, NULL) && ox == 7,
          "a stove in a row picked up its neighbour's chest");
    CHECK(interact_stove_other(7, 20, 6, &ox, NULL, NULL) && ox == 8,
          "a chest in a row picked up the wrong stove");
    interact_break(8, 20, 6, ITEM_PICK_IRON);
    item_entity_reset();

    // NO ROOM, NO STOVE: half a kitchen is a stove that cannot cook.
    set_block(11, 20, 6, BLK_STONE, ST_PLACED);  // where the chest would go
    ray_hit_t h3 = {0};
    h3.x = 12, h3.y = 19, h3.z = 6;
    h3.px = 12, h3.py = 20, h3.pz = 6;
    h3.block = BLK_GRASS;
    h3.face  = MESH_DIR_PY;
    CHECK(!interact_place_dir(&h3, BLK_STOVE, NULL, 0.0f, 1.0f), "a stove went down with nowhere for its chest");
    CHECK(world_block(12, 20, 6) == BLK_AIR, "a refused stove left half of itself behind");
    CHECK(blockent_at(12, 20, 6) == NULL, "a refused stove kept a record from the pool");

    // --- Why it is idle, which is the user's info message -------------
    be->stamp = 0;
    CHECK(stove_idle_reason(be, chest) == STOVE_IDLE_NO_PICK, "a stove with no dish chosen does not say so");

    // BREAD: three wheat, and the chest has two of them.
    stove_set_pick(be, ITEM_BREAD);
    chest->slot[0].item  = ITEM_WHEAT;
    chest->slot[0].count = 2;
    CHECK(stove_idle_reason(be, chest) == STOVE_IDLE_MISSING, "a stove short of an ingredient does not say so");
    int      need = 0;
    uint16_t what = stove_missing(be, chest, &need);
    CHECK(what == ITEM_WHEAT && need == 1, "the stove named %u x %u as missing, wanted 1 wheat", need, what);
    CHECK(stove_idle_reason(be, NULL) == STOVE_IDLE_NO_CHEST,
          "a stove with no chest complains about ingredients instead");

    chest->slot[0].count = 9;  // three loaves' worth
    CHECK(stove_idle_reason(be, chest) == STOVE_IDLE_NO_FUEL, "a stove with no fuel does not say so");

    // --- Cooking ------------------------------------------------------
    // Four coal, because a loaf is 1200 ticks and a coal is 1600: one
    // lump does not see three loaves through, and a test that gave it
    // one would be testing the fuel rule while claiming to test the
    // clock.
    be->slot[BE_STOVE_FUEL].item  = ITEM_COAL;
    be->slot[BE_STOVE_FUEL].count = 4;
    CHECK(stove_busy(be, chest), "a stove with a dish, a chest and coal is not cooking");

    stove_catch_up(be, chest, STOVE_COOK_TICKS - 1);
    CHECK(be->slot[BE_STOVE_OUT].count == 0, "the stove paid out before the minute was up");
    CHECK(chest->slot[0].count == 9, "the stove took its wheat before the bread was baked");
    CHECK(stove_progress_pct(be) > 90, "the progress bar is not nearly full a tick before the end");

    stove_catch_up(be, chest, STOVE_COOK_TICKS);
    CHECK(be->slot[BE_STOVE_OUT].item == ITEM_BREAD && be->slot[BE_STOVE_OUT].count == 1,
          "a minute did not make one loaf");
    CHECK(chest->slot[0].count == 6, "the loaf did not cost three wheat out of the chest (%u left)",
          chest->slot[0].count);

    stove_catch_up(be, chest, STOVE_COOK_TICKS * 3);
    CHECK(be->slot[BE_STOVE_OUT].count == 3, "three minutes made %u loaves", be->slot[BE_STOVE_OUT].count);
    CHECK(chest->slot[0].count == 0, "three loaves did not empty a chest of nine wheat");

    // AN IDLE STOVE BANKS NOTHING. A week of standing with an empty
    // chest must not turn the next handful of wheat into bread on the
    // spot -- the same rule the barrel and the composter have.
    stove_catch_up(be, chest, STOVE_COOK_TICKS * 3 + MAKER_DAY * 7);
    chest->slot[0].item  = ITEM_WHEAT;
    chest->slot[0].count = 9;
    stove_catch_up(be, chest, STOVE_COOK_TICKS * 3 + MAKER_DAY * 7 + 10);
    CHECK(be->slot[BE_STOVE_OUT].count == 3, "an idle stove banked a week and baked at once");

    // THE PIZZA'S TWO ROWS: the fake sausage counts (the user), so a
    // chest with a bean sausage in it makes the same pizza.
    blockent_t* be2 = blockent_add(2, 21, 2, BE_STOVE);
    CHECK(be2 != NULL, "the pool would not give a second stove a record");
    if (be2 != NULL) {
        blockent_t* ch2 = blockent_add(3, 21, 2, BE_CHEST);
        CHECK(ch2 != NULL, "the pool would not give a second chest a record");
        if (ch2 != NULL) {
            be2->stamp                     = 0;
            be2->slot[BE_STOVE_FUEL].item  = ITEM_COAL;
            be2->slot[BE_STOVE_FUEL].count = 4;
            stove_set_pick(be2, ITEM_PIZZA);
            ch2->slot[0] = (inv_slot_t){ITEM_WHEAT, 2, 0};
            ch2->slot[1] = (inv_slot_t){ITEM_SAUSAGE_VEG, 1, 0};
            ch2->slot[2] = (inv_slot_t){ITEM_CHEESE, 1, 0};
            ch2->slot[3] = (inv_slot_t){ITEM_SHRIMP, 2, 0};
            stove_catch_up(be2, ch2, STOVE_COOK_TICKS);
            CHECK(be2->slot[BE_STOVE_OUT].item == ITEM_PIZZA,
                  "a bean sausage did not count as a pizza's sausage");
            CHECK(ch2->slot[1].count == 0, "the pizza did not eat the bean sausage");
        }
        blockent_remove(3, 21, 2);
        blockent_remove(2, 21, 2);
    }

    // --- What it remembers across a save ------------------------------
    //
    // The dish is stored by NAME, so a recipe inserted in the middle of
    // the table cannot repoint a stove somebody left cooking.
    {
        static uint8_t sec[8192];
        size_t const   n = blockent_encode_chunk(0, 0, sec, sizeof(sec));
        CHECK(n > 0, "a stove and its chest would not encode");
        blockent_clear();
        blockent_decode_section(sec + 5, n - 5);
        blockent_t const* back = blockent_at(6, 20, 6);
        CHECK(back != NULL && back->kind == BE_STOVE, "the stove did not come back from the card");
        if (back != NULL) {
            CHECK(back->pick == ITEM_BREAD, "the stove forgot which dish it was set to");
            CHECK(back->slot[BE_STOVE_OUT].item == ITEM_BREAD && back->slot[BE_STOVE_OUT].count == 3,
                  "the stove's finished bread did not survive");
        }
        blockent_t const* chest_back = blockent_at(5, 20, 6);
        CHECK(chest_back != NULL && chest_back->kind == BE_CHEST, "the stove's chest did not come back");
    }

    // --- Breaking it --------------------------------------------------
    //
    // Either half takes both, BOTH give back what they were holding, and
    // exactly ONE stove drops -- which is what makes the pair safe at a
    // chunk border where only one half is resident.
    be    = blockent_at(6, 20, 6);
    chest = blockent_at(5, 20, 6);
    CHECK(be != NULL && chest != NULL, "the pair is not in the pool after the save round trip");
    if (be == NULL || chest == NULL) return;
    chest->slot[0] = (inv_slot_t){ITEM_WHEAT, 11, 0};

    // WITH AN AXE, AT THE CHEST END. That is the wrong tool for the
    // stone half and has to work anyway: a pair breaks as one thing,
    // and a kitchen taken from the wrong end must not evaporate (see
    // the note on BLK_STOVE in blocks.c).
    item_entity_reset();
    break_result_t const r = interact_break(5, 20, 6, ITEM_AXE_IRON);  // the CHEST half
    CHECK(r.ok, "the stove's chest would not break");
    CHECK(world_block(6, 20, 6) == BLK_AIR && world_block(5, 20, 6) == BLK_AIR,
          "breaking one half of the stove left the other");
    int stoves = 0, wheat = 0, bread = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e == NULL || !e->alive) continue;
        if (e->item == BLK_STOVE) stoves += e->count;
        if (e->item == ITEM_WHEAT) wheat += e->count;
        if (e->item == ITEM_BREAD) bread += e->count;
    }
    CHECK(stoves == 1, "breaking a stove dropped %d of them", stoves);
    CHECK(wheat == 11, "the chest kept %d of its 11 wheat", wheat);
    CHECK(bread == 3, "the stove kept %d of its 3 loaves", bread);
    CHECK(blockent_at(5, 20, 6) == NULL && blockent_at(6, 20, 6) == NULL,
          "breaking the pair left a record behind in the pool");
    printf("  placed as two, saved as two, broken as two, dropped as one stove\n");
    item_entity_reset();
    blockent_clear();
}

// =====================================================================
//  HUNGER, SATURATION AND THE FALL (step 11)
// ---------------------------------------------------------------------
//  The hunger loop is the one system in this game that plays out over
//  half an hour, so it is the one nobody is going to watch on a badge.
//  Here it runs at a few million ticks a second.
// =====================================================================
static void check_hunger(void) {
    printf("hunger: the reserve, the regeneration, and starving\n");

    food_t f;
    food_reset(&f);
    CHECK(f.health == FOOD_HEALTH_MAX && f.hunger == FOOD_HUNGER_MAX && f.saturation == 0,
          "a new player did not start full with no reserve");

    // --- What a walk costs ---------------------------------------------
    //
    // At a walk (PL_WALK, 0.215 blocks a tick) one drumstick should be
    // most of a minute and a half. If this number ever moves a long way,
    // the food table stops meaning what the user priced it at.
    int ticks = 0;
    while (f.hunger == FOOD_HUNGER_MAX && ticks < 20 * 60 * 60) {
        food_tick(&f, 0.215f, false, false);
        ticks++;
    }
    CHECK(f.hunger == FOOD_HUNGER_MAX - 1, "the first drumstick went in one step of %d", ticks);
    printf("  walking costs one drumstick every %d ticks (%.0f seconds)\n", ticks, ticks / 20.0);
    CHECK(ticks > 20 * 40 && ticks < 20 * 180, "a drumstick a walk is %.0f seconds, which is not a walk",
          ticks / 20.0);

    // --- The reserve is spent first ------------------------------------
    food_reset(&f);
    f.hunger = 10;
    food_eat_t const fe = food_eat(&f, ITEM_SMOKED_SALMON);  // 2 hunger, 4 saturation
    CHECK(fe.ate, "smoked salmon would not go down");
    CHECK(f.hunger == 12, "smoked salmon gave %d hunger, wanted 2", f.hunger - 10);
    CHECK(f.saturation == 4, "smoked salmon gave %d reserve, wanted 4", f.saturation);

    int const before = f.hunger;
    while (f.saturation > 0) food_tick(&f, 1.0f, false, false);
    CHECK(f.hunger == before, "hunger went down while there was still reserve to spend");
    food_tick(&f, 400.0f, false, false);
    CHECK(f.hunger < before, "hunger did not go down once the reserve was gone");

    // SATURATION CANNOT EXCEED HUNGER, which is where the food table's
    // shape comes from: the salmon's four points are only worth having
    // to somebody with the drumsticks to hold them.
    food_reset(&f);
    f.hunger     = FOOD_HUNGER_MAX - 1;
    f.saturation = 0;
    food_eat(&f, ITEM_PIZZA);  // 10 hunger, 8 reserve, into one drumstick of room
    CHECK(f.hunger == FOOD_HUNGER_MAX, "a pizza did not fill the bar");
    CHECK(f.saturation <= f.hunger, "the reserve is bigger than the hunger holding it");

    // A FULL PLAYER REFUSES, and says so. Without it, leaning on the Use
    // key eats a larder.
    food_eat_t const full = food_eat(&f, ITEM_BREAD);
    CHECK(!full.ate && full.full, "a full player ate anyway");

    // THE PAIL COMES BACK when the milk is drunk.
    food_reset(&f);
    f.hunger = 10;
    food_eat_t const milk = food_eat(&f, ITEM_BUCKET_MILK);
    CHECK(milk.ate && milk.leftover == ITEM_BUCKET, "drinking the milk did not give the bucket back");
    CHECK(food_leftover(ITEM_BREAD) == 0, "a loaf of bread left something behind");

    // NOTHING THAT IS NOT FOOD IS FOOD, which is a column in the item
    // table and not a list: a pickaxe, a plank and a raw fish.
    CHECK(!food_is_food(ITEM_PICK_IRON) && !food_is_food(BLK_PLANKS), "a tool or a block counts as food");
    CHECK(!food_is_food(ITEM_SALMON), "raw salmon is food before it has seen a stove");
    CHECK(food_is_food(ITEM_TOMATO) && food_is_food(ITEM_CHEESE), "a tomato or a cheese is not food");

    // --- Regeneration, and what it costs -------------------------------
    food_reset(&f);
    f.health = 10;
    int beats = 0;
    for (int i = 0; i < FOOD_BEAT_TICKS * 4; i++) {
        if (food_tick(&f, 0.0f, false, false) == FOOD_HEALED) beats++;
    }
    CHECK(beats > 0 && f.health > 10, "a well-fed player did not heal");
    printf("  healing: %d beats in %d ticks took hunger to %d\n", beats, FOOD_BEAT_TICKS * 4, f.hunger);
    CHECK(f.hunger < FOOD_HUNGER_MAX, "healing cost nothing at all");

    // ... and a hungry one does not heal. Nine drumsticks of ten is the
    // gate, so the last one is the warning.
    food_reset(&f);
    f.health = 10;
    f.hunger = FOOD_REGEN_AT - 1;
    for (int i = 0; i < FOOD_BEAT_TICKS * 4; i++) food_tick(&f, 0.0f, false, false);
    CHECK(f.health == 10, "a player under the regeneration gate healed anyway");

    // --- Starving, which can kill --------------------------------------
    food_reset(&f);
    f.hunger     = 0;
    f.saturation = 0;
    bool died = false;
    for (int i = 0; i < FOOD_BEAT_TICKS * (FOOD_HEALTH_MAX + 2) && !died; i++) {
        died = food_tick(&f, 0.0f, false, false) == FOOD_DIED;
    }
    CHECK(died && f.health == 0, "an empty stomach did not kill in %d beats", FOOD_HEALTH_MAX + 2);
    printf("  an empty larder kills in %.0f seconds\n", FOOD_HEALTH_MAX * FOOD_BEAT_TICKS / 20.0);

    // --- Damage ---------------------------------------------------------
    food_reset(&f);
    CHECK(food_hurt(&f, 6) == FOOD_STARVED && f.health == FOOD_HEALTH_MAX - 6, "six points of damage took %d",
          FOOD_HEALTH_MAX - f.health);
    CHECK(f.exhaustion > 0.0f, "being hurt cost no exhaustion");
    CHECK(food_hurt(&f, 99) == FOOD_DIED && f.health == 0, "a mortal blow did not kill");
    CHECK(food_hurt(&f, 1) == FOOD_NOTHING, "a corpse took more damage");
}

// The fall rule, which is the other half of health going down. Driven
// against a REAL BODY falling through a real world -- phys_move and
// phys_gravity, the same two calls the player tick makes -- and through
// the shipped tracker rather than a copy of its arithmetic. What is
// being tested is the number the body's y actually reaches, not what a
// formula says about a distance nobody measured.
static void check_fall(void) {
    printf("falling: three blocks for nothing, and a point a block after\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the falling world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_STONE, 0);

    // Drop a body from `h` blocks above the floor and see what it costs.
    // The floor is the top of the stone at y = 20, so the feet start at
    // 20 + h and land at 20.
    struct {
        int drop;
        int want;
    } const CASES[] = {
        {1, 0}, {2, 0}, {3, 0}, {4, 1}, {5, 2}, {8, 5}, {20, 17},
    };
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        phys_body_t b;
        phys_body_init(&b, 8.5, 20.0 + (double)CASES[i].drop, 8.5);
        food_t f;
        food_reset(&f);
        fall_t fl;
        fall_reset(&fl, b.y);

        int took = 0;
        for (int t = 0; t < 600; t++) {
            phys_move(&b, 0.0, (double)b.vy, 0.0);
            phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
            int const hurt = fall_tick(&fl, b.y, b.on_ground, false);
            if (hurt > 0) {
                food_hurt(&f, hurt);
                took = hurt;
                break;
            }
        }
        CHECK(b.on_ground, "a body dropped %d blocks never landed", CASES[i].drop);
        CHECK(took == CASES[i].want, "a fall of %d blocks took %d health, wanted %d", CASES[i].drop, took,
              CASES[i].want);
        CHECK(f.health == FOOD_HEALTH_MAX - CASES[i].want, "the damage did not reach the health");
    }
    printf("  1 to 3 blocks are free; 4 costs 1, 8 costs 5, 20 costs 17\n");

    // A LEDGE HALF WAY DOWN IS TWO SHORT FALLS, not one long one -- the
    // reason the tracker keeps a height and not a velocity. Twelve
    // blocks in two hops of six costs twice what six costs, not what
    // twelve costs.
    {
        phys_body_t b;
        phys_body_init(&b, 8.5, 26.0, 8.5);
        fall_t fl;
        fall_reset(&fl, b.y);
        int first = 0, second = 0;
        for (int t = 0; t < 400 && first == 0; t++) {
            phys_move(&b, 0.0, (double)b.vy, 0.0);
            phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
            first = fall_tick(&fl, b.y, b.on_ground, false);
        }
        CHECK(first == 3, "the first six-block hop took %d, wanted 3", first);
        // Off the ledge again, the same distance.
        b.y = 26.0;
        b.on_ground = false;
        b.vy = 0.0f;
        for (int t = 0; t < 400 && second == 0; t++) {
            phys_move(&b, 0.0, (double)b.vy, 0.0);
            phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
            second = fall_tick(&fl, b.y, b.on_ground, false);
        }
        CHECK(second == 3, "the second six-block hop took %d, wanted 3 -- the tracker banked the first", second);
    }
    printf("  two six-block hops cost 3 each, not 9 once\n");

    // WATER IS A LANDING. The same drop with the body in water costs
    // nothing, which is what makes a waterfall a way down.
    {
        phys_body_t b;
        phys_body_init(&b, 8.5, 40.0, 8.5);
        fall_t fl;
        fall_reset(&fl, b.y);
        int took = 0;
        for (int t = 0; t < 600 && took == 0; t++) {
            phys_move(&b, 0.0, (double)b.vy, 0.0);
            phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
            // Into the pool at y 22 and below.
            took = fall_tick(&fl, b.y, b.on_ground, b.y <= 22.0);
        }
        CHECK(took == 0, "a twenty-block fall into water took %d health", took);
    }
    printf("  the same fall into water costs nothing\n");
    chunk_store_clear();
}

// =====================================================================
//  SAPLINGS, AND THE COMPOST BED (the user, 2026-09-29)
// ---------------------------------------------------------------------
//  Two things that were not renewable and now are: WOOD, because no
//  recipe anywhere makes a log, and YELLOW FLOWERS, which a pork
//  sausage needs for its spice and which only the generator ever put
//  in the world.
// =====================================================================
static void check_saplings(void) {
    printf("saplings: wood that grows back, and a bed of flowers\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the sapling world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);
    item_entity_reset();

    // --- Which sapling comes off which trunk --------------------------
    CHECK(tree_sapling_for(BLK_LOG) == BLK_SAPLING_OAK, "an oak does not leave an oak seedling");
    CHECK(tree_sapling_for(BLK_BIRCH_LOG) == BLK_SAPLING_BIRCH, "a birch does not leave a birch seedling");
    CHECK(tree_sapling_for(BLK_STONE) == BLK_AIR, "stone leaves a seedling");
    CHECK(tree_is_sapling(BLK_SAPLING_OAK) && tree_is_sapling(BLK_SAPLING_BIRCH), "a sapling is not a sapling");
    CHECK(!tree_is_sapling(BLK_WHEAT_CROP), "wheat claims to be a sapling");

    // ONE OR TWO, from the world's hash and never rand(): the same spot
    // asked twice gives the same answer, which is what makes a replay
    // reproduce a woodpile.
    int ones = 0, twos = 0;
    for (int32_t i = 0; i < 400; i++) {
        int const n = tree_drops_at(i, 20, i * 7);
        CHECK(n >= TREE_DROP_MIN && n <= TREE_DROP_MAX, "a tree left %d seedlings", n);
        if (n == 1) ones++;
        if (n == 2) twos++;
    }
    printf("  400 trees left %d singles and %d pairs\n", ones, twos);
    CHECK(ones > 100 && twos > 100, "one or two is not a coin toss: %d and %d", ones, twos);
    CHECK(tree_drops_at(3, 20, 9) == tree_drops_at(3, 20, 9), "the seedling roll is not deterministic");

    // --- Planting ------------------------------------------------------
    //
    // A SAPLING IS THE ONE SEED THAT DOES NOT WANT A TILLED FIELD.
    CHECK(crops_plant(4, 19, 4, BLK_SAPLING_OAK) == PLANT_OK, "a sapling would not go into grass");
    CHECK(world_block(4, 20, 4) == BLK_SAPLING_OAK, "the sapling is not standing on the grass");
    CHECK(crop_stage(world_state(4, 20, 4)) == 0, "a planted sapling did not start at its first stage");

    set_block(6, 19, 6, BLK_FARMLAND_WET, ST_PLACED);
    CHECK(crops_plant(6, 19, 6, BLK_SAPLING_OAK) == PLANT_NEEDS_GROUND,
          "a sapling went into a tilled field");
    set_block(8, 19, 8, BLK_STONE, ST_PLACED);
    CHECK(crops_plant(8, 19, 8, BLK_SAPLING_BIRCH) == PLANT_NEEDS_GROUND, "a sapling went into stone");

    // --- Growing -------------------------------------------------------
    //
    // Driven through crops_advance, which is where every path into
    // growth ends -- the sweep, the catch-up on load and compost.
    for (int step = 0; step < 3; step++) crops_advance(4, 20, 4, 1);
    uint8_t const grew = world_block(4, 20, 4);
    CHECK(grew != BLK_SAPLING_OAK, "the sapling is still a sapling after all of its stages");
    CHECK(grew == BLK_LOG, "the sapling became %s, wanted a trunk", block_def(grew)->name);

    // A TRUNK AND A CANOPY, and the trunk is GROWN and not PLACED -- so
    // the tree somebody planted comes down like any other (Part F).
    int logs = 0, leaves = 0;
    for (int y = 20; y < 32; y++) {
        for (int dz = -3; dz <= 3; dz++) {
            for (int dx = -3; dx <= 3; dx++) {
                uint8_t const b = world_block(4 + dx, y, 4 + dz);
                if (b == BLK_LOG) logs++;
                if (b == BLK_LEAVES) leaves++;
            }
        }
    }
    printf("  a planted seedling grew %d logs and %d leaves\n", logs, leaves);
    CHECK(logs >= 4, "the grown tree has a trunk of %d", logs);
    CHECK(leaves > 10, "the grown tree has a canopy of %d", leaves);
    CHECK((world_state(4, 21, 4) & ST_PLACED) == 0, "a grown tree is marked as placed and will not fell");

    // AND IT FELLS AS A TREE, seedlings and all: the loop closes.
    item_entity_reset();
    break_result_t const fell = interact_break(4, 20, 4, ITEM_AXE_IRON);
    CHECK(fell.was_tree, "a planted tree did not fell");
    int seeds = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e != NULL && e->alive && e->item == BLK_SAPLING_OAK) seeds += e->count;
    }
    CHECK(seeds >= TREE_DROP_MIN, "felling a planted tree left no seedling");
    printf("  ... and felling it left %d seedling(s): the loop closes\n", seeds);

    // --- No room, no tree ----------------------------------------------
    //
    // A sapling under a ceiling has nowhere to put a canopy. It must
    // stay a sapling rather than growing half a tree -- and it must
    // still be there to try again later.
    item_entity_reset();
    CHECK(crops_plant(12, 19, 12, BLK_SAPLING_OAK) == PLANT_OK, "the second sapling would not go in");
    for (int y = 22; y < 26; y++) set_block(12, y, 12, BLK_STONE, ST_PLACED);
    for (int step = 0; step < 4; step++) crops_advance(12, 20, 12, 1);
    CHECK(world_block(12, 20, 12) == BLK_SAPLING_OAK, "a sapling under a ceiling grew anyway");
    CHECK(world_block(12, 21, 12) == BLK_AIR, "half a tree went up under the ceiling");
    // Take the ceiling away and the next nudge grows it.
    for (int y = 22; y < 26; y++) set_block(12, y, 12, BLK_AIR, 0);
    crops_advance(12, 20, 12, 1);
    CHECK(world_block(12, 20, 12) == BLK_LOG, "a sapling that was refused never tried again");
    printf("  a sapling under a ceiling waits, and grows when the ceiling goes\n");

    // --- Compost on bare grass: a 3 x 3 of yellow flowers -------------
    chunk_store_clear();
    CHECK(flat_world(20) != NULL, "the flower world would not become resident");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++) set_block(x, 19, z, BLK_GRASS, 0);

    // Aimed at the top of the grass at (8, 19, 8).
    use_result_t u = interact_use_item(8.5, 21.0, 8.5, 0.0f, -1.0f, 0.0f, ITEM_COMPOST);
    CHECK(u.acted, "compost on bare grass did nothing");
    CHECK(u.consume, "a bed of flowers cost no compost");
    int flowers = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (world_block(8 + dx, 20, 8 + dz) == BLK_FLOWER_YELLOW) flowers++;
        }
    }
    CHECK(flowers == 9, "compost raised %d flowers, wanted a 3 x 3", flowers);
    CHECK(world_block(5, 20, 8) == BLK_AIR, "the bed spread further than three by three");
    printf("  one compost raised a 3 x 3 of yellow flowers\n");

    // IT DOES NOT PAVE OVER WHAT IS THERE. A second helping on the same
    // spot has nothing left to do, and says so rather than eating the
    // compost for nothing.
    u = interact_use_item(8.5, 21.0, 8.5, 0.0f, -1.0f, 0.0f, ITEM_COMPOST);
    CHECK(!u.acted && u.msg != USE_SAID_NOTHING, "a second compost on a full bed was spent for nothing");

    // AND IT ONLY GROWS ON GRASS. A stone floor gets nothing.
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) set_block(3 + dx, 19, 3 + dz, BLK_STONE, ST_PLACED);
    u = interact_use_item(3.5, 21.0, 3.5, 0.0f, -1.0f, 0.0f, ITEM_COMPOST);
    CHECK(!u.acted, "compost grew flowers on stone");
    printf("  ... on grass, and on nothing else\n");
    chunk_store_clear();
    item_entity_reset();
}

static void check_replay(void) {
    printf("replays\n");
    replay_start_t st = {.seed = 0xC0FFEEu, .time_of_day = 4242, .x = 12.5, .y = 30.0, .z = -7.25,
                         .yaw = 1.25f, .pitch = -0.3f, .selected = 3};
    st.inv[0] = (inv_slot_t){ITEM_PICK_STONE, 1, 9};
    st.inv[5] = (inv_slot_t){BLK_TORCH, 17, 0};
    CHECK(replay_record_begin(&st), "could not start recording");
    for (int i = 0; i < 300; i++) replay_record_tick((uint32_t)(i * 2654435761u), (float)i * 0.01f, -(float)i * 0.02f);
    CHECK(replay_record_end("build/host/test.smr"), "could not write the replay");
    replay_start_t back;
    CHECK(replay_load("build/host/test.smr", &back), "could not read the replay back");
    CHECK(back.seed == st.seed && back.time_of_day == 4242 && back.x == 12.5 && back.z == -7.25 &&
              back.yaw == 1.25f && back.selected == 3,
          "the replay's start did not survive");
    CHECK(back.inv[0].item == ITEM_PICK_STONE && back.inv[0].wear == 9 && back.inv[5].count == 17,
          "the replay's inventory did not survive");
    int      n  = 0;
    bool     ok = true;
    uint32_t m;
    float    gy, gp;
    while (replay_next(&m, &gy, &gp)) {
        ok = ok && m == (uint32_t)(n * 2654435761u) && gy == (float)n * 0.01f && gp == -(float)n * 0.02f;
        n++;
    }
    CHECK(n == 300 && ok, "the replay played back %d ticks%s", n, ok ? "" : ", not the ones recorded");
    CHECK(!replay_playing(), "a finished replay says it is still playing");
}

static void check_physics(void) {
    printf("physics\n");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");

    phys_body_t b;

    // Falls onto the floor and stops exactly on top of it.
    phys_body_init(&b, 8.5, 20.0, 8.5);
    for (int i = 0; i < 200; i++) phys_move(&b, 0.0, -0.4, 0.0);
    printf("  fell to y = %.3f (floor top is 8)\n", b.y);
    CHECK(b.on_ground, "a body that fell 12 blocks is not on the ground");
    CHECK(b.y > 7.99 && b.y < 8.01, "a body came to rest at y %g, expected 8", b.y);

    // A fast fall must not pass through the floor: 3 blocks a tick is
    // terminal velocity and the floor is 8 thick, but one sub-step must
    // never cross more than a block.
    phys_body_init(&b, 8.5, 40.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.0, -3.0, 0.0);
    CHECK(b.y > 7.99 && b.y < 8.01, "at terminal velocity a body tunnelled to y %g", b.y);

    // Walks into a wall and stops against it, without stopping dead in
    // the other axis (it must slide).
    set_block(11, 8, 8, BLK_STONE, 0);
    set_block(11, 9, 8, BLK_STONE, 0);
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 40; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  stopped at x = %.3f against a wall whose face is at 11\n", b.x);
    CHECK(b.x < 10.71 && b.x > 10.69, "a body stopped at x %g, expected 10.70 (11 - 0.3)", b.x);
    CHECK(b.hit_x, "a body against a wall does not report hit_x");

    // A one-block step is walked up without jumping: a plateau at y = 8,
    // wide enough that the walk ends standing ON it rather than having
    // crossed it and dropped off the far side.
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    for (int32_t x = 11; x <= 24; x++) {
        for (int32_t z = 6; z <= 10; z++) set_block(x, 8, z, BLK_STONE, 0);
    }
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 40; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  after walking at a 1-block step: x %.2f, y %.2f\n", b.x, b.y);
    CHECK(b.x > 12.0, "a body stopped at a 1-block rise instead of stepping up (x %g)", b.x);
    CHECK(b.y > 8.99 && b.y < 9.01, "a body is at y %g on top of a 1-block rise, expected 9", b.y);
    CHECK(b.on_ground, "a body that stepped up is not on the ground");

    // Two of them in a row: a staircase is walkable, which is what the
    // step is for.
    for (int32_t x = 15; x <= 24; x++) {
        for (int32_t z = 6; z <= 10; z++) set_block(x, 9, z, BLK_STONE, 0);
    }
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  after a two-step staircase: x %.2f, y %.2f\n", b.x, b.y);
    CHECK(b.y > 9.99 && b.y < 10.01, "a body is at y %g after two steps, expected 10", b.y);

    // A two-block wall is NOT climbed. This is the bound that keeps
    // the step from being a cheat: walls stay walls.
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    for (int32_t z = 6; z <= 10; z++) {
        set_block(14, 8, z, BLK_STONE, 0);
        set_block(14, 9, z, BLK_STONE, 0);
    }
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  against a 2-block wall: x %.2f, y %.2f\n", b.x, b.y);
    CHECK(b.y < 8.6, "a body climbed a 2-block wall (y %g)", b.y);
    CHECK(b.x < 13.8, "a body passed through a 2-block wall (x %g)", b.x);

    // A 2-block gap is walkable; a 1-block one is not (the player is
    // 1.8 tall).
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    for (int32_t x = 20; x <= 26; x++) {
        set_block(x, 10, 8, BLK_STONE, 0);  // a ceiling 2 blocks above the floor
    }
    phys_body_init(&b, 19.5, 8.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  through a 2-high gap: x %.2f\n", b.x);
    CHECK(b.x > 26.0, "a body could not walk through a 2-block-high gap (x %g)", b.x);

    for (int32_t x = 30; x <= 36; x++) {
        set_block(x, 9, 8, BLK_STONE, 0);  // a ceiling 1 block above the floor
    }
    phys_body_init(&b, 29.5, 8.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  at a 1-high gap: x %.2f (should be stopped near 30)\n", b.x);
    CHECK(b.x < 30.0, "a body 1.8 tall walked through a 1-block-high gap (x %g)", b.x);

    // THE JUMP ARC, asserted rather than felt.
    //
    // It has to clear a whole block with room to spare, because placing
    // a block under yourself is how you get out of a hole and it needs
    // the apex to last long enough to press a key in. The first version
    // of this peaked at 0.83 blocks -- it could not clear a one-block
    // ledge -- and nothing in the code said so: it took simulating the
    // arc to see it, which is exactly what this does.
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    phys_body_init(&b, 8.5, 8.0, 8.5);
    {
        double const start = b.y;
        double       peak  = b.y;
        int          up = 0, total = 0;
        b.vy = PL_JUMP;  // as player_tick does on the ground
        for (int t = 0; t < 200; t++) {
            // phys_move then phys_gravity: exactly what player_tick
            // does, because it is the same two calls and not a copy of
            // them.
            phys_move(&b, 0.0, (double)b.vy, 0.0);
            phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
            total++;
            if (b.y > peak) {
                peak = b.y;
                up   = total;
            }
            if (b.on_ground && total > 2) break;
        }
        printf("  jump: apex %.2f blocks, %.2f s up, %.2f s in the air\n", peak - start, (double)up / 20.0,
               (double)total / 20.0);
        CHECK(peak - start > 1.15, "a jump reaches %g blocks: it cannot clear one with room to place under itself",
              peak - start);
        CHECK(peak - start < 2.0, "a jump reaches %g blocks, which clears two: that is a bug, not a feature",
              peak - start);
        CHECK(up >= 6, "a jump reaches its apex in %d ticks (%.2f s); too quick to aim a placement at", up,
              (double)up / 20.0);
        CHECK(b.y > 7.99 && b.y < 8.01, "a jump landed at y %g instead of back on the floor", b.y);
    }

    // And it lands ON a one-block ledge rather than bouncing off it.
    for (int32_t x = 11; x <= 14; x++) {
        for (int32_t z = 6; z <= 10; z++) set_block(x, 8, z, BLK_STONE, 0);
    }
    phys_body_init(&b, 9.5, 8.0, 8.5);
    b.vy = PL_JUMP;
    for (int t = 0; t < 60; t++) {
        phys_move(&b, 0.15, (double)b.vy, 0.0);
        phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
        if (b.on_ground && t > 2) break;
    }
    printf("  jumping onto a 1-block ledge landed at x %.2f, y %.2f\n", b.x, b.y);
    CHECK(b.y > 8.99 && b.y < 9.01, "a jump onto a 1-block ledge ended at y %g, expected 9", b.y);

    // The edge of the world is a wall, not a hole (D-14). A clean
    // world for this one: the obstacles above are in the way.
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 400; i++) phys_move(&b, 0.25, -0.1, 0.0);
    printf("  walking off the resident set stopped at x = %.1f\n", b.x);
    CHECK(b.x < 48.0, "a body walked out of the resident world to x %g", b.x);
}

// A brute-force march, fine enough that it cannot miss a block: the
// SNEAKING: the two rules a body can be given (game/physics.h), which
// are the player's for as long as the toggle is on (game/player.h).
//
// Both of them are rules about what does NOT happen, which is the kind
// that rots quietly: nobody notices that a sneak stopped holding the
// edge until they walk off a tower they were building.
static void check_sneak(void) {
    printf("sneaking\n");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");

    // A cliff. Everything from x = 12 east is cut away to the bedrock,
    // so the lip of the floor is the plane x = 12 and the drop is the
    // whole eight blocks.
    for (int32_t x = 12; x <= 31; x++) {
        for (int32_t z = 0; z <= 15; z++) {
            for (int32_t y = 0; y <= 7; y++) set_block(x, y, z, BLK_AIR, 0);
        }
    }

    phys_body_t b;

    // Walking off it, which is what everyone does by accident and what
    // the rule is FOR.
    phys_body_init(&b, 8.5, 8.0, 8.5);
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.3, 0.0);
    CHECK(b.y < 4.0, "a walking body did not fall off a cliff (y %g)", b.y);

    // ... and not walking off it.
    phys_body_init(&b, 8.5, 8.0, 8.5);
    b.edge_stop = true;
    b.step_up   = 0.0f;
    for (int i = 0; i < 200; i++) phys_move(&b, (double)PL_SNEAK, -0.3, 0.0);
    printf("  sneaking, stopped at x = %.3f with the lip at 12 and a %.1f-wide box\n", b.x, (double)b.w);
    CHECK(b.y > 7.99 && b.y < 8.01 && b.on_ground, "a sneaking body left the ground (y %g)", b.y);
    // HANGING OVER THE EDGE IS THE POINT. The rule is that SOMETHING is
    // under the box, not all of it -- so the far half may be out over
    // the drop, which is what puts the cell under your own feet within
    // reach and makes building outwards possible at all.
    CHECK(b.x > 12.0, "a sneaking body stopped %.2f blocks short of the lip (x %g)", 12.0 - b.x, b.x);
    CHECK(b.x < 12.30, "a sneaking body walked past the last supported point (x %g)", b.x);

    // It still SLIDES along the lip. The rule is asked per axis, so
    // walking into the drop at an angle walks you ALONG it rather than
    // gluing you to the spot -- which is the difference between an edge
    // and a corner you are stuck in.
    double const z0 = b.z;
    for (int i = 0; i < 40; i++) phys_move(&b, (double)PL_SNEAK, -0.3, (double)PL_SNEAK);
    printf("  and slid along it from z %.2f to z %.2f\n", z0, b.z);
    CHECK(b.z > z0 + 1.0, "a sneaking body could not slide along the lip (z %g -> %g)", z0, b.z);
    CHECK(b.y > 7.99, "sliding along the lip dropped the body (y %g)", b.y);

    // The rule holds a body that is STANDING on something. One already
    // in the air -- jumping off, falling, shoved by another animal --
    // is not held by it, or a leap off a tower would stop dead in
    // mid-air over the edge.
    phys_body_init(&b, 11.9, 12.0, 8.5);
    b.edge_stop = true;
    for (int i = 0; i < 20; i++) phys_move(&b, 0.2, -0.2, 0.0);
    CHECK(b.x > 12.5, "a body in the air was held back by the ledge rule (x %g)", b.x);

    // NO CLIMBING. The one-block step that a walk goes straight up
    // (PHYS_STEP, check_physics) has to stop a sneak dead.
    CHECK(flat_world(8) != NULL, "the test world would not rebuild");
    for (int32_t x = 11; x <= 24; x++) {
        for (int32_t z = 6; z <= 10; z++) set_block(x, 8, z, BLK_STONE, 0);
    }
    phys_body_init(&b, 8.5, 8.0, 8.5);
    b.edge_stop = true;
    b.step_up   = 0.0f;
    for (int i = 0; i < 60; i++) phys_move(&b, 0.2, -0.1, 0.0);
    printf("  sneaking at a 1-block step: x %.2f, y %.2f\n", b.x, b.y);
    CHECK(b.y > 7.99 && b.y < 8.01, "a sneaking body climbed a 1-block step (y %g)", b.y);
    CHECK(b.x > 10.6 && b.x < 10.71, "a sneaking body stopped at x %g, expected 10.70 against the step", b.x);

    // ... UNLESS JUMPING (the user's rule). The escape hatch matters as
    // much as the rule: a sneak you cannot get out of an alcove in is a
    // trap, not a mode.
    phys_body_init(&b, 8.5, 8.0, 8.5);
    b.edge_stop = true;
    b.step_up   = 0.0f;
    bool jumped = false;
    for (int i = 0; i < 60; i++) {
        if (b.on_ground && !jumped && b.x > 10.0) {
            b.vy   = PL_JUMP;  // as player_tick does on the ground
            jumped = true;
        }
        phys_move(&b, 0.2, (double)b.vy, 0.0);
        phys_gravity(&b, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
    }
    printf("  and jumping at it from a sneak landed at x %.2f, y %.2f\n", b.x, b.y);
    CHECK(jumped, "the test never jumped");
    CHECK(b.y > 8.99 && b.y < 9.01, "a sneaking body could not jump onto a 1-block step (y %g)", b.y);
}

// reference the DDA has to agree with.
static bool brute_pick(double ox, double oy, double oz, float dx, float dy, float dz, float max, int32_t* bx,
                       int32_t* by, int32_t* bz) {
    float const len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f) return false;
    double const ux = dx / len, uy = dy / len, uz = dz / len;
    for (double t = 0.0; t <= (double)max; t += 0.0005) {
        int32_t const x = (int32_t)floor(ox + ux * t);
        int32_t const y = (int32_t)floor(oy + uy * t);
        int32_t const z = (int32_t)floor(oz + uz * t);
        if (!block_solid(world_block(x, y, z))) continue;
        *bx = x;
        *by = y;
        *bz = z;
        return true;
    }
    return false;
}

static void check_raycast(void) {
    printf("picking\n");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");
    set_block(10, 9, 8, BLK_COBBLE, 0);
    set_block(10, 10, 8, BLK_COBBLE, 0);
    set_block(6, 9, 12, BLK_LOG, 0);

    // The face reported must be the one the ray came in through, and
    // the placement cell must be the empty one in front of it.
    ray_hit_t h;
    CHECK(ray_pick(8.5, 9.5, 8.5, 1.0f, 0.0f, 0.0f, RAY_REACH, RAY_SOLID, &h), "a ray straight at a block missed it");
    printf("  hit (%d,%d,%d) face %u, place at (%d,%d,%d), %.2f blocks away\n", h.x, h.y, h.z, h.face, h.px, h.py,
           h.pz, h.dist);
    CHECK(h.x == 10 && h.y == 9 && h.z == 8, "hit (%d,%d,%d), expected (10,9,8)", h.x, h.y, h.z);
    CHECK(h.face == MESH_DIR_NX, "face %u, expected -x (%u)", h.face, MESH_DIR_NX);
    CHECK(h.px == 9 && h.py == 9 && h.pz == 8, "placement cell (%d,%d,%d), expected (9,9,8)", h.px, h.py, h.pz);
    CHECK(!block_solid(world_block(h.px, h.py, h.pz)), "the placement cell is not empty");

    // Reach: the same ray from further away finds nothing.
    CHECK(!ray_pick(0.5, 9.5, 8.5, 1.0f, 0.0f, 0.0f, RAY_REACH, RAY_SOLID, &h), "a ray reached further than RAY_REACH");

    // A placed torch can be pointed at (F-56): the crosshair ray is the
    // non-solid one, and it must stop at the torch -- while a solid-only
    // ray looks straight through it. Water is looked through by both.
    set_block(8, 9, 11, BLK_TORCH, ST_PLACED);
    CHECK(ray_pick(8.5, 9.5, 8.5, 0.0f, 0.0f, 1.0f, RAY_REACH, RAY_PICKABLE, &h) && h.block == BLK_TORCH && h.z == 11,
          "the crosshair ray does not stop at a placed torch");
    CHECK(!ray_pick(8.5, 9.5, 8.5, 0.0f, 0.0f, 1.0f, RAY_REACH, RAY_SOLID, &h) || h.block != BLK_TORCH,
          "a solid-only ray stopped at a torch");
    set_block(8, 9, 11, BLK_AIR, 0);
    set_block(8, 11, 8, BLK_WATER, 0);
    set_block(8, 10, 8, BLK_WATER, 0);
    CHECK(ray_pick(8.5, 12.5, 8.5, 0.0f, -1.0f, 0.0f, RAY_REACH, RAY_PICKABLE, &h) && h.y == 7,
          "water hid the floor from the crosshair (hit y %d)", h.y);
    set_block(8, 11, 8, BLK_AIR, 0);
    set_block(8, 10, 8, BLK_AIR, 0);

    // Straight down finds the floor.
    CHECK(ray_pick(8.5, 12.0, 8.5, 0.0f, -1.0f, 0.0f, RAY_REACH, RAY_SOLID, &h), "a ray straight down missed the floor");
    CHECK(h.y == 7 && h.face == MESH_DIR_PY, "downward ray hit y %d face %u, expected y 7 face +y", h.y, h.face);

    // An unloaded chunk is solid to the BODY (D-14) and invisible to
    // the PICKER. Reporting it would put a highlight box round a piece
    // of fog and offer to mine it.
    {
        ray_hit_t hb;
        CHECK(!ray_pick(8.5, 9.5, 8.5, 0.0f, 0.0f, -1.0f, 200.0f, RAY_SOLID, &hb),
              "a ray picked BLK_BARRIER: the edge of the loaded world is not a block");
        phys_body_t edge;
        phys_body_init(&edge, 8.5, 9.0, 8.5);
        CHECK(!phys_fits(&edge, 8.5, 9.0, -100.0), "an unloaded chunk is not solid to the body (D-14)");
    }

    // And the real test: a fan of directions, every one of which must
    // agree with a brute-force march. A DDA that skips a corner is the
    // classic bug and it is invisible until someone mines through a
    // wall diagonally.
    int checked = 0, agreed = 0;
    for (int a = 0; a < 64; a++) {
        for (int e = -12; e <= 12; e += 3) {
            float const yaw = (float)a * 0.0982f, pitch = (float)e * 0.09f;
            float       dx, dy, dz;
            ray_forward(yaw, pitch, &dx, &dy, &dz);
            double const ox = 8.37, oy = 9.61, oz = 8.23;  // deliberately not on a boundary
            int32_t      bx = 0, by = 0, bz = 0;
            bool const   want = brute_pick(ox, oy, oz, dx, dy, dz, RAY_REACH, &bx, &by, &bz);
            bool const   got  = ray_pick(ox, oy, oz, dx, dy, dz, RAY_REACH, RAY_SOLID, &h);
            checked++;
            CHECK(want == got, "yaw %g pitch %g: brute force says %d, the DDA says %d", (double)yaw, (double)pitch,
                  want, got);
            if (!want || !got) continue;
            CHECK(h.x == bx && h.y == by && h.z == bz, "yaw %g pitch %g: DDA hit (%d,%d,%d), brute force (%d,%d,%d)",
                  (double)yaw, (double)pitch, h.x, h.y, h.z, bx, by, bz);
            agreed++;
            if (s_fail) return;
        }
    }
    printf("  %d directions checked against a brute-force march, %d hits, all agreed\n", checked, agreed);
}

// The floor of the world must be unbreakable, or it can be mined out.
// Nobody falls THROUGH -- world_block() answers BLK_BARRIER below y = 0
// -- but a hole in the bottom of the world is still a hole, and until
// 2026-09-28 the bottom layer was ordinary stone.
static void check_world_floor(void) {
    printf("the floor of the world\n");
    CHECK(block_def(BLK_BEDROCK)->hardness == HARDNESS_UNBREAKABLE, "bedrock is breakable");
    CHECK(block_def(BLK_STONE)->hardness != HARDNESS_UNBREAKABLE,
          "stone is unbreakable, which would make the rest of this prove nothing");

    static uint8_t id[CH_CELLS], st[CH_CELLS];
    chunk_t        c;
    // Ordinary ground, far from the origin, and the Far Lands -- which
    // has a generator of its own and lays Beta's ragged bedrock, so it
    // is the one most likely to disagree.
    struct {
        int32_t cx, cz;
    } const WHERE[] = {{0, 0}, {-40, 17}, {6250, -6250}, {(FARLANDS_X_DEFAULT / CH_W) - 2, 3}};
    for (size_t i = 0; i < sizeof WHERE / sizeof WHERE[0]; i++) {
        gen_into(&c, id, st, WHERE[i].cx, WHERE[i].cz, GEN_SEED);
        int wrong = 0, placed = 0;
        for (int z = 0; z < CH_D; z++) {
            for (int x = 0; x < CH_W; x++) {
                if (id[CH_IDX(x, CH_BEDROCK, z)] != BLK_BEDROCK) wrong++;
                if (st[CH_IDX(x, CH_BEDROCK, z)] != 0) placed++;
            }
        }
        CHECK(wrong == 0, "chunk (%d,%d): %d floor cell(s) are not bedrock", WHERE[i].cx, WHERE[i].cz, wrong);
        CHECK(placed == 0, "chunk (%d,%d): %d floor cell(s) carry state", WHERE[i].cx, WHERE[i].cz, placed);
    }
    printf("  y=0 is bedrock in ordinary chunks and in the Far Lands\n");

    // FORCED LAST, so no generator can punch through it. Carve the floor
    // out by hand and re-force it: the property is that the forcing
    // REPAIRS, not that worldgen happens to write bedrock in passing.
    // This is also what heals a world saved before the rule existed.
    gen_into(&c, id, st, 0, 0, GEN_SEED);
    for (int i = 0; i < 8; i++) {
        id[CH_IDX(i, CH_BEDROCK, 0)] = BLK_AIR;
        st[CH_IDX(i, CH_BEDROCK, 0)] = ST_PLACED;
    }
    worldgen_force_floor(&c);
    int holes = 0, dirt = 0;
    for (int i = 0; i < 8; i++) {
        holes += id[CH_IDX(i, CH_BEDROCK, 0)] != BLK_BEDROCK;
        dirt += st[CH_IDX(i, CH_BEDROCK, 0)] != 0;
    }
    CHECK(holes == 0, "forcing the floor left %d hole(s): a cave generator could open the world", holes);
    CHECK(dirt == 0, "forcing the floor left %d cell(s) carrying state", dirt);
    printf("  a hole punched in the floor is closed again, state and all\n");
}

// A stack of cactus comes down with the block it stood on (BF2_STACKED),
// which is support rather than the felling rule -- so it ignores
// ST_PLACED, and it goes straight up one column.
static void check_stacked(void) {
    printf("stacked blocks\n");
    CHECK(block_stacked(BLK_CACTUS), "cactus is not a stacked block");
    CHECK(!block_stacked(BLK_LOG), "a log is stacked, which would make trees collapse");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");
    item_entity_reset();

    int32_t const x = 6, z = 6, base = 8;
    for (int i = 0; i < 4; i++) set_block(x, base + i, z, BLK_CACTUS, 0);
    // One a PLAYER planted, on top: support does not care who put it
    // there, so it must come down too.
    set_block(x, base + 4, z, BLK_CACTUS, ST_PLACED);
    // A neighbouring cactus, touching: a separate plant, untouched.
    for (int i = 0; i < 3; i++) set_block(x + 1, base + i, z, BLK_CACTUS, 0);

    break_result_t r = interact_break(x, base + 1, z, ITEM_NONE);
    printf("  breaking the second of five took %d block(s)\n", r.felled);
    CHECK(r.ok, "breaking a cactus failed");
    CHECK(!r.was_tree, "a cactus reported itself as a tree");
    CHECK(r.felled == 4, "breaking the second of five took %d, expected 4", r.felled);
    CHECK(world_block(x, base, z) == BLK_CACTUS, "the cactus below the break came down too");
    for (int i = 1; i < 5; i++) {
        CHECK(world_block(x, base + i, z) == BLK_AIR, "cactus left standing at +%d", i);
    }
    int neighbour = 0;
    for (int i = 0; i < 3; i++) neighbour += world_block(x + 1, base + i, z) == BLK_CACTUS;
    CHECK(neighbour == 3, "the cactus next door lost %d block(s)", 3 - neighbour);

    // Something resting ON a cactus is not part of the stack.
    set_block(x + 1, base + 3, z, BLK_PLANKS, ST_PLACED);
    r = interact_break(x + 1, base, z, ITEM_NONE);
    CHECK(r.felled == 3, "breaking the bottom took %d, expected the 3 cactus", r.felled);
    CHECK(world_block(x + 1, base + 3, z) == BLK_PLANKS, "the planks on top came down with the cactus");
    printf("  a block resting on top is left where it was\n");
}

static void check_felling(void) {
    printf("the logging rule\n");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");
    item_entity_reset();

    // A tree: a trunk with a canopy, all GROWN (no ST_PLACED).
    int32_t const tx = 8, tz = 8, base = 8;
    for (int y = 0; y < 5; y++) set_block(tx, base + y, tz, BLK_LOG, 0);
    int leaves = 0;
    for (int dy = 3; dy <= 5; dy++) {
        for (int dz = -2; dz <= 2; dz++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dz == 0 && dy < 5) continue;
                set_block(tx + dx, base + dy, tz + dz, BLK_LEAVES, 0);
                leaves++;
            }
        }
    }
    // A SECOND tree far enough away that its canopy does not touch.
    for (int y = 0; y < 4; y++) set_block(tx + 12, base + y, tz, BLK_LOG, 0);

    // A player-placed log, in the first tree's trunk.
    set_block(tx, base + 2, tz, BLK_LOG, ST_PLACED);

    // Breaking the PLACED one takes exactly that block.
    break_result_t r = interact_break(tx, base + 2, tz, ITEM_AXE_STONE);
    printf("  breaking a placed log took %d block(s), tree=%d\n", r.felled, (int)r.was_tree);
    CHECK(r.ok, "breaking a placed log failed");
    CHECK(!r.was_tree, "breaking a PLACED log felled the tree");
    CHECK(r.felled == 1, "breaking a placed log took %d blocks, expected 1", r.felled);
    CHECK(world_block(tx, base + 3, tz) == BLK_LOG, "the trunk above a placed log was taken");

    // Breaking a GROWN one fells everything from there up.
    r = interact_break(tx, base + 3, tz, ITEM_AXE_STONE);
    printf("  breaking a grown log took %d block(s), tree=%d\n", r.felled, (int)r.was_tree);
    CHECK(r.ok && r.was_tree, "breaking a grown log did not fell the tree");
    CHECK(r.felled > 20, "felling took only %d blocks; the canopy should have gone too", r.felled);

    // The stump stays: y >= the broken block, never below.
    CHECK(world_block(tx, base, tz) == BLK_LOG, "felling took the stump at the bottom of the trunk");
    CHECK(world_block(tx, base + 1, tz) == BLK_LOG, "felling reached below the block that was broken");
    // Everything at and above it is gone.
    CHECK(world_block(tx, base + 4, tz) == BLK_AIR, "felling left a log above the break");
    int left = 0;
    for (int dy = 3; dy <= 5; dy++) {
        for (int dz = -2; dz <= 2; dz++) {
            for (int dx = -2; dx <= 2; dx++) left += world_block(tx + dx, base + dy, tz + dz) == BLK_LEAVES;
        }
    }
    CHECK(left == 0, "%d leaves survived the fell", left);

    // A LEAF is an ordinary break, however it got there. The fell still
    // spreads through leaves -- the canopy went with the trunk above --
    // but cutting one must not bring a tree down.
    int32_t const lx = tx + 12, lz = tz;                  // the second tree
    for (int y = 0; y < 4; y++) set_block(lx, base + y, lz, BLK_LOG, 0);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) set_block(lx + dx, base + 4, lz + dz, BLK_LEAVES, 0);
    r = interact_break(lx - 1, base + 4, lz, ITEM_AXE_STONE);
    printf("  breaking a grown leaf took %d block(s), tree=%d\n", r.felled, (int)r.was_tree);
    CHECK(r.ok, "breaking a leaf failed");
    CHECK(!r.was_tree, "breaking a LEAF felled the tree");
    CHECK(r.felled == 1, "breaking a leaf took %d blocks, expected 1", r.felled);
    int standing = 0;
    for (int y = 0; y < 4; y++) standing += world_block(lx, base + y, lz) == BLK_LOG;
    CHECK(standing == 4, "cutting a leaf took %d logs off the trunk under it", 4 - standing);

    // The NEIGHBOUR is untouched. This is the bound that matters: one
    // tree must never take the forest.
    int neighbour = 0;
    for (int y = 0; y < 4; y++) neighbour += world_block(tx + 12, base + y, tz) == BLK_LOG;
    printf("  the neighbouring tree still has %d of its 4 logs\n", neighbour);
    CHECK(neighbour == 4, "felling one tree took %d logs off a tree 12 blocks away", 4 - neighbour);

    // Placing sets ST_PLACED, which is what makes all of the above work.
    ray_hit_t h = {.x = 20, .y = base, .z = 20, .px = 20, .py = base, .pz = 20, .face = MESH_DIR_PY};
    CHECK(interact_place(&h, BLK_LOG, NULL), "placing a log failed");
    CHECK(world_block(20, base, 20) == BLK_LOG, "the placed log is not there");
    CHECK((world_state(20, base, 20) & ST_PLACED) != 0, "a placed block does not have ST_PLACED set");
    r = interact_break(20, base, 20, ITEM_AXE_STONE);
    CHECK(!r.was_tree && r.felled == 1, "a just-placed log felled as a tree");
}

static void check_items(void) {
    printf("items and the inventory\n");

    // The two id spaces meet without a gap, which is what lets a block
    // be an item without a second table to keep in step.
    CHECK(item_is_block(BLK_COBBLE), "a block id is not an item");
    CHECK(!item_is_block(ITEM_COAL), "coal is being treated as a block");
    CHECK(item_block(BLK_COBBLE) == BLK_COBBLE, "a block item does not place its own block");
    CHECK(item_block(ITEM_COAL) == BLK_AIR, "coal claims to place a block");
    CHECK(item_def(BLK_COBBLE).name != NULL && item_def(BLK_COBBLE).name[0] != 0,
          "a block item has no name; the block table should have supplied it");

    // Tools speed up their own class and nothing else. A pickaxe that
    // digs dirt faster makes carrying a shovel pointless.
    int const stone_hand  = item_break_ticks(BLK_STONE, 0);
    int const stone_pick  = item_break_ticks(BLK_STONE, ITEM_PICK_STONE);
    int const stone_shov  = item_break_ticks(BLK_STONE, ITEM_SHOVEL_STONE);
    int const dirt_hand   = item_break_ticks(BLK_DIRT, 0);
    int const dirt_shovel = item_break_ticks(BLK_DIRT, ITEM_SHOVEL_STONE);
    printf("  stone: %d ticks by hand, %d with a stone pickaxe, %d with a shovel\n", stone_hand, stone_pick,
           stone_shov);
    printf("  dirt:  %d ticks by hand, %d with a stone shovel\n", dirt_hand, dirt_shovel);
    CHECK(stone_pick < stone_hand, "a pickaxe does not speed up stone");
    CHECK(stone_shov == stone_hand, "a shovel speeds up stone; only the right class should");
    CHECK(dirt_shovel < dirt_hand, "a shovel does not speed up dirt");
    CHECK(item_break_ticks(BLK_BARRIER, ITEM_PICK_STONE) < 0, "the edge of the world is breakable");

    // AND BY HOW MUCH. The user's complaint was that the wrong tool was
    // not "a lot slower" -- it was 2x for wood and 3x for stone, which
    // next to a fist is nothing. The multiplier is 2 x the tool level,
    // and these are the numbers that say so rather than an ordering
    // that would still pass at 1.1x.
    int const stone_wood = item_break_ticks(BLK_STONE, ITEM_PICK_WOOD);
    CHECK(stone_hand / stone_wood == 2, "a wooden pickaxe is %dx a fist, wanted 2x", stone_hand / stone_wood);
    CHECK(stone_hand / stone_pick == 4, "a stone pickaxe is %dx a fist, wanted 4x", stone_hand / stone_pick);

    // A block the Use key opens rather than places against, and the
    // registry is what says so -- main.c only decides what it opens.
    CHECK(block_usable(BLK_CRAFTING_TABLE), "a crafting table cannot be opened");
    CHECK(!block_usable(BLK_STONE), "stone opens something");
    CHECK(!block_usable(BLK_PLANKS), "planks open something");

    // Too soft a tool still breaks the block; it just yields nothing.
    CHECK(!item_can_harvest(BLK_STONE, 0), "bare hands harvest stone");
    CHECK(item_can_harvest(BLK_STONE, ITEM_PICK_WOOD), "a wooden pickaxe cannot harvest stone");
    CHECK(item_can_harvest(BLK_DIRT, 0), "bare hands cannot harvest dirt");

    // --- Stacking ----------------------------------------------------
    inventory_t inv;
    inv_clear(&inv);
    CHECK(inv_add(&inv, BLK_COBBLE, 10, 0) == 0, "10 cobblestone would not fit in an empty inventory");
    CHECK(inv_add(&inv, BLK_COBBLE, 10, 0) == 0, "a second 10 would not fit");
    CHECK(inv_count(&inv, BLK_COBBLE) == 20, "20 cobblestone are not all there: %d", inv_count(&inv, BLK_COBBLE));

    // THE POINT: they must be in ONE slot, not two of ten. A partial
    // stack has to be filled before an empty slot is taken.
    int used = 0;
    for (int i = 0; i < INV_SLOTS; i++) used += inv.slot[i].item != 0;
    printf("  20 cobblestone in %d slot(s)\n", used);
    CHECK(used == 1, "20 cobblestone are spread over %d slots; partial stacks are not being filled first", used);

    // Overflow goes to a second slot, and the whole inventory fills.
    inv_clear(&inv);
    int const cap  = INV_SLOTS * ITEM_STACK_MAX;
    int const left = inv_add(&inv, BLK_DIRT, cap + 7, 0);
    printf("  an inventory holds %d dirt; %d of %d were refused\n", cap, left, cap + 7);
    CHECK(left == 7, "a full inventory refused %d, expected 7", left);
    CHECK(inv_count(&inv, BLK_DIRT) == cap, "a full inventory holds %d, expected %d", inv_count(&inv, BLK_DIRT), cap);

    // Tools never stack, and two differently-worn ones stay two.
    inv_clear(&inv);
    inv_add(&inv, ITEM_PICK_STONE, 1, 0);
    inv_add(&inv, ITEM_PICK_STONE, 1, 40);
    used = 0;
    for (int i = 0; i < INV_SLOTS; i++) used += inv.slot[i].item != 0;
    CHECK(used == 2, "two pickaxes merged into %d slot(s): one of them silently repaired", used);

    // --- Durability ---------------------------------------------------
    inv_clear(&inv);
    inv_add(&inv, ITEM_PICK_WOOD, 1, 0);
    uint16_t const life = item_def(ITEM_PICK_WOOD).durability;
    int            uses = 0;
    while (uses < 1000) {
        uses++;
        if (inv_wear_held(&inv, 1)) break;
    }
    printf("  a wooden pickaxe lasted %d uses (durability %u)\n", uses, life);
    CHECK(uses == (int)life, "a pickaxe lasted %d uses, expected %u", uses, life);
    CHECK(inv_held(&inv)->item == 0, "a broken tool left something in the slot");
    CHECK(!inv_wear_held(&inv, 1), "wearing an empty slot reported a break");

    // A block never wears, however hard it is swung.
    inv_clear(&inv);
    inv_add(&inv, BLK_COBBLE, 5, 0);
    CHECK(!inv_wear_held(&inv, 100), "a stack of cobblestone broke like a tool");
    CHECK(inv_held(&inv)->count == 5, "wearing a block stack changed its count");

    // Placing consumes exactly one.
    CHECK(inv_consume_held(&inv), "placing from a stack of 5 failed");
    CHECK(inv_held(&inv)->count == 4, "placing took %d, expected 1", 5 - inv_held(&inv)->count);
}

// The Tab screen draws the hotbar at the BOTTOM and the storage rows
// above it, the reverse of slot order. The cursor has to move the way
// the screen looks, which it did not: up from the hotbar went nowhere.
// ---------------------------------------------------------------------
//  Crafting (Part C)
//
//  The grid is gone, so the old promise -- "every recipe resolves at
//  every legal grid offset; no two collide" -- went with it. Nothing
//  infers a recipe from a pile of ingredients any more, which is why a
//  pickaxe and an axe may both want three planks and two sticks. What
//  has to be true instead is below.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  Block entities, and the furnace that runs on them
//
//  The chunk format has had a number for these since Part W and never a
//  byte in one, so this section is the first thing that has ever proved
//  the section machinery works at all.
// ---------------------------------------------------------------------

static void check_autocraft(void) {
    printf("crafting: making what it needs first\n");

    recipe_t const* pick = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->out == ITEM_PICK_WOOD) pick = recipe_at(i);
    }
    CHECK(pick != NULL, "there is no wooden pickaxe recipe");
    if (pick == NULL) return;

    // THE USER'S OWN EXAMPLE: "when we need a pickaxe, but only have
    // blocks of wood". Two logs is eight planks; three of those and two
    // sticks (which are another two planks) is a pickaxe.
    inventory_t inv;
    inv_clear(&inv);
    inv_add(&inv, BLK_LOG, 2, 0);
    CHECK(recipe_can_make(pick, &inv, 1) == 0, "a pickaxe can be made directly out of logs");
    CHECK(recipe_can_make_auto(pick, &inv, RS_TABLE) == 1, "two logs should reach a wooden pickaxe");
    CHECK(recipe_make_auto(pick, &inv, 1, RS_TABLE) == 1, "the planner refused a pickaxe it said it could make");
    CHECK(inv_count(&inv, ITEM_PICK_WOOD) == 1, "no pickaxe came out of the plan");
    CHECK(inv_count(&inv, BLK_LOG) == 0, "the plan left a log unused");
    printf("  2 logs -> 8 planks -> 4 sticks -> a pickaxe, and %d planks over\n", inv_count(&inv, BLK_PLANKS));

    // NOT ENOUGH, AND NOTHING TAKEN. One log cannot reach a pickaxe,
    // and the attempt must leave the log alone rather than turn it into
    // planks the player never asked for -- this is the half-finished
    // plan, and it is the reason the snapshot wraps the WHOLE plan.
    inv_clear(&inv);
    inv_add(&inv, BLK_LOG, 1, 0);
    CHECK(recipe_can_make_auto(pick, &inv, RS_TABLE) == 0, "one log reached a pickaxe");
    CHECK(recipe_make_auto(pick, &inv, 1, RS_TABLE) == 0, "one log made a pickaxe");
    CHECK(inv_count(&inv, BLK_LOG) == 1, "the failed plan ate the log");
    CHECK(inv_count(&inv, BLK_PLANKS) == 0, "the failed plan left %d planks behind",
          inv_count(&inv, BLK_PLANKS));

    // ASKING DOES NOT DO. recipe_can_make_auto works on a copy, so the
    // inventory it was asked about must be untouched.
    inv_clear(&inv);
    inv_add(&inv, BLK_LOG, 2, 0);
    (void)recipe_can_make_auto(pick, &inv, RS_TABLE);
    CHECK(inv_count(&inv, BLK_LOG) == 2 && inv_count(&inv, BLK_PLANKS) == 0,
          "asking whether a plan works carried it out");

    // THE STATION STILL COUNTS. The same two logs at no table reach
    // planks and sticks, and stop there.
    inv_clear(&inv);
    inv_add(&inv, BLK_LOG, 2, 0);
    CHECK(recipe_can_make_auto(pick, &inv, RS_INVENTORY) == 0, "a pickaxe was planned with no crafting table");

    // AND IT NEVER SMELTS. An iron pickaxe from iron ORE would mean the
    // planner lighting a furnace on the player's behalf.
    recipe_t const* iron = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->out == ITEM_PICK_IRON) iron = recipe_at(i);
    }
    CHECK(iron != NULL, "there is no iron pickaxe recipe");
    if (iron != NULL) {
        inv_clear(&inv);
        inv_add(&inv, BLK_IRON_ORE, 8, 0);
        inv_add(&inv, BLK_LOG, 4, 0);
        CHECK(recipe_can_make_auto(iron, &inv, RS_TABLE) == 0, "the planner smelted iron ore by itself");
        CHECK(inv_count(&inv, BLK_IRON_ORE) == 8, "the planner consumed ore it could not use");
    }

    // The depth stop is real, and nothing recurses forever: every
    // recipe planned from an empty pack terminates and makes nothing.
    inv_clear(&inv);
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station == RS_FURNACE) continue;
        CHECK(recipe_make_auto(r, &inv, 1, RS_TABLE) == 0, "%s was made out of an empty pack",
              item_def(r->out).name);
    }
}

static void check_trash(void) {
    printf("the trashcan: it empties by the clock, not by a button\n");

    blockent_clear();
    blockent_t* bin = blockent_add(0, 64, 0, BE_TRASH);
    CHECK(bin != NULL, "could not make a trashcan");
    if (bin == NULL) return;
    bin->slot[0] = (inv_slot_t){BLK_COBBLE, 64, 0};
    bin->slot[1] = (inv_slot_t){ITEM_PICK_WOOD, 1, 30};
    bin->stamp   = 0;

    // A MINUTE SHORT AND IT IS ALL STILL THERE.
    CHECK(blockent_rot_trash(bin, BE_TRASH_TICKS - 1) == 0, "the bin emptied early");
    CHECK(bin->slot[0].count == 64, "the bin lost something before its time");

    // ... and on the tick it is due, it goes.
    CHECK(blockent_rot_trash(bin, BE_TRASH_TICKS) == 2, "the bin did not empty on time");
    CHECK(bin->slot[0].item == 0 && bin->slot[1].item == 0, "the bin kept something");
    CHECK(bin->stamp == BE_TRASH_TICKS, "the bin did not restamp itself");

    // A CHEST IS NOT A BIN, however long it is left.
    blockent_t* box = blockent_add(1, 64, 0, BE_CHEST);
    box->slot[0]    = (inv_slot_t){BLK_COBBLE, 64, 0};
    box->stamp      = 0;
    CHECK(blockent_rot_trash(box, 0xFFFFFFFEu) == 0, "a chest rotted");
    CHECK(box->slot[0].count == 64, "a chest lost what was in it");

    // A stamp from the future is not an instant emptying.
    bin->slot[0] = (inv_slot_t){BLK_SAND, 10, 0};
    bin->stamp   = 9000;
    CHECK(blockent_rot_trash(bin, 10) == 0, "a clock that went backwards emptied the bin");
    CHECK(bin->slot[0].count == 10, "the bin lost sand to a clock that went backwards");

    printf("  %u ticks is %u minutes of playing\n", (unsigned)BE_TRASH_TICKS, (unsigned)BE_TRASH_MINUTES);
    blockent_clear();
}

static void check_bench(void) {
    printf("the bench: what comes apart, and what does not\n");

    // WHAT THE USER SAID MUST NOT REVERSE. These are their own
    // examples, so they are named here rather than counted.
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        bool const      rev = (r->flags & RF_REVERSIBLE) != 0;
        if (r->out == ITEM_IRON_INGOT || r->out == ITEM_STICK || r->out == BLK_PLANKS || r->out == BLK_TORCH ||
            r->out == BLK_GLASS || r->out == BLK_STONE) {
            CHECK(!rev, "%s can be taken apart, and should not be", item_def(r->out).name);
        }
        if (r->out == ITEM_PICK_WOOD || r->out == BLK_CHEST || r->out == BLK_FURNACE ||
            r->out == BLK_CRAFTING_TABLE) {
            CHECK(rev, "%s cannot be taken apart, and should be", item_def(r->out).name);
        }
    }

    // A worn tool gives back its FULL ingredient list (the user's
    // call). Three planks and two sticks, whatever state it is in.
    recipe_t const* pick = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->out == ITEM_PICK_WOOD) pick = recipe_at(i);
    }
    if (pick == NULL) return;

    inventory_t inv;
    inv_clear(&inv);
    inv_add(&inv, ITEM_PICK_WOOD, 1, 55);  // nearly spent: 60 uses, 55 gone
    CHECK(inv_take(&inv, ITEM_PICK_WOOD, pick->out_n), "a worn pickaxe could not be taken off the bench");
    for (int i = 0; i < pick->n_in; i++) {
        CHECK(inv_add(&inv, pick->in[i].item, pick->in[i].count, 0) == 0, "the pieces would not fit back");
    }
    CHECK(inv_count(&inv, BLK_PLANKS) == 3 && inv_count(&inv, ITEM_STICK) == 2,
          "a 92%% worn pickaxe gave back %d planks and %d sticks, wanted 3 and 2", inv_count(&inv, BLK_PLANKS),
          inv_count(&inv, ITEM_STICK));
    printf("  a nearly-spent pickaxe still gives back 3 planks and 2 sticks\n");
}

static void check_iron(void) {
    printf("iron: the one block that refuses the swing\n");

    CHECK(block_tool_required(BLK_IRON_ORE), "iron ore does not require a tool");
    CHECK(!block_tool_required(BLK_STONE), "stone requires a tool, and should only be slow without one");
    CHECK(!block_tool_required(BLK_COAL_ORE), "coal ore requires a tool; a wooden pickaxe should get it");

    CHECK(!item_can_harvest(BLK_IRON_ORE, ITEM_PICK_WOOD), "a wooden pickaxe harvests iron");
    CHECK(item_can_harvest(BLK_IRON_ORE, ITEM_PICK_STONE), "a stone pickaxe cannot harvest iron");
    CHECK(item_can_harvest(BLK_COAL_ORE, ITEM_PICK_WOOD), "a wooden pickaxe cannot harvest coal");
    CHECK(item_can_harvest(BLK_STONE, ITEM_PICK_WOOD), "a wooden pickaxe cannot harvest stone");

    // The refusal has to NAME the tool, or it is indistinguishable
    // from a bug -- which is the whole reason Minecraft chose the other
    // rule (blocks.h, BF2_TOOL_REQUIRED).
    block_def_t const* d = block_def(BLK_IRON_ORE);
    CHECK(item_tool_for(d->tool, d->tool_level) == ITEM_PICK_STONE, "iron ore cannot say what it wants");
    CHECK(item_tool_for(TOOL_PICK, 3) == ITEM_PICK_IRON, "there is no level-3 pickaxe to name");
    CHECK(item_tool_for(TOOL_NONE, 0) == 0, "a tool class of none named something");

    // Every block that refuses a swing must be able to say what it
    // wants, or a player is left hitting it with no idea why.
    for (int b = 0; b < BLK_COUNT; b++) {
        if (!block_tool_required((uint8_t)b)) continue;
        block_def_t const* def = block_def((uint8_t)b);
        CHECK(item_tool_for(def->tool, def->tool_level) != 0, "%s refuses the swing and cannot name a tool",
              def->name);
    }

    // Iron takes 6x off a bare fist with the tool that finally opens it.
    int const by_hand = item_break_ticks(BLK_IRON_ORE, 0);
    int const by_iron = item_break_ticks(BLK_IRON_ORE, ITEM_PICK_IRON);
    CHECK(by_hand / by_iron == 6, "an iron pickaxe is %dx a fist on iron ore, wanted 6", by_hand / by_iron);
    printf("  iron ore: %d ticks by hand, %d with an iron pickaxe\n", by_hand, by_iron);
}

static void check_blockent(void) {
    printf("block entities: blocks that remember\n");

    blockent_clear();
    CHECK(blockent_count() == 0, "the pool did not start empty");
    CHECK(blockent_at(0, 0, 0) == NULL, "an empty pool found something");

    blockent_t* a = blockent_add(10, 64, -20, BE_FURNACE);
    CHECK(a != NULL, "could not make a furnace record");
    CHECK(blockent_at(10, 64, -20) == a, "the record is not where it was put");
    CHECK(blockent_at(10, 65, -20) == NULL, "a record answered for the cell above it");
    CHECK(blockent_count() == 1, "the pool holds %d records, wanted 1", blockent_count());

    // Two cells one apart in each axis are three different records,
    // which is the bug a position hash would have.
    blockent_add(11, 64, -20, BE_FURNACE);
    blockent_add(10, 64, -21, BE_FURNACE);
    CHECK(blockent_count() == 3, "three cells did not make three records");

    blockent_remove(11, 64, -20);
    CHECK(blockent_at(11, 64, -20) == NULL, "a removed record is still there");
    CHECK(blockent_count() == 2, "removing one took %d", 3 - blockent_count());

    // FULL IS REFUSED, NOT SILENTLY DROPPED. A chest that quietly did
    // not become a chest is a chest somebody puts their things in.
    blockent_clear();
    for (int i = 0; i < BE_MAX; i++) {
        CHECK(blockent_add(i, 64, 0, BE_CHEST) != NULL, "the pool refused record %d of %d", i, BE_MAX);
    }
    CHECK(blockent_count() == BE_MAX, "the pool holds %d, wanted %d", blockent_count(), BE_MAX);
    CHECK(blockent_add(9999, 64, 0, BE_CHEST) == NULL, "a full pool accepted another record");

    // Eviction takes a chunk's records and NOTHING ELSE.
    blockent_clear();
    blockent_add(0, 64, 0, BE_FURNACE);      // chunk (0, 0)
    blockent_add(3, 64, 5, BE_FURNACE);      // chunk (0, 0)
    blockent_add(CH_W + 1, 64, 0, BE_CHEST); // chunk (1, 0)
    CHECK(blockent_count_in(0, 0) == 2, "two records in chunk 0,0 counted as %d", blockent_count_in(0, 0));
    blockent_drop_chunk(0, 0);
    CHECK(blockent_count_in(0, 0) == 0, "evicting chunk 0,0 left records behind");
    CHECK(blockent_count() == 1, "evicting one chunk took the other chunk's records");

    // --- The round trip, which is the whole point ---------------------
    blockent_clear();
    blockent_t* f = blockent_add(5, 70, 9, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT]  = (inv_slot_t){BLK_LOG, 7, 0};
    f->slot[BE_FURNACE_FUEL]   = (inv_slot_t){ITEM_COAL, 2, 0};
    f->slot[BE_FURNACE_OUTPUT] = (inv_slot_t){ITEM_COAL, 3, 0};
    f->burn_left               = 900;
    f->burn_max                = 1600;
    f->cook                    = 77;
    f->stamp                   = 123456;
    blockent_t* c = blockent_add(6, 70, 9, BE_CHEST);
    c->slot[0]                 = (inv_slot_t){ITEM_PICK_WOOD, 1, 42};
    c->slot[26]                = (inv_slot_t){BLK_COBBLE, 64, 0};

    static uint8_t sec[CHUNK_SECTIONS_MAX];
    size_t const   n = blockent_encode_chunk(0, 0, sec, sizeof(sec));
    CHECK(n > 0, "two records would not encode");
    CHECK(sec[0] == SECTION_BLOCK_ENTITIES, "the section has the wrong id");
    printf("  a furnace and a chest encode to %u bytes\n", (unsigned)n);

    blockent_clear();
    // Past the u8 id and the u32 length: the CONTENTS, as
    // chunk_decode_ex hands them over.
    blockent_decode_section(sec + 5, n - 5);
    CHECK(blockent_count() == 2, "%d records came back, wanted 2", blockent_count());

    blockent_t const* f2 = blockent_at(5, 70, 9);
    CHECK(f2 != NULL, "the furnace did not come back");
    if (f2 != NULL) {
        CHECK(f2->kind == BE_FURNACE, "the furnace came back as kind %d", f2->kind);
        CHECK(f2->slot[BE_FURNACE_INPUT].item == BLK_LOG && f2->slot[BE_FURNACE_INPUT].count == 7,
              "the furnace's input did not survive");
        CHECK(f2->slot[BE_FURNACE_OUTPUT].count == 3, "the furnace's output did not survive");
        CHECK(f2->burn_left == 900 && f2->cook == 77 && f2->stamp == 123456,
              "the furnace's fire did not survive: burn %u cook %u stamp %u", f2->burn_left, f2->cook,
              f2->stamp);
    }
    blockent_t const* c2 = blockent_at(6, 70, 9);
    CHECK(c2 != NULL, "the chest did not come back");
    if (c2 != NULL) {
        CHECK(c2->slot[0].item == ITEM_PICK_WOOD && c2->slot[0].wear == 42, "a worn tool did not survive a chest");
        CHECK(c2->slot[26].count == 64, "the chest's last slot did not survive");
    }

    // A TRUNCATED SECTION MUST STOP, NOT WALK OFF THE END. Every
    // truncation of it, one byte at a time -- the same treatment the
    // MIDI files get (F-72), and for the same reason: this is a file
    // off somebody's SD card.
    for (size_t cut = 5; cut < n; cut++) {
        blockent_clear();
        blockent_decode_section(sec + 5, cut - 5);
        CHECK(blockent_count() <= 2, "a %u-byte section produced %d records", (unsigned)cut, blockent_count());
    }
    blockent_clear();
    printf("  survives all %u truncations\n", (unsigned)(n - 5));
}

static void check_furnace(void) {
    printf("the furnace: it never ticks, it catches up\n");

    // What burns, and what does not.
    CHECK(furnace_is_fuel(ITEM_COAL), "coal does not burn");
    CHECK(furnace_is_fuel(BLK_LOG), "a log does not burn");
    CHECK(furnace_is_fuel(BLK_PLANKS), "planks do not burn");
    CHECK(furnace_is_fuel(ITEM_STICK), "a stick does not burn");
    CHECK(furnace_is_fuel(ITEM_PICK_WOOD), "a wooden pickaxe does not burn");
    CHECK(!furnace_is_fuel(ITEM_PICK_STONE), "a stone pickaxe burns");
    CHECK(!furnace_is_fuel(BLK_COBBLE), "cobblestone burns");
    CHECK(!furnace_is_fuel(BLK_SAND), "sand burns");

    // What smelts.
    CHECK(furnace_smelts_to(BLK_LOG) == ITEM_COAL, "a log does not become coal");
    CHECK(furnace_smelts_to(BLK_SAND) == BLK_GLASS, "sand does not become glass");
    CHECK(furnace_smelts_to(BLK_COBBLE) == BLK_STONE, "cobblestone does not become stone");
    CHECK(furnace_smelts_to(ITEM_COAL) == 0, "coal smelts into something");

    blockent_clear();
    blockent_t* f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT] = (inv_slot_t){BLK_SAND, 4, 0};
    f->slot[BE_FURNACE_FUEL]  = (inv_slot_t){ITEM_COAL, 1, 0};
    f->stamp                  = 0;

    // Nothing has happened yet.
    furnace_catch_up(f, 0);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 0, "it smelted something in no time at all");

    // Half an item in.
    furnace_catch_up(f, FURNACE_COOK_TICKS / 2);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 0, "half an item produced a whole one");
    CHECK(furnace_progress_pct(f) == 50, "half way through reads as %d%%", furnace_progress_pct(f));
    CHECK(furnace_busy(f), "it is not burning with fuel and sand in it");

    // One whole item.
    furnace_catch_up(f, FURNACE_COOK_TICKS);
    CHECK(f->slot[BE_FURNACE_OUTPUT].item == BLK_GLASS && f->slot[BE_FURNACE_OUTPUT].count == 1,
          "one cook produced %d of item %u", f->slot[BE_FURNACE_OUTPUT].count, f->slot[BE_FURNACE_OUTPUT].item);
    CHECK(f->slot[BE_FURNACE_INPUT].count == 3, "the sand was not consumed");

    // And then the rest of it, all at once. One lump of coal is 1600
    // ticks and an item takes 200, so it sees all four through with
    // fuel to spare.
    furnace_catch_up(f, 1000000);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 4, "four sand made %d glass", f->slot[BE_FURNACE_OUTPUT].count);
    CHECK(f->slot[BE_FURNACE_INPUT].item == 0, "there is sand left after smelting all of it");
    CHECK(!furnace_busy(f), "it is still burning with nothing to smelt");
    CHECK(furnace_idle_reason(f) == FURNACE_IDLE_NO_INPUT, "an empty furnace gives reason %d",
          furnace_idle_reason(f));

    // FUEL RUNS OUT. Two items of work, one item of fuel: a stick is
    // 100 ticks, which is half a cook.
    blockent_clear();
    f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT] = (inv_slot_t){BLK_SAND, 2, 0};
    f->slot[BE_FURNACE_FUEL]  = (inv_slot_t){ITEM_STICK, 1, 0};
    furnace_catch_up(f, 1000000);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 0, "half a stick's worth of fire finished an item");
    CHECK(f->slot[BE_FURNACE_INPUT].count == 2, "the sand was eaten by a fire that went out");
    CHECK(furnace_idle_reason(f) == FURNACE_IDLE_NO_FUEL, "a cold furnace gives reason %d",
          furnace_idle_reason(f));

    // FUEL IS NOT BURNED FOR NOTHING. A furnace with fuel and no input
    // still has its fuel when you come back.
    blockent_clear();
    f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_FUEL] = (inv_slot_t){ITEM_COAL, 3, 0};
    furnace_catch_up(f, 1000000);
    CHECK(f->slot[BE_FURNACE_FUEL].count == 3, "an idle furnace burned %d coal for nothing",
          3 - f->slot[BE_FURNACE_FUEL].count);

    // A FULL OUTPUT STOPS IT, and does not eat the input.
    blockent_clear();
    f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT]  = (inv_slot_t){BLK_SAND, 10, 0};
    f->slot[BE_FURNACE_FUEL]   = (inv_slot_t){ITEM_COAL, 10, 0};
    f->slot[BE_FURNACE_OUTPUT] = (inv_slot_t){BLK_GLASS, ITEM_STACK_MAX, 0};
    furnace_catch_up(f, 1000000);
    CHECK(f->slot[BE_FURNACE_INPUT].count == 10, "a full output still ate the input");
    CHECK(furnace_idle_reason(f) == FURNACE_IDLE_FULL, "a full furnace gives reason %d", furnace_idle_reason(f));

    // A STAMP FROM THE FUTURE is not four billion ticks of free work.
    // (A world restored from a backup; the debug key that moves the
    // clock; a test that rewinds.)
    blockent_clear();
    f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT] = (inv_slot_t){BLK_SAND, 8, 0};
    f->slot[BE_FURNACE_FUEL]  = (inv_slot_t){ITEM_COAL, 8, 0};
    f->stamp                  = 5000;
    furnace_catch_up(f, 100);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 0, "a clock that went backwards smelted %d",
          f->slot[BE_FURNACE_OUTPUT].count);
    CHECK(f->stamp == 100, "the stamp did not follow the clock back");

    // THE LOOP IS PER EVENT, NOT PER TICK. A furnace opened after a
    // week of world time must not cost a week of iterations -- this is
    // the one thing that would make the lazy design worse than ticking.
    blockent_clear();
    f = blockent_add(0, 64, 0, BE_FURNACE);
    f->slot[BE_FURNACE_INPUT] = (inv_slot_t){BLK_SAND, 64, 0};
    f->slot[BE_FURNACE_FUEL]  = (inv_slot_t){ITEM_COAL, 64, 0};
    clock_t const t0 = clock();
    furnace_catch_up(f, 0xFFFFFFFEu);
    double const ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    CHECK(ms < 50.0, "catching up on 4 billion ticks took %.1f ms", ms);
    CHECK(f->slot[BE_FURNACE_OUTPUT].count == 64, "64 sand and plenty of coal made %d glass",
          f->slot[BE_FURNACE_OUTPUT].count);
    printf("  4 billion ticks of catching up in %.2f ms, 64 glass out\n", ms);

    blockent_clear();
}

// ---------------------------------------------------------------------
//  Ore veins, and caves that reach daylight
//
//  Two numbers matter and only one of them is obvious. HOW MUCH ore
//  there is decides how long a player digs; HOW CLUMPED it is decides
//  whether digging is worth it at all. Before veins, every ore block
//  was an independent coin flip -- the share was right and the game was
//  wrong, and no check that counted only the share would have noticed.
//
//  So this counts neighbours as well: for every ore block, how many of
//  its six faces touch the same ore. Scattered blocks score near zero.
//  A vein scores two or more.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  Biomes
//
//  Three questions, and the second is the one that matters. Does every
//  biome EXIST in a reasonable share of the world -- a biome nobody
//  ever walks through is a row of dead data. And is each one actually
//  DIFFERENT: it is easy to add a table, read it everywhere, and still
//  generate the same world three times over, and nothing about the code
//  would look wrong.
// ---------------------------------------------------------------------

// The benchmark flight's path, asserted against the generator that is
// actually compiled in. The path was chosen by a search over seeds and
// headings (game/benchpath.h); a later change to worldgen moves the
// terrain under it, and this is what says so at build time instead of
// leaving a benchmark quietly flying over somewhere flat.
static void check_bench_path(void) {
    printf("bench flight: the path the renderer measurements fly\n");

    int  seen[BIOME_COUNT];
    memset(seen, 0, sizeof(seen));
    int   hmin = 9999, hmax = -9999, worst = 0, prev = -1;
    long  steps = 0;
    double rough = 0.0;

    // Every block along it, not a sample: a one-block spike is exactly
    // what would bury the camera, and sampling is how you miss it.
    for (int i = 0; i <= (int)BENCH_DIST; i++) {
        double const t = (double)i / BENCH_SPEED;
        double       wx, wz;
        float        yaw;
        bench_path_at(t, &wx, &wz, &yaw);
        int32_t const x = (int32_t)floor(wx), z = (int32_t)floor(wz);
        int const     h = worldgen_height(x, z, BENCH_SEED);
        uint8_t const b = worldgen_biome(x, z, BENCH_SEED);

        CHECK(b < BIOME_COUNT, "bench path leaves the biome table at %d,%d (%u)", x, z, b);
        CHECK(h > 0 && h < CH_H, "bench path has no ground at %d,%d (h %d)", x, z, h);
        if (b < BIOME_COUNT) seen[b]++;
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
        if (prev >= 0) {
            int const dh = h - prev < 0 ? prev - h : h - prev;
            if (dh > worst) worst = dh;
            rough += (double)dh;
            steps++;
        }
        prev = h;
    }
    rough /= (double)steps;

    int crossed = 0;
    for (int b = 0; b < BIOME_COUNT; b++) {
        if (seen[b] > 0) crossed++;
        printf("  %-12s %4d of %d blocks\n", BIOMES[b].name, seen[b], (int)BENCH_DIST + 1);
    }
    printf("  ground %d..%d (%d blocks), mean |dh| %.2f, worst step %d, %d biomes in %.0f s\n", hmin, hmax,
           hmax - hmin, rough, worst, crossed, BENCH_SECS);

    // The three properties the search selected for. Each one is a way
    // the benchmark stops being worth running.
    CHECK(crossed == BIOME_COUNT, "bench path crosses %d of %d biomes -- it is meant to show every one", crossed,
          BIOME_COUNT);
    CHECK(worst <= 2, "bench path steps %d blocks somewhere -- the camera would fly into it", worst);
    CHECK(hmax - hmin >= 12, "bench path is %d blocks of relief -- too flat to measure anything on",
          hmax - hmin);

    // It must also stay out of the Far Lands, which have their own
    // generator and no ore at all.
    CHECK(!farlands_chunk_is(0, FARLANDS_X_DEFAULT), "the bench path starts inside the Far Lands");

    // How much of the card the pre-generated world will take, so a
    // change to the view distance cannot quietly make it enormous.
    int const chunks_long = (int)(BENCH_DIST / CH_D) + 1;
    printf("  pre-generates roughly %d chunks (%d along the path, 11 wide at the near view)\n",
           chunks_long * 11, chunks_long);
}

static void check_biomes(void) {
    printf("biomes: three places, and they differ\n");

    uint32_t const seed = 0x81011E5u;

    // The share of the world each one takes, over a wide area so the
    // fields get to wander.
    long share[BIOME_COUNT];
    memset(share, 0, sizeof(share));
    long total = 0;
    for (int32_t z = -2000; z <= 2000; z += 17) {
        for (int32_t x = -2000; x <= 2000; x += 17) {
            uint8_t const b = worldgen_biome(x, z, seed);
            CHECK(b < BIOME_COUNT, "worldgen_biome returned %u", b);
            if (b < BIOME_COUNT) share[b]++;
            total++;
        }
    }
    for (int b = 0; b < BIOME_COUNT; b++) {
        double const pct = 100.0 * (double)share[b] / (double)total;
        printf("  %-12s %5.1f%% of the world\n", BIOMES[b].name, pct);
        CHECK(pct > 4.0, "%s is %.1f%% of the world -- nobody would ever walk through it", BIOMES[b].name, pct);
        CHECK(pct < 80.0, "%s is %.1f%% of the world -- the others are garnish", BIOMES[b].name, pct);
    }

    // Every row has to be filled in, or a biome generates air and dirt.
    for (int b = 0; b < BIOME_COUNT; b++) {
        CHECK(BIOMES[b].name != NULL && BIOMES[b].name[0] != '\0', "biome %d has no name", b);
        CHECK(BIOMES[b].surface != BLK_AIR && BIOMES[b].filler != BLK_AIR, "%s has no blocks", BIOMES[b].name);
        CHECK(BIOMES[b].soil_min >= 1 && BIOMES[b].soil_max >= BIOMES[b].soil_min, "%s has a bad soil band",
              BIOMES[b].name);
    }

    // AND THEY REALLY ARE DIFFERENT. Generate a stretch of each and
    // count what comes out: this is the check that fails if the table
    // is read everywhere and says the same thing each time.
    static uint8_t id[CH_CELLS];
    static uint8_t st[CH_CELLS];
    chunk_t        c;
    memset(&c, 0, sizeof(c));
    c.id = id;
    c.st = st;

    long logs[BIOME_COUNT], plants[BIOME_COUNT], sand_top[BIOME_COUNT], grass_top[BIOME_COUNT];
    long cols[BIOME_COUNT], birch[BIOME_COUNT], cacti[BIOME_COUNT], sandstone[BIOME_COUNT];
    memset(logs, 0, sizeof(logs));
    memset(plants, 0, sizeof(plants));
    memset(sand_top, 0, sizeof(sand_top));
    memset(grass_top, 0, sizeof(grass_top));
    memset(cols, 0, sizeof(cols));
    memset(birch, 0, sizeof(birch));
    memset(cacti, 0, sizeof(cacti));
    memset(sandstone, 0, sizeof(sandstone));

    // A long east-west strip, which crosses several biomes.
    for (int32_t cx = -40; cx <= 40; cx++) {
        c.cx = cx;
        c.cz = 0;
        worldgen_chunk(&c, seed, FARLANDS_NONE);
        for (int lz = 0; lz < CH_D; lz++) {
            for (int lx = 0; lx < CH_W; lx++) {
                int32_t const wx = cx * CH_W + lx, wz = lz;
                uint8_t const b  = worldgen_biome(wx, wz, seed);
                if (b >= BIOME_COUNT) continue;
                int const h = worldgen_height(wx, wz, seed);
                if (h <= CH_SEA_LEVEL + 1 || h + 1 >= CH_H) continue;  // beaches are not a biome
                cols[b]++;
                uint8_t const top = id[CH_IDX(lx, h, lz)];
                uint8_t const above = id[CH_IDX(lx, h + 1, lz)];
                if (top == BLK_SAND) sand_top[b]++;
                if (top == BLK_GRASS) grass_top[b]++;
                if (above == BLK_TALL_GRASS || above == BLK_FLOWER_RED || above == BLK_FLOWER_YELLOW) plants[b]++;
                for (int y = h; y < CH_H && y < h + 8; y++) {
                    uint8_t const t = id[CH_IDX(lx, y, lz)];
                    if (t == BLK_LOG) logs[b]++;
                    if (t == BLK_BIRCH_LOG) birch[b]++;
                    if (t == BLK_CACTUS) cacti[b]++;
                }
                for (int y = 1; y < h; y++) {
                    if (id[CH_IDX(lx, y, lz)] == BLK_SANDSTONE) sandstone[b]++;
                }
            }
        }
    }

    for (int b = 0; b < BIOME_COUNT; b++) {
        if (cols[b] == 0) continue;
        printf("  %-12s %4.1f%% sand, %4.1f%% grass, %4.1f%% plants, %.3f oak, %.3f birch, %.2f cactus, "
               "%.2f sandstone a column\n",
               BIOMES[b].name, 100.0 * (double)sand_top[b] / (double)cols[b],
               100.0 * (double)grass_top[b] / (double)cols[b], 100.0 * (double)plants[b] / (double)cols[b],
               (double)logs[b] / (double)cols[b], (double)birch[b] / (double)cols[b],
               (double)cacti[b] / (double)cols[b], (double)sandstone[b] / (double)cols[b]);
    }

    CHECK(cols[BIOME_SAND] > 0 && sand_top[BIOME_SAND] > cols[BIOME_SAND] * 9 / 10,
          "the sand flats are not mostly sand");
    CHECK(grass_top[BIOME_SAND] == 0, "%ld columns of the sand flats grew grass", grass_top[BIOME_SAND]);
    CHECK(plants[BIOME_SAND] == 0, "%ld plants grew in the sand flats", plants[BIOME_SAND]);
    CHECK(logs[BIOME_SAND] == 0, "%ld logs grew in the sand flats", logs[BIOME_SAND]);

    CHECK(cols[BIOME_PLAINS] > 0 && grass_top[BIOME_PLAINS] > cols[BIOME_PLAINS] * 9 / 10,
          "the plains are not mostly grass");
    CHECK(cols[BIOME_FOREST] > 0, "the strip crossed no forest at all");

    // EACH NEW BLOCK WHERE IT BELONGS, AND NOWHERE ELSE. Five permanent
    // ids went in for these (D-74), and a block that never generates is
    // an id spent for nothing.
    CHECK(birch[BIOME_BIRCH] > 0, "no birch grew in the birch wood");
    CHECK(birch[BIOME_FOREST] == 0, "%ld birch logs grew in the oak forest", birch[BIOME_FOREST]);
    CHECK(birch[BIOME_PLAINS] == 0, "%ld birch logs grew on the plains", birch[BIOME_PLAINS]);
    CHECK(logs[BIOME_BIRCH] == 0, "%ld oaks grew in the birch wood", logs[BIOME_BIRCH]);
    CHECK(cacti[BIOME_SAND] > 0, "no cactus grew in the sand flats");
    CHECK(cacti[BIOME_PLAINS] == 0 && cacti[BIOME_FOREST] == 0, "a cactus grew somewhere green");
    CHECK(sandstone[BIOME_SAND] > 0, "there is no sandstone under the sand");
    CHECK(sandstone[BIOME_PLAINS] == 0, "there is sandstone under the plains");

    // --- The ground is CONTINUOUS ------------------------------------
    //
    // This is the check the whole design exists for. Height is blended
    // from each biome's smooth share of a spot, rather than looked up
    // by biome id -- and if that ever regresses to a lookup, the symptom
    // is a vertical cliff at every biome border and NOTHING ELSE here
    // would notice: the shares would be right, the surface blocks would
    // be right, and the world would be full of walls.
    //
    // So: walk a long line, watch every step, and watch hardest at the
    // steps that CROSS a border, which is exactly where a lookup breaks
    // and noise does not.
    int  worst = 0, worst_cross = 0;
    long crossings = 0;
    int32_t worst_x = 0;
    for (int32_t z = -600; z <= 600; z += 131) {
        int prev  = worldgen_height(-3000, z, seed);
        uint8_t pb = worldgen_biome(-3000, z, seed);
        for (int32_t x = -2999; x <= 3000; x++) {
            int const     hh = worldgen_height(x, z, seed);
            uint8_t const bb = worldgen_biome(x, z, seed);
            int const     d  = hh > prev ? hh - prev : prev - hh;
            if (d > worst) {
                worst   = d;
                worst_x = x;
            }
            if (bb != pb) {
                crossings++;
                if (d > worst_cross) worst_cross = d;
            }
            prev = hh;
            pb   = bb;
        }
    }
    printf("  %ld biome crossings; biggest step %d blocks, %d of them at a crossing\n", crossings, worst,
           worst_cross);
    CHECK(crossings > 100, "only %ld biome crossings in 60000 blocks -- the walk saw nothing", crossings);
    // Terrain noise alone steps 3-4 blocks at its steepest. A lookup by
    // biome id would step by the difference between two biomes' bands,
    // which is twenty or more.
    CHECK(worst_cross <= 6, "a %d-block step at a biome border (near x=%d) -- the height is not blended",
          worst_cross, worst_x);

    // SNOW ON ABOUT A FIFTH OF THE TALL GROUND -- the user's number, so
    // it is checked as a number and not as "some".
    // SEVERAL ROWS, not one. The first version of this walked a single
    // strip at z = 0, found no ground above the snow line in it, and
    // skipped the whole check in silence -- which is the worst thing a
    // check can do, because it reads exactly like passing.
    long tall = 0, snowy = 0;
    for (int32_t cz = -6; cz <= 6; cz += 3) {
        for (int32_t cx = -40; cx <= 40; cx++) {
            c.cx = cx;
            c.cz = cz;
            worldgen_chunk(&c, seed, FARLANDS_NONE);
            for (int lz = 0; lz < CH_D; lz++) {
                for (int lx = 0; lx < CH_W; lx++) {
                    int32_t const wx = cx * CH_W + lx, wz = cz * CH_D + lz;
                    if (worldgen_biome(wx, wz, seed) != BIOME_MOUNTAIN) continue;
                    int const hh = worldgen_height(wx, wz, seed);
                    if (hh < (int)BIOMES[BIOME_MOUNTAIN].snow_above || hh + 1 >= CH_H) continue;
                    tall++;
                    if (id[CH_IDX(lx, hh, lz)] == BLK_SNOW) snowy++;
                }
            }
        }
    }
    // What the generated chunks prove: snow EXISTS, and only where it
    // is allowed. A handful of peaks is enough for that.
    CHECK(tall > 200, "only %ld columns above the snow line -- the check saw almost nothing", tall);
    CHECK(snowy > 0, "no snow anywhere on %ld columns of high ground", tall);

    // What the SHARE needs, which those chunks cannot give: the snow
    // field is slower than a mountain is wide, so a strip of terrain
    // samples two or three summits and reports 0% or 100%. Measured
    // over a wide area instead, on the generator's own predicate --
    // cheap, because it asks no chunk to be built.
    long wide = 0, wide_snow = 0;
    for (int32_t z = -6000; z <= 6000; z += 23) {
        for (int32_t x = -6000; x <= 6000; x += 23) {
            if (worldgen_biome(x, z, seed) != BIOME_MOUNTAIN) continue;
            if (worldgen_height(x, z, seed) < (int)BIOMES[BIOME_MOUNTAIN].snow_above) continue;
            wide++;
            if (worldgen_snow(x, z, seed)) wide_snow++;
        }
    }
    double const pct = wide > 0 ? 100.0 * (double)wide_snow / (double)wide : 0.0;
    printf("  snow on %.1f%% of ground above y=%u, over %ld summits' worth\n", pct,
           BIOMES[BIOME_MOUNTAIN].snow_above, wide);
    CHECK(wide > 2000, "only %ld samples of high ground -- not enough to call a share", wide);
    CHECK(pct > 10.0 && pct < 35.0, "snow covers %.1f%% of the tall ground, and the user asked for 20", pct);

    // Mountains are actually higher, and bare on top.
    long high = 0, rock = 0, mcols = 0;
    int  peak = 0;
    for (int32_t z = -400; z <= 400; z += 7) {
        for (int32_t x = -3000; x <= 3000; x += 7) {
            if (worldgen_biome(x, z, seed) != BIOME_MOUNTAIN) continue;
            int const hh = worldgen_height(x, z, seed);
            mcols++;
            if (hh > peak) peak = hh;
            if (hh > 40) high++;
            if (hh >= (int)BIOMES[BIOME_MOUNTAIN].rock_above) rock++;
        }
    }
    if (mcols > 0) {
        printf("  mountains: peak y=%d, %.1f%% above y=40 (bare rock)\n", peak,
               100.0 * (double)rock / (double)mcols);
        CHECK(peak > 42, "the highest mountain is y=%d -- that is a hill", peak);
        CHECK(rock > 0, "no mountain column anywhere reaches bare rock");
        CHECK(peak <= CH_H - 8, "a mountain reached y=%d, past the generator's ceiling", peak);
    }
    if (cols[BIOME_FOREST] > 0 && cols[BIOME_PLAINS] > 0) {
        double const fl = (double)logs[BIOME_FOREST] / (double)cols[BIOME_FOREST];
        double const pl = (double)logs[BIOME_PLAINS] / (double)cols[BIOME_PLAINS];
        CHECK(fl > pl * 1.6, "a forest has %.3f logs a column against the plains' %.3f -- that is the same place",
              fl, pl);
    }
}

static void check_ores(void) {
    printf("ores: veins, and caves that open\n");

    uint32_t const seed = 0x0DEF17u;
    long stone = 0, coal = 0, iron = 0;
    long coal_touch = 0, iron_touch = 0;
    long columns = 0;

    // A block of chunks, generated the way the game generates them.
    static uint8_t id[CH_CELLS];
    static uint8_t st[CH_CELLS];
    chunk_t c;
    memset(&c, 0, sizeof(c));
    c.id = id;
    c.st = st;

    #define AT(cc, X, Y, Z) ((cc)->id[CH_IDX((X), (Y), (Z))])

    for (int32_t cz = 0; cz < 4; cz++) {
        for (int32_t cx = 0; cx < 4; cx++) {
            c.cx = cx;
            c.cz = cz;
            worldgen_chunk(&c, seed, FARLANDS_NONE);

            for (int lz = 0; lz < CH_D; lz++) {
                for (int lx = 0; lx < CH_W; lx++) {
                    columns++;
                    int const h = worldgen_height(cx * CH_W + lx, cz * CH_D + lz, seed);

                    for (int y = 1; y < CH_H; y++) {
                        uint8_t const b = AT(&c, lx, y, lz);
                        if (b == BLK_STONE) stone++;
                        if (b != BLK_COAL_ORE && b != BLK_IRON_ORE) continue;

                        // Its six neighbours, inside this chunk only:
                        // the edges undercount a little and identically
                        // for both ores, which is fine for a ratio.
                        int same = 0;
                        int const dx[6] = {1, -1, 0, 0, 0, 0};
                        int const dy[6] = {0, 0, 1, -1, 0, 0};
                        int const dz[6] = {0, 0, 0, 0, 1, -1};
                        for (int k = 0; k < 6; k++) {
                            int const nx = lx + dx[k], ny = y + dy[k], nz = lz + dz[k];
                            if (nx < 0 || nx >= CH_W || nz < 0 || nz >= CH_D || ny < 0 || ny >= CH_H) continue;
                            if (AT(&c, nx, ny, nz) == b) same++;
                        }
                        if (b == BLK_COAL_ORE) {
                            coal++;
                            coal_touch += same;
                        } else {
                            iron++;
                            iron_touch += same;
                        }
                    }
                }
            }
        }
    }
    #undef AT

    double const coal_pct = stone > 0 ? 100.0 * (double)coal / (double)(stone + coal + iron) : 0.0;
    double const iron_pct = stone > 0 ? 100.0 * (double)iron / (double)(stone + coal + iron) : 0.0;
    double const coal_nb  = coal > 0 ? (double)coal_touch / (double)coal : 0.0;
    double const iron_nb  = iron > 0 ? (double)iron_touch / (double)iron : 0.0;
    // (The cave-mouth number that used to be worked out here counted
    //  columns whose surface cell is air, which is very nearly the
    //  definition of a surface cell -- it could not tell a hillside
    //  from a hole, and so could not fail when the holes stopped.
    //  check_cave_mouths floods the sky into the ground instead: F-126.)

    printf("  coal %.2f%% of rock, %.2f ore neighbours each\n", coal_pct, coal_nb);
    printf("  iron %.2f%% of rock, %.2f ore neighbours each\n", iron_pct, iron_nb);


    // ENOUGH TO FIND, NOT SO MUCH THAT IT IS EVERYWHERE.
    CHECK(coal_pct > 0.4 && coal_pct < 4.0, "coal is %.2f%% of rock, wanted 0.4..4", coal_pct);
    CHECK(iron_pct > 0.1 && iron_pct < 2.0, "iron is %.2f%% of rock, wanted 0.1..2", iron_pct);
    CHECK(iron_pct < coal_pct, "iron (%.2f%%) is not rarer than coal (%.2f%%)", iron_pct, coal_pct);

    // AND IT IS IN VEINS. This is the check that would have failed
    // before, when the share was already right: scattered blocks touch
    // each other about 0.05 times on average, a vein two or more.
    CHECK(coal_nb > 2.0, "coal blocks touch %.2f others on average -- that is not a vein", coal_nb);
    CHECK(iron_nb > 1.5, "iron blocks touch %.2f others on average -- that is not a vein", iron_nb);

    // Iron stays deep and coal stays out of the topsoil.
    for (int32_t cz = 0; cz < 2; cz++) {
        for (int32_t cx = 0; cx < 2; cx++) {
            c.cx = cx;
            c.cz = cz;
            worldgen_chunk(&c, seed, FARLANDS_NONE);
            for (int lz = 0; lz < CH_D; lz++) {
                for (int lx = 0; lx < CH_W; lx++) {
                    for (int y = 0; y < CH_H; y++) {
                        uint8_t const b = c.id[CH_IDX(lx, y, lz)];
                        CHECK(!(b == BLK_IRON_ORE && y > VEIN_IRON_YMAX + 3),
                              "iron ore at y=%d, above its %d limit", y, VEIN_IRON_YMAX);
                        CHECK(!(b == BLK_COAL_ORE && y > VEIN_COAL_YMAX + 3),
                              "coal ore at y=%d, above its %d limit", y, VEIN_COAL_YMAX);
                    }
                }
            }
        }
    }

    // CAVES REACHING DAYLIGHT ARE CHECKED IN check_cave_mouths, by
    // flooding the sky into the ground. They were checked here, with a
    // range around a number that counted columns whose surface cell is
    // air -- and both bounds passed happily for weeks while no world
    // this game generated had a single cave entrance in it (F-126).
    //
    // The lesson is not "that number was wrong". It is that a check
    // which asserts a RANGE on a quantity nobody has tied to the thing
    // it names is a check that can only ever pass.
}

static void check_recipes(void) {
    printf("crafting: the recipe table\n");

    int const n = recipe_count();
    CHECK(n > 0, "there are no recipes at all");

    for (int i = 0; i < n; i++) {
        recipe_t const* r = recipe_at(i);
        char const*     w = item_def(r->out).name;

        CHECK(r->out != 0 && r->out < ITEM_COUNT, "recipe %d makes item %u, which does not exist", i, r->out);
        CHECK(r->out_n >= 1, "recipe for %s makes none of it", w);
        CHECK(r->out_n <= item_def(r->out).stack_max, "recipe for %s makes %u, more than a stack holds", w,
              r->out_n);
        CHECK(r->station < RS_COUNT, "recipe for %s is made at station %u, which does not exist", w, r->station);
        CHECK(r->n_in >= 1 && r->n_in <= RECIPE_IN_MAX, "recipe for %s names %u ingredients", w, r->n_in);
        CHECK(item_label(r->out) != 0, "recipe for %s makes something with no name on screen", w);

        for (int k = 0; k < r->n_in; k++) {
            uint16_t const id = r->in[k].item;
            CHECK(id != 0 && id < ITEM_COUNT, "recipe for %s wants item %u, which does not exist", w, id);
            CHECK(r->in[k].count >= 1, "recipe for %s wants none of %s", w, item_def(id).name);
            CHECK(item_label(id) != 0, "recipe for %s wants %s, which has no name on screen", w,
                  item_def(id).name);
            // The same thing twice in one recipe would make
            // recipe_can_make count it twice and be wrong about both.
            for (int j = 0; j < k; j++) {
                CHECK(r->in[j].item != id, "recipe for %s names %s twice", w, item_def(id).name);
            }
            CHECK(id != r->out, "recipe for %s is made of itself", w);
        }

        // Distinct output AND station -- IN THE PLACES A PLAYER PICKS
        // A ROW. Two rows making the same thing at a crafting table is
        // a line the book shows twice with no way of telling them
        // apart, and two smelts of one ore would be the same problem
        // one step along.
        //
        // A MACHINE IS THE OTHER WAY ROUND: nobody picks a row in a
        // cheese maker, they put things in it and it matches whatever
        // fits (game/maker.h). "1 pork + 1 flower of any colour" is
        // therefore two rows on purpose, and what has to be distinct
        // there is the INGREDIENTS, not the output.
        bool const picked = r->station == RS_INVENTORY || r->station == RS_TABLE || r->station == RS_FURNACE;
        for (int j = 0; j < i; j++) {
            recipe_t const* o = recipe_at(j);
            if (o->station != r->station) continue;
            if (picked) {
                CHECK(o->out != r->out, "two recipes make %s at station %u", w, r->station);
                continue;
            }
            bool same = o->n_in == r->n_in;
            for (int k = 0; k < r->n_in && same; k++) {
                bool found = false;
                for (int l = 0; l < o->n_in && !found; l++) {
                    found = o->in[l].item == r->in[k].item && o->in[l].count == r->in[k].count;
                }
                same = found;
            }
            CHECK(!same, "two recipes at station %u want exactly the same things", r->station);
        }
    }

    // A reversible recipe has to be undoable into things that exist,
    // which is the whole of what the disassembly bench will ask of it.
    for (int i = 0; i < n; i++) {
        recipe_t const* r = recipe_at(i);
        if ((r->flags & RF_REVERSIBLE) == 0) continue;
        CHECK(r->station != RS_FURNACE, "%s is smelted and marked reversible", item_def(r->out).name);
        for (int k = 0; k < r->n_in; k++) {
            CHECK(item_def(r->in[k].item).name[0] != 0, "%s reverses into something nameless",
                  item_def(r->out).name);
        }
    }
    printf("  %d recipes, all made of things that exist\n", n);
}

static void check_discovery(void) {
    printf("crafting: what the player knows\n");

    inventory_t inv;
    inv_clear(&inv);

    // An empty book. This is the state a new world starts in, and the
    // user asked for exactly it.
    int known = 0;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_known(recipe_at(i), &inv)) known++;
    }
    CHECK(known == 0, "a player who has held nothing already knows %d recipes", known);

    // ONE ingredient reveals a recipe -- not all of them. Picking up a
    // log has to tell you what a log is for.
    inv_add(&inv, BLK_LOG, 1, 0);
    CHECK(inv_seen(&inv, BLK_LOG), "a log was picked up and not remembered");
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r    = recipe_at(i);
        bool            uses = false;
        for (int k = 0; k < r->n_in; k++) {
            if (r->in[k].item == BLK_LOG) uses = true;
        }
        CHECK(recipe_known(r, &inv) == uses, "%s: known=%d but uses a log=%d", item_def(r->out).name,
              (int)recipe_known(r, &inv), (int)uses);
    }

    // Held once is known forever: spending it all does not take the
    // recipe away again.
    CHECK(inv_take(&inv, BLK_LOG, 1), "could not take back the one log");
    CHECK(inv_count(&inv, BLK_LOG) == 0, "the log is still there after being taken");
    CHECK(inv_seen(&inv, BLK_LOG), "spending the last log forgot what it was for");

    // Even a pickup that does not fit counts: it was in their hands.
    inventory_t full;
    inv_clear(&full);
    for (int i = 0; i < INV_SLOTS; i++) {
        full.slot[i].item  = BLK_STONE;
        full.slot[i].count = ITEM_STACK_MAX;
    }
    CHECK(inv_add(&full, ITEM_COAL, 1, 0) == 1, "a full pack accepted coal");
    CHECK(inv_seen(&full, ITEM_COAL), "coal bounced off a full pack and was forgotten");
    printf("  empty at the start; one ingredient is enough; spending it does not forget\n");
}

static void check_crafting(void) {
    printf("crafting: making things\n");

    // Find the recipes by output rather than by index, so inserting a
    // row above them does not silently change what is being tested.
    recipe_t const *planks = NULL, *sticks = NULL, *torch = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->out == BLK_PLANKS) planks = r;
        if (r->out == ITEM_STICK) sticks = r;
        if (r->out == BLK_TORCH) torch = r;
    }
    CHECK(planks != NULL && sticks != NULL && torch != NULL, "the starting recipes are not all there");
    if (planks == NULL || sticks == NULL || torch == NULL) return;

    inventory_t inv;
    inv_clear(&inv);

    // Nothing carried: makes nothing, and takes nothing doing it.
    CHECK(recipe_can_make(planks, &inv, 8) == 0, "planks can be made out of nothing");
    CHECK(recipe_make(planks, &inv, 1) == 0, "planks were made out of nothing");

    inv_add(&inv, BLK_LOG, 3, 0);
    CHECK(recipe_can_make(planks, &inv, 8) == 3, "3 logs should be 3 lots of planks");
    CHECK(recipe_make(planks, &inv, 2) == 2, "two lots of planks were refused");
    CHECK(inv_count(&inv, BLK_LOG) == 1, "the wrong number of logs was consumed");
    CHECK(inv_count(&inv, BLK_PLANKS) == 8, "2 x 4 planks did not arrive");

    // Minecraft's numbers, as the user asked for.
    CHECK(planks->out_n == 4 && planks->in[0].count == 1, "a log is not 4 planks");
    CHECK(sticks->out_n == 4 && sticks->in[0].count == 2, "2 planks are not 4 sticks");
    CHECK(torch->out_n == 4, "a torch recipe does not make 4");

    // Asking for more than the materials allow makes as many as it can
    // and stops, rather than failing outright.
    CHECK(recipe_make(planks, &inv, 10) == 1, "the last log did not become the last planks");
    CHECK(inv_count(&inv, BLK_LOG) == 0, "logs left over after making as many planks as possible");

    // A RECIPE WITH TWO INGREDIENTS, all the way through. The torch
    // was reported as taking the materials and giving nothing back
    // (the cause turned out to be elsewhere -- player.c reopening the
    // screen every tick, so enter crafted the FIRST row rather than the
    // one under the cursor) but "two ingredients out, one output in"
    // had never actually been exercised, so it is now.
    inv_clear(&inv);
    inv_add(&inv, ITEM_COAL, 3, 0);
    inv_add(&inv, ITEM_STICK, 2, 0);
    CHECK(recipe_can_make(torch, &inv, 9) == 2, "3 coal and 2 sticks should be 2 lots of torches, got %d",
          recipe_can_make(torch, &inv, 9));
    CHECK(recipe_make(torch, &inv, 1) == 1, "a torch with everything to hand was refused");
    CHECK(inv_count(&inv, BLK_TORCH) == 4, "one torch recipe gave %d torches, wanted 4",
          inv_count(&inv, BLK_TORCH));
    CHECK(inv_count(&inv, ITEM_COAL) == 2, "the torch took %d coal, wanted 1", 3 - inv_count(&inv, ITEM_COAL));
    CHECK(inv_count(&inv, ITEM_STICK) == 1, "the torch took %d sticks, wanted 1",
          2 - inv_count(&inv, ITEM_STICK));

    // ... and again, onto the stack it already made.
    CHECK(recipe_make(torch, &inv, 1) == 1, "a second torch was refused");
    CHECK(inv_count(&inv, BLK_TORCH) == 8, "two torch recipes gave %d torches, wanted 8",
          inv_count(&inv, BLK_TORCH));
    CHECK(recipe_make(torch, &inv, 1) == 0, "a third torch was made with no sticks left");
    CHECK(inv_count(&inv, ITEM_COAL) == 1, "the refused third torch still took coal");

    // A missing ingredient is REPORTED, not just refused: this is the
    // "so a user knows what to look for" the user asked for.
    inv_clear(&inv);
    inv_add(&inv, ITEM_COAL, 1, 0);
    int short_of = -1;
    for (int k = 0; k < torch->n_in; k++) {
        if (torch->in[k].item == ITEM_STICK) short_of = k;
    }
    CHECK(short_of >= 0, "a torch does not want a stick");
    if (short_of >= 0) {
        CHECK(recipe_missing(torch, &inv, short_of) == 1, "a torch with no stick is not short one stick");
    }

    // A FULL PACK MUST NOT EAT THE MATERIALS. Every slot taken, and
    // the ingredient sitting in a stack DEEP ENOUGH TO SURVIVE being
    // drawn from -- otherwise consuming it frees its own slot and the
    // output lands there, which is what the first draft of this check
    // accidentally tested.
    inv_clear(&inv);
    for (int i = 0; i < INV_SLOTS; i++) {
        inv.slot[i].item  = BLK_STONE;
        inv.slot[i].count = ITEM_STACK_MAX;
    }
    inv.slot[0].item  = BLK_PLANKS;
    inv.slot[0].count = ITEM_STACK_MAX;
    CHECK(recipe_can_make(sticks, &inv, 1) >= 1, "the planks are there but cannot be used");
    CHECK(recipe_make(sticks, &inv, 1) == 0, "sticks were made with nowhere to put them");
    CHECK(inv_count(&inv, BLK_PLANKS) == ITEM_STACK_MAX, "planks were eaten by a craft that could not finish");
    CHECK(inv_count(&inv, ITEM_STICK) == 0, "sticks appeared in a pack with no room for them");
    printf("  quantities are Minecraft's; a full pack refuses without eating the materials\n");
}

// ---------------------------------------------------------------------
//  The search box's fold (i18n/fold.h)
//
//  The badge has one QWERTY and the game speaks 32 languages, so this
//  is what makes the crafting search box work at all. The check that
//  matters is COVERAGE OF THE WHOLE DOMAIN -- every character of every
//  string of every language -- not a handful of samples.
// ---------------------------------------------------------------------

static void check_fold(void) {
    printf("crafting: folding 32 languages onto one keyboard\n");

    sm_lang_t const was = i18n_language();

    int holes = 0, chars = 0;
    for (int l = 0; l < SM_LANG_COUNT; l++) {
        i18n_set_language((sm_lang_t)l);
        for (int k = 0; k < SM_STR_COUNT; k++) {
            char const* p = i18n_text((sm_str_t)k);
            uint32_t    cp;
            while ((p = fold_utf8_next(p, &cp)) != NULL) {
                chars++;
                if (!fold_known(cp)) {
                    if (holes < 8) {
                        printf("  FAIL: %s has U+%04X, which nothing folds\n", i18n_language_code((sm_lang_t)l),
                               (unsigned)cp);
                    }
                    holes++;
                    s_fail++;
                }
            }
        }
    }
    CHECK(holes == 0, "%d characters in the shipped strings have no fold", holes);

    // Every name a player can search for survives the fold as
    // something they can actually type: a name that folds away to
    // nothing is a row that can never be found.
    for (int l = 0; l < SM_LANG_COUNT; l++) {
        i18n_set_language((sm_lang_t)l);
        for (uint16_t id = 1; id < ITEM_COUNT; id++) {
            if (id == BLK_AIR || id == BLK_BARRIER) continue;
            sm_str_t const lab = item_label(id);
            CHECK(lab != 0, "%s has no name on screen", item_def(id).name);
            if (lab == 0) continue;
            char folded[FOLD_MAX];
            fold_text(i18n_text(lab), folded, sizeof(folded));
            CHECK(folded[0] != '\0', "%s in %s folds away to nothing", item_def(id).name,
                  i18n_language_code((sm_lang_t)l));
            for (char const* c = folded; *c; c++) {
                CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'),
                      "%s in %s folds to '%s', which is not typable", item_def(id).name,
                      i18n_language_code((sm_lang_t)l), folded);
            }
        }
    }
    i18n_set_language(was);

    // Folding is idempotent -- the needle is folded once and the
    // haystack every frame, and the two have to meet.
    struct {
        char const* in;
        char const* want;
    } const cases[] = {
        {"Кирка", "kirka"}, {"Kömür", "komur"},  {"Dřevo", "drevo"},
        {"Łopata", "lopata"}, {"Ξύλο", "xylo"},  {"Straße", "strasse"},
        {"Iron Pickaxe", "ironpickaxe"},         {"Щит", "shchit"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char a[FOLD_MAX], b[FOLD_MAX];
        fold_text(cases[i].in, a, sizeof(a));
        CHECK(strcmp(a, cases[i].want) == 0, "'%s' folded to '%s', wanted '%s'", cases[i].in, a, cases[i].want);
        fold_text(a, b, sizeof(b));
        CHECK(strcmp(a, b) == 0, "folding '%s' twice gave '%s' then '%s'", cases[i].in, a, b);
    }

    // Matching: a substring in the folded form, an empty needle
    // matching everything, and punctuation and spaces never in the way.
    CHECK(fold_match("Iron Pickaxe", "ironpick"), "'iron pick' does not find the iron pickaxe");
    CHECK(fold_match("Кирка", ""), "an empty search box hides things");
    CHECK(!fold_match("Stone", "xyz"), "the search box matches things it should not");

    // A truncated and a malformed UTF-8 string must terminate, not
    // loop: a lang file off the SD card is a stranger's file (i18n.h).
    char out[FOLD_MAX];
    fold_text("\xD0", out, sizeof(out));
    fold_text("\xE2\x82", out, sizeof(out));
    fold_text("\x80\x80\x80", out, sizeof(out));
    fold_text("a\xFF" "b", out, sizeof(out));
    CHECK(strcmp(out, "ab") == 0, "a stray byte took its neighbours with it: '%s'", out);

    // A needle longer than the buffer must truncate, not overrun.
    char big[FOLD_MAX * 4];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    fold_text(big, out, sizeof(out));
    CHECK(strlen(out) == sizeof(out) - 1, "a long name folded to %u characters, not %u", (unsigned)strlen(out),
          (unsigned)(sizeof(out) - 1));

    printf("  %d characters across %d languages, every one of them foldable\n", chars, SM_LANG_COUNT);
}

static void check_inv_cursor(void) {
    printf("the inventory cursor\n");
    inventory_t inv;
    inv_clear(&inv);
    inv.cursor = 2;  // a hotbar slot
    CHECK(inv_screen_row(inv.cursor) == INV_ROWS, "the hotbar is not the bottom row on screen");
    inv_move_cursor(&inv, 0, -1);
    CHECK(inv_screen_row(inv.cursor) == INV_ROWS - 1 && inv.cursor % INV_HOTBAR == 2,
          "up from the hotbar went to slot %d, not the storage row just above", inv.cursor);
    for (int i = 0; i < INV_ROWS + 3; i++) inv_move_cursor(&inv, 0, -1);
    CHECK(inv_screen_row(inv.cursor) == 0, "up did not stop at the top row");
    for (int i = 0; i < INV_ROWS + 3; i++) inv_move_cursor(&inv, 0, 1);
    CHECK(inv.cursor == 2, "down did not come back to the hotbar slot it started from (%d)", inv.cursor);
    // Every slot is reachable, and each is visited once walking the grid.
    int seen[INV_SLOTS] = {0};
    for (int r = 0; r <= INV_ROWS; r++) {
        for (int c = 0; c < INV_HOTBAR; c++) {
            inv.cursor = 0;
            for (int i = 0; i < INV_ROWS; i++) inv_move_cursor(&inv, 0, -1);  // to the top row
            inv_move_cursor(&inv, -INV_HOTBAR, 0);
            inv_move_cursor(&inv, c, r);
            CHECK(inv_screen_row(inv.cursor) == r, "walking to row %d landed on row %d", r, inv_screen_row(inv.cursor));
            seen[inv.cursor]++;
        }
    }
    for (int i = 0; i < INV_SLOTS; i++) CHECK(seen[i] == 1, "slot %d was reached %d times", i, seen[i]);
}

static void check_drops(void) {
    printf("drops and despawn\n");
    CHECK(flat_world(8) != NULL, "the test world would not become resident");
    item_entity_reset();
    inventory_t inv;
    inv_clear(&inv);

    // Stone drops cobblestone -- but only to a tool that qualifies.
    set_block(8, 8, 8, BLK_STONE, 0);
    break_result_t r = interact_break(8, 8, 8, 0);  // bare hands
    CHECK(r.ok, "stone would not break by hand");
    printf("  stone broken by hand dropped %d\n", r.dropped);
    CHECK(r.dropped == 0, "bare hands harvested stone");

    set_block(8, 8, 8, BLK_STONE, 0);
    r = interact_break(8, 8, 8, ITEM_PICK_STONE);
    printf("  stone broken with a pickaxe dropped %d\n", r.dropped);
    CHECK(r.dropped == 1, "a pickaxe on stone dropped %d, expected 1", r.dropped);
    CHECK(item_entity_live() == 1, "the drop is not on the ground");

    // It falls, then is collected when the player comes near -- and
    // NOT before ITEM_PICKUP_DELAY, or breaking a block under your feet
    // snatches it back before it is visible.
    int picked = 0;
    for (uint32_t t = 0; t < ITEM_PICKUP_DELAY - 1; t++) picked += item_entity_tick(&inv, 8.5, 8.0, 8.5);
    CHECK(picked == 0, "a drop was collected before ITEM_PICKUP_DELAY");
    for (int t = 0; t < 20 && item_entity_live() > 0; t++) picked += item_entity_tick(&inv, 8.5, 8.0, 8.5);
    printf("  picked up %d after the delay; %d still on the ground\n", picked, item_entity_live());
    CHECK(picked == 1, "the drop was not collected: %d", picked);
    CHECK(inv_count(&inv, BLK_COBBLE) == 1, "the cobblestone is not in the inventory");

    // WHAT THE PLAYER THROWS MUST STAY THROWN for a moment. An item
    // lands inside the 1.4-block pickup radius whichever way you face,
    // so without a longer delay pressing G looks like it does nothing:
    // the item leaves the inventory and is collected again half a
    // second later.
    item_entity_reset();
    inv_clear(&inv);
    CHECK(item_entity_throw(8.5, 9.3, 8.5, BLK_DIRT, 1, 0, 0.2f, 0.12f, 0.0f, ITEM_THROW_DELAY) == 1,
          "throwing an item failed");
    picked = 0;
    for (uint32_t t = 0; t < ITEM_THROW_DELAY - 1; t++) picked += item_entity_tick(&inv, 8.5, 8.0, 8.5);
    printf("  a thrown item was still on the ground after %u ticks with the player standing on it\n",
           ITEM_THROW_DELAY - 1);
    CHECK(picked == 0, "a thrown item was collected again after %d ticks; G would look like it does nothing",
          ITEM_THROW_DELAY - 1);
    CHECK(item_entity_live() == 1, "the thrown item vanished");
    // ... and then it can be picked up again, or you could never
    // change your mind.
    // It comes to rest CLEAR of where it was thrown from -- which is
    // the other half of G working: an item you have to walk back to.
    double rest_x = 0.0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        if (!item_entity_at(i)->alive) continue;
        rest_x = item_entity_at(i)->body.x;
        break;
    }
    printf("  it came to rest %.2f blocks from the thrower (pickup range %.2f)\n", rest_x - 8.5,
           (double)ITEM_PICKUP_RANGE);
    CHECK(rest_x - 8.5 > (double)ITEM_PICKUP_RANGE,
          "a thrown item settled %.2f blocks away, inside the pickup radius: it would be scooped straight back up",
          rest_x - 8.5);

    // ... and walking to it picks it up, or you could never change
    // your mind.
    for (int t = 0; t < 10 && item_entity_live() > 0; t++) picked += item_entity_tick(&inv, rest_x, 8.0, 8.5);
    CHECK(picked == 1, "a thrown item could not be picked back up by walking to it");

    // A drop nobody collects despawns at ITEM_DESPAWN_TICKS -- in
    // TICKS, so a pause or a week away does not age it (D-51).
    item_entity_reset();
    CHECK(item_entity_spawn(20, 9, 20, BLK_DIRT, 1, 0) == 1, "spawning a drop failed");
    uint32_t t = 0;
    while (item_entity_live() > 0 && t < ITEM_DESPAWN_TICKS * 2) {
        item_entity_tick(NULL, 0.0, 0.0, 0.0);  // no player: nothing collects it
        t++;
    }
    printf("  an uncollected drop despawned after %u ticks (%.1f minutes at 20 Hz)\n", t,
           (double)t / 20.0 / 60.0);
    CHECK(t == ITEM_DESPAWN_TICKS, "a drop despawned after %u ticks, expected %u", t, ITEM_DESPAWN_TICKS);

    // Felling drops every block it takes, which is the point of felling
    // -- PLUS the seedlings (world/tree.h), which are one or two per
    // tree and not one per block.
    item_entity_reset();
    for (int y = 0; y < 5; y++) set_block(30, 8 + y, 30, BLK_LOG, 0);
    r = interact_break(30, 8, 30, ITEM_AXE_STONE);
    int saplings = 0, logs = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
        item_entity_t const* e = item_entity_at(i);
        if (e == NULL || !e->alive) continue;
        if (e->item == BLK_SAPLING_OAK) saplings += e->count;
        if (e->item == BLK_LOG) logs += e->count;
    }
    printf("  felling a 5-log trunk took %d blocks, dropped %d logs and %d seedling(s)\n", r.felled, logs,
           saplings);
    CHECK(r.was_tree, "the trunk did not fell");
    CHECK(logs == r.felled, "felling took %d blocks but dropped %d logs", r.felled, logs);
    CHECK(saplings >= TREE_DROP_MIN && saplings <= TREE_DROP_MAX, "a felled tree left %d seedlings, wanted %d..%d",
          saplings, TREE_DROP_MIN, TREE_DROP_MAX);
    // ONE PER TREE, NOT ONE PER BLOCK: forty saplings off one oak would
    // make the first tree the last one anybody had to plant.
    CHECK(saplings < r.felled, "a fell left one seedling per block");

    // The pool is finite and a full one must refuse, not corrupt.
    item_entity_reset();
    int made = 0;
    for (int i = 0; i < ITEM_ENTITY_MAX + 20; i++) made += item_entity_spawn(40, 9, 40, ITEM_PICK_WOOD, 1, 0);
    printf("  the pool took %d of %d single-item drops\n", made, ITEM_ENTITY_MAX + 20);
    CHECK(made == ITEM_ENTITY_MAX, "the pool took %d, expected its size %d", made, ITEM_ENTITY_MAX);
    CHECK(item_entity_live() == ITEM_ENTITY_MAX, "the live count disagrees with what was made");
}

// ---------------------------------------------------------------------
//  Languages (step 6.5)
//
//  Two things nobody notices until a player does. First, that the FONT
//  can draw every character of every translation: the engine's glyph
//  tables are asked directly, so this is the renderer's own answer, not
//  a second opinion. Second, that i18n_fmt() puts the values where the
//  translation says and takes their types from English whatever the
//  translation says -- the property that lets a lang file on the SD
//  card be edited by a stranger without the game trusting it.
// ---------------------------------------------------------------------

static void check_lang(void) {
    printf("languages: %d strings x %d\n", SM_STR_COUNT, SM_LANG_COUNT);

    // Every character of every string, through the engine's own tables.
    int tofu = 0;
    for (int l = 0; l < SM_LANG_COUNT; l++) {
        for (int s = 0; s < SM_STR_COUNT; s++) {
            char const* p = SM_STRINGS[l][s];
            for (;;) {
                uint32_t  cp;
                int const used = hershey_utf8_next(p, &cp);
                if (used == 0) break;
                p += used;
                if (cp == 0) {
                    CHECK(0, "%s / %s: not valid UTF-8", SM_LANG_CODES[l], SM_STR_KEYS[s]);
                    break;
                }
                hershey_glyph_t g;
                if (!hershey_glyph(cp, &g)) {
                    CHECK(0, "%s / %s: the font cannot draw U+%04X", SM_LANG_CODES[l],
                          SM_STR_KEYS[s], (unsigned)cp);
                    tofu++;
                }
            }
        }
    }
    CHECK(tofu == 0, "%d character(s) would come out as empty boxes", tofu);

    // Languages are told apart by their code, and every one has a name.
    for (int l = 0; l < SM_LANG_COUNT; l++) {
        sm_lang_t got = (sm_lang_t)-1;
        CHECK(i18n_language_from_code(SM_LANG_CODES[l], &got), "code %s is not recognised",
              SM_LANG_CODES[l]);
        CHECK(got == (sm_lang_t)l, "code %s came back as %d", SM_LANG_CODES[l], (int)got);
        CHECK(SM_LANG_NAMES[l][0] != '\0', "language %d has no name", l);
    }
    sm_lang_t unused = SM_LANG_EN;
    CHECK(!i18n_language_from_code("xx", &unused), "an unknown code was accepted");

    // The formatter. English first: the ordinary path.
    char buf[128];
    i18n_set_language(SM_LANG_EN);
    i18n_fmt(buf, sizeof buf, SM_STR_WORLD_SUB, 3, 12345u);
    CHECK(strcmp(buf, "slot 3, seed 12345") == 0, "en world.sub: %s", buf);
    i18n_fmt(buf, sizeof buf, SM_STR_INFO_CLOCK, 7, 5, (long long)42);
    CHECK(strcmp(buf, "07:05   day 42") == 0, "en info.clock: %s", buf);
    i18n_fmt(buf, sizeof buf, SM_STR_WORLDS_SLOT, 2, "Testworld");
    CHECK(strcmp(buf, "2  Testworld") == 0, "en worlds.slot: %s", buf);

    // Every language, every format string: it must not crash, and the
    // values must come out somewhere.
    for (int l = 0; l < SM_LANG_COUNT; l++) {
        i18n_set_language((sm_lang_t)l);
        i18n_fmt(buf, sizeof buf, SM_STR_WORLD_SUB, 7, 99u);
        CHECK(strstr(buf, "7") != NULL && strstr(buf, "99") != NULL,
              "%s world.sub lost a value: %s", SM_LANG_CODES[l], buf);
        i18n_fmt(buf, sizeof buf, SM_STR_NEW_DEFAULT_NAME, 4);
        CHECK(strstr(buf, "4") != NULL, "%s new.default_name lost the number: %s",
              SM_LANG_CODES[l], buf);
    }
    i18n_set_language(SM_LANG_EN);

    // A string with no values at all goes through untouched.
    i18n_fmt(buf, sizeof buf, SM_STR_MENU_PLAY);
    CHECK(strcmp(buf, T(SM_STR_MENU_PLAY)) == 0, "a plain string was changed: %s", buf);

    // Too small a buffer truncates and still terminates, like snprintf.
    char small[8];
    int const want = i18n_fmt(small, sizeof small, SM_STR_WORLD_SUB, 3, 12345u);
    CHECK(want == (int)strlen("slot 3, seed 12345"), "truncated call returned %d", want);
    CHECK(strlen(small) == sizeof small - 1, "truncated to %zu", strlen(small));
    CHECK(strncmp(small, "slot 3,", 7) == 0, "truncated wrongly: %s", small);

    // And now the part that matters: a translation is DATA. These are
    // the strings a hand-edited file on the card might hold.
    char const* const nasties[] = {
        "%s",          // where English says %d: must still read an int
        "%9$d",        // a value that does not exist
        "%",           // a lone per-cent
        "%d %d %d %d", // more than English has
        "%%d",         // an escaped one, then nothing
        "%2$d %2$d",   // the same value twice
    };
    for (size_t i = 0; i < sizeof nasties / sizeof nasties[0]; i++) {
        // SM_STR_NEW_DEFAULT_NAME takes one int; pretend the file says this.
        char out[64];
        sm_str_t const id = SM_STR_NEW_DEFAULT_NAME;
        char const* const saved = SM_STRINGS[SM_LANG_EN][id];
        (void)saved;
        i18n_test_override(id, nasties[i]);
        int const n = i18n_fmt(out, sizeof out, id, 5);
        CHECK(n >= 0 && n < (int)sizeof out, "%s produced %d bytes", nasties[i], n);
        CHECK(strlen(out) == (size_t)(n < (int)sizeof out ? n : (int)sizeof out - 1),
              "%s: length disagrees with the return", nasties[i]);
        i18n_test_override(id, NULL);
    }
    // "%s" where English says %d prints the NUMBER, not a wild pointer.
    i18n_test_override(SM_STR_NEW_DEFAULT_NAME, "Welt %s");
    i18n_fmt(buf, sizeof buf, SM_STR_NEW_DEFAULT_NAME, 5);
    CHECK(strcmp(buf, "Welt 5") == 0, "a wrong conversion was obeyed: %s", buf);
    i18n_test_override(SM_STR_NEW_DEFAULT_NAME, NULL);

    // Reordering, which is the whole point of doing this ourselves.
    i18n_test_override(SM_STR_WORLD_SUB, "seed %2$u in slot %1$d");
    i18n_fmt(buf, sizeof buf, SM_STR_WORLD_SUB, 3, 12345u);
    CHECK(strcmp(buf, "seed 12345 in slot 3") == 0, "reordered badly: %s", buf);
    i18n_test_override(SM_STR_WORLD_SUB, NULL);
    printf("  every character drawable, %d nasty format strings survived\n",
           (int)(sizeof nasties / sizeof nasties[0]) + 2);
}

// ---------------------------------------------------------------------
//  Music
//
//  The eleven MIDI files in assets/music are the soundtrack, and the
//  sequencer that reads them (main/audio/midi_seq.c) is pure, so both
//  can be checked here rather than by listening on the badge. What this
//  proves, for every file we ship:
//
//    * it parses, and has notes in it;
//    * it ENDS -- run at the real sample rate it reaches its last event
//      in a plausible number of minutes rather than looping forever on
//      a malformed delta;
//    * rewinding gives exactly the same performance again, which is
//      what lets the scheduler replay a piece without re-reading the
//      card;
//    * a TRUNCATED copy, at every length, still terminates and never
//      reads past the buffer -- the failure mode a file half-copied
//      onto an SD card would otherwise produce on the audio task.
// ---------------------------------------------------------------------

typedef struct {
    long notes;
    long offs;
    long programs;
    long note_sum;  // so two runs can be compared without keeping the notes
} midi_tally_t;

static void tally_on(void* ctx, uint8_t ch, uint8_t note, uint8_t vel) {
    midi_tally_t* t = ctx;
    t->notes++;
    t->note_sum += (long)note * 3 + (long)ch * 7 + (long)vel;
}
static void tally_off(void* ctx, uint8_t ch, uint8_t note) {
    midi_tally_t* t = ctx;
    t->offs++;
    t->note_sum += (long)note + (long)ch;
}
static void tally_prog(void* ctx, uint8_t ch, uint8_t prog) {
    midi_tally_t* t = ctx;
    t->programs++;
    (void)ch;
    (void)prog;
}
static midi_sink_t const TALLY = {
    .note_on = tally_on, .note_off = tally_off, .program = tally_prog};

// Run a sequence to its end, at 22050 Hz in the mixer's 256-frame
// blocks. Returns the playing time in seconds, or -1 if it did not end
// inside `cap_s` -- which is the check that matters, because a
// sequencer that never returns false hangs the audio task.
static double midi_play_out(midi_seq_t* s, midi_tally_t* t, double cap_s) {
    long const  cap_blocks = (long)(cap_s * 22050.0 / 256.0);
    for (long i = 0; i < cap_blocks; i++) {
        if (!midi_seq_advance(s, &TALLY, t, 256)) {
            return (double)(i + 1) * 256.0 / 22050.0;
        }
    }
    return -1.0;
}

static uint8_t* slurp(char const* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long const n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t* buf = malloc((size_t)n);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t const got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    *len = got;
    return buf;
}

static void check_midi(void) {
    printf("music: the shipped MIDI files\n");

    // The list is read from the directory rather than written out here,
    // so a piece added to assets/music is checked the day it is added
    // and one removed does not leave a check failing for a missing file.
    FILE* ls = popen("ls assets/music/*.mid 2>/dev/null", "r");
    if (ls == NULL) {
        CHECK(false, "cannot list assets/music");
        return;
    }

    int  files = 0;
    char path[256];
    while (fgets(path, sizeof(path), ls) != NULL) {
        char* nl = strchr(path, '\n');
        if (nl) *nl = '\0';
        if (path[0] == '\0') continue;
        files++;

        size_t   len = 0;
        uint8_t* buf = slurp(path, &len);
        if (buf == NULL) {
            CHECK(false, "%s: cannot read", path);
            continue;
        }

        midi_seq_t   seq;
        midi_tally_t t = {0};
        if (!midi_seq_load(&seq, buf, len, 22050u)) {
            CHECK(false, "%s: not a MIDI file the sequencer accepts", path);
            free(buf);
            continue;
        }

        // Twenty minutes is longer than anything in the repertoire and
        // far short of forever.
        double const secs = midi_play_out(&seq, &t, 20.0 * 60.0);
        CHECK(secs > 0.0, "%s: did not end inside twenty minutes", path);
        CHECK(t.notes > 32, "%s: only %ld notes; is this the right file?", path, t.notes);
        // Every note that starts should stop. A few strays are normal
        // (a piece ending on a held chord), but not hundreds.
        CHECK(t.offs >= t.notes - 16, "%s: %ld note-ons but only %ld note-offs", path, t.notes, t.offs);
        if (secs > 0.0) {
            CHECK(secs > 15.0, "%s: %.0f s is too short to be the piece", path, secs);
            printf("  %-42s %5.0f s  %5ld notes\n", strrchr(path, '/') + 1, secs, t.notes);
        }

        // Rewinding replays it identically.
        midi_seq_rewind(&seq);
        midi_tally_t again = {0};
        double const secs2 = midi_play_out(&seq, &again, 20.0 * 60.0);
        CHECK(again.notes == t.notes && again.note_sum == t.note_sum,
              "%s: a rewound replay differs (%ld vs %ld notes)", path, again.notes, t.notes);
        CHECK(secs2 == secs, "%s: a rewound replay runs %.2f s, not %.2f s", path, secs2, secs);

        // Every truncation of it must still terminate and stay inside
        // the buffer. Stepping by a prime keeps this quick while still
        // cutting in the middle of events, deltas and meta lengths.
        for (size_t cut = 14; cut < len; cut += 97) {
            midi_seq_t   ts;
            midi_tally_t tt = {0};
            if (!midi_seq_load(&ts, buf, cut, 22050u)) continue;  // refused: also fine
            double const cs = midi_play_out(&ts, &tt, 20.0 * 60.0);
            CHECK(cs > 0.0, "%s: truncated to %zu bytes, never ends", path, cut);
            if (cs <= 0.0) break;
        }

        free(buf);
    }
    pclose(ls);

    CHECK(files > 0, "no MIDI files in assets/music");

    // A file that is not MIDI at all, and one whose header lies.
    {
        midi_seq_t s;
        uint8_t const junk[64] = {'N', 'o', 'p', 'e'};
        CHECK(!midi_seq_load(&s, junk, sizeof(junk), 22050u), "junk accepted as a MIDI file");
        CHECK(!midi_seq_load(&s, NULL, 0, 22050u), "a null file accepted");

        // A valid header claiming 2000 tracks, with none of them there.
        uint8_t hdr[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0x07, 0xD0, 0x01, 0xE0};
        CHECK(!midi_seq_load(&s, hdr, sizeof(hdr), 22050u), "a header with no tracks accepted");

        // SMPTE division (the high bit set) is refused rather than played
        // at some invented speed.
        uint8_t smpte[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE8, 0x04};
        CHECK(!midi_seq_load(&s, smpte, sizeof(smpte), 22050u), "an SMPTE division accepted");
    }
}

// ---------------------------------------------------------------------
//  Menu labels fit their column
//
//  A settings row draws its label at the left and its value -- a slider,
//  a tick box, a word -- at a fixed offset from it (`value_dx` in
//  main/ui/menu.c). A label longer than that offset runs under the
//  value. In English nothing came close; in Ukrainian "Дальність
//  промальовування" was 492 px against a 260 px column, and three
//  screens were overlapping in a dozen languages before anyone measured
//  (F-76).
//
//  So measure. The width here is the engine's own -- the same
//  `hershey_advance` the renderer uses, at the same row height -- so a
//  label that passes here fits on the badge. When a column is widened
//  or a screen re-laid-out, the number below moves with it.
// ---------------------------------------------------------------------

#define ROW_TEXT_H    28.0f          // SE_UI_ROW_TEXT_H
#define HERSHEY_SCALE (21.0f / 28.0f)  // rendertext.c
#define HERSHEY_BASE  21.0f          // HERSHEY_DIRECT_BASE_HEIGHT

static float label_width(char const* s) {
    float const scale = (ROW_TEXT_H * HERSHEY_SCALE) / HERSHEY_BASE;
    int         w     = 0;
    for (;;) {
        uint32_t  cp;
        int const used = hershey_utf8_next(s, &cp);
        if (used == 0) break;
        s += used;
        if (cp != 0) w += (int)((float)hershey_advance(cp) * scale);
    }
    return (float)w;
}

// One row per screen that puts a value beside its labels: the key
// prefix, and the value column it has to stay clear of. A screen whose
// rows carry no value needs no entry -- its labels may run the width of
// the panel.
static struct {
    char const* prefix;
    float       value_dx;
} const LABEL_COLUMNS[] = {
    {"audio.", 390.0f},     // the wide panel (PANEL_W_WIDE)
    {"graphics.", 340.0f},  // ... and so is this one
    {"display.", 300.0f},
    {"settings.", 260.0f},
    // The crafting book and the "what it takes" panel put an ITEM NAME
    // in the label column with a value beside it. Russian "Деревянная
    // лопата" is half as wide again as "Wooden shovel", and nothing was
    // measuring it until the day the translations landed.
    {"item.", 380.0f},
};

// ---------------------------------------------------------------------
//  Lines that have to fit where they are drawn
//
//  check_label_widths above measures LABELS against their value column.
//  This measures everything else: the values themselves, the hint lines
//  and the free-standing ones, against the room each actually has.
//
//  It exists because the labels were not the problem the second time.
//  The user found "have 0, need 2 more" running out of the crafting
//  panel and off the right of the screen, and the inventory's legend
//  doing the same -- in ENGLISH, which is the shortest language here.
//  A check that only looked at labels could not see either.
//
//  A format string is measured with its blanks filled in the worst way
//  the game could fill them: every number 999 and every name the
//  LONGEST ITEM NAME IN THAT LANGUAGE, since that is what these lines
//  are made of.
// ---------------------------------------------------------------------

static float text_width_at(char const* s, float h) {
    float const scale = (h * HERSHEY_SCALE) / HERSHEY_BASE;
    int         w     = 0;
    for (;;) {
        uint32_t  cp;
        int const used = hershey_utf8_next(s, &cp);
        if (used == 0) break;
        s += used;
        if (cp != 0) w += (int)((float)hershey_advance(cp) * scale);
    }
    return (float)w;
}

// WHICH ITEMS A LINE CAN BE ASKED TO NAME. Not all of them are all of
// them, and the difference is what this check is for.
typedef enum {
    ITEMS_ANY = 0,   // anything the player can be carrying
    ITEMS_BOOK,      // only what the crafting book can put on a line
} item_scope_t;

// Can `id` appear in the crafting book at all -- as a row's output or
// in a row's ingredient footer?
//
// THE BOOK DOES NOT SHOW A MACHINE'S RECIPES. Nobody picks a row in a
// sausage maker, and the stove has a screen of its own (step 11), so a
// dish's name is never drawn on one of the book's lines. Scoping the
// ROOM to the book's own recipes without scoping the NAMES was half an
// answer: the day "Steak and potatoes" was added, the book's ingredient
// footer failed in eleven languages over a string it can never print.
static bool recipe_in_book(recipe_t const* r) {
    return r->station == RS_INVENTORY || r->station == RS_TABLE || r->station == RS_FURNACE;
}

static bool in_book(uint16_t id) {
    for (int ri = 0; ri < recipe_count(); ri++) {
        recipe_t const* r = recipe_at(ri);
        if (!recipe_in_book(r)) continue;
        if (r->out == id) return true;
        for (int i = 0; i < r->n_in; i++) {
            if (r->in[i].item == id) return true;
        }
    }
    return false;
}

// The longest thing a %s in one of these lines can be filled with.
static char const* longest_item_label(item_scope_t scope) {
    char const* worst = "";
    float       wmax  = 0.0f;
    for (uint16_t id = 1; id < ITEM_COUNT; id++) {
        if (id == BLK_AIR || id == BLK_BARRIER) continue;
        if (scope == ITEMS_BOOK && !in_book(id)) continue;
        sm_str_t const lab = item_label(id);
        if (lab == 0) continue;
        char const* const t = i18n_text(lab);
        float const       w = text_width_at(t, 28.0f);
        if (w > wmax) {
            wmax  = w;
            worst = t;
        }
    }
    return worst;
}

// Fill a format string the worst way the game can fill it.
static void expand_worst(char const* fmt, item_scope_t scope, char* out, size_t cap) {
    char const* const name = longest_item_label(scope);
    size_t            n    = 0;
    for (char const* p = fmt; *p != '\0' && n + 1 < cap; p++) {
        if (*p != '%') {
            out[n++] = *p;
            continue;
        }
        p++;
        if (*p == '\0') break;
        char const* ins = NULL;
        if (*p == 'd' || *p == 'u' || *p == 'i') {
            ins = "64";  // ITEM_STACK_MAX: no count is ever larger
        } else if (*p == 's') {
            ins = name;
        } else if (*p == '%') {
            ins = "%";
        } else {
            continue;  // a width or a flag: not worth modelling
        }
        for (char const* q = ins; *q != '\0' && n + 1 < cap; q++) out[n++] = *q;
    }
    out[n] = '\0';
}

// The room a line gets, worked out the way se_ui.c works it out:
//     panel_x = (800 - 800f) / 2
//     text_x  = panel_x + SE_UI_TEXT_INSET + SE_UI_CHEVRON_GUTTER
// so a label has (800f - 50 - 28) and a value (800f - 50 - dx - 28).
#define PANEL_ROOM(f)         ((f) * 800.0f - 50.0f - 28.0f)
#define VALUE_ROOM(f, dx)     (PANEL_ROOM(f) - (dx))

// A LINE THAT IS BUILT OUT OF SEVERAL COPIES OF ONE STRING, or out of
// several strings, rather than drawn on its own. Its width and its
// contents are decided together, so the only honest check is to
// assemble the real thing and measure it -- see the note in
// check_text_fits().
typedef enum {
    ASM_NONE = 0,     // an ordinary line: it gets `px` to itself
    ASM_BOOK_ING,     // the crafting book's footer: every ingredient of one recipe
    ASM_STOVE_DISH,   // the stove picker's footer: what a dish takes, then what it is worth
} assembled_t;

static struct {
    char const* key;
    float       h;   // the height it is drawn at
    float       px;  // the room it has, or 0 when `asm_kind` says it is built
    // WHICH ITEMS ITS %s CAN NAME. A machine's screen lists whatever
    // the player is carrying, so ITEMS_ANY; the crafting book draws
    // only its own recipes, so ITEMS_BOOK. Left out of a row means
    // ITEMS_ANY, which is the safe way round.
    item_scope_t scope;
    assembled_t  asm_kind;
} const TEXT_BUDGETS[] = {
    // The inventory screen's legend, centred on the whole display.
    {.key = "hud.inventory_hint", .h = 16.0f, .px = 800.0f - 32.0f},

    // The crafting book: the list's value column, and its footer.
    {.key = "craft.value_make", .h = 28.0f, .px = VALUE_ROOM(0.88f, 380.0f), .scope = ITEMS_BOOK},
    {.key = "craft.value_missing", .h = 28.0f, .px = VALUE_ROOM(0.88f, 380.0f), .scope = ITEMS_BOOK},
    // Several of these share one line. The share is worked out from the
    // recipe table below, not guessed here, so a recipe with another
    // ingredient in it tightens this automatically.
    {.key = "craft.ing", .h = 14.0f, .px = 0.0f, .scope = ITEMS_BOOK, .asm_kind = ASM_BOOK_ING},
    {.key = "craft.hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.search", .h = 18.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.made", .h = 14.0f, .px = PANEL_ROOM(0.88f), .scope = ITEMS_BOOK},
    {.key = "craft.full", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.empty_sub", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.empty", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.no_match", .h = 28.0f, .px = PANEL_ROOM(0.88f)},

    // ... and the "what it takes" panel, which is where it went wrong.
    {.key = "craft.detail_have", .h = 28.0f, .px = VALUE_ROOM(0.88f, 380.0f), .scope = ITEMS_BOOK},
    {.key = "craft.detail_sub", .h = 18.0f, .px = PANEL_ROOM(0.88f), .scope = ITEMS_BOOK},
    {.key = "craft.detail_hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},

    // The chest screen, which is drawn by hand rather than by se_ui:
    // two grids of CHEST_SLOT_W, with lines centred on the display.
    {.key = "chest.title", .h = 30.0f, .px = 800.0f - 32.0f},
    {.key = "chest.title_trash", .h = 30.0f, .px = 800.0f - 32.0f},
    {.key = "chest.yours", .h = 18.0f, .px = 6.0f * 44.0f + 5.0f * 4.0f},
    {.key = "chest.hint", .h = 15.0f, .px = 800.0f - 32.0f},
    {.key = "chest.trash_warn", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "chest.trash_gone", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "chest.no_room", .h = 16.0f, .px = 800.0f - 32.0f},

    // The bench, and the planner's state, which shares the search line.
    {.key = "bench.title", .h = 32.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "bench.hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "bench.empty", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "bench.gives", .h = 18.0f, .px = PANEL_ROOM(0.88f), .scope = ITEMS_BOOK},
    {.key = "bench.done", .h = 18.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "craft.auto_on", .h = 18.0f, .px = PANEL_ROOM(0.88f) / 2.0f},
    {.key = "craft.auto_off", .h = 18.0f, .px = PANEL_ROOM(0.88f) / 2.0f},
    {.key = "craft.auto_made", .h = 14.0f, .px = PANEL_ROOM(0.88f), .scope = ITEMS_BOOK},

    // The "how many?" modal: a fixed 420 px box, so its own width and
    // not a fraction of the panel.
    {.key = "amount.title", .h = 22.0f, .px = 640.0f - 44.0f},
    {.key = "amount.hint", .h = 14.0f, .px = 640.0f - 44.0f},

    // THE COMPASS (game/hud.c): one or two letters between ticks 32 px
    // apart, so the room is what is between two ticks less a margin. A
    // translator who writes the whole word here fails this.
    {.key = "dir.n.short", .h = 13.0f, .px = 30.0f},
    {.key = "dir.e.short", .h = 13.0f, .px = 30.0f},
    {.key = "dir.s.short", .h = 13.0f, .px = 30.0f},
    {.key = "dir.w.short", .h = 13.0f, .px = 30.0f},

    // The loading and saving screens: one line, centred on the display.
    {.key = "loading.plain", .h = 30.0f, .px = 800.0f - 32.0f},
    {.key = "loading.world", .h = 30.0f, .px = 800.0f - 32.0f},
    {.key = "loading.creating", .h = 30.0f, .px = 800.0f - 32.0f},
    {.key = "loading.saving", .h = 30.0f, .px = 800.0f - 32.0f},

    // The line a block shows when it will not break, top left.
    {.key = "hud.needs_tool", .h = 16.0f, .px = 800.0f - 32.0f},

    // The furnace: three rows with a value column, a subtitle that says
    // what it is doing, and the picker over the player's own stacks.
    {.key = "furnace.slot", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "furnace.empty", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "furnace.input", .h = 28.0f, .px = 240.0f},
    {.key = "furnace.fuel", .h = 28.0f, .px = 240.0f},
    {.key = "furnace.output", .h = 28.0f, .px = 240.0f},
    {.key = "furnace.smelting", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.no_input", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.no_fuel", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.full", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.took", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.hint", .h = 14.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "furnace.becomes", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "furnace.burns", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "furnace.pick_hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "furnace.pick_none_input", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "furnace.pick_none_fuel", .h = 28.0f, .px = PANEL_ROOM(0.88f)},

    // THE TWO MAKERS (step 10), which never had budget lines -- the
    // screen was added and this table was not, so nothing measured it
    // for a week. Same geometry as the furnace above: a 0.94 panel with
    // a 240 px value column, and a 0.88 picker.
    {.key = "maker.slot", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "maker.empty", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "maker.input", .h = 28.0f, .px = 240.0f},
    {.key = "maker.output", .h = 28.0f, .px = 240.0f},
    {.key = "maker.extra", .h = 28.0f, .px = 240.0f},
    {.key = "maker.cheese_title", .h = 32.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.sausage_title", .h = 32.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.working", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.no_input", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.full", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.took", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.bucket_back", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.hint", .h = 14.0f, .px = PANEL_ROOM(0.94f)},
    // "Needs 1 Red flower", and the line that joins two of them. `or`
    // is filled with the longest label in BOTH of its slots, which is
    // worse than the real thing can be -- an over-estimate is the safe
    // way to be wrong about a line nobody can see overflow.
    {.key = "maker.missing", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.or", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "maker.take_out", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "maker.pick_input", .h = 32.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "maker.pick_none", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "maker.pick_hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "maker.a_day", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "maker.a_minute", .h = 14.0f, .px = PANEL_ROOM(0.88f)},

    // THE KITCHEN STOVE (step 11), which Part A said would need a
    // budget line of its own the day it existed. Same shape as the
    // furnace above -- three rows with a value column, a subtitle that
    // says what it is doing, and two pickers -- plus the dish list's
    // footer, which is assembled out of two strings and is checked by
    // building it.
    {.key = "stove.slot", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "stove.empty", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "stove.none", .h = 28.0f, .px = VALUE_ROOM(0.94f, 240.0f)},
    {.key = "stove.dish", .h = 28.0f, .px = 240.0f},
    {.key = "stove.fuel", .h = 28.0f, .px = 240.0f},
    {.key = "stove.output", .h = 28.0f, .px = 240.0f},
    {.key = "stove.title", .h = 32.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.cooking", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.no_pick", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.no_chest", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.missing", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.no_fuel", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.full", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.took", .h = 18.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.hint", .h = 14.0f, .px = PANEL_ROOM(0.94f)},
    {.key = "stove.pick_title", .h = 32.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "stove.pick_fuel", .h = 32.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "stove.pick_none_fuel", .h = 28.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "stove.pick_hint", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    {.key = "stove.from_chest", .h = 14.0f, .px = PANEL_ROOM(0.88f)},
    // The dish list's rows are DISH NAMES, which is the same trap the
    // crafting book's rows were (step 8.5): the row is the item label,
    // so the longest one in any language is what has to fit.
    {.key = "stove.needs", .h = 14.0f, .px = 0.0f, .scope = ITEMS_ANY, .asm_kind = ASM_STOVE_DISH},
    {.key = "stove.feeds", .h = 18.0f, .px = PANEL_ROOM(0.88f)},

    // The two lines hunger puts on the HUD, centred on the display like
    // every other thing said under the crosshair.
    {.key = "farm.needs_ground", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "food.not_hungry", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "food.starving", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "food.died", .h = 16.0f, .px = 800.0f - 32.0f},
    {.key = "food.respawn", .h = 16.0f, .px = 800.0f - 32.0f},
};

static void check_text_fits(void) {
    printf("text: does every line fit where it is drawn\n");

    sm_lang_t const was = i18n_language();
    float       worst_slack = 1e9f;
    char const* worst_key   = "";
    char const* worst_lang  = "";

    for (int li = 0; li < SM_LANG_COUNT; li++) {
        i18n_set_language((sm_lang_t)li);
        for (size_t bi = 0; bi < sizeof(TEXT_BUDGETS) / sizeof(TEXT_BUDGETS[0]); bi++) {
            char const* const key = TEXT_BUDGETS[bi].key;

            int found = -1;
            for (int k = 0; k < SM_STR_COUNT; k++) {
                if (strcmp(SM_STR_KEYS[k], key) == 0) found = k;
            }
            CHECK(found >= 0, "no string called %s -- the budget table has gone stale", key);
            if (found < 0) continue;

            // A BUDGET OF 0 MEANS "BUILD THE REAL LINE". The crafting
            // book's ingredient footer is the one line here whose width
            // and whose contents are decided together: it is every
            // ingredient of ONE recipe, three spaces apart, and how
            // much room each gets is how many that recipe has.
            //
            // The generic path below cannot say that. It pairs the
            // longest label in the game with the tightest division, and
            // those two never meet -- "Table for disassembly" is not an
            // ingredient of the three-part recipe that made the
            // division tight. That is how this line came to fail in
            // thirteen languages over a sentence none of them can
            // print, on the day the stove became the book's first
            // recipe with three ingredients.
            //
            // So this assembles each book recipe's footer exactly as
            // craft_ui.c assembles it, with the worst `have` the count
            // can be, and asks whether THAT fits.
            if (TEXT_BUDGETS[bi].asm_kind == ASM_STOVE_DISH) {
                // THE STOVE PICKER'S FOOTER, assembled exactly as
                // stove_ui.c assembles it: every ingredient of the
                // dish under the cursor, then what eating it is worth.
                for (int d = 0; d < stove_dish_count(); d++) {
                    recipe_t const* r = stove_dish_at(d);
                    char            takes[192];
                    takes[0] = '\0';
                    for (int i = 0; i < r->n_in; i++) {
                        char part[64];
                        i18n_fmt(part, sizeof(part), (sm_str_t)found, (int)r->in[i].count,
                                 T(item_label(r->in[i].item)));
                        if (takes[0] != '\0') strncat(takes, ", ", sizeof(takes) - strlen(takes) - 1);
                        strncat(takes, part, sizeof(takes) - strlen(takes) - 1);
                    }
                    // The ingredients get the hint line to themselves;
                    // what the dish is worth is the subtitle above it
                    // and is checked as an ordinary budget row below.
                    char const* const line = takes;
                    float const lw = text_width_at(line, TEXT_BUDGETS[bi].h);
                    float const lr = PANEL_ROOM(0.88f);
                    CHECK(lw <= lr, "%s/%s: \"%s\" is %.0f px, room is %.0f",
                          i18n_language_code((sm_lang_t)li), key, line, (double)lw, (double)lr);
                    if (lr - lw < worst_slack) {
                        worst_slack = lr - lw;
                        worst_key   = key;
                        worst_lang  = i18n_language_code((sm_lang_t)li);
                    }
                    // ... and the ROW it belongs to, which is the dish's
                    // own name at the picker's row height.
                    char const* const row = T(item_label(r->out));
                    float const       rw  = text_width_at(row, 28.0f);
                    CHECK(rw <= PANEL_ROOM(0.88f), "%s/stove dish row: \"%s\" is %.0f px, room is %.0f",
                          i18n_language_code((sm_lang_t)li), row, (double)rw, (double)PANEL_ROOM(0.88f));
                }
                continue;
            }

            if (TEXT_BUDGETS[bi].asm_kind == ASM_BOOK_ING) {
                for (int ri = 0; ri < recipe_count(); ri++) {
                    recipe_t const* r = recipe_at(ri);
                    if (!recipe_in_book(r)) continue;
                    char line[256];
                    line[0] = '\0';
                    for (int i = 0; i < r->n_in; i++) {
                        char part[64];
                        i18n_fmt(part, sizeof(part), (sm_str_t)found, T(item_label(r->in[i].item)),
                                 ITEM_STACK_MAX, (int)r->in[i].count);
                        if (line[0] != '\0') strncat(line, "   ", sizeof(line) - strlen(line) - 1);
                        strncat(line, part, sizeof(line) - strlen(line) - 1);
                    }
                    float const lw = text_width_at(line, TEXT_BUDGETS[bi].h);
                    float const lr = PANEL_ROOM(0.88f);
                    CHECK(lw <= lr, "%s/%s: \"%s\" is %.0f px, room is %.0f",
                          i18n_language_code((sm_lang_t)li), key, line, (double)lw, (double)lr);
                    if (lr - lw < worst_slack) {
                        worst_slack = lr - lw;
                        worst_key   = key;
                        worst_lang  = i18n_language_code((sm_lang_t)li);
                    }
                }
                continue;
            }

            char filled[256];
            expand_worst(i18n_text((sm_str_t)found), TEXT_BUDGETS[bi].scope, filled, sizeof(filled));
            float const w = text_width_at(filled, TEXT_BUDGETS[bi].h);

            float const room  = TEXT_BUDGETS[bi].px;
            float const slack = room - w;
            CHECK(slack >= 0.0f, "%s/%s: \"%s\" is %.0f px, room is %.0f",
                  i18n_language_code((sm_lang_t)li), key, filled, (double)w, (double)room);
            if (slack < worst_slack) {
                worst_slack = slack;
                worst_key   = key;
                worst_lang  = i18n_language_code((sm_lang_t)li);
            }
        }
    }
    i18n_set_language(was);
    printf("  %d lines x %d languages; tightest %.0f px to spare (%s, %s)\n",
           (int)(sizeof(TEXT_BUDGETS) / sizeof(TEXT_BUDGETS[0])), SM_LANG_COUNT, (double)worst_slack,
           worst_key, worst_lang);
}

static void check_label_widths(void) {
    printf("menu labels: do they fit beside their values\n");

    int worst_n = 0;
    float worst = 0.0f;
    char const* worst_text = "";

    for (int li = 0; li < SM_LANG_COUNT; li++) {
        i18n_set_language((sm_lang_t)li);
        for (int si = 0; si < (int)(sizeof(LABEL_COLUMNS) / sizeof(LABEL_COLUMNS[0])); si++) {
            char const* const pre = LABEL_COLUMNS[si].prefix;
            size_t const      n   = strlen(pre);
            for (int k = 0; k < SM_STR_COUNT; k++) {
                char const* const key = SM_STR_KEYS[k];
                if (strncmp(key, pre, n) != 0) continue;
                // Titles, subtitles and hints are not rows and are not
                // beside anything.
                char const* const tail = key + n;
                if (strcmp(tail, "title") == 0 || strcmp(tail, "sub") == 0 || strcmp(tail, "hint") == 0) continue;

                char const* const text = i18n_text((sm_str_t)k);
                float const       w    = label_width(text);
                CHECK(w < LABEL_COLUMNS[si].value_dx, "%s/%s: \"%s\" is %.0f px, column is %.0f",
                      i18n_language_code((sm_lang_t)li), key, text, (double)w,
                      (double)LABEL_COLUMNS[si].value_dx);
                float const slack = LABEL_COLUMNS[si].value_dx - w;
                if (worst_n == 0 || slack < worst) {
                    worst      = slack;
                    worst_text = text;
                    worst_n    = 1;
                }
            }
        }
    }
    i18n_set_language(SM_LANG_EN);
    printf("  tightest fit: %.0f px to spare (\"%s\")\n", (double)worst, worst_text);
}

// ---------------------------------------------------------------------
//  Farming (step 9)
//
//  The claims worth defending, in the order they matter:
//
//    * a hoe makes farmland, and WET or DRY is decided by water within
//      four blocks on the same level -- once, when it is tilled (D-106);
//    * dry soil REFUSES a seed rather than swallowing it;
//    * a crop grows on the chunk's own slow clock, catches up after
//      an absence, and costs nothing in a chunk with nothing growing;
//    * the clock survives a save and a load, because a chunk that came
//      back without one would ripen the next thing planted on the spot;
//    * a harvest depends on how grown the plant was;
//    * rice is water AND a plant, and the pond around it does not drain;
//    * the composter turns a day into a unit and 0 to 2 worms, and banks
//      nothing while it is empty.
// ---------------------------------------------------------------------

// How many crop cells the whole resident set holds, and how many of
// those are ripe.
static int crop_count(int* ripe_out) {
    int n = 0, ripe = 0;
    for (int i = 0; i < CH_SLOT_COUNT; i++) {
        chunk_t const* c = chunk_slot_at(i);
        if (c == NULL || c->cstate != CS_READY) continue;
        int r = 0;
        n += crops_count_in(c, &r);
        ripe += r;
    }
    if (ripe_out != NULL) *ripe_out = ripe;
    return n;
}

// Run the slow sweep for `ticks`, with the world clock advancing with it
// -- which is what the game does (main.c).
static void crop_run(uint32_t* clock, int ticks) {
    for (int i = 0; i < ticks; i++) crops_tick((*clock)++);
}

static void check_farming(void) {
    printf("farming: soil, crops and the slow clock\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(20) != NULL, "the farming world would not become resident");
    uint32_t clock = 100000;  // a world that has been played for a while

    // --- Tilling, and the water rule ---------------------------------
    for (int x = 0; x < 12; x++)
        for (int z = 0; z < 12; z++) set_block(x, 19, z, BLK_GRASS, 0);

    CHECK(crops_till(2, 19, 2), "a hoe would not turn over grass");
    CHECK(world_block(2, 19, 2) == BLK_FARMLAND, "tilled grass with no water near it came out wet");
    CHECK(!crops_till(2, 20, 2), "a hoe tilled the air above the soil");

    // Water four away wets it; five away does not. The exact edge is the
    // rule, so both sides of it are checked.
    set_block(8, 19, 2, BLK_WATER, ST_PLACED);
    CHECK(crops_water_near(4, 19, 2), "water 4 blocks away did not wet the soil");
    CHECK(!crops_water_near(3, 19, 2), "water 5 blocks away wet the soil anyway");
    CHECK(!crops_water_near(4, 18, 2), "water on another level wet the soil");
    CHECK(crops_till(4, 19, 2), "a hoe would not turn over grass beside water");
    CHECK(world_block(4, 19, 2) == BLK_FARMLAND_WET, "soil four blocks from water came out dry");

    // Re-tilling is how a plot's wet/dry state is refreshed, which is
    // the user's own instruction: nothing else ever looks again.
    set_block(8, 19, 2, BLK_AIR, 0);
    CHECK(crops_till(4, 19, 2), "an empty plot could not be re-tilled");
    CHECK(world_block(4, 19, 2) == BLK_FARMLAND, "re-tilling after the water went did not dry the plot");
    set_block(8, 19, 2, BLK_WATER, ST_PLACED);
    CHECK(crops_till(4, 19, 2) && world_block(4, 19, 2) == BLK_FARMLAND_WET, "re-tilling did not wet the plot again");

    // --- Dry soil refuses the seed -----------------------------------
    CHECK(crops_plant(2, 19, 2, ITEM_WHEAT_SEEDS) == PLANT_TOO_DRY, "dry soil took a seed");
    CHECK(world_block(2, 20, 2) == BLK_AIR, "a refused seed still planted something");
    CHECK(crops_plant(1, 19, 1, ITEM_WHEAT_SEEDS) == PLANT_NEEDS_SOIL, "a seed went into untilled grass");
    CHECK(crops_plant(4, 19, 2, ITEM_COAL) == PLANT_NOT_SEED, "coal planted a crop");
    CHECK(crops_plant(4, 19, 2, ITEM_WHEAT_SEEDS) == PLANT_OK, "wet soil refused a seed");
    CHECK(world_block(4, 20, 2) == BLK_WHEAT_CROP, "planting put nothing above the soil");
    CHECK(crop_stage(world_state(4, 20, 2)) == 0, "a new seedling did not start at stage 0");

    // --- It grows on the chunk's clock -------------------------------
    chunk_t* c = chunk_find(0, 0);
    CHECK(c != NULL && (c->flags & CF_CROPS) != 0, "planting did not mark the chunk as having crops");

    // Not instantly: a seed planted now is not ripe a tick later.
    crop_run(&clock, 300);
    CHECK(crop_stage(world_state(4, 20, 2)) == 0, "a seedling grew in fifteen seconds");

    // A full stage's worth of ticks, plus a round of the sweep so the
    // chunk is actually visited. Wheat is an in-game DAY from seed to
    // harvest (the user), which is three stages of CROP_TICKS_DAY.
    uint32_t const wheat_stage = block_def(BLK_WHEAT_CROP)->grow_ticks;
    CHECK(wheat_stage == CROP_TICKS_DAY, "wheat grows a stage every %u ticks, expected %u", wheat_stage,
          CROP_TICKS_DAY);
    CHECK((uint32_t)block_def(BLK_WHEAT_CROP)->growth_max * wheat_stage == DAY_TICKS,
          "wheat takes %u ticks from seed to harvest, expected one in-game day (%u)",
          (unsigned)block_def(BLK_WHEAT_CROP)->growth_max * wheat_stage, DAY_TICKS);
    CHECK((uint32_t)block_def(BLK_POTATO_CROP)->growth_max * block_def(BLK_POTATO_CROP)->grow_ticks == 2u * DAY_TICKS,
          "a potato does not take two in-game days");
    CHECK((uint32_t)block_def(BLK_RICE_CROP)->growth_max * block_def(BLK_RICE_CROP)->grow_ticks == 2u * DAY_TICKS,
          "rice does not take two in-game days");

    crop_run(&clock, (int)wheat_stage + 512);
    CHECK(crop_stage(world_state(4, 20, 2)) == 1, "a crop did not gain a stage in %u ticks (it is at %u)",
          wheat_stage, crop_stage(world_state(4, 20, 2)));

    // ... and it stops at ripe rather than running off the end of its
    // three bits.
    crop_run(&clock, (int)wheat_stage * 8 + 512);
    {
        int ripe = 0;
        int const n = crop_count(&ripe);
        printf("  the field is %d plants, %d of them ripe\n", n, ripe);
        CHECK(n == 1 && ripe == 1, "%d crops, %d ripe: expected one of each", n, ripe);
    }
    CHECK(crop_is_ripe(BLK_WHEAT_CROP, world_state(4, 20, 2)), "a crop left for eight stages is not ripe");
    CHECK(crop_stage(world_state(4, 20, 2)) == BLOCKS[BLK_WHEAT_CROP].growth_max, "a ripe crop grew past its last stage");

    // --- A chunk that was away shows the time it was away ------------
    //
    // The user's requirement, and the reason the clock is saved at all:
    // "advance events in one go to where they would be now as if the
    // chunk was never unloaded."
    crops_plant(4, 19, 2, ITEM_WHEAT_SEEDS);  // (the ripe one is still there)
    set_block(6, 19, 2, BLK_FARMLAND_WET, ST_PLACED);
    CHECK(crops_plant(6, 19, 2, ITEM_WHEAT_SEEDS) == PLANT_OK, "a second seed would not go in");
    CHECK(crop_stage(world_state(6, 20, 2)) == 0, "the second seedling did not start at 0");

    c->stamp = clock;               // up to date as of now
    clock += wheat_stage * 2;  // ... and then two stages go by with nobody there
    crops_chunk_join(c, clock);
    CHECK(crop_stage(world_state(6, 20, 2)) == 2, "a chunk away for two stages came back at stage %u",
          crop_stage(world_state(6, 20, 2)));

    // --- A DAY AFTER IT WAS SOWN, NOT AFTER THE CHUNK'S CLOCK --------
    //
    // The whole point of the phase (crops.h). The chunk keeps ONE clock
    // and growth happens on absolute boundaries, so without it every
    // plant in a chunk would ripen at the same instant whenever it went
    // in -- and a seed sown just before a boundary would gain a free
    // stage worth a third of its life.
    //
    // Two seeds, sown a third of a stage apart, and both have to take
    // the same time.
    {
        uint32_t const iv = block_def(BLK_WHEAT_CROP)->grow_ticks;
        uint32_t const want = iv * block_def(BLK_WHEAT_CROP)->growth_max;  // an in-game day
        int            measured[2];
        for (int k = 0; k < 2; k++) {
            // Start each one at a deliberately awkward moment.
            clock += (uint32_t)k * (iv / 3u) + 137u;
            crops_tick(clock);  // so planted_state sees this moment
            int32_t const px = 11 + k;
            set_block(px, 19, 11, BLK_FARMLAND_WET, ST_PLACED);
            CHECK(crops_plant(px, 19, 11, ITEM_WHEAT_SEEDS) == PLANT_OK, "the timing seed would not go in");
            uint32_t const sown = clock;
            int            guard = 0;
            while (!crop_is_ripe(BLK_WHEAT_CROP, world_state(px, 20, 11)) && guard++ < 200) {
                crop_run(&clock, 512);  // two full rounds of the sweep
            }
            measured[k] = (int)(clock - sown);
            CHECK(crop_is_ripe(BLK_WHEAT_CROP, world_state(px, 20, 11)), "the timing seed never ripened");
            set_block(px, 20, 11, BLK_AIR, 0);
        }
        // The sweep visits a chunk every 256 ticks and a stage boundary
        // can fall anywhere between two visits, so the slack is one
        // round of the sweep plus a sixteenth of a stage.
        int const slack = 512 + (int)(iv / CROP_PHASES);
        printf("  wheat sown at two different moments ripened in %d and %d ticks (a day is %u)\n", measured[0],
               measured[1], want);
        for (int k = 0; k < 2; k++) {
            CHECK(measured[k] >= (int)want - slack && measured[k] <= (int)want + slack,
                  "wheat took %d ticks to ripen, expected about %u (+/- %d)", measured[k], want, slack);
        }
    }

    // --- Nothing grows in a chunk with nothing in it -----------------
    chunk_t* empty = chunk_find(1, 1);
    CHECK(empty != NULL, "the neighbour chunk is not resident");
    CHECK((empty->flags & CF_CROPS) == 0, "a chunk with no crops is marked as having some");
    uint32_t const before_stamp = empty->stamp;
    crop_run(&clock, 600);
    CHECK(empty->stamp != before_stamp || before_stamp == 0, "an empty chunk's clock never moved");
    CHECK((empty->flags & CF_CROPS) == 0, "an empty chunk gained the crop flag from the sweep");

    // A harvested field stops being swept: the flag clears itself.
    set_block(6, 20, 2, BLK_AIR, 0);
    set_block(4, 20, 2, BLK_AIR, 0);
    crop_run(&clock, (int)wheat_stage + 600);
    CHECK((chunk_find(0, 0)->flags & CF_CROPS) == 0, "a harvested chunk is still marked as having crops");

    // --- FARMING WAKES NO PHYSICS ------------------------------------
    //
    // Crops are tier 2 and must never enter the tick wheel: that is what
    // blockupdate.h promises in its own header, and a field of wheat
    // quietly joining the fluid queue would be a frame-rate bug nobody
    // would think to look for here.
    blockupdate_clear();
    set_block(7, 19, 7, BLK_FARMLAND_WET, ST_PLACED);
    CHECK(crops_plant(7, 19, 7, ITEM_POTATO) == PLANT_OK, "a potato would not go in");
    crop_run(&clock, (int)block_def(BLK_POTATO_CROP)->grow_ticks * 4 + 600);
    blockupdate_stats_t const bst = blockupdate_stats();
    printf("  a field grown from seed to ripe left %d cells in the physics queue\n", bst.pending);
    CHECK(bst.pending == 0, "growing crops put %d cells in the tick wheel", bst.pending);
    CHECK(crop_is_ripe(BLK_POTATO_CROP, world_state(7, 20, 7)), "the potato did not ripen");

    // --- What a harvest gives ----------------------------------------
    //
    // Ripe: the harvest, plus a seed where the seed and the crop are
    // different things. Unripe: the seed back, and nothing else.
    item_entity_reset();
    int before = item_entity_live();
    interact_break(7, 20, 7, 0);
    CHECK(item_entity_live() > before, "a ripe potato dropped nothing");

    set_block(3, 19, 7, BLK_FARMLAND_WET, ST_PLACED);
    CHECK(crops_plant(3, 19, 7, ITEM_WHEAT_SEEDS) == PLANT_OK, "wheat would not go in");
    item_entity_reset();
    interact_break(3, 20, 7, 0);
    CHECK(item_entity_live() == 1, "an unripe crop dropped %d stacks, not one seed", item_entity_live());

    // --- WHAT EACH CROP IS WORTH RIPE (the user's numbers) -----------
    //
    // Wheat has to give back MORE SEED THAN IT TOOK or a field can never
    // be bigger than the tall grass somebody cut; a tomato gives no seed
    // at all, because its seeds come off the crafting table, and is
    // worth more fruit instead.
    {
        struct {
            uint8_t  crop;
            uint16_t seed, fruit;
            int      fruit_lo, fruit_hi, seed_lo, seed_hi;
        } const want[] = {
            {BLK_WHEAT_CROP, ITEM_WHEAT_SEEDS, ITEM_WHEAT, 1, 3, 1, 2},
            {BLK_POTATO_CROP, ITEM_POTATO, ITEM_POTATO, 1, 3, 0, 0},
            {BLK_TOMATO_CROP, ITEM_TOMATO_SEEDS, ITEM_TOMATO, 2, 4, 0, 0},
            {BLK_BEAN_CROP, ITEM_BEANS, ITEM_BEANS, 1, 3, 0, 0},
        };
        for (size_t k = 0; k < sizeof want / sizeof want[0]; k++) {
            block_def_t const* bd = block_def(want[k].crop);
            CHECK(bd->drop_min == want[k].fruit_lo && bd->drop_max == want[k].fruit_hi,
                  "%s yields %u-%u fruit, expected %d-%d", bd->name, bd->drop_min, bd->drop_max, want[k].fruit_lo,
                  want[k].fruit_hi);
            CHECK(bd->seed_min == want[k].seed_lo && bd->seed_max == want[k].seed_hi,
                  "%s yields %u-%u seeds, expected %d-%d", bd->name, bd->seed_min, bd->seed_max, want[k].seed_lo,
                  want[k].seed_hi);

            // And what actually comes out of the ground, over enough
            // harvests to see the whole range.
            int lo_f = 99, hi_f = 0, lo_s = 99, hi_s = 0, saw_seed_stack = 0;
            for (int t = 0; t < 60; t++) {
                crops_tick(300000u + (uint32_t)t * 37u);  // a different moment each time
                set_block(9, 19, 9, BLK_FARMLAND_WET, ST_PLACED);
                set_block(9, 20, 9, want[k].crop, crop_state_with(ST_PLACED, bd->growth_max));
                item_entity_reset();
                interact_break(9, 20, 9, 0);
                int fruit = 0, seed = 0, stacks = 0;
                for (int i = 0; i < ITEM_ENTITY_MAX; i++) {
                    item_entity_t const* e = item_entity_at(i);
                    if (e == NULL || !e->alive) continue;
                    stacks++;
                    if (e->item == want[k].fruit) fruit += e->count;
                    else if (e->item == want[k].seed) seed += e->count;
                }
                if (want[k].seed == want[k].fruit) seed = 0;  // a potato is its own seed
                if (seed > 0) saw_seed_stack++;
                if (fruit < lo_f) lo_f = fruit;
                if (fruit > hi_f) hi_f = fruit;
                if (seed < lo_s) lo_s = seed;
                if (seed > hi_s) hi_s = seed;
                (void)stacks;
            }
            printf("  %-12s 60 harvests: %d-%d fruit, %d-%d seeds\n", bd->name, lo_f, hi_f, lo_s, hi_s);
            CHECK(lo_f == want[k].fruit_lo && hi_f == want[k].fruit_hi,
                  "%s dropped %d-%d fruit over 60 harvests, expected the full %d-%d", bd->name, lo_f, hi_f,
                  want[k].fruit_lo, want[k].fruit_hi);
            CHECK(lo_s == want[k].seed_lo && hi_s == want[k].seed_hi,
                  "%s dropped %d-%d seeds over 60 harvests, expected the full %d-%d", bd->name, lo_s, hi_s,
                  want[k].seed_lo, want[k].seed_hi);
            // THE SAME PLOT, SIXTY TIMES, and the count moved: the yield
            // is rolled at harvest and not baked into the cell.
            if (want[k].fruit_hi > want[k].fruit_lo) {
                CHECK(hi_f > lo_f, "%s gave the same %d fruit every time from the same plot", bd->name, lo_f);
            }
            CHECK(want[k].seed_hi == 0 || saw_seed_stack > 0, "%s never gave a seed back", bd->name);
        }
        set_block(9, 20, 9, BLK_AIR, 0);
    }

    // --- A CROP CANNOT STAND ON NOTHING ------------------------------
    set_block(3, 19, 8, BLK_FARMLAND_WET, ST_PLACED);
    CHECK(crops_plant(3, 19, 8, ITEM_WHEAT_SEEDS) == PLANT_OK, "wheat would not go in above the soil");
    item_entity_reset();
    interact_break(3, 19, 8, 0);  // dig out the soil under it
    CHECK(world_block(3, 20, 8) == BLK_AIR, "the crop stayed in the air after its soil was dug out");
    CHECK(item_entity_live() >= 2, "digging out the soil under a crop dropped %d stacks, not soil and seed",
          item_entity_live());

    // --- The clock survives the card ---------------------------------
    //
    // Not the encoder in isolation: the whole path, through a real
    // region file. The decode side runs on the core-1 worker and has to
    // put the number back into the CHUNK being filled rather than into
    // a pool, which is the one thing about this section that is not
    // like the block entities beside it (region.c, take_section).
    {
        uint8_t      buf[64];
        chunk_t*     ch = chunk_find(0, 0);
        ch->stamp       = 123456u;
        size_t const n  = crops_encode_chunk(ch, buf, sizeof(buf));
        CHECK(n == 9, "the clock section is %zu bytes, not 9", n);
        CHECK(buf[0] == SECTION_CHUNK_CLOCK, "the clock section has the wrong id");
        ch->stamp = 0;
        crops_decode_section(ch, &buf[5], 4);
        CHECK(ch->stamp == 123456u, "the clock did not survive a round trip (%u)", ch->stamp);

        // Its OWN directory: TEST_DIR has been written, torn, damaged and
        // compacted by the checks above, and a region file left in one
        // of those states is not what this is testing.
        #define FARM_DIR "build/host/farmtest"
        CHECK(sm_mkdir_p(FARM_DIR), "could not create " FARM_DIR);
        {
            char path[192];
            region_path(path, sizeof(path), FARM_DIR, region_of(ch->cx), region_of(ch->cz));
            sm_remove(path);
        }
        ch->stamp = 987654u;
        CHECK(region_write_chunk(FARM_DIR, ch), "could not write the farmed chunk");

        chunk_t back;
        memset(&back, 0, sizeof(back));
        back.id = g_ib;
        back.st = g_sb;
        back.cx = ch->cx;
        back.cz = ch->cz;
        CHECK(region_read_chunk(FARM_DIR, &back, NULL) == 1, "could not read the farmed chunk back");
        CHECK(back.stamp == 987654u, "the chunk came back from the card with clock %u, not 987654", back.stamp);

        // AND A CHUNK FROM A WORLD WRITTEN BEFORE FARMING has no clock
        // at all, which must read as "now" rather than as tick zero --
        // otherwise every field in an upgraded world ripens on sight.
        back.stamp = 0;
        crops_chunk_join(&back, 555000u);
        CHECK(back.stamp == 555000u, "a chunk with no saved clock came back stamped %u, not now", back.stamp);
    }
}

// RICE IS TWO THINGS AT ONCE, and this is the check that it can be.
static void check_rice(void) {
    printf("farming: rice, which is water and a plant\n");
    chunk_store_clear();
    blockupdate_clear();
    CHECK(flat_world(18) != NULL, "the rice world would not become resident");

    // A sandy shallow: sand at 18, water at 19, air above.
    for (int x = 2; x <= 8; x++) {
        for (int z = 2; z <= 8; z++) {
            set_block(x, 17, z, BLK_SAND, 0);
            set_block(x, 18, z, BLK_SAND, 0);
            set_block(x, 19, z, BLK_WATER, ST_PLACED);
        }
    }
    fl_run(200);

    CHECK(crops_plant(5, 19, 5, ITEM_RICE) == PLANT_OK, "rice would not go into one-deep water on sand");
    CHECK(world_block(5, 19, 5) == BLK_RICE_CROP, "planting rice did not put rice in the water cell");

    // Not on dry land, and not in deep water.
    set_block(5, 19, 7, BLK_WATER, ST_PLACED);
    set_block(5, 20, 7, BLK_WATER, ST_PLACED);
    fl_run(50);
    CHECK(crops_plant(5, 19, 7, ITEM_RICE) == PLANT_NEEDS_WATER, "rice went into water two blocks deep");
    set_block(5, 20, 7, BLK_AIR, 0);
    set_block(2, 19, 2, BLK_AIR, 0);
    CHECK(crops_plant(2, 18, 2, ITEM_RICE) == PLANT_NEEDS_WATER, "rice went into dry sand");
    // ... and not on stone under the water, which is the other half of
    // "on sand": a shore, not a quarry.
    set_block(3, 18, 3, BLK_STONE, 0);
    CHECK(crops_plant(3, 19, 3, ITEM_RICE) == PLANT_NEEDS_WATER, "rice went into water over stone");
    set_block(3, 18, 3, BLK_SAND, 0);

    // --- THE POND DOES NOT DRAIN ROUND IT ----------------------------
    //
    // The cell holds rice, not water, so everything that asks "is my
    // neighbour water" has to be told about it -- otherwise a paddy
    // makes the shallows recede, which is the bug this rule exists to
    // prevent (blocks.h, BF2_WATERLOGGED).
    int const wet_before = fl_count();
    fl_run(400);
    printf("  a paddy in a %d-cell pool left %d cells of water\n", wet_before, fl_count());
    CHECK(fl_count() >= wet_before - 1, "planting rice drained %d cells of the pond", wet_before - fl_count());
    CHECK(world_block(5, 19, 5) == BLK_RICE_CROP, "the water washed the rice away");
    CHECK(blockupdate_stats().pending == 0, "a planted paddy left the physics queue busy");

    // --- TWO BLOCKS TALL, AND THEY LIVE AND DIE TOGETHER -------------
    //
    // The user: "Rice should also be a two block tall plant (breaking
    // always breaks both blocks)."
    CHECK(world_block(5, 20, 5) == BLK_RICE_TOP, "planting rice did not put its upper half in the air above it");
    CHECK(!block_waterlogged(BLK_RICE_TOP), "the upper half of the rice claims to be water");
    CHECK(crop_stage(world_state(5, 20, 5)) == crop_stage(world_state(5, 19, 5)),
          "the two halves of the rice were planted at different stages");

    // They grow in step, whichever half the compost lands on.
    crops_advance(5, 20, 5, 1);
    CHECK(crop_stage(world_state(5, 19, 5)) == crop_stage(world_state(5, 20, 5)),
          "composting the top half left the bottom half behind");
    crops_advance(5, 19, 5, 1);
    CHECK(crop_stage(world_state(5, 19, 5)) == crop_stage(world_state(5, 20, 5)),
          "composting the bottom half left the top half behind");

    // Breaking the TOP takes the bottom with it, and the whole plant
    // drops exactly one harvest: the lower half's.
    item_entity_reset();
    interact_break(5, 20, 5, 0);
    CHECK(world_block(5, 19, 5) != BLK_RICE_CROP, "breaking the top of the rice left the bottom standing");
    CHECK(world_block(5, 20, 5) == BLK_AIR, "breaking the top of the rice left something behind");
    int const from_top = item_entity_live();
    CHECK(from_top >= 1, "breaking the top of the rice dropped nothing");
    fl_run(200);
    CHECK(world_block(5, 19, 5) == BLK_WATER, "the pond did not close over a harvested paddy");

    // ... and breaking the BOTTOM takes the top, for the same one
    // harvest. Two plants, two harvests, never one plant and two.
    CHECK(crops_plant(4, 19, 4, ITEM_RICE) == PLANT_OK, "a second paddy would not go in");
    item_entity_reset();
    interact_break(4, 19, 4, 0);
    CHECK(world_block(4, 20, 4) == BLK_AIR, "breaking the bottom of the rice left its top in the air");
    printf("  breaking the top dropped %d stacks, breaking the bottom dropped %d\n", from_top, item_entity_live());
    CHECK(item_entity_live() >= 1, "breaking the bottom of the rice dropped nothing");
    fl_run(200);
}

static void check_composter(void) {
    printf("composting: a day a unit, and worms\n");
    chunk_store_clear();
    CHECK(flat_world(20) != NULL, "the composting world would not become resident");
    blockent_clear();

    // What rots and what does not. The list is the item table's, which
    // is the point of having a column rather than a list in the machine.
    CHECK(composter_accepts(BLK_LEAVES), "leaves do not compost");
    CHECK(composter_accepts(ITEM_WHEAT_SEEDS), "seeds do not compost");
    CHECK(composter_accepts(BLK_FLOWER_RED), "flowers do not compost");
    CHECK(!composter_accepts(BLK_COBBLE), "cobblestone composts");
    CHECK(!composter_accepts(ITEM_COMPOST), "compost composts into itself");

    blockent_t* be = blockent_add(4, 20, 4, BE_COMPOST);
    CHECK(be != NULL, "the pool would not give a composter a record");
    be->stamp             = 0;
    be->slot[BE_COMPOST_INPUT].item  = BLK_LEAVES;
    be->slot[BE_COMPOST_INPUT].count = 4;

    // Most of a day is not a day.
    composter_catch_up(be, COMPOST_TICKS - 1);
    CHECK(be->slot[BE_COMPOST_OUT].count == 0, "the composter paid out before a day was up");
    CHECK(composter_progress_pct(be, COMPOST_TICKS - 1) > 90, "the progress bar is not nearly full after a day less one tick");

    // A day is.
    composter_catch_up(be, COMPOST_TICKS);
    CHECK(be->slot[BE_COMPOST_OUT].item == ITEM_COMPOST && be->slot[BE_COMPOST_OUT].count == 1,
          "a day of leaves made %d compost", be->slot[BE_COMPOST_OUT].count);
    CHECK(be->slot[BE_COMPOST_INPUT].count == 3, "the composter did not eat its scrap");

    // THREE MORE DAYS IN ONE GO, which is the lazy clock doing the thing
    // it exists for -- a box nobody visited for three days is right when
    // they come back.
    composter_catch_up(be, COMPOST_TICKS * 4);
    CHECK(be->slot[BE_COMPOST_OUT].count == 4, "three days away made %d compost, not 4 in total",
          be->slot[BE_COMPOST_OUT].count);
    CHECK(be->slot[BE_COMPOST_INPUT].count == 0, "the input is not empty after four units");

    // AN EMPTY BOX BANKS NOTHING. Otherwise a composter left empty for a
    // week turns its next scrap into compost the instant it goes in.
    uint32_t const t = COMPOST_TICKS * 40;
    composter_catch_up(be, t);
    be->slot[BE_COMPOST_INPUT].item  = BLK_LEAVES;
    be->slot[BE_COMPOST_INPUT].count = 1;
    composter_catch_up(be, t + 1);
    CHECK(be->slot[BE_COMPOST_OUT].count == 4, "an empty composter banked the time it stood idle");
    composter_catch_up(be, t + COMPOST_TICKS + 1);
    CHECK(be->slot[BE_COMPOST_OUT].count == 5, "the box would not start again after standing idle");

    // --- Worms: 0 to 2, and never the same number for ever -----------
    int hist[4] = {0};
    for (uint32_t n = 0; n < 600; n++) {
        int const w = composter_worms_for(4, 20, 4, n);
        CHECK(w >= 0 && w <= COMPOST_WORMS_MAX, "a unit gave %d worms", w);
        hist[w]++;
    }
    printf("  600 units gave %d x 0 worms, %d x 1, %d x 2\n", hist[0], hist[1], hist[2]);
    for (int i = 0; i <= COMPOST_WORMS_MAX; i++) {
        CHECK(hist[i] > 100, "%d worms came up only %d times in 600: the spread is not flat", i, hist[i]);
    }
    // Deterministic, which is what Part T requires of anything random.
    CHECK(composter_worms_for(4, 20, 4, 7) == composter_worms_for(4, 20, 4, 7), "the same unit gave two answers");
    blockent_clear();
}

// THE WILD CROPS, in a real generated world. "Sometimes found" is the
// user's own phrase and they defined it: one or two plants in one
// instance of the biome. So the thing to measure is the RATE -- rare
// enough to be a find, common enough to exist at all.
static void check_wild_crops(void) {
    printf("farming: the crops a player finds growing\n");
    uint32_t const seed = 20260929u;
    int            found[BLK_COUNT];
    memset(found, 0, sizeof(found));

    chunk_t* c = (chunk_t*)malloc(sizeof(chunk_t));
    CHECK(c != NULL, "no room for a scratch chunk");
    if (c == NULL) return;
    memset(c, 0, sizeof(*c));
    c->id = (uint8_t*)malloc(CH_CELLS);
    c->st = (uint8_t*)malloc(CH_CELLS);
    CHECK(c->id != NULL && c->st != NULL, "no room for a scratch chunk's planes");

    int const span = 22;  // 44 x 44 chunks: 700 x 700 blocks
    int       columns = 0;
    for (int32_t cz = -span; cz < span; cz++) {
        for (int32_t cx = -span; cx < span; cx++) {
            c->cx = cx;
            c->cz = cz;
            worldgen_chunk(c, seed, FARLANDS_NONE);
            columns += CH_W * CH_D;
            for (size_t i = 0; i < CH_CELLS; i++) {
                uint8_t const b = c->id[i];
                if (block_crop(b)) found[b]++;
            }
        }
    }
    printf("  in %d columns: %d potato, %d tomato, %d bean, %d rice\n", columns, found[BLK_POTATO_CROP],
           found[BLK_TOMATO_CROP], found[BLK_BEAN_CROP], found[BLK_RICE_CROP]);
    CHECK(found[BLK_POTATO_CROP] > 0, "no wild potato in %d columns: nobody could ever start a farm", columns);
    CHECK(found[BLK_BEAN_CROP] > 0, "no wild beans in %d columns", columns);
    CHECK(found[BLK_TOMATO_CROP] > 0, "no wild tomato in %d columns", columns);
    // Rare, not absent: more than one in a thousand columns is a field,
    // not a find.
    for (int b = 1; b < BLK_COUNT; b++) {
        if (!block_crop(b)) continue;
        CHECK(found[b] * 1000 < columns, "%s grows in more than one column in a thousand", BLOCKS[b].name);
    }
    // And what is found is READY: a plant you have to wait for is a
    // plant you walk past.
    free(c->id);
    free(c->st);
    free(c);
}

int main(void) {
    check_blocks();
    check_ids();
    check_rng();
    check_tags();
    check_chunk_coords();
    check_chunk_state_bits();
    if (!chunk_store_init()) {
        printf("  FAIL: chunk_store_init()\n");
        return 1;
    }
    check_chunk_store();
    chunk_store_shutdown();
    check_worldgen();
    check_biomes();
    check_bench_path();
    check_ores();
    check_farlands();
    check_codec();
    check_region();
    check_region_buckets();
    check_region_damage();
    check_torn_write();
    check_compaction();
    check_sections();
    check_worldstore();
    check_title_world();
    check_title_persists();
    check_palette();
    check_slots();
    check_datadir();
    check_rename();
    check_streaming();
    check_faces();
    check_trace();
    check_world_floor();
    if (!chunk_store_init()) {
        printf("  FAIL: chunk_store_init() for the player checks\n");
        return 1;
    }
    check_physics();
    check_sneak();
    check_raycast();
    check_stacked();
    check_felling();
    check_items();
    check_inv_cursor();
    check_recipes();
    check_blockent();
    check_furnace();
    check_discovery();
    check_crafting();
    check_autocraft();
    check_trash();
    check_bench();
    check_iron();
    check_fold();
    check_light();
    check_fluid();
    check_fluid_worldgen();
    check_farming();
    check_rice();
    check_composter();
    check_wild_crops();
    check_texture_budget();
    check_pens();
    check_shoving();
    check_breeding();
    check_cave_mouths();
    check_population();
    check_swimming();
    check_fishing();
    check_sheep();
    check_bed();
    check_animals();
    check_makers();
    check_saplings();
    check_stove();
    check_hunger();
    check_fall();
    check_replay();
    check_drops();
    check_lang();
    check_label_widths();
    check_text_fits();
    check_midi();
    chunk_store_shutdown();
    if (s_fail) {
        printf("\nworldcheck: %d FAILURE(S)\n", s_fail);
        return 1;
    }
    printf("\nworldcheck: all checks passed\n");
    return 0;
}
