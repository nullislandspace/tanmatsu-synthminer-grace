// =====================================================================
//  SynthMiner  --  the smithing forge (see forge.h)
// ---------------------------------------------------------------------
//  The stove's shape with two differences, both of them the user's:
//  a job that makes exactly ONE item, and a job that keeps its progress
//  when the fuel runs out.
// =====================================================================

#include "game/forge.h"

#include <string.h>

#include "game/furnace.h"  // furnace_is_fuel / furnace_fuel_ticks: the forge burns the furnace's way
#include "items/items.h"

// --- What it can make ---------------------------------------------------

int forge_item_count(void) {
    int n = 0;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->station == RS_SMITH) n++;
    }
    return n;
}

recipe_t const* forge_item_at(int n) {
    if (n < 0) return NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != RS_SMITH) continue;
        if (n-- == 0) return r;
    }
    return NULL;
}

uint32_t forge_job_ticks(uint16_t out) {
    if (out == 0) return 0;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != RS_SMITH || r->out != out) continue;
        // Iron's five are a day and titanium's five are two (D-158). It is
        // the METAL that sets the clock, not the piece: a titanium sword is
        // as long to make as a titanium chestplate.
        return r->in[0].item == ITEM_TITANIUM_INGOT ? 2u * DAY_TICKS : DAY_TICKS;
    }
    return 0;
}

recipe_t const* forge_pick(blockent_t const* be) {
    if (be == NULL || be->kind != BE_SMITH || be->pick == 0) return NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station == RS_SMITH && r->out == be->pick) return r;
    }
    return NULL;  // a pick this build does not have reads as "nothing chosen"
}

// --- The slots ----------------------------------------------------------

static bool out_has_room(blockent_t const* be, uint16_t out, int n) {
    inv_slot_t const* o = &be->slot[BE_SMITH_OUT];
    if (o->item == 0 || o->count == 0) return true;
    if (o->item != out) return false;
    return o->count + n <= item_def(out).stack_max;
}

static void out_add(blockent_t* be, uint16_t out, int n) {
    inv_slot_t* o = &be->slot[BE_SMITH_OUT];
    if (o->item == 0 || o->count == 0) {
        o->item  = out;
        o->count = (uint8_t)n;
        o->wear  = 0;
        return;
    }
    o->count = (uint8_t)(o->count + n);
}

int forge_metal_count(blockent_t const* be) {
    if (be == NULL) return 0;
    inv_slot_t const* m = &be->slot[BE_SMITH_METAL];
    return (m->item != 0 && m->count > 0) ? m->count : 0;
}

int forge_chest_count(blockent_t const* chest, uint16_t item) {
    if (chest == NULL || item == 0) return 0;
    int n = 0;
    for (int i = 0; i < BE_SLOTS; i++) {
        inv_slot_t const* s = &chest->slot[i];
        if (s->item == item) n += s->count;
    }
    return n;
}

// Take `n` of `item` out of the chest, from the front. The caller has
// already checked that the chest has all of them.
static int chest_take(blockent_t* chest, uint16_t item, int n) {
    if (chest == NULL) return 0;
    int const want = n;
    for (int i = 0; i < BE_SLOTS && n > 0; i++) {
        inv_slot_t* s = &chest->slot[i];
        if (s->item != item) continue;
        int const t = s->count < n ? s->count : n;
        s->count    = (uint8_t)(s->count - t);
        n -= t;
        if (s->count == 0) {
            s->item = 0;
            s->wear = 0;
        }
    }
    return want - n;
}

