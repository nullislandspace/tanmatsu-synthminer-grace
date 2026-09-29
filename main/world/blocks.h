#pragma once
// =====================================================================
//  SynthMiner  --  the block registry
// ---------------------------------------------------------------------
//  ONE table describes every block: how it meshes, how it looks, how
//  long it takes to break, what it drops, and how it behaves. The mesher
//  (voxel/voxel_mesh.c), the collider, the picker and the interaction
//  code all read it and none of them has a switch over block ids.
//
//  Adding a block is: one row here, one BLK_ id below, one 16x16 PNG
//  (plus its VM_ material and its row in MAT_FILES), one metadata.json
//  line. Nothing else. That is the whole extendability contract
//  (claudeplans/synthminer.md, Part L).
//
//  Pure: no engine, no RTOS, no allocation. tools/worldcheck.c compiles
//  this as-is.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>
#include "voxel/voxel_mesh.h"  // vox_mat_t (VM_*), vox_face_t (VF_*)

// How a block meshes. Drives voxel_mesh.c; see its header for what each
// kind emits.
typedef enum {
    K_AIR = 0,  // nothing
    K_CUBE,     // a full cube; hides the faces of its neighbours
    K_SEE,      // a cube with cut-out texels (leaves, glass)
    K_PLANT,    // two crossed double-sided quads (flowers, crops)
    K_TORCH,    // a thin stick in the middle of the cell
    K_SIGN,     // a post with a board on it, facing east, its text a texture
    // A FENCE: a post in the middle of the cell and a pair of rails
    // towards every neighbour that is worth joining. It stands HALF A
    // BLOCK TALLER than its cell (block_collide_top), which is the
    // whole point of it -- a pig cannot walk over one and neither can
    // a player, and no gate would be needed if it could be jumped.
    K_FENCE,
    // The gate in that fence: two posts and a bar across, or the same
    // swung aside. Which of the two is the BLOCK ID, not a state bit,
    // for exactly the reason farmland is two ids -- an open gate is
    // not solid and a closed one is, and `block_solid` takes an id
    // (D-106's argument, applied again).
    K_GATE,
    // An open square barrel: the cheese maker. A shell of planks with
    // an inner surface whose colour is WHAT IS IN IT, taken from the
    // state byte -- white for milk, yellow for cheese, nothing when it
    // is empty. The user asked for three appearances and this is them,
    // in one block id rather than three.
    K_BARREL,
    // A liquid is ONLY ever its surface. It draws no sides and no
    // bottom, and a top only where there is air above it, and it never
    // hides the faces of its neighbours -- so a lake is a lid over
    // terrain you can see into, rather than a solid box with nothing
    // inside it. The engine has no blending, so this is what
    // "transparent" has to mean here (D-86). See voxel_mesh.c.
    K_LIQUID,
} block_kind_t;

// Behaviour flags.
#define BF_SOLID       (1u << 0)  // stops the player and mobs
#define BF_OPAQUE      (1u << 1)  // blocks light (reserved for step 15)
#define BF_FELLABLE    (1u << 2)  // part of a tree: the felling rule applies (Part F)
#define BF_CROP        (1u << 3)  // grows through state bits 1..3
#define BF_GRAVITY     (1u << 4)  // falls when unsupported (sand, gravel)
#define BF_REPLACEABLE (1u << 5)  // a placement overwrites it (air, water, tall grass)
#define BF_LIQUID      (1u << 6)
// A K_SEE block that shows its faces against ITS OWN id: leaves are a
// canopy you can see into, glass hides glass.
#define BF_SEE_SELF    (1u << 7)

// The Use key OPENS this block instead of placing against it: a
// crafting table, and later a furnace, a chest and the benches. The
// player only reports which block was used -- what a screen is and
// how to show one is main.c's business, not the registry's.
//
// It is a second flag word because the first is full: eight bits,
// eight flags, and BF_SEE_SELF took the last one.
#define BF2_USABLE     (1u << 0)

// The block keeps a side record: three slots and a fire for a furnace,
// twenty-seven for a chest (world/blockent.h). Placing one takes a
// record from the pool and breaking one gives it back, contents first.
#define BF2_RECORD     (1u << 1)

