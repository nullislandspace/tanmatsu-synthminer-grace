#pragma once
// =====================================================================
//  SynthMiner  --  the cheese maker's and sausage maker's screen
// ---------------------------------------------------------------------
//  ONE SCREEN FOR BOTH, because they are one machine with different
//  numbers (game/maker.h). It is the composter's screen again --
//  enter on an output takes what is there, enter on an ingredient row
//  opens a picker over what you are carrying -- with two differences
//  that are the machines' own:
//
//    * TWO INGREDIENT ROWS, since a sausage is pork AND a flower. The
//      cheese maker uses one and its second row is simply not shown.
//    * A SECOND OUTPUT, for the bone that rarely comes out with a pork
//      sausage. Same reason the composter has one for worms: two kinds
//      of thing cannot share a slot.
//
//  The bucket that comes back when milk goes in is handed over HERE,
//  because this is the only place that knows whose hands it goes into.
// =====================================================================

#include <stdbool.h>

#include "bsp/input.h"
#include "items/inventory.h"
#include "pax_gfx.h"
#include "world/blockent.h"

// Open the machine at (x, y, z). False if there is no maker there.
bool maker_ui_open(int32_t x, int32_t y, int32_t z);
void maker_ui_close(void);
bool maker_ui_active(void);

void maker_ui_event(bsp_input_event_t const* ev);

// Once a frame. `now` is the world clock: the machine never ticks, so
// this is it running (game/maker.h).
void maker_ui_update(inventory_t* inv, uint32_t now);
void maker_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now);
