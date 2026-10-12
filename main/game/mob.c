// =====================================================================
//  SynthMiner  --  the creatures (see mob.h)
// =====================================================================

#include "game/mob.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common/rng.h"
#include "common/tags.h"
#include "items/item_entity.h"
#include "items/items.h"
#include "world/chunk_codec.h"
#include "world/deepmine.h"
#include "world/dimension.h"
#include "world/worldgen.h"

// --- The registry ------------------------------------------------------
//
// Three rows, and the third is the one that shows the shape is right: a
// dog differs from a pig in what it eats, what tames it and how fast it
// is, and in nothing else.
mob_def_t const MOBS[MOB_KIND_COUNT] = {
    [MOB_NONE] = {.name = "none", .label = SM_STR_MOB_PIG, .w = 0.5f, .h = 0.5f},

    // A pig: pork, and it will follow a potato or a handful of beans
    // anywhere. Its feed is what it would otherwise be competing with
    // the player for, which is the point of the choice.
    [MOB_PIG] = {.name       = "pig",
                 .label      = SM_STR_MOB_PIG,
                 .w          = 0.9f,
                 .h          = 0.9f,
                 .health_max = 10,
                 .speed      = 0.055f,
                 .drop_item  = ITEM_PORK,
                 .drop_min   = 1,
                 .drop_max   = 3,
                 .feed       = {ITEM_POTATO, ITEM_BEANS},
                 .argb       = 0xFFE8A0A4u},

    // A cow: beef, and MILK to anybody who walks up with a bucket --
    // which needed no new idea at all, because a full bucket has been
    // its own item id since D-100.
    [MOB_COW] = {.name       = "cow",
                 .label      = SM_STR_MOB_COW,
                 .w          = 0.9f,
                 .h          = 1.4f,
                 .health_max = 10,
                 .speed      = 0.045f,
                 .drop_item  = ITEM_BEEF,
                 .drop_min   = 1,
                 .drop_max   = 3,
                 .feed       = {ITEM_WHEAT, 0},
                 .argb       = 0xFF6A5040u},

    // A dog: found wild in the woods, tamed with a BONE (the user), and
    // then it is yours -- it follows, it sits, and it comes back to you
    // when it is left too far behind. It drops nothing: a dog is not
    // livestock, and killing one should pay exactly nothing.
    [MOB_DOG] = {.name       = "dog",
                 .label      = SM_STR_MOB_DOG,
                 .w          = 0.6f,
                 .h          = 0.85f,
                 .health_max = 8,
                 .speed      = 0.095f,
                 .drop_item  = ITEM_NONE,
                 .feed       = {ITEM_BEEF, 0},
                 .tame_item  = ITEM_BONE,
                 .argb       = 0xFFB8B0A4u},

    // A SHEEP: mutton when it dies, wool whenever you ask, and the
    // second is the point. It eats wheat like a cow -- which is not a
    // clash with D-119 ("each animal its own food"): the rule is that
    // an animal HAS a food, not that no two share one, and wheat is
    // what a sheep eats.
    [MOB_SHEEP] = {.name       = "sheep",
                   .label      = SM_STR_MOB_SHEEP,
                   .w          = 0.8f,
                   .h          = 1.1f,
                   .health_max = 8,
                   .speed      = 0.05f,
                   .drop_item  = ITEM_MUTTON,
                   .drop_min   = 1,
                   .drop_max   = 2,
                   .feed       = {ITEM_WHEAT, 0},
                   .shear_item = ITEM_WOOL,
                   .shear_min  = 1,
                   .shear_max  = 3,
                   .argb       = 0xFFEEEAE0u},

    // THE TWO THAT FIGHT BACK (D-137), and nothing else. They are rows
    // like any other -- what is different is the intent the caller may
    // put them in, not the record.
    //
    // THEY DROP NOTHING, which is D-144 satisfied by doing nothing: no
    // recipe in the graph has a mob in it, because bone already comes
    // from the sausage maker (D-120) and string from sheep (D-126). A
    // drop here would be optional loot off a body, not an ingredient,
    // and which ones to add is still the user's open question -- so the
    // honest row is an empty one, and an item added later can be taken
    // back out without breaking anything.
    //
    // WHAT THEY TAKE OFF YOU (D-156), which is the other half of D-144:
    // killing them is not how you get anything, and being killed by them
    // is the only thing they are good at. A zombie is the slower one with
    // the harder hit; a skeleton is quicker to shrug off and dies sooner,
    // so the two are a choice rather than two sizes of the same threat.
    [MOB_ZOMBIE] = {.name       = "zombie",
                    .label      = SM_STR_MOB_ZOMBIE,
                    .w          = 0.6f,
                    .h          = 1.8f,
                    .health_max = 20,
                    .speed      = 0.06f,
                    .drop_item  = ITEM_NONE,
                    .hostile    = true,
                    .attack     = 3,
                    .argb       = 0xFF6E8A5Cu},

    [MOB_SKELETON] = {.name       = "skeleton",
                      .label      = SM_STR_MOB_SKELETON,
                      .w          = 0.6f,
                      .h          = 1.9f,
                      .health_max = 16,
                      .speed      = 0.055f,
                      .drop_item  = ITEM_NONE,
                      .hostile    = true,
                      .attack     = 2,
                      .argb       = 0xFFD8D4C8u},
};

int mob_damage_of(uint16_t item) {
    // A FIST IS ONE, AND A TOOL IS WHAT IT IS WORTH. There is no sword
    // yet and none of these is one: an axe hits hardest because it is
    // the heaviest thing in the game, and a hoe hits like a fist
    // because swinging a hoe at a cow is not a plan.
    //
    // Ten points of health means a cow takes ten punches or four blows
    // of an iron axe -- long enough that killing one is a decision,
    // short enough that it is not a chore.
    item_def_t const d = item_def(item);
    switch (d.tool) {
        case TOOL_AXE: return 2 + (int)d.tool_level;
        case TOOL_PICK:
        case TOOL_SHOVEL: return 1 + (int)d.tool_level;
        default: return 1;
    }
}

// --- The pool ----------------------------------------------------------

static mob_t    s_pool[MOB_MAX];
static int      s_live;
// HOW MANY ANIMALS WERE TURNED AWAY because the pool was full. Zero in
// normal play, and the number that would have made F-125 obvious on the
// first boot instead of after an afternoon's walking: a full pool stops
// the world QUIETLY, and quiet is the problem.
static uint32_t s_refused;
// WHICH KIND hit the player during the last mob_tick (D-156), for the
// HUD. Held here for the same reason the damage is reported rather than
// applied: mob.c does not know what a screen is.
static uint8_t s_attacker_kind;
// Ids are handed out in order and never reused while a world is open:
// they are what a creature's own randomness is seeded from, so two
// animals standing in the same place still behave differently.
static uint32_t s_next_id = 1;

void mob_reset(void) {
    memset(s_pool, 0, sizeof s_pool);
    s_live    = 0;
    s_next_id = 1;
    s_refused = 0;
    s_attacker_kind = MOB_NONE;
}

int mob_live(void) {
    return s_live;
}

mob_t const* mob_at(int i) {
    return (i >= 0 && i < MOB_MAX) ? &s_pool[i] : NULL;
}

mob_t* mob_at_mut(int i) {
    return (i >= 0 && i < MOB_MAX) ? &s_pool[i] : NULL;
}

static mob_t* claim(void) {
    for (int i = 0; i < MOB_MAX; i++) {
        if (!s_pool[i].alive) return &s_pool[i];
    }
    return NULL;  // full: nothing is born, which is better than a stall
}

int mob_spawn(uint8_t kind, double x, double y, double z, bool baby) {
    if (kind == MOB_NONE || kind >= MOB_KIND_COUNT) return -1;
    mob_t* m = claim();
    if (m == NULL) return -1;

    mob_def_t const* d = mob_def(kind);
    memset(m, 0, sizeof *m);
    phys_body_init(&m->body, x, y, z);
    // A CALF IS THE SAME ANIMAL AT HALF THE SIZE, box included -- so it
    // fits under things its mother does not, which is the only thing
    // anybody ever notices about a baby animal.
    m->body.w  = baby ? d->w * 0.5f : d->w;
    m->body.h  = baby ? d->h * 0.5f : d->h;
    m->alive   = true;
    m->kind    = kind;
    m->health  = d->health_max;
    m->id      = s_next_id++;
    m->baby    = baby;
    m->yaw     = 0.0f;
    s_live++;
    return (int)(m - s_pool);
}

static void kill(mob_t* m) {
    if (!m->alive) return;
    m->alive = false;
    if (s_live > 0) s_live--;
}

// --- Is anybody looking? ------------------------------------------------
//
// A creature only lives while its chunk is resident, exactly like a
// dropped item: it does not fall, does not age and does not wander
// while nobody is there. Walking away must not turn a herd loose.
static bool resident(mob_t const* m) {
    int32_t const x = (int32_t)floor(m->body.x), z = (int32_t)floor(m->body.z);
    return chunk_find(chunk_of(x), chunk_of(z)) != NULL;
}

// --- Randomness that a replay reproduces --------------------------------

