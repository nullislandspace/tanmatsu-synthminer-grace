#pragma once
// =====================================================================
//  SynthMiner  --  the creatures
// ---------------------------------------------------------------------
//  Pigs, cows and dogs -- and after them, in step 13, the things that
//  fight back. item_entity.h said its shape was "the shape every pig,
//  cow and zombie will reuse" and this is that promise being kept: a
//  fixed pool, a tick over the live ones, no allocation, and a registry
//  row per kind rather than a file per creature.
//
//  WHAT A CREATURE IS is one row in MOBS[] (mob.c): how big it is, how
//  much it can take, what it drops, what it eats and what it says. A
//  zombie is a row plus whatever "wants to reach the player" turns out
//  to cost; nothing here is about pigs in particular.
//
//  SAVED WITH THE CHUNK IT STANDS IN (D-33), in the entities section
//  the chunk format has had a number for since step 1.3 and never
//  written a byte into (chunk_codec.h, SECTION_ENTITIES). So a cow left
//  in a field is in that field tomorrow, and a herd in a chunk nobody
//  has visited costs nothing at all -- the records are on the card and
//  not in the pool.
//
//  A CREATURE CANNOT LEAVE THE RESIDENT WORLD. An unloaded chunk is
//  solid (D-14), so a pig walking west stops at the edge of what is
//  loaded exactly as the player does. That is what makes "saved with
//  its chunk" safe: nothing can wander into a chunk that is not there
//  and be lost when the one it came from is written.
//
//  EVERY RANDOM NUMBER IS A HASH (Part T). Which way a pig turns, when
//  it grunts, how much pork it drops and where a herd generates all
//  come from sm_hash3/sm_rand3 over the world tick and the creature's
//  own id -- never rand(), so a replay reproduces a whole farm.
//
//  Pure: no engine, no allocation, no audio. A creature that wants to
//  be heard sets `say` and the caller (main.c) makes the noise, for the
//  same reason interact.c reports what it did instead of playing it.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

#include "game/physics.h"
#include "i18n/strings_gen.h"
#include "items/inventory.h"
#include "world/chunk.h"

// Live creatures across the whole resident ring. A farm is a dozen and
// a wild valley a handful; 48 is room for both at once, and the pool is
// what bounds the cost of the tick.
#define MOB_MAX 48

typedef enum {
    MOB_NONE = 0,
    MOB_PIG,
    MOB_COW,
    MOB_DOG,
    MOB_KIND_COUNT
} mob_kind_t;

// What a creature wants this moment. Deliberately small: the difference
// between an animal and a mob (step 13) is which of these it can be in
// and what puts it there, not a different kind of mind.
typedef enum {
    MOB_STAND = 0,
    MOB_WANDER,  // ambling in a direction it picked
    MOB_FLEE,    // just been hit, running from where the blow came
    MOB_FOLLOW,  // after a player: the food they are holding, or its owner
} mob_intent_t;

// What it wants to say, drained once a tick by the caller. Never played
// here: this file is pure.
typedef enum {
    MOB_SAY_NONE = 0,
    MOB_SAY_IDLE,
    MOB_SAY_HURT,
    MOB_SAY_DIE,
    MOB_SAY_TAMED,
} mob_say_t;

typedef struct {
    char const* name;   // stable id on disk; never translated, never changed
    sm_str_t    label;  // what the player reads
    float       w, h;   // the body box, in blocks
    int16_t     health_max;
    float       speed;       // blocks a tick while ambling
    uint16_t    drop_item;   // what it leaves when it dies (ITEM_NONE for none)
    uint8_t     drop_min, drop_max;
    // WHAT IT EATS, and it is the same key for two locks: offering this
    // to an adult puts it in the mood to breed, and a young one follows
    // whoever is holding it. Two, because a pig takes potatoes or beans.
    uint16_t    feed[2];
    // WHAT TAMES IT, or 0 for an animal that is nobody's. A bone, for a
    // dog -- which is the user's own answer and is why the sausage
    // maker has a second output slot.
    uint16_t    tame_item;
    uint32_t    argb;  // the flat colour it draws as when textures are off
} mob_def_t;

extern mob_def_t const MOBS[MOB_KIND_COUNT];

static inline mob_def_t const* mob_def(uint8_t kind) {
    return &MOBS[kind < MOB_KIND_COUNT ? kind : MOB_NONE];
}

