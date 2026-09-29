#pragma once
// =====================================================================
//  SynthMiner  --  the kitchen stove's screen
// ---------------------------------------------------------------------
//  The furnace's screen with the input row replaced by a RECIPE
//  SELECTOR, which is the whole difference between the two machines
//  (game/stove.h): a furnace is told what to smelt by what you put in
//  it, and a stove is told what to cook and finds the ingredients
//  itself, in the chest it came with.
//
//  THREE ROWS. The dish, the fuel, and what is ready. Enter on the dish
//  opens a list of everything this build can cook -- the crafting
//  book's shape, not a new widget -- with what it takes and what it is
//  worth under the cursor. Enter on the fuel opens the furnace's own
//  picker over what the player is carrying. Enter on the output takes
//  it, and never asks how many (the user's rule: there is no reason to
//  leave half a dish in the pan).
//
//  AND THE FOOTER IS THE POINT. The user asked for "an appropriate info
//  message" when a recipe is short or no chest touches it, and a stove
//  that sits there doing nothing has more ways to be wrong than any
//  other machine here: no dish chosen, no chest, no fuel, a full pan,
//  or two shrimp short. It says which.
// =====================================================================

#include <stdbool.h>

#include "bsp/input.h"
#include "items/inventory.h"
#include "pax_gfx.h"

// Open the stove at (x, y, z). False if there is no stove there.
bool stove_ui_open(int32_t x, int32_t y, int32_t z);
void stove_ui_close(void);
bool stove_ui_active(void);

void stove_ui_event(bsp_input_event_t const* ev);

// Once a frame. `now` is the world clock: the stove never ticks, so
// this is it running (game/stove.h).
void stove_ui_update(inventory_t* inv, uint32_t now);
void stove_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now);
