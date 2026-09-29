// =====================================================================
//  SynthMiner  --  the background chunk task (see chunk_worker.h)
// =====================================================================

#include "world/chunk_worker.h"

#include "common/trace.h"
#include "world/blockupdate.h"
#include "world/crops.h"
#include "world/light.h"
#include <string.h>
#include "common/psram.h"
#include "world/chunkmesh.h"
#include "world/worldgen.h"
#include "world/worldstore.h"

#ifndef SM_HOST
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static char const TAG[] = "cmworker";

// Below the engine's residents on core 1: the audio mixer sits at
// configMAX_PRIORITIES-2, the PPA pump at -3, the MP3 decoder at -4.
// -6 leaves -5 free for a second worker later without re-tuning
// anything. Chunks are never more urgent than sound.
// PRIORITY BY DEADLINE, AND CHUNK LOADING HAS THE LOOSEST ONE ON THIS CORE.
//
// This was configMAX_PRIORITIES - 6, about 19, which put it ABOVE both the
// engine's A/V stream task (5) and its USB network task (10) on the same
// core. That is the wrong order, and it was measured: while the player
// moved and this task had work, it preempted the stream task completely --
// encoded frames fell from 22 a second to 0.7 while each encode still took
// only 8.8 ms, and 42% of the audio never got encoded at all. The stream
// did not slow down, it stopped getting the CPU.
//
// Chunk work is the one job here that can be late without anything
// noticing. There are always chunks loaded around the player, so a second
// or two of lag shows up as terrain appearing a little further out rather
// than as a gap in the sound or a frozen picture. Audio cannot be late at
// all, and video frames carry timestamps a receiver schedules against.
//
// Safe because NOTHING WAITS ON THIS TASK: chunk_worker_collect() polls its
// queue with a zero timeout, so the render loop takes whatever is ready and
// never blocks, and synchronous mode does the work inline on the caller's
// thread rather than through this task. So there is no priority inversion
// to create by lowering it.
//
// Core 1's ladder, tightest deadline first:
//   audio_mixer  ~23  must feed the I2S DMA or the speaker glitches
//   usbnet        10  moves bytes for the link
//   se_stream      5  A/V encode and mux
//   cmworker       4  <- here: has slack, degrades invisibly
#define WORKER_PRIO  4
#define WORKER_CORE  1
#define WORKER_STACK 6144
#define QUEUE_DEPTH  48
#endif

typedef enum {
    JOB_LOAD = 0,
    JOB_MESH,
    JOB_SAVE
} job_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t lod;
    uint8_t seq;   // the chunk's edit_seq when this was queued
    uint8_t sect;  // JOB_MESH only: which vertical section (D-34)
    int32_t cx, cz;
} job_t;

// Whether a job waits its turn or goes to the head of the queue. See
// submit(), which is where the reasoning is.
typedef enum {
    Q_NORMAL = 0,
    Q_FRONT
} queue_where_t;

typedef struct {
    uint8_t kind;
    uint8_t lod;
    uint8_t seq;
    uint8_t sect;
    uint8_t ok;
    int32_t cx, cz;
    mesh_t  mesh;  // JOB_MESH only; ownership passes to main on receive
} result_t;

static uint32_t s_seed;
static int32_t  s_farlands_x = FARLANDS_X_DEFAULT;
// Generation time, ordinary chunks [0] and Far Lands [1], since boot.
// Written by the worker, read by the stats log: a torn read costs one
// log line a wrong average, nothing more.
static int64_t  s_gen_us[2];
static int32_t  s_gen_n[2];

// And the CARD: how long a chunk takes to come off it and go back on.
// Generation is arithmetic and scales with the CPU; these two are the
// filesystem, and they are what a directory full of region files, or a
// cache that is working, actually shows up in. Worst as well as total,
// because a mean hides the one that stalled a frame.
static int64_t  s_load_us, s_save_us;
static int64_t  s_load_max, s_save_max;
static int32_t  s_load_n, s_save_n;
static int64_t  s_compact_us, s_compact_max;
static int32_t  s_compact_n;
static int32_t  s_save_failed;
static int32_t  s_paced_ms, s_paced_n;   // time given back to the card  // writes the card refused; see apply()

