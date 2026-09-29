// =====================================================================
//  SynthMiner  --  the recipe table (see recipes.h)
// ---------------------------------------------------------------------
//  Quantities are Minecraft's, as the user asked. Where SynthMiner
//  invents something -- the disassembly bench, the trashcan -- the row
//  says so.
// =====================================================================

#include "items/recipes.h"

#include <string.h>

static recipe_t const RECIPES[] = {
    // --- Anywhere, from the Tab screen ------------------------------
    //
    // The user's short list: what a player must be able to make with
    // nothing but their hands, so that finding wood is enough to get
    // started. Everything else waits for a crafting table.

    {.out     = BLK_PLANKS, .out_n = 4, .station = RS_INVENTORY,
     .n_in    = 1,
     .in      = {{BLK_LOG, 1}}},

    {.out     = ITEM_STICK, .out_n = 4, .station = RS_INVENTORY,
     .n_in    = 1,
     .in      = {{BLK_PLANKS, 2}}},

    {.out     = BLK_TORCH, .out_n = 4, .station = RS_INVENTORY,
     .n_in    = 2,
     .in      = {{ITEM_COAL, 1}, {ITEM_STICK, 1}}},

    // None of the three is RF_REVERSIBLE: planks and sticks are what
    // the user called basic resources, and a torch that came back as
    // coal and a stick would be a way of un-burning it.

    // The table itself is made without one, or there would be no way
    // to ever have one.
    {.out     = BLK_CRAFTING_TABLE, .out_n = 1, .station = RS_INVENTORY, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{BLK_PLANKS, 4}}},

    // --- At a crafting table ----------------------------------------
    //
    // Minecraft's quantities, and Minecraft's shapes are what made a
    // pickaxe and an axe different recipes there. Here they are simply
    // two rows with the same ingredients, which is allowed and which is
    // why the "no two collide" rule went away with the grid (Part C).

    {.out     = ITEM_PICK_WOOD, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_AXE_WOOD, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_SHOVEL_WOOD, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 1}, {ITEM_STICK, 2}}},

    {.out     = ITEM_PICK_STONE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_COBBLE, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_AXE_STONE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_COBBLE, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_SHOVEL_STONE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_COBBLE, 1}, {ITEM_STICK, 2}}},

    {.out     = BLK_FURNACE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{BLK_COBBLE, 8}}},

    // Containers. The "+ one coal" on the last two is the user's
    // design: the destructive ones cost a little fire to build.
    {.out     = BLK_CHEST, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{BLK_PLANKS, 8}}},

    {.out     = BLK_TRASH, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 8}, {ITEM_COAL, 1}}},

    {.out     = BLK_BENCH, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 4}, {ITEM_COAL, 1}}},

    // Iron tools. Ingots come out of the furnace, which is why these
    // could not exist before it did.
    {.out     = ITEM_PICK_IRON, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_IRON_INGOT, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_AXE_IRON, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_IRON_INGOT, 3}, {ITEM_STICK, 2}}},
    {.out     = ITEM_SHOVEL_IRON, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_IRON_INGOT, 1}, {ITEM_STICK, 2}}},

    // The bucket: three ingots, as asked, and Minecraft's cost. It is
    // REVERSIBLE like every other iron thing -- the bench gets the
    // metal back. Only the EMPTY one has a recipe; a full one is an
    // empty one that has been somewhere (game/interact.h).
    {.out     = ITEM_BUCKET, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{ITEM_IRON_INGOT, 3}}},

    // --- Farming (step 9) --------------------------------------------
    //
    // THE HOE, at 2 sticks plus 2 material (the user). Every tool in the
    // game is 2 sticks plus its material -- 1 for a shovel, 3 for a
    // pickaxe or an axe -- so a hoe at 2 sits between them and moved
    // nothing. It is also Minecraft's own price (D-106).
    {.out     = ITEM_HOE_WOOD, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 2}, {ITEM_STICK, 2}}},
    {.out     = ITEM_HOE_STONE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_COBBLE, 2}, {ITEM_STICK, 2}}},
    {.out     = ITEM_HOE_IRON, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_IRON_INGOT, 2}, {ITEM_STICK, 2}}},

    // THE COMPOSTER: seven planks (the user), which is the same price as
    // the cheese maker will be. Two rows with identical ingredients are
    // fine here and would not be in Minecraft -- a recipe is a multiset
    // and the player picks the row out of the book, which the wooden
    // pickaxe and the wooden axe have been proving since step 8.2
    // (D-104). Nothing needs distorting to tell them apart.
    {.out     = BLK_COMPOSTER, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{BLK_PLANKS, 7}}},

    // A tomato into seeds, as the user asked: "must be turned into
    // tomato seeds in the crafting bench". Two of them, so one fruit is
    // two plants -- enough that a single tomato found in a birch wood
    // grows into a crop rather than being a thing you ate once.
    //
    // NOT reversible: the bench would turn two seeds back into a whole
    // tomato, which is the kind of loop the RF_REVERSIBLE bit exists to
    // keep out (the user's own rule about ingots and ore).
    {.out     = ITEM_TOMATO_SEEDS, .out_n = 2, .station = RS_TABLE,
     .n_in    = 1,
     .in      = {{ITEM_TOMATO, 1}}},

    // --- In a furnace -----------------------------------------------
    //
    // One input, and the fuel is NOT an ingredient -- it has a slot of
    // its own (game/furnace.h), so these rows name only what goes in
    // and what comes out.
    //
    // Wood becomes COAL, not charcoal: the user's simplification, and
    // it means one fewer item that burns exactly like another one.
    {.out     = ITEM_COAL, .out_n = 1, .station = RS_FURNACE,
     .n_in    = 1,
     .in      = {{BLK_LOG, 1}}},

    // Sand to glass, and cobblestone back to stone. Neither was asked
    // for, both are Minecraft's, and each unlocks a block that had NO
    // way of being obtained at all: glass drops nothing when broken,
    // and stone drops cobblestone. Without these the furnace's only job
    // would be turning wood into coal you could have dug up.
    {.out     = BLK_GLASS, .out_n = 1, .station = RS_FURNACE,
     .n_in    = 1,
     .in      = {{BLK_SAND, 1}}},

    {.out     = BLK_STONE, .out_n = 1, .station = RS_FURNACE,
     .n_in    = 1,
     .in      = {{BLK_COBBLE, 1}}},

    // NOT reversible: an ingot does not go back to ore, which is the
    // user's own example of what the bench must refuse.
    {.out     = ITEM_IRON_INGOT, .out_n = 1, .station = RS_FURNACE,
     .n_in    = 1,
     .in      = {{BLK_IRON_ORE, 1}}},

    // --- The animals' kit (step 10) ----------------------------------
    //
    // The fence and its gate, both of planks and sticks, and both worth
    // more than the wood in them because what they buy is an animal
    // that stays where it was put.
    {.out     = BLK_FENCE, .out_n = 3, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 2}, {ITEM_STICK, 4}}},

    {.out     = BLK_FENCE_GATE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{BLK_PLANKS, 2}, {ITEM_STICK, 4}}},

    // SEVEN PLANKS, the same as the composter (Part A): two rows with
    // the same ingredients are legal here, because the player picks the
    // row and not the pile.
    {.out     = BLK_CHEESE_MAKER, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{BLK_PLANKS, 7}}},

    // Nine iron INGOTS, not ore (the user's correction, 2026-09-29).
    {.out     = BLK_SAUSAGE_MAKER, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{ITEM_IRON_INGOT, 9}}},

    // --- ... and what the two of them make ---------------------------
    //
    // A bucket of milk becomes cheese, and THE BUCKET COMES STRAIGHT
    // BACK (the user) -- which is not in this row, because it happens
    // when the milk goes in rather than when the cheese comes out
    // (game/maker.c). A recipe that returned the bucket at the end
    // would leave it locked up for a whole in-game day.
    {.out     = ITEM_CHEESE, .out_n = 1, .station = RS_CHEESE,
     .n_in    = 1,
     .in      = {{ITEM_BUCKET_MILK, 1}}},

    // "1 pork + 1 flower of any colour" is TWO ROWS, one per colour.
    // There is no "any of these" in a multiset and there should not be:
    // a third flower is a row, and a row is the unit of change here.
    {.out     = ITEM_SAUSAGE, .out_n = 1, .station = RS_SAUSAGE,
     .n_in    = 2,
     .in      = {{ITEM_PORK, 1}, {BLK_FLOWER_RED, 1}}},

    {.out     = ITEM_SAUSAGE, .out_n = 1, .station = RS_SAUSAGE,
     .n_in    = 2,
     .in      = {{ITEM_PORK, 1}, {BLK_FLOWER_YELLOW, 1}}},

    // The vegetarian one, worth EXACTLY what the pork one is worth and
    // counting as a pizza's sausage (the user). Two beans, no flower.
    {.out     = ITEM_SAUSAGE_VEG, .out_n = 1, .station = RS_SAUSAGE,
     .n_in    = 1,
     .in      = {{ITEM_BEANS, 2}}},

    // --- The fishing rod ----------------------------------------------
    //
    // THREE STICKS AND TWO STRING (the user, 2026-09-29), which closes
    // the last open question of the whole food chain. It was a
    // placeholder of three sticks from the day fishing was designed,
    // held open because the user had decided only what string would
    // NOT come from; string came off a sheep the same afternoon
    // (D-126) and this followed.
    //
    // What it means for the game is that a rod now costs a flock: two
    // string is two thirds of a fleece, and a fleece wants shears,
    // which want iron. Fishing is no longer the thing you can do on
    // day one -- which is right, because the composter that feeds it
    // is not either.
    {.out     = ITEM_ROD, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_STICK, 3}, {ITEM_STRING, 2}}},

    // --- Sheep, wool, and what wool is for ---------------------------
    //
    // SHEARS ARE TWO IRON INGOTS (the user). They are the only way to
    // get wool off a sheep without killing it, which is what makes a
    // flock worth keeping rather than eating.
    {.out     = ITEM_SHEARS, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 1,
     .in      = {{ITEM_IRON_INGOT, 2}}},

    // THE BED: three wool and three planks, and it sets the spawn.
    {.out     = BLK_BED_FOOT, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 2,
     .in      = {{ITEM_WOOL, 3}, {BLK_PLANKS, 3}}},

    // AND WHERE STRING COMES FROM, which has been an open question
    // since the fishing rod was designed (Part A: the user, "strings
    // will not come from spiders"). One fleece is three strings.
    //
    // NOT reversible: three strings must not become a fleece, or wool
    // and string would be the same thing at different rates.
    {.out     = ITEM_STRING, .out_n = 3, .station = RS_TABLE,
     .n_in    = 1,
     .in      = {{ITEM_WOOL, 1}}},

    // --- The kitchen stove, and its chest (step 11, D-105 and D-110) --
    //
    // ONE ITEM THAT PUTS DOWN TWO BLOCKS. The price is the user's -- 3
    // iron, 6 stone, 8 planks -- and the 8 planks in it ARE the chest,
    // which is why there is no second recipe and no second thing to
    // carry: "which chest?" is a question this recipe makes unaskable
    // (interact.c places the pair).
    {.out     = BLK_STOVE, .out_n = 1, .station = RS_TABLE, .flags = RF_REVERSIBLE,
     .n_in    = 3,
     .in      = {{ITEM_IRON_INGOT, 3}, {BLK_STONE, 6}, {BLK_PLANKS, 8}}},

    // --- The thirteen dishes (step 11) --------------------------------
    //
    // EVERY ONE OF THESE IS THE USER'S, numbers and all (Part A's food
    // table). A dish is a row here and a row in items.c and nothing
    // else: the stove reads its ingredients out of the chest beside it
    // and has no idea what a pizza is.
    //
    // The order is the table's, which is also roughly cheapest first --
    // and order matters for exactly one pair, the pizza's two rows
    // below, where first match wins (game/stove.c).
    {.out     = ITEM_BAKED_POTATO, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_POTATO, 1}}},

    // ONE FRUIT IN, ONE DISH OUT (the user). This file first guessed
    // two, on the reasoning that a raw tomato at 1 hunger would be
    // pointless beside a cooked one at 2 for the same fruit. That
    // reasoning was backwards: doubling what a tomato is worth is what
    // cooking is FOR, and the raw one is the option you have with no
    // stove and no fuel.
    {.out     = ITEM_GRILLED_TOMATOES, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_TOMATO, 1}}},

    // Beans have no raw form at all, so one bean for 2 hunger is the
    // floor the rest of the table stands on.
    {.out     = ITEM_BAKED_BEANS, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_BEANS, 1}}},

    {.out     = ITEM_BREAD, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_WHEAT, 3}}},

    {.out     = ITEM_RICE_PATTY, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_RICE, 2}}},

    // 2 hunger and FOUR saturation, which is more reserve per drumstick
    // than the pizza gives: the thing to carry when travelling.
    {.out     = ITEM_SMOKED_SALMON, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_SALMON, 1}}},

    {.out     = ITEM_GRILLED_SHRIMP, .out_n = 1, .station = RS_STOVE,
     .n_in    = 1,
     .in      = {{ITEM_SHRIMP, 3}}},

    {.out     = ITEM_SASHIMI, .out_n = 1, .station = RS_STOVE,
     .n_in    = 2,
     .in      = {{ITEM_SALMON, 1}, {ITEM_RICE, 1}}},

    {.out     = ITEM_PORK_BEANS, .out_n = 1, .station = RS_STOVE,
     .n_in    = 2,
     .in      = {{ITEM_PORK, 1}, {ITEM_BEANS, 1}}},

    {.out     = ITEM_STEAK_POTATOES, .out_n = 1, .station = RS_STOVE,
     .n_in    = 2,
     .in      = {{ITEM_BEEF, 1}, {ITEM_POTATO, 1}}},

    // The two that arrived with the sheep, and the kebab is the first
    // recipe in the game with four ingredients: a crop, a fruit, a bean
    // and the meat.
    {.out     = ITEM_MUTTON_MASH, .out_n = 1, .station = RS_STOVE,
     .n_in    = 2,
     .in      = {{ITEM_MUTTON, 1}, {ITEM_POTATO, 2}}},

    {.out     = ITEM_KEBAB, .out_n = 1, .station = RS_STOVE,
     .n_in    = 4,
     .in      = {{ITEM_MUTTON, 1}, {ITEM_BEANS, 1}, {ITEM_WHEAT, 2}, {ITEM_TOMATO, 1}}},

    // THE SUPERFOOD, and it is TWO ROWS because "the fake sausage
    // counts as a pizza's sausage" (the user). There is no "either of
    // these" in a multiset and there should not be -- the sausage maker
    // says the same thing about flowers three screens up.
    //
    // It was never going to make the pizza vegetarian: there are two
    // shrimp in it. Refusing would only have been a trap for somebody
    // who had done everything right.
    {.out     = ITEM_PIZZA, .out_n = 1, .station = RS_STOVE,
     .n_in    = 4,
     .in      = {{ITEM_WHEAT, 2}, {ITEM_SAUSAGE, 1}, {ITEM_CHEESE, 1}, {ITEM_SHRIMP, 2}}},

    {.out     = ITEM_PIZZA, .out_n = 1, .station = RS_STOVE,
     .n_in    = 4,
     .in      = {{ITEM_WHEAT, 2}, {ITEM_SAUSAGE_VEG, 1}, {ITEM_CHEESE, 1}, {ITEM_SHRIMP, 2}}},
};

