// =====================================================================
//  SynthMiner  --  the kitchen stove (see stove.h)
// =====================================================================

#include "game/stove.h"

#include "game/furnace.h"  // furnace_fuel_ticks: fuel is fuel, wherever it burns
#include "items/items.h"

int stove_dish_count(void) {
    int n = 0;
    for (int i = 0; i < recipe_count(); i++) {
        if (recipe_at(i)->station == RS_STOVE) n++;
    }
    return n;
}

recipe_t const* stove_dish_at(int n) {
    if (n < 0) return NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != RS_STOVE) continue;
        if (n-- == 0) return r;
    }
    return NULL;
}

int stove_chest_count(blockent_t const* chest, uint16_t item) {
    if (chest == NULL || item == 0) return 0;
    int n = 0;
    for (int i = 0; i < BE_SLOTS; i++) {
        inv_slot_t const* s = &chest->slot[i];
        if (s->item == item) n += s->count;
    }
    return n;
}

// Take `n` of `item` out of the chest, from the front. Returns how many
// it actually got, which the caller has already checked is all of them.
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

// Can the chest supply every ingredient of `r` once over?
static bool chest_can(blockent_t const* chest, recipe_t const* r) {
    if (r == NULL) return false;
    for (int i = 0; i < r->n_in; i++) {
        if (stove_chest_count(chest, r->in[i].item) < r->in[i].count) return false;
    }
    return true;
}

recipe_t const* stove_pick(blockent_t const* be, blockent_t const* chest) {
    if (be == NULL || be->kind != BE_STOVE || be->pick == 0) return NULL;

    // TWO ROWS CAN MAKE THE SAME DISH -- the pizza's real and fake
    // sausage (recipes.c) -- and which of them a stove is using is not
    // a thing the player chose or should have to. The first row the
    // chest can actually supply wins; failing that, the first row at
    // all, so the screen has something to name as missing.
    recipe_t const* first = NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != RS_STOVE || r->out != be->pick) continue;
        if (first == NULL) first = r;
        if (chest_can(chest, r)) return r;
    }
    return first;
}

void stove_set_pick(blockent_t* be, uint16_t out) {
    if (be == NULL || be->kind != BE_STOVE) return;
    if (be->pick == out) return;
    be->pick = out;
    // CHANGING YOUR MIND THROWS AWAY THE PART-COOKED DISH and nothing
    // else. The ingredients for it were never taken (they come out of
    // the chest when it finishes), so all that is lost is the time --
    // and the fuel keeps burning, exactly as a furnace's does when its
    // input is swapped.
    be->cook = 0;
}

// Can the output slot take `n` more of `out`?
static bool out_has_room(blockent_t const* be, uint16_t out, int n) {
    inv_slot_t const* o = &be->slot[BE_STOVE_OUT];
    if (o->item == 0 || o->count == 0) return true;
    if (o->item != out) return false;
    return o->count + n <= item_def(out).stack_max;
}

static void out_add(blockent_t* be, uint16_t out, int n) {
    inv_slot_t* o = &be->slot[BE_STOVE_OUT];
    if (o->item == 0 || o->count == 0) {
        o->item  = out;
        o->count = (uint8_t)n;
        o->wear  = 0;
        return;
    }
    o->count = (uint8_t)(o->count + n);
}

// Take one fuel item and light it. False if there was none.
static bool light_next(blockent_t* be) {
    inv_slot_t* f = &be->slot[BE_STOVE_FUEL];
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

uint16_t stove_missing(blockent_t const* be, blockent_t const* chest, int* need) {
    if (need != NULL) *need = 0;
    recipe_t const* r = stove_pick(be, chest);
    if (r == NULL) return 0;
    for (int i = 0; i < r->n_in; i++) {
        int const have = stove_chest_count(chest, r->in[i].item);
        if (have >= r->in[i].count) continue;
        if (need != NULL) *need = r->in[i].count - have;
        return r->in[i].item;
    }
    return 0;
}

stove_idle_t stove_idle_reason(blockent_t const* be, blockent_t const* chest) {
    if (be == NULL || be->kind != BE_STOVE) return STOVE_IDLE_NO_PICK;
    recipe_t const* r = stove_pick(be, chest);
    if (r == NULL) return STOVE_IDLE_NO_PICK;
    // THE ORDER OF THE NEXT TWO MATTERS. With no chest at all, every
    // ingredient is missing -- and "you are short of 2 wheat" is a
    // useless thing to say to somebody whose chest has been broken.
    if (chest == NULL) return STOVE_IDLE_NO_CHEST;
    if (stove_missing(be, chest, NULL) != 0) return STOVE_IDLE_MISSING;
    if (!out_has_room(be, r->out, r->out_n)) return STOVE_IDLE_FULL;
    if (be->burn_left == 0 && !furnace_is_fuel(be->slot[BE_STOVE_FUEL].item)) return STOVE_IDLE_NO_FUEL;
    return STOVE_IDLE_NONE;
}

bool stove_busy(blockent_t const* be, blockent_t const* chest) {
    return stove_idle_reason(be, chest) == STOVE_IDLE_NONE;
}

int stove_progress_pct(blockent_t const* be) {
    if (be == NULL) return 0;
    int const pct = (int)((uint32_t)be->cook * 100u / STOVE_COOK_TICKS);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

void stove_catch_up(blockent_t* be, blockent_t* chest, uint32_t now) {
    if (be == NULL || be->kind != BE_STOVE) return;

    // Time only ever runs forward: a stamp from the future (a restored
    // save, a test that rewinds the clock) buys no free dinner. The
    // furnace says the same thing at length.
    uint32_t elapsed = now >= be->stamp ? now - be->stamp : 0;
    be->stamp        = now;

    // ONCE ROUND PER EVENT, not per tick -- fuel lit, dish finished,
    // work stopped. A stove left for a week costs the same handful of
    // iterations as one left for a minute.
    while (elapsed > 0) {
        recipe_t const* r = stove_pick(be, chest);
        if (r == NULL || chest == NULL || !chest_can(chest, r) || !out_has_room(be, r->out, r->out_n)) {
            // Nothing it can make. The fire is NOT spent while it waits:
            // nobody has ever been glad of a machine that burned coal
            // into an empty pan.
            be->cook = 0;
            break;
        }

        if (be->burn_left == 0 && !light_next(be)) {
            be->cook = 0;  // cold: the part-cooked dish does not keep
            break;
        }

        uint32_t const need = STOVE_COOK_TICKS - be->cook;
        uint32_t       step = elapsed;
        if (step > need) step = need;
        if (step > be->burn_left) step = be->burn_left;

        be->cook      = (uint16_t)(be->cook + step);
        be->burn_left = (uint16_t)(be->burn_left - step);
        elapsed -= step;

        if (be->cook >= STOVE_COOK_TICKS) {
            be->cook = 0;
            // THE INGREDIENTS GO NOW, at the end. Reserving them at the
            // start would make a stove a thing that eats out of your
            // chest and gives nothing back if you break it half way.
            for (int i = 0; i < r->n_in; i++) chest_take(chest, r->in[i].item, r->in[i].count);
            out_add(be, r->out, r->out_n);
        }
    }

    if (be->burn_left == 0) be->burn_max = 0;
}
