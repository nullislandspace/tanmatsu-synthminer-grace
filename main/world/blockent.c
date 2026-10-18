// =====================================================================
//  SynthMiner  --  blocks that remember more than a byte (see blockent.h)
// =====================================================================

#include "world/blockent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/psram.h"
#include "world/chunk_codec.h"
#include "common/tags.h"
#include "items/items.h"

// Only for the slow sweep at the end of this file: the five catch-up
// rules, and the one function that finds a two-cell machine's other
// half. Nothing else here needs them.
#include "game/composter.h"
#include "game/forge.h"
#include "game/furnace.h"
#include "game/interact.h"
#include "game/maker.h"
#include "game/stove.h"
#include "world/blocks.h"

static blockent_t* s_pool;

// What each kind is called on disk. A STRING, not a number, as the
// chunk format says (chunk_codec.h): there are a handful of these, the
// name costs nothing, and it can never be misread after a renumbering.
static char const* kind_name(uint8_t kind) {
    switch (kind) {
        case BE_FURNACE: return "furnace";
        case BE_CHEST: return "chest";
        case BE_TRASH: return "trash";
        case BE_COMPOST: return "composter";
        case BE_CHEESE: return "cheese_maker";
        case BE_SAUSAGE: return "sausage_maker";
        case BE_STOVE: return "stove";
        case BE_SMITH: return "smith";
        default: return "";
    }
}

static uint8_t kind_by_name(char const* s) {
    for (uint8_t k = BE_FURNACE; k <= BE_SMITH; k++) {
        if (strcmp(kind_name(k), s) == 0) return k;
    }
    return BE_NONE;
}

bool blockent_init(void) {
    if (s_pool != NULL) return true;
    s_pool = (blockent_t*)sm_calloc(BE_MAX, sizeof(blockent_t));
    return s_pool != NULL;
}

void blockent_shutdown(void) {
    if (s_pool == NULL) return;
    sm_free(s_pool);
    s_pool = NULL;
}

void blockent_clear(void) {
    if (s_pool != NULL) memset(s_pool, 0, (size_t)BE_MAX * sizeof(blockent_t));
}

blockent_t* blockent_at(int32_t x, int32_t y, int32_t z) {
    if (s_pool == NULL) return NULL;
    // A linear walk over 192 records, and deliberately so: it is a
    // handful of comparisons against a table that is almost always
    // nearly empty, called once when a player presses a key -- not per
    // frame and not per block. A hash here would be a structure to keep
    // right for no measurable gain.
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t* b = &s_pool[i];
        if (b->kind != BE_NONE && b->x == x && b->y == y && b->z == z) return b;
    }
    return NULL;
}

blockent_t* blockent_add(int32_t x, int32_t y, int32_t z, uint8_t kind) {
    if (s_pool == NULL || kind == BE_NONE) return NULL;

    // One per cell. Placing over an old record -- which can happen if a
    // break was missed somehow -- takes the cell rather than leaving
    // two records fighting over it.
    blockent_t* b = blockent_at(x, y, z);
    if (b == NULL) {
        for (int i = 0; i < BE_MAX && b == NULL; i++) {
            if (s_pool[i].kind == BE_NONE) b = &s_pool[i];
        }
    }
    if (b == NULL) return NULL;  // full: the caller has to say so

    memset(b, 0, sizeof(*b));
    b->x    = x;
    b->y    = y;
    b->z    = z;
    b->kind = kind;
    return b;
}

void blockent_remove(int32_t x, int32_t y, int32_t z) {
    blockent_t* b = blockent_at(x, y, z);
    if (b != NULL) memset(b, 0, sizeof(*b));
}

void blockent_touch(blockent_t const* be) {
    if (be == NULL || be->kind == BE_NONE) return;
    // THE ONE RULE FROM blockent.h, enforced here so there is only one
    // place to get it right: a record may only be marked dirty while its
    // chunk is CS_READY.
    //
    // Marking a chunk that is being written is not merely pointless, it
    // is how a change gets thrown away. chunk_worker.c clears CF_EDITED
    // after a successful save when the chunk's edit_seq is what it was
    // when the job went out, on the sound reasoning that the bytes it
    // wrote already contain the change. For a record touched mid-save
    // that reasoning is false -- the worker serialised the OLD contents
    // -- and the bit it clears is the only thing that would have written
    // the new ones. A furnace emptied while its chunk drains, gone.
    //
    // CS_SAVING is what chunk_find() happily returns for a chunk on the
    // worker, so this cannot be left to the callers.
    chunk_t* c = chunk_find(chunk_of(be->x), chunk_of(be->z));
    if (c == NULL || c->cstate != CS_READY) return;
    chunk_mark_edited(chunk_of(be->x), chunk_of(be->z));
}