static uint32_t roll(mob_t const* m, uint32_t now, uint32_t salt) {
    return sm_hash3((int32_t)m->id, (int32_t)now, (int32_t)m->kind, 0x4D0B5EEDu ^ salt);
}

// WHICH WAY IT FACES, AND HOW OFTEN IT MAY CHANGE ITS MIND (F-139).
//
// A small correction is free -- walking towards a mate or a player is a
// few degrees a tick and must not be rationed, or nothing could track
// anything. A BIG turn costs a second's commitment, which is the whole
// fix for the creature the user watched spinning 180 degrees a frame in
// a pond: shore_dir() was being re-asked every tick and its answer
// flipped between two equidistant shores as the body drifted.
//
// Returns true if the turn was taken.
static bool face(mob_t* m, float yaw) {
    float d = yaw - m->yaw;
    // Shortest way round, so turning from 350 degrees to 10 is 20 and
    // not 340 -- otherwise every wrap of the circle reads as a reversal.
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;
    float const mag = d < 0.0f ? -d : d;

    if (mag < MOB_TURN_BIG) {
        m->yaw = yaw;  // a correction, not a decision
        return true;
    }
    if (m->turn_cd > 0) return false;  // it has just committed to a direction
    m->yaw     = yaw;
    m->turn_cd = (uint8_t)MOB_TURN_COOLDOWN;
    return true;
}

// --- Gravity, and the water they float in -------------------------------

#define MOB_GRAVITY    0.04f
#define MOB_DRAG       0.98f
#define MOB_TERMINAL   1.5f
#define MOB_JUMP       0.30f

// IN WATER THEY FLOAT, and these are the player's own numbers (D-86,
// player.h): buoyancy cancels almost all of gravity and the water kills
// a fall in well under a second. Without them an animal that wandered
// into a lake SANK -- it had a swim stroke but full gravity under it,
// so the stroke was 0.025 against 0.04 and the net was down. What that
// looked like from the shore was animals standing on the sea bed, which
// read as animals SPAWNING in the ocean (F-125).
#define MOB_WATER_GRAV 0.008f
#define MOB_WATER_DRAG 0.80f
#define MOB_WATER_TERM 0.50f
// The stroke, which is what keeps a floating animal at the surface
// rather than drifting down through it.
#define MOB_SWIM_UP    0.020f
// ... and the harder one, for climbing out at a bank.
#define MOB_SWIM_CLIMB 0.16f

// IS IT IN THE WATER -- asked at the FEET, not at the middle.
//
// The middle is what the player uses (PL_WADE_Y), because the question
// there is "am I wading or swimming". For an animal the question is
// "is the water holding me up", and a floating body rides with its
// middle ABOVE the surface: testing there said dry, which switched the
// buoyancy and the swim-for-the-shore steering off at exactly the
// moment they were doing their job, and left a cow bobbing in the
// middle of a lake turning in circles.
// A floating body rides with its FEET AT THE SURFACE -- buoyancy lifts
// it until they leave the water and gravity drops them back -- so the
// cell the feet are in reads air about as often as it reads water. Two
// samples, a little above the feet and a little below, and either one
// counts: without the lower one a floating animal flickers between
// swimming and not, which turns off its buoyancy and its steering every
// other tick and leaves it turning in circles in the middle of a lake.
#define MOB_WET_UP   0.25
#define MOB_WET_DOWN 0.15
// WATER, AND NOT ANY LIQUID (D-156). These two asked `block_liquid()`,
// which is true of lava too, so a creature standing in a lava sea was
// buoyant, swam a stroke, and floated on it like a cow in a pond --
// while the code two screens away was about to start burning it. The
// float, the swim stroke and the swim-for-the-shore steering are what
// water does; lava is what the hazard test below is for.
static bool in_water(mob_t const* m) {
    int32_t const x = (int32_t)floor(m->body.x), z = (int32_t)floor(m->body.z);
    if (world_block(x, (int32_t)floor(m->body.y + MOB_WET_UP), z) == BLK_WATER) return true;
    return world_block(x, (int32_t)floor(m->body.y - MOB_WET_DOWN), z) == BLK_WATER;
}

// IS IT IN THE WATER OR ON IT? A swimming body BOBS: MOB_SWIM_UP lifts
// it until its feet are above the surface, at which point in_water()
// says no -- and that single tick is enough to turn a swim into a
// hazard-avoidance reversal, because water ahead IS a hazard to
// something standing on dry land. The next tick it sinks back, swims
// on, bobs up, and turns again.
//
// THAT is the creature the user watched spinning 180 degrees a frame in
// a lake (F-139). Nothing to do with which shore it had picked: it was
// being asked, every other tick, whether it was an animal on a beach
// looking at a river.
//
// So: anything with water under its feet is still at sea, and water
// ahead of it is the way home rather than a danger.
static bool afloat(mob_t const* m) {
    if (in_water(m)) return true;
    int32_t const x = (int32_t)floor(m->body.x), z = (int32_t)floor(m->body.z);
    return world_block(x, (int32_t)floor(m->body.y) - 1, z) == BLK_WATER;
}

// IS THE THING IN FRONT A FENCE? A creature hops over a one-block step
// so it can follow the player across broken ground, and that same hop
// at a fence is what a pen is for -- so the fence is the one thing it
// will not try (F-122, the user: "I put two cows into an enclosure of
// fences. They where able to jump up the fence and escape").
//
// The alternative was to make a fence taller than anything can jump,
// which is not a height: a creature standing on a block beside a
// two-block fence would want a three-block one. A rule about what a
// fence IS settles it at any height.
static bool is_fence(uint8_t b) {
    block_kind_t const k = block_kind(b);
    return k == K_FENCE || (k == K_GATE && block_solid(b));
}

static bool fence_ahead(mob_t const* m) {
    // THE AXES THAT ACTUALLY BLOCKED IT, not the way it is facing. In a
    // corner a creature is stopped on x while walking mostly along z,
    // and a probe along the yaw looks straight past the fence that
    // stopped it -- which is how the first version of this let a cow
    // jump in a corner.
    double const hw = (double)m->body.w * 0.5 + 0.1;
    struct {
        double x, z;
    } const probes[2] = {
        {m->body.hit_x ? (m->body.vx < 0.0f ? -hw : hw) : 0.0, 0.0},
        {0.0, m->body.hit_z ? (m->body.vz < 0.0f ? -hw : hw) : 0.0},
    };
    // FROM ONE CELL BELOW THE FEET. A fence is a block and a half, so a
    // creature standing on anything beside one meets it at the cell
    // BELOW its feet -- and that is exactly the case that let a cow out
    // of the user's pen.
    for (int p = 0; p < 2; p++) {
        if (probes[p].x == 0.0 && probes[p].z == 0.0) continue;
        int32_t const x = (int32_t)floor(m->body.x + probes[p].x);
        int32_t const z = (int32_t)floor(m->body.z + probes[p].z);
        int32_t const y = (int32_t)floor(m->body.y + 0.1);
        for (int d = -1; d <= 1; d++) {
            if (is_fence(world_block(x, y + d, z))) return true;
        }
    }
    return false;
}

// IS THE WAY AHEAD SOMEWHERE THIS CREATURE SHOULD NOT GO -- a drop it
// would not survive the walk down, or water it would have to swim?
//
// WATER IS NOT AT ONE HEIGHT. There is no sea level in this game: a
// pond is wherever the ground dips, a player can pour a bucket out on a
// hilltop, and a stream flows down a slope (the user, on being shown a
// rule that assumed otherwise). So this finds the FLOOR of the cell
// ahead -- wherever that is -- and measures the water standing on it.
//
// One block of water is waded: a stream, a rice paddy, the edge of a
// lake. Two is a swim, and an animal that swims is an animal drifting
// away from where its owner left it.
//
// Testing the cell at the animal's own foot level was not enough and
// was wrong twice over: an animal on a BANK has air at its feet and a
// pond below, and an animal at a shore has the lake bed below it, which
// says nothing about how deep the water over it is.
//
// LAVA IS IN HERE ON PURPOSE, WATER IS NOT (D-156). This is the one
// liquid test that still asks `block_liquid()`, because the question is
// "should it not go there" and a lava sea is the loudest possible answer
// to it. It is what makes the Deep Mine's lakes a barrier a player can
// use: a zombie will not walk into one, so a lake is a wall that the
// cave's own terrain built.
#define MOB_WADE_DEPTH 1  // cells of water it will walk through

static bool hazard_ahead(mob_t const* m, float dx, float dz) {
    int32_t const x = (int32_t)floor(m->body.x + (double)dx * 0.7);
    int32_t const z = (int32_t)floor(m->body.z + (double)dz * 0.7);
    int32_t const y = (int32_t)floor(m->body.y);

    // Down from its own level for something to stand on. A step up is
    // the caller's business (the body hops); this is about going DOWN.
    for (int d = 0; d <= 3; d++) {
        int32_t const fy = y - d;
        uint8_t const b  = world_block(x, fy, z);
        if (!block_solid(b)) continue;

        // The floor. How much water is standing on it?
        int depth = 0;
        for (int u = 1; u <= MOB_WADE_DEPTH + 1; u++) {
            if (!block_liquid(world_block(x, fy + u, z))) break;
            depth++;
        }
        return depth > MOB_WADE_DEPTH;
    }
    return true;  // nothing within three: a cliff
}

