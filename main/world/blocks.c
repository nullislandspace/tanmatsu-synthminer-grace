// =====================================================================
//  SynthMiner  --  the block table (see blocks.h)
// ---------------------------------------------------------------------
//  Hardness is in 20 Hz ticks, bare-handed. Rough scale: 5 = instant
//  (a flower), 20 = one second (dirt), 150 = seven seconds (stone by
//  hand). A tool of the right class divides it; step 4 owns that maths.
//
//  `drop_item` is an ITEM id (items/items.h), and for anything that
//  drops itself that is simply its own block id -- the two id spaces
//  are deliberately the same below BLK_COUNT. The interesting rows are
//  the ones where they differ: stone drops cobblestone, grass drops
//  dirt, coal ore drops coal. A block with no drop row drops nothing,
//  which is right for water, leaves, glass and tall grass.
// =====================================================================

#include "world/blocks.h"

#include "items/items.h"    // the ITEM_* ids the drop column names
#include "world/blockent.h"  // the BE_* kinds the record column names
#include "world/crops.h"    // CROP_TICKS_*: how long a stage takes

#define M3(t, s, b) \
    { (t), (s), (b) }
#define M1(m) \
    { (m), (m), (m) }

block_def_t const BLOCKS[BLK_COUNT] = {
    [BLK_AIR] = {.name = "air", .kind = K_AIR, .mat = M1(0), .hardness = HARDNESS_UNBREAKABLE, .flags = BF_REPLACEABLE},

    [BLK_GRASS] = {.name     = "grass", .drop_item = BLK_DIRT, .drop_min = 1, .drop_max = 1,
                   .kind     = K_CUBE,
                   .mat      = M3(VM_GRASS_TOP, VM_GRASS_SIDE, VM_DIRT),
                   .hardness = 20,
                   .tool     = TOOL_SHOVEL,
                   .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_SOFT},

    [BLK_DIRT] = {.name     = "dirt", .drop_item = BLK_DIRT, .drop_min = 1, .drop_max = 1,
                  .kind     = K_CUBE,
                  .mat      = M1(VM_DIRT),
                  .hardness = 20,
                  .tool     = TOOL_SHOVEL,
                  .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_GRAVEL},

    [BLK_STONE] = {.name       = "stone", .drop_item = BLK_COBBLE, .drop_min = 1, .drop_max = 1,
                   .kind       = K_CUBE,
                   .mat        = M1(VM_STONE),
                   .hardness   = 150,
                   .tool       = TOOL_PICK,
                   .tool_level = 1,
                   .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE},

    [BLK_COBBLE] = {.name       = "cobblestone", .drop_item = BLK_COBBLE, .drop_min = 1, .drop_max = 1,
                    .kind       = K_CUBE,
                    .mat        = M1(VM_COBBLE),
                    .hardness   = 160,
                    .tool       = TOOL_PICK,
                    .tool_level = 1,
                    .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE},

    [BLK_SAND] = {.name     = "sand", .drop_item = BLK_SAND, .drop_min = 1, .drop_max = 1,
                  .kind     = K_CUBE,
                  .mat      = M1(VM_SAND),
                  .hardness = 15,
                  .tool     = TOOL_SHOVEL,
                  .flags    = BF_SOLID | BF_OPAQUE | BF_GRAVITY, .sound = SND_SAND},

    // The engine has no blending, so water is not see-through in the
    // usual sense. It is K_LIQUID instead: only its surface is drawn,
    // and it hides nothing, so you look through a lake at the bed of it
    // (D-86). BF_OPAQUE stays on for the lighting, which reads
    // BF_LIQUID first and dims by 2 a block either way.
    [BLK_WATER] = {.name     = "water",
                   .kind     = K_LIQUID,
                   .mat      = M1(VM_WATER),
                   .hardness = HARDNESS_UNBREAKABLE,
                   .flags    = BF_OPAQUE | BF_REPLACEABLE | BF_LIQUID, .sound = SND_SPLASH},

    [BLK_LOG] = {.name     = "log", .drop_item = BLK_LOG, .drop_min = 1, .drop_max = 1,
                 .kind     = K_CUBE,
                 .mat      = M3(VM_LOG_TOP, VM_LOG_SIDE, VM_LOG_TOP),
                 .hardness = 40,
                 .tool     = TOOL_AXE,
                 .flags    = BF_SOLID | BF_OPAQUE | BF_FELLABLE, .flags2 = BF2_TRUNK, .sound = SND_WOOD},

    [BLK_PLANKS] = {.name     = "planks", .drop_item = BLK_PLANKS, .drop_min = 1, .drop_max = 1,
                    .kind     = K_CUBE,
                    .mat      = M1(VM_PLANKS),
                    .hardness = 40,
                    .tool     = TOOL_AXE,
                    .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD},

    [BLK_LEAVES] = {.name     = "leaves",
                    .kind     = K_SEE,
                    .mat      = M1(VM_LEAVES),
                    .hardness = 8,
                    .tool     = TOOL_SHEARS,
                    .flags    = BF_SOLID | BF_FELLABLE | BF_SEE_SELF, .sound = SND_SOFT},

    [BLK_COAL_ORE] = {.name       = "coal_ore", .drop_item = ITEM_COAL, .drop_min = 1, .drop_max = 1,
                      .kind       = K_CUBE,
                      .mat        = M1(VM_COAL),
                      .hardness   = 200,
                      .tool       = TOOL_PICK,
                      .tool_level = 1,
                      .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE},

    [BLK_GLASS] = {.name = "glass", .kind = K_SEE, .mat = M1(VM_GLASS), .hardness = 12, .flags = BF_SOLID, .sound = SND_GLASS},

    [BLK_TORCH] = {.name = "torch", .drop_item = BLK_TORCH, .drop_min = 1, .drop_max = 1, .kind = K_TORCH, .mat = M1(VM_TORCH), .hardness = 1, .light = 14, .flags2 = BF2_ITEM_ICON, .sound = SND_WOOD},

    [BLK_FLOWER_RED] =
        {.name = "flower_red", .drop_item = BLK_FLOWER_RED, .drop_min = 1, .drop_max = 1, .kind = K_PLANT, .mat = M1(VM_FLOWER_RED), .hardness = 1, .flags = BF_REPLACEABLE, .sound = SND_SOFT},

    [BLK_FLOWER_YELLOW] =
        {.name = "flower_yellow", .drop_item = BLK_FLOWER_YELLOW, .drop_min = 1, .drop_max = 1, .kind = K_PLANT, .mat = M1(VM_FLOWER_YELLOW), .hardness = 1, .flags = BF_REPLACEABLE, .sound = SND_SOFT},

    // WHERE FARMING STARTS. Tall grass gives up a wheat seed about half
    // the time, and that is the whole of the entry fee: no recipe, no
    // machine, just the undergrowth that has been in every world since
    // step 1 (D-107). drop_min 0 is what makes it a chance rather than a
    // certainty, and drop_for() treats a roll of nothing as nothing.
    [BLK_TALL_GRASS] =
        {.name = "tall_grass", .drop_item = ITEM_WHEAT_SEEDS, .drop_min = 0, .drop_max = 1,
         .kind = K_PLANT, .mat = M1(VM_TALL_GRASS), .hardness = 1, .flags = BF_REPLACEABLE, .sound = SND_SOFT},

    // Never generated, never placed, never meshed: what world_block()
    // answers for a chunk that is not resident (D-14).
    [BLK_BARRIER] = {.name     = "barrier",
                     .kind     = K_CUBE,
                     .mat      = M1(VM_STONE),
                     .hardness = HARDNESS_UNBREAKABLE,
                     .flags    = BF_SOLID | BF_OPAQUE},

    // The floor of the world, as in Beta: nothing breaks it. Only the
    // Far Lands put it down so far (D-79).
    [BLK_BEDROCK] = {.name     = "bedrock",
                     .kind     = K_CUBE,
                     .mat      = M1(VM_BEDROCK),
                     .hardness = HARDNESS_UNBREAKABLE,
                     .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_STONE},

    // The one block a player has to make before they can make anything
    // else (Part C). Wood, so an axe is the tool and it comes back when
    // broken -- a table left in a cave is not lost.
    [BLK_CRAFTING_TABLE] = {.name     = "crafting_table", .drop_item = BLK_CRAFTING_TABLE, .drop_min = 1, .drop_max = 1,
                            .kind     = K_CUBE,
                            .mat      = M3(VM_TABLE_TOP, VM_TABLE_SIDE, VM_PLANKS),
                            .hardness = 50,
                            .tool     = TOOL_AXE,
                            .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD,
                            .flags2   = BF2_USABLE},

    // Stone, so it wants a pickaxe, and it keeps a block entity for
    // its three slots and its fire (world/blockent.h). THE OPENING IS
    // ON ALL FOUR SIDES: there is no facing bit in the state byte yet,
    // and a furnace you can always recognise is worth more than one
    // whose single front happens to face the wall you built it into.
    [BLK_FURNACE] = {.name       = "furnace", .drop_item = BLK_FURNACE, .drop_min = 1, .drop_max = 1,
                     .kind       = K_CUBE,
                     .mat        = M3(VM_FURNACE_TOP, VM_FURNACE_FRONT, VM_FURNACE_TOP),
                     .hardness   = 180,
                     .tool       = TOOL_PICK,
                     .tool_level = 1,
                     .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE,
                     .flags2     = BF2_USABLE | BF2_RECORD},

    // Iron. Needs a stone pickaxe and REFUSES the swing without one
    // (BF2_TOOL_REQUIRED, the user's call) -- the ore drops itself and
    // the furnace turns it into an ingot.
    [BLK_IRON_ORE] = {.name       = "iron_ore", .drop_item = BLK_IRON_ORE, .drop_min = 1, .drop_max = 1,
                      .kind       = K_CUBE,
                      .mat        = M1(VM_IRON_ORE),
                      .hardness   = 220,
                      .tool       = TOOL_PICK,
                      .tool_level = 2,
                      .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE,
                      .flags2     = BF2_TOOL_REQUIRED},

    [BLK_CHEST] = {.name     = "chest", .drop_item = BLK_CHEST, .drop_min = 1, .drop_max = 1,
                   .kind     = K_CUBE,
                   .mat      = M3(VM_CHEST_TOP, VM_CHEST_SIDE, VM_CHEST_TOP),
                   .hardness = 50,
                   .tool     = TOOL_AXE,
                   .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD,
                   .flags2   = BF2_USABLE | BF2_RECORD},

    // The same box, and what goes in it does not stay: the user's
    // trashcan, emptied by the clock rather than by a button.
    [BLK_TRASH] = {.name     = "trash_chest", .drop_item = BLK_TRASH, .drop_min = 1, .drop_max = 1,
                   .kind     = K_CUBE,
                   .mat      = M3(VM_TRASH_TOP, VM_TRASH_SIDE, VM_TRASH_TOP),
                   .hardness = 50,
                   .tool     = TOOL_AXE,
                   .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD,
                   .flags2   = BF2_USABLE | BF2_RECORD},

    // Takes things apart. No record: it holds nothing between uses.
    [BLK_BENCH] = {.name     = "disassembly_bench", .drop_item = BLK_BENCH, .drop_min = 1, .drop_max = 1,
                   .kind     = K_CUBE,
                   .mat      = M3(VM_BENCH_TOP, VM_TABLE_SIDE, VM_PLANKS),
                   .hardness = 50,
                   .tool     = TOOL_AXE,
                   .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD,
                   .flags2   = BF2_USABLE},

    // Birch. A second tree, and the reason it exists is that a stand
    // of white trunks reads as somewhere else from a long way off.
    // Fellable like the oak, so the logging rule (Part F) takes it
    // whole.
    [BLK_BIRCH_LOG] = {.name     = "birch_log", .drop_item = BLK_BIRCH_LOG, .drop_min = 1, .drop_max = 1,
                       .kind     = K_CUBE,
                       .mat      = M3(VM_BIRCH_TOP, VM_BIRCH_SIDE, VM_BIRCH_TOP),
                       .hardness = 40,
                       .tool     = TOOL_AXE,
                       .flags    = BF_SOLID | BF_OPAQUE | BF_FELLABLE, .flags2 = BF2_TRUNK, .sound = SND_WOOD},

    [BLK_BIRCH_LEAVES] = {.name     = "birch_leaves",
                          .kind     = K_SEE,
                          .mat      = M1(VM_BIRCH_LEAVES),
                          .hardness = 8,
                          .tool     = TOOL_SHEARS,
                          .flags    = BF_SOLID | BF_FELLABLE | BF_SEE_SELF, .sound = SND_SOFT},

    // A cactus is a solid block here rather than the narrow post
    // Minecraft draws: the mesher has no kind for a thin cube, and a
    // full one still reads as a cactus at this size.
    [BLK_CACTUS] = {.name     = "cactus", .drop_item = BLK_CACTUS, .drop_min = 1, .drop_max = 1,
                    .kind     = K_CUBE,
                    .mat      = M1(VM_CACTUS),
                    .hardness = 12,
                    .flags    = BF_SOLID | BF_OPAQUE, .flags2 = BF2_STACKED, .sound = SND_SOFT},

    [BLK_SNOW] = {.name     = "snow", .drop_item = BLK_SNOW, .drop_min = 1, .drop_max = 1,
                  .kind     = K_CUBE,
                  .mat      = M1(VM_SNOW),
                  .hardness = 10,
                  .tool     = TOOL_SHOVEL,
                  .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_SOFT},

    // Under the sand, which is where it is in Minecraft and where it
    // does the one useful thing it does: tell you how deep the sand is.
    [BLK_SANDSTONE] = {.name       = "sandstone", .drop_item = BLK_SANDSTONE, .drop_min = 1, .drop_max = 1,
                       .kind       = K_CUBE,
                       .mat        = M1(VM_SANDSTONE),
                       .hardness   = 120,
                       .tool       = TOOL_PICK,
                       .tool_level = 1,
                       .flags      = BF_SOLID | BF_OPAQUE, .sound = SND_STONE},

    [BLK_GRAVEL] = {.name     = "gravel", .drop_item = BLK_GRAVEL, .drop_min = 1, .drop_max = 1,
                    .kind     = K_CUBE,
                    .mat      = M1(VM_GRAVEL),
                    .hardness = 18,
                    .tool     = TOOL_SHOVEL,
                    .flags    = BF_SOLID | BF_OPAQUE | BF_GRAVITY, .sound = SND_GRAVEL},

    // Not solid, as in Minecraft: you walk through a sign. It drops
    // nothing, since there is no sign item yet (D-79).
    [BLK_SIGN] = {.name = "sign", .kind = K_SIGN, .mat = M1(VM_PLANKS), .hardness = 40, .tool = TOOL_AXE, .sound = SND_WOOD},

    // --- Farming (step 9) --------------------------------------------
    //
    // Tilled soil. It drops DIRT, not itself: a hoe makes farmland out
    // of ground and a shovel turns it back into what it was, so there is
    // no farmland item and nothing to carry about.
    //
    // Wet and dry are two ids and one difference -- the top texture --
    // and which one a till produces is decided once, by looking for
    // water within four blocks on the same level (D-106). Nothing looks
    // again until somebody tills or plants there.
    [BLK_FARMLAND] = {.name     = "farmland", .drop_item = BLK_DIRT, .drop_min = 1, .drop_max = 1,
                      .kind     = K_CUBE,
                      .mat      = M3(VM_FARMLAND, VM_DIRT, VM_DIRT),
                      .hardness = 20,
                      .tool     = TOOL_SHOVEL,
                      .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_GRAVEL},

    [BLK_FARMLAND_WET] = {.name     = "farmland_wet", .drop_item = BLK_DIRT, .drop_min = 1, .drop_max = 1,
                          .kind     = K_CUBE,
                          .mat      = M3(VM_FARMLAND_WET, VM_DIRT, VM_DIRT),
                          .hardness = 20,
                          .tool     = TOOL_SHOVEL,
                          .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_GRAVEL},

    // The composter: seven planks, no fuel, and a day per unit. Compost
    // out of one slot and WORMS out of the other, which is what ties the
    // farm to the fishing (D-104).
    [BLK_COMPOSTER] = {.name     = "composter", .drop_item = BLK_COMPOSTER, .drop_min = 1, .drop_max = 1,
                       .kind     = K_CUBE,
                       .mat      = M3(VM_COMPOSTER_TOP, VM_COMPOSTER_SIDE, VM_COMPOSTER_SIDE),
                       .hardness = 50,
                       .tool     = TOOL_AXE,
                       .flags    = BF_SOLID | BF_OPAQUE, .sound = SND_WOOD,
                       .flags2   = BF2_USABLE | BF2_RECORD},

    // THE FIVE CROPS. Each is a K_PLANT sprite with its stage in state
    // bits 1..3, and `mat` says two things at once (blocks.h):
    //
    //   mat[VF_TOP]  -- the FIRST of its four stage textures, which the
    //                   mesher adds the stage to;
    //   mat[VF_SIDE] -- the RIPE one, which is what the inventory icon
    //                   and the flat-shaded far meshes use, because a
    //                   sprout is not a picture of wheat.
    //
    // They are not REPLACEABLE: water must not wash a field away, and a
    // block placed against one must not eat it. Breaking one is
    // instant, as picking a plant should be.
    // Wheat: 1-3 grain and 1-2 seeds (the user). The seeds are what
    // matter -- at one seed back a field could never be bigger than the
    // tall grass a player had cut, and bread costs three wheat.
    [BLK_WHEAT_CROP] = {.name     = "wheat_crop", .drop_item = ITEM_WHEAT, .drop_min = 1, .drop_max = 3,
                        .kind     = K_PLANT,
                        .mat      = M3(VM_WHEAT_0, VM_WHEAT_3, VM_WHEAT_0),
                        .hardness = 1,
                        .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_DAY, .seed_item = ITEM_WHEAT_SEEDS,
                        .seed_min = 1, .seed_max = 2,
                        .sound    = SND_SOFT},

    [BLK_POTATO_CROP] = {.name     = "potato_crop", .drop_item = ITEM_POTATO, .drop_min = 1, .drop_max = 3,
                         .kind     = K_PLANT,
                         .mat      = M3(VM_POTATO_0, VM_POTATO_3, VM_POTATO_0),
                         .hardness = 1,
                         .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_TWO_DAYS, .seed_item = ITEM_POTATO,
                         .sound    = SND_SOFT},

    // Tomatoes give NO seeds when picked (the user): the seeds come off
    // the crafting table, one fruit into two. The fruit count is 2-4
    // rather than 1-3 to pay for it, so a tomato plant is worth the
    // extra step rather than punished for it.
    [BLK_TOMATO_CROP] = {.name     = "tomato_crop", .drop_item = ITEM_TOMATO, .drop_min = 2, .drop_max = 4,
                         .kind     = K_PLANT,
                         .mat      = M3(VM_TOMATO_0, VM_TOMATO_3, VM_TOMATO_0),
                         .hardness = 1,
                         .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_DAY, .seed_item = ITEM_TOMATO_SEEDS,
                         .sound    = SND_SOFT},

    [BLK_BEAN_CROP] = {.name     = "bean_crop", .drop_item = ITEM_BEANS, .drop_min = 1, .drop_max = 3,
                       .kind     = K_PLANT,
                       .mat      = M3(VM_BEANS_0, VM_BEANS_3, VM_BEANS_0),
                       .hardness = 1,
                       .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_TWO_DAYS, .seed_item = ITEM_BEANS,
                       .sound    = SND_SOFT},

    // Rice stands IN water, so the cell is both (BF2_WATERLOGGED). It is
    // the only block in the game that is two things at once, and the
    // reason it can be is that its water is always a full source.
    [BLK_RICE_CROP] = {.name     = "rice_crop", .drop_item = ITEM_RICE, .drop_min = 1, .drop_max = 3,
                       .kind     = K_PLANT,
                       .mat      = M3(VM_RICE_0, VM_RICE_3, VM_RICE_0),
                       .hardness = 1,
                       .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_TWO_DAYS, .seed_item = ITEM_RICE,
                       .tall_other = BLK_RICE_TOP,
                       .sound    = SND_SOFT,
                       .flags2   = BF2_WATERLOGGED},

    // THE UPPER HALF. It grows through the same four stages as the half
    // below it -- they are advanced together and stay in step -- but it
    // drops NOTHING: the harvest belongs to the lower half, and a plant
    // that paid out twice for one break would be a plant everybody
    // farmed. It is not waterlogged either: it stands in the air.
    [BLK_RICE_TOP] = {.name     = "rice_top",
                      .kind     = K_PLANT,
                      .mat      = M3(VM_RICE_TOP_0, VM_RICE_TOP_3, VM_RICE_TOP_0),
                      .hardness = 1,
                      .flags    = BF_CROP, .growth_max = 3, .grow_ticks = CROP_TICKS_TWO_DAYS,
                      .tall_other = BLK_RICE_CROP,
                      .sound    = SND_SOFT,
                      .flags2   = BF2_TALL_TOP},
};

// Which kind of record each block keeps. A function rather than a
// column, because blockent.h includes this file's header and a be_kind_t
// in block_def_t would be a cycle. One line per block, and a block
// carrying BF2_RECORD without a line here is caught by worldcheck.
uint8_t block_record_kind(uint8_t id) {
    switch (id) {
        case BLK_FURNACE: return BE_FURNACE;
        case BLK_CHEST: return BE_CHEST;
        case BLK_TRASH: return BE_TRASH;
        case BLK_COMPOSTER: return BE_COMPOST;
        default: return BE_NONE;
    }
}