int blockent_rot_trash(blockent_t* be, uint32_t now) {
    if (be == NULL || be->kind != BE_TRASH) return 0;

    // A stamp from the future means the clock moved back under it;
    // treat it as no time at all rather than as an instant emptying.
    uint32_t const elapsed = now >= be->stamp ? now - be->stamp : 0;
    if (elapsed < BE_TRASH_TICKS) return 0;

    int gone = 0;
    for (int i = 0; i < BE_SLOTS; i++) {
        if (be->slot[i].item == 0) continue;
        memset(&be->slot[i], 0, sizeof(be->slot[i]));
        gone++;
    }
    be->stamp = now;
    return gone;
}

void blockent_drop_chunk(int32_t cx, int32_t cz) {
    if (s_pool == NULL) return;
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t* b = &s_pool[i];
        if (b->kind == BE_NONE) continue;
        if (chunk_of(b->x) == cx && chunk_of(b->z) == cz) memset(b, 0, sizeof(*b));
    }
}

int blockent_count(void) {
    int n = 0;
    if (s_pool == NULL) return 0;
    for (int i = 0; i < BE_MAX; i++) {
        if (s_pool[i].kind != BE_NONE) n++;
    }
    return n;
}

int blockent_count_in(int32_t cx, int32_t cz) {
    int n = 0;
    if (s_pool == NULL) return 0;
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t const* b = &s_pool[i];
        if (b->kind != BE_NONE && chunk_of(b->x) == cx && chunk_of(b->z) == cz) n++;
    }
    return n;
}

// --- Saving -----------------------------------------------------------

// How many slots a kind actually uses. Only those are written, so a
// furnace costs three fields and not twenty-seven empty ones.
static int slots_used(uint8_t kind) {
    // A furnace has three and a composter has three; a chest and a
    // trashcan use all of them.
    if (kind == BE_FURNACE || kind == BE_COMPOST) return 3;
    if (kind == BE_CHEESE || kind == BE_SAUSAGE) return 4;  // two in, two out
    if (kind == BE_STOVE) return 2;                        // fuel and output; the chest holds the rest
    // Fuel, output, and the ingots the job is made of (D-160).
    if (kind == BE_SMITH) return 3;
    return BE_SLOTS;
}

// One record's tagged fields. Items go in BY NAME, like everything else
// a save keeps (D-74), so a build that renumbers items still reads it.
static void write_record(tag_writer_t* w, blockent_t const* b) {
    tag_put_str(w, "kind", kind_name(b->kind));
    tag_put_i32(w, "x", b->x);
    tag_put_i32(w, "y", b->y);
    tag_put_i32(w, "z", b->z);
    tag_put_i32(w, "stamp", (int32_t)b->stamp);
    if (b->kind == BE_FURNACE || b->kind == BE_STOVE || b->kind == BE_SMITH) {
        tag_put_i16(w, "burn", (int16_t)b->burn_left);
        tag_put_i16(w, "burnmax", (int16_t)b->burn_max);
        tag_put_i16(w, "cook", (int16_t)b->cook);
    }
    // THE FORGE'S JOB, in an i32 for the reason blockent.h gives: a
    // titanium item is 48000 ticks, and `cook`'s signed 16-bit tag stops
    // at 32767. A forge never writes `cook`, so the two fields never
    // disagree about anything.
    if (b->kind == BE_SMITH) tag_put_i32(w, "job", (int32_t)b->job);
    // WHAT THE STOVE IS SET TO COOK, by the dish's stable name. Not the
    // recipe's index: inserting a row in recipes.c would otherwise
    // repoint every stove ever saved (blockent.h).
    if ((b->kind == BE_STOVE || b->kind == BE_SMITH) && b->pick != 0)
        tag_put_str(w, "pick", item_def(b->pick).name);
    int const n = slots_used(b->kind);
    for (int i = 0; i < n; i++) {
        inv_slot_t const* s = &b->slot[i];
        if (s->item == 0 || s->count == 0) continue;  // empty slots cost nothing
        char key[8];
        snprintf(key, sizeof(key), "s%d", i);
        tag_begin(w, key);
        tag_put_str(w, "item", item_def(s->item).name);
        tag_put_i16(w, "n", (int16_t)s->count);
        tag_put_i16(w, "wear", (int16_t)s->wear);
        tag_end(w);
    }
}