// THE BLOCK WILL NOT BREAK AT ALL without a tool of at least its
// `tool_level`. Minecraft's rule is that it breaks and drops nothing,
// and this game used that until iron arrived -- the user asked for the
// stricter one (2026-09-23), so iron ore refuses the swing outright.
//
// A refusal has to SAY SO, or a swing that does nothing and explains
// nothing reads as a bug -- which is exactly why Minecraft chose the
// other rule. See break_result_t.needs_tool.
#define BF2_TOOL_REQUIRED (1u << 2)

// A TRUNK: cutting this is what starts a fell (Part F). Separate from
// BF_FELLABLE, which means only "tree material" and is what the flood
// fill spreads through -- leaves are fellable and are NOT trunks.
//
// They were one flag until 2026-09-28, and breaking a single leaf
// therefore brought the whole tree down, which is not what anyone
// wants from clearing a canopy. The two ideas had looked like one
// because logs are the only thing that is both.
#define BF2_TRUNK      (1u << 3)

// THE BLOCK STANDS ON THE ONE BELOW IT, and a stack of them falls as a
// unit: take one out and everything of the same kind directly above
// comes down with it. Cactus, and sugar cane when it arrives.
//
// This is SUPPORT, not the felling rule, and the difference matters in
// two places. It does not care who put the block there -- physics has
// no opinion about ST_PLACED -- and it goes straight up one column
// rather than flooding through a shape.
//
// It is also a stand-in. Properly this is a block update: remove a
// block, tell the neighbours, let each decide whether it can still
// stand. Water, falling sand and the torch that step 29 left hanging in
// mid-air all want the same machinery, and when it exists this flag
// becomes one of its rules instead of a special case in interact.c.
#define BF2_STACKED    (1u << 4)

// THE UPPER HALF OF A PLANT THAT STANDS TWO BLOCKS TALL. Rice, and
// nothing else so far (the user, 2026-09-29: "Rice should also be a two
// block tall plant (breaking always breaks both blocks)").
//
// Which half is which matters in three places and `tall_other` names the
// other one, so none of them needs an id written into it: the LOWER half
// is the one that stands in the water, carries the harvest and is what
// planting puts down; the upper half is scenery that comes and goes with
// it. Breaking either takes both, and only the lower one drops.
#define BF2_TALL_TOP    (1u << 7)

// THE CELL IS FULL OF WATER AS WELL AS THIS BLOCK. Rice, and nothing
// else so far: it grows in water one block deep, so the cell has to be
// water for the pond it stands in and rice for the player.
//
// A cell holds ONE block and the state byte's low bits already belong
// to the growth stage, so there is nowhere to write a level -- which is
// exactly why this is safe. Rice only ever stands in water one deep, so
// its water is always a FULL SOURCE (level 0, not falling), and that is
// the one case that needs no state at all. world/fluid.c reads a cell
// like this as a source so a pond does not drain into it, and the
// mesher draws the surface under the sprite (D-107).
#define BF2_WATERLOGGED (1u << 6)

// DRAW ME FROM item_<name>.png IN THE INVENTORY, not from my own side
// texture. Most blocks are cubes and their side texture IS what they
// look like in the hand; a torch is a stick, and its side texture is a
// full square of wood with a glowing band along the top -- "the torch
// image in the inventory looks like a block with a yellow top instead
// of a torch" (the user, 2026-09-28).
//
// Anything drawn as a thin thing rather than as a cube wants this:
// signs and rails when they arrive, and the torch today.
#define BF2_ITEM_ICON  (1u << 5)

// Tool classes. `tool_level` is 0 hand, 1 wood, 2 stone, 3 iron.
typedef enum {
    TOOL_NONE = 0,
    TOOL_PICK,
    TOOL_AXE,
    TOOL_SHOVEL,
    TOOL_SHEARS,
    // The hoe TILLS rather than digs: it is the only tool whose point is
    // what it does to a block rather than how fast it breaks it, so no
    // block names it in `tool` (game/interact.h, interact_use_item).
    TOOL_HOE
} tool_t;