void chunk_worker_io_stats(chunk_worker_io_t* out) {
    if (out == NULL) return;
    out->load_n   = s_load_n;
    out->load_us  = s_load_us;
    out->load_max = s_load_max;
    out->save_n   = s_save_n;
    out->save_us  = s_save_us;
    out->save_max = s_save_max;
    out->compact_n   = s_compact_n;
    out->compact_us  = s_compact_us;
    out->compact_max = s_compact_max;
    out->save_failed = s_save_failed;
    out->paced_ms    = s_paced_ms;
    out->paced_n     = s_paced_n;
}

void chunk_worker_gen_stats(int* n_ord, int64_t* us_ord, int* n_far, int64_t* us_far) {
    *n_ord  = s_gen_n[0];
    *us_ord = s_gen_us[0];
    *n_far  = s_gen_n[1];
    *us_far = s_gen_us[1];
}
static bool     s_sync = true;  // until the task starts, everything is inline
static bool     s_running;
static uint8_t* s_scratch;  // the worker's own mesher box (F-08)
static int      s_loaded_total, s_meshed_total;
// Why a frame's worth of work did not get done. A streaming world can
// fail in three ways that all look identical on screen -- the world
// lagging behind the camera -- and only these tell them apart: the job
// queue full (the worker is behind), the result queue not drained (MAIN
// is behind), or nothing asked for in the first place.
static int      s_saved_total, s_refused_total, s_applied_total, s_asked_total;

#ifndef SM_HOST
static QueueHandle_t s_jobs;
static QueueHandle_t s_done;
static TaskHandle_t  s_task;
static volatile int  s_in_flight;
#endif

// --- The work itself, wherever it runs --------------------------------

// Bring a chunk into residence: from the card if it is there, generated
// if it is not. Runs on the worker while the chunk is CS_LOADING, so
// nothing else may look at it.
static bool do_load(int32_t cx, int32_t cz) {
    // chunk_slot_claimed, not chunk_find: the chunk is CS_LOADING, which
    // chunk_find hides on purpose so no game code reads a half-filled
    // one. This is the code doing the filling.
    chunk_t* c = chunk_slot_claimed(cx, cz);
    if (c == NULL || c->cstate != CS_LOADING) return false;  // main gave up the slot

#ifndef SM_HOST
    int64_t const tl0 = esp_timer_get_time();
#endif
    int const r = world_chunk_load(c);
#ifndef SM_HOST
    if (r == 1) {
        int64_t const dt = esp_timer_get_time() - tl0;
        s_load_us += dt;
        if (dt > s_load_max) s_load_max = dt;
        s_load_n++;
    }
#endif
    if (r == 1) {
        // Repair the floor of a chunk written before it was forced. Any
        // chunk from the card may predate that rule, and re-forcing one
        // that does not is 256 stores (worldgen.h).
        worldgen_force_floor(c);
        c->flags |= CF_GENERATED;
        // AND MARK IT DIRTY, although nothing has touched it yet. The
        // user's call, 2026-09-28: "if a player loads then unloads an
        // existing chunk, we probably also need to always save that
        // chunk, for animal movements, crops growing etc."
        //
        // A chunk is not a photograph. Time passes in it while it is
        // resident -- animals walk, crops grow, furnaces burn down --
        // and none of that is worth anything if leaving the area throws
        // it away. It is also what makes the paragraph above work: the
        // repaired floor reaches the card because the chunk is written
        // back, without a migration pass to write.
        c->flags |= CF_EDITED;
        chunk_resummarise(c);
        light_chunk_local(c);  // its own light, here on core 1 (light.h)
        return true;
    }
    if (r < 0) return false;

    // Not on the card: this is the first time anyone has been here.
#ifndef SM_HOST
    int64_t const t0 = esp_timer_get_time();
#endif
    worldgen_chunk(c, s_seed, s_farlands_x);
#ifndef SM_HOST
    int const kind = farlands_chunk_is(cx, s_farlands_x) ? 1 : 0;
    s_gen_us[kind] += esp_timer_get_time() - t0;
    s_gen_n[kind]++;
#endif
    light_chunk_local(c);
    // Freshly generated and not yet written, so it has to be saved
    // before the slot can be reused.
    c->flags |= CF_EDITED;
    return true;
}

static bool do_mesh(int32_t cx, int32_t cz, int lod, int sect, mesh_t* out) {
    return chunkmesh_build(cx, cz, lod, sect, s_scratch, out);
}

