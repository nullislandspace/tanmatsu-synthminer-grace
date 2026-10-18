---
title: Time, growth and sleeping
summary: Two different clocks, which one each thing uses, and why sleeping is the best button in the game.
---

## The day

| | |
|---|---|
| A full day | **24 000 ticks** = **20 minutes** of playing |
| The game runs at | 20 ticks a second |
| A new world starts at | Morning |

There is a real sun, a moon, clouds and stars, and the light changes as the day goes.
Nothing comes out at night in this version — the dark is an inconvenience, not a threat.

## Two clocks, and the difference matters

Almost everything in the game that takes time runs on the **world clock** — the same number
that decides where the sun is. A few things run on a count of **ticks you actually played**.

They are identical while you are walking around. They come apart the moment you go to
[bed](../devices/bed.html), because sleeping **jumps the world clock forward to morning**
without any time passing for you.

### On the world clock — sleeping fast-forwards these

| | |
|---|---|
| **Crops** growing | wheat and tomatoes a day, potatoes, beans and rice two days |
| **Saplings** becoming trees | one day |
| **Composters** | one unit a day |
| **Cheese makers** | one cheese a day |
| **Sausage makers** | one a minute — a whole batch overnight |
| **Kitchen stoves** | one dish a minute — likewise |
| **Furnaces** | ten seconds an item |
| **Trash chests** | they empty ten minutes after you last used them |

### On ticks you played — sleeping does nothing for these

| | |
|---|---|
| **Your hunger** — so sleeping is strictly free |
| **A shorn sheep's fleece** — twenty minutes of playing |
| **A calf growing up** — ten minutes of playing |
| **A fed animal's mood**, and its rest afterwards |
| **Dropped items** waiting to despawn |

<p class="note"><b>So the rhythm of a farm is: load everything, then sleep.</b> Fill the
composters, fill the cheese barrels, put fuel and ingredients in the stove and the sausage
maker, sow the fields — and go to bed. It is all done in the morning, and it cost you
nothing but the walk to the bed.</p>

## Machines run while you are near, and catch up when you are not

This is the other thing worth knowing, and it works entirely in your favour.

**No machine ever waits for you to watch it.** Each one remembers the moment it was last
looked at and works out what should have happened — a little every moment while its chunk
is loaded, and the whole gap in one go the moment that chunk comes back. A composter in a
chunk you have not visited for a week costs the game exactly nothing — and is full when
you get there.

The same is true of **crops**. A field you left an hour ago has had the hour. A chunk
arriving back in memory is advanced by however long it was gone, in one go, the moment it
lands.

Which means:

- you can build as many machines as you like — fifty composters cost no more than one;
- **nothing is lost by walking away**, ever;
- there is no reason to stand and watch anything.

## You can see it working

| Machine | What it shows from outside |
|---|---|
| **Furnace, kitchen stove, smithing forge** | A flame out of the top, for exactly as long as it is working |
| **Composter** | A wisp of haze above it while its output holds finished compost |
| **Sausage maker** | Its dark grey side plate turns dark red while its output holds a sausage |
| **Cheese maker** | The barrel is drawn empty, full of milk, or holding finished cheese |

The flame is the whole signal: **no flame means nothing is running in that machine right
now** — it finished, it is out of fuel, or its output is full. Which kind of *nothing* it
is takes one look inside, which is one keypress on one block rather than a walk around
everything you own.

**A machine whose output is full stops.** Its fire goes out and it stops burning coal
instead of quietly working away; empty it and it starts again.

All of this is only drawn for machines in the loaded area, which is the part of the world
you can see anyway.

## Crops ripen in steps, together

Each chunk has its own slow clock, and every plant in it advances on that clock rather than
on a timer of its own. So a field you sowed in one go comes up in one go — it looks like a
field ripening rather than like sixty independent egg-timers.

A seed planted just before the chunk's next step gets that step almost free. At most one
stage, and on average half of one, in your favour.

## The clock on screen

Press <kbd>Backspace</kbd> and the overlay gives you the in-game time along with your
coordinates and heading. Useful for working out whether it is worth going to bed yet — the
bed refuses in daylight, though it still moves your respawn point.

## How long things take, all together

| | |
|---|---|
| Wheat, tomatoes, a sapling | **1 day** — 20 min, or one sleep |
| Potatoes, beans, rice | **2 days** — 40 min, or two sleeps |
| A composter unit | 1 day |
| A cheese | 1 day |
| A sausage | 1 minute |
| A cooked dish | 1 minute |
| A furnace smelt | 10 seconds |
| A trash chest emptying | 10 minutes |
| A sheep's fleece | 20 minutes **of playing** |
| A calf growing up | 10 minutes **of playing** |
| A fed animal staying in the mood | 30 seconds |
| … and resting afterwards | 5 minutes |
