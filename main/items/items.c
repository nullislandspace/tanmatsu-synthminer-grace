// =====================================================================
//  SynthMiner  --  the item registry (see items.h)
// =====================================================================

#include "items/items.h"

#include <string.h>

// The items that are not blocks. Indexed by id - BLK_COUNT.
static item_def_t const ITEMS[ITEM_COUNT - BLK_COUNT] = {
    [ITEM_COAL - BLK_COUNT]  = {"coal", SM_STR_ITEM_COAL, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 1600, 0xFF2A2A2Eu},
    [ITEM_STICK - BLK_COUNT] = {"stick", SM_STR_ITEM_STICK, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 100, 0xFF9A7040u},

    // Tools. Durability is uses, not ticks: a wooden pickaxe is 60
    // blocks of stone, a stone one 130 -- enough that running out is a
    // thing that happens without being the thing that happens.
    [ITEM_PICK_WOOD - BLK_COUNT]    = {"pickaxe_wood", SM_STR_ITEM_PICKAXE_WOOD, 1, TOOL_PICK, 1, 60, 200, 0xFFB08040u},
    [ITEM_PICK_STONE - BLK_COUNT]   = {"pickaxe_stone", SM_STR_ITEM_PICKAXE_STONE, 1, TOOL_PICK, 2, 130, 0, 0xFF9098A0u},
    [ITEM_AXE_WOOD - BLK_COUNT]     = {"axe_wood", SM_STR_ITEM_AXE_WOOD, 1, TOOL_AXE, 1, 60, 200, 0xFFC08848u},
    [ITEM_AXE_STONE - BLK_COUNT]    = {"axe_stone", SM_STR_ITEM_AXE_STONE, 1, TOOL_AXE, 2, 130, 0, 0xFFA0A8B0u},
    [ITEM_SHOVEL_WOOD - BLK_COUNT]  = {"shovel_wood", SM_STR_ITEM_SHOVEL_WOOD, 1, TOOL_SHOVEL, 1, 60, 200, 0xFFA07838u},
    [ITEM_SHOVEL_STONE - BLK_COUNT] = {"shovel_stone", SM_STR_ITEM_SHOVEL_STONE, 1, TOOL_SHOVEL, 2, 130, 0, 0xFF888F98u},

    // Iron: smelted from the ore, and the only tier that opens iron
    // ore itself. 250 uses, as in Minecraft, and iron does not burn.
    [ITEM_IRON_INGOT - BLK_COUNT]  = {"iron_ingot", SM_STR_ITEM_IRON_INGOT, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFD8D8DEu},
    [ITEM_PICK_IRON - BLK_COUNT]   = {"pickaxe_iron", SM_STR_ITEM_PICKAXE_IRON, 1, TOOL_PICK, 3, 250, 0, 0xFFDEDEE4u},
    [ITEM_AXE_IRON - BLK_COUNT]    = {"axe_iron", SM_STR_ITEM_AXE_IRON, 1, TOOL_AXE, 3, 250, 0, 0xFFDEDEE4u},
    [ITEM_SHOVEL_IRON - BLK_COUNT] = {"shovel_iron", SM_STR_ITEM_SHOVEL_IRON, 1, TOOL_SHOVEL, 3, 250, 0, 0xFFDEDEE4u},

    // BUCKETS DO NOT STACK, EMPTY OR FULL. Minecraft stacks empty ones
    // sixteen deep and filled ones not at all, and that asymmetry is
    // the whole difficulty: filling one out of a stack of sixteen has
    // to find a SECOND slot for the full one, which may not exist --
    // so a bucket dipped in a lake with a full inventory would either
    // vanish or have to put the water back, and one of those is a bug
    // report. At one apiece the stack is swapped in place and there is
    // no failure to get wrong. Iron is cheap enough by then.
    [ITEM_BUCKET - BLK_COUNT]       = {"bucket", SM_STR_ITEM_BUCKET, 1, TOOL_NONE, 0, 0, 0, 0xFFC0C4CCu},
    [ITEM_BUCKET_WATER - BLK_COUNT] = {"bucket_water", SM_STR_ITEM_BUCKET_WATER, 1, TOOL_NONE, 0, 0, 0, 0xFF3A62C8u},

    // THE HOE. Its durability is the same as the other tools of its
    // tier, and it burns like them if it is wooden -- the fuel column
    // is why a wooden thing never needs a line in the furnace.
    //
    // A hoe never breaks a block faster than a fist does, which is why
    // no block names TOOL_HOE in its `tool`: the tiers buy uses, not
    // speed, so an iron hoe is a hoe you stop re-making.
    [ITEM_HOE_WOOD - BLK_COUNT]  = {"hoe_wood", SM_STR_ITEM_HOE_WOOD, 1, TOOL_HOE, 1, 60, 200, 0xFFB4883Cu},
    [ITEM_HOE_STONE - BLK_COUNT] = {"hoe_stone", SM_STR_ITEM_HOE_STONE, 1, TOOL_HOE, 2, 130, 0, 0xFF949CA4u},
    [ITEM_HOE_IRON - BLK_COUNT]  = {"hoe_iron", SM_STR_ITEM_HOE_IRON, 1, TOOL_HOE, 3, 250, 0, 0xFFDEDEE4u},

    // Seeds and harvests. None of them is food yet: eating arrives with
    // the hunger loop in step 10, and these are its ingredients.
    [ITEM_WHEAT_SEEDS - BLK_COUNT]  = {"wheat_seeds", SM_STR_ITEM_WHEAT_SEEDS, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFB8B070u, 1},
    [ITEM_WHEAT - BLK_COUNT]        = {"wheat", SM_STR_ITEM_WHEAT, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFD8BC5Cu, 1},
    [ITEM_POTATO - BLK_COUNT]       = {"potato", SM_STR_ITEM_POTATO, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFC89A54u, 1},
    [ITEM_TOMATO_SEEDS - BLK_COUNT] = {"tomato_seeds", SM_STR_ITEM_TOMATO_SEEDS, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFC0A878u, 1},
    [ITEM_TOMATO - BLK_COUNT]       = {"tomato", SM_STR_ITEM_TOMATO, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFD03C2Cu, 1},
    [ITEM_BEANS - BLK_COUNT]        = {"beans", SM_STR_ITEM_BEANS, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFF9C6A38u, 1},
    [ITEM_RICE - BLK_COUNT]         = {"rice", SM_STR_ITEM_RICE, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE8E4D0u, 1},

    // Compost, and the worms that come out of the same box.
    [ITEM_COMPOST - BLK_COUNT] = {"compost", SM_STR_ITEM_COMPOST, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFF54402Cu},
    [ITEM_WORM - BLK_COUNT]    = {"worm", SM_STR_ITEM_WORM, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFC08078u},

    // What comes off an animal, and what the two slow machines make of
    // it. Raw meat does not rot down in a composter: the user's list of
    // what goes in it is plant matter and food, and a farm that turned
    // its own pork into fertiliser would make the pig the cheapest
    // crop there is.
    [ITEM_PORK - BLK_COUNT]        = {"pork", SM_STR_ITEM_PORK, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE0847Cu},
    [ITEM_BEEF - BLK_COUNT]        = {"beef", SM_STR_ITEM_BEEF, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFB0403Cu},
    [ITEM_BUCKET_MILK - BLK_COUNT] = {"bucket_milk", SM_STR_ITEM_BUCKET_MILK, 1, TOOL_NONE, 0, 0, 0, 0xFFF2F0E6u},
    [ITEM_CHEESE - BLK_COUNT]      = {"cheese", SM_STR_ITEM_CHEESE, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE8B84Cu},
    [ITEM_SAUSAGE - BLK_COUNT]     = {"sausage", SM_STR_ITEM_SAUSAGE, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFA85440u},
    [ITEM_SAUSAGE_VEG - BLK_COUNT] = {"sausage_veg", SM_STR_ITEM_SAUSAGE_VEG, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFF986A40u},
    // A bone is a TOOL of no class: it breaks nothing and wears at
    // nothing, and the one thing it does -- taming a dog -- is a use,
    // not a swing (game/mob.h).
    [ITEM_BONE - BLK_COUNT]        = {"bone", SM_STR_ITEM_BONE, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE4E0D0u},

    // --- The sheep's half of the farm ---------------------------------
    //
    // The three trailing numbers are `compost`, `hunger`, `saturation`.
    // Raw meat is none of those: it composts no more than pork does and
    // it is not food until a stove has been at it.
    [ITEM_MUTTON - BLK_COUNT] = {"mutton", SM_STR_ITEM_MUTTON, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFC6605Cu},
    // WOOL ROTS DOWN. It is the one animal product that does: it is
    // hair, it is what a composter is for, and a player with a shed
    // full of it should have something to do with the surplus.
    [ITEM_WOOL - BLK_COUNT]   = {"wool", SM_STR_ITEM_WOOL, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFEEEAE0u, 1},
    // SHEARS ARE A TOOL OF THEIR OWN CLASS (TOOL_SHEARS, which has been
    // in blocks.h since step 0.3 with nothing to put in it). They break
    // no block faster than a fist; what they do is take a fleece, and
    // they wear by one each time.
    [ITEM_SHEARS - BLK_COUNT] = {"shears", SM_STR_ITEM_SHEARS, 1, TOOL_SHEARS, 1, 220, 0, 0xFFC8CCD4u},
    [ITEM_STRING - BLK_COUNT] = {"string", SM_STR_ITEM_STRING, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE2DED2u},

    // The two dishes, with the user's numbers. Nothing can cook them
    // until the stove exists (step 11); these rows are where the
    // numbers live in the meantime.
    [ITEM_MUTTON_MASH - BLK_COUNT] = {"mutton_mash", SM_STR_ITEM_MUTTON_MASH, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0,
                                      0xFFD8A870u, 0, 5, 2},
    [ITEM_KEBAB - BLK_COUNT]       = {"kebab", SM_STR_ITEM_KEBAB, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFC07840u, 0,
                                      8, 7},

    // --- Fishing -------------------------------------------------------
    //
    // The rod does not wear out (the user), so its durability is 0 like
    // a bucket's rather than a tool's. It is wooden, so it burns.
    [ITEM_ROD - BLK_COUNT]     = {"fishing_rod", SM_STR_ITEM_FISHING_ROD, 1, TOOL_NONE, 0, 0, 200, 0xFFB08848u},
    // Raw fish, and none of it is food until a stove has been at it --
    // the same as every other raw thing here except milk and tomatoes.
    [ITEM_SARDINE - BLK_COUNT] = {"sardine", SM_STR_ITEM_SARDINE, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFB8C0C8u},
    [ITEM_SALMON - BLK_COUNT]  = {"salmon", SM_STR_ITEM_SALMON, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFE08858u},
    [ITEM_SHRIMP - BLK_COUNT]  = {"shrimp", SM_STR_ITEM_SHRIMP, ITEM_STACK_MAX, TOOL_NONE, 0, 0, 0, 0xFFF0A078u},
};