#define HARDNESS_UNBREAKABLE 0xFFFFu
#define ITEM_NONE            0u

// What a block SOUNDS like: walked on, broken, put down. One class per
// material, not one per block, because a footstep on cobble and one on
// coal ore are the same noise. audio/sfx.c turns a class into the three
// sounds; a block that makes none (air, the barrier) is SND_NONE.
//
// The order matters: sfx.h's SFX_STEP_* / SFX_BREAK_* rows run in this
// same order, so the lookup is an addition rather than a switch.
typedef enum {
    SND_NONE = 0,
    SND_SOFT,    // grass, leaves, flowers
    SND_GRAVEL,  // dirt, gravel
    SND_STONE,   // stone, cobble, ore, bedrock
    SND_WOOD,    // log, planks, a sign
    SND_SAND,
    SND_GLASS,
    SND_SPLASH,  // water
    SND_COUNT
} block_sound_t;

typedef struct {
    char const* name;        // stable id; the string a future save format would key on
    uint8_t     kind;        // block_kind_t
    uint8_t     mat[3];      // vox_mat_t for VF_TOP / VF_SIDE / VF_BOTTOM
    uint16_t    hardness;    // ticks to break bare-handed; HARDNESS_UNBREAKABLE never
    uint8_t     tool;        // tool_t that speeds it up
    uint8_t     tool_level;  // minimum level that drops anything at all
    uint16_t    drop_item;   // ITEM_NONE drops nothing
    uint8_t     drop_min, drop_max;
    uint8_t     flags;
    uint8_t     light;       // light emitted, 0..15 (world/light.h)
    uint8_t     growth_max;  // BF_CROP: the highest growth stage

    // BF_CROP: THE ITEM THAT PLANTS THIS, and what an unripe one drops
    // when it is broken -- exactly one of them. ITEM_NONE for anything
    // that is not a crop.
    uint16_t    seed_item;

    // ... AND HOW MANY SEEDS A RIPE ONE GIVES BACK, on top of the
    // harvest. Wheat gives 1-2 and so grows a field; a tomato gives NONE
    // and is worth more fruit instead, because its seeds come off the
    // crafting table (the user, 2026-09-29).
    //
    // A COLUMN RATHER THAN A RULE. This was inferred at first -- a ripe
    // crop gave a seed whenever its seed and its harvest were different
    // items -- which is the sort of cleverness that reads well and
    // cannot express what somebody actually wants. Both are 0 for a crop
    // whose seed IS its harvest (a potato, a bean, a grain of rice).
    uint8_t     seed_min, seed_max;

    // BF_CROP: TICKS PER GROWTH STAGE. A crop has growth_max + 1 stages,
    // so seed to harvest is growth_max of these -- 8000 is an in-game
    // day (DAY_TICKS is 24000 and there are three transitions), 16000 is
    // two days (the user, 2026-09-29).
    //
    // Per crop rather than one constant, because the user wants wheat
    // and tomatoes at a day and potatoes, beans and rice at two: what a
    // crop is worth and how long it takes are the two halves of the same
    // decision and both belong in its row.
    uint16_t    grow_ticks;

    // THE OTHER HALF of a two-block plant, or BLK_AIR. On the lower half
    // this is the block that stands above it; on the upper half (which
    // carries BF2_TALL_TOP) it is the one below. Rice, so far.
    uint8_t     tall_other;
    uint8_t     sound;       // block_sound_t: what it sounds like (audio/sfx.h)
    uint8_t     flags2;      // BF2_*
} block_def_t;

