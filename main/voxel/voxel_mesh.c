// =====================================================================
//  SynthMiner  --  meshing blocks (see voxel_mesh.h)
//  Lifted from tanmatsu-showreel-grace,
//  main/craftminer/voxel/voxel_mesh.c. Changes here are SynthMiner's;
//  the showreel stays the origin to diff against.
// =====================================================================

#include "voxel/voxel_mesh.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "common/psram.h"
#include "world/blocks.h"

int voxel_face_mat(uint8_t block, vox_face_t face) {
    block_def_t const* d = block_def(block);
    return d->kind == K_AIR ? -1 : (int)d->mat[face];
}

// Whether the face of cube `b` towards neighbour `n` shows.
static bool face_shows(uint8_t b, uint8_t n, vox_mesh_mode_t mode) {
    switch (block_kind(n)) {
        case K_CUBE:
            return false;
        case K_SEE:
            // A see-through block against a different block always shows;
            // against its own id only if it is marked BF_SEE_SELF (leaves
            // are a canopy you see into, glass hides glass).
            return mode == VOX_MESH_FANCY && (n != b || (block_def(b)->flags & BF_SEE_SELF) != 0);
        case K_LIQUID:
            // A liquid never hides anything, in either mode. Without
            // this the bed of a lake is never meshed and the surface is
            // a lid over nothing (D-86).
            return true;
        default:
            return true;  // air, a plant, a torch
    }
}

// The face's material in this kind of mesh: leaves are opaque unless
// fancy.
static int mode_mat(uint8_t b, vox_face_t face, vox_mesh_mode_t mode) {
    int const m = voxel_face_mat(b, face);
    if (mode == VOX_MESH_FANCY) return m;
    if (m == VM_LEAVES) return VM_LEAVES_FAST;
    if (m == VM_BIRCH_LEAVES) return VM_BIRCH_LEAVES_FAST;
    return m;
}

// The six face directions: the axis the face looks along (0 x, 1 y,
// 2 z) and which way.
typedef struct {
    int axis, sign;
} dir_t;

static dir_t const DIRS[6] = {{0, +1}, {0, -1}, {1, +1}, {1, -1}, {2, +1}, {2, -1}};

// One merged rectangle of faces, emitted as a quad whose corners run
// counter-clockwise seen from outside, texture u along its first edge
// and v along its second (v downwards on side faces, so the grass strip
// is at the top). The slice is the block layer the faces belong to; the
// rectangle spans [p0, p0+wp) x [q0, q0+hq) of the plane's two axes:
// (z, y) for x faces, (x, y) for z faces, (x, z) for y faces.
static void emit_f(mesh_t* m, dir_t dir, float plane, float P0, float P1, float Q0, float Q1, float W, float H,
                   uint8_t mat) {
    // Which way this face looks, so the renderer can cull it with one
    // compare instead of a cross product (mesh.h, MESH_DIR_*).
    mesh_set_dir(m, (uint8_t)(dir.axis * 2 + (dir.sign > 0 ? 0 : 1)));
    vec3_t c[4];
    float  uv[4][2] = {{0.0f, 0.0f}, {W, 0.0f}, {W, H}, {0.0f, H}};
    switch (dir.axis * 2 + (dir.sign < 0)) {
        case 0: {  // +x: u along +z, v down
            float const x = plane;
            c[0] = v3(x, Q1, P0), c[1] = v3(x, Q1, P1), c[2] = v3(x, Q0, P1), c[3] = v3(x, Q0, P0);
        } break;
        case 1: {  // -x: u along -z, v down
            float const x = plane;
            c[0] = v3(x, Q1, P1), c[1] = v3(x, Q1, P0), c[2] = v3(x, Q0, P0), c[3] = v3(x, Q0, P1);
        } break;
        case 4: {  // +z: u along -x, v down
            float const z = plane;
            c[0] = v3(P1, Q1, z), c[1] = v3(P0, Q1, z), c[2] = v3(P0, Q0, z), c[3] = v3(P1, Q0, z);
        } break;
        case 5: {  // -z: u along +x, v down
            float const z = plane;
            c[0] = v3(P0, Q1, z), c[1] = v3(P1, Q1, z), c[2] = v3(P1, Q0, z), c[3] = v3(P0, Q0, z);
        } break;
        case 2: {  // +y: u along +z, v along +x
            float const y = plane;
            c[0] = v3(P0, y, Q0), c[1] = v3(P0, y, Q1), c[2] = v3(P1, y, Q1), c[3] = v3(P1, y, Q0);
            float const t[4][2] = {{0.0f, 0.0f}, {H, 0.0f}, {H, W}, {0.0f, W}};
            memcpy(uv, t, sizeof(uv));
        } break;
        default: {  // -y: u along +x, v along +z
            float const y = plane;
            c[0] = v3(P0, y, Q0), c[1] = v3(P1, y, Q0), c[2] = v3(P1, y, Q1), c[3] = v3(P0, y, Q1);
        } break;
    }
    int const a = mesh_vert(m, c[0]), b = mesh_vert(m, c[1]), cc = mesh_vert(m, c[2]), d = mesh_vert(m, c[3]);
    if (a < 0 || b < 0 || cc < 0 || d < 0) return;
    mesh_quad(m, a, b, cc, d, mat, uv);
}

// One merged rectangle of whole cube faces in block layer `slice`.
static void emit(mesh_t* m, dir_t dir, int slice, int p0, int q0, int wp, int hq, float step, uint8_t mat) {
    emit_f(m, dir, ((float)slice + (dir.sign > 0 ? 1.0f : 0.0f)) * step, (float)p0 * step, (float)(p0 + wp) * step,
           (float)q0 * step, (float)(q0 + hq) * step, (float)wp * step, (float)hq * step, mat);
}

