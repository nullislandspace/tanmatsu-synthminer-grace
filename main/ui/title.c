// =====================================================================
//  SynthMiner  --  the title, written in blocks (see title.h)
// =====================================================================

#include "ui/title.h"

#include <math.h>
#include <string.h>

#include "world/chunk.h"
#include "world/chunk_render.h"
#include "world/worldstore.h"

// The seed the title's landscape is generated from.
//
// Fixed, so the picture is the same every boot: a title screen that
// looked different each time would read as a bug rather than as
// variety. CHOSEN, not picked -- the first seed tried put the camera
// over open ocean, which is a flat blue band and says nothing about
// the game. This one was found by scoring every seed under 4000 on the
// ground it gives across the camera's field of view: land, well clear
// of the water, with real relief and no cliff. It comes out at 25..38
// over a sea level of 24.
#define TITLE_SEED 0x00000B05u

#define GLYPH_H    7
// The letters' bottom row. Clear of the tallest TREE, not merely the
// tallest ground: terrain reaches y41 and a tree on it another six, so
// anything below y47 gets a canopy in front of it. The first version
// sat at 44 and the first word spent the whole title behind an oak.
// LOWERED from 56 on 2026-09-28, the user: the word was nicely centred
// but "you can only see the tops of a few blocks at the very bottom of
// the screen". Letters high up mean a camera that looks steeply up, and
// looking up puts the ground behind you. 50 is still clear of the
// tallest tree -- terrain reaches y41 and an oak on it another six --
// which is the constraint that set 56 in the first place.
#define TITLE_Y    50
#define TITLE_Z    24   // the plane they stand in
#define TITLE_DEEP 2    // blocks thick, so they read as solid from an angle

// The camera's height, which used to be TITLE_Y - 8 and so moved with
// the letters. It does not want to: it wants to be a few blocks above
// the GROUND, close enough that the meadow recedes to a horizon instead
// of dropping away under the lens. Terrain here tops out around y38 and
// a tree on it reaches 44, so 46 clears both with a little room.
#define TITLE_CAM_Y 46.0f

#define LOOP_SECS 24.0f  // one pass of the drift, there and back

typedef struct {
    char        c;
    char const* rows[GLYPH_H];
} glyph_t;

// The showreel's font, with three letters it never had. It carried the
// nine distinct letters of "CraftMiner"; "SynthMiner" needs nine of its
// own, and S, y and h had to be drawn in its style to match -- capitals
// five columns wide over all seven rows, x-height letters four wide
// over the bottom five, ascenders and descenders reaching out of that
// band by two. A '#' is a block.
//
// THE TWO WORDS COME OUT THE SAME WIDTH, 48 blocks, which is not luck
// so much as arithmetic that was worth checking: the camera below is
// framed on that number (see the path), and a wider word would have
// walked off the edges of the screen.
static glyph_t const FONT[] = {
    {'S', {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}},
    {'y', {"....", "....", "#..#", "#..#", ".###", "...#", ".##."}},
    {'n', {"....", "....", "###.", "#..#", "#..#", "#..#", "#..#"}},
    {'t', {".#..", ".#..", "###.", ".#..", ".#..", ".#.#", "..#."}},
    {'h', {"#...", "#...", "###.", "#..#", "#..#", "#..#", "#..#"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'i', {"#", ".", "#", "#", "#", "#", "#"}},
    {'e', {"....", "....", ".##.", "#..#", "####", "#...", ".###"}},
    {'r', {"....", "....", "#.##", "##..", "#...", "#...", "#..."}},
};

static char const TEXT[] = "SynthMiner";
#define SPLIT 5  // "Synth" in grass | "Miner" in cobblestone

// One block of the title. It used to carry when it appears and whether
// it had; both went when the word became part of the world rather than
// an animation (title_update).
typedef struct {
    int32_t x, y, z;
    uint8_t block;
} place_t;

#define MAX_BLOCKS 560
static place_t s_place[MAX_BLOCKS];
static int     s_n;
static float   s_mid_x, s_x0, s_x1;
static bool    s_fresh;   // the world had to be generated; the letters are not in it yet
static bool    s_active;

uint32_t title_seed(void) {
    return TITLE_SEED;
}

static glyph_t const* glyph(char c) {
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++) {
        if (FONT[i].c == c) return &FONT[i];
    }
    return NULL;
}

