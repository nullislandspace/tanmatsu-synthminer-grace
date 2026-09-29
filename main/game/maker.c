// =====================================================================
//  SynthMiner  --  the cheese maker and the sausage maker (see maker.h)
// =====================================================================

#include "game/maker.h"

#include "common/rng.h"
#include "items/items.h"

// THE TWO MACHINES, and a third would be a row. `extra_from` is what
// has to have gone in for the rare second output to be possible: a bone
// comes off the pig, so a vegetarian sausage never leaves one.
static maker_def_t const MAKERS[] = {
    {BE_CHEESE, RS_CHEESE, MAKER_DAY, 1, 0, 0},
    {BE_SAUSAGE, RS_SAUSAGE, MAKER_MINUTE, 2, ITEM_BONE, ITEM_PORK},
};

maker_def_t const* maker_def(uint8_t kind) {
    for (size_t i = 0; i < sizeof MAKERS / sizeof MAKERS[0]; i++) {
        if (MAKERS[i].kind == kind) return &MAKERS[i];
    }
    return NULL;
}

bool maker_accepts(uint8_t kind, uint16_t item) {
    maker_def_t const* d = maker_def(kind);
    if (d == NULL || item == 0) return false;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != d->station) continue;
        for (int j = 0; j < r->n_in; j++) {
            if (r->in[j].item == item) return true;
        }
    }
    return false;
}

uint16_t maker_returns(uint8_t kind, uint16_t item) {
    // A PAIL IS NOT AN INGREDIENT. Milk is what the barrel wants and
    // the bucket is only how it got there, so it goes back to whoever
    // carried it -- at once, not in a day's time (the user).
    if (kind == BE_CHEESE && item == ITEM_BUCKET_MILK) return ITEM_BUCKET;
    return 0;
}

// How many of `item` are in the input slots.
static int have(blockent_t const* be, int slots_in, uint16_t item) {
    int n = 0;
    for (int i = 0; i < slots_in; i++) {
        inv_slot_t const* s = &be->slot[BE_MAKER_IN_A + i];
        if (s->item == item) n += s->count;
    }
    return n;
}

static void take(blockent_t* be, int slots_in, uint16_t item, int n) {
    for (int i = 0; i < slots_in && n > 0; i++) {
        inv_slot_t* s = &be->slot[BE_MAKER_IN_A + i];
        if (s->item != item) continue;
        int const t = s->count < n ? s->count : n;
        s->count    = (uint8_t)(s->count - t);
        n -= t;
        if (s->count == 0) {
            s->item = 0;
            s->wear = 0;
        }
    }
}

recipe_t const* maker_match(blockent_t const* be) {
    if (be == NULL) return NULL;
    maker_def_t const* d = maker_def(be->kind);
    if (d == NULL) return NULL;
    for (int i = 0; i < recipe_count(); i++) {
        recipe_t const* r = recipe_at(i);
        if (r->station != d->station) continue;
        bool all = true;
        for (int j = 0; j < r->n_in && all; j++) {
            all = have(be, d->slots_in, r->in[j].item) >= r->in[j].count;
        }
        // FIRST MATCH WINS, and the table's order is what decides
        // between two rows that both fit -- pork and a red flower
        // before pork and a yellow one. They make the same sausage, so
        // there is nothing here for a player to notice.
        if (all) return r;
    }
    return NULL;
}

// Can `slot` take `n` more of `item`?
static bool slot_has_room(blockent_t const* be, int slot, uint16_t item, int n) {
    inv_slot_t const* o = &be->slot[slot];
    if (o->item == 0 || o->count == 0) return true;
    if (o->item != item) return false;
    return o->count + n <= item_def(item).stack_max;
}

static void slot_add(blockent_t* be, int slot, uint16_t item, int n) {
    if (n <= 0) return;
    inv_slot_t* o = &be->slot[slot];
    if (o->item == 0 || o->count == 0) {
        o->item  = item;
        o->count = (uint8_t)n;
        o->wear  = 0;
        return;
    }
    if (o->item != item) return;  // something else is in there: the yield is lost
    int const cap  = item_def(item).stack_max;
    int const room = cap - o->count;
    o->count       = (uint8_t)(o->count + (n < room ? n : room));
}