// --- One creature's tick -------------------------------------------------

static void pick_intent(mob_t* m, uint32_t now) {
    uint32_t const r = roll(m, now, 0x11u);
    // Half the time it stands there. An animal that is always walking
    // reads as a wind-up toy, and a field of them is exhausting to look
    // at.
    if ((r & 1u) == 0u) {
        m->intent     = MOB_STAND;
        m->intent_for = (uint16_t)(20u + (r >> 8) % 60u);
        return;
    }
    m->intent     = MOB_WANDER;
    m->intent_for = (uint16_t)(20u + (r >> 16) % 50u);
    // Through face(), like every other turn: an amble that reversed on
    // the tick after a reversal is the same jitter from a third source.
    face(m, (float)((r >> 4) % 628u) / 100.0f);  // 0 .. 2pi
}

// --- Shoving -------------------------------------------------------------
//
// TWO BODIES DO NOT SHARE A SPACE. The collider knows about the world
// and nothing else (physics.h: "nothing here knows what a player is"),
// which is right -- but it means a cow walks through a cow and through
// the player, which is what the user saw.
//
// The answer is a SOFT PUSH rather than a hard collision, and it is
// Minecraft's: bodies that overlap are eased apart a little each tick
// along the line between them. A hard one would need the sweep to test
// against moving boxes, and two animals in a corner would lock solid
// instead of squeezing past each other.
//
// Round rather than square: the boxes are as wide as they are deep, so
// a circle of the same width is the same test with no corner cases,
// and the overlap is a subtraction.

// Push `a` and `b` apart, each by its own share. Either may be NULL-
// shared (a sitting dog does not budge), and every move goes through
// phys_move so the world still wins.
static void shove(phys_body_t* a, phys_body_t* b, float a_share, float b_share, uint32_t salt) {
    double const dy = a->y - b->y;
    // Different floors: a cow on a roof is not in the way of one below.
    if (dy > (double)b->h || -dy > (double)a->h) return;

    double dx = a->x - b->x, dz = a->z - b->z;
    double const r  = ((double)a->w + (double)b->w) * 0.5;
    double const d2 = dx * dx + dz * dz;
    if (d2 >= r * r) return;

    double d = sqrt(d2);
    if (d < 1e-4) {
        // Exactly on top of each other, which happens the moment a calf
        // is born between its parents. A hash rather than a constant, or
        // every such pair would part along the same axis for ever.
        uint32_t const h = salt * 2654435761u;
        dx               = ((h & 0xFFFFu) / 32768.0) - 1.0;
        dz               = (((h >> 16) & 0xFFFFu) / 32768.0) - 1.0;
        d                = sqrt(dx * dx + dz * dz);
        if (d < 1e-4) {
            dx = 1.0;
            dz = 0.0;
            d  = 1.0;
        }
    }
    double push = (r - d) * 0.5;
    if (push > (double)MOB_PUSH) push = (double)MOB_PUSH;
    double const ux = dx / d, uz = dz / d;
    if (a_share > 0.0f) phys_move(a, ux * push * (double)a_share, 0.0, uz * push * (double)a_share);
    if (b_share > 0.0f) phys_move(b, -ux * push * (double)b_share, 0.0, -uz * push * (double)b_share);
}

// WHICH WAY IS THE SHORE? The yaw towards the nearest cell an animal
// could stand on, or a negative number if there is none within reach.
//
// This is what makes the rule above SAFE RATHER THAN BRITTLE. Refusing
// to walk into water stops an animal choosing to swim; it does not stop
// one being shoved off a bank by another, hopping a fence into a moat,
// being poured on by a player with a bucket, or standing where a lake
// already is because an older build let it. Something has to get them
// out again, and it is this.
//
// Eight directions, six blocks: 48 lookups, and only for a creature
// that is actually in water, which is a handful at most.
static float shore_dir(mob_t const* m) {
    int32_t const y = (int32_t)floor(m->body.y);
    float         best_yaw = -1.0f;
    int           best_d = 99;
    float         best_turn = 99.0f;

    for (int a = 0; a < 8; a++) {
        float const yaw = (float)a * 0.785398f;  // an eighth of a turn
        float const sx = sinf(yaw), sz = cosf(yaw);
        for (int d = 1; d <= MOB_SHORE_RANGE && d <= best_d; d++) {
            int32_t const x = (int32_t)floor(m->body.x + (double)sx * (double)d);
            int32_t const z = (int32_t)floor(m->body.z + (double)sz * (double)d);
            // Land is a floor within a step of the surface with no more
            // than a wade of water on it.
            for (int u = 1; u >= -2; u--) {
                uint8_t const b = world_block(x, y + u, z);
                if (!block_solid(b)) continue;
                int depth = 0;
                for (int w = 1; w <= MOB_WADE_DEPTH + 1; w++) {
                    if (!block_liquid(world_block(x, y + u + w, z))) break;
                    depth++;
                }
                if (depth <= MOB_WADE_DEPTH) {
                    // TIES GO TO WHERE IT IS ALREADY HEADED, which is
                    // the other half of not spinning: the middle of a
                    // pond has two shores exactly as near, and picking
                    // the first one in a fixed scan order means the
                    // choice flips as the body drifts across the
                    // midline (F-139).
                    float td = yaw - m->yaw;
                    while (td > 3.14159265f) td -= 6.28318531f;
                    while (td < -3.14159265f) td += 6.28318531f;
                    float const turn = td < 0.0f ? -td : td;
                    if (d < best_d || turn < best_turn) {
                        best_d    = d;
                        best_yaw  = yaw;
                        best_turn = turn;
                    }
                }
                break;
            }
        }
    }
    return best_yaw;
}

// The player, as far as an animal is concerned.
typedef struct {
    double x, y, z;
    uint16_t held;
} watcher_t;

static bool wants(mob_t const* m, uint16_t item) {
    if (item == 0) return false;
    mob_def_t const* d = mob_def(m->kind);
    return item == d->feed[0] || item == d->feed[1];
}

// The nearest OTHER adult of the same kind that has also been fed, or
// NULL. What an animal in the mood walks towards.
static mob_t* partner_for(mob_t const* m) {
    mob_t* best = NULL;
    double best_d = (double)MOB_SEEK_RANGE * (double)MOB_SEEK_RANGE;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* o = &s_pool[i];
        if (o == m || !o->alive || o->kind != m->kind || o->baby || o->love == 0 || o->breed_cd > 0) continue;
        double const dx = o->body.x - m->body.x, dz = o->body.z - m->body.z;
        double const d2 = dx * dx + dz * dz;
        if (d2 >= best_d) continue;
        best_d = d2;
        best   = o;
    }
    return best;
}

static void breed_pass(mob_t* m, uint32_t now) {
    if (m->baby || m->love == 0) return;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* o = &s_pool[i];
        if (o == m || !o->alive || o->kind != m->kind || o->baby || o->love == 0) continue;
        double const dx = o->body.x - m->body.x, dz = o->body.z - m->body.z;
        double const dy = o->body.y - m->body.y;
        if (dx * dx + dy * dy + dz * dz > (double)(MOB_BREED_RANGE * MOB_BREED_RANGE)) continue;

        // A CALF, between the two of them, and both go off the boil for
        // a while. Nothing is created if the pool is full, which is the
        // only way breeding can fail and the only one worth having.
        if (mob_spawn(m->kind, (m->body.x + o->body.x) * 0.5, m->body.y, (m->body.z + o->body.z) * 0.5, true) < 0) {
            return;
        }
        m->love = o->love = 0;
        m->breed_cd = o->breed_cd = (uint16_t)MOB_BREED_CD;
        m->say                    = MOB_SAY_IDLE;
        (void)now;
        return;
    }
}

// --- Aggression (D-156) -------------------------------------------------
//
// THE USER'S RULE, whole: *"Each hostile mob has zero or one target they
// are aggressive against. If a Zombie hits a skeleton or vice versa by
// accident, the attacked mob sets its agression target to that attacker
// instead. Mobs can hurt each other."*
//
// That is three things, and the third is what makes the second need no
// bookkeeping: a target, a way to reach it, and a swing that hits
// whatever is nearest in front of the thing swinging. Nothing has to
// notice that a zombie happened to be in the way of a skeleton's swing,
// because the swing was never aimed at a skeleton in the first place --
// it was aimed at whatever was nearest.

// The creature with this id, or NULL when it is not in the pool any more.
// Ids are never reused while a world is open, so "not found" means it
// died or its chunk was evicted, and both are a reason to stop being
// angry at it.
static mob_t* find_by_id(uint32_t id) {
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* o = &s_pool[i];
        if (o->alive && o->id == id) return o;
    }
    return NULL;
}