// The letters' blocks in the order they appear: column by column from
// the left, bottom to top, front layer then back.
static void build_title(void) {
    int width = -1;
    for (int k = 0; TEXT[k]; k++) width += (int)strlen(glyph(TEXT[k])->rows[0]) + 1;

    int const x0 = -width / 2;  // centred on the world origin
    s_x0         = (float)x0;
    s_x1         = (float)(x0 + width);

    s_n     = 0;
    int col = x0;
    for (int k = 0; TEXT[k]; k++) {
        glyph_t const* g = glyph(TEXT[k]);
        int const      w = (int)strlen(g->rows[0]);
        uint8_t const  b = (k < SPLIT) ? BLK_GRASS : BLK_COBBLE;
        for (int cx = 0; cx < w; cx++, col++) {
            for (int row = GLYPH_H - 1; row >= 0; row--) {
                if (g->rows[row][cx] != '#') continue;
                for (int dz = 0; dz < TITLE_DEEP && s_n < MAX_BLOCKS; dz++) {
                    s_place[s_n] = (place_t){
                        .x     = col,
                        .y     = TITLE_Y + (GLYPH_H - 1 - row),
                        .z     = TITLE_Z + dz,
                        .block = b,
                    };
                    s_n++;
                }
            }
        }
        col++;  // the gap between letters
    }

    // The middle of what was actually written, not of the width the
    // glyphs were budgeted. The two differ by a few blocks -- the last
    // letter has no trailing gap -- and aiming the camera at the wrong
    // one puts the word off-centre on screen.
    int lo = 1 << 20, hi = -(1 << 20);
    for (int i = 0; i < s_n; i++) {
        if (s_place[i].x < lo) lo = s_place[i].x;
        if (s_place[i].x > hi) hi = s_place[i].x;
    }
    s_x0    = (float)lo;
    s_x1    = (float)hi + 1.0f;
    s_mid_x = 0.5f * (s_x0 + s_x1);
}

// The camera's path, `s` running 0..1 and BACK again.
//
// NO JUMP AT THE SEAM. The drift used to run 0..1 and restart, so every
// sixteen seconds the camera teleported from one end of its travel to
// the other. The user asked for "a sinusiod motion or something like
// that (no position jumps)", and a cosine gives more than continuity:
// its derivative is zero at both ends, so the camera eases to a stop
// and turns round rather than reversing at full speed.
//
// FAR ENOUGH BACK TO FIT THE WORD. The letters are 48 blocks across and
// the projection sees 400/450 of the distance to either side, so at 34
// blocks it could show 60 -- until the drift moves the camera off
// centre and needs 68. 44 blocks back plus the sway fits it with room.
static void path(float s, double* x, float* y, double* z) {
    *x = (double)s_mid_x - 6.0 + 12.0 * (double)s;
    // A gentle rise and fall, a quarter cycle out of step with the
    // sideways sway, so the motion reads as drifting rather than as a
    // track.
    *y = TITLE_CAM_Y + 1.5f * sinf(3.14159265f * s);
    *z = (double)TITLE_Z - 46.0 + 4.0 * (double)s;
}

// The loop's position, 0..1..0, smooth at both ends.
static float loop_s(double t) {
    float const ft = (float)fmod(t, (double)LOOP_SECS);
    return 0.5f - 0.5f * cosf(2.0f * 3.14159265f * ft / LOOP_SECS);
}

bool title_begin(world_meta_t* out_meta, player_state_t* out_player) {
    world_meta_t   meta;
    player_state_t player;
    // THE WORLD IS KEPT, not generated every time (worldstore.h,
    // F-115). `fresh` means there was nothing usable on the card, so
    // the caller has to build the letters in and save it.
    bool made = true;
    if (!worldstore_open_title(TITLE_SEED, SM_TITLE_GEN, &meta, &player, &made)) return false;
    build_title();
    s_fresh  = made;
    s_active = true;
    // THE CALLER NEEDS THIS META. It used to stay in a static in here,
    // and main.c then wrote level.smw for the title world out of the
    // meta belonging to whatever world it had open last -- which at
    // boot is a zeroed struct with no slug and no seed.
    if (out_meta != NULL) *out_meta = meta;
    if (out_player != NULL) *out_player = player;
    return true;
}

