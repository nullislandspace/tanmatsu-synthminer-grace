---
title: Plants and farming
summary: The hoe, wet and dry soil, five crops, and how long each one takes.
---

Farming is the game's second act. It needs almost nothing — a hoe of any material
and some water — and it is what makes food a supply rather than a hunt.

## The four steps

1. **Find water**, or bring it in a [bucket](../tools/bucket.html). Two sources in
   a trench make an infinite spring, so one trip with a bucket is enough for good.
2. **Till the ground** with a [hoe](../tools/hoe.html): point at grass or dirt
   with nothing on top and press <kbd>E</kbd>. It becomes farmland.
3. **Plant** a seed on the farmland — point at the *soil*, not the air above it.
4. **Wait**, then break the ripe plant to harvest it.

## Wet soil and dry soil

Tilled soil comes out **wet** or **dry**, and the difference is the whole of the
water rule:

- **Wet** if there is water within **4 blocks** of it, measured as a 9×9 box on the
  same level. One source waters a large patch.
- **Dry** otherwise — and **dry soil refuses seeds.** It does not take them and
  quietly fail to grow; it tells you the ground is too dry.

<p class="note"><b>The water is only checked twice:</b> when you till, and when you
plant. Nothing watches a field while it grows. So draining the moat afterwards will
not kill a crop — but it will stop you sowing the next one, and re-tilling the plot
is how you fix it.</p>

## The five crops

Every crop has four stages and is harvested at the last one.

| Crop | Seed from | Seed to ripe | Yield | Found wild in |
|---|---|---|---|---|
| [**Wheat**](wheat.html) | Tall grass | **1 day** (20 min) | 1–3 wheat | — |
| [**Tomato**](tomato.html) | 1 tomato → 2 seeds at a table | **1 day** (20 min) | 2–4 tomatoes | Birch woods |
| [**Potato**](potato.html) | Itself | **2 days** (40 min) | 1–3 potatoes | Plains, very rarely |
| [**Beans**](beans.html) | Itself | **2 days** (40 min) | 1–3 beans | Forests |
| [**Rice**](rice.html) | Itself | **2 days** (40 min) | 1–3 rice | Shallow water on sand |

"A day" means a full in-game day: 20 minutes of playing, or one night's sleep.

<p class="note"><b>Rice is the odd one.</b> It does not grow on farmland at all —
it is planted <em>in</em> water exactly one block deep standing on sand, and it
stands two blocks tall. See <a href="rice.html">Rice</a>.</p>

## Where the first seeds come from

You cannot craft a seed out of nothing, so the first of each is found:

- **Wheat seeds** — break **tall grass**. Roughly half of it gives a seed. There is
  tall grass all over the plains and forests, so this is the easy one.
- **Potatoes** — a wild potato plant in the plains. About one column in seven
  thousand, so one or two in a plain you can see across. Keep your eyes open; it
  is the one you are most likely to go without.
- **Beans** — a wild bean plant in a forest, about three times commoner than the
  potato.
- **Tomatoes** — a wild tomato plant in a birch wood. The commonest of the three
  *within* its biome, but birch woods are rare, so it evens out. A tomato must be
  turned into **tomato seeds** at a crafting table (1 tomato → 2 seeds).
- **Rice** — growing in the shallows, on a sandy shore, in water one deep.

Wild plants are generated **ripe**, so the first one you find is also your first
harvest.

## Making it grow faster

[**Compost**](../devices/composter.html) pushes a crop on **one whole stage** —
so three lots of compost take a plant from seed to ripe on the spot. Compost comes
out of a composter, which eats the leaves, flowers, grass and crop stalks you were
clearing anyway.

The same composter also produces **worms**, which is the bait
[fishing](../mechanics/fishing.html) needs. That is not a coincidence: it is the
one box that connects the farm to the water.

## How growing actually works

Worth knowing, because it explains two things you will notice:

- **A field ripens in steps, together.** Each chunk has its own slow clock and
  every plant in it advances on that clock, so a field you sowed at once comes up
  at once. It looks like a field rather than like sixty independent timers.
- **Time passes while you are away.** Come back to a field after an hour of doing
  something else and it has had the hour. Nothing was simulated while you were
  gone — the game works out what should have happened the moment the chunk loads.

See [Time, growth and sleeping](../mechanics/time.html) — including the answer to
"does a night's sleep grow my crops?" (it does).

## And the things that are not crops

- [**Trees and saplings**](trees.html) — wood is renewable: fell a tree, get
  saplings, plant them.
- [**Flowers, grass and cactus**](flowers.html) — including how to make yellow
  flowers grow back, which matters because sausages eat them.