// Horizontal distance, which is the distance that matters for noticing
// somebody: two creatures on different floors of the cave are not near
// each other no matter what x and z say.
static double gap(mob_t const* a, mob_t const* b) {
    double const dx = b->body.x - a->body.x, dz = b->body.z - a->body.z;
    return sqrt(dx * dx + dz * dz);
}

// WHO IT IS AFTER, decided afresh every tick.
//
// THE NEARER THING WINS. Somebody already being chased is only worth
// chasing while nothing closer has walked into range -- otherwise a
// zombie that lost an argument with a skeleton three rooms away goes on
// ignoring the player standing in front of it, which reads as a mob that
// has decided to be useless.
//
// Returns the creature it is after, or NULL when the target is the player
// (who is not in the pool) or nobody.
static mob_t* choose_target(mob_t* m, mob_def_t const* d, phys_body_t const* player, double to_you) {
    if (!d->hostile) {
        m->target_kind = MOB_TARGET_NONE;
        return NULL;
    }

    mob_t* rival = (m->target_kind == MOB_TARGET_MOB) ? find_by_id(m->target_id) : NULL;
    if (m->target_kind == MOB_TARGET_MOB && rival == NULL) m->target_kind = MOB_TARGET_NONE;  // it died
    if (m->target_kind == MOB_TARGET_PLAYER && (player == NULL || to_you > (double)MOB_FORGET_RANGE)) m->target_kind = MOB_TARGET_NONE;
    if (rival != NULL && gap(m, rival) > (double)MOB_FORGET_RANGE) {
        m->target_kind = MOB_TARGET_NONE;
        rival          = NULL;
    }

    double const to_rival = rival != NULL ? gap(m, rival) : 1e9;
    if (player != NULL && to_you < (double)MOB_AGGRO_RANGE && to_you < to_rival) {
        m->target_kind = MOB_TARGET_PLAYER;
        m->target_id   = 0;
    }
    return m->target_kind == MOB_TARGET_MOB ? rival : NULL;
}

// IS IT IN REACH, AND IN FRONT OF IT? Both halves matter: range without
// an arc means a mob damages you from behind, and an arc without range
// means it hits the far end of the room.
static bool in_reach_and_in_front(mob_t const* m, double dx, double dz, double d) {
    if (d >= (double)MOB_ATTACK_RANGE) return false;
    if (d < 1e-4) return true;  // on top of it: there is no direction to check
    float const fx = sinf(m->yaw), fz = cosf(m->yaw);
    return ((dx / d) * (double)fx + (dz / d) * (double)fz) >= (double)MOB_ATTACK_ARC;
}

// WHAT A SWING HITS: the nearest body within reach and inside the arc --
// the player or another creature, whichever is nearer. `hit_player` says
// which of the two it was.
//
// Nearest, not "the one it is chasing", and that single word is the
// whole of the user's "by accident".
static mob_t* swing_target(mob_t const* m, phys_body_t const* player, bool* hit_player) {
    *hit_player = false;
    double best = (double)MOB_ATTACK_RANGE;
    mob_t* best_mob = NULL;

    if (player != NULL) {
        double const dx = player->x - m->body.x, dz = player->z - m->body.z;
        double const dy = player->y - m->body.y;
        double const d  = sqrt(dx * dx + dz * dz);
        // Different floors: a creature three blocks below the lip of a
        // shaft is not within a swing, whatever its x and z say. Same
        // test the shove pass uses, for the same reason.
        if (dy <= (double)m->body.h && -dy <= (double)player->h && in_reach_and_in_front(m, dx, dz, d)) {
            best        = d;
            *hit_player = true;
        }
    }

    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* o = &s_pool[i];
        if (!o->alive || o == m) continue;
        double const dx = o->body.x - m->body.x, dz = o->body.z - m->body.z;
        double const dy = o->body.y - m->body.y;
        double const d  = sqrt(dx * dx + dz * dz);
        if (d >= best) continue;
        if (dy > (double)m->body.h || -dy > (double)o->body.h) continue;
        if (!in_reach_and_in_front(m, dx, dz, d)) continue;
        best        = d;
        best_mob    = o;
        *hit_player = false;
    }
    return best_mob;
}

// TAKE THE HEALTH, and if that was the last of it, leave what it drops.
//
// Split out of mob_hit because a player's swing, another creature's swing
// and a lava pool are three different events, and only the health and the
// corpse are the same in all three. The flinch, the knockback and who it
// is now angry at belong to the caller: lava does none of them, and a
// creature shoved out of a lava pool by the knockback of its own burning
// would be a mob that cannot die in one.
static bool damage_mob(mob_t* m, int damage) {
    m->health = (int16_t)(m->health - (damage < 1 ? 1 : damage));
    m->say    = MOB_SAY_HURT;
    if (m->health > 0) return false;

    // DEAD, and what it leaves is rolled here rather than stored: the
    // count varies per kill, the way a harvest does (D-117), and it
    // comes from the world's hash so a replay agrees.
    mob_def_t const* d = mob_def(m->kind);
    int32_t const    cx = (int32_t)floor(m->body.x), cy = (int32_t)floor(m->body.y), cz = (int32_t)floor(m->body.z);
    // A CALF DROPS NOTHING. Otherwise the cheapest way to farm meat is
    // to breed and immediately butcher, which is not husbandry.
    if (!m->baby && d->drop_item != ITEM_NONE && d->drop_max > 0) {
        int const span = d->drop_max - d->drop_min + 1;
        int const n    = d->drop_min + (int)(sm_hash3(cx, cy, cz, 0xB1FF00Du ^ m->id) % (uint32_t)(span > 0 ? span : 1));
        item_entity_spawn(cx, cy, cz, d->drop_item, n, 0);
    }
    m->say = MOB_SAY_DIE;
    kill(m);
    return true;
}

// Knocked back from (fx, fz), and off the ground a little.
static void knock_back(mob_t* m, double fx, double fz) {
    double const dx  = m->body.x - fx, dz = m->body.z - fz;
    double const len = sqrt(dx * dx + dz * dz);
    m->flee_x        = len > 1e-6 ? (float)(dx / len) : 1.0f;
    m->flee_z        = len > 1e-6 ? (float)(dz / len) : 0.0f;
    m->body.vx       = m->flee_x * 0.25f;
    m->body.vz       = m->flee_z * 0.25f;
    if (m->body.on_ground) m->body.vy = 0.18f;
}

