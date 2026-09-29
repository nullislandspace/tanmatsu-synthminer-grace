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

    // --- Animals (step 10) -------------------------------------------
    //
    // WHAT AN ANIMAL IS WORTH, and none of it is food yet: eating is
    // the next step, and these are its ingredients (Part A's table).
    // Pork and beef come off a pig and a cow; the milk is a BUCKET, so
    // it is one more row in BUCKETS[] below and needs no new idea
    // (D-100, and the half of it that was written down as a guess).
    ITEM_PORK,
    ITEM_BEEF,
    ITEM_BUCKET_MILK,

    // What the two slow machines make of them. The fake sausage is two
    // beans and is worth EXACTLY what the pork one is worth -- the
    // vegetarian option is not a consolation prize, and it counts as a
    // pizza's sausage (the user, 2026-09-29).
    ITEM_CHEESE,
    ITEM_SAUSAGE,
    ITEM_SAUSAGE_VEG,

    // AND THE RARE THING THAT COMES OUT WITH A PORK SAUSAGE. The user's
    // answer to "what tames a dog": "When feeding the sausage maker, a
    // rare drop is a bone. That is what you tame the dog with." So a
    // dog costs a pig, a flower and some luck rather than a dish off a
    // stove that does not exist yet.
    ITEM_BONE,

    // --- Sheep, and what comes off one (step 10, the user's round two)
    //
    // MUTTON is the third meat and the first that is worth more cooked
    // than any other: both of its dishes are near the top of Part A's
    // table. WOOL is the interesting one -- it is the first material in
    // this game that is neither mined nor grown, and it is where STRING
    // comes from, which has been an open question since the rod was
    // designed (Part A: "string will not come from spiders").
    ITEM_MUTTON,
    ITEM_WOOL,
    ITEM_SHEARS,
    ITEM_STRING,

    // The two dishes mutton makes, which need a stove and so cannot be
    // cooked until step 11. Their rows are here now because the numbers
    // are the user's and this is where numbers live -- the recipes name
    // a station nothing has yet, so they are inert rather than wrong.
    ITEM_MUTTON_MASH,
    ITEM_KEBAB,

    // --- Fishing (step 12) --------------------------------------------
    //
    // THE ROD, AND WHAT COMES UP. Three sticks and two string (the
    // user's final recipe, 2026-09-29), so a rod costs a flock: string
    // is wool, wool wants shears, shears want iron. No durability
    // either, their call -- the rod is paid for in worms, and charging
    // twice for one activity is how a system stops being worth using.
    ITEM_ROD,
    ITEM_SARDINE,
    ITEM_SALMON,
    ITEM_SHRIMP,
    ITEM_COUNT
};

#define ITEM_STACK_MAX 64

// HOW MANY TEXTURES THE GAME ASKS FOR BY NAME, on top of the block
// materials (VM_COUNT, voxel_mesh.h). The texture cache has to hold
// both at once, and chunk_render.c asserts that it does.
//
// IT IS DERIVED, NOT COUNTED BY HAND, and that is the whole point.
// F-120 was a hand-counted budget that stopped being true when a round
// of new blocks and items went in: everything past the end of the cache
// loaded as a flat colour, the badge logged "cache full" twenty times a
// boot, and nobody read it. Every item that is not a block has an icon
// of its own (item_<name>.png, game/hud.c), so adding one moves this
// number by itself.
//
// The 16 covers what is asked for by name and belongs to no item: the
// water blend, three torch frames, the flame, Fred's face -- and the
// handful of BLOCKS drawn as things rather than cubes (BF2_ITEM_ICON:
// the torch, the fence, the gate), which cannot be counted at compile
// time. tools/worldcheck.c counts the real ones and fails if they ever
// outgrow this slack, so the estimate cannot rot either.
#define TEX_BY_NAME ((int)(ITEM_COUNT - BLK_COUNT) + 16)

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

    // WHAT EATING IT IS WORTH (Part A's table). Both are Minecraft's
    // units: hunger is the drumsticks and saturation is the invisible
    // reserve that drains first. 0 for anything that is not food --
    // which is most of the table, and why these are last.
    //
    // The loop that spends them is step 11; the numbers are here
    // because the user gave them with the food and a number in two
    // places is a number that will disagree with itself.
    uint8_t     hunger;
    uint8_t     saturation;
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

// Is `item` a pail at all -- empty, full of a world fluid, or full of
// milk? A function rather than a test on the contents, because MILK IS
// NOT A BLOCK: there is no milk fluid to pour and there never will be,
// so a milk bucket's contents read as BLK_AIR and an "is it empty"
// test would call it an empty bucket and dip it in the nearest lake.
bool item_is_bucket(uint16_t item);

// The colour of what is inside one, for the model in Fred's fist
// (fred.c). A world fluid takes its block's colour; milk takes its
// own, and an empty pail is its own shadow.
uint32_t item_bucket_argb(uint16_t item);

// How many ticks `block` takes to break while holding `tool_item`.
//
// The right tool class divides the time by its level plus one, and a
// tool below the block's `tool_level` still breaks it -- it just drops
// nothing, which is Minecraft's rule and the one that makes a stone
// pickaxe feel like progress rather than a permission slip.
int item_break_ticks(uint8_t block, uint16_t tool_item);

// Does `tool_item` qualify to collect what `block` drops?
bool item_can_harvest(uint8_t block, uint16_t tool_item);
