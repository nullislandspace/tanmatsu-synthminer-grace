#pragma once
// =====================================================================
//  SynthMiner  --  the one host/badge seam
// ---------------------------------------------------------------------
//  Most of this game is portable C: the world, generation, the mesher,
//  physics, picking, inventory, crafting. That is deliberate -- it is
//  what lets tools/worldcheck.c test them on a PC in seconds instead of
//  on the badge in minutes (claudeplans/synthminer.md, Part H).
//
//  Allocation is the only thing those modules need that differs between
//  the two, so it is the only seam. On the badge every allocation here
//  goes to PSRAM: this data is read sequentially, once per frame or
//  less, and the 150 KiB of internal SRAM is far too precious for it
//  (its largest free block is about 62 KiB). On the host it is plain
//  malloc.
//
//  THE RULE, enforced by `make hostpurity`: no module in the pure set
//  may include esp_heap_caps.h, esp_log.h, a FreeRTOS header, or
//  synthengine3d.h. It includes this instead.
// =====================================================================

#include <stddef.h>
#include <stdint.h>  // SIZE_MAX, for sm_free_bytes() on the host

#ifdef SM_HOST

#include <stdlib.h>
#define sm_alloc(n)      malloc(n)
#define sm_calloc(n, s)  calloc((n), (s))
#define sm_realloc(p, n) realloc((p), (n))
#define sm_free(p)       free(p)

// HOW MUCH IS LEFT. On the host, "plenty": a PC's heap is not the thing
// this is for, and a check that trimmed caches on the host would be
// testing the host's malloc. The badge answers honestly.
#define sm_free_bytes() ((size_t)SIZE_MAX)

#else

#include "esp_heap_caps.h"
#define sm_alloc(n)      heap_caps_malloc((n), MALLOC_CAP_SPIRAM)
#define sm_calloc(n, s)  heap_caps_calloc((n), (s), MALLOC_CAP_SPIRAM)
#define sm_realloc(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM)
#define sm_free(p)       heap_caps_free(p)

// WHAT IS LEFT OF PSRAM. Read rather than guessed, because the mesh
// cache is the one thing here that grows without being asked to and the
// consequence of letting it win is invisible: a mesh that cannot be
// allocated is a block you broke that stays on screen (F-135).
#define sm_free_bytes() heap_caps_get_free_size(MALLOC_CAP_SPIRAM)

#endif
