// =====================================================================
//  SynthMiner  --  dimensions (see dimension.h)
// =====================================================================

#include "world/dimension.h"

#include "world/light.h"

// THE ONE ANSWER. Not a field of chunk_t, and not a field of the world:
// the store holds one dimension at a time, so a per-chunk answer could
// only ever disagree with the store about a chunk that should not be
// there. It lives here because this is the only file that owns it.
static dimension_t s_current = DIM_OVERWORLD;

dimension_t dimension_current(void) {
    return s_current;
}

void dimension_set_current(dimension_t d) {
    s_current = (d < DIM_COUNT) ? d : DIM_OVERWORLD;
}

char const* dimension_name(dimension_t d) {
    return d == DIM_DEEP_MINE ? "deep_mine" : "overworld";
}

// The overworld keeps the flat directory. That is not laziness: a world
// already on a card has its regions there, and a check that reads them
// through a new path would find an empty directory and report a world
// with no terrain.
char const* dimension_region_sub(dimension_t d) {
    return d == DIM_DEEP_MINE ? "deep_mine" : "";
}

bool dimension_has_sky(dimension_t d) {
    return d != DIM_DEEP_MINE;
}

int dimension_sky_light(dimension_t d) {
    return d == DIM_DEEP_MINE ? DM_SKY_LIGHT : (int)LIGHT_MAX;
}