// A box lo..hi with its texture once across every face (the torch).
static void emit_box(mesh_t* m, vec3_t lo, vec3_t hi, uint8_t mat) {
    mesh_set_dir(m, MESH_DIR_NONE);  // not a greedy face; cull it the general way
    emit_f(m, DIRS[0], hi.x, lo.z, hi.z, lo.y, hi.y, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[1], lo.x, lo.z, hi.z, lo.y, hi.y, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[2], hi.y, lo.x, hi.x, lo.z, hi.z, 0.2f, 1.0f, mat);  // the glowing tip (v 0..0.2)
    emit_f(m, DIRS[3], lo.y, lo.x, hi.x, lo.z, hi.z, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[4], hi.z, lo.x, hi.x, lo.y, hi.y, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[5], lo.z, lo.x, hi.x, lo.y, hi.y, 1.0f, 1.0f, mat);
}

// A box lo..hi with `front` on its +x face and `mat` on the others, each
// face's texture once across it (the sign's post and board).
static void emit_box_mats(mesh_t* m, vec3_t lo, vec3_t hi, uint8_t mat, uint8_t front) {
    mesh_set_dir(m, MESH_DIR_NONE);
    emit_f(m, DIRS[0], hi.x, lo.z, hi.z, lo.y, hi.y, 1.0f, 1.0f, front);
    emit_f(m, DIRS[1], lo.x, lo.z, hi.z, lo.y, hi.y, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[2], hi.y, lo.x, hi.x, lo.z, hi.z, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[3], lo.y, lo.x, hi.x, lo.z, hi.z, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[4], hi.z, lo.x, hi.x, lo.y, hi.y, 1.0f, 1.0f, mat);
    emit_f(m, DIRS[5], lo.z, lo.x, hi.x, lo.y, hi.y, 1.0f, 1.0f, mat);
}

// --- The fence, the gate and the barrel -------------------------------
//
// All three are BOXES rather than cubes, drawn one cell at a time like
// the torch and the sign: the greedy pass only owns K_CUBE, K_SEE and
// the liquids, so nothing here has to be taken out of it.

// Does a fence at this cell join to `n`?
//
// To another fence, to a gate, and to any full cube -- so a run of
// fence meets a wall flush instead of stopping a rail short of it. Not
// to a plant, a torch or air, which would look like a rail into
// nothing.
static bool fence_joins(uint8_t n) {
    block_kind_t const k = block_kind(n);
    return k == K_FENCE || k == K_GATE || k == K_CUBE;
}

#define FENCE_POST_R  0.125f  // half the post's thickness
#define FENCE_RAIL_R  0.0625f
#define FENCE_RAIL_LO 0.40f   // the two rails, up from the cell's floor
#define FENCE_RAIL_HI 0.95f
#define FENCE_RAIL_H  0.18f   // how deep each rail is

// A rail from the post out to the cell's edge, along one axis.
static void emit_rail(mesh_t* m, float cx, float cz, float y0, int dx, int dz, uint8_t mat) {
    float const x0 = dx < 0 ? cx - 0.5f : cx - FENCE_POST_R;
    float const x1 = dx > 0 ? cx + 0.5f : cx + FENCE_POST_R;
    float const z0 = dz < 0 ? cz - 0.5f : cz - FENCE_POST_R;
    float const z1 = dz > 0 ? cz + 0.5f : cz + FENCE_POST_R;
    // Across the run it is thin; along it, it reaches the cell edge.
    float const ax0 = dx != 0 ? x0 : cx - FENCE_RAIL_R, ax1 = dx != 0 ? x1 : cx + FENCE_RAIL_R;
    float const az0 = dz != 0 ? z0 : cz - FENCE_RAIL_R, az1 = dz != 0 ? z1 : cz + FENCE_RAIL_R;
    emit_box_mats(m, v3(ax0, y0, az0), v3(ax1, y0 + FENCE_RAIL_H, az1), mat, mat);
}

// A post and whatever rails it carries. `nx/px/nz/pz` say which of the
// four neighbours it joins to.
static void emit_fence(mesh_t* m, int X, int Y, int Z, uint8_t mat, bool nx, bool px, bool nz, bool pz) {
    float const cx = (float)X + 0.5f, cz = (float)Z + 0.5f, y = (float)Y;
    // THE POST IS A BLOCK AND A HALF, which is the number the collider
    // uses (blocks.h, BLOCK_FENCE_TOP) -- a fence you can see over the
    // top of but cannot get over.
    emit_box_mats(m, v3(cx - FENCE_POST_R, y, cz - FENCE_POST_R),
                  v3(cx + FENCE_POST_R, y + BLOCK_FENCE_TOP, cz + FENCE_POST_R), mat, mat);
    float const rails[2] = {y + FENCE_RAIL_LO, y + FENCE_RAIL_HI};
    for (int i = 0; i < 2; i++) {
        if (nx) emit_rail(m, cx, cz, rails[i], -1, 0, mat);
        if (px) emit_rail(m, cx, cz, rails[i], +1, 0, mat);
        if (nz) emit_rail(m, cx, cz, rails[i], 0, -1, mat);
        if (pz) emit_rail(m, cx, cz, rails[i], 0, +1, mat);
    }
}

