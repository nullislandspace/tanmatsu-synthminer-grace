---
title: Health, falling and death
summary: Ten hearts, three free blocks, and a death that costs you almost nothing.
---

You have **10 hearts — 20 points of health.** Two things in this version of the game can
take it: **falling**, and **starving**.

There are no monsters yet. Animals do not fight back. Water does not drown you. Lava does
not exist.

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

<p class="note"><b>The HOME mark on the compass is where you will wake up.</b> It is your
bed if you have slept in one, and the world's spawn column otherwise. Sleeping in a bed
moves it — see <a href="../devices/bed.html">Bed</a>.</p>
