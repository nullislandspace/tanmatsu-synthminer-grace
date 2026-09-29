#pragma once
// =====================================================================
//  SynthMiner  --  hunger, saturation, and what damage is
// ---------------------------------------------------------------------
//  The HUD has drawn ten hearts and ten drumsticks since step 4.3 with
//  nothing moving either of them. This is what moves them.
//
//  IT IS MINECRAFT'S MODEL, which is what the user asked for ("hunger
//  and saturation basically the same way as in Minecraft") and what
//  D-08 committed to on the first day. Three numbers, not two:
//
//    HEALTH      ten hearts; fall damage and starvation take it,
//                and it comes back by itself while you are well fed.
//    HUNGER      ten drumsticks; what the player can see going down.
//    SATURATION  an INVISIBLE reserve, never more than hunger, that is
//                spent first. It is why a good meal lasts longer than
//                its drumsticks say, and it is the whole reason the
//                food table has two columns instead of one.
//
//  And a fourth that is not a number anyone sees: EXHAUSTION, which
//  every step, jump and swing adds a little of. At FOOD_EXHAUST_STEP it
//  empties and takes one point of saturation -- or, when there is no
//  saturation left, one drumstick. So walking does not cost hunger
//  directly; it costs exhaustion, and hunger is what exhaustion is paid
//  out of when the reserve is gone.
//
//  WHAT A FOOD IS WORTH IS A ROW IN THE ITEM TABLE (items.h, `hunger`
//  and `saturation`), every number of it the user's. Nothing here knows
//  what a pizza is.
//
//  EATING IS A TAP, not a key held down for a second and a half. This
//  is a handheld whose Use key is also how you place a block, open a
//  chest and milk a cow; a hold-to-eat would be a fourth meaning for
//  the same button and the only one with a timer on it.
//
//  Pure: no engine, no allocation, no world. tools/worldcheck.c drives
//  the whole loop on the host, which is the only way anyone is going to
//  watch half an hour of walking.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

// Minecraft's units, and what the HUD draws: ten hearts and ten
// drumsticks, two points each.
#define FOOD_HEALTH_MAX 20
#define FOOD_HUNGER_MAX 20

// Exhaustion that buys one point out of the reserve. Minecraft's 4.0.
#define FOOD_EXHAUST_STEP 4.0f

// WHAT EACH THING COSTS, per block moved or per event. Minecraft's
// numbers where it has one, and the two it does not have an equivalent
// for are chosen to sit beside them:
//
//   * there is no sprint in this game, so SWIM is the expensive way to
//     travel and is priced a half above a walk;
//   * a block broken is 0.005, which makes a day's mining about the
//     same as a day's walking.
#define FOOD_EXHAUST_WALK  0.010f  // per block, on foot
#define FOOD_EXHAUST_SWIM  0.015f  // per block, in water
#define FOOD_EXHAUST_JUMP  0.050f  // per jump
#define FOOD_EXHAUST_MINE  0.005f  // per block broken
#define FOOD_EXHAUST_HURT  0.100f  // per point of damage taken
#define FOOD_EXHAUST_HEAL  6.000f  // per heart-half regenerated: healing is EXPENSIVE

// Ticks between one beat of regeneration or starvation and the next.
// Four seconds at 20 Hz, which is Minecraft's.
#define FOOD_BEAT_TICKS 80

// Hunger at or above which health comes back by itself. Nine drumsticks
// of ten: the last one is the warning, and it is also what makes a
// stocked larder worth having rather than merely survivable.
#define FOOD_REGEN_AT 18

typedef struct {
    int   health;      // 0..FOOD_HEALTH_MAX; 0 is dead
    int   hunger;      // 0..FOOD_HUNGER_MAX
    int   saturation;  // 0..hunger: the invisible reserve, spent first
    // Never saved. It is worth at most one drumstick and it would be a
    // float in a save file for the sake of a fifth of a hunger point
    // (world/worldstore.h).
    float exhaustion;
    // Ticks since the last regeneration or starvation beat. Also not
    // saved: reopening a world starts the four seconds again, which
    // nobody can tell from the alternative.
    uint16_t beat;
} food_t;