// The gate. Two posts at the ends of its axis, and between them either
// a panel across the gap (closed) or the same panel folded back against
// the posts (open). `axis` is GATE_AXIS_X or GATE_AXIS_Z: the axis the
// gate LIES along, so you walk through it across that axis.
static void emit_gate(mesh_t* m, int X, int Y, int Z, uint8_t mat, unsigned axis, bool open) {
    float const cx = (float)X + 0.5f, cz = (float)Z + 0.5f, y = (float)Y;
    // AS TALL AS THE FENCE IT STANDS IN, because that is what the
    // collider says it is (blocks.h, block_collide_top). A gate drawn
    // shorter than it blocks is a gate people try to jump.
    float const r = FENCE_POST_R, h = BLOCK_FENCE_TOP;
    // Along the gate's axis: the two posts sit at its ends.
    float const ax = axis == GATE_AXIS_X ? 1.0f : 0.0f, az = axis == GATE_AXIS_X ? 0.0f : 1.0f;
    for (int s = -1; s <= 1; s += 2) {
        float const px = cx + ax * (float)s * 0.375f, pz = cz + az * (float)s * 0.375f;
        emit_box_mats(m, v3(px - r, y, pz - r), v3(px + r, y + h, pz + r), mat, mat);
    }
    float const rails[2] = {y + FENCE_RAIL_LO, y + FENCE_RAIL_HI};
    for (int i = 0; i < 2; i++) {
        if (!open) {
            // Closed: the panel spans the gap between the posts.
            float const x0 = axis == GATE_AXIS_X ? cx - 0.375f : cx - FENCE_RAIL_R;
            float const x1 = axis == GATE_AXIS_X ? cx + 0.375f : cx + FENCE_RAIL_R;
            float const z0 = axis == GATE_AXIS_X ? cz - FENCE_RAIL_R : cz - 0.375f;
            float const z1 = axis == GATE_AXIS_X ? cz + FENCE_RAIL_R : cz + 0.375f;
            emit_box_mats(m, v3(x0, rails[i], z0), v3(x1, rails[i] + FENCE_RAIL_H, z1), mat, mat);
            continue;
        }
        // Open: the two halves are swung back against their own posts,
        // across the way through. Nothing rotates -- a greedy mesher
        // emits axis-aligned boxes, and a panel lying the other way is
        // what "swung aside" looks like without one (the torch on a
        // wall is the same trick).
        for (int s = -1; s <= 1; s += 2) {
            float const px = cx + ax * (float)s * 0.375f, pz = cz + az * (float)s * 0.375f;
            float const x0 = axis == GATE_AXIS_X ? px - FENCE_RAIL_R : px;
            float const x1 = axis == GATE_AXIS_X ? px + FENCE_RAIL_R : px + 0.4f;
            float const z0 = axis == GATE_AXIS_X ? pz : pz - FENCE_RAIL_R;
            float const z1 = axis == GATE_AXIS_X ? pz + 0.4f : pz + FENCE_RAIL_R;
            emit_box_mats(m, v3(x0, rails[i], z0), v3(x1, rails[i] + FENCE_RAIL_H, z1), mat, mat);
        }
    }
}

// THE OPEN BARREL, and its three appearances (blocks.h, BARREL_*): four
// walls, a floor, a rim, and an inner surface whose material is what is
// standing in it. The top face is NOT drawn -- that is what makes it
// open -- so the rim and the inner walls are what stop the eye looking
// straight through the cell from above.
#define BARREL_WALL 0.15f
#define BARREL_FILL 0.82f  // how high the milk, or the cheese, stands

static void emit_barrel(mesh_t* m, int X, int Y, int Z, uint8_t side, uint8_t top, unsigned contents) {
    float const x0 = (float)X, x1 = (float)X + 1.0f;
    float const z0 = (float)Z, z1 = (float)Z + 1.0f;
    float const y0 = (float)Y, y1 = (float)Y + 1.0f;
    mesh_set_dir(m, MESH_DIR_NONE);
    // The four outer walls and the underside.
    emit_f(m, DIRS[0], x1, z0, z1, y0, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[1], x0, z0, z1, y0, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[4], z1, x0, x1, y0, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[5], z0, x0, x1, y0, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[3], y0, x0, x1, z0, z1, 1.0f, 1.0f, top);
    // The rim: four strips of the top face, leaving the middle open.
    float const ix0 = x0 + BARREL_WALL, ix1 = x1 - BARREL_WALL;
    float const iz0 = z0 + BARREL_WALL, iz1 = z1 - BARREL_WALL;
    emit_f(m, DIRS[2], y1, x0, ix0, z0, z1, BARREL_WALL, 1.0f, top);
    emit_f(m, DIRS[2], y1, ix1, x1, z0, z1, BARREL_WALL, 1.0f, top);
    emit_f(m, DIRS[2], y1, ix0, ix1, z0, iz0, 1.0f - 2 * BARREL_WALL, BARREL_WALL, top);
    emit_f(m, DIRS[2], y1, ix0, ix1, iz1, z1, 1.0f - 2 * BARREL_WALL, BARREL_WALL, top);
    // The inside of the four walls, seen when you look down into it.
    float const fy = contents == BARREL_EMPTY ? y0 + 0.12f : y0 + BARREL_FILL;
    emit_f(m, DIRS[1], ix1, iz0, iz1, fy, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[0], ix0, iz0, iz1, fy, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[5], iz1, ix0, ix1, fy, y1, 1.0f, 1.0f, side);
    emit_f(m, DIRS[4], iz0, ix0, ix1, fy, y1, 1.0f, 1.0f, side);
    // And what is standing in it -- or the boards at the bottom.
    uint8_t const fill = contents == BARREL_MILK ? VM_MILK : contents == BARREL_CHEESE ? VM_CHEESE : top;
    emit_f(m, DIRS[2], fy, ix0, ix1, iz0, iz1, 1.0f, 1.0f, fill);
}

// HALF A BED: a frame with a mattress on it, nine sixteenths of a block
// tall so it can be walked onto. `head` puts the pillow end's texture on
// top; `face` is the direction from the foot to the head, which decides
// which of the four sides is the open one where the two halves meet.
#define BED_H 0.5625f

