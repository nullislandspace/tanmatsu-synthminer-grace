#pragma once
// =====================================================================
//  SynthMiner  --  blocks that remember more than a byte
// ---------------------------------------------------------------------
//  THE THIRD TIER (chunk.h). A cell is a block id and a state byte, and
//  that is enough for most of the world -- which way a sign faces, how
//  grown a crop is, who placed a log. It is nowhere near enough for a
//  furnace, which has to remember three stacks of items, how much fuel
//  is still alight and how far through the current item it is.
//
//  So those blocks get a SIDE RECORD, keyed by where they stand and
//  saved with the chunk they are in (chunk_codec.h,
//  SECTION_BLOCK_ENTITIES -- a section reserved when the format was
//  designed and, until the furnace, never written to).
//
//  A FIXED GLOBAL POOL, NOT A LIST PER CHUNK. The decode runs on the
//  core-1 worker and Part K's contract is that the worker does not
//  allocate; a pool taken once at world open means a chunk arriving
//  never has to. Records find their chunk by their own coordinates, and
//  BE_MAX of them across the resident ring is far more containers than
//  anyone will build within sight of themselves.
//
//  THE RACE, AND THE ONE RULE THAT KEEPS IT OUT. The worker writes this
//  pool while it decodes a chunk (blockent_decode_section) and reads it
//  while it serialises one (blockent_encode_chunk); the main task writes
//  it when a player puts something into a furnace and, since Part Q,
//  when blockent_tick() advances one. Four hands, no lock.
//
//  What used to keep them apart was an accident: a chunk was only saved
//  on eviction, and a furnace was only ever stirred by somebody standing
//  in it. A per-tick pass takes that accident away -- it walks every
//  record in the world, including the ones in a chunk being written on
//  core 1 at the same instant.
//
//  THE RULE THAT REPLACES IT: a record may only be touched from the main
//  task when its own chunk is CS_READY. Not CS_LOADING (the worker is
//  filling it, and its records are being written right now) and not
//  CS_SAVING (the worker is serialising them right now). That is one
//  state test per record and it covers both directions.
//
//  It is sound rather than merely plausible, and the reason is the order
//  the decoder writes a record in: blockent_add() stamps x/y/z and
//  leaves `kind` for last, and read_record() then copies the rest of the
//  body over a record whose coordinates are already final. So a record
//  that is VISIBLE to a walk over the pool always has coordinates a
//  torn read cannot have changed, which is what makes "which chunk is
//  this in" a safe question to ask mid-decode.
//
//  Pure: no engine, no RTOS. The one allocation goes through psram.h,
//  which is the sanctioned seam. tools/worldcheck.c builds it as-is.
// =====================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "items/inventory.h"
#include "world/chunk.h"

// Containers within reach at once. A furnace is 3 slots and a chest 27,
// but a record is one size for all of them -- a pool of mixed-size
// records is a heap, and a heap is the thing this is avoiding.
#define BE_MAX   192
#define BE_SLOTS 27

typedef enum {
    BE_NONE = 0,
    BE_FURNACE,
    BE_CHEST,
    BE_TRASH,
    // The composter (world/blocks.h, game/composter.h): scraps in one
    // slot, compost and worms out of two more, and a day per unit on the
    // same lazy clock the furnace runs on.
    BE_COMPOST,
    // The animals' two machines (game/maker.h). Same record, same lazy
    // clock, different numbers -- a day for the cheese, a minute for
    // the sausage -- so they share a module and a screen.
    BE_CHEESE,
    BE_SAUSAGE,
    // The kitchen stove (game/stove.h). Two slots of its own -- fuel
    // and output -- and its INGREDIENTS are in the chest beside it,
    // which is an ordinary BE_CHEST record found through the facing in
    // the stove's state byte (D-110). So the stove is the first record
    // here that reads another one.
    BE_STOVE,
    // THE SMITHING FORGE (game/forge.h, Part S). The stove's shape --
    // fuel and output of its own, its METAL in the chest beside it,
    // found through the facing both halves carry (D-110) -- with one
    // difference that is the whole point of the machine: it has an
    // explicit Start and makes exactly one item, so it needs a job.
    BE_SMITH,
} be_kind_t;

