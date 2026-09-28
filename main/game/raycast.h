#pragma once
// =====================================================================
//  SynthMiner  --  what the crosshair is pointing at
// ---------------------------------------------------------------------
//  A ray walked through the block grid, cell by cell, in the order it
//  actually crosses them (Amanatides and Woo): step to whichever axis
//  boundary is nearest, test that cell, repeat. No sampling, so a
//  diagonal ray cannot slip through the corner between two blocks and
//  no step size has to be chosen.
//
//  It reports the FACE it came in through as well as the block, because
//  every use needs it: placing puts the new block against that face,
//  breaking draws the crack overlay on it, and a torch or a stair will
//  want to know which way it is being attached.
//
//  Pure: no engine, no allocation. tools/worldcheck.c checks it against
//  a brute-force march.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

// How far the player can reach, in blocks. Minecraft's survival reach.
#define RAY_REACH 4.5f

typedef struct {
    int32_t x, y, z;     // the block struck
    int32_t px, py, pz;  // the empty cell in front of its face: where a placement goes
    uint8_t block;       // what was struck
    uint8_t face;        // MESH_DIR_* of the face entered through
    float   dist;        // along the ray, in blocks
} ray_hit_t;

// WHAT COUNTS AS A HIT. Three questions, three answers, and they are
// genuinely different questions rather than one with a tolerance.
typedef enum {
    // Anything you can POINT AT: every block but air and liquids. The
    // player's crosshair. A torch or a flower is not solid, and a ray
    // that ignored them made a placed torch impossible to take back
    // (F-56); water is looked through, so it never hides the riverbed.
    RAY_PICKABLE = 0,
    // Anything that STOPS THE PLAYER (BF_SOLID). What the chase camera
    // asks, so it does not end up inside a wall.
    RAY_SOLID,
    // Pickable, PLUS liquids. The bucket, and only the bucket: it is
    // the one thing whose whole purpose is the water the crosshair is
    // deliberately blind to. Without it there is no way to aim at a
    // pond at all, and a bucket that fills from whatever is behind the
    // lake is worse than one that does not fill.
    RAY_FLUID,
} ray_mode_t;

// Walk from (ox, oy, oz) along (dx, dy, dz) -- which need not be
// normalised -- for at most `max` blocks. True when it strikes
// something `mode` counts as a hit.
bool ray_pick(double ox, double oy, double oz, float dx, float dy, float dz, float max, ray_mode_t mode,
              ray_hit_t* out);

// The direction a yaw/pitch pair looks along, in the engine's
// convention (se_scene.c, camera_build_basis):
//     forward = ( sin yaw cos pitch, -sin pitch, cos yaw cos pitch )
// POSITIVE PITCH LOOKS DOWN. Kept here so that the picker, the camera
// and the player's movement cannot drift apart on the sign.
void ray_forward(float yaw, float pitch, float* dx, float* dy, float* dz);