static void emit_bed(mesh_t* m, int X, int Y, int Z, uint8_t top, uint8_t side, unsigned face, bool head) {
    float const x0 = (float)X, x1 = (float)X + 1.0f;
    float const z0 = (float)Z, z1 = (float)Z + 1.0f;
    float const y0 = (float)Y, y1 = (float)Y + BED_H;
    mesh_set_dir(m, MESH_DIR_NONE);
    emit_f(m, DIRS[2], y1, x0, x1, z0, z1, 1.0f, 1.0f, top);
    emit_f(m, DIRS[3], y0, x0, x1, z0, z1, 1.0f, 1.0f, side);
    // The four sides, minus the one the other half is against: two beds
    // meeting would otherwise draw a wall down the middle of one bed.
    int const dx = face == FACE_PX ? 1 : face == FACE_NX ? -1 : 0;
    int const dz = face == FACE_PZ ? 1 : face == FACE_NZ ? -1 : 0;
    int const jx = head ? -dx : dx, jz = head ? -dz : dz;  // towards the other half
    if (jx != 1) emit_f(m, DIRS[0], x1, z0, z1, y0, y1, 1.0f, BED_H, side);
    if (jx != -1) emit_f(m, DIRS[1], x0, z0, z1, y0, y1, 1.0f, BED_H, side);
    if (jz != 1) emit_f(m, DIRS[4], z1, x0, x1, y0, y1, 1.0f, BED_H, side);
    if (jz != -1) emit_f(m, DIRS[5], z0, x0, x1, y0, y1, 1.0f, BED_H, side);
}

int voxel_sign_text(int32_t x, int32_t y, int32_t z) {
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (int)(h % VOX_SIGN_TEXTS);
}

// A standing sign, facing east (+x): a post up to half the cell, and a
// board across the whole cell above it, its front the text (one texture
// across the board: 64 x 32 texels on 1 x 0.5 blocks).
static void emit_sign(mesh_t* m, int X, int Y, int Z) {
    float const cx = (float)X + 0.5f, cz = (float)Z + 0.5f, t = 1.0f / 16.0f;
    uint8_t const front = (uint8_t)(VM_SIGN_0 + voxel_sign_text(X, Y, Z));
    emit_box_mats(m, v3(cx - t, (float)Y, cz - t), v3(cx + t, (float)Y + 0.5f, cz + t), VM_PLANKS, VM_PLANKS);
    emit_box_mats(m, v3(cx - t, (float)Y + 0.5f, (float)Z), v3(cx + t, (float)Y + 1.0f, (float)Z + 1.0f), VM_PLANKS, front);
}

// A plant: two vertical quads along the cell's diagonals, each twice
// (one per side), the texture upright and unmirrored from both.
// --- Fluid surfaces ---------------------------------------------------
//
// A FLOWING cell is not a cube and cannot go through the greedy pass:
// its top face is part-way up the cell and its four corners are at
// different heights, so it merges with nothing. It is emitted one cell
// at a time, beside the plants and the torches, which is where this
// file has always put the blocks that are not boxes.
//
// This is WHY the levels are worth simulating. Until the surface
// actually drops, a film one texel deep and a full block of water draw
// identically and the player has no way to read depth, direction or
// where the spring is -- which was the user's objection, and it was
// right.
//
// Three things get drawn and the last one is not optional:
//
//   THE TOP, as a quad whose four corners are averaged from the cells
//   meeting at each corner. That average is the whole effect: the sheet
//   tilts down the way it is running, so the slope IS the flow arrow.
//
//   THE UNDERSIDE, a second copy wound the other way, because an
//   axis-aligned face is visible only from the side its normal points
//   at -- the same reason D-86 needed one for the flat surface, and the
//   same symptom if it is missing (the water vanishes as the eye goes
//   under).
//
//   THE SIDES, against air. D-86 said a liquid draws no sides, and for
//   a lake that is right: its rim is the only place a side could show
//   and the rule bought a surface you can see through. A FALLING COLUMN
//   IS NOTHING BUT SIDES. With the rule applied to it a waterfall emits
//   no geometry whatsoever -- no top (the cell above is water), no
//   bottom, no sides -- and is invisible from the source to the splash.
//   So flows and falls draw their sides and SOURCES STILL DO NOT, which
//   leaves every lake and ocean exactly as D-86 left them.

static size_t grid_idx(vox_grid_t const* g, int x, int y, int z) {
    return ((size_t)(z + 1) * (size_t)(g->w + 2) + (size_t)(x + 1)) * (size_t)(g->h + 2) + (size_t)(y + 1);
}

static uint8_t grid_cell(vox_grid_t const* g, int x, int y, int z) {
    return g->cells[grid_idx(g, x, y, z)];
}

static uint8_t grid_data(vox_grid_t const* g, int x, int y, int z) {
    return g->data != NULL ? g->data[grid_idx(g, x, y, z)] : (uint8_t)0;
}

// Is this cell a liquid the greedy pass has given up on -- a flow or a
// fall, as opposed to a source or a still block of ocean?
static bool fluid_is_shaped(vox_grid_t const* g, int x, int y, int z) {
    if (block_kind(grid_cell(g, x, y, z)) != K_LIQUID) return false;
    uint8_t const dat = grid_data(g, x, y, z);
    return (dat & VOX_FLUID_LEVEL_MASK) != 0u || (dat & VOX_FLUID_FALLING) != 0u;
}

// How high the liquid stands in a cell, in block units, 0 for a cell
// holding none.
static float fluid_surface(vox_grid_t const* g, int x, int y, int z) {
    if (block_kind(grid_cell(g, x, y, z)) != K_LIQUID) return 0.0f;
    return voxel_fluid_height(grid_data(g, x, y, z));
}

