---
title: Controls and the HUD
summary: Every key, what the bars mean, and the two ways of looking around.
---

## The keyboard

These are the defaults. **Every one of them can be rebound** — pause, then
*Settings → Controls* — so if a key is not where this table says it is, that is
where you moved it to.

| Key | Does |
|---|---|
| <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> | Walk |
| <kbd>Space</kbd> | Jump — and swim upwards while you are in water |
| <kbd>Shift</kbd> | Sneak — a **toggle**, not a key you hold. In water it means *dive* |
| <kbd>Q</kbd> | **Break** — hold it on a block until the block gives up |
| <kbd>E</kbd> | **Use** — place a block, open a machine, eat, milk a cow, cast a rod |
| <kbd>←</kbd> <kbd>→</kbd> <kbd>↑</kbd> <kbd>↓</kbd> | Look |
| <kbd>F1</kbd>…<kbd>F6</kbd> | Pick hotbar slot 1 to 6 |
| <kbd>Tab</kbd> | Inventory |
| <kbd>C</kbd> | The crafting book |
| <kbd>G</kbd> | Drop what you are holding |
| <kbd>Backspace</kbd> | Show where you are: coordinates and heading |
| <kbd>0</kbd> | Screenshot, onto the SD card |
| <kbd>Esc</kbd> | Pause — and the way to save and leave |

## Sneaking

<kbd>Shift</kbd> **toggles** sneak — tap it once to crouch, again to stand. While you are
sneaking:

- you move at about a third of walking speed;
- **you will not walk off an edge**, which is what makes building out over a drop possible;
- you will **not step up** onto a block, so you can work along the edge of a shelf without
  climbing it;
- the camera drops a little, and Fred visibly crouches.

**In water it means dive instead.** There is no ledge to fall off in a lake, and swimming
into a bank has to get you out of it — so sneak sinks you, and <kbd>Space</kbd> lifts you.

<p class="note"><b>One key does nearly everything.</b> <kbd>E</kbd> is the whole
interface to the world: what it does depends on what you are pointing at and what
is in your hand. Pointed at a cow with a bucket it milks; pointed at tilled soil
with seeds it plants; pointed at nothing in particular while holding bread it
eats the bread. When it refuses, it says why on one line at the bottom of the
screen — read that line, it is usually the answer.</p>

## Looking by turning the badge

There is a gyroscope in the Tanmatsu, and *Settings → Controls → Gyroscope look*
turns it into the mouse this handheld does not have. Turn the badge thirty
degrees and the view turns thirty degrees; tip it back and you look up.

The arrow keys keep working alongside it — the two add together, so you can lean
for the big movements and nudge with a key.

## The HUD

Along the bottom of the screen, from the middle outwards:

- **The crosshair**, and a wire box round the block you are pointing at. No box
  means nothing is in reach.
- **The hotbar** — six slots, <kbd>F1</kbd> to <kbd>F6</kbd>. The rest of your
  inventory is behind <kbd>Tab</kbd>.
- **Hearts**: ten of them, twenty half-hearts of health.
  See [Health, falling and death](mechanics/health.html).
- **Drumsticks**: ten of them, and they go down as you walk, mine and jump.
  See [Hunger and saturation](mechanics/hunger.html).
- A **durability bar** under a tool that has been used, and a number in the
  corner of a stack.

At the top there is a **compass**, with a mark on it for **HOME** — your bed if
you have slept in one, and otherwise the spot the world was created around. It is
the same place you come back to when you die.

## Breaking and placing

Hold <kbd>Q</kbd> to break. How long it takes depends on the block and on what is
in your hand: the right tool is between two and six times faster than a fist, and
a few blocks will not give you anything at all without one — see
[Mining speed and tools](mechanics/mining.html).

<kbd>E</kbd> places the block you are holding against the face you are pointing
at. You cannot place a block inside yourself, and you cannot place one in the air
with nothing to attach to.

## Pausing, saving and leaving

<kbd>Esc</kbd> pauses. The game **does not save every tick** — it saves when a
chunk is unloaded, when you choose *Save* in the pause menu, and when you quit.
Quitting from the pause menu always saves first, so the honest way to stop
playing is <kbd>Esc</kbd> and then *Quit*.

Worlds, settings, screenshots and replays live in `/sd/synthminer`, which is not
the folder the app installs into — reinstalling or updating the game cannot touch
them.
