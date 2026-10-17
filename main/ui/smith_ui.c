// =====================================================================
//  SynthMiner  --  the smithing forge's screen (see smith_ui.h)
// =====================================================================

#include "ui/smith_ui.h"

#include <stdio.h>
#include <string.h>

#include "audio/sfx.h"
#include "game/forge.h"
#include "game/furnace.h"  // furnace_is_fuel / furnace_fuel_ticks: the fuel picker is the furnace's
#include "game/interact.h"
#include "i18n/i18n.h"
#include "items/items.h"
#include "se_ui.h"
#include "testkit/showtime.h"
#include "world/chunk.h"

#define ROW_ITEM   0
#define ROW_FUEL   1
#define ROW_METAL  2
#define ROW_OUTPUT 3
#define ROW_START  4
#define ROW_COUNT  5

#define MSG_SECONDS 1.8

// The most items the picker will list. The table has ten; this is a bound
// on the arrays below, not a limit anybody should ever reach by adding a
// recipe -- worldcheck fails the build if it is passed.
#define SMITH_UI_MAX_ITEMS 32

static bool    s_open;
static int32_t s_x, s_y, s_z;
static int     s_cursor;

// Which picker is up: -1 none, ROW_ITEM for the list of things this build
// can forge, ROW_FUEL for the player's burnable stacks.
static int s_pick_for = -1;
static int s_pick_cursor;
static int s_pick[INV_SLOTS];
static int s_pick_n;

#define ACT_UP   0x01u
#define ACT_DOWN 0x02u
#define ACT_OK   0x04u
#define ACT_BACK 0x08u
static unsigned s_act;

static char   s_msg[96];
static double s_msg_until;

static blockent_t* forge(void) {
    blockent_t* b = blockent_at(s_x, s_y, s_z);
    return (b != NULL && b->kind == BE_SMITH) ? b : NULL;
}

// THE CHEST THE FORGE SPENDS ITS METAL FROM, found through the facing both
// halves carry (D-110). NULL when it has been broken, or when its chunk is
// not resident -- and the screen says so rather than saying the chest is
// short of ingots, because those are very different problems.
static blockent_t* forge_chest(void) {
    int32_t cx, cy, cz;
    if (!interact_pair_other(s_x, s_y, s_z, &cx, &cy, &cz)) return NULL;
    if (world_block(cx, cy, cz) != BLK_SMITH_CHEST) return NULL;
    blockent_t* b = blockent_at(cx, cy, cz);
    return (b != NULL && b->kind == BE_CHEST) ? b : NULL;
}

bool smith_ui_open(int32_t x, int32_t y, int32_t z) {
    blockent_t* b = blockent_at(x, y, z);
    if (b == NULL || b->kind != BE_SMITH) return false;
    s_open      = true;
    s_x         = x;
    s_y         = y;
    s_z         = z;
    s_cursor    = ROW_ITEM;
    s_pick_for  = -1;
    s_act       = 0;
    s_msg_until = 0.0;
    return true;
}

void smith_ui_close(void) {
    s_open     = false;
    s_pick_for = -1;
}

bool smith_ui_active(void) {
    return s_open;
}

void smith_ui_event(bsp_input_event_t const* ev) {
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

// Which of the player's stacks burn. The furnace's list, the stove's list,
// this one: one rule for every machine that needs something to burn.
static void build_fuel_pick(inventory_t const* inv) {
    s_pick_n = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slot_t const* s = &inv->slot[i];
        if (s->item == 0 || s->count == 0) continue;
        if (furnace_is_fuel(s->item)) s_pick[s_pick_n++] = i;
    }
}