// The height at one corner of a cell's surface: the average over the
// LIQUID cells among the four that meet there, and only those.
//
// The slope comes out of the averaging itself. Along a sheet running
// 1.000, 0.875, 0.750 the corners land halfway between each pair, so
// every cell's surface tilts the way the water is thinning -- and the
// tilt IS the flow arrow. Nothing has to work out a direction.
//
// DRY CELLS ARE SKIPPED, NOT COUNTED AS ZERO, and that is the whole of
// the rule. The first draft counted air as a height of nothing, on the
// reasoning that the water ends there and ought to slope down into it.
// It does not survive the simplest case: a single puddle with air on
// all sides has three dry cells at every corner, so a level-4 cell --
// half a block of water -- would have drawn as a film an eighth deep.
// A lone cell should read as exactly as deep as it is, and with dry
// cells skipped it does: it is the only liquid at each of its corners,
// so all four come out at its own height and the surface is flat.
// IS THIS CELL WATER, for the purposes of a surface? A liquid is, and so
// is a waterlogged block -- rice, which is a plant standing in a full
// cell of it (blocks.h, BF2_WATERLOGGED). Both of the functions below
// have to agree about that, or a paddy gets a sloping edge where the
// plants are.
static bool cell_is_water(vox_grid_t const* g, int x, int y, int z) {
    uint8_t const b = grid_cell(g, x, y, z);
    return block_kind(b) == K_LIQUID || block_waterlogged(b);
}

// How deep it stands. A waterlogged cell is ALWAYS FULL: its state byte
// holds a growth stage, not a fluid level, and reading one as the other
// would drain the paddy as the rice ripened.
static float cell_water_height(vox_grid_t const* g, int x, int y, int z) {
    if (block_waterlogged(grid_cell(g, x, y, z))) return 1.0f;
    return voxel_fluid_height(grid_data(g, x, y, z));
}

static float fluid_corner(vox_grid_t const* g, int x, int y, int z, int cx, int cz) {
    float sum = 0.0f;
    int   n   = 0;
    for (int dz = -1; dz <= 0; dz++) {
        for (int dx = -1; dx <= 0; dx++) {
            int const px = x + cx + dx, pz = z + cz + dz;
            // Liquid directly above: the column carries on up through
            // this corner, so there is no dip in it at all.
            if (block_kind(grid_cell(g, px, y + 1, pz)) == K_LIQUID) return 1.0f;
            if (!cell_is_water(g, px, y, pz)) continue;
            sum += cell_water_height(g, px, y, pz);
            n++;
        }
    }
    // The cell itself is always one of the four, so n is never 0.
    return n > 0 ? sum / (float)n : cell_water_height(g, x, y, z);
}

static void emit_fluid(mesh_t* m, vox_grid_t const* g, int x, int y, int z, uint8_t mat) {
    float const X = (float)(x + g->x0), Y = (float)(y + g->y0), Z = (float)(z + g->z0);
    float const uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    // The four corners, (x, z) then (x+1, z) then (x+1, z+1) then
    // (x, z+1) -- anticlockwise seen from above.
    float const h00 = fluid_corner(g, x, y, z, 0, 0);
    float const h10 = fluid_corner(g, x, y, z, 1, 0);
    float const h11 = fluid_corner(g, x, y, z, 1, 1);
    float const h01 = fluid_corner(g, x, y, z, 0, 1);

    // THE SURFACE, both ways round. Only where there is air over it --
    // the same condition the greedy pass uses for a full cell, so a
    // flow running under an overhang has no top face either.
    //
    // EACH COPY IS A DIRECTIONAL FACE, one winding, exactly as the
    // greedy pass emits the flat version. The plants next door do the
    // opposite -- both windings at MESH_DIR_NONE, let the general cull
    // sort it out -- and that is right for a quad standing on its edge,
    // which has no direction to name. A water surface has one, it is
    // within a few degrees of straight up however the corners tilt, and
    // naming it lets the renderer drop the wrong copy on a compare
    // instead of on a cross product. Over a pool that is the difference
    // between 12 triangles a cell and 20, and the triangle budget is
    // the thing this renderer runs out of first (F-63).
    if (block_kind(grid_cell(g, x, y + 1, z)) == K_AIR) {
        int const a = mesh_vert(m, v3(X, Y + h00, Z)), b = mesh_vert(m, v3(X + 1.0f, Y + h10, Z));
        int const c = mesh_vert(m, v3(X + 1.0f, Y + h11, Z + 1.0f)), d = mesh_vert(m, v3(X, Y + h01, Z + 1.0f));
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        mesh_set_dir(m, MESH_DIR_PY);
        mesh_quad(m, d, c, b, a, mat, uv);  // seen from above
        mesh_set_dir(m, MESH_DIR_NY);
        mesh_quad(m, a, b, c, d, mat, uv);  // ... and from under it
    }

    // THE SIDES, against air and against shallower water. Each one runs
    // from whatever the neighbour's surface is up to this cell's two
    // corners on that edge, so the step between two levels is closed
    // rather than left as a tear you can see the riverbed through.
    static int const SX[4]  = {1, -1, 0, 0};
    static int const SZ[4]  = {0, 0, 1, -1};
    static uint8_t const SD[4] = {MESH_DIR_PX, MESH_DIR_NX, MESH_DIR_PZ, MESH_DIR_NZ};
    for (int i = 0; i < 4; i++) {
        int const          nx = x + SX[i], nz = z + SZ[i];
        block_kind_t const nk = block_kind(grid_cell(g, nx, y, nz));
        if (nk != K_AIR && nk != K_LIQUID && nk != K_PLANT) continue;  // a cube hides it
        // A waterlogged neighbour is a FULL cell of water with a plant in
        // it, so there is no step down into it and the side is dropped
        // by the test below rather than drawn against a paddy.
        float const base = cell_is_water(g, nx, y, nz) ? cell_water_height(g, nx, y, nz)
                                                       : fluid_surface(g, nx, y, nz);

        // The two corners of this cell along that edge.
        float t0, t1;
        vec3_t p0, p1;
        if (SX[i] == 1) {
            t0 = h10, t1 = h11;
            p0 = v3(X + 1.0f, 0.0f, Z), p1 = v3(X + 1.0f, 0.0f, Z + 1.0f);
        } else if (SX[i] == -1) {
            t0 = h01, t1 = h00;
            p0 = v3(X, 0.0f, Z + 1.0f), p1 = v3(X, 0.0f, Z);
        } else if (SZ[i] == 1) {
            t0 = h11, t1 = h01;
            p0 = v3(X + 1.0f, 0.0f, Z + 1.0f), p1 = v3(X, 0.0f, Z + 1.0f);
        } else {
            t0 = h00, t1 = h10;
            p0 = v3(X, 0.0f, Z), p1 = v3(X + 1.0f, 0.0f, Z);
        }
        if (t0 <= base && t1 <= base) continue;  // the neighbour is as full: no step
        float const b0 = t0 < base ? t0 : base, b1 = t1 < base ? t1 : base;

        // Outward only. The inward copy would be the wall of the step
        // seen from INSIDE the water, which is an eighth of a block of
        // sliver in a view that is already tinted and murky -- not
        // worth doubling the cost of every flowing cell for. The
        // SURFACE keeps its second copy, which is the one D-86 was
        // actually about.
        mesh_set_dir(m, SD[i]);
        int const a = mesh_vert(m, v3(p0.x, Y + t0, p0.z)), b = mesh_vert(m, v3(p1.x, Y + t1, p1.z));
        int const c = mesh_vert(m, v3(p1.x, Y + b1, p1.z)), d = mesh_vert(m, v3(p0.x, Y + b0, p0.z));
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        mesh_quad(m, a, b, c, d, mat, uv);
    }
    mesh_set_dir(m, MESH_DIR_NONE);
}