size_t blockent_encode_chunk(int32_t cx, int32_t cz, uint8_t* out, size_t cap) {
    if (s_pool == NULL || out == NULL) return 0;

    int const n = blockent_count_in(cx, cz);
    if (n == 0) return 0;  // no records, no section

    // u8 id, u32 length, then u16 count and the records.
    if (cap < 7) return 0;
    size_t pos = 0;
    out[pos++] = SECTION_BLOCK_ENTITIES;
    size_t const len_at = pos;
    pos += 4;  // filled in once the contents are known
    size_t const body_at = pos;
    out[pos++]           = (uint8_t)(n & 0xFF);
    out[pos++]           = (uint8_t)((n >> 8) & 0xFF);

    int written = 0;
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t const* b = &s_pool[i];
        if (b->kind == BE_NONE) continue;
        if (chunk_of(b->x) != cx || chunk_of(b->z) != cz) continue;
        if (pos + 2 > cap) break;  // the buffer is full: write what is in it

        // Each record is a u16 length then its fields, so a reader can
        // step over one whose kind it does not know.
        size_t const rec_len_at = pos;
        pos += 2;
        tag_writer_t w;
        tag_write_init(&w, out + pos, cap - pos);
        write_record(&w, b);
        size_t const rec = tag_write_done(&w);
        if (rec == 0) break;     // ... and so is this one

        out[rec_len_at]     = (uint8_t)(rec & 0xFF);
        out[rec_len_at + 1] = (uint8_t)((rec >> 8) & 0xFF);
        pos += rec;
        written++;
    }

    // THE COUNT IS WHAT WENT IN, as mob.c does it: a reader told about a
    // record that is not there would walk off the end. A chest full of
    // slots is the biggest record in the format, so this is the encoder
    // most likely to run out of room -- and the one whose loss a player
    // would notice, because a furnace's contents are not regenerated.
    if (written < n) chunk_note_drops((uint32_t)(n - written));
    if (written == 0) return 0;
    out[body_at]     = (uint8_t)(written & 0xFF);
    out[body_at + 1] = (uint8_t)((written >> 8) & 0xFF);

    size_t const body = pos - body_at;
    out[len_at]       = (uint8_t)(body & 0xFF);
    out[len_at + 1]   = (uint8_t)((body >> 8) & 0xFF);
    out[len_at + 2]   = (uint8_t)((body >> 16) & 0xFF);
    out[len_at + 3]   = (uint8_t)((body >> 24) & 0xFF);
    return pos;
}

static void read_slot(tag_reader_t* r, inv_slot_t* out) {
    char     name[TAG_NAME_MAX + 1];
    uint16_t item = 0;
    int      n = 0, wear = 0;
    for (;;) {
        int const t = tag_next(r, name, sizeof(name));
        if (t == TAG_END || t < 0 || r->error) break;
        if (t == TAG_STR && strcmp(name, "item") == 0) {
            char buf[48];
            tag_get_str(r, buf, sizeof(buf));
            // A name this build does not know becomes nothing: there is
            // nothing honest to turn it into.
            item = item_by_name(buf);
        } else if (t == TAG_I16 && strcmp(name, "n") == 0) {
            n = tag_get_i16(r);
        } else if (t == TAG_I16 && strcmp(name, "wear") == 0) {
            wear = tag_get_i16(r);
        } else {
            tag_skip(r, t);
        }
    }
    if (item == 0 || n <= 0) return;
    int const cap = item_def(item).stack_max;
    out->item     = item;
    out->count    = (uint8_t)(n > cap ? cap : n);
    out->wear     = (uint16_t)(wear < 0 ? 0 : wear);
}

