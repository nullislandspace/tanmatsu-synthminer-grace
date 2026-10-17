---
title: Armour
summary: Four pieces, a percentage off every blow, and a figure in the inventory to put them on.
---

| Piece | Where | Iron | Titanium | [Forged from](../devices/smithing-forge.html) |
|---|---|---|---|---|
| **Helmet** | Head | 9% | **18%** | 4 ingots |
| **Chestplate** | Chest | 16% | **32%** | 6 ingots |
| **Leggings** | Legs | 12% | **24%** | 5 ingots |
| **Boots** | Feet | 8% | **16%** | 3 ingots |
| **Full set** | | **45%** | **90%** | **18 ingots** |

Every piece is made in the [smithing forge](../devices/smithing-forge.html). An iron
set is eighteen ingots and **four in-game days** at the forge; a titanium set is
eighteen titanium ingots and **eight**. Add 2 more ingots for a
[sword](sword.html) and the whole kit is **twenty ingots**.

**Durability is 150 for iron and 300 for titanium**, and it is spent — see *Armour
wears out* below.

## Armour is a percentage, not a number

A blow is reduced by the **total guard of everything you are wearing**, added together.
Titanium is exactly twice iron piece for piece, so a full titanium set takes off 90% of
everything.

| Wearing | Guard | A [zombie](../mobs/zombie.html)'s 10 becomes | A [skeleton](../mobs/skeleton.html)'s 8 becomes |
|---|---|---|---|
| Nothing | 0% | **10 — five hearts**, dead in 2 swings | 8 — four hearts, dead in 3 |
| Chestplate only | 16% | 8 — four hearts | 7 |
| Full iron | 45% | 6 — three hearts, dead in 4 swings | 4 — two hearts |
| **Full titanium** | **90%** | **1 — half a heart**, dead in 20 swings | **1 — half a heart** |

The rounding is ordinary: 45% off a zombie's 10 is 5.5, which lands as 6. And a blow
**never rounds down to nothing** — the smallest thing armour can turn a hit into is half
a heart, so no amount of gear makes you immune.

<p class="note"><b>That half-heart floor is the whole design.</b> A full titanium set
does not make the Deep Mine safe; it makes a zombie a nuisance instead of a death
sentence, and it makes the walk back to the shaft a matter of how much food you have
rather than whether you survive.</p>

## Putting it on

Open the inventory with <kbd>Tab</kbd>. There is a **figure of the player** at the right
hand end of the grid, with four slots on it labelled **Head · Chest · Legs · Feet**.

- Step the cursor **right** out of the last column of the grid and it lands on the
  figure; step **left** to get back. Up and down move between the four places.
- Select an armour piece anywhere in the grid and press <kbd>Enter</kbd> to **put it
  on**. It goes to the place it belongs to, and the slot it was in empties.
- Select a slot on the figure and press <kbd>Enter</kbd> to **take it off**.
- Select something that is not armour and it says so.

**Taking a piece off needs an empty slot in the grid.** Armour does not swap with
whatever happens to be under the cursor, because a full inventory silently eating a
chestplate is worse than being told there is no room.

What you are wearing is saved with your position, so it survives quitting to the title
and coming back.

## Armour wears out

**Every blow you take costs one use from every piece you are wearing.** A full set
standing in a zombie's face spends four times the durability of a single piece.

- A full **iron** set takes 150 swings before all four pieces go at once.
- A full **titanium** set takes 300.

A piece that runs out breaks, says **"Your iron chestplate wore out"**, and is gone —
there is no repairing anything in this game, only forging another one.

## What armour does not cover

Armour guards you against **creature blows** and nothing else:

| | Armour helps? |
|---|---|
| A zombie or skeleton | **Yes** |
| [Lava](../world/lava.html) | No |
| A [fall](../mechanics/health.html) | No |
| [Starving](../mechanics/hunger.html) | No |

That is deliberate. Lava and gravity are the parts of the Deep Mine you survive by
being careful, and a set of armour that made them survivable would remove the reason
not to walk across a lava lake.

## It is visible

In third person the figure wears what is equipped: a helmet on the head, a chestplate
over the torso with pauldrons, leggings, boots. Each piece is drawn in the colour of
its own metal, so **an iron chestplate with titanium boots looks like exactly that**.
Armour does not hide the hard hat or change anything else about how he looks or moves.
