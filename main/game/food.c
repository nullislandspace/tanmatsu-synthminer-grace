// =====================================================================
//  SynthMiner  --  hunger and saturation (see food.h)
// =====================================================================

#include "game/food.h"

#include <stddef.h>

#include "items/items.h"

void food_reset(food_t* f) {
    if (f == NULL) return;
    f->health     = FOOD_HEALTH_MAX;
    f->hunger     = FOOD_HUNGER_MAX;
    // A NEW PLAYER HAS NO RESERVE, which is Minecraft's own starting
    // state: full drumsticks and nothing behind them, so the first
    // walk across the map costs hunger straight away and the first
    // meal is worth eating.
    f->saturation = 0;
    f->exhaustion = 0.0f;
    f->beat       = 0;
}

// Spend one point out of the reserve, or out of hunger when the reserve
// is empty. Returns true if it was a drumstick.
static bool spend_one(food_t* f) {
    if (f->saturation > 0) {
        f->saturation--;
        return false;
    }
    if (f->hunger > 0) f->hunger--;
    return true;
}

food_event_t food_hurt(food_t* f, int n) {
    if (f == NULL || n <= 0 || f->health <= 0) return FOOD_NOTHING;
    f->health -= n;
    // Being hurt is tiring, which is Minecraft's rule and the one that
    // makes a bad landing cost twice: the hearts now, and the food it
    // takes to get them back.
    f->exhaustion += FOOD_EXHAUST_HURT * (float)n;
    if (f->health <= 0) {
        f->health = 0;
        return FOOD_DIED;
    }
    return FOOD_STARVED;
}

void food_exhaust(food_t* f, float n) {
    if (f == NULL || n <= 0.0f) return;
    f->exhaustion += n;
}

food_event_t food_tick(food_t* f, float moved, bool swimming, bool jumped) {
    if (f == NULL) return FOOD_NOTHING;

    // --- What this tick cost -----------------------------------------
    if (moved > 0.0f) f->exhaustion += moved * (swimming ? FOOD_EXHAUST_SWIM : FOOD_EXHAUST_WALK);
    if (jumped) f->exhaustion += FOOD_EXHAUST_JUMP;

    // A WHILE AND NOT AN IF. One tick can in principle carry more than
    // one step's worth -- a heal adds six at once -- and an `if` would
    // bank the surplus for ever rather than spending it.
    bool ate_into = false;
    while (f->exhaustion >= FOOD_EXHAUST_STEP) {
        f->exhaustion -= FOOD_EXHAUST_STEP;
        if (spend_one(f)) ate_into = true;
    }

    // Saturation can never be worth more than the drumsticks holding
    // it. Enforced here as well as when eating, because hunger can go
    // down for reasons saturation does not (there are none yet; there
    // will be when a mob can poison you).
    if (f->saturation > f->hunger) f->saturation = f->hunger;

    // --- The four-second beat ----------------------------------------
    //
    // Regeneration and starvation share it, because they are the two
    // ends of one rule and can never both apply: one wants hunger at 18
    // or more, the other wants it at 0.
    bool const want_beat = (f->hunger >= FOOD_REGEN_AT && f->health < FOOD_HEALTH_MAX) || f->hunger == 0;
    if (!want_beat) {
        f->beat = 0;
        return ate_into ? FOOD_ATE_INTO : FOOD_NOTHING;
    }
    if (++f->beat < FOOD_BEAT_TICKS) return ate_into ? FOOD_ATE_INTO : FOOD_NOTHING;
    f->beat = 0;

    if (f->hunger == 0) {
        // STARVING, and it can kill. There is one difficulty in this
        // game, so there is no floor to stop at (Minecraft's easy mode
        // stops at five hearts); an empty larder is a way to die and
        // the drumsticks have been counting down to it for half an
        // hour in plain sight.
        return food_hurt(f, 1);
    }

    f->health++;
    if (f->health > FOOD_HEALTH_MAX) f->health = FOOD_HEALTH_MAX;
    // HEALING IS EXPENSIVE: six exhaustion, a point and a half of the
    // reserve, per heart-half. That is what makes food a supply line
    // rather than a formality, and it is Minecraft's number.
    f->exhaustion += FOOD_EXHAUST_HEAL;
    return FOOD_HEALED;
}

// --- The fall ---------------------------------------------------------

void fall_reset(fall_t* fl, double y) {
    if (fl == NULL) return;
    fl->from    = y;
    fl->falling = false;
}

int fall_tick(fall_t* fl, double y, bool on_ground, bool in_water) {
    if (fl == NULL) return 0;
    if (in_water) {
        fl->falling = false;
        fl->from    = y;
        return 0;
    }
    if (!on_ground) {
        if (!fl->falling || y > fl->from) fl->from = y;
        fl->falling = true;
        return 0;
    }
    if (!fl->falling) return 0;
    fl->falling       = false;
    double const drop = fl->from - y;
    fl->from          = y;

    // ROUNDED UP, which is Minecraft's rule and is not a detail. A body
    // comes to rest a hair above what it landed on (PHYS_SKIN, so
    // "touching" is never "inside"), so a four-block fall measures
    // 3.999 -- and truncating turned every whole-block drop in the game
    // into one block less. Rounding up also gives a half-block overhang
    // its point: falling three and a half blocks hurts, and falling
    // exactly three does not.
    double const over = drop - (double)FALL_SAFE;
    if (over <= 0.0) return 0;
    int const hurt = (int)over + ((double)(int)over < over ? 1 : 0);
    return hurt;
}

// --- Eating -----------------------------------------------------------

bool food_is_food(uint16_t item) {
    return item != 0 && item_def(item).hunger > 0;
}

// WHAT COMES BACK WHEN IT IS EATEN. One row today; a bottle will be the
// second, and a bowl the third.
static struct {
    uint16_t food;
    uint16_t leaves;
} const FOOD_LEFTOVERS[] = {
    {ITEM_BUCKET_MILK, ITEM_BUCKET},
};

uint16_t food_leftover(uint16_t item) {
    for (size_t i = 0; i < sizeof FOOD_LEFTOVERS / sizeof FOOD_LEFTOVERS[0]; i++) {
        if (FOOD_LEFTOVERS[i].food == item) return FOOD_LEFTOVERS[i].leaves;
    }
    return 0;
}

food_eat_t food_eat(food_t* f, uint16_t item) {
    food_eat_t r = {0};
    if (f == NULL || !food_is_food(item)) return r;
    if (f->hunger >= FOOD_HUNGER_MAX) {
        r.full = true;
        return r;
    }

    item_def_t const d = item_def(item);
    f->hunger += d.hunger;
    if (f->hunger > FOOD_HUNGER_MAX) f->hunger = FOOD_HUNGER_MAX;
    f->saturation += d.saturation;
    // Capped at the drumsticks holding it, which is where the food
    // table's shape comes from: smoked salmon's four points of reserve
    // are only worth having to somebody with the hunger to hold them.
    if (f->saturation > f->hunger) f->saturation = f->hunger;

    r.ate      = true;
    r.leftover = food_leftover(item);
    return r;
}