// A player who has never played: everything full, nothing in progress.
void food_reset(food_t* f);

// What one tick of the loop did, so the caller can make a noise, shake
// the screen or respawn the body. Only one of these can happen in a
// tick -- a beat either heals or hurts.
typedef enum {
    FOOD_NOTHING = 0,
    FOOD_HEALED,    // a heart-half came back
    FOOD_ATE_INTO,  // a drumstick went, because the reserve was empty
    FOOD_STARVED,   // hunger is empty and it took a point of health
    FOOD_DIED,      // ... and that was the last one
} food_event_t;

// One tick. `moved` is how far the body travelled horizontally this
// tick in blocks, `swimming` whether it did so in water and `jumped`
// whether this tick was a take-off. All three are what the caller
// already knows; none of them is measured here, because a pure module
// cannot see a body.
food_event_t food_tick(food_t* f, float moved, bool swimming, bool jumped);

// Add exhaustion directly, for the things that do not happen at the
// moment the tick is settled -- breaking a block, which is decided
// after the body has already moved (game/player.c). It is spent on the
// next tick at the latest, which nobody can tell from this one.
void food_exhaust(food_t* f, float n);

// Take `n` points of damage -- a fall, and later a blow. Returns what
// happened, which is FOOD_DIED when it was the last of the health.
//
// Damage is exhausting, which is Minecraft's rule and the one that
// makes a bad landing cost twice: the hearts, and then the food it
// takes to get them back.
food_event_t food_hurt(food_t* f, int n);

// --- The fall ---------------------------------------------------------
//
// HOW FAR YOU MAY FALL FOR NOTHING. Three blocks, which is Minecraft's,
// and a point of health per block after that. It matters that it is
// three and not two: the player's jump reaches 1.33 blocks
// (game/player.h), so at two a hop down off anything you can hop up
// onto would hurt.
#define FALL_SAFE 3.0f

// WHERE THIS FALL STARTED -- the highest the feet have been since they
// last left the ground.
//
// NOT A VELOCITY, which is the whole reason this is a struct and not an
// expression at the landing. A fall broken by a ledge half way down is
// two short falls and must not be charged as one long one, and a
// velocity cannot tell the difference. A jump off a cliff counts from
// the APEX and not from the lip, which is a third of a block in the
// player's favour and is Minecraft's rule.
typedef struct {
    double from;
    bool   falling;
} fall_t;

// Start (or restart) the tracker at `y`. A body that has just been put
// somewhere is not falling -- without this, respawning after a fatal
// fall lands with the old fall still on the clock and kills the player
// again the moment they touch the ground, which is a death loop and the
// one bug fall damage is most likely to have.
void fall_reset(fall_t* fl, double y);

// One tick, AFTER the body has moved. Returns the damage a landing this
// tick costs, and 0 on every other tick.
//
// WATER IS A LANDING, tested before the height: a dive from build
// height into a lake costs nothing, which is what every player expects
// and what makes a waterfall a way down.
int fall_tick(fall_t* fl, double y, bool on_ground, bool in_water);

// --- Eating -----------------------------------------------------------

// Is `item` food at all? True for anything with a hunger value, which
// is a column in the item table and not a list here.
bool food_is_food(uint16_t item);

// What eating one of `item` leaves in the hand -- an empty pail for a
// bucket of milk, and nothing for everything else. A table of one row
// rather than a case, because a bottle will want the same answer.
uint16_t food_leftover(uint16_t item);

typedef struct {
    bool     ate;       // it went down; take one from the stack
    uint16_t leftover;  // what the hand is left holding, 0 for nothing
    bool     full;      // it refused BECAUSE the player is not hungry
} food_eat_t;

// Eat one of `item`. Refuses when it is not food, and refuses when
// hunger is already full -- which is Minecraft's rule and the one that
// stops a full player eating a whole larder by leaning on the key.
//
// SATURATION IS CAPPED AT HUNGER, which is where the food table's shape
// comes from: smoked salmon's 4 saturation is only worth having to a
// player who has the drumsticks to hold it.
food_eat_t food_eat(food_t* f, uint16_t item);
