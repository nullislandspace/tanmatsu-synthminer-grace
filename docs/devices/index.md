---
title: Machines and furniture
summary: Everything you put down and then use, and what each one costs.
---

| Thing | Costs | Does |
|---|---|---|
| [**Crafting table**](crafting-table.html) | 4 planks | Unlocks most recipes |
| [**Furnace**](furnace.html) | 8 cobblestone | Smelts ore, sand and cobble; makes coal out of logs |
| [**Chest**](chest.html) | 8 planks | 27 stacks of storage |
| [**Trash chest**](trash-chest.html) | 8 planks + 1 coal | Destroys what you put in it, ten minutes later |
| [**Disassembly bench**](disassembly-bench.html) | 4 planks + 1 coal | Takes things apart and gives the materials back |
| [**Composter**](composter.html) | 7 planks | Plant waste → **compost** and **worms** |
| [**Cheese maker**](cheese-maker.html) | 7 planks | Milk → cheese |
| [**Sausage maker**](sausage-maker.html) | **9 iron ingots** | Pork + flower → sausage, and sometimes a **bone** |
| [**Kitchen stove**](kitchen-stove.html) | 3 ingots + 6 stone + 8 planks | Cooks thirteen dishes out of the chest beside it |
| [**Bed**](bed.html) | 3 wool + 3 planks | Skips the night, and sets where you respawn |
| [**Fences and gates**](fences.html) | 2 planks + 4 sticks | Keep animals in |
| [**Torches**](torch.html) | 1 coal + 1 stick → 4 | Light |

Everything except the torch and the fence is made at a
[crafting table](crafting-table.html). The table itself, and torches, can be made
with bare hands.

## The machines never tick

Five of these are machines with a timer: the furnace, the composter, the two makers
and the stove. None of them is *running* in any real sense.

Instead, each one remembers when it was last looked at, and works out what should have
happened the moment you open it, draw it, or the game saves it. That has three
consequences that are all in your favour:

- **A machine in a chunk you have not visited costs nothing.** You can have fifty
  composters.
- **A box left for a week is right when you open it.** Nothing is lost by walking
  away.
- **Sleeping fast-forwards them.** The world clock jumps to morning, and anything
  measured against the world clock jumps with it — see
  [Time, growth and sleeping](../mechanics/time.html).

## How long each one takes

| Machine | Per item |
|---|---|
| **Furnace** | 10 seconds |
| **Sausage maker** | 1 minute of playing |
| **Kitchen stove** | 1 minute of playing |
| **Trash chest** | 10 minutes of playing, then it empties |
| **Composter** | **1 in-game day** (20 minutes of playing) |
| **Cheese maker** | **1 in-game day** |

"An in-game day" and "a minute of playing" are genuinely different clocks. A minute of
playing is a minute on your wrist. An in-game day is twenty minutes of playing — or one
night's sleep, which is why sleeping empties your composters.

## Getting things back out

Every machine gives back **everything inside it** when you break it, so you never lose
a chestful of iron to a misplaced swing.

To take something out of a slot without breaking the machine, open it and pick the
**"take it out"** row from the slot's list. The two makers both have this on every slot.

<p class="note"><b>Not everywhere yet.</b> The furnace, the composter and the stove's
fuel slot have no "take it out" row — if you put the wrong thing in one of those, the
way to get it back is to break the machine and put it down again. You lose nothing but
the block, and the block drops.</p>
