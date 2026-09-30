---
title: How a world is made
summary: The order the ground is built in, and what that means for where things are.
---

A SynthMiner world is **64 blocks tall**, endless sideways, and built from a seed
you can type in when you create it. Two worlds with the same seed are the same
world, down to which field the cows are standing in.

It is generated a chunk at a time as you walk, always in the same order:

| | |
|---|---|
| **Height** | Two noise fields added: a broad one that decides land from sea, a finer one for hills. So coastlines are big and the ground between them is interesting. |
| **Biome** | Two more, slower fields — temperature and humidity — pick one of [five places](biomes.html), and set what the surface is, how deep the soil runs and how much grows on it. |
| **Strata** | Bedrock at the bottom, stone above it, then the biome's soil, then its surface block. At the waterline it is sand in every biome, which is why every shore is a beach. |
| **Sea** | Everything below **y = 24** that would have been air becomes water. |
| **Caves** | A 3D field carves tunnels out of the stone, wider the deeper it goes. A separate field decides the few places one is allowed to break the surface. |
| **Ores** | Veins of [coal and iron](../ores/index.html), on a coarse grid. |
| **Plants** | Flowers, grass, cactus, and the rare wild crop. |
| **Trees** | Last, so a canopy can cross a chunk boundary. |
| **Animals** | A herd is placed with the land, not spawned at you later. |

## Heights worth remembering

| y | What is there |
|---|---|
| 0 | Bedrock. Unbreakable, the floor of the world. |
| 1–27 | Stone and caves. **Iron** can appear up to 28. |
| 24 | **Sea level.** Water fills everything hollow below this. |
| up to 40 | **Coal** can appear anywhere up to here. |
| ~13–45 | Where the ground actually is, depending on the biome. |
| 40+ | Bare rock on mountains; snow above 41 on about a fifth of the high ground. |
| 63 | The top of the world. |

<kbd>Backspace</kbd> shows your height, which is the only way to know whether you
are deep enough for iron.

## Chunks, and what "loaded" means

The world is stored in 16×16 columns called chunks, and only the ones near you
are in memory. This has two consequences you can see:

- **Beyond the loaded world, everything is solid.** You cannot walk out past the
  edge of what has been streamed in — and neither can an animal, which is why a
  cow left in a field is in that field tomorrow.
- **Time still passes in an unloaded chunk.** A field you left an hour ago has
  grown an hour when you come back; a composter has composted; a furnace has
  smelted. Nothing is simulated while you are away — the game works out what
  *would* have happened the moment you look. See
  [Time, growth and sleeping](../mechanics/time.html).

## The Far Lands

Walk far enough **west** — past x = −2048 — and the ground stops being this
world's and becomes Minecraft Beta 1.7.3's, fed coordinates past the point where
its terrain generator overflowed. It is a wall of stone from the bedrock to the
sky, riddled with tunnels that all run west.

This is not a decoration that imitates the Far Lands; it is Beta's own generator,
ported, breaking in the same arithmetic it broke in originally. About one chunk in
four of the last ordinary land before the wall has a **sign** standing in it,
left by whoever got there first.

It is a long walk. Bring food.