static void read_record(uint8_t const* data, size_t len) {
    tag_reader_t r;
    tag_read_init(&r, data, len);

    // Defaults first, then whatever the record actually carries, then
    // anything unrecognised stepped over -- the three lines of tags.h
    // that make a save format survive being added to.
    blockent_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    bool have_pos = false;
    int  got_xyz  = 0;

    char name[TAG_NAME_MAX + 1];
    for (;;) {
        int const t = tag_next(&r, name, sizeof(name));
        if (t == TAG_END || t < 0 || r.error) break;
        if (t == TAG_STR && strcmp(name, "kind") == 0) {
            char buf[24];
            tag_get_str(&r, buf, sizeof(buf));
            tmp.kind = kind_by_name(buf);
        } else if (t == TAG_I32 && strcmp(name, "x") == 0) {
            tmp.x = tag_get_i32(&r);
            got_xyz++;
        } else if (t == TAG_I32 && strcmp(name, "y") == 0) {
            tmp.y = tag_get_i32(&r);
            got_xyz++;
        } else if (t == TAG_I32 && strcmp(name, "z") == 0) {
            tmp.z = tag_get_i32(&r);
            got_xyz++;
        } else if (t == TAG_I32 && strcmp(name, "stamp") == 0) {
            tmp.stamp = (uint32_t)tag_get_i32(&r);
        } else if (t == TAG_I16 && strcmp(name, "burn") == 0) {
            tmp.burn_left = (uint16_t)tag_get_i16(&r);
        } else if (t == TAG_I16 && strcmp(name, "burnmax") == 0) {
            tmp.burn_max = (uint16_t)tag_get_i16(&r);
        } else if (t == TAG_I16 && strcmp(name, "cook") == 0) {
            tmp.cook = (uint16_t)tag_get_i16(&r);
        } else if (t == TAG_I32 && strcmp(name, "job") == 0) {
            tmp.job = (uint32_t)tag_get_i32(&r);
        } else if (t == TAG_STR && strcmp(name, "pick") == 0) {
            char buf[32];
            tag_get_str(&r, buf, sizeof(buf));
            // A dish this build does not have reads as "nothing chosen",
            // which is the right answer: the stove simply waits to be
            // told again rather than cooking something else.
            tmp.pick = item_by_name(buf);
        } else if (t == TAG_COMPOUND && name[0] == 's' && name[1] >= '0' && name[1] <= '9') {
            int const i = atoi(name + 1);
            if (i >= 0 && i < BE_SLOTS) {
                read_slot(&r, &tmp.slot[i]);
            } else {
                inv_slot_t discard = {0};
                read_slot(&r, &discard);
            }
        } else {
            tag_skip(&r, t);
        }
    }
    have_pos = got_xyz == 3;

    // A record with no kind is one this build does not know: skipped,
    // which is exactly what the format promises.
    if (tmp.kind == BE_NONE || !have_pos || r.error) return;

    blockent_t* b = blockent_add(tmp.x, tmp.y, tmp.z, tmp.kind);
    if (b == NULL) return;  // pool full; the chunk keeps its copy on disk
    uint8_t const kind = b->kind;
    *b                 = tmp;
    b->kind            = kind;
}

void blockent_decode_section(uint8_t const* data, size_t len) {
    if (s_pool == NULL || data == NULL || len < 2) return;
    size_t pos    = 0;
    int const cnt = (int)((unsigned)data[0] | ((unsigned)data[1] << 8));
    pos           = 2;
    for (int i = 0; i < cnt; i++) {
        if (pos + 2 > len) return;
        size_t const rec = (size_t)((unsigned)data[pos] | ((unsigned)data[pos + 1] << 8));
        pos += 2;
        if (pos + rec > len) return;  // truncated: stop, keep what parsed
        read_record(data + pos, rec);
        pos += rec;
    }
}

// =====================================================================
//  THE SLOW SWEEP (Part Q)
//
//  Everything above this line is passive: a machine only ever advances
//  when somebody opens it, and `now - stamp` does the rest. That is what
//  makes an unvisited furnace free, and it is also why a furnace across
//  the world looks dead even when it is burning.
//
//  This is the half that makes them ALIVE, and it is deliberately the
//  same two doors crops.c uses (world/crops.h, step 9): blockent_tick()
//  once a world tick while the chunk is resident, blockent_chunk_join()
//  once in a go when a chunk comes back. The arithmetic does not move.
//  Every *_catch_up() already spends a week's absence in one loop and is
//  idempotent, which is the only reason a per-tick pass can sit BESIDE
//  the UI's call instead of replacing it -- open a furnace and both run,
//  and the second one finds nothing to do.
//
//  WHAT IT COSTS. One walk over BE_MAX records per tick, and for all but
//  the handful that are machines it is a `kind` compare. A world with no
//  furnaces in it pays nothing, in the same way crops_tick() pays nothing
//  in a world with no farms. It runs on the main task, like every write
//  to a resident chunk.
// =====================================================================

static uint32_t s_now;

