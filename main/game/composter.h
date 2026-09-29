#pragma once
// =====================================================================
//  SynthMiner  --  the composter
// ---------------------------------------------------------------------
//  The user's replacement for bone meal, and it does two jobs with one
//  box (D-104): scraps in one slot, COMPOST out of a second and WORMS
//  out of a third. Compost pushes a crop on by a stage; worms are the
//  bait fishing will need, which is what ties the farm to the water.
//
//  IT NEVER TICKS, like the furnace it is modelled on (game/furnace.h):
//  one unit per in-game day, worked out from `now - stamp` whenever
//  somebody opens it. So a composter in a chunk nobody has visited costs
//  nothing at all, and a box left for a week is right when it is opened.
//
//  NO FUEL. Rotting needs no fire, which is the user's design and one
//  fewer thing to feed than the furnace.
//
//  WHAT ROTS is a column in the item table (items.h, `compost`), not a
//  list here: leaves, flowers, seeds, crops and food, as asked. A new
//  plant brings its own answer with it.
//
//  0 TO 2 WORMS PER UNIT, from the world's own hash and never rand():
//  the same world opened twice has to compost the same way, and a replay
//  has to reproduce it (Part T). The hash takes the box's position and
//  WHICH unit this is, so a composter does not hand out the same number
//  for ever.
//
//  Pure: no engine, no allocation. tools/worldcheck.c drives it.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/blockent.h"

// One in-game day per unit, which is the user's number. DAY_TICKS
// (game/daytime.h) is 24000 -- twenty minutes of playing.
#define COMPOST_TICKS 24000u

// The most worms one unit can give. Zero is a possible answer and has to
// be: a box that always paid out would make worms as free as scraps.
#define COMPOST_WORMS_MAX 2

// Does `item` rot down? Reads the item table.
bool composter_accepts(uint16_t item);

// Run the box forward to `now`, then stamp it. Called when it is opened,
// when it is drawn and when it is saved -- anywhere the contents have to
// be true rather than merely stored. Twice in a row is free.
void composter_catch_up(blockent_t* be, uint32_t now);

// How far through the current unit it is, 0..100, and whether it is
// doing anything at all.
int  composter_progress_pct(blockent_t const* be, uint32_t now);
bool composter_busy(blockent_t const* be);

// Why it is idle, for the line under the slots.
typedef enum {
    COMPOST_IDLE_NONE = 0,   // it is working
    COMPOST_IDLE_NO_INPUT,   // nothing in it
    COMPOST_IDLE_FULL,       // the compost slot cannot take any more
} composter_idle_t;

composter_idle_t composter_idle_reason(blockent_t const* be);

// How many worms one unit yields at (x, y, z), for unit number `n`.
// Exposed for the host check: the distribution matters and a test that
// re-implemented it would be testing itself.
int composter_worms_for(int32_t x, int32_t y, int32_t z, uint32_t n);
