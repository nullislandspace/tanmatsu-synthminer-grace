// =====================================================================
//  SynthMiner  --  the player (see player.h)
// =====================================================================

#include "audio/sfx.h"
#include "game/player.h"

#include "common/trace.h"

#include <math.h>

#include "game/interact.h"
#include "items/item_entity.h"
#include "world/chunk.h"

// Just short of straight up or down. Exactly +-pi/2 makes the forward
// vector's horizontal part zero, and then "which way am I walking" has
// no answer.
#define PITCH_MAX 1.55f

// Ticks between one blow of the tool and the next while a block is being
// mined. Kept in step with FRED_STROKES in main.c (1.8 swings a second at
// 20 Hz), so the sound lands with the arm rather than beside it.
#define SWING_TICKS 11

void player_spawn(player_t* p, double x, double z, float yaw) {
    int const g = world_ground((int32_t)floor(x), (int32_t)floor(z));
    phys_body_init(&p->body, x, (double)(g > 0 ? g : CH_SEA_LEVEL), z);
    p->yaw        = yaw;
    p->pitch      = 0.0f;
    p->prev_x     = p->body.x;
    p->prev_y     = p->body.y;
    p->prev_z     = p->body.z;
    p->prev_yaw   = yaw;
    p->prev_pitch = 0.0f;
    p->in_air_last = false;
    p->aim_valid   = false;
    p->mining      = false;
    p->mine_ticks  = 0;
}

bool player_place(player_t* p, double x, double y, double z, float yaw, float pitch) {
    // A fresh body for the test, so it has the player's dimensions even
    // on a player that has never been initialised.
    phys_body_t probe;
    phys_body_init(&probe, x, y, z);
    if (!phys_fits(&probe, x, y, z)) return false;
    p->body = probe;
    p->yaw        = yaw;
    p->pitch      = pitch;
    p->prev_x     = x;
    p->prev_y     = y;
    p->prev_z     = z;
    p->prev_yaw   = yaw;
    p->prev_pitch = pitch;
    p->in_air_last = false;
    p->aim_valid   = false;
    p->mining      = false;
    p->mine_ticks  = 0;
    return true;
}

void player_reset(player_t* p) {
    p->health     = PL_HEALTH_MAX;
    p->hunger     = PL_HUNGER_MAX;
    p->aim_valid  = false;
    p->mining     = false;
    p->mine_ticks = 0;
    p->hit_mob    = -1;
    fishing_reset(&p->fish);

    // NOTHING. The starting kit is gone, the day a crafting table can
    // make what was in it (the user's call, 2026-09-23): punch a tree,
    // make planks, make a table, make a pickaxe. An empty pack is also
    // what makes the crafting book's discovery rule mean anything --
    // with the kit in hand, half of it was already known on the first
    // frame.
    inv_clear(&p->inv);
}