int mob_tick(uint32_t now, phys_body_t* player, uint16_t held) {
    watcher_t const you = {player != NULL ? player->x : 0.0, player != NULL ? player->y : 0.0,
                           player != NULL ? player->z : 0.0, held};
    // WHAT IT DID TO THE PLAYER (D-156), added up over the whole pool and
    // reported once: two zombies hitting you in the same tick is one event
    // as far as a health bar is concerned, and one line on the HUD.
    int dealt = 0;
    s_attacker_kind = MOB_NONE;

    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* m = &s_pool[i];
        if (!m->alive) continue;
        m->say = MOB_SAY_NONE;
        // NOT WHILE NOBODY IS THERE (mob.h): no falling, no ageing, no
        // wandering in a chunk that is not loaded.
        if (!resident(m)) continue;

        mob_def_t const* d = mob_def(m->kind);
        if (m->hurt > 0) m->hurt--;
        if (m->turn_cd > 0) m->turn_cd--;
        if (m->love > 0) m->love--;
        if (m->breed_cd > 0) m->breed_cd--;
        if (m->attack_cd > 0) m->attack_cd--;
        if (m->lava_cd > 0) m->lava_cd--;

        // THE FLEECE GROWS BACK, on ticks elapsed like everything else
        // here: a flock in a chunk nobody has visited grows no wool
        // while nobody is looking (D-51).
        if (m->shorn && ++m->age_shorn >= MOB_REGROW_TICKS) {
            m->shorn     = false;
            m->age_shorn = 0;
        }

        // GROWING UP. A calf becomes an animal, box and all, and only
        // then can it be bred or milked.
        m->age++;
        if (m->baby && m->age >= MOB_BABY_TICKS) {
            m->baby   = false;
            m->body.w = d->w;
            m->body.h = d->h;
        }

        // --- What it wants ------------------------------------------
        double const dx = you.x - m->body.x, dz = you.z - m->body.z;
        double const to_you = sqrt(dx * dx + dz * dz);
        // LURED BY FOOD -- unless it has had some. An animal that is
        // already looking for a partner, or resting after breeding, is
        // not interested in the player waving another potato (the
        // user's rule): it has somewhere else to be, and following the
        // player is what kept dragging a fed pig away from the other
        // one. A calf has no mood and is always lured.
        bool const   busy   = !m->baby && (m->love > 0 || m->breed_cd > 0);
        bool const   lured  = !busy && wants(m, you.held) && to_you < (double)MOB_FOLLOW_RANGE;
        bool const   heel   = m->tame && !m->sitting && to_you > 4.0;

        // WHO IT IS AGGRESSIVE AGAINST (D-156), settled before anything
        // else gets to decide what it wants: a hostile creature that has
        // picked somebody does not amble, is not lured by what is in
        // their hand, is not looking for a mate and does not run away.
        mob_t* const rival   = choose_target(m, d, player, to_you);
        bool const   hunting = d->hostile && m->target_kind != MOB_TARGET_NONE;
        double const to_foe  = rival != NULL ? gap(m, rival) : to_you;

        if (m->intent == MOB_FLEE && m->intent_for == 0) m->intent = MOB_STAND;
        // IN THE MOOD BEATS EVERYTHING BUT FEAR. Two fed animals walk
        // to each other rather than waiting to be herded together,
        // which is what made feeding a pen of pigs look like it did
        // nothing at all (F-124): the mood lasts thirty seconds and
        // they have to be within two and a half blocks of each other
        // while it lasts.
        mob_t const* mate = (!m->baby && m->love > 0 && m->breed_cd == 0) ? partner_for(m) : NULL;
        if (hunting) {
            // REACHING THEM IS THE JOB (D-156). It faces who it is after
            // and walks at them; the swing is what the walking buys.
            m->intent = MOB_HUNT;
            face(m, rival != NULL ? (float)atan2(rival->body.x - m->body.x, rival->body.z - m->body.z)
                                  : (float)atan2(dx, dz));
        } else if (m->sitting) {
            m->intent = MOB_STAND;
        } else if (m->intent != MOB_FLEE && mate != NULL) {
            m->intent = MOB_SEEK;
            face(m, (float)atan2(mate->body.x - m->body.x, mate->body.z - m->body.z));
        } else if (m->intent != MOB_FLEE && (lured || heel)) {
            m->intent = MOB_FOLLOW;
            face(m, (float)atan2(dx, dz));
        } else if (m->intent_for == 0) {
            pick_intent(m, now);
        }
        if (m->intent_for > 0) m->intent_for--;

        // A TAMED DOG LEFT TOO FAR BEHIND COMES BACK. Without it, one
        // cliff or one closed door and the dog is gone for good --
        // which is a pet that punishes you for having one.
        if (m->tame && !m->sitting && to_you > (double)MOB_DOG_TELEPORT) {
            int const gy = world_ground((int32_t)floor(you.x), (int32_t)floor(you.z));
            if (gy > 0) {
                m->body.x  = you.x;
                m->body.y  = (double)gy;
                m->body.z  = you.z;
                m->body.vx = m->body.vz = 0.0f;
            }
        }

        // --- Moving ---------------------------------------------------
        float speed = 0.0f;
        float yaw   = m->yaw;

        // IN THE WATER AND NOT MEANING TO BE: swim for the shore. See
        // shore_dir -- the rule that keeps them out is about choosing,
        // and plenty of things put an animal in a lake without it
        // choosing anything.
        bool const swimming = in_water(m);
        // A creature that is chasing somebody keeps chasing them through
        // water: the shore is the thing an animal is saved by, and a mob
        // that swam off to the bank the moment you stepped into a stream
        // would be a mob that cannot be followed down one.
        if (afloat(m) && m->intent != MOB_FOLLOW && m->intent != MOB_HUNT) {
            float const to_land = shore_dir(m);
            if (to_land >= 0.0f) {
                // THE TURN IS RATIONED and the swim is not: if it may not
                // turn yet it keeps swimming the way it already was,
                // which is how it crosses a pond instead of pivoting in
                // the middle of one (F-139).
                face(m, to_land);
                m->intent     = MOB_WANDER;
                m->intent_for = 20;
                yaw           = m->yaw;
            } else {
                // AND IF THERE IS NO SHORE IN RANGE, SWIM ON. Falling
                // through to the ordinary wander meant a fresh random
                // direction every twenty ticks, which is a random walk:
                // 1.9 reversals a second, measured, and it never left
                // the water. Holding the heading is a straight line, and
                // a straight line finds a bank.
                m->intent = MOB_WANDER;
                if (m->intent_for < (uint16_t)MOB_SWIM_COMMIT) m->intent_for = (uint16_t)MOB_SWIM_COMMIT;
                yaw = m->yaw;
            }
        }
        switch (m->intent) {
            case MOB_WANDER: speed = swimming ? d->speed * 1.3f : d->speed; break;
            // CLOSE ENOUGH IS CLOSE ENOUGH (F-138). A lured animal used
            // to walk at the player until the shove pass and its own
            // legs reached a draw -- which they did INSIDE the player,
            // because 1.4x its speed beats a third of MOB_PUSH. It now
            // stops a body's width short and stands there, which is
            // still well within reach to feed, milk or shear.
            case MOB_FOLLOW:
                speed = to_you > (double)MOB_FOLLOW_STANDOFF ? d->speed * 1.4f : 0.0f;
                break;
            case MOB_SEEK: speed = d->speed * 1.4f; break;
            // IT COMES TO YOU, and stops within a swing. 1.4x is the same
            // multiplier a lured animal gets, which is the deliberate
            // half of this: a zombie is faster than it ambles and slower
            // than a player walks, so the answer to a pack you cannot
            // beat is to leave, and the answer to being in a room with
            // one is to hit it. 2.0x -- what a frightened animal gets --
            // would make every encounter unwinnable by running.
            case MOB_HUNT:
                speed = to_foe > (double)MOB_ATTACK_RANGE ? d->speed * 1.4f : 0.0f;
                break;
            case MOB_FLEE:
                speed = d->speed * 2.0f;
                yaw   = (float)atan2(m->flee_x, m->flee_z);
                break;
            default: break;
        }
        // A calf is quicker on its feet and shorter in the stride; the
        // net effect is that it keeps up with its mother.
        if (m->baby) speed *= 1.1f;

        if (speed > 0.0f) {
            float const sx = sinf(yaw), sz = cosf(yaw);
            // GOING TO SOMEBODY IS THE ONE REASON TO TAKE THE RISK (the
            // user): a dog heeling and an animal after the food in your
            // hand will follow you into water and down a drop. Anything
            // else -- ambling, fleeing, walking to a mate -- stops at
            // the edge, because an animal that wanders into a lake or
            // off a cliff is an animal the player has to go and fetch.
            // ... and an animal already IN the water is past being
            // warned about it: it is swimming for the shore, and the
            // shore is on the other side of more water.
            // AFLOAT, not merely submerged: a body bobbing at the surface
            // is still at sea (afloat(), F-139).
            bool const reckless = m->intent == MOB_FOLLOW || afloat(m);
            // MOB_HUNT IS NOT ON THAT LIST (D-156). A hostile creature
            // will walk to you across a room, down a ledge it can step
            // down, and into a pool it can wade; what it will not do is
            // walk off a cliff or into a lava sea, because `hazard_ahead`
            // counts both. That is not caution for its own sake: it is
            // what makes the Deep Mine's lakes walls, and a player who
            // can lead a pack to the edge of one and stand on the far
            // side of it has a plan that does not need a sword.
            if (!reckless && hazard_ahead(m, sx, sz)) {
                // Turn away -- but only if it is allowed to turn. Without
                // the ration this fired every tick at the lip of a drop
                // and the animal shivered in place (F-139).
                face(m, m->yaw + 2.2f);
                speed = 0.0f;
            } else {
                m->body.vx = sx * speed;
                m->body.vz = sz * speed;
            }
        }
        if (speed <= 0.0f) {
            m->body.vx = 0.0f;
            m->body.vz = 0.0f;
        }

        bool const wet = swimming;
        phys_move(&m->body, (double)m->body.vx, (double)m->body.vy, (double)m->body.vz);
        // WALLS ARE STEPPED OVER, NOT CLIMBED: a body that is blocked
        // and on the ground hops, which is how an animal gets up a
        // one-block rise -- AND NEVER AT A FENCE, which is the whole of
        // what a pen is (fence_ahead, F-122).
        if ((m->body.hit_x || m->body.hit_z) && m->body.on_ground && speed > 0.0f && !fence_ahead(m)) {
            m->body.vy = MOB_JUMP;
        } else if ((m->body.hit_x || m->body.hit_z) && wet && !fence_ahead(m)) {
            // CLIMBING OUT. A swimming body is never `on_ground`, so the
            // step-up will not lift it and an animal that swam to the
            // bank floated against it for ever -- its feet below the
            // top of a bank it was touching. Pressing UP against
            // whatever stopped it is what gets it out, and it is what
            // anything swimming does.
            m->body.vy = MOB_SWIM_CLIMB;
        }
        if (wet) {
            // Swim up, then let the WATER's gravity and drag act rather
            // than the air's: one without the other is what sank them.
            m->body.vy += MOB_SWIM_UP;
            if (m->body.vy > 0.12f) m->body.vy = 0.12f;
            phys_gravity(&m->body, MOB_WATER_GRAV, MOB_WATER_DRAG, MOB_WATER_TERM);
        } else {
            phys_gravity(&m->body, MOB_GRAVITY, MOB_DRAG, MOB_TERMINAL);
        }

        // --- The swing (D-156) ------------------------------------------
        //
        // AFTER the move, because what it can hit depends on where it got
        // to, and before anything that reads the creature afterwards,
        // because a swing can kill one.
        if (hunting && d->attack > 0 && m->attack_cd == 0) {
            bool   on_the_player = false;
            mob_t* foe           = swing_target(m, player, &on_the_player);
            if (on_the_player) {
                dealt += d->attack;
                s_attacker_kind = m->kind;
                m->attack_cd    = (uint8_t)MOB_ATTACK_COOLDOWN;
            } else if (foe != NULL) {
                m->attack_cd = (uint8_t)MOB_ATTACK_COOLDOWN;
                foe->hurt    = (uint8_t)MOB_HURT_TICKS;
                knock_back(foe, m->body.x, m->body.z);
                // THE ATTACKED ONE IS NOW ANGRY AT THE ATTACKER, which is
                // the user's rule stated as an outcome instead of as a
                // special case: whoever it hit is whoever it is after now,
                // zombie to skeleton and skeleton to zombie alike. Nothing
                // here knows which two kinds are fighting.
                if (mob_def(foe->kind)->hostile) {
                    foe->target_kind = MOB_TARGET_MOB;
                    foe->target_id   = m->id;
                    foe->sitting     = false;
                }
                (void)damage_mob(foe, d->attack);
            }
        }

        // --- Breeding, and being heard --------------------------------
        if (m->love > 0 && m->breed_cd == 0) breed_pass(m, now);

        // A voice now and then, and only when there is somebody near
        // enough to hear it: about once every twenty seconds each.
        if (m->say == MOB_SAY_NONE && to_you < 20.0 && (roll(m, now, 0x77u) % 400u) == 0u) m->say = MOB_SAY_IDLE;

        // --- Lava (D-156) -----------------------------------------------
        //
        // LAST, because it can be the thing that kills this one, and
        // nothing above should run on a creature that just died.
        //
        // The same burn a player takes, from the same predicate
        // (physics.h, phys_in_lava) and on the same beat. No knockback and
        // no new target: lava is not something to be angry at, and a
        // creature flung out of a lava sea by the knockback of its own
        // burning would be a mob that cannot die in one.
        // THE COOLDOWN IS TICKED ONCE, with the others at the top of this
        // loop. Ticking it here as well halves the beat: the check's zombie
        // lasts 91 ticks in a lava pool, and 46 with a second decrement --
        // the sort of number nobody notices until a check asks how long it
        // lasted and gets half of what was designed.
        if (m->lava_cd == 0 && phys_in_lava(&m->body)) {
            m->lava_cd = (uint8_t)LAVA_BURN_TICKS;
            (void)damage_mob(m, LAVA_DAMAGE);
        }
    }

    // --- Nobody shares a space ------------------------------------------
    //
    // After everything has moved, not during: a pass that pushed as it
    // went would give the creature with the lower index the advantage,
    // and a herd would drift the way the pool is ordered.
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* a = &s_pool[i];
        if (!a->alive || !resident(a)) continue;

        // The player first, so a cow cannot be pushed INTO them by
        // another cow and stay there for a tick.
        if (player != NULL) {
            // A SITTING DOG IS FURNITURE: it holds its ground and the
            // player goes round it, which is the whole point of telling
            // one to sit.
            shove(&a->body, player, a->sitting ? 0.0f : 1.0f - MOB_PUSH_PLAYER, MOB_PUSH_PLAYER, a->id);
        }
        for (int j = i + 1; j < MOB_MAX; j++) {
            mob_t* b = &s_pool[j];
            if (!b->alive || !resident(b)) continue;
            shove(&a->body, &b->body, a->sitting ? 0.0f : 0.5f, b->sitting ? 0.0f : 0.5f, a->id * 31u + b->id);
        }
    }
    return dealt;
}