#define RECIPE_N ((int)(sizeof(RECIPES) / sizeof(RECIPES[0])))

int recipe_count(void) {
    return RECIPE_N;
}

recipe_t const* recipe_at(int i) {
    return (i >= 0 && i < RECIPE_N) ? &RECIPES[i] : NULL;
}

bool recipe_known(recipe_t const* r, inventory_t const* inv) {
    if (r == NULL || inv == NULL) return false;
    // ANY one ingredient, not all of them: the book is a hint about
    // what the material in your hand is for.
    for (int i = 0; i < r->n_in; i++) {
        if (inv_seen(inv, r->in[i].item)) return true;
    }
    return false;
}

int recipe_can_make(recipe_t const* r, inventory_t const* inv, int cap) {
    if (r == NULL || inv == NULL || cap <= 0 || r->n_in == 0) return 0;

    int n = cap;
    for (int i = 0; i < r->n_in && n > 0; i++) {
        int const have = inv_count(inv, r->in[i].item);
        int const need = r->in[i].count;
        int const from = need > 0 ? have / need : cap;
        if (from < n) n = from;
    }
    return n < 0 ? 0 : n;
}

int recipe_missing(recipe_t const* r, inventory_t const* inv, int ing) {
    if (r == NULL || inv == NULL || ing < 0 || ing >= r->n_in) return 0;
    int const need = r->in[ing].count;
    int const have = inv_count(inv, r->in[ing].item);
    return have >= need ? 0 : need - have;
}