typedef struct {
    bool        alive;
    uint8_t     kind;
    phys_body_t body;
    float       yaw;       // which way it faces, and walks
    int16_t     health;
    uint32_t    id;        // unique while it lives: the seed of its own randomness
    uint8_t     intent;    // mob_intent_t
    uint16_t    intent_for;  // ticks left of it
    float       flee_x, flee_z;  // the direction it is running

    // AGE IN TICKS ELAPSED, never a timestamp (D-51): a calf left in an
    // unloaded chunk does not grow up while nobody is looking, which is
    // the same rule a dropped item's despawn follows.
    uint32_t    age;
    bool        baby;
    uint16_t    love;      // ticks left of being ready to breed
    uint16_t    breed_cd;  // ticks before it can be again

    bool        tame;
    bool        sitting;
    uint8_t     hurt;      // ticks left of the flinch, and of being unhittable
    uint8_t     say;       // mob_say_t, drained by the caller each tick
} mob_t;

// How long a calf takes to grow up: ten minutes of PLAYING at 20 Hz,
// like the trashcan's ten minutes and for the same reason -- it is a
// wait somebody sits through, not a thing the world's clock does.
#define MOB_BABY_TICKS  12000u
// How long being fed lasts, and how long before it counts again.
#define MOB_LOVE_TICKS  600u
#define MOB_BREED_CD    6000u
// How close two of them have to be, and how far a creature will follow
// somebody holding its food.
#define MOB_BREED_RANGE 2.5f
#define MOB_FOLLOW_RANGE 7.0f
// A tamed dog that has been left behind gives up walking and appears
// beside its owner. Minecraft's rule, and the one that stops a dog
// being a thing you lose to a cliff.
#define MOB_DOG_TELEPORT 18.0f
#define MOB_HURT_TICKS  10u

// WHAT A SWING IS WORTH against a creature, by what is in the hand.
// Arithmetic over `tool` and `tool_level` rather than a column in
// items.c: the day a weapon exists it becomes a column with something
// to put in it.
int mob_damage_of(uint16_t item);

void mob_reset(void);
int  mob_live(void);
mob_t const* mob_at(int i);
mob_t*       mob_at_mut(int i);

// Put one into the world. Returns its index, or -1 if the pool is full.
int mob_spawn(uint8_t kind, double x, double y, double z, bool baby);

// One tick of every creature in a resident chunk: falling, ambling,
// fleeing, following, growing up and breeding. `px, py, pz` is where
// the player is standing and `held` what they are holding, which is all
// an animal knows about them.
void mob_tick(uint32_t now, double px, double py, double pz, uint16_t held);

// --- What the player does to them -------------------------------------

// The creature the ray from (ex, ey, ez) along (dx, dy, dz) hits first
// within `reach`, or -1. `dist` gets how far away it was, so the caller
// can prefer whichever of the block and the creature is nearer.
int mob_pick(double ex, double ey, double ez, float dx, float dy, float dz, float reach, float* dist);

// Hit one for `damage`, knocked back from (fx, fz). True if it died --
// in which case its drops are already on the ground.
bool mob_hit(int i, int damage, double fx, double fz);

// What a use on a creature did, so the caller can change the hand and
// say what happened (the shape interact.h's use_result_t has).
typedef enum {
    MOB_USE_NOTHING = 0,
    MOB_USE_MILKED,
    MOB_USE_FED,
    MOB_USE_TAMED,
    MOB_USE_SIT,
    MOB_USE_STAND,
} mob_use_t;

typedef struct {
    uint8_t  what;     // mob_use_t
    uint16_t becomes;  // what the held stack turns into, 0 to leave it
    bool     consume;  // take one from the held stack
} mob_use_result_t;

mob_use_result_t mob_use(int i, uint16_t item);

// --- Where they come from ---------------------------------------------

// Populate a chunk that has just been GENERATED -- never one read back
// from the card, which brings its own animals with it. Deterministic in
// (cx, cz, seed): the same world always puts the same herd in the same
// field, which is what lets a host check count them (the user's
// "generated with the land").
void mob_populate_chunk(int32_t cx, int32_t cz, uint32_t seed);

// --- Saving ------------------------------------------------------------

// The creatures of (cx, cz) as a SECTION_ENTITIES section, header
// included. 0 if there are none.
size_t mob_encode_chunk(int32_t cx, int32_t cz, uint8_t* out, size_t cap);

// Read one back; `data`/`len` are the section's contents. A kind this
// build does not know is skipped, which is what the tagged format is
// for.
void mob_decode_section(uint8_t const* data, size_t len);

// Forget every creature in a chunk: what EVICTION does, after the save.
void mob_drop_chunk(int32_t cx, int32_t cz);

// How many are in one chunk, for the save path and the checks.
int mob_count_in(int32_t cx, int32_t cz);
