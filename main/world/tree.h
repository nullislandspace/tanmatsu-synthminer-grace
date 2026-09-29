#pragma once
// =====================================================================
//  SynthMiner  --  a sapling, and the tree it becomes
// ---------------------------------------------------------------------
//  The user, 2026-09-29: *"Logs (trees) need to be renewable resources.
//  So cutting trees should drop one or two seedlings that can be
//  planted to grow into a new tree."*
//
//  Which closes the last hole in the economy. Stone, ore, crops, wool,
//  meat and milk all come back; WOOD did not, and there is no recipe
//  anywhere that makes a log. A player who cleared the forest round
//  their house had cleared it for good.
//
//  A SAPLING IS A CROP (world/crops.h). It has stages in its state
//  byte, it grows on the chunk's own slow clock, it catches up when its
//  chunk comes back after an hour away, and compost pushes it on a
//  stage -- all of that already existed and none of it had to learn
//  what a sapling is. The ONE thing that is different is what happens
//  when it reaches its last stage: a wheat plant stops and waits to be
//  cut, and this turns into a tree.
//
//  ONE SAPLING PER SPECIES. A birch wood that grew back as oak is not
//  the wood anybody planted, so the table below is the whole of the
//  difference between them -- which log, which leaf, which sapling a
//  felled trunk drops.
//
//  THE TREE IS THE GENERATOR'S OWN SHAPE (worldgen.h,
//  worldgen_tree_shape), written through world_set instead of stamped
//  into a chunk. So a planted tree is the same tree the world would
//  have grown on that spot, and there is one canopy loop in the
//  codebase rather than two that agree until somebody edits one.
//
//  Pure: no engine, no allocation. tools/worldcheck.c grows one.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

// How long a sapling takes: one in-game day, which is wheat's -- three
// stages of CROP_TICKS_DAY (world/crops.h). Twenty minutes of playing
// is already a long time to look at a twig, and wood is wanted in
// quantity rather than in ones.
//
// It is not a constant here: it is `grow_ticks` in the block table,
// like every other growing thing. Said out loud only because "how long
// does a tree take" is the first question anybody asks.

// WHICH TREE A SAPLING GROWS, and which sapling a log drops. Both
// directions of one two-row table.
//
// tree_sapling_for() returns BLK_AIR for anything that is not a trunk,
// and tree_is_sapling() is the other end of the same question -- there
// is no block flag for it, because a flag would only say THAT a block
// is a sapling and this says WHICH, which is what every caller wants.
uint8_t tree_sapling_for(uint8_t log_block);
bool    tree_is_sapling(uint8_t block);

// Grow the sapling at (x, y, z) into a tree, through world_set. False
// if there is no sapling there, or if the tree would not fit.
//
// `seed` is the world's, so the tree that comes up is the one that spot
// would have grown on its own -- same height, same clipped canopy
// corners. A replay therefore reproduces a planted forest (Part T).
bool tree_grow(int32_t x, int32_t y, int32_t z, uint32_t seed);

// HOW MANY SEEDLINGS A FELLED TREE LEAVES: one or two, from the world's
// own hash and never rand(), like the composter's worms and the sausage
// maker's bone. Exposed because the host check would otherwise be
// testing its own copy of the rule.
#define TREE_DROP_MIN 1
#define TREE_DROP_MAX 2

int tree_drops_at(int32_t x, int32_t y, int32_t z);
