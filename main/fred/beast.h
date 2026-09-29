#pragma once
// =====================================================================
//  SynthMiner  --  the animals' figures
// ---------------------------------------------------------------------
//  Fred's neighbour: the same idea one step simpler. A pig, a cow and a
//  dog are each a BODY, a HEAD and FOUR LEGS, built round their joints
//  so a pose is a rotation -- and since all three are that shape, there
//  is one mesh set with per-kind numbers rather than three models.
//
//  WHAT DIFFERS between them is in the table here: how long the body
//  is, how big the head, what colour each part is, and the one or two
//  pieces that make the animal recognisable -- a snout, a pair of
//  horns, a tail that stands up. That is enough: at the size these are
//  drawn on a 800x480 screen, a silhouette and a colour ARE the animal.
//
//  THE LEGS SWING WITH THE WALK, out of the body's own speed rather
//  than a state bit, so an animal that is standing still has still legs
//  and nothing has to be kept between frames (fred.c's rule).
//
//  Not pure: it submits geometry to the engine, like fred.c.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "game/mob.h"
#include "math/xform.h"

// Build the meshes. After chunk_render_init(), like fred_init().
void beast_init(void);
void beast_shutdown(void);

// One creature, at `root` (between its feet, +z the way it faces).
// `walk` is the walk cycle in radians and `stride` 0..1; `light` is the
// light byte of the cell it stands in.
void beast_submit(xform_t const* root, uint8_t kind, bool baby, bool sitting, float walk, float stride, uint8_t light);