static void emit_plant(mesh_t* m, int x, int y, int z, uint8_t mat) {
    // Both windings of each quad are emitted, so a plant is visible from
    // either side. That is the opposite of a directional face: leave it
    // MESH_DIR_NONE and let the general cull drop whichever copy faces
    // away.
    mesh_set_dir(m, MESH_DIR_NONE);
    float const  X = (float)x, Y = (float)y, Z = (float)z;
    float const  uv[4][2]   = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    vec3_t const ends[2][2] = {{{X, 0, Z}, {X + 1.0f, 0, Z + 1.0f}}, {{X + 1.0f, 0, Z}, {X, 0, Z + 1.0f}}};
    for (int k = 0; k < 2; k++) {
        for (int side = 0; side < 2; side++) {
            vec3_t const p = ends[k][side], q = ends[k][1 - side];
            int const    a = mesh_vert(m, v3(p.x, Y + 1.0f, p.z)), b = mesh_vert(m, v3(q.x, Y + 1.0f, q.z));
            int const    c = mesh_vert(m, v3(q.x, Y, q.z)), d = mesh_vert(m, v3(p.x, Y, p.z));
            if (a < 0 || b < 0 || c < 0 || d < 0) return;
            mesh_quad(m, a, b, c, d, mat, uv);
        }
    }
}

void voxel_build_cube(mesh_t* m, float half) {
    int v[8];
    for (int i = 0; i < 8; i++)
        v[i] = mesh_vert(m, v3(i & 1 ? half : -half, i & 2 ? half : -half, i & 4 ? half : -half));
    float const uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    // The corner orders of emit_f(): seen from outside, u right, v down.
    mesh_quad(m, v[3], v[7], v[5], v[1], 1, uv);  // +x
    mesh_quad(m, v[6], v[2], v[0], v[4], 1, uv);  // -x
    mesh_quad(m, v[2], v[6], v[7], v[3], 0, uv);  // +y
    mesh_quad(m, v[0], v[1], v[5], v[4], 2, uv);  // -y
    mesh_quad(m, v[7], v[6], v[4], v[5], 1, uv);  // +z
    mesh_quad(m, v[2], v[3], v[1], v[0], 1, uv);  // -z
}