// Take one fuel item and light it. FALSE if there was none.
//
// ONE ITEM AT A TIME, which is the furnace's own model and the reason 15
// coal is 15 separate 1600-tick fires rather than one 24000-tick one:
// `burn_left` never holds a day at all, and a forge that is fed as the fire
// goes down behaves the same whether it was fed 1 coal or 64.
static bool light_next(blockent_t* be) {
    inv_slot_t* f = &be->slot[BE_SMITH_FUEL];
    if (f->item == 0 || f->count == 0) return false;
    uint16_t const ticks = furnace_fuel_ticks(f->item);
    if (ticks == 0) return false;

    be->burn_left = ticks;
    be->burn_max  = ticks;
    if (--f->count == 0) {
        f->item = 0;
        f->wear = 0;
    }
    return true;
}

// --- The selector -------------------------------------------------------

bool forge_set_pick(blockent_t* be, uint16_t out) {
    if (be == NULL || be->kind != BE_SMITH) return false;
    // D-160, and it is not a preference. The ingots in the forge's own slot
    // were spent on the item the selector names; letting the selector move
    // mid-job would turn a half-made chestplate into a whole sword.
    if (be->job > 0) return false;
    if (out != 0 && forge_job_ticks(out) == 0) return false;  // not a forge item
    if (be->pick == out) return true;
    be->pick = out;
    return true;
}

// --- Start --------------------------------------------------------------

bool forge_start(blockent_t* be, blockent_t* chest) {
    if (be == NULL || be->kind != BE_SMITH) return false;
    if (be->job > 0) return false;  // one job at a time, and it is already running

    recipe_t const* r = forge_pick(be);
    if (r == NULL) return false;
    if (chest == NULL) return false;
    if (!out_has_room(be, r->out, r->out_n)) return false;

    int const need = r->in[0].count;
    if (forge_chest_count(chest, r->in[0].item) < need) return false;

    // SOMETHING TO BURN, OR THE JOB WOULD ONLY WAIT (D-160). Mid-job the
    // forge is allowed to go cold and keep its progress; at Start it is
    // not, because "Start" is the one moment the player is told whether
    // the job can begin. Without this line the footer's NO_FUEL and
    // READY would both be followed by a Start that works, which makes the
    // message decorative. A fire still burning from the last item counts.
    if (be->burn_left == 0 && !furnace_is_fuel(be->slot[BE_SMITH_FUEL].item)) return false;

    // THE METAL MOVES NOW (D-160). Out of the chest and into the forge, where
    // it belongs to the job: rob the chest afterwards and the sword still
    // lands, break the forge afterwards and the ingots come back out of it.
    int const got = chest_take(chest, r->in[0].item, need);
    if (got < need) return false;  // cannot happen after the count above; never spend twice
    inv_slot_t* m  = &be->slot[BE_SMITH_METAL];
    m->item        = r->in[0].item;
    m->count       = (uint8_t)got;
    m->wear        = 0;

    // ONE TICK SPENT, which is what distinguishes "told to make something"
    // from "idle" in a single field. Zero would be both, and the difference
    // is the whole machine.
    be->job = 1u;
    return true;
}

// --- Running ------------------------------------------------------------

bool forge_busy(blockent_t const* be) {
    if (be == NULL || be->kind != BE_SMITH || be->job == 0) return false;
    uint32_t const len = forge_job_ticks(be->pick);
    return len != 0 && be->job < len;
}