typedef struct {
    int32_t    x, y, z;
    uint8_t    kind;              // be_kind_t; BE_NONE means the pool slot is free
    inv_slot_t slot[BE_SLOTS];

    // WHEN THIS RECORD WAS LAST BROUGHT UP TO DATE, in world ticks.
    // Everything here is worked out lazily from `now - stamp` when the
    // player opens it (Part C): no furnace ticks, no catch-up pass, and
    // a furnace in a chunk nobody has visited costs nothing at all.
    uint32_t   stamp;

    uint16_t   burn_left;  // furnace and stove: ticks of fuel still alight
    uint16_t   burn_max;   // ... and what it started at, for the flame
    uint16_t   cook;       // furnace and stove: ticks into the thing being made

    // THE FORGE'S JOB, in ticks of work DONE on the item it was told to
    // make: 0 for "nothing committed", 1 the instant Start is pressed,
    // and up to the item's length (24000 iron, 48000 titanium) when it
    // lands.
    //
    // NOT `cook`, for the arithmetic reason D-160 names: `cook` is a
    // uint16_t in RAM and a SIGNED i16 tag on the card, which stops at
    // 32767 and so cannot hold a titanium job's 48000. A day of forge
    // fits `cook`; two days do not, and a field that overflows quietly
    // is the kind of bug this file's save format exists to avoid.
    uint32_t   job;

    // THE STOVE'S RECIPE SELECTOR, as the OUTPUT ITEM of the chosen
    // dish (0 for "nothing chosen yet"). The user's design has a stove
    // pick what it is cooking rather than infer it, because a chest
    // full of a farm's produce fits a dozen recipes at once and
    // guessing which one is wanted is not a thing a machine can do.
    //
    // The forge uses it the same way for the same reason: it is a machine
    // with a selector, and a chest of ingots fits all five of its recipes
    // at once.
    //
    // An item id and NOT a recipe index, for the reason every save in
    // this game keys on names: a recipe inserted in the middle of the
    // table would silently repoint every stove in the world. The save
    // writes the item's name and reads it back the same way
    // (blockent.c), so even renumbering the items is safe.
    uint16_t   pick;
} blockent_t;

// The slots a furnace uses, by name. A chest uses all BE_SLOTS.
#define BE_FURNACE_INPUT  0
#define BE_FURNACE_FUEL   1
#define BE_FURNACE_OUTPUT 2

// And the composter's three. No fuel slot: rotting needs no fire, which
// is the user's own design and one fewer thing to feed.
#define BE_COMPOST_INPUT  0
#define BE_COMPOST_OUT    1
#define BE_COMPOST_WORMS  2

// AND THE MAKERS' FOUR. Two in, because a sausage is pork AND a flower;
// two out, because a pork sausage sometimes leaves a bone behind and a
// bone cannot share a slot with a sausage (the composter's worms are
// the same problem and this is the same answer).
//
// The cheese maker uses IN_A and OUT and leaves the other two empty,
// which costs nothing: a slot with nothing in it is not written to the
// card (blockent.c, write_record).
#define BE_MAKER_IN_A  0
#define BE_MAKER_IN_B  1
#define BE_MAKER_OUT   2
#define BE_MAKER_EXTRA 3

// AND THE STOVE'S TWO. There is no input slot: what goes in is whatever
// is in the chest beside it, which is the whole of the user's idea
// (D-105). A stove is therefore the cheapest record in the pool.
#define BE_STOVE_FUEL 0
#define BE_STOVE_OUT  1

// AND THE FORGE'S THREE. Fuel and output are the stove's two; the third
// is the METAL THE JOB IS MADE OF, which leaves the chest the moment
// Start is pressed and belongs to the job from then on (D-160). That is
// why the forge has a slot of its own instead of reading the chest every
// event the way the stove does: a forge whose chest is robbed or buried
// still finishes what it was told to make.
#define BE_SMITH_FUEL  0
#define BE_SMITH_OUT   1
#define BE_SMITH_METAL 2

// Take the pool (once, at world open) and give it back. Safe to call
// twice either way.
bool blockent_init(void);
void blockent_shutdown(void);

// Everything forgotten, for a world being closed or swapped.
void blockent_clear(void);

// The record at (x, y, z), or NULL. A walk over the pool -- see the
// note in blockent.c for why that is the right shape here.
blockent_t* blockent_at(int32_t x, int32_t y, int32_t z);

