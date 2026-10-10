#pragma once
// =====================================================================
//  SynthMiner  --  crossing between dimensions (see claudeplans/synthminer.md, Part M)
// ---------------------------------------------------------------------
//  A teleport is the only place in the game where the whole store
//  changes identity at once, so it is a new kind of event rather than a
//  bigger version of an existing one. The order is fixed (D-141):
//
//    1. save every dirty chunk in the current dimension;
//    2. drain the worker -- no jobs pending, no results pending;
//    3. release every chunk through chunk_release(), the single path
//       F-132 established, or the entity and block-entity pools never
//       come back;
//    4. free the mesh cache -- meshes belong to a dimension;
//    5. load the 3x3 around the arrival point synchronously;
//    6. record it in the flight recorder.
//
//  STEP 5 IS NOT HERE, and that is not an omission. The entry sequence
//  (Part K) belongs to main.c: physics frozen, those nine chunks as the
//  gate, the rest streaming nearest-first. This file does the part that
//  can be checked on a host, which is everything up to the point where
//  the store is empty and the new dimension is the current one.
//
//  IT REFUSES RATHER THAN CLEARS. A chunk that could not reach the card
//  is something the player made, and a switch that dropped it would be
//  data loss -- the same rule Part P already applies to eviction, the
//  pause menu's Save and quit to launcher. Refusing is recoverable: the
//  player walks a little further, or the card comes back, and the
//  crossing can be tried again. Clearing is not.
//
//  Pure: no engine, no RTOS, no allocation.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "world/dimension.h"

// Steps 1-4 and 6. On success the store is empty and `to` is the current
// dimension; the caller then loads the 3x3 (step 5) and moves the player.
//
// False means nothing was changed: no chunk was written, none was
// released, and the dimension is still `from`.
bool dimension_switch(int32_t x, int32_t z, dimension_t to);
