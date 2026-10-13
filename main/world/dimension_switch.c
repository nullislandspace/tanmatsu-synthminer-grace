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

// How long one drain may take, in milliseconds. A healthy save is 6.6/21.5 ms
// a chunk (chunk_worker.h), so this covers a whole ring of them several times
// over; past it the card is not slow, it is not answering, and refusing is
// the answer that keeps the game running.
#define SWITCH_DRAIN_MS 4000

// Wait for the worker, and say out loud what it was still doing. Returns
// false when the switch has to refuse.
static bool drain_or_refuse(char const* what) {
    if (chunk_worker_drain(SWITCH_DRAIN_MS)) return true;
    chunk_worker_flow_t f;
    chunk_worker_flow(&f);
    trace_note("D switch refused: %s (%d job(s) still running)", what, f.in_flight);
    return false;
}

bool dimension_switch(int32_t x, int32_t z, dimension_t to) {
    dimension_t const from = dimension_current();
    if (from == to) return true;

    // 0. LET THE WORKER FINISH WHAT IT HAS ALREADY STARTED.
    //
    // A live streamer is never quiet: it has jobs in the queue on almost
    // every frame, and a switch that required an idle worker would refuse
    // on nearly every crossing a player ever made. A queue is a wait, not
    // a refusal. What genuinely cannot be saved is a chunk whose planes
    // are still the worker's (Part K), and draining is what separates the
    // two -- everything that can become saveable does, and what is left
    // below is a chunk that truly cannot be.
    //
    // AND IT HAS TO ACTUALLY WAIT. Until 2026-10-13 this was
    // `while (chunk_worker_collect(N) > 0)`, and collect() is a non-blocking
    // peek (xQueueReceive with a zero timeout): that loop takes delivery of
    // whatever has ALREADY finished and ends the moment nothing has, which on
    // a streaming world -- 9/48 jobs in flight in an ordinary second of play --
    // means it never waited for anything. Every check below then saw a busy
    // worker and refused, so a shaft said "would not open" and a death in the
    // Deep Mine never crossed home (F-160).
    if (!drain_or_refuse("the worker had not finished streaming")) return false;

    // 1. SAVE EVERY DIRTY CHUNK.
    //
    // Freshly generated chunks are CF_EDITED from the moment they are
    // built, so a ring during play is dirty almost everywhere. That is not
    // a reason to refuse: it is the write point D-141 says a crossing has,
    // and it is the same rule Part P applies to eviction -- a chunk the
    // card did not take is something the player made.
    for (int pass = 0; pass < SWITCH_SAVE_PASSES; pass++) {
        int dirty = 0;
        for (int i = 0; i < CH_SLOT_COUNT; i++) {
            chunk_t* c = chunk_slot_at(i);
            if (c == NULL || c->cstate == CS_FREE) continue;
            if (c->cstate == CS_LOADING) {
                trace_note("D switch refused: chunk %ld,%ld was still being filled", (long)c->cx, (long)c->cz);
                return false;
            }
            if (!(c->flags & CF_EDITED)) continue;
            dirty++;
            // A full queue is not a failure; it is a retry next pass.
            (void)chunk_worker_request_save(c->cx, c->cz);
        }
        if (dirty == 0) break;
        if (!drain_or_refuse("the card had not taken the saves yet")) return false;
    }

    // 2. DRAIN THE WORKER, and check what it could not write. A save the
    // card refused leaves the chunk CF_EDITED -- the streamer will not
    // evict a chunk it could not write -- so anything still dirty here is
    // a chunk this switch would have to throw away. It does not.
    if (!drain_or_refuse("still draining after the last save pass")) return false;

    for (int i = 0; i < CH_SLOT_COUNT; i++) {
        chunk_t* c = chunk_slot_at(i);
        if (c == NULL || c->cstate == CS_FREE) continue;
        if (c->flags & CF_EDITED) {
            trace_note("D switch refused: chunk %ld,%ld never reached the card", (long)c->cx, (long)c->cz);
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
    trace_note("D switch %s -> %s at %ld,%ld (%d chunks released)", dimension_name(from), dimension_name(to),
               (long)x, (long)z, released);

    // 5 is the caller's: the 3x3 around the arrival point, loaded
    // synchronously, and the player moved into it.
    dimension_set_current(to);
    return true;
}