// Make one. Returns NULL if the pool is full, which the caller must
// report rather than swallow -- a chest that silently did not become a
// chest is a chest somebody is about to put their things in.
blockent_t* blockent_add(int32_t x, int32_t y, int32_t z, uint8_t kind);

// Forget the one at (x, y, z), if any. What breaking the block does.
void blockent_remove(int32_t x, int32_t y, int32_t z);

// SAY THAT A RECORD HAS CHANGED. Call it after putting anything into a
// furnace or a chest, or after taking anything out.
//
// This exists because of a hole that is easy to miss and expensive to
// find: a chunk is only written to the card when it is marked EDITED,
// and only world_set() marks it -- so smelting, which changes what is
// in a block without changing any block, would be lost on the next
// eviction, and nobody would notice until they walked back to their
// furnace. Nothing else here can mark it, because a record does not
// know it is in a chunk until it is asked.
void blockent_touch(blockent_t const* be);

// Ten minutes of PLAYING at 20 Hz -- the user's call, and NOT the
// in-world clock, where ten minutes is a sixtieth of a 20-minute day
// and would be three seconds.
#define BE_TRASH_TICKS   12000u
#define BE_TRASH_MINUTES (BE_TRASH_TICKS / (20u * 60u))

// Empty a trashcan whose time is up, and stamp it. Returns how many
// stacks went. Does nothing to any other kind of record.
//
// ONE STAMP FOR THE WHOLE BIN, refreshed whenever something goes in
// (blockent_touch does not do this; the caller does). Per-slot stamps
// would be more literal, but "it empties ten minutes after you last
// threw something in" is a rule a player can hold in their head, and
// "every stack has its own clock you cannot see" is not.
//
// Worked out when the bin is OPENED, never on a timer: the same lazy
// clock the furnace runs on (game/furnace.h), and the user's own
// instruction -- "calculated the next time we open it".
int blockent_rot_trash(blockent_t* be, uint32_t now);

// Forget every record in a chunk: what EVICTION does, after the save.
void blockent_drop_chunk(int32_t cx, int32_t cz);

// --- The slow sweep (Part Q) ------------------------------------------
//
// THE SAME TWO DOORS CROPS COME IN THROUGH (world/crops.h, step 9), for
// the machines: a tick pass while the chunk is resident, and one catch-up
// in a go when it comes back. The arithmetic does not move -- every
// *_catch_up() already spends a week's absence in one loop and is
// idempotent, which is the only reason a per-tick pass is safe BESIDE
// the UI's call rather than instead of it.
//
// It also writes what the player SEES. A machine's MACHINE_LIT /
// MACHINE_DONE bits (blocks.h) and a cheese maker's BARREL_* bits are
// mirrored into the cell's state byte here, which is the only way the
// mesher can know that a forge is burning: it reads block bytes on core
// 1 and has never once seen a record. Until this pass existed the
// cheese maker only changed colour when somebody opened it, because the
// UI was the only writer of its state byte.
//
// Once a tick, over at most BE_MAX records, most of which are a `kind`
// test. Free in a world with no machines, in the same way crops_tick()
// is free in a world with no farms.
void blockent_tick(uint32_t now);

// The clock blockent_tick() last saw, so a chunk landing on the worker's
// behalf can be caught up without anything under world/ being handed the
// world metadata (the same reason crops_now() exists, D-52).
uint32_t blockent_now(void);

// A chunk has arrived (main task, from apply()): catch up every record
// standing in it, by however long it was away.
void blockent_chunk_join(chunk_t* c, uint32_t now);

// How many records are live, and how many are in one chunk. For the
// checks and the memory report.
int blockent_count(void);
int blockent_count_in(int32_t cx, int32_t cz);

// --- Saving -----------------------------------------------------------

// Write the records of (cx, cz) as a SECTION_BLOCK_ENTITIES section,
// header included, ready to hand to chunk_encode. Returns the bytes
// written, or 0 if there are none (in which case nothing is written and
// the chunk simply has no such section).
size_t blockent_encode_chunk(int32_t cx, int32_t cz, uint8_t* out, size_t cap);

// Read one back. `data` and `len` are the section's CONTENTS, as
// chunk_decode_ex hands them over. Records whose kind this build does
// not know are skipped, which is the whole point of the tagged format.
void blockent_decode_section(uint8_t const* data, size_t len);
