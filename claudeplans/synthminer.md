# Implementation plan: SynthMiner

Living document: design, step-by-step status, findings and decisions.
Updated whenever a step starts or finishes, something is measured, or
something is decided. Plan approved by the user on 2026-09-20.

## Context

The repository is the `tanmatsu-template-grace` template plus a skeleton
(`main/main.c`: one block turning in front of the camera). The goal is a
basic-but-extendable Minecraft-like game for the Tanmatsu, on SynthEngine3D,
slug `at.cavac.synthminer`, SD card only (`external_only`).

The decisive find: **the showreel already contains a complete, measured voxel
engine** — `../tanmatsu-showreel-grace/main/craftminer/voxel/` (greedy mesher,
chunk LOD renderer, break/place effects, blocky sky) on top of its mesh, math,
camera and texture-cache layers, with 20 generated 16x16 block textures. That is
the hardest ~1500 lines of this project, already written and already tuned
against device measurements. This plan lifts it and builds a *game* around it.

What the showreel does **not** have, and this project must write: streaming
chunks (its world is a fixed 128x32x128 array), player physics and collision,
DDA block picking, items/inventory/crafting, entities and mobs, menus and key
bindings, audio, and persistence.

**User requirements.** Mining, crafting, farming, animal husbandry (pigs, cows,
chickens, dogs), cooking, fishing — **re-imagined away from Minecraft's version
on 2026-09-29 and specified in full in Part A**: five crops (wheat, potatoes,
tomatoes, beans, rice), compost instead of bone meal, cooking on a kitchen stove
that reads the chest beside it, and four small machines with their own timers
(D-104..D-109). Tree seeds stay. Dogs are found wild and tamed with steak.
Mobs: zombies, skeletons, spiders — **no creepers, and no mob griefing**. Beds set spawn. Death **keeps the inventory**
and respawns at spawn with half health. Health + hunger + tool durability.
Multiple seeded worlds on the SD card, written **only when needed**. Far Lands
west of spawn, a short walk away (D-78). Core 1 for background work. Every key remappable in a menu.

**User decisions (D-05..D-08 below).** F1-F6 are hotbar slots, so the engine's
F1-exits is off and leaving goes through a pause menu that saves first. Textures
*and* flat shading, selectable in a graphics menu, default textured at half
resolution. The first playable build is walk / mine / place / save, without mobs
or crafting. Survival is the full version: health, hunger and tool durability.

## About this document

This file is committed with the work. It contains the design (Parts L, W, K, N,
P, T, H, G, X, F, C, A), the step-by-step plan with a status table (Part D), and
the findings and decisions logs (Part E).

Numbering starts at **F-01 / D-01** — this is a different repository from the
showreel, whose logs run to F-39 / D-54. Standing rules carried over from it:

- **Engine, graceloader or launcher problems: stop and ask.** No workarounds.
- Builds run in the **foreground**, never backgrounded with sleep-polling
  (`.claude-memory/feedback_no_background_builds.md`).
- **Commit and push only when the user asks.**
- Tight timeouts; no routine BadgeLink downloads (`--fetch` only when a look has
  to be judged).

---

## Part L: source layout

Three principles, in order:

1. **Pure modules first.** Anything that can compile with a plain host `cc`
   does. That is what makes the hands-free host harness (Part H) possible, and
   it keeps world, physics and crafting logic away from engine and RTOS
   dependencies.
2. **Data tables, not switch statements.** Adding a block, an item, a recipe or
   a mob is a table row plus a PNG — never a code edit in five files.
3. **One directory per concern.**

**`*` marks what exists today; everything else is still the plan.** Kept
honest deliberately -- a layout document that quietly disagrees with the tree
is worse than none, because it is the thing a newcomer trusts.

```
main/
* main.c                  app_main, se_app_config_t, the five callbacks
  app.h                   sm_app_t -- the context threaded through se_run's `user`
  common/
*   psram.h               sm_alloc/sm_calloc/sm_free -- the ONLY host/badge seam
*   rng.{c,h}             xorshift64*, hash2/hash3, value noise          (pure)
*   tags.{c,h}            the NBT-like tagged-field codec (D-30)         (pure)
*   texcache.{c,h}        LIFTED
  math/
*   xform.{c,h}           LIFTED (vec3_t, mat3_t, smoothstep)            (pure)
*   mesh.{c,h}            LIFTED + mesh_tri_t.dir                        (pure)
*   mesh_render.{c,h}     LIFTED + mesh_submit_world()
*   camera.{c,h}          LIFTED
  voxel/
*   voxel_mesh.{c,h}      LIFTED + sections (D-34) + light in the merge key (pure)
*   voxel_sky.{c,h}       LIFTED: sun, moon, world-space clouds, stars
*   starfield.{c,h}       LIFTED from the showreel's common/
    voxel_fx.{c,h}        LIFTED, not yet built: waits on the crack overlay
    backdrop.{c,h}        LIFTED, not yet built
    horizon.{c,h}         LIFTED, not yet built
  world/
*   blocks.{c,h}          BLOCK REGISTRY                                 (pure)
*   blockent.{c,h}        BLOCK ENTITIES: furnace, chest and trash slots (pure)
*   worldgen.{c,h}        pure (seed, cx, cz) -> id/state planes         (pure)
*   farlands.{c,h}        Beta 1.7.3's density generator, overflowed     (pure)
*   datadir.{c,h}         /sd/synthminer: adopting old data into it (D-80),
*                         renaming the saves and retiring CraftMiner (D-91, D-92) (pure)
*   chunk.{c,h}           chunk_t, the ring store, world_block/set/state
*   light.{c,h}           sky + block light: floods on arrival and on change (pure)
*   chunk_codec.{c,h}     RLE over a chunk's two planes                  (pure)
*   chunkmesh.{c,h}       one vertical section into a mesh               (pure)
*   region.{c,h}          region file: header, dual directory, compaction
*   vfs_compat.{c,h}      the FatFs calls graceloader does not export (D-27)
*   worldstore.{c,h}      save slots, level.smw (player + inventory), FatFs
                          enumeration, adopting the pre-slots Testworld (D-60)
*   chunk_worker.{c,h}    the core-1 task, queues, the ownership contract
*   chunk_render.{c,h}    per-section LOD cache, frustum cull, submission
  game/
*   tick.{c,h}            fixed step
*   replay.{c,h}          record / play the per-tick input (pure)
*   daytime.{c,h}         the clock: sun, sky, fog, the light table (pure)
*   physics.{c,h}         swept AABB against voxels                      (pure)
*   raycast.{c,h}         DDA block pick                                 (pure)
*   player.{c,h}          movement, mining, spawn/place/reset; health/hunger shown only
*   interact.{c,h}        break/place, drops, TREE FELLING               (pure)
*   input.{c,h}           se_bindings, the look abstraction, key names, swap-binding
*   hud.{c,h}             crosshair, block outline, hotbar, bars
*   flycam.{c,h}          the debug camera, on F
*   membench.{c,h}        what the memory costs, at boot (F-40)
*   furnace.{c,h}         smelting: the lazy clock, fuel, the cook timer (pure)
*   composter.{c,h}       a day a unit, and the worms fishing needs (pure)
*   maker.{c,h}           the cheese maker and the sausage maker (pure)
*   mob.{c,h}             THE CREATURE REGISTRY: pool, tick, save (pure)
*   benchpath.h           the benchmark flight's seed and path (step 41) (pure)
  items/
*   items.{c,h}           ITEM REGISTRY; ids below BLK_COUNT are blocks  (pure)
*   inventory.{c,h}       slots, hotbar, stacking, the Tab grid          (pure)
*   item_entity.{c,h}     dropped items: pool, tick, despawn             (pure)
*   recipes.{c,h}         RECIPE TABLE + resolver, and the auto-planner  (pure)
  fred/
*   fred.{c,h}            PORTED showreel miner: the player's figure and arm
*   fred_mesh.{c,h}       his meshes, plus an axe and a shovel
*   beast.{c,h}           the animals' figures: one model, a row per kind
  audio/
*   audio.{c,h}           the mixer's lifetime; footsteps, landings
*   sfx.{c,h}             THE EFFECT TABLE: one row per sound
*   music.{c,h}           which piece, and how long the silence before it
*   midi_seq.{c,h}        Standard MIDI File sequencer, PORTED from tadoom  (pure)
*   midi_synth.{c,h}      a voice pool over se_voice.h; GM families -> six shapes
  i18n/
*   fold.{c,h}            UTF-8 -> QWERTY, so the box searches 32 languages (pure)
*   i18n.{c,h}            T(id), the language, and a printf that reorders    (pure)
*   strings_gen.{c,h}     GENERATED from lang/*.txt by tools/make_lang.py
  ui/
*   title.{c,h}           "SynthMiner" in blocks, on a scratch world (D-58)
*   menu.{c,h}            every menu: title strip, then se_ui panels for slots,
                          new world, typing, settings, controls, pause
*   keybind_ui.{c,h}      PORTED from synthracer: a binding as a key cap or label
*   icons.{c,h}           PORTED from synthracer: the launcher's key-cap PNGs
*   settings.{c,h}        settings.txt on the SD card: language, graphics, audio, gyro, bindings (D-67)
*   craft_ui.{c,h}        the recipe book: filter, have/missing, auto-craft
*   furnace_ui.{c,h}      the three slots, and the lazy clock's catch-up
*   chest_ui.{c,h}        two grids side by side; the trashcan is the same screen
*   composter_ui.{c,h}    scraps in, compost and worms out
*   maker_ui.{c,h}        the two makers, on one screen with two to four rows
*   bench_ui.{c,h}        the disassembly bench
*   amount_ui.{c,h}       how many to move: slider and typed number
*   cheat_ui.{c,h}        any item in the game, English only (test aid)
* testkit/                wired into CMakeLists; PROF_HUD added (F-46)
assets/
* music/*.mid             the soundtrack, public domain (assets/music/MUSIC.md)
* music/manifest.json     where each file came from, and its hash
tools/
* worldcheck.c            host test of every pure module
* get_music.py            fetch/verify the soundtrack; refuses anything not PD
* symcheck.sh             every symbol we call, the loader can resolve (F-74)
* make_fold.py           the fold table, from every character in every lang file
* ids.txt                every block id and item name ever shipped (D-74)
* testrun.py             the device test harness
* recover.py             after a crash or a hang
  scenecheck.c            host budget test via synthengine3D/host/se_host_stub.c
* meshcheck.c             LIFTED + the sectioning check (D-34)
* hostpurity.sh           the pure set really is pure
* badgectl.py             ping / mode / exitapp
* make_textures.py        LIFTED
* textures/               the block PNGs
```

Two departures from the plan above, both deliberate: `hud.{c,h}` and
`input.{c,h}` live in `game/` rather than `ui/` and `input/`, because each is
one file that only the player uses and a directory holding one file is a
directory you forget to look in. They move the day a second file joins them.

### The registries (the extendability contract)

**There are five**, not the three this section was written for: blocks,
items, recipes, `biome_def_t` since the biomes round (step 33) -- and
since step 10 `mob_def_t` in `game/mob.h`, where a pig and a dog differ
by a row and not by a file. Part L's plan said "one file per creature";
what was built is a table, for the same reason everything else here is
one.

The four that were here before: blocks, items, recipes, and
`biome_def_t` in `world/worldgen.h`, which is a registry by the same contract -- a place is
a row (surface and filler block, soil depth, tree and plant chances, the
three height numbers, which log and leaf it grows, what stands in its
columns, what caps it above a height). Adding a biome is a row; adding a
second desert plant or a third tree is a column already there.

```c
/* main/world/blocks.h */
typedef struct {
    char const* name;        /* "stone" -- the stable id for any future save format */
    uint8_t  kind;           /* K_AIR/K_CUBE/K_SEE/K_PLANT/K_TORCH -- drives voxel_mesh.c */
    uint8_t  mat[3];         /* VM_* for VF_TOP / VF_SIDE / VF_BOTTOM */
    uint16_t hardness;       /* ticks to break bare-handed; 0xFFFF unbreakable */
    uint8_t  tool, tool_level;
    uint16_t drop_item;      /* ITEM_NONE drops nothing */
    uint8_t  drop_min, drop_max;
    uint8_t  flags;          /* BF_SOLID|BF_OPAQUE|BF_FELLABLE|BF_CROP|BF_GRAVITY|BF_REPLACEABLE */
    uint8_t  light;          /* emitted light 0..15 (world/light.h) */
    uint8_t  growth_max;     /* BF_CROP: highest growth stage */
    uint8_t  sound;          /* block_sound_t: what it sounds like (audio/sfx.h) */
    uint8_t  flags2;         /* BF2_* -- the first byte ran out */
} block_def_t;
extern block_def_t const BLOCKS[BLK_COUNT];
```

`voxel_mesh.c`'s `kind()` becomes `BLOCKS[b].kind` and `voxel_face_mat()` becomes
`BLOCKS[b].mat[face]`. That is the **whole** change to the donor mesher, and
`vox_grid_t` is untouched (F-07). Adding a block is one row, one 16x16 PNG and
one `metadata.json` line.

`item_def_t` (name, label, stack_max, place_block, tool/level, durability, food
values, icon-or-NULL) follows the same shape. `recipe_t` was sketched here as
(w, h, in[9], out, out_n, shapeless, station) and **Part C replaced it with an
ingredient multiset** once the grid went away. `entity_def_t` is a type vtable (`tick`, `submit`, `interact`,
`on_death`) over a fixed `entity_t` pool, so adding a mob is one file plus one
row.

---

## Part W: chunk format and world dimensions

| | Value | Why |
|---|---|---|
| Chunk XZ | **16 x 16** | Keeps `VOX_CHUNK == 16`, so the donor `fill_fine`/`fill_coarse` and the mesher's greedy mask sizing are unchanged. |
| Chunk Y | **64**, one column, no vertical chunking (D-11) | The showreel's 32 is too shallow for mining plus build room plus a bedrock-to-sky Far Lands wall. 64 keeps the **single-column** layout, which is what lets a column stay contiguous for the mesher. Vertical chunking would save memory but break that for no gameplay gain at this scale. |
| Sea level | **24** | ~20 blocks of stone and caves below, ~40 above. |
| Coordinates | x, z `int32_t`; y `0..63` | The Far Lands edge (x = -2048) fits trivially. |

Two parallel 1-byte planes, column-major exactly as the donor mesher expects:

```c
#define CH_W 16
#define CH_H 64
#define CH_D 16
#define CH_CELLS (CH_W*CH_H*CH_D)                          /* 16384 */
#define CH_IDX(x,y,z) ((((size_t)(z)*CH_W + (size_t)(x))*CH_H) + (size_t)(y))
```

| plane | bytes | contents |
|---|---|---|
| `id[CH_CELLS]` | 16 KiB | block id (0 = air) — the mesher's input layout, verbatim |
| `st[CH_CELLS]` | 16 KiB | bit 0 `ST_PLACED`; bits 1-3 growth stage; bits 4-6 variant/facing; bit 7 reserved |

Two planes rather than one interleaved `uint16` plane, because
`voxel_mesh_build()` takes a `uint8_t const* cells`. Interleaving would force a
de-interleave pass on every remesh (D-12).

**The store is a 16x16 ring with an identity check, not a hash** (D-13):

```c
#define RING 16                                    /* 256 slots, radius up to 7 */
#define SLOT(cx,cz) ((((cx) & (RING-1)) * RING) + ((cz) & (RING-1)))
static inline chunk_t* chunk_find(int32_t cx, int32_t cz) {
    chunk_t* c = &s_slots[SLOT(cx,cz)];
    return (c->cstate == CS_READY && c->cx == cx && c->cz == cz) ? c : NULL;
}
```

Two ANDs and a compare. `chunk_find` is the hottest function in the program —
mesher border fetches, collision, DDA all go through it — and a probing hash
would cost several times more on that path. The `cx`/`cz` identity check makes
aliasing safe: a stale occupant simply reads as absent. Residency radius 6, evict
at 8 (hysteresis), so a wanted slot is free before it is wanted.

Everything is allocated **once at boot as a slab**: no runtime allocation in the
worker, no fragmentation, the worst case known at start. 256 x (16 + 16) KiB =
**8 MiB**, plus ~3.2 MiB of chunk meshes.

**Measured on the badge (F-22):** 28796 KiB of PSRAM is free once the engine has
booted, and the slab leaves 20604 KiB. So this is comfortable with about 20 MiB
to spare, and none of the fallbacks (`CH_H` 64->48, `RING` 16->8, a pool of `st`
planes) is needed. Internal SRAM is untouched at 160 KiB free / 62 KiB largest.

**Unloaded chunks read as `BLK_BARRIER`** (D-14): `{K_CUBE, BF_SOLID,
hardness 0xFFFF}`, never meshed (a chunk is meshed only when its eight
neighbours are `CS_READY`). So the player *stops* at the edge of generated
terrain instead of falling through it, and after half a second pressed against
one the HUD says "Generating...". That is the honest answer to outrunning
generation.

---

## Part K: the core-1 chunk worker

```c
#define WORKER_PRIO  (configMAX_PRIORITIES - 6)  /* below mixer -2, PPA -3, MP3 -4 */
#define WORKER_CORE  1
#define WORKER_STACK 6144
```

`-6` leaves `-5` free for a second worker later without re-tuning. The ~22 KiB
mesher scratch grid lives in PSRAM and is **owned by the worker** — the donor
meshes through a *shared static* scratch (`voxel_render.c:121`), which races the
moment a second task meshes (F-08).

Two queues of 32: `chunk_job_t {kind, lod, seq, slot, cx, cz}` in, and
`chunk_result_t {kind, lod, seq, ok, slot, cx, cz, mesh_t mesh}` out. Load jobs
are enqueued by the main thread nearest-first, at most 2 a frame, and only while
the queue has room to spare so a save can always get in.

### The ownership contract (no locks on the hot path)

| Data | Written by | Rule |
|---|---|---|
| `id[]`, `st[]` while `CS_LOADING` | worker | Main treats the chunk as absent (`BLK_BARRIER`). |
| `id[]`, `st[]` while `CS_READY` | **main only** | An edit bumps `edit_seq`. A mesh result whose `seq` differs is discarded and re-queued. A save job snapshots `edit_seq`; if it moved, the chunk stays `CF_EDITED`. |
| `lod[]` meshes | **main only** | The worker builds a fresh `mesh_t` in its own storage and passes the struct **by value**. Main does `mesh_free(&c->lod[l]); c->lod[l] = r.mesh;` — a swap the renderer can never observe half-done, because the renderer *is* main. |
| `cstate` | main | Only main transitions; the worker reports, main applies. |

The worker allocates mesh storage (PSRAM, through `mesh_vert`/`mesh_tri`
growth); main owns and frees it. Main drains the result queue with a budget of
**2 mesh + 2 load results per frame**, so a burst cannot blow a frame.

**Synchronous escape hatch** (D-15): `chunk_store_set_synchronous(true)` runs
every request inline on the caller. Required for the testkit `shots` test and for
the spawn neighbourhood below. Built in from day one — retrofitting it is painful.

### Entering a world (D-25, D-26)

The three ways in are deliberately different, because their costs are:

| | what happens | cost |
|---|---|---|
| **Create** | "Creating world": pre-generate the spawn area and **save it**, behind a progress bar. | ~9.5 s of generation (F-23) plus the write — once, at a moment a player already expects to wait. |
| **Load** | Read the 3x3 around the saved player position out of the region file. | a disk read; to be measured, but expected far under generation's 56 ms a chunk |
| **Respawn** | The same, at the bed or at world spawn. | as Load — both are places that have been visited or were pre-generated, so both are on disk |

Pre-generating at creation is what makes the other two cheap: the spawn area is
never generated twice, and the single large write at creation fits the
"write only when needed" rule better than a drip of writes during play.

In all three cases the sequence is the same:

1. **Physics frozen.** The player is placed but does not fall, and no tick runs.
2. **The 3x3 around the player** is loaded (or, only at creation, generated),
   synchronously. Nine chunks — not the whole view.
3. **Play starts.** The gate is those nine and nothing else, so a large view
   distance never delays the start of the game.
4. **The rest of the view distance streams in** on the worker over the next few
   seconds, nearest first, while the player is already walking. What has not
   arrived yet is fog.

This is the concrete form of determinism rule 4 (Part T): a tick never branches
on load state, because a tick only ever runs with its 3x3 present.

---

## Part N: what saves have to survive

Every format here will grow: more block types, more player attributes,
creatures with more to remember. A save that cannot absorb that is a save that
gets thrown away, so the shape of the answer is decided once, here, rather than
per feature.

### Three tiers of per-block state (D-29)

| tier | what it holds | cost |
|---|---|---|
| **the id alone** | stone, dirt, planks — most of the world | 1 byte |
| **id + 7 bits of data** | a crop's growth stage, which way a stair or rail faces, a redstone wire's power, a lit furnace | already there, and RLEs to nothing because it is zero almost everywhere |
| **a block entity** | a furnace's slots and burn timer, a chest's 27, a sign's text | a record in the chunk's side list |

The state byte is `ST_PLACED` (bit 0, universal — the felling rule reads it) plus
**7 bits whose meaning belongs to the block type**, 0..127. That is far more than
the 4 bits Minecraft managed with for years, and it costs nothing.

Creatures and dropped items are not blocks and live in the chunk's **entity**
list.

### Growth without migrations (D-30)

Anything variable-sized — a block entity, a creature — is stored as **named,
typed, skippable fields** (`common/tags.h`), NBT's model over a byte buffer
because a chunk payload is built in memory on the worker, not through a `FILE*`.

Every load is the same three steps, and this is the whole mechanism:

1. fill the struct with **defaults**
2. walk the fields present, overwrite what is **recognised**
3. **skip** anything else

So a world opened by a newer build picks up new defaults for fields it never
had, and is written back complete the next time its chunk is unloaded — worlds
upgrade themselves, gradually, with no upgrade pass and no version check. A
world opened by an *older* build keeps working too: unknown fields are stepped
over. Adding "is this dog sitting", "is this mob aggressive" or a nametag is one
line in the writer and one case in the reader, and no migration.

The same applies one level up: the chunk payload ends in a list of
**sections** (`u8 id, u32 length, bytes`), and a reader skips ids it does not
know. Block entities and entities arrived that way and whatever comes next can
too.

### Block ids can move (D-31)

The chunk planes store one byte per cell, so a saved id only means something
next to the table current when it was written. `level.smw` records that table:
every block's **name** against the id it had. On open each name is looked up in
today's registry and a remap is built, which `chunk_decode` applies as it
unpacks. Blocks can then be added anywhere, reordered, or removed and old worlds
still load. A name this build no longer has becomes air, reported rather than
hidden.

Entities and block entities name their **type as a string** in their own record
instead, since there are few enough of them for that to cost nothing — so they
need no palette and cannot be misread after a renumbering.

### Major versions, for the changes tags cannot absorb (D-32)

Tags and palettes handle everything additive. A change that alters a layout
wholesale is what the **major version in each file's magic** is for:
`"SMR" + digit` for regions, `"SMW" + digit` for `level.smw`. A mismatched
major is **refused, not guessed at**, so a future upgrader has something
definite to act on. It moves only for such a change, never for a new field.

### What persists, and for how long (D-33)

Animals, mobs and dropped items are saved with the chunk they stand in, and come
back when it loads — the point being that the world is *consistent*: a cow left
in a field is in that field tomorrow, and a chest of ore dropped in a cave is
still there.

- **Creatures persist indefinitely**, like blocks. They are not despawned for
  being far away.
- **Dropped items despawn after 10 minutes** — 12000 ticks at 20 Hz. The age
  advances only while the chunk is loaded, so walking away and coming back does
  not cost the player their drops; it is idling next to them that does.
- **Every persisted duration is a count of TICKS ELAPSED**, never seconds and
  never a timestamp to compare against. See D-51: this is the rule for crop
  growth, furnace burn, breeding cooldowns and hunger as much as for a dropped
  item, and getting it wrong is only visible weeks later.
- **Hostile mobs keep spawning** in unlit places, up to a **cap per loaded
  chunk**. Because entities are stored per chunk, that cap is a count of what is
  already there — and it is what bounds the population, given nothing despawns.

## Part P: on-disk format

Root is **`/sd/synthminer`** (`SM_DATA_DIR`, `world/datadir.h`) -- NOT the
install directory, which is where this started and where the launcher may
empty it on an update (D-80). It was `/sd/craftminer` until the game was
renamed, and a card that still has that is adopted from on start (D-91).

```
<base>/worlds/worlds.idx              NBT index (an optimisation, not the truth)
<base>/worlds/<slug>/level.smw        NBT: seed, player, spawn, time, inventory
<base>/worlds/<slug>/r.<rx>.<rz>.smr  region: 8x8 chunks
<base>/bench/                         the benchmark world, outside worlds/ (step 41)
```

The extensions were `.cmw` and `.cmr`, and the magics `CMW1` / `CMR1`, when
the game was CraftMiner. **The formats did not change with the name** -- only
what they are called. Files are renamed on start, and both magics are read,
so a save from before the rename loads whether or not the rename reached it
(D-91).

**`se_save.h` is not used for worlds** (F-05): it is a numbered-slot framework
(`SE_SAVE_SLOT_COUNT` 3, one directory) and does not fit many named worlds.
`se_nbt.h` is used directly on our own paths, and the peek idea is reimplemented
in `worlds.idx`.

**Region files, 8x8 chunks** (D-16). Per-chunk files lose twice on FAT and a slow
SD card: a directory scan per open, and a wasted cluster per file (32 KiB
clusters against ~2 KiB of RLE'd chunk). 8x8 rather than Minecraft's 32x32
because 1024 chunks a region is far more than this world touches at once, and a
smaller region makes compaction cheap.

```c
/* r.<rx>.<rz>.smr, little-endian */
struct cmr_header {              /* 0x000, 64 bytes */
    char     magic[4];           /* "SMR1" */
    uint16_t version;
    uint16_t region_dim;         /* 8 */
    int32_t  rx, rz;
    uint16_t chunk_w, chunk_h, chunk_d;  /* a mismatch = refuse to load */
    uint32_t waste;              /* bytes orphaned by rewrites */
    uint32_t dir_serial;
    uint8_t  reserved[28];
};
struct cmr_entry {               /* 8 bytes, index = lcz*8 + lcx */
    uint32_t offset;             /* 0 = absent */
    uint16_t length;
    uint8_t  codec;              /* 0 raw, 1 RLE */
    uint8_t  flags;              /* bit0 has_entities */
};
/* 0x040 directory copy A + crc32; 0x240 copy B + crc32; 0x440 payloads */
```

**Dual directory + serial + CRC32** is the durability mechanism (D-17). `fsync`
*is* exported (F-06 corrects an earlier assumption), but a FAT driver's
power-loss semantics are not something to bet a world on. Write order: append the
payload and flush; write the *stale* directory copy with the new entry, its CRC
and `serial = cur + 1`, and flush; update `waste` and close. A power loss between
any two steps leaves one valid copy, and the reader takes the higher serial whose
CRC validates. Worst case one chunk's last save is lost, never the region.

**Codec: RLE**, `(value, count)` pairs over each plane in memory order. Vertical
runs of stone and air dominate, so a chunk goes 16 KiB -> 1-3 KiB per plane.
Deliberately **not** zlib/miniz: `main/testkit/screenshot.c` documents the trap —
a compressor wants ~130 KiB of state out of a heap that lands in scarce internal
SRAM. RLE needs no state, is thirty lines, and round-trips in a host test.

Rewrites append and add the old length to `waste`; when `waste > filesize/2`,
and only at an explicit save or unload, the region is rewritten to a temp file
and renamed.

**World enumeration, two tier** (D-18): `worlds.idx` answers the world-select
screen in one file open; when it is missing, stale, or an entry fails to resolve,
the list is rebuilt by enumerating `worlds/` with FatFs. `opendir`/`readdir` are
**not exported** (F-06) — copy `dir_open()` from `synthengine3D/src/se_mp3.c:198`
verbatim, including its volume-path probing. Deleting a world uses `f_unlink`
(`remove` and `unlink` are not exported either). A world hand-copied onto the
card therefore still works, which it must.

**Writes happen only on**: chunk eviction, the pause menu's Save, quit to
launcher, and death/respawn. Never per tick.

---

## Part T: determinism and the tick

The simulation runs at a fixed **20 Hz** (`TICK_DT = 0.05f`), decoupled from
rendering, which interpolates. Physics is then stable at any frame rate, a
recorded input stream replays exactly, and the frame becomes a pure function of
`showtime_now()` — the precondition the testkit's `shots` test needs.

```c
s_acc += dt;                              /* already clamped by SE_FRAME_DT_MAX */
int n = 0;
while (s_acc >= TICK_DT && n < TICK_MAX_CATCHUP /*4*/) { tick_once(&in); s_acc -= TICK_DT; n++; }
if (s_acc >= TICK_DT) s_acc = 0.0f;       /* give up; never spiral */
float const alpha = s_acc / TICK_DT;      /* render lerp(prev, cur, alpha) */
```

**Look is split**: sampled at frame rate into float yaw/pitch so the camera is
smooth, and snapshotted quantised into the tick input. The simulation (what the
mine ray hits) uses the snapshot; the camera uses the continuous value. A <=50 ms
crosshair disagreement is imperceptible and buys both smoothness and exact
replay.

```c
typedef struct {            /* 8 bytes; 160 B/s; a 10-minute replay is 96 KiB */
    uint16_t buttons;       /* FWD|BACK|LEFT|RIGHT|JUMP|SNEAK|MINE|USE|... */
    int16_t  yaw_q, pitch_q;/* absolute, 1/65536 turn */
    uint8_t  hotbar;        /* 0..5, 0xFF unchanged */
    uint8_t  flags;
} tick_input_t;
```

**The four determinism rules** (they go in `tick.h`'s header comment — they are
the contract):

1. Nothing in a tick reads wall-clock time or `dt`. The step is the constant.
2. All randomness comes from explicit seeded streams (`world_rng`, `entity.rng`),
   advanced only inside ticks. Never `esp_random()`, never `rand()`.
3. **Worldgen is a pure function of (seed, cx, cz)** and never reads a neighbour
   chunk. Cross-chunk decorations (trees) iterate the 3x3 neighbourhood's
   candidate positions and write only cells inside *this* chunk, so load order
   cannot change content.
4. **A tick runs only when the 3x3 chunks around the player are `CS_READY`**
   (D-19). Otherwise the tick is skipped and the frame still renders. This is
   what async streaming forces: no tick ever branches on load state, so a replay
   is exact however fast the SD card was that day.

In `shots` mode main turns on synchronous chunk loading, fixes the seed, drives
the tick count from `round(showtime_now() / TICK_DT)` and reads inputs from a
canned script compiled into the build. Without rule 4 and the synchronous hatch
the hashes would never be stable.

---

## Part H: the host harness

Everything under `main/world/` (bar `chunk_worker.c` and the FatFs half of
`worldstore.c`), `main/game/{physics,raycast,interact}`, `main/items/*`,
`main/voxel/voxel_mesh.c` and `main/math/*` compiles with a plain `cc`. The one
seam is `main/common/psram.h` (`sm_alloc` -> `malloc` or `heap_caps_malloc`).
No module in the pure set may include `esp_heap_caps.h`, `esp_log.h` or FreeRTOS
headers — enforced by a grep rule in `make check`.

`make worldcheck` (`tools/worldcheck.c`):

| Check | What it proves |
|---|---|
| Worldgen determinism | The same chunk twice is byte-identical; a 3x3 span generated chunk-by-chunk equals the same span generated whole (rule 3). Includes coordinates near x = -100000. |
| Far Lands shape | Part X. |
| Mesher validity | The donor `meshcheck` cases (closed, consistently wound, outward, volume = cells, area = exposed faces) plus: no face between two solids; border cells emit nothing outside the chunk AABB (a streaming-only bug the showreel could not have); every triangle's `dir` matches its computed normal and the direction groups are contiguous. |
| AABB sweeps | 10000 random (start, velocity) pairs: never ends inside a solid; zero velocity is a fixed point; no tunnelling to 40 m/s; a 1.0-block step-up succeeds and 1.5 does not; a 0.6-wide body fits a 1-wide gap and not a 0.5 one. |
| DDA picking | 10000 random rays against a brute-force march at 1/64 block: same block, same face normal, reach honoured, normal points at the cell a placement would fill. |
| Crafting | Distinct output + station per recipe; every ingredient, output and `drop_item` exists; every reversible recipe round-trips; discovery, auto-crafting and the search fold, each in its own row (Part C). **The old promise -- "resolves at every grid offset; no two collide" -- is retired with the grid it was written for**: nothing infers a recipe from ingredients any more, so a pickaxe and an axe are free to want the same three planks and two sticks. |
| Water | A pool, counted per material and per direction: the surface is one merged quad up and one down, the water has no side or bottom faces at all, the stone under it keeps its top face, and a pool with a block over it draws no water at all (D-86). |
| Menu labels | Every settings-row label, in all 32 languages, is narrower than the value column it sits beside -- measured with the engine's own glyph advances at the menu's row height (F-76). |
| Music | Every file in `assets/music` loads, has notes, and ENDS inside twenty minutes at the real sample rate; a rewind replays it identically; every truncation of it still terminates. Junk, a header with no tracks and an SMPTE division are all refused. |
| Save round trip | Chunk -> RLE -> chunk byte-identical; a 64-chunk region written and fully read back; a corrupted directory copy A falls back to B and reports the chunk *absent*, never corrupt; compaction preserves every chunk. |
| Tree felling | Part F. |
| Registry invariants | `BLK_COUNT < 256`; every block has materials; every item has a name and `stack_max >= 1`; inventory index maths at every boundary. |
| Save slots | Eight slots told apart without opening them: a world, an empty slot, one from a NEWER major (refused, never offered as free), one that is damaged, and a legacy world adopted into the first free slot. Deleting frees the slot, directory and all. |
| The data directory | `datadir_adopt` moves a pre-D-80 install directory across, never over an existing entry, leaves a clash reported rather than merged, and is a no-op on the second start. The app's own shipped files are not touched. |
| The rename from CraftMiner | A card exactly as CraftMiner left it: `/sd/craftminer` with two worlds, the bench world, a negative region coordinate and a replay. After one start every byte is readable under the new name, nothing is left under the old, and a second start is silent. Then `datadir_retire` **refuses** while a world is still in the old install directory, refuses a path that is or contains the live one, and only once the world is gone deletes the directory, shipped files and all (D-91, D-92). |

`make scenecheck` (`tools/scenecheck.c` on `synthengine3D/host/se_host_stub.c`)
is the **budget** test, so a cap overflow is never discovered on the badge. It
drives a canned camera path through a generated world with the real
`chunk_render.c` + `mesh_render.c` + `voxel_mesh.c` and asserts: triangle counts
under the caps at each view distance (fail over, warn at 90%); nothing crosses
`RENDER_NEAR_CLIP_Z`; `outside_view()` really culls; the per-chunk AABB really
bounds its geometry.

The caps are grepped straight out of `CMakeLists.txt`, as the showreel does
(`Makefile:327`), so the checker can never test against different caps than the
app builds with:

```make
ENGINE_DEFS := $(shell sed -n 's/^add_compile_definitions(\(SE_[A-Z_]*=[0-9]*\))/-D\1/p' CMakeLists.txt)
check: worldcheck scenecheck meshcheck hostpurity
```

`make build` runs `make check` first, so a broken invariant stops the build.

---

## Part G: render budget

Measured baseline (quarter resolution, textured walk): rasterize 40 ms + submit
14-16 ms + prepare 4 ms + PPA wait 6.5 ms = about 64 ms, 15.5 fps. Target 33-50 ms.

### G1 — remove the submit overhead (the 14-16 ms)

**(a) `mesh_submit_world()`.** Chunk meshes are already world-space and their
xform is identity (F-02), yet `mesh_submit` runs a full `xform_apply` per vertex
through a PSRAM scratch array. A variant reading `m->v[]` directly removes tens
of thousands of transforms and hundreds of KiB of PSRAM write-then-read per frame.

**(b) Direction-grouped back-face culling.** `tri_faces_point()` is a cross
product, a centroid and a dot per triangle; greedy-mesher faces are axis-aligned,
so the answer is a sign test. `mesh_tri_t` has a **free pad byte** (F-01), so a
`uint8_t dir` costs nothing, the mesher's `emit_f()` already knows it, and
triangles sort into six contiguous runs at build time. Submission becomes six
loops with one compare per triangle.

Expected: submit **14-16 ms -> 3-5 ms**. Both changes are build-time and
host-testable.

### G2 — the LOD ladder, driven by the graphics menu

| View distance | fancy | tex | coarse | draw | fog0/fog1 | chunks |
|---|---|---|---|---|---|---|
| Near | 8 | 14 | 24 | 40 | 18 / 44 | ~25 |
| **Medium (default)** | 12 | 20 | 32 | 56 | 24 / 60 | ~49 |
| Far | 12 | 20 | 40 | 72 | 30 / 78 | ~81 |

`vox_view_t` is reused verbatim. Mesh residency follows the ladder by rule rather
than by LRU, so the ~3.2 MB is predictable. `outside_view()` gains the per-chunk
`bottom` as well as `top`: a 64-tall AABB culls badly along the horizon, a
terrain-hugging one does not.

### G3 — settings and engine flags

- **Textures on/off**: off swaps in the flat `mean_argb` material table, which
  `voxel_render.c` already builds for its fog path. 3-4x cheaper fill.
- **Render scale**: `scene_set_render_scale(2)` default; Full offered, labelled slow.
- `scene_set_options({.frustum_cull = true, .depth_order = true})` always.
- **Stay on `SE_RENDER_ZBUFFER`.** It is now the only built-in: raycast was
  2.5-5x slower on voxels and was removed (D-88), and banded rendering was
  built, measured over the bench flight and removed as well (D-90, G6).
- `SE_SCENE_TRI_CAP` stays 4096; **`SE_SCENE_TEXTURED_TRI_CAP` -> 2048**
  (~70 KiB PSRAM, no per-frame cost). The showreel peaked at 1785 textured with a
  showreel camera; a player facing a forest will pass 1024. `scenecheck` guards it.
- Use the `scene_prepare()` / `scene_rasterize()` split around the PPA backdrop
  so cull and sort overlap the hardware fills.

### G4 — the projected budget, and the risk

| Phase | Target (quarter, textured, Medium) |
|---|---|
| backdrop (PPA) | 0 CPU, overlapped |
| submit (G1) | 3-5 ms |
| prepare | 3-4 ms |
| rasterize | 25-30 ms |
| PPA wait + upscale | 6.5 ms |
| tick + entities | 1-2 ms |
| HUD | 1-2 ms |
| **total** | **40-50 ms, 20-25 fps** |

**Risk, with evidence:** 40 ms rasterize was measured on a showreel camera; a
player under a canopy measured 46 ms. G1 buys back ~11 ms of a ~64 ms frame,
landing at 20-25 fps, not 30. If the device reports under 18 fps the levers, in
order, are: `tex_dist` 20 -> 16; drop FANCY leaves (see-through canopies were 58%
of near triangles, showreel F-36); default textures off. **Do not reach for the
raycaster.** Measure at step 2.4, not at the end.

### G5 — what step 2.4 actually measured (F-31..F-34)

**16.0 fps**, quarter resolution, near view distance, textures on. The
projection above was optimistic, and the phase split says why:

| phase | ms | whose |
|---|---:|---|
| rasterize | 37-46 | **engine** — software fill at ~5 Mpx/s |
| submit | 15-16 | **game** — ~4.4 us per triangle, and far too many are sent |
| PPA wait + upscale | 6.0 | engine, unavoidable at quarter resolution |
| engine prepare | 4.5 | game — cull + sort, proportional to what is submitted |
| sky fill | 0.9 | game |
| residual | 1.3 | — |

The split matters because it says where work pays:

- **About 21 ms is the game's**, and most of it is waste: 68% of every chunk's
  mesh is cave walls (F-33), so ~7000 triangles are submitted to draw ~1000.
  Vertical render sections (D-34) are the fix and they touch no engine code.
  **Read F-35 with this**: sectioning is done and measured, and it is worth
  about 8%, not the 2.6x F-33 implied. The 68% was measured from one chunk, and
  that chunk was sea floor.
- **About 43 ms is the engine's**, and it is the documented cost of a software
  rasteriser, not a defect. The game chooses the pixel count -- view distance,
  render scale, textured against flat -- but not the rate per pixel.

**The engine's rasteriser is plain scalar C** (checked: no PIE/SIMD anywhere in
`src/se_scene.c`; the only SIMD in the engine is minimp3's x86/ARM paths, which
are explicitly disabled). The ESP32-P4 has a 128-bit SIMD unit suited to exactly
what a span loop does. So 5 Mpx/s is this implementation's speed, **not a
hardware ceiling** -- but how much headroom there is has not been profiled and
should not be guessed at.

Two engine-side routes exist if the game-side work is not enough, and both are
**stop and ask** under the standing rule, because the engine is shared:

- `se_renderer_register` -- the engine offers a documented seam for a custom
  rasteriser with no change to any `scene_tri` call site. A voxel-specialised
  one could exploit every face being axis-aligned.
- SIMD the existing span loops, which would help the engine's other users too.

Order of work: game side first. It is the larger single win, it carries no
engine risk, and it makes any later engine measurement cleaner by removing
geometry that should never have been submitted.

### G6 — page flipping, and the rasteriser band by band (2026-09-24)

The engine-side route G5 left open, taken with the user (D-87, D-88, D-89).
Three changes, all in SynthEngine3D 2.2, and one of them needs graceloader
2.6.0.

**(a) The present flips pages instead of copying (D-87, step 36).** The
engine used to draw into two buffers of its own and copy each finished frame,
768 KB, into the display driver's buffer on DMA2D, then wait on the panel's
tearing-effect line. It now draws straight into the driver's own buffers,
**three** of them, and a present only selects the finished one for the next
refresh. The refresh signal comes from graceloader 2.6.0's
`graceloader_display_register_callbacks()`, because the DSI driver only
accepts IRAM callbacks and our code is in PSRAM. Three, not two: this game
runs at 11-17 fps, and with two buffers every frame would wait for the
refresh that frees the other one -- half a 60 Hz refresh on average, ~8 ms,
about 10% of our frame (F-88). With three, a game slower than the refresh
never waits. PSRAM is unchanged (three buffers before as well).

**(b) The raycast renderer is gone (D-88, step 37).** It never won (G3), and
every renderer change had to be made twice.

**(c) `SE_RENDER_BANDED` (D-89, step 38).** The z-buffer renderer's passes,
run one vertical band of `SE_SCENE_BAND_W` = 32 logical columns at a time. A
logical column is one raw row of the rotated framebuffer, so a band is one
contiguous block: it is copied into a colour buffer in internal SRAM (with the
backdrop), drawn against a plain 16-bit depth buffer there, and copied back.
The per-pixel work never touches PSRAM; bands with nothing in them are
skipped. 60 KB of internal SRAM on first use; the band spans (~23 KB with our
list caps) are in PSRAM. **Same pixels as the z-buffer**, checked on the host
(F-90). Selected per run with the `_banded` scene suffix; `_fullres` forces
full resolution for the run, whatever settings.txt says.

What to expect, honestly: at **quarter** resolution this game already has its
depth test in SRAM (`SE_SCENE_DEPTH16_INTERNAL`, step 25, F-67), which was the
bigger half of what banding removes. What is left there is the colour writes
into the 190 KB half-size layer, which misses a 128 KB cache. At **full**
resolution banding removes both, the stamped 1.5 MB depth plane included. So
full resolution is where it should show first, and where it is measured
first.

**The tests it needs, in order:**

1. **Install graceloader 2.6.0** on the badge. Start an app built against the
   OLD engine (another grace app, or last week's SynthMiner): it must run as
   it did -- that is 2.6.0's compatibility promise (old apps on a new loader).
2. **The flip, by eye and under load.** Play: no tearing when turning fast.
   Save settings and the world while playing (flash writes switch the cache
   off, and the app's refresh callback is skipped then): the display must not
   glitch, and the frame may stall only for the write itself.
3. **Present cost, measured.** `PROF_BLIT` / `PROF_VSYNC` are in every PERF
   record but have never been fed, so they read 0.0 (F-88). Feed them from
   `se_present_stats()` (the showreel does) before comparing anything, so the
   flip's cache write-back is on the record rather than hidden in the loop.
4. **Same image.** `shots` of `flight` and `flight_banded` at the same `ms=`,
   and of `flight_fullres` and `flight_fullres_banded`: the framebuffer
   hashes must be equal pairwise. A difference is a bug in the banded
   renderer, not a tolerance.
5. **Speed.** `perf` at `secs=20` on `flight_fullres` against
   `flight_fullres_banded` first, then `flight` against `flight_banded`, then
   the replay walk (`replay_near` / `replay_far` and their `_banded`). Compare
   `rast`, fps, and the tri/ttri pixel and span counts (which must match
   between the two, being the same image).
6. **SRAM.** The log line `banded renderer: 32-column bands, 2 x 30KB
   internal`, and `sram` / `sram_big` in the PERF records, with and without
   banding. If the band buffers do not fit, the renderer says so once and
   draws with the z-buffer -- a run that "is no faster" may simply not be
   banded.
7. **Band width.** If banding wins, try `SE_SCENE_BAND_W` 16 and 64: narrower
   sets up spanning triangles more often, wider costs SRAM.

**The decision rule (D-89) said:** if banded is faster at full resolution and
not slower at quarter resolution here, it becomes the default and the
z-buffer rasteriser is removed; if it loses, it is removed. Either way one
renderer is left.

### G6 — what the measurements said, and what was decided (2026-09-25)

**None of the runs above were the ones that decided it**, because the scene
they were to be run on could not measure a renderer at all: `flight` opens a
SCRATCH world, so it generates its terrain from noise on every run (F-91).
Steps 4-7 were run on a new persisted bench world instead (step 41,
`game/benchpath.h`): one fixed path, seed 1030, 240 blocks due +z from the
origin, forty seconds at the flight's own 6 blocks a second, crossing all
five biomes with 23 blocks of relief and no step bigger than one block.

| | fps | rast mean | rast max | frame max |
|---|---|---|---|---|
| Quarter res, z-buffer | **20.16** | **27.59 ms** | 76.30 | 117.28 |
| Quarter res, banded (32-wide) | 18.13 | 29.72 ms | 75.35 | 117.88 |
| Quarter res, z-buffer, no SRAM depth plane | 16.21 | 40.11 ms | 117.26 | 157.57 |
| Full res, z-buffer | 5.70 | 148.18 ms | 410.05 | 453.44 |
| Full res, banded | **8.08** | **92.06 ms** | 242.88 | 289.17 |

**Banded is 1.61x faster at full resolution and 8% slower at quarter.** Full
resolution is unplayable either way -- 5.70 fps against 8.08 -- so the only
number that decides anything is the quarter-resolution one, and there banded
loses. The split is exactly what (c) predicted: at quarter resolution
`SE_SCENE_DEPTH16_INTERNAL` already puts the depth test in SRAM, which is the
larger half of what banding buys, so what is left is the colour writes
against the cost of setting a triangle up once per band it spans.

**Band width could not rescue it (test 7).** `SE_SCENE_BAND_W=64` needs two
60 KB contiguous blocks of internal SRAM; the largest free block is 37-38 KB
**with or without the 188 KB depth plane freed**, so the renderer logged its
fallback and drew as the z-buffer -- twice, and the second run is only in the
table above because the fallback is what produced the "no SRAM depth plane"
row. 32 columns is the widest this hardware allows, so the 2.13 ms gap is not
tunable. Test 6's warning earned its place: **both** fallback runs looked
like "no faster", and only the `sram` line and the log said why.

That same row is the other half of the argument: the 188 KB depth plane is
worth 27.59 ms against 40.11 ms to the z-buffer, a 31% saving. Sixty KB of
band buffers is a far worse use of the same scarce SRAM, and internal SRAM is
what everything else here will want next.

**Test 4 (same image) could not be run, and that is our fault, not the
renderer's (F-92).** The `shots` hashes of `bench` and `bench_banded`
differed at all five moments -- but so did two runs of `bench` against each
other, and the triangle counts handed to the rasteriser differed between
runs, so the two renderers were never asked to draw the same thing. The
engine's own host check (1000 random scenes, and it catches a deliberately
broken copy) remains the only evidence on pixel equality, and it is good
evidence; our device harness simply cannot confirm it yet.

**Decided (D-90): the banded renderer is removed.** It won where nobody
plays, lost where everybody does, could not be tuned, and cost 60 KB. Engine
2.2 was never released, so no version was spent on it: it was added,
measured and removed inside one unreleased version. **What stays** is the
raster target -- the raster passes draw through one struct instead of the
frame-level buffers -- because that is what (d) would need, and it costs
nothing to keep.

**(d) is therefore not happening** on the back of banding. If the second core
is ever used for pixels it needs a different way in; the notes below are kept
for whoever looks at that.

**(d) Bands on both cores -- designed, not built (step 40).** Only if (c)
wins. The single-core version was written for it: every raster pass draws
through one raster target (colour, depth, index offset, clip rectangle, fill
counters), and a band reads only the lists and spans, which are read-only
after `scene_prepare()`. What remains:

- the target becomes a parameter of the raster passes instead of one
  `static`, and the fill counters are summed per worker;
- a worker task on core 1 that takes bands from a shared counter (one atomic
  increment per band), with its own target and band buffers (another 60 KB of
  internal SRAM -- the 188 KB of (c)'s last point pays for it);
- **its priority goes below the chunk worker**, e.g. `configMAX_PRIORITIES - 7`
  (Part K has -6): chunk streaming must never wait for pixels, holes at the
  edge of the world are worse than a lower frame rate. Core 0 draws bands
  itself and never waits for core 1 beyond the band it is on, so the frame is
  never slower than single-core;
- the chunk worker spends much of its time blocked on the SD card, which is
  exactly when core 1 would help;
- the risk is the PSRAM bus, which the chunk worker's meshing and SD
  transfers share with the band copies: measure with a streaming walk
  (`replay_far`), not only a still view.

---

## Part X: the Far Lands

**Redesigned 2026-09-22 by the user (D-78).** The first design -- a
west-only zone at x = -100000 behind a 4096-block ramp, with the look
imitated by hand (a biased wall, y quantised every 12 blocks, an 8:1 smear)
-- is dropped. What replaces it:

- **Near.** The edge is at **x = -2048**: about 8 minutes' walk west of
  spawn at walking speed (`PL_WALK` 0.215 blocks a tick, 20 ticks a second,
  4.3 blocks a second), and on a chunk boundary. Every column **west of**
  it (x < -2048, chunk x -129 and beyond) is Far Lands; chunk -128 is the
  last ordinary one.
- **Changeable later, per world.** The edge is a field of the world,
  `farlands_x` in level.smw, written when a world is first created or
  opened by a build that knows it, from `FARLANDS_X_DEFAULT`. Changing the
  default changes NEW worlds only: an existing world keeps its edge, so its
  generated chunks and its ungenerated ones always agree and no seam is
  ever cut through a world someone is playing (D-78). It must be a multiple
  of 16. A world first opened before this existed has no such chunks west
  of it unless its player walked there; any that were generated stay as
  they are, ordinary terrain inside the Far Lands, like any saved chunk.
- **Sudden.** No ramp. East of the edge is ordinary terrain; from the edge
  on, every chunk is Far Lands. A cliff of jumbled terrain, as Kurt found it.
- **The real thing.** As close as possible to the Edge Far Lands of
  **Minecraft Beta 1.7.3** (the version of KurtJMac's *Far Lands or Bust*).

### Why they looked like that (minecraft.wiki, "Far Lands (Java Edition)")

Beta's terrain is a 3D density field: two 16-octave Perlin noises ("low"
and "high") blended by an 8-octave "selector" noise, minus a height
falloff, sampled every 4 blocks across and 8 up (5 x 17 x 5 samples a
chunk) and interpolated in between. Positive is solid.

Each Perlin octave casts its coordinate to a 32-bit int. The finest octave
of low and high advances 171.103 a block, which passes 2^31 at 12,550,824:
beyond it, Java's cast **saturates** at 2^31-1, so the "fraction" left over
is no longer 0..1 but enormous (about 10^11 one block in on the positive
side, 10^49 on the negative), and the smoothing polynomial EXTRAPOLATES it.
That octave's output then "completely dwarfs all other terms that would
normally give the terrain its shape" -- the height falloff included -- so:

- **the wall** runs from the bottom of the world to the top (Beta: y 127);
- **the holes** are long tunnels **perpendicular to the edge**: the
  overflowed axis always hits the same noise values, so the pattern does
  not change along it ("long unchanging tunnels", the "Swiss cheese wall");
- **water**: every air cell below sea level is flooded (about 23% of the
  Edge Far Lands is water; 36% stone, 25% air, 10% dirt and grass);
- **surface**: grass on top, dirt under it, sand and gravel near sea
  level; ordinary caves still carve it, "limited and small"; trees only
  high up, where there is sky;
- the edge starts three blocks early (12,550,821), because the 4-block
  sample spacing interpolates the overflow outwards.

### How we do it: run Beta's maths, broken the same way

Not an imitation. `world/farlands.{c,h}` (pure) **ports the Beta 1.7.3
density generator** -- the octave Perlin noise with its permutation
tables, the low / high / selector / depth noises, the 5 x 17 x 5 sampling,
the top slide and the interpolation -- in doubles, like Java, and feeds it
coordinates **past the overflow**:

    beta_x = x - FARLANDS_X - 12550821     (x <= FARLANDS_X, so beta_x <= -12550821)
    beta_z = z

so our edge IS Beta's west edge. Two things must be done by hand, because
C is not Java:

- **the saturating cast.** A C cast of an out-of-range double to int is
  undefined behaviour, not saturation. A `java_d2i()` that clamps to
  INT32_MIN / INT32_MAX exactly as the JVM does is the one line the whole
  effect depends on; the host tests pin it.
- **overflowing int arithmetic** in the noise (Java wraps) is done in
  uint32_t and cast back.

The permutation tables are seeded with **`java.util.Random`'s 48-bit LCG**
(a dozen lines), consuming it in Beta's order. That costs nothing and
makes the wall a given seed produces the wall Beta produced for that seed,
not just one like it.

**Fitting it into our world** (64 high, sea level 24, where Beta was 128
and 64):

- **Height: squashed 2:1.** The 17 vertical samples are spread every 4
  blocks instead of 8, so the whole Beta column -- wall top, tunnels and
  all -- fits in 64. Tunnels come out half as tall; the proportions of the
  face stay.
- **Water**: air below OUR sea level (24) floods. Beta's sea level was
  half-way up its world, ours is 3/8, so a little less of the face is water.
- **Surface**: Beta's own surface pass (grass on top, dirt below, sand and
  gravel beaches near sea level, bedrock at the bottom), run on the
  full-height Beta column before it is fitted into ours. **Bedrock and
  gravel** are new blocks for this (D-79); ordinary terrain does not use
  them yet.
- **Per chunk, not per column.** A chunk is either ordinary or Far Lands
  (the edge is chunk-aligned), so `worldgen_chunk` picks one generator;
  trees from the ordinary side may still lean over the edge.
- **Left out of the port** (for now): **ore, and therefore veins** -- the
  ordinary generator grew veins and cave mouths in step 32 and `farlands.c`
  was not touched, so there is not one block of coal or iron west of the
  edge and the Far Lands are mineable for stone alone. Beta's own decoration
  pass would have placed them, and porting that is where they come from;
  biomes -- every column is grass over
  dirt, and the temperature and humidity the density reads are constants,
  which in the Far Lands only touch the height falloff the overflow drowns;
  ice; and Beta's decoration pass -- its caves, ores, lakes, trees and
  flowers. Beta's sandstone becomes sand.
- Not reproduced: Beta's falling-sand lag, and the precision jitter (that
  needs millions of blocks, and D-01's floating origin prevents it anyway).
  Only the Edge Far Lands exist here -- one edge, west -- so no Corner Far
  Lands and no Farther Lands.

### Costs, and what to measure

- **Generation.** 5 x 17 x 5 = 425 density samples a chunk, each about
  40 octave evaluations (16 + 16 + 8) plus the 2D depth noise, and the
  surface pass's three noises over 256 columns. In doubles, which the
  badge does in software. **The fast path** takes low and high from their
  first octave only and skips the falloff's two noises: in the Far Lands
  the first octave is worth ~10^49 and the rest at most 2^15, so they
  cannot change a block -- and worldcheck proves it, block for block
  against the full port. **Measured on the badge (F-68): 134 ms a Far
  Lands chunk, against 33 ms for an ordinary one.** On the core-1 worker,
  so it costs loading time, not frame rate.
- **Meshes.** Expected to be heavy (a wall of holes) and measured to be
  light: the tunnels do not change along x, so nearly every face spans a
  chunk's width and the greedy mesher merges it whole -- about 200
  triangles a chunk. F-13's 40000-vertex assertion covers it in worldcheck.
- **The frame.** Walking up to the wall at Medium: 11.0 fps, no geometry
  dropped (F-68).

### Host tests (worldcheck)

- `java_d2i()` saturates at both ends and truncates toward zero inside;
  `java.util.Random` reproduces known Java outputs for a known seed.
- **The noise overflows where Beta's did**: the finest octave's integer
  coordinate is 2^31-1 (or INT32_MIN) at beta_x = -12550825, and ordinary
  one block east of the overflow point.
- **The cliff is sudden**: x = -2047 generates ordinary terrain (identical to
  worldgen without Far Lands); x <= -2051 has at least 90% of columns solid
  at y = 1 and near the top.
- **Tunnels run along x**: at fixed (y, z) inside the Far Lands, solidity
  stays the same for long runs of x; along z it changes often (>= 4 solid-air
  transitions per 256 blocks).
- **Composition** roughly as the wiki's (stone, air, water, grass/dirt), with
  generous bounds -- our squash and sea level shift it.
- **Asymmetry guard**: x = +2048 and z = +-2048 (and well beyond) are
  ordinary terrain -- the test against a sign bug turning the whole world
  into Far Lands.
- Determinism, and no chunk over 40000 vertices.

On the badge: `shots scene=farlands` looks at the wall; a `perf` run there
measures generation and frame cost.

### Signs at the edge

A few signs stand at random along the edge, on the ordinary side, facing
east -- towards whoever walks up to the wall -- with texts like **"Kurt
was here"** and **"Wolfie was here"** (Wolfie: Kurt's dog in the series).
Placed by the generator from the seed, so every world has its own and they
are the same every time.

For now signs are **generated only** (D-79): no sign item, nothing places
or edits one. The texts are a fixed list, so each is a ready-made texture
-- the planks with the lettering on it, drawn by `tools/make_textures.py`
-- and a sign's text follows from the world's seed and where it stands, so
there is nothing per sign to store. Player-written signs, when they come,
will need their text saved with the chunk (a tagged record, a format
addition).

---

## Part F: the tree-felling rule

**The bit** is `st` **bit 0, `ST_PLACED`**, set by `world_place_block()` for every
block a player places, never by worldgen. It round-trips through the RLE with the
rest of the state plane, so a felled tree behaves the same after a reload. It
also gives "placed leaves never decay" for free later.

**Two flags, because there are two questions.** `BF_FELLABLE` is tree
MATERIAL -- what a fell spreads through -- and `BF2_TRUNK` is what STARTS
one. Logs are both; leaves are only the first. They were a single flag
until 2026-09-28, which meant snipping one leaf brought the tree down
(D-97).

- not `BF2_TRUNK` -> normal single drop, leaves included;
- `BF2_TRUNK` **with** `ST_PLACED` -> single drop, no fell;
- `BF2_TRUNK` **without** `ST_PLACED` -> fell.

The fill below still spreads through `BF_FELLABLE`, so a canopy comes
down with its trunk exactly as before.

Connectivity, two phases:

1. **Logs.** BFS from the seed, **6-neighbour**, visiting only cells with
   `cy >= y`, block `BLK_LOG`, `ST_PLACED` clear. Same-`y` is allowed, so a 2x2
   trunk or a branch at the break height falls; below-`y` never is. 6-way rather
   than 26-way so a trunk merely touching a neighbour diagonally is not dragged in.
2. **Leaves.** BFS from every log found, **26-neighbour**, carrying a distance
   counter capped at `LEAF_DIST = 5` (Minecraft's decay radius), visiting only
   `BLK_LEAVES` with `ST_PLACED` clear and `cy >= y`. The cap stops a leaf bridge
   between two touching canopies from felling both trees.

`FELL_MAX = 512` cells; hitting it fells what was found and stops — a hard bound
against a grove of touching trunks. The visited set is an open-addressed hash of
1024 packed coordinates, 4 KiB of PSRAM owned by `interact.c` and allocated once.
The BFS goes through `world_block()`, so it spans chunks naturally; an unloaded
chunk stops expansion, which never fires in practice because a tick requires the
3x3 neighbourhood loaded (Part T rule 4) and a tree is at most ~7 blocks wide.
Removal is one batch, marking every touched chunk `CF_EDITED` and its LODs stale
(plus the neighbour where a cell sits on a border). **Drops are merged into
stacks** and spawned as one item entity — sixty item entities would cost more
frame time than the fell.

---

## Part C: crafting, containers and the recipe book

Designed with the user on 2026-09-23, before step 8 started, and almost every
line of it departs from Minecraft on purpose. Their brief opens with the
reason: *"Placing stuff in a grid like the original minecraft seems a bit
tedious and pointless (especially since we only have a keyboard but no mouse or
touch screen). Let's use a recipe book instead."*

The badge has a keyboard and no pointer. A 3x3 grid is a pointer interface
wearing a keyboard costume, and everything below follows from not building one.

### A recipe is a multiset, not a grid

```c
typedef struct { uint16_t item; uint8_t count; } ingredient_t;

typedef struct {
    uint16_t     out;      uint8_t out_n;
    uint8_t      station;  // RS_INVENTORY / RS_TABLE / RS_FURNACE
    uint8_t      flags;    // RF_REVERSIBLE
    ingredient_t in[RECIPE_IN_MAX];  uint8_t n_in;
} recipe_t;
```

No `w`, no `h`, no `in[9]`, no offsets, no shapeless flag. Part L sketched
`recipe_t (w, h, in[9], out, out_n, shapeless, station)` back when step 8 meant
a grid; with a recipe book the shape is not merely unused, it is **unobservable**
-- there is no surface on which a player could ever see or express it.

**This retires a rule this document has carried since the first week.** Part H
promised the host check proves *"every recipe resolves at every legal grid
offset; no two collide"*. In Minecraft a pickaxe and an axe are both three
material and two sticks, and only the shape separates them, so with a grid that
rule is load-bearing. The user asked for Minecraft's resource counts, so the
collision is real and arrives with the first tool.

It does not matter, because **nothing in SynthMiner ever infers a recipe from a
pile of ingredients.** Forward, the player names the recipe. Backward, the
disassembly bench starts from the item, which is equally known. The requirement
that replaces it is the one that is actually true: distinct output *and*
station per recipe, every ingredient and output exists, every reversible recipe
round-trips.

Quantities are Minecraft's, as asked.

### Discovery is stored as every item ever held

The user's rule: a recipe appears in the book once the player has picked up at
least one of the materials it needs. Stored **the other way round** -- a bit per
item, set by `inv_add`, and a recipe is known when any one of its ingredients'
bits is set.

Three reasons, and the first is the one that decides it:

1. A recipe bitmask would make **recipe numbering permanent**, the way block ids
   are permanent (D-74, `tools/ids.txt`), and every recipe inserted in a later
   build would need a migration or would silently shift what every existing
   player knows. Item names are already the thing saves key on -- inventories,
   dropped items and replays all store them.
2. A recipe added by a later build is then correctly **already known** to a
   player who has handled its ingredients, rather than locked behind materials
   they have had in a chest for a month.
3. It is smaller, and it is a more meaningful thing to have in a save file.

Saved in `level.smw` as a list of item names, beside the inventory.

### Three stations, and the furnace has three slots

`RS_INVENTORY` is the short list the user specified -- planks, sticks, torches,
a crafting table -- reachable from the Tab screen anywhere. `RS_TABLE` is
everything else. `RS_FURNACE` is smelting.

**The furnace was designed twice.** The first version here was a recipe book
filtered to smelting: pick *Iron ingot*, it queues the work, walk away. The
user replaced it the same day with the real thing -- *"The furnace has three
slots (input, output, fuel). When selecting the output slot, the item(s) go
into our inventory, for input and fuel slots the inventory pops up so we can
select an item stack to put into that slot."*

They were right, and the reason is worth keeping: a recipe book answers *"what
can I make"*, and that is not the question anybody has in front of a furnace.
The question there is *"what is in it and what is it doing"*, and three rows
answer it at a glance where a filtered book never could.

**The picker is what makes it work without a pointer.** Nothing is ever
dragged: selecting Input or Fuel opens a list of the player's own stacks,
narrowed to what fits the slot, with a right-hand column saying what each one
would BECOME (*makes Glass*) or how far it would go (*smelts 8*). That column
is a thing a mouse-and-grid furnace cannot tell you at all, which makes this
better than the interface it replaces rather than a substitute for it.

**Fuel is anything that burns**, as asked -- logs, planks, sticks, wooden tools,
and coal. It is a column in the ITEM table (`item_def_t.fuel`, in ticks), so a
new wooden thing brings its burn time with it instead of needing a line in the
furnace.

**Wood becomes coal, not charcoal** (the user's simplification): one fewer item
that burns exactly like another one.

### Reversal is one bit, not a special case

*"Doesn't work for basic resources like iron ingots turning into ore or sticks
turning into planks"* is `RF_REVERSIBLE`, a column in the table. Tools, the
crafting table, the chest, the furnace, the disassembly bench and the trashcan
carry it. Log-to-planks, planks-to-sticks, torches and every smelt do not.

A worn tool returns its **full** ingredient list (the user's call, asked
explicitly). Salvage-and-recraft is therefore a repair that costs the one-time
coal in the bench's own recipe, and that is the intended price.

### Lazy time: the furnace and the trashcan never tick

The user's rule for the trashcan -- *"Items in it expire (get deleted) after 10
in-game minutes (calculated the next time we open it)"* -- is the right
mechanism for the furnace as well. Both do their arithmetic from
`now - last_touched` **when opened**, and store nothing but that stamp.

No per-tick furnace list, no catch-up pass, no work at all for a furnace in a
chunk nobody has visited -- and it is automatically correct across a chunk being
evicted and reloaded, a world being closed for a week, and the clock being
stepped by the N key. Crop growth in step 9 uses the same trick, and the four food
machines of steps 9 to 11 are its next customers (Part A).

Ten in-game minutes is **12000 ticks of playing** (the user's call: not the
in-world clock, where a minute is a sixtieth of a 20-minute day and ten of them
would be three seconds).

### Containers need the block-entity section, which has never been written

`SECTION_BLOCK_ENTITIES` has been reserved in the chunk format since Part W was
written and **nothing has ever put a byte in it**: `region.c` calls
`chunk_encode(c, NULL, 0, ...)`. A chest is what finally needs it, and so
`world/blockent.{c,h}` is the real cost of step 8.

A **fixed global pool keyed by world position**, not a list allocated per chunk,
because the decode runs on the core-1 worker and Part K's contract is that the
worker does not allocate. A chunk's save walks the pool for cells inside it. The
pool is bounded, and placing a container when it is full fails with a message
rather than losing one quietly.

Steps 9 to 13 all land on the same machinery: a crop's stage fits in the
state byte, but a cow does not -- and neither does the time a crop last grew,
which is why step 9 buys a per-chunk stamp (Part A).

### The search box, on a keyboard that has one alphabet

The user's catch, and it would have broken the feature in 25 of the 32
languages: **the Tanmatsu has one fixed QWERTY**, so a player reading
*Кирка* cannot type К -- and it is not only the non-Latin scripts, since Turkish
*Kömür*, Polish *Łopata* and Czech *Dřevo* are equally untypable.

So the filter never matches what is on the screen. It matches a folded form of
it: `i18n/fold.{c,h}`, UTF-8 in, lowercase ASCII out, applied to **both** sides.
Accented Latin folds to its base letter, Cyrillic and Greek transliterate
(`Кирка` -> `kirka`, `щ` -> `shch`, `χ` -> `ch`). The item's **stable English
name matches too, always**, so `pick` works in every language -- which is also
the way out for someone who sets the language to Greek by accident and cannot
read their way back.

One letter, not a digraph: `ö` -> `o`, the user's call. A German would type
*loeffel* and a Turk *komur*, only one of those can win, and the single base
letter is right for more of the 32 than the digraph is.

**The check is what makes the table trustworthy**, and it is the same shape as
the font's "every character of every string is drawable" from 6.5: worldcheck
asserts the fold **covers the whole domain** -- every character occurring in any
item name in any of the 32 languages has an entry, and every entry lands in
ASCII. A translation using a letter the fold does not know fails `make check` on
the machine that builds it, not on a card where a recipe simply cannot be found.

### Tools: three tiers, and a wrong tool that is actually slow

*"Mining with the wrong tool is a lot slower."* It was not, and no hardness
number has to change to fix it: the right tool divided the time by
`tool_level + 1`, which is 2x for wood and 3x for stone -- a rounding error next
to swinging a fist. It becomes `2 x tool_level`: hand 1x, wood 2x, stone 4x,
iron 6x. A stone block is 7.5 seconds by hand and 1.25 with an iron pickaxe.

Iron ore **refuses to break** without a stone pickaxe (the user's call, over the
existing breaks-but-drops-nothing rule, which is Minecraft's). That needs
`BF_TOOL_REQUIRED` and it needs to *say so* on the HUD, because a swing that
does nothing at all and explains nothing reads as a bug -- which is the entire
reason Minecraft chose the other rule.

### What the host checks prove

| Check | What it proves |
|---|---|
| Recipe table | Distinct output + station; every ingredient and output exists; every reversible recipe round-trips to its own ingredient list; no recipe needs more than `RECIPE_IN_MAX` kinds. |
| Discovery | An empty set knows nothing; picking up one ingredient reveals exactly the recipes naming it; the set survives a save and a reload by name. |
| Auto-craft | A plan for a target the player cannot make directly is found, executed, and leaves the inventory exactly as the plan said; a target that is genuinely unreachable is refused without consuming anything; depth and cycles are bounded. |
| The fold | Covers the whole domain (above); folding is idempotent; a folded needle found in a folded haystack matches what a player would expect for a sample of each script. |
| Containers | A chest survives encode, evict, decode; the pool full is refused, not lost; a trashcan's expiry is computed from the stamp and not from when it was opened; a furnace's queue completes across a reload. |

---

## Part A: food, from a seed to a pizza

The user re-imagined this whole chain on 2026-09-29 rather than copying
Minecraft's, and gave it in one message. Four of the differences are not
decoration -- they change what code has to exist:

- **no bone meal.** Fertiliser comes out of a **composter**, which also
  produces the **worms** fishing needs, so one machine feeds two
  systems (D-104).
- **cooking is not smelting.** The user: *"In Minecraft, preparing food
  happens either on the crafting table or in the furnace. That makes
  absolutely no sense."* Food is made on a **kitchen stove** that takes
  its ingredients out of a **chest standing next to it** (D-105).
- **farmland is wet or dry**, and a plant only goes into tilled soil
  within four blocks of water on its own level (D-106).
- **fishing comes before mobs**, the user's own reordering (D-109).

Three tiers of time carry all of it, and all three already exist:

| tier | mechanism | who uses it |
|---|---|---|
| sub-second | the wheel in `world/blockupdate.h` (D-98) | water, and later falling sand |
| minutes | `now - stamp`, read when opened (D-51, `game/furnace.h`) | the stove and the sausage maker, one in-game minute a go |
| a day | the same clock, a longer number | the composter and the cheese maker |

Crops are the awkward one and are dealt with below: a crop is a block,
not a block entity, so there is nowhere in a cell to write *when it last
grew*.

### The composter, and the worms (D-104)

A placeable block with the furnace's shape of screen: an input holding up
to a stack of compostable material -- leaves, food, flowers, seeds -- and
**two** output slots, one compost, one worms. One in-game day turns one
unit of material into one unit of compost, plus **0-2 worms**.

- Compostability is one column in the item table, not a list in the
  composter: `item_def_t.compost`. Food items are not blocks, so it
  cannot be a block flag.
- 0-2 worms has to come from the world's RNG, not `rand()` -- Part T:
  the same world opened twice must compost the same way, and a replay
  must reproduce it.
- A full stack is 64 in-game days, which is 21 hours of play. That is
  fine and is the point: it drips. The lazy clock means coming back
  after a long absence pays out in one go, and the input is the cap.
- Recipe: **not given by the user.** Proposed: 7 wooden planks, like the
  cheese maker, since both are open wooden boxes.

### Farmland is wet or dry, and water is four blocks away (D-106)

Tilled with a **hoe** -- *"2 sticks plus 2 material"*, so three of them,
wood, stone and iron -- from grass or dirt. The user's rule: *"The tilled
soil must be within 4 blocks of a water block on the same level"*, and
farmland is drawn **dark when wet, lighter when dry**.

That price lands the hoe exactly where it belongs in a table nobody had
to adjust for it: every tool in the game is 2 sticks plus its material,
**1 for a shovel, 2 for a hoe, 3 for a pickaxe or an axe** (step 8.2).
It is also Minecraft's own hoe recipe, which means nobody arriving from
that game has to learn it.

**Wetness is not watched, and it is not even checked while a crop
grows** -- the user's own refinement, and it is cheaper again than the
version this file first proposed:

> Only compute when a seed is planted or at least when the player tries
> to plant) or the block is tilled. If farmland is dry, crops can't be
> planted. (tilled unplanted soil can also be re-tilled to update its
> status)

So the water search runs **on a keypress and nowhere else**: tilling a
cell, re-tilling an empty one, and trying to plant. Dry soil **refuses
the seed** rather than accepting it and never growing, which is the
better failure by a distance -- a refusal with a reason is a thing a
player learns from, and a plot that silently never sprouts is a thing
they file as a bug.

Three consequences, all of them wanted:

- **The fluid scheduler needs nothing at all.** Not a wake path, not a
  four-block radius -- the one radius it cannot reach cheaply, its
  neighbourhood being a single cell by design -- and not a growth-time
  check either.
- **Draining the moat after planting does not kill the crop.** The check
  happened when the seed went in. Minecraft would wither it; this will
  not, and that is a fair trade for a farm that cannot be ruined by
  water being moved two chunks away.
- **What the colour shows is the last answer, not today's.** A dry plot
  beside water placed a minute ago stays light until something asks
  again, which is exactly why the user said re-tilling an empty plot
  updates it: the hoe is the refresh.

Cost: the cells at distance 1..4 on the same y, under a hundred reads of
an already resident chunk, **once per hoe swing or planting attempt**.

### Five crops, and where the first seed comes from (D-107)

| crop | first one found | planted in |
|---|---|---|
| Wheat | grass seeds, from tall grass (already in the game) | tilled soil |
| Potato | *"sometimes found in large grassy lands"* | tilled soil |
| Tomato | *"sometimes found in birch forrests"*, made into seeds at the crafting bench | tilled soil |
| Beans | *"sometimes found in normal forrests"* | tilled soil |
| Rice | *"sometimes found on the shore in water one block deep"* | one-deep water on sand |

*"Sometimes found"* is the user's own definition: **one or two plants
generated in one instance of that biome.** Worldgen already places
plants per biome from a `biome_def_t` column (step 33/35), so this is a
second, much rarer column, not new machinery. It also means a world
generated before this step has no potatoes in it until the player walks
into fresh plains -- which is how every block added since 8.4 has
behaved, and is not a migration.

A wild plant is **the crop block at its last growth stage**, so finding
one costs no extra block id.

**Rice is the interesting one.** It grows in water, and a cell holds one
block. Rather than a waterlogged bit, the rice block carries a flag that
makes `world/fluid.c` read it as **a full source at level 0** and the
mesher draw a water surface under the sprite. That is safe precisely
because of the user's own constraint: rice only grows in water one block
deep, so its water is always a source and there is never a level to
store in state bits that growth already owns.

### The stage of a crop is cheap; the time is not

`BF_CROP` and `growth_max` have been in `block_def_t` since step 0.3 and
nothing has used them: a stage lives in state bits 1..3, which is free.
What does not fit is *when it last grew* -- and the user's own lazy
catch-up -- the half of D-98 that nothing has used yet -- says a chunk
loaded after an hour away should show the hour.

So step 9 pays for **a per-chunk stamp**: a new chunk section (id 3),
holding the tick the chunk was last swept. The section list already
skips ids it does not know (`chunk_codec.h`), so old worlds read as
"stamp absent, treat as now" and gain one the first time they are
written -- and since step 49 a loaded chunk is already dirty, that is
immediately. No format version bump (D-30).

**One clock, crops of different speeds** (D-117). The stamp is the
ABSOLUTE tick of the last sweep, and growth counts the stage boundaries
that fall between two sweeps -- one stage every `grow_ticks` since the
world's clock began. That is what lets a single number serve wheat at a
day and rice at two, and it leaves no remainder to keep anywhere.

Two things make it honest about WHEN a plant was sown, which the clock
alone cannot know:

- a plant carries its own **phase** -- where in its cycle it sits, in
  sixteenths of a stage -- in four state bits that were free. Without
  it, every plant in a chunk would ripen on the same instant whenever
  it went in;
- **sowing sweeps the chunk to now first.** The window between two
  sweeps reaches into the past, so a fresh seed would otherwise be
  credited with time that passed before it existed. A host check
  measured one ripening in 15360 ticks instead of 24000 -- a whole free
  stage -- and that check stays.

Growth then runs on every crop cell when a chunk arrives, advancing it
by however long the chunk was away, and on the slow sweep while it is
resident (D-112).

### Animals, and a bucket that already knows how to hold things

**BUILT 2026-09-29, ahead of the stove** -- the user's reordering, for
the same reason fishing went ahead of mobs: *"Let's implement phase 11
(Animals) before Phase 10 (food, hunger, kitchen stove)."* So animals
are step 10 and the stove is step 11.

Pigs drop **raw pork**, cows drop **raw beef**, and a cow **used with a
bucket gives milk**. Milk is drunk for 1 hunger and no saturation, or
turned into cheese.

D-100 already settled the shape of this: a filled bucket is **its own
item id**, and buckets do not stack. `ITEM_BUCKET_MILK` is one more row
in `BUCKETS[]`, and `fred_build_bucket` already colours its contents
from the table, so the held model needs no code. This was written down
as a guess at lava and milk; it is the milk half being cashed in.

One thing did have to change: milk is **not a fluid in the world**.
There is no milk block and no reason for one, so its `fluid` is
`BLK_AIR` -- and "is this a bucket" had been a test on the contents,
which would have read a milk bucket as an empty one and dipped it in
the nearest lake. The family is now the table rather than the contents,
and the colour of what is inside is a column.

**Four questions the design never answered** were put to the user before
any of it was built, and the answers are D-118 to D-121:

| question | answer |
|---|---|
| chickens, when no dish uses one? | **leave them out for now** |
| what does breeding want? | **each animal its own food** |
| what tames a dog? | **a bone, a rare drop from the sausage maker** |
| where do animals come from? | **generated with the land** |

The bone is the best of those and was not on the list offered: it makes
the sausage maker the only source of the one creature that follows you
about, so a machine that looked like a side dish is now on the path to
a dog.

**And the fence** (D-122), asked for in the same breath as the step:
*"we will also need to be able to craft and place fences and fence gates
so we can manage the animals"*. A fence is a block and a half tall --
the first height that is neither walkable nor jumpable -- and a gate is
two block ids, open and shut. Without them an animal is something you
follow about rather than something you keep, which is the difference
between husbandry and hunting.

**And a voice each** (D-124), also asked for in the same breath: a moo,
an oink and a bark, synthesised like every other sound in this game,
two a tick and nothing past 26 blocks.

### Two machines that take a day, two that take a minute (D-105, D-108)

| block | recipe | fuel | in | out | time |
|---|---|---|---|---|---|
| Kitchen stove + its chest | 3 iron + 6 stone + 8 planks, **one item, two blocks** (D-110) | **yes** | its own chest half, plus a recipe selector | 1 slot | 1 in-game minute |
| Cheese maker | 7 wooden planks | no | 1 bucket of milk (**the bucket comes straight back**) | cheese | 1 in-game day |
| Sausage maker | 9 iron ingots | no | 1 pork + 1 flower of any colour, or 2 beans | 1 sausage, and **1 bone in 6** off the pork one | 1 in-game minute |
| Composter | 7 wooden planks | no | up to a stack of compostables | compost + 0-2 worms | 1 in-game day per unit |

The cheese maker *"looks like an open barrel (quadratic, not round)"*
with three appearances -- white with milk, yellow-orange with cheese,
empty -- which is a texture set, not three block ids: it is a block
entity, so its contents say which to draw.

**The composter and the cheese maker cost the same seven planks**, which
is legal here and would not be in Minecraft: a recipe is a multiset and
the player picks the row out of the book, so two rows with identical
ingredients are fine -- the wooden pickaxe and the wooden axe have been
proving it since step 8.2. Worth knowing before anyone "fixes" one of
the two prices to tell them apart.

The **stove is the one with a new idea in it.** It has a fuel slot, an
output slot and a recipe selector, and its ingredients come from *"a
chest from which it takes its raw resources"*. If the recipe is short an
ingredient, or there is no chest beside it at all, it says so -- the
user asked for *"an appropriate info message"*, and that matters here
for the same reason iron refusing a wooden pick needed a line on the HUD
(step 8.4): a machine that does nothing and says nothing reads as
broken.

Mechanically it is the furnace's record plus a recipe id. Its picker
should be **the crafting book's list**, searched the same way, not a new
widget (Part C already solved searching on a keyboard with one alphabet).

**The chest is not any chest -- it arrives with the stove** (D-110), the
user's own refinement, and it removes a question instead of answering
it. The recipe includes the chest's 8 planks, the item puts down **two
blocks** -- chest half on the left, stove on the right, as the player
sees it while placing -- and **the two store each other's coordinates**.
Breaking either takes both.

That link is what makes a row of stoves work at all. `chest | stove |
chest | stove` has adjacencies that are wrong three times in four, and
no rule that looks at neighbours can untangle it; a pointer needs no
untangling. It also means *"no chest next to the stove"* stops being
something a player can provoke, so the missing-ingredient message does
all the real work.

Two things follow. A pair is **two block ids and two `BE_MAX` records**,
not one of each. And a pair can **straddle a chunk border**, so breaking
one half while the other is not resident leaves an orphan -- dealt with
the way D-99 deals with fluids at a seam: each half checks its partner
when its chunk arrives, the half the player breaks drops the item and
its contents, and an orphan found at load drops **only** its contents
and vanishes, so nothing can be duplicated.

### Fishing, and what the reorder costs (D-109)

The user: *"i want to implement 'Fishing' before 'Mobs and Combat', so
switch the order of those two."* Done -- fishing is step 12, mobs are
step 13.

Bait is **worms, held in the inventory**, and it is **one worm per
cast** (the user) -- not per catch, so a cast that brings nothing up
still costs one. That is the number that makes the composter matter:
**worms are the throttle on fishing**, and a rod of three sticks is
not.

The catch is one of **sardines, salmon, shrimp**.

The rod is **three sticks for now**, and the user is explicit that this
is temporary: *"i tell you the final recipe of the fishing rod another
day when i have decided."* One row in a recipe table, in a game where a
recipe is a multiset and not a grid (Part C), so changing it later costs
nothing.

What they did decide is worth more than the recipe: **string will not
come from spiders.** That removes the dependency this section was
written to worry about. The reorder was never really at risk -- fishing
does not need step 13 for its tackle after all, because whatever string
turns out to be, it will not be a mob drop. The likely source is a
plant, and tall grass is already in the world doing one job.

The one thing to hold onto meanwhile: a rod bought for three sticks is
nearly free, so **the scarce thing in fishing is the worms**, not the
tackle -- and the final recipe should not change that.

### What a crop is worth, and how long it takes (D-117)

Every number here is the user's.

| crop | ripe yield | seeds back | seed to harvest |
|---|---|---|---|
| Wheat | 1-3 wheat | 1-2 | one in-game day |
| Tomato | 2-4 tomatoes | none (the crafting table: 1 fruit -> 2 seeds) | one in-game day |
| Potato | 1-3 potatoes | it is its own seed | two days |
| Beans | 1-3 beans | it is its own seed | two days |
| Rice | 1-3 rice | it is its own seed | two days |

An unripe crop gives back **one seed** and nothing else, so pulling a
sprout up by mistake costs the time and not the seed. Tall grass gives a
wheat seed about half the time, which is where a farm starts.

An in-game day is 20 minutes of playing (DAY_TICKS is 24000), so the
fast crops are a day's work and the slow ones are something to come back
to. **The count is rolled at every harvest**, not baked into the cell --
otherwise a given square would give the same number for ever.

### The food table

Everything here is the user's number. Raw, and drunk or eaten as found:

| food | hunger | saturation |
|---|---|---|
| Milk | 1 | 0 |
| Tomato | 1 | 0 |

Made by a machine that is not the stove:

| food | station | ingredients | hunger | saturation |
|---|---|---|---|---|
| Cheese | cheese maker | 1 bucket of milk | 2 | 2 |
| Pork sausage | sausage maker | 1 pork + 1 flower (any colour) | 2 | 2 |
| Fake sausage | sausage maker | 2 beans | 2 | 2 |

Cooked on the stove, one in-game minute each:

| dish | ingredients | hunger | saturation |
|---|---|---|---|
| Baked potato | 1 potato | 1 | 1 |
| Grilled tomatoes | 1 tomato | 2 | 0 |
| Baked beans | 1 bean | 2 | 0 |
| Bread | 3 wheat | 2 | 1 |
| Rice patty | 2 rice | 3 | 1 |
| Smoked salmon | 1 salmon | 2 | 4 |
| Grilled shrimp | 3 shrimp | 2 | 2 |
| Sashimi | 1 salmon + 1 rice | 4 | 0 |
| Pork and beans | 1 pork + 1 beans | 5 | 2 |
| Steak and potatoes | 1 beef + 1 potato | 5 | 2 |
| **Pizza** | 2 wheat + 1 sausage + 1 cheese + 2 shrimp | **10** | **8** |

**The fake sausage counts as a pizza's sausage** (the user), so the
vegetarian option is not a dead end one dish short of the best food in
the game. It was never going to make the pizza vegetarian -- there are
two shrimp in it -- so refusing would only have been a trap for someone
who had done everything right.

**Grilled tomatoes and baked beans are 1:1** (the user), one fruit in,
one dish out. This file guessed two and guessed wrong, on the reasoning
that a raw tomato at 1 hunger would be pointless beside a grilled one at
2 for the same tomato. That reasoning was backwards: doubling what a
tomato is worth **is what cooking is for**, and the raw option is the
one you eat with no stove and no fuel -- which is exactly the relation
Minecraft's raw and cooked meat have. Beans have no raw form at all, so
one bean for 2 hunger is the floor the rest of the table stands on.

Pizza is *"the superfood of this game"* and is priced like one: it needs
a crop, a fished animal, a cow and a pig, so it needs all four systems
working at once. Smoked salmon is the odd one and is right: 2 hunger but
**4 saturation**, more than the pizza gives per hunger point, so it is
the thing to carry when travelling rather than the thing to eat when
starving.

Hunger and saturation behave *"basically the same way as in Minecraft"*
(the user), which is the model D-08 already committed to: saturation
drains before hunger, hunger gates regeneration, and empty hunger
damages. The HUD has drawn both bars since 4.3 with nothing moving them.

`item_def_t` gains **two columns**, `hunger` and `saturation`, so a food
stays a table row.

### What it costs

- **Sixteen permanent block ids** (D-74, and a line each in
  `tools/ids.txt`): farmland wet and dry, five machine blocks -- the
  stove is two of them, D-110 -- six for the crops, because rice is two
  blocks tall and its upper half is an id of its own (D-115), and three
  for the fence and its gate, which is two ids because an open gate is
  not solid (D-122). Ids 31 up, the first spent since step 35; step 9
  took 31 to 39 and step 10 took **40 to 44**.
- **About thirty item ids**: three hoes, a rod, compost, worms, seeds
  for wheat and tomatoes, five harvests, pork, beef, milk, cheese, two
  sausages, a bone, three fish, eleven dishes. Step 9 spent twelve and
  step 10 **seven**; the item table stands at 33 non-block items.
- **Thirty-two languages.** Roughly 35 new labels plus four machine
  screens is the biggest string round since 8.5, and
  `check_label_widths()` (F-76) decides whether the stove's picker fits
  before a badge does.
- **The block-entity pool.** `BE_MAX` is 192 records across the resident
  ring, sized for chests and furnaces. A farm has a composter, a stove,
  a cheese maker and a sausage maker in one place, and the pool is
  shared: it may need raising, which is PSRAM and is measurable rather
  than guessable. Still 192 after step 10, and untested against a real
  farm.

- **A creature pool**, which step 10 added: `MOB_MAX` is 48 live animals
  across the resident ring, and it is what bounds the cost of the tick.
  A herd or two per chunk that has one, measured at 96 chunks in 576
  with a herd in them and 272 animals over a 384 x 384 block world.

- **The texture cache**, which is the one that has already bitten
  (F-120). It now has to hold every material AND an icon per item, the
  requirement is derived from the item table rather than counted, and
  both halves are checked -- one at compile time, one on the host.
- **A chunk section and a new tag**, for the stamp above.

### Open, and to be decided when it is built

Written down so they are decided on purpose rather than by whoever
types the code:

- **The rod's final recipe**, which the user will give another day.
  Three sticks until then, and **string does not come from spiders** --
  so it is not waiting on step 13 (D-109).
- **Where string does come from**, which that recipe will settle. Tall
  grass is the obvious candidate and already exists.
- **Whether a BEAN sausage can leave a bone.** It cannot, as built: a
  bone comes off the pig, and the user's words were "when feeding the
  sausage maker, a rare drop is a bone", which does not say which
  sausage. One line in `MAKERS[]` either way. It matters because it
  decides whether a vegetarian player can have a dog.
- **One in six**, the bone's rate, which is a guess and the only number
  in step 10 that is not the user's. It sets what a dog costs.
- **Chickens**, left out for now (D-118) and waiting for a dish that
  wants one.

---

## Part D: step-by-step plan with status tracking

**Status values:** `todo` / `in progress` / `done` / `blocked (why)` /
`skipped (why)`. The Notes column records the result, with links to findings
(F-n) and decisions (D-n).

| # | Step | Status | Notes |
|---|---|---|---|
| **0** | **Scaffolding (host only, no device)** | | |
| 0.1 | This document, with the status table, F-01..F-09 and D-01..D-19 | done | 2026-09-20 |
| 0.2 | Create the layout; lift `mesh`, `xform`, `mesh_render`, `camera`, `texcache`, `backdrop`, `horizon`, `voxel_mesh`, `voxel_sky`, `voxel_fx`, `textures/*.png`, `tools/{make_textures.py,meshcheck.c}`, with provenance in each header | done | 2026-09-20: 22 files carry a provenance line. Textures flattened to `textures/` (one app, not a reel with segments) and `make_textures.py` trimmed to SynthMiner's 20; regenerated **byte-identical** to the showreel's (F-14). `backdrop`/`horizon`/`voxel_sky`/`voxel_fx` are in the tree but not yet in `APP_SOURCES` — they wait for `world/chunk_render.h` in step 2. |
| 0.3 | `common/psram.h` seam; `world/blocks.c` registry; `voxel_mesh.c`'s `kind()`/`voxel_face_mat()` become `BLOCKS[]` lookups (`vox_grid_t` unchanged) | done | 2026-09-20: 17 blocks incl. `BLK_BARRIER`. The leaves-see-leaves / glass-hides-glass rule became the `BF_SEE_SELF` flag instead of a hard-coded id. **All eleven of the showreel's mesher cases pass with its exact triangle counts** (F-15), so the conversion is behaviour-preserving. `voxel_mesh.c`'s greedy mask moved to `sm_calloc` (F-12 closed). |
| 0.4 | `tools/worldcheck.c` skeleton, `make check`, the host-purity grep rule | done | 2026-09-20: `make check` = `hostpurity` + `meshcheck` + `worldcheck`, and `make build` **depends on it**. `worldcheck` has the registry section; the rest arrive with their milestones. `scenecheck` waits for `chunk_render.c` (step 2). |
| 0.5 | CMake: `SE_SCENE_TEXTURED_TRI_CAP=2048`, `SE_BINDINGS_MAX=24`; testkit sources in; `SCREENSHOT_DIR`; `metadata.json` gains the PNGs | done | 2026-09-20: both definitions sit before `add_subdirectory(synthengine3D)` and `make check` greps them back out with sed, so app and checker can never disagree. Testkit compiled in; `SCREENSHOT_DIR="/sd/apps/at.cavac.synthminer/test"`. `make install` uploads the 20 textures to `<app>/textures/`. |
| | **Accept:** `make check` green, `make build` clean, `make verify` passes | **done** | 2026-09-20: all three. `app.so` 25605 text / 43252 data / 15649 bss. |
| **1** | **World data, generation and persistence (host only)** | | |
| 1.1 | `chunk.{c,h}` ring store, `world_block/set/state`, `BLK_BARRIER`; **measure free PSRAM on the badge** and record it | done | 2026-09-20: measured on the badge (F-22). 28.1 MiB PSRAM free after the engine boots; the 8 MiB slab leaves 20.1 MiB. The residency radius of 6 is comfortable and could grow. Chunk generation measured at 56 ms (F-23).
| 1.2 | `worldgen.c`: heightmap, strata, water, ores, caves, trees, plants; cross-chunk decoration by neighbourhood iteration | done | 2026-09-20: two octave stacks (broad coast field + fine hills), soil depth, beaches, two-field cave worms, coal, trees on a jittered 5-block grid, flowers and tall grass. Measured over 327k columns: relief y17-38, **31.3% at or below sea level**, centred at y27 with sea level 24. Host-tested for determinism (including x=-100000 and z=1400) and load-order independence (F-17). |
| 1.3 | `chunk_codec.c` RLE; `region.c` header, dual directory, append, compaction | done | 2026-09-20: a generated chunk packs to **3962 bytes** from 32768 raw (8.3x), with a raw fallback so no input can fail to store. Region files host-tested for round trip, damage, **torn-write recovery** and compaction (F-25), then measured on the card (F-26). `world/vfs_compat.{c,h}` added for the calls graceloader does not export (D-27).
| 1.4 | `worldstore.c`: `level.smw` schema, `worlds.idx`, the `se_mp3.c` FatFs enumeration | done | 2026-09-20: worlds create / list / open / save / delete, slugs made FAT-safe and unique, player state round-tripped including a position out at x=-100000. Palette written and remap proven (F-29). `common/tags.{c,h}` added for D-30, tested both ways (F-28). Enumeration is a live FatFs directory scan; `worlds.idx` is **not** written yet -- the scan is the truth and the index is only an optimisation, so it waits until a listing is measured as slow.
| | **Accept:** `make worldcheck` passes determinism, cross-chunk equivalence, RLE and region round trips, torn-write recovery, compaction | | |
| **2** | **Streaming render on the device** | | |
| 2.1 | `chunk_worker.c`: task, queues, ownership contract, synchronous mode | done | 2026-09-20: core 1 at `configMAX_PRIORITIES-6`, 48-deep job and result queues, worker-owned 21 KiB mesher scratch (F-08 closed). Host-tested through the synchronous path (F-30). |
| 2.2 | `chunk_render.c`: streamed LOD cache, `outside_view` with `top`/`bottom`, the view table | done | 2026-09-20: ring-at-a-time nearest-first loading, eviction with hysteresis and save-before-drop, three LOD bands, fog-tinted flat palette cached per fog step. |
| 2.3 | G1: `mesh_tri_t.dir`, direction-grouped triangles, `mesh_submit_world()` | done | 2026-09-20: the free pad byte holds the face direction (F-01), so an axis-aligned back-face test is one compare. `meshcheck` proves every greedy face's direction matches its real normal and that plants carry none. Grouping was **skipped** — the per-triangle test is already cheap and submit time turned out to be elsewhere (F-32). |
| 2.4 | A free-flying debug camera over a streamed world | done | 2026-09-20: a circular flight, a pure function of the show clock. 16.0 fps measured (F-31, F-32, F-33) — **that figure is wrong, see F-36**: the camera was pointing at the sky and flying sideways. Corrected, the same build is **12.3 fps**. |
| 2.7 | The world reloading itself, reported from free flight | done | 2026-09-21: three causes (F-41). The far preset did not fit the chunk ring — two chunks per slot, evicting each other forever. A LOD change drew nothing until its mesh arrived. The result budget was still 2 a frame from before sectioning. Two of the three were mine, from D-34. |
| 2.6 | Where the frame time really goes; the engine at -O2 | done | 2026-09-21: the user asked for SIMD and for internal-SRAM textures. **Neither is the answer, and both were measured rather than assumed** (F-37, F-38, F-40). What was: `-O2` and inline rounding, worth **13.4 -> 15.3 fps** (F-39). Spans average **6 pixels**, so the cost is per-span setup, not per-pixel work. |
| 2.5 | Vertical render sections (D-34) and a hand-flown camera | done | 2026-09-21: `vox_grid_t.y0`; 4 sections a chunk, each culled and meshed on its own; meshes moved out of the static `chunk_t` into the PSRAM slab (**−35 KiB bss**). **12.6 → 13.6 fps** (F-35, two runs each). The mesh check proves the seam is exact by meshing a lump whole and in slices and comparing surface area and volume — and fails when the offset or the border is broken. Free flight (WASD / arrows / Space / Shift, T and V toggles) whenever no test is running (D-38). |
| | **Accept host:** `make scenecheck` no cap overflow at any view distance; `make meshcheck` `dir` matches every normal. **Accept device:** `make cycle TEST="perf scene=flyover secs=20"`; **submit must be under 6 ms** | | |
| **3** | **The player** | | |
| 3.1 | `physics.c`: swept AABB (0.6 x 1.8), gravity, jump, step-up | done | 2026-09-21: axis-at-a-time sweep in sub-steps of 0.25 so nothing tunnels at terminal velocity (3 blocks a tick, against a body 1.8 tall). Step height is **a whole block, not Minecraft's 0.6** (D-45). Host-tested: rests exactly on the floor, slides along walls, climbs a step and a staircase, refuses a 2-block wall, fits a 2-high gap and not a 1-high one, stops at the edge of the resident world. |
| 3.2 | `raycast.c`: DDA pick, block + face normal, reach 4.5 | done | 2026-09-21: Amanatides-Woo, reporting the face entered through and the cell in front of it. Host-tested against a **brute-force march over 576 directions**, every one agreeing on hit/miss and on which block. |
| 3.3 | `tick.c`: fixed step, interpolation, replay record/play | done (replay deferred) | 2026-09-21: fixed 20 Hz off the show clock, capped at 5 ticks a frame with the remainder forgiven rather than carried (a stutter must not spiral), position AND view angles interpolated. `tick_freeze` for D-26. **Replay record/play is not written**: it needs the input stream stored somewhere, which is step 5's save format, and the `shots` determinism it serves also needs synchronous chunk loading turned on for the test. Listed in block 5. |
| 3.4 | `input.c` + `look_source.c` + `controls.c` (se_bindings, the user's defaults) | done (menu in 6.1) | 2026-09-21: 21 actions through `se_bindings`, every one remappable and persisted; the defaults the user specified. Looking goes through `input_look()`, so a mouse replaces the cursor keys without touching a call site. The menu itself is step 6.1. |
| 3.5 | `interact.c`: break/place with `ST_PLACED`; `voxel_fx` retargeted; **tree felling** | done (`voxel_fx` deferred) | 2026-09-21: break and place on the key edge, `ST_PLACED` set on every placement, and **the logging rule** -- a placed log drops itself, a grown one fells the tree. Host-tested both ways, including that a tree 12 blocks away keeps all its logs and that the stump below the break survives. `voxel_fx` (crack overlay, particles) waits for block 4's break-progress timer, which is what it would animate. |
| 3.6 | Aiming: crosshair and the block highlight | done | 2026-09-21: added after the user reported aiming was guesswork (F-44). The crosshair is derived from `RENDER_HORIZON_Y`, **not** the middle of the screen — the two differ by 16 rows and the difference is an aiming error nobody would think to suspect. Verified by pulling the framebuffer off the badge. Found and fixed a picker bug on the way: `BLK_BARRIER` was reportable, so an unloaded chunk could be highlighted and offered for mining. |
| | **Accept host:** collision fuzz, DDA against brute force, felling tests | **done** | 2026-09-21: all in `make check`. Collision over hand-built fixtures (rest, terminal-velocity tunnelling, wall slide, step, staircase, 2-block wall, 2-high and 1-high gaps, world edge) plus the jump arc asserted in blocks and seconds; DDA against a brute-force march over 576 directions; felling both ways with the neighbouring tree and the stump checked. |
| | **Accept device:** `shots scene=replay_walk` gives **identical hashes across two runs** — the proof Part T works | **unblocked, not re-run** | 2026-09-28: no longer blocked. Both things F-45 named have since been delivered by step 5.8 — the input stream is stored (`replays/*.smr`, the start plus each tick's action mask and gyro turn) and the synchronous chunk mode is switched on, and 5.8 records three captures of one instant hashing identically. What has **not** been done is this test: two full runs of `shots scene=replay_walk` compared against each other on the badge. It is the proof Part T works and it is one command; it stays open rather than being marked done on the strength of a neighbouring result. |
| **4** | **Items** | | |
| 4.1 | `items.c`, `inventory.c`, merged-stack drops, `item_entity.c` | done (not persisted) | 2026-09-21: **item ids below `BLK_COUNT` ARE block ids** (D-53), so a stack of cobblestone needs no second table and the inventory can draw a block the block registry already describes. Stacking fills partial stacks before empty slots — host-tested, because the other order silently gives you five slots of three cobblestone. Drops spawn from the block table's new drop column, scatter deterministically, fall, and fly to the player. **Age is elapsed ticks** (D-51), despawn asserted at exactly 12000. **Not saved yet**: `SECTION_ENTITIES` is reserved and nothing writes it, so drops are lost on reload — block 5. |
| 4.2 | Tool durability; hotbar (F1-F6) and the inventory screen (Tab) | done | 2026-09-21: breaking is **held, not tapped** — `item_break_ticks()` of them, so hardness and the tool in hand finally mean something, and progress belongs to a cell so looking away abandons it. The right tool class only: a pickaxe does not dig dirt faster. Too soft a tool still breaks the block and drops nothing. One use of durability per **break**, so felling a tree is one swing of the axe, not forty. Tab opens the grid; F1-F6 swaps a stack onto the hotbar, which is the one operation without which everything past the sixth slot is unreachable. |
| 4.3 | `hud.c`: crosshair, hotbar, health, hunger | done | 2026-09-21: hotbar with counts and a wear bar, ten hearts, ten drumsticks, and a mining progress bar. **Tools are drawn as shapes, not colours** (D-54) — three stone tools as flat squares are three identical grey squares. Drawn through `se_direct565.h`, not PAX: that is the difference between **13.4 ms a frame and 0.8** (F-46). Health and hunger are displayed but nothing moves them yet; the systems are steps 10 (hunger) and 13 (damage). |
| | **Accept:** the item registry, stacking, durability and drops are host-tested; breaking yields what the block table says and only to a tool that qualifies | **done** | 2026-09-21: in `make check`. Tool speed by class, harvest qualification, partial-stack filling, a full inventory refusing the overflow, two differently-worn tools staying two, a wooden pickaxe lasting exactly its durability, drops appearing and being collected only after `ITEM_PICKUP_DELAY`, despawn at exactly 12000 ticks, felling dropping every block it takes, and a full entity pool refusing rather than corrupting. |
| **5** | **First playable — worlds on the SD card** | | |
| 5.1 | `screens.c`: title -> world list -> new world (name + seed, or rolled) -> play | done | 2026-09-21: the **title** is the showreel's idea rebuilt on a streamed world (D-58). 2026-09-22: **Play / Settings / Quit** along the bottom of it, under the word; Play opens the **eight save slots** (D-60), a used slot offers Play / Rename / Delete, an empty one the new-world form — name, and a seed that is a number, any text (hashed), or blank for random. All in `ui/menu.c`, drawn with the engine's list menu, which has no scrolling of its own, so long lists window round the cursor. |
| 5.2 | Save policy: chunk unload, pause-menu Save, quit. Never per tick | done | 2026-09-21: `save_world()` writes the player and every edited resident chunk; eviction already saved. 2026-09-22: saves on **opening the pause menu** (D-61), on its Save row, and on Save and quit. Nothing saves on a tick or a timer. |
| 5.10 | **Player position and inventory in the save** | done | 2026-09-22, asked for by the user. The inventory is stored **by item name** (D-62) — each slot's item, count and wear, plus the selected slot. The position is restored **exactly** (`player_place`), falling back to standing on the ground only if the body would not fit there; before this a returning player was always put on the surface, whatever cave they had left from. A `placed` flag tells a real position from a new world's spawn guess, with a rule for saves from before the flag (F-51). Host-tested: a worn pickaxe, a partial stack and the last slot round-trip; a position 11 blocks down comes back as 11. |
| 5.11 | **Adopt the pre-slots Testworld** | done | 2026-09-22, asked for by the user: people are already playing. The one world earlier builds kept, `worlds/flyover`, is moved by **one directory rename** into the first free slot and renamed *Testworld*. Nothing is copied, so nothing can be half-copied; a card that never had it is left alone, and a second start finds nothing to do. Host-tested against a hand-written level.smw in the old format, with a chunk in it: terrain, the placed block, the player's exact position and the seed all survive. **Not yet run on the badge** — it was unreachable when this was written. |
| 5.7 | **Entities in the save** | done | 2026-09-22: every dropped item goes into level.smw with the world (D-68) -- by item name, with count, wear, age and what is left of its pickup delay -- and comes back on opening. An item whose chunk is not loaded holds still (no falling, ageing or pickup), which keeps D-33's promise without per-chunk entity sections. Host-tested: a round trip, and an item in an unloaded chunk neither falling nor ageing. |
| 5.8 | **Replay record/play**, and synchronous chunks for the `shots` test | done | 2026-09-22: `game/replay.{c,h}`. R (a debug key, unless bound) records from where the player stands to `replays/last.smr`: the start (seed, clock, position, inventory) and each tick's action mask and gyro turn. The `replay` and `replay_third` test scenes play `replays/test.smr` (else `last.smr`) on a SCRATCH world of that seed, so no save is touched; under a test the replay runs exactly the ticks that belong to the set clock. Two ordering bugs found making it deterministic (F-59, F-60). Host-tested round trip; on the badge a scripted replay placed a torch at night and mined, photographed in both views. Earlier notes: | 2026-09-21: the synchronous half is done (D-59) — a `shots` run switches the worker inline and settles the world, so a shot photographs the world instead of the sky, and three captures of one instant now hash identically. **Replay record/play is still not written.** Carried in from 3.3, was blocked on F-45. A `shots` run SETS the clock rather than running it, so the chunks never stream and every screenshot is empty sky — the fix is the synchronous mode D-15 put there for exactly this. Until both exist, block 3's device accept line cannot be met and shot hashes cover the overlay but not the world. |
| 5.9 | Move `time_of_day` from the player record to the world (D-52) | done | 2026-09-22: `world_meta_t.time_of_day`, advanced one per simulation tick and never by the wall clock. A save from before reads the player's copy instead (host-tested with a hand-written old save). New worlds start at a morning. |
| 5.5 | **Pre-generate and save the spawn area on world creation**, behind a "Creating world" progress bar (D-25) | done | 2026-09-22: an `APP_LOADING` state generates a 60 ms slice a frame and draws "Creating world" / "Loading world" with the world's name and a bar, for the title at boot too. Under a deterministic test it loads in one step, and the frame that finishes carries straight on (F-60). |
| 5.6 | **The entering sequence** (D-26): physics frozen, 3x3 synchronous, play, then stream the rest | done | 2026-09-21: the tick is frozen until the chunk under the player is resident, and the spawn area is pre-generated before play starts. The "carry on streaming the rest while walking" half already worked. **2026-09-23: the gate itself.** Everything else had been built and `loading_step()` was still asking the wrong question -- it waited for `missing == 0`, the whole view distance, which is the one thing D-26 was written to avoid. Now `chunk_render_nine()` counts the nine chunks around the player and play starts at nine, with the rest streaming in behind them. Creation still waits for all of it (it pre-generates and SAVES the spawn area, 5.5), as do the title, the debug flight and a replay, and `devtest_deterministic()` forces the old behaviour so `shots` is untouched. The progress bar measures the nine it is waiting for rather than crawling across the view and jumping. |
| 5.3 | Pause menu; `f1_exits = false`; quitting saves first | done | 2026-09-22: Resume / Save / Settings / Save and quit to title. Opened by the Pause binding **or Esc, always** (D-63); Esc closes the inventory first if it is open. |
| 5.4 | `worldlist_ui.c` with index + FatFs rebuild; delete a world | done | 2026-09-22: the slot list reads each slot's level.smw when it opens (eight file opens, not per frame). Delete asks first with **No** under the cursor, and removes the directories as well as the files, so the slot is really free. `worlds.idx` is still unneeded (1.4). |
| | **Accept:** a scripted device test creates a world, edits 200 blocks across 3 chunks, saves, reloads, and reports whether every edit survived. **-> hand to the user** | **done** | 2026-09-22: the `savecheck` scene (`make cycle TEST="perf scene=savecheck secs=3"`), in a hidden world deleted afterwards. **200 of 200 edits survived** on the badge. The test kit gained `devtest_content_failed()` so a miss ends the test "bad". |
| **6** | **Settings** | | |
| 6.1 | Controls menu, the synthracer pattern, all actions rebindable | done | 2026-09-22: all 21 actions, plus Reset to defaults. Binding a key another action has **swaps** them, so no two actions share a key and none is left with none. The capture is the engine's `se_ui_capture_key`, fixed to take the cursor keys (F-50), and the key column is synthracer's `keybind_ui.c` with its key-cap icons (`ui/icons.c`), both ported with provenance headers. The main menu is a strip under the title (the user kept it); every other screen is an `se_ui` panel. The polled fallback for keyboards that send arrows as navigation events now follows the binding rather than the action, and the debug keys stand aside for any key a player has bound (D-64). |
| 6.4 | **Gyroscope look** | done | 2026-09-22, asked for by the user: a Controls checkbox, off by default. The **rate** gyroscope is added up frame by frame and handed to the look beside the cursor keys, one real degree per view degree, so both work at once (D-65). A resting gyroscope's offset is tracked rather than turned into a slow spin. Yaw sign as the diagram in `graceloader_imu.h` predicts; the **pitch sign had to be flipped**, reported by the user on the badge. |
| 6.2 | Graphics menu: textures, render scale, view distance; settings.txt | done | 2026-09-22: view distance (default near -- medium for a few hours, D-66 then D-76), textures, half / full resolution, in `settings.txt` on the SD card (D-67; NVS under `synthminer` at first, moved the same day). Replaces the T and V debug keys. Full resolution clears its own sky now, which it never had to while it was only the no-PPA fallback. |
| 6.3 | Audio and display via `se_hw.h` | done | 2026-09-22: device volume and the three brightnesses through `se_hw` (shared with the launcher); music and effects switches stored and wired to the mixer's gates, and labelled as waiting for block 14, since the game makes no sound yet. |
| 6.5 | **The UI in six languages** (D-81) | done | 2026-09-23, asked for by the user. `lang/*.txt` (English the reference, plus German, Dutch, Flemish, French, Bulgarian), baked by `tools/make_lang.py` into `main/i18n/strings_gen.c`: 126 strings x 6, a lookup is an array index. Language is the first row of Settings, each named in its own language, stored in settings.txt; a player with no toolchain can correct any line from `/sd/synthminer/lang/<code>.txt` on the card. The font was the work, not the text (F-69): the engine drew ASCII only, and now draws Cyrillic, accented Latin, both dashes and the European quotation marks, generated from Hershey's own database with a check that every letter of every declared alphabet exists. `i18n_fmt` does its own `%2$s` substitution and takes the argument types from English, so an edited lang file cannot mislead it (F-70). `make` regenerates the tables whenever a lang file changes -- an ordinary make rule with known inputs, so nobody has to remember a second command -- and the generator validates while it generates (keys, placeholders, no word mixing two alphabets). `make langcheck` is the CI form, asking whether what is committed is up to date. worldcheck's "languages" section covers the rest: every character drawable, the formatter against six nasty strings. On the badge: the language list, Settings in Bulgarian, Controls in German. The language list first drew tick boxes, which the user rejected -- one choice out of many is a radio button -- so the engine gained `SE_MENU_VAL_RADIO` (their call to add it there rather than work round it). **Then 26 more languages, the user's call after asking what was missing** (F-71): 32 in all, English first and the rest alphabetical by the name each calls itself. The font grew seven accents (caron, breve, double acute, macron, dot above, ogonek, comma below), Greek out of Hershey's own SIMPLEX face -- the same weight as the Latin, which the Cyrillic never was -- and a dozen letterforms nobody can compose. 126 strings x 32 = 4032, every character of every one of them drawable -- 128 once step 14 added the volume sliders, and a label-width check came with them (F-76). |
| | **Accept:** every menu reached on the badge, a key rebound and used, a world created, played, saved, reopened with its inventory; the Testworld adopted | **in progress** | 2026-09-22: the Testworld adoption **ran on the user's card** — `worlds/flyover` became `slot1`, named *Testworld*, all five region files with it (a copy of the original is kept off the badge). The title strip renders (screenshot). The user is testing the rest by hand: the gyroscope works after one sign flip (F-55), and the inventory cursor bug (F-54) was found that way. `make check` covers slots, the inventory round trip and the adoption. |
| **7** | **Far Lands** | | |
| 7.0 | **Bedrock, gravel, and generated signs** (D-79) | done | 2026-09-22, asked for by the user for the Far Lands: bedrock (unbreakable) and gravel (shovel, drops itself) as blocks 17 and 18; a sign, block 19, a post with a board facing east (`K_SIGN`), not solid, breakable with nothing dropped. Its text -- "Kurt / was here", "Wolfie / was here", "Far Lands / or Bust!" -- is one of three 64x32 textures drawn by `make_textures.py` with its own 5x7 pixel font (so the PNGs do not depend on PIL's fonts), chosen by a hash of where the sign stands (`voxel_sign_text`). Existing textures regenerate byte-identical. |
| 7.1 | `farlands.c`: Beta 1.7.3's density generator, ported, fed coordinates past its overflow; the edge at x = -2048, sudden, stored per world (Part X, D-78) | done | 2026-09-22: redesigned by the user -- near spawn and a sudden cliff, as close to Beta's Edge Far Lands as possible, instead of a ramp at -100000. Built the same day (F-68): java.util.Random and Java's saturating cast ported, NoiseGeneratorPerlin / Octaves and ChunkProviderGenerate's density, terrain and surface passes in doubles; Beta chunk -784428 onwards is our chunk -129 onwards. `farlands_x` in level.smw; `chunk_worker_set_world(seed, farlands_x)` replaces set_seed. Composition 42% rock / 30% air / 19% water / 9% dirt and grass against the wiki's 36 / 25 / 23 / 10; tunnels run west (98.7% of neighbours alike along x, 87% along z); ground 27 high at the edge, the wall 61 one block on. A `farlands` test scene walks up to the wall. 134 ms a chunk on the badge. |
| 7.2 | Signs along the edge: "Kurt was here", "Wolfie was here" | done | 2026-09-22, asked for by the user; signs themselves are 7.0. One chunk in four of the last ordinary chunk column gets a sign, 0-2 blocks from the edge, on dry ground, facing east: 26 along 2048 blocks of edge in worldcheck's seed. |
| | **Accept:** the far-lands host section; `shots scene=farlands` for a look; generation and frame cost measured at the wall | **done** | 2026-09-22: worldcheck's "far lands" section passes; shots of the wall from 30 and 12 blocks on the badge; 134 ms a chunk, 11 fps at Medium walking up to it. Left for the user: a look at it in play. |
| **8+** | **The game** | | |
| **8** | **Crafting: a recipe book, containers, iron** (Part C) | | 2026-09-23: designed with the user, who replaced the grid with a searchable recipe book. Delivered in stages, their call, so the book's feel can be judged before the rest is built on it. |
| 8.1 | `items/recipes.{c,h}`, the discovery set, `i18n/fold.{c,h}`, inventory crafting and the book | done | 2026-09-23: recipes as ingredient multisets; discovery stored as every item ever held; the search box folding 32 languages onto one QWERTY (F-78). Three fixes straight from the user's first play: an empty pack on a new world, the opening keystroke no longer lands in the search box, and a recipe that says "missing" now opens a panel saying what is missing. Then four more from the second: a crafting table shows the inventory recipes too (one-way), Tab no longer opens the inventory behind an open screen, and two lines that ran off the screen (F-81). And F-80, which was two reports and one bug. |
| 8.2 | The crafting table and the wood and stone tools | done | 2026-09-23: block 20, its two textures, and the seven recipes. `BF2_USABLE` -- the Use key OPENS a block rather than placing against it, and which blocks do is a registry flag while what each one opens is main.c's business. **Tool speeds fixed**: the right tool divided by `level + 1`, so wood was 2x a bare fist and stone 3x -- the user's "mining with the wrong tool is a lot slower" simply was not true. Now `2 x level`: hand 1x, wood 2x, stone 4x, iron 6x, and no hardness number had to move. Iron moved out to 8.4, since ingots need the furnace. |
| 8.3 | `world/blockent.{c,h}` -- the chunk format's unused block-entity section -- and the **furnace** | done | 2026-09-23: pulled forward from 8.4 the moment the user found that iron needs smelting and coal needs finding, so a furnace early is what makes wood into fuel. `world/blockent.{c,h}`: a fixed pool of 192 records keyed by world position, written into `SECTION_BLOCK_ENTITIES` -- **a section the format has had a number for since Part W and never had a byte in**. `game/furnace.{c,h}` never ticks; it catches up from `now - stamp` when opened, in a loop that runs once per EVENT rather than once per tick (4 billion ticks in 0.01 ms, host-measured). Smelting: log to coal, sand to glass, cobblestone to stone -- the last two unasked for, but glass and stone had no way of being obtained at all. Two real catches on the way, both in F-77. |
| 8.4 | Chests, the trashcan, the disassembly bench, iron, auto-crafting | done | 2026-09-23. **Chests and the trashcan** are the block-entity pool's second and third customers, and cost almost nothing on top of the furnace: a record with 24 slots and no timers. Their screen is two grids side by side -- Tab swaps sides, enter moves a stack -- and the trashcan is the SAME screen, emptying by the same lazy clock the furnace runs on (`blockent_rot_trash`, one stamp for the bin, refreshed when anything goes in). **The bench** takes apart anything whose recipe carries `RF_REVERSIBLE`, giving back the full ingredient list however worn the tool -- the user's call, so salvage-and-recraft is a repair priced at the one coal in the bench's own recipe. **Iron** is block 22, deeper and rarer than coal, and the first block that REFUSES the swing (`BF2_TOOL_REQUIRED`) -- with a line on the HUD naming the tool it wants, because a swing that does nothing and says nothing is a bug as far as anyone can tell. **Auto-crafting** is Tab in the book, and F-82 is the part worth reading. |
| 35 | **Cactus, snow, sandstone, birch** | done | 2026-09-23, asked for by the user. **Five permanent block ids** (26-30, D-74), the first spent since the furnace round, and each one earns it by generating somewhere: a birch wood is its own biome at 5.8% of the world -- rare on purpose, because a stand of white trunks you come across now and then is somewhere and one you can always see is wallpaper -- chosen by a THIRD field, slower still, splitting the wet ground in two, because which trees won here is not a climate. Sandstone lies five blocks under the sand; cactus stands 1-3 blocks tall in the flats; snow caps **20.3% of the ground above y=41**, from a field slower than a mountain is wide so a summit is snowy or bare rather than speckled. `biome_def_t` gained the columns to say all of it -- `log_block`, `leaf_block`, `subsoil`, `column_plant`, `snow_above` -- so a third tree or a second desert plant is a row. Two bugs caught by the checks, both in F-87. |
| 34 | **Per-biome height, and mountains** | done | 2026-09-23, the user's call: the expensive half, plus a fourth biome. **The terrain is blended and the surface is not**, and that difference is the whole design. A biome id is a step function, so height taken from a lookup puts a vertical cliff at every border; what is blended instead is the WEIGHTS -- each biome's membership is a smooth function of temperature and humidity, the three height numbers are mixed by those weights, and the ground is continuous because every term in it is. It costs four multiplies on two noise values the column already needed; the obvious alternative (sample the biome on a grid of offsets and average) is 25 lookups and 50 noise fields per column. `worldgen_biome` is now simply the LARGEST weight, so the block on the ground and the shape of the ground cannot disagree about where a place starts. Mountains reach y=52 and go to **bare rock above y=40** -- the stone already under everything, shown rather than added, so a mountain range costs no permanent block id. Measured: plains 50.5%, forest 31.4%, mountains 12.0%, sand flats 6.1%; **biggest height step anywhere in 60000 blocks: 1 block**, including at crossings (F-86). |
| 33 | **Biomes: three places, from blocks we already have** | done | 2026-09-23, the user's call after asking what biomes would cost: the cheapest of three options -- three biomes, **no new block ids** (permanent, D-74) and no per-biome terrain height. A FOURTH REGISTRY (`biome_def_t` in worldgen.h): surface block, filler, soil depth, tree chance, plant chance, flower share. Chosen from two broad fields, temperature and humidity, at 420 and 360 blocks -- deliberately NOT from the height field, which would put the same place on every hilltop. Plains keeps the old world's exact numbers, so a plains chunk generates as it always did. Measured: plains 58.6%, forest 35.0%, sand flats 6.4%; a forest grows 0.135 logs a column against the plains' 0.059, and the sand flats are 100% sand with no grass, no plants and no trees. **The waterline stays sand in every biome** -- a beach is an edge, not a place. Still open, and the expensive half: per-biome HEIGHT, which needs the height parameters blended across biome borders or every boundary is a vertical cliff. |
| 32 | **Ore veins, and caves that reach daylight** | done | 2026-09-23, asked for by the user, who gave the reason as well as the request: caves *"encourage underground exploration"* and *"make it easier to find ore veins because they expose a lot of underground surface area"*. Ore is now a coarse candidate grid -- one vein per 8x8x8 cell at a probability, a lumpy ellipsoid with three different radii so it reads as a vein and not a decoration. The shape is forced by the generator: `fill_column` asks about one cell at a time and must answer the same from either side of a chunk border, so a vein cannot be grown by walking and has to be a function of position. Caves already existed and **could never reach the surface** -- `cave_at` refused the top four blocks outright -- so a broad mouth field now says where they may, and there they take the soil and the turf as well. Both numbers measured, not guessed (F-84). |
| 31 | **How many to move, and a cheat console** | done | 2026-09-23, asked for by the user. `ui/amount_ui.{c,h}`: moving a stack of more than one into or out of a chest, or into a furnace, asks first -- a slider AND a number, because one answers "about half" and the other answers "exactly seventeen" and neither answers both. It starts at everything, which is what the key did before it asked, and a stack of ONE never asks: there is nothing to decide and the modal would be a keypress added to every move. Taking a furnace's output never asks either (the user's rule: there is no reason to leave half a smelt behind). `ui/cheat_ui.{c,h}` on the backtick: every item in the game, searched by its STABLE name -- "pickaxe_wood", "iron_ore" -- which is English already, is what the save format keys on, is unambiguous, and needs no translation, which is exactly what the user asked for. |
| 8.6 | The 8.4 screens in all 32 languages | done | 2026-09-23: 24 more keys each. Three languages spell "disassembly bench" wider than the column that holds an item name (Portuguese, Greek, Bulgarian) and were shortened rather than the column widened -- it is already the widest layout in the game. F-83. |
| 8.5 | Item names and the crafting UI in all 32 languages | done | 2026-09-23: 63 keys x 31 languages -- every block and item a player can carry, the crafting book, the furnace and its picker. **Six overflowed and the check caught all six** before the badge did (French, Irish, Albanian, Greek, Bulgarian, Serbian), and widening the two crafting panels to hold them exposed something nothing had been measuring: **the book's row labels are ITEM NAMES**, and Russian "Деревянная лопата" is half as wide again as "Wooden shovel". `item.` joined `LABEL_COLUMNS`, the panels went to the wide layout, and the fold table grew to cover 82978 characters across the 32 languages. |
| 9 | **Farming: the hoe, wet and dry soil, five crops, and the composter** | done | 2026-09-29, built from Part A and D-104 to D-110. **Nine permanent block ids** (31-39, D-74): farmland dry and wet, the composter, five crops, and the upper half of the rice; twelve items, three of them hoes. The hoe tills grass or dirt and the soil comes out **wet or dry by a water search done on that keypress and never again** (D-106) -- dry soil then REFUSES the seed, which is the failure worth having: a refusal a player learns from instead of a plot that silently never sprouts. **Crops cost almost nothing to draw**: `BF_CROP` and `growth_max` had been sitting in the block table unused since step 0.3, and the stage-to-texture step rides on the data plane the fluids already paid for (D-101), so a crop is `mat[VF_TOP] + stage` and four textures in a run. **The real cost was time, and it is D-111**: a crop is a block, so there is nowhere in a cell to write when it last grew -- `chunk_t.stamp` is now saved in a nine-byte section of its own, no version bump, and a chunk that arrives without one is stamped *now* (which is what stops every field in an upgraded world ripening on sight; the host check caught exactly that). Growth runs on a **round-robin sweep, one chunk slot a tick** and a flag test for a chunk with nothing growing (D-112), plus the full catch-up when a chunk lands -- the user's *"advance events in one go to where they would be now"*. **Rice is the interesting one**: it stands IN the water, so its cell is a plant and a full water source at once (`BF2_WATERLOGGED`), which is safe only because the user's own rule puts it in water exactly one block deep. The **composter** is the furnace's record with a longer number: a day a unit, 0-2 worms from the world's hash and never `rand()`, and **an empty box banks nothing** so a week of standing idle does not turn the next scrap into instant compost. Wild potatoes, beans and tomatoes generate ripe in their biomes and rice in the shallows (D-107) -- measured at 54, 23, 10 and 37 in a 700 x 700 block world, which is a find rather than a crop. **Measured, not assumed: a field grown from seed to ripe leaves 0 cells in the physics queue**, so crops never touch tier 1. Two checks caught real bugs before the badge did: a hand-built test world ripened everything instantly (the zero-clock case, now D-111's rule) and `check_label_widths` caught French and Russian potato-plant labels overflowing the crafting book. 39 new strings x 32 languages. **Then the user played it, and three of their complaints were one bug** (F-120): `TEXCACHE_MAX` had been 48 since the showreel and this round took the material count to 64, so potato, tomato, bean and rice textures, `water_blend.png` and every item icon failed to load and fell back to their flat average colour -- which is what read as *"stand-ins"*, as plants that *"only slightly change color"* instead of growing, and as transparent water that would not switch on. The badge had logged `cache full` about twenty times a boot and nobody read it. Now 128, with a **static assert** so the next material fails the build rather than the picture. In the same round rice became **two blocks tall** (D-115), ripe wheat went **gold all over** (D-116), and every stage got a different silhouette rather than a different shade. **The yields and the clock were the user's next round** (D-117): wheat 1-3 grain and 1-2 seeds so a field can grow at all, tomatoes 2-4 fruit and no seeds since the crafting table makes those, the count rolled at every harvest rather than baked into the cell, and growth slowed from 7.5 minutes to **an in-game day** for wheat and tomatoes and **two** for potatoes, beans and rice -- which rebuilt the clock as absolute stage boundaries with a per-plant phase, and is a better shape than what it replaced. Not yet: eating any of it, which is step 10. |
| 10 | **Animals: pigs, cows and dogs; milk, cheese, sausages, fences** | done | 2026-09-29, **built ahead of the stove on the user's instruction** and out of four questions they answered first (D-118 to D-121). **Five permanent block ids** (40-44): the cheese maker, the sausage maker, the fence and its gate open and shut; **seven items**: pork, beef, a milk bucket, cheese, two sausages and a bone. **The creatures are the new shape** (`game/mob.{c,h}`) and item_entity.h had promised it since step 4: a fixed pool of 48, a registry row per kind, no allocation, and every random number a hash of the tick and the creature's own id so a replay reproduces a whole farm. A pig differs from a dog in what it eats, what tames it and how fast it is -- and in nothing else. **They are saved with the chunk they stand in** (D-123), in the entities section the format has had a number for since step 1.3 and never written a byte into; safe because an unloaded chunk is solid (D-14), so nothing can wander off the edge of the resident world and be lost. **Herds are generated with the land** (D-121) and needed one new chunk flag -- `CF_GENERATED` could not say "made just now", because both the card and the generator set it. Measured: 96 chunks in 576 hold a herd, 272 animals over 384 x 384 blocks. **A cow with a bucket gives milk**, which D-100 had already paid for -- though milk is not a fluid in the world, so "is this a bucket" stopped being a test on the contents before it dipped a milk pail in a lake. **The fence is the part that makes them animals you keep** (D-122): a block and a half tall, which is the first height that is neither walkable (step-up is one) nor jumpable (the player reaches 1.33). That cost one rule in the collider and a new mesh kind; a gate is two block ids for the same reason farmland is, and lies across the way the player was facing when it went down. A host check walks a body into one for 40 ticks and demands it stay outside, then opens the gate and demands it get through. **The two machines are the furnace's trick a third time** (`game/maker.{c,h}`): one file, a row each, a day for the cheese and a playing minute for the sausage, never a tick of work, and what each can make is the RECIPE TABLE under a station of its own. The bucket comes back when the milk goes IN, not when the cheese comes out -- returning it at the end would lock a pail up for a day, which is the one thing the user said not to do. **The bone** (D-120) is one in six off a pork sausage, from the world's hash, in a second output slot for the same reason the composter has one. **And a voice each** (D-124), synthesised like every other sound here. 37 new strings x 32 languages. **Then the user asked for the texture cache to be made safe** (F-120, amended): the by-name half is now derived from the item table, so a new item moves the requirement by itself, worldcheck counts what the game really asks for, and the cache says how full it ended up -- which caught an open gate asking for an icon that does not exist and never could (F-121). Not yet: eating any of it, which is now step 11. |
| 11 | **Food, hunger, and the kitchen stove** | todo | **Moved behind the animals on the user's instruction** (2026-09-29): *"Let's implement phase 11 (Animals) before Phase 10 (food, hunger, kitchen stove)."* Which is the right way round -- the stove's table needs cheese, sausages and beef, and all three come off an animal. Designed 2026-09-29, and the user's verdict on the alternative was blunt: cooking on a crafting table or in a furnace *"makes absolutely no sense"*. So food is made on a **stove that reads its ingredients out of the chest beside it** (D-105) -- and the chest is not one a player has to supply: **the recipe includes it, the item places two blocks, and each holds the other's coordinates** (D-110, the user's refinement, which kills the "which chest?" question by making it unaskable and makes a row of stoves possible). Breaking either half takes both, contents drop as every container in this game already does, and a pair that straddles a chunk border repairs itself on load the way D-99 repairs fluids at a seam -- the broken half drops the item, an orphan drops only its contents, so nothing duplicates -- a recipe selector, a fuel slot, an output slot, one in-game minute a dish, and **a message naming what is missing** when a recipe is short or no chest touches it, for the same reason iron refusing a wooden pick needed a line on the HUD. Eleven dishes, every number the user's, with **pizza as the superfood** at 10 hunger and 8 saturation because it needs a crop, a fish, a cow and a pig -- all four systems at once. `item_def_t` gains `hunger` and `saturation`, so a food is a table row. The hunger loop is Minecraft's model, which D-08 committed to on day one; the HUD has drawn both bars since 4.3 with nothing moving them. |
| 12 | **Fishing** | todo | **Moved ahead of mobs on the user's instruction** (2026-09-29, D-109): *"i want to implement 'Fishing' before 'Mobs and Combat', so switch the order of those two."* Bait is **worms held in the inventory**, **one per cast and not per catch**, which is why the composter in step 9 has two output slots and why its 0-2 worms a day is the dial that sets how much fishing anyone does. The catch is sardines, salmon or shrimp, and three of the eleven dishes need them. The rod is **three sticks for now**, with the final recipe still the user's to give -- and the thing they have decided is that **string will not come from spiders**, which means this step never depended on step 13 in the first place. **No rod durability** either, their call and the right one: fishing is already paid for in worms, and charging twice for the same activity is how a system stops being worth using. |
| 13 | **Mobs: zombies, skeletons, spiders; spawning, pathing, combat; beds and spawn; death keeps the inventory** | todo | Now after fishing (D-109). Unchanged otherwise, and still the biggest single block left: it is the first thing in the game that needs an entity with a mind rather than a record with a timer. |
| 14 | **Audio: sound effects, and music that is mostly silence** (D-82, D-83, D-84, D-85) | done | 2026-09-23: the mixer starts at boot. 21 effects as table rows (`audio/sfx.c`), and which one a block makes is its own registry row (`block_def_t.sound`), so a new block brings its sounds with it. Music is eleven public-domain MIDI files played by a ported sequencer and a six-shape synth: 72 KB for half an hour, against megabytes for the same music as MP3. `worldcheck`'s `check_midi` proves every shipped file parses, ends, rewinds identically and survives truncation at any length (F-72). The raw voice sum clipped, so the synth carries a master gain and a cubic soft limiter (F-73). **Three volume sliders** in Settings -> Audio (D-85): the badge's own, then how loudly the music and the effects are each mixed in. The effects turned out to be inaudible whenever the music was off -- the amplifier was asleep and eating them (F-75) -- which is why the engine is 2.2. Two checks came out of the round and stay behind: `tools/symcheck.sh`, after an unexported `strcasecmp` made the app link clean and then refuse to start with no message at all (F-74), and `check_label_widths()`, after the sliders' labels turned out to be the least of it -- three settings screens had been overlapping their own text in a dozen languages since the day the language count went to 32 (F-76). |
| 15 | Block and sky lighting | done | 2026-09-22, asked for by the user (torches that light the area, computed when a block changes). Pulled forward from the end of the plan: a light plane per chunk (sky and block light, 0..15 each, D-69), flooded when a chunk arrives -- its own light on core 1, the border exchange on the main task (F-58) -- and updated with the two-queue flood on every block change. The mesher keys faces on light; a per-frame table turns light into brightness for the time of day, through the engine's new `SE_TRI_LIGHT`. Host-tested: fall-off, removal, a shaft opened and capped, across a chunk border and into a chunk arriving late. On the badge: a placed torch lights the ground at night. |
| 16 | **Day and night; sun, moon, clouds, stars** | done | 2026-09-22, asked for by the user. `game/daytime.{c,h}`: a 20-minute day from the world's tick count -- sun direction, sky and fog colours with an orange band at sunrise and sunset, a daylight fraction, the light table. The showreel's blocky sun, moon and clouds (`voxel/voxel_sky.c`, clouds laid out in world coordinates) and its starfield. Night takes 9 light levels off the sky, not Minecraft's 11 (F-57). Clouds are a Graphics toggle; the title has none (they flew through its letters). |
| 17 | **Fred: first-person arm, held item, third person** | done | 2026-09-22, asked for by the user: the showreel's miner, ported as `fred/fred.{c,h}` and `fred_mesh.{c,h}` with provenance, given an axe and a shovel beside the pickaxe, blocks in their own textures, flowers as sprites and a torch as its stick. Lit by the cell he stands in. First person draws the arm at a third of the showreel's distance and size (D-70). Third person (Settings -> Graphics -> Camera) puts the camera 4 blocks behind his eyes, pulled in by a ray when something is in the way; the crosshair is hidden there, the block outline kept. |
| 18 | **Position overlay** | done | 2026-09-22, asked for by the user: a new action, `Show position`, Backspace by default and rebindable, toggles coordinates, compass heading (D-71), the world's clock and day, and replay status. |
| 19 | **Torches could not be taken back** (bug) | done | 2026-09-22, reported by the user (F-56). |
| 20 | **Terrain vanishing; "walking through" steps** (bug) | done | 2026-09-22, reported by the user as critical: at a step the view went into the ground, and looking round left holes. **Not physics** -- the same collision code walked the user's own terrain on the host for 6400 ticks without once ending inside a block, and the badge log showed the player climbing. It was the engine's geometry lists overflowing and DROPPING triangles, which the engine never reported (F-63). Caps raised, the line list shrunk to pay for it, far meshes' light rounded, the engine's drop counter fixed (D-72). The same walk now peaks at about 3000/6144 flat and 1900/4096 textured, nothing dropped -- at **7-9 fps** at the medium view. |
| 21 | **Fred's hand**, and the handedness bug | done | 2026-09-22, reported by the user: first person held things in the right hand, third person in the left (F-64). Now a Graphics setting, right by default, and both views follow it. Checked on the badge mid-swing, both ways: right-handed the pickaxe rises by his right shoulder, left-handed by his left. |
| 23 | **Permanent block ids; slots that say why they cannot be opened** | done | 2026-09-22, the user's call after F-52 was explained (D-74, D-75). Every block numbered explicitly; `tools/ids.txt` lists every block id and name and every item name ever shipped, and `make check` fails on a renumbered, renamed, removed or unlisted one -- each of the three cases tried and caught. Replays now store their inventory by item name (format 2). The slot list tells a world from a newer build ("from a newer version"), an older format ("needs upgrading") and a damaged one apart from an empty slot; nothing can be created over any of them. The upgrader itself is left until there is a format change for it to do. |
| 24 | **Lighting's triangle cost, and the frame rate** | done | 2026-09-22, the user asked why the frame rate had fallen so far. Measured, not guessed: on the same scripted flight as 2026-09-21, today's build is as fast as yesterday's (14.5 vs 14.6 fps with lighting and clouds off, 14.1 with everything on) -- **no regression** (F-66). What had changed is the scene and the setting: walking at eye height puts close-up textured ground over the whole screen (rasterize 46 -> 72 ms), and the user plays at Far. Light in the merge key rounded in the near meshes, sky to every 4th level and torch to every 2nd (F-65): 25% fewer near triangles with torches about, +4% fps on the walk. Default view back to near (D-76). The `flight` and `replay_*` test scenes make these comparisons repeatable. |
| 22 | **N: step the clock** | done | 2026-09-22, asked for by the user: a debug key moving the world's clock a quarter of a day, for looking at night without waiting. |
| 27 | **The player's data out of the app's directory** | done | 2026-09-22, the user's catch (D-80): worlds, settings.txt, replays and screenshots move from `/sd/apps/at.cavac.synthminer` to `/sd/synthminer`, which the launcher does not manage. `world/datadir.c` moves what an earlier build left there on the first start -- a rename per entry, never over an existing one, nothing on a second start -- host-tested. Test-kit shots go to `/sd/synthminer/test`. |
| 26 | **Screenshots** | done | 2026-09-22, asked for by the user: a new action, `Screenshot`, **0** by default and rebindable, saves the frame as the player sees it (HUD included) to `/sd/synthminer/screenshots/shotNNN.png` with the test kit's PNG writer; a "Saved ..." line shows for 2.5 s on the frames after, so it is never in the picture. |
| 25 | **The depth plane in internal SRAM, and a key sort** | done | 2026-09-22, the user's call (D-77). Engine option `SE_SCENE_DEPTH16_INTERNAL`: at quarter resolution a plain 16-bit depth plane (188 KB) in internal SRAM, cleared each frame (0.29 ms), instead of the stamped PSRAM plane; the flat list it displaced went to PSRAM. Depth order is now a radix sort of 32-bit keys in internal SRAM plus one gather, not a qsort of the records. Same scenes as F-66: flight 13.97 -> 16.30 fps, near walk 10.22 -> 12.36, Far walk 7.08 -> 7.99 (F-67). |
| 29 | **Torches on walls** | done | 2026-09-23, asked for by the user. The mesher can now see each cell's block-data field -- a third plane beside cells and lights, carrying `st_data()` already extracted, because `voxel_mesh.c` may not include `chunk.h` -- and the torch reads it: upright in the middle of its cell, or shifted 0.30 to whichever wall was pointed at and lifted 0.20 off the floor. No tilt: a greedy voxel mesher emits axis-aligned boxes, and a rotated stick would be a second kind of geometry for one block. Placing picks the wall from the face that was struck, refuses the underside of a block (nothing here hangs) and refuses a wall that is not solid -- the first placement in this game to say no for a reason other than the cell being full. meshcheck pins where the stick ends up, not merely that something was drawn. **Known gap:** breaking the block a torch leans on leaves it floating; that wants block-update propagation, which leaves-decay and falling sand will want too, so it is worth building once rather than special-casing here. |
| 30 | **Item icons, a bigger inventory, and the arm** | done | 2026-09-23, asked for by the user: the slots were flat average colours (D-03's placeholder) and three stone tools were three grey squares. A block is now drawn with **its own side texture** -- one table of files, no second copy, and a new block brings its icon with it -- and the eight things that are not blocks got drawn 16x16s with cut-out backgrounds. Tab slots 44 -> 60 px. The first-person arm got a mesh of its own, the sleeve running back past the camera: its flat cut end had been sitting just inside the bottom of the frustum, which is what made it read as a severed arm hanging in the air. |
| 28 | **Water you can see into, and swim in** (D-86) | done | 2026-09-23, the user: water was an opaque cube, so putting your eyes under it broke the picture, and there was no swimming. Their rule, and it is the whole of it: **do not draw the sides or the bottom of a water block, and draw its top only when the block above is air.** That became `K_LIQUID`. Two things follow that the rule does not say out loud and the picture needs: a liquid must stop HIDING its neighbours, or the lake bed is never meshed and the surface is a lid over nothing; and the surface needs a second, downward-facing copy, emitted by the air cell above it, because an axis-aligned face is visible only from the side its normal points at -- which is exactly why it vanished as the eye went under. `water.png` became a cut-out checkerboard (the engine's one-bit alpha, the leaves' mechanism) so you see through the surface both ways. Swimming is buoyancy in `player.c`: jump rises, sneak dives, and the numbers come from `phys_gravity`'s recurrence rather than from feel. meshcheck pins the rule per material and per direction, and caught a real bug on the way -- the extra slice let a border cell act as an owner and doubled every face at a section seam. **And the blue.** The user's read of it was right and mine was wrong: the renderer already touches the brightness of every pixel, so the tint belongs there. `se_scene_set_tint()` scales the red and green of every triangle by one factor and the blue by another; the sky and the fog go to a dark blue and the sun, moon, clouds and stars are not drawn from under the surface. |
| 36 | **Page flipping on the display's own buffers** (D-87) | done, device test todo | 2026-09-24, the user's call after looking at another engine's numbers. Engine 2.2: three driver framebuffers, present = select for the next refresh, no copy; the flip also writes back and drops the frame from the cache (F-89). Needs **graceloader 2.6.0** (`graceloader_display_register_callbacks`, IRAM trampolines chaining the BSP's callback, `esp_lcd_dpi_panel_get_frame_buffer` exported). Engine `4ac29d8`, graceloader `89fb785`, template `0c62ac4`, SynthMiner `cee4e24`. G6 tests 1-3 outstanding. |
| 37 | **The raycast renderer removed from the engine** (D-88) | done | 2026-09-24, the user's call. Engine `929f12d`; synthracer's debug key R went with it (`7e7e139`). Recorded under 2.2 as a deliberate exception to MAJOR. |
| 38 | **`SE_RENDER_BANDED`: the z-buffer, band by band in internal SRAM** (D-89) | done, then removed (D-90) | 2026-09-24 built (engine `39ec8b4`), 2026-09-25 measured and taken out again. Host check: identical to the z-buffer in 1000 random scenes, and it catches a deliberately broken copy (F-90). On the badge, over the bench flight: 1.61x faster at full resolution, **8% slower at quarter**, and `SE_SCENE_BAND_W=64` does not fit in internal SRAM at all (largest free block 37-38 KB against the two 60 KB it needs), so the gap is not tunable. Removed from the engine with `SE_SCENE_BAND_W`, and `_banded` with it here; `SE_RENDER_BUILTIN_COUNT` back to 1. The raster target stays. Numbers and reasoning in G6. |
| 39 | **Measure, and keep one renderer** | done | 2026-09-25. The z-buffer is kept; see G6 for the five measurements and D-90 for the decision. Two things the round taught that outlast it: the scene a renderer is measured on has to be able to measure one (F-91, step 41), and `PROF_BLIT` / `PROF_VSYNC` had never been fed, so the present was hiding in the residual (F-88) -- both fixed. **Still owed:** a `renderercheck` beside `meshcheck` was not written, because with one renderer left there is nothing to compare; if a second is ever added it comes back with it. |
| 40 | **Bands on both cores** | dropped | 38 lost, and this was conditional on it. G6 (d) keeps the design notes: the raster target that would have carried it survives, so a later attempt does not start from nothing. |
| 41 | **A world worth measuring on** | done | 2026-09-25, out of F-91 and the user's call: *"a persisted, pre-generated test world seems the best option... Clear a flight path so you don't get blocked... The world should be separate from the worlds i can manage through savegames and also be based on a fixed seed."* `game/benchpath.h` holds the seed, the path and `bench_path_at()`, and `tools/worldcheck.c`'s `check_bench_path` asserts the path against the generator that is compiled in, so a worldgen change that moves this terrain fails the build. **The path was chosen by search, not by eye**: 4000 seeds x 8 headings, keeping only those whose ground never steps more than two blocks, then the busiest -- seed 1030 due +z crosses ALL FIVE biomes in 240 blocks with 23 blocks of relief and a worst step of ONE, so nothing had to be carved and the ground-following camera can never be buried. The world lives at `<base>/bench/`, OUTSIDE `worlds/`, which is the whole of how it stays invisible: `worldstore_list()` scans `worlds/`, so the world-select screen cannot show it, open it or delete it, and the slug is reserved so a player-named world cannot collide. A bench world whose seed does not match is deleted and generated again rather than measured. `bench_gen` walks the path in 8-block stops waiting for `missing == 0` at each (generated chunks are already `CF_EDITED`, so eviction writes them; 49 chunks, ~45 s, once); `bench` and `bench_fullres` fly it off the card in 735 ms of loading and 40 s of flight, view distance forced to near so two runs compare. The perf clock and accumulators restart when the world is resident (`devtest_perf_restart`), so the card is not averaged into the rasteriser. |
| 42 | **CraftMiner becomes SynthMiner** | done | 2026-09-25, the user's call after a Discord discussion (D-91), and done with no badge to hand. 445 references over 84 files, in one scripted pass so the ordering is auditable rather than a chain of hand edits: the showreel's own paths are sentinelled out first (`main/craftminer/...` and `cm_title.c` name files in ANOTHER repository and are not this game's to rename), then identifiers, then extensions and magics, then the name itself, including the declined forms five translations carry -- `CraftMinerom`, `CraftMinerem`, `CraftMinerjem`, `CraftMinerilla`, `CraftMineriga` -- which stay correct because the ending attaches to a stem that was swapped, not to the word. Two things the sweep could not have caught on its own: `-DCM_HOST` in the Makefile, where the `D` is a word character so the boundary guard did not fire, and a `www.cmr.no` in a vendored zlib header that the `.cmr` rule matched and that was reverted. **The title screen keeps its framing by arithmetic, not by luck**: the block font never had S, y or h, and the three were drawn in its style so that "SynthMiner" comes out at exactly 48 blocks, the width "CraftMiner" was and the width the camera path is framed on. Migration and its host check are D-91; the cleanup that follows it, and the appfs finding, are D-92. **Verified end to end on the badge on 2026-09-26**, on a real card with a real world, after three bugs that only hardware could show: the stack (F-93), the half-migrated world that read as an empty slot (D-93), and a directory removal that reported success without removing anything (F-94). The card now holds `/sd/synthminer` and `/sd/apps/at.cavac.synthminer` and nothing of CraftMiner's; the world came through as `level.smw` and 21 `.smr` regions. |
| 43 | **Livestream the screen into OBS** | done on the badge, **moved into the engine**; audio parked in step 44 | 2026-09-26, the user's ask. Ported from `tanmatsu-nfmtest-grace`, which was built to answer exactly this question: `nfm/{netraw,usbnet,tsmux}.{c,h}` come across **byte-identical**, TinyUSB is vendored beside them (`components/tinyusb`, the loader does not export it), and `nfm/stream.c` is the one that was rewritten -- there the frames came from a test pattern in its own task at a fixed rate, here they come from the game. So the shape changed: **the colour conversion is inline in `on_render`**, because that is the only moment the framebuffer is still (the engine flips pages), and everything after it -- encoder, muxer, USB -- is the stream task's. A frame offered while the encoder is busy is DROPPED, never waited for: a stream that stutters beats a game that does. `nfm/livestream.{c,h}` is the switch, and the Display menu's fourth row is the only way to work it. The encoder came from `upstream/main` (`8add127`, "Sync with graceloader: the hardware H.264 encoder") -- before that merge `fakelib` exported none of `esp_h264_*` and the port could not have linked, let alone loaded. **2026-09-26, the user: "it would also be a good idea to implement all the streaming in the engine, so we can re-use it in other apps."** So all of it moved: `se_stream.h` is engine API now, TinyUSB is vendored under `src/internal/tinyusb` and built only in plain-CMake mode (under the IDF the host has its own), and this app keeps nothing but one call in `on_render` and one menu row. Moving it also made AUDIO the engine's to give rather than the game's to wire: the mixer is already there, so `cfg.audio` needs no tap and no callback from any app. **The switch is the last row of the main Settings menu**, not a Display setting, by the user's correction (D-95). **And `cfg.audio` now carries audio** (engine 2.3): it used to log "no audio in this stream; video only" and degrade, because there was no permissively licensed encoder to put behind it. There is now -- `pdmp2`, written for this, public domain, MPEG-2 LSF Layer II at the mixer's own 22050 so nothing is resampled (D-96). The vendored shine is gone and nothing in the engine is copyleft. Verified on the host at six rate/channel/bitrate combinations and through the muxer into a `.ts` that reads back as `mp2 / 0x0004, 22050 Hz, stereo, 128 kb/s`; **the two findings that cost the most are F-95 (the syncword is 12 bits) and F-96 (the analysis window is not a design choice)**. **Tested on the badge on 2026-09-26, and it works: picture and sound in OBS.** Getting there took three bugs the host could not have shown, all of them in the transport rather than the codec: the parameter sets were too rare for a receiver to join reliably (F-99), quitting the app mid-stream wedged the badge's USB (F-100), and the video and audio clocks ran at different rates so the sound fell steadily further behind (F-101). **Diagnosing any of it needed an instrument, because the console is what the stream takes away.** During the hunt the counters were drawn on screen, which is the only output channel a running stream leaves; they were taken off again once it worked, at the user's request -- *"Remove the debug info onscreen."* What stays is `se_stream_stop()` logging the same figures at the first moment there is anywhere to print them: `published`/`frames`/`dgrams`/`dgrams_failed`/`audio_frames` separate "the game never offered a frame" from "the encoder refused it" from "the muxer emitted nothing" from "the link took nothing". That is enough for a stream that is already known to work, and if a future stream does not, the overlay is four lines of `se_stream_get_stats()` in `on_render` again. Still open: a possible constant audio/video offset, untested, with the ring backlog the prime suspect (F-101). |
| 44 | **Why the livestream stalls, and audio shipped off** | done (video), **audio parked** | 2026-09-27, out of the user's packet capture and then five rounds of instrumentation (F-102..F-108). **Nothing was wrong with the link or the game.** The capture exonerated the network -- clean continuity counters on all five PIDs, 0 bad sync bytes, `dgfail` 33/11688, constant arrival-minus-PTS skew -- and the badge's own counters exonerated everything else: PPA 14.5 ms, H.264 8.0 ms and mux 2 ms constant in and out of collapse, `pub` 9-10 offers/s throughout, `audn` 86.1 pushes/s at 24 us straight through. **STARVED was 0%**, so the task held the CPU and the `WORKER_PRIO` change I had been about to make could never have helped. Three theories died on facts already in hand (F-104: `chunk_worker.c` has no mutex at all; the job queue is `xQueueSend(..., 0)`; core 1 was not saturated). The accounting did not close until three untimed calls were timed (F-103), and `aud_us` turned out to be measuring the mixer's push rather than the encode, so the MP2 cost had never been measured on hardware at all. The answer is F-108: `allocate()` sweeping the encoder's 5.6 KB struct out of PSRAM, 1.3 ms -> 160 ms, a 125x swing. The fix is written and **unmeasured** -- it takes the codec to 22.5 KB of internal SRAM and chunk loading broke -- so the user's call was *"Fuck audio streaming for now."* `SM_STREAM_AUDIO` is 0 in every ordinary build (`make STREAM_AUDIO=1` for a measurement run, which prints a banner), the code all stays, and with it off `se_stream_audio_prepare()` is never called so no buffer, ring or PID exists. **Owed before anyone tries again: an internal-RAM budget for the whole app.** The instruments stay in the tree -- `tools/streamcap.py`, `tools/streamanalyse.py`, `tools/infoanalyse.py`, and `/sd/defuckinfo.txt` written by `se_stream_stop()` -- because the console is what the stream takes away, and every wrong turn in this round was reasoning where a timer would have answered. |
| 45 | **App icons** | done | 2026-09-28, asked for by the user: the three icons `metadata.json` names were placeholder question marks. They are the game's iron pickaxe on the game's own stone darkened right down, generated like the textures -- `tools/make_icons.py` imports `sm_stone` from `make_textures.py` rather than copying it, so the backdrop cannot drift from the rock the player digs. One drawing on a 16-unit grid multiplied by 1, 2 and 4, so the three sizes stay one picture; `make icons` regenerates them byte-identically. Two shapes earned comments by being wrong first -- a flat bar with two teeth under it reads as a table with a stick leaning on it, and a shaft that passes the head makes the whole icon a figure 7 -- and the dark outline is skipped at 16, where it merges with the background instead of separating from it. |
| 46 | **The app repository gets the whole game** | done, **publish not committed** | 2026-09-28, the user: *"Verify that metadata.json includes all required files... And make sure that `make apprepo` also copies the files correctly."* Both were broken, and F-109 is why neither could have been noticed here. `metadata.json`'s asset list is now generated from the same directories the install rules glob (`tools/make_metadata.py`, `make metadata`, `make metadatacheck`, and `metadata` is a dependency of `make check`); `apprepo` copies the textures and the music and then `tools/apprepocheck.py` walks what the file promises, reporting anything stale **without deleting it**, because `APP_REPO_PATH` points outside this checkout. The publish was run for real into `../tanmatsu-app-repository/at.cavac.synthminer` -- a first publish, the slug directory had never existed -- and verified four ways: the repository's own schema against all 64 apps (0 failing), 70 promised files present and 0 unexpected, the runtime layout traced from `install_basepath` to `texcache_init`, and all 61 texture names the code can ask for resolved. The comma left the description and the torch flame found its file (F-110). **The directory is untracked in that repository and left for the user**, since committing there is a pull request. |
| 47 | **Blocks that did not appear, and leaves that felled trees** | felling **done**; render bug found later, in step 48 (F-113) | 2026-09-28, both out of the user's play session. F-111 and F-112. The render bug was two faults: a dense canopy asks for 8540 textured triangles against a 4096 cap, **and** submission ran in slot order -- a wrapped coordinate hash -- so the overflow landed anywhere, including the chunk underfoot, and moved as the player walked. Now sorted near to far, so a full list loses its far edge into the fog, and the drop counter is on the position overlay instead of only in a console no player has. **The badge then refuted the capacity half** (see F-111's correction): the textured list is in PSRAM and peaks at 1340 of 4096 in real play, nothing is dropped, and the host figure was inflated six times over by counting underground sections and the whole 360 degrees. The sort stays as defensive work; **the missing blocks are still unexplained**, and the drop counter on the overlay is now the instrument that will say whether the lists are involved at all. Felling: Part F gave one flag two jobs, so `BF_FELLABLE` now means tree material (what a fell spreads through) and `BF2_TRUNK` what starts one; worldcheck covers both directions. |
| 48 | **The flight recorder** | done | 2026-09-28, the user: *"It might be worth actually saving the relevant numbers ... So if i encounter a bug, we can just look at the trace afterwards (without having to know that we need to debug beforehand)."* `/sd/synthminer/trace.txt`, always on, no setting: a header per run, a line a second (frame rate, position, both geometry lists, drops, streamer), a line per block placed or broken, and a line for the mesh that answers an edit **carrying the lag** -- the wait between changing a block and seeing it, which nothing else reports. `common/trace.c` is pure stdio and takes its clock and directory from the caller, so worldcheck exercises the formatting, the lag and the rotation on the host. One playthrough per file (the user's call), the previous kept as `trace.prev.txt`, and the size cap is a ceiling rather than a rotation -- rotating mid-run would throw away the start of the run being recorded. `tools/traceanalyse.py` leads with the two things that mean something went wrong. **It paid for itself within one session: F-113.** |
| 49 | **The floor of the world, and chunks that are never a photograph** | done | 2026-09-28, out of the user's question about bedrock. y=0 was BLK_STONE under a comment calling it unbreakable; `worldgen_force_floor()` now runs as the LAST step of generation, after both generators, on the user's instruction -- *"generators like caves can't accidentaly make holes"* -- and on every chunk arriving from the card, so old worlds repair themselves. No version bump: D-30 already says worlds upgrade as their chunks are written back. What makes that work is the user's own aside, which is the bigger change: **a chunk loaded from the card is marked dirty immediately**, because *"for animal movements, crops growing etc"* a chunk is not a photograph. It costs roughly two saves per load (F-114). |
| 50 | **Region buckets, an open-file cache, and what the card costs** | done | 2026-09-28, the user: *"Add the region handle cache and subdirectories now. Maybe we should also have another directory level for a sort of mega-region? How big is each chunk file?"* Measured: 41-227 KiB a region, 128x128 blocks. Buckets of 16x16 regions -- at most 256 files in a bucket, one entry per 2048x2048 blocks in the parent -- and **no third level**, because filling the parent would take 84000 square kilometres of explored ground. Four cached handles, LRU, dropped before anything renames or removes underneath them. Two real bugs fell out, both caught by running worldcheck twice rather than by the badge: **deleting a world stopped working** (the delete walked one directory level, so every bucket survived and the rmdir failed) and `sm_remove` on a cached region left an unlinked file that writes still went to. Load and save are now timed separately from generation (F-114). |
| 51 | **A cactus comes down as one** | done | 2026-09-28, the user, from a play session the recorder had already logged them doing the hard way -- six breaks in fourteen seconds. `BF2_STACKED` on the registry rather than a hard-coded id, so sugar cane arrives with it working. It is SUPPORT, not the felling rule: it ignores ST_PLACED, goes straight up one column, and stops at the first block that is not the same kind. A stand-in for block updates, and says so. |
| 52 | **The main menu takes fifteen seconds** | **done** (F-117) | 2026-09-28, the user's observation, answered but not fixed: the title runs on a scratch world, so its 81 chunks are generated every time, at 186 ms each (F-115). Loading them off the card instead is 2.4-3.0 ms a chunk. The bench world of step 41 is the pattern and the title is a better fit for it -- fixed seed, fixed camera, nothing a player can change. |
| 53 | **The SD card's write timeouts were ESP-Hosted** | done | 2026-09-28, out of the user's question *"does the filesystem handle this correctly, or do we silently corrupt the FAT file system?"* -- which turned out to be the right question asked of the wrong layer, twice over. fsck.fat said no corruption but **45 leaked clusters in six chains**, exactly the FatFs behaviour: a cluster allocated by create_chain() before a failed write stays marked in use while the directory entry that would reference it is never written, and sync_window() discards the second FAT copy's write result entirely. The cause was neither the card nor the filesystem: the ESP32-P4 has one SDMMC controller and it was shared with the WiFi co-processor (F-116). WiFi is gone from graceloader, the errors with it, and 42 KiB of internal heap came back. What remains in the tree for the next time: a retrying disk layer in graceloader (ESP-IDF's does not retry at all), region_recover_tmp() for a compaction interrupted between the remove and the rename, cache_drop() on every region failure path -- **one read error used to poison a cached FILE\* and stall the world for ever** -- and `cardfail=` in the flight recorder. |
| 54 | **The C6 radio is powered down at startup** | done | 2026-09-28, the user, once WiFi had left graceloader: *"can we also tell the coprocessor during graceloader startup to power down the C6 radio processor completely?"* It can, but not from the loader (D-97): `bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_OFF)` needs a coprocessor handle, and `bsp_tanmatsu_coprocessor_get_handle()` answers ESP_FAIL rather than initialising on demand -- while graceloader's first comment says it links every component and initialises none, because the app decides. So it sits in `on_init`, where the engine has already brought the BSP up, in **synthminer and synthracer** both. Failure is logged and ignored: a badge whose coprocessor will not answer has a worse problem than an idle radio. **It persists past the app** -- the coprocessor holds the state -- so a launcher that wants WiFi has to ask for it back. |
| 55 | **Water that flows, and the scheduler under it** | done | 2026-09-28, the user: *"Let's implement water/fluid physics"*, with the design given in the same message -- know which blocks are actually doing something so the rest cost nothing, save the state, and use the same machine later for falling sand and for crops. Built as TWO TIERS (D-98): `world/blockupdate.h` is a wheel of 64 tick-buckets over an explicit set of woken cells, with **one bit per cell in a fourth chunk plane** saying whether it is in the queue -- both the duplicate filter and the literal answer to the user's *"know if a block even has active physics going on"*; `world/fluid.h` is Minecraft's rule on top of it (levels 0-7 in the state byte, a falling bit, down-first, two sources make a third, and a cut-off flow that dries up). **Settled water is not in the queue at all**, and a full sea chunk arriving wakes exactly 0 of its 11520 cells -- both pinned by host checks. The **bucket** is three iron ingots, fills only from a source and casts its own `RAY_FLUID` ray because the crosshair looks through water on purpose; it has a drawn icon and, after the user found it being held as a coloured cube, a model of its own beside the pick and the axe (`FRED_HOLD_BUCKET`, a tapered pail with a wire handle, its contents coloured from the block table so lava and milk will need no code). **The user's verdict on the model: "a bit strange, not really like a bucket. But i guess it's good enough for now"** -- so it is a placeholder that works, not a finished thing. Costs 512 KiB of PSRAM for the active plane. **The seam is the hard part and it is the user's own catch** (D-99): a missing chunk reads as BLK_BARRIER, so a flow stops at the edge of the loaded world -- and `blockupdate_chunk_join()`, called from the same place in `apply()` as `light_chunk_join()`, wakes both the arriving chunk's unsettled fluid and the facing border of the chunks already there. Not yet: Minecraft's flow-toward-a-hole preference, lava, and falling sand -- which is one `dispatch()` line away. |
| 56 | **Water you can read: partial heights and a visible waterfall** | done | 2026-09-28, the user, on being told partial heights needed a different mesher: *"What's the problem with partial water heights? That seems to be integral as feedback to the player."* They were right and the claim was wrong (D-101). A flow is emitted per cell beside the plants and the torches, where this file has always put the blocks that are not boxes -- surface part-way up the cell, **corners averaged from the neighbouring cells so the sheet tilts the way it is running**, and the tilt is the flow arrow without anything working out a direction. Sources stay in the greedy pass, so every ocean is byte-identical and D-86 stands where it was made. **It also fixed a bug nobody had seen yet**: under D-86's "a liquid draws no sides" a falling column emits *nothing at all* -- no top (the cell above is water), no bottom, no sides -- so a waterfall was invisible between the spring and the splash (D-102). Held to **12 triangles for the worst-case cell** by naming a direction on every face and emitting one winding a wall, against 20 for the double-sided version; meshcheck pins the number, the slope, the surface height and the fact that a source pool is untouched. |
| 57 | **Played, and the numbers say it is free** | done | 2026-09-28, the user's first test game: *"Water works surprisingly well."* The flight recorder agrees and says why. The physics queue peaked at **201 cells, dropped 0, carried 0, and returned to 0 every time** -- settled water really does cost nothing, which is the claim the whole design rests on. Mesh lag stayed at a **median of 89 ms** with every real case between 78 and 163 ms, so the remesh churn I had predicted from water edits marking sections urgent did not happen. 114 chunks saved at a mean of 16.8 ms, no compaction, no card refusals. **The one alarming number in the report was mine** (F-118): a 31281 ms mesh lag that turned out to be the recorder matching a mesh against an unrelated edit. Also visible for the first time: `pace_after_write`, the cargo left from the refuted erase-cycle theory, **waited 173 ms across four pauses, worst 68 ms** -- it is not inert, and now has a measured cost rather than a suspicion. |
| 58 | **Water you can actually see through** | done, **default on** | 2026-09-29, the user, after being told partial heights were the expensive part: *"Currently we have fake transparency for water (like leaves). How much actual FPS impact would it be to have actual water transparency?"* The estimate said 5-20%, scaling with how much water fills the view. They asked for it behind a key so the two could be compared in one place, played it, and reported *"The new transparent water looked much better and framerates seems comparable."* Now a Graphics setting, **on by default and on for upgrades too** (D-103). `SE_TRI_BLEND` in the engine (2.5): a 50/50 RGB565 mix, and -- the part that matters -- blended triangles sorted after every opaque one and far-to-near among themselves, which cost **one bit of the existing depth key and no extra pass**, because a positive float never sets its sign bit and the top bit was always spare. D-86 is amended a second time: water is no longer only its surface's cut-out, it is a real mix, and `water_blend.png` is the same texture without its checkerboard. **The frame-rate claim is the user's, not the trace's**: their A/B in one spot is controlled and the trace was not, because nothing recorded which mode was running -- which is now fixed (`trace_event`, F-119). |

---

## Part E: findings and decisions log

- **F-82** 2026-09-23: **the auto-crafting planner was wrong in a way that
  only a worked example shows.** The user's own example was the test: *"when we
  need a pickaxe, but only have blocks of wood"*. Two logs is eight planks; a
  pickaxe is three planks and two sticks, and sticks are two more planks.

  The first version provided each ingredient in turn and then crafted. It turns
  a log into four planks (three needed -- done), then turns two of those planks
  into four sticks -- **and the three planks it had a moment ago are now two.**
  Nothing reserves anything, so a later ingredient quietly eats what an earlier
  one was given.

  The fix is not reservation, which would need a whole allocator's worth of
  bookkeeping for a tree four deep. It is to **ask again**: each pass
  re-provides whatever is short, and the loop ends when the recipe can actually
  be made, when a pass achieves nothing, or when something cannot be provided at
  all. Two logs now come out as a pickaxe with three planks over, which is the
  arithmetic done by hand.

  Two smaller things fell out of writing the check. The planner would have made
  a crafting-table recipe in your bare hands, because nothing asked whether the
  RECIPE belonged at the station -- only its ingredients. And it must never
  smelt: an iron pickaxe planned from iron ORE would have the menu lighting a
  furnace on the player's behalf.

- **F-87** 2026-09-23: **two ways a generator check can pass while seeing
  nothing**, both found in the same afternoon.

  The plant pass looked for `BLK_GRASS` to find the surface. A desert has no
  grass, so the cactus pass hung off it would have fired **exactly never** --
  and the biome table, the block, the texture and the id would all have looked
  perfectly correct. It looks for the biome's OWN surface block now.

  Worse, because it was in the check rather than the code: the snow share was
  measured along a single strip at z = 0, which contained no ground above the
  snow line at all, so the whole check **skipped in silence**. A check that
  finds nothing to look at reads exactly like a check that passed. It now
  asserts it found enough to measure (`tall > 200`, `wide > 2000`) before it
  believes its own number.

  That second one also forced the right measurement. The snow field is
  deliberately slower than a mountain is wide -- so a summit is snowy or bare,
  not speckled -- which means a strip of terrain samples two or three summits
  and honestly reports 0% or 100%. The share is now taken over 5579 summits'
  worth of high ground using the generator's own predicate, which costs no
  chunk generation at all, while the generated chunks are still used for what
  they can actually prove: that snow exists and lies only where it is allowed.

  Four generator constants have now been swept rather than chosen, each with
  its numbers in the comment beside it and its result printed every run: the
  cave mouths (F-84), the cold edge (F-86), the snow line, and the snow field.

- **F-86** 2026-09-23: **the check that exists for one failure mode nothing
  else can see.** Per-biome height has exactly one way of going wrong, and it
  is invisible to every other check in the file: take the height numbers from
  a LOOKUP by biome id instead of from blended weights, and the shares are
  still right, the surface blocks are still right, the determinism still
  holds, every chunk still matches its neighbour -- and the world is full of
  vertical walls, one at every biome border.

  So worldcheck walks 60000 blocks of line, watches every single step, and
  watches hardest at the steps that CROSS a border, which is where a lookup
  breaks and noise does not. Terrain noise alone steps 1-2 blocks at its
  steepest; a lookup would step by the gap between two biomes' bands, which is
  twenty or more. The check fails over 6, and reports the worst it saw and
  where.

  Two thresholds were swept rather than chosen, and the numbers are in the
  comments beside them: the cold edge (0.38 makes mountains 24% of the world,
  which is not a mountain range but a mountain planet; 0.32 makes them 12%;
  0.26 makes them 5% and hard to ever find), and before it the cave-mouth
  edge in F-84. Both print their result on every run, because a generator
  constant that has drifted looks exactly like one that has not.

- **F-85** 2026-09-23: **the check a table-driven change actually needs is
  "are they different", not "does it compile".** Routing the surface block, the
  soil depth, the tree chance and the plant mix through a `biome_def_t` row is
  easy to do and easy to do WRONG in a way that looks entirely correct: read
  the table everywhere, and still generate the same world three times over
  because every row holds the same numbers, or because one of the five call
  sites still reads a constant.

  So worldcheck generates an 80-chunk strip and counts what comes out of each
  biome: 0.135 logs a column in the forest against 0.059 in the plains (it
  fails below 1.6x), 100% sand and zero grass, plants and trees in the sand
  flats. Plus the share of the world each one takes, with a floor -- a biome
  nobody ever walks through is a row of dead data, and nothing else would ever
  say so.

- **F-84** 2026-09-23: **a check that counts the right number can still miss
  the whole point.** Ore was about 1.2% of stone, which is roughly what it
  should be -- and every block of it was an independent coin flip, so two
  together were a coincidence and there was nothing to follow. The share was
  right and the game was wrong. Any check that measured only density would have
  passed, happily, forever.

  So worldcheck's "ores" section counts **neighbours** as well as share: for
  each ore block, how many of its six faces touch the same ore. Scattered
  blocks score about 0.05. Veins score 3.58 (coal) and 2.97 (iron), and the
  check fails below 2.0 and 1.5. That single number is the difference between
  "there is ore in the world" and "there is a reason to dig sideways".

  The cave-mouth threshold went the same way. The field it reads rarely rises
  above 0.85, so the useful range is a cliff: **0.78 opens 8.7% of the land**
  (a colander), **0.84 opens 1.7%** (about one column in sixty, which reads as
  the occasional hole in a hillside) and **0.88 opens none at all**. Swept and
  measured rather than picked, and the number is printed on every run with a
  band either side of it -- because a future change that quietly seals the
  caves again would otherwise look exactly like success.

- **F-83** 2026-09-23: **a translation helper that dropped keys in silence.**
  The script appending translations to `lang/*.txt` grouped them under four
  known prefixes and wrote nothing for a key outside that list -- so when the
  chest, the bench and the HUD brought `chest.`, `bench.` and `hud.`, 13 keys x
  31 languages went in and nowhere, with no error. `make_lang.py` caught it
  ("13 of 215 untranslated"), which is the only reason it was a round trip and
  not a shipped bug.

  The helper now refuses to write anything unless every key it was given is
  accounted for. A tool that quietly does part of the job is worse than one
  that fails, because the part it did looks like the whole.

- **F-81** 2026-09-23: **the width check only measured labels, so it passed
  while the user was looking at the bug.** F-76 added `check_label_widths()`
  after three settings screens turned out to be overlapping their own text in a
  dozen languages -- and it measured each LABEL against its value column, which
  was the failure of the day. The failure this time was the VALUE: "have 0,
  need 2 more" ran out of the crafting panel and off the right of the screen,
  and the inventory's legend did the same, **in English**, which is the
  shortest language the game ships.

  `check_text_fits()` measures the other side: values, hints and free-standing
  lines against the room each actually gets, worked out the way `se_ui.c` works
  it out, with every format string filled in the worst way the game can fill it
  -- every count a full stack, every `%s` the longest item name in that
  language. It found **five more of the same bug immediately**, two of them on
  the furnace screen, which the user had not reached yet: `64 Wooden pickaxe`
  wanted 326 px in a 190 px column and `makes Wooden pickaxe` 367 px in 198.
  The furnace picker lost its value column as a result -- what a stack would do
  moved to the footer, which is 14 px and has the whole panel.

  The lesson is not "measure text". It is that **a check written for one
  failure tends to measure exactly that failure and nothing next to it**, and
  the cheapest time to widen it is the next time something in the same family
  goes wrong. 30 lines x 32 languages now, tightest 33 px.

- **F-80** 2026-09-23: **two bugs the user reported turned out to be one**, and
  the lesson is worth more than the fix. They saw *"sometimes the cursor keys
  don't work in crafting and furnace menus"* and *"crafting torches took the
  items from my inventory without adding torches"* -- which read as an input
  bug and an inventory bug, in different files, neither reproducible on the
  host.

  `player_t.used_block` says "the player opened something THIS TICK". It was
  set and cleared in the branch of `player_tick` that handles using a block --
  and once a screen is open the player is frozen, so that branch is never
  reached again and **the flag stayed set forever**. main.c dutifully reopened
  the screen every tick. Opening one resets its cursor to the first row, empties
  its search box and swallows the keystroke that opened it, so: the arrows did
  nothing, typing did nothing, and enter crafted whatever was FIRST in the list
  instead of what was under the cursor. Hence materials gone and the wrong thing
  back.

  Only when the screen was opened by USING a block, never by the Craft key --
  which is exactly the "sometimes".

  **A field that means "this tick" is cleared at the top of the tick**, not
  where it happens to be written. It is now, and main.c also refuses to open a
  screen that is already showing: two locks, because the failure is invisible
  and the symptom points somewhere else entirely. The two-ingredient craft path
  the report blamed was then exercised end to end in worldcheck and was correct
  all along -- which is the other half of the lesson, since the obvious suspect
  cost nothing to clear and would have cost a day to keep suspecting.

- **F-77** 2026-09-23: **two ways a furnace could have quietly eaten your
  things**, both found while building it and neither by playing.

  1. **A chunk is only written to the card when `CF_EDITED` is set, and only
     `world_set()` sets it.** Smelting changes what is inside a block without
     changing any block, so a furnace full of coal would have been thrown away
     on the next eviction -- and the player would not have found out until they
     walked back to it, by which time there is nothing to debug. Fixed with
     `chunk_mark_edited()` and `blockent_touch()`, which every put and take
     calls; the comment on `blockent_touch` says why it exists, because the next
     container will need it and nothing about the call site suggests it.
  2. **Breaking a container dropped the container and not its contents.** Fixed
     in `interact_break`, which now spawns every slot before the record goes.

  Neither is the kind of bug a test suite finds by accident, and both are
  unrecoverable for the player, so they are written down rather than merely
  fixed.

- **F-78** 2026-09-23: **the badge has one QWERTY and the game speaks 32
  languages**, which the user spotted as soon as a search box was proposed:
  *"the Tanmatsu always has the same QWERTY keyboard, how will this work with
  other languages?"* A Bulgarian player reading *Кирка* cannot type К -- and
  neither can a Turk type the o-umlaut in *Kömür*, a Pole the stroked l in
  *Łopata* or a Czech the r-caron in *Dřevo*, so it is not only the non-Latin
  scripts. `i18n/fold.{c,h}` folds both sides of the match down to plain ASCII;
  `tools/make_fold.py` derives the table from every character in every lang file
  and **refuses to emit one with a hole in it**; worldcheck walks 73568
  characters across the 32 languages and asserts every one folds. One letter,
  not a digraph (the user's call): a German would type *loeffel* and a Turk
  *komur*, and only one of those can win.

- **F-79** 2026-09-23: **`symcheck` caught a missing source file**, not a
  missing loader export. `main/game/furnace.c` went into the Makefile's pure
  list and `hostpurity.sh` and not into `CMakeLists.txt`, so the host checks
  passed, the app linked, and five `furnace_*` symbols were left for graceloader
  to resolve -- which it could not, and the app would have started and died in
  silence exactly as F-74 did. The check written for one failure caught a
  different one on the same path.


### Findings (F-n), each with date and source

- **F-01** 2026-09-20, `../tanmatsu-showreel-grace/main/mesh.h:29`: `mesh_tri_t`
  is `uint16 a,b,c; uint8 mat; float uv[3][2]`, so there is **one byte of padding
  free** before `uv`. A `uint8_t dir` face-direction field costs nothing, which
  is what makes the direction-grouped back-face cull (G1b) free.
- **F-02** 2026-09-20, `../tanmatsu-showreel-grace/main/mesh_render.c:22-60`:
  `mesh_submit()` transforms every vertex through `xform_apply()` into a PSRAM
  scratch array and runs `tri_faces_point()` per triangle — even for chunk meshes
  whose xform is identity and whose faces are axis-aligned. This is the measured
  14-16 ms.
- **F-03** 2026-09-20, showreel `devdocs/performance.md`: textured fill runs at
  about 5 Mpx/s, so a screen-sized textured layer costs ~50 ms whatever the
  triangle count; flat fill is 3-4x cheaper; the PPA backdrop is free. Quarter
  resolution roughly doubles the frame rate (overview 10.4 -> 20.6, walk 6.3 ->
  15.5). `depth_order` helps voxel scenes; the raycast renderer is 2.5-5x
  *slower* on them. Vsync caps at 30 fps.
- **F-04** 2026-09-20, showreel F-36: all chunks meshed full-detail overflowed
  both lists (4746 flat, 2181 textured); with the LOD ladder, 2262 flat and 910
  textured. Leaves were 58% of near triangles, plants 15%.
- **F-05** 2026-09-20, `synthengine3D/include/se_save.h`: the save framework is
  **numbered slots** (`SE_SAVE_SLOT_COUNT` 3) in one directory. It does not fit
  many named worlds, so worlds use `se_nbt.h` directly on our own paths.
  `se_nbt.h` also has **no byte array** type (int32/int64/double/string/compound
  only), so chunk payloads need their own container.
- **F-06** 2026-09-20, `nm -D fakelib/liball.so`: **not exported** — `opendir`,
  `readdir`, `remove`, `unlink`. **Exported** — `f_opendir`, `f_readdir`,
  `f_unlink`, `f_rename`, `xTaskCreatePinnedToCore`, `esp_random`, `crc32`, and
  (correcting an earlier assumption) **`fsync`**. Directory work and deletion go
  through FatFs; the dual-directory region scheme is kept anyway, because a FAT
  driver's power-loss semantics are not worth betting a world on.
- **F-07** 2026-09-20, `voxel_mesh.c`: the mesher is already pure data over a
  borrowed `vox_grid_t`. Its only coupling is the `kind()` and
  `voxel_face_mat()` switches over `vox_block_t`; replacing those two with table
  lookups is the whole change, and the `vox_grid_t` ABI is preserved.
- **F-08** 2026-09-20, `voxel_render.c:121`: `build()` meshes through a **shared
  static scratch grid** (`s_grid`, filled by `fill_fine`/`fill_coarse`). That
  races the moment a second task meshes, so the core-1 worker must own its own
  scratch buffer.
- **F-09** 2026-09-20, Part X: the far-lands path needs a 3D density evaluation
  per cell (16384 a chunk) against 256 heightmap columns for normal terrain —
  about 50x the work. Mitigated by a 4x4x4 lattice with trilinear interpolation
  (425 samples a chunk), shared with cave carving. Untested on this hardware;
  the biggest worldgen performance unknown.
- **F-10** 2026-09-20, `voxel_world.c:56`, measured: the donor's lattice noise
  hashes a point as `hash01(ix * 7919 + iz * 104729, seed)` -- two coordinates
  folded into one `int` key. Two concrete defects, both latent in a 128x128
  world:
  - **It aliases.** Both factors are prime, so `(ix, iz)` and
    `(ix + 104729, iz - 7919)` produce the identical key and therefore the
    identical terrain. In *lattice* units, which at a terrain scale of ~32
    blocks puts the twin about 3.4 million blocks away -- far enough not to
    matter for the heightmap.
  - **It overflows.** `iz * 104729` leaves `int32` beyond `|iz| = 20505`, and
    `ix * 7919` beyond `|ix| = 271181`. At terrain scales those are millions of
    blocks out, but the Far Lands and the cave carver sample a **3D density
    field at roughly block resolution**, where the lattice index *is* the world
    coordinate. There `|z| = 20505` blocks is well inside ordinary play.
  An earlier draft of this finding claimed the donor hash collides in a local
  block near x = -100000; measured, it does not (0 collisions in 40000 points),
  because the smallest aliasing offset is 104729. The replacement
  (`common/rng.h`, SplitMix64 over separate 64-bit lanes) is still the right
  fix -- it cannot alias or overflow at any `int32` coordinate -- but the
  reason is the density field, not the heightmap.
- **F-11** 2026-09-20, `synthengine3D/include/se_config.h:127`: `SE_BINDINGS_MAX`
  is **16**, and SynthMiner declares about 20 actions. It **clamps silently**, so
  the last actions would simply not work. Raise it in CMake *and* add a
  `_Static_assert(ACT_COUNT <= SE_BINDINGS_MAX)` in `controls.c`.
- **F-12** 2026-09-20, `voxel_mesh.c:230`: the mesher still `calloc`s its greedy
  mask from the default heap, i.e. internal SRAM, and it will run on the worker
  task. Only about 1 KiB at `CH_H = 64`, but internal SRAM is ~150 KiB free with
  a 62 KiB largest block — hoist it to worker-owned PSRAM scratch.
- **F-13** 2026-09-20, showreel `mesh.h`: `mesh_t` caps at 65535 vertices. A
  greedy-merged 16x16x64 chunk stays far below, but a Far Lands wall chunk is
  unusually face-dense — `worldcheck` asserts no generated chunk exceeds 40000
  vertices at any LOD.

- **F-14** 2026-09-20, step 0.2: `tools/make_textures.py`, trimmed to
  SynthMiner's twenty generators and re-keyed to flat names, regenerates all
  twenty PNGs **byte-identical** to the showreel's committed ones. The
  generators are seeded per texture, not off a shared stream, so dropping the
  sixteen space textures moved none of the others.
- **F-15** 2026-09-20, step 0.3: after `voxel_mesh.c`'s two block switches
  became `BLOCKS[]` lookups, all eleven of the showreel's mesher cases pass
  with its exact triangle counts, and every case's volume equals its solid
  cells and surface area its exposed faces. The conversion is
  behaviour-preserving, which is what made it safe to do before anything else
  is built on the mesher.
- **F-17** 2026-09-20, step 1.2: the load-order-independence check is only
  meaningful if decoration actually crosses chunk borders, so it counts what
  crosses: chunk (0,0) at the test seed has 122 leaf cells, **71 of them on an
  edge**. Generating the 3x3 neighbourhood first and then the middle chunk
  again gives a byte-identical chunk, so `stamp()`'s
  walk-the-neighbourhood-and-clip approach holds.
- **F-18** 2026-09-20, step 1.2, measured over 327184 columns: the height
  field is a single bell from y17 to y38 centred on y27-28, with 31.3% of it
  at or below sea level (y24). That is a reasonable land/water split, but the
  relief is unimodal — there are no distinct plains and mountains, because two
  added fbm stacks tend to a bell. If the world reads as samey when played, a
  ridged or terraced third term is the lever; noted rather than pre-optimised.
- **F-16** 2026-09-20, step 0.5: the cross toolchain is at
  `/home/cavac/idf/tools-6.0`, found through the `IDF_TOOLS_PATH` environment
  variable. The repository has no `.IDF_TOOLS_PATH` file (nor do the showreel
  or synthracer), so builds here pass it explicitly.

- **F-19** 2026-09-20, first device session: the two bridge ports behave
  independently, and the badgelink one can be down while the console is fine.
  Observed: `localhost:4001` (console) negotiates and the badge answers
  `BADGELINK` with `OK switching to badgelink mode` /
  `I (51877) USB device: Switching to BadgeLink USB mode`; `localhost:4003`
  (badgelink) accepts the TCP connection and **closes it immediately**, before
  any protocol, so every `fs` operation dies with `ConnectionResetError`. The
  badge is in the right mode; the proxy is not serving. This is the showreel's
  F-33 seen again, and it is infrastructure, not the app -- the standing rule
  applies: stop and ask, do not work around it.
- **F-20** 2026-09-20, first device session: `ConnectionResetError` from
  badgelink also means "an app is running and holding the USB link", which is
  indistinguishable from F-19 at the tool level. `tools/recover.py` reported
  "no app answered after the reset (probably in the launcher)" while SynthMiner
  was in fact still running, so its guess is not evidence. Check by exiting the
  app before concluding anything about the bridge.
- **F-21** 2026-09-20: `pyserial`'s `rfc2217://` client intermittently fails
  the handshake against this bridge (`Remote does not seem to support RFC2217
  or BINARY mode`) even though the server offers BINARY, SGA and
  COM-PORT-OPTION. `tools/testrun.py` already handles this -- it opens with
  `do_not_open=True`, sets RTS then DTR low, and retries the whole connect
  (its own F-16/F-18). Anything new that talks to the console should go
  through that path rather than opening the port itself.

- **F-22** 2026-09-20, measured on the badge (`results/20260920T201508Z-perf-block/`):
  after `se_run()` has booted the display, audio, input and scene --
  i.e. with both framebuffers, the depth plane and the geometry lists already
  allocated -- **PSRAM: 28796 KiB free, largest block 28672 KiB**. The chunk
  slab takes 8192 KiB (256 slots x 32 KiB) and leaves **20604 KiB free,
  largest 20480 KiB**. Internal SRAM is 160 KiB free / 62 KiB largest both
  before and after, matching the showreel exactly (the slab is PSRAM-only, as
  intended). So the residency radius of 6 is comfortable with about 20 MiB to
  spare, and the planned ~3.2 MiB of chunk meshes fits easily. Nothing here
  forces `CH_H` down or the ring smaller.
- **F-23** 2026-09-20, measured on the badge: **one chunk generates in 56 ms**
  (16 x 16 x 64 cells, `worldgen_chunk`, including trees and plants). On the
  host the same call is 0.519 ms, so the badge is about 108x slower, which is
  in line with the rest of the port. What that means for play:
  - **steady-state walking is fine.** Crossing a chunk boundary needs about
    13 new chunks, i.e. 0.73 s of work, and a player at 4.3 blocks/s crosses
    one every 3.7 s -- a 20% duty cycle on core 1, before meshing.
  - **filling a fresh residency is not.** 169 chunks is about 9.5 s. That is
    a one-time cost at world creation, where it belongs and where it is
    expected (D-25); afterwards the spawn area is on disk and is *loaded*, not
    generated. The 3 x 3 the first tick needs (D-19, D-26) is about 0.5 s of
    generation and less than that of loading, so entering a world is not the
    problem; the problem was only ever the first fill.
  - the lever, if it is ever needed, is F-09's 4 x 4 x 4 density lattice with
    trilinear interpolation. The cave field is two `sm_noise3` calls per stone
    cell below the surface and dominates the cost. **Not done yet** -- 56 ms is
    survivable and meshing has not been measured, so the two get optimised
    together or not at all.
- **F-24** 2026-09-20, the first green cycle: `make cycle` end to end gives
  **30.0 fps** on the placeholder block (300 frames, rast 2.74 ms mean /
  3.09 max, submit 0.09 ms), vsync-capped as the showreel found. The app
  returned to the launcher by itself, so the loop really is hands-free.

- **F-25** 2026-09-20, step 1.3, host checks: the region format's durability
  claims are tested, not asserted. Destroying directory copy A still reads the
  chunk; destroying both reports an error rather than handing back rubbish; a
  corrupted payload reads as "no chunk" (so it is regenerated) and leaves
  nothing half-written. The **torn-write** case is the important one and it
  passes: after two saves, destroying the copy the second save wrote returns
  the **first** save intact, and the region is still writable afterwards. A
  chunk packs to 3962 bytes from 32768; compaction took a region from 339022
  to 29306 bytes with every chunk preserved and no stale copy resurrected.
- **F-26** 2026-09-20, measured on the badge's SD card: **write 21.5 ms,
  read 6.6 ms** for one chunk, round trip identical. **Loading is 8.54x
  cheaper than generating** (6.6 ms against 56.3 ms), which is the number
  D-25 and D-26 were betting on, so the whole "pre-generate once, load ever
  after" shape holds. What it means:
  - **Creating a world**: 169 chunks x (56.3 gen + 21.5 write) = about
    **13 s** behind the progress bar. Long, but once, and expected.
  - **Loading one**: 169 x 6.6 ms = about **1.1 s** for the whole view
    distance; the 3 x 3 the first tick needs is **59 ms**, i.e. instant.
  - **Walking through explored land** is essentially free: 13 chunks a
    boundary is 86 ms of reading, against 730 ms if it has to generate.
  - Saving on eviction at 21.5 ms a chunk is cheap enough to stay off the
    frame budget entirely.
  - Compaction is 43 ms and works on the card.
- **F-27** 2026-09-20: `make verify` **cannot** catch a missing symbol in code
  nothing calls -- `--gc-sections` removes it from `app.so` first, so the check
  passes and the app still fails to LOAD on the badge. The region and
  compaction paths only started referencing `f_unlink` / `f_rename` once
  `main.c` actually called them. Anything that exists to work around a missing
  export has to be exercised on the device, not merely compiled.

- **F-28** 2026-09-20, step 1.4: the compatibility guarantee is tested in both
  directions rather than assumed. A record written by a "newer build" (a cow
  with `aggressive`, `sitting`, a nametag and a breeding timestamp) is read
  correctly by an "older" reader that knows only position and health; a record
  written by the older one is read by the newer with every absent field at its
  default, not at rubbish. Skipping a nested compound lands exactly on the next
  field, which is the case a furnace's inventory needs.
- **F-29** 2026-09-20, step 1.4: `level.smw` names all 17 blocks in its palette,
  and a remap applied at decode moved 6361 stone cells in a test chunk. A block
  the remap drops becomes air rather than whatever now sits at that number.

- **F-30** 2026-09-20, step 2.1: the first streaming loop drew an empty world
  at a confident 30 fps. `do_load()` reached for its chunk with `chunk_find()`,
  which deliberately hides a chunk while it is `CS_LOADING` so no game code can
  read a half-filled one -- and the loader is the code doing the filling. Added
  `chunk_slot_claimed()` for the one caller that is allowed to see it, and a
  host streaming check that reproduces the bug when the fix is reverted. Nothing
  else in the suite noticed; an empty world is not a crash.
- **F-31** 2026-09-20, step 2.4, measured: **the engine's framebuffer clear was
  13 ms a frame.** With no `on_backdrop` registered, `se_run` clears the whole
  800x480 buffer to `backdrop_argb` every frame -- and the quarter-resolution
  upscale then covers every pixel of it. Registering an empty `on_backdrop` took
  the residual from 14.4 ms to 1.3 ms and the frame rate from 11.5 to 16.0 fps.
  It showed up as unattributed time, not as any phase.
- **F-32** 2026-09-20, step 2.4, measured: submit costs about **4.4 us per
  triangle submitted**, and it is the engine's projection, not the cull. The
  cull is working: 8886 triangles tested, 4356 passed. What the profile actually
  said was that far too many are being submitted -- medium view distance put
  ~4400 a frame past the engine's 4096 flat cap, where the overflow is **dropped
  silently**. Deferring the second and third vertex loads past the cull, and
  caching the fog palette per step instead of per chunk, moved submit by under a
  millisecond: both were the wrong suspects.
- **F-33** 2026-09-20, step 2.4, measured on the host: **68% of a chunk's
  triangles are cave walls**, none of them visible from the surface. Chunk (0,0)
  meshes to 1094 triangles, of which 744 sit well below the surface; filling the
  caves in gives 426, a 2.6x reduction. That is why 15 visible chunks submit
  ~7000 triangles for ~1000 drawn.
  **The fix is vertical render sections** (D-34): a 16 x 16 x 64 chunk is one
  mesh with one bounding box spanning bedrock to sky, so the frustum test cannot
  reject the underground half. Splitting it into 16-high sections, each with its
  own box, lets the cull drop what is beneath the ground the player is standing
  on. This is what the original plan called for (a `y0` field on `vox_grid_t`)
  before D-11 traded it away for keeping the donor mesher untouched; the trade
  is measurably worse than expected.

  **CORRECTED 2026-09-21 by F-35. The 68% figure does not survive a wider
  sample, and the single chunk it came from was a bad one to have picked.**
  Chunk (0,0) is sea floor: it meshes to 346 triangles, not 1094, and 344 of
  them are in one section. The finding should never have been stated from one
  chunk. Sectioning is still right, for the reasons F-35 gives -- but not for
  the reason given here, and not by the factor claimed.

- **F-35** 2026-09-21, step 2.5, measured on the host over **49 chunks sampled
  across 4000 blocks of world** (not one chunk, which is what went wrong in
  F-33) and on the badge:

  | where the triangles are | share |
  |---|---|
  | y 0-15 (underground, caves) | 34% |
  | y 16-31 (the surface band) | 47% |
  | y 32-47 (hills above it) | 17% |
  | y 48-63 (sky) | 0% |

  Only 6 of the 49 chunks have 90% or more of their triangles in a single
  section, so **the geometry really is spread over three sections** and a
  frustum test per section has something to reject. But it is 34% underground,
  not 68%, and a box test is conservative -- the near chunks' underground
  sections are partly in view from three blocks above the ground.

  On the badge, same camera path, same 20-second window: **12.32 and 12.83 fps
  without sections, 13.95 and 13.25 with** -- about **8% faster**, rasterise
  51.2 -> 49.9 ms, ~15% fewer triangles submitted. Two runs each; the spread
  within one build is ~5%, so this is a real gain but a modest one, and it is
  smaller than F-33 promised.

  The terrain itself is fine and was briefly suspected of not being: over 94864
  samples the height runs y14..41, median 27, 24% at or below sea level, which
  matches what step 1.2 recorded. **The origin is simply ocean**, which is why
  the one chunk F-33 measured looked the way it did.

  Sectioning earns its keep in two further ways that are not in the frame time:
  a section is only meshed when it is about to be drawn, so the underground of
  a chunk you never look into is **never built at all**; and a block edit now
  dirties 4096 cells instead of 16384, which is what break-and-place in step 3.5
  will pay for.

- **F-36** 2026-09-21, step 2.5: **the 16.0 fps in F-31 was measured with the
  camera pointing at the sky.** Two bugs, both in the debug flight and both
  invisible until the camera became steerable:
  * the engine's forward vector is `(sin yaw, cos yaw)` in x and z, and the
    scripted flight's `yaw = a + pi/2` matched *neither* component of its own
    direction of travel -- it had been flying sideways since it was written.
    The yaw that matches both is `-a`.
  * **positive pitch looks DOWN** (`se_scene.c`, `camera_build_basis`:
    `fwd.y = -sin pitch`). The flight passed `-0.18`, tilting it up into empty
    sky, which is cheap to fill.
  Corrected, the same build measures 12.3 rather than 16.0. Every frame rate
  recorded before this one is optimistic by roughly that much. The convention
  is now written down at both places that use it.

- **F-34** 2026-09-20, step 2.4 (**its speculation is corrected by F-40**; the
  fill rate is set by span setup, not by scalar arithmetic): the engine's
  rasteriser is **scalar C**. No
  PIE/SIMD appears anywhere in `synthengine3D/src/se_scene.c`; the engine's only
  SIMD mention is minimp3's x86/ARM paths, disabled by `MINIMP3_NO_SIMD`. The
  ESP32-P4 has a 128-bit SIMD unit whose 16-bit lane arithmetic and saturating
  ops match what a textured span loop does. So the measured ~5 Mpx/s is the
  speed of this implementation rather than of the hardware. **Unprofiled** --
  how much headroom that represents is not known and is not worth guessing.

- **F-37** 2026-09-21, the user asked where the internal SRAM goes: **the app's
  own statics are in PSRAM, not internal SRAM.** kbelf loads `app.so` there --
  a static probe sits at `0x4801e6f0`, in the same region as a PSRAM
  allocation, while an internal allocation is at `0x4ff37ee8`. So `app.so`'s
  224 KiB of `.bss` costs no internal SRAM at all, and **the 35 KiB that D-34's
  commit message claims to have taken "off the internal-SRAM bss" was PSRAM**.
  The saving is real; the memory it came from was misnamed.

  What is actually using internal SRAM: **462 KiB of 622 KiB, in 301 blocks,
  all of it allocated before `on_init` runs** -- ESP-IDF, graceloader and the
  engine's boot. Low-water equals free, so nothing has been released since. The
  app itself takes about 18 KiB (the worker's stack and queues, the texture
  cache). 160 KiB free, largest block 62 KiB.

- **F-38** 2026-09-21: **the block textures fit in internal SRAM with room to
  spare, and it makes no measurable difference.** All eighteen are 16x16 RGB565
  -- 512 bytes each, **9216 bytes for the set** -- against 160 KiB free, so no
  freeing was needed for the move the user asked about. Textured fill measured
  36.0 ms a frame with them internal against 35.7 and 37.0 with them in PSRAM:
  **a null result**, inside the run-to-run spread. The reason is in F-40: the
  texel fetch is not what the loop is waiting for. Kept anyway, because 9 KiB
  is nothing and the argument only gets better as textures are added.

- **F-39** 2026-09-21: **the engine and the app were both built `-Os`**, and at
  `-Os` the compiler would not inline `scene_index()` or the three span
  functions despite their `static inline` -- so every one of 28000 spans a
  frame paid a function call. `-O2` on the engine: flat fill 22.0 -> 19.4 ms,
  textured 37.7 -> 34.7. `-O2` on the app as well: submit 11-13 -> 10.2 ms.
  `.text` grew 71 -> 87 KiB, which is PSRAM and therefore free (F-37).

  And **`ceilf`/`floorf` are library calls even at `-O2`**: they must set
  `errno`, so GCC cannot fold them into the single RISC-V convert the value
  needs. The column scans call them twice per span -- **57000 library calls a
  frame** to round numbers already in float registers. Replaced with inline
  `ceil_i`/`floor_i`: flat 19.4 -> 13.4 ms, textured 34.7 -> 31.7.

  Altogether **rasterize 49.7 -> 43.6 ms and 13.4 -> 15.3 fps**, with no SIMD
  written.

- **F-40** 2026-09-21, the answer to "would SIMD help?", measured rather than
  argued. The chain of measurements matters as much as the conclusion, because
  the first two readings each pointed the wrong way:

  | measured | result |
  |---|---|
  | flat vs textured, per pixel | 230 vs 214 ns -- textured is **not** dearer |
  | span-loop memory pattern alone, PSRAM | 55 ns/px (internal SRAM: 39) |
  | the same with a 16-bit depth plane | 56 ns/px -- **no change** |
  | the loop's arithmetic alone | 45 ns/px (16 cycles at 360 MHz) |
  | the real rasterizer | 172-199 ns/px (62-72 cycles) |
  | **average span length** | **6.0 pixels flat, 12.5 textured** |

  Read in order: textured costing the same as flat per pixel says the
  arithmetic is not the wall. A 16-bit depth plane not helping says the bytes
  are not either -- the memory cost is per-access latency, not bandwidth, so
  halving the depth plane would have bought nothing and the engine change it
  would have needed was avoided. Arithmetic (16 cyc) plus memory (20 cyc) is
  **36 of the 62-72 cycles a pixel costs**, so the rest is neither: it is
  per-span setup, over spans **six pixels long**.

  **So SIMD is the wrong tool here.** The ESP32-P4's PIE vector unit is real,
  128-bit with 16-bit lanes, and this toolchain already enables it -- the app
  compiles with `xesploop_xespv2p1` today, and the assembler accepts
  `esp.vld.128`, `esp.vadd.s16`, `esp.vcmp.gt.s16` and the rest. But a 6-pixel
  span does not fill one 8-lane vector; the depth test needs a per-pixel
  conditional store; and the textured loop's texel fetch is a **data-dependent
  gather**, which PIE has no instruction for. Vectorising the inner loops would
  attack the 16 cycles that are already the cheapest part.

  **F-34 is therefore wrong where it speculates.** "~5 Mpx/s is the speed of
  this implementation rather than of the hardware" is true, but its implied
  cause -- scalar arithmetic in the span loops -- is not. The fill rate is set
  by how many spans the geometry breaks into, and the fix is fewer and longer
  spans (bigger on-screen triangles, more aggressive distance LOD), which is
  game-side work.

  The measurements live in `main/game/membench.c` and in the engine's
  `scene_fill_stats()`, so any of this can be re-checked rather than believed.

- **F-41** 2026-09-21, **the user, flying by hand**: "it sometimes seems to
  reload all the chunks". Three separate causes, found by logging the
  streaming's flow per second (`stream/s:` in the app log) rather than by
  reading the code:

  1. **The far view preset did not fit the chunk ring, and this one really
     does reload everything.** `evict_radius` 8 keeps a 17-chunk square; the
     ring is 16 across and a slot is the low four bits of the coordinate, so
     two resident chunks land on the same slot. Each evicts the other, each is
     then read as `BLK_BARRIER` and requested again, **forever, even while the
     player stands still.** `CH_RING`'s own comment said "a residency radius up
     to 7" and the preset asked for 8. Fixed to 7, clamped in
     `chunk_render_set_view()`, and `_Static_assert`-ed against `CH_EVICT_MAX`
     so the build stops rather than the world thrashing -- verified by putting
     8 back and watching it fail to compile.

  2. **A level-of-detail change drew nothing until its mesh arrived.** Each
     level is a separate mesh, so crossing a distance band asks for one that
     has never existed. Flying *upwards* moves every chunk into `LOD_COARSE` at
     once: ~160 section meshes that do not exist, at ~30 applied a second. The
     renderer now falls back to whichever level IS built while the wanted one
     is queued. A frame at the wrong detail is not noticeable; a hole in the
     ground is.

  3. **The result budget had not kept up with D-34.** `CHUNK_RESULTS_PER_FRAME`
     was 2 -- about 30 a second -- chosen when a chunk had 3 meshes. Sectioning
     made it 12, and a fast flight wants ~90 a second. The worker then fills
     its 48-deep result queue and **blocks**, which stops loads as well as
     meshes. Measured at startup: `asked 80, applied 36, queue 44/48`. At a
     budget of 8: `asked 82, applied 82, queue 0/48`.

  Causes 2 and 3 are **regressions I introduced with D-34** and did not think
  to look for: sectioning multiplied the number of mesh jobs by four and I left
  every budget around it alone.

- **F-42** 2026-09-21, step 3.1: the first step-up test **passed while the code
  was wrong and then failed while it was right**, because it checked the body's
  height after a walk that had already crossed the step and dropped off the far
  side. A physics assertion has to name the moment as well as the value. The
  test now walks onto a plateau wide enough to end standing on it.

- **F-43** 2026-09-21, **the user, playing it**: four faults, and three of them
  were invisible from reading the code.

  1. **The jump could not clear a block.** Apex **0.83 blocks**, so pillaring up
     -- how you get out of a hole -- was impossible. Cause: gravity was applied
     BEFORE the move, so the first tick of the jump was spent decelerating
     instead of rising. The same three constants give 1.25 the other way round.
     Nothing in the code looks wrong; it took simulating the arc. The order now
     lives in `phys_gravity()`, which the player and the host test both call,
     and the test asserts the apex in blocks and the hang time in seconds.
  2. **Too fast to aim.** At 15 fps a Minecraft jump is nine frames from
     take-off to landing. Retuned to the same apex over a longer arc -- the
     pair `(v0 * k, g * k^2)` with k = 0.7 -- giving **1.33 blocks, 0.85 s**.
  3. **You could see through a wall you were touching.** The engine's near clip
     plane is **0.5** and the player's eye is **0.3** from a wall they are
     flush against and **0.18** below a ceiling they stand under. Both were
     being clipped away. Now 0.1, with a `_Static_assert` tying it to
     `PHYS_PLAYER_W` and `PHYS_PLAYER_EYE` so it cannot drift back --
     verified by restoring 0.5 and watching the build fail. The cost is depth
     range and precision, both proportional to the near plane: the far limit
     falls from 32000 blocks to 6400, still ninety times what this draws.
  4. **Breaking a block blanked the surrounding area for a frame.** An edit
     marks every level of its section stale, and the renderer treated stale as
     undrawable, so the chunk disappeared until the worker caught up.
     **"Stale" and "never built" are not the same state** and conflating them
     was the bug: `chunk_t` now carries `lod_built` alongside `lod_stale`, a
     mesh one block out of date goes on being drawn while its replacement is
     queued, and only a mesh that has never existed is skipped.

- **F-44** 2026-09-21, the user: **aiming was guesswork** -- no crosshair, no
  highlight. Two things came out of adding them.

  **The crosshair does not go in the middle of the screen.** The engine
  projects the camera's forward axis to `(RENDER_HALF_W, RENDER_HORIZON_Y)`,
  and `RENDER_HORIZON_Y` is **256 on a 480-row display**, not 240 -- a game can
  move its horizon (se_config.h). Drawn at the geometric centre it would sit
  16 pixels below where the pick actually points, which is an aiming error
  nobody would think to suspect. Derived from the projection constants instead.
  Verified by pulling the framebuffer off the badge and looking at it
  (`badgelink fs download` on a `shots` PNG).

  **The picker reported `BLK_BARRIER`.** An unloaded chunk is deliberately
  solid so the player stops at the edge of the world rather than falling out of
  it (D-14) -- but it is not a block, and the picker was happy to report one,
  which put a highlight box round a piece of fog and offered to mine it. Found
  by accident: a throwaway raycast from the scripted camera drew a box at
  point-blank range and it came out as a black line across the whole screen.
  `ray_pick` now stops at a barrier without reporting a hit, and the host test
  asserts both halves -- solid to the body, invisible to the picker.

- **F-45** 2026-09-21: **the `shots` test cannot yet photograph the world.** It
  SETS the clock rather than running it, so only a few frames render and the
  chunks never stream in -- every shot is empty sky. That is the async chunk
  worker, and the fix is the synchronous mode D-15 put there for exactly this,
  switched on for the duration of a shots run. Not done: it belongs with the
  replay work deferred out of step 3.3, and until then shot hashes cover the
  overlay but not the world.

- **F-46** 2026-09-21, step 4.3: **the overlay cost 13.4 ms a frame through
  PAX** — fifteen per cent of the frame, for about 120 rectangles covering some
  14000 pixels. At the memory speeds this hardware actually has (F-40) those
  pixels are worth about 1.5 ms, so the rest was per-call overhead: a matrix, a
  clip, a shader dispatch and a function-pointer setter, per rectangle.

  Redrawn through `se_direct565.h` — which is public for exactly this reason —
  it is **0.8 ms**, a 16x cut, and the screenshot is pixel-identical. A
  rectangle becomes one contiguous halfword run per column, because the display
  is rotated and a vertical run is what is contiguous in memory.

  It was found only because the phase split did not add up: 71.7 ms of phases
  in an 86.7 ms frame. `PROF_HUD` now exists so the next person does not have
  to notice a subtraction.

- **F-47** 2026-09-21, **the user asking "how do I drop items?"**: G was bound
  and wired, and it **appeared to do nothing**. The item left the inventory and
  was collected again half a second later.

  The arithmetic makes it unavoidable rather than unlucky: the pickup radius is
  1.4 blocks and nothing can be thrown further than an arm's length in one
  tick, so a dropped item ALWAYS lands inside it. Distance was never going to
  be what made G work -- **the pickup delay is**. A block's drop needs 10 ticks
  (so breaking the floor under you does not snatch it back); a thrown one needs
  40, which is two seconds and Minecraft's number.

  Now thrown with velocity from eye height rather than placed in a cell, so it
  arcs, slides and comes to rest **1.7 blocks clear** -- an item you have to
  walk back to. The host test asserts both halves: that standing on it for 39
  ticks does not collect it, and that walking to it does.

  Worth noting how it was found: not by testing, but by a question. The feature
  was "done", host-tested, and committed.

- **F-48** 2026-09-21, step 5.1: **half the title screen was invisible, and
  four wrong explanations were tested before the right one.** "SynthMiner" is
  written in real blocks in the world; "Miner" drew and "Craft" did not.

  Ruled out, each by measurement rather than argument: the blocks were not
  placed (216 of 216 were, and a printed map of the world showed the whole
  word); the chunks were not resident (`chunk_find` returned all four); the
  meshes were not built (140/222/142/78 triangles in the right sections); the
  geometry was culled (nothing was); the engine's lists overflowed (a new drop
  counter said zero); it was a `shots` warm-up artefact (three captures of the
  same instant hashed identically).

  What fixed it: **raising the letters and the camera**. The sight line from
  the old camera to the missing letters passed a few blocks over the treetops,
  and something along it was writing depth without being visible -- distant
  terrain flat-shaded towards a fog colour that IS the sky colour is the
  obvious candidate. **Not confirmed.** The fix is empirical and the cause is
  recorded as unproven rather than dressed up.

  The lasting value is in the two things added while chasing it, both kept:
  `scene_drop_stats()` and pre-generation.

- **F-49** 2026-09-21: **the engine's geometry lists dropped silently.** A full
  list returns without drawing, in submission order, so what vanishes is
  whatever the game submitted last -- a corner of the world, a chunk, half a
  title. That is a hole in the picture that looks like a bug in the game, and
  it had already cost one debugging session (F-32) before it cost another here.
  `scene_drop_stats()` now counts it and the app warns; **anything non-zero is
  a hole**.

- **F-50** 2026-09-22, step 6.1: **the engine's key capture could not bind the
  arrow keys.** `se_ui_capture_key` refused every escaped scancode (>= 0xE000)
  and mapped only F1-F12 from navigation events -- and the cursor keys are
  SynthMiner's default look keys, so once rebound they could never be bound
  back. First worked round with a capture of the game's own; **the user asked
  for the engine to be fixed instead** and synthracer's menu code to be used
  as it is. Fixed in the engine (2.1, `src/se_run.c`, `se_bindable_scancode`):
  escaped grey keys bind as their scancodes, and navigation-only cursor, Home,
  End and Page keys map onto the same scancodes, so a key binds to one value
  whichever keyboard pressed it.
- **F-51** 2026-09-22, step 5.10: **saves from before `placed` need a rule, and
  there is a sound one.** Those builds wrote the player on creation (x and z at
  the spawn column's centre, 0.5, 0.5) and on leaving (where they really were),
  and at no other time. So a save whose position is anything but that default
  is a real position. The one wrong answer this can give is a player who left
  standing on exactly (0.5, 0.5) — who then lands on the ground at the same
  column, which is where they were anyway, give or take a cave.
- **F-52** 2026-09-22, found while writing 5.11: **re-saving level.smw rewrites
  the palette as today's, but untouched chunks keep yesterday's ids.** The
  palette is written from the current registry on every save, while a chunk on
  the card is only re-encoded when it is edited. The day a block id moves, the
  next save relabels every unedited chunk with the new numbering. Harmless
  until then — no id has ever moved — but D-31's promise does not hold across
  two saves. The fix is for a chunk to carry the palette generation it was
  written with, or for a renumbering to rewrite every region. Not fixed here;
  recorded so it is fixed before the first renumbering, not after.
  **Closed 2026-09-22 by D-74:** ids never move, so there is no renumbering
  for it to go wrong in, and `make check` enforces that.
- **F-54** 2026-09-22, reported by the user: **the inventory cursor walked the
  rows upside down.** The Tab screen draws the hotbar at the bottom and the
  storage rows above it, but the cursor moved in slot order, where the hotbar
  (slots 0-5) comes first: up from the hotbar went nowhere, and the bottom
  storage row was reached by going down from the top edge. Now the cursor moves
  in screen rows through `inv_screen_row()`, which the screen also draws with,
  so the two cannot disagree again. Host-tested: up from the hotbar lands on
  the row above it, both edges clamp, and walking the grid reaches every slot
  exactly once. Block 4's tests covered stacking and swapping but never
  movement, which is how it shipped.
- **F-55** 2026-09-22, reported by the user: **the gyroscope's pitch sign was
  wrong; its yaw sign was right.** Both came from the axis diagram in
  `graceloader_imu.h`. Turning left and right worked first time; tipping the
  badge looked the wrong way up. Flipped (`GYRO_PITCH_SIGN`), not investigated
  further -- whether the diagram or my reading of it is wrong is worth
  checking before the next app trusts it for pitch.
- **F-53** 2026-09-22: **`strtoll` is not exported by the graceloader.** It
  linked, and `make verify` caught it before the badge did. The seed parser
  does its own decimal.

- **F-56** 2026-09-22, reported by the user: **a placed torch could not be
  picked up.** The crosshair's ray stopped only at SOLID blocks, and a torch is
  not solid, so it could never be aimed at -- nor could a flower or tall grass.
  The ray now stops at anything but air and liquids (so water still never hides
  the riverbed), and placing onto tall grass replaces it as the highlight says.
  Host-tested both ways.
- **F-57** 2026-09-22: **Minecraft's night (sky light minus 11) is unreadable
  on this screen.** Measured by photographing the title at midnight: the
  letters were barely there. Minus 9 leaves a moonlit field dim but legible and
  still far darker than torchlight.
- **F-58** 2026-09-22: **lighting a chunk as it arrived cost 23 ms on the main
  task** -- title pre-generation went 2757 -> 4593 ms. Three wastes removed
  (marking meshes of a chunk nobody has drawn, seeding the sky flood from every
  lit cell, re-flooding the neighbours' lit cells) brought it to 8.4 ms, and
  splitting the work -- a chunk's own light on core 1 while it loads, only the
  border exchange on the main task -- to about 5 ms a chunk in total, most of it
  off the frame path.
- **F-59** 2026-09-22: **settling the player after the frame's ticks undid
  them.** Entering a world puts the player back at their saved position on the
  first frame their chunk is resident; that ran AFTER the ticks, so it threw
  them away. Invisible in play (one frame), fatal to a test that jumps the clock
  and runs a hundred ticks in that frame. Now it runs before.
- **F-60** 2026-09-22: **the frame that finished loading was drawn with a
  camera nobody had set** -- on_update returned early for the loading step, and
  a test photographed exactly that frame. Now the finishing frame carries on.
- **F-61** 2026-09-22, found while adding light: **an edit on a chunk border
  could lose the neighbour's remesh.** world_set marked the neighbour stale
  without moving its edit_seq, so a mesh of it already in flight came back,
  was accepted, and cleared the stale bit. `world_mark_dirty` now bumps edit_seq
  on every chunk it marks.
- **F-62** 2026-09-22: free PSRAM at boot is now about 13 MiB, down from 20:
  the light plane adds 4 MiB to the chunk slab and the two flood contexts 1.5 MiB.

- **F-63** 2026-09-22, reported by the user as a critical bug: **nearby
  terrain vanished and the player seemed to walk into steps.** The engine's
  geometry lists overflowed and dropped triangles in submission order -- which
  is slot order, not distance -- so whole patches near the player went missing,
  a step among them. Two causes stacked: the medium view (already over the flat
  cap before lighting, the reason near had been the default) and lighting,
  which splits merged faces (+46% triangles in the fancy meshes, +16% fast,
  +10% coarse, measured on the user's terrain). It went unseen because the
  engine only counted drops that happened during near-plane clipping; the
  common case, a whole triangle arriving at a full list, returned without
  counting. The walk that showed it submitted 6000-8400 triangles a frame
  against caps of 4096 + 2048.
- **F-64** 2026-09-22, reported by the user: **Fred held things in his left
  hand in third person and his right in first.** The showreel built the miner
  with his right hand on -x; in this engine the camera's right at yaw 0 is +x
  (host-checked: a model's +x lands on the right of a camera behind it). One
  caution from checking it: a screenshot of a figure seen from behind at night
  easily shows the WORLD's torch where you expect his -- it took a mid-swing
  shot, pickaxe above the shoulder, to see which hand was which.

- **F-65** 2026-09-22: **where lighting's extra triangles come from.**
  Measured on the host, 25 chunks of the user's terrain, nearest meshes:
  no lighting 32034; sky light at full precision 46806 (+46%) -- all of it sky,
  since generated terrain has no torches; rounded to every 2nd level 43712;
  to every 4th 37514. Then with 30 torches placed (one a chunk or so, as round
  a base) full precision is 67792: each torch lays a fourteen-level ring of
  strips round itself. Sky every 4th and torch every 2nd: 50868, 25% under full
  precision. On the badge that took the medium walk from 7.61 to 7.94 fps.
  The torchlight bands are mild (checked at night); cave-mouth bands were not
  photographed.
- **F-66** 2026-09-22, the user: "didn't we get like 15 FPS yesterday?" **Yes,
  and today still does, on the same scene.** Yesterday's 15.3 (F-39) was the
  scripted debug flight -- 3 blocks up, looking across the land -- at near.
  Rebuilding that commit and adding the same flight to today's build:
  14.60 fps yesterday, 14.51 today with lighting and clouds off, 14.09 with
  everything on. The frame rates people see come from WALKING: at eye height
  the screen is close-up textured ground, rasterize 72 ms instead of 46, 9.6
  fps at near, 7.9 at medium, 7.35 at Far (the user's own setting). So the
  work that would move the frame rate is the rasterizer's per-span setup
  (F-40), not undoing features. Lesson recorded with it: a frame rate belongs
  to a scene, and two numbers from two scenes compare nothing -- the same
  mistake as F-36, the other way round.
- **F-71** 2026-09-23, going from 6 languages to 32 (6.5): **the alphabet
  check turned a vague question into a costed one.** Asked which major
  European languages were missing, the generator answered exactly, because
  coverage was already declared per language rather than per string: nine were
  free (Russian above all -- 110M speakers, and the Cyrillic import had
  already taken all 32 letters and composed Ё precisely so it would be), and
  the rest sorted themselves by what they actually needed. The shape of the
  answer was not what it looked like from the missing-glyph counts. Greek
  looked like the most expensive at 58 missing and was among the cheapest:
  Hershey drew a Greek SIMPLEX face (527-550, 627-650), the same weight as our
  Latin, so it is a two-line import, while the Cyrillic had only ever been
  available as the heavier complex. And one accent, the caron, unlocked six
  languages at once. What cost real work was what no accent can make: Polish's
  stroked Ł, Icelandic's þ and ð, Serbian's five new Cyrillic forms -- though
  two of those (Љ Њ) turned out to be ligatures of letters Hershey already
  drew, joined so they share the upright, which is what they are made of.
  A mixed-script check was added after a machine translation put a Latin `a`
  inside a Cyrillic word: identical on screen, and a box on the badge.
- **F-69** 2026-09-23, translating the UI (6.5): **the font was the whole
  job; the strings were the easy half.** The engine drew ASCII and nothing
  else -- Hershey roman simplex, 95 glyphs, indexed by `char - 32` -- so
  German needed umlauts, French its accents and Bulgarian an alphabet the
  font had never heard of. Hershey's own database (public domain, the same
  source the 95 came from) turned out to hold a Cyrillic face, 32 letters in
  each case, and the quotation marks, comma and dash the European languages
  want; the accented Latin letters are composed (one acute, fifty vowels),
  `Æ` `æ` `Ĳ` `ĳ` `“` `”` `„` `…` are Hershey's glyphs placed side by side, and
  `ß` `ẞ` `œ` `Œ` `«` `»` were drawn by hand. Two things make that trustworthy
  rather than hopeful: the generator regenerates the 95 ASCII glyphs from
  the raw database on every run and refuses to write anything unless they
  match the committed table point for point (so the coordinate conversion is
  provably the renderer's own), and it checks every letter of every declared
  alphabet has a glyph and names those that do not. A codepoint with no
  glyph draws an empty box, so a gap is visible rather than silently
  dropped. Cost: about 5 KB of tables, and a bisection over 83 entries only
  for characters that are not ASCII. Measured on the badge: Bulgarian menus,
  German umlauts and French cedillas all render at menu size.
- **F-70** 2026-09-23, translating the UI (6.5): **positional printf is not
  promised, so the substitution is ours.** A translation may need the values
  in a different order than English puts them, which printf spells `%2$s` --
  a POSIX extension newlib only has when built with `_WANT_IO_POS_ARGS`.
  The graceloader's sdkconfig suggests full formatting, which probably has
  it; "probably" is not something a UI should rest on, and an IDF upgrade
  could take it away. `i18n_fmt` therefore does the substitution itself and
  hands the C library one value and one plain specifier at a time, which
  every libc can do. It also made the thing safe: the TYPES come from the
  English string, never from the translation, so a lang file a stranger
  edited on the SD card can get the padding wrong or drop a value but can
  never make the formatter read the wrong kind of argument off the stack.
  Host-tested against `%s` where English says `%d`, `%9$d`, a lone per-cent,
  more values than exist, and a reorder.
- **F-68** 2026-09-22, building the Far Lands (7.1): **porting Beta's generator
  and overflowing it reproduces the Edge Far Lands without being told what
  they look like.** Over 32 chunks at the edge: 42% rock, 30% air, 19% water,
  9% dirt and grass -- the wiki gives 36 / 25 / 23 / 10 for Beta's own, and
  ours has half the height and a lower sea, so the match is closer than it
  had any right to be. Neighbouring blocks agree 98.7% of the time along x
  and 87% along z: the tunnels run west. Things that were not what the
  design expected: (1) the mesh is LIGHT, not heavy -- unchanging along x
  means every face spans the chunk and merges whole, ~200 triangles a
  chunk; (2) the fast path (first octave of low and high, no falloff) makes
  the same blocks as the full port in all 524288 cells compared, a third of
  the host time; (3) on the badge, with doubles in software, a Far Lands
  chunk still takes 134 ms against 33 ms for an ordinary one. That is
  worker time, so loading is slower out there, not the frame rate: walking
  up to the wall at Medium ran at 11.0 fps with nothing dropped. Not yet
  profiled; by operation count the noises outside the overflow (the
  selector, 3400 octave samples, and the surface pass, 3072) should be
  most of it, against 850 for the overflowed octave. Floats there would
  cut it, at the price of the block-for-block proof, which would have to
  become a bound.
- **F-67** 2026-09-22, the user asked whether a pixel's cost was the pixel or
  the z-buffer, then to move the z-buffer into internal SRAM. **Moving it cut
  the cost per pixel by about 30%, far more than F-40's memory benchmark
  predicted** (which put all of memory at ~20 of 62-72 cycles): flat 178 ->
  122 ns/px, textured 190-210 -> 140-155. The depth test was already the first
  thing a pixel does -- a pixel that loses never pays the divide, the texel
  fetch or the framebuffer write -- so that part needed no change. The plane
  cannot fit at full resolution (750 KB) and did not fit beside the 240 KB flat
  list either (133 KB free, largest block 62 KB), and the list cannot shrink:
  its peak is 4897 (far). So the list went to PSRAM, which it reads in order.
  That cost prep time where the list is SORTED: qsort moving 40-byte records in
  PSRAM took the Far walk's prep from 12.4 to 17.3 ms and ate a third of its
  gain. The sort is now keys in internal SRAM (16-bit depth over 16-bit index,
  two 8-bit radix passes) and one gather into a second buffer that swaps with
  the first: prep 10.5 ms, under the 12.4 it was before. Costs 48 KB internal
  for the keys, 512 KB PSRAM for the gather buffers; internal SRAM free went
  133 -> 161 KB. Same scenes as F-66, 25 s each:

  | Scene | fps before | depth internal | + key sort |
  |---|---|---|---|
  | flight | 13.97 | 16.30 | -- |
  | walk, near | 10.22 | 12.36 | -- |
  | walk, far | 7.08 | 7.59 | 7.99 |

  Rasterize on the Far walk 66.2 -> 49.1 ms. What is left is still the
  per-span setup of very short spans (5-7 px flat, 10-13 textured) and about
  4x overdraw on the walk -- the next levers, not memory. Not visually
  checked (the user's call): neither change can move a pixel, the sort only
  changes which triangle the depth test meets first.

- **F-72** 2026-09-23, step 14: **the sequencer could be proved on the host,
  so it was.** `midi_seq.c` is pure -- no engine, no RTOS, no allocation --
  which meant `worldcheck` could run every shipped file through the real
  sequencer at the real sample rate rather than anyone listening for a
  problem on the badge. What that caught is not that the files play, which
  was never in doubt, but the shapes of failure that only show up later:
  a file that never ENDS (the audio task would hang on it), a rewind that
  differs from the first pass (the scheduler replays pieces), and a
  truncated copy -- a file half-copied onto an SD card -- read past its
  buffer. The check truncates every file at every 97th byte and requires
  each one to terminate.

  The clock needed fixing for the same reason. The donor player kept its
  tempo in `double`; the P4's FPU is single precision, so that is a call
  into a software library, on the audio task, per tick. It is 32.32 fixed
  point here -- and the obvious `(num << 32) / den` **overflows**: a 32-bit
  core has no `__int128`, and `rate * us_per_quarter` is already 1.1e10
  before the shift. Long division with two sixteen-bit refinements instead.

- **F-73** 2026-09-23, step 14: **sixteen voices summing is not bounded by
  anything.** Rendered on the host, a dense Schumann chord peaked at 1.64
  where 1.0 is full scale, and the honest int16 conversion clipped it --
  1036 clipped samples in Traeumerei, heard as a crackle on every loud
  chord. Turning the master gain down far enough to fix that alone would
  have made the Satie, which never went above 0.89, too quiet to hear under
  the footsteps.

  The fix is a gain of 0.58 and the cubic soft clipper `1.5x - 0.5x^3`:
  three multiplies, no table, no branch on the common path. It has a second
  property worth having -- its slope at small signals is 1.5, so it lifts
  quiet music while it compresses loud music, and the eleven pieces end up
  nearer each other in level than they went in. Afterwards: peaks 0.71 to
  1.00 and **not one clipped sample** in any of them.

  The general lesson is the one worth keeping: the host render was a WAV
  file and a peak meter, and it found in a minute what would have been
  "the music sounds a bit crunchy sometimes" on the badge.

- **F-74** 2026-09-23, step 14: **the app built, linked, uploaded and then
  would not start, and nothing said why.** No console output, no panic, no
  splash -- `make ping` answered "the launcher is up, or the app is wedged",
  which is true and useless. Three cycles went into the tooling before the
  cause turned out to be one line of ours: `music.c` called **`strcasecmp`**,
  and graceloader does not export it.

  An ELF app for graceloader links against NOTHING. Every libc and IDF
  function is resolved at LOAD time from the loader's export table, so a
  call to something that table does not carry compiles clean, links clean,
  uploads clean, and then fails at the one moment when there is no way left
  to report it. se_mp3.c's header records the same trap with `opendir()`,
  which is how it was eventually recognised.

  Two things came out of it, and the second matters more than the fix:

  * `ieq()` in music.c, four lines, because a file extension is ASCII;
  * **`tools/symcheck.sh`, run by `make build` after the link**: every
    undefined symbol in app.so, checked against
    `../tanmatsu-graceloader/main/symbol_export/all`, naming anything
    missing. It found exactly one symbol -- ours -- and it will find the
    next one in the second it is introduced rather than after an hour of
    suspecting the badge. Where the loader's source is not checked out it
    SKIPS and says so, rather than failing a clone that only wants to
    build.

  The general shape is one this project keeps meeting: a seam between two
  builds that the compiler cannot see across wants a check that CAN. It is
  the same argument as `hostpurity.sh` and the language-table staleness
  rule, and the same argument for having written `check_midi` before ever
  putting a MIDI file on the badge.

- **F-75** 2026-09-23, **the user**, reporting it: **"the tool sounds only
  play when music is enabled."** Which is exactly what it looked like, and
  exactly what it was not.

  Nothing in the effects path reads the music setting. What actually
  happened is the mixer's idle power policy: it mutes the amplifier and
  disables I2S about 46 ms after the last sound, and powers back up when the
  next voice is registered. The audio is mixed and written correctly either
  way -- but **an amplifier's turn-on is not instantaneous**, and SynthMiner's
  effects are short. `SFX_HIT`, the tool striking a block, is 35 ms. Against a
  cold amp it is over before the speaker is listening.

  Why the music setting appeared to control it: a music source that is
  INSTALLED counts as an active source every chunk, whether or not it is
  making any sound. With music on, our source renders silence through the
  four-to-ten-minute gaps and the amplifier never sleeps, so the effects are
  fine. Turn the music off, the gate skips the source, the mixer idles, and
  every effect now has to wake the speaker up.

  The badge said so plainly once asked the right question. With `music=0`:
  `power_down: amp+I2S off` at 4662 ms and never up again. With the fix:
  `power_up` at 7992 ms and no power_down in the next 43 seconds.

  Fixed in the engine with `audio_mixer_keep_awake()` rather than in the
  game, because the sharp edge is the engine's and the next game to meet it
  would lose the same afternoon. SynthMiner holds it on for as long as it
  runs. The cost is the amplifier's idle draw -- which, note, **this game was
  already paying** whenever the music was switched on.

  The lesson is not about amplifiers. It is that "feature A only works when
  feature B is on" almost never means A reads B; it means both depend on
  something neither of them mentions.

- **F-76** 2026-09-23, adding the volume sliders: **three settings screens
  had been overlapping their own text in a dozen languages, and nobody had
  measured.** A row draws its label at the left and its value -- a slider, a
  tick box, a word -- at a fixed offset (`value_dx`). The offsets were chosen
  by eye against ENGLISH, when English was the only language there. Going to
  32 (F-71) checked that every letter could be DRAWN and never asked whether
  the words would FIT.

  What was already broken before a single slider was added: Graphics
  overflowed in 23 of 32 languages, Settings in 7, Audio in 4. The worst was
  Ukrainian's "Дальність промальовування" at 492 px against a 260 px column
  -- nearly double.

  Two kinds of fix, and both were needed:

  * the panels and columns widened where the language is simply longer than
    English (Audio and Graphics now use a wider panel; every column grew);
  * eight translations shortened where no panel could have held them. These
    were descriptions where a label was wanted -- Bulgarian's "Разделителна
    способност" for a resolution toggle is correct and is not what a control
    is called.

  The third fix is the one that lasts: **`check_label_widths()` in
  worldcheck**, which measures every label of every screen in every language
  with the engine's OWN `hershey_advance` at the row height the menu uses. A
  label that passes there fits on the badge. It reports the tightest fit in
  the whole game (16 px, Spanish "Volumen de los efectos") so the margin is
  visible rather than assumed, and it was proved to bite by lengthening a
  Ukrainian label and watching it name the language, the key, the text, the
  width and the column.

  This is the same lesson as F-74 and `hostpurity.sh`: a property the
  compiler cannot see wants a check that can. "Every character is drawable"
  and "every label fits" look like the same question and are not.

- **F-88** 2026-09-24, pricing the page flip: **the present has never been
  on the record.** Every PERF record since block 2 shows `blit` and `vsync`
  at 0.0, and not because they were free: `PROF_BLIT` / `PROF_VSYNC` exist in
  `testkit/profile.h` and nothing in this game feeds them. What the old
  present did cost can be read off the engine instead: the copy ran on
  DMA2D (little CPU), and the tearing-effect wait returned at once whenever
  a frame had taken longer than a refresh, because a binary semaphore given
  mid-frame is still there. At 11-17 fps that was every frame.

  The panel refreshes at 30 MHz / (590 x 848) = **59.96 Hz**, from the ST7701
  timing in the DSI abstraction. That number decided three buffers over two
  (D-87): a double-buffered flip must wait for the refresh that frees the
  other buffer, half a refresh on average, ~8 ms of a 60-90 ms frame.

  **Fixed 2026-09-25**, and it had to be before anything could be compared:
  `frame_stats()` now reads `se_present_stats()` once a frame and feeds both
  through `prof_add()`. The present runs after `on_render` returns, so this
  charges the PREVIOUS frame's present to this one -- over a reporting
  period, the same number. Measured on the badge: **blit 0.32-0.50 ms,
  vsync 0.00-0.02 ms**, so the flip is cheap and the triple buffer really
  does mean a game below the refresh rate never waits. Both had read 0.0
  since block 2, and the present had been hiding in the residual.

- **F-89** 2026-09-24, while building the banded renderer: **the engine's
  cache argument for the framebuffer was luck, and banding would have broken
  it.** `docs/ppa.md` held that a framebuffer needs no invalidate because
  each frame's working set (768 KB of framebuffer plus the depth plane) far
  exceeds the 128 KB L2, so a buffer's old lines are gone before it is drawn
  into again. With depth in internal SRAM and empty bands skipped, that is no
  longer certain: a line still cached from two frames ago, hit by a partial
  CPU write (a HUD glyph) after the PPA has DMA'd the new sky under it,
  would write the old pixels back over the new sky. The flip now writes the
  frame back AND drops it from the cache, and a band writes back and drops
  its own lines before it reads the backdrop in.

- **F-90** 2026-09-24: **the banded renderer draws the z-buffer's pixels,
  and the check that says so can fail.** `se_scene.c` compiled on the host
  against six small stub headers (ESP-IDF heap, log, timer, cache, memory
  utils, and four PAX symbols), then 1000 random scenes drawn both ways:
  dense (400-1500 primitives, some of them huge and crossing the near plane)
  and sparse (5-60 small ones, so most bands are skipped), full and quarter
  resolution, with and without `SE_SCENE_DEPTH16_INTERNAL`, with viewports,
  cull and depth order, a light, a tint, cut-out textures and both byte
  orders. **No pixel differed.** The same test against a copy whose band
  spans were made one band too narrow failed 25 runs of 40, so it does see
  a mistake of that kind. The harness is in the session scratchpad, not the
  repo (step 39).
- **F-91** 2026-09-25, trying to run G6's tests and getting nonsense: **the
  scene the renderers were to be compared on could not measure a renderer.**
  `flight` opens a SCRATCH world (`worldstore_open_scratch`) -- no directory,
  nothing on disk -- so every chunk is generated from noise on every run. The
  first `perf scene=flight_fullres` reported **23.84 fps** over 20 seconds,
  which was eleven seconds of loading screen at ~1 fps (counted: the loading
  path calls `devtest_after_render` like any frame) followed by nine seconds
  of empty sky. `world: 0 chunks / 0 sections drawn`, `0 of 1008 meshes
  built`, `refused 1272`/s with the mesh queue pinned at 49/48, for the whole
  run. I called that a meshing regression; **the user said it first and was
  right**: *"instead of purely testing the render performance, you also have
  to generate a new world on the fly for every test?"* Nothing was broken.
  The camera outran a generator that had to invent everything, forever, and
  the streamer never caught up. Worse for the purpose: that generator
  saturates the PSRAM bus on core 1 for the whole measurement window -- the
  exact resource banding exists to relieve -- so the comparison would have
  been of the generator. Fixed by step 41: a persisted world, pre-generated
  once, streamed off the card (`0 missing`, `refused 0`, queue 0-6 of 48, and
  735 ms to load instead of eleven seconds to generate).
- **F-116** 2026-09-28, **it was ESP-Hosted, and the badge settled it in
  one run.** The SD card's write timeouts -- dozens a session of
  `sdmmc_write_blocks failed (0x106)`, 45 leaked FAT clusters measured
  with fsck.fat -- stopped completely when WiFi was removed from
  graceloader. Not one read or write error in the session after, and the
  retrying disk layer added for them never fired once: the failures did
  not get caught and recovered, they stopped happening.

  The chain of wrong answers is the useful part, because each was
  refuted by looking rather than by arguing:

  1. "The card is busy with an erase cycle, so pace the writes." Wrong.
     The card answers CMD13 with 0x900 -- READY_FOR_DATA, state `tran` --
     immediately after a failure.
  2. "So the card is fine and had five seconds anyway." Also wrong, and
     the user caught it: sd_host_wait_for_event() logs when IT times
     out and that line is not in the log, so the five seconds never
     elapsed. ESP_ERR_TIMEOUT was arriving from a hardware interrupt --
     RTO, DTO or EBE, and the driver only says which at ESP_LOGD.
  3. The answer was in graceloader's own source the whole time:
     *"ESP-Hosted already owns the shared SDMMC host controller."* The
     ESP32-P4 has ONE SDMMC controller and the card shares it with the
     WiFi co-processor. A transaction disturbed by the other user of
     the controller fails instantly and leaves the card healthy, which
     is exactly the signature.

  **And the memory was ten times what the size report predicted.**
  `idf.py size` showed 3.7 KiB of DIRAM saved; the badge gave back
  **42 KiB** of internal heap (free_int 242727 -> 285651), because
  ESP-Hosted's SDIO task and its receive buffers are heap and never
  appear in a static size report. 283 KiB of flash came with it.

  What stays, and why: the retrying disk layer, because ESP-IDF's
  ff_sdmmc_write does not retry at all and FatFs has no journal -- one
  glitch from any cause still means a dead file handle and a leaked
  cluster. It is insurance that now costs nothing, and its counters say
  so.
- **F-117** 2026-09-28, from the same run: **the title world made the
  menu fifteen times faster.** 16114 ms of generating 81 chunks became
  1057 ms of reading them off the card, and 1256 ms on the way back from
  a world.

      card/s: 81 load at 5.2 ms (worst 8.4)

  F-115 predicted 2.4-3.0 ms a chunk against 186 ms to generate one, and
  that is what it came out at. Step 52 is done.

  Getting there cost four wrong builds, all of them the same shape: the
  world reached the card in a state that was almost right and was marked
  complete anyway. Wrong meta written into level.smw, then 49 of 81
  chunks because a full save sweep overruns a 48-deep queue in silence,
  then the half-written world surviving because a MARKED world is never
  rebuilt. The version file (SM_TITLE_GEN) is what finally made that
  last one recoverable, and it exists because the user asked for it
  before any of this happened: *"add a sort of version file ... That way
  we can decide later on if we want to recreate it for a future
  update."*
- **F-118** 2026-09-28, reading the first play session's trace: **the
  recorder was inventing mesh lags of half a minute.** The report said
  *"worst 31281 ms"* on one chunk, with 18422 ms and 2887 ms beside it,
  against a median of 89 ms. None of it was real, and the temptation to
  go looking for a stall in the game is exactly the trap F-111 was.

  Two faults in `trace_mesh`, and they compounded:

  * **IT TIMED THE FAR LEVELS OF DETAIL.** LOD_FAST and COARSE are
    rebuilt whenever the streamer has spare capacity, and the player is
    by definition too far off to be looking at them -- timing one
    against an edit measures the queue's idle time, not anybody's wait.
    Worse, it reported them and then did not clear the entry, so the
    same edit stayed live to be reported again.
  * **IT CLEARED ONE PENDING EDIT PER MESH, AND EDITS COALESCE.**
    `lod_stale` is a bitmask on purpose (chunk.h): three quick breaks in
    one section are one rebuild, not three. The leftover entries stayed
    live and were matched, half a minute later, against a mesh answering
    something else entirely. **The number printed was the gap between
    two unrelated events.**

  Now only the detailed mesh is timed, it reports against the OLDEST
  edit it answers, and it clears every one of them -- with `n=` saying
  how many it coalesced, so a rebuild that answered four edits says so
  instead of looking like three that were never drawn.

  A second false alarm went with it: refusals and pauses share a trace
  line, and `traceanalyse` counted the LINES, so a session with no card
  trouble at all announced **"THE CARD REFUSED 0 CHUNK WRITE(S)"**. That
  is the sort of banner people learn to skip past, which is the worst
  thing an instrument can become. Pacing is now reported separately,
  which is how the 173 ms above became visible.

  The lesson is F-111's, again and unlearnt: **a number from my own tool
  is not evidence until the tool has been checked.** Both times the tool
  was measuring something adjacent to the question and reporting it as
  the answer.

- **F-119** 2026-09-29, trying to check the user's *"framerates seems
  comparable"* against the recorder and finding I could not: **a trace
  of a session whose settings changed half way through is worth less
  than a trace of either half.**

  Three sessions were on the card -- 356 s at a median of 12.9 fps
  before blending existed, then 134 s at 15.3 and 164 s at 15.6 with it.
  That looks like a clean answer and is not one. The sessions covered
  different ground with different amounts of water in view; the last of
  them had **no water edits and one physics line at all**, so its 15.6
  says nothing about blended water; and nothing recorded which mode was
  running, so the two could not be separated even in principle.

  **The user's own A/B was the controlled experiment and mine was not.**
  Flipping in one spot and watching the counter compares two settings
  over one view. Averaging a whole session compares two walks.

  `trace_event()` is the fix -- a `!` line stamped with the moment a
  setting changed -- and `traceanalyse.py` now splits its frame-rate
  report on those marks instead of printing one mean that belongs to no
  configuration that was ever running. It sits beside `trace_note()`,
  which states settings once at the top; the difference is that this one
  records them CHANGING.

  (`trace_note` already existed, which I found by compiling a
  redefinition of it. Grep first.)

- **F-115** 2026-09-28, out of **the user**: *"Loading the main menu takes
  a long time (both from game start and also when quitting a world back to
  the main menu)."* **The title runs on a scratch world, so its 81 chunks
  are GENERATED every single time.** From the boot log:

      loaded (Loading): 81 chunks resident (0 missing) in 22 rounds, 15882 ms
      generated: 81 ordinary chunks at 186.4 ms

  `title_view()` has `load_radius 4`, which is (2*4+1)^2 = 81, and
  `title_begin()` calls `worldstore_open_scratch()` -- a world with
  nowhere to be saved, by design. So 81 x 186 ms, and it happens again
  on every return to the menu.

  The contrast is the whole finding: a chunk LOADED off the card measured
  **2.4-3.0 ms** in the same sessions (F-114). Generating one is 186 ms.
  **Sixty to seventy-five times slower**, and the title pays it for
  scenery that is the same every time because the seed is fixed.

  Step 41 already solved exactly this for the bench world -- generate
  once, persist, stream it back -- and the title is a better fit for the
  trick than the bench world was: fixed seed, fixed camera path, nothing
  the player can change. Unfixed, and left as its own step.
- **F-114** 2026-09-28, from four play sessions with the flight recorder,
  the first hard numbers on what the card costs:

      load      2.4-4.0 ms mean, 9.6-18.9 ms worst
      save     12.5-15.9 ms mean, 138.9-236.6 ms worst
      compact  2 rewrites in 160 s, worst 146 ms
      region file 41-227 KiB (655-3625 B a chunk; the spread is terrain)

  Saves outnumber loads roughly two to one, which is D-30 and the
  write-back-everything rule showing their price. All of it is on the
  worker, pinned to core 1, so it delays the STREAMER and not the frame
  -- the first version of traceanalyse said the opposite and had to be
  corrected.

  **Two instrument bugs came out of reading this, both of the same kind:
  a number that was too round to be true.** Compaction was counted by
  elapsed time, which cannot tell a rewrite from a slow check, and
  reported 316 compactions for 316 saves; counted on the return value it
  is 2. And the trace file held every session appended, so a "worst since
  boot" from an hour earlier was reported as the current run's -- which
  is how 236.6 ms got quoted for a session that never saw it. One
  playthrough per file now, and traceanalyse splits what it is given.
- **F-113** 2026-09-28, **the flight recorder found the render bug three
  days after it was reported, in a session nobody set up to catch it.**
  F-111 had blamed the geometry caps and been refuted by the badge; this
  is what it actually was.

      B t=173.0 ... blk=flower_red ch=-103,6 s=2
      T t=173.7  queue=25/48 miss=2
      T t=174.7  queue=29/48 miss=7  save=7@14951us
      T t=175.7  queue=34/48 miss=0
      T t=176.8  queue=49/48 miss=7
      M t=176.9  ch=-103,6 s=2 lod=0 lag=3943ms

  A flower broken at t=173.0 stayed on screen until t=176.9. The block
  left the WORLD the instant it broke; the MESH did not, because its
  rebuild went into the same FIFO as forty-odd speculative streaming
  jobs and waited its turn. Dense terrain means more streaming, a longer
  queue, a longer wait -- which is exactly "especially in dense
  forrests", and the geometry lists peaked at 43% and 50% in the same
  session.

  Fixed by `lod_urgent` (chunk.h) plus `xQueueSendToFront`: a remesh
  caused by a BLOCK CHANGE jumps the queue, one caused by a chunk
  arriving does not. Measured after: **median 89 ms, worst 322 ms over 13
  edits**, against 3943 ms. Not a like-for-like comparison -- the queue
  peaked at 25/48 in the good run against 49/48 in the bad one -- so the
  honest claim is "clearly fixed", not "12x".

  **The floor is one frame.** A remesh is not asked for when the block
  breaks; it is asked for by `chunk_render_submit` on the next render
  pass, so at 15 fps there is a 67 ms floor before the job is even
  queued. The 89 ms median is one frame plus the job, which is as good
  as this shape of code gets.

  What this finding is really about: **every diagnostic before it had to
  be armed before the bug.** This one was not, and that is why it worked.
- **F-112** 2026-09-28, out of **the user** after a play session: *"when
  cutting trees, just remove leaves cuts down the tree, but the 'cut the
  whole tree up from this point' should only happen when cutting the
  trunk."* Part F is what was wrong, not the code -- it gave one flag two
  jobs, and `interact.c` did faithfully what it said. Split into
  `BF_FELLABLE` (tree material, what the fill spreads through) and
  `BF2_TRUNK` (what starts a fell). The two ideas had looked like one
  because a log is the only thing that is both, which is the whole shape
  of the mistake: a flag named for its consequence rather than its
  meaning.
- **F-111** 2026-09-28, out of **the user**: *"stuff i place isn't showing
  up visually, especially in dense forrests. I suspect we are running
  again into the issue with the render lists getting full."* Right on
  both counts, and it is **two faults**.

  Measured on the host -- the mesher is pure, so this needed no badge --
  at seed 1030, the densest canopy in a 9x9, the medium preset:

      textured chunk (-4, 3) dist  8.0 : 2672 tris
      textured chunk (-3, 3) dist 11.3 : 1918 tris
      textured chunk (-4, 4) dist  0.0 : 2406 tris   <- underfoot
      textured chunk (-3, 4) dist  8.0 : 1544 tris
      TEXTURED 8540 against a cap of 4096 -- 2.1x over
      FLAT      5948 against 6144 -- fits, barely

  The chunk the player stands in is 2406 triangles by itself, so two
  chunks of canopy in view overflow the textured list. That is the
  capacity fault, and it is the one the user named.

  **The second is why it read as "my block did not appear."**
  `chunk_render_submit` iterated slots 0..255, and a slot is
  `(cx & 15) * 16 + (cz & 15)` -- a wrapped coordinate hash unrelated to
  the camera. The scene drops in SUBMISSION order, so the hole landed
  wherever the hash put it, underfoot as readily as at the horizon, and
  it moved as the player walked because the wrap point moves. Sorting
  near to far turns an arbitrary hole into one at the far edge, inside
  the fog. It costs 2 KB of bss and an insertion sort over what survived
  the frustum.

  Two things worth keeping from this. The engine's own header says a full
  list "drops whatever the game happened to submit last" -- the game was
  never submitting in an order that made that sentence safe, and nobody
  had connected the two. And the drop counter has been logged since D-72,
  to a console a player does not have while playing; it is now on the
  position overlay, which is where a number that means "the picture is
  incomplete" has to be.

  **CORRECTION, same day, from the badge: the capacity half of this
  finding is WRONG, and the host measurement that produced it was
  inflated about six times over.** The user's `make monitor` capture of
  real play answers both questions:

      scene: textured list: PSRAM (272KB, 4096 tris)
      scene: geometry lists: tris=PSRAM (240KB) lines=INTERNAL (7KB)
      scene: quarter-resolution depth plane: INTERNAL (187KB)
      ... over two minutes of play, running through the test world:
      textured peaks at 1340/4096 (33%)
      flat     peaks at 3147/6144 (51%)

  Nothing was dropped. The lists are in PSRAM, so raising the cap was
  never the memory question I made it -- and it is not worth raising,
  because it is a third full.

  Where the 8540 came from: the tool summed EVERY vertical section of
  every chunk within tex_dist, over the whole 360 degrees. The device
  does neither. Per-section culling alone accounts for 3546 of the 8540
  (42%) -- sections at y 0-31 with the eye at y 52, which is F-33's
  "the underground half is 68% of a chunk's triangles" restated -- and
  the frustum takes most of what is left. 4994 survive the vertical cut
  over 360 degrees, and a ~90-degree view of that is about the 1340 the
  badge actually reports.

  **So the reported bug is still unexplained.** The near-to-far order is
  right and stays -- it makes an overflow benign if one ever happens --
  but it is defensive work, not the fix, and it should not have been
  written up as one. The lesson is the one F-108 already taught and I
  did not carry over: a host number that has not been reconciled against
  the device is a hypothesis, and this one was reported as a measurement
  in a commit message. What made it convincing was that it agreed with
  what the user already suspected.
- **F-110** 2026-09-28, found only because the app-repository publish forced
  an audit of every asset path: **`voxel_fx` asked for
  `synthminer/torch_flame.png`, and nothing has ever written a
  `synthminer/` directory.** The torch flame has been failing to load and
  drawing flat since the port, on the badge as much as from the repository.

  It is a leftover from the showreel, whose textures live one subdirectory
  per reel segment; step 0.2 flattened them to `textures/` and this one
  string kept the old shape. It did not fail loudly because texcache is
  built not to: a missing texture logs `-- drawn flat` and returns NULL,
  which is right for a renderer and wrong for the only person who could
  have noticed. Every other texture path in the app is flat, and the
  grep that says so is two lines.

  The audit that caught it is worth keeping as a habit: resolve **every**
  texture name the code can ask for -- the 41 string literals and the
  twelve `item_<name>.png` built at runtime from the `ITEMS` table -- and
  check each against what ships. It came out 61 of 61 after the fix, with
  the seven `/int/icons/*.png` key-caps correctly excluded as firmware's.
- **F-109** 2026-09-28, on being asked to verify metadata.json before a
  first publish: **there are two install paths, and the one nobody here
  uses had rotted.** `metadata.json` named 25 of the 54 textures and none
  of the 11 pieces of music, and `make apprepo` copied the metadata, the
  icons and `app.so` and stopped.

  Either fault alone hands a stranger a game with no item icons, no
  furnace, no chest, no birch, and silence. Neither could be noticed here,
  because `make install` over BadgeLink globs `textures/` and
  `assets/music/` in the Makefile and never reads `metadata.json` -- so the
  badge on this desk has always been complete, and the file that describes
  it to everyone else drifted for 34 assets without a symptom.

  **The lesson is not "check metadata.json".** It is that a second
  description of the same fact, maintained by hand, in the path that is
  never exercised, is a fact that will be wrong. Both lists now come from
  the directories the install rules already glob
  (`tools/make_metadata.py`), and `tools/apprepocheck.py` walks what the
  file promises against what was copied.

  Two more came out of the same audit. The repository's schema forbids a
  comma in `description` -- the pattern is letters, digits and a short list
  of punctuation -- and validating all 64 apps already published showed
  every one passing and **ours the only comma in the repository**, so the
  pull request would have failed `verify_metadata` before a human read it.
  And the layout was checked rather than assumed: graceloader sets
  `install_basepath` to the directory holding the executable, the
  repository copies each asset to `<mount>/apps/<slug>/<target_file>`, and
  the app opens `<basepath>/textures` and `<basepath>/music` -- the same
  tree `make install` writes, which is the one known to work.
- **F-108** 2026-09-27, from per-phase timing inside the codec, after two
  wrong answers to the same question: **it is allocate(), sweeping the
  encoder's own struct in PSRAM.** Streaming audio ships OFF
  (`SM_STREAM_AUDIO`, CMakeLists.txt).

  The measurement, per pass, healthy against collapsed:

      filterbank   4.84 ms  ->   5.34 ms
      sb-sweeps    1.28 ms  -> 160.42 ms

  The filterbank is the big arithmetic -- 184320 multiply-accumulates, and
  the loop whose CODE is fetched from PSRAM like all of app.so. It barely
  moves. So the 125x is not instruction fetch, which is what I had concluded
  from "moving the data bought 8%" and was about to have the user reflash
  graceloader for, committing to an exported ABI, on the strength of an
  inference. The three timers that refuted it took ten minutes.

  Within the sweeps it is `allocate` (56.3 ms average against `scf` 2.4 and
  `wr` 0.34), and it does not read the subband array at all: it is a greedy
  loop that rescans every live subband each iteration, and `aidx`, `live`,
  `codes`, `ncodes`, `nscf`, `apart` and `power` are all members of
  `struct pdmp2_enc` -- under 2 KB, swept tens of thousands of times for one
  frame, allocated with plain PDMP2_MALLOC and therefore in PSRAM. 5.6 KB,
  the cheapest thing in the encoder to move, and the last one anybody
  looked at.

  It is now PDMP2_MALLOC_HOT, and **that fix is unmeasured**: with it the
  codec holds 22.5 KB of internal SRAM and CHUNK LOADING BROKE. That is the
  second time making audio work cost something that matters more -- F-106
  was the H.264 encoder, and the reserve added there did not help because
  the reserve protects a fixed 48 KB without knowing who needs it. Whoever
  returns to this needs an internal-RAM budget for the whole app first, not
  another placement change. Mono would halve every phase (all four are
  per-channel) and not one byte of the footprint, so it is margin, not a
  fix: half of 125x is 62x.

  `s_flat` went back to PSRAM in the same round, also measured: I had put it
  in SRAM believing the filterbank read it 36 times over. It does not --
  encode_frame() converts it to float in one sequential pass and the
  filterbank reads that -- and the timed copy was 0.10-0.17 ms and never
  moved under load. Three of my four placement decisions in this
  investigation were made from reading the code and were wrong; the timers
  were right the first time.
- **F-107** 2026-09-27, out of **the user**: *"make install should always
  install everything. You already have a specific makefile target for only
  uploading app.so, right?"* **A cache keyed on the wrong question is worse
  than no cache.** `make install` stamped what THIS CHECKOUT had last
  uploaded and skipped anything unchanged since -- but the question that
  matters is what is ON THE CARD. On a badge that had never had synthminer
  installed it skipped all 65 assets and printed `54 already current` with
  zero of them present: a listing showed 1 file in the app directory, 0
  textures, 0 music. An app with no textures and no music, and nothing on
  screen able to say why.
  I had even written the failure mode into the Makefile -- *"the stamps
  record what THIS CHECKOUT last sent, so they are wrong if the card is
  swapped or wiped"* -- and shipped it behind a manual `INSTALL_FORCE=1`,
  which is a footgun with a note taped to it rather than a fix.
  My first repair was to ask the device with one `fs list` a directory and
  skip only what was genuinely there. The user stopped it, and was right to:
  `make push` ALREADY EXISTS for the fast path, so the optimisation had no
  remaining purpose and was pure risk. Install is now unconditional -- every
  file, every time, 54 textures and 11 pieces of music, verified on the
  device afterwards as 5/54/11.
  The asymmetry is the whole lesson: a redundant upload costs twenty
  seconds, a wrong skip costs an hour of looking for the bug somewhere else
  entirely. Optimisations belong on the path that is actually hot, and only
  when nothing cheaper already covers it.
- **F-106** 2026-09-26, out of **the user** the moment F-105's fix was
  installed: *"now i can't even enable livestream in the menu"*. **Internal
  SRAM is shared with the thing the audio exists to accompany.** F-105 put
  all 38.5 KB of pdmp2's working set in internal RAM, with a PSRAM fallback
  so the codec could never fail to open -- and that fallback is exactly what
  made it dangerous: the codec always succeeded, and
  `esp_h264_enc_open()`, which wants internal DMA buffers of its own, failed
  instead. `se_stream_start()` returned ESP_FAIL, and the row is written to
  leave the box unticked on a refusal, so the symptom was a menu that did
  nothing. Audio was protected at the price of the picture.
  Three things, because one of them alone only moves the failure:
  * **Only the hot buffers go inside.** `PDMP2_MALLOC_HOT` (new in pdmp2)
    takes win 2.0 KB, mat 8.0 and work 6.5 -- the three the filterbank
    re-reads 72 times a frame -- and `hist`, `sb` and `pcm`, touched about
    twice a sample, stay in PSRAM. 16.5 KB instead of 38.5.
  * **Video allocates first.** Audio prepare moved after
    `ppa_register_client()` and `esp_h264_enc_open()`. A tight internal heap
    now costs audio timing, which is a glitch, instead of the whole stream,
    which is a mystery.
  * **A reserve, not just a fallback.** `usbnet_start()` comes after the
    codec and wants internal DMA memory too, so falling back only when the
    allocation FAILS would have killed the link next -- the same mistake one
    step down. The hot allocator takes internal memory only while 64 KB
    would still be left.
  The lesson is narrower than "internal RAM is scarce": a fallback that
  cannot fail hides the shortage and pushes the failure onto whichever
  component asks next, so it lands somewhere with no obvious connection to
  the change. The reserve puts the failure back where it belongs.
- **F-105** 2026-09-26, from the residual the F-103 instrumentation added,
  on a run that stalled the same way: **the stream task was never starved.
  It was in the MP2 encoder, and the encoder was slow because its tables
  were in PSRAM.** STARVED went to **0%** in the collapse -- the task held
  the CPU for the whole 1079 ms pass -- and the time was all in one call:
  `se_stream_audio_take`, 7.1 ms a frame while healthy and **134-142 ms** in
  the collapse. The same code, on the same 1152 samples, twenty times
  slower, while the hardware H.264 encode beside it never moved off 8 ms.

  Identical work at twenty times the cost is memory, and the filterbank is
  the reason: 36 slots x 2 channels, each reading all 512 floats of `win`
  and all 2048 of `mat`, so the 8 KB matrix is re-read **72 times a frame**
  -- 576 KB of reads out of an 8 KB table, 184320 MACs. That is a
  cache-resident working set. 7 ms is 38 ns an access, 140 ms is 760 ns,
  which is a PSRAM miss. The game's chunk meshing evicts 10 KB of audio
  tables and every MAC becomes a round trip.

  At 140 ms per 52 ms frame the task cannot keep up by arithmetic, and
  everything else observed is downstream of it: passes fall to 0.8/s, the
  ring laps, `auddrop` reaches 28.9 s, video collapses to 0.7 fps because
  the same task encodes it, and none of it shows in an average because the
  encoder is fine whenever the cache happens to hold it.

  Fixed by allocating the encoder's 38.5 KB in **internal SRAM**
  (`pdmp2_port.h`), internal-first with PSRAM still there as a fallback so a
  badge short of internal RAM gets slow sound rather than none. `s_flat`
  moved with it, because the filterbank reads it 36 times over; the ring
  stays in PSRAM, where a streamed buffer belongs.

  THE HEADER SAID THE OPPOSITE, IN MY OWN WORDS: *"The encoder touches these
  buffers once per audio frame (52 ms), so PSRAM's latency is irrelevant to
  it."* Once per frame was off by a factor of 184320, and it was written
  confidently enough that nothing went back to check it for four rounds of
  debugging. Three separate theories -- chunk-worker priority inversion, a
  full job queue, a saturated core 1 -- were argued against measurements
  that had already ruled them out, while the one number that mattered had
  never been taken: `aud_us` times the MIXER's push, so the MP2 encode had
  never once been measured on hardware. The "24-71 us a frame" figure
  carried through this whole investigation was of something else.
- **F-102** 2026-09-26, from the first full packet capture of a stalling
  stream (`tools/streamcap.py`, 186 s, 11659 datagrams) cross-checked against
  the badge's own counters (`/sd/defuckinfo.txt`, `make pullinfo`): **the
  stream task is starved, not slow, and not blocked on the network.** The
  three readings agree on the same run -- 11688 datagrams against 11659
  captured, 2498 audio frames against 2491, 1199 video frames against 1191 --
  so the two files describe one event and can be read together.

  What is NOT wrong, each with the number that rules it out:
  * **The network.** Continuity counters clean on all five PIDs, zero bad
    sync bytes, `dgfail 33` of 11688. Arrival-minus-PTS skew is constant to
    30 ms across 148 s (25757 -> 25455), so nothing buffered and nothing
    drifted anywhere between the encoder and the disk.
  * **The link's capacity.** In the worst windows the MEDIAN gap between
    datagrams is 0.0 ms -- full 1316 B datagrams back to back -- then 300 ms
    of silence. It carries 0.83 Mbit/s when asked and 0.16 during the
    collapse. Bandwidth is not the constraint.
  * **Every per-frame cost.** PPA 14.5 ms, H.264 8.0 ms, mux 2 ms, in the
    collapse and out of it, unchanged. Nothing got slower.
  * **The game.** `pub` holds at 9-10 offers/s straight through, while
    `drop` rises to 9/s: the game keeps offering and the stream keeps
    refusing because the previous frame is still sitting in the slot.
  * **The mixer.** `audn` is 86.1 pushes/s at a constant 24 us for the whole
    run, collapse included -- and the mixer is the HIGHEST priority task on
    core 1. So core 1 is not saturated and the audio producer is healthy.
  * **pdmp2.** 2491 frames, every one well-formed MPEG-2 LSF Layer II,
    22050 Hz, 128 kbit/s, 836 B, 1152 samples. The codec has never been the
    problem and this is the third measurement saying so.

  What IS wrong: `pcronly`, which counts passes round the stream loop, falls
  from 21/s to **0.8/s** while the work in a pass stays at about 10 ms. The
  task does 10 ms of work and is scheduled less than once a second.

  The audio holes are a consequence, and the arithmetic closes exactly:
  0.8 passes/s * `AUDIO_BURST_MAX` 8 = 6.4 frames/s delivered against 19.1
  produced = 12.7 lost/s; measured `auddrop` 12.8/s, and 553 frames = 28.9 s
  total, which is the same 28.9 s of holes the capture found on the wire.
  `RING_FRAMES 4` and the burst bound are not the disease -- every hole is
  2 to 4 frames, never more, because the ring is exactly that deep.

  Two of my own theories died here, and both were things I should have read
  before theorising: `main/world/chunk_worker.c` contains **no mutex or
  semaphore at all**, so the priority-inversion story was baseless, and it
  submits with a **zero** timeout, so the game can never block on a full
  queue.
- **F-103** 2026-09-26, trying to find what starves it and failing on my own
  instrumentation: **a measurement that does not close cannot choose between
  two opposite fixes.** The counters showed 10 ms of work in a 1250 ms pass
  and therefore proved only that the missing 1240 ms was somewhere they did
  not look. Three calls in the loop were untimed -- `tsmux_pcr_if_due`,
  `se_stream_audio_take` and `tsmux_write_audio` -- and ALL THREE SEND, so
  "blocked in the link" and "never scheduled" were indistinguishable, which
  is the whole question. `aud_us` was no help either: it times the mixer's
  push in `se_stream_tap`, not anything in the stream task.
  Fixed by timing all three and adding `pass`, top-of-loop to top-of-loop,
  so `pass - (pcr + atk + amx + enc + mux)` is time off the CPU by
  subtraction rather than by argument. `tools/infoanalyse.py` prints it as
  the STARVED column.
- **F-104** 2026-09-26, reading the priority ladder after the counters said
  starvation: **core 1 carries six tasks and the stream is fifth of six.**
  `audio_mixer` 23, PPA pump 22, `se_mp3` 21, `usbnet` 10, `se_stream` 5,
  `cmworker` 4, with the render loop alone on core 0. Lowering `WORKER_PRIO`
  to 4 (F-101's fix) moved the worker below the stream and did not help,
  which in hindsight it could not: the worker was never the only thing above
  it. Nothing above priority 5 has been ruled out by measurement yet, and
  the loader does not export `uxTaskGetSystemState`, so the app cannot
  enumerate tasks -- which is why the residual above had to be built by
  hand.
- **F-99** 2026-09-26, with the stream reaching OBS and OBS showing nothing:
  **the H.264 parameter sets were sent too rarely to join.** The encoder
  puts SPS and PPS in front of every keyframe, so about once a second at
  `gop` 20 -- and over UDP there is no connection, no handshake and no way
  to ask for a replay, so whether a receiver ever decodes depends on where
  in the GOP it happened to open its socket. It does not fail loudly: the
  transport stream parses, the program is found, the stream is correctly
  identified as H.264, and then it is `unspecified size` and
  `non-existing PPS 0 referenced` for ever.

  **The trap underneath it is that two receivers disagree.** `ffplay`
  waits for a keyframe and plays the same stream perfectly. OBS opens its
  media source with `analyzeduration 0` and decides what the stream
  contains from the first packets it happens to see. So the first verdict
  was "works in ffplay, not in OBS", which reads like an OBS problem and
  is not one -- **"it works in one player" is not evidence that a stream
  is correct.** The user's own observation is what broke it open: *"it's
  very sporadic, even in ffplay. It sometimes works when i join mid
  stream, sometimes not."* Sporadic, not broken, was the whole clue --
  parameter sets that were missing entirely could never have worked.

  Measured rather than argued, on the host: mux a real H.264 stream
  through `tsmux`, truncate it at every packet boundary in turn (which is
  what joining a live UDP stream late amounts to) and ask ffprobe with
  OBS's settings whether it can find the video size. **50.2% of offsets
  before, 94.9% after**, the residual being only the last packets of a
  finite file where there is not enough left to probe. That 50% is exactly
  the coin flip that was reported. Fixed in `tsmux` by caching the
  parameter sets and putting them in front of EVERY access unit: forty
  bytes a frame, 8 kbit/s against a 3 Mbit/s stream, and a stream any
  receiver can join at any instant.

- **F-100** 2026-09-26, when `make install` could not reach the badge:
  **quitting an app while it was streaming left the USB-C PHY with the OTG
  controller, and the badge unreachable by console AND BadgeLink.**
  `se_stream_stop()` was only ever called from the menu row that turned
  the stream on, so returning from the run loop -- which is what "quit to
  the launcher" does -- never gave the port back. The launcher then runs
  perfectly and cannot be talked to by either channel, with nothing on
  screen to say why, and only a power cycle clears it. Fixed in
  `se_run()`, not in the game: the stream is the ENGINE's resource, the
  engine started it, and a game should not have to remember. (F-01's PHY
  swap is the mechanism; this is the missing counterpart to it.)

- **F-101** 2026-09-26, out of **the user**: *"audio seems quite a bit
  delayed (multiple seconds)"* -- **the stream carried two clocks running
  at different rates.** The audio PTS counted SAMPLES, which is real time
  by construction. The video PTS was `s_seq * 90000 / fps_hint`: a count
  of encoded frames scaled by the rate the encoder was *configured* for.
  But `fps_hint` is a hint, the game renders at whatever it manages, and a
  frame offered while the encoder is busy is dropped without advancing
  `s_seq` at all -- so that clock ran at (actual rate / 20) of real time,
  typically well under half. With the PCR riding the video PID, the
  audio's timestamps pull steadily ahead of the stream clock and a player
  holds them back to match: the sound arrives late by an amount that
  **grows**, reaching seconds within a minute.

  The old comment is the finding in miniature -- *"the game's rate varies;
  the player only needs a clock that advances evenly"*. True of a stream
  carrying video alone, and wrong the moment there is a second stream that
  has to agree with it. Fixed by making the video PTS real elapsed
  microseconds, **stamped when the frame is captured** rather than when the
  encoder gets to it, which can be tens of milliseconds later. Confirmed
  on the badge: the offset stops growing.

  Still open: a possible CONSTANT audio/video offset, untested. If there is
  one, the likely cause is known -- the audio ring can hold up to
  `RING_FRAMES` x 1152 samples, about 418 ms, accumulated while USB
  enumerates, and the first frame taken out of it is that stale while being
  stamped as current. Clearing the ring when the stream task starts is the
  fix; it was deliberately NOT done at the same time as the clock fix, so
  that one change could be attributed.

- **F-98** 2026-09-26, noticed only when the encoder was made a submodule
  and the app could not carry it: **`git add -A` had silently flattened the
  `synthengine3D` submodule into 133 plain files.** `c8e5944` replaced the
  gitlink with a tree --

      before:  160000 commit f28855c...  synthengine3D
      after:   040000 tree   22ba661...  synthengine3D

  -- and nothing complained, because a submodule whose `.git` is a *file*
  (a gitdir pointer, which is what a submodule checkout has) does not trip
  the "embedded git repository" warning the way a nested `.git` directory
  does. The symptoms were quiet and easy to misread as normal: engine
  changes showed up as modified files in BOTH repos, so every engine commit
  had to be made twice, and the app's history was quietly accumulating a
  duplicate of the engine's. `.gitmodules` had described `synthengine3D` as
  a submodule since the project's first commit (`fd9be00`) and the Makefile
  still said a fresh clone needs `--recursive`, so the intent was never in
  doubt -- only the index disagreed. Fixed with `git rm -r --cached
  synthengine3D` then `git add synthengine3D`, which restores the gitlink;
  `git submodule status --recursive` now walks app -> engine -> pdmp2.
  **The check that would have caught it is one line**: `git ls-files -s
  synthengine3D` should print ONE entry at mode 160000, never a list.

- **F-95** 2026-09-26, writing the Layer II encoder and getting a stream no
  decoder would open: **the MPEG audio syncword is TWELVE bits, not eleven.**
  The header is sync(12) ID(1) layer(2) protection(1), which is 16 bits
  before the bitrate field. Written as sync(11) then ID it is 15, so every
  field after it is shifted by one -- and the failure hides, because with
  `ID = 1` (MPEG-1) eleven ones plus a one IS twelve ones, so the first two
  bytes come out right and only MPEG-2 breaks. The class of bug matters more
  than the bug: a wrong bit here does not crash anything, it produces a file
  that ffmpeg declines with "Failed to read frame size". Fixed by writing
  `0xFFF` in 12 bits, and the header bytes now match ffmpeg's own encoder's
  byte for byte, which is the check that should have been run first.

- **F-96** 2026-09-26, with the encoder structurally correct and sounding
  terrible: **the analysis window is not a design choice, and no amount of
  bits hides a wrong one.** A 512-tap Kaiser-windowed sinc, cutoff pi/64,
  stopband over 100 dB -- a better filter than the standard's by any ordinary
  measure -- gave **16 dB SNR**, and raising the bitrate by 50% moved it less
  than 1 dB. That flatness is the whole diagnosis: if the quantiser were the
  limit, more bits would help, so the filterbank was the limit. The reason is
  that this is a cosine-modulated *pseudo*-QMF bank: adjacent subbands
  overlap heavily and their aliasing only CANCELS against a synthesis
  prototype that is the analysis prototype time-reversed -- and the synthesis
  side lives in the decoder, fixed by the standard. The window is therefore
  as much an interface as the allocation tables are. Fixed by MEASURING it:
  push single subband samples through a reference decoder, fit the basis
  functions that come back (residual 0.0005%), reverse them. 16 dB became 64.
  Two sub-traps on the way: the injected sample was large enough to CLIP the
  basis functions, which fits nothing, and the bands at or above `sblimit`
  decode as silence, so leaving them in the fit put their whole energy in
  the residual and made a perfect fit look like a 6% failure.

- **F-97** 2026-09-26, with the filterbank fixed and a 1 kHz tone still at
  27 dB: **a masking model that has to be tuned will be tuned wrong.** With
  the bank working, a loud tone leaves about -80 dBFS of skirt in every other
  subband. Whether the spreading model called that masked turned on a
  constant being 7 or 8 dB per band; on the wrong side of it the allocator
  served thirty bands of filterbank leakage before the band with the signal
  in it. There was a second, coarser version of the same mistake underneath:
  the model used a band's own level as its masking threshold, so one
  allocation step always looked good enough and the allocator stopped having
  spent almost nothing. Fixed by deleting the model. Plain rate-distortion
  greedy -- most noise reduction per bit, mean square error, no constants --
  cannot make either mistake, and a Layer II frame is a fixed size so there
  is never a reason to stop early: bits not spent are written out as zero
  padding and thrown away.

- **F-94** 2026-09-26, with the card migrated but the old directories still
  on it: **`sm_remove` cannot delete a directory, and said it had.** The log
  read `retire: could not remove /sd/craftminer (1 entries went)` -- one
  entry, meaning `wipe` found nothing to do and `sm_remove` had returned
  TRUE for a directory that was still there afterwards.

  ```c
  FRESULT const r = f_unlink(cand);
  if (r == FR_OK || r == FR_NO_FILE) return true;   // <- the bug
  ```

  `f_unlink` will not take a directory; it answers `FR_DENIED`. The loop
  then walks on to the next candidate spelling (`vfs_compat.c` tries four,
  because a VFS path is not a FatFs path), and a spelling that names the
  wrong volume answers `FR_NO_FILE` -- which this read as "already gone"
  and reported as success. **`sm_rename` has the same loop and accepts
  `FR_OK` alone, which is exactly why every rename in the migration worked
  and every directory removal did not.**

  Fixed two ways. `sm_remove` now counts only `FR_OK`, and asks `stat`
  rather than `f_unlink` whether the thing is gone. And directories get
  their own call, `sm_rmdir`, built on POSIX `rmdir` -- which graceloader
  DOES export, like `mkdir` and `stat` and unlike `remove`, so it needs
  none of the candidate machinery.

  **This was not the migration's bug.** `worldstore_delete` has ended with
  `sm_remove(region); sm_remove(dir);` since save slots arrived, so deleting
  a world has been leaving its empty directory on the card ever since, and
  reporting success -- invisible, because `worldstore_delete` returns
  `!slug_exists(slug)`, which only asks whether the level FILE is gone. Both
  call sites now use `sm_rmdir`.

- **F-93** 2026-09-26, the first time the migration ever ran on a card: **it
  blew the main task's stack.** The app crashed straight after the splash --
  `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)`, task
  "main", bounds `0x4ff277b8`-`0x4ff299b0` (**8.5 KB**), stack pointer
  `0x4ff27790`, *below* the lower bound. The backtrace pointed at
  `heap_caps_malloc`, which is only where the guard happened to notice; the
  log placed it exactly, between `miner_face.png` and `settings:`, which is
  where `on_init` runs the migration.

  The chain: `on_init` (`char report[1024]`) -> `datadir_rename_saves`
  (`slugs[32][24]` plus four 512-byte paths, ~2.9 KB) -> `rename_world`
  (three more, 1.5 KB) -> `rename_batch` (`names[24][64]` plus two more,
  ~2.6 KB). **Over 8 KB before `datadir_retire` even starts**, and `wipe()`
  then recursed up to eight deep with another 2 KB a level. The comment on
  `DD_PATH` had said the 512 bytes were there "so that `join` never has to
  refuse, not because it might" -- generosity written into the one routine
  that runs on the smallest stack in the program.

  Fixed: `DD_PATH` 160 (the longest path this really builds is 82),
  `DD_NAME` 64, `BATCH` 12, `RETIRE_DEPTH` 5, and every name array moved
  into statics -- which for this app are in PSRAM, since kbelf loads app.so
  there, so they cost no internal SRAM at all. +6656 bytes of bss, and the
  deepest chain is now under 1.7 KB.

  **No host check could have caught this**: worldcheck runs on a PC with an
  8 MB stack, so the same code passes there and always will. The lesson is
  about where the code runs, not about what it computes.

  What it left on the card is the good news, and only by luck: adoption had
  finished, `level.cmw` had become `level.smw`, and all 21 region files were
  still `.cmr`. Nothing was lost -- but that exact state is what D-93 is
  about.

- **F-92** 2026-09-25, running G6 test 4: **the `shots` hashes cannot compare
  two renderers, because the scene does not reproduce itself.** `bench` and
  `bench_banded` differed at all five moments -- and so did two runs of
  `bench` against each other (three different hashes at t=20 and t=24 across
  three runs). The SHOT records show two separate faults. **The geometry
  differs**: at t=16 one run submitted 1028 flat triangles and another 926;
  at t=20, 1158 textured against 637. The renderers were never handed the
  same scene, so the pixels could not agree. That is F-45 again -- `shots`
  SETS the clock rather than running it, so a camera that jumps 96 blocks
  between moments finds a world that has not settled, and `LOAD_GATE_ALL`
  runs once at startup and never again. **And shading differs**: at t=0 and
  t=34 all three runs agree exactly on triangle counts and the hashes still
  split 1-2, so something in sky or time state carries across a launch. The
  control run is what exposed both; without it the inequality would have
  been read as a bug in the banded renderer. What it would take: settle the
  world at each shot moment, and pin the sky. Not done -- with one renderer
  left there is nothing to compare (step 39).

- **F-120** 2026-09-29, the user's first play of step 9, and it is one
  number: **the texture cache held 48 and the game wanted 64.**

  Their report was three complaints that turned out to be one bug:
  *"The textures of tomato, rice and potato plants as well as their icons
  (and for the seeds as well) look very much like stand-ins"*, *"Those
  'plants' also only slightly change color in their growth stages
  instead of actually growing"*, and *"I switched to transparent water in
  the settings, but i still got the old leave-like pseudo
  transparency."*

  `TEXCACHE_MAX` was 48 and had been since the showreel. Step 9 took
  `VM_COUNT` to 64, so `chunk_render_init`'s loop filled the cache at
  entry 48 and **everything after it failed to load**:

  | material | index | what happened |
  |---|---|---|
  | wheat 0-3 | 44-47 | loaded, by one slot |
  | potato, tomato, beans, rice | 48-63 | **flat colour, no texture** |
  | `water_blend.png` | after the loop | **never loaded** |
  | 3 torch frames, ~27 item icons | after that | **never loaded** |

  So the four crops that "looked like stand-ins" had no picture at all --
  they were their `argb` average, which is exactly why they *"only
  slightly change color"* as they grew, and why wheat was the one crop
  the user did not complain about. And transparent water could not be
  switched on because `chunk_render_set_water_blend()` refuses without
  the solid texture -- correctly, and silently as far as the screen was
  concerned.

  **THE BADGE SAID SO EVERY TIME.** `texcache_get` logs `cache full, %s
  not loaded` at ESP_LOGE for each one, which is about twenty lines in
  the boot log. Nobody read the log. That is the finding, more than the
  constant: a `make install` was run, a play session happened, and the
  first thing that would have explained all three symptoms in one line
  was never looked at.

  Fixed at `TEXCACHE_MAX 128`, and the invariant became a **static
  assert in chunk_render.c** -- `VM_COUNT + 36 <= TEXCACHE_MAX` -- so a
  new material fails the BUILD rather than quietly turning four blocks
  into coloured quads. A build failure is the right home for it: the
  numbers are both compile-time constants, and the failure mode without
  it is invisible.

  **AMENDED 2026-09-29 (step 10), at the user's instruction:** *"Also
  make sure the texture cache has an appropriate limit. We have run into
  issues before."* The fix above was still half a hand-count -- the 36
  was a number somebody had counted once, which is precisely the shape
  of the original bug. Three changes:

  * the by-name half is now **derived**: `TEX_BY_NAME` in items.h is
    `ITEM_COUNT - BLK_COUNT` plus a fixed slack, so **every new item
    moves the requirement by itself** and the assert cannot go stale;
  * `tools/worldcheck.c` counts what the game will really ask for --
    an icon per non-block item, one per block drawn as a thing rather
    than a cube, and the six files that belong to nothing -- and fails
    if those outgrow the slack. So the estimate cannot rot either: one
    of the two fails first, and both fail on the host;
  * `texcache_report()` prints **how full the cache ended up** and warns
    when it is within eight of the wall. That is the part aimed at the
    real finding above: a line that says "seven left" is one somebody
    reads before the wall rather than after it.

  `TEXCACHE_MAX` is **192** after this round, which is 7.7 KiB of table
  and leaves room for the stove, the fish and the mobs. Measured today:
  74 materials + 42 by name = **116 of 192**.

- **F-121** 2026-09-29, step 10, and the new counting check above found
  it within a minute of existing: **an open fence gate was asking for a
  texture that does not exist.** It carried `BF2_ITEM_ICON`, which means
  "draw me from `item_<name>.png` in the inventory" -- but an open gate
  drops the SHUT one when broken, so it can never be in anybody's hands
  and `item_fence_gate_open.png` was never drawn. Live, it would have
  cost a cache slot and a `missing -- drawn flat` line for a picture
  nothing ever looks at.

  Small, and the point is where it was caught: a check that counts what
  the game asks for finds the things it asks for and should not, which
  a check that only tests the limit never would.

- **F-122** 2026-09-29, the user, on the first pen they built: *"I put
  two cows into an enclosure of fences. They where able to jump up the
  fence and escape."* **Two bugs, and the test that should have caught
  them had tested the one case that always worked.**

  The pen check written the same morning put a PIG in a BARE pen for
  two minutes. A pig is 0.9 blocks tall, a bare pen has nothing in it,
  and two minutes is not an afternoon. Every one of those choices made
  the test easier than the thing it was standing in for.

  **The first bug is the step-up, and it is ours rather than
  Minecraft's.** `PHYS_STEP` is a WHOLE BLOCK here -- a deliberate
  departure (player.h: this is a handheld, and tapping jump at every
  clod of terrain is tiring). A fence is a block and a half. So a body
  standing on **anything one block high beside a fence** -- a tuft of
  terrain, a plot of soil, a chest -- is lifted a whole block by the
  step-up, to 1.0 above its feet, which is **higher than the fence's
  top measured from the fence's own floor**, and the sideways move that
  follows carries it clean over and down the far side. Measured: a cow
  in a pen with one dirt block in it was out in seconds, and reached
  y 22.00 over a fence that tops out at 21.50.

  Fixed by refusing the LIFT rather than inspecting where it landed --
  by the time it has landed it is already outside the pen. A fence is
  not a step; everything else one block high still is, and the
  staircase and 1-block-step checks are untouched. Jumping onto a fence
  and falling onto one both still work: what is gone is the free block
  of lift.

  **The second bug was in the block table**, and only the broader test
  found it: `block_collide_top` keyed on `K_FENCE`, and **a gate is
  `K_GATE`** -- so a shut gate stood ONE block high to the collider
  while looking like part of the fence, and a cow hopped over it. The
  picture was in on it too: the gate was drawn 1.3 blocks tall against
  the fence's 1.5, which is a gate people would have tried to jump. Both
  are the fence's height now.

  A third change is about animals rather than geometry: **a creature
  does not jump at a fence.** It hops at an ordinary one-block step, so
  it can still follow you over broken ground, and it stops dead at a
  fence. The alternative was a taller fence, which is not a height -- a
  creature standing on a block beside a two-block fence wants a
  three-block one. A rule about what a fence IS settles it at any
  height. (The first version of that rule probed along the creature's
  yaw and missed both the corner case and the case that mattered: a
  fence beside a body standing on a block is in the cell BELOW its
  feet.)

  What replaced the test is five pens -- bare, with a block inside, with
  a shut gate, with an open one, and with a deliberate staircase -- a
  cow rather than a pig, and 30000 ticks rather than 2400. The
  staircase one is checked and **allowed**: two blocks stacked inside is
  the player's doing, and the rule is "a fence is not a step", not "a
  fence is a forcefield".

- **F-123** 2026-09-29, the user, straight after the fences: *"cows
  phase through each other and the player."* They did, and it was not
  an oversight in the creature code -- it is what `physics.h` says on
  its first line: *"An axis-aligned box swept against the voxel world...
  nothing here knows what a player is."* The collider knows about the
  WORLD. Two dozen bodies moving through each other is what that buys,
  and until there was more than one body it cost nothing.

  Fixed with a **soft push** rather than hard collision, which is
  Minecraft's answer and the right one here: overlapping bodies are
  eased apart a little each tick along the line between them. A hard
  one would need the sweep to test against moving boxes -- and two
  animals in a corner would lock solid instead of squeezing past each
  other, which is worse than what it replaces.

  Four things it has to get right, and each is a line in the check:

  * **it stops.** A push that keeps pushing walks a herd off the edge
    of the world over an afternoon, so it is capped per tick and it
    ends the moment the two are clear;
  * **the world still wins.** Every push goes through `phys_move`, so
    three cows squeezed against a wall stay on their own side of it;
  * **the player is moved less than the animal** (0.35 of it). Being
    shoved about by livestock is annoying in a way that shoving them is
    not;
  * **a sitting dog is furniture.** It holds its ground and the player
    goes round it, which is the whole point of telling one to sit.

  The separation runs as a pass AFTER everything has moved, not during:
  pushing as it went would give the creature with the lower pool index
  the advantage, and a herd would drift the way the pool is ordered. A
  pair that is exactly coincident -- which happens the moment a calf is
  born between its parents -- parts along a direction taken from the
  hash of the two ids, not a constant, or every such pair would part
  the same way for ever.

### Decisions (D-n), each with date and who decided

- **D-81** 2026-09-23, **the user**: **the UI is translated, English by
  default, into German, Dutch, Flemish, French and Bulgarian**, with the
  language an easy reach in Settings. Machine translations to begin with,
  "but the translations should be easy to adapt by humans" -- so the text is
  text files (`lang/*.txt`, `key = text`, UTF-8) and never a string literal
  in the code, and the cheapest thing at run time was asked for as well.
  Both: the files are baked into arrays by `tools/make_lang.py`, so a lookup
  is an array index and nothing is parsed while the game runs, and a player
  with no toolchain can still drop `/sd/synthminer/lang/<code>.txt` on the
  card to correct what ships. `lang/en.txt` defines the keys; a language
  missing one shows English, and a key English does not have is an error.
  Language names are never translated -- the player who needs that row is
  the one who cannot read the language the game is in. Not translated:
  the name SynthMiner, world names, the key names printed on the badge's
  own keys, and every log line (the user: "the debug output stays
  untouched").
  **Amended the same day, the user:** support the *languages*, not the
  strings that exist today -- the font must cover every letter of every
  alphabet, so that adding or changing a string can never meet a character
  the font lacks. The check therefore runs over alphabets, not over
  translations, and how to extend the font is written down for the next
  language (`synthengine3D/tools/hershey/README.md`).
  **Amended again 2026-09-23, the user:** all of them -- every European
  language the font could be made to reach, Turkish included, 32 in total.
  English first in the list and the rest **alphabetical by the name each
  language calls itself**, "to make it easy to find": the name a player is
  looking for is the one they can read, so the list sorts by that and not by
  English name or by code. The translations past English stay a machine's
  work, and the README now asks for pull requests by people who speak them.
- **D-82** 2026-09-23, **the user**: **the music is MIDI files of classic
  pieces that are out of copyright**, played "at random intervals with long
  pauses between them, similar to old minecraft versions", chosen randomly
  rather than in playlist order, "with the only limit to not play the same
  piece twice in a row".

  Three things follow, and the third is the one that needed care.

  * **MIDI, not MP3.** The engine has `se_mp3.h` and it would have worked,
    but the cost is a decoder task with a 32 KB stack, a 64 KB PCM ring and a
    16 KB read buffer, and the music itself is megabytes. The eleven pieces we
    ship are **72 KB in total**, and the sequencer allocates nothing but the
    file. `se_voice.h` had said all along that "a future MIDI player will keep
    a pool of voices and route note-on / note-off events to them"; this is
    that player, so the engine did not have to change at all.
  * **The silence is the feature.** A piece, then four to ten minutes of
    nothing, and a fresh random gap each time. The first gap is shorter
    (25-70 s) so that a player who puts the badge down after five minutes has
    at least heard that the music exists.
  * **TWO COPYRIGHTS, not one.** A MIDI file carries the copyright of its
    *engraving* as well as of the composition. Satie, Debussy, Chopin,
    Schumann and Bach are all long out of copyright; the particular typeset
    edition a file was generated from is a new work and usually is not. A
    MIDI file found loose on the web is almost never licensed to
    redistribute, whoever wrote the tune. So every file comes from the
    Mutopia Project, which publishes an explicit licence per piece -- and
    only from its **Public Domain** set, never its Creative Commons one.
    That cost us the Gnossiennes, which are BY-SA, and share-alike would
    have put conditions on anyone redistributing SynthMiner. The
    Gymnopedies are Public Domain, so the mood survived.
    `tools/get_music.py` **refuses to download anything that is not Public
    Domain**, so extending the set cannot go wrong by accident, and
    `assets/music/MUSIC.md` records where each file came from.

- **D-83** 2026-09-23, Claude: **what a block sounds like is a field in the
  block registry, not a switch in the audio code.** `block_def_t.sound` names
  a material class (SND_STONE, SND_WOOD, ...) and the footstep, the break and
  the place all follow from it. This is the same extendability contract as
  the rest of the table (Part L): adding a block is one row, and the row now
  carries its sounds. The alternative -- a lookup in `sfx.c` keyed on block id
  -- would have been a second table to keep in step with the first, and the
  one that got forgotten.

  The effects themselves are **one table, not one file per noise**. The
  synthracer modules this is descended from gave each effect its own file,
  which is right when there are seven and each is a different idea, and wrong
  here, where a footstep on gravel and a footstep on sand are the same idea
  with different numbers. A row is a tone layer and a noise layer through one
  filter and one envelope, with per-play pitch jitter so no two footsteps are
  identical.

- **D-84** 2026-09-23, Claude: **the game thread reads the card; the mixer
  only plays.** `se_audio_source.h` forbids blocking in `render()`, and a
  MIDI file lives on the SD card. Unlike the MP3 source, which needs a whole
  decoder task to bridge that, a MIDI file is small enough to read whole:
  `music_frame()` loads it on the game thread and hands it over through a
  single atomic state word, each state having exactly one writer. Parsing
  happens in `render()` because it is a few hundred bytes of header walking
  with no I/O. If a file is slow to load the silence simply lasts a moment
  longer; nothing on the audio path ever waits.

- **D-80** 2026-09-22, **the user**: **the player's data lives in
  /sd/synthminer, not in the app's install directory.** The launcher owns
  /sd/apps/<slug> and may empty it on an update or a reinstall; worlds,
  settings, replays and screenshots must survive both. The install
  directory keeps only what ships with the app. Data an earlier build left
  there is moved across on start (a rename per entry, never overwriting),
  so nobody's world is lost in the move. Amends D-67's "next to the
  worlds, in the app's directory".
- **D-78** 2026-09-22, **the user**: **the Far Lands are a short walk west,
  sudden, and Beta 1.7.3's own.** The edge moves from x = -100000 to about
  5-10 minutes' walk from spawn (x = -2048, about 8 minutes at walking
  speed); the ramp goes -- a cliff of jumbled terrain, as in *Far Lands or
  Bust*; and the look should be as close as possible to Beta 1.7.3's Edge Far
  Lands, which Part X gets by porting Beta's generator and overflowing it
  rather than imitating the result. Later, signs such as "Kurt was here" and
  "Wolfie was here" at random along the edge. Replaces the first Part X.
  Confirmed the same day: -2048 it is, **as long as it can be changed
  later** -- hence the edge is stored per world, and a new default only
  reaches new worlds.
- **D-79** 2026-09-22, **the user**: **bedrock and gravel are blocks, and
  signs exist -- generated only, for now.** Bedrock (17, unbreakable) and
  gravel (18) come with the Far Lands, which Beta built from them. Signs
  (19) stand where the generator puts them and cannot yet be placed,
  written or carried; breaking one drops nothing. Their texts are a fixed
  list drawn into textures, so a sign needs no stored text until players
  can write their own.
- **D-77** 2026-09-22, **the user**: **the quarter-resolution depth plane
  lives in internal SRAM**, and the flat triangle list gives up its internal
  SRAM for it (the list's cap stays at 6144, which F-63 needed). An engine
  option, off by default, so other games keep their layout; SynthMiner turns
  it on in CMakeLists.txt. Full resolution keeps the PSRAM plane (F-67).
- **D-76** 2026-09-22, **the user**: **near is the default view distance
  again**, replacing D-66, once the walk measured medium at 7.9 fps against
  near's 9.6. It only changes what a player gets before choosing: a
  settings.txt that says otherwise (the user's own says Far) is kept.
- **D-74** 2026-09-22, **the user**: **a block id, once shipped, never
  changes** -- nor its name, and a block is never removed, only retired. Item
  names likewise. That makes F-52 impossible instead of fixing it, costs a
  lifetime limit of 255 blocks (past which is a format change anyway), and is
  enforced rather than remembered: explicit ids in `blocks.h` and a registry,
  `tools/ids.txt`, that `make check` holds the code to in both directions.
  Adding a block is appending a line there in the same commit. The palette
  stays, as a safety net that now has nothing to translate.
- **D-75** 2026-09-22, **the user** (the upgrader), Claude (the slot states):
  **a structural change is met by a one-time upgrade of the whole world,
  written when the first such change exists.** The major version in each
  file's magic is the hook (D-32). Meanwhile the slot list says what an
  unreadable slot is -- newer, older, damaged -- rather than showing it as
  empty. When the upgrader is written it must be crash-safe: convert region by
  region into new files, rename each over its original, record each region's
  version, and move the world's version on last, so an interrupted upgrade
  resumes rather than leaving a mixture.
- **D-72** 2026-09-22, Claude: **the geometry caps are 6144 flat (internal
  SRAM), 256 lines, 4096 textured (PSRAM).** The line list was 112 KB of
  internal SRAM for the dozen lines of a block outline; shrinking it pays for
  the bigger flat list, which stays in fast memory. The far meshes round light
  to every fourth level for merging (16% -> 4% extra triangles there); the
  near ones keep every level, where banding would show. The frame stats now
  print how full each list is. Medium stays the default (D-66); at 7-9 fps
  that is the user's call to revisit, with the frame-rate work they deferred.
- **D-73** 2026-09-22, **the user**: **Fred's handedness is a setting**, right
  by default, and both views follow it.
- **D-68** 2026-09-22, Claude: **dropped items are saved with the world
  record, not per chunk.** D-33 wanted them in each chunk's own section; that
  means handing entity data to and from the worker with every chunk save and
  load, for a pool of 96. A list in level.smw is far simpler and keeps what
  D-33 actually promised, because an item in an unloaded chunk holds still (no
  fall, no ageing, no pickup) until its chunk returns. Revisit when there are
  creatures, whose numbers will not fit one list.
- **D-69** 2026-09-22, Claude (the design; the user asked for static torch
  light): **light is a derived plane, drawn through a table.** One byte a cell,
  sky and block light, never saved; flooded on arrival and on every change.
  Faces carry the byte; brightness = a table of it, rebuilt each frame from the
  time of day and handed to the engine as `SE_TRI_LIGHT` -- so night falls
  without re-meshing anything. Costs 4 MiB of PSRAM and four bytes a mesh
  triangle (mesh_tri_t 32 -> 36 bytes).
- **D-70** 2026-09-22, Claude: **the first-person arm is drawn at a third of
  the showreel's distance and size.** Same picture; but at 0.9 blocks out it
  vanished into any block the player was touching, which is every block they
  mine.
- **D-71** 2026-09-22, Claude: **+z is north, +x east.** The camera turns right
  from +z to +x, which is north-to-east on a map with north up, and the sun
  rises at +x. The position overlay's compass says so.
- **D-60** 2026-09-22, **the user**: **named worlds in save slots, and the
  Testworld moves into slot 1.** Eight slots, directories `slot1`..`slot8`, the
  name in level.smw — so a name can be anything the keyboard types and renaming
  never moves a file. The pre-slots world goes into the first free slot as
  *Testworld*, by a single directory rename, the first time the new build
  starts.
- **D-61** 2026-09-22, Claude: **opening the pause menu saves.** On a handheld
  the way people stop is to switch it off, and pausing first is the habit. This
  is still "only when needed" in Part N's sense: it happens once per pause,
  never on a tick or a timer. It costs a short stall while edited chunks are
  written.
- **D-62** 2026-09-22, Claude: **the inventory is saved by item name**, the rule
  D-31 applies to blocks, and for the same reason. A name the build no longer
  has drops that stack rather than turning it into something else.
- **D-63** 2026-09-22, Claude: **Esc always pauses**, whatever Pause is bound
  to. Rebinding Pause must not be able to remove the way out of the game, and
  Esc is also the menus' Back, so it is the key everyone will try.
- **D-65** 2026-09-22, **the user** (the feature), Claude (the sensor): **look
  by turning the badge, with the keys still live.** The RATE gyroscope, not the
  accelerometer synthracer steers with: tilt is an absolute angle, right for a
  steering wheel, and cannot say which way you face. Integrated per frame,
  consumed per tick with the key delta. Not in the replay mask, so a replay of a
  gyro session will not look where the player looked -- replay is unwritten
  (5.8), and when it is written the gyro delta has to be recorded with it.
- **D-67** 2026-09-22, **the user**: **the game's settings live on the SD card**,
  so a player backs up everything with one copy of the app directory.
  `settings.txt` beside `worlds/`: plain `key=value`, unknown keys ignored,
  missing ones defaulted, written to `settings.tmp` and renamed into place. Key
  bindings included, keyed by each action's stable short id; for that the
  engine's `se_bindings` gained a mode with no NVS at all (a NULL namespace,
  engine 2.1), which leaves games that pass a namespace, synthracer among them,
  exactly as they were. Volume and brightness stay the launcher's. The NVS
  values written by the builds of the same day are not migrated: they existed
  for hours, on one badge.
- **D-66** 2026-09-22, **the user** (replaced by D-76 the same day): **medium is the default view distance.**
  Near was chosen when medium measured about 4400 flat triangles, past the 4096
  cap (the comment that said so went with the settings rewrite). Sectioning and
  culling have changed that number since; the GEOMETRY DROPPED log line is what
  will say if it has not changed enough.
- **D-64** 2026-09-22, Claude: **debug keys yield to bindings.** F (free
  camera) and P (pause the scripted flight) act only on a key no action is
  bound to. T and V are gone: their jobs are in Settings -> Graphics. N (added
  later the same day, asked for by the user) moves the world's clock a quarter
  of a day on, for looking at night without waiting for it.
- **D-57** 2026-09-21, **the user**: **generate every chunk before the intro
  runs.** The streamer asks for four chunks a frame so that walking never
  stalls, but "never stalls" and "is complete" are different promises and an
  opening sequence needs the second. The title writes its letters with
  `world_set()`, which no-ops on a chunk that is not resident, so a title that
  starts before its world exists spells half a word -- and which half depends
  on the SD card that morning. `pregenerate()` runs the streamer to completion
  inline first: 2.8 s for the title's 81 chunks, all of it behind a screen that
  is not yet showing anything.

- **D-58** 2026-09-21, Claude: **the title runs on a scratch world.** Generated
  from a fixed seed, never written, and absent from the world list. The letters
  are real blocks placed through `world_set()`, so the greedy mesher, the
  streamer and the fog treat them as what they are -- the title screen is a
  view of the game rather than a picture of one. The seed was CHOSEN, not
  picked: the first one put the camera over open ocean, so every seed under
  4000 was scored on the ground it gives across the camera's field of view.

- **D-59** 2026-09-21, Claude: **a `shots` test switches the chunk worker to
  synchronous and settles the world before drawing.** That is what D-15 put
  synchronous mode there for. Until now every shot was a photograph of empty
  sky (F-45), so the hashes covered the overlay and nothing else.

- **D-56** 2026-09-21, **the user**: **the engine's splash goes first, and the
  game's name waits for its title screen.** `se_splash()` -- the SynthEngine
  wordmark over `se_version_string()`, so the version tracks the engine instead
  of going stale in a string here. The "SynthMiner / a block world" card that
  was there was a placeholder; the game's own title belongs on the title screen
  (step 5.1), not on a second text splash the player sits through every boot.

- **D-53** 2026-09-21, Claude: **item ids below `BLK_COUNT` are block ids.** A
  stack of cobblestone is item id `BLK_COBBLE`; `id < BLK_COUNT` is the whole
  test for "this is a block". Not a coincidence to be tidied away later — it is
  what stops two registries drifting, and it is why the inventory can draw a
  block without a sprite: `block_def()` already describes it. Real items — coal,
  a pickaxe — start at `BLK_COUNT` and carry their own row.

- **D-54** 2026-09-21, Claude: **a tool's icon is a shape, not a colour.** Three
  stone tools drawn as flat squares are three near-identical grey squares, and
  picking the wrong one is then something you discover by swinging it. A handle
  plus a head in the class's outline is tellable apart at a glance and needs no
  texture. Real sprites (D-03) replace it without a caller changing.

- **D-55** 2026-09-21, Claude: **the player starts with a stone pickaxe, axe and
  shovel and some blocks.** Crafting is step 8, and without a kit neither
  durability nor break speed nor placing can be tried at all. It is six lines in
  `player_spawn` and it goes the day a crafting table can make them.

- **D-51** 2026-09-21, **the user**, before block 4 was written: **a persisted
  duration is a count of ticks elapsed — never seconds, never a timestamp.**
  Prompted by dropped items, but it is the rule for every timer the world
  saves: crop growth, furnace burn, breeding cooldown, hunger, mob despawn.

  Two failures it prevents, and the second is why the distinction is not
  pedantry:

  * **wall-clock seconds**: the player pauses, or the frame rate changes, and
    the timer no longer matches the simulation. Part T rule 1 already bans
    reading wall time inside a tick; this extends the ban to what a tick
    *stores*.
  * **an absolute "spawned at tick N", compared against a world clock**: a
    world loaded a week later is fine — the world clock only advances while
    someone is playing — but a chunk that was *unloaded* for an hour of play
    is not. Its items would come back already expired, having aged while
    nothing was simulating them. **The stored field is elapsed ticks,
    incremented by the entity's own tick**, so an unsimulated chunk's clocks
    simply stand still. That is also what Minecraft does, and it is what makes
    "walking away does not cost you your drops" true rather than aspirational.

  Width is **uint32**: 10 minutes is 12000 ticks, but a uint16 caps at 54
  minutes, which a furnace timer or a crop over a long session would reach.

  Wall-clock time survives in exactly one place — `world_meta.last_played`,
  for the world-select screen. That is a thing to show a human, not a thing a
  tick reads.

- **D-52** 2026-09-21, Claude (raised by D-51): **the world's tick count is
  world state and belongs in `level.smw`.** `player_state_t.time_of_day` is on
  the wrong record — a world has one time of day however many players it has
  had, and a day/night cycle (step 29) reads it. Moving it is a block 5 job,
  and cheap because both records are tagged and skippable (D-30): the old
  field is simply ignored where it was and defaulted where it now lives.

- **D-01** 2026-09-20, Claude: **a floating render origin.** The engine subtracts
  the camera after world floats reach it, so coordinates near 100000 lose ~0.008
  blocks of precision — exactly where the Far Lands are. Chunk meshes are built
  in chunk-local coordinates and offset at submit; **no absolute world coordinate
  ever reaches `se_scene`**, `voxel_fx` included.
- **D-02** 2026-09-20, Claude: **fixed 20 Hz simulation, rendering interpolates.**
  Stable physics, exact replays, and the pure-function-of-showtime property the
  `shots` test needs. See Part T.
- **D-03** 2026-09-20, Claude: **one texture per material, no atlas.** The greedy
  mesher emits UVs in block units and relies on the rasterizer's power-of-two
  mask wrap to tile them; an atlas would force per-block quads.
- **D-04** 2026-09-20, Claude: **chunk payloads get their own binary format**
  (F-05); NBT keeps the world header, the player and the world index.
- **D-05** 2026-09-20, the user: **F1-F6 are hotbar slots**, so `f1_exits` is
  false and leaving goes through a pause menu that saves first.
- **D-06** 2026-09-20, the user: **both textured and flat shading**, exposed in a
  graphics menu with render scale and view distance; default textured, half scale.
- **D-07** 2026-09-20, the user: **the first playable build is walk / mine /
  place / save** — terrain, streaming, collision, break and place, hotbar and
  inventory, worlds on the SD card. No mobs, crafting or farming yet.
- **D-08** 2026-09-20, the user: **full survival** — health, hunger and tool
  durability.
- **D-09** 2026-09-20, Claude: **`SE_SCENE_TEXTURED_TRI_CAP` -> 2048**, flat stays
  at 4096 (F-04); set before `add_subdirectory(synthengine3D)`.
- **D-10** 2026-09-20, Claude: **stay on `SE_RENDER_ZBUFFER`** with `frustum_cull`
  and `depth_order` on (F-03).
- **D-11** 2026-09-20, Claude (render half **reversed** by D-34, storage half
  still standing): **chunk 16 x 16 x 64, one column, no vertical
  chunking.** 32 is too shallow for mining plus build room plus a bedrock-to-sky
  wall; 64 keeps the single-column layout the donor mesher and `fill_fine` want.
  Revisit sectioning only if remeshing shows up in a profile.
- **D-12** 2026-09-20, Claude: **two parallel 1-byte planes** (id, state), not one
  interleaved `uint16` plane, because `voxel_mesh_build()` takes a
  `uint8_t const*` and interleaving would force a de-interleave on every remesh.
- **D-13** 2026-09-20, Claude: **a 16x16 ring with a `cx`/`cz` identity check**,
  not a hash. `chunk_find` is the hottest function in the program.
- **D-14** 2026-09-20, Claude: **unloaded chunks read as `BLK_BARRIER`** — the
  player stops at the edge of generated terrain rather than falling through it.
- **D-15** 2026-09-20, Claude: **a synchronous chunk-loading mode from day one**,
  required by the `shots` test and by new-world spawn.
- **D-16** 2026-09-20, Claude: **region files of 8x8 chunks**, not per-chunk files
  (FAT cluster waste and directory scans) and not Minecraft's 32x32.
- **D-17** 2026-09-20, Claude: **dual directory + serial + CRC32** for region
  durability, with a strict write order, rather than trusting `fsync` (F-06).
- **D-18** 2026-09-20, Claude: **a world index plus a FatFs rebuild**, so a world
  hand-copied onto the card still appears.
- **D-36** 2026-09-20, Claude: **fix the game side before touching the
  engine.** The 66 ms frame splits roughly 21 ms game / 43 ms engine (G5), and
  the game's share is mostly geometry that should never have been submitted.
  Engine work -- a custom renderer through `se_renderer_register`, or SIMD in
  the span loops -- stays a stop-and-ask, and would in any case be measured
  more honestly once the noise is gone.
- **D-34** 2026-09-20, Claude: **render in vertical sections**, reversing the
  render half of D-11. Chunk storage stays one 16 x 16 x 64 column -- the
  planes, the codec and the save format are all fine -- but its *mesh* splits
  into 16-high sections with a bounding box each, so the frustum cull can reject
  everything below the ground. Measured cause in F-33.
  **Done 2026-09-21.** `vox_grid_t` gained `y0`; a chunk keeps `CH_MESH_N` = 12
  meshes (3 levels x 4 sections) in the store's PSRAM slab rather than in the
  static `chunk_t`, which took 35 KiB *off* the internal-SRAM bss as a
  side-effect. The level of detail stays the **chunk's**, not the section's:
  two stacked sections at different resolutions would not line up where they
  meet and the coarse skirt only closes the sides. Worth about 8% (F-35), which
  is less than F-33 predicted -- kept because the structure is right, the
  meshing saving is real, and it is what step 3.5's block edits need.

- **D-37** 2026-09-21, Claude: **a finding measured from one chunk is not a
  finding.** F-33 was stated from chunk (0,0), which turned out to be the one
  place in the world where the claim was both true and meaningless (it is sea
  floor, and 99% of its triangles are in one section). The rule now: any claim
  about "the terrain" or "a chunk" is measured over a sample spread across the
  world, and the sample size goes in the finding.

- **D-50** 2026-09-21, Claude: **the crosshair is inverted, not painted.** A
  white one vanishes against sand and a black one against a cave mouth; the
  inverse of whatever is behind it is legible against everything. Minecraft's
  does the same, for the same reason.

- **D-48** 2026-09-21, Claude: **movement constants are chosen by simulating
  the arc, not by feel, and the result is asserted in blocks and seconds.** A
  jump is three constants and an integration order, and the height that comes
  out is not something you can read off the source -- the first version was a
  third short and looked perfectly reasonable.

- **D-49** 2026-09-21, Claude: **a mesh that is out of date is still worth
  drawing.** Only one that has never been built is not. See F-43.4: treating
  the two as one state made the world blink at every block break.

- **D-45** 2026-09-21, Claude: **a step is a whole block, not Minecraft's
  0.6.** There, 0.6 exists so that stairs and slabs are walkable and full blocks
  are not; you jump for those. This is a handheld with a keyboard and no mouse,
  there are no stairs yet, and tapping jump at every clod of terrain is tiring
  in a way it is not with a hand already on a mouse. Walls are still walls: a
  step is kept only if the body can settle onto something afterwards, so two
  blocks stops you, and the host test asserts exactly that. Easy to reverse --
  it is one constant, `PHYS_STEP`.

- **D-46** 2026-09-21, Claude: **Esc leaves, until the pause menu exists.**
  `f1_exits` is now false because F1-F6 are the hotbar (D-05), so the engine's
  own way out is gone. Esc becomes "open the pause menu" in step 5.3, and that
  menu saves before it quits -- which is the reason the key had to come back
  from the engine in the first place.

- **D-47** 2026-09-21, Claude: **the hotbar holds six placeable blocks until
  block 4 gives it an inventory.** Cobble, planks, dirt, glass, torch, sand on
  F1-F6. Breaking and placing are the thing worth testing by hand now, and
  waiting for the inventory to test them would be waiting for the wrong reason.

- **D-43** 2026-09-21, Claude: **a limit the ring imposes is checked by the
  compiler, not by a comment.** `CH_RING`'s comment had said "a residency
  radius up to 7" since it was written, and a preset asked for 8 anyway
  (F-41). `CH_EVICT_MAX` plus `_Static_assert` makes that a build failure.

- **D-44** 2026-09-21, Claude: **never draw nothing when something is
  available.** If the level of detail a section wants is not built yet, draw
  one that is. A streaming world's visible quality is set by what it does while
  it waits, and "wait with a hole in the ground" is the worst of the options.

- **D-39** 2026-09-21, the user: **engine work is authorised**, and the engine
  stays at one version while it is being worked on -- no bump per change. It
  sat at **2.1** through the rasteriser work, the menus and the font, and went
  to **2.2** when the mixer gained public calls that a game outside this one
  would want (D-85, F-75).
  The standing "an engine problem means stop and ask" rule (D-15's sibling)
  still holds for anything beyond what has been asked for.

- **D-40** 2026-09-21, Claude: **`-O2`, not `-Os`, for both the engine and the
  app.** Both run from PSRAM, where code size is the resource that is not
  scarce, and `-Os` was costing a function call per span. Measured in F-39.

- **D-41** 2026-09-21, Claude: **measure the shape of the work before
  optimising it.** Three plausible theories here -- scalar arithmetic, PSRAM
  bandwidth, texel-fetch locality -- were each worth a day and each wrong, and
  a fourth (per-span setup over 6-pixel spans) was not on the list until the
  spans were counted. The engine now reports pixels *and spans* per pass
  (`scene_fill_stats`) precisely so the next person does not have to guess.

- **D-42** 2026-09-21, Claude: **no SIMD in the span loops.** Available,
  enabled, and the wrong tool: see F-40. Revisit only if spans get much longer,
  which is a geometry change, not an engine one.

- **D-38** 2026-09-21, Claude: **free flight when no test is running, the
  scripted path when one is.** `devtest_running()` decides. A `shots` test needs
  the frame to be a pure function of the show clock, and a camera driven by
  which keys are held is not -- but a world nobody can steer through is a world
  whose bugs are only found by accident. Both, chosen automatically, is the way
  to have the two properties at once. The debug camera deliberately does **not**
  go through `se_bindings`: those slots are the player's (D-05).
- **D-35** 2026-09-20, Claude: **an empty `on_backdrop` is required**, not
  optional, for any game that covers the screen itself. See F-31: the default
  clear costs 13 ms a frame and is invisible in the phase split.
- **D-29** 2026-09-20, the user (prompted by stairs, rails, redstone and
  furnaces): **three tiers of block state** — id, id + 7 bits of block-owned
  data, and a block entity for anything variable-sized. The state byte was
  originally split into fixed growth/variant fields; those were too small and
  too presumptuous, so bits 1-7 are now simply the block type's own.
- **D-30** 2026-09-20, the user: **extensible per-record data, so upgrades need
  no upgrade system.** Load = defaults, then overwrite what is recognised, then
  skip the rest; save always writes today's format, so a world rewrites itself
  as its chunks are unloaded. Implemented as `common/tags.h` (NBT's model over a
  byte buffer, because a chunk payload is built in memory on the worker) and, a
  level up, as skippable sections on the chunk payload.
- **D-31** 2026-09-20, Claude: **a block palette in `level.smw`** maps saved ids
  to names, so block ids can be added, reordered or removed. Entities and block
  entities name their type as a string instead and need no palette.
- **D-32** 2026-09-20, the user: **a major version in each file's magic**
  (`CMR1`, `SMW1`), bumped only for a change tags cannot absorb, and a mismatch
  is refused rather than guessed at so an upgrader has a definite hook.
- **D-33** 2026-09-20, the user: **creatures and dropped items persist with
  their chunk.** Creatures indefinitely, like blocks; dropped items for 10
  minutes of *loaded* time, so walking away does not cost the player their
  drops. Hostile mobs keep spawning in unlit places up to a cap per loaded
  chunk, which is what bounds the population given nothing despawns.
- **D-27** 2026-09-20, Claude: **`world/vfs_compat.{c,h}`** for the file
  operations graceloader does not export -- `remove`, `rename`, `unlink`,
  `opendir`/`readdir` (F-06). They go through FatFs, with the same
  try-the-plausible-spellings path mapping the engine uses
  (`se_mp3.c:198`), and through plain stdio under `SM_HOST` so the region
  checks still run on a PC. A missing export here is a **load-time** failure,
  which is why the device self-test exercises compaction rather than trusting
  `make verify` (F-27).
- **D-28** 2026-09-20, Claude: **region files carry their own CRC-32**
  rather than calling graceloader's exported zlib one. A region written on the
  badge has to validate on a PC and the other way round, and "two zlibs agree"
  is an assumption; a 64-byte nibble table makes it a fact. Verified against
  the standard check value (`"123456789"` -> `0xCBF43926`).
- **D-25** 2026-09-20, the user: **pre-generate the spawn area when a world
  is created**, as Minecraft does, behind a "Creating world" progress screen.
  This is where the 9.5 s of F-23 goes: once, visibly, at a moment the player
  already expects to wait -- instead of as a stutter every time they load the
  world. The chunks are *saved* as they are generated, so it also pays for
  itself twice over: every later visit to the spawn area is a disk read rather
  than a re-generation, and the one big write fits the
  "write only when needed" rule (one batch, at creation).
- **D-26** 2026-09-20, the user: **the spawn/respawn sequence is
  3 x 3 first, then play, then fill.** Load (or generate) the 3 x 3 chunks
  around the player **with physics frozen**, so they cannot fall through an
  absent world; the moment those nine are resident, gameplay runs normally;
  the rest of the view distance streams in over the next few seconds while the
  player is already moving. This refines D-19: the gate on running a tick is
  the **3 x 3 only**, never the full view distance, so a large view distance
  never delays the start of play. It also means respawning after death is
  normally a *load* of saved chunks, not a generation -- a bed is somewhere
  the player built, and world spawn was pre-generated at creation.
- **D-22** 2026-09-20, Claude: **`install` depends on a `mode` target** that
  calls `tools/testrun.py`'s own `ensure_badgelink_mode()` -- which probes
  BadgeLink first and only then asks for it on the console, retrying six
  times. The badge leaves USB in debug mode whenever the launcher comes back,
  and badgelink then fails with `ConnectionResetError`. An earlier attempt made
  `install` depend on the raw `mode_badgelink` target instead; that turned the
  console's flaky handshake (F-21) into a hard install failure, which was
  worse than the problem. Reuse the tool that already handles it.
- **D-23** 2026-09-20, the user: **`make ping`**, plus `make mode` and
  `make exitapp` -- thin wrappers in `tools/badgectl.py` over `testrun.py`'s
  connection code. When a cycle fails it is rarely clear whether the app is
  alive, wedged or never started, and `ping` answers that in two seconds
  (F-20 was exactly that confusion costing half an hour).
- **D-24** 2026-09-20, Claude: **generate `app_version.h`** from
  `main/app_version.h.in` with `git describe --always --dirty`, as the
  showreel does. Without it the app reports `git unknown` and `testrun.py`
  refuses every result as coming from an unidentifiable build -- correctly,
  but it looks like a link failure.
- **D-20** 2026-09-20, Claude: **textures live flat in `textures/`**, not in a
  per-segment subdirectory. SynthMiner is one app, not a reel with segments, so
  the showreel's `segcheck` separation buys nothing here. They install to
  `<app>/textures/`.
- **D-21** 2026-09-20, Claude: **the leaves-see-leaves rule became a flag**
  (`BF_SEE_SELF`) rather than the donor's hard-coded `b == VB_LEAVES`, so a
  future see-through block chooses its own behaviour in the table.
- **D-19** 2026-09-20, Claude: **a tick runs only when the 3x3 chunks around the
  player are ready.** No tick branches on load state, so replays are exact
  regardless of SD-card timing. This is the crux of Part T.

---

- **D-85** 2026-09-23, **the user**: **three volume sliders, and they are not
  the same kind of thing.** The existing one stays what it is -- the BADGE's
  volume, the launcher's setting, shared with every app -- and two new ones
  set "how it is mixed in" for the music and for the effects, inside the game.

  The distinction is the whole point and the menu is laid out to make it
  obvious: the device slider first, then each toggle paired with its own
  slider. Turning the badge down quietens everything including the launcher;
  turning Music down leaves the footsteps exactly where they were.

  It went in the engine rather than in our own render paths. Scaling our
  samples before the mixer's fixed gain would have been arithmetically
  identical and half the work, but the mixer is where mixing belongs, and
  `se_config.h` already owned the music-versus-effects balance -- the sliders
  ride on top of that rather than replacing it. Two calls,
  `audio_mixer_set_music_volume()` and `_set_group_volume()`, both additive,
  and the engine went to 2.2.

  One trap worth writing down: **a volume of 0 is not the same as off.** A
  silent source is still a playing source, so it still holds the speaker up
  (F-75). The enable gates remain the way to turn a class off.

- **D-86** 2026-09-23, **the user**: **water draws only its surface.** Not
  "no sides and bottom" as an optimisation -- as the definition of what a
  liquid IS in a renderer with no blending.

  (Amended twice. **D-102**, 2026-09-28: a flow or a fall draws its
  sides, because a falling column that draws only its surface draws
  nothing at all. **D-103**, 2026-09-29: the engine grew blending, so
  the surface is a real 50/50 mix rather than a cut-out standing in for
  one -- but the SHAPE below is unchanged, and it is what makes that
  affordable.)

  The problem was stated plainly: water is an opaque cube, "that's sort of
  fine for now. But if we enter a place where the water covers our 'eyes',
  the rendering breaks down." And the fix, equally plainly: "don't render
  sides and bottoms of water blocks, and only render the top of water blocks
  when the block above them is air", plus "maybe we could adapt the water
  surface texture so it has a checkerboard of transparent pixels (sort of
  like the leaves of trees), so you can sort of see through".

  That is the rule, and `K_LIQUID` is it. Two consequences the rule implies
  but does not say, and the picture is wrong without either:

  * **a liquid hides nothing.** `face_shows` returns true against a liquid in
    both mesh modes. Water used to be K_CUBE, so the stone under a lake had
    its top face culled: remove water's own faces and all you would see is a
    surface over a void.
  * **the surface is drawn twice, up and down.** An axis-aligned face is
    visible only from the side its normal points at -- that is the cheap cull
    the whole renderer is built on. One face means the surface disappears the
    instant the eye goes under it, which is the reported bug in a different
    costume. The downward copy is emitted by the AIR cell above the water, so
    it lands on the same plane for free (`emit()` puts a +y face at y+1 and a
    -y face at y) and merges greedily like any other.

  The checkerboard was the user's idea and it is the right one: alpha here is
  one bit (se_texture.h), so "see through" can only mean holes, and a regular
  grid reads as a half-transparent sheet where the leaves' random scatter
  would read as damage. The wave crests stay solid, or the water looks torn.

  **Swimming** came with it, since water you can see into is water you will
  end up in. Buoyancy cancels almost all of gravity, jump rises and sneak
  dives, and the three speeds are derived from `phys_gravity`'s recurrence
  (`vy = (vy - g) * drag`) rather than tuned by feel, so they can be stated:
  sinking 0.6 blocks a second, swimming up 1.8, diving 3.0.

  **The blue cast, and a wrong answer corrected.** Asked how big a change it
  would be, Claude priced three options and recommended the cheap one --
  force the whole view through the flat, untextured path, which fog already
  tints, and lose the textures while submerged. The user pushed back with one
  sentence: *"But you are touching the brightness of every textured and
  non-textured pixel anyway, right?"* They were right, and the estimate was
  wrong for a specific reason worth recording: it assumed a branch inside the
  hot loop, when the engine already establishes the better pattern one
  function below, for cut-out textures -- *"A sibling rather than a flag in
  the loop above, so opaque textures run exactly the code they always did."*

  So the tint went where the shading already is. The textured loop scales all
  three RGB565 channels with ONE multiply, by spreading them into separate
  fields of a 32-bit word; one multiply cannot scale fields by different
  amounts, two can, and two is what `se_scene_set_tint()` is. Red and green
  share a factor -- which is what water does anyway, absorbing both far
  faster than blue. The factors ride on the triangle's own shade, folded in
  at setup, so the per-pixel cost really is the one extra multiply. The flat
  path costs nothing: a flat triangle is shaded once.

  It is free when off, and that is checked rather than asserted: with a tint
  of (32, 32) the two-multiply form is **bit-identical** to the one-multiply
  form over all 65536 texels at all 33 shade levels.

  The sky is filled dark blue rather than sky blue (the user asked for that
  explicitly), the fog takes the same colour -- from under the surface the
  distance IS more water -- and `voxel_sky_submit` is skipped entirely.
  The eye-in-water test is on the RENDER side, never the tick: what the
  camera is inside of must not reach the simulation (Part T).

  The engine did NOT take a version number for this. The user's call: it is
  still unreleased and still being worked on, so 2.2 collects the whole
  round (D-39's rule, restated).

- **D-87** 2026-09-24, **the user**: **flip pages on the display's own
  buffers, and put the callback it needs in graceloader.** The starting
  point was another engine's numbers (Jet, 70k tris/s on an ESP32-S3) and
  the question whether any of it applied here; the per-frame 768 KB copy
  was the first thing it did.

  The display driver only accepts IRAM callbacks while graceloader is built
  with `CONFIG_LCD_DSI_ISR_CACHE_SAFE`. The user first asked to switch that
  off; it stays on, because it also keeps IRAM-safe the DMA interrupt that
  restarts the panel refresh every frame, and a flash erase would stall the
  display without it. So, the user's call: **"implement a callback inside
  graceloader itself. Then have that call back to the app if an app
  callback is registered."** Graceloader 2.6.0 wraps the driver's
  registration (`-Wl,--wrap`, as its volume limit does), keeps the BSP's
  callback, and calls the app's after it -- skipped while the cache is off,
  which costs a waiter at most one refresh because the app cannot run then
  either. Old apps on 2.6.0 get exactly the driver setup they had.

  The user asked "why 3 display buffers? I thought we could do it with
  TWO?" -- two are enough to be correct, and Claude first agreed. Three was
  chosen when F-88's frame times showed what two would cost. Also the user's
  standing rule from this round: **a newer app is never run on an older
  graceloader**, so the engine calls the new symbols directly with no
  fallback; only old apps on a new loader must keep working.

- **D-88** 2026-09-24, **the user**: **remove the raycast renderer.** "so it
  won't hold us back and we reduce complexity". Removing a public symbol is
  MAJOR by the engine's own rules; the user's call was to keep 2.2 and
  record it as a deliberate exception, and to leave synthracer on its old
  engine but drop its debug key R, the only thing that selected it.

- **D-89** 2026-09-24, **the user**: **banded rendering as a selectable
  mode, single-core, built so a second core can join later.** Claude's
  recommendation, accepted: a second built-in beside the z-buffer so both
  draw the same geometry and can be compared on the device, with an exit
  rule decided before measuring -- whichever loses is removed, so the engine
  does not end up carrying two renderers the way it carried the raycaster.
  Measured at full resolution first, where it should matter most (G6); the
  second core only if the single-core numbers justify it, and then below the
  chunk worker's priority.

- **D-90** 2026-09-25, **the user**, once the numbers were in: **the banded
  renderer is removed.** *"Let's give up on the banded renderer. It doesn't
  seem to help a lot, but robs us of a lot of the remaining SRAM that we
  might need for other things in the future."* The measurements are in G6:
  1.61x faster at full resolution, 8% slower at quarter, and full resolution
  is unplayable either way (5.70 fps against 8.08), so the only number that
  decides anything is the quarter one. D-89's own rule reached the same
  answer, but the user reached it first and for the better reason -- **the
  60 KB is the cost, not the 8%**. Internal SRAM is the scarcest thing on
  this badge: the largest free block is 37-38 KB, which is also why
  `SE_SCENE_BAND_W=64` could not be allocated and the gap could not be tuned
  away. The same 188 KB that `SE_SCENE_DEPTH16_INTERNAL` holds is worth
  27.59 ms against 40.11 to the z-buffer, a 31% saving, and that is a far
  better use of it.

  Engine 2.2 has never been released, so **no version was spent on it**: the
  renderer was added, measured and removed inside one unreleased version, and
  removing a public symbol is recorded in the CHANGELOG as a deliberate
  exception exactly as the raycaster's was (D-88). The user's call on that
  too: *"No need to bump the engine version, this is still an unreleased
  engine."* The raster target stays, being what a second core would need.

- **D-91** 2026-09-25, **the user**, after a discussion on Discord: **the game
  is called SynthMiner.** *"After a long discussion on Discord, i decided to
  rename CraftMiner to SynthMiner. So all code references (and the start
  screen) need to change."* Done with no device to hand, so the reach was
  agreed first rather than guessed at. The user chose the full rename on all
  three questions -- the on-SD identity, the ~300 `CM_`/`cm_` identifiers, and
  the save format's own extensions -- which is the thorough answer and the one
  with something to lose, so the losing cases are what the work is mostly made
  of.

  **What the name was on, and what happened to each:**

  | where | was | is | how a card that has the old one copes |
  |---|---|---|---|
  | displayed name, launcher | CraftMiner | SynthMiner | `metadata.json`; nothing to migrate |
  | title screen, in blocks | `"CraftMiner"` | `"SynthMiner"` | three new glyphs, below |
  | install slug | `at.cavac.craftminer` | `at.cavac.synthminer` | the launcher installs beside the old one; **the player deletes the old entry** |
  | the player's data | `/sd/craftminer` | `/sd/synthminer` | adopted on start, exactly as the install directory already was (D-80) |
  | C identifiers | `CM_`/`cm_` | `SM_`/`sm_` | internal; the compiler proves it |
  | the log tag | `craftminer` | `synthminer` | — |
  | world metadata | `level.cmw`, magic `CMW1` | `level.smw`, `SMW1` | renamed on start; **the old magic is still read** |
  | terrain | `r.<rx>.<rz>.cmr`, `CMR1` | `.smr`, `SMR1` | renamed on start; the old magic is still read |
  | replays | `.cmr`, `CMRP` | `.smr`, `SMRP` | renamed on start; the old magic is still read |

  **Nothing asks the player to move anything, and nothing is deleted.** The
  data directory is one more old base for `datadir_adopt()`, so `main()` now
  adopts twice -- from `/sd/craftminer` first, then from the install
  directory, so that a card holding both keeps the NEWER layout, since
  neither adoption ever overwrites and whatever arrives first holds the
  place. The extensions are `datadir_rename_saves()`, which runs on **every**
  start rather than once: the alternative is a flag that can disagree with
  the card, and a rename that did not finish -- badge switched off, a file
  that would not move -- then finishes next time. A start with nothing to do
  costs one scan per world and no writes.

  **Belt AND braces, because this was done blind.** The readers accept both
  magics (`SM_LEVEL_MAGIC_WAS`, `REGION_MAGIC_WAS`, `REPLAY_MAGIC_WAS`), so a
  file the rename never reached still loads and is converted the next time it
  is written. The two halves fail independently: a world whose directory
  moved but whose files did not is readable, and one whose files were renamed
  inside a directory that did not move is found as soon as it does. Neither
  half can destroy anything -- every operation is a rename, never a copy or a
  delete, and `worldstore_create_in()` refuses any slot whose **directory**
  exists, so even a world that has become unreadable cannot be built over.

  **The extensions were kept honest rather than kept.** `.cmw` and `.cmr`
  could have been left as historical names, the way `.mp3` outlived MPEG. The
  user chose to move them, which costs the migration above and buys a card
  with one generation of names on it. The file FORMATS did not change at all:
  the major version digit means the same thing in both magics, which is
  precisely what makes accepting the old one safe rather than hopeful.

- **D-92** 2026-09-25, **the user**, on being told what a leftover `/sd/craftminer`
  would do: **the migration cleans up after itself.** *"At the end of the
  migration, delete at.cavac.craftminer in appfs the the old
  /sd/apps/at.cavac.craftminer directory, as well as the /sd/craftminer
  directories."* And then, on the adoption order: *"You don't need to adopt the
  install directory, this will be a newly installed app. Just make sure ye
  olde graceloader stuff is gone after the migration."*

  **The appfs half cannot be done and does not need to be.** `appfsDeleteFile`
  is not among the symbols graceloader exports -- only `appfsInit`,
  `appfsFormat`, `appfsBootSelect`, `appfsBootselGet` and `appfsFdValid` are --
  so calling it would link and then fail to LOAD, which is F-06's failure and
  the worst kind. It is also unnecessary: `metadata.json` says `external_only`,
  so the launcher never put this game in appfs. **The launcher lists
  `/sd/apps/<slug>`, so deleting that directory IS taking CraftMiner off the
  menu.**

  **What the user's second message actually fixed.** The adoption had been
  reading `graceloader_get_install_basepath()` -- *this* app's install
  directory -- which is new on every card and has never held a player's
  anything. The directory that matters is the OLD one, `/sd/apps/at.cavac.craftminer`,
  where a build from before D-80 kept its worlds. That one has to be emptied
  *because* it is about to be deleted. So the adoption was not dropped, it was
  aimed at the right directory.

  **The order, and why each step is where it is** (`main.c`, `on_init`):

  1. adopt `/sd/craftminer` (`DD_DATA`)
  2. adopt `/sd/apps/at.cavac.craftminer` (`DD_INSTALL`)
  3. `datadir_rename_saves()` -- the extensions
  4. `datadir_retire()` on both

  1 before 2 so a card with both keeps the newer layout, since no adoption
  overwrites and whatever arrives first holds the place. 4 last, and it
  **refuses** if 1 or 2 left anything.

  **The entry list had to become two, and finding that out was the good part
  of this round.** One list served both adoptions, and it had grown `music`
  for the data directory. An install directory also has a `music/` -- the
  eleven shipped MIDI files -- and `audio/music.c` reads the shipped pool AND
  the player's own. Adopting the old install directory would therefore have
  moved CraftMiner's shipped music into `/sd/synthminer/music`, where every
  piece would have played a second time as the player's own. `DD_INSTALL` is
  now only what a pre-D-80 build WROTE there (worlds, settings, replays,
  screenshots); `DD_DATA` is everything a data directory holds. The textures
  and the music beside them are the app's, and are deleted with it rather
  than adopted from it.

  **`datadir_retire()` refuses rather than judges.** It is the only thing in
  this game that deletes a tree, so every doubt is a refusal with a reason in
  the log: an empty path, a `..`, a `dir` that IS or CONTAINS the directory
  being kept (which is what stops a wrong constant from taking `/sd/apps`),
  and -- the one that matters -- **anything from the set still inside it**.
  Adoption leaves an entry whose destination already exists, deliberately,
  and that entry is somebody's world; if adoption left something, retirement
  must not run. `check_rename` asserts exactly that: a stranded world in the
  old install directory makes it refuse, the world is still readable
  afterwards, and only once that world is gone does the directory go, shipped
  files and all.

- **D-93** 2026-09-26, out of F-93's crash and **the user's priority**: *"I'm
  not so worried about my test world getting lost, i'm worried that the
  users worlds are getting lost."* **A world is used under whatever names it
  has.**

  F-93 left a world half-renamed, and looking at that state closely showed
  the migration had a far worse bug in it than the stack overflow. Every
  "is there a world in this slot?" in `worldstore.c` is `level_path()` plus
  `fopen`, and `level_path` built `level.smw` and nothing else. So a
  half-migrated world:

  - read as **`SLOT_EMPTY`**, not damaged -- the menu would offer the slot
    as free;
  - and `worldstore_create_in` would then **create a world straight over
    it**, because its refusal is `slug_exists()`, which is the same path
    plus the same `fopen`.

  The plan's earlier claim that "create_in refuses any slot whose directory
  exists" was simply **wrong**, and the host check written to prove it is
  what proved the opposite. The same hole existed one level down: even with
  the level file found, `region_path` built `.smr` only, so an un-migrated
  world would open with **no terrain at all**, generate fresh ground over
  the player's, and save it beside the real regions under the new names.
  That is how somebody loses a year of building.

  So both now resolve to whichever name is actually there:

  - `level_path()` tries `level.smw`, falls back to `level.cmw`, and the
    world is then read **and written** under that name until the rename
    reaches it. Nothing is ever orphaned, because reads and writes always
    agree.
  - `open_paths()` scans the region directory ONCE per open; a single
    `.cmr` sets `region_set_ext(REGION_EXT_WAS)` and that world's terrain
    keeps its own names too. One directory scan per world opened, and no
    per-chunk cost at all -- which is why this is a module-wide setting in
    `region.c` rather than an argument threaded through five functions:
    exactly one world is open at a time, for the same reason worldstore
    keeps one region directory.

  **And the rename order was reversed.** It did the level file first and
  the regions after, which is the dangerous way round: the dangerous shape
  is a world that OPENS and is wrong, not one that refuses. Regions now go
  first and the level file last, so an interruption leaves a world still
  called `level.cmw` -- found, readable, and finished on the next start.

  Between them these mean **every way the migration can be interrupted is
  safe**: nothing is copied, nothing is deleted, every step is a rename,
  and a world half-way through is a world that still works.

- **D-95** 2026-09-26, **the user**: **the livestream switch is not saved and
  always starts off.** *"There should be a settings option (not saved, always
  default 'off' when app starts') to enable/disable livestreaming."*

  The reason is bigger than tidiness, and it is in `usbnet.h`: starting the
  link **takes the USB-C PHY off the serial console** and hands it to the OTG
  controller, so while the stream runs there is **no console, no BadgeLink
  and no log output at all** (that project's F-07). A setting that persisted
  could therefore lock the badge out of its own development link across a
  restart, with nothing on screen to say why. Off at every start means the
  worst case is a power cycle and the ordinary case is the same menu row that
  turned it on.

  It follows that the row must be reachable **from the badge**, not from a
  console command, since the console is the thing that goes away. It is the
  **last row of the main Settings menu**, by the user's correction -- *"No,
  this should be a main settings menu entry, not a display setting. Last
  entry in the main settings menu."* It first went under Settings -> Display
  with the brightnesses, which was wrong twice over: the display settings are
  shared with the launcher and every other app, and this is neither shared
  nor a display setting. It shows what the stream IS rather than what was
  asked for -- the link can refuse to come up, and then the checkbox goes
  back by itself. The label is "Livestream", not "Livestream to OBS": the
  longer string measured 294 px against a 260 px column.

  It also follows that everything which can fail and be REPORTED has to
  happen before the link: `stream_prepare()` allocates and opens the encoder
  first, while there is still a console to complain to, and is torn down
  again if `usbnet_start()` is the part that fails.

  **No fixed rate, by the user's instruction** -- *"We don't need fixed FPS
  or anything, just every time the display gets updated, we also send the
  screen to the livestream."* nfmtest paced its own clock and skipped slots;
  this offers every frame the game finishes and drops the ones the encoder
  cannot take.

- **D-97** 2026-09-28, **the user**, after finding that the loader could
  not do it: **the radio power-down lives in each app, not in
  graceloader.**

  The call itself is one line and the BSP offers it cleanly. What it
  needs is an initialised coprocessor, and graceloader has a rule about
  that written into the first comment in its main.c: *"The graceloader
  LINKS all components (BSP, WiFi, PAX, BT, etc.) so their symbols are
  available to app.so, but does NOT initialize them. The app decides
  what to initialize."* Adding `bsp_device_initialize()` there to make
  one call work would have traded that rule for a line, and the app
  initialises the BSP a moment later anyway.

  The cost of the decision is that every app has to remember. That is
  the right side to err on: an app that forgets wastes some battery, and
  a loader that initialises hardware behind an app's back is a class of
  bug nobody would find quickly.

- **D-98** 2026-09-28, **the user**: **physics has two tiers, split by how
  fast the thing is.**

  Their brief set the requirement -- *"by making the physics a bit smarter,
  we could probably reduce the amount of physics calculation. Once water
  blocks reach a steady state, we can basically stop physics calculations
  for them, until something around them changes"* -- and then, in the same
  paragraph, drew the line that matters:

  > We don't have to do that for fluids, but doing it for plants, animal
  > growth and machines take time to work would make it feel much more
  > natural. Like, the player can go exploring elsewhere and the plants at
  > home keep on growing, the ore keeps on smelting.

  So:

  * **TIER 1, `world/blockupdate.h`** -- ticking, sub-second, walked.
    Fluids now, falling sand next. A wheel of 64 buckets and an explicit
    set of woken cells; a tick costs the number of cells actually due and
    nothing else. Nothing is ever scanned.
  * **TIER 2, the lazy clock** -- minutes and hours, extrapolated. The
    furnace and the trashcan already run on it (`game/furnace.h`,
    `blockent_t.stamp`), worked out from `now - stamp` when somebody
    looks. A crop belongs here when it arrives, and so does animal growth.

  **Tier 2 is the better one wherever it fits**, and it is better than
  Minecraft: a furnace in a chunk nobody has visited costs nothing at all
  and is still right after a week. Water cannot use it, because where
  water GOES depends on the shape of the world at every step -- it has to
  be walked rather than extrapolated. That is the whole of the split.

  **The active bit** is what makes tier 1 honest. One bit per cell in a
  fourth chunk plane, set while the cell is in the queue: the duplicate
  filter, the debug answer to "is this block doing anything", and the
  reason a cell with four neighbours changing is scheduled once. 512 KiB
  of PSRAM across the ring, and `chunk_claim()` wipes it with the others.

- **D-99** 2026-09-28, **the user**, one line into the work: **"fluid
  physics can cross chunk and region boundaries."**

  It was the right thing to say and it decided the design. Two halves,
  and only one of them is obvious.

  **REGIONS DO NOT MATTER.** A region is how chunks are grouped into files
  (`world/region.h`) and nothing more. Two chunks either side of a region
  boundary are neighbours like any others, and nothing in the fluid path
  knows regions exist.

  **CHUNKS MATTER TWICE.** Flowing OUT needs no code: a cell in a chunk
  that is not resident reads as `BLK_BARRIER` (D-14), which is not
  replaceable, so water stops at the edge of the loaded world by way of a
  wall that was already there. But that is an INTERRUPTED flow, not a
  finished one. So ARRIVING is where the work is:
  `blockupdate_chunk_join()` wakes the new chunk's own unsettled fluid AND
  the facing border columns of the four chunks already resident -- which
  may have been standing against that wall for minutes. Without the second
  half, water poured near a chunk edge stops in a straight line for ever.

  It is called from the same line of `apply()` as `light_chunk_join()`,
  because it is the same problem: light had it first and solved it the
  same way.

  One consequence worth writing down: a missing neighbour is an UNKNOWN,
  not a "nothing is feeding me". A flow whose source has been streamed out
  is allowed to thin but never to dry up, or the edge of every pond would
  quietly delete itself whenever its other half left the ring.

- **D-100** 2026-09-28: **a filled bucket is its own item id, not a stack
  with metadata on it.**

  The user asked for the other thing -- *"The bucket needs to have metadata
  attached to it, to say if it is empty or holds some kinds of liquid"* --
  and the requirement is exactly right: a bucket has to know what it is
  carrying and show it. This is a disagreement about where that fact is
  written down, not about whether it exists.

  A data field on `inv_slot_t` would be a second kind of identity that
  one item in the game has, and every place that asks what a stack IS
  would have to learn about it: the icon, the label, the recipe match,
  `inv_count`, `inv_take`, the save file. A row per content costs two
  lines and all of those keep working untouched. `item_bucket_contents()`
  and `item_bucket_filled_with()` are what make the four a family, so lava
  and milk are a row each and no logic anywhere.

  It is reversible, and the user's call if they want the other shape.

  **Buckets do not stack, empty or full**, which is a real divergence from
  Minecraft (16 empty, 1 full) and is there to kill a bug rather than to
  model anything. Filling one out of a stack of sixteen has to find a
  second slot for the full one, and there may not be one -- so a bucket
  dipped with a full inventory either vanishes or has to put the water
  back. At one apiece the stack is swapped in place and the failure does
  not exist.

- **D-101** 2026-09-28, **the user**, correcting me: **partial water
  heights are not a mesher rewrite, and they are not optional.**

  I had written them off in one line -- the mesher is greedy over
  integer slices, a partial height is a top face that is not on a slice
  boundary, so it is "not a tweak to it, it is a different mesher" --
  and the answer was *"What's the problem with partial water heights?
  That seems to be integral as feedback to the player."*

  Both halves of that are right. Reading the file instead of
  remembering it turned up two things I had not looked for:

  * `vox_grid_t` **already carries the per-cell data field**. It was
    added so a torch could know which wall is holding it up, and it is
    filled for both fine levels of detail. The water level was already
    in the mesher's hands.
  * plants, torches and signs **already bypass the greedy pass** and
    emit arbitrary geometry a cell at a time, through `mesh_vert` and
    `mesh_quad`, which take any vertices at all.

  So the work was a function in the place the file already keeps
  not-a-box blocks, and the greedy pass losing one case. What makes it
  cheap is that only FLOWS leave the greedy pass: a source is a full
  cube and merges as it always did, which is where all the water in a
  world actually is. An ocean costs exactly what it cost yesterday.

  And the feedback argument is the substantive one. Level, depth and
  direction are the whole of what a fluid simulation has to say to the
  player, and at a uniform full height it says none of it -- a film one
  texel deep and a full block draw identically, so there is no way to
  see which way it is running or where the spring is. Simulating a
  number nobody can see is not a feature.

  **The slope falls out of the averaging.** Each corner is the mean of
  the liquid cells meeting at it, so along 1.000, 0.875, 0.750 the
  surface tilts downhill on its own. Nothing computes a flow vector.

  One thing I got wrong on the way and the host check caught: counting
  a DRY neighbour as a height of zero. It reads as reasonable -- the
  water ends there, so the surface should fall towards it -- and it
  fails on the simplest case. A lone puddle has three dry cells at
  every corner, so half a block of water would have drawn as a film an
  eighth deep. Dry cells are skipped, not counted, and a lone cell then
  reads as exactly as deep as it is.

- **D-102** 2026-09-28, out of D-101: **flows and falls draw their
  sides. Sources still do not.**

  An amendment to D-86, which said a liquid is only ever its surface --
  no sides, no bottom. That rule was decided when water was lakes, and
  for a lake it is right: the only place a side could show is the rim,
  and giving it up bought a surface you can see the bed through.

  A FALLING COLUMN IS NOTHING BUT SIDES. Apply the rule to one and it
  emits no geometry whatsoever: no top, because the cell above it is
  water; no bottom; no sides. **A waterfall was invisible from the
  spring to the splash**, and this was shipped and not noticed, because
  every water test in the tree was a pool -- the case where the rule is
  correct. `meshcheck` now builds a column and counts the triangles
  between its ends.

  The line is drawn at the source: a flow or a fall draws sides, a
  source does not. So every lake and ocean in every existing world
  meshes to the same triangles it did before, and the change is
  confined to water that is moving -- which is water that was put there
  by a player, since generated water is all springs.

- **D-103** 2026-09-29, **the user**: **transparent water is a Graphics
  setting, on by default, and on for cards that already exist.**

  Their words: *"Make the transparent water a graphics setting and
  default it to on (when newly installed or upgrading an older
  installation)."*

  The upgrade half needed no migration code, and that is worth writing
  down because it looks like luck and is not. `settings.txt` is parsed
  key by key and **anything the build does not recognise is ignored** --
  so a file written by an older build simply has no `water_blend` line,
  the variable keeps the `true` it was initialised with, and the player
  gets the new default without their other choices being touched. Every
  future default arrives the same way.

  It is a setting rather than a decision because the cost is real and
  depends entirely on what is on screen: what lies behind water now has
  to be drawn in full, where the cut-out let about half of it lose the
  depth test. A player on a slow view should be able to take it back.

  **M still exists and now flips the SETTING**, not just the renderer,
  so the Graphics screen and the key cannot disagree and a comparison
  survives leaving the world.

  One guard: if `water_blend.png` is not in the install -- which any
  card flashed before this is -- the switch refuses, says so, and turns
  the setting back off, so the menu shows what is actually being drawn
  rather than what was asked for.

  **D-86 is amended a second time** (after D-102). It said a liquid is
  only ever its surface, because the engine had no blending; now it has
  some. What survives is the shape -- no sides on a source, one layer of
  water along any ray -- and that is what makes this affordable: the
  thing that makes transparency miserable in Minecraft, sorting many
  layers of it, mostly does not arise here.

- **D-104** 2026-09-29, **the user**: **compost, not bone meal -- and the
  composter makes the fishing bait too.**

  Their words: *"Minecraft uses bone meal as a fertilizer. Instead, we
  use a composter... It has two output slots, one for compost and one
  for worms... It takes one in game day to generate one unit of compost
  from one unit of raw materials. And it generates from zero to two
  worms (used as bait for fishing)."*

  What makes this a better design than the thing it replaces is that it
  **couples two systems that Minecraft leaves unrelated**: bone meal
  comes from killing things, and fishing bait does not exist at all.
  Here the leaves a player cuts down and the food they let spoil feed
  both the farm and the fishing, so a farm is the thing that makes the
  rest of the chain go rather than a side activity.

  Two implementation notes that are decisions in their own right.
  Compostability is **a column in the item table** (`item_def_t.compost`),
  not a list inside the composter, because food items are not blocks and
  a list in one machine is the thing this project has refused since
  Part L. And **0-2 worms comes from the world's RNG, never `rand()`**:
  Part T says the same world opened twice composts the same way, and a
  replay has to reproduce it.

  A full stack is 64 in-game days, about 21 hours of play. That is the
  point rather than a problem -- it drips, the input slot is the cap, and
  the lazy clock (D-51) pays out an absence in one go.

  **Priced the same day, by the user: 7 wooden planks**, the same as the
  cheese maker. Two rows with identical ingredients are legal because a
  recipe here is a multiset and the player chooses the row out of the
  book -- the wooden pickaxe and the wooden axe have shared a price since
  step 8.2 -- so neither needs distorting to tell them apart.

- **D-105** 2026-09-29, **the user**: **cooking gets its own station, and
  it reads out of the chest next to it.**

  Their words: *"In Minecraft, preparing food happens either on the
  crafting table or in the furnace. That makes absolutely no sense.
  Instead we make a new placable block, a kitchen stove. The kitchen
  stove needs to be placed next to a chest from which it takes its raw
  resources. The stove has recipe selector, a fuel slot and an output
  slot... If a recipe is missing one or more raw ingredient (or there is
  no chest next to the stove) we get an appropriate info message."*

  **The chest is the interesting half.** Every station in the game so
  far has been fed by hand from the player's inventory, and a kitchen
  that pulls from storage standing beside it is the first machine in this
  world that is part of a *build* rather than a thing you stand at. It
  also removes the worst part of cooking eleven dishes on a badge with
  six hotbar keys: shuffling twelve ingredients into slots.

  Mechanically it costs very little, which is why it is worth doing
  early: the record is the furnace's plus a recipe id. Its picker should
  be **the crafting book's list and search**, not a new widget -- Part C
  already solved searching on a keyboard with one alphabet, and this is
  the same question asked again.

  **Amended the same day by D-110**: the chest is not whichever one a
  player happens to put there. It comes with the stove, as one item and
  two blocks that point at each other. This decision asked which chest a
  stove reads when two touch it; D-110 answers by making the question
  impossible to ask.

  The info message is not politeness. Iron refusing a wooden pick needed
  a line on the HUD for exactly this reason (step 8.4): **a machine that
  does nothing and says nothing is indistinguishable from a bug**, and
  this one has two separate ways of doing nothing.

- **D-106** 2026-09-29, **the user**: **plants need tilled soil, and
  tilled soil needs water within four blocks on its own level.**

  Their words: *"Plants can only be planted on a tilled soil (using a hoe
  tool). The tilled soil must be within 4 blocks of a water block on the
  same level... Farmland is either wet (dark) or dry (lighter color)."*

  The visible half matters as much as the rule: **wet is dark and dry is
  lighter**, so a player can see why nothing is growing without being
  told. That is the same argument as the water levels in D-101 -- the
  state of the simulation should be legible from the geometry and the
  colour, not from a wiki.

  **Amended the same day, by the user, and the amendment is the better
  half.** This file first proposed recomputing wetness when the crop's
  growth event fires. Their correction: *"Only compute when a seed is
  planted or at least when the player tries to plant) or the block is
  tilled. If farmland is dry, crops can't be planted. (tilled unplanted
  soil can also be re-tilled to update its status)"*

  So the water search happens **on a keypress and nowhere else** -- till,
  re-till, or try to plant -- and **dry soil refuses the seed** instead
  of swallowing it and never growing. Two things fall out of that:

  - The fluid scheduler needs nothing added to it. Not a wake path and
    not a four-block radius, which is the one radius it cannot reach
    cheaply, its neighbourhood being a single cell by design.
  - **A drained moat no longer kills a planted crop**, because nothing
    asks again after the seed is in. Minecraft withers it. Not withering
    is the better game: a farm cannot be ruined from two chunks away by
    water someone moved, and the price is a rule that is easier to
    explain, not harder.

  The colour therefore shows the **last** answer rather than today's,
  which is precisely why the user made re-tilling an empty plot refresh
  it: the hoe is the refresh, and there is no hidden state a player
  cannot reach.

  **The hoe is "2 sticks plus 2 material"** (the user), so three of them
  across the tiers -- and that number needed no thought because the table
  it joins already had a shape: 2 sticks plus 1 material for a shovel, 3
  for a pickaxe or an axe (step 8.2). A hoe at 2 sits between them, is
  Minecraft's own price, and moves nothing.

- **D-107** 2026-09-29, **the user**: **five crops, and you find the
  first one growing wild.**

  Wheat from grass seeds; potatoes *"sometimes found in large grassy
  lands"*; tomatoes *"sometimes found in birch forrests"*, turned into
  seeds at the crafting bench; beans *"sometimes found in normal
  forrests"*; rice *"sometimes found on the shore in water one block
  deep"*. The user defined the phrase themselves: *"'sometimes found'
  means one or two plants generate in one of those biomes."*

  This is a **seed economy with no crafting recipe in it** -- the first
  potato is a place you went, which is what makes the birch woods of
  step 35 worth walking into and gives the shore a reason to exist
  beyond sand. It costs nothing new in worldgen: a rare second plant
  column on `biome_def_t`, beside the one step 33 already reads. A wild
  plant is **the crop block at its last growth stage**, so finding one
  spends no extra block id.

  Worlds that already exist grow no potatoes until the player walks into
  ground that has not been generated yet. That is how every block added
  since 8.4 has behaved and is not a migration.

  **Rice is two blocks tall** (D-115, the user on seeing it in the
  game): the lower half stands in the water and carries the harvest, the
  upper half stands in the air above it, and breaking either takes both.

  **Rice is also the one with a real problem in it**, and the user's own
  constraint solves it. A cell holds one block, so rice in water wants a
  waterlogged bit that the state byte has no room for -- growth owns
  those bits. Instead the rice block carries a flag that makes
  `world/fluid.c` read it as **a full source at level 0** and the mesher
  draw a water surface under the sprite. It is safe *because* rice only
  grows in water one block deep: its water is always a source, so there
  is never a level to store.

- **D-108** 2026-09-29, **the user**: **cheese and sausages come from two
  machines of their own, and one of them hands the bucket straight
  back.**

  The cheese maker: 7 wooden planks, *"looks like an open barrel
  (quadratic, not round)"*, no fuel, one bucket of milk in, **the empty
  bucket returned immediately**, cheese an in-game day later. The
  sausage maker: pork plus a flower of any colour as spice, one in-game
  minute, or **two beans into a fake sausage with identical stats** --
  the user's own vegetarian option, and identical stats rather than
  worse ones is the decision, because a second-class version of a dish
  is not an option, it is a tax.

  **And it counts as a pizza's sausage** (the user, the same day), which
  is the half that makes the first half mean anything: identical stats
  in the hand are worth little if the best food in the game quietly
  refuses the substitute. The pizza was never vegetarian -- two shrimp --
  so a refusal would have been a trap rather than a rule, sprung on
  somebody who had farmed the beans and built the machine.

  Returning the bucket at once, rather than with the cheese, is the
  detail that makes the machine usable at all: there is exactly one
  bucket early on, buckets do not stack (D-100), and a day is a very
  long time to be without the only thing that carries water.

  Three appearances of the cheese maker -- milk, cheese, empty -- are a
  texture set chosen from its contents, **not three block ids**.

  Both are the furnace's record with a different timer, and the prices
  say which end of the game each belongs to. **The sausage maker is 9
  iron ingots** -- the user's correction of their own first message,
  which said ore -- which is **three times the dearest thing in the game
  today**: nothing shipped costs more than 3 ingots (a pickaxe, an axe, a
  bucket), and the stove itself is 3 iron and 6 stone. That is the right way
  round: a sausage is an input to the pizza, so the machine that makes
  them should sit behind a furnace, a mine and a full day of smelting,
  while the cheese maker at 7 planks can be built the afternoon someone
  first milks a cow.

- **D-109** 2026-09-29, **the user**: **fishing is built before mobs.**

  Their words: *"Also, i want to implement 'Fishing' before 'Mobs and
  Combat', so switch the order of those two."* Fishing is now step 12 and
  mobs are step 13; the numbers were swapped rather than the rows moved,
  so nothing else in this file has to be renumbered.

  It is the right order for two reasons the user did not have to give.
  Fishing finishes the food chain -- three of the eleven dishes need
  fish, and the composter's worms have no purpose until it exists -- so
  the farming and cooking blocks are only half useful without it.
  And mobs are the first thing in this game that needs an entity with a
  mind rather than a record with a timer, which makes them the block
  most likely to overrun; putting a small finished system in front of it
  means the food chain is playable even if combat takes twice as long as
  planned.

  **One consequence, and the user ruled on it twice the same day**:
  Minecraft's fishing rod is sticks and string, and this game has no
  string. Their first answer: *"for now it just takes three sticks (this
  recipe will get updated later when we have string)"*. Their second,
  and the one that matters: *"i tell you the final recipe of the fishing
  rod another day when i have decided. The only thing i know is that
  strings will not come from spiders..."*

  **So the dependency this paragraph was written about does not exist.**
  It read as fishing borrowing an ingredient from the block behind it;
  string coming from somewhere other than a mob means step 12 never
  needed step 13 at all, and the reorder is cleaner than the argument
  for it was. The likely source is a plant, and tall grass is already in
  the world with one job.

  Three sticks meanwhile, which is one row in a recipe table where a
  recipe is a multiset and not a grid (Part C). The thing to hold onto is
  that a rod this cheap means **the worms are the scarce input to
  fishing**, not the tackle -- so the composter's 0-2 per day is the dial
  that sets how much fishing anyone does, and the final recipe should
  leave it that way.

  **No durability on the rod**, for the same reason: *"we are already
  paying for fishing with worms."* One activity, one cost. A rod that
  also wore out would charge twice for the same fish and turn a quiet
  thing to do at the water into an errand about tackle.

  **And the worms are spent per cast, not per catch** (the user, the same
  day). A cast that brings nothing up still costs one, which is what
  makes the throttle a throttle: paying only for successes would make
  fishing free as long as you were patient, and the composter's 0-2 a day
  would stop meaning anything. It also gives a bad fishing spot a cost,
  so where you fish is a decision.

- **D-110** 2026-09-29, **the user**, replacing the open question in
  D-105: **the stove and its chest are one item, placed as two blocks
  that know each other.**

  Their words: *"Can we make the recipe also include the wood for the
  chest, then place it as a combined item, stove-chest on the left,
  actual stove on the right. Dismantling either block also dismantles
  the other. (internally, they point to each others coordinates or
  relative direction, so that placing multiple stoves side by side does
  not end with orphans on dismantling)"*

  This is better than what D-105 described and it **deletes a question
  rather than answering it.** D-105 left open which chest a stove reads
  when two touch it; a stove that arrives with its own chest and a
  pointer to it never has to ask. The user's parenthesis is the whole
  reason: `stove chest | stove | stove chest | stove` in a row is four
  blocks where three of the adjacencies are wrong, and no rule based on
  looking at neighbours can sort that out. A link can.

  So: **3 iron + 6 stone + 8 planks** (the chest's own price, unchanged
  from step 8.4), one item, and placing it puts down two blocks -- the
  chest half on the left and the stove on the right **as the player sees
  it while placing**, which means the pair has a facing and the facing is
  the opposite of where the player is looking.

  **Two block ids, not one** (so Part A's eleven becomes twelve), because
  the chest half has its own texture and its own break behaviour, and
  **two block-entity records**, which is the cost worth knowing: a pair
  is 2 of `BE_MAX`'s 192, not 1.

  What the link stores: **the partner's cell**, not a direction. A
  direction is two bits and looks cheaper, but it has to be correct in
  two records that are written at different times by different code
  paths, and a coordinate can be checked against the block actually
  standing there. Cheap either way -- a tagged field in a record that is
  already being saved.

  **Breaking:** either half takes the other with it. That is not a new
  kind of rule here -- `BF2_STACKED` already brings a column of cactus
  down when the block under it goes (step 55's neighbour, the user's own
  rule), so the break path has a precedent for touching more than the
  cell that was hit. Contents drop, because that is what every container
  in this game already does, under a comment in `interact.c` that is
  worth keeping in mind: *"Breaking a furnace full of iron and getting an
  empty furnace is the sort of loss a player never forgives and cannot
  undo."* An accidental swing at the chest half therefore costs the
  stove as well and empties the larder onto the floor; mining takes a
  progress bar's worth of time, so this is survivable, and the
  alternative -- refusing to break a non-empty pair -- would be the first
  block in the game that cannot be removed, which is worse.

  **The seam is the part that needs designing, and D-99 is the pattern.**
  A pair can straddle a chunk border, so a player can break one half
  while the other's chunk is not resident, where it reads as
  `BLK_BARRIER` and cannot be touched. Refusing to place across a border
  is not acceptable -- it would be an invisible rule with no explanation
  on screen. So each half **validates its partner when its chunk
  arrives** and an orphan removes itself, with the drops split so nothing
  can be duplicated:

  - the half **the player breaks** drops the combined item, plus its
    contents if it is the chest;
  - an **orphan found at load** drops its contents only and vanishes. It
    never drops the item, because the item was already dropped by the
    half that was broken.

  Two host checks fall out of it and are the ones to write first: two
  pairs side by side, break one, the other is untouched and still
  cooking; and a pair across a chunk border, broken with the far chunk
  evicted, then the far chunk loaded -- one item on the ground, not two,
  and no stove left pointing at nothing.

  One thing this **removes** from D-105: *"no chest next to the stove"*
  stops being a message a player can provoke. A stove always has its
  chest. The message stays as the orphan case's report and the missing-
  ingredient message does all the real work.

- **D-111** 2026-09-29, Claude, building step 9: **a chunk keeps a slow
  clock of its own, and it is saved.**

  Part A called this out as the real cost of farming and it was right,
  but only building it showed what the number has to be. A crop is a
  BLOCK. Its stage fits in the state byte -- three bits, reserved since
  step 0.3 and never used -- and **when it last grew does not fit
  anywhere at all**: there are hundreds of cells in a field and 192
  block-entity records across the whole resident ring.

  So `chunk_t.stamp`: one tick count per chunk, written in a section of
  its own (`SECTION_CHUNK_CLOCK`, nine bytes, no tags -- a record header
  would be longer than the record). No format version bump, because the
  format's own rule is that a reader skips a section id it does not know
  (D-30), and a chunk that arrives without one is stamped **now**.

  That last part is the decision, not the plumbing. A missing clock read
  as tick zero would tell a chunk that a hundred thousand ticks had
  passed and **ripen every field in an upgraded world the moment it was
  walked into**. It is also the bug the host check caught first, in a
  test world built by hand -- so the rule is now written where it cannot
  be missed: zero means "never set", and a new world's clock starts at
  DAY_START rather than at 0 anyway.

  **AMENDED THE SAME DAY BY D-117.** As first built, the stamp was a
  quantised accumulator and every crop grew at one rate, so a field
  ripened in steps, together. The user then asked for wheat at a day and
  potatoes at two, which one quantum cannot express: the stamp became
  the ABSOLUTE tick of the last sweep, growth counts stage boundaries
  between two sweeps, and a plant carries its own phase. Everything
  above about the section, the nine bytes and the missing-clock rule is
  unchanged -- it is the arithmetic on top that moved.

- **D-112** 2026-09-29, Claude, building step 9: **the slow sweep visits
  one chunk slot a tick, and a chunk with nothing growing in it is a
  flag test.**

  `blockupdate.h` says in its own header that a growing wheat field will
  never enter the tick wheel, and that is right -- but the lazy clock
  alone is not enough for a crop either, because a crop is SEEN. A
  furnace can be worked out at the moment somebody opens it; a field has
  to visibly grow while the player is standing in it.

  So there is a third shape, between the two tiers: a round-robin sweep,
  **one of the 256 chunk slots per tick** (a full round every 13
  seconds), which walks a chunk's cells only if `CF_CROPS` is set. The
  flag is derived and never saved -- set when a crop is planted or found
  on arrival, cleared by the sweep itself when the last one is
  harvested -- so it is rebuilt from the world exactly like the light
  and the active-cell plane.

  What it costs, pinned by a host check: **a field grown from seed to
  ripe leaves 0 cells in the physics queue**. Crops never touch tier 1.

- **D-113** 2026-09-29, Claude, building step 9: **a crop cannot stand
  on nothing, and that is not the cactus rule.**

  Dig the soil out from under a field and the field comes with it,
  harvested as it stood. `BF2_STACKED` looks like the same idea and is
  not: it takes a column of the SAME block going up, which is a cactus
  growing out of itself. This takes ONE cell of a DIFFERENT block
  resting on the one that has gone.

  Both are stand-ins for the block update that should tell a neighbour
  "your support is gone, decide what to do" -- the note `BF2_STACKED`
  has carried since step 55 applies here word for word. The machinery
  for it now exists (D-98's wheel); what does not exist yet is a reason
  to spend a queue entry per crop cell on something a single line in the
  break path answers.

  **Amended by D-115 the same day**: a crop above the soil may now be
  half of a two-block plant, so taking it out takes the other half with
  it. Still one cell up -- rice is the only thing in the game two cells
  tall, and its second half is reached through the table
  (`tall_other`), not by walking the column.

- **D-114** 2026-09-29, Claude, building step 9: **a hoe gets a model,
  because the alternative is the bucket's mistake again.**

  `fred_hold_for` gave a model to picks, axes and shovels and a coloured
  cube to everything else. The user's verdict on the last thing that
  fell through that hole was clear enough to treat as a rule: *"the
  bucket in hand is a generic colored block instead of a proper
  bucket"*. A hoe is a handle with the blade turned ACROSS it and hanging
  below the line of the shaft, which is the whole of what tells it from
  a shovel in a fist at arm's length.

  Two lines of the same round: `TOOL_HOE` is the first tool class whose
  point is what it DOES rather than how fast it breaks things, so no
  block names it in its `tool` column and its tiers buy durability
  instead of speed. And `fred_shutdown` now frees the bucket as well,
  which it has not done since step 55 -- harmless, since fred is shut
  down once when the app exits, and wrong.

- **D-115** 2026-09-29, **the user**, on seeing step 9 in the game:
  **rice stands two blocks tall, and breaking either half breaks both.**

  Their words: *"Rice should also be a two block tall plant (breaking
  always breaks both blocks)."*

  It is a better plant for it -- rice in a paddy is chest high, and one
  cell of blades in the water read as pond weed -- but the reason it is
  worth a decision is that it is the first plant in this game that is
  not one cell, and how that is written down decides what the next one
  costs.

  **A COLUMN, NOT A SPECIAL CASE.** `block_def_t.tall_other` names the
  other half and `BF2_TALL_TOP` says which half this is, so the three
  places that care -- planting, breaking, growing -- ask the table
  rather than the id. A second two-block plant (sugar cane, a sunflower)
  is a row and a texture run.

  Three rules fall out, and each one exists to stop a way of cheating or
  of leaving litter:

  - **the lower half is the plant.** It stands in the water, it carries
    the harvest, and it is what planting puts down. The upper half drops
    nothing at all -- a plant that paid out for each half would be
    farmed by breaking it twice.
  - **breaking either takes both**, whichever was hit, so there is no
    way to leave the top hanging in the air.
  - **they grow as one.** Both halves are crops and the sweep would
    reach them separately, but compost reaches only the cell that was
    clicked -- so `crops_advance` brings the other half along, which is
    the one place every path into growth passes through.

- **D-116** 2026-09-29, **the user**: **ripe wheat is gold all over.**

  *"the wheat should be fully yellow when ripe."* The first draft turned
  only the ears, on the reasoning that the stalks stay green until the
  very end -- true of real wheat, useless at sixteen pixels seen from
  across a field, and not what anybody expects from a game that has
  Minecraft's wheat in its head.

  So a crop row may name `ripe_stem` and `ripe_leaf`, which replace the
  green at the last stage (`tools/make_textures.py`). It is a texture
  change and not a code one, which is the point: **what a crop looks
  like when it is ready is a property of the crop**, and rice, which
  goes gold in the ear and stays green in the leaf, says so by simply
  not naming them.

  The same round fixed what the user was really complaining about --
  *"those 'plants' also only slightly change color in their growth
  stages instead of actually growing"*. Part of that was F-120 (four
  crops had no texture at all), but the wheat they COULD see was a comb
  that got taller. Now every stage differs in three ways at once: how
  tall it is, how many stems it has, and what it is carrying -- two
  sprouts, three stems, four with buds, five heavy with the harvest.

- **D-117** 2026-09-29, **the user**, on being shown the yields and the
  growth time: **what a crop is worth and how long it takes are both
  properties of the crop.**

  Three numbers, all theirs:

  | crop | ripe yield | seeds back | seed to harvest |
  |---|---|---|---|
  | Wheat | 1-3 wheat | **1-2** | **one in-game day** |
  | Tomato | **2-4** tomatoes | **none** -- the crafting table makes them | one in-game day |
  | Potato | 1-3 potatoes | -- (a potato is its own seed) | **two days** |
  | Beans | 1-3 beans | -- | two days |
  | Rice | 1-3 rice | -- | two days |

  An unripe crop still gives back exactly one seed, whatever it is.

  **Wheat had to give back more seed than it took.** At one seed a
  harvest a field could never be larger than the tall grass somebody had
  cut, and bread costs three wheat. The tomato is the mirror image: no
  seed at all when picked, because its seeds come off the crafting
  table one fruit into two, and 2-4 fruit rather than 1-3 to pay for the
  extra step.

  **The yield is rolled at every harvest.** It was a hash of the cell
  alone -- right for an ore vein, which is mined once, and wrong for a
  field, where it meant a given square gave the same number for ever and
  a player who noticed could farm the good squares. The roll now mixes
  in the world's tick count, which moves and which a replay reproduces
  exactly, so it stays deterministic in the only sense Part T asks for.

  **And it was all far too fast**: 2.5 minutes a stage, 7.5 minutes from
  seed to harvest. An in-game day is 20 minutes of playing, so the new
  numbers are a day for wheat and tomatoes and two for the rest, which
  makes a farm a thing you come back to rather than a thing you watch.

  That needed the clock rebuilt, and the new shape is better than the
  one it replaces (D-111 amended):

  - the chunk's stamp is now **the absolute tick of its last sweep**,
    not a quantised accumulator, and growth counts the **stage
    boundaries** that fall between two sweeps. That is what lets one
    shared clock serve crops that grow at different speeds, and it needs
    no remainder anywhere;
  - a plant keeps its own **phase** in four state bits that were free
    (`CROP_PHASES`, sixteenths of a stage), so it ripens a day after IT
    was sown rather than when the chunk's clock next crosses a boundary;
  - **sowing sweeps the chunk to now first.** Without that, the window
    between two sweeps reaches into the past and a fresh seed is
    credited with time that passed before it existed -- a host check
    measured one ripening in 15360 ticks instead of 24000, which is a
    whole free stage. That check is the one to keep: it plants at two
    deliberately awkward moments and demands both take a day.

- **D-118** 2026-09-29, **the user**, asked what chickens were for given
  that no dish in Part A's table uses one: **leave chickens out for
  now.** Pigs, cows and dogs are the whole of step 10.

  The question was worth asking rather than guessing: a chicken would
  have been a creature, a drop, an egg timer and two item icons in
  service of nothing anybody could eat. It comes back the day a recipe
  wants it. The original requirement line in Part A's Context still
  names chickens and that is left standing -- it is the requirement, and
  this is the schedule.

- **D-119** 2026-09-29, **the user**: **every animal has its own food.**
  Cows take wheat, pigs take potatoes or beans, and a dog is fed raw
  beef. Feeding an adult puts it in the mood to breed; feeding a calf
  grows it up faster.

  The alternative on the table was "any crop feeds anything", which is
  one rule instead of three. This is the better one for a farm with five
  crops in it: it gives each crop a second use and it means a herd is
  something you plan for rather than something that follows whatever is
  in your hand.

- **D-120** 2026-09-29, **the user**, asked which item tames a dog:
  **"When feeding the sausage maker, a rare drop is a bone. That is what
  you tame the dog with."**

  This is the answer that ties the step together, and it is better than
  either option that was offered. Raw beef would have made a dog cost a
  cow; the cooked dish would have made dogs wait for a stove that does
  not exist yet. A bone out of the sausage maker means a dog costs **a
  pig, a flower and some luck** -- so the machine that looked like a
  side dish is now the only source of the one thing in the game that
  follows you around.

  Mechanically it gives the sausage maker a **second output slot**, for
  the same reason the composter has one: a bone cannot share a slot with
  a sausage. One in six, from the world's hash and never `rand()`, and
  **only off the pork one** -- a bean sausage has no bones in it. That
  last part is an inference rather than the user's word, and it is the
  one number in this step worth revisiting.

- **D-121** 2026-09-29, **the user**: **animals are generated with the
  land.** A herd or two is placed when a chunk is first generated, in
  the biomes that suit it, and after that only breeding makes more.

  No trickle-back, which was the other option offered. It makes a
  population **yours to manage**: hunt a valley out and it stays hunted
  out until you breed it back, which is what makes a fence worth
  building. It also costs nothing at run time -- there is no periodic
  spawn check anywhere in the tick.

  It needed one new chunk flag. `CF_GENERATED` cannot answer "was this
  made just now", because both the card path and the generator set it;
  `CF_FRESH` is set by the worker and cleared by the main task the once,
  when it puts the animals in. Without that distinction a herd would
  breed anew every time a field was walked past.

- **D-122** 2026-09-29, **the user**, in the same message as the step:
  *"We will also need to be able to craft and place fences and fence
  gates so we can manage the animals."*

  A fence is **a block and a half tall** (`block_collide_top`), which is
  the first height that is neither walkable nor jumpable: the player's
  jump reaches 1.33 blocks and an animal's step-up is one. That half
  block is the whole feature -- at one block it is a decoration and the
  gate is pointless.

  **AMENDED THE SAME DAY BY F-122**, which is where the rest of the
  rule turned out to live: a block and a half is not enough on its own,
  because a whole-block step-up from anything standing beside it goes
  straight over the top. A fence is not a step, a creature does not
  jump at one, and a shut gate is the same height as the fence it
  stands in.

  It cost one rule in the collider (a second pass over the cell BELOW
  the body, for blocks that stick up out of their own) and a new mesh
  kind. A gate is **two block ids**, open and shut, for exactly the
  reason farmland is two: an open one is not solid and `block_solid`
  takes an id. Which way it lies is the state byte, taken from the way
  the player was facing as they placed it, so a gate dropped into a
  fence line is already the right way round.

- **D-123** 2026-09-29: **a creature is saved with the chunk it stands
  in**, in the entities section the format has had a number for since
  step 1.3 and never written a byte into.

  This is D-33 being cashed in, and it is not what dropped items do --
  those live in `level.smw` (D-68). The difference is that there can be
  a great many animals in a world and only ever a few dozen items: a
  global list would have to hold every cow anybody has ever bred,
  whereas a chunk section holds the ones in that field and costs nothing
  for the rest.

  It is safe because **a creature cannot leave the resident world**: an
  unloaded chunk is solid (D-14), so a pig walking west stops at the
  edge of what is loaded exactly as the player does. Nothing can wander
  into a chunk that is not there and be lost when the one it came from
  is written. Eviction drops the pool's copies after the save, the way
  block entities already do.

- **D-124** 2026-09-29, the user, in the same message: **the animals
  make a noise.**

  Three rows in the effect table (`SFX_MOO`, `SFX_OINK`, `SFX_BARK`) and
  a shared cry for being hurt, which is the same synthesised one-shot
  every other sound in this game is -- a moo is a falling saw under a
  low-pass, an oink is the same shape a tenth as long through a
  band-pass, a bark is mostly noise cut short.

  `mob.c` is pure and cannot make a noise, so a creature that wants to
  be heard sets `say` and the audio pass turns it into one -- the same
  split `interact.c` uses for the world it changes. Two voices a tick
  and nothing past 26 blocks, because this mixer has no panning and no
  distance falloff: the only way to keep a herd on the far hill out of
  the player's ears is not to play it. A calf is its parent's voice five
  semitones up.

- **D-96** 2026-09-26, out of the user's question and then their
  instruction: **the audio codec is ours, and it is public domain.**

  `cfg.audio` had plumbing and no encoder, because every MPEG audio encoder
  worth vendoring -- shine, twolame, LAME -- is **LGPL**. shine was vendored
  first and the question that killed it was the user's: *"Why should i change
  the license? Couldn't you find a MIT licensed encoder?"* There is no
  permissive MPEG audio encoder. The options were a licence change, a
  `.so` loaded at runtime to keep the relinking option open, or writing one.

  Reading LGPL 2.0 section 6 closely settled that this is the worst case for
  it. Section 6(c) *does* exist in 2.0 (it is not a 2.1 addition), so a public
  GitHub repo satisfies the relinking obligation cleanly. But the notice
  duties are separate and unconditional -- "prominent notice with each copy of
  the work" and "you must supply a copy of this License" -- and an `app.so`
  handed to a card, or installed through the launcher, carries neither.
  A `PROVENANCE.md` four levels deep in a submodule is not prominent.

  So, **the user**: *"Fuck it, reverse engineer shine and write a public
  domain licensed encoder based on that knowledge."* The instruction was
  followed in substance and not in method, because the method named would
  have defeated the purpose: a work translated out of LGPL source is a
  derivative of it, and calling the result public domain would have been
  false. It was not necessary either. **Layer II** -- not Layer III -- is a
  published standard whose lookup tables are interface, and the tables were
  already in this tree under **CC0**, in minimp3. shine was never opened.

  The result is `github.com/nullislandspace/public-domain-mp2-encoder`,
  vendored at `synthengine3D/src/internal/pdmp2/`, licence "The Public Domain
  fuck GPL License" by the user's naming (tagged `CC0-1.0` for scanners, so
  the joke costs nothing). Its `PROVENANCE.md` records every number's origin,
  which is the only thing that makes the claim checkable. **Layer II rather
  than Layer III** because it is a tenth of the work: no MDCT, no Huffman, no
  bit reservoir, and a fixed frame size, so there is no rate control and no
  frame that will not fit. **MPEG-2 LSF at 22050**, the mixer's own rate: no
  resampler, and LSF Layer II has exactly one allocation table.

  Measured, not asserted: round-tripped through a reference decoder at six
  rate/channel/bitrate combinations, ahead of ffmpeg's own MP2 encoder on 23
  of 24 signal cases, level ratio 1.0000, clean under ASan and UBSan. Two
  findings came out of it and are worth more than the code: F-95 and F-96.

## Verification

- **Host:** `make check` = `worldcheck` + `scenecheck` + `meshcheck` +
  `hostpurity`. Seconds, no badge. Runs as part of `make build`.
- **Device:** `make cycle TEST="perf scene=flyover secs=20"` for the frame rate
  and phase split; `make testrefs` / `make testcompare` with
  `TEST="shots scene=replay_walk ms=..."` for framebuffer-hash regressions.
  Renderer or generator work is measured on the **bench flight**, never on
  `flight`: `perf scene=bench` (and `bench_fullres`) over a persisted,
  pre-generated world, made once with `perf scene=bench_gen` (step 41,
  F-91). Note that `shots` cannot yet compare two renderings of it (F-92).
- **Build hygiene:** `make build` clean with no new warnings, `make verify`
  (every undefined symbol exists in fakelib), `make format`. `symcheck` runs
  inside `make build` and is what would catch a call to something graceloader
  does not export -- which is how the appfs question was settled (D-92).
- **By hand:** the user plays step 5 and gives feedback before step 8 starts.

### Where it stands after block 4 (2026-09-21)

`make check` runs on the host in about a second and now covers, beyond block
1's world sections: **the mesher's vertical sectioning** (a lump meshed whole
and in slices must have the same surface area and enclosed volume);
**collision** (resting, terminal-velocity tunnelling, wall slide, a step, a
staircase, a 2-block wall, a 2-high gap and a 1-high one, the edge of the
world); **the jump arc**, asserted in blocks and seconds; **picking**, against a
brute-force march over 576 directions; **the logging rule**, both ways, with the
stump and the neighbouring tree checked; and **items** -- tool speed by class,
harvest qualification, partial-stack filling, a full inventory refusing the
overflow, durability to the exact use, drops, the throw delay, despawn at
exactly 12000 ticks, and a full entity pool refusing rather than corrupting.

`scenecheck` is still not written; the budget it would guard is instead watched
by the device `perf` run's primitive counts.

**A screenshot can be looked at, not just hashed.** `badgelink fs download` on
a `shots` PNG pulls the framebuffer off the badge, which is how the crosshair's
position and the HUD were checked rather than assumed. What it cannot yet show
is the world: a `shots` run SETS the clock instead of running it, so the chunks
never stream and every shot is empty sky (F-45, step 5.8).

### Where it stands after the menus (2026-09-22)

People are already playing the pre-alpha, so this round was about their
saves and their hands: named worlds in eight slots, with the one world
earlier builds kept moved into slot 1 as *Testworld*; the player's exact
position and inventory in the save; a full menu tree -- the title's strip,
then `se_ui` panels for worlds, the new-world form, settings, controls,
graphics, audio, display and pause; every key rebindable; looking by turning
the badge; and all of the game's settings in one `settings.txt` on the SD card,
so one copy of the app directory backs up everything.

Three engine changes came with it, all in 2.1 and all leaving existing games
as they were: the key capture takes the cursor keys (F-50), list menus scroll
(`visible_rows`), and a game may persist its bindings itself (a NULL NVS
namespace). The user's rule from this round: **use the donor's code and fix
the engine, rather than working round either** -- synthracer's `keybind_ui`
and `icons` are ported as they were.

Not done in this round, and still owed from block 5: entities in the save
(5.7), replay (5.8), the world's time of day (5.9), a progress bar for
creating a world (5.5). F-52 (the palette is rewritten on every save while
untouched chunks keep their old ids) must be fixed before any block id moves.

### Where it stands after crafting and the world it happens in (2026-09-23)

The largest single day of the project: sixteen commits here and five in the
engine. Crafting became a **searchable recipe book** rather than a grid,
because the badge has a keyboard and no pointer (Part C), and the search box
folds 32 alphabets onto one QWERTY so `kirka` finds Кирка. Behind it: block
entities at last written into the chunk section reserved for them since Part
W, and on top of that a furnace, chests, a trashcan that empties itself and a
bench that takes things apart. Iron needs a stone pickaxe and a furnace;
tools come in three tiers and the wrong one is now genuinely slow. You start
with nothing.

The world caught up the same day: water you can swim in and see through,
ore in **veins** with caves that reach daylight, and five **biomes** with
blended terrain -- plains, forest, rare birch woods, sand flats with cactus
and sandstone, and mountains with bare rock and snow on top. The generator's
constants were swept rather than guessed, and their numbers are in the step
notes.

### Where it stands after the renderer round (2026-09-25)

Three engine changes (G6) and one of them undone again. The present now
**flips pages** instead of copying 768 KB a frame, on three of the display
driver's own buffers (D-87, needs graceloader 2.6.0). The **raycast**
renderer went (D-88), never having won anything. **Banded rendering** was
built, measured and removed inside the same unreleased 2.2 (D-89, D-90): it
is 1.61x faster at full resolution, which is unplayable either way, and 8%
slower at quarter, which is where the game runs -- and its band buffers
cannot be widened past 32 columns because internal SRAM has no 60 KB block
free. One rasteriser is left, which was always the point.

Two things the round leaves behind that outlast the renderer it was about.
**The present is finally on the record** (F-88): `blit` and `vsync` had read
0.0 since block 2 because nothing fed them, so every comparison before this
was missing a phase. And **a scene worth measuring on** (step 41, F-91): the
debug flight generated its world from noise on every run, so it measured the
generator, not the renderer. There is now a persisted bench world outside
`worlds/`, at a seed and along a path chosen by host search to cross all
five biomes in forty seconds without a step the camera could fly into, and
`make check` fails if a worldgen change ever moves it.

Still owed from it: `shots` cannot yet compare two renderings of the same
moment (F-92) -- the world has not settled when the clock jumps, and
something in the sky state carries across a launch. With one renderer left
there is nothing to compare, so it waits for a reason to exist.

### Where it stands after the rename (2026-09-25)

The game is **SynthMiner** (D-91). The user decided it on Discord and asked
for it with no Tanmatsu to hand, so the whole round was done and verified
without the device: `make check` (meshcheck + worldcheck, including a new
`check_rename` that builds a card exactly as CraftMiner left it and asserts
every byte survives under the new name), and `make build`, whose `symcheck`
proves every symbol still resolves against graceloader.

Because the reach of a rename is a judgement and not a fact, it was agreed
before anything was touched: how far onto the SD card, whether the ~300
`CM_`/`cm_` identifiers move, and whether the save format's own extensions
do. The answer was yes to all three, which is the version with something to
lose, so most of the work is the losing cases -- adopting the old data
directory, renaming the saved files on every start until it sticks, and
reading both magics for ever.

**Then it was run on the badge, and it crashed** (2026-09-26). Not the
rename -- the migration, on the stack (F-93), and looking at the wreckage
turned up the much worse bug underneath it (D-93): a half-migrated world
read as an EMPTY SLOT and could be built over. Both are fixed, and the
second one is the reason this round was worth doing on real hardware
rather than shipping on green host checks.

What the crash also showed is that `make install` had broken for a reason
that had nothing to do with any of this: the directory rename left
`badgelink/tools/.venv` pointing at its old absolute path, so BadgeLink
could not start at all. A virtualenv does not survive being moved.

**It is done, and it was done on the badge.** A real card, carrying a real
world in slot 1, went from CraftMiner to SynthMiner: `/sd/craftminer`
adopted, `level.cmw` and 21 `r.*.cmr` renamed, and both
`/sd/craftminer` and `/sd/apps/at.cavac.craftminer` deleted -- which is
also what took CraftMiner off the launcher. The player's message shows
while it happens.

Every one of the three bugs in the way was invisible to `make check`:

| | what the host could not see |
|---|---|
| F-93 | worldcheck runs with an 8 MB stack; the badge's main task has 8.5 KB |
| D-93 | the host never half-migrates, so it never opens a world under two names |
| F-94 | the host's `remove()` takes a directory; FatFs's `f_unlink` does not |

Each was found by running it, and the second was found only by reading the
wreckage of the first. **The host checks were green the whole time.**

The title screen's three new letters were confirmed by eye on the badge --
**the user**: *"yes, yes the title letters look ok."*

**The player has to do nothing by hand** (D-92). The first start adopts both
old directories, renames the saved files, and then deletes
`/sd/craftminer` and `/sd/apps/at.cavac.craftminer` -- which is also what
takes CraftMiner off the launcher's menu, since the launcher lists
`/sd/apps/<slug>` and this game was never in appfs to be removed from.
The deletion refuses, loudly and in the log, if either adoption left
anything behind.

That refusal is the interesting half. Left alone, a leftover
`/sd/craftminer` is harmless -- an empty directory costing nine `stat`
calls a start. The hazard was never the directory but the old APP: play it
once after the rename and it writes a world into `/sd/craftminer/worlds`,
which adoption then cannot take, because `worlds` moves as one entry and
the destination already exists. The world would sit on the card, intact and
invisible, with one line in the debug console as the only sign. Deleting
the old app is what removes that possibility; refusing to delete anything
that still holds a world is what makes it safe to do automatically.

The icons in `metadata/` are still the template's placeholder -- a red
question mark -- so the rename had nothing to do there. They are the one
visible thing left that does not say what this is.

## Critical files

- **Lifted from `../tanmatsu-showreel-grace`:** `main/craftminer/voxel/*`,
  `main/{mesh,mesh_render,xform,camera,backdrop,horizon}.{c,h}`,
  `main/common/texcache.*`, `textures/*.png`,
  `tools/{make_textures.py,meshcheck.c,scenecheck.c}`. The textures were
  **flattened** in step 0.2 -- the showreel keeps one subdirectory per reel
  segment, this is one app -- and a `synthminer/` prefix that survived the
  flattening in one string is F-110.
- **Adapted from `../tanmatsu-synthracer-grace`:** `main/controls_settings.{c,h}`
  and `main/keybind_ui.{c,h}` (the `se_bindings` + `se_ui` + `se_ui_capture_key`
  pattern), and the `main/screens.c` menu shape.
- **Copied verbatim:** `dir_open()` from `synthengine3D/src/se_mp3.c:198-245`,
  the FatFs path probing for world enumeration and deletion.
- **Changed:** `CMakeLists.txt`, `Makefile`, `metadata/metadata.json`,
  `main/main.c`, `README.md`, and `main/testkit/profile.{c,h}` (`PROF_HUD`,
  F-46).
- **The instruments.** `common/trace.{c,h}` is the flight recorder --
  always on, one file per playthrough, pure stdio so the host checks
  exercise it (step 48). `tools/traceanalyse.py` reads it.
  `tools/{streamcap,streamanalyse,infoanalyse}.py` are the livestream's,
  from step 44. The rule these exist to enforce: a diagnostic that has
  to be switched on BEFORE the bug is a promise to reproduce the bug,
  and the two that mattered most (F-111's render bug, F-113) were both
  noticed while playing and never reproduced on demand.
- **Written here, and generated rather than drawn or typed:**
  `tools/make_icons.py` (the launcher's three icons, from the game's own
  pickaxe and stone, step 45), `tools/make_metadata.py` (metadata.json's
  asset list, from the directories the install rules glob) and
  `tools/apprepocheck.py` (what the published directory holds against what
  the file promises) -- both of the last two out of F-109. The instruments
  from step 44 are `tools/{streamcap,streamanalyse,infoanalyse}.py`.
- **Engine: CHANGED, with permission (D-39).** No longer "no changes planned",
  which it was until the user asked for the rasteriser to be looked at.
  SynthEngine3D is a submodule and its commits are its own; the version went
  to **2.2** when the mixer gained public calls (see below).
  * `include/se_ui.h`, `src/se_ui.c`: `SE_MENU_VAL_RADIO`, a ring filled on
    the chosen row, for a single-choice list (the language menu). The user's
    call: "If there is no radio button in the engine, add it."
  * `src/internal/hershey*.h`, `tools/hershey/`: text is UTF-8 and the font
    has Cyrillic, accented Latin, both dashes and the European quotation
    marks (F-69). The user's call, and their permission to amend the fonts:
    "If you need to ammend the fonts (use the full set) ... you are allowed
    to." Hershey's database is vendored with the generator that reads it;
    `simplex` itself is untouched and still public.
  * `include/se_scene.h`, `src/se_scene.c`: `se_scene_set_tint()`, a
    scene-wide colour cast for being underwater, as sibling raster loops
    so an untinted scene is unchanged (D-86).
  * `include/se_audio.h`, `src/audio_mixer.c`: per-class volume
    (`audio_mixer_set_music_volume` / `_set_group_volume`) and
    `audio_mixer_keep_awake()`, which holds the amplifier up through the
    quiet so a short one-shot is not eaten by its turn-on (D-85, F-75).
    The engine is **2.2** from here.
  * `src/se_run.c`, `include/se_run.h`: the present flips between the
    display driver's three buffers, on graceloader 2.6.0's refresh
    callback, and drops the frame from the cache (D-87, F-89).
  * `src/se_scene.c`, `include/se_scene.h`: the raycast renderer removed
    (D-88), and `SE_RENDER_BANDED` added (D-89) and removed again (D-90)
    within the same unreleased 2.2, `SE_SCENE_BAND_W` with it. What
    survives of it is the **raster target**: the raster passes write
    through one struct rather than the frame-level buffers, kept because
    it is what a second core would need. `SE_RENDER_BUILTIN_COUNT` is 1.
  * `CMakeLists.txt`: built `-O2`, not `-Os` (F-39).
  * `src/se_scene.c`: `ceil_i` / `floor_i` instead of the libm calls in the
    column scans, and per-pass pixel and span counters.
  * `include/se_scene.h`: `scene_fill_stats()` -- the one public addition.
  * `CHANGELOG.md`: all of the above, with the measurements.
  The standing rule still holds for everything NOT asked for: an engine problem
  that turns up in passing is still stop-and-ask.
