#pragma once
// =====================================================================
//  SynthMiner  --  the flight recorder
// ---------------------------------------------------------------------
//  ALWAYS ON, because the user's point is the whole of the design:
//
//    "So if i encounter a bug, we can just look at the trace afterwards
//     (without having to know that we need to debug beforehand)."
//
//  Everything diagnostic in this game so far has had to be switched on
//  BEFORE the thing it was meant to catch: the stream counters need a
//  measurement build, the console needs the badge in the right USB mode
//  and a person watching it, and the position overlay needs the player
//  to have pressed Backspace already. Every one of those is a promise to
//  reproduce the bug a second time, and the render bug of 2026-09-28 was
//  not reproduced on demand -- it was noticed while playing.
//
//  So this writes to the card while the game runs, and the question
//  afterwards is "what does the trace say", not "can you make it happen
//  again".
//
//  WHAT IS IN IT. Three kinds of line, each self-describing, so a file
//  from an older build still reads (tools/traceanalyse.py reads the
//  keys, not the column order):
//
//    H  the header: build, engine, seed, world, the graphics settings
//    T  once a second: frame rate, where the player is, how full the
//       geometry lists got, what was dropped, what the streamer did
//    P/B a block placed or broken, with its coordinates and chunk
//    M  a mesh arriving, with how long that section had been stale --
//       which is the difference between "drawn late" and "never drawn"
//
//  COST. One buffered write a second of a few hundred bytes, next to a
//  frame that takes 90 ms and a chunk streamer already writing regions
//  to the same card. The file rotates at TRACE_MAX_BYTES so it cannot
//  grow without bound; one previous file is kept.
//
//  Pure: stdio only, no engine and no IDF. The caller passes the clock
//  and the directory, so tools/worldcheck.c can exercise the formatting
//  and the rotation on the host.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>

// Stop recording once a session's file reaches this. NOT a rotation
// trigger: one playthrough gets one file (trace_open), so the only way
// to reach the cap is a single run long enough to fill it, and rotating
// then would throw away the beginning of the very run being recorded.
// At roughly 200 bytes a second, 256 KiB is about 20 minutes of play.
// Two files is the ceiling on the card: this one and the last.
#define TRACE_MAX_BYTES (256u * 1024u)

// The once-a-second snapshot. One struct rather than sixteen arguments
// so that adding a number later does not touch every call site, and so
// the writer can decide what is worth a line.
typedef struct {
    double   t;             // seconds since the world was opened
    float    fps;
    double   px, py, pz;    // where the player is
    int      yaw;           // degrees, 0 = north
    int      flat, flat_cap;      // geometry lists, as submitted
    int      tex, tex_cap;
    int      drop_flat, drop_tex; // ANYTHING here is a hole in the picture
    int      drawn, sections;     // chunks and sections actually drawn
    int      resident, missing;
    int      queue, queue_cap;    // the mesh queue
    unsigned mesh_kib;
    unsigned psram_kib, internal_kib;
    // What the card costs, in microseconds, averaged over this second
    // and at worst since boot. The filesystem, as opposed to the
    // arithmetic -- see chunk_worker.h.
    int      load_n, save_n;
    int      load_avg_us, save_avg_us;
    int      load_max_us, save_max_us;
    int      compact_n, compact_avg_us, compact_max_us;
} trace_tick_t;

// Start a file for a world. `dir` is where it lives (SM_DATA_DIR on the
// badge), the rest is the header. Rotates first if the old one is full.
// False if the file could not be opened, and everything below then does
// nothing -- a trace that cannot be written must never stop the game.
bool trace_open(char const* dir, char const* build, char const* engine, uint32_t seed, char const* world);

// Flush and close. Called when the world is left, by whatever route.
void trace_close(void);

// A free-form header line, for the settings and anything else worth
// stating once. Ignored when no trace is open.
void trace_note(char const* fmt, ...) __attribute__((format(printf, 1, 2)));

void trace_tick(trace_tick_t const* s);

// The clock, set once a frame by whoever owns one. Events do not carry
// a timestamp of their own: a block is placed deep inside the player
// update and a mesh arrives inside the streamer, and neither of those
// should have to know what time it is or where the clock comes from.
void trace_set_time(double t);

// A block placed ('P') or broken ('B'). `extra` is how many blocks the
// break actually took, which is the felling rule's answer and 1 for
// everything else. The chunk and section are derived here.
void trace_edit(char kind, int32_t x, int32_t y, int32_t z, char const* block, int extra);

// A rebuilt mesh arriving. Prints how long ago the matching edit was,
// when there was one -- the lag between changing a block and being able
// to see it, which is the number nothing else in the game reports.
void trace_mesh(int32_t cx, int32_t cz, int lod, int sect);

// For the host test: how many lines have been written since open.
int trace_lines(void);
