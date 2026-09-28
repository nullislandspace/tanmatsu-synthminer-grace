// =====================================================================
//  SynthMiner  --  scheduled block updates (see blockupdate.h)
// =====================================================================

#include "world/blockupdate.h"

#include <string.h>
#include "common/psram.h"
#include "world/fluid.h"

// --- The wheel --------------------------------------------------------
//
// BU_WHEEL buckets, one per tick of horizon, and a tick drains exactly
// one of them. No sorting, no heap, no scan: scheduling is a push onto
// a list and running is walking one list. The cost of a tick is the
// number of cells actually due, and nothing else -- which is the whole
// point of the file.

typedef struct {
    int32_t x, z;
    int16_t next;  // -1 ends a list
    uint8_t y;
} bu_entry_t;

static bu_entry_t* s_pool;
static int16_t     s_wheel[BU_WHEEL];
static int16_t     s_free = -1;
static uint32_t    s_now;  // ticks; the low bits index the wheel

static int s_pending, s_peak, s_fired, s_dropped, s_carried;

// --- The active bit ---------------------------------------------------
//
// One bit per cell in the chunk's fourth plane. Set means "in the
// queue". See blockupdate.h: this is both the duplicate filter and the
// answer to "does this block have physics going on".

static uint8_t* act_bit(int32_t x, int32_t y, int32_t z, uint8_t* mask) {
    if (y < 0 || y >= CH_H) return NULL;
    chunk_t* c = chunk_find(chunk_of(x), chunk_of(z));
    // NOT RESIDENT IS NOT AN ERROR. A cell in a chunk that is not here
    // has nowhere to keep its bit, so it is simply not scheduled -- and
    // it does not need to be, because a chunk arriving wakes its own
    // unsettled cells (blockupdate_chunk_join).
    if (c == NULL || c->act == NULL) return NULL;
    size_t const i = CH_IDX(chunk_off(x), y, chunk_off(z));
    *mask          = (uint8_t)(1u << (i & 7u));
    return &c->act[i >> 3];
}

bool blockupdate_active(int32_t x, int32_t y, int32_t z) {
    uint8_t        m = 0;
    uint8_t const* p = act_bit(x, y, z, &m);
    return p != NULL && (*p & m) != 0;
}

// --- Life -------------------------------------------------------------

bool blockupdate_init(void) {
    if (s_pool != NULL) return true;
    s_pool = sm_calloc(BU_MAX, sizeof(bu_entry_t));
    if (s_pool == NULL) return false;
    blockupdate_clear();
    return true;
}

void blockupdate_shutdown(void) {
    sm_free(s_pool);
    s_pool = NULL;
    s_free = -1;
    for (int i = 0; i < BU_WHEEL; i++) s_wheel[i] = -1;
    s_pending = 0;
}

void blockupdate_clear(void) {
    for (int i = 0; i < BU_WHEEL; i++) s_wheel[i] = -1;
    s_free = -1;
    if (s_pool != NULL) {
        for (int i = BU_MAX - 1; i >= 0; i--) {
            s_pool[i].next = s_free;
            s_free         = (int16_t)i;
        }
    }
    s_pending = 0;
    s_now     = 0;
    s_carried = 0;
}

// --- Scheduling -------------------------------------------------------

void blockupdate_at(int32_t x, int32_t y, int32_t z, int delay) {
    if (s_pool == NULL) return;
    uint8_t  m = 0;
    uint8_t* p = act_bit(x, y, z, &m);
    if (p == NULL) return;
    // ALREADY WAITING. The time already asked for stands: re-linking a
    // cell to an earlier bucket would cost a search of the list it is
    // in, to buy a difference of at most a few ticks in something that
    // is deliberately slow.
    if ((*p & m) != 0) return;

    if (s_free < 0) {
        // NEVER SILENT. A refused update is a puddle that stops
        // half-way, and a queue that quietly declines work is exactly
        // the bug that took three days to find in F-113.
        s_dropped++;
        return;
    }

    int16_t const i = s_free;
    s_free          = s_pool[i].next;
    s_pool[i].x     = x;
    s_pool[i].z     = z;
    s_pool[i].y     = (uint8_t)y;

    if (delay < 1) delay = 1;
    if (delay > BU_WHEEL - 1) delay = BU_WHEEL - 1;
    int const b    = (int)((s_now + (uint32_t)delay) & (BU_WHEEL - 1));
    s_pool[i].next = s_wheel[b];
    s_wheel[b]     = i;

    *p |= m;
    if (++s_pending > s_peak) s_peak = s_pending;
}