// The block ids -- AND THEY ARE PERMANENT (D-74).
//
// A chunk stores one byte per cell, and every chunk on every card was
// written with these numbers. So once a block has shipped its id NEVER
// changes, its NAME never changes (a world's palette matches blocks by
// name when it opens), and it is never deleted: a block the game stops
// using is RETIRED -- it keeps its row and its number, and nothing else
// ever gets that number. New blocks go at the END, with the next free id,
// wherever they belong in a menu.
//
// tools/ids.txt lists every id and name ever shipped, and `make check`
// fails the build if this table disagrees with it: a renumbered, renamed
// or missing block, or a new one not yet added to the list. Adding a
// block is appending one line there, in the same commit.
//
// 0 is air; 255 is reserved. One byte is 255 blocks for the life of the
// game, retired ones included; past that is a format change (a new major
// version and a one-time upgrade of every world, D-32).
enum {
    BLK_AIR           = 0,
    BLK_GRASS         = 1,
    BLK_DIRT          = 2,
    BLK_STONE         = 3,
    BLK_COBBLE        = 4,
    BLK_SAND          = 5,
    BLK_WATER         = 6,
    BLK_LOG           = 7,
    BLK_PLANKS        = 8,
    BLK_LEAVES        = 9,
    BLK_COAL_ORE      = 10,
    BLK_GLASS         = 11,
    BLK_TORCH         = 12,
    BLK_FLOWER_RED    = 13,
    BLK_FLOWER_YELLOW = 14,
    BLK_TALL_GRASS    = 15,
    // A chunk that is not resident reads as this: solid, unbreakable,
    // never meshed. The player stops at the edge of generated terrain
    // instead of falling through it (D-14). Never stored in a chunk, but
    // it has a number like any other and keeps it.
    BLK_BARRIER = 16,
    BLK_BEDROCK = 17,
    BLK_GRAVEL  = 18,
    // Generated only, for now (D-79): nothing places or writes one, and
    // there is no sign item. Its text follows from where it stands
    // (voxel_mesh.h, voxel_sign_text).
    BLK_SIGN = 19,
    BLK_CRAFTING_TABLE = 20,
    BLK_FURNACE        = 21,
    BLK_IRON_ORE       = 22,
    BLK_CHEST          = 23,
    BLK_TRASH          = 24,
    BLK_BENCH          = 25,
    BLK_BIRCH_LOG      = 26,
    BLK_BIRCH_LEAVES   = 27,
    BLK_CACTUS         = 28,
    BLK_SNOW           = 29,
    BLK_SANDSTONE      = 30,
    // Farming (step 9). Tilled soil is TWO IDS rather than one id with a
    // wet bit, because the difference is a different top texture and
    // the mesher reads materials out of this table -- a state-driven
    // texture would be new machinery for one block. What it costs is an
    // id, and ids are cheap next to a special case (D-106).
    BLK_FARMLAND       = 31,
    BLK_FARMLAND_WET   = 32,
    BLK_COMPOSTER      = 33,
    // The five crops. Their NAMES all end in _crop because the harvest
    // is an item with the plain name -- "wheat" is what you carry, and
    // item_by_name() searches one id space (items.h).
    BLK_WHEAT_CROP     = 34,
    BLK_POTATO_CROP    = 35,
    BLK_TOMATO_CROP    = 36,
    BLK_BEAN_CROP      = 37,
    BLK_RICE_CROP      = 38,
    // The upper half of the rice plant, which stands in the air above
    // the half that stands in the water (BF2_TALL_TOP).
    BLK_RICE_TOP       = 39,
    // Animals (step 10). Two machines, and the fence that keeps what
    // they are made of from wandering off.
    BLK_CHEESE_MAKER   = 40,
    BLK_SAUSAGE_MAKER  = 41,
    BLK_FENCE          = 42,
    // A GATE IS TWO IDS, open and closed, and the state byte says only
    // which way it lies (GATE_AXIS_X / GATE_AXIS_Z). Both drop the
    // closed one, so a player never ends up carrying "an open gate".
    BLK_FENCE_GATE     = 43,
    BLK_FENCE_GATE_OPEN = 44,
    // New blocks here: BLK_SOMETHING = 45, and a line in tools/ids.txt.
    BLK_COUNT
};

extern block_def_t const BLOCKS[BLK_COUNT];

// Safe accessor: an id past the table reads as barrier, so a corrupt
// save can never index out of bounds.
static inline block_def_t const* block_def(uint8_t id) {
    return &BLOCKS[id < BLK_COUNT ? id : BLK_BARRIER];
}