// A bucket and what is in it. Adding lava is this row plus a row in the
// item table, the fluid table (world/fluid.c) and the block table --
// and nothing in interact.c, which is the point of having the pair of
// lookups below rather than a test against ITEM_BUCKET_WATER.
static struct {
    uint8_t  fluid;
    uint16_t item;
    uint32_t argb;  // what is inside it, when the world has no block for it
} const BUCKETS[] = {
    // THE EMPTY ONE IS FIRST and has to stay first: BLK_AIR appears
    // twice now, and item_bucket_filled_with(BLK_AIR) -- which is what
    // emptying one returns -- takes the first match.
    {BLK_AIR, ITEM_BUCKET, 0xFF3A3E46u},
    {BLK_WATER, ITEM_BUCKET_WATER, 0xFF3054C4u},
    // MILK IS NOT A FLUID IN THE WORLD. There is no milk block and
    // there is no reason for one -- you cannot pour it out, you drink
    // it or you make cheese of it -- so its `fluid` is BLK_AIR and the
    // family it belongs to is this table rather than the block id.
    // item_is_bucket() therefore asks the table and not the contents.
    {BLK_AIR, ITEM_BUCKET_MILK, 0xFFF2F0E6u},
};

uint8_t item_bucket_contents(uint16_t item) {
    for (size_t i = 0; i < sizeof BUCKETS / sizeof BUCKETS[0]; i++) {
        if (BUCKETS[i].item == item) return BUCKETS[i].fluid;
    }
    return BLK_AIR;
}