bool maker_extra_for(int32_t x, int32_t y, int32_t z, uint32_t n) {
    uint32_t const h = sm_hash3(x, y, z, 0x0B04E5u ^ (n * 2654435761u));
    return (h % MAKER_BONE_ONE_IN) == 0u;
}

maker_idle_t maker_idle_reason(blockent_t const* be) {
    if (be == NULL || maker_def(be->kind) == NULL) return MAKER_IDLE_NO_INPUT;
    recipe_t const* r = maker_match(be);
    if (r == NULL) return MAKER_IDLE_NO_INPUT;
    if (!slot_has_room(be, BE_MAKER_OUT, r->out, r->out_n)) return MAKER_IDLE_FULL;
    return MAKER_IDLE_NONE;
}

bool maker_busy(blockent_t const* be) {
    return maker_idle_reason(be) == MAKER_IDLE_NONE;
}

void maker_catch_up(blockent_t* be, uint32_t now) {
    maker_def_t const* d = be != NULL ? maker_def(be->kind) : NULL;
    if (d == NULL) return;

    // Time only runs forward: a stamp from the future must not buy a
    // free sausage (the furnace's rule, and the composter's).
    uint32_t elapsed = now >= be->stamp ? now - be->stamp : 0;

    bool idle = false;
    while (elapsed >= d->ticks) {
        recipe_t const* r = maker_match(be);
        if (r == NULL || !slot_has_room(be, BE_MAKER_OUT, r->out, r->out_n)) {
            idle = true;  // nothing that goes together, or nowhere to put it
            break;
        }
        elapsed -= d->ticks;

        // WHICH UNIT THIS IS -- the world clock at the moment it
        // finished, which a save and a replay agree on.
        uint32_t const unit = (now - elapsed) / (d->ticks > 0 ? d->ticks : 1u);

        // THE RARE ONE IS ROLLED BEFORE THE INGREDIENTS ARE TAKEN,
        // because whether it can happen depends on what went in.
        bool const bone = d->extra != 0 && have(be, d->slots_in, d->extra_from) > 0 &&
                          maker_extra_for(be->x, be->y, be->z, unit);

        for (int j = 0; j < r->n_in; j++) take(be, d->slots_in, r->in[j].item, r->in[j].count);
        slot_add(be, BE_MAKER_OUT, r->out, r->out_n);
        // A bone with nowhere to go is lost rather than held: the
        // machine is not a chest and must not stop working over one.
        if (bone && slot_has_room(be, BE_MAKER_EXTRA, d->extra, 1)) slot_add(be, BE_MAKER_EXTRA, d->extra, 1);
    }

    // The remainder is kept, and an idle machine banks nothing -- or a
    // barrel left empty for a week would turn its next bucket into
    // cheese on the spot (composter.c says the same thing at length).
    be->stamp = idle ? now : now - elapsed;
}

int maker_progress_pct(blockent_t const* be, uint32_t now) {
    maker_def_t const* d = be != NULL ? maker_def(be->kind) : NULL;
    if (d == NULL || d->ticks == 0 || !maker_busy(be)) return 0;
    uint32_t const since = now >= be->stamp ? now - be->stamp : 0u;
    uint32_t const part  = since % d->ticks;
    int const      pct   = (int)((part * 100u) / d->ticks);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

uint8_t maker_barrel_state(blockent_t const* be) {
    if (be == NULL || be->kind != BE_CHEESE) return BARREL_EMPTY;
    // WHAT IT SHOWS IS WHAT IT HOLDS, and the output wins: a barrel
    // with cheese standing in it and a fresh bucket waiting is a barrel
    // of cheese, which is the one that tells the player to come and
    // empty it.
    if (be->slot[BE_MAKER_OUT].item == ITEM_CHEESE && be->slot[BE_MAKER_OUT].count > 0) return BARREL_CHEESE;
    if (be->slot[BE_MAKER_IN_A].item != 0 && be->slot[BE_MAKER_IN_A].count > 0) return BARREL_MILK;
    return BARREL_EMPTY;
}