uint32_t blockent_now(void) {
    return s_now;
}

// WHICH BITS OF THE STATE BYTE this kind is allowed to write, and 0 for
// "the mesher shows nothing about this machine". See blocks.h: bits 2..3
// are MACHINE_LIT / MACHINE_DONE, and a cheese maker owns BARREL_* in
// bits 0..1 instead because it already had them.
static uint8_t machine_mask(uint8_t kind) {
    switch (kind) {
        case BE_FURNACE:
        case BE_STOVE:
        case BE_SMITH:   return MACHINE_LIT;
        case BE_COMPOST:
        case BE_SAUSAGE: return MACHINE_DONE;
        case BE_CHEESE:  return BARREL_EMPTY | BARREL_MILK | BARREL_CHEESE;
        default: return 0u;  // chest, trash
    }
}

// ... and WHICH BLOCK that cell is supposed to hold. The state byte
// belongs to the block, so writing a machine's bits into a cell that is
// no longer the machine would be a fire burning in a lump of air. It
// should not happen (breaking the block removes the record) and costs
// one compare to be certain it did not.
static uint8_t machine_block(uint8_t kind) {
    switch (kind) {
        case BE_FURNACE: return BLK_FURNACE;
        case BE_STOVE:   return BLK_STOVE;
        case BE_SMITH:   return BLK_SMITH;
        case BE_COMPOST: return BLK_COMPOSTER;
        case BE_SAUSAGE: return BLK_SAUSAGE_MAKER;
        case BE_CHEESE:  return BLK_CHEESE_MAKER;
        default: return 0u;
    }
}

// WHAT THOSE BITS SHOULD SAY right now.
//
// THE FLAME FOLLOWS THE IDLE RULE, NOT `burn_left`, and that is the
// user's "a machine stops when its output is full" made visible. Every
// catch-up loop already stops at a full output; what would have been
// wrong is drawing a fire in a stopped machine, because the fire is what
// the player reads as "it is working". So: no flame the moment the rule
// says it is not working -- and the fuel it already swallowed stays
// swallowed, because a furnace that has lit a lump of coal has burned
// it whether or not the output has room. Coming back is one keypress.
static uint8_t machine_bits(uint8_t kind, blockent_t const* be, blockent_t const* chest) {
    switch (kind) {
        case BE_FURNACE: return furnace_idle_reason(be) == FURNACE_IDLE_NONE ? MACHINE_LIT : 0u;
        case BE_STOVE:   return stove_idle_reason(be, chest) == STOVE_IDLE_NONE ? MACHINE_LIT : 0u;
        // NOT forge_busy(): that answers "is a job committed", which is
        // true while the forge is starved of coal halfway through a
        // titanium sword. The flame is the answer to "is it working".
        case BE_SMITH:   return forge_idle_reason(be, chest) == FORGE_IDLE_NONE ? MACHINE_LIT : 0u;
        case BE_COMPOST: return be->slot[BE_COMPOST_OUT].count > 0 ? MACHINE_DONE : 0u;
        case BE_SAUSAGE: return be->slot[BE_MAKER_OUT].count > 0 ? MACHINE_DONE : 0u;
        case BE_CHEESE:  return (uint8_t)maker_barrel_state(be);
        default: return 0u;
    }
}

// MIRROR THE RECORD INTO THE CELL. The mesher runs on core 1 out of block
// bytes and has never once seen a record; the state byte is the only
// channel between them, which is exactly how a cheese maker already shows
// milk and cheese.
//
// Cheap, and it says so: world_set() returns immediately when the block
// and byte are already what they should be, so a machine that has not
// changed state does not dirty its chunk, does not bump edit_seq and does
// not re-mesh. That is the common case -- the flame changes twice a day.
static void sync_cell(blockent_t const* be, blockent_t const* chest) {
    uint8_t const mask = machine_mask(be->kind);
    if (mask == 0u) return;

    int32_t const  x = be->x, y = be->y, z = be->z;
    if (world_block(x, y, z) != machine_block(be->kind)) return;
    uint8_t const  want = machine_bits(be->kind, be, chest);
    uint8_t const  st   = world_state(x, y, z);
    if ((uint8_t)(st_data(st) & mask) == want) return;

    world_set(x, y, z, world_block(x, y, z),
              st_with_data(st, (uint8_t)((st_data(st) & (uint8_t)~mask) | want)));
}

