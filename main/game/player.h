#pragma once
// =====================================================================
//  SynthMiner  --  the player
// ---------------------------------------------------------------------
//  A body (physics.h), a direction to look, and what the two of them do
//  with a tick's worth of input. Health, hunger and the inventory are
//  block 4; this is walking, jumping, looking, breaking and placing.
//
//  IT TICKS AT 20 Hz AND THE FRAME INTERPOLATES (D-02). player_tick()
//  advances the simulation by exactly one tick from a bitmask;
//  player_eye() blends the last two poses for the frame being drawn.
//  That is what makes the movement the same at 10 fps and at 30, and
//  what makes a recorded bitmask stream replay to the same world.
//
//  SPEEDS ARE PER TICK, not per second, for the same reason: a number
//  that means "per tick" cannot silently acquire a frame-rate
//  dependency later.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "game/fishing.h"
#include "game/food.h"
#include "game/input.h"
#include "game/mob.h"
#include "game/physics.h"
#include "game/raycast.h"
#include "items/inventory.h"

// Blocks per tick. 0.215 is about 4.3 blocks a second, Minecraft's
// walk. Sneaking is a little under a third of it.
#define PL_WALK  0.215f
#define PL_SNEAK 0.065f
#define PL_ACCEL 0.35f  // share of the gap to target speed closed per tick

// SNEAKING IS A TOGGLE (the user), not a key held down. This is a
// handheld: the sneak key is a shoulder button under a thumb that is
// also steering, and the one thing you do while sneaking -- walk
// backwards to a lip and lay a block under your own feet -- takes both
// thumbs and several seconds. Holding a third button through that is
// the sort of thing a keyboard and a mouse let you get away with and a
// badge does not.
//
// The cost of a toggle is forgetting it is on, so it has to SHOW: the
// camera drops, Fred crouches, and the HUD says the word. All three,
// because in first person there is no Fred, and a camera that dropped
// half a second ago is not a state anybody can read.
//
// What it does, both of them Minecraft's (game/physics.h):
//   -- you climb nothing without jumping (step_up = 0)
//   -- you cannot walk off a ledge (edge_stop)
//
// 0.30 blocks of drop, which is four times Minecraft's 0.08. Theirs
// sits under a 1080p monitor a foot from your face; this is a 4.3-inch
// screen at arm's length, upscaled from half resolution, and 0.08 of a
// block is not a movement on it -- it is a pixel and a half.
#define PL_SNEAK_DROP 0.30f   // how far the eye falls, in blocks
#define PL_CROUCH_RATE 0.25f  // of the way per tick: four ticks, a fifth of a second

// And the bound on it: the crouched eye stays in the head rather than
// in the boots. A drop big enough to put the camera below the middle of
// the body puts it inside whatever the player is standing beside.
_Static_assert(PHYS_PLAYER_EYE - PL_SNEAK_DROP > PHYS_PLAYER_H * 0.5f, "the crouch drops the camera too far");

// The jump arc. These three are chosen together, by simulating the arc
// rather than by feel, because what matters is a number you can state:
//
//   apex 1.33 blocks, 0.40 s up, 0.85 s in the air.
//
// The apex has to clear a block WITH ROOM, or you cannot place one
// underneath yourself -- and pillaring up is how you get out of a hole,
// so it is not a trick, it is basic movement. 1.33 leaves a third of a
// block of margin at the top for the placement to happen in.
//
// Slower than Minecraft (0.60 s in the air) on purpose: at 15 fps a
// 0.60 s jump is nine frames from take-off to landing, and judging a
// landing in nine frames is not fair on the player. Same apex, longer
// arc -- which is the pair (v0 * k, g * k^2), here with k = 0.7.
#define PL_GRAVITY  0.04f  // blocks per tick per tick
#define PL_DRAG     0.98f  // per tick, on the vertical
#define PL_JUMP     0.32f  // -> apex 1.33 blocks
#define PL_TERMINAL 3.0f   // blocks a tick: nothing falls faster

