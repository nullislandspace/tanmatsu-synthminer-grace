// =====================================================================
//  SynthMiner  --  fishing (see fishing.h)
// =====================================================================

#include "game/fishing.h"

#include <stddef.h>

#include "common/rng.h"
#include "items/items.h"
#include "world/blocks.h"

// WHAT COMES UP, and how often. The user named three fish and did not
// weight them, so the weights are the one guess here: sardines are the
// common one a river is full of, salmon the prize, and shrimp sit
// between -- which matters because the pizza wants TWO shrimp and
// nothing else wants a sardine at all yet.
static struct {
    uint16_t item;
    uint8_t  weight;
} const CATCH[] = {
    {ITEM_SARDINE, 5},
    {ITEM_SHRIMP, 3},
    {ITEM_SALMON, 2},
};

uint16_t fishing_catch_for(int32_t x, int32_t y, int32_t z, uint32_t seed, uint32_t n) {
    int total = 0;
    for (size_t i = 0; i < sizeof CATCH / sizeof CATCH[0]; i++) total += CATCH[i].weight;

    uint32_t const h = sm_hash3(x, y, z, seed ^ (n * 2654435761u) ^ 0xF15Du);
    int            r = (int)(h % (uint32_t)total);
    for (size_t i = 0; i < sizeof CATCH / sizeof CATCH[0]; i++) {
        r -= CATCH[i].weight;
        if (r < 0) return CATCH[i].item;
    }
    return CATCH[0].item;
}

// How long until the next bite, counted from the cast. Each one is its
// own roll, so a cast that has been out a while is no more likely to
// produce than a fresh one -- which is what makes waiting feel like
// waiting rather than like a progress bar.
static uint32_t wait_for(fishing_t const* f, uint32_t n) {
    uint32_t const h = sm_hash3(f->x, f->y, f->z, f->cast_at ^ (n * 40503u) ^ 0x8A17u);
    return FISH_WAIT_MIN + (h % FISH_WAIT_SPAN);
}

void fishing_reset(fishing_t* f) {
    if (f == NULL) return;
    f->out       = false;
    f->elapsed   = 0;
    f->next_bite = 0;
    f->bite_left = 0;
    f->bites     = 0;
}

fish_use_t fishing_use(fishing_t* f, int32_t wx, int32_t wy, int32_t wz, bool have_worm, uint32_t now) {
    fish_use_t r = {FISH_NOTHING, 0};
    if (f == NULL) return r;

    if (f->out) {
        // STRIKING. On the bite, a fish; off it, nothing -- and the
        // line STAYS OUT, because the worm bought the cast and a
        // mistimed press should cost a moment, not the bait.
        if (f->bite_left > 0) {
            r.what     = FISH_CAUGHT;
            r.item     = fishing_catch_for(f->x, f->y, f->z, f->cast_at, f->bites);
            fishing_reset(f);
            return r;
        }
        // Nothing on the line. If a bite has never come, this is an
        // impatient press; either way the player gets the line back if
        // they keep pressing, which is what REELED is for.
        r.what = f->elapsed < wait_for(f, f->bites) / 4u ? FISH_TOO_SOON : FISH_REELED;
        if (r.what == FISH_REELED) fishing_reset(f);
        return r;
    }

    // CASTING. Water first, then the worm: "you are not pointing at
    // water" is the more useful of the two complaints, and a player
    // who is pointing at a wall should not also lose a worm.
    if (wy < 0) {
        r.what = FISH_NO_WATER;
        return r;
    }
    if (!have_worm) {
        r.what = FISH_NO_WORM;
        return r;
    }

    f->out       = true;
    f->x         = wx;
    f->y         = wy;
    f->z         = wz;
    f->cast_at   = now;
    f->elapsed   = 0;
    f->bites     = 0;
    f->bite_left = 0;
    f->next_bite = wait_for(f, 0);
    r.what       = FISH_CAST;
    return r;
}

bool fishing_tick(fishing_t* f) {
    if (f == NULL || !f->out) return false;
    f->elapsed++;

    if (f->bite_left > 0) {
        if (--f->bite_left == 0) {
            // MISSED. Another fish comes along -- the worm is spent on
            // the cast, not on the fish, so the line is still fishing.
            f->bites++;
            f->next_bite = f->elapsed + wait_for(f, f->bites);
        }
        return false;
    }

    if (f->elapsed >= f->next_bite) {
        f->bite_left = FISH_BITE_TICKS;
        return true;  // the tick the float dips: the caller makes the splash
    }
    return false;
}
