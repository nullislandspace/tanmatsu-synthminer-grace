#pragma once
// =====================================================================
//  SynthMiner  --  optional external VFD status display
// ---------------------------------------------------------------------
//  Some Tanmatsu units carry a NE-HCS12SS59T-R1 I2C VFD (12-character
//  ASCII) jumpered to address 0x13 (the default 0x10 collides with an
//  on-board device on the internal bus). It is looked for on the CATT
//  port I2C bus first, then on the internal I2C bus -- the same probe as
//  the tanmatsu-vfdclock app and synthracer's VFD, which this is lifted
//  from.
//
//  WHAT IT SHOWS, for the two things this game is doing:
//
//    VFD_SHOW_TITLE   not in a world -- the title screen, the loading and
//                     saving screens, and any menu: "SynthMiner"
//    VFD_SHOW_ITEM    in a world: what is in the player's hand, blank for
//                     an empty hotbar slot
//
//  All I2C traffic runs in a small background task on core 1, so the
//  frame loop never waits on the bus: the game only publishes what the
//  display should show -- one aligned word, so no lock is needed -- and
//  the task writes the display when that changes. Without a VFD the task
//  exits after the probe and every call here is a cheap no-op.
//
//  THE TUBE SPEAKS ASCII and this game speaks thirty-two languages, so a
//  label the tube cannot draw is put through the same fold the crafting
//  book's search box uses (i18n/fold.h): "Кирка" reads as "kirka" rather
//  than as garbage. A name longer than the 12 visible characters is
//  scrolled by the display itself.
// =====================================================================


typedef enum {
    VFD_SHOW_TITLE = 0,
    VFD_SHOW_ITEM,
} vfd_show_t;

// The label an empty hand has. Published as-is; the packing below turns it
// into the value the task reads back as "nothing to show".
#define VFD_NO_ITEM (-1)

// Start the VFD task: probe both buses, and if the display is found
// enable it, set a safe filament current and show the title. Call once
// from on_init. Non-blocking.
void vfd_init(void);

// Publish what the display should show. `label` is an item label string
// (item_label(), items.h) and is used only by VFD_SHOW_ITEM; pass
// VFD_NO_ITEM for an empty hand. Cheap; call every frame.
void vfd_show(vfd_show_t show, int label);

// Blank and switch off the display, then stop the task. Blocks until the
// task is done (bounded), so call it right before returning to the
// launcher. A no-op when no VFD was found.
void vfd_shutdown(void);
