#pragma once
// =====================================================================
//  SynthMiner  --  Fred
// ---------------------------------------------------------------------
//  The player's figure: seen whole in third person, and as his right arm
//  and whatever it holds in first person. Posed as a pure function of a
//  fred_pose_t, so nothing is kept between frames; the meshes are
//  fred_mesh.h's.
//
//  Ported from tanmatsu-showreel-grace, main/craftminer/assets/miner.h,
//  where he was "the miner" and only ever held a pickaxe or a block.
//  Here he holds what the player's hotbar holds -- any of three tools in
//  two materials, a block in its own textures, or an item -- and he is
//  lit by the cell he stands in, so he goes dark at night and warm by a
//  torch like everything else. Changes are SynthMiner's; the showreel
//  stays the origin to diff against.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "math/xform.h"

typedef enum {
    FRED_HOLD_NONE = 0,
    FRED_HOLD_TOOL,   // tool + level
    FRED_HOLD_BLOCK,  // block, textured
    FRED_HOLD_SPRITE, // a flower, tall grass: crossed flat quads, as the world draws them
    FRED_HOLD_TORCH,  // a torch: the thin stick the world draws
    FRED_HOLD_ITEM,   // a small cube in the item's colour: coal, a stick
    FRED_HOLD_BUCKET, // a pail, with `argb` for whatever is in it
    FRED_HOLD_ROD,    // the fishing rod: a long taper with a line off the end
    FRED_HOLD_BONE,   // a bone, knobs and all
    FRED_HOLD_BED,    // a bed under one arm, rather than the red cube a block gets
} fred_hold_kind_t;

typedef struct {
    fred_hold_kind_t kind;
    uint8_t          tool;   // TOOL_PICK / TOOL_AXE / TOOL_SHOVEL (world/blocks.h)
    uint8_t          level;  // 1 wood, 2 stone, 3 iron: the head's colour
    uint8_t          block;
    uint32_t         argb;   // FRED_HOLD_ITEM, and the contents of a FRED_HOLD_BUCKET
} fred_hold_t;

typedef struct {
    float       walk;        // walk cycle, radians: legs and arms swing with sin(walk)
    float       stride;      // 0 standing still .. 1 walking
    float       swing;       // the tool arm: 0 at rest .. 1 raised (a stroke runs 1 -> 0)
    float       head_yaw;    // radians, + to his left
    float       head_pitch;  // radians, + looking down (the camera's convention)
    fred_hold_t hold;
    bool        left_handed;  // which hand holds the tool (a Graphics setting)
    float       crouch;       // 0 standing .. 1 sneaking (game/player.h)
} fred_pose_t;

// THE CROUCH, and it is the torso that does it, not the legs.
//
// His legs are one rigid piece hanging from the hip: lower the hips and
// his boots go through the floor. So the LEGS STAY PLANTED and the
// torso -- with the arms, what they hold and the head on it -- drops
// and tips forward over them. That is Minecraft's cheat too, and it
// works for the same reason: the torso mesh is wide enough at the waist
// that the join never shows.
//
// The two numbers are one number. The camera drops PL_SNEAK_DROP (0.30
// blocks) and his head has to arrive in the same place, or third person
// and first person disagree about where the player is looking from.
// Leaning by FRED_CROUCH_LEAN already drops a head sitting FRED_BODY_H
// above the pivot by BODY_H * (1 - cos lean) = 0.085; the rest is the
// slide, and the lot is divided by FRED_SCALE to get model units:
//
//   0.30 / 0.9 = 0.333 down in all -> 0.333 - 0.085 = 0.248 of slide.
#define FRED_CROUCH_LEAN 0.5f   // radians the torso tips forward
#define FRED_CROUCH_DROP 0.25f  // model units the torso slides down

// Build the meshes and look up the materials. After chunk_render_init()
// (a block in his hand borrows the world's textures).
void fred_init(void);
void fred_shutdown(void);

// What he holds when the player holds `item` (items/items.h).
fred_hold_t fred_hold_for(uint16_t item);

// Fred standing at `root`: its origin between his feet on the ground, +z
// the way he faces. `light` is the light byte of the cell he stands in
// (world/light.h).
#define FRED_SCALE  0.9f
#define FRED_HEIGHT (2.04f * FRED_SCALE)
void fred_submit(xform_t const* root, fred_pose_t const* pose, uint8_t light);

// The swing of a stroke at `phase` (0..1 through one stroke): up
// quickly, down hard, a short rest. For fred_pose_t.swing.
float fred_stroke(float phase);

// First person: his right arm and what it holds, at the lower right of
// the current camera's view. `swing` as in the pose; `bob` lifts it
// (walking). Call after the camera is set.
void fred_submit_fp_arm(float swing, fred_hold_t const* hold, float bob, uint8_t light, bool left_handed);