float player_mine_progress(player_t const* p) {
    if (!p->mining || p->mine_needed <= 0) return 0.0f;
    float const f = (float)p->mine_ticks / (float)p->mine_needed;
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

void player_tick(player_t* p, sm_actions_t mask, sm_actions_t pressed) {
    // ONE TICK'S WORTH, CLEARED FIRST. It used to be cleared further
    // down, in the branch that does the actual using -- which the
    // frozen branch below returns before ever reaching. So the moment a
    // crafting table opened a screen, the flag stayed set, and main.c
    // reopened that screen on EVERY TICK: the cursor snapped back to
    // the first row, the search box emptied, and every keystroke was
    // swallowed as the one that opened it. The arrow keys looked dead
    // and enter crafted whatever happened to be first in the list
    // rather than what was under the cursor, which is how it took the
    // materials and gave back something else (the user found both
    // halves of this; they are one bug).
    //
    // A field that says "this tick" is cleared at the top of the tick.
    p->used_block = BLK_AIR;
    p->needs_tool = 0;
    p->hit_mob    = -1;
    if (p->use_msg_ticks > 0 && --p->use_msg_ticks == 0) p->use_msg = USE_SAID_NOTHING;
    if (p->fish_msg_ticks > 0 && --p->fish_msg_ticks == 0) p->fish_msg = FISH_NOTHING;
    if (p->mob_msg_ticks > 0 && --p->mob_msg_ticks == 0) p->mob_msg = MOB_USE_NOTHING;

    // --- The inventory screen ----------------------------------------
    //
    // Open, it takes the movement and look keys for navigation and the
    // player stands still. Reading a grid while still walking is how
    // you end up in the lava you were standing next to.
    // NOT while a full-screen UI is up: Tab opened the inventory behind
    // the crafting book, which then had two screens taking the same
    // keys (the user found it immediately).
    if (!p->ui_open && act_held(pressed, SM_INVENTORY)) {
        p->inv.open = !p->inv.open;
        p->mining   = false;
    }
    // The crafting book freezes the player the same way, and takes the
    // navigation keys itself -- so this branch does nothing but hold
    // him still and let him fall.
    if (p->ui_open) p->mining = false;
    if (p->inv.open || p->ui_open) {
        p->prev_x     = p->body.x;
        p->prev_y     = p->body.y;
        p->prev_z     = p->body.z;
        p->prev_yaw   = p->yaw;
        p->prev_pitch = p->pitch;

        int const dx = (act_held(pressed, SM_RIGHT) || act_held(pressed, SM_LOOK_RIGHT) ? 1 : 0) -
                       (act_held(pressed, SM_LEFT) || act_held(pressed, SM_LOOK_LEFT) ? 1 : 0);
        int const dy = (act_held(pressed, SM_BACK) || act_held(pressed, SM_LOOK_DOWN) ? 1 : 0) -
                       (act_held(pressed, SM_FORWARD) || act_held(pressed, SM_LOOK_UP) ? 1 : 0);
        if (!p->ui_open && (dx || dy)) inv_move_cursor(&p->inv, dx, dy);

        // A hotbar key SWAPS the cursor's stack into that slot -- the
        // one operation the screen has to support, since without it
        // everything past the sixth slot is unreachable.
        for (int i = 0; i < INV_HOTBAR && !p->ui_open; i++) {
            if (!act_held(pressed, (sm_action_t)(SM_SLOT1 + i))) continue;
            inv_swap(&p->inv, p->inv.cursor, i);
            // AND SELECT IT. Putting a thing on the hotbar is almost
            // always followed by wanting it in hand, and the user asked
            // for the second step to stop being a step: "when putting
            // an icon into the hotbar using a function key, it helps
            // the user experience to make that automatically the
            // selected hotbar."
            p->inv.selected = i;
        }

        // Still fall while reading: standing over a hole and opening
        // the inventory must not make you hover.
        p->body.vx = 0.0f;
        p->body.vz = 0.0f;
        phys_move(&p->body, 0.0, (double)p->body.vy, 0.0);
        phys_gravity(&p->body, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
        if (item_entity_tick(&p->inv, p->body.x, p->body.y, p->body.z) > 0) sfx_play(SFX_PICKUP);
        p->aim_valid = false;
        return;
    }

    p->prev_x     = p->body.x;
    p->prev_y     = p->body.y;
    p->prev_z     = p->body.z;
    p->prev_yaw   = p->yaw;
    p->prev_pitch = p->pitch;

    // --- Looking ------------------------------------------------------
    float dyaw = 0.0f, dpitch = 0.0f;
    input_look(mask, &dyaw, &dpitch);
    p->yaw += dyaw;
    p->pitch += dpitch;
    if (p->pitch > PITCH_MAX) p->pitch = PITCH_MAX;
    if (p->pitch < -PITCH_MAX) p->pitch = -PITCH_MAX;

    // --- In the water? ------------------------------------------------
    //
    // Tested at the middle of the body (PL_WADE_Y): wading through a
    // shallow stream should not turn into swimming, and being up to the
    // chest in it should not stay walking.
    bool const in_water = block_liquid(world_block((int32_t)floor(p->body.x), (int32_t)floor(p->body.y + PL_WADE_Y),
                                                   (int32_t)floor(p->body.z)));

    // --- Walking ------------------------------------------------------
    //
    // Flattened: forward is where the player is facing, not where they
    // are looking. Looking at your feet must not slow you down.
    float const fwd = (act_held(mask, SM_FORWARD) ? 1.0f : 0.0f) - (act_held(mask, SM_BACK) ? 1.0f : 0.0f);
    float const str = (act_held(mask, SM_RIGHT) ? 1.0f : 0.0f) - (act_held(mask, SM_LEFT) ? 1.0f : 0.0f);
    // In water there is one speed: sneak means dive, not creep.
    float const speed = in_water ? PL_SWIM : act_held(mask, SM_SNEAK) ? PL_SNEAK : PL_WALK;

    float wish_x = 0.0f, wish_z = 0.0f;
    if (fwd != 0.0f || str != 0.0f) {
        float const cy = cosf(p->yaw), sy = sinf(p->yaw);
        // The engine's basis: forward is (sin yaw, cos yaw) in x and z,
        // right is (cos yaw, -sin yaw). Kept identical to flycam.c and
        // raycast.c on purpose.
        wish_x = fwd * sy + str * cy;
        wish_z = fwd * cy - str * sy;
        float const len = sqrtf(wish_x * wish_x + wish_z * wish_z);
        if (len > 1e-6f) {
            wish_x = wish_x / len * speed;
            wish_z = wish_z / len * speed;
        }
    }
    // Ease towards the wanted velocity rather than snapping to it, so
    // stopping and turning have some weight. On the ground only: in the
    // air you keep what you had, which is what makes a jump commit.
    // Water has purchase the way the ground does, but half of it, so
    // starting and stopping in it feel heavy.
    float const accel = in_water ? PL_ACCEL * 0.5f : p->body.on_ground ? PL_ACCEL : PL_ACCEL * 0.2f;
    p->body.vx += (wish_x - p->body.vx) * accel;
    p->body.vz += (wish_z - p->body.vz) * accel;

    // --- Jumping and falling -----------------------------------------
    //
    // MOVE WITH THE VELOCITY, THEN UPDATE IT. Applying gravity before
    // the move instead spends the first tick of a jump decelerating,
    // which costs a third of the height: the same three constants gave
    // an apex of 0.83 blocks that way and 1.25 this way. A jump that
    // cannot clear one block is not a jump, and it is not obvious from
    // reading the code -- only from simulating the arc.
    if (in_water) {
        // Swimming: an impulse added every tick the key is held, which
        // the water's drag turns into a steady rise (player.h). Jump
        // goes up, sneak goes down; let go and you sink slowly.
        if (act_held(mask, SM_JUMP)) p->body.vy += PL_SWIM_UP;
        else if (act_held(mask, SM_SNEAK)) p->body.vy -= PL_SWIM_UP;
    } else if (act_held(mask, SM_JUMP) && p->body.on_ground) {
        p->body.vy = PL_JUMP;
    }

    p->in_air_last = !p->body.on_ground;
    phys_move(&p->body, (double)p->body.vx, (double)p->body.vy, (double)p->body.vz);

    // A stopped body should not keep a velocity it cannot use, or it
    // shoots off the moment the obstruction goes away.
    if (p->body.hit_x) p->body.vx = 0.0f;
    if (p->body.hit_z) p->body.vz = 0.0f;

    // The vertical for the NEXT tick, including the landing and
    // head-bump cases. In physics.c so that the host test runs the
    // same code rather than a copy of it.
    if (in_water) {
        phys_gravity(&p->body, PL_WATER_GRAV, PL_WATER_DRAG, PL_WATER_TERM);
    } else {
        phys_gravity(&p->body, PL_GRAVITY, PL_DRAG, PL_TERMINAL);
    }

    // --- The hotbar ---------------------------------------------------
    for (int i = 0; i < INV_HOTBAR; i++) {
        if (act_held(pressed, (sm_action_t)(SM_SLOT1 + i))) {
            p->inv.selected = i;
            p->mining       = false;  // switching tools abandons the dig
        }
    }

    // --- What the crosshair is on ------------------------------------
    double const ex = p->body.x, ey = p->body.y + (double)PHYS_PLAYER_EYE, ez = p->body.z;
    float        dx, dy, dz;
    ray_forward(p->yaw, p->pitch, &dx, &dy, &dz);
    // Anything that can be pointed at, not only solids: torches, flowers
    // and tall grass have to be breakable too (F-56).
    p->aim_valid = ray_pick(ex, ey, ez, dx, dy, dz, RAY_REACH, RAY_PICKABLE, &p->aim);

    uint16_t const held = inv_held(&p->inv)->item;

    // --- Hitting a creature, which is a TAP -------------------------
    //
    // Breaking is held down and a blow is not: an animal takes one hit
    // per press, which is what stops holding the key down being a
    // butchery machine and what makes a weapon worth having later.
    //
    // A CREATURE IN FRONT OF A BLOCK WINS. Both are picked along the
    // same ray and the nearer one takes the swing -- otherwise a cow
    // standing against a wall is unhittable, and a cow in the open is
    // fine, which is the sort of difference nobody can see and
    // everybody notices.
    float     mob_d = 0.0f;
    int const aimed_mob = mob_pick(ex, ey, ez, dx, dy, dz, RAY_REACH, &mob_d);
    p->hit_mob          = (aimed_mob >= 0 && (!p->aim_valid || (double)mob_d <= (double)p->aim.dist)) ? aimed_mob : -1;

    if (p->hit_mob >= 0 && act_held(mask, SM_ATTACK)) {
        // Swinging at an animal is not digging: whatever is behind it
        // keeps its cracks, and the key being held changes nothing
        // until it is let go and pressed again.
        p->mining = false;
        if (act_held(pressed, SM_ATTACK)) {
            if (mob_hit(p->hit_mob, mob_damage_of(held), p->body.x, p->body.z)) p->hit_mob = -1;
        }
    } else

    // --- Breaking, which is HELD ------------------------------------
    //
    // A block takes item_break_ticks() of them. That is what makes
    // hardness and the tool in hand mean anything, and it is what the
    // crack overlay animates once it is retargeted. Progress belongs to
    // a CELL: look away and it is abandoned, which is the behaviour
    // everyone expects and nobody states.
    if (p->aim_valid && act_held(mask, SM_ATTACK)) {
        bool const same = p->mining && p->mine_x == p->aim.x && p->mine_y == p->aim.y && p->mine_z == p->aim.z;
        if (!same) {
            p->mining      = true;
            p->mine_x      = p->aim.x;
            p->mine_y      = p->aim.y;
            p->mine_z      = p->aim.z;
            p->mine_ticks  = 0;
            p->mine_needed = item_break_ticks(p->aim.block, held);
        }
        // Will it break at all? Asked every tick the key is held, not
        // once, so the message keeps showing for as long as they keep
        // swinging at it rather than blinking once and going away.
        if (block_tool_required(p->aim.block) && !item_can_harvest(p->aim.block, held)) {
            block_def_t const* d = block_def(p->aim.block);
            p->needs_tool        = item_tool_for(d->tool, d->tool_level);
            p->mining            = false;
            p->mine_ticks        = 0;
            if ((p->mine_ticks % SWING_TICKS) == 0) sfx_play(SFX_DENY);
            return;
        }
        uint8_t const aimed = p->aim.block;
        if (p->mine_needed < 0) {
            p->mining = false;  // unbreakable: bedrock, or the edge of the world
        } else if (++p->mine_ticks >= p->mine_needed) {
            break_result_t const r = interact_break(p->aim.x, p->aim.y, p->aim.z, held);
            if (r.ok) {
                trace_edit('B', p->aim.x, p->aim.y, p->aim.z, block_def(aimed)->name, r.felled);
                // One use per BREAK, not per felled block: a tree is
                // one swing of the axe, not forty.
                inv_wear_held(&p->inv, 1);
                // A whole tree coming down is a different sound from one
                // block breaking, and it should be: the rule that made it
                // fall is the game's one deliberate departure from
                // Minecraft (Part F), so it is worth hearing.
                if (r.was_tree && r.felled > 1) sfx_play(SFX_FELL);
                else sfx_play_break(aimed);
            }
            p->mining    = false;
            p->aim_valid = false;  // whatever was aimed at is gone
        } else if ((p->mine_ticks % SWING_TICKS) == 1) {
            // The tool striking the block, at the rate Fred's arm swings
            // -- not once a tick, which would be a buzz rather than
            // a tapping.
            sfx_play_pitched(SFX_HIT, block_sound(aimed) == SND_STONE ? 3.0f : 0.0f);
        }
    } else {
        p->mining = false;
    }

    // --- Using and placing, which are the same tap -------------------
    //
    // A block that OPENS something wins over placing against it, or a
    // table with planks in hand could never be opened at all -- which
    // is Minecraft's rule too, and the reason sneaking exists there.
    // --- The line in the water --------------------------------------
    //
    // It fishes while the rod is in hand and comes straight back in
    // when it is not: switching to a pickaxe with a float bobbing in
    // the river would leave a thing in the world with nothing holding
    // it.
    if (p->fish.out && held != ITEM_ROD) fishing_reset(&p->fish);
    if (p->fish.out) {
        double const fdx = (double)p->fish.x + 0.5 - p->body.x;
        double const fdz = (double)p->fish.z + 0.5 - p->body.z;
        // Walked away from it: the same rule Minecraft has, and it
        // stops a float being left across the map.
        if (fdx * fdx + fdz * fdz > 14.0 * 14.0) fishing_reset(&p->fish);
    }
    p->fish_clock++;
    if (fishing_tick(&p->fish)) {
        // THE FLOAT DIPS. A splash, low and short, which is the only
        // warning the strike window gives.
        sfx_play_pitched(SFX_STEP_SPLASH, -4.0f);
    }

    if (act_held(pressed, SM_USE)) {
        bool used_creature = false;

        // THE ROD GETS THE KEY BEFORE ANYTHING ELSE, because a cast
        // goes at water and water is the one thing the ordinary use
        // path also wants (a bucket). A rod in hand means fishing.
        if (held == ITEM_ROD) {
            int32_t   wx = 0, wy = -1, wz = 0;
            ray_hit_t wet;
            if (ray_pick(ex, ey, ez, dx, dy, dz, RAY_REACH, RAY_FLUID, &wet) && block_liquid(wet.block)) {
                wx = wet.x;
                wy = wet.y;
                wz = wet.z;
            }
            fish_use_t const fu = fishing_use(&p->fish, wx, wy, wz, inv_count(&p->inv, ITEM_WORM) > 0,
                                              (uint32_t)p->fish_clock);
            switch (fu.what) {
                case FISH_CAST:
                    // ONE WORM PER CAST (the user), and it goes now --
                    // not when something bites.
                    inv_take(&p->inv, ITEM_WORM, 1);
                    sfx_play_pitched(SFX_STEP_SPLASH, 2.0f);
                    break;
                case FISH_CAUGHT: {
                    int const left = inv_add(&p->inv, fu.item, 1, 0);
                    if (left > 0) {
                        item_entity_spawn((int32_t)floor(p->body.x), (int32_t)floor(p->body.y) + 1,
                                          (int32_t)floor(p->body.z), fu.item, left, 0);
                    }
                    inv_mark_seen(&p->inv, fu.item);
                    p->fish_caught = fu.item;
                    sfx_play(SFX_PICKUP);
                } break;
                case FISH_NO_WORM:
                case FISH_NO_WATER:
                case FISH_TOO_SOON: sfx_play(SFX_DENY); break;
                case FISH_REELED: sfx_play_pitched(SFX_STEP_SPLASH, 6.0f); break;
                default: break;
            }
            if (fu.what != FISH_NOTHING) {
                p->fish_msg       = fu.what;
                p->fish_msg_ticks = USE_MSG_TICKS;
            }
            used_creature = true;  // the rod has answered; nothing else gets the key
        }
        // A CREATURE IN FRONT OF EVERYTHING. Use on a cow with a bucket
        // is a milking and not a water bucket looking for a lake, and
        // use on a dog with a bone is a taming and not a bone being
        // placed (it is not a block, but the rule has to be stated
        // where the order is decided).
        if (p->hit_mob >= 0) {
            mob_use_result_t const mu = mob_use(p->hit_mob, held);
            if (mu.what != MOB_USE_NOTHING) {
                inv_slot_t* s = inv_held(&p->inv);
                if (mu.becomes != 0) {
                    s->item  = mu.becomes;
                    s->count = 1;
                    s->wear  = 0;
                    inv_mark_seen(&p->inv, mu.becomes);
                }
                if (mu.consume) inv_consume_held(&p->inv);
                // SHEARS WEAR BY ONE PER FLEECE, like a hoe per plot.
                if (mu.wear) inv_wear_held(&p->inv, 1);
                p->mob_msg       = mu.what;
                p->mob_msg_ticks = USE_MSG_TICKS;
                sfx_play(mu.what == MOB_USE_MILKED ? SFX_PICKUP : SFX_CLICK);
                used_creature = true;
            }
        }
        if (!used_creature) {
            // THE HELD ITEM GETS FIRST REFUSAL, and it gets it before the
            // crosshair is consulted at all -- a bucket casts its own ray,
            // because the one that drew the highlight box looked straight
            // through the water it is after (raycast.h, RAY_FLUID).
            use_result_t const u = interact_use_item(ex, ey, ez, dx, dy, dz, held);
            if (u.acted) {
                // The stack is swapped IN PLACE: a bucket does not stack,
                // empty or full (items.c), so there is exactly one of them
                // here and no second slot to find.
                inv_slot_t* s = inv_held(&p->inv);
                if (u.becomes != 0) {
                    s->item  = u.becomes;
                    s->count = 1;
                    s->wear  = 0;
                    inv_mark_seen(&p->inv, u.becomes);
                }
                // A SEED OR A COMPOST IS SPENT, one from the stack -- the
                // other way a use can change what is in the hand, and the
                // ordinary one for everything that is not a bucket.
                if (u.consume) inv_consume_held(&p->inv);
                // A HOE WEARS LIKE ANY OTHER TOOL, once per job. Tilling is
                // the only thing it does, so this is the whole of its life.
                if (u.wear) inv_wear_held(&p->inv, 1);
                p->use_msg       = USE_SAID_NOTHING;
                p->use_msg_ticks = 0;
                trace_edit('U', u.x, u.y, u.z, block_def(u.block)->name, 1);
                if (u.sound != SND_NONE) sfx_play_place(u.block);
            } else if (u.msg != USE_SAID_NOTHING) {
                // It refused, and it has a reason. Held for about two
                // seconds, and repeated presses keep it up rather than
                // making it flicker.
                p->use_msg       = u.msg;
                p->use_msg_ticks = USE_MSG_TICKS;
                sfx_play(SFX_DENY);
            } else if (p->aim_valid) {
                if (block_usable(p->aim.block)) {
                    // A GATE OPENS RATHER THAN OPENING SOMETHING. It is the
                    // first usable block with no screen behind it, so it is
                    // answered here and never reaches main.c.
                    if (interact_toggle_gate(p->aim.x, p->aim.y, p->aim.z)) {
                        trace_edit('U', p->aim.x, p->aim.y, p->aim.z, block_def(p->aim.block)->name, 1);
                        sfx_play_place(BLK_FENCE_GATE);
                    } else {
                        p->used_block = p->aim.block;
                    }
                } else {
                    uint8_t const block = item_block(held);
                    if (block != BLK_AIR && interact_place_dir(&p->aim, block, &p->body, dx, dz)) {
                        trace_edit('P', p->aim.px, p->aim.py, p->aim.pz, block_def(block)->name, 1);
                        inv_consume_held(&p->inv);
                        sfx_play_place(block);
                    } else if (block == BLK_BED_FOOT) {
                        // A BED WANTS TWO CELLS and this spot had one.
                        // Every other placement that fails is obvious --
                        // the cell is full, or you are standing in it --
                        // but half a bed's worth of room looks exactly
                        // like enough.
                        p->use_msg       = USE_NO_ROOM;
                        p->use_msg_ticks = USE_MSG_TICKS;
                        sfx_play(SFX_DENY);
                    }
                }
            }
        }
    }

    // --- Dropping what is held ---------------------------------------
    //
    // THROWN, not placed. A dropped item lands inside the 1.4-block
    // pickup radius whichever way you face -- you cannot throw a thing
    // further than your own arm in one tick -- so it is the pickup
    // DELAY that makes G work at all, not the distance. Two seconds is
    // long enough to walk away from.
    if (act_held(pressed, SM_DROP)) {
        inv_slot_t* s = inv_held(&p->inv);
        if (s->item != 0) {
            double const ox = p->body.x + (double)(dx * 0.4f);
            double const oy = p->body.y + (double)PHYS_PLAYER_EYE - 0.3;
            double const oz = p->body.z + (double)(dz * 0.4f);
            if (item_entity_throw(ox, oy, oz, s->item, 1, s->wear, dx * 0.22f, 0.12f, dz * 0.22f, ITEM_THROW_DELAY) >
                0) {
                inv_consume_held(&p->inv);
            }
        }
    }

    // --- What is lying about -----------------------------------------
    if (item_entity_tick(&p->inv, p->body.x, p->body.y, p->body.z) > 0) sfx_play(SFX_PICKUP);
}

void player_eye(player_t const* p, float alpha, double* x, double* y, double* z, float* yaw, float* pitch) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    double const a = (double)alpha;
    if (x != NULL) *x = p->prev_x + (p->body.x - p->prev_x) * a;
    if (y != NULL) *y = p->prev_y + (p->body.y - p->prev_y) * a + (double)PHYS_PLAYER_EYE;
    if (z != NULL) *z = p->prev_z + (p->body.z - p->prev_z) * a;
    // The view angles interpolate too, or turning is visibly steppy at
    // 20 Hz however smooth the position is.
    if (yaw != NULL) *yaw = p->prev_yaw + (p->yaw - p->prev_yaw) * alpha;
    if (pitch != NULL) *pitch = p->prev_pitch + (p->pitch - p->prev_pitch) * alpha;
}
