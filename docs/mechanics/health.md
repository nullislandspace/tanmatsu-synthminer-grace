---
title: Health, falling and death
summary: Ten hearts, three free blocks, what can take them, and a death that costs you almost nothing.
---

You have **10 hearts — 20 points of health.** Four things can take it: **falling**,
**starving**, **hostile mobs**, and **lava**.

The last two are new in v0.3.0, and both of them are found only in the
[Deep Mine](../world/deep-mine.html).

**Animals still do not fight back.** Nothing on the surface of the overworld can hurt you,
and water does not drown you.

## Fall damage

**Three blocks are free.** Every block past the third costs **one point — half a heart.**

| Fall | Damage |
|---|---|
| 1–3 blocks | Nothing |
| 4 blocks | 1 point (half a heart) |
| 8 blocks | 5 points |
| 13 blocks | 10 points — half your health |
| **23 blocks** | **20 points — fatal from full health** |

<p class="note"><b>Three, not two, for a reason.</b> Your jump reaches 1.33 blocks, so you
can climb exactly one block. If two blocks hurt, then hopping down off anything you could
hop up onto would cost you health — which would be miserable.</p>

### It is measured by height, not by speed

The fall is counted from **the highest your feet have been since they last left the ground**
— and that has two consequences in your favour:

- **A ledge breaks a fall.** Landing half way down a cliff and setting off again is two
  short falls, not one long one. Digging a shelf into the side of a shaft genuinely works.
- **A jump off a cliff counts from the apex**, not from the lip you jumped off. That is a
  third of a block in your favour.

A half-block overhang counts: falling three and a half blocks hurts, falling exactly three
does not.

### Water cancels everything

**Any fall into water costs nothing, from any height.** Water is tested before the height
is, so a dive from the top of the world into a lake is free.

This is the single most useful fact in the game:

- a pool at the foot of a cliff is a lift;
- a [bucket](../tools/bucket.html) emptied at the bottom of a shaft is a way down it;
- a waterfall is a staircase.

Carry a bucket.

## Mobs

Only in the [Deep Mine](../world/deep-mine.html), and only from the two kinds there:

| | Damage per swing | How often |
|---|---|---|
| [**Zombie**](../mobs/zombie.html) | **10** — five hearts | Once a second |
| [**Skeleton**](../mobs/skeleton.html) | **8** — four hearts | Once a second |

**Unarmoured, a zombie kills you in two swings and a skeleton in three.** They notice you
at 16 blocks and walk to within a swing. They are slower than you, so the first answer to
a mob is still to not be where it is — but the second answer is now
[armour](../tools/armour.html), and it is a bigger one than it used to be. The HUD says
**"Zombie hits you"** rather leaving you to work it out.

### Armour

Armour is a **percentage off the blow**, added across everything you are wearing: 45% for
a full iron set, **90% for a full titanium set**. A zombie's 10 lands as 6 through iron
and as **1 — half a heart** — through titanium.

Two limits worth knowing:

- **A blow never rounds down to nothing.** Half a heart is the least armour can turn a hit
  into, at any level of gear.
- **Armour only covers creature blows.** Lava, falls and starving are untouched by what
  you are wearing.

Every piece spends one use per blow it stands, so armour runs out. See
[Armour](../tools/armour.html).

## Lava

**Standing in lava costs 2 health — one heart — every half second.** Full health to dead in
five seconds, and lava does not slow you down or push you out: it is the floor, on fire.

The HUD says **"The lava burns"**. See [Lava](../world/lava.html).

## Starving

At **zero** drumsticks you lose a point of health every 4 seconds until you are dead — about
**80 seconds** from full health. There is no floor it stops at.

See [Hunger and saturation](hunger.html).

## Healing

Health comes back **by itself**: half a heart every 4 seconds, as long as your hunger is at
**9 drumsticks or more**.

It is expensive. Each half-heart costs one and a half points of hunger, so healing from one
heart to ten eats **more than a full hunger bar**. A bad landing costs twice — the hearts,
and then the dinner.

Below 9 drumsticks you do not heal at all. This is what the last drumstick is for.

## Dying

When your health reaches zero:

| | |
|---|---|
| You wake up **at your bed**, or at the world's original spawn if you have never slept in one |
| **With half your health** — 5 hearts |
| **With your whole inventory.** Nothing is dropped, nothing is scattered, nothing is lost |
| Your **hunger is refilled** |

That is it. There is no penalty beyond the walk back to wherever you were.

The inventory is deliberate: this game does not scatter your things on the ground when you
die and it never will. And hunger going back to full is not generosity — waking up starving
would be waking up eighty seconds from dying again, which is a loop rather than a penalty.

**Dying in the Deep Mine adds one thing: you go home.** There is no bed down there and no
spawn point of its own, so you wake up in the **overworld**, at your bed or at the world's
original spawn, with your whole inventory. The cave you died in stays as you left it — the
mobs you killed stay dead — and the [mine shaft](../devices/mine-shaft.html) you crossed to
get there is still standing.

<p class="note"><b>The HOME mark on the compass is where you will wake up.</b> It is your
bed if you have slept in one, and the world's spawn column otherwise. Sleeping in a bed
moves it — see <a href="../devices/bed.html">Bed</a>.</p>
