// =====================================================================
//  SynthMiner  --  the animals' figures (see beast.h)
// =====================================================================

#include "fred/beast.h"

#include <math.h>
#include <string.h>

#include "common/texcache.h"
#include "math/mesh_render.h"
#include "world/chunk_render.h"

// The materials. One per surface rather than one per animal, so a
// fourth creature adds a colour and not a model.
typedef enum {
    BM_HIDE = 0,   // the body: a TEXTURE, so a breed is a picture
    BM_FACE,       // the front of the head
    BM_PATCH,      // ears and tail, which are too small for a texture
    BM_SNOUT,      // nose, muzzle
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
    // THE HIDE IS A TEXTURE, one 16x16 put once on each box rather than
    // an unwrapped skin: these models are half a dozen boxes apiece and
    // an unwrap would be a layout to maintain for every one of them. A
    // breed marking is a pattern anyway -- an Oxford Sandy and Black is
    // black blotches on sandy, wherever you look at it.
    char const* hide;
    char const* face;
    char const* shorn;  // ... and what is under the fleece (sheep)
    uint32_t argb[BM_COUNT];
} beast_look_t;

static beast_look_t const LOOKS[MOB_KIND_COUNT] = {
    // Never drawn: MOB_NONE is the "no such creature" row.
    [MOB_NONE] = {0},

    // A PIG: long in the body, short in the leg, and all snout. The
    // snout is what makes it a pig at twenty paces.
    // AN OXFORD SANDY AND BLACK (the user's choice of breed): sandy
    // ginger with big black blotches, which is what the breed is -- a
    // plain sandy pig is a Tamworth.
    [MOB_PIG] = {.body_l   = 0.46f,
                 .body_w   = 0.30f,
                 .body_h   = 0.30f,
                 .leg_h    = 0.26f,
                 .leg_r    = 0.07f,
                 .head     = 0.17f,
                 .head_fwd = 0.16f,
                 .snout    = true,
                 .ears     = true,
                 .hide     = "pig_hide.png",
                 .face     = "pig_face.png",
                 .argb     = {0xFFCE9260u, 0xFFD49A68u, 0xFF6A5A50u, 0xFFE0BCA0u, 0xFF8C6058u, 0xFF201818u}},

    // A COW: taller, heavier, white patches and a pair of horns.
    // AN AYRSHIRE (the user's choice): white with sharply edged
    // red-brown patches. The edge is the breed -- a Hereford's markings
    // are soft and a Holstein's are black.
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
                 .hide     = "cow_hide.png",
                 .face     = "cow_face.png",
                 .argb     = {0xFFCEB4A0u, 0xFFDCD2C4u, 0xFFE8E4DCu, 0xFFDCC8B4u, 0xFFD8D0C0u, 0xFF201818u}},

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
                 .argb     = {0xFFB8B0A4u, 0xFFA89E90u, 0xFF8C8478u, 0xFF4A4038u, 0xFFE8E4DCu, 0xFF201818u}},

    // A SHEEP: round, low and mostly fleece, with a dark face. Its
    // body is the widest of the four for its length, which is what
    // makes a woolly thing woolly at this size -- and the shorn
    // texture is what the same shape wears afterwards.
    [MOB_SHEEP] = {.body_l   = 0.42f,
                   .body_w   = 0.32f,
                   .body_h   = 0.34f,
                   .leg_h    = 0.40f,
                   .leg_r    = 0.065f,
                   .head     = 0.15f,
                   .head_fwd = 0.16f,
                   .snout    = true,
                   .ears     = true,
                   .hide     = "sheep_wool.png",
                   .face     = "sheep_face.png",
                   .shorn    = "sheep_shorn.png",
                   .argb     = {0xFFEEEAE0u, 0xFF48403Au, 0xFF48403Au, 0xFF3C3632u, 0xFFD8D0C0u, 0xFF181414u}},
};

static bool          s_ready;
static mesh_t        s_body[MOB_KIND_COUNT], s_head[MOB_KIND_COUNT], s_leg[MOB_KIND_COUNT];
static se_texture_t const* s_hide[MOB_KIND_COUNT];
static se_texture_t const* s_face[MOB_KIND_COUNT];
static se_texture_t const* s_shorn[MOB_KIND_COUNT];

// A box with a material per face, so the head can wear a face on its
// front and hide on the rest. fred_mesh.c has the same helper for the
// same reason; it is six lines and not worth a shared header.
static void box6(mesh_t* m, vec3_t lo, vec3_t hi, uint8_t const mat[6]) {
    int v[8];
    for (int i = 0; i < 8; i++) v[i] = mesh_vert(m, v3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
    float const uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    mesh_quad(m, v[3], v[7], v[5], v[1], mat[0], uv);  // +x
    mesh_quad(m, v[6], v[2], v[0], v[4], mat[1], uv);  // -x
    mesh_quad(m, v[2], v[6], v[7], v[3], mat[2], uv);  // +y
    mesh_quad(m, v[0], v[1], v[5], v[4], mat[3], uv);  // -y
    mesh_quad(m, v[7], v[6], v[4], v[5], mat[4], uv);  // +z: the front
    mesh_quad(m, v[2], v[3], v[1], v[0], mat[5], uv);  // -z
}

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
    float const   h        = L->head;
    uint8_t const head6[6] = {BM_HIDE, BM_HIDE, BM_HIDE, BM_HIDE, BM_FACE, BM_HIDE};
    box6(&s_head[kind], v3(-h, -h, -h), v3(h, h, h), head6);
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
    for (uint8_t k = MOB_PIG; k < MOB_KIND_COUNT; k++) {
        build_one(k);
        // The hides, by name, like every other texture in the game
        // (common/texcache.h). A missing one is not fatal: the material
        // carries a flat colour too and the creature simply draws in it.
        s_hide[k]  = LOOKS[k].hide != NULL ? texcache_get(LOOKS[k].hide) : NULL;
        s_face[k]  = LOOKS[k].face != NULL ? texcache_get(LOOKS[k].face) : NULL;
        s_shorn[k] = LOOKS[k].shorn != NULL ? texcache_get(LOOKS[k].shorn) : NULL;
    }
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

void beast_submit(xform_t const* root, uint8_t kind, bool baby, bool sitting, bool shorn, float walk, float stride,
                  uint8_t light) {
    if (!s_ready || kind == MOB_NONE || kind >= MOB_KIND_COUNT) return;
    beast_look_t const* L = &LOOKS[kind];

    mesh_mat_t     mats[BM_COUNT];
    uint32_t const f = SE_TRI_LIGHT(mesh_light_level(light));
    for (int i = 0; i < BM_COUNT; i++) mats[i] = (mesh_mat_t){NULL, L->argb[i], f};
    // TEXTURED UNLESS THE PLAYER TURNED TEXTURES OFF, in which case the
    // flat colour beside each texture is what the world is drawing too
    // -- an animal should not be the one thing still wearing a picture
    // in the flat-shaded mode.
    if (chunk_render_textured()) {
        mats[BM_HIDE].tex = shorn && s_shorn[kind] != NULL ? s_shorn[kind] : s_hide[kind];
        mats[BM_FACE].tex = s_face[kind];
    }
    // A SHORN SHEEP IS PINK EVEN WITHOUT TEXTURES, or shearing one in
    // the flat mode changes nothing at all.
    if (shorn && L->shorn != NULL) mats[BM_HIDE].argb = 0xFFE2B8B0u;

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
