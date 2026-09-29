// =====================================================================
//  SynthMiner  --  the composter screen (see composter_ui.h)
// =====================================================================

#include "ui/composter_ui.h"

#include <stdio.h>
#include <string.h>

#include "audio/sfx.h"
#include "game/composter.h"
#include "i18n/i18n.h"
#include "items/items.h"
#include "se_ui.h"
#include "testkit/showtime.h"
#include "ui/amount_ui.h"

#define ROW_INPUT   0
#define ROW_COMPOST 1
#define ROW_WORMS   2
#define ROW_COUNT   3

#define MSG_SECONDS 1.8

static bool    s_open;
static int32_t s_x, s_y, s_z;
static int     s_cursor;

// The picker over the player's slots. -1 when it is not up.
static bool s_picking;
static int  s_pick_cursor;
static int  s_pick[INV_SLOTS];
static int  s_pick_n;

#define ACT_UP   0x01u
#define ACT_DOWN 0x02u
#define ACT_OK   0x04u
#define ACT_BACK 0x08u
static unsigned s_act;

static char   s_msg[64];
static double s_msg_until;

// A put waiting on "how many?". The slot index, not a pointer -- the
// same reason chest_ui.c keeps one.
static bool s_asking;
static int  s_ask_slot;

static blockent_t* box(void) {
    blockent_t* b = blockent_at(s_x, s_y, s_z);
    return (b != NULL && b->kind == BE_COMPOST) ? b : NULL;
}

bool composter_ui_open(int32_t x, int32_t y, int32_t z) {
    blockent_t* b = blockent_at(x, y, z);
    if (b == NULL || b->kind != BE_COMPOST) return false;
    s_open      = true;
    s_x         = x;
    s_y         = y;
    s_z         = z;
    s_cursor    = ROW_INPUT;
    s_picking   = false;
    s_act       = 0;
    s_asking    = false;
    s_msg_until = 0.0;
    return true;
}

void composter_ui_close(void) {
    s_open    = false;
    s_picking = false;
}

bool composter_ui_active(void) {
    return s_open;
}

void composter_ui_event(bsp_input_event_t const* ev) {
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

// Which of the player's stacks rot down. A list with everything in it
// and no way to tell which entries do anything answers no question.
static void build_pick(inventory_t const* inv) {
    s_pick_n = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slot_t const* s = &inv->slot[i];
        if (s->item == 0 || s->count == 0) continue;
        if (composter_accepts(s->item)) s_pick[s_pick_n++] = i;
    }
}

// Move a stack from the player into the scraps slot. What was there
// comes back, so choosing the wrong thing is undone by choosing the
// right one -- furnace_ui.c's rule, and for the same reason.
static void put_in(inventory_t* inv, blockent_t* be, int inv_slot, int want) {
    inv_slot_t* src = &inv->slot[inv_slot];
    inv_slot_t* dst = &be->slot[BE_COMPOST_INPUT];
    if (src->item == 0 || want <= 0) return;
    if (want > src->count) want = src->count;

    if (dst->item == src->item && dst->wear == src->wear) {
        int const cap  = item_def(dst->item).stack_max;
        int const room = cap - dst->count;
        int const take = want < room ? want : room;
        dst->count     = (uint8_t)(dst->count + take);
        src->count     = (uint8_t)(src->count - take);
        if (src->count == 0) {
            src->item = 0;
            src->wear = 0;
        }
        return;
    }

    inv_slot_t const was = *dst;
    *dst                 = *src;
    dst->count           = (uint8_t)want;
    src->count           = (uint8_t)(src->count - want);
    if (src->count == 0) {
        src->item = 0;
        src->wear = 0;
    }
    if (was.item != 0 && was.count > 0) {
        int const left = inv_add(inv, was.item, was.count, was.wear);
        if (left > 0) {
            src->item  = was.item;
            src->count = (uint8_t)left;
            src->wear  = was.wear;
        }
    }
}

// Take everything out of one of the two output slots.
static void take_out(inventory_t* inv, blockent_t* be, int slot) {
    inv_slot_t* o = &be->slot[slot];
    if (o->item == 0 || o->count == 0) {
        sfx_play(SFX_DENY);
        return;
    }
    uint16_t const what = o->item;
    int const      had  = o->count;
    int const      left = inv_add(inv, o->item, o->count, o->wear);
    int const      took = had - left;
    if (took <= 0) {
        sfx_play(SFX_DENY);
        return;
    }
    o->count = (uint8_t)left;
    if (o->count == 0) {
        o->item = 0;
        o->wear = 0;
    }
    blockent_touch(be);
    i18n_fmt(s_msg, sizeof(s_msg), SM_STR_COMPOSTER_TOOK, took, T(item_label(what)));
    s_msg_until = showtime_now() + MSG_SECONDS;
    sfx_play(SFX_PICKUP);
}