// SWIMMING (D-86). Water is not solid, so a player in it falls -- but
// slowly, because buoyancy cancels almost all of gravity, and they can
// push themselves back up. The numbers follow from phys_gravity's
// recurrence `vy = (vy - g) * drag` rather than from feel:
//
//   sinking, hands down:  4 * (0 - 0.008)         = -0.032/tick, 0.6 b/s
//   swimming up:          4 * (0.030 - 0.008)     = +0.088/tick, 1.8 b/s
//   diving:               4 * (-0.030 - 0.008)    = -0.152/tick, 3.0 b/s
//
// (the 4 is drag/(1 - drag) with drag = 0.8.) Jump swims up and sneak
// dives, which is free because the speed in water does not depend on
// sneaking the way it does on land.
#define PL_SWIM       0.11f   // blocks a tick: about half a walk
#define PL_SWIM_UP    0.030f  // added to vy per tick while jump (or sneak) is held
#define PL_WATER_GRAV 0.008f  // what is left of gravity once the water holds you
#define PL_WATER_DRAG 0.80f   // per tick: water kills a fall in well under a second
#define PL_WATER_TERM 0.50f   // blocks a tick: how deep a running jump can plunge you

// Where the body is tested for being in water. Not the feet, which are
// in the water the moment a toe touches it, and not the eye, which is
// out of it while you are still swimming: the middle.
#define PL_WADE_Y 0.9f

// SURVIVAL LIVES IN game/food.h now (step 11), numbers and all: health,
// hunger, the invisible saturation reserve, the exhaustion that spends
// it, and the fall. This file carries the body those rules are applied
// to and measures the two things they need from it -- how far it walked
// and how far it fell.

// How long a refusal stays on screen: about two seconds at 20 Hz.
#define USE_MSG_TICKS 36

