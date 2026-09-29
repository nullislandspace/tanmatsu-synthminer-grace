// =====================================================================
//  SynthMiner  --  breaking and placing (see interact.h)
// =====================================================================

#include "game/interact.h"

#include "common/rng.h"
#include "items/item_entity.h"
#include "world/blockent.h"
#include "world/chunk.h"
#include "world/tree.h"
#include "world/crops.h"
#include "world/fluid.h"

// Which tool the current fell is being done with. A parameter would
// have to thread through the flood fill's whole frontier; the fell is
// one call on one task, so this is simply set around it.
static uint16_t s_fell_tool;

static inline int32_t fl(double v) {
    int32_t const i = (int32_t)v;
    return (v < (double)i) ? i - 1 : i;
}

// A grown tree block: fellable, and not one a player put there.
static bool grown_tree(int32_t x, int32_t y, int32_t z) {
    if (!block_fellable(world_block(x, y, z))) return false;
    return (world_state(x, y, z) & ST_PLACED) == 0;
}

// HOW MANY, between `lo` and `hi` inclusive.
//
// The cell AND THE CLOCK. A hash of the position alone is right for an
// ore vein, which is mined once, and wrong for a field: it would give
// the same plot the same number for ever, so a player who noticed could
// farm the good squares and leave the bad ones (the user, on seeing the
// yields written out: "the yield should be randomized at every
// harvest"). The world's tick count is the thing that moves, and it is
// the RIGHT thing to move by -- a replay reproduces the tick count
// exactly, so this stays deterministic in the only sense Part T asks
// for.
static int roll(int32_t x, int32_t y, int32_t z, uint32_t salt, int lo, int hi) {
    if (hi <= lo) return lo;
    float const r = sm_rand3(x, y, z, salt ^ crops_now());
    int const   n = lo + (int)(r * (float)(hi - lo + 1));
    return n > hi ? hi : n;
}

// Drop what a block yields, on the ground where it stood. `state` is the
// state byte it had BEFORE it was cleared, which for a crop is the
// difference between a harvest and a handful of seeds.
static int drop_for(uint8_t block, uint8_t state, int32_t x, int32_t y, int32_t z, uint16_t tool_item) {
    block_def_t const* d = block_def(block);
    if (!item_can_harvest(block, tool_item)) return 0;

    // A CROP YIELDS BY HOW GROWN IT IS, which is the one drop in the
    // game that is not a straight read of the table. Unripe gives ONE
    // seed back -- pulling up a sprout by mistake costs the time, not
    // the seed -- and ripe gives the harvest plus however many seeds the
    // table says (blocks.h, seed_min / seed_max): wheat 1-2, a tomato
    // none at all, and a potato none because a potato IS its seed.
    if (block_crop(block)) {
        if (!crop_is_ripe(block, state)) {
            return d->seed_item == ITEM_NONE ? 0 : item_entity_spawn(x, y, z, d->seed_item, 1, 0);
        }
        int n = 0;
        if (d->seed_item != ITEM_NONE && d->seed_max > 0) {
            int const seeds = roll(x, y, z, 0x0C40Du, d->seed_min, d->seed_max);
            if (seeds > 0) n += item_entity_spawn(x, y, z, d->seed_item, seeds, 0);
        }
        int const h = roll(x, y, z, 0x0C41Fu, d->drop_min, d->drop_max);
        if (d->drop_item == ITEM_NONE || h <= 0) return n;
        return n + item_entity_spawn(x, y, z, d->drop_item, h, 0);
    }

    if (d->drop_item == ITEM_NONE || d->drop_max == 0) return 0;

    int const n = roll(x, y, z, 0x0D40Fu, d->drop_min, d->drop_max);
    // A drop_min of 0 is a CHANCE, not a promise: tall grass gives up a
    // wheat seed about half the time (blocks.c), and a roll of nothing
    // has to spawn nothing rather than a stack of zero.
    if (n <= 0) return 0;
    return item_entity_spawn(x, y, z, d->drop_item, n, 0);
}