uint16_t item_bucket_filled_with(uint8_t fluid) {
    for (size_t i = 0; i < sizeof BUCKETS / sizeof BUCKETS[0]; i++) {
        if (BUCKETS[i].fluid == fluid) return BUCKETS[i].item;
    }
    return 0;
}

bool item_is_bucket(uint16_t item) {
    for (size_t i = 0; i < sizeof BUCKETS / sizeof BUCKETS[0]; i++) {
        if (BUCKETS[i].item == item) return true;
    }
    return false;
}

uint32_t item_bucket_argb(uint16_t item) {
    for (size_t i = 0; i < sizeof BUCKETS / sizeof BUCKETS[0]; i++) {
        if (BUCKETS[i].item != item) continue;
        // A world fluid knows its own colour; anything else carries it
        // in the row, which is what keeps milk from needing a block.
        return BUCKETS[i].fluid != BLK_AIR ? item_def(BUCKETS[i].fluid).argb : BUCKETS[i].argb;
    }
    return 0;
}

// A flat colour standing in for the block's texture in the inventory.
// Approximately each block's average, which is what a 16x16 texture
// reads as at icon size anyway; the real mini-cube icon (D-03) can
// replace this without anything above changing.
static uint32_t const BLOCK_ARGB[BLK_COUNT] = {
    [BLK_AIR] = 0,
    [BLK_GRASS] = 0xFF5C9634u,        [BLK_DIRT] = 0xFF7A563Au,
    [BLK_STONE] = 0xFF7A7A7Cu,        [BLK_COBBLE] = 0xFF767676u,
    [BLK_SAND] = 0xFFD6C896u,         [BLK_WATER] = 0xFF3054C4u,
    [BLK_LOG] = 0xFF644C2Eu,          [BLK_PLANKS] = 0xFFA4804Eu,
    [BLK_LEAVES] = 0xFF3A7026u,       [BLK_COAL_ORE] = 0xFF606062u,
    [BLK_GLASS] = 0xFFC8D8DEu,        [BLK_TORCH] = 0xFF6E502Cu,
    [BLK_FLOWER_RED] = 0xFFD62824u,   [BLK_FLOWER_YELLOW] = 0xFFFAD428u,
    [BLK_TALL_GRASS] = 0xFF5C9634u,   [BLK_BARRIER] = 0xFF303030u,
    [BLK_BEDROCK] = 0xFF4A4A4Au,      [BLK_GRAVEL] = 0xFF847C78u,
    [BLK_SIGN] = 0xFFA4804Eu,         [BLK_CRAFTING_TABLE] = 0xFF9C7A4Au,
    [BLK_FURNACE] = 0xFF707072u,        [BLK_IRON_ORE] = 0xFF8E8278u,
    [BLK_CHEST] = 0xFF96703Eu,          [BLK_TRASH] = 0xFF605C5Au,
    [BLK_BENCH] = 0xFF967446u,          [BLK_BIRCH_LOG] = 0xFFD2D0C4u,
    [BLK_BIRCH_LEAVES] = 0xFF6C983Eu,   [BLK_CACTUS] = 0xFF4A803Cu,
    [BLK_SNOW] = 0xFFECF0F8u,           [BLK_SANDSTONE] = 0xFFD6C694u,
    [BLK_FARMLAND] = 0xFF8A6642u,       [BLK_FARMLAND_WET] = 0xFF5E4428u,
    [BLK_COMPOSTER] = 0xFF9A7A46u,      [BLK_WHEAT_CROP] = 0xFFC8B45Au,
    [BLK_POTATO_CROP] = 0xFF6A9E48u,    [BLK_TOMATO_CROP] = 0xFFB4462Eu,
    [BLK_BEAN_CROP] = 0xFF86A24Eu,      [BLK_RICE_CROP] = 0xFFC2BE6Au,
    [BLK_RICE_TOP] = 0xFFD2C878u,
};

