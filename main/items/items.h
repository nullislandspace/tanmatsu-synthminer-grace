#pragma once
// =====================================================================
//  SynthMiner  --  the item registry
// ---------------------------------------------------------------------
//  ONE TABLE, like blocks.h, and for the same reason: adding a thing is
//  a row, not a search for every switch that needs a new case.
//
//  ITEM IDS 1..BLK_COUNT-1 *ARE* THE BLOCK IDS. A stack of cobblestone
//  is item id BLK_COBBLE, and placing it puts block BLK_COBBLE down.
//  That is not a coincidence to be tidied away later -- it is what
//  stops the two tables drifting, and it is why the inventory can draw
//  a block without a sprite for it: block_def() already knows what it
//  looks like. Real items -- coal, a pickaxe, bread -- start at
//  ITEM_FIRST and carry their own row here.
//
//  Pure: no engine, no allocation. tools/worldcheck.c builds it as-is.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "i18n/strings_gen.h"  // sm_str_t: the name the player reads
#include "world/blocks.h"

// The items that are not blocks. The first is BLK_COUNT, so the two id
// spaces meet without a gap and `id < BLK_COUNT` is the whole test for
// "this is a block".
enum {
    ITEM_COAL = BLK_COUNT,
    ITEM_STICK,
    ITEM_PICK_WOOD,
    ITEM_PICK_STONE,
    ITEM_AXE_WOOD,
    ITEM_AXE_STONE,
    ITEM_SHOVEL_WOOD,
    ITEM_SHOVEL_STONE,
    ITEM_IRON_INGOT,
    ITEM_PICK_IRON,
    ITEM_AXE_IRON,
    ITEM_SHOVEL_IRON,
    // THE BUCKET, AND WHAT IS IN IT. One id per content, not one id
    // with a data field on the stack.
    //
    // The user asked for "metadata attached to it, to say if it is
    // empty or holds some kinds of liquid", and this IS that metadata
    // -- kept where every other fact about an item is kept. A data
    // field on inv_slot_t would be a second kind of identity that only
    // one item has, and every place that asks what a stack is -- the
    // icon, the label, the recipe match, inv_count, inv_take, the save
    // file -- would need to learn about it. A row each costs nothing
    // and all of those keep working untouched.
    //
    // item_bucket_contents() / item_bucket_filled_with() are the pair
    // that make them one family rather than four unrelated things, so
    // lava and milk are a row here and a row in BUCKETS, and no logic.
    ITEM_BUCKET,
    ITEM_BUCKET_WATER,

    // --- Farming (step 9) --------------------------------------------
    //
    // The hoe is the first tool whose point is not speed: it TILLS.
    // Three tiers like the others, and 2 sticks plus 2 material, which
    // sits between a shovel's 1 and a pickaxe's 3 and is Minecraft's own
    // price (D-106).
    ITEM_HOE_WOOD,
    ITEM_HOE_STONE,
    ITEM_HOE_IRON,

    // SEEDS AND HARVESTS. A potato, a bean and a grain of rice plant
    // themselves -- the thing you eat is the thing you sow -- while
    // wheat and tomatoes have a seed of their own, so both appear here
    // (block_def_t.seed_item says which is which).
    ITEM_WHEAT_SEEDS,
    ITEM_WHEAT,
    ITEM_POTATO,
    ITEM_TOMATO_SEEDS,
    ITEM_TOMATO,
    ITEM_BEANS,
    ITEM_RICE,

    // What the composter makes: fertiliser, and bait for a rod that does
    // not exist yet (D-104, D-109).
    ITEM_COMPOST,
    ITEM_WORM,
    ITEM_COUNT
};

#define ITEM_STACK_MAX 64

typedef struct {
    char const* name;        // stable id, as blocks have one
    sm_str_t    label;       // what the player reads, in their language (i18n.h)
    uint8_t     stack_max;   // 1 for a tool, ITEM_STACK_MAX for most things
    uint8_t     tool;        // tool_t this counts as, TOOL_NONE for anything else
    uint8_t     tool_level;  // 1 wood, 2 stone, 3 iron
    uint16_t    durability;  // uses before it breaks; 0 = never wears
    uint16_t    fuel;        // ticks it burns in a furnace; 0 = it does not (game/furnace.h)
    uint32_t    argb;        // the icon, until items have sprites of their own
    // IT ROTS DOWN in a composter (game/composter.h). A column rather
    // than a list inside the machine, for the same reason `fuel` is one:
    // a new plant brings its own answer, and the composter never has to
    // be edited to learn about it (D-104).
    //
    // LAST ON PURPOSE. The rows in items.c are positional, so a column
    // inserted anywhere else would silently shift every one of them --
    // argb would land in `fuel` and a pickaxe would burn for four
    // billion ticks. A new column goes on the end, or every row changes.
    uint8_t     compost;
} item_def_t;

// Everything about an item, blocks included. For a block id this is
// synthesised from the block table rather than stored twice.
item_def_t item_def(uint16_t id);

static inline bool item_is_block(uint16_t id) {
    return id != BLK_AIR && id < BLK_COUNT;
}

// The block this item places, or BLK_AIR if it places nothing.
static inline uint8_t item_block(uint16_t id) {
    return item_is_block(id) ? (uint8_t)id : BLK_AIR;
}

// The item called `name`, or 0 if this build has no such item. How a
// saved inventory survives items being added or renumbered: it stores
// names, the way level.smw's palette does for blocks (D-31).
uint16_t item_by_name(char const* name);

// What to call `id` on screen, in the player's language. The stable
// `name` is the id a save file keys on and is never translated; this is
// the other one. Every item a player can carry has a label, and
// worldcheck fails the build over one that does not -- a nameless row
// in the crafting book is not a thing anybody would notice by playing.
static inline sm_str_t item_label(uint16_t id) {
    return item_def(id).label;
}

// The tool of class `tool` at exactly `level`, or 0 if this build has
// none. What lets a refusal NAME what is needed ("Needs a stone
// pickaxe") instead of saying only that it will not budge.
uint16_t item_tool_for(uint8_t tool, uint8_t level);

// --- Buckets ----------------------------------------------------------

// What `item` is holding: BLK_WATER for a water bucket, BLK_AIR for an
// empty one or for anything that is not a bucket.
uint8_t item_bucket_contents(uint16_t item);

// The bucket holding `fluid`, or 0 if no bucket carries it. BLK_AIR
// gives the empty bucket, which is what emptying one returns.
uint16_t item_bucket_filled_with(uint8_t fluid);

static inline bool item_is_bucket(uint16_t item) {
    return item == ITEM_BUCKET || item_bucket_contents(item) != BLK_AIR;
}

// How many ticks `block` takes to break while holding `tool_item`.
//
// The right tool class divides the time by its level plus one, and a
// tool below the block's `tool_level` still breaks it -- it just drops
// nothing, which is Minecraft's rule and the one that makes a stone
// pickaxe feel like progress rather than a permission slip.
int item_break_ticks(uint8_t block, uint16_t tool_item);

// Does `tool_item` qualify to collect what `block` drops?
bool item_can_harvest(uint8_t block, uint16_t tool_item);