// --- Being hit ----------------------------------------------------------

int mob_pick(double ex, double ey, double ez, float dx, float dy, float dz, float reach, float* dist) {
    int   best = -1;
    float best_t = reach;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = &s_pool[i];
        if (!m->alive) continue;
        // The slab test, against the creature's own box -- GROWN A
        // LITTLE for the pointing, which is not the same question as
        // the colliding.
        //
        // A pig is 0.9 blocks tall and the player's eye is at 1.62, so
        // at two paces the crosshair passes clean over its back unless
        // you look twenty degrees down. That is what made feeding pigs
        // feel broken while cows (1.4 tall) worked (F-124). The margin
        // is what a hand on a d-pad needs; it is not enough to let you
        // hit something you are not looking at.
        double const grow = (double)MOB_AIM_MARGIN;
        double const hw   = (double)m->body.w * 0.5 + grow;
        double const lo[3] = {m->body.x - hw, m->body.y - grow, m->body.z - hw};
        double const hi[3] = {m->body.x + hw, m->body.y + (double)m->body.h + grow, m->body.z + hw};
        double const o[3]  = {ex, ey, ez};
        double const v[3]  = {(double)dx, (double)dy, (double)dz};
        double       t0 = 0.0, t1 = (double)reach;
        bool         miss = false;
        for (int a = 0; a < 3 && !miss; a++) {
            if (fabs(v[a]) < 1e-9) {
                miss = o[a] < lo[a] || o[a] > hi[a];
                continue;
            }
            double ta = (lo[a] - o[a]) / v[a], tb = (hi[a] - o[a]) / v[a];
            if (ta > tb) {
                double const s = ta;
                ta             = tb;
                tb             = s;
            }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            miss = t0 > t1;
        }
        if (miss || t0 < 0.0 || t0 > (double)best_t) continue;
        best   = i;
        best_t = (float)t0;
    }
    if (dist != NULL) *dist = best_t;
    return best;
}

bool mob_hit(int i, int damage, double fx, double fz) {
    mob_t* m = mob_at_mut(i);
    if (m == NULL || !m->alive || m->hurt > 0) return false;

    m->hurt = (uint8_t)MOB_HURT_TICKS;
    // Knocked back, and then what it does about it depends on what it is.
    knock_back(m, fx, fz);
    m->sitting = false;
    m->love    = 0;

    if (damage_mob(m, damage)) return true;

    if (mob_def(m->kind)->hostile) {
        // A HOSTILE ONE DOES NOT RUN FROM THIS (D-156). The player hit it,
        // so the player is what it is after. The knockback still applies,
        // because a swing has to move something or it looks like it missed
        // -- but MOB_FLEE would send a zombie away from the only thing it
        // wants to reach, and a mob that runs from being hit is a mob you
        // clear a room from by throwing one stone at it.
        m->target_kind = MOB_TARGET_PLAYER;
        m->target_id   = 0;
        m->intent      = MOB_HUNT;
        m->intent_for  = 0;
    } else {
        // ... and an animal runs -- away from whoever hit it, and a tamed
        // dog runs too, because a dog that stands and takes it reads as
        // broken rather than as loyal.
        m->intent     = MOB_FLEE;
        m->intent_for = 45;
    }
    return false;
}

uint8_t mob_attacker_kind(void) {
    return s_attacker_kind;
}

mob_use_result_t mob_use(int i, uint16_t item) {
    mob_use_result_t r = {MOB_USE_NOTHING, 0, false, false};
    mob_t*           m = mob_at_mut(i);
    if (m == NULL || !m->alive) return r;
    mob_def_t const* d = mob_def(m->kind);

    // A BUCKET ON A COW. Not on a calf: a calf gives no milk, and the
    // refusal is worth having because it explains what to do about it.
    if (item == ITEM_BUCKET && m->kind == MOB_COW && !m->baby) {
        r.what    = MOB_USE_MILKED;
        r.becomes = ITEM_BUCKET_MILK;
        return r;
    }

    // SHEARS ON A SHEEP. A fleece is the one thing an animal gives more
    // than once, so it is the one that needs a timer: a shorn sheep
    // gives nothing until its coat is back (MOB_REGROW_TICKS), and it
    // looks shorn in the meantime, which is what stops a player
    // wandering a flock trying every one of them.
    if (item_def(item).tool == TOOL_SHEARS && d->shear_item != ITEM_NONE && !m->baby) {
        if (m->shorn) {
            r.what = MOB_USE_BARE;
            return r;
        }
        m->shorn     = true;
        m->age_shorn = 0;  // counted up by the tick until the coat is back
        int const span = (int)d->shear_max - (int)d->shear_min + 1;
        int const n    = (int)d->shear_min + (int)(sm_hash3((int32_t)m->body.x, (int32_t)m->body.y,
                                                            (int32_t)m->body.z, 0x5EA12Cu ^ m->id) %
                                                   (uint32_t)(span > 0 ? span : 1));
        item_entity_spawn((int32_t)floor(m->body.x), (int32_t)floor(m->body.y), (int32_t)floor(m->body.z),
                          d->shear_item, n, 0);
        m->say = MOB_SAY_IDLE;
        r.what = MOB_USE_SHORN;
        r.wear = true;
        return r;
    }

    // A BONE ON A WILD DOG. The user's own answer, and it is why the
    // sausage maker has a second output slot.
    if (!m->tame && d->tame_item != 0 && item == d->tame_item) {
        m->tame    = true;
        m->sitting = false;
        m->say     = MOB_SAY_TAMED;
        r.what     = MOB_USE_TAMED;
        r.consume  = true;
        return r;
    }

    // A TAMED ONE SITS AND STANDS, so a dog can be left somewhere on
    // purpose instead of following you down a mine.
    if (m->tame && item != d->feed[0] && (d->feed[1] == 0 || item != d->feed[1])) {
        m->sitting = !m->sitting;
        r.what     = m->sitting ? MOB_USE_SIT : MOB_USE_STAND;
        return r;
    }

    // FEEDING: an adult goes in the mood to breed, and a calf grows up
    // a little faster for it (Minecraft's rule, and a use for a crop
    // that would otherwise be eaten).
    if (wants(m, item)) {
        if (m->baby) {
            // A young one eats to GROW, and has neither a mood nor a
            // cooldown to be busy with.
            m->age += MOB_BABY_TICKS / 10u;
        } else if (m->love > 0 || m->breed_cd > 0) {
            // ALREADY FED, OR RESTING AFTER BREEDING (the user's rule).
            // It eats nothing and the food stays in your hand, which is
            // what makes a stack of potatoes last as long as it should.
            r.what = MOB_USE_BUSY;
            return r;
        } else {
            m->love = (uint16_t)MOB_LOVE_TICKS;
        }
        m->say    = MOB_SAY_IDLE;
        r.what    = MOB_USE_FED;
        r.consume = true;
        return r;
    }
    return r;
}

