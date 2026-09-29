// =====================================================================
//  SynthMiner  --  the composter (see composter.h)
// =====================================================================

#include "game/composter.h"

#include "common/rng.h"
#include "items/items.h"

bool composter_accepts(uint16_t item) {
    return item != 0 && item_def(item).compost != 0;
}

int composter_worms_for(int32_t x, int32_t y, int32_t z, uint32_t n) {
    // A flat 0, 1 or 2 -- one worm on average per unit, so a stack of
    // scraps is about a stack-and-a-bit of bait over sixty-four days.
    uint32_t const h = sm_hash3(x, y, z, 0x0C0FFEEu ^ (n * 2654435761u));
    return (int)(h % (uint32_t)(COMPOST_WORMS_MAX + 1));
}

// Can the compost slot take one more?
static bool out_has_room(blockent_t const* be) {
    inv_slot_t const* o = &be->slot[BE_COMPOST_OUT];
    if (o->item == 0 || o->count == 0) return true;
    if (o->item != ITEM_COMPOST) return false;
    return o->count < item_def(ITEM_COMPOST).stack_max;
}

static void add_to(blockent_t* be, int slot, uint16_t item, int n) {
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

void composter_catch_up(blockent_t* be, uint32_t now) {
    if (be == NULL || be->kind != BE_COMPOST) return;

    // Time only runs forward -- the same rule as the furnace, and for
    // the same reason: a stamp from the future must not buy free compost.
    uint32_t elapsed = now >= be->stamp ? now - be->stamp : 0;

    // Once per UNIT, not once per tick: a box left for a fortnight costs
    // the handful of iterations its input can actually pay for.
    bool idle = false;
    while (elapsed >= COMPOST_TICKS) {
        inv_slot_t* in = &be->slot[BE_COMPOST_INPUT];
        if (in->item == 0 || in->count == 0 || !composter_accepts(in->item) || !out_has_room(be)) {
            idle = true;  // nothing to rot, or nowhere to put it
            break;
        }

        elapsed -= COMPOST_TICKS;

        // WHICH UNIT THIS IS, so a box does not give the same number of
        // worms for ever. The world clock at the moment the unit
        // finished is a number a save and a replay agree on.
        uint32_t const unit = (now - elapsed) / COMPOST_TICKS;
        add_to(be, BE_COMPOST_OUT, ITEM_COMPOST, 1);
        add_to(be, BE_COMPOST_WORMS, ITEM_WORM, composter_worms_for(be->x, be->y, be->z, unit));

        if (--in->count == 0) {
            in->item = 0;
            in->wear = 0;
        }
    }

    // THE REMAINDER IS KEPT, and that is the whole of the progress bar:
    // the stamp is left `elapsed` ticks in the past, so how far through
    // the current unit the box is can be read straight off it and there
    // is no second number to keep true (which is where the furnace needs
    // its `cook` field).
    //
    // A BOX WITH NOTHING IN IT BANKS NOTHING. Otherwise a composter left
    // empty for a week would turn its first scrap into compost the
    // instant it went in, which is not a machine, it is a lottery.
    be->stamp = idle ? now : now - elapsed;
}

composter_idle_t composter_idle_reason(blockent_t const* be) {
    if (be == NULL || be->kind != BE_COMPOST) return COMPOST_IDLE_NO_INPUT;
    inv_slot_t const* in = &be->slot[BE_COMPOST_INPUT];
    if (in->item == 0 || in->count == 0 || !composter_accepts(in->item)) return COMPOST_IDLE_NO_INPUT;
    if (!out_has_room(be)) return COMPOST_IDLE_FULL;
    return COMPOST_IDLE_NONE;
}

bool composter_busy(blockent_t const* be) {
    return composter_idle_reason(be) == COMPOST_IDLE_NONE;
}

int composter_progress_pct(blockent_t const* be, uint32_t now) {
    if (be == NULL || !composter_busy(be)) return 0;
    // Straight off the stamp: catch_up leaves it however far into the
    // current unit the box has got (above), so this is a subtraction
    // rather than a stored number that could disagree with it.
    uint32_t const since = now >= be->stamp ? now - be->stamp : 0u;
    uint32_t const part  = since % COMPOST_TICKS;
    int const      pct   = (int)((part * 100u) / COMPOST_TICKS);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