bool title_is_fresh(void) {
    return s_fresh;
}

int title_write_letters(void) {
    int written = 0;
    for (int i = 0; i < s_n; i++) {
        place_t const* p = &s_place[i];
        // No-ops on a chunk that is not resident, which is why this is
        // called after the loading gate rather than during it.
        world_set(p->x, p->y, p->z, p->block, ST_PLACED);
        if (world_block(p->x, p->y, p->z) == p->block) written++;
    }
    return written;
}

void title_end(void) {
    s_active = false;
    worldstore_close();
}

// NOTHING TO DO EVERY FRAME ANY MORE. The letters used to be written
// and taken away again on a timer, one block every 13 ms, so the word
// built itself each time the loop came round. The user, 2026-09-28:
// "we don't really need to generate and remove the SynthMiner logo
// every few seconds. Due to the renderer, it stutters when showing up
// (not a smooth animation)."
//
// And it could not have been smooth: every block written marks its
// chunk section stale, so the word was asking for a remesh several
// times a second -- 81 chunks' worth of streaming plus a rebuild per
// letter. They are part of the world now, written once when it is
// generated and saved with it.
void title_update(double t) {
    (void)t;
}

title_view_t title_camera(double t) {
    float const s = loop_s(t);

    title_view_t v;
    path(s, &v.wx, &v.wy, &v.wz);

    // Look at the middle of the letters, drifting along them so the eye
    // is led across the word rather than staring at its centre.
    double const tx = (double)s_mid_x + 4.0 * (double)s;
    // AIMED AT THE MIDDLE OF THE WORD, which with the camera now near
    // the ground means looking very slightly UP -- a couple of degrees
    // rather than the eleven it used to be. That is what puts the
    // meadow in the lower half of the frame instead of past the bottom
    // edge of it.
    double const ty = (double)TITLE_Y + 0.5 * (double)GLYPH_H;
    double const tz = (double)TITLE_Z;

    double const dx = tx - v.wx, dy = ty - (double)v.wy, dz = tz - v.wz;
    // The engine's convention: forward is (sin yaw, cos yaw) in x and z,
    // and POSITIVE PITCH LOOKS DOWN (raycast.h).
    v.yaw   = (float)atan2(dx, dz);
    v.pitch = (float)atan2(-dy, sqrt(dx * dx + dz * dz));
    return v;
}

void title_stream_at(double t, double* wx, double* wz) {
    title_view_t const v = title_camera(t);
    if (wx != NULL) *wx = v.wx;
    if (wz != NULL) *wz = v.wz;
}

sm_view_t title_view(void) {
    // The title's own view, not one of the player's presets. The
    // letters stand 52 blocks off, and the near preset would have them
    // flat-shaded and six tenths of the way into the fog -- grey, where
    // "Craft" is supposed to be green. This keeps textures out to 60
    // and pushes the fog past the letters entirely, so the word reads
    // as the blocks it is made of.
    //
    // evict_radius 7 is the ring's limit (CH_EVICT_MAX) and the reason
    // draw_dist stops at 96: six chunks of loading is 96 blocks, and
    // drawing further than you load is drawing a hole.
    // DRAW DISTANCE IS A TRIANGLE BUDGET, not a preference. The engine's
    // lists hold 4096 flat and 2048 textured and a full list drops the
    // rest SILENTLY, in submission order -- so an over-generous view
    // does not degrade, it deletes an arbitrary corner of the world.
    // The first version of this asked for 96 blocks over 169 chunks and
    // lost "Craft" (F-48).
    //
    // 56 blocks of terrain behind letters that stand 44 away is enough
    // scenery, and it fits with room. `scene_drop_stats()` says so
    // rather than the picture having to.
    return (sm_view_t){
        .fancy_dist   = 20.0f,
        .tex_dist     = 52.0f,
        .coarse_dist  = 44.0f,
        .draw_dist    = 56.0f,
        .fog0         = 44.0f,
        .fog1         = 64.0f,
        .fog_argb     = SM_SKY_ARGB,
        .load_radius  = 4,
        .evict_radius = 5,
    };
}
