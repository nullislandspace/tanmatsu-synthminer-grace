// =====================================================================
//  SynthMiner  --  crossing between dimensions (see dimension_switch.h)
// =====================================================================

#include "world/dimension_switch.h"

#include "common/trace.h"
#include "world/chunk.h"
#include "world/chunk_worker.h"

// How many passes the save gets before it is called a failure. The queue
// holds fewer jobs than the ring holds chunks, so one pass is never
// enough on a full ring; a card that is healthy drains in two or three.
// The bound is here so a card that has stopped answering cannot hang the
// game -- it is the same reason the retry layer in Part P has a bound.
#define SWITCH_SAVE_PASSES 8

bool dimension_switch(int32_t x, int32_t z, dimension_t to) {
    dimension_t const from = dimension_current();
    if (from == to) return true;

    // 1. SAVE EVERY DIRTY CHUNK.
    //
    // A chunk the worker is still filling cannot be saved -- its planes
    // are the worker's until it hands them over (Part K) -- so a switch
    // is only ever possible when the store is quiet. That is also what
    // makes this loop safe: nothing changes state underneath it.
    for (int pass = 0; pass < SWITCH_SAVE_PASSES; pass++) {
        int dirty = 0;
        for (int i = 0; i < CH_SLOT_COUNT; i++) {
            chunk_t* c = chunk_slot_at(i);
            if (c == NULL || c->cstate == CS_FREE) continue;
            if (c->cstate == CS_LOADING) return false;
            if (!(c->flags & CF_EDITED)) continue;
            dirty++;
            // A full queue is not a failure; it is a retry next pass.
            (void)chunk_worker_request_save(c->cx, c->cz);
        }
        if (dirty == 0) break;
        while (chunk_worker_collect(CH_SLOT_COUNT) > 0) {
        }
    }

    // 2. DRAIN THE WORKER, and check what it could not write. A save the
    // card refused leaves the chunk CF_EDITED -- the streamer will not
    // evict a chunk it could not write -- so anything still dirty here is
    // a chunk this switch would have to throw away. It does not.
    while (chunk_worker_collect(CH_SLOT_COUNT) > 0) {
    }
    if (!chunk_worker_idle()) return false;

    for (int i = 0; i < CH_SLOT_COUNT; i++) {
        chunk_t* c = chunk_slot_at(i);
        if (c == NULL || c->cstate == CS_FREE) continue;
        if (c->flags & CF_EDITED) {
            trace_note("D switch refused: chunk %d,%d never reached the card", c->cx, c->cz);
            return false;
        }
    }

    // 3. RELEASE EVERY CHUNK, through the one path. This is the largest
    // one-time eviction the game will ever do, and a switch that skipped
    // it would keep the creatures and the block entities of a dimension
    // that is no longer resident -- the pools would never come back, and
    // the next dimension would find them full.
    //
    // It also does step 4 by itself: chunk_release() frees the slot's
    // meshes before marking it free. The explicit trim after it is the
    // order in Part M written down, and it costs nothing on a store that
    // is already empty.
    int released = 0;
    for (int i = 0; i < CH_SLOT_COUNT; i++) {
        chunk_t* c = chunk_slot_at(i);
        if (c == NULL || c->cstate == CS_FREE) continue;
        chunk_release(c);
        released++;
    }

    // 4. Free the mesh cache. Nothing in it is valid after the switch.
    (void)chunk_store_trim_meshes((double)x, (double)z, 0.0f);

    // 6. Record it as an event, not as a position. A replay that only
    // stored where the player was would replay a session that crossed
    // over into the wrong dimension.
    trace_note("D switch %s -> %s at %d,%d (%d chunks released)", dimension_name(from), dimension_name(to), x, z,
               released);

    // 5 is the caller's: the 3x3 around the arrival point, loaded
    // synchronously, and the player moved into it.
    dimension_set_current(to);
    return true;
}
