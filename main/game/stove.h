#pragma once
// =====================================================================
//  SynthMiner  --  the kitchen stove
// ---------------------------------------------------------------------
//  THE USER THREW OUT MINECRAFT'S ANSWER (2026-09-29, D-105): "In
//  Minecraft, preparing food happens either on the crafting table or in
//  the furnace. That makes absolutely no sense." So food is cooked on a
//  stove, and a stove
//
//    * takes its ingredients OUT OF A CHEST STANDING NEXT TO IT,
//    * has a RECIPE SELECTOR, a FUEL slot and an OUTPUT slot,
//    * and SAYS WHAT IS MISSING when it will not start.
//
//  WHY IT READS A CHEST AND NOT INPUT SLOTS. A dish wants up to four
//  different things (the pizza wants four, the kebab four). Four input
//  slots to fill by hand, per dish, is a chore; a chest you tip the
//  farm's whole produce into and a picker that says "cook this" is not.
//  It is also the one idea in the food chain that is nobody else's.
//
//  AND THE CHEST ARRIVES WITH IT (D-110, the user's refinement). The
//  recipe includes the chest's eight planks, the ITEM PLACES TWO BLOCKS
//  -- chest on the left as the player sees it -- and each half carries
//  the facing that names the other (world/blocks.h, FACE_*). That kills
//  the "which chest?" question by making it unaskable, and it is what
//  lets a row of stoves work: `chest | stove | chest | stove` has
//  adjacencies that are wrong three times in four, and no rule that
//  looks at neighbours can untangle it.
//
//  IT NEVER TICKS, like the furnace, the composter and the two makers
//  before it: all of its work is `now - stamp`, done when somebody
//  opens it, draws it or saves it. A stove in a chunk nobody has
//  visited costs nothing.
//
//  WHAT IT CAN COOK IS THE RECIPE TABLE (items/recipes.h, RS_STOVE).
//  Nothing here knows what a pizza is. A fourteenth dish is a row
//  there and a row in items.c.
//
//  Pure: no engine, no allocation, and it never touches the world --
//  the caller finds the chest and hands the record over, which is also
//  what lets tools/worldcheck.c drive the whole machine on the host.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "items/recipes.h"
#include "world/blockent.h"

// ONE PLAYING MINUTE A DISH, which is the sausage maker's number and
// for the sausage maker's reason (game/maker.h): an in-WORLD minute is
// 16 ticks at a 20-minute day, and 16 ticks is not a machine, it is a
// button. The user settled this on the trashcan and it has held for
// every machine since.
#define STOVE_COOK_TICKS 1200u

// How many dishes this build can cook, and the n'th of them. The
// picker's list, in the recipe table's own order (which is Part A's
// food table, cheapest first).
int             stove_dish_count(void);
recipe_t const* stove_dish_at(int n);

// The dish a stove is set to, or NULL for "nothing chosen yet".
//
// Matched by the OUTPUT ITEM rather than by an index, because that is
// what the record stores and what the save file keys on (blockent.h).
// Two rows making the same dish -- the pizza's real and fake sausage --
// resolve to the first that the chest can actually supply.
recipe_t const* stove_pick(blockent_t const* be, blockent_t const* chest);

// Set what it cooks. `out` is a dish's output item, or 0 to clear it.
void stove_set_pick(blockent_t* be, uint16_t out);

// Run it forward to `now`, then stamp it. `chest` is the record of the
// block the stove's facing points at, or NULL if there is none resident
// -- in which case nothing is cooked and nothing is spent.
//
// INGREDIENTS COME OUT OF THE CHEST as each dish finishes, not up
// front: a stove that had reserved four pizzas' worth of shrimp the
// moment it was switched on would be a chest that eats things.
void stove_catch_up(blockent_t* be, blockent_t* chest, uint32_t now);

// Is it cooking, and how far through the current dish (0..100)?
bool stove_busy(blockent_t const* be, blockent_t const* chest);
int  stove_progress_pct(blockent_t const* be);

// Why it is idle, for the line under the slots. The user asked for "an
// appropriate info message" when a recipe is short or no chest touches
// it, and that matters here for the same reason iron refusing a wooden
// pickaxe needed a line on the HUD: a machine that does nothing and
// says nothing reads as broken.
typedef enum {
    STOVE_IDLE_NONE = 0,   // it is cooking
    STOVE_IDLE_NO_PICK,    // nothing chosen to cook
    STOVE_IDLE_NO_CHEST,   // its chest is gone, or not loaded yet
    STOVE_IDLE_MISSING,    // the chest is short of something: stove_missing says what
    STOVE_IDLE_NO_FUEL,    // nothing to burn
    STOVE_IDLE_FULL,       // the output slot cannot take any more
} stove_idle_t;

stove_idle_t stove_idle_reason(blockent_t const* be, blockent_t const* chest);

// WHAT IT IS SHORT OF, and how many: the first ingredient of the chosen
// dish the chest cannot supply. Returns 0 when nothing is missing (or
// nothing is chosen). `need` is how many more are wanted.
uint16_t stove_missing(blockent_t const* be, blockent_t const* chest, int* need);

// How many of `item` a chest holds, across all of its slots. Exposed
// because the screen counts the same thing for its footer and a second
// copy of the loop would be a second thing to get wrong.
int stove_chest_count(blockent_t const* chest, uint16_t item);