typedef struct {
    phys_body_t body;
    float       yaw, pitch;

    // SNEAKING: the toggle, and how far into the crouch the figure and
    // the camera are (0 standing .. 1 down). The blend is advanced in
    // the tick and interpolated for the frame, like the position --
    // which also makes it replay-safe, since the eye the ray is cast
    // from depends on it.
    bool        sneaking;
    float       crouch, prev_crouch;
    inventory_t inv;
    // Health, hunger, saturation and exhaustion (game/food.h). One
    // struct because they are one system: what a fall takes, what a
    // meal gives back and what a walk costs are three views of the
    // same four numbers.
    food_t      food;

    // Where this fall started (game/food.h). A struct rather than two
    // fields, because the rule that reads it is pure and the host check
    // drives the real one rather than a copy of it.
    fall_t      fall;

    // WHAT THE SURVIVAL LOOP DID THIS TICK (food_event_t), and how many
    // ticks are left of saying so. Reported rather than acted on, like
    // used_block: this file has no business knowing what a screen is,
    // and main.c is what respawns a body.
    uint8_t     food_msg;
    uint16_t    food_msg_ticks;

    // Breaking is HELD, not tapped: a block takes item_break_ticks() of
    // them, which is what makes hardness and tool choice mean anything
    // and what the crack overlay will animate. Tracked against the cell
    // being mined, so looking away abandons the progress.
    int32_t mine_x, mine_y, mine_z;
    int     mine_ticks;   // ticks spent on that cell
    int     mine_needed;  // ticks it takes, for the progress bar
    bool    mining;

    // The previous tick's pose, so a frame drawn between two ticks can
    // interpolate instead of stepping.
    double prev_x, prev_y, prev_z;
    float  prev_yaw, prev_pitch;

    bool      in_air_last;  // for a landing sound, later
    ray_hit_t aim;          // what the crosshair found this tick
    bool      aim_valid;

    // The player swung at a block that will not break with what they
    // are holding, and THIS is the tool it wants (an item id, 0 for
    // none). Reported so the HUD can name it -- a swing that does
    // nothing and says nothing is a bug as far as anyone can tell.
    uint16_t  needs_tool;

    // The Use key was pressed on a block that opens something (a
    // crafting table; later a furnace or a chest). The block's id, or
    // BLK_AIR for "nothing was used this tick". REPORTED, not acted on:
    // player.c has no business knowing what a screen is, and main.c
    // already owns every other screen in the game.
    uint8_t   used_block;

    // WHY THE LAST USE DID NOTHING (use_msg_t, game/interact.h), and how
    // many ticks are left of saying so. A hoe swung at stone, a seed
    // offered to dry soil and compost put on ripe wheat all look exactly
    // like a key that did not register, and the difference has to be on
    // screen: the same argument as needs_tool above.
    //
    // Counted in TICKS rather than held against a clock, because this
    // file is pure -- and a message that lasted a fixed number of ticks
    // is also one a replay reproduces.
    uint8_t   use_msg;
    uint16_t  use_msg_ticks;

    // THE CREATURE UNDER THE CROSSHAIR this tick, or -1 (game/mob.h).
    // Reported rather than acted on, like used_block: the HUD names it
    // so a player knows what they are about to hit, and a use goes to
    // it before it goes to the world.
    int       hit_mob;

    // ... and what the last use of one DID, with how many ticks are
    // left of saying so. Milking, feeding and taming all look like
    // nothing happening unless the screen says otherwise -- the same
    // argument as use_msg above.
    uint8_t   mob_msg;
    uint16_t  mob_msg_ticks;

    // THE LINE, if it is in the water (game/fishing.h). Not saved: a
    // world reopened has the rod in hand and nothing in the river,
    // which is the only state a player could not tell apart anyway.
    fishing_t fish;
    // What the last use of the rod did, and how long is left of saying
    // so -- the same shape as use_msg, and for the same reason: a cast
    // that went nowhere and a key that did not register look identical.
    uint8_t   fish_msg;
    uint16_t  fish_msg_ticks;
    // ... and WHAT came up, so the line can name it.
    uint16_t  fish_caught;
    // A tick counter of its own, because player_tick is not told the
    // world's clock -- and it must not reach for one: a count of ticks
    // taken is exactly as reproducible in a replay as the world's is,
    // and it is what seeds where the fish are (game/fishing.h).
    uint32_t  fish_clock;

    // A full-screen UI is up -- the crafting book (ui/craft_ui.h).
    // Mirrored here once a frame by main.c rather than reached for,
    // because player.c has no business knowing what a pax_buf_t is.
    // The effect is the inventory screen's: stand still, keep falling.
    bool      ui_open;
} player_t;

// How far through breaking the aimed block, 0..1. Zero when not mining.
float player_mine_progress(player_t const* p);

// A player who has never played: full health and hunger, the starting
// kit, nothing in progress. Position is left alone -- that is
// player_spawn's or player_place's job.
void player_reset(player_t* p);

// Put the player at (x, z), standing on whatever is there. Only moves
// them: health, hunger and the inventory are untouched.
void player_spawn(player_t* p, double x, double z, float yaw);

// Put the player back EXACTLY where they were -- a cave, a ledge, a
// tower they built. Returns false, and moves nothing, if the body would
// not fit there (a chunk that changed, a save from a build with
// different terrain); the caller then stands them on the ground with
// player_spawn instead.
bool player_place(player_t* p, double x, double y, double z, float yaw, float pitch);

// Advance one simulation tick. `mask` is the tick's input, `pressed`
// the actions that went down since the last tick (so a held key places
// one block, not twenty).
void player_tick(player_t* p, sm_actions_t mask, sm_actions_t pressed);


// The eye for the frame being drawn. `alpha` is how far through the
// current tick it is, 0..1. The crouch is IN the y it returns.
void player_eye(player_t const* p, float alpha, double* x, double* y, double* z, float* yaw, float* pitch);

// How far into the crouch, 0..1, for the frame being drawn. Anyone who
// needs the FEET back out of player_eye's y wants
// `y - PHYS_PLAYER_EYE + PL_SNEAK_DROP * player_crouch(...)`.
float player_crouch(player_t const* p, float alpha);
