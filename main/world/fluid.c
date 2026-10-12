// =====================================================================
//  SynthMiner  --  fluids (see fluid.h)
// =====================================================================

#include "world/fluid.h"

#include "world/blocks.h"

// --- The registry -----------------------------------------------------
//
// Two numbers do all the tuning. `delay` is how sluggish it looks;
// `reach` is how far a source runs before it thins out to nothing, and
// it is also the cap that makes a cut-off flow DRY UP rather than
// hang there for ever (a cell wanting a level past the reach becomes
// air). Water's are Minecraft's.
// LAVA IS IN THIS TABLE (the user, 2026-10-12: "Lava needs to flow like
// water"). It is K_LIQUID so the mesher draws only its surface, and a row
// here is what makes it MOVE: breach a lava lake with a pickaxe and the
// lake runs out of the hole like water does, at a third of water's speed
// and for three cells before it thins to nothing.
//
// `infinite` is FALSE, which is the one place lava is not water: two
// neighbours of a lava flow never become a new source, so a lava lake can
// be drained and stays drained. Minecraft's rule for the same reason --
// an inexhaustible lava source is a furnace you cannot turn off.
//
// WHY THIS DOES NOT FILL THE CAVE: the Deep Mine's lava is a LEVEL
// (DM_LAVA_LEVEL, deepmine.c), so every lake is a basin whose walls are
// columns whose floor is above the level. A source standing on its own
// fluid does not spread (fluid_unsettled, and step 2 of the same rule),
// and a same-fluid neighbour only accepts something strictly fuller, so a
// generated lake is settled the moment it is generated and puts nothing
// in the queue. Only a player-made breach flows.
static fluid_def_t const FLUIDS[] = {
    {.block = BLK_WATER, .delay = 5, .reach = 7, .infinite = true},
    {.block = BLK_LAVA, .delay = 20, .reach = 3, .infinite = false},
    {.block = BLK_AIR},  // the end
};

fluid_def_t const* fluid_def(uint8_t block) {
    if (block == BLK_AIR) return NULL;
    for (int i = 0; FLUIDS[i].block != BLK_AIR; i++) {
        if (FLUIDS[i].block == block) return &FLUIDS[i];
    }
    return NULL;
}

int fluid_delay(uint8_t block) {
    fluid_def_t const* d = fluid_def(block);
    return d != NULL ? (int)d->delay : 0;
}

// --- Neighbours -------------------------------------------------------

static int const DX[4] = {1, -1, 0, 0};
static int const DZ[4] = {0, 0, 1, -1};

// All six, for the one question that is not about where a fluid is going
// but about what it is standing next to.
static int const NX[6] = {1, -1, 0, 0, 0, 0};
static int const NY[6] = {0, 0, 1, -1, 0, 0};
static int const NZ[6] = {0, 0, 0, 0, 1, -1};

// The other fluid. There are two, so this is the whole of "what does this
// one react with".
static uint8_t fluid_other(uint8_t fluid) {
    if (fluid == BLK_WATER) return BLK_LAVA;
    if (fluid == BLK_LAVA) return BLK_WATER;
    return BLK_AIR;
}

