#pragma once
// =====================================================================
//  SynthMiner  --  the cheese maker and the sausage maker
// ---------------------------------------------------------------------
//  Two of the four machines the user designed on 2026-09-29 (Part A,
//  D-108), and they are the same machine with different numbers:
//
//    cheese maker    7 planks    1 bucket of milk        -> cheese     a day
//    sausage maker   9 ingots    1 pork + 1 flower       -> sausage    a minute
//                                2 beans                 -> the same, vegetarian
//
//  So they share a file, a record and a screen, and what differs is a
//  ROW in MAKERS[] -- which is the same bargain the block, item and
//  recipe tables make (Part L). The stove in the next step is NOT one
//  of these: it reads its ingredients out of a chest beside it and has
//  a recipe selector, which is a different machine and will be its own
//  file.
//
//  WHAT EACH ONE CAN MAKE IS THE RECIPE TABLE (items/recipes.h), under
//  a station of its own. Nothing here knows what cheese is: it matches
//  what is in the input slots against the rows for its station, takes
//  the first that fits and puts the result in the output slot. A third
//  machine is a row here, a station there and some recipes.
//
//  IT NEVER TICKS, like the furnace and the composter before it: all of
//  its work is done from `now - stamp` when somebody opens it, draws it
//  or saves it. A machine in a chunk nobody has visited costs nothing.
//
//  THE BUCKET COMES STRAIGHT BACK (the user). That happens when the
//  milk goes IN, not when the cheese comes out -- the slot keeps the
//  milk bucket as the token of what is standing in the barrel, and the
//  player walks away with the empty pail at once. Returning it at the
//  end would lock a bucket up for a whole in-game day, which is the
//  one thing the user explicitly did not want.
//
//  Pure: no engine, no allocation. tools/worldcheck.c drives it.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "items/recipes.h"
#include "world/blockent.h"

// ONE IN-GAME DAY, and one PLAYING minute.
//
// The day is the composter's number (24000 ticks, twenty minutes at
// 20 Hz). The minute is NOT a sixtieth of that -- an in-world minute is
// 16 ticks, which is not a machine, it is a button -- and the trashcan
// already settled this: its "ten minutes" are ten minutes of playing
// (blockent.h, BE_TRASH_TICKS), on the user's own instruction. So a
// sausage takes a minute of somebody's life, and a cheese takes a day
// of the world's.
#define MAKER_DAY    24000u
#define MAKER_MINUTE 1200u

// How often a pork sausage leaves a bone behind: one in this many.
//
// The user's answer to what tames a dog -- "When feeding the sausage
// maker, a rare drop is a bone" -- so a dog costs a pig, a flower and
// some patience. Six is chosen so a dog is a morning's work rather than
// an accident, and it is the one number here that is a guess.
#define MAKER_BONE_ONE_IN 6u

typedef struct {
    uint8_t  kind;      // be_kind_t: which record this describes
    uint8_t  station;   // recipe_station_t: the rows it can make
    uint32_t ticks;     // per unit
    uint8_t  slots_in;  // 1 or 2: how many input slots the screen shows
    // The rare second output, and what has to have gone in for it: a
    // bone comes off MEAT, so a bean sausage leaves none.
    uint16_t extra;
    uint16_t extra_from;
} maker_def_t;

// The row for a record kind, or NULL if it is not one of these.
maker_def_t const* maker_def(uint8_t kind);

// Does this machine take `item` at all? What the input picker asks --
// true if any recipe for its station names the item.
bool maker_accepts(uint8_t kind, uint16_t item);

// What the machine would give back AT ONCE for putting `item` in: the
// cheese maker's empty bucket, and nothing for anything else.
uint16_t maker_returns(uint8_t kind, uint16_t item);

// The recipe its input slots currently satisfy, or NULL.
recipe_t const* maker_match(blockent_t const* be);

// Run it forward to `now`, then stamp it. Free to call twice.
void maker_catch_up(blockent_t* be, uint32_t now);

// Is it working, and how far through the current unit (0..100)?
bool maker_busy(blockent_t const* be);
int  maker_progress_pct(blockent_t const* be, uint32_t now);

// Why it is idle, for the line under the slots.
typedef enum {
    MAKER_IDLE_NONE = 0,  // it is working
    MAKER_IDLE_NO_INPUT,  // nothing in it, or nothing that goes together
    MAKER_IDLE_FULL,      // the output slot cannot take any more
} maker_idle_t;

maker_idle_t maker_idle_reason(blockent_t const* be);

// Does unit `n` of the machine at (x, y, z) leave a bone? From the
// world's own hash and never rand(), like the composter's worms: the
// same world opened twice has to behave the same way, and a replay has
// to reproduce it (Part T). Exposed because the host check would
// otherwise be testing its own copy of the rule.
bool maker_extra_for(int32_t x, int32_t y, int32_t z, uint32_t n);

// WHAT THE BARREL SHOWS (blocks.h, BARREL_*): milk while it is
// standing, cheese once it is made, bare boards when it is empty. The
// caller writes it into the block's state byte, because this file may
// not touch the world.
uint8_t maker_barrel_state(blockent_t const* be);
