---
title: Smithing forge
summary: The machine with a Start button. One item per press, a day or two of forging, and the chest standing next to it holds the metal.
---

| | |
|---|---|
| **Recipe** | **12 diamonds + 10 planks**, at a [crafting table](crafting-table.html) |
| **Made at** | The [crafting table](crafting-table.html) — and it is the only way to get one |
| **Puts down** | **Two blocks** — the forge, and a chest beside it |
| **Slots** | Fuel · Metal · Output |
| **Speed** | **1 in-game day** for an iron item, **2** for a titanium one |
| **Breaks with** | Anything. A pickaxe is quicker |

The forge is what [diamonds](../ores/diamond.html) are for. Twelve of them, ten planks,
and it is the only way to make a [sword](../tools/sword.html) or
[armour](../tools/armour.html).

It looks like a [furnace](furnace.html) cut from blue diamond, and it works like the
[kitchen stove](kitchen-stove.html): a working block with **its own chest beside it**,
placed as one thing and broken as one thing.

## It has a Start button

This is the one machine in the game that is **not** left running.

The furnace, the stove, the composter and the two makers all cook away for as long as
they have fuel and ingredients. The forge is **told what to make, makes exactly one of
it, and stops.** Press <kbd>Enter</kbd> on **Start crafting** for the next item.

That is what makes a day of forging a decision instead of a queue you forget about: you
choose the item, you pay for the item, and the forge does not go off and spend your
ingots on something else overnight.

## What it makes

| Item | Metal | Ingot cost | Time |
|---|---|---|---|
| [Iron sword](../tools/sword.html) | iron | 2 | 1 day |
| [Iron helmet](../tools/armour.html) | iron | 4 | 1 day |
| [Iron chestplate](../tools/armour.html) | iron | 6 | 1 day |
| [Iron leggings](../tools/armour.html) | iron | 5 | 1 day |
| [Iron boots](../tools/armour.html) | iron | 3 | 1 day |
| [Titanium sword](../tools/sword.html) | titanium | 2 | **2 days** |
| [Titanium helmet](../tools/armour.html) | titanium | 4 | **2 days** |
| [Titanium chestplate](../tools/armour.html) | titanium | 6 | **2 days** |
| [Titanium leggings](../tools/armour.html) | titanium | 5 | **2 days** |
| [Titanium boots](../tools/armour.html) | titanium | 3 | **2 days** |

**The time is set by the metal, not the item.** A titanium sword takes as long as a
titanium chestplate. A full set of armour plus a sword is **twenty ingots and ten jobs**.

None of it is reversible: a [disassembly bench](disassembly-bench.html) will not turn a
chestplate back into six ingots, and the forge itself does not come back as twelve
diamonds.

## Using it

Point at the forge and press <kbd>E</kbd>. Five rows:

| Row | |
|---|---|
| **Item** | What to forge. Pick from the ten |
| **Fuel** | Coal, or anything wooden — the same fuels a [furnace](furnace.html) burns |
| **Metal** | What the forge is holding for the current job, and what its chest has |
| **Output** | Where the finished item waits. Take it out here |
| **Start crafting** | Make **one** of the chosen item |

**The metal comes out of the chest beside it**, the way a stove's ingredients do. You
do not feed ingots into the forge by hand.

## The metal is spent at the start

The ingots leave the chest **the moment you press Start** and sit in the forge's own
slot until the item is finished.

So a forge whose chest gets emptied, robbed, or buried under a collapsed tunnel still
finishes what it was told to make. What a broken forge costs you is **the time**, not
the metal: break it and the committed ingots come back out with everything else in the
chest.

## While it is working

**The item is locked.** The selector cannot be changed mid-job — it names the metal that
was already spent, and letting it change would mint ingots out of nothing. Pick the next
thing after the current one finishes.

**The fuel slot stays open.** If the forge runs out of coal halfway through, it stops
where it is, says **"it is waiting for fuel — the job keeps its progress"**, and finishes
as soon as you drop more coal in. It does not start the item over, and it does not give
the ingots back.

**Fuel needed, per item:**

| Item | Coal |
|---|---|
| Iron | **15** |
| Titanium | **30** |

One coal burns 1600 ticks and an iron item is 24000, which is where 15 comes from. A
stack of logs works too; the fuel row tells you how many items what you have will cover.

## When it will not start

It says which of these it is, which saves a lot of guessing:

| It says | Fix |
|---|---|
| choose what to forge | Pick an item |
| no chest beside it | Put the pair down properly, or replace a broken chest half |
| **the chest is short of 6 iron ingots** — and it names the amount | Put more in the chest |
| nothing to burn | Coal, logs, planks, sticks, spent wooden tools |
| the output is full | Take the finished item out |
| it is working — the item is fixed until it finishes | Wait, or take the finished item and start the next |

## Days, and sleeping

An in-game day is **20 minutes of playing**, or one night's sleep — but a bed only
advances the clock **13000 ticks, 54% of a day**, so a sleep *contributes* to a forging
job rather than paying for one. An iron item is an afternoon awake, or two nights. A
titanium item is four.

Load the forge up before you sleep and before you go down the
[mine shaft](mine-shaft.html); both are ways of spending the wait. See
[Time, growth and sleeping](../mechanics/time.html).

<p class="note"><b>It is not a furnace.</b> The forge does not smelt. Ore still goes
through a <a href="furnace.html">furnace</a> into an ingot, and only then to the forge.
The order is: mine → furnace → forge.</p>