// What each block is CALLED on screen, beside the colour above. A
// second per-block table in this file rather than a column in blocks.h,
// for the same reason BLOCK_ARGB is here: the block registry describes
// how a block behaves, and how it is spelled in 32 languages is the
// item layer's business.
static sm_str_t const BLOCK_LABEL[BLK_COUNT] = {
    [BLK_GRASS] = SM_STR_ITEM_GRASS,         [BLK_DIRT] = SM_STR_ITEM_DIRT,
    [BLK_STONE] = SM_STR_ITEM_STONE,         [BLK_COBBLE] = SM_STR_ITEM_COBBLESTONE,
    [BLK_SAND] = SM_STR_ITEM_SAND,           [BLK_WATER] = SM_STR_ITEM_WATER,
    [BLK_LOG] = SM_STR_ITEM_LOG,             [BLK_PLANKS] = SM_STR_ITEM_PLANKS,
    [BLK_LEAVES] = SM_STR_ITEM_LEAVES,       [BLK_COAL_ORE] = SM_STR_ITEM_COAL_ORE,
    [BLK_GLASS] = SM_STR_ITEM_GLASS,         [BLK_TORCH] = SM_STR_ITEM_TORCH,
    [BLK_FLOWER_RED] = SM_STR_ITEM_FLOWER_RED,
    [BLK_FLOWER_YELLOW] = SM_STR_ITEM_FLOWER_YELLOW,
    [BLK_TALL_GRASS] = SM_STR_ITEM_TALL_GRASS,
    [BLK_BEDROCK] = SM_STR_ITEM_BEDROCK,     [BLK_GRAVEL] = SM_STR_ITEM_GRAVEL,
    [BLK_SIGN] = SM_STR_ITEM_SIGN,
    [BLK_CRAFTING_TABLE] = SM_STR_ITEM_CRAFTING_TABLE,
    [BLK_FURNACE] = SM_STR_ITEM_FURNACE,
    [BLK_IRON_ORE] = SM_STR_ITEM_IRON_ORE,
    [BLK_CHEST] = SM_STR_ITEM_CHEST,
    [BLK_TRASH] = SM_STR_ITEM_TRASH_CHEST,
    [BLK_BENCH] = SM_STR_ITEM_DISASSEMBLY_BENCH,
    [BLK_BIRCH_LOG] = SM_STR_ITEM_BIRCH_LOG,
    [BLK_BIRCH_LEAVES] = SM_STR_ITEM_BIRCH_LEAVES,
    [BLK_CACTUS] = SM_STR_ITEM_CACTUS,
    [BLK_SNOW] = SM_STR_ITEM_SNOW,
    [BLK_SANDSTONE] = SM_STR_ITEM_SANDSTONE,
    [BLK_FARMLAND] = SM_STR_ITEM_FARMLAND,
    [BLK_FARMLAND_WET] = SM_STR_ITEM_FARMLAND,
    [BLK_COMPOSTER] = SM_STR_ITEM_COMPOSTER,
    [BLK_WHEAT_CROP] = SM_STR_ITEM_WHEAT_CROP,
    [BLK_POTATO_CROP] = SM_STR_ITEM_POTATO_CROP,
    [BLK_TOMATO_CROP] = SM_STR_ITEM_TOMATO_CROP,
    [BLK_BEAN_CROP] = SM_STR_ITEM_BEAN_CROP,
    [BLK_RICE_CROP] = SM_STR_ITEM_RICE_CROP,
    [BLK_RICE_TOP] = SM_STR_ITEM_RICE_CROP,
    [BLK_CHEESE_MAKER] = SM_STR_ITEM_CHEESE_MAKER,
    [BLK_SAUSAGE_MAKER] = SM_STR_ITEM_SAUSAGE_MAKER,
    [BLK_FENCE] = SM_STR_ITEM_FENCE,
    // Open or shut, it is a gate: one name for two ids, the way wet and
    // dry farmland share one.
    [BLK_FENCE_GATE] = SM_STR_ITEM_FENCE_GATE,
    [BLK_FENCE_GATE_OPEN] = SM_STR_ITEM_FENCE_GATE,
    // One name for both halves: it is one bed.
    [BLK_BED_FOOT] = SM_STR_ITEM_BED_FOOT,
    [BLK_BED_HEAD] = SM_STR_ITEM_BED_FOOT,
    // Air and the barrier are never in anybody's hands and have none.
};

