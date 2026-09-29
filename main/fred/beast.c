// =====================================================================
//  SynthMiner  --  the animals' figures (see beast.h)
// =====================================================================

#include "fred/beast.h"

#include <math.h>
#include <string.h>

#include "math/mesh_render.h"

// The materials. One per surface rather than one per animal, so a
// fourth creature adds a colour and not a model.
typedef enum {
    BM_HIDE = 0,   // the body
    BM_PATCH,      // the second colour: a cow's markings, a dog's back
    BM_SNOUT,      // nose, muzzle, beak
    BM_HORN,       // horns, hooves, claws
    BM_EYE,
    BM_COUNT
} beast_mat_t;

typedef struct {
    float   body_l, body_w, body_h;  // the barrel of it
    float   leg_h, leg_r;
    float   head;                    // the head cube's half-size
    float   head_fwd;                // how far the neck reaches out
    bool    horns, snout, ears, tail_up;
    uint32_t argb[BM_COUNT];
} beast_look_t;

static beast_look_t const LOOKS[MOB_KIND_COUNT] = {
    // Never drawn: MOB_NONE is the "no such creature" row.
    [MOB_NONE] = {0},

    // A PIG: long in the body, short in the leg, and all snout. The
    // snout is what makes it a pig at twenty paces.
    [MOB_PIG] = {.body_l   = 0.46f,
                 .body_w   = 0.30f,
                 .body_h   = 0.30f,
                 .leg_h    = 0.26f,
                 .leg_r    = 0.07f,
                 .head     = 0.17f,
                 .head_fwd = 0.16f,
                 .snout    = true,
                 .ears     = true,
                 .argb     = {0xFFE8A0A4u, 0xFFD48A90u, 0xFFF0B8BCu, 0xFF8C6058u, 0xFF201818u}},

    // A COW: taller, heavier, white patches and a pair of horns.
    [MOB_COW] = {.body_l   = 0.55f,
                 .body_w   = 0.34f,
                 .body_h   = 0.38f,
                 .leg_h    = 0.52f,
                 .leg_r    = 0.09f,
                 .head     = 0.20f,
                 .head_fwd = 0.22f,
                 .horns    = true,
                 .snout    = true,
                 .ears     = true,
                 .argb     = {0xFF6A5040u, 0xFFE8E4DCu, 0xFFDCC8B4u, 0xFFD8D0C0u, 0xFF201818u}},

    // A DOG: small, quick, with its tail up -- which is the whole
    // difference between a dog and a wolf here, and a tail is cheaper
    // than either.
    [MOB_DOG] = {.body_l   = 0.36f,
                 .body_w   = 0.22f,
                 .body_h   = 0.24f,
                 .leg_h    = 0.34f,
                 .leg_r    = 0.06f,
                 .head     = 0.15f,
                 .head_fwd = 0.14f,
                 .snout    = true,
                 .ears     = true,
                 .tail_up  = true,
                 .argb     = {0xFFB8B0A4u, 0xFF8C8478u, 0xFF4A4038u, 0xFFE8E4DCu, 0xFF201818u}},
};

static bool   s_ready;
static mesh_t s_body[MOB_KIND_COUNT], s_head[MOB_KIND_COUNT], s_leg[MOB_KIND_COUNT];