// The recipe that makes `item` at this station or a lesser one, or
// NULL. Never a furnace: see the header.
static recipe_t const* maker_of(uint16_t item, int station) {
    for (int i = 0; i < RECIPE_N; i++) {
        recipe_t const* r = &RECIPES[i];
        // BY HAND OR AT A TABLE, and nowhere else. Smelting takes fuel
        // and time, and so does every machine after it: none of them is
        // something a menu should start on the player's behalf.
        if (r->out != item || (r->station != RS_INVENTORY && r->station != RS_TABLE)) continue;
        if (recipe_station_allows(station, r->station)) return r;
    }
    return NULL;
}

// Bring the number of `item` carried up to `need`, crafting if it must.
static bool provide(uint16_t item, int need, inventory_t* inv, int station, int depth) {
    if (inv_count(inv, item) >= need) return true;
    if (depth <= 0) return false;

    recipe_t const* r = maker_of(item, station);
    if (r == NULL) return false;

    // Its own ingredients first, then one batch, then look again. The
    // loop terminates because a recipe never makes what it consumes
    // (worldcheck: "a recipe for %s is made of itself") and every batch
    // adds at least one, so the count strictly rises.
    while (inv_count(inv, item) < need) {
        for (int i = 0; i < r->n_in; i++) {
            if (!provide(r->in[i].item, r->in[i].count, inv, station, depth - 1)) return false;
        }
        if (recipe_make(r, inv, 1) < 1) return false;  // no room, or a cycle we cannot see
    }
    return true;
}

