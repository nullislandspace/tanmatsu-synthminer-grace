#pragma once
// =====================================================================
//  SynthMiner  --  the smithing forge's screen
// ---------------------------------------------------------------------
//  The stove's screen with one row more, and that row is the whole
//  difference between the two machines (game/forge.h): a stove starts
//  itself and cooks as long as the clock allows, and a forge waits for an
//  explicit "Start crafting" and then makes exactly one item.
//
//  FIVE ROWS. The item, the fuel, the metal, what is finished, and Start.
//  Enter on the item opens the list of everything this build can forge --
//  the crafting book's shape, not a new widget -- with what it costs and
//  how long it takes under the cursor. Enter on the fuel opens the
//  furnace's own picker over what the player is carrying, and the fuel
//  slot stays open WHILE A JOB RUNS, because "waits for more coal" means
//  nothing otherwise (D-160). Enter on the metal says where it came from.
//  Enter on the output takes it. Enter on Start presses the button.
//
//  THE SELECTOR IS READ-ONLY WHILE A JOB RUNS (D-160). The row answers
//  SFX_DENY and the footer says why, exactly as the stove and the two
//  makers answer a refused key.
//
//  AND THE FOOTER IS THE POINT, as it is on the stove: a forge that sits
//  there doing nothing has more ways to be wrong than any other machine
//  here -- nothing chosen, no chest, a chest short of metal, nothing to
//  burn, a full output, a job starved half way, or ready and unstarted.
//  It says which.
// =====================================================================

#include <stdbool.h>

#include "bsp/input.h"
#include "items/inventory.h"
#include "pax_gfx.h"

// Open the forge at (x, y, z). False if there is no forge there.
bool smith_ui_open(int32_t x, int32_t y, int32_t z);
void smith_ui_close(void);
bool smith_ui_active(void);

void smith_ui_event(bsp_input_event_t const* ev);

// Once a frame. `now` is the world clock: the forge never ticks, so this
// is it running (game/forge.h).
void smith_ui_update(inventory_t* inv, uint32_t now);
void smith_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now);