// Spill what a block was holding onto the ground and forget the record.
// Hoisted out of interact_break the day a second cell had to be emptied
// in the same swing: a stove and its chest are one thing to break and
// two records to give back (D-110).
static void empty_record(int32_t x, int32_t y, int32_t z) {
    blockent_t* be = blockent_at(x, y, z);
    if (be == NULL) return;
    for (int i = 0; i < BE_SLOTS; i++) {
        inv_slot_t const* sl = &be->slot[i];
        if (sl->item != 0 && sl->count > 0) item_entity_spawn(x, y, z, sl->item, sl->count, sl->wear);
    }
    blockent_remove(x, y, z);
}

// Is the cell (x, y, z) inside `avoid`? A helper rather than eight
// lines in place, because a stove puts down TWO cells and both of them
// have to miss the player (D-110).
static bool inside_body(phys_body_t const* avoid, int32_t x, int32_t y, int32_t z) {
    if (avoid == NULL) return false;
    double const hw = (double)avoid->w * 0.5;
    double const x0 = avoid->x - hw, x1 = avoid->x + hw;
    double const y0 = avoid->y, y1 = avoid->y + (double)avoid->h;
    double const z0 = avoid->z - hw, z1 = avoid->z + hw;
    return fl(x0) <= x && x <= fl(x1) && fl(y0) <= y && y <= fl(y1 - 1e-9) && fl(z0) <= z && z <= fl(z1);
}

bool interact_stove_other(int32_t x, int32_t y, int32_t z, int32_t* ox, int32_t* oy, int32_t* oz) {
    uint8_t const b = world_block(x, y, z);
    if (b != BLK_STOVE && b != BLK_STOVE_CHEST) return false;
    // EACH HALF'S FACING POINTS AT THE OTHER -- the stove's at its
    // chest, the chest's back at its stove (interact_place_dir writes
    // the opposite into the second cell). So there is no sign to get
    // right here, unlike the bed, where both halves carry the SAME
    // facing and the head has to step backwards along it.
    uint8_t const face = (uint8_t)(st_data(world_state(x, y, z)) & 0x03u);
    if (ox != NULL) *ox = x + face_step_x(face);
    if (oy != NULL) *oy = y;
    if (oz != NULL) *oz = z + face_step_z(face);
    return true;
}

bool interact_bed_other(int32_t x, int32_t y, int32_t z, int32_t* ox, int32_t* oy, int32_t* oz) {
    uint8_t const b = world_block(x, y, z);
    if (b != BLK_BED_FOOT && b != BLK_BED_HEAD) return false;
    uint8_t const face = (uint8_t)(st_data(world_state(x, y, z)) & 0x03u);
    int const     s    = b == BLK_BED_FOOT ? 1 : -1;  // foot -> head, or back
    if (ox != NULL) *ox = x + s * face_step_x(face);
    if (oy != NULL) *oy = y;
    if (oz != NULL) *oz = z + s * face_step_z(face);
    return true;
}

bool interact_toggle_gate(int32_t x, int32_t y, int32_t z) {
    uint8_t const b = world_block(x, y, z);
    if (b != BLK_FENCE_GATE && b != BLK_FENCE_GATE_OPEN) return false;
    // TWO IDS, ONE STATE BYTE: which way it lies is kept, which is what
    // stops a gate turning itself as it opens.
    uint8_t const to = b == BLK_FENCE_GATE ? BLK_FENCE_GATE_OPEN : BLK_FENCE_GATE;
    world_set(x, y, z, to, world_state(x, y, z));
    return true;
}

