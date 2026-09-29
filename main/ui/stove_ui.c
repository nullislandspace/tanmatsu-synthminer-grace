// =====================================================================
//  SynthMiner  --  the kitchen stove's screen (see stove_ui.h)
// =====================================================================

#include "ui/stove_ui.h"

#include <stdio.h>
#include <string.h>

#include "audio/sfx.h"
#include "game/furnace.h"  // furnace_is_fuel: the fuel picker is the furnace's
#include "game/interact.h"
#include "game/stove.h"
#include "i18n/i18n.h"
#include "items/items.h"
#include "se_ui.h"
#include "testkit/showtime.h"
#include "world/chunk.h"

#define ROW_DISH   0
#define ROW_FUEL   1
#define ROW_OUTPUT 2
#define ROW_COUNT  3

#define MSG_SECONDS 1.8

// The most dishes the picker will list. The table has thirteen; this is
// a bound on the arrays below, not a limit anybody should ever reach by
// adding a recipe -- worldcheck fails the build if it is passed.
#define STOVE_UI_MAX_DISHES 32

static bool    s_open;
static int32_t s_x, s_y, s_z;
static int     s_cursor;

// Which picker is up: -1 none, ROW_DISH for the dish list, ROW_FUEL for
// the player's burnable stacks.
static int s_pick_for = -1;
static int s_pick_cursor;
static int s_pick[INV_SLOTS];
static int s_pick_n;

#define ACT_UP   0x01u
#define ACT_DOWN 0x02u
#define ACT_OK   0x04u
#define ACT_BACK 0x08u
static unsigned s_act;

static char   s_msg[64];
static double s_msg_until;

static blockent_t* stove(void) {
    blockent_t* b = blockent_at(s_x, s_y, s_z);
    return (b != NULL && b->kind == BE_STOVE) ? b : NULL;
}

// THE CHEST THE STOVE COOKS OUT OF, found through the facing both
// halves carry (D-110). NULL when it has been broken, or when its chunk
// is not resident -- and the screen says so rather than saying the
// ingredients are missing, because those are very different problems.
static blockent_t* stove_chest(void) {
    int32_t cx, cy, cz;
    if (!interact_stove_other(s_x, s_y, s_z, &cx, &cy, &cz)) return NULL;
    if (world_block(cx, cy, cz) != BLK_STOVE_CHEST) return NULL;
    blockent_t* b = blockent_at(cx, cy, cz);
    return (b != NULL && b->kind == BE_CHEST) ? b : NULL;
}

bool stove_ui_open(int32_t x, int32_t y, int32_t z) {
    blockent_t* b = blockent_at(x, y, z);
    if (b == NULL || b->kind != BE_STOVE) return false;
    s_open      = true;
    s_x         = x;
    s_y         = y;
    s_z         = z;
    s_cursor    = ROW_DISH;
    s_pick_for  = -1;
    s_act       = 0;
    s_msg_until = 0.0;
    return true;
}

void stove_ui_close(void) {
    s_open     = false;
    s_pick_for = -1;
}

bool stove_ui_active(void) {
    return s_open;
}

void stove_ui_event(bsp_input_event_t const* ev) {
    if (!s_open || ev == NULL) return;
    if (ev->type == INPUT_EVENT_TYPE_NAVIGATION && ev->args_navigation.state) {
        switch (ev->args_navigation.key) {
            case BSP_INPUT_NAVIGATION_KEY_ESC: s_act |= ACT_BACK; break;
            case BSP_INPUT_NAVIGATION_KEY_RETURN: s_act |= ACT_OK; break;
            case BSP_INPUT_NAVIGATION_KEY_UP: s_act |= ACT_UP; break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN: s_act |= ACT_DOWN; break;
            default: break;
        }
    } else if (ev->type == INPUT_EVENT_TYPE_SCANCODE) {
        uint16_t const sc = ev->args_scancode.scancode;
        if ((sc & BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != 0) return;
        switch (sc) {
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_UP: s_act |= ACT_UP; break;
            case BSP_INPUT_SCANCODE_ESCAPED_GREY_DOWN: s_act |= ACT_DOWN; break;
            default: break;
        }
    }
}

// Which of the player's stacks burn.
static void build_fuel_pick(inventory_t const* inv) {
    s_pick_n = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slot_t const* s = &inv->slot[i];
        if (s->item == 0 || s->count == 0) continue;
        if (furnace_is_fuel(s->item)) s_pick[s_pick_n++] = i;
    }
}