// One fuel item from the player into the fuel slot, the whole stack of it.
// A forge job is a day long and a stack of coal is fifteen of them, so the
// whole stack is always what was meant -- and the slot stays open while a
// job runs, because "waits for more coal" means nothing otherwise (D-160).
static void put_fuel(inventory_t* inv, blockent_t* be, int inv_slot) {
    inv_slot_t* src = &inv->slot[inv_slot];
    inv_slot_t* dst = &be->slot[BE_SMITH_FUEL];
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

// Run it and remember what it did. BOTH RECORDS ARE TOUCHED when the
// result lands, because a finished item changes both halves' business and
// a pair can straddle a chunk border (D-110) -- the hole blockent_touch
// exists for. The forge's own record is touched whenever the JOB moves,
// which is the field that a chunk eviction would otherwise lose: 18000
// ticks of a half-made chestplate is the difference between a day's work
// and nothing.
static void run_forge(blockent_t* be, blockent_t* chest, uint32_t now) {
    uint32_t const was_job   = be->job;
    uint16_t const was_out   = be->slot[BE_SMITH_OUT].count;
    forge_catch_up(be, now);
    if (be->slot[BE_SMITH_OUT].count != was_out) {
        blockent_touch(be);
        if (chest != NULL) blockent_touch(chest);
    } else if (be->job != was_job) {
        blockent_touch(be);
    }
}

void smith_ui_update(inventory_t* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;

    blockent_t* be = forge();
    if (be == NULL) {  // broken while it was open
        smith_ui_close();
        return;
    }
    blockent_t* chest = forge_chest();
    run_forge(be, chest, now);

    // --- The item list ------------------------------------------------
    if (s_pick_for == ROW_ITEM) {
        int const n = forge_item_count();
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
            recipe_t const* r = forge_item_at(s_pick_cursor);
            if (r != NULL && forge_set_pick(be, r->out)) {
                blockent_touch(be);  // or the card never hears about it
                sfx_play(SFX_CLICK);
            } else {
                sfx_play(SFX_DENY);
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
                run_forge(be, chest, now);
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

    // --- The five rows ------------------------------------------------
    if (s_act & ACT_BACK) {
        smith_ui_close();
        s_act = 0;
        return;
    }
    if (s_act & ACT_UP) s_cursor--;
    if (s_act & ACT_DOWN) s_cursor++;
    if (s_cursor < 0) s_cursor = 0;
    if (s_cursor >= ROW_COUNT) s_cursor = ROW_COUNT - 1;

    if (s_act & ACT_OK) {
        if (s_cursor == ROW_START) {
            // THE BUTTON. Everything the other rows do is preparation for
            // this one, and it either spends the ingots or says why it
            // cannot (game/forge.h).
            if (forge_start(be, chest)) {
                blockent_touch(be);
                if (chest != NULL) blockent_touch(chest);  // the metal left it
                run_forge(be, chest, now);
                i18n_fmt(s_msg, sizeof(s_msg), SM_STR_FORGE_STARTED, T(item_label(be->pick)));
                s_msg_until = showtime_now() + MSG_SECONDS;
                sfx_play(SFX_CLICK);
            } else {
                sfx_play(SFX_DENY);
                snprintf(s_msg, sizeof(s_msg), "%s", T(SM_STR_FORGE_CANNOT));
                s_msg_until = showtime_now() + MSG_SECONDS;
            }
        } else if (s_cursor == ROW_METAL) {
            // Not a slot you fill. Where the metal comes from is the one
            // thing about this machine a player has to be told once.
            snprintf(s_msg, sizeof(s_msg), "%s", T(SM_STR_FORGE_FROM_CHEST));
            s_msg_until = showtime_now() + MSG_SECONDS;
            sfx_play(SFX_CLICK);
        } else if (s_cursor == ROW_OUTPUT) {
            inv_slot_t* o = &be->slot[BE_SMITH_OUT];
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
                    i18n_fmt(s_msg, sizeof(s_msg), SM_STR_FORGE_TOOK, took, T(item_label(what)));
                    s_msg_until = showtime_now() + MSG_SECONDS;
                    sfx_play(SFX_PICKUP);
                }
            }
        } else if (s_cursor == ROW_ITEM && be->job > 0) {
            // READ-ONLY WHILE IT WORKS (D-160): the selector names the
            // ingots that were already spent on this item.
            sfx_play(SFX_DENY);
            snprintf(s_msg, sizeof(s_msg), "%s", T(SM_STR_FORGE_LOCKED));
            s_msg_until = showtime_now() + MSG_SECONDS;
        } else {
            s_pick_for    = s_cursor;
            s_pick_cursor = 0;
            if (s_cursor == ROW_FUEL) {
                build_fuel_pick(inv);
            } else {
                // Open the list ON the item it is already set to, so that
                // changing your mind is one keypress from where you were
                // rather than from the top.
                for (int i = 0; i < forge_item_count(); i++) {
                    recipe_t const* r = forge_item_at(i);
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

// "4 Iron ingot", or "- empty -".
static void slot_text(inv_slot_t const* s, char* out, size_t cap) {
    if (s->item == 0 || s->count == 0) {
        snprintf(out, cap, "%s", T(SM_STR_FORGE_EMPTY));
    } else {
        i18n_fmt(out, cap, SM_STR_FORGE_SLOT, (int)s->count, T(item_label(s->item)));
    }
}

// "4 Iron ingot" -- what one item costs. Every forge recipe has exactly
// one ingredient, which is the whole reason this is one call and not the
// stove's loop over a dish's four.
static void cost_text(recipe_t const* r, char* out, size_t cap) {
    if (r == NULL || r->n_in <= 0) {
        out[0] = '\0';
        return;
    }
    i18n_fmt(out, cap, SM_STR_FORGE_NEEDS, (int)r->in[0].count, T(item_label(r->in[0].item)));
}

static void draw_item_picker(pax_buf_t* fb) {
    static se_menu_row_t rows[SMITH_UI_MAX_ITEMS];
    static char          labels[SMITH_UI_MAX_ITEMS][48];

    int const n = forge_item_count();
    int const shown = n < SMITH_UI_MAX_ITEMS ? n : SMITH_UI_MAX_ITEMS;
    for (int i = 0; i < shown; i++) {
        recipe_t const* r = forge_item_at(i);
        snprintf(labels[i], sizeof(labels[i]), "%s", T(item_label(r->out)));
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = labels[i];
        rows[i].kind  = SE_MENU_VAL_NONE;
    }

    // WHAT IT COSTS AND HOW LONG IT TAKES, on one line, because both are
    // short: "6 Iron ingot, two days". The two numbers a player compares
    // items by, and the only two this machine has.
    static char takes[128];
    static char worth[96];
    recipe_t const* r = forge_item_at(s_pick_cursor);
    if (r != NULL) {
        cost_text(r, takes, sizeof(takes));
        uint32_t const len = forge_job_ticks(r->out);
        i18n_fmt(worth, sizeof(worth), SM_STR_FORGE_COST,
                 T(len == 2u * DAY_TICKS ? SM_STR_FORGE_TAKES_TWO : SM_STR_FORGE_TAKES),
                 len == 2u * DAY_TICKS ? T(SM_STR_ITEM_TITANIUM_INGOT) : T(SM_STR_ITEM_IRON_INGOT));
    } else {
        snprintf(takes, sizeof(takes), "%s", T(SM_STR_FORGE_FROM_CHEST));
        worth[0] = '\0';
    }

    se_menu_def_t const def = {
        .title        = T(SM_STR_FORGE_PICK_TITLE),
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

static void draw_fuel_picker(pax_buf_t* fb, inventory_t const* inv, blockent_t const* be) {
    se_menu_row_t rows[INV_SLOTS];
    static char   labels[INV_SLOTS][64];

    int n = 0;
    for (int i = 0; i < s_pick_n; i++) {
        inv_slot_t const* s = &inv->slot[s_pick[i]];
        i18n_fmt(labels[n], sizeof(labels[n]), SM_STR_FORGE_SLOT, (int)s->count, T(item_label(s->item)));
        memset(&rows[n], 0, sizeof(rows[n]));
        rows[n].label = labels[n];
        rows[n].kind  = SE_MENU_VAL_NONE;
        n++;
    }
    if (n == 0) {
        memset(&rows[0], 0, sizeof(rows[0]));
        rows[0].label = T(SM_STR_FORGE_PICK_NONE_FUEL);
        n             = 1;
    }

    char foot[128];
    if (s_pick_n > 0) {
        inv_slot_t const* s  = &inv->slot[s_pick[s_pick_cursor]];
        // How many ITEMS this stack would see through, which is the only
        // number that makes one fuel comparable with another here: a job
        // is a day, so coal (1600 ticks) covers fifteen of them and a
        // stack of coal covers a fortnight of smithing.
        int const len = (int)forge_job_ticks(be != NULL ? be->pick : 0);
        int const per = len > 0 ? (int)furnace_fuel_ticks(s->item) * (int)s->count / len : 0;
        i18n_fmt(foot, sizeof(foot), SM_STR_FORGE_BURNS, per);
    } else {
        snprintf(foot, sizeof(foot), "%s", T(SM_STR_FORGE_PICK_HINT));
    }

    se_menu_def_t const def = {
        .title        = T(SM_STR_FORGE_PICK_FUEL),
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

void smith_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now) {
    (void)now;
    if (!s_open || inv == NULL) return;
    blockent_t const* be = forge();
    if (be == NULL) return;
    blockent_t const* chest = forge_chest();

    if (s_pick_for == ROW_ITEM) {
        draw_item_picker(fb);
        return;
    }
    if (s_pick_for == ROW_FUEL) {
        draw_fuel_picker(fb, inv, be);
        return;
    }

    static char   vals[ROW_COUNT][96];
    se_menu_row_t rows[ROW_COUNT];
    sm_str_t const names[ROW_COUNT] = {SM_STR_FORGE_ITEM, SM_STR_FORGE_FUEL, SM_STR_FORGE_METAL,
                                       SM_STR_FORGE_OUTPUT, SM_STR_FORGE_START};

    if (be->pick != 0) {
        snprintf(vals[ROW_ITEM], sizeof(vals[ROW_ITEM]), "%s", T(item_label(be->pick)));
    } else {
        snprintf(vals[ROW_ITEM], sizeof(vals[ROW_ITEM]), "%s", T(SM_STR_FORGE_NONE));
    }
    slot_text(&be->slot[BE_SMITH_FUEL], vals[ROW_FUEL], sizeof(vals[ROW_FUEL]));
    slot_text(&be->slot[BE_SMITH_METAL], vals[ROW_METAL], sizeof(vals[ROW_METAL]));
    slot_text(&be->slot[BE_SMITH_OUT], vals[ROW_OUTPUT], sizeof(vals[ROW_OUTPUT]));
    // Start is a button, not a slot: it says what it would do.
    if (be->job > 0) {
        i18n_fmt(vals[ROW_START], sizeof(vals[ROW_START]), SM_STR_FORGE_MAKING, T(item_label(be->pick)));
    } else {
        snprintf(vals[ROW_START], sizeof(vals[ROW_START]), "%s", T(SM_STR_FORGE_START));
    }

    for (int i = 0; i < ROW_COUNT; i++) {
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = T(names[i]);
        rows[i].kind  = SE_MENU_VAL_TEXT;
        rows[i].value = vals[i];
    }

    // WHAT IT IS DOING, OR WHY IT IS NOT -- the stove's rule, and a forge
    // has more ways of being idle than a stove does: nothing chosen, no
    // chest, a chest short of metal, a full output, nothing to burn, a job
    // starved half way through, or ready and unstarted.
    char footer[128];
    if (showtime_now() < s_msg_until) {
        snprintf(footer, sizeof(footer), "%s", s_msg);
    } else {
        switch (forge_idle_reason(be, chest)) {
            case FORGE_IDLE_NO_PICK: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_NO_PICK)); break;
            case FORGE_IDLE_NO_CHEST: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_NO_CHEST)); break;
            case FORGE_IDLE_NO_METAL: {
                int            need = 0;
                uint16_t const what = forge_missing(be, chest, &need);
                i18n_fmt(footer, sizeof(footer), SM_STR_FORGE_NO_METAL, need, T(item_label(what)));
            } break;
            case FORGE_IDLE_NO_FUEL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_NO_FUEL)); break;
            case FORGE_IDLE_FULL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_FULL)); break;
            case FORGE_IDLE_WAITING: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_WAITING)); break;
            case FORGE_IDLE_READY: snprintf(footer, sizeof(footer), "%s", T(SM_STR_FORGE_READY)); break;
            default:
                i18n_fmt(footer, sizeof(footer), SM_STR_FORGE_WORKING, forge_progress_pct(be));
                break;
        }
    }

    se_menu_def_t const def = {
        .title     = T(SM_STR_FORGE_TITLE),
        .subtitle  = footer,
        .rows      = rows,
        .row_count = ROW_COUNT,
        .hint      = T(SM_STR_FORGE_HINT),
        .title_h   = 32.0f,
        .row_h     = 40.0f,
        .value_dx  = 240.0f,
        .panel_w   = 0.94f,
        .panel_h   = 0.72f,
    };
    se_menu_t const m = {.def = &def, .cursor = s_cursor};
    se_menu_draw(&m, fb);
}