int interact_fell(int32_t x0, int32_t y0, int32_t z0) {
    // The frontier, as explicit storage rather than recursion: a canopy
    // is hundreds of blocks and this runs on the game task.
    static struct {
        int32_t x, y, z;
    } stack[FELL_MAX];
    int n = 0, taken = 0;

    stack[n].x = x0;
    stack[n].y = y0;
    stack[n].z = z0;
    n++;
    // Take the first one immediately, so it cannot be pushed again.
    uint8_t const first = world_block(x0, y0, z0);
    uint8_t const first_st = world_state(x0, y0, z0);
    world_set(x0, y0, z0, BLK_AIR, 0);
    drop_for(first, first_st, x0, y0, z0, s_fell_tool);
    taken++;

    while (n > 0) {
        n--;
        int32_t const cx = stack[n].x, cy = stack[n].y, cz = stack[n].z;

        // The 26 neighbours, not 6: a canopy is diagonal everywhere, and
        // a 6-neighbour fill leaves half a tree hanging in the air.
        for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    int32_t const nx = cx + dx, ny = cy + dy, nz = cz + dz;

                    // Never below the block that was broken: the stump
                    // stays, and a tree on a cliff edge does not reach
                    // down it.
                    if (ny < y0) continue;
                    if (ny - y0 > FELL_HEIGHT) continue;
                    int32_t const ax = nx - x0 < 0 ? x0 - nx : nx - x0;
                    int32_t const az = nz - z0 < 0 ? z0 - nz : nz - z0;
                    if (ax > FELL_RADIUS || az > FELL_RADIUS) continue;

                    if (!grown_tree(nx, ny, nz)) continue;
                    if (taken >= FELL_MAX || n >= FELL_MAX) return taken;

                    // Clear it as it is pushed, not as it is popped:
                    // that is what stops it being reached twice, and it
                    // is why no "visited" set is needed.
                    uint8_t const was = world_block(nx, ny, nz);
                    uint8_t const was_st = world_state(nx, ny, nz);
                    world_set(nx, ny, nz, BLK_AIR, 0);
                    drop_for(was, was_st, nx, ny, nz, s_fell_tool);
                    taken++;
                    stack[n].x = nx;
                    stack[n].y = ny;
                    stack[n].z = nz;
                    n++;
                }
            }
        }
    }
    return taken;
}