// Wake one cell, at whatever rate its own block runs at. A cell whose
// block has no rules is not scheduled at all -- which is why digging
// out dry stone puts nothing in the queue.
static void wake(int32_t x, int32_t y, int32_t z) {
    int const d = fluid_delay(world_block(x, y, z));
    if (d > 0) blockupdate_at(x, y, z, d);
}

void blockupdate_around(int32_t x, int32_t y, int32_t z) {
    wake(x, y, z);
    wake(x + 1, y, z);
    wake(x - 1, y, z);
    wake(x, y + 1, z);
    wake(x, y - 1, z);
    wake(x, y, z + 1);
    wake(x, y, z - 1);
}

void blockupdate_block_changed(int32_t x, int32_t y, int32_t z, uint8_t was, uint8_t now) {
    (void)was;
    (void)now;
    // The cell AND its six neighbours, whatever changed. Cheaper to ask
    // than to work out which changes matter to which neighbour: wake()
    // already refuses anything without rules, so the test it would save
    // is the test it is doing.
    blockupdate_around(x, y, z);
}

// --- Running ----------------------------------------------------------

static void dispatch(int32_t x, int32_t y, int32_t z) {
    // One line per kind of physics. A cell in a chunk that has since
    // gone reads as BLK_BARRIER and falls through all of them.
    if (fluid_has_rules(world_block(x, y, z))) fluid_update(x, y, z);
}

int blockupdate_tick(void) {
    if (s_pool == NULL) return 0;
    s_now++;
    int const b    = (int)(s_now & (BU_WHEEL - 1));
    int const next = (int)((s_now + 1u) & (BU_WHEEL - 1));

    int16_t i  = s_wheel[b];
    s_wheel[b] = -1;
    int fired  = 0;
    s_carried  = 0;

    while (i >= 0) {
        int16_t const after = s_pool[i].next;
        if (fired >= BU_PER_TICK) {
            // OVER BUDGET. Carried to the next tick with its bit still
            // set, so it keeps its place and cannot be scheduled twice.
            // A fiftieth of a second later is not a difference anybody
            // can see; a frame that took twice as long is.
            s_pool[i].next = s_wheel[next];
            s_wheel[next]  = i;
            s_carried++;
        } else {
            int32_t const x = s_pool[i].x, z = s_pool[i].z;
            int const     y = (int)s_pool[i].y;

            // CLEAR THE BIT AND GIVE THE ENTRY BACK *BEFORE* RUNNING.
            // The update is about to call world_set(), which wakes this
            // same cell again -- and that has to be allowed to take, or
            // a flow would stop after one step.
            uint8_t  m = 0;
            uint8_t* p = act_bit(x, y, z, &m);
            if (p != NULL) *p &= (uint8_t)~m;

            s_pool[i].next = s_free;
            s_free         = i;
            s_pending--;

            // A CELL WHOSE CHUNK HAS GONE is skipped, and note that the
            // bit was not ours to clear: chunk_claim() wiped the plane
            // when the slot was taken over, so act_bit() above either
            // found nothing or found the NEW chunk -- which is why
            // chunk_find()'s identity check has to be the one deciding.
            if (p != NULL) dispatch(x, y, z);
            fired++;
            s_fired++;
        }
        i = after;
    }
    return fired;
}

// --- A chunk arriving -------------------------------------------------

// Every cell of `c` that still has something to do. The cheap test
// first: a fluid cell whose five lower-and-sideways neighbours are all
// full fluid of its own kind can do nothing, whatever else is true, and
// that is almost every cell of an ocean.
static bool boxed_in(chunk_t const* c, int lx, int y, int lz, uint8_t b) {
    if (lx == 0 || lx == CH_W - 1 || lz == 0 || lz == CH_D - 1 || y == 0) return false;
    size_t const n[5] = {
        CH_IDX(lx, y - 1, lz), CH_IDX(lx + 1, y, lz), CH_IDX(lx - 1, y, lz),
        CH_IDX(lx, y, lz + 1), CH_IDX(lx, y, lz - 1),
    };
    for (int i = 0; i < 5; i++) {
        if (c->id[n[i]] != b || !fluid_is_full(c->st[n[i]])) return false;
    }
    return true;
}

