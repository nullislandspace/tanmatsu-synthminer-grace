#pragma once
// =====================================================================
//  SynthMiner  --  packing a chunk into bytes
// ---------------------------------------------------------------------
//  A chunk is two 16 KiB planes of one byte per cell. Stored raw that
//  would be 32 KiB a chunk on a slow SD card, nearly all of it runs of
//  stone and air, so it is run-length encoded along the column -- which
//  is the direction the runs go, because that is how CH_IDX orders the
//  cells.
//
//  WHY NOT DEFLATE. `main/testkit/screenshot.c` documents the trap in
//  its own header: every compressing path wants ~130 KiB of state out
//  of a heap that lands in scarce internal SRAM (160 KiB free, largest
//  block 62 KiB -- F-22). RLE needs no state at all, is thirty lines,
//  runs on the chunk worker without an allocation, and gets a typical
//  chunk to 1-3 KiB. A better ratio is not worth a heap it cannot have.
//
//  EVERY ENCODE CAN FAIL SAFELY. A pathological chunk (alternating
//  blocks) makes RLE twice the size of the raw plane, so a plane is
//  stored raw whenever the encoding did not come out smaller. That is
//  what the `codec` byte in the payload header is for, and it means
//  there is no input this cannot store.
//
//  Pure: no engine, no RTOS, no allocation, no stdio.
// =====================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "world/chunk.h"
#include "world/blockent.h"
#include "game/mob.h"

// --- Sections ---------------------------------------------------------
//
// After the two planes the payload carries a list of sections:
//
//     u8 id, u32 length, <length bytes>
//
// and a reader SKIPS any id it does not know. That is what stops the
// format having to change again: block entities and creatures were
// added this way, and whatever comes next can be too, while older
// builds go on reading the chunks they understand.
//
// A section's contents are u16 count, then that many records, each a
// u16 length followed by tagged fields (common/tags.h). So a record of
// an unknown KIND is skippable too, and a record of a known kind that
// has grown new fields still loads -- defaults, then whatever is there.
// Every record names its own type as a string ("cow", "furnace"): there
// are few enough entities for that to cost nothing, and it means they
// need no palette and can never be misread after a renumbering.
#define SECTION_BLOCK_ENTITIES 1  // furnace contents, chest contents, sign text
#define SECTION_ENTITIES       2  // creatures and dropped items
// THE CHUNK'S SLOW CLOCK (chunk_t.stamp, world/crops.h): four bytes,
// little-endian, and no tags -- there is exactly one number in it and a
// record header would be longer than the record.
//
// A build without this section simply does not write one, and a build
// without this id steps over it: that is the whole reason the format
// has never needed a version bump (D-30). A chunk that arrives without
// one is stamped "now", which is the only honest answer for a world
// written before farming existed.
#define SECTION_CHUNK_CLOCK    3

// The most a chunk payload can ever need for its PLANES: both raw, plus
// the header and the two plane headers. Sections are on top of this, so
// a caller's buffer is this plus whatever it allows them.
#define CHUNK_PLANES_MAX (4u + 2u * (3u + (size_t)CH_CELLS))

// Room for the sections of one chunk, sized to the TRUE worst case rather
// than to a guess. That is only possible now that the region directory's
// length is a uint32 (F-144): the old uint16 capped a payload at 65535 and
// left 32757 for sections, which the worst case exceeds by 4.5x, so no cap
// could ever be a guarantee.

// The record maxima are measured, not estimated (F-142), and worldcheck
// re-measures them against the live tables so these cannot go stale.
#define BE_RECORD_WORST   1279u  // 44 B of fields + 27 filled slots at 46 B
#define MOB_RECORD_WORST  119u   // every optional field present
#define SECTION_FRAME     5u     // u8 id + u32 length

#define CHUNK_SECTIONS_MAX (SECTION_FRAME + 2u + (size_t)BE_MAX * BE_RECORD_WORST +   /* block entities */ \
                            SECTION_FRAME + 2u + (size_t)MOB_MAX * MOB_RECORD_WORST +  /* creatures */ \
                            SECTION_FRAME + 4u)                                        /* the chunk clock */

#define CHUNK_PAYLOAD_MAX (CHUNK_PLANES_MAX + CHUNK_SECTIONS_MAX)

// Because every record here comes from a pool that is itself bounded, the
// encoders cannot overflow: `chunk_section_drops()` is now a number that
// must read 0, and the loss F-142 made visible is no longer possible.
//
// What it costs is RAM, always, whatever the data -- four statics in
// region.c, three CHUNK_PAYLOAD_MAX and one CHUNK_SECTIONS_MAX, so
// 1,141,626 B, about 1.1 MiB of PSRAM (F-37) against F-22's 20,604 KiB
// free. The card does not pay it: the directory entry stores the length
// actually encoded, so a chunk with two pigs still writes ~174 B.

// RECORDS A SECTION ENCODER HAD TO LEAVE OUT because the buffer filled.
// Cumulative since boot, and zero in normal play. The point is that it
// is a number that MOVES: a section that did not fit used to write
// nothing at all and say nothing at all, which is the same fault as a
// full pool (F-125) -- the world stops quietly.
uint32_t chunk_section_drops(void);
void     chunk_note_drops(uint32_t n);

// --- The plane codec --------------------------------------------------

// Run-length encode `n` bytes. Returns the length written, or 0 if it
// did not fit in `out_cap` OR would not be smaller than `n` -- either
// way the caller stores the plane raw instead.
size_t chunk_rle_encode(uint8_t const* plane, size_t n, uint8_t* out, size_t out_cap);

// Decode exactly `n` bytes. False if the input is malformed or does not
// produce exactly `n` bytes -- a truncated or corrupt payload must fail,
// never half-fill a chunk.
bool chunk_rle_decode(uint8_t const* in, size_t in_n, uint8_t* plane, size_t n);

// --- The chunk payload ------------------------------------------------

// Pack a chunk's two planes, then append `sections` verbatim (already
// formed by the caller, who owns what lives in them). `sections` may be
// NULL. Returns the length written, or 0 if `out_cap` is too small.
size_t chunk_encode(chunk_t const* c, uint8_t const* sections, size_t sections_n, uint8_t* out, size_t out_cap);

// Hand each section of a decoded payload to `fn`. Sections the caller
// does not recognise it simply ignores; nothing here interprets them.
typedef void (*chunk_section_fn)(uint8_t id, uint8_t const* data, size_t len, void* user);

// Unpack into `c`'s existing planes and refresh its summaries. False on
// anything malformed, in which case `c` is left cleared rather than
// half-written: a corrupt region must read as "no chunk here", never as
// broken terrain.
//
// `remap` translates the block ids as they were SAVED into the ids this
// build uses, so a world stays readable when blocks are added, removed
// or reordered (worldstore.h builds it from the world's palette). NULL
// means the ids are already current.
bool chunk_decode(uint8_t const* in, size_t in_n, chunk_t* c, uint8_t const* remap);

// The same, plus a callback for each section found. `fn` may be NULL,
// which is exactly chunk_decode().
bool chunk_decode_ex(uint8_t const* in, size_t in_n, chunk_t* c, uint8_t const* remap, chunk_section_fn fn,
                     void* user);