break_result_t interact_break(int32_t x, int32_t y, int32_t z, uint16_t tool_item) {
    break_result_t r = {0};
    uint8_t const  b = world_block(x, y, z);
    r.block          = b;

    if (b == BLK_AIR) return r;
    if (block_def(b)->hardness == HARDNESS_UNBREAKABLE) return r;  // bedrock, and the world's edge
    if (chunk_find(chunk_of(x), chunk_of(z)) == NULL) return r;

    // Some blocks will not break at all without the right tool (the
    // user's rule for iron, 2026-09-23). Say WHICH tool: a swing that
    // does nothing and explains nothing is indistinguishable from a
    // bug, and that is exactly why Minecraft chose the softer rule.
    if (block_tool_required(b) && !item_can_harvest(b, tool_item)) {
        block_def_t const* d = block_def(b);
        r.needs_tool         = item_tool_for(d->tool, d->tool_level);
        return r;
    }

    int const before = item_entity_live();
    bool const placed = (world_state(x, y, z) & ST_PLACED) != 0;
    if (block_trunk(b) && !placed) {
        s_fell_tool = tool_item;
        r.felled    = interact_fell(x, y, z);
        r.was_tree  = true;
        r.ok        = true;

        // ONE OR TWO SEEDLINGS PER TREE (the user, 2026-09-29), which
        // is what makes wood renewable -- there is no recipe anywhere
        // that makes a log, so a cleared forest was cleared for good.
        //
        // PER TREE, not per block. A fell is one swing of the axe and
        // forty blocks, and forty saplings off one oak would make the
        // first tree the last one anybody ever had to plant. Rolled
        // from the world's own hash like the composter's worms, so a
        // replay reproduces a woodpile (Part T), and OF THE SPECIES
        // FELLED -- a birch wood grows back birch.
        uint8_t const sapling = tree_sapling_for(b);
        if (sapling != BLK_AIR) {
            item_entity_spawn(x, y, z, sapling, tree_drops_at(x, y, z), 0);
        }
        r.dropped = item_entity_live() - before;
        return r;
    }

    // A container gives back what is in it BEFORE it stops existing.
    // Breaking a furnace full of iron and getting an empty furnace is
    // the sort of loss a player never forgives and cannot undo.
    empty_record(x, y, z);

    uint8_t const st = world_state(x, y, z);
    world_set(x, y, z, BLK_AIR, 0);
    drop_for(b, st, x, y, z, tool_item);
    r.felled = 1;

    // THE OTHER HALF OF A TWO-BLOCK PLANT goes with this one, whichever
    // half was hit -- the user's rule for rice: "breaking always breaks
    // both blocks". It drops nothing of its own; the harvest belongs to
    // the lower half and has already been taken above (blocks.h,
    // tall_other / BF2_TALL_TOP).
    {
        uint8_t const other = block_tall_other(b);
        if (other != BLK_AIR) {
            int32_t const oy = block_tall_top(b) ? y - 1 : y + 1;
            if (oy >= 0 && oy < CH_H && world_block(x, oy, z) == other) {
                uint8_t const ost = world_state(x, oy, z);
                world_set(x, oy, z, BLK_AIR, 0);
                drop_for(other, ost, x, oy, z, tool_item);
                r.felled++;
            }
        }
    }

    // AND THE OTHER HALF OF A BED, which is the same rule lying down:
    // two cells side by side rather than one above the other, so it
    // finds its partner through the facing both halves carry rather
    // than through `tall_other`. Only the FOOT pays out, like the rice.
    //
    // Worked out from the id and state SAVED ABOVE, because the cell
    // itself is already air by now.
    if (b == BLK_BED_FOOT || b == BLK_BED_HEAD) {
        uint8_t const face = (uint8_t)(st_data(st) & 0x03u);
        int const     s    = b == BLK_BED_FOOT ? 1 : -1;
        int32_t const bx = x + s * face_step_x(face), bz = z + s * face_step_z(face);
        uint8_t const other = world_block(bx, y, bz);
        // A half whose partner is missing simply finds something else
        // there, which is what makes this safe at a chunk border.
        if (other == BLK_BED_FOOT || other == BLK_BED_HEAD) {
            uint8_t const ost = world_state(bx, y, bz);
            world_set(bx, y, bz, BLK_AIR, 0);
            if (other == BLK_BED_FOOT) drop_for(other, ost, bx, y, bz, tool_item);
            r.felled++;
        }
    }

    // AND THE OTHER HALF OF A STOVE, which is the bed's rule again with
    // one difference that matters: both halves hold THINGS. The chest
    // has twenty-seven slots of a farm's produce in it and the stove
    // has fuel and a finished dish, so both are emptied onto the ground
    // -- and only the STOVE half pays out the item, because the recipe
    // made one item out of two blocks (D-110).
    //
    // That asymmetry is also what makes this safe at a chunk border. If
    // the partner is not resident, `other` is the barrier or something
    // else entirely and nothing happens to it: breaking the stove
    // leaves an orphan chest, breaking the chest leaves an orphan stove
    // that says "no chest" when it is opened. Neither can duplicate the
    // item, because only one of the two ever drops it.
    if (b == BLK_STOVE || b == BLK_STOVE_CHEST) {
        uint8_t const face = (uint8_t)(st_data(st) & 0x03u);
        int32_t const sx = x + face_step_x(face), sz = z + face_step_z(face);
        uint8_t const other = world_block(sx, y, sz);
        if ((b == BLK_STOVE && other == BLK_STOVE_CHEST) || (b == BLK_STOVE_CHEST && other == BLK_STOVE)) {
            uint8_t const ost = world_state(sx, y, sz);
            empty_record(sx, y, sz);
            world_set(sx, y, sz, BLK_AIR, 0);
            if (other == BLK_STOVE) drop_for(other, ost, sx, y, sz, tool_item);
            r.felled++;
        }
    }

    // A CROP CANNOT STAND ON NOTHING. Dig the soil out from under a
    // field and the field comes with it, harvested as it stood -- ripe
    // wheat yields wheat, a sprout yields its seed back (drop_for).
    //
    // This is not the BF2_STACKED rule below it and must not be folded
    // into it: that one takes a column of the SAME block, which is a
    // cactus growing out of itself, and this one takes a DIFFERENT block
    // resting on the one that has gone. One cell up, and if what stood
    // there was half of a two-block plant the recursion takes its other
    // half with it.
    if (y + 1 < CH_H) {
        uint8_t const above = world_block(x, y + 1, z);
        if (block_crop(above)) {
            uint8_t const ast = world_state(x, y + 1, z);
            world_set(x, y + 1, z, BLK_AIR, 0);
            drop_for(above, ast, x, y + 1, z, tool_item);
            r.felled++;
            uint8_t const other = block_tall_other(above);
            if (!block_tall_top(above) && other != BLK_AIR && y + 2 < CH_H && world_block(x, y + 2, z) == other) {
                world_set(x, y + 2, z, BLK_AIR, 0);
                r.felled++;
            }
        }
    }

    // A STACK COMES DOWN WITH THE BLOCK IT STOOD ON (BF2_STACKED). The
    // user, 2026-09-28: "When mining a cactus block, the cactus blocks
    // above should also drop, like we are cutting down the cactus at
    // this level."
    //
    // Straight up, and only the same block: two cacti growing side by
    // side are two plants, and a cactus with something resting on top
    // does not bring that down. Nothing about ST_PLACED here -- a
    // cactus a player planted is held up by the same nothing as one
    // that grew.
    if (block_stacked(b)) {
        for (int32_t up = y + 1; up < CH_H && world_block(x, up, z) == b; up++) {
            uint8_t const ust = world_state(x, up, z);
            world_set(x, up, z, BLK_AIR, 0);
            drop_for(b, ust, x, up, z, tool_item);
            r.felled++;
        }
    }

    r.ok      = true;
    r.dropped = item_entity_live() - before;
    return r;
}