// --- Where a herd comes from --------------------------------------------

// What lives in each biome, and how likely a chunk is to hold a herd of
// it. A row per biome pair rather than a rule, so adding sheep to the
// mountains is a line.
// HOW OFTEN A CHUNK HOLDS A HERD, and the numbers are a POPULATION
// rather than a feeling (F-125).
//
// Every resident chunk keeps its animals in the pool, and at the far
// view distance the ring holds 225 chunks (chunk_render.h,
// VIEW_FAR_EVICT). So the rates here multiplied by 225 must fit inside
// MOB_MAX with room to spare, or the pool fills and the world stops
// spawning -- which is exactly what happened at the first numbers:
// about 1.3 animals a chunk, which is a farmyard, not a landscape.
//
// These come to roughly half that again -- a herd every four or five
// chunks, so a walk meets one often enough to feel alive -- and about
// 100 animals across a full far-view ring.
//
// THE DOG RATES ARE UNCHANGED. They were already the rare ones, and
// the reason no dog had ever been seen was the cap rather than the
// odds: a rare roll that comes up when the pool is full is a roll that
// never happened.
static struct {
    uint8_t biome;
    uint8_t kind;
    uint8_t chance_pct;  // that a chunk of this biome holds a herd
    uint8_t min_n, max_n;
} const HERDS[] = {
    {BIOME_PLAINS, MOB_COW, 4, 2, 4},
    {BIOME_PLAINS, MOB_PIG, 4, 2, 3},
    // SHEEP ARE A PLAINS ANIMAL and a mountain one: grass and hillside,
    // which is where sheep are.
    {BIOME_PLAINS, MOB_SHEEP, 4, 2, 4},
    {BIOME_MOUNTAIN, MOB_SHEEP, 4, 1, 3},
    {BIOME_FOREST, MOB_PIG, 4, 2, 3},
    {BIOME_FOREST, MOB_COW, 2, 1, 3},
    {BIOME_BIRCH, MOB_PIG, 3, 1, 3},
    // WILD DOGS ARE RARE, and they were UNFINDABLE: at 5% of forest
    // and 3% of birch they wanted 400 chunks of walking, because this
    // world is four fifths PLAINS (measured: 629 plains chunks against
    // 58 of forest in 784). A rare thing in a place nobody goes is not
    // rare, it is absent -- and a dog is the one creature the player is
    // supposed to go looking for.
    //
    // So: commoner in the woods where they belong, and a thin scatter
    // on the grassland where the player actually walks. About one dog
    // every forty chunks, which is a find rather than an errand.
    {BIOME_FOREST, MOB_DOG, 8, 1, 2},
    {BIOME_BIRCH, MOB_DOG, 6, 1, 1},
    {BIOME_PLAINS, MOB_DOG, 1, 1, 2},
};

// --- Packs (D-147) ---------------------------------------------------
//
// The Deep Mine's population, and the same shape as HERDS[] -- a roll
// per chunk from the world's hash, never rand() -- with two differences
// that are the whole of what a pack is.
//
// ONE ROLL PER CHUNK, not one per kind, because the numbers the user
// gave are a share of chunks (12%) and a size (2-3), not two independent
// kind rates. So `deepmine_pack()` answers whether a chunk holds one and
// how big it is, and this table only decides what each member of that
// pack is. Splitting the share across kinds would make the share a
// function of the table rather than of the design.
//
// A PACK LIVES IN ONE CHUNK, because creatures are stored with the chunk
// they stand in (D-123): a pack spread over two chunks is not a pack, it
// is two records.
//
// AND NOTHING COMES BACK (D-146). A chunk that is rolled once and then
// cleared stays cleared, so this is the only place a hostile mob is ever
// made. That is what makes `s_refused` matter more here than in the
// overworld: a pack the pool turned away is not delayed, it is gone for
// good.
static struct {
    uint8_t kind;
    uint8_t weight;  // this kind's share of a pack's members
} const PACKS[] = {
    {MOB_ZOMBIE, 60},
    {MOB_SKELETON, 40},
};

static uint8_t pack_kind(uint32_t roll) {
    int total = 0;
    for (size_t i = 0; i < sizeof PACKS / sizeof PACKS[0]; i++) total += PACKS[i].weight;
    uint32_t r = roll % (uint32_t)total;
    for (size_t i = 0; i < sizeof PACKS / sizeof PACKS[0]; i++) {
        if (r < (uint32_t)PACKS[i].weight) return PACKS[i].kind;
        r -= (uint32_t)PACKS[i].weight;
    }
    return PACKS[0].kind;
}

// A pack on this chunk's floor. The floor is a column function, so the
// cell is knowable before the chunk exists -- but the mob is only put
// here while the chunk is resident, which is what `mob_populate_chunk`
// already guarantees (it runs on the main task, once, on CF_FRESH).
static void populate_pack(int32_t cx, int32_t cz, uint32_t seed) {
    int n = 0;
    if (!deepmine_pack(cx, cz, seed, &n)) return;

    for (int i = 0; i < n; i++) {
        uint32_t const q  = sm_hash3(cx, (int32_t)(i + 1), cz, seed ^ 0x5EED1234u);
        int32_t const bx = cx * CH_W + 4 + (int32_t)(q % 8u);
        int32_t const bz = cz * CH_D + 4 + (int32_t)((q >> 8) % 8u);
        // THE COLUMN A BODY FITS IN, which is three questions the generator
        // now answers for us: is there room under the roof at all (a pinched
        // column is a wall of the cave, not part of it), and where is the cell
        // to stand in once the lava is counted (a mob standing in a lake is a
        // mob nobody can reach). `deepmine_ground_y()` answered neither.
        if (!deepmine_has_room(bx, bz, seed)) continue;
        int const y = deepmine_stand_y(bx, bz, seed);
        if (y < 1 || y + 2 >= CH_H) continue;
        // And not on a lake. `stand_y` is the cell ABOVE the lava, so the two
        // emptiness checks below would happily put a zombie on the surface of
        // a lava sea: the cell is air, and the mob is unreachable and on fire.
        if (deepmine_lava_top(bx, bz, seed) >= 0) continue;

        // And the cells have to be empty in the world that is actually
        // resident: a player-placed shaft, or something the player dug and
        // filled back in, is not in the generator's answer. A mob standing in
        // a shaft's trigger cell would be a crossing the player did not mean
        // to make.
        if (world_block(bx, y, bz) != BLK_AIR) continue;
        if (world_block(bx, y + 1, bz) != BLK_AIR) continue;

        if (mob_spawn(pack_kind(q >> 16), (double)bx + 0.5, (double)y, (double)bz + 0.5, false) < 0) s_refused++;
    }
}

uint32_t mob_refused(void) {
    return s_refused;
}

void mob_populate_chunk(int32_t cx, int32_t cz, uint32_t seed) {
    // TWO TABLES, ONE ENTRY POINT. The overworld has no hostile mobs at
    // all (D-137), and the Deep Mine has no herds -- so which table this
    // reads is which world the player is in, and nothing else changes.
    if (dimension_current() == DIM_DEEP_MINE) {
        populate_pack(cx, cz, seed);
        return;
    }

    for (size_t h = 0; h < sizeof HERDS / sizeof HERDS[0]; h++) {
        uint32_t const r = sm_hash3(cx, (int32_t)h, cz, seed ^ 0x9A12BEEFu);
        if ((r % 100u) >= HERDS[h].chance_pct) continue;

        // Where in the chunk, and how many. The middle four cells of a
        // sixteen-wide chunk, so a herd does not straddle the edge and
        // half of it land in a chunk that is not generated yet.
        int const span = HERDS[h].max_n - HERDS[h].min_n + 1;
        int const n    = HERDS[h].min_n + (int)((r >> 8) % (uint32_t)(span > 0 ? span : 1));
        for (int i = 0; i < n; i++) {
            uint32_t const q  = sm_hash3(cx, (int32_t)(h * 16 + (size_t)i), cz, seed ^ 0x5EED1234u);
            int32_t const  bx = cx * CH_W + 4 + (int32_t)(q % 8u);
            int32_t const  bz = cz * CH_D + 4 + (int32_t)((q >> 8) % 8u);
            if (worldgen_biome(bx, bz, seed) != HERDS[h].biome) continue;

            int const y = worldgen_height(bx, bz, seed);
            if (y <= 0 || y >= CH_H - 2) continue;
            // Never in or under water: worldgen_height is the first air
            // above the ground, so a cell of water reads as ground and
            // a cow would be standing in a lake.
            if (block_liquid(world_block(bx, y - 1, bz)) || block_liquid(world_block(bx, y, bz))) continue;
            if (mob_spawn(HERDS[h].kind, (double)bx + 0.5, (double)y, (double)bz + 0.5, false) < 0) s_refused++;
        }
    }
}

