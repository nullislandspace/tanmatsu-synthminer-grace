// =====================================================================
//  SynthMiner  --  the makers' screen (see maker_ui.h)
// =====================================================================

#include "ui/maker_ui.h"

#include <stdio.h>
#include <string.h>

#include "audio/sfx.h"
#include "game/maker.h"
#include "i18n/i18n.h"
#include "items/item_entity.h"
#include "items/items.h"
#include "se_ui.h"
#include "testkit/showtime.h"
#include "ui/amount_ui.h"
#include "world/chunk.h"

#define ROW_MAX 4
#define MSG_SECONDS 1.8

static bool    s_open;
static int32_t s_x, s_y, s_z;
static int     s_cursor;

static bool s_picking;
static int  s_pick_cursor;
static int  s_pick[INV_SLOTS];
static int  s_pick_n;
// Which ingredient row the picker is filling.
static int  s_pick_row;

#define ACT_UP   0x01u
#define ACT_DOWN 0x02u
#define ACT_OK   0x04u
#define ACT_BACK 0x08u
static unsigned s_act;

static char   s_msg[64];
static double s_msg_until;

static bool s_asking;
static int  s_ask_slot;

static blockent_t* box(void) {
    blockent_t* b = blockent_at(s_x, s_y, s_z);
    return (b != NULL && maker_def(b->kind) != NULL) ? b : NULL;
}

// Which rows this machine shows, in order: the ingredient slots it
// uses, then the output, then the rare second output if it has one.
static int rows_of(blockent_t const* be, int slots[ROW_MAX], sm_str_t names[ROW_MAX]) {
    maker_def_t const* d = maker_def(be->kind);
    int                n = 0;
    for (int i = 0; i < d->slots_in && i < 2; i++) {
        slots[n]   = BE_MAKER_IN_A + i;
        names[n++] = SM_STR_MAKER_INPUT;
    }
    slots[n]   = BE_MAKER_OUT;
    names[n++] = SM_STR_MAKER_OUTPUT;
    if (d->extra != 0) {
        slots[n]   = BE_MAKER_EXTRA;
        names[n++] = SM_STR_MAKER_EXTRA;
    }
    return n;
}

// THE BARREL SHOWS WHAT IS IN IT (blocks.h, BARREL_*). Written into the
// block's state byte whenever the contents change, because the mesher
// reads the world and not the record.
static void sync_barrel(blockent_t const* be) {
    if (be->kind != BE_CHEESE) return;
    uint8_t const want = maker_barrel_state(be);
    uint8_t const st   = world_state(s_x, s_y, s_z);
    if (st_data(st) == want) return;  // no edit, no re-mesh
    world_set(s_x, s_y, s_z, world_block(s_x, s_y, s_z), st_with_data(st, want));
}

bool maker_ui_open(int32_t x, int32_t y, int32_t z) {
    blockent_t* b = blockent_at(x, y, z);
    if (b == NULL || maker_def(b->kind) == NULL) return false;
    s_open      = true;
    s_x         = x;
    s_y         = y;
    s_z         = z;
    s_cursor    = 0;
    s_picking   = false;
    s_act       = 0;
    s_asking    = false;
    s_msg_until = 0.0;
    return true;
}

void maker_ui_close(void) {
    s_open    = false;
    s_picking = false;
}

bool maker_ui_active(void) {
    return s_open;
}

void maker_ui_event(bsp_input_event_t const* ev) {
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

// What the player is carrying that this machine takes at all.
static void build_pick(inventory_t const* inv, uint8_t kind) {
    s_pick_n = 0;
    for (int i = 0; i < INV_SLOTS; i++) {
        inv_slot_t const* s = &inv->slot[i];
        if (s->item == 0 || s->count == 0) continue;
        if (maker_accepts(kind, s->item)) s_pick[s_pick_n++] = i;
    }
}

static void put_in(inventory_t* inv, blockent_t* be, int slot, int inv_slot, int want) {
    inv_slot_t* src = &inv->slot[inv_slot];
    inv_slot_t* dst = &be->slot[slot];
    if (src->item == 0 || want <= 0) return;
    if (want > src->count) want = src->count;

    // THE PAIL COMES STRAIGHT BACK (the user), and it is handed over
    // here because this is where the hands are. If there is no room for
    // it the milk does not go in at all -- losing somebody's bucket to
    // a full inventory would be a bug report.
    uint16_t const back = maker_returns(be->kind, src->item);
    if (back != 0) {
        want = 1;  // one pail at a time; they do not stack anyway
        if (dst->item != 0 && dst->count > 0) {
            sfx_play(SFX_DENY);
            return;
        }
    }

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
    if (back != 0) {
        int const left = inv_add(inv, back, 1, 0);
        if (left > 0) item_entity_spawn(s_x, s_y + 1, s_z, back, left, 0);
        snprintf(s_msg, sizeof(s_msg), "%s", T(SM_STR_MAKER_BUCKET_BACK));
        s_msg_until = showtime_now() + MSG_SECONDS;
    }
}

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
    i18n_fmt(s_msg, sizeof(s_msg), SM_STR_MAKER_TOOK, took, T(item_label(what)));
    s_msg_until = showtime_now() + MSG_SECONDS;
    sfx_play(SFX_PICKUP);
}