void voxel_mesh_build(mesh_t* m, vox_grid_t const* g, vox_mesh_mode_t mode) {
    int const      w = g->w, h = g->h, d = g->d, H = h + 2, W = w + 2;
    uint8_t const* cells = g->cells;
#define CIDX(x, y, z) (((size_t)((z) + 1) * (size_t)W + (size_t)((x) + 1)) * (size_t)H + (size_t)((y) + 1))
#define CELL(x, y, z) cells[CIDX(x, y, z)]
    uint8_t const* lights = g->lights;
#define LIGHT(x, y, z) (lights != NULL ? lights[CIDX(x, y, z)] : (uint8_t)MESH_LIGHT_FULL)

    // Faces only exist between a cube and a cell that is not one: from
    // one below the lowest such cell (border included) up to the highest
    // non-air cell of the box. Below is rock, above is sky.
    int ylo = h, yhi = -1;
    for (int z = -1; z <= d; z++) {
        for (int x = -1; x <= w; x++) {
            for (int y = -1; y <= h; y++) {
                block_kind_t const k = block_kind(CELL(x, y, z));
                if (k != K_CUBE && y - 1 < ylo) ylo = y - 1;
                if (k != K_AIR && x >= 0 && x < w && z >= 0 && z < d && y >= 0 && y < h && y > yhi) yhi = y;
            }
        }
    }
    if (ylo < 0) ylo = 0;
    if (yhi < ylo) return;
    int const band = yhi - ylo + 1;

    // The mask of one slice: the face at (p, q) as light << 8 | material
    // + 1, 0 none. Light in the key is what stops a lit face merging
    // with a dark one.
    int const side = w > d ? w : d;
    uint16_t* mask = sm_calloc((size_t)side * (size_t)(side > band ? side : band),
                               sizeof(uint16_t));  // F-12: PSRAM, not the scarce internal heap
    if (!mask) {
        m->failed = true;
        return;
    }
    float const step = (float)g->step;
    // WHICH LIQUIDS THE GREEDY PASS STILL OWNS. A source or a still
    // block of ocean is a full cube and merges with its neighbours like
    // anything else -- which matters, because that is where all the
    // water in a world actually is. Only the flows and the falls are
    // taken out and shaped one at a time, and only on the near meshes:
    // a film half a block deep is not something anybody can see at the
    // far level of detail, so it keeps the cheap full-height surface
    // there, exactly as the plants do.
    bool const shaped_fluids = mode == VOX_MESH_FANCY && g->step == 1 && g->data != NULL;
#define SHAPED(x, y, z) (shaped_fluids && fluid_is_shaped(g, (x), (y), (z)))
    for (int k = 0; k < 6; k++) {
        dir_t const dir = DIRS[k];
        // The slices along the face's axis, and each slice's (p, q) plane.
        int         s0, s1, pn, qn, q0;
        switch (dir.axis) {
            case 0:
                s0 = 0, s1 = w, pn = d, q0 = ylo, qn = band;
                break;
            case 2:
                s0 = 0, s1 = d, pn = w, q0 = ylo, qn = band;
                break;
            default:
                // ONE SLICE HIGHER than the highest block, because the
                // underside of a water surface is emitted by the AIR
                // cell above the liquid (see the mask below) and that
                // cell is, by definition, above the highest block.
                //
                // NEVER past the box, though: a cell outside it belongs
                // to the neighbouring section and is only ever a
                // neighbour here. Letting one act as an owner emitted
                // its faces twice, once from each side of the seam --
                // which meshcheck's sectioning test caught at once
                // (volume 186.667 against 148).
                //
                // A lake whose top cell IS the box's top needs no slice
                // here: the section above owns the air over it and emits
                // the underside from its own y = 0.
                s0 = ylo, s1 = yhi + 2 > h ? h : yhi + 2, pn = w, q0 = 0, qn = d;
                break;
        }
        int const        nx = dir.axis == 0 ? dir.sign : 0, ny = dir.axis == 1 ? dir.sign : 0;
        int const        nz    = dir.axis == 2 ? dir.sign : 0;
        vox_face_t const face  = dir.axis != 1 ? VF_SIDE : dir.sign > 0 ? VF_TOP : VF_BOTTOM;
        bool const       sides = dir.axis != 1;
        for (int slice = s0; slice < s1; slice++) {
            for (int q = 0; q < qn; q++) {
                for (int p = 0; p < pn; p++) {
                    int x, y, z;
                    switch (dir.axis) {
                        case 0:
                            x = slice, y = q0 + q, z = p;
                            break;
                        case 2:
                            x = p, y = q0 + q, z = slice;
                            break;
                        default:
                            x = p, y = slice, z = q0 + q;
                            break;
                    }
                    uint8_t const      b    = CELL(x, y, z);
                    uint8_t const      nb   = CELL(x + nx, y + ny, z + nz);
                    block_kind_t const kb   = block_kind(b);
                    uint16_t           v    = 0;
                    bool const         edge = g->skirt && (x + nx < 0 || x + nx >= w || z + nz < 0 || z + nz >= d);
                    if ((kb == K_CUBE || kb == K_SEE) && (edge || face_shows(b, nb, mode))) {
                        // A skirt takes the block's top material: it only
                        // closes a seam, so it should match the ground.
                        v = (uint16_t)(((unsigned)LIGHT(x + nx, y + ny, z + nz) << 8) |
                                       (unsigned)(mode_mat(b, edge ? VF_TOP : face, mode) + 1));
                    } else if (kb == K_LIQUID && ny > 0 && block_kind(nb) == K_AIR && !SHAPED(x, y, z)) {
                        // THE SURFACE, and the only face a liquid has: no
                        // sides, no bottom, and a top only where there is
                        // air above it. Lit by that air, so it reads as
                        // sky on water rather than as the gloom below.
                        v = (uint16_t)(((unsigned)LIGHT(x + nx, y + ny, z + nz) << 8) |
                                       (unsigned)(voxel_face_mat(b, VF_TOP) + 1));
                    } else if (kb == K_AIR && ny < 0 && block_kind(nb) == K_LIQUID && !SHAPED(x, y - 1, z)) {
                        // THE UNDERSIDE of that same surface, emitted by
                        // the air cell above it so that it lands on the
                        // same plane (emit() puts a +y face at y+1 and a
                        // -y face at y). An axis-aligned face is visible
                        // only from the side its normal points at, so
                        // without this second copy the surface disappears
                        // the moment the eye goes under it -- which is
                        // exactly how swimming used to look.
                        v = (uint16_t)(((unsigned)LIGHT(x, y, z) << 8) |
                                       (unsigned)(voxel_face_mat(nb, VF_TOP) + 1));
                    }
                    mask[q * pn + p] = v;
                }
            }
            // Greedy: grow each rectangle along p, then along q while
            // every row matches; clear what it covers.
            for (int q = 0; q < qn; q++) {
                for (int p = 0; p < pn;) {
                    uint16_t const v = mask[q * pn + p];
                    if (v == 0) {
                        p++;
                        continue;
                    }
                    int wp = 1;
                    while (p + wp < pn && mask[q * pn + p + wp] == v) wp++;
                    int        hq       = 1;
                    bool const no_stack = sides && (v & 0xFFu) == VM_GRASS_SIDE + 1;
                    while (!no_stack && q + hq < qn) {
                        bool row = true;
                        for (int i = 0; i < wp && row; i++) row = mask[(q + hq) * pn + p + i] == v;
                        if (!row) break;
                        hq++;
                    }
                    for (int j = 0; j < hq; j++) memset(&mask[(q + j) * pn + p], 0, (size_t)wp * sizeof(uint16_t));
                    // Into the box's own coordinates. Which of x0/y0/z0
                    // applies depends on which axis the face looks
                    // along: `slice` runs along that axis, `p` and `q`
                    // across the other two.
                    int const sw = slice + (dir.axis == 0 ? g->x0 : dir.axis == 1 ? g->y0 : g->z0);
                    int const pw = p + (dir.axis == 0 ? g->z0 : g->x0);
                    int const qw = q0 + q + (dir.axis == 1 ? g->z0 : g->y0);
                    mesh_set_light(m, (uint8_t)(v >> 8));
                    emit(m, dir, sw, pw, qw, wp, hq, step, (uint8_t)((v & 0xFFu) - 1));
                    p += wp;
                }
            }
        }
    }
    sm_free(mask);
    // The plants (fancy meshes only), torches and signs, one by one (whole
    // blocks only: a coarse grid has neither).
    if (g->step != 1) return;
    for (int z = 0; z < d; z++) {
        for (int x = 0; x < w; x++) {
            for (int y = ylo; y <= yhi; y++) {
                uint8_t const      b = CELL(x, y, z);
                block_kind_t const k = block_kind(b);
                int const          X = x + g->x0, Y = y + g->y0, Z = z + g->z0;
                // A FLOW OR A FALL, shaped by hand. Lit by the air over
                // it, like the flat surface the greedy pass emits, so
                // the top of a stream reads as sky on water rather than
                // as the gloom of whatever it is running through.
                if (k == K_LIQUID && SHAPED(x, y, z)) {
                    mesh_set_light(m, LIGHT(x, y + 1, z));
                    emit_fluid(m, g, x, y, z, (uint8_t)voxel_face_mat(b, VF_TOP));
                }
                if (k == K_BED) {
                    mesh_set_light(m, LIGHT(x, y, z));
                    emit_bed(m, X, Y, Z, (uint8_t)voxel_face_mat(b, VF_TOP), (uint8_t)voxel_face_mat(b, VF_SIDE),
                             grid_data(g, x, y, z) & 0x03u, b == BLK_BED_HEAD);
                }
                if (k == K_PLANT || k == K_TORCH || k == K_SIGN || k == K_FENCE || k == K_GATE || k == K_BARREL) {
                    mesh_set_light(m, LIGHT(x, y, z));  // lit by its own cell
                }
                if (k == K_FENCE) {
                    uint8_t const mat = (uint8_t)voxel_face_mat(b, VF_SIDE);
                    emit_fence(m, X, Y, Z, mat, fence_joins(CELL(x - 1, y, z)), fence_joins(CELL(x + 1, y, z)),
                               fence_joins(CELL(x, y, z - 1)), fence_joins(CELL(x, y, z + 1)));
                }
                if (k == K_GATE) {
                    emit_gate(m, X, Y, Z, (uint8_t)voxel_face_mat(b, VF_SIDE),
                              grid_data(g, x, y, z) & 1u, !block_solid(b));
                }
                if (k == K_BARREL) {
                    emit_barrel(m, X, Y, Z, (uint8_t)voxel_face_mat(b, VF_SIDE),
                                (uint8_t)voxel_face_mat(b, VF_TOP), grid_data(g, x, y, z) & 0x03u);
                }
                if (k == K_SIGN) emit_sign(m, X, Y, Z);
                if (k == K_PLANT && mode == VOX_MESH_FANCY) {
                    // A CROP IS DRAWN BY ITS STAGE, and the stage is in
                    // the state byte this grid already carries -- the
                    // data plane the fluids paid for (D-101). Its four
                    // textures are one run starting at mat[VF_TOP]
                    // (blocks.h), so the stage is an addition and not a
                    // lookup. Anything else draws with its side texture,
                    // as it always has.
                    uint8_t mat = (uint8_t)voxel_face_mat(b, VF_SIDE);
                    if (block_crop(b)) {
                        unsigned stage = grid_data(g, x, y, z) & 0x07u;
                        if (stage >= VOX_CROP_STAGES) stage = VOX_CROP_STAGES - 1u;
                        mat = (uint8_t)(voxel_face_mat(b, VF_TOP) + (int)stage);
                    }
                    emit_plant(m, X, Y, Z, mat);
                }
                // RICE STANDS IN WATER, so its cell owes the pond a
                // surface: without this there is a plant-shaped hole in
                // the shallows with no water in it (blocks.h,
                // BF2_WATERLOGGED). One quad, at the top of the cell,
                // exactly where the greedy pass would have put the
                // water it is standing in.
                if (k == K_PLANT && block_waterlogged(b) && mode == VOX_MESH_FANCY) {
                    mesh_set_light(m, LIGHT(x, y + 1, z));
                    emit_fluid(m, g, x, y, z, (uint8_t)voxel_face_mat(BLK_WATER, VF_TOP));
                    mesh_set_light(m, LIGHT(x, y, z));
                }
                if (k == K_TORCH) {
                    // Upright in the middle of the cell, or shifted to
                    // one wall and lifted, which is what a torch on a
                    // wall looks like without the mesher having to
                    // tilt anything. A greedy voxel mesher emits
                    // axis-aligned boxes; a rotated stick would be a
                    // second kind of geometry for one block.
                    uint8_t const how = g->data != NULL ? g->data[CIDX(x, y, z)] : TORCH_FLOOR;
                    float const   r   = 1.0f / 16.0f;
                    float         cx = (float)X + 0.5f, cz = (float)Z + 0.5f, base = (float)Y;
                    if (how != TORCH_FLOOR) {
                        base += 0.2f;  // brackets hold a torch above the floor
                        switch (how) {
                            case TORCH_WALL_NX: cx -= 0.30f; break;
                            case TORCH_WALL_PX: cx += 0.30f; break;
                            case TORCH_WALL_NZ: cz -= 0.30f; break;
                            case TORCH_WALL_PZ: cz += 0.30f; break;
                            default: break;
                        }
                    }
                    emit_box(m, v3(cx - r, base, cz - r), v3(cx + r, base + 0.625f, cz + r), VM_TORCH);
                }
            }
        }
    }
    mesh_set_light(m, MESH_LIGHT_FULL);
#undef SHAPED
#undef LIGHT
#undef CELL
#undef CIDX
}
