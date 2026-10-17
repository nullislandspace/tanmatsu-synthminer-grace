#pragma once
// =====================================================================
//  SynthMiner  --  the smithing forge
// ---------------------------------------------------------------------
//  THE ONE MACHINE IN THIS GAME THAT HAS A START AND A STOP (Part S,
//  D-158). Everything else here -- the furnace, the stove, the composter,
//  the two makers -- is `now - stamp` and runs as long as the clock
//  allows: a furnace left for a week smelts as many ingots as it can fit.
//  The forge is told what to make, makes exactly one of it, and stops.
//
//  WHICH IS THE HALF THAT HAS TO BE WRITTEN RATHER THAN REUSED. A job
//  that ends means the record has to remember that a job is running
//  (blockent_t::job, a u32 because a titanium item is 48000 ticks and
//  `cook`'s signed 16-bit tag stops at 32767), and the catch-up has to
//  CLAMP at one item however long the chunk was away.
//
//  THE METAL IS COMMITTED AT START; THE COAL IS NOT (D-160). "Smithing
//  forge should also take the metal resources at the start of a job."
//  The furnace takes its ore out of the input slot when the job FINISHES,
//  so an interrupted furnace still owns its input. The forge spends the
//  ingots the moment Start is pressed and holds them in its own slot,
//  which is why it has a third slot instead of reading the chest every
//  event the way the stove does: a forge whose chest is emptied, robbed,
//  or buried under a collapsed tunnel still finishes what it was told to
//  make. What a broken forge costs is the time, not the metal -- the
//  metal comes back out of the record when it is broken (interact.c).
//
//  A RUNNING FORGE CANNOT BE TOLD TO MAKE SOMETHING ELSE (D-160), because
//  the selector names the thing the committed ingots were spent on:
//  changing it mid-job would mint metal out of nothing.
//
//  A STARVED FORGE WAITS, AND THAT IS THE ONE PLACE IT DISAGREES WITH THE
//  FURNACE (D-160). "If the forge runs out of coal in the middle of the
//  process, it just stops and waits for more." furnace_catch_up() and
//  stove_catch_up() both zero `cook` when they go cold; a forge that runs
//  out of coal mid-chestplate keeps 18000 of its 24000 and finishes when
//  the next coal lands in the slot. The fuel slot stays open while a job
//  runs, because "waits for more coal" means nothing otherwise.
//
//  ITS CHEST IS THE STOVE'S CHEST (D-110): the forge and its chest are
//  two cells that each carry the facing naming the other, and the caller
//  finds the chest and hands both records over.
//
//  Pure: no engine, no allocation, and it never touches the world -- which
//  is also what lets tools/worldcheck.c drive the whole machine on the
//  host.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "game/daytime.h"  // DAY_TICKS
#include "items/recipes.h"
#include "world/blockent.h"

// AN IRON ITEM IS A FULL IN-GAME DAY and a titanium item two of them
// (D-158, D-159): 24000 ticks is 20 playing minutes, so a sword is an
// afternoon and a chestplate is a day and a half. A bed advances at most
// 13000 ticks -- 54% of a day -- so a sleep contributes to a job rather
// than paying for one.
uint32_t forge_job_ticks(uint16_t out);  // 0 for anything the forge cannot make

// What it can forge, and the n'th of it -- the selector's list, in the
// recipe table's own order (iron's five first, then titanium's).
int             forge_item_count(void);
recipe_t const* forge_item_at(int n);

// The item a forge is set to, or NULL for "nothing chosen yet". Matched by
// OUTPUT ITEM rather than by index, for the reason every save in this game
// keys on names (blockent.h).
recipe_t const* forge_pick(blockent_t const* be);

// Set what it forges. FALSE if a job is running, which is D-160's rule and
// not a preference: the selector names the metal that was already spent.
bool forge_set_pick(blockent_t* be, uint16_t out);

// Press Start. Validates the recipe, the chest, and the output slot; takes
// the ingots OUT OF THE CHEST into the forge's own slot; sets the job
// running. False, and nothing is spent, if it cannot start.
bool forge_start(blockent_t* be, blockent_t* chest);

// Run it forward to `now`, then stamp it. Like every machine here it does
// not tick; this IS the forge running. Clamps at ONE item.
//
// It does not want the chest: the metal was committed into the forge's own
// slot when Start was pressed, so a job runs from the record alone.
void forge_catch_up(blockent_t* be, uint32_t now);

// A job is committed and not yet finished. Also the answer to "may the
// selector change?" and the reason the flame is worth drawing.
bool forge_busy(blockent_t const* be);

// How far through the current item, 0..100. 100 for a finished job that is
// waiting for somewhere to put the result.
int forge_progress_pct(blockent_t const* be);

// Why it is standing still, for the line under the slots. A forge has more
// ways to be idle than a stove: nothing chosen, no chest, a chest short of
// metal, nothing to burn, a full output, a job starved mid-way, or simply
// ready and not yet started.
typedef enum {
    FORGE_IDLE_NONE = 0,   // it is forging
    FORGE_IDLE_NO_PICK,    // nothing chosen
    FORGE_IDLE_NO_CHEST,   // its chest is gone, or not loaded yet
    FORGE_IDLE_NO_METAL,   // the chest is short: forge_missing says what of
    FORGE_IDLE_NO_FUEL,    // nothing to burn, and nothing committed
    FORGE_IDLE_FULL,       // the output cannot take the result
    FORGE_IDLE_WAITING,    // a job is committed and starved of fuel (D-160)
    FORGE_IDLE_READY,      // everything is there; Start has not been pressed
} forge_idle_t;

forge_idle_t forge_idle_reason(blockent_t const* be, blockent_t const* chest);

// WHAT THE CHEST IS SHORT OF, and how many: the first ingredient of the
// chosen item the chest cannot supply. 0 with *need == 0 when it has it all.
uint16_t forge_missing(blockent_t const* be, blockent_t const* chest, int* need);

// How many ingots the forge is already holding for the job (0 when idle).
int forge_metal_count(blockent_t const* be);

// How much of `item` its chest holds. Public for the same reason the
// stove's is: the screen shows what the chest has before it says the forge
// cannot start.
int forge_chest_count(blockent_t const* chest, uint16_t item);