int recipe_make_auto(recipe_t const* r, inventory_t* inv, int n, int station) {
    if (r == NULL || inv == NULL || n <= 0) return 0;
    // The recipe itself has to belong here. The book never offers one
    // that does not, but a planner that would make a pickaxe in your
    // bare hands because nobody asked it not to is a planner nobody
    // should trust with the inventory.
    if (!recipe_station_allows(station, r->station)) return 0;

    int made = 0;
    for (; made < n; made++) {
        // The snapshot covers the WHOLE plan, not just the last step: a
        // plan that turns three logs into planks and then finds there
        // are no sticks must give the logs back, not leave the player
        // with planks they did not ask for.
        inventory_t const before = *inv;

        // MORE THAN ONE PASS, and this is the whole subtlety of the
        // thing. Ingredients are provided one after another, and a
        // later one can EAT what an earlier one just made: asked for a
        // wooden pickaxe from two logs, the planner turns a log into
        // four planks, then turns two of those planks into sticks --
        // and the three planks it had a moment ago are now two.
        //
        // Nothing here reserves. It simply asks again: each pass
        // re-provides whatever is short, and the loop ends when the
        // recipe can actually be made, when a pass achieves nothing,
        // or when something cannot be provided at all.
        bool ok = false;
        for (int pass = 0; pass < RECIPE_AUTO_DEPTH && !ok; pass++) {
            inventory_t const snap = *inv;
            bool              all  = true;
            for (int i = 0; i < r->n_in && all; i++) {
                all = provide(r->in[i].item, r->in[i].count, inv, station, RECIPE_AUTO_DEPTH);
            }
            if (!all) break;
            if (recipe_can_make(r, inv, 1) >= 1) ok = true;
            else if (memcmp(&snap, inv, sizeof(snap)) == 0) break;  // no progress; it will not converge
        }
        if (!ok || recipe_make(r, inv, 1) < 1) {
            *inv = before;
            break;
        }
    }
    return made;
}