// WILL THIS CELL TAKE `fluid` AT `lvl`? The one test every write goes
// through, and it is deliberately strict: a cell already holding the
// same fluid is only overwritten by something STRICTLY FULLER.
//
// Anything weaker oscillates. Two cells of equal level that each think
// they should rewrite the other keep waking each other for ever, at a
// cost of two world_set calls a tick each, and it would look exactly
// like water that is working.
static bool accepts(int32_t x, int32_t y, int32_t z, uint8_t fluid, int lvl) {
    if (y < 0 || y >= CH_H) return false;
    uint8_t const nb = world_block(x, y, z);
    if (nb == fluid) return (int)fluid_level(world_state(x, y, z)) > lvl;
    // AND THE OTHER FLUID IS NOT A CELL EITHER OF THEM MAY TAKE. Both are
    // BF_REPLACEABLE in the registry, but that flag is about a pickaxe and a
    // bucket -- about a player being able to fill a pool in -- not about two
    // fluids meeting. Read through that flag, a lava flow poured water out
    // of its way and a water flow poured lava out of its way, and every
    // shoreline between them was a two-cell oscillator: each tick one of
    // them deleted the other and woke itself back up again. That is the
    // "lava/water interaction is buggy" of 2026-10-12 (F-157). What happens
    // at a meeting instead is fluid_quench(), below.
    if (nb == fluid_other(fluid)) return false;
    // BLK_BARRIER -- a chunk that is not resident -- is not replaceable,
    // so this is also what stops a flow at the edge of the loaded world
    // (blockupdate.h, the boundary note). It is not a special case; it
    // is the wall D-14 already put there.
    return block_replaceable(nb);
}

// Is this fluid cell standing next to the other one? Six reads, and the
// only place that pays for them is a SOURCE that everything else has
// already called settled (fluid_unsettled).
static bool fluid_touches_other(int32_t x, int32_t y, int32_t z, uint8_t fluid) {
    uint8_t const other = fluid_other(fluid);
    if (other == BLK_AIR) return false;
    for (int i = 0; i < 6; i++)
        if (world_block(x + NX[i], y + NY[i], z + NZ[i]) == other) return true;
    return false;
}

// WATER MEETS LAVA, AND THE LAVA BECOMES STONE (the user, 2026-10-12: "a
// block where water contacts lava should turn into stone").
//
// THE LAVA IS THE ONE THAT SOLIDIFIES, in both directions, which is what
// makes the rule one rule: a water cell that touches lava turns that lava
// to stone, and a lava cell that touches water turns ITSELF to stone. So
// the meeting gives the same answer whichever of the two was woken first,
// whichever of them moved, and whether the contact was made by a flow, by
// a bucket, or by two halves of the world arriving in either order.
//
// STONE, not cobblestone and not obsidian. `BLK_STONE` drops cobblestone
// (blocks.c), so the item that ends up in the hand is the one Minecraft
// gives for a flow, and the Deep Mine's ground is stone, so the wall a
// player builds out of a lake matches the cave it was cut from. Minecraft
// makes a SOURCE into obsidian; that distinction is not built here, and
// neither is a reason to make a lake unkillable with one bucket.
//
// It cannot run away from itself: stone is not replaceable, so a quenched
// cell is not in contact any more, and the work is bounded by the number
// of lava cells in the world.
//
// Returns true when THIS cell became stone, which is the caller's cue to
// stop: a cell that is no longer a fluid has no business spreading.
static bool fluid_quench(int32_t x, int32_t y, int32_t z, uint8_t fluid) {
    uint8_t const other = fluid_other(fluid);
    if (other == BLK_AIR) return false;

    if (fluid == BLK_LAVA) {
        for (int i = 0; i < 6; i++) {
            if (world_block(x + NX[i], y + NY[i], z + NZ[i]) != other) continue;
            world_set(x, y, z, BLK_STONE, 0);
            return true;
        }
        return false;
    }

    for (int i = 0; i < 6; i++) {
        int32_t const nx = x + NX[i], ny = y + NY[i], nz = z + NZ[i];
        if (world_block(nx, ny, nz) == other) world_set(nx, ny, nz, BLK_STONE, 0);
    }
    return false;
}

// --- The step ---------------------------------------------------------

