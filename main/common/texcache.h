#pragma once
// =====================================================================
//  Showreel asset  --  shared texture cache
// ---------------------------------------------------------------------
//  Assets ask for textures by file name; each file is loaded once (from
//  the app's install directory, into PSRAM) and shared, so the station
//  and the marauders can both use plate_gunmetal.png without two copies.
//  Everything is unloaded together by texcache_shutdown().
//  Lifted from tanmatsu-showreel-grace,
//  main/common/texcache.h. Changes here are SynthMiner's;
//  the showreel stays the origin to diff against.
// =====================================================================

#include "synthengine3d.h"

// HOW MANY TEXTURES MAY BE LOADED AT ONCE, and it has to cover every
// block material (VM_COUNT, voxel_mesh.h) plus the ones asked for by
// name afterwards: water_blend.png, the three torch frames, and one
// item_*.png per item that is not a cube.
//
// It was 48 and step 9 took the material count to 64, which is the bug
// the user found: the loop in chunk_render_init filled the cache and
// everything after entry 48 failed with "cache full" -- the potato,
// tomato, bean and rice textures, then water_blend.png (so transparent
// water could not be switched on at all), then every item icon. Each
// one falls back to a flat average colour, which is why four of the
// five crops looked like stand-ins and only changed colour as they
// grew: there was no picture, only the colour.
//
// tools/worldcheck.c holds this against VM_COUNT so it cannot happen
// again quietly. 128 is roughly 64 KiB of 16x16 RGB565 and leaves room
// for the machines of steps 10 and 11.
#define TEXCACHE_MAX 128

// Where the texture files are. Call before the first texcache_get().
void texcache_init(char const* asset_dir);
void texcache_shutdown(void);

// The texture loaded from `file`, or NULL if it failed to load (logged
// once; the caller draws flat instead).
se_texture_t const* texcache_get(char const* file);

// Log how the loaded textures split between internal SRAM and PSRAM.
// Call once after the last texcache_get(): a texture that fell back to
// PSRAM still works, so this is the only way to notice.
void texcache_report(void);