// One fuel item from the player into the fuel slot. A stove's fuel slot
// never asks "how many?" the way a furnace's does: a dish is a minute
// and a stack of coal is an evening, so the whole stack is always what
// was meant.
static void put_fuel(inventory_t* inv, blockent_t* be, int inv_slot) {
    inv_slot_t* src = &inv->slot[inv_slot];
    inv_slot_t* dst = &be->slot[BE_STOVE_FUEL];
    if (src->item == 0 || src->count == 0) return;

    if (dst->item == src->item && dst->wear == src->wear) {
        int const cap  = item_def(dst->item).stack_max;
        int const room = cap - dst->count;
        int const take = src->count < room ? src->count : room;
        dst->count     = (uint8_t)(dst->count + take);
        src->count     = (uint8_t)(src->count - take);
        if (src->count == 0) {
            src->item = 0;
            src->wear = 0;
        }
        return;
    }

    // Something else was in there: it comes back out, and nothing is
    // ever lost -- if the pack will not take it, it goes back into the
    // slot the new stack has just left.
    inv_slot_t const was = *dst;
    *dst                 = *src;
    src->item            = 0;
    src->count           = 0;
    src->wear            = 0;
    if (was.item != 0 && was.count > 0) {
        int const left = inv_add(inv, was.item, was.count, was.wear);
        if (left > 0) {
            src->item  = was.item;
            src->count = (uint8_t)left;
            src->wear  = was.wear;
        }
    }
}