// --- Giving the card room to breathe ----------------------------------
//
// WHY THE CARD STALLS. An SD card does its own erase-block management,
// and a write that lands on a block needing an erase starts an internal
// cycle that can hold the bus busy for hundreds of milliseconds. If the
// host's timeout is shorter than that, the transaction fails --
// `sdmmc_write_blocks failed (0x106)`, which is ESP_ERR_TIMEOUT.
//
// And we feed it the worst possible pattern: a burst of many small
// writes scattered across different region files, each with its own
// flush and its own FAT metadata update. 81 of them back to back when
// the title world is built, 49 when a world is left.
//
// So: WHEN THE CARD SHOWS SIGNS OF BEING BUSY, STOP WRITING FOR A
// MOMENT. A healthy save here is 12-20 ms and gets no delay at all --
// paying a pause on every write would cost more than it saves. One that
// took far longer means the card was already doing internal work, and
// the next write is the one most likely to time out.
#define SAVE_SLOW_US    (60 * 1000)   // beyond this, the card is busy
#define SAVE_PACE_MAX_MS 120
#define SAVE_FAIL_PACE_MS 100         // it already refused; do not rush back

static void pace_after_write(int64_t took_us, bool ok) {
#ifndef SM_HOST
    int ms = 0;
    if (!ok) {
        ms = SAVE_FAIL_PACE_MS;
    } else if (took_us > SAVE_SLOW_US) {
        // Half of what it just cost: enough to matter, bounded so a
        // pathological reading cannot stall the streamer outright.
        ms = (int)(took_us / 2000);
        if (ms > SAVE_PACE_MAX_MS) ms = SAVE_PACE_MAX_MS;
    }
    if (ms <= 0) return;
    s_paced_ms += ms;
    s_paced_n++;
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    (void)took_us;
    (void)ok;
#endif
}

static bool do_save(int32_t cx, int32_t cz) {
    chunk_t const* c = chunk_find(cx, cz);
    if (c == NULL) return false;
#ifndef SM_HOST
    int64_t const ts0 = esp_timer_get_time();
#endif
    bool const wrote = world_chunk_save(c);
#ifndef SM_HOST
    int64_t const dt = esp_timer_get_time() - ts0;
    if (wrote) {
        s_save_us += dt;
        if (dt > s_save_max) s_save_max = dt;
        s_save_n++;
    }
    pace_after_write(dt, wrote);
#endif
    if (!wrote) return false;
    // Rewrites leave dead bytes behind. Tidying them up is a whole-file
    // rewrite, so it happens here on core 1 and only when the waste has
    // actually built up -- never on the frame path.
    //
    // TIMED SEPARATELY, and it was not timed at all until the first
    // real trace showed a 236 ms save and left no way to tell whether
    // this was in it. It is not: the window above closes before this
    // runs, so a compaction was invisible. A whole-region rewrite is
    // 40-230 KiB, so it is the one thing here that could plausibly
    // cost a quarter of a second.
#ifndef SM_HOST
    int64_t const tc0 = esp_timer_get_time();
#endif
    bool const compacted = world_region_maintain(cx, cz);
#ifndef SM_HOST
    // Counted on the RETURN VALUE, not on elapsed time. The first
    // version called anything over a millisecond a compaction, which
    // cannot tell a real rewrite from a slow should-compact check --
    // and it reported 316 compactions for 316 saves, which is exactly
    // the sort of too-round number that turns out to be the instrument.
    if (compacted) {
        int64_t const dt = esp_timer_get_time() - tc0;
        s_compact_us += dt;
        if (dt > s_compact_max) s_compact_max = dt;
        s_compact_n++;
    }
#else
    (void)compacted;
#endif
    return true;
}