int recipe_can_make_auto(recipe_t const* r, inventory_t const* inv, int station) {
    if (r == NULL || inv == NULL) return 0;
    inventory_t scratch = *inv;  // ask by doing, on a copy: one code path
    return recipe_make_auto(r, &scratch, 1, station);
}

int recipe_make(recipe_t const* r, inventory_t* inv, int n) {
    if (r == NULL || inv == NULL || n <= 0) return 0;

    int made = 0;
    for (; made < n; made++) {
        if (recipe_can_make(r, inv, 1) < 1) break;

        // ALL OR NOTHING PER UNIT. The output may not fit -- a full
        // inventory, or a tool that cannot stack onto the one already
        // held -- and crafting must never eat the materials in that
        // case. So the whole inventory is snapshotted before the unit
        // and put back if anything goes wrong. It is 130-odd bytes of
        // struct copy against a class of bug that is very hard to see
        // and impossible to undo.
        inventory_t const before = *inv;

        bool ok = true;
        for (int i = 0; i < r->n_in && ok; i++) {
            ok = inv_take(inv, r->in[i].item, r->in[i].count);
        }
        if (ok && inv_add(inv, r->out, r->out_n, 0) != 0) ok = false;

        if (!ok) {
            *inv = before;
            break;
        }
    }
    return made;
}