void stove_ui_update(inventory_t* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;

    blockent_t* be = stove();
    if (be == NULL) {  // broken while it was open
        stove_ui_close();
        return;
    }
    blockent_t* chest = stove_chest();
    // Everything it has done since it was last looked at. It does not
    // tick, so this IS the stove running (game/stove.h).
    //
    // BOTH RECORDS ARE TOUCHED, because cooking changes both: the dish
    // lands in the stove and the ingredients come out of the chest. A
    // pair can straddle a chunk border (D-110), so touching only the
    // stove would lose a chest's worth of wheat on the next eviction --
    // which is exactly the hole blockent_touch was written for.
    uint16_t const was_out = be->slot[BE_STOVE_OUT].count;
    stove_catch_up(be, chest, now);
    if (chest != NULL && be->slot[BE_STOVE_OUT].count != was_out) {
        blockent_touch(be);
        blockent_touch(chest);
    }

    // --- The dish list ------------------------------------------------
    if (s_pick_for == ROW_DISH) {
        int const n = stove_dish_count();
        if (s_act & ACT_BACK) {
            s_pick_for = -1;
            s_act      = 0;
            return;
        }
        if (s_act & ACT_UP) s_pick_cursor--;
        if (s_act & ACT_DOWN) s_pick_cursor++;
        if (s_pick_cursor < 0) s_pick_cursor = 0;
        if (s_pick_cursor >= n) s_pick_cursor = n - 1;
        if (s_act & ACT_OK) {
            recipe_t const* r = stove_dish_at(s_pick_cursor);
            if (r != NULL) {
                stove_set_pick(be, r->out);
                stove_catch_up(be, chest, now);  // it may start this instant
                blockent_touch(be);              // or the card never hears about it
                sfx_play(SFX_CLICK);
            }
            s_pick_for = -1;
        }
        s_act = 0;
        return;
    }

    // --- The fuel picker ----------------------------------------------
    if (s_pick_for == ROW_FUEL) {
        build_fuel_pick(inv);
        if (s_act & ACT_BACK) {
            s_pick_for = -1;
            s_act      = 0;
            return;
        }
        if (s_pick_n > 0) {
            if (s_act & ACT_UP) s_pick_cursor--;
            if (s_act & ACT_DOWN) s_pick_cursor++;
            if (s_pick_cursor < 0) s_pick_cursor = 0;
            if (s_pick_cursor >= s_pick_n) s_pick_cursor = s_pick_n - 1;
            if (s_act & ACT_OK) {
                put_fuel(inv, be, s_pick[s_pick_cursor]);
                stove_catch_up(be, chest, now);
                blockent_touch(be);
                sfx_play(SFX_PLACE);
                s_pick_for = -1;
            }
        } else {
            s_pick_cursor = 0;
        }
        s_act = 0;
        return;
    }

    // --- The three rows -----------------------------------------------
    if (s_act & ACT_BACK) {
        stove_ui_close();
        s_act = 0;
        return;
    }
    if (s_act & ACT_UP) s_cursor--;
    if (s_act & ACT_DOWN) s_cursor++;
    if (s_cursor < 0) s_cursor = 0;
    if (s_cursor >= ROW_COUNT) s_cursor = ROW_COUNT - 1;

    if (s_act & ACT_OK) {
        if (s_cursor == ROW_OUTPUT) {
            inv_slot_t* o = &be->slot[BE_STOVE_OUT];
            if (o->item == 0 || o->count == 0) {
                sfx_play(SFX_DENY);
            } else {
                uint16_t const what = o->item;
                int const      had  = o->count;
                int const      left = inv_add(inv, o->item, o->count, o->wear);
                int const      took = had - left;
                if (took <= 0) {
                    sfx_play(SFX_DENY);
                } else {
                    o->count = (uint8_t)left;
                    if (o->count == 0) {
                        o->item = 0;
                        o->wear = 0;
                    }
                    blockent_touch(be);
                    i18n_fmt(s_msg, sizeof(s_msg), SM_STR_STOVE_TOOK, took, T(item_label(what)));
                    s_msg_until = showtime_now() + MSG_SECONDS;
                    sfx_play(SFX_PICKUP);
                }
            }
        } else {
            s_pick_for    = s_cursor;
            s_pick_cursor = 0;
            if (s_cursor == ROW_FUEL) {
                build_fuel_pick(inv);
            } else {
                // Open the list ON the dish it is already set to, so
                // that changing your mind is one keypress from where
                // you were rather than from the top.
                for (int i = 0; i < stove_dish_count(); i++) {
                    recipe_t const* r = stove_dish_at(i);
                    if (r != NULL && r->out == be->pick) {
                        s_pick_cursor = i;
                        break;
                    }
                }
            }
        }
    }
    s_act = 0;
}

// "3 Coal", or "- empty -".
static void slot_text(inv_slot_t const* s, char* out, size_t cap) {
    if (s->item == 0 || s->count == 0) {
        snprintf(out, cap, "%s", T(SM_STR_STOVE_EMPTY));
    } else {
        i18n_fmt(out, cap, SM_STR_STOVE_SLOT, (int)s->count, T(item_label(s->item)));
    }
}

// "2 Wheat, 1 Sausage, 1 Cheese, 2 Shrimp" -- what a dish takes out of
// the chest, written out in full because the whole question a player
// has in this list is "can I make that yet".
static void recipe_text(recipe_t const* r, char* out, size_t cap) {
    out[0]     = '\0';
    size_t pos = 0;
    for (int i = 0; i < r->n_in && pos + 2 < cap; i++) {
        if (i > 0) {
            out[pos++] = ',';
            out[pos++] = ' ';
            out[pos]   = '\0';
        }
        i18n_fmt(out + pos, cap - pos, SM_STR_STOVE_NEEDS, (int)r->in[i].count, T(item_label(r->in[i].item)));
        pos = strlen(out);
    }
}