void fluid_update(int32_t x, int32_t y, int32_t z) {
    uint8_t const      b  = world_block(x, y, z);
    fluid_def_t const* fd = fluid_def(b);
    if (fd == NULL) return;  // the usual case: a cell woken by a neighbour

    // 0. FIRST MEETING, THEN MOVING. A cell that has just touched the other
    //    fluid is not a cell with somewhere to flow: the contact happens
    //    before the spread, so water arriving at the edge of a lava lake
    //    walls the lake rather than pouring through it, and the stone it
    //    made is what it then flows over.
    if (fluid_quench(x, y, z, b)) return;

    uint8_t st      = world_state(x, y, z);
    int     lvl     = (int)fluid_level(st);
    bool    falling = fluid_is_falling(st);

    // 1. WHAT SHOULD I BE? A source is the answer to its own question
    //    and skips all of this; everything else is only as full as what
    //    is feeding it, which is what makes water recede.
    if (!fluid_is_source(st)) {
        int  want_lvl;
        bool want_fall;
        // A NEIGHBOUR IN A CHUNK THAT IS NOT HERE IS AN UNKNOWN, NOT A
        // NO. It reads as BLK_BARRIER, which is the right answer for
        // "can I flow that way" and the wrong one for "is anything
        // feeding me" -- and acting on it would quietly delete the edge
        // of every pond whenever its other half was streamed out. So a
        // cell with a missing neighbour is allowed to thin, never to
        // dry up: it waits for the chunk to come back.
        bool unknown = false;

        if (world_block(x, y + 1, z) == b) {
            // Fed from directly above: full, and passing through.
            want_lvl  = 0;
            want_fall = true;
        } else {
            int best = FLUID_LEVEL_MASK + 1;  // 8: nothing is feeding me
            int srcs = 0;
            for (int i = 0; i < 4; i++) {
                int32_t const nx = x + DX[i], nz = z + DZ[i];
                uint8_t const nid = world_block(nx, y, nz);
                if (nid == BLK_BARRIER) unknown = true;
                // A WATERLOGGED CELL IS A FULL SOURCE OF WATER -- rice,
                // which shares its cell with the pond it grows in
                // (blocks.h, BF2_WATERLOGGED). Without this the water
                // around a paddy would read the rice as dry ground and
                // recede from it, which is the shallows draining
                // themselves because somebody planted something.
                //
                // Water specifically: a waterlogged block holds water
                // and there is nothing else it could hold. A second
                // fluid that wanted this would want a column in the
                // block table saying which.
                if (b == BLK_WATER && block_waterlogged(nid)) {
                    best = 0;
                    srcs++;
                    continue;
                }
                if (nid != b) continue;
                uint8_t const nst = world_state(nx, y, nz);
                // A FALLING NEIGHBOUR FEEDS AT FULL STRENGTH. It has to:
                // the cell where a waterfall lands is falling, and the
                // ring of water spreading out from it has nothing else
                // holding it up. Minecraft does the same thing, and
                // treating falling as "not a feed" dries the foot of
                // every waterfall one tick after it forms.
                int const nl = fluid_is_full(nst) ? 0 : (int)fluid_level(nst);
                if (nl < best) best = nl;
                if (fluid_is_source(nst)) srcs++;
            }

            // TWO SOURCES MAKE A THIRD -- the 2x2 hole and a bucket in
            // two corners. Not in mid-air: the cell below has to be
            // something that would hold water.
            if (fd->infinite && srcs >= 2 && !accepts(x, y - 1, z, b, 0)) {
                want_lvl  = 0;
                want_fall = false;
            } else {
                want_lvl  = best + 1;  // 9 when nothing feeds it: past any reach
                want_fall = false;
            }
        }

        if (want_lvl > (int)fd->reach) {
            if (unknown) return;             // the feed may be in a chunk that is not here
            world_set(x, y, z, BLK_AIR, 0);  // cut off: it dries up
            return;
        }
        if (want_lvl != lvl || want_fall != falling) {
            st = fluid_state(st, (uint8_t)want_lvl, want_fall);
            world_set(x, y, z, b, st);
            lvl     = want_lvl;
            falling = want_fall;
        }
    }

    // 2. DOWN FIRST, AND NOTHING ELSE IF IT CAN. One rule, and every
    //    shape a fluid makes comes out of it:
    //
    //      poured in mid-air  a one-block column, because every cell
    //                         of it has somewhere to fall
    //      poured on ground   a sheet, because none of them has
    //      poured on a ledge  a sheet that stops at the lip and falls
    //                         off it, because the lip cell does
    //
    // The second half is the one that is easy to get wrong. A cell
    // whose neighbour below is already FULL of the same fluid cannot
    // fall -- but that is a fall in progress, not a floor, and letting
    // it spread instead turns every waterfall into a cone. Getting this
    // backwards flooded a test world's entire airspace, 13719 cells,
    // from one source placed in the air.
    uint8_t const below = world_block(x, y - 1, z);
    if (accepts(x, y - 1, z, b, 0)) {
        world_set(x, y - 1, z, b, fluid_state(0, 0, true));
        return;
    }
    // Replaceable and yet it would not take any: it is full of us.
    if (block_replaceable(below)) return;

    // 3. OUTWARDS, one level thinner -- or at full strength minus one
    //    where a fall has just landed, which is what spreads a pool out
    //    from the foot of a waterfall.
    int const out = falling ? 1 : lvl + 1;
    if (out > (int)fd->reach) return;
    for (int i = 0; i < 4; i++) {
        int32_t const nx = x + DX[i], nz = z + DZ[i];
        if (accepts(nx, y, nz, b, out)) world_set(nx, y, nz, b, fluid_state(0, (uint8_t)out, false));
    }
}