int forge_progress_pct(blockent_t const* be) {
    if (be == NULL) return 0;
    uint32_t const len = forge_job_ticks(be->pick);
    if (len == 0) return 0;
    int const pct = (int)(be->job * 100u / len);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

uint16_t forge_missing(blockent_t const* be, blockent_t const* chest, int* need) {
    if (need != NULL) *need = 0;
    recipe_t const* r = forge_pick(be);
    if (r == NULL) return 0;
    int const have = forge_chest_count(chest, r->in[0].item);
    if (have >= r->in[0].count) return 0;
    if (need != NULL) *need = r->in[0].count - have;
    return r->in[0].item;
}

forge_idle_t forge_idle_reason(blockent_t const* be, blockent_t const* chest) {
    if (be == NULL || be->kind != BE_SMITH) return FORGE_IDLE_NO_PICK;
    recipe_t const* r = forge_pick(be);
    if (r == NULL) return FORGE_IDLE_NO_PICK;

    if (be->job > 0) {
        uint32_t const len = forge_job_ticks(be->pick);
        // A finished job that has nowhere to put the result: it waits with
        // the item made rather than making it twice.
        if (len != 0 && be->job >= len) return out_has_room(be, r->out, r->out_n) ? FORGE_IDLE_NONE : FORGE_IDLE_FULL;
        // Starved, and keeping its progress (D-160).
        if (be->burn_left == 0 && !furnace_is_fuel(be->slot[BE_SMITH_FUEL].item)) return FORGE_IDLE_WAITING;
        return FORGE_IDLE_NONE;
    }

    // Idle: what would stop Start from working, in the order a player would
    // ask about them. THE ORDER OF THE NEXT TWO MATTERS for the stove's
    // reason -- with no chest at all the metal is missing, and "you are
    // short of 4 iron ingots" is a useless thing to say to somebody whose
    // chest has been broken.
    if (chest == NULL) return FORGE_IDLE_NO_CHEST;
    if (forge_missing(be, chest, NULL) != 0) return FORGE_IDLE_NO_METAL;
    if (!out_has_room(be, r->out, r->out_n)) return FORGE_IDLE_FULL;
    if (be->burn_left == 0 && !furnace_is_fuel(be->slot[BE_SMITH_FUEL].item)) return FORGE_IDLE_NO_FUEL;
    return FORGE_IDLE_READY;
}

void forge_catch_up(blockent_t* be, uint32_t now) {
    if (be == NULL || be->kind != BE_SMITH) return;

    // Time only ever runs forward: a stamp from the future (a restored save,
    // a test that rewinds the clock) buys no free sword.
    uint32_t elapsed = now >= be->stamp ? now - be->stamp : 0;
    be->stamp        = now;

    // ONCE ROUND PER EVENT, not per tick. A forge left for a week costs the
    // same handful of iterations as one left for a minute -- and ONE ITEM,
    // because that is the machine the user described. The stove's loop would
    // cook as many dishes as the clock allows; this one stops at the end of
    // the thing it was told to make.
    while (elapsed > 0 && be->job > 0) {
        recipe_t const* r = forge_pick(be);
        if (r == NULL) {
            be->job = 0;  // a pick this build cannot name: the job goes, the metal stays
            break;
        }
        uint32_t const len = forge_job_ticks(be->pick);
        if (len == 0) {
            be->job = 0;
            break;
        }

        if (!out_has_room(be, r->out, r->out_n)) break;  // finished or waiting; the result is not lost

        if (forge_metal_count(be) == 0) break;  // nothing committed: nothing to work on

        // THE LINE THAT IS NOT THE FURNACE'S. furnace_catch_up() and
        // stove_catch_up() both zero their progress here -- a furnace out of
        // coal mid-smelt throws away the 200 ticks it had spent. A forge
        // keeps 18000 of its 24000 and finishes when the next coal arrives
        // (D-160), so there is no `be->job = 0` in this branch.
        if (be->burn_left == 0 && !light_next(be)) break;

        uint32_t       step = elapsed;
        uint32_t const need = len - be->job;
        if (step > need) step = need;
        if (step > be->burn_left) step = be->burn_left;
        if (step == 0) break;  // no fuel and none to light: the job waits

        be->job       = be->job + step;
        be->burn_left = (uint16_t)(be->burn_left - step);
        elapsed -= step;

        if (be->job >= len) {
            // THE JOB ENDS. Exactly one item, the committed metal is spent,
            // and the forge goes back to being idle rather than starting
            // another one on its own.
            be->job = 0;
            inv_slot_t* m = &be->slot[BE_SMITH_METAL];
            m->item       = 0;
            m->count      = 0;
            m->wear       = 0;
            out_add(be, r->out, r->out_n);
        }
    }

    if (be->burn_left == 0) be->burn_max = 0;
}