static void draw_dish_picker(pax_buf_t* fb) {
    static se_menu_row_t rows[STOVE_UI_MAX_DISHES];
    static char          labels[STOVE_UI_MAX_DISHES][48];

    int const n = stove_dish_count();
    int       shown = n < STOVE_UI_MAX_DISHES ? n : STOVE_UI_MAX_DISHES;
    for (int i = 0; i < shown; i++) {
        recipe_t const* r = stove_dish_at(i);
        snprintf(labels[i], sizeof(labels[i]), "%s", T(item_label(r->out)));
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = labels[i];
        rows[i].kind  = SE_MENU_VAL_NONE;
    }

    // WHAT IT TAKES AND WHAT IT IS WORTH, on TWO lines and not one.
    // They started on one, three spaces apart, and the check that
    // builds the real line caught it at once: a four-ingredient dish
    // in Serbian is 675 px of a 626 px panel. Four ingredients is what
    // the best two dishes in the game are made of, so the line that has
    // to give is the one carrying two sentences.
    //
    // The worth goes in the SUBTITLE, under the title, where it is also
    // the more useful of the two while scrolling: the numbers are what
    // a player compares dishes by.
    static char takes[192];
    static char worth[96];
    recipe_t const* r = stove_dish_at(s_pick_cursor);
    if (r != NULL) {
        recipe_text(r, takes, sizeof(takes));
        item_def_t const d = item_def(r->out);
        i18n_fmt(worth, sizeof(worth), SM_STR_STOVE_FEEDS, (int)d.hunger, (int)d.saturation);
    } else {
        snprintf(takes, sizeof(takes), "%s", T(SM_STR_STOVE_FROM_CHEST));
        worth[0] = '\0';
    }

    se_menu_def_t const def = {
        .title        = T(SM_STR_STOVE_PICK_TITLE),
        .subtitle     = worth,
        .rows         = rows,
        .row_count    = shown,
        .hint         = takes,
        .title_h      = 32.0f,
        .row_h        = 34.0f,
        .value_dx     = 0.0f,
        .panel_w      = 0.88f,
        .panel_h      = 0.92f,
        .visible_rows = 8,
    };
    se_menu_t const m = {.def = &def, .cursor = s_pick_cursor};
    se_menu_draw(&m, fb);
}

static void draw_fuel_picker(pax_buf_t* fb, inventory_t const* inv) {
    se_menu_row_t rows[INV_SLOTS];
    static char   labels[INV_SLOTS][64];

    int n = 0;
    for (int i = 0; i < s_pick_n; i++) {
        inv_slot_t const* s = &inv->slot[s_pick[i]];
        i18n_fmt(labels[n], sizeof(labels[n]), SM_STR_STOVE_SLOT, (int)s->count, T(item_label(s->item)));
        memset(&rows[n], 0, sizeof(rows[n]));
        rows[n].label = labels[n];
        rows[n].kind  = SE_MENU_VAL_NONE;
        n++;
    }
    if (n == 0) {
        memset(&rows[0], 0, sizeof(rows[0]));
        rows[0].label = T(SM_STR_STOVE_PICK_NONE_FUEL);
        n             = 1;
    }

    char foot[128];
    if (s_pick_n > 0) {
        inv_slot_t const* s = &inv->slot[s_pick[s_pick_cursor]];
        // How many dishes this stack would see through, which is the
        // only number that makes one fuel comparable with another.
        int const per = furnace_fuel_ticks(s->item) / (int)STOVE_COOK_TICKS;
        i18n_fmt(foot, sizeof(foot), SM_STR_FURNACE_BURNS, per * (int)s->count);
    } else {
        snprintf(foot, sizeof(foot), "%s", T(SM_STR_STOVE_PICK_HINT));
    }

    se_menu_def_t const def = {
        .title        = T(SM_STR_STOVE_PICK_FUEL),
        .rows         = rows,
        .row_count    = n,
        .hint         = foot,
        .title_h      = 32.0f,
        .row_h        = 34.0f,
        .value_dx     = 0.0f,
        .panel_w      = 0.88f,
        .panel_h      = 0.92f,
        .visible_rows = 8,
    };
    se_menu_t const m = {.def = &def, .cursor = s_pick_n > 0 ? s_pick_cursor : n};
    se_menu_draw(&m, fb);
}