static inline uint8_t block_kind(uint8_t id) {
    return block_def(id)->kind;
}
static inline uint8_t block_sound(uint8_t id) {
    return block_def(id)->sound;
}
static inline bool block_solid(uint8_t id) {
    return (block_def(id)->flags & BF_SOLID) != 0;
}
static inline bool block_liquid(uint8_t id) {
    return (block_def(id)->flags & BF_LIQUID) != 0;
}
static inline bool block_replaceable(uint8_t id) {
    return (block_def(id)->flags & BF_REPLACEABLE) != 0;
}
// Tree material: what a fell SPREADS through (logs and leaves).
static inline bool block_fellable(uint8_t id) {
    return (block_def(id)->flags & BF_FELLABLE) != 0;
}
// A trunk: what STARTS a fell. Breaking anything else fellable -- a
// leaf -- is an ordinary single break.
static inline bool block_trunk(uint8_t id) {
    return (block_def(id)->flags2 & BF2_TRUNK) != 0;
}
// Needs the block below it; a stack comes down together (BF2_STACKED).
static inline bool block_stacked(uint8_t id) {
    return (block_def(id)->flags2 & BF2_STACKED) != 0;
}
// Has a drawn inventory icon of its own (BF2_ITEM_ICON).
static inline bool block_has_item_icon(uint8_t id) {
    return (block_def(id)->flags2 & BF2_ITEM_ICON) != 0;
}
// Half of a two-block plant, and which half.
static inline uint8_t block_tall_other(uint8_t id) {
    return block_def(id)->tall_other;
}
static inline bool block_tall_top(uint8_t id) {
    return (block_def(id)->flags2 & BF2_TALL_TOP) != 0;
}

// A crop: grows through its stages and is planted rather than placed.
static inline bool block_crop(uint8_t id) {
    return (block_def(id)->flags & BF_CROP) != 0;
}
// The cell is water as well as this block (BF2_WATERLOGGED): rice.
static inline bool block_waterlogged(uint8_t id) {
    return (block_def(id)->flags2 & BF2_WATERLOGGED) != 0;
}

// HOW HIGH THE BLOCK REACHES, for the collider only. One cell for
// everything in the world except a fence, which reaches a cell and a
// half -- and that half is the whole point of a fence: a player jumps
// 1.33 blocks (player.h) and an animal's step-up is one, so 1.5 is the
// first height neither of them can get over. Without it a fence is a
// decoration and the gate is pointless.
//
// A function rather than a column because it has one exception in it.
// The day a second shape needs one -- a slab, a step -- it becomes a
// column, with the rows that earn it.
#define BLOCK_FENCE_TOP 1.5f
static inline float block_collide_top(uint8_t id) {
    return block_kind(id) == K_FENCE ? BLOCK_FENCE_TOP : 1.0f;
}

// WHICH WAY A GATE LIES, in its state byte: along x, or along z. The
// mesher and the placement both read it, so like the torch's data
// (voxel_mesh.h) the names live where the shape does -- but a gate is
// the block table's own idea and has no second reader, so they are
// here.
#define GATE_AXIS_X 0u
#define GATE_AXIS_Z 1u

// WHAT IS IN A CHEESE MAKER, in its state byte: the three appearances
// the user asked for. The machine (game/maker.h) writes it whenever
// its contents change and the mesher draws whatever it says.
#define BARREL_EMPTY  0u
#define BARREL_MILK   1u
#define BARREL_CHEESE 2u

static inline bool block_usable(uint8_t id) {
    return (block_def(id)->flags2 & BF2_USABLE) != 0;
}

static inline bool block_keeps_record(uint8_t id) {
    return (block_def(id)->flags2 & BF2_RECORD) != 0;
}

static inline bool block_tool_required(uint8_t id) {
    return (block_def(id)->flags2 & BF2_TOOL_REQUIRED) != 0;
}

// The be_kind_t a block's record is. Not in the table: blockent.h
// includes this header, so the number cannot be named here without a
// cycle -- and there are few enough of these for a line each.
uint8_t block_record_kind(uint8_t id);