// Apply a finished job on the MAIN task. Everything that changes a
// chunk's visible state happens here, which is what makes the contract
// hold without locks.
static void apply(result_t* r) {
    chunk_t*   c    = &(*chunk_slot_at(chunk_slot(r->cx, r->cz)));
    bool const mine = c->cx == r->cx && c->cz == r->cz;

    if (r->kind == JOB_LOAD) {
        if (mine && c->cstate == CS_LOADING) {
            c->cstate = r->ok ? CS_READY : CS_FREE;
            if (r->ok) {
                s_loaded_total++;
                c->lod_stale = CH_MESH_ALL;
                c->lod_built = 0;
                // The light it trades with the chunks already here -- its
                // own was worked out on the worker. On the main task,
                // like every write to a resident chunk (light.h).
                light_chunk_join(c);
                // And the physics seam: this chunk's own unsettled
                // fluid, and the water in the chunks already here that
                // has been standing against the wall a missing chunk
                // reads as (blockupdate.h, the boundary note). Same
                // place, same task, same reason as the light.
                blockupdate_chunk_join(c);
                // And the slow half of the same idea: everything in
                // this chunk that grows, advanced by however long it
                // was away (world/crops.h -- the user's "advance events
                // in one go to where they would be now as if the chunk
                // was never unloaded").
                crops_chunk_join(c, crops_now());
            }
        }
        return;
    }

    if (r->kind == JOB_MESH) {
        // Stale if the slot moved on, or the chunk was edited after the
        // job was queued. Either way the mesh describes a world that no
        // longer exists, so throw it away rather than show it.
        bool const valid = r->lod < LOD_COUNT && r->sect < CH_SECT_N;
        bool const fresh = mine && c->cstate == CS_READY && c->edit_seq == r->seq && r->ok;
        if (fresh && valid) {
            mesh_t* slot = chunk_mesh(c, r->lod, r->sect);
            mesh_free(slot);
            *slot         = r->mesh;  // the swap: one pointer, between frames
            // Only now is it not stale -- and only if nothing marked it
            // again while the job was in flight, which the edit_seq
            // check above has already ruled out.
            c->lod_stale &= (uint16_t)~CH_MESH_BIT(r->lod, r->sect);
            // The edit has been answered; this section goes back to
            // being ordinary streaming work (chunk.h, lod_urgent).
            c->lod_urgent &= (uint16_t)~CH_MESH_BIT(r->lod, r->sect);
            // Empty is a legitimate answer -- solid rock, open sky --
            // and an empty mesh is not "drawable", it is "nothing to
            // draw". Only real geometry counts as built, or the
            // fallback below would pick an empty mesh over a good one.
            // The trace wants this moment and nothing else does: it is
            // the end of the wait between changing a block and being
            // able to see the change.
            trace_mesh(c->cx, c->cz, r->lod, r->sect);
            if (slot->tn > 0) c->lod_built |= CH_MESH_BIT(r->lod, r->sect);
            else c->lod_built &= (uint16_t)~CH_MESH_BIT(r->lod, r->sect);
            memset(&r->mesh, 0, sizeof(r->mesh));
            s_meshed_total++;
        }
        if (r->mesh.v != NULL || r->mesh.t != NULL) mesh_free(&r->mesh);
        if (mine && valid) c->lod_inflight &= (uint16_t)~CH_MESH_BIT(r->lod, r->sect);
        return;
    }

    if (r->kind == JOB_SAVE) {
        if (r->ok) s_saved_total++;
        // A WRITE THAT FAILED. The card is what fails here: the badge
        // reported dozens of `sdmmc_write_blocks failed (0x106)` under
        // save bursts on 2026-09-28, and nothing in the game noticed --
        // the driver logged it to a console a player does not have, and
        // every counter carried on as if the world were being written.
        //
        // Nothing is LOST when this happens, and that is by design: the
        // chunk keeps CF_EDITED below, and the streamer refuses to evict
        // a chunk it could not write, so the save is retried for as long
        // as it takes. What was missing was anybody saying so.
        else s_save_failed++;
        // ONLY IF NOTHING CHANGED WHILE IT WAS BEING WRITTEN. The mesh
        // path above has always checked edit_seq; this one did not, and
        // cleared CF_EDITED on any successful save. A write that raced
        // an edit would then be the last word: the chunk is not dirty
        // any more, so it is never written again, and the edit is gone.
        //
        // Not reachable by a PLAYER -- a save is triggered by drifting
        // past the evict radius, 80 blocks and up, and reach is 4.5.
        // But the writers that are coming do not have hands: flowing
        // water, growing crops and animals walking all change chunks
        // nobody is standing in, which is exactly where this fires.
        // Leaving the bit set costs one more save; clearing it wrongly
        // costs the edit.
        if (mine && r->ok && c->edit_seq == r->seq) c->flags &= (uint8_t)~CF_EDITED;
        if (mine && c->cstate == CS_SAVING) c->cstate = CS_READY;
    }
}