static void build_one(uint8_t kind) {
    beast_look_t const* L = &LOOKS[kind];

    mesh_init(&s_body[kind]);
    s_body[kind].name = "beast_body";
    // THE BARREL, with its origin at the shoulder height so a leg hangs
    // from y = 0 and the walk is a rotation about it.
    mesh_box(&s_body[kind], v3(-L->body_w, 0.0f, -L->body_l), v3(L->body_w, L->body_h, L->body_l), BM_HIDE, 1.0f);
    // A stripe of the second colour along the back and down one flank:
    // enough to read as markings without being a texture.
    mesh_box(&s_body[kind], v3(-L->body_w * 0.55f, L->body_h - 0.01f, -L->body_l * 0.7f),
             v3(L->body_w * 0.55f, L->body_h + 0.015f, L->body_l * 0.2f), BM_PATCH, 1.0f);
    // The tail: up for a dog, hanging for the others.
    if (L->tail_up) {
        mesh_box(&s_body[kind], v3(-0.035f, L->body_h * 0.5f, -L->body_l - 0.14f),
                 v3(0.035f, L->body_h + 0.14f, -L->body_l), BM_PATCH, 1.0f);
    } else {
        mesh_box(&s_body[kind], v3(-0.03f, L->body_h * 0.2f, -L->body_l - 0.07f),
                 v3(0.03f, L->body_h * 0.9f, -L->body_l), BM_HIDE, 1.0f);
    }

    mesh_init(&s_head[kind]);
    s_head[kind].name = "beast_head";
    float const h = L->head;
    mesh_box(&s_head[kind], v3(-h, -h, -h), v3(h, h, h), BM_HIDE, 1.0f);
    if (L->snout) {
        mesh_box(&s_head[kind], v3(-h * 0.5f, -h * 0.7f, h), v3(h * 0.5f, h * 0.15f, h + h * 0.55f), BM_SNOUT, 1.0f);
    }
    if (L->ears) {
        for (int s = -1; s <= 1; s += 2) {
            mesh_box(&s_head[kind], v3((float)s * h * 0.55f - h * 0.22f, h, -h * 0.2f),
                     v3((float)s * h * 0.55f + h * 0.22f, h + h * 0.45f, h * 0.2f), BM_PATCH, 1.0f);
        }
    }
    if (L->horns) {
        for (int s = -1; s <= 1; s += 2) {
            mesh_box(&s_head[kind], v3((float)s * h * 0.9f - 0.04f, h * 0.55f, -0.04f),
                     v3((float)s * h * 0.9f + 0.04f, h * 0.55f + 0.10f, 0.04f), BM_HORN, 1.0f);
        }
    }
    // Two eyes, on the front, so the thing has a direction even in the
    // flat-shaded mode where nothing else tells you which end is which.
    for (int s = -1; s <= 1; s += 2) {
        mesh_box(&s_head[kind], v3((float)s * h * 0.45f - 0.035f, h * 0.05f, h - 0.01f),
                 v3((float)s * h * 0.45f + 0.035f, h * 0.05f + 0.07f, h + 0.015f), BM_EYE, 1.0f);
    }

    mesh_init(&s_leg[kind]);
    s_leg[kind].name = "beast_leg";
    mesh_box(&s_leg[kind], v3(-L->leg_r, -L->leg_h, -L->leg_r), v3(L->leg_r, 0.0f, L->leg_r), BM_HIDE, 1.0f);
    mesh_box(&s_leg[kind], v3(-L->leg_r, -L->leg_h, -L->leg_r), v3(L->leg_r, -L->leg_h + 0.05f, L->leg_r), BM_HORN,
             1.0f);  // the hoof, or the paw
}

void beast_init(void) {
    if (s_ready) return;
    for (uint8_t k = MOB_PIG; k < MOB_KIND_COUNT; k++) build_one(k);
    s_ready = true;
}

void beast_shutdown(void) {
    if (!s_ready) return;
    for (uint8_t k = MOB_PIG; k < MOB_KIND_COUNT; k++) {
        mesh_free(&s_body[k]);
        mesh_free(&s_head[k]);
        mesh_free(&s_leg[k]);
    }
    s_ready = false;
}

static xform_t joint(xform_t const* parent, vec3_t at, mat3_t r) {
    xform_t const local = {r, at, 1.0f};
    return xform_mul(parent, &local);
}

void beast_submit(xform_t const* root, uint8_t kind, bool baby, bool sitting, float walk, float stride,
                  uint8_t light) {
    if (!s_ready || kind == MOB_NONE || kind >= MOB_KIND_COUNT) return;
    beast_look_t const* L = &LOOKS[kind];

    mesh_mat_t     mats[BM_COUNT];
    uint32_t const f = SE_TRI_LIGHT(mesh_light_level(light));
    for (int i = 0; i < BM_COUNT; i++) mats[i] = (mesh_mat_t){NULL, L->argb[i], f};

    // A CALF IS THE SAME MODEL AT HALF THE SIZE, which is what the
    // collider does with its box too (mob.c): one number, and the two
    // cannot drift apart.
    float const   scale  = baby ? 0.5f : 1.0f;
    xform_t const scaled = {root->r, root->pos, root->scale * scale};

    // Sitting drops the back end and tips the body up, which is the
    // whole animation a dog needs.
    float const   sit   = sitting ? 0.5f : 0.0f;
    xform_t const spine = joint(&scaled, v3(0.0f, L->leg_h * (1.0f - sit * 0.45f), 0.0f), mat3_rot_x(-sit * 0.5f));
    mesh_submit(&s_body[kind], &spine, mats, BM_COUNT);

    xform_t const head = joint(&spine, v3(0.0f, L->body_h * 0.75f, L->body_l + L->head_fwd), mat3_rot_x(sit * 0.5f));
    mesh_submit(&s_head[kind], &head, mats, BM_COUNT);

    // Four legs, the diagonal pairs in step: front-left with back-right,
    // which is what a walking quadruped does and what stops it looking
    // like a pantomime horse.
    float const swing = 0.6f * stride * sinf(walk);
    for (int fz = -1; fz <= 1; fz += 2) {
        for (int sx = -1; sx <= 1; sx += 2) {
            float const phase = (fz * sx > 0) ? swing : -swing;
            // A sitting animal folds its back legs under it rather than
            // swinging them.
            float const ang = sitting && fz < 0 ? -1.1f : phase;
            xform_t const leg =
                joint(&spine, v3((float)sx * (L->body_w - L->leg_r), 0.0f, (float)fz * (L->body_l - L->leg_r)),
                      mat3_rot_x(ang));
            mesh_submit(&s_leg[kind], &leg, mats, BM_COUNT);
        }
    }
}
