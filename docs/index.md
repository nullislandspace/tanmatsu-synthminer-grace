---
title: SynthMiner
summary: A blocky open world for the Tanmatsu, loosely inspired by Minecraft.
---

# SynthMiner

**A blocky open world for the [Tanmatsu](https://nicolaielectronics.nl/), loosely
inspired by Minecraft.**

You wake up on a hillside with nothing. There is wood in the trees, stone under
the grass, coal and iron below that, and animals in the fields. By the end of a
long evening you can have a farm, a flock, a stove that cooks a pizza out of the
chest beside it, and a bed that means you no longer start the morning wherever
the world dropped you.

It runs on the handheld itself — an 800×480 screen, a keyboard you can rebind,
and a gyroscope you can look around with if you would rather lean than press
arrows. Worlds live on the SD card and there is no time limit on anything.

This is the **player's manual**. It is here for the parts of the game that are
not guessable: what a sausage maker's second slot wants, how deep iron goes, how
long a tree takes, why a sheep has stopped giving wool.

<ul class="cards">
<li><a href="getting-started.html"><b>Your first day</b><span>Wood, a table, a pickaxe, a torch — in that order, and why.</span></a></li>
<li><a href="controls.html"><b>Controls and the HUD</b><span>Every key, the hotbar, and what the bars along the bottom mean.</span></a></li>
<li><a href="world/index.html"><b>The world</b><span>How the ground is built: biomes, caves, the sea, and the wall at the west.</span></a></li>
<li><a href="ores/index.html"><b>Ores and stone</b><span>Coal, iron, and how far down you have to go.</span></a></li>
<li><a href="plants/index.html"><b>Plants and farming</b><span>The hoe, wet soil, five crops, and trees that grow back.</span></a></li>
<li><a href="animals/index.html"><b>Animals</b><span>Pigs, cows, sheep and dogs: finding, feeding, breeding, shearing.</span></a></li>
<li><a href="tools/index.html"><b>Tools</b><span>Four kinds, three tiers, and what each one is actually for.</span></a></li>
<li><a href="devices/index.html"><b>Machines and furniture</b><span>Furnace, chests, composter, the two makers, the kitchen stove, the bed.</span></a></li>
<li><a href="mechanics/hunger.html"><b>Hunger</b><span>Drumsticks, the invisible reserve, and why a good meal lasts longer.</span></a></li>
<li><a href="mechanics/fishing.html"><b>Fishing</b><span>Worms for bait, and a bite you have to strike.</span></a></li>
<li><a href="recipes.html"><b>Every recipe</b><span>One table per station: hands, table, furnace, makers, stove.</span></a></li>
<li><a href="food.html"><b>Every food</b><span>Thirteen dishes and eight other edibles, with what each is worth.</span></a></li>
</ul>

## The shape of the game

There is no boss, no score and nothing to win. What there is instead is a chain
of things that each need the one before it, and the chain is most of the game:

**wood** → a crafting table → a wooden pickaxe → **stone** → a stone pickaxe →
**iron ore** → a furnace → **iron** → a bucket, shears and a stove → **milk,
wool and cooked food**.

Off to the side of that spine: a hoe turns grass into a field, a composter turns
the weeds into fertiliser *and* fishing bait, a bucket turns a cow into cheese,
and a bone turns a wild dog into company.

## Numbers worth knowing

| | |
|---|---|
| World height | 64 blocks, bedrock at the bottom |
| Sea level | 24 |
| A day | 20 minutes of playing (24 000 ticks) |
| Reach | 4.5 blocks |
| Jump | 1.33 blocks — so you can climb one block, never two |
| Free fall | 3 blocks; every block after that costs half a heart |
| Health | 10 hearts |
| Hunger | 10 drumsticks, plus a reserve you cannot see |
| Languages | 32 |

<p class="note"><b>Worlds are forwards-compatible.</b> A world made in an older
version opens in a newer one and gains whatever the new version added — the save
format has not changed since the first release, and the game upgrades a world
chunk by chunk as you walk about in it.</p>

## Also here

- [Version history](releases/index.html) — what each release added, newest first.
- The [source](https://github.com/nullislandspace/tanmatsu-synthminer-grace), if
  you would rather read how any of this actually works. Every header file in
  `main/` explains its own corner of the game and why it was built that way.