// Do one job and produce its result. Runs on the worker, or inline in
// synchronous mode.
static void run_job(job_t const* j, result_t* r) {
    memset(r, 0, sizeof(*r));
    r->kind = j->kind;
    r->lod  = j->lod;
    r->seq  = j->seq;
    r->sect = j->sect;
    r->cx   = j->cx;
    r->cz   = j->cz;

    switch (j->kind) {
        case JOB_LOAD:
            r->ok = do_load(j->cx, j->cz) ? 1 : 0;
            break;
        case JOB_MESH:
            r->ok = do_mesh(j->cx, j->cz, j->lod, j->sect, &r->mesh) ? 1 : 0;
            break;
        case JOB_SAVE:
            r->ok = do_save(j->cx, j->cz) ? 1 : 0;
            break;
        default:
            break;
    }
}

// --- Submitting -------------------------------------------------------

// A PLAYER'S EDIT GOES TO THE FRONT. Measured on the badge,
// 2026-09-28: a flower broken at t=173.0 did not leave the screen until
// t=176.9 -- 3943 ms -- because its remesh queued behind forty-odd
// speculative streaming jobs while `queue` sat at 49/48. The block was
// gone from the world the instant it broke; what the player was looking
// at was a mesh from before the swing. That is the whole of "stuff i
// place isn't showing up visually".
//
// Everything else in this queue is work nobody is waiting on: a section
// the streamer has not reached yet is behind fog or behind a coarser
// level, and a save is invisible by definition. One job a person is
// staring at beats all of it -- and a pickaxe swings a few times a
// second at most, so the front of the queue cannot be flooded from
// here.
static bool submit(job_t const* j, queue_where_t where) {
    if (s_scratch == NULL) return false;

    if (s_sync) {
        result_t r;
        run_job(j, &r);
        apply(&r);
        return true;
    }
#ifndef SM_HOST
    BaseType_t const sent = where == Q_FRONT ? xQueueSendToFront(s_jobs, j, 0) : xQueueSend(s_jobs, j, 0);
    if (sent != pdTRUE) {
        s_refused_total++;
        return false;
    }
    s_in_flight++;
    s_asked_total++;
    return true;
#else
    (void)where;
    return false;
#endif
}

bool chunk_worker_request_load(int32_t cx, int32_t cz) {
    chunk_t* c = chunk_claim(cx, cz);
    if (c == NULL) return false;  // the slot is busy; ask again next frame
    job_t const j = {.kind = JOB_LOAD, .cx = cx, .cz = cz};
    if (!submit(&j, Q_NORMAL)) {
        c->cstate = CS_FREE;  // never leave a slot stuck in CS_LOADING
        return false;
    }
    return true;
}

bool chunk_worker_request_mesh(int32_t cx, int32_t cz, int lod, int sect) {
    chunk_t* c = chunk_find(cx, cz);
    if (c == NULL || lod < 0 || lod >= LOD_COUNT || sect < 0 || sect >= CH_SECT_N) return false;
    if ((c->lod_inflight & CH_MESH_BIT(lod, sect)) != 0) return true;  // already asked

    job_t const j = {
        .kind = JOB_MESH, .lod = (uint8_t)lod, .sect = (uint8_t)sect, .seq = c->edit_seq, .cx = cx, .cz = cz};
    // Stale because somebody changed a block, rather than because this
    // section has never been built? Then a player is looking at the old
    // one (chunk.h, lod_urgent).
    uint16_t const      bit   = CH_MESH_BIT(lod, sect);
    queue_where_t const where = (c->lod_urgent & bit) != 0 ? Q_FRONT : Q_NORMAL;
    if (!submit(&j, where)) return false;
    if (!s_sync) c->lod_inflight |= CH_MESH_BIT(lod, sect);
    return true;
}

bool chunk_worker_request_save(int32_t cx, int32_t cz) {
    chunk_t* c = chunk_find(cx, cz);
    if (c == NULL) return false;
    job_t const j = {.kind = JOB_SAVE, .seq = c->edit_seq, .cx = cx, .cz = cz};
    if (!s_sync) c->cstate = CS_SAVING;
    if (!submit(&j, Q_NORMAL)) {
        c->cstate = CS_READY;
        return false;
    }
    return true;
}

