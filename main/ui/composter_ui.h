#pragma once
// =====================================================================
//  SynthMiner  --  the composter screen
// ---------------------------------------------------------------------
//  Three rows -- Scraps, Compost, Worms -- and the furnace screen's
//  design, because it is the same question asked again (ui/furnace_ui.h):
//
//    * enter on either OUTPUT takes what is there
//    * enter on SCRAPS opens a picker over what you are carrying, showing
//      only what actually rots down
//
//  What it does NOT have is a fuel row, because rotting needs no fire.
//  Nothing is dragged, and the picker can say what a stack would become,
//  which is the part dragging never manages.
// =====================================================================

#include <stdbool.h>

#include "bsp/input.h"
#include "items/inventory.h"
#include "pax_gfx.h"
#include "world/blockent.h"

// Open the composter at (x, y, z). False if there is none there.
bool composter_ui_open(int32_t x, int32_t y, int32_t z);
void composter_ui_close(void);
bool composter_ui_active(void);

void composter_ui_event(bsp_input_event_t const* ev);

// Once a frame. `now` is the world clock, which is the only thing that
// makes a composter work at all (game/composter.h -- it never ticks).
void composter_ui_update(inventory_t* inv, uint32_t now);

void composter_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now);
