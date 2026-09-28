#pragma once
// =====================================================================
//  SynthMiner  --  region files
// ---------------------------------------------------------------------
//  Chunks are stored 8 x 8 to a file, `r.<rx>.<rz>.smr`. Per-chunk
//  files would lose twice on FAT and a slow SD card: a directory scan
//  on every open, and a whole cluster wasted per file (32 KiB clusters
//  against a chunk that RLEs to 1-3 KiB). A region amortises a whole
//  residency to a handful of opens and packs the payloads contiguously.
//  8 x 8 rather than Minecraft's 32 x 32 because 1024 chunks a region
//  is far more than this world touches at once, and a small region
//  makes compaction cheap.
//
//  DURABILITY WITHOUT TRUSTING fsync. The directory is stored TWICE,
//  each copy with its own CRC32 and a serial number, and a write
//  updates the STALE copy. So whatever happens, one copy is intact:
//  the reader takes the higher serial whose CRC validates. `fsync` is
//  exported here (F-06), but a FAT driver's behaviour across a power
//  cut is not something to bet a world on, and this costs 512 bytes.
//
//  Write order, and it matters:
//    1. append the payload at end of file, flush
//    2. write the stale directory copy with the new entry, its CRC and
//       serial = current + 1, flush
//    3. update the header's waste counter, flush, close
//  A loss between any two steps leaves the other copy valid and loses
//  at most that one chunk's newest save -- never the region.
//
//  REWRITES APPEND. A chunk that no longer fits its old slot goes on
//  the end and the directory points at the new place; the old bytes
//  become waste. When waste passes half the file the region is
//  compacted -- but only at an explicit save or unload, never mid-frame.
//
//  stdio only: no engine, no RTOS. The host checks build it as-is.
// =====================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "world/chunk.h"

#define REGION_DIM   8
#define REGION_CHUNKS (REGION_DIM * REGION_DIM)
// "SMR" + the MAJOR version digit. Like level.smw's, it moves only when
// the layout changes wholesale; a mismatched major is refused, not
// guessed at. REGION_VERSION below is the minor revision within it.
#define REGION_EXT     ".smr"
#define REGION_EXT_WAS ".cmr"  // CraftMiner's (D-93)
#define REGION_MAGIC   "SMR1"
// And what CraftMiner wrote (D-91): read, never written. A region that
// is compacted or rewritten comes back under the new name by itself.
#define REGION_MAGIC_WAS "CMR1"
#define REGION_VERSION 1

// --- Where region files live ------------------------------------------
//
// NOT one flat directory. Every chunk the player generates OR merely
// visits is written back, so the file count tracks explored area: a
// region is 128 x 128 blocks, so 2 km x 2 km of wandering is about 256
// files and 10 km x 10 km about 6100. FAT allows 65536 entries in a
// directory and `r.-127.15.smr` cannot be an 8.3 name, so it costs two
// or three of them -- but the real cost arrives long before the limit,
// because a FAT lookup is a LINEAR SCAN and there is one per chunk read
// and per chunk write.
//
// So regions are bucketed: region/<bx>.<bz>/r.<rx>.<rz>.smr, 16 x 16
// regions to a bucket. That is at most 256 files in a bucket directory
// and one entry per 2048 x 2048 blocks in the parent.
//
// NO THIRD LEVEL, and the arithmetic is why: filling the parent
// directory would take 20000 buckets, which is 84000 square kilometres
// of explored ground. At four blocks a second that is not reachable in
// a human lifetime of walking. Two levels is not a compromise here, it
// is the end of the problem.
#define REGION_BUCKET_SHIFT 4
#define REGION_BUCKET       (1 << REGION_BUCKET_SHIFT)

static inline int32_t region_bucket(int32_t r) {
    return r >> REGION_BUCKET_SHIFT;  // arithmetic shift: bucket -1 holds region -16..-1
}

// Move any region file still sitting flat in `dir` into its bucket.
// Called once when a world is opened; worlds written before bucketing
// existed migrate the first time they are played. Returns how many were
// moved, or -1 if the directory could not be read.
int region_migrate(char const* dir);

// World chunk coordinate -> region coordinate / index within it.
// Shift and mask, so negatives land correctly (chunk -1 is in region -1
// at local 7, not in region 0).
#define REGION_SHIFT 3
static inline int32_t region_of(int32_t c) {
    return c >> REGION_SHIFT;
}
static inline int region_local(int32_t c) {
    return (int)(c & (REGION_DIM - 1));
}

// Which extension region files are read and written under. ".smr",
// unless the world being opened still carries CraftMiner's names --
// then worldstore sets ".cmr" and that world is used UNDER ITS OWN
// NAMES, read AND written, until the rename reaches it (D-93).
//
// A MODULE-WIDE SETTING because exactly one world is open at a time,
// which is the same reason worldstore keeps one region directory. It
// must be set before any chunk of that world is touched -- open_paths()
// does it, beside the directory it belongs with.
void region_set_ext(char const* ext);

// Build "<dir>/r.<rx>.<rz><ext>". False if it would not fit.
bool region_path(char* out, size_t cap, char const* dir, int32_t rx, int32_t rz);

// Read one chunk. `c` must already carry cx/cz and have its planes.
// `remap` translates saved block ids to current ones (chunk_codec.h);
// NULL if they are already current.
// Returns:
//    1  read and decoded
//    0  the region or the chunk is simply not there (not an error)
//   -1  the file exists but is unreadable or corrupt
// A corrupt directory falls back to the other copy; a corrupt payload
// reads as 0, so a damaged region loses chunks rather than the world.
int region_read_chunk(char const* dir, chunk_t* c, uint8_t const* remap);

// Write one chunk, creating the region if needed. False on IO failure.
bool region_write_chunk(char const* dir, chunk_t const* c);

// Rewrite a region without its waste. Called at an explicit save or an
// unload when region_should_compact() says so -- never during a frame.
bool region_compact(char const* dir, int32_t rx, int32_t rz);
// Close every cached region handle, flushing first. Call when a world
// is closed: the handles are this module's, and nothing else knows they
// are open.
//
// THE CACHE EXISTS because a read or a write used to fopen() by path,
// and on FAT that is a linear scan of the directory -- several times a
// second, forever, growing with the number of regions the player has
// ever visited.
void region_close_all(void);

bool region_should_compact(char const* dir, int32_t rx, int32_t rz);
