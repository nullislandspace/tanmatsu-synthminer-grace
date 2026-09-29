#pragma once
// =====================================================================
//  SynthMiner  --  breaking and placing
// ---------------------------------------------------------------------
//  THE LOGGING RULE (claudeplans/synthminer.md, Part F) lives here, and
//  it is SynthMiner's one deliberate departure from Minecraft:
//
//    break a TRUNK the world GREW                  -> the tree falls
//    break a trunk the PLAYER placed               -> that block drops
//    break a LEAF, however it got there            -> that leaf drops
//
//  Only a trunk starts a fell. Leaves are tree material -- the fell
//  SPREADS through them, so a canopy still comes down with its trunk --
//  but cutting one is an ordinary break. Until 2026-09-28 both tests
//  were the same flag, so clearing a canopy by hand felled the tree out
//  from under it (BF2_TRUNK, blocks.h).
//
//  Which is which is the ST_PLACED bit, set on everything a player puts
//  down and never by generation (chunk.h). So a forest can be cleared
//  without twenty minutes of jumping at trunks, and a player's own
//  timber-framed house does not collapse when they mis-click a beam.
//
//  Felling is a flood fill through GROWN tree blocks only, and it is
//  bounded three ways, all of which matter:
//
//    * y >= the broken block's y -- so breaking a trunk at head height
//      does not take the stump you are standing on, and a tree growing
//      out of a cliff does not reach down it;
//    * FELL_RADIUS horizontally and FELL_HEIGHT up -- so one tree whose
//      canopy touches another cannot take the whole forest;
//    * FELL_MAX blocks in total -- a hard stop, so a pathological shape
//      cannot stall a tick however the other two are tuned.
//
//  Pure: no engine, no allocation (the fill has a fixed-size stack).
//  tools/worldcheck.c tests the rule both ways.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "game/physics.h"
#include "game/raycast.h"
#include "items/inventory.h"
#include "items/items.h"

#define FELL_RADIUS 8   // blocks from the break, horizontally
#define FELL_HEIGHT 24  // ... and upwards
#define FELL_MAX    512 // total blocks one fell can take

// What a break did, so the caller can make the right noise, spawn the
// right drops and show the right particles.
typedef struct {
    bool    ok;
    // The block will not break with what is in hand (BF2_TOOL_REQUIRED)
    // and this is the tool it wants -- an ITEM id, so the caller can
    // name it in the player's language rather than saying only "no".
    uint16_t needs_tool;
    uint8_t block;     // what was there
    int     felled;    // blocks removed IN TOTAL (1 for an ordinary break)
    bool    was_tree;  // the felling rule applied
    int     dropped;   // items spawned on the ground
} break_result_t;

// Break the block at (x, y, z) with `tool_item` in hand. Refuses
// bedrock, air and anything outside the resident world. Applies the
// logging rule, and drops what the block table says -- but only if the
// tool qualifies (items.h, item_can_harvest): a block mined with too
// soft a tool still breaks and simply yields nothing, which is what
// makes a stone pickaxe progress rather than a permission slip.
break_result_t interact_break(int32_t x, int32_t y, int32_t z, uint16_t tool_item);

// Put `block` in the empty cell the ray reported (hit->p*), if it is
// free and the player's own box is not in the way. `avoid` is the
// player's box, or NULL to skip that test.
//
// Sets ST_PLACED, which is what makes the logging rule work and what
// will later stop placed leaves decaying.
bool interact_place(ray_hit_t const* hit, uint8_t block, phys_body_t const* avoid);

// The same, told WHICH WAY THE PLAYER IS FACING (a flattened look
// direction). Only a block whose shape depends on it cares -- the fence
// gate, which lies across the way you are walking -- and
// interact_place() is this with a fixed direction, for every caller
// that has no such block in hand.
bool interact_place_dir(ray_hit_t const* hit, uint8_t block, phys_body_t const* avoid, float dx, float dz);

