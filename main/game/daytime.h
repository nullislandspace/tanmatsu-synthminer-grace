#pragma once
// =====================================================================
//  SynthMiner  --  the time of day
// ---------------------------------------------------------------------
//  A world's clock is a tick count (world_meta_t.time_of_day, D-51's
//  rule: elapsed ticks, advanced by the world's own tick, never a wall
//  clock), and a day is DAY_TICKS of them -- 20 minutes at 20 Hz, as in
//  Minecraft. Everything the sky and the light need is a pure function
//  of that count:
//
//    the sun's direction   rising in the east (+x), noon overhead-ish,
//                          setting in the west; the moon opposite
//    the sky and fog       day blue, an orange band round sunrise and
//                          sunset, dark blue at night
//    `day`                 0 at night .. 1 in full daylight
//    the light table       what mesh_set_light_lut() wants: a cell's
//                          light byte -> a brightness, with the sky part
//                          dimmed by the time of day
//
//  Pure: no engine, no allocation. The host checks test it.
// =====================================================================

#include <stdint.h>

#include "math/xform.h"

#define DAY_TICKS 24000
// Where a new world's clock starts: an hour after sunrise, so the first
// thing a player sees is a morning and not a sunrise they did not ask
// for.
#define DAY_START 1000

typedef struct {
    vec3_t   sun_dir;   // towards the sun, normalised; the moon is -sun_dir
    uint32_t sky_argb;  // the sky, and what the backdrop is filled with
    uint32_t fog_argb;  // what the far terrain fades into
    float    day;       // 0 night .. 1 full day
} daytime_t;

daytime_t daytime_at(int64_t ticks);

// THE CLOCK THAT DOES NOT MOVE THE SKY (Part M, D-139). The Deep Mine has
// no sun and no moon, so there is nothing for the clock to be a function
// of -- but the renderer still reads `day` (the sky half of the light
// table) and a direction out of a daytime_t, so it has to answer. Fixed
// values, and the tick count still runs, because every persisted duration
// in this game is a count of ticks (D-51): crops, fleece, a smelt and a
// despawn all read the same clock whether or not there is a sunrise.
//
// `day` is 1.0 rather than something lower. The cave's light is a
// constant, and dimming it by a time of day that does not exist would
// make the whole place darker for no reason anybody could see.
daytime_t daytime_deep_mine(void);

// Fill the 256-entry light table (mesh_render.h) for daylight `day`.
// Entry l is the brightness, 0..32, of a face lit by light byte l:
//
//   effective = max(block, sky - darkening)     darkening 0 by day, 9 at night
//   brightness = a power curve of effective     each level 0.8 of the next
//
// so a sunlit meadow is full brightness, the same meadow at midnight is
// a dim blue-grey, a torch looks the same by day and by night, and a
// cave with no torch in it is dark at noon.
void daytime_light_lut(float day, uint8_t lut[256]);

// The time as a clock face, for the HUD: hours 0..23 and minutes, with
// sunrise at 06:00 like Minecraft's.
void daytime_clock(int64_t ticks, int* hours, int* minutes);