void composter_ui_update(inventory_t* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;

    blockent_t* be = box();
    if (be == NULL) {  // broken while it was open
        composter_ui_close();
        return;
    }
    // Everything it has done since anybody last looked. It does not
    // tick, so this IS the composter running (game/composter.h).
    composter_catch_up(be, now);

    if (s_asking) {
        int const want = amount_update();
        if (want == AMOUNT_PENDING) return;
        s_asking = false;
        s_act    = 0;
        if (want > 0) {
            put_in(inv, be, s_ask_slot, want);
            composter_catch_up(be, now);
            blockent_touch(be);
            sfx_play(SFX_PLACE);
        }
        s_picking = false;
        return;
    }

    if (s_picking) {
        build_pick(inv);
        if (s_act & ACT_BACK) {
            s_picking = false;
            s_act     = 0;
            return;
        }
        if (s_pick_n > 0) {
            if (s_act & ACT_UP) s_pick_cursor--;
            if (s_act & ACT_DOWN) s_pick_cursor++;
            if (s_pick_cursor < 0) s_pick_cursor = 0;
            if (s_pick_cursor >= s_pick_n) s_pick_cursor = s_pick_n - 1;
            if (s_act & ACT_OK) {
                inv_slot_t const* src = &inv->slot[s_pick[s_pick_cursor]];
                if (src->count > 1) {
                    s_asking   = true;
                    s_ask_slot = s_pick[s_pick_cursor];
                    amount_open(src->item, src->count);
                    s_act = 0;
                    return;
                }
                put_in(inv, be, s_pick[s_pick_cursor], 1);
                composter_catch_up(be, now);
                blockent_touch(be);
                sfx_play(SFX_PLACE);
                s_picking = false;
            }
        } else {
            s_pick_cursor = 0;
        }
        s_act = 0;
        return;
    }

    if (s_act & ACT_BACK) {
        composter_ui_close();
        s_act = 0;
        return;
    }
    if (s_act & ACT_UP) s_cursor--;
    if (s_act & ACT_DOWN) s_cursor++;
    if (s_cursor < 0) s_cursor = 0;
    if (s_cursor >= ROW_COUNT) s_cursor = ROW_COUNT - 1;

    if (s_act & ACT_OK) {
        if (s_cursor == ROW_COMPOST) {
            take_out(inv, be, BE_COMPOST_OUT);
        } else if (s_cursor == ROW_WORMS) {
            take_out(inv, be, BE_COMPOST_WORMS);
        } else {
            s_picking     = true;
            s_pick_cursor = 0;
            build_pick(inv);
        }
    }
    s_act = 0;
}

static void slot_text(inv_slot_t const* s, char* out, size_t cap) {
    if (s->item == 0 || s->count == 0) {
        snprintf(out, cap, "%s", T(SM_STR_COMPOSTER_EMPTY));
    } else {
        i18n_fmt(out, cap, SM_STR_COMPOSTER_SLOT, (int)s->count, T(item_label(s->item)));
    }
}

static void draw_picker(pax_buf_t* fb, inventory_t const* inv) {
    se_menu_row_t rows[INV_SLOTS];
    static char   labels[INV_SLOTS][64];

    int n = 0;
    for (int i = 0; i < s_pick_n; i++) {
        inv_slot_t const* s = &inv->slot[s_pick[i]];
        i18n_fmt(labels[n], sizeof(labels[n]), SM_STR_COMPOSTER_SLOT, (int)s->count, T(item_label(s->item)));
        memset(&rows[n], 0, sizeof(rows[n]));
        rows[n].label = labels[n];
        rows[n].kind  = SE_MENU_VAL_NONE;
        n++;
    }
    if (n == 0) {
        memset(&rows[0], 0, sizeof(rows[0]));
        rows[0].label = T(SM_STR_COMPOSTER_PICK_NONE);
        n             = 1;
    }

    char foot[128];
    snprintf(foot, sizeof(foot), "%s", T(s_pick_n > 0 ? SM_STR_COMPOSTER_A_DAY : SM_STR_COMPOSTER_PICK_HINT));

    se_menu_def_t const def = {
        .title        = T(SM_STR_COMPOSTER_PICK_INPUT),
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

void composter_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;
    blockent_t const* be = box();
    if (be == NULL) return;

    if (s_picking || s_asking) {
        draw_picker(fb, inv);
        if (s_asking) amount_draw(fb);
        return;
    }

    static char   vals[ROW_COUNT][48];
    se_menu_row_t rows[ROW_COUNT];
    sm_str_t const names[ROW_COUNT] = {SM_STR_COMPOSTER_INPUT, SM_STR_COMPOSTER_COMPOST, SM_STR_COMPOSTER_WORMS};
    int const      slots[ROW_COUNT] = {BE_COMPOST_INPUT, BE_COMPOST_OUT, BE_COMPOST_WORMS};

    for (int i = 0; i < ROW_COUNT; i++) {
        slot_text(&be->slot[slots[i]], vals[i], sizeof(vals[i]));
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = T(names[i]);
        rows[i].kind  = SE_MENU_VAL_TEXT;
        rows[i].value = vals[i];
    }

    // What it is doing, or why it is not. A box that sits there for a day
    // doing nothing has to say so, or it reads as broken.
    char footer[96];
    if (showtime_now() < s_msg_until) {
        snprintf(footer, sizeof(footer), "%s", s_msg);
    } else {
        switch (composter_idle_reason(be)) {
            case COMPOST_IDLE_NO_INPUT: snprintf(footer, sizeof(footer), "%s", T(SM_STR_COMPOSTER_NO_INPUT)); break;
            case COMPOST_IDLE_FULL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_COMPOSTER_FULL)); break;
            default:
                i18n_fmt(footer, sizeof(footer), SM_STR_COMPOSTER_WORKING, composter_progress_pct(be, now));
                break;
        }
    }

    se_menu_def_t const def = {
        .title     = T(SM_STR_COMPOSTER_TITLE),
        .subtitle  = footer,
        .rows      = rows,
        .row_count = ROW_COUNT,
        .hint      = T(SM_STR_COMPOSTER_HINT),
        .title_h   = 32.0f,
        .row_h     = 40.0f,
        .value_dx  = 240.0f,
        .panel_w   = 0.94f,
        .panel_h   = 0.66f,
    };
    se_menu_t const m = {.def = &def, .cursor = s_cursor};
    se_menu_draw(&m, fb);
}