// What ROTS DOWN, block side. Leaves, flowers, undergrowth: the green
// stuff a player clears anyway, which is what makes the composter a use
// for the by-product rather than another thing to farm (D-104).
static uint8_t const BLOCK_COMPOST[BLK_COUNT] = {
    [BLK_LEAVES] = 1, [BLK_BIRCH_LEAVES] = 1, [BLK_TALL_GRASS] = 1,
    [BLK_FLOWER_RED] = 1, [BLK_FLOWER_YELLOW] = 1,
    // A whole plant, pulled up green: the crop blocks themselves. Not
    // the soil, obviously, and not the composter.
    [BLK_WHEAT_CROP] = 1, [BLK_POTATO_CROP] = 1, [BLK_TOMATO_CROP] = 1,
    [BLK_BEAN_CROP] = 1, [BLK_RICE_CROP] = 1, [BLK_RICE_TOP] = 1,
};

// What a BLOCK burns for, in ticks. Wood and things made of wood, as
// the user asked -- "fuel is everything that burns (wood, wooden tools,
// planks, sticks)". Minecraft's numbers: a log or planks 300, a stick
// 100, coal 1600, a wooden tool 200.
static uint16_t const BLOCK_FUEL[BLK_COUNT] = {
    [BLK_LOG] = 300, [BLK_PLANKS] = 300, [BLK_CRAFTING_TABLE] = 300,
    [BLK_CHEST] = 300, [BLK_TRASH] = 300, [BLK_BENCH] = 300,
    [BLK_BIRCH_LOG] = 300, [BLK_COMPOSTER] = 300,
};