// WHAT THE MACHINE LOOKED LIKE BEFORE IT WAS RUN, so the sweep can tell
// whether anything actually moved. A tick that changes nothing must not
// mark the chunk dirty: that would be a save every tick for every furnace
// in the world, which is how a working game becomes a card writer.
//
// Four slots is the widest a machine uses (the makers have two in and two
// out); a chest is not a machine and never gets here.
typedef struct {
    uint16_t   burn_left, cook;
    uint32_t   job;
    uint8_t    n;
    inv_slot_t slot[4];
} be_watch_t;

static void watch_read(be_watch_t* w, blockent_t const* be) {
    int n = slots_used(be->kind);
    if (n > 4) n = 4;
    w->n         = (uint8_t)n;
    w->burn_left = be->burn_left;
    w->cook      = be->cook;
    w->job       = be->job;
    for (int i = 0; i < n; i++) w->slot[i] = be->slot[i];
}

static bool watch_moved(blockent_t const* be, be_watch_t const* w) {
    if (be->burn_left != w->burn_left || be->cook != w->cook || be->job != w->job) return true;
    for (int i = 0; i < w->n; i++) {
        if (be->slot[i].item != w->slot[i].item) return true;
        if (be->slot[i].count != w->slot[i].count) return true;
        if (be->slot[i].wear != w->slot[i].wear) return true;
    }
    return false;
}

// ONE RECORD, ONE TICK. `be` points into the pool, which is why nothing
// here may call anything that could move the pool: no add, no remove, no
// decode. The catch-up rules only ever write in place.
static void run_record(blockent_t* be, uint32_t now) {
    // THE RULE, before anything else: not while the worker is filling
    // this chunk's records, not while it is writing them. See blockent.h.
    chunk_t* c = chunk_find(chunk_of(be->x), chunk_of(be->z));
    if (c == NULL || c->cstate != CS_READY) return;

    // The stove and the forge eat out of the chest beside them, and both
    // idle rules ask for it. A chest in the next chunk over that is not
    // resident reads as STOVE_IDLE_NO_CHEST, which is the honest answer
    // and the same one the UI gets.
    blockent_t* chest = NULL;
    if (be->kind == BE_STOVE || be->kind == BE_SMITH) {
        int32_t ox, oy, oz;
        if (interact_pair_other(be->x, be->y, be->z, &ox, &oy, &oz)) {
            chest = blockent_at(ox, oy, oz);
        }
    }

    be_watch_t was;
    watch_read(&was, be);

    switch (be->kind) {
        case BE_FURNACE: furnace_catch_up(be, now); break;
        case BE_COMPOST: composter_catch_up(be, now); break;
        case BE_CHEESE:
        case BE_SAUSAGE: maker_catch_up(be, now); break;
        case BE_STOVE:   stove_catch_up(be, chest, now); break;
        case BE_SMITH:   forge_catch_up(be, now); break;
        default: return;  // chest and trash are not machines
    }

    sync_cell(be, chest);

    if (!watch_moved(be, &was)) return;
    blockent_touch(be);

    // A stove that cooked out of its chest changed a record the sweep is
    // not standing in, and the two halves can straddle a chunk border
    // (D-110). Mark that chunk too, or the ingredients vanish.
    if (be->kind == BE_STOVE && chest != NULL &&
        be->slot[BE_STOVE_OUT].count != was.slot[BE_STOVE_OUT].count) {
        blockent_touch(chest);
    }
}

void blockent_tick(uint32_t now) {
    s_now = now;
    if (s_pool == NULL) return;
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t* be = &s_pool[i];
        if (be->kind == BE_NONE) continue;
        run_record(be, now);
    }
}

// A CHUNK HAS ARRIVED. Every record standing in it is caught up by
// however long it was away -- the same one-loop catch-up the UI has
// always done, applied without anybody walking there.
//
// It reuses the ordinary path rather than adding a second one, and can:
// chunk_worker.c sets CS_READY before it calls the joins, so run_record's
// own state test passes. That is also what keeps the two from disagreeing
// about the rule.
void blockent_chunk_join(chunk_t* c, uint32_t now) {
    if (s_pool == NULL || c == NULL) return;
    if (s_now == 0u) s_now = now;
    for (int i = 0; i < BE_MAX; i++) {
        blockent_t* be = &s_pool[i];
        if (be->kind == BE_NONE) continue;
        if (chunk_of(be->x) != c->cx || chunk_of(be->z) != c->cz) continue;
        run_record(be, now);
    }
}