// --- Saving --------------------------------------------------------------

int mob_count_in(int32_t cx, int32_t cz) {
    int n = 0;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = &s_pool[i];
        if (m->alive && chunk_of((int32_t)floor(m->body.x)) == cx && chunk_of((int32_t)floor(m->body.z)) == cz) n++;
    }
    return n;
}

void mob_drop_chunk(int32_t cx, int32_t cz) {
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* m = &s_pool[i];
        if (!m->alive) continue;
        if (chunk_of((int32_t)floor(m->body.x)) != cx || chunk_of((int32_t)floor(m->body.z)) != cz) continue;
        kill(m);
    }
}

// One creature's tagged fields. The KIND IS A STRING (D-31): there are
// few enough of these for the name to cost nothing, and it can never be
// misread after a renumbering.
static void write_record(tag_writer_t* w, mob_t const* m) {
    tag_put_str(w, "kind", mob_def(m->kind)->name);
    tag_put_f32(w, "x", (float)m->body.x);
    tag_put_f32(w, "y", (float)m->body.y);
    tag_put_f32(w, "z", (float)m->body.z);
    tag_put_f32(w, "yaw", m->yaw);
    tag_put_i16(w, "hp", m->health);
    tag_put_i32(w, "age", (int32_t)m->age);
    if (m->baby) tag_put_i8(w, "baby", 1);
    if (m->tame) tag_put_i8(w, "tame", 1);
    if (m->shorn) {
        tag_put_i8(w, "shorn", 1);
        tag_put_i32(w, "shornage", (int32_t)m->age_shorn);
    }
    if (m->sitting) tag_put_i8(w, "sit", 1);
    if (m->love > 0) tag_put_i16(w, "love", (int16_t)m->love);
    if (m->breed_cd > 0) tag_put_i16(w, "cd", (int16_t)m->breed_cd);
}

size_t mob_encode_chunk(int32_t cx, int32_t cz, uint8_t* out, size_t cap) {
    if (out == NULL) return 0;
    int const n = mob_count_in(cx, cz);
    if (n == 0) return 0;
    if (cap < 7) return 0;

    size_t pos = 0;
    out[pos++] = SECTION_ENTITIES;
    size_t const len_at = pos;
    pos += 4;
    size_t const body_at = pos;
    out[pos++]           = (uint8_t)(n & 0xFF);
    out[pos++]           = (uint8_t)((n >> 8) & 0xFF);

    int written = 0;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = &s_pool[i];
        if (!m->alive) continue;
        if (chunk_of((int32_t)floor(m->body.x)) != cx || chunk_of((int32_t)floor(m->body.z)) != cz) continue;
        if (pos + 2 > cap) break;  // the buffer is full: write what is in it
        size_t const rec_len_at = pos;
        pos += 2;
        tag_writer_t w;
        tag_write_init(&w, out + pos, cap - pos);
        write_record(&w, m);
        size_t const rec = tag_write_done(&w);
        if (rec == 0) break;     // ... and so is this one
        out[rec_len_at]     = (uint8_t)(rec & 0xFF);
        out[rec_len_at + 1] = (uint8_t)((rec >> 8) & 0xFF);
        pos += rec;
        written++;
    }

    // THE COUNT IS WHAT WENT IN, not what was there. A reader told about
    // a record that is not in the section would walk off the end, so a
    // section that filled the buffer has to say how many records it
    // actually holds.
    //
    // It used to abandon the whole section and report nothing. The save
    // still SUCCEEDED -- chunk_encode cannot refuse a buffer it owns --
    // so the chunk was written without its creatures and evicted, and
    // the loss was permanent, because a chunk is rolled once and does
    // not repopulate (D-146). Writing what fits turns a whole section
    // lost into as much as fits, and the rest into a count that moves.
    if (written < n) chunk_note_drops((uint32_t)(n - written));
    if (written == 0) return 0;

    out[body_at]     = (uint8_t)(written & 0xFF);
    out[body_at + 1] = (uint8_t)((written >> 8) & 0xFF);

    size_t const body = pos - body_at;
    out[len_at]       = (uint8_t)(body & 0xFF);
    out[len_at + 1]   = (uint8_t)((body >> 8) & 0xFF);
    out[len_at + 2]   = (uint8_t)((body >> 16) & 0xFF);
    out[len_at + 3]   = (uint8_t)((body >> 24) & 0xFF);
    return pos;
}

static uint8_t kind_by_name(char const* s) {
    for (uint8_t k = MOB_PIG; k < MOB_KIND_COUNT; k++) {
        if (strcmp(MOBS[k].name, s) == 0) return k;
    }
    return MOB_NONE;
}

static void read_record(tag_reader_t* r, size_t end) {
    char     name[TAG_NAME_MAX + 1];
    char     kind[TAG_NAME_MAX + 1] = {0};
    float    x = 0, y = 0, z = 0, yaw = 0;
    int      hp = 0, age = 0, love = 0, cd = 0;
    bool     baby = false, tame = false, sit = false, shorn = false;
    int      shornage = 0;

    while (r->pos < end && !r->error) {
        int const t = tag_next(r, name, sizeof(name));
        if (t == TAG_END || t < 0) break;
        if (t == TAG_STR && strcmp(name, "kind") == 0) {
            tag_get_str(r, kind, sizeof(kind));
        } else if (t == TAG_F32 && strcmp(name, "x") == 0) {
            x = tag_get_f32(r);
        } else if (t == TAG_F32 && strcmp(name, "y") == 0) {
            y = tag_get_f32(r);
        } else if (t == TAG_F32 && strcmp(name, "z") == 0) {
            z = tag_get_f32(r);
        } else if (t == TAG_F32 && strcmp(name, "yaw") == 0) {
            yaw = tag_get_f32(r);
        } else if (t == TAG_I16 && strcmp(name, "hp") == 0) {
            hp = tag_get_i16(r);
        } else if (t == TAG_I32 && strcmp(name, "age") == 0) {
            age = tag_get_i32(r);
        } else if (t == TAG_I16 && strcmp(name, "love") == 0) {
            love = tag_get_i16(r);
        } else if (t == TAG_I16 && strcmp(name, "cd") == 0) {
            cd = tag_get_i16(r);
        } else if (t == TAG_I8 && strcmp(name, "baby") == 0) {
            baby = tag_get_i8(r) != 0;
        } else if (t == TAG_I8 && strcmp(name, "tame") == 0) {
            tame = tag_get_i8(r) != 0;
        } else if (t == TAG_I8 && strcmp(name, "shorn") == 0) {
            shorn = tag_get_i8(r) != 0;
        } else if (t == TAG_I32 && strcmp(name, "shornage") == 0) {
            shornage = tag_get_i32(r);
        } else if (t == TAG_I8 && strcmp(name, "sit") == 0) {
            sit = tag_get_i8(r) != 0;
        } else {
            tag_skip(r, t);  // a field this build does not know (D-30)
        }
    }

    uint8_t const k = kind_by_name(kind);
    if (k == MOB_NONE) return;  // a creature this build does not have
    int const i = mob_spawn(k, (double)x, (double)y, (double)z, baby);
    if (i < 0) return;
    mob_t* m = mob_at_mut(i);
    m->yaw      = yaw;
    if (hp > 0) m->health = (int16_t)hp;
    m->age      = (uint32_t)(age < 0 ? 0 : age);
    m->tame      = tame;
    m->sitting   = sit;
    m->shorn     = shorn;
    m->age_shorn = (uint32_t)(shornage < 0 ? 0 : shornage);
    m->love     = (uint16_t)(love < 0 ? 0 : love);
    m->breed_cd = (uint16_t)(cd < 0 ? 0 : cd);
}

void mob_decode_section(uint8_t const* data, size_t len) {
    if (data == NULL || len < 2) return;
    int const n = (int)data[0] | ((int)data[1] << 8);
    size_t    pos = 2;
    for (int i = 0; i < n && pos + 2 <= len; i++) {
        size_t const rec = (size_t)data[pos] | ((size_t)data[pos + 1] << 8);
        pos += 2;
        if (pos + rec > len) break;
        tag_reader_t r;
        tag_read_init(&r, data + pos, rec);
        read_record(&r, rec);
        pos += rec;
    }
}
