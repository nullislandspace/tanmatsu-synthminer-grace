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
// Ids are handed out in order and never reused while a world is open:
// they are what a creature's own randomness is seeded from, so two
// animals standing in the same place still behave differently.
static uint32_t s_next_id = 1;

void mob_reset(void) {
    memset(s_pool, 0, sizeof s_pool);
    s_live    = 0;
    s_next_id = 1;
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

// --- Gravity, and the water they float in -------------------------------

#define MOB_GRAVITY    0.04f
#define MOB_DRAG       0.98f
#define MOB_TERMINAL   1.5f
#define MOB_JUMP       0.30f
#define MOB_SWIM_UP    0.025f

static bool in_water(mob_t const* m) {
    return block_liquid(world_block((int32_t)floor(m->body.x), (int32_t)floor(m->body.y + (double)m->body.h * 0.5),
                                    (int32_t)floor(m->body.z)));
}

// Is the cell ahead a drop the creature should not walk off? Two below
// and still nothing is a cliff; one is a step.
static bool cliff_ahead(mob_t const* m, float dx, float dz) {
    int32_t const x = (int32_t)floor(m->body.x + (double)dx * 0.7);
    int32_t const z = (int32_t)floor(m->body.z + (double)dz * 0.7);
    int32_t const y = (int32_t)floor(m->body.y);
    for (int d = 1; d <= 3; d++) {
        uint8_t const b = world_block(x, y - d, z);
        if (block_solid(b) || block_liquid(b)) return false;
    }
    return true;
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
    m->yaw        = (float)((r >> 4) % 628u) / 100.0f;  // 0 .. 2pi
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

void mob_tick(uint32_t now, double px, double py, double pz, uint16_t held) {
    watcher_t const you = {px, py, pz, held};

    for (int i = 0; i < MOB_MAX; i++) {
        mob_t* m = &s_pool[i];
        if (!m->alive) continue;
        m->say = MOB_SAY_NONE;
        // NOT WHILE NOBODY IS THERE (mob.h): no falling, no ageing, no
        // wandering in a chunk that is not loaded.
        if (!resident(m)) continue;

        mob_def_t const* d = mob_def(m->kind);
        if (m->hurt > 0) m->hurt--;
        if (m->love > 0) m->love--;
        if (m->breed_cd > 0) m->breed_cd--;

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
        bool const   lured  = wants(m, you.held) && to_you < (double)MOB_FOLLOW_RANGE;
        bool const   heel   = m->tame && !m->sitting && to_you > 4.0;

        if (m->intent == MOB_FLEE && m->intent_for == 0) m->intent = MOB_STAND;
        if (m->sitting) {
            m->intent = MOB_STAND;
        } else if (m->intent != MOB_FLEE && (lured || heel)) {
            m->intent = MOB_FOLLOW;
            m->yaw    = (float)atan2(dx, dz);
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
        switch (m->intent) {
            case MOB_WANDER: speed = d->speed; break;
            case MOB_FOLLOW: speed = d->speed * 1.4f; break;
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
            if (m->intent == MOB_WANDER && cliff_ahead(m, sx, sz)) {
                // Turn rather than fall: a pig at the bottom of a ravine
                // is a pig the player has to go and fetch.
                m->yaw += 2.2f;
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

        bool const wet = in_water(m);
        phys_move(&m->body, (double)m->body.vx, (double)m->body.vy, (double)m->body.vz);
        // WALLS ARE STEPPED OVER, NOT CLIMBED: a body that is blocked
        // and on the ground hops, which is how an animal gets up a
        // one-block rise and how it fails at a fence (a fence is a
        // block and a half, blocks.h).
        if ((m->body.hit_x || m->body.hit_z) && m->body.on_ground && speed > 0.0f) m->body.vy = MOB_JUMP;
        if (wet) {
            m->body.vy += MOB_SWIM_UP;  // it floats rather than drowns
            if (m->body.vy > 0.12f) m->body.vy = 0.12f;
        }
        phys_gravity(&m->body, MOB_GRAVITY, MOB_DRAG, MOB_TERMINAL);

        // --- Breeding, and being heard --------------------------------
        if (m->love > 0 && m->breed_cd == 0) breed_pass(m, now);

        // A voice now and then, and only when there is somebody near
        // enough to hear it: about once every twenty seconds each.
        if (m->say == MOB_SAY_NONE && to_you < 20.0 && (roll(m, now, 0x77u) % 400u) == 0u) m->say = MOB_SAY_IDLE;
    }
}

// --- Being hit ----------------------------------------------------------

int mob_pick(double ex, double ey, double ez, float dx, float dy, float dz, float reach, float* dist) {
    int   best = -1;
    float best_t = reach;
    for (int i = 0; i < MOB_MAX; i++) {
        mob_t const* m = &s_pool[i];
        if (!m->alive) continue;
        // The slab test, against the creature's own box. Half a dozen
        // compares per creature and no square roots.
        double const hw = (double)m->body.w * 0.5;
        double const lo[3] = {m->body.x - hw, m->body.y, m->body.z - hw};
        double const hi[3] = {m->body.x + hw, m->body.y + (double)m->body.h, m->body.z + hw};
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

    m->health = (int16_t)(m->health - (damage < 1 ? 1 : damage));
    m->hurt   = (uint8_t)MOB_HURT_TICKS;
    m->say    = MOB_SAY_HURT;
    // Knocked back, and then it runs -- away from whoever hit it, and
    // a tamed dog runs too, because a dog that stands and takes it
    // reads as broken rather than as loyal.
    double const dx = m->body.x - fx, dz = m->body.z - fz;
    double const len = sqrt(dx * dx + dz * dz);
    m->flee_x        = len > 1e-6 ? (float)(dx / len) : 1.0f;
    m->flee_z        = len > 1e-6 ? (float)(dz / len) : 0.0f;
    m->body.vx       = m->flee_x * 0.25f;
    m->body.vz       = m->flee_z * 0.25f;
    if (m->body.on_ground) m->body.vy = 0.18f;
    m->intent     = MOB_FLEE;
    m->intent_for = 45;
    m->sitting    = false;
    m->love       = 0;

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

mob_use_result_t mob_use(int i, uint16_t item) {
    mob_use_result_t r = {MOB_USE_NOTHING, 0, false};
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
            m->age += MOB_BABY_TICKS / 10u;
        } else if (m->breed_cd == 0) {
            m->love = (uint16_t)MOB_LOVE_TICKS;
        } else {
            return r;  // too soon: it eats nothing and the food stays
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
static struct {
    uint8_t biome;
    uint8_t kind;
    uint8_t chance_pct;  // that a chunk of this biome holds a herd
    uint8_t min_n, max_n;
} const HERDS[] = {
    {BIOME_PLAINS, MOB_COW, 14, 2, 4},
    {BIOME_PLAINS, MOB_PIG, 12, 2, 3},
    {BIOME_FOREST, MOB_PIG, 12, 2, 3},
    {BIOME_FOREST, MOB_COW, 7, 1, 3},
    {BIOME_BIRCH, MOB_PIG, 8, 1, 3},
    // WILD DOGS ARE RARE and they live in the woods, which is what
    // makes taming one an event rather than an errand.
    {BIOME_FOREST, MOB_DOG, 5, 1, 2},
    {BIOME_BIRCH, MOB_DOG, 3, 1, 1},
};

void mob_populate_chunk(int32_t cx, int32_t cz, uint32_t seed) {
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
            mob_spawn(HERDS[h].kind, (double)bx + 0.5, (double)y, (double)bz + 0.5, false);
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
        if (pos + 2 > cap) return 0;
        size_t const rec_len_at = pos;
        pos += 2;
        tag_writer_t w;
        tag_write_init(&w, out + pos, cap - pos);
        write_record(&w, m);
        size_t const rec = tag_write_done(&w);
        if (rec == 0) return 0;  // did not fit: write no section at all
        out[rec_len_at]     = (uint8_t)(rec & 0xFF);
        out[rec_len_at + 1] = (uint8_t)((rec >> 8) & 0xFF);
        pos += rec;
        written++;
    }

    size_t const body = pos - body_at;
    out[len_at]       = (uint8_t)(body & 0xFF);
    out[len_at + 1]   = (uint8_t)((body >> 8) & 0xFF);
    out[len_at + 2]   = (uint8_t)((body >> 16) & 0xFF);
    out[len_at + 3]   = (uint8_t)((body >> 24) & 0xFF);
    return written == n ? pos : 0;
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
    bool     baby = false, tame = false, sit = false;

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
    m->tame     = tame;
    m->sitting  = sit;
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