void stove_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now) {
    (void)now;
    if (!s_open || inv == NULL) return;
    blockent_t const* be = stove();
    if (be == NULL) return;
    blockent_t const* chest = stove_chest();

    if (s_pick_for == ROW_DISH) {
        draw_dish_picker(fb);
        return;
    }
    if (s_pick_for == ROW_FUEL) {
        draw_fuel_picker(fb, inv);
        return;
    }

    static char   vals[ROW_COUNT][64];
    se_menu_row_t rows[ROW_COUNT];
    sm_str_t const names[ROW_COUNT] = {SM_STR_STOVE_DISH, SM_STR_STOVE_FUEL, SM_STR_STOVE_OUTPUT};

    if (be->pick != 0) {
        snprintf(vals[ROW_DISH], sizeof(vals[ROW_DISH]), "%s", T(item_label(be->pick)));
    } else {
        snprintf(vals[ROW_DISH], sizeof(vals[ROW_DISH]), "%s", T(SM_STR_STOVE_NONE));
    }
    slot_text(&be->slot[BE_STOVE_FUEL], vals[ROW_FUEL], sizeof(vals[ROW_FUEL]));
    slot_text(&be->slot[BE_STOVE_OUT], vals[ROW_OUTPUT], sizeof(vals[ROW_OUTPUT]));

    for (int i = 0; i < ROW_COUNT; i++) {
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = T(names[i]);
        rows[i].kind  = SE_MENU_VAL_TEXT;
        rows[i].value = vals[i];
    }

    // WHAT IT IS DOING, OR WHY IT IS NOT. The user asked for this by
    // name, and a stove has more ways of being idle than any other
    // machine here -- so it names the one that applies, and when the
    // chest is merely short it names what of.
    char footer[128];
    if (showtime_now() < s_msg_until) {
        snprintf(footer, sizeof(footer), "%s", s_msg);
    } else {
        switch (stove_idle_reason(be, chest)) {
            case STOVE_IDLE_NO_PICK: snprintf(footer, sizeof(footer), "%s", T(SM_STR_STOVE_NO_PICK)); break;
            case STOVE_IDLE_NO_CHEST: snprintf(footer, sizeof(footer), "%s", T(SM_STR_STOVE_NO_CHEST)); break;
            case STOVE_IDLE_MISSING: {
                int            need = 0;
                uint16_t const what = stove_missing(be, chest, &need);
                i18n_fmt(footer, sizeof(footer), SM_STR_STOVE_MISSING, need, T(item_label(what)));
            } break;
            case STOVE_IDLE_NO_FUEL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_STOVE_NO_FUEL)); break;
            case STOVE_IDLE_FULL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_STOVE_FULL)); break;
            default: i18n_fmt(footer, sizeof(footer), SM_STR_STOVE_COOKING, stove_progress_pct(be)); break;
        }
    }

    se_menu_def_t const def = {
        .title     = T(SM_STR_STOVE_TITLE),
        .subtitle  = footer,
        .rows      = rows,
        .row_count = ROW_COUNT,
        .hint      = T(SM_STR_STOVE_HINT),
        .title_h   = 32.0f,
        .row_h     = 40.0f,
        .value_dx  = 240.0f,
        .panel_w   = 0.94f,
        .panel_h   = 0.66f,
    };
    se_menu_t const m = {.def = &def, .cursor = s_cursor};
    se_menu_draw(&m, fb);
}