bool interact_place(ray_hit_t const* hit, uint8_t block, phys_body_t const* avoid) {
    // Facing north, which is what everything that does not care gets.
    return interact_place_dir(hit, block, avoid, 0.0f, 1.0f);
}

bool interact_place_dir(ray_hit_t const* hit, uint8_t block, phys_body_t const* avoid, float dx, float dz) {
    if (hit == NULL || block == BLK_AIR || block >= BLK_COUNT) return false;
    // A hit with no face is the player standing inside a block: there
    // is no "in front of" to place into.
    if (hit->face == 0xFFu) return false;

    // Aimed at something a placement overwrites -- tall grass -- the
    // block goes INTO that cell, not in front of it: that is what the
    // highlighted box promised.
    bool const    into = block_replaceable(hit->block);
    int32_t const x = into ? hit->x : hit->px, y = into ? hit->y : hit->py, z = into ? hit->z : hit->pz;
    if (y < 0 || y >= CH_H) return false;
    if (chunk_find(chunk_of(x), chunk_of(z)) == NULL) return false;
    if (!block_replaceable(world_block(x, y, z))) return false;

    // Not inside the player. Only matters for solid blocks -- a torch
    // or a flower may share the cell.
    if (block_solid(block) && inside_body(avoid, x, y, z)) return false;

    // --- Which way up does it go, and will it stay there? ------------
    //
    // A torch is the first block whose SHAPE depends on how it was
    // placed: upright on the floor, or against whichever wall the
    // player pointed at (voxel_mesh.h, TORCH_*). It also needs
    // something to hold it, which is the first placement this game has
    // ever refused for a reason other than the cell being full.
    uint8_t state = ST_PLACED;
    if (block_kind(block) == K_TORCH) {
        uint8_t how;
        switch (hit->face) {
            case MESH_DIR_PY: how = TORCH_FLOOR; break;
            case MESH_DIR_PX: how = TORCH_WALL_NX; break;
            case MESH_DIR_NX: how = TORCH_WALL_PX; break;
            case MESH_DIR_PZ: how = TORCH_WALL_NZ; break;
            case MESH_DIR_NZ: how = TORCH_WALL_PZ; break;
            // The underside of a block. Nothing here hangs, so rather
            // than drop a torch on the floor somewhere the player did
            // not point at, this does nothing.
            default: return false;
        }
        // Whatever it leans on has to be there. Aiming at the side of a
        // flower and getting a torch floating in the air is worse than
        // the click doing nothing.
        int32_t const sx = how == TORCH_WALL_NX ? x - 1 : how == TORCH_WALL_PX ? x + 1 : x;
        int32_t const sz = how == TORCH_WALL_NZ ? z - 1 : how == TORCH_WALL_PZ ? z + 1 : z;
        int32_t const sy = how == TORCH_FLOOR ? y - 1 : y;
        if (!block_solid(world_block(sx, sy, sz))) return false;
        state = (uint8_t)(ST_PLACED | (uint8_t)(how << ST_DATA_SHIFT));
    }

    // A GATE LIES ACROSS THE WAY YOU ARE FACING, so that putting one
    // down in a fence line and walking on works without thinking about
    // it. The torch above takes its direction from the FACE that was
    // aimed at; a gate cannot, because most of them are placed on the
    // ground, where the face says only "upwards".
    if (block_kind(block) == K_GATE) {
        float const ax = dx < 0.0f ? -dx : dx, az = dz < 0.0f ? -dz : dz;
        uint8_t const axis = ax > az ? GATE_AXIS_Z : GATE_AXIS_X;
        state              = (uint8_t)(ST_PLACED | (uint8_t)(axis << ST_DATA_SHIFT));
    }

    // A BED IS TWO CELLS and goes down as one: the foot where the
    // player pointed and the head one step further along the way they
    // are facing. If that second cell is not free, or has nothing to
    // stand on, NOTHING is placed -- half a bed is not a thing.
    if (block == BLK_BED_FOOT) {
        uint8_t const face = face_from_dir(dx, dz);
        int32_t const hx = x + face_step_x(face), hz = z + face_step_z(face);
        if (chunk_find(chunk_of(hx), chunk_of(hz)) == NULL) return false;
        if (!block_replaceable(world_block(hx, y, hz))) return false;
        if (!block_solid(world_block(hx, y - 1, hz))) return false;
        state = (uint8_t)(ST_PLACED | (uint8_t)(face << ST_DATA_SHIFT));
        world_set(x, y, z, BLK_BED_FOOT, state);
        world_set(hx, y, hz, BLK_BED_HEAD, state);
        return true;
    }

    // A STOVE IS TWO CELLS TOO, and its second one is a CHEST (D-110).
    // The user's refinement kills the "which chest?" question by making
    // it unaskable: the recipe includes the chest's eight planks, and
    // the item puts both down at once.
    //
    // ON THE LEFT AS THE PLAYER SEES IT while placing -- their left,
    // not the world's -- so that a kitchen built by walking along a
    // wall comes out as `chest stove chest stove` with every pair the
    // right way round. Each half's facing names the other, so a row of
    // them needs no untangling (blocks.h, FACE_*).
    //
    // ALL OR NOTHING. If the second cell is taken, or the player is
    // standing in it, or the record pool is full, NOTHING is placed:
    // half a kitchen is a stove that can never cook.
    if (block == BLK_STOVE) {
        uint8_t const face = face_left_of(dx, dz);
        int32_t const cx = x + face_step_x(face), cz = z + face_step_z(face);
        if (chunk_find(chunk_of(cx), chunk_of(cz)) == NULL) return false;
        if (!block_replaceable(world_block(cx, y, cz))) return false;
        if (inside_body(avoid, cx, y, cz)) return false;
        if (blockent_add(x, y, z, BE_STOVE) == NULL) return false;
        if (blockent_add(cx, y, cz, BE_CHEST) == NULL) {
            blockent_remove(x, y, z);  // the stove's record goes back, or the pool leaks
            return false;
        }
        world_set(x, y, z, BLK_STOVE, (uint8_t)(ST_PLACED | (uint8_t)(face << ST_DATA_SHIFT)));
        world_set(cx, y, cz, BLK_STOVE_CHEST,
                  (uint8_t)(ST_PLACED | (uint8_t)(face_opposite(face) << ST_DATA_SHIFT)));
        return true;
    }

    // A block that remembers things needs somewhere to remember them,
    // and if the pool is full it must not be placed at all: a furnace
    // with no record behind it would look like a furnace and behave
    // like a wall.
    if (block_keeps_record(block) && blockent_add(x, y, z, block_record_kind(block)) == NULL) return false;

    world_set(x, y, z, block, state);
    return true;
}