// THE OTHER HALF OF THE BED at (x, y, z), or false if that is not a
// bed. Both halves carry the same facing in their state byte, so the
// pair needs no stored coordinates: the head is the foot's cell plus
// its facing and the foot is the head's minus it (blocks.h).
bool interact_bed_other(int32_t x, int32_t y, int32_t z, int32_t* ox, int32_t* oy, int32_t* oz);

// OPEN OR SHUT A GATE at (x, y, z), which is the whole of what using
// one does: the two states are two block ids (blocks.h), so this swaps
// the id and keeps the state byte that says which way it lies. False if
// there is no gate there.
//
// It lives here rather than in player.c for the reason everything else
// in this file does: it is a change to the world, and the world's rules
// are tested on the host.
bool interact_toggle_gate(int32_t x, int32_t y, int32_t z);

// Fell the tree reachable from (x, y, z), which must already have been
// checked as a grown tree block. Returns how many blocks it took,
// including the one at (x, y, z). Exposed for the host test.
int interact_fell(int32_t x, int32_t y, int32_t z);

// --- Using what is in your hand ---------------------------------------
//
// THE OTHER HALF OF THE USE KEY. Tapping Use with a block in hand puts
// the block down; tapping it with a BUCKET in hand moves water, and
// nothing about that is a placement -- there is no block to place, the
// target may be a cell the crosshair cannot even see, and what the hand
// ends up holding is not what it started with.
//
// It casts its OWN ray, in RAY_FLUID mode, because the crosshair looks
// straight through water on purpose (raycast.h) and a bucket must not.
//
// The caller applies the result. That split is deliberate: the world
// change and the inventory change have to agree, and the one place that
// knows whether the hand can take what is coming back is the caller.
// Why a use did nothing, for the line under the crosshair. A refusal
// that says nothing is indistinguishable from a bug -- the same argument
// as iron refusing a wooden pickaxe (blocks.h, BF2_TOOL_REQUIRED).
typedef enum {
    USE_SAID_NOTHING = 0,
    USE_CANNOT_TILL,     // a hoe, on something that is not soil
    USE_TOO_DRY,         // a seed, on tilled soil with no water near it
    USE_NEEDS_SOIL,      // a seed, on anything but tilled soil
    USE_NEEDS_WATER,     // rice, away from one-deep water over sand
    USE_ALREADY_RIPE,    // compost, on a crop with nowhere left to grow
    // A BED NEEDS TWO CELLS and only one was free. A placement that
    // does nothing and says nothing is the same bug as a swing that
    // does nothing: the player aims again at exactly the same spot.
    USE_NO_ROOM,
} use_msg_t;

typedef struct {
    bool     acted;    // it did something; do NOT also try to place a block
    uint16_t becomes;  // what the held stack turns into, 0 to leave it alone
    bool     consume;  // take ONE from the held stack (a seed, a compost)
    bool     wear;     // the tool in hand did a job and should wear by one
    uint8_t  msg;      // use_msg_t: why nothing happened, if nothing did
    uint8_t  sound;    // block_sound_t to play, SND_NONE for silence
    // What moved and where, for the flight recorder (common/trace.h) --
    // pouring a bucket out is an edit like any other and belongs in the
    // trace beside the breaks and the placements.
    uint8_t  block;
    int32_t  x, y, z;
} use_result_t;

use_result_t interact_use_item(double ex, double ey, double ez, float dx, float dy, float dz, uint16_t item);

// --- Farming's half of the same key -----------------------------------
//
// THREE MORE THINGS THE USE KEY DOES, all of them through the ray above
// and all of them in world/crops.h, which owns the rules:
//
//    a HOE     tills grass or dirt into farmland, wet or dry
//    a SEED    goes into wet tilled soil -- or, for rice, into the
//              shallows it grows in
//    COMPOST   pushes one crop on by a stage
//
// They are here rather than in player.c for the reason the bucket is:
// the world change and the inventory change have to agree, and the
// caller is the one that knows whether the hand can take the result.
