// =====================================================================
//  SynthMiner  --  the miner's meshes (see fred_mesh.h)
// =====================================================================

#include "fred/fred_mesh.h"

// A box with its own material on each face (+x, -x, +y, -y, +z, -z), the
// texture once across each: the head, whose front is the face. Eight
// shared corners, so it is a closed solid; faces wound outward.
static void box6(mesh_t* m, vec3_t lo, vec3_t hi, uint8_t const mat[6]) {
    int v[8];
    for (int i = 0; i < 8; i++) v[i] = mesh_vert(m, v3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
    float const uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    // Corners top-left, top-right, bottom-right, bottom-left, seen from
    // outside (u right, v down on the sides).
    mesh_quad(m, v[3], v[7], v[5], v[1], mat[0], uv);  // +x
    mesh_quad(m, v[6], v[2], v[0], v[4], mat[1], uv);  // -x
    mesh_quad(m, v[2], v[6], v[7], v[3], mat[2], uv);  // +y
    mesh_quad(m, v[0], v[1], v[5], v[4], mat[3], uv);  // -y
    mesh_quad(m, v[7], v[6], v[4], v[5], mat[4], uv);  // +z: the front
    mesh_quad(m, v[2], v[3], v[1], v[0], mat[5], uv);  // -z
}

void fred_build_head(mesh_t* m) {
    mesh_init(m);
    m->name               = "fred_head";
    uint8_t const head[6] = {FM_SKIN, FM_SKIN, FM_HAIR, FM_SKIN, FM_FACE, FM_HAIR};
    box6(m, v3(-0.25f, 0.0f, -0.25f), v3(0.25f, 0.5f, 0.25f), head);
    // The hard hat: a crown, a brim sticking out in front, the lamp.
    mesh_box(m, v3(-0.28f, 0.44f, -0.28f), v3(0.28f, 0.64f, 0.28f), FM_HAT, 1.0f);
    mesh_box(m, v3(-0.30f, 0.42f, 0.20f), v3(0.30f, 0.47f, 0.42f), FM_HAT, 1.0f);
    mesh_box(m, v3(-0.07f, 0.49f, 0.27f), v3(0.07f, 0.61f, 0.33f), FM_LAMP, 1.0f);
}

void fred_build_body(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_body";
    mesh_box(m, v3(-0.25f, 0.0f, -0.15f), v3(0.25f, 0.36f, 0.15f), FM_OVERALLS, 1.0f);
    mesh_box(m, v3(-0.25f, 0.36f, -0.15f), v3(0.25f, FRED_BODY_H, 0.15f), FM_SHIRT, 1.0f);
    // The bib of the overalls, on the chest.
    mesh_box(m, v3(-0.15f, 0.36f, 0.15f), v3(0.15f, 0.58f, 0.17f), FM_OVERALLS, 1.0f);
}

void fred_build_arm(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_arm";
    mesh_box(m, v3(-0.1f, -0.30f, -0.1f), v3(0.1f, 0.0f, 0.1f), FM_SHIRT, 1.0f);
    mesh_box(m, v3(-0.09f, -FRED_ARM_H, -0.09f), v3(0.09f, -0.30f, 0.09f), FM_SKIN, 1.0f);
}

void fred_build_fp_arm(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_fp_arm";
    // The sleeve runs to +0.55 rather than stopping at the shoulder:
    // rotated forward for first person that puts the open end behind
    // the camera and below the view, where its cap cannot be seen.
    mesh_box(m, v3(-0.1f, -0.30f, -0.1f), v3(0.1f, 0.55f, 0.1f), FM_SHIRT, 1.0f);
    mesh_box(m, v3(-0.09f, -FRED_ARM_H, -0.09f), v3(0.09f, -0.30f, 0.09f), FM_SKIN, 1.0f);
}

void fred_build_leg(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_leg";
    mesh_box(m, v3(-0.11f, -0.56f, -0.12f), v3(0.11f, 0.0f, 0.12f), FM_OVERALLS, 1.0f);
    mesh_box(m, v3(-0.12f, -FRED_LEG_H, -0.13f), v3(0.12f, -0.56f, 0.15f), FM_BOOTS, 1.0f);
}

void fred_build_pick(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_pick";
    // The handle through the fist, forward; the head across its end,
    // pointed at both tips.
    mesh_box(m, v3(-0.035f, -0.035f, -0.12f), v3(0.035f, 0.035f, 0.62f), FM_WOOD, 1.0f);
    mesh_box(m, v3(-0.045f, -0.24f, 0.52f), v3(0.045f, 0.24f, 0.62f), FM_IRON, 1.0f);
    mesh_box(m, v3(-0.03f, 0.24f, 0.50f), v3(0.03f, 0.34f, 0.58f), FM_IRON, 1.0f);
    mesh_box(m, v3(-0.03f, -0.34f, 0.50f), v3(0.03f, -0.24f, 0.58f), FM_IRON, 1.0f);
}

// A PAIL: four trapezium sides, a bottom, what is in it, and a wire
// handle over the top.
//
// The taper is the whole silhouette and it is why this is not a
// mesh_box. A straight-sided box of this size in the hand reads as a
// crate or a lunch tin; narrowing the bottom by a third is what makes
// it a bucket at a glance, which is the entire job of a held model.
//
// It is also why the bucket could not just be FRED_HOLD_ITEM like coal
// or a stick: those are lumps and a small coloured cube is a fair
// picture of a lump. A bucket is a shape, and the user's original brief
// asked for the model as well as the icon -- "the associated icons and
// render models (when held in hand)".
void fred_build_bucket(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_bucket";
    float const rt = 0.15f, rb = 0.10f, h = 0.26f;  // top, bottom, height
    float const uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    // Four bottom corners then four top ones, anticlockwise from -x-z.
    int b[4], t[4];
    float const bx[4] = {-rb, rb, rb, -rb}, bz[4] = {-rb, -rb, rb, rb};
    float const tx[4] = {-rt, rt, rt, -rt}, tz[4] = {-rt, -rt, rt, rt};
    for (int i = 0; i < 4; i++) {
        b[i] = mesh_vert(m, v3(bx[i], 0.0f, bz[i]));
        t[i] = mesh_vert(m, v3(tx[i], h, tz[i]));
    }
    // The sides, wound outward: top-left, top-right, bottom-right,
    // bottom-left seen from outside.
    for (int i = 0; i < 4; i++) {
        int const j = (i + 1) & 3;
        mesh_quad(m, t[j], t[i], b[i], b[j], FM_IRON, uv);
    }
    mesh_quad(m, b[0], b[1], b[2], b[3], FM_IRON, uv);  // the bottom, seen from below

    // WHAT IS IN IT, a disc just under the rim. An empty bucket draws
    // this too, in a dark grey: the inside of a pail is not a hole, and
    // without it you can see through the mouth and out of the bottom.
    float const ri = rt - 0.012f, hi = h - 0.025f;
    int const   f0 = mesh_vert(m, v3(-ri, hi, -ri)), f1 = mesh_vert(m, v3(ri, hi, -ri));
    int const   f2 = mesh_vert(m, v3(ri, hi, ri)), f3 = mesh_vert(m, v3(-ri, hi, ri));
    mesh_quad(m, f3, f2, f1, f0, FM_FLUID, uv);

    // The handle: two short uprights and a bar across, at the sides the
    // thumb is not.
    float const w = 0.014f, hh = h + 0.12f;
    mesh_box(m, v3(-rt, h - 0.03f, -w), v3(-rt + 2.0f * w, hh, w), FM_IRON, 1.0f);
    mesh_box(m, v3(rt - 2.0f * w, h - 0.03f, -w), v3(rt, hh, w), FM_IRON, 1.0f);
    mesh_box(m, v3(-rt, hh - 2.0f * w, -w), v3(rt, hh, w), FM_IRON, 1.0f);
}

void fred_build_axe(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_axe";
    // The handle, then a broad blade to one side of its end.
    mesh_box(m, v3(-0.035f, -0.035f, -0.12f), v3(0.035f, 0.035f, 0.62f), FM_WOOD, 1.0f);
    mesh_box(m, v3(-0.03f, 0.03f, 0.40f), v3(0.03f, 0.24f, 0.62f), FM_IRON, 1.0f);
}

// THE HOE. The same handle as the shovel with the blade turned ACROSS
// it, which is the only thing that tells the two apart in a fist at
// arm's length -- and the reason it needs a model at all: without one it
// falls to FRED_HOLD_ITEM and is carried as a coloured cube, which is
// exactly what the user objected to about the bucket ("a generic colored
// block instead of a proper bucket", 2026-09-28).
void fred_build_hoe(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_hoe";
    mesh_box(m, v3(-0.035f, -0.035f, -0.12f), v3(0.035f, 0.035f, 0.60f), FM_WOOD, 1.0f);
    // The blade: wide across x, shallow along z, and hanging BELOW the
    // line of the handle, which is what a hoe does and a shovel does not.
    mesh_box(m, v3(-0.13f, -0.16f, 0.52f), v3(0.13f, -0.04f, 0.60f), FM_IRON, 1.0f);
}

void fred_build_shovel(mesh_t* m) {
    mesh_init(m);
    m->name = "fred_shovel";
    // A longer handle and a flat spade across its end.
    mesh_box(m, v3(-0.035f, -0.035f, -0.12f), v3(0.035f, 0.035f, 0.56f), FM_WOOD, 1.0f);
    mesh_box(m, v3(-0.10f, -0.02f, 0.54f), v3(0.10f, 0.02f, 0.78f), FM_IRON, 1.0f);
}