void maker_ui_update(inventory_t* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;

    blockent_t* be = box();
    if (be == NULL) {  // broken while it was open
        maker_ui_close();
        return;
    }
    maker_catch_up(be, now);
    sync_barrel(be);

    int      slots[ROW_MAX];
    sm_str_t names[ROW_MAX];
    int const n_rows = rows_of(be, slots, names);

    if (s_asking) {
        int const want = amount_update();
        if (want == AMOUNT_PENDING) return;
        s_asking = false;
        s_act    = 0;
        if (want > 0) {
            put_in(inv, be, s_pick_row, s_ask_slot, want);
            maker_catch_up(be, now);
            blockent_touch(be);
            sync_barrel(be);
            sfx_play(SFX_PLACE);
        }
        s_picking = false;
        return;
    }

    if (s_picking) {
        build_pick(inv, be->kind);
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
                if (src->count > 1 && maker_returns(be->kind, src->item) == 0) {
                    s_asking   = true;
                    s_ask_slot = s_pick[s_pick_cursor];
                    amount_open(src->item, src->count);
                    s_act = 0;
                    return;
                }
                put_in(inv, be, s_pick_row, s_pick[s_pick_cursor], 1);
                maker_catch_up(be, now);
                blockent_touch(be);
                sync_barrel(be);
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
        maker_ui_close();
        s_act = 0;
        return;
    }
    if (s_act & ACT_UP) s_cursor--;
    if (s_act & ACT_DOWN) s_cursor++;
    if (s_cursor < 0) s_cursor = 0;
    if (s_cursor >= n_rows) s_cursor = n_rows - 1;

    if (s_act & ACT_OK) {
        int const slot = slots[s_cursor];
        if (slot == BE_MAKER_OUT || slot == BE_MAKER_EXTRA) {
            take_out(inv, be, slot);
            sync_barrel(be);
        } else {
            s_picking     = true;
            s_pick_row    = slot;
            s_pick_cursor = 0;
            build_pick(inv, be->kind);
        }
    }
    s_act = 0;
}

static void slot_text(inv_slot_t const* s, char* out, size_t cap) {
    if (s->item == 0 || s->count == 0) {
        snprintf(out, cap, "%s", T(SM_STR_MAKER_EMPTY));
    } else {
        i18n_fmt(out, cap, SM_STR_MAKER_SLOT, (int)s->count, T(item_label(s->item)));
    }
}

static void draw_picker(pax_buf_t* fb, inventory_t const* inv, blockent_t const* be) {
    se_menu_row_t rows[INV_SLOTS];
    static char   labels[INV_SLOTS][64];

    int n = 0;
    for (int i = 0; i < s_pick_n; i++) {
        inv_slot_t const* s = &inv->slot[s_pick[i]];
        i18n_fmt(labels[n], sizeof(labels[n]), SM_STR_MAKER_SLOT, (int)s->count, T(item_label(s->item)));
        memset(&rows[n], 0, sizeof(rows[n]));
        rows[n].label = labels[n];
        rows[n].kind  = SE_MENU_VAL_NONE;
        n++;
    }
    if (n == 0) {
        memset(&rows[0], 0, sizeof(rows[0]));
        rows[0].label = T(SM_STR_MAKER_PICK_NONE);
        n             = 1;
    }

    char foot[128];
    sm_str_t const rate = be->kind == BE_CHEESE ? SM_STR_MAKER_A_DAY : SM_STR_MAKER_A_MINUTE;
    snprintf(foot, sizeof(foot), "%s", T(s_pick_n > 0 ? rate : SM_STR_MAKER_PICK_HINT));

    se_menu_def_t const def = {
        .title        = T(SM_STR_MAKER_PICK_INPUT),
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

void maker_ui_draw(pax_buf_t* fb, inventory_t const* inv, uint32_t now) {
    if (!s_open || inv == NULL) return;
    blockent_t const* be = box();
    if (be == NULL) return;

    if (s_picking || s_asking) {
        draw_picker(fb, inv, be);
        if (s_asking) amount_draw(fb);
        return;
    }

    int      slots[ROW_MAX];
    sm_str_t names[ROW_MAX];
    int const     n_rows = rows_of(be, slots, names);
    static char   vals[ROW_MAX][48];
    se_menu_row_t rows[ROW_MAX];

    for (int i = 0; i < n_rows; i++) {
        slot_text(&be->slot[slots[i]], vals[i], sizeof(vals[i]));
        memset(&rows[i], 0, sizeof(rows[i]));
        rows[i].label = T(names[i]);
        rows[i].kind  = SE_MENU_VAL_TEXT;
        rows[i].value = vals[i];
    }

    char footer[96];
    if (showtime_now() < s_msg_until) {
        snprintf(footer, sizeof(footer), "%s", s_msg);
    } else {
        switch (maker_idle_reason(be)) {
            case MAKER_IDLE_NO_INPUT: snprintf(footer, sizeof(footer), "%s", T(SM_STR_MAKER_NO_INPUT)); break;
            case MAKER_IDLE_FULL: snprintf(footer, sizeof(footer), "%s", T(SM_STR_MAKER_FULL)); break;
            default: i18n_fmt(footer, sizeof(footer), SM_STR_MAKER_WORKING, maker_progress_pct(be, now)); break;
        }
    }

    se_menu_def_t const def = {
        .title     = T(be->kind == BE_CHEESE ? SM_STR_MAKER_CHEESE_TITLE : SM_STR_MAKER_SAUSAGE_TITLE),
        .subtitle  = footer,
        .rows      = rows,
        .row_count = n_rows,
        .hint      = T(SM_STR_MAKER_HINT),
        .title_h   = 32.0f,
        .row_h     = 40.0f,
        .value_dx  = 240.0f,
        .panel_w   = 0.94f,
        .panel_h   = 0.70f,
    };
    se_menu_t const m = {.def = &def, .cursor = s_cursor};
    se_menu_draw(&m, fb);
}