static void wake_own(chunk_t* c) {
    int32_t const bx = c->cx * CH_W, bz = c->cz * CH_D;
    for (int lz = 0; lz < CH_D; lz++) {
        for (int lx = 0; lx < CH_W; lx++) {
            uint8_t const* col = &c->id[CH_IDX(lx, 0, lz)];
            uint8_t const* sol = &c->st[CH_IDX(lx, 0, lz)];
            for (int y = 0; y < CH_H; y++) {
                uint8_t const b = col[y];
                if (!fluid_has_rules(b)) continue;
                if (fluid_is_source(sol[y]) && boxed_in(c, lx, y, lz, b)) continue;
                if (fluid_unsettled(bx + lx, y, bz + lz)) blockupdate_at(bx + lx, y, bz + lz, fluid_delay(b));
            }
        }
    }
}

// THE SEAM, and the reason this function exists at all. Water in a
// chunk that was already here may have been sitting against the wall
// that a missing chunk reads as (BLK_BARRIER, D-14). That wall has just
// become terrain, so every fluid cell on the four facing borders is
// asked one question -- could you now spread into the cell in front of
// you? -- and woken if the answer is yes.
//
// Only the facing column, and only the horizontal question: falling is
// vertical and never leaves a chunk.
static void wake_seam(chunk_t const* c) {
    static int const DCX[4] = {1, -1, 0, 0};
    static int const DCZ[4] = {0, 0, 1, -1};

    for (int d = 0; d < 4; d++) {
        chunk_t const* n = chunk_find(c->cx + DCX[d], c->cz + DCZ[d]);
        if (n == NULL) continue;
        int32_t const nbx = n->cx * CH_W, nbz = n->cz * CH_D;

        for (int t = 0; t < CH_W; t++) {
            // The neighbour's border cell, and the cell of `c` it faces.
            int nlx, nlz, olx, olz;
            if (DCX[d] == 1) {
                nlx = 0, nlz = t, olx = CH_W - 1, olz = t;
            } else if (DCX[d] == -1) {
                nlx = CH_W - 1, nlz = t, olx = 0, olz = t;
            } else if (DCZ[d] == 1) {
                nlx = t, nlz = 0, olx = t, olz = CH_D - 1;
            } else {
                nlx = t, nlz = CH_D - 1, olx = t, olz = 0;
            }

            for (int y = 0; y < CH_H; y++) {
                size_t const       i  = CH_IDX(nlx, y, nlz);
                uint8_t const      b  = n->id[i];
                fluid_def_t const* fd = fluid_def(b);
                if (fd == NULL) continue;
                uint8_t const st  = n->st[i];
                int const     out = fluid_is_falling(st) ? 1 : (int)fluid_level(st) + 1;
                if (out > (int)fd->reach) continue;
                // Would it reach into the chunk that has just arrived?
                size_t const  j  = CH_IDX(olx, y, olz);
                uint8_t const ob = c->id[j];
                bool const    ok = (ob == b) ? ((int)fluid_level(c->st[j]) > out) : block_replaceable(ob);
                if (ok) blockupdate_at(nbx + nlx, y, nbz + nlz, (int)fd->delay);
            }
        }
    }
}

void blockupdate_chunk_join(chunk_t* c) {
    if (c == NULL || s_pool == NULL) return;
    wake_own(c);
    wake_seam(c);
}

// --- Counters ---------------------------------------------------------

blockupdate_stats_t blockupdate_stats(void) {
    blockupdate_stats_t s = {
        .pending = s_pending, .peak = s_peak, .fired = s_fired, .dropped = s_dropped, .carried = s_carried};
    return s;
}

void blockupdate_stats_reset(void) {
    s_peak = s_fired = s_dropped = s_carried = 0;
}