// --- Using what is in your hand (see interact.h) -----------------------

use_result_t interact_use_item(double ex, double ey, double ez, float dx, float dy, float dz, uint16_t item) {
    use_result_t r = {0};
    r.sound        = SND_NONE;

    item_def_t const held    = item_def(item);
    bool const       is_hoe  = held.tool == TOOL_HOE;
    bool const       is_seed = crops_block_for_seed(item) != BLK_AIR;
    uint8_t const    carried = item_bucket_contents(item);
    bool const       is_bucket = item == ITEM_BUCKET || carried != BLK_AIR;
    if (!is_bucket && !is_hoe && !is_seed && item != ITEM_COMPOST) return r;  // nothing to use

    // THE SAME RAY FOR ALL OF THEM, in RAY_FLUID mode. The bucket needs
    // it because the crosshair looks straight through water on purpose
    // (raycast.h); rice needs it for exactly the same reason, since the
    // cell it is planted in IS water.
    ray_hit_t h;
    if (!ray_pick(ex, ey, ez, dx, dy, dz, RAY_REACH, RAY_FLUID, &h)) return r;

    // --- A hoe: tilling ----------------------------------------------
    if (is_hoe) {
        // The face matters. Tilling the SIDE of a block would put
        // farmland where the player cannot see they pointed, so only the
        // top of a block turns over.
        if (h.face != MESH_DIR_PY) {
            r.msg = USE_CANNOT_TILL;
            return r;
        }
        if (!crops_till(h.x, h.y, h.z)) {
            r.msg = USE_CANNOT_TILL;
            return r;
        }
        r.acted = true;
        r.wear  = true;
        r.sound = block_sound(world_block(h.x, h.y, h.z));
        r.block = world_block(h.x, h.y, h.z);
        r.x = h.x, r.y = h.y, r.z = h.z;
        return r;
    }

    // --- A seed: planting --------------------------------------------
    if (is_seed) {
        switch (crops_plant(h.x, h.y, h.z, item)) {
            case PLANT_OK: break;
            case PLANT_TOO_DRY: r.msg = USE_TOO_DRY; return r;
            case PLANT_NEEDS_WATER: r.msg = USE_NEEDS_WATER; return r;
            case PLANT_NEEDS_GROUND: r.msg = USE_NEEDS_GROUND; return r;
            case PLANT_BLOCKED:
            case PLANT_NEEDS_SOIL:
            case PLANT_NOT_SEED:
            default: r.msg = USE_NEEDS_SOIL; return r;
        }
        uint8_t const crop = crops_block_for_seed(item);
        r.acted   = true;
        r.consume = true;
        r.sound   = SND_SOFT;
        r.block   = crop;
        // Rice goes in the cell that was struck; everything else on top
        // of it. crops_plant knows which, so ask the world rather than
        // working it out twice.
        r.x = h.x, r.y = block_waterlogged(crop) ? h.y : h.y + 1, r.z = h.z;
        return r;
    }

    // --- Compost: one stage at once, or a bed of flowers -------------
    if (item == ITEM_COMPOST) {
        uint8_t const at = world_block(h.x, h.y, h.z);

        // ON BARE GRASS IT RAISES A 3 x 3 OF YELLOW FLOWERS (the user,
        // 2026-09-29: "throwing compost onto the ground should grow a
        // 3x3 grid of yellow flowers").
        //
        // Which makes the second non-renewable thing renewable. A
        // yellow flower is what a pork sausage needs for its spice, and
        // until now the only ones in the world were the ones the
        // generator happened to scatter -- pick the meadow and the
        // sausage maker stops.
        //
        // WHAT IT CAN REACH is every cell of the nine whose ground is
        // grass and whose air is free; the rest are skipped rather than
        // refusing the whole thing, so a patch at the edge of a pond
        // does what it can. It only fails, and says so, when it could
        // do nothing at all.
        if (at == BLK_GRASS && h.face == MESH_DIR_PY) {
            int grown = 0;
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int32_t const fx = h.x + dx, fz = h.z + dz;
                    if (chunk_find(chunk_of(fx), chunk_of(fz)) == NULL) continue;
                    if (world_block(fx, h.y, fz) != BLK_GRASS) continue;
                    if (h.y + 1 >= CH_H) continue;
                    uint8_t const above = world_block(fx, h.y + 1, fz);
                    // Air, or the undergrowth a placement would overwrite
                    // anyway -- but never a flower that is already there,
                    // which would be a use that looked like it did nothing.
                    if (above != BLK_AIR && !block_replaceable(above)) continue;
                    if (above == BLK_FLOWER_YELLOW) continue;
                    world_set(fx, h.y + 1, fz, BLK_FLOWER_YELLOW, 0);
                    grown++;
                }
            }
            if (grown == 0) {
                r.msg = USE_ALREADY_RIPE;  // nine cells and nothing to do in any of them
                return r;
            }
            r.acted   = true;
            r.consume = true;
            r.sound   = SND_SOFT;
            r.block   = BLK_FLOWER_YELLOW;
            r.x = h.x, r.y = h.y + 1, r.z = h.z;
            return r;
        }

        if (!block_crop(at)) {
            r.msg = USE_NEEDS_SOIL;
            return r;
        }
        if (crops_advance(h.x, h.y, h.z, 1) == 0) {
            r.msg = USE_ALREADY_RIPE;
            return r;
        }
        r.acted   = true;
        r.consume = true;
        r.sound   = SND_SOFT;
        r.block   = at;
        r.x = h.x, r.y = h.y, r.z = h.z;
        return r;
    }

    if (carried != BLK_AIR) {
        // POURING IT OUT. Into the struck cell if that cell is
        // something water may occupy -- air, tall grass, a film of
        // flowing water -- and otherwise against its face, which is
        // the ordinary placement cell the ray already worked out.
        //
        // A source is the exception: emptying a bucket into water that
        // is already a spring would swallow the bucketful and change
        // nothing, so that case takes the face instead and the water
        // lands where the player was clearly pointing.
        bool const onto_source = h.block == carried && fluid_is_source(world_state(h.x, h.y, h.z));
        bool const into        = block_replaceable(h.block) && !onto_source;
        int32_t const tx = into ? h.x : h.px, ty = into ? h.y : h.py, tz = into ? h.z : h.pz;
        if (!fluid_place_source(tx, ty, tz, carried)) return r;
        r.acted   = true;
        r.becomes = ITEM_BUCKET;
        r.sound   = block_sound(carried);
        r.block   = carried;
        r.x = tx, r.y = ty, r.z = tz;
        return r;
    }

    // FILLING IT. Only from a source -- scooping a flow would let a
    // player carry a pond away one film at a time, and Minecraft says
    // no for the same reason. A flow struck by the ray is simply not a
    // thing the bucket can take, and the tap does nothing.
    uint8_t const got = fluid_take_source(h.x, h.y, h.z);
    if (got == BLK_AIR) return r;
    uint16_t const full = item_bucket_filled_with(got);
    if (full == 0) {
        // A fluid no bucket carries. Put it back rather than destroy
        // it: taking the source was the first half of a trade that
        // cannot be completed.
        fluid_place_source(h.x, h.y, h.z, got);
        return r;
    }
    r.acted   = true;
    r.becomes = full;
    r.sound   = block_sound(got);
    r.block   = got;
    r.x = h.x, r.y = h.y, r.z = h.z;
    return r;
}
