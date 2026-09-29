#pragma once
// =====================================================================
//  SynthMiner  --  fishing
// ---------------------------------------------------------------------
//  The user's design (Part A, D-109), and the two things in it that are
//  not Minecraft's:
//
//    * BAIT IS WORMS, held in the inventory, and it is ONE WORM PER
//      CAST -- not per catch. A cast that brings nothing up still costs
//      one. That is what makes the composter matter: worms are the
//      throttle on fishing, and a rod of three sticks is not;
//    * THE ROD DOES NOT WEAR OUT. Fishing is already paid for in worms,
//      and charging twice for one activity is how a system stops being
//      worth using.
//
//  The catch is a sardine, a salmon or a shrimp.
//
//  TWO TAPS, NOT ONE. Use the rod at water and the float goes out; wait
//  for the bite and use it again to strike. A cast that lands a fish
//  with no second press would be a button that dispenses food, and the
//  waiting is the only thing fishing has ever been.
//
//  A MISSED BITE IS NOT A LOST WORM. The line stays out and another
//  fish comes along, because the worm buys the cast and the player has
//  already paid for it. Reeling in early forfeits it, which is the
//  player's own choice.
//
//  DETERMINISTIC (Part T): how long the wait is, when the bite comes
//  and which fish it is all come from the hash of where the float
//  landed and the tick it landed on -- never rand(), so a replay fishes
//  the same river the same way.
//
//  Pure: no engine, no allocation. tools/worldcheck.c drives it.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "items/inventory.h"

// How long a bite takes to come, and how long it lasts. The wait is
// 4 to 14 seconds at 20 Hz, which is close to Minecraft's and is long
// enough to be a wait without being a chore; the strike window is a
// second and a half, which is generous on a handheld where the Use key
// is under a thumb.
#define FISH_WAIT_MIN   80u
#define FISH_WAIT_SPAN  200u
#define FISH_BITE_TICKS 30u

typedef struct {
    bool     out;        // the line is in the water
    int32_t  x, y, z;    // the cell the float is sitting in
    uint32_t cast_at;    // the tick it went out, and the seed of everything after
    uint32_t elapsed;    // ticks since the cast
    uint32_t next_bite;  // `elapsed` at which the current bite started, or 0
    uint32_t bite_left;  // ticks left of the strike window, 0 when nothing is biting
    uint32_t bites;      // how many have come and gone on this cast
} fishing_t;

// What a use of the rod did, so the caller can make the noise and say
// so. The caller applies everything: this file touches no inventory.
typedef enum {
    FISH_NOTHING = 0,
    FISH_CAST,       // the line went out; one worm is gone
    FISH_NO_WORM,    // nothing to bait it with
    FISH_NO_WATER,   // not pointed at water within reach
    FISH_CAUGHT,     // struck at the right moment: `item` is the fish
    FISH_TOO_SOON,   // struck with nothing on the line; the line stays out
    FISH_REELED,     // reeled in with nothing biting, by choice
} fish_result_t;

typedef struct {
    uint8_t  what;  // fish_result_t
    uint16_t item;  // FISH_CAUGHT: what came up
} fish_use_t;

// Use the rod. `wx, wy, wz` is the cell the player's ray found water in
// (or a negative y for "no water"), `have_worm` whether they are
// carrying one, and `now` the world tick.
fish_use_t fishing_use(fishing_t* f, int32_t wx, int32_t wy, int32_t wz, bool have_worm, uint32_t now);

// One tick with the line out: advances the wait, opens and closes the
// bite window. Returns true on the tick a bite STARTS, which is what
// the caller turns into a splash.
bool fishing_tick(fishing_t* f);

// Reel in unconditionally -- what leaving the world, dying or putting
// the rod away does.
void fishing_reset(fishing_t* f);

// Is something biting right now? For the float's dip and the HUD.
static inline bool fishing_biting(fishing_t const* f) {
    return f->out && f->bite_left > 0;
}

// WHICH FISH, for a cast at (x, y, z) on tick `seed`, `n` bites in.
// Exposed because the host check would otherwise be testing its own
// copy of the table.
uint16_t fishing_catch_for(int32_t x, int32_t y, int32_t z, uint32_t seed, uint32_t n);