// --- Collecting -------------------------------------------------------

int chunk_worker_collect(int max_results) {
    if (s_sync) return 0;  // already applied, inline
#ifndef SM_HOST
    int      n = 0;
    result_t r;
    while (n < max_results && xQueueReceive(s_done, &r, 0) == pdTRUE) {
        apply(&r);
        s_in_flight--;
        n++;
    }
    s_applied_total += n;
    return n;
#else
    (void)max_results;
    return 0;
#endif
}

bool chunk_worker_idle(void) {
#ifndef SM_HOST
    if (!s_sync) return s_in_flight == 0;
#endif
    return true;
}

void chunk_worker_stats(int* queued, int* loaded_total, int* meshed_total) {
#ifndef SM_HOST
    if (queued != NULL) *queued = s_sync ? 0 : s_in_flight;
#else
    if (queued != NULL) *queued = 0;
#endif
    if (loaded_total != NULL) *loaded_total = s_loaded_total;
    if (meshed_total != NULL) *meshed_total = s_meshed_total;
}

void chunk_worker_flow(chunk_worker_flow_t* f) {
    if (f == NULL) return;
#ifndef SM_HOST
    f->in_flight = s_sync ? 0 : s_in_flight;
    f->capacity  = QUEUE_DEPTH;
#else
    f->in_flight = 0;
    f->capacity  = 0;
#endif
    f->asked   = s_asked_total;
    f->applied = s_applied_total;
    f->refused = s_refused_total;
    f->loaded  = s_loaded_total;
    f->meshed  = s_meshed_total;
    f->saved   = s_saved_total;
}

// --- Lifecycle --------------------------------------------------------

#ifndef SM_HOST
static void worker_main(void* arg) {
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        result_t r;
        run_job(&j, &r);
        // Block if main is behind: dropping a result would leak its mesh
        // and leave a chunk permanently in flight.
        if (xQueueSend(s_done, &r, portMAX_DELAY) != pdTRUE) {
            if (r.mesh.v != NULL || r.mesh.t != NULL) mesh_free(&r.mesh);
        }
    }
}
#endif

bool chunk_worker_start(uint32_t seed) {
    s_seed = seed;
    if (s_scratch == NULL) {
        s_scratch = sm_alloc(chunkmesh_scratch_bytes());
        if (s_scratch == NULL) return false;
    }

#ifndef SM_HOST
    if (s_running) return true;
    s_jobs = xQueueCreate(QUEUE_DEPTH, sizeof(job_t));
    s_done = xQueueCreate(QUEUE_DEPTH, sizeof(result_t));
    if (s_jobs == NULL || s_done == NULL) return false;
    if (xTaskCreatePinnedToCore(worker_main, "cmworker", WORKER_STACK, NULL, WORKER_PRIO, &s_task, WORKER_CORE) !=
        pdPASS) {
        return false;
    }
    ESP_LOGI(TAG, "chunk worker on core %d, priority %d, %u B scratch", WORKER_CORE, WORKER_PRIO,
             (unsigned)chunkmesh_scratch_bytes());
    s_sync = false;
#endif
    s_running = true;
    return true;
}

void chunk_worker_stop(void) {
#ifndef SM_HOST
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_jobs != NULL) {
        vQueueDelete(s_jobs);
        s_jobs = NULL;
    }
    if (s_done != NULL) {
        vQueueDelete(s_done);
        s_done = NULL;
    }
    s_in_flight = 0;
#endif
    sm_free(s_scratch);
    s_scratch = NULL;
    s_running = false;
    s_sync    = true;
}

void chunk_worker_set_world(uint32_t seed, int32_t farlands_x) {
    s_seed       = seed;
    s_farlands_x = farlands_x;
}

void chunk_worker_set_synchronous(bool on) {
#ifndef SM_HOST
    if (!on && !s_running) return;  // no task to be asynchronous with
    if (on) {
        // Drain first: a result arriving after the switch would be
        // applied twice, once inline and once on collect.
        while (s_in_flight > 0) chunk_worker_collect(64);
    }
#endif
    s_sync = on;
}

bool chunk_worker_synchronous(void) {
    return s_sync;
}