// --- Is there anything left to do here? -------------------------------

bool fluid_unsettled(int32_t x, int32_t y, int32_t z) {
    uint8_t const      b  = world_block(x, y, z);
    fluid_def_t const* fd = fluid_def(b);
    if (fd == NULL) return false;

    uint8_t const st = world_state(x, y, z);

    // ANYTHING THAT IS NOT A SOURCE IS UNSETTLED, and that is not
    // laziness -- a flow's level depends on a source that may no longer
    // be there, so the only way to know is to run the step. It is also
    // cheap where it counts: an ocean is sources all the way down, so
    // the case this rule is generous with is the rare one.
    if (!fluid_is_source(st)) return true;

    // CONTACT IS WORK, and it has to be asked before anything below decides
    // this cell is settled. A source standing on its own fluid with lava to
    // one side reads as done by every test under this one -- it has nowhere
    // to fall and nowhere to spread -- and the meeting still has to happen.
    // It is also the only way a world that was GENERATED with the two
    // touching reacts at all, and the way it reacts the same way whichever
    // half of it arrived first.
    if (fluid_touches_other(x, y, z, b)) return true;

    if (accepts(x, y - 1, z, b, 0)) return true;
    // Standing on its own fluid: it will not spread, so there is
    // nothing here to do. This is the line that keeps an ocean chunk
    // out of the queue entirely (see fluid_update, step 2).
    if (block_replaceable(world_block(x, y - 1, z))) return false;
    int const out = fluid_is_falling(st) ? 1 : (int)fluid_level(st) + 1;
    if (out > (int)fd->reach) return false;
    for (int i = 0; i < 4; i++) {
        if (accepts(x + DX[i], y, z + DZ[i], b, out)) return true;
    }
    return false;
}

// --- The bucket -------------------------------------------------------

bool fluid_place_source(int32_t x, int32_t y, int32_t z, uint8_t block) {
    if (y < 0 || y >= CH_H) return false;
    if (fluid_def(block) == NULL) return false;
    uint8_t const b = world_block(x, y, z);
    if (!block_replaceable(b)) return false;
    // ST_PLACED, because it was: the bit means what it has always meant
    // and a spring somebody poured is not a spring the world grew.
    world_set(x, y, z, block, fluid_state(ST_PLACED, 0, false));
    return true;
}

uint8_t fluid_take_source(int32_t x, int32_t y, int32_t z) {
    uint8_t const b = world_block(x, y, z);
    if (fluid_def(b) == NULL) return BLK_AIR;
    if (!fluid_is_source(world_state(x, y, z))) return BLK_AIR;
    world_set(x, y, z, BLK_AIR, 0);
    return b;
}