item_def_t item_def(uint16_t id) {
    if (item_is_block(id)) {
        // Synthesised, not stored: the block table is the authority on
        // what a block is called and what it looks like, and a second
        // copy here would be a second thing to keep right.
        block_def_t const* b = block_def((uint8_t)id);
        return (item_def_t){
            .name       = b->name,
            .label      = BLOCK_LABEL[id < BLK_COUNT ? id : BLK_BARRIER],
            .stack_max  = ITEM_STACK_MAX,
            .tool       = TOOL_NONE,
            .tool_level = 0,
            .durability = 0,
            .fuel       = BLOCK_FUEL[id < BLK_COUNT ? id : BLK_BARRIER],
            .compost    = BLOCK_COMPOST[id < BLK_COUNT ? id : BLK_BARRIER],
            .argb       = BLOCK_ARGB[id < BLK_COUNT ? id : BLK_BARRIER],
        };
    }
    if (id >= BLK_COUNT && id < ITEM_COUNT) return ITEMS[id - BLK_COUNT];
    return (item_def_t){.name = "", .stack_max = 1, .argb = 0};
}

uint16_t item_by_name(char const* name) {
    if (name == NULL || *name == '\0') return 0;
    for (uint16_t id = 1; id < ITEM_COUNT; id++) {
        if (id == BLK_BARRIER) continue;  // never carried
        if (strcmp(item_def(id).name, name) == 0) return id;
    }
    return 0;
}

uint16_t item_tool_for(uint8_t tool, uint8_t level) {
    if (tool == TOOL_NONE || level == 0) return 0;
    for (uint16_t id = BLK_COUNT; id < ITEM_COUNT; id++) {
        item_def_t const d = item_def(id);
        if (d.tool == tool && d.tool_level == level) return id;
    }
    return 0;
}

int item_break_ticks(uint8_t block, uint16_t tool_item) {
    block_def_t const* b = block_def(block);
    if (b->hardness == HARDNESS_UNBREAKABLE) return -1;

    int ticks = (int)b->hardness;
    if (ticks < 1) ticks = 1;

    item_def_t const t = item_def(tool_item);
    // The right class only. A pickaxe does not speed up dirt, which is
    // what makes carrying more than one tool worth the slots.
    //
    // TWICE THE LEVEL, not the level plus one. The old divisor -- 2x
    // for wood, 3x for stone -- was a rounding error next to a fist,
    // and the user's complaint was exactly that: "mining with the wrong
    // tool is a lot slower" was not true. Now hand 1x, wood 2x, stone
    // 4x, iron 6x, so a block of stone is 7.5 seconds by hand and 1.9
    // with a stone pickaxe. No hardness number had to move.
    if (b->tool != TOOL_NONE && t.tool == b->tool && t.tool_level > 0) {
        ticks /= 2 * (int)t.tool_level;
    }
    return ticks < 1 ? 1 : ticks;
}

bool item_can_harvest(uint8_t block, uint16_t tool_item) {
    block_def_t const* b = block_def(block);
    if (b->tool_level == 0) return true;  // hands are enough
    item_def_t const t = item_def(tool_item);
    return t.tool == b->tool && t.tool_level >= b->tool_level;
}
